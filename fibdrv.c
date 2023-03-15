#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

void swap_char(char *a, char *b)
{
    *a = *a ^ *b;
    *b = *a ^ *b;
    *a = *a ^ *b;
}

int rev_core(char *head, int idx)
{
    if (head[idx] != '\0') {
        int end = rev_core(head, idx + 1);
        if (idx > end / 2)
            swap_char(head + idx, head + end - idx);
        return end;
    }
    return idx - 1;
}

char *reverse(char *s)
{
    if (!s)
        return NULL;
    rev_core(s, 0);
    return s;
}

void add_str(char *num1, char *num2, char *result)
{
    size_t len1 = strlen(num1), len2 = strlen(num2);
    int i, sum, carry = 0;
    reverse(num1);
    reverse(num2);
    if (len1 >= len2) {
        for (i = 0; i < len2; i++) {
            sum = (num1[i] - '0') + (num2[i] - '0') + carry;
            result[i] = '0' + sum % 10;
            carry = sum / 10;
        }
        for (i = len2; i < len1; i++) {
            sum = (num1[i] - '0') + carry;
            result[i] = '0' + sum % 10;
            carry = sum / 10;
        }
    } else {
        for (i = 0; i < len1; i++) {
            sum = (num1[i] - '0') + (num2[i] - '0') + carry;
            result[i] = '0' + sum % 10;
            carry = sum / 10;
        }

        for (i = len1; i < len2; i++) {
            sum = (num2[i] - '0') + carry;
            result[i] = '0' + sum % 10;
            carry = sum / 10;
        }
    }

    if (carry)
        result[i++] = '0' + carry;

    result[i] = '\0';
    reverse(num1);
    reverse(num2);
    reverse(result);
}

void mul_str(char *num1, char *num2, char *result)
{
    if (num1[0] == '0' || num2[0] == '0') {
        strncpy(result, "0", 1);
        return;
    }

    int len1 = strlen(num1), len2 = strlen(num2);
    int i, j, k = 0;

    for (i = 0; i < len1 + len2; i++) {
        result[i] = '0';
    }

    // Multiply each digit of num1 with each digit of num2
    for (i = len1 - 1; i >= 0; i--) {
        int carry = 0;
        for (j = len2 - 1; j >= 0; j--) {
            int sum = (num1[i] - '0') * (num2[j] - '0') +
                      (result[i + j + 1] - '0') + carry;
            carry = sum / 10;
            result[i + j + 1] = (sum % 10) + '0';
        }
        result[i + j + 1] = carry + '0';
    }

    // Remove leading zeros from result array
    i = 0;
    while (result[i] == '0') {
        i++;
    }

    // Copy result array to final result string
    k = 0;
    while (i < len1 + len2) {
        result[k++] = result[i++];
    }
    result[k] = '\0';
}

void sub_str(char *num1, char *num2, char *result)
{
    reverse(num1);
    reverse(num2);
    strncpy(result, num1, strlen(num1));

    int len2 = strlen(num2);

    for (int i = 0; i < len2; i++) {
        if (result[i] >= num2[i])
            result[i] = (result[i] - '0') - (num2[i] - '0') + '0';
        else {
            // borrow
            int k = 1;
            while (result[i + k] == '0') {
                result[i + k] = '9';
                k++;
            }
            result[i + k] = result[i + k] - '1' + '0';

            result[i] = (result[i] - '0' + 10) - (num2[i] - '0') + '0';
        }
    }

    bool zero = true;
    for (int i = strlen(result) - 1; i >= 0; i--) {
        if (result[i] == '0')
            result[i] = '\0';
        else {
            zero = false;
            break;
        }
    }
    if (zero)
        result[0] = '0';

    reverse(num1);
    reverse(num2);
    reverse(result);
}

static long long fib_sequence(long long k, char *buf)
{
    int num_bits = sizeof(k) * 8 - __builtin_clzll(k);
    char *t1 = kmalloc(128, GFP_KERNEL), *t2 = kmalloc(128, GFP_KERNEL);
    char *a = kmalloc(128, GFP_KERNEL), *b = kmalloc(128, GFP_KERNEL);
    char *tmp = kmalloc(128, GFP_KERNEL);

    strncpy(a, "0", 1);
    strncpy(b, "1", 1);
    for (int i = num_bits; i > 0; i--) {
        mul_str("2", b, t1);
        sub_str(t1, a, t2);
        mul_str(a, t2, t1);

        mul_str(a, a, t2);
        mul_str(b, b, tmp);
        add_str(t2, tmp, b);

        strncpy(a, t1, strlen(t1));

        if ((k >> (i - 1)) & 1) {
            add_str(a, b, t1);
            strncpy(a, b, strlen(b));
            strncpy(b, t1, strlen(t1));
        }
    }
    printk("a: %s, b: %s\n", a, b);

    int len = strlen(a) + 1;

    if (__copy_to_user(buf, a, len))
        return -EFAULT;

    return len;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    return (ssize_t) fib_sequence(*offset, buf);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
