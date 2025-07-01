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

int main(int argc, char **argv) {
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
    int version_entries_length = strlen(version_entries);

    char *old_version_entries = malloc(sizeof(char) * version_entries_length + 1);
    snprintf(old_version_entries, version_entries_length + 1, "%s", version_entries);

    while (1) {
        if (version_entries_length > 0) {
            int num_of_entries = 0;
            int position = -1;
            //char **entries = get_entries(version_entries, "", &num_of_entries, &position);            
            
            
        }
        if (strcmp(old_version_entries, version_entries) != 0) {
            printf("old: %s\n", old_version_entries);
            printf("now: %s\n", version_entries);
            break;
        }
        

    }


    close(version_fd);
    return 0;
}