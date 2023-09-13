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
#include "oplus_bq25980.h"
#include "../oplus_pps.h"

static struct chip_bq25980 *chip_bq25980_master = NULL;

static struct mutex i2c_rw_lock;

bool bq25980_master_get_enable(void);

/************************************************************************/
static int __bq25980_read_byte(u8 reg, u8 *data)
{
	int ret;
	struct chip_bq25980 *chip = chip_bq25980_master;

	ret = i2c_smbus_read_byte_data(chip->master_client, reg);

	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8)ret;

	return 0;
}

static int __bq25980_write_byte(int reg, u8 val)
{
	int ret;
	struct chip_bq25980 *chip = chip_bq25980_master;

	ret = i2c_smbus_write_byte_data(chip->master_client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}

	return 0;
}

static int bq25980_read_byte(u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __bq25980_read_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int bq25980_write_byte(u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __bq25980_write_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int bq25980_read_word(u8 reg, u8 *data_block)
{
	struct chip_bq25980 *chip = chip_bq25980_master;
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

static int bq25980_master_i2c_masked_write(u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __bq25980_read_byte(reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __bq25980_write_byte(reg, tmp);
	if (ret)
		pr_err("Faileds: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

int bq25980_master_get_tdie(void)
{
	u8 data_block[2] = { 0 };
	int tdie = 0;

	bq25980_read_word(BQ25980_REG_37, data_block);
	tdie = BQ25980_TDIE_OFFSET +
	       (((data_block[0] & BQ25980_TDIE_POL_H_MASK) << 8) |
		(data_block[1] & BQ25980_TDIE_POL_L_MASK)) *
		       BQ25980_TDIE_ADC_LSB;
	pps_err("0x37[0x%x] 0x38[0x%x] tdie[%d] = %d\n", data_block[0],
		data_block[1], tdie);

	return tdie;
}

int bq25980_master_get_ucp_flag(void)
{
	int ret = 0;
	u8 temp;
	int ucp_fail = 0;

	ret = bq25980_read_byte(BQ25980_REG_19, &temp);
	if (ret < 0) {
		pr_err("BQ25980_REG_19\n");
		return -1;
	}

	ucp_fail = (temp & BQ25980_BUS_UCP_FALL_FLAG_MASK) >>
		   BQ25980_BUS_UCP_FALL_FLAG_SHIFT;
	pps_err("0x19[0x%x] ucp_fail = %d\n", temp, ucp_fail);

	return ucp_fail;
}

int bq25980_master_get_vout(void)
{
	u8 data_block[2] = { 0 };
	int vout = 0;

	bq25980_read_word(BQ25980_REG_2D, data_block);
	vout = BQ25980_VOUT_OFFSET +
	       (((data_block[0] & BQ25980_VOUT_POL_H_MASK) << 8) |
		(data_block[1] & BQ25980_VOUT_POL_L_MASK)) *
		       BQ25980_VOUT_ADC_LSB;

	return vout;
}

int bq25980_master_get_vac(void)
{
	u8 data_block[2] = { 0 };
	int vac;

	bq25980_read_word(BQ25980_REG_29, data_block);
	vac = BQ25980_VAC1_OFFSET +
	      (((data_block[0] & BQ25980_VAC1_POL_H_MASK) << 8) |
	       (data_block[1] & BQ25980_VAC1_POL_L_MASK)) *
		      BQ25980_VAC1_ADC_LSB;

	return vac;
}

int bq25980_master_get_vbus(void)
{
	u8 data_block[2] = { 0 };
	int cp_vbus;

	bq25980_read_word(BQ25980_REG_27, data_block);
	cp_vbus = BQ25980_VBUS_OFFSET +
		  (((data_block[0] & BQ25980_VBUS_POL_H_MASK) << 8) |
		   (data_block[1] & BQ25980_VBUS_POL_L_MASK)) *
			  BQ25980_VBUS_ADC_LSB;
	pps_err("0x27[0x%x] 0x28[0x%x] cp_vbus[%d]\n", data_block[0],
		data_block[1], cp_vbus);

	return cp_vbus;
}

int bq25980_master_get_ibus(void)
{
	u8 data_block[2] = { 0 };
	int cp_ibus;

	bq25980_master_get_tdie();

	bq25980_read_word(BQ25980_REG_25, data_block);
	cp_ibus = (((data_block[0] & BQ25980_IBUS_POL_H_MASK) << 8) |
		   (data_block[1] & BQ25980_IBUS_POL_L_MASK)) *
		  BQ25980_IBUS_ADC_LSB;
	pps_err("0x25[0x%x] 0x26[0x%x] cp_ibus[%d]\n", data_block[0],
		data_block[1], cp_ibus);

	return cp_ibus;
}

int bq25980_master_cp_enable(int enable)
{
	struct chip_bq25980 *bq25980 = chip_bq25980_master;
	int ret = 0;
	if (!bq25980) {
		pps_err("Failed\n");
		return -1;
	}
	pps_err(" enable = %d\n", enable);
	if (enable && (bq25980_master_get_enable() == false)) {
		ret = bq25980_master_i2c_masked_write(BQ25980_CHRGR_CTRL_2,
						      BQ25980_ENABLE_MASK,
						      BQ25980_CHG_EN);
	} else if (!enable && (bq25980_master_get_enable() == true)) {
		ret = bq25980_master_i2c_masked_write(BQ25980_CHRGR_CTRL_2,
						      BQ25980_ENABLE_MASK,
						      BQ25980_CHG_DISABLE);
	}
	return ret;
}

bool bq25980_master_get_enable(void)
{
	int ret = 0;
	u8 temp;
	bool cp_enable = false;

	ret = bq25980_read_byte(BQ25980_CHRGR_CTRL_2, &temp);
	if (ret < 0) {
		pr_err("SC8547_REG_07\n");
		return -1;
	}

	cp_enable = (temp & BQ25980_ENABLE_MASK) >> BQ25980_ENABLE_SHIFT;

	return cp_enable;
}

void bq25980_master_pmid2vout_enable(bool enable)
{
	/*no limit*/
}

void bq25980_master_cfg_sc(void)
{
	bq25980_write_byte(
		BQ25980_CHRGR_CTRL_2,
		0x02); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	bq25980_write_byte(BQ25980_REG_0, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	bq25980_write_byte(BQ25980_BATOVP_ALM, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	bq25980_write_byte(BQ25980_BATOCP, 0xD1); /*0X02 DIS_BATOCP*/
	bq25980_write_byte(BQ25980_BATOCP_ALM, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_CHRGR_CFG_1, 0xA8); /*0X04 DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_CHRGR_CTRL_1, 0x4A); /*0X05 DIS_BATOCP_ALM*/

	bq25980_write_byte(BQ25980_BUSOVP, 0x4B); /*0X06 BUS_OVP=23V*/
	bq25980_write_byte(BQ25980_BUSOVP_ALM, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	bq25980_write_byte(BQ25980_BUSOCP, 0x13); /*0X08 DIS_BUSOVP_ALM*/
	bq25980_write_byte(BQ25980_REG_09, 0x8C); /*0X09 DIS_BUSOVP_ALM*/
	bq25980_write_byte(
		BQ25980_TEMP_CONTROL,
		0x6C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	bq25980_write_byte(BQ25980_TDIE_ALM, 0xC8); /*0X0B DIS_BATOVP_ALM*/
	bq25980_write_byte(BQ25980_TSBUS_FLT, 0x15); /*0X0C DIS_BATOCP*/
	bq25980_write_byte(BQ25980_TSBAT_FLG, 0x15); /*0X0D DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_VAC_CONTROL,
			   0xD8); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	bq25980_write_byte(BQ25980_CHRGR_CTRL_3,
			   0x70); /*0X10 disalbe watchdog*/
	bq25980_write_byte(BQ25980_CHRGR_CTRL_4, 0x6D); /*0X11 DIS_BATOVP_ALM*/
	bq25980_write_byte(BQ25980_CHRGR_CTRL_5, 0x60); /*0X12 DIS_BATOCP*/
	bq25980_write_byte(BQ25980_ADC_CONTROL1, 0x90); /*0X23 DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_ADC_CONTROL2, 0x0E); /*0X24 DIS_BATOCP_ALM*/
}

void bq25980_master_cfg_bypass(void)
{
	bq25980_write_byte(
		BQ25980_CHRGR_CTRL_2,
		0x08); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	bq25980_write_byte(BQ25980_REG_0, 0x7F); /*0X00  EN_BATOVP=9.540V*/
	bq25980_write_byte(BQ25980_BATOVP_ALM, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	bq25980_write_byte(BQ25980_BATOCP, 0xD1); /*0X02 DIS_BATOCP*/
	bq25980_write_byte(BQ25980_BATOCP_ALM, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_CHRGR_CTRL_1, 0x0); /*0X05 DIS_BATOCP_ALM*/

	bq25980_write_byte(BQ25980_BUSOVP, 0x5A); /*0X06 BUS_OVP=10.5V*/
	bq25980_write_byte(BQ25980_BUSOVP_ALM, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	bq25980_write_byte(BQ25980_BUSOCP, 0x1C); /*0X08 DIS_BUSOVP_ALM*/
	bq25980_write_byte(
		BQ25980_TEMP_CONTROL,
		0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	bq25980_write_byte(BQ25980_VAC_CONTROL,
			   0x58); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	bq25980_write_byte(BQ25980_CHRGR_CTRL_3,
			   0x84); /*0X10 disalbe watchdog*/
	bq25980_write_byte(BQ25980_CHRGR_CTRL_4, 0x6C); /*0X11 DIS_BATOVP_ALM*/
	bq25980_write_byte(BQ25980_CHRGR_CTRL_5, 0x60); /*0X12 DIS_BATOCP*/
	bq25980_write_byte(BQ25980_ADC_CONTROL1, 0x80); /*0X23 DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_ADC_CONTROL2, 0x0E); /*0X24 DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_REG_42, 0xFC); /*0X42 DIS_BATOCP_ALM*/
}

void bq25980_master_hardware_init(void)
{
	bq25980_write_byte(
		BQ25980_CHRGR_CTRL_2,
		0x08); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	bq25980_write_byte(BQ25980_REG_0, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	bq25980_write_byte(BQ25980_BATOVP_ALM, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	bq25980_write_byte(BQ25980_BATOCP, 0xD1); /*0X02 DIS_BATOCP*/
	bq25980_write_byte(BQ25980_BATOCP_ALM, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	bq25980_write_byte(BQ25980_BUSOVP, 0x46); /*0X06 BUS_OVP=10.5V*/
	bq25980_write_byte(BQ25980_BUSOVP_ALM, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	bq25980_write_byte(
		BQ25980_TEMP_CONTROL,
		0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	bq25980_write_byte(BQ25980_VAC_CONTROL,
			   0x58); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	bq25980_write_byte(BQ25980_CHRGR_CTRL_3,
			   0x84); /*0X10 disalbe watchdog*/

	pps_err(" end!\n");
}

void bq25980_master_reset(void)
{
	bq25980_write_byte(BQ25980_CHRGR_CTRL_2, 0x80); /*0x0F reset cp*/
	pps_err(" end!\n");
}

int bq25980_master_dump_registers(void)
{
	int ret = 0;

	u8 addr;
	u8 val_buf[11] = { 0x0 };

	for (addr = BQ25980_REG_13; addr <= BQ25980_REG_1C; addr++) {
		ret = bq25980_read_byte(addr, &val_buf[addr - BQ25980_REG_13]);
		if (ret < 0) {
			pps_err("bq25980_master_dump_registers Couldn't read "
				"0x%02x ret = %d\n",
				addr, ret);
			return -1;
		}
	}
	ret = bq25980_read_byte(BQ25980_REG_42, &val_buf[10]);
	if (ret < 0) {
		pps_err("BQ25980_REG_42 fail\n");
		return -1;
	}

	pps_err("bq25980_master_dump_registers:[13~17][0x%x, 0x%x, 0x%x, 0x%x, "
		"0x%x]\n",
		val_buf[0], val_buf[1], val_buf[2], val_buf[3], val_buf[4]);
	pps_err("bq25980_master_dump_registers:[18~1c][0x%x, 0x%x, 0x%x, 0x%x, "
		"0x%x][0x42= 0x%x]\n",
		val_buf[5], val_buf[6], val_buf[7], val_buf[8], val_buf[9],
		val_buf[10]);
	return ret;
}

static ssize_t bq25980_show_registers(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "bq25980");
	for (addr = BQ25980_REG_0; addr <= BQ25980_REG_42; addr++) {
		ret = bq25980_read_byte(addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t bq25980_store_register(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= BQ25980_REG_42)
		bq25980_write_byte((unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0660, bq25980_show_registers,
		   bq25980_store_register);

static void bq25980_create_device_node(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_registers);
	if (err)
		pps_err("bq25980 create device err!\n");
}

static int bq25980_master_parse_dt(struct chip_bq25980 *chip)
{
	if (!chip) {
		pr_debug("chip null\n");
		return -1;
	}

	return 0;
}

irqreturn_t bq25980_ucp_interrupt_handler(struct chip_bq25980 *chip)
{
	int ucp_value;

	ucp_value = bq25980_master_get_ucp_flag();

	if (ucp_value) {
		oplus_pps_stop_disconnect();
	}
	chg_err(",ucp_value = %d", ucp_value);

	return IRQ_HANDLED;
}

static int bq25980_irq_gpio_init(struct chip_bq25980 *chip)
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

static irqreturn_t bq25980_ucp_interrupt(int irq, void *dev_id)
{
	struct chip_bq25980 *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	return bq25980_ucp_interrupt_handler(chip);
}

static int bq25980_irq_register(struct chip_bq25980 *chip)
{
	int ret = 0;

	bq25980_irq_gpio_init(chip);
	chg_err("bq25980 chip->ucp_irq = %d\n", chip->ucp_irq);
	if (chip->ucp_irq) {
		ret = request_threaded_irq(chip->ucp_irq, NULL,
					   bq25980_ucp_interrupt,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   "bq25980_ucp_irq", chip);
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

static int bq25980_master_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct chip_bq25980 *chip;

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
	chip_bq25980_master = chip;

	bq25980_create_device_node(&(client->dev));

	bq25980_master_parse_dt(chip);

	bq25980_master_hardware_init();

	bq25980_irq_register(chip);

	bq25980_master_get_enable();

	pps_err("bq25980_parse_dt successfully!\n");

	return 0;
}

static void bq25980_master_shutdown(struct i2c_client *client)
{
	return;
}

static struct of_device_id bq25980_master_match_table[] = {
	{
		.compatible = "oplus,bq25980-master",
	},
	{},
};

static const struct i2c_device_id bq25980_master_charger_id[] = {
	{ "bq25980-master", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq25980_master_charger_id);

static struct i2c_driver bq25980_master_driver = {
	.driver =
		{
			.name = "bq25980-master",
			.owner = THIS_MODULE,
			.of_match_table = bq25980_master_match_table,
		},
	.id_table = bq25980_master_charger_id,

	.probe = bq25980_master_probe,
	.shutdown = bq25980_master_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
int __init bq25980_master_subsys_init(void)
{
	int ret = 0;
	pps_err("init start\n");

	if (i2c_add_driver(&bq25980_master_driver) != 0) {
		pps_err(" failed to register bq25980 i2c driver.\n");
	} else {
		pps_err(" Success to register bq25980 i2c driver.\n");
	}

	return ret;
}
EXPORT_SYMBOL(bq25980_master_subsys_init);

subsys_initcall(bq25980_master_subsys_init);
#else
int bq25980_master_subsys_init(void)
{
	int ret = 0;
	pps_err(" init start\n");

	if (i2c_add_driver(&bq25980_master_driver) != 0) {
		pps_err(" failed to register bq25980 i2c driver.\n");
	} else {
		pps_err(" Success to register bq25980 i2c driver.\n");
	}

	return ret;
}

void bq25980_master_subsys_exit(void)
{
	i2c_del_driver(&bq25980_master_driver);
}
#endif

MODULE_DESCRIPTION("TI BQ25980 Master Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
