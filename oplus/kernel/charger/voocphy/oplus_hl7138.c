// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Opus. All rights reserved.
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
#include<linux/ktime.h>
#include <linux/pm_qos.h>
#include "oplus_voocphy.h"
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "oplus_hl7138.h"
#include "../charger_ic/oplus_switching.h"
#include "../oplus_chg_module.h"

static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;

extern bool oplus_chg_check_pd_svooc_adapater(void);

#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200

static int __hl7138_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		chg_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __hl7138_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		chg_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}

	return 0;
}

static int hl7138_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int hl7138_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int hl7138_update_bits(struct i2c_client *client, u8 reg,
                              u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_read_byte(client, reg, &tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __hl7138_write_byte(client, reg, tmp);
	if (ret)
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 hl7138_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret;

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		chg_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 hl7138_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		chg_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);
	return 0;
}

static int hl7138_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	int ret;
	if (!chip) {
		chg_err("failed: chip is null\n");
		return -1;
	}

	/* predata, pre_wdata,JL: REG_31 change to REG_3D */
	ret = hl7138_write_word(chip->client, HL7138_REG_3D, val);
	if (ret < 0) {
		chg_err("failed: write predata\n");
		return -1;
	}
	chg_debug("write predata 0x%0x\n", val);
	return ret;
}

static int hl7138_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	int ret;
	if (!chip) {
		chg_err("failed: chip is null\n");
		return -1;
	}

	/* txbuff, tx_wdata, JL: REG_2C change to REG_38 */
	ret = hl7138_write_word(chip->client, HL7138_REG_38, val);
	if (ret < 0) {
		chg_err("failed: write txbuff\n");
		return -1;
	}

	return ret;
}

static int hl7138_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	int data;
	if (!chip) {
		chg_err("gchip is null\n");
		return -1;
	}

	data = hl7138_read_word(chip->client, HL7138_REG_3A);		/* JL: 2E=RX_Rdata,change to 0x3A */

	if (data < 0) {
		chg_err("hl7138_read_word faile\n");
		return -1;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	chg_debug("data, vooc_flag, vooc_rxdata: 0x%0x 0x%0x 0x%0x\n", data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static int hl7138_clear_interrupts(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 val = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	ret = hl7138_read_byte(chip->client, HL7138_REG_01, &val);
	if (ret) {
		chg_err("clear int fail %d", ret);
		return ret;
	}
	ret = hl7138_read_byte(chip->client, HL7138_REG_3B, &val);
	if (ret) {
		chg_err("clear int fail %d", ret);
		return ret;
	}
	return 0;
}

#define HL7138_SVOOC_IBUS_FACTOR	110/100
#define HL7138_VOOC_IBUS_FACTOR		215/100
#define HL7138_FACTOR_125_100		125/100
#define HL7138_FACTOR_400_100		400/100
#define HL7138_REG_NUM_2 2
#define HL7138_REG_NUM_12 12
#define HL7138_REG_ADC_BIT_OFFSET_4 4
#define HL7138_REG_ADC_H_BIT_VBUS 0
#define HL7138_REG_ADC_L_BIT_VBUS 1
#define HL7138_REG_ADC_H_BIT_ICHG 2
#define HL7138_REG_ADC_L_BIT_ICHG 3
#define HL7138_REG_ADC_H_BIT_VBAT 4
#define HL7138_REG_ADC_L_BIT_VBAT 5
#define HL7138_REG_ADC_H_BIT_VSYS 10
#define HL7138_REG_ADC_L_BIT_VSYS 11
static void hl7138_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_12] = {0};
	int i = 0;
	int data;
	int ret = 0;

	data = hl7138_read_word(chip->client, HL7138_REG_05);
	if (data < 0) {
		chg_err("hl7138_read_word faile\n");
		return;
	}
	chip->int_flag = data;

	/* parse data_block for improving time of interrupt
	 * JL:VIN,IIN,VBAT,IBAT,VTS,VOUT,VDIE,;
	 */
	ret = i2c_smbus_read_i2c_block_data(chip->client, HL7138_REG_42,
						HL7138_REG_NUM_12, data_block);		/* JL:first Reg is 13,need to change to 42; */

	for (i=0; i < HL7138_REG_NUM_12; i++) {
		chg_debug("data_block[%d] = %u\n", i, data_block[i]);
	}

	if (chip->adapter_type == ADAPTER_SVOOC) {
		chip->cp_ichg = ((data_block[HL7138_REG_ADC_H_BIT_ICHG] << HL7138_REG_ADC_BIT_OFFSET_4)
				| data_block[HL7138_REG_ADC_L_BIT_ICHG]) * HL7138_SVOOC_IBUS_FACTOR;	/* Iin_lbs=1.10mA@CP; */
	} else {
		chip->cp_ichg = ((data_block[HL7138_REG_ADC_H_BIT_ICHG] << HL7138_REG_ADC_BIT_OFFSET_4)
				| data_block[HL7138_REG_ADC_L_BIT_ICHG])*HL7138_VOOC_IBUS_FACTOR;	/* Iin_lbs=2.15mA@BP; */
	}
	chip->cp_vbus = ((data_block[HL7138_REG_ADC_H_BIT_VBUS] << HL7138_REG_ADC_BIT_OFFSET_4)
			| data_block[HL7138_REG_ADC_L_BIT_VBUS]) * HL7138_FACTOR_400_100;	/* vbus_lsb=4mV; */
	chip->cp_vsys = ((data_block[HL7138_REG_ADC_H_BIT_VSYS] << HL7138_REG_ADC_BIT_OFFSET_4)
			| data_block[HL7138_REG_ADC_L_BIT_VSYS]) * HL7138_FACTOR_125_100;	/* vout_lsb=1.25mV; */
	chip->cp_vbat = ((data_block[HL7138_REG_ADC_H_BIT_VBAT] << HL7138_REG_ADC_BIT_OFFSET_4)
			| data_block[HL7138_REG_ADC_L_BIT_VBAT]) * HL7138_FACTOR_125_100;	/* vout_lsb=1.25mV; */
	chip->cp_vac = chip->cp_vbus;
	chg_debug("cp_ichg = %d cp_vbus = %d, cp_vsys = %d, cp_vbat = %d, int_flag = %d",
		chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->int_flag);
}

/*********************************************************************/
static int hl7138_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	unsigned char value;
	int ret = 0;
	ret = hl7138_read_byte(chip->client, HL7138_REG_01, &value);	/* clear INTb */

	hl7138_write_byte(chip->client, HL7138_REG_09, 0x00);	/* set default mode; */
	hl7138_write_byte(chip->client, HL7138_REG_0A, 0xAE);	/* set default mode; */
	hl7138_write_byte(chip->client, HL7138_REG_0C, 0x03);	/* set default mode; */
	hl7138_write_byte(chip->client, HL7138_REG_0F, 0x00);	/* set default mode; */
	hl7138_write_byte(chip->client, HL7138_REG_13, 0x40);	/* set 100ms ucp debounce time; */
	hl7138_write_byte(chip->client, HL7138_REG_15, 0x00);	/* set default mode; */
	hl7138_write_byte(chip->client, HL7138_REG_17, 0x00);	/* set default mode; */
	hl7138_write_byte(chip->client, HL7138_REG_37, 0x02);	/* reset VOOC PHY; */
	hl7138_write_byte(chip->client, HL7138_REG_3F, 0xD1);	/* bit7 T6:170us */

	return ret;
}

static u8 hl7138_get_int_value(struct oplus_voocphy_manager *chip)
{
	int int_data;
	u8 data = 0;

	if (!chip) {
		chg_err("%s: chip null\n", __func__);
		return -1;
	}

	int_data = hl7138_read_word(chip->client, HL7138_REG_05);
	if (int_data < 0) {
		chg_err(" read HL7138_REG_05 failed\n");
		return -1;
	}
	chip->int_flag = int_data;
	data = int_data & 0xff;

	return data;
}

static int hl7138_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}
	ret = hl7138_read_byte(chip->client, HL7138_REG_12, data);	/* JL:RST_REG 12 change to 14; */
	if (ret < 0) {
		chg_err("HL7138_REG_12\n");
		return -1;
	}
	*data = *data >> HL7138_CHG_EN_SHIFT;

	return ret;
}

#define HL7138_CHG_EN (HL7138_CHG_ENABLE << HL7138_CHG_EN_SHIFT)                    /* 1 << 7   0x80 */
#define HL7138_CHG_DIS (HL7138_CHG_DISABLE << HL7138_CHG_EN_SHIFT)                  /* 0 << 7   0x00 */
#define HL7138_IBUS_UCP_EN (HL7138_IBUS_UCP_ENABLE << HL7138_IBUS_UCP_DIS_SHIFT)    /* 1 << 2   0x04 */
#define HL7138_IBUS_UCP_DIS (HL7138_IBUS_UCP_DISABLE << HL7138_IBUS_UCP_DIS_SHIFT)  /* 0 << 2   0x00 */
#define HL7138_IBUS_UCP_DEFAULT  0x01

static int hl7138_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	if (enable) {
		if (oplus_chg_check_pd_svooc_adapater()) {
			hl7138_write_byte(chip->client, HL7138_REG_13,
					  HL7138_IBUS_UCP_DEB_100ms << HL7138_IBUS_UCP_DEB_SHIFT); /* UCP debounce time 100ms */
			return hl7138_write_byte(chip->client, HL7138_REG_12, HL7138_CHG_EN | HL7138_IBUS_UCP_EN | HL7138_IBUS_UCP_DEFAULT);   /* is pdsvooc adapter: disable ucp */
		} else {
			return hl7138_write_byte(chip->client, HL7138_REG_12, HL7138_CHG_EN | HL7138_IBUS_UCP_EN | HL7138_IBUS_UCP_DEFAULT);  /* is not pdsvooc adapter: enable ucp */
		}
	} else {
		return hl7138_write_byte(chip->client, HL7138_REG_12, HL7138_CHG_DIS | HL7138_IBUS_UCP_EN | HL7138_IBUS_UCP_DEFAULT);      /* chg disable */
	}
}

static int hl7138_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_2] = {0};
	int cp_ichg = 0;
	u8 cp_enable = 0;

	hl7138_get_chg_enable(chip, &cp_enable);

	if(cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	i2c_smbus_read_i2c_block_data(chip->client, HL7138_REG_44, HL7138_REG_NUM_2, data_block);

	if (chip->adapter_type == ADAPTER_SVOOC) {
		chip->cp_ichg = ((data_block[0] << HL7138_REG_ADC_BIT_OFFSET_4) | data_block[1]) * HL7138_SVOOC_IBUS_FACTOR;	/* Iin_lbs=1.10mA@CP; */
	} else {
		chip->cp_ichg = ((data_block[0] << HL7138_REG_ADC_BIT_OFFSET_4) | data_block[1]) * HL7138_VOOC_IBUS_FACTOR;	/* Iin_lbs=2.15mA@BP; */
	}

	return cp_ichg;
}

int hl7138_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_2] = {0};

	/*parse data_block for improving time of interrupt*/
	i2c_smbus_read_i2c_block_data(chip->client, HL7138_REG_46, HL7138_REG_NUM_2, data_block);

	chip->cp_vbat = ((data_block[0] << HL7138_REG_ADC_BIT_OFFSET_4) | data_block[1]) * HL7138_FACTOR_125_100;

	return chip->cp_vbat;
}

static void hl7138_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	u8 reg = 0;
	if (!chip) {
		chg_err("Failed\n");
		return;
	}

	if (enable)
		hl7138_write_byte(chip->client, HL7138_REG_13,
				  HL7138_IBUS_UCP_DEB_100ms << HL7138_IBUS_UCP_DEB_SHIFT); /* UCP debounce time 100ms */
	else
		hl7138_write_byte(chip->client, HL7138_REG_13,
				  HL7138_IBUS_UCP_DEB_10ms << HL7138_IBUS_UCP_DEB_SHIFT);

	ret = hl7138_read_byte(chip->client, HL7138_REG_13, &reg);
	if (ret < 0) {
		chg_err("HL7138_REG_13\n");
		return;
	}
	chg_debug("pd_svooc config HL7138_REG_13 = %d\n", reg);
}

static bool hl7138_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;

	if (!chip) {
		chg_err("Failed\n");
		return false;
	}

	ret = hl7138_read_byte(chip->client, HL7138_REG_13, &data);
	if (ret < 0) {
		chg_err("HL7138_REG_13\n");
		return false;
	}

	chg_debug("HL7138_REG_13 = 0x%0x\n", data);

	return (data & HL7138_IBUS_UCP_DEB_MASK);
}

static int hl7138_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	ret = hl7138_read_byte(chip->client, HL7138_REG_40, data);
	if (ret < 0) {
		chg_err("HL7138_REG_40\n");
		return -1;
	}

	*data = (*data & HL7138_ADC_ENABLE);

	return ret;
}

static int hl7138_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	if (enable)
		return hl7138_update_bits(chip->client, HL7138_REG_40,
									HL7138_ADC_EN_MASK, HL7138_ADC_ENABLE);
	else
		return hl7138_update_bits(chip->client, HL7138_REG_40,
									HL7138_ADC_EN_MASK, HL7138_ADC_DISABLE);
}

/*
 * @function	 hl7138_set_adc_forcedly_enable
 * @detailed
 * This function will configure the ADC operating mode
 * @param[in] chip -- oplus_voocphy_manager
 * @param[in] mode -- ADC operation mode
 * HL7138_ADC_AUTO_MODE(00)           : ADC Auto mode. Enabled in Standby state and Active state, disabled in Shutdown state
 * HL7138_ADC_FORCEDLY_ENABLED(01)    : ADC forcedly enabled
 * HL7138_ADC_FORCEDLY_DISABLED_10(10): ADC forcedly disabled
 * HL7138_ADC_FORCEDLY_DISABLED_11(11): ADC forcedly disabled
 * @return 0:success
 */
static int hl7138_set_adc_forcedly_enable(struct oplus_voocphy_manager *chip, int mode)
{
	int ret = 0;
        if (!chip) {
                chg_err("Failed\n");
                return -1;
        }

	switch(mode) {
	case ADC_AUTO_MODE:
		ret = hl7138_update_bits(chip->client, HL7138_REG_40,
					HL7138_ADC_FORCEDLY_EN_MASK, HL7138_ADC_AUTO_MODE);
		break;
	case ADC_FORCEDLY_ENABLED:
		ret = hl7138_update_bits(chip->client, HL7138_REG_40,
					HL7138_ADC_FORCEDLY_EN_MASK, HL7138_ADC_FORCEDLY_ENABLED);
		break;
	case ADC_FORCEDLY_DISABLED_10:
		ret = hl7138_update_bits(chip->client, HL7138_REG_40,
					HL7138_ADC_FORCEDLY_EN_MASK, HL7138_ADC_FORCEDLY_DISABLED_10);
		break;
	case ADC_FORCEDLY_DISABLED_11:
		ret = hl7138_update_bits(chip->client, HL7138_REG_40,
					HL7138_ADC_FORCEDLY_EN_MASK, HL7138_ADC_FORCEDLY_DISABLED_11);
		break;
	default:
		chg_err("[HL7138] Without this mode, no processing is performed!!!\n");
	}

	return ret;
}

void hl7138_send_handshake_seq(struct oplus_voocphy_manager *chip)
{
	hl7138_write_byte(chip->client, HL7138_REG_37, 0x81);	/* JL:2B->37,EN & Handshake; */

	chg_debug("hl7138_send_handshake_seq done");
}

int hl7138_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 data;

	/*aviod exit fastchg vbus ovp drop out*/
	hl7138_write_byte(chip->client, HL7138_REG_14, 0x08);

	/* hwic config with plugout */
	hl7138_write_byte(chip->client, HL7138_REG_11, 0xDC);	/* JL:Dis VBAT,IBAT reg; */
	hl7138_write_byte(chip->client, HL7138_REG_08, 0x38);	/* JL:vbat_ovp=4.65V;00->08;(4.65-0.09)/10=54; */
	hl7138_write_byte(chip->client, HL7138_REG_0B, 0x83);	/* JL:VBUS_OVP=7V;4+val*lsb; */
	/* hl7138_write_byte(chip->client, HL7138_REG_0C, 0x0F);		//JL:vbus_ovp=10V;04->0c;10.5/5.25V; */
	hl7138_write_byte(chip->client, HL7138_REG_0E, 0x32);	/* JL:UCP_deb=5ms;IBUS_OCP=3.6A;05->0e;3.5A_max; */
	hl7138_write_byte(chip->client, HL7138_REG_40, 0x00);	/* JL:Dis_ADC;11->40; */
	hl7138_write_byte(chip->client, HL7138_REG_02, 0xE0);	/* JL:mask all INT_FLAG */
	hl7138_write_byte(chip->client, HL7138_REG_10, 0xEC);	/* JL:Dis IIN_REG; */

	/* turn off mos */
	hl7138_write_byte(chip->client, HL7138_REG_12, 0x05);	/* JL:Fsw=500KHz;07->12; */
	/* set 100ms ucp debounce time; */
	hl7138_write_byte(chip->client, HL7138_REG_13, 0x40);

	/* clear tx data */
	hl7138_write_byte(chip->client, HL7138_REG_38, 0x00);	/* JL:2C->38; */
	hl7138_write_byte(chip->client, HL7138_REG_39, 0x00);	/* JL:2D->39; */

	/* disable vooc phy irq */
	hl7138_write_byte(chip->client, HL7138_REG_3C, 0xff);	/* JL:30->3C,VOOC_PHY FLAG ALL MASK; */

	/* set D+ HiZ */
	/* hl7138_write_byte(chip->client, HL7138_REG_21, 0xc0);	//JL:No need in HL7138; */

	/* disable vooc */
	hl7138_read_byte(chip->client, HL7138_REG_3B, &data);	/* before disable vooc, 7138 need clear flag */
	hl7138_write_byte(chip->client, HL7138_REG_37, 0x00);	/* JL:2B->37,Dis all; */
	hl7138_read_byte(chip->client, HL7138_REG_3B, &data);	/* after disable vooc, 7138 need clear flag */
	hl7138_set_predata(chip, 0);

	chg_debug("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}

int hl7138_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	/* to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us */
	hl7138_set_predata(chip, 0);

	/* clear tx data */
	hl7138_write_byte(chip->client, HL7138_REG_38, 0x00);	/* JL:2C->38,Dis all; */
	hl7138_write_byte(chip->client, HL7138_REG_39, 0x00);	/* JL:2D->39,Dis all; */

	/* vooc */
	hl7138_write_byte(chip->client, HL7138_REG_3C, 0x85);	/* JL:30->3C,JUST enable RX_START & TX_DONE; */
	hl7138_send_handshake_seq(chip);

	chg_debug ("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static irqreturn_t hl7138_charger_interrupt(int irq, void *dev_id)
{
	struct oplus_voocphy_manager *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	return oplus_voocphy_interrupt_handler(chip);
}

static int hl7138_init_device(struct oplus_voocphy_manager *chip)
{
	hl7138_write_byte(chip->client, HL7138_REG_40, 0x00);	/* ADC_CTRL:disable,JL:11-40; */
	hl7138_write_byte(chip->client, HL7138_REG_0B, 0x83);	/* VBUS_OVP=7V,JL:02->0B; */
	/* hl7138_write_byte(chip->client, HL7138_REG_0C, 0x0F);		//VBUS_OVP:10.2 2:1 or 1:1V,JL:04-0C; */
	hl7138_write_byte(chip->client, HL7138_REG_11, 0xDC);	/* ovp:90mV */
	hl7138_write_byte(chip->client, HL7138_REG_08, 0x38);	/* VBAT_OVP:4.56	4.56+0.09*/
	hl7138_write_byte(chip->client, HL7138_REG_0E, 0x32);	/* IBUS_OCP:3.5A      ocp:100mA */
	/* hl7138_write_byte(chip->client, HL7138_REG_0A, 0x2E);		//IBAT_OCP:max;JL:01-0A;0X2E=6.6A,MAX; */
	hl7138_write_byte(chip->client, HL7138_REG_37, 0x00);	/* VOOC_CTRL:disable;JL:2B->37; */

	hl7138_write_byte(chip->client, HL7138_REG_3C, 0x85);	/* diff mask inter; */
	hl7138_write_byte(chip->client, HL7138_REG_02, 0xE0);	/* JL:mask all INT_FLAG */
	hl7138_write_byte(chip->client, HL7138_REG_10, 0xEC);	/* JL:Dis IIN_REG; */
	hl7138_write_byte(chip->client, HL7138_REG_12, 0x05);	/* JL:Fsw=500KHz;07->12; */
	hl7138_write_byte(chip->client, HL7138_REG_14, 0x08);	/* JL:dis WDG; */
	hl7138_write_byte(chip->client, HL7138_REG_16, 0x2C);	/* JL:OV=500, UV=250 */

	return 0;
}

static int hl7138_work_mode_lockcheck(struct oplus_voocphy_manager *chip)
{
	unsigned char reg;

	if (!chip) {
		return -1;
	}

	if (!hl7138_read_byte(chip->client, HL7138_REG_A7, &reg) && reg != 0x4) {
		/*test mode unlock & lock avoid burnning out the chip*/
		hl7138_write_byte(chip->client, HL7138_REG_A0, 0xF9);
		hl7138_write_byte(chip->client, HL7138_REG_A0, 0x9F);	/* Unlock test register */
		hl7138_write_byte(chip->client, HL7138_REG_A7, 0x04);
		hl7138_write_byte(chip->client, HL7138_REG_A0, 0x00);	/* Lock test register */
		chg_err("hl7138_work_mode_lockcheck done\n");
	}
	return 0;
}

int hl7138_init_vooc(struct oplus_voocphy_manager *chip)
{
	chg_err(">>>> start init vooc\n");

	hl7138_reg_reset(chip, true);
	hl7138_work_mode_lockcheck(chip);
	hl7138_init_device(chip);

	/* to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us */
	hl7138_set_predata(chip, 0);

	return 0;
}

static int hl7138_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		chg_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->irq_gpio = of_get_named_gpio(node,
	                                   "qcom,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		chg_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			rc = gpio_request(chip->irq_gpio,
			                  "irq_gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
				         chip->irq_gpio);
			}
		}
		chg_err("chip->irq_gpio =%d\n", chip->irq_gpio);
	}
	/* irq_num */
	chip->irq = gpio_to_irq(chip->irq_gpio);
	chg_err("irq way1 chip->irq =%d\n",chip->irq);

	/* set voocphy pinctrl*/
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->charging_inter_active =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_active");
	if (IS_ERR_OR_NULL(chip->charging_inter_active)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	chip->charging_inter_sleep =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_sleep");
	if (IS_ERR_OR_NULL(chip->charging_inter_sleep)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	/* irq active */
	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl, chip->charging_inter_active); /* no_PULL */

	rc = gpio_get_value(chip->irq_gpio);
	chg_err("irq chip->irq_gpio input =%d irq_gpio_stat = %d\n", chip->irq_gpio, rc);

	return 0;
}

static int hl7138_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret = 0;

	hl7138_irq_gpio_init(chip);
	chg_err("hl7138 chip->irq = %d\n", chip->irq);
	if (chip->irq) {
        ret = request_threaded_irq(chip->irq, NULL,
                                   hl7138_charger_interrupt,
                                   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                   "hl7138_charger_irq", chip);
		if (ret < 0) {
			chg_debug("request irq for irq=%d failed, ret =%d\n",
							chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
	}
	chg_debug("request irq ok\n");

	return ret;
}

static int hl7138_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	/* hl7138_write_byte(chip->client, HL7138_REG_08, 0x38);	//VBAT_OVP:4.65V,JL:00-08; */
	hl7138_write_byte(chip->client, HL7138_REG_40, 0x05);	/* ADC_CTRL:ADC_EN,JL:11-40; */

	hl7138_write_byte(chip->client, HL7138_REG_0B, 0x88);	/* VBUS_OVP:12V */
	hl7138_write_byte(chip->client, HL7138_REG_0C, 0x00);	/* VIN_OVP:10.2V,,JL:04-0C; */

	if (chip->high_curr_setting)
		hl7138_write_byte(chip->client, HL7138_REG_0E, 0xB2);	/* disable OCP */
	else
		hl7138_write_byte(chip->client, HL7138_REG_0E, 0x32);	/* IBUS_OCP:3.6A,UCP_DEB=5ms;JL:05-0E; */

	hl7138_write_byte(chip->client, HL7138_REG_14, 0x02);	/* WD:1000ms,JL:09-14; */
	hl7138_write_byte(chip->client, HL7138_REG_15, 0x00);	/* enter cp mode */
	hl7138_write_byte(chip->client, HL7138_REG_16, 0x2C);	/* JL:OV=500, UV=250 */

	hl7138_write_byte(chip->client, HL7138_REG_3F, 0x91);	/* Loose_det=1,JL:33-3F; */

	return 0;
}

static int hl7138_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	/* hl7138_write_byte(chip->client, HL7138_REG_08, 0x38);	//VBAT_OVP:4.65V,JL:00-08; */
	hl7138_write_byte(chip->client, HL7138_REG_40, 0x05);	/* ADC_CTRL:ADC_EN,JL:11-40; */

	hl7138_write_byte(chip->client, HL7138_REG_0B, 0x83);	/* VBUS_OVP=7V */
	hl7138_write_byte(chip->client, HL7138_REG_0C, 0x0F);	/* VIN_OVP:5.85v,,JL:04-0C; */

	hl7138_write_byte(chip->client, HL7138_REG_0E, 0x1A);	/* IBUS_OCP:4.6A,(16+9)*0.1+0.1+2=4.6A; */

	hl7138_write_byte(chip->client, HL7138_REG_14, 0x02);	/* WD:1000ms,JL:09-14; */
	hl7138_write_byte(chip->client, HL7138_REG_15, 0x80);	/* JL:bp mode; */
	hl7138_write_byte(chip->client, HL7138_REG_16, 0x2C);	/* JL:OV=500, UV=250 */
	hl7138_write_byte(chip->client, HL7138_REG_3F, 0x91);	/* Loose_det=1,JL:33-3F; */

	return 0;
}

static int hl7138_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	/* hl7138_write_byte(chip->client, HL7138_REG_08, 0x38);	//VBAT_OVP:4.65V,JL:00-08; */
	hl7138_write_byte(chip->client, HL7138_REG_0B, 0x83);	/* VBUS_OVP=7V */
	hl7138_write_byte(chip->client, HL7138_REG_0C, 0x0F);	/* VIN_OVP:11.7v,,JL:04-0C; */
	hl7138_write_byte(chip->client, HL7138_REG_0E, 0xAF);	/* IBUS_OCP:3.6A,UCP_DEB=5ms;JL:05-0E; */
	hl7138_write_byte(chip->client, HL7138_REG_14, 0x08);	/* WD:DIS,JL:09-14; */
	hl7138_write_byte(chip->client, HL7138_REG_15, 0x80);	/* JL:bp mode; */

	hl7138_write_byte(chip->client, HL7138_REG_40, 0x00);	/* ADC_CTRL:disable,JL:11-40; */
	hl7138_write_byte(chip->client, HL7138_REG_37, 0x00);	/* VOOC_CTRL:disable;JL:2B->37; */
	/* hl7138_write_byte(chip->client, HL7138_REG_3D, 0x01);	//pre_wdata,JL:31-3D; */
	/* hl7138_write_byte(chip->client, HL7138_REG_3E, 0x80);	//pre_wdata,JL:32-3E; */
	/* hl7138_write_byte(chip->client, HL7138_REG_3F, 0xd1);	//Loose_det=1,JL:33-3F; */
	return 0;
}

static int hl7138_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_write_byte(chip->client, HL7138_REG_08, 0x2E);	/* VBAT_OVP:4.45V */
	hl7138_write_byte(chip->client, HL7138_REG_0B, 0x88);	/* VBUS_OVP:12V */
	hl7138_write_byte(chip->client, HL7138_REG_0C, 0x0F);	/* VIN_OVP:11.7V */
	hl7138_write_byte(chip->client, HL7138_REG_0E, 0xAF);	/* IBUS_OCP:3.6A */
	hl7138_write_byte(chip->client, HL7138_REG_14, 0x08);	/* WD:DIS */
	hl7138_write_byte(chip->client, HL7138_REG_15, 0x00);	/* enter cp mode */
	hl7138_write_byte(chip->client, HL7138_REG_40, 0x00);	/* ADC_CTRL:disable */
	hl7138_write_byte(chip->client, HL7138_REG_37, 0x00);	/* VOOC_CTRL:disable */
	return 0;
}

int hl7138_enable_t5t6_check(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!enable) {
		hl7138_write_byte(chip->client, HL7138_REG_3F, 0x91);
	} else {
		hl7138_write_byte(chip->client, HL7138_REG_3F, 0xD1);
	}
	return 0;
}

#define MDELAY_10 10
static int hl7138_hw_reset(struct oplus_voocphy_manager *chip)
{
	hl7138_write_byte(chip->client, HL7138_REG_14, 0xc8);
	mdelay(MDELAY_10);

	return 0;
}

static int hl7138_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		chg_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
		case SETTING_REASON_PROBE:
		case SETTING_REASON_RESET:
			hl7138_init_device(chip);
			/*reset for avoiding PBS01 & PBV01 chg break*/
			hl7138_enable_t5t6_check(chip, true);
			hl7138_hw_reset(chip);
			chg_debug("SETTING_REASON_RESET OR PROBE\n");
			break;
		case SETTING_REASON_SVOOC:
			hl7138_svooc_hw_setting(chip);
			chg_debug("SETTING_REASON_SVOOC\n");
			break;
		case SETTING_REASON_VOOC:
			hl7138_vooc_hw_setting(chip);
			chg_debug("SETTING_REASON_VOOC\n");
			break;
		case SETTING_REASON_5V2A:
			hl7138_5v2a_hw_setting(chip);
			chg_debug("SETTING_REASON_5V2A\n");
			break;
		case SETTING_REASON_PDQC:
			hl7138_pdqc_hw_setting(chip);
			chg_debug("SETTING_REASON_PDQC\n");
			break;
		default:
			chg_err("do nothing\n");
			break;
	}
	return 0;
}

static ssize_t hl7138_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "chip");
	for (addr = 0x0; addr <= 0x4F; addr++) {
		if((addr < 0x18) || (addr > 0x35 && addr < 0x4F)) {
			ret = hl7138_read_byte(chip->client, addr, &val);
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

static ssize_t hl7138_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x4F)
		hl7138_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0660, hl7138_show_registers, hl7138_store_register);

static void hl7138_create_device_node(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_registers);
	if (err)
		chg_err("hl7138 create device err!\n");
}

static struct of_device_id hl7138_charger_match_table[] = {
	{
		.compatible = "chip,hl7138-standalone",
	},
	{},
};

static int hl7138_gpio_init(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get chargerid_switch_gpio pinctrl fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_high =
	    pinctrl_lookup_state(chip->pinctrl,
	                         "switch1_act_switch2_act");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_high)) {
		chg_err("get switch1_act_switch2_act fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_low =
	    pinctrl_lookup_state(chip->pinctrl,
	                         "switch1_sleep_switch2_sleep");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_low)) {
		chg_err("get switch1_sleep_switch2_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->pinctrl,
	                     chip->charger_gpio_sw_ctrl2_low);

	chip->slave_charging_inter_default =
	    pinctrl_lookup_state(chip->pinctrl,
	                         "slave_charging_inter_default");
	if (IS_ERR_OR_NULL(chip->slave_charging_inter_default)) {
		chg_err("get slave_charging_inter_default fail\n");
	} else {
		pinctrl_select_state(chip->pinctrl,
	                     chip->slave_charging_inter_default);
	}

	chg_err("oplus_chip is ready!\n", __func__);
	return 0;
}

static int hl7138_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node * node = NULL;

	if (!chip) {
		chg_debug("chip null\n");
		return -1;
	}

	/* Parsing gpio switch gpio47*/
	node = chip->dev->of_node;
	chip->switch1_gpio = of_get_named_gpio(node,
	                                       "qcom,charging_switch1-gpio", 0);
	if (chip->switch1_gpio < 0) {
		chg_debug("chip->switch1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->switch1_gpio)) {
			rc = gpio_request(chip->switch1_gpio,
			                  "charging-switch1-gpio");
			if (rc) {
				chg_debug("unable to request gpio [%d]\n",
				         chip->switch1_gpio);
			} else {
				rc = hl7138_gpio_init(chip);
				if (rc)
					chg_err("unable to init charging_sw_ctrl2-gpio:%d\n",
					        chip->switch1_gpio);
			}
		}
		chg_debug("chip->switch1_gpio =%d\n", chip->switch1_gpio);
	}

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_low",
	                          &chip->voocphy_vbus_low);
	if (rc) {
		chip->voocphy_vbus_low = DEFUALT_VBUS_LOW;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_low);

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_high",
	                          &chip->voocphy_vbus_high);
	if (rc) {
		chip->voocphy_vbus_high = DEFUALT_VBUS_HIGH;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_high);

	chip->high_curr_setting = of_property_read_bool(node, "qcom,high_curr_setting");

	return 0;
}

static void hl7138_set_switch_fast_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("hl7138_set_switch_fast_charger chip null\n");
		return;
	}

	mutex_lock(&chip->voocphy_pinctrl_mutex);
	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_high);
	gpio_direction_output(chip->switch1_gpio, 1);	/* out 1*/
	mutex_unlock(&chip->voocphy_pinctrl_mutex);

	chg_err("switch switch2 %d to fast finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void hl7138_set_switch_normal_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("hl7138_set_switch_normal_charger chip null\n");
		return;
	}

	mutex_lock(&chip->voocphy_pinctrl_mutex);
	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);
	if (chip->switch1_gpio > 0) {
		gpio_direction_output(chip->switch1_gpio, 0);	/* in 0*/
	}
	mutex_unlock(&chip->voocphy_pinctrl_mutex);

	chg_err("switch switch2 %d to normal finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void hl7138_set_switch_mode(struct oplus_voocphy_manager *chip, int mode)
{
	switch (mode) {
	case VOOC_CHARGER_MODE:
		hl7138_set_switch_fast_charger(chip);
		break;
	case NORMAL_CHARGER_MODE:
	default:
		hl7138_set_switch_normal_charger(chip);
		break;
	}

	return;
}

static int hl7138_get_chip_id(void)
{
	return CHIP_ID_HL7138;
}

struct oplus_voocphy_operations oplus_hl7138_ops = {
	.hw_setting		= hl7138_hw_setting,
	.init_vooc		= hl7138_init_vooc,
	.set_predata		= hl7138_set_predata,
	.set_txbuff		= hl7138_set_txbuff,
	.get_adapter_info	= hl7138_get_adapter_info,
	.update_data		= hl7138_update_data,
	.get_chg_enable		= hl7138_get_chg_enable,
	.set_chg_enable		= hl7138_set_chg_enable,
	.reset_voocphy		= hl7138_reset_voocphy,
	.reactive_voocphy 	= hl7138_reactive_voocphy,
	.set_switch_mode	= hl7138_set_switch_mode,
	.send_handshake		= hl7138_send_handshake_seq,
	.get_cp_vbat		= hl7138_get_cp_vbat,
	.get_int_value		= hl7138_get_int_value,
	.get_adc_enable		= hl7138_get_adc_enable,
	.set_adc_enable		= hl7138_set_adc_enable,
	.set_adc_forcedly_enable= hl7138_set_adc_forcedly_enable,
	.get_ichg		= hl7138_get_cp_ichg,
	.set_pd_svooc_config	= hl7138_set_pd_svooc_config,
	.get_pd_svooc_config	= hl7138_get_pd_svooc_config,
	.clear_interrupts	= hl7138_clear_interrupts,
	.get_chip_id		= hl7138_get_chip_id,
};

static int hl7138_charger_choose(struct oplus_voocphy_manager *chip)
{
	int ret;

	if (!oplus_voocphy_chip_is_null()) {
		chg_err("oplus_voocphy_chip already exists!");
		return 0;
	} else {
		ret = i2c_smbus_read_byte_data(chip->client, 0x00);
		chg_err("0x00 = %d\n", ret);
		if (ret < 0) {
			chg_err("i2c communication fail");
			return -EPROBE_DEFER;
		}
		else
			return 1;
	}
}

static int hl7138_charger_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	int ret;

	chg_err("hl7138_charger_probe enter!\n");
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->dev = &client->dev;
	chip->client = client;
	mutex_init(&i2c_rw_lock);
	mutex_init(&chip->voocphy_pinctrl_mutex);

	i2c_set_clientdata(client, chip);

	ret = hl7138_charger_choose(chip);
	if (ret <= 0)
		return ret;

	hl7138_create_device_node(&(client->dev));

	ret = hl7138_parse_dt(chip);

	ret = hl7138_irq_register(chip);
	if (ret < 0)
		goto hl7138_probe_err;

	/* check commu of iic to identificate chip id */
	ret = hl7138_reg_reset(chip, true);
	if (ret) {
		goto hl7138_probe_err;
	}

	hl7138_work_mode_lockcheck(chip);
	hl7138_init_device(chip);


	chip->ops = &oplus_hl7138_ops;

	oplus_voocphy_init(chip);

	oplus_voocphy_mg = chip;

	init_proc_voocphy_debug();

	chg_err("hl7138_charger_probe succesfull\n");
	return 0;

hl7138_probe_err:
	chg_err("hl7138_charger_probe failed\n");
	return ret;
}

static void hl7138_charger_shutdown(struct i2c_client *client)
{
	if (oplus_voocphy_mg) {
		hl7138_write_byte(oplus_voocphy_mg->client, HL7138_REG_40, 0x00);	/* disable */
		hl7138_reg_reset(oplus_voocphy_mg, true);
		hl7138_hw_reset(oplus_voocphy_mg);
	}
	chg_err("hl7138_charger_shutdown end\n");

	return;
}

static const struct i2c_device_id hl7138_charger_id[] = {
	{"hl7138-standalone", 0},
	{},
};

static struct i2c_driver hl7138_charger_driver = {
	.driver		= {
		.name	= "hl7138-charger",
		.owner	= THIS_MODULE,
		.of_match_table = hl7138_charger_match_table,
	},
	.id_table	= hl7138_charger_id,

	.probe		= hl7138_charger_probe,
	.shutdown	= hl7138_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init hl7138_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&hl7138_charger_driver) != 0) {
		chg_err(" failed to register hl7138 i2c driver.\n");
	} else {
		chg_debug(" Success to register hl7138 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(hl7138_subsys_init);
#else
int hl7138_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&hl7138_charger_driver) != 0) {
		chg_err(" failed to register hl7138 i2c driver.\n");
	} else {
		chg_debug(" Success to register hl7138 i2c driver.\n");
	}

	return ret;
}

void hl7138_subsys_exit(void)
{
	i2c_del_driver(&hl7138_charger_driver);
}
oplus_chg_module_register(hl7138_subsys);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("Hl7138 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("OPLUS");
