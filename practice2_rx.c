#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>

#define GPIO_RX "/dev/gpio17"
#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)
#define GPIO_IOCTL_DISABLE_IRQ _IOW(GPIO_IOCTL_MAGIC, 2, int)

int fd;

void sigio_handler(int signo) {
    char buf[2] = {0};
    ssize_t n = read(fd, buf, 1);

    printf("RX interrupt : %c, handle interrupt\n", buf[0]);
    fflush(stdout);
    usleep(300 * 1000);

}

int main() {
    int cnt=0;
    fd = open(GPIO_RX, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    signal(SIGIO, sigio_handler);
    fcntl(fd, F_SETOWN, getpid());
    fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK);

    ioctl(fd, GPIO_IOCTL_ENABLE_IRQ, 0);

    printf("Waiting for GPIO interrupts on %s...\n", GPIO_RX);
    while (1) {
        printf("Processing %d\n", cnt++);
        usleep(100*1000);
    }
    
    ioctl(fd, GPIO_IOCTL_DISABLE_IRQ, 0);
    close(fd);
    return 0;
}
