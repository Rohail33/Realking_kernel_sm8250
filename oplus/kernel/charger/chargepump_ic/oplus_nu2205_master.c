// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2022 Oplus. All rights reserved.
 */


#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
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
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>

#include <trace/events/sched.h>
#include <linux/ktime.h>
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "oplus_nu2205.h"
#include "../oplus_pps.h"

static struct chip_nu2205 *chip_nu2205_master = NULL;

static struct mutex i2c_rw_lock;

bool nu2205_master_get_enable(void);

/************************************************************************/
static int __nu2205_read_byte(u8 reg, u8 *data)
{
	int ret;
	struct chip_nu2205 *chip = chip_nu2205_master;

	ret = i2c_smbus_read_byte_data(chip->master_client, reg);

	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8)ret;

	return 0;
}

static int __nu2205_write_byte(int reg, u8 val)
{
	int ret;
	struct chip_nu2205 *chip = chip_nu2205_master;

	ret = i2c_smbus_write_byte_data(chip->master_client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}

	return 0;
}

static int nu2205_read_byte(u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2205_read_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int nu2205_write_byte(u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2205_write_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int nu2205_read_word(u8 reg, u8 *data_block)
{
	struct chip_nu2205 *chip = chip_nu2205_master;
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_i2c_block_data(chip->master_client, reg, 2,
					    data_block);
	if (ret < 0) {
		chg_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static int nu2205_master_i2c_masked_write(u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __nu2205_read_byte(reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __nu2205_write_byte(reg, tmp);
	if (ret)
		pr_err("Faileds: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

int nu2205_master_get_tdie(void)
{
	u8 data_block[2] = { 0 };
	int tdie = 0;

	nu2205_read_word(0x29, data_block);
	tdie = (((data_block[0] & 0x01) << 8) | (data_block[1] & 0xff)) * 1 / 2;
	pps_err("0x37[0x%x] 0x38[0x%x] tdie[%d]\n", data_block[0],
		data_block[1], tdie);

	return tdie;
}

int nu2205_master_get_ucp_flag(void)
{
	int ret = 0;
	u8 temp;
	int ucp_fail = 0;

	ret = nu2205_read_byte(0x0A, &temp);
	if (ret < 0) {
		pr_err("NU2205_REG_19\n");
		return -1;
	}

	ucp_fail = (temp & 0x70) >> 6;
	pps_err("0x19[0x%x] ucp_fail = %d\n", temp, ucp_fail);
	if (ucp_fail == 1) {
		nu2205_master_dump_registers();
		nu2205_slave_dump_registers();
	}

	return ucp_fail;
}

int nu2205_master_get_vout(void)
{
	u8 data_block[2] = { 0 };
	int vout = 0;

	nu2205_read_word(0x1f, data_block);
	vout = (((data_block[0] & 0x3f) << 8) | (data_block[1] & 0xff)) * 1;
	pps_err("0x2D[0x%x] 0x2E[0x%x] vout[%d]\n", data_block[0],
		data_block[1], vout);

	return vout;
}

int nu2205_master_get_vac(void)
{
	u8 data_block[2] = { 0 };
	int vac;

	nu2205_read_word(0x1b, data_block);
	vac = (((data_block[0] & 0x7f) << 8) | (data_block[1] & 0xff)) * 1;
	pps_err("0x29[0x%x] 0x2A[0x%x] cp_vac1[%d]\n", data_block[0],
		data_block[1], vac);

	return vac;
}

int nu2205_master_get_vbus(void)
{
	u8 data_block[2] = { 0 };
	int cp_vbus;

	nu2205_read_word(0x19, data_block);
	cp_vbus = (((data_block[0] & 0x7f) << 8) | (data_block[1] & 0xff)) * 1;
	pps_err("0x27[0x%x] 0x28[0x%x] cp_vbus[%d]\n", data_block[0],
		data_block[1], cp_vbus);

	return cp_vbus;
}

int nu2205_master_get_ibus(void)
{
	u8 data_block[2] = { 0 };
	int cp_ibus;

	nu2205_master_get_tdie();
	nu2205_read_word(0x17, data_block);
	cp_ibus = (((data_block[0] & 0x1f) << 8) | (data_block[1] & 0xff)) * 1;
	pps_err("0x25[0x%x] 0x26[0x%x] cp_ibus[%d]\n", data_block[0],
		data_block[1], cp_ibus);

	return cp_ibus;
}

int nu2205_master_cp_enable(int enable)
{
	struct chip_nu2205 *nu2205 = chip_nu2205_master;
	int ret = 0;
	if (!nu2205) {
		pps_err("Failed\n");
		return -1;
	}
	pps_err(" enable = %d\n", enable);
	if (enable && (nu2205_master_get_enable() == false)) {
		ret = nu2205_master_i2c_masked_write(0x0E, 0x80, 0x80);
	} else if (!enable && (nu2205_master_get_enable() == true)) {
		ret = nu2205_master_i2c_masked_write(0x0E, 0x80, 0);
	}
	return ret;
}

bool nu2205_master_get_enable(void)
{
	int ret = 0;
	u8 temp;
	bool cp_enable = false;

	ret = nu2205_read_byte(0x0E, &temp);
	if (ret < 0) {
		pr_err("NU2205_REG_0F\n");
		return -1;
	}

	cp_enable = (temp & 0x80) >> 7;
	pps_err("temp = 0x%x,cp_enable = %d\n", temp, cp_enable);

	return cp_enable;
}

void nu2205_master_cfg_sc(void)
{
	nu2205_write_byte(NU2205_REG_0E,
			  0x06); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	nu2205_write_byte(NU2205_REG_00, 0x32); /*0X00	EN_BATOVP=9.540V*/
	nu2205_write_byte(NU2205_REG_01, 0x9C); /*0X01 DIS_BATOVP_ALM*/
	nu2205_write_byte(NU2205_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	nu2205_write_byte(NU2205_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	nu2205_write_byte(NU2205_REG_04, 0xA8); /*0X06 BUS_OVP=10.5V*/
	nu2205_write_byte(NU2205_REG_05, 0x06); /*0X07 DIS_BUSOVP_ALM*/
	nu2205_write_byte(
		NU2205_REG_06,
		0x06); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	nu2205_write_byte(
		NU2205_REG_07,
		0x3F); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	nu2205_write_byte(NU2205_REG_08, 0xA0); /*0X06 BUS_OVP=10.5V*/
	nu2205_write_byte(NU2205_REG_09, 0x08); /*0X07 DIS_BUSOVP_ALM*/
	nu2205_write_byte(
		NU2205_REG_0A,
		0x90); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	nu2205_write_byte(
		NU2205_REG_0B,
		0x00); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	nu2205_write_byte(NU2205_REG_0D,
			  0x32); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	nu2205_write_byte(NU2205_REG_15, 0x80); /*0X10 disalbe watchdog*/
	nu2205_write_byte(NU2205_REG_16,
			  0x0E); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	nu2205_write_byte(NU2205_REG_2E, 0xA0); /*0X10 disalbe watchdog*/
	nu2205_write_byte(NU2205_REG_33,
			  0x0A); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	pps_err(" end!\n");
}

void nu2205_master_cfg_bypass(void)
{
	nu2205_write_byte(NU2205_REG_0E,
			  0x26); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	nu2205_write_byte(NU2205_REG_00, 0x32); /*0X00	EN_BATOVP=9.540V*/
	nu2205_write_byte(NU2205_REG_01, 0x9C); /*0X01 DIS_BATOVP_ALM*/
	nu2205_write_byte(NU2205_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	nu2205_write_byte(NU2205_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	nu2205_write_byte(NU2205_REG_04, 0xA8); /*0X06 BUS_OVP=10.5V*/
	nu2205_write_byte(NU2205_REG_05, 0x07); /*0X07 DIS_BUSOVP_ALM*/
	nu2205_write_byte(
		NU2205_REG_06,
		0x07); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	nu2205_write_byte(
		NU2205_REG_07,
		0x3B); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	nu2205_write_byte(NU2205_REG_08, 0xA0); /*0X06 BUS_OVP=10.5V*/
	nu2205_write_byte(NU2205_REG_09, 0x0F); /*0X07 DIS_BUSOVP_ALM*/
	nu2205_write_byte(
		NU2205_REG_0A,
		0x90); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	nu2205_write_byte(
		NU2205_REG_0B,
		0x00); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	nu2205_write_byte(NU2205_REG_0D,
			  0x36); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	nu2205_write_byte(NU2205_REG_15, 0x80); /*0X10 disalbe watchdog*/
	nu2205_write_byte(NU2205_REG_16,
			  0x0E); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	nu2205_write_byte(NU2205_REG_2E, 0xA0); /*0X10 disalbe watchdog*/
	nu2205_write_byte(NU2205_REG_33,
			  0x0A); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	pps_err(" end!\n");
}

void nu2205_master_hardware_init(void)
{
	nu2205_write_byte(NU2205_REG_0E,
			  0x26); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	nu2205_write_byte(NU2205_REG_00, 0x32); /*0X00	EN_BATOVP=9.540V*/
	nu2205_write_byte(NU2205_REG_01, 0x9C); /*0X01 DIS_BATOVP_ALM*/
	nu2205_write_byte(NU2205_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	nu2205_write_byte(NU2205_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	nu2205_write_byte(NU2205_REG_04, 0xA8); /*0X06 BUS_OVP=10.5V*/
	nu2205_write_byte(NU2205_REG_05, 0x07); /*0X07 DIS_BUSOVP_ALM*/
	nu2205_write_byte(
		NU2205_REG_06,
		0x07); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	nu2205_write_byte(
		NU2205_REG_07,
		0x3B); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	nu2205_write_byte(NU2205_REG_08, 0xA0); /*0X06 BUS_OVP=10.5V*/
	nu2205_write_byte(NU2205_REG_09, 0x09); /*0X07 DIS_BUSOVP_ALM*/
	nu2205_write_byte(
		NU2205_REG_0A,
		0x90); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	nu2205_write_byte(
		NU2205_REG_0B,
		0x00); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	nu2205_write_byte(NU2205_REG_0D,
			  0x36); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	nu2205_write_byte(NU2205_REG_15, 0x00); /*0X10 disalbe watchdog*/
	nu2205_write_byte(NU2205_REG_16,
			  0x0E); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	nu2205_write_byte(NU2205_REG_2E, 0xA0); /*0X10 disalbe watchdog*/
	nu2205_write_byte(NU2205_REG_33,
			  0x0A); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	pps_err(" end!\n");
}

int nu2205_master_dump_registers(void)
{
	int ret = 0;
	u8 addr;
	u8 val_buf[11] = { 0x0 };
	return ret;

	for (addr = 0x0; addr <= NU2205_REG_1C; addr++) {
		ret = nu2205_read_byte(addr, &val_buf[addr - NU2205_REG_13]);
		if (ret < 0) {
			pps_err("nu2205_master_dump_registers Couldn't read "
				"0x%02x ret = %d\n",
				addr, ret);
			return -1;
		}
	}
	ret = nu2205_read_byte(NU2205_REG_42, &val_buf[10]);
	if (ret < 0) {
		pps_err("NU2205_REG_19 fail\n");
		return -1;
	}

	pps_err("nu2205_master_dump_registers:[13~17][0x%x, 0x%x, 0x%x, 0x%x, "
		"0x%x]\n",
		val_buf[0], val_buf[1], val_buf[2], val_buf[3], val_buf[4]);
	pps_err("nu2205_master_dump_registers:[18~1c][0x%x, 0x%x, 0x%x, 0x%x, "
		"0x%x][0x42= 0x%x]\n",
		val_buf[5], val_buf[6], val_buf[7], val_buf[8], val_buf[9],
		val_buf[10]);
	return ret;
}

static ssize_t nu2205_show_registers(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "nu2205");
	for (addr = NU2205_REG_00; addr <= NU2205_REG_43; addr++) {
		ret = nu2205_read_byte(addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t nu2205_store_register(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= NU2205_REG_43)
		nu2205_write_byte((unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0660, nu2205_show_registers,
		   nu2205_store_register);

static void nu2205_create_device_node(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_registers);
	if (err)
		pps_err("nu2205 create device err!\n");
}

static int nu2205_parse_dt(struct chip_nu2205 *chip)
{
	if (!chip) {
		pr_debug("chip null\n");
		return -1;
	}

	return 0;
}

irqreturn_t nu2205_ucp_interrupt_handler(struct chip_nu2205 *chip)
{
	int ucp_value;

	ucp_value = nu2205_master_get_ucp_flag();

	if (ucp_value) {
		oplus_pps_stop_disconnect();
	}
	chg_err(",ucp_value = %d", ucp_value);

	return IRQ_HANDLED;
}

static int nu2205_irq_gpio_init(struct chip_nu2205 *chip)
{
	int rc;
	struct device_node *node = chip->master_dev->of_node;

	if (!node) {
		chg_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->ucp_gpio = of_get_named_gpio(node, "qcom,ucp_gpio", 0);
	if (chip->ucp_gpio < 0) {
		chg_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->ucp_gpio)) {
			rc = gpio_request(chip->ucp_gpio, "ucp_gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
					chip->ucp_gpio);
			}
		}
		chg_err("chip->ucp_gpio =%d\n", chip->ucp_gpio);
	}

	chip->ucp_irq = gpio_to_irq(chip->ucp_gpio);
	chg_err("irq chip->ucp_irq =%d\n", chip->ucp_irq);

	/* set voocphy pinctrl*/
	chip->ucp_pinctrl = devm_pinctrl_get(chip->master_dev);
	if (IS_ERR_OR_NULL(chip->ucp_pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->ucp_int_active =
		pinctrl_lookup_state(chip->ucp_pinctrl, "ucp_int_active");
	if (IS_ERR_OR_NULL(chip->ucp_int_active)) {
		chg_err(": %d Failed to get the state pinctrl handle\n",
			__LINE__);
		return -EINVAL;
	}

	chip->ucp_int_sleep =
		pinctrl_lookup_state(chip->ucp_pinctrl, "ucp_int_sleep");
	if (IS_ERR_OR_NULL(chip->ucp_int_sleep)) {
		chg_err(": %d Failed to get the state pinctrl handle\n",
			__LINE__);
		return -EINVAL;
	}

	gpio_direction_input(chip->ucp_gpio);
	pinctrl_select_state(chip->ucp_pinctrl,
			     chip->ucp_int_active); /* no_PULL */

	rc = gpio_get_value(chip->ucp_gpio);
	chg_err("irq chip->ucp_gpio input =%d irq_gpio_stat = %d\n",
		chip->ucp_gpio, rc);

	return 0;
}

static irqreturn_t nu2205_ucp_interrupt(int irq, void *dev_id)
{
	struct chip_nu2205 *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	return nu2205_ucp_interrupt_handler(chip);
}

static int nu2205_irq_register(struct chip_nu2205 *chip)
{
	int ret = 0;

	nu2205_irq_gpio_init(chip);
	chg_err("nu2205 chip->ucp_irq = %d\n", chip->ucp_irq);
	if (chip->ucp_irq) {
		ret = request_threaded_irq(chip->ucp_irq, NULL,
					   nu2205_ucp_interrupt,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   "nu2205_ucp_irq", chip);
		if (ret < 0) {
			chg_err("request irq for ucp_irq=%d failed, ret =%d\n",
				chip->ucp_irq, ret);
			return ret;
		}
		enable_irq_wake(chip->ucp_irq);
	}
	chg_err("request irq ok\n");

	return ret;
}

static int nu2205_master_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct chip_nu2205 *chip;

	pps_err(" enter!\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->master_client = client;
	chip->master_dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);
	chip_nu2205_master = chip;

	nu2205_create_device_node(&(client->dev));

	nu2205_parse_dt(chip);

	nu2205_master_hardware_init();
	nu2205_irq_register(chip);

	nu2205_master_get_enable();

	pps_err("nu2205_parse_dt successfully!\n");

	return 0;
}

static void nu2205_master_shutdown(struct i2c_client *client)
{
	return;
}

static struct of_device_id nu2205_master_match_table[] = {
	{
		.compatible = "oplus,nu2205-master",
	},
	{},
};

static const struct i2c_device_id nu2205_master_charger_id[] = {
	{ "nu2205-master", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, nu2205_master_charger_id);

static struct i2c_driver nu2205_master_driver = {
	.driver =
		{
			.name = "nu2205-master",
			.owner = THIS_MODULE,
			.of_match_table = nu2205_master_match_table,
		},
	.id_table = nu2205_master_charger_id,

	.probe = nu2205_master_probe,
	.shutdown = nu2205_master_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init nu2205_master_subsys_init(void)
{
	int ret = 0;
	pps_err("init start\n");

	if (i2c_add_driver(&nu2205_master_driver) != 0) {
		pps_err(" failed to register nu2205 i2c driver.\n");
	} else {
		pps_err(" Success to register nu2205 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(nu2205_master_subsys_init);
#else
int nu2205_master_subsys_init(void)
{
	int ret = 0;
	pps_err("2 init start\n");

	if (i2c_add_driver(&nu2205_master_driver) != 0) {
		pps_err(" failed to register nu2205 i2c driver.\n");
	} else {
		pps_err(" Success to register nu2205 i2c driver.\n");
	}

	return ret;
}

void nu2205_master_subsys_exit(void)
{
	i2c_del_driver(&nu2205_master_driver);
}
#endif

MODULE_DESCRIPTION("SC NU2205 Master Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
