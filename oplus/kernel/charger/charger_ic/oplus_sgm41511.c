// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/power_supply.h>
#include <linux/iio/consumer.h>

#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_short.h"
#include "../oplus_wireless.h"
#include "../oplus_adapter.h"
#include "../charger_ic/oplus_short_ic.h"
#include "../gauge_ic/oplus_bq27541.h"
#include <soc/oplus/system/boot_mode.h>
//#include "../oplus_chg_module.h"
#include "../oplus_chg_ops_manager.h"
#include "../voocphy/oplus_voocphy.h"
#include "oplus_discrete_charger.h"
#include "oplus_sgm41511.h"

extern bool oplus_get_otg_online_status_default(void);
extern int qpnp_get_battery_voltage(void);
extern int opchg_get_real_charger_type(void);
extern int smbchg_get_chargerid_switch_val(void);
extern void smbchg_set_chargerid_switch_val(int value);
extern int smbchg_get_chargerid_volt(void);
extern int smbchg_get_boot_reason(void);
extern int oplus_chg_get_shutdown_soc(void);
extern int oplus_chg_backup_soc(int backup_soc);
extern int oplus_chg_get_charger_subtype(void);
extern int oplus_chg_set_pd_config(void);
extern int oplus_chg_set_qc_config(void);
extern int oplus_sm8150_get_pd_type(void);
extern int oplus_chg_enable_qc_detect(void);
extern void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip);
extern void oplus_set_typec_sinkonly(void);
extern bool oplus_usbtemp_condition(void);
extern int opchg_get_charger_type(void);
extern void oplus_set_pd_active(int active);
extern void oplus_set_usb_props_type(enum power_supply_type type);
extern int oplus_get_adapter_svid(void);
extern void oplus_wake_up_usbtemp_thread(void);
extern int qpnp_get_prop_charger_voltage_now(void);
extern int qpnp_get_prop_ibus_now(void);

extern struct oplus_chg_chip *g_oplus_chip;

struct chip_sgm41511 {
	struct device		*dev;
	struct i2c_client	*client;
	int			irq_gpio;
	int			enable_gpio;
	struct pinctrl 		*pinctrl;
	struct pinctrl_state 	*enable_gpio_active;
	struct pinctrl_state 	*enable_gpio_sleep;
	enum power_supply_type	oplus_charger_type;
	atomic_t		charger_suspended;
	atomic_t		is_suspended;
	struct mutex		i2c_lock;
	struct mutex		dpdm_lock;
	struct regulator	*dpdm_reg;
	bool			dpdm_enabled;
	bool			power_good;
	bool			is_sgm41511;
	bool			is_bq25601d;
	bool			bc12_done;
	char			bc12_delay_cnt;
	char			bc12_retried;
	int			hw_aicl_point;
	int			sw_aicl_point;
	int			reg_access;
	int			before_suspend_icl;
	int			before_unsuspend_icl;
	struct delayed_work	bc12_retry_work;
	int		set_ovp_value;
};

static struct chip_sgm41511 *charger_ic = NULL;
static int aicl_result = 500;
#define OPLUS_BC12_RETRY_CNT 1
#define OPLUS_BC12_DELAY_CNT 18

#define SGM41511_OVP_5500MV		5500
#define SGM41511_OVP_6500MV		6500
#define SGM41511_OVP_10500MV	10500
#define SGM41511_OVP_14000MV	14000

static int sgm41511_request_dpdm(struct chip_sgm41511 *chip, bool enable);
static void sgm41511_get_bc12(struct chip_sgm41511 *chip);

#define I2C_RETRY_DELAY_US	5000
#define I2C_RETRY_MAX_COUNT	3
static int __sgm41511_read_reg(struct chip_sgm41511 *chip, int reg, int *data)
{
	s32 ret = 0;
	int retry = I2C_RETRY_MAX_COUNT;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		while(retry > 0) {
			usleep_range(I2C_RETRY_DELAY_US, I2C_RETRY_DELAY_US);
			ret = i2c_smbus_read_byte_data(chip->client, reg);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*data = ret;
	}

	return 0;
}

static int __sgm41511_write_reg(struct chip_sgm41511 *chip, int reg, int val)
{
	s32 ret = 0;
	int retry = I2C_RETRY_MAX_COUNT;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		while(retry > 0) {
			usleep_range(I2C_RETRY_DELAY_US, I2C_RETRY_DELAY_US);
			ret = i2c_smbus_write_byte_data(chip->client, reg, val);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		chg_err("i2c write fail: can't write %02x to %02x: %d\n", val, reg, ret);
		return ret;
	}

	return 0;
}

static int sgm41511_read_reg(struct chip_sgm41511 *chip, int reg, int *data)
{
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41511_read_reg(chip, reg, data);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static __maybe_unused int sgm41511_write_reg(struct chip_sgm41511 *chip, int reg, int data)
{
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41511_write_reg(chip, reg, data);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static __maybe_unused int sgm41511_config_interface(struct chip_sgm41511 *chip, int reg, int data, int mask)
{
	int ret;
	int tmp;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41511_read_reg(chip, reg, &tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sgm41511_write_reg(chip, reg, tmp);
	if (ret)
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

int sgm41511_set_vindpm_vol(int vol)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41511 *chip = charger_ic;
	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	tmp = (vol - REG06_SGM41511_VINDPM_OFFSET) / REG06_SGM41511_VINDPM_STEP_MV;
	rc = sgm41511_config_interface(chip, REG06_SGM41511_ADDRESS,
					tmp << REG06_SGM41511_VINDPM_SHIFT,
					REG06_SGM41511_VINDPM_MASK);

	return rc;
}

int sgm41511_usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

static int sgm41511_get_usb_icl(void)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	rc = sgm41511_read_reg(chip, REG00_SGM41511_ADDRESS, &tmp);
	if (rc) {
		chg_err("Couldn't read REG00_SGM41511_ADDRESS rc = %d\n", rc);
		return 0;
	}
	tmp = (tmp & REG00_SGM41511_INPUT_CURRENT_LIMIT_MASK) >> REG00_SGM41511_INPUT_CURRENT_LIMIT_SHIFT;
	return (tmp * REG00_SGM41511_INPUT_CURRENT_LIMIT_STEP + REG00_SGM41511_INPUT_CURRENT_LIMIT_OFFSET);
}

int sgm41511_input_current_limit_without_aicl(int current_ma)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("in suspend\n");
		return 0;
	}

	tmp = (current_ma - REG00_SGM41511_INPUT_CURRENT_LIMIT_OFFSET) / REG00_SGM41511_INPUT_CURRENT_LIMIT_STEP;
	chg_err("tmp current [%d]ma\n", current_ma);
	rc = sgm41511_config_interface(chip, REG00_SGM41511_ADDRESS,
					tmp << REG00_SGM41511_INPUT_CURRENT_LIMIT_SHIFT,
					REG00_SGM41511_INPUT_CURRENT_LIMIT_MASK);

	if (rc < 0) {
		chg_err("Couldn't set aicl rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_chg_get_dyna_aicl_result(void)
{
	return aicl_result;
}

#define AICL_POINT_VOL_5V_HIGH		4300
#define AICL_POINT_VOL_5V_LOW		4140
#define HW_AICL_POINT_VOL_5V_PHASE1	4400
#define HW_AICL_POINT_VOL_5V_PHASE2	4500
#define HW_AICL_POINT_VOL_5V_PHASE3	4600
#define HW_AICL_POINT_VOL_5V_CHECK	4250
#define SW_AICL_POINT_VOL_5V_PHASE1	4500
#define SW_AICL_POINT_VOL_5V_PHASE2	4535
void sgm41511_set_aicl_point(int vbatt)
{
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return;

	if (vbatt > AICL_POINT_VOL_5V_HIGH) {
		if (chip->hw_aicl_point != HW_AICL_POINT_VOL_5V_PHASE3) {
			chip->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE3;
			chip->sw_aicl_point = SW_AICL_POINT_VOL_5V_PHASE2;
		}
	} else if (vbatt > AICL_POINT_VOL_5V_LOW) {
		if (chip->hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE3 && vbatt < HW_AICL_POINT_VOL_5V_CHECK) {
			chip->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE2;
			chip->sw_aicl_point = SW_AICL_POINT_VOL_5V_PHASE2;
		} else if (chip->hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE1) {
			chip->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE2;
			chip->sw_aicl_point = SW_AICL_POINT_VOL_5V_PHASE2;
		}
	} else {
		if (chip->hw_aicl_point != HW_AICL_POINT_VOL_5V_PHASE1) {
			chip->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
			chip->sw_aicl_point = SW_AICL_POINT_VOL_5V_PHASE1;
		}
	}

	sgm41511_set_vindpm_vol(chip->hw_aicl_point);
}

int sgm41511_get_charger_vol(void)
{
	return qpnp_get_prop_charger_voltage_now();
}

int sgm41511_get_vbus_voltage(void)
{
	return qpnp_get_prop_charger_voltage_now();
}

#define AICL_DOWN_DELAY_MS	50
#define AICL_DELAY_MIN_US	90000
#define AICL_DELAY_MAX_US	91000
#define SUSPEND_IBUS_MA		100
#define DEFAULT_IBUS_MA		500
int sgm41511_input_current_limit_write(int current_ma)
{
	int i = 0, rc = 0;
	int chg_vol = 0;
	int sw_aicl_point = 0;
	struct chip_sgm41511 *chip = charger_ic;
	int pre_icl_index = 0, pre_icl = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (atomic_read(&chip->is_suspended) == 1) {
		chg_err("suspend,ignore set current=%dmA\n", current_ma);
		return 0;
	}

	/*first: icl down to 500mA, step from pre icl*/
	pre_icl = sgm41511_get_usb_icl();
	for (pre_icl_index = ARRAY_SIZE(sgm41511_usb_icl) - 1; pre_icl_index >= 0; pre_icl_index--) {
		if (sgm41511_usb_icl[pre_icl_index] < pre_icl) {
			break;
		}
	}
	chg_err("icl_set: %d, pre_icl: %d, pre_icl_index: %d\n", current_ma, pre_icl, pre_icl_index);

	for (i = pre_icl_index; i > 1; i--) {
		rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
		if (rc) {
			chg_err("icl_down: set icl to %d mA fail, rc=%d\n", sgm41511_usb_icl[i], rc);
		} else {
			chg_err("icl_down: set icl to %d mA\n", sgm41511_usb_icl[i]);
		}
		msleep(AICL_DOWN_DELAY_MS);
	}

	/*second: aicl process, step from 500ma*/
	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	sw_aicl_point = chip->sw_aicl_point;

	i = 1; /* 500 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		chg_debug( "use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (current_ma < 1350) {
		i = i - 1; //We use 1.2A here
		goto aicl_end;
	}

	i = 5; /* 1500 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; //We use 1.2A here
		goto aicl_end;
	} else if (current_ma < 2000) {
		goto aicl_end;
	}

	i = 6; /* 1750 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3; //1.2
		goto aicl_pre_step;
	}

	i = 7; /* 2000 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; //1.5
		goto aicl_pre_step;
	} else if (current_ma < 3000) {
		goto aicl_end;
	}

	i = 8; /* 3000 */
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	usleep_range(AICL_DELAY_MIN_US, AICL_DELAY_MAX_US);
	chg_vol = sgm41511_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i -1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000) {
		goto aicl_end;
	}

aicl_pre_step:
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, sgm41511_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_end:
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, sgm41511_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_rerun:
	aicl_result = sgm41511_usb_icl[i];
	rc = sgm41511_input_current_limit_without_aicl(sgm41511_usb_icl[i]);
	rc = sgm41511_set_vindpm_vol(chip->hw_aicl_point);
	return rc;
}

#define VOOC_AICL_STEP_MA	500
#define VOOC_AICL_DELAY_MS	35
int sgm41511_input_current_limit_ctrl_by_vooc_write(int current_ma)
{
	int rc = 0;
	int cur_usb_icl  = 0;
	int temp_curr = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (atomic_read(&chip->is_suspended) == 1) {
		chg_err("suspend,ignore set current=%dmA\n", current_ma);
		return 0;
	}

	cur_usb_icl = sgm41511_get_usb_icl();
	chg_err(" get cur_usb_icl = %d\n", cur_usb_icl);

	if (current_ma > cur_usb_icl) {
		for (temp_curr = cur_usb_icl; temp_curr < current_ma; temp_curr += VOOC_AICL_STEP_MA) {
			msleep(VOOC_AICL_DELAY_MS);
			rc = sgm41511_input_current_limit_without_aicl(temp_curr);
			chg_err("[up] set input_current = %d\n", temp_curr);
		}
	} else {
		for (temp_curr = cur_usb_icl; temp_curr > current_ma; temp_curr -= VOOC_AICL_STEP_MA) {
			msleep(VOOC_AICL_DELAY_MS);
			rc = sgm41511_input_current_limit_without_aicl(temp_curr);
			chg_err("[down] set input_current = %d\n", temp_curr);
		}
	}

	rc = sgm41511_input_current_limit_without_aicl(current_ma);
	return rc;
}

static ssize_t sgm41511_access_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct chip_sgm41511 *chip = dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}
	return sprintf(buf, "0x%02x\n", chip->reg_access);
}

static ssize_t sgm41511_access_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct chip_sgm41511 *chip = dev_get_drvdata(dev);
	int ret = 0;
	char *pvalue = NULL, *addr, *val;
	unsigned int reg_value = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	if (buf != NULL && size != 0) {
		pr_info("[%s] buf is %s and size is %zu\n", __func__, buf, size);

		pvalue = (char *)buf;
		if (size > 3) {
			addr = strsep(&pvalue, " ");
			ret = kstrtou32(addr, 16, (unsigned int *)&chip->reg_access);
		} else
			ret = kstrtou32(pvalue, 16, (unsigned int *)&chip->reg_access);

		if (size > 3) {
			val = strsep(&pvalue, " ");
			ret = kstrtou32(val, 16, (unsigned int *)&reg_value);
			pr_err("[%s] write sgm41511 reg 0x%02x with value 0x%02x !\n",
				__func__, (unsigned int) chip->reg_access, reg_value);
			ret = sgm41511_write_reg(chip, chip->reg_access, reg_value);
		} else {
			ret = sgm41511_read_reg(chip, chip->reg_access, &reg_value);
			pr_err("[%s] read sgm41511 reg 0x%02x with value 0x%02x !\n",
				__func__, (unsigned int) chip->reg_access, reg_value);
		}
	}
	return size;
}

static DEVICE_ATTR(sgm41511_access, 0660, sgm41511_access_show, sgm41511_access_store);

void sgm41511_dump_registers(void)
{
	int ret = 0;
	int addr = 0;
	int val_buf[SGM41511_REG_NUMBER] = {0x0};
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return;
	}

	for(addr = SGM41511_FIRST_REG; addr <= SGM41511_LAST_REG; addr++) {
		ret = sgm41511_read_reg(chip, addr, &val_buf[addr]);
		if (ret) {
			chg_err("Couldn't read 0x%02x ret = %d\n", addr, ret);
		}
	}

	chg_err("[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		"[0x%02x, 0x%02x, 0x%02x, 0x%02x]\n",
		val_buf[0], val_buf[1], val_buf[2], val_buf[3],
		val_buf[4], val_buf[5], val_buf[6], val_buf[7],
		val_buf[8], val_buf[9], val_buf[10], val_buf[11]);
}

int sgm41511_kick_wdt(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG01_SGM41511_ADDRESS,
					REG01_SGM41511_WDT_TIMER_RESET,
					REG01_SGM41511_WDT_TIMER_RESET_MASK);
	if (rc) {
		chg_err("Couldn't sgm41511 kick wdt rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_set_wdt_timer(int reg)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG05_SGM41511_ADDRESS,
					reg,
					REG05_SGM41511_WATCHDOG_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return 0;
}

static void sgm41511_wdt_enable(bool wdt_enable)
{
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return;

	if (wdt_enable)
		sgm41511_set_wdt_timer(REG05_SGM41511_WATCHDOG_TIMER_40S);
	else
		sgm41511_set_wdt_timer(REG05_SGM41511_WATCHDOG_TIMER_DISABLE);

	chg_err("sgm41511_wdt_enable[%d]\n", wdt_enable);
}

int sgm41511_set_stat_dis(bool enable)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG00_SGM41511_ADDRESS,
					enable ? REG00_SGM41511_STAT_DIS_ENABLE : REG00_SGM41511_STAT_DIS_DISABLE,
					REG00_SGM41511_STAT_DIS_MASK);
	if (rc) {
		chg_err("Couldn't sgm41511 set_stat_dis rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_set_int_mask(int val)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG0A_SGM41511_ADDRESS,
					val,
					REG0A_SGM41511_VINDPM_INT_MASK | REG0A_SGM41511_IINDPM_INT_MASK);
	if (rc) {
		chg_err("Couldn't sgm41511 set_int_mask rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_set_chg_timer(bool enable)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG05_SGM41511_ADDRESS,
					enable ? REG05_SGM41511_CHG_SAFETY_TIMER_ENABLE : REG05_SGM41511_CHG_SAFETY_TIMER_DISABLE,
					REG05_SGM41511_CHG_SAFETY_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't sgm41511 set_chg_timer rc = %d\n", rc);
	}

	return rc;
}

bool sgm41511_get_bus_gd(void)
{
	int rc = 0;
	int reg_val = 0;
	bool bus_gd = false;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_read_reg(chip, REG0A_SGM41511_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read regeister, rc = %d\n", rc);
		return false;
	}

	bus_gd = ((reg_val & REG0A_SGM41511_BUS_GD_MASK) == REG0A_SGM41511_BUS_GD_YES) ? 1 : 0;
	return bus_gd;
}

bool sgm41511_get_power_gd(void)
{
	int rc = 0;
	int reg_val = 0;
	bool power_gd = false;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_read_reg(chip, REG08_SGM41511_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't get_power_gd rc = %d\n", rc);
		return false;
	}

	power_gd = ((reg_val & REG08_SGM41511_POWER_GOOD_STAT_MASK) == REG08_SGM41511_POWER_GOOD_STAT_GOOD) ? 1 : 0;
	return power_gd;
}

static bool sgm41511_chg_is_usb_present(void)
{
	if (oplus_get_otg_online_status_default()) {
		chg_err("otg,return false");
		return false;
	}

	return sgm41511_get_bus_gd();
}

int sgm41511_charging_current_write_fast(int chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("set charge current = %d\n", chg_cur);

	tmp = chg_cur - REG02_SGM41511_FAST_CHG_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG02_SGM41511_FAST_CHG_CURRENT_LIMIT_STEP;

	rc = sgm41511_config_interface(chip, REG02_SGM41511_ADDRESS,
					tmp << REG02_SGM41511_FAST_CHG_CURRENT_LIMIT_SHIFT,
					REG02_SGM41511_FAST_CHG_CURRENT_LIMIT_MASK);

	return rc;
}

int sgm41511_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("vfloat_mv = %d\n", vfloat_mv);

	if (chip->is_bq25601d) {
		tmp = vfloat_mv - REG04_BQ25601D_CHG_VOL_LIMIT_OFFSET;
	} else {
		tmp = vfloat_mv - REG04_SGM41511_CHG_VOL_LIMIT_OFFSET;
	}
	tmp = tmp / REG04_SGM41511_CHG_VOL_LIMIT_STEP;

	rc = sgm41511_config_interface(chip, REG04_SGM41511_ADDRESS,
					tmp << REG04_SGM41511_CHG_VOL_LIMIT_SHIFT,
					REG04_SGM41511_CHG_VOL_LIMIT_MASK);

	return rc;
}

int sgm41511_set_termchg_current(int term_curr)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("term_current = %d\n", term_curr);
	tmp = term_curr - REG03_SGM41511_TERM_CHG_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_SGM41511_TERM_CHG_CURRENT_LIMIT_STEP;

	rc = sgm41511_config_interface(chip, REG03_SGM41511_ADDRESS,
					tmp << REG03_SGM41511_PRE_CHG_CURRENT_LIMIT_SHIFT,
					REG03_SGM41511_PRE_CHG_CURRENT_LIMIT_MASK);
	return 0;
}

int sgm41511_otg_ilim_set(int ilim)
{
	int rc;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG02_SGM41511_ADDRESS,
					ilim,
					REG02_SGM41511_OTG_CURRENT_LIMIT_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41511_otg_ilim_set  rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_otg_enable(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41511_set_wdt_timer(REG05_SGM41511_WATCHDOG_TIMER_DISABLE);

	rc = sgm41511_otg_ilim_set(REG02_SGM41511_OTG_CURRENT_LIMIT_1200MA);
	if (rc < 0) {
		chg_err("Couldn't sgm41511_otg_ilim_set rc = %d\n", rc);
	}

	rc = sgm41511_config_interface(chip, REG01_SGM41511_ADDRESS,
					REG01_SGM41511_OTG_ENABLE,
					REG01_SGM41511_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41511_otg_enable  rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_otg_disable(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG01_SGM41511_ADDRESS,
					REG01_SGM41511_OTG_DISABLE,
					REG01_SGM41511_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41511_otg_disable rc = %d\n", rc);
	}

	sgm41511_set_wdt_timer(REG05_SGM41511_WATCHDOG_TIMER_40S);

	return rc;
}

static int sgm41511_enable_gpio(struct chip_sgm41511 *chip, bool enable)
{
	if (!chip) {
		return -EINVAL;
	}

	if (enable) {
		if (IS_ERR_OR_NULL(chip->enable_gpio_active)) {
			chg_debug(": enable_gpio_active is error or NULL\n");
			return -EINVAL;
		} else {
			pinctrl_select_state(chip->pinctrl, chip->enable_gpio_active);
		}
	} else {
		if (IS_ERR_OR_NULL(chip->enable_gpio_sleep)) {
			chg_debug(": enable_gpio_sleep is error or NULL\n");
			return -EINVAL;
		} else {
			pinctrl_select_state(chip->pinctrl, chip->enable_gpio_sleep);
		}
	}

	return 0;
}


int sgm41511_enable_charging(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41511_enable_gpio(chip, true);

	sgm41511_otg_disable();
	rc = sgm41511_config_interface(chip, REG01_SGM41511_ADDRESS,
					REG01_SGM41511_CHARGING_ENABLE,
					REG01_SGM41511_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41511_enable_charging rc = %d\n", rc);
	}

	chg_err("sgm41511_enable_charging \n");
	return rc;
}

int sgm41511_disable_charging(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41511_enable_gpio(chip, false);

	sgm41511_otg_disable();
	rc = sgm41511_config_interface(chip, REG01_SGM41511_ADDRESS,
					REG01_SGM41511_CHARGING_DISABLE,
					REG01_SGM41511_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41511_disable_charging rc = %d\n", rc);
	}

	chg_err("sgm41511_disable_charging \n");
	return rc;
}

int sgm41511_check_charging_enable(void)
{
	int rc = 0;
	int reg_val = 0;
	struct chip_sgm41511 *chip = charger_ic;
	bool charging_enable = false;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_read_reg(chip, REG01_SGM41511_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG01_SGM41511_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charging_enable = ((reg_val & REG01_SGM41511_CHARGING_MASK) == REG01_SGM41511_CHARGING_ENABLE) ? 1 : 0;

	return charging_enable;
}

int sgm41511_get_ibus_current(void)
{
	if (sgm41511_check_charging_enable()) {
		return qpnp_get_prop_ibus_now();
	} else {
		return 0;
	}
}

int sgm41511_suspend_charger(void)
{
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	atomic_set(&chip->is_suspended, 1);

	chip->before_suspend_icl = sgm41511_get_usb_icl();
	sgm41511_input_current_limit_without_aicl(100);
	return sgm41511_disable_charging();
}
int sgm41511_unsuspend_charger(void)
{
	struct chip_sgm41511 *chip = charger_ic;
	struct oplus_chg_chip *ochip = g_oplus_chip;
	int ret = 0;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	atomic_set(&chip->is_suspended, 0);

	chip->before_unsuspend_icl = sgm41511_get_usb_icl();
	if ((chip->before_unsuspend_icl == 0)
			|| (chip->before_suspend_icl == 0)
			|| (chip->before_unsuspend_icl != SUSPEND_IBUS_MA)
			|| (chip->before_unsuspend_icl == chip->before_suspend_icl)) {
		chg_err("ignore set icl [%d %d]\n", chip->before_suspend_icl, chip->before_unsuspend_icl);
	} else {
		sgm41511_input_current_limit_without_aicl(chip->before_suspend_icl);
	}

	if (ochip && ochip->is_double_charger_support) {
		if (ochip->slave_charger_enable || ochip->em_mode)
			ret = sgm41511_enable_charging();
	} else {
		ret = sgm41511_enable_charging();
	}
	return ret;
}

bool sgm41511_check_suspend_charger(void)
{
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	return atomic_read(&chip->is_suspended);
}

int sgm41511_set_rechg_voltage(int recharge_mv)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG04_SGM41511_ADDRESS,
					recharge_mv,
					REG04_SGM41511_RECHG_THRESHOLD_VOL_MASK);

	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_reset_charger(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG0B_SGM41511_ADDRESS,
					REG0B_SGM41511_REG_RST_RESET,
					REG0B_SGM41511_REG_RST_MASK);

	if (rc) {
		chg_err("Couldn't sgm41511_reset_charger rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_registers_read_full(void)
{
	int rc = 0;
	int reg_full = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_read_reg(chip, REG08_SGM41511_ADDRESS, &reg_full);
	if (rc) {
		chg_err("Couldn't read REG08_SGM41511_ADDRESS rc = %d\n", rc);
		return 0;
	}

	reg_full = ((reg_full & REG08_SGM41511_CHG_STAT_MASK) == REG08_SGM41511_CHG_STAT_CHG_TERMINATION) ? 1 : 0;
	if (reg_full) {
		chg_err("the sgm41511 is full");
		sgm41511_dump_registers();
	}

	return rc;
}

int sgm41511_set_chging_term_disable(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG05_SGM41511_ADDRESS,
					REG05_SGM41511_TERMINATION_DISABLE,
					REG05_SGM41511_TERMINATION_MASK);
	if (rc) {
		chg_err("Couldn't set chging term disable rc = %d\n", rc);
	}

	return rc;
}

bool sgm41511_check_charger_resume(void)
{
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return false;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return false;
	}

	return true;
}

bool sgm41511_need_to_check_ibatt(void)
{
	return false;
}

int sgm41511_get_chg_current_step(void)
{
	return REG02_SGM41511_FAST_CHG_CURRENT_LIMIT_STEP;
}

int sgm41511_set_prechg_voltage_threshold(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41511_config_interface(chip, REG01_SGM41511_ADDRESS,
					REG01_SGM41511_SYS_VOL_LIMIT_3400MV,
					REG01_SGM41511_SYS_VOL_LIMIT_MASK);

	return rc;
}

int sgm41511_set_prechg_current( int ipre_mA)
{
	int tmp = 0;
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	tmp = ipre_mA - REG03_SGM41511_PRE_CHG_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_SGM41511_PRE_CHG_CURRENT_LIMIT_STEP;
	rc = sgm41511_config_interface(chip, REG03_SGM41511_ADDRESS,
					(tmp + 1) << REG03_SGM41511_PRE_CHG_CURRENT_LIMIT_SHIFT,
					REG03_SGM41511_PRE_CHG_CURRENT_LIMIT_MASK);

	return 0;
}

int sgm41511_set_otg_voltage(void)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG06_SGM41511_ADDRESS,
					REG06_SGM41511_OTG_VLIM_5000MV,
					REG06_SGM41511_OTG_VLIM_MASK);

	return rc;
}

int sgm41511_set_ovp(int val)
{
	int rc = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG06_SGM41511_ADDRESS,
					val,
					REG06_SGM41511_OVP_MASK);

	return rc;
}

int oplus_sgm41511_set_ovp_value(int val)
{
	int rc = 0;

	if(val == SGM41511_OVP_14000MV) {
		rc = sgm41511_set_ovp(REG06_SGM41511_OVP_14P0V);
	} else if (val == SGM41511_OVP_10500MV) {
		rc = sgm41511_set_ovp(REG06_SGM41511_OVP_10P5V);
	} else if (val == SGM41511_OVP_5500MV) {
		rc = sgm41511_set_ovp(REG06_SGM41511_OVP_5P5V);
	} else {
		rc = sgm41511_set_ovp(REG06_SGM41511_OVP_6P5V);
	}

	return rc;
}

int sgm41511_get_vbus_stat(void)
{
	int rc = 0;
	int vbus_stat = 0;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_read_reg(chip, REG08_SGM41511_ADDRESS, &vbus_stat);
	if (rc) {
		chg_err("Couldn't read REG08_SGM41511_ADDRESS rc = %d\n", rc);
		return 0;
	}

	vbus_stat = vbus_stat & REG08_SGM41511_VBUS_STAT_MASK;

	return vbus_stat;

}

int sgm41511_set_iindet(void)
{
	int rc;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_config_interface(chip, REG07_SGM41511_ADDRESS,
					REG07_SGM41511_IINDET_EN_MASK,
					REG07_SGM41511_IINDET_EN_FORCE_DET);
	if (rc < 0) {
		chg_err("Couldn't set REG07_SGM41511_IINDET_EN_MASK rc = %d\n", rc);
	}

	return rc;
}

int sgm41511_get_iindet(void)
{
	int rc = 0;
	int reg_val = 0;
	bool is_complete = false;
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41511_read_reg(chip, REG07_SGM41511_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG07_SGM41511_ADDRESS rc = %d\n", rc);
		return false;
	}

	is_complete = ((reg_val & REG07_SGM41511_IINDET_EN_MASK) == REG07_SGM41511_IINDET_EN_DET_COMPLETE) ? 1 : 0;
	return is_complete;
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

void sgm41511_vooc_timeout_callback(bool vbus_rising)
{
	struct chip_sgm41511 *chip = charger_ic;

	if (!chip) {
		return;
	}

	chip->power_good = vbus_rising;
	if (!vbus_rising) {
		sgm41511_request_dpdm(chip, false);
		chip->bc12_done = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_set_usb_props_type(chip->oplus_charger_type);
	}
	sgm41511_dump_registers();
}

int sgm41511_hardware_init(void)
{
	struct chip_sgm41511 *chip = charger_ic;
	struct oplus_chg_chip *ochip = g_oplus_chip;
	int vbatt = 0;

	if (ochip) {
		vbatt = ochip->batt_volt;
	}

	chg_err("init sgm41511 hardware! \n");

	if (!chip) {
                return false;
        }

	/*must be before set_vindpm_vol and set_input_current*/
	if (vbatt > AICL_POINT_VOL_5V_HIGH) {
		chip->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE3;
	} else if (vbatt > AICL_POINT_VOL_5V_LOW) {
		chip->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE2;
	} else {
		chip->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
	}
	chip->sw_aicl_point = SW_AICL_POINT_VOL_5V_PHASE1;


	sgm41511_set_stat_dis(false);
	sgm41511_set_int_mask(REG0A_SGM41511_VINDPM_INT_NOT_ALLOW | REG0A_SGM41511_IINDPM_INT_NOT_ALLOW);

	sgm41511_set_chg_timer(false);

	sgm41511_disable_charging();


	sgm41511_set_chging_term_disable();

	sgm41511_float_voltage_write(WPC_TERMINATION_VOLTAGE);

	sgm41511_otg_ilim_set(REG02_SGM41511_OTG_CURRENT_LIMIT_1200MA);

	sgm41511_set_prechg_voltage_threshold();

	sgm41511_set_prechg_current(WPC_PRECHARGE_CURRENT);


	sgm41511_set_termchg_current(WPC_TERMINATION_CURRENT);

	sgm41511_set_rechg_voltage(WPC_RECHARGE_VOLTAGE_OFFSET);

	sgm41511_set_vindpm_vol(chip->hw_aicl_point);

	sgm41511_set_otg_voltage();

	oplus_sgm41511_set_ovp_value(chip->set_ovp_value);

	sgm41511_set_wdt_timer(REG05_SGM41511_WATCHDOG_TIMER_40S);

	return true;
}

static int sgm41511_get_charger_type(void)
{
        struct chip_sgm41511 *chip = charger_ic;

        if (!chip)
                return POWER_SUPPLY_TYPE_UNKNOWN;

        return chip->oplus_charger_type;
}

struct oplus_chg_operations  sgm41511_chg_ops = {
	.dump_registers = sgm41511_dump_registers,
	.kick_wdt = sgm41511_kick_wdt,
	.hardware_init = sgm41511_hardware_init,
	.charging_current_write_fast = sgm41511_charging_current_write_fast,
	.set_aicl_point = sgm41511_set_aicl_point,
	.input_current_write = sgm41511_input_current_limit_write,
	.input_current_ctrl_by_vooc_write = sgm41511_input_current_limit_ctrl_by_vooc_write,
	.float_voltage_write = sgm41511_float_voltage_write,
	.term_current_set = sgm41511_set_termchg_current,
	.charging_enable = sgm41511_enable_charging,
	.charging_disable = sgm41511_disable_charging,
	.get_charging_enable = sgm41511_check_charging_enable,
	.charger_suspend = sgm41511_suspend_charger,
	.charger_unsuspend = sgm41511_unsuspend_charger,
	.charger_suspend_check = sgm41511_check_suspend_charger,
	.set_rechg_vol = sgm41511_set_rechg_voltage,
	.reset_charger = sgm41511_reset_charger,
	.read_full = sgm41511_registers_read_full,
	.otg_enable = sgm41511_otg_enable,
	.otg_disable = sgm41511_otg_disable,
	.set_charging_term_disable = sgm41511_set_chging_term_disable,
	.check_charger_resume = sgm41511_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = sgm41511_need_to_check_ibatt,
	.get_chg_current_step = sgm41511_get_chg_current_step,
	.get_charger_type = sgm41511_get_charger_type,
	.get_real_charger_type = opchg_get_real_charger_type,
	.get_charger_volt = sgm41511_get_vbus_voltage,
	.get_charger_current = sgm41511_get_ibus_current,
	.check_chrdet_status = sgm41511_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oplus_chg_get_shutdown_soc,
	.set_rtc_soc = oplus_chg_backup_soc,
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = sgm41511_chg_get_dyna_aicl_result,
#endif
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
	.get_charger_subtype = oplus_chg_get_charger_subtype,
	.oplus_chg_pd_setup = oplus_chg_set_pd_config,
	.set_qc_config = oplus_chg_set_qc_config,
	.oplus_chg_get_pd_type = oplus_sm8150_get_pd_type,
	.enable_qc_detect = oplus_chg_enable_qc_detect,
	.input_current_write_without_aicl = sgm41511_input_current_limit_without_aicl,
	.oplus_chg_wdt_enable = sgm41511_wdt_enable,
	.get_usbtemp_volt = oplus_get_usbtemp_volt,
	.set_typec_sinkonly = oplus_set_typec_sinkonly,
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
	.vooc_timeout_callback = sgm41511_vooc_timeout_callback,
};

static int sgm41511_parse_dt(struct chip_sgm41511 *chip)
{
	int ret = 0;

#if 0
	chip->irq_gpio = of_get_named_gpio(chip->client->dev.of_node, "sgm41511-irq-gpio", 0);
	if (!gpio_is_valid(chip->irq_gpio)) {
		chg_err("gpio_is_valid fail irq-gpio[%d]\n", chip->irq_gpio);
		return -EINVAL;
	}

	/*slaver charger not need bc1.2 and irq service.*/
	ret = devm_gpio_request(chip->dev, chip->irq_gpio, "sgm41511-irq-gpio");
	if (ret) {
		chg_err("unable to request irq-gpio[%d]\n", chip->irq_gpio);
		return -EINVAL;
	}

	chg_err("irq-gpio[%d]\n", chip->irq_gpio);
#endif

	/*Get the slave charger enable gpio.*/
	chip->enable_gpio = of_get_named_gpio(chip->client->dev.of_node, "qcom,slave_charg_enable-gpio", 0);
	if (!gpio_is_valid(chip->enable_gpio)) {
                chg_err("gpio_is_valid fail enable-gpio[%d]\n", chip->irq_gpio);
        } else {
		chg_err("enable-gpio[%d]\n", chip->enable_gpio);
	}

	ret = of_property_read_u32(chip->client->dev.of_node, "qcom,set_sgm41511_ovp", &chip->set_ovp_value);
	if(ret) {
		chip->set_ovp_value = SGM41511_OVP_6500MV;
		chg_err("Failed to read node of qcom,set_sgm41511_ovp\n");
	}

	return ret;
}

static int sgm41511_request_dpdm(struct chip_sgm41511 *chip, bool enable)
{
	int ret = 0;

	if (!chip)
		return 0;

	/* fetch the DPDM regulator */
	if (!chip->dpdm_reg && of_get_property(chip->dev->of_node,
				"dpdm-supply", NULL)) {
		chip->dpdm_reg = devm_regulator_get(chip->dev, "dpdm");
		if (IS_ERR(chip->dpdm_reg)) {
			ret = PTR_ERR(chip->dpdm_reg);
			chg_err("Couldn't get dpdm regulator ret=%d\n", ret);
			chip->dpdm_reg = NULL;
			return ret;
		}
	}

	mutex_lock(&chip->dpdm_lock);
	if (enable) {
		if (chip->dpdm_reg && !chip->dpdm_enabled) {
			chg_err("enabling DPDM regulator\n");
			ret = regulator_enable(chip->dpdm_reg);
			if (ret < 0)
				chg_err("Couldn't enable dpdm regulator ret=%d\n", ret);
			else
				chip->dpdm_enabled = true;
		}
	} else {
		if (chip->dpdm_reg && chip->dpdm_enabled) {
			chg_err("disabling DPDM regulator\n");
			ret = regulator_disable(chip->dpdm_reg);
			if (ret < 0)
				chg_err("Couldn't disable dpdm regulator ret=%d\n", ret);
			else
				chip->dpdm_enabled = false;
		}
	}
	mutex_unlock(&chip->dpdm_lock);

	return ret;
}

static void sgm41511_bc12_retry_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sgm41511 *chip = container_of(dwork, struct chip_sgm41511, bc12_retry_work);

	if (chip->is_sgm41511) {
		if (!sgm41511_chg_is_usb_present()) {
			chg_err("plugout during BC1.2, delay_cnt=%d,return\n", chip->bc12_delay_cnt);
			chip->bc12_delay_cnt = 0;
			return;
		}

		if (chip->bc12_delay_cnt >= OPLUS_BC12_DELAY_CNT) {
			chg_err("BC1.2 not complete delay_cnt to max\n");
			return;
		}
		chip->bc12_delay_cnt++;

		if (sgm41511_get_iindet()) {
			chg_err("BC1.2 complete, delay_cnt=%d\n", chip->bc12_delay_cnt);
			sgm41511_get_bc12(chip);
		} else {
			chg_err("BC1.2 not complete delay 50ms,delay_cnt=%d\n", chip->bc12_delay_cnt);
			schedule_delayed_work(&chip->bc12_retry_work, round_jiffies_relative(msecs_to_jiffies(50)));
		}
	}
}

static void sgm41511_start_bc12_retry(struct chip_sgm41511 *chip) {
	if (!chip)
		return;

	sgm41511_set_iindet();
	if (chip->is_sgm41511) {
		schedule_delayed_work(&chip->bc12_retry_work, round_jiffies_relative(msecs_to_jiffies(100)));
	}
}

static void sgm41511_get_bc12(struct chip_sgm41511 *chip)
{
	int vbus_stat = 0;

	if (!chip)
		return;

	if (!chip->bc12_done) {
		vbus_stat = sgm41511_get_vbus_stat();
		switch (vbus_stat) {
		case REG08_SGM41511_VBUS_STAT_SDP:
			if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
				chip->bc12_retried++;
				chg_err("bc1.2 sdp retry cnt=%d\n", chip->bc12_retried);
				sgm41511_start_bc12_retry(chip);
				break;
			}
			chip->bc12_done = true;

			chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB;
			oplus_set_usb_props_type(chip->oplus_charger_type);
			oplus_chg_wake_update_work();
			break;
		case REG08_SGM41511_VBUS_STAT_CDP:
			if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
				chip->bc12_retried++;
				chg_err("bc1.2 cdp retry cnt=%d\n", chip->bc12_retried);
				sgm41511_start_bc12_retry(chip);
				break;
			}
			chip->bc12_done = true;

			chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB_CDP;
			oplus_set_usb_props_type(chip->oplus_charger_type);
			oplus_chg_wake_update_work();
			break;
		case REG08_SGM41511_VBUS_STAT_DCP:
		case REG08_SGM41511_VBUS_STAT_OCP:
		case REG08_SGM41511_VBUS_STAT_FLOAT:
			chip->bc12_done = true;
			chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			oplus_set_usb_props_type(chip->oplus_charger_type);
			oplus_chg_wake_update_work();
			break;
		case REG08_SGM41511_VBUS_STAT_OTG_MODE:
		case REG08_SGM41511_VBUS_STAT_UNKNOWN:
		default:
			break;
		}
	}
}

static irqreturn_t sgm41511_irq_handler(int irq, void *data)
{
	struct chip_sgm41511 *chip = (struct chip_sgm41511 *) data;
	bool prev_pg = false, curr_pg = false, bus_gd = false;

	if (!chip)
		return IRQ_HANDLED;

	if (oplus_get_otg_online_status_default()) {
		chg_err("otg,ignore\n");
		chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_set_usb_props_type(chip->oplus_charger_type);
		return IRQ_HANDLED;
	}

	prev_pg = chip->power_good;
	curr_pg = bus_gd = sgm41511_get_bus_gd();
	sgm41511_dump_registers();
	oplus_chg_check_break(bus_gd);
	if (oplus_vooc_get_fastchg_started() == true
			&& oplus_vooc_get_adapter_update_status() != 1) {
		chg_err("oplus_vooc_get_fastchg_started = true!\n", __func__);
		return IRQ_HANDLED;
	} else {
		chip->power_good = curr_pg;
	}
	chg_err("(%d,%d, %d, %d)\n", prev_pg, chip->power_good, curr_pg, bus_gd);

	if (!prev_pg && chip->power_good) {
		sgm41511_request_dpdm(chip, true);
		chip->bc12_done = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		oplus_chg_wake_update_work();
		oplus_wake_up_usbtemp_thread();
		return IRQ_HANDLED;
	} else if (prev_pg && !chip->power_good) {
		sgm41511_request_dpdm(chip, false);
		chip->bc12_done = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_set_usb_props_type(chip->oplus_charger_type);
		oplus_vooc_reset_fastchg_after_usbout();
		if (oplus_vooc_get_fastchg_started() == false) {
			oplus_chg_set_chargerid_switch_val(0);
			oplus_chg_clear_chargerid_info();
		}
		oplus_chg_set_charger_type_unknown();
		oplus_chg_wake_update_work();
		oplus_wake_up_usbtemp_thread();
		return IRQ_HANDLED;
	} else if (!prev_pg && !chip->power_good) {
		chg_err("prev_pg & now_pg is false\n");
		chip->bc12_done = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		return IRQ_HANDLED;
	}

	sgm41511_get_bc12(chip);
	return IRQ_HANDLED;
}

static int sgm41511_enable_gpio_init(struct chip_sgm41511 *chip) {
	int rc = 0;

	if (NULL == chip) {
		return -EINVAL;
	}

	/* set pinctrl*/
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->enable_gpio_active = pinctrl_lookup_state(chip->pinctrl, "charging_switch1_active");
	if (IS_ERR_OR_NULL(chip->enable_gpio_active)) {
		chg_err(": Failed to get the active state pinctrl handle\n");
		return -EINVAL;
	}

	chip->enable_gpio_sleep = pinctrl_lookup_state(chip->pinctrl, "charging_switch1_sleep");
	if (IS_ERR_OR_NULL(chip->enable_gpio_sleep)) {
		chg_err(" Failed to get the sleep state pinctrl handle\n");
		return -EINVAL;
	}

	gpio_direction_input(chip->enable_gpio);
	pinctrl_select_state(chip->pinctrl, chip->enable_gpio_sleep);
	rc = gpio_get_value(chip->enable_gpio);

	chg_err("get the enable_gpio init value is %d\n", rc);

	return 0;
}

static int sgm41511_irq_register(struct chip_sgm41511 *chip)
{
	int ret = 0;

	ret = devm_request_threaded_irq(chip->dev, gpio_to_irq(chip->irq_gpio), NULL,
					sgm41511_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"sgm41511-eint",
					chip);
	if (ret < 0) {
		chg_err("sgm41511 request_irq fail!");
		return -EFAULT;
	}

	/*In Pikachu(21291), sgm41511 is slave charger, not need the BC1.2 and irq service*/
	ret = enable_irq_wake(gpio_to_irq(chip->irq_gpio));
	if (ret != 0) {
		chg_err("enable_irq_wake: irq_gpio failed %d\n", ret);
	}

	return ret;
}

static void hw_component_detect(struct chip_sgm41511 *chip)
{
	int rc = 0;
	int tmp = 0;

	rc = sgm41511_read_reg(chip, REG0B_SGM41511_ADDRESS, &tmp);
	if (rc) {
		chg_err("Couldn't read REG0B_SGM41511_ADDRESS rc = %d\n", rc);
		return;
	}

	switch (tmp & REG0B_SGM41511_PN_MASK) {
		case REG0B_SGM41511_PN:
			chip->is_sgm41511 = true;
			chg_err("is sgm41511\n");
			break;
		default:
			chg_err("not support REG0B:[0x%02x]\n", tmp);
			break;
	}

	return;
}

static int sgm41511_charger_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct chip_sgm41511 *chip = NULL;
	int ret = 0;

	chg_err("sgm41511 probe enter\n");

	chip = devm_kzalloc(&client->dev, sizeof(struct chip_sgm41511), GFP_KERNEL);
	if (!chip) {
		return -ENOMEM;
	}

	charger_ic = chip;
	chip->dev = &client->dev;
	chip->client = client;

	i2c_set_clientdata(client, chip);
	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->dpdm_lock);
	INIT_DELAYED_WORK(&chip->bc12_retry_work, sgm41511_bc12_retry_work);
	atomic_set(&chip->charger_suspended, 0);
	atomic_set(&chip->is_suspended, 0);
	chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->power_good = false;
	chip->before_suspend_icl = 0;
	chip->before_unsuspend_icl = 0;
	chip->is_sgm41511 = false;
	chip->is_bq25601d = false;
	chip->bc12_done = false;
	chip->bc12_retried = 0;
	chip->bc12_delay_cnt = 0;

	ret = sgm41511_parse_dt(chip);
	if (ret) {
		chg_err("Couldn't parse device tree ret=%d\n", ret);
		goto err_parse_dt;
	}

	hw_component_detect(chip);
	if (!chip->is_sgm41511) {
		chg_err("not support sgm41511\n");
		ret = -ENOTSUPP;
		goto err_parse_dt;
	}

	sgm41511_enable_gpio_init(chip);

	sgm41511_dump_registers();
	sgm41511_hardware_init();

	ret = device_create_file(chip->dev, &dev_attr_sgm41511_access);
	if (ret) {
		chg_err("create sgm41511_access file fail ret=%d\n", ret);
		goto err_create_file;
	}

	ret = sgm41511_irq_register(chip);
	if (ret) {
		chg_err("Failed to register irq ret=%d\n", ret);
	}
	set_charger_ic(SGM41511);
	chg_err("SGM41511 probe success.");

	return 0;

err_create_file:
	device_remove_file(chip->dev, &dev_attr_sgm41511_access);
err_parse_dt:
	mutex_destroy(&chip->dpdm_lock);
	mutex_destroy(&chip->i2c_lock);
	return ret;
}

static int sgm41511_charger_remove(struct i2c_client *client)
{
	struct chip_sgm41511 *chip = i2c_get_clientdata(client);

	mutex_destroy(&chip->dpdm_lock);
	mutex_destroy(&chip->i2c_lock);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int sgm41511_pm_resume(struct device *dev)
{
	struct chip_sgm41511 *chip = NULL;
	struct i2c_client *client = to_i2c_client(dev);

	if (client) {
		chip = i2c_get_clientdata(client);
		if (chip)
			atomic_set(&chip->charger_suspended, 0);
	}

	return 0;
}

static int sgm41511_pm_suspend(struct device *dev)
{
	struct chip_sgm41511 *chip = NULL;
	struct i2c_client *client = to_i2c_client(dev);

	if (client) {
		chip = i2c_get_clientdata(client);
		if (chip)
			atomic_set(&chip->charger_suspended, 1);
	}

	return 0;
}

static const struct dev_pm_ops sgm41511_pm_ops = {
	.resume			= sgm41511_pm_resume,
	.suspend		= sgm41511_pm_suspend,
};
#else
static int sgm41511_resume(struct i2c_client *client)
{
	struct chip_sgm41511 *chip = i2c_get_clientdata(client);

	if(!chip) {
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);

	return 0;
}

static int sgm41511_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct chip_sgm41511 *chip = i2c_get_clientdata(client);

	if(!chip) {
		return 0;
	}

	atomic_set(&chip->charger_suspended, 1);

	return 0;
}
#endif

static void sgm41511_charger_shutdown(struct i2c_client *client)
{
	//TODO:
}

static struct of_device_id sgm41511_charger_match_table[] = {
	{.compatible = "oplus,sgm41511",},
	{},
};

MODULE_DEVICE_TABLE(of, sgm41511_charger_match_table);

static const struct i2c_device_id sgm41511_i2c_device_id[] = {
	{ "sgm41511", 0x6b },
	{ },
};

MODULE_DEVICE_TABLE(i2c, sgm41511_i2c_device_id);

static struct i2c_driver sgm41511_charger_driver = {
	.driver = {
		.name = "sgm41511-charger",
		.owner = THIS_MODULE,
		.of_match_table = sgm41511_charger_match_table,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &sgm41511_pm_ops,
#endif
	},

	.probe = sgm41511_charger_probe,
	.remove = sgm41511_charger_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= sgm41511_resume,
	.suspend	= sy6974_suspend,
#endif
	.shutdown = sgm41511_charger_shutdown,
	.id_table = sgm41511_i2c_device_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(sgm41511_charger_driver);
#else
void sgm41511_charger_exit(void)
{
	i2c_del_driver(&sgm41511_charger_driver);
}

int sgm41511_charger_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	/*SGM41511 is slave charger in Pikachu(21291) */
	/*oplus_chg_ops_register("ext-sgm41511", &sgm41511_chg_ops);*/

	if (i2c_add_driver(&sgm41511_charger_driver) != 0) {
		chg_err(" failed to register sgm41511 i2c driver.\n");
	} else {
		chg_debug(" Success to register sgm41511 i2c driver.\n");
	}
	return ret;
}
#endif

MODULE_DESCRIPTION("SGM41511 Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("SY");
