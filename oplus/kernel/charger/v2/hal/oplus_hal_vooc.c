// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[HAL_VOOC]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_chg_vooc.h>
#include <oplus_hal_vooc.h>

static enum oplus_chg_vooc_switch_mode g_dpdm_switch_mode = VOOC_SWITCH_MODE_NORMAL;

static const char * const oplus_chg_switch_mode_text[] = {
	[VOOC_SWITCH_MODE_NORMAL] = "normal",
	[VOOC_SWITCH_MODE_VOOC] = "vooc",
	[VOOC_SWITCH_MODE_HEADPHONE] = "headphone",
};

int oplus_vooc_set_clock_active(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_SET_CLOCK_ACTIVE);
	if (rc < 0) {
		chg_err("set clock active error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_vooc_set_clock_sleep(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_SET_CLOCK_SLEEP);
	if (rc < 0) {
		chg_err("set clock sleep error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_vooc_set_reset_active(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_RESET_ACTIVE);
	if (rc < 0) {
		chg_err("set reset active error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_vooc_set_reset_sleep(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_RESET_SLEEP);
	if (rc < 0) {
		chg_err("set reset sleep error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

__maybe_unused static int oplus_vooc_fw_update(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_FW_UPGRADE);
	if (rc < 0) {
		chg_err("firmware upgrade error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_vooc_fw_check_then_recover(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return FW_ERROR_DATA_MODE;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER);
	if (rc != FW_CHECK_MODE) {
		chg_err("fw_check_then_recover error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

int oplus_vooc_fw_check_then_recover_fix(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return FW_ERROR_DATA_MODE;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX);
	if (rc != FW_CHECK_MODE) {
		chg_err("fw_check_then_recover_fix error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static void
oplus_vooc_set_switch_mode(struct oplus_chg_ic_dev *vooc_ic,
			   enum oplus_chg_vooc_switch_mode switch_mode)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE,
			       switch_mode);
	if (rc < 0)
		chg_err("switch to %s mode error, rc=%d\n",
			oplus_chg_switch_mode_text[switch_mode], rc);
	g_dpdm_switch_mode = switch_mode;
}

void oplus_vooc_eint_register(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_EINT_REGISTER);
	if (rc < 0)
		chg_err("eint register error, rc=%d\n", rc);
}

void oplus_vooc_eint_unregister(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_EINT_UNREGISTER);
	if (rc < 0)
		chg_err("eint unregister error, rc=%d\n", rc);
}

void oplus_vooc_set_data_active(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_SET_DATA_ACTIVE);
	if (rc < 0)
		chg_err("set data active error, rc=%d\n", rc);
}

void oplus_vooc_set_data_sleep(struct oplus_chg_ic_dev *vooc_ic)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_SET_DATA_SLEEP);
	if (rc < 0)
		chg_err("set data sleep error, rc=%d\n", rc);
}


__maybe_unused static int oplus_vooc_get_gpio_ap_data(struct oplus_chg_ic_dev *vooc_ic)
{
	int val;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_GET_DATA_GPIO_VAL, &val);
	if (rc < 0) {
		chg_err("get data gpio val error, rc=%d\n", rc);
		return 0;
	}

	return val;
}

int oplus_vooc_read_ap_data(struct oplus_chg_ic_dev *vooc_ic)
{
	int data;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_READ_DATA, &data);
	if (rc < 0) {
		chg_err("get data bit error, rc=%d\n", rc);
		return rc;
	}

	return data;
}

void oplus_vooc_reply_data(struct oplus_chg_ic_dev *vooc_ic, int ret_info,
			   int device_type, int data_width, int curr_ma)
{
	int data;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	data = (ret_info << 1) | (device_type & BIT(0));

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_REPLY_DATA, data,
			       data_width, curr_ma);
	if (rc < 0)
		chg_err("reply data error, rc=%d\n", rc);
}

void oplus_vooc_reply_data_no_type(struct oplus_chg_ic_dev *vooc_ic,
				   int ret_info, int data_width, int curr_ma)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_REPLY_DATA, ret_info,
			       data_width, curr_ma);
	if (rc < 0)
		chg_err("reply data error, rc=%d\n", rc);
}

enum oplus_chg_vooc_switch_mode
oplus_chg_vooc_get_switch_mode(struct oplus_chg_ic_dev *vooc_ic)
{
	int mode;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return VOOC_SWITCH_MODE_NORMAL;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_GET_SWITCH_MODE,
			       &mode);
	if (rc < 0) {
		chg_err("get switch mode error, rc=%d\n", rc);
		mode = VOOC_SWITCH_MODE_NORMAL;
	}

	return (enum oplus_chg_vooc_switch_mode)mode;
}

__maybe_unused static void
set_vooc_chargerid_switch_val(struct oplus_chg_ic_dev *vooc_ic, int value)
{
	int level = 0;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	if (value == 1)
		level = 1;
	else if (value == 0)
		level = 0;
	else
		return;
}

void switch_fast_chg(struct oplus_chg_ic_dev *vooc_ic)
{
	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	oplus_vooc_set_clock_sleep(vooc_ic);
	oplus_vooc_set_reset_active(vooc_ic);
	oplus_vooc_set_switch_mode(vooc_ic, VOOC_SWITCH_MODE_VOOC);
}

void switch_normal_chg(struct oplus_chg_ic_dev *vooc_ic)
{
	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return;
	}

	oplus_vooc_set_switch_mode(vooc_ic, VOOC_SWITCH_MODE_NORMAL);
}

__maybe_unused static int
oplus_vooc_get_reset_gpio_val(struct oplus_chg_ic_dev *vooc_ic)
{
	int val;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_GET_RESET_GPIO_VAL,
			       &val);
	if (rc < 0) {
		chg_err("get reset gpio val error, rc=%d\n", rc);
		return 0;
	}

	return val;
}

__maybe_unused static int
oplus_vooc_get_ap_clk_gpio_val(struct oplus_chg_ic_dev *vooc_ic)
{
	int val;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_GET_CLOCK_GPIO_VAL,
			       &val);
	if (rc < 0) {
		chg_err("get clk gpio val error, rc=%d\n", rc);
		return 0;
	}

	return val;
}

__maybe_unused static int
oplus_vooc_get_switch_gpio_val(struct oplus_chg_ic_dev *vooc_ic)
{
	int val;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return 0;
	}

	switch (oplus_chg_vooc_get_switch_mode(vooc_ic)) {
	case VOOC_SWITCH_MODE_NORMAL:
		val = 0;
		break;
	case VOOC_SWITCH_MODE_VOOC:
		val = 1;
		break;
	case VOOC_SWITCH_MODE_HEADPHONE:
		val = 1;
		break;
	}

	return val;
}

__maybe_unused static int
oplus_vooc_get_fw_verion_from_ic(struct oplus_chg_ic_dev *vooc_ic)
{
	u32 version;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_GET_FW_VERSION,
			       &version);
	if (rc < 0) {
		chg_err("get firmware version error, rc=%d\n", rc);
		return 0;
	}

	return (int)version;
}

__maybe_unused static void oplus_vooc_update_temperature_soc(void)
{
}

bool oplus_vooc_asic_fw_status(struct oplus_chg_ic_dev *vooc_ic)
{
	bool pass;
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return false;
	}

	rc = oplus_chg_ic_func(vooc_ic, OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS, &pass);
	if (rc == -ENOTSUPP) {
		return true;
	} else if (rc < 0) {
		chg_err("get firmware status error, rc=%d\n", rc);
		return false;
	}

	return pass;
}

int oplus_hal_vooc_init(struct oplus_chg_ic_dev *vooc_ic)
{
	oplus_vooc_set_clock_sleep(vooc_ic);

	return 0;
}

int set_chg_auto_mode(struct oplus_chg_ic_dev *vooc_ic, bool enable)
{
	int rc;

	if (vooc_ic == NULL) {
		chg_err("vooc_ic is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(vooc_ic,
			       OPLUS_IC_FUNC_VOOCPHY_SET_CHG_AUTO_MODE,
			       enable);
	if (rc == -ENOTSUPP)
		return 0;
	else if (rc < 0)
		chg_info("failed to %s chg auto mode, rc=%d\n",
			 enable ? "enable" : "disable", rc);

	return rc;
}
