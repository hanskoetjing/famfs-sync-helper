// based on coba14
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <assert.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/syscall.h>

#define IOCTL_MAGIC       0xCD
#define IOCTL_SETUP_NETWORK     _IOW(IOCTL_MAGIC, 0x02, struct famfs_sync_control_struct)

struct famfs_sync_control_struct {
	char path[129];
	int port;
};

void * process_queue_data(void *queue_name) {
    

    printf("%s\n", (char *)queue_name);
}

int main(int argc, char *argv[]) {
    
    srand(time(NULL));
    uint64_t bef_uc, aft_uc;
    pthread_t process_thread;

    int ret = 0;
    
    if (argc != 3) {
        perror("usage: <chr_dev_file_name> <port>");
        return 1;
    }

    ret = syscall(548, "127.0.0.1", 57580);
    printf("Syscall 548 returned: %d\n", ret);
    if (ret != 0) syscall(550);
    ret = syscall(549, "HELLO WORLD");
    ret = syscall(549, "SBGN");
    printf("Syscall 549 returned: %d\n", ret);
    ret = syscall(550);
    printf("Syscall 550 returned: %d\n", ret);

    printf("Char dev file name: %s\n", argv[1]);

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    /*
    struct famfs_sync_control_struct net_addr;
    char addr[16] = {0};
    strncpy(addr, argv[2], sizeof(argv[2]) + 1);
    net_addr.port = (int)strtol(argv[2], NULL, 10);

    printf("Port: %d\n", net_addr.port);

    if (ioctl(fd, IOCTL_SETUP_NETWORK, &net_addr) < 0) {
        perror("ioctl set network server");
        close(fd);
        return 1;
    }
    */
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        close(fd);
        return 1;
    }
    


    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl");
        close(fd);
        close(epfd);
        return 1;
    }

    char buf[32] = {0};
    while (1) {
        struct epoll_event events[1];
        int n = epoll_wait(epfd, events, 1, -1); // Tunggu selamanya sampe ada event
        if (n < 0) {
            perror("epoll_wait");
            break;
        }

        if (events[0].data.fd == fd && (events[0].events & EPOLLIN)) {
            int len = read(fd, buf, sizeof(buf));
            if (len > 0) {
                buf[len] = 0;
                printf("Event from kernel: %s\n", buf);
                char data[32] = {0};
                strncpy(data, buf, sizeof(buf));
                pthread_create(&process_thread, NULL, process_queue_data, data);
            }
        }
    }

    close(fd);

    return 0;
}
