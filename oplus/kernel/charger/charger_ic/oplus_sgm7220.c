/* sgm7220.c -- SGMICRO SGM7220 USB TYPE-C Controller device driver */
/* Copyright (c) 2009-2019, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include <linux/interrupt.h>
#include <linux/i2c.h>

#include <linux/errno.h>
#include <linux/err.h>

#include <linux/power_supply.h>
#include "oplus_battery_sm6375.h"
#include "../oplus_chg_module.h"
#include "../oplus_chg_ops_manager.h"

#undef pr_debug
#define pr_debug pr_err

extern int chg_init_done;
extern int typec_dir;
extern struct oplus_chg_chip *g_oplus_chip;
extern void oplus_enable_device_mode(bool enable);
//#define TYPEC_PM_OP

/******************************************************************************
* Register addresses
******************************************************************************/
#define REG_BASE                    0x00
/* 0x00 - 0x07 reserved for ID */
#define REG_MOD                     0x08
#define REG_INT                     0x09
#define REG_SET                     0x0A
#define REG_CTL                     0X45
#define REG_REVISION                0XA0

/******************************************************************************
* Register bits
******************************************************************************/
/* REG_ID (0x00) */
#define ID_REG_LEN  0X08

/* REG_MOD (0x08) */
#define MOD_ACTIVE_CABLE_DETECTION  0X01    /*RU*/
#define MOD_ACCESSORY_CONNECTED_SHIFT   1
#define MOD_ACCESSORY_CONNECTED         (0x07 << MOD_ACCESSORY_CONNECTED_SHIFT)    /*RU*/
#define MOD_CURRENT_MODE_DETECT_SHIFT    4
#define MOD_CURRENT_MODE_DETECT         (0x03 << MOD_CURRENT_MODE_DETECT_SHIFT)    /*RU*/
#define MOD_CURRENT_MODE_ADVERTISE_SHIFT    6
#define MOD_CURRENT_MODE_ADVERTISE          (0x03 << MOD_CURRENT_MODE_ADVERTISE_SHIFT)    /*RW*/

/* REG_INT (0x09) */
#define INT_DISABLE_UFP_ACCESSORY_SHIFT    0
#define INT_DISABLE_UFP_ACCESSORY   (0x01 << INT_DISABLE_UFP_ACCESSORY_SHIFT)      /*RW*/
#define INT_DRP_DUTY_CYCLE_SHIFT    1
#define INT_DRP_DUTY_CYCLE          (0x03 << INT_DRP_DUTY_CYCLE_SHIFT)      /*RW*/
#define INT_INTERRUPT_STATUS_SHIFT  4
#define INT_INTERRUPT_STATUS        (0x01 << INT_INTERRUPT_STATUS_SHIFT)    /*RCU*/
#define INT_CABLE_DIR_SHIFT         5
#define INT_CABLE_DIR               (0x01 << INT_CABLE_DIR_SHIFT)           /*RU*/
#define INT_ATTACHED_STATE_SHIFT    6
#define INT_ATTACHED_STATE          (0x03 << INT_ATTACHED_STATE_SHIFT)      /*RU*/

/* REG_SET (0x0A) */
#define SET_I2C_DISABLE_TERM        0x01    /*RW*/
#define SET_I2C_SOURCE_PREF_SHIFT   1
#define SET_I2C_SOURCE_PREF         (0x03 << SET_I2C_SOURCE_PREF_SHIFT)  /*RW*/
#define SET_I2C_SOFT_RESET_SHIFT    3
#define SET_I2C_SOFT_RESET          (0x01 << SET_I2C_SOFT_RESET_SHIFT)  /*RSU*/
#define SET_MODE_SELECT_SHIFT       4
#define SET_MODE_SELECT             (0x03 << SET_MODE_SELECT_SHIFT)     /*RW*/
#define SET_DEBOUNCE_SHIFT          6
#define SET_DEBOUNCE                (0x03 << SET_DEBOUNCE_SHIFT)        /*RW*/

/* REG_CTR (0x45) */
#define CTR_DISABLE_RD_RP_SHIFT     2
#define CTR_DISABLE_RD_RP           (0x01 << CTR_DISABLE_RD_RP_SHIFT)   /*RW*/

/******************************************************************************
 * Register values
 ******************************************************************************/
/* SET_MODE_SELECT */
#define SET_MODE_SELECT_DEFAULT  0x00
#define SET_MODE_SELECT_SNK       0x01
#define SET_MODE_SELECT_SRC       0x02
#define SET_MODE_SELECT_DRP       0x03
/* MOD_CURRENT_MODE_ADVERTISE */
#define MOD_CURRENT_MODE_ADVERTISE_DEFAULT      0x00
#define MOD_CURRENT_MODE_ADVERTISE_MID          0x01
#define MOD_CURRENT_MODE_ADVERTISE_HIGH         0x02
/* MOD_CURRENT_MODE_DETECT */
#define MOD_CURRENT_MODE_DETECT_DEFAULT      0x00
#define MOD_CURRENT_MODE_DETECT_MID          0x01
#define MOD_CURRENT_MODE_DETECT_ACCESSARY    0x02
#define MOD_CURRENT_MODE_DETECT_HIGH         0x03
/* CTR_DISABLE_RD_RP */
#define CTR_DISABLE_RD_RP_DEFAULT      0x00
#define CTR_DISABLE_RD_RP_DISABLE      0x01

/* DISABLE_UFP_ACCESSORY */
#define DISABLE_UFP_ACCESSORY_ENABLE	0x00
#define DISABLE_UFP_ACCESSORY_DISABLE	0x01

/******************************************************************************
 * Constants
 ******************************************************************************/

enum drp_toggle_type {
	TOGGLE_DFP_DRP_30 = 0,
	TOGGLE_DFP_DRP_40,
	TOGGLE_DFP_DRP_50,
	TOGGLE_DFP_DRP_60
};

enum current_adv_type {
	HOST_CUR_USB = 0,   /*default 500mA or 900mA*/
	HOST_CUR_1P5,      /*1.5A*/
	HOST_CUR_3A       /*3A*/
};

enum current_det_type {
	DET_CUR_USB = 0,    /*default 500mA or 900mA*/
	DET_CUR_1P5,
	DET_CUR_ACCESSORY,  /*charg through accessory 500mA*/
	DET_CUR_3A
};

enum accessory_attach_type {
	ACCESSORY_NOT_ATTACHED = 0,
	ACCESSORY_AUDIO = 4,
	ACCESSORY_CHG_THRU_AUDIO = 5,
	ACCESSORY_DEBUG = 6
};

enum cable_attach_type {
	CABLE_NOT_ATTACHED = 0,
	CABLE_ATTACHED
};

enum cable_state_type {
	CABLE_STATE_NOT_ATTACHED = 0,
	CABLE_STATE_AS_DFP,
	CABLE_STATE_AS_UFP,
	CABLE_STATE_TO_ACCESSORY
};

enum cable_dir_type {
	ORIENT_CC2 = 0,
	ORIENT_CC1
};

#if 0
enum cc_modes_type {
	MODE_DEFAULT = 0,
	MODE_UFP,
	MODE_DFP,
	MODE_DRP
};
#endif

#if 0
enum sink_src_mode {
	SINK_MODE,
	SRC_MODE,
	AUDIO_ACCESS_MODE,
	UNATTACHED_MODE,
};
#endif

/* Type-C Attrs */
struct type_c_parameters {
	enum current_det_type current_det;         /*charging current on UFP*/
	enum accessory_attach_type accessory_attach;     /*if an accessory is attached*/
	enum cable_attach_type active_cable_attach;         /*if an active_cable is attached*/
	enum cable_state_type attach_state;        /*DFP->UFP or UFP->DFP*/
	enum cable_dir_type cable_dir;           /*cc1 or cc2*/
};

struct state_disorder_monitor {
	int count;
	int err_detected;
	enum cable_state_type former_state;
	unsigned long time_before;
};

enum en_gpio_type {
	EN_CC_MUX = 0,
	EN_VREG_5V,
	EN_GPIO_MAX = 3
};

struct en_gpio_item {
	char *name;
	int index;
};

static struct en_gpio_item en_gpio_info[] = {
	{
		.name   = "sgm7220,en_cc_gpio",
		.index  = EN_CC_MUX,

	},
	{
		.name   = "sgm7220,vreg_5v_gpio",
		.index  = EN_VREG_5V,

	},
	/*add more gpio here if need*/
};

struct sgm7220_info {
	struct i2c_client  *i2c;
	struct device  *dev_t;
	struct device  *dev;
	struct mutex  mutex;
	struct class  *device_class;

	atomic_t suspended;

	int irq_gpio;
	int en_gpio[EN_GPIO_MAX];

	struct type_c_parameters type_c_param;
	struct state_disorder_monitor monitor;
	enum sink_src_mode sink_src_mode;

	struct pinctrl *pinctrl;
	struct pinctrl_state *typec_inter_active;
	struct pinctrl_state *typec_inter_sleep;

	bool probe;
	/*for qcom*/
	struct extcon_dev	*extcon;

	#ifdef OPLUS_ARCH_EXTENDS
	u32 headphone_det_support;
	#endif /* OPLUS_ARCH_EXTENDS */

	/*fix chgtype identify error*/
	struct wakeup_source *keep_resume_ws;
	wait_queue_head_t wait;
};

bool __attribute__((weak)) oplus_get_otg_switch_status(void)
{
	return false;
}

void __attribute__((weak)) oplus_enable_device_mode(bool enable)
{
}

struct sgm7220_info *gchip = NULL;

#define SGM7220_TYPEC_DIR_CC1	1
#define SGM7220_TYPEC_DIR_CC2	2
int oplus_sgm7220_set_mode(int mode);

#ifdef OPLUS_ARCH_EXTENDS
struct blocking_notifier_head cc_audio_notifier = BLOCKING_NOTIFIER_INIT(cc_audio_notifier);

int cc_audio_register_notify(struct notifier_block *nb)
{
        return blocking_notifier_chain_register(&cc_audio_notifier, nb);
}
EXPORT_SYMBOL(cc_audio_register_notify);

int cc_audio_unregister_notify(struct notifier_block *nb)
{
        return blocking_notifier_chain_unregister(&cc_audio_notifier, nb);;
}
EXPORT_SYMBOL(cc_audio_unregister_notify);
#endif /* OPLUS_ARCH_EXTENDS */

/* i2c operate interfaces */
static int sgm7220_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct sgm7220_info *info = i2c_get_clientdata(i2c);
	int ret;
	int retry = 20;

	mutex_lock(&info->mutex);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if(ret < 0) {
		while(retry > 0) {
			usleep_range(10000, 10000);
			ret = i2c_smbus_read_byte_data(i2c, reg);
			pr_err("%s: ret = %d \n", __func__, ret);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}
	mutex_unlock(&info->mutex);
	if (ret < 0) {
		pr_err("%s: (0x%x) error, ret(%d)\n", __func__, reg, ret);
		return ret;
	}

	ret &= 0xff;
	*dest = ret;
	return 0;
}

static void oplus_keep_resume_awake_init(struct sgm7220_info *chip)
{
	chip->keep_resume_ws = NULL;
	if (!chip) {
		pr_err("[%s]chip is null\n", __func__);
		return;
	}
	chip->keep_resume_ws = wakeup_source_register(NULL, "split_chg_keep_resume");
	return;
}

static void oplus_keep_resume_wakelock(struct sgm7220_info *chip, bool awake)
{
	static bool pm_flag = false;

	if (!chip || !chip->keep_resume_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->keep_resume_ws);
		pr_err("[%s] true\n", __func__);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->keep_resume_ws);
		pm_flag = false;
		pr_err("[%s] false\n", __func__);
	}
	return;
}

static int sgm7220_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct sgm7220_info *info = i2c_get_clientdata(i2c);
	int ret;
	int retry = 20;

	mutex_lock(&info->mutex);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	if (ret < 0) {
		while(retry > 0) {
			usleep_range(10000, 10000);
			ret = i2c_smbus_write_byte_data(i2c, reg, value);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}
	mutex_unlock(&info->mutex);
	if (ret < 0)
		pr_err("%s: (0x%x) error, ret(%d)\n", __func__, reg, ret);

	return ret;
}

static int __sgm7220_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	int ret;
	u8 old_val, new_val;

	ret = i2c_smbus_read_byte_data(i2c, reg);

	if (ret >= 0) {
		old_val = ret & 0xff;
		new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}

	return ret;
}

static int sgm7220_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	struct sgm7220_info *info = i2c_get_clientdata(i2c);
	int ret;
	int retry = 20;
	/*u8 old_val, new_val;*/

	mutex_lock(&info->mutex);
	ret = __sgm7220_update_reg(i2c, reg, val, mask);
	if (ret < 0) {
		while(retry > 0) {
			usleep_range(10000, 10000);
			ret = __sgm7220_update_reg(i2c, reg, val, mask);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}
	mutex_unlock(&info->mutex);
	return ret;
}

void sgm7220_set_typec_sinkonly(void)
{
	u8 value;

	if (!gchip) {
		pr_err("%s: gchip is invalid\n", __func__);
		return;
	}

	value = CTR_DISABLE_RD_RP_DEFAULT << CTR_DISABLE_RD_RP_SHIFT;
	if (sgm7220_update_reg(gchip->i2c, REG_CTL, value, CTR_DISABLE_RD_RP)) {
		pr_err("%s: REG_CTL CTR_DISABLE_RD_RP_DEFAULT failed\n", __func__);
		msleep(300);
		if (sgm7220_update_reg(gchip->i2c, REG_CTL, value, CTR_DISABLE_RD_RP)) {
			pr_err("%s: REG_CTL CTR_DISABLE_RD_RP_DEFAULT2 failed\n", __func__);
			return;
		}
	}

	value = SET_MODE_SELECT_SNK << SET_MODE_SELECT_SHIFT;
	if (sgm7220_update_reg(gchip->i2c, REG_SET, value, SET_MODE_SELECT)) {
		pr_err("%s: REG_SET SET_MODE_SELECT_SNK failed\n", __func__);
		return;
	}
}
EXPORT_SYMBOL(sgm7220_set_typec_sinkonly);

void sgm7220_set_typec_cc_open(void)
{
	u8 value;

	if (!gchip) {
		pr_err("%s: gchip is invalid\n", __func__);
		return;
	}

	value = CTR_DISABLE_RD_RP_DISABLE << CTR_DISABLE_RD_RP_SHIFT;
	if (sgm7220_update_reg(gchip->i2c, REG_CTL, value, CTR_DISABLE_RD_RP)) {
		pr_err("%s: REG_CTL CTR_DISABLE_RD_RP_DISABLE failed\n", __func__);
		return;
	}
}
EXPORT_SYMBOL(sgm7220_set_typec_cc_open);

/* Config DFP/UFP/DRP mode */
/* e.g #echo 1 >/sys/class/type-c/sgm7220/mode_select */
static ssize_t mode_select_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);
	u8 value;
	int ret;

	ret = sgm7220_read_reg(info->i2c, REG_SET, &value);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}
	value = (value & SET_MODE_SELECT) >> SET_MODE_SELECT_SHIFT;
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t mode_select_store(struct device *dev,
				 struct device_attribute *attr, const char *buf, size_t size)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);
	int mode;
	u8 value;
	int ret;

	ret = kstrtoint(buf, 0, &mode);
	if (ret != 0)
		return -EINVAL;

	if (mode == MODE_DFP)
		value = SET_MODE_SELECT_SRC;
	else if (mode == MODE_UFP)
		value = SET_MODE_SELECT_SNK;
	else if (mode == MODE_DRP)
		value = SET_MODE_SELECT_DRP;
	else
		return -EINVAL;

	value = value << SET_MODE_SELECT_SHIFT;
	ret = sgm7220_update_reg(info->i2c, REG_SET, value, SET_MODE_SELECT);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return ret;
}

/* Advertise current when act as DFP */
static ssize_t current_advertise_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);
	u8 value;
	int ret;

	ret = sgm7220_read_reg(info->i2c, REG_MOD, &value);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}
	value = (value & MOD_CURRENT_MODE_ADVERTISE) >> MOD_CURRENT_MODE_ADVERTISE_SHIFT;
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t current_advertise_store(struct device *dev,
				       struct device_attribute *attr, const char *buf, size_t size)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);
	int mode;
	u8 value;
	int ret;

	ret = kstrtoint(buf, 0, &mode);
	if (ret != 0)
		return -EINVAL;

	if (mode == HOST_CUR_USB)
		value = MOD_CURRENT_MODE_ADVERTISE_DEFAULT;
	else if (mode == HOST_CUR_1P5)
		value = MOD_CURRENT_MODE_ADVERTISE_MID;
	else if (mode == HOST_CUR_3A)
		value = MOD_CURRENT_MODE_ADVERTISE_HIGH;
	else
		return -EINVAL;

	value = value << MOD_CURRENT_MODE_ADVERTISE_SHIFT;

	ret = sgm7220_update_reg(info->i2c, REG_MOD, value, MOD_CURRENT_MODE_ADVERTISE);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return ret;
}

/* Detct current when act as UFP */
static ssize_t current_detect_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);
	u8 value;
	int ret;

	ret = sgm7220_read_reg(info->i2c, REG_MOD, &value);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}
	value = (value & MOD_CURRENT_MODE_DETECT) >> MOD_CURRENT_MODE_DETECT_SHIFT;

	if (value == MOD_CURRENT_MODE_DETECT_DEFAULT)
		return snprintf(buf, PAGE_SIZE, "500mA or 900mA\n");
	else if (value == MOD_CURRENT_MODE_DETECT_MID)
		return snprintf(buf, PAGE_SIZE, "mid 1P5A\n");
	else if (value == MOD_CURRENT_MODE_DETECT_HIGH)
		return snprintf(buf, PAGE_SIZE, "high 3A\n");
	else if (value == MOD_CURRENT_MODE_DETECT_ACCESSARY)
		return snprintf(buf, PAGE_SIZE, "accessary 500mA\n");
	else
		return snprintf(buf, PAGE_SIZE, "unknown\n");
}

static ssize_t gpio_debug_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "EN_CC_MUX:%d\n",
			gpio_get_value(info->en_gpio[EN_CC_MUX]));
}

static ssize_t gpio_debug_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);
	int level;
	int ret;

	ret = kstrtoint(buf, 0, &level);
	if (ret != 0)
		return -EINVAL;

	if (info->en_gpio[EN_CC_MUX])
		gpio_direction_output(info->en_gpio[EN_CC_MUX], !!level);

	return 1;
}

/* For Emode, read typec chip info */
static ssize_t show_chip_info(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);
	u8 reg_val;
	char name[ID_REG_LEN + 1] = { 0 };
	u8 revision = 0;
	int i, j, ret;

	for (i = 0, j = 0; i < ID_REG_LEN; i++) {
		reg_val = 0;
		ret = sgm7220_read_reg(info->i2c, (REG_BASE + (ID_REG_LEN - 1) - i), &reg_val);
		if (ret < 0) {
			pr_err("%s: read reg fail!\n", __func__);
			snprintf(name, ID_REG_LEN, "%s", "Error");
			break;
		}
		if (!reg_val) {
			j++;
			continue;
		}
		name[i - j] = (char)reg_val;
	}

	ret = sgm7220_read_reg(info->i2c, REG_REVISION, &revision);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return snprintf(buf, PAGE_SIZE, "USB TypeC chip info:\n"
				"	Manufacture: TI\n"
				"	Chip Name  : %s\n"
				"	Revision ID : Error\n",
				name);
	} else {
		return snprintf(buf, PAGE_SIZE, "USB TypeC chip info:\n"
				"	Manufacture: TI\n"
				"	Chip Name  : %s\n"
				"	Revision ID : %d\n",
				name, revision);
	}
}

/**************************************************************************/
#define TYPE_C_ATTR(field, format_string) \
		static ssize_t \
		field ## _show(struct device *dev, struct device_attribute *attr, \
			char *buf) \
		{ \
			struct sgm7220_info *info = dev_get_drvdata(dev); \
			return snprintf(buf, PAGE_SIZE, \
			format_string, info->type_c_param.field); \
		} \
		static DEVICE_ATTR(field, S_IRUGO, field ## _show, NULL);

static DEVICE_ATTR(gpio_debug,  S_IRUGO | S_IWUSR,
			gpio_debug_show, gpio_debug_store);
static DEVICE_ATTR(mode_select, S_IRUGO | S_IWUSR,
			mode_select_show, mode_select_store);
static DEVICE_ATTR(current_advertise, S_IRUGO | S_IWUSR,
			current_advertise_show, current_advertise_store);
static DEVICE_ATTR(current_det_string, S_IRUGO, current_detect_show, NULL);

/* For Emode, read typec chip info */
static DEVICE_ATTR(chip_info, S_IRUGO, show_chip_info, NULL);

TYPE_C_ATTR(current_det, "%d\n")
TYPE_C_ATTR(accessory_attach, "%d\n")
TYPE_C_ATTR(active_cable_attach, "%d\n")
TYPE_C_ATTR(attach_state, "%d\n")
TYPE_C_ATTR(cable_dir, "%d\n")

static struct device_attribute *usb_typec_attributes[] = {
	&dev_attr_mode_select,
	&dev_attr_current_advertise,
	&dev_attr_current_det_string,
	&dev_attr_chip_info,
	&dev_attr_current_det,
	&dev_attr_accessory_attach,
	&dev_attr_active_cable_attach,
	&dev_attr_attach_state,
	&dev_attr_cable_dir,
	&dev_attr_gpio_debug,
	/*end*/
	NULL
};
/******************************************************************************/

/* Detect non-DFP->DFP changes that happen more than 3 times within 10 Secs */
static void state_disorder_detect(struct sgm7220_info *info)
{
	unsigned long timeout;

	/* count the (non-DFP -> DFP) changes */
	if ((info->monitor.former_state != info->type_c_param.attach_state)
	    && (info->type_c_param.attach_state == CABLE_STATE_AS_DFP)) {
		if (!info->monitor.count) {
			info->monitor.time_before = jiffies;
		}
		info->monitor.count++;
	}

	/* store the state */
	info->monitor.former_state = info->type_c_param.attach_state;

	if (info->monitor.count > 3) {
		timeout = msecs_to_jiffies(10 * 1000); /* 10 Seconds */
		if (time_before(jiffies, info->monitor.time_before + timeout)) {
			info->monitor.err_detected = 1;
			/* disbale id irq before qpnp react to cc chip's id output */
			//interfere_id_irq_from_usb(0);
		}
		info->monitor.count = 0;
	}

	if ((info->type_c_param.attach_state == CABLE_STATE_NOT_ATTACHED)
	    && info->monitor.err_detected) {
		/* enable id irq */
		//interfere_id_irq_from_usb(1);
		info->monitor.err_detected = 0;
	}
}

#ifndef CONFIG_OPLUS_CHARGER_MTK
static int oplus_register_extcon(struct sgm7220_info *info)
{
	int rc = 0;

	info->extcon = devm_extcon_dev_allocate(info->dev, smblib_extcon_cable);
	if (IS_ERR(info->extcon)) {
		rc = PTR_ERR(info->extcon);
		dev_err(info->dev, "failed to allocate extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = devm_extcon_dev_register(info->dev, info->extcon);
	if (rc < 0) {
		dev_err(info->dev, "failed to register extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	/* Support reporting polarity and speed via properties */
	rc = extcon_set_property_capability(info->extcon,
			EXTCON_USB, EXTCON_PROP_USB_TYPEC_POLARITY);
	rc |= extcon_set_property_capability(info->extcon,
			EXTCON_USB, EXTCON_PROP_USB_SS);
	rc |= extcon_set_property_capability(info->extcon,
			EXTCON_USB_HOST, EXTCON_PROP_USB_TYPEC_POLARITY);
	rc |= extcon_set_property_capability(info->extcon,
			EXTCON_USB_HOST, EXTCON_PROP_USB_SS);

	dev_err(info->dev, "oplus_register_extcon rc=%d\n",
			rc);
cleanup:
	return rc;
}
#endif

static int opluschg_get_typec_cc_orientation(union power_supply_propval *val)
{
	chg_err("typec_dir = %s\n", typec_dir == 1 ? "cc1 attach" : "cc2_attach");
	val->intval = typec_dir;
	return typec_dir;
}

static void oplus_notify_extcon_props(struct sgm7220_info *info, int id)
{
	union extcon_property_value val;
	union power_supply_propval prop_val;

	opluschg_get_typec_cc_orientation(&prop_val);
	val.intval = ((prop_val.intval == 2) ? 1 : 0);
	extcon_set_property(info->extcon, id,
			EXTCON_PROP_USB_TYPEC_POLARITY, val);
	val.intval = true;
	extcon_set_property(info->extcon, id,
			EXTCON_PROP_USB_SS, val);
}

void oplus_notify_device_mode(bool enable)
{
	struct sgm7220_info *info = gchip;

	if (!info) {
		oplus_enable_device_mode(enable);
		return;
	}

	if (enable)
		oplus_notify_extcon_props(info, EXTCON_USB);

	extcon_set_state_sync(info->extcon, EXTCON_USB, enable);
	pr_err("[%s] enable[%d]\n", __func__, enable);
}

static void oplus_notify_usb_host(bool enable)
{
	struct sgm7220_info *info = gchip;

	if (!info || !g_oplus_chip) {
		pr_err("[%s]  sgm7220_info or oplus_chip is null\n", __func__);
		return;
	}
	if (enable) {
		pr_debug("enabling VBUS in OTG mode\n");
		//sy6974b_otg_enable();
		g_oplus_chip->chg_ops->otg_enable();
		oplus_notify_extcon_props(info, EXTCON_USB_HOST);
	} else {
		pr_debug("disabling VBUS in OTG mode\n");
		//sy6974b_otg_disable();
		g_oplus_chip->chg_ops->otg_disable();
	}

	extcon_set_state_sync(info->extcon, EXTCON_USB_HOST, enable);
	pr_debug("[%s] enable[%d]\n", __func__, enable);
}

static void oplus_typec_sink_insertion(void)
{
	struct smb_charger *chg = NULL;

	if (!g_oplus_chip) {
		pr_err("[%s] oplus_chip is null\n", __func__);
		return;
	}
	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;
	chg->otg_present = true;
	g_oplus_chip->otg_online = true;
	oplus_chg_wake_update_work();

	oplus_notify_usb_host(true);
	pr_debug("wakeup[%s] done!!!\n", __func__);
}

static void oplus_typec_src_insertion(void)
{
	pr_debug("[%s] done!!!\n", __func__);
}

static void oplus_typec_ra_ra_insertion(void)
{
	pr_debug("[%s] done!!!\n", __func__);
}

static void oplus_typec_sink_removal(void)
{
	struct smb_charger *chg = NULL;
	if (!g_oplus_chip) {
		pr_err("[%s] oplus_chip is null\n", __func__);
		return;
	}
	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;

	if (chg->otg_present)
		oplus_notify_usb_host(false);
	chg->otg_present = false;
	g_oplus_chip->otg_online = false;
	oplus_chg_wake_update_work();
	if(!oplus_get_otg_switch_status()) {
		oplus_sgm7220_set_mode(MODE_UFP);
	}
	pr_debug("wakeup [%s] done!!!\n", __func__);
}

static void oplus_typec_src_removal(void)
{
	struct smb_charger *chg = NULL;
	if (!g_oplus_chip) {
		pr_err("[%s] oplus_chip is null\n", __func__);
		return;
	}
	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;
	oplus_notify_device_mode(false);
	pr_debug("[%s] done!!!\n", __func__);
}

static void oplus_typec_mode_unattached(void)
{
	pr_debug("[%s] done!!!\n", __func__);
}

static void process_mode_register(struct sgm7220_info *info)
{
	int ret;
	u8 val;
	u8 status;
	u8 tmp;

	ret = sgm7220_read_reg(info->i2c, REG_MOD, &status);
	if (ret < 0) {
		pr_err("%s err\n", __func__);
		return;
	}
	tmp = status;
	pr_debug("%s mode_register:0x%x\n", __func__, status);

	/* check current_detect */
	val = ((tmp & MOD_CURRENT_MODE_DETECT) >> MOD_CURRENT_MODE_DETECT_SHIFT);
	info->type_c_param.current_det = val;

	/* check accessory attch */
	tmp = status;
	val = ((tmp & MOD_ACCESSORY_CONNECTED) >> MOD_ACCESSORY_CONNECTED_SHIFT);
	info->type_c_param.accessory_attach = val;

	/* check cable attach */
	tmp = status;
	val = (tmp & MOD_ACTIVE_CABLE_DETECTION);
	info->type_c_param.active_cable_attach = val;
}


static void process_interrupt_register(struct sgm7220_info *info)
{
	int ret;
	u8 val;
	u8 status;
	u8 tmp;
	static bool cur_attach = false;
	bool pre_attach = cur_attach;
	bool attach_changed = false;
	enum cable_state_type pre_state_type = info->type_c_param.attach_state;

	ret = sgm7220_read_reg(info->i2c, REG_INT, &status);
	if (ret < 0) {
		pr_err("%s err\n", __func__);
		return;
	}
	tmp = status;
	pr_debug("%s interrupt_register:0x%x\n", __func__, status);

	/* check attach state */
	val = ((tmp & INT_ATTACHED_STATE) >> INT_ATTACHED_STATE_SHIFT);
	info->type_c_param.attach_state = val;
	if (pre_state_type == CABLE_STATE_NOT_ATTACHED && info->type_c_param.attach_state != CABLE_STATE_NOT_ATTACHED) {
		cur_attach = true;
	} else if (pre_state_type != CABLE_STATE_NOT_ATTACHED && info->type_c_param.attach_state == CABLE_STATE_NOT_ATTACHED) {
		cur_attach = false;
	}
	if (cur_attach != pre_attach) {
		attach_changed = true;
	}

	/* update current adv when act as DFP */
	if (info->type_c_param.attach_state == CABLE_STATE_AS_DFP ||
	    info->type_c_param.attach_state == CABLE_STATE_TO_ACCESSORY) {
		val = (HOST_CUR_USB << MOD_CURRENT_MODE_ADVERTISE_SHIFT);
	} else {
		val = (HOST_CUR_3A << MOD_CURRENT_MODE_ADVERTISE_SHIFT);
	}
	sgm7220_update_reg(info->i2c, REG_MOD, val, MOD_CURRENT_MODE_ADVERTISE);

	/* in case configured as DRP may detect some non-standard SDP */
	/* chargers as UFP, which may lead to a cyclic switching of DFP */
	/* and UFP on state detection result. */
	state_disorder_detect(info);

	/* check cable dir */
	tmp = status;
	val = ((tmp & INT_CABLE_DIR) >> INT_CABLE_DIR_SHIFT);
	info->type_c_param.cable_dir = val;

	typec_dir = val ? SGM7220_TYPEC_DIR_CC2 : SGM7220_TYPEC_DIR_CC1;
	pr_debug("%s attach[%d] cabledir[%d] probe[%d]\n", __func__,
		info->type_c_param.attach_state, typec_dir, info->probe);
	if (attach_changed) {
		if (cur_attach) {
			if (info->type_c_param.attach_state == CABLE_STATE_AS_DFP) {		//host
				info->sink_src_mode = SRC_MODE;
				oplus_typec_sink_insertion();
			} else if (info->type_c_param.attach_state == CABLE_STATE_AS_UFP) {	//device
				info->sink_src_mode = SINK_MODE;
				oplus_typec_src_insertion();
			} else if (info->type_c_param.attach_state == CABLE_STATE_TO_ACCESSORY){
				info->sink_src_mode = AUDIO_ACCESS_MODE;
				oplus_typec_ra_ra_insertion();
				#ifdef OPLUS_ARCH_EXTENDS
				if (info->headphone_det_support) {
					blocking_notifier_call_chain(&cc_audio_notifier, 1, NULL);
					pr_err("%s this is audio access enter!\n", __func__);
				}
				#endif /* OPLUS_ARCH_EXTENDS */
			}
		} else {
			switch (info->sink_src_mode) {
				case SRC_MODE:
					oplus_typec_sink_removal();
					break;
				case SINK_MODE:
				case AUDIO_ACCESS_MODE:
					#ifdef OPLUS_ARCH_EXTENDS
					if (info->headphone_det_support)
						blocking_notifier_call_chain(&cc_audio_notifier, 0, NULL);
					#endif /* OPLUS_ARCH_EXTENDS */
					oplus_typec_src_removal();
					break;
				case UNATTACHED_MODE:
				default:
					oplus_typec_mode_unattached();
					break;
			}
			info->sink_src_mode = AUDIO_ACCESS_MODE;
		}
	}
	if (info->probe) {
		if (info->type_c_param.attach_state != CABLE_STATE_AS_DFP) {
			oplus_sgm7220_set_mode(MODE_UFP);
			pr_debug("%s not found host devices and set to ufp mode]\n", __func__);
		} else {
			pr_debug("%s found host devices and keep drp mode]\n", __func__);
		}
		info->probe = false;
	}
}

int sgm7220_get_typec_attach_state(void)
{
	if (!gchip) {
		pr_err("%s: sgm7220 info is invalid\n", __func__);
		return -EINVAL;
	}

	return gchip->sink_src_mode;
}

#define OPLUS_WAIT_RESUME_TIM	200

static irqreturn_t sgm7220_irq_thread(int irq, void *handle)
{
	struct sgm7220_info *info = (struct sgm7220_info *)handle;
	int ret;

	pr_debug("%s enter irq[%d]\n", __func__, irq);

	oplus_keep_resume_wakelock(info, true);
	/*for check bus i2c/spi is ready or not*/
	if (atomic_read(&info->suspended)==1) {
		pr_notice(" sgm7220_irq_thread:suspended and wait_event_interruptible %d\n", OPLUS_WAIT_RESUME_TIM);
		wait_event_interruptible_timeout(info->wait, atomic_read(&info->suspended) == 0, msecs_to_jiffies(OPLUS_WAIT_RESUME_TIM));
	}

	info->probe = false;
	if (irq == 0) {
		info->probe = true;
		oplus_sgm7220_set_mode(MODE_DRP);
	}

	process_mode_register(info);
	process_interrupt_register(info);
	ret = sgm7220_update_reg(info->i2c,
				   REG_INT, (0x1 << INT_INTERRUPT_STATUS_SHIFT), INT_INTERRUPT_STATUS);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	oplus_keep_resume_wakelock(info, false);
	pr_debug("%s done\n", __func__);

	return IRQ_HANDLED;
}

#ifdef TYPEC_PM_OP
static int sgm7220_suspend(struct device *dev)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);

	pr_info("%s enter\n", __func__);

	if (gchip) {
		atomic_set(&gchip->suspended, 1);
	}

	disable_irq_wake(info->i2c->irq);
	disable_irq(info->i2c->irq);

	if (info->en_gpio[EN_CC_MUX])
		gpio_direction_output(info->en_gpio[EN_CC_MUX], 0);

	return 0;
}

static int sgm7220_resume(struct device *dev)
{
	struct sgm7220_info *info = dev_get_drvdata(dev);

	pr_info("%s enter\n", __func__);

	if (info->en_gpio[EN_CC_MUX])
		gpio_direction_output(info->en_gpio[EN_CC_MUX], 1);

	msleep(20);

	enable_irq_wake(info->i2c->irq);
	enable_irq(info->i2c->irq);

	if (gchip) {
		atomic_set(&gchip->suspended, 0);
		wake_up_interruptible(&gchip->wait);
	}

	return 0;
}
#endif



static int sgm7220_initialization(struct sgm7220_info *info)
{
	int ret = 0;
	u8 reg_val;
	/* do initialization here, before enable irq,
	 * clear irq,
	 * config DRP/UFP/DFP mode,
	 * and etc..
	 */
	pr_debug("%s enter\n", __func__);
	ret = sgm7220_read_reg(info->i2c, REG_REVISION, &reg_val);
	if (ret < 0) {
		pr_err("%s read [0x%0x] fail\n", __func__, REG_REVISION);
		return ret;
	}

	/* 1 Disable term(default 0) [bit 0]
	 * 2 Source pref(DRP performs try.SNK 01) [bit 2:1]
	 * 3 Soft reset(default 0) [bit 3]
	 * 4 Mode slect(DRP start from Try.SNK 00) [bit 5:4]
	 * 5 Debounce time of voltage change on CC pin(default 0) [bit 7:6]
	 */
	reg_val = 0x02;
	ret = sgm7220_write_reg(info->i2c, REG_SET, reg_val);
	if (ret < 0) {
		pr_err("%s: init REG_SET fail!\n", __func__);
		return ret;
	}

	//reg_val = SET_MODE_SELECT_DRP << SET_MODE_SELECT_SHIFT;
	//ret = sgm7220_update_reg(info->i2c, REG_SET, reg_val, SET_MODE_SELECT);


	/* CURRENT MODE ADVERTISE 3A [bit 7:6] */
	reg_val = (HOST_CUR_3A << MOD_CURRENT_MODE_ADVERTISE_SHIFT);
	ret = sgm7220_update_reg(info->i2c, REG_MOD, reg_val, MOD_CURRENT_MODE_ADVERTISE);
	if (ret < 0) {
		pr_err("%s: init REG_MOD fail!\n", __func__);
		return ret;
	}

	return ret;
}

static int sgm7220_create_device(struct sgm7220_info *info)
{
	struct device_attribute **attrs = usb_typec_attributes;
	struct device_attribute *attr;
	int err;

	pr_debug("%s:\n", __func__);
	info->device_class = class_create(THIS_MODULE, "type-c");
	if (IS_ERR(info->device_class))
		return PTR_ERR(info->device_class);

	info->dev_t = device_create(info->device_class, NULL, 0, NULL, "chip");
	if (IS_ERR(info->dev_t))
		return PTR_ERR(info->dev_t);

	dev_set_drvdata(info->dev_t, info);

	while ((attr = *attrs++)) {
		err = device_create_file(info->dev_t, attr);
		if (err) {
			device_destroy(info->device_class, 0);
			return err;
		}
	}
	return 0;
}

static void sgm7220_destroy_device(struct sgm7220_info *info)
{
	struct device_attribute **attrs = usb_typec_attributes;
	struct device_attribute *attr;

	while ((attr = *attrs++))
		device_remove_file(info->dev_t, attr);

	device_destroy(info->device_class, 0);
	class_destroy(info->device_class);
	info->device_class = NULL;
}

static int sgm7220_parse_gpio_from_dts(struct device_node *np, struct sgm7220_info *info)
{
	int ret;
	int i;

	/* irq_line */
	ret = of_get_named_gpio(np, "sgm7220,irq_gpio", 0);
	if (ret < 0) {
		pr_err("%s: error invalid irq gpio err: %d\n", __func__, ret);
		return ret;
	}
	info->irq_gpio = ret;
	pr_debug("%s: valid irq_gpio number: %d\n", __func__, ret);

	/*
	 * en gpios, for changing state during suspend to reduce power consumption,
	 * as these gpios may already have initial config in the dts files,
	 * getting gpio failure here may not be a critical error.
	 */
	for (i = 0; i < EN_GPIO_MAX; i++) {
		ret = of_get_named_gpio(np, en_gpio_info[i].name, 0);
		if (ret < 0) {
			info->en_gpio[en_gpio_info[i].index] = 0;
			pr_err("%s: error invalid en gpio [%s] err: %d\n",
			       __func__, en_gpio_info[i].name, ret);
		} else {
			info->en_gpio[en_gpio_info[i].index] = ret;
			pr_debug("%s: valid en gpio number: %d\n", __func__, ret);
		}
	}

	return 0;
}

int oplus_sgm7220_set_mode(int mode)
{
	u8 value;

	if (!gchip) {
		pr_err("%s: mode[%d] info is invalid\n", __func__, mode);
		return -EINVAL;
	}

	if (mode == MODE_DFP)
		value = SET_MODE_SELECT_SRC;
	else if (mode == MODE_UFP)
		value = SET_MODE_SELECT_SNK;
	else if (mode == MODE_DRP)
		value = SET_MODE_SELECT_DRP;
	else {
		pr_err("%s: mode[%d] is invalid\n", __func__, mode);
		return -EINVAL;
	}
	value = value << SET_MODE_SELECT_SHIFT;
	if (sgm7220_update_reg(gchip->i2c, REG_SET, value, SET_MODE_SELECT)) {
		pr_err("%s: mode[%d] failed\n", __func__, mode);
		return -EINVAL;
	}
	pr_err("%s: mode[%d] readback REG_SET:0x%0x[0x%0x] done\n",
		__func__, mode, REG_SET, i2c_smbus_read_byte_data(gchip->i2c, REG_SET));

	if (mode == MODE_DRP) {
		sgm7220_update_reg(gchip->i2c, REG_INT, 0, 1);
	} else {
		sgm7220_update_reg(gchip->i2c, REG_INT, 1, 1);
	}

	pr_err("%s: mode[%d] readback REG_INT:0x%0x[0x%0x] done\n",
		__func__, mode, REG_INT, i2c_smbus_read_byte_data(gchip->i2c, REG_INT));
	return 0;
}

int oplus_chg_get_typec_attach_state(void)
{
	int ret = 0;

	ret = sgm7220_get_typec_attach_state();
	return ret;
}

int oplus_chg_cclogic_set_mode(int mode)
{
	int ret = 0;

	ret = oplus_sgm7220_set_mode(mode);
	return ret;
}

int oplus_chg_inquire_cc_polarity(void)
{
	return typec_dir;
}

//extern int oplus_sy697x_get_charger_type(void);
static int sgm7220_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct sgm7220_info *info;
	struct device_node *np = client->dev.of_node;
	int ret, irq;
	#ifdef OPLUS_ARCH_EXTENDS
	u32 headphone_det_support = 0;
	#endif /* OPLUS_ARCH_EXTENDS */

	pr_err("%s: lkl enter\n", __func__);
	info = kzalloc(sizeof(struct sgm7220_info), GFP_KERNEL);
	info->i2c = client;
	info->dev = &client->dev;

	#ifdef OPLUS_ARCH_EXTENDS
	ret = of_property_read_u32(client->dev.of_node,
			"oplus,headphone-det-support", &headphone_det_support);
	if (ret || headphone_det_support == 0) {
		info->headphone_det_support = 0;
	} else {
		info->headphone_det_support = 1;
	}
	dev_err(info->dev,
		"%s: headphone_det_support = %d\n",
			__func__, info->headphone_det_support);
	#endif /* OPLUS_ARCH_EXTENDS */

	atomic_set(&info->suspended, 0);
	/* get gpio(s) */
	ret = sgm7220_parse_gpio_from_dts(np, info);
	if (ret < 0) {
		pr_err("%s: error invalid irq gpio number: %d\n", __func__, ret);
		goto err_pinctrl;
	}

	i2c_set_clientdata(client, info);
	mutex_init(&info->mutex);

	init_waitqueue_head(&info->wait);
	oplus_keep_resume_awake_init(info);

	/* try initialize device before request irq */
	ret = sgm7220_initialization(info);
	if (ret < 0) {
		pr_err("%s: fails to do initialization %d\n", __func__, ret);
		goto err_init_dev;
	}

	/* create device and sysfs nodes */
	ret = sgm7220_create_device(info);
	if (ret) {
		pr_err("%s: create device failed\n", __func__);
		goto err_device_create;
	}

	irq = gpio_to_irq(info->irq_gpio);
	if (irq < 0) {
		pr_err("%s: error gpio_to_irq returned %d\n", __func__, irq);
		goto err_request_irq;
	} else {
		pr_debug("%s: requesting IRQ %d\n", __func__, irq);
		client->irq = irq;
	}

	/* set tyoec pinctrl*/
	info->pinctrl = devm_pinctrl_get(info->dev);
	if (IS_ERR_OR_NULL(info->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -EINVAL;
	}
	info->typec_inter_active =
		pinctrl_lookup_state(info->pinctrl, "typec_inter_active");
	if (IS_ERR_OR_NULL(info->typec_inter_active)) {
		pr_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	info->typec_inter_sleep =
		pinctrl_lookup_state(info->pinctrl, "typec_inter_sleep");
	if (IS_ERR_OR_NULL(info->typec_inter_sleep)) {
		pr_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

#ifndef CONFIG_OPLUS_CHARGER_MTK
	oplus_register_extcon(info);
#endif

	//irq active
	gpio_direction_input(info->irq_gpio);
	pinctrl_select_state(info->pinctrl, info->typec_inter_active); /* no_PULL */
	ret = gpio_get_value(info->irq_gpio);
	pr_err("[%s]irq_gpio =%d irq_gpio_stat = %d\n", __func__, info->irq_gpio, ret);

	ret = request_threaded_irq(client->irq, NULL, sgm7220_irq_thread,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "sgm7220_irq", info);
	if (ret) {
		dev_err(&client->dev, "error failed to request IRQ\n");
		goto err_request_irq;
	}

	ret = enable_irq_wake(client->irq);
	if (ret < 0) {
		dev_err(&client->dev, "failed to enable wakeup src %d\n", ret);
		goto err_enable_irq;
	}
	gchip = info;
	sgm7220_set_typec_sinkonly();
	sgm7220_irq_thread(0, info);
	pr_err("[%s] probe success!!\n", __func__);
	return 0;

err_enable_irq:
	free_irq(client->irq, NULL);
err_request_irq:
	sgm7220_destroy_device(info);
err_device_create:
err_init_dev:
	wakeup_source_unregister(info->keep_resume_ws);
	mutex_destroy(&info->mutex);
	i2c_set_clientdata(client, NULL);
err_pinctrl:
	kfree(info);
	info = NULL;
	return ret;
}

static int sgm7220_remove(struct i2c_client *client)
{
	struct sgm7220_info *info = i2c_get_clientdata(client);

	if (client->irq) {
		disable_irq_wake(client->irq);
		free_irq(client->irq, info);
	}

	sgm7220_destroy_device(info);
	mutex_destroy(&info->mutex);
	i2c_set_clientdata(client, NULL);

	kfree(info);
	return 0;
}

static const struct of_device_id sgm7220_dt_match[] = {
	{
		.compatible = "oplus,sgm7220",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sgm7220_dt_match);

static const struct i2c_device_id sgm7220_id_table[] = {
	{
		.name = "sgm7220",
	},
};

static void sgm7220_shutdown(struct i2c_client *client)
{
	if (!gchip)
		return;

	//set ufp mode and disable ufp accessory
	sgm7220_update_reg(gchip->i2c, REG_SET, (1 << 4), (3 << 4));	//0xA
	pr_err("%s: readback:0x%0x[0x%0x] done\n",	__func__, REG_SET, i2c_smbus_read_byte_data(gchip->i2c, REG_SET));
	sgm7220_update_reg(gchip->i2c, REG_INT, 1, 1);	//0x9
	pr_err("%s: readback:0x%0x[0x%0x] done\n",	__func__, REG_INT, i2c_smbus_read_byte_data(gchip->i2c, REG_INT));

	return;
}

static int oplus_sgm7220_suspend_noirq(struct device *device)
{
	/*pr_err("+++ prepare %s: enter +++\n", __func__);*/
	if (gchip) {
		atomic_set(&gchip->suspended, 1);
	} else {
		pr_err("%s: gchip is null\n", __func__);
	}
	/*pr_err("--- prepare %s: exit ---\n", __func__);*/
	return 0;
}

static void oplus_sgm7220_resume_noirq(struct device *device)
{
	/*pr_err("+++ complete %s: enter +++\n", __func__);*/
	if (gchip) {
		atomic_set(&gchip->suspended, 0);
	} else {
		pr_err("%s: gchip is null\n", __func__);
	}
	/*pr_err("--- complete %s: exit ---\n", __func__);*/
	return;
}

static const struct dev_pm_ops oplus_sgm7220_pm_ops = {
	.prepare		= oplus_sgm7220_suspend_noirq,
	.complete		= oplus_sgm7220_resume_noirq,
};

/*#ifdef TYPEC_PM_OP*/
/*static SIMPLE_DEV_PM_OPS(sgm7220_dev_pm, sgm7220_suspend, sgm7220_resume);*/
/*#endif*/

static struct i2c_driver sgm7220_i2c_driver = {
	.driver = {
		.name = "sgm7220",
		.of_match_table = of_match_ptr(sgm7220_dt_match),
/*#ifdef TYPEC_PM_OP*/
		.pm = &oplus_sgm7220_pm_ops,
/*#endif*/
	},
	.probe    = sgm7220_probe,
	.remove   = sgm7220_remove,
	.shutdown = sgm7220_shutdown,
	.id_table = sgm7220_id_table,
};

#if 0
static __init int sgm7220_i2c_init(void)
{
	return i2c_add_driver(&sgm7220_i2c_driver);
}

static __exit void sgm7220_i2c_exit(void)
{
	i2c_del_driver(&sgm7220_i2c_driver);
}

module_init(sgm7220_i2c_init);
module_exit(sgm7220_i2c_exit);
#endif

void sgm7220_i2c_exit(void)
{
	i2c_del_driver(&sgm7220_i2c_driver);
}

int sgm7220_i2c_init(void)
{
	int ret = 0;
	i2c_add_driver(&sgm7220_i2c_driver);
	return ret;
}


MODULE_DESCRIPTION("I2C bus driver for sgm7220 USB Type-C");
MODULE_LICENSE("GPL v2");
