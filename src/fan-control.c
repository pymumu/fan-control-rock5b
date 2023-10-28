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
#include "lib/tiny-json.h"

#define TMP_BUFF_LEN_32 32
#define MAX_CONF_FILE_SIZE 4096

int pidfile_fd = 0;
int pwmchip_id = -1;
int pwmchip_gpio_id = 0;
int pwm_period = 10000;
int fan_mode = 0;

#define DEFAULT_PID_PATH "/run/fan-control.pid"
#define DEFAULT_CONF_PATH "/etc/fan-control.json"

#define FAN_PWM_PATH "/sys/devices/platform/fd8b0010.pwm/pwm"
#define TEMP_PATH "/sys/class/thermal/thermal_zone0/temp"

struct temp_map_struct
{
    int speed;
    int temp;
    int duty;
    int duration;
};

struct temp_map_struct default_temp_map[] = {
    {0, 40, 0, 20},
    {1, 44, 5500, 25},
    {2, 49, 6000, 35},
    {3, 54, 7000, 45},
    {4, 59, 8000, 60},
    {5, 64, 9000, 120},
    {6, 67, 10000, 180},
};

int default_temp_map_size = sizeof(default_temp_map) / sizeof(struct temp_map_struct);
struct temp_map_struct *temp_map = default_temp_map;
int temp_map_size = sizeof(default_temp_map) / sizeof(struct temp_map_struct);

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

int write_pwmchip_value(int chipId, const char *key, const char *value)
{
    char file[1024];
    snprintf(file, 1024, "%s/pwmchip%d/%s", FAN_PWM_PATH, chipId, key);
    return write_value(file, value);
}

int write_pwmchip_pwm_value(int chipId, int pwm, const char *key, const char *value)
{
    char file[1024];
    snprintf(file, 1024, "%s/pwmchip%d/pwm%d/%s", FAN_PWM_PATH, chipId, pwm, key);
    return write_value(file, value);
}

int write_speed(int speed)
{
    if (speed >= temp_map_size)
    {
        return -1;
    }

    char buffer[16];
    snprintf(buffer, 15, "%d", temp_map[speed].duty);
    if (fan_mode == 0)
    {
        return write_pwmchip_pwm_value(pwmchip_id, pwmchip_gpio_id, "duty_cycle", buffer);
    }
    else if (fan_mode == 1)
    {
        return write_value("/sys/devices/platform/pwm-fan/hwmon/hwmon8/pwm1", buffer);
    }

    return -1;
}

int set_speed(int speed)
{
    static int last_speed = -1;
    int ret = 0;
    if (speed < -1 || speed >= temp_map_size)
    {
        return 0;
    }

    if (last_speed == speed)
    {
        return 0;
    }

    if (last_speed <= 0 && speed > 0)
    {
        write_speed(temp_map_size - 1);
        usleep(100000);
    }

    ret = write_speed(speed);
    last_speed = speed;
    return ret;
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
        if (temperature > temp_map[i].temp)
        {
            speed = temp_map[i].speed;
            if (last_speed < speed)
            {
                count = temp_map[i].duration;
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
                "  -p       specify a pid file path (default: /run/fan-control.pid)\n"
                "  -s [0-6] set fan speed.\n"
                "  -h       show help message.\n"
                "\n";
    printf("%s", msg);
}

int init_pwm_gpio_by_ids(int chipId, int pwmId)
{
    int ret = 0;
    char max_speed[16];

    snprintf(max_speed, 15, "%d", temp_map[temp_map_size - 1].duty);
    ret = write_pwmchip_value(chipId, "export", "0");
    if (ret < 0 && errno != EBUSY)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        return -1;
    }

    ret = write_pwmchip_pwm_value(chipId, pwmId, "duty_cycle", "0");
    if (ret < 0 && errno != EINVAL)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        goto do_unexport;
    }

    ret = write_pwmchip_pwm_value(chipId, pwmId, "period", max_speed);
    if (ret < 0)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        goto do_unexport;
    }

    ret = write_pwmchip_pwm_value(chipId, pwmId, "polarity", "normal");
    if (ret < 0)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        goto do_unexport;
    }

    ret = write_pwmchip_pwm_value(chipId, pwmId, "enable", "1");
    if (ret < 0)
    {
        printf("Failed to export GPIO, %s\n", strerror(errno));
        goto do_unexport;
    }

    return 0;

do_unexport:
    write_pwmchip_value(chipId, "unexport", "0");
    return -1;
}

int init_pwm_GPIO()
{
    if (pwmchip_id > 0)
    {
        if (init_pwm_gpio_by_ids(pwmchip_id, pwmchip_gpio_id) != 0)
        {
            printf("Failed to init pwmchip%d GPIO %d, %s\n", pwmchip_id, pwmchip_gpio_id, strerror(errno));
            return -1;
        }
    }

    for (int i = 0; i < 6; i++)
    {
        if (init_pwm_gpio_by_ids(i, pwmchip_gpio_id) == 0)
        {
            pwmchip_id = i;
            printf("Found pwmchip%d\n", pwmchip_id);
            fan_mode = 0;
            return 0;
        }
    }

    printf("Failed to init GPIO\n");
    return -1;
}

int init_thermal()
{
    int ret = write_value("/sys/class/thermal/thermal_zone0/policy", "user_space");
    if (ret < 0)
    {
        printf("Failed to set thermal policy, %s\n", strerror(errno));
        return -1;
    }

    ret = write_value("/sys/class/thermal/thermal_zone0/mode", "disabled");
    if (ret < 0)
    {
        printf("Failed to set thermal mode, %s\n", strerror(errno));
        return -1;
    }

    fan_mode = 1;

    return 0;
}

void update_temp_map()
{
    for (int i = 0; i < temp_map_size; i++)
    {
        temp_map[i].duty = temp_map[i].duty * 100 / pwm_period * 255 / 100;
    }
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

int parser_conf_json(const char *data)
{
    char str[MAX_CONF_FILE_SIZE];
    struct temp_map_struct *temp_map_buff = NULL;
    enum
    {
        MAX_FIELDS = 1024
    };
    json_t pool[MAX_FIELDS];

    strncpy(str, data, MAX_CONF_FILE_SIZE - 1);
    json_t const *parent = json_create(str, pool, MAX_FIELDS);
    if (parent == NULL)
    {
        printf("Failed to parse json file.\n");
        goto errout;
    }

    json_t const *pwmchipfield = json_getProperty(parent, "pwmchip");
    if (pwmchipfield != NULL)
    {
        if (json_getType(pwmchipfield) != JSON_INTEGER)
        {
            printf("Invalid pwmchip field.\n");
            goto errout;
        }

        pwmchip_id = json_getInteger(pwmchipfield);
    }

    json_t const *gpiofield = json_getProperty(parent, "gpio");
    if (gpiofield != NULL)
    {
        if (json_getType(gpiofield) != JSON_INTEGER)
        {
            printf("Invalid gpio field.\n");
            goto errout;
        }

        pwmchip_gpio_id = json_getInteger(gpiofield);
    }

    json_t const *periodfield = json_getProperty(parent, "pwm-period");
    if (periodfield != NULL)
    {
        if (json_getType(periodfield) != JSON_INTEGER)
        {
            printf("Invalid period field.\n");
            goto errout;
        }

        pwm_period = json_getInteger(periodfield);
    }

    json_t const *temp_map_array = json_getProperty(parent, "temp-map");
    if (temp_map_array != NULL)
    {
        if (json_getType(temp_map_array) != JSON_ARRAY)
        {
            printf("Invalid temp-map field.\n");
            goto errout;
        }

        int temp_obj_size = 0;
        json_t const *temp_obj;
        for (temp_obj = json_getChild(temp_map_array); temp_obj != 0; temp_obj = json_getSibling(temp_obj))
        {
            temp_obj_size++;
        }

        if (temp_obj_size > 0)
        {
            temp_map_buff = (struct temp_map_struct *)malloc(sizeof(struct temp_map_struct) * temp_obj_size);
            if (temp_map_buff == NULL)
            {
                printf("Failed to malloc temp_map_buff.\n");
                goto errout;
            }
            memset(temp_map_buff, 0, sizeof(struct temp_map_struct) * temp_obj_size);

            int id = 0;
            for (temp_obj = json_getChild(temp_map_array); temp_obj != 0; temp_obj = json_getSibling(temp_obj))
            {
                if (JSON_OBJ != json_getType(temp_obj))
                {
                    continue;
                }

                json_t const *json_temp = json_getProperty(temp_obj, "temp");
                json_t const *json_duty = json_getProperty(temp_obj, "duty");
                json_t const *json_duration = json_getProperty(temp_obj, "duration");

                if (json_getType(json_temp) != JSON_INTEGER)
                {
                    printf("Invalid temp field.\n");
                    goto errout;
                }

                if (json_getType(json_duty) != JSON_INTEGER)
                {
                    printf("Invalid duty field.\n");
                    goto errout;
                }

                if (json_getType(json_duration) != JSON_INTEGER)
                {
                    printf("Invalid duration field.\n");
                    goto errout;
                }

                int temp = json_getInteger(json_temp);
                int duty = json_getInteger(json_duty);
                int duration = json_getInteger(json_duration);

                temp_map_buff[id].speed = id;
                temp_map_buff[id].temp = temp;
                temp_map_buff[id].duty = duty * pwm_period / 100;
                temp_map_buff[id].duration = duration;
                id++;
            }

            temp_map_size = temp_obj_size;
            temp_map = temp_map_buff;
        }
    }

    return 0;

errout:
    if (temp_map_buff != NULL)
    {
        free(temp_map_buff);
    }
    return -1;
}

int load_conf(const char *conf_file)
{
    FILE *fp = NULL;
    char buff[MAX_CONF_FILE_SIZE];

    fp = fopen(conf_file, "r");
    if (fp == NULL)
    {
        printf("Failed to open config file, %s\n", strerror(errno));
        return -1;
    }

    memset(buff, 0, MAX_CONF_FILE_SIZE);
    int len = fread(buff, 1, MAX_CONF_FILE_SIZE, fp);
    if (len <= 0)
    {
        printf("Failed to read config file, %s\n", strerror(errno));
        goto errout;
    }

    if (parser_conf_json(buff) < 0)
    {
        printf("Failed to parser config file.\n");
        goto errout;
    }

    fclose(fp);
    return 0;

errout:
    if (fp != NULL)
    {
        fclose(fp);
    }

    return -1;
}

void display_config()
{
    if (fan_mode == 0)
    {
        printf("pwmchip: %d\n", pwmchip_id);
        printf("gpio: %d\n", pwmchip_gpio_id);
        printf("pwm-period: %d\n", pwm_period);
    }
    printf("temp-map:\n");

    for (int i = 0; i < temp_map_size; i++)
    {
        printf("  speed: %d, temp: %d, duty: %d, duration: %d\n", temp_map[i].speed, temp_map[i].temp, temp_map[i].duty, temp_map[i].duration);
    }
}

int main(int argc, char *argv[])
{
    int fd_temperature = -1;
    char buff[32];
    char pid_file[1024];
    char conf_file[1024] = {0};
    int temperatrue = 0;
    int speed_set = -1;
    int is_daemon = 0;

    int opt;

    while ((opt = getopt(argc, argv, "s:p:c:dh")) != -1)
    {
        switch (opt)
        {
        case 's':
            speed_set = atoi(optarg);
            break;
        case 'p':
            strncpy(pid_file, optarg, sizeof(pid_file) - 1);
            break;
        case 'c':
            strncpy(conf_file, optarg, sizeof(conf_file) - 1);
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

    if (conf_file[0] == 0)
    {
        strncpy(conf_file, DEFAULT_CONF_PATH, sizeof(conf_file) - 1);
    }

    if (load_conf(conf_file) != 0)
    {
        fprintf(stderr, "load config file failed.\n");
        return 1;
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

    if (init_pwm_GPIO())
    {
        if (init_thermal())
        {
            printf("Failed to init thermal.\n");
            return 1;
        }

        update_temp_map();
    }

    display_config();

    if (speed_set != -1)
    {
        printf("Set speed to %d.\n", speed_set);
        if (speed_set < 0 || speed_set >= temp_map_size)
        {
            fprintf(stderr, "speed is invalid.\n");
            return 1;
        }

        if (set_speed(speed_set) != 0)
        {
            printf("Set speed to %d failed.\n", speed_set);
            return 1;
        }

        return 0;
    }

    fd_temperature = open(TEMP_PATH, O_RDONLY);
    if (fd_temperature < 0)
    {
        printf("Failed to open temperature file, %s\n", strerror(errno));
        return -1;
    }

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
