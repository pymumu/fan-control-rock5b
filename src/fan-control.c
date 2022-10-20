/*
MIT License

Copyright (c) 2022 Nick Peng

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define TMP_BUFF_LEN_32 32
int pidfile_fd = 0;
#define DEFAULT_PID_PATH "/run/fan-control.pid"

int pwm_speed_map[] = {
    0,
    5500,
    6000,
    7000,
    8000,
    9000,
    10000,
};

int temp_map[][3] = {
    {40, 0, 20},
    {44, 1, 25},
    {49, 2, 35},
    {54, 3, 45},
    {59, 4, 60},
    {64, 5, 120},
    {67, 6, 180},
};

int temp_map_size = 6;

int mapsize = sizeof(pwm_speed_map) / sizeof(int);

int write_value(const char *file, const char *value)
{
    int fd;
    fd = open(file, O_WRONLY);
    if (fd == -1)
    {
        return -1;
    }

    if (write(fd, value, strnlen(value, 1024)) == -1)
    {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void write_speed(int speed)
{
    char buffer[16];
    snprintf(buffer, 15, "%d", pwm_speed_map[speed]);
    write_value("/sys/devices/platform/fd8b0010.pwm/pwm/pwmchip1/pwm0/duty_cycle", buffer);
}

void set_speed(int speed)
{
    static int last_speed = -1;
    if (speed < -1 || speed >= mapsize)
    {
        return;
    }

    if (last_speed == speed)
    {
        return;
    }

    if (last_speed <= 0 && speed > 0)
    {
        write_speed(mapsize - 1);
        usleep(100000);
    }
    write_speed(speed);
    last_speed = speed;
}

int get_speed(int temperature)
{
    int i = 0;
    int speed = 0;
    static int last_speed = -1;
    static int last_temperature = -1;
    static int count = 0;

    for (i = temp_map_size - 1; i >= 0; i--)
    {
        if (temperature > temp_map[i][0])
        {
            speed = temp_map[i][1];
            if (last_speed < speed)
            {
                count = temp_map[i][2];
            }

            break;
        }
    }

    if (speed < last_speed)
    {
        count--;
    }
    else if (temperature > last_temperature)
    {
        count++;
    }

    if (count <= 0 || last_speed == -1 || last_speed < speed)
    {
        last_speed = speed;
    }

    last_temperature = temperature;
    return last_speed;
}

void show_help(void)
{
    char *msg = "PI custom fan control service.\n"
                "Usage: fan-control [option]\n"
                "Options:\n"
                "  -d       start as a daemon service.\n"
                "    -p       specify a pid file path (default: /run/fan-control.pid)\n"
                "  -s [0-6] set fan speed.\n"
                "  -h       show help message.\n"
                "\n";
    printf("%s", msg);
}

int init_GPIO()
{
    int ret = 0;
    char max_speed[16];
    snprintf(max_speed, 15, "%d", pwm_speed_map[mapsize - 1]);

    ret = write_value("/sys/devices/platform/fd8b0010.pwm/pwm/pwmchip1/export", "0");
    if (ret < 0 && errno != EBUSY)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        return -1;
    }

    ret = write_value("/sys/devices/platform/fd8b0010.pwm/pwm/pwmchip1/pwm0/duty_cycle", "0");
    if (ret < 0 && errno != EINVAL)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        return -1;
    }

    ret = write_value("/sys/devices/platform/fd8b0010.pwm/pwm/pwmchip1/pwm0/period", max_speed);
    if (ret < 0)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        return -1;
    }

    ret = write_value("/sys/devices/platform/fd8b0010.pwm/pwm/pwmchip1/pwm0/polarity", "normal");
    if (ret < 0)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        return -1;
    }

    ret = write_value("/sys/devices/platform/fd8b0010.pwm/pwm/pwmchip1/pwm0/enable", "1");
    if (ret < 0)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int create_pid_file(const char *pid_file)
{
    int fd = 0;
    int flags = 0;
    char buff[TMP_BUFF_LEN_32];

    /*  create pid file, and lock this file */
    fd = open(pid_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        fprintf(stderr, "create pid file failed, %s\n", strerror(errno));
        return -1;
    }

    flags = fcntl(fd, F_GETFD);
    if (flags < 0)
    {
        fprintf(stderr, "Could not get flags for PID file %s\n", pid_file);
        goto errout;
    }

    flags |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, flags) == -1)
    {
        fprintf(stderr, "Could not set flags for PID file %s\n", pid_file);
        goto errout;
    }

    if (lockf(fd, F_TLOCK, 0) < 0)
    {
        fprintf(stderr, "Server is already running.\n");
        goto errout;
    }

    snprintf(buff, TMP_BUFF_LEN_32, "%d\n", getpid());

    if (write(fd, buff, strnlen(buff, TMP_BUFF_LEN_32)) < 0)
    {
        fprintf(stderr, "write pid to file failed, %s.\n", strerror(errno));
        goto errout;
    }

    if (pidfile_fd > 0)
    {
        close(pidfile_fd);
    }

    pidfile_fd = fd;

    return 0;
errout:
    if (fd > 0)
    {
        close(fd);
    }
    return -1;
}

int main(int argc, char *argv[])
{
    int fd_temperature = -1;
    char buff[32];
    char pid_file[1024];
    int temperatrue = 0;
    int speed_set = -1;
    int is_daemon = 0;

    int opt;

    while ((opt = getopt(argc, argv, "s:p:dh")) != -1)
    {
        switch (opt)
        {
        case 's':
            speed_set = atoi(optarg);
            if (speed_set < 0 || speed_set >= mapsize)
            {
                fprintf(stderr, "speed is invalid.\n");
                return 1;
            }
            break;
        case 'p':
            strncpy(pid_file, optarg, sizeof(pid_file) - 1);
            break;
        case 'd':
            is_daemon = 1;
            break;
        case 'h':
            show_help();
            return 1;
            break;
        default:
            show_help();
            return 1;
        }
    }

    if (is_daemon)
    {
        if (daemon(0, 0) != 0)
        {
            printf("run daemon failed.\n");
            return 1;
        }

        if (pid_file[0] == '\0')
        {
            strncpy(pid_file, DEFAULT_PID_PATH, sizeof(pid_file) - 1);
        }

        if (create_pid_file(pid_file))
        {
            return 1;
        }
    }

    if (init_GPIO())
    {
        return 0;
    }

    if (speed_set != -1)
    {
        printf("Set speed to %d.\n", speed_set);
        set_speed(speed_set);
        return 0;
    }

    fd_temperature = open("/sys/class/hwmon/hwmon1/temp1_input", O_RDONLY);

    while (1)
    {
        sleep(1);

        lseek(fd_temperature, 0, SEEK_SET);
        if (read(fd_temperature, &buff, 32) <= 0)
        {
            perror("read");
            goto errout;
        }

        temperatrue = atoi(buff);
        speed_set = get_speed(temperatrue / 1000);
        set_speed(speed_set);

        if (!is_daemon)
        {
            printf("speed:%d  temperatrue:%d\n", speed_set, temperatrue);
        }
    }
    close(fd_temperature);
    return 0;

errout:
    if (fd_temperature > 0)
    {
        close(fd_temperature);
        fd_temperature = -1;
    }

    return 1;
}
