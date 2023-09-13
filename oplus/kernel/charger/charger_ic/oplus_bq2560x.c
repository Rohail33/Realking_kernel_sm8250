/*
 * BQ2560x battery charging driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#define pr_fmt(fmt)	"[OPLUS_CHG] [BQ2560X] %s: " fmt, __func__

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
#include <mt-plat/mtk_boot.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/time.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <mt-plat/upmu_common.h>
#include "../oplus_chg_track.h"

#include <mt6357-pulse-charger.h>
#include <charger_class.h>
#include "../oplus_charger.h"
#include "oplus_bq2560x_reg.h"
#include "../oplus_vooc.h"
extern void oplus_wake_up_usbtemp_thread(void);
extern int oplus_chg_get_charger_subtype(void);
extern int oplus_chg_pd_setup(void);
extern int oplus_chg_get_pd_type(void);
extern void oplus_set_typec_cc_open(void);
extern void oplus_set_typec_sinkonly(void);
extern void oplus_get_usbtemp_burn_volt(struct oplus_chg_chip *chip);
extern bool oplus_usbtemp_condition(void);
extern void set_charger_ic(int sel);
extern struct charger_consumer *charger_manager_get_by_name(
		struct device *dev,	const char *name);
extern int mt6357_get_vbus_voltage(void);
extern bool mt6357_get_vbus_status(void);
extern int oplus_battery_meter_get_battery_voltage(void);
extern int set_rtc_spare_fg_value(int val);
extern void mt_usb_connect(void);
extern void mt_usb_disconnect(void);
extern bool is_usb_rdy(void);
extern unsigned int pmic_get_register_value(struct regmap *map,
			unsigned int addr,
			unsigned int mask,
			unsigned int shift);

void oplus_bq2560x_set_mivr_by_battery_vol(void);
static struct bq2560x *g_bq;
static bool first_connect = false;
extern struct oplus_chg_chip *g_oplus_chip;
extern struct regmap *mt6357_regmap;

extern void oplus_vooc_reset_fastchg_after_usbout(void);
extern bool oplus_vooc_get_fastchg_started(void);
extern bool oplus_voocphy_get_fastchg_commu_ing(void);
extern void oplus_chg_set_chargerid_switch_val(int value);
extern void oplus_chg_clear_chargerid_info(void);
extern void oplus_chg_set_charger_type_unknown(void);
extern bool oplus_vooc_get_fastchg_to_normal(void);
extern bool oplus_vooc_get_fastchg_to_warm(void);
extern int oplus_vooc_get_adapter_update_status(void);
extern int oplus_get_usb_status(void);

#ifdef CONFIG_OPLUS_CHARGER_MTK
extern int mt_get_chargerid_volt (void);
extern void mt_set_chargerid_switch_val(int value);
extern int mt_get_chargerid_switch_val(void);
#define FG_INTR_CHARGER_IN	8
#define FG_INTR_CHARGER_OUT	4
#endif

#define BQ_CHARGER_CURRENT_MAX_MA		3400
/*input current limit*/
#define BQ_INPUT_CURRENT_MAX_MA 		2000
#define BQ_INPUT_CURRENT_COLD_TEMP_MA		1000
#define BQ_INPUT_CURRENT_NORMAL_TEMP_MA 	2000
#define BQ_INPUT_CURRENT_WARM_TEMP_MA		1500
#define BQ_INPUT_CURRENT_WARM_TEMP_HVDCP_MA	1200
#define BQ_INPUT_CURRENT_HOT_TEMP_MA		1200
#define BQ_INPUT_CURRENT_HOT_TEMP_HVDCP_MA	1000

#define BQ_COLD_TEMPERATURE_DECIDEGC	0
#define BQ_WARM_TEMPERATURE_DECIDEGC	340
#define BQ_HOT_TEMPERATURE_DECIDEGC	370

#define AICL_POINT_VOL_9V 7600
#define AICL_POINT_VOL_5V 4140
#define HW_AICL_POINT_VOL_5V_PHASE1 4440
#define HW_AICL_POINT_VOL_5V_PHASE2 4520
#define SW_AICL_POINT_VOL_5V_PHASE1 4500
#define SW_AICL_POINT_VOL_5V_PHASE2 4535

enum bq2560x_part_no {
	BQ25600 = 0x00,
	BQ25601 = 0x02,
};

enum bq2560x_charge_state {
	CHARGE_STATE_IDLE = REG08_CHRG_STAT_IDLE,
	CHARGE_STATE_PRECHG = REG08_CHRG_STAT_PRECHG,
	CHARGE_STATE_FASTCHG = REG08_CHRG_STAT_FASTCHG,
	CHARGE_STATE_CHGDONE = REG08_CHRG_STAT_CHGDONE,
};

#ifdef CONFIG_TCPC_CLASS
enum bq2560x_charging_status {
	BQ2560X_CHG_STATUS_NOT_CHARGING = 0,
	BQ2560X_CHG_STATUS_FAST_CHARGING,
	BQ2560X_CHG_STATUS_PRE_CHARGING,
	BQ2560X_CHG_STATUS_DONE,
	BQ2560X_CHG_STATUS_MAX,
};

static enum power_supply_usb_type bq2560x_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};
#endif

struct bq2560x {
	struct device *dev;
	struct i2c_client *client;
	struct delayed_work bq2560x_aicr_setting_work;
	struct delayed_work bq2560x_current_setting_work;
	struct delayed_work init_work;
	struct delayed_work bq2560x_check_vbus_status_work;

	enum bq2560x_part_no part_no;
	int revision;

	const char *chg_dev_name;
	const char *eint_name;

	int status;
	int irq;

	struct mutex i2c_rw_lock;
	struct mutex chgdet_en_lock;

	bool chg_det_enable;
	bool charge_enabled;/* Register bit status */
	bool power_good;
	bool otg_enable;

	enum charger_type chg_type;
	enum power_supply_type oplus_chg_type;

	struct bq2560x_platform_data* platform_data;
	struct charger_device *chg_dev;
	struct timespec ptime[2];

	struct power_supply *psy;
	bool psy_online;
	int pre_current_ma;
	int aicr;
	int chg_cur;
	int hw_aicl_point;
	bool nonstand_retry_bc;
	bool is_sgm41511;

#ifdef CONFIG_TCPC_CLASS
	struct power_supply_desc psy_desc;
	/*type_c_port0*/
	struct tcpc_device *tcpc;
	struct notifier_block pd_nb;
	/*chg_det*/
	struct completion chrdet_start;
	struct task_struct *attach_task;
	struct mutex attach_lock;
	bool typec_attach;
	bool tcpc_kpoc;
	unsigned int chgdet_mdelay;
#endif
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*subchg_enable;
	struct pinctrl_state	*subchg_disable;
};

static bool dumpreg_by_irq = 0;
static int  current_percent = 70;
static bool ignore_bc_detect = false;
module_param(current_percent, int, 0644);
module_param(dumpreg_by_irq, bool, 0644);

static const struct charger_properties bq2560x_chg_props = {
	.alias_name = "bq2560x",
};

static void pmic_set_register_value(struct regmap *map,
			unsigned int addr,
			unsigned int mask,
			unsigned int shift,
			unsigned int val)
{
	regmap_update_bits(map, addr, mask << shift, val << shift);
}


int __attribute__((weak)) wakeup_fg_daemon(unsigned int flow_state, int cmd, int para1)
{
	return 0;
}

static int __bq2560x_read_reg(struct bq2560x* bq, u8 reg, u8 *data)
{
	s32 ret;
	int retry = 3;

	ret = i2c_smbus_read_byte_data(bq->client, reg);

	if (ret < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_byte_data(bq->client, reg);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8)ret;

	return 0;
}

static int __bq2560x_write_reg(struct bq2560x* bq, int reg, u8 val)
{
	s32 ret;
	int retry_cnt = 3;

	ret = i2c_smbus_write_byte_data(bq->client, reg, val);

	if (ret < 0) {
		while(retry_cnt > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_write_byte_data(bq->client, reg, val);
			if (ret < 0) {
				retry_cnt--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
				val, reg, ret);
		return ret;
	}
	return 0;
}

static int bq2560x_read_byte(struct bq2560x *bq, u8 *data, u8 reg)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __bq2560x_read_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}


static int bq2560x_write_byte(struct bq2560x *bq, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __bq2560x_write_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
	}
	return ret;
}


static int bq2560x_update_bits(struct bq2560x *bq, u8 reg,
				u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __bq2560x_read_reg(bq, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __bq2560x_write_reg(bq, reg, tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
	}

out:
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}

static void hw_bc11_init(void)
{
#if IS_ENABLED(CONFIG_USB_MTK_HDRC)
	int timeout = 200;
#endif
	msleep(200);
	if (first_connect == true) {
#if IS_ENABLED(CONFIG_USB_MTK_HDRC)
		/* add make sure USB Ready */
		if (is_usb_rdy() == false) {
			pr_info("CDP, block\n");
			while (is_usb_rdy() == false && timeout > 0) {
				msleep(100);
				timeout--;
			}
			if (timeout == 0)
				pr_info("CDP, timeout\n");
			else
				pr_info("CDP, free\n");
		} else
			pr_info("CDP, PASS\n");
#endif
		first_connect = false;
	}
	/* RG_bc11_BIAS_EN=1 */
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_BIAS_EN_ADDR,
			PMIC_RG_BC11_BIAS_EN_MASK,
			PMIC_RG_BC11_BIAS_EN_SHIFT,
			0x1);
	/* RG_bc11_VSRC_EN[1:0]=00 */
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VSRC_EN_ADDR,
			PMIC_RG_BC11_VSRC_EN_MASK,
			PMIC_RG_BC11_VSRC_EN_SHIFT,
			0x0);
	/* RG_bc11_VREF_VTH = [1:0]=00 */
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VREF_VTH_ADDR,
			PMIC_RG_BC11_VREF_VTH_MASK,
			PMIC_RG_BC11_VREF_VTH_SHIFT,
			0x0);
	/* RG_bc11_CMP_EN[1.0] = 00 */
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_CMP_EN_ADDR,
			PMIC_RG_BC11_CMP_EN_MASK,
			PMIC_RG_BC11_CMP_EN_SHIFT,
			0x0);
	/* RG_bc11_IPU_EN[1.0] = 00 */
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_IPU_EN_ADDR,
			PMIC_RG_BC11_IPU_EN_MASK,
			PMIC_RG_BC11_IPU_EN_SHIFT,
			0x0);
	/* RG_bc11_IPD_EN[1.0] = 00 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_IPD_EN_ADDR,
		PMIC_RG_BC11_IPD_EN_MASK,
		PMIC_RG_BC11_IPD_EN_SHIFT,
		0x0);
	/* bc11_RST=1 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_RST_ADDR,
		PMIC_RG_BC11_RST_MASK,
		PMIC_RG_BC11_RST_SHIFT,
		0x1);
	/* bc11_BB_CTRL=1 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_BB_CTRL_ADDR,
		PMIC_RG_BC11_BB_CTRL_MASK,
		PMIC_RG_BC11_BB_CTRL_SHIFT,
		0x1);
	/* add pull down to prevent PMIC leakage */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_IPD_EN_ADDR,
		PMIC_RG_BC11_IPD_EN_MASK,
		PMIC_RG_BC11_IPD_EN_SHIFT,
		0x1);
	msleep(50);

#if IS_ENABLED(CONFIG_USB_MTK_HDRC)
	Charger_Detect_Init();
#endif
}

static unsigned int hw_bc11_DCD(void)
{
	unsigned int wChargerAvail = 0;
	/* RG_bc11_IPU_EN[1.0] = 10 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_IPU_EN_ADDR,
		PMIC_RG_BC11_IPU_EN_MASK,
		PMIC_RG_BC11_IPU_EN_SHIFT,
		0x2);
	/* RG_bc11_IPD_EN[1.0] = 01 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_IPD_EN_ADDR,
		PMIC_RG_BC11_IPD_EN_MASK,
		PMIC_RG_BC11_IPD_EN_SHIFT,
		0x1);
	/* RG_bc11_VREF_VTH = [1:0]=01 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_VREF_VTH_ADDR,
		PMIC_RG_BC11_VREF_VTH_MASK,
		PMIC_RG_BC11_VREF_VTH_SHIFT,
		0x1);
	/* RG_bc11_CMP_EN[1.0] = 10 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_CMP_EN_ADDR,
		PMIC_RG_BC11_CMP_EN_MASK,
		PMIC_RG_BC11_CMP_EN_SHIFT,
		0x2);
	msleep(80);
	/* mdelay(80); */
	wChargerAvail = pmic_get_register_value(mt6357_regmap,
		PMIC_RGS_BC11_CMP_OUT_ADDR,
		PMIC_RGS_BC11_CMP_OUT_MASK,
		PMIC_RGS_BC11_CMP_OUT_SHIFT);

	/* RG_bc11_IPU_EN[1.0] = 00 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_IPU_EN_ADDR,
		PMIC_RG_BC11_IPU_EN_MASK,
		PMIC_RG_BC11_IPU_EN_SHIFT,
		0x0);
	/* RG_bc11_IPD_EN[1.0] = 00 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_IPD_EN_ADDR,
		PMIC_RG_BC11_IPD_EN_MASK,
		PMIC_RG_BC11_IPD_EN_SHIFT,
		0x0);
	/* RG_bc11_CMP_EN[1.0] = 00 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_CMP_EN_ADDR,
		PMIC_RG_BC11_CMP_EN_MASK,
		PMIC_RG_BC11_CMP_EN_SHIFT,
		0x0);
	/* RG_bc11_VREF_VTH = [1:0]=00 */
	pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_VREF_VTH_ADDR,
		PMIC_RG_BC11_VREF_VTH_MASK,
		PMIC_RG_BC11_VREF_VTH_SHIFT,
		0x0);

	return wChargerAvail;
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
/*modify for cfi*/
static int oplus_get_boot_mode(void)
{
	return (int)get_boot_mode();
}

static int oplus_get_boot_reason(void)
{
	return 0;
}
#endif

static unsigned int hw_bc11_stepA2(void)
{
	unsigned int wChargerAvail = 0;
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VSRC_EN_ADDR,
			PMIC_RG_BC11_VSRC_EN_MASK,
			PMIC_RG_BC11_VSRC_EN_SHIFT,
			0x2);
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_IPD_EN_ADDR,
			PMIC_RG_BC11_IPD_EN_MASK,
			PMIC_RG_BC11_IPD_EN_SHIFT,
			0x1);
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VREF_VTH_ADDR,
			PMIC_RG_BC11_VREF_VTH_MASK,
			PMIC_RG_BC11_VREF_VTH_SHIFT,
			0x0);
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_CMP_EN_ADDR,
			PMIC_RG_BC11_CMP_EN_MASK,
			PMIC_RG_BC11_CMP_EN_SHIFT,
			0x1);

	msleep(80);
	/* RG_bc11_VSRC_EN[1:0]=00 */
	wChargerAvail = pmic_get_register_value(mt6357_regmap,
			PMIC_RGS_BC11_CMP_OUT_ADDR,
			PMIC_RGS_BC11_CMP_OUT_MASK,
			PMIC_RGS_BC11_CMP_OUT_SHIFT);
	/* RG_bc11_IPD_EN[1.0] = 00 */
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VSRC_EN_ADDR,
			PMIC_RG_BC11_VSRC_EN_MASK,
			PMIC_RG_BC11_VSRC_EN_SHIFT,
			0x0);
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_IPD_EN_ADDR,
			PMIC_RG_BC11_IPD_EN_MASK,
			PMIC_RG_BC11_IPD_EN_SHIFT,
			0x0);
	pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_CMP_EN_ADDR,
			PMIC_RG_BC11_CMP_EN_MASK,
			PMIC_RG_BC11_CMP_EN_SHIFT,
			0x0);

	return wChargerAvail;
}

static unsigned int hw_bc11_stepB2(void)
{
    unsigned int wChargerAvail = 0;

		pmic_set_register_value(mt6357_regmap,
		PMIC_RG_BC11_VSRC_EN_ADDR,
		PMIC_RG_BC11_VSRC_EN_MASK,
		PMIC_RG_BC11_VSRC_EN_SHIFT,
		0x1);
    //RG_bc11_IPD_EN[1:0]=10
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_IPD_EN_ADDR,
			PMIC_RG_BC11_IPD_EN_MASK,
			PMIC_RG_BC11_IPD_EN_SHIFT,
			0x2);
    //RG_bc11_VREF_VTH = [1:0]=01
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VREF_VTH_ADDR,
			PMIC_RG_BC11_VREF_VTH_MASK,
			PMIC_RG_BC11_VREF_VTH_SHIFT,
			0x0);

    //RG_bc11_CMP_EN[1.0] = 01
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_CMP_EN_ADDR,
			PMIC_RG_BC11_CMP_EN_MASK,
			PMIC_RG_BC11_CMP_EN_SHIFT,
			0x2);
    msleep(80);
    wChargerAvail = pmic_get_register_value(mt6357_regmap,
			PMIC_RGS_BC11_CMP_OUT_ADDR,
			PMIC_RGS_BC11_CMP_OUT_MASK,
			PMIC_RGS_BC11_CMP_OUT_SHIFT);

    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VSRC_EN_ADDR,
			PMIC_RG_BC11_VSRC_EN_MASK,
			PMIC_RG_BC11_VSRC_EN_SHIFT,
			0x0);
    //RG_bc11_IPU_EN[1.0] = 00
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_IPD_EN_ADDR,
			PMIC_RG_BC11_IPD_EN_MASK,
			PMIC_RG_BC11_IPD_EN_SHIFT,
			0x0);
    //RG_bc11_CMP_EN[1.0] = 00
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_CMP_EN_ADDR,
			PMIC_RG_BC11_CMP_EN_MASK,
			PMIC_RG_BC11_CMP_EN_SHIFT,
			0x0);
    //RG_bc11_VREF_VTH = [1:0]=00
    //pmic_set_register_value(PMIC_RG_BC11_VREF_VTH, 0x0);
    if (wChargerAvail == 1) {
    	pmic_set_register_value(mt6357_regmap,
				PMIC_RG_BC11_VSRC_EN_ADDR,
				PMIC_RG_BC11_VSRC_EN_MASK,
				PMIC_RG_BC11_VSRC_EN_SHIFT,
				0x2);
        pr_notice("charger type: DCP, keep DM voltage source in stepB2\n");
    }

    return  wChargerAvail;
}

static void hw_bc11_done(void)
{
    //RG_bc11_VSRC_EN[1:0]=00
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VSRC_EN_ADDR,
			PMIC_RG_BC11_VSRC_EN_MASK,
			PMIC_RG_BC11_VSRC_EN_SHIFT,
			0x0);
    //RG_bc11_VREF_VTH = [1:0]=0
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_VREF_VTH_ADDR,
			PMIC_RG_BC11_VREF_VTH_MASK,
			PMIC_RG_BC11_VREF_VTH_SHIFT,
			0x0);
    //RG_bc11_CMP_EN[1.0] = 00
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_CMP_EN_ADDR,
			PMIC_RG_BC11_CMP_EN_MASK,
			PMIC_RG_BC11_CMP_EN_SHIFT,
			0x0);
    //RG_bc11_IPU_EN[1.0] = 00
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_IPU_EN_ADDR,
			PMIC_RG_BC11_IPU_EN_MASK,
			PMIC_RG_BC11_IPU_EN_SHIFT,
			0x0);
    //RG_bc11_IPD_EN[1.0] = 00
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_IPD_EN_ADDR,
			PMIC_RG_BC11_IPD_EN_MASK,
			PMIC_RG_BC11_IPD_EN_SHIFT,
			0x0);
    //RG_bc11_BIAS_EN=0
    pmic_set_register_value(mt6357_regmap,
			PMIC_RG_BC11_BIAS_EN_ADDR,
			PMIC_RG_BC11_BIAS_EN_MASK,
			PMIC_RG_BC11_BIAS_EN_SHIFT,
			0x0);

#if IS_ENABLED(CONFIG_USB_MTK_HDRC)
    Charger_Detect_Release();
#endif

}

static void dump_charger_name(int type)
{
	switch (type) {
	case CHARGER_UNKNOWN:
		pr_info("charger type: %d, CHARGER_UNKNOWN\n", type);
		break;
	case STANDARD_HOST:
		pr_info("charger type: %d, Standard USB Host\n", type);
		break;
	case CHARGING_HOST:
		pr_info("charger type: %d, Charging USB Host\n", type);
		break;
	case NONSTANDARD_CHARGER:
		pr_info("charger type: %d, Non-standard Charger\n", type);
		break;
	case STANDARD_CHARGER:
		pr_info("charger type: %d, Standard Charger\n", type);
		break;
	default:
		pr_info("charger type: %d, Not Defined!!!\n", type);
		break;
	}
}

static int hw_charging_get_charger_type(void) {
	enum charger_type g_chr_type_num = CHARGER_UNKNOWN;
	/********* Step initial  ***************/
	hw_bc11_init();

	/********* Step DCD ***************/
	if (hw_bc11_DCD()) {
		/********* Step A1 ***************/
		g_chr_type_num = STANDARD_CHARGER;
		pr_notice("step A1 : STANDARD CHARGER!\r\n");
	} else {
		/********* Step A2 ***************/
		if (hw_bc11_stepA2()) {
			/********* Step B2 ***************/
			if (hw_bc11_stepB2()) {
				g_chr_type_num = STANDARD_CHARGER;
				pr_notice("step B2 : STANDARD CHARGER!\r\n");
			} else {
				g_chr_type_num = CHARGING_HOST;
				pr_notice("step B2 :  Charging Host!\r\n");
			}
		} else {
			g_chr_type_num = STANDARD_HOST;
			pr_notice("step A2 : Standard USB Host!\r\n");
		}

	}

	/********* Finally setting *******************************/
	if (g_chr_type_num != STANDARD_CHARGER) {
		hw_bc11_done();
	} else {
		pr_notice("charger type: skip bc11 release for BC12 DCP SPEC\n");
	}

	dump_charger_name(g_chr_type_num);

	return g_chr_type_num;
}
/***************BC1.2*****************/


static int bq2560x_get_charger_type(struct bq2560x *bq, const enum charger_type type)
{
	enum charger_type chg_type = type;

	pr_notice("bq2560x_get_charger_type:%d\n", chg_type);
	if (chg_type == CHARGER_UNKNOWN)
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	if (chg_type == STANDARD_HOST)
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB;
	if (chg_type == CHARGING_HOST)
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_CDP;
	if (chg_type == STANDARD_CHARGER
		|| chg_type == APPLE_2_1A_CHARGER)
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
	if (chg_type == NONSTANDARD_CHARGER)
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;

	return 0;

}

static int bq2560x_inform_online_type(struct bq2560x *bq)
{
	int ret = 0;
	union power_supply_propval propval;

	if (!bq->psy) {
		bq->psy = power_supply_get_by_name("mtk-master-charger");
		if (IS_ERR_OR_NULL(bq->psy)) {
			pr_notice("Couldn't get bq->psy\n");
			return -EINVAL;
		}
	}

	power_supply_changed(bq->psy);

	return ret;
}

static int bq2560x_enable_otg(struct bq2560x *bq)
{
	u8 val = REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_01,
				REG01_OTG_CONFIG_MASK, val);

}

static int bq2560x_disable_otg(struct bq2560x *bq)
{
	u8 val = REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_01,
				   REG01_OTG_CONFIG_MASK, val);

}

static int bq2560x_enable_charger(struct bq2560x *bq)
{
	int ret;
	u8 val = REG01_CHG_ENABLE << REG01_CHG_CONFIG_SHIFT;
	ret = bq2560x_update_bits(bq, BQ2560X_REG_01, REG01_CHG_CONFIG_MASK, val);

	return ret;
}

static int bq2560x_disable_charger(struct bq2560x *bq)
{
	int ret;
	u8 val = REG01_CHG_DISABLE << REG01_CHG_CONFIG_SHIFT;
	ret = bq2560x_update_bits(bq, BQ2560X_REG_01, REG01_CHG_CONFIG_MASK, val);
	return ret;
}

int oplus_bq2560x_set_current(int curr)
{
	u8 ichg = 0;
	if (!g_bq) {
		return -1;
	}

	if (curr < REG02_ICHG_BASE)
		curr = REG02_ICHG_BASE;

	ichg = (curr - REG02_ICHG_BASE)/REG02_ICHG_LSB;
	return bq2560x_update_bits(g_bq, BQ2560X_REG_02, REG02_ICHG_MASK,
			ichg << REG02_ICHG_SHIFT);
}

int bq2560x_set_chargecurrent(struct bq2560x *bq, int curr)
{
	u8 ichg;

	if (curr < REG02_ICHG_BASE)
		curr = REG02_ICHG_BASE;

	ichg = (curr - REG02_ICHG_BASE)/REG02_ICHG_LSB;
	return bq2560x_update_bits(bq, BQ2560X_REG_02, REG02_ICHG_MASK,
				ichg << REG02_ICHG_SHIFT);

}

int bq2560x_set_term_current(struct bq2560x *bq, int curr)
{
	u8 iterm;

	if (curr < REG03_ITERM_BASE)
		curr = REG03_ITERM_BASE;

	iterm = (curr - REG03_ITERM_BASE) / REG03_ITERM_LSB;

	return bq2560x_update_bits(bq, BQ2560X_REG_03, REG03_ITERM_MASK,
				iterm << REG03_ITERM_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2560x_set_term_current);


int bq2560x_set_prechg_current(struct bq2560x *bq, int curr)
{
	u8 iprechg;

	if (curr < REG03_IPRECHG_BASE)
		curr = REG03_IPRECHG_BASE;

	iprechg = (curr - REG03_IPRECHG_BASE) / REG03_IPRECHG_LSB;

	return bq2560x_update_bits(bq, BQ2560X_REG_03, REG03_IPRECHG_MASK,
				iprechg << REG03_IPRECHG_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2560x_set_prechg_current);

int bq2560x_set_chargevolt(struct bq2560x *bq, int volt)
{
	u8 val;

	if (volt < REG04_VREG_BASE)
		volt = REG04_VREG_BASE;

	val = (volt - REG04_VREG_BASE)/REG04_VREG_LSB;
	return bq2560x_update_bits(bq, BQ2560X_REG_04, REG04_VREG_MASK,
				val << REG04_VREG_SHIFT);
}


int bq2560x_set_input_volt_limit(struct bq2560x *bq, int volt)
{
	u8 val;

	if (volt < REG06_VINDPM_BASE)
		volt = REG06_VINDPM_BASE;

	val = (volt - REG06_VINDPM_BASE) / REG06_VINDPM_LSB;
	return bq2560x_update_bits(bq, BQ2560X_REG_06, REG06_VINDPM_MASK,
				val << REG06_VINDPM_SHIFT);
}

int bq2560x_set_input_current_limit(struct bq2560x *bq, int curr)
{
	u8 val;

	if (curr < REG00_IINLIM_BASE)
		curr = REG00_IINLIM_BASE;

	val = (curr - REG00_IINLIM_BASE) / REG00_IINLIM_LSB;
	return bq2560x_update_bits(bq, BQ2560X_REG_00, REG00_IINLIM_MASK,
				val << REG00_IINLIM_SHIFT);
}


int bq2560x_set_watchdog_timer(struct bq2560x *bq, u8 timeout)
{
	u8 temp;

	temp = (u8)(((timeout - REG05_WDT_BASE) / REG05_WDT_LSB) << REG05_WDT_SHIFT);

	return bq2560x_update_bits(bq, BQ2560X_REG_05, REG05_WDT_MASK, temp);
}
EXPORT_SYMBOL_GPL(bq2560x_set_watchdog_timer);

int bq2560x_disable_watchdog_timer(struct bq2560x *bq)
{
	u8 val = REG05_WDT_DISABLE << REG05_WDT_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_05, REG05_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2560x_disable_watchdog_timer);

int bq2560x_reset_watchdog_timer(struct bq2560x *bq)
{
	u8 val = REG01_WDT_RESET << REG01_WDT_RESET_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_01, REG01_WDT_RESET_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2560x_reset_watchdog_timer);

int bq2560x_reset_chip(struct bq2560x *bq)
{
	int ret;
	u8 val = REG0B_REG_RESET << REG0B_REG_RESET_SHIFT;

	ret = bq2560x_update_bits(bq, BQ2560X_REG_0B, REG0B_REG_RESET_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2560x_reset_chip);

int bq2560x_enter_hiz_mode(struct bq2560x *bq)
{
	u8 val = REG00_HIZ_ENABLE << REG00_ENHIZ_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_00, REG00_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2560x_enter_hiz_mode);

int bq2560x_exit_hiz_mode(struct bq2560x *bq)
{

	u8 val = REG00_HIZ_DISABLE << REG00_ENHIZ_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_00, REG00_ENHIZ_MASK, val);

}
EXPORT_SYMBOL_GPL(bq2560x_exit_hiz_mode);

int bq2560x_get_hiz_mode(struct bq2560x *bq, u8 *state)
{
	u8 val;
	int ret;

	ret = bq2560x_read_byte(bq, &val, BQ2560X_REG_00);
	if (ret)
		return ret;
	*state = (val & REG00_ENHIZ_MASK) >> REG00_ENHIZ_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(bq2560x_get_hiz_mode);

int bq2560x_set_hiz_mode(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	if (en)
		ret = bq2560x_enter_hiz_mode(bq);
	else
		ret = bq2560x_exit_hiz_mode(bq);
	return ret;

}
EXPORT_SYMBOL_GPL(bq2560x_set_hiz_mode);

static int bq2560x_enable_term(struct bq2560x* bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = REG05_TERM_ENABLE << REG05_EN_TERM_SHIFT;
	else
		val = REG05_TERM_DISABLE << REG05_EN_TERM_SHIFT;

	ret = bq2560x_update_bits(bq, BQ2560X_REG_05, REG05_EN_TERM_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(bq2560x_enable_term);

int bq2560x_set_boost_current(struct bq2560x *bq, int curr)
{
	u8 val;

	val = REG02_BOOST_LIM_0P5A;
	if (curr >= BOOSTI_1200)
		val = REG02_BOOST_LIM_1P2A;

	return bq2560x_update_bits(bq, BQ2560X_REG_02, REG02_BOOST_LIM_MASK,
				val << REG02_BOOST_LIM_SHIFT);
}

int bq2560x_set_boost_voltage(struct bq2560x *bq, int volt)
{
	u8 val;

	if (volt == BOOSTV_4850)
		val = REG06_BOOSTV_4P85V;
	else if (volt == BOOSTV_5150)
		val = REG06_BOOSTV_5P15V;
	else if (volt == BOOSTV_5300)
		val = REG06_BOOSTV_5P3V;
	else
		val = REG06_BOOSTV_5V;

	return bq2560x_update_bits(bq, BQ2560X_REG_06, REG06_BOOSTV_MASK,
				val << REG06_BOOSTV_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2560x_set_boost_voltage);


static int bq2560x_set_acovp_threshold(struct bq2560x *bq, int volt)
{
	u8 val;

	if (volt == VAC_OVP_14300)
		val = REG06_OVP_14P3V;
	else if (volt == VAC_OVP_10500)
		val = REG06_OVP_10P5V;
	else if (volt == VAC_OVP_6500)
		val = REG06_OVP_6P2V;
	else
		val = REG06_OVP_5P5V;

	return bq2560x_update_bits(bq, BQ2560X_REG_06, REG06_OVP_MASK,
				val << REG06_OVP_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2560x_set_acovp_threshold);

static int bq2560x_set_stat_ctrl(struct bq2560x *bq, int ctrl)
{
	u8 val;

	val = ctrl;

	return bq2560x_update_bits(bq, BQ2560X_REG_00, REG00_STAT_CTRL_MASK,
				val << REG00_STAT_CTRL_SHIFT);
}


static int bq2560x_set_int_mask(struct bq2560x *bq, int mask)
{
	u8 val;

	val = mask;

	return bq2560x_update_bits(bq, BQ2560X_REG_0A, REG0A_INT_MASK_MASK,
				val << REG0A_INT_MASK_SHIFT);
}

static int bq2560x_enable_batfet(struct bq2560x *bq)
{
	const u8 val = REG07_BATFET_ON << REG07_BATFET_DIS_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_07, REG07_BATFET_DIS_MASK,
				val);
}
EXPORT_SYMBOL_GPL(bq2560x_enable_batfet);


static int bq2560x_disable_batfet(struct bq2560x *bq)
{
	const u8 val = REG07_BATFET_OFF << REG07_BATFET_DIS_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_07, REG07_BATFET_DIS_MASK,
				val);
}
EXPORT_SYMBOL_GPL(bq2560x_disable_batfet);

static int bq2560x_enter_ship_mode(struct bq2560x *bq, bool en)
{
	int ret;
	u8 val;
	val = en?REG07_BATFET_OFF:REG07_BATFET_ON;
	val <<= REG07_BATFET_DIS_SHIFT;

	ret = bq2560x_update_bits(bq, BQ2560X_REG_07,
						REG07_BATFET_DIS_MASK, val);

	return ret;
}

static int bq2560x_enable_shipmode(bool en)
{
	int ret;

	ret = bq2560x_enter_ship_mode(g_bq, en);

	return 0;
}

static int bq2560x_set_hz_mode(bool en)
{
	int ret;

	if (en)
		ret = bq2560x_enter_hiz_mode(g_bq);
	else
		ret = bq2560x_exit_hiz_mode(g_bq);

	return 0;
}

static int bq2560x_set_batfet_delay(struct bq2560x *bq, uint8_t delay)
{
	u8 val;

	if (delay == 0)
		val = REG07_BATFET_DLY_0S;
	else
		val = REG07_BATFET_DLY_10S;

	val <<= REG07_BATFET_DLY_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_07, REG07_BATFET_DLY_MASK,
								val);
}
EXPORT_SYMBOL_GPL(bq2560x_set_batfet_delay);

static int bq2560x_set_vdpm_bat_track(struct bq2560x *bq)
{
	const u8 val = REG07_VDPM_BAT_TRACK_200MV << REG07_VDPM_BAT_TRACK_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_07, REG07_VDPM_BAT_TRACK_MASK,
				val);
}
EXPORT_SYMBOL_GPL(bq2560x_set_vdpm_bat_track);

static int bq2560x_enable_safety_timer(struct bq2560x *bq)
{
	const u8 val = REG05_CHG_TIMER_ENABLE << REG05_EN_TIMER_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_05, REG05_EN_TIMER_MASK,
				val);
}
EXPORT_SYMBOL_GPL(bq2560x_enable_safety_timer);


static int bq2560x_disable_safety_timer(struct bq2560x *bq)
{
	const u8 val = REG05_CHG_TIMER_DISABLE << REG05_EN_TIMER_SHIFT;

	return bq2560x_update_bits(bq, BQ2560X_REG_05, REG05_EN_TIMER_MASK,
				val);
}
EXPORT_SYMBOL_GPL(bq2560x_disable_safety_timer);


static int bq2560x_inform_charger_type(struct bq2560x *bq);
static int bq2560x_chgdet_en(struct bq2560x *bq, bool en)
{
	u8 val;
	struct oplus_chg_chip *chip = g_oplus_chip;
	bq->chg_det_enable = en;
	if(g_oplus_chip == NULL || g_oplus_chip->chg_ops == NULL
		|| g_oplus_chip->chg_ops->get_charging_enable == NULL){
		return -1;
	}

	if (en) {
		if (oplus_vooc_get_fastchg_to_normal() == false
			&& oplus_vooc_get_fastchg_to_warm() == false) {
			if (chip->authenticate
				&& chip->mmi_chg
				&& !chip->balancing_bat_stop_chg
				&& (chip->charging_state != CHARGING_STATUS_FAIL)
				&& oplus_vooc_get_allow_reading()
				&& !oplus_is_rf_ftm_mode()) {
				bq2560x_enable_charger(bq);
			} else {
				chg_err("should not turn on charging here! \n");
			}
		}
	} else {
		bq->pre_current_ma = -1;
		bq->nonstand_retry_bc = false;
		if (chip) {
			pr_notice("pd qc is false\n");
		}
		bq2560x_disable_charger(bq);
		Charger_Detect_Release();
		memset(&bq->ptime[0], 0, sizeof(struct timespec));
		memset(&bq->ptime[1], 0, sizeof(struct timespec));
		cancel_delayed_work_sync(&bq->bq2560x_aicr_setting_work);
		cancel_delayed_work_sync(&bq->bq2560x_current_setting_work);
		bq->chg_type = CHARGER_UNKNOWN;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
#ifdef CONFIG_TCPC_CLASS
		bq2560x_inform_charger_type(bq);
#endif
	}
	return 0;
}

#ifdef CONFIG_TCPC_CLASS
static int bq2560x_enable_chgdet(struct charger_device *chg_dev, bool en)
{
    int ret;
    struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);

    pr_notice("bq2560x_enable_chgdet:%d\n",en);

    ret = bq2560x_chgdet_en(bq, en);

    return ret;
}
#endif

static int bq2560x_charger_ic_enable_gpio(struct bq2560x *bq)
{
	if (!bq) {
		chg_err("bq2560x chip not ready!\n");
		return -EINVAL;
	}

	bq->pinctrl = devm_pinctrl_get(bq->dev);
	if (IS_ERR(bq->pinctrl)) {
		chg_err("--get subch_en_pinctrl fail\n");
		return -EINVAL;
	} else {
		chg_err("--get subch_en_pinctrl ok\n");
	}

	bq->subchg_enable = pinctrl_lookup_state(bq->pinctrl, "subchg_enable");
	if (IS_ERR(bq->subchg_enable)) {
		chg_err("get subchg_enable fail\n");
		return -EINVAL;
	}

	bq->subchg_disable = pinctrl_lookup_state(bq->pinctrl, "subchg_disable");
	if (IS_ERR(bq->subchg_disable)) {
		chg_err("get subchg_disable fail\n");
		return -EINVAL;
	}

	pr_err("bq2560x charger IC gpio enabling");
	pinctrl_select_state(bq->pinctrl, bq->subchg_enable);

	return 0;
}

static struct bq2560x_platform_data* bq2560x_parse_dt(struct device_node *np,
							struct bq2560x * bq)
{
    int ret;
	struct bq2560x_platform_data* pdata;

	pdata = devm_kzalloc(bq->dev, sizeof(struct bq2560x_platform_data),
						GFP_KERNEL);
	if (!pdata) {
		pr_err("Out of memory\n");
		return NULL;
	}
#if 0
	ret = of_property_read_u32(np, "ti,bq2560x,chip-enable-gpio", &bq->gpio_ce);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,chip-enable-gpio\n");
	}
#endif

	if (of_property_read_string(np, "charger_name", &bq->chg_dev_name) < 0) {
		bq->chg_dev_name = "primary_chg";
		pr_warn("no charger name\n");
	}

	if (of_property_read_string(np, "eint_name", &bq->eint_name) < 0) {
		bq->eint_name = "chr_stat";
		pr_warn("no eint name\n");
	}

	bq->chg_det_enable =
	    of_property_read_bool(np, "ti,bq2560x,charge-detect-enable");

    ret = of_property_read_u32(np,"ti,bq2560x,usb-vlim",&pdata->usb.vlim);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,usb-vlim\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,usb-ilim",&pdata->usb.ilim);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,usb-ilim\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,usb-vreg",&pdata->usb.vreg);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,usb-vreg\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,usb-ichg",&pdata->usb.ichg);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,usb-ichg\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,stat-pin-ctrl",&pdata->statctrl);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,stat-pin-ctrl\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,precharge-current",&pdata->iprechg);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,precharge-current\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,termination-current",&pdata->iterm);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,termination-current\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,boost-voltage",&pdata->boostv);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,boost-voltage\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,boost-current",&pdata->boosti);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,boost-current\n");
	}

    ret = of_property_read_u32(np,"ti,bq2560x,vac-ovp-threshold",&pdata->vac_ovp);
    if(ret) {
		pr_err("Failed to read node of ti,bq2560x,vac-ovp-threshold\n");
	}

    return pdata;
}

static int bq2560x_init_device(struct bq2560x *bq)
{
	int ret;

	bq2560x_disable_watchdog_timer(bq);
	ret = bq2560x_set_input_current_limit(bq, 500);
	if (ret)
                pr_err("Failed to set input limit\n");
	bq2560x_set_vdpm_bat_track(bq);
	bq2560x_disable_safety_timer(bq);
	ret = bq2560x_set_stat_ctrl(bq, bq->platform_data->statctrl);
	if (ret)
		pr_err("Failed to set stat pin control mode, ret = %d\n",ret);

	ret = bq2560x_set_prechg_current(bq, bq->platform_data->iprechg);
	if (ret)
		pr_err("Failed to set prechg current, ret = %d\n",ret);

	ret = bq2560x_set_term_current(bq, bq->platform_data->iterm);
	if (ret)
		pr_err("Failed to set termination current, ret = %d\n",ret);

	ret = bq2560x_set_boost_voltage(bq, bq->platform_data->boostv);
	if (ret)
		pr_err("Failed to set boost voltage, ret = %d\n",ret);

	ret = bq2560x_set_boost_current(bq, bq->platform_data->boosti);
	if (ret)
		pr_err("Failed to set boost current, ret = %d\n",ret);

	ret = bq2560x_set_acovp_threshold(bq, bq->platform_data->vac_ovp);
	if (ret)
		pr_err("Failed to set acovp threshold, ret = %d\n",ret);

	ret = bq2560x_set_int_mask(bq, REG0A_IINDPM_INT_MASK | REG0A_VINDPM_INT_MASK);
	if (ret)
		pr_err("Failed to set vindpm and iindpm int mask\n");

	ret = bq2560x_set_input_volt_limit(bq, 4400);
	if (ret)
		pr_err("Failed to set input volt limit\n");

	return 0;
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define OPLUS_BC12_RETRY_NUM 2
#else
#define OPLUS_BC12_RETRY_NUM 1
#endif
static bool oplus_bq2560x_pd_without_usb(void)
{
#ifdef CONFIG_TCPC_CLASS
	if (!g_oplus_chip || !g_bq || !g_bq->tcpc) {
		chg_err("fail to init oplus_chip\n");
		return false;
	}

	if (!tcpm_inquire_pd_connected(g_bq->tcpc))
		return false;

	return (tcpm_inquire_dpm_flags(g_bq->tcpc) & DPM_FLAGS_PARTNER_USB_COMM) ? false : true;
#else
	return false;
#endif
}

static int bq2560x_bc12_detect(struct bq2560x *bq)
{
	int ret = 0;
	int bc12_retry_num = 0;
	enum charger_type prev_chg_type;

	prev_chg_type = bq->chg_type;
	bq->chg_type = hw_charging_get_charger_type();
	while ((bc12_retry_num < OPLUS_BC12_RETRY_NUM) && (bq->chg_type == STANDARD_HOST || bq->chg_type == CHARGING_HOST)) {
		bc12_retry_num++;
		if(bq->chg_type == CHARGING_HOST)
		{
			pr_info("usb check cdp successfully1\n");
		}
		pr_info("sdp cdp retry bc1, retry:[%d]\n", bc12_retry_num);
		bq->chg_type = hw_charging_get_charger_type();
	}

	if (bq->chg_type == NONSTANDARD_CHARGER) {
		pr_info("nonstd retry bc\n");
		bq->chg_type = hw_charging_get_charger_type();
	} else if (bq->chg_type == STANDARD_HOST) {
		if (oplus_bq2560x_pd_without_usb()) {
			pr_info("pd without usb_comm,force sdp to dcp\n");
			bq->chg_type = STANDARD_CHARGER;
			bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		}
	} else if (bq->chg_type == CHARGING_HOST) {
		if (oplus_bq2560x_pd_without_usb()) {
			pr_info("pd without usb_comm,force cdp to dcp\n");
			bq->chg_type = STANDARD_CHARGER;
			bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		}
	}

	ret = bq2560x_get_charger_type(bq, bq->chg_type);
	bq2560x_inform_online_type(bq);

	chg_err("[BQ2560X] prev_chg_type[%d], chg_type[%d]!!!\n", prev_chg_type, bq->chg_type);
	return ret;
}

static int bq2560x_detect_device(struct bq2560x* bq)
{
    int ret;
    u8 data;

    ret = bq2560x_read_byte(bq, &data, BQ2560X_REG_0B);
    if(ret == 0){
        bq->part_no = (data & REG0B_PN_MASK) >> REG0B_PN_SHIFT;
        bq->revision = (data & REG0B_DEV_REV_MASK) >> REG0B_DEV_REV_SHIFT;
    }
	pr_notice("/**********charger IC is SGM41511***********/ pn = 0x%2x \n", bq->part_no);
	if (bq->part_no == 0x02) {
		bq->is_sgm41511 = true;
		pr_notice("/**********charger IC is SGM41511***********/ \n");
	}

    return ret;
}

static void register_charger_devinfo(struct bq2560x* bq)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int ret = 0;
	char *version;
	char *manufacture;

	if(!bq) {
		chg_err("No bq2560x device found!\n");
		return;
        }
	switch (bq->part_no) {
	case 0x00:
		version = "bq25600";
		manufacture = "TI";
		break;
	case 0x02:
		version = "bq25601";
		manufacture = "TI";
		break;
	default:
		version = "unknown";
		manufacture = "UNKNOWN";
		break;
	}
	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		ret = register_device_proc("charger", version, manufacture);
	}
	else {
		ret = register_device_proc("secondary_charger",version,manufacture);
	}
	if (ret) {
          pr_err("register_charger_devinfo failed\n");
        }
#endif
}

#ifdef CONFIG_TCPC_CLASS
static int bq2560x_get_charging_status(struct bq2560x *bq, enum bq2560x_charging_status *chg_stat)
{
	int ret = 0;
	u8 val;

	ret = bq2560x_read_byte(bq, &val, BQ2560X_REG_08);
	if(ret < 0){
		*chg_stat = BQ2560X_CHG_STATUS_NOT_CHARGING;
		return ret;
	}
	val = (val & REG08_CHRG_STAT_MASK) >> REG08_CHRG_STAT_SHIFT;

	switch (val) {
		case REG08_CHRG_STAT_IDLE:
			*chg_stat = BQ2560X_CHG_STATUS_NOT_CHARGING;
			break;
		case REG08_CHRG_STAT_PRECHG:
			*chg_stat = BQ2560X_CHG_STATUS_PRE_CHARGING;
			break;
		case REG08_CHRG_STAT_FASTCHG:
			*chg_stat = BQ2560X_CHG_STATUS_FAST_CHARGING;
			break;
		case REG08_CHRG_STAT_CHGDONE:
			*chg_stat = BQ2560X_CHG_STATUS_DONE;
			break;
		default:
			*chg_stat = BQ2560X_CHG_STATUS_NOT_CHARGING;
			break;
	}
	return 0;
}
#endif

static void bq2560x_dump_regs(struct bq2560x *bq)
{
	int addr;
	u8 val;
	int ret;
	char buffer[256];
	int len;
	int idx = 0;

	memset(buffer, '\0', sizeof(buffer));
	for (addr = 0x0; addr <= 0x0B; addr++) {
		ret = bq2560x_read_byte(bq, &val, addr);
		if (ret == 0){
			len = snprintf(buffer + idx, sizeof(buffer) - idx, "[%.2x]=0x%.2x  ", addr, val);
			idx += len;
		}
	}
	pr_err("%s\n",buffer);
}

static ssize_t bq2560x_show_registers(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bq2560x *bq = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret ;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "bq2560x Reg");
	for (addr = 0x0; addr <= 0x0B; addr++) {
		ret = bq2560x_read_byte(bq, &val, addr);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,"Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t bq2560x_store_registers(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct bq2560x *bq = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < 0x0B) {
		bq2560x_write_byte(bq, (unsigned char)reg, (unsigned char)val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, bq2560x_show_registers, bq2560x_store_registers);

static struct attribute *bq2560x_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group bq2560x_attr_group = {
	.attrs = bq2560x_attributes,
};

static int bq2560x_charging(struct charger_device *chg_dev, bool enable)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	u8 val;

	pr_err("start.\n");
	if (enable)
		ret = bq2560x_enable_charger(bq);
	else
		ret = bq2560x_disable_charger(bq);

	pr_debug("%s charger %s\n", enable ? "enable" : "disable",
				  !ret ? "successfully" : "failed");

	ret = bq2560x_read_byte(bq, &val, BQ2560X_REG_01);
	printk("wangtao dump charging\n");
	bq2560x_dump_regs(bq);
	if (!ret)
		bq->charge_enabled = !!(val & REG01_CHG_CONFIG_MASK);

	return ret;
}

static int bq2560x_plug_in(struct charger_device *chg_dev)
{

	int ret;
	int boot_mode;
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	boot_mode = get_boot_mode();
	if ( boot_mode == FACTORY_BOOT || boot_mode == ATE_FACTORY_BOOT)
	{
		ret = bq2560x_charging(chg_dev, false);
		bq2560x_enter_hiz_mode(bq);
	}
	else
	{
		ret = bq2560x_charging(chg_dev, false);
		mdelay(15);
		ret = bq2560x_charging(chg_dev, true);
	}
	pr_err("bq2560x----boot_mode = %d\n",boot_mode);
	if (!ret)
		pr_err("Failed to enable charging:%d\n", ret);

	return ret;
}

static int bq2560x_plug_out(struct charger_device *chg_dev)
{
	int ret;

	ret = bq2560x_charging(chg_dev, false);
	if (!ret)
		pr_err("Failed to disable charging:%d\n", ret);

	return ret;
}

static int bq2560x_dump_register(struct charger_device *chg_dev)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);

	bq2560x_dump_regs(bq);

	return 0;
}

static int bq2560x_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);

	*en = bq->charge_enabled;

	return 0;
}

static int bq2560x_check_charge_done(struct bq2560x *bq, bool *done)
{
	int ret;
	u8 val;

	ret = bq2560x_read_byte(bq, &val, BQ2560X_REG_08);
	if (!ret) {
		val = val & REG08_CHRG_STAT_MASK;
		val = val >> REG08_CHRG_STAT_SHIFT;
		*done = (val == REG08_CHRG_STAT_CHGDONE);
	}

	return ret;
}

static int bq2560x_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	ret = bq2560x_check_charge_done(bq, done);

	return ret;
}

static int bq2560x_do_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	switch (event) {
		case EVENT_EOC:
			charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
 			break;
		case EVENT_RECHARGE:
 			charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
			break;
		default:
			break;
	}
	return 0;
}

static int bq2560x_set_ichg(struct charger_device *chg_dev, u32 curr)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	return bq2560x_set_chargecurrent(bq, curr/1000);
}


static int bq2560x_get_ichg(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int ichg;
	int ret;

	ret = bq2560x_read_byte(bq, &reg_val, BQ2560X_REG_02);
	if (!ret) {
		ichg = (reg_val & REG02_ICHG_MASK) >> REG02_ICHG_SHIFT;
		ichg = ichg * REG02_ICHG_LSB + REG02_ICHG_BASE;
		*curr = ichg * 1000;
	}

	return ret;
}

static int bq2560x_get_min_ichg(struct charger_device *chg_dev, u32 *curr)
{

	*curr = 60 * 1000;

	return 0;
}

static int bq2560x_set_vchg(struct charger_device *chg_dev, u32 volt)
{

	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);

	return bq2560x_set_chargevolt(bq, volt/1000);
}

static int bq2560x_get_vchg(struct charger_device *chg_dev, u32 *volt)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int vchg;
	int ret;

	ret = bq2560x_read_byte(bq, &reg_val, BQ2560X_REG_04);
	if (!ret) {
		vchg = (reg_val & REG04_VREG_MASK) >> REG04_VREG_SHIFT;
		vchg = vchg * REG04_VREG_LSB + REG04_VREG_BASE;
		*volt = vchg * 1000;
	}

	return ret;
}

static int bq2560x_set_ivl(struct charger_device *chg_dev, u32 volt)
{

	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);

	return bq2560x_set_input_volt_limit(bq, volt/1000);

}

static int bq2560x_set_icl(struct charger_device *chg_dev, u32 curr)
{

	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	return bq2560x_set_input_current_limit(bq, curr/1000);
}

static int bq2560x_get_icl(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int icl;
	int ret;

	ret = bq2560x_read_byte(bq, &reg_val, BQ2560X_REG_00);
	if (!ret) {
		icl = (reg_val & REG00_IINLIM_MASK) >> REG00_IINLIM_SHIFT;
		icl = icl * REG00_IINLIM_LSB + REG00_IINLIM_BASE;
		*curr = icl * 1000;
	}

	return ret;

}

static int bq2560x_kick_wdt(struct charger_device *chg_dev)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);

	return bq2560x_reset_watchdog_timer(bq);
}

static int bq2560x_set_otg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	if (en)
		ret = bq2560x_enable_otg(bq);
	else
		ret = bq2560x_disable_otg(bq);

	g_bq->otg_enable = en;
	power_supply_changed(g_oplus_chip->usb_psy);
	return ret;
}

static int bq2560x_set_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	if (en)
		ret = bq2560x_enable_safety_timer(bq);
	else
		ret = bq2560x_disable_safety_timer(bq);

	return ret;
}

static int bq2560x_is_safety_timer_enabled(struct charger_device *chg_dev, bool *en)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 reg_val;

	ret = bq2560x_read_byte(bq, &reg_val, BQ2560X_REG_05);

	if (!ret)
		*en = !!(reg_val & REG05_EN_TIMER_MASK);

	return ret;
}


static int bq2560x_set_boost_ilmt(struct charger_device *chg_dev, u32 curr)
{
	struct bq2560x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	ret = bq2560x_set_boost_current(bq, curr/1000);

	return ret;
}

#ifdef CONFIG_TCPC_CLASS
static int bq2560x_inform_charger_type(struct bq2560x *bq)
{
	return oplus_chg_wake_update_work();
}
#else
static int bq2560x_inform_charger_type(struct bq2560x *bq)
{
	int ret = 0;
	union power_supply_propval propval;

	if (!bq->psy) {
		bq->psy = power_supply_get_by_name("mtk-master-charger");
		if (IS_ERR_OR_NULL(bq->psy)) {
			pr_notice("Couldn't get bq->psy\n");
			return -EINVAL;
		}
	}


	if (bq->chg_type != CHARGER_UNKNOWN)
		propval.intval = 1;
	else
		propval.intval = 0;

	ret = power_supply_set_property(bq->psy, POWER_SUPPLY_PROP_ONLINE,
					&propval);

	if (ret < 0)
		pr_notice("inform power supply online failed:%d\n", ret);

	power_supply_changed(bq->psy);

	return ret;
}
#endif

void oplus_bq2560x_dump_registers(void)
{
	bq2560x_dump_regs(g_bq);
}

bool oplus_bq2560x_check_chrdet_status(void)
{
	bool pre_vbus_status = false;

	if (g_oplus_chip == NULL) {
		return false;
	}

	/* Keep the charger in place during fast charge communication */
	if (oplus_voocphy_get_fastchg_commu_ing()) {
		pre_vbus_status = true;
		g_bq->psy_online = pre_vbus_status;
		return pre_vbus_status;
	}

	if (g_oplus_chip && g_oplus_chip->unwakelock_chg == 1
		&& g_oplus_chip->charger_type != POWER_SUPPLY_TYPE_UNKNOWN) {
		g_bq->psy_online = pre_vbus_status;
		chg_err("[BQ2560X] unwakelock_chg=1, use pre status=%d\n", pre_vbus_status);
		return pre_vbus_status;
	}

#ifdef CONFIG_TCPC_CLASS
	if (g_bq->tcpc) {
		if (tcpm_inquire_typec_attach_state(g_bq->tcpc) == TYPEC_ATTACHED_SRC){
			return false;
		}
	}
#else
	if (g_oplus_chip->otg_online == true) {
		return false;
	}
#endif
	pre_vbus_status = pmic_get_register_value(mt6357_regmap,
		PMIC_RGS_CHRDET_ADDR,
		PMIC_RGS_CHRDET_MASK,
		PMIC_RGS_CHRDET_SHIFT);
	chg_err("[BQ2560X] pre_vbus_status: %s\n", pre_vbus_status?"Pass":"Fail");

	g_bq->psy_online = pre_vbus_status;

	if (!pre_vbus_status && (g_bq->chg_type != CHARGER_UNKNOWN)) {
		g_bq->chg_type = CHARGER_UNKNOWN;
		g_bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chg_err("[BQ2560X] clear chg_type!!!\n");
	}
	return pre_vbus_status;
}

#define BQ2560X_VBUS_STATUS_CHECK_MDELAY 1000
static void bq2560x_wakeup_vbus_status_check(unsigned int delay_ms)
{
	pr_notice("check vbus status after %d ms\n", delay_ms);
	cancel_delayed_work(&g_bq->bq2560x_check_vbus_status_work);
	schedule_delayed_work(&g_bq->bq2560x_check_vbus_status_work, round_jiffies_relative(msecs_to_jiffies(delay_ms)));
}

static irqreturn_t bq2560x_irq_handler(int irq, void *data)
{
	struct bq2560x *bq = (struct bq2560x *)data;
	int ret;
	u8 reg_val;
	bool prev_pg;
	static enum charger_type prev_chg_type = CHARGER_UNKNOWN;
	struct oplus_chg_chip *chip = g_oplus_chip;
	bool vbus_status = false;

	ret = bq2560x_read_byte(bq, &reg_val, BQ2560X_REG_08);
	if (ret)
		return IRQ_HANDLED;

	prev_pg = bq->power_good;

	bq->power_good = !!(reg_val & REG08_PG_STAT_MASK);

	pr_notice("bq2560x_irq_handler:(%d,%d)\n",prev_pg,bq->power_good);
	oplus_bq2560x_dump_registers();

	oplus_chg_track_check_wired_charging_break(bq->power_good);

	if (oplus_vooc_get_fastchg_started() == true) {
		chg_err("oplus_vooc_get_fastchg_started = true!(%d %d)\n", prev_pg, bq->power_good);
		return IRQ_HANDLED;
	}
	oplus_bq2560x_set_mivr_by_battery_vol();

	if (prev_pg == bq->power_good && bq->chg_type != CHARGER_UNKNOWN
		&& bq->chg_type != NONSTANDARD_CHARGER) {
		chg_err("%s prev_pg & current pg is same!\n", __func__);
		return IRQ_HANDLED;
	}

	if (!prev_pg && bq->power_good) {
		get_monotonic_boottime(&bq->ptime[0]);
		g_bq->psy_online = true;
		oplus_chg_track_check_wired_charging_break(g_bq->psy_online);

		bq2560x_inform_charger_type(bq);
		bq2560x_inform_online_type(bq);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		wakeup_fg_daemon(FG_INTR_CHARGER_IN, 0, 0);
#endif
		pr_notice("adapter/usb inserted\n");
	} else if (prev_pg && !bq->power_good) {
		if (g_oplus_chip->vchg_status == CHARGER_STATUS__VOL_HIGH
				&& g_oplus_chip->charger_volt >= g_oplus_chip->limits.charger_hv_thr) {
			pr_notice("adapter/usb ovp return\n");
			return IRQ_HANDLED;
		}
		bq->psy_online = false;
		oplus_chg_track_check_wired_charging_break(bq->psy_online);
		bq->pre_current_ma = -1;
		bq->chg_type = CHARGER_UNKNOWN;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		bq->nonstand_retry_bc = false;
		memset(&bq->ptime[0], 0, sizeof(struct timespec));
		memset(&bq->ptime[1], 0, sizeof(struct timespec));
		if (chip) {
			pr_notice("pd qc is false\n");
		}

		vbus_status = oplus_bq2560x_check_chrdet_status();
		if (oplus_vooc_get_fastchg_started() == true
			&& oplus_vooc_get_adapter_update_status() != 1) {
			pr_err("oplus_vooc_get_fastchg_started = true!\n");
		} else {
			if (!vbus_status) {
				oplus_vooc_reset_fastchg_after_usbout();
				if (oplus_vooc_get_fastchg_started() == false) {
					oplus_chg_set_chargerid_switch_val(0);
					oplus_chg_clear_chargerid_info();
				}
				oplus_chg_set_charger_type_unknown();
			} else {
				bq2560x_wakeup_vbus_status_check(BQ2560X_VBUS_STATUS_CHECK_MDELAY);
			}
		}
		bq2560x_inform_charger_type(bq);
		bq2560x_inform_online_type(bq);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		wakeup_fg_daemon(FG_INTR_CHARGER_OUT, 0, 0);
#endif
		Charger_Detect_Release();
		cancel_delayed_work_sync(&bq->bq2560x_aicr_setting_work);
		cancel_delayed_work_sync(&bq->bq2560x_current_setting_work);
		ret = bq2560x_set_input_current_limit(bq, 500);
		if (ret)
			pr_err("set input limit error\n");
		pr_notice("adapter/usb removed\n");
		return IRQ_HANDLED;
	} else if (!prev_pg && !bq->power_good) {
		pr_notice("prev_pg & now_pg is false\n");
		return IRQ_HANDLED;
	}

	if (g_bq->otg_enable) {
		pr_err("otg enable exit!\n", __func__);
		g_bq->psy_online = false;
		return IRQ_HANDLED;
	}

#ifdef CONFIG_CHARGER_SY6970
	oplus_wake_up_usbtemp_thread();
#endif

	if (bq->chg_type != CHARGER_UNKNOWN) {
		chg_err("[BQ2560X] prev_chg_type[%d] , current chg_type[%d]", prev_chg_type, bq->chg_type);
		prev_chg_type = bq->chg_type;
		return IRQ_HANDLED;
	}

	// ALPS07072607
	if (!ignore_bc_detect) {
		bq2560x_bc12_detect(bq);
	} else {
		bq->chg_type = STANDARD_CHARGER;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
	}

	schedule_delayed_work(&g_bq->bq2560x_aicr_setting_work, msecs_to_jiffies(200));

	if (!ret && prev_chg_type != bq->chg_type) {
		if ((NONSTANDARD_CHARGER == bq->chg_type) && (!bq->nonstand_retry_bc)) {
			bq->nonstand_retry_bc = true;
			return IRQ_HANDLED;
		} else if (STANDARD_CHARGER != bq->chg_type) {
			Charger_Detect_Release();
		}

		if (bq->chg_type == CHARGER_UNKNOWN) {
			memset(&bq->ptime[0], 0, sizeof(struct timespec));
			memset(&bq->ptime[1], 0, sizeof(struct timespec));
			cancel_delayed_work_sync(&bq->bq2560x_aicr_setting_work);
			cancel_delayed_work_sync(&bq->bq2560x_current_setting_work);
		}
	}

	prev_chg_type = bq->chg_type;
	bq2560x_inform_charger_type(bq);
	bq2560x_inform_online_type(bq);

	return IRQ_HANDLED;
}

static int bq2560x_register_interrupt(struct device_node *np,struct bq2560x *bq)
{
	int ret = 0;

	bq->irq = irq_of_parse_and_map(np, 0);

	ret = devm_request_threaded_irq(bq->dev, bq->irq, NULL,
					bq2560x_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					bq->eint_name, bq);
	if (ret < 0) {
		pr_err("request thread irq failed:%d\n", ret);
		return ret;
	}

	enable_irq_wake(bq->irq);

	return 0;
}

static struct charger_ops bq2560x_chg_ops = {
	/* Normal charging */
	.plug_in = bq2560x_plug_in,
	.plug_out = bq2560x_plug_out,
	.dump_registers = bq2560x_dump_register,
	.enable = bq2560x_charging,
	.is_enabled = bq2560x_is_charging_enable,
	.get_charging_current = bq2560x_get_ichg,
	.set_charging_current = bq2560x_set_ichg,
	.get_input_current = bq2560x_get_icl,
	.set_input_current = bq2560x_set_icl,
	.get_constant_voltage = bq2560x_get_vchg,
	.set_constant_voltage = bq2560x_set_vchg,
	.kick_wdt = bq2560x_kick_wdt,
	.set_mivr = bq2560x_set_ivl,
	.is_charging_done = bq2560x_is_charging_done,
	.get_min_charging_current = bq2560x_get_min_ichg,

	/* Safety timer */
	.enable_safety_timer = bq2560x_set_safety_timer,
	.is_safety_timer_enabled = bq2560x_is_safety_timer_enabled,

	/* Power path */
	.enable_powerpath = NULL,
	.is_powerpath_enabled = NULL,
#ifdef CONFIG_TCPC_CLASS
	.enable_chg_type_det = bq2560x_enable_chgdet,
#endif
	/* OTG */
	.enable_otg = bq2560x_set_otg,
	.set_boost_current_limit = bq2560x_set_boost_ilmt,
	.enable_discharge = bq2560x_set_hiz_mode,

	/* PE+/PE+20 */
	.send_ta_current_pattern = NULL,
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = NULL,
//	.set_ta20_reset = NULL,
	.enable_cable_drop_comp = NULL,

	/* ADC */
	.get_tchg_adc = get_chgntc_adc_temp,
	.event = bq2560x_do_event,
};

/* v-haoyunlai 给otg节点赋值 */
bool oplus_get_otg_online_status_default(void)
{
#ifdef CONFIG_TCPC_CLASS
	pr_info("start\n");
	if (!g_oplus_chip || !g_bq || !g_bq->tcpc) {
		chg_err("fail to init oplus_chip\n");
		return false;
	}

	if (tcpm_inquire_typec_attach_state(g_bq->tcpc) == TYPEC_ATTACHED_SRC)
		g_oplus_chip->otg_online = true;
	else
		g_oplus_chip->otg_online = false;
	return g_oplus_chip->otg_online;
#else
	return g_oplus_chip->otg_online;
#endif
}

int oplus_get_otg_online_status(void)
{
	return oplus_get_otg_online_status_default();
}

int oplus_bq2560x_kick_wdt(void)
{
	return bq2560x_reset_watchdog_timer(g_bq);
}

int oplus_bq2560x_set_ichg(int cur)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	u32 uA = cur*1000;
	u32 temp_uA;
	int ret = 0;

	ret = bq2560x_set_chargecurrent(g_bq, cur);
	g_bq->chg_cur = cur;
	cancel_delayed_work(&g_bq->bq2560x_current_setting_work);
	schedule_delayed_work(&g_bq->bq2560x_current_setting_work, msecs_to_jiffies(3000));

	return ret;
}

void oplus_bq2560x_set_mivr(int vbatt)
{
	if(g_bq->hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE1 && vbatt > AICL_POINT_VOL_5V) {
		g_bq->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE2;
	} else if(g_bq->hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE2 && vbatt < AICL_POINT_VOL_5V) {
		g_bq->hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
	}

	bq2560x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);

}

void oplus_bq2560x_set_mivr_by_battery_vol(void)
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

     bq2560x_set_input_volt_limit(g_bq, mV);
}

static int usb_icl[] = {
	100, 500, 900, 1200, 1500, 1750, 2000, 3000,
};

int oplus_bq2560x_set_aicr(int current_ma)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int aicl_point_temp = 0;

	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		if(g_bq->chg_type == CHARGER_UNKNOWN){
			current_ma = 500;
		}
	}

	if (g_bq->pre_current_ma == current_ma)
		return rc;
	else
		g_bq->pre_current_ma = current_ma;

	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol > AICL_POINT_VOL_9V) {
		aicl_point_temp = aicl_point = AICL_POINT_VOL_9V;
	} else {
		if (chip->batt_volt > AICL_POINT_VOL_5V)
			aicl_point_temp = aicl_point = SW_AICL_POINT_VOL_5V_PHASE2;
		else
			aicl_point_temp = aicl_point = SW_AICL_POINT_VOL_5V_PHASE1;
	}
	chg_err("[BQ2560X] [current_ma:%d, chg_vol:%d, batt_volt:%d ,aicl_point:%d]\n", current_ma, chg_vol, chip->batt_volt, aicl_point);
	if (chip->stop_chg == 0 && ((g_bq->oplus_chg_type == POWER_SUPPLY_TYPE_USB || g_bq->oplus_chg_type == POWER_SUPPLY_TYPE_USB_CDP)
		|| chip->mmi_chg == 0)) {
		bq2560x_disable_charger(g_bq);
		i = 0;
		goto aicl_end;
	}

	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);

	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		pr_debug( "use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1500 */
	aicl_point_temp = aicl_point + 50;
	bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(120);
	chg_vol = mt6357_get_vbus_voltage();
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
	bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(120);
	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		i = i - 2; //1.2
		goto aicl_pre_step;
	}

	i = 6; /* 2000 */
	aicl_point_temp = aicl_point;
	bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	if (chg_vol < aicl_point_temp) {
		i =  i - 2;//1.5
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 7; /* 3000 */
	bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		if (chip->is_double_charger_support) {
			if (usb_icl[i]>1000) {
				bq2560x_set_input_current_limit(g_bq, usb_icl[i]*current_percent/100);
				chip->sub_chg_ops->input_current_write(usb_icl[i]*(100-current_percent)/100);
			} else {
				bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
			}
		} else {
			bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
		}
	} else {
		bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	}
	pr_info("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
aicl_end:
	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		if (chip->is_double_charger_support) {
			if (usb_icl[i]>1000) {
				bq2560x_set_input_current_limit(g_bq, usb_icl[i] *current_percent/100);
				chip->sub_chg_ops->input_current_write(usb_icl[i]*(100-current_percent)/100);
			} else {
				bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
			}
		} else {
			bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
		}
	} else {
		bq2560x_set_input_current_limit(g_bq, usb_icl[i]);
	}
	pr_info("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
}

int oplus_bq2560x_input_current_limit_protection(int input_cur_ma)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int temp_cur = 0;

	if(chip->temperature > BQ_HOT_TEMPERATURE_DECIDEGC) {  	/* > 37C */
		temp_cur = BQ_INPUT_CURRENT_HOT_TEMP_MA;
	} else if(chip->temperature >= BQ_WARM_TEMPERATURE_DECIDEGC) {	/* >= 34C */
		temp_cur = BQ_INPUT_CURRENT_WARM_TEMP_MA;
	} else if(chip->temperature > BQ_COLD_TEMPERATURE_DECIDEGC) {	/* > 0C */
		temp_cur = BQ_INPUT_CURRENT_NORMAL_TEMP_MA;
	} else {
		temp_cur = BQ_INPUT_CURRENT_COLD_TEMP_MA;
	}

	temp_cur = (input_cur_ma < temp_cur) ? input_cur_ma : temp_cur;
	pr_info("input_cur_ma:%d temp_cur:%d", input_cur_ma, temp_cur);

	return temp_cur;
}

#define PD_MIN_IBUS_MA 500
#define PD_MAX_IBUS_MA 3000
int oplus_bq2560x_set_input_current_limit(int current_ma)
{
	struct timespec diff;
	unsigned int ms;
	int vbus_mv = 0;
	int ibus_ma = 0;
	int cur_ma = current_ma;
	int rc = 0;

	if (g_oplus_chip->chg_ops->oplus_chg_get_pd_type) {
		if (g_oplus_chip->chg_ops->oplus_chg_get_pd_type() == true) {
			rc = oplus_pdc_get(&vbus_mv, &ibus_ma);
			pr_err("PD[%dmV, %dmA] CUR_MA[%d]\n", vbus_mv, ibus_ma, cur_ma);
			if (rc >= 0 && ibus_ma >= PD_MIN_IBUS_MA
				&& ibus_ma < PD_MAX_IBUS_MA
				&& cur_ma > ibus_ma) {
				cur_ma = ibus_ma;
				pr_err("usb input max current limit=%d(pd)\n", cur_ma);
			}
		}

	}

	if (g_oplus_chip != NULL && g_oplus_chip->dual_charger_support == true) {
		cur_ma = oplus_bq2560x_input_current_limit_protection(cur_ma);
	}

	get_monotonic_boottime(&g_bq->ptime[1]);
	diff = timespec_sub(g_bq->ptime[1], g_bq->ptime[0]);
	g_bq->aicr = cur_ma;
	if (cur_ma && diff.tv_sec < 1) {
		ms = (1 - diff.tv_sec)*1000;
		cancel_delayed_work(&g_bq->bq2560x_aicr_setting_work);
		//ms = 1000;
		pr_info("delayed work %d ms", ms);
		schedule_delayed_work(&g_bq->bq2560x_aicr_setting_work, msecs_to_jiffies(ms));
	} else {
		cancel_delayed_work(&g_bq->bq2560x_aicr_setting_work);
		schedule_delayed_work(&g_bq->bq2560x_aicr_setting_work, 0);
	}

	return 0;
}

int oplus_bq2560x_set_cv(int cur)
{
	return bq2560x_set_chargevolt(g_bq, cur);
}

int oplus_bq2560x_set_ieoc(int cur)
{
	return bq2560x_set_term_current(g_bq, cur);
}

int oplus_bq2560x_charging_enable(void)
{
	return bq2560x_enable_charger(g_bq);
}

int oplus_bq2560x_charging_disable(void)
{
	bq2560x_disable_watchdog_timer(g_bq);
	g_bq->pre_current_ma = -1;
	g_bq->hw_aicl_point = 4400;
	bq2560x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);

	return bq2560x_disable_charger(g_bq);
}

int oplus_bq2560x_hardware_init(void)
{
	int ret;

	pr_info("start\n");

	g_bq->hw_aicl_point =4400;
	bq2560x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);

	/* Enable WDT */
	ret = bq2560x_set_watchdog_timer(g_bq, 80);
	if (ret < 0)
		pr_notice("en wdt fail\n");

	/* Disable safety timer*/
	ret = bq2560x_disable_safety_timer(g_bq);
	if (ret < 0)
		chg_err("disable safety timer fail\n");

	/* Enable charging */
	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
#ifdef CONFIG_OPLUS_CHARGER_MTK
               pr_info("++boot mode=%d\n", get_boot_mode());
               if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
                               || get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
			bq2560x_enter_hiz_mode(g_bq);
               } else {
			bq2560x_exit_hiz_mode(g_bq);
			ret = bq2560x_enable_charger(g_bq);
			if (ret < 0)
				pr_notice("en chg fail\n");
               }
               pr_info("chargerid_switch=%d\n", mt_get_chargerid_switch_val());
#else
                ret = bq2560x_enable_charger(g_bq);
                if (ret < 0)
                        pr_notice("en chg fail\n");
#endif

		if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
			if(g_bq->oplus_chg_type != POWER_SUPPLY_TYPE_USB) {
				g_oplus_chip->sub_chg_ops->hardware_init();
			} else {
				g_oplus_chip->sub_chg_ops->charging_disable();
			}

		}
	}

	return ret;

}

int oplus_bq2560x_is_charging_enabled(void)
{
	int ret;
	u8 val;
	struct bq2560x *bq = g_bq;

	pr_err("start.", __func__);
	ret = bq2560x_read_byte(bq, &val, BQ2560X_REG_01);
	bq2560x_dump_regs(bq);
	if (ret) {
		chg_err("Couldn't read REG01_BQ2560x_ADDRESS ret = %d\n", ret);
	} else {
		bq->charge_enabled = !!(val & REG01_CHG_CONFIG_MASK);
	}
	g_bq->charge_enabled = bq->charge_enabled;

	return g_bq->charge_enabled;
}

int oplus_bq2560x_is_charging_done(void)
{
	int ret = 0;
	bool done;

	bq2560x_check_charge_done(g_bq, &done);

	return done;

}

int oplus_bq2560x_enable_otg(void)
{
	int ret = 0;

	ret = bq2560x_set_boost_current(g_bq, g_bq->platform_data->boosti);

	ret = bq2560x_set_otg(g_bq->chg_dev, true);
	if (ret < 0) {
		pr_notice("en otg fail(%d)\n", ret);
		return ret;
	}

#ifndef CONFIG_TCPC_CLASS
	g_oplus_chip->otg_online = true;
#endif

	g_bq->otg_enable = true;
	return ret;
}

int oplus_bq2560x_disable_otg(void)
{
	int ret = 0;

	ret = bq2560x_set_otg(g_bq->chg_dev, false);

	if (ret < 0) {
		pr_notice("disable otg fail(%d)\n", ret);
		return ret;
	}

#ifndef CONFIG_TCPC_CLASS
	g_oplus_chip->otg_online = false;
#endif

	g_bq->otg_enable = false;
	return ret;

}

int oplus_bq2560x_disable_te(void)
{
	return  bq2560x_enable_term(g_bq, false);
}

int oplus_bq2560x_get_chg_current_step(void)
{
	return REG02_ICHG_LSB;
}

int oplus_bq2560x_get_charger_type(void)
{
	return g_bq->oplus_chg_type;
}

#define BQ2560X_SUSPEND_AICR 100
#define BQ2560X_UNSUSPEND_JIFFIES 200
#define USB_TEMP_HIGH 0x01
int oplus_bq2560x_charger_suspend(void)
{
	int boot_mode = get_boot_mode();

	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
			g_oplus_chip->sub_chg_ops->charger_suspend();
		}
	}
	if (boot_mode == META_BOOT || ((oplus_get_usb_status() & USB_TEMP_HIGH) > 0)) {
		bq2560x_enter_hiz_mode(g_bq);
	} else {
		oplus_bq2560x_set_aicr(BQ2560X_SUSPEND_AICR);
		if (oplus_vooc_get_fastchg_to_normal() == false
			&& oplus_vooc_get_fastchg_to_warm() == false) {
			bq2560x_disable_charger(g_bq);
		}
	}

	return 0;
}

int oplus_bq2560x_charger_unsuspend(void)
{
	u8 val = 0;
	int ret = 0;
	int boot_mode = get_boot_mode();

	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
			g_oplus_chip->sub_chg_ops->charger_unsuspend();
		}
	}

	ret = bq2560x_get_hiz_mode(g_bq, &val);
	if ((boot_mode == META_BOOT || ((oplus_get_usb_status() & USB_TEMP_HIGH) == 0))
			&& ((ret == 0) && (val != 0))) {
		bq2560x_exit_hiz_mode(g_bq);
	}

	schedule_delayed_work(&g_bq->bq2560x_aicr_setting_work, msecs_to_jiffies(BQ2560X_UNSUSPEND_JIFFIES));

	if (g_oplus_chip) {
		if (oplus_vooc_get_fastchg_to_normal() == false
			&& oplus_vooc_get_fastchg_to_warm() == false) {
			if (g_oplus_chip->authenticate
				&& g_oplus_chip->mmi_chg
				&& !g_oplus_chip->balancing_bat_stop_chg
				&& (g_oplus_chip->charging_state != CHARGING_STATUS_FAIL)
				&& oplus_vooc_get_allow_reading()
				&& !oplus_is_rf_ftm_mode()) {
				chg_err("enable charger by update bits \n");
				bq2560x_enable_charger(g_bq);
			}
		}
	} else {
		bq2560x_enable_charger(g_bq);
	}

	return 0;
}

void oplus_bq2560x_really_charger_suspend(bool en)
{
	if (en) {
		bq2560x_enter_hiz_mode(g_bq);
	} else {
		bq2560x_exit_hiz_mode(g_bq);
	}
}

int oplus_bq2560x_set_rechg_vol(int vol)
{
	return 0;
}

int oplus_bq2560x_reset_charger(void)
{
	return 0;
}

bool oplus_bq2560x_check_charger_resume(void)
{
	return true;
}

void oplus_bq2560x_set_chargerid_switch_val(int value)
{
	return;
}

int oplus_bq2560x_get_chargerid_switch_val(void)
{
	return 0;
}

int oplus_bq2560x_get_chargerid_volt(void)
{
	return 0;
}



int oplus_bq2560x_get_charger_subtype(void)
{
	return oplus_chg_get_charger_subtype();
}

bool oplus_bq2560x_need_to_check_ibatt(void)
{
	return false;
}

int oplus_bq2560x_get_dyna_aicl_result(void)
{
	u8 val;
	int ret = 0;
	int ilim = 0;

	ret = bq2560x_read_byte(g_bq, &val, BQ2560X_REG_00);
	if (!ret) {
		ilim = val & REG00_IINLIM_MASK;
		ilim = (ilim >> REG00_IINLIM_SHIFT) * REG00_IINLIM_LSB;
		ilim = ilim + REG00_IINLIM_BASE;
	}

	return ilim;
}

bool oplus_bq2560x_get_shortc_hw_gpio_status(void)
{
	return true;
}

static void oplus_mt_power_off(void)
{
	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (g_oplus_chip->ac_online != true) {
		if(!oplus_bq2560x_check_chrdet_status())
			kernel_power_off();
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ac_online is true, return!\n", __func__);
	}
}

int oplus_bq2560x_get_pd_type(void)
{
	return oplus_chg_get_pd_type();
}

int oplus_bq2560x_pd_setup(void)
{
	return oplus_chg_pd_setup();
}

int oplus_bq2560x_set_qc_config(void)
{
	return false;
}

int oplus_bq2560x_enable_qc_detect(void)
{
	return 0;
}

int oplus_bq2560x_chg_set_high_vbus(bool en)
{
	return false;
}

static int oplus_bq2560x_get_rtc_spare_oplus_fg_value(void)
{
	return 0;
}

static int oplus_bq2560x_set_rtc_spare_oplus_fg_value(int soc)
{
	return 0;
}

static void bq2560x_init_work_handler(struct work_struct *work)
{
	int ret;
	u8 reg_val;
	if (oplus_bq2560x_check_chrdet_status()) {
		pr_notice("USB is inserted!\n");

		ret = bq2560x_read_byte(g_bq, &reg_val, BQ2560X_REG_08);
		if (ret) {
			pr_err("read BQ2560X_REG_08 error!\n");
			return;
		}

		if (g_bq->otg_enable) {
			pr_err("otg enable exit!\n");
			return;
		}

		g_bq->power_good = !!(reg_val & REG08_PG_STAT_MASK);

		mutex_lock(&g_bq->chgdet_en_lock);
		g_bq->psy_online = true;
		bq2560x_chgdet_en(g_bq, false);
		bq2560x_chgdet_en(g_bq, true);

		if (g_bq->chg_type == CHARGER_UNKNOWN) {
			bq2560x_bc12_detect(g_bq);
			bq2560x_inform_charger_type(g_bq);
		}
#ifdef CONFIG_OPLUS_CHARGER_MTK
		wakeup_fg_daemon(FG_INTR_CHARGER_IN, 0, 0);
#endif
		mutex_unlock(&g_bq->chgdet_en_lock);
	}
}

struct oplus_chg_operations  oplus_chg_bq2560x_ops = {
	.dump_registers = oplus_bq2560x_dump_registers,
	.kick_wdt = oplus_bq2560x_kick_wdt,
	.hardware_init = oplus_bq2560x_hardware_init,
	.charging_current_write_fast = oplus_bq2560x_set_ichg,
	.set_aicl_point = oplus_bq2560x_set_mivr,
	.input_current_write = oplus_bq2560x_set_input_current_limit,
	.float_voltage_write = oplus_bq2560x_set_cv,
	.term_current_set = oplus_bq2560x_set_ieoc,
	.charging_enable = oplus_bq2560x_charging_enable,
	.charging_disable = oplus_bq2560x_charging_disable,
	.get_charging_enable = oplus_bq2560x_is_charging_enabled,
	.charger_suspend = oplus_bq2560x_charger_suspend,
	.charger_unsuspend = oplus_bq2560x_charger_unsuspend,
	.really_suspend_charger = oplus_bq2560x_really_charger_suspend,
	.set_rechg_vol = oplus_bq2560x_set_rechg_vol,
	.reset_charger = oplus_bq2560x_reset_charger,
	.read_full = oplus_bq2560x_is_charging_done,
	.otg_enable = oplus_bq2560x_enable_otg,
	.otg_disable = oplus_bq2560x_disable_otg,
	.set_charging_term_disable = oplus_bq2560x_disable_te,
	.check_charger_resume = oplus_bq2560x_check_charger_resume,

	.get_charger_type = oplus_bq2560x_get_charger_type,
	.get_charger_volt = mt6357_get_vbus_voltage,
//	int (*get_charger_current)(void);
#ifdef CONFIG_OPLUS_CHARGER_MTK
	.get_chargerid_volt = mt_get_chargerid_volt,
	.set_chargerid_switch_val = mt_set_chargerid_switch_val,
	.get_chargerid_switch_val  = mt_get_chargerid_switch_val,
#else
	.get_chargerid_volt = oplus_bq2560x_get_chargerid_volt,
	.set_chargerid_switch_val = oplus_bq2560x_set_chargerid_switch_val,
	.get_chargerid_switch_val = oplus_bq2560x_get_chargerid_switch_val,
#endif
	.check_chrdet_status = oplus_bq2560x_check_chrdet_status,

	.get_boot_mode = (int (*)(void))oplus_get_boot_mode,
	.get_boot_reason = (int (*)(void))oplus_get_boot_reason,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = oplus_bq2560x_get_rtc_spare_oplus_fg_value,
	.set_rtc_soc = oplus_bq2560x_set_rtc_spare_oplus_fg_value,
	.set_power_off = oplus_mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
	.get_chg_current_step = oplus_bq2560x_get_chg_current_step,
	.need_to_check_ibatt = oplus_bq2560x_need_to_check_ibatt,
	.get_dyna_aicl_result = oplus_bq2560x_get_dyna_aicl_result,
	.get_shortc_hw_gpio_status = oplus_bq2560x_get_shortc_hw_gpio_status,
	.oplus_chg_get_pd_type = oplus_bq2560x_get_pd_type,
	.oplus_chg_pd_setup = oplus_bq2560x_pd_setup,
	.get_charger_subtype = oplus_bq2560x_get_charger_subtype,
	.set_qc_config = oplus_bq2560x_set_qc_config,
	.enable_qc_detect = oplus_bq2560x_enable_qc_detect,
	.oplus_chg_set_high_vbus = oplus_bq2560x_chg_set_high_vbus,
	.enable_shipmode = bq2560x_enable_shipmode,
	.oplus_chg_set_hz_mode = bq2560x_set_hz_mode,
	.get_usbtemp_volt = oplus_get_usbtemp_burn_volt,
	.set_typec_sinkonly = oplus_set_typec_sinkonly,
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
	.set_typec_cc_open = oplus_set_typec_cc_open,
};

static void aicr_setting_work_callback(struct work_struct *work)
{
	oplus_bq2560x_set_aicr(g_bq->aicr);
}

static void bq2560x_check_vbus_status_work_callback(struct work_struct *work)
{
	bool vbus_status = oplus_bq2560x_check_chrdet_status();
	pr_info("Start\n");

	if (!vbus_status) {
		oplus_vooc_reset_fastchg_after_usbout();
		if (oplus_vooc_get_fastchg_started() == false) {
			oplus_chg_set_chargerid_switch_val(0);
			oplus_chg_clear_chargerid_info();
		}
		oplus_chg_set_charger_type_unknown();
	}
	if (g_bq)
		bq2560x_inform_online_type(g_bq);
}

static void charging_current_setting_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	u32 uA = g_bq->chg_cur*1000;
	u32 temp_uA;
	int ret = 0;

	if(g_bq->chg_cur > BQ_CHARGER_CURRENT_MAX_MA) {
		uA = BQ_CHARGER_CURRENT_MAX_MA * 1000;
	}
	ret = bq2560x_set_chargecurrent(g_bq, uA/1000);
	if(ret) {
		pr_info("bq2560x set cur:%d %d failed\n", g_bq->chg_cur, uA);
	}
	pr_err("g_bq->chg_cur = %d, uA = %d\n", g_bq->chg_cur, uA);
}

#ifdef CONFIG_TCPC_CLASS
static int typec_attach_thread(void *data)
{
	struct bq2560x *bq = data;
	int ret = 0;
	bool attach;
	struct charger_device *chg_dev = NULL;
	union power_supply_propval val;

#ifdef OPLUS_FEATURE_CHG_BASIC
	unsigned int ms = 0;
#endif
	pr_info("++\n");
	while (!kthread_should_stop()) {
		wait_for_completion(&bq->chrdet_start);
#ifdef OPLUS_FEATURE_CHG_BASIC
		ms = bq->chgdet_mdelay;
#endif
		mutex_lock(&bq->attach_lock);
		attach = bq->typec_attach;
		mutex_unlock(&bq->attach_lock);

		val.intval = attach;
		power_supply_set_property(bq->psy,
						POWER_SUPPLY_PROP_ONLINE, &val);
	}
	return ret;
}

static void handle_typec_attach(struct bq2560x *bq,
				bool en)
{
	mutex_lock(&bq->attach_lock);
	bq->typec_attach = en;
	complete(&bq->chrdet_start);
	mutex_unlock(&bq->attach_lock);
}

static int pd_tcp_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct bq2560x *bq =
		(struct bq2560x *)container_of(nb,
		struct bq2560x, pd_nb);

	switch (event) {
	case TCP_NOTIFY_PD_STATE:
		pr_info("%s pd state = %d\n",
				    __func__, noti->pd_state.connected);
		switch (noti->pd_state.connected) {
		case PD_CONNECT_NONE:
			break;
		case PD_CONNECT_HARD_RESET:
			break;
		case PD_CONNECT_PE_READY_SNK:
		case PD_CONNECT_PE_READY_SNK_PD30:
		case PD_CONNECT_PE_READY_SNK_APDO:
		case PD_CONNECT_PE_READY_SRC:
		case PD_CONNECT_PE_READY_SRC_PD30:
			bq2560x_set_acovp_threshold(bq, VAC_OVP_10500);
			break;
		};
		break;
	case TCP_NOTIFY_SINK_VBUS:
		pr_info("%s sink vbus %dmV %dmA type(0x%02X)\n",
				    __func__, noti->vbus_state.mv,
				    noti->vbus_state.ma, noti->vbus_state.type);
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
		    (noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
		    noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC)) {
			pr_info("USB Plug in, pol = %d\n", noti->typec_state.polarity);
			pr_info("call switch_usb_state = 1 \n");
			ignore_bc_detect = false;
			if (oplus_vooc_get_fastchg_to_normal() ||
				oplus_vooc_get_fastchg_to_warm()) {
				chg_err("oplus_vooc_get_fastchg_to_normal or warm, not enable charger.\n");
			} else {
				bq->chgdet_mdelay = 450;
				handle_typec_attach(bq, true);
			}
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
			noti->typec_state.old_state == TYPEC_ATTACHED_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC)
			&& noti->typec_state.new_state == TYPEC_UNATTACHED) {
			pr_info("USB Plug out\n");
			if (bq->tcpc_kpoc) {
				pr_info("typec unattached, power off\n");
#ifndef OPLUS_FEATURE_CHG_BASIC
#ifdef FIXME
				kernel_power_off();
#endif
#endif /*OPLUS_FEATURE_CHG_BASIC*/
			}
			pr_info("call switch_usb_state = 0 \n");
			//switch_usb_state(0);
			bq->chgdet_mdelay = 0;
			ignore_bc_detect = false;
			handle_typec_attach(bq, false);
		} else if (noti->typec_state.old_state == TYPEC_ATTACHED_SRC &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SNK) {
			pr_info("Source_to_Sink\n");
			bq->chgdet_mdelay = 0;
			handle_typec_attach(bq, true);
			ignore_bc_detect = true;
		}  else if (noti->typec_state.old_state == TYPEC_ATTACHED_SNK &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			pr_info("Sink_to_Source\n");
                     bq->chgdet_mdelay = 0;
			handle_typec_attach(bq, false);
			ignore_bc_detect = true;
		}
		break;
	default:
		break;
	};
	return NOTIFY_OK;
}
//====pd_notifier_end=====

static int bq2560x_charger_get_online(struct bq2560x *bq,
				     bool *val)
{
	bool pwr_rdy = false;

	mutex_lock(&bq->attach_lock);
	pwr_rdy = bq->psy_online;
	mutex_unlock(&bq->attach_lock);

	pr_info("online = %d\n", pwr_rdy);
	*val = pwr_rdy;
	return 0;
}

static int bq2560x_charger_set_online(struct bq2560x *bq,
				     const union power_supply_propval *val)
{
	return bq2560x_enable_chgdet(bq->chg_dev, val->intval);
}

static int bq2560x_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct bq2560x *bq = power_supply_get_drvdata(psy);
	enum bq2560x_charging_status chg_stat = BQ2560X_CHG_STATUS_NOT_CHARGING;
	bool pwr_rdy = false, chg_en = false;
	int ret = 0;

	pr_info("prop = %d\n", psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq2560x_charger_get_online(bq, &pwr_rdy);
		val->intval = pwr_rdy;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq2560x_charger_get_online(bq, &pwr_rdy);
		ret = bq2560x_is_charging_enable(bq->chg_dev, &chg_en);
		ret = bq2560x_get_charging_status(bq, &chg_stat);
		if (!pwr_rdy) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			return ret;
		}
		switch (chg_stat) {
		case BQ2560X_CHG_STATUS_PRE_CHARGING:
			/* fallthrough */
		case BQ2560X_CHG_STATUS_FAST_CHARGING:
			if (chg_en)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case BQ2560X_CHG_STATUS_DONE:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		case BQ2560X_CHG_STATUS_NOT_CHARGING:
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		default:
			ret = -ENODATA;
			break;
		}
		break;
	case POWER_SUPPLY_PROP_TYPE:
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = bq->oplus_chg_type;
		break;
	default:
		ret = -ENODATA;
	}
	return ret;
}

static int bq2560x_charger_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct bq2560x *bq = power_supply_get_drvdata(psy);
	int ret;

	pr_info("prop = %d\n", psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq2560x_charger_set_online(bq, val);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int bq2560x_charger_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property bq2560x_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static const struct power_supply_desc bq2560x_charger_desc = {
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= bq2560x_charger_properties,
	.num_properties		= ARRAY_SIZE(bq2560x_charger_properties),
	.get_property		= bq2560x_charger_get_property,
	.set_property		= bq2560x_charger_set_property,
	.property_is_writeable	= bq2560x_charger_property_is_writeable,
	.usb_types		= bq2560x_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(bq2560x_charger_usb_types),
};

static char *bq2560x_charger_supplied_to[] = {
	"battery",
	"mtk-master-charger"
};
#endif

static int bq2560x_charger_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct bq2560x *bq;
	struct oplus_gauge_chip	*chip;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
#ifdef CONFIG_TCPC_CLASS
	struct power_supply_config charger_cfg = {};
#endif
	int ret = 0;


	bq = devm_kzalloc(&client->dev, sizeof(struct bq2560x), GFP_KERNEL);
	if (!bq) {
		pr_err("Out of memory\n");
		return -ENOMEM;
	}

	bq->dev = &client->dev;
	bq->client = client;
	g_bq = bq;

	i2c_set_clientdata(client, bq);

	mutex_init(&bq->i2c_rw_lock);
#ifdef CONFIG_TCPC_CLASS
	mutex_init(&bq->attach_lock);
#endif
	mutex_init(&bq->chgdet_en_lock);

	// Initialize gpio to enable sub charger IC bq2560x
	ret = bq2560x_charger_ic_enable_gpio(bq);
	if (ret < 0)
		pr_err("BQ2560X charger ic gpio enabled failed, ret:%d", ret);

	ret = bq2560x_detect_device(bq);
	if(ret) {
		pr_err("No bq2560x device found!\n");
		return -ENODEV;
	}

	bq->platform_data = bq2560x_parse_dt(node, bq);

	if (!bq->platform_data) {
		pr_err("No platform data provided.\n");
		return -EINVAL;
	}

	ret = bq2560x_init_device(bq);
	if (ret) {
		pr_err("Failed to init device\n");
		goto err_0;
	}

	ret = bq2560x_init_device(bq);
	if (ret) {
		pr_err("Failed to init device\n");
		return ret;
	}

	bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	bq->pre_current_ma = -1;
	register_charger_devinfo(bq);

#ifndef CONFIG_TCPC_CLASS
	if (strcmp(bq->chg_dev_name, "primary_chg") == 0) {
		Charger_Detect_Init();
	}
#endif
	ret = bq2560x_exit_hiz_mode(bq);
	bq2560x_update_bits(bq, BQ2560X_REG_07, REG07_BATFET_RST_EN_MASK,
						REG07_BATFET_RST_DISABLE);

#ifdef CONFIG_TCPC_CLASS
	/* power supply register */
	memcpy(&bq->psy_desc,
		&bq2560x_charger_desc, sizeof(bq->psy_desc));
	bq->psy_desc.name = "bq2560x";

	charger_cfg.drv_data = bq;
	charger_cfg.of_node = client->dev.of_node;
	charger_cfg.supplied_to = bq2560x_charger_supplied_to;
	charger_cfg.num_supplicants = ARRAY_SIZE(bq2560x_charger_supplied_to);
	bq->psy = devm_power_supply_register(&client->dev,
					&bq->psy_desc, &charger_cfg);
	if (IS_ERR_OR_NULL(bq->psy)) {
		pr_notice("Fail to register power supply dev\n");
		ret = PTR_ERR(bq->psy);
		goto err_register_psy;
	}
#endif

	INIT_DELAYED_WORK(&bq->bq2560x_aicr_setting_work, aicr_setting_work_callback);
	INIT_DELAYED_WORK(&bq->bq2560x_current_setting_work, charging_current_setting_work);
	INIT_DELAYED_WORK(&bq->init_work, bq2560x_init_work_handler);
	INIT_DELAYED_WORK(&bq->bq2560x_check_vbus_status_work, bq2560x_check_vbus_status_work_callback);

	bq->chg_dev = charger_device_register(bq->chg_dev_name,
							&client->dev, bq,
							&bq2560x_chg_ops,
							&bq2560x_chg_props);
	if (IS_ERR_OR_NULL(bq->chg_dev)) {
		ret = PTR_ERR(bq->chg_dev);
		goto err_0;
	}

	if (strcmp(bq->chg_dev_name, "primary_chg") == 0) {
		bq2560x_register_interrupt(node,bq);
	}

	ret = sysfs_create_group(&bq->dev->kobj, &bq2560x_attr_group);
	if (ret) {
		pr_err("failed to register sysfs. err: %d\n", ret);
	}

	first_connect = true;
	
	if (strcmp(bq->chg_dev_name, "primary_chg") == 0) {
		schedule_delayed_work(&bq->init_work, msecs_to_jiffies(14000));
#ifdef CONFIG_TCPC_CLASS
		init_completion(&bq->chrdet_start);
		mutex_init(&bq->attach_lock);

		bq->attach_task = kthread_run(typec_attach_thread, bq,
					"attach_thread");
		if (IS_ERR(bq->attach_task)) {
			ret = PTR_ERR(bq->attach_task);
			goto err_attach_task;
		}

		bq->tcpc = tcpc_dev_get_by_name("type_c_port0");
		if (!bq->tcpc) {
			pr_err("get tcpc device type_c_port0 fail\n");
			ret = -ENODEV;
			goto err_get_tcpcdev;
		}

		bq->pd_nb.notifier_call = pd_tcp_notifier_call;
		ret = register_tcp_dev_notifier(bq->tcpc, &bq->pd_nb,
					TCP_NOTIFY_TYPE_ALL);
		if (ret < 0) {
			pr_notice("register tcpc notifer fail\n");
			ret = -EINVAL;
			goto err_register_tcp_notifier;
		}
#endif
	}

	set_charger_ic(BQ2560X);

	pr_err("bq2560x probe successfully, Part Num:%d, Revision:%d\n!",
				bq->part_no, bq->revision);

	return 0;

#ifdef CONFIG_TCPC_CLASS
err_register_tcp_notifier:
err_get_tcpcdev:
	if (bq->attach_task)
		kthread_stop(bq->attach_task);
err_attach_task:
err_register_psy:
#endif
err_0:

	return ret;
}

static int bq2560x_charger_remove(struct i2c_client *client)
{
	struct bq2560x *bq = i2c_get_clientdata(client);


	mutex_destroy(&bq->i2c_rw_lock);
	mutex_destroy(&bq->chgdet_en_lock);

	sysfs_remove_group(&bq->dev->kobj, &bq2560x_attr_group);


	return 0;
}


static void bq2560x_charger_shutdown(struct i2c_client *client)
{
	oplus_bq2560x_disable_otg();
}

static struct of_device_id bq2560x_charger_match_table[] = {
	{.compatible = "ti,bq25601",},
	{},
};
MODULE_DEVICE_TABLE(of,bq2560x_charger_match_table);

static const struct i2c_device_id bq2560x_charger_id[] = {
	{ "bq25601-charger", BQ25601 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq2560x_charger_id);

static struct i2c_driver bq2560x_charger_driver = {
	.driver 	= {
		.name 	= "bq2560x-charger",
		.owner 	= THIS_MODULE,
		.of_match_table = bq2560x_charger_match_table,
	},
	.id_table	= bq2560x_charger_id,

	.probe		= bq2560x_charger_probe,
	.remove		= bq2560x_charger_remove,
	.shutdown	= bq2560x_charger_shutdown,

};

module_i2c_driver(bq2560x_charger_driver);

MODULE_DESCRIPTION("TI BQ2560x Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");
