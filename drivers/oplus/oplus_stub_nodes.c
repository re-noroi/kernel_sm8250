#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>

static char ufs_read_mb[32] = "0\n";
static char ufs_write_mb[32] = "0\n";
static char lcd_info[64] = "unknown\n";

static void set_proc_owner(struct proc_dir_entry *entry) {
    if (entry) {
        proc_set_user(entry, make_kuid(&init_user_ns, 0), make_kgid(&init_user_ns, 1000));
    }
}

static ssize_t ufs_read_mb_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, ufs_read_mb, strlen(ufs_read_mb));
}
static ssize_t ufs_read_mb_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    if (count >= sizeof(ufs_read_mb)) return -EINVAL;
    if (copy_from_user(ufs_read_mb, buf, count)) return -EFAULT;
    ufs_read_mb[count] = '\0';
    return count;
}
static const struct file_operations ufs_read_mb_fops = {
    .read = ufs_read_mb_read,
    .write = ufs_read_mb_write,
};

static ssize_t ufs_write_mb_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, ufs_write_mb, strlen(ufs_write_mb));
}
static ssize_t ufs_write_mb_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    if (count >= sizeof(ufs_write_mb)) return -EINVAL;
    if (copy_from_user(ufs_write_mb, buf, count)) return -EFAULT;
    ufs_write_mb[count] = '\0';
    return count;
}
static const struct file_operations ufs_write_mb_fops = {
    .read = ufs_write_mb_read,
    .write = ufs_write_mb_write,
};

static ssize_t dummy_sink_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    return count; 
}
static ssize_t dummy_zero_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, "0\n", 2);
}
static const struct file_operations dummy_sink_fops = {
    .read = dummy_zero_read,
    .write = dummy_sink_write,
};

static ssize_t devinfo_lcd_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, lcd_info, strlen(lcd_info));
}
static ssize_t devinfo_lcd_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    if (count >= sizeof(lcd_info)) return -EINVAL;
    if (copy_from_user(lcd_info, buf, count)) return -EFAULT;
    lcd_info[count] = '\0';
    return count;
}
static const struct file_operations devinfo_lcd_fops = {
    .read = devinfo_lcd_read,
    .write = devinfo_lcd_write, 
};

static ssize_t devinfo_lcd_s_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, "none\n", 5);
}
static const struct file_operations devinfo_lcd_s_fops = {
    .read = devinfo_lcd_s_read,
    .write = dummy_sink_write, 
};

static struct class *oplus_chg_class;
static struct device *oplus_chg_common_dev;

static ssize_t mutual_cmd_show(struct device *dev, struct device_attribute *attr, char *buf) {
    msleep(5000); 
    return sprintf(buf, "0\n"); 
}
static ssize_t mutual_cmd_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    msleep(5000);
    return count; 
}
static DEVICE_ATTR_RW(mutual_cmd);

static int __init oplus_stub_nodes_init(void) {
    struct proc_dir_entry *oplus_storage, *io_metrics, *forever;
    struct proc_dir_entry *oplus_scheduler, *sched_assist;
    struct proc_dir_entry *oplus_afs, *oplus_mem, *devinfo;
    struct proc_dir_entry *proc_storage;

    set_proc_owner(proc_create("bootprof", 0666, NULL, &dummy_sink_fops));
    set_proc_owner(proc_create("theiaPwkReport", 0666, NULL, &dummy_sink_fops));

    proc_storage = proc_mkdir("storage", NULL);
    set_proc_owner(proc_storage);
    if (proc_storage) {
        set_proc_owner(proc_create("buf_log", 0666, proc_storage, &dummy_sink_fops));
    }

    oplus_storage = proc_mkdir("oplus_storage", NULL);
    set_proc_owner(oplus_storage);
    if (oplus_storage) {
        io_metrics = proc_mkdir("io_metrics", oplus_storage);
        set_proc_owner(io_metrics);
        if (io_metrics) {
            forever = proc_mkdir("forever", io_metrics);
            set_proc_owner(forever);
            if (forever) {
                set_proc_owner(proc_create("ufs_total_read_size_mb", 0666, forever, &ufs_read_mb_fops));
                set_proc_owner(proc_create("ufs_total_write_size_mb", 0666, forever, &ufs_write_mb_fops));
            }
        }
    }

    oplus_scheduler = proc_mkdir("oplus_scheduler", NULL);
    set_proc_owner(oplus_scheduler);
    if (oplus_scheduler) {
        sched_assist = proc_mkdir("sched_assist", oplus_scheduler);
        set_proc_owner(sched_assist);
        if (sched_assist) {
            set_proc_owner(proc_create("sched_assist_scene", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("sched_impt_task", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("im_flag_app", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("im_flag", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("ux_task", 0666, sched_assist, &dummy_sink_fops));
        }
    }

    oplus_afs = proc_mkdir("oplus_afs_config", NULL);
    set_proc_owner(oplus_afs);
    if (oplus_afs) {
        set_proc_owner(proc_create("afs_config", 0666, oplus_afs, &dummy_sink_fops));
    }

    oplus_mem = proc_mkdir("oplus_mem", NULL);
    set_proc_owner(oplus_mem);
    if (oplus_mem) {
        set_proc_owner(proc_create("memory_monitor", 0666, oplus_mem, &dummy_sink_fops));
    }

    devinfo = proc_mkdir("devinfo", NULL);
    set_proc_owner(devinfo);
    if (devinfo) {
        set_proc_owner(proc_create("lcd", 0666, devinfo, &devinfo_lcd_fops));
        set_proc_owner(proc_create("lcd_s", 0666, devinfo, &devinfo_lcd_s_fops));
    }

    oplus_chg_class = class_create(THIS_MODULE, "oplus_chg");
    if (!IS_ERR(oplus_chg_class)) {
        oplus_chg_common_dev = device_create(oplus_chg_class, NULL, MKDEV(0, 0), NULL, "common");
        if (!IS_ERR(oplus_chg_common_dev)) {
            device_create_file(oplus_chg_common_dev, &dev_attr_mutual_cmd);
        }
    }

    return 0;
}

module_init(oplus_stub_nodes_init);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danda");