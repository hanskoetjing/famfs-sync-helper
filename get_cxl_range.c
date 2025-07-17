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
#include <linux/types.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/dax.h>
#include <linux/ioport.h>

#define DEVICE_NAME             "cxl_mmap"
#define CLASS_NAME              "cxl_mmap_class"
#define FILE_PATH_LENGTH        32

//temporary definition
#define IVSHM_BASE_UC		    0xc080000000ULL
#define IVSHM_SIZE              0x02000000ULL 
//temporary definition end

#define IOCTL_MAGIC             0xCC
#define IOCTL_SET_FILE_PATH     _IOW(IOCTL_MAGIC, 0x01, struct cxl_dev_path_struct)


struct cxl_dev_path_struct {
	char path[FILE_PATH_LENGTH];
};

static char device_path[FILE_PATH_LENGTH];
static dev_t dev_num, dax_dev_num;
static struct cdev ffs_cdev;
static struct class *ffs_class;
static struct device *cxl_dax_device_device;
static struct dax_device *cxl_dax_device;

static int mmap_helper(struct file *filp, struct vm_area_struct *vma);
static long cxl_range_helper_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = mmap_helper,
	.unlocked_ioctl = cxl_range_helper_ioctl
};

static vm_fault_t
famfs_filemap_fault(struct vm_fault *vmf)
{
	pr_info("fault\n");
	return NULL;
}

const struct vm_operations_struct famfs_file_vm_ops = {
	.fault		= famfs_filemap_fault
};


static int mmap_helper(struct file *filp, struct vm_area_struct *vma) {
	unsigned long size = vma->vm_end - vma->vm_start;
	long nr_page = 0; 
	pfn_t pfn;
	void **kaddr = NULL;
	

	pr_info("cxl: mmap region size: %lu\n", size);
	nr_page = size / PAGE_SIZE;
	if (size % PAGE_SIZE != 0)
		nr_page += 1;
	long dax_ret = dax_direct_access(cxl_dax_device, 0, nr_page, DAX_ACCESS, kaddr, &pfn);
	pr_info("cxl: mmap region sz: %ld\n", dax_ret);

	int ret = remap_pfn_range(vma, vma->vm_start, pfn.val, size, vma->vm_page_prot);
	if (!ret)
		pr_info("DAX mmap: 0x%lx (user virt) mapped to PFN 0x%llx (phys)\n", vma->vm_start, pfn.val);
	return ret;
}

//taken from famfs kernel code
static int lookup_daxdev(const char *pathname, dev_t *devno) {
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

static long cxl_range_helper_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	struct cxl_dev_path_struct rw;

	if (copy_from_user(&rw, (void __user *)arg, sizeof(rw)))
		return -EFAULT;

	pr_info("Path: %s\n", rw.path);

	switch (cmd) {
		case IOCTL_SET_FILE_PATH:
			int path_length = strscpy(device_path, rw.path, FILE_PATH_LENGTH);
			pr_info("%d char copied to file_path. File path: %s\n", path_length, device_path);
			break;
		default:
			return -ENOTTY;
	}

	return 0;
}

static int __init cxl_range_helper_init(void) {	
	//init char device
	alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	cdev_init(&ffs_cdev, &fops);
	cdev_add(&ffs_cdev, dev_num, 1);
	ffs_class = class_create(CLASS_NAME);
	device_create(ffs_class, NULL, dev_num, NULL, DEVICE_NAME);

	//init others
	strscpy(device_path, "/dev/dax0.0", sizeof(device_path)); //default device, can be altered using ioctl
	pr_info("using default path: %s\n", device_path);
	int l = lookup_daxdev(device_path, &dax_dev_num);
	if (!l) {
		pr_info("dax dev num: %d\n", dax_dev_num);
		cxl_dax_device = dax_dev_get(dax_dev_num);
		if (cxl_dax_device) {
			pr_info("got dax_device\n");
		} else {
			pr_info("no cxl_dax_device\n");
		}
		
	} else {
		pr_info("no dax dev num:\n");
	}
	
	pr_info("get_cxl_range: loaded\n");
	return 0;
}

static void __exit cxl_range_helper_exit(void) {
	//destroying char devices
	device_destroy(ffs_class, dev_num);
	class_destroy(ffs_class);
	cdev_del(&ffs_cdev);
	unregister_chrdev_region(dev_num, 1);
	pr_info("get_cxl_range: unloaded\n"); 
}


module_init(cxl_range_helper_init);
module_exit(cxl_range_helper_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FAMFS sync helper for multi-host configuration (r/w for all, not only master)");
