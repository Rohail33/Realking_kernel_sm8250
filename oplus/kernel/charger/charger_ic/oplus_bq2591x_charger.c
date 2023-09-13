// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

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
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>

#include "oplus_bq2591x_reg.h"
#include <mt-plat/upmu_common.h>
#include <mt-plat/mtk_boot.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_class.h>
#include <mt-plat/charger_type.h>
#else
#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/charger_type.h>
#endif

#include "../../mediatek/charger/mtk_charger_intf.h"
#include "../oplus_charger.h"

extern void set_charger_ic(int sel);

static struct bq2591x *g_bq;
static int bq2591x_online = false;

enum bq2591x_part_no {
	BQ25910 = 0x01,
};

enum {
	USER	= BIT(0),
	PARALLEL = BIT(1),
};


struct bq2591x_config {
	int chg_mv;
	int chg_ma;

	int ivl_mv;
	int icl_ma;

	int iterm_ma;
	int batlow_mv;
	
	bool enable_term;
};


struct bq2591x {
	struct device	*dev;
	struct i2c_client *client;
	struct charger_device *chg_dev;
	struct charger_properties chg_props;
	const char *chg_dev_name;
	
	enum bq2591x_part_no part_no;
	int revision;

	struct bq2591x_config cfg;
	struct delayed_work monitor_work;

	bool charge_enabled;
	bool hz_mode;
	int chg_mv;
	int chg_ma;
	int ivl_mv;
	int icl_ma;

	int charge_state;
	int fault_status;

	int prev_stat_flag;
	int prev_fault_flag;

	int reg_stat;
	int reg_fault;
	int reg_stat_flag;
	int reg_fault_flag;

	int skip_reads;
	int skip_writes;
	int pre_current_ma;
	enum charger_type chg_type;

	struct mutex i2c_rw_lock;
};

static const struct charger_properties bq2591x_chg_props = {
	.alias_name = "bq2591x",
};

static int __bq2591x_read_reg(struct bq2591x *bq, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(bq->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	*data = (u8)ret;
	return 0;
}

static int __bq2591x_write_reg(struct bq2591x *bq, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(bq->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
				val, reg, ret);
		return ret;
	}
	return 0;
}

static int bq2591x_read_byte(struct bq2591x *bq, u8 *data, u8 reg)
{
	int ret;

	if (bq->skip_reads) {
		*data = 0;
		return 0;
	}

	mutex_lock(&bq->i2c_rw_lock);
	ret = __bq2591x_read_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}


static int bq2591x_write_byte(struct bq2591x *bq, u8 reg, u8 data)
{
	int ret;

	if (bq->skip_writes)
		return 0;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __bq2591x_write_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}


static int bq2591x_update_bits(struct bq2591x *bq, u8 reg,
					u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	if (bq->skip_reads || bq->skip_writes)
		return 0;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __bq2591x_read_reg(bq, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __bq2591x_write_reg(bq, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}

static int bq2591x_enable_charger(struct bq2591x *bq)
{
	int ret;
	u8 val = BQ2591X_CHG_ENABLE << BQ2591X_EN_CHG_SHIFT;

	ret = bq2591x_update_bits(bq, BQ2591X_REG_06,
				BQ2591X_EN_CHG_MASK, val);
	return ret;
}

static int bq2591x_disable_charger(struct bq2591x *bq)
{
	int ret;
	u8 val = BQ2591X_CHG_DISABLE << BQ2591X_EN_CHG_SHIFT;

	ret = bq2591x_update_bits(bq, BQ2591X_REG_06,
				BQ2591X_EN_CHG_MASK, val);

	return ret;
}

static int bq2591x_enable_term(struct bq2591x *bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2591X_TERM_ENABLE;
	else
		val = BQ2591X_TERM_DISABLE;

	val <<= BQ2591X_EN_TERM_SHIFT;

	ret = bq2591x_update_bits(bq, BQ2591X_REG_05,
				BQ2591X_EN_TERM_MASK, val);

	return ret;
}

static int bq2591x_set_chargecurrent(struct charger_device *chg_dev, u32 curr)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 ichg;
	
	curr /= 1000; /*to mA */
	ichg = (curr - BQ2591X_ICHG_BASE)/BQ2591X_ICHG_LSB;

	ichg <<= BQ2591X_ICHG_SHIFT;

	return bq2591x_update_bits(bq, BQ2591X_REG_01,
				BQ2591X_ICHG_MASK, ichg);

}

static int bq2591x_get_chargecurrent(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 val;
	int ret;
	int ichg;
	
	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_01);
	if (!ret) {
		ichg = ((u32)(val & BQ2591X_ICHG_MASK)  >> BQ2591X_ICHG_SHIFT) * BQ2591X_ICHG_LSB + BQ2591X_ICHG_BASE;
		*curr = ichg * 1000; /*to uA*/
	}
	
	return ret;
}

static int bq2591x_get_min_ichg(struct charger_device *chg_dev, u32 *curr)
{
	int ret = 0;
	
	*curr = 500*1000;/*500mA*/
	
	return ret;
}
/*
static int bq2591x_set_term_current(struct bq2591x *bq, int curr)
{
	u8 iterm;

	if (curr == 500)
		iterm = BQ2591X_ITERM_500MA;
	else if (curr == 650)
		iterm = BQ2591X_ITERM_650MA;
	else if (curr == 800)
		iterm = BQ2591X_ITERM_800MA;
	else if (curr == 1000)
		iterm = BQ2591X_ITERM_1000MA;
	else
		iterm = BQ2591X_ITERM_1000MA;

	iterm <<= BQ2591X_ITERM_SHIFT;

	return bq2591x_update_bits(bq, BQ2591X_REG_04,
				BQ2591X_ITERM_MASK, iterm);
}
*/
static int bq2591x_set_chargevoltage(struct charger_device *chg_dev, u32 volt)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 val;

	volt /= 1000; /*to mV*/
	val = (volt - BQ2591X_VREG_BASE)/BQ2591X_VREG_LSB;
	val <<= BQ2591X_VREG_SHIFT;
	
	return bq2591x_update_bits(bq, BQ2591X_REG_00,
				BQ2591X_VREG_MASK, val);
}

static int bq2591x_get_chargevoltage(struct charger_device *chg_dev, u32 *cv)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 val;
	int ret;
	int volt;
	
	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_00);
	if (!ret) {
		volt = val & BQ2591X_VREG_MASK;
		volt = (volt >> BQ2591X_VREG_SHIFT) * BQ2591X_VREG_LSB;
		volt = volt + BQ2591X_VREG_BASE;
		*cv = volt * 1000; /*to uV*/
	}
	
	return ret;
}

static int bq2591x_set_input_volt_limit(struct charger_device *chg_dev, u32 volt)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 val;

	volt /= 1000; /*to mV*/
	val = (volt - BQ2591X_VINDPM_BASE) / BQ2591X_VINDPM_LSB;
	val <<= BQ2591X_VINDPM_SHIFT;

	return bq2591x_update_bits(bq, BQ2591X_REG_02,
				BQ2591X_VINDPM_MASK, val);
}


static int bq2591x_set_input_current_limit(struct charger_device *chg_dev, u32 curr)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 val;
	
	curr /= 1000;/*to mA*/
	val = (curr - BQ2591X_IINLIM_BASE) / BQ2591X_IINLIM_LSB;
	val <<= BQ2591X_IINLIM_SHIFT;

	return bq2591x_update_bits(bq, BQ2591X_REG_03,
				BQ2591X_IINLIM_MASK, val);
}

static int bq2591x_get_input_current_limit(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 val;
	int ret;
	int ilim;
	
	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_03);
	if (!ret) {
		ilim = val & BQ2591X_IINLIM_MASK;
		ilim = (ilim >> BQ2591X_IINLIM_SHIFT) * BQ2591X_IINLIM_LSB;
		ilim = ilim + BQ2591X_IINLIM_BASE;
		*curr = ilim; /*to uA*/
	}
	
	return ret;								 
}

int bq2591x_set_watchdog_timer(struct bq2591x *bq, u8 timeout)
{
	u8 val;

	val = (timeout - BQ2591X_WDT_BASE) / BQ2591X_WDT_LSB;

	val <<= BQ2591X_WDT_SHIFT;

	return bq2591x_update_bits(bq, BQ2591X_REG_05,
				BQ2591X_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2591x_set_watchdog_timer);

int bq2591x_disable_watchdog_timer(struct bq2591x *bq)
{
	u8 val = BQ2591X_WDT_DISABLE << BQ2591X_WDT_SHIFT;

	return bq2591x_update_bits(bq, BQ2591X_REG_05,
				BQ2591X_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2591x_disable_watchdog_timer);

static int bq2591x_reset_watchdog_timer(struct charger_device *chg_dev)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	
	u8 val = BQ2591X_WDT_RESET << BQ2591X_WDT_RESET_SHIFT;

	return bq2591x_update_bits(bq, BQ2591X_REG_05,
				BQ2591X_WDT_RESET_MASK, val);
}

int bq2591x_reset_chip(struct bq2591x *bq)
{
	int ret;
	u8 val = BQ2591X_RESET << BQ2591X_RESET_SHIFT;

	ret = bq2591x_update_bits(bq, BQ2591X_REG_0D,
				BQ2591X_RESET_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2591x_reset_chip);

int bq2591x_exit_hiz_mode(struct bq2591x *bq)
{
	int ret;
	u8 val = BQ2591X_WDT_RESET << BQ2591X_WDT_RESET_SHIFT;

	ret = bq2591x_update_bits(bq, BQ2591X_REG_05,
				BQ2591X_WDT_RESET_MASK, val);
	ret	= bq2591x_enable_charger(bq);
	bq->hz_mode = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(bq2591x_exit_hiz_mode);

int bq2591x_enter_hiz_mode(struct bq2591x *bq)
{
	int ret = 0;
	bq->hz_mode = 1;
	return ret;
}
EXPORT_SYMBOL_GPL(bq2591x_enter_hiz_mode);

static int bq2591x_set_vbatlow_volt(struct bq2591x *bq, int volt)
{
	int ret;
	u8 val;

	if (volt == 2600)
		val = BQ2591X_VBATLOWV_2600MV;
	else if (volt == 2900)
		val = BQ2591X_VBATLOWV_2900MV;
	else if (volt == 3200)
		val = BQ2591X_VBATLOWV_3200MV;
	else if (volt == 3500)
		val = BQ2591X_VBATLOWV_3500MV;
	else
		val = BQ2591X_VBATLOWV_3500MV;

	val <<= BQ2591X_VBATLOWV_SHIFT;

	ret = bq2591x_update_bits(bq, BQ2591X_REG_06,
				BQ2591X_VBATLOWV_MASK, val);

	return ret;
}

static int bq2591x_enable_safety_timer(struct charger_device *chg_dev,
											bool enable)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 val;
	int ret;
	
	if (enable)
		val = BQ2591X_CHG_TIMER_ENABLE;
	else
		val = BQ2591X_CHG_TIMER_DISABLE;
	
	val <<= BQ2591X_EN_TIMER_SHIFT;
	
	ret = bq2591x_update_bits(bq, BQ2591X_REG_05,
				BQ2591X_EN_TIMER_MASK, val);
	return ret;
}

static int bq2591x_is_safety_timer_enable(struct charger_device *chg_dev,
	bool *en)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 val;
	
	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_05);
	if (!ret)
		*en = !!(val & BQ2591X_EN_TIMER_MASK);
	
	return ret;
}	

static ssize_t bq2591x_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bq2591x *bq = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "bq2591x Reg");
	for (addr = 0x0; addr <= 0x0D; addr++) {
		ret = bq2591x_read_byte(bq, &val, addr);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
					"Reg[%02X] = 0x%02X\n",	addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t bq2591x_store_registers(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq2591x *bq = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x0D)
		bq2591x_write_byte(bq, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, bq2591x_show_registers,
						bq2591x_store_registers);

static struct attribute *bq2591x_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group bq2591x_attr_group = {
	.attrs = bq2591x_attributes,
};

static int bq2591x_charging(struct charger_device *chg_dev, bool enable)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	u8 val;

	if (enable)
		ret = bq2591x_enable_charger(bq);
	else
		ret = bq2591x_disable_charger(bq);

	pr_err("%s charger %s\n", enable ? "enable" : "disable",
				  !ret ? "successfully" : "failed");
	
	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_06);

	if (!ret)
		bq->charge_enabled = !!(val & BQ2591X_EN_CHG_MASK);
	
	return ret;
}

static int bq2591x_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	
	*en = bq->charge_enabled;
	
	return 0;
}

static int bq2591x_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 val;
	
	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_09);
	if (!ret) {
		*done = !!(val & BQ2591X_CHRG_TERM_FLAG_MASK);	
	}
	
	return ret;
}


static int bq2591x_parse_dt(struct device *dev, struct bq2591x *bq)
{
	int ret;
	struct device_node *np = dev->of_node;

	if (of_property_read_string(np, "charger_name", &bq->chg_dev_name) < 0) {
		bq->chg_dev_name = "secondary_chg";
		pr_warn("no charger name\n");
	}

	bq->charge_enabled = !(of_property_read_bool(np, "ti,charging-disabled"));
	bq->cfg.enable_term = of_property_read_bool(np, "ti,bq2591x,enable-term");

	ret = of_property_read_u32(np, "ti,bq2591x,charge-voltage",
					&bq->cfg.chg_mv);
	if (ret) {
		bq->cfg.chg_mv = 4400;
		pr_err("Failed to read node of ti,bq2591x,charge-voltage\n");
		return ret;
	}

	ret = of_property_read_u32(np, "ti,bq2591x,charge-current",
					&bq->cfg.chg_ma);
	if (ret) {
		bq->cfg.chg_ma = 500;
		pr_err("Failed to read node of ti,bq2591x,charge-voltage\n");
		return ret;
	}
	ret = of_property_read_u32(np, "ti,bq2591x,input-current-limit",
					&bq->cfg.icl_ma);
	if (ret) {
		bq->cfg.icl_ma = 500;
		pr_err("Failed to read node of ti,bq2591x,charge-voltage\n");
		return ret;
	}

	ret = of_property_read_u32(np, "ti,bq2591x,input-voltage-limit",
					&bq->cfg.ivl_mv);
	if (ret) {
		bq->cfg.icl_ma = 4400;
		pr_err("Failed to read node of ti,bq2591x,charge-voltage\n");
		return ret;
	}

	ret = of_property_read_u32(np, "ti,bq2591x,vbatlow-volt",
					&bq->cfg.batlow_mv);
	if (ret) {
		bq->cfg.batlow_mv = 3500;
		pr_err("Failed to read node of ti,bq2591x,charge-voltage\n");
		return ret;
	}

	ret = of_property_read_u32(np, "ti,bq2591x,term-current",
					&bq->cfg.iterm_ma);
	if (ret) {
		bq->cfg.iterm_ma = 250;
		pr_err("Failed to read node of ti,bq2591x,charge-voltage\n");
		return ret;
	}

	return ret;
}

static int bq2591x_detect_device(struct bq2591x *bq)
{
	int ret;
	u8 data;

	ret = bq2591x_read_byte(bq, &data, BQ2591X_REG_0D);
	if (ret == 0) {
		bq->part_no = (data & BQ2591X_PN_MASK) >> BQ2591X_PN_SHIFT;
		bq->revision = (data & BQ2591X_DEV_REV_MASK) >> BQ2591X_DEV_REV_SHIFT;
		bq2591x_online = true;
	}
	return ret;
}

bool bq2591x_is_detected(void)
{
	return bq2591x_online;
}

static int bq2591x_set_charge_profile(struct bq2591x *bq)
{
	int ret;

	ret = bq2591x_set_chargevoltage(bq->chg_dev, bq->cfg.chg_mv * 1000);
	if (ret < 0) {
		pr_err("Failed to set charge voltage:%d\n", ret);
		return ret;
	}

	ret = bq2591x_set_chargecurrent(bq->chg_dev, bq->cfg.chg_ma * 1000);
	if (ret < 0) {
		pr_err("Failed to set charge current:%d\n", ret);
		return ret;
	}

	ret = bq2591x_set_input_current_limit(bq->chg_dev, bq->cfg.icl_ma * 1000);
	if (ret < 0) {
		pr_err("Failed to set input current limit:%d\n", ret);
		return ret;
	}

	ret = bq2591x_set_input_volt_limit(bq->chg_dev, bq->cfg.ivl_mv * 1000);
	if (ret < 0) {
		pr_err("Failed to set input voltage limit:%d\n", ret);
		return ret;
	}
	return 0;
}

static int bq2591x_init_device(struct bq2591x *bq)
{
	int ret;
	bq->chg_mv = bq->cfg.chg_mv;
	bq->chg_ma = bq->cfg.chg_ma;
	bq->ivl_mv = bq->cfg.ivl_mv;
	bq->icl_ma = bq->cfg.icl_ma;

	ret = bq2591x_disable_watchdog_timer(bq);
	if (ret < 0)
		pr_err("Failed to disable watchdog timer:%d\n", ret);

	ret = bq2591x_enable_term(bq, bq->cfg.enable_term);
	if (ret < 0)
		pr_err("Failed to %s termination:%d\n",
			bq->cfg.enable_term ? "enable" : "disable", ret);

	ret = bq2591x_set_vbatlow_volt(bq, bq->cfg.batlow_mv);
	if (ret < 0)
		pr_err("Failed to set vbatlow volt to %d,rc=%d\n",
					bq->cfg.batlow_mv, ret);

//	bq2591x_set_term_current(bq, bq->cfg.iterm_ma * 1000);
	
	bq2591x_set_charge_profile(bq);

	if (bq->charge_enabled)
		ret = bq2591x_enable_charger(bq);
	else
		ret = bq2591x_disable_charger(bq);

	if (ret < 0)
		pr_err("Failed to %s charger:%d\n",
			bq->charge_enabled ? "enable" : "disable", ret);

	return 0;
}
/*
static void bq2591x_dump_regs(struct bq2591x *bq)
{
	int ret;
	u8 addr;
	u8 val;

	for (addr = 0x00; addr <= 0x0D; addr++) {
		msleep(2);
		ret = bq2591x_read_byte(bq, &val, addr);
		if (!ret)
			pr_err("Reg[%02X] = 0x%02X\n", addr, val);
	}

}
*/
static void bq2591x_stat_handler(struct bq2591x *bq)
{
	if (bq->prev_stat_flag == bq->reg_stat_flag)
		return;

	bq->prev_stat_flag = bq->reg_stat_flag;
	pr_info("%s\n", (bq->reg_stat & BQ2591X_PG_STAT_MASK) ?
					"Power Good" : "Power Poor");

	if (bq->reg_stat & BQ2591X_IINDPM_STAT_MASK)
		pr_info("IINDPM Triggered\n");

	if (bq->reg_stat & BQ2591X_VINDPM_STAT_MASK)
		pr_info("VINDPM Triggered\n");

	if (bq->reg_stat & BQ2591X_TREG_STAT_MASK)
		pr_info("TREG Triggered\n");

	if (bq->reg_stat & BQ2591X_WD_STAT_MASK)
		pr_err("Watchdog overflow\n");

	bq->charge_state = (bq->reg_stat & BQ2591X_CHRG_STAT_MASK)
					>> BQ2591X_CHRG_STAT_SHIFT;
	if (bq->charge_state == BQ2591X_CHRG_STAT_NCHG)
		pr_info("Not Charging\n");
	else if (bq->charge_state == BQ2591X_CHRG_STAT_FCHG)
		pr_info("Fast Charging\n");
	else if (bq->charge_state == BQ2591X_CHRG_STAT_TCHG)
		pr_info("Taper Charging\n");

}

static void bq2591x_fault_handler(struct bq2591x *bq)
{
	if (bq->prev_fault_flag == bq->reg_fault_flag)
		return;

	bq->prev_fault_flag = bq->reg_fault_flag;

	if (bq->reg_fault_flag & BQ2591X_VBUS_OVP_FLAG_MASK)
		pr_info("VBus OVP fault occured, current stat:%d",
				bq->reg_fault & BQ2591X_VBUS_OVP_STAT_MASK);

	if (bq->reg_fault_flag & BQ2591X_TSHUT_FLAG_MASK)
		pr_info("Thermal shutdown occured, current stat:%d",
				bq->reg_fault & BQ2591X_TSHUT_STAT_MASK);

	if (bq->reg_fault_flag & BQ2591X_BATOVP_FLAG_MASK)
		pr_info("Battery OVP fault occured, current stat:%d",
				bq->reg_fault & BQ2591X_BATOVP_STAT_MASK);

	if (bq->reg_fault_flag & BQ2591X_CFLY_FLAG_MASK)
		pr_info("CFLY fault occured, current stat:%d",
				bq->reg_fault & BQ2591X_CFLY_STAT_MASK);

	if (bq->reg_fault_flag & BQ2591X_TMR_FLAG_MASK)
		pr_info("Charge safety timer fault, current stat:%d",
				bq->reg_fault & BQ2591X_TMR_STAT_MASK);

	if (bq->reg_fault_flag & BQ2591X_CAP_COND_FLAG_MASK)
		pr_info("CAP conditon fault occured, current stat:%d",
				bq->reg_fault & BQ2591X_CAP_COND_STAT_MASK);
}


static irqreturn_t bq2591x_charger_interrupt(int irq, void *data)
{
	struct bq2591x *bq = data;
	int ret;
	u8  val;

	pr_info("bq2591x_charger_interrupt Triggered\n");

	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_07);
	if (ret)
		return IRQ_HANDLED;
	bq->reg_stat = val;

	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_08);
	if (ret)
		return IRQ_HANDLED;
	bq->reg_fault = val;

	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_09);
	if (ret)
		return IRQ_HANDLED;
	bq->reg_stat_flag = val;

	ret = bq2591x_read_byte(bq, &val, BQ2591X_REG_0A);
	if (ret)
		return IRQ_HANDLED;
	bq->reg_fault_flag = val;

	bq2591x_stat_handler(bq);
	bq2591x_fault_handler(bq);

	//bq2591x_dump_regs(bq);

	return IRQ_HANDLED;
}

static void bq2591x_monitor_workfunc(struct work_struct *work)
{
	struct bq2591x *bq = container_of(work, struct bq2591x, monitor_work.work);

	//bq2591x_dump_register(bq);
	
	schedule_delayed_work(&bq->monitor_work, 5 * HZ);
}


static int bq2591x_plug_in(struct charger_device *chg_dev)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	
	int ret;
	
	ret = bq2591x_charging(chg_dev, true);
	if (!ret)
		pr_err("Failed to enable charging:%d\n", ret);
	
	return ret;
	
}

static int bq2591x_plug_out(struct charger_device *chg_dev)
{
	struct bq2591x *bq = dev_get_drvdata(&chg_dev->dev);
	
	int ret;
	
	ret = bq2591x_charging(chg_dev, false);
	if (!ret)
		pr_err("Failed to enable charging:%d\n", ret);
	
	return ret;
}

static struct charger_ops bq2591x_chg_ops = {
	/* Normal charging */
	.plug_in = bq2591x_plug_in,
	.plug_out = bq2591x_plug_out,
	.dump_registers = NULL,
	.enable = bq2591x_charging,
	.is_enabled = bq2591x_is_charging_enable,
	.get_charging_current = bq2591x_get_chargecurrent,
	.set_charging_current = bq2591x_set_chargecurrent,
	.get_input_current = bq2591x_get_input_current_limit,
	.set_input_current = bq2591x_set_input_current_limit,
	.get_constant_voltage = bq2591x_get_chargevoltage,
	.set_constant_voltage = bq2591x_set_chargevoltage,
	.kick_wdt = bq2591x_reset_watchdog_timer,
	.set_mivr = NULL,
	.is_charging_done = bq2591x_is_charging_done,
	.get_min_charging_current = bq2591x_get_min_ichg,

	/* Safety timer */
	.enable_safety_timer = bq2591x_enable_safety_timer,
	.is_safety_timer_enabled = bq2591x_is_safety_timer_enable,

	/* Power path */
	.enable_powerpath = NULL,
	.is_powerpath_enabled = NULL,

	/* OTG */
	.enable_otg = NULL,
	.set_boost_current_limit = NULL,
	.enable_discharge = NULL,

	/* PE+/PE+20 */
	.send_ta_current_pattern = NULL,
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = NULL,
//	.set_ta20_reset = NULL,
//	.enable_cable_drop_comp = NULL,

	/* ADC */
	.get_tchg_adc = NULL,
};

void oplus_bq2591x_dump_registers(void)
{
	int addr;
	u8 val[25];
	int ret;
	char buf[400];
	char *s = buf;

	for (addr = 0x0; addr <= 0x0D; addr++) {
		ret = bq2591x_read_byte(g_bq, &val[addr], addr);
		msleep(1);
	}

	s+=sprintf(s,"bq2591x_dump_regs:");
	for (addr = 0x0; addr <= 0x0D; addr++){
		s+=sprintf(s,"[0x%.2x,0x%.2x]", addr, val[addr]);
	}

	s+=sprintf(s,"\n");
	dev_info(g_bq->dev,"%s",buf);
}

int oplus_bq2591x_kick_wdt(void)
{
	u8 val = BQ2591X_WDT_RESET << BQ2591X_WDT_RESET_SHIFT;

	if(g_bq->hz_mode)
		return 0;

	return bq2591x_update_bits(g_bq, BQ2591X_REG_05,
				BQ2591X_WDT_RESET_MASK, val);
}

int oplus_bq2591x_hardware_init(void)
{
	int ret = 0;

	dev_info(g_bq->dev, "%s\n", __func__);

	/* Enable WDT */
	ret = bq2591x_set_watchdog_timer(g_bq, true);
	if (ret < 0) {
		dev_notice(g_bq->dev, "%s set wdt fail(%d)\n", __func__, ret);
		return ret;
	}

	/* Enable charging */
	ret = bq2591x_enable_charger(g_bq);
	if (ret < 0)
		dev_notice(g_bq->dev, "%s en fail(%d)\n", __func__, ret);

	g_bq->charge_enabled = true;
	if (strcmp(g_bq->chg_dev_name, "secondary_chg") == 0) {
		bq2591x_exit_hiz_mode(g_bq);
   	}

	return ret;
}

int oplus_bq2591x_set_ichg(int cur)
{
	u8 ichg;
	u8 charge_enabled = 0, ret, reg_val = 0;

	dev_info(g_bq->dev, "%s:%d\n", __func__,cur);

	if (cur) {
		ret = bq2591x_read_byte(g_bq, &reg_val, BQ2591X_REG_06);
		if (!ret)
			charge_enabled = !!(reg_val & BQ2591X_EN_CHG_MASK);

		if (!charge_enabled && g_bq->charge_enabled)
			bq2591x_enable_charger(g_bq);
	}

	ichg = (cur - BQ2591X_ICHG_BASE)/BQ2591X_ICHG_LSB;
	ichg <<= BQ2591X_ICHG_SHIFT;

	return bq2591x_update_bits(g_bq, BQ2591X_REG_01,
				BQ2591X_ICHG_MASK, ichg);
}

void oplus_bq2591x_set_mivr(int vbatt)
{
	u8 val;

	vbatt = vbatt + 200;
	if(vbatt<4200) {
		vbatt = 4200;
	}

	val = (vbatt - BQ2591X_VINDPM_BASE) / BQ2591X_VINDPM_LSB;
	val <<= BQ2591X_VINDPM_SHIFT;

	bq2591x_update_bits(g_bq, BQ2591X_REG_02,
				BQ2591X_VINDPM_MASK, val);
}

static int oplus_bq2591x_set_input_current_limit(int curr)
{
	u8 val;
	if(curr < BQ2591X_IINLIM_BASE)
		curr = BQ2591X_IINLIM_BASE;

	val = (curr - BQ2591X_IINLIM_BASE) / BQ2591X_IINLIM_LSB;
	val <<= BQ2591X_IINLIM_SHIFT;

	return bq2591x_update_bits(g_bq, BQ2591X_REG_03,
				BQ2591X_IINLIM_MASK, val);
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1500, 1750, 2000, 3000,
};
int oplus_bq2591x_set_aicr(int current_ma)
{
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int aicl_point_temp = 0;

	dev_info(g_bq->dev, "%s:%d\n", __func__,current_ma);

     if (strcmp(g_bq->chg_dev_name, "secondary_chg") == 0) {
	    if (current_ma && (true == g_bq->charge_enabled)) {
			bq2591x_exit_hiz_mode(g_bq);
			bq2591x_enable_charger(g_bq);
		} else if (0 == current_ma) {
			bq2591x_enter_hiz_mode(g_bq);
			bq2591x_disable_charger(g_bq);
		}

		return oplus_bq2591x_set_input_current_limit(current_ma);
	}

	if (g_bq->pre_current_ma == current_ma)
		return rc;
	else
		g_bq->pre_current_ma = current_ma;

	dev_info(g_bq->dev, "%s usb input max current limit=%d\n", __func__,current_ma);
	aicl_point_temp = aicl_point = 4500;
	oplus_bq2591x_set_input_current_limit(4200);

	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	msleep(90);

	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		pr_debug( "use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1500 */
	aicl_point_temp = aicl_point + 50;
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	msleep(120);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 2; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 1; //We use 1.2A here
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 5; /* 1750 */
	aicl_point_temp = aicl_point + 50;
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	msleep(120);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 2; //1.2
		goto aicl_pre_step;
	}

	i = 6; /* 2000 */
	aicl_point_temp = aicl_point;
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	msleep(90);
	if (chg_vol < aicl_point_temp) {
		i =  i - 2;//1.5
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 7; /* 3000 */
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	dev_info(g_bq->dev, "%s:usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n",__func__, chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
aicl_end:
	oplus_bq2591x_set_input_current_limit(usb_icl[i]);
	dev_info(g_bq->dev, "%s:usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n",__func__, chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
}

int oplus_bq2591x_set_cv(int mv)
{
	u8 val;

	val = (mv - BQ2591X_VREG_BASE)/BQ2591X_VREG_LSB;
	val <<= BQ2591X_VREG_SHIFT;

	return bq2591x_update_bits(g_bq, BQ2591X_REG_00,
				BQ2591X_VREG_MASK, val);
}

int oplus_bq2591x_set_ieoc(int cur)
{
	return 0;
}

int oplus_bq2591x_charging_enable(void)
{
	int ret;

	dev_info(g_bq->dev, "%s\n", __func__);
	ret = bq2591x_enable_charger(g_bq);
	if (!ret) {
		g_bq->charge_enabled = true;
	}

	return ret;
}

int oplus_bq2591x_charging_disable(void)
{
	int ret;

	dev_info(g_bq->dev, "%s\n", __func__);
	/* Disable WDT */
	bq2591x_disable_watchdog_timer(g_bq);
	ret = bq2591x_disable_charger(g_bq);
	if (!ret) {
		g_bq->charge_enabled = false;
		g_bq->pre_current_ma = -1;
	}

	return ret;
}

int oplus_bq2591x_is_charging_enabled(void)
{
	return g_bq->charge_enabled;
}

int oplus_bq2591x_charger_suspend(void)
{
	return 0;
}

int oplus_bq2591x_charger_unsuspend(void)
{
	return 0;
}

int oplus_bq2591x_set_rechg_vol(int vol)
{
	return 0;
}

int oplus_bq2591x_reset_charger(void)
{
	return 0;
}

int oplus_bq2591x_is_charging_done(void)
{
	bool done = false;
	int ret;
	u8 val;

	ret = bq2591x_read_byte(g_bq, &val, BQ2591X_REG_09);
	if (!ret) {
		done = !!(val & BQ2591X_CHRG_TERM_FLAG_MASK);	
	}

	return done;
}

int oplus_bq2591x_enable_otg(void)
{
	int ret = 0;

	return ret;
}

int oplus_bq2591x_disable_otg(void)
{
	int ret = 0;

	return ret;
}

int oplus_bq2591x_disable_te(void)
{
	return  bq2591x_enable_term(g_bq, false);
}

bool oplus_bq2591x_check_charger_resume(void)
{
	return true;
}

int oplus_bq2591x_get_charger_type(void)
{
	return g_bq->chg_type;
}

void oplus_bq2591x_set_chargerid_switch_val(int value)
{
	return;
}

int oplus_bq2591x_get_chargerid_switch_val(void)
{
	return 0;
}

int oplus_bq2591x_get_chg_current_step(void)
{
	return BQ2591X_ICHG_LSB;
}

bool oplus_bq2591x_need_to_check_ibatt(void)
{
	return false;
}

int oplus_bq2591x_get_dyna_aicl_result(void)
{
	u8 val;
	int ret;
	int ilim = 0;

	ret = bq2591x_read_byte(g_bq, &val, BQ2591X_REG_03);
	if (!ret) {
		ilim = val & BQ2591X_IINLIM_MASK;
		ilim = (ilim >> BQ2591X_IINLIM_SHIFT) * BQ2591X_IINLIM_LSB;
		ilim = ilim + BQ2591X_IINLIM_BASE;
	}

	return ilim;
}

bool oplus_bq2591x_get_shortc_hw_gpio_status(void)
{
	return false;
}

int oplus_bq2591x_get_charger_subtype(void)
{
	return CHARGER_SUBTYPE_DEFAULT;
}

int oplus_bq2591x_enable_shipmode(bool en)
{
	int ret = 0;
	return ret;
}

struct oplus_chg_operations  oplus_chg_bq2591x_ops = {
	.dump_registers = oplus_bq2591x_dump_registers,
	.kick_wdt = oplus_bq2591x_kick_wdt,
	.hardware_init = oplus_bq2591x_hardware_init,
	.charging_current_write_fast = oplus_bq2591x_set_ichg,
	.set_aicl_point = oplus_bq2591x_set_mivr,
	.input_current_write = oplus_bq2591x_set_aicr,
	.float_voltage_write = oplus_bq2591x_set_cv,
	.term_current_set = oplus_bq2591x_set_ieoc,
	.charging_enable = oplus_bq2591x_charging_enable,
	.charging_disable = oplus_bq2591x_charging_disable,
	.get_charging_enable = oplus_bq2591x_is_charging_enabled,
	.charger_suspend = oplus_bq2591x_charger_suspend,
	.charger_unsuspend = oplus_bq2591x_charger_unsuspend,
	.set_rechg_vol = oplus_bq2591x_set_rechg_vol,
	.reset_charger = oplus_bq2591x_reset_charger,
	.read_full = oplus_bq2591x_is_charging_done,
	.otg_enable = oplus_bq2591x_enable_otg,
	.otg_disable = oplus_bq2591x_disable_otg,
	.set_charging_term_disable = oplus_bq2591x_disable_te,
	.check_charger_resume = oplus_bq2591x_check_charger_resume,

	.get_charger_type = oplus_bq2591x_get_charger_type,
	.get_charger_volt = battery_get_vbus,
//	int (*get_charger_current)(void);
	.get_chargerid_volt = NULL,
    .set_chargerid_switch_val = oplus_bq2591x_set_chargerid_switch_val,
    .get_chargerid_switch_val = oplus_bq2591x_get_chargerid_switch_val,
	.check_chrdet_status = (bool (*) (void)) pmic_chrdet_status,

	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = oplus_get_rtc_ui_soc,
	.set_rtc_soc = oplus_set_rtc_ui_soc,
	.set_power_off = mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
    .get_chg_current_step = oplus_bq2591x_get_chg_current_step,
    .need_to_check_ibatt = oplus_bq2591x_need_to_check_ibatt,
    .get_dyna_aicl_result = oplus_bq2591x_get_dyna_aicl_result,
    .get_shortc_hw_gpio_status = oplus_bq2591x_get_shortc_hw_gpio_status,
//	void (*check_is_iindpm_mode) (void);
    .oplus_chg_get_pd_type = NULL,
    .oplus_chg_pd_setup = NULL,
	.get_charger_subtype = oplus_bq2591x_get_charger_subtype,
	.set_qc_config = NULL,
	.enable_qc_detect = NULL,
	.oplus_chg_set_high_vbus = NULL,
	.enable_shipmode = oplus_bq2591x_enable_shipmode,
};

static int bq2591x_charger_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct bq2591x *bq = NULL;
	int ret;

	pr_info("bq2591x probe enter\n");

	bq = devm_kzalloc(&client->dev, sizeof(struct bq2591x), GFP_KERNEL);
	if (!bq) {
		pr_err("out of memory\n");
		return -ENOMEM;
	}

	bq->dev = &client->dev;
	bq->client = client;
	g_bq = bq;
	bq->hz_mode = 0;
	i2c_set_clientdata(client, bq);

	mutex_init(&bq->i2c_rw_lock);

	ret = bq2591x_detect_device(bq);
	if (!ret && bq->part_no == BQ25910) {
		pr_info("charger device bq2591x detected, revision:%d\n",
							bq->revision);
	} else {
		pr_info("no bq2591x charger device found:%d\n", ret);
		ret= -ENODEV;
		goto err_detect_device;
	}

	if (client->dev.of_node)
		bq2591x_parse_dt(&client->dev, bq);

	bq->chg_props = bq2591x_chg_props;
		/* Register charger device */
	bq->chg_dev = charger_device_register(
		"bq25910", &client->dev, bq, &bq2591x_chg_ops,
		&bq->chg_props);
	if (IS_ERR_OR_NULL(bq->chg_dev)) {
		ret = PTR_ERR(bq->chg_dev);
		goto err_charger_device;
	}

	ret = bq2591x_init_device(bq);
	if (ret) {
		pr_err("device init failure: %d\n", ret);
		goto err_init_device;
	}
	
	INIT_DELAYED_WORK(&bq->monitor_work, bq2591x_monitor_workfunc);

	if (strcmp(g_bq->chg_dev_name, "secondary_chg") != 0) {
		if (client->irq) {
			ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					bq2591x_charger_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"bq2591x charger irq", bq);
			if (ret < 0) {
				pr_err("request irq for irq=%d failed,ret =%d\n",
					client->irq, ret);
				goto err_irq;
			}
		}
	}

	ret = sysfs_create_group(&bq->dev->kobj, &bq2591x_attr_group);
	if (ret){
		dev_err(bq->dev, "failed to register sysfs. err: %d\n", ret);
		goto err_sysfs_create;
	}
	
	set_charger_ic(BQ2591X);
	pr_info("BQ2591X charger driver probe successfully\n");

	return 0;
	
err_sysfs_create:	
err_irq:
	charger_device_unregister(bq->chg_dev);
err_charger_device:
err_init_device:
err_detect_device:
	mutex_destroy(&bq->i2c_rw_lock);
	devm_kfree(bq->dev, bq);
	return ret;
}

static int bq2591x_charger_remove(struct i2c_client *client)
{
	struct bq2591x *bq = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&bq->monitor_work);

	mutex_destroy(&bq->i2c_rw_lock);

	sysfs_remove_group(&bq->dev->kobj, &bq2591x_attr_group);

	return 0;
}


static void bq2591x_charger_shutdown(struct i2c_client *client)
{
	pr_info("shutdown\n");

}

static struct of_device_id bq2591x_charger_match_table[] = {
	{.compatible = "ti,bq2591x"},
	{},
};


static const struct i2c_device_id bq2591x_charger_id[] = {
	{ "bq2591x", BQ25910 },
	{},
};

MODULE_DEVICE_TABLE(i2c, bq2591x_charger_id);

static struct i2c_driver bq2591x_charger_driver = {
	.driver		= {
		.name	= "bq2591x",
		.of_match_table = bq2591x_charger_match_table,
	},
	.id_table	= bq2591x_charger_id,

	.probe		= bq2591x_charger_probe,
	.remove		= bq2591x_charger_remove,
	.shutdown   = bq2591x_charger_shutdown,
};

module_i2c_driver(bq2591x_charger_driver);

MODULE_DESCRIPTION("TI BQ2591x Charger Driver");
MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Texas Instruments");
