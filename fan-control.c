#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define START_TEMP 49000
#define STOP_TEMP 44000

#define PWM_PIN 1

int pwm_speed_map[] = {
    0, 
    800,
    900,
    950,
    1000,
    1010,
    1024,
};

int temp_map[][3] = {
    {40, 0, 10},
    {44, 2, 15},
    {46, 3, 45}, 
    {50, 4, 60}, 
    {55, 5, 120}, 
    {60, 6, 180}, 
};

int temp_map_size = 6;

int mapsize = sizeof(pwm_speed_map) / sizeof(int);

void set_speed(int speed) 
{
    static int last_speed = -1;
    if (speed < -1 || speed >= mapsize ) {
        return ;
    }

    if (last_speed == speed) {
        return;
    }

    if (last_speed <= 0 && speed > 0) {
        pwmWrite(PWM_PIN, 1024);
        usleep(100000);
    }
    pwmWrite(PWM_PIN, pwm_speed_map[speed]);
    last_speed = speed;
}



int get_speed(int temperature)
{
    int i = 0;
    int speed = 0;
    static int last_speed = -1;
    static int last_temperature = -1;
    static int count = 0;

    for (i = temp_map_size - 1; i >= 0; i--) {
        if (temperature > temp_map[i][0]) {
            speed = temp_map[i][1];
            if (last_speed < speed) {
                count = temp_map[i][2];
            }
            
            break;
        }
    }

    if (speed < last_speed) {
        count--;
    } else if (temperature > last_temperature) {
        count++;
    }

    if (count <= 0 || last_speed == -1 || last_speed < speed) {
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
        "  -s [0-6] set fan speed.\n"
        "  -h       show help message.\n"
        "\n"
        ;
    printf("%s", msg);
}

int main(int argc, char *argv[])
{
    int fd_temperature = -1;
    char buff[32];
    int temperatrue = 0;
    int speed_set = -1;
    int is_daemon = 0;

    int opt;

    while ((opt = getopt(argc, argv, "s:dh")) != -1) {
        switch (opt) {
        case 's':
            speed_set = atoi(optarg);
            if (speed_set < 0 || speed_set >= mapsize) {
                fprintf(stderr, "speed is invalid.\n");
                return 1;
            }
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

    if (is_daemon) {
        daemon(0, 0);
    }

    if (wiringPiSetup() != 0) {
        perror("wiringPiSetup:");
        return 1;
    }

    pinMode(PWM_PIN, PWM_OUTPUT);

    if (speed_set != -1) {
        printf("Set speed to %d.\n", speed_set);
        set_speed(speed_set);
        return 0;
    }

    fd_temperature = open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY);

    while(1) {
        sleep(1);

        lseek(fd_temperature, 0, SEEK_SET);
        if (read(fd_temperature, &buff, 32) <= 0) {
            perror("read");
            goto errout;
        }	

        temperatrue = atoi(buff);
        speed_set = get_speed(temperatrue / 1000);
        set_speed(speed_set);

        if (!is_daemon) {
            printf("speed:%d  temperatrue:%d\n", speed_set, temperatrue);
        }
    }
    close(fd_temperature);
    return 0; 

errout:
    if (fd_temperature > 0) {
        close(fd_temperature);
        fd_temperature = -1;
    }

    return 1;
}


