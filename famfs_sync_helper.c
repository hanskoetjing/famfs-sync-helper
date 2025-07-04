// ffs_handler_ioctl_rw.c
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/kthread.h>
#include <linux/delay.h> // untuk msleep()
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>
#include <linux/types.h>

#define DEVICE_NAME             "ffs_sync"
#define CLASS_NAME              "ffs_class"
#define DUMMY_FILE_PATH         "undefined file path, pls setup using ioctl"
#define FILE_PATH_LENGTH        128
#define OPEN_TCP_PORT        57580
#define MAX_BUFFER_NET			32

#define IOCTL_MAGIC             0xCD
#define IOCTL_SET_FILE_PATH     _IOW(IOCTL_MAGIC, 0x01, struct famfs_sync_control_struct)
#define IOCTL_SETUP_NETWORK     _IOW(IOCTL_MAGIC, 0x02, struct famfs_sync_control_struct)
#define IOCTL_TEST_NETWORK      _IOW(IOCTL_MAGIC, 0x69, struct famfs_sync_control_struct)//temporary

struct famfs_sync_control_struct {
	char path[FILE_PATH_LENGTH + 1];
};

static char ffs_file_path[FILE_PATH_LENGTH + 1];
static int path_length;
static dev_t dev_num;
static struct cdev ffs_cdev;
static struct class *ffs_class;
static struct socket *server_socket, *client_socket;
static struct sockaddr_in sin;
static struct sockaddr_in client_sockaddr;
static struct task_struct *my_kthread;
static char ip_4_addr[16];
int accept_connection(void *socket_in);
void sendMessage (char *message);


static int tcp_client_start(void) {
	int ret = 0;
	if (!client_socket) {
		ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &client_socket);
		if (ret < 0) return ret;
		memset(&client_sockaddr, 0, sizeof(client_sockaddr));
		//initialise client socket address
		client_sockaddr.sin_family = AF_INET;
		client_sockaddr.sin_port = htons(OPEN_TCP_PORT);
		int ret_ip = in4_pton(ip_4_addr, INET_ADDRSTRLEN, (u8 *)&client_sockaddr.sin_addr.s_addr, -1, NULL);
		if (ret_ip == 0) return -EINVAL;
		ret = client_socket->ops->connect(client_socket, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr), 0);
		if (ret < 0) return ret;
	} else {
		pr_info("There is a client socket\n");
		ret = -1;
	}
	return ret;
}

void sendMessage(char *message) {
	char msg[32] = {0};
	int len = strscpy(msg, message, sizeof(msg));
	pr_info("Sending message %s length %lu\n", msg, len);

	if (client_socket) {
		struct msghdr hdr;
		memset(&hdr, 0, sizeof(hdr));
		struct kvec iov = {
			.iov_base = message,
			.iov_len = sizeof(msg)
		};
		kernel_sendmsg(client_socket, &hdr, &iov, 1, strlen(msg));
	}
}

static void tcp_client_stop(void) {
	if (client_socket) {
		pr_info("Disconnect from server %s port %d\n", ip_4_addr, OPEN_TCP_PORT);
		sock_release(client_socket);
		client_socket = NULL;
	}
}

static int tcp_server_start(void) {
	int ret = 0;
	if (!server_socket) {
		pr_info("Start TCP server on port %d\n", OPEN_TCP_PORT);
		
		//initialise socket address
		memset(&sin, 0, sizeof(sin));
		sin.sin_addr.s_addr = INADDR_ANY;
		sin.sin_family = AF_INET;
		sin.sin_port = htons(OPEN_TCP_PORT);

		//create socket, bind, and listen
		ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &server_socket);
		if (ret < 0) return ret;
		ret = server_socket->ops->bind(server_socket, (struct sockaddr *)&sin, sizeof(sin));
		if (ret < 0) return ret;
		ret = server_socket->ops->listen(server_socket, 1);
		if (ret < 0) return ret;

		//accept connection inkernel_sendmsg separate thread
		my_kthread = kthread_run(accept_connection, (void *)server_socket, "accept_connection");
	}
	return ret;
}

int accept_connection(void *socket_in) {
	int ret_val = 0;
	struct socket *srv_socket = (struct socket *)socket_in;
	struct socket *new_socket;
	char buf[MAX_BUFFER_NET] = {0};
	pr_info("Waiting for connection\n");
	while(!kthread_should_stop()) {
		kernel_accept(srv_socket, &new_socket, 0);
		if (new_socket) {
			struct sockaddr_in connected_client_addr;
			struct msghdr hdr;
			memset(&hdr, 0, sizeof(hdr));
			struct kvec iov = {
				.iov_base = buf,
				.iov_len = sizeof(buf) - 1
			};
			kernel_getpeername(new_socket, (struct sockaddr *)&connected_client_addr);
			pr_info("Connected! client: %pI4\n", &connected_client_addr.sin_addr);
			int len = -1;
			for(;;) {
				len = kernel_recvmsg(new_socket, &hdr, &iov, 1, sizeof(buf) - 1, 0);
				if (len > 0) {
					pr_info("Data: %s\n", buf);
				} else if (len == 0) {
					pr_info("Client closed connection.\n");
					break;
				} else if (len == -EAGAIN) {
					msleep(10);int accept_connection(void *socket_in);

				} else {
					ret_val = len;
					break;
				}
				//overwrite buf data with NULL char
				memset(buf, 0, sizeof(buf));			
			}
			sock_release(new_socket);
			new_socket = NULL;
			pr_info("Done receiving data\n");
		}
	}
	pr_info("Acceptor thread exit. Bye\n");
	return ret_val;
}

static void tcp_server_stop(void) {
	if (server_socket) {
		pr_info("Release server socket on port %d\n", OPEN_TCP_PORT);
		sock_release(server_socket);
		server_socket = NULL;
	}
}

static long ffs_helper_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	struct famfs_sync_control_struct rw;

	if (copy_from_user(&rw, (void __user *)arg, sizeof(rw)))
		return -EFAULT;

	pr_info("Path: %s\n", rw.path);

	switch (cmd) {
		case IOCTL_SET_FILE_PATH:
			path_length = strscpy(ffs_file_path, rw.path, FILE_PATH_LENGTH);
			pr_info("%d char copied to file_path. File path: %s\n", path_length, ffs_file_path);
			break;
		case IOCTL_SETUP_NETWORK:
			memset(ip_4_addr, 0, sizeof(ip_4_addr));
			path_length = strscpy(ip_4_addr, rw.path, 16);
			pr_info("Server IP v4 address: %s\n", ip_4_addr);
			break;
		case IOCTL_TEST_NETWORK:
			tcp_client_start();
			sendMessage(rw.path);
			tcp_client_stop();
			break;
		default:
			return -ENOTTY;
	}

	return 0;
}

static int ffs_helper_mmap(struct file *filp, struct vm_area_struct *vma) {

	if (strcmp(DUMMY_FILE_PATH, ffs_file_path)){
		pr_info("Please set the file path first\n");
		return -EINVAL;
	}

	return 0;
}


static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ffs_helper_ioctl,
	.mmap = ffs_helper_mmap,
};

static int __init ffs_helper_init(void) {	
	alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	cdev_init(&ffs_cdev, &fops);
	cdev_add(&ffs_cdev, dev_num, 1);
	ffs_class = class_create(CLASS_NAME);
	device_create(ffs_class, NULL, dev_num, NULL, DEVICE_NAME);
	strscpy(ffs_file_path, DUMMY_FILE_PATH, 64);
	pr_info("famfs_sync_helper: loaded\n");
	pr_info("%s\n", ffs_file_path);
	tcp_server_start();

	return 0;
}

static void __exit ffs_helper_exit(void) {
	kthread_stop(my_kthread);
	tcp_server_stop();
	device_destroy(ffs_class, dev_num);
	class_destroy(ffs_class);
	cdev_del(&ffs_cdev);
	unregister_chrdev_region(dev_num, 1);
	pr_info("famfs_sync_helper: unloaded\n");
}

module_init(ffs_helper_init);
module_exit(ffs_helper_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FAMFS sync helper for multi-host configuration (r/w for all, not only master)");
