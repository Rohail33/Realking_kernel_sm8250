// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/version.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#include <linux/xlog.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/mtk_gpio.h>
#endif
#include <mt-plat/mtk_rtc.h>
#endif
#include <linux/module.h>
#include <uapi/linux/rtc.h>
#include <linux/rtc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <mt-plat/charging.h>
#endif
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <soc/oplus/device_info.h>

extern void mt_power_off(void);
#else
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#include <soc/oplus/device_info.h>

#include <soc/oplus/system/boot_mode.h>

#endif

#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "oplus_mp2650.h"
#include "../op_wlchg_v2/oplus_chg_wls.h"
#include "../oplus_charger.h"
#include "../oplus_chg_ops_manager.h"
#ifdef OPLUS_CUSTOME_OP_DEF
#ifdef CONFIG_OP_WIRELESSCHG
#include "../oplus_wlchg_policy.h"
#endif // CONFIG_OP_WIRELESSCHG
#else
#include "../oplus_wireless.h"
#endif // OPLUS_CUSTOM_OP_DEF

#define DEBUG_BY_FILE_OPS

#define TRACK_LOCAL_T_NS_TO_S_THD		1000000000
#define TRACK_UPLOAD_COUNT_MAX		10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD	(24 * 3600)
#define TRACK_I2CERROR_SAMEADDR_MIN_INTERVAL	3

struct chip_mp2650 *charger_ic = NULL;
int reg_access_allow = 0;
int mp2650_reg = 0;
static int aicl_result = 500;
void mp2650_wireless_set_mps_otg_en_val(int value);
int mp2650_get_vbus_voltage(void);
int mp2650_burst_mode_enable(bool enable);

static DEFINE_MUTEX(mp2650_i2c_access);
static int mp2650_chg_track_upload_icl_err_info(
	struct chip_mp2650 *chip, int err_type);
static int mp2650_track_upload_i2c_err_info(
	struct chip_mp2650 *chip, int err_type, u8 reg);

#ifndef CONFIG_REMOVE_OPLUS_FUNCTION
int __attribute__((weak)) register_device_proc(char *name, char *version, char *vendor)
{
	return 0;
}
#endif /* CONFIG_REMOVE_OPLUS_FUNCTION */

int __attribute__((weak)) oplus_get_charger_cycle(void)
{
	return 0;
}

void __attribute__((weak)) oplus_adsp_voocphy_set_match_temp(void)
{

}

void __attribute__((weak)) oplus_get_props_from_adsp_by_buffer(void)
{

}

int __attribute__((weak)) opchg_get_real_charger_type(void)
{
	return 0;
}

int  __attribute__((weak)) oplus_chg_set_pps_config(int vbus_mv, int ibus_ma)
{
	return 0;
}

u32  __attribute__((weak)) oplus_chg_get_pps_status(void)
{
	return 0;
}

int  __attribute__((weak)) oplus_chg_pps_get_max_cur(int vbus_mv)
{
	return 0;
}

void __attribute__((weak)) oplus_set_typec_cc_open(void)
{

}

int __attribute__((weak)) oplus_set_bcc_curr_to_voocphy(int bcc_curr)
{
	return 0;
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
int __attribute__((weak)) oplus_force_get_subboard_temp(void)
{
	return 0;
}

int __attribute__((weak)) oplus_check_cc_mode(void)
{
	return -EINVAL;
}
#endif

static int __tongfeng_test_mp2650_write_reg(int reg, int val)
{
    int ret = 0;
    struct chip_mp2650 *chip = charger_ic;
	int retry = 3;

	if(!chip) {
        chg_err("chip is NULL\n");
		return 0;
	}

    ret = i2c_smbus_write_byte_data(chip->client, reg, val);

	if (ret < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_write_byte_data(chip->client, reg, val);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

    if (ret < 0) {
        chg_err("i2c write fail: can't write %02x to %02x: %d\n",
        val, reg, ret);
        return ret;
    }

    return 0;
}


static ssize_t mp2650_reg_access_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#if 0
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
#endif

    return snprintf(buf, PAGE_SIZE, "0x%02x\n", reg_access_allow);
}
static ssize_t mp2650_reg_access_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#if 0
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s: value=%d\n", __FUNCTION__, val);
	reg_access_allow = val;

	return count;

}
static int mp2650_read_reg(int reg, int *returnData);
static int mp2650_read_reg(int reg, int *returnData);

static ssize_t mp2650_reg_set_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    size_t count = 0;
    int reg_val = 0;

	mp2650_read_reg(mp2650_reg, &reg_val);
	count += snprintf(buf+count, PAGE_SIZE-count, "reg[0x%02x]: 0x%02x\n", mp2650_reg, reg_val);
    return count;
}

static ssize_t mp2650_reg_set_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int databuf[2] = {0, 0};

    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        pr_err("%s: reg[0x%02x]=0x%02x\n", __FUNCTION__, databuf[0], databuf[1]);
		mp2650_reg = databuf[0];
		__tongfeng_test_mp2650_write_reg((int)databuf[0], (int)databuf[1]);
    }
    return count;
}

static ssize_t mp2650_regs_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
    ssize_t len = 0;
    int i = 0;
    int reg_val = 0;
	
	for (i = MP2650_FIRST_REG; i <= 0x15; i++) {

		mp2650_read_reg(i, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x \n", i, reg_val);
    }
    return len;
}
static ssize_t mp2650_regs_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{

    return count;
}

static DEVICE_ATTR(reg_access, S_IWUSR | S_IRUGO, mp2650_reg_access_show, mp2650_reg_access_store);
static DEVICE_ATTR(reg_set, S_IWUSR | S_IRUGO, mp2650_reg_set_show, mp2650_reg_set_store);
static DEVICE_ATTR(read_regs, S_IWUSR | S_IRUGO, mp2650_regs_show, mp2650_regs_store);

static struct attribute *mp2650_attributes[] = {
    &dev_attr_reg_access.attr,
    &dev_attr_reg_set.attr,
	&dev_attr_read_regs.attr,
	NULL
};

static struct attribute_group mp2650_attribute_group = {
	.attrs = mp2650_attributes
};



static int __mp2650_read_reg(int reg, int *returnData)
{
	int ret = 0;
	struct chip_mp2650 *chip = charger_ic;
	int retry = 3;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	if (reg == REG1C1D_MP2650_ADDRESS) {
		ret = i2c_smbus_read_word_data(chip->client, (unsigned char)reg);
		if (ret < 0) {
			while(retry > 0) {
				usleep_range(5000, 5000);
				ret = i2c_smbus_read_word_data(chip->client, (unsigned char)reg);
				if (ret < 0) {
					retry--;
				} else {
					break;
				}
			}
		}

	} else {

		ret = i2c_smbus_read_byte_data(chip->client, (unsigned char)reg);

		if (ret < 0) {
			while(retry > 0) {
				usleep_range(5000, 5000);
				ret = i2c_smbus_read_byte_data(chip->client, (unsigned char)reg);
				if (ret < 0) {
					retry--;
				} else {
					break;
				}
			}
		}

	}

	if (ret < 0 || chip->debug_force_i2c_err != 0) {
		chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		if (chip->track_init_done)
			mp2650_track_upload_i2c_err_info(chip, ret, reg);
		return ret;
	} else {
		*returnData = ret;
	}

	return 0;
}

static int mp2650_read_reg(int reg, int *returnData)
{
    int ret = 0;
//    struct chip_mp2650 *chip = charger_ic;

	mutex_lock(&mp2650_i2c_access);
	ret = __mp2650_read_reg(reg, returnData);
	mutex_unlock(&mp2650_i2c_access);
    return ret;
}

static int __mp2650_write_reg(int reg, int val)
{
	int ret = 0;
	struct chip_mp2650 *chip = charger_ic;
	int retry = 3;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	if (reg_access_allow != 0) {
		return 0;
	}
    ret = i2c_smbus_write_byte_data(chip->client, reg, val);

	if (ret < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_write_byte_data(chip->client, reg, val);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0 || chip->debug_force_i2c_err != 0) {
		chg_err("i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, ret);
		if (chip->track_init_done)
			mp2650_track_upload_i2c_err_info(chip, ret, reg);
		return ret;
	}

	return 0;
}

/**********************************************************
  *
  *   [Read / Write Function] 
  *
  *********************************************************/
/*
*/

static int mp2650_config_interface(int RegNum, int val, int MASK)
{
    int mp2650_reg = 0;
    int ret = -1;
    int count = 0;

    mutex_lock(&mp2650_i2c_access);
    while (count++ < 3 && ret != 0) {
        ret = __mp2650_read_reg(RegNum, &mp2650_reg);

        if (ret != 0)
            msleep(10);
    }
    if (ret != 0) {
        mutex_unlock(&mp2650_i2c_access);
        return ret;
    }
    //chg_err(" Reg[%x]=0x%x %d\n", RegNum, mp2650_reg, MASK);

    mp2650_reg &= ~MASK;
    mp2650_reg |= val;

    //chg_err(" write Reg[%x]=0x%x %d\n", RegNum, mp2650_reg, val);

    count = 0;
    ret = -1;
    while (count++ < 3 && ret != 0) {
        ret = __mp2650_write_reg(RegNum, mp2650_reg);

        if (ret != 0)
            msleep(10);
    }
    //ret = __mp2650_read_reg(RegNum, &mp2650_reg);

    //chg_err(" Check Reg[%x]=0x%x\n", RegNum, mp2650_reg);

    mutex_unlock(&mp2650_i2c_access);

    return ret;
}


/*this interface just for busrt mode use, do not add any other mutex */
static int mp2650_config_interface_without_lock(int RegNum, int val, int MASK)
{
    int mp2650_reg = 0;
    int ret = -1;
    int count = 0;

    while (count++ < 3 && ret != 0) {
        ret = __mp2650_read_reg(RegNum, &mp2650_reg);

        if (ret != 0)
            msleep(10);
    }
    if (ret != 0) {
        return ret;
    }

    mp2650_reg &= ~MASK;
    mp2650_reg |= val;

    count = 0;
    ret = -1;
    while (count++ < 3 && ret != 0) {
        ret = __mp2650_write_reg(RegNum, mp2650_reg);

        if (ret != 0)
            msleep(10);
    }

    return ret;
}


int mp2650_set_vindpm_vol(int vol)//default 4.5V
{
	int rc;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	tmp = (vol - REG01_MP2650_VINDPM_THRESHOLD_OFFSET)/REG01_MP2650_VINDPM_THRESHOLD_STEP;
	rc = mp2650_config_interface(REG01_MP2650_ADDRESS, tmp << REG01_MP2650_VINDPM_THRESHOLD_SHIFT, REG01_MP2650_VINDPM_THRESHOLD_MASK);
	chg_debug("vindpm_vol [%d] ret [%d]\n", vol, rc);

    return rc;
}

int mp2650_usbin_input_current_limit[] = {
	500, 800, 1000, 1200, 1500, 1750, 2000, 2200, 2500, 2750, 3000,
};

int mp2650_input_Ilimit_disable(void)
{
    int rc = 0;
    struct chip_mp2650 *chip = charger_ic;

    if (atomic_read(&chip->charger_suspended) == 1)
        return 0;


    return rc;
}

int mp2650_get_pre_icl_index(void)
{
	int reg_val = 0;
	int icl_index = 0;
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = mp2650_read_reg(REG00_MP2650_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG00_mp2650_ADDRESS rc = %d\n", rc);
		return icl_index;
	}

	reg_val &= REG00_MP2650_1ST_CURRENT_LIMIT_MASK;

	switch (reg_val) {
	case REG00_MP2650_1ST_CURRENT_LIMIT_500MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_0;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_800MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_1;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_1000MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_2;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_1200MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_3;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_1500MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_4;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_1750MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_5;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_2000MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_6;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_2200MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_7;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_2500MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_8;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_2750MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_9;
		break;
	case REG00_MP2650_1ST_CURRENT_LIMIT_3000MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_10;
		break;
	default:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_0;
		break;
	}

	chg_debug( "pre icl=%d setting %02x\n", mp2650_usbin_input_current_limit[icl_index], icl_index);
	return icl_index;
}



int mp2650_get_charger_vol(void)
{
    int chg_vol = 0;

#ifdef CONFIG_OPLUS_CHARGER_MTK
    static int chv_vol_pre = 0;
	if (oplus_vooc_get_allow_reading() == true && (oplus_pps_get_support_type() != PPS_SUPPORT_2CP &&
		oplus_pps_get_support_type() != PPS_SUPPORT_3CP)) {
        chg_vol = mp2650_get_vbus_voltage();
        if (chg_vol >= 0) {
            chv_vol_pre = chg_vol;
        }
    } else {
        chg_vol = battery_meter_get_charger_voltage();
        if (chg_vol >= 0) {
            chv_vol_pre = chg_vol;
        }
    }
#else
    chg_vol = qpnp_get_prop_charger_voltage_now();
#endif
    return chg_vol;
}

int mp2650_input_current_limit_ctrl_by_vooc_write(int current_ma)
{
	int rc = 0;
	int tmp = 0;
	int count =0;
	struct chip_mp2650 *chip = charger_ic;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_read_reg(REG00_MP2650_ADDRESS, &tmp);
	if (rc) {
		chg_err("Couldn't read REG00_mp2650_ADDRESS rc = %d\n", rc);
		return 0;
	}

	tmp = tmp * 50;
	for (count=(tmp / 500); count > 2; count--) {
		chg_err("set charge current limit = %d\n", 500*count);
		tmp = 500*count - REG00_MP2650_1ST_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, tmp << REG00_MP2650_1ST_CURRENT_LIMIT_SHIFT, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, tmp << REG0F_MP2650_2ND_CURRENT_LIMIT_SHIFT, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		msleep(25);
	}

	for (count=3; count <= (current_ma /500); count++) {
		chg_err("set charge current limit = %d\n", 500*count);
		tmp = 500*count - REG00_MP2650_1ST_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, tmp << REG00_MP2650_1ST_CURRENT_LIMIT_SHIFT, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, tmp << REG0F_MP2650_2ND_CURRENT_LIMIT_SHIFT, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		msleep(25);
	}
	chg_err("set charge ctrl_by_vooc current limit = %d\n", current_ma);
	tmp = current_ma - REG00_MP2650_1ST_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;

	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, tmp << REG00_MP2650_1ST_CURRENT_LIMIT_SHIFT, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, tmp << REG0F_MP2650_2ND_CURRENT_LIMIT_SHIFT, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);

	return rc;
}

int mp2650_input_current_limit_write(int current_ma)
{
	int i = 0, rc = 0;
    	int chg_vol = 0;
    	int pre_icl_index = 0;
	int sw_aicl_point = 0;
	struct chip_mp2650 *chip = charger_ic;
	struct oplus_chg_chip *oplus_chip = oplus_chg_get_chg_struct();
	int tmp_aicl_hw_point;
	int subtype = CHARGER_SUBTYPE_DEFAULT;

	if (!chip || !oplus_chip) {
		chg_err("chip or oplus_chip is NULL\n");
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_vol = mp2650_get_charger_vol();
	subtype = oplus_chip->chg_ops->get_charger_subtype();

	if ((oplus_chip->pdqc_9v_voltage_adaptive) &&
	    (oplus_vooc_get_fastchg_started() == false) &&
	    ((subtype == CHARGER_SUBTYPE_PD) ||
	    (subtype == CHARGER_SUBTYPE_QC)) &&
	    (chg_vol > MP2762_AICL_POINT_SWITCH_THRE)) {
		tmp_aicl_hw_point = MP2762_AICL_POINT_VOL_9V;
		sw_aicl_point = MP2762_AICL_POINT_VOL_9V;
	} else {
		tmp_aicl_hw_point = chip->hw_aicl_point;
		sw_aicl_point = chip->sw_aicl_point;
	}

	chg_debug("hw_sw_vol[%d %d] chg_vol [%d] pdqc_9v_voltage_adaptive %s\n",
		tmp_aicl_hw_point, sw_aicl_point, chg_vol,
		oplus_chip->pdqc_9v_voltage_adaptive == true ?"true":"false");

	for (i = ARRAY_SIZE(mp2650_usbin_input_current_limit) - 1; i >= 0; i--) {
        	if (mp2650_usbin_input_current_limit[i] <= current_ma) {
            		break;
       		} else if (i == 0) {
            		break;
        	}
   	}

	chg_debug( "usb input max current limit=%d setting %02x\n", current_ma, i);

	pre_icl_index = mp2650_get_pre_icl_index();
	if (pre_icl_index < 2) {
        	//<1.2A, have been set 500 in bqmp2650_hardware_init
    	} else {
        	for (i = pre_icl_index - 1; i > 0; i--) {
			switch(i) {
			case INPUT_CURRENT_LIMIT_INDEX_1:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_800MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_800MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_2:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_3:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1200MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1200MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_4:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_5:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1750MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1750MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_6:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_7:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2200MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2200MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_8:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_9:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2750MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2750MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_10:
				rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_3000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_3000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
				break;
                	default:
                    		break;
            		 }
            	msleep(50);
          	}
	}

	i = INPUT_CURRENT_LIMIT_INDEX_0; /* 500 */
    	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
    	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_get_charger_vol();
    	if (chg_vol < sw_aicl_point) {
        	chg_debug( "use 500 here\n");
        	goto aicl_end;
	} else if (current_ma < 800)
        	goto aicl_end;

	i = INPUT_CURRENT_LIMIT_INDEX_1; /* 800 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_800MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_800MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
    	chg_vol = mp2650_get_charger_vol();
   	if (chg_vol < sw_aicl_point) {
        	i = i - 1;
        	goto aicl_pre_step;
	} else if (current_ma < 1000)
        	goto aicl_end;

	i = INPUT_CURRENT_LIMIT_INDEX_2; /* 1000 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
	        i = i - 1;
	        goto aicl_pre_step;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_3; /* 1200 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1200MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1200MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_get_charger_vol();
	chg_err("the i=3 chg_vol: %d\n", chg_vol);
	if (chg_vol < sw_aicl_point) {
        	i = i - 2; //We DO NOT use 1.2A here
        	goto aicl_pre_step;
	} else if (current_ma < 1200) {
        	i = i - 1; /* We use 1A here */
        	goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_4; /* 1500 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
    	chg_vol = mp2650_get_charger_vol();
    	if (chg_vol < sw_aicl_point) {
        	i = i - 3; //We DO NOT use 1.2A here
        	goto aicl_pre_step;
	} else if (current_ma < 1500) {
        	i = i - 1; /*We use 1.2A here*/
        	goto aicl_end;
	} else if (current_ma < 2000) {
        	goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_5; /* 1750 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1750MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1750MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
    	chg_vol = mp2650_get_charger_vol();
    	if (chg_vol < sw_aicl_point) {
        	i = i - 3; //1.2
        	goto aicl_pre_step;
    	}

	i = INPUT_CURRENT_LIMIT_INDEX_6; /* 2000 */
    	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
   	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
    	chg_vol = mp2650_get_charger_vol();
    	if (chg_vol < sw_aicl_point) {
        	i = i - 3; //1.5
        	goto aicl_pre_step;
	} else if (current_ma < 2200) {
        	goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_7; /* 2200 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2200MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2200MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	} else if (current_ma < 2500) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_8; /* 2500 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	} else if (current_ma < 2750) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_9; /* 2750 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2750MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2750MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	} else if (current_ma < 3000) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_10; /* 3000 */
	rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_3000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_3000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	}

aicl_pre_step:
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, mp2650_usbin_input_current_limit[i], sw_aicl_point);
	goto aicl_rerun;
aicl_end:
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, mp2650_usbin_input_current_limit[i], sw_aicl_point);
	goto aicl_rerun;
aicl_rerun:
	aicl_result = mp2650_usbin_input_current_limit[i];
	switch (i) {
	case INPUT_CURRENT_LIMIT_INDEX_0:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_1:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_800MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_800MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_2:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_3:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1200MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1200MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_4:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_5:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1750MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_1750MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_6:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_7:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2200MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2200MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_8:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2500MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2500MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_9:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2750MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_2750MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_10:
		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_3000MA, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, REG00_MP2650_1ST_CURRENT_LIMIT_3000MA, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
		break;
	default:
		break;
	}

	mp2650_set_vindpm_vol(tmp_aicl_hw_point);

	if (chip->track_init_done &&
	    ((oplus_chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP &&
	    current_ma >= OPLUS_CHG_TRACK_ICL_MONITOR_THD_MA &&
	    aicl_result < OPLUS_CHG_TRACK_ICL_MONITOR_THD_MA) ||
	    chip->debug_force_icl_err != 0))
		mp2650_chg_track_upload_icl_err_info(
			chip, TRACK_PMIC_ERR_ICL_VBUS_LOW_POINT);

	return rc;
}

int mp2650_charging_current_write_fast(int chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("set charge current = %d\n", chg_cur);

	tmp = chg_cur - REG02_MP2650_CHARGE_CURRENT_SETTING_OFFSET;
	tmp = tmp / REG02_MP2650_CHARGE_CURRENT_SETTING_STEP;
	
	rc = mp2650_config_interface(REG02_MP2650_ADDRESS, tmp << REG02_MP2650_CHARGE_CURRENT_SETTING_SHIFT, REG02_MP2650_CHARGE_CURRENT_SETTING_MASK);

	return rc;
}

#if 1
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
int mp2650_chg_get_dyna_aicl_result(void)
{
    return aicl_result;
}
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */


void mp2650_set_aicl_point(int vbatt)
{
	struct chip_mp2650 *chip = charger_ic;
	struct oplus_chg_chip *oplus_chip = oplus_chg_get_chg_struct();
	int chg_vol;
	int tmp_pdqc_hw_vol = 0;
	int subtype = CHARGER_SUBTYPE_DEFAULT;
	bool neend_check_vindpm = false;

	if (!chip || !oplus_chip) {
		chg_err("chip or oplus_chip is NULL\n");
		return;
	}

	chg_vol = mp2650_get_charger_vol();
	subtype = oplus_chip->chg_ops->get_charger_subtype();

	if ((oplus_chip->pdqc_9v_voltage_adaptive) &&
	    (oplus_vooc_get_fastchg_started() == false) &&
	    ((subtype == CHARGER_SUBTYPE_PD) ||
	    (subtype == CHARGER_SUBTYPE_QC)) &&
	    (chg_vol > MP2762_AICL_POINT_SWITCH_THRE)) {
		tmp_pdqc_hw_vol = MP2762_AICL_POINT_VOL_9V;
		mp2650_set_vindpm_vol(tmp_pdqc_hw_vol);
		chg_debug("pdqc_hw_volpont_vol [%d] chg_vol %d hw_sw_vbat [%d %d %d]\n",
			tmp_pdqc_hw_vol, chg_vol, chip->hw_aicl_point,
			chip->sw_aicl_point, vbatt);
	} else {
		if (chip->hw_aicl_point == MP2762_HW_AICL_POINT_5V_PHASE1 &&
		    vbatt > MP2762_AICL_POINT_VOL_PHASE2) {
			chip->hw_aicl_point = MP2762HW_AICL_POINT_5V_PHASE2;
			chip->sw_aicl_point = MP2762SW_AICL_POINT_5V_PHASE2;
			neend_check_vindpm = true;
		} else if (chip->hw_aicl_point == MP2762HW_AICL_POINT_5V_PHASE2 &&
			    vbatt < MP2762_AICL_POINT_VOL_PHASE1) {
			chip->hw_aicl_point = MP2762_HW_AICL_POINT_5V_PHASE1;
			chip->sw_aicl_point = MP2762_SW_AICL_POINT_5V_PHASE1;
			neend_check_vindpm = true;
		}

		if (neend_check_vindpm || oplus_chip->pdqc_9v_voltage_adaptive)
			mp2650_set_vindpm_vol(chip->hw_aicl_point);

		chg_debug("chg_vol %d hw_sw_vbat [%d %d %d]\n", chg_vol,
			chip->hw_aicl_point, chip->sw_aicl_point, vbatt);
	}
}

int mp2650_set_enable_volatile_writes(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	//need do nothing
	
    return rc;
}

int mp2650_set_complete_charge_timeout(int val)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

    if (val == OVERTIME_AC) {
        val = REG09_MP2650_CHARGE_TIMER_EN_ENABLE | REG09_MP2650_FAST_CHARGE_TIMER_8H;
    } else if (val == OVERTIME_USB) {
        val = REG09_MP2650_CHARGE_TIMER_EN_ENABLE | REG09_MP2650_FAST_CHARGE_TIMER_12H;
    } else {
        val = REG09_MP2650_CHARGE_TIMER_EN_DISABLE | REG09_MP2650_FAST_CHARGE_TIMER_20H;
    }

    rc = mp2650_config_interface(REG09_MP2650_ADDRESS, val, REG09_MP2650_FAST_CHARGE_TIMER_MASK | REG09_MP2650_CHARGE_TIMER_EN_MASK);
    if (rc < 0) {
        chg_err("Couldn't complete charge timeout rc = %d\n", rc);
    }
    return 0;
}

int mp2650_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

    chg_err("vfloat_mv = %d\n", vfloat_mv); 

    if (vfloat_mv > 5000) {
        if (vfloat_mv > 9000) {
            vfloat_mv = 9000;
        }
        vfloat_mv = vfloat_mv / 2;
    }

    if (vfloat_mv > 4500) {
		vfloat_mv = 4500;
    }
    
	tmp = vfloat_mv * 10 - REG04_MP2650_CHARGE_FULL_VOL_OFFSET;
	tmp = tmp / REG04_MP2650_CHARGE_FULL_VOL_STEP;
	
	rc = mp2650_config_interface(REG04_MP2650_ADDRESS, tmp << REG04_MP2650_CHARGE_FULL_VOL_SHIFT, REG04_MP2650_CHARGE_FULL_VOL_MASK);
	
	return rc;
}

int mp2650_set_prechg_voltage_threshold(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG07_MP2650_ADDRESS, REG07_MP2650_PRECHARGE_THRESHOLD_7200MV, REG07_MP2650_PRECHARGE_THRESHOLD_MASK);

	return 0;
}

int mp2650_set_prechg_current( int ipre_mA)
{
    int tmp = 0;
    int rc = 0;
    struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
    tmp = ipre_mA - REG03_MP2650_PRECHARGE_CURRENT_LIMIT_OFFSET;
    tmp = tmp / REG03_MP2650_PRECHARGE_CURRENT_LIMIT_STEP;
    rc = mp2650_config_interface(REG03_MP2650_ADDRESS, (tmp + 1) << REG03_MP2650_PRECHARGE_CURRENT_LIMIT_SHIFT, REG03_MP2650_PRECHARGE_CURRENT_LIMIT_MASK);
	
	return 0;
}

int mp2650_set_termchg_current(int term_curr)
{
	int rc = 0;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic;	

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

    chg_err("term_current = %d\n", term_curr); 
    tmp = term_curr - REG03_MP2650_TERMINATION_CURRENT_LIMIT_OFFSET;
    tmp = tmp / REG03_MP2650_TERMINATION_CURRENT_LIMIT_STEP;
	
	rc = mp2650_config_interface(REG03_MP2650_ADDRESS, tmp << REG03_MP2650_TERMINATION_CURRENT_LIMIT_SHIFT, REG03_MP2650_TERMINATION_CURRENT_LIMIT_MASK);
	return 0;
}

int mp2650_set_rechg_voltage(int recharge_mv)
{
	int rc = 0;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    
	tmp = recharge_mv - REG04_MP2650_BAT_RECHARGE_THRESHOLD_OFFSET;
	tmp = tmp / REG04_MP2650_BAT_RECHARGE_THRESHOLD_STEP;
	

    /*The rechg voltage is: Charge Full Voltage - 100mV  or - 200mV, default is - 100mV*/
	rc = mp2650_config_interface(REG04_MP2650_ADDRESS, tmp << REG04_MP2650_BAT_RECHARGE_THRESHOLD_SHIFT, REG04_MP2650_BAT_RECHARGE_THRESHOLD_MASK);

    if (rc) {
        chg_err("Couldn't set recharging threshold rc = %d\n", rc);     
    }
	 return rc;
}

int mp2650_set_wdt_timer(int reg)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG09_MP2650_ADDRESS, reg, REG09_MP2650_WTD_TIMER_MASK);
    if (rc) {
        chg_err("Couldn't set recharging threshold rc = %d\n", rc);     
    }
    
    return 0;
}

int mp2650_set_chging_term_disable(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_config_interface(REG09_MP2650_ADDRESS, REG09_MP2650_CHARGE_TERMINATION_EN_DISABLE, REG09_MP2650_CHARGE_TERMINATION_EN_MASK);
    if (rc) {
        chg_err("Couldn't set chging term disable rc = %d\n", rc);     
    }
    return rc;
}

int mp2650_set_chging_term_enable(void)
{
	int rc = 0;
    struct chip_mp2650 *chip = charger_ic;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_config_interface(REG09_MP2650_ADDRESS, REG09_MP2650_CCHARGE_TERMINATION_EN_ENABLE, REG09_MP2650_CHARGE_TERMINATION_EN_MASK);
    if (rc) {
        chg_err("Couldn't set chging term enable rc = %d\n", rc);     
    }
    return rc;
}

int mp2650_kick_wdt(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    rc = mp2650_config_interface(REG08_MP2650_ADDRESS, REG08_MP2650_WTD_RST_RESET, REG08_MP2650_WTD_RST_MASK);
    if (rc) {
        chg_err("Couldn't mp2650 kick wdt rc = %d\n", rc);     
    }  
    return rc;
}

static void mp2650_wdt_enable(bool wdt_enable)
{
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return;
	}
	if (atomic_read(&chip->charger_suspended) == 1)
		return;

	if (wdt_enable)
		mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_40S);
	else
		mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_DISABLE);

	chg_err("mp2650_wdt_enable[%d]\n", wdt_enable);
}

int mp2650_enable_charging(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
		REG08_MP2650_CHG_EN_ENABLE, REG08_MP2650_CHG_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn'tmp2650_enable_charging rc = %d\n", rc);
	}

	chg_err("mp2650_enable_charging \n");
	return rc;
}

int mp2650_disable_charging(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

    chg_err(" mp2650_disable_charging \n");

	if(atomic_read(&chip->charger_suspended) == 1) {	
		chg_err(" charger_suspended \n");
		return 0;
	}
    rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 
		REG08_MP2650_CHG_EN_DISABLE, REG08_MP2650_CHG_EN_MASK);
    if (rc < 0) {
		chg_err("Couldn't mp2650_disable_charging  rc = %d\n", rc);
	}

	return rc;
}

int mp2650_check_charging_enable(void)
{
	int rc = 0;
	int reg_val = 0;
    struct chip_mp2650 *chip = charger_ic;
	
	bool charging_enable = false;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_read_reg(REG08_MP2650_ADDRESS, &reg_val);
    if (rc) {
        chg_err("Couldn't read REG08_MP2650_ADDRESS rc = %d\n", rc);
        return 0;
    }
	
    charging_enable = ((reg_val & REG08_MP2650_CHG_EN_MASK) == REG08_MP2650_CHG_EN_ENABLE) ? 1 : 0;
	
	return charging_enable;	
}

int mp2650_enable_adc_detect(bool enable)
{
	int rc;

	chg_err("mp2650_enable_adc_detect enable= %d\n", enable);
	/* Enable or disable adc detect */
	if (enable) {
		rc = mp2650_config_interface(REG0B_MP2650_ADDRESS, 0x3, REG0B_MP2650_PROCHOT_PSYS_CFG_MASK);
		if (rc < 0)
			chg_err("Couldn't mp2650_enable_adc_detect rc = %d\n", rc);
	} else {
		rc = mp2650_config_interface(REG0B_MP2650_ADDRESS, 0, REG0B_MP2650_PROCHOT_PSYS_CFG_MASK);
		if (rc < 0)
			chg_err("Couldn't mp2650_enable_adc_detect rc = %d\n", rc);
	}
	return rc;
}

int mp2650_get_vbus_voltage(void)
{
    int vol_high = 0;
    int vol_low = 0;
    int vbus_vol = 0;
	int rc = 0;

	if(oplus_is_rf_ftm_mode()) {
		mp2650_enable_adc_detect(true);
		msleep(1);
	}

	rc = mp2650_read_reg(REG1C1D_MP2650_ADDRESS, &vbus_vol);
    if (rc) {
        chg_err("Couldn't read REG1C1D_MP2650_ADDRESS rc = %d\n", rc);
        return 0;
    }

	vol_low = vbus_vol & REG01_MP2650_VINDPM_THRESHOLD_MASK;
	vol_high = (vbus_vol >> 8) & REG01_MP2650_VINDPM_THRESHOLD_MASK;
    vbus_vol = (vol_high * 100) + ((vol_low >> 6) * 25);

	if(oplus_is_rf_ftm_mode())
		mp2650_enable_adc_detect(false);

    return vbus_vol;
}

int mp2650_get_ibus_current(void)
{
    int cur_high = 0;
    int cur_low = 0;
    int ibus_curr = 0;
	int rc = 0;

    rc = mp2650_read_reg(REG1E1F_MP2650_ADDRESS, &cur_low);
    if (rc) {
        chg_err("Couldn't read REG1E1F_MP2650_ADDRESS rc = %d\n", rc);
        return 0;
    }

    rc = mp2650_read_reg((REG1E1F_MP2650_ADDRESS + 1), &cur_high);
    if (rc) {
        chg_err("Couldn't read REG1E1F_MP2650_ADDRESS + 1 rc = %d\n", rc);
        return 0;
    }

    ibus_curr = (cur_high * 25000) + ((cur_low >> 6) * 6250);
    return ibus_curr;
}

int mp2650_get_termchg_voltage(void)
{
    int vol_data = 0;
	int rc = 0;

    rc = mp2650_read_reg(REG04_MP2650_ADDRESS, &vol_data);
    if (rc) {
        chg_err("Couldn't read REG04_MP2650_ADDRESS rc = %d\n", rc);
        return -1;
    }

	vol_data = (vol_data & REG04_MP2650_CHARGE_FULL_VOL_MASK) >> REG04_MP2650_CHARGE_FULL_VOL_SHIFT;
	vol_data = vol_data * REG04_MP2650_CHARGE_FULL_VOL_STEP;
	vol_data = vol_data + REG04_MP2650_CHARGE_FULL_VOL_OFFSET;
	vol_data = vol_data / 10;

	return vol_data;
}

int mp2650_get_termchg_current(void)
{
    int cur_data = 0;
	int rc = 0;

    rc = mp2650_read_reg(REG03_MP2650_ADDRESS, &cur_data);
    if (rc) {
        chg_err("Couldn't read REG03_MP2650_ADDRESS rc = %d\n", rc);
        return -1;
    }

	cur_data = (cur_data & REG03_MP2650_TERMINATION_CURRENT_LIMIT_MASK) >> REG03_MP2650_TERMINATION_CURRENT_LIMIT_SHIFT;
	cur_data = cur_data * REG03_MP2650_TERMINATION_CURRENT_LIMIT_STEP;
	cur_data = cur_data + REG03_MP2650_TERMINATION_CURRENT_LIMIT_OFFSET;

	return cur_data;
}

int mp2650_registers_read_full(void)
{

	int rc;
	int reg_full = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_read_reg(REG13_MP2650_ADDRESS, &reg_full);
	if (rc) {
		chg_err("Couldn't read STAT_C rc = %d\n", rc);
		return 0;
	}
	
	reg_full = ((reg_full & REG13_MP2650_CHARGING_STATUS_MASK) == REG13_MP2650_CHARGING_STATUS_CHARGE_TERMINATION) ? 1 : 0;
	if (reg_full) {
		chg_err("the mp2650 is full");
		mp2650_dump_registers();
	}

	return 0;
}

bool mp2650_check_suspend_charger(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;
	int data = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_read_reg(REG08_MP2650_ADDRESS, &data);
	if (rc) {
		chg_err("Couldn't read REG1C1D_MP2650_ADDRESS rc = %d\n", rc);
		return 0;
	}

	return (data & REG08_MP2650_LEARN_EN_MASK);
}

int mp2650_suspend_charger(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
	mp2650_config_interface(REG08_MP2650_ADDRESS, REG08_MP2650_LEARN_EN_ENABLE, REG08_MP2650_LEARN_EN_MASK);

	chg_err( " rc = %d\n", rc);
	if (rc < 0) {
		chg_err("Couldn't mp2650_suspend_charger rc = %d\n", rc);
	}

	return rc;
}
int mp2650_unsuspend_charger(void)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int boot_mode = get_boot_mode();
#endif
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (boot_mode == META_BOOT || boot_mode == FACTORY_BOOT
			|| boot_mode == ADVMETA_BOOT || boot_mode == ATE_FACTORY_BOOT) {
		mp2650_config_interface(REG08_MP2650_ADDRESS, REG08_MP2650_LEARN_EN_ENABLE, REG08_MP2650_LEARN_EN_MASK);
		chg_err("Meta mode or FTM stop charging!");
		return 0;
	}
#endif
    mp2650_config_interface(REG08_MP2650_ADDRESS, REG08_MP2650_LEARN_EN_DISABLE, REG08_MP2650_LEARN_EN_MASK);

    chg_err( " rc = %d\n", rc);
    if (rc < 0) {
		chg_err("Couldn't mp2650_unsuspend_charger rc = %d\n", rc);
	}
	return rc;
}

bool mp2650_check_charger_resume(void)
{
    struct chip_mp2650 *chip = charger_ic;
	if(atomic_read(&chip->charger_suspended) == 1) {
		return false;
	} else {
		return true;
	}
}

int mp2650_reset_charger_for_wired_charge(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}


    chg_err("reset mp2650 for wired charge! \n");
    
    rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 
		REG08_MP2650_REG_RST_RESET, REG08_MP2650_REG_RST_MASK);
    if (rc < 0) {
		chg_err("Couldn't mp2650_reset_charger  rc = %d\n", rc);
	}

	return rc;
}

int mp2650_otg_enable(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;
	int retry = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	while (retry <= WAIT_RESUME_MAX_TRY_TIME) {
		/*mp2650 resume*/
		if (atomic_read(&chip->charger_suspended) != 1) {
			break;
		}

		msleep(10);

		retry++;
		if (retry == WAIT_RESUME_MAX_TRY_TIME) {
			chg_err("wait charger_resume timeout \n");
			return 0;
		}
	}

	rc = mp2650_burst_mode_enable(true);

	mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_DISABLE);
	rc = mp2650_config_interface(REG53_MP2650_ADDRESS, 0x95, 0xff);
	rc = mp2650_config_interface(REG3F_MP2650_ADDRESS, 0x20, 0xff);

	mp2650_wireless_set_mps_otg_en_val(MP2650_GPIO_OTG_EN);

	rc = mp2650_otg_ilim_set(REG07_MP2650_OTG_CURRENT_LIMIT_1250MA);
	if (rc < 0) {
		chg_err("Couldn't mp2650_otg_ilim_set rc = %d\n", rc);
	}
    	rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
		REG08_MP2650_OTG_EN_ENABLE, REG08_MP2650_OTG_EN_MASK);
    	if (rc < 0) {
		chg_err("Couldn't mp2650_otg_enable  rc = %d\n", rc);
	}

	msleep(10);
	rc = mp2650_config_interface(REG3F_MP2650_ADDRESS, 0x00, 0xff);
	rc = mp2650_config_interface(REG53_MP2650_ADDRESS, 0x00, 0xff);

	return rc;
}

int mp2650_otg_disable(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;
	int retry = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	while (retry <= WAIT_RESUME_MAX_TRY_TIME) {
		/*mp2650 resume*/
		if (atomic_read(&chip->charger_suspended) != 1) {
			break;
		}

		msleep(10);

		retry++;
		if (retry == WAIT_RESUME_MAX_TRY_TIME) {
			chg_err("wait charger_resume timeout \n");
			return 0;
		}
	}

	mp2650_wireless_set_mps_otg_en_val(MP2650_GPIO_OTG_DIS);
	rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
		REG08_MP2650_OTG_EN_DISABLE, REG08_MP2650_OTG_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_otg_disable  rc = %d\n", rc);
	}

	mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_40S);

	return rc;
}


int mp2650_otg_wait_vbus_decline(void)
{
#if 1
	msleep(200);
	return 0;
#else
    int rc;
    int vbus_volt = 0;
    struct chip_mp2650 *chip = charger_ic;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("<~WPC~>mp2650_otg_wait_vbus_decline\n");

    do{
        msleep(200);
        vbus_volt = mp2650_get_vbus_voltage();
    }while(vbus_volt >= 3200);
    
	return rc;
#endif
}

int mp2650_reset_charger(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;
	
	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (chip->probe_flag) {
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS,
			REG08_MP2650_REG_RST_RESET, REG08_MP2650_REG_RST_MASK);
		if (rc < 0) {
			chg_err("Couldn't mp2650_reset_charger  rc = %d\n", rc);
		}
		chip->probe_flag = false;
		chg_err("HW reset chip->probe_flag = %d\n", chip->probe_flag);
	} else {
		rc = mp2650_config_interface(REG10_MP2650_ADDRESS, 0x01, 0xFF);
		rc = mp2650_config_interface(REG11_MP2650_ADDRESS, 0xFF, 0xFF);
		rc = mp2650_config_interface(REG12_MP2650_ADDRESS, 0x77, 0xFF);
		rc = mp2650_config_interface(REG2D_MP2650_ADDRESS, 0x0F, 0xFF);
		rc = mp2650_config_interface(REG36_MP2650_ADDRESS, 0xF5, 0xFF);
		rc = mp2650_burst_mode_enable(true);

		rc = mp2650_config_interface(REG00_MP2650_ADDRESS, 0, 0xFF);
		rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, 0, 0xFF);

		rc = mp2650_config_interface(REG01_MP2650_ADDRESS, 0x2D, 0xFF);
		rc = mp2650_config_interface(REG02_MP2650_ADDRESS, 0X14, 0xFF);
		rc = mp2650_config_interface(REG03_MP2650_ADDRESS, 0x32, 0xFF);
		rc = mp2650_config_interface(REG04_MP2650_ADDRESS, 0x4E, 0xFF);
		rc = mp2650_config_interface(REG05_MP2650_ADDRESS, 0x03, 0xFF);
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, 0x5, 0xFF);
		rc = mp2650_config_interface(REG07_MP2650_ADDRESS, 0x14, 0xFF);
		rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 0x16, 0xFF);
		rc = mp2650_config_interface(REG09_MP2650_ADDRESS, 0x4C, 0xFF);
		rc = mp2650_config_interface(REG0A_MP2650_ADDRESS, 0x36, 0xFF);
		rc = mp2650_config_interface(REG0B_MP2650_ADDRESS, 0, 0xFF);
		rc = mp2650_config_interface(REG0C_MP2650_ADDRESS, 0xC0, 0xFF);
		rc = mp2650_config_interface(REG0D_MP2650_ADDRESS, 0x01, 0xFF);
		rc = mp2650_config_interface(REG0E_MP2650_ADDRESS, 0x23, 0xFF);
		rc = mp2650_config_interface(REG31_MP2650_ADDRESS, 0x03, 0xFF);
		rc = mp2650_config_interface(REG33_MP2650_ADDRESS, 0x15, 0xFF);
		/*MP2762A VSYS Ripple Issue 20200730, it occurs after charging full and disable charge*/
		rc = mp2650_config_interface(REG2D_MP2650_ADDRESS, 0x0f, 0xff);
		chg_err(" SW reset chip->probe_flag = %d\n", chip->probe_flag);
	}

	return rc;
}
#if 0
int mp2650_otg_vlim_set(int vlim)
{
    int rc;
    struct chip_mp2650 *chip = charger_ic;

    if(atomic_read(&chip->charger_suspended) == 1) {
        return 0;
    }

    return rc;
}
#endif

int mp2650_otg_ilim_set(int ilim)
{
    int rc;
    struct chip_mp2650 *chip = charger_ic;

    if(atomic_read(&chip->charger_suspended) == 1) {
        return 0;
    }
    rc = mp2650_config_interface(REG07_MP2650_ADDRESS,
    ilim, REG07_MP2650_OTG_CURRENT_LIMIT_MASK );
    if (rc < 0) {
    	chg_err("Couldn't mp2650_otg_ilim_set  rc = %d\n", rc);
    }

    return rc;
}


int mp2650_set_switching_frequency(void)
{
	//int value;
	int rc = 0;
    struct chip_mp2650 *chip = charger_ic;
	
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
	rc = mp2650_config_interface(REG0B_MP2650_ADDRESS, REG0B_MP2650_SW_FREQ_800K, REG0B_MP2650_SW_FREQ_MASK);
	return 0;
}

int mp2650_set_prochot_psys_cfg(void)
{
	//int value;
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG0B_MP2650_ADDRESS, REG0B_MP2650_PROCHOT_PSYS_CFG_DISABLE, REG0B_MP2650_PROCHOT_PSYS_CFG_MASK);
	return 0;
}

int mp2650_set_mps_otg_current(void)
{
	int rc = 0;
    struct chip_mp2650 *chip = charger_ic;
		
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
	rc = mp2650_config_interface(REG07_MP2650_ADDRESS, REG07_MP2650_OTG_CURRENT_LIMIT_1500MA, REG07_MP2650_OTG_CURRENT_LIMIT_MASK);
	return 0;
}

int mp2650_set_mps_otg_voltage(bool is_9v)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (is_9v) {
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_OTG_VOL_OPTION_8750MV, REG06_MP2650_OTG_VOL_OPTION_MASK);
	} else {
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_OTG_VOL_OPTION_4750MV, REG06_MP2650_OTG_VOL_OPTION_MASK);
	}
	return 0;
}

int mp2650_set_mps_second_otg_voltage(bool is_750mv)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (is_750mv) {
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_2ND_OTG_VOL_SETTING_400MV, REG06_MP2650_2ND_OTG_VOL_SETTING_MASK);
	} else {
		rc = mp2650_config_interface(REG06_MP2650_ADDRESS, REG06_MP2650_2ND_OTG_VOL_SETTING_250MV, REG06_MP2650_2ND_OTG_VOL_SETTING_MASK);
	}
	return 0;
}

int mp2650_set_mps_otg_enable(void)
{
	//int value;
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
		chg_err("mp2650_set_mps_otg_enable start \n");
	rc = mp2650_config_interface(REG08_MP2650_ADDRESS, REG08_MP2650_OTG_EN_ENABLE, REG08_MP2650_OTG_EN_MASK);
	return 0;
}

int mp2650_set_mps_otg_disable(void)
{
	//int value;
	int rc = 0;
    struct chip_mp2650 *chip = charger_ic;
		
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
		chg_err("mp2650_set_mps_otg_disable start \n");
	rc = mp2650_config_interface(REG08_MP2650_ADDRESS, REG08_MP2650_OTG_EN_DISABLE, REG08_MP2650_OTG_EN_MASK);
	return 0;
}


int mp2650_enable_hiz(void)
{
	int rc;
    struct chip_mp2650 *chip = charger_ic;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
    rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 
		 REG08_MP2650_LEARN_EN_ENABLE, REG08_MP2650_LEARN_EN_MASK);		
    if (rc < 0) {
		chg_err("Couldn'mp2650_enable_hiz rc = %d\n", rc);
	}
	
	chg_err("mp2650_enable_hiz \n");
	
	return rc;
}

int mp2650_disable_hiz(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
    rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 
		REG08_MP2650_LEARN_EN_DISABLE, REG08_MP2650_LEARN_EN_MASK);
    if (rc < 0) {
		chg_err("Couldn't mp2650_disable_hiz  rc = %d\n", rc);
	}

	chg_err("mp2650_disable_hiz \n");
	
	return rc;
}

int mp2650_enable_buck_switch(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
    rc = mp2650_config_interface(REG12_MP2650_ADDRESS, 
		 REG12_MP2650_BUCK_SWITCH_ENABLE, REG12_MP2650_BUCK_SWITCH_MASK);		
    if (rc < 0) {
		chg_err("Couldn'mp2650_enable_dig_skip rc = %d\n", rc);
	}
	
	chg_err("mp2650_enable_dig_skip \n");
	
	return rc;
}

int mp2650_disable_buck_switch(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	
    rc = mp2650_config_interface(REG12_MP2650_ADDRESS, 
		 REG12_MP2650_BUCK_SWITCH_DISABLE, REG12_MP2650_BUCK_SWITCH_MASK);	
    if (rc < 0) {
		chg_err("Couldn't mp2650_disable_dig_skip  rc = %d\n", rc);
	}

	chg_err("mp2650_disable_dig_skip \n");
	
	return rc;
}

int mp2650_enable_async_mode(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG36_MP2650_ADDRESS,
		REG36_MP2650_ASYNC_MODE_ENABLE, REG36_MP2650_ASYNC_MODE_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_enable_async_mode rc = %d\n", rc);
	}

	chg_err("mp2650_enable_async_mode\n");

	return rc;
}

int mp2650_disable_async_mode(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_config_interface(REG36_MP2650_ADDRESS,
		REG36_MP2650_ASYNC_MODE_DISABLE, REG36_MP2650_ASYNC_MODE_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_disable_async_mode rc = %d\n", rc);
	}

	chg_err("mp2650_disable_async_mode\n");

	return rc;
}

static int mp2650_vbus_avoid_electric_config(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	return 0;
	chg_err("mp2650_vbus_avoid_electric_config start\n");
	rc = mp2650_config_interface(REG53_MP2650_ADDRESS, 0x95, 0xff);
	rc = mp2650_config_interface(REG39_MP2650_ADDRESS, 0x40, 0xff);
 	rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 0x36, 0xff);
	rc = mp2650_config_interface(REG08_MP2650_ADDRESS, 0x16, 0xff);
	rc = mp2650_config_interface(REG39_MP2650_ADDRESS, 0x00, 0xff);
	rc = mp2650_config_interface(REG53_MP2650_ADDRESS, 0x00, 0xff);
	rc = mp2650_config_interface(REG2F_MP2650_ADDRESS, 0x15, 0xff);
	return 0;
}

int mp2650_check_learn_mode(void)
{
	int rc = 0;
	int reg_val = 0;
	bool learn_mode = false;
    struct chip_mp2650 *chip = charger_ic;
	
	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_read_reg(REG08_MP2650_ADDRESS, &reg_val);
    if (rc) {
        chg_err("Couldn't read REG08_MP2650_ADDRESS rc = %d\n", rc);
        return 0;
    }

	chg_err(" LEARN MODE = %02x\n", reg_val);
	
    learn_mode = ((reg_val & REG08_MP2650_LEARN_EN_MASK) == REG08_MP2650_LEARN_EN_MASK) ? 1 : 0;
	
	return learn_mode;	
}

int mp2650_set_charger_vsys_threshold(int val)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

//change Vsys Skip threshold
	rc = mp2650_config_interface(REG31_MP2650_ADDRESS, val, 0xff);

	return rc;
}


/*this function just can use mp2650_config_interface_without_lock to modify registers */
int mp2650_burst_mode_enable(bool enable)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	mutex_lock(&mp2650_i2c_access);
	//Enable or disable Burst mode
	if (enable) {
		rc = mp2650_config_interface_without_lock(REG53_MP2650_ADDRESS, 0x95, 0xff);
		if (rc != 0) {
			goto error;
		}
		rc = mp2650_config_interface_without_lock(REG3E_MP2650_ADDRESS, 0x00, 0xff);
		if (rc != 0) {
			goto error;
		}
		rc = mp2650_config_interface_without_lock(REG53_MP2650_ADDRESS, 0x00, 0xff);
		if (rc != 0) {
			goto error;
		}
	} else {
		rc = mp2650_config_interface_without_lock(REG53_MP2650_ADDRESS, 0x95, 0xff);
		if (rc != 0) {
			goto error;
		}
		rc = mp2650_config_interface_without_lock(REG3E_MP2650_ADDRESS, 0x80, 0xff);
		if (rc != 0) {
			goto error;
		}
		rc = mp2650_config_interface_without_lock(REG53_MP2650_ADDRESS, 0x00, 0xff);
		if (rc != 0) {
			goto error;
		}
	}
error:
	mutex_unlock(&mp2650_i2c_access);
	return rc;

}

int mp2650_other_registers_init(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	//add for mp2762 damaged issue,keep Ilimi2 duration shortest
	rc = mp2650_config_interface(REG10_MP2650_ADDRESS, 0x01, 0xff);
	rc = mp2650_config_interface(REG11_MP2650_ADDRESS, 0xff, 0xff);

	return rc;
/*
#if 0
    rc = mp2650_config_interface(0x01, 0x2F, 0xFF);
    if (rc < 0) {
		chg_err("Couldn't write 0x01  rc = %d\n", rc);
	}
#endif

    rc = mp2650_config_interface(0x01, 0x2B, 0xFF);
    if (rc < 0) {
		chg_err("Couldn't write 0x01  rc = %d\n", rc);
	}
	
    //rc = mp2650_config_interface(0x3E, 0x01, 0xFF);
    //if (rc < 0) {
	//	chg_err("Couldn't write 0x3E  rc = %d\n", rc);
	//}
	
    //rc = mp2650_config_interface(0x2E, 0x30, 0xFF);
    //if (rc < 0) {
	//	chg_err("Couldn't write 0x2E  rc = %d\n", rc);
	//}
	
    //rc = mp2650_config_interface(0x53, 0x95, 0xFF);
    //if (rc < 0) {
	//	chg_err("Couldn't write 0x53  rc = %d\n", rc);
	//}
	
    //rc = mp2650_config_interface(0x4A, 0x0A, 0xFF);
    //if (rc < 0) {
	//	chg_err("Couldn't write 0x4A  rc = %d\n", rc);
	//}

    //rc = mp2650_config_interface(0x12, 0x75, 0xFF); /////////////
    //if (rc < 0) {
	//	chg_err("Couldn't write 0x12  rc = %d\n", rc);
	//}
#if 0
    rc = mp2650_config_interface(0x2A, 0xC2, 0xFF);
    if (rc < 0) {
		chg_err("Couldn't write 0x2A  rc = %d\n", rc);
	}

    rc = mp2650_config_interface(0x2B, 0x30, 0xFF);
    if (rc < 0) {
		chg_err("Couldn't write 0x2B  rc = %d\n", rc);
	}

    rc = mp2650_config_interface(0x2C, 0x00, 0xFF);
    if (rc < 0) {
		chg_err("Couldn't write 0x2C  rc = %d\n", rc);
	}
#endif

    //rc = mp2650_config_interface(0x37, 0x77, 0xFF);
    //if (rc < 0) {
	//	chg_err("Couldn't write 0x37  rc = %d\n", rc);
	//}
	
	return rc;
*/
}

#define DUMP_REG_LOG_CNT_30S             6
void mp2650_dump_registers(void)
{
    int rc;
	int addr;
    static int dump_count = 0;
    struct chip_mp2650 *chip = charger_ic;
    unsigned int val_buf[MP2650_DUMP_MAX_REG + 5] = {0x0};

	if(atomic_read(&chip->charger_suspended) == 1) {
		return ;
	}

    if(dump_count == DUMP_REG_LOG_CNT_30S) {
        dump_count = 0;
        for (addr = MP2650_FIRST_REG; addr <= MP2650_DUMP_MAX_REG; addr++) {
            rc = mp2650_read_reg(addr, &val_buf[addr]);
            if (rc) {
                chg_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
            }
        }
        rc = mp2650_read_reg(0x48, &val_buf[MP2650_DUMP_MAX_REG + 1]);
        if (rc) {
             chg_err("Couldn't  read 0x48 rc = %d\n", rc);
        }
        rc = mp2650_read_reg(0x49, &val_buf[MP2650_DUMP_MAX_REG + 2]);
        if (rc) {
             chg_err("Couldn't  read 0x49 rc = %d\n", rc);
        }
		rc = mp2650_read_reg(0x2D, &val_buf[MP2650_DUMP_MAX_REG + 3]);
		if (rc) {
			chg_err("Couldn't  read 0x2D rc = %d\n", rc);
		}
		rc = mp2650_read_reg(0x30, &val_buf[MP2650_DUMP_MAX_REG + 4]);
		if (rc) {
			chg_err("Couldn't  read 0x30 rc = %d\n", rc);
		}

        printk(KERN_ERR "mp2650_dump_reg: [0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		"[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		"[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		"[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		"[0x%02x, 0x%02x, 0x%02x], [reg48=0x%02x, reg49=0x%02x, reg2D=0x%02x, reg30=0x%02x]\n",
		val_buf[0], val_buf[1], val_buf[2], val_buf[3],
		val_buf[4], val_buf[5], val_buf[6], val_buf[7],
		val_buf[8], val_buf[9], val_buf[10], val_buf[11],
		val_buf[12], val_buf[13], val_buf[14], val_buf[15],
		val_buf[16], val_buf[17], val_buf[18], val_buf[19],
		val_buf[20], val_buf[21], val_buf[22], val_buf[23],
		val_buf[24], val_buf[25], val_buf[26], val_buf[27],
		val_buf[28], val_buf[29], val_buf[30], val_buf[31],
		val_buf[32], val_buf[33], val_buf[34],
		val_buf[35], val_buf[36], val_buf[37], val_buf[38]);
    }
    dump_count++;
}
bool mp2650_need_to_check_ibatt(void)
{
    return false;
}


int mp2650_get_chg_current_step(void)
{
    int rc = 50;

    return rc;
}

int mp2650_input_current_limit_init(void)
{
    int rc = 0;
    struct chip_mp2650 *chip = charger_ic;

    if (atomic_read(&chip->charger_suspended) == 1) {
        chg_err("mp2650_input_current_limit_init: in suspended\n");
        return 0;
    }
    rc = mp2650_config_interface(REG00_MP2650_ADDRESS, 0x03, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
    rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, 0x03, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	
    if (rc < 0) {
        chg_err("Couldn't mp2650_input_current_limit_init rc = %d\n", rc);
    }

    return rc;
}

int mp2650_input_current_limit_without_aicl(int current_ma)
{
    int rc = 0;
    int reg_val = 0;
    struct chip_mp2650 *chip = charger_ic;

    if (atomic_read(&chip->charger_suspended) == 1) {
        chg_err("mp2650_input_current_limit_init: in suspended\n");
        return 0;
    }
    chip->pre_current_ma = current_ma;

    reg_val = current_ma / REG00_MP2650_1ST_CURRENT_LIMIT_STEP;
    chg_err(" reg_val current [%d]ma\n", current_ma);
    rc = mp2650_config_interface(REG00_MP2650_ADDRESS, reg_val, REG00_MP2650_1ST_CURRENT_LIMIT_MASK);
    rc = mp2650_config_interface(REG0F_MP2650_ADDRESS, reg_val, REG0F_MP2650_2ND_CURRENT_LIMIT_MASK);
	
    if (rc < 0) {
        chg_err("Couldn't mp2650_input_current_limit_init rc = %d\n", rc);
    }

    return rc;
}


int mp2650_set_voltage_slew_rate(int value)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("mp2650_set_voltage_slew_rate: in suspended\n");
		return 0;
	}
	if (value == 1)
		rc = mp2650_config_interface(0x2D, 0x0E, BIT(3)|BIT(2)|BIT(1));
	else if (value == 2)
		rc = mp2650_config_interface(0x2D, 0x06, BIT(3)|BIT(2)|BIT(1));
	if (rc < 0) {
		chg_err("Couldn't mp2650_input_current_limit_init rc = %d\n", rc);
	}
	return 0;
}

static int mp2650_set_option(int option)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("mp2650_set_voltage_slew_rate: in suspended\n");
		return 0;
	}
	if (option == 1)
		rc = mp2650_config_interface(REG30_MP2650_ADDRESS, 0, REG30_MP2650_OPTION_MASK);
	else if (option == 2)
		rc = mp2650_config_interface(REG30_MP2650_ADDRESS, 1, REG30_MP2650_OPTION_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_set_option rc = %d\n", rc);
	}
	return 0;
}

int mp2650_hardware_init(void)
{
	struct chip_mp2650 *chip = charger_ic;
	int gpio_otg_value = 0;

	chg_err("init mp2650 hardware! \n");

	chip->hw_aicl_point = 4440;
	chip->sw_aicl_point = 4500;
	gpio_otg_value = mp2650_wireless_get_mps_otg_en_val();
	if (gpio_otg_value == MP2650_GPIO_OTG_EN
		&& chip->probe_flag == false) {
		chg_err("otg already enabled, do not init mp2650! \n");
		return 0;
	}
	mp2650_reset_charger();

	mp2650_disable_charging();

	mp2650_set_chging_term_disable();

	mp2650_input_current_limit_init();

	mp2650_float_voltage_write(WPC_TERMINATION_VOLTAGE);

	mp2650_otg_ilim_set(REG07_MP2650_OTG_CURRENT_LIMIT_1250MA);

	mp2650_set_enable_volatile_writes();

	mp2650_set_complete_charge_timeout(OVERTIME_DISABLED);

	mp2650_set_option(1);

	mp2650_set_prechg_voltage_threshold();

	mp2650_set_prechg_current(WPC_PRECHARGE_CURRENT);

	mp2650_charging_current_write_fast(WPC_CHARGE_CURRENT_DEFAULT);


	mp2650_set_termchg_current(WPC_TERMINATION_CURRENT);

	mp2650_set_rechg_voltage(WPC_RECHARGE_VOLTAGE_OFFSET);

	mp2650_set_switching_frequency();

	mp2650_set_prochot_psys_cfg();

	mp2650_set_vindpm_vol(chip->hw_aicl_point);

	mp2650_set_mps_otg_current();

	mp2650_set_mps_otg_voltage(false);
	mp2650_set_mps_second_otg_voltage(false);

	mp2650_set_mps_otg_enable();

	mp2650_other_registers_init();

	mp2650_unsuspend_charger();

	mp2650_enable_charging();

	mp2650_set_wdt_timer(REG09_MP2650_WTD_TIMER_40S);

	mp2650_input_current_limit_without_aicl(500);

	return true;
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
    struct rtc_time tm;
    struct rtc_device *rtc;
    int rc = 0;

    rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
    if (rtc == NULL) {
        pr_err("%s: unable to open rtc device (%s)\n",
        __FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
        return 0;
    }

    rc = rtc_read_time(rtc, &tm);
    if (rc) {
        pr_err("Error reading rtc device (%s) : %d\n",
        CONFIG_RTC_HCTOSYS_DEVICE, rc);
        goto close_time;
    }

    rc = rtc_valid_tm(&tm);
    if (rc) {
        pr_err("Invalid RTC time (%s): %d\n",
        CONFIG_RTC_HCTOSYS_DEVICE, rc);
        goto close_time;
    }

    if ((tm.tm_year == 110) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
        chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
        tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
        tm.tm_wday, tm.tm_yday, tm.tm_isdst);
        rtc_class_close(rtc);
        return 1;
    }

    chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
    tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
    tm.tm_wday, tm.tm_yday, tm.tm_isdst);

    close_time:
        rtc_class_close(rtc);
    return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

void __attribute__((weak)) oplus_set_typec_sinkonly(void)
{
	return;
}

void __attribute__((weak)) oplus_get_usbtemp_volt(struct oplus_chg_chip *chip)
{
	return;
}

bool __attribute__((weak)) oplus_usbtemp_condition(void)
{
	return false;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#ifdef CONFIG_OPLUS_CHARGER_MTK
int __attribute__((weak)) get_boot_reason(void)
{
	return 0;
}
#endif
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK
/*modify for cfi*/
static int oplus_get_boot_mode(void)
{
	return (int)get_boot_mode();
}

static int oplus_get_boot_reason(void)
{
	return (int)get_boot_reason();
}
#endif

struct oplus_chg_operations  mp2650_chg_ops = {
    .get_charger_cycle = oplus_get_charger_cycle,
    .dump_registers = mp2650_dump_registers,
    .kick_wdt = mp2650_kick_wdt,
    .hardware_init = mp2650_hardware_init,
    .charging_current_write_fast = mp2650_charging_current_write_fast,
    .set_aicl_point = mp2650_set_aicl_point,
    .input_current_write = mp2650_input_current_limit_write,
    .input_current_ctrl_by_vooc_write = mp2650_input_current_limit_ctrl_by_vooc_write,
    .float_voltage_write = mp2650_float_voltage_write,
    .term_current_set = mp2650_set_termchg_current,
    .charging_enable = mp2650_enable_charging,
    .charging_disable = mp2650_disable_charging,
    .get_charging_enable = mp2650_check_charging_enable,
    .charger_suspend = mp2650_suspend_charger,
    .charger_unsuspend = mp2650_unsuspend_charger,
    .charger_suspend_check = mp2650_check_suspend_charger,
    .set_rechg_vol = mp2650_set_rechg_voltage,
    .reset_charger = mp2650_reset_charger,
    .read_full = mp2650_registers_read_full,
    .otg_enable = mp2650_otg_enable,
    .otg_disable = mp2650_otg_disable,
    .set_charging_term_disable = mp2650_set_chging_term_disable,
    .check_charger_resume = mp2650_check_charger_resume,
#ifdef 		CONFIG_OPLUS_CHARGER_MTK
    .get_charger_type = mt_power_supply_type_check,
    .get_charger_volt = mp2650_get_charger_vol,
    .check_chrdet_status = oplus_mt_get_vbus_status,
    .get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
    .get_boot_mode = oplus_get_boot_mode,
    .get_boot_reason = oplus_get_boot_reason,
    .get_chargerid_volt = mt_get_chargerid_volt,
    .set_chargerid_switch_val = mt_set_chargerid_switch_val ,
    .get_chargerid_switch_val  = mt_get_chargerid_switch_val,
    .get_rtc_soc = get_rtc_spare_oplus_fg_value,
    .set_rtc_soc = set_rtc_spare_oplus_fg_value,
    .set_power_off = oplus_mt_power_off,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)) && defined(CONFIG_EXTCON_MTK_USB)
	.usb_connect = mt_usb_connect_v1,
	.usb_disconnect = mt_usb_disconnect_v1,
#else
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
#endif
    .get_charger_current = mp2650_get_ibus_current,
    .check_pdphy_ready = oplus_check_pdphy_ready,
    .get_subboard_temp = oplus_force_get_subboard_temp,
    .check_cc_mode = oplus_check_cc_mode,
#else /* CONFIG_OPLUS_CHARGER_MTK */
    .get_chargerid_volt = smbchg_get_chargerid_volt,
    .set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
    .get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
    .need_to_check_ibatt = mp2650_need_to_check_ibatt,
    .get_chg_current_step = mp2650_get_chg_current_step,
    .get_charger_type = opchg_get_charger_type,
    .get_real_charger_type = opchg_get_real_charger_type,
    .get_charger_volt = mp2650_get_vbus_voltage,
    .get_charger_current = mp2650_get_ibus_current,
    .check_chrdet_status = oplus_chg_is_usb_present,
    .get_instant_vbatt = qpnp_get_battery_voltage,
    .get_boot_mode = get_boot_mode,
    .get_boot_reason = smbchg_get_boot_reason,
    .get_rtc_soc = oplus_chg_get_shutdown_soc,
    .set_rtc_soc = oplus_chg_backup_soc,
#endif /* CONFIG_OPLUS_CHARGER_MTK */
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
    .get_dyna_aicl_result = mp2650_chg_get_dyna_aicl_result,
#endif
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
    .check_rtc_reset = rtc_reset_check,
#endif
    .get_charger_subtype = oplus_chg_get_charger_subtype,
    .oplus_chg_pd_setup = oplus_chg_set_pd_config,
    .oplus_chg_pps_setup = oplus_chg_set_pps_config,
    .oplus_chg_get_pps_status = oplus_chg_get_pps_status,
    .oplus_chg_get_max_cur = oplus_chg_pps_get_max_cur,
    .set_qc_config = oplus_chg_set_qc_config,
#ifdef CONFIG_OPLUS_CHARGER_MTK
    .oplus_chg_get_pd_type = oplus_chg_get_pd_type,
#else
    .oplus_chg_get_pd_type = oplus_sm8150_get_pd_type,
#endif
    .enable_qc_detect = oplus_chg_enable_qc_detect,
    .input_current_write_without_aicl = mp2650_input_current_limit_without_aicl,
    .set_charger_vsys_threshold = mp2650_set_charger_vsys_threshold,
    .enable_burst_mode = mp2650_burst_mode_enable,
    .oplus_chg_wdt_enable = mp2650_wdt_enable,
    .get_usbtemp_volt = oplus_get_usbtemp_volt,
    .set_typec_sinkonly = oplus_set_typec_sinkonly,
    .set_typec_cc_open = oplus_set_typec_cc_open,
    .oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
    .adsp_voocphy_set_match_temp = oplus_adsp_voocphy_set_match_temp,
    .get_props_from_adsp_by_buffer = oplus_get_props_from_adsp_by_buffer,
    .set_enable_volatile_writes = mp2650_set_enable_volatile_writes,
    .set_complete_charge_timeout = mp2650_set_complete_charge_timeout,
    .set_prechg_voltage_threshold = mp2650_set_prechg_voltage_threshold,
    .set_prechg_current = mp2650_set_prechg_current,
    .set_vindpm_vol = mp2650_set_vindpm_vol,
    .disable_buck_switch = mp2650_disable_buck_switch,
    .disable_async_mode = mp2650_disable_async_mode,
    .set_switching_frequency = mp2650_set_switching_frequency,
    .set_mps_otg_current = mp2650_set_mps_otg_current,
    .set_mps_otg_voltage = mp2650_set_mps_otg_voltage,
    .set_mps_second_otg_voltage = mp2650_set_mps_second_otg_voltage,
    .set_wdt_timer = mp2650_set_wdt_timer,
    .set_voltage_slew_rate = mp2650_set_voltage_slew_rate,
    .otg_wait_vbus_decline = mp2650_otg_wait_vbus_decline,
    .set_bcc_curr_to_voocphy = oplus_set_bcc_curr_to_voocphy,
};

struct oplus_chg_operations * oplus_get_chg_ops(void)
{
	chg_err(" oplus_get_chg_ops--mp2650_chg_ops\n");

    return &mp2650_chg_ops;
}

static void register_charger_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
    int ret = 0;
    char *version;
    char *manufacture;

    version = "mp2650";
    manufacture = "MP";

    ret = register_device_proc("charger", version, manufacture);
    if (ret)
        chg_err("register_charger_devinfo fail\n");
#endif
}

bool oplus_charger_ic_chip_is_null(void)
{
    if (!charger_ic) {
        return true;
    } else {
        return false;
    }
}

static int mp2650_mps_otg_en_gpio_init(struct chip_mp2650 *chip)
{

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip_mp2650 not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get mps_otg_en pinctrl fail\n");
		return -EINVAL;
	}

	chip->mps_otg_en_active = pinctrl_lookup_state(chip->pinctrl, "mps_otg_en_active");
	if (IS_ERR_OR_NULL(chip->mps_otg_en_active)) {
		chg_err("get mps_otg_en_active fail\n");
		return -EINVAL;
	}

	chip->mps_otg_en_sleep = pinctrl_lookup_state(chip->pinctrl, "mps_otg_en_sleep");
	if (IS_ERR_OR_NULL(chip->mps_otg_en_sleep)) {
		chg_err("get mps_otg_en fail\n");
		return -EINVAL;
	}

	chip->mps_otg_en_default = pinctrl_lookup_state(chip->pinctrl, "mps_otg_en_default");
	if (IS_ERR_OR_NULL(chip->mps_otg_en_default)) {
		chg_err("get mps_otg_en_default fail\n");
		return -EINVAL;
	}

	//if (chg->otg_en_gpio > 0) {
		//gpio_direction_input(chg->wired_conn_gpio);
	//}

	gpio_direction_output(chip->mps_otg_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->mps_otg_en_sleep);
  	chg_err("gpio_val:%d\n", gpio_get_value(chip->mps_otg_en_gpio));

	return 0;
}

static int mp2650_gpio_init(struct chip_mp2650 *chip)
{
	int rc=0;
	struct device_node *node = chip->dev->of_node;

	// Parsing gpio mps_otg_en
	chip->mps_otg_en_gpio = of_get_named_gpio(node, "qcom,mps_otg_en-gpio", 0);
	if(chip->mps_otg_en_gpio < 0 ) {
		pr_err("chip->mps_otg_en_gpio not specified\n");
	} else {
		if( gpio_is_valid(chip->mps_otg_en_gpio) ) {
			rc = gpio_request(chip->mps_otg_en_gpio, "mps_otg_en-gpio");
			if(rc){
				pr_err("unable to request gpio [%d]\n", chip->mps_otg_en_gpio);
			}
		}
		rc = mp2650_mps_otg_en_gpio_init(chip);
		pr_err("chip->mps_otg_en_gpio =%d\n",chip->mps_otg_en_gpio);
	}

	chg_err(" mp2650_gpio_init FINISH\n");

	return rc;
}

void mp2650_wireless_set_mps_otg_en_val(int value)
{
    struct chip_mp2650 *chip = charger_ic;
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip_mp2650 not ready!\n", __func__);
		return;
	}

	if (chip->mps_otg_en_gpio <= 0) {
		chg_err("mps_otg_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl)
		|| IS_ERR_OR_NULL(chip->mps_otg_en_active)
		|| IS_ERR_OR_NULL(chip->mps_otg_en_sleep)
		|| IS_ERR_OR_NULL(chip->mps_otg_en_default)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(chip->mps_otg_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl,
				chip->mps_otg_en_active);
	} else {
		gpio_direction_output(chip->mps_otg_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl,
				chip->mps_otg_en_default);
	}

	chg_err("<~WPC~>set value:%d, gpio_val:%d\n",
		value, gpio_get_value(chip->mps_otg_en_gpio));
}

int mp2650_wireless_get_mps_otg_en_val(void)
{
	struct chip_mp2650 *chip = charger_ic;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip_mp2650 not ready!\n", __func__);
		return -1;
	}

	if (chip->mps_otg_en_gpio <= 0) {
		chg_err("mps_otg_en_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl)
		|| IS_ERR_OR_NULL(chip->mps_otg_en_active)
		|| IS_ERR_OR_NULL(chip->mps_otg_en_sleep)
		|| IS_ERR_OR_NULL(chip->mps_otg_en_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chip->mps_otg_en_gpio);
}

#ifdef DEBUG_BY_FILE_OPS
static char mp2650_add = 0;

/*echo "xx" > /proc/mp2650_write_log*/
static ssize_t mp2650_data_log_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};
	int critical_log = 0;
	int rc;

	if (len > sizeof(write_data) || len < 1) {
		return -EINVAL;
	}

	if (copy_from_user(&write_data, buff, len)) {
		pr_err("mp2650_data_log_write error.\n");
		return -EFAULT;
	}

	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	critical_log = (int)simple_strtoul(write_data, NULL, 0);
	if (critical_log > 256) {
		critical_log = 256;
	}

	pr_err("%s: input data = %s,  write_mp2650_data = 0x%02X\n", __func__, write_data, critical_log);

	rc = mp2650_config_interface(mp2650_add, critical_log, 0xff);
	if (rc) {
		 chg_err("Couldn't write 0x%02X rc = %d\n", mp2650_add, rc);
	}

	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations mp2650_write_log_proc_fops = {
	.write = mp2650_data_log_write,
};
#else
static const struct proc_ops mp2650_write_log_proc_fops = {
	.proc_write = mp2650_data_log_write,
	.proc_lseek = seq_lseek,
};
#endif

static void init_mp2650_write_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("mp2650_write_log", 0664, NULL, &mp2650_write_log_proc_fops);
	if (!p) {
		pr_err("proc_create mp2650_write_log fail!\n");
	}
}

/*echo "xx" > /proc/mp2650_read_log*/
static ssize_t mp2650_reg_store(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};
	int critical_log = 0;
	int rc;
	int val_buf;

	if (len > sizeof(write_data) || len < 1) {
		return -EINVAL;
	}

	if (copy_from_user(&write_data, buff, len)) {
		pr_err("mp2650_data_log_read error.\n");
		return -EFAULT;
	}

	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	critical_log = (int)simple_strtoul(write_data, NULL, 0);
	if (critical_log > 256) {
		critical_log = 256;
	}

	mp2650_add = critical_log;

	pr_err("%s: input data = %s,  mp2650_addr = 0x%02X\n", __func__, write_data, mp2650_add);

	rc = mp2650_read_reg(mp2650_add, &val_buf);
	if (rc) {
		 chg_err("Couldn't read 0x%02X rc = %d\n", mp2650_add, rc);
	} else {
		 chg_err("mp2650_read 0x%02X = 0x%02X\n", mp2650_add, val_buf);
	}

	return len;
}

static ssize_t mp2650_reg_show(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	int rc;
	int val_buf;
	int len = 0;

	rc = mp2650_read_reg(mp2650_add, &val_buf);
	if (rc) {
		 chg_err("Couldn't read 0x%02X rc = %d\n", mp2650_add, rc);
	}

	len = sprintf(page, "reg = 0x%x, data = 0x%x\n", mp2650_add, val_buf);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations mp2650_read_log_proc_fops = {
	.read = mp2650_reg_show,
	.write = mp2650_reg_store,
};
#else
static const struct proc_ops mp2650_read_log_proc_fops = {
	.proc_read = mp2650_reg_show,
	.proc_write = mp2650_reg_store,
	.proc_lseek = seq_lseek,
};
#endif
static void init_mp2650_read_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("mp2650_read_log", 0664, NULL, &mp2650_read_log_proc_fops);
	if (!p) {
		pr_err("proc_create mp2650_read_log fail!\n");
	}
}
#endif /*DEBUG_BY_FILE_OPS*/

static int mp2650_chg_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static void mp2650_track_i2c_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_mp2650 *chip =
		container_of(dwork, struct chip_mp2650, i2c_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	mutex_lock(&chip->track_i2c_err_lock);
	if (chip->i2c_err_load_trigger) {
		kfree(chip->i2c_err_load_trigger);
		chip->i2c_err_load_trigger = NULL;
	}
	chip->i2c_err_uploading = false;
	mutex_unlock(&chip->track_i2c_err_lock);
}

static int mp2650_track_upload_i2c_err_info(
	struct chip_mp2650 *chip, int err_type, u8 reg)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	static int pre_reg = MP2650_MAX_ADDRESS;

	if (!chip)
		return -EINVAL;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	mutex_lock(&chip->track_upload_lock);
	curr_time = mp2650_chg_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (curr_time - pre_upload_time < TRACK_I2CERROR_SAMEADDR_MIN_INTERVAL
	    && pre_reg == reg) {
		mutex_unlock(&chip->track_upload_lock);
		chg_debug("Recently 3s uploaded reg %02x\n", pre_reg);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		chg_debug("upload_count %d > %d\n", upload_count, TRACK_UPLOAD_COUNT_MAX);
		return 0;
	}

	if (chip->debug_force_i2c_err)
		err_type = -chip->debug_force_i2c_err;

	mutex_lock(&chip->track_i2c_err_lock);
	if (chip->i2c_err_uploading) {
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		chg_debug("i2c_err_uploading, should return\n");
		return 0;
	}

	if (chip->i2c_err_load_trigger)
		kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->i2c_err_load_trigger) {
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		chg_err("i2c_err_load_trigger memery alloc fail\n");
		return -ENOMEM;
	}
	chip->i2c_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->i2c_err_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_EXTERN_PMIC_ABNORMAL;
	chip->i2c_err_uploading = true;
	upload_count++;
	pre_reg = reg;
	pre_upload_time = mp2650_chg_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(
		&(chip->i2c_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"mp2762");
	index += snprintf(
		&(chip->i2c_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		OPLUS_CHG_TRACK_SCENE_I2C_ERR);

	memset(chip->track_temp, 0, sizeof(chip->track_temp));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	oplus_chg_track_get_i2c_err_reason(err_type,
				chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(
		&(chip->i2c_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->track_temp, sizeof(chip->track_temp));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->track_temp);
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$access_reg@@0x%02x", reg);
	schedule_delayed_work(&chip->i2c_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	chg_debug("success reg %02x\n", reg);
	return 0;
}

static void mp2650_chg_track_icl_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_mp2650 *chip =
		container_of(dwork, struct chip_mp2650, icl_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->icl_err_load_trigger));
	mutex_lock(&chip->track_icl_err_lock);
	if (chip->icl_err_load_trigger) {
		kfree(chip->icl_err_load_trigger);
		chip->icl_err_load_trigger = NULL;
	}
	chip->icl_err_uploading = false;
	mutex_unlock(&chip->track_icl_err_lock);
}

static int mp2650_chg_track_upload_icl_err_info(
	struct chip_mp2650 *chip, int err_type)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	mutex_lock(&chip->track_upload_lock);
	curr_time = mp2650_chg_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (err_type != TRACK_PMIC_ERR_ICL_VBUS_COLLAPSE &&
	    err_type != TRACK_PMIC_ERR_ICL_VBUS_LOW_POINT) {
		mutex_unlock(&chip->track_upload_lock);
		return -EINVAL;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		chg_debug("upload_count %d > %d\n",
			upload_count, TRACK_UPLOAD_COUNT_MAX);
		return 0;
	}

	mutex_lock(&chip->track_icl_err_lock);
	if (chip->icl_err_uploading) {
		chg_debug("icl_err_uploading, should return\n");
		mutex_unlock(&chip->track_icl_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->icl_err_load_trigger)
		kfree(chip->icl_err_load_trigger);
	chip->icl_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->icl_err_load_trigger) {
		chg_err("icl_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_icl_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->icl_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->icl_err_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_EXTERN_PMIC_ABNORMAL;
	chip->icl_err_uploading = true;
	upload_count++;
	pre_upload_time = mp2650_chg_track_get_local_time_s();
	mutex_unlock(&chip->track_icl_err_lock);

	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	index += snprintf(
		&(chip->icl_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"mp2762");
	index += snprintf(
		&(chip->icl_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		OPLUS_CHG_TRACK_SCENE_PMIC_ICL_ERR);

	oplus_chg_track_get_pmic_err_reason(err_type,
				chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(
		&(chip->icl_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$err_reason@@%s", chip->err_reason);

	memset(chip->track_temp, 0, sizeof(chip->track_temp));
	oplus_chg_track_obtain_power_info(chip->track_temp, sizeof(chip->track_temp));
	index += snprintf(&(chip->icl_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->track_temp);

	index += snprintf(&(chip->icl_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$reg_info@@%s", chip->track_temp);
	schedule_delayed_work(&chip->icl_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	chg_debug("success\n");

	return 0;
}


static int mp2650_chg_track_debugfs_init(struct chip_mp2650 *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_mp2650;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_mp2650 = debugfs_create_dir("mp2762", debugfs_root);
	if (!debugfs_mp2650) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_create_u32("debug_force_icl_err", 0644,
	    debugfs_mp2650, &(chip->debug_force_icl_err));

	debugfs_create_u32("debug_force_i2c_err", 0644,
	     debugfs_mp2650, &(chip->debug_force_i2c_err));

	return ret;
}

static int mp2650_chg_track_init(struct chip_mp2650 *chip)
{
	int rc;

	if (!chip)
		return - EINVAL;

	mutex_init(&chip->track_icl_err_lock);
	mutex_init(&chip->track_upload_lock);
	mutex_init(&chip->track_i2c_err_lock);
	chip->icl_err_uploading = false;
	chip->icl_err_load_trigger = NULL;

	chip->i2c_err_uploading = false;
	chip->i2c_err_load_trigger = NULL;

	chip->debug_force_icl_err = 0;
	chip->debug_force_i2c_err = 0;

	INIT_DELAYED_WORK(&chip->icl_err_load_trigger_work,
	    mp2650_chg_track_icl_err_load_trigger_work);

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work,
				mp2650_track_i2c_err_load_trigger_work);

	rc = mp2650_chg_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("mp2762 debugfs init error, rc=%d\n", rc);
	}

	chip->track_init_done = true;
	return rc;
}

static int mp2650_driver_probe(struct i2c_client *client, const struct i2c_device_id *id) 
{
	int ret = 0;
	struct chip_mp2650 *chg_ic;

	chg_ic = devm_kzalloc(&client->dev,
		sizeof(struct chip_mp2650), GFP_KERNEL);
	if (!chg_ic) {
		chg_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	chg_debug( " call \n");
	chg_ic->client = client;
	chg_ic->dev = &client->dev;

	charger_ic = chg_ic;
	atomic_set(&chg_ic->charger_suspended, 0);
	chg_ic->track_init_done = false;
	mp2650_chg_track_init(chg_ic);
	mp2650_dump_registers();
	mp2650_vbus_avoid_electric_config();
	chg_ic->probe_flag = true;
	mp2650_hardware_init();
	mp2650_gpio_init(chg_ic);

	register_charger_devinfo();
	ret = sysfs_create_group(&chg_ic->dev->kobj, &mp2650_attribute_group);
	if (ret < 0) {
		chg_debug(" sysfs_create_group error fail\n");
        	/*return ret;*/
	}

#ifdef DEBUG_BY_FILE_OPS
	init_mp2650_write_log();
	init_mp2650_read_log();
#endif

	chg_debug(" success\n");

	return ret;
}


static struct i2c_driver mp2650_i2c_driver;

static int mp2650_driver_remove(struct i2c_client *client)
{

    int ret=0;

    //ret = i2c_del_driver(&mp2650_i2c_driver);
    chg_debug( "  ret = %d\n", ret);
    return 0;
}

static unsigned long suspend_tm_sec = 0;
static int get_current_time(unsigned long *now_tm_sec)
{
    struct rtc_time tm;
    struct rtc_device *rtc = NULL;
    int rc = 0;

    rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
    if (rtc == NULL) {
        chg_err("%s: unable to open rtc device (%s)\n",
        __FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
        return -EINVAL;
    }

    rc = rtc_read_time(rtc, &tm);
    if (rc) {
        chg_err("Error reading rtc device (%s) : %d\n",
        CONFIG_RTC_HCTOSYS_DEVICE, rc);
        goto close_time;
    }

    rc = rtc_valid_tm(&tm);
    if (rc) {
        chg_err("Invalid RTC time (%s): %d\n",
        CONFIG_RTC_HCTOSYS_DEVICE, rc);
        goto close_time;
    }
    rtc_tm_to_time(&tm, now_tm_sec);

    close_time:
        rtc_class_close(rtc);
    return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int mp2650_pm_resume(struct device *dev)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int ui_soc, real_soc;
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic;

	if(!chip) {
		return 0;
	}
	atomic_set(&chip->charger_suspended, 0);
	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	ui_soc = oplus_chg_get_ui_soc();
	real_soc = oplus_chg_get_soc();
	if (abs(ui_soc - real_soc) > URGENT_CHECK_THRESHOLD ||
	    sleep_time > NORMAL_CHECK_UPDATE_INTERVAL)
		oplus_chg_soc_update_when_resume(sleep_time);
	return 0;
}

static int mp2650_pm_suspend(struct device *dev)
{
    struct chip_mp2650 *chip = charger_ic;

    if(!chip) {
        return 0;
    }
    atomic_set(&chip->charger_suspended, 1);
    if (get_current_time(&suspend_tm_sec)) {
        chg_err("RTC read failed\n");
        suspend_tm_sec = -1;
    }
    return 0;

}

static const struct dev_pm_ops mp2650_pm_ops = {
	.resume		= mp2650_pm_resume,
	.suspend		= mp2650_pm_suspend,
};
#else
static int mp2650_resume(struct i2c_client *client)
{
    unsigned long resume_tm_sec = 0;
    unsigned long sleep_time = 0;
    int rc = 0;
    struct chip_mp2650 *chip = charger_ic;

    if(!chip) {
        return 0;
    }
    atomic_set(&chip->charger_suspended, 0);
    rc = get_current_time(&resume_tm_sec);
    if (rc || suspend_tm_sec == -1) {
        chg_err("RTC read failed\n");
        sleep_time = 0;
    } else {
        sleep_time = resume_tm_sec - suspend_tm_sec;
    }
    /*
    if(sleep_time < 0) {
    sleep_time = 0;
    }
    */	
    oplus_chg_soc_update_when_resume(sleep_time);
    return 0;
}

static int mp2650_suspend(struct i2c_client *client, pm_message_t mesg)
{
    struct chip_mp2650 *chip = charger_ic;

    if(!chip) {
        return 0;
    }
    atomic_set(&chip->charger_suspended, 1);
    if (get_current_time(&suspend_tm_sec)) {
        chg_err("RTC read failed\n");
        suspend_tm_sec = -1;
    }
    return 0;
}
#endif

static void mp2650_reset(struct i2c_client *client)
{
	mp2650_otg_disable();
	mp2650_unsuspend_charger();
}


/**********************************************************
  *
  *   [platform_driver API] 
  *
  *********************************************************/

static const struct of_device_id mp2650_match[] = {
	{ .compatible = "oplus,mp2650-charger"},
	{ },
};

static const struct i2c_device_id mp2650_id[] = {
	{"mp2650-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mp2650_id);


static struct i2c_driver mp2650_i2c_driver = {
	.driver		= {
		.name = "mp2650-charger",
		.owner	= THIS_MODULE,
		.of_match_table = mp2650_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &mp2650_pm_ops,
#endif
	},
	.probe		= mp2650_driver_probe,
	.remove		= mp2650_driver_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= mp2650_resume,
	.suspend	= mp2650_suspend,
#endif
	.shutdown	= mp2650_reset,
	.id_table	= mp2650_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(mp2650_i2c_driver);
#else
int mp2650_driver_init(void)
{
	int ret = 0;

	chg_debug(" start\n");

	oplus_chg_ops_register("ext-mp2650", &mp2650_chg_ops);

	if (i2c_add_driver(&mp2650_i2c_driver) != 0) {
		chg_err(" failed to register mp2650 i2c driver.\n");
	} else {
		chg_debug( " Success to register mp2650 i2c driver.\n");
	}

	return ret;
}

void mp2650_driver_exit(void)
{
	i2c_del_driver(&mp2650_i2c_driver);
}

#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/
MODULE_DESCRIPTION("Driver for mp2650 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:mp2650-charger");
