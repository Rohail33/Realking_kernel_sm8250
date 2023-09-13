/*
 *  s2asl01_switching.c
 *  Samsung S2ASL01 Switching IC driver
 *
 *  Copyright (C) 2018 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "s2asl01_switching.h"
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/oplus_project.h>
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <mt-plat/mtk_boot_common.h>
#endif

struct s2asl01_switching_data *g_switching;
static int s2asl01_boot_mode;
int s2asl01_switching_set_fastcharge_current(int charge_curr_ma);
int s2asl01_switching_set_discharge_current(int discharge_current_ma);
int get_switching_hw_enable(void);
char *current_limiter_type_str[] = {
	"NONE",
	"MAIN LIMITER",
	"SUB LIMITER",
};

static enum power_supply_property s2asl01_main_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW
};

static enum power_supply_property s2asl01_sub_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW
};
static void s2asl01_set_error_status(int status)
{
	if (g_switching != NULL) {
		if (get_switching_hw_enable() && atomic_read(&g_switching->suspended) == 0) {
			g_switching->switching_chip->error_status = status;
			chg_err("s2asl01 set error status:%d\n", status);
		}
	}
}
static int s2asl01_write_reg(struct i2c_client *client, int reg, u8 data)
{
	struct s2asl01_switching_data *switching = i2c_get_clientdata(client);
	int ret = 0;
	int retry = 0;

	mutex_lock(&switching->i2c_lock);
	ret = i2c_smbus_write_byte_data(client, reg, data);
	mutex_unlock(&switching->i2c_lock);
	while (ret < 0) {
		pr_info("%s [%s]: reg(0x%x), ret(%d), retry:%d\n",
			__func__, current_limiter_type_str[switching->pdata->bat_type], reg, ret, retry);
		msleep(50);
		mutex_lock(&switching->i2c_lock);
		ret = i2c_smbus_write_byte_data(client, reg, data);
		mutex_unlock(&switching->i2c_lock);
		retry++;
		if (retry > 3) {
			s2asl01_set_error_status(SWITCH_ERROR_I2C_ERROR);
			break;
		}
	}
	return ret;
}

static int s2asl01_read_reg(struct i2c_client *client, int reg, void *data)
{
	struct s2asl01_switching_data *switching = i2c_get_clientdata(client);
	int ret = 0;
	int retry = 0;

	mutex_lock(&switching->i2c_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&switching->i2c_lock);
	while (ret < 0) {
		s2asl01_set_error_status(SWITCH_ERROR_I2C_ERROR);
		pr_info("%s [%s]: reg(0x%x), ret(%d), retry:%d\n",
			__func__, current_limiter_type_str[switching->pdata->bat_type], reg, ret, retry);
		mutex_lock(&switching->i2c_lock);
		ret = i2c_smbus_read_byte_data(client, reg);
		mutex_unlock(&switching->i2c_lock);
		retry++;
		if (retry > 3) {
			s2asl01_set_error_status(SWITCH_ERROR_I2C_ERROR);
			return ret;
		}
	}
	ret &= 0xff;
	*(u8 *)data = (u8)ret;

	return ret;
}

static int s2asl01_update_reg(struct i2c_client *client, u8 reg, u8 val, u8 mask)
{
	struct s2asl01_switching_data *switching = i2c_get_clientdata(client);
	int ret = 0;
	int retry = 0;
	u8 old_val = 0, new_val = 0;

	mutex_lock(&switching->i2c_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&switching->i2c_lock);
	while (ret < 0) {
		s2asl01_set_error_status(SWITCH_ERROR_I2C_ERROR);
		pr_info("%s [%s]: reg(0x%x), ret(%d), retry:%d\n",
			__func__, current_limiter_type_str[switching->pdata->bat_type], reg, ret, retry);
		mutex_lock(&switching->i2c_lock);
		ret = i2c_smbus_read_byte_data(client, reg);
		mutex_unlock(&switching->i2c_lock);
		retry++;
		if (retry > 3) {
			s2asl01_set_error_status(SWITCH_ERROR_I2C_ERROR);
			break;
		}
	}
	if (ret >= 0) {
		old_val = ret & 0xff;
		new_val = (val & mask) | (old_val & (~mask));
		mutex_lock(&switching->i2c_lock);
		ret = i2c_smbus_write_byte_data(client, reg, new_val);
		mutex_unlock(&switching->i2c_lock);
	}
	return ret;
}

static void s2asl01_test_read(struct i2c_client *client)
{
	struct s2asl01_switching_data *switching = i2c_get_clientdata(client);
	u8 data = 0;
	char str[1016] = {0,};
	int i = 0;

	/* address 0x00 ~ 0x1f */
	for (i = 0x0; i <= 0x1F; i++) {
		s2asl01_read_reg(switching->client, i, &data);
		sprintf(str+strlen(str), "0x%02x:0x%02x, ", i, data);
	}

	pr_info("%s [%s]: %s\n", __func__, current_limiter_type_str[switching->pdata->bat_type], str);
}

static void s2asl01_set_in_ok(struct s2asl01_switching_data *switching, bool onoff)
{
	chg_err("[%s]: INOK = %d\n", current_limiter_type_str[switching->pdata->bat_type], onoff);
	if (onoff)
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3,
			1 << S2ASL01_CTRL3_INOK_SHIFT, S2ASL01_CTRL3_INOK_MASK);
	else
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3,
			0 << S2ASL01_CTRL3_INOK_SHIFT, S2ASL01_CTRL3_INOK_MASK);
}

static void s2asl01_set_supllement_mode(struct s2asl01_switching_data *switching, bool onoff)
{
	pr_info("%s[%s]: SUPLLEMENT MODE = %d\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], onoff);

	if (onoff) {
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3,
			1 << S2ASL01_CTRL3_SUPLLEMENTMODE_SHIFT, S2ASL01_CTRL3_SUPLLEMENTMODE_MASK);
		switching->supllement_mode = true;
	} else {
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3,
			0 << S2ASL01_CTRL3_SUPLLEMENTMODE_SHIFT, S2ASL01_CTRL3_SUPLLEMENTMODE_MASK);
		switching->supllement_mode = false;
	}
}

static void s2asl01_set_recharging_start(struct s2asl01_switching_data *switching)
{
	pr_info("%s: INOK = %d, SUPLLEMENT MODE = %d\n",
		__func__, switching->in_ok, switching->supllement_mode);

	s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL1,
			1 << S2ASL01_CTRL1_RESTART_SHIFT, S2ASL01_CTRL1_RESTART_MASK);
}

static void s2asl01_set_eoc_on(struct s2asl01_switching_data *switching)
{
	pr_info("%s[%s]: INOK = %d\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], switching->in_ok);

	s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL1,
			1 << S2ASL01_CTRL1_EOC_SHIFT, S2ASL01_CTRL1_EOC_MASK);
}

static void s2asl01_set_dischg_mode(struct s2asl01_switching_data *switching, int chg_mode)
{
	u8 val = 0;
	int ret = 0;

	switch (chg_mode) {
	case CURRENT_REGULATION:
		val = DIS_CHG_MODE_CURRENT__REGULATION_ONLY;
		break;
	case NO_REGULATION_FULLY_ON:
		val = DIS_CHG_MODE_NO_REGULATION_FULLY_ON;
		break;
	default:
		pr_info("%s: wrong input(%d)\n", __func__, chg_mode);
		break;
	}

	ret = s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3,
			val << S2ASL01_CTRL3_DIS_MODE_SHIFT, S2ASL01_CTRL3_DIS_MODE_MASK);
	if (ret < 0)
		pr_err("%s, i2c read fail\n", __func__);

	pr_info("%s: set discharge mode (%d) val:%d\n", __func__, chg_mode, val);
}

static int s2asl01_get_dischg_mode(struct s2asl01_switching_data *switching)
{
	u8 val = 0, chg_mode = 0;
	int ret = 0;

	ret = s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3, &val);
	chg_mode = (val & S2ASL01_CTRL3_DIS_MODE_MASK) >> S2ASL01_CTRL3_DIS_MODE_SHIFT;

	switch (chg_mode) {
	case CURRENT_REGULATION:
	case NO_REGULATION_FULLY_ON:
		return chg_mode;
	default:
		return -EINVAL;
	}
}

static void s2asl01_set_chg_mode(struct s2asl01_switching_data *switching, int chg_mode)
{
	u8 val = 0;
	int ret = 0;

	switch (chg_mode) {
	case CURRENT_REGULATION:
	case NO_REGULATION_FULLY_ON:
		val = chg_mode;
		break;
	default:
		pr_info("%s: wrong input(%d)\n", __func__, chg_mode);
		break;
	}

	ret = s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3,
			val << S2ASL01_CTRL3_CHG_MODE_SHIFT, S2ASL01_CTRL3_CHG_MODE_MASK);
	if (ret < 0)
		pr_err("%s, i2c read fail\n", __func__);

	pr_info("%s: set charge mode (%d)\n", __func__, chg_mode);
}

static int s2asl01_get_chg_mode(struct s2asl01_switching_data *switching)
{
	u8 val = 0, chg_mode = 0;
	int ret = 0;

	ret = s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3, &val);
	chg_mode = (val & S2ASL01_CTRL3_CHG_MODE_MASK) >> S2ASL01_CTRL3_CHG_MODE_SHIFT;

	switch (chg_mode) {
	case CURRENT_REGULATION:
	case NO_REGULATION_FULLY_ON:
		return chg_mode;
	default:
		return -EINVAL;
	}
}

static int s2asl01_get_vchg(struct s2asl01_switching_data *switching, int mode)
{
	u8 data[2] = {0, };
	u16 temp = 0;
	u32 vchg = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL1_VCHG, &data[0]);
	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL2_VCHG, &data[1]);

	temp = (data[0] << 4) | ((data[1] & 0xF0) >> 4);
	temp &= 0x0FFF;
	vchg = temp * 1250;

	if (mode == S2M_BATTERY_VOLTAGE_MV)
		vchg = vchg / 1000;

	pr_info("%s [%s]: vchg = %d %s\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], vchg,
		(mode == S2M_BATTERY_VOLTAGE_UV) ? "uV" : "mV");

	return vchg;
}

static int s2asl01_get_vbat(struct s2asl01_switching_data *switching, int mode)
{
	u8 data[2] = {0, };
	u16 temp = 0;
	u32 vbat = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL1_VBAT, &data[0]);
	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL2_VBAT, &data[1]);

	temp = (data[0] << 4) | ((data[1] & 0xF0) >> 4);
	temp &= 0x0FFF;
	vbat = temp * 1250;

	if (mode == S2M_BATTERY_VOLTAGE_MV)
		vbat = vbat / 1000;

	pr_info("%s [%s]: vbat = %d %s\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], vbat,
		(mode == S2M_BATTERY_VOLTAGE_UV) ? "uV" : "mV");

	return vbat;
}

static int s2asl01_get_ichg(struct s2asl01_switching_data *switching, int mode)
{
	u8 data[2] = {0, };
	u16 temp = 0;
	u32 ichg = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL1_ICHG, &data[0]);
	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL2_ICHG, &data[1]);

	temp = (data[0] << 4) | ((data[1] & 0xF0) >> 4);
	temp &= 0x0FFF;
	ichg = (temp * 15625) / 10;

	if (mode == S2M_BATTERY_CURRENT_MA)
		ichg = ichg / 1000;

	pr_info("%s [%s]: Ichg = %d %s\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], ichg,
		(mode == S2M_BATTERY_CURRENT_UA) ? "uA" : "mA");

	return ichg;
}

static int s2asl01_get_idischg(struct s2asl01_switching_data *switching, int mode)
{
	u8 data[2] = {0, };
	u16 temp = 0;
	u32 idischg = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL1_IDISCHG, &data[0]);
	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_VAL2_IDISCHG, &data[1]);

	temp = (data[0] << 4) | ((data[1] & 0xF0) >> 4);
	temp &= 0x0FFF;
	idischg = (temp * 15625) / 10;

	if (mode == S2M_BATTERY_CURRENT_MA)
		idischg = idischg / 1000;

	pr_info("%s [%s]: IDISCHG = %d %s\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], idischg,
		(mode == S2M_BATTERY_CURRENT_UA) ? "uA" : "mA");

	return idischg;
}

static void s2asl01_set_fast_charging_current_limit(struct s2asl01_switching_data *switching, int charging_current)
{
	u8 data = 0;

	if (switching->rev_id == 0) {
		if (charging_current <= 50)
			data = 0x00;
		else if (charging_current > 50 && charging_current <= 3200)
			data = (charging_current / 50) - 1;
		else
			data = 0x3F;
	} else {
		if (charging_current <= 50)
			data = 0x00;
		else if (charging_current > 50 && charging_current <= 350)
			data = (charging_current - 50) / 75;
		else if (charging_current > 350 && charging_current <= 3300)
			data = ((charging_current - 350) / 50) + 4;
		else
			data = 0x3F;
	}

	chg_err("[%s]: current %d, 0x%02x\n",
		current_limiter_type_str[switching->pdata->bat_type], charging_current, data);

	s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL4, data, FCC_CHG_CURRENTLIMIT_MASK);
}

static int s2asl01_get_fast_charging_current_limit(struct s2asl01_switching_data *switching)
{
	u8 data = 0;
	int charging_current = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL4, &data);

	data = data & FCC_CHG_CURRENTLIMIT_MASK;

	if (data > 0x3F) {
		pr_err("%s: Invalid fast charging current limit value\n", __func__);
		data = 0x3F;
	}

	if (switching->rev_id == 0)
		charging_current = (data + 1) * 50;
	else {
		if ((data >= 0x00) && (data <= 0x04))
			charging_current = (data * 75) + 50;
		else
			charging_current = ((data - 4) * 50) + 350;
	}

	return charging_current;
}

static void s2asl01_set_trickle_charging_current_limit(struct s2asl01_switching_data *switching, int charging_current)
{
	u8 data = 0;

	if (switching->rev_id == 0) {
		if (charging_current <= 50)
			data = 0x00;
		else if (charging_current > 50 && charging_current <= 500)
			data = (charging_current / 50) - 1;
		else
			data = 0x09;
	} else {
		if (charging_current <= 50)
			data = 0x00;
		else if (charging_current > 50 && charging_current <= 350)
			data = (charging_current - 50) / 75;
		else if (charging_current > 350 && charging_current <= 550)
			data = ((charging_current - 350) / 50) + 4;
		else
			data = 0x08;
	}

	pr_info("%s [%s]: current %d, 0x%02x\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], charging_current, data);

	s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL5, data, TRICKLE_CHG_CURRENT_LIMIT_MASK);
}

static int s2asl01_get_trickle_charging_current_limit(struct s2asl01_switching_data *switching)
{
	u8 data = 0;
	int charging_current = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL5, &data);

	data = data & TRICKLE_CHG_CURRENT_LIMIT_MASK;

	if (switching->rev_id == 0) {
		if (data > 0x09) {
			pr_err("%s: Invalid trickle charging current limit value\n", __func__);
			data = 0x09;
		}
		charging_current = (data + 1) * 50;
	} else {
		if (data > 0x08) {
			pr_err("%s: Invalid trickle charging current limit value\n", __func__);
			data = 0x08;
		}
		if ((data >= 0x00) && (data <= 0x04))
			charging_current = (data * 75) + 50;
		else
			charging_current = ((data - 4) * 50) + 350;
	}

	return charging_current;
}

static void s2asl01_set_dischg_charging_current_limit(struct s2asl01_switching_data *switching, int charging_current)
{
	u8 data = 0;

	if (switching->rev_id == 0) {
		if (charging_current <= 50)
			data = 0x00;
		else if (charging_current > 50 && charging_current <= 6400)
			data = (charging_current / 50) - 1;
		else
			data = 0x7F;
	} else {
		if (charging_current <= 50)
			data = 0x00;
		else if (charging_current > 50 && charging_current <= 350)
			data = (charging_current - 50) / 75;
		else if (charging_current > 350 && charging_current <= 6500)
			data = ((charging_current - 350) / 50) + 4;
		else
			data = 0x7F;
	}
	pr_err("%s [%s]: current %d, 0x%02x\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], charging_current, data);

	s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL6, data, DISCHG_CURRENT_LIMIT_MASK);
}

static int s2asl01_get_dischg_charging_current_limit(struct s2asl01_switching_data *switching)
{
	u8 data = 0;
	int dischg_current = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL6, &data);

	data = data & DISCHG_CURRENT_LIMIT_MASK;

	if (data > 0x7F) {
		pr_err("%s: Invalid discharging current limit value\n", __func__);
		data = 0x7F;
	}

	if (switching->rev_id == 0)
		dischg_current = (data + 1) * 50;
	else {
		if ((data >= 0x00) && (data <= 0x04))
			dischg_current = (data * 75) + 50;
		else
			dischg_current = ((data - 4) * 50) + 350;
	}

	return dischg_current;
}

static void s2asl01_set_recharging_voltage(struct s2asl01_switching_data *switching, int charging_voltage)
{
	u8 data = 0;

	if (charging_voltage < 4000)
		data = 0x90;
	else if (charging_voltage >= 4000 && charging_voltage < 4400)
		data = (charging_voltage - 2560) / 10;
	else
		data = 0xB8;

	pr_info("%s: voltage %d, 0x%02x\n", __func__, charging_voltage, data);

	s2asl01_write_reg(switching->client, S2ASL01_SWITCHING_TOP_RECHG_CTRL1, data);
}

static int s2asl01_get_recharging_voltage(struct s2asl01_switching_data *switching)
{
	u8 data = 0;
	int charging_voltage = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_TOP_RECHG_CTRL1, &data);
	charging_voltage = (data * 10) + 2560;

	return charging_voltage;
}

static void s2asl01_set_eoc_voltage(struct s2asl01_switching_data *switching, int charging_voltage)
{
	u8 data = 0;

	if (charging_voltage < 4000)
		data = 0x90;
	else if (charging_voltage >= 4000 && charging_voltage < 4400)
		data = (charging_voltage - 2560) / 10;
	else
		data = 0xB8;

	pr_info("%s [%s]: voltage %d, 0x%02x\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], charging_voltage, data);

	s2asl01_write_reg(switching->client, S2ASL01_SWITCHING_TOP_EOC_CTRL1, data);
}

static int s2asl01_get_eoc_voltage(struct s2asl01_switching_data *switching)
{
	u8 data = 0;
	int charging_voltage = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_TOP_EOC_CTRL1, &data);
	charging_voltage = (data * 10) + 2560;

	return charging_voltage;
}

static void s2asl01_set_eoc_current(struct s2asl01_switching_data *switching, int charging_current)
{
	u8 data = 0;

	if (charging_current < 100)
		data = 0x04;
	else if (charging_current >= 100 && charging_current < 775)
		data = charging_current / 25;
	else
		data = 0x1F;

	pr_info("%s [%s]: current %d, 0x%02x\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type], charging_current, data);

	s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_TOP_EOC_CTRL2, data, 0x1F);
}

static int s2asl01_get_eoc_current(struct s2asl01_switching_data *switching)
{
	u8 data = 0;
	int charging_current = 0;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_TOP_EOC_CTRL2, &data);
	data &= 0x1F;
	charging_current = data * 25;

	return charging_current;
}

static void s2asl01_powermeter_onoff(struct s2asl01_switching_data *switching, bool onoff)
{
	pr_info("%s [%s]: (%d)\n", __func__, current_limiter_type_str[switching->pdata->bat_type], onoff);

	/* Power Meter Continuous Operation Mode
	 * [7]: VCHG
	 * [6]: VBAT
	 * [5]: ICHG
	 * [4]: IDISCHG
	 */
	if (onoff) {
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_ENABLE, 0xF0, 0xF0);
		switching->power_meter = true;
	} else {
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_ENABLE, 0x00, 0xF0);
		switching->power_meter = false;
	}
}

static void s2asl01_tsd_onoff(
		struct s2asl01_switching_data *switching, bool onoff)
{
	pr_info("%s(%d)\n", __func__, onoff);
	if (onoff) {
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_COMMON1,
				S2ASL01_COMMON1_CM_TSD_EN, S2ASL01_COMMON1_CM_TSD_EN);
		switching->tsd = true;
	} else {
		s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_COMMON1,
				0, S2ASL01_COMMON1_CM_TSD_EN);
		switching->tsd = false;
	}
}

static int s2asl01_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct s2asl01_switching_data *switching = power_supply_get_drvdata(psy);
	enum s2m_power_supply_property s2m_psp = (enum s2m_power_supply_property)psp;

	switch ((int)psp) {
	case POWER_SUPPLY_S2M_PROP_MIN ... POWER_SUPPLY_S2M_PROP_MAX:
		switch (s2m_psp) {
		case POWER_SUPPLY_S2M_PROP_CHGIN_OK:
			val->intval = switching->in_ok ? 1 : 0;
			break;
		case POWER_SUPPLY_S2M_PROP_SUPLLEMENT_MODE:
			val->intval = switching->supllement_mode ? 1 : 0;
			break;
		case POWER_SUPPLY_S2M_PROP_DISCHG_MODE:
			val->intval = s2asl01_get_dischg_mode(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_CHG_MODE:
			val->intval = s2asl01_get_chg_mode(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_CHG_VOLTAGE:
			val->intval = s2asl01_get_vchg(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_BAT_VOLTAGE:
			val->intval = s2asl01_get_vbat(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_CHG_CURRENT:
			val->intval = s2asl01_get_ichg(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_DISCHG_CURRENT:
			val->intval = s2asl01_get_idischg(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_FASTCHG_LIMIT_CURRENT:
			val->intval = s2asl01_get_fast_charging_current_limit(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_TRICKLECHG_LIMIT_CURRENT:
			val->intval = s2asl01_get_trickle_charging_current_limit(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_DISCHG_LIMIT_CURRENT:
			val->intval = s2asl01_get_dischg_charging_current_limit(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_RECHG_VOLTAGE:
			val->intval = s2asl01_get_recharging_voltage(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_EOC_VOLTAGE:
			val->intval = s2asl01_get_eoc_voltage(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_EOC_CURRENT:
			val->intval = s2asl01_get_eoc_current(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_POWERMETER_ENABLE:
			val->intval = switching->power_meter;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	//s2asl01_test_read(switching->client);
	return 0;
}

static int s2asl01_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct s2asl01_switching_data *switching = power_supply_get_drvdata(psy);
	enum s2m_power_supply_property s2m_psp = (enum s2m_power_supply_property)psp;

	//pr_info("%s [%s]\n", __func__, current_limiter_type_str[switching->pdata->bat_type]);

	switch ((int)psp) {
	case POWER_SUPPLY_S2M_PROP_MIN ... POWER_SUPPLY_S2M_PROP_MAX:
		switch (s2m_psp) {
		case POWER_SUPPLY_S2M_PROP_CHGIN_OK:
			switching->in_ok = val->intval;
			s2asl01_set_in_ok(switching, switching->in_ok);
			break;
		case POWER_SUPPLY_S2M_PROP_SUPLLEMENT_MODE:
			s2asl01_set_supllement_mode(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_RECHG_ON:
			s2asl01_set_recharging_start(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_EOC_ON:
			s2asl01_set_eoc_on(switching);
			break;
		case POWER_SUPPLY_S2M_PROP_DISCHG_MODE:
			s2asl01_set_dischg_mode(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_CHG_MODE:
			s2asl01_set_chg_mode(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_FASTCHG_LIMIT_CURRENT:
			s2asl01_set_fast_charging_current_limit(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_TRICKLECHG_LIMIT_CURRENT:
			s2asl01_set_trickle_charging_current_limit(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_DISCHG_LIMIT_CURRENT:
			s2asl01_set_dischg_charging_current_limit(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_RECHG_VOLTAGE:
			s2asl01_set_recharging_voltage(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_EOC_VOLTAGE:
			s2asl01_set_eoc_voltage(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_EOC_CURRENT:
			s2asl01_set_eoc_current(switching, val->intval);
			break;
		case POWER_SUPPLY_S2M_PROP_POWERMETER_ENABLE:
			s2asl01_powermeter_onoff(switching, val->intval);
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	s2asl01_test_read(switching->client);
	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static int s2asl01_switching_parse_dt(struct device *dev, struct s2asl01_platform_data *pdata)
{
	//struct device_node *np = of_find_node_by_name(NULL, "s2asl01-switching");
	struct device_node *np = dev->of_node;
	int ret = 0;
	//enum of_gpio_flags irq_gpio_flags;

	pr_info("%s parsing start\n", __func__);

	if (np == NULL) {
		pr_err("%s np is NULL\n", __func__);
	} else {
		pdata->pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR_OR_NULL(pdata->pinctrl)) {
			chg_err(": %d Getting pinctrl handle failed\n", __LINE__);
		} else {
			pdata->switch_active = pinctrl_lookup_state(pdata->pinctrl,
					"switch_active");
			if (IS_ERR_OR_NULL(pdata->switch_active)) {
				chg_err(
						": %d Failed to get the switch_active, pinctrl handle\n",
						__LINE__);
			} else {
				pinctrl_select_state(pdata->pinctrl, pdata->switch_active);
			}
		}
		pdata->en_gpio = of_get_named_gpio(np, "en_gpio", 0);
			if (pdata->en_gpio < 0) {
				chg_err("pdata->en_gpio not specified\n");
			} else {
				if (gpio_is_valid(pdata->en_gpio)) {
					ret = gpio_request(pdata->en_gpio, "switch_en_gpio");
					if (ret) {
						chg_err("unable to request gpio [%d]\n", pdata->en_gpio);
					}
				}
				chg_err("pdata->en_gpio =%d\n", pdata->en_gpio);
			}
		ret = of_property_read_u32(np, "limiter,bat_type", &pdata->bat_type);
		if (ret < 0)
			pr_info("%s: bat_type is empty\n", __func__);

		if (pdata->bat_type & LIMITER_MAIN) {
			pr_info("%s: It is MAIN battery dt\n", __func__);
#if 0
			ret = pdata->bat_enb = of_get_named_gpio_flags(np, "limiter,main_bat_enb_gpio",
					0, &irq_gpio_flags);
			if (ret < 0)
				dev_err(dev, "%s: can't main_bat_enb_gpio\r\n", __func__);
#endif
			np = of_find_node_by_name(NULL, "sec-dual-battery");
			if (!np)
				pr_info("%s: np NULL\n", __func__);
			else {
				ret = of_property_read_u32(np, "battery,main_charging_rate", &pdata->charging_rate);
				if (ret)
					pdata->charging_rate = 60;
				pr_info("%s: pdata->charging_rate(%d)\n", __func__, pdata->charging_rate);
			}
			np = of_find_node_by_name(NULL, "s2asl01-switching-main");
		} else if (pdata->bat_type & LIMITER_SUB) {
			pr_info("%s: It is SUB battery dt\n", __func__);
#if 0
			ret = pdata->bat_enb = of_get_named_gpio_flags(np, "limiter,sub_bat_enb_gpio",
					0, &irq_gpio_flags);
			if (ret < 0)
				dev_err(dev, "%s: can't sub_bat_enb_gpio\r\n", __func__);
#endif
			np = of_find_node_by_name(NULL, "sec-dual-battery");
			if (!np)
				pr_info("%s: np NULL\n", __func__);
			else {
				ret = of_property_read_u32(np, "battery,sub_charging_rate", &pdata->charging_rate);
				if (ret)
					pdata->charging_rate = 50;
				pr_info("%s: charging_rate(%d)\n", __func__, pdata->charging_rate);
			}
			np = of_find_node_by_name(NULL, "s2asl01-switching-sub");
		}

		ret = of_property_read_string(np, "limiter,switching_name", (char const **)&pdata->switching_name);
		if (ret < 0) {
			pr_info("%s: Switching IC name is empty\n", __func__);
			pdata->switching_name = "s2asl01-switching";
		}

		ret = of_property_read_u32(np, "limiter,chg_current_limit", &pdata->chg_current_limit);
		if (ret < 0) {
			ret = of_property_read_u32(np, "limiter,chg_current_max", &pdata->chg_current_max);
			if (ret < 0)
				pdata->chg_current_limit = 1650;
			else
				pdata->chg_current_limit = pdata->chg_current_max * pdata->charging_rate / 100;
		}
		pr_info("%s: Chg current limit is (%d)\n", __func__, pdata->chg_current_limit);

		ret = of_property_read_u32(np, "limiter,eoc", &pdata->eoc);
		if (ret < 0) {
			pr_info("%s: eoc is empty\n", __func__);
			pdata->eoc = 200; /* for interrupt setting, not used */
		}

		ret = of_property_read_u32(np, "limiter,float_voltage", &pdata->float_voltage);
		if (ret < 0) {
			pr_info("%s: float voltage is empty\n", __func__);
			pdata->float_voltage = 4350; /* for interrupt setting, not used */
		}

		ret = of_property_read_u32(np, "limiter,hys_vchg_level", &pdata->hys_vchg_level);
		if (ret < 0) {
			pr_info("%s: Hysteresis level is empty(vchg)\n", __func__);
			pdata->hys_vchg_level = 4; /* 250mV(default) */
		}

		ret = of_property_read_u32(np, "limiter,hys_vbat_level", &pdata->hys_vbat_level);
		if (ret < 0) {
			pr_info("%s: Hysteresis level is empty(vbat)\n", __func__);
			pdata->hys_vbat_level = 4; /* 250mV(default) */
		}

		ret = of_property_read_u32(np, "limiter,hys_ichg_level", &pdata->hys_ichg_level);
		if (ret < 0) {
			pr_info("%s: Hysteresis level is empty(ichg)\n", __func__);
			pdata->hys_ichg_level = 4; /* 500mA(default) */
		}

		ret = of_property_read_u32(np, "limiter,hys_idischg_level", &pdata->hys_idischg_level);
		if (ret < 0) {
			pr_info("%s: Hysteresis level is empty(idischg)\n", __func__);
			pdata->hys_idischg_level = 4; /* 500mA(default) */
		}

		pdata->tsd_en = (of_find_property(np, "limiter,tsd-en", NULL)) ? true : false;
	}

	pr_info("%s parsing end\n", __func__);
	return 0;
}

static const struct of_device_id s2asl01_switching_match_table[] = {
	{ .compatible = "samsung,s2asl01-switching",},
	{},
};
#else
static int s2asl01_switching_parse_dt(struct device *dev, struct s2asl01_switching_data *switching)
{
	return 0;
}

#define s2asl01_switching_match_table NULL
#endif /* CONFIG_OF */

static int s2asl01_get_rev_id(struct s2asl01_switching_data *switching)
{
	u8 val1 = 0, val2 = 0;
	int ret = 0;

	/* rev ID */
	ret = s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_ID, &val1);
	if (ret < 0) {
		pr_info("%s i2c error!!! ret:%d\n",
			__func__, ret);
		return ret;
	}
	if ((val1 & 0xF0) == 0) {
		s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_CORE_CTRL3, &val2);
		if (val2 != 0x0C) {
			/* EVT0 0x11 address default value: 0x02 */
			switching->es_num = 0;
			switching->rev_id = 0;
		} else {
			/* EVT1 0x11 address default value: 0x0C */
			switching->es_num = 1;
			switching->rev_id = 1;
		}
	} else {
		switching->es_num = (val1 & 0xC0) >> 6;
		switching->rev_id = (val1 & 0x30) >> 4;
	}

	pr_info("%s [%s]: rev id: %d, es_num = %d\n",
			__func__, current_limiter_type_str[switching->pdata->bat_type],
			switching->rev_id, switching->es_num);
	return 0;
}

static ssize_t  powermeter_en_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;

	return snprintf(buf, PAGE_SIZE, "Power meter %s\n", switching->power_meter ? "enabled" : "disabled");
}

static ssize_t powermeter_en_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int ret;
	u8 temp;

	ret = kstrtou8(buf, 16, &temp);
	if (ret)
		return -EINVAL;
	else {
		switch (temp) {
			case 0 :
				s2asl01_powermeter_onoff(switching, false);
				break;
			case 1 :
				s2asl01_powermeter_onoff(switching, true);
				break;
			default :
				pr_info("%s : wrong input\n",__func__);
				break;
		}
	}
	return count;
}

static ssize_t  v_referesh_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;
	u8 temp;
	int refresh_time;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_PM_V_OPTION, &temp);
	refresh_time = (temp & 0x30) >> 4;

	return snprintf(buf, PAGE_SIZE, "V referesh time %d\n", refresh_time);
}

static ssize_t v_referesh_time_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int ret;
	u8 temp = 0, val = 0;

	ret = kstrtou8(buf, 16, &temp);
	if (ret)
		return -EINVAL;
	else {
		switch (temp) {
		case 0 :
		case 1 :
		case 2 :
		case 3 :
			val = (temp << 4) & 0x30;
			s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_V_OPTION, val, 0x30);
			break;
		default :
			pr_info("%s : wrong input\n",__func__);
			break;
		}
	}
	return count;
}

static ssize_t  v_avg1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;
	u8 temp;
	int avg;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_PM_V_OPTION, &temp);
	avg = (temp & 0x0C) >> 2;

	return snprintf(buf, PAGE_SIZE, "V AVG1 %d\n", avg);
}

static ssize_t v_avg1_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int ret;
	u8 temp = 0, val = 0;

	ret = kstrtou8(buf, 16, &temp);
	if (ret)
		return -EINVAL;
	else {
		switch (temp) {
		case 0 :
		case 1 :
		case 2 :
		case 3 :
			val = (temp << 2) & 0x0C;
			s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_V_OPTION, val, 0x0C);
			break;
		default :
			pr_info("%s : wrong input\n",__func__);
			break;
		}
	}
	return count;
}

static ssize_t  v_avg2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;
	u8 temp;
	int avg;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_PM_V_OPTION, &temp);
	avg = (temp & 0x03);

	return snprintf(buf, PAGE_SIZE, "V AVG2 %d\n", avg);
}

static ssize_t v_avg2_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int ret;
	u8 temp = 0, val = 0;

	ret = kstrtou8(buf, 16, &temp);
	if (ret)
		return -EINVAL;
	else {
		switch (temp) {
		case 0 :
		case 1 :
		case 2 :
		case 3 :
			val = temp & 0x03;
			s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_V_OPTION, val, 0x03);
			break;
		default :
			pr_info("%s : wrong input\n",__func__);
			break;
		}
	}
	return count;
}

static ssize_t  i_referesh_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;
	u8 temp;
	int refresh_time;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_PM_I_OPTION, &temp);
	refresh_time = (temp & 0x30) >> 4;

	return snprintf(buf, PAGE_SIZE, "I referesh time %d\n", refresh_time);
}

static ssize_t i_referesh_time_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int ret;
	u8 temp = 0, val = 0;

	ret = kstrtou8(buf, 16, &temp);
	if (ret)
		return -EINVAL;
	else {
		switch (temp) {
		case 0 :
		case 1 :
		case 2 :
		case 3 :
			val = (temp << 4) & 0x30;
			s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_I_OPTION, val, 0x30);
			break;
		default :
			pr_info("%s : wrong input\n",__func__);
			break;
		}
	}

	return count;
}

static ssize_t  i_avg1_show(struct device *dev, struct device_attribute *attr, char *buf) {

	struct s2asl01_switching_data *switching = g_switching;
	u8 temp;
	int avg;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_PM_I_OPTION, &temp);
	avg = (temp & 0x0C) >> 2;

	return snprintf(buf, PAGE_SIZE, "I AVG1 %d\n", avg);
}

static ssize_t i_avg1_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int ret;
	u8 temp = 0, val = 0;

	ret = kstrtou8(buf, 16, &temp);
	if (ret)
		return -EINVAL;
	else {
		switch (temp) {
		case 0 :
		case 1 :
		case 2 :
		case 3 :
			val = (temp << 2) & 0x0C;
			s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_I_OPTION, val, 0x0C);
			break;
		default :
			pr_info("%s : wrong input\n",__func__);
			break;
		}
	}
	return count;
}

static ssize_t  i_avg2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;
	u8 temp;
	int avg;

	s2asl01_read_reg(switching->client, S2ASL01_SWITCHING_PM_I_OPTION, &temp);
	avg = (temp & 0x03);

	return snprintf(buf, PAGE_SIZE, "I AVG2 %d\n", avg);
}

static ssize_t i_avg2_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int ret;
	u8 temp = 0, val = 0;

	ret = kstrtou8(buf, 16, &temp);
	if (ret)
		return -EINVAL;
	else {
		switch (temp) {
		case 0 :
		case 1 :
		case 2 :
		case 3 :
			val = temp & 0x03;
			s2asl01_update_reg(switching->client, S2ASL01_SWITCHING_PM_V_OPTION, val, 0x03);
			break;
		default :
			pr_info("%s : wrong input\n",__func__);
			break;
		}
	}
	return count;
}

static ssize_t addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;

	return sprintf(buf, "0x%x\n", switching->addr);
}

static ssize_t addr_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int x;

	if (sscanf(buf, "0x%x\n", &x) == 1)
		switching->addr = (u8)(x & 0xff);

	return count;
}

static ssize_t data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct s2asl01_switching_data *switching = g_switching;
	s2asl01_read_reg(switching->client, switching->addr, &switching->data);

	return sprintf(buf, "0x%x\n", switching->data);
}

static ssize_t data_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct s2asl01_switching_data *switching = g_switching;
	int x;
	u8 val;

	if (sscanf(buf, "0x%x\n", &x) == 1) {
		val = (u8)(x & 0xff);
		switching->data = val;
		s2asl01_write_reg(switching->client, switching->addr, switching->data);
		pr_info("%s : addr, : 0x%x, data : 0x%x\n", __func__, switching->addr, switching->data);
	}

	return count;
}

static ssize_t fastchg_curr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int curr_ma;
	curr_ma = s2asl01_get_fast_charging_current_limit(g_switching);
	chg_err("fastchg_curr_show:%d\n", curr_ma);
	return sprintf(buf, "%d\n", curr_ma);
}

static ssize_t fastchg_curr_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int curr_ma;

	if (sscanf(buf, "%d\n", &curr_ma)) {
		chg_err("fastchg_curr_store:%d\n", curr_ma);
		s2asl01_switching_set_fastcharge_current(curr_ma);
	}

	return count;
}

static ssize_t dischg_curr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int curr_ma;
	curr_ma = s2asl01_get_dischg_charging_current_limit(g_switching);
	chg_err("dischg_curr_show:%d\n", curr_ma);
	return sprintf(buf, "%d\n", curr_ma);
}

static ssize_t dischg_curr_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int curr_ma;

	if (sscanf(buf, "%d\n", &curr_ma)) {
		chg_err("dischg_curr_store:%d\n", curr_ma);
		s2asl01_switching_set_discharge_current(curr_ma);
	}

	return count;
}

static int s2asl01_switch_enable(struct s2asl01_platform_data *chip, int enable)
{
	if (chip == NULL) {
		chg_err("pdata->en_gpio not specified\n");
		return -1;
	}
	if (gpio_is_valid(chip->en_gpio)) {
		gpio_direction_output(chip->en_gpio, enable);
		msleep(10);
		chg_err("pdata->en_gpio level:%d\n",gpio_get_value(chip->en_gpio));
	}
	return 0;
}

int get_switching_hw_enable(void)
{
	if (g_switching == NULL) {
		chg_err("g_switching not specified\n");
		return 0;
	}

	if (gpio_is_valid(g_switching->pdata->en_gpio)) {
		return gpio_get_value(g_switching->pdata->en_gpio);
	} else {
		chg_err("en_gpio not specified\n");
		return 0;
	}
	return 0;
}

int get_switching_charge_enable(void)
{
	if (g_switching == NULL) {
		chg_err("g_switching not specified\n");
		return 0;
	}

	if (g_switching->supllement_mode == false)
		return true;
	else
		return false;
}

int switching_hw_enable(int enable)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if(s2asl01_boot_mode == ATE_FACTORY_BOOT || s2asl01_boot_mode == FACTORY_BOOT || g_switching->switching_chip->error_status != 0) {
#else
	if(s2asl01_boot_mode == MSM_BOOT_MODE__FACTORY || g_switching->switching_chip->error_status != 0) {
#endif
		chg_err(" boot_mode:%d, disabled switch\n",s2asl01_boot_mode);
		s2asl01_switch_enable(g_switching->pdata,0);
		return -1;
	} else
#endif
	{
		s2asl01_switch_enable(g_switching->pdata,enable);
		return 0;
	}
}

int s2asl01_switching_set_discharge_current(int discharge_current_ma)
{
	if(get_switching_hw_enable() == 0) {
		chg_err("switch not enabled\n");
		return 0;
	}
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if(s2asl01_boot_mode == ATE_FACTORY_BOOT || s2asl01_boot_mode == FACTORY_BOOT) {
#else
	if(s2asl01_boot_mode == MSM_BOOT_MODE__FACTORY) {
#endif
		chg_err(" boot_mode:%d, disabled switch\n",s2asl01_boot_mode);
		return -1;
	} else
#endif
	{
		chg_err("discharge_current_ma:%d\n", discharge_current_ma);
		s2asl01_set_dischg_charging_current_limit(g_switching, discharge_current_ma);
		s2asl01_set_supllement_mode(g_switching, false);
		return 0;
	}
}

int s2asl01_switching_set_fastcharge_current(int charge_curr_ma) {
	chg_err("charge_curr_ma:%d\n", charge_curr_ma);
	if(get_switching_hw_enable() == 0) {
		chg_err("switch not enabled\n");
		return 0;
	}
	s2asl01_set_fast_charging_current_limit(g_switching, charge_curr_ma);
	s2asl01_set_supllement_mode(g_switching, false);
	s2asl01_set_chg_mode(g_switching, 0x0);
	return 0;
}

int s2asl01_switching_enable_charge(int en) {
	chg_err("enable:%d\n", en);
	if(get_switching_hw_enable() == 0) {
		chg_err("switch not enabled\n");
		return 0;
	}
	if(en) {
		s2asl01_set_supllement_mode(g_switching, false);
	} else {
		s2asl01_set_supllement_mode(g_switching, true);
	}
	return 0;
}

int s2asl01_switching_get_fastchg_curr(void) {
	int curr_ma;
	curr_ma = s2asl01_get_fast_charging_current_limit(g_switching);
	chg_err("fastchg_curr_show:%d\n", curr_ma);
	return curr_ma;
}

int s2asl01_switching_get_discharge_curr(void) {
	int curr_ma;
	curr_ma = s2asl01_get_dischg_charging_current_limit(g_switching);
	chg_err("fastchg_curr_show:%d\n", curr_ma);
	return curr_ma;
}

int s2asl01_switching_set_discharge_mode(int mode) {
	s2asl01_set_dischg_mode(g_switching, mode);
	chg_err("s2asl01_switching_set_discharge_mode:%d\n", mode);
	return 0;
}
static DEVICE_ATTR(powermeter_en, 0644, powermeter_en_show, powermeter_en_store);
static DEVICE_ATTR(v_referesh_time, 0644, v_referesh_time_show, v_referesh_time_store);
static DEVICE_ATTR(v_avg1, 0644, v_avg1_show, v_avg1_store);
static DEVICE_ATTR(v_avg2, 0644, v_avg2_show, v_avg2_store);
static DEVICE_ATTR(i_referesh_time, 0644, i_referesh_time_show, i_referesh_time_store);
static DEVICE_ATTR(i_avg1, 0644, i_avg1_show, i_avg1_store);
static DEVICE_ATTR(i_avg2, 0644, i_avg2_show, i_avg2_store);
static DEVICE_ATTR(addr, 0644, addr_show, addr_store);
static DEVICE_ATTR(data, 0644, data_show, data_store);
static DEVICE_ATTR(fastchg_curr, 0644, fastchg_curr_show, fastchg_curr_store);
static DEVICE_ATTR(dischg_curr, 0644, dischg_curr_show, dischg_curr_store);
static struct attribute *s2asl01_attributes[] = {
	/* power_meter setting */
	&dev_attr_powermeter_en.attr,
	&dev_attr_v_referesh_time.attr,
	&dev_attr_v_avg1.attr,
	&dev_attr_v_avg2.attr,
	&dev_attr_i_referesh_time.attr,
	&dev_attr_i_avg1.attr,
	&dev_attr_i_avg2.attr,
	/* I2C read & write for debug */
	&dev_attr_addr.attr,
	&dev_attr_data.attr,
	&dev_attr_fastchg_curr.attr,
	&dev_attr_dischg_curr.attr,
	NULL,
};

static const struct attribute_group s2asl01_attr_group = {
	.attrs = s2asl01_attributes,
};

static void s2asl01_init_regs(struct s2asl01_switching_data *switching)
{
	pr_info("%s: s2asl01 switching initialize\n", __func__);

	/* Set supllement mode */
	s2asl01_set_supllement_mode(switching, false);

	s2asl01_test_read(switching->client);

	/* TSD function */
	if (switching->pdata->tsd_en)
		s2asl01_tsd_onoff(switching, true);

}

struct power_supply_desc s2asl01_main_power_supply_desc = {
	.name = "s2asl01-switching-main",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = s2asl01_main_props,
	.num_properties = ARRAY_SIZE(s2asl01_main_props),
	.get_property = s2asl01_get_property,
	.set_property = s2asl01_set_property,
	.no_thermal = true,
};

struct power_supply_desc s2asl01_sub_power_supply_desc = {
	.name = "s2asl01-switching-sub",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = s2asl01_sub_props,
	.num_properties = ARRAY_SIZE(s2asl01_sub_props),
	.get_property = s2asl01_get_property,
	.set_property = s2asl01_set_property,
	.no_thermal = true,
};

struct oplus_switch_operations s2asl01_switching_ops = {
	.switching_hw_enable = switching_hw_enable,
	.switching_set_fastcharge_current = s2asl01_switching_set_fastcharge_current,
	.switching_set_discharge_current = s2asl01_switching_set_discharge_current,
	.switching_enable_charge = s2asl01_switching_enable_charge,
	.switching_get_hw_enable = get_switching_hw_enable,
	.switching_get_fastcharge_current = s2asl01_switching_get_fastchg_curr,
	.switching_get_discharge_current = s2asl01_switching_get_discharge_curr,
	.switching_get_charge_enable = get_switching_charge_enable,
	.switching_set_discharge_mode = s2asl01_switching_set_discharge_mode,
};
static int s2asl01_switching_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *of_node = client->dev.of_node;
	struct s2asl01_switching_data *switching;
	struct s2asl01_platform_data *pdata = client->dev.platform_data;
	int ret = 0;
	int batt_volt;
	int sub_batt_volt;
	bool batt_authenticate;
	bool sub_batt_authenticate;
	int error_status = 0;
	int gauge_statue = 0;
	int balance_status = 0;
	gauge_statue = oplus_gauge_get_batt_soc();
	if (gauge_statue == -1) {
		chg_err("gauge0 not ready, will do after gauge0 init\n");
		return -EPROBE_DEFER;
	}
	gauge_statue = oplus_gauge_get_sub_batt_soc();
	if (gauge_statue == -1) {
		chg_err("gauge1 not ready, will do after gauge1 init\n");
		return -EPROBE_DEFER;
	}
	batt_volt = oplus_gauge_get_batt_mvolts();
	sub_batt_volt = oplus_gauge_get_sub_batt_mvolts();
	batt_authenticate = oplus_gauge_get_batt_authenticate();
	sub_batt_authenticate = oplus_gauge_get_sub_batt_authenticate();
	s2asl01_boot_mode = get_boot_mode();
	chg_err(" batt_authenticate:%d, sub_batt_authenticate:%d\n", batt_authenticate, sub_batt_authenticate);

	chg_err("%s: ###############S2ASL01 Switching Driver Loading\n", __func__);
	if (batt_authenticate != true || sub_batt_authenticate != true) {
		error_status = SWITCH_ERROR_I2C_ERROR;
		chg_err("gauge i2c error! batt_authenticate:%d, sub_batt_authenticate:%d, error_status:%d\n",
				batt_authenticate, sub_batt_authenticate, error_status);
	}
	if (batt_volt > 3400 && sub_batt_volt < 3100 && batt_volt - sub_batt_volt > 1000) {
		error_status = SWITCH_ERROR_OVER_VOLTAGE;
	}

	if (sub_batt_volt - batt_volt > SINGLE_BATT_VOLT_MV) {
		balance_status = PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE;
	}
	if (of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		ret = s2asl01_switching_parse_dt(&client->dev, pdata);
		if (ret < 0)
			goto err_parse_dt;
	} else {
		pdata = client->dev.platform_data;
	}

	switching = kzalloc(sizeof(*switching), GFP_KERNEL);

	if (switching == NULL) {
		ret = -ENOMEM;
		goto err_limiter_nomem;
	}
	switching->dev = &client->dev;

	ret = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
							I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK);
	if (!ret) {
		ret = i2c_get_functionality(client->adapter);
		dev_err(switching->dev, "I2C functionality is not supported.\n");
		ret = -EINVAL;
		goto err_i2cfunc_not_support;
	}
	switching->client = client;
	switching->pdata = pdata;

	i2c_set_clientdata(client, switching);
	mutex_init(&switching->i2c_lock);

	switching->in_ok = false;
	switching->supllement_mode = false;
	switching->power_meter = false;
#if 0
	pr_info("%s [%s]: enb = %d\n",
		__func__, current_limiter_type_str[switching->pdata->bat_type],
		gpio_get_value(switching->pdata->bat_enb));
#endif

	/* sysfs create */
	ret = sysfs_create_group(&dev->kobj, &s2asl01_attr_group);
	if (ret) {
		pr_err("%s sysfs_create_group failed\n", __func__);
		goto err_irq;
	}
	if (error_status == 0 && balance_status != PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE) {
		s2asl01_switch_enable(pdata, 1);
		ret = s2asl01_get_rev_id(switching);
		if (ret) {
			pr_err("%s s2asl01_get_rev_id failed\n", __func__);
			error_status = SWITCH_ERROR_I2C_ERROR;
		} else {
			s2asl01_init_regs(switching);
		}
	}
	g_switching = switching;

	g_switching->switching_chip = devm_kzalloc(&client->dev,
		sizeof(struct oplus_switch_chip), GFP_KERNEL);
	if (!g_switching->switching_chip) {
		chg_err("kzalloc() failed.\n");
		g_switching->switching_chip = NULL;
		goto oplus_switch_free;
	}
	g_switching->switching_chip->switch_ops = &s2asl01_switching_ops;
	g_switching->switching_chip->client = g_switching->client;
	g_switching->switching_chip->dev = g_switching->dev;
	g_switching->switching_chip->error_status = error_status;
	atomic_set(&g_switching->suspended, 0);
	oplus_switching_init(g_switching->switching_chip, PARALLEL_SWITCH_IC);

	if (error_status != 0) {
		s2asl01_switch_enable(pdata, 0);
		chg_err("%s: #######################S2ASL01 Switching Driver Loaded error_status !!!\n", __func__);
		return 0;
	}

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if(s2asl01_boot_mode == ATE_FACTORY_BOOT || s2asl01_boot_mode == FACTORY_BOOT) {
#else
	if(s2asl01_boot_mode == MSM_BOOT_MODE__FACTORY  || balance_status == PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE) {
#endif
		chg_err(" boot_mode:%d, disabled switch\n",s2asl01_boot_mode);
		s2asl01_switch_enable(pdata,0);
	} else
#endif
	{
		s2asl01_switch_enable(pdata,1);
		s2asl01_set_dischg_mode(switching, 0x0);
		s2asl01_set_chg_mode(switching, 0x0);
		s2asl01_switching_set_fastcharge_current(200);
	}

	chg_err("%s: #######################S2ASL01 Switching Driver Loaded\n", __func__);
	return 0;

oplus_switch_free:
kfree(g_switching->switching_chip);
err_irq:
err_i2cfunc_not_support:
	kfree(switching);
err_parse_dt:
err_limiter_nomem:
	devm_kfree(&client->dev, pdata);

	return ret;
}

static const struct i2c_device_id s2asl01_switching_id[] = {
	{"s2asl01-switching", 0},
	{}
};

static void s2asl01_switching_shutdown(struct i2c_client *client)
{
	pr_info("%s\n", __func__);
}

static int s2asl01_switching_remove(struct i2c_client *client)
{
	struct s2asl01_switching_data *switching = i2c_get_clientdata(client);

	power_supply_unregister(switching->psy_sw);
	mutex_destroy(&switching->i2c_lock);
	return 0;
}

#if IS_ENABLED(CONFIG_PM)
static int s2asl01_switching_suspend(struct device *dev)
{
	atomic_set(&g_switching->suspended, 1);
	pr_info("%s suspended:%d\n", __func__, g_switching->suspended);
	return 0;
}

static int s2asl01_switching_resume(struct device *dev)
{
	atomic_set(&g_switching->suspended, 0);
	pr_info("%s suspended:%d\n", __func__, g_switching->suspended);
	return 0;
}
#else
#define s2asl01_switching_suspend NULL
#define s2asl01_switching_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(s2asl01_switching_pm_ops, s2asl01_switching_suspend, s2asl01_switching_resume);

static struct i2c_driver s2asl01_switching_driver = {
	.driver = {
		.name = "s2asl01-switching",
		.owner = THIS_MODULE,
		.pm = &s2asl01_switching_pm_ops,
		.of_match_table = s2asl01_switching_match_table,
	},
	.probe  = s2asl01_switching_probe,
	.remove = s2asl01_switching_remove,
	.shutdown   = s2asl01_switching_shutdown,
	.id_table   = s2asl01_switching_id,
};

int s2asl01_switching_init(void)
{
	pr_info("%s: S2ASL01 Switching Init\n", __func__);
	return i2c_add_driver(&s2asl01_switching_driver);
}
void s2asl01_switching_exit(void)
{
	i2c_del_driver(&s2asl01_switching_driver);
}

MODULE_DESCRIPTION("Samsung S2ASL01 Switching IC Driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
