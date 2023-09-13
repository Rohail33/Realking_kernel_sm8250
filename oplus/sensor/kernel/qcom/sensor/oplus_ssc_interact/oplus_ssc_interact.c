/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "<ssc_interactive>" fmt

#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include "oplus_ssc_interact.h"
#include <linux/of.h>
#include <linux/of_device.h>

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
#include <linux/soc/qcom/panel_event_notifier.h>
#include <drm/drm_panel.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/task_work.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/delay.h>
#endif

#define FIFO_SIZE 32
#define LB_TO_HB_THRD    150

/*static DECLARE_KFIFO_PTR(test, struct fifo_frame);*/

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
/* to do */
#else
extern int register_lcdinfo_notifier(struct notifier_block *nb);
extern int unregister_lcdinfo_notifier(struct notifier_block *nb);
#endif

static struct ssc_interactive *g_ssc_cxt = NULL;

static void ssc_interactive_set_fifo(uint8_t type, uint16_t data)
{
	struct fifo_frame fifo_fm;
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;
	int ret = 0;
	/*pr_info("type= %u, data=%d\n", type, data);*/
	memset(&fifo_fm, 0, sizeof(struct fifo_frame));
	fifo_fm.type = type;
	fifo_fm.data = data;
	ret = kfifo_in_spinlocked(&ssc_cxt->fifo, &fifo_fm, 1, &ssc_cxt->fifo_lock);
	if(ret != 1) {
		pr_err("kfifo is full\n");
	}
	wake_up_interruptible(&ssc_cxt->wq);
}


static void ssc_interactive_set_dc_mode(uint16_t dc_mode)
{
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;
	spin_lock(&ssc_cxt->rw_lock);
	if (dc_mode == ssc_cxt->a_info.dc_mode) {
		/*pr_info("dc_mode=%d is the same\n", dc_mode);*/
		spin_unlock(&ssc_cxt->rw_lock);
		return;
	}
	pr_info("start dc_mode=%d\n", dc_mode);
	ssc_cxt->a_info.dc_mode = dc_mode;
	spin_unlock(&ssc_cxt->rw_lock);


	ssc_interactive_set_fifo(LCM_DC_MODE_TYPE, dc_mode);
}

static void ssc_interactive_set_brightness(uint16_t brigtness)
{
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;
	spin_lock(&ssc_cxt->rw_lock);
/*	if(brigtness > LB_TO_HB_THRD)
		brigtness = 1023;
	if(brigtness < ssc_cxt->m_dvb_coef.dvb1) {
		brigtness = ssc_cxt->m_dvb_coef.dvb1 - 1;
	} else if(brigtness < ssc_cxt->m_dvb_coef.dvb2) {
		brigtness = ssc_cxt->m_dvb_coef.dvb2 - 1;
	} else if(brigtness < ssc_cxt->m_dvb_coef.dvb3) {
		brigtness = ssc_cxt->m_dvb_coef.dvb3 - 1;
	} else if (brigtness >= ssc_cxt->m_dvb_coef.dvb_l2h) {
		brigtness = 1023;
	} else {
		//do noting
		spin_unlock(&ssc_cxt->rw_lock);
		return;
	}
*/

	if (brigtness == ssc_cxt->a_info.brightness) {
		/*pr_info("brigtness=%d is the same\n", brigtness);*/
		spin_unlock(&ssc_cxt->rw_lock);
		return;
	}
	/*pr_info("brigtness=%d brightness=%d\n", brigtness, ssc_cxt->a_info.brightness);*/

	ssc_cxt->a_info.brightness = brigtness;
	spin_unlock(&ssc_cxt->rw_lock);

	ssc_interactive_set_fifo(LCM_BRIGHTNESS_TYPE, brigtness);
}

static ssize_t ssc_interactive_write(struct file *file, const char __user * buf,
                size_t count, loff_t * ppos)
{
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;
	pr_info("ssc_interactive_write start\n");
	if (count > *ppos) {
		count -= *ppos;
	} else
		count = 0;

	*ppos += count;
	wake_up_interruptible(&ssc_cxt->wq);
	return count;
}

static unsigned int ssc_interactive_poll(struct file *file, struct poll_table_struct *pt)
{
	unsigned int ptr = 0;
	int count = 0;
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;

	poll_wait(file, &ssc_cxt->wq, pt);
	spin_lock(&ssc_cxt->fifo_lock);
	count = kfifo_len(&ssc_cxt->fifo);
	spin_unlock(&ssc_cxt->fifo_lock);
	if (count > 0) {
		ptr |= POLLIN | POLLRDNORM;
	}
	return ptr;
}

static ssize_t ssc_interactive_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	size_t read = 0;
	int fifo_count = 0;
	int ret;
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;

	if (count !=0 && count < sizeof(struct fifo_frame)) {
		pr_err("err count %lu\n", count);
		return -EINVAL;
	}
	while ((read + sizeof(struct fifo_frame)) <= count) {
		struct fifo_frame fifo_fm;
		spin_lock(&ssc_cxt->fifo_lock);
		fifo_count = kfifo_len(&ssc_cxt->fifo);
		spin_unlock(&ssc_cxt->fifo_lock);

		if (fifo_count <= 0) {
			break;
		}
		ret = kfifo_out(&ssc_cxt->fifo, &fifo_fm, 1);
		if (copy_to_user(buf+read, &fifo_fm, sizeof(struct fifo_frame))) {
			pr_err("copy_to_user failed \n");
			return -EFAULT;
		}
		read += sizeof(struct fifo_frame);
	}
	*ppos += read;
	return read;
}

static int ssc_interactive_release (struct inode *inode, struct file *file)
{
	pr_info("%s\n", __func__);
	return 0;
}

static const struct file_operations under_mdevice_fops = {
	.owner  = THIS_MODULE,
	.read   = ssc_interactive_read,
	.write        = ssc_interactive_write,
	.poll         = ssc_interactive_poll,
	.llseek = generic_file_llseek,
	.release = ssc_interactive_release,
};

static ssize_t brightness_store(struct device *dev,
        struct device_attribute *attr, const char *buf,
        size_t count)
{
	uint8_t type = 0;
	uint16_t data = 0;
	int err = 0;

	err = sscanf(buf, "%hhu %hu", &type, &data);
	pr_info("brightness_store2 start = %s, brightness =%d\n", buf, data);
	if (err < 0) {
		pr_err("brightness_store error: err = %d\n", err);
		return err;
	}
	if (type != LCM_BRIGHTNESS_TYPE) {
		pr_err("brightness_store type not match  = %d\n", type);
		return count;
	}


	ssc_interactive_set_brightness(data);

	pr_info("brightness_store = %s, brightness =%d\n", buf, data);

	return count;
}

static ssize_t brightness_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;
	uint16_t brightness = 0;

	spin_lock(&ssc_cxt->rw_lock);
	brightness = ssc_cxt->a_info.brightness;
	spin_unlock(&ssc_cxt->rw_lock);

	pr_info("brightness_show brightness=  %d\n", brightness);

	return sprintf(buf, "%d\n", brightness);
}


static ssize_t dc_mode_store(struct device *dev,
        struct device_attribute *attr, const char *buf,
        size_t count)
{
	uint8_t type = 0;
	uint16_t data = 0;
	int err = 0;

	err = sscanf(buf, "%hhu %hu", &type, &data);
	if (err < 0) {
		pr_err("dc_mode_store error: err = %d\n", err);
		return err;
	}
	if (type != LCM_DC_MODE_TYPE) {
		pr_err("dc_mode_store type not match  = %d\n", type);
		return count;
	}
	ssc_interactive_set_dc_mode(data);
	return count;
}

static ssize_t dc_mode_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;
	uint16_t dc_mode = 0;

	spin_lock(&ssc_cxt->rw_lock);
	dc_mode = ssc_cxt->a_info.dc_mode;
	spin_unlock(&ssc_cxt->rw_lock);

	pr_info("dc_mode_show dc_mode= %u\n", dc_mode);

	return snprintf(buf, PAGE_SIZE, "%d\n", dc_mode);
}


DEVICE_ATTR(brightness, 0644, brightness_show, brightness_store);
DEVICE_ATTR(dc_mode, 0644, dc_mode_show, dc_mode_store);



static struct attribute *ssc_interactive_attributes[] = {
	&dev_attr_brightness.attr,
	&dev_attr_dc_mode.attr,
	NULL
};



static struct attribute_group ssc_interactive_attribute_group = {
	.attrs = ssc_interactive_attributes
};

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
static void lcdinfo_callback(enum panel_event_notifier_tag tag,
        struct panel_event_notification *notification, void *client_data)
{
	if (!notification) {
		pr_err("Invalid notification\n");
		return;
	}

	/*pr_err("Notification type:%d, data:%d",
			notification->notif_type,
			notification->notif_data.data);*/

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BACKLIGHT:
		ssc_interactive_set_brightness(notification->notif_data.data);
		break;
	case DRM_PANEL_EVENT_DC_MODE:
		ssc_interactive_set_dc_mode(notification->notif_data.data);
		break;
	default:
		break;
	}
	return;
}
#else
static void ssc_interactive_set_power_mode(uint16_t power_mode)
{
        struct ssc_interactive *ssc_cxt = g_ssc_cxt;

        spin_lock(&ssc_cxt->rw_lock);
        if (power_mode == ssc_cxt->a_info.power_mode) {
                spin_unlock(&ssc_cxt->rw_lock);
                return;
        }
        ssc_cxt->a_info.power_mode = power_mode;
        spin_unlock(&ssc_cxt->rw_lock);

        ssc_interactive_set_fifo(LCM_POWER_MODE, power_mode);
}

static void ssc_interactive_set_pad_power_mode(uint16_t power_mode)
{
        struct ssc_interactive *ssc_cxt = g_ssc_cxt;

        spin_lock(&ssc_cxt->rw_lock);
        if (power_mode == ssc_cxt->a_info.pad_power_mode) {
                spin_unlock(&ssc_cxt->rw_lock);
                return;
        }
        ssc_cxt->a_info.pad_power_mode = power_mode;
        spin_unlock(&ssc_cxt->rw_lock);

        ssc_interactive_set_fifo(LCM_POWER_MODE_SEC, power_mode);
}

static void ssc_interactive_set_pad_brightness(uint16_t brigtness)
{
        struct ssc_interactive *ssc_cxt = g_ssc_cxt;

        spin_lock(&ssc_cxt->rw_lock);
        if (brigtness == ssc_cxt->a_info.pad_brightness) {
                spin_unlock(&ssc_cxt->rw_lock);
                return;
        }
        ssc_cxt->a_info.pad_brightness = brigtness;
        spin_unlock(&ssc_cxt->rw_lock);

        ssc_interactive_set_fifo(LCM_BRIGHTNESS_TYPE_SEC, brigtness);
}

static int lcdinfo_callback(struct notifier_block *nb, unsigned long event,
        void *data)
{
	int val = 0;
	if (!data) {
		return 0;
	}
	val = *(int*)data;
	switch (event) {
	case LCM_DC_MODE_TYPE:
		ssc_interactive_set_dc_mode(val);
		break;
	case LCM_BRIGHTNESS_TYPE:
		ssc_interactive_set_brightness(val);
		break;
	case LCM_BRIGHTNESS_TYPE_SEC:
		ssc_interactive_set_pad_brightness(val);
		break;
	case LCM_POWER_MODE:
		ssc_interactive_set_pad_power_mode(val);
		break;
	case LCM_POWER_MODE_SEC:
		ssc_interactive_set_power_mode(val);
		break;
	default:
		break;
	}
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
static void ssc_regiseter_lcd_notify_work(struct work_struct *work)
{
	int rc = 0;
	int i;
	int count;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;
	struct device_node *np = NULL;
	void *cookie = NULL;
	void *data = NULL;

	np = of_find_node_by_name(NULL, "sensor_dev");
	if (!np) {
		pr_err("Device tree info missing.\n");
		return;
	} else {
		pr_err("Device tree info found.\n");
	}

	count = of_count_phandle_with_args(np, "oplus,display_panel", NULL);
	if (count <= 0) {
		pr_err("oplus,display_panel not found\n");
		return;
	}

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "oplus,display_panel", i);
		panel = of_drm_find_panel(node);
		pr_err("%s: panel[%d] IS_ERR =%d \n", __func__, i, IS_ERR(panel));
		of_node_put(node);
		if (!IS_ERR(panel)) {
			g_ssc_cxt->active_panel = panel;
		} else {
			rc = PTR_ERR(panel);
		}
	}

	if (g_ssc_cxt->active_panel) {
		cookie = panel_event_notifier_register(
			PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_BACKLIGHT,
			g_ssc_cxt->active_panel, &lcdinfo_callback,
			data);
		if (!cookie) {
			pr_err("Unable to register chg_panel_notifier\n");
		} else {
			pr_err("success register chg_panel_notifier\n");
			g_ssc_cxt->notify_work_regiseted = true;
			g_ssc_cxt->notifier_cookie = cookie;
		}
	} else {
		pr_err("can't find active panel, rc=%d\n", rc);
	}

	if (!g_ssc_cxt->notify_work_regiseted && g_ssc_cxt->notify_work_retry > 0) {
		g_ssc_cxt->notify_work_retry--;
		schedule_delayed_work(&g_ssc_cxt->regiseter_lcd_notify_work, msecs_to_jiffies(1000));
	}
	return;
}
#endif

static int __init ssc_interactive_init(void)
{
	int err = 0;
	int rc = 0;
	uint32_t lb_value = 0;
	struct ssc_interactive *ssc_cxt = kzalloc(sizeof(*ssc_cxt), GFP_KERNEL);
	struct device_node *lb_chek = NULL;

	g_ssc_cxt = ssc_cxt;
	if (ssc_cxt == NULL) {
		pr_err("kzalloc ssc_cxt failed\n");
		err = -ENOMEM;
		goto alloc_ssc_cxt_failed;
	}
	err = kfifo_alloc(&ssc_cxt->fifo, FIFO_SIZE, GFP_KERNEL);
	if (err) {
		pr_err("kzalloc kfifo failed\n");
		err = -ENOMEM;
		goto alloc_fifo_failed;
	}

	spin_lock_init(&ssc_cxt->fifo_lock);
	spin_lock_init(&ssc_cxt->rw_lock);

	init_waitqueue_head(&ssc_cxt->wq);

	memset(&ssc_cxt->mdev, 0 , sizeof(struct miscdevice));
	ssc_cxt->mdev.minor = MISC_DYNAMIC_MINOR;
	ssc_cxt->mdev.name = "ssc_interactive";
	ssc_cxt->mdev.fops = &under_mdevice_fops;
	if (misc_register(&ssc_cxt->mdev) != 0) {
		pr_err("misc_register  mdev failed\n");
		err = -ENODEV;
		goto register_mdevice_failed;
	}

	lb_chek = of_find_node_with_property(NULL, "use_lb_algo");
	if (NULL != lb_chek) {
		rc = of_property_read_u32(lb_chek, "use_lb_algo", &lb_value);
		if (0 == rc && 1 == lb_value) {
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
			ssc_cxt->notify_work_retry = 10;
			ssc_cxt->notify_work_regiseted = false;
			INIT_DELAYED_WORK(&ssc_cxt->regiseter_lcd_notify_work, ssc_regiseter_lcd_notify_work);
			schedule_delayed_work(&ssc_cxt->regiseter_lcd_notify_work, msecs_to_jiffies(1500));
#else
			ssc_cxt->nb.notifier_call = lcdinfo_callback;
			register_lcdinfo_notifier(&ssc_cxt->nb);
#endif
		} else {
			pr_err("Does not support low light mode");
		}
	} else {
		pr_err("Does not support low light mode");
	}

	err = sysfs_create_group(&ssc_cxt->mdev.this_device->kobj, &ssc_interactive_attribute_group);
	if (err < 0) {
		pr_err("unable to create ssc_interactive_attribute_group file err=%d\n", err);
		goto sysfs_create_failed;
	}

	ssc_cxt->m_dvb_coef.dvb1         = 180;
	ssc_cxt->m_dvb_coef.dvb2         = 250;
	ssc_cxt->m_dvb_coef.dvb3         = 320;
	ssc_cxt->m_dvb_coef.dvb_l2h      = 350;
	ssc_cxt->m_dvb_coef.dvb_h2l      = 320;


	pr_info("ssc_interactive_init success!\n");

	return 0;
sysfs_create_failed:
	misc_deregister(&ssc_cxt->mdev);
register_mdevice_failed:
	kfifo_free(&ssc_cxt->fifo);
alloc_fifo_failed:
	kfree(ssc_cxt);
alloc_ssc_cxt_failed:
	return err;
}

static void __exit ssc_interactive_exit(void)
{
	struct ssc_interactive *ssc_cxt = g_ssc_cxt;
	sysfs_remove_group(&ssc_cxt->mdev.this_device->kobj, &ssc_interactive_attribute_group);
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
	if (ssc_cxt->active_panel && ssc_cxt->notifier_cookie) {
		panel_event_notifier_unregister(ssc_cxt->notifier_cookie);
	}
#else
	unregister_lcdinfo_notifier(&ssc_cxt->nb);
#endif
	misc_deregister(&ssc_cxt->mdev);
	kfifo_free(&ssc_cxt->fifo);
	kfree(ssc_cxt);
	ssc_cxt = NULL;
	g_ssc_cxt = NULL;
}

module_init(ssc_interactive_init);
module_exit(ssc_interactive_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JiangHua.Tang");
