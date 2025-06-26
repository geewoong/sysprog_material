#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEV_MOSI "/dev/gpio27"  // Master Out, Slave In
#define DEV_MISO "/dev/gpio17"  // Master In, Slave Out
#define DEV_CLK  "/dev/gpio22"  // Clock

#define BIT_DELAY_US 10000 

void send_bit(int fd_mosi, int fd_clk, int bit) {
    const char *val = bit ? "1" : "0";
    write(fd_mosi, val, 1);
    usleep(BIT_DELAY_US / 4);

    write(fd_clk, "1", 1);
    usleep(BIT_DELAY_US / 2);

    write(fd_clk, "0", 1);
    usleep(BIT_DELAY_US / 4);
}

int recv_bit(int fd_miso) {
    char val;
    lseek(fd_miso, 0, SEEK_SET);
    read(fd_miso, &val, 1);
    return (val == '1') ? 1 : 0;
}

int main() {
    int fd_mosi = open(DEV_MOSI, O_WRONLY);
    int fd_clk  = open(DEV_CLK,  O_WRONLY);
    int fd_miso = open(DEV_MISO, O_RDONLY);
    
    if (fd_mosi < 0 || fd_clk < 0 || fd_miso < 0) {
        perror("open");
        return 1;
    }

    const char *msg = "HELLO";
    for (int i = 0; i < strlen(msg); ++i) {
        unsigned char tx = msg[i];
        unsigned char rx = 0;

        for (int b = 7; b >= 0; --b) {
            int tx_bit = (tx >> b) & 1;
            send_bit(fd_mosi, fd_clk, tx_bit);

            usleep(BIT_DELAY_US / 8); 
            int rx_bit = recv_bit(fd_miso);
            rx |= (rx_bit << b);

            printf("TX: %d, RX: %d\n", tx_bit, rx_bit);
        }
        printf("TX char: %c, RX char: %c (0x%02X)\n", tx, rx, rx);
    }

    close(fd_mosi);
    close(fd_clk);
    close(fd_miso);
    return 0;
}
