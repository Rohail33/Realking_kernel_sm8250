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
#include "oplus_sc8571.h"
#include "../oplus_pps.h"

static struct chip_sc8571 *chip_sc8571_slave = NULL;
static struct mutex i2c_rw_lock;

bool sc8571_slave_get_enable(void);
int sc8571_slave_dump_registers(void);
/************************************************************************/
static int __sc8571_read_byte(u8 reg, u8 *data)
{
	int ret = 0;
	int retry = 3;

	struct chip_sc8571 *chip = chip_sc8571_slave;
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}

	ret = i2c_smbus_read_byte_data(chip->slave_client, reg);

	if (ret < 0) {
		pps_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_byte_data(chip->slave_client, reg);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	*data = (u8)ret;

	return 0;
}

static int __sc8571_write_byte(int reg, u8 val)
{
	int ret = 0;
	int retry = 3;

	struct chip_sc8571 *chip = chip_sc8571_slave;
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}

	ret = i2c_smbus_write_byte_data(chip->slave_client, reg, val);
	if (ret < 0) {
		pps_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_write_byte_data(chip->slave_client, reg, val);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	return ret;
}

static int sc8571_read_byte(u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8571_read_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8571_write_byte(u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8571_write_byte(reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8571_read_word(u8 reg, u8 *data_block)
{
	struct chip_sc8571 *chip = chip_sc8571_slave;
	int ret = 0;
	int retry = 3;
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_i2c_block_data(chip->slave_client, reg, 2, data_block);
	if (ret < 0) {
		pps_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&i2c_rw_lock);
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_i2c_block_data(chip->slave_client, reg, 2,
					    data_block);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static int sc8571_slave_i2c_masked_write(u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8571_read_byte(reg, &tmp);
	if (ret) {
		pps_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __sc8571_write_byte(reg, tmp);
	if (ret)
		pps_err("Faileds: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

int sc8571_slave_get_tdie(void)
{
	u8 data_block[2] = { 0 };
	int tdie = 0;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return tdie;
	}

	sc8571_read_word(SC8571_REG_37, data_block);
	tdie = (((data_block[0] & SC8571_TDIE_POL_H_MASK) << 8) |
		(data_block[1] & SC8571_TDIE_POL_L_MASK)) *
	       SC8571_TDIE_ADC_LSB;

	if (tdie < SC8571_TDIE_MIN || tdie > SC8571_TDIE_MAX)
		tdie = SC8571_TDIE_MAX;
	return tdie;
}

int sc8571_slave_get_ucp_flag(void)
{
	int ret = 0;
	u8 temp;
	int ucp_fail = 0;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return 0;
	}

	ret = sc8571_read_byte(SC8571_REG_19, &temp);
	if (ret < 0) {
		pps_err("SC8571_REG_19\n");
		return 0;
	}

	ucp_fail = (temp & SC8571_BUS_UCP_FALL_FLAG_MASK) >>
		   SC8571_BUS_UCP_FALL_FLAG_SHIFT;
	pps_err("0x19[0x%x] ucp_fail = %d\n", temp, ucp_fail);

	return ucp_fail;
}

int sc8571_slave_get_vout(void)
{
	u8 data_block[2] = { 0 };
	int vout = 0;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return vout;
	}

	sc8571_read_word(SC8571_REG_2D, data_block);
	vout = (((data_block[0] & SC8571_VOUT_POL_H_MASK) << 8) |
		(data_block[1] & SC8571_VOUT_POL_L_MASK)) *
	       SC8571_VOUT_ADC_LSB;

	if (vout < SC8571_VOUT_MIN || vout > SC8571_VOUT_MAX)
		vout = SC8571_VOUT_MAX;
	return vout;
}

int sc8571_slave_get_vac(void)
{
	u8 data_block[2] = { 0 };
	int vac = 0;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return vac;
	}

	sc8571_read_word(SC8571_REG_29, data_block);
	vac = (((data_block[0] & SC8571_VAC1_POL_H_MASK) << 8) |
	       (data_block[1] & SC8571_VAC1_POL_L_MASK)) *
	      SC8571_VAC1_ADC_LSB;

	if (vac < SC8571_VAC1_MIN || vac > SC8571_VAC1_MAX)
		vac = SC8571_VAC1_MAX;
	return vac;
}

int sc8571_slave_get_vbus(void)
{
	u8 data_block[2] = { 0 };
	int cp_vbus = 0;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return cp_vbus;
	}

	sc8571_read_word(SC8571_REG_27, data_block);
	cp_vbus = (((data_block[0] & SC8571_VBUS_POL_H_MASK) << 8) |
		   (data_block[1] & SC8571_VBUS_POL_L_MASK)) *
		  SC8571_VBUS_ADC_LSB;

	if (cp_vbus < SC8571_VBUS_MIN || cp_vbus > SC8571_VBUS_MAX)
		cp_vbus = SC8571_VBUS_MAX;
	return cp_vbus;
}

int sc8571_slave_get_ibus(void)
{
	u8 data_block[2] = { 0 };
	int cp_ibus = 0;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return cp_ibus;
	}

	sc8571_read_word(SC8571_REG_25, data_block);
	cp_ibus = (((data_block[0] & SC8571_IBUS_POL_H_MASK) << 8) |
		   (data_block[1] & SC8571_IBUS_POL_L_MASK)) *
		  SC8571_IBUS_ADC_LSB;

	if (cp_ibus < SC8571_IBUS_MIN || cp_ibus > SC8571_IBUS_MAX)
		cp_ibus = SC8571_IBUS_MAX;
	return cp_ibus;
}

int sc8571_slave_cp_enable(int enable)
{
	struct chip_sc8571 *chip = chip_sc8571_slave;
	int ret = 0;
	if (!chip) {
		pps_err("chip is NULL\n");
		return ret;
	}
	mutex_lock(&chip->cp_enable_mutex);
	if (enable && (sc8571_slave_get_enable() == false)) {
		ret = sc8571_slave_i2c_masked_write(
			SC8571_REG_0F, SC8571_CHG_EN_MASK,
			SC8571_CHG_ENABLE << SC8571_CHG_EN_SHIFT);
	} else if (!enable) {
		 if (sc8571_slave_get_enable() == false)
		 	msleep(100);
		 if (sc8571_slave_get_enable() == true)
			ret = sc8571_slave_i2c_masked_write(
				SC8571_REG_0F, SC8571_CHG_EN_MASK,
				SC8571_CHG_DISABLE << SC8571_CHG_EN_SHIFT);
	}
	mutex_unlock(&chip->cp_enable_mutex);
	return ret;
}

bool sc8571_slave_get_enable(void)
{
	int ret = 0;
	u8 temp;
	bool cp_enable = false;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return false;
	}

	ret = sc8571_read_byte(SC8571_REG_0F, &temp);
	if (ret < 0) {
		pr_err("SC8571_REG_0F\n");
		return false;
	}

	cp_enable = (temp & SC8571_CHG_EN_MASK) >> SC8571_CHG_EN_SHIFT;

	return cp_enable;
}

void sc8571_slave_pmid2vout_enable(bool enable)
{
	return; /*return temporary*/
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return;
	}

	if (enable == false)
		sc8571_write_byte(SC8571_REG_41,
				  0x20); /*0X41 disable pmid2vout*/
	else
		sc8571_write_byte(SC8571_REG_41,
				  0x00); /*0X41 enable pmid2vout*/
}

void sc8571_slave_cfg_sc(void)
{
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x00); /*0x0F Disable charge, SC_mode*/
	sc8571_write_byte(SC8571_REG_00, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	sc8571_write_byte(SC8571_REG_01, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/

	sc8571_write_byte(SC8571_REG_05, 0x00); /*0X05 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_06, 0x4B); /*0X06 BUS_OVP=23V*/
	sc8571_write_byte(SC8571_REG_07, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_08, 0x14); /*0X08 DIS_IBUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_0A, 0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	sc8571_write_byte(SC8571_REG_0E, 0xD8); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	sc8571_write_byte(SC8571_REG_10, 0x30); /*0X30 enable watchdog*/
	sc8571_write_byte(SC8571_REG_11, 0x58); /*0X11  IBUS UCP*/
	sc8571_write_byte(SC8571_REG_12, 0x60); /*0X12 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_23, 0x80); /*0X23 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_24, 0x0E); /*0X24 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_41, 0x20); /*0X41 disable pmid2vout temp*/
	sc8571_write_byte(SC8571_REG_42, 0x5C); /*0X42 pmid2vout 400mv*/
	/*sc8571_slave_pmid2vout_enable(false);*/ /*0X41 disable pmid2vout*/
}

void sc8571_slave_cfg_bypass(void)
{
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x08); /*0x0F Disable charge, Bypass_mode, EN_ACDRV1*/
	sc8571_write_byte(SC8571_REG_00, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	sc8571_write_byte(SC8571_REG_01, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_05, 0x0); /*0X05 DIS_BATOCP_ALM*/

	sc8571_write_byte(SC8571_REG_06, 0x5A); /*0X06 BUS_OVP=10.5V*/
	sc8571_write_byte(SC8571_REG_07, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_08, 0x16); /*0X08 DIS_IBUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_0A, 0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/
	sc8571_write_byte(SC8571_REG_0E, 0x58); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/

	sc8571_write_byte(SC8571_REG_10, 0x30); /*0X30 enable watchdog*/
	sc8571_write_byte(SC8571_REG_11, 0x58); /*0X11 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_12, 0x60); /*0X12 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_23, 0x80); /*0X23 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_24, 0x0E); /*0X24 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_41, 0x20); /*0X41 disable pmid2vout temp*/
	sc8571_write_byte(SC8571_REG_42, 0xFC); /*0xFC*/
	pps_err(" end! \n");
}

void sc8571_slave_hardware_init(void)
{
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x0); /*0x0F Disable charge, sc mode*/
	sc8571_write_byte(SC8571_REG_00, 0x7F); /*0X00	EN_BATOVP=9.540V*/
	sc8571_write_byte(SC8571_REG_01, 0xC6); /*0X01 DIS_BATOVP_ALM*/
	sc8571_write_byte(SC8571_REG_02, 0xD1); /*0X02 DIS_BATOCP*/
	sc8571_write_byte(SC8571_REG_03, 0xD0); /*0X03 DIS_BATOCP_ALM*/
	sc8571_write_byte(SC8571_REG_06, 0x0); /*0X06 BUS_OVP=10.5V*/
	sc8571_write_byte(SC8571_REG_07, 0xA2); /*0X07 DIS_BUSOVP_ALM*/
	sc8571_write_byte(SC8571_REG_0A, 0x0C); /*0X0A TDIE_FLT=140. TDIE_ALM enable.  DIS_TDIE_ALM. DIS_TSBUS. DIS_TSBAT*/

	sc8571_write_byte(SC8571_REG_0E, 0x58); /*0X0E VAC1OVP=12V. VAC2OVP=22V*/
	sc8571_write_byte(SC8571_REG_10, 0x84); /*0X10 disalbe watchdog*/
	sc8571_write_byte(SC8571_REG_23, 0x00); /*0X23 adc disable continous*/
	sc8571_write_byte(SC8571_REG_24, 0x0E); /*0X24 disalbe TSBUT_ADC/TSBAT_ADC/IBAT_ADC*/
	pps_err(" end!\n");
}

void sc8571_slave_reset(void)
{
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return;
	}
	sc8571_write_byte(SC8571_REG_0F, 0x80); /*0x0F reset cp*/
}

int sc8571_slave_dump_registers(void)
{
	int ret = 0;
	char buf[1024];
	char *s;

	u8 addr;
	u8 val_buf[0x43] = { 0x0 };

	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return -1;
	}

	for (addr = 0; addr < 0x10; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s+=sprintf(s, "sc8571_slave_dump_registers:0~0x10");
	for (addr = 0; addr < 0x10; addr++) {
		s+=sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s+=sprintf(s, "\n");
	pr_err("%s \n", buf);

	memset(buf, 0, sizeof(buf));
	s = buf;

	for (addr = 0x10; addr < 0x20; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s+=sprintf(s, "sc8571_slave_dump_registers: 0x10~0x20");
	for (addr = 0x10; addr < 0x20; addr++) {
		s+=sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s+=sprintf(s, "\n");
	pr_err("%s \n", buf);


	memset(buf, 0, sizeof(buf));
	s = buf;

	for (addr = 0x20; addr < 0x30; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s+=sprintf(s, "sc8571_slave_dump_registers: 0x20~0x30");
	for (addr = 0x20; addr < 0x30; addr++) {
		s+=sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s+=sprintf(s, "\n");
	pr_err("%s \n", buf);


	memset(buf, 0, sizeof(buf));
	s = buf;

	for (addr = 0x30; addr < 0x42; addr++) {
		ret = sc8571_read_byte(addr, &val_buf[addr]);
		if (ret < 0) {
			pps_err(" Couldn't read 0x%02x ret = %d\n", addr, ret);
			return -1;
		}
	}
	s = buf;
	s+=sprintf(s, "sc8571_slave_dump_registers: 0x30~0x42");
	for (addr = 0x30; addr < 0x43; addr++) {
		s+=sprintf(s, "[0x%x, 0x%x]", addr, val_buf[addr]);
	}
	s+=sprintf(s, "\n");
	pr_err("%s \n", buf);


	return ret;
}

static ssize_t sc8571_show_registers(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return idx;
	}

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8571");
	for (addr = SC8571_REG_00; addr <= SC8571_REG_43; addr++) {
		ret = sc8571_read_byte(addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sc8571_store_register(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int ret;
	unsigned int reg;
	unsigned int val;
	if (!chip_sc8571_slave) {
		pps_err("chip is NULL\n");
		return 0;
	}

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SC8571_REG_43)
		sc8571_write_byte((unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0660, sc8571_show_registers,
		   sc8571_store_register);

static void sc8571_slave_create_device_node(struct device *dev)
{
	int err = 0;

	err = device_create_file(dev, &dev_attr_registers);
	if (err)
		pps_err("sc8571 create device err!\n");
}

static int sc8571_slave_parse_dt(struct chip_sc8571 *chip)
{
	if (!chip) {
		pps_err("chip is NULL\n");
		return -1;
	}

	return 0;
}

static int sc8571_slave_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct chip_sc8571 *chip;

	pps_err(" enter!\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->slave_client = client;
	chip->slave_dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);
	chip_sc8571_slave = chip;

	sc8571_slave_create_device_node(&(client->dev));

	sc8571_slave_parse_dt(chip);
	sc8571_slave_dump_registers();

	sc8571_slave_hardware_init();
	/*oplus_pps_cp_register_ops(&oplus_sc8571_ops);*/

	sc8571_slave_get_enable();
	mutex_init(&chip->cp_enable_mutex);

	pps_err(" successfully!\n");

	return 0;
}

static void sc8571_slave_shutdown(struct i2c_client *client)
{
	return;
}

static struct of_device_id sc8571_slave_match_table[] = {
	{
		.compatible = "oplus,sc8571-slave",
	},
	{},
};

static const struct i2c_device_id sc8571_slave_charger_id[] = {
	{ "sc8571-slave", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sc8571_slave_charger_id);

static struct i2c_driver sc8571_slave_driver = {
	.driver =
		{
			.name = "sc8571-slave",
			.owner = THIS_MODULE,
			.of_match_table = sc8571_slave_match_table,
		},
	.id_table = sc8571_slave_charger_id,

	.probe = sc8571_slave_probe,
	.shutdown = sc8571_slave_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
int __init sc8571_slave_subsys_init(void)
{
	int ret = 0;
	pps_err("init start\n");

	if (i2c_add_driver(&sc8571_slave_driver) != 0) {
		pps_err(" failed to register sc8571 i2c driver.\n");
	} else {
		pps_err(" Success to register sc8571 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(sc8571_slave_subsys_init);
#else
int sc8571_slave_subsys_init(void)
{
	int ret = 0;
	pps_err(" init start\n");

	if (i2c_add_driver(&sc8571_slave_driver) != 0) {
		pps_err(" failed to register sc8571 i2c driver.\n");
	} else {
		pps_err(" Success to register sc8571 i2c driver.\n");
	}

	return ret;
}

void sc8571_slave_subsys_exit(void)
{
	i2c_del_driver(&sc8571_slave_driver);
}
#endif

MODULE_AUTHOR("JJ Kong");
MODULE_DESCRIPTION("SC SC8571 Slave Charge Pump Driver");
MODULE_LICENSE("GPL v2");
