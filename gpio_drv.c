#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/poll.h>

#define CLASS_NAME "sysprog_gpio"
#define MAX_GPIO 10
#define GPIOCHIP_BASE 512

#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)
#define GPIO_IOCTL_DISABLE_IRQ _IOW(GPIO_IOCTL_MAGIC, 2, int)

static dev_t dev_num_base;
static struct cdev gpio_cdev;
static int major_num;

struct gpio_entry {
    int bcm_num;
    struct gpio_desc *desc;
    struct device *dev;
    int irq_num;
    bool irq_enabled;
    struct fasync_struct *async_queue;
};

static struct class *gpiod_class;
static struct gpio_entry *gpio_table[MAX_GPIO];

static int find_gpio_index(int bcm) {
    for (int i = 0; i < MAX_GPIO; i++) {
        if (gpio_table[i] && gpio_table[i]->bcm_num == bcm)
            return i;
    }
    return -1;
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id) {
    struct gpio_entry *entry = dev_id;
    pr_info("[sysprog_gpio] IRQ on GPIO %d\n", entry->bcm_num);
    if (entry->async_queue)
        kill_fasync(&entry->async_queue, SIGIO, POLL_IN);
    return IRQ_HANDLED;
}

static int gpio_fops_open(struct inode *inode, struct file *filp) {
    int minor = iminor(inode);
    if (minor >= MAX_GPIO || !gpio_table[minor])
        return -ENODEV;
    filp->private_data = gpio_table[minor];
    return 0;
}

static int gpio_fops_release(struct inode *inode, struct file *filp) {
    struct gpio_entry *entry = filp->private_data;
    if (entry && entry->irq_enabled) {
        free_irq(entry->irq_num, entry);
        entry->irq_enabled = false;
    }
    fasync_helper(-1, filp, 0, &entry->async_queue);
    return 0;
}

static int gpio_fops_fasync(int fd, struct file *filp, int mode) {
    struct gpio_entry *entry = filp->private_data;
    return fasync_helper(fd, filp, mode, &entry->async_queue);
}

static long gpio_fops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct gpio_entry *entry = filp->private_data;
    int irq;

    switch (cmd) {
    case GPIO_IOCTL_ENABLE_IRQ:
        if (entry->irq_enabled)
            return -EBUSY;
        irq = gpiod_to_irq(entry->desc);
        if (irq < 0) return -EINVAL;
        if (request_irq(irq, gpio_irq_handler,
                        IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                        "gpio_irq", entry)) {
            pr_err("[sysprog_gpio] IRQ request failed\n");
            return -EIO;
        }
        entry->irq_num = irq;
        entry->irq_enabled = true;
        return 0;
    case GPIO_IOCTL_DISABLE_IRQ:
        if (!entry->irq_enabled)
            return -EINVAL;
        free_irq(entry->irq_num, entry);
        entry->irq_enabled = false;
        return 0;
    default:
        return -ENOTTY;
    }
}

static ssize_t gpio_fops_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    struct gpio_entry *entry = filp->private_data;
    char val = gpiod_get_value(entry->desc) ? '1' : '0';

    if (copy_to_user(buf, &val, 1))
        return -EFAULT;

    return 1;
}


static ssize_t gpio_fops_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    struct gpio_entry *entry = filp->private_data;
    char kbuf[8] = {0};
    if (len >= sizeof(kbuf)) return -EINVAL;
    if (copy_from_user(kbuf, buf, len)) return -EFAULT;
    kbuf[len] = '\0';
    if (sysfs_streq(kbuf, "1")) {
        if (gpiod_get_direction(entry->desc)) return -EPERM;
        gpiod_set_value(entry->desc, 1);
    } else if (sysfs_streq(kbuf, "0")) {
        if (gpiod_get_direction(entry->desc)) return -EPERM;
        gpiod_set_value(entry->desc, 0);
    } else if (sysfs_streq(kbuf, "in")) {
        gpiod_direction_input(entry->desc);
    } else if (sysfs_streq(kbuf, "out")) {
        gpiod_direction_output(entry->desc, 0);
    } else {
        return -EINVAL;
    }
    return len;
}

static const struct file_operations gpio_fops = {
    .owner = THIS_MODULE,
    .open = gpio_fops_open,
    .read = gpio_fops_read,
    .write = gpio_fops_write,
    .release = gpio_fops_release,
    .fasync = gpio_fops_fasync,
    .unlocked_ioctl = gpio_fops_ioctl,
};

static ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    int val = gpiod_get_value(entry->desc);
    return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t value_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    if (gpiod_get_direction(entry->desc)) return -EPERM;
    if (buf[0] == '1') gpiod_set_value(entry->desc, 1);
    else if (buf[0] == '0') gpiod_set_value(entry->desc, 0);
    else return -EINVAL;
    return count;
}

static ssize_t direction_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    int dir = gpiod_get_direction(entry->desc);
    return scnprintf(buf, PAGE_SIZE, "%s\n", dir ? "in" : "out");
}

static ssize_t direction_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    if (sysfs_streq(buf, "in")) gpiod_direction_input(entry->desc);
    else if (sysfs_streq(buf, "out")) gpiod_direction_output(entry->desc, 0);
    else return -EINVAL;
    return count;
}

static DEVICE_ATTR_RW(value);
static DEVICE_ATTR_RW(direction);

static ssize_t export_store(const struct class *class, const struct class_attribute *attr, const char *buf, size_t count) {
    int bcm, i;
    int kernel_gpio;
    struct gpio_entry *entry;
    char name[16];

    if (kstrtoint(buf, 10, &bcm) < 0)
        return -EINVAL;

    if (find_gpio_index(bcm) >= 0)
        return -EEXIST;

    for (i = 0; i < MAX_GPIO; i++) {
        if (!gpio_table[i]) break;
    }
    if (i == MAX_GPIO) return -ENOMEM;

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) return -ENOMEM;

    kernel_gpio = GPIOCHIP_BASE + bcm;
    entry->bcm_num = bcm;
    entry->desc = gpio_to_desc(kernel_gpio);
    if (!entry->desc) {
        kfree(entry);
        return -EINVAL;
    }

    gpiod_direction_input(entry->desc);

    snprintf(name, sizeof(name), "gpio%d", bcm);
    entry->dev = device_create(gpiod_class, NULL, MKDEV(major_num, i), NULL, name);
    if (IS_ERR(entry->dev)) {
        kfree(entry);
        return PTR_ERR(entry->dev);
    }

    dev_set_drvdata(entry->dev, entry);
    device_create_file(entry->dev, &dev_attr_value);
    device_create_file(entry->dev, &dev_attr_direction);
    gpio_table[i] = entry;

    pr_info("[sysprog_gpio] Exported GPIO %d\n", bcm);
    return count;
}

static ssize_t unexport_store(const struct class *class, const struct class_attribute *attr, const char *buf, size_t count) {
    int bcm, idx;

    if (kstrtoint(buf, 10, &bcm) < 0)
        return -EINVAL;

    idx = find_gpio_index(bcm);
    if (idx < 0) return -ENOENT;

    device_remove_file(gpio_table[idx]->dev, &dev_attr_value);
    device_remove_file(gpio_table[idx]->dev, &dev_attr_direction);
    device_destroy(gpiod_class, MKDEV(major_num, idx));
    kfree(gpio_table[idx]);
    gpio_table[idx] = NULL;

    pr_info("[sysprog_gpio] Unexported GPIO %d\n", bcm);
    return count;
}

static CLASS_ATTR_WO(export);
static CLASS_ATTR_WO(unexport);

static int __init gpio_driver_init(void) {
    int ret;
    pr_info("[sysprog_gpio] module loading\n");

    gpiod_class = class_create(CLASS_NAME);
    if (IS_ERR(gpiod_class)) return PTR_ERR(gpiod_class);

    ret = class_create_file(gpiod_class, &class_attr_export);
    if (ret) return ret;

    ret = class_create_file(gpiod_class, &class_attr_unexport);
    if (ret) return ret;

    ret = alloc_chrdev_region(&dev_num_base, 0, MAX_GPIO, "gpio");
    if (ret) return ret;

    major_num = MAJOR(dev_num_base);
    cdev_init(&gpio_cdev, &gpio_fops);
    gpio_cdev.owner = THIS_MODULE;
    ret = cdev_add(&gpio_cdev, dev_num_base, MAX_GPIO);
    if (ret) return ret;

    return 0;
}

static void __exit gpio_driver_exit(void) {
    for (int i = 0; i < MAX_GPIO; i++) {
        if (gpio_table[i]) {
            device_remove_file(gpio_table[i]->dev, &dev_attr_value);
            device_remove_file(gpio_table[i]->dev, &dev_attr_direction);
            device_destroy(gpiod_class, MKDEV(major_num, i));
            kfree(gpio_table[i]);
            gpio_table[i] = NULL;
        }
    }

    cdev_del(&gpio_cdev);
    unregister_chrdev_region(dev_num_base, MAX_GPIO);

    class_remove_file(gpiod_class, &class_attr_export);
    class_remove_file(gpiod_class, &class_attr_unexport);
    class_destroy(gpiod_class);

    pr_info("[sysprog_gpio] module unloaded\n");
}

module_init(gpio_driver_init);
module_exit(gpio_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jiwoong Park");
MODULE_DESCRIPTION("GPIO control driver for system programming lectures");
