#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <linux/stat.h>

#ifdef DEBUG
#define MCD_DBG(fmt, ...) \
    do { \
        dev_dbg(mcd.dev, fmt, ##__VA_ARGS__); \
        pr_debug(fmt, ##__VA_ARGS__); \
    } while(0)

#define MCD_ERR(fmt, ...) \
    do { \
        dev_err(mcd.dev, fmt, ##__VA_ARGS__); \
        pr_err(fmt, ##__VA_ARGS__); \
    } while(0)
#else
#define MCD_DBG(fmt, ...) dev_dbg(mcd.dev, fmt, ##__VA_ARGS__);
#define MCD_ERR(fmt, ...) dev_err(mcd.dev, fmt, ##__VA_ARGS__);
#endif

#define DEVICE_NAME         "my_char_dev"
#define CLASS_NAME          "my_cdev"

#define DBGFS_ENTRY_NAME    "my_cdev_debugfs"
#define DBGFS_ENTRY_MODE    S_IRUGO

#define SYSFS_ENTRY_NAME    "my_cdev_sysfs"
#define SYSFS_ENTRY_MODE    (S_IRUGO | S_IWUGO)

#define MIN_USERS           1
#define MAX_USERS           16
#define DEFAULT_USERS       MAX_USERS

#define BUFFER_SIZE         1024

/* main funcs */
static int __init mcd_init(void);
static void __exit mcd_exit(void);

/* mcd char dev (file ops) */
static int my_char_dev_init(void);
static void my_char_dev_exit(void);
static int mcd_open(struct inode *inode, struct file *file);
static ssize_t mcd_read(struct file *file, char __user *buf, size_t len, loff_t *off);
static ssize_t mcd_write(struct file *file, const char __user *buf, size_t len, loff_t *off);
static int mcd_release(struct inode *inode, struct file *file);

/* dbgfs */
static int mcd_dbgfs_init(void);
static void mcd_dbgfs_exit(void);

/* sysfs */
static int mcd_sysfs_init(void);
static void mcd_sysfs_exit(void);
static ssize_t my_char_dev_sysfs_store(struct class *cls, struct class_attribute *attr, const char *buf, size_t len);
static ssize_t my_char_dev_sysfs_show(struct class *cls, struct class_attribute *attr, char *buf);

/* driver private structure */
struct my_char_dev
{
    const char *name;
    struct class *class;
    struct device *dev;
    struct dentry *dbgfs_entry;
    uint32_t major_number;
    uint32_t minor_number;
    uint8_t max_users;
    uint8_t cur_users;
    uint8_t unused[2];
    char buf[BUFFER_SIZE];
    size_t buf_len;
};

/* file ops for /dev */
static struct file_operations fops =
{
    .open = mcd_open,
    .release = mcd_release,
    .read = mcd_read,
    .write = mcd_write,
};

/* file osp for /sys/kernel/debug */
static struct my_char_dev mcd =
{
    .name = DEVICE_NAME,
    .max_users = DEFAULT_USERS,
};

/* file ops & attr for /sys/class */
static struct class_attribute cls_attr_exercise_sysfs =
{
    {
        .name = SYSFS_ENTRY_NAME,
        .mode = SYSFS_ENTRY_MODE
    },
    .show = my_char_dev_sysfs_show,
    .store = my_char_dev_sysfs_store,
};

static int my_char_dev_init(void)
{
    int32_t major;
    struct class *class;
    struct device *dev;
    int ret = 0;

    /* alloc dev dynamically, so pass 0 as major */
    major = register_chrdev(0, mcd.name, &fops);
    if (major < 0) {
        MCD_ERR("MCD: Failed to register char dev\n");
        return major;
    }
    mcd.major_number = major;

    class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR_OR_NULL(class)){
        ret = PTR_ERR(class);
        MCD_ERR("MCD: Failed to register device class\n");
        goto class_err;
    }
    mcd.class = class;

    dev = device_create(mcd.class, NULL, MKDEV(mcd.major_number, mcd.minor_number), NULL, mcd.name);
    if (IS_ERR_OR_NULL(dev)){
        ret = PTR_ERR(dev);
        MCD_ERR("MCD: Failed to create the device\n");
        goto dev_err;
    }
    mcd.dev = dev;

    return 0;

dev_err:
    class_destroy(mcd.class);
class_err:
    unregister_chrdev(mcd.major_number, mcd.name);

    return ret;
}

static void my_char_dev_exit(void)
{
    device_destroy(mcd.class, MKDEV(mcd.major_number, mcd.minor_number));
    class_unregister(mcd.class);
    class_destroy(mcd.class);
    unregister_chrdev(mcd.major_number, mcd.name);
}

static int mcd_dbgfs_init(void)
{
    struct dentry *file;

    /* We need only read major number, so use predefined helper functions */
    file = debugfs_create_u8(DBGFS_ENTRY_NAME, DBGFS_ENTRY_MODE, NULL, &mcd.cur_users);
    if (IS_ERR_OR_NULL(file)) {
        MCD_ERR("MCD: Failed to create debug fs enrty\n");
        return PTR_ERR(file);
    }
    mcd.dbgfs_entry = file;

    return 0;
}

static void mcd_dbgfs_exit(void)
{
    debugfs_remove(mcd.dbgfs_entry);
}

static int mcd_sysfs_init(void)
{
    int ret;

    ret = class_create_file(mcd.class, &cls_attr_exercise_sysfs);
    if (ret) {
        MCD_ERR("MCD: Failed to create class file\n");
        return ret;
    }
    return 0;
}

static void mcd_sysfs_exit(void)
{
    class_remove_file(mcd.class, &cls_attr_exercise_sysfs);
}

static ssize_t my_char_dev_sysfs_store(struct class *cls, struct class_attribute *attr, const char *buf, size_t len)
{
    unsigned long n;
    char *ptr;

    (void)cls;
    (void)attr;

    if (len == 0) {
        MCD_DBG("MCD: Len == 0, nothing to store\n");
        return 0;
    }

    n = simple_strtoul(buf, &ptr, 10);
    if (ptr == buf) {
        MCD_ERR("MCD: Invalid data, required int\n");
        return -EINVAL;
    }

    if (n < MIN_USERS || n > MAX_USERS) {
        MCD_ERR("MCD: Invalid max users\n");
        return -EINVAL;
    }

    mcd.max_users = (uint8_t)n;

    return len;
}

static ssize_t my_char_dev_sysfs_show(struct class *cls, struct class_attribute *attr, char *buf)
{
    (void)cls;
    (void)attr;
    (void)buf;

    return sprintf(buf, "%hu", mcd.max_users);
}

static int __init mcd_init(void)
{
    int ret;

    ret = my_char_dev_init();
    if (ret)
        return ret;

    ret = mcd_dbgfs_init();
    if (ret)
        return ret;

    ret = mcd_sysfs_init();
    if (ret)
        return ret;

    return 0;
}

static void __exit mcd_exit(void)
{
    mcd_sysfs_exit();
    mcd_dbgfs_exit();
    my_char_dev_exit();
}

static int mcd_open(struct inode *inode, struct file *file)
{
    (void)inode;
    (void)file;

    if (mcd.cur_users >= mcd.max_users) {
        MCD_ERR("MCD: MCD is busy, to many users at the same time\n");
        return -EBUSY;
    }

    ++mcd.cur_users;
    MCD_DBG("MCD: Open, users = %d\n", mcd.cur_users);
    return 0;
}

static ssize_t mcd_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
    size_t bytes_to_copy;
    size_t ret;

    (void)file;
    (void)off;

    if (mcd.buf_len == 0) {
        MCD_DBG("MCD: Buffer is empty\n");
        return 0;
    }

    bytes_to_copy = min(len, mcd.buf_len);
    MCD_DBG("MCD: Copy %zu bytes to user\n", bytes_to_copy);

    ret = raw_copy_to_user(buf, mcd.buf, bytes_to_copy);
    if (ret) {
        MCD_ERR("MCD: Failed to copy data from user\n");
        return bytes_to_copy - ret;
    }

    mcd.buf_len = 0;

    return bytes_to_copy;
}

static ssize_t mcd_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
    size_t ret;

    (void)file;
    (void)off;

    if (len >= BUFFER_SIZE) {
        MCD_ERR("MCD: Data is too long, max size = %d\n", BUFFER_SIZE);
        return -ENOSPC;
    }

    MCD_DBG("Copy %zu bytes from user\n", len);

    ret = raw_copy_from_user(mcd.buf, buf, len);
    if (ret) {
        MCD_ERR("MCD: Failed to copy data to user\n");
        mcd.buf_len = 0;
        return len - ret;
    }

    mcd.buf_len = len;
    return len;
}

static int mcd_release(struct inode *inode, struct file *file)
{
    (void)inode;
    (void)file;

    --mcd.cur_users;
    MCD_DBG("MCD: Release, users = %d\n", mcd.cur_users);

    return 0;
}

module_init(mcd_init);
module_exit(mcd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Kukowski <michalkukowski10@gmail.com>");
MODULE_VERSION("1.0");

