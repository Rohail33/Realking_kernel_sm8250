#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <trace/events/sched.h>
#include<linux/ktime.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/sched/clock.h>
#include <linux/mutex.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/of_platform.h>

#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include "../oplus_voocphy.h"
#include "oplus_sc8547.h"

static struct mutex i2c_rw_lock;

static bool error_reported = false;
#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200
#define I2C_ERR_NUM 10
#define MAIN_I2C_ERROR (1 << 0)

static int sc8547_get_chg_enable(struct oplus_voocphy_manager *chip, unsigned char *data);

static void sc8547_i2c_error(struct oplus_voocphy_manager *chip, bool happen)
{
	int report_flag = 0;
	if (!chip || error_reported)
		return;

	if (happen) {
		chip->voocphy_iic_err = true;
		chip->voocphy_iic_err_num++;
		if (chip->voocphy_iic_err_num >= I2C_ERR_NUM){
			report_flag |= MAIN_I2C_ERROR;
#ifdef OPLUS_CHG_UNDEF /* TODO */
			oplus_chg_sc8547_error(report_flag, NULL, 0);
#endif
			error_reported = true;
		}
	} else {
		chip->voocphy_iic_err_num = 0;
#ifdef OPLUS_CHG_UNDEF /* TODO */
		oplus_chg_sc8547_error(0, NULL, 0);
#endif
	}
}

/************************************************************************/
static int __sc8547_read_byte(struct i2c_client *client, unsigned char reg, unsigned char *data)
{
	int ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	sc8547_i2c_error(chip, false);
	*data = (unsigned char) ret;

	return 0;
}

static int __sc8547_write_byte(struct i2c_client *client, int reg, unsigned char val)
{
	int ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}
	sc8547_i2c_error(chip, false);
	return 0;
}

static int sc8547_read_byte(struct i2c_client *client, unsigned char reg, unsigned char *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8547_write_byte(struct i2c_client *client, unsigned char reg, unsigned char data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8547_update_bits(struct i2c_client *client, unsigned char reg,
                              unsigned char mask, unsigned char data)
{
	int ret;
	unsigned char tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_read_byte(client, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8547_write_byte(client, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static int sc8547_read_word(struct i2c_client *client, unsigned char reg)
{
	int ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	sc8547_i2c_error(chip, false);
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static int sc8547_write_word(struct i2c_client *client, unsigned char reg, u16 val)
{
	int ret;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	sc8547_i2c_error(chip, false);
	mutex_unlock(&i2c_rw_lock);
	return 0;
}

static int sc8547_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	int ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	//predata
	ret = sc8547_write_word(chip->client, SC8547_REG_31, val);
	if (ret < 0) {
		pr_err("failed: write predata\n");
		return -1;
	}
	pr_info("write predata 0x%0x\n", val);
	return ret;
}

static int sc8547_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	int ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	//txbuff
	ret = sc8547_write_word(chip->client, SC8547_REG_2C, val);
	if (ret < 0) {
		pr_err("write txbuff\n");
		return -1;
	}

	return ret;
}

static int sc8547_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	int data;
	if (!chip) {
		pr_err("chip is null\n");
		return -1;
	}

	data = sc8547_read_word(chip->client, SC8547_REG_2E);

	if (data < 0) {
		pr_err("sc8547_read_word faile\n");
		return -1;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	pr_info("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n", data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void sc8547_update_data(struct oplus_voocphy_manager *chip)
{
	unsigned char data_block[4] = {0};
	int i = 0;
	unsigned char data = 0;
	int ret = 0;

	/*int_flag*/
	sc8547_read_byte(chip->client, SC8547_REG_0F, &data);
	chip->interrupt_flag = data;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_19, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(chip, false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read vsys vbat data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_vsys = ((data_block[0] << 8) | data_block[1])*125 / 100;
	chip->cp_vbat = ((data_block[2] << 8) | data_block[3])*125 / 100;

	memset(data_block, 0 , sizeof(unsigned char) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_13, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(chip, false);
	}
	for (i = 0; i< 4; i++) {
		pr_info("read ichg vbus data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_ichg = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;
	chip->cp_vbus = (((data_block[2] & SC8547_VBUS_POL_H_MASK) << 8) |
			   data_block[3]) * SC8547_VBUS_ADC_LSB;

	memset(data_block, 0 , sizeof(u8) * 4);
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_17, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("sc8547_update_data read vac error\n");
	} else {
		sc8547_i2c_error(chip, false);
	}

	for (i = 0; i< 2; i++) {
		pr_info("read vac data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_vac = (((data_block[0] & SC8547_VAC_POL_H_MASK) << 8) |
			  data_block[1]) * SC8547_VAC_ADC_LSB;

	pr_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d cp_vac = %d int_flag = %d",
	        chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->cp_vac, chip->interrupt_flag);
}

static int sc8547_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	unsigned char data_block[2] = {0};
	int cp_ichg = 0;
	unsigned char cp_enable = 0;
	int ret = 0;

	sc8547_get_chg_enable(chip, &cp_enable);

	if(cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_13, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("sc8547 read ichg error \n");
	} else {
		sc8547_i2c_error(chip, false);
	}

	cp_ichg = ((data_block[0] << 8) | data_block[1])*1875 / 1000;

	return cp_ichg;
}

static int sc8547_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	unsigned char data_block[2] = {0};
	int ret = 0;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_1B, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("sc8547 read vbat error \n");
	} else {
		sc8547_i2c_error(chip, false);
	}

	chip->cp_vbat = ((data_block[0] << 8) | data_block[1])*125 / 100;

	return chip->cp_vbat;
}

static int sc8547_get_cp_vbus(struct oplus_voocphy_manager *chip)
{
	unsigned char data_block[2] = {0};
	int ret = 0;

	/* parse data_block for improving time of interrupt */
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_15, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(chip, true);
		pr_err("sc8547 read vbat error \n");
	} else {
		sc8547_i2c_error(chip, false);
	}

	chip->cp_vbus = (((data_block[0] & SC8547_VBUS_POL_H_MASK) << 8) |
			   data_block[1]) * SC8547_VBUS_ADC_LSB;

	return chip->cp_vbus;
}

/*********************************************************************/
static int sc8547_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	unsigned char val;
	if (enable)
		val = SC8547_RESET_REG;
	else
		val = SC8547_NO_REG_RESET;

	val <<= SC8547_REG_RESET_SHIFT;

	ret = sc8547_update_bits(chip->client, SC8547_REG_07,
	                         SC8547_REG_RESET_MASK, val);

	return ret;
}

static int sc8547_get_chg_enable(struct oplus_voocphy_manager *chip, unsigned char *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_07, data);
	if (ret < 0) {
		pr_err("SC8547_REG_07\n");
		return -1;
	}

	*data = *data >> 7;

	return ret;
}

static int sc8547_get_voocphy_enable(struct oplus_voocphy_manager *chip, unsigned char *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_2B, data);
	if (ret < 0) {
		pr_err("SC8547_REG_2B\n");
		return -1;
	}

	return ret;
}

static void sc8547_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	int i = 0, p = 0;
	//unsigned char value[DUMP_REG_CNT] = {0};
	if(!chip) {
		pr_err( "!!!!! oplus_voocphy_manager chip NULL");
		return;
	}

	for( i = 0; i < 37; i++) {
		p = p + 1;
		sc8547_read_byte(chip->client, i, &chip->reg_dump[p]);
	}
	for( i = 0; i < 9; i++) {
		p = p + 1;
		sc8547_read_byte(chip->client, 43 + i, &chip->reg_dump[p]);
	}
	p = p + 1;
	sc8547_read_byte(chip->client, SC8547_REG_36, &chip->reg_dump[p]);
	p = p + 1;
	sc8547_read_byte(chip->client, SC8547_REG_3A, &chip->reg_dump[p]);
	pr_err( "p[%d], ", p);

	///memcpy(chip->voocphy.reg_dump, value, DUMP_REG_CNT);
	return;
}

static int sc8547_get_adc_enable(struct oplus_voocphy_manager *chip, unsigned char *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_11, data);
	if (ret < 0) {
		pr_err("SC8547_REG_11\n");
		return -1;
	}

	*data = *data >> 7;

	return ret;
}

static unsigned char sc8547_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	unsigned char data = 0;

	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_0F, &data);
	if (ret < 0) {
		pr_err(" read SC8547_REG_0F failed\n");
		return -1;
	}

	return data;
}

static int sc8547_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable)
		return sc8547_write_byte(chip->client, SC8547_REG_07, 0x84);
	else
		return sc8547_write_byte(chip->client, SC8547_REG_07, 0x04);
}

static void sc8547_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	unsigned char reg_data = 0;
	if (!chip) {
		pr_err("Failed\n");
		return;
	}

	if (enable) {
		reg_data = 0xA0 | (chip->ocp_reg & 0xf);
		sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
		sc8547_write_byte(chip->client, SC8547_REG_09, 0x13);
	}
	else {
		reg_data = 0x20 | (chip->ocp_reg & 0xf);
		sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_05, &reg_data);
	if (ret < 0) {
		pr_err("SC8547_REG_05\n");
		return;
	}
	pr_err("pd_svooc config SC8547_REG_05 = %d\n", reg_data);
}

static bool sc8547_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	unsigned char data = 0;

	if (!chip) {
		pr_err("Failed\n");
		return false;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_05, &data);
	if (ret < 0) {
		pr_err("SC8547_REG_05\n");
		return false;
	}

	pr_err("SC8547_REG_05 = 0x%0x\n", data);

	data = data >> 7;
	if (data == 1)
		return true;
	else
		return false;
}

static int sc8547_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable)
		return sc8547_write_byte(chip->client, SC8547_REG_11, 0x80);
	else
		return sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);
}

void sc8547_send_handshake(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x81);
}


static int sc8547_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	unsigned char reg_data;

	/* turn off mos */
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x04);

	/* hwic config with plugout */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	sc8547_write_byte(chip->client, SC8547_REG_00, reg_data);
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x07);
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);

	reg_data = 0x20 | (chip->ocp_reg & 0xf);
	sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);

	/* clear tx data */
	sc8547_write_byte(chip->client, SC8547_REG_2C, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_2D, 0x00);

	/* disable vooc phy irq */
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x7f);

	/* set D+ HiZ */
	sc8547_write_byte(chip->client, SC8547_REG_21, 0xc0);

	/* select big bang mode */

	/* disable vooc */
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00);

	/* set predata */
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);

	/* mask insert irq */
	sc8547_write_word(chip->client, SC8547_REG_10, 0x02);
	pr_info ("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}


static int sc8547_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	unsigned char value;

	//to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us
	//set predata
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);
	//sc8547_set_predata(0x0);
	sc8547_read_byte(chip->client, SC8547_REG_3A, &value);
	value = value | (3 << 5);
	sc8547_write_byte(chip->client, SC8547_REG_3A, value);

	//dpdm
	sc8547_write_byte(chip->client, SC8547_REG_21, 0x21);
	sc8547_write_byte(chip->client, SC8547_REG_22, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xD1);

	//clear tx data
	sc8547_write_byte(chip->client, SC8547_REG_2C, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_2D, 0x00);

	//vooc
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x05);
	sc8547_send_handshake(chip);

	pr_info ("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static int sc8547_init_device(struct oplus_voocphy_manager *chip)
{
	unsigned char reg_data;
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x0);	/* ADC_CTRL:disable */
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x7);
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);	/* VBUS_OVP:10 2:1 or 1:1V */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	sc8547_write_byte(chip->client, SC8547_REG_00, reg_data);	/* VBAT_OVP:4.65V */
	reg_data = 0x20 | (chip->ocp_reg & 0xf);
	sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);	/* IBUS_OCP_UCP:3.6A */
	sc8547_write_byte(chip->client, SC8547_REG_01, 0xbf);
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00);	/* OOC_CTRL:disable */
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	sc8547_update_bits(chip->client, SC8547_REG_09,
			   SC8547_IBUS_UCP_RISE_MASK_MASK,
			   (1 << SC8547_IBUS_UCP_RISE_MASK_SHIFT));
	sc8547_write_byte(chip->client, SC8547_REG_10, 0x02);	/* mask insert irq */

	return 0;
}

static int sc8547_init_vooc(struct oplus_voocphy_manager *chip)
{
	unsigned char value;
	pr_err(" >>>>start init vooc\n");

	sc8547_reg_reset(chip, true);
	sc8547_init_device(chip);

	//to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us
	//SET PREDATA
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);
	//sc8547_set_predata(0x0);
	sc8547_read_byte(chip->client, SC8547_REG_3A, &value);
	value = value | (1 << 6);
	sc8547_write_byte(chip->client, SC8547_REG_3A, value);
	//sc8547_read_byte(__sc, SC8547_REG_3A, &value);
	pr_err("read value %d\n", value);

	//dpdm
	sc8547_write_byte(chip->client, SC8547_REG_21, 0x21);
	sc8547_write_byte(chip->client, SC8547_REG_22, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xD1);

	//vooc
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x05);

	return 0;
}

static int sc8547_svooc_ovp_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01);
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);

	return 0;
}

static int sc8547_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	unsigned char reg_data;
	//sc8547_write_byte(sc, SC8547_REG_00, 0x2E);
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01);	//VAC_OVP:12v
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);	//VBUS_OVP:10v
	reg_data = 0x20 | (chip->ocp_reg & 0xf);
	sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);	//IBUS_OCP_UCP:3.6A
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x13);	/*WD:1000ms*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x80);	//ADC_CTRL:ADC_EN
	sc8547_write_byte(chip->client, SC8547_REG_0D, 0x70);
	//sc8547_write_byte(chip->client, SC8547_REG_2B, 0x81);	//VOOC_CTRL,send handshake
	//oplus_vooc_send_handshake_seq();
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xd1);	//Loose_det=1
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	return 0;
}

static int sc8547_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	//sc8547_write_byte(sc, SC8547_REG_00, 0x2E);
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x07);	//VAC_OVP:
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);	//VBUS_OVP:
	sc8547_write_byte(chip->client, SC8547_REG_05, 0x2c);	//IBUS_OCP_UCP:
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x93);	/*WD:1000ms*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x80);	//ADC_CTRL:
	//sc8547_write_byte(chip->client, SC8547_REG_2B, 0x81);	//VOOC_CTRL, send handshake
	//oplus_vooc_send_handshake_seq();
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xd1);	//Loose_det
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	return 0;
}

static int sc8547_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	//sc8547_write_byte(sc, SC8547_REG_00, 0x2E);
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x07);	//VAC_OVP:
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);	//VBUS_OVP:
	//sc8547_write_byte(__sc, SC8547_REG_05, 0x2c);	//IBUS_OCP_UCP:
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x04);
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x10);	/*WD:DISABLE*/

	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);	//ADC_CTRL:
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00);	//VOOC_CTRL
	//sc8547_write_byte(__sc, SC8547_REG_31, 0x01);	//
	//sc8547_write_byte(__sc, SC8547_REG_32, 0x80);	//
	//sc8547_write_byte(__sc, SC8547_REG_33, 0xd1);	//Loose_det
	return 0;
}

static int sc8547_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_00, 0x2E);	//VAC_OVP:
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01);	//VAC_OVP:
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);	//VBUS_OVP:
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x04);
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x10);	/*WD:DISABLE*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);	//ADC_CTRL:
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00);	//VOOC_CTRL
	return 0;
}

static int sc8547_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		pr_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		sc8547_init_device(chip);
		pr_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_COPYCAT_SVOOC:
		sc8547_svooc_ovp_hw_setting(chip);
		pr_info("SETTING_REASON_COPYCAT_SVOOC\n");
		break;
	case SETTING_REASON_SVOOC:
		sc8547_svooc_hw_setting(chip);
		pr_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		sc8547_vooc_hw_setting(chip);
		pr_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		sc8547_5v2a_hw_setting(chip);
		pr_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		sc8547_pdqc_hw_setting(chip);
		pr_info("SETTING_REASON_PDQC\n");
		break;
	default:
		pr_err("do nothing\n");
		break;
	}
	return 0;
}

static void sc8547_hardware_init(struct oplus_voocphy_manager *chip)
{
	sc8547_reg_reset(chip, true);
	sc8547_init_device(chip);
}

static bool sc8547_check_cp_int_happened(struct oplus_voocphy_manager *chip,
					 bool *dump_reg, bool *send_info)
{
	int i = 0;

	for (i = 0; i < IRQ_EVNET_NUM; i++) {
		if ((int_flag[i].mask & chip->interrupt_flag) && int_flag[i].mark_except) {
			chg_info("cp int happened %s\n", int_flag[i].except_info);
			if (int_flag[i].mask != VOUT_OVP_FLAG_MASK &&
			    int_flag[i].mask != ADAPTER_INSERT_FLAG_MASK &&
			    int_flag[i].mask != VBAT_INSERT_FLAG_MASK)
				*dump_reg = true;
			return true;
		}
	}

	return false;
}

static ssize_t sc8547_show_registers(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	unsigned char addr;
	unsigned char val;
	unsigned char tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8547");
	for (addr = 0x0; addr <= 0x3C; addr++) {
		if((addr < 0x24) || (addr > 0x2B && addr < 0x33)
		   || addr == 0x36 || addr == 0x3C) {
			ret = sc8547_read_byte(chip->client, addr, &val);
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

static ssize_t sc8547_store_register(struct device *dev,
                                     struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x3C)
		sc8547_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}


static DEVICE_ATTR(registers, 0660, sc8547_show_registers, sc8547_store_register);

static void sc8547_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}


static struct of_device_id sc8547_charger_match_table[] = {
	{
		.compatible = "master_vphy_sc8547",
	},
	{},
};

static struct oplus_voocphy_operations oplus_sc8547_ops = {
	.hardware_init		= sc8547_hardware_init,
	.hw_setting		= sc8547_hw_setting,
	.init_vooc		= sc8547_init_vooc,
	.set_predata		= sc8547_set_predata,
	.set_txbuff		= sc8547_set_txbuff,
	.get_adapter_info	= sc8547_get_adapter_info,
	.update_data		= sc8547_update_data,
	.get_chg_enable		= sc8547_get_chg_enable,
	.set_chg_enable		= sc8547_set_chg_enable,
	.reset_voocphy		= sc8547_reset_voocphy,
	.reactive_voocphy	= sc8547_reactive_voocphy,
	.send_handshake		= sc8547_send_handshake,
	.get_cp_vbat		= sc8547_get_cp_vbat,
	.get_cp_vbus		= sc8547_get_cp_vbus,
	.get_int_value		= sc8547_get_int_value,
	.get_adc_enable		= sc8547_get_adc_enable,
	.set_adc_enable		= sc8547_set_adc_enable,
	.get_ichg			= sc8547_get_cp_ichg,
	.set_pd_svooc_config = sc8547_set_pd_svooc_config,
	.get_pd_svooc_config = sc8547_get_pd_svooc_config,
	.get_voocphy_enable = sc8547_get_voocphy_enable,
	.dump_voocphy_reg	= sc8547_dump_reg_in_err_issue,
	.check_cp_int_happened = sc8547_check_cp_int_happened,
};

static int sc8547_charger_choose(struct oplus_voocphy_manager *chip)
{
	int ret;

	if (!oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip already exists!");
		return 0;
	} else {
		ret = i2c_smbus_read_byte_data(chip->client, 0x07);
		pr_err("0x07 = %d\n", ret);
		if (ret < 0) {
			pr_err("i2c communication fail");
			return -EPROBE_DEFER;
		}
		else
			return 1;
	}
}

static int sc8547_charger_probe(struct i2c_client *client,
                                const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	int ret;

	chg_info("sc8547_charger_probe enter\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);

	ret = sc8547_charger_choose(chip);
	if (ret <= 0)
		return ret;

	sc8547_create_device_node(&(client->dev));
	chip->ops = &oplus_sc8547_ops;
	ret = oplus_register_voocphy(chip);
	if (ret < 0) {
		chg_err("failed to register voocphy, ret = %d", ret);
		return ret;
	}
	chg_info("sc8547_charger_probe successfully!\n");

	return 0;
}

static void sc8547_charger_shutdown(struct i2c_client *client)
{
	sc8547_write_byte(client, SC8547_REG_11, 0x00);
	sc8547_write_byte(client, SC8547_REG_21, 0x00);
	pr_err("sc8547_charger_shutdown successfully!\n");

	return;
}

static const struct i2c_device_id sc8547_charger_id[] = {
	{"sc8547-master", 0},
	{},
};


static struct i2c_driver sc8547_charger_driver = {
	.driver		= {
		.name	= "sc8547-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sc8547_charger_match_table,
	},
	.id_table	= sc8547_charger_id,

	.probe		= sc8547_charger_probe,
	.shutdown	= sc8547_charger_shutdown,
};


int sc8547_subsys_init(void)
{
	int ret = 0;
	pr_err(" init start\n");

	if (i2c_add_driver(&sc8547_charger_driver) != 0) {
		pr_err(" failed to register sc8547 i2c driver.\n");
	} else {
		pr_err(" Success to register sc8547 i2c driver.\n");
	}

	return ret;
}

void sc8547_subsys_exit(void)
{
	i2c_del_driver(&sc8547_charger_driver);
}

oplus_chg_module_register(sc8547_subsys);

MODULE_DESCRIPTION("SC SC8547 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("lukaili");
