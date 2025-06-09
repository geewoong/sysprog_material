#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define GPIO_RX "/dev/gpio17"
#define DELAY_MS 100

int main() {
    int fd = open(GPIO_RX, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char prev = -1;
    while (1) {
        char buf[2] = {0};
        ssize_t n = read(fd, buf, 1);
        if (n < 0) {
            perror("read");
            break;
        }

        if ((buf[0] == '0' || buf[0] == '1') && buf[0] != prev) {
            printf("RX changed: %c\n", buf[0]);
            prev = buf[0];
        }

        usleep(DELAY_MS * 1000);
    }

    close(fd);
    return 0;
}
