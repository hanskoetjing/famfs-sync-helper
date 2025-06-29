// ivshm_ioctl_rw.c
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>

#define DEVICE_NAME             "ivshm"
#define CLASS_NAME              "ivshm_class"

//#define IVSHM_BASE              0xC080000000ULL
//#define IVSHM_BASE_UC           0xC082000000ULL
#define CXL_BASE                0x0518200000ULL
#define CXL_BASE_UC		0x0516200000ULL
#define IVSHM_BASE              0x0518200000ULL
//#define IVSHM_BASE_UC		0x381800000000ULL
#define IVSHM_BASE_UC		0xc080000000ULL
#define IVSHM_SIZE              0x02000000ULL  // 32MB

#define IOCTL_MAGIC             0xCE
#define IOCTL_WRITE_VAL         _IOW(IOCTL_MAGIC, 0x01, struct ivshm_rw)
#define IOCTL_READ_VAL          _IOR(IOCTL_MAGIC, 0x02, struct ivshm_rw)
#define IOCTL_SET_CACHE_MODE    _IOW(IOCTL_MAGIC, 0x03, struct ivshm_rw)

struct ivshm_rw {
	uint32_t offset;
	uint32_t value;
	uint32_t cache_mode;
};

enum {
	CACHE_MODE_UC = 0,
	CACHE_MODE_C  = 1,
};

static dev_t dev_num;
static struct cdev ivshm_cdev;
static struct class *ivshm_class;
static void __iomem *ivshm_virt;
static void __iomem *ivshm_virt_uc;
//static void __iomem *cxl_virt;
static void __iomem *cxl_virt_uc;
static int cache_mode = CACHE_MODE_C;

static long ivshm_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	struct ivshm_rw rw;
	void __iomem *ivshm_virt_local = ivshm_virt;

	if (copy_from_user(&rw, (void __user *)arg, sizeof(rw)))
		return -EFAULT;

	if (rw.offset + sizeof(uint32_t) > IVSHM_SIZE)
		return -EINVAL;
	pr_info("cmd: %d\n", cmd);
	switch (cmd) {
		case IOCTL_SET_CACHE_MODE:
			cache_mode = rw.cache_mode;
			if (cache_mode == CACHE_MODE_UC) {
				ivshm_virt_local = ivshm_virt_uc;
			}
			break;
		case IOCTL_WRITE_VAL:
			writel(rw.value, ivshm_virt_local + rw.offset);
			pr_info("ivshm: Wrote 0x%x to offset 0x%x\n", rw.value, rw.offset);
			break;
		case IOCTL_READ_VAL:
			rw.value = readl(ivshm_virt_local + rw.offset);
			if (copy_to_user((void __user *)arg, &rw, sizeof(rw)))
				return -EFAULT;
		pr_info("ivshm: Read  0x%x from offset 0x%x\n", rw.value, rw.offset);
		break;

		default:
			return -ENOTTY;
	}

	return 0;
}

static int ivshm_mmap(struct file *filp, struct vm_area_struct *vma) {
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn;

	if (size > IVSHM_SIZE)
		return -EINVAL;

	pr_info("ivshm: mmap region\n");
	pfn = IVSHM_BASE >> PAGE_SHIFT;
	if (cache_mode == CACHE_MODE_UC) {
		pfn = IVSHM_BASE_UC >> PAGE_SHIFT;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		pr_info("uncached\n");
	} else {
		pr_info("cached\n");
	}
	pr_info("ivshm: mmap region %lu\n", pfn);
	return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ivshm_ioctl,
	.mmap = ivshm_mmap,
};

static int __init ivshm_init(void) {
	//ivshm_virt = ioremap_cache(IVSHM_BASE, IVSHM_SIZE - 1);
	ivshm_virt_uc = ioremap_uc(IVSHM_BASE_UC, IVSHM_SIZE - 1);
	cxl_virt_uc = ioremap_uc(CXL_BASE_UC, IVSHM_SIZE - 1);
	pr_info("ivshm: cached  at 0x%llx\n", IVSHM_BASE);
	pr_info("ivshm: uncached at 0x%llx\n", IVSHM_BASE_UC);
	pr_info("cxl: cached at 0x%llx\n", CXL_BASE);
	pr_info("cxl: uncached at 0x%llx\n", CXL_BASE_UC);
	//pr_info("ivshm: mapped cached  at %p\n", ivshm_virt);
	pr_info("ivshm: mapped uncached at %p\n", ivshm_virt_uc);
	//pr_info("cxl: mapped cached  at %p\n", ivshm_virt);
	pr_info("cxl: mapped uncached at %p\n", cxl_virt_uc);
	//if (!ivshm_virt || !ivshm_virt_uc)
		//return -ENOMEM;
	if (!ivshm_virt_uc || !cxl_virt_uc)
		return -ENOMEM;
	alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	cdev_init(&ivshm_cdev, &fops);
	cdev_add(&ivshm_cdev, dev_num, 1);
	ivshm_class = class_create(CLASS_NAME);
	device_create(ivshm_class, NULL, dev_num, NULL, DEVICE_NAME);

	pr_info("ivshm: loaded\n");
	return 0;
}

static void __exit ivshm_exit(void) {
	device_destroy(ivshm_class, dev_num);
	class_destroy(ivshm_class);
	cdev_del(&ivshm_cdev);
	unregister_chrdev_region(dev_num, 1);

	//if (ivshm_virt)
		//iounmap(ivshm_virt);
	if (ivshm_virt_uc)
		iounmap(ivshm_virt_uc);

	pr_info("ivshm: unloaded\n");
}

module_init(ivshm_init);
module_exit(ivshm_exit);
MODULE_LICENSE("GPL");
