// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */


#define pr_fmt(fmt)	"[bq2589x]:%s: " fmt, __func__

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
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/power_supply.h>
#include <mt-plat/upmu_common.h>
#include <mt-plat/mtk_boot.h>

#include "../../mediatek/charger/mtk_charger_intf.h"
#include "../oplus_charger.h"
#define _BQ25890H_
#include "oplus_bq2589x_reg.h"
#include <linux/time.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <soc/oplus/oplus_project.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/charger_type.h>
#else
#include <soc/oplus/system/oplus_project.h>
#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/charger_type.h>
#endif

extern void set_charger_ic(int sel);
extern unsigned int is_project(int project);

/*charger current limit*/
#define BQ_CHARGER_CURRENT_MAX_MA		3400
/*input current limit*/
#define BQ_INPUT_CURRENT_MAX_MA 		2000
#define BQ_INPUT_CURRENT_COLD_TEMP_MA		1000
#define BQ_INPUT_CURRENT_NORMAL_TEMP_MA 	2000
#define BQ_INPUT_CURRENT_WARM_TEMP_MA		1800
#define BQ_INPUT_CURRENT_WARM_TEMP_HVDCP_MA	1500
#define BQ_INPUT_CURRENT_HOT_TEMP_MA		1500
#define BQ_INPUT_CURRENT_HOT_TEMP_HVDCP_MA	1200
#define BQ_INPUT_CURRENT_HOT_5V_MA	        1800

#define BQ_COLD_TEMPERATURE_DECIDEGC	0
#define BQ_WARM_TEMPERATURE_DECIDEGC	340
#define BQ_HOT_TEMPERATURE_DECIDEGC	370
#define BQ_HOT_TEMP_TO_5V	420

#define BQ2589X_DEVICE_CONFIGURATION 3

enum {
	PN_BQ25890H,
	PN_BQ25892,
	PN_BQ25895,
};

static int pn_data[] = {
	[PN_BQ25890H] = 0x03,
	[PN_BQ25892] = 0x00,
	[PN_BQ25895] = 0x07,
};

static char *pn_str[] = {
	[PN_BQ25890H] = "bq25890H",
	[PN_BQ25892] = "bq25892",
	[PN_BQ25895] = "bq25895",
};

enum hvdcp_type {
	HVDCP_5V,
	HVDCP_9V,
	HVDCP_12V,
	HVDCP_20V,
	HVDCP_CONTINOUS,
	HVDCP_DPF_DMF,
};

struct chg_para{
	int vlim;
	int ilim;

	int vreg;
	int ichg;
};

struct bq2589x_platform_data {
	int iprechg;
	int iterm;

	int boostv;
	int boosti;

	struct chg_para usb;
};


struct bq2589x {
	struct device *dev;
	struct i2c_client *client;
	struct delayed_work bq2589x_aicr_setting_work;
	struct delayed_work bq2589x_retry_adapter_detection;
	struct delayed_work bq2589x_current_setting_work;

	int part_no;
	int revision;

	const char *chg_dev_name;
	const char *eint_name;

	bool chg_det_enable;
	bool otg_enable;

	enum charger_type chg_type;
	enum power_supply_type oplus_chg_type;
	
	int status;
	int irq;

	struct mutex i2c_rw_lock;

	bool charge_enabled;	/* Register bit status */
	bool power_good;

	struct bq2589x_platform_data *platform_data;
	struct charger_device *chg_dev;
	struct timespec ptime[2];

	struct power_supply *psy;
	struct charger_consumer *chg_consumer;
	bool disable_hight_vbus;
	bool pdqc_setup_5v;
	bool hvdcp_can_enabled;
	int pre_current_ma;
	int aicr;
	int chg_cur;
	int vbus_type;
	bool hvdcp_checked;
	int hw_aicl_point;
	bool retry_hvdcp_algo;
	bool nonstand_retry_bc;
	bool camera_on;
	bool calling_on;
	bool is_bq2589x;
};

static bool disable_PE = 0;
static bool disable_QC = 0;
static bool disable_PD = 0;
static bool dumpreg_by_irq = 0;
static int  current_percent = 70;
module_param(disable_PE, bool, 0644);
module_param(disable_QC, bool, 0644);
module_param(disable_PD, bool, 0644);
module_param(current_percent, int, 0644);
module_param(dumpreg_by_irq, bool, 0644);
static struct bq2589x *g_bq;

void oplus_wake_up_usbtemp_thread(void);

extern struct oplus_chg_chip *g_oplus_chip;

extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);
void oplus_bq2589x_set_mivr_by_battery_vol(void);
static void bq2589x_dump_regs(struct bq2589x *bq);

static const struct charger_properties bq2589x_chg_props = {
	.alias_name = "bq2589x",
};

static int g_bq2589x_read_reg(struct bq2589x *bq, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(bq->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int g_bq2589x_write_reg(struct bq2589x *bq, int reg, u8 val)
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

static int bq2589x_read_byte(struct bq2589x *bq, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = g_bq2589x_read_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static int bq2589x_write_byte(struct bq2589x *bq, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = g_bq2589x_write_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

static int bq2589x_update_bits(struct bq2589x *bq, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&bq->i2c_rw_lock);
	ret = g_bq2589x_read_reg(bq, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = g_bq2589x_write_reg(bq, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}

static int bq2589x_enable_otg(struct bq2589x *bq)
{

	u8 val = BQ2589X_OTG_ENABLE << BQ2589X_OTG_CONFIG_SHIFT;

	bq2589x_update_bits(bq, BQ2589X_REG_03,
				   BQ2589X_OTG_CONFIG_MASK, val);

	if(g_oplus_chip->is_double_charger_support) {
		msleep(50);
		g_oplus_chip->sub_chg_ops->charger_suspend();
	}

	return 0;
}

static int bq2589x_disable_otg(struct bq2589x *bq)
{
	u8 val = BQ2589X_OTG_DISABLE << BQ2589X_OTG_CONFIG_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_03,
				   BQ2589X_OTG_CONFIG_MASK, val);
}

static int bq2589x_enable_hvdcp(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_HVDCP_ENABLE << BQ2589X_HVDCPEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 
				BQ2589X_HVDCPEN_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_enable_hvdcp);

static int bq2589x_disable_hvdcp(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_HVDCP_DISABLE << BQ2589X_HVDCPEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 
				BQ2589X_HVDCPEN_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_disable_hvdcp);

static int bq2589x_disable_maxc(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_MAXC_DISABLE << BQ2589X_MAXCEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 
				BQ2589X_MAXCEN_MASK, val);
	return ret;
}

static int bq2589x_disable_batfet_rst(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_BATFET_RST_EN_DISABLE << BQ2589X_BATFET_RST_EN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_09, 
				BQ2589X_BATFET_RST_EN_MASK, val);
	return ret;
}

static int bq2589x_disable_ico(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_ICO_DISABLE << BQ2589X_ICOEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 
				BQ2589X_ICOEN_MASK, val);
	return ret;
}
static int bq2589x_enable_charger(struct bq2589x *bq)
{
	int ret;

	u8 val = BQ2589X_CHG_ENABLE << BQ2589X_CHG_CONFIG_SHIFT;

	dev_info(bq->dev, "%s\n", __func__);
	ret = bq2589x_update_bits(bq, BQ2589X_REG_03, 
				BQ2589X_CHG_CONFIG_MASK, val);

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		if(g_bq->oplus_chg_type != POWER_SUPPLY_TYPE_USB) {
			g_oplus_chip->sub_chg_ops->charging_enable();
		} else {
			g_oplus_chip->sub_chg_ops->charging_disable();
		}
	}

	return ret;
}

static int bq2589x_disable_charger(struct bq2589x *bq)
{
	int ret;

	u8 val = BQ2589X_CHG_DISABLE << BQ2589X_CHG_CONFIG_SHIFT;
	
	dev_info(bq->dev, "%s\n", __func__);
	ret = bq2589x_update_bits(bq, BQ2589X_REG_03, 
				BQ2589X_CHG_CONFIG_MASK, val);

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
    		g_oplus_chip->sub_chg_ops->charging_disable();
    }

	return ret;
}

int bq2589x_adc_start(struct bq2589x *bq, bool oneshot)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_02, &val);
	if (ret < 0) {
		dev_err(bq->dev, "%s failed to read register 0x02:%d\n", __func__, ret);
		return ret;
	}

	if (((val & BQ2589X_CONV_RATE_MASK) >> BQ2589X_CONV_RATE_SHIFT) == BQ2589X_ADC_CONTINUE_ENABLE)
		return 0; /*is doing continuous scan*/
	if (oneshot)
		ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_START_MASK,
					BQ2589X_CONV_START << BQ2589X_CONV_START_SHIFT);
	else
		ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_RATE_MASK,  
					BQ2589X_ADC_CONTINUE_ENABLE << BQ2589X_CONV_RATE_SHIFT);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_adc_start);

int bq2589x_adc_stop(struct bq2589x *bq)
{
	return bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_RATE_MASK, 
				BQ2589X_ADC_CONTINUE_DISABLE << BQ2589X_CONV_RATE_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_adc_stop);


int bq2589x_adc_read_battery_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_0E, &val);
	if (ret < 0) {
		dev_err(bq->dev, "read battery voltage failed :%d\n", ret);
		return ret;
	} else{
		volt = BQ2589X_BATV_BASE + ((val & BQ2589X_BATV_MASK) >> BQ2589X_BATV_SHIFT) * BQ2589X_BATV_LSB ;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_battery_volt);


int bq2589x_adc_read_sys_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_0F, &val);
	if (ret < 0) {
		dev_err(bq->dev, "read system voltage failed :%d\n", ret);
		return ret;
	} else{
		volt = BQ2589X_SYSV_BASE + ((val & BQ2589X_SYSV_MASK) >> BQ2589X_SYSV_SHIFT) * BQ2589X_SYSV_LSB ;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_sys_volt);

int bq2589x_adc_read_vbus_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_11, &val);
	if (ret < 0) {
		dev_err(bq->dev, "read vbus voltage failed :%d\n", ret);
		return ret;
	} else{
		volt = BQ2589X_VBUSV_BASE + ((val & BQ2589X_VBUSV_MASK) >> BQ2589X_VBUSV_SHIFT) * BQ2589X_VBUSV_LSB ;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_vbus_volt);

int bq2589x_adc_read_temperature(struct bq2589x *bq)
{
	uint8_t val;
	int temp;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_10, &val);
	if (ret < 0) {
		dev_err(bq->dev, "read temperature failed :%d\n", ret);
		return ret;
	} else{
		temp = BQ2589X_TSPCT_BASE + ((val & BQ2589X_TSPCT_MASK) >> BQ2589X_TSPCT_SHIFT) * BQ2589X_TSPCT_LSB ;
		return temp;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_temperature);

int bq2589x_adc_read_charge_current(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_12, &val);
	if (ret < 0) {
		dev_err(bq->dev, "read charge current failed :%d\n", ret);
		return ret;
	} else{
		volt = (int)(BQ2589X_ICHGR_BASE + ((val & BQ2589X_ICHGR_MASK) >> BQ2589X_ICHGR_SHIFT) * BQ2589X_ICHGR_LSB) ;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_charge_current);
int bq2589x_set_chargecurrent(struct bq2589x *bq, int curr)
{
	u8 ichg;
	
	dev_info(bq->dev, "%s: ichg = %d\n", __func__, curr);
		
	if (curr < BQ2589X_ICHG_BASE)
		curr = BQ2589X_ICHG_BASE;

	ichg = (curr - BQ2589X_ICHG_BASE)/BQ2589X_ICHG_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_04, 
						BQ2589X_ICHG_MASK, ichg << BQ2589X_ICHG_SHIFT);

}

int bq2589x_set_term_current(struct bq2589x *bq, int curr)
{
	u8 iterm;

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->term_current_set(curr + 200);
	}

	if (curr < BQ2589X_ITERM_BASE)
		curr = BQ2589X_ITERM_BASE;

	iterm = (curr - BQ2589X_ITERM_BASE) / BQ2589X_ITERM_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_05, 
						BQ2589X_ITERM_MASK, iterm << BQ2589X_ITERM_SHIFT);

}
EXPORT_SYMBOL_GPL(bq2589x_set_term_current);

int bq2589x_set_prechg_current(struct bq2589x *bq, int curr)
{
	u8 iprechg;

	if (curr < BQ2589X_IPRECHG_BASE)
		curr = BQ2589X_IPRECHG_BASE;

	iprechg = (curr - BQ2589X_IPRECHG_BASE) / BQ2589X_IPRECHG_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_05, 
						BQ2589X_IPRECHG_MASK, iprechg << BQ2589X_IPRECHG_SHIFT);

}
EXPORT_SYMBOL_GPL(bq2589x_set_prechg_current);

int bq2589x_set_chargevolt(struct bq2589x *bq, int volt)
{
	u8 val;
	
	dev_info(bq->dev, "%s: volt = %d\n", __func__, volt);

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->float_voltage_write(volt);
	}

	if (volt < BQ2589X_VREG_BASE)
		volt = BQ2589X_VREG_BASE;

	val = (volt - BQ2589X_VREG_BASE)/BQ2589X_VREG_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_06, 
						BQ2589X_VREG_MASK, val << BQ2589X_VREG_SHIFT);
}

int bq2589x_set_input_volt_limit(struct bq2589x *bq, int volt)
{
	u8 val;
	
	dev_info(bq->dev, "%s: volt = %d\n", __func__, volt);
	
	if (volt < BQ2589X_VINDPM_BASE)
		volt = BQ2589X_VINDPM_BASE;

	val = (volt - BQ2589X_VINDPM_BASE) / BQ2589X_VINDPM_LSB;
	
	bq2589x_update_bits(bq, BQ2589X_REG_0D, 
						BQ2589X_FORCE_VINDPM_MASK, BQ2589X_FORCE_VINDPM_ENABLE << BQ2589X_FORCE_VINDPM_SHIFT);
						
	return bq2589x_update_bits(bq, BQ2589X_REG_0D, 
						BQ2589X_VINDPM_MASK, val << BQ2589X_VINDPM_SHIFT);
}

int bq2589x_set_input_current_limit(struct bq2589x *bq, int curr)
{
	u8 val;
	
	dev_info(bq->dev, "%s: curr = %d\n", __func__, curr);

	if (curr < BQ2589X_IINLIM_BASE)
		curr = BQ2589X_IINLIM_BASE;

	val = (curr - BQ2589X_IINLIM_BASE) / BQ2589X_IINLIM_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_IINLIM_MASK, 
						val << BQ2589X_IINLIM_SHIFT);
}


int bq2589x_set_watchdog_timer(struct bq2589x *bq, u8 timeout)
{
	u8 val;

	val = (timeout - BQ2589X_WDT_BASE) / BQ2589X_WDT_LSB;
	val <<= BQ2589X_WDT_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07, 
						BQ2589X_WDT_MASK, val); 
}
EXPORT_SYMBOL_GPL(bq2589x_set_watchdog_timer);

int bq2589x_disable_watchdog_timer(struct bq2589x *bq)
{
	u8 val = BQ2589X_WDT_DISABLE << BQ2589X_WDT_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07, 
						BQ2589X_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_disable_watchdog_timer);

int bq2589x_reset_watchdog_timer(struct bq2589x *bq)
{
	u8 val = BQ2589X_WDT_RESET << BQ2589X_WDT_RESET_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_03, 
						BQ2589X_WDT_RESET_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_reset_watchdog_timer);


int bq2589x_force_dpdm(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_FORCE_DPDM << BQ2589X_FORCE_DPDM_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 
						BQ2589X_FORCE_DPDM_MASK, val);

	pr_info("Force DPDM %s\n", !ret ?  "successfully" : "failed");
	bq2589x_set_input_current_limit(bq, 1400);
	if ((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->input_current_write(600);
	}
	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_force_dpdm);

int bq2589x_reset_chip(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_RESET << BQ2589X_RESET_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_14, 
						BQ2589X_RESET_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_reset_chip);

int bq2589x_enter_hiz_mode(struct bq2589x *bq)
{
	u8 val = BQ2589X_HIZ_ENABLE << BQ2589X_ENHIZ_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_00, 
						BQ2589X_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2589x_enter_hiz_mode);

int bq2589x_exit_hiz_mode(struct bq2589x *bq)
{

	u8 val = BQ2589X_HIZ_DISABLE << BQ2589X_ENHIZ_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_00, 
						BQ2589X_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2589x_exit_hiz_mode);

int bq2589x_disable_enlim(struct bq2589x *bq)
{
	u8 val = BQ2589X_ENILIM_DISABLE << BQ2589X_ENILIM_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_00, 
						BQ2589X_ENILIM_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2589x_disable_enlim);

int bq2589x_get_hiz_mode(struct bq2589x *bq, u8 *state)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_00, &val);
	if (ret)
		return ret;
	*state = (val & BQ2589X_ENHIZ_MASK) >> BQ2589X_ENHIZ_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(bq2589x_get_hiz_mode);

static int bq2589x_enable_term(struct bq2589x *bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_TERM_ENABLE << BQ2589X_EN_TERM_SHIFT;
	else
		val = BQ2589X_TERM_DISABLE << BQ2589X_EN_TERM_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_07, 
						BQ2589X_EN_TERM_MASK, val);
	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->set_charging_term_disable();
    }

	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_enable_term);

int bq2589x_set_boost_current(struct bq2589x *bq, int curr)
{
	u8 temp;

	if (curr < 750)
		temp = BQ2589X_BOOST_LIM_500MA;
	else if (curr < 1200)
		temp = BQ2589X_BOOST_LIM_750MA;
	else if (curr < 1400)
		temp = BQ2589X_BOOST_LIM_1200MA;
	else if (curr < 1650)
		temp = BQ2589X_BOOST_LIM_1400MA;
	else if (curr < 1870)
		temp = BQ2589X_BOOST_LIM_1650MA;
	else if (curr < 2150)
		temp = BQ2589X_BOOST_LIM_1875MA;
	else if (curr < 2450)
		temp = BQ2589X_BOOST_LIM_2150MA;
	else
		temp= BQ2589X_BOOST_LIM_2450MA;

	return bq2589x_update_bits(bq, BQ2589X_REG_0A, 
				BQ2589X_BOOST_LIM_MASK, 
				temp << BQ2589X_BOOST_LIM_SHIFT);

}

static int bq2589x_enable_auto_dpdm(struct bq2589x* bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_AUTO_DPDM_ENABLE << BQ2589X_AUTO_DPDM_EN_SHIFT;
	else
		val = BQ2589X_AUTO_DPDM_DISABLE << BQ2589X_AUTO_DPDM_EN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 
						BQ2589X_AUTO_DPDM_EN_MASK, val);

	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_enable_auto_dpdm);

int bq2589x_set_boost_voltage(struct bq2589x *bq, int volt)
{
	u8 val = 0;

	if (volt < BQ2589X_BOOSTV_BASE)
		volt = BQ2589X_BOOSTV_BASE;
	if (volt > BQ2589X_BOOSTV_BASE 
			+ (BQ2589X_BOOSTV_MASK >> BQ2589X_BOOSTV_SHIFT) 
			* BQ2589X_BOOSTV_LSB)
		volt = BQ2589X_BOOSTV_BASE 
			+ (BQ2589X_BOOSTV_MASK >> BQ2589X_BOOSTV_SHIFT) 
			* BQ2589X_BOOSTV_LSB;

	val = ((volt - BQ2589X_BOOSTV_BASE) / BQ2589X_BOOSTV_LSB) 
			<< BQ2589X_BOOSTV_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_0A, 
				BQ2589X_BOOSTV_MASK, val);


}
EXPORT_SYMBOL_GPL(bq2589x_set_boost_voltage);

static int bq2589x_enable_ico(struct bq2589x* bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_ICO_ENABLE << BQ2589X_ICOEN_SHIFT;
	else
		val = BQ2589X_ICO_DISABLE << BQ2589X_ICOEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_ICOEN_MASK, val);

	return ret;

}
EXPORT_SYMBOL_GPL(bq2589x_enable_ico);

static int bq2589x_read_idpm_limit(struct bq2589x *bq, int *icl)
{
	uint8_t val;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_13, &val);
	if (ret < 0) {
		dev_err(bq->dev, "read vbus voltage failed :%d\n", ret);
		return ret;
	} else{
		*icl = BQ2589X_IDPM_LIM_BASE + ((val & BQ2589X_IDPM_LIM_MASK) >> BQ2589X_IDPM_LIM_SHIFT) * BQ2589X_IDPM_LIM_LSB ;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_read_idpm_limit);

static int bq2589x_enable_safety_timer(struct bq2589x *bq)
{
	const u8 val = BQ2589X_CHG_TIMER_ENABLE << BQ2589X_EN_TIMER_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07, BQ2589X_EN_TIMER_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(bq2589x_enable_safety_timer);

static int bq2589x_disable_safety_timer(struct bq2589x *bq)
{
	const u8 val = BQ2589X_CHG_TIMER_DISABLE << BQ2589X_EN_TIMER_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07, BQ2589X_EN_TIMER_MASK,
				   val);

}
EXPORT_SYMBOL_GPL(bq2589x_disable_safety_timer);

static int bq2589x_switch_to_hvdcp(struct bq2589x *bq, enum hvdcp_type type)
{

	int ret;
	u8 val;
	u8 mask;

	switch (type) {
	case HVDCP_5V:
		val = (BQ2589X_DP_0P6V << BQ2589X_DPDAC_SHIFT) 
			| (BQ2589X_DM_0V << BQ2589X_DMDAC_SHIFT);
		break;
	case HVDCP_9V:
		val = (BQ2589X_DP_3P3V << BQ2589X_DPDAC_SHIFT)
			| (BQ2589X_DM_0P6V << BQ2589X_DMDAC_SHIFT);
		break;
	case HVDCP_12V:
		val = (BQ2589X_DP_0P6V << BQ2589X_DPDAC_SHIFT)
			| (BQ2589X_DM_0P6V << BQ2589X_DMDAC_SHIFT);
		break;
	case HVDCP_20V:
		val = (BQ2589X_DP_3P3V << BQ2589X_DPDAC_SHIFT)
			| (BQ2589X_DM_3P3V << BQ2589X_DMDAC_SHIFT);
		break;

	case HVDCP_CONTINOUS:
		val = (BQ2589X_DP_0P6V << BQ2589X_DPDAC_SHIFT)
			| (BQ2589X_DM_3P3V << BQ2589X_DMDAC_SHIFT);
		break;
	case HVDCP_DPF_DMF:
		val = (BQ2589X_DP_HIZ << BQ2589X_DPDAC_SHIFT)
			| (BQ2589X_DM_HIZ << BQ2589X_DMDAC_SHIFT);
		break;
	default:
		break;
	}

	mask = BQ2589X_DPDAC_MASK | BQ2589X_DMDAC_MASK;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_01, mask, val);

	return ret;
}

static int bq2589x_check_charge_done(struct bq2589x *bq, bool *done)
{
	int ret;
	u8 val;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &val);
	if (!ret) {
		val = val & BQ2589X_CHRG_STAT_MASK;
		val = val >> BQ2589X_CHRG_STAT_SHIFT;
		*done = (val == BQ2589X_CHRG_STAT_CHGDONE);
	}

	return ret;

}

static struct bq2589x_platform_data *bq2589x_parse_dt(struct device_node *np,
						      struct bq2589x *bq)
{
	int ret;
	struct bq2589x_platform_data *pdata;

	pdata = devm_kzalloc(bq->dev, sizeof(struct bq2589x_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_string(np, "charger_name", &bq->chg_dev_name) < 0) {
		bq->chg_dev_name = "primary_chg";
		pr_warn("no charger name\n");
	}

	if (of_property_read_string(np, "eint_name", &bq->eint_name) < 0) {
		bq->eint_name = "chr_stat";
		pr_warn("no eint name\n");
	}

	bq->chg_det_enable =
	    of_property_read_bool(np, "ti,bq2589x,charge-detect-enable");

	ret = of_property_read_u32(np, "ti,bq2589x,usb-vlim", &pdata->usb.vlim);
	if (ret) {
		pdata->usb.vlim = 4500;
		pr_err("Failed to read node of ti,bq2589x,usb-vlim\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,usb-ilim", &pdata->usb.ilim);
	if (ret) {
		pdata->usb.ilim = 2000;
		pr_err("Failed to read node of ti,bq2589x,usb-ilim\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,usb-vreg", &pdata->usb.vreg);
	if (ret) {
		pdata->usb.vreg = 4200;
		pr_err("Failed to read node of ti,bq2589x,usb-vreg\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,usb-ichg", &pdata->usb.ichg);
	if (ret) {
		pdata->usb.ichg = 2000;
		pr_err("Failed to read node of ti,bq2589x,usb-ichg\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,precharge-current",
				   &pdata->iprechg);
	if (ret) {
		pdata->iprechg = 256;
		pr_err("Failed to read node of ti,bq2589x,precharge-current\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,termination-current",
				   &pdata->iterm);
	if (ret) {
		pdata->iterm = 250;
		pr_err("Failed to read node of ti,bq2589x,termination-current\n");
	}

	ret =
	    of_property_read_u32(np, "ti,bq2589x,boost-voltage",
				 &pdata->boostv);
	if (ret) {
		pdata->boostv = 5000;
		pr_err("Failed to read node of ti,bq2589x,boost-voltage\n");
	}

	ret =
	    of_property_read_u32(np, "ti,bq2589x,boost-current",
				 &pdata->boosti);
	if (ret) {
		pdata->boosti = 1200;
		pr_err("Failed to read node of ti,bq2589x,boost-current\n");
	}


	return pdata;
}

static int bq2589x_get_charger_type(struct bq2589x *bq, enum charger_type *type)
{
	int ret;

	u8 reg_val = 0;
	int vbus_stat = 0;
	enum charger_type chg_type = CHARGER_UNKNOWN;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);

	if (ret)
		return ret;

	vbus_stat = (reg_val & BQ2589X_VBUS_STAT_MASK);
	vbus_stat >>= BQ2589X_VBUS_STAT_SHIFT;
	bq->vbus_type = vbus_stat;
	pr_err("bq2589x_get_charger_type:%d,reg0B = 0x%x,part_no = %d\n", vbus_stat, reg_val, bq->part_no);
	switch (vbus_stat) {
	case BQ2589X_VBUS_TYPE_NONE:
		chg_type = CHARGER_UNKNOWN;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	case BQ2589X_VBUS_TYPE_SDP:
		chg_type = STANDARD_HOST;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB;
		break;
	case BQ2589X_VBUS_TYPE_CDP:
		chg_type = CHARGING_HOST;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case BQ2589X_VBUS_TYPE_DCP:
		chg_type = STANDARD_CHARGER;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case BQ2589X_VBUS_TYPE_HVDCP:
		chg_type = STANDARD_CHARGER;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		if(!disable_QC){
			bq->hvdcp_can_enabled = true;
		}
		break;
	case BQ2589X_VBUS_TYPE_UNKNOWN:
		chg_type = NONSTANDARD_CHARGER;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case BQ2589X_VBUS_TYPE_NON_STD:
		chg_type = NONSTANDARD_CHARGER;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	default:
		chg_type = NONSTANDARD_CHARGER;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	}

	*type = chg_type;
	pr_err("%s oplus_chg_type = %d\n", __func__, bq->oplus_chg_type);
	return 0;
}

static int bq2589x_inform_charger_type(struct bq2589x *bq)
{
	int ret = 0;
	union power_supply_propval propval;

	if (!bq->psy) {
		bq->psy = power_supply_get_by_name("charger");
		if (!bq->psy)
			return -ENODEV;
	}

	if (bq->chg_type == CHARGER_UNKNOWN || !bq->power_good)
		propval.intval = 0;
	else
		propval.intval = 1;

	ret = power_supply_set_property(bq->psy, POWER_SUPPLY_PROP_ONLINE,
					&propval);

	if (ret < 0)
		pr_notice("inform power supply online failed:%d\n", ret);

	propval.intval = bq->chg_type;

	ret = power_supply_set_property(bq->psy,
					POWER_SUPPLY_PROP_CHARGE_TYPE,
					&propval);

	if (ret < 0)
		pr_notice("inform power supply charge type failed:%d\n", ret);

	return ret;
}

static irqreturn_t bq2589x_irq_handler(int irq, void *data)
{
	struct bq2589x *bq = (struct bq2589x *)data;
	int ret;
	u8 reg_val;
	bool prev_pg;
	enum charger_type prev_chg_type;
	struct oplus_chg_chip *chip = g_oplus_chip;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);
	if (ret)
		return IRQ_HANDLED;

	prev_pg = bq->power_good;

	bq->power_good = !!(reg_val & BQ2589X_PG_STAT_MASK);

	pr_notice("bq2589x_irq_handler:(%d,%d)\n",prev_pg,bq->power_good);
	
	oplus_bq2589x_set_mivr_by_battery_vol();
	
	if(dumpreg_by_irq)
		bq2589x_dump_regs(bq);
	
	if (!prev_pg && bq->power_good) {
#ifdef CONFIG_TCPC_CLASS
		if (!bq->chg_det_enable)
			return IRQ_HANDLED;
#endif
		get_monotonic_boottime(&bq->ptime[0]);
		pr_notice("adapter/usb inserted\n");
	} else if (prev_pg && !bq->power_good) {
#ifdef CONFIG_TCPC_CLASS
		if (bq->chg_det_enable)
			return IRQ_HANDLED;
#endif
		bq->pre_current_ma = -1;
		bq->hvdcp_can_enabled = false;
		bq->hvdcp_checked = false;
		bq->chg_type = CHARGER_UNKNOWN;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		bq->nonstand_retry_bc = false;
		memset(&bq->ptime[0], 0, sizeof(struct timespec));
		memset(&bq->ptime[1], 0, sizeof(struct timespec));
		if (chip) {
			pr_notice("%s pd qc is false\n", __func__);
		}

		bq2589x_inform_charger_type(bq);
		bq2589x_disable_hvdcp(bq);
		bq2589x_switch_to_hvdcp(g_bq, HVDCP_DPF_DMF);
		Charger_Detect_Release();
		cancel_delayed_work_sync(&bq->bq2589x_aicr_setting_work);
		cancel_delayed_work_sync(&bq->bq2589x_retry_adapter_detection);
		cancel_delayed_work_sync(&bq->bq2589x_current_setting_work);
		pr_notice("adapter/usb removed\n");
		return IRQ_HANDLED;
	} else if (!prev_pg && !bq->power_good) {
		pr_notice("prev_pg & now_pg is false\n");
		return IRQ_HANDLED;
	}

	if (g_bq->otg_enable)
		return IRQ_HANDLED;

	prev_chg_type = bq->chg_type;
	ret = bq2589x_get_charger_type(bq, &bq->chg_type);

	//bq2589x_inform_charger_type(bq);
	if (!ret && prev_chg_type != bq->chg_type && bq->chg_det_enable) {
		bq2589x_inform_charger_type(bq);
		if ((NONSTANDARD_CHARGER == bq->chg_type) && (!bq->nonstand_retry_bc)) {
			bq->nonstand_retry_bc = true;
			if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
				bq2589x_force_dpdm(bq);//even-c
				pr_info("not dpdm [%s] \n",__func__);
			} else
				bq2589x_force_dpdm(bq);
			return IRQ_HANDLED;
		} else if (STANDARD_CHARGER != bq->chg_type) {
			Charger_Detect_Release();
		}

		if (bq->chg_type == CHARGER_UNKNOWN) {
			if (bq->is_bq2589x) {
				bq2589x_disable_hvdcp(bq);
			} else {
				bq2589x_enable_hvdcp(bq);
			}
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_DPF_DMF);
			memset(&bq->ptime[0], 0, sizeof(struct timespec));
			memset(&bq->ptime[1], 0, sizeof(struct timespec));
			cancel_delayed_work_sync(&bq->bq2589x_aicr_setting_work);
			cancel_delayed_work_sync(&bq->bq2589x_current_setting_work);
		}

	//	bq2589x_set_input_current_limit(bq, 1400);
	//	g_oplus_chip->sub_chg_ops->input_current_write(600);
		if (bq->is_bq2589x) {
			if (!bq->hvdcp_checked && bq->vbus_type == BQ2589X_VBUS_TYPE_DCP) {
				bq->hvdcp_checked = true;
				bq2589x_enable_hvdcp(bq);
				bq2589x_force_dpdm(bq);
			}
		}
	} else if (!ret && (prev_chg_type == bq->chg_type)
		&& (bq->vbus_type == BQ2589X_VBUS_TYPE_DCP)
		&& !bq->retry_hvdcp_algo && bq->chg_det_enable) {
//		bq->retry_hvdcp_algo = true;
//		schedule_delayed_work(&g_bq->bq2589x_retry_adapter_detection, msecs_to_jiffies(3000));
	}

	return IRQ_HANDLED;
}

static int bq2589x_register_interrupt(struct device_node *np,struct bq2589x *bq)
{
	int ret = 0;
	
	bq->irq = irq_of_parse_and_map(np, 0);
	pr_info("irq = %d\n", bq->irq);

	ret = devm_request_threaded_irq(bq->dev, bq->irq, NULL,
					bq2589x_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					bq->eint_name, bq);
	if (ret < 0) {
		pr_err("request thread irq failed:%d\n", ret);
		return ret;
	}

	enable_irq_wake(bq->irq);

	return 0;
}

static int bq2589x_init_device(struct bq2589x *bq)
{
	int ret;

	bq2589x_disable_watchdog_timer(bq);

	ret = bq2589x_set_prechg_current(bq, bq->platform_data->iprechg);
	if (ret)
		pr_err("Failed to set prechg current, ret = %d\n", ret);

	ret = bq2589x_set_term_current(bq, bq->platform_data->iterm);
	if (ret)
		pr_err("Failed to set termination current, ret = %d\n", ret);

	ret = bq2589x_set_boost_voltage(bq, bq->platform_data->boostv);
	if (ret)
		pr_err("Failed to set boost voltage, ret = %d\n", ret);

	ret = bq2589x_set_boost_current(bq, bq->platform_data->boosti);
	if (ret)
		pr_err("Failed to set boost current, ret = %d\n", ret);

	return 0;
}

bool bq2589x_is_hvdcp(struct bq2589x *bq)
{
	int ret;

	u8 reg_val = 0;
	int vbus_stat = 0;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);

	if (ret)
		return 0;

	vbus_stat = (reg_val & BQ2589X_VBUS_STAT_MASK);
	vbus_stat >>= BQ2589X_VBUS_STAT_SHIFT;

	if(vbus_stat == BQ2589X_VBUS_TYPE_HVDCP)
		return 1;

	return 0;
}

static void determine_initial_status(struct bq2589x *bq)
{
	if(bq2589x_is_hvdcp(bq)) {
		bq2589x_get_charger_type(bq, &bq->chg_type);
		bq2589x_inform_charger_type(bq);
	}
}

static int bq2589x_detect_device(struct bq2589x *bq)
{
	int ret;
	u8 data;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_14, &data);
	if (!ret) {
		bq->part_no = (data & BQ2589X_PN_MASK) >> BQ2589X_PN_SHIFT;
		bq->revision =
		    (data & BQ2589X_DEV_REV_MASK) >> BQ2589X_DEV_REV_SHIFT;
	}
	if (bq->part_no == BQ2589X_DEVICE_CONFIGURATION) {
		bq->is_bq2589x = true;
	} else {
		bq->is_bq2589x = false;
	}
	return ret;
}

static void bq2589x_dump_regs(struct bq2589x *bq)
{
	int addr;
	u8 val[25];
	int ret;
	char buf[400];
	char *s = buf;

	for (addr = 0x0; addr <= 0x14; addr++) {
		ret = bq2589x_read_byte(bq, addr, &val[addr]);
		msleep(1);
	}
	
	s+=sprintf(s,"bq2589x_dump_regs:");
	for (addr = 0x0; addr <= 0x14; addr++){
		s+=sprintf(s,"[0x%.2x,0x%.2x]", addr, val[addr]);
	}
	s+=sprintf(s,"\n");
	
	dev_info(bq->dev,"%s",buf);
}

static ssize_t
bq2589x_show_registers(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct bq2589x *bq = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "bq2589x Reg");
	for (addr = 0x0; addr <= 0x14; addr++) {
		ret = bq2589x_read_byte(bq, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t
bq2589x_store_registers(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct bq2589x *bq = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < 0x14) {
		bq2589x_write_byte(bq, (unsigned char) reg,
				   (unsigned char) val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, bq2589x_show_registers,
		   bq2589x_store_registers);

static struct attribute *bq2589x_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group bq2589x_attr_group = {
	.attrs = bq2589x_attributes,
};

static int bq2589x_charging(struct charger_device *chg_dev, bool enable)
{

	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	u8 val;

	if (enable)
		ret = bq2589x_enable_charger(bq);
	else
		ret = bq2589x_disable_charger(bq);

	pr_err("%s charger %s\n", enable ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	ret = bq2589x_read_byte(bq, BQ2589X_REG_03, &val);

	if (!ret)
		bq->charge_enabled = !!(val & BQ2589X_CHG_CONFIG_MASK);

	return ret;
}

static int bq2589x_plug_in(struct charger_device *chg_dev)
{

	int ret;

	ret = bq2589x_charging(chg_dev, true);

	if (ret)
		pr_err("Failed to enable charging:%d\n", ret);

	return ret;
}

static int bq2589x_plug_out(struct charger_device *chg_dev)
{
	int ret;

	ret = bq2589x_charging(chg_dev, false);

	if (ret)
		pr_err("Failed to disable charging:%d\n", ret);

	return ret;
}

static int bq2589x_dump_register(struct charger_device *chg_dev)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	bq2589x_dump_regs(bq);

	return 0;
}

static int bq2589x_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	*en = bq->charge_enabled;

	return 0;
}

static int bq2589x_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	ret = bq2589x_check_charge_done(bq, done);

	return ret;
}

static int bq2589x_set_ichg(struct charger_device *chg_dev, u32 curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("charge curr = %d\n", curr);

	return bq2589x_set_chargecurrent(bq, curr / 1000);
}

static int _bq2589x_get_ichg(struct bq2589x *bq, u32 *curr)
{
	u8 reg_val;
	int ichg;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_04, &reg_val);
	if (!ret) {
		ichg = (reg_val & BQ2589X_ICHG_MASK) >> BQ2589X_ICHG_SHIFT;
		ichg = ichg * BQ2589X_ICHG_LSB + BQ2589X_ICHG_BASE;
		*curr = ichg * 1000;
	}

	return ret;
}

static int bq2589x_get_ichg(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int ichg;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_04, &reg_val);
	if (!ret) {
		ichg = (reg_val & BQ2589X_ICHG_MASK) >> BQ2589X_ICHG_SHIFT;
		ichg = ichg * BQ2589X_ICHG_LSB + BQ2589X_ICHG_BASE;
		*curr = ichg * 1000;
	}

	return ret;
}

static int bq2589x_get_min_ichg(struct charger_device *chg_dev, u32 *curr)
{
	*curr = 60 * 1000;

	return 0;
}

static int bq2589x_set_vchg(struct charger_device *chg_dev, u32 volt)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("charge volt = %d\n", volt);

	return bq2589x_set_chargevolt(bq, volt / 1000);
}

static int bq2589x_get_vchg(struct charger_device *chg_dev, u32 *volt)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int vchg;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_06, &reg_val);
	if (!ret) {
		vchg = (reg_val & BQ2589X_VREG_MASK) >> BQ2589X_VREG_SHIFT;
		vchg = vchg * BQ2589X_VREG_LSB + BQ2589X_VREG_BASE;
		*volt = vchg * 1000;
	}

	return ret;
}

static int bq2589x_set_ivl(struct charger_device *chg_dev, u32 volt)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("vindpm volt = %d\n", volt);

	return bq2589x_set_input_volt_limit(bq, volt / 1000);

}

static int bq2589x_set_icl(struct charger_device *chg_dev, u32 curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	pr_err("indpm curr = %d\n", curr);

	return bq2589x_set_input_current_limit(bq, curr / 1000);
}

static int bq2589x_get_icl(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int icl;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_00, &reg_val);
	if (!ret) {
		icl = (reg_val & BQ2589X_IINLIM_MASK) >> BQ2589X_IINLIM_SHIFT;
		icl = icl * BQ2589X_IINLIM_LSB + BQ2589X_IINLIM_BASE;
		*curr = icl * 1000;
	}

	return ret;

}

static int bq2589x_kick_wdt(struct charger_device *chg_dev)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	return bq2589x_reset_watchdog_timer(bq);
}

static int bq2589x_set_otg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	if (en)
		ret = bq2589x_enable_otg(bq);
	else
		ret = bq2589x_disable_otg(bq);

	if(!ret)
		bq->otg_enable = en;

	pr_err("%s OTG %s\n", en ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	if (g_oplus_chip != NULL) {
		power_supply_changed(g_oplus_chip->usb_psy);
	}

	return ret;
}

static int bq2589x_set_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	if (en)
		ret = bq2589x_enable_safety_timer(bq);
	else
		ret = bq2589x_disable_safety_timer(bq);

	return ret;
}

static int bq2589x_is_safety_timer_enabled(struct charger_device *chg_dev,
					   bool *en)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 reg_val;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_07, &reg_val);

	if (!ret)
		*en = !!(reg_val & BQ2589X_EN_TIMER_MASK);

	return ret;
}

static int bq2589x_set_boost_ilmt(struct charger_device *chg_dev, u32 curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	pr_err("otg curr = %d\n", curr);

	ret = bq2589x_set_boost_current(bq, curr / 1000);

	return ret;
}

static int bq2589x_enable_chgdet(struct charger_device *chg_dev, bool en)
{
	int ret;
	u8 val;
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	struct oplus_chg_chip *chip = g_oplus_chip;

	pr_notice("bq2589x_enable_chgdet:%d\n",en);
	bq->chg_det_enable = en;

	if (en) {
		Charger_Detect_Init();
	} else {
		bq->pre_current_ma = -1;
		bq->hvdcp_can_enabled = false;
		bq->hvdcp_checked = false;
		bq->retry_hvdcp_algo = false;
		bq->nonstand_retry_bc = false;
		if (chip) {
			pr_notice("%s pd qc is false\n", __func__);
		}
		if (bq->is_bq2589x) {
				bq2589x_disable_hvdcp(bq);
		} else {
				bq2589x_enable_hvdcp(bq);
		}
		bq2589x_switch_to_hvdcp(g_bq, HVDCP_DPF_DMF);
		Charger_Detect_Release();
		memset(&bq->ptime[0], 0, sizeof(struct timespec));
		memset(&bq->ptime[1], 0, sizeof(struct timespec));
		cancel_delayed_work_sync(&bq->bq2589x_aicr_setting_work);
		cancel_delayed_work_sync(&bq->bq2589x_retry_adapter_detection);
		cancel_delayed_work_sync(&bq->bq2589x_current_setting_work);
		bq->chg_type = CHARGER_UNKNOWN;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		bq2589x_inform_charger_type(bq);
	}

	val = en ? BQ2589X_AUTO_DPDM_ENABLE : BQ2589X_AUTO_DPDM_DISABLE;
	val <<= BQ2589X_AUTO_DPDM_EN_SHIFT;
	ret = bq2589x_update_bits(bq, BQ2589X_REG_02,
						BQ2589X_AUTO_DPDM_EN_MASK, val);

	if (!ret && en) {
		if(false == bq2589x_is_hvdcp(bq)){
			if (bq->is_bq2589x) {
				bq2589x_disable_hvdcp(bq);
			} else {
				bq2589x_enable_hvdcp(bq);
			}
			bq2589x_force_dpdm(bq);
		}
	}

	return ret;
}

static int bq2589x_enter_ship_mode(struct bq2589x *bq, bool en)
{
	int ret;
	u8 val;

	if (en)
		val = BQ2589X_BATFET_OFF;
	else
		val = BQ2589X_BATFET_ON;
	val <<= BQ2589X_BATFET_DIS_SHIFT;
  
	ret = bq2589x_update_bits(bq, BQ2589X_REG_09, 
						BQ2589X_BATFET_DIS_MASK, val);
	return ret;

}

static int bq2589x_enable_shipmode(bool en)
{
	int ret;
	
	ret = bq2589x_enter_ship_mode(g_bq, en);
	
	return 0;
}

static int bq2589x_set_hz_mode(bool en)
{
	int ret;

	if (en) {
		ret = bq2589x_enter_hiz_mode(g_bq);
	} else {
		ret = bq2589x_exit_hiz_mode(g_bq);
	}

	return ret;
}

static int bq2589x_set_pep20_efficiency_table(struct charger_device *chg_dev)
{
	int ret = 0;
	struct charger_manager *chg_mgr = NULL;

	chg_mgr = charger_dev_get_drvdata(chg_dev);
	if (!chg_mgr)
		return -EINVAL;
		
	chg_mgr->pe2.profile[0].vbat = 3400000;
	chg_mgr->pe2.profile[1].vbat = 3500000;
	chg_mgr->pe2.profile[2].vbat = 3600000;
	chg_mgr->pe2.profile[3].vbat = 3700000;
	chg_mgr->pe2.profile[4].vbat = 3800000;
	chg_mgr->pe2.profile[5].vbat = 3900000;
	chg_mgr->pe2.profile[6].vbat = 4000000;
	chg_mgr->pe2.profile[7].vbat = 4100000;
	chg_mgr->pe2.profile[8].vbat = 4200000;
	chg_mgr->pe2.profile[9].vbat = 4400000;

	chg_mgr->pe2.profile[0].vchr = 8000000;
	chg_mgr->pe2.profile[1].vchr = 8000000;
	chg_mgr->pe2.profile[2].vchr = 8000000;
	chg_mgr->pe2.profile[3].vchr = 8500000;
	chg_mgr->pe2.profile[4].vchr = 8500000;
	chg_mgr->pe2.profile[5].vchr = 8500000;
	chg_mgr->pe2.profile[6].vchr = 9000000;
	chg_mgr->pe2.profile[7].vchr = 9000000;
	chg_mgr->pe2.profile[8].vchr = 9000000;
	chg_mgr->pe2.profile[9].vchr = 9000000;

	return ret;
}

struct timespec ptime[13];
static int cptime[13][2];

static int dtime(int i)
{
	struct timespec time;

	time = timespec_sub(ptime[i], ptime[i-1]);
	return time.tv_nsec/1000000;
}

#define PEOFFTIME 40
#define PEONTIME 90

static int bq2589x_set_pep20_current_pattern(struct charger_device *chg_dev,
	u32 chr_vol)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	int value;
	int i, j = 0;
	int flag;

	
	//bq25890_set_vindpm(0x13);
	bq2589x_set_input_volt_limit(bq, 4500);
	//bq25890_set_ichg(8);
	bq2589x_set_chargecurrent(bq, 512);
	//bq25890_set_ico_en_start(0);
	bq2589x_enable_ico(bq, false);

	usleep_range(1000, 1200);
	value = (chr_vol - 5500000) / 500000;

	//bq25890_set_iinlim(0x0);
	bq2589x_set_input_current_limit(bq, 0);
	
	msleep(70);

	get_monotonic_boottime(&ptime[j++]);
	for (i = 4; i >= 0; i--) {
		flag = value & (1 << i);

		if (flag == 0) {
			//bq25890_set_iinlim(0xc);
			bq2589x_set_input_current_limit(bq, 700);
			msleep(PEOFFTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				pr_info(
					"charging_set_ta20_current_pattern fail1: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;
			//bq25890_set_iinlim(0x0);
			bq2589x_set_input_current_limit(bq, 0);
			msleep(PEONTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEONTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				pr_info(
					"charging_set_ta20_current_pattern fail2: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;

		} else {
			//bq25890_set_iinlim(0xc);
			bq2589x_set_input_current_limit(bq, 700);
			msleep(PEONTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEONTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				pr_info(
					"charging_set_ta20_current_pattern fail3: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;
			//bq25890_set_iinlim(0x0);
			bq2589x_set_input_current_limit(bq, 0);
			
			msleep(PEOFFTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				pr_info(
					"charging_set_ta20_current_pattern fail4: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;
		}
	}

	//bq25890_set_iinlim(0xc);
	bq2589x_set_input_current_limit(bq, 700);
	msleep(160);
	get_monotonic_boottime(&ptime[j]);
	cptime[j][0] = 160;
	cptime[j][1] = dtime(j);
	if (cptime[j][1] < 150 || cptime[j][1] > 240) {
		pr_info(
			"charging_set_ta20_current_pattern fail5: idx:%d target:%d actual:%d\n",
			i, PEOFFTIME, cptime[j][1]);
		return -EIO;
	}
	j++;

	//bq25890_set_iinlim(0x0);
	bq2589x_set_input_current_limit(bq, 0);
	msleep(30);
	//bq25890_set_iinlim(0xc);
	bq2589x_set_input_current_limit(bq, 700);

	pr_info(
	"[charging_set_ta20_current_pattern]:chr_vol:%d bit:%d time:%3d %3d %3d %3d %3d %3d %3d %3d %3d %3d %3d!!\n",
	chr_vol, value,
	cptime[1][0], cptime[2][0], cptime[3][0], cptime[4][0], cptime[5][0],
	cptime[6][0], cptime[7][0], cptime[8][0], cptime[9][0], cptime[10][0], cptime[11][0]);

	pr_info(
	"[charging_set_ta20_current_pattern2]:chr_vol:%d bit:%d time:%3d %3d %3d %3d %3d %3d %3d %3d %3d %3d %3d!!\n",
	chr_vol, value,
	cptime[1][1], cptime[2][1], cptime[3][1], cptime[4][1], cptime[5][1],
	cptime[6][1], cptime[7][1], cptime[8][1], cptime[9][1], cptime[10][1], cptime[11][1]);


	//bq25890_set_ico_en_start(1);
	bq2589x_enable_ico(bq, true);
	
	//bq25890_set_iinlim(0x3f);
	bq2589x_set_input_current_limit(bq, 3250);
	
	bq->pre_current_ma = -1;
	return 0;
}

static int bq2589x_set_pep20_reset(struct charger_device *chg_dev)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	
	//bq25890_set_vindpm(0x13);
	bq2589x_set_input_volt_limit(bq, 4500);
	//bq25890_set_ichg(8);
	bq2589x_set_chargecurrent(bq, 512);

	//bq25890_set_ico_en_start(0);
	bq2589x_enable_ico(bq, false);
	
	//bq25890_set_iinlim(0x0);
	bq2589x_set_input_current_limit(bq, 0);
	
	msleep(250);
	
	//bq25890_set_iinlim(0xc);
	bq2589x_set_input_current_limit(bq, 700);
	
	//bq25890_set_ico_en_start(1);
	bq2589x_enable_ico(bq, true);
	bq->pre_current_ma = -1;
	
	return 0;
}

static struct charger_ops bq2589x_chg_ops = {
	/* Normal charging */
	.plug_in = bq2589x_plug_in,
	.plug_out = bq2589x_plug_out,
	.dump_registers = bq2589x_dump_register,
	.enable = bq2589x_charging,
	.is_enabled = bq2589x_is_charging_enable,
	.get_charging_current = bq2589x_get_ichg,
	.set_charging_current = bq2589x_set_ichg,
	.get_input_current = bq2589x_get_icl,
	.set_input_current = bq2589x_set_icl,
	.get_constant_voltage = bq2589x_get_vchg,
	.set_constant_voltage = bq2589x_set_vchg,
	.kick_wdt = bq2589x_kick_wdt,
	.set_mivr = bq2589x_set_ivl,
	.is_charging_done = bq2589x_is_charging_done,
	.get_min_charging_current = bq2589x_get_min_ichg,

	/* Safety timer */
	.enable_safety_timer = bq2589x_set_safety_timer,
	.is_safety_timer_enabled = bq2589x_is_safety_timer_enabled,

	/* Power path */
	.enable_powerpath = NULL,
	.is_powerpath_enabled = NULL,

	.enable_chg_type_det = bq2589x_enable_chgdet,
	/* OTG */
	.enable_otg = bq2589x_set_otg,
	.set_boost_current_limit = bq2589x_set_boost_ilmt,
	.enable_discharge = NULL,

	/* PE+/PE+20 */
	.send_ta_current_pattern = NULL,
	.set_pe20_efficiency_table = bq2589x_set_pep20_efficiency_table,
	.send_ta20_current_pattern = bq2589x_set_pep20_current_pattern,
	.reset_ta = bq2589x_set_pep20_reset,
	.enable_cable_drop_comp = NULL,
	
	/* ADC */
	.get_tchg_adc = NULL,
};

void oplus_bq2589x_dump_registers(void)
{
	bq2589x_dump_regs(g_bq);

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->kick_wdt();
        g_oplus_chip->sub_chg_ops->dump_registers();
	}
}

int oplus_bq2589x_kick_wdt(void)
{
	return bq2589x_reset_watchdog_timer(g_bq);
}

static const int cool_down_current_limit_normal[6] = {500, 900, 1200, 1500, 2000, 1500};
int oplus_bq2589x_set_ichg(int cur)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	u32 uA = cur*1000;
	u32 temp_uA;
	int ret = 0;
	static old_cur = 0;

	if (g_oplus_chip->mmi_chg == 0)
		cur = 100;

	if (!(is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1))){
		if (chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)
			return ret;
	}

	if (chip->cool_down && (!(g_oplus_chip->led_on && g_oplus_chip->temperature > 420))) {
		cur = cool_down_current_limit_normal[chip->cool_down - 1];
		ret = bq2589x_set_chargecurrent(g_bq, cur);
		chip->sub_chg_ops->charging_current_write_fast(0);
	} else {
		if(cur <= 1000){   // <= 1A
			ret = bq2589x_set_chargecurrent(g_bq, cur);
			chip->sub_chg_ops->charging_current_write_fast(0);
		} else {
			if (old_cur != cur){
				ret = bq2589x_set_chargecurrent(g_bq, 1000);

				if(g_oplus_chip->is_double_charger_support) {
					g_oplus_chip->sub_chg_ops->charger_unsuspend();
					msleep(50);
				}
				chip->sub_chg_ops->charging_current_write_fast(600);
				g_bq->chg_cur = cur;
				cancel_delayed_work(&g_bq->bq2589x_current_setting_work);
				schedule_delayed_work(&g_bq->bq2589x_current_setting_work, msecs_to_jiffies(5000));
			}
		}
	}
	old_cur = cur;

	return ret;
}

void oplus_bq2589x_set_mivr(int vbatt)
{
	if(g_bq->hw_aicl_point == 4400 && vbatt > 4250) {
		g_bq->hw_aicl_point = 4500;
	} else if(g_bq->hw_aicl_point == 4500 && vbatt < 4150) {
		g_bq->hw_aicl_point = 4400;
	}

	bq2589x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);

	if(g_oplus_chip->is_double_charger_support) {
		g_oplus_chip->sub_chg_ops->set_aicl_point(vbatt);
	}
}

void oplus_bq2589x_set_mivr_by_battery_vol(void)
{

	u32 mV =0;
	int vbatt = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	
	if (chip) {
		vbatt = chip->batt_volt;
	}
	
	if(vbatt > 4300){
		mV = vbatt + 400;
	}else if(vbatt > 4200){
		mV = vbatt + 300;
	}else{
		mV = vbatt + 200;
	}
	
    if(mV<4400)
        mV = 4400;

     bq2589x_set_input_volt_limit(g_bq, mV);
}

static int usb_icl[] = {
	100, 500, 900, 1200, 1500, 1750, 2000, 3000,
};

int oplus_bq2589x_set_aicr(int current_ma)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int aicl_point_temp = 0;

	if (g_bq->pre_current_ma == current_ma)
		return rc;
	else
		g_bq->pre_current_ma = current_ma;

	if ((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support))
		chip->sub_chg_ops->input_current_write(0);

	dev_info(g_bq->dev, "%s usb input max current limit=%d\n", __func__,current_ma);
	aicl_point_temp = aicl_point = 4500;
	//bq2589x_set_input_volt_limit(g_bq, 4200);
	
	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);

	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		pr_debug( "use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1500 */
	aicl_point_temp = aicl_point + 50;
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
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
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(120);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 2; //1.2
		goto aicl_pre_step;
	}

	i = 6; /* 2000 */
	aicl_point_temp = aicl_point;
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	if (chg_vol < aicl_point_temp) {
		i =  i - 2;//1.5
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 7; /* 3000 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	if ((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		if(usb_icl[i]>1000){
			bq2589x_set_input_current_limit(g_bq, usb_icl[i]*current_percent/100);
			chip->sub_chg_ops->input_current_write(usb_icl[i]*(100-current_percent)/100);
		}else{
			bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
		}
	}else{
		bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	}
	dev_info(g_bq->dev, "%s:usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n",__func__, chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
aicl_end:
	if(chip->is_double_charger_support){
		if(usb_icl[i]>1000){
			bq2589x_set_input_current_limit(g_bq, usb_icl[i] *current_percent/100);
			chip->sub_chg_ops->input_current_write(usb_icl[i]*(100-current_percent)/100);
		}else{
			bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
		}
	}else{
		bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	}
	dev_info(g_bq->dev, "%s:usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n",__func__, chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
}

void oplus_bq2589x_safe_calling_status_check()
{
	if(g_oplus_chip == NULL) {
		return;
	}
	if((g_oplus_chip->charger_volt > 7500) && (g_oplus_chip->calling_on)) {
		if(g_bq->hvdcp_can_enabled == true) {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
			g_bq->calling_on = g_oplus_chip->calling_on;
			g_bq->hvdcp_can_enabled = false;
			dev_info(g_bq->dev, "%s:calling is on, disable hvdcp\n", __func__);
		}
	} else if((g_bq->calling_on) && (g_oplus_chip->calling_on == false)) {
		if((g_oplus_chip->ui_soc < 90) || (g_oplus_chip->batt_volt < 4250)) {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			g_bq->hvdcp_can_enabled = true;
			dev_info(g_bq->dev, "%s:calling is off, enable hvdcp\n", __func__);
		}
		g_bq->calling_on = g_oplus_chip->calling_on;
	}
}

void oplus_bq2589x_safe_camera_status_check()
{
	if(g_oplus_chip == NULL) {
		return;
	}
	if((g_oplus_chip->charger_volt > 7500) && (g_oplus_chip->camera_on)) {
		if(g_bq->hvdcp_can_enabled == true) {
			if(!(g_bq->is_bq2589x)) {
				bq2589x_disable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
			}
			g_bq->camera_on = g_oplus_chip->camera_on;
			g_bq->hvdcp_can_enabled = false;
			dev_info(g_bq->dev, "%s:Camera is on, disable hvdcp\n", __func__);
		}
	} else if((g_bq->camera_on) && (g_oplus_chip->camera_on == false)) {
		if((g_oplus_chip->ui_soc < 90) || (g_oplus_chip->batt_volt < 4250)) {
			if(!(g_bq->is_bq2589x)) {
				bq2589x_enable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			}
			g_bq->hvdcp_can_enabled = true;
			dev_info(g_bq->dev, "%s:Camera is off, enable hvdcp\n", __func__);
		}
		g_bq->camera_on = g_oplus_chip->camera_on;
	}
}

void oplus_bq2589x_cool_down_status_check()
{
	static int old_cool_flag = false;
	if (g_oplus_chip == NULL) {
		return;
	}
	if(g_bq->oplus_chg_type == POWER_SUPPLY_TYPE_USB || g_bq->oplus_chg_type == POWER_SUPPLY_TYPE_USB_CDP) {
		dev_info(g_bq->dev, "%s:cool down is disable in usb type\n", __func__);
		return;
	}
	if ((g_oplus_chip->charger_volt > 7500) && (g_oplus_chip->cool_down_force_5v)) {
		if (g_bq->hvdcp_can_enabled == true) {
			if(!(g_bq->is_bq2589x)) {
				bq2589x_disable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
			}
			g_bq->hvdcp_can_enabled = false;
			dev_info(g_bq->dev, "%s:cool down is on, disable hvdcp\n",__func__);
		}
	} else if (g_oplus_chip->cool_down_force_5v == false && old_cool_flag) {
		if ((g_oplus_chip->ui_soc < 90) || (g_oplus_chip->batt_volt < 4250)) {
			if (!(g_bq->is_bq2589x)) {
				bq2589x_enable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			}
			g_bq->hvdcp_can_enabled = true;
			dev_info(g_bq->dev, "%s:cool down is off, enable hvdcp\n", __func__);
		}
	}

	if (old_cool_flag != g_oplus_chip->cool_down_force_5v) {
		old_cool_flag = g_oplus_chip->cool_down_force_5v;
	}
}

void oplus_bq2589x_batt_temp_status_check()
{
	static int batt_temp_flag = false;
	if (g_oplus_chip == NULL) {
		return;
	}

	dev_info(g_bq->dev, "%s:battery temp %d,batt_temp_flag %d\n",  __func__,g_oplus_chip->tbatt_temp,batt_temp_flag);
	if ((g_oplus_chip->charger_volt > 7500) && (g_oplus_chip->tbatt_temp > BQ_HOT_TEMP_TO_5V)) {
		if (g_bq->hvdcp_can_enabled == true) {
			if(!(g_bq->is_bq2589x)) {
				bq2589x_disable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
			}
			g_bq->hvdcp_can_enabled = false;
			batt_temp_flag = true;
			dev_info(g_bq->dev, "%s:battery temp is high, disable hvdcp\n",  __func__);
		}
	} else if (g_oplus_chip->tbatt_temp < BQ_HOT_TEMP_TO_5V && batt_temp_flag) {
		if ((g_oplus_chip->ui_soc < 90) || (g_oplus_chip->batt_volt < 4250)) {
			if (!(g_bq->is_bq2589x)) {
				bq2589x_enable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			}
			g_bq->hvdcp_can_enabled = true;
			batt_temp_flag = false;
			dev_info(g_bq->dev, "%s:battery temp is OK, enable hvdcp\n", __func__);
		}
	}

}

int oplus_bq2589x_input_current_limit_protection(int input_cur_ma)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int temp_cur = 0;

	if(chip->temperature > BQ_HOT_TEMP_TO_5V) {
		temp_cur = BQ_INPUT_CURRENT_HOT_5V_MA;
	} else if(chip->temperature > BQ_HOT_TEMPERATURE_DECIDEGC) {  	/* > 37C */
		if (g_bq->hvdcp_can_enabled == true) {
			temp_cur = BQ_INPUT_CURRENT_HOT_TEMP_HVDCP_MA;
		} else {
			temp_cur = BQ_INPUT_CURRENT_HOT_TEMP_MA;
		}
	} else if(chip->temperature >= BQ_WARM_TEMPERATURE_DECIDEGC) {	/* >= 34C */
		if (g_bq->hvdcp_can_enabled == true) {
			temp_cur = BQ_INPUT_CURRENT_WARM_TEMP_HVDCP_MA;
		} else {
			temp_cur = BQ_INPUT_CURRENT_WARM_TEMP_MA;
		}
	} else if(chip->temperature > BQ_COLD_TEMPERATURE_DECIDEGC) {	/* > 0C */
		temp_cur = BQ_INPUT_CURRENT_NORMAL_TEMP_MA;
	} else {
		temp_cur = BQ_INPUT_CURRENT_COLD_TEMP_MA;
	}

	temp_cur = (input_cur_ma < temp_cur) ? input_cur_ma : temp_cur;
	dev_info(g_bq->dev, "%s:input_cur_ma:%d temp_cur:%d",__func__, input_cur_ma, temp_cur);

	return temp_cur;
}

int oplus_bq2589x_set_input_current_limit(int current_ma)
{
	struct timespec diff;
	unsigned int ms;
	int cur_ma = current_ma;

	oplus_bq2589x_cool_down_status_check();
	oplus_bq2589x_safe_camera_status_check();
	oplus_bq2589x_safe_calling_status_check();
	cur_ma = oplus_bq2589x_input_current_limit_protection(cur_ma);
	oplus_bq2589x_batt_temp_status_check();
	get_monotonic_boottime(&g_bq->ptime[1]);
	diff = timespec_sub(g_bq->ptime[1], g_bq->ptime[0]);
	g_bq->aicr = cur_ma;
	if (cur_ma && diff.tv_sec < 3) {
		ms = (3 - diff.tv_sec)*1000;
		cancel_delayed_work(&g_bq->bq2589x_aicr_setting_work);
		dev_info(g_bq->dev, "delayed work %d ms", ms);
		schedule_delayed_work(&g_bq->bq2589x_aicr_setting_work, msecs_to_jiffies(ms));
	} else {
		cancel_delayed_work(&g_bq->bq2589x_aicr_setting_work);
		schedule_delayed_work(&g_bq->bq2589x_aicr_setting_work, 0);
	}

	return 0;
}

#define MAX_BAT_VOLTAGE 4435
int oplus_bq2589x_set_cv(int cur)
{
	pr_err("sy6970 cur = %d,bat volt =%d\n", cur, g_oplus_chip->batt_volt);
	/*for sy6970, if bat ovp occur, vsys will drop, cause device reboot
		so battery cv can not bigger than current bat voltage*/
		if (cur < g_oplus_chip->batt_volt) {
			pr_err("sy6970: cv(%d) is lower than bat volt(%d)\n", cur, g_oplus_chip->batt_volt);
			cur = g_oplus_chip->batt_volt;
			if(cur > MAX_BAT_VOLTAGE)
				cur = MAX_BAT_VOLTAGE;
		}

	return bq2589x_set_chargevolt(g_bq, cur);
}

int oplus_bq2589x_set_ieoc(int cur)
{
	return bq2589x_set_term_current(g_bq, cur);
}

int oplus_bq2589x_charging_enable(void)
{
	return bq2589x_enable_charger(g_bq); 
}

int oplus_bq2589x_charging_disable(void)
{
	struct charger_manager *info = NULL;
	
	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info){
		dev_info(g_bq->dev, "%s:error\n", __func__);
		return false;
	}

	//mtk_pdc_plugout(info);
	if(g_bq->hvdcp_can_enabled){
		if(!(g_bq->is_bq2589x)) {
			bq2589x_disable_hvdcp(g_bq);
			if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
				bq2589x_force_dpdm(g_bq);//even-c
				pr_info("not dpdm [%s] \n",__func__);
			} else
				bq2589x_force_dpdm(g_bq);
		} else {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
		}
	dev_info(g_bq->dev, "%s: set qc to 5V", __func__);
	}

	bq2589x_disable_watchdog_timer(g_bq);
	g_bq->pre_current_ma = -1;
	g_bq->hw_aicl_point =4400;
	bq2589x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);
	
	return bq2589x_disable_charger(g_bq);
}

int oplus_bq2589x_hardware_init(void)
{
	int ret = 0;

	dev_info(g_bq->dev, "%s\n", __func__);

	g_bq->hw_aicl_point =4400;
	bq2589x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);

	/* Enable WDT */
	ret = bq2589x_set_watchdog_timer(g_bq, 80);
	if (ret < 0)
		dev_notice(g_bq->dev, "%s: en wdt fail\n", __func__);

	/* Enable charging */
	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		ret = bq2589x_enable_charger(g_bq);
		if (ret < 0)
			dev_notice(g_bq->dev, "%s: en chg fail\n", __func__);
	}

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		if(g_bq->oplus_chg_type != POWER_SUPPLY_TYPE_USB) {
			g_oplus_chip->sub_chg_ops->hardware_init();
		} else {
			g_oplus_chip->sub_chg_ops->charging_disable();
		}
	}

	return ret;

}

int oplus_bq2589x_is_charging_enabled(void)
{
	int ret;
	u8 val;

	ret = bq2589x_read_byte(g_bq, BQ2589X_REG_03, &val);

	if (!ret) {
		g_bq->charge_enabled = !!(val & BQ2589X_CHG_CONFIG_MASK);
		//pr_err("sy reg03 val = 0x%x ret=%d,enable=%d\n",val,ret,g_bq->charge_enabled);
	}

	return g_bq->charge_enabled;
}

int oplus_bq2589x_is_charging_done(void)
{
	int ret = 0;
	bool done;

	bq2589x_check_charge_done(g_bq, &done);

	return done;

}

int oplus_bq2589x_enable_otg(void)
{
	int ret = 0;

	ret = bq2589x_set_boost_current(g_bq, g_bq->platform_data->boosti);
	ret = bq2589x_enable_otg(g_bq);

	if (ret < 0) {
		dev_notice(g_bq->dev, "%s en otg fail(%d)\n", __func__, ret);
		return ret;
	}

	g_bq->otg_enable = true;
	return ret;
}

int oplus_bq2589x_disable_otg(void)
{
	int ret = 0;

	ret = bq2589x_disable_otg(g_bq);

	if (ret < 0) {
		dev_notice(g_bq->dev, "%s disable otg fail(%d)\n", __func__, ret);
		return ret;
	}

	g_bq->otg_enable = false;
	return ret;

}

int oplus_bq2589x_disable_te(void)
{
	return  bq2589x_enable_term(g_bq, false);
}

int oplus_bq2589x_get_chg_current_step(void)
{
	return BQ2589X_ICHG_LSB;
}

int oplus_bq2589x_get_charger_type(void)
{
	return g_bq->oplus_chg_type;
}

int oplus_bq2589x_charger_suspend(void)
{
	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->charger_suspend();
    }

	bq2589x_disable_charger(g_bq);

	return 0;
}

int oplus_bq2589x_charger_unsuspend(void)
{
	if(g_oplus_chip->mmi_chg == 0)
		return 0;
	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->charger_unsuspend();
	}

	bq2589x_enable_charger(g_bq);

	return 0;
}

int oplus_bq2589x_set_rechg_vol(int vol)
{
	return 0;
}

int oplus_bq2589x_reset_charger(void)
{
	return 0;
}

bool oplus_bq2589x_check_charger_resume(void)
{
	return true;
}

void oplus_bq2589x_set_chargerid_switch_val(int value)
{
	return;
}

int oplus_bq2589x_get_chargerid_switch_val(void)
{
	return 0;
}

int oplus_bq2589x_get_charger_subtype(void)
{
	struct charger_manager *info = NULL;

	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info){
		dev_info(g_bq->dev, "%s:error\n", __func__);
		return false;
	}

	if (mtk_pdc_check_charger(info) && (!disable_PD)) {
		return CHARGER_SUBTYPE_PD;
	} else if (g_bq->hvdcp_can_enabled){
		return CHARGER_SUBTYPE_QC;
	} else {
		return CHARGER_SUBTYPE_DEFAULT;
	}
}

bool oplus_bq2589x_need_to_check_ibatt(void)
{
	return false;
}

int oplus_bq2589x_get_dyna_aicl_result(void)
{
	int mA = 0;
	
	bq2589x_read_idpm_limit(g_bq, &mA);
	return mA;
}

int oplus_bq2589x_set_qc_config(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct charger_manager *info = NULL;

	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info){
		dev_info(g_bq->dev, "%s:error\n", __func__);
		return false;
	}
	
	if (!chip) {
		dev_info(g_bq->dev, "%s: error\n", __func__);
		return false;
	}

	if(disable_QC){
		dev_info(g_bq->dev, "%s:disable_QC\n", __func__);
		return false;
	}

	if(g_bq->disable_hight_vbus==1){
		dev_info(g_bq->dev, "%s:disable_hight_vbus\n", __func__);
		return false;
	}

	if(chip->charging_state == CHARGING_STATUS_FAIL) {
		dev_info(g_bq->dev, "%s:charging_status_fail\n", __func__);
		return false;
	}

	if ((chip->temperature >= 450) || (chip->temperature < 0)) {
		if(!(g_bq->is_bq2589x)) {
			bq2589x_disable_hvdcp(g_bq);
			if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
				bq2589x_force_dpdm(g_bq);//even-c
				pr_info("not dpdm [%s] \n",__func__);
			} else
				bq2589x_force_dpdm(g_bq);
		} else
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
		dev_info(g_bq->dev, "%s: qc set to 5V, batt temperature hot or cold\n", __func__);
		return false;
	}

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500
		&& chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr&&chip->ui_soc>=85&&chip->icharging > -1000) {
		if(!(g_bq->is_bq2589x)) {
			bq2589x_disable_hvdcp(g_bq);
			if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
				bq2589x_force_dpdm(g_bq);//even-c
				pr_info("not dpdm [%s] \n",__func__);
			} else
				bq2589x_force_dpdm(g_bq);
		} else
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
		g_bq->pdqc_setup_5v = 1;
		g_bq->hvdcp_can_enabled = false;
		dev_info(g_bq->dev, "%s: set qc to 5V", __func__);
	} else { // 9v
			if (chip->ui_soc >= 92 || chip->charger_volt > 7500) {
				dev_info(g_bq->dev, "%s: soc high,or qc is 9V return", __func__);
				return false;
			}

			if(!(g_bq->is_bq2589x)) {
				bq2589x_enable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			}
	g_bq->pdqc_setup_5v = 0;
	g_bq->hvdcp_can_enabled = true;
	dev_info(g_bq->dev, "%s:qc Force output 9V\n", __func__);
	}

	return true;
}

int oplus_bq2589x_enable_qc_detect(void)
{
	return 0;
}

bool oplus_bq2589x_get_shortc_hw_gpio_status(void)
{
	return false;
}

int oplus_bq2589x_chg_set_high_vbus(bool en)
{
	int subtype;
	struct oplus_chg_chip *chip = g_oplus_chip;
	
	if (!chip) {
		dev_info(g_bq->dev, "%s: error\n", __func__);
		return false;
	}

	if(en){
		g_bq->disable_hight_vbus= 0;
		if(chip->charger_volt >7500){
			dev_info(g_bq->dev, "%s:charger_volt already 9v\n", __func__);
			return false;
		}

		if(g_bq->pdqc_setup_5v){
			dev_info(g_bq->dev, "%s:pdqc already setup5v no need 9v\n", __func__);
			return false;
		}

	}else{
		g_bq->disable_hight_vbus= 1;
		if(chip->charger_volt < 5400){
			dev_info(g_bq->dev, "%s:charger_volt already 5v\n", __func__);
			return false;
		}
	}
	
	subtype=oplus_bq2589x_get_charger_subtype();
	if(subtype==CHARGER_SUBTYPE_QC){
		if(en) {
			if(!(g_bq->is_bq2589x)) {
				bq2589x_enable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			}
			dev_info(g_bq->dev, "%s:QC Force output 9V\n", __func__);
	  	} else {
			if(!(g_bq->is_bq2589x)) {
				bq2589x_disable_hvdcp(g_bq);
				if (is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1)){
					bq2589x_force_dpdm(g_bq);//even-c
					pr_info("not dpdm [%s] \n",__func__);
				} else
					bq2589x_force_dpdm(g_bq);
		  } else {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
		  }
		dev_info(g_bq->dev, "%s: set qc to 5V", __func__);
	  }
	}else{
		dev_info(g_bq->dev, "%s:do nothing\n", __func__);
	}

	return false;
}

int oplus_bq2589x_get_pd_type(void)
{
	struct charger_manager *info = NULL;

	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info){
		dev_info(g_bq->dev, "%s:error\n", __func__);
		return false;
	}

	if(disable_PD){
		dev_info(g_bq->dev, "%s:disable_PD\n", __func__);
		return false;
	}

	info->enable_pe_4 = false;
	if(mtk_pdc_check_charger(info))
	{
		return true;
	}

	return false;

}
#define VBUS_VOL_5V	5000
#define VBUS_VOL_9V	9000
#define IBUS_VOL_2A	2000
#define IBUS_VOL_3A	3000
int oplus_bq2589x_pd_setup (void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct charger_manager *info = NULL;
	int vbus = 0, ibus = 0;

	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info){
		dev_info(g_bq->dev, "%s:error\n", __func__);
		return false;
	}

	if (!chip) {
		dev_info(g_bq->dev, "%s: error\n", __func__);
		return false;
	}

	if(disable_PD){
		dev_info(g_bq->dev, "%s:disable_PD\n", __func__);
		return false;
	}

	if(g_bq->disable_hight_vbus==1){
		dev_info(g_bq->dev, "%s:disable_hight_vbus\n", __func__);
		return false;
	}

	if(chip->charging_state == CHARGING_STATUS_FAIL) {
		dev_info(g_bq->dev, "%s:charging_status_fail\n", __func__);
		return false;
	}

	if ((chip->charger_volt > 7500) &&
			((chip->temperature >= 450) || (chip->temperature < 0))) {
		vbus = VBUS_VOL_5V;
		ibus = IBUS_VOL_3A;
		oplus_pdc_setup(&vbus, &ibus);
		dev_info(g_bq->dev, "%s: pd set 5V, batt temperature hot or cold\n", __func__);
		return false;
	}

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500
		&& chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr&&chip->ui_soc>=85&&chip->icharging > -1000) {
		dev_info(g_bq->dev, "%s: pd set qc to 5V", __func__);
		vbus = VBUS_VOL_5V;
		ibus = IBUS_VOL_3A;
		oplus_pdc_setup(&vbus, &ibus);
		g_bq->pdqc_setup_5v = 1;
	}else{
		dev_info(g_bq->dev, "%s:pd Force output 9V\n",__func__);
		vbus = VBUS_VOL_9V;
		ibus = IBUS_VOL_2A;
		oplus_pdc_setup(&vbus, &ibus);
		g_bq->pdqc_setup_5v = 0;
	}

	return true;
}

extern int oplus_battery_meter_get_battery_voltage(void);
extern int oplus_get_rtc_ui_soc(void);
extern int oplus_set_rtc_ui_soc(int value);
extern int set_rtc_spare_fg_value(int val);
extern void mt_power_off(void);
extern bool pmic_chrdet_status(void);
extern void mt_usb_connect(void);
extern void mt_usb_disconnect(void);
struct oplus_chg_operations  oplus_chg_bq2589x_ops = {
	.dump_registers = oplus_bq2589x_dump_registers,
	.kick_wdt = oplus_bq2589x_kick_wdt,
	.hardware_init = oplus_bq2589x_hardware_init,
	.charging_current_write_fast = oplus_bq2589x_set_ichg,
	.set_aicl_point = oplus_bq2589x_set_mivr,
	.input_current_write = oplus_bq2589x_set_input_current_limit,
	.float_voltage_write = oplus_bq2589x_set_cv,
	.term_current_set = oplus_bq2589x_set_ieoc,
	.charging_enable = oplus_bq2589x_charging_enable,
	.charging_disable = oplus_bq2589x_charging_disable,
	.get_charging_enable = oplus_bq2589x_is_charging_enabled,
	.charger_suspend = oplus_bq2589x_charger_suspend,
	.charger_unsuspend = oplus_bq2589x_charger_unsuspend,
	.set_rechg_vol = oplus_bq2589x_set_rechg_vol,
	.reset_charger = oplus_bq2589x_reset_charger,
	.read_full = oplus_bq2589x_is_charging_done,
	.otg_enable = oplus_bq2589x_enable_otg,
	.otg_disable = oplus_bq2589x_disable_otg,
	.set_charging_term_disable = oplus_bq2589x_disable_te,
	.check_charger_resume = oplus_bq2589x_check_charger_resume,

	.get_charger_type = oplus_bq2589x_get_charger_type,
	.get_charger_volt = battery_get_vbus,
//	int (*get_charger_current)(void);
	.get_chargerid_volt = NULL,
    .set_chargerid_switch_val = oplus_bq2589x_set_chargerid_switch_val,
    .get_chargerid_switch_val = oplus_bq2589x_get_chargerid_switch_val,
	.check_chrdet_status = (bool (*) (void)) pmic_chrdet_status,

	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = oplus_get_rtc_ui_soc,
	.set_rtc_soc = oplus_set_rtc_ui_soc,
	.set_power_off = mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
    .get_chg_current_step = oplus_bq2589x_get_chg_current_step,
    .need_to_check_ibatt = oplus_bq2589x_need_to_check_ibatt,
    .get_dyna_aicl_result = oplus_bq2589x_get_dyna_aicl_result,
    .get_shortc_hw_gpio_status = oplus_bq2589x_get_shortc_hw_gpio_status,
//	void (*check_is_iindpm_mode) (void);
    .oplus_chg_get_pd_type = oplus_bq2589x_get_pd_type,
    .oplus_chg_pd_setup = oplus_bq2589x_pd_setup,
	.get_charger_subtype = oplus_bq2589x_get_charger_subtype,
	.set_qc_config = oplus_bq2589x_set_qc_config,
	.enable_qc_detect = oplus_bq2589x_enable_qc_detect,
	.oplus_chg_set_high_vbus = oplus_bq2589x_chg_set_high_vbus,
	.enable_shipmode = bq2589x_enable_shipmode,
	.oplus_chg_set_hz_mode = bq2589x_set_hz_mode,
};

static void retry_detection_work_callback(struct work_struct *work)
{
	bq2589x_force_dpdm(g_bq);
	pr_notice("retry BC flow\n");
}

static void aicr_setting_work_callback(struct work_struct *work)
{
	oplus_bq2589x_set_aicr(g_bq->aicr);

	if(g_oplus_chip->batt_full != true)
		schedule_delayed_work(&g_bq->bq2589x_aicr_setting_work, msecs_to_jiffies(10000));
}

static void charging_current_setting_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	u32 uA = g_bq->chg_cur*1000;
	u32 temp_uA;
	int ret = 0;

	if (!(is_project(0x216AF) || is_project(0x216B0) || is_project(0x216B1))){
		if (chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)
			return;
	}

	if(g_bq->chg_cur > BQ_CHARGER_CURRENT_MAX_MA) {
		uA = BQ_CHARGER_CURRENT_MAX_MA * 1000;
	}
	temp_uA = uA  * current_percent / 100;
	ret = bq2589x_set_chargecurrent(g_bq, temp_uA/1000);
	ret = _bq2589x_get_ichg(g_bq, &temp_uA);
	uA-=temp_uA;
	chip->sub_chg_ops->charging_current_write_fast(uA/1000);
	if(ret) {
		pr_info("bq2589x set cur:%d %d failed\n", g_bq->chg_cur, temp_uA);
	}
}

static struct of_device_id bq2589x_charger_match_table[] = {
	{.compatible = "sy6970",},
	{},
};

MODULE_DEVICE_TABLE(of, bq2589x_charger_match_table);

static const struct i2c_device_id bq2589x_i2c_device_id[] = {
	{ "bq25890h", 0x03 },
	{ "bq25892", 0x00 },
	{ "bq25895", 0x07 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, bq2589x_i2c_device_id);

static int bq2589x_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct bq2589x *bq;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret = 0;

	pr_info("bq2589x probe enter\n");
	bq = devm_kzalloc(&client->dev, sizeof(struct bq2589x), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;
	g_bq = bq;

	bq->chg_consumer =
		charger_manager_get_by_name(&client->dev, "bq2589x");

	i2c_set_clientdata(client, bq);
	mutex_init(&bq->i2c_rw_lock);
	ret = bq2589x_detect_device(bq);
	if (ret) {
		pr_err("No bq2589x device found!\n");
		ret = -ENODEV;
		goto err_nodev;
	}

	bq->platform_data = bq2589x_parse_dt(node, bq);
	if (!bq->platform_data) {
		pr_err("No platform data provided.\n");
		ret = -EINVAL;
		goto err_parse_dt;
	}

	ret = bq2589x_init_device(bq);
	if (ret) {
		pr_err("Failed to init device\n");
		goto err_init;
	}

	bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	bq->pre_current_ma = -1;

	//modify for recharge
	ret =  bq2589x_enable_term(bq,true);
	if (ret) {
		pr_err("Failed to enable term\n");
		goto err_init;
	}

#ifndef CONFIG_TCPC_CLASS
	Charger_Detect_Init();
#endif
	if (bq->is_bq2589x)
		bq2589x_disable_hvdcp(bq);
	else
		bq2589x_enable_hvdcp(bq);
	bq2589x_disable_maxc(bq);
	bq2589x_disable_batfet_rst(bq);
	bq2589x_disable_ico(bq);

#ifdef CONFIG_OPLUS_CHARGER_MTK6769R
//mofify for hardware
	bq2589x_update_bits(bq, BQ2589X_REG_00, 
			BQ2589X_ENILIM_MASK, 0);
#endif
	bq2589x_update_bits(bq, BQ2589X_REG_09, 0x08,0xFF);
	bq2589x_update_bits(bq, BQ2589X_REG_06, 0x01,0xFF);

	INIT_DELAYED_WORK(&bq->bq2589x_aicr_setting_work, aicr_setting_work_callback);
	INIT_DELAYED_WORK(&bq->bq2589x_retry_adapter_detection, retry_detection_work_callback);
	INIT_DELAYED_WORK(&bq->bq2589x_current_setting_work, charging_current_setting_work);

	bq2589x_register_interrupt(node,bq);
	bq->chg_dev = charger_device_register(bq->chg_dev_name,
					      &client->dev, bq,
					      &bq2589x_chg_ops,
					      &bq2589x_chg_props);
	if (IS_ERR_OR_NULL(bq->chg_dev)) {
		ret = PTR_ERR(bq->chg_dev);
		goto err_device_register;
	}

	ret = sysfs_create_group(&bq->dev->kobj, &bq2589x_attr_group);
	if (ret){
		dev_err(bq->dev, "failed to register sysfs. err: %d\n", ret);
		goto err_sysfs_create;
	}

	schedule_delayed_work(&g_bq->bq2589x_retry_adapter_detection, msecs_to_jiffies(3000));
	determine_initial_status(bq);

	set_charger_ic(BQ2589X);
	pr_err("BQ2589X probe successfully, Part Num:%d, Revision:%d\n!",
	       bq->part_no, bq->revision);
	g_bq->camera_on = false;
	g_bq->calling_on = false;

	return 0;

err_sysfs_create:
	charger_device_unregister(bq->chg_dev);
err_device_register:	
err_init:
err_parse_dt:	
//err_match:
err_nodev:
	mutex_destroy(&bq->i2c_rw_lock);
	devm_kfree(bq->dev, bq);
	return ret;

}

static int bq2589x_charger_remove(struct i2c_client *client)
{
	struct bq2589x *bq = i2c_get_clientdata(client);

	mutex_destroy(&bq->i2c_rw_lock);

	sysfs_remove_group(&bq->dev->kobj, &bq2589x_attr_group);

	return 0;
}

static void bq2589x_charger_shutdown(struct i2c_client *client)
{
	if((g_oplus_chip != NULL) && g_bq != NULL) {
		if((g_bq->hvdcp_can_enabled) && (g_oplus_chip->charger_exist)) {
			oplus_bq2589x_charging_disable();
		}
	}
}

static struct i2c_driver bq2589x_charger_driver = {
	.driver = {
		   .name = "bq2589x-charger",
		   .owner = THIS_MODULE,
		   .of_match_table = bq2589x_charger_match_table,
		   },

	.probe = bq2589x_charger_probe,
	.remove = bq2589x_charger_remove,
	.shutdown = bq2589x_charger_shutdown,
	.id_table = bq2589x_i2c_device_id,

};

module_i2c_driver(bq2589x_charger_driver);

MODULE_DESCRIPTION("TI BQ2589X Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");


