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

#define VERSION_SIZE (512 * 1024) - 1
#define FILE_SIZE (1 * 1024 * 1024)

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
    if (strlen(version_entries) > 0) {
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
    } else {
        return NULL;
    }
}

char * normalise_path(char * dir_path) {
    int dir_path_length = strlen(dir_path);
    char *dir_path_ret;
    if (dir_path[dir_path_length - 1] != '/') {
        dir_path_ret = (char *)malloc(sizeof(char) * dir_path_length + 1);
        snprintf(dir_path_ret, dir_path_length + 2, "%s/", dir_path);
    } else {
        dir_path_ret = (char *)malloc(sizeof(char) * dir_path_length);
        snprintf(dir_path_ret, dir_path_length + 1, "%s", dir_path);
    }
    return dir_path_ret;
}

int main(int argc, char **argv) {

    if (argc < 4) {
        perror("Missing arguments. parameter: <version_file> <data_file_dir> <dest_data_dir_file>");
        return 1;
    }

    int version_fd = open(argv[1], O_RDWR);
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

    char *old_version_entries = malloc(VERSION_SIZE);
    snprintf(old_version_entries, VERSION_SIZE, "%s", version_entries);
    printf("waiting for changes\n");
    int i = 0;
    int a = 0, b = 0;
    int num_of_entries = 0;
    int num_of_old_entries = 0;
    char *dir_path = normalise_path(argv[2]);
    char *dest_dir_path = normalise_path(argv[3]);

    while (1) {
        if (strcmp(old_version_entries, version_entries) != 0) {
            printf("old: %s\n", old_version_entries);
            printf("now: %s\n", version_entries);
            
            char **new_entries = get_entries(version_entries, "", &num_of_entries, &b);
            char **old_entries = get_entries(old_version_entries, "", &num_of_old_entries, &b);
            int num_of_changed_file = 0;
            int *changes = (int *)malloc(sizeof(int) * num_of_entries);
            if (old_entries) {
                for (int j = 0; j < num_of_entries; j++) {
                    if (strcmp(new_entries[j], old_entries[j]) != 0) {
                        changes[a] = j;
                        a++;
                    }
                }
                if (num_of_entries > num_of_old_entries) {
                    changes[a] = num_of_entries - 1;
                    a++;
                }
            } else {
                changes[0] = 0;
            }
            
            snprintf(old_version_entries, VERSION_SIZE, "%s", version_entries);
            printf("%s\n", old_version_entries);
            //assuming only one changes at a time
            char *changed_entry = (char *)malloc(sizeof(char) * strlen(new_entries[changes[0]]) + 1);
            snprintf(changed_entry, strlen(new_entries[changes[0]]), "%s", new_entries[changes[0]]);
            char *changed_filename = strtok(changed_entry, ",");
            char *full_path = (char *)malloc(sizeof(char) * strlen(dir_path) + strlen(changed_filename) + 1);
            snprintf(full_path, strlen(dir_path) + strlen(changed_filename) + 1, "%s%s", dir_path, changed_filename);
            printf("%s\n", full_path);
            
            free(new_entries);
            free(old_entries);
            free(changes);
            
            int fd = open(full_path, O_RDWR);
            if (fd < 0) {
                perror("open data file");
                return 1;
            }
            free(full_path);
            
            void *data_map = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (data_map == MAP_FAILED) {
                perror("mmap data");
                printf("errno: %d\n", errno);
                close(version_fd);
                return 1;
            }
            volatile long unsigned int *uc_int = (volatile long unsigned int *)data_map;

            for (int loop = 0; loop < 10; loop++) {
                unsigned long sum = 0;
                unsigned long tmp = 0;
                for (int i = 0; i < 3; i++) {
                    tmp = uc_int[loop];
                }
                printf(" read value: %lu\n", tmp);
            }

            full_path = (char *)malloc(sizeof(char) * strlen(dest_dir_path) + strlen(changed_filename) + 1);
            snprintf(full_path, strlen(dest_dir_path) + strlen(changed_filename) + 1, "%s%s", dest_dir_path, changed_filename);
            printf("Dest path: %s\n", full_path);
            free(changed_entry);
            
            int dest_fd = open(full_path, O_RDWR);
            if (dest_fd < 0) {
                perror("open dest data file");
                return 1;
            }
            free(full_path);
            
            void *dest_data_map = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, dest_fd, 0);
            if (dest_data_map == MAP_FAILED) {
                perror("mmap data");
                printf("errno: %d\n", errno);
                close(version_fd);
                return 1;
            }
            memcpy(dest_data_map, uc_int, FILE_SIZE);

            volatile long unsigned int *dest_uc_int = (volatile long unsigned int *)dest_data_map;

            for (int loop = 0; loop < 10; loop++) {
                unsigned long sum = 0;
                unsigned long tmp = 0;
                for (int i = 0; i < 3; i++) {
                    tmp = dest_uc_int[loop];
                }
                printf(" read written value: %lu\n", tmp);
            }

            munmap(data_map, FILE_SIZE);
            close(fd);
            //i++;
            num_of_entries = 0;
            num_of_old_entries = 0;
            a = 0;
            
            if (i == 5) break;
        } else {
            sleep(1);
        }
        

    }
    free(dir_path);
    free(dest_dir_path);
    close(version_fd);
    return 0;
}