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
#include <linux/mmzone.h>
#include <asm-generic/memory_model.h>

#define DEVICE_NAME             "cxl_mmap"
#define CLASS_NAME              "cxl_mmap_class"
#define FILE_PATH_LENGTH        32

#define IOCTL_MAGIC             0xCC
#define IOCTL_SET_FILE_PATH     _IOW(IOCTL_MAGIC, 0x01, struct cxl_dev_path_struct)


struct cxl_dev_path_struct {
	char path[FILE_PATH_LENGTH];
};

static char device_path[FILE_PATH_LENGTH];
static dev_t dev_num, dax_dev_num;
static struct cdev ffs_cdev;
static struct class *ffs_class;
static struct dax_device *cxl_dax_device;

static int mmap_helper(struct file *filp, struct vm_area_struct *vma);
static long cxl_range_helper_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int get_cxl_device(void);
static pgoff_t dax_pgoff; 

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.mmap = mmap_helper,
	.unlocked_ioctl = cxl_range_helper_ioctl
};

static vm_fault_t cxl_helper_filemap_fault(struct vm_fault *vmf)
{
	pfn_t pf;
	unsigned long pfn_from_kaddr;
    void *kaddr;
    long nr_pages_avail;
    
	pr_info("Page fault at user address 0x%lx (pgoff 0x%lx)\n",
           vmf->address, vmf->pgoff);
	dax_pgoff = vmf->pgoff;
	if (!cxl_dax_device)
		get_cxl_device();
	nr_pages_avail = dax_direct_access(cxl_dax_device, dax_pgoff, 1, DAX_ACCESS, &kaddr, &pf);
	pr_info("Num of page(s) %ld, pfn: 0x%llx, kaddr %p\n", nr_pages_avail, pf.val, kaddr);
	int is_pfn_valid = pfn_valid(pf.val);
	pr_info("Is PFN valid? %d\n", is_pfn_valid);
	if (is_pfn_valid) {
		int ret = vmf_insert_pfn(vmf->vma, vmf->address, pf.val);
		pr_info("Mapping 0x%llx from mem to 0x%lx (pgoff 0x%lx)\n", pf.val,
           vmf->address, vmf->pgoff);
		if (ret){
			pr_info("inserting pfn, ret: %d\n", ret);
			return VM_FAULT_SIGBUS;
		}
			
	}
	
	
	return VM_FAULT_NOPAGE;
}

const struct vm_operations_struct cxl_helper_file_vm_ops = {
	.fault		= cxl_helper_filemap_fault
};


static int mmap_helper(struct file *filp, struct vm_area_struct *vma) {
	unsigned long size = vma->vm_end - vma->vm_start;

	pr_info("cxl: mmap region size: %lu\n", size);
	vma->vm_ops = &cxl_helper_file_vm_ops;
	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	//long dax_ret = dax_direct_access(cxl_dax_device, dax_pgoff, 1, DAX_ACCESS, kaddr, &pfn);
	return 0;
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

static int get_cxl_device(void) {
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
	
	return 0;
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
			get_cxl_device();
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
	pr_info("get_cxl_range: loaded\n");
	get_cxl_device();
	dax_pgoff = 0;
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
