#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <errno.h>

#define DEV_MOSI "/dev/gpio26"
#define DEV_MISO "/dev/gpio20"
#define DEV_CLK  "/dev/gpio21"

#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)

#define STABLE_DELAY_US 500

int fd_mosi, fd_miso, fd_clk;
volatile sig_atomic_t irq_flag = 0;

void sigio_handler(int signo) {
    irq_flag = 1;
}

int read_gpio(int fd) {
    char val;
    lseek(fd, 0, SEEK_SET);
    read(fd, &val, 1);
    return (val == '1') ? 1 : 0;
}

void write_gpio(int fd, int val) {
    const char *s = val ? "1" : "0";
    write(fd, s, 1);
}

int main() {
	
    fd_mosi = open(DEV_MOSI, O_RDONLY);
    fd_miso = open(DEV_MISO, O_WRONLY);
    fd_clk = open(DEV_CLK, O_RDONLY | O_NONBLOCK);  // IRQ ???

    if (fd_mosi < 0 || fd_miso < 0 || fd_clk < 0) {
        perror("open");
        return 1;
    }
    
    signal(SIGIO, sigio_handler);
    fcntl(fd_clk, F_SETOWN, getpid());
    fcntl(fd_clk, F_SETFL, O_ASYNC | O_NONBLOCK);
    
    if (ioctl(fd_clk, GPIO_IOCTL_ENABLE_IRQ, 0) < 0) {
        perror("ioctl");
        return 1;
    }

    const char *reply = "ALOHA";
    int idx = 0;
    int reply_len = strlen(reply);

    printf("Waiting for Master\n");

	while (1) {
        unsigned char tx = reply[idx % reply_len];
        unsigned char rx = 0;
        int next_tx_bit = (tx >> 7) & 1;
        write_gpio(fd_miso, next_tx_bit);  // ?? ? ?? ??

        for (int b = 7; b >= 0; --b) {
            while (!irq_flag)
                usleep(5);
            irq_flag = 0;

            char dummy[8];
            lseek(fd_clk, 0, SEEK_SET);
            read(fd_clk, dummy, sizeof(dummy));

            int bit = read_gpio(fd_mosi);
            rx |= (bit << b);

            if (b > 0) {
                next_tx_bit = (tx >> (b - 1)) & 1;
                write_gpio(fd_miso, next_tx_bit);
                usleep(STABLE_DELAY_US);  // ?? ???
            }
        }

        printf("Received: %c (0x%02X), Sent: %c\n", rx, rx, tx);
        idx++;
    }

    close(fd_mosi);
    close(fd_miso);
    close(fd_clk);
    return 0;
}
