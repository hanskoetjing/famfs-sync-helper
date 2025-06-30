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

#define DEVICE_NAME             "ffs_sync"
#define CLASS_NAME              "ffs_class"
#define DUMMY_FILE_PATH         "undefined file path, pls setup using ioctl"

#define IOCTL_MAGIC             0xCE
#define IOCTL_SET_FILE_PATH     _IOW(IOCTL_MAGIC, 0x01, struct famfs_sync_control_struct)

struct famfs_sync_control_struct {
	char * path;
};

enum {
	CACHE_MODE_UC = 0,
	CACHE_MODE_C  = 1,
};

static char ffs_file_path[64];
static int path_length;
static dev_t dev_num;
static struct cdev ffs_cdev;
static struct class *ffs_class;

static long ffs_helper_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	struct famfs_sync_control_struct rw;

	if (copy_from_user(&rw, (void __user *)arg, sizeof(rw)))
		return -EFAULT;

	switch (cmd) {
		case IOCTL_SET_FILE_PATH:
			path_length = strscpy(ffs_file_path, rw.path, 64);
			pr_info("%d char copied to file_path. File path: %s\n", path_length, ffs_file_path);
			break;
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
	return 0;
}

static void __exit ffs_helper_exit(void) {
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
