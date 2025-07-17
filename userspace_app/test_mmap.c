#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

int main() {
    int fd = open("/dev/cxl_mmap", O_RDWR | O_SYNC);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    off_t phy_addr = 0x0;
    size_t page_size = 4096;

    void *map_result = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phy_addr);

    if (map_result == MAP_FAILED) {
        perror("mmap");
        printf("errono: %d\n", errno);
        close(fd);
        return 1;
    }

    printf("meow cxl addr %p\n", map_result);

    volatile int *lala = (volatile int *)map_result;
    printf("initial cxl val: %d\n", *lala);

    *lala = 0x0001;

    printf("meow cxl %d \n", *lala);
    
    munmap(map_result, page_size);
    close(fd);
    return 0;
}