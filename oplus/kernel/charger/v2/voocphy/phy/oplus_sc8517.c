//SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[sc8517]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <soc/oplus/device_info.h>
#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include "../oplus_voocphy.h"
#include "oplus_sc8517.h"

static struct mutex i2c_rw_lock;
static bool error_reported = false;

static void sc8517_i2c_error(struct oplus_voocphy_manager *chip, bool happen)
{
	int report_flag = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return;
	}

	if (error_reported)
		return;

	if (happen) {
		chip->voocphy_iic_err = 1;
		chip->voocphy_iic_err_num++;
		report_flag |= (1 << 0);
#ifdef OPLUS_CHG_UNDEF /* TODO */
		oplus_chg_sc8547_error(report_flag, NULL, 0);
#endif
		if (chip->voocphy_iic_err_num >= 10)
			error_reported = true;
	}
}

/************************************************************************/
static int __sc8517_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		sc8517_i2c_error(chip, true);
		chg_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __sc8517_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		sc8517_i2c_error(chip, true);
		chg_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}

	return 0;
}

static int sc8517_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8517_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8517_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8517_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}


static int sc8517_update_bits(struct i2c_client *client, u8 reg,
                              u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8517_read_byte(client, reg, &tmp);
	if (ret) {
		chg_err("failed to read %02X register, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8517_write_byte(client, reg, tmp);
	if (ret)
		chg_err("failed to write %02X register, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static s32 sc8517_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		sc8517_i2c_error(chip, true);
		chg_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}

	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static s32 sc8517_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		sc8517_i2c_error(chip, true);
		chg_err("i2c write word fail: can't write 0x%02X to reg:0x%02X\n", val, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);

	return 0;
}
static int sc8517_read_i2c_block(struct i2c_client *client, u8 reg, u8 length, u8 *returnData)
{
	int rc = 0;
	int retry = 3;


	mutex_lock(&i2c_rw_lock);
	rc = i2c_smbus_read_i2c_block_data(client, reg, length, returnData);

	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_read_i2c_block_data(client, reg, length, returnData);
			if (rc < 0)
				retry--;
			else
				break;
		}
	}

	if (rc < 0)
		chg_err("read err, rc = %d,\n", rc);
	mutex_unlock(&i2c_rw_lock);

	return rc;
}

static int sc8517_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	/* predata */
	ret = sc8517_write_word(chip->client, SC8517_REG_2A, val);
	if (ret < 0) {
		chg_err("failed to write predata(%d)\n", ret);
		return ret;
	}

	chg_info("write predata 0x%0x\n", val);

	return ret;
}

static int sc8517_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	/* txbuff */
	ret = sc8517_write_word(chip->client, SC8517_REG_25, val);
	if (ret < 0) {
		chg_err("failed to  write txbuff(%d)\n", ret);
		return ret;
	}

	return ret;
}

static int sc8517_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	s32 data;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	data = sc8517_read_word(chip->client, SC8517_REG_27);

	if (data < 0) {
		chg_err("failed to get adapter info(%d)\n", data);
		return data;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	chg_info("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n",
		 data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void sc8517_update_data(struct oplus_voocphy_manager *chip)
{
	/* interrupt_flag */
	/* sc8517_read_byte(chip->client, SC8517_REG_09, &data); */
	chip->interrupt_flag = 0;
	chip->cp_vsys = 0;
	chip->cp_vbat = 0;
	chip->cp_ichg = 0;
	chip->cp_vbus = 0;

	chg_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d interrupt_flag = %d",
		 chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->interrupt_flag);
}

static int sc8517_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	return 0;
}

int sc8517_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	return 0;
}

/*********************************************************************/
static int sc8517_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8517_RESET_REG;
	else
		val = SC8517_NO_REG_RESET;

	val <<= SC8517_REG_RESET_SHIFT;

	ret = sc8517_update_bits(chip->client, SC8517_REG_06,
	                         SC8517_REG_RESET_MASK, val);

	chg_err("%s register reset\n", enable ? "enable" : "false");

	return ret;
}

static int sc8517_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	ret = sc8517_read_byte(chip->client, SC8517_REG_02, data);
	if (ret < 0) {
		chg_err("failed get chg enable status(%d)\n", ret);
		return ret;
	}

	*data = *data & SC8517_CHG_EN_MASK;

	return ret;
}

static int sc8517_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	/* vac range disable */
	sc8517_write_byte(chip->client, SC8517_REG_02, 0x7a);

	if (enable)
		ret = sc8517_write_byte(chip->client, SC8517_REG_02, 0x7b); /* enable mos */
	else
		ret = sc8517_write_byte(chip->client, SC8517_REG_02, 0x78); /* disable mos */
	chg_err("----enable = %d\n", enable);

	if (ret < 0) {
		chg_err("failed to set chg enable(%d)\n", ret);
		return ret;
	}

	chg_err("%s\n", enable ? "enable" : "false");

	return ret;
}


static int sc8517_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	return 0;
}

static int sc8517_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	return 0;
}

static u8 sc8517_get_vbus_status(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 value = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	ret = sc8517_read_byte(chip->client, SC8517_REG_0B, &value);
	if (ret < 0) {
		chg_err("failed to get vbus status(%d)\n", ret);
		return ret;
	}

	value = (value & VBUS_INRANGE_STATUS_MASK) >> VBUS_INRANGE_STATUS_SHIFT;
	chg_info("vbus status: %d\n", value);

	return value;
}

static int sc8517_get_voocphy_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	ret = sc8517_read_byte(chip->client, SC8517_REG_2B, data);
	if (ret < 0)
		chg_err("failed to get voocphy enable status(%d)\n", ret);

	return ret;
}

static void sc8517_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("chip is null\n");
		return;
	}

	chg_info("SC8517_REG_09 -->09~0E[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]\n",
		 chip->reg_dump[9], chip->reg_dump[10], chip->reg_dump[11],
		 chip->reg_dump[12], chip->reg_dump[13], chip->reg_dump[14]);

	return;
}

static u8 sc8517_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 int_column[6];

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_09, 6, int_column);
	if (ret < 0) {
		sc8517_i2c_error(chip, true);
		chg_err("read SC8517_REG_09 6 bytes failed(%d)\n", ret);
		memset(chip->int_column, 0, sizeof(chip->int_column));
		/* set int_column[1]=1, otherwise the dcdc chg can't be enabled*/
		chip->int_column[1] = BIT(0);
		return chip->int_column[1];
	}

	memcpy(chip->int_column, int_column, sizeof(chip->int_column));
	chg_info("SC8517_REG_09 -->09~0E[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]\n",
		 chip->int_column[0], chip->int_column[1], chip->int_column[2],
		 chip->int_column[3], chip->int_column[4], chip->int_column[5]);

	return int_column[1];
}

static u8 sc8517_get_chg_auto_mode(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 value = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}
	ret = sc8517_read_byte(chip->client, SC8517_REG_15, &value);
	value = value & SC8517_CHG_MODE_MASK;
	chg_info("get auto mode value = %d\n", value);

	return value;
}

static int sc8517_set_chg_auto_mode(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	chg_info("enable = %d\n", enable);

	if (enable && (sc8517_get_chg_auto_mode(chip) == SC8517_CHG_FIX_MODE))
		ret = sc8517_update_bits(chip->client, SC8517_REG_15,
					 SC8517_CHG_MODE_MASK,
					 SC8517_CHG_AUTO_MODE);
	else if (!enable && (sc8517_get_chg_auto_mode(chip) == SC8517_CHG_AUTO_MODE))
		ret = sc8517_update_bits(chip->client, SC8517_REG_15,
					 SC8517_CHG_MODE_MASK,
					 SC8517_CHG_FIX_MODE);
	if (ret < 0)
		chg_err("failed to %s auto mode\n", enable ? "enable" : "disable");

	return ret;
}

static void sc8517_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("chip is null\n");
		return;
	}

	sc8517_write_byte(chip->client, SC8517_REG_04, 0x36); /* WD:1000ms */
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xd1); /* Loose_det=1 */

	chg_info("pd svooc \n");
}

static bool sc8517_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("Failed\n");
		return false;
	}
	chg_info("no ucp flag,return true\n");

	return true;
}

void sc8517_send_handshake(struct oplus_voocphy_manager *chip)
{
	/* enable voocphy and handshake */
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x81);
}


static int sc8517_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	/* turn off mos */
	sc8517_write_byte(chip->client, SC8517_REG_02, 0x78);
	/* clear tx data */
	sc8517_write_byte(chip->client, SC8517_REG_25, 0x00);
	sc8517_write_byte(chip->client, SC8517_REG_26, 0x00);
	/* disable vooc phy irq */
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x7F); /* mask all flag */
	sc8517_write_byte(chip->client, SC8517_REG_10, 0x79); /* disable irq */
	/* set D+ HiZ */
	sc8517_write_byte(chip->client, SC8517_REG_20, 0xc0);

	/* select big bang mode */
	/* disable vooc */
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00);
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x06); /* disable wdt */

	/* set predata */
	/* sc8517_write_word(chip->client, SC8517_REG_2A, 0x0); */
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x02); /* reset voocphy */
	msleep(1);
	/* sc8517_set_predata(0x0); */
	chg_err("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}


static int sc8517_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	/* set predata 0 */
	sc8517_write_word(chip->client, SC8517_REG_2A, 0x0);

	/* dpdm */
	sc8517_write_byte(chip->client, SC8517_REG_20, 0x21);
	sc8517_write_byte(chip->client, SC8517_REG_21, 0x80);
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xD1);

	/* clear tx data */
	sc8517_write_byte(chip->client, SC8517_REG_25, 0x00);
	sc8517_write_byte(chip->client, SC8517_REG_26, 0x00);

	/* vooc */
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x05); /* diable vooc int flag mask */
	sc8517_send_handshake(chip);

	chg_info("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static int sc8517_init_device(struct oplus_voocphy_manager *chip)
{
	sc8517_reg_reset(chip, true);
	msleep(10);
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00); /* VOOC_CTRL:disable,no handshake */
	chg_info("\n");

	return 0;
}

static int sc8517_init_vooc(struct oplus_voocphy_manager *chip)
{
	chg_info("start init vooc\n");

	sc8517_write_byte(chip->client, SC8517_REG_24, 0x02); /* reset voocphy */
	sc8517_write_word(chip->client, SC8517_REG_2A, 0x00); /* reset predata 00 */
	msleep(1);
	sc8517_write_byte(chip->client, SC8517_REG_20, 0x21); /* VOOC_CTRL:disable,no handshake */
	sc8517_write_byte(chip->client, SC8517_REG_21, 0x80); /* VOOC_CTRL:disable,no handshake */
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xD1); /* VOOC_CTRL:disable,no handshake */
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x25); /* mask sedseq flag,rx start,tx done */
	/* sc8517_vac_inrange_enable(chip, SC8517_VAC_INRANGE_DISABLE); */

	return 0;
}

static void sc8517_hardware_init(struct oplus_voocphy_manager *chip)
{
	chg_info("\n");
	sc8517_reg_reset(chip, true);
	msleep(10);
	sc8517_write_byte(chip->client, SC8517_REG_01, 0x5e); /* disable v1x_scp */
	sc8517_write_byte(chip->client, SC8517_REG_06, 0x85); /* enable  audio mode */
	sc8517_write_byte(chip->client, SC8517_REG_08, 0xA6); /* REF_SKIP_R 40mv */
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x05); /* Masked Pulse_filtered, RX_Start,Tx_Done,soft intflag */
	sc8517_write_byte(chip->client, SC8517_REG_10, 0x79); /* Masked Pulse_filtered, RX_Start,Tx_Done */
}

static int sc8517_dump_registers(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 int_column[7];

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_00, 7, int_column);
	if (ret < 0) {
		chg_err("read SC8517_REG_00 7 bytes failed(%d)\n", ret);
		return ret;
	}
	chg_info("[00~06][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
		 int_column[0], int_column[1], int_column[2],
		 int_column[3], int_column[4], int_column[5], int_column[6]);

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_07, 7, int_column);
	if (ret < 0) {
		chg_err("read SC8517_REG_07 7 bytes failed[%d]\n", ret);
		return ret;
	}
	chg_info("[07~0D][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
		 int_column[0], int_column[1], int_column[2],
		 int_column[3], int_column[4], int_column[5], int_column[6]);

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_0E, 7, int_column);
	if (ret < 0) {
		chg_err("read SC8517_REG_0E 7 bytes failed[%d]\n", ret);
		return ret;
	}
	chg_info("[0E~14][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
		 int_column[0], int_column[1], int_column[2],
		 int_column[3], int_column[4], int_column[5], int_column[6]);

	return ret;
}

static int sc8517_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x36); /* WD:1000ms */
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xd1); /* Loose_det=1 */

	return 0;
}

static int sc8517_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x46); /* WD:5000ms */
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xd1); /* Loose_det=1 */

	return 0;
}

static int sc8517_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8517_write_byte(chip->client, SC8517_REG_02, 0x78); /* disable acdrv,close mos */
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x06); /* dsiable wdt */
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00); /* close voocphy */

	return 0;
}

static int sc8517_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8517_write_byte(chip->client, SC8517_REG_02, 0x78); /* disable acdrv,close mos */
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x06); /* dsiable wdt */
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00); /* close voocphy */

	return 0;
}

static int sc8517_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		chg_err("chip is null exit\n");
		return -1;
	}
	chg_info("reason = %d\n", reason);
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		sc8517_init_device(chip);
		chg_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		sc8517_svooc_hw_setting(chip);
		chg_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		sc8517_vooc_hw_setting(chip);
		chg_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		sc8517_5v2a_hw_setting(chip);
		chg_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		sc8517_pdqc_hw_setting(chip);
		chg_info("SETTING_REASON_PDQC\n");
		break;
	default:
		chg_info("do nothing\n");
		break;
	}
	return 0;
}

static bool sc8517_check_cp_int_happened(struct oplus_voocphy_manager *chip,
					 bool *dump_reg, bool *send_info)
{
	int i = 0;

	if (((bidirect_int_flag[0].mask & chip->int_column_pre[1]) == 0) && bidirect_int_flag[i].mark_except) {
		*dump_reg = true;
		*send_info = true;
		memcpy(&chip->reg_dump[9], chip->int_column_pre, sizeof(chip->int_column_pre));
		chg_info("cp int happened %s\n", bidirect_int_flag[i].except_info);
		return true;
	}

	for (i = 1; i < 7; i++) {
		if ((bidirect_int_flag[i].mask & chip->int_column_pre[3]) && bidirect_int_flag[i].mark_except) {
			*dump_reg = true;
			*send_info = true;
			memcpy(&chip->reg_dump[9], chip->int_column_pre, sizeof(chip->int_column_pre));
			chg_info("cp int happened %s\n", bidirect_int_flag[i].except_info);
			return true;
		}
	}

	for (i = 7; i < BIDIRECT_IRQ_EVNET_NUM; i++) {
		if ((bidirect_int_flag[i].mask & chip->int_column_pre[5]) && bidirect_int_flag[i].mark_except) {
			*dump_reg = true;
			*send_info = true;
			memcpy(&chip->reg_dump[9], chip->int_column_pre, sizeof(chip->int_column_pre));
			chg_info("cp int happened %s\n", bidirect_int_flag[i].except_info);
			return true;
		}
	}

	return false;
}

static ssize_t sc8517_show_registers(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8517");
	for (addr = 0x0; addr <= 0x3C; addr++) {
		if ((addr < 0x24) || (addr > 0x2B && addr < 0x33)
		    || addr == 0x36 || addr == 0x3C) {
			ret = sc8517_read_byte(chip->client, addr, &val);
			if (ret == 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx,
				               "Reg[%.2X] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t sc8517_store_register(struct device *dev,
                                     struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x3C)
		sc8517_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}


static DEVICE_ATTR(registers, 0660, sc8517_show_registers, sc8517_store_register);

static void sc8517_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}


static struct of_device_id sc8517_charger_match_table[] = {
	{
		.compatible = "sc,sc8517-master",
	},
	{},
};

static void register_voocphy_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int ret = 0;
	char *version;
	char *manufacture;

	version = "sc8517";
	manufacture = "MP";

	ret = register_device_proc("voocphy", version, manufacture);
	if (ret)
		chg_err("register_voocphy_devinfo fail\n");
#endif
}

static struct oplus_voocphy_operations oplus_sc8517_ops = {
	.hardware_init		= sc8517_hardware_init,
	.hw_setting		= sc8517_hw_setting,
	.init_vooc		= sc8517_init_vooc,
	.set_predata	= sc8517_set_predata,
	.set_txbuff		= sc8517_set_txbuff,
	.get_adapter_info	= sc8517_get_adapter_info,
	.update_data		= sc8517_update_data,
	.get_chg_enable		= sc8517_get_chg_enable,
	.set_chg_enable		= sc8517_set_chg_enable,
	.reset_voocphy		= sc8517_reset_voocphy,
	.reactive_voocphy	= sc8517_reactive_voocphy,
	.send_handshake		= sc8517_send_handshake,
	.get_cp_vbat		= sc8517_get_cp_vbat,
	.get_int_value		= sc8517_get_int_value,
	.get_adc_enable		= sc8517_get_adc_enable,
	.set_adc_enable		= sc8517_set_adc_enable,
	.get_ichg			= sc8517_get_cp_ichg,
	.set_pd_svooc_config = sc8517_set_pd_svooc_config,
	.get_pd_svooc_config = sc8517_get_pd_svooc_config,
	.get_vbus_status	 = sc8517_get_vbus_status,
	.set_chg_auto_mode 	= sc8517_set_chg_auto_mode,
	.get_voocphy_enable = sc8517_get_voocphy_enable,
	.dump_voocphy_reg	= sc8517_dump_reg_in_err_issue,
	.check_cp_int_happened = sc8517_check_cp_int_happened,
};

static int sc8517_charger_probe(struct i2c_client *client,
                                const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	int ret;

	chg_info("start\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&i2c_rw_lock);
	i2c_set_clientdata(client, chip);

	sc8517_create_device_node(&(client->dev));
	chip->ops = &oplus_sc8517_ops;
	ret = oplus_register_voocphy(chip);
	if (ret < 0) {
		chg_err("failed to register voocphy, ret = %d", ret);
		return ret;
	}
	register_voocphy_devinfo();
	sc8517_dump_registers(chip);
	chg_info("probe successed\n");

	return 0;
}

static void sc8517_charger_shutdown(struct i2c_client *client)
{
	sc8517_update_bits(client, SC8517_REG_06, SC8517_REG_RESET_MASK,
			   SC8517_RESET_REG << SC8517_REG_RESET_SHIFT);
	msleep(10);
	/* disable v1x_scp */
	sc8517_write_byte(client, SC8517_REG_01, 0x5e);
	/* REF_SKIP_R 40mv */
	sc8517_write_byte(client, SC8517_REG_08, 0xA6);
	/* Masked Pulse_filtered, RX_Start,Tx_Done,bit soft intflag */
	sc8517_write_byte(client, SC8517_REG_29, 0x05);

	return;
}

static const struct i2c_device_id sc8517_charger_id[] = {
	{"sc8517-master", 0},
	{},
};

static struct i2c_driver sc8517_charger_driver = {
	.driver		= {
		.name	= "sc8517-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sc8517_charger_match_table,
	},
	.id_table	= sc8517_charger_id,

	.probe		= sc8517_charger_probe,
	.shutdown	= sc8517_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init sc8517_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&sc8517_charger_driver) != 0) {
		chg_err(" failed to register sc8517 i2c driver.\n");
	} else {
		chg_debug(" Success to register sc8517 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(sc8517_subsys_init);
#else
static int sc8517_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&sc8517_charger_driver) != 0) {
		chg_err(" failed to register sc8517 i2c driver.\n");
	} else {
		chg_debug(" Success to register sc8517 i2c driver.\n");
	}

	return ret;
}

static void sc8517_subsys_exit(void)
{
	i2c_del_driver(&sc8517_charger_driver);
}
oplus_chg_module_register(sc8517_subsys);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC SC8517 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
