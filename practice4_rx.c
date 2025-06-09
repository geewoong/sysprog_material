#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>

#define RX_DATA "/dev/gpio17"
#define RX_CLK  "/dev/gpio19"
#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)

int fd_data;
unsigned char bits[64];
int bit_index = 0;

char bits_to_char(unsigned char *bits) {
    char c = 0;
    for (int i = 0; i < 8; ++i)
        c = (c << 1) | (bits[i] & 1);
    return c;
}

void sigio_handler(int signo) {
    char buf[2] = {0};
    lseek(fd_data, 0, SEEK_SET);
    if (read(fd_data, buf, 1) > 0) {
        if (buf[0] == '0' || buf[0] == '1') {
            bits[bit_index++] = (buf[0] == '1') ? 1 : 0;
            printf("RX bit %d: %c\n", bit_index - 1, buf[0]);
        }
    }
}

int main() {
    int fd_clk = open(RX_CLK, O_RDONLY | O_NONBLOCK);
    fd_data = open(RX_DATA, O_RDONLY);
    if (fd_clk < 0 || fd_data < 0) {
        perror("open");
        return 1;
    }

    // register SIGIO handler
    signal(SIGIO, sigio_handler);
    fcntl(fd_clk, F_SETOWN, getpid());
    fcntl(fd_clk, F_SETFL, O_ASYNC | O_NONBLOCK);

    // enable IRQ
    if (ioctl(fd_clk, GPIO_IOCTL_ENABLE_IRQ, 0) < 0) {
        perror("ioctl");
        return 1;
    }

    printf("Waiting for data...\n");
    while (bit_index < 40) pause();

    printf("Received: ");
    for (int i = 0; i < 40; i += 8) {
        printf("%c", bits_to_char(&bits[i]));
    }
    printf("\n");

    close(fd_clk);
    close(fd_data);
    return 0;
}
