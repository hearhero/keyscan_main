#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <mach/irqs.h>
#include <asm/io.h>
#include <asm/uaccess.h>

int keyscan_major = 250;
int keyscan_minor = 0;

const int keyset0[] = {10, 11, 12, 16};
const int keyset2[] = {7, 8, 9, 15};
const int keyset11[] = {4, 5, 6, 14};
const int keyset19[] = {1, 2, 3, 13};

struct class *keyboard_class;
struct cdev cdev;
int data = 0;

unsigned long *GPECON = NULL;
unsigned long *GPEDAT = NULL;
unsigned long *GPFCON = NULL;
unsigned long *GPFDAT = NULL;
unsigned long *GPGCON = NULL;
unsigned long *GPGDAT = NULL;

static DEFINE_SPINLOCK(keyscan_lock);

int keyscan_count = 0;

static int keyscan_open(struct inode *inode, struct file *filp)
{
	spin_lock(&keyscan_lock);

	if (0 < keyscan_count)
	{
		spin_unlock(&keyscan_lock);
		return -EBUSY;
	}

	keyscan_count++;
	spin_unlock(&keyscan_lock);

	return 0;
}

static int keyscan_release(struct inode *inode, struct file *filp)
{
	spin_lock(&keyscan_lock);
	keyscan_count--;
	spin_unlock(&keyscan_lock);

	return 0;
}

ssize_t keyscan_read(struct file *filep, char *buff, size_t count, loff_t *offp)
{
	ssize_t ret = 0;

	if (0 < copy_to_user(buff, &data, sizeof(data)))
	{
		ret = -EFAULT;
	}
	
	return ret;
}

ssize_t keyscan_write(struct file *filep, const char *buff, size_t count, loff_t *offp)
{
	ssize_t ret = 0;

	if (0 < copy_from_user(&data, buff, sizeof(data)))
	{
		ret = -EFAULT;
	}

	return ret;
}

struct file_operations keyscan_fops = {
	.owner = THIS_MODULE,
	.read = keyscan_read,
	.write = keyscan_write,
	.open = keyscan_open,
	.release = keyscan_release,
};

//set interrupt register to input mode
static void interrupt_reg_set(void)
{
	//EINT0
	*GPFCON &= ~3;

	//EINT2
	*GPFCON &= ~(3<<4);

	//EINT11
	*GPGCON &= ~(3<<6);

	//EINT19
	*GPGCON &= ~(3<<22);
}

//set KSCAN0~3 to zero
static void keyscan_reg_clear(void)
{
	//KSCAN0
	*GPEDAT &= ~(1<<11);

	//KSCAN1
	*GPGDAT &= ~(1<<6);

	//KSCAN2
    *GPEDAT &= ~(1<<13);

	//KSCAN3
	*GPGDAT &= ~(1<<2);
}

//set KSCAN0~3 to specified value: 0111, 1011, 1101, 1110
static void keyscan_reg_set(int value)
{
	switch (value) 
	{
	case 7:
		keyscan_reg_clear();
		*GPGDAT |= 1<<6;
		*GPEDAT |= 1<<13;
		*GPGDAT |= 1<<2;
		break;
	case 11:
		keyscan_reg_clear();
		*GPEDAT |= 1<<11;
		*GPEDAT |= 1<<13;
		*GPGDAT |= 1<<2;
		break;
	case 13:
		keyscan_reg_clear();
		*GPEDAT |= 1<<11;
		*GPGDAT |= 1<<6;
		*GPGDAT |= 1<<2;
		break;
	case 14:
		keyscan_reg_clear();
		*GPEDAT |= 1<<11;
		*GPGDAT |= 1<<6;
		*GPEDAT |= 1<<13;
		break;
	default:
		break;
	}
}

//set register to interrupt mode
static void interrupt_reg_init(void)
{
	//EINT0
	*GPFCON &= ~3;
	*GPFCON |= 2;

	//EINT2
	*GPFCON &= ~(3<<4);
	*GPFCON |= 2<<4;

	//EINT11
	*GPGCON &= ~(3<<6);
	*GPGCON |= 2<<6;

	//EINT19
	*GPGCON &= ~(3<<22);
	*GPGCON |= 2<<22;
}

//set KSCAN0~3 to output mode and set value to zero
static void keyboard_reg_init(void)
{
	//KSCAN0
	*GPECON &= ~(3<<22);
	*GPECON |= 1<<22;
	*GPEDAT &= ~(1<<11);

	//KSCAN1
	*GPGCON &= ~(3<<12);
	*GPGCON |= 1<<12;
	*GPGDAT &= ~(1<<6);

	//KSCAN2
	*GPECON &= ~(3<<26);
	*GPECON |= 1<<26;
    *GPEDAT &= ~(1<<13);

	//KSCAN3
	*GPGCON &= ~(3<<4);
	*GPGCON |= 1<<4;
	*GPGDAT &= ~(1<<2);
}

//scan EINT0 & EINT2
static void keyscan_key_query1(const int *keyset, int bitmask)
{
	keyscan_reg_set(7);

	if (!(*GPFDAT & (1 << bitmask)))
	{
		data = keyset[0];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}

	keyscan_reg_set(11);

	if (!(*GPFDAT & (1 << bitmask)))
	{
		data = keyset[1];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}

	keyscan_reg_set(13);

	if (!(*GPFDAT & (1 << bitmask)))
	{
		data = keyset[2];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}
	
	keyscan_reg_set(14);

	if (!(*GPFDAT & (1 << bitmask)))
	{
		data = keyset[3];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}
}

//scan EINT11 & EINT19
static void keyscan_key_query2(const int *keyset, int bitmask)
{
	keyscan_reg_set(7);

	if (!(*GPGDAT & (1 << bitmask)))
	{
		data = keyset[0];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}

	keyscan_reg_set(11);

	if (!(*GPGDAT & (1 << bitmask)))
	{
		data = keyset[1];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}

	keyscan_reg_set(13);

	if (!(*GPGDAT & (1 << bitmask)))
	{
		data = keyset[2];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}
	
	keyscan_reg_set(14);

	if (!(*GPGDAT & (1 << bitmask)))
	{
		data = keyset[3];
		printk(KERN_INFO "K%d emitted\n", data);
		return;
	}
}

static irqreturn_t keyscan_interrupt(int irqno, void *dev_id)
{
	interrupt_reg_set();

	switch (irqno) 
	{
	case IRQ_EINT0:
		keyscan_key_query1(keyset0, 0);
		break;
	case IRQ_EINT2:
		keyscan_key_query1(keyset2, 2);
		break;
	case IRQ_EINT11:
		keyscan_key_query2(keyset11, 3);
		break;
	case IRQ_EINT19:
		keyscan_key_query2(keyset19, 11);
		break;
	default:
		break;
	}

	keyscan_reg_clear();
	interrupt_reg_init();

	return IRQ_HANDLED;
}

static void interrupt_setup(void)
{
	int error;

	error = request_irq(IRQ_EINT0, keyscan_interrupt, IRQF_DISABLED | IRQF_TRIGGER_FALLING, "KEYSCAN_EINT0", NULL);
	if (error)
		goto end;
	
	error = request_irq(IRQ_EINT2, keyscan_interrupt, IRQF_DISABLED | IRQF_TRIGGER_FALLING, "KEYSCAN_EINT2", NULL);
	if (error)
		goto err0;

	error = request_irq(IRQ_EINT11, keyscan_interrupt, IRQF_DISABLED | IRQF_TRIGGER_FALLING, "KEYSCAN_EINT11", NULL);
	if (error)
		goto err2;

	error = request_irq(IRQ_EINT19, keyscan_interrupt, IRQF_DISABLED | IRQF_TRIGGER_FALLING, "KEYSCAN_EINT19", NULL);
	if (!error)
		goto end;

	free_irq(IRQ_EINT11, NULL);

err2:
	free_irq(IRQ_EINT2, NULL);

err0:
	free_irq(IRQ_EINT0, NULL);

end:
	return;
}

static void char_reg_setup_cdev(void)
{
	int error;
	dev_t dev;

	dev = MKDEV(keyscan_major, keyscan_minor);

	cdev_init(&cdev, &keyscan_fops);
	cdev.owner = THIS_MODULE;
	cdev.ops = &keyscan_fops;

	error = cdev_add(&cdev, dev, 1);

	if (0 > error)
	{
		printk(KERN_NOTICE "Error %d adding char_reg_setup_cdev\n", error);
		return;
	}

	keyboard_class = class_create(THIS_MODULE, "keyboard_class");

	if (IS_ERR(keyboard_class))
	{
		printk("Failed to create keyboard_class\n");
		return;
	}

	device_create(keyboard_class, NULL, dev, NULL, "KEYSCAN");
}

static int __init keyscan_init(void)
{
	int ret;
	dev_t dev;

	dev = MKDEV(keyscan_major, keyscan_minor);

	ret = register_chrdev_region(dev, 1, "KEYSCAN");

	if (0 > ret)
	{
		printk(KERN_WARNING "keyboard: cat't get the major number %d\n", keyscan_major);
		return ret;
	}
	
	char_reg_setup_cdev();

	GPECON = (unsigned long *)ioremap(0x56000040, 4);
	GPEDAT = (unsigned long *)ioremap(0x56000044, 4);
	GPFCON = (unsigned long *)ioremap(0x56000050, 4);
	GPFDAT = (unsigned long *)ioremap(0x56000054, 4);
	GPGCON = (unsigned long *)ioremap(0x56000060, 4);
	GPGDAT = (unsigned long *)ioremap(0x56000064, 4);

	interrupt_reg_init();
	keyboard_reg_init();
	interrupt_setup();

	printk(KERN_INFO "KEYSCAN driver registered\n");

	return 0;
}

static void __exit keyscan_exit(void)
{
	dev_t dev;
	dev = MKDEV(keyscan_major, keyscan_minor);

	device_destroy(keyboard_class, dev);
	class_destroy(keyboard_class);

	cdev_del(&cdev);
	unregister_chrdev_region(dev, 1); 

	iounmap(GPECON);
	iounmap(GPEDAT);
	iounmap(GPFCON);
	iounmap(GPFDAT);
	iounmap(GPGCON);
	iounmap(GPGDAT);

	printk(KERN_INFO "KEYSCAN driver unregistered\n");
}

module_init(keyscan_init);
module_exit(keyscan_exit);

MODULE_LICENSE("GPL");
