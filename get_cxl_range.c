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
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/dax.h>
#include <linux/ioport.h>
#include "dax-private.h"

#define DEVICE_NAME             "ffs_mmap"
#define CLASS_NAME              "ffs_class"
#define DUMMY_FILE_PATH         "undefined file path, pls setup using ioctl"
#define FILE_PATH_LENGTH        128
#define OPEN_TCP_PORT           57580
#define MAX_BUFFER_NET          128
#define DEFAULT_PORT            57580
#define COMMAND_LENGTH          4

#define IOCTL_MAGIC             0xCD
#define IOCTL_SET_FILE_PATH     _IOW(IOCTL_MAGIC, 0x01, struct famfs_sync_control_struct)
#define IOCTL_SETUP_NETWORK     _IOW(IOCTL_MAGIC, 0x02, struct famfs_sync_control_struct)
#define IOCTL_TEST_NETWORK      _IOW(IOCTL_MAGIC, 0x69, struct famfs_sync_control_struct)//temporary


struct famfs_sync_control_struct {
	char path[FILE_PATH_LENGTH + 1];
	int port;
};

static dev_t dev_num, dax_dev_num;
static struct cdev ffs_cdev;
static struct class *ffs_class;
static struct device *cxl_dax_device_device;
static struct dax_device *cxl_dax_device;
static struct dev_dax *cxl_dev_dax;
static struct dax_region *region;

static int mmap_helper(struct file *filp, struct vm_area_struct *vma);

static int mmap_helper(struct file *filp, struct vm_area_struct *vma) {
	return 0;
}

static int
lookup_daxdev(const char *pathname, dev_t *devno) {
	struct inode *inode;
	struct path path;
	int err;

	if (!pathname || !*pathname)
		return -EINVAL;

	err = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if (err)
		return err;

	inode = d_backing_inode(path.dentry);
	if (!S_ISCHR(inode->i_mode)) {
		err = -EINVAL;
		goto out_path_put;
	}

	if (!may_open_dev(&path)) { /* had to export this */
		err = -EACCES;
		goto out_path_put;
	}

	 /* if it's dax, i_rdev is struct dax_device */
	*devno = inode->i_rdev;

out_path_put:
	path_put(&path);
	return err;
}

static int __init ffs_helper_init(void) {	
	//init char device
	alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	cdev_init(&ffs_cdev, &fops);
	cdev_add(&ffs_cdev, dev_num, 1);
	ffs_class = class_create(CLASS_NAME);
	device_create(ffs_class, NULL, dev_num, NULL, DEVICE_NAME);

	//init others
	int l = lookup_daxdev("/dev/dax0.0", &dax_dev_num);
	if (!l)
		pr_info("dax dev num: %d\n", dax_dev_num);
	cxl_dax_device = dax_dev_get(dax_dev_num);
	if (cxl_dax_device)
		pr_info("got dax_device\n");
	cxl_dev_dax = container_of(&cxl_dax_device, struct dev_dax, dax_dev);
	if (cxl_dev_dax)
		pr_info("got cxl_dev_dax %d %llu\n", cxl_dev_dax->id, cxl_dev_dax->region->res.end);
	strscpy(ffs_file_path, DUMMY_FILE_PATH, 64);
	pr_info("famfs_sync_helper: loaded\n");
	pr_info("%s\n", ffs_file_path);

	return 0;
}

static void __exit ffs_helper_exit(void) {
	//destroying char devices
	device_destroy(ffs_class, dev_num);
	class_destroy(ffs_class);
	cdev_del(&ffs_cdev);
	unregister_chrdev_region(dev_num, 1);
	pr_info("famfs_sync_helper: unloaded\n"); 
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = mmap_helper
};


module_init(ffs_helper_init);
module_exit(ffs_helper_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FAMFS sync helper for multi-host configuration (r/w for all, not only master)");
