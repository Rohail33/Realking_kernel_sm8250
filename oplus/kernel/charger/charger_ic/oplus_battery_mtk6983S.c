// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/mfd/mt6397/core.h>/* PMIC MFD core header */
#include <linux/regmap.h>
#include <linux/of_platform.h>

#include <asm/setup.h>

//#include "mtk_charger.h"
#include "oplus_battery_mtk6983S.h"
#ifndef OPLUS_FEATURE_CHG_BASIC
#include "mtk_battery.h"
#endif


#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/iio/consumer.h>
#include "op_charge.h"
#include <tcpm.h>
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_adapter.h"
#include "../oplus_short.h"
#include "../oplus_configfs.h"
#include "../oplus_chg_track.h"

#include "../oplus_chg_ops_manager.h"
#include "../oplus_chg_module.h"
#include "../oplus_chg_comm.h"
#include "../oplus_wireless.h"
#include "../op_wlchg_v2/oplus_chg_wls.h"
#include "../oplus_pps.h"
#include "oplus_mp2650.h"
#include "../wireless_ic/oplus_p9418.h"

#include "oplus_charge_pump.h"
#include "../voocphy/oplus_voocphy.h"
#include "../voocphy/oplus_sc8547.h"
#include <linux/rtc.h>
#include <linux/thermal.h>
#include "charger_ic/oplus_switching.h"
#ifdef CONFIG_OPLUS_CHARGER_OPTIGA
#include "../gauge_ic/oplus_optiga/oplus_optiga.h"
#endif

struct oplus_chg_chip *g_oplus_chip = NULL;
static struct mtk_charger *pinfo;
static struct task_struct *oplus_usbtemp_kthread;
static bool probe_done;
bool is_mtksvooc_project = false;

struct mtk_hv_flashled_pinctrl {
	int chgvin_gpio;
	int pmic_chgfunc_gpio;
	int bc1_2_done;
	bool hv_flashled_support;
	struct mutex chgvin_mutex;

	struct pinctrl *pinctrl;
	struct pinctrl_state *chgvin_enable;
	struct pinctrl_state *chgvin_disable;
	struct pinctrl_state *pmic_chgfunc_enable;
	struct pinctrl_state *pmic_chgfunc_disable;
};

struct mtk_hv_flashled_pinctrl mtkhv_flashled_pinctrl;

#define USB_TEMP_HIGH		0x01//bit0
#define USB_WATER_DETECT	0x02//bit1
#define USB_RESERVE2		0x04//bit2
#define USB_RESERVE3		0x08//bit3
#define USB_RESERVE4		0x10//bit4
#define USB_DONOT_USE		0x80000000
#define DEFAULT_FLOAT_VOLTAGE	4385
#define DEFAULT_CHARGING_CURR	512
#define DEFAULT_TERM_CURR	150
#define DEFAULT_AICL_RESULT	500
#define DEFAULT_POWER_OFF_FLOAT_VOLTAGE	4400
#define MT6375_CURR_STEP	100

#define AICL_POINT_VOL_9V 7600
#define AICL_POINT_VOL_5V 4140
#define HW_AICL_POINT_VOL_5V_PHASE1 4440
#define HW_AICL_POINT_VOL_5V_PHASE2 4520
#define SW_AICL_POINT_VOL_5V_PHASE1 4500
#define SW_AICL_POINT_VOL_5V_PHASE2 4535

#define CHARGER_VOLT_HV		7500
#define CHARGER_VOLT_NORMAL	6500
#define HIGH_SOC		90
#define HIGH_TEMP		530

static int usb_status = 0;

#define POWER_SUPPLY_TYPE_USB_HVDCP 13
#define POWER_SUPPLY_TYPE_USB_HVDCP_3 14

#define CCDETECT_DELAY_MS	50
#define WD0_GET_STATUS_DELAY_MS	500
#define MT6375_OTG_CURRENT_LIMIT_1300MA (1300000)

#define DEFUALT_TEMP		25
#define BTB_25C_VOLT		918 /* mv */
#define BTB_CRITICAL_LOW	4251000
#define BTB_PU_R		100000
#define BTB_PU_VOLTAGE		1836
#define BTB_TEMP_MIN		(-400)
#define BTB_TEMP_MAX		1250
#define BTB_OFFSET		(-5) /* mv */
#define NTC_PARAM_NUM		7
#define BTB_HIGH_TEMP_AGING 690

struct delayed_work ccdetect_work ;
struct delayed_work wd0_detect_work ;
struct delayed_work wd0_get_status_work;

extern struct oplus_chg_operations * oplus_get_chg_ops(void);
extern void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip);
extern int oplus_usbtemp_monitor_common(void *data);
extern int oplus_usbtemp_monitor_common_new_method(void *data);
extern int op10_subsys_init(void);
extern int charge_pump_driver_init(void);
extern int mp2650_driver_init(void);
extern int bq27541_driver_init(void);
extern int adapter_ic_init(void);
extern int hl7227_driver_init(void);
extern nu1619_driver_init(void);
void oplus_ccdetect_disable(void);
void oplus_ccdetect_enable(void);
void oplus_wake_up_usbtemp_thread(void);
bool oplus_usbtemp_condition(void);
bool oplus_get_otg_switch_status(void);
static bool is_vooc_project(void);
int oplus_chg_get_charger_subtype(void);
extern void oplus_set_boost_en_val(int value);
extern void oplus_set_ext1_otg_en_val(int value);

extern int mt6375_get_hvdcp_type(void);
extern void mt6375_enable_hvdcp_detect(void);
extern int mt6375_set_hvdcp_to_5v(void);
extern int mt6375_set_hvdcp_to_9v(void);
extern void oplus_notify_hvdcp_detect_stat(void);
extern void oplus_set_hvdcp_flag_clear(void);

struct oplus_chg_operations  mtk6375_chg_ops;
static int oplus_mt6375_check_charging_enable(void);
static int oplus_mt6375_disable_charging(void);

#if 0
static void oplus_set_usb_status(int status)
{
	usb_status = usb_status | status;
}

static void oplus_clear_usb_status(int status)
{
	if( g_oplus_chip->usb_status == USB_TEMP_HIGH) {
		g_oplus_chip->usb_status = g_oplus_chip->usb_status & (~USB_TEMP_HIGH);
	}
	usb_status = usb_status & (~status);
}
#endif
int oplus_get_usb_status(void)
{
	if( g_oplus_chip->usb_status == USB_TEMP_HIGH) {
		return g_oplus_chip->usb_status;
	} else {
		return usb_status;
	}
}

int is_vooc_cfg = false;
static bool is_vooc_project(void)
{
	return is_vooc_cfg;
}

void __attribute__((weak)) oplus_set_wrx_otg_value(void)
{
	return;
}

int __attribute__((weak)) oplus_get_idt_en_val(void)
{
	return -1;
}

int __attribute__((weak)) oplus_get_wrx_en_val(void)
{
	return -1;
}

int __attribute__((weak)) oplus_get_wrx_otg_val(void)
{
	return 0;
}

void __attribute__((weak)) oplus_wireless_set_otg_en_val(void)
{
	return;
}

void __attribute__((weak)) oplus_dcin_irq_enable(void)
{
	return;
}

__maybe_unused static bool is_wls_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->wls_ocm)
		chip->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!chip->wls_ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->comm_ocm)
		chip->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!chip->comm_ocm;
}

__attribute__((weak)) bool tcpci_get_wd0_status(void);

int __attribute__((weak)) oplus_chg_set_shipping_mode(void);

bool oplus_get_wired_otg_online(void)
{
	return false;
}

bool oplus_get_wired_chg_present(void)
{
	union power_supply_propval prop;
	struct power_supply *psy = NULL;

	if (!pinfo) {
		pr_err("%s, pinfo null!!\n", __func__);
		return false;
	}

	psy = pinfo->chg_psy;
	if (psy == NULL || IS_ERR(psy)) {
		pr_err("%s retry to get chg_psy\n", __func__);
		psy = devm_power_supply_get_by_phandle(&pinfo->pdev->dev, "charger");
		pinfo->chg_psy = psy;
	}

	if (psy == NULL || IS_ERR(psy)) {
		pr_err("%s Couldn't get chg_psy\n", __func__);
		return false;
	} else {
		power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &prop);
		if (!!prop.intval) {
			return true;
		} else {
			return false;
		}
	}
}
#endif

int get_uisoc(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret;

	bat_psy = info->bat_psy;

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 50;
	} else {
		ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_CAPACITY, &prop);
		ret = prop.intval;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_battery_voltage(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret;

	bat_psy = info->bat_psy;

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 3999;
	} else {
		ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
		ret = prop.intval / 1000;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_battery_temperature(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret = 0;
	int tmp_ret = 0;

	bat_psy = info->bat_psy;

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 27;
	} else {
		tmp_ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
		ret = prop.intval / 10;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_battery_current(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret = 0;
	int tmp_ret = 0;

	bat_psy = info->bat_psy;

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 0;
	} else {
		tmp_ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &prop);
		ret = prop.intval / 1000;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

static int get_pmic_vbus(struct mtk_charger *info, int *vchr)
{
	union power_supply_propval prop;
	static struct power_supply *chg_psy;
	int ret;

	if (chg_psy == NULL)
		chg_psy = power_supply_get_by_name("mtk_charger_type");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		ret = -1;
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
	}
	*vchr = prop.intval;

	chr_debug("%s vbus:%d\n", __func__,
		prop.intval);
	return ret;
}

int get_vbus(struct mtk_charger *info)
{
	int ret = 0;
	int vchr = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_vbus(info->chg1_dev, &vchr);
	if (ret < 0) {
		ret = get_pmic_vbus(info, &vchr);
		if (ret < 0)
			chr_err("%s: get vbus failed: %d\n", __func__, ret);
	} else
		vchr /= 1000;

	return vchr;
}

int get_ibat(struct mtk_charger *info)
{
	int ret = 0;
	int ibat = 0;

	if (info == NULL)
		return -EINVAL;
	ret = charger_dev_get_ibat(info->chg1_dev, &ibat);
	if (ret < 0)
		chr_err("%s: get ibat failed: %d\n", __func__, ret);

	return ibat / 1000;
}

int get_ibus(struct mtk_charger *info)
{
	int ret = 0;
	int ibus = 0;

	if (info == NULL)
		return -EINVAL;
	ret = charger_dev_get_ibus(info->chg1_dev, &ibus);
	if (ret < 0)
		chr_err("%s: get ibus failed: %d\n", __func__, ret);

	return ibus / 1000;
}

bool is_battery_exist(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret = 0;
	int tmp_ret = 0;

	bat_psy = info->bat_psy;

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 1;
	} else {
		tmp_ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_PRESENT, &prop);
		ret = prop.intval;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

bool is_charger_exist(struct mtk_charger *info)
{
	union power_supply_propval prop;
	static struct power_supply *chg_psy;
	int ret;
	int tmp_ret = 0;

	chg_psy = info->chg_psy;

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s retry to get chg_psy\n", __func__);
		chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "charger");
		info->chg_psy = chg_psy;
	}

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);
		ret = -1;
	} else {
		tmp_ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		ret = prop.intval;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_type(struct mtk_charger *info)
{
	union power_supply_propval prop, prop2, prop3;
	static struct power_supply *chg_psy;
	int ret;

	chg_psy = info->chg_psy;

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s retry to get chg_psy\n", __func__);
		chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "charger");
		info->chg_psy = chg_psy;
	}

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);

		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_TYPE, &prop2);

		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop3);

		if (prop.intval == 0 ||
		    (prop2.intval == POWER_SUPPLY_TYPE_USB &&
		    prop3.intval == POWER_SUPPLY_USB_TYPE_UNKNOWN))
			prop2.intval = POWER_SUPPLY_TYPE_UNKNOWN;
	}

	chr_debug("%s online:%d type:%d usb_type:%d\n", __func__,
		prop.intval,
		prop2.intval,
		prop3.intval);

	return prop2.intval;
}

int get_usb_type(struct mtk_charger *info)
{
	union power_supply_propval prop, prop2;
	static struct power_supply *chg_psy;
	int ret;

	chg_psy = info->chg_psy;

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s retry to get chg_psy\n", __func__);
		chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev,
						       "charger");
		info->chg_psy = chg_psy;
	}
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop2);
	}
	chr_debug("%s online:%d usb_type:%d\n", __func__,
		prop.intval,
		prop2.intval);
	return prop2.intval;
}

int get_charger_temperature(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int tchg_min = 0, tchg_max = 0;

	if (info == NULL)
		return 0;

	ret = charger_dev_get_temperature(chg, &tchg_min, &tchg_max);
	if (ret < 0)
		chr_err("%s: get temperature failed: %d\n", __func__, ret);
	else
		ret = (tchg_max + tchg_min) / 2;

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_charging_current(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int olduA = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_charging_current(chg, &olduA);
	if (ret < 0)
		chr_err("%s: get charging current failed: %d\n", __func__, ret);
	else
		ret = olduA;

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_input_current(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int olduA = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_input_current(chg, &olduA);
	if (ret < 0)
		chr_err("%s: get input current failed: %d\n", __func__, ret);
	else
		ret = olduA;

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_zcv(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int zcv = 0;

	if (info == NULL)
		return 0;

	ret = charger_dev_get_zcv(chg, &zcv);
	if (ret < 0)
		chr_err("%s: get charger zcv failed: %d\n", __func__, ret);
	else
		ret = zcv;
	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

#define PMIC_RG_VCDT_HV_EN_ADDR		0xb88
#define PMIC_RG_VCDT_HV_EN_MASK		0x1
#define PMIC_RG_VCDT_HV_EN_SHIFT	11

static void pmic_set_register_value(struct regmap *map,
	unsigned int addr,
	unsigned int mask,
	unsigned int shift,
	unsigned int val)
{
	regmap_update_bits(map,
		addr,
		mask << shift,
		val << shift);
}

unsigned int pmic_get_register_value(struct regmap *map,
	unsigned int addr,
	unsigned int mask,
	unsigned int shift)
{
	unsigned int value = 0;

	regmap_read(map, addr, &value);
	value =
		(value &
		(mask << shift))
		>> shift;
	return value;
}

int disable_hw_ovp(struct mtk_charger *info, int en)
{
	struct device_node *pmic_node;
	struct platform_device *pmic_pdev;
	struct mt6397_chip *chip;
	struct regmap *regmap;

	pmic_node = of_parse_phandle(info->pdev->dev.of_node, "pmic", 0);
	if (!pmic_node) {
		chr_err("get pmic_node fail\n");
		return -1;
	}

	pmic_pdev = of_find_device_by_node(pmic_node);
	if (!pmic_pdev) {
		chr_err("get pmic_pdev fail\n");
		return -1;
	}
	chip = dev_get_drvdata(&(pmic_pdev->dev));

	if (!chip) {
		chr_err("get chip fail\n");
		return -1;
	}

	regmap = chip->regmap;

	pmic_set_register_value(regmap,
		PMIC_RG_VCDT_HV_EN_ADDR,
		PMIC_RG_VCDT_HV_EN_SHIFT,
		PMIC_RG_VCDT_HV_EN_MASK,
		en);

	return 0;
}

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

#ifdef MODULE
static char __chg_cmdline[COMMAND_LINE_SIZE];
static char *chg_cmdline = __chg_cmdline;

const char *chg_get_cmd(void)
{
	struct device_node * of_chosen = NULL;
	char *bootargs = NULL;

	if (__chg_cmdline[0] != 0)
		return chg_cmdline;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(
					of_chosen, "bootargs", NULL);
		if (!bootargs)
			chr_err("%s: failed to get bootargs\n", __func__);
		else {
			strcpy(__chg_cmdline, bootargs);
			chr_err("%s: bootargs: %s\n", __func__, bootargs);
		}
	} else
		chr_err("%s: failed to get /chosen \n", __func__);

	return chg_cmdline;
}

#else
const char *chg_get_cmd(void)
{
	return saved_command_line;
}
#endif

int chr_get_debug_level(void)
{
	struct power_supply *psy;
	static struct mtk_charger *info;
	int ret;

	if (info == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL)
			ret = CHRLOG_DEBUG_LEVEL;
		else {
			info =
			(struct mtk_charger *)power_supply_get_drvdata(psy);
			if (info == NULL)
				ret = CHRLOG_DEBUG_LEVEL;
			else
				ret = info->log_level;
		}
	} else
		ret = info->log_level;

	return ret;
}
EXPORT_SYMBOL(chr_get_debug_level);

void _wake_up_charger(struct mtk_charger *info)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
/* do not use mtk charger thread */
	return;
#else

	unsigned long flags;

	info->timer_cb_duration[2] = ktime_get_boottime();
	if (info == NULL)
		return;

	spin_lock_irqsave(&info->slock, flags);
	info->timer_cb_duration[3] = ktime_get_boottime();
	if (!info->charger_wakelock->active)
		__pm_stay_awake(info->charger_wakelock);
	info->timer_cb_duration[4] = ktime_get_boottime();
	spin_unlock_irqrestore(&info->slock, flags);
	info->charger_thread_timeout = true;
	info->timer_cb_duration[5] = ktime_get_boottime();
	wake_up_interruptible(&info->wait_que);
#endif
}

bool is_disable_charger(struct mtk_charger *info)
{
	if (info == NULL)
		return true;

	if (info->disable_charger == true || IS_ENABLED(CONFIG_POWER_EXT))
		return true;
	else
		return false;
}

int _mtk_enable_charging(struct mtk_charger *info,
	bool en)
{
	chr_debug("%s en:%d\n", __func__, en);
	if (info->algo.enable_charging != NULL)
		return info->algo.enable_charging(info, en);
	return false;
}

int mtk_charger_notifier(struct mtk_charger *info, int event)
{
	return srcu_notifier_call_chain(&info->evt_nh, event, NULL);
}

static void mtk_charger_parse_dt(struct mtk_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val = 0;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;

	boot_node = of_parse_phandle(dev->of_node, "bootmode", 0);
	if (!boot_node)
		chr_err("%s: failed to get boot mode phandle\n", __func__);
	else {
		tag = (struct tag_bootmode *)of_get_property(boot_node,
							"atag,boot", NULL);
		if (!tag)
			chr_err("%s: failed to get atag,boot\n", __func__);
		else {
			chr_err("%s: size:0x%x tag:0x%x bootmode:0x%x boottype:0x%x\n",
				__func__, tag->size, tag->tag,
				tag->bootmode, tag->boottype);
			info->bootmode = tag->bootmode;
			info->boottype = tag->boottype;
		}
	}

	if (of_property_read_string(np, "algorithm_name",
		&info->algorithm_name) < 0) {
		chr_err("%s: no algorithm_name name\n", __func__);
		info->algorithm_name = "Basic";
	}

#ifndef OPLUS_FEATURE_CHG_BASIC
	if (strcmp(info->algorithm_name, "Basic") == 0) {
		chr_err("found Basic\n");
		mtk_basic_charger_init(info);
	} else if (strcmp(info->algorithm_name, "Pulse") == 0) {
		chr_err("found Pulse\n");
		mtk_pulse_charger_init(info);
	}
#endif

	info->disable_charger = of_property_read_bool(np, "disable_charger");
	info->atm_enabled = of_property_read_bool(np, "atm_is_enabled");
	info->enable_sw_safety_timer =
			of_property_read_bool(np, "enable_sw_safety_timer");
	info->sw_safety_timer_setting = info->enable_sw_safety_timer;

	/* common */

	if (of_property_read_u32(np, "charger_configuration", &val) >= 0)
		info->config = val;
	else {
		chr_err("use default charger_configuration:%d\n",
			SINGLE_CHARGER);
		info->config = SINGLE_CHARGER;
	}

	if (of_property_read_u32(np, "battery_cv", &val) >= 0)
		info->data.battery_cv = val;
	else {
		chr_err("use default BATTERY_CV:%d\n", BATTERY_CV);
		info->data.battery_cv = BATTERY_CV;
	}

	if (of_property_read_u32(np, "max_charger_voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.max_charger_voltage = V_CHARGER_MAX;
	}
	info->data.max_charger_voltage_setting = info->data.max_charger_voltage;

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MIN:%d\n", V_CHARGER_MIN);
		info->data.min_charger_voltage = V_CHARGER_MIN;
	}
	info->enable_vbat_mon = of_property_read_bool(np, "enable_vbat_mon");
	if (info->enable_vbat_mon == true)
		info->setting.vbat_mon_en = true;
	chr_err("use 6pin bat, enable_vbat_mon:%d\n", info->enable_vbat_mon);

	/* sw jeita */
	info->enable_sw_jeita = of_property_read_bool(np, "enable_sw_jeita");
	if (of_property_read_u32(np, "jeita_temp_above_t4_cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_CV:%d\n",
			JEITA_TEMP_ABOVE_T4_CV);
		info->data.jeita_temp_above_t4_cv = JEITA_TEMP_ABOVE_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_CV:%d\n",
			JEITA_TEMP_T3_TO_T4_CV);
		info->data.jeita_temp_t3_to_t4_cv = JEITA_TEMP_T3_TO_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_CV:%d\n",
			JEITA_TEMP_T2_TO_T3_CV);
		info->data.jeita_temp_t2_to_t3_cv = JEITA_TEMP_T2_TO_T3_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_CV:%d\n",
			JEITA_TEMP_T1_TO_T2_CV);
		info->data.jeita_temp_t1_to_t2_cv = JEITA_TEMP_T1_TO_T2_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_CV:%d\n",
			JEITA_TEMP_T0_TO_T1_CV);
		info->data.jeita_temp_t0_to_t1_cv = JEITA_TEMP_T0_TO_T1_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_CV:%d\n",
			JEITA_TEMP_BELOW_T0_CV);
		info->data.jeita_temp_below_t0_cv = JEITA_TEMP_BELOW_T0_CV;
	}

	if (of_property_read_u32(np, "temp_t4_thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else {
		chr_err("use default TEMP_T4_THRES:%d\n",
			TEMP_T4_THRES);
		info->data.temp_t4_thres = TEMP_T4_THRES;
	}

	if (of_property_read_u32(np, "temp_t4_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T4_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T4_THRES_MINUS_X_DEGREE);
		info->data.temp_t4_thres_minus_x_degree =
					TEMP_T4_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t3_thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else {
		chr_err("use default TEMP_T3_THRES:%d\n",
			TEMP_T3_THRES);
		info->data.temp_t3_thres = TEMP_T3_THRES;
	}

	if (of_property_read_u32(np, "temp_t3_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T3_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T3_THRES_MINUS_X_DEGREE);
		info->data.temp_t3_thres_minus_x_degree =
					TEMP_T3_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t2_thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else {
		chr_err("use default TEMP_T2_THRES:%d\n",
			TEMP_T2_THRES);
		info->data.temp_t2_thres = TEMP_T2_THRES;
	}

	if (of_property_read_u32(np, "temp_t2_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T2_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T2_THRES_PLUS_X_DEGREE);
		info->data.temp_t2_thres_plus_x_degree =
					TEMP_T2_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t1_thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else {
		chr_err("use default TEMP_T1_THRES:%d\n",
			TEMP_T1_THRES);
		info->data.temp_t1_thres = TEMP_T1_THRES;
	}

	if (of_property_read_u32(np, "temp_t1_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T1_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T1_THRES_PLUS_X_DEGREE);
		info->data.temp_t1_thres_plus_x_degree =
					TEMP_T1_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t0_thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else {
		chr_err("use default TEMP_T0_THRES:%d\n",
			TEMP_T0_THRES);
		info->data.temp_t0_thres = TEMP_T0_THRES;
	}

	if (of_property_read_u32(np, "temp_t0_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T0_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T0_THRES_PLUS_X_DEGREE);
		info->data.temp_t0_thres_plus_x_degree =
					TEMP_T0_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_neg_10_thres", &val) >= 0)
		info->data.temp_neg_10_thres = val;
	else {
		chr_err("use default TEMP_NEG_10_THRES:%d\n",
			TEMP_NEG_10_THRES);
		info->data.temp_neg_10_thres = TEMP_NEG_10_THRES;
	}

	/* battery temperature protection */
	info->thermal.sm = BAT_TEMP_NORMAL;
	info->thermal.enable_min_charge_temp =
		of_property_read_bool(np, "enable_min_charge_temp");

	if (of_property_read_u32(np, "min_charge_temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else {
		chr_err("use default MIN_CHARGE_TEMP:%d\n",
			MIN_CHARGE_TEMP);
		info->thermal.min_charge_temp = MIN_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "min_charge_temp_plus_x_degree", &val)
		>= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else {
		chr_err("use default MIN_CHARGE_TEMP_PLUS_X_DEGREE:%d\n",
			MIN_CHARGE_TEMP_PLUS_X_DEGREE);
		info->thermal.min_charge_temp_plus_x_degree =
					MIN_CHARGE_TEMP_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "max_charge_temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else {
		chr_err("use default MAX_CHARGE_TEMP:%d\n",
			MAX_CHARGE_TEMP);
		info->thermal.max_charge_temp = MAX_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "max_charge_temp_minus_x_degree", &val)
		>= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else {
		chr_err("use default MAX_CHARGE_TEMP_MINUS_X_DEGREE:%d\n",
			MAX_CHARGE_TEMP_MINUS_X_DEGREE);
		info->thermal.max_charge_temp_minus_x_degree =
					MAX_CHARGE_TEMP_MINUS_X_DEGREE;
	}

	/* charging current */
	if (of_property_read_u32(np, "usb_charger_current", &val) >= 0) {
		info->data.usb_charger_current = val;
	} else {
		chr_err("use default USB_CHARGER_CURRENT:%d\n",
			USB_CHARGER_CURRENT);
		info->data.usb_charger_current = USB_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_current", &val) >= 0) {
		info->data.ac_charger_current = val;
	} else {
		chr_err("use default AC_CHARGER_CURRENT:%d\n",
			AC_CHARGER_CURRENT);
		info->data.ac_charger_current = AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_input_current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else {
		chr_err("use default AC_CHARGER_INPUT_CURRENT:%d\n",
			AC_CHARGER_INPUT_CURRENT);
		info->data.ac_charger_input_current = AC_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "charging_host_charger_current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else {
		chr_err("use default CHARGING_HOST_CHARGER_CURRENT:%d\n",
			CHARGING_HOST_CHARGER_CURRENT);
		info->data.charging_host_charger_current =
					CHARGING_HOST_CHARGER_CURRENT;
	}

	/* dynamic mivr */
	info->enable_dynamic_mivr =
			of_property_read_bool(np, "enable_dynamic_mivr");

	if (of_property_read_u32(np, "min_charger_voltage_1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else {
		chr_err("use default V_CHARGER_MIN_1: %d\n", V_CHARGER_MIN_1);
		info->data.min_charger_voltage_1 = V_CHARGER_MIN_1;
	}

	if (of_property_read_u32(np, "min_charger_voltage_2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else {
		chr_err("use default V_CHARGER_MIN_2: %d\n", V_CHARGER_MIN_2);
		info->data.min_charger_voltage_2 = V_CHARGER_MIN_2;
	}

	if (of_property_read_u32(np, "max_dmivr_charger_current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else {
		chr_err("use default MAX_DMIVR_CHARGER_CURRENT: %d\n",
			MAX_DMIVR_CHARGER_CURRENT);
		info->data.max_dmivr_charger_current =
					MAX_DMIVR_CHARGER_CURRENT;
	}

	/* ntc_resistance:100k internal_pull_up:100k voltage:1.84v */
	info->ntc_temp_volt_1840mv = of_property_read_bool(np, "qcom,ntc_temp_volt_1840mv");

	/* fast charging algo support indicator */
	info->enable_fast_charging_indicator =
			of_property_read_bool(np, "enable_fast_charging_indicator");

	info->support_ntc_01c_precision = of_property_read_bool(np, "qcom,support_ntc_01c_precision");
	chr_debug("%s: support_ntc_01c_precision: %d\n", __func__, info->support_ntc_01c_precision);
}

static void mtk_charger_start_timer(struct mtk_charger *info)
{
	struct timespec64 end_time, time_now;
	ktime_t ktime, ktime_now;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&info->charger_timer);
	if (ret < 0) {
		chr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	ktime_now = ktime_get_boottime();
	time_now = ktime_to_timespec64(ktime_now);
	end_time.tv_sec = time_now.tv_sec + info->polling_interval;
	end_time.tv_nsec = time_now.tv_nsec + 0;
	info->endtime = end_time;
	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);

	chr_err("%s: alarm timer start:%d, %ld %ld\n", __func__, ret,
		info->endtime.tv_sec, info->endtime.tv_nsec);
	alarm_start(&info->charger_timer, ktime);
}

static void check_battery_exist(struct mtk_charger *info)
{
	unsigned int i = 0;
	int count = 0;
	//int boot_mode = get_boot_mode();

	if (is_disable_charger(info))
		return;

	for (i = 0; i < 3; i++) {
		if (is_battery_exist(info) == false)
			count++;
	}

#ifdef FIXME
	if (count >= 3) {
		if (boot_mode == META_BOOT || boot_mode == ADVMETA_BOOT ||
		    boot_mode == ATE_FACTORY_BOOT)
			chr_info("boot_mode = %d, bypass battery check\n",
				boot_mode);
		else {
			chr_err("battery doesn't exist, shutdown\n");
			orderly_poweroff(true);
		}
	}
#endif
}

static void check_dynamic_mivr(struct mtk_charger *info)
{
	int i = 0, ret = 0;
	int vbat = 0;
	bool is_fast_charge = false;
	struct chg_alg_device *alg = NULL;

	if (!info->enable_dynamic_mivr)
		return;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_fast_charge = true;
			break;
		}
	}

	if (!is_fast_charge) {
		vbat = get_battery_voltage(info);
		if (vbat < info->data.min_charger_voltage_2 / 1000 - 200)
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage_2);
		else if (vbat < info->data.min_charger_voltage_1 / 1000 - 200)
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage_1);
		else
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage);
	}
}

/* sw jeita */
void do_sw_jeita_state_machine(struct mtk_charger *info)
{
	struct sw_jeita_data *sw_jeita;

	sw_jeita = &info->sw_jeita;
	sw_jeita->pre_sm = sw_jeita->sm;
	sw_jeita->charging = true;

	/* JEITA battery temp Standard */
	if (info->battery_temp >= info->data.temp_t4_thres) {
		chr_err("[SW_JEITA] Battery Over high Temperature(%d) !!\n",
			info->data.temp_t4_thres);

		sw_jeita->sm = TEMP_ABOVE_T4;
		sw_jeita->charging = false;
	} else if (info->battery_temp > info->data.temp_t3_thres) {
		/* control 45 degree to normal behavior */
		if ((sw_jeita->sm == TEMP_ABOVE_T4)
		    && (info->battery_temp
			>= info->data.temp_t4_thres_minus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t4_thres_minus_x_degree,
				info->data.temp_t4_thres);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t3_thres,
				info->data.temp_t4_thres);

			sw_jeita->sm = TEMP_T3_TO_T4;
		}
	} else if (info->battery_temp >= info->data.temp_t2_thres) {
		if (((sw_jeita->sm == TEMP_T3_TO_T4)
		     && (info->battery_temp
			 >= info->data.temp_t3_thres_minus_x_degree))
		    || ((sw_jeita->sm == TEMP_T1_TO_T2)
			&& (info->battery_temp
			    <= info->data.temp_t2_thres_plus_x_degree))) {
			chr_err("[SW_JEITA] Battery Temperature not recovery to normal temperature charging mode yet!!\n");
		} else {
			chr_err("[SW_JEITA] Battery Normal Temperature between %d and %d !!\n",
				info->data.temp_t2_thres,
				info->data.temp_t3_thres);
			sw_jeita->sm = TEMP_T2_TO_T3;
		}
	} else if (info->battery_temp >= info->data.temp_t1_thres) {
		if ((sw_jeita->sm == TEMP_T0_TO_T1
		     || sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t1_thres_plus_x_degree)) {
			if (sw_jeita->sm == TEMP_T0_TO_T1) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
					info->data.temp_t1_thres_plus_x_degree,
					info->data.temp_t2_thres);
			}
			if (sw_jeita->sm == TEMP_BELOW_T0) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
					info->data.temp_t1_thres,
					info->data.temp_t1_thres_plus_x_degree);
				sw_jeita->charging = false;
			}
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t1_thres,
				info->data.temp_t2_thres);

			sw_jeita->sm = TEMP_T1_TO_T2;
		}
	} else if (info->battery_temp >= info->data.temp_t0_thres) {
		if ((sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t0_thres_plus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t0_thres,
				info->data.temp_t0_thres_plus_x_degree);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t0_thres,
				info->data.temp_t1_thres);

			sw_jeita->sm = TEMP_T0_TO_T1;
		}
	} else {
		chr_err("[SW_JEITA] Battery below low Temperature(%d) !!\n",
			info->data.temp_t0_thres);
		sw_jeita->sm = TEMP_BELOW_T0;
		sw_jeita->charging = false;
	}

	/* set CV after temperature changed */
	/* In normal range, we adjust CV dynamically */
	if (sw_jeita->sm != TEMP_T2_TO_T3) {
		if (sw_jeita->sm == TEMP_ABOVE_T4)
			sw_jeita->cv = info->data.jeita_temp_above_t4_cv;
		else if (sw_jeita->sm == TEMP_T3_TO_T4)
			sw_jeita->cv = info->data.jeita_temp_t3_to_t4_cv;
		else if (sw_jeita->sm == TEMP_T2_TO_T3)
			sw_jeita->cv = 0;
		else if (sw_jeita->sm == TEMP_T1_TO_T2)
			sw_jeita->cv = info->data.jeita_temp_t1_to_t2_cv;
		else if (sw_jeita->sm == TEMP_T0_TO_T1)
			sw_jeita->cv = info->data.jeita_temp_t0_to_t1_cv;
		else if (sw_jeita->sm == TEMP_BELOW_T0)
			sw_jeita->cv = info->data.jeita_temp_below_t0_cv;
		else
			sw_jeita->cv = info->data.battery_cv;
	} else {
		sw_jeita->cv = 0;
	}

	chr_err("[SW_JEITA]preState:%d newState:%d tmp:%d cv:%d\n",
		sw_jeita->pre_sm, sw_jeita->sm, info->battery_temp,
		sw_jeita->cv);
}

static int mtk_chgstat_notify(struct mtk_charger *info)
{
	int ret = 0;
	char *env[2] = { "CHGSTAT=1", NULL };

	chr_err("%s: 0x%x\n", __func__, info->notify_code);
	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		chr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static void mtk_charger_set_algo_log_level(struct mtk_charger *info, int level)
{
	struct chg_alg_device *alg;
	int i = 0, ret = 0;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		ret = chg_alg_set_prop(alg, ALG_LOG_LEVEL, level);
		if (ret < 0)
			chr_err("%s: set ALG_LOG_LEVEL fail, ret =%d", __func__, ret);
	}
}

static ssize_t sw_jeita_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->enable_sw_jeita);
	return sprintf(buf, "%d\n", pinfo->enable_sw_jeita);
}

static ssize_t sw_jeita_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_sw_jeita = false;
		else
			pinfo->enable_sw_jeita = true;

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(sw_jeita);
/* sw jeita end*/

static ssize_t chr_type_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->chr_type);
	return sprintf(buf, "%d\n", pinfo->chr_type);
}

static ssize_t chr_type_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0)
		pinfo->chr_type = temp;
	else
		chr_err("%s: format error!\n", __func__);

	return size;
}

static DEVICE_ATTR_RW(chr_type);

static ssize_t Pump_Express_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret = 0, i = 0;
	bool is_ta_detected = false;
	struct mtk_charger *pinfo = dev->driver_data;
	struct chg_alg_device *alg = NULL;

	if (!pinfo) {
		chr_err("%s: pinfo is null\n", __func__);
		return sprintf(buf, "%d\n", is_ta_detected);
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = pinfo->alg[i];
		if (alg == NULL)
			continue;
		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_ta_detected = true;
			break;
		}
	}
	chr_err("%s: idx = %d, detect = %d\n", __func__, i, is_ta_detected);
	return sprintf(buf, "%d\n", is_ta_detected);
}

static DEVICE_ATTR_RO(Pump_Express);

static ssize_t fast_chg_indicator_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->fast_charging_indicator);
	return sprintf(buf, "%d\n", pinfo->fast_charging_indicator);
}

static ssize_t fast_chg_indicator_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->fast_charging_indicator = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if (pinfo->fast_charging_indicator > 0) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(fast_chg_indicator);

static ssize_t enable_meta_current_limit_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->enable_meta_current_limit);
	return sprintf(buf, "%d\n", pinfo->enable_meta_current_limit);
}

static ssize_t enable_meta_current_limit_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->enable_meta_current_limit = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if (pinfo->enable_meta_current_limit > 0) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(enable_meta_current_limit);

static ssize_t vbat_mon_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->enable_vbat_mon);
	return sprintf(buf, "%d\n", pinfo->enable_vbat_mon);
}

static ssize_t vbat_mon_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_vbat_mon = false;
		else
			pinfo->enable_vbat_mon = true;
	} else {
		chr_err("%s: format error!\n", __func__);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(vbat_mon);

static ssize_t ADC_Charger_Voltage_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int vbus = get_vbus(pinfo); /* mV */

	chr_err("%s: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR_RO(ADC_Charger_Voltage);

static ssize_t ADC_Charging_Current_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int ibat = get_battery_current(pinfo); /* mA */

	chr_err("%s: %d\n", __func__, ibat);
	return sprintf(buf, "%d\n", ibat);
}

static DEVICE_ATTR_RO(ADC_Charging_Current);

static ssize_t input_current_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int aicr = 0;

	aicr = pinfo->chg_data[CHG1_SETTING].thermal_input_current_limit;
	chr_err("%s: %d\n", __func__, aicr);
	return sprintf(buf, "%d\n", aicr);
}

static ssize_t input_current_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	struct charger_data *chg_data;
	signed int temp;

	chg_data = &pinfo->chg_data[CHG1_SETTING];
	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0)
			chg_data->thermal_input_current_limit = 0;
		else
			chg_data->thermal_input_current_limit = temp;
	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(input_current);

static ssize_t charger_log_level_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->log_level);
	return sprintf(buf, "%d\n", pinfo->log_level);
}

static ssize_t charger_log_level_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0) {
			chr_err("%s: val is invalid: %ld\n", __func__, temp);
			temp = 0;
		}
		pinfo->log_level = temp;
		chr_err("%s: log_level=%d\n", __func__, pinfo->log_level);

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(charger_log_level);

static ssize_t BatteryNotify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_info("%s: 0x%x\n", __func__, pinfo->notify_code);

	return sprintf(buf, "%u\n", pinfo->notify_code);
}

static ssize_t BatteryNotify_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 16, &reg);
		if (ret < 0) {
			chr_err("%s: failed, ret = %d\n", __func__, ret);
			return ret;
		}
		pinfo->notify_code = reg;
		chr_info("%s: store code=0x%x\n", __func__, pinfo->notify_code);
		mtk_chgstat_notify(pinfo);
	}
	return size;
}

static DEVICE_ATTR_RW(BatteryNotify);

/* procfs */
static int mtk_chg_set_cv_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;

	seq_printf(m, "%d\n", pinfo->data.battery_cv);
	return 0;
}

static int mtk_chg_set_cv_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_set_cv_show, PDE_DATA(node));
}

static ssize_t mtk_chg_set_cv_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int cv = 0;
	struct mtk_charger *info = PDE_DATA(file_inode(file));
	struct power_supply *psy = NULL;
	union  power_supply_propval dynamic_cv;

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &cv);
	if (ret == 0) {
		if (cv >= BATTERY_CV) {
			info->data.battery_cv = BATTERY_CV;
			chr_info("%s: adjust charge voltage %dV too high, use default cv\n",
				  __func__, cv);
		} else {
			info->data.battery_cv = cv;
			chr_info("%s: adjust charge voltage = %dV\n", __func__, cv);
		}
		psy = power_supply_get_by_name("battery");
		if (!IS_ERR_OR_NULL(psy)) {
			dynamic_cv.intval = info->data.battery_cv;
			ret = power_supply_set_property(psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &dynamic_cv);
			if (ret < 0)
				chr_err("set gauge cv fail\n");
		}
		return count;
	}

	chr_err("%s: bad argument\n", __func__);
	return count;
}

static const struct proc_ops mtk_chg_set_cv_fops = {
	.proc_open = mtk_chg_set_cv_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_set_cv_write,
};

static int mtk_chg_current_cmd_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->usb_unlimited, pinfo->cmd_discharging);
	return 0;
}

static int mtk_chg_current_cmd_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_current_cmd_show, PDE_DATA(node));
}

static ssize_t mtk_chg_current_cmd_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32] = {0};
	int current_unlimited = 0;
	int cmd_discharging = 0;
	struct mtk_charger *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &current_unlimited, &cmd_discharging) == 2) {
		info->usb_unlimited = current_unlimited;
		if (cmd_discharging == 1) {
			info->cmd_discharging = true;
			charger_dev_enable(info->chg1_dev, false);
			charger_dev_do_event(info->chg1_dev,
					EVENT_DISCHARGE, 0);
		} else if (cmd_discharging == 0) {
			info->cmd_discharging = false;
			charger_dev_enable(info->chg1_dev, true);
			charger_dev_do_event(info->chg1_dev,
					EVENT_RECHARGE, 0);
		}

		chr_info("%s: current_unlimited=%d, cmd_discharging=%d\n",
			__func__, current_unlimited, cmd_discharging);
		return count;
	}

	chr_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

static const struct proc_ops mtk_chg_current_cmd_fops = {
	.proc_open = mtk_chg_current_cmd_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_current_cmd_write,
};

static int mtk_chg_en_power_path_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(pinfo->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static int mtk_chg_en_power_path_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_en_power_path_show, PDE_DATA(node));
}

static ssize_t mtk_chg_en_power_path_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct mtk_charger *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(info->chg1_dev, enable);
		chr_info("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	chr_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static const struct proc_ops mtk_chg_en_power_path_fops = {
	.proc_open = mtk_chg_en_power_path_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_en_power_path_write,
};

static int mtk_chg_en_safety_timer_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool safety_timer_en = false;

	charger_dev_is_safety_timer_enabled(pinfo->chg1_dev, &safety_timer_en);
	seq_printf(m, "%d\n", safety_timer_en);

	return 0;
}

static int mtk_chg_en_safety_timer_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_en_safety_timer_show, PDE_DATA(node));
}

static ssize_t mtk_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct mtk_charger *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_safety_timer(info->chg1_dev, enable);
		info->safety_timer_cmd = (int)enable;
		chr_info("%s: enable safety timer = %d\n", __func__, enable);

		/* SW safety timer */
		if (info->sw_safety_timer_setting == true) {
			if (enable)
				info->enable_sw_safety_timer = true;
			else
				info->enable_sw_safety_timer = false;
		}

		return count;
	}

	chr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

static const struct proc_ops mtk_chg_en_safety_timer_fops = {
	.proc_open = mtk_chg_en_safety_timer_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_en_safety_timer_write,
};

void scd_ctrl_cmd_from_user(void *nl_data, struct sc_nl_msg_t *ret_msg)
{
	struct sc_nl_msg_t *msg;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return;

	msg = nl_data;
	ret_msg->sc_cmd = msg->sc_cmd;

	switch (msg->sc_cmd) {

	case SC_DAEMON_CMD_PRINT_LOG:
		{
			chr_err("[%s] %s", SC_TAG, &msg->sc_data[0]);
		}
	break;

	case SC_DAEMON_CMD_SET_DAEMON_PID:
		{
			memcpy(&info->sc.g_scd_pid, &msg->sc_data[0],
				sizeof(info->sc.g_scd_pid));
			chr_err("[%s][fr] SC_DAEMON_CMD_SET_DAEMON_PID = %d(first launch)\n",
				SC_TAG, info->sc.g_scd_pid);
		}
	break;

	case SC_DAEMON_CMD_SETTING:
		{
			struct scd_cmd_param_t_1 data;

			memcpy(&data, &msg->sc_data[0],
				sizeof(struct scd_cmd_param_t_1));

			chr_debug("[%s] rcv data:%d %d %d %d %d %d %d %d %d %d %d %d %d %d Ans:%d\n",
				SC_TAG,
				data.data[0],
				data.data[1],
				data.data[2],
				data.data[3],
				data.data[4],
				data.data[5],
				data.data[6],
				data.data[7],
				data.data[8],
				data.data[9],
				data.data[10],
				data.data[11],
				data.data[12],
				data.data[13],
				data.data[14]);

			info->sc.solution = data.data[SC_SOLUTION];
			if (data.data[SC_SOLUTION] == SC_DISABLE)
				info->sc.disable_charger = true;
			else if (data.data[SC_SOLUTION] == SC_REDUCE)
				info->sc.disable_charger = false;
			else
				info->sc.disable_charger = false;
		}
	break;
	default:
		chr_err("[%s] bad sc_DAEMON_CTRL_CMD_FROM_USER 0x%x\n", SC_TAG, msg->sc_cmd);
		break;
	}

}

static void sc_nl_send_to_user(u32 pid, int seq, struct sc_nl_msg_t *reply_msg)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	/* int size=sizeof(struct fgd_nl_msg_t); */
	int size = reply_msg->sc_data_len + SCD_NL_MSG_T_HDR_LEN;
	int len = NLMSG_SPACE(size);
	void *data;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return;

	reply_msg->identity = SCD_NL_MAGIC;

	if (in_interrupt())
		skb = alloc_skb(len, GFP_ATOMIC);
	else
		skb = alloc_skb(len, GFP_KERNEL);

	if (!skb)
		return;

	nlh = nlmsg_put(skb, pid, seq, 0, size, 0);
	data = NLMSG_DATA(nlh);
	memcpy(data, reply_msg, size);
	NETLINK_CB(skb).portid = 0;	/* from kernel */
	NETLINK_CB(skb).dst_group = 0;	/* unicast */

	ret = netlink_unicast(info->sc.daemo_nl_sk, skb, pid, MSG_DONTWAIT);
	if (ret < 0) {
		chr_err("[Netlink] sc send failed %d\n", ret);
		return;
	}

}

static void chg_nl_data_handler(struct sk_buff *skb)
{
	u32 pid;
	kuid_t uid;
	int seq;
	void *data;
	struct nlmsghdr *nlh;
	struct sc_nl_msg_t *sc_msg, *sc_ret_msg;
	int size = 0;

	nlh = (struct nlmsghdr *)skb->data;
	pid = NETLINK_CREDS(skb)->pid;
	uid = NETLINK_CREDS(skb)->uid;
	seq = nlh->nlmsg_seq;

	data = NLMSG_DATA(nlh);

	sc_msg = (struct sc_nl_msg_t *)data;

	size = sc_msg->sc_ret_data_len + SCD_NL_MSG_T_HDR_LEN;

	if (size > (PAGE_SIZE << 1))
		sc_ret_msg = vmalloc(size);
	else {
		if (in_interrupt())
			sc_ret_msg = kmalloc(size, GFP_ATOMIC);
	else
		sc_ret_msg = kmalloc(size, GFP_KERNEL);
	}

	if (sc_ret_msg == NULL) {
		if (size > PAGE_SIZE)
			sc_ret_msg = vmalloc(size);

		if (sc_ret_msg == NULL)
			return;
	}

	memset(sc_ret_msg, 0, size);

	scd_ctrl_cmd_from_user(data, sc_ret_msg);
	sc_nl_send_to_user(pid, seq, sc_ret_msg);

	kvfree(sc_ret_msg);
}

void sc_select_charging_current(struct mtk_charger *info, struct charger_data *pdata)
{

	if (info->sc.g_scd_pid != 0 && info->sc.disable_in_this_plug == false) {
		chr_debug("sck: %d %d %d %d %d\n",
			info->sc.pre_ibat,
			info->sc.sc_ibat,
			pdata->charging_current_limit,
			pdata->thermal_charging_current_limit,
			info->sc.solution);
		if (info->sc.pre_ibat == -1 || info->sc.solution == SC_IGNORE
			|| info->sc.solution == SC_DISABLE) {
			info->sc.sc_ibat = -1;
		} else {
			if (info->sc.pre_ibat == pdata->charging_current_limit
				&& info->sc.solution == SC_REDUCE
				&& ((pdata->charging_current_limit - 100000) >= 500000)) {
				if (info->sc.sc_ibat == -1)
					info->sc.sc_ibat = pdata->charging_current_limit - 100000;
				else if (info->sc.sc_ibat - 100000 >= 500000)
					info->sc.sc_ibat = info->sc.sc_ibat - 100000;
			}
		}
	}
	info->sc.pre_ibat =  pdata->charging_current_limit;

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
		info->sc.disable_in_this_plug = true;
	} else if ((info->sc.solution == SC_REDUCE || info->sc.solution == SC_KEEP)
		&& info->sc.sc_ibat <
		pdata->charging_current_limit && info->sc.g_scd_pid != 0 &&
		info->sc.disable_in_this_plug == false) {
		pdata->charging_current_limit = info->sc.sc_ibat;
	}


}

void sc_init(struct smartcharging *sc)
{
	sc->enable = false;
	sc->battery_size = 3000;
	sc->start_time = 0;
	sc->end_time = 80000;
	sc->current_limit = 2000;
	sc->target_percentage = 80;
	sc->left_time_for_cv = 3600;
	sc->pre_ibat = -1;
}

void sc_update(struct mtk_charger *info)
{
	memset(&info->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	info->sc.data.data[SC_VBAT] = get_battery_voltage(info);
	info->sc.data.data[SC_BAT_TMP] = get_battery_temperature(info);
	info->sc.data.data[SC_UISOC] = get_uisoc(info);
	//info->sc.data.data[SC_SOC] = battery_get_soc();

	info->sc.data.data[SC_ENABLE] = info->sc.enable;
	info->sc.data.data[SC_BAT_SIZE] = info->sc.battery_size;
	info->sc.data.data[SC_START_TIME] = info->sc.start_time;
	info->sc.data.data[SC_END_TIME] = info->sc.end_time;
	info->sc.data.data[SC_IBAT_LIMIT] = info->sc.current_limit;
	info->sc.data.data[SC_TARGET_PERCENTAGE] = info->sc.target_percentage;
	info->sc.data.data[SC_LEFT_TIME_FOR_CV] = info->sc.left_time_for_cv;

	charger_dev_get_charging_current(info->chg1_dev, &info->sc.data.data[SC_IBAT_SETTING]);
	info->sc.data.data[SC_IBAT_SETTING] = info->sc.data.data[SC_IBAT_SETTING] / 1000;
	info->sc.data.data[SC_IBAT] = get_battery_current(info);


}

int wakeup_sc_algo_cmd(struct scd_cmd_param_t_1 *data, int subcmd, int para1)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (info->sc.g_scd_pid != 0) {
		struct sc_nl_msg_t *sc_msg;
		int size = SCD_NL_MSG_T_HDR_LEN + sizeof(struct scd_cmd_param_t_1);

		if (size > (PAGE_SIZE << 1))
			sc_msg = vmalloc(size);
		else {
			if (in_interrupt())
				sc_msg = kmalloc(size, GFP_ATOMIC);
		else
			sc_msg = kmalloc(size, GFP_KERNEL);

		}

		if (sc_msg == NULL) {
			if (size > PAGE_SIZE)
				sc_msg = vmalloc(size);

			if (sc_msg == NULL)
				return -1;
		}

		chr_debug(
			"[wakeup_fg_algo] malloc size=%d pid=%d\n",
			size, info->sc.g_scd_pid);
		memset(sc_msg, 0, size);
		sc_msg->sc_cmd = SC_DAEMON_CMD_NOTIFY_DAEMON;
		sc_msg->sc_subcmd = subcmd;
		sc_msg->sc_subcmd_para1 = para1;
		memcpy(sc_msg->sc_data, data, sizeof(struct scd_cmd_param_t_1));
		sc_msg->sc_data_len += sizeof(struct scd_cmd_param_t_1);
		sc_nl_send_to_user(info->sc.g_scd_pid, 0, sc_msg);

		kvfree(sc_msg);

		return 0;
	}
	chr_debug("pid is NULL\n");
	return -1;
}

static ssize_t enable_sc_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[enable smartcharging] : %d\n",
	info->sc.enable);

	return sprintf(buf, "%d\n", info->sc.enable);
}

static ssize_t enable_sc_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[enable smartcharging] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[enable smartcharging] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val == 0)
			info->sc.enable = false;
		else
			info->sc.enable = true;

		chr_err(
			"[enable smartcharging]enable smartcharging=%d\n",
			info->sc.enable);
	}
	return size;
}
static DEVICE_ATTR_RW(enable_sc);

static ssize_t sc_stime_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging stime] : %d\n",
	info->sc.start_time);

	return sprintf(buf, "%d\n", info->sc.start_time);
}

static ssize_t sc_stime_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging stime] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging stime] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			info->sc.start_time = val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			info->sc.start_time);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_stime);

static ssize_t sc_etime_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging etime] : %d\n",
	info->sc.end_time);

	return sprintf(buf, "%d\n", info->sc.end_time);
}

static ssize_t sc_etime_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging etime] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging etime] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			info->sc.end_time = val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			info->sc.end_time);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_etime);

static ssize_t sc_tuisoc_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging target uisoc] : %d\n",
	info->sc.target_percentage);

	return sprintf(buf, "%d\n", info->sc.target_percentage);
}

static ssize_t sc_tuisoc_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging tuisoc] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging tuisoc] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			info->sc.target_percentage = val;

		chr_err(
			"[smartcharging stime]tuisoc=%d\n",
			info->sc.target_percentage);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_tuisoc);

static ssize_t sc_ibat_limit_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging ibat limit] : %d\n",
	info->sc.current_limit);

	return sprintf(buf, "%d\n", info->sc.current_limit);
}

static ssize_t sc_ibat_limit_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging ibat limit] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging ibat limit] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			info->sc.current_limit = val;

		chr_err(
			"[smartcharging ibat limit]=%d\n",
			info->sc.current_limit);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_ibat_limit);

int mtk_chg_enable_vbus_ovp(bool enable)
{
	static struct mtk_charger *pinfo;
	int ret = 0;
	u32 sw_ovp = 0;
	struct power_supply *psy;

	if (pinfo == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL) {
			chr_err("[%s]psy is not rdy\n", __func__);
			return -1;
		}

		pinfo = (struct mtk_charger *)power_supply_get_drvdata(psy);
		if (pinfo == NULL) {
			chr_err("[%s]mtk_gauge is not rdy\n", __func__);
			return -1;
		}
	}

	if (enable)
		sw_ovp = pinfo->data.max_charger_voltage_setting;
	else
		sw_ovp = 15000000;

	/* Enable/Disable SW OVP status */
	pinfo->data.max_charger_voltage = sw_ovp;

	disable_hw_ovp(pinfo, enable);

	chr_err("[%s] en:%d ovp:%d\n",
			    __func__, enable, sw_ovp);
	return ret;
}
EXPORT_SYMBOL(mtk_chg_enable_vbus_ovp);

/* return false if vbus is over max_charger_voltage */
static bool mtk_chg_check_vbus(struct mtk_charger *info)
{
	int vchr = 0;

	vchr = get_vbus(info) * 1000; /* uV */
	if (vchr > info->data.max_charger_voltage) {
		chr_err("%s: vbus(%d mV) > %d mV\n", __func__, vchr / 1000,
			info->data.max_charger_voltage / 1000);
		return false;
	}
	return true;
}

static void mtk_battery_notify_VCharger_check(struct mtk_charger *info)
{
#if defined(BATTERY_NOTIFY_CASE_0001_VCHARGER)
	int vchr = 0;

	vchr = get_vbus(info) * 1000; /* uV */
	if (vchr < info->data.max_charger_voltage)
		info->notify_code &= ~CHG_VBUS_OV_STATUS;
	else {
		info->notify_code |= CHG_VBUS_OV_STATUS;
		chr_err("[BATTERY] charger_vol(%d mV) > %d mV\n",
			vchr / 1000, info->data.max_charger_voltage / 1000);
		mtk_chgstat_notify(info);
	}
#endif
}

static void mtk_battery_notify_VBatTemp_check(struct mtk_charger *info)
{
#if defined(BATTERY_NOTIFY_CASE_0002_VBATTEMP)
	if (info->battery_temp >= info->thermal.max_charge_temp) {
		info->notify_code |= CHG_BAT_OT_STATUS;
		chr_err("[BATTERY] bat_temp(%d) out of range(too high)\n",
			info->battery_temp);
		mtk_chgstat_notify(info);
	} else {
		info->notify_code &= ~CHG_BAT_OT_STATUS;
	}

	if (info->enable_sw_jeita == true) {
		if (info->battery_temp < info->data.temp_neg_10_thres) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
	} else {
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
		if (info->battery_temp < info->thermal.min_charge_temp) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
#endif
	}
#endif
}

static void mtk_battery_notify_UI_test(struct mtk_charger *info)
{
	switch (info->notify_test_mode) {
	case 1:
		info->notify_code = CHG_VBUS_OV_STATUS;
		chr_debug("[%s] CASE_0001_VCHARGER\n", __func__);
		break;
	case 2:
		info->notify_code = CHG_BAT_OT_STATUS;
		chr_debug("[%s] CASE_0002_VBATTEMP\n", __func__);
		break;
	case 3:
		info->notify_code = CHG_OC_STATUS;
		chr_debug("[%s] CASE_0003_ICHARGING\n", __func__);
		break;
	case 4:
		info->notify_code = CHG_BAT_OV_STATUS;
		chr_debug("[%s] CASE_0004_VBAT\n", __func__);
		break;
	case 5:
		info->notify_code = CHG_ST_TMO_STATUS;
		chr_debug("[%s] CASE_0005_TOTAL_CHARGINGTIME\n", __func__);
		break;
	case 6:
		info->notify_code = CHG_BAT_LT_STATUS;
		chr_debug("[%s] CASE6: VBATTEMP_LOW\n", __func__);
		break;
	case 7:
		info->notify_code = CHG_TYPEC_WD_STATUS;
		chr_debug("[%s] CASE7: Moisture Detection\n", __func__);
		break;
	default:
		chr_debug("[%s] Unknown BN_TestMode Code: %x\n",
			__func__, info->notify_test_mode);
	}
	mtk_chgstat_notify(info);
}

static void mtk_battery_notify_check(struct mtk_charger *info)
{
	if (info->notify_test_mode == 0x0000) {
		mtk_battery_notify_VCharger_check(info);
		mtk_battery_notify_VBatTemp_check(info);
	} else {
		mtk_battery_notify_UI_test(info);
	}
}

static void mtk_chg_get_tchg(struct mtk_charger *info)
{
	int ret;
	int tchg_min = -127, tchg_max = -127;
	struct charger_data *pdata;
	bool en = false;

	pdata = &info->chg_data[CHG1_SETTING];
	ret = charger_dev_get_temperature(info->chg1_dev, &tchg_min, &tchg_max);
	if (ret < 0) {
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
	} else {
		pdata->junction_temp_min = tchg_min;
		pdata->junction_temp_max = tchg_max;
	}

	if (info->chg2_dev) {
		pdata = &info->chg_data[CHG2_SETTING];
		ret = charger_dev_get_temperature(info->chg2_dev,
			&tchg_min, &tchg_max);

		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->dvchg1_dev) {
		pdata = &info->chg_data[DVCHG1_SETTING];
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
		ret = charger_dev_is_enabled(info->dvchg1_dev, &en);
		if (ret >= 0 && en) {
			ret = charger_dev_get_adc(info->dvchg1_dev,
						  ADC_CHANNEL_TEMP_JC,
						  &tchg_min, &tchg_max);
			if (ret >= 0) {
				pdata->junction_temp_min = tchg_min;
				pdata->junction_temp_max = tchg_max;
			}
		}
	}

	if (info->dvchg2_dev) {
		pdata = &info->chg_data[DVCHG2_SETTING];
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
		ret = charger_dev_is_enabled(info->dvchg2_dev, &en);
		if (ret >= 0 && en) {
			ret = charger_dev_get_adc(info->dvchg2_dev,
						  ADC_CHANNEL_TEMP_JC,
						  &tchg_min, &tchg_max);
			if (ret >= 0) {
				pdata->junction_temp_min = tchg_min;
				pdata->junction_temp_max = tchg_max;
			}
		}
	}
}

static void charger_check_status(struct mtk_charger *info)
{
	bool charging = true;
	bool chg_dev_chgen = true;
	int temperature;
	struct battery_thermal_protection_data *thermal;
	int uisoc = 0;

	if (get_charger_type(info) == POWER_SUPPLY_TYPE_UNKNOWN)
		return;

	temperature = info->battery_temp;
	thermal = &info->thermal;
	uisoc = get_uisoc(info);

	info->setting.vbat_mon_en = true;
	if (info->enable_sw_jeita == true || info->enable_vbat_mon != true ||
	    info->batpro_done == true)
		info->setting.vbat_mon_en = false;

	if (info->enable_sw_jeita == true) {
		do_sw_jeita_state_machine(info);
		if (info->sw_jeita.charging == false) {
			charging = false;
			goto stop_charging;
		}
	} else {

		if (thermal->enable_min_charge_temp) {
			if (temperature < thermal->min_charge_temp) {
				chr_err("Battery Under Temperature or NTC fail %d %d\n",
					temperature, thermal->min_charge_temp);
				thermal->sm = BAT_TEMP_LOW;
				charging = false;
				goto stop_charging;
			} else if (thermal->sm == BAT_TEMP_LOW) {
				if (temperature >=
				    thermal->min_charge_temp_plus_x_degree) {
					chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
					thermal->min_charge_temp,
					temperature,
					thermal->min_charge_temp_plus_x_degree);
					thermal->sm = BAT_TEMP_NORMAL;
				} else {
					charging = false;
					goto stop_charging;
				}
			}
		}

		if (temperature >= thermal->max_charge_temp) {
			chr_err("Battery over Temperature or NTC fail %d %d\n",
				temperature, thermal->max_charge_temp);
			thermal->sm = BAT_TEMP_HIGH;
			charging = false;
			goto stop_charging;
		} else if (thermal->sm == BAT_TEMP_HIGH) {
			if (temperature
			    < thermal->max_charge_temp_minus_x_degree) {
				chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
				thermal->max_charge_temp,
				temperature,
				thermal->max_charge_temp_minus_x_degree);
				thermal->sm = BAT_TEMP_NORMAL;
			} else {
				charging = false;
				goto stop_charging;
			}
		}
	}

	mtk_chg_get_tchg(info);

	if (!mtk_chg_check_vbus(info)) {
		charging = false;
		goto stop_charging;
	}

	if (info->cmd_discharging)
		charging = false;
	if (info->safety_timeout)
		charging = false;
	if (info->vbusov_stat)
		charging = false;
	if (info->sc.disable_charger == true)
		charging = false;
stop_charging:
	mtk_battery_notify_check(info);

	if (charging && uisoc < 80 && info->batpro_done == true) {
		info->setting.vbat_mon_en = true;
		info->batpro_done = false;
		info->stop_6pin_re_en = false;
	}

	chr_err("tmp:%d (jeita:%d sm:%d cv:%d en:%d) (sm:%d) en:%d c:%d s:%d ov:%d sc:%d %d %d saf_cmd:%d bat_mon:%d %d\n",
		temperature, info->enable_sw_jeita, info->sw_jeita.sm,
		info->sw_jeita.cv, info->sw_jeita.charging, thermal->sm,
		charging, info->cmd_discharging, info->safety_timeout,
		info->vbusov_stat, info->sc.disable_charger,
		info->can_charging, charging, info->safety_timer_cmd,
		info->enable_vbat_mon, info->batpro_done);

	charger_dev_is_enabled(info->chg1_dev, &chg_dev_chgen);

	if (charging != info->can_charging)
		_mtk_enable_charging(info, charging);
	else if (charging == false && chg_dev_chgen == true)
		_mtk_enable_charging(info, charging);

	info->can_charging = charging;
}

static bool charger_init_algo(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	int idx = 0;

	alg = get_chg_alg_by_name("pe5");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe5 fail\n");
	else {
		chr_err("get pe5 success\n");
		alg->config = info->config;
		alg->alg_id = PE5_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe4");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe4 fail\n");
	else {
		chr_err("get pe4 success\n");
		alg->config = info->config;
		alg->alg_id = PE4_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pd");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pd fail\n");
	else {
		chr_err("get pd success\n");
		alg->config = info->config;
		alg->alg_id = PDC_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe2");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe2 fail\n");
	else {
		chr_err("get pe2 success\n");
		alg->config = info->config;
		alg->alg_id = PE2_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe fail\n");
	else {
		chr_err("get pe success\n");
		alg->config = info->config;
		alg->alg_id = PE_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev)
		chr_err("Found primary charger\n");
	else {
		chr_err("*** Error : can't find primary charger ***\n");
		return false;
	}

	chr_err("config is %d\n", info->config);
	if (info->config == DUAL_CHARGERS_IN_SERIES) {
		info->chg2_dev = get_charger_by_name("secondary_chg");
		if (info->chg2_dev)
			chr_err("Found secondary charger\n");
		else {
			chr_err("*** Error : can't find secondary charger ***\n");
			return false;
		}
	} else if (info->config == DIVIDER_CHARGER ||
		   info->config == DUAL_DIVIDER_CHARGERS) {
		info->dvchg1_dev = get_charger_by_name("primary_dvchg");
		if (info->dvchg1_dev)
			chr_err("Found primary divider charger\n");
		else {
			chr_err("*** Error : can't find primary divider charger ***\n");
			return false;
		}
		if (info->config == DUAL_DIVIDER_CHARGERS) {
			info->dvchg2_dev =
				get_charger_by_name("secondary_dvchg");
			if (info->dvchg2_dev)
				chr_err("Found secondary divider charger\n");
			else {
				chr_err("*** Error : can't find secondary divider charger ***\n");
				return false;
			}
		}
	}

	chr_err("register chg1 notifier %d %d\n",
		info->chg1_dev != NULL, info->algo.do_event != NULL);
	if (info->chg1_dev != NULL && info->algo.do_event != NULL) {
		chr_err("register chg1 notifier done\n");
		info->chg1_nb.notifier_call = info->algo.do_event;
		register_charger_device_notifier(info->chg1_dev,
						&info->chg1_nb);
		charger_dev_set_drvdata(info->chg1_dev, info);
	}

	chr_err("register dvchg chg1 notifier %d %d\n",
		info->dvchg1_dev != NULL, info->algo.do_dvchg1_event != NULL);
	if (info->dvchg1_dev != NULL && info->algo.do_dvchg1_event != NULL) {
		chr_err("register dvchg chg1 notifier done\n");
		info->dvchg1_nb.notifier_call = info->algo.do_dvchg1_event;
		register_charger_device_notifier(info->dvchg1_dev,
						&info->dvchg1_nb);
		charger_dev_set_drvdata(info->dvchg1_dev, info);
	}

	chr_err("register dvchg chg2 notifier %d %d\n",
		info->dvchg2_dev != NULL, info->algo.do_dvchg2_event != NULL);
	if (info->dvchg2_dev != NULL && info->algo.do_dvchg2_event != NULL) {
		chr_err("register dvchg chg2 notifier done\n");
		info->dvchg2_nb.notifier_call = info->algo.do_dvchg2_event;
		register_charger_device_notifier(info->dvchg2_dev,
						 &info->dvchg2_nb);
		charger_dev_set_drvdata(info->dvchg2_dev, info);
	}

	return true;
}

static int mtk_charger_plug_out(struct mtk_charger *info)
{
	struct charger_data *pdata1 = &info->chg_data[CHG1_SETTING];
	struct charger_data *pdata2 = &info->chg_data[CHG2_SETTING];
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	int i;

	chr_err("%s\n", __func__);
	info->chr_type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->charger_thread_polling = false;
	info->pd_reset = false;

	pdata1->disable_charging_count = 0;
	pdata1->input_current_limit_by_aicl = -1;
	pdata2->disable_charging_count = 0;

	notify.evt = EVT_PLUG_OUT;
	notify.value = 0;
	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		chg_alg_notifier_call(alg, &notify);
	}
	memset(&info->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	wakeup_sc_algo_cmd(&info->sc.data, SC_EVENT_PLUG_OUT, 0);
	charger_dev_set_input_current(info->chg1_dev, 100000);
	charger_dev_set_mivr(info->chg1_dev, info->data.min_charger_voltage);
	charger_dev_plug_out(info->chg1_dev);

	return 0;
}

static int mtk_charger_plug_in(struct mtk_charger *info,
				int chr_type)
{
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	int i, vbat;

	chr_debug("%s\n",
		__func__);

	info->chr_type = chr_type;
	info->usb_type = get_usb_type(info);
	info->charger_thread_polling = true;

	info->can_charging = true;
	//info->enable_dynamic_cv = true;
	info->safety_timeout = false;
	info->vbusov_stat = false;
	info->old_cv = 0;
	info->stop_6pin_re_en = false;
	info->batpro_done = false;

	chr_err("mtk_is_charger_on plug in, type:%d\n", chr_type);

	vbat = get_battery_voltage(info);

	notify.evt = EVT_PLUG_IN;
	notify.value = 0;
	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		chg_alg_notifier_call(alg, &notify);
		chg_alg_set_prop(alg, ALG_REF_VBAT, vbat);
	}

	memset(&info->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	info->sc.disable_in_this_plug = false;
	wakeup_sc_algo_cmd(&info->sc.data, SC_EVENT_PLUG_IN, 0);
	charger_dev_set_input_current(info->chg1_dev,
				info->data.usb_charger_current);
	charger_dev_plug_in(info->chg1_dev);

	return 0;
}


static bool mtk_is_charger_on(struct mtk_charger *info)
{
	int chr_type;

	chr_type = get_charger_type(info);
	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN) {
		if (info->chr_type != POWER_SUPPLY_TYPE_UNKNOWN) {
			mtk_charger_plug_out(info);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	} else {
		if (info->chr_type == POWER_SUPPLY_TYPE_UNKNOWN)
			mtk_charger_plug_in(info, chr_type);
		else {
			info->chr_type = chr_type;
			info->usb_type = get_usb_type(info);
		}

		if (info->cable_out_cnt > 0) {
			mtk_charger_plug_out(info);
			mtk_charger_plug_in(info, chr_type);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	}

	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN)
		return false;

	return true;
}

#ifndef OPLUS_FEATURE_CHG_BASIC
static void charger_send_kpoc_uevent(struct mtk_charger *info)
{
	static bool first_time = true;
	ktime_t ktime_now;

	if (first_time) {
		info->uevent_time_check = ktime_get();
		first_time = false;
	} else {
		ktime_now = ktime_get();
		if ((ktime_ms_delta(ktime_now, info->uevent_time_check) / 1000) >= 60) {
			mtk_chgstat_notify(info);
			info->uevent_time_check = ktime_now;
		}
	}
}

static void kpoc_power_off_check(struct mtk_charger *info)
{
	unsigned int boot_mode = info->bootmode;
	int vbus = 0;

	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (boot_mode == 8 || boot_mode == 9) {
		vbus = get_vbus(info);
		if (vbus >= 0 && vbus < 2500 && !mtk_is_charger_on(info) && !info->pd_reset) {
			chr_err("Unplug Charger/USB in KPOC mode, vbus=%d, shutdown\n", vbus);
			while (1) {
				if (info->is_suspend == false) {
					chr_err("%s, not in suspend, shutdown\n", __func__);
					kernel_power_off();
				} else {
					chr_err("%s, suspend! cannot shutdown\n", __func__);
					msleep(20);
				}
			}
		}
		charger_send_kpoc_uevent(info);
	}
}
#endif

static void charger_status_check(struct mtk_charger *info)
{
	union power_supply_propval online, status;
	struct power_supply *chg_psy = NULL;
	int ret;
	bool charging = true;

	chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev,
						       "charger");
	if (IS_ERR_OR_NULL(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &online);

		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_STATUS, &status);

		if (!online.intval)
			charging = false;
		else {
			if (status.intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
				charging = false;
		}
	}
	if (charging != info->is_charging)
		power_supply_changed(info->psy1);
	info->is_charging = charging;
}


static char *dump_charger_type(int chg_type, int usb_type)
{
	switch (chg_type) {
	case POWER_SUPPLY_TYPE_UNKNOWN:
		return "none";
	case POWER_SUPPLY_TYPE_USB:
		if (usb_type == POWER_SUPPLY_USB_TYPE_SDP)
			return "usb";
		else
			return "nonstd";
	case POWER_SUPPLY_TYPE_USB_CDP:
		return "usb-h";
	case POWER_SUPPLY_TYPE_USB_DCP:
		return "std";
	//case POWER_SUPPLY_TYPE_USB_FLOAT:
	//	return "nonstd";
	default:
		return "unknown";
	}
}

void oplus_usbtemp_set_cc_open(void)
{
	if (pinfo == NULL || pinfo->tcpc == NULL)
		return;

	tcpm_typec_disable_function(pinfo->tcpc, true);
}

void oplus_usbtemp_recover_cc_open(void)
{
	if (pinfo == NULL || pinfo->tcpc == NULL)
		return;

	tcpm_typec_disable_function(pinfo->tcpc, false);
}

void oplus_set_typec_cc_open(void)
{
	if (pinfo == NULL || pinfo->tcpc == NULL)
		return;

	tcpm_typec_disable_function(pinfo->tcpc, true);
}

static int charger_routine_thread(void *arg)
{
	struct mtk_charger *info = arg;
	unsigned long flags;
	static bool is_module_init_done;
	bool is_charger_on;
	int ret;
	int vbat_min, vbat_max;
	u32 chg_cv = 0;

	while (1) {
		ret = wait_event_interruptible(info->wait_que,
			(info->charger_thread_timeout == true));
		if (ret < 0) {
			chr_err("%s: wait event been interrupted(%d)\n", __func__, ret);
			continue;
		}

		while (is_module_init_done == false) {
			if (charger_init_algo(info) == true)
				is_module_init_done = true;
			else {
				chr_err("charger_init fail\n");
				msleep(5000);
			}
		}

		mutex_lock(&info->charger_lock);
		spin_lock_irqsave(&info->slock, flags);
		if (!info->charger_wakelock->active)
			__pm_stay_awake(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		info->charger_thread_timeout = false;

		info->battery_temp = get_battery_temperature(info);
		ret = charger_dev_get_adc(info->chg1_dev,
			ADC_CHANNEL_VBAT, &vbat_min, &vbat_max);
		ret = charger_dev_get_constant_voltage(info->chg1_dev, &chg_cv);

		if (vbat_min != 0)
			vbat_min = vbat_min / 1000;

		chr_err("Vbat=%d vbats=%d vbus:%d ibus:%d I=%d T=%d uisoc:%d type:%s>%s pd:%d swchg_ibat:%d cv:%d\n",
			get_battery_voltage(info),
			vbat_min,
			get_vbus(info),
			get_ibus(info),
			get_battery_current(info),
			info->battery_temp,
			get_uisoc(info),
			dump_charger_type(info->chr_type, info->usb_type),
			dump_charger_type(get_charger_type(info), get_usb_type(info)),
			info->pd_type, get_ibat(info), chg_cv);

		is_charger_on = mtk_is_charger_on(info);

		if (info->charger_thread_polling == true)
			mtk_charger_start_timer(info);

		check_battery_exist(info);
		check_dynamic_mivr(info);
		charger_check_status(info);
#ifndef OPLUS_FEATURE_CHG_BASIC
		kpoc_power_off_check(info);
#endif

		if (is_disable_charger(info) == false &&
			is_charger_on == true &&
			info->can_charging == true) {
			if (info->algo.do_algorithm)
				info->algo.do_algorithm(info);
			sc_update(info);
			wakeup_sc_algo_cmd(&info->sc.data, SC_EVENT_CHARGING, 0);
			charger_status_check(info);
		} else {
			chr_debug("disable charging %d %d %d\n",
			    is_disable_charger(info), is_charger_on, info->can_charging);
			sc_update(info);
			wakeup_sc_algo_cmd(&info->sc.data, SC_EVENT_STOP_CHARGING, 0);
		}

		spin_lock_irqsave(&info->slock, flags);
		__pm_relax(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		chr_debug("%s end , %d\n",
			__func__, info->charger_thread_timeout);
		mutex_unlock(&info->charger_lock);
	}

	return 0;
}


#ifdef CONFIG_PM
static int charger_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	ktime_t ktime_now;
	struct timespec64 now;
	struct mtk_charger *info;

	info = container_of(notifier,
		struct mtk_charger, pm_notifier);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		info->is_suspend = true;
		chr_debug("%s: enter PM_SUSPEND_PREPARE\n", __func__);
		break;
	case PM_POST_SUSPEND:
		info->is_suspend = false;
		chr_debug("%s: enter PM_POST_SUSPEND\n", __func__);
		ktime_now = ktime_get_boottime();
		now = ktime_to_timespec64(ktime_now);

		if (timespec64_compare(&now, &info->endtime) >= 0 &&
			info->endtime.tv_sec != 0 &&
			info->endtime.tv_nsec != 0) {
			chr_err("%s: alarm timeout, wake up charger\n",
				__func__);
			__pm_relax(info->charger_wakelock);
			info->endtime.tv_sec = 0;
			info->endtime.tv_nsec = 0;
			_wake_up_charger(info);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
#endif /* CONFIG_PM */

static enum alarmtimer_restart
	mtk_charger_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct mtk_charger *info =
	container_of(alarm, struct mtk_charger, charger_timer);
	ktime_t *time_p = info->timer_cb_duration;

	info->timer_cb_duration[0] = ktime_get_boottime();
	if (info->is_suspend == false) {
		chr_debug("%s: not suspend, wake up charger\n", __func__);
		info->timer_cb_duration[1] = ktime_get_boottime();
		_wake_up_charger(info);
		info->timer_cb_duration[6] = ktime_get_boottime();
	} else {
		chr_debug("%s: alarm timer timeout\n", __func__);
		__pm_stay_awake(info->charger_wakelock);
	}

	info->timer_cb_duration[7] = ktime_get_boottime();

	if (ktime_us_delta(time_p[7], time_p[0]) > 5000)
		chr_err("%s: delta_t: %ld %ld %ld %ld %ld %ld %ld (%ld)\n",
			__func__,
			ktime_us_delta(time_p[1], time_p[0]),
			ktime_us_delta(time_p[2], time_p[1]),
			ktime_us_delta(time_p[3], time_p[2]),
			ktime_us_delta(time_p[4], time_p[3]),
			ktime_us_delta(time_p[5], time_p[4]),
			ktime_us_delta(time_p[6], time_p[5]),
			ktime_us_delta(time_p[7], time_p[6]),
			ktime_us_delta(time_p[7], time_p[0]));

	return ALARMTIMER_NORESTART;
}

static void mtk_charger_init_timer(struct mtk_charger *info)
{
	alarm_init(&info->charger_timer, ALARM_BOOTTIME,
			mtk_charger_alarm_timer_func);
	mtk_charger_start_timer(info);

#ifdef CONFIG_PM
	if (register_pm_notifier(&info->pm_notifier))
		chr_err("%s: register pm failed\n", __func__);
#endif /* CONFIG_PM */
}

static int mtk_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
	struct proc_dir_entry *battery_dir = NULL, *entry = NULL;
	struct mtk_charger *info = platform_get_drvdata(pdev);

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_jeita);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chr_type);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_enable_meta_current_limit);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_fast_chg_indicator);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_vbat_mon);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Pump_Express);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charger_Voltage);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charging_Current);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_input_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_log_level);
	if (ret)
		goto _out;

	/* Battery warning */
	ret = device_create_file(&(pdev->dev), &dev_attr_BatteryNotify);
	if (ret)
		goto _out;

	/* sysfs node */
	ret = device_create_file(&(pdev->dev), &dev_attr_enable_sc);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_stime);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_etime);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_tuisoc);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_ibat_limit);
	if (ret)
		goto _out;

	battery_dir = proc_mkdir("mtk_battery_cmd", NULL);
	if (!battery_dir) {
		chr_err("%s: mkdir /proc/mtk_battery_cmd failed\n", __func__);
		return -ENOMEM;
	}

	entry = proc_create_data("current_cmd", 0644, battery_dir,
			&mtk_chg_current_cmd_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_power_path", 0644, battery_dir,
			&mtk_chg_en_power_path_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_safety_timer", 0644, battery_dir,
			&mtk_chg_en_safety_timer_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("set_cv", 0644, battery_dir,
			&mtk_chg_set_cv_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}

	return 0;

fail_procfs:
	remove_proc_subtree("mtk_battery_cmd", NULL);
_out:
	return ret;
}

void mtk_charger_get_atm_mode(struct mtk_charger *info)
{
	char atm_str[64] = {0};
	char *ptr = NULL, *ptr_e = NULL;
	char keyword[] = "androidboot.atm=";
	int size = 0;

	ptr = strstr(chg_get_cmd(), keyword);
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");
		if (ptr_e == 0)
			goto end;

		size = ptr_e - (ptr + strlen(keyword));
		if (size <= 0)
			goto end;
		strncpy(atm_str, ptr + strlen(keyword), size);
		atm_str[size] = '\0';
		chr_err("%s: atm_str: %s\n", __func__, atm_str);

		if (!strncmp(atm_str, "enable", strlen("enable")))
			info->atm_enabled = true;
	}
end:
	chr_err("%s: atm_enabled = %d\n", __func__, info->atm_enabled);
}

static int psy_charger_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return 1;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return 1;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property charger_psy_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static int psy_charger_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct mtk_charger *info;
	struct charger_device *chg;
#ifndef OPLUS_FEATURE_CHG_BASIC
	struct charger_data *pdata;
#endif
	int ret = 0;
	struct chg_alg_device *alg = NULL;

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);

	chr_err("%s psp:%d\n",
		__func__, psp);


	if (info->psy1 != NULL &&
		info->psy1 == psy)
		chg = info->chg1_dev;
	else if (info->psy2 != NULL &&
		info->psy2 == psy)
		chg = info->chg2_dev;
	else if (info->psy_dvchg1 != NULL && info->psy_dvchg1 == psy)
		chg = info->dvchg1_dev;
	else if (info->psy_dvchg2 != NULL && info->psy_dvchg2 == psy)
		chg = info->dvchg2_dev;
	else {
		chr_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (chg == info->dvchg1_dev) {
			val->intval = false;
			alg = get_chg_alg_by_name("pe5");
			if (alg == NULL)
				chr_err("get pe5 fail\n");
			else {
				ret = chg_alg_is_algo_ready(alg);
				if (ret == ALG_RUNNING)
					val->intval = true;
			}
			break;
		}

		val->intval = is_charger_exist(info);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (chg != NULL)
			val->intval = true;
		else
			val->intval = false;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = info->enable_hv_charging;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_vbus(info);
		break;
	case POWER_SUPPLY_PROP_TEMP:
#ifndef OPLUS_FEATURE_CHG_BASIC
		if (chg == info->chg1_dev)
			val->intval =
				info->chg_data[CHG1_SETTING].junction_temp_max * 10;
		else if (chg == info->chg2_dev)
			val->intval =
				info->chg_data[CHG2_SETTING].junction_temp_max * 10;
		else if (chg == info->dvchg1_dev) {
			pdata = &info->chg_data[DVCHG1_SETTING];
			val->intval = pdata->junction_temp_max;
		} else if (chg == info->dvchg2_dev) {
			pdata = &info->chg_data[DVCHG2_SETTING];
			val->intval = pdata->junction_temp_max;
		} else
			val->intval = -127;
#else
		val->intval = oplus_get_chargeric_temp();
#endif
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = get_charger_charging_current(info, chg);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = get_charger_input_current(info, chg);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = info->chr_type;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_BOOT:
		val->intval = get_charger_zcv(info, chg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int psy_charger_set_property(struct power_supply *psy,
			enum power_supply_property psp,
			const union power_supply_propval *val)
{
	struct mtk_charger *info;
	int idx;

	chr_err("%s: prop:%d %d\n", __func__, psp, val->intval);

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);

	if (info->psy1 != NULL &&
		info->psy1 == psy)
		idx = CHG1_SETTING;
	else if (info->psy2 != NULL &&
		info->psy2 == psy)
		idx = CHG2_SETTING;
	else if (info->psy_dvchg1 != NULL && info->psy_dvchg1 == psy)
		idx = DVCHG1_SETTING;
	else if (info->psy_dvchg2 != NULL && info->psy_dvchg2 == psy)
		idx = DVCHG2_SETTING;
	else {
		chr_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (val->intval > 0)
			info->enable_hv_charging = true;
		else
			info->enable_hv_charging = false;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		info->chg_data[idx].thermal_charging_current_limit =
			val->intval;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		info->chg_data[idx].thermal_input_current_limit =
			val->intval;
		break;
	default:
		return -EINVAL;
	}
	_wake_up_charger(info);

	return 0;
}

static void mtk_charger_external_power_changed(struct power_supply *psy)
{
	struct mtk_charger *info;
	union power_supply_propval prop, prop2;
	struct power_supply *chg_psy = NULL;
#ifdef OPLUS_FEATURE_CHG_BASIC
	union oplus_chg_mod_propval temp_val = {0, };
	struct oplus_chg_chip *chip = g_oplus_chip;
	int new_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
#endif
	int ret;

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (!chip) {
		chg_err("g_oplus_chip is NULL!\n");
		return;
	}
#endif

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
	chg_psy = info->chg_psy;

	if (IS_ERR_OR_NULL(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);
	chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev,
						       "charger");
		info->chg_psy = chg_psy;
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop2);
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	new_charger_type = chip->chg_ops->get_charger_type();
	pr_notice("%s event, name:%s online:%d type:%d vbus:%d charger_type:%d new_charger_type = %d\n",
		__func__, psy->desc->name, prop.intval, prop2.intval,
		get_vbus(info), chip->charger_type, new_charger_type);
	if (oplus_vooc_get_fastchg_started() == true
			&& oplus_vooc_get_adapter_update_status() != 1) {
		printk(KERN_ERR "[OPLUS_CHG] %s oplus_vooc_get_fastchg_started = true!\n", __func__);
		if (prop.intval) {
		/*vooc adapters MCU vbus reset time is about 800ms(default standard),
		 * but some adapters reset time is about 350ms, so when vbus plugin irq
		 * was trigger, fastchg_started is true(default standard is false).
		 */
			oplus_chg_suspend_charger();
		}
	}
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (!prop.intval) {
		oplus_chg_global_event(chip->chgic_mtk.oplus_info->usb_ocm, OPLUS_CHG_EVENT_OFFLINE);
		oplus_chg_set_charger_type_unknown();
		if (oplus_vooc_get_fastchg_started() == false) {
			oplus_vooc_reset_fastchg_after_usbout();
			oplus_chg_set_chargerid_switch_val(0);
			oplus_chg_clear_chargerid_info();
		}
	} else {
		oplus_chg_global_event(chip->chgic_mtk.oplus_info->usb_ocm, OPLUS_CHG_EVENT_ONLINE);
		oplus_wake_up_usbtemp_thread();
		if (!oplus_vooc_get_fastchg_started() &&
		    chip->chg_ops == &mtk6375_chg_ops &&
		    !chip->mmi_chg &&
		    oplus_mt6375_check_charging_enable())
			oplus_mt6375_disable_charging();
	}

	if (is_wls_ocm_available(chip)) {
		oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_TRX_ONLINE, &temp_val);
		if (!!temp_val.intval) {
			temp_val.intval = false;
			oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_ONLINE, &temp_val);

			msleep(100);

			temp_val.intval = true;
                        oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_ONLINE, &temp_val);
		}

		if (oplus_chg_is_wls_present())
			oplus_chg_set_charger_type_unknown();
	}

	chg_err("%s wake_update_work\n", __func__);

	/* Add for fix the bug: BC12 charger type change.*/
	if (prop.intval && (chip->charger_type != new_charger_type) && (chip->charger_type == POWER_SUPPLY_TYPE_USB
		|| chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
		chg_err("%s charger_type=%d, new_charger_type = %d is not unknow, set as unknow and wake_update_work\n",
			__func__, chip->charger_type, new_charger_type);
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	}

	oplus_chg_wake_update_work();

	oplus_set_hvdcp_flag_clear();
#endif

	_wake_up_charger(info);
}

int notify_adapter_event(struct notifier_block *notifier,
			unsigned long evt, void *val)
{
	struct mtk_charger *pinfo = NULL;

	chr_err("%s %d\n", __func__, evt);

	pinfo = container_of(notifier,
		struct mtk_charger, pd_nb);

	switch (evt) {
	case  MTK_PD_CONNECT_NONE:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify Detach\n");
		pinfo->pd_type = MTK_PD_CONNECT_NONE;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		mtk_chg_alg_notify_call(pinfo, EVT_DETACH, 0);
		/* reset PE40 */
		break;

	case MTK_PD_CONNECT_HARD_RESET:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify HardReset\n");
		pinfo->pd_type = MTK_PD_CONNECT_NONE;
		pinfo->pd_reset = true;
		mutex_unlock(&pinfo->pd_lock);
		mtk_chg_alg_notify_call(pinfo, EVT_HARDRESET, 0);
		_wake_up_charger(pinfo);
		/* reset PE40 */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify fixe voltage ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK;
		pinfo->pd_reset = false;
		oplus_chg_track_record_chg_type_info();
		mutex_unlock(&pinfo->pd_lock);
		/* PD is ready */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK_PD30:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify PD30 ready\r\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_PD30;
		pinfo->pd_reset = false;
		oplus_chg_track_record_chg_type_info();
		mutex_unlock(&pinfo->pd_lock);
		/* PD30 is ready */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK_APDO:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify APDO Ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_APDO;
		pinfo->pd_reset = false;
		oplus_chg_track_record_chg_type_info();
		mutex_unlock(&pinfo->pd_lock);
		/* PE40 is ready */
		_wake_up_charger(pinfo);
		break;

	case MTK_PD_CONNECT_TYPEC_ONLY_SNK:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify Type-C Ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_TYPEC_ONLY_SNK;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		/* type C is ready */
		_wake_up_charger(pinfo);
		break;
	case MTK_TYPEC_WD_STATUS:
		chr_err("wd status = %d\n", *(bool *)val);
		pinfo->water_detected = *(bool *)val;
		if (pinfo->water_detected == true)
			pinfo->notify_code |= CHG_TYPEC_WD_STATUS;
		else
			pinfo->notify_code &= ~CHG_TYPEC_WD_STATUS;
		mtk_chgstat_notify(pinfo);
		break;
	}
	return NOTIFY_DONE;
}

int chg_alg_event(struct notifier_block *notifier,
			unsigned long event, void *data)
{
	chr_err("%s: evt:%d\n", __func__, event);

	return NOTIFY_DONE;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#define OPLUS_SVID 0x22D9
#define SVID_NONE 0x0
uint32_t pd_svooc_abnormal_adapter[] = {
	0x20002,
	0x10002,
	0x10001,
	0x40001,
};

int oplus_get_adapter_svid(void)
{
	int i = 0, j = 0;
	uint32_t vdos[VDO_MAX_NR] = {0};
	struct tcpc_device *tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	struct tcpm_svid_list svid_list= {0, {0}};

	if (tcpc_dev == NULL || !g_oplus_chip) {
		chg_err("tcpc_dev is null return\n");
		return -ENODEV;
	}

	tcpm_inquire_pd_partner_svids(tcpc_dev, &svid_list);
	for (i = 0; i < svid_list.cnt; i++) {
		chg_err("svid[%d] = 0x%x\n", i, svid_list.svids[i]);
		if (svid_list.svids[i] == OPLUS_SVID) {
			g_oplus_chip->pd_svooc = true;
			chg_err("match svid and this is oplus adapter\n");
			break;
		}
	}

	tcpm_inquire_pd_partner_inform(tcpc_dev, vdos);
	if ((vdos[0] & 0xFFFF) == OPLUS_SVID) {
		g_oplus_chip->pd_svooc = true;
		chg_err("match svid and this is oplus adapter 11\n");
		for (j = 0; j < ARRAY_SIZE(pd_svooc_abnormal_adapter); j++) {
			if (pd_svooc_abnormal_adapter[j] == vdos[2]) {
				chg_err("This is oplus gnd abnormal adapter %x %x \n", vdos[1], vdos[2]);
				g_oplus_chip->is_abnormal_adapter = true;
				break;
			}
		}
		return OPLUS_SVID;
	} else
		return SVID_NONE;
}

bool oplus_check_pdphy_ready(void)
{
	if (!pinfo || !pinfo->tcpc) {
		chr_err("pinfo or tcpc  null return \n");
		return false;
	}

	return tcpm_inquire_pdphy_ready(pinfo->tcpc);
}
EXPORT_SYMBOL(oplus_check_pdphy_ready);

static void oplus_chg_pps_get_source_cap(struct mtk_charger *info)
{
	struct tcpm_power_cap_val apdo_cap;
	struct pd_source_cap_ext cap_ext;
	uint8_t cap_i = 0;
	int ret = 0;
	int idx = 0;
	unsigned int i = 0;

	if (info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
		while (1) {
			ret = tcpm_inquire_pd_source_apdo(info->tcpc,
					TCPM_POWER_CAP_APDO_TYPE_PPS,
					&cap_i, &apdo_cap);
			if (ret == TCPM_ERROR_NOT_FOUND) {
				break;
			} else if (ret != TCPM_SUCCESS) {
				pr_err("[%s] tcpm_inquire_pd_source_apdo failed(%d)\n",
					__func__, ret);
				break;
			}

			ret = tcpm_dpm_pd_get_source_cap_ext(info->tcpc,
				NULL, &cap_ext);
			if (ret == TCPM_SUCCESS)
				info->srccap.pdp = cap_ext.source_pdp;
			else {
				info->srccap.pdp = 0;
				pr_err("[%s] tcpm_dpm_pd_get_source_cap_ext failed(%d)\n",
					__func__, ret);
			}

			info->srccap.pwr_limit[idx] = apdo_cap.pwr_limit;
			/* If TA has PDP, we set pwr_limit as true */
			if (info->srccap.pdp > 0 && !info->srccap.pwr_limit[idx])
				info->srccap.pwr_limit[idx] = 1;
			info->srccap.ma[idx] = apdo_cap.ma;
			info->srccap.max_mv[idx] = apdo_cap.max_mv;
			info->srccap.min_mv[idx] = apdo_cap.min_mv;
			info->srccap.maxwatt[idx] = apdo_cap.max_mv * apdo_cap.ma;
			info->srccap.minwatt[idx] = apdo_cap.min_mv * apdo_cap.ma;
			info->srccap.type[idx] = MTK_PD_APDO;

			idx++;

			pr_err("pps_boundary[%d], %d mv ~ %d mv, %d ma pl:%d\n",
				cap_i,
				apdo_cap.min_mv, apdo_cap.max_mv,
				apdo_cap.ma, apdo_cap.pwr_limit);
			if (idx >= ADAPTER_CAP_MAX_NR) {
				pr_notice("CAP NR > %d\n", ADAPTER_CAP_MAX_NR);
				break;
			}
		}

		info->srccap.nr = idx;

		for (i = 0; i < info->srccap.nr; i++) {
			pr_err("pps_cap[%d:%d], %d mv ~ %d mv, %d ma pl:%d pdp:%d\n",
				i, (int)info->srccap.nr, info->srccap.min_mv[i],
				info->srccap.max_mv[i], info->srccap.ma[i],
				info->srccap.pwr_limit[i], info->srccap.pdp);
			}

		if (cap_i == 0)
			pr_notice("no APDO for pps\n");
	}

	return;
}

static int pd_tcp_notifier_call(struct notifier_block *pnb,
				unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct mtk_charger *pinfo = NULL;
	int ret = 0;
	union oplus_chg_mod_propval temp_val = {0, };
	static struct charger_device *primary_charger = NULL;
	static bool otg_flag = 0;
	int svid = 0;

	if (!primary_charger) {
		primary_charger = get_charger_by_name("primary_chg");
		if (!primary_charger) {
			chr_err("get primary charger device failed\n");
		}
	}

	pinfo = container_of(pnb, struct mtk_charger, pd_nb);

	pr_notice("PD charger event:%d %d\n", (int)event,
		(int)noti->pd_state.connected);

	switch (event) {
	case TCP_NOTIFY_SOURCE_VBUS:
		if (!g_oplus_chip) {
			pr_err("%s, g_oplus_chip error!\n", __func__);
			return ret;
		}

		if (is_wls_ocm_available(g_oplus_chip)) {
                        oplus_chg_mod_get_property(g_oplus_chip->wls_ocm,
                                        OPLUS_CHG_PROP_TRX_ONLINE, &temp_val);
		}

		chr_err("source vbus = %dmv, trx = %d\n", noti->vbus_state.mv, temp_val.intval);

		if (noti->vbus_state.mv) {
			if (g_oplus_chip->chgic_mtk.oplus_info->usb_ocm) {
				oplus_chg_global_event(g_oplus_chip->chgic_mtk.oplus_info->usb_ocm, OPLUS_CHG_EVENT_OTG_ENABLE);
				msleep(50);
			}
			oplus_set_ext1_otg_en_val(1);
			oplus_set_boost_en_val(1);
			if (primary_charger && !otg_flag) {
				charger_dev_set_boost_current_limit(primary_charger, MT6375_OTG_CURRENT_LIMIT_1300MA);
				g_oplus_chip->chg_ops->otg_enable();
				otg_flag = true;
			}
		} else {
			if (otg_flag) {
				g_oplus_chip->chg_ops->otg_disable();
				otg_flag = false;
			}
			oplus_set_boost_en_val(0);
			oplus_set_ext1_otg_en_val(0);
			msleep(50);
			if (g_oplus_chip->chgic_mtk.oplus_info->usb_ocm) {
				oplus_chg_global_event(g_oplus_chip->chgic_mtk.oplus_info->usb_ocm, OPLUS_CHG_EVENT_OTG_DISABLE);
				msleep(50);
			}
		}
		break;

	case TCP_NOTIFY_PD_STATE:
		switch (noti->pd_state.connected) {
		case  PD_CONNECT_NONE:
			pinfo->pd_type = MTK_PD_CONNECT_NONE;
			chr_err("PD Notify Detach\n");
			break;

		case PD_CONNECT_HARD_RESET:
			pinfo->pd_type = MTK_PD_CONNECT_NONE;
			chr_err("PD Notify HardReset\n");
			break;

		case PD_CONNECT_PE_READY_SNK:
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK;
			chr_err("PD Notify fixe voltage ready\n");
			svid = oplus_get_adapter_svid();
			if (svid == SVID_NONE)
				g_oplus_chip->pd_svooc = false;
			break;

		case PD_CONNECT_PE_READY_SNK_PD30:
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_PD30;
			chr_err("PD Notify PD30 ready\r\n");
			svid = oplus_get_adapter_svid();
			if (svid == SVID_NONE)
				g_oplus_chip->pd_svooc = false;
			break;

		case PD_CONNECT_PE_READY_SNK_APDO:
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_APDO;
			chr_err("PD Notify APDO Ready\n");
			svid = oplus_get_adapter_svid();
			if (svid == SVID_NONE)
				g_oplus_chip->pd_svooc = false;
			oplus_chg_pps_get_source_cap(pinfo);
			break;

		case PD_CONNECT_TYPEC_ONLY_SNK_DFT:
			/* fall-through */
		case PD_CONNECT_TYPEC_ONLY_SNK:
			pinfo->pd_type = MTK_PD_CONNECT_TYPEC_ONLY_SNK;
			chr_err("PD Notify Type-C Ready\n");
			break;
		};
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		chr_err("old_state=%d, new_state=%d\n",
				noti->typec_state.old_state,
				noti->typec_state.new_state);
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			chr_err("Type-C SRC plug in\n");
		} else if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
			noti->typec_state.new_state == TYPEC_ATTACHED_SNK) {
			chr_err("Type-C SINK plug in\n");
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SRC ||
			noti->typec_state.old_state == TYPEC_ATTACHED_SNK) &&
			noti->typec_state.new_state == TYPEC_UNATTACHED) {
			chr_err("Type-C plug out\n");
		}
		break;
	case TCP_NOTIFY_WD_STATUS:
		chr_err("wd status = %d\n", (bool)noti->wd_status.water_detected);
		pinfo->water_detected = (bool)noti->wd_status.water_detected;
		if (pinfo->water_detected == true)
			pinfo->notify_code |= CHG_TYPEC_WD_STATUS;
		else
			pinfo->notify_code &= ~CHG_TYPEC_WD_STATUS;
		mtk_chgstat_notify(pinfo);
		break;
	case TCP_NOTIFY_WD0_STATE:
		pinfo->wd0_detect = noti->wd0_state.wd0;
		pr_err("%s wd0 = %d\n", __func__, noti->wd0_state.wd0);

		if (pinfo->wd0_detect) {
			oplus_ccdetect_enable();
		} else {
			if (oplus_get_otg_switch_status() == false)
				oplus_ccdetect_disable();
		}
		if (wd0_detect_work.work.func) {
			schedule_delayed_work(&wd0_detect_work, msecs_to_jiffies(CCDETECT_DELAY_MS));
		} else {
			pr_err("%s wd0_detect_work.work.func is NULL\n", __func__);
		}

		if (g_oplus_chip) {
			power_supply_changed(g_oplus_chip->usb_psy);
			if (pinfo->wd0_detect == 0) {
				oplus_usbtemp_recover_cc_open();
			}
		}
		break;
	case TCP_NOTIFY_CHRDET_STATE:
		pinfo->chrdet_state = noti->chrdet_state.chrdet;
		pr_err("%s chrdet = %d\n", __func__, noti->chrdet_state.chrdet);

		oplus_chg_check_break(pinfo->chrdet_state);

		if (pinfo->chrdet_state) {
			oplus_chg_wake_update_work();
			oplus_wake_up_usbtemp_thread();
		} else {
			if (!pinfo->wait_hard_reset_complete)
				oplus_chg_set_charger_type_unknown();
		}
		break;
	case TCP_NOTIFY_HARD_RESET_STATE:
		switch (noti->hreset_state.state) {
		case TCP_HRESET_SIGNAL_SEND:
		case TCP_HRESET_SIGNAL_RECV:
			pinfo->wait_hard_reset_complete = true;
			break;
		default:
			pinfo->wait_hard_reset_complete = false;
			break;
		}
		pr_err("pd_wait_hard_reset_complete: %d\n", pinfo->wait_hard_reset_complete);
		break;

	}
	return ret;
}

static bool oplus_chg_svooc_30w_adapter(struct oplus_voocphy_manager *chip)
{
	int fast_chg_type = 0;
	bool ret = false;

	if (!chip)
		return false;
	fast_chg_type = chip->fast_chg_type;

	switch (fast_chg_type) {
	case 0x61:		/* 11V3A */
	case 0x49:		/* for 11V3A adapter temp */
	case 0x4A:		/* for 11V3A adapter temp */
	case 0x4B:		/* for 11V3A adapter temp */
	case 0x4C:		/* for 11V3A adapter temp */
	case 0x4D:		/* for 11V3A adapter temp */
	case 0x4E:		/* for 11V3A adapter temp */
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static void oplus_chg_choose_gauge_curve(int index_curve)
{
	static last_curve_index = -1;
	int target_index_curve = -1;
	bool svooc_30w_adapter = false;

	struct oplus_voocphy_manager *voocphy_chip = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	oplus_voocphy_get_chip(&voocphy_chip);
	if (!pinfo || !voocphy_chip)
		return;

	if (index_curve == CHARGER_SUBTYPE_QC || ((index_curve == CHARGER_SUBTYPE_PD) && (chip->pd_svooc == false)))
		target_index_curve = CHARGER_FASTCHG_VOOC_AND_QCPD_CURVE;
	else if (index_curve == 0)
		target_index_curve = CHARGER_NORMAL_CHG_CURVE;
	else {
		if (voocphy_chip->fast_chg_type != FASTCHG_CHARGER_TYPE_UNKOWN) {

			if (voocphy_chip->adapter_type == ADAPTER_SVOOC)
				svooc_30w_adapter = oplus_chg_svooc_30w_adapter(voocphy_chip);

			if (voocphy_chip->adapter_type == ADAPTER_VOOC20 || voocphy_chip->adapter_type == ADAPTER_VOOC30)
				target_index_curve = CHARGER_FASTCHG_VOOC_AND_QCPD_CURVE;
			else if ((voocphy_chip->adapter_type == ADAPTER_SVOOC) &&(svooc_30w_adapter == false))
				target_index_curve = CHARGER_FASTCHG_SVOOC_CURVE;
			else if ((voocphy_chip->adapter_type == ADAPTER_SVOOC) &&(svooc_30w_adapter == true))
				target_index_curve = CHARGER_FASTCHG_VOOC_AND_QCPD_CURVE;
			else
				pr_err("adapter_type error\n");
		}
	}

	if ((target_index_curve < CHARGER_NORMAL_CHG_CURVE) || (target_index_curve > CHARGER_FASTCHG_VOOC_AND_QCPD_CURVE))
		target_index_curve = CHARGER_NORMAL_CHG_CURVE;

	chg_err("target_index_curve=%d, last_curve_index=%d, index_curve=%d, svooc_30w_adapter is %s\n",
					target_index_curve,
					last_curve_index,
					index_curve,
					svooc_30w_adapter == true ?"true":"false");
	if (target_index_curve != last_curve_index) {
		printk(KERN_ERR "%s: index_curve() =%d target_index_curve =%d last_curve_index =%d  pd svooc %s",
					__func__, index_curve, target_index_curve, last_curve_index,
					chip->pd_svooc == true ?"true":"false");

		printk(KERN_ERR "%s svooc_30w_adapter %s set_charge_power_sel  %d", __func__,
					svooc_30w_adapter == true ?"true":"false",
					target_index_curve);

		oplus_gauge_set_power_sel(target_index_curve);
		last_curve_index = target_index_curve;
	}
	return;
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	int rc = 0;
	struct rtc_time tm;
	struct rtc_device *rtc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

void oplus_set_typec_sinkonly(void)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: usbtemp occur otg switch[0]\n", __func__);
#ifndef OPLUS_FEATURE_CHG_BASIC
		tcpm_typec_change_role(pinfo->tcpc, TYPEC_ROLE_SNK);
#else
		tcpm_typec_change_role_postpone(pinfo->tcpc, TYPEC_ROLE_SNK, true);
#endif
	}
}

int oplus_chg_get_main_ibat(void)
{
	int ibat = 0;
	int ret = 0;
	struct charger_device *chg = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	ret = charger_dev_get_ibat(chg, &ibat);
	if (ret < 0) {
		pr_err("[%s] get ibat fail\n", __func__);
		return -1;
	}

	return ibat / 1000;
}

static void set_usbswitch_to_rxtx(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output2);
	mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
	}
	chg_err("set_usbswitch_to_rxtx\n");
	return;
}

static void set_usbswitch_to_dpdm(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output1);
	mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return;
	}
	chg_err("set_usbswitch_to_dpdm\n");
}
static bool chargerid_support = false;
static bool is_support_chargerid_check(void)
{
#ifdef CONFIG_OPLUS_CHECK_CHARGERID_VOLT
	return chargerid_support;
#else
	return false;
#endif
}

int mt_get_chargerid_volt(void)
{
	int chargerid_volt = 0;
	int rc = 0;

	if (!pinfo->charger_id_chan) {
                chg_err("charger_id_chan NULL\n");
                return 0;
        }

	if (is_support_chargerid_check() == true) {
		rc = iio_read_channel_processed(pinfo->charger_id_chan, &chargerid_volt);
		if (rc < 0) {
			chg_err("read charger_id_chan fail, rc=%d\n", rc);
			return 0;
		}

		chargerid_volt = chargerid_volt * 1500 / 4096;
		chg_debug("chargerid_volt=%d\n", chargerid_volt);
	} else {
		chg_debug("is_support_chargerid_check=false!\n");
		return 0;
	}

	return chargerid_volt;
}
EXPORT_SYMBOL(mt_get_chargerid_volt);

void mt_set_chargerid_switch_val(int value)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (is_support_chargerid_check() == false)
		return;

	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return;
	}
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
			|| IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output1)
			|| IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value == 1) {
		set_usbswitch_to_rxtx(chip);
	} else if (value == 0) {
		set_usbswitch_to_dpdm(chip);
	} else {
		//do nothing
	}
	chg_debug("get_val=%d\n", gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio));

	return;
}
EXPORT_SYMBOL(mt_set_chargerid_switch_val);

int mt_get_chargerid_switch_val(void)
{
	int gpio_status = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	if (is_support_chargerid_check() == false)
		return 0;

	gpio_status = gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio);

	chg_debug("mt_get_chargerid_switch_val=%d\n", gpio_status);

	return gpio_status;
}
EXPORT_SYMBOL(mt_get_chargerid_switch_val);

static int oplus_usb_switch_gpio_gpio_init(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.chargerid_switch_gpio pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.charger_gpio_as_output1 =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
			"charger_gpio_as_output_low");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output1)) {
		chg_err("get charger_gpio_as_output_low fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.charger_gpio_as_output2 =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
			"charger_gpio_as_output_high");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("get charger_gpio_as_output_high fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
			chip->normalchg_gpio.charger_gpio_as_output1);

	return 0;
}

static int oplus_chg_chargerid_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chargerid_support = of_property_read_bool(node, "qcom,chargerid_support");
	if (chargerid_support == false){
		chg_err("not support chargerid\n");
		//return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_gpio =
			of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("Couldn't read chargerid_switch-gpio rc=%d, chargerid_switch-gpio:%d\n",
				rc, chip->normalchg_gpio.chargerid_switch_gpio);
	} else {
		if (gpio_is_valid(chip->normalchg_gpio.chargerid_switch_gpio)) {
			rc = gpio_request(chip->normalchg_gpio.chargerid_switch_gpio, "charging_switch1-gpio");
			if (rc) {
				chg_err("unable to request chargerid_switch-gpio:%d\n",
						chip->normalchg_gpio.chargerid_switch_gpio);
			} else {
				rc = oplus_usb_switch_gpio_gpio_init();
				if (rc)
					chg_err("unable to init chargerid_switch-gpio:%d\n",
							chip->normalchg_gpio.chargerid_switch_gpio);
			}
		}
		chg_err("chargerid_switch-gpio:%d\n", chip->normalchg_gpio.chargerid_switch_gpio);
	}

	return rc;
}

static bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio)) {
		return true;
	}

	return false;
}

static int oplus_shortc_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	chip->normalchg_gpio.shortc_active =
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "shortc_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
		chg_err("get shortc_active fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.shortc_active);

	return 0;
}

static int oplus_chg_shortc_hw_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

        if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.shortc_gpio = of_get_named_gpio(node, "qcom,shortc-gpio", 0);
	if (chip->normalchg_gpio.shortc_gpio <= 0) {
		chg_err("Couldn't read qcom,shortc-gpio rc=%d, qcom,shortc-gpio:%d\n",
				rc, chip->normalchg_gpio.shortc_gpio);
	} else {
		if (oplus_shortc_check_is_gpio(chip) == true) {
			rc = gpio_request(chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
			if (rc) {
				chg_err("unable to request shortc-gpio:%d\n",
						chip->normalchg_gpio.shortc_gpio);
			} else {
				rc = oplus_shortc_gpio_init(chip);
				if (rc)
					chg_err("unable to init shortc-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
			}
		}
		chg_err("shortc-gpio:%d\n", chip->normalchg_gpio.shortc_gpio);
	}

	return rc;
}

static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

static int oplus_ship_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	chip->normalchg_gpio.ship_active = 
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
			"ship_active");

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}
	chip->normalchg_gpio.ship_sleep = 
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
				"ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
		chip->normalchg_gpio.ship_sleep);

	return 0;
}

#define SHIP_MODE_CONFIG		0x40
#define SHIP_MODE_MASK			BIT(0)
#define SHIP_MODE_ENABLE		0
#define PWM_COUNT				5
static void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
	int i = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ship_check_is_gpio(chip) == true) {
		chg_err("select gpio control\n");
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active) && !IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
			for (i = 0; i < PWM_COUNT; i++) {
				//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 1);
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
				mdelay(3);
				//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 0);
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
				mdelay(3);
			}
		}
		chg_err("power off after 15s\n");
	} else {
		oplus_chg_set_shipping_mode();
		chg_err("power off after 15s\n");
	}
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#define CHARGER_25C_VOLT	900
struct chtemperature {
	__s32 bts_temp;
	__s32 temperature_r;
};

#define TEMP_25C 250

static struct temp_param sub_board_temp_table[] = {
	{-40, 4397119}, {-39, 4092874}, {-38, 3811717}, {-37, 3551749}, {-36, 3311236}, {-35, 3088599}, {-34, 2882396}, {-33, 2691310},
	{-32, 2514137}, {-31, 2349778}, {-30, 2197225}, {-29, 2055558}, {-28, 1923932}, {-27, 1801573}, {-26, 1687773}, {-25, 1581881},
	{-24, 1483100}, {-23, 1391113}, {-22, 1305413}, {-21, 1225531}, {-20, 1151037}, {-19, 1081535}, {-18, 1016661}, {-17,  956080},
	{-16,  899481}, {-15,  846579}, {-14,  797111}, {-13,  750834}, {-12,  707524}, {-11,  666972}, {-10,  628988}, {-9,   593342},
	{-8,   559931}, {-7,   528602}, {-6,   499212}, {-5,   471632}, {-4,   445772}, {-3,   421480}, {-2,   398652}, {-1,   377193},
	{0,    357012}, {1,    338006}, {2,    320122}, {3,    303287}, {4,    287434}, {5,    272500}, {6,    258426}, {7,    245160},
	{8,    232649}, {9,    220847}, {10,   209710}, {11,   199196}, {12,   189268}, {13,   179890}, {14,   171028}, {15,   162651},
	{16,   154726}, {17,   147232}, {18,   140142}, {19,   133432}, {20,   127080}, {21,   121066}, {22,   115368}, {23,   109970},
	{24,   104852}, {25,   100000}, {26,   95398 }, {27,   91032 }, {28,   86889 }, {29,   82956 }, {30,   79222 }, {31,   75675 },
	{32,   72306 }, {33,   69104 }, {34,   66061 }, {35,   63167 }, {36,   60415 }, {37,   57797 }, {38,   55306 }, {39,   52934 },
	{40,   50677 }, {41,   48528 }, {42,   46482 }, {43,   44533 }, {44,   42675 }, {45,   40904 }, {46,   39213 }, {47,   37601 },
	{48,   36063 }, {49,   34595 }, {50,   33195 }, {51,   31859 }, {52,   30584 }, {53,   29366 }, {54,   28203 }, {55,   27091 },
	{56,   26028 }, {57,   25013 }, {58,   24042 }, {59,   23113 }, {60,   22224 }, {61,   21374 }, {62,   20561 }, {63,   19782 },
	{64,   19036 }, {65,   18323 }, {66,   17640 }, {67,   16986 }, {68,   16360 }, {69,   15760 }, {70,   15184 }, {71,   14631 },
	{72,   14101 }, {73,   13592 }, {74,   13104 }, {75,   12635 }, {76,   12187 }, {77,   11757 }, {78,   11344 }, {79,   10947 },
	{80,   10566 }, {81,   10200 }, {82,     9848}, {83,     9510}, {84,     9185}, {85,     8873}, {86,     8572}, {87,     8283},
	{88,     8006}, {89,     7738}, {90,     7481}, {91,     7234}, {92,     6997}, {93,     6769}, {94,     6548}, {95,     6337},
	{96,     6132}, {97,     5934}, {98,     5744}, {99,     5561}, {100,    5384}, {101,    5214}, {102,    5051}, {103,    4893},
	{104,    4741}, {105,    4594}, {106,    4453}, {107,    4316}, {108,    4184}, {109,    4057}, {110,    3934}, {111,    3816},
	{112,    3701}, {113,    3591}, {114,    3484}, {115,    3380}, {116,    3281}, {117,    3185}, {118,    3093}, {119,    3003},
	{120,    2916}, {121,    2832}, {122,    2751}, {123,    2672}, {124,    2596}, {125,    2522}
};

static struct chtemperature charger_ic_temp_table[] = {
	{-40, 4251000}, {-39, 3962000}, {-38, 3695000}, {-37, 3447000}, {-36, 3218000}, {-35, 3005000},
	{-34, 2807000}, {-33, 2624000}, {-32, 2454000}, {-31, 2296000}, {-30, 2149000}, {-29, 2012000},
	{-28, 1885000}, {-27, 1767000}, {-26, 1656000}, {-25, 1554000}, {-24, 1458000}, {-23, 1369000},
	{-22, 1286000}, {-21, 1208000}, {-20, 1135000}, {-19, 1068000}, {-18, 1004000}, {-17, 945000 },
	{-16, 889600 }, {-15, 837800 }, {-14, 789300 }, {-13, 743900 }, {-12, 701300 }, {-11, 661500 },
	{-10, 624100 }, {-9, 589000  }, {-8, 556200  }, {-7, 525300  }, {-6, 496300  }, {-5, 469100  },
	{-4, 443500  }, {-3, 419500  }, {-2, 396900  }, {-1, 375600  }, {0, 355600   }, {1, 336800   },
	{2, 319100   }, {3, 302400   }, {4, 286700   }, {5, 271800   }, {6, 257800   }, {7, 244700   },
	{8, 232200   }, {9, 220500   }, {10, 209400  }, {11, 198900  }, {12, 189000  }, {13, 179700  },
	{14, 170900  }, {15, 162500  }, {16, 154600  }, {17, 147200  }, {18, 140100  }, {19, 133400  },
	{20, 127000  }, {21, 121000  }, {22, 115400  }, {23, 110000  }, {24, 104800  }, {25, 100000  },
	{26, 95400   }, {27, 91040   }, {28, 86900   }, {29, 82970   }, {30, 79230   }, {31, 75690   },
	{32, 72320   }, {33, 69120   }, {34, 66070   }, {35, 63180   }, {36, 60420   }, {37, 57810   },
	{38, 55310   }, {39, 52940   }, {40, 50680   }, {41, 48530   }, {42, 46490   }, {43, 44530   },
	{44, 42670   }, {45, 40900   }, {46, 39210   }, {47, 37600   }, {48, 36060   }, {49, 34600   },
	{50, 33190   }, {51, 31860   }, {52, 30580   }, {53, 29360   }, {54, 28200   }, {55, 27090   },
	{56, 26030   }, {57, 25010   }, {58, 24040   }, {59, 23110   }, {60, 22220   }, {61, 21370   },
	{62, 20560   }, {63, 19780   }, {64, 19040   }, {65, 18320   }, {66, 17640   }, {67, 16990   },
	{68, 16360   }, {69, 15760   }, {70, 15180   }, {71, 14630   }, {72, 14100   }, {73, 13600   },
	{74, 13110   }, {75, 12640   }, {76, 12190   }, {77, 11760   }, {78, 11350   }, {79, 10960   },
	{80, 10580   }, {81, 10210   }, {82, 9859    }, {83, 9522    }, {84, 9198    }, {85, 8887    },
	{86, 8587    }, {87, 8299    }, {88, 8022    }, {89, 7756    }, {90, 7500    }, {91, 7254    },
	{92, 7016    }, {93, 6788    }, {94, 6568    }, {95, 6357    }, {96, 6153    }, {97, 5957    },
	{98, 5768    }, {99, 5586    }, {100, 5410   }, {101, 5241   }, {102, 5078   }, {103, 4921   },
	{104, 4769   }, {105, 4623   }, {106, 4482   }, {107, 4346   }, {108, 4215   }, {109, 4088   },
	{110, 3965   }, {111, 3847   }, {112, 3733   }, {113, 3623   }, {114, 3517   }, {115, 3415   },
	{116, 3315   }, {117, 3220   }, {118, 3127   }, {119, 3038   }, {120, 2951   }, {121, 2868   },
	{122, 2787   }, {123, 2709   }, {124, 2633   }, {125, 2560   }
};

static __s16 oplus_ch_thermistor_conver_temp(__s32 res)
{
	int i = 0;
	int asize = 0;
	__s32 res1 = 0, res2 = 0;
	__s32 tap_value = -2000, tmp1 = 0, tmp2 = 0;

	asize = (sizeof(charger_ic_temp_table) / sizeof(struct chtemperature));

	if (res >= charger_ic_temp_table[0].temperature_r) {
		tap_value = -400;	/* min */
	} else if (res <= charger_ic_temp_table[asize - 1].temperature_r) {
		tap_value = 1250;	/* max */
	} else {
		res1 = charger_ic_temp_table[0].temperature_r;
		tmp1 = charger_ic_temp_table[0].bts_temp;

		for (i = 0; i < asize; i++) {
			if (res >= charger_ic_temp_table[i].temperature_r) {
				res2 = charger_ic_temp_table[i].temperature_r;
				tmp2 = charger_ic_temp_table[i].bts_temp;
				break;
			}
			res1 = charger_ic_temp_table[i].temperature_r;
			tmp1 = charger_ic_temp_table[i].bts_temp;
		}

		tap_value = (((res - res2) * tmp1) + ((res1 - res) * tmp2)) * 10 / (res1 - res2);
	}

	return tap_value;
}

static __s16 oplus_ts_ch_volt_to_temp(__u32 dwvolt)
{
	__s32 tres;
	__u64 dwvcrich = 0;
	__s32 chg_tmp = -100;
	__u64 dwvcrich2 = 0;
	const int g_tap_over_critical_low = 4251000;
	const int g_rap_pull_up_r = 100000; /* 100K, pull up resister */
	const int g_rap_pull_up_voltage = 1840;
	dwvcrich = ((__u64)g_tap_over_critical_low * (__u64)g_rap_pull_up_voltage);
	dwvcrich2 = (g_tap_over_critical_low + g_rap_pull_up_r);
	do_div(dwvcrich, dwvcrich2);

	if (dwvolt > ((__u32)dwvcrich)) {
		tres = g_tap_over_critical_low;
	} else {
		tres = (g_rap_pull_up_r * dwvolt) / (g_rap_pull_up_voltage - dwvolt);
	}

	/* convert register to temperature */
	chg_tmp = oplus_ch_thermistor_conver_temp(tres);

	return chg_tmp;
}

int oplus_get_chargeric_temp(void)
{
	int val = 0;
	int ret = 0, output;

	if (pinfo && pinfo->chargeric_temp_chan) {
		ret = iio_read_channel_processed(pinfo->chargeric_temp_chan, &val);
		if (ret< 0) {
			chg_err("read usb_temp_v_r_chan volt failed, rc=%d\n", ret);
			return ret;
		}
	}

	if (val <= 0) {
		val = CHARGER_25C_VOLT;
	}

	ret = val;
	output = oplus_ts_ch_volt_to_temp(ret);
	if (pinfo->support_ntc_01c_precision) {
		pinfo->chargeric_temp = output;
	} else {
		pinfo->chargeric_temp = output / 10;
	}

	return pinfo->chargeric_temp;
}

int oplus_mt6375_get_tchg(int *tchg_min,	int *tchg_max)
{
	if (pinfo != NULL) {
		oplus_get_chargeric_temp();
		*tchg_min = pinfo->chargeric_temp;
		*tchg_max = pinfo->chargeric_temp;
		return pinfo->chargeric_temp;
	}

	return -EBUSY;
}

bool oplus_tchg_01c_precision(void)
{
	if (!pinfo) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: charger_data not ready!\n", __func__);
		return false;
	}
	return pinfo->support_ntc_01c_precision;
}
EXPORT_SYMBOL(oplus_tchg_01c_precision);
#endif

static void enter_ship_mode_function(struct oplus_chg_chip *chip)
{

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (chip->enable_shipmode) {
		smbchg_enter_shipmode(chip);
		printk(KERN_ERR "[OPLUS_CHG][%s]: enter_ship_mode_function\n", __func__);
	}
}

static int oplus_chg_shipmode_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

        if (chip != NULL)
        	node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ship_gpio =
			of_get_named_gpio(node, "qcom,ship-gpio", 0);
	if (chip->normalchg_gpio.ship_gpio <= 0) {
		chg_err("Couldn't read qcom,ship-gpio rc = %d, qcom,ship-gpio:%d\n",
				rc, chip->normalchg_gpio.ship_gpio);
	} else {
		if (oplus_ship_check_is_gpio(chip) == true) {
			rc = gpio_request(chip->normalchg_gpio.ship_gpio, "ship-gpio");
			if (rc) {
				chg_err("unable to request ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
			} else {
				rc = oplus_ship_gpio_init(chip);
				if (rc)
					chg_err("unable to init ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
			}
		}
		chg_err("ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
	}

	return rc;
}

static struct temp_param battcon_temp_table[] = {
	{-40, 4251000}, {-39, 3962000}, {-38, 3695000}, {-37, 3447000}, {-36, 3218000}, {-35, 3005000}, {-34, 2807000}, {-33, 2624000},
	{-32, 2454000}, {-31, 2296000}, {-30, 2149000}, {-29, 2012000}, {-28, 1885000}, {-27, 1767000}, {-26, 1656000}, {-25, 1554000},
	{-24, 1458000}, {-23, 1369000}, {-22, 1286000}, {-21, 1208000}, {-20, 1135000}, {-19, 1068000}, {-18, 1004000}, {-17,  945000},
	{-16,  889600}, {-15,  837800}, {-14,  789300}, {-13,  743900}, {-12,  701300}, {-11,  661500}, {-10,  624100}, {-9,   589000},
	{-8,   556200}, {-7,   525300}, {-6,   496300}, {-5,   469100}, {-4,   443500}, {-3,   419500}, {-2,   396900}, {-1,   375600},
	{0,    355600}, {1,    336800}, {2,    319100}, {3,    302400}, {4,    286700}, {5,    271800}, {6,    257800}, {7,    244700},
	{8,    232200}, {9,    220500}, {10,   209400}, {11,   198900}, {12,   189000}, {13,   179700}, {14,   170900}, {15,   162500},
	{16,   154600}, {17,   147200}, {18,   140100}, {19,   133400}, {20,   127000}, {21,   121000}, {22,   115400}, {23,   110000},
	{24,   104800}, {25,   100000}, {26,   95400 }, {27,   91040 }, {28,   86900 }, {29,   82970 }, {30,   79230 }, {31,   75690 },
	{32,   72320 }, {33,   69120 }, {34,   66070 }, {35,   63180 }, {36,   60420 }, {37,   57810 }, {38,   55310 }, {39,   52940 },
	{40,   50680 }, {41,   48530 }, {42,   46490 }, {43,   44530 }, {44,   42670 }, {45,   40900 }, {46,   39210 }, {47,   37600 },
	{48,   36060 }, {49,   34595 }, {50,   33195 }, {51,   31859 }, {52,   30584 }, {53,   29366 }, {54,   28203 }, {55,   27091 },
	{56,   26028 }, {57,   25013 }, {58,   24042 }, {59,   23113 }, {60,   22224 }, {61,   21374 }, {62,   20561 }, {63,   19782 },
	{64,   19036 }, {65,   18323 }, {66,   17640 }, {67,   16986 }, {68,   16360 }, {69,   15760 }, {70,   15184 }, {71,   14631 },
	{72,   14101 }, {73,   13592 }, {74,   13104 }, {75,   12635 }, {76,   12187 }, {77,   11757 }, {78,   11344 }, {79,   10947 },
	{80,   10566 }, {81,   10200 }, {82,     9848}, {83,     9510}, {84,     9185}, {85,     8873}, {86,     8572}, {87,     8283},
	{88,     8006}, {89,     7738}, {90,     7481}, {91,     7234}, {92,     6997}, {93,     6769}, {94,     6548}, {95,     6337},
	{96,     6132}, {97,     5934}, {98,     5744}, {99,     5561}, {100,    5384}, {101,    5214}, {102,    5051}, {103,    4893},
	{104,    4769}, {105,    4623}, {106,    4482}, {107,    4346}, {108,    4215}, {109,    4088}, {110,    3966}, {111,    3848},
	{112,    3734}, {113,    3624}, {114,    3518}, {115,    3415}, {116,    3316}, {117,    3220}, {118,    3128}, {119,    3038},
	{120,    2952}, {121,    2868}, {122,    2787}, {123,    2709}, {124,    2634}, {125,    2561}
};

static struct ntc_temp *init_ntc_param(char *str)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct device_node *node = NULL;
	struct ntc_temp *ntc_param = NULL;
	int length;
	int rc;

	if (!chip)
		return NULL;

	node = chip->dev->of_node;
	if (!node)
		return NULL;

	length = of_property_count_elems_of_size(node, str, sizeof(u32));
	if (length < 0) {
		chg_err("%s no configure, use defalult setting\n", str);
		return NULL;
	} else {
		if (length != NTC_PARAM_NUM) {
			chg_err("%s configure wrong, use defalult setting\n", str);
			return NULL;
		}
		ntc_param = devm_kzalloc(chip->dev, sizeof(struct ntc_temp), GFP_KERNEL);
		rc = of_property_read_u32_array(node, str, (u32 *)ntc_param, length);
		if (rc < 0) {
			chg_err("parse %s failed, rc=%d\n", str, rc);
			devm_kfree(chip->dev, ntc_param);
			return NULL;
		} else {
			chg_err("%s: %d %d %d %d %d %d %d\n",
				str, ntc_param->i_tap_over_critical_low,
				ntc_param->i_rap_pull_up_r, ntc_param->i_rap_pull_up_voltage,
				ntc_param->i_tap_min, ntc_param->i_tap_max,
				ntc_param->i_25c_volt, ntc_param->volt_offset);
			ntc_param->pst_temp_table = battcon_temp_table;
			ntc_param->i_table_size = (sizeof(battcon_temp_table) / sizeof(struct temp_param));
		}
	}

	return ntc_param;
}
/* ntc_resistance:100k internal_pull_up:100k voltage:1.84v */
int con_temp_ntc_100k_1840mv[] = {
	-40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22,
	-21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
	26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
	74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
	98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,
	117, 118, 119, 120, 121, 122, 123, 124, 125
};

int con_volt_ntc_100k_1840mv[] = {
	1799, 1796, 1793, 1790, 1786, 1782, 1778, 1774, 1770, 1765, 1760, 1755, 1749, 1743, 1737, 1731,
	1724, 1717, 1709, 1701, 1693, 1684, 1675, 1666, 1656, 1646, 1635, 1624, 1612, 1600, 1588, 1575,
	1561, 1547, 1533, 1518, 1503, 1478, 1471, 1454, 1437, 1420, 1402, 1384, 1365, 1346, 1327, 1307,
	1287, 1267, 1246, 1225, 1204, 1183, 1161, 1139, 1118, 1096, 1074, 1052, 1030, 1008, 986, 964,
	942, 920, 898, 877, 855, 834, 813, 793, 772, 752, 732, 712, 693, 674, 655, 637, 619, 601, 584,
	567, 550, 534, 518, 503, 488, 473, 459, 445, 431, 418, 405, 392, 380, 368, 357, 345, 335, 324,
	314, 304, 294, 285, 276, 267, 259, 251, 243, 235, 227, 220, 213, 206, 200, 194, 187, 182, 176,
	170, 165, 160, 155, 150, 145, 140, 136, 132, 128, 124, 120, 117, 113, 110, 106, 103, 100, 97,
	94, 91, 88, 86, 83, 81, 78, 76, 74, 72, 70, 68, 66, 64, 62, 60, 58, 57, 55, 54, 52, 51, 49, 48,
	47, 45
};
/* ntc_resistance:100k internal_pull_up:100k voltage:1.84v */

static __s16 oplus_thermistor_conver_temp(__s32 res, struct ntc_temp *ntc_param)
{
	int i = 0;
	int asize = 0;
	__s32 res1 = 0, res2 = 0;
	__s32 tap_value = -2000, tmp1 = 0, tmp2 = 0;

	if (!ntc_param)
		return tap_value;

	asize = ntc_param->i_table_size;

	if (res >= ntc_param->pst_temp_table[0].temperature_r) {
		tap_value = ntc_param->i_tap_min;	/* min */
	} else if (res <= ntc_param->pst_temp_table[asize - 1].temperature_r) {
		tap_value = ntc_param->i_tap_max;	/* max */
	} else {
		res1 = ntc_param->pst_temp_table[0].temperature_r;
		tmp1 = ntc_param->pst_temp_table[0].bts_temp;

		for (i = 0; i < asize; i++) {
			if (res >= ntc_param->pst_temp_table[i].temperature_r) {
				res2 = ntc_param->pst_temp_table[i].temperature_r;
				tmp2 = ntc_param->pst_temp_table[i].bts_temp;
				break;
			}
			res1 = ntc_param->pst_temp_table[i].temperature_r;
			tmp1 = ntc_param->pst_temp_table[i].bts_temp;
		}
		if (res1 != res2)
			tap_value = (((res - res2) * tmp1) + ((res1 - res) * tmp2)) * 10 / (res1 - res2);
		else
			tap_value = DEFUALT_TEMP * 10;
	}

	return tap_value;
}

static __s16 oplus_res_to_temp(struct ntc_temp *ntc_param)
{
	__s32 tres;
	__u64 dwvcrich = 0;
	__s32 chg_tmp = -100;
	__u64 dwvcrich2 = 0;

	if (!ntc_param) {
		chg_err("ntc param is null\n");
		return DEFUALT_TEMP * 10;
	}
	dwvcrich = ((__u64)ntc_param->i_tap_over_critical_low * (__u64)ntc_param->i_rap_pull_up_voltage);
	dwvcrich2 = (ntc_param->i_tap_over_critical_low + ntc_param->i_rap_pull_up_r);
	do_div(dwvcrich, dwvcrich2);

	if (ntc_param->ui_dwvolt > ((__u32)dwvcrich)) {
		tres = ntc_param->i_tap_over_critical_low;
	} else {
		if (ntc_param->i_rap_pull_up_voltage != ntc_param->ui_dwvolt) {
			tres = (ntc_param->i_rap_pull_up_r * ntc_param->ui_dwvolt)
				/ (ntc_param->i_rap_pull_up_voltage - ntc_param->ui_dwvolt);
		} else {
			return DEFUALT_TEMP * 10;
		}
	}

	/* convert register to temperature */
	chg_tmp = oplus_thermistor_conver_temp(tres, ntc_param);

	return chg_tmp;
}


static int oplus_get_temp_volt(struct iio_channel *ntc_temp_chan, struct ntc_temp *ntc_param)
{
	int rc = 0;
	int ntc_temp_volt = 0;

	if (!ntc_param || !pinfo) {
		return -1;
	}

	if (!ntc_temp_chan) {
		chg_err("unsupported ntc\n");
		return -1;
	}
	rc = iio_read_channel_processed(ntc_temp_chan, &ntc_temp_volt);
	if (rc < 0) {
		chg_err("read ntc_temp_chan volt failed, rc=%d\n", rc);
	}

	if (ntc_temp_volt <= 0) {
		chg_err("ntc volt error use default volt\n");
		ntc_temp_volt = ntc_param->i_25c_volt;
	}

	return ntc_temp_volt;
}


static int oplus_get_ntc_temp(struct iio_channel *ntc_temp_chan, struct ntc_temp *ntc_param)
{
	int chargeric_temp = 0;
	static bool is_param_init = false;
	static struct ntc_temp ntc_param_default = {0};

	if (!is_param_init) {
		ntc_param_default.i_tap_over_critical_low = BTB_CRITICAL_LOW;
		ntc_param_default.i_rap_pull_up_r = BTB_PU_R;
		ntc_param_default.i_rap_pull_up_voltage = BTB_PU_VOLTAGE;
		ntc_param_default.i_tap_min = BTB_TEMP_MIN;
		ntc_param_default.i_tap_max = BTB_TEMP_MAX;
		ntc_param_default.i_25c_volt = BTB_25C_VOLT;
		ntc_param_default.volt_offset = BTB_OFFSET;
		ntc_param_default.pst_temp_table = battcon_temp_table;
		ntc_param_default.i_table_size = (sizeof(battcon_temp_table) / sizeof(struct temp_param));
		is_param_init = true;
		chg_err("ntc_param_default critical_low:%d,pull_up_r=%d,pull_up_voltage=%d,tap_min=%d,tap_max=%d,table_size=%d\n",
			ntc_param_default.i_tap_over_critical_low, ntc_param_default.i_rap_pull_up_r,
			ntc_param_default.i_rap_pull_up_voltage, ntc_param_default.i_tap_min,
			ntc_param_default.i_tap_max, ntc_param_default.i_table_size);
	}
	if (!ntc_param) {
		ntc_param_default.ui_dwvolt = oplus_get_temp_volt(ntc_temp_chan, &ntc_param_default);
		ntc_param_default.ui_dwvolt = ntc_param_default.ui_dwvolt + ntc_param_default.volt_offset;
		chargeric_temp = oplus_res_to_temp(&ntc_param_default);
	} else {
		ntc_param->ui_dwvolt = oplus_get_temp_volt(ntc_temp_chan, ntc_param);
		ntc_param->ui_dwvolt = ntc_param->ui_dwvolt + ntc_param->volt_offset;
		chargeric_temp = oplus_res_to_temp(ntc_param);
	}

	return chargeric_temp;
}

static int oplus_chg_get_main_battery_btb(void)
{
	int temp = DEFUALT_TEMP;

	if (!pinfo || !g_oplus_chip) {
		chg_err("pinfo is NULL\n");
		return DEFUALT_TEMP;
	}

	if (!pinfo->batcon_temp_chan) {
		pinfo->batcon_temp_chan = iio_channel_get(g_oplus_chip->dev, "batcon_temp");
		if (IS_ERR(pinfo->batcon_temp_chan)) {
			chg_err("Couldn't get batcon_temp_chan...\n");
			pinfo->batcon_temp_chan = NULL;
		}
	}

	if (!IS_ERR_OR_NULL(pinfo->batcon_temp_chan)) {
		temp = oplus_get_ntc_temp(pinfo->batcon_temp_chan, pinfo->batt_ntc_param);
		return temp;
	}

	return DEFUALT_TEMP;
}

static int oplus_chg_get_sub_battery_btb(void)
{
	int sub_temp = DEFUALT_TEMP;

	if (!pinfo || !g_oplus_chip) {
		chg_err("pinfo is NULL\n");
		return DEFUALT_TEMP;
	}

	if (!pinfo->sub_batcon_temp_chan) {
		pinfo->sub_batcon_temp_chan = iio_channel_get(g_oplus_chip->dev, "sub_batcon_temp");
		if (IS_ERR(pinfo->sub_batcon_temp_chan)) {
			chg_err("Couldn't get sub_batcon_temp_chan...\n");
			pinfo->sub_batcon_temp_chan = NULL;
		}
	}

	if (!IS_ERR_OR_NULL(pinfo->sub_batcon_temp_chan)) {
		sub_temp = oplus_get_ntc_temp(pinfo->sub_batcon_temp_chan, pinfo->sub_batt_ntc_param);
		return sub_temp;
	}

	return DEFUALT_TEMP;
}

int oplus_chg_get_battery_btb_temp_cal(void)
{
	int temp = DEFUALT_TEMP;
	int sub_temp = DEFUALT_TEMP;

	temp = oplus_chg_get_main_battery_btb() / 10;
	sub_temp = oplus_chg_get_sub_battery_btb() / 10;

	return temp > sub_temp ? temp : sub_temp;
}
EXPORT_SYMBOL(oplus_chg_get_battery_btb_temp_cal);

int oplus_chg_get_usb_btb_temp_cal(void)
{
	int temp = DEFUALT_TEMP;

	if (!pinfo || !g_oplus_chip) {
		chg_err("pinfo is NULL\n");
		return DEFUALT_TEMP;
	}

	if (!pinfo->usbcon_temp_chan) {
		pinfo->usbcon_temp_chan = iio_channel_get(g_oplus_chip->dev, "usbcon_temp");
		if (IS_ERR(pinfo->usbcon_temp_chan)) {
			chg_err("Couldn't get usbcon_temp_chan...\n");
			pinfo->usbcon_temp_chan = NULL;
		}
	}

	if (pinfo->usbcon_temp_chan) {
		temp = oplus_get_ntc_temp(pinfo->usbcon_temp_chan, pinfo->usb_ntc_param);
		temp = temp / 10;

		return temp;
	} else {
		chg_err("Couldn't get usbcon_temp_chan, return default\n");
		return DEFUALT_TEMP;
	}
}
EXPORT_SYMBOL(oplus_chg_get_usb_btb_temp_cal);


int oplus_force_get_subboard_temp(void)
{
	int subboard_temp = 0;
	int rc = 0;
	int ntc_temp_volt = 0;
	static bool is_param_init = false;
	static struct ntc_temp ntc_param = {0};

	if (!probe_done) {
		chg_err("%s probe not done\n", __func__);
		return TEMP_25C;
	}

	if (!is_param_init) {
		ntc_param.i_tap_over_critical_low = BTB_CRITICAL_LOW;
		ntc_param.i_rap_pull_up_r = BTB_PU_R;
		ntc_param.i_rap_pull_up_voltage = BTB_PU_VOLTAGE;
		ntc_param.i_tap_min = BTB_TEMP_MIN;
		ntc_param.i_tap_max = BTB_TEMP_MAX;
		ntc_param.i_25c_volt = BTB_25C_VOLT;
		ntc_param.pst_temp_table = sub_board_temp_table;
		ntc_param.i_table_size = (sizeof(sub_board_temp_table) / sizeof(struct temp_param));
		is_param_init = true;

		chg_err("ntc_temp ,critical_low:%d,pull_up_r=%d,pull_up_voltage=%d,tap_min=%d,tap_max=%d,table_size=%d\n", \
			ntc_param.i_tap_over_critical_low, ntc_param.i_rap_pull_up_r, \
			ntc_param.i_rap_pull_up_voltage, ntc_param.i_tap_min, ntc_param.i_tap_max, ntc_param.i_table_size);
	}

	rc = iio_read_channel_processed(pinfo->subboard_temp_chan, &ntc_temp_volt);
	if (rc < 0) {
		chg_err("%s read ntc_temp_chan volt failed, rc=%d\n", __func__, rc);
		ntc_temp_volt = ntc_param.i_25c_volt;
	}

	ntc_param.ui_dwvolt = ntc_temp_volt;
	subboard_temp = oplus_res_to_temp(&ntc_param);

	if (!pinfo->support_ntc_01c_precision)
		subboard_temp = subboard_temp / 10;

	chg_err("subboard_temp:%d,volt:%d\n", subboard_temp, ntc_param.ui_dwvolt);
	if (get_eng_version() == HIGH_TEMP_AGING) {
		chg_err("CONFIG_HIGH_TEMP_VERSION enable here, force batt_temp = %d \n", BTB_HIGH_TEMP_AGING);
		if (subboard_temp > BTB_HIGH_TEMP_AGING)
			return BTB_HIGH_TEMP_AGING;
	}

	return subboard_temp;
}
EXPORT_SYMBOL(oplus_force_get_subboard_temp);



static bool oplus_usbtemp_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio))
		return true;

	return false;
}

static bool oplus_ccdetect_check_is_wd0(struct oplus_chg_chip *chip)
{
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		printk(KERN_ERR "device tree info missing\n", __func__);
		return false;
	}

	if (chip->support_wd0)
		return true;
	if (of_property_read_bool(node, "qcom,ccdetect_by_wd0")) {
		chip->support_wd0 = true;
		return true;
	}

	return false;
}

bool oplus_chg_get_wd0_status(void)
{
	if (!pinfo) {
		pr_err("%s, pinfo null!\n", __func__);
		return false;
	}

	return pinfo->wd0_detect;
}

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->chgic_mtk.oplus_info->ccdetect_gpio)){
		//pr_err("[ccdetecct]has gpio ");
		return true;
	}

	return false;
}

bool oplus_ccdetect_support_check(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(chip) == true)
		return true;

	chg_err("not support, return false\n");

	return false;
}

int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->pinctrl)) {
		chg_err("get ccdetect_pinctrl fail\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->ccdetect_active = pinctrl_lookup_state(chip->chgic_mtk.oplus_info->pinctrl, "ccdetect_active");
	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->ccdetect_active)) {
		chg_err("get ccdetect_active fail\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->ccdetect_sleep = pinctrl_lookup_state(chip->chgic_mtk.oplus_info->pinctrl, "ccdetect_sleep");
	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->ccdetect_sleep)) {
		chg_err("get ccdetect_sleep fail\n");
		return -EINVAL;
	}
	if (chip->chgic_mtk.oplus_info->ccdetect_gpio > 0) {
		gpio_direction_input(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	}

	pinctrl_select_state(chip->chgic_mtk.oplus_info->pinctrl,  chip->chgic_mtk.oplus_info->ccdetect_active);
	return 0;
}
struct delayed_work usbtemp_recover_work;
void oplus_ccdetect_work(struct work_struct *work)
{
	int level;
	struct oplus_chg_chip *chip = g_oplus_chip;
	level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	pr_err("%s: level [%d]", __func__, level);
	if (level != 1) {
		oplus_ccdetect_enable();
	        oplus_wake_up_usbtemp_thread();
	} else {
		oplus_chg_clear_abnormal_adapter_var();
		if (g_oplus_chip)
			g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();

		if (oplus_get_otg_switch_status() == false)
			oplus_ccdetect_disable();
		if(g_oplus_chip->usb_status == USB_TEMP_HIGH) {
			schedule_delayed_work(&usbtemp_recover_work, 0);
		}
	}

}

void oplus_wd0_detect_work(struct work_struct *work)
{
	int level;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("%s: g_oplus_chip not ready!\n", __func__);
		return;
	}

	level = !oplus_chg_get_wd0_status();
	pr_err("%s: level [%d]", __func__, level);

	if (level != 1) {
		oplus_wake_up_usbtemp_thread();
	} else {
		oplus_chg_clear_abnormal_adapter_var();
		chip->usbtemp_check = oplus_usbtemp_condition();

		if (chip->usb_status == USB_TEMP_HIGH) {
			schedule_delayed_work(&usbtemp_recover_work, 0);
		}
	}

	/*schedule_delayed_work(&wd0_detect_work, msecs_to_jiffies(CCDETECT_DELAY_MS));*/

}

void oplus_wd0_get_status_work(struct work_struct *work)
{
	/* should check whether the weak function tcpci_get_wd0_status() is implemented */
	if (pinfo != NULL && pinfo->wd0_detect != tcpci_get_wd0_status()) {
		pinfo->wd0_detect = tcpci_get_wd0_status();
		pr_err("%s wd0 = %d\n", __func__, pinfo->wd0_detect);

		if (pinfo->wd0_detect) {
			oplus_ccdetect_enable();
		} else {
			if (oplus_get_otg_switch_status() == false)
				oplus_ccdetect_disable();
		}

		if (wd0_detect_work.work.func) {
			schedule_delayed_work(&wd0_detect_work, msecs_to_jiffies(CCDETECT_DELAY_MS));
		}

		if (g_oplus_chip) {
			power_supply_changed(g_oplus_chip->usb_psy);
			if (pinfo->wd0_detect == 0) {
				oplus_usbtemp_recover_cc_open();
			}
		}
	}
}

void oplus_usbtemp_recover_work(struct work_struct *work)
{
	oplus_usbtemp_recover_func(g_oplus_chip);
}

void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}
	chip->chgic_mtk.oplus_info->ccdetect_irq = gpio_to_irq(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	printk(KERN_ERR "[OPLUS_CHG][%s]: chip->chgic_mtk.oplus_info->ccdetect_gpio[%d]!\n", __func__, chip->chgic_mtk.oplus_info->ccdetect_gpio);

}

irqreturn_t oplus_ccdetect_change_handler(int irq, void *data)
{
	//struct oplus_chg_chip *chip = data;

	cancel_delayed_work_sync(&ccdetect_work);
	//smblib_dbg(chg, PR_INTERRUPT, "Scheduling ccdetect work\n");
    printk(KERN_ERR "[OPLUS_CHG][%s]: Scheduling ccdetect work!\n", __func__);
	schedule_delayed_work(&ccdetect_work,
			msecs_to_jiffies(CCDETECT_DELAY_MS));
	return IRQ_HANDLED;
}

/**************************************************************
 * bit[0]=0: NO standard typec device/cable connected(ccdetect gpio in high level)
 * bit[0]=1: standard typec device/cable connected(ccdetect gpio in low level)
 * bit[1]=0: NO OTG typec device/cable connected
 * bit[1]=1: OTG typec device/cable connected
 **************************************************************/
#define DISCONNECT						0
#define STANDARD_TYPEC_DEV_CONNECT	BIT(0)
#define OTG_DEV_CONNECT				BIT(1)

bool oplus_get_otg_online_status_default(void)
{
	if (!g_oplus_chip || !pinfo || !pinfo->tcpc) {
		chg_err("fail to init oplus_chip\n");
		return false;
	}

	if (tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC)
		g_oplus_chip->otg_online = true;
	else
		g_oplus_chip->otg_online = false;
	return g_oplus_chip->otg_online;
}

int oplus_get_otg_online_status(void)
{
	int online = 0;
	int level = 0;
	int typec_otg = 0;
	static int pre_level = 1;
	static int pre_typec_otg = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip || !pinfo || !pinfo->tcpc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: g_oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(chip) == true) {
		level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
		if (level != gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio)) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(5000, 5100);
			level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
		}
	} else if (oplus_ccdetect_check_is_wd0(chip) == true) {
		level = !oplus_chg_get_wd0_status();
	} else {
		return oplus_get_otg_online_status_default();
	}
	online = (level == 1) ? DISCONNECT : STANDARD_TYPEC_DEV_CONNECT;

	if (tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC) {
		typec_otg = 1;
	} else {
		typec_otg = 0;
	}
	online = online | ((typec_otg == 1) ? OTG_DEV_CONNECT : DISCONNECT);

	if ((pre_level ^ level) || (pre_typec_otg ^ typec_otg)) {
		pre_level = level;
		pre_typec_otg = typec_otg;
		printk(KERN_ERR "[OPLUS_CHG][%s]: gpio[%s], c-otg[%d], otg_online[%d]\n",
				__func__, level ? "H" : "L", typec_otg, online);
	}

	chip->otg_online = typec_otg;
	return online;
}


bool oplus_get_otg_switch_status(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	return chip->otg_switch;
}

void oplus_set_otg_switch_status(bool value)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		if(oplus_ccdetect_check_is_gpio(g_oplus_chip) == true) {
			if(gpio_get_value(g_oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio) == 0) {
				printk(KERN_ERR "[OPLUS_CHG][oplus_set_otg_switch_status]: gpio[L], should set, return\n");
				return;
			}
		}

		if(oplus_ccdetect_check_is_wd0(g_oplus_chip) == true) {
			if(pinfo->wd0_detect) {
				printk(KERN_ERR "[OPLUS_CHG][oplus_set_otg_switch_status]: wd0 is 1, should set, return\n");
				return;
			}
		}

		printk(KERN_ERR "[OPLUS_CHG][%s]: otg switch[%d]\n", __func__, value);
#ifndef OPLUS_FEATURE_CHG_BASIC
		tcpm_typec_change_role(pinfo->tcpc, value ? TYPEC_ROLE_TRY_SNK : TYPEC_ROLE_SNK);
#else
		tcpm_typec_change_role_postpone(pinfo->tcpc, value ? TYPEC_ROLE_TRY_SNK : TYPEC_ROLE_SNK, true);
#endif
	}
}
EXPORT_SYMBOL(oplus_set_otg_switch_status);

int oplus_get_typec_cc_orientation(void)
{
	int val = 0;

	if (pinfo != NULL && pinfo->tcpc != NULL) {
		if (tcpm_inquire_typec_attach_state(pinfo->tcpc) != TYPEC_UNATTACHED) {
			val = (int)tcpm_inquire_cc_polarity(pinfo->tcpc) + 1;
		} else {
			val = 0;
		}
		if (val != 0)
			printk(KERN_ERR "[OPLUS_CHG][%s]: cc[%d]\n", __func__, val);
	} else {
		val = 0;
	}

	return val;
}

void oplus_ccdetect_irq_register(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	ret = devm_request_threaded_irq(chip->dev,  chip->chgic_mtk.oplus_info->ccdetect_irq,
			NULL, oplus_ccdetect_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "ccdetect-change", chip);
	if (ret < 0) {
		chg_err("Unable to request ccdetect-change irq: %d\n", ret);
	}
	printk(KERN_ERR "%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(chip->chgic_mtk.oplus_info->ccdetect_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: ccdetect_irq failed %d\n", ret);
	}
}

void oplus_ccdetect_enable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ccdetect_check_is_gpio(chip) != true
			&& oplus_ccdetect_check_is_wd0(chip) != true)
		return;

	/* set DRP mode */
	if (pinfo != NULL && pinfo->tcpc != NULL){
#ifndef OPLUS_FEATURE_CHG_BASIC
		tcpm_typec_change_role(pinfo->tcpc,TYPEC_ROLE_TRY_SNK);
#else
		tcpm_typec_change_role_postpone(pinfo->tcpc, TYPEC_ROLE_TRY_SNK, true);
#endif
		pr_err("%s: set try_snk", __func__);
	}

}

void oplus_ccdetect_disable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ccdetect_check_is_gpio(chip) != true
			&& oplus_ccdetect_check_is_wd0(chip) != true)
		return;

	/* set SINK mode */
	if (pinfo != NULL && pinfo->tcpc != NULL){
#ifndef OPLUS_FEATURE_CHG_BASIC
		tcpm_typec_change_role(pinfo->tcpc,TYPEC_ROLE_SNK);
#else
		tcpm_typec_change_role_postpone(pinfo->tcpc,TYPEC_ROLE_SNK, true);
#endif
		pr_err("%s: set sink", __func__);
	}

}

int oplus_chg_ccdetect_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}
	chip->chgic_mtk.oplus_info->ccdetect_gpio = of_get_named_gpio(node, "qcom,ccdetect-gpio", 0);
	if (chip->chgic_mtk.oplus_info->ccdetect_gpio <= 0) {
		chg_err("Couldn't read qcom,ccdetect-gpio rc=%d, qcom,ccdetect-gpio:%d\n",
				rc, chip->chgic_mtk.oplus_info->ccdetect_gpio);
	} else {
		if (oplus_ccdetect_support_check() == true) {
			rc = gpio_request(chip->chgic_mtk.oplus_info->ccdetect_gpio, "ccdetect-gpio");
			if (rc) {
				chg_err("unable to request ccdetect_gpio:%d\n",
						chip->chgic_mtk.oplus_info->ccdetect_gpio);
			} else {
				rc = oplus_ccdetect_gpio_init(chip);
				if (rc){
					chg_err("unable to init ccdetect_gpio:%d\n",
							chip->chgic_mtk.oplus_info->ccdetect_gpio);
				}else{
					oplus_ccdetect_irq_init(chip);
					}
			}
		}
		chg_err("ccdetect-gpio:%d\n", chip->chgic_mtk.oplus_info->ccdetect_gpio);
	}

	return rc;
}

static bool oplus_usbtemp_check_is_platpmic(struct oplus_chg_chip *chip)
{
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		printk(KERN_ERR "device tree info missing\n", __func__);
		return false;
	}

	if (of_property_read_bool(node, "qcom,usbtemp_dischg_by_pmic"))
		return true;

	return false;
}

static bool oplus_usbtemp_check_is_support(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_usbtemp_check_is_gpio(chip) == true)
		return true;

	if (oplus_usbtemp_check_is_platpmic(chip) == true)
		return true;

	chg_err("not support, return false\n");

	return false;
}

static int oplus_dischg_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_enable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_enable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
		chg_err("get dischg_enable fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_disable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_disable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable)) {
		chg_err("get dischg_disable fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);

	return 0;
}

#define USBTEMP_DEFAULT_VOLT_VALUE_MV 950
void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip)
{
	int rc, usbtemp_volt = 0;
	static int usbtemp_volt_l_pre = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	static int usbtemp_volt_r_pre = USBTEMP_DEFAULT_VOLT_VALUE_MV;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: smb5_chg not ready!\n", __func__);
		return ;
	}

	if (IS_ERR_OR_NULL(pinfo->usb_temp_v_l_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: pinfo->usb_temp_v_l_chan  is  NULL !\n", __func__);
		chip->usbtemp_volt_l = usbtemp_volt_l_pre;
		goto usbtemp_next;
	}

	rc = iio_read_channel_processed(pinfo->usb_temp_v_l_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		chip->usbtemp_volt_l = usbtemp_volt_l_pre;
		goto usbtemp_next;
	}

	chip->usbtemp_volt_l = usbtemp_volt;
	usbtemp_volt_l_pre = chip->usbtemp_volt_l;
usbtemp_next:
	usbtemp_volt = 0;
	if (IS_ERR_OR_NULL(pinfo->usb_temp_v_r_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: pinfo->usb_temp_v_r_chan  is  NULL !\n", __func__);
		chip->usbtemp_volt_r = usbtemp_volt_r_pre;
		return;
	}

	rc = iio_read_channel_processed(pinfo->usb_temp_v_r_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		chip->usbtemp_volt_r = usbtemp_volt_r_pre;
		return;
	}

	chip->usbtemp_volt_r = usbtemp_volt;
	usbtemp_volt_r_pre = chip->usbtemp_volt_r;

	/*chg_err("usbtemp_volt_l:%d, usbtemp_volt_r:%d\n",chip->usbtemp_volt_l, chip->usbtemp_volt_r);*/
	return;
}


static bool oplus_chg_get_vbus_status(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (chip->wireless_support && oplus_chg_is_wls_present())
                return false;

	return chip->charger_exist;
}

static void oplus_usbtemp_thread_init(void)
{
	if (g_oplus_chip->support_usbtemp_protect_v2)
		oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_common_new_method, g_oplus_chip, "usbtemp_kthread");
	else
		oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_common, g_oplus_chip, "usbtemp_kthread");
	if (IS_ERR(oplus_usbtemp_kthread)) {
		chg_err("failed to cread oplus_usbtemp_kthread\n");
	}
}

void oplus_wake_up_usbtemp_thread(void)
{
	if (oplus_usbtemp_check_is_support() == true) {
		g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();
		if (g_oplus_chip->usbtemp_check) {
			if (g_oplus_chip->support_usbtemp_protect_v2)
				wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq_new_method);
			else
				wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq);
		}
	}
}

static int oplus_chg_usbtemp_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_gpio = of_get_named_gpio(node, "qcom,dischg-gpio", 0);
	if (chip->normalchg_gpio.dischg_gpio <= 0) {
		chg_err("Couldn't read qcom,dischg-gpio rc=%d, qcom,dischg-gpio:%d\n",
				rc, chip->normalchg_gpio.dischg_gpio);
	} else {
		if (oplus_usbtemp_check_is_support() == true) {
			rc = gpio_request(chip->normalchg_gpio.dischg_gpio, "dischg-gpio");
			if (rc) {
				chg_err("unable to request dischg-gpio:%d\n",
						chip->normalchg_gpio.dischg_gpio);
			} else {
				rc = oplus_dischg_gpio_init(chip);
				if (rc)
					chg_err("unable to init dischg-gpio:%d\n",
							chip->normalchg_gpio.dischg_gpio);
			}
		}
		chg_err("dischg-gpio:%d\n", chip->normalchg_gpio.dischg_gpio);
	}

	return rc;
}

static bool oplus_mtk_hv_flashled_check_is_gpio()
{
	if (gpio_is_valid(mtkhv_flashled_pinctrl.chgvin_gpio) && gpio_is_valid(mtkhv_flashled_pinctrl.pmic_chgfunc_gpio)) {
		return true;
	} else {
		chg_err("[%s] fail\n", __func__);
		return false;
	}
}
static int oplus_mtk_hv_flashled_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("[%s] oplus_chip or device tree info. missing\n", __func__);
		return -EINVAL;
	}

	mtkhv_flashled_pinctrl.hv_flashled_support = of_property_read_bool(node, "qcom,hv_flashled_support");
	if (mtkhv_flashled_pinctrl.hv_flashled_support == false) {
		chg_err("[%s] hv_flashled_support not support\n", __func__);
		return -EINVAL;
	}
	mutex_init(&mtkhv_flashled_pinctrl.chgvin_mutex);

	mtkhv_flashled_pinctrl.chgvin_gpio = of_get_named_gpio(node, "qcom,chgvin", 0);
	mtkhv_flashled_pinctrl.pmic_chgfunc_gpio = of_get_named_gpio(node, "qcom,pmic_chgfunc", 0);
	if (mtkhv_flashled_pinctrl.chgvin_gpio <= 0 || mtkhv_flashled_pinctrl.pmic_chgfunc_gpio <= 0) {
		chg_err("read dts fail %d %d\n", mtkhv_flashled_pinctrl.chgvin_gpio, mtkhv_flashled_pinctrl.pmic_chgfunc_gpio);
	} else {
		if (oplus_mtk_hv_flashled_check_is_gpio() == true) {
			rc = gpio_request(mtkhv_flashled_pinctrl.chgvin_gpio, "chgvin");
			if (rc ) {
				chg_err("unable to request chgvin:%d\n",
						mtkhv_flashled_pinctrl.chgvin_gpio);
			} else {
				mtkhv_flashled_pinctrl.pinctrl= devm_pinctrl_get(chip->dev);
				//chgvin
				mtkhv_flashled_pinctrl.chgvin_enable =
					pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "chgvin_enable");
				if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.chgvin_enable)) {
					chg_err("get chgvin_enable fail\n");
					return -EINVAL;
				}

				mtkhv_flashled_pinctrl.chgvin_disable =
					pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "chgvin_disable");
				if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.chgvin_disable)) {
					chg_err("get chgvin_disable fail\n");
					return -EINVAL;
				}
				pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);


				rc = gpio_request(mtkhv_flashled_pinctrl.pmic_chgfunc_gpio, "pmic_chgfunc");
				if (rc ) {
					chg_err("unable to request pmic_chgfunc:%d\n",
							mtkhv_flashled_pinctrl.pmic_chgfunc_gpio);
				} else {
					//pmic_chgfunc
					mtkhv_flashled_pinctrl.pmic_chgfunc_enable =
						pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "pmic_chgfunc_enable");
					if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.pmic_chgfunc_enable)) {
						chg_err("get pmic_chgfunc_enable fail\n");
						return -EINVAL;
					}

					mtkhv_flashled_pinctrl.pmic_chgfunc_disable =
						pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "pmic_chgfunc_disable");
					if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.pmic_chgfunc_disable)) {
						chg_err("get pmic_chgfunc_disable fail\n");
						return -EINVAL;
					}
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.pmic_chgfunc_disable);
				}

			}
		}
		//chg_err("mtk_hv_flash_led:%d\n", chip->normalchg_gpio.shortc_gpio);
	}

	chg_err("mtk_hv_flash_led:%d\n", rc);
	return rc;

}

static int oplus_wrx_otg_en_gpio_init(struct oplus_chg_chip *chip)
{
	struct mtk_charger *info = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus chip not ready!\n", __func__);
		return -EINVAL;
	}

	info = chip->chgic_mtk.oplus_info;

	info->wrx_otg_en_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(info->wrx_otg_en_pinctrl)) {
		chg_err("get wrx_otg_en_pinctrl fail\n");
		return -EINVAL;
	}

	info->wrx_otg_en_active =
		pinctrl_lookup_state(info->wrx_otg_en_pinctrl, "wrx_otg_en_active");
	if (IS_ERR_OR_NULL(info->wrx_otg_en_active)) {
		chg_err("get wrx_otg_en_active fail\n");
		return -EINVAL;
	}

	info->wrx_otg_en_sleep =
		pinctrl_lookup_state(info->wrx_otg_en_pinctrl, "wrx_otg_en_sleep");
	if (IS_ERR_OR_NULL(info->wrx_otg_en_sleep)) {
		chg_err("get wrx_otg_en_sleep fail\n");
		return -EINVAL;
	}

	gpio_direction_output(info->wrx_otg_en_gpio, 0);
	pinctrl_select_state(info->wrx_otg_en_pinctrl, info->wrx_otg_en_sleep);

	return 0;
}

void oplus_set_wrx_otg_en_val(int value)
{
	struct mtk_charger *info = pinfo;

	if (!info) {
		chg_err("mtk_charger info is NULL!\n");
		return;
	}

	if (info->wrx_otg_en_gpio <= 0) {
		chg_err("wrx_otg_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(info->wrx_otg_en_pinctrl)
			|| IS_ERR_OR_NULL(info->wrx_otg_en_active)
			|| IS_ERR_OR_NULL(info->wrx_otg_en_sleep)) {
		chg_err("wrx_otg_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(info->wrx_otg_en_gpio , 1);
		pinctrl_select_state(info->wrx_otg_en_pinctrl, info->wrx_otg_en_active);
	} else {
		gpio_direction_output(info->wrx_otg_en_gpio, 0);
		pinctrl_select_state(info->wrx_otg_en_pinctrl, info->wrx_otg_en_sleep);
	}

	chg_err("set value:%d, gpio_val:%d\n", value, gpio_get_value(info->wrx_otg_en_gpio));
}
EXPORT_SYMBOL(oplus_set_wrx_otg_en_val);

int oplus_boost_en_gpio_init(struct oplus_chg_chip *chip)
{
	struct mtk_charger *info = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus chip not ready!\n", __func__);
		return -EINVAL;
	}

	info = chip->chgic_mtk.oplus_info;

	info->boost_en_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(info->boost_en_pinctrl)) {
		chg_err("get boost_en_pinctrl fail\n");
		return -EINVAL;
	}

	info->boost_en_active = pinctrl_lookup_state(info->boost_en_pinctrl, "boost_en_active");
	if (IS_ERR_OR_NULL(info->boost_en_active)) {
		chg_err("get boost_en_active fail\n");
		return -EINVAL;
	}

	info->boost_en_sleep = pinctrl_lookup_state(info->boost_en_pinctrl, "boost_en_sleep");
	if (IS_ERR_OR_NULL(info->boost_en_sleep)) {
		chg_err("get boost_en_sleep fail\n");
		return -EINVAL;
	}

	gpio_direction_output(info->boost_en_gpio, 0);
	pinctrl_select_state(info->boost_en_pinctrl,  info->boost_en_sleep);
	return 0;
}

void oplus_set_boost_en_val(int value)
{
	struct mtk_charger *info = pinfo;

	if (!info) {
		chg_err("mtk_charger info is NULL!\n");
		return;
	}

	if (info->boost_en_gpio <= 0) {
		chg_err("boost_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(info->boost_en_pinctrl)
			|| IS_ERR_OR_NULL(info->boost_en_active)
			|| IS_ERR_OR_NULL(info->boost_en_sleep)) {
		chg_err("boost_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(info->boost_en_gpio , 1);
		pinctrl_select_state(info->boost_en_pinctrl, info->boost_en_active);
	} else {
		gpio_direction_output(info->boost_en_gpio, 0);
		pinctrl_select_state(info->boost_en_pinctrl, info->boost_en_sleep);
	}

	chg_err("set value:%d, gpio_val:%d\n", value, gpio_get_value(info->boost_en_gpio));
}
EXPORT_SYMBOL(oplus_set_boost_en_val);

int oplus_ext1_otg_en_gpio_init(struct oplus_chg_chip *chip)
{
	struct mtk_charger *info = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus chip not ready!\n", __func__);
		return -EINVAL;
	}

	info = chip->chgic_mtk.oplus_info;

	info->ext1_otg_en_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(info->ext1_otg_en_pinctrl)) {
		chg_err("get ext1_otg_en_pinctrl fail\n");
		return -EINVAL;
	}

	info->ext1_otg_en_active = pinctrl_lookup_state(info->ext1_otg_en_pinctrl, "ext1_otg_en_active");
	if (IS_ERR_OR_NULL(info->ext1_otg_en_active)) {
		chg_err("get ext1_otg_en_active fail\n");
		return -EINVAL;
	}

	info->ext1_otg_en_sleep = pinctrl_lookup_state(info->ext1_otg_en_pinctrl, "ext1_otg_en_sleep");
	if (IS_ERR_OR_NULL(info->ext1_otg_en_sleep)) {
		chg_err("get ext1_otg_en_sleep fail\n");
		return -EINVAL;
	}

	gpio_direction_output(info->ext1_otg_en_gpio, 0);
	pinctrl_select_state(info->ext1_otg_en_pinctrl,  info->ext1_otg_en_sleep);
	return 0;
}

void oplus_set_ext1_otg_en_val(int value)
{
	struct mtk_charger *info = pinfo;

	if (!info) {
		chg_err("mtk_charger info is NULL!\n");
		return;
	}

	if (info->ext1_otg_en_gpio <= 0) {
		chg_err("ext1_otg_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(info->ext1_otg_en_pinctrl)
			|| IS_ERR_OR_NULL(info->ext1_otg_en_active)
			|| IS_ERR_OR_NULL(info->ext1_otg_en_sleep)) {
		chg_err("ext1_otg_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(info->ext1_otg_en_gpio , 1);
		pinctrl_select_state(info->ext1_otg_en_pinctrl, info->ext1_otg_en_active);
	} else {
		gpio_direction_output(info->ext1_otg_en_gpio, 0);
		pinctrl_select_state(info->ext1_otg_en_pinctrl, info->ext1_otg_en_sleep);
	}

	chg_err("set value:%d, gpio_val:%d\n", value, gpio_get_value(info->ext1_otg_en_gpio));
}
EXPORT_SYMBOL(oplus_set_ext1_otg_en_val);

int oplus_ext2_otg_en_gpio_init(struct oplus_chg_chip *chip)
{
	struct mtk_charger *info = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus chip not ready!\n", __func__);
		return -EINVAL;
	}

	info = chip->chgic_mtk.oplus_info;

	info->ext2_otg_en_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(info->ext2_otg_en_pinctrl)) {
		chg_err("get ext2_otg_en_pinctrl fail\n");
		return -EINVAL;
	}

	info->ext2_otg_en_active = pinctrl_lookup_state(info->ext2_otg_en_pinctrl, "ext2_otg_en_active");
	if (IS_ERR_OR_NULL(info->ext2_otg_en_active)) {
		chg_err("get ext2_otg_en_active fail\n");
		return -EINVAL;
	}

	info->ext2_otg_en_sleep = pinctrl_lookup_state(info->ext2_otg_en_pinctrl, "ext2_otg_en_sleep");
	if (IS_ERR_OR_NULL(info->ext2_otg_en_sleep)) {
		chg_err("get ext2_otg_en_sleep fail\n");
		return -EINVAL;
	}

	gpio_direction_output(info->ext2_otg_en_gpio, 0);
	pinctrl_select_state(info->ext2_otg_en_pinctrl,  info->ext2_otg_en_sleep);
	return 0;
}

void oplus_set_ext2_otg_en_val(int value)
{
	struct mtk_charger *info = pinfo;

	if (!info) {
		chg_err("mtk_charger info is NULL!\n");
		return;
	}

	if (info->ext2_otg_en_gpio <= 0) {
		chg_err("ext2_otg_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(info->ext2_otg_en_pinctrl)
			|| IS_ERR_OR_NULL(info->ext2_otg_en_active)
			|| IS_ERR_OR_NULL(info->ext2_otg_en_sleep)) {
		chg_err("ext2_otg_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(info->ext2_otg_en_gpio , 1);
		pinctrl_select_state(info->ext2_otg_en_pinctrl, info->ext2_otg_en_active);
	} else {
		gpio_direction_output(info->ext2_otg_en_gpio, 0);
		pinctrl_select_state(info->ext2_otg_en_pinctrl, info->ext2_otg_en_sleep);
	}

	chg_err("set value:%d, gpio_val:%d\n", value, gpio_get_value(info->ext2_otg_en_gpio));
}
EXPORT_SYMBOL(oplus_set_ext2_otg_en_val);

static int oplus_mtk_parse_wls_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;
	struct mtk_charger *info = NULL;

	if (chip) {
		info = chip->chgic_mtk.oplus_info;
		node = chip->dev->of_node;
	}

        if (!chip || !node || !info) {
                chg_err("[%s] oplus_chip or device tree info. missing\n", __func__);
                return -EINVAL;
        }

	info->wrx_otg_en_gpio = of_get_named_gpio(node, "qcom,wrx_otg_en-gpio", 0);
	if (info->wrx_otg_en_gpio <= 0) {
		chg_err("Couldn't read qcom,wrx_otg_en-gpio, rc = %d\n", rc);
	} else {
		if (gpio_is_valid(info->wrx_otg_en_gpio)) {
			rc = gpio_request(info->wrx_otg_en_gpio, "wrx_otg_en-gpio");
			if (rc) {
				chg_err("unable to wrx_otg_en-gpio:%d\n", info->wrx_otg_en_gpio);
			} else {
				rc = oplus_wrx_otg_en_gpio_init(chip);
				if (rc)
					chg_err("unable to init wrx_otg_en-gpio:%d\n", info->wrx_otg_en_gpio);
				else
					chg_err("init wrx_otg_en_gpio level[%d]\n", gpio_get_value(info->wrx_otg_en_gpio));
			}
		}
		chg_err("wrx_otg_en-gpio:%d\n", info->wrx_otg_en_gpio);
	}

	info->boost_en_gpio = of_get_named_gpio(node, "qcom,boost_en-gpio", 0);
	if (info->boost_en_gpio <= 0) {
		chg_err("Couldn't read qcom,boost_en-gpio rc=%d, qcom,boost_en-gpio:%d\n",
				rc, info->boost_en_gpio);
	} else {
		if (gpio_is_valid(info->boost_en_gpio)) {
			rc = gpio_request(info->boost_en_gpio, "boost_en-gpio");
			if (rc) {
				chg_err("unable to request boost_en_gpio:%d\n",
					info->boost_en_gpio);
			} else {
				rc = oplus_boost_en_gpio_init(chip);
				if(rc) {
					chg_err("unable to init boost_en_gpio:%d\n",
						info->boost_en_gpio);
				} else {
					chg_err("init boost_en_gpio level[%d]\n",
						gpio_get_value(info->boost_en_gpio));
				}
			}
			chg_err("boost_en-gpio:%d\n", info->boost_en_gpio);
		}
	}

	info->ext1_otg_en_gpio = of_get_named_gpio(node, "qcom,ext1_otg_en-gpio", 0);
	if (info->ext1_otg_en_gpio <= 0) {
		chg_err("Couldn't read qcom,ext1_otg_en-gpio rc=%d, qcom,ext1_otg_en-gpio:%d\n",
				rc, info->ext1_otg_en_gpio);
	} else {
		if (gpio_is_valid(info->ext1_otg_en_gpio)) {
			rc = gpio_request(info->ext1_otg_en_gpio, "ext1_otg_en-gpio");
			if (rc) {
				chg_err("unable to request ext1_otg_en_gpio:%d\n",
					info->ext1_otg_en_gpio);
			} else {
				rc = oplus_ext1_otg_en_gpio_init(chip);
				if(rc) {
					chg_err("unable to init ext1_otg_en_gpio:%d\n",
						info->ext1_otg_en_gpio);
				} else {
					chg_err("init ext1_otg_en_gpio level[%d]\n",
						gpio_get_value(info->ext1_otg_en_gpio));
				}
			}
			chg_err("ext1_otg_en-gpio:%d\n", info->ext1_otg_en_gpio);
		}
	}

	info->ext2_otg_en_gpio = of_get_named_gpio(node, "qcom,ext2_otg_en-gpio", 0);
	if (info->ext2_otg_en_gpio <= 0) {
		chg_err("Couldn't read qcom,ext2_otg_en-gpio rc=%d, qcom,ext2_otg_en-gpio:%d\n",
				rc, info->ext2_otg_en_gpio);
	} else {
		if (gpio_is_valid(info->ext2_otg_en_gpio)) {
			rc = gpio_request(info->ext2_otg_en_gpio, "ext2_otg_en-gpio");
			if (rc) {
				chg_err("unable to request ext2_otg_en_gpio:%d\n",
					info->ext2_otg_en_gpio);
			} else {
				rc = oplus_ext2_otg_en_gpio_init(chip);
				if(rc) {
					chg_err("unable to init ext2_otg_en_gpio:%d\n",
						info->ext2_otg_en_gpio);
				} else {
					chg_err("init ext2_otg_en_gpio level[%d]\n",
						gpio_get_value(info->ext2_otg_en_gpio));
				}
			}
			chg_err("ext2_otg_en-gpio:%d\n", info->ext2_otg_en_gpio);
		}
	}

	return rc;
}

static int oplus_chg_parse_custom_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;
        struct mtk_charger *info = NULL;
	
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	info = chip->chgic_mtk.oplus_info;

	rc = oplus_chg_chargerid_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_chargerid_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_shipmode_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_shipmode_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_shortc_hw_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_shortc_hw_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_usbtemp_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_usbtemp_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_ccdetect_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_ccdetect_parse_dt fail!\n", __func__);
	}
	
	rc = oplus_mtk_hv_flashled_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_mtk_hv_flashled_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_mtk_parse_wls_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_mtk_parse_wls_dt fail!\n", __func__);
		return -EINVAL;
	}

	node = chip->dev->of_node;

	return rc;
}

static int mt_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;

	rc = oplus_ac_get_property(psy, psp, val);
	return rc;
}

#define CHG_OPS_LEN 64
static bool is_ext_mp2650_chg_ops(void)
{
	return (strncmp(oplus_chg_ops_name_get(), "ext-mp2650", CHG_OPS_LEN) == 0);
}

static int mt_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	return oplus_usb_get_property(psy, psp, val);
}

static int battery_prop_is_writeable(struct power_supply *psy,
	enum power_supply_property psp)
{
	return oplus_battery_property_is_writeable(psy, psp);
}

static int battery_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	return oplus_battery_set_property(psy, psp, val);
}

static int battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;

	if (is_vooc_project()) {
		switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
			if (g_oplus_chip && (g_oplus_chip->ui_soc == 0)) {
				val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
					chg_err("bat pro POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, should shutdown!!!\n");
				}
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			if (g_oplus_chip) {
				val->intval = g_oplus_chip->batt_fcc * 1000;
			}
			break;
		case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
			val->intval = 0;
			break;
		default:
			rc = oplus_battery_get_property(psy, psp, val);
			break;
		}
	}

	return 0;
}

static enum power_supply_property mt_ac_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property mt_usb_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static enum power_supply_property battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static void oplus_chg_wls_status_keep_clean_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_charger *info=
		container_of(dwork, struct mtk_charger, status_keep_clean_work);
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip == NULL || info == NULL) {
		chg_err("oplus_chg_chip or info is NULL\n");
	} else {
		if (chip->wls_status_keep == WLS_SK_BY_HAL) {
			chip->wls_status_keep = WLS_SK_WAIT_TIMEOUT;
			schedule_delayed_work(&info->status_keep_clean_work, msecs_to_jiffies(5000));
			return;
		}

		chip->wls_status_keep = WLS_SK_NULL;
		power_supply_changed(chip->batt_psy);
	}
	if (info->status_wake_lock_on) {
		chg_err("release status_wake_lock\n");
		__pm_relax(info->status_wake_lock);
		info->status_wake_lock_on = false;
	}
}

#define KEEP_CLEAN_INTERVAL	2000
static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct mtk_charger *info = pinfo;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int rc = 0;
	static bool pre_wls_online;

	if (!chip || !is_wls_ocm_available(chip) || !info) {
		pr_err("g_oplus_chip or pinfo or wireless mod not found\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		pval->intval = oplus_chg_is_wls_present();
		if (oplus_get_wired_chg_present() == true) {
			pval->intval = 0;
			pre_wls_online = pval->intval;
			chip->wls_status_keep = WLS_SK_NULL;
			break;
		}
		if (chip->wls_status_keep != WLS_SK_NULL) {
			pval->intval = 1;
		} else {
			if (pre_wls_online && pval->intval == 0) {
				if (!info->status_wake_lock_on) {
					chg_err("acquire status_wake_lock\n");
					__pm_stay_awake(info->status_wake_lock);
					info->status_wake_lock_on = true;
				}
				pre_wls_online = pval->intval;
				chip->wls_status_keep = WLS_SK_BY_KERNEL;
				pval->intval = 1;
				schedule_delayed_work(&info->status_keep_clean_work, msecs_to_jiffies(KEEP_CLEAN_INTERVAL));
			} else {
				pre_wls_online = pval->intval;
				if (info->status_wake_lock_on) {
					cancel_delayed_work_sync(&info->status_keep_clean_work);
					schedule_delayed_work(&info->status_keep_clean_work, 0);
				}
			}
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_VOLTAGE_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_VOLTAGE_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_CURRENT_NOW,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_CURRENT_MAX,
			(union oplus_chg_mod_propval *)pval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = oplus_chg_mod_get_property(chip->wls_ocm,
			OPLUS_CHG_PROP_PRESENT,
			(union oplus_chg_mod_propval *)pval);
		break;
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}

	return 0;
}

static int wls_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	return 0;
}

static int wls_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return 0;
}

static enum power_supply_property wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_PRESENT,
};

static int oplus_power_supply_init(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct oplus_chg_chip *mt_chg = NULL;

	if (chip == NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}
	mt_chg = chip;

	mt_chg->ac_psd.name = "ac";
	mt_chg->ac_psd.type = POWER_SUPPLY_TYPE_MAINS;
	mt_chg->ac_psd.properties = mt_ac_properties;
	mt_chg->ac_psd.num_properties = ARRAY_SIZE(mt_ac_properties);
	mt_chg->ac_psd.get_property = mt_ac_get_property;
	mt_chg->ac_cfg.drv_data = mt_chg;

	mt_chg->usb_psd.name = "usb";
	mt_chg->usb_psd.type = POWER_SUPPLY_TYPE_USB;
	mt_chg->usb_psd.properties = mt_usb_properties;
	mt_chg->usb_psd.num_properties = ARRAY_SIZE(mt_usb_properties);
	mt_chg->usb_psd.get_property = mt_usb_get_property;
	mt_chg->usb_cfg.drv_data = mt_chg;
    
	mt_chg->battery_psd.name = "battery";
	mt_chg->battery_psd.type = POWER_SUPPLY_TYPE_BATTERY;
	mt_chg->battery_psd.properties = battery_properties;
	mt_chg->battery_psd.num_properties = ARRAY_SIZE(battery_properties);
	mt_chg->battery_psd.get_property = battery_get_property;
	mt_chg->battery_psd.set_property = battery_set_property;
	mt_chg->battery_psd.property_is_writeable = battery_prop_is_writeable;

	mt_chg->wls_psd.name = "wireless",
	mt_chg->wls_psd.type = POWER_SUPPLY_TYPE_WIRELESS,
	mt_chg->wls_psd.properties = wls_props,
	mt_chg->wls_psd.num_properties = ARRAY_SIZE(wls_props),
	mt_chg->wls_psd.get_property = wls_psy_get_prop,
	mt_chg->wls_psd.set_property = wls_psy_set_prop,
	mt_chg->wls_psd.property_is_writeable = wls_psy_prop_is_writeable,

	mt_chg->ac_psy = power_supply_register(mt_chg->dev, &mt_chg->ac_psd,
			&mt_chg->ac_cfg);
	if (IS_ERR(mt_chg->ac_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply ac: %ld\n",
			PTR_ERR(mt_chg->ac_psy));
		ret = PTR_ERR(mt_chg->ac_psy);
		goto err_ac_psy;
	}

	mt_chg->usb_psy = power_supply_register(mt_chg->dev, &mt_chg->usb_psd,
			&mt_chg->usb_cfg);
	if (IS_ERR(mt_chg->usb_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply usb: %ld\n",
			PTR_ERR(mt_chg->usb_psy));
		ret = PTR_ERR(mt_chg->usb_psy);
		goto err_usb_psy;
	}

	mt_chg->batt_psy = power_supply_register(mt_chg->dev, &mt_chg->battery_psd,
			NULL);
	if (IS_ERR(mt_chg->batt_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply battery: %ld\n",
			PTR_ERR(mt_chg->batt_psy));
		ret = PTR_ERR(mt_chg->batt_psy);
		goto err_battery_psy;
	}

	mt_chg->wls_psy = power_supply_register(mt_chg->dev, &mt_chg->wls_psd,
                        NULL);
        if (IS_ERR(mt_chg->wls_psy)) {
                dev_err(mt_chg->dev, "Failed to register power supply wirless: %ld\n",
                        PTR_ERR(mt_chg->wls_psy));
                ret = PTR_ERR(mt_chg->wls_psy);
                goto err_wireless_psy;
        }

	chg_err("%s OK\n", __func__);
	return 0;

err_wireless_psy:
	power_supply_unregister(mt_chg->wls_psy);
err_battery_psy:
	power_supply_unregister(mt_chg->usb_psy);
err_usb_psy:
	power_supply_unregister(mt_chg->ac_psy);
err_ac_psy:

	return ret;
}

int oplus_get_fast_chg_type(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	union oplus_chg_mod_propval pval;
	int fast_chg_type = 0;
	int rc;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return fast_chg_type;
	}

	fast_chg_type = oplus_vooc_get_fast_chg_type();
	if (fast_chg_type == 0) {
		fast_chg_type = oplus_chg_get_charger_subtype();
	}

	if (is_wls_ocm_available(chip) && (fast_chg_type == 0) && !oplus_get_wired_chg_present()) {
		rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &pval);
		if (rc == 0) {
			if ((pval.intval == OPLUS_CHG_WLS_VOOC)
				|| (pval.intval == OPLUS_CHG_WLS_SVOOC)
				|| (pval.intval == OPLUS_CHG_WLS_PD_65W)) {
				rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_ADAPTER_TYPE, &pval);
				if (rc == 0)
					fast_chg_type = pval.intval;
				if (fast_chg_type == WLS_ADAPTER_TYPE_PD_65W)
					fast_chg_type = WLS_ADAPTER_TYPE_SVOOC;
			}
		}
	}

	return fast_chg_type;
}

static bool oplus_pd_without_usb(void)
{
	struct tcpc_device *tcpc;

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!tcpc) {
		chg_err("get type_c_port0 fail\n");
		return true;
	}

	if (!tcpm_inquire_pd_connected(tcpc))
		return true;
	return (tcpm_inquire_dpm_flags(tcpc) &
			DPM_FLAGS_PARTNER_USB_COMM) ? false : true;
}

int mt_power_supply_type_check(void)
{
	int chr_type;
	union power_supply_propval prop = {0, };
	struct power_supply *chg_psy = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	chg_psy = devm_power_supply_get_by_phandle(&pinfo->pdev->dev, "charger");
	if (IS_ERR_OR_NULL(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
	} else {
		power_supply_get_property(chg_psy, POWER_SUPPLY_PROP_ONLINE, &prop);
	}

	chr_type = get_charger_type(pinfo);

	if (chip && chip->wireless_support && !prop.intval &&
			(oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present()))
		chr_type = POWER_SUPPLY_TYPE_WIRELESS;

	if (!oplus_pd_without_usb())
		chr_type = POWER_SUPPLY_USB_TYPE_PD_SDP;

	chr_err("charger_type[%d]\n", chr_type);

	return chr_type;
}
EXPORT_SYMBOL(mt_power_supply_type_check);

int battery_meter_get_charger_voltage(void)
{
	int rc = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	union oplus_chg_mod_propval pval = {0};

	if (oplus_chg_is_wls_present()) {
		rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_VOLTAGE_NOW, &pval);
		if (rc >= 0) {
			return pval.intval;
		}
	}

	return get_vbus(pinfo);
}
EXPORT_SYMBOL(battery_meter_get_charger_voltage);

int oplus_chg_set_qc_config_forsvooc(void)
{
	int ret = -1;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("oplus_chip is null\n");
		return -1;
	}

	printk(KERN_ERR "%s: qc9v svooc [%d %d %d]", __func__, chip->limits.vbatt_pdqc_to_9v_thr, chip->limits.vbatt_pdqc_to_5v_thr, chip->batt_volt);
	if (!chip->calling_on && chip->charger_volt < CHARGER_VOLT_NORMAL
	    && (chip->limits.tbatt_pdqc_to_5v_thr < 0
		|| (chip->limits.tbatt_pdqc_to_5v_thr > 0
		    && chip->temperature <= chip->limits.tbatt_pdqc_to_5v_thr))
	    && chip->cool_down_force_5v == false
	    && (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
		printk(KERN_ERR "%s: set qc to 9V", __func__);
		mt6375_set_hvdcp_to_5v();	//Before request 9V, need to force 5V first.
		msleep(300);
		oplus_chg_suspend_charger();
		oplus_chg_config_charger_vsys_threshold(0x02);//set Vsys Skip threshold 104%
		oplus_chg_enable_burst_mode(false);
		if (oplus_chg_get_voocphy_support()) {
			oplus_voocphy_set_pdqc_config();
		}
		mt6375_set_hvdcp_to_9v();
#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
		oplus_notify_hvdcp_detect_stat();
#endif

		if (chip->pdqc_9v_voltage_adaptive && is_ext_mp2650_chg_ops())
			oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_5VTO9V);

		msleep(300);
		oplus_chg_unsuspend_charger();
		ret = 0;
	} else {
		if (chip->charger_volt > CHARGER_VOLT_HV
		    && (chip->calling_on
			|| chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr
			|| (chip->limits.tbatt_pdqc_to_5v_thr > 0
			    && chip->temperature > chip->limits.tbatt_pdqc_to_5v_thr)
			|| chip->cool_down_force_5v == true)) {
			printk(KERN_ERR "%s: set qc to 5V", __func__);
			chip->chg_ops->input_current_write(500);
			oplus_chg_suspend_charger();
			oplus_chg_config_charger_vsys_threshold(0x03);//set Vsys Skip threshold 101%
			mt6375_set_hvdcp_to_5v();
			msleep(400);
			if (chip->pdqc_9v_voltage_adaptive && is_ext_mp2650_chg_ops())
				oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_9VTO5V);

			printk(KERN_ERR "%s: charger voltage=%d", __func__, chip->charger_volt);
			oplus_chg_unsuspend_charger();
			ret = 0;
		}
		printk(KERN_ERR "%s: qc9v svooc  default[%d]", __func__, chip->batt_volt);
	}

	return ret;
}

void mt_usb_connect(void)
{
	return;
}
EXPORT_SYMBOL(mt_usb_connect);

void mt_usb_disconnect(void)
{
	return;
}
EXPORT_SYMBOL(mt_usb_disconnect);

int get_rtc_spare_oplus_fg_value(void)
{
	return 0;
}
EXPORT_SYMBOL(get_rtc_spare_oplus_fg_value);

int set_rtc_spare_oplus_fg_value(int soc)
{
        return 0;
}
EXPORT_SYMBOL(set_rtc_spare_oplus_fg_value);

bool oplus_mt_get_vbus_status(void)
{
	struct mtk_charger *info = pinfo;

	if (!info) {
		pr_err("pinfo not found\n");
		return -ENODEV;
	}

	if(g_oplus_chip->vbatt_num == 2 && !oplus_chg_is_wls_present()
			&& tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC){
		return false;
	}

	if (is_charger_exist(pinfo) || info->chrdet_state || info->wait_hard_reset_complete
		|| mt6375_int_chrdet_attach()) {
		return true;
	} else if (oplus_wpc_get_wireless_charge_start() || oplus_chg_is_wls_present()) {
		return true;
	} else {
		return false;
	}
}
EXPORT_SYMBOL(oplus_mt_get_vbus_status);

void oplus_mt_power_off(void)
{
#if 0
	struct tcpc_device *tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
#endif

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (g_oplus_chip->ac_online != true) {
#if 0
		if (tcpc_dev == NULL) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: tcpc_dev not ready!\n", __func__);
			tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
		}
		if (tcpc_dev) {
			if (!(tcpc_dev->pd_wait_hard_reset_complete)
					&& !oplus_mt_get_vbus_status()) {
				kernel_power_off();
			}
		}
#endif
		kernel_power_off();
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ac_online is true, return!\n", __func__);
	}
}
EXPORT_SYMBOL(oplus_mt_power_off);

int oplus_chg_set_qc_config(void)
{
	int ret = -1;

	struct oplus_chg_chip *chip = g_oplus_chip;

	pr_err("oplus_chg_set_qc_config\n");

	if (!chip) {
		pr_err("oplus_chip is null\n");
		return -1;
	}

	if (is_mtksvooc_project) {
		ret = oplus_chg_set_qc_config_forsvooc();
	} else {
		if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90
				&& chip->temperature <= 420 && chip->cool_down_force_5v == false) {
			printk(KERN_ERR "%s: set qc to 9V", __func__);
			mt6375_set_hvdcp_to_9v();
			ret = 0;
		} else {
			if (chip->charger_volt > 7500 &&
					(chip->calling_on || chip->camera_on || chip->soc >= 90 || chip->batt_volt >= 4450
					|| chip->temperature > 420 || chip->cool_down_force_5v == true)) {
				printk(KERN_ERR "%s: set qc to 5V", __func__);
				mt6375_set_hvdcp_to_5v();
				ret = 0;
			}
		}
	}
        return ret;
}
EXPORT_SYMBOL(oplus_chg_set_qc_config);

int oplus_chg_set_pd_config(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int ret = 0;
	int vbus_mv_t = 0;
	int ibus_ma_t = 0;
	struct tcpc_device *tcpc = NULL;

	if(chip->pd_svooc){
		printk(KERN_ERR, "%s pd_svooc support\n", __func__);
		return 0;
	}

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (tcpc == NULL) {
		printk(KERN_ERR "%s:get type_c_port0 fail\n", __func__);
		return -EINVAL;
	}

	printk(KERN_ERR "%s: pd_type %d pd9v svooc [%d %d %d]", __func__, pinfo->pd_type, chip->limits.vbatt_pdqc_to_9v_thr, chip->limits.vbatt_pdqc_to_5v_thr, chip->batt_volt);

	if (!chip->calling_on && chip->charger_volt < 6500 && chip->soc < 90
			&& chip->temperature <= 530 && chip->cool_down_force_5v == false
			&& (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
		oplus_chg_suspend_charger();
		oplus_chg_config_charger_vsys_threshold(0x02);//set Vsys Skip threshold 104%
		oplus_chg_enable_burst_mode(false);

		ret = tcpm_dpm_pd_request(tcpc, 9000, 2000, NULL);
		if (ret != TCPM_SUCCESS) {
			printk(KERN_ERR "%s: tcpm_dpm_pd_request fail\n", __func__);
			msleep(300);
			oplus_chg_unsuspend_charger();
			return -EINVAL;
		}

		ret = tcpm_inquire_pd_contract(tcpc, &vbus_mv_t, &ibus_ma_t);
		if (ret != TCPM_SUCCESS) {
			printk(KERN_ERR "%s: inquire current vbus_mv and ibus_ma fail\n", __func__);
			msleep(300);
			oplus_chg_unsuspend_charger();
			return -EINVAL;
		}

		msleep(300);

		if (chip->pdqc_9v_voltage_adaptive && is_ext_mp2650_chg_ops())
			oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_5VTO9V);

		oplus_chg_unsuspend_charger();
		printk(KERN_ERR "%s: PD request vbus_mv[%d], ibus_ma[%d]\n", __func__, vbus_mv_t, ibus_ma_t);
	} else {
		if (chip->charger_volt > 7500 &&
				(chip->calling_on || chip->soc >= 90 || chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr
				|| chip->temperature > 530 || chip->cool_down_force_5v == true)) {
			chip->chg_ops->input_current_write(500);
			oplus_chg_suspend_charger();
			oplus_chg_config_charger_vsys_threshold(0x03);//set Vsys Skip threshold 101%

			ret = tcpm_dpm_pd_request(tcpc, 5000, 2000, NULL);
			if (ret != TCPM_SUCCESS) {
				printk(KERN_ERR "%s: tcpm_dpm_pd_request fail\n", __func__);
				msleep(300);
				oplus_chg_unsuspend_charger();
				return -EINVAL;
			}

			ret = tcpm_inquire_pd_contract(tcpc, &vbus_mv_t, &ibus_ma_t);
			if (ret != TCPM_SUCCESS) {
				printk(KERN_ERR "%s: inquire current vbus_mv and ibus_ma fail\n", __func__);
				msleep(300);
				oplus_chg_unsuspend_charger();
				return -EINVAL;
			}

			msleep(300);
			if (chip->pdqc_9v_voltage_adaptive && is_ext_mp2650_chg_ops())
				oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_9VTO5V);

			oplus_chg_unsuspend_charger();
			printk(KERN_ERR "%s: PD Default vbus_mv[%d], ibus_ma[%d]\n", __func__, vbus_mv_t, ibus_ma_t);
		}
	}

	return 0;
}
EXPORT_SYMBOL(oplus_chg_set_pd_config);

void oplus_chg_stop_pps_config(void)
{
	int ret = 0;
	int vbus_set = 0;
	int ibus_set = 0;
	struct tcpc_device *tcpc = NULL;

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (tcpc == NULL) {
		printk(KERN_ERR "%s:get type_c_port0 fail\n", __func__);
		return;
	}

	oplus_chg_config_charger_vsys_threshold(0x03);//set Vsys Skip threshold 101%

	ret = tcpm_dpm_pd_request(tcpc, 5000, 2000, NULL);
	if (ret != TCPM_SUCCESS) {
		printk(KERN_ERR "%s: tcpm_dpm_pd_request fail\n", __func__);
		return;
	}

	ret = tcpm_inquire_pd_contract(tcpc, &vbus_set, &ibus_set);
	if (ret != TCPM_SUCCESS) {
		printk(KERN_ERR "%s: inquire current vbus_mv and ibus_ma fail\n", __func__);
		return;
	}

	msleep(300);
	printk(KERN_ERR "%s: set vbus_mv[%d], ibus_ma[%d]\n", __func__, vbus_set, ibus_set);
}
EXPORT_SYMBOL(oplus_chg_stop_pps_config);

int oplus_chg_get_pd_type(void)
{
	if (pinfo != NULL) {
		pr_err("pd_type: %d\n", pinfo->pd_type);
		if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
				pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
				pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
			return PD_ACTIVE;
		} else {
			return PD_INACTIVE;
		}
	}

	return PD_INACTIVE;
}
EXPORT_SYMBOL(oplus_chg_get_pd_type);

int oplus_check_cc_mode(void) {
	int ret = -EINVAL;
	const char *tcpc_name = "type_c_port0";
	struct tcpc_device *tcpc_dev;
	tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (IS_ERR_OR_NULL(tcpc_dev)) {
		chg_err("tcpc info error\n");
		return ret;
	}
	ret = tcpm_inquire_typec_role(tcpc_dev);
	return ret;
}

int oplus_chg_set_pps_config(int vbus_mv, int ibus_ma)
{
	int ret = 0;
	int vbus_mv_t = 0;
	int ibus_ma_t = 0;
	struct tcpc_device *tcpc = NULL;

	printk(KERN_ERR "%s: request vbus_mv[%d], ibus_ma[%d]\n", __func__, vbus_mv, ibus_ma);

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (tcpc == NULL) {
		printk(KERN_ERR "%s:get type_c_port0 fail\n", __func__);
		return -EINVAL;
	}

	ret = tcpm_set_apdo_charging_policy(tcpc, DPM_CHARGING_POLICY_PPS, vbus_mv, ibus_ma, NULL);
	if (ret == TCP_DPM_RET_REJECT) {
		printk(KERN_ERR "%s: set_apdo_charging_policy reject\n", __func__);
		//return MTK_ADAPTER_REJECT;
		return 0;
	} else if (ret != 0) {
		printk(KERN_ERR "%s: set_apdo_charging_policy error\n", __func__);
		return MTK_ADAPTER_ERROR;
	}

	ret = tcpm_dpm_pd_request(tcpc, vbus_mv, ibus_ma, NULL);
	if (ret != TCPM_SUCCESS) {
		printk(KERN_ERR "%s: tcpm_dpm_pd_request fail\n", __func__);
		return -EINVAL;
	}

	ret = tcpm_inquire_pd_contract(tcpc, &vbus_mv_t, &ibus_ma_t);
	if (ret != TCPM_SUCCESS) {
		printk(KERN_ERR "%s: inquire current vbus_mv and ibus_ma fail\n", __func__);
		return -EINVAL;
	}

	printk(KERN_ERR "%s: request vbus_mv[%d], ibus_ma[%d]\n", __func__, vbus_mv_t, ibus_ma_t);

	return 0;
}

u32 oplus_chg_get_pps_status(void)
{
	int tcpm_ret = TCPM_SUCCESS;
	struct pd_pps_status pps_status;
	struct tcpc_device *tcpc = NULL;

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (tcpc == NULL) {
		printk(KERN_ERR "%s:get type_c_port0 fail\n", __func__);
		return -EINVAL;
	}

	tcpm_ret = tcpm_dpm_pd_get_pps_status(tcpc, NULL, &pps_status);
	if (tcpm_ret == TCP_DPM_RET_NOT_SUPPORT)
		return MTK_ADAPTER_NOT_SUPPORT;
	else if (tcpm_ret != 0)
		return MTK_ADAPTER_ERROR;

	return (PD_PPS_SET_OUTPUT_MV(pps_status.output_mv) | PD_PPS_SET_OUTPUT_MA(pps_status.output_ma) << 16);
}

int oplus_chg_pps_get_max_cur(int vbus_mv)
{
	unsigned int i = 0;
	int ibus_ma = 0;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
		for (i = 0; i < pinfo->srccap.nr; i++) {
			if (pinfo->srccap.min_mv[i] <= vbus_mv && vbus_mv <= pinfo->srccap.max_mv[i]) {
				if (ibus_ma < pinfo->srccap.ma[i]) {
					ibus_ma = pinfo->srccap.ma[i];
				}
			}
		}
	}

	pr_err("oplus_chg_pps_get_max_cur ibus_ma: %d\n", ibus_ma);

	if (ibus_ma > 0)
		return ibus_ma;
	else
		return -EINVAL;
}

int oplus_chg_get_charger_subtype(void)
{
	int charg_subtype = CHARGER_SUBTYPE_DEFAULT;

	if (!pinfo) {
		return charg_subtype;
	}

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
			pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30)
		charg_subtype = CHARGER_SUBTYPE_PD;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		charg_subtype = CHARGER_SUBTYPE_PD;

#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
	if (mt6375_get_hvdcp_type() == POWER_SUPPLY_TYPE_USB_HVDCP)
		charg_subtype = CHARGER_SUBTYPE_QC;
#endif

	return charg_subtype;
}
EXPORT_SYMBOL(oplus_chg_get_charger_subtype);

int oplus_chg_enable_qc_detect(void)
{
#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
	chg_err("enter chg enable qc!\n");
	mt6375_enable_hvdcp_detect();
#endif
	return 0;
}
EXPORT_SYMBOL(oplus_chg_enable_qc_detect);

int oplus_battery_meter_get_battery_voltage(void)
{
	return 4000;
}
EXPORT_SYMBOL(oplus_battery_meter_get_battery_voltage);

bool oplus_usbtemp_condition(void)
{
	int level = -1;
	int chg_volt = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!chip || !pinfo || !pinfo->tcpc) {
		return false;
	}
	if(oplus_ccdetect_check_is_gpio(g_oplus_chip)){
		level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
		if(level == 1
			|| tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_AUDIO
			|| tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC){
			return false;
		}
		if (oplus_vooc_get_fastchg_ing() != true) {
			chg_volt = chip->chg_ops->get_charger_volt();
			if(chg_volt < 3000) {
				return false;
			}
		}
		return true;
	}

	if(oplus_ccdetect_check_is_wd0(g_oplus_chip)){
		level = !oplus_chg_get_wd0_status();
		if(level == 1
			|| tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_AUDIO
			|| tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC){
			return false;
		}

		if (level == 0 && get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
			return true;
		}

		if (oplus_vooc_get_fastchg_ing() != true) {
			if (pinfo->chrdet_state) {
				return true;
			} else {
				return false;
			}
		}
		return true;
	}

	return oplus_chg_get_vbus_status(chip);
}

static enum oplus_chg_mod_property oplus_chg_usb_props[] = {
	OPLUS_CHG_PROP_ONLINE,
};

static enum oplus_chg_mod_property oplus_chg_usb_uevent_props[] = {
	OPLUS_CHG_PROP_ONLINE,
};

static int oplus_chg_usb_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	return -EINVAL;
}

static int oplus_chg_usb_set_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			const union oplus_chg_mod_propval *pval)
{
	return -EINVAL;
}

static int oplus_chg_usb_prop_is_writeable(struct oplus_chg_mod *ocm,
				enum oplus_chg_mod_property prop)
{
	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_usb_mod_desc = {
	.name = "usb",
	.type = OPLUS_CHG_MOD_USB,
	.properties = oplus_chg_usb_props,
	.num_properties = ARRAY_SIZE(oplus_chg_usb_props),
	.uevent_properties = oplus_chg_usb_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_usb_uevent_props),
	.exten_properties = NULL,
	.num_exten_properties = 0,
	.get_property = oplus_chg_usb_get_prop,
	.set_property = oplus_chg_usb_set_prop,
	.property_is_writeable	= oplus_chg_usb_prop_is_writeable,
};

static int oplus_chg_usb_event_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_mod *owner_ocm = v;

	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
	case OPLUS_CHG_EVENT_OFFLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}

		if (!strcmp(owner_ocm->desc->name, "wireless")) {
			pr_info("%s wls %s\n", __func__, val == OPLUS_CHG_EVENT_ONLINE ? "online" : "offline");
			oplus_chg_wake_update_work();
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_usb_init_mod(struct oplus_chg_chip *chip)
{
	struct oplus_chg_mod_config ocm_cfg = {};
	int rc;

	ocm_cfg.drv_data = chip;
	ocm_cfg.of_node = chip->dev->of_node;

	chip->chgic_mtk.oplus_info->usb_ocm = oplus_chg_mod_register(chip->dev,
					   &oplus_chg_usb_mod_desc,
					   &ocm_cfg);
	if (IS_ERR(chip->chgic_mtk.oplus_info->usb_ocm)) {
		pr_err("Couldn't register usb ocm\n");
		rc = PTR_ERR(chip->chgic_mtk.oplus_info->usb_ocm);
		return rc;
	}

	chip->chgic_mtk.oplus_info->usb_event_nb.notifier_call =
					oplus_chg_usb_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&chip->chgic_mtk.oplus_info->usb_event_nb);
	if (rc) {
		pr_err("register usb event notifier error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

bool is_meta_mode(void)
{
	if (!pinfo) {
		pr_err("%s: pinfo is null\n", __func__);
		return false;
	}

	pr_err("%s: bootmode = %d\n", __func__, pinfo->bootmode);

	if (pinfo->bootmode == 1)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(is_meta_mode);

static int oplus_chg_wls_input_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	if (!probe_done)
		return 0;

	if (en)
		mp2650_unsuspend_charger();
	else
		mp2650_suspend_charger();

	return 0;
}

static int oplus_chg_wls_output_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (en)
		rc = mp2650_enable_charging();
	else
		rc = mp2650_disable_charging();

	return rc;
}

static int oplus_chg_wls_set_icl(struct oplus_chg_ic_dev *ic_dev, int icl_ma)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	rc = mp2650_input_current_limit_without_aicl(icl_ma);

	return rc;
}

static int oplus_chg_wls_set_fcc(struct oplus_chg_ic_dev *ic_dev, int fcc_ma)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	fcc_ma = fcc_ma / 2;

	rc = mp2650_charging_current_write_fast(fcc_ma);

	return rc;
}

static int oplus_chg_wls_set_fv(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
                pr_err("oplus_chg_ic_dev is NULL");
                return -ENODEV;
        }

	rc = mp2650_float_voltage_write(fv_mv);

	return rc;
}

static int oplus_chg_wls_rechg_vol(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	rc = mp2650_set_rechg_voltage(fv_mv);

	return rc;
}

static int oplus_chg_wls_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	return rc;
}

static int oplus_chg_wls_get_input_vol(struct oplus_chg_ic_dev *ic_dev, int *vol)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	return rc;
}

static int oplus_chg_wls_set_boost_en(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	pr_err("mp2650_wls_set_boost_en: %d\n", en);
	if (en) {
		if (oplus_mt_get_vbus_status()) {
			oplus_set_wrx_otg_en_val(0);
			oplus_set_ext1_otg_en_val(0);
			oplus_set_ext2_otg_en_val(1);
			oplus_set_boost_en_val(1);
		} else {
			oplus_set_wrx_otg_en_val(1);
			mp2650_otg_enable();
		}
	} else {
		if (!oplus_chg_get_otg_online()) {
			oplus_set_boost_en_val(0);
			oplus_set_ext1_otg_en_val(0);
		}
		oplus_set_ext2_otg_en_val(0);
		mp2650_set_mps_otg_voltage(false);
		mp2650_set_mps_second_otg_voltage(false);
		mp2650_otg_disable();
		oplus_set_wrx_otg_en_val(0);
	}

	return rc;
}

static int oplus_chg_wls_set_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	pr_err("mp2650_wls_set_boost_vol: %d\n", vol_mv);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (oplus_mt_get_vbus_status()) {
		pr_err("%s vbus present", __func__);
		return 0;
	}
#endif

	if (vol_mv == WLS_TRX_MODE_VOL_MV) {
		mp2650_set_mps_otg_voltage(true);
		mp2650_set_mps_second_otg_voltage(true);
	} else if (vol_mv == WLS_FW_UPGRADE_VOL_MV) {
		mp2650_set_mps_otg_voltage(false);
		mp2650_set_mps_second_otg_voltage(true);
	}

	return rc;
}

static int oplus_chg_wls_set_aicl_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	return rc;
}

static int oplus_chg_wls_rerun_aicl(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	return rc;
}

static int oplus_chg_wls_set_vindpm(struct oplus_chg_ic_dev *ic_dev, int vindpm_mv)
{
	int rc = 0;
	int vol = 0;

	if (!probe_done)
		return 0;

	if (ic_dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	switch (vindpm_mv) {
	case WLS_VINDPM_BPP:
		vol = 4800;
		break;
	case WLS_VINDPM_EPP:
	case WLS_VINDPM_AIRVOOC:
	case WLS_VINDPM_AIRSVOOC:
		vol = 10700;
		mp2650_disable_buck_switch();
		mp2650_disable_async_mode();
		break;
	case WLS_VINDPM_DEFAULT:
	default:
		vol = 4400;
		mp2650_enable_buck_switch();
		mp2650_enable_async_mode();
		break;
	}

	rc = mp2650_set_vindpm_vol(vol);

	return rc;
}

static struct oplus_chg_ic_buck_ops wls_ic_ops = {
	.chg_set_input_enable = oplus_chg_wls_input_enable,
	.chg_set_output_enable = oplus_chg_wls_output_enable,
	.chg_set_icl = oplus_chg_wls_set_icl,
	.chg_set_fcc = oplus_chg_wls_set_fcc,
	.chg_set_fv = oplus_chg_wls_set_fv,
	.chg_set_rechg_vol = oplus_chg_wls_rechg_vol,
	.chg_get_icl = NULL,
	.chg_get_input_curr = oplus_chg_wls_get_input_curr,
	.chg_get_input_vol = oplus_chg_wls_get_input_vol,
	.chg_set_boost_en = oplus_chg_wls_set_boost_en,
	.chg_set_boost_vol = oplus_chg_wls_set_boost_vol,
	.chg_set_boost_curr_limit =  NULL,
	.chg_set_aicl_enable = oplus_chg_wls_set_aicl_enable,
	.chg_set_aicl_rerun = oplus_chg_wls_rerun_aicl,
	.chg_set_vindpm = oplus_chg_wls_set_vindpm,
};
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
//====================================================================//
static void oplus_mt6375_dump_registers(void)
{
    struct charger_device *chg = NULL;

    if (!g_oplus_chip) {
        chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
        return;
    }

    /*This function runs for more than 400ms, so return when no charger for saving power */
    if (g_oplus_chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN
            || oplus_get_chg_powersave() == true) {
        return;
    }
    chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

    charger_dev_dump_registers(chg);

    return;
}

static int oplus_mt6375_kick_wdt(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	rc = charger_dev_kick_wdt(chg);
	if (rc < 0) {
		chg_debug("charger_dev_kick_wdt fail\n");
	}
	return 0;
}

static int oplus_mt6375_enable_charging(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_enable(chg, true);
	chg_err("enable charger\n");
	if (rc < 0) {
		chg_debug("enable charging fail\n");
	}

	return 0;
}

static int oplus_mt6375_disable_charging(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_enable(chg, false);
	chg_err("disable charger\n");
	if (rc < 0) {
		chg_debug("disable charging fail\n");
	}

	return 0;
}

static int oplus_mt6375_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_set_constant_voltage(chg, vfloat_mv * 1000);
	if (rc < 0) {
		chg_debug("set float voltage:%d fail\n", vfloat_mv);
	}

	return 0;
}

static int oplus_mt6375_charging_current_write_fast(int chg_curr)
{
	int rc = 0;
	u32 ret_chg_curr = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_set_charging_current(chg, chg_curr * 1000);
	if (rc < 0) {
		chg_debug("set fast charge current:%d fail\n", chg_curr);
	} else {
		charger_dev_get_charging_current(chg, &ret_chg_curr);
		chg_debug("set fast charge current:%d ret_chg_curr = %d\n", chg_curr, ret_chg_curr);
	}

	return 0;
}

static int oplus_mt6375_set_termchg_current(int term_curr)
{
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	charger_dev_set_eoc_current(chg, term_curr * 1000);

	return 0;
}

static int oplus_mt6375_suspend_charger(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	charger_dev_enable_powerpath(chg,false);
	chg_err("suspend charger\n");
	if (rc < 0) {
		chg_debug("suspend charger fail\n");
	}

	return 0;
}

static int oplus_mt6375_unsuspend_charger(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	charger_dev_enable_powerpath(chg,true);
	chg_err("unsuspend charger\n");
	if (rc < 0) {
		chg_debug("unsuspend charger fail\n");
	}

	return 0;
}

static int oplus_mt6375_hardware_init(void)
{
	int hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
		oplus_mt6375_disable_charging();
		oplus_mt6375_float_voltage_write(DEFAULT_POWER_OFF_FLOAT_VOLTAGE);
		msleep(100);
	}

	oplus_mt6375_float_voltage_write(DEFAULT_FLOAT_VOLTAGE);

	oplus_mt6375_charging_current_write_fast(DEFAULT_CHARGING_CURR);

	oplus_mt6375_set_termchg_current(DEFAULT_TERM_CURR);

	charger_dev_set_mivr(chg, hw_aicl_point * 1000);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
			|| get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
		chg_err("[OPLUS_CHG]: boot mode %d stop charger!\n", get_boot_mode());
		oplus_mt6375_suspend_charger();
		oplus_mt6375_disable_charging();
	} else {
		oplus_mt6375_unsuspend_charger();
		oplus_mt6375_enable_charging();
	}
#else /* CONFIG_OPLUS_CHARGER_MTK */
	oplus_mt6375_unsuspend_charger();
#endif /* CONFIG_OPLUS_CHARGER_MTK */

	return 0;
}

static void oplus_mt6375_set_aicl_point(int vbatt)
{
	int rc = 0;
	static int hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
	struct charger_device *chg = NULL;
	int chg_vol = 0;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return;
	}

	if (!g_oplus_chip->authenticate) {
		chg_err("[OPLUS_CHG][%s]:!g_oplus_chip->authenticate \n");
		return;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol > AICL_POINT_VOL_9V) {
		hw_aicl_point = AICL_POINT_VOL_9V;
	} else {
		if (hw_aicl_point == AICL_POINT_VOL_9V) {
			hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
		}
		if (hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE1 && vbatt > AICL_POINT_VOL_5V) {
			hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE2;
		} else if (hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE2 && vbatt <= AICL_POINT_VOL_5V) {
			hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
		}
	}
	rc = charger_dev_set_mivr(chg, hw_aicl_point * 1000);
	if (rc < 0) {
		chg_debug("set aicl point:%d fail\n", hw_aicl_point);
	}
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 2000, 2400, 3000,
};

static int oplus_mt6375_input_current_limit_write(int value)
{
	int rc = 0;
	int i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int vbus_mv = 0;
	int ibus_ma = 0;
	struct charger_device *chg = NULL;
	struct tcpc_device *tcpc = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg_debug("usb input max current limit=%d setting %02x\n", value, i);

	if (!g_oplus_chip->authenticate) {
		g_oplus_chip->chg_ops->charging_disable();
		chg_err("[OPLUS_CHG][%s]: !g_oplus_chip->authenticate , charging_disable\n");
		return 0;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	if (g_oplus_chip->chg_ops->oplus_chg_get_pd_type) {
		if (g_oplus_chip->chg_ops->oplus_chg_get_pd_type() == true) {
			tcpc = tcpc_dev_get_by_name("type_c_port0");
			if (tcpc == NULL) {
				printk(KERN_ERR "%s:get type_c_port0 fail\n", __func__);
				return 0;
			}
			rc = tcpm_inquire_pd_contract(tcpc, &vbus_mv, &ibus_ma);
			if (rc != TCPM_SUCCESS) {
				printk(KERN_ERR "%s: inquire current vbus_mv and ibus_ma fail\n", __func__);
				return 0;
			}
			if (rc >= 0 && ibus_ma >= 500 && ibus_ma < 3000 && value > ibus_ma) {
				value = ibus_ma;
				chg_debug("usb input max current limit=%d(pd)\n", value);
			}
		}
	}

	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol > AICL_POINT_VOL_9V) {
		aicl_point = AICL_POINT_VOL_9V;
	} else {
		if (g_oplus_chip->batt_volt > AICL_POINT_VOL_5V)
			aicl_point = SW_AICL_POINT_VOL_5V_PHASE2;
		else
			aicl_point = SW_AICL_POINT_VOL_5V_PHASE1;
	}

	if (value < 500) {
		i = 0;
		goto aicl_end;
	}

	g_oplus_chip->aicl_done = false;

	i = 1; /* 500 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		chg_debug( "use 500 here\n");
		goto aicl_end;
	} else if (value < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 2;
		goto aicl_pre_step;
	}

	i = 5; /* 1500 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 3; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (value < 1350) {
		i = i - 2; /* We use 1.2A here */
		goto aicl_end;
	} else if (value < 1500) {
		i = i - 1; //We use 1.35A here
		goto aicl_end;
	} else if (value < 2000)
		goto aicl_end;

	i = 6; /* 2000 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value < 2400)
		goto aicl_end;

	i = 7; /* 2400 */
        rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
        msleep(90);
        chg_vol = battery_meter_get_charger_voltage();
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        } else if (value < 3000)
                goto aicl_end;

	i = 8; /* 3000 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value >= 3000)
		goto aicl_end;

aicl_pre_step:
	chg_debug("usb input max current limit aicl chg_vol=%d i[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	goto aicl_rerun;
aicl_end:
	chg_debug("usb input max current limit aicl chg_vol=%d i[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	goto aicl_rerun;
aicl_rerun:
	g_oplus_chip->aicl_done = true;

	return rc;
}

static int oplus_mt6375_check_charging_enable(void)
{
	int rc = 0;
	bool enable = false;
	bool power_path_en = false;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_is_enabled(chg,&enable);
	if (rc < 0) {
		chg_debug("charger_dev_is_enabled fail\n");
	}

	rc = charger_dev_is_powerpath_enabled(chg,&power_path_en);
	if (rc < 0) {
		chg_debug("charger_dev_is_powerpath_enabled fail\n");
	}
	chg_err("enable = %d,power_path_en = %d\n",enable,power_path_en);

	return (power_path_en && enable);
}

static int oplus_mt6375_registers_read_full(void)
{
	bool full = false;
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	rc = charger_dev_is_charging_done(chg, &full);
	if (rc < 0) {
		chg_debug("registers read full  fail\n");
		full = false;
	} else {
		//chg_debug("registers read full\n");
	}

	return full;
}

static int oplus_mt6375_otg_enable(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_enable_otg(chg,true);

	return 0;
}

static int oplus_mt6375_otg_disable(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_enable_otg(chg,false);

	return 0;
}

static int oplus_mt6375_set_chging_term_disable(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_enable_termination(chg,false);//bringup 6895
	if (rc < 0) {
		chg_debug("disable chging_term fail\n");
	}
	return 0;
}

static bool oplus_mt6375_check_charger_resume(void)
{
	return true;
}

static int oplus_mt6375_get_chg_current_step(void)
{
	return MT6375_CURR_STEP;
}

static int oplus_mt6375_get_chg_ibus(void)
{
	struct charger_device *chg = NULL;
	u32 ibus = 0;
	int ret = 0;
	if (g_oplus_chip) {
		chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
		if (chg) {
			ret = charger_dev_get_ibus(chg, &ibus);
			if (!ret)
				return ibus;
		}
	}

	return -1;
}

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oplus_mt6375_chg_get_dyna_aicl_result(void)
{
	int rc = 0;
	int aicl_ma = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	rc = charger_dev_get_input_current(chg, &aicl_ma);
	if (rc < 0) {
		chg_debug("get dyna aicl fail\n");
		return DEFAULT_AICL_RESULT;
	}
	return aicl_ma / 1000;
}
#endif

#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
static bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("[OPLUS_CHG]: oplus_chip not ready!\n");
		return shortc_hw_status;
	}

	if (oplus_shortc_check_is_gpio(chip) == true) {
		shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else /* CONFIG_OPLUS_SHORT_HW_CHECK */
static bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	return shortc_hw_status;
}
#endif /* CONFIG_OPLUS_SHORT_HW_CHECK */

#define VBUS_9V	9000
#define VBUS_5V	5000
#define IBUS_2A	2000
#define IBUS_3A	3000

static int oplus_pdc_setup(int *vbus_mv, int *ibus_ma) {
	int ret = 0;
	int vbus_mv_t = 0;
	int ibus_ma_t = 0;
	struct tcpc_device *tcpc = NULL;

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (tcpc == NULL) {
		chg_err("get type_c_port0 fail\n");
		return -EINVAL;
	}

	ret = tcpm_dpm_pd_request(tcpc, *vbus_mv, *ibus_ma, NULL);
	if (ret != TCPM_SUCCESS) {
		chg_err("tcpm_dpm_pd_request fail\n");
		return -EINVAL;
	}

	ret = tcpm_inquire_pd_contract(tcpc, &vbus_mv_t, &ibus_ma_t);
	if (ret != TCPM_SUCCESS) {
		chg_err("inquire current vbus_mv and ibus_ma fail\n");
		return -EINVAL;
	}

	chg_err("request vbus_mv[%d], ibus_ma[%d]\n", vbus_mv_t, ibus_ma_t);

	return 0;
}

int oplus_chg_set_pd_config_voocphy(void) {
	int vbus_mv = VBUS_9V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct tcpc_device *tcpc = NULL;
	int i;

	if(chip->pd_svooc){
		chg_err("pd_svooc support\n");
		return 0;
	}

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (tcpc == NULL) {
		chg_err("get type_c_port0 fail\n");
		return -EINVAL;
	}

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	chg_err("pd_type %d pd9v svooc [%d %d %d]", pinfo->pd_type, chip->limits.vbatt_pdqc_to_9v_thr, chip->limits.vbatt_pdqc_to_5v_thr, chip->batt_volt);
	chip->charger_volt = chip->chg_ops->get_charger_volt();
	if (!chip->calling_on && !chip->camera_on
	    && chip->charger_volt < CHARGER_VOLT_NORMAL
	    && (chip->limits.tbatt_pdqc_to_5v_thr < 0
		|| (chip->limits.tbatt_pdqc_to_5v_thr > 0
		    && chip->temperature <= chip->limits.tbatt_pdqc_to_5v_thr))
	    && chip->cool_down_force_5v == false
	    && (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
		oplus_voocphy_set_pdqc_config();
		if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
			adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
			for (i = 0; i < cap.nr; i++) {
				chg_err("PD APDO cap %d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n", i,
					cap.max_mv[i], cap.min_mv[i], cap.ma[i],
					cap.type[i], cap.pwr_limit[i], cap.pdp);
			}

			for (i = 0; i < cap.nr; i++) {
				if (cap.min_mv[i] <= VBUS_9V && VBUS_9V <= cap.max_mv[i]) {
					vbus_mv = VBUS_9V;
					ibus_ma = cap.ma[i];
					if (ibus_ma > IBUS_2A)
						ibus_ma = IBUS_2A;
					break;
				}
			}
		} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
			|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
			adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
			for (i = 0; i < cap.nr; i++) {
				chg_err("PD cap %d: mV:%d,%d mA:%d type:%d\n", i,
					cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
			}

			for (i = 0; i < cap.nr; i++) {
				if (VBUS_9V <= cap.max_mv[i]) {
					vbus_mv = cap.max_mv[i];
					ibus_ma = cap.ma[i];
					if (ibus_ma > IBUS_2A)
						ibus_ma = IBUS_2A;
					break;
				}
			}
		} else {
			vbus_mv = VBUS_9V;
			ibus_ma = IBUS_2A;
		}

		chg_err("PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
		ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
	} else {
		if (chip->charger_volt > CHARGER_VOLT_HV
		    && (chip->calling_on || chip->camera_on
			|| chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr
			|| (chip->limits.tbatt_pdqc_to_5v_thr > 0
			    && chip->temperature > chip->limits.tbatt_pdqc_to_5v_thr)
			|| chip->cool_down_force_5v == true)) {
			vbus_mv = VBUS_5V;
			ibus_ma = IBUS_2A;

			chg_err("PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
			ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		}

		chg_err("qc9v svooc  batt_volt: %d", chip->batt_volt);
	}

	return ret;
}

int oplus_chg_set_pd_config_mt6375(void) {
	int ret = -1;

	if (is_mtksvooc_project) {
		if (oplus_chg_get_voocphy_support()) {
			ret = oplus_chg_set_pd_config_voocphy();
		} else {
			ret = oplus_chg_set_pd_config();/*not use, pre for mp2762 pd*/
		}
	}

	return ret;
}

bool oplus_chg_check_qchv_condition(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("oplus_chip is null\n");
		return false;
	}

	chip->charger_volt = chip->chg_ops->get_charger_volt();
	if (!chip->calling_on && !chip->camera_on
	    && chip->charger_volt < CHARGER_VOLT_NORMAL
	    && (chip->limits.tbatt_pdqc_to_5v_thr < 0
		|| (chip->limits.tbatt_pdqc_to_5v_thr > 0
		    && chip->temperature <= chip->limits.tbatt_pdqc_to_5v_thr))
	    && chip->cool_down_force_5v == false
	    && (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
		return true;
	}

	return false;
}

int get_boot_reason(void)
{
	return 0;
}

struct oplus_chg_operations  mtk6375_chg_ops = {
	.dump_registers = oplus_mt6375_dump_registers,
	.kick_wdt = oplus_mt6375_kick_wdt,
	.hardware_init = oplus_mt6375_hardware_init,
	.charging_current_write_fast = oplus_mt6375_charging_current_write_fast,
	.set_aicl_point = oplus_mt6375_set_aicl_point,
	.input_current_write = oplus_mt6375_input_current_limit_write,
	.float_voltage_write = oplus_mt6375_float_voltage_write,
	.term_current_set = oplus_mt6375_set_termchg_current,
	.charging_enable = oplus_mt6375_enable_charging,
	.charging_disable = oplus_mt6375_disable_charging,
	.get_charging_enable = oplus_mt6375_check_charging_enable,
	.charger_suspend = oplus_mt6375_suspend_charger,
	.charger_unsuspend = oplus_mt6375_unsuspend_charger,
	.read_full = oplus_mt6375_registers_read_full,
	.otg_enable = oplus_mt6375_otg_enable,
	.otg_disable = oplus_mt6375_otg_disable,
	.set_charging_term_disable = oplus_mt6375_set_chging_term_disable,
	.check_charger_resume = oplus_mt6375_check_charger_resume,
	.get_chg_current_step = oplus_mt6375_get_chg_current_step,
#ifdef CONFIG_OPLUS_CHARGER_MTK
	.get_charger_type = mt_power_supply_type_check,
	.get_charger_volt = battery_meter_get_charger_voltage,
	.check_chrdet_status = oplus_mt_get_vbus_status,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_chargerid_volt = mt_get_chargerid_volt,
	.set_chargerid_switch_val = mt_set_chargerid_switch_val ,
	.get_chargerid_switch_val  = mt_get_chargerid_switch_val,
	.get_rtc_soc = get_rtc_spare_oplus_fg_value,
	.set_rtc_soc = set_rtc_spare_oplus_fg_value,
	.set_power_off = oplus_mt_power_off,
	.get_charger_subtype = oplus_chg_get_charger_subtype,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
	.get_platform_gauge_curve = oplus_chg_choose_gauge_curve,
	.get_charger_current = oplus_mt6375_get_chg_ibus,
#else /* CONFIG_OPLUS_CHARGER_MTK */
	.get_charger_type = qpnp_charger_type_get,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.check_chrdet_status = qpnp_lbc_is_usb_chg_plugged_in,
	.get_instant_vbatt = qpnp_get_prop_battery_voltage_now,
	.get_boot_mode = get_boot_mode,
	.get_rtc_soc = qpnp_get_pmic_soc_memory,
	.set_rtc_soc = qpnp_set_pmic_soc_memory,
#endif /* CONFIG_OPLUS_CHARGER_MTK */

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oplus_mt6375_chg_get_dyna_aicl_result,
#endif
	.get_shortc_hw_gpio_status = oplus_chg_get_shortc_hw_gpio_status,
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
	.oplus_chg_get_pd_type = oplus_chg_get_pd_type,
	.oplus_chg_pd_setup = oplus_chg_set_pd_config_mt6375,
	.set_qc_config = oplus_chg_set_qc_config,
	.enable_qc_detect = oplus_chg_enable_qc_detect,	//for qc 9v2a //bringup 6895
	.check_pdphy_ready = oplus_check_pdphy_ready,
	.get_usbtemp_volt = oplus_get_usbtemp_volt,
	.set_typec_sinkonly = oplus_set_typec_sinkonly,
	.set_typec_cc_open = oplus_usbtemp_set_cc_open,
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
	.check_qchv_condition = oplus_chg_check_qchv_condition,
	.get_subboard_temp = oplus_force_get_subboard_temp,
	.check_cc_mode = oplus_check_cc_mode,
};
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef CONFIG_THERMAL
static int cp_thermal_read_temp(struct thermal_zone_device *tzd,
		int *temp)
{
	int cp_temp;

	cp_temp = oplus_chg_get_main_battery_btb() * 100; /* thermal zone need 0.001 */
	*temp = cp_temp;

	return 0;
}

static struct thermal_zone_device_ops cp_temp_tzd_ops = {
	.get_temp = cp_thermal_read_temp,
};

static int sub_batt_thermal_read_temp(struct thermal_zone_device *tzd,
		int *temp)
{
	int batt_temp;

	batt_temp = oplus_chg_get_sub_battery_btb() * 100; /* thermal zone need 0.001 */
	*temp = batt_temp;

	return 0;
}

static struct thermal_zone_device_ops sub_batt_temp_tzd_ops = {
	.get_temp = sub_batt_thermal_read_temp,
};

static int register_tz_thermal(struct mtk_charger *info)
{
	int ret = 0;

	info->cp_temp_tzd = thermal_zone_device_register("charge_pump_temp",
					0, 0, NULL, &cp_temp_tzd_ops, NULL, 0, 0);
	if (IS_ERR(info->cp_temp_tzd)) {
		chg_err("cp_temp_tzd register fail");
		return PTR_ERR(info->cp_temp_tzd);
	}
	ret = thermal_zone_device_enable(info->cp_temp_tzd);
	if (ret)
		thermal_zone_device_unregister(info->cp_temp_tzd);

	info->sub_batt_temp_tzd = thermal_zone_device_register("sub_batt_temp",
					0, 0, NULL, &sub_batt_temp_tzd_ops, NULL, 0, 0);
	if (IS_ERR(info->sub_batt_temp_tzd)) {
		chg_err("sub_batt_temp_tzd register fail");
		return PTR_ERR(info->sub_batt_temp_tzd);
	}
	ret = thermal_zone_device_enable(info->sub_batt_temp_tzd);
	if (ret)
		thermal_zone_device_unregister(info->sub_batt_temp_tzd);

	return ret;
}
#endif

static int mtk_charger_probe(struct platform_device *pdev)
{
	struct mtk_charger *info = NULL;
	int i;
	char *name = NULL;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct netlink_kernel_cfg cfg = {
		.input = chg_nl_data_handler,
	};

#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_chg_chip *oplus_chip;
	int level = 0;
	int rc = 0;
#endif

	chr_err("%s: starts\n", __func__);

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (!tcpc_dev_get_by_name("type_c_port0")) {
		chr_err("%s get tcpc device type_c_port0 fail\n", __func__);
		return -EPROBE_DEFER;
	}

	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(*oplus_chip), GFP_KERNEL);
	if (!oplus_chip)
		return -ENOMEM;

	oplus_chip->dev = &pdev->dev;
	g_oplus_chip = oplus_chip;
	oplus_chg_parse_svooc_dt(oplus_chip);
	if (oplus_chip->vbatt_num == 1) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oplus_chip->chg_ops = &mtk6375_chg_ops;
		is_vooc_cfg = true;
		is_mtksvooc_project = true;
		chg_err("%s is_vooc_cfg = %d\n", __func__, is_vooc_cfg);
	} else {
		chg_err("[oplus_chg_init] gauge[%d]vooc[%d]adapter[%d]\n",
				oplus_gauge_check_chip_is_null(),
				oplus_vooc_check_chip_is_null(),
				oplus_adapter_check_chip_is_null());
		if (oplus_gauge_check_chip_is_null() || oplus_vooc_check_chip_is_null() || oplus_adapter_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge || vooc || adapter null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oplus_chip->chg_ops = oplus_get_chg_ops();
		is_vooc_cfg = true;
		is_mtksvooc_project = true;
		chg_err("%s is_vooc_cfg = %d\n", __func__, is_vooc_cfg);
	}
#endif /* OPLUS_FEATURE_CHG_BASIC */

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	mtk_charger_parse_dt(info, &pdev->dev);

#ifdef OPLUS_FEATURE_CHG_BASIC
	pinfo = info;
	oplus_chip->chgic_mtk.oplus_info = info;

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev) {
		chg_err("found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	} else {
		chg_err("can't find primary charger!\n");
	}
#endif

	mutex_init(&info->cable_out_lock);
	mutex_init(&info->charger_lock);
	mutex_init(&info->pd_lock);

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_power_supply_init(oplus_chip);
	oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	oplus_chip->authenticate = oplus_gauge_get_batt_authenticate();
	chg_err("oplus_chg_init!\n");
	oplus_chg_init(oplus_chip);

	rc = of_property_read_u32(oplus_chip->dev->of_node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		pr_err("can't get ic type, rc=%d\n", rc);
	}

	rc = of_property_read_u32(oplus_chip->dev->of_node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		pr_err("can't get ic index, rc=%d\n", rc);
	}

	info->ic_dev = devm_oplus_chg_ic_register(oplus_chip->dev, oplus_chip->dev->of_node->name, ic_index);
	if (!info->ic_dev) {
		rc = -ENODEV;
		pr_err("register %s error\n", oplus_chip->dev->of_node->name);
		return rc;
	}

	info->ic_dev->dev_ops = &wls_ic_ops;
	info->ic_dev->type = ic_type;

	if (get_boot_mode() != KERNEL_POWER_OFF_CHARGING_BOOT) {
		oplus_tbatt_power_off_task_init(oplus_chip);
	}
#endif

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s",
		"charger suspend wakelock");
	info->charger_wakelock =
		wakeup_source_register(NULL, name);
	spin_lock_init(&info->slock);

	init_waitqueue_head(&info->wait_que);
	info->polling_interval = CHARGING_INTERVAL;
	mtk_charger_init_timer(info);
#ifdef CONFIG_PM
	info->pm_notifier.notifier_call = charger_pm_event;
#endif /* CONFIG_PM */
	srcu_init_notifier_head(&info->evt_nh);
	mtk_charger_setup_files(pdev);
	mtk_charger_get_atm_mode(info);

	for (i = 0; i < CHGS_SETTING_MAX; i++) {
		info->chg_data[i].thermal_charging_current_limit = -1;
		info->chg_data[i].thermal_input_current_limit = -1;
		info->chg_data[i].input_current_limit_by_aicl = -1;
	}
	info->enable_hv_charging = true;

	info->psy_desc1.name = "mtk-master-charger";
	info->psy_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc1.properties = charger_psy_properties;
	info->psy_desc1.num_properties = ARRAY_SIZE(charger_psy_properties);
	info->psy_desc1.get_property = psy_charger_get_property;
	info->psy_desc1.set_property = psy_charger_set_property;
	info->psy_desc1.property_is_writeable =
			psy_charger_property_is_writeable;
	info->psy_desc1.external_power_changed =
		mtk_charger_external_power_changed;
	info->psy_cfg1.drv_data = info;
	info->psy1 = power_supply_register(&pdev->dev, &info->psy_desc1,
			&info->psy_cfg1);

	info->chg_psy = devm_power_supply_get_by_phandle(&pdev->dev,
		"charger");
	if (IS_ERR_OR_NULL(info->chg_psy))
		chr_err("%s: devm power fail to get chg_psy\n", __func__);

	info->bat_psy = devm_power_supply_get_by_phandle(&pdev->dev,
		"gauge");
	if (IS_ERR_OR_NULL(info->bat_psy))
		chr_err("%s: devm power fail to get bat_psy\n", __func__);

	if (IS_ERR(info->psy1))
		chr_err("register psy1 fail:%d\n",
			PTR_ERR(info->psy1));

	info->psy_desc2.name = "mtk-slave-charger";
	info->psy_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc2.properties = charger_psy_properties;
	info->psy_desc2.num_properties = ARRAY_SIZE(charger_psy_properties);
	info->psy_desc2.get_property = psy_charger_get_property;
	info->psy_desc2.set_property = psy_charger_set_property;
	info->psy_desc2.property_is_writeable =
			psy_charger_property_is_writeable;
	info->psy_cfg2.drv_data = info;
	info->psy2 = power_supply_register(&pdev->dev, &info->psy_desc2,
			&info->psy_cfg2);

	if (IS_ERR(info->psy2))
		chr_err("register psy2 fail:%d\n",
			PTR_ERR(info->psy2));

	info->psy_dvchg_desc1.name = "mtk-mst-div-charger";
	info->psy_dvchg_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_dvchg_desc1.properties = charger_psy_properties;
	info->psy_dvchg_desc1.num_properties =
		ARRAY_SIZE(charger_psy_properties);
	info->psy_dvchg_desc1.get_property = psy_charger_get_property;
	info->psy_dvchg_desc1.set_property = psy_charger_set_property;
	info->psy_dvchg_desc1.property_is_writeable =
		psy_charger_property_is_writeable;
	info->psy_dvchg_cfg1.drv_data = info;
	info->psy_dvchg1 = power_supply_register(&pdev->dev,
						 &info->psy_dvchg_desc1,
						 &info->psy_dvchg_cfg1);
	if (IS_ERR(info->psy_dvchg1))
		chr_err("register psy dvchg1 fail:%d\n",
			PTR_ERR(info->psy_dvchg1));

	info->psy_dvchg_desc2.name = "mtk-slv-div-charger";
	info->psy_dvchg_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_dvchg_desc2.properties = charger_psy_properties;
	info->psy_dvchg_desc2.num_properties =
		ARRAY_SIZE(charger_psy_properties);
	info->psy_dvchg_desc2.get_property = psy_charger_get_property;
	info->psy_dvchg_desc2.set_property = psy_charger_set_property;
	info->psy_dvchg_desc2.property_is_writeable =
		psy_charger_property_is_writeable;
	info->psy_dvchg_cfg2.drv_data = info;
	info->psy_dvchg2 = power_supply_register(&pdev->dev,
						 &info->psy_dvchg_desc2,
						 &info->psy_dvchg_cfg2);
	if (IS_ERR(info->psy_dvchg2))
		chr_err("register psy dvchg2 fail:%d\n",
			PTR_ERR(info->psy_dvchg2));

	info->log_level = CHRLOG_ERROR_LEVEL;

	info->pd_adapter = get_adapter_by_name("pd_adapter");
	if (!info->pd_adapter)
		chr_err("%s: No pd adapter found\n");
	else {
		info->pd_nb.notifier_call = notify_adapter_event;
		register_adapter_device_notifier(info->pd_adapter,
						 &info->pd_nb);
	}

	info->sc.daemo_nl_sk = netlink_kernel_create(&init_net, NETLINK_CHG, &cfg);

	if (info->sc.daemo_nl_sk == NULL)
		chr_err("sc netlink_kernel_create error id:%d\n", NETLINK_CHG);
	else
		chr_err("sc_netlink_kernel_create success id:%d\n", NETLINK_CHG);
	sc_init(&info->sc);

	info->chg_alg_nb.notifier_call = chg_alg_event;

	info->fast_charging_indicator = 0;
	info->is_charging = false;
	info->safety_timer_cmd = -1;

	kthread_run(charger_routine_thread, info, "charger_thread");

#ifdef OPLUS_FEATURE_CHG_BASIC
	pinfo->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!pinfo->tcpc) {
		chr_err("%s get tcpc device type_c_port0 fail\n", __func__);
	} else {
		pinfo->pd_nb.notifier_call = pd_tcp_notifier_call;
		rc = register_tcp_dev_notifier(pinfo->tcpc, &pinfo->pd_nb,
					TCP_NOTIFY_TYPE_ALL);
		if (rc < 0) {
			pr_err("%s: register tcpc notifer fail\n", __func__);
			return -EINVAL;
		}
	}

	pinfo->chargeric_temp_chan = devm_iio_channel_get(oplus_chip->dev, "auxadc3-chargeric_temp");
	if (IS_ERR(pinfo->chargeric_temp_chan)) {
		chg_err("Couldn't get chargeric_temp_chan...\n");
		pinfo->chargeric_temp_chan = NULL;
	}

	pinfo->charger_id_chan = devm_iio_channel_get(oplus_chip->dev, "auxadc3-charger_id");
	if (IS_ERR(pinfo->charger_id_chan)) {
		chg_err("Couldn't get charger_id_chan...\n");
		pinfo->charger_id_chan = NULL;
	}

	pinfo->usb_temp_v_l_chan = devm_iio_channel_get(oplus_chip->dev, "usb_temp_v_l");
	if (IS_ERR(pinfo->usb_temp_v_l_chan)) {
		chg_err("Couldn't get usb_temp_v_l_chan...\n");
		pinfo->usb_temp_v_l_chan = NULL;
	}

	pinfo->usb_temp_v_r_chan = devm_iio_channel_get(oplus_chip->dev, "usb_temp_v_r");
	if (IS_ERR(pinfo->usb_temp_v_r_chan)) {
		chg_err("Couldn't get usb_temp_v_r_chan...\n");
		pinfo->usb_temp_v_r_chan = NULL;
	}

	pinfo->usbcon_temp_chan = devm_iio_channel_get(oplus_chip->dev, "usbcon_temp");
	if (IS_ERR(pinfo->usbcon_temp_chan)) {
		chg_err("Couldn't get usbcon_temp_chan...\n");
		pinfo->usbcon_temp_chan = NULL;
	}

	pinfo->batcon_temp_chan = devm_iio_channel_get(oplus_chip->dev, "batcon_temp");
	if (IS_ERR(pinfo->batcon_temp_chan)) {
		chg_err("Couldn't get batcon_temp_chan...\n");
		pinfo->batcon_temp_chan = NULL;
	}

	pinfo->sub_batcon_temp_chan = devm_iio_channel_get(oplus_chip->dev, "sub_batcon_temp");
	if (IS_ERR(pinfo->sub_batcon_temp_chan)) {
		chg_err("Couldn't get sub_batcon_temp_chan...\n");
		pinfo->sub_batcon_temp_chan = NULL;
	}

	pinfo->subboard_temp_chan = devm_iio_channel_get(oplus_chip->dev, "subboard_temp");
	if (IS_ERR(pinfo->subboard_temp_chan)) {
		chg_err("Couldn't get subboard_temp_chan...\n");
		pinfo->subboard_temp_chan = NULL;
	}

	pinfo->batt_ntc_param = init_ntc_param("batt_ntc_param");
	pinfo->sub_batt_ntc_param = init_ntc_param("sub_batt_ntc_param");
	pinfo->usb_ntc_param = init_ntc_param("usb_ntc_param");

	if (oplus_ccdetect_support_check() == true){
		INIT_DELAYED_WORK(&ccdetect_work,oplus_ccdetect_work);
		INIT_DELAYED_WORK(&usbtemp_recover_work,oplus_usbtemp_recover_work);
		oplus_ccdetect_irq_register(oplus_chip);
		level = gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio);
		usleep_range(2000, 2100);
		if (level != gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio)) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(10000, 11000);
			level = gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio);
		}

		if (level <= 0) {
			schedule_delayed_work(&ccdetect_work, msecs_to_jiffies(6000));
		}

		printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio ..level[%d]  \n", __func__, level);
	}

	if (oplus_ccdetect_check_is_wd0(oplus_chip) == true) {
		INIT_DELAYED_WORK(&wd0_detect_work, oplus_wd0_detect_work);
		INIT_DELAYED_WORK(&wd0_get_status_work, oplus_wd0_get_status_work);
		if (oplus_ccdetect_support_check() == false) {
			INIT_DELAYED_WORK(&usbtemp_recover_work, oplus_usbtemp_recover_work);
		}
	}

	if (info->ntc_temp_volt_1840mv) {
		oplus_chip->con_volt = con_volt_ntc_100k_1840mv;
		oplus_chip->con_temp = con_temp_ntc_100k_1840mv;
		oplus_chip->len_array = ARRAY_SIZE(con_temp_ntc_100k_1840mv);
	} else {
		oplus_chip->con_volt = con_volt_20131;
		oplus_chip->con_temp = con_temp_20131;
		oplus_chip->len_array = ARRAY_SIZE(con_temp_20131);
	}

	if (oplus_usbtemp_check_is_support() == true)
		oplus_usbtemp_thread_init();

	INIT_DELAYED_WORK(&pinfo->status_keep_clean_work, oplus_chg_wls_status_keep_clean_work);
	pinfo->status_wake_lock = wakeup_source_register(&pinfo->pdev->dev, "status_wake_lock");
	pinfo->status_wake_lock_on = false;

	oplus_chg_wake_update_work();

        //if (is_vooc_project() == true)
        //        oplus_mt6360_suspend_charger();

        oplus_chg_configfs_init(oplus_chip);

	rc = oplus_chg_usb_init_mod(oplus_chip);
	if (rc < 0) {
		pr_err("oplus_chg_usb_init_mod failed, rc=%d\n", rc);
	}

	mtk_chg_enable_vbus_ovp(true);
	if (wd0_get_status_work.work.func) {
		schedule_delayed_work(&wd0_get_status_work, msecs_to_jiffies(WD0_GET_STATUS_DELAY_MS));
	}

	if (pinfo->tcpc && tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_UNATTACHED) {
		if (oplus_chip->chgic_mtk.oplus_info->usb_ocm)
			oplus_chg_global_event(oplus_chip->chgic_mtk.oplus_info->usb_ocm, OPLUS_CHG_EVENT_ADSP_STARTED);
		else
			pr_err("usb ocm not fount\n");
	}

#ifdef CONFIG_THERMAL
	rc = register_tz_thermal(pinfo);
	if (rc)
		chg_err("register tz fail");
#endif

	oplus_mt6375_record_qc_type(&oplus_chg_track_record_chg_type_info);
	oplus_mt6375_wired_charging_break(&oplus_chg_track_check_wired_charging_break);

	probe_done = true;
#endif

	return 0;
}

static int mtk_charger_remove(struct platform_device *dev)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct mtk_charger *info = g_oplus_chip->chgic_mtk.oplus_info;

	probe_done = true;
	//devm_oplus_chg_ic_unregister(bcdev->dev, bcdev->ic_dev);
	oplus_chg_mod_unregister(info->usb_ocm);
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	oplus_test_kit_unregister();
#endif
#endif

	return 0;
}

static void mtk_charger_shutdown(struct platform_device *dev)
{
	struct mtk_charger *info = platform_get_drvdata(dev);
	int i;

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (info->alg[i] == NULL)
			continue;
		chg_alg_stop_algo(info->alg[i]);
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (g_oplus_chip) {
		enter_ship_mode_function(g_oplus_chip);
	}
#endif	
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static unsigned long suspend_tm_sec = 0;
static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc = NULL;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		chg_err("%s: unable to open rtc device (%s)\n",
		__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		chg_err("Error reading rtc device (%s) : %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		chg_err("Invalid RTC time (%s): %d\n",
		CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);

	return rc;
}

static int mtk_charger_pm_resume(struct device *dev)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int ui_soc, real_soc;
	int rc = 0;

	if (!g_oplus_chip)
		return 0;

	if (g_oplus_chip->chg_ops != &mtk6375_chg_ops)
		return 0;

	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}

	ui_soc = oplus_chg_get_ui_soc();
	real_soc = oplus_chg_get_soc();
	if (abs(ui_soc - real_soc) > URGENT_CHECK_THRESHOLD ||
	    sleep_time > NORMAL_CHECK_UPDATE_INTERVAL)
		oplus_chg_soc_update_when_resume(sleep_time);

	return 0;
}

static int mtk_charger_pm_suspend(struct device *dev)
{
	if (!g_oplus_chip)
		return 0;

	if (g_oplus_chip->chg_ops != &mtk6375_chg_ops)
		return 0;

	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}

	return 0;
}

static const struct dev_pm_ops mtk_charger_pm_ops = {
	.resume		= mtk_charger_pm_resume,
	.suspend	= mtk_charger_pm_suspend,
};
#endif
#endif

static const struct of_device_id mtk_charger_of_match[] = {
	{.compatible = "mediatek,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_charger_of_match);

struct platform_device mtk_charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver mtk_charger_driver = {
	.probe = mtk_charger_probe,
	.remove = mtk_charger_remove,
	.shutdown = mtk_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = mtk_charger_of_match,
#ifdef OPLUS_FEATURE_CHG_BASIC
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		   .pm = &mtk_charger_pm_ops,
#endif
#endif
	},
};

static int __init mtk_charger_init(void)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	charge_pump_driver_init();
	mp2650_driver_init();
	bq27541_driver_init();
	op10_subsys_init();
	adapter_ic_init();
	sc8547_subsys_init();
	sc8547_slave_subsys_init();
	p9418_driver_init();
#ifdef CONFIG_OPLUS_CHARGER_OPTIGA
	oplus_optiga_driver_init();
#endif
#endif
	return platform_driver_register(&mtk_charger_driver);
}
#ifndef OPLUS_FEATURE_CHG_BASIC
module_init(mtk_charger_init);
#endif

static void __exit mtk_charger_exit(void)
{
	platform_driver_unregister(&mtk_charger_driver);
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_ops_deinit();
#ifdef CONFIG_OPLUS_CHARGER_OPTIGA
	oplus_optiga_driver_exit();
#endif
#endif
	sc8547_subsys_exit();
	sc8547_slave_subsys_exit();
	p9418_driver_exit();
}
#ifndef OPLUS_FEATURE_CHG_BASIC
module_exit(mtk_charger_exit);
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
oplus_chg_module_register(mtk_charger);
#endif

MODULE_DESCRIPTION("MTK Charger Driver");
MODULE_LICENSE("GPL");
