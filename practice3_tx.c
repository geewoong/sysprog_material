#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define GPIO_TX "/dev/gpio26"
#define BIT_DELAY_MS 100

unsigned char bits[40] = {
    0,1,0,0,1,0,0,0,   // H
    0,1,0,0,0,1,0,1,   // E
    0,1,0,0,1,1,0,0,   // L
    0,1,0,0,1,1,0,0,   // L
    0,1,0,0,1,1,1,1    // O
};

void write_bit(int fd, int bit) {
    const char *val = bit ? "1" : "0";
    write(fd, val, 1);
    usleep(BIT_DELAY_MS * 1000);
}

int main() {
    int fd = open(GPIO_TX, O_WRONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Start bit: 0xAA = 10101010
    unsigned char start_byte = 0xAA;
    for (int i = 7; i >= 0; i--) {
        int bit = (start_byte >> i) & 1;
        write_bit(fd, bit);
    }

    // Send actual data bits
    for (int i = 0; i < 40; ++i) {
        write_bit(fd, bits[i]);
    }

    close(fd);
    return 0;
}
