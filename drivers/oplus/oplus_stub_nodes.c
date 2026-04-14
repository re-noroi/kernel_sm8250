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
#include <linux/power_supply.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/notifier.h>

static char ufs_read_mb[32] = "0\n";
static char ufs_write_mb[32] = "0\n";
static char lcd_info[64] = "unknown\n";

static bool smart_chg_enable = true;
static bool input_suspended = false;
static int cool_down_level = 0;
static int bcc_current_ma = 0;
static int chg_up_limit_value = 0;
static int otg_switch = 0;

static struct class *oplus_chg_class;
static struct device *oplus_chg_battery_dev;
static struct device *oplus_chg_usb_dev;
static struct device *oplus_chg_common_dev;
static struct device *oplus_chg_ac_dev;

static struct notifier_block psy_nb;
static bool usb_attr_added = false;

#define QUICK_CHARGE_NORMAL	0
#define QUICK_CHARGE_FAST	1
#define QUICK_CHARGE_FLASH	2
#define QUICK_CHARGE_TURBO	3
#define QUICK_CHARGE_SUPER	4

#define PROTOCOL_NORMAL		0
#define PROTOCOL_PD		4
#define PROTOCOL_PD_PPS		5

static void set_proc_owner(struct proc_dir_entry *entry) {
    if (entry) proc_set_user(entry, make_kuid(&init_user_ns, 0), make_kgid(&init_user_ns, 1000));
}

static int read_sysfs_int(const char *path, int *out_value) {
	struct file *fp;
	char buf[32];
	int ret;
	loff_t pos = 0;

	fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp)) return PTR_ERR(fp);

	ret = kernel_read(fp, buf, sizeof(buf) - 1, &pos);
	filp_close(fp, NULL);

	if (ret <= 0) return -EIO;

	buf[ret] = '\0';
	if (kstrtoint(buf, 10, out_value)) return -EINVAL;
	return 0;
}

static int get_usb_type(void) {
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	psy = power_supply_get_by_name("usb");
	if (psy) {
		if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_USB_TYPE, &val))
			usb_type = val.intval;
		power_supply_put(psy);
	}
	return usb_type;
}

static int get_adapter_power_mw(void) {
    struct power_supply *psy;
    union power_supply_propval val = { 0 };
    int power_w = 0;

    psy = power_supply_get_by_name("usb");
    if (psy) {
        if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_POWER_MAX, &val)) {
            power_w = val.intval;
        }
        power_supply_put(psy);
    }

    if (power_w > 0) {
        if (power_w == 90) power_w = 100;
        return power_w * 1000;
    }

    return 10000;
}

static u8 get_quick_charge_type(void) {
	int power_mw = get_adapter_power_mw();
	int usb_type = get_usb_type();

	if (power_mw >= 65000) return QUICK_CHARGE_SUPER;
	if (power_mw >= 15000 || usb_type == POWER_SUPPLY_USB_TYPE_PD_PPS) return QUICK_CHARGE_FAST;

	return QUICK_CHARGE_NORMAL;
}

static int get_protocol_code(void) {
	switch (get_usb_type()) {
	case POWER_SUPPLY_USB_TYPE_PD_PPS: return PROTOCOL_PD_PPS;
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_DRP: return PROTOCOL_PD;
	default:                           return PROTOCOL_NORMAL;
	}
}

static int get_soh(void) {
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int full = 0, design = 0;

	psy = power_supply_get_by_name("battery");
	if (!psy) return -1;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL, &val)) full = val.intval;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &val)) design = val.intval;
	power_supply_put(psy);
	return (full > 0 && design > 0) ? (full * 100) / design : -1;
}

static const char *health_str(int h) {
	switch (h) {
	case POWER_SUPPLY_HEALTH_GOOD:        return "Good";
	case POWER_SUPPLY_HEALTH_OVERHEAT:    return "Overheat";
	case POWER_SUPPLY_HEALTH_DEAD:        return "Dead";
	case POWER_SUPPLY_HEALTH_OVERVOLTAGE: return "Overvoltage";
	case POWER_SUPPLY_HEALTH_COLD:        return "Cold";
	case POWER_SUPPLY_HEALTH_WARM:        return "Warm";
	case POWER_SUPPLY_HEALTH_COOL:        return "Cool";
	default:                              return "Unknown";
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
static const struct file_operations ufs_read_mb_fops = { .read = ufs_read_mb_read, .write = ufs_read_mb_write };

static ssize_t ufs_write_mb_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, ufs_write_mb, strlen(ufs_write_mb));
}
static ssize_t ufs_write_mb_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    if (count >= sizeof(ufs_write_mb)) return -EINVAL;
    if (copy_from_user(ufs_write_mb, buf, count)) return -EFAULT;
    ufs_write_mb[count] = '\0';
    return count;
}
static const struct file_operations ufs_write_mb_fops = { .read = ufs_write_mb_read, .write = ufs_write_mb_write };

static ssize_t dummy_sink_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) { return count; }
static ssize_t dummy_zero_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, "0\n", 2);
}
static const struct file_operations dummy_sink_fops = { .read = dummy_zero_read, .write = dummy_sink_write };

static ssize_t devinfo_lcd_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, lcd_info, strlen(lcd_info));
}
static ssize_t devinfo_lcd_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    if (count >= sizeof(lcd_info)) return -EINVAL;
    if (copy_from_user(lcd_info, buf, count)) return -EFAULT;
    lcd_info[count] = '\0';
    return count;
}
static const struct file_operations devinfo_lcd_fops = { .read = devinfo_lcd_read, .write = devinfo_lcd_write };

static ssize_t devinfo_lcd_s_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    return simple_read_from_buffer(buf, count, ppos, "none\n", 5);
}
static const struct file_operations devinfo_lcd_s_fops = { .read = devinfo_lcd_s_read, .write = dummy_sink_write };

static int batt_health_show(struct seq_file *m, void *v) {
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int soc = -1, health = -1, cycles = -1;
	int charge_full = -1, charge_full_design = -1, soh;

	psy = power_supply_get_by_name("battery");
	if (!psy) {
		seq_puts(m, "battery PSY not available\n");
		return 0;
	}

	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &val)) soc = val.intval;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_HEALTH, &val)) health = val.intval;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CYCLE_COUNT, &val)) cycles = val.intval;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL, &val)) charge_full = val.intval;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &val)) charge_full_design = val.intval;
	power_supply_put(psy);

	seq_printf(m, "soc:                %d%%\n", soc);
	seq_printf(m, "health:             %s\n", health >= 0 ? health_str(health) : "N/A");
	seq_printf(m, "cycle_count:        %d\n", cycles);

	if (charge_full > 0 && charge_full_design > 0) {
		soh = (charge_full * 100) / charge_full_design;
		seq_printf(m, "charge_full:        %d mAh\n", charge_full / 1000);
		seq_printf(m, "charge_full_design: %d mAh\n", charge_full_design / 1000);
		seq_printf(m, "soh:                %d%%\n", soh);
	} else {
		seq_puts(m, "charge_full:        N/A\n");
		seq_puts(m, "soh:                N/A\n");
	}

	seq_printf(m, "smart_charge:       %s\n", smart_chg_enable ? "enabled" : "disabled");
	seq_printf(m, "charge_limit:       100%%\n");
	seq_printf(m, "input_suspended:    %s\n", input_suspended ? "yes" : "no");
	return 0;
}

static int batt_health_open(struct inode *inode, struct file *file) {
    return single_open(file, batt_health_show, NULL); 
}

static const struct file_operations batt_health_ops = { .open = batt_health_open, .read = seq_read, .llseek = seq_lseek, .release = single_release };

static ssize_t fast_charge_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_quick_charge_type() >= QUICK_CHARGE_FAST ? 1 : 0);
}
static DEVICE_ATTR_RO(fast_charge);

static ssize_t fast_chg_type_show(struct device *dev, struct device_attribute *attr, char *buf) {
    int power_mw = get_adapter_power_mw();
    int usb_type = get_usb_type();
    int type = 0;

    if (power_mw >= 65000) {
        type = 2;
    } else if (power_mw >= 15000 || usb_type == POWER_SUPPLY_USB_TYPE_PD_PPS) {
        type = 1;
    } else if (usb_type == POWER_SUPPLY_USB_TYPE_PD || usb_type == POWER_SUPPLY_USB_TYPE_PD_DRP) {
        type = 3;
    }

    return scnprintf(buf, PAGE_SIZE, "%d\n", type);
}
static DEVICE_ATTR_RO(fast_chg_type);

static ssize_t ppschg_ing_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_usb_type() == POWER_SUPPLY_USB_TYPE_PD_PPS ? 1 : 0);
}
static DEVICE_ATTR_RO(ppschg_ing);

static ssize_t ppschg_power_show(struct device *dev, struct device_attribute *attr, char *buf) {
	if (get_usb_type() != POWER_SUPPLY_USB_TYPE_PD_PPS) return scnprintf(buf, PAGE_SIZE, "0\n");
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_adapter_power_mw());
}
static DEVICE_ATTR_RO(ppschg_power);

static ssize_t battery_fcc_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int fcc = 0;
	psy = power_supply_get_by_name("battery");
	if (psy) {
		if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL, &val)) fcc = val.intval / 1000;
		power_supply_put(psy);
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", fcc);
}
static DEVICE_ATTR_RO(battery_fcc);

static ssize_t battery_rm_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct power_supply *psy;
	union power_supply_propval fcc = { 0 }, soc = { 0 };
	int rm = 0;
	psy = power_supply_get_by_name("battery");
	if (psy) {
		if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL, &fcc) &&
		    !power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &soc))
			rm = (fcc.intval / 1000) * soc.intval / 100;
		power_supply_put(psy);
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", rm);
}
static DEVICE_ATTR_RO(battery_rm);

static ssize_t soh_show(struct device *dev, struct device_attribute *attr, char *buf) {
	int soh = get_soh();
	return scnprintf(buf, PAGE_SIZE, "%d\n", soh >= 0 ? soh : 0);
}
static DEVICE_ATTR_RO(soh);

static ssize_t gauge_car_c_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return soh_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(gauge_car_c);

static ssize_t battery_notify_code_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "0\n");
}
static DEVICE_ATTR_RO(battery_notify_code);

static ssize_t smart_charging_enable_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", smart_chg_enable ? 1 : 0);
}
static ssize_t smart_charging_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	int val;
	if (kstrtoint(buf, 10, &val)) return -EINVAL;
	smart_chg_enable = !!val;
	return count;
}
static DEVICE_ATTR(smart_charging_enable, 0664, smart_charging_enable_show, smart_charging_enable_store);

static ssize_t charging_protect_level_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "100\n");
}
static ssize_t charging_protect_level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	return count;
}
static DEVICE_ATTR(charging_protect_level, 0664, charging_protect_level_show, charging_protect_level_store);

static ssize_t mmi_charging_enable_show(struct device *dev, struct device_attribute *attr, char *buf) {
	int val = 0;
	if (read_sysfs_int("/sys/class/power_supply/battery/input_suspend", &val) < 0) val = 0;
	return scnprintf(buf, PAGE_SIZE, "%d\n", !val);
}
static ssize_t mmi_charging_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct file *fp;
	loff_t pos = 0;
	int val;
	char val_str[8];
	int len;

	if (kstrtoint(buf, 10, &val)) return -EINVAL;

	fp = filp_open("/sys/class/power_supply/battery/input_suspend", O_WRONLY, 0);
	if (IS_ERR(fp)) return PTR_ERR(fp);

	len = snprintf(val_str, sizeof(val_str), "%d", !val);
	kernel_write(fp, val_str, len, &pos);
	filp_close(fp, NULL);

	input_suspended = !val;
	return count;
}
static DEVICE_ATTR(mmi_charging_enable, 0664, mmi_charging_enable_show, mmi_charging_enable_store);

static ssize_t input_suspend_show(struct device *dev, struct device_attribute *attr, char *buf) {
	int val = 0;
	if (read_sysfs_int("/sys/class/power_supply/battery/input_suspend", &val) < 0) val = 0;
	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}
static ssize_t input_suspend_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct file *fp;
	loff_t pos = 0;
	int val;
	char val_str[8];
	int len;

	if (kstrtoint(buf, 10, &val)) return -EINVAL;

	fp = filp_open("/sys/class/power_supply/battery/input_suspend", O_WRONLY, 0);
	if (IS_ERR(fp)) return PTR_ERR(fp);

	len = snprintf(val_str, sizeof(val_str), "%d", !!val);
	kernel_write(fp, val_str, len, &pos);
	filp_close(fp, NULL);

	input_suspended = !!val;
	return count;
}
static DEVICE_ATTR(input_suspend, 0664, input_suspend_show, input_suspend_store);

static ssize_t cool_down_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", cool_down_level);
}
static ssize_t cool_down_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	int val;
	if (kstrtoint(buf, 10, &val)) return -EINVAL;
	if (val < 0) return -EINVAL;
	cool_down_level = val;
	return count;
}
static DEVICE_ATTR(cool_down, 0664, cool_down_show, cool_down_store);

static ssize_t bcc_current_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", bcc_current_ma);
}
static ssize_t bcc_current_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	int val;
	if (kstrtoint(buf, 10, &val)) return -EINVAL;
	if (val < 0) return -EINVAL;
	bcc_current_ma = min(val, 9000);
	return count;
}
static DEVICE_ATTR(bcc_current, 0664, bcc_current_show, bcc_current_store);

static ssize_t design_capacity_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int capacity = 0;
	psy = power_supply_get_by_name("battery");
	if (psy) {
		if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &val))
			capacity = val.intval / 1000;
		power_supply_put(psy);
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", capacity);
}
static DEVICE_ATTR_RO(design_capacity);

static ssize_t battery_type_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "N/A\n");
}
static DEVICE_ATTR_RO(battery_type);

static ssize_t chip_soc_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int soc = -1;
	psy = power_supply_get_by_name("battery");
	if (psy) {
		if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &val))
			soc = val.intval;
		power_supply_put(psy);
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", soc >= 0 ? soc : 0);
}
static DEVICE_ATTR_RO(chip_soc);

static ssize_t ui_soc_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return chip_soc_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(ui_soc);

static ssize_t smooth_soc_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return chip_soc_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(smooth_soc);

static ssize_t smartchg_soh_support_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct power_supply *psy;
	union power_supply_propval val = { 0 };
	int soh = -1;
	psy = power_supply_get_by_name("battery");
	if (psy) {
		if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL, &val)) {
			int design = 0;
			val.intval = 0;
			power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &val);
			design = val.intval;
			val.intval = 0;
			power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_FULL, &val);
			if (design > 0) soh = (val.intval * 100) / design;
		}
		power_supply_put(psy);
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", soh >= 0 ? soh : 0);
}
static DEVICE_ATTR_RO(smartchg_soh_support);

static struct attribute *oplus_chg_battery_attrs[] = {
	&dev_attr_fast_charge.attr,
	&dev_attr_fast_chg_type.attr,
	&dev_attr_ppschg_ing.attr,
	&dev_attr_ppschg_power.attr,
	&dev_attr_battery_fcc.attr,
	&dev_attr_battery_rm.attr,
	&dev_attr_soh.attr,
	&dev_attr_gauge_car_c.attr,
	&dev_attr_battery_notify_code.attr,
	&dev_attr_smart_charging_enable.attr,
	&dev_attr_charging_protect_level.attr,
	&dev_attr_mmi_charging_enable.attr,
	&dev_attr_input_suspend.attr,
	&dev_attr_cool_down.attr,
	&dev_attr_bcc_current.attr,
	&dev_attr_design_capacity.attr,
	&dev_attr_battery_type.attr,
	&dev_attr_chip_soc.attr,
	&dev_attr_ui_soc.attr,
	&dev_attr_smooth_soc.attr,
	&dev_attr_smartchg_soh_support.attr,
	NULL,
};
static const struct attribute_group oplus_chg_battery_group = {
	.attrs = oplus_chg_battery_attrs,
};

static ssize_t usb_status_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return 0;
}
static DEVICE_ATTR_RO(usb_status);

static ssize_t otg_online_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "0\n");
}
static DEVICE_ATTR_RO(otg_online);

static ssize_t otg_switch_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", otg_switch);
}
static ssize_t otg_switch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	int val;
	if (kstrtoint(buf, 10, &val)) return -EINVAL;
    otg_switch = !!val;
	return count;
}
static DEVICE_ATTR(otg_switch, 0664, otg_switch_show, otg_switch_store);

static struct attribute *oplus_chg_usb_attrs[] = {
	&dev_attr_usb_status.attr,
	&dev_attr_otg_online.attr,
	&dev_attr_otg_switch.attr,
	NULL,
};
static const struct attribute_group oplus_chg_usb_group = {
	.attrs = oplus_chg_usb_attrs,
};

static ssize_t protocol_type_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_protocol_code());
}
static DEVICE_ATTR_RO(protocol_type);

static ssize_t ui_power_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_adapter_power_mw());
}
static DEVICE_ATTR_RO(ui_power);

static ssize_t cpa_power_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return ui_power_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(cpa_power);

static ssize_t plc_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "0\n");
}
static DEVICE_ATTR_RO(plc);

static ssize_t adapter_power_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return ui_power_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(adapter_power);

static ssize_t chg_up_limit_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%d\n", chg_up_limit_value);
}
static ssize_t chg_up_limit_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	int val;
	if (kstrtoint(buf, 10, &val)) return -EINVAL;
	chg_up_limit_value = val;
	return count;
}
static DEVICE_ATTR(chg_up_limit, 0664, chg_up_limit_show, chg_up_limit_store);

static DECLARE_WAIT_QUEUE_HEAD(mutual_cmd_wq);
static int mutual_cmd_val = 0;
static ssize_t mutual_cmd_show(struct device *dev, struct device_attribute *attr, char *buf) {
    wait_event_interruptible_timeout(mutual_cmd_wq, false, msecs_to_jiffies(30000));
    return sprintf(buf, "%d\n", mutual_cmd_val);
}
static ssize_t mutual_cmd_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    if (sscanf(buf, "%d", &mutual_cmd_val) == 1) wake_up_interruptible(&mutual_cmd_wq);
    return count;
}
static DEVICE_ATTR_RW(mutual_cmd);

static ssize_t charger_wattage_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct power_supply *psy;
    union power_supply_propval val = { 0 };
    int power_w = 0;

    psy = power_supply_get_by_name("usb");
    if (psy) {
        if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_POWER_MAX, &val)) {
            power_w = val.intval;
        }
        power_supply_put(psy);
    }

    return scnprintf(buf, PAGE_SIZE, "%d\n", power_w);
}
static DEVICE_ATTR_RO(charger_wattage);

static struct attribute *oplus_chg_common_attrs[] = {
	&dev_attr_protocol_type.attr,
	&dev_attr_ui_power.attr,
	&dev_attr_cpa_power.attr,
	&dev_attr_plc.attr,
	&dev_attr_adapter_power.attr,
	&dev_attr_chg_up_limit.attr,
    &dev_attr_mutual_cmd.attr,
    &dev_attr_charger_wattage.attr,
	NULL,
};
static const struct attribute_group oplus_chg_common_group = {
	.attrs = oplus_chg_common_attrs,
};

static ssize_t ac_online_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "0\n");
}
static DEVICE_ATTR_RO(ac_online);

static struct attribute *oplus_chg_ac_attrs[] = {
	&dev_attr_ac_online.attr,
	NULL,
};
static const struct attribute_group oplus_chg_ac_group = {
	.attrs = oplus_chg_ac_attrs,
};

static ssize_t usb_quick_charge_type_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return scnprintf(buf, PAGE_SIZE, "%u", get_quick_charge_type());
}
static DEVICE_ATTR(quick_charge_type, 0444, usb_quick_charge_type_show, NULL);

static void try_add_usb_attr(void) {
	struct power_supply *usb_psy;

	if (usb_attr_added) return;
	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) return;
	if (!device_create_file(&usb_psy->dev, &dev_attr_quick_charge_type))
		usb_attr_added = true;
	power_supply_put(usb_psy);
}

static int psy_notifier_call(struct notifier_block *nb, unsigned long event, void *data) {
	struct power_supply *psy = data;
	if (psy && !strcmp(psy->desc->name, "usb")) try_add_usb_attr();
	return NOTIFY_OK;
}

static struct device *oplus_chg_create_dev(const char *name, int minor, const struct attribute_group *grp) {
	struct device *dev;
	int rc;

	dev = device_create(oplus_chg_class, NULL, MKDEV(0, minor), NULL, name);
	if (IS_ERR(dev)) return dev;
	rc = sysfs_create_group(&dev->kobj, grp);
	if (rc) {
		device_destroy(oplus_chg_class, MKDEV(0, minor));
		return ERR_PTR(rc);
	}
	return dev;
}

static int __init oplus_stub_nodes_init(void) {
    struct proc_dir_entry *oplus_storage, *io_metrics, *forever;
    struct proc_dir_entry *oplus_scheduler, *sched_assist;
    struct proc_dir_entry *oplus_afs, *oplus_mem, *devinfo, *proc_storage;
    struct proc_dir_entry *charger_dir;
    int rc;

    set_proc_owner(proc_create("bootprof", 0666, NULL, &dummy_sink_fops));
    set_proc_owner(proc_create("theiaPwkReport", 0666, NULL, &dummy_sink_fops));

    proc_storage = proc_mkdir("storage", NULL);
    if (proc_storage) {
        set_proc_owner(proc_storage);
        set_proc_owner(proc_create("buf_log", 0666, proc_storage, &dummy_sink_fops));
    }

    oplus_storage = proc_mkdir("oplus_storage", NULL);
    if (oplus_storage) {
        set_proc_owner(oplus_storage);
        io_metrics = proc_mkdir("io_metrics", oplus_storage);
        if (io_metrics) {
            set_proc_owner(io_metrics);
            forever = proc_mkdir("forever", io_metrics);
            if (forever) {
                set_proc_owner(forever);
                set_proc_owner(proc_create("ufs_total_read_size_mb", 0666, forever, &ufs_read_mb_fops));
                set_proc_owner(proc_create("ufs_total_write_size_mb", 0666, forever, &ufs_write_mb_fops));
            }
        }
    }

    oplus_scheduler = proc_mkdir("oplus_scheduler", NULL);
    if (oplus_scheduler) {
        set_proc_owner(oplus_scheduler);
        sched_assist = proc_mkdir("sched_assist", oplus_scheduler);
        if (sched_assist) {
            set_proc_owner(sched_assist);
            set_proc_owner(proc_create("sched_assist_scene", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("sched_impt_task", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("im_flag_app", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("im_flag", 0666, sched_assist, &dummy_sink_fops));
            set_proc_owner(proc_create("ux_task", 0666, sched_assist, &dummy_sink_fops));
        }
    }

    oplus_afs = proc_mkdir("oplus_afs_config", NULL);
    if (oplus_afs) {
        set_proc_owner(oplus_afs);
        set_proc_owner(proc_create("afs_config", 0666, oplus_afs, &dummy_sink_fops));
    }

    oplus_mem = proc_mkdir("oplus_mem", NULL);
    if (oplus_mem) {
        set_proc_owner(oplus_mem);
        set_proc_owner(proc_create("memory_monitor", 0666, oplus_mem, &dummy_sink_fops));
    }

    devinfo = proc_mkdir("devinfo", NULL);
    if (devinfo) {
        set_proc_owner(devinfo);
        set_proc_owner(proc_create("lcd", 0666, devinfo, &devinfo_lcd_fops));
        set_proc_owner(proc_create("lcd_s", 0666, devinfo, &devinfo_lcd_s_fops));
    }

    charger_dir = proc_mkdir("oplus_charger", NULL);
    if (charger_dir) {
        set_proc_owner(charger_dir);
        set_proc_owner(proc_create("batt_health", 0444, charger_dir, &batt_health_ops));
    }

    oplus_chg_class = class_create(THIS_MODULE, "oplus_chg");
    if (!IS_ERR(oplus_chg_class) && oplus_chg_class) {
		oplus_chg_battery_dev = oplus_chg_create_dev("battery", 0, &oplus_chg_battery_group);
		oplus_chg_usb_dev = oplus_chg_create_dev("usb", 1, &oplus_chg_usb_group);
		oplus_chg_common_dev = oplus_chg_create_dev("common", 2, &oplus_chg_common_group);
		oplus_chg_ac_dev = oplus_chg_create_dev("ac", 3, &oplus_chg_ac_group);
    }

	psy_nb.notifier_call = psy_notifier_call;
	rc = power_supply_reg_notifier(&psy_nb);
	if (rc) pr_warn("oplus_charger: psy notifier failed: %d\n", rc);

	try_add_usb_attr();

    return 0;
}

static void __exit oplus_stub_nodes_exit(void) {
	power_supply_unreg_notifier(&psy_nb);

	if (usb_attr_added) {
		struct power_supply *usb_psy = power_supply_get_by_name("usb");
		if (usb_psy) {
			device_remove_file(&usb_psy->dev, &dev_attr_quick_charge_type);
			power_supply_put(usb_psy);
		}
	}

	if (oplus_chg_class && oplus_chg_ac_dev) {
		sysfs_remove_group(&oplus_chg_ac_dev->kobj, &oplus_chg_ac_group);
		device_destroy(oplus_chg_class, MKDEV(0, 3));
	}
	if (oplus_chg_class && oplus_chg_common_dev) {
		sysfs_remove_group(&oplus_chg_common_dev->kobj, &oplus_chg_common_group);
		device_destroy(oplus_chg_class, MKDEV(0, 2));
	}
	if (oplus_chg_class && oplus_chg_usb_dev) {
		sysfs_remove_group(&oplus_chg_usb_dev->kobj, &oplus_chg_usb_group);
		device_destroy(oplus_chg_class, MKDEV(0, 1));
	}
	if (oplus_chg_class && oplus_chg_battery_dev) {
		sysfs_remove_group(&oplus_chg_battery_dev->kobj, &oplus_chg_battery_group);
		device_destroy(oplus_chg_class, MKDEV(0, 0));
	}
	if (oplus_chg_class) class_destroy(oplus_chg_class);
}

module_init(oplus_stub_nodes_init);
module_exit(oplus_stub_nodes_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danda");