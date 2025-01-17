#include <linux/cdev.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/cdev.h>	
#include <linux/uaccess.h>
#include <linux/hashtable.h>

#include "ioctl_dev.h"
#include "ioctl.h"

/* store the major number extracted by dev_t */
int ioctl_d_interface_major = 0;
int ioctl_d_interface_minor = 0;

#define DEVICE_NAME "ioctl_d"
char* ioctl_d_interface_name = DEVICE_NAME;

ioctl_d_interface_dev ioctl_d_interface;

struct file_operations ioctl_d_interface_fops = {
	.owner = THIS_MODULE,
	.read = NULL,
	.write = NULL,
	.open = ioctl_d_interface_open,
	.unlocked_ioctl = ioctl_d_interface_ioctl,
	.release = ioctl_d_interface_release
};

struct inode_lock {
    uint32_t inode_num;
	bool held;
    struct hlist_node node;
};
DEFINE_HASHTABLE(lock_cache, 20);

/* Private API */
static int ioctl_d_interface_dev_init(ioctl_d_interface_dev* ioctl_d_interface);
static void ioctl_d_interface_dev_del(ioctl_d_interface_dev* ioctl_d_interface);
static int ioctl_d_interface_setup_cdev(ioctl_d_interface_dev* ioctl_d_interface);
static int ioctl_d_interface_init(void);
static void ioctl_d_interface_exit(void);

static int ioctl_d_interface_dev_init(ioctl_d_interface_dev * ioctl_d_interface)
{
	int result = 0;
	memset(ioctl_d_interface, 0, sizeof(ioctl_d_interface_dev));
	atomic_set(&ioctl_d_interface->available, 1);
	sema_init(&ioctl_d_interface->sem, 1);

	return result;
}

static void ioctl_d_interface_dev_del(ioctl_d_interface_dev * ioctl_d_interface) {}

static int ioctl_d_interface_setup_cdev(ioctl_d_interface_dev * ioctl_d_interface)
{
  int error = 0;
	dev_t devno = MKDEV(ioctl_d_interface_major, ioctl_d_interface_minor);

	cdev_init(&ioctl_d_interface->cdev, &ioctl_d_interface_fops);
	ioctl_d_interface->cdev.owner = THIS_MODULE;
	ioctl_d_interface->cdev.ops = &ioctl_d_interface_fops;
	error = cdev_add(&ioctl_d_interface->cdev, devno, 1);

	return error;
}

static int ioctl_d_interface_init(void)
{
	dev_t           devno = 0;
	int             result = 0;

	ioctl_d_interface_dev_init(&ioctl_d_interface);


	/* register char device
	 * we will get the major number dynamically this is recommended see
	 * book : ldd3
	 */
	result = alloc_chrdev_region(&devno, ioctl_d_interface_minor, 1, ioctl_d_interface_name);
	ioctl_d_interface_major = MAJOR(devno);
	if (result < 0) {
		printk(KERN_WARNING "ioctl_d_interface: can't get major number %d\n", ioctl_d_interface_major);
		goto fail;
	}

	result = ioctl_d_interface_setup_cdev(&ioctl_d_interface);
	if (result < 0) {
		printk(KERN_WARNING "ioctl_d_interface: error %d adding ioctl_d_interface", result);
		goto fail;
	}

	printk(KERN_INFO "ioctl_d_interface: module loaded\n");

	hash_init(lock_cache);

	struct inode_lock *cur = NULL;
	uint32_t i, num = 1000000;
	
	for (i = 0; i < num; i++) {
		cur = (struct inode_lock *)kmalloc(sizeof(inode_lock), GFP_KERNEL);
		
		if (cur == NULL) {
			printk(KERN_WARNING "Insufficient memory after %d alloactions\n", i);
			break;
		}

		cur->inode_num = i;
		cur->held = true;

		hash_add(lock_cache, &(cur->node), cur->inode_num);
	}

	return 0;

fail:
	ioctl_d_interface_exit();
	return result;
}

static void ioctl_d_interface_exit(void)
{
	struct inode_lock *cur;

	unsigned bkt;

	hash_for_each(lock_cache, bkt, cur, node) {
		hash_del(&(cur->node));
		kfree(cur);
	}
	printk(KERN_INFO "ioctl_d_interface: hashtable freed\n");

	dev_t devno = MKDEV(ioctl_d_interface_major, ioctl_d_interface_minor);

	cdev_del(&ioctl_d_interface.cdev);
	unregister_chrdev_region(devno, 1);
	ioctl_d_interface_dev_del(&ioctl_d_interface);

	printk(KERN_INFO "ioctl_d_interface: module unloaded\n");
}

/* Public API */
int ioctl_d_interface_open(struct inode *inode, struct file *filp)
{
	ioctl_d_interface_dev *ioctl_d_interface;

	ioctl_d_interface = container_of(inode->i_cdev, ioctl_d_interface_dev, cdev);
	filp->private_data = ioctl_d_interface;

	if (!atomic_dec_and_test(&ioctl_d_interface->available)) {
		atomic_inc(&ioctl_d_interface->available);
		printk(KERN_ALERT "open ioctl_d_interface : the device has been opened by some other device, unable to open lock\n");
		return -EBUSY;		/* already open */
	}

	return 0;
}

int ioctl_d_interface_release(struct inode *inode, struct file *filp)
{
	ioctl_d_interface_dev *ioctl_d_interface = filp->private_data;
	atomic_inc(&ioctl_d_interface->available);	/* release the device */
	return 0;
}

long ioctl_d_interface_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		// Get the number of channel found
		case IOCTL_BASE_GET_MUIR:
			uint32_t num;

			if (copy_from_user(&num, (uint32_t*) arg, sizeof(num))){
				return -EFAULT;
			}

			printk(KERN_INFO "<%s> ioctl: invalidating %u\n", DEVICE_NAME, num);

			struct inode_lock *cur = NULL;

			hash_for_each_possible(lock_cache, cur, node, num) {
				cur->held = false;
				printk(KERN_INFO "<%s> ioctl: invalidated %u\n", DEVICE_NAME, num);
			}

			break;

		default:
			break;
	}

	return 0;
}


module_init(ioctl_d_interface_init);
module_exit(ioctl_d_interface_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Depraz <florian.depraz@outlook.com>");
MODULE_DESCRIPTION("IOCTL Interface driver");
MODULE_VERSION("0.1");

