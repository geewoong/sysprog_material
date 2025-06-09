#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define GPIO_TX "/dev/gpio26"
#define DELAY_MS 500
#define ITERATIONS 50

int main() {
    int fd = open(GPIO_TX, O_WRONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        const char *val = (i % 2 == 0) ? "1" : "0";
        if (write(fd, val, 1) < 0) {
            perror("write");
            close(fd);
            return 1;
        }
        printf("TX: %s\n", val);
        usleep(DELAY_MS * 1000);
    }

    close(fd);
    return 0;
}
