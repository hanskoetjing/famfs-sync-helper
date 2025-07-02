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

#define DEVICE_NAME             "ffs_sync"
#define CLASS_NAME              "ffs_class"
#define DUMMY_FILE_PATH         "undefined file path, pls setup using ioctl"
#define FILE_PATH_LENGTH        128
#define OPEN_TCP_PORT        57580

#define IOCTL_MAGIC             0xCE
#define IOCTL_SET_FILE_PATH     _IOW(IOCTL_MAGIC, 0x01, struct famfs_sync_control_struct)
#define IOCTL_TEST_NETWORK     _IOW(IOCTL_MAGIC, 0x69, struct famfs_sync_control_struct) //temporary

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
// Pointer untuk menyimpan task_struct dari thread kita
static struct task_struct *my_kthread;

int accept_connection(void *socket_in);

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
		case IOCTL_TEST_NETWORK:

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

static void tcp_server_stop(void) {
    if (client_socket)
        sock_release(client_socket);
    if (server_socket)
        sock_release(server_socket);
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ffs_helper_ioctl,
	.mmap = ffs_helper_mmap,
};

int accept_connection(void *socket_in) {
	struct socket *srv_socket = (struct socket *)socket_in;
	struct socket *new_socket;
	pr_info("Waiting for connection\n");
	while(!kthread_should_stop()) {
		kernel_accept(srv_socket, &new_socket, 0);
		if (new_socket) {
			pr_info("Got data!\n");
			pr_info("%lu\n", new_socket->state);
		}
	}
	return 0;
}	

static int __init ffs_helper_init(void) {	
	alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	cdev_init(&ffs_cdev, &fops);
	cdev_add(&ffs_cdev, dev_num, 1);
	ffs_class = class_create(CLASS_NAME);
	device_create(ffs_class, NULL, dev_num, NULL, DEVICE_NAME);
	strscpy(ffs_file_path, DUMMY_FILE_PATH, 64);
	pr_info("famfs_sync_helper: loaded\n");
	pr_info("%s\n", ffs_file_path);

	int ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &server_socket);
	if (ret < 0) return ret;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(OPEN_TCP_PORT);
	ret = server_socket->ops->bind(server_socket, (struct sockaddr *)&sin, sizeof(sin));
	if (ret < 0) return ret;
	ret = server_socket->ops->listen(server_socket, 1);
	my_kthread = kthread_run(accept_connection, (void *)server_socket, "accept_connection");

	return 0;
}

static void __exit ffs_helper_exit(void) {
	device_destroy(ffs_class, dev_num);
	class_destroy(ffs_class);
	cdev_del(&ffs_cdev);
	unregister_chrdev_region(dev_num, 1);
	tcp_server_stop();
	pr_info("famfs_sync_helper: unloaded\n");
}

module_init(ffs_helper_init);
module_exit(ffs_helper_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FAMFS sync helper for multi-host configuration (r/w for all, not only master)");
