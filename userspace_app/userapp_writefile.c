// based on coba14
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>
#include <x86intrin.h>
#include <string.h>
#include <regex.h>

#define PAGE_SIZE       4096

#define CACHE_TRASH_SIZE (1 * 1024 * 1024)
#define PAGE_COUNT (1 * 1024 * 1024 / PAGE_SIZE)
#define ARRAY_COUNT (1 * 1024 * 1024 / sizeof(long unsigned int))

#define VERSION_SIZE (512 * 1024) - 1
#define VERSION_PAGE_COUNT (VERSION_SIZE / PAGE_SIZE)

static __attribute__((always_inline)) inline uint64_t read_tsc(void) {
    unsigned int aux;
    return __rdtscp(&aux);
}


int match_filename(char *filename, char *entry) {
    char front[] = "^";
    char back[] = ",[0-9]*[0-9]*$";
    char *match_criterion = (char *)malloc(sizeof(char) * (sizeof(front) + sizeof(filename) + sizeof(back)));
    snprintf(match_criterion, sizeof(front) + sizeof(filename) + sizeof(back), "%s%s%s", front, filename, back);

    regex_t regex;
    regcomp(&regex, match_criterion, REG_EXTENDED);
    int match = regexec(&regex, entry, 0, NULL, 0);
    free(match_criterion);
    return !match;
}

char **get_entries(char *version_entries, char *filename, int *num_of_entries, int *position) {
    char *v_entries = (char *)malloc(VERSION_SIZE);
    snprintf(v_entries, VERSION_SIZE, "%s", version_entries);
    char *v_entry = strtok(v_entries, ";");
    char **entries = malloc(128 * sizeof(char *));
    while(v_entry != NULL) {
        entries[*num_of_entries] = malloc(sizeof(char) * strlen(v_entry));
        snprintf(entries[*num_of_entries], strlen(v_entry) + 1, "%s", v_entry);
        if (match_filename(filename, v_entry)) {
            *position = *num_of_entries;
        }
        v_entry = strtok(NULL, ";");
        (*num_of_entries)++;
    }
    free(v_entries);
    return entries;
}

int write_version(char *version_filename, char *arg_file_name) {
        int version_fd = open(version_filename, O_RDWR);
        if (version_fd < 0) {
            perror("open version file");
            return 1;
        }

        void *version_map = mmap(NULL, VERSION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, version_fd, 0);
        if (version_map == MAP_FAILED) {
            perror("mmap version");
            printf("errno: %d\n", errno);
            close(version_fd);
            return 1;
        }

        volatile char *version_entries = (volatile char *)version_map;
        
        char *filepath_arg = (char *)malloc(strlen(arg_file_name) + 1);
        strcpy(filepath_arg, arg_file_name);
        char *filepath = strtok(filepath_arg, "/");
        char *filename = (char *)malloc(strlen(arg_file_name) + 1);
        while(filepath != NULL) {
            snprintf(filename, strlen(filepath) + 1, "%s", filepath);
            filepath = strtok(NULL, "/");
        }
        int version_entries_length = strlen(version_entries);

        if (version_entries_length > 0) {
            int num_of_entries = 0;
            int position = -1;
            char **entries = get_entries(version_entries, filename, &num_of_entries, &position);            
            
            if (position == -1) { //new file
                char * new_entry = malloc(sizeof(char) * 65);
                snprintf(new_entry, 64, "%s,%d;", filename,1);
                strcat(version_entries, new_entry);
                free(new_entry);
            } else { //existing file
                char *version = strtok(entries[position], ",");
                version = strtok(NULL, ",");
                long int file_version = strtol(version, NULL, 10);
                snprintf(entries[position], VERSION_SIZE, "%s,%d", filename,++file_version);
                
                char *new_version_entries = (char *)malloc(VERSION_SIZE);
                for (int i = 0; i < num_of_entries; i++) {
                    strcat(new_version_entries, strcat(entries[i], ";"));
                }
                snprintf(version_entries, VERSION_SIZE, "%s", new_version_entries);
                free(new_version_entries);
            }
        } else {
            char *ver_entry = (char *)malloc(65);
            snprintf(ver_entry, 64, "%s,%d;", filename,1);
            snprintf(version_entries, 64, "%s", ver_entry);
            free(ver_entry);
        }
        munmap(version_map, VERSION_SIZE);
        close(version_fd);
        free(filepath_arg);
        free(filename);

        return 0;
}

int main(int argc, char *argv[]) {
    
    srand(time(NULL));
    uint64_t bef_uc, aft_uc;

    if (argc < 2) {
        perror("usage: <data_file_name> <version_file_name>");
        return 1;
    }

    printf("File name: %s\n", argv[1]);
    printf("Version file name: %s\n", argv[2]);


    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    void *uc = mmap(NULL, PAGE_COUNT * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (uc == MAP_FAILED) {
        perror("mmap");
        printf("errno: %d\n", errno);
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
    write_version(argv[2], argv[1]);

    return 0;
}
