#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define TX_DATA "/dev/gpio26"
#define TX_CLK  "/dev/gpio27"
#define DELAY_US 100000 

void send_bit(int fd_data, int fd_clk, int bit) {
    const char *val = bit ? "1" : "0";
    write(fd_data, val, 1);           
    usleep(5000);                     
    write(fd_clk, "1", 1);            
    usleep(100000 - 5000);            
    write(fd_clk, "0", 1);
}


int main() {
    int fd_data = open(TX_DATA, O_WRONLY);
    int fd_clk = open(TX_CLK, O_WRONLY);
    if (fd_data < 0 || fd_clk < 0) {
        perror("open");
        return 1;
    }

    const char *msg = "HELLO";
    for (int i = 0; i < strlen(msg); ++i) {
        unsigned char ch = msg[i];
        for (int b = 7; b >= 0; --b) {
            int bit = (ch >> b) & 1;
            send_bit(fd_data, fd_clk, bit);
            printf("TX: %d\n", bit);
        }
    }

    close(fd_data);
    close(fd_clk);
    return 0;
}
