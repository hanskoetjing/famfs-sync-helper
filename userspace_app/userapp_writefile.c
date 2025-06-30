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

#define PAGE_SIZE       4096

#define CACHE_TRASH_SIZE (1 * 1024 * 1024)
#define PAGE_COUNT (1 * 1024 * 1024 / PAGE_SIZE)
#define ARRAY_COUNT (1 * 1024 * 1024 / sizeof(long unsigned int))

void flush_cache() {
    printf("trashing cache\n");
    volatile char *trash = malloc(CACHE_TRASH_SIZE);

    if (!trash) return;

    for (size_t i = 0; i < CACHE_TRASH_SIZE; i += 64){
        trash[i] = i;
        _mm_mfence();
    }

    __asm__ volatile("mfence" ::: "memory");  // barrier biar gak dioptimasi
    for (size_t i = 0; i < CACHE_TRASH_SIZE; i += 64) {
        _mm_clflush((void *)&trash[i]);  // flush per cache line
    }
    __asm__ volatile("mfence" ::: "memory");

    free((void *)trash);
    printf("trashing cache done\n");

}

static __attribute__((always_inline)) inline uint64_t read_tsc_start(void) {
    unsigned int lo, hi;
    __asm__ volatile (
        "cpuid\n\t"        // serialize sebelum RDTSC
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        : "a"(0)
        : "%rbx", "%rcx");
    return ((uint64_t)hi << 32) | lo;
}

static __attribute__((always_inline)) inline uint64_t read_tsc_end(void) {
    uint32_t lo, hi;
    __asm__ volatile (
        "rdtscp\n\t"        // serialize setelah instruksi utama
        "mov %%eax, %0\n\t"
        "mov %%edx, %1\n\t"
        "cpuid\n\t"         // serialize setelah RDTSC
        : "=r"(lo), "=r"(hi)
        :
        : "%rax", "%rbx", "%rcx", "%rdx");
    return ((uint64_t)hi << 32) | lo;
}

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

    //uncached
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    void *uc = mmap(NULL, PAGE_COUNT * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (uc == MAP_FAILED) {
        perror("mmap");
        printf("errono: %d\n", errno);
        close(fd);
        return 1;
    }

    printf("%s\n", argv[1]);

    volatile long unsigned int *uc_int = (volatile long unsigned int *)uc;
    for (int i = 0; i < ARRAY_COUNT; i++) uc_int[i] = rand() % INT64_MAX;
    for (int loop = 0; loop < 10; loop++) {
        unsigned long sum = 0;
        unsigned long tmp = 0;
        for (int i = 0; i < 3; i++) {
            __asm__ volatile("mfence" ::: "memory");
            if(loop == 0) flush_cache();
            bef_uc = read_tsc();
            tmp = uc_int[loop];
            aft_uc = read_tsc();
            __asm__ volatile("mfence" ::: "memory");    
            sum += aft_uc - bef_uc;
        }
        printf(" %d uncached cycle used: %lu. val %lu\n", loop + 1, sum, tmp);
    }

    munmap(uc, PAGE_COUNT * PAGE_SIZE);
    close(fd);
    return 0;
}
