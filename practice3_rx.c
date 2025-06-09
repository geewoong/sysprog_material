#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define GPIO_RX "/dev/gpio17"
#define BIT_DELAY_MS 100
#define TOTAL_BITS 40

char bits_to_char(unsigned char *bits) {
    char result = 0;
    for (int i = 0; i < 8; ++i) {
        result <<= 1;
        result |= bits[i] & 1;
    }
    return result;
}

int read_bit(int fd) {
    char buf[2] = {0};
    lseek(fd, 0, SEEK_SET);
    read(fd, buf, 1);
    return (buf[0] == '1') ? 1 : 0;
}

int main() {
    int fd = open(GPIO_RX, O_RDONLY);

    unsigned char bits[TOTAL_BITS] = {0};
    unsigned char sync_bits[8] = {0};
    int synced = 0;

    printf("Waiting for start bit pattern (10101010)...\n");

    while (!synced) {
        for (int i = 0; i < 7; ++i)
            sync_bits[i] = sync_bits[i+1];
        sync_bits[7] = read_bit(fd);
        usleep(BIT_DELAY_MS * 1000);

        unsigned char pattern = 0;
        for (int i = 0; i < 8; ++i) {
            pattern <<= 1;
            pattern |= sync_bits[i];
        }

        if (pattern == 0xAA) {
            synced = 1;
            printf("Start bit detected!\n");
        }
    }

    // Read 40 bits of actual data
    for (int i = 0; i < TOTAL_BITS; ++i) {
        bits[i] = read_bit(fd);
        printf("Bit %2d: %d\n", i, bits[i]);
        usleep(BIT_DELAY_MS * 1000);
    }
    close(fd);

    printf("Received: ");
    for (int i = 0; i < TOTAL_BITS; i += 8) {
        char c = bits_to_char(&bits[i]);
        printf("%c", c);
    }
    printf("\n");

    return 0;
}

