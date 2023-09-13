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
#include "../oplus_chg_ops_manager.h"
#include "../voocphy/oplus_voocphy.h"
#include "oplus_discrete_charger.h"
#include "oplus_sgm41512.h"
#include <linux/pm_wakeup.h>

extern void oplus_notify_device_mode(bool enable);
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
extern void oplus_set_typec_cc_open(void);
extern void oplus_set_pd_active(int active);
extern void oplus_set_usb_props_type(enum power_supply_type type);
extern int oplus_get_adapter_svid(void);
extern void oplus_wake_up_usbtemp_thread(void);
extern int qpnp_get_prop_charger_voltage_now(void);
extern int qpnp_get_prop_ibus_now(void);
extern void oplus_sgm41512_enable_gpio(bool enable);

struct chip_sgm41512 {
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
	bool			is_sgm41512;
	bool			is_bq25601d;
	bool			bc12_done;
	char			bc12_delay_cnt;
	char			bc12_retried;
	int			hw_aicl_point;
	int			sw_aicl_point;
	int			reg_access;
	int			before_suspend_icl;
	int			before_unsuspend_icl;
	bool                    batfet_reset_disable;
	bool			use_voocphy;
	struct delayed_work	init_work;
	struct delayed_work	bc12_retry_work;

	struct wakeup_source *suspend_ws;
	/*fix chgtype identify error*/
	struct wakeup_source *keep_resume_ws;
	wait_queue_head_t wait;
};

static struct chip_sgm41512 *charger_ic = NULL;
static int aicl_result = 500;
#define OPLUS_BC12_RETRY_CNT 1
#define OPLUS_BC12_DELAY_CNT 18


static int sgm41512_request_dpdm(struct chip_sgm41512 *chip, bool enable);
static void sgm41512_get_bc12(struct chip_sgm41512 *chip);
#define READ_REG_CNT_MAX	20
#define USLEEP_5000MS	5000
static bool dumpreg_by_irq = 0;

static void oplus_chg_wakelock(struct chip_sgm41512 *chip, bool awake);

static int __sgm41512_read_reg(struct chip_sgm41512 *chip, int reg, int *data)
{
	s32 ret = 0;
	int retry = READ_REG_CNT_MAX;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		while(retry > 0 && atomic_read(&chip->is_suspended) == 0) {
			usleep_range(USLEEP_5000MS, USLEEP_5000MS);
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

static int __sgm41512_write_reg(struct chip_sgm41512 *chip, int reg, int val)
{
	s32 ret = 0;
	int retry = 3;

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
		chg_err("i2c write fail: can't write %02x to %02x: %d\n", val, reg, ret);
		return ret;
	}

	return 0;
}

static int sgm41512_read_reg(struct chip_sgm41512 *chip, int reg, int *data)
{
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41512_read_reg(chip, reg, data);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static __maybe_unused int sgm41512_write_reg(struct chip_sgm41512 *chip, int reg, int data)
{
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41512_write_reg(chip, reg, data);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static __maybe_unused int sgm41512_config_interface(struct chip_sgm41512 *chip, int reg, int data, int mask)
{
	int ret;
	int tmp;

	mutex_lock(&chip->i2c_lock);
	ret = __sgm41512_read_reg(chip, reg, &tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sgm41512_write_reg(chip, reg, tmp);
	if (ret)
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

int sgm41512_set_vindpm_vol(int vol)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41512 *chip = charger_ic;
	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	tmp = (vol - REG06_SGM41512_VINDPM_OFFSET) / REG06_SGM41512_VINDPM_STEP_MV;
	rc = sgm41512_config_interface(chip, REG06_SGM41512_ADDRESS,
			tmp << REG06_SGM41512_VINDPM_SHIFT,
			REG06_SGM41512_VINDPM_MASK);

	return rc;
}

int sgm41512_usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

static int sgm41512_get_usb_icl(void)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	rc = sgm41512_read_reg(chip, REG00_SGM41512_ADDRESS, &tmp);
	if (rc) {
		chg_err("Couldn't read REG00_SGM41512_ADDRESS rc = %d\n", rc);
		return 0;
	}
	tmp = (tmp & REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK) >> REG00_SGM41512_INPUT_CURRENT_LIMIT_SHIFT;
	return (tmp * REG00_SGM41512_INPUT_CURRENT_LIMIT_STEP + REG00_SGM41512_INPUT_CURRENT_LIMIT_OFFSET);
}

int sgm41512_input_current_limit_without_aicl(int current_ma)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("in suspend\n");
		return 0;
	}

	if (current_ma > REG00_SGM41512_INPUT_CURRENT_LIMIT_MAX)
		current_ma = REG00_SGM41512_INPUT_CURRENT_LIMIT_MAX;

	if (current_ma < REG00_SGM41512_INPUT_CURRENT_LIMIT_OFFSET)
		current_ma = REG00_SGM41512_INPUT_CURRENT_LIMIT_OFFSET;

	tmp = (current_ma - REG00_SGM41512_INPUT_CURRENT_LIMIT_OFFSET) / REG00_SGM41512_INPUT_CURRENT_LIMIT_STEP;
	chg_err("tmp current [%d]ma\n", current_ma);
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS,
			tmp << REG00_SGM41512_INPUT_CURRENT_LIMIT_SHIFT,
			REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);

	if (rc < 0) {
		chg_err("Couldn't set aicl rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_chg_get_dyna_aicl_result(void)
{
	return aicl_result;
}

void sgm41512_set_aicl_point(int vbatt)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return;

	if (chip->hw_aicl_point == 4440 && vbatt > 4140) {
		chip->hw_aicl_point = 4520;
		chip->sw_aicl_point = 4535;
		sgm41512_set_vindpm_vol(chip->hw_aicl_point);
	} else if (chip->hw_aicl_point == 4520 && vbatt < 4000) {
		chip->hw_aicl_point = 4440;
		chip->sw_aicl_point = 4500;
		sgm41512_set_vindpm_vol(chip->hw_aicl_point);
	}
}

int sgm41512_get_charger_vol(void)
{
	return qpnp_get_prop_charger_voltage_now();
}

int sgm41512_get_vbus_voltage(void)
{
	return qpnp_get_prop_charger_voltage_now();
}

int sgm41512_get_ibus_current(void)
{
	return qpnp_get_prop_ibus_now();
}

int sgm41512_input_current_limit_write(int current_ma)
{
	int i = 0, rc = 0;
	int chg_vol = 0;
	int sw_aicl_point = 0;
	struct chip_sgm41512 *chip = charger_ic;
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
	pre_icl = sgm41512_get_usb_icl();
	for (pre_icl_index = ARRAY_SIZE(sgm41512_usb_icl) - 1; pre_icl_index >= 0; pre_icl_index--) {
		if (sgm41512_usb_icl[pre_icl_index] < pre_icl) {
			break;
		}
	}
	chg_err("icl_set: %d, pre_icl: %d, pre_icl_index: %d\n", current_ma, pre_icl, pre_icl_index);

	for (i = pre_icl_index; i > 1; i--) {
		rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
		if (rc) {
			chg_err("icl_down: set icl to %d mA fail, rc=%d\n", sgm41512_usb_icl[i], rc);
		} else {
			chg_err("icl_down: set icl to %d mA\n", sgm41512_usb_icl[i]);
		}
		msleep(50);
	}

	/*second: aicl process, step from 500ma*/
	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}


	sw_aicl_point = chip->sw_aicl_point;

	i = 1; /* 500 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		chg_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;
	i = 2; /* 900 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;
	i = 3; /* 1200 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}
	i = 4; /* 1350 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1350) {
		i = i - 1; /*We use 1.2A here*/
		goto aicl_end;
	}
	i = 5; /* 1500 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; /*We use 1.2A here*/
		goto aicl_end;
	} else if (current_ma < 2000) {
		goto aicl_end;
	}
	i = 6; /* 1750 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /*1.2*/
		goto aicl_pre_step;
	}
	i = 7; /* 2000 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*1.5*/
		goto aicl_pre_step;
	} else if (current_ma < 3000) {
		goto aicl_end;
	}
	i = 8; /* 3000 */
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41512_get_charger_vol();
	if (chg_vol < sw_aicl_point) {
		i = i -1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000) {
		goto aicl_end;
	}
aicl_pre_step:
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, sgm41512_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_end:
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, sgm41512_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_rerun:
	aicl_result = sgm41512_usb_icl[i];
	rc = sgm41512_input_current_limit_without_aicl(sgm41512_usb_icl[i]);
	rc = sgm41512_set_vindpm_vol(chip->hw_aicl_point);
	return rc;
}

int sgm41512_input_current_limit_ctrl_by_vooc_write(int current_ma)
{
	int rc = 0;
	int cur_usb_icl  = 0;
	int temp_curr = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (atomic_read(&chip->is_suspended) == 1) {
		chg_err("suspend,ignore set current=%dmA\n", current_ma);
		return 0;
	}

	cur_usb_icl = sgm41512_get_usb_icl();
	chg_err(" get cur_usb_icl = %d\n", cur_usb_icl);

	if (current_ma > cur_usb_icl) {
		for (temp_curr = cur_usb_icl; temp_curr < current_ma; temp_curr += 500) {
			msleep(35);
			rc = sgm41512_input_current_limit_without_aicl(temp_curr);
			chg_err("[up] set input_current = %d\n", temp_curr);
		}
	} else {
		for (temp_curr = cur_usb_icl; temp_curr > current_ma; temp_curr -= 500) {
			msleep(35);
			rc = sgm41512_input_current_limit_without_aicl(temp_curr);
			chg_err("[down] set input_current = %d\n", temp_curr);
		}
	}

	rc = sgm41512_input_current_limit_without_aicl(current_ma);
	return rc;
}

static ssize_t sgm41512_access_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct chip_sgm41512 *chip = dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}
	return sprintf(buf, "0x%02x\n", chip->reg_access);
}

static ssize_t sgm41512_access_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct chip_sgm41512 *chip = dev_get_drvdata(dev);
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
			pr_err("[%s] write sgm41512 reg 0x%02x with value 0x%02x !\n",
					__func__, (unsigned int) chip->reg_access, reg_value);
			ret = sgm41512_write_reg(chip, chip->reg_access, reg_value);
		} else {
			ret = sgm41512_read_reg(chip, chip->reg_access, &reg_value);
			pr_err("[%s] read sgm41512 reg 0x%02x with value 0x%02x !\n",
					__func__, (unsigned int) chip->reg_access, reg_value);
		}
	}
	return size;
}

static DEVICE_ATTR(sgm41512_access, 0660, sgm41512_access_show, sgm41512_access_store);

void sgm41512_dump_registers(void)
{
	int ret = 0;
	int addr = 0;
	int val_buf[SGM41512_REG_NUMBER] = {0x0};
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return;
	}

	for(addr = SGM41512_FIRST_REG; addr <= SGM41512_LAST_REG; addr++) {
		ret = sgm41512_read_reg(chip, addr, &val_buf[addr]);
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

int sgm41512_kick_wdt(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_WDT_TIMER_RESET,
			REG01_SGM41512_WDT_TIMER_RESET_MASK);
	if (rc) {
		chg_err("Couldn't sgm41512 kick wdt rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_set_wdt_timer(int reg)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG05_SGM41512_ADDRESS,
			reg,
			REG05_SGM41512_WATCHDOG_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return 0;
}

static void sgm41512_wdt_enable(bool wdt_enable)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return;

	if (wdt_enable)
		sgm41512_set_wdt_timer(REG05_SGM41512_WATCHDOG_TIMER_40S);
	else
		sgm41512_set_wdt_timer(REG05_SGM41512_WATCHDOG_TIMER_DISABLE);

	chg_err("sgm41512_wdt_enable[%d]\n", wdt_enable);
}

int sgm41512_set_stat_dis(bool enable)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS,
			enable ? REG00_SGM41512_STAT_DIS_ENABLE : REG00_SGM41512_STAT_DIS_DISABLE,
			REG00_SGM41512_STAT_DIS_MASK);
	if (rc) {
		chg_err("Couldn't sgm41512 set_stat_dis rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_set_int_mask(int val)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG0A_SGM41512_ADDRESS,
			val,
			REG0A_SGM41512_VINDPM_INT_MASK | REG0A_SGM41512_IINDPM_INT_MASK);
	if (rc) {
		chg_err("Couldn't sgm41512 set_int_mask rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_set_chg_timer(bool enable)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG05_SGM41512_ADDRESS,
			enable ? REG05_SGM41512_CHG_SAFETY_TIMER_ENABLE : REG05_SGM41512_CHG_SAFETY_TIMER_DISABLE,
			REG05_SGM41512_CHG_SAFETY_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't sgm41512 set_chg_timer rc = %d\n", rc);
	}

	return rc;
}

bool sgm41512_get_bus_gd(void)
{
	int rc = 0;
	int reg_val = 0;
	bool bus_gd = false;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG0A_SGM41512_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read regeister, rc = %d\n", rc);
		return false;
	}

	bus_gd = ((reg_val & REG0A_SGM41512_BUS_GD_MASK) == REG0A_SGM41512_BUS_GD_YES) ? 1 : 0;
	return bus_gd;
}

bool sgm41512_get_power_gd(void)
{
	int rc = 0;
	int reg_val = 0;
	bool power_gd = false;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG08_SGM41512_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't get_power_gd rc = %d\n", rc);
		return false;
	}

	power_gd = ((reg_val & REG08_SGM41512_POWER_GOOD_STAT_MASK) == REG08_SGM41512_POWER_GOOD_STAT_GOOD) ? 1 : 0;
	return power_gd;
}

static bool sgm41512_chg_is_usb_present(void)
{
	if (oplus_get_otg_online_status_default()) {
		chg_err("otg,return false");
		return false;
	}

	if (oplus_vooc_get_fastchg_started() == true) {
		chg_err("[%s]:svooc/vooc already started!\n", __func__);
		return true;
	} else {
		return sgm41512_get_bus_gd();
	}
}

int sgm41512_charging_current_write_fast(int chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("set charge current = %d\n", chg_cur);

	tmp = chg_cur - REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_STEP;

	rc = sgm41512_config_interface(chip, REG02_SGM41512_ADDRESS,
			tmp << REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_SHIFT,
			REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_MASK);

	return rc;
}

int sgm41512_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("vfloat_mv = %d\n", vfloat_mv);

	if (chip->is_bq25601d) {
		tmp = vfloat_mv - REG04_BQ25601D_CHG_VOL_LIMIT_OFFSET;
	} else {
		tmp = vfloat_mv - REG04_SGM41512_CHG_VOL_LIMIT_OFFSET;
	}
	tmp = tmp / REG04_SGM41512_CHG_VOL_LIMIT_STEP;

	rc = sgm41512_config_interface(chip, REG04_SGM41512_ADDRESS,
			tmp << REG04_SGM41512_CHG_VOL_LIMIT_SHIFT,
			REG04_SGM41512_CHG_VOL_LIMIT_MASK);

	return rc;
}

int sgm41512_set_termchg_current(int term_curr)
{
	int rc = 0;
	int tmp = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("term_current = %d\n", term_curr);
	tmp = term_curr - REG03_SGM41512_TERM_CHG_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_SGM41512_TERM_CHG_CURRENT_LIMIT_STEP;

	rc = sgm41512_config_interface(chip, REG03_SGM41512_ADDRESS,
			tmp << REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_SHIFT,
			REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_MASK);
	return 0;
}

int sgm41512_otg_ilim_set(int ilim)
{
	int rc;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG02_SGM41512_ADDRESS,
			ilim,
			REG02_SGM41512_OTG_CURRENT_LIMIT_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41512_otg_ilim_set  rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_otg_enable(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41512_set_wdt_timer(REG05_SGM41512_WATCHDOG_TIMER_DISABLE);

	rc = sgm41512_otg_ilim_set(REG02_SGM41512_OTG_CURRENT_LIMIT_1200MA);
	if (rc < 0) {
		chg_err("Couldn't sgm41512_otg_ilim_set rc = %d\n", rc);
	}

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_OTG_ENABLE,
			REG01_SGM41512_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41512_otg_enable  rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_otg_disable(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_OTG_DISABLE,
			REG01_SGM41512_OTG_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41512_otg_disable rc = %d\n", rc);
	}

	sgm41512_set_wdt_timer(REG05_SGM41512_WATCHDOG_TIMER_DISABLE);

	return rc;
}

static int sgm41512_enable_gpio(struct chip_sgm41512 *chip, bool enable)
{
	if (enable == true) {
		if (chip->enable_gpio > 0)
			gpio_direction_output(chip->enable_gpio, 0);
	} else {
		if (chip->enable_gpio > 0)
			gpio_direction_output(chip->enable_gpio, 1);
	}
	return 0;
}
int sgm41512_enable_charging(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41512_enable_gpio(chip, true);

	sgm41512_otg_disable();
	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_CHARGING_ENABLE,
			REG01_SGM41512_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41512_enable_charging rc = %d\n", rc);
	}

	chg_err("sgm41512_enable_charging \n");
	return rc;
}

int sgm41512_disable_charging(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	sgm41512_enable_gpio(chip, false);

	sgm41512_otg_disable();
	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_CHARGING_DISABLE,
			REG01_SGM41512_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn't sgm41512_disable_charging rc = %d\n", rc);
	}

	chg_err("sgm41512_disable_charging \n");
	return rc;
}

int sgm41512_check_charging_enable(void)
{
	int rc = 0;
	int reg_val = 0;
	struct chip_sgm41512 *chip = charger_ic;
	bool charging_enable = false;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG01_SGM41512_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG01_SGM41512_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charging_enable = ((reg_val & REG01_SGM41512_CHARGING_MASK) == REG01_SGM41512_CHARGING_ENABLE) ? 1 : 0;

	return charging_enable;
}

int sgm41512_suspend_charger(void)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	atomic_set(&chip->is_suspended, 1);

	chip->before_suspend_icl = sgm41512_get_usb_icl();
	sgm41512_input_current_limit_without_aicl(100);
	return sgm41512_disable_charging();
}
int sgm41512_unsuspend_charger(void)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	atomic_set(&chip->is_suspended, 0);

	chip->before_unsuspend_icl = sgm41512_get_usb_icl();
	if ((chip->before_unsuspend_icl == 0)
			|| (chip->before_suspend_icl == 0)
			|| (chip->before_unsuspend_icl != 100)
			|| (chip->before_unsuspend_icl == chip->before_suspend_icl)) {
		chg_err("ignore set icl [%d %d]\n", chip->before_suspend_icl, chip->before_unsuspend_icl);
	} else {
		sgm41512_input_current_limit_without_aicl(chip->before_suspend_icl);
	}
	return sgm41512_enable_charging();
}

bool sgm41512_check_suspend_charger(void)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	return atomic_read(&chip->is_suspended);
}

void sgm41512_really_suspend_charger(bool en)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return;
	}

	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS,
			en ? REG00_SGM41512_SUSPEND_MODE_ENABLE : REG00_SGM41512_SUSPEND_MODE_DISABLE,
			REG00_SGM41512_SUSPEND_MODE_MASK);

	if (rc < 0) {
		chg_err("fail en=%d rc = %d\n", en, rc);
	}
}

int sgm41512_set_rechg_voltage(int recharge_mv)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG04_SGM41512_ADDRESS,
			recharge_mv,
			REG04_SGM41512_RECHG_THRESHOLD_VOL_MASK);

	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_reset_charger(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG0B_SGM41512_ADDRESS,
			REG0B_SGM41512_REG_RST_RESET,
			REG0B_SGM41512_REG_RST_MASK);

	if (rc) {
		chg_err("Couldn't sgm41512_reset_charger rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_registers_read_full(void)
{
	int rc = 0;
	int reg_full = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG08_SGM41512_ADDRESS, &reg_full);
	if (rc) {
		chg_err("Couldn't read REG08_SGM41512_ADDRESS rc = %d\n", rc);
		return 0;
	}

	reg_full = ((reg_full & REG08_SGM41512_CHG_STAT_MASK) == REG08_SGM41512_CHG_STAT_CHG_TERMINATION) ? 1 : 0;
	if (reg_full) {
		chg_err("the sgm41512 is full");
		sgm41512_dump_registers();
	}

	return rc;
}

int sgm41512_set_chging_term_disable(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG05_SGM41512_ADDRESS,
			REG05_SGM41512_TERMINATION_DISABLE,
			REG05_SGM41512_TERMINATION_MASK);
	if (rc) {
		chg_err("Couldn't set chging term disable rc = %d\n", rc);
	}

	return rc;
}

bool sgm41512_check_charger_resume(void)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return false;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return false;
	}

	return true;
}

bool sgm41512_need_to_check_ibatt(void)
{
	return false;
}

int sgm41512_get_chg_current_step(void)
{
	return REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_STEP;
}

int sgm41512_set_prechg_voltage_threshold(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_SYS_VOL_LIMIT_3400MV,
			REG01_SGM41512_SYS_VOL_LIMIT_MASK);

	return rc;
}

int sgm41512_set_prechg_current(int ipre_mA)
{
	int tmp = 0;
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	tmp = ipre_mA - REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_STEP;
	rc = sgm41512_config_interface(chip, REG03_SGM41512_ADDRESS,
			(tmp + 1) << REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_SHIFT,
			REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_MASK);

	return 0;
}

int sgm41512_set_otg_voltage(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG06_SGM41512_ADDRESS,
			REG06_SGM41512_OTG_VLIM_5000MV,
			REG06_SGM41512_OTG_VLIM_MASK);

	return rc;
}

int sgm41512_set_ovp(int val)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG06_SGM41512_ADDRESS,
			val,
			REG06_SGM41512_OVP_MASK);

	return rc;
}

int sgm41512_get_vbus_stat(void)
{
	int rc = 0;
	int vbus_stat = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG08_SGM41512_ADDRESS, &vbus_stat);
	if (rc) {
		chg_err("Couldn't read REG08_SGM41512_ADDRESS rc = %d\n", rc);
		return 0;
	}

	vbus_stat = vbus_stat & REG08_SGM41512_VBUS_STAT_MASK;

	return vbus_stat;
}

int sgm41512_set_iindet(void)
{
	int rc;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG07_SGM41512_ADDRESS,
			REG07_SGM41512_IINDET_EN_MASK,
			REG07_SGM41512_IINDET_EN_FORCE_DET);
	if (rc < 0) {
		chg_err("Couldn't set REG07_SGM41512_IINDET_EN_MASK rc = %d\n", rc);
	}

	return rc;
}

int sgm41512_get_iindet(void)
{
	int rc = 0;
	int reg_val = 0;
	bool is_complete = false;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG07_SGM41512_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG07_SGM41512_ADDRESS rc = %d\n", rc);
		return false;
	}

	is_complete = ((reg_val & REG07_SGM41512_IINDET_EN_MASK) == REG07_SGM41512_IINDET_EN_DET_COMPLETE) ? 1 : 0;
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

void sgm41512_vooc_timeout_callback(bool vbus_rising)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return;
	}

	chip->power_good = vbus_rising;
	if (!vbus_rising) {
		sgm41512_request_dpdm(chip, false);
		chip->bc12_done = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_set_usb_props_type(chip->oplus_charger_type);
		oplus_chg_wakelock(chip, false);
	}
	sgm41512_dump_registers();
}

static int sgm41512_batfet_reset_disable(struct chip_sgm41512 *chip, bool enable)
{
	int rc = 0;
	int val = 0;

	if(enable) {
		val = SGM41512_BATFET_RST_DISABLE << REG07_SGM41512_BATFET_RST_EN_SHIFT;
	} else {
		val = SGM41512_BATFET_RST_ENABLE << REG07_SGM41512_BATFET_RST_EN_SHIFT;
	}

	rc = sgm41512_config_interface(chip, REG07_SGM41512_ADDRESS, val, REG07_SGM41512_BATFET_RST_EN_MASK);

	return rc;
}

int sgm41512_set_shipmode(bool enable)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (chip == NULL)
		return rc;

	if (enable) {
		rc = sgm41512_config_interface(chip, REG07_SGM41512_ADDRESS,
			REG07_SGM41512_BATFET_DIS_ON,
			REG07_SGM41512_BATFET_DIS_MASK);
		if (rc < 0) {
			chg_err("Couldn't set REG07_SGM41512_BATFET_DIS_ON rc = %d\n", rc);
		}
	} else {
		rc = sgm41512_config_interface(chip, REG07_SGM41512_ADDRESS,
			REG07_SGM41512_BATFET_DIS_OFF,
			REG07_SGM41512_BATFET_DIS_MASK);
		if (rc < 0) {
			chg_err("Couldn't set REG07_SGM41512_BATFET_DIS_OFF rc = %d\n", rc);
		}
	}

	return rc;
}

int sgm41512_hardware_init(void)
{
	struct chip_sgm41512 *chip = charger_ic;

	chg_err("init sgm41512 hardware! \n");

	if (!chip) {
		return false;
	}

	/*must be before set_vindpm_vol and set_input_current*/
	chip->hw_aicl_point = 4440;
	chip->sw_aicl_point = 4500;


	sgm41512_set_stat_dis(false);
	sgm41512_set_int_mask(REG0A_SGM41512_VINDPM_INT_NOT_ALLOW | REG0A_SGM41512_IINDPM_INT_NOT_ALLOW);

	sgm41512_set_chg_timer(false);

	sgm41512_disable_charging();

	sgm41512_set_ovp(REG06_SGM41512_OVP_14P0V);

	sgm41512_set_chging_term_disable();

	sgm41512_float_voltage_write(WPC_TERMINATION_VOLTAGE);

	sgm41512_otg_ilim_set(REG02_SGM41512_OTG_CURRENT_LIMIT_1200MA);

	sgm41512_set_prechg_voltage_threshold();

	sgm41512_set_prechg_current(WPC_PRECHARGE_CURRENT);


	sgm41512_set_termchg_current(WPC_TERMINATION_CURRENT);

	sgm41512_input_current_limit_without_aicl(REG00_SGM41512_INIT_INPUT_CURRENT_LIMIT_500MA);

	sgm41512_set_rechg_voltage(WPC_RECHARGE_VOLTAGE_OFFSET);

	sgm41512_set_vindpm_vol(chip->hw_aicl_point);

	sgm41512_set_otg_voltage();

	sgm41512_batfet_reset_disable(chip, chip->batfet_reset_disable);

	sgm41512_unsuspend_charger();

	sgm41512_enable_charging();

	sgm41512_set_wdt_timer(REG05_SGM41512_WATCHDOG_TIMER_40S);

	return true;
}

static int sgm41512_get_charger_type(void)
{
	struct chip_sgm41512 *chip = charger_ic;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();

	if (!chip || !g_oplus_chip)
		return POWER_SUPPLY_TYPE_UNKNOWN;


	if (chip->oplus_charger_type != g_oplus_chip->charger_type && g_oplus_chip->usb_psy)
		power_supply_changed(g_oplus_chip->usb_psy);

	return chip->oplus_charger_type;
}

struct oplus_chg_operations  sgm41512_chg_ops = {
	.dump_registers = sgm41512_dump_registers,
	.kick_wdt = sgm41512_kick_wdt,
	.hardware_init = sgm41512_hardware_init,
	.charging_current_write_fast = sgm41512_charging_current_write_fast,
	.set_aicl_point = sgm41512_set_aicl_point,
	.input_current_write = sgm41512_input_current_limit_write,
	.input_current_ctrl_by_vooc_write = sgm41512_input_current_limit_ctrl_by_vooc_write,
	.float_voltage_write = sgm41512_float_voltage_write,
	.term_current_set = sgm41512_set_termchg_current,
	.charging_enable = sgm41512_enable_charging,
	.charging_disable = sgm41512_disable_charging,
	.get_charging_enable = sgm41512_check_charging_enable,
	.charger_suspend = sgm41512_suspend_charger,
	.charger_unsuspend = sgm41512_unsuspend_charger,
	.charger_suspend_check = sgm41512_check_suspend_charger,
	.set_rechg_vol = sgm41512_set_rechg_voltage,
	.reset_charger = sgm41512_reset_charger,
	.read_full = sgm41512_registers_read_full,
	.otg_enable = sgm41512_otg_enable,
	.otg_disable = sgm41512_otg_disable,
	.set_charging_term_disable = sgm41512_set_chging_term_disable,
	.check_charger_resume = sgm41512_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = sgm41512_need_to_check_ibatt,
	.get_chg_current_step = sgm41512_get_chg_current_step,
	.get_charger_type = sgm41512_get_charger_type,
	.get_real_charger_type = opchg_get_real_charger_type,
	.get_charger_volt = sgm41512_get_vbus_voltage,
	.get_charger_current = sgm41512_get_ibus_current,
	.check_chrdet_status = sgm41512_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oplus_chg_get_shutdown_soc,
	.set_rtc_soc = oplus_chg_backup_soc,
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = sgm41512_chg_get_dyna_aicl_result,
#endif
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
	.get_charger_subtype = oplus_chg_get_charger_subtype,
	.oplus_chg_pd_setup = oplus_chg_set_pd_config,
	.set_qc_config = oplus_chg_set_qc_config,
	.oplus_chg_get_pd_type = oplus_sm8150_get_pd_type,
	.enable_qc_detect = oplus_chg_enable_qc_detect,
	.input_current_write_without_aicl = sgm41512_input_current_limit_without_aicl,
	.oplus_chg_wdt_enable = sgm41512_wdt_enable,
	.get_usbtemp_volt = oplus_get_usbtemp_volt,
	.set_typec_sinkonly = oplus_set_typec_sinkonly,
	.set_typec_cc_open = oplus_set_typec_cc_open,
	.really_suspend_charger = sgm41512_really_suspend_charger,
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
	.vooc_timeout_callback = sgm41512_vooc_timeout_callback,
	.enable_shipmode = sgm41512_set_shipmode,
};

static int sgm41512_parse_dt(struct chip_sgm41512 *chip)
{
	int ret = 0;

	chip->irq_gpio = of_get_named_gpio(chip->client->dev.of_node, "sgm41512-irq-gpio", 0);
	if (!gpio_is_valid(chip->irq_gpio)) {
		chg_err("gpio_is_valid fail irq-gpio[%d]\n", chip->irq_gpio);
		return -EINVAL;
	}

	/*slaver charger not need bc1.2 and irq service.*/
	ret = devm_gpio_request(chip->dev, chip->irq_gpio, "sgm41512-irq-gpio");
	if (ret) {
		chg_err("unable to request irq-gpio[%d]\n", chip->irq_gpio);
		return -EINVAL;
	}

	chg_err("irq-gpio[%d]\n", chip->irq_gpio);

	/*Get the slave charger enable gpio.*/

	chip->enable_gpio = of_get_named_gpio(chip->client->dev.of_node, "qcom,slave_charger_enable-gpio", 0);
	if (!gpio_is_valid(chip->enable_gpio)) {
                chg_err("gpio_is_valid fail enable-gpio[%d]\n", chip->enable_gpio);
        } else {
		chg_err("enable-gpio[%d]\n", chip->enable_gpio);
		ret = gpio_request(chip->enable_gpio,
			                  "msg41512-enable-gpio");
			if (ret) {
				chg_err("unable to request gpio [%d]\n",
				         chip->enable_gpio);
			}
	}
	chip->use_voocphy = of_property_read_bool(chip->client->dev.of_node, "qcom,use_voocphy");
	chg_err("use_voocphy=%d\n", chip->use_voocphy);
	return ret;
}

static int sgm41512_request_dpdm(struct chip_sgm41512 *chip, bool enable)
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

static void sgm41512_bc12_retry_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sgm41512 *chip = container_of(dwork, struct chip_sgm41512, bc12_retry_work);

	if (chip->is_sgm41512) {
		if (!sgm41512_chg_is_usb_present()) {
			chg_err("plugout during BC1.2, delay_cnt=%d,return\n", chip->bc12_delay_cnt);
			chip->bc12_delay_cnt = 0;
			return;
		}

		if (chip->bc12_delay_cnt >= OPLUS_BC12_DELAY_CNT) {
			chg_err("BC1.2 not complete delay_cnt to max\n");
			return;
		}
		chip->bc12_delay_cnt++;

		if (sgm41512_get_iindet()) {
			chg_err("BC1.2 complete, delay_cnt=%d\n", chip->bc12_delay_cnt);
			sgm41512_get_bc12(chip);
		} else {
			chg_err("BC1.2 not complete delay 50ms,delay_cnt=%d\n", chip->bc12_delay_cnt);
			schedule_delayed_work(&chip->bc12_retry_work, round_jiffies_relative(msecs_to_jiffies(50)));
		}
	}
}

static void sgm41512_start_bc12_retry(struct chip_sgm41512 *chip) {
	if (!chip)
		return;

	sgm41512_set_iindet();
	if (chip->is_sgm41512) {
		schedule_delayed_work(&chip->bc12_retry_work, round_jiffies_relative(msecs_to_jiffies(100)));
	}
}

static void sgm41512_get_bc12(struct chip_sgm41512 *chip)
{
	int vbus_stat = 0;

	if (!chip)
		return;

	if (!chip->bc12_done) {
		vbus_stat = sgm41512_get_vbus_stat();
		switch (vbus_stat) {
		case REG08_SGM41512_VBUS_STAT_SDP:
				if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
					chip->bc12_retried++;
					chg_err("bc1.2 sdp retry cnt=%d\n", chip->bc12_retried);
					sgm41512_start_bc12_retry(chip);
					break;
				} else {
					oplus_notify_device_mode(true);
				}
				chip->bc12_done = true;

				chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB;
				oplus_set_usb_props_type(chip->oplus_charger_type);
				oplus_chg_wake_update_work();
				break;
		case REG08_SGM41512_VBUS_STAT_CDP:
				if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
					chip->bc12_retried++;
					chg_err("bc1.2 cdp retry cnt=%d\n", chip->bc12_retried);
					sgm41512_start_bc12_retry(chip);
					break;
				}
				chip->bc12_done = true;

				chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB_CDP;
				oplus_set_usb_props_type(chip->oplus_charger_type);
				oplus_notify_device_mode(true);
				oplus_chg_wake_update_work();
				break;
		case REG08_SGM41512_VBUS_STAT_DCP:
		case REG08_SGM41512_VBUS_STAT_OCP:
		case REG08_SGM41512_VBUS_STAT_FLOAT:
				chip->bc12_done = true;
				chip->oplus_charger_type = POWER_SUPPLY_TYPE_USB_DCP;
				oplus_set_usb_props_type(chip->oplus_charger_type);
				oplus_chg_wake_update_work();
				break;
		case REG08_SGM41512_VBUS_STAT_OTG_MODE:
		case REG08_SGM41512_VBUS_STAT_UNKNOWN:
		default:
			break;
		}
	}
}

static void oplus_chg_awake_init(struct chip_sgm41512 *chip)
{
	if (!chip) {
		pr_err("[%s]chip is null\n", __func__);
		return;
	}
	chip->suspend_ws = NULL;
	chip->suspend_ws = wakeup_source_register(NULL, "split chg wakelock");
	return;
}

static void oplus_chg_wakelock(struct chip_sgm41512 *chip, bool awake)
{
	static bool pm_flag = false;

	if (!chip || !chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
		pr_err("[%s] true\n", __func__);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
		pr_err("[%s] false\n", __func__);
	}
	return;
}

static void oplus_keep_resume_awake_init(struct chip_sgm41512 *chip)
{
	if (!chip) {
		pr_err("[%s]chip is null\n", __func__);
		return;
	}
	chip->keep_resume_ws = NULL;
	chip->keep_resume_ws = wakeup_source_register(NULL, "split_chg_keep_resume");
	return;
}

static void oplus_keep_resume_wakelock(struct chip_sgm41512 *chip, bool awake)
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

#define OPLUS_WAIT_RESUME_TIME	200

static irqreturn_t sgm41512_irq_handler(int irq, void *data)
{
	struct chip_sgm41512 *chip = (struct chip_sgm41512 *) data;
	bool prev_pg = false, curr_pg = false, bus_gd = false;
	struct oplus_chg_chip *g_oplus_chip = oplus_chg_get_chg_struct();
	int reg_val = 0;
	int ret = 0;

	if (!chip)
		return IRQ_HANDLED;

	if (oplus_get_otg_online_status_default()) {
		chg_err("otg,ignore\n");
		oplus_keep_resume_wakelock(chip, false);
		chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_set_usb_props_type(chip->oplus_charger_type);
		return IRQ_HANDLED;
	}

	chg_err(" sgm41512_irq_handler:enter improve irq time\n");
	oplus_keep_resume_wakelock(chip, true);

	/*for check bus i2c/spi is ready or not*/
	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err(" sgm41512_irq_handler:suspended and wait_event_interruptible %d\n", OPLUS_WAIT_RESUME_TIME);
		wait_event_interruptible_timeout(chip->wait, atomic_read(&chip->charger_suspended) == 0, msecs_to_jiffies(OPLUS_WAIT_RESUME_TIME));
	}
	prev_pg = chip->power_good;
	ret = sgm41512_read_reg(chip, REG0A_SGM41512_ADDRESS, &reg_val);
	if (ret) {
		chg_err("[%s] SGM41512_REG_0A read failed ret[%d]\n", __func__, ret);
		oplus_keep_resume_wakelock(chip, false);
		return IRQ_HANDLED;
	}
	curr_pg = bus_gd = sgm41512_get_bus_gd();

	if(curr_pg) {
		oplus_chg_wakelock(chip, true);
	}
	sgm41512_dump_registers();
	oplus_chg_check_break(bus_gd);
	if (oplus_vooc_get_fastchg_started() == true
			&& oplus_vooc_get_adapter_update_status() != 1) {
		chg_err("oplus_vooc_get_fastchg_started = true!\n", __func__);
		oplus_keep_resume_wakelock(chip, false);
		return IRQ_HANDLED;
	} else {
		chip->power_good = curr_pg;
	}
	chg_err("(%d,%d, %d, %d)\n", prev_pg, chip->power_good, curr_pg, bus_gd);


	if (!prev_pg && chip->power_good) {
		oplus_chg_wakelock(chip, true);
		sgm41512_request_dpdm(chip, true);
		chip->bc12_done = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		if (chip->use_voocphy) {
			oplus_voocphy_set_adc_enable(true);
		}
		sgm41512_set_wdt_timer(REG05_SGM41512_WATCHDOG_TIMER_40S);
		oplus_wake_up_usbtemp_thread();
		if (chip->oplus_charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			sgm41512_get_bc12(chip);
		}

		if (g_oplus_chip) {
			if (oplus_vooc_get_fastchg_to_normal() == false
					&& oplus_vooc_get_fastchg_to_warm() == false) {
				if (g_oplus_chip->authenticate
						&& g_oplus_chip->mmi_chg
						&& !g_oplus_chip->balancing_bat_stop_chg
						&& oplus_vooc_get_allow_reading()
						&& !oplus_is_rf_ftm_mode()) {
					sgm41512_enable_charging();
				}
			}
		}
		goto POWER_CHANGE;
	} else if (prev_pg && !chip->power_good) {
		sgm41512_request_dpdm(chip, false);
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
		oplus_notify_device_mode(false);
		if (chip->use_voocphy) {
			oplus_voocphy_set_adc_enable(false);
		}
		oplus_chg_wakelock(chip, false);
		goto POWER_CHANGE;
	} else if (!prev_pg && !chip->power_good) {
		chg_err("prev_pg & now_pg is false\n");
		chip->bc12_done = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		goto POWER_CHANGE;
	}
	sgm41512_get_bc12(chip);
POWER_CHANGE:
	if(dumpreg_by_irq)
		sgm41512_dump_registers();

	oplus_keep_resume_wakelock(chip, false);
	return IRQ_HANDLED;
}

static int sgm41512_enable_gpio_init(struct chip_sgm41512 *chip)
{
	if (NULL == chip) {
		return -EINVAL;
	}

	sgm41512_enable_gpio(chip, true);

	return 0;
}

static int sgm41512_irq_register(struct chip_sgm41512 *chip)
{
	int ret = 0;

	ret = devm_request_threaded_irq(chip->dev, gpio_to_irq(chip->irq_gpio), NULL,
			sgm41512_irq_handler,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"sgm41512-eint",
			chip);
	if (ret < 0) {
		chg_err("sgm41512 request_irq fail!");
		return -EFAULT;
	}

	/*In Pikachu(21291), sgm41512 is slave charger, not need the BC1.2 and irq service*/
	ret = enable_irq_wake(gpio_to_irq(chip->irq_gpio));
	if (ret != 0) {
		chg_err("enable_irq_wake: irq_gpio failed %d\n", ret);
	}

	return ret;
}

static void sgm41512_init_work_handler(struct work_struct *work)
{
	struct chip_sgm41512 *chip = NULL;

	if (charger_ic) {
		chip = charger_ic;

		sgm41512_irq_handler(0, chip);

		if (sgm41512_chg_is_usb_present())
			sgm41512_irq_handler(0, chip);
	}

	return;
}

#define INIT_WORK_NORMAL_DELAY 8000
#define INIT_WORK_OTHER_DELAY 1000


static int sgm41512_charger_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct chip_sgm41512 *chip = NULL;
	int ret = 0;

	chg_err("sgm41512 probe enter\n");

	chip = devm_kzalloc(&client->dev, sizeof(struct chip_sgm41512), GFP_KERNEL);
	if (!chip) {
		return -ENOMEM;
	}

	charger_ic = chip;
	chip->dev = &client->dev;
	chip->client = client;

	i2c_set_clientdata(client, chip);
	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->dpdm_lock);
	INIT_DELAYED_WORK(&chip->bc12_retry_work, sgm41512_bc12_retry_work);
	atomic_set(&chip->charger_suspended, 0);
	atomic_set(&chip->is_suspended, 0);
	oplus_chg_awake_init(chip);
	init_waitqueue_head(&chip->wait);
	oplus_keep_resume_awake_init(chip);
	chip->oplus_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->power_good = false;
	chip->before_suspend_icl = 0;
	chip->before_unsuspend_icl = 0;
	chip->is_sgm41512 = true;
	chip->is_bq25601d = false;
	chip->bc12_done = false;
	chip->bc12_retried = 0;
	chip->bc12_delay_cnt = 0;
	chip->batfet_reset_disable = true;

	ret = sgm41512_parse_dt(chip);
	if (ret) {
		chg_err("Couldn't parse device tree ret=%d\n", ret);
		goto err_parse_dt;
	}

	if (!chip->is_sgm41512) {
		chg_err("not support sgm41512\n");
		ret = -ENOTSUPP;
		goto err_parse_dt;
	}

	sgm41512_enable_gpio_init(chip);

	sgm41512_dump_registers();
	sgm41512_hardware_init();

	ret = device_create_file(chip->dev, &dev_attr_sgm41512_access);
	if (ret) {
		chg_err("create sgm41512_access file fail ret=%d\n", ret);
		goto err_create_file;
	}

	sgm41512_irq_register(chip);
	if (ret) {
		chg_err("Failed to register irq ret=%d\n", ret);
	}

	INIT_DELAYED_WORK(&chip->init_work, sgm41512_init_work_handler);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (NORMAL_BOOT == get_boot_mode())
#else
	if (MSM_BOOT_MODE__NORMAL == get_boot_mode())
#endif
		schedule_delayed_work(&chip->init_work, msecs_to_jiffies(INIT_WORK_NORMAL_DELAY));
	else
		schedule_delayed_work(&chip->init_work, msecs_to_jiffies(INIT_WORK_OTHER_DELAY));

	set_charger_ic(SGM41512);

	chg_err("SGM41512 probe success.");

	return 0;

err_create_file:
	device_remove_file(chip->dev, &dev_attr_sgm41512_access);
err_parse_dt:
	mutex_destroy(&chip->dpdm_lock);
	mutex_destroy(&chip->i2c_lock);
	return ret;
}

static int sgm41512_charger_remove(struct i2c_client *client)
{
	struct chip_sgm41512 *chip = i2c_get_clientdata(client);

	mutex_destroy(&chip->dpdm_lock);
	mutex_destroy(&chip->i2c_lock);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int sgm41512_pm_resume(struct device *dev)
{
	struct chip_sgm41512 *chip = NULL;
	struct i2c_client *client = to_i2c_client(dev);

	pr_err("+++ complete %s: enter +++\n", __func__);
	if (client) {
		chip = i2c_get_clientdata(client);
		if (chip) {
			wake_up_interruptible(&charger_ic->wait);
			atomic_set(&chip->charger_suspended, 0);
		}
	}

	return 0;
}

static int sgm41512_pm_suspend(struct device *dev)
{
	struct chip_sgm41512 *chip = NULL;
	struct i2c_client *client = to_i2c_client(dev);

	if (client) {
		chip = i2c_get_clientdata(client);
		if (chip)
			atomic_set(&chip->charger_suspended, 1);
	}

	return 0;
}

static const struct dev_pm_ops sgm41512_pm_ops = {
	.resume			= sgm41512_pm_resume,
	.suspend		= sgm41512_pm_suspend,
};
#else
static int sgm41512_resume(struct i2c_client *client)
{
	struct chip_sgm41512 *chip = i2c_get_clientdata(client);

	if(!chip) {
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);

	return 0;
}

static int sgm41512_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct chip_sgm41512 *chip = i2c_get_clientdata(client);

	if(!chip) {
		return 0;
	}

	atomic_set(&chip->charger_suspended, 1);

	return 0;
}
#endif

static void sgm41512_charger_shutdown(struct i2c_client *client)
{
}

static struct of_device_id sgm41512_charger_match_table[] = {
	{.compatible = "oplus,sgm41512", },
	{},
};

MODULE_DEVICE_TABLE(of, sgm41512_charger_match_table);

static const struct i2c_device_id sgm41512_i2c_device_id[] = {
	{ "sgm41512", 0x6b },
	{ },
};

MODULE_DEVICE_TABLE(i2c, sgm41512_i2c_device_id);

static struct i2c_driver sgm41512_charger_driver = {
	.driver = {
		.name = "sgm41512-charger",
		.owner = THIS_MODULE,
		.of_match_table = sgm41512_charger_match_table,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &sgm41512_pm_ops,
#endif
	},

	.probe = sgm41512_charger_probe,
	.remove = sgm41512_charger_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= sgm41512_resume,
	.suspend	= sy6974_suspend,
#endif
	.shutdown = sgm41512_charger_shutdown,
	.id_table = sgm41512_i2c_device_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(sgm41512_charger_driver);
#else
void sgm41512_charger_exit(void)
{
	i2c_del_driver(&sgm41512_charger_driver);
}

int sgm41512_charger_init(void)
{
	int ret = 0;
	chg_err(" init start\n");

	/*SGM41512 is slave charger in Pikachu(21291) */
	oplus_chg_ops_register("ext-sgm41512", &sgm41512_chg_ops);

	if (i2c_add_driver(&sgm41512_charger_driver) != 0) {
		chg_err(" failed to register sgm41512 i2c driver.\n");
	} else {
		chg_debug(" Success to register sgm41512 i2c driver.\n");
	}
	return ret;
}
#endif

MODULE_DESCRIPTION("Driver for charge IC.");
MODULE_LICENSE("GPL v2");
