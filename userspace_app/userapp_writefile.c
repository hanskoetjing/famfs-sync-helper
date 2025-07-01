// based on coba14
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <emmintrin.h> 
#include <time.h>
#include <x86intrin.h>
#include <string.h>

#define PAGE_SIZE       4096

#define CACHE_TRASH_SIZE (1 * 1024 * 1024)
#define PAGE_COUNT (1 * 1024 * 1024 / PAGE_SIZE)
#define ARRAY_COUNT (1 * 1024 * 1024 / sizeof(long unsigned int))

#define VERSION_SIZE (500 * 1024)
#define VERSION_PAGE_COUNT (VERSION_SIZE / PAGE_SIZE)

static __attribute__((always_inline)) inline uint64_t read_tsc(void) {
    unsigned int aux;
    return __rdtscp(&aux);
}


int main(int argc, char *argv[]) {
    
    srand(time(NULL));
    uint64_t bef_uc, aft_uc;

    if (argc < 2) {
        perror("need file name");
        return 1;
    }

    printf("File name: %S\n", argv[1]);
    printf("Version file name: %S\n", argv[2]);


    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    int version_fd = open(argv[2], O_RDWR);
    if (version_fd < 0) {
        perror("open version file");
        close(fd);
        return 1;
    }

    void *version_map = mmap(NULL, VERSION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, version_fd, 0);
    if (version_map == MAP_FAILED) {
        perror("mmap version");
        printf("errno: %d\n", errno);
        close(version_fd);
        close(fd);
        return 1;
    }

    volatile char *version_entry = (volatile char *)version_map;
    char *filepath_arg = (char *)malloc(strlen(argv[1]) + 1);
    strcpy(filepath_arg, argv[1]);
    char *filepath = strtok(filepath_arg, "/");
    char *filename = (char *)malloc(strlen(argv[1]) + 1);
    while(filepath != NULL) {
        strncpy(filename, filepath, strlen(filepath));
        snprintf(filename, strlen(filepath), "%s", filepath);
        filepath = strtok(NULL, "/");
    }
    if (strlen(version_entry > 0)) {

    } else {

        strcpy(version_entry, "");
    }

    munmap(version_map, VERSION_SIZE);
    close(version_fd);

    void *uc = mmap(NULL, PAGE_COUNT * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (uc == MAP_FAILED) {
        perror("mmap");
        printf("errno: %d\n", errno);
        munmap(version_map, VERSION_SIZE);
        close(version_fd);
        close(fd);
        return 1;
    }

    volatile long unsigned int *uc_int = (volatile long unsigned int *)uc;
    for (int i = 0; i < ARRAY_COUNT; i++) uc_int[i] = rand() % INT32_MAX;
    for (int loop = 0; loop < 10; loop++) {
        unsigned long sum = 0;
        unsigned long tmp = 0;
        for (int i = 0; i < 3; i++) {
            __asm__ volatile("mfence" ::: "memory");
            bef_uc = read_tsc();
            tmp = uc_int[loop];
            aft_uc = read_tsc();
            __asm__ volatile("mfence" ::: "memory");    
            sum += aft_uc - bef_uc;
        }
        printf(" %d cycle used: %lu. written value: %lu\n", loop + 1, sum, tmp);
    }

    munmap(uc, PAGE_COUNT * PAGE_SIZE);
    close(fd);
    return 0;
}
