// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <linux/xlog.h>
#include <linux/module.h>
#include <linux/of_gpio.h>

#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "oplus_sgm41512_charger.h"
#include "../oplus_charger.h"
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_battery.h>
#include <mt-plat/charger_class.h>
#else
#include <mt-plat/v1/charger_type.h>
#include <mt-plat/v1/mtk_battery.h>
#include <mt-plat/v1/charger_class.h>
#endif

#define CHARGER_NORMAL_TEMP_HIGH 370
#define CHARGER_NORMAL_TEMP_HIGH_MINUS 350
#define CHARGER_NORMAL_TEMP_HIGH_CURRENT 1700
#define CHARGER_NORMAL_TEMP_HIGH_REG 0x1C
#define REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA 0x0D
#define HEADSET_PLUG_LIMIT_SOC 80
#define CHARGER_VBUS_VOTLAGE_REF 2600
#define BC12_RETRY_TIMES 2

extern int oplus_get_rtc_ui_soc(void);
extern int oplus_set_rtc_ui_soc(int value);
extern void mt_usb_connect_v1(void);
extern bool oplus_voocphy_get_fastchg_ing(void);

static struct chip_sgm41512 *charger_ic = NULL;
static int aicl_result = 500;
static struct delayed_work charger_modefy_work;

static DEFINE_MUTEX(sgm41512_i2c_access);

static int sgm41512_unsuspend_charger(void);
static int sgm41512_suspend_charger(void);
static int sgm41512_enable_charging(void);
static int sgm41512_disable_charging(void);
static struct delayed_work sgm41512_irq_delay_work;
static struct delayed_work sgm41512_bc_delay_work;
static struct delayed_work sgm41512_threaded_irq_work;
bool fg_sgm41512_irq_delay_work_running = false;

struct chip_sgm41512 {
	struct i2c_client	*client;
	struct device		*dev;

	/*mtk charger relate*/
	struct charger_device *chg_dev;
	struct charger_properties chg_props;
	const char *chg_dev_name;
	enum charger_type chg_type;

	int				hw_aicl_point;
	int				sw_aicl_point;
	atomic_t			charger_suspended;
	int				irq_gpio;
	int 			pwr_gd;
	int				chip_id;
	int 			ibat_last;
	bool 			charge_normal_temp_high_flag;
	struct delayed_work check_ibat_work;
	int				headset_plugin_flag;
	int				ui_soc_old;
	int				input_current_last;
	bool			enable_headset_plugin_limit;
	int				before_suspend_icl;
};

enum power_supply_type charger_type_sgm = POWER_SUPPLY_TYPE_UNKNOWN;

static unsigned int bmt_find_closest_level(const unsigned int *pList,
		unsigned int number,
		unsigned int level)
{
	unsigned int i;
	unsigned int max_value_in_last_element;

	if (pList[0] < pList[1])
		max_value_in_last_element = 1;
	else
		max_value_in_last_element = 0;

	if (max_value_in_last_element == 1) {
		for (i = (number - 1); i != 0;
		     i--) {	/* max value in the last element */
			if (pList[i] <= level) {
				pr_debug_ratelimited("zzf_%d<=%d, i=%d\n",
					pList[i], level, i);
				return pList[i];
			}
		}

		pr_info("Can't find closest level\n");
		return pList[0];
		/* return CHARGE_CURRENT_0_00_MA; */
	} else {
		/* max value in the first element */
		for (i = 0; i < number; i++) {
			if (pList[i] <= level)
				return pList[i];
		}

		pr_info("Can't find closest level\n");
		return pList[number - 1];
		/* return CHARGE_CURRENT_0_00_MA; */
	}
}

unsigned int charging_parameter_to_value(const unsigned int
		*parameter, const unsigned int array_size,
		const unsigned int val)
{
	unsigned int i;

	pr_debug_ratelimited("array_size = %d\n", array_size);

	for (i = 0; i < array_size; i++) {
		if (val == *(parameter + i))
			return i;
	}

	pr_info("NO register value match\n");
	/* TODO: ASSERT(0);   not find the value */
	return 0;
}

static int __sgm41512_read_reg(struct chip_sgm41512 *chip, int reg, int *returnData)
{
	int ret = 0;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*returnData = ret;
	}

	return 0;
}

static int sgm41512_read_reg(struct chip_sgm41512 *chip, int reg, int *returnData)
{
	int ret = 0;

	mutex_lock(&sgm41512_i2c_access);
	ret = __sgm41512_read_reg(chip, reg, returnData);
	mutex_unlock(&sgm41512_i2c_access);
	return ret;
}

static int __sgm41512_write_reg(struct chip_sgm41512 *chip, int reg, int val)
{
	int ret = 0;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write %02x to %02x: %d\n", val, reg, ret);
		return ret;
	}

	return 0;
}

static int sgm41512_config_interface (struct chip_sgm41512 *chip, int RegNum, int val, int MASK)
{
	int sgm41512_reg = 0;
	int ret = 0;

	mutex_lock(&sgm41512_i2c_access);
	ret = __sgm41512_read_reg(chip, RegNum, &sgm41512_reg);

	/* pr_err("Reg[%x]=0x%x\n", RegNum, sgm41512_reg); */

	sgm41512_reg &= ~MASK;
	sgm41512_reg |= val;

	ret = __sgm41512_write_reg(chip, RegNum, sgm41512_reg);

	/* pr_err("write Reg[%x]=0x%x\n", RegNum, sgm41512_reg); */

	__sgm41512_read_reg(chip, RegNum, &sgm41512_reg);

	/* pr_err("Check Reg[%x]=0x%x\n", RegNum, sgm41512_reg); */
	mutex_unlock(&sgm41512_i2c_access);

	return ret;
}

static int sgm41512_usbin_input_current_limit[] = {
	100,    300,    500,    900,
	1200,   1500,   2000,   3000,
};

#define AICL_COUNT	10
static  int __maybe_unused chg_vol_avg(int *chg_vol)
{
	int chg_vol_temp = 0;
	int i = 0;

	for (i = 0; i < AICL_COUNT; i++) {
		chg_vol_temp += chg_vol[i];
	}
	chg_vol_temp /= AICL_COUNT;
	return chg_vol_temp;
}

static int sgm41512_charging_current_write_fast(int chg_cur);
extern struct oplus_chg_chip *g_oplus_chip;

static void sgm41512_check_ibat(void)
{
	int batt_temp = g_oplus_chip->temperature;
	static int high_count = 0;
	if (g_oplus_chip->led_on == true) {
		charger_ic->charge_normal_temp_high_flag = false;
		return;
	}
	pr_err("led_off,batt_temp=%d,ibat_last=%d,flag=%d\n", batt_temp, charger_ic->ibat_last, charger_ic->charge_normal_temp_high_flag);

	if (batt_temp >= CHARGER_NORMAL_TEMP_HIGH) {
		high_count++;
		if(!charger_ic->charge_normal_temp_high_flag && high_count >= 5) {
			charger_ic->charge_normal_temp_high_flag = true;
			if (charger_ic->ibat_last > CHARGER_NORMAL_TEMP_HIGH_CURRENT) {
				sgm41512_config_interface(charger_ic, REG02_SGM41512_ADDRESS, CHARGER_NORMAL_TEMP_HIGH_REG,
					REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_MASK);
			}
		}
		high_count = high_count % 5;
	} else if ((batt_temp <= CHARGER_NORMAL_TEMP_HIGH_MINUS && charger_ic->charge_normal_temp_high_flag)) {
		charger_ic->charge_normal_temp_high_flag = false;
		sgm41512_charging_current_write_fast(charger_ic->ibat_last);
	}
}

void sgm41512_switch_headset_plugin(void)
{
	struct power_supply *battery_psy;
	union power_supply_propval pval = {0, };
	int ui_soc = 0;
	int reg_val = 0;
	int input_current_old;
	battery_psy = power_supply_get_by_name("battery");
	power_supply_get_property(battery_psy, POWER_SUPPLY_PROP_CAPACITY, &pval);
	ui_soc = pval.intval;

	if(charger_ic->chip_id != 1)
		return;
	if(charger_ic == NULL)
		return;
	if(charger_ic->enable_headset_plugin_limit == 0)
		return;
	charger_ic->headset_plugin_flag = 1;
	pr_err("plug_in flag=%d,ui_soc=%d", charger_ic->headset_plugin_flag, ui_soc);
	if(ui_soc >= HEADSET_PLUG_LIMIT_SOC) {
	sgm41512_read_reg(charger_ic, REG00_SGM41512_ADDRESS, &reg_val);
	charger_ic->input_current_last = reg_val;
	if(reg_val >= REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA)
		sgm41512_config_interface(charger_ic, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	}
}

void sgm41512_switch_headset_plugout(void)
{
	if(charger_ic->chip_id != 1)
		return;
	if(charger_ic == NULL)
		return;
	if(charger_ic->enable_headset_plugin_limit == 0)
		return;
	charger_ic->headset_plugin_flag = 0;
	pr_err("plug_out flag=%d", charger_ic->headset_plugin_flag);
	if(charger_ic->input_current_last != 0)
		sgm41512_config_interface(charger_ic, REG00_SGM41512_ADDRESS, charger_ic->input_current_last, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
}

void sgm41512_check_ibus(void)
{
	struct power_supply *battery_psy;
	union power_supply_propval pval = {0, };
	int ui_soc = 0;
	int reg_val;
	battery_psy = power_supply_get_by_name("battery");
	power_supply_get_property(battery_psy, POWER_SUPPLY_PROP_CAPACITY, &pval);
	ui_soc = pval.intval;
	pr_err("sgm41512_check_ibus plug_in flag=%d,ui_soc=%d", charger_ic->headset_plugin_flag, ui_soc);
	if(charger_ic->headset_plugin_flag == 0) {
		return;
	}

	if((charger_ic->ui_soc_old < HEADSET_PLUG_LIMIT_SOC)&&(ui_soc >= HEADSET_PLUG_LIMIT_SOC)) {
		sgm41512_read_reg(charger_ic, REG00_SGM41512_ADDRESS, &reg_val);
		charger_ic->input_current_last = reg_val;
		if(reg_val >= REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA)
			sgm41512_config_interface(charger_ic, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	} else if ((charger_ic->ui_soc_old >= HEADSET_PLUG_LIMIT_SOC)&&(ui_soc < HEADSET_PLUG_LIMIT_SOC)) {
		if(charger_ic->input_current_last != 0)
			sgm41512_config_interface(charger_ic, REG00_SGM41512_ADDRESS, charger_ic->input_current_last, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	}

	charger_ic->ui_soc_old = ui_soc;
}

static void sgm41512_check_ibat_work(struct work_struct *work)
{
	sgm41512_check_ibat();
	sgm41512_check_ibus();
	schedule_delayed_work(&charger_ic->check_ibat_work, round_jiffies_relative(msecs_to_jiffies(10000)));
}

#define SGM41512_INLIM_OFFSET 100
#define SGM41512_INLIM_STEP   100
#define SGM41512_INLIM_SHIFT  0
static int sgm41512_input_current_limit_write(int value)
{
	int rc = 0;
	int i = 0;
	int j = 0;
	u8 reg_val;
	int chg_vol = 0;
	int aicl_point_temp = 0;
	int chg_vol_all[10] = {0};
	struct chip_sgm41512 *chip = charger_ic;
	struct power_supply *battery_psy;
	union power_supply_propval pval = {0, };
	int ui_soc = 0;
	battery_psy = power_supply_get_by_name("battery");
	power_supply_get_property(battery_psy, POWER_SUPPLY_PROP_CAPACITY, &pval);
	ui_soc = pval.intval;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}
	if (value <= 0) {
		 rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, 0, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		pr_err("%s, aicr <= 0, disable sub ic\n", __func__);
	} else {
		reg_val = (value - SGM41512_INLIM_OFFSET) / SGM41512_INLIM_STEP << SGM41512_INLIM_SHIFT;
		pr_err("%s, aicr = %d,reg = %d\n", __func__, value, reg_val);
		rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, reg_val, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	}

	pr_err("usb input max current limit=%d setting %02x\n", value, i);
	aicl_point_temp = chip->sw_aicl_point;

	if (value < 300) {
		j = 0;
		goto aicl_end;
	} else if (value < 500) {
		j = 1;
		goto aicl_end;
	}

	j = 2; /* 500 */
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_500MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		j = 2;
		goto aicl_pre_step;
	} else if (value < 900)
		goto aicl_end;

	j = 3; /* 900 */
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_900MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		j = j - 1;
		goto aicl_pre_step;
	} else if (value < 1200)
		goto aicl_end;

	j = 4; /* 1200 */
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1200MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		j = j - 1;
		goto aicl_pre_step;
	}

	j = 5; /* 1500 */
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1500MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		j = j - 2; /* We DO NOT use 1.2A here */
		goto aicl_pre_step;
	} else if (value < 1500) {
		j = j - 1; /* We use 1.2A here */
		goto aicl_end;
	} else if (value < 2000)
		goto aicl_end;

	j = 6; /* 2000 */
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_2000MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);

	for (i = 0; i < AICL_COUNT; i++) {
		chg_vol = battery_get_vbus();
		chg_vol_all[i] = chg_vol;
		usleep_range(5000, 5000);
	}
	chg_vol = chg_vol_avg(chg_vol_all);

	if (chg_vol < aicl_point_temp) {
		j = j - 1;
		goto aicl_pre_step;
	} else if (value < 3000)
		goto aicl_end;

	j = 7; /* 3000 */
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_3000MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		j = j - 1;
		goto aicl_pre_step;
	} else if (value >= 3000)
		goto aicl_end;

aicl_pre_step:
	pr_err("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, j,
				sgm41512_usbin_input_current_limit[j], aicl_point_temp);
	goto aicl_rerun;
aicl_end:
	pr_err("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, j, sgm41512_usbin_input_current_limit[j], aicl_point_temp);
	goto aicl_rerun;
aicl_rerun:
	if ((j >= 2) && (j <= ARRAY_SIZE(sgm41512_usbin_input_current_limit) - 1))
		aicl_result = sgm41512_usbin_input_current_limit[j];
	switch (j) {
	case 0:
		sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_100MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		break;
	case 1:
		sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_300MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		break;
	case 2:
		sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_500MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		break;
	case 3:
		sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_900MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		break;
	case 4:
		sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1200MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		break;
	case 5:
		if((charger_ic->headset_plugin_flag == 1)&&(ui_soc >= HEADSET_PLUG_LIMIT_SOC)) {
			charger_ic->input_current_last = REG00_SGM41512_INPUT_CURRENT_LIMIT_1500MA;
			sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		} else {
			sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1500MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		}
		break;
	case 6:
		if((charger_ic->headset_plugin_flag == 1)&&(ui_soc >= HEADSET_PLUG_LIMIT_SOC)) {
			charger_ic->input_current_last = REG00_SGM41512_INPUT_CURRENT_LIMIT_2000MA;
			sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		} else {
			sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_2000MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		}
		break;
	case 7:
		if((charger_ic->headset_plugin_flag == 1)&&(ui_soc >= HEADSET_PLUG_LIMIT_SOC)) {
			charger_ic->input_current_last = REG00_SGM41512_INPUT_CURRENT_LIMIT_3000MA;
			sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_1400MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		} else {
			sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, REG00_SGM41512_INPUT_CURRENT_LIMIT_3000MA, REG00_SGM41512_INPUT_CURRENT_LIMIT_MASK);
		}
		break;
	default:
		break;
	}

	return rc;
}

static int sgm41512_charging_current_write_fast(int chg_cur)
{
	int rc = 0;
	int value = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	pr_err("chg_cur = %d\r\n", chg_cur);

	if ((charger_ic->charge_normal_temp_high_flag == true)&&(chg_cur > CHARGER_NORMAL_TEMP_HIGH_CURRENT)) {
		charger_ic->ibat_last = chg_cur;
		chg_cur = CHARGER_NORMAL_TEMP_HIGH_CURRENT;
	} else {
		charger_ic->ibat_last = chg_cur;
	}
	if (chg_cur > REG02_SGM41512_MAX_FAST_CHG_CURRENT_MA
			|| chg_cur < REG02_SGM41512_MIN_FAST_CHG_CURRENT_MA)
		chg_cur = REG02_SGM41512_MAX_FAST_CHG_CURRENT_MA;
	value = (chg_cur - REG02_SGM41512_MIN_FAST_CHG_CURRENT_MA) / REG02_SGM41512_FAST_CHG_CURRENT_STEP_MA;
	value <<= REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_SHIFT;

	rc = sgm41512_config_interface(chip, REG02_SGM41512_ADDRESS, value,
			REG02_SGM41512_FAST_CHG_CURRENT_LIMIT_MASK);
	if (rc < 0) {
		pr_err("Couldn't write fast charge current rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_set_vindpm_vol(int vol)
{
	int rc = 0;
	int value = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	if (vol < REG06_SGM41512_VINDPM_OFFSET)
		vol = REG06_SGM41512_VINDPM_OFFSET;
	value = (vol - REG06_SGM41512_VINDPM_OFFSET) / REG06_SGM41512_VINDPM_STEP_MV;
	value <<= REG06_SGM41512_VINDPM_SHIFT;

	rc = sgm41512_config_interface(chip, REG06_SGM41512_ADDRESS,
			value, REG06_SGM41512_VINDPM_MASK);
	return rc;
}

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

/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int sgm41512_chg_get_dyna_aicl_result(void)
{
	return aicl_result;
}

static void sgm41512_set_aicl_point(int vbatt)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return;
	}

	if (chip->hw_aicl_point == 4400 && vbatt > 4100) {
		chip->hw_aicl_point = 4500;
		chip->sw_aicl_point = 4550;
		sgm41512_set_vindpm_vol(chip->hw_aicl_point);
	} else if (chip->hw_aicl_point == 4500 && vbatt <= 4100) {
		chip->hw_aicl_point = 4400;
		chip->sw_aicl_point = 4500;
		sgm41512_set_vindpm_vol(chip->hw_aicl_point);
	}
}

static int sgm41512_set_enable_volatile_writes(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	return rc;
}

static int sgm41512_set_complete_charge_timeout(int val)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	if (val == OVERTIME_AC) {
		val = REG05_SGM41512_CHG_SAFETY_TIMER_ENABLE | REG05_SGM41512_FAST_CHG_TIMEOUT_10H;
	} else if (val == OVERTIME_USB) {
		val = REG05_SGM41512_CHG_SAFETY_TIMER_ENABLE | REG05_SGM41512_FAST_CHG_TIMEOUT_10H;
	} else {
		val = REG05_SGM41512_CHG_SAFETY_TIMER_DISABLE | REG05_SGM41512_FAST_CHG_TIMEOUT_10H;
	}

	rc = sgm41512_config_interface(chip, REG05_SGM41512_ADDRESS, val,
			REG05_SGM41512_CHG_SAFETY_TIMER_MASK | REG05_SGM41512_FAST_CHG_TIMEOUT_MASK);
	if (rc < 0) {
		pr_err("Couldn't complete charge timeout rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	int value = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	if (vfloat_mv < REG04_SGM41512_MIN_FLOAT_MV) {
		pr_err("bad vfloat_mv:%d,return\n", vfloat_mv);
		return 0;
	}
	value = (vfloat_mv - REG04_SGM41512_MIN_FLOAT_MV) / REG04_SGM41512_VFLOAT_STEP_MV;
	value <<= REG04_SGM41512_CHG_VOL_LIMIT_SHIFT;
	pr_err("sgm41512_set_float_voltage vfloat_mv = %d value=%d\n", vfloat_mv, value);

	rc = sgm41512_config_interface(chip, REG04_SGM41512_ADDRESS, value, REG04_SGM41512_CHG_VOL_LIMIT_MASK);
	if (rc < 0) {
		pr_err("Couldn't set float voltage rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_set_prechg_current(int ipre_ma)
{
	int value = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	if (ipre_ma < REG03_SGM41512_MIN_PRE_CHG_CURRENT_MA)
		ipre_ma = REG03_SGM41512_MIN_PRE_CHG_CURRENT_MA;
	if (ipre_ma > REG03_SGM41512_MAX_PRE_CHG_CURRENT_MA)
		ipre_ma = REG03_SGM41512_MAX_PRE_CHG_CURRENT_MA;
	value = (ipre_ma - REG03_SGM41512_MIN_PRE_CHG_CURRENT_MA) / REG03_SGM41512_PRE_CHG_CURRENT_STEP_MA;
	value <<= REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_SHIFT;

	return sgm41512_config_interface(chip, REG03_SGM41512_ADDRESS, value,
			REG03_SGM41512_PRE_CHG_CURRENT_LIMIT_MASK);
}

static int sgm41512_set_termchg_current(int term_curr)
{
	int value = 0;
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	if (term_curr < REG03_SGM41512_MIN_TERM_CHG_CURRENT_MA) {
		term_curr = REG03_SGM41512_MIN_TERM_CHG_CURRENT_MA;
	}
	value = (term_curr - REG03_SGM41512_MIN_TERM_CHG_CURRENT_MA) / REG03_SGM41512_TERM_CHG_CURRENT_STEP_MA;
	value <<= REG03_SGM41512_TERM_CHG_CURRENT_LIMIT_SHIFT;
	pr_err("value=%d\n", value);

	rc = sgm41512_config_interface(chip, REG03_SGM41512_ADDRESS, value,
			REG03_SGM41512_TERM_CHG_CURRENT_LIMIT_MASK);
	if (rc < 0) {
		pr_err("Couldn't set termination curren rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_set_rechg_voltage(int recharge_mv)
{
	int reg = 0;
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	/* set recharge voltage */
	if (recharge_mv >= 200) {
		reg = REG04_SGM41512_RECHG_THRESHOLD_VOL_200MV;
	} else {
		reg = REG04_SGM41512_RECHG_THRESHOLD_VOL_100MV;
	}
	rc = sgm41512_config_interface(chip, REG04_SGM41512_ADDRESS, reg, REG04_SGM41512_RECHG_THRESHOLD_VOL_MASK);
	if (rc) {
		pr_err("Couldn't set rechg threshold rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_set_chging_term_disable(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG05_SGM41512_ADDRESS,
			REG05_SGM41512_TERMINATION_DISABLE, REG05_SGM41512_TERMINATION_MASK);

	return rc;
}

static int sgm41512_kick_wdt(void)
{
	return 0;
}


static int sgm41512_enable_charging(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;
	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_CHARGING_ENABLE, REG01_SGM41512_CHARGING_MASK);
	if (rc < 0) {
		pr_err("Couldn'tsgm41512_enable_charging rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_disable_charging(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_CHARGING_DISABLE, REG01_SGM41512_CHARGING_MASK);
	if (rc < 0) {
		pr_err("Couldn't sgm41512_disable_charging  rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_check_charging_enable(void)
{
	int rc = 0;
	int reg_val = 0;
	bool charging_enable = false;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG01_SGM41512_ADDRESS, &reg_val);
	if (rc) {
		pr_err("Couldn't read REG01_SGM41512_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charging_enable = ((reg_val & REG01_SGM41512_CHARGING_MASK) == REG01_SGM41512_CHARGING_ENABLE) ? 1 : 0;

	return charging_enable;
}

static int sgm41512_registers_read_full(void)
{
	int rc = 0;
	int reg_full = 0;
	int reg_ovp = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	rc = sgm41512_read_reg(chip, REG08_SGM41512_ADDRESS, &reg_full);
	if (rc) {
		pr_err("Couldn't read REG08_SGM41512_ADDRESS rc = %d\n", rc);
		return 0;
	}
	reg_full = ((reg_full & REG08_SGM41512_CHG_STAT_MASK) == REG08_SGM41512_CHG_STAT_CHG_TERMINATION) ? 1 : 0;

	rc = sgm41512_read_reg(chip, REG09_SGM41512_ADDRESS, &reg_ovp);
	if (rc) {
		pr_err("Couldn't read REG09_SGM41512_ADDRESS rc = %d\n", rc);
		return 0;
	}
	reg_ovp = ((reg_ovp & REG09_SGM41512_BAT_FAULT_MASK) == REG09_SGM41512_BAT_FAULT_BATOVP) ? 1 : 0;

	/* pr_err("sgm41512_registers_read_full, reg_full = %d, reg_ovp = %d\r\n", reg_full, reg_ovp); */
	return (reg_full || reg_ovp);
}

static int oplus_otg_online = 0;

int sgm41512_otg_enable(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}
#endif /* CONFIG_OPLUS_CHARGER_MTK */
	oplus_otg_online = 1;
	sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS,
			REG00_SGM41512_SUSPEND_MODE_DISABLE, REG00_SGM41512_SUSPEND_MODE_MASK);

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_OTG_ENABLE, REG01_SGM41512_OTG_MASK);
	if (rc) {
		pr_err("Couldn't enable OTG mode rc=%d\n", rc);
	} else {
		pr_err("sgm41512_otg_enable rc=%d\n", rc);
	}

	oplus_otg_online = 1;
	oplus_chg_set_otg_online(true);
	return rc;
}

int sgm41512_otg_disable(void)
{
	int rc = 0;
	int val_buf;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		return 0;
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}
#endif /* CONFIG_OPLUS_CHARGER_MTK */

	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS,
			REG01_SGM41512_OTG_DISABLE, REG01_SGM41512_OTG_MASK);
	if (rc)
		pr_err("Couldn't disable OTG mode rc=%d\n", rc);
	else
		pr_err("sgm41512_otg_disable rc=%d\n", rc);

	oplus_otg_online = 0;
	oplus_chg_set_otg_online(false);

	return rc;
}

int oplus_sgm41512_get_charger_type(void)
{
	return charger_type_sgm;
}

void oplus_sgm41512_set_chargerid_switch_val(int value)
{
	return;
}

int oplus_sgm41512_get_chargerid_switch_val(void)
{
	return 0;
}

bool oplus_sgm41512_need_to_check_ibatt(void)
{
	return false;
}

int oplus_sgm41512_chg_get_charger_subtype(void)
{
	return CHARGER_SUBTYPE_DEFAULT;
}

int oplus_sgm41512_set_hz_mode(bool en)
{
	return sgm41512_suspend_charger();
}

static int sgm41512_suspend_charger(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;
	union power_supply_propval pval = {0, };
	struct power_supply *bat_psy = NULL;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	bat_psy = power_supply_get_by_name("battery");
	if(bat_psy == NULL)
		return false;

	power_supply_get_property(bat_psy, POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE, &pval);
	if ((pval.intval == 1) && (get_boot_mode() != META_BOOT)) {
		return rc;
	}

	pr_err("suspend sgm41512\n");
	chip->before_suspend_icl = sgm41512_get_usb_icl();
	sgm41512_input_current_limit_write(REG00_SGM41512_INPUT_CURRENT_LIMIT_500MA);
	pr_err("before_suspend_icl = %d\n", chip->before_suspend_icl);

	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS,
			REG00_SGM41512_SUSPEND_MODE_ENABLE, REG00_SGM41512_SUSPEND_MODE_MASK);
	if (rc < 0) {
		pr_err("Couldn't sgm41512_suspend_charger rc = %d\n", rc);
	} else {
		pr_err("rc = %d\n", rc);
	}

	return rc;
}

static int sgm41512_unsuspend_charger(void)
{
	int rc = 0, val_reg = 0, val_buf = 0, vbus = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}
	pr_err("unsuspend sgm41512\n");
	pr_err("before_suspend_icl = %d\n", chip->before_suspend_icl);

	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS,
			REG00_SGM41512_SUSPEND_MODE_DISABLE, REG00_SGM41512_SUSPEND_MODE_MASK);
	if (rc < 0) {
		pr_err("Couldn't sgm41512_unsuspend_charger rc = %d\n", rc);
	} else {
		sgm41512_read_reg(charger_ic, REG08_SGM41512_ADDRESS, &val_buf);
		val_buf = val_buf & REG08_SGM41512_VBUS_STAT_MASK;
		pr_err("val_buf = %d\n", val_buf);
		pr_err("sgm41512_unsuspend_charger rc = %d\n", rc);
		if (chip->before_suspend_icl > 0 && val_buf != REG08_SGM41512_VBUS_STAT_DCP) {
			sgm41512_input_current_limit_write(chip->before_suspend_icl);
			chip->before_suspend_icl = 0;
		}
	}
	/* default enable charging */
	sgm41512_enable_charging();
	/* trigger once irq  usb plug out*/
	sgm41512_read_reg(chip, REG0A_SGM41512_ADDRESS, &val_reg);
	vbus = battery_get_vbus();
	if (((val_reg & REG0A_SGM41512_VBUS_GOOD_STAT_MASK) >>7) == 0 && vbus < CHARGER_VBUS_VOTLAGE_REF) {
		pr_err("unsuspend sgm41512 trigger once irq val_reg =%d vbus=%d\n", val_reg, vbus);
		schedule_delayed_work(&sgm41512_threaded_irq_work, 0);
	}
	return rc;
}

static int sgm41512_reset_charger(void)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}
/*
	rc = sgm41512_config_interface(chip, REG00_SGM41512_ADDRESS, 0x0, 0xFF);
	if (rc < 0) {
	pr_err("Couldn't reset REG00 rc = %d\n", rc);
	}
*/
	rc = sgm41512_config_interface(chip, REG01_SGM41512_ADDRESS, 0x1A, 0xFF);
	if (rc < 0) {
		pr_err("Couldn't reset REG01 rc = %d\n", rc);
	}
	rc = sgm41512_config_interface(chip, REG02_SGM41512_ADDRESS, 0x85, 0xFF);
	if (rc < 0) {
		pr_err("Couldn't reset REG02 rc = %d\n", rc);
	}
	rc = sgm41512_config_interface(chip, REG03_SGM41512_ADDRESS, 0x22, 0xFF);
	if (rc < 0) {
		pr_err("Couldn't reset REG03 rc = %d\n", rc);
	}
	rc = sgm41512_config_interface(chip, REG04_SGM41512_ADDRESS, 0x88, 0xFF);
	if (rc < 0) {
		pr_err("Couldn't reset REG04 rc = %d\n", rc);
	}
	rc = sgm41512_config_interface(chip, REG05_SGM41512_ADDRESS, 0x0F, 0xFF);
	if (rc < 0) {
		pr_err("Couldn't reset REG05 rc = %d\n", rc);
	}
	rc = sgm41512_config_interface(chip, REG06_SGM41512_ADDRESS, 0x56, 0xFF);
	if (rc < 0) {
		pr_err("Couldn't reset REG06 rc = %d\n", rc);
	}
	rc = sgm41512_config_interface(chip, REG07_SGM41512_ADDRESS, 0x48, 0xFF);
	if (rc < 0) {
		pr_err("Couldn't reset REG07 rc = %d\n", rc);
	}
	rc = sgm41512_config_interface(chip, REG0A_SGM41512_ADDRESS, 0x3, 0x03);
	if (rc < 0) {
		pr_err("Couldn't reset REG0A rc = %d\n", rc);
	}
	pr_err("rc = %d\n", rc);

	return rc;
}

static bool sgm41512_check_charger_resume(void)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return false;
	} else {
		return true;
	}
}

#define DUMP_REG_LOG_CNT_30S  1
static void sgm41512_dump_registers(void)
{
	int rc = 0;
	int addr = 0;
	unsigned int val_buf[SGM41512_REG_NUMBER] = {0x0};
	static int dump_count = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return;
	}

	for (addr = SGM41512_FIRST_REG; addr <= SGM41512_LAST_REG; addr++) {
		rc = sgm41512_read_reg(chip, addr, &val_buf[addr]);
		if (rc) {
			 pr_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
			 return;
		}
	}

	if (dump_count == DUMP_REG_LOG_CNT_30S) {
		dump_count = 0;
		pr_err("sgm41512_reg: [0x%02x,0x%02x,0x%02x,0x%02x], [0x%02x,0x%02x,0x%02x,0x%02x], [0x%02x,0x%02x,0x%02x,0x%02x]\n",
			val_buf[0], val_buf[1], val_buf[2], val_buf[3], val_buf[4], val_buf[5],
			val_buf[6], val_buf[7], val_buf[8], val_buf[9], val_buf[10], val_buf[11]);
	}
	dump_count++;

	return;
}

static int sgm41512_check_registers(void)
{
	int rc = 0;
	int addr = 0;
	unsigned int val_buf[SGM41512_REG_NUMBER] = {0x0};
	struct chip_sgm41512 *chip = charger_ic;

	if (atomic_read(&chip->charger_suspended) == 1) {
		pr_err("charger in suspended.\n");
		return 0;
	}

	for (addr = SGM41512_FIRST_REG; addr <= SGM41512_LAST_REG; addr++) {
		rc = sgm41512_read_reg(chip, addr, &val_buf[addr]);
		if (rc) {
			 pr_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
			 return -1;
		}
	}

	return 0;
}

static int sgm41512_get_chg_current_step(void)
{
	return 60;
}

static int sgm41512_hardware_init(void)
{
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		pr_err("%s chip null,return\n", __func__);
		return -1;
	}

	/* must be before set_vindpm_vol and set_input_current */
	chip->hw_aicl_point = 4400;
	chip->sw_aicl_point = 4500;

	sgm41512_reset_charger();
	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
		sgm41512_float_voltage_write(4400);
		msleep(100);
	}

	sgm41512_float_voltage_write(4370);

	sgm41512_set_enable_volatile_writes();

	sgm41512_set_complete_charge_timeout(OVERTIME_DISABLED);

	sgm41512_set_prechg_current(300);

	sgm41512_charging_current_write_fast(512);

	sgm41512_set_termchg_current(150);

	sgm41512_set_rechg_voltage(100);

	sgm41512_set_vindpm_vol(chip->hw_aicl_point);

	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
			|| get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
		sgm41512_suspend_charger();
		sgm41512_disable_charging();
	} else {
		sgm41512_unsuspend_charger();
	}

	sgm41512_enable_charging();

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

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		pr_err(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	pr_err(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

extern bool oplus_chg_get_shortc_hw_gpio_status(void);

extern void kernel_power_off(void);

static void mt_power_off(void)
{
	kernel_power_off();
}

static int oplus_sgm41512_get_rtc_spare_oplus_fg_value(void)
{
	return 0;
}

static int oplus_sgm41512_set_rtc_spare_oplus_fg_value(int soc)
{
	return 0;
}

static int sgm41512_enable_shipmode(bool en)
{
	int rc = 0;
	struct chip_sgm41512 *chip = charger_ic;
	if (en) {
		rc = sgm41512_config_interface(chip, REG07_SGM41512_ADDRESS, 0x20, 0x20);
		if (rc < 0) {
			chg_err("set shipmode REG07 enable error rc = %d\n", rc);
		}
	} else {
		rc = sgm41512_config_interface(chip, REG07_SGM41512_ADDRESS, 0x00, 0x20);
		if (rc < 0) {
			chg_err("set shipmode REG07 disable error rc = %d\n", rc);
		}
	}

	return 0;
}

int oplus_sgm41512_get_pd_type(void)
{
	return 0;
}

int oplus_sgm41512_enable_qc_detect(void)
{
	return 0;
}

struct oplus_chg_operations  oplus_chg_sgm41512_ops = {
	.dump_registers = sgm41512_dump_registers,
	.kick_wdt = sgm41512_kick_wdt,
	.hardware_init = sgm41512_hardware_init,
	.charging_current_write_fast = sgm41512_charging_current_write_fast,
	.set_aicl_point = sgm41512_set_aicl_point,
	.input_current_write = sgm41512_input_current_limit_write,
	.float_voltage_write = sgm41512_float_voltage_write,
	.term_current_set = sgm41512_set_termchg_current,
	.charging_enable = sgm41512_enable_charging,
	.charging_disable = sgm41512_disable_charging,
	.get_charging_enable = sgm41512_check_charging_enable,
	.charger_suspend = sgm41512_suspend_charger,
	.charger_unsuspend = sgm41512_unsuspend_charger,
	.set_rechg_vol = sgm41512_set_rechg_voltage,
	.reset_charger = sgm41512_reset_charger,
	.read_full = sgm41512_registers_read_full,
	.otg_enable = sgm41512_otg_enable,
	.otg_disable = sgm41512_otg_disable,
	.set_charging_term_disable = sgm41512_set_chging_term_disable,
	.check_charger_resume = sgm41512_check_charger_resume,

	.get_charger_type = oplus_sgm41512_get_charger_type,
	.get_charger_volt = battery_get_vbus,
	/*int (*get_charger_current)(void);*/
	.get_chargerid_volt = mt_get_chargerid_volt,
	/*.set_chargerid_switch_val = oplus_sgm41512_set_chargerid_switch_val,*/
	/*.get_chargerid_switch_val = oplus_sgm41512_get_chargerid_switch_val,*/
	.set_chargerid_switch_val = mt_set_chargerid_switch_val,
	.get_chargerid_switch_val  = mt_get_chargerid_switch_val,
	.check_chrdet_status = (bool (*)(void))pmic_chrdet_status,

	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = oplus_sgm41512_get_rtc_spare_oplus_fg_value,
	.set_rtc_soc = oplus_sgm41512_set_rtc_spare_oplus_fg_value,
	.set_power_off = mt_power_off,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
#else
	.usb_connect = mt_usb_connect_v1,
	.usb_disconnect = mt_usb_disconnect_v1,
#endif
	.get_chg_current_step = sgm41512_get_chg_current_step,
	.need_to_check_ibatt = oplus_sgm41512_need_to_check_ibatt,
	.get_dyna_aicl_result = sgm41512_chg_get_dyna_aicl_result,
	.get_shortc_hw_gpio_status = oplus_chg_get_shortc_hw_gpio_status,
	/* void (*check_is_iindpm_mode) (void); */
	.oplus_chg_get_pd_type = oplus_sgm41512_get_pd_type,
	.oplus_chg_pd_setup = NULL,
	.get_charger_subtype = oplus_sgm41512_chg_get_charger_subtype,
	.set_qc_config = NULL,
	.enable_qc_detect = oplus_sgm41512_enable_qc_detect,
	.oplus_chg_set_high_vbus = NULL,
	.enable_shipmode = sgm41512_enable_shipmode,
	.oplus_chg_set_hz_mode = oplus_sgm41512_set_hz_mode,
};

void sgm41512_set_boost_lim(unsigned int val)
{
	unsigned int ret = 0;
	struct chip_sgm41512 *chip = charger_ic;
	if(val == 1) {
		val = 0x80;
        }
	ret = sgm41512_config_interface(chip, REG02_SGM41512_ADDRESS,
			val, REG02_SGM41512_OTG_CURRENT_LIMIT_MASK);
}

static int sgm41512_enable_otg(struct charger_device *chg_dev, bool en)
{
	return en ? sgm41512_otg_enable() : sgm41512_otg_disable();
}

/* BQ25601 REG0A BOOST_LIM[2:0], mA */
const unsigned int BOOST_CURRENT_LIMIT[] = {
	500, 1200
};

static int sgm41512_set_boost_current_limit(struct charger_device *chg_dev,
	u32 uA)
{
	int ret = 0;
	u32 array_size = 0;
	u32 boost_ilimit = 0;
	u8 boost_reg = 0;

	uA /= 1000;
	array_size = ARRAY_SIZE(BOOST_CURRENT_LIMIT);
	boost_ilimit = bmt_find_closest_level(BOOST_CURRENT_LIMIT, array_size,
					      uA);
	boost_reg = charging_parameter_to_value(BOOST_CURRENT_LIMIT,
				array_size, boost_ilimit);
	sgm41512_set_boost_lim(boost_reg);

	return ret;
}

static int sgm41512_enable_charger_type_det(struct charger_device *dev, bool en) {
	int reg_val = 0;
	struct chip_sgm41512 *chip = charger_ic;

	pr_err("%s en=%d\n", __func__, en);

	if (!chip) {
		pr_err("%s chip null,return\n", __func__);
		return -1;
	}

	if (en == 0) {
		chip->before_suspend_icl = 0;
	}
	return 0;
}

static struct charger_ops sgm41512_chg_ops = {
	/* OTG */
	.enable_otg = sgm41512_enable_otg,
	.set_boost_current_limit = sgm41512_set_boost_current_limit,
	.enable_chg_type_det = sgm41512_enable_charger_type_det,
};

static void register_charger_devinfo(void)
{
	int ret = 0;
	char *version = "sgm41512";
	char *manufacture = "TI";
	ret = register_device_proc("charger", version, manufacture);
	if (ret)
		pr_err("register_charger_devinfo fail\n");
}

extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);
extern bool is_usb_rdy(void);
static void hw_bc12_init(void)
{
	int timeout = 350;
	static bool first_connect = true;

	if (first_connect == true) {
		/* add make sure USB Ready */
		if (is_usb_rdy() == false) {
			pr_err("CDP, block\n");
			while (is_usb_rdy() == false && timeout > 0) {
				msleep(100);
				timeout--;
			}
			if (timeout == 0) {
				pr_err("CDP, timeout\n");
			} else {
				pr_err("CDP, free, timeout:%d\n", timeout);
			}
		} else {
			pr_err("CDP, PASS\n");
		}
		first_connect = false;
	}
	Charger_Detect_Init();
}

/* #ifdef VENDOR_EDIT */
static enum charger_type MTK_CHR_Type_num;

enum charger_type mt_charger_type_detection_sgm41512(void)
{
	int rc = 0;
	int chgr_type_addr = 0;
	int val_buf = 0;
	int count = 0;
	int i = 0;
	if (!charger_ic) {
		pr_err("%s charger_ic null, return\n", __func__);
		return CHARGER_UNKNOWN;
	}

	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
			|| get_boot_mode() == ADVMETA_BOOT
			|| get_boot_mode() == ATE_FACTORY_BOOT) {
		return STANDARD_HOST;
	}

	chgr_type_addr = REG08_SGM41512_ADDRESS;

	hw_bc12_init();
	if (charger_ic->chip_id == 2) {
		usleep_range(10000, 10200);
		sgm41512_config_interface(charger_ic, REG07_SGM41512_ADDRESS,
				REG07_SGM41512_IINDET_EN_FORCE_DET, REG07_SGM41512_IINDET_EN_MASE);
		sgm41512_dump_registers();
	}
	rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
	val_buf &= REG07_SGM41512_IINDET_EN_MASE;
	while (val_buf == REG07_SGM41512_IINDET_EN_FORCE_DET && count < 20) {
		count++;
		rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
		val_buf &= REG07_SGM41512_IINDET_EN_MASE;
		usleep_range(50000, 50200);
	}

	if (count == 20) {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
		return MTK_CHR_Type_num;
	}

	rc = sgm41512_read_reg(charger_ic, chgr_type_addr, &val_buf);

	val_buf = val_buf & REG08_SGM41512_VBUS_STAT_MASK;
	pr_err("mt_charger_type_detection3, val_buf=[0x%x] count = %d\n", val_buf, count);

	if (val_buf == REG08_SGM41512_VBUS_STAT_UNKNOWN) {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
	} else if (val_buf == REG08_SGM41512_VBUS_STAT_SDP) {
		MTK_CHR_Type_num = STANDARD_HOST;
	} else if (val_buf == REG08_SGM41512_VBUS_STAT_CDP) {
		MTK_CHR_Type_num = CHARGING_HOST;
	} else if (val_buf == REG08_SGM41512_VBUS_STAT_DCP
			|| val_buf == REG08_SGM41512_VBUS_STAT_OCP
			|| val_buf == REG08_SGM41512_VBUS_STAT_FLOAT) {
		MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
	} else {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
	}

	/* the 2-3nd detection */
	for (i = 0; i < BC12_RETRY_TIMES; i++) {
		if (((MTK_CHR_Type_num == CHARGER_UNKNOWN) || (MTK_CHR_Type_num == STANDARD_HOST)) && charger_ic->pwr_gd) {
			pr_err("mt_charger_type_detection: %dnd...\n", i + 2);
			sgm41512_config_interface(charger_ic, REG07_SGM41512_ADDRESS,
					REG07_SGM41512_IINDET_EN_FORCE_DET, REG07_SGM41512_IINDET_EN_MASE);
			sgm41512_dump_registers();

			rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
			val_buf &= REG07_SGM41512_IINDET_EN_MASE;
			count = 0;
			while (val_buf == REG07_SGM41512_IINDET_EN_FORCE_DET && count < 20) {
				count++;
				rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
				val_buf &= REG07_SGM41512_IINDET_EN_MASE;
				usleep_range(50000, 50200);
			}
			pr_err("mt_charger_type_detection: %dnd, count=%d\n", i + 2, count);
			if (count == 20) {
				MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
				return MTK_CHR_Type_num;
			}

			rc = sgm41512_read_reg(charger_ic, chgr_type_addr, &val_buf);
			sgm41512_dump_registers();
			val_buf = val_buf & REG08_SGM41512_VBUS_STAT_MASK;
			pr_err("mt_charger_type_detection: %dnd, val_buf=[0x%x]\n", i + 2, val_buf);
			if (val_buf == REG08_SGM41512_VBUS_STAT_UNKNOWN) {
				MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
			} else if (val_buf == REG08_SGM41512_VBUS_STAT_SDP) {
				MTK_CHR_Type_num = STANDARD_HOST;
			} else if (val_buf == REG08_SGM41512_VBUS_STAT_CDP) {
				MTK_CHR_Type_num = CHARGING_HOST;
			} else if (val_buf == REG08_SGM41512_VBUS_STAT_DCP
					|| val_buf == REG08_SGM41512_VBUS_STAT_OCP
					|| val_buf == REG08_SGM41512_VBUS_STAT_FLOAT) {
				MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
			} else {
				MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
			}
		}
	}

	Charger_Detect_Release();

	return MTK_CHR_Type_num;
}
#ifdef TEST_CHARGER_TYPE_DETECTION
enum charger_type mt_charger_type_detection_sgm41512(void)
{
	int rc = 0;
	int addr = 0;
	int val_buf;
	int count = 0;
	int i;

	if (!charger_ic) {
		pr_err("%s charger_ic null, return\n", __func__);
		return CHARGER_UNKNOWN;
	}
	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
			|| get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
		return STANDARD_HOST;
	}
	addr = SGM41512_FIRST_REG + 8;
	pr_err("mt_charger_type_detection0\n");
	hw_bc12_init();
	usleep_range(40000, 40200);
	pr_err("mt_charger_type_detection1\n");
	sgm41512_config_interface(charger_ic, REG00_SGM41512_ADDRESS, 0, 0x80);
	usleep_range(5000, 5200);
	sgm41512_config_interface(charger_ic, REG07_SGM41512_ADDRESS, 0x80, 0);
	sgm41512_dump_registers();
	rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
	while (val_buf == 0xcb && count < 20) {
		count++;
		rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
		usleep_range(50000, 50200);
		if (!pmic_chrdet_status()) {
			MTK_CHR_Type_num = CHARGER_UNKNOWN;
			return MTK_CHR_Type_num;
		}
	}
	if (count == 20) {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
		pr_err("count time out,return apple adapter\n");
		return MTK_CHR_Type_num;
	}
	pr_err("mt_charger_type_detection2\n");
	rc = sgm41512_read_reg(charger_ic, addr, &val_buf);
	pr_err("mt_charger_type_detection\n");
	sgm41512_dump_registers();
	val_buf = val_buf & 0xc0;
	pr_err("val_buf = 0x%x\n", val_buf);
	if (val_buf == 0) {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
	}
	else if (val_buf == 0x40) {
		MTK_CHR_Type_num = STANDARD_HOST;
	}
	else if (val_buf == 0x80) {
		MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
	}
	else {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
	}
	if ((MTK_CHR_Type_num == CHARGER_UNKNOWN || MTK_CHR_Type_num == STANDARD_HOST) && pmic_chrdet_status()) {
		if (MTK_CHR_Type_num == STANDARD_HOST) {
			for (i = 0; i < 4; i++) {
				usleep_range(100000, 100020);
			}
		}
		sgm41512_config_interface(charger_ic, REG00_SGM41512_ADDRESS, 0, 0x80);
		usleep_range(5000, 5020);
		sgm41512_config_interface(charger_ic, REG07_SGM41512_ADDRESS, 0x80, 0);
		sgm41512_dump_registers();
		rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
		while (val_buf == 0xcb && count <20) {
			count++;
			rc = sgm41512_read_reg(charger_ic, REG07_SGM41512_ADDRESS, &val_buf);
			usleep_range(50000, 50200);
			if (!pmic_chrdet_status()) {
				MTK_CHR_Type_num = CHARGER_UNKNOWN;
				return MTK_CHR_Type_num;
			}
		}
		if (count == 20) {
			MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
			pr_err("count time out,return apple adapter\n");
			return MTK_CHR_Type_num;
		}
		rc = sgm41512_read_reg(charger_ic, addr, &val_buf);
		sgm41512_dump_registers();
		val_buf = val_buf & 0xc0;
		pr_err("val_buf =0x%x\n", val_buf);
		if (val_buf == 0) {
			MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
		}
		else if	(val_buf == 0x40) {
			MTK_CHR_Type_num = STANDARD_HOST;
		}
		else if (val_buf == 0x80) {
			MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
		}
		else {
			MTK_CHR_Type_num = CHARGER_UNKNOWN;
		}
	}
	pr_err("chr_type:%d\n", MTK_CHR_Type_num);
	Charger_Detect_Release();
	/*MTK_CHR_Type_num = APPLE_2_1A_CHARGER;*/

	return MTK_CHR_Type_num;
}
#endif

static int sgm41512_enable_chg_type_det(bool en);
static void do_charger_modefy_work(struct work_struct *data)
{
	struct chip_sgm41512 *chip = charger_ic;
	int power_good = 0, val_reg = 0;

	if (chip != NULL) {
		sgm41512_read_reg(chip, REG0A_SGM41512_ADDRESS, &val_reg);
		chip->pwr_gd = (val_reg &= REG0A_SGM41512_VBUS_GOOD_STAT_MASK) >> 7;
		if (chip->pwr_gd) {
			sgm41512_enable_chg_type_det(true);
		} else {
			sgm41512_enable_chg_type_det(false);
		}
		pr_err("%s, (%d)\n", __func__, chip->pwr_gd);
	}
}

static int sgm41512_inform_psy_changed(bool vbus_gd)
{
	int ret = 0;
	union power_supply_propval propval;
	static struct power_supply *psy;
	struct chip_sgm41512 *chip = charger_ic;
	/* Get chg type det power supply */
	psy = power_supply_get_by_name("charger");
	if (!psy) {
		dev_notice(chip->dev, "%s get power supply fail\n", __func__);
		return -EINVAL;
	}
	/* inform chg det power supply */
	propval.intval = vbus_gd;
	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_ONLINE,
					&propval);
	if (ret < 0)
		dev_notice(chip->dev, "%s psy online fail(%d)\n",
				      __func__, ret);

	propval.intval = chip->chg_type;
	ret = power_supply_set_property(psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
	if (ret < 0)
		dev_notice(chip->dev, "%s psy type fail(%d)\n", __func__, ret);
	power_supply_changed(psy);

	return ret;
}

static int sgm41512_enable_chg_type_det(bool en)
{
	struct chip_sgm41512 *chip = charger_ic;
	if (en) {
		chip->chg_type = mt_charger_type_detection_sgm41512();
		switch (chip->chg_type) {
		case CHARGER_UNKNOWN :
			charger_type_sgm = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		case STANDARD_HOST :
			charger_type_sgm = POWER_SUPPLY_TYPE_USB;
			mt_usb_connect_v1();
			break;
		case CHARGING_HOST :
			charger_type_sgm = POWER_SUPPLY_TYPE_USB_CDP;
			mt_usb_connect_v1();
			break;
		case NONSTANDARD_CHARGER :
		case STANDARD_CHARGER :
		case APPLE_2_1A_CHARGER :
		case APPLE_1_0A_CHARGER :
		case APPLE_0_5A_CHARGER :
			charger_type_sgm = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		default:
			charger_type_sgm = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		}
	} else {
		chip->chg_type = CHARGER_UNKNOWN;
		charger_type_sgm = POWER_SUPPLY_TYPE_UNKNOWN;
	}

	pr_err("SGM41512 charger type:%d, power_good:%d\n", chip->chg_type, chip->pwr_gd);
	sgm41512_inform_psy_changed(en);
	return 0;
}

static void do_sgm41512_bc_delay_work(struct work_struct *data)
{
	struct chip_sgm41512 *chip = charger_ic;
	sgm41512_enable_chg_type_det(chip->pwr_gd);
}

static void do_sgm41512_irq_delay_work(struct work_struct *data)
{
	int val_buf = 0;
	int val_reg = 0;
	int i = 0;
	int otg_overcurrent_flag = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		pr_err("%s chip null,return\n", __func__);
		return;
	}

	for (i = 0; i < 10; i++) {
		sgm41512_read_reg(chip, REG09_SGM41512_ADDRESS, &val_reg);
		val_buf = val_reg & 0x40;
		if (val_buf == 0x40) {
			otg_overcurrent_flag++;
		}

		usleep_range(10000, 10200);
	}
	pr_err("[OPLUS_CHG] do_sgm41512_irq_delay_work disable vbus out flag=%d, val_reg[0x%x]\n", otg_overcurrent_flag, val_reg);
	if (otg_overcurrent_flag >= 8) {
		sgm41512_otg_disable();
	}
	fg_sgm41512_irq_delay_work_running = false;
	return;
}

static int sgm41512_parse_dts(void)
{
	int ret = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (!chip) {
		pr_err("chip is NULL\n");
		return -1;
	}

	chip->enable_headset_plugin_limit = of_property_read_bool(chip->client->dev.of_node, "enable_headset_plugin_limit");
	chip->irq_gpio = of_get_named_gpio(chip->client->dev.of_node, "chg-irq-gpio", 0);
	if (chip->irq_gpio <= 0) {
		pr_err("Couldn't read chg-irq-gpio:%d\n", chip->irq_gpio);
		return -1;
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			ret = gpio_request(chip->irq_gpio, "chg-irq-gpio");
			if (ret) {
				pr_err("unable to request chg-irq-gpio[%d]\n", chip->irq_gpio);
				chip->irq_gpio = -EINVAL;
			} else {
				gpio_direction_input(chip->irq_gpio);
			}
		} else {
			pr_err("gpio_is_valid fail chg-irq-gpio[%d]\n", chip->irq_gpio);
			chip->irq_gpio = -EINVAL;
			return -1;
		}
	}
	pr_err("chg-irq-gpio[%d]\n", chip->irq_gpio);
	return ret;
}

enum sgm41512_part_no {
	BQ25601D = 0X02,
	SY6974B = 0x08,
};

#define REG0B_PN_MASK             	0x78
#define REG0B_PN_SHIFT            	3

void sgm41512_get_chip_id(void)
{
	int data, partno;
	sgm41512_read_reg(charger_ic, REG0B_SGM41512_ADDRESS, &data);
	partno = (data & REG0B_PN_MASK) >> REG0B_PN_SHIFT;
	pr_err("partno=%d\n", partno);
	if (partno == BQ25601D) {
		charger_ic->chip_id = 1;
	} else if (partno == SY6974B) {
		charger_ic->chip_id = 2;
	}
}

extern int charger_ic_flag;
extern int oplus_get_usb_status(void);
static void do_sgm41512_threaded_irq_work(struct work_struct *data)
{
	struct chip_sgm41512 *chip = charger_ic;
	int power_good, val_reg, hiz_status, vbus;
	if (!chip) {
		pr_err("%s chip null,return\n", __func__);
		return;
	}

	if (oplus_vooc_get_fastchg_started() == true
			&& oplus_vooc_get_adapter_update_status() != 1) {
		pr_err("%s, oplus_vooc_get_fastchg_started = true!\n", __func__);
		return;
	}

	/* normal charger/usb set hiz enter */
	sgm41512_read_reg(chip, REG00_SGM41512_ADDRESS, &hiz_status);
	if ((hiz_status & REG00_SGM41512_SUSPEND_MODE_MASK) && (!oplus_get_usb_status())) {
		vbus = battery_get_vbus();
		/* vbus online and set sgm41512 to hiz mode */
		if (vbus > CHARGER_VBUS_VOTLAGE_REF) {
			pr_err("%s, hiz mode interrupt ignore vbus= %d\n", __func__, vbus);
			return;
		}
	}

	power_good = chip->pwr_gd;
	sgm41512_read_reg(chip, REG0A_SGM41512_ADDRESS, &val_reg);
	chip->pwr_gd = (val_reg &= REG0A_SGM41512_VBUS_GOOD_STAT_MASK) >> 7;
	/* normal charger try read need wait 50ms */
	vbus = battery_get_vbus();
	if (!chip->pwr_gd && vbus > CHARGER_VBUS_VOTLAGE_REF) {
		msleep(50);
		sgm41512_read_reg(chip, REG0A_SGM41512_ADDRESS, &val_reg);
		pr_err("%s, chip->pwr_gd = %d\n", __func__, chip->pwr_gd);
		chip->pwr_gd = (val_reg &= REG0A_SGM41512_VBUS_GOOD_STAT_MASK) >> 7;
	}
	pr_err("%s, (%d:%d)\n", __func__, power_good, chip->pwr_gd);

	if (chip->pwr_gd != power_good) {
		/*  usb plug out action */
		if (!chip->pwr_gd && power_good) {
			if (oplus_voocphy_get_fastchg_ing() == false) {
				oplus_vooc_reset_fastchg_after_usbout();
				oplus_chg_set_chargerid_switch_val(0);
				oplus_chg_clear_chargerid_info();
			}
			oplus_chg_set_charger_type_unknown();
			oplus_chg_wake_update_work();
		}

		sgm41512_enable_chg_type_det(chip->pwr_gd);
		if(chip->enable_headset_plugin_limit) {
			if(chip->pwr_gd) {
				schedule_delayed_work(&charger_ic->check_ibat_work, round_jiffies_relative(msecs_to_jiffies(10000)));
			} else {
				cancel_delayed_work_sync(&charger_ic->check_ibat_work);
			}
		}
	}

	if (fg_sgm41512_irq_delay_work_running == false) {
		fg_sgm41512_irq_delay_work_running = true;
		schedule_delayed_work(&sgm41512_irq_delay_work, round_jiffies_relative(msecs_to_jiffies(50)));
	}
}

static irqreturn_t sgm41512_irq_handler_fn(int irq, void *dev_id)
{
	struct chip_sgm41512 *chip = charger_ic;
	if (!chip) {
		pr_err("%s chip null,return\n", __func__);
		return IRQ_HANDLED;
	}

	pr_err("%s, enter!\n", __func__);
	schedule_delayed_work(&sgm41512_threaded_irq_work, 0);
	return IRQ_HANDLED;
}

static int sgm41512_irq_registration(void)
{
	int ret = 0;
	struct chip_sgm41512 *chip = charger_ic;

	if (chip->irq_gpio <= 0) {
		pr_err("chip->irq_gpio fail\n");
		return -1;
	}
	ret = request_threaded_irq(gpio_to_irq(chip->irq_gpio), NULL,
			sgm41512_irq_handler_fn,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"SGM41512-eint", chip);
	if (ret < 0) {
		pr_err("SGM41512 request_irq IRQ LINE NOT AVAILABLE!");
		return -EFAULT;
	}
	enable_irq_wake(gpio_to_irq(chip->irq_gpio));
	return 0;
}

static int sgm41512_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int reg = 0, ret;
	struct chip_sgm41512 *chip = NULL;

	pr_err("call \n");

	chip = devm_kzalloc(&client->dev, sizeof(struct chip_sgm41512), GFP_KERNEL);
	if (!chip) {
		pr_err("kzalloc() failed\n");
		return -ENOMEM;
	}
	charger_ic = chip;
	chip->client = client;
	chip->dev = &client->dev;
	chip->chg_props.alias_name = "sgm41512_chg";
	chip->chg_dev_name = "primary_chg";
	chip->pwr_gd = 0;
	chip->before_suspend_icl = 0;

	reg = sgm41512_check_registers();
	if (reg < 0) {
		return -ENODEV;
	}

	/* Register charger device */
	chip->chg_dev = charger_device_register(chip->chg_dev_name,
						&client->dev, chip,
						&sgm41512_chg_ops,
						&chip->chg_props);
	if (IS_ERR_OR_NULL(chip->chg_dev)) {
		pr_info("%s: register charger device  failed\n", __func__);
		ret = PTR_ERR(chip->chg_dev);
		return ret;
	}

	INIT_DELAYED_WORK(&sgm41512_irq_delay_work, do_sgm41512_irq_delay_work);
	INIT_DELAYED_WORK(&sgm41512_bc_delay_work, do_sgm41512_bc_delay_work);
	INIT_DELAYED_WORK(&sgm41512_threaded_irq_work, do_sgm41512_threaded_irq_work);
	sgm41512_parse_dts();
	sgm41512_irq_registration();
	atomic_set(&chip->charger_suspended, 0);
	sgm41512_reset_charger();
	register_charger_devinfo();
	sgm41512_get_chip_id();
	usleep_range(1000, 1200);
	INIT_DELAYED_WORK(&charger_modefy_work, do_charger_modefy_work);
	schedule_delayed_work(&charger_modefy_work, round_jiffies_relative(msecs_to_jiffies(2000)));
	pr_err("call OK!\n");
	set_charger_ic(SGM41512);
	sgm41512_dump_registers();
	if(chip->enable_headset_plugin_limit) {
		INIT_DELAYED_WORK(&chip->check_ibat_work, sgm41512_check_ibat_work);
	}
	pr_info("SGM41512 charger driver probe successfully\n");

	return 0;
}

static struct i2c_driver sgm41512_i2c_driver;

static int sgm41512_driver_remove(struct i2c_client *client)
{
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
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
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
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int sgm41512_resume(struct device *dev)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int rc = 0;

	if (!charger_ic) {
		return 0;
	}
	atomic_set(&charger_ic->charger_suspended, 0);
	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		pr_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	if (sleep_time < 0) {
		sleep_time = 0;
	}
	pr_err("resume_sec:%ld,sleep_time:%ld\n\n", resume_tm_sec, sleep_time);
	oplus_chg_soc_update_when_resume(sleep_time);
	return 0;
}

static int sgm41512_suspend(struct device *dev)
{
	if (!charger_ic) {
		return 0;
	}
	atomic_set(&charger_ic->charger_suspended, 1);
	if (get_current_time(&suspend_tm_sec)) {
		pr_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	pr_err("suspend_sec:%ld\n", suspend_tm_sec);
	return 0;
}

static const struct dev_pm_ops sgm41512_pm_ops = {
	.resume		= sgm41512_resume,
	.suspend		= sgm41512_suspend,
};

#else

static int sgm41512_resume(struct i2c_client *client)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int rc = 0;

	if (!charger_ic) {
		return 0;
	}
	atomic_set(&charger_ic->charger_suspended, 0);
	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		pr_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	if (sleep_time < 0) {
		sleep_time = 0;
	}
	oplus_chg_soc_update_when_resume(sleep_time);
	return 0;
}

static int sgm41512_suspend(struct i2c_client *client, pm_message_t mesg)
{
	if (!charger_ic) {
		return 0;
	}
	atomic_set(&charger_ic->charger_suspended, 1);
	if (get_current_time(&suspend_tm_sec)) {
		pr_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	return 0;
}
#endif

static void sgm41512_reset(struct i2c_client *client)
{
	sgm41512_otg_disable();
}

/**********************************************************
 * *
 * *   [platform_driver API]
 * *
 * *********************************************************/

static const struct of_device_id sgm41512_match[] = {
	{ .compatible = "ti,sgm41512-charger"},
	{ },
};

static const struct i2c_device_id sgm41512_id[] = {
	{"sgm41512-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm41512_id);

static struct i2c_driver sgm41512_i2c_driver = {
	.driver		= {
		.name			= "sgm41512-charger",
		.owner			= THIS_MODULE,
		.of_match_table	= sgm41512_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm				= &sgm41512_pm_ops,
#endif
	},
	.probe		= sgm41512_driver_probe,
	.remove		= sgm41512_driver_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= sgm41512_resume,
	.suspend		= sgm41512_suspend,
#endif

	.shutdown	= sgm41512_reset,
	.id_table		= sgm41512_id,
};

module_i2c_driver(sgm41512_i2c_driver);
MODULE_DESCRIPTION("Driver for sgm41512 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:sgm41512-charger");
