// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#include <linux/configfs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/nls.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/reboot.h>
#include "oplus_chg_core.h"
#include "oplus_charger.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_pps.h"
#include "oplus_short.h"
#include "oplus_adapter.h"
#include "oplus_wireless.h"
#include "charger_ic/oplus_short_ic.h"
#include "charger_ic/oplus_switching.h"
#include "oplus_debug_info.h"
#include "oplus_chg_track.h"
#include "op_wlchg_v2/oplus_chg_wls.h"
#include "wireless_ic/oplus_nu1619.h"
#include "voocphy/oplus_voocphy.h"
#include "oplus_ufcs.h"
#include "oplus_quirks.h"
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/oplus_project.h>
#endif

#define OPLUS_SVOOC_ID_MIN    10

static struct class *oplus_chg_class;
static struct device *oplus_ac_dir;
static struct device *oplus_usb_dir;
static struct device *oplus_battery_dir;
static struct device *oplus_wireless_dir;
static struct device *oplus_common_dir;

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

/**********************************************************************
* ac device nodes
**********************************************************************/
static ssize_t ac_online_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_ac_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->charger_exist) {
		if ((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) || (oplus_vooc_get_fastchg_started() == true) ||
		    (oplus_vooc_get_fastchg_to_normal() == true) || (oplus_vooc_get_fastchg_to_warm() == true) ||
		    (oplus_vooc_get_fastchg_dummy_started() == true) ||
		    (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
		    (oplus_vooc_get_btb_temp_over() == true)) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;
		}
	} else {
		if ((oplus_vooc_get_fastchg_started() == true) || (oplus_vooc_get_fastchg_to_normal() == true) ||
		    (oplus_vooc_get_fastchg_to_warm() == true) || (oplus_vooc_get_fastchg_dummy_started() == true) ||
		    (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
		    (oplus_vooc_get_btb_temp_over() == true) || chip->mmi_fastchg == 0) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;
		}
	}

	if (chip->ac_online == false && oplus_quirks_keep_connect_status() == 1)
		chip->ac_online = true;

	if (chip->ac_online) {
		chg_err("chg_exist:%d, ac_online:%d\n", chip->charger_exist, chip->ac_online);
	}

	return sprintf(buf, "%d\n", chip->ac_online);
}
static DEVICE_ATTR(online, S_IRUGO, ac_online_show, NULL);

static ssize_t ac_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "Mains");
}
static DEVICE_ATTR(type, S_IRUGO, ac_type_show, NULL);

static struct device_attribute *oplus_ac_attributes[] = {
	&dev_attr_online,
	&dev_attr_type,
	NULL
};

/**********************************************************************
* usb device nodes
**********************************************************************/
int __attribute__((weak)) oplus_get_fast_chg_type(void)
{
	return 0;
}

int __attribute__((weak)) oplus_get_otg_online_status(void)
{
	return 0;
}

static ssize_t otg_online_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int otg_online = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	otg_online = oplus_get_otg_online_status();

	return sprintf(buf, "%d\n", otg_online);
}
static DEVICE_ATTR_RO(otg_online);

int __attribute__((weak)) oplus_get_otg_switch_status(void)
{
	return 0;
}

static ssize_t otg_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_get_otg_switch_status();
	return sprintf(buf, "%d\n", chip->otg_switch);
}

void __attribute__((weak)) oplus_set_otg_switch_status(bool value)
{
	return;
}

static ssize_t otg_switch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val == 1) {
		chip->otg_switch = true;
		oplus_set_otg_switch_status(true);
	} else {
		chip->otg_switch = false;
		chip->otg_online = false;
		oplus_set_otg_switch_status(false);
	}

	return count;
}
static DEVICE_ATTR_RW(otg_switch);

int __attribute__((weak)) oplus_get_usb_status(void)
{
	return 0;
}

static ssize_t usb_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status = 0;

	status = oplus_get_usb_status();
	return sprintf(buf, "%d\n", status);
}
static DEVICE_ATTR_RO(usb_status);

int __attribute__((weak)) oplus_get_usbtemp_volt_l(void)
{
	return 0;
}

static ssize_t usbtemp_volt_l_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int volt = 0;

	volt = oplus_get_usbtemp_volt_l();
	return sprintf(buf, "%d\n", volt);
}
static DEVICE_ATTR_RO(usbtemp_volt_l);

int __attribute__((weak)) oplus_get_usbtemp_volt_r(void)
{
	return 0;
}

static ssize_t usbtemp_volt_r_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int volt = 0;

	volt = oplus_get_usbtemp_volt_r();
	return sprintf(buf, "%d\n", volt);
}
static DEVICE_ATTR_RO(usbtemp_volt_r);

static int fast_chg_type_by_user = -1;
static ssize_t fast_chg_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int type = oplus_chg_get_fast_chg_type();

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_PD_SDP ||
	    (CHARGER_SUBTYPE_PD == type && (chip->pd_svooc || chip->charger_type == POWER_SUPPLY_TYPE_USB ||
						chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP ||
						chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN))) {
		type = CHARGER_SUBTYPE_DEFAULT;
	}

	if (fast_chg_type_by_user > 0)
		type = fast_chg_type_by_user;
	return sprintf(buf, "%d\n", type);
}
static ssize_t fast_chg_type_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	/*
	if (get_eng_version() == RELEASE)
		return count;
	*/

	fast_chg_type_by_user = val;
	chg_err("costumer set val [%d], fast_chg_type_by_user [%d]\n", val, fast_chg_type_by_user);

	return count;
}
static DEVICE_ATTR_RW(fast_chg_type);

int __attribute__((weak)) oplus_get_typec_cc_orientation(void)
{
	return 0;
}

static ssize_t typec_cc_orientation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int cc_orientation = 0;

	cc_orientation = oplus_get_typec_cc_orientation();

	return sprintf(buf, "%d\n", cc_orientation);
}
static DEVICE_ATTR_RO(typec_cc_orientation);

int __attribute__((weak)) oplus_get_water_detect(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_water_detect(bool enable)
{
	return;
}

static ssize_t water_detect_feature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_get_water_detect());
}

static ssize_t water_detect_feature_store(struct device *dev, struct device_attribute *attr, const char *buf,
					  size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set water_detect_feature = [%d].\n", val);

	if (val == 0) {
		oplus_set_water_detect(false);
	} else {
		oplus_set_water_detect(true);
	}

	return count;
}
static DEVICE_ATTR_RW(water_detect_feature);

static struct device_attribute *oplus_usb_attributes[] = {
	&dev_attr_otg_online,
	&dev_attr_otg_switch,
	&dev_attr_usb_status,
	&dev_attr_typec_cc_orientation,
	&dev_attr_fast_chg_type,
	&dev_attr_usbtemp_volt_l,
	&dev_attr_usbtemp_volt_r,
	&dev_attr_water_detect_feature,
	NULL
};

/**********************************************************************
* battery device nodes
**********************************************************************/
static ssize_t authenticate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->authenticate);
}
static DEVICE_ATTR_RO(authenticate);

static ssize_t battery_cc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_cc);
}
static DEVICE_ATTR_RO(battery_cc);

static ssize_t battery_fcc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_fcc);
}
static DEVICE_ATTR_RO(battery_fcc);

static ssize_t battery_rm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_rm);
}
static DEVICE_ATTR_RO(battery_rm);

static ssize_t design_capacity_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_capacity_mah);
}
static DEVICE_ATTR_RO(design_capacity);

static ssize_t smartchg_soh_support_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->smart_chg_soh_support);
}
static DEVICE_ATTR_RO(smartchg_soh_support);

static ssize_t battery_soh_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_soh);
}
static DEVICE_ATTR_RO(battery_soh);

static ssize_t soh_report_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_chg_get_soh_report());
}
static DEVICE_ATTR_RO(soh_report);

static ssize_t cc_report_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_chg_get_cc_report());
}
static DEVICE_ATTR_RO(cc_report);

#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
static ssize_t call_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->calling_on);
}

static ssize_t call_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chip->calling_on = val;
	if (is_wls_ocm_available(chip))
		oplus_chg_anon_mod_event(chip->wls_ocm, val ? OPLUS_CHG_EVENT_CALL_ON : OPLUS_CHG_EVENT_CALL_OFF);

	return count;
}
static DEVICE_ATTR_RW(call_mode);
#endif /*CONFIG_OPLUS_CALL_MODE_SUPPORT*/

static ssize_t charge_technology_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->vooc_project);
}

static ssize_t charge_technology_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	/*
	if (get_eng_version() == RELEASE)
		return count;
	*/

	if (val > NO_VOOC && val < INVALID_VOOC_PROJECT)
		chip->vooc_project = val;
	chg_err("costumer set val [%d], new_vooc-project [%d]\n", val, chip->vooc_project);

	return count;
}
static DEVICE_ATTR_RW(charge_technology);

#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
static ssize_t chip_soc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->soc);
}
static DEVICE_ATTR_RO(chip_soc);
#endif /*CONFIG_OPLUS_CHIP_SOC_NODE*/

#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
static ssize_t cool_down_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->cool_down);
}

static ssize_t cool_down_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	oplus_smart_charge_by_cool_down(chip, val);
	if (is_wls_ocm_available(chip)) {
		pval.intval = val;
		oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_COOL_DOWN, &pval);
	}

	return count;
}
static DEVICE_ATTR_RW(cool_down);
#endif /*CONFIG_OPLUS_SMART_CHARGER_SUPPORT*/

static ssize_t em_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->em_mode);
}

static ssize_t em_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val == 0) {
		chip->em_mode = false;
	} else {
		chip->em_mode = true;
#ifndef CONFIG_OPLUS_CHARGER_MTK
		if (chip->chg_ops && chip->chg_ops->subcharger_force_enable)
			chip->chg_ops->subcharger_force_enable();
#endif
	}

	return count;
}
static DEVICE_ATTR_RW(em_mode);

static ssize_t normal_current_now_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	val = val & 0XFFFF;
	chg_err("val:%d\n", val);
	if (!chip->led_on) {
		if (chip->smart_normal_cool_down == 0) {
			if (oplus_pps_get_support_type() != PPS_SUPPORT_NOT) {
				chip->normal_cool_down = oplus_convert_pps_current_to_level(chip, val);
			} else {
				chip->normal_cool_down = oplus_convert_current_to_level(chip, val);
			}
			chg_err("set normal_cool_down:%d\n", val);
		}
	}

	return count;
}
DEVICE_ATTR_WO(normal_current_now);

static ssize_t normal_cool_down_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chg_err("val:%d\n", val);
	if (chip->smart_charge_version == 1) {
		chip->smart_normal_cool_down = val;
		chip->normal_cool_down = val;
		chg_err("set normal_cool_down:%d\n", val);
	}
	return count;
}
DEVICE_ATTR_WO(normal_cool_down);

static ssize_t get_quick_mode_time_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int total_time = 0, gain_time = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	total_time = chip->quick_mode_time.tv_sec - chip->start_time;
	gain_time = chip->quick_mode_gain_time_ms / 1000;
	if (gain_time < 0)
		gain_time = 0;
	chg_err("total_time:%d, gain_time:%d, quick_mode_gain_time_ms:%d\n", total_time, gain_time,
		chip->quick_mode_gain_time_ms);
	return sprintf(buf, "%d\n", gain_time);
}
static DEVICE_ATTR_RO(get_quick_mode_time_gain);

static ssize_t get_quick_mode_percent_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int percent = 0, total_time = 0, gain_time = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	total_time = chip->quick_mode_time.tv_sec - chip->start_time;
	gain_time = chip->quick_mode_gain_time_ms / 1000;
	percent = (gain_time * 100) / (total_time + gain_time);
	chg_err("total_time:%d, gain_time:%d, percent:%d\n", total_time, gain_time, percent);
	return sprintf(buf, "%d\n", percent);
}
static DEVICE_ATTR_RO(get_quick_mode_percent_gain);

static ssize_t fast_charge_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	val = oplus_chg_show_vooc_logo_ornot();

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(fast_charge);

static ssize_t mmi_charging_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->mmi_chg);
}

static ssize_t mmi_charging_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
					 size_t count)
{
	int val = 0;
	int ret = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set mmi_chg = [%d].\n", val);
	chip->total_time = 0;
	if (val == 0) {
		if (chip->unwakelock_chg == 1) {
			ret = -EINVAL;
			chg_err("unwakelock testing , this test not allowed.\n");
		} else {
			chip->mmi_chg = 0;
			oplus_chg_turn_off_charging(chip);
			if ((chip->vooc_project) && (oplus_vooc_get_fastchg_started() == true)) {
				chip->mmi_fastchg = 0;
			}
			if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
				oplus_adsp_voocphy_turn_off();
			} else {
				if (!(((chip->pd_svooc == false &&
					chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) ||
					chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) &&
					!oplus_vooc_get_fastchg_started()))
					oplus_vooc_turn_off_fastchg();
			}
			if (oplus_voocphy_get_bidirect_cp_support() && chip->chg_ops->check_chrdet_status()) {
				oplus_voocphy_set_chg_auto_mode(true);
			}
		}
	} else {
		if (chip->unwakelock_chg == 1) {
			ret = -EINVAL;
			chg_err("unwakelock testing , this test not allowed.\n");
		} else {
			chip->mmi_chg = 1;
			if (chip->mmi_fastchg == 0) {
				oplus_chg_clear_chargerid_info();
			}
			chip->mmi_fastchg = 1;
			if (oplus_voocphy_get_bidirect_cp_support()) {
				oplus_voocphy_set_chg_auto_mode(false);
			}
			if (!chip->otg_online)
				oplus_chg_turn_on_charging_in_work();
			if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
				oplus_adsp_voocphy_turn_on();
			}
		}
	}

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_RW(mmi_charging_enable);

#ifdef CONFIG_OPLUS_CHARGER_MTK
static ssize_t stop_charging_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->stop_chg);
}

static ssize_t stop_charging_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
					  size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set stop_chg = [%d].\n", val);

	if (val == 0) {
		chip->stop_chg = false;
	} else {
		chip->stop_chg = true;
	}

	return count;
}
static DEVICE_ATTR_RW(stop_charging_enable);
#endif

static ssize_t battery_notify_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->notify_code);
}
static DEVICE_ATTR_RO(battery_notify_code);

int __attribute__((weak)) oplus_chg_get_subcurrent(void)
{
	return 0;
}

static ssize_t sub_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int sub_current = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->dual_charger_support)
		sub_current = oplus_chg_get_subcurrent();

	return sprintf(buf, "%d\n", sub_current);
}
static DEVICE_ATTR_RO(sub_current);

static ssize_t charge_timeout_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->chging_over_time);
}
static DEVICE_ATTR_RO(charge_timeout);

static ssize_t adapter_fw_update_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_vooc_get_adapter_update_status());
}
static DEVICE_ATTR_RO(adapter_fw_update);

static ssize_t batt_cb_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_gauge_get_battery_cb_status());
}
static DEVICE_ATTR_RO(batt_cb_status);

int __attribute__((weak)) oplus_get_chg_i2c_err(void)
{
	return 0;
}

void __attribute__((weak)) oplus_clear_chg_i2c_err(void)
{
	return;
}

static ssize_t chg_i2c_err_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_get_chg_i2c_err());
}

static ssize_t chg_i2c_err_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	oplus_clear_chg_i2c_err();

	return count;
}
static DEVICE_ATTR_RW(chg_i2c_err);

#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
static ssize_t ship_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->enable_shipmode);
}

static ssize_t ship_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chip->enable_shipmode = val;
	oplus_gauge_update_soc_smooth_parameter();

	return count;
}
static DEVICE_ATTR_RW(ship_mode);
#endif /*CONFIG_OPLUS_SHIP_MODE_SUPPORT*/

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
static ssize_t short_c_limit_chg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", (int)chip->short_c_batt.limit_chg);
}

static ssize_t short_c_limit_chg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	printk(KERN_ERR "[OPLUS_CHG] [short_c_bat] set limit chg[%d]\n", !!val);
	chip->short_c_batt.limit_chg = !!val;
	/* for userspace logic */
	if (!!val == 0) {
		chip->short_c_batt.is_switch_on = 0;
	}

	return count;
}
static DEVICE_ATTR_RW(short_c_limit_chg);

static ssize_t short_c_limit_rechg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", (int)chip->short_c_batt.limit_rechg);
}

static ssize_t short_c_limit_rechg_store(struct device *dev, struct device_attribute *attr, const char *buf,
					 size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	printk(KERN_ERR "[OPLUS_CHG] [short_c_bat] set limit rechg[%d]\n", !!val);
	chip->short_c_batt.limit_rechg = !!val;

	return count;
}
static DEVICE_ATTR_RW(short_c_limit_rechg);

static ssize_t charge_term_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->limits.iterm_ma);
}
static DEVICE_ATTR_RO(charge_term_current);

static ssize_t input_current_settled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	val = 2000;
	if (chip && chip->chg_ops->get_dyna_aicl_result) {
		val = chip->chg_ops->get_dyna_aicl_result();
	}

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(input_current_settled);
#endif /*CONFIG_OPLUS_SHORT_USERSPACE*/
#endif /*CONFIG_OPLUS_SHORT_C_BATT_CHECK*/

#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
static ssize_t short_c_hw_feature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.is_feature_hw_on);
}

static ssize_t short_c_hw_feature_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	printk(KERN_ERR "[OPLUS_CHG] [short_c_hw_check]: set is_feature_hw_on [%d]\n", val);
	chip->short_c_batt.is_feature_hw_on = val;

	return count;
}
static DEVICE_ATTR_RW(short_c_hw_feature);

static ssize_t short_c_hw_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.shortc_gpio_status);
}
static DEVICE_ATTR_RO(short_c_hw_status);
#endif /*CONFIG_OPLUS_SHORT_HW_CHECK*/

#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
static ssize_t short_ic_otp_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.ic_short_otp_st);
}
static DEVICE_ATTR_RO(short_ic_otp_status);

static ssize_t short_ic_volt_thresh_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.ic_volt_threshold);
}

static ssize_t short_ic_volt_thresh_store(struct device *dev, struct device_attribute *attr, const char *buf,
					  size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->short_c_batt.ic_volt_threshold = val;
	oplus_short_ic_set_volt_threshold(chip);

	return count;
}
static DEVICE_ATTR_RW(short_ic_volt_thresh);

static ssize_t short_ic_otp_value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_short_ic_get_otp_error_value(chip));
}
static DEVICE_ATTR_RO(short_ic_otp_value);
#endif /*CONFIG_OPLUS_SHORT_IC_CHECK*/

static ssize_t voocchg_ing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval = {
		0,
	};

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	val = oplus_vooc_get_fastchg_ing();
#ifndef WPC_NEW_INTERFACE
	if (!val && chip->wireless_support) {
		val = oplus_wpc_get_fast_charging();
	}
#else
	if (!val && chip->wireless_support) {
		val = oplus_wpc_get_status();
	}
#endif

	if (is_wls_ocm_available(chip) && !val) {
		oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_FASTCHG_STATUS, &pval);
		val = pval.intval;
	}

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(voocchg_ing);

static ssize_t ppschg_ing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	if (oplus_is_ufcs_charging())
		val = oplus_ufcs_get_protocol_status();
	else
		val = oplus_is_pps_charging();

	if (val == 0
		&& oplus_quirks_keep_connect_status() == 1
		&& oplus_voocphy_get_fastchg_start() == 0) {
		val = oplus_pps_get_last_charging_status();
	}
	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(ppschg_ing);

static ssize_t ppschg_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	if (oplus_is_ufcs_charging())
		val = oplus_ufcs_get_power();
	else
		val = oplus_pps_show_power();

	if (val == OPLUS_PPS_POWER_CLR
		&& oplus_quirks_keep_connect_status() == 1
		&& oplus_voocphy_get_fastchg_start() == 0)
		val = oplus_pps_get_last_power();

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(ppschg_power);

static ssize_t screen_off_by_batt_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	val = chip->screen_off_control_by_batt_temp;

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(screen_off_by_batt_temp);

static ssize_t bcc_exception_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	chg_err("%s\n", buf);
	oplus_chg_bcc_err(buf);

	return count;
}
static DEVICE_ATTR_WO(bcc_exception);

int __attribute__((weak)) oplus_gauge_get_bcc_parameters(char *buf)
{
	return 0;
}

int __attribute__((weak)) oplus_gauge_set_bcc_parameters(const char *buf)
{
	return 0;
}

static ssize_t bcc_parms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	ssize_t len = 0;
	struct oplus_chg_chip *chip = NULL;
        int type = oplus_chg_get_fast_chg_type();

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_vooc_get_reply_bits() == 7
                && oplus_chg_get_voocphy_support() == NO_VOOCPHY
                && (type == CHARGER_SUBTYPE_FASTCHG_SVOOC || type >= OPLUS_SVOOC_ID_MIN)) {
		val = oplus_gauge_get_prev_bcc_parameters(buf);
	} else {
		val = oplus_gauge_get_bcc_parameters(buf);
	}
	len = strlen(buf);
	chg_err("len: %d\n", len);

	return len;
}

static ssize_t bcc_parms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	ret = oplus_gauge_set_bcc_parameters(buf);
	if (ret < 0) {
		chg_err("error\n");
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_RW(bcc_parms);

static ssize_t bcc_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->bcc_current);
}

static ssize_t bcc_current_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0, ret = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ret = oplus_smart_charge_by_bcc(chip, val);
	if (ret < 0) {
		chg_err("error\n");
		return -EINVAL;
	}
	if (is_wls_ocm_available(chip)) {
		pval.intval = val;
		oplus_chg_mod_set_property(chip->wls_ocm,
			OPLUS_CHG_PROP_BCC_CURRENT, &pval);
	}

	mutex_lock(&chip->bcc_curr_done_mutex);
	chip->bcc_curr_done = BCC_CURR_DONE_REQUEST;
	chg_err("bcc_curr_done:%d\n", chip->bcc_curr_done);
	mutex_unlock(&chip->bcc_curr_done_mutex);

	if (oplus_chg_get_voocphy_support() != NO_VOOCPHY) {
		oplus_chg_check_bcc_curr_done();
	}
	return count;
}
static DEVICE_ATTR_RW(bcc_current);

extern u8 soc_store[4];
extern u8 night_count;
static ssize_t soc_ajust_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->soc_ajust);
}

static ssize_t soc_ajust_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val == 0) {
		chip->soc_ajust = 0;
		night_count = 0;
		chip->modify_soc = 0;
	} else {
		chip->soc_ajust = 1;
		soc_store[0] = chip->soc;
		chg_err("[soc_ajust_feature]:set soc_ajust_switch,soc_store0 = [%d].\n", soc_store[0]);
		set_soc_feature();
	}
	chg_err("[soc_ajust_feature]:set soc_ajust_switch = [%d] soc = [%d].\n", val, chip->soc);

	return count;
}
static DEVICE_ATTR_RW(soc_ajust);

static ssize_t parallel_chg_mos_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_switching_get_hw_enable() == MOS_OPEN ||
	    chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS8 ||
	    chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS9) {
		chg_err("mos: %d, test next time!\n", oplus_switching_get_hw_enable());
		return 0;
	}
	if (!chip->mos_test_result) {
		if (!chip->mos_test_started)
			schedule_delayed_work(&chip->parallel_chg_mos_test_work, 0);
	} else {
		chg_err("mos test success, use last result!\n");
	}

	return sprintf(buf, "%d\n", chip->mos_test_result);
}
static DEVICE_ATTR_RO(parallel_chg_mos_test);

static ssize_t parallel_chg_mos_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int val;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	val = oplus_switching_get_hw_enable();
	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(parallel_chg_mos_status);

static ssize_t aging_ffc_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int ffc1_voltage_offset = 0;
	int ffc2_voltage_offset = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_chg_get_aging_ffc_offset(chip, &ffc1_voltage_offset, &ffc2_voltage_offset);

	return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n", chip->aging_ffc_version, chip->vbatt_num,
		       oplus_switching_support_parallel_chg(), chip->debug_batt_cc, chip->batt_cc,
		       chip->limits.default_ffc1_normal_vfloat_sw_limit + ffc1_voltage_offset,
		       chip->limits.default_ffc1_warm_vfloat_sw_limit + ffc1_voltage_offset,
		       chip->limits.default_ffc2_normal_vfloat_sw_limit + ffc2_voltage_offset,
		       chip->limits.default_ffc2_warm_vfloat_sw_limit + ffc2_voltage_offset);
}

static ssize_t aging_ffc_data_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->debug_batt_cc = val;

	return count;
}
static DEVICE_ATTR_RW(aging_ffc_data);

static ssize_t battery_charging_state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->charging_state);
}
static DEVICE_ATTR_RO(battery_charging_state);

static ssize_t bms_heat_temp_compensation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->bms_heat_temp_compensation);
}

static ssize_t bms_heat_temp_compensation_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->bms_heat_temp_compensation = val;

	return count;
}
static DEVICE_ATTR_RW(bms_heat_temp_compensation);

static struct device_attribute *oplus_battery_attributes[] = {
	&dev_attr_authenticate,
	&dev_attr_battery_cc,
	&dev_attr_battery_fcc,
	&dev_attr_battery_rm,
	&dev_attr_battery_soh,
	&dev_attr_soh_report,
	&dev_attr_cc_report,
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	&dev_attr_call_mode,
#endif
	&dev_attr_charge_technology,
#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
	&dev_attr_chip_soc,
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	&dev_attr_cool_down,
#endif
	&dev_attr_em_mode,
	&dev_attr_fast_charge,
	&dev_attr_mmi_charging_enable,
#ifdef CONFIG_OPLUS_CHARGER_MTK
	&dev_attr_stop_charging_enable,
#endif
	&dev_attr_battery_notify_code,
	&dev_attr_sub_current,
	&dev_attr_charge_timeout,
	&dev_attr_adapter_fw_update,
	&dev_attr_batt_cb_status,
	&dev_attr_chg_i2c_err,
#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
	&dev_attr_ship_mode,
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	&dev_attr_short_c_limit_chg,
	&dev_attr_short_c_limit_rechg,
	&dev_attr_charge_term_current,
	&dev_attr_input_current_settled,
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	&dev_attr_short_c_hw_feature,
	&dev_attr_short_c_hw_status,
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	&dev_attr_short_ic_otp_status,
	&dev_attr_short_ic_volt_thresh,
	&dev_attr_short_ic_otp_value,
#endif
	&dev_attr_voocchg_ing,
	&dev_attr_ppschg_ing,
	&dev_attr_ppschg_power,
	&dev_attr_screen_off_by_batt_temp,
	&dev_attr_bcc_parms,
	&dev_attr_bcc_current,
	&dev_attr_bcc_exception,
	&dev_attr_soc_ajust,
	&dev_attr_normal_cool_down,
	&dev_attr_normal_current_now,
	&dev_attr_get_quick_mode_time_gain,
	&dev_attr_get_quick_mode_percent_gain,
	&dev_attr_parallel_chg_mos_test,
	&dev_attr_parallel_chg_mos_status,
	&dev_attr_design_capacity,
	&dev_attr_smartchg_soh_support,
	&dev_attr_aging_ffc_data,
	&dev_attr_battery_charging_state,
	&dev_attr_bms_heat_temp_compensation,
	NULL
};

/**********************************************************************
* wireless device nodes
**********************************************************************/
static ssize_t tx_voltage_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(tx_voltage_now);

static ssize_t tx_current_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(tx_current_now);

static ssize_t cp_voltage_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(cp_voltage_now);

static ssize_t cp_current_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(cp_current_now);

static ssize_t wireless_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(wireless_mode);

static ssize_t wireless_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(wireless_type);

static ssize_t cep_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(cep_info);

int __attribute__((weak)) oplus_wpc_get_real_type(void)
{
	return 0;
}
static ssize_t real_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int real_type = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval = {
		0,
	};

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	real_type = oplus_wpc_get_real_type();

	if (is_wls_ocm_available(chip)) {
		oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_REAL_TYPE, &pval);
		real_type = pval.intval;
	}

	return sprintf(buf, "%d\n", real_type);
}
static DEVICE_ATTR_RO(real_type);

#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
ssize_t __attribute__((weak))
oplus_chg_wls_upgrade_fw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
ssize_t __attribute__((weak))
oplus_chg_wls_upgrade_fw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}
static ssize_t upgrade_firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_wls_ocm_available(chip))
		return oplus_chg_wls_upgrade_fw_show(&chip->wls_ocm->dev, attr, buf);
	return 0;
}

static ssize_t upgrade_firmware_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_wls_ocm_available(chip))
		count = oplus_chg_wls_upgrade_fw_store(&chip->wls_ocm->dev, attr, buf, count);

	return count;
}
static DEVICE_ATTR_RW(upgrade_firmware);
#endif /*OPLUS_CHG_ADB_ROOT_ENABLE*/

static ssize_t status_keep_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->wls_status_keep);
}

static ssize_t status_keep_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set wls_status_keep=%d\n", val);
	if (val == WLS_SK_BY_HAL && chip->wls_status_keep == WLS_SK_NULL) {
		val = WLS_SK_NULL;
		chg_err("force to set wls_status_keep=%d\n", val);
	}
	WRITE_ONCE(chip->wls_status_keep, val);
	if (chip->wls_status_keep == 0)
		power_supply_changed(chip->batt_psy);

	return count;
}
static DEVICE_ATTR_RW(status_keep);

int __attribute__((weak)) oplus_wpc_get_max_wireless_power(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_wls_get_max_wireless_power(struct device *dev)
{
	return 0;
}

static ssize_t max_w_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int max_wls_power = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	max_wls_power = oplus_wpc_get_max_wireless_power();

	if (is_wls_ocm_available(chip))
		max_wls_power = oplus_chg_wls_get_max_wireless_power(&chip->wls_ocm->dev);

	return sprintf(buf, "%d\n", max_wls_power);
}
static DEVICE_ATTR_RO(max_w_power);

static struct device_attribute *oplus_wireless_attributes[] = {
	&dev_attr_tx_voltage_now,
	&dev_attr_tx_current_now,
	&dev_attr_cp_voltage_now,
	&dev_attr_cp_current_now,
	&dev_attr_wireless_mode,
	&dev_attr_wireless_type,
	&dev_attr_cep_info,
	&dev_attr_real_type,
#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
	&dev_attr_upgrade_firmware,
#endif
	&dev_attr_status_keep,
	&dev_attr_max_w_power,
	NULL
};

/**********************************************************************
* common device nodes
**********************************************************************/
#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
ssize_t __attribute__((weak))
oplus_chg_comm_charge_parameter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
ssize_t __attribute__((weak))
oplus_chg_comm_charge_parameter_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}
static ssize_t charge_parameter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		return oplus_chg_comm_charge_parameter_show(&chip->comm_ocm->dev, attr, buf);
	return 0;
}

static ssize_t charge_parameter_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		count = oplus_chg_comm_charge_parameter_store(&chip->comm_ocm->dev, attr, buf, count);

	return count;
}
static DEVICE_ATTR_RW(charge_parameter);
#endif /*OPLUS_CHG_ADB_ROOT_ENABLE*/

ssize_t __attribute__((weak)) oplus_chg_comm_send_mutual_cmd(struct oplus_chg_mod *comm_ocm, char *buf)
{
	return -EINVAL;
}
ssize_t __attribute__((weak))
oplus_chg_comm_response_mutual_cmd(struct oplus_chg_mod *comm_ocm, const char *buf, size_t count)
{
	return -EINVAL;
}
ssize_t __attribute__((weak)) oplus_chg_send_mutual_cmd(char *buf)
{
	return -EINVAL;
}
ssize_t __attribute__((weak)) oplus_chg_response_mutual_cmd(const char *buf, size_t count)
{
	return -EINVAL;
}
static ssize_t mutual_cmd_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		ret = oplus_chg_comm_send_mutual_cmd(chip->comm_ocm, buf);
	else
		ret = oplus_chg_send_mutual_cmd(buf);

	return ret;
}

static ssize_t mutual_cmd_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		oplus_chg_comm_response_mutual_cmd(chip->comm_ocm, buf, count);
	else
		oplus_chg_response_mutual_cmd(buf, count);

	return count;
}
static DEVICE_ATTR_RW(mutual_cmd);

int __attribute__((weak)) oplus_chg_track_set_hidl_info(const char *buf, size_t count)
{
	return 0;
}

static ssize_t track_hidl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_chg_track_set_hidl_info(buf, count);

	return count;
}
static DEVICE_ATTR_WO(track_hidl);

static ssize_t boot_completed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->boot_completed);
}
static DEVICE_ATTR_RO(boot_completed);

static struct device_attribute *oplus_common_attributes[] = {
#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
	&dev_attr_charge_parameter,
#endif
	&dev_attr_mutual_cmd,
	&dev_attr_track_hidl,
	&dev_attr_boot_completed,
	NULL
};
#ifdef OPLUS_FEATURE_CHG_BASIC
void __attribute__((weak)) oplus_pps_get_adapter_status(struct oplus_chg_chip *chip)
{
	return;
}
void __attribute__((weak)) oplus_get_pps_parameters_from_adsp(void)
{
	return;
}
int __attribute__((weak)) oplus_pps_get_authenticate(void)
{
	return 0;
}
#endif

/**********************************************************************
* ac/usb/battery/wireless/common directory nodes create
**********************************************************************/
static int oplus_ac_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "ac");
	if (status < 0) {
		chg_err("alloc_chrdev_region ac fail!\n");
		return -ENOMEM;
	}
	oplus_ac_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "ac");
	oplus_ac_dir->devt = devt;
	dev_set_drvdata(oplus_ac_dir, chip);

	attrs = oplus_ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_ac_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_ac_dir->class, oplus_ac_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_ac_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_ac_dir, attr);
	device_destroy(oplus_ac_dir->class, oplus_ac_dir->devt);
	unregister_chrdev_region(oplus_ac_dir->devt, 1);
}

static int oplus_usb_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "usb");
	if (status < 0) {
		chg_err("alloc_chrdev_region usb fail!\n");
		return -ENOMEM;
	}
	oplus_usb_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "usb");
	oplus_usb_dir->devt = devt;
	dev_set_drvdata(oplus_usb_dir, chip);

	attrs = oplus_usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_usb_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_usb_dir->class, oplus_usb_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_usb_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_usb_dir, attr);
	device_destroy(oplus_usb_dir->class, oplus_usb_dir->devt);
	unregister_chrdev_region(oplus_usb_dir->devt, 1);
}

static int oplus_battery_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "battery");
	if (status < 0) {
		chg_err("alloc_chrdev_region battery fail!\n");
		return -ENOMEM;
	}

	oplus_battery_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "battery");
	oplus_battery_dir->devt = devt;
	dev_set_drvdata(oplus_battery_dir, chip);

	attrs = oplus_battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_battery_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_battery_dir->class, oplus_battery_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_battery_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_battery_dir, attr);
	device_destroy(oplus_battery_dir->class, oplus_battery_dir->devt);
	unregister_chrdev_region(oplus_battery_dir->devt, 1);
}

static int oplus_wireless_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "wireless");
	if (status < 0) {
		chg_err("alloc_chrdev_region wireless fail!\n");
		return -ENOMEM;
	}

	oplus_wireless_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "wireless");
	oplus_wireless_dir->devt = devt;
	dev_set_drvdata(oplus_wireless_dir, chip);

	attrs = oplus_wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_wireless_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_wireless_dir->class, oplus_wireless_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_wireless_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_wireless_dir, attr);
	device_destroy(oplus_wireless_dir->class, oplus_wireless_dir->devt);
	unregister_chrdev_region(oplus_wireless_dir->devt, 1);
}

static int oplus_common_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "common");
	if (status < 0) {
		chg_err("alloc_chrdev_region common fail!\n");
		return -ENOMEM;
	}

	oplus_common_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "common");
	oplus_common_dir->devt = devt;
	dev_set_drvdata(oplus_common_dir, chip);

	attrs = oplus_common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_common_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_common_dir->class, oplus_common_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_common_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_common_dir, attr);
	device_destroy(oplus_common_dir->class, oplus_common_dir->devt);
	unregister_chrdev_region(oplus_common_dir->devt, 1);
}

/**********************************************************************
* configfs init APIs
**********************************************************************/
int oplus_ac_node_add(struct device_attribute **ac_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_ac_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_ac_dir->class, oplus_ac_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_ac_node_delete(struct device_attribute **ac_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_ac_dir, attr);
}

int oplus_usb_node_add(struct device_attribute **usb_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_usb_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_usb_dir->class, oplus_usb_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_usb_node_delete(struct device_attribute **usb_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_usb_dir, attr);
}

int oplus_battery_node_add(struct device_attribute **battery_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_battery_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_battery_dir->class, oplus_battery_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_battery_node_delete(struct device_attribute **battery_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_battery_dir, attr);
}

int oplus_wireless_node_add(struct device_attribute **wireless_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_wireless_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_wireless_dir->class, oplus_wireless_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_wireless_node_delete(struct device_attribute **wireless_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_wireless_dir, attr);
}

int oplus_common_node_add(struct device_attribute **common_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_common_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_common_dir->class, oplus_common_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_common_node_delete(struct device_attribute **common_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_common_dir, attr);
}

int oplus_chg_configfs_init(struct oplus_chg_chip *chip)
{
	int status = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_chg_class = class_create(THIS_MODULE, "oplus_chg");
	if (IS_ERR(oplus_chg_class)) {
		chg_err("oplus_chg_configfs_init fail!\n");
		return PTR_ERR(oplus_chg_class);
	}

	status = oplus_ac_dir_create(chip);
	if (status < 0)
		chg_err("oplus_ac_dir_create fail!\n");

	status = oplus_usb_dir_create(chip);
	if (status < 0)
		chg_err("oplus_usb_dir_create fail!\n");

	status = oplus_battery_dir_create(chip);
	if (status < 0)
		chg_err("oplus_battery_dir_create fail!\n");

	status = oplus_wireless_dir_create(chip);
	if (status < 0)
		chg_err("oplus_wireless_dir_create fail!\n");

	status = oplus_common_dir_create(chip);
	if (status < 0)
		chg_err("oplus_common_dir_create fail!\n");

	return 0;
}
EXPORT_SYMBOL(oplus_chg_configfs_init);

int oplus_chg_configfs_exit(void)
{
	oplus_common_dir_destroy();
	oplus_wireless_dir_destroy();
	oplus_battery_dir_destroy();
	oplus_usb_dir_destroy();
	oplus_ac_dir_destroy();

	if (!IS_ERR(oplus_chg_class))
		class_destroy(oplus_chg_class);
	return 0;
}
EXPORT_SYMBOL(oplus_chg_configfs_exit);
