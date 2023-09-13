/*
 * SY6529 battery charging driver
*/
#define pr_fmt(fmt)	"[sy6529] %s: " fmt, __func__
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
#include "oplus_voocphy.h"
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "oplus_sy6529.h"


static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;


#define sy6529_DEVICE_ID		0x1F /* SY6529's I2C address is 0x1F */

#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200
#define IBUS_VBAT_LSB 15625 / 100000
#define IBUS_VBAT_LSB 15625 / 100000
#define VBUS_LSB 50 / 100
#define CHAR_BITS 8

static int __sy6529_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;
	return 0;
}

static int __sy6529_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
			   val, reg, ret);
		return ret;
	}
	return 0;
}

static int sy6529_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sy6529_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sy6529_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sy6529_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sy6529_update_bits(struct i2c_client *client, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sy6529_read_byte(client, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sy6529_write_byte(client, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sy6529_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret;

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		pr_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sy6529_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;

	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		pr_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);
	return 0;
}

static s32 sy6529_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;

	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	/*predata*/
	ret = sy6529_write_word(chip->client, sy6529_REG_31, val);
	if (ret < 0) {
		pr_err("failed: write predata\n");
		return -1;
	}
	pr_info("write predata 0x%0x\n", val);
	return ret;
}

static s32 sy6529_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;

	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	/*txbuff*/
	ret = sy6529_write_word(chip->client, sy6529_REG_2C, val);
	pr_info("sy6529_REG_2C = 0x%0x\n", val);
	if (ret < 0) {
		pr_err("failed: write txbuff\n");
		return -1;
	}

	return ret;
}



static s32 sy6529_get_adapter_request_info(struct oplus_voocphy_manager *chip)
{
	s32 data;

	if (!chip) {
		pr_err("chip is null\n");
		return -1;
	}

	data = sy6529_read_word(chip->client, sy6529_REG_2E);			/*	VOOC RDATA	& VOOC FLAG*/

	if (data < 0) {
		pr_err("sy6529_read_word faile\n");
		return -1;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);

	pr_info("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n", data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

#define UPDATE_DATA_BLOCK_LENGTH 8
static void sy6529_update_chg_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[UPDATE_DATA_BLOCK_LENGTH] = {0};
	int i = 0;
	u8 data = 0;
	u8 data_flag = 0;
	u8 data_tmp = 0;
	u8 value = 0;

	/*int_flag
	Reg0x0D:
	Bit7:VBAT_OVP
	Bit6:IBAT_OCP
	*/
	sy6529_read_byte(chip->client, sy6529_REG_0D, &data);
	data = data & (sy6529_VBAT_OVP_FLAG_MASK | sy6529_IBAT_OCP_FLAG_MASK);
	data_flag = data >> 1;

	/*
	Reg0x0B:
	Bit3:VBUS_OVP
	Bit2:IBUS_OCP
	Bit0:IBAT_UCP
	*/
	sy6529_read_byte(chip->client, sy6529_REG_0B, &data);
	data_tmp = data & (sy6529_VBUS_OVP_FLAG_MASK | sy6529_IBUS_OCP_FLAG_MASK);
	data_tmp = data_tmp << 1;
	data = (data & sy6529_IBUS_UCP_FALL_FLAG_MASK) << 2;
	data_flag |= data_tmp | data;

	/*
	Reg0x0F:
	Bit7:VBUS_insert
	Bit6:VBAT_insert
	*/
	sy6529_read_byte(chip->client, sy6529_REG_0F, &data);
	data = data & (sy6529_VBUS_INSERT_FLAG_MASK | sy6529_VBAT_INSERT_FLAG_MASK);
	data = data >> sy6529_VBAT_INSERT_FLAG_SHIFT;
	data_flag |= data;

	chip->int_flag = data_flag;

	/*parse data_block for improving time of interrupt*/
	/* Reg12-Reg13:VBUS_ADC
	   Reg14-Reg15:IBUS_ADC
	   Reg16-Reg17:VBAT_ADC
	   Reg18-Reg19:IBAT_ADC
	   Reg1A:TDIE_ADC
	 */
	i2c_smbus_read_i2c_block_data(chip->client, sy6529_REG_12, UPDATE_DATA_BLOCK_LENGTH, data_block);
	for (i = 0; i < UPDATE_DATA_BLOCK_LENGTH; i++) {
		pr_info("data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_ichg = ((data_block[2] << CHAR_BITS) | data_block[3]) * IBUS_VBAT_LSB;		/* SY6529's ibus adc:reg14,15 LSB=0.15625mA */
	chip->cp_vbus = ((data_block[0] << CHAR_BITS) | data_block[1]) * VBUS_LSB;			/* SY6529's Vbus adc:reg12,13 LSB=0.5mV */
	chip->cp_vbat = ((data_block[4] << CHAR_BITS) | data_block[5]) * IBUS_VBAT_LSB;		/* SY6529's VBAT adc:reg16,17 LSB=0.15625mV */

	chip->cp_vsys = oplus_gauge_get_batt_mvolts();

	pr_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d int_flag = %d",
		chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->int_flag);

	sy6529_read_byte(chip->client, sy6529_REG_00, &value);
	pr_info("sy6529_REG_00 = 0x%0x\n", value);


}


static int sy6529_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 value;
	u8 tmp;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	sy6529_read_byte(chip->client, sy6529_REG_00, &tmp);
	pr_info("start chg sy6529_REG_00 0x%0x\n", tmp);

	sy6529_read_byte(chip->client, sy6529_REG_02, &value);
	pr_info("enable chg sy6529_REG_02 0x%0x\n", value);

	sy6529_read_byte(chip->client, sy6529_REG_02, &value);
	pr_info("enable chg sy6529_REG_02 0x%0x\n", value);

	if (enable) {
		if (chip->adapter_type == ADAPTER_VOOC20||chip->adapter_type == ADAPTER_VOOC30) {  /*mode:1:1*/
			tmp = tmp | (sy6529_CHARGE_MODE_1_1 << sy6529_CHARGE_MODE_SHIFT);
		} else {                 															/*mode:2:1*/
			tmp = tmp | (sy6529_CHARGE_MODE_2_1 << sy6529_CHARGE_MODE_SHIFT);
		}
		ret = sy6529_write_byte(chip->client, sy6529_REG_00, tmp);
		sy6529_read_byte(chip->client, sy6529_REG_00, &value);
		pr_info("enable chg sy6529_REG_00 0x%0x\n", value);
		return ret;
	} else {
		ret = sy6529_write_byte(chip->client, sy6529_REG_00, tmp);
		sy6529_read_byte(chip->client, sy6529_REG_00, &value);
		pr_info("chg sy6529_REG_00 0x%0x\n", value);
		return ret;
	}
}

static int sy6529_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = {0};

	i2c_smbus_read_i2c_block_data(chip->client, sy6529_REG_16, 2, data_block);

	chip->cp_vbat = ((data_block[0] << CHAR_BITS) | data_block[1]) * IBUS_VBAT_LSB;		/* SY6529's VBAT adc:reg16,17 LSB=0.15625mV*/
	pr_info("cp_vbat = %d\n", chip->cp_vbat);
	return chip->cp_vbat;
}

static int sy6529_direct_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	/*  modification by silergy: SY6529's mode control bit:	reg00 bit[6:4]*/

	ret = sy6529_read_byte(chip->client, sy6529_REG_00, data);
	if (ret < 0) {
		pr_err("sy6529_REG_00\n");
		return -1;
	}

	*data = *data & sy6529_CHARGE_MODE_MASK; /*SY6529 reg0x00:Bit[6:4]=001, 1_1 mode;  Bit[6:4]=010,2_1 mode*/
	*data = *data >> sy6529_CHARGE_MODE_SHIFT;

	if (*data == sy6529_CHARGE_MODE_2_1 | *data == sy6529_CHARGE_MODE_1_1) /* SY6529 is working in 2_1 mode or 1_1 mode, return 1*/
		*data=1;
	else
		*data=0;	/* SY6529 not working, return 0*/

	pr_info("data = %d\n", *data);
	return ret;
}

static int sy6529_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = {0};
	int cp_ichg = 0;
	u8 cp_enable = 0;

	sy6529_direct_chg_enable(chip, &cp_enable);

	if (cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	i2c_smbus_read_i2c_block_data(chip->client, sy6529_REG_14, 2, data_block);
	chip->cp_ichg = ((data_block[0] << CHAR_BITS) | data_block[1]) * IBUS_VBAT_LSB;			/* SY6529's ibus adc:reg14,15 LSB=0.15625mA*/
	pr_info("cp_ichg = %d\n", chip->cp_ichg);
	return cp_ichg;
}

static u8 sy6529_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;
	u8 data_flag = 0;
	u8 data_tmp = 0;

	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -1;
	}

	/*
	Reg0x0D:
	Bit7:VBAT_OVP
	Bit6:IBAT_OVP
	*/
	ret = sy6529_read_byte(chip->client, sy6529_REG_0D, &data);
	if (ret < 0) {
		pr_err(" read sy6529_REG_0D failed\n");
		return -1;
	}
	data = data & (sy6529_VBAT_OVP_FLAG_MASK | sy6529_IBAT_OCP_FLAG_MASK);
	data_flag = data >> 1;

	/*
	Reg0x0B:
	Bit3:VBUS_OVP
	Bit2:IBUS_OCP
	Bit0:IBAT_UCP
	*/
	ret = sy6529_read_byte(chip->client, sy6529_REG_0B, &data);
	if (ret < 0) {
		pr_err(" read sy6529_REG_0B failed\n");
		return -1;
	}
	data_tmp = data & (sy6529_VBUS_OVP_FLAG_MASK | sy6529_IBUS_OCP_FLAG_MASK);
	data_tmp = data_tmp << 1;
	data = (data & sy6529_IBUS_UCP_FALL_FLAG_MASK) << 2;
	data_flag |= data_tmp | data;

	/*
	Reg0x0F:
	Bit7:VBUS_insert
	Bit6:VBAT_insert
	*/
	ret = sy6529_read_byte(chip->client, sy6529_REG_0F, &data);
	if (ret < 0) {
		pr_err(" read sy6529_REG_0F failed\n");
		return -1;
	}
	data = data & (sy6529_VBUS_INSERT_FLAG_MASK | sy6529_VBAT_INSERT_FLAG_MASK);
	data = data >> sy6529_VBAT_INSERT_FLAG_SHIFT;
	data_flag |= data;

	return data_flag;

}

static int sy6529_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sy6529_read_byte(chip->client, sy6529_REG_11, data);
	if (ret < 0) {
		pr_err("sy6529_REG_11\n");
		return -1;
	}

	*data = *data >> sy6529_ADC_EN_SHIFT;
	pr_info("data = %d", *data);
	return ret;
}

static int sy6529_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable) {
		pr_info("enable adc sy6529_REG_11 0x80\n");
		return sy6529_write_byte(chip->client, sy6529_REG_11, 0x80);
	} else {
		pr_info("adc sy6529_REG_11 0x00\n");
		return sy6529_write_byte(chip->client, sy6529_REG_11, 0x00);
	}
}

static void sy6529_set_switch_fast_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sy6529_set_switch_fast_charger chip null\n");
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_high);
	gpio_direction_output(chip->switch1_gpio, 1);	/* out 1*/

	pr_err("switch switch2 %d to fast finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sy6529_set_switch_normal_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sy6529_set_switch_normal_charger chip null\n");
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);
	if (chip->switch1_gpio > 0) {
		gpio_direction_output(chip->switch1_gpio, 0);	/* in 0*/
	}

	pr_err("switch switch2 %d to normal finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sy6529_set_switch_mode(struct oplus_voocphy_manager *chip, int mode)
{
	switch (mode) {
	case VOOC_CHARGER_MODE:
		sy6529_set_switch_fast_charger(chip);
		break;
	case NORMAL_CHARGER_MODE:
	default:
		sy6529_set_switch_normal_charger(chip);
		break;
	}

	return;
}

static void sy6529_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	u8 reg07_data = 0;
	if (!chip) {
		pr_err("Failed\n");
		return;
	}


	if (enable) {
		sy6529_write_byte(chip->client, sy6529_REG_07, 0x3F);	/*SY6529: IBUS_UCP:disable*/
		sy6529_write_byte(chip->client, sy6529_REG_4D, 0x80);	/*SY6529: bus_ucp 40ms*/
		sy6529_write_byte(chip->client, sy6529_REG_0C, 0x02);	/*IBUS_UCP_RISE_MASK*/
		sy6529_write_byte(chip->client, sy6529_REG_00, 0x02);	/*SY6529: WD:1000ms*/
	} else {
		sy6529_write_byte(chip->client, sy6529_REG_07, 0xBF);	/*SY6529: IBUS_OCP:3.6A*/
	}

	ret = sy6529_read_byte(chip->client, sy6529_REG_07, &reg07_data);
	if (ret < 0) {
		pr_err("sy6529_REG_07\n");
		return;
	}
	pr_err("pd_svooc config sy6529_REG_07 = %d\n", reg07_data);
}

static bool sy6529_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;

	if (!chip) {
		pr_err("Failed\n");
		return false;
	}

	ret = sy6529_read_byte(chip->client, sy6529_REG_07, &data);
	if (ret < 0) {
		pr_err("sy6529_REG_07\n");
		return false;
	}

	pr_err("sy6529_REG_07 = 0x%0x\n", data);

	data = data >> sy6529_IBUS_UCP_DIS_SHIFT;
	if (data == 1)
		return true;
	else
		return false;
}

static int sy6529_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = sy6529_RESET_REG;
	else
		val = sy6529_NO_REG_RESET;

	val <<= sy6529_REG_RESET_SHIFT;

	ret = sy6529_update_bits(chip->client, sy6529_REG_00,
				sy6529_REG_RESET_MASK, val);

	return ret;
}

static int oplus_vooc_hardware_monitor_stop(void)
{
	/*lkl need modify*/
	int status = VOOCPHY_SUCCESS;

	return status;
}

static int oplus_vooc_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	/* stop hardware monitor */
	status = oplus_vooc_hardware_monitor_stop();
	if (status != VOOCPHY_SUCCESS)
		return VOOCPHY_EFAILED;


	/*turn off mos*/
	sy6529_write_byte(chip->client, sy6529_REG_00, 0x8B);	/* SY6529: Charge disable, RST=1, watchdog disable */
	sy6529_write_byte(chip->client, sy6529_REG_00, 0x0B);	/* SY6529: Charge disable, watchdog disable */

	/*hwic config with plugout*/
	sy6529_write_byte(chip->client, sy6529_REG_08, 0xAE);	/* SY6529: set vbat_ovp=4.65V */
	sy6529_write_byte(chip->client, sy6529_REG_04, 0x13);	/* SY6529: set vac_ovp=7V */
	sy6529_write_byte(chip->client, sy6529_REG_06, 0x94);	/* SY6529: set VBUS_ovp=6V */
	sy6529_write_byte(chip->client, sy6529_REG_07, 0xBF);	/* SY6529: set UCP_TH:150/300mA  BUS OCP=3.6A(Max) */
	sy6529_write_byte(chip->client, sy6529_REG_02, 0xB0);	/* SY6529: VBUS_LOW_ERR:1.01 */
	sy6529_write_byte(chip->client, sy6529_REG_4D, 0x80);	/* SY6529: bus_ucp 40ms */
	sy6529_write_byte(chip->client, sy6529_REG_11, 0x00);	/* SY6529: reset adc */
	sy6529_write_byte(chip->client, sy6529_REG_10, 0x80);	/* SY6529: disable VBUS INSERT */

	/*clear tx data*/
	sy6529_write_byte(chip->client, sy6529_REG_2C, 0x00);
	sy6529_write_byte(chip->client, sy6529_REG_2D, 0x00);

	/*disable vooc phy irq*/
	sy6529_write_byte(chip->client, sy6529_REG_30, 0xFF);

	/*select big bang mode*/
	/*disable vooc*/
	sy6529_write_byte(chip->client, sy6529_REG_2B, 0x00);
	/*set predata*/
	sy6529_write_byte(chip->client, sy6529_REG_31, 0x0);

	pr_info ("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}

void oplus_vooc_send_handshake_seq(struct oplus_voocphy_manager *chip)
{
	u8 value;

	sy6529_write_byte(chip->client, sy6529_REG_2B, 0x81);
	pr_info("sy6529_REG_2B = 0x81");
	sy6529_read_byte(chip->client, sy6529_REG_2B, &value);
	pr_info("read sy6529_REG_2B = %d", value);
}

static int oplus_vooc_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 value;

	/*to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us*/
	/*set predata*/
	sy6529_write_byte(chip->client, sy6529_REG_31, 0x0);
	sy6529_read_byte(chip->client, sy6529_REG_3A, &value);
	value = value | (3 << 5);
	sy6529_write_byte(chip->client, sy6529_REG_3A, value);
	sy6529_write_byte(chip->client, sy6529_REG_33, 0xD1);			/* losse_det EN*/

	/*clear tx data*/
	sy6529_write_byte(chip->client, sy6529_REG_2C, 0x00);
	sy6529_write_byte(chip->client, sy6529_REG_2D, 0x00);

	/*vooc*/
	sy6529_write_byte(chip->client, sy6529_REG_30, 0x05);			/* MASK irq RX_START; TX_DONE*/
	oplus_vooc_send_handshake_seq(chip);

	pr_info ("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

/*
 * interrupt does nothing, just info event chagne, other module could get info
 * through power supply interface
 */

static irqreturn_t sy6529_charger_interrupt(int irq, void *dev_id)
{
	/*do irq things*/
	struct oplus_voocphy_manager *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	return oplus_voocphy_interrupt_handler(chip);
}

static int sy6529_init_device(struct oplus_voocphy_manager *chip)
{
	sy6529_write_byte(chip->client, sy6529_REG_00, 0x08);	/* SY6529: disable WD */
	sy6529_write_byte(chip->client, sy6529_REG_11, 0x0);	/* SY6529: ADC_CTRL:disable */
	sy6529_write_byte(chip->client, sy6529_REG_04, 0x13);	/* SY6529: vac_ovp =7V */
	sy6529_write_byte(chip->client, sy6529_REG_06, 0x94);	/* SY6529: VBUS_OVP:6V */
	sy6529_write_byte(chip->client, sy6529_REG_08, 0xAE);	/* SY6529: VBAT_OVP:4.65V */
	sy6529_write_byte(chip->client, sy6529_REG_07, 0xBF);	/* SY6529: IBUS_OCP:3.6A */
	sy6529_write_byte(chip->client, sy6529_REG_02, 0xB0);	/* SY6529: VBUS_LOW_ERR:1.01 */
	sy6529_write_byte(chip->client, sy6529_REG_4D, 0x80);	/* SY6529: bus_ucp:40ms */

	sy6529_write_byte(chip->client, sy6529_REG_09, 0x34);	/* SY6529: IBAT OCP DIS */
	sy6529_write_byte(chip->client, sy6529_REG_2B, 0x00);	/* SY6529: VOOC_CTRL:disable */
	sy6529_write_byte(chip->client, sy6529_REG_05, 0x35);	/* SY6529: Vdrop = 300mV */
	sy6529_write_byte(chip->client, sy6529_REG_0A, 0x00);	/* SY6529: disable REGULATION */
	sy6529_write_byte(chip->client, sy6529_REG_10, 0x80);	/* SY6529: disable VBUS INSERT */
	pr_info("sy6529_init_device done");
	return 0;
}

int sy6529_init_vooc(struct oplus_voocphy_manager *chip)
{
	u8 value;

	pr_err(" >>>>start init vooc\n");

	sy6529_reg_reset(chip, true);
	sy6529_init_device(chip);

	/*to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us*/
	sy6529_write_word(chip->client, sy6529_REG_31, 0x0);
	sy6529_read_byte(chip->client, sy6529_REG_3A, &value);

	pr_info("value = %d\n", value);

	value = value | (1 << 6);

	sy6529_write_byte(chip->client, sy6529_REG_3A, value);
	pr_err("read value %d\n", value);


	sy6529_write_byte(chip->client, sy6529_REG_33, 0xD1);

	/*vooc*/
	sy6529_write_byte(chip->client, sy6529_REG_30, 0x25);

	return 0;
}

static int sy6529_gpio_init(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
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

	printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip is ready!\n", __func__);
	return 0;
}

static int oplus_chg_sw_ctrl2_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node * node = NULL;

	if (!chip) {
		pr_debug("chip null\n");
		return -1;
	}

	/* Parsing gpio switch gpio47*/
	node = chip->dev->of_node;
	chip->switch1_gpio = of_get_named_gpio(node,
										   "qcom,charging_switch1-gpio", 0);
	if (chip->switch1_gpio < 0) {
		pr_debug("chip->switch1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->switch1_gpio)) {
			rc = gpio_request(chip->switch1_gpio,
							  "charging-switch1-gpio");
			if (rc) {
				pr_debug("unable to request gpio [%d]\n",
						 chip->switch1_gpio);
			} else {
				rc = sy6529_gpio_init(chip);
				if (rc)
					chg_err("unable to init charging_sw_ctrl2-gpio:%d\n",
							chip->switch1_gpio);
			}
		}
		pr_debug("chip->switch1_gpio =%d\n", chip->switch1_gpio);
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

	return 0;

}

static int sy6529_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->irq_gpio = of_get_named_gpio(node,
									   "qcom,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		pr_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			rc = gpio_request(chip->irq_gpio,
							  "irq_gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n",
						 chip->irq_gpio);
			}
		}
		pr_err("chip->irq_gpio =%d\n", chip->irq_gpio);
	}
	/*irq_num*/
	chip->irq = gpio_to_irq(chip->irq_gpio);
	pr_err("irq way1 chip->irq =%d\n",chip->irq);

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

	/*irq active*/
	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl, chip->charging_inter_active); /* no_PULL */

	rc = gpio_get_value(chip->irq_gpio);
	pr_err("irq chip->irq_gpio input =%d irq_gpio_stat = %d\n", chip->irq_gpio, rc);

	return 0;
}

static int sy6529_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret = 0;

	sy6529_irq_gpio_init(chip);
	pr_err("sy6529 chip->irq = %d\n", chip->irq);
	if (chip->irq) {
		ret = request_threaded_irq(chip->irq, NULL,
								   sy6529_charger_interrupt,
								   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
								   "sy6529_charger_irq", chip);
		if (ret < 0) {
			pr_debug("request irq for irq=%d failed, ret =%d\n",
							chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
	}
	pr_debug("request irq ok\n");

	return ret;
}

static int sy6529_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sy6529_write_byte(chip->client, sy6529_REG_04, 0x18);	/*SY6529: VAC_OVP:12v*/
	sy6529_write_byte(chip->client, sy6529_REG_06, 0xBC);	/*SY6529: VBUS_OVP:10V*/
	sy6529_write_byte(chip->client, sy6529_REG_07, 0xBF);	/*SY6529: IBUS_OCP:3.6A*/
	sy6529_write_byte(chip->client, sy6529_REG_02, 0xB0);	/*SY6529: VBUS_LOW_ERR:1.01*/

	sy6529_write_byte(chip->client, sy6529_REG_00, 0x02);	/*SY6529: WD:1000ms*/
	sy6529_write_byte(chip->client, sy6529_REG_11, 0x80);	/*SY6529: ADC_CTRL:ADC_EN*/

	sy6529_write_byte(chip->client, sy6529_REG_33, 0xD1);	/*SY6529: Loose_det=1*/

	pr_info("sy6529_svooc_hw_setting done");
	return 0;
}

static int sy6529_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sy6529_write_byte(chip->client, sy6529_REG_04, 0x13);	/*SY6529: VAC_OVP:7V*/
	sy6529_write_byte(chip->client, sy6529_REG_06, 0x94);	/*SY6529: VBUS_OVP:6V*/
	sy6529_write_byte(chip->client, sy6529_REG_07, 0xBF);	/*SY6529: IBUS_OCP:3.6A(max)*/
	sy6529_write_byte(chip->client, sy6529_REG_02, 0xB0);	/*SY6529: VBUS_LOW_ERR:1.01*/

	sy6529_write_byte(chip->client, sy6529_REG_00, 0x04);	/*SY6529: WD:5000ms*/
	sy6529_write_byte(chip->client, sy6529_REG_11, 0x80);	/*SY6529: ADC_CTRL:*/

	sy6529_write_byte(chip->client, sy6529_REG_33, 0xD1);	/*SY6529: Loose_det*/

	pr_info("sy6529_vooc_hw_setting done");
	return 0;
}

static int sy6529_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{

	sy6529_write_byte(chip->client, sy6529_REG_04, 0x13);	/*SY6529: VAC_OVP:12V*/
	sy6529_write_byte(chip->client, sy6529_REG_06, 0x94);	/*SY6529: VBUS_OVP:11.5V*/

	sy6529_write_byte(chip->client, sy6529_REG_01, 0x60);	/*SY6529: CP Frequency 375kHz*/
	sy6529_write_byte(chip->client, sy6529_REG_00, 0x0B);	/*SY6529: WD: disable*/

	sy6529_write_byte(chip->client, sy6529_REG_11, 0x00);	/*SY6529: ADC_CTRL:*/
	sy6529_write_byte(chip->client, sy6529_REG_2B, 0x00);	/*SY6529: VOOC_CTRL*/

	pr_info("sy6529_5v2a_hw_setting done");
	return 0;
}

static int sy6529_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sy6529_write_byte(chip->client, sy6529_REG_04, 0x18);	/*VAC_OVP:	 12V*/
	sy6529_write_byte(chip->client, sy6529_REG_06, 0xBC);	/*VBUS_OVP:  11.5V*/
	sy6529_write_byte(chip->client, sy6529_REG_01, 0x80);
	sy6529_write_byte(chip->client, sy6529_REG_00, 0x0B);	/*WD:*/
	sy6529_write_byte(chip->client, sy6529_REG_11, 0x00);	/*ADC_CTRL:*/
	sy6529_write_byte(chip->client, sy6529_REG_2B, 0x00);	/*VOOC_CTRL*/
	pr_info("sy6529_pdqc_hw_setting");
	return 0;
}

static int sy6529_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		pr_err("chip is null exit\n");
		return -1;
	}

	switch (reason) {
		case SETTING_REASON_PROBE:
		case SETTING_REASON_RESET:
			sy6529_init_device(chip);
			pr_info("SETTING_REASON_RESET OR PROBE\n");
			break;
		case SETTING_REASON_SVOOC:
			sy6529_svooc_hw_setting(chip);
			pr_info("SETTING_REASON_SVOOC\n");
			break;
		case SETTING_REASON_VOOC:
			sy6529_vooc_hw_setting(chip);
			pr_info("SETTING_REASON_VOOC\n");
			break;
		case SETTING_REASON_5V2A:
			sy6529_5v2a_hw_setting(chip);
			pr_info("SETTING_REASON_5V2A\n");
			break;
		case SETTING_REASON_PDQC:
			sy6529_pdqc_hw_setting(chip);
			pr_info("SETTING_REASON_PDQC\n");
			break;
		default:
			pr_err("do nothing\n");
			break;
	}
	return 0;
}

static ssize_t sy6529_show_registers(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sy6529");

	/************ sy6529 registers ************/
	for (addr = sy6529_REG_00; addr <= sy6529_REG_4D; addr++) {
		if ((addr <= sy6529_REG_1A) || (addr >= sy6529_REG_2B && addr <= sy6529_REG_33) 		/* show SY6529 all registers*/
			|| addr == sy6529_REG_36 || addr == sy6529_REG_3A || addr == sy6529_REG_4D) {
			ret = sy6529_read_byte(chip->client, addr, &val);
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

static ssize_t sy6529_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= sy6529_REG_4D)
		sy6529_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sy6529_show_registers, sy6529_store_register);	/* SY6529 device ID is 0x1F*/
static void sy6529_create_device_node(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_registers);
	if (ret)
		chg_err("create_device_node fail\n");
}


static struct of_device_id sy6529_charger_match_table[] = {
	{
		.compatible = "sy,sy6529-master",
	},
	{},
};

static struct oplus_voocphy_operations oplus_sy6529_ops = {
	.hw_setting 	= sy6529_hw_setting,
	.init_vooc		= sy6529_init_vooc,
	.set_predata		= sy6529_set_predata,
	.set_txbuff 	= sy6529_set_txbuff,
	.get_adapter_info	= sy6529_get_adapter_request_info,
	.update_data		= sy6529_update_chg_data,
	.get_chg_enable 	= sy6529_direct_chg_enable,
	.set_chg_enable 	= sy6529_set_chg_enable,
	.reset_voocphy		= oplus_vooc_reset_voocphy,
	.reactive_voocphy	= oplus_vooc_reactive_voocphy,
	.set_switch_mode	= sy6529_set_switch_mode,
	.send_handshake 	= oplus_vooc_send_handshake_seq,
	.get_cp_vbat		= sy6529_get_cp_vbat,
	.get_int_value		= sy6529_get_int_value,
	.get_adc_enable 	= sy6529_get_adc_enable,
	.set_adc_enable 	= sy6529_set_adc_enable,
	.get_ichg			= sy6529_get_cp_ichg,
	.set_pd_svooc_config = sy6529_set_pd_svooc_config,
	.get_pd_svooc_config = sy6529_get_pd_svooc_config,
};

static int sy6529_charger_choose(struct oplus_voocphy_manager *chip)
{
	int ret;

	if (!oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip already exists!");
		return 0;
	} else {
		ret = i2c_smbus_read_byte_data(chip->client, 0x00);
		pr_err("0x00 = %d\n", ret);
		if (ret < 0) {
			pr_err("i2c communication fail");
			return -EPROBE_DEFER;
		}
		else
			return 1;
	}
}

static int sy6529_charger_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	int ret;

	pr_err("sy6529_charger_probe enter!\n");
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip){
		dev_err(&client->dev, "couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->dev = &client->dev;
	chip->client = client;

	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);

	ret = sy6529_charger_choose(chip);
	if (ret <= 0)
		return ret;

	sy6529_create_device_node(&(client->dev));
	ret = oplus_chg_sw_ctrl2_parse_dt(chip);

	sy6529_reg_reset(chip, true);

	sy6529_init_device(chip);

	ret = sy6529_irq_register(chip);
	if (ret < 0)
		goto err_1;

	chip->ops = &oplus_sy6529_ops;

	/*request oplus_vooc_mg memery*/
	oplus_voocphy_init(chip);

	oplus_voocphy_mg = chip;

	init_proc_voocphy_debug();

	pr_err("sy6529 sy6529_parse_dt successfully!\n");

	return 0;

err_1:
	pr_err("sy6529 probe err_1 \n");
	return ret;
}

static void sy6529_charger_shutdown(struct i2c_client *client)
{
	sy6529_write_byte(client, sy6529_REG_11, 0x00);

	return;
}

static const struct i2c_device_id sy6529_charger_id[] = {
	{"sy6529-master", 0},
	{},
};

static struct i2c_driver sy6529_charger_driver = {
	.driver 	= {
		.name	= "sy6529-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sy6529_charger_match_table,
	},
	.id_table	= sy6529_charger_id,

	.probe		= sy6529_charger_probe,
	.shutdown	= sy6529_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init sy6529_subsys_init(void)
{
	int ret = 0;

	chg_debug(" init start\n");

	if (i2c_add_driver(&sy6529_charger_driver) != 0) {
		chg_err(" failed to register sy6529 i2c driver.\n");
	} else {
		chg_debug(" Success to register sy6529 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(sy6529_subsys_init);
#else
int sy6529_subsys_init(void)
{
	int ret = 0;

	chg_debug(" init start\n");

	if (i2c_add_driver(&sy6529_charger_driver) != 0) {
		chg_err(" failed to register sy6529 i2c driver.\n");
	} else {
		chg_debug(" Success to register sy6529 i2c driver.\n");
	}

	return ret;
}

void sy6529_subsys_exit(void)
{
	i2c_del_driver(&sy6529_charger_driver);
}
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

/*module_i2c_driver(sy6529_charger_driver);*/

MODULE_DESCRIPTION("SC sy6529 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
