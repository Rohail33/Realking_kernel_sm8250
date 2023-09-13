// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[WLS]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/power_supply.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#include <linux/oem/boot_mode.h>
#include <linux/oem/oplus_chg_voter.h>
#else
#include <oplus_chg_core.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/boot_mode.h>
#endif
#include <oplus_chg_voter.h>
#endif
#include "oplus_chg_module.h"
#include "hal/oplus_chg_ic.h"
#include "oplus_chg_wls.h"
#include "hal/wls_chg_intf.h"
#include "oplus_gauge.h"
#include "oplus_charger.h"

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
#include "oplus_chg_wls_cfg.h"
#endif

extern struct oplus_chg_chip *g_oplus_chip;
extern bool oplus_get_wired_otg_online(void);

struct oplus_chg_wls_state_handler {
	int (*enter_state)(struct oplus_chg_wls *wls_dev);
	int (*handle_state)(struct oplus_chg_wls *wls_dev);
	int (*exit_state)(struct oplus_chg_wls *wls_dev);
};

#define OPLUS_CHG_WLS_BREAK_DETECT_DELAY 6000
#define OPLUS_CHG_WLS_START_DETECT_DELAY 3000
#define OPLUS_WLS_BCC_MAX_CURR_INIT 5000
#define OPLUS_WLS_BCC_MIN_CURR_INIT 4000
#define OPLUS_WLS_BCC_STOP_CURR_INIT 1000

#define BCC_TO_ICL 100
#define OPLUS_WLS_BCC_UPDATE_TIME	500
#define OPLUS_WLS_BCC_UPDATE_INTERVAL	round_jiffies_relative(msecs_to_jiffies(OPLUS_WLS_BCC_UPDATE_TIME))

#ifdef WLS_QI_DEBUG
static int wls_dbg_fcc_ma = 0;
module_param(wls_dbg_fcc_ma, int, 0644);
MODULE_PARM_DESC(wls_dbg_fcc_ma, "debug wls fcc ma");

static int wls_dbg_vout_mv = 0;
module_param(wls_dbg_vout_mv, int, 0644);
MODULE_PARM_DESC(wls_dbg_vout_mv, "debug wls vout mv");
#endif

static ATOMIC_NOTIFIER_HEAD(wls_ocm_notifier);
static bool adsp_started;
static bool online_pending;

static struct wls_base_type wls_base_table[] = {
	{ 0x00, 30000 }, { 0x01, 40000 }, { 0x02, 50000 }, { 0x03, 50000 }, { 0x04, 50000 },
	{ 0x05, 50000 }, { 0x06, 50000 }, { 0x07, 50000 }, { 0x08, 50000 }, { 0x09, 50000 },
	{ 0x0a, 100000 }, { 0x0b, 100000 }, { 0x10, 100000 }, { 0x11, 100000 }, { 0x12, 100000 },
	{ 0x13, 100000 }, { 0x1f, 50000 },
};

static u8 oplus_trx_id_table[] = {
	0x02, 0x03, 0x04, 0x05, 0x06
};

/*static int oplus_chg_wls_pwr_table[] = {0, 12000, 12000, 35000, 50000};*/
static struct wls_pwr_table oplus_chg_wls_pwr_table[] = {/*(f2_id, r_power, t_power)*/
	{ 0x00, 12, 15 }, { 0x01, 12, 20 }, { 0x02, 12, 30 }, { 0x03, 35, 50 }, { 0x04, 45, 65 },
	{ 0x05, 50, 75 }, { 0x06, 60, 85 }, { 0x07, 65, 95 }, { 0x08, 75, 105 }, { 0x09, 80, 115 },
	{ 0x0A, 90, 125 }, { 0x0B, 20, 20 }, { 0x0C, 100, 140 }, { 0x0D, 115, 160 }, { 0x0E, 130, 180 },
	{ 0x0F, 145, 200 },
	{ 0x11, 35, 50 }, { 0x12, 35, 50 }, { 0x13, 12, 20 }, { 0x14, 45, 65 }, { 0x15, 12, 20 },
	{ 0x16, 12, 20 }, { 0x17, 12, 30 }, { 0x18, 12, 30 }, { 0x19, 12, 30 }, { 0x1A, 12, 33 },
	{ 0x1B, 12, 33 }, { 0x1C, 12, 44 }, { 0x1D, 12, 44 }, { 0x1E, 12, 44 },
	{ 0x21, 35, 50 }, { 0x22, 12, 44 }, { 0x23, 35, 50 }, { 0x24, 35, 55 }, { 0x25, 35, 55 },
	{ 0x26, 35, 55 }, { 0x27, 35, 55 }, { 0x28, 45, 65 }, { 0x29, 12, 30 }, { 0x2A, 45, 65 },
	{ 0x2B, 45, 66 }, { 0x2C, 45, 67 }, { 0x2D, 45, 67 }, { 0x2E, 45, 67 },
	{ 0x31, 35, 50 }, { 0x32, 90, 120 }, { 0x33, 35, 50 }, { 0x34, 12, 20 }, { 0x35, 45, 65 },
	{ 0x36, 45, 66 }, { 0x37, 50, 88 }, { 0x38, 50, 88 }, { 0x39, 50, 88 }, { 0x3A, 50, 88 },
	{ 0x3B, 75, 100 }, { 0x3C, 75, 100 }, { 0x3D, 75, 100 }, { 0x3E, 75, 100 },
	{ 0x41, 12, 30 }, { 0x42, 12, 30 }, { 0x43, 12, 30 }, { 0x44, 12, 30 }, { 0x45, 12, 30 },
	{ 0x46, 12, 30 }, { 0x47, 90, 120 }, { 0x48, 90, 120 }, { 0x49, 12, 33 }, { 0x4A, 12, 33 },
	{ 0x4B, 50, 80 }, { 0x4C, 50, 80 }, { 0x4D, 50, 80 }, { 0x4E, 50, 80 },
	{ 0x51, 90, 125 },
	{ 0x61, 12, 33 }, { 0x62, 35, 50 }, { 0x63, 45, 65 }, { 0x64, 45, 66 }, { 0x65, 50, 80 },
	{ 0x66, 45, 65 }, { 0x67, 90, 125 }, { 0x68, 90, 125 }, { 0x69, 75, 100 }, { 0x6A, 75, 100 },
	{ 0x6B, 90, 120 }, { 0x6C, 45, 67 }, { 0x6D, 45, 67 }, { 0x6E, 45, 65 },
	{ 0x7F, 30, 0 },
};


static struct wls_pwr_table oplus_chg_wls_tripartite_pwr_table[] = {
	{0x01, 20, 20}, {0x02, 30, 30}, {0x03, 40, 40},  {0x04, 50, 50},
};

static const char * const oplus_chg_wls_rx_state_text[] = {
	[OPLUS_CHG_WLS_RX_STATE_DEFAULT] = "default",
	[OPLUS_CHG_WLS_RX_STATE_BPP] = "bpp",
	[OPLUS_CHG_WLS_RX_STATE_EPP] = "epp",
	[OPLUS_CHG_WLS_RX_STATE_EPP_PLUS] = "epp-plus",
	[OPLUS_CHG_WLS_RX_STATE_FAST] = "fast",
	[OPLUS_CHG_WLS_RX_STATE_FFC] = "ffc",
	[OPLUS_CHG_WLS_RX_STATE_DONE] = "done",
	[OPLUS_CHG_WLS_RX_STATE_QUIET] = "quiet",
	[OPLUS_CHG_WLS_RX_STATE_STOP] = "stop",
	[OPLUS_CHG_WLS_RX_STATE_DEBUG] = "debug",
	[OPLUS_CHG_WLS_RX_STATE_FTM] = "ftm",
	[OPLUS_CHG_WLS_RX_STATE_ERROR] = "error",
};

static const char * const oplus_chg_wls_trx_state_text[] = {
	[OPLUS_CHG_WLS_TRX_STATE_DEFAULT] = "default",
	[OPLUS_CHG_WLS_TRX_STATE_READY] = "ready",
	[OPLUS_CHG_WLS_TRX_STATE_WAIT_PING] = "wait_ping",
	[OPLUS_CHG_WLS_TRX_STATE_TRANSFER] = "transfer",
	[OPLUS_CHG_WLS_TRX_STATE_OFF] = "off",
};

static u8 oplus_chg_wls_disable_fod_parm[WLS_FOD_PARM_LENGTH] = {
	0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f,
	0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f,
};

static struct oplus_chg_wls_dynamic_config default_config = {
	.fcc_step = {
		{},
		{},
		{},
		{},
	},
};

__maybe_unused static bool is_wls_psy_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->wls_psy)
		wls_dev->wls_psy = power_supply_get_by_name("wireless");
	return !!wls_dev->wls_psy;
}

__maybe_unused static bool is_batt_psy_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->batt_psy)
		wls_dev->batt_psy = power_supply_get_by_name("battery");
	return !!wls_dev->batt_psy;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->comm_ocm)
		wls_dev->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!wls_dev->comm_ocm;
}

__maybe_unused static bool oplus_chg_wls_is_usb_present(struct oplus_chg_wls *wls_dev)
{
	return oplus_get_wired_chg_present();
}

__maybe_unused static bool oplus_chg_wls_is_usb_connected(struct oplus_chg_wls *wls_dev)
{
	if (!gpio_is_valid(wls_dev->usb_int_gpio)) {
		pr_err("usb_int_gpio invalid\n");
		return false;
	}

	return !!gpio_get_value(wls_dev->usb_int_gpio);
}

__maybe_unused static bool is_track_ocm_available(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev->track_ocm)
		wls_dev->track_ocm = oplus_chg_mod_get_by_name("track");
	return !!wls_dev->track_ocm;
}

static bool oplus_chg_wls_track_set_prop(
	struct oplus_chg_wls *wls_dev,
	enum oplus_chg_mod_property prop, int val)
{
	union oplus_chg_mod_propval pval = {0};
	int rc;

	if (!is_track_ocm_available(wls_dev))
		return false;

	pval.intval = val;
	rc = oplus_chg_mod_set_property(wls_dev->track_ocm,
				prop, &pval);
	if (rc < 0)
		return false;

	return true;
}

bool oplus_chg_wls_is_otg_mode(struct oplus_chg_wls *wls_dev)
{
	return oplus_get_wired_otg_online();
}

static int oplus_chg_wls_get_base_power_max(u8 id)
{
	int i;
	int pwr = WLS_VOOC_PWR_MAX_MW;

	for (i = 0; i < ARRAY_SIZE(wls_base_table); i++) {
		if (wls_base_table[i].id == id) {
			pwr = wls_base_table[i].power_max_mw;
			return pwr;
		}
	}

	return pwr;
}

static int oplus_chg_wls_get_r_power(struct oplus_chg_wls *wls_dev, u8 f2_data)
{
	int i = 0;
	int r_pwr = WLS_RECEIVE_POWER_DEFAULT;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	for (i = 0; i < ARRAY_SIZE(oplus_chg_wls_pwr_table); i++) {
		if (oplus_chg_wls_pwr_table[i].f2_id == (f2_data & 0x7F)) {
			r_pwr = oplus_chg_wls_pwr_table[i].r_power * 1000;
			break;
		}
	}
	if (wls_status->wls_type == OPLUS_CHG_WLS_PD_65W)
		return WLS_RECEIVE_POWER_PD65W;
	return r_pwr;
}

static int oplus_chg_wls_get_tripartite_r_power(u8 f2_data)
{
	int i = 0;
	int r_pwr = WLS_RECEIVE_POWER_DEFAULT;

	for (i = 0; i < ARRAY_SIZE(oplus_chg_wls_tripartite_pwr_table); i++) {
		if (oplus_chg_wls_tripartite_pwr_table[i].f2_id == (f2_data & 0x7F)) {
			r_pwr = oplus_chg_wls_tripartite_pwr_table[i].r_power * 1000;
			break;
		}
	}
	return r_pwr;
}

#define WLS_BASE_ID_DEFAUTL  0x02
static u8 oplus_chg_wls_get_q_value(struct oplus_chg_wls *wls_dev, u8 id)
{
	int i;

	for (i = 0; i < WLS_BASE_NUM_MAX; i++) {
		if (wls_dev->static_config.fastchg_match_q[i].id == id) {
			return wls_dev->static_config.fastchg_match_q[i].q_value;
		}
	}

	for (i = 0; i < WLS_BASE_NUM_MAX; i++) {
		if (wls_dev->static_config.fastchg_match_q[i].id == WLS_BASE_ID_DEFAUTL) {
			return wls_dev->static_config.fastchg_match_q[i].q_value;
		}
	}

	return 0;
}

static int oplus_chg_wls_get_ibat(struct oplus_chg_wls *wls_dev, int *ibat_ma)
{
	*ibat_ma = oplus_gauge_get_batt_current();
	return 0;
}

static int oplus_chg_wls_get_vbat(struct oplus_chg_wls *wls_dev, int *vbat_mv)
{
	*vbat_mv = oplus_gauge_get_batt_mvolts();
	if (*vbat_mv <= 0)
		return -ENODEV;
	return 0;
}

static int oplus_chg_wls_get_real_soc(struct oplus_chg_wls *wls_dev, int *soc)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return -ENODEV;
	}

	*soc = g_oplus_chip->soc;

	return 0;
}

static int oplus_chg_wls_get_ui_soc(struct oplus_chg_wls *wls_dev, int *ui_soc)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return -ENODEV;
	}

	*ui_soc = min(g_oplus_chip->ui_soc, 100);

	return 0;
}

static int oplus_chg_wls_get_batt_temp(struct oplus_chg_wls *wls_dev, int *temp)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return -ENODEV;
	}

	*temp = g_oplus_chip->temperature;

	return 0;
}

static int oplus_chg_wls_get_batt_num(struct oplus_chg_wls *wls_dev, int *num)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return -ENODEV;
	}

	*num = g_oplus_chip->vbatt_num;

	return 0;
}

static int oplus_chg_wls_get_skin_temp(struct oplus_chg_wls *wls_dev, int *temp)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm ocm not found\n");
		return -ENODEV;
	}
	if(wls_dev->factory_mode){
		*temp = 250;
		return 0;
	}

	rc = oplus_chg_mod_get_property(wls_dev->comm_ocm, OPLUS_CHG_PROP_SKIN_TEMP, &pval);
	if (rc < 0)
		return rc;
	*temp = pval.intval;

	return 0;
}

static int oplus_chg_wls_set_wrx_enable(struct oplus_chg_wls *wls_dev, bool en)
{
	int rc;

	if (IS_ERR_OR_NULL(wls_dev->pinctrl) ||
	    IS_ERR_OR_NULL(wls_dev->wrx_en_active) ||
	    IS_ERR_OR_NULL(wls_dev->wrx_en_sleep)) {
		pr_err("wrx_en pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(wls_dev->pinctrl,
		en ? wls_dev->wrx_en_active : wls_dev->wrx_en_sleep);
	if (rc < 0)
		pr_err("can't %s wrx, rc=%d\n", en ? "enable" : "disable", rc);
	else
		pr_debug("set wrx %s\n", en ? "enable" : "disable");

	pr_err("wrx: set value:%d, gpio_val:%d\n", en, gpio_get_value(wls_dev->wrx_en_gpio));

	return rc;
}

static int oplus_chg_wls_fcc_vote_callback(struct votable *votable, void *data,
				int fcc_ma, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;

	if (fcc_ma < 0)
		return 0;

	if (fcc_ma > dynamic_cfg->fastchg_curr_max_ma)
		fcc_ma = dynamic_cfg->fastchg_curr_max_ma;

#ifdef WLS_QI_DEBUG
	if (wls_dbg_fcc_ma != 0)
		fcc_ma = wls_dbg_fcc_ma;
#endif

	wls_status->fastchg_target_curr_ma = fcc_ma;
	pr_info("set target current to %d\n", wls_status->fastchg_target_curr_ma);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return 0;
}

static int oplus_chg_wls_fastchg_disable_vote_callback(struct votable *votable, void *data,
				int disable, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	wls_status->fastchg_disable = disable;
	pr_info("%s wireless fast charge\n", disable ? "disable" : "enable");

	return 0;
}

static int oplus_chg_wls_wrx_en_vote_callback(struct votable *votable, void *data,
				int en, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	rc = oplus_chg_wls_set_wrx_enable(wls_dev, en);
	return rc;
}

static int oplus_chg_wls_nor_icl_vote_callback(struct votable *votable, void *data,
				int icl_ma, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	if (icl_ma < 0)
		return 0;

	if (step)
		rc = oplus_chg_wls_nor_set_icl_by_step(wls_dev->wls_nor, icl_ma, 200, false);
	else
		rc = oplus_chg_wls_nor_set_icl(wls_dev->wls_nor, icl_ma);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return rc;
}

static int oplus_chg_wls_nor_fcc_vote_callback(struct votable *votable, void *data,
				int fcc_ma, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	if (fcc_ma < 0)
		return 0;

	if (step)
		rc = oplus_chg_wls_nor_set_fcc_by_step(wls_dev->wls_nor, fcc_ma, 100, false);
	else
		rc = oplus_chg_wls_nor_set_fcc(wls_dev->wls_nor, fcc_ma);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return rc;
}

static int oplus_chg_wls_nor_fv_vote_callback(struct votable *votable, void *data,
				int fv_mv, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	if (fv_mv < 0)
		return 0;

	rc = oplus_chg_wls_nor_set_fv(wls_dev->wls_nor, fv_mv);

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

	return rc;
}

static int oplus_chg_wls_nor_out_disable_vote_callback(struct votable *votable,
			void *data, int disable, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	rc = oplus_chg_wls_nor_set_output_enable(wls_dev->wls_nor, !disable);
	if (rc < 0)
		pr_err("can't %s wireless nor out charge\n", disable ? "disable" : "enable");
	else
		pr_err("%s wireless nor out charge\n", disable ? "disable" : "enable");

	return rc;
}

static int oplus_chg_wls_nor_input_disable_vote_callback(struct votable *votable,
			void *data, int disable, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	rc = oplus_chg_wls_nor_set_input_enable(wls_dev->wls_nor, !disable);
	if (rc < 0)
		pr_err("can't %s wireless nor input charge\n", disable ? "disable" : "enable");
	else
		pr_err("%s wireless nor input charge\n", disable ? "disable" : "enable");

	return rc;
}

static int oplus_chg_wls_rx_disable_vote_callback(struct votable *votable, void *data,
				int disable, const char *client, bool step)
{
	struct oplus_chg_wls *wls_dev = data;
	int rc;

	rc = oplus_chg_wls_rx_enable(wls_dev->wls_rx, !disable);
	if (rc < 0)
		pr_err("can't %s wireless charge\n", disable ? "disable" : "enable");
	else
		pr_err("%s wireless charge\n", disable ? "disable" : "enable");

	return rc;
}

static int oplus_chg_wls_set_ext_pwr_enable(struct oplus_chg_wls *wls_dev, bool en)
{
	int rc;

	if (IS_ERR_OR_NULL(wls_dev->pinctrl) ||
	    IS_ERR_OR_NULL(wls_dev->ext_pwr_en_active) ||
	    IS_ERR_OR_NULL(wls_dev->ext_pwr_en_sleep)) {
		pr_err("ext_pwr_en pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(wls_dev->pinctrl,
		en ? wls_dev->ext_pwr_en_active : wls_dev->ext_pwr_en_sleep);
	if (rc < 0)
		pr_err("can't %s ext_pwr, rc=%d\n", en ? "enable" : "disable", rc);
	else
		pr_err("set ext_pwr %s\n", en ? "enable" : "disable");

	pr_err("ext_pwr: set value:%d, gpio_val:%d\n", en, gpio_get_value(wls_dev->ext_pwr_en_gpio));

	return rc;
}

static void oplus_chg_wls_get_third_part_verity_data_work(
				struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, wls_get_third_part_verity_data_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	oplus_chg_common_set_mutual_cmd(wls_dev->comm_ocm,
		CMD_WLS_THIRD_PART_AUTH,
		sizeof(wls_status->vendor_id), &(wls_status->vendor_id));
}

static void oplus_chg_wls_standard_msg_handler(struct oplus_chg_wls *wls_dev,
					       u8 mask, u8 data)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	bool msg_ok = false;
	int pwr_mw;
	u8 q_value;

	pr_info("mask=0x%02x, msg_type=0x%02x\n", mask, rx_msg->msg_type);
	switch (mask) {
	case WLS_RESPONE_EXTERN_CMD:
		if (rx_msg->msg_type == WLS_CMD_GET_EXTERN_CMD) {
			pr_info("tx extern cmd=0x%02x\n", data);
			wls_status->tx_extern_cmd_done = true;
		}
		break;
	case WLS_RESPONE_VENDOR_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_VENDOR_ID) {
			wls_status->vendor_id = data;
			wls_status->aes_verity_data_ok = false;
			if (is_comm_ocm_available(wls_dev) && !wls_status->verify_by_aes)
				schedule_delayed_work(&wls_dev->wls_get_third_part_verity_data_work, 0);
			wls_status->verify_by_aes = true;
			pr_info("verify_by_aes=%d, vendor_id=0x%02x\n",
				wls_status->verify_by_aes, wls_status->vendor_id);
		}
		break;
	case WLS_RESPONE_ADAPTER_TYPE:
		if (rx_msg->msg_type == WLS_CMD_INDENTIFY_ADAPTER) {
			wls_status->adapter_type = data & WLS_ADAPTER_TYPE_MASK;
			wls_status->adapter_id = (data & WLS_ADAPTER_ID_MASK) >> 3;
			pr_err("wkcs: adapter_id = %d\n", wls_status->adapter_id);
			pr_err("wkcs: adapter_type = 0x%02x\n", wls_status->adapter_type);
			pwr_mw = oplus_chg_wls_get_base_power_max(wls_status->adapter_id);
			oplus_vote(wls_dev->fcc_votable, BASE_MAX_VOTER, true, (pwr_mw * 1000 / WLS_RX_VOL_MAX_MV), false);
			if (wls_dev->wls_ocm)
				oplus_chg_mod_changed(wls_dev->wls_ocm);
			if ((wls_status->adapter_type == WLS_ADAPTER_TYPE_VOOC) ||
			    (wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC) ||
			    (wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W)) {
				q_value = oplus_chg_wls_get_q_value(wls_dev, wls_status->adapter_id);
				(void)oplus_chg_wls_rx_send_match_q(wls_dev->wls_rx, q_value);
				msleep(200);
			}
		}
		break;
	case WLS_RESPONE_INTO_FASTCHAGE:
		if (rx_msg->msg_type == WLS_CMD_INTO_FASTCHAGE) {
			wls_status->adapter_power = data;
			if (wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY)
				wls_status->pwr_max_mw = oplus_chg_wls_get_r_power(wls_dev, wls_status->adapter_power);
			else
				wls_status->pwr_max_mw = oplus_chg_wls_get_tripartite_r_power(wls_status->adapter_power);
			if (wls_status->pwr_max_mw > 0) {
				oplus_vote(wls_dev->fcc_votable, RX_MAX_VOTER, true,
				     (wls_status->pwr_max_mw * 1000 / WLS_RX_VOL_MAX_MV),
				     false);
			}
			wls_status->charge_type = WLS_CHARGE_TYPE_FAST;
			if (wls_dev->static_config.fastchg_fod_enable) {
				if(wls_status->fod_parm_for_fastchg)
					(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
						wls_dev->static_config.fastchg_fod_parm, WLS_FOD_PARM_LENGTH);
				else if(wls_dev->static_config.fastchg_12V_fod_enable)
					(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
						wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			} else {
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
			}
			if (!wls_status->verity_started)
				oplus_vote(wls_dev->fcc_votable, VERITY_VOTER, true, dynamic_cfg->verity_curr_max_ma, false);
		}
		break;
	case WLS_RESPONE_INTO_NORMAL_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_NORMAL_MODE) {
			wls_status->quiet_mode = false;
			wls_status->quiet_mode_init = true;
		}
		break;
	case WLS_RESPONE_INTO_QUIET_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_QUIET_MODE) {
			wls_status->quiet_mode = true;
			wls_status->quiet_mode_init = true;
		}
		break;
	case WLS_RESPONE_FAN_SPEED:
		if (rx_msg->msg_type == WLS_CMD_SET_FAN_SPEED) {
			if (data <= FAN_PWM_PULSE_IN_SILENT_MODE_THR)
				wls_status->quiet_mode = true;
			else
				wls_status->quiet_mode = false;
			wls_status->quiet_mode_init = true;
		}
		break;
	case WLS_RESPONE_CEP_TIMEOUT:
		if (rx_msg->msg_type == WLS_CMD_SET_CEP_TIMEOUT) {
			wls_status->cep_timeout_adjusted = true;
		}
		break;
	case WLS_RESPONE_READY_FOR_EPP:
		wls_status->adapter_type = WLS_ADAPTER_TYPE_EPP;
		break;
	case WLS_RESPONE_WORKING_IN_EPP:
		wls_status->epp_working = true;
		break;
	default:
		break;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	switch (mask) {
	case WLS_RESPONE_ADAPTER_TYPE:
		if (rx_msg->msg_type == WLS_CMD_INDENTIFY_ADAPTER)
			msg_ok = true;
		break;
	case WLS_RESPONE_INTO_FASTCHAGE:
		if (rx_msg->msg_type == WLS_CMD_INTO_FASTCHAGE)
			msg_ok = true;
		break;
	case WLS_RESPONE_INTO_NORMAL_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_NORMAL_MODE)
			msg_ok = true;
		break;
	case WLS_RESPONE_INTO_QUIET_MODE:
		if (rx_msg->msg_type == WLS_CMD_SET_QUIET_MODE)
			msg_ok = true;
		break;
	case WLS_RESPONE_CEP_TIMEOUT:
		if (rx_msg->msg_type == WLS_CMD_SET_CEP_TIMEOUT)
			msg_ok = true;
		break;
	case WLS_RESPONE_LED_BRIGHTNESS:
		if (rx_msg->msg_type == WLS_CMD_SET_LED_BRIGHTNESS)
			msg_ok = true;
		break;
	case WLS_RESPONE_FAN_SPEED:
		if (rx_msg->msg_type == WLS_CMD_SET_FAN_SPEED)
			msg_ok = true;
		break;
	case WLS_RESPONE_VENDOR_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_VENDOR_ID)
			msg_ok = true;
		break;
	case WLS_RESPONE_EXTERN_CMD:
		if (rx_msg->msg_type == WLS_CMD_GET_EXTERN_CMD)
			msg_ok = true;
		break;
	default:
		pr_err("unknown msg respone(=%d)\n", mask);
	}
	mutex_unlock(&wls_dev->send_msg_lock);

	if (msg_ok) {
		cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
		complete(&wls_dev->msg_ack);
	}
}

static void oplus_chg_wls_data_msg_handler(struct oplus_chg_wls *wls_dev,
					   u8 mask, u8 data[3])
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	bool msg_ok = false;

	switch (mask) {
	case WLS_RESPONE_ENCRYPT_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA4) {
			wls_status->encrypt_data[0] = data[0];
			wls_status->encrypt_data[1] = data[1];
			wls_status->encrypt_data[2] = data[2];
		}
		break;
	case WLS_RESPONE_ENCRYPT_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA5) {
			wls_status->encrypt_data[3] = data[0];
			wls_status->encrypt_data[4] = data[1];
			wls_status->encrypt_data[5] = data[2];
		}
		break;
	case WLS_RESPONE_ENCRYPT_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA6) {
			wls_status->encrypt_data[6] = data[0];
			wls_status->encrypt_data[7] = data[1];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA1:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA1) {
			wls_status->aes_encrypt_data[0] = data[0];
			wls_status->aes_encrypt_data[1] = data[1];
			wls_status->aes_encrypt_data[2] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA2:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA2) {
			wls_status->aes_encrypt_data[3] = data[0];
			wls_status->aes_encrypt_data[4] = data[1];
			wls_status->aes_encrypt_data[5] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA3:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA3) {
			wls_status->aes_encrypt_data[6] = data[0];
			wls_status->aes_encrypt_data[7] = data[1];
			wls_status->aes_encrypt_data[8] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA4) {
			wls_status->aes_encrypt_data[9] = data[0];
			wls_status->aes_encrypt_data[10] = data[1];
			wls_status->aes_encrypt_data[11] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA5) {
			wls_status->aes_encrypt_data[12] = data[0];
			wls_status->aes_encrypt_data[13] = data[1];
			wls_status->aes_encrypt_data[14] = data[2];
		}
		break;
	case WLS_RESPONE_GET_AES_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA6) {
			wls_status->aes_encrypt_data[15] = data[0];
		}
		break;
	case WLS_RESPONE_PRODUCT_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_PRODUCT_ID) {
			wls_status->tx_product_id_done = true;
			wls_status->product_id = (data[0] << 8) | data[1];
			pr_info("product_id:0x%x, tx_product_id_done:%d\n",
			    wls_status->product_id, wls_status->tx_product_id_done);
		}
		break;
	case WLS_RESPONE_BATT_TEMP_SOC:
		if (rx_msg->msg_type == WLS_CMD_SEND_BATT_TEMP_SOC) {
			pr_info("temp:%d, soc:%d\n", (data[0] << 8) | data[1], data[2]);
		}
		break;
	default:
		break;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	switch (mask) {
	case WLS_RESPONE_ENCRYPT_DATA1:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA1)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA2:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA2)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA3:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA3)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA4)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA5)
			msg_ok = true;
		break;
	case WLS_RESPONE_ENCRYPT_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_ENCRYPT_DATA6)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA1:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA1)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA2:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA2)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA3:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA3)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA4:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA4)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA5:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA5)
			msg_ok = true;
		break;
	case WLS_RESPONE_SET_AES_DATA6:
		if (rx_msg->msg_type == WLS_CMD_SET_AES_DATA6)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA1:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA1)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA2:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA2)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA3:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA3)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA4:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA4)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA5:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA5)
			msg_ok = true;
		break;
	case WLS_RESPONE_GET_AES_DATA6:
		if (rx_msg->msg_type == WLS_CMD_GET_AES_DATA6)
			msg_ok = true;
		break;
	case WLS_RESPONE_PRODUCT_ID:
		if (rx_msg->msg_type == WLS_CMD_GET_PRODUCT_ID)
			msg_ok = true;
		break;
	case WLS_RESPONE_BATT_TEMP_SOC:
		if (rx_msg->msg_type == WLS_CMD_SEND_BATT_TEMP_SOC)
			msg_ok = true;
		break;
	default:
		pr_err("unknown msg respone(=%d)\n", mask);
	}
	mutex_unlock(&wls_dev->send_msg_lock);

	if (msg_ok) {
		cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
		complete(&wls_dev->msg_ack);
	}
}

static bool oplus_chg_wls_is_oplus_trx(u8 id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(oplus_trx_id_table); i++) {
		if (oplus_trx_id_table[i] == id)
			return true;
	}

	return false;
}

static void oplus_chg_wls_extended_msg_handler(struct oplus_chg_wls *wls_dev,
					       u8 data[])
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	bool msg_ok = false;

	pr_info("msg_type=0x%02x\n", data[0]);

	switch (data[0]) {
	case WLS_CMD_GET_TX_ID:
		if (rx_msg->msg_type != WLS_CMD_GET_TX_ID)
			break;
		if (data[4] == 0x04 && data[2] == 0x05 && oplus_chg_wls_is_oplus_trx(data[3])) {
			wls_status->is_op_trx = true;
			pr_info("is oplus trx\n");
		}
		msg_ok = true;
		break;
	case WLS_CMD_GET_TX_PWR:
		wls_status->tx_pwr_mw = (data[2] << 8) | data[1];
		wls_status->rx_pwr_mw = (data[4] << 8) | data[3];
		wls_status->ploos_mw = wls_status->tx_pwr_mw - wls_status->rx_pwr_mw;
		pr_err("tx_pwr=%d, rx_pwr=%d, ploos=%d\n", wls_status->tx_pwr_mw,
			wls_status->rx_pwr_mw, wls_status->ploos_mw);
		break;
	default:
		pr_err("unknown msg_type(=%d)\n", data[0]);
	}

	if (msg_ok) {
		cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
		complete(&wls_dev->msg_ack);
	}
}

static void oplus_chg_wls_rx_msg_callback(void *dev_data, u8 data[])
{
	struct oplus_chg_wls *wls_dev = dev_data;
	struct oplus_chg_wls_status *wls_status;
	struct oplus_chg_rx_msg *rx_msg;
	u8 temp[2];

	if (wls_dev == NULL) {
		pr_err("wls_dev is NULL\n");
		return;
	}

	wls_status = &wls_dev->wls_status;
	rx_msg = &wls_dev->rx_msg;

	temp[0] = ~data[2];
	temp[1] = ~data[4];
	switch (data[0]) {
	case WLS_MSG_TYPE_STANDARD_MSG:
		pr_info("received: standrad msg\n");
		if (!((data[1] >= WLS_RESPONE_ENCRYPT_DATA1 && data[1] <= WLS_RESPONE_ENCRYPT_DATA6) ||
		    (data[1] >= WLS_RESPONE_SET_AES_DATA1 && data[1] <= WLS_RESPONE_GET_AES_DATA6) ||
		     data[1] == WLS_RESPONE_PRODUCT_ID || data[1] == WLS_RESPONE_BATT_TEMP_SOC)) {
			if ((data[1] == temp[0]) && (data[3] == temp[1])) {
				pr_info("Received TX command: 0x%02X, data: 0x%02X\n",
					data[1], data[3]);
				oplus_chg_wls_standard_msg_handler(wls_dev, data[1], data[3]);
			} else {
				pr_err("msg data error\n");
			}
		} else {
			pr_info("Received TX data: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", data[1], data[2], data[3], data[4]);
			oplus_chg_wls_data_msg_handler(wls_dev, data[1], &data[2]);
		}
		break;
	case WLS_MSG_TYPE_EXTENDED_MSG:
		oplus_chg_wls_extended_msg_handler(wls_dev, &data[1]);
		break;
	default:
		pr_err("Unknown msg type(=%d)\n", data[0]);
	}
}

static void oplus_chg_wls_send_msg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, wls_send_msg_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	int delay_ms = 1000;
	int cep;
	int rc;

	if (!wls_status->rx_online) {
		pr_err("wireless charge is not online\n");
		complete(&wls_dev->msg_ack);
		return;
	}

	if (rx_msg->msg_type == WLS_CMD_GET_TX_PWR || rx_msg->long_data) {
		// need wait cep
		rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
		if (rc < 0) {
			pr_err("can't read cep, rc=%d\n", rc);
			complete(&wls_dev->msg_ack);
			return;
		}
		pr_info("wkcs: cep = %d\n", cep);
		if (abs(cep) > 3) {
			delay_ms = 3000;
			goto out;
		}
		pr_info("wkcs: get tx pwr\n");
	}
	if (rx_msg->long_data)
		rc = oplus_chg_wls_rx_send_data(wls_dev->wls_rx, rx_msg->msg_type,
			rx_msg->buf, ARRAY_SIZE(rx_msg->buf));
	else
		rc = oplus_chg_wls_rx_send_msg(wls_dev->wls_rx, rx_msg->msg_type, rx_msg->data);
	if (rc < 0) {
		pr_err("rx send msg error, rc=%d\n", rc);
		complete(&wls_dev->msg_ack);
		return;
	}

out:
	schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(delay_ms));
}

static int oplus_chg_wls_send_msg(struct oplus_chg_wls *wls_dev, u8 msg, u8 data, int wait_time_s)
{
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	int cep;
	int rc;

	if (!wls_dev->msg_callback_ok) {
		rc = oplus_chg_wls_rx_register_msg_callback(wls_dev->wls_rx, wls_dev,
			oplus_chg_wls_rx_msg_callback);
		if (rc < 0) {
			pr_err("can't reg msg callback, rc=%d\n", rc);
			return rc;
		} else {
			wls_dev->msg_callback_ok = true;
		}
	}

	if (rx_msg->pending) {
		pr_err("msg pending\n");
		return -EAGAIN;
	}

	if (msg == WLS_CMD_GET_TX_PWR) {
		// need wait cep
		rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
		if (rc < 0) {
			pr_err("can't read cep, rc=%d\n", rc);
			return rc;
		}
		if (abs(cep) > 3) {
			pr_info("wkcs: cep = %d\n", cep);
			return -EAGAIN;
		}
	}

	rc = oplus_chg_wls_rx_send_msg(wls_dev->wls_rx, msg, data);
	if (rc) {
		pr_err("send msg error, rc=%d\n", rc);
		return rc;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	rx_msg->msg_type = msg;
	rx_msg->data = data;
	rx_msg->long_data = false;
	mutex_unlock(&wls_dev->send_msg_lock);
	if (wait_time_s > 0) {
		rx_msg->pending = true;
		reinit_completion(&wls_dev->msg_ack);
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
		rc = wait_for_completion_timeout(&wls_dev->msg_ack, msecs_to_jiffies(wait_time_s * 1000));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
			rc = -ETIMEDOUT;
		}
		rx_msg->msg_type = 0;
		rx_msg->data = 0;
		rx_msg->buf[0] = 0;
		rx_msg->buf[1] = 0;
		rx_msg->buf[2] = 0;
		rx_msg->respone_type = 0;
		rx_msg->long_data = false;
		rx_msg->pending = false;
	} else if (wait_time_s < 0) {
		rx_msg->pending = false;
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
	}

	return rc;
}

static int oplus_chg_wls_send_data(struct oplus_chg_wls *wls_dev, u8 msg, u8 data[3], int wait_time_s)
{
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;
	int cep;
	int rc;

	if (!wls_dev->msg_callback_ok) {
		rc = oplus_chg_wls_rx_register_msg_callback(wls_dev->wls_rx, wls_dev,
			oplus_chg_wls_rx_msg_callback);
		if (rc < 0) {
			pr_err("can't reg msg callback, rc=%d\n", rc);
			return rc;
		} else {
			wls_dev->msg_callback_ok = true;
		}
	}

	if (rx_msg->pending) {
		pr_err("msg pending\n");
		return -EAGAIN;
	}

	// need wait cep
	rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
	if (rc < 0) {
		pr_err("can't read cep, rc=%d\n", rc);
		return rc;
	}
	if (abs(cep) > 3) {
		pr_info("wkcs: cep = %d\n", cep);
		return -EAGAIN;
	}

	rc = oplus_chg_wls_rx_send_data(wls_dev->wls_rx, msg, data, 3);
	if (rc) {
		pr_err("send data error, rc=%d\n", rc);
		return rc;
	}

	mutex_lock(&wls_dev->send_msg_lock);
	rx_msg->msg_type = msg;
	rx_msg->buf[0] = data[0];
	rx_msg->buf[1] = data[1];
	rx_msg->buf[2] = data[2];
	rx_msg->long_data = true;
	mutex_unlock(&wls_dev->send_msg_lock);
	if (wait_time_s > 0) {
		rx_msg->pending = true;
		reinit_completion(&wls_dev->msg_ack);
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
		rc = wait_for_completion_timeout(&wls_dev->msg_ack, msecs_to_jiffies(wait_time_s * 1000));
		if (!rc) {
			pr_err("Error, timed out sending message\n");
			cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
			rc = -ETIMEDOUT;
		}
		rx_msg->msg_type = 0;
		rx_msg->data = 0;
		rx_msg->buf[0] = 0;
		rx_msg->buf[1] = 0;
		rx_msg->buf[2] = 0;
		rx_msg->respone_type = 0;
		rx_msg->long_data = false;
		rx_msg->pending = false;
	} else if (wait_time_s < 0) {
		rx_msg->pending = false;
		schedule_delayed_work(&wls_dev->wls_send_msg_work, msecs_to_jiffies(1000));
	}

	return rc;
}

static void oplus_chg_wls_clean_msg(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_rx_msg *rx_msg = &wls_dev->rx_msg;

	cancel_delayed_work_sync(&wls_dev->wls_send_msg_work);
	if (rx_msg->pending)
		complete(&wls_dev->msg_ack);
	rx_msg->msg_type = 0;
	rx_msg->data = 0;
	rx_msg->buf[0] = 0;
	rx_msg->buf[1] = 0;
	rx_msg->buf[2] = 0;
	rx_msg->respone_type = 0;
	rx_msg->long_data = false;
	rx_msg->pending = false;
}

static int oplus_chg_wls_get_verity_data(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	mutex_lock(&wls_dev->cmd_data_lock);
	memset(&wls_dev->cmd, 0, sizeof(struct wls_dev_cmd));
	wls_dev->cmd.cmd = WLS_DEV_CMD_WLS_AUTH;
	wls_dev->cmd.data_size = 0;
	wls_dev->cmd_data_ok = true;
	wls_status->verity_data_ok = false;
	mutex_unlock(&wls_dev->cmd_data_lock);
	wake_up(&wls_dev->read_wq);

	return 0;
}

#define VERITY_TIMEOUT_MAX	40
static void oplus_chg_wls_des_verity(struct oplus_chg_wls *wls_dev)
{
	u8 buf[3];
	int rc;
	struct oplus_chg_wls_status *wls_status;

	if (!wls_dev)
		return;

	wls_status = &wls_dev->wls_status;
	if (!wls_status->rx_online)
		return;

	wls_status->verity_started = true;
#ifndef OPLUS_CHG_WLS_AUTH_ENABLE
	wls_status->verity_pass = true;
	goto done;
#endif

	wls_status->verity_wait_timeout = jiffies + VERITY_TIMEOUT_MAX * HZ;
retry:
	if (!wls_status->verity_data_ok) {
		pr_err("verity data not ready\n");
		if (wls_status->verity_count < 5) {
			wls_status->verity_count++;
			oplus_chg_wls_get_verity_data(wls_dev);
			schedule_delayed_work(&wls_dev->wls_verity_work,
					      msecs_to_jiffies(500));
			return;
		}
		wls_status->verity_pass = false;
		goto done;
	}

	buf[0] = wls_status->verfity_data.random_num[0];
	buf[1] = wls_status->verfity_data.random_num[1];
	buf[2] = wls_status->verfity_data.random_num[2];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_ENCRYPT_DATA1, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA1, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	buf[0] = wls_status->verfity_data.random_num[3];
	buf[1] = wls_status->verfity_data.random_num[4];
	buf[2] = wls_status->verfity_data.random_num[5];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_ENCRYPT_DATA2, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA2, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	buf[0] = wls_status->verfity_data.random_num[6];
	buf[1] = wls_status->verfity_data.random_num[7];
	buf[2] = WLS_ENCODE_MASK;
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_ENCRYPT_DATA3, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA3, rc);
		wls_status->verity_pass = false;
		goto done;
	}

	/* Wait for the base encryption to complete */
	msleep(500);

	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_ENCRYPT_DATA4, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA4, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_ENCRYPT_DATA5, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA5, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_ENCRYPT_DATA6, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_ENCRYPT_DATA6, rc);
		wls_status->verity_pass = false;
		goto done;
	}

	pr_info("encrypt_data: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x",
		wls_status->encrypt_data[0], wls_status->encrypt_data[1],
		wls_status->encrypt_data[2], wls_status->encrypt_data[3],
		wls_status->encrypt_data[4], wls_status->encrypt_data[5],
		wls_status->encrypt_data[6], wls_status->encrypt_data[7]);

	if (memcmp(&wls_status->encrypt_data,
		   &wls_status->verfity_data.encode_num, WLS_AUTH_ENCODE_LEN)) {
		pr_err("verity faile\n");
		wls_status->verity_pass = false;
	} else {
		pr_err("verity pass\n");
		wls_status->verity_pass = true;
	}

done:
	wls_status->verity_count = 0;
	if (!wls_status->verity_pass && wls_status->rx_online &&
	    time_before(jiffies, wls_status->verity_wait_timeout))
		goto retry;
	oplus_vote(wls_dev->fcc_votable, VERITY_VOTER, false, 0, false);
	if (!wls_status->verity_pass && wls_status->rx_online) {
		wls_status->online_keep = true;
		wls_status->verity_state_keep = true;
		oplus_vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
		schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
	}
}

static void oplus_chg_wls_aes_verity(struct oplus_chg_wls *wls_dev)
{
	u8 buf[3];
	int rc;
	int ret;
	struct oplus_chg_wls_status *wls_status;

	if (!wls_dev)
		return;

	wls_status = &wls_dev->wls_status;
	if (!wls_status->rx_online)
		return;

	wls_status->verity_started = true;
#ifndef OPLUS_CHG_WLS_AUTH_ENABLE
	wls_status->verity_pass = true;
	goto done;
#endif

	wls_status->verity_wait_timeout = jiffies + VERITY_TIMEOUT_MAX * HZ;
retry:
	if (!wls_status->aes_verity_data_ok) {
		pr_err("verity data not ready\n");
		if (is_comm_ocm_available(wls_dev))
			ret = oplus_chg_common_set_mutual_cmd(wls_dev->comm_ocm,
					CMD_WLS_THIRD_PART_AUTH,
					sizeof(wls_status->vendor_id), &(wls_status->vendor_id));
		else
			goto done;
		if (ret == CMD_ERROR_HIDL_NOT_READY) {
			schedule_delayed_work(&wls_dev->wls_verity_work,
						      msecs_to_jiffies(1000));
			return;
		} else if (ret != CMD_ACK_OK) {
			if (wls_status->verity_count < 5) {
				wls_status->verity_count++;
				schedule_delayed_work(&wls_dev->wls_verity_work,
						      msecs_to_jiffies(1500));
				return;
			}
			wls_status->verity_pass = false;
			goto done;
		}
	}

	buf[0] = wls_status->aes_verfity_data.aes_random_num[0];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[1];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[2];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA1, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA1, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[3];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[4];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[5];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA2, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA2, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[6];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[7];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[8];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA3, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA3, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[9];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[10];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[11];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA4, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA4, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[12];
	buf[1] = wls_status->aes_verfity_data.aes_random_num[13];
	buf[2] = wls_status->aes_verfity_data.aes_random_num[14];
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA5, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA5, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	buf[0] = wls_status->aes_verfity_data.aes_random_num[15];
	buf[1] = wls_status->vendor_id;
	buf[2] = wls_status->aes_key_num;
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_SET_AES_DATA6, buf, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_SET_AES_DATA6, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	/* Wait for the base encryption to complete */
	msleep(500);

	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA1, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA1, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA2, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA2, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA3, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA3, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA4, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA4, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA5, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA5, rc);
		wls_status->verity_pass = false;
		goto done;
	}
	msleep(500);
	wls_status->verity_count = 0;
	do {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_AES_DATA6, 0xff, 5);
		if (rc < 0) {
			if (rc != -EAGAIN)
				wls_status->verity_count++;
			msleep(500);
		}
	} while (rc < 0 && wls_status->verity_count < 5 && wls_status->rx_online);
	if (rc < 0 || !wls_status->rx_online) {
		pr_err("send 0x%02x error, rc=%d\n", WLS_CMD_GET_AES_DATA6, rc);
		wls_status->verity_pass = false;
		goto done;
	}

	pr_info("aes encrypt_data: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x \
		0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x",
		wls_status->aes_encrypt_data[0], wls_status->aes_encrypt_data[1],
		wls_status->aes_encrypt_data[2], wls_status->aes_encrypt_data[3],
		wls_status->aes_encrypt_data[4], wls_status->aes_encrypt_data[5],
		wls_status->aes_encrypt_data[6], wls_status->aes_encrypt_data[7],
		wls_status->aes_encrypt_data[8], wls_status->aes_encrypt_data[9],
		wls_status->aes_encrypt_data[10], wls_status->aes_encrypt_data[11],
		wls_status->aes_encrypt_data[12], wls_status->aes_encrypt_data[13],
		wls_status->aes_encrypt_data[14], wls_status->aes_encrypt_data[15]);

	if (memcmp(&wls_status->aes_encrypt_data,
		   &wls_status->aes_verfity_data.aes_encode_num, WLS_AUTH_AES_ENCODE_LEN)) {
		pr_err("verity faile\n");
		wls_status->verity_pass = false;
	} else {
		pr_err("verity pass\n");
		wls_status->verity_pass = true;
	}

done:
	wls_status->verity_count = 0;
	if (!wls_status->verity_pass && wls_status->rx_online &&
	    time_before(jiffies, wls_status->verity_wait_timeout))
		goto retry;
	wls_status->aes_verity_done = true;
	oplus_vote(wls_dev->fcc_votable, VERITY_VOTER, false, 0, false);
	if (!wls_status->verity_pass && wls_status->rx_online) {
		wls_status->online_keep = true;
		wls_status->verity_state_keep = true;
		oplus_vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
		schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
	}
}

static void oplus_chg_wls_verity_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, wls_verity_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (!wls_status->verify_by_aes)
		oplus_chg_wls_des_verity(wls_dev);
	else
		oplus_chg_wls_aes_verity(wls_dev);
}

static void oplus_chg_wls_exchange_batt_mesg(struct oplus_chg_wls *wls_dev)
{
	int soc = 0, temp = 0;
	u8 buf[3];
	struct oplus_chg_wls_status *wls_status;
	int rc;

	if (!wls_dev)
		return;

	wls_status = &wls_dev->wls_status;
	if (!wls_status->rx_online || !wls_status->aes_verity_done ||
	    wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY)
		return;

	rc = oplus_chg_wls_get_ui_soc(wls_dev, &soc);
	if (rc < 0) {
		pr_err("can't get ui soc, rc=%d\n", rc);
		return;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n", rc);
		return;
	}

	buf[0] = (temp >> 8) & 0xff;
	buf[1] = temp & 0xff;
	buf[2] = soc & 0xff;
	pr_info("soc:%d, temp:%d\n", soc, temp);

	mutex_lock(&wls_dev->update_data_lock);
	oplus_chg_wls_send_data(wls_dev, WLS_CMD_SEND_BATT_TEMP_SOC, buf, 0);
	msleep(100);
	mutex_unlock(&wls_dev->update_data_lock);
}

static void oplus_chg_wls_reset_variables(struct oplus_chg_wls *wls_dev) {
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	wls_status->adapter_type = WLS_ADAPTER_TYPE_UNKNOWN;
	wls_status->adapter_id = 0;
	wls_status->adapter_power = 0;
	wls_status->charge_type = WLS_CHARGE_TYPE_DEFAULT;
	wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
	wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
	wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
	wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_DEFAULT;
	wls_status->wls_type = OPLUS_CHG_WLS_UNKNOWN;
	wls_status->temp_region = BATT_TEMP_UNKNOWN;

	// wls_status->rx_online = false;
	// wls_status->rx_present = false;
	wls_status->is_op_trx = false;
	wls_status->epp_working = false;
	wls_status->epp_5w = false;
	wls_status->quiet_mode = false;
	wls_status->switch_quiet_mode = false;
	wls_status->quiet_mode_init = false;
	wls_status->cep_timeout_adjusted = false;
	wls_status->upgrade_fw_pending = false;
	wls_status->fw_upgrading = false;
	wls_status->trx_present = false;
	wls_status->trx_online = false;
	wls_status->trx_transfer_start_time = 0;
	wls_status->trx_transfer_end_time = 0;
	wls_status->trx_usb_present_once = false;
	wls_status->fastchg_started = false;
	wls_status->fastchg_disable = false;
	wls_status->fastchg_vol_set_ok = false;
	wls_status->fastchg_curr_set_ok = false;
	wls_status->fastchg_curr_need_dec = false;
	wls_status->ffc_check = false;
	wls_status->wait_cep_stable = false;
	wls_status->fastchg_restart = false;
	wls_status->rx_adc_test_enable = false;
	wls_status->rx_adc_test_pass = false;
	wls_status->boot_online_keep = false;
	wls_status->chg_done = false;
	wls_status->chg_done_quiet_mode = false;

	wls_status->state_sub_step = 0;
	wls_status->iout_ma = 0;
	wls_status->iout_ma_conunt = 0;
	wls_status->vout_mv = 0;
	wls_status->vrect_mv = 0;
	wls_status->trx_curr_ma = 0;
	wls_status->trx_vol_mv = 0;
	// wls_status->fastchg_target_curr_ma = 0;
	wls_status->fastchg_target_vol_mv = 0;
	wls_status->fastchg_ibat_max_ma = 0;
	wls_status->tx_pwr_mw = 0;
	wls_status->rx_pwr_mw = 0;
	wls_status->ploos_mw = 0;
	wls_status->epp_plus_skin_step = 0;
	wls_status->epp_skin_step = 0;
	wls_status->bpp_skin_step = 0;
	wls_status->epp_plus_led_on_skin_step = 0;
	wls_status->epp_led_on_skin_step = 0;
	wls_status->pwr_max_mw = 0;
	wls_status->quiet_mode_need = WLS_QUIET_MODE_UNKOWN;
	wls_status->adapter_info_cmd_count = 0;
	wls_status->fastchg_retry_count = 0;
	wls_status->fastchg_err_count = 0;
	wls_status->non_fcc_level = 0;
	wls_status->skewing_level = 0;
	wls_status->fast_cep_check = 0;
#ifndef CONFIG_OPLUS_CHG_OOS
	wls_status->cool_down = 0;
#endif
	wls_status->wls_bcc_max_curr = OPLUS_WLS_BCC_MAX_CURR_INIT;
	wls_status->wls_bcc_min_curr = OPLUS_WLS_BCC_MIN_CURR_INIT;
	wls_status->wls_bcc_stop_curr = OPLUS_WLS_BCC_STOP_CURR_INIT;
	wls_status->bcc_curve_idx = 0;
	wls_status->bcc_true_idx = 0;
	wls_status->bcc_temp_range = 0;

	wls_status->cep_ok_wait_timeout = jiffies;
	wls_status->fastchg_retry_timer = jiffies;
	wls_status->fastchg_err_timer = jiffies;
	wls_dev->batt_charge_enable = true;

	/*wls encrypt*/
	if (!wls_status->verity_state_keep) {
		/*
		 * After the AP is actively disconnected, the verification
		 * status flag does not need to change.
		 */
		wls_status->verity_pass = true;
	}
	wls_status->verity_started = false;
	wls_status->verity_data_ok = false;
	wls_status->verity_count = 0;
	wls_status->aes_verity_done = false;
	wls_status->verify_by_aes = false;
	wls_status->tx_extern_cmd_done = false;
	wls_status->tx_product_id_done = false;
	memset(&wls_status->encrypt_data, 0, ARRAY_SIZE(wls_status->encrypt_data));
	memset(&wls_status->verfity_data, 0, sizeof(struct wls_auth_result));

	oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, wls_status->rx_present ? 0 : 100, false);
	oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, MAX_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, SKIN_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, UOVP_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, NON_FFC_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, CV_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, COOL_DOWN_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, CEP_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, BCC_CURRENT_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_fcc_votable, MAX_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_fv_votable, USER_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, MAX_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, DEF_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, STEP_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, FCC_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, JEITA_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, CEP_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, SKIN_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, BASE_MAX_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, RX_MAX_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, COOL_DOWN_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, VERITY_VOTER, false, 0, false);
	oplus_vote(wls_dev->fcc_votable, BCC_CURRENT_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, QUIET_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, CEP_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, FCC_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, SKIN_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, BATT_VOL_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, RX_FAST_ERR_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, IOUT_CURR_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, HW_ERR_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, CURR_ERR_VOTER, false, 0, false);
	oplus_vote(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_out_disable_votable, FFC_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_out_disable_votable, USER_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_out_disable_votable, UOVP_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_input_disable_votable, UOVP_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
	oplus_rerun_election(wls_dev->nor_icl_votable, false);
	oplus_rerun_election(wls_dev->nor_fcc_votable, false);
	oplus_rerun_election(wls_dev->nor_fv_votable, false);
	oplus_rerun_election(wls_dev->fcc_votable, false);
	oplus_rerun_election(wls_dev->fastchg_disable_votable, false);
	oplus_rerun_election(wls_dev->nor_out_disable_votable, false);
	oplus_rerun_election(wls_dev->nor_input_disable_votable, false);

	if (is_comm_ocm_available(wls_dev))
		oplus_chg_comm_status_init(wls_dev->comm_ocm);
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FAULT_INJECT_CHG)
noinline
#endif
static int oplus_chg_wls_get_skewing_current(struct oplus_chg_wls *wls_dev)
{
	int cep_curr_ma = 0;

	if (oplus_is_client_vote_enabled(wls_dev->fcc_votable, CEP_VOTER))
		cep_curr_ma = oplus_get_client_vote(wls_dev->fcc_votable, CEP_VOTER);
	else if (oplus_is_client_vote_enabled(wls_dev->nor_icl_votable, CEP_VOTER))
		cep_curr_ma = oplus_get_client_vote(wls_dev->nor_icl_votable, CEP_VOTER);

	pr_info("cep_curr_ma:%d\n", cep_curr_ma);
	return cep_curr_ma;
}

static void oplus_chg_wls_update_track_info(struct oplus_chg_wls *wls_dev,
					    char *crux_info, bool clear)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int batt_temp = 0;
	u32 rx_version = 0;
	u32 trx_version = 0;
	int cep_curr_ma = 0;
	static int highest_temp = 0;
	static int max_iout = 0;
	static int min_cool_down = 0;
	static int min_skewing_current = 0;
	int index = 0;

	if (clear) {
		highest_temp = 0;
		max_iout = 0;
		min_cool_down = 0;
		min_skewing_current = 0;
		return;
	}

	oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	highest_temp = max(highest_temp, batt_temp);
	max_iout = max(max_iout, wls_status->iout_ma);
	if (wls_status->cool_down) {
		if (!min_cool_down)
			min_cool_down = wls_status->cool_down;
		else
			min_cool_down =
				min(min_cool_down, wls_status->cool_down);
	}

	if (oplus_is_client_vote_enabled(wls_dev->fcc_votable, CEP_VOTER))
		cep_curr_ma =
			oplus_get_client_vote(wls_dev->fcc_votable, CEP_VOTER);
	else if (oplus_is_client_vote_enabled(wls_dev->nor_icl_votable,
					      CEP_VOTER))
		cep_curr_ma = oplus_get_client_vote(wls_dev->nor_icl_votable,
						    CEP_VOTER);

	if (cep_curr_ma) {
		if (!min_skewing_current)
			min_skewing_current = cep_curr_ma;
		else
			min_skewing_current =
				min(min_skewing_current, cep_curr_ma);
	}

	oplus_chg_wls_rx_get_trx_version(wls_dev->wls_rx, &trx_version);
	oplus_chg_wls_rx_get_rx_version(wls_dev->wls_rx, &rx_version);
	if (crux_info) {
		index += snprintf(&crux_info[index],
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$wls_general_info@@tx_version=%d,rx_"
				  "version=%d,adapter_type_wpc=%d,"
				  "dock_version=%d,fastchg_ing=%d,vout=%d,iout="
				  "%d,break_count=%d,"
				  "wpc_chg_err=%d,highest_temp=%d,max_iout=%d,"
				  "min_cool_down=%d,"
				  "min_skewing_current=%d,"
				  "wls_auth_fail=%d,work_silent_mode=%d",
				  trx_version, rx_version,
				  wls_status->adapter_type,
				  wls_status->adapter_id,
				  wls_status->fastchg_started,
				  wls_status->vout_mv, wls_status->iout_ma,
				  wls_status->break_count, wls_status->trx_err,
				  highest_temp, max_iout, min_cool_down,
				  min_skewing_current, !wls_status->verity_pass,
				  wls_status->switch_quiet_mode);
		pr_info("%s\n", crux_info);
	}
}

#define WLS_TRX_INFO_UPLOAD_THD_2MINS		(2 * 60)
#define WLS_LOCAL_T_NS_TO_S_THD			1000000000
#define WLS_TRX_INFO_THD_1MIN			60
static int oplus_chg_wls_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / WLS_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int oplus_chg_wls_track_upload_trx_general_info(
	struct oplus_chg_wls *wls_dev, char *trx_crux_info, bool usb_present_once)
{
	int index = 0;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	memset(wls_dev->trx_info_load_trigger.crux_info,
		0, sizeof(wls_dev->trx_info_load_trigger.crux_info));

	index += snprintf(&(wls_dev->trx_info_load_trigger.crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			"$$power_mode@@wireless");
	index += snprintf(&(wls_dev->trx_info_load_trigger.crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$total_time@@%d",
			(wls_status->trx_transfer_end_time -
			wls_status->trx_transfer_start_time) /
			WLS_TRX_INFO_THD_1MIN);

	index += snprintf(&(wls_dev->trx_info_load_trigger.crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$usb_present_once@@%d", usb_present_once);

	if (trx_crux_info && strlen(trx_crux_info))
		index += snprintf(&(wls_dev->trx_info_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
				trx_crux_info);

	schedule_delayed_work(&wls_dev->trx_info_load_trigger_work, 0);
	pr_info("%s\n", wls_dev->trx_info_load_trigger.crux_info);

	return 0;
}

static int oplus_chg_wls_track_upload_rx_err_info(struct oplus_chg_wls *wls_dev, int err_type)
{
	int index = 0;
	char err_reason[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN] = { 0 };
	char *rx_crux_info;

	if (wls_dev->rx_err_uploading) {
		pr_info("rx_err_uploading now, should return\n");
		return 0;
	}

	mutex_lock(&wls_dev->track_upload_lock);
	wls_dev->rx_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!wls_dev->rx_err_load_trigger) {
		pr_err("rx_err_load_trigger memery alloc fail\n");
		mutex_unlock(&wls_dev->track_upload_lock);
		return -ENOMEM;
	}
	rx_crux_info = kzalloc(OPLUS_CHG_TRACK_CURX_INFO_LEN, GFP_KERNEL);
	if (!rx_crux_info) {
		pr_err("rx_crux_info memery alloc fail\n");
		kfree(wls_dev->rx_err_load_trigger);
		wls_dev->rx_err_load_trigger = NULL;
		mutex_unlock(&wls_dev->track_upload_lock);
		return -ENOMEM;
	}

	wls_dev->rx_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	wls_dev->rx_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;

	wls_dev->rx_err_uploading = true;

	index += snprintf(&(wls_dev->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s", "wls");
	index += snprintf(&(wls_dev->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		OPLUS_CHG_TRACK_SCENE_WLS_RX_ERR);

	oplus_chg_track_get_wls_trx_err_reason(err_type, err_reason, sizeof(err_reason));
	index += snprintf(&(wls_dev->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_reason@@%s", err_reason);

	oplus_chg_wls_update_track_info(wls_dev, rx_crux_info, false);
	index += snprintf(&(wls_dev->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", rx_crux_info);
	kfree(rx_crux_info);

	schedule_delayed_work(&wls_dev->rx_err_load_trigger_work, 0);
	mutex_unlock(&wls_dev->track_upload_lock);
	pr_info("%s\n", wls_dev->rx_err_load_trigger->crux_info);

	return 0;
}

static int oplus_chg_wls_set_trx_enable(struct oplus_chg_wls *wls_dev, bool en)
{
	char trx_crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN] = {0};
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc = 0;

	if (en && wls_status->fw_upgrading) {
		pr_err("FW is upgrading, reverse charging cannot be used\n");
		return -EFAULT;
	}
	if (en && wls_dev->usb_present && !wls_dev->support_tx_boost &&
	    !wls_dev->support_wls_and_tx_boost) {
		pr_err("during USB charging, reverse charging cannot be used");
		return -EFAULT;
	}
	if (en && wls_status->rx_present) {
		pr_err("wireless charging, reverse charging cannot be used");
		return -EFAULT;
	}

	mutex_lock(&wls_dev->connect_lock);
	if (en) {
		if (wls_status->wls_type == OPLUS_CHG_WLS_TRX)
			goto out;
		cancel_delayed_work_sync(&wls_dev->wls_connect_work);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if (!wls_dev->usb_present)
			oplus_vote(wls_dev->wrx_en_votable, TRX_EN_VOTER, true, 1, false);
		else
			oplus_vote(wls_dev->wrx_en_votable, TRX_EN_VOTER, false, 0, false);
		msleep(20);
#endif
		rc = oplus_chg_wls_nor_set_boost_vol(wls_dev->wls_nor, WLS_TRX_MODE_VOL_MV);
		if (rc < 0) {
			pr_err("can't set trx boost vol, rc=%d\n", rc);
			goto out;
		}
		oplus_chg_wls_nor_set_boost_en(wls_dev->wls_nor, true);
		if (rc < 0) {
			pr_err("can't enable trx boost, rc=%d\n", rc);
			goto out;
		}
		msleep(500);
		oplus_chg_wls_reset_variables(wls_dev);
		wls_status->wls_type = OPLUS_CHG_WLS_TRX;
		/*set trx_present true after power up for factory mode*/
		wls_status->trx_present = true;
		wls_status->trx_present_keep = false;
		wls_status->trx_rxac = false;
		oplus_chg_wls_rx_set_trx_start(wls_dev->wls_rx);

		if (!wls_dev->trx_wake_lock_on) {
			pr_info("acquire trx_wake_lock\n");
			__pm_stay_awake(wls_dev->trx_wake_lock);
			wls_dev->trx_wake_lock_on = true;
		} else {
			pr_err("trx_wake_lock is already stay awake\n");
		}
		cancel_delayed_work_sync(&wls_dev->wls_trx_sm_work);
		queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_trx_sm_work, 0);
	} else {
		if (wls_status->wls_type != OPLUS_CHG_WLS_TRX)
			goto out;
		if (wls_status->trx_rxac == true)
			oplus_vote(wls_dev->rx_disable_votable, RXAC_VOTER, true, 1, false);
		cancel_delayed_work_sync(&wls_dev->wls_trx_sm_work);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		oplus_vote(wls_dev->rx_disable_votable, TRX_EN_VOTER, true, 0, false);
#endif
		oplus_chg_wls_nor_set_boost_en(wls_dev->wls_nor, false);
		wls_status->wls_type = OPLUS_CHG_WLS_UNKNOWN;
		wls_status->trx_present = false;
		wls_status->trx_online = false;
		msleep(100);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		oplus_vote(wls_dev->wrx_en_votable, TRX_EN_VOTER, false, 0, false);
		oplus_vote(wls_dev->rx_disable_votable, TRX_EN_VOTER, false, 0, false);
#endif
		if (wls_status->trx_rxac == true)
			oplus_vote(wls_dev->rx_disable_votable, RXAC_VOTER, false, 0, false);
		wls_status->trx_transfer_end_time = oplus_chg_wls_get_local_time_s();
		pr_info("trx_online=%d, usb_present_once=%d,start_time=%d, end_time=%d\n",
			wls_status->trx_online, wls_status->trx_usb_present_once,
			wls_status->trx_transfer_start_time,
			wls_status->trx_transfer_end_time);
		if (wls_status->trx_online && wls_status->trx_transfer_start_time &&
		   (wls_status->trx_transfer_end_time - wls_status->trx_transfer_start_time >
		    WLS_TRX_INFO_UPLOAD_THD_2MINS)) {
			oplus_chg_wls_update_track_info(
				wls_dev, trx_crux_info, false);
			oplus_chg_wls_track_upload_trx_general_info(
				wls_dev, trx_crux_info,
				wls_status->trx_usb_present_once);
		}
		oplus_chg_wls_reset_variables(wls_dev);
		if (is_batt_psy_available(wls_dev))
			power_supply_changed(wls_dev->batt_psy);
		if (wls_dev->trx_wake_lock_on) {
			pr_info("release trx_wake_lock\n");
			__pm_relax(wls_dev->trx_wake_lock);
			wls_dev->trx_wake_lock_on = false;
		} else {
			pr_err("trx_wake_lock is already relax\n");
		}
	}

	if (wls_dev->wls_ocm)
		oplus_chg_mod_changed(wls_dev->wls_ocm);

out:
	mutex_unlock(&wls_dev->connect_lock);
	return rc;
}

#ifdef OPLUS_CHG_DEBUG
static int oplus_chg_wls_path_ctrl(struct oplus_chg_wls *wls_dev,
				enum oplus_chg_wls_path_ctrl_type type)
{
	if (wls_dev->force_type != OPLUS_CHG_WLS_FORCE_TYPE_FAST)
		return -EINVAL;
	if (!wls_dev->support_fastchg)
		return -EINVAL;

	switch(type) {
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_ALL:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_ENABLE_NOR:
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_NOR:
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_ENABLE_FAST:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, true);
		(void)oplus_chg_wls_fast_start(wls_dev->wls_fast);
		break;
	case OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_FAST:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		break;
	default:
		pr_err("unknown path ctrl type(=%d), \n", type);
		return -EINVAL;
	}

	return 0;
}
#endif /* OPLUS_CHG_DEBUG */

static bool oplus_wls_check_bcc_support(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (!wls_dev)
		return -EINVAL;

	if (wls_dev->support_wls_chg_bcc &&
		(wls_status->bcc_temp_range == WLS_BCC_TEMP_160_TO_400 ||
		    wls_status->bcc_temp_range == WLS_BCC_TEMP_400_TO_440))
		return true;

	return false;
}

static void oplus_chg_wls_set_bcc_current_iout(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int bcc_icl = 0;

	if (oplus_wls_check_bcc_support(wls_dev) != true)
		return;

	bcc_icl = wls_status->bcc_current / wls_dev->wls_bcc_fcc_to_icl_factor;

	if (bcc_icl > 0 &&
	    (wls_status->wls_type == OPLUS_CHG_WLS_SVOOC ||
	    wls_status->wls_type == OPLUS_CHG_WLS_PD_65W)) {
		oplus_vote(wls_dev->nor_icl_votable, BCC_CURRENT_VOTER, true, bcc_icl, false);
		oplus_vote(wls_dev->fcc_votable, BCC_CURRENT_VOTER, true, bcc_icl, false);
	}

	pr_err("set bcc_icl=%d, wls_status->bcc_current=%d\n", bcc_icl, wls_status->bcc_current);
}

#ifndef CONFIG_OPLUS_CHG_OOS
static void oplus_chg_wls_set_cool_down_iout(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int cool_down = wls_status->cool_down;
	int icl_ma = 0;
	int cool_down_max_step = 0;
	int index = 0;

	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_BPP:
		index = OPLUS_WLS_CHG_MODE_BPP;
		break;
	case OPLUS_CHG_WLS_EPP:
		index = OPLUS_WLS_CHG_MODE_EPP;
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
		index = OPLUS_WLS_CHG_MODE_EPP_PLUS;
		break;
	case OPLUS_CHG_WLS_VOOC:
		index = OPLUS_WLS_CHG_MODE_AIRVOOC;
		break;
	case OPLUS_CHG_WLS_SVOOC:
	case OPLUS_CHG_WLS_PD_65W:
		index = OPLUS_WLS_CHG_MODE_AIRSVOOC;
		break;
	default:
		index = OPLUS_WLS_CHG_MODE_BPP;
		break;
	}

	cool_down_max_step = wls_dev->cool_down_step[index].max_step;
	if (cool_down >= cool_down_max_step && cool_down > 1)
		cool_down = cool_down_max_step - 1;
	icl_ma = dynamic_cfg->cool_down_step[index][cool_down];

	if (cool_down == 0 || wls_dev->factory_mode) {
		oplus_vote(wls_dev->nor_icl_votable, COOL_DOWN_VOTER, false, 0, false);
		oplus_vote(wls_dev->fcc_votable, COOL_DOWN_VOTER, false, 0, false);
		icl_ma = 0;
	} else {
		if (icl_ma > 0) {
			oplus_vote(wls_dev->nor_icl_votable, COOL_DOWN_VOTER, true, icl_ma, false);
			oplus_vote(wls_dev->fcc_votable, COOL_DOWN_VOTER, true, icl_ma, false);
		}
	}

	pr_err("icl_cool_down_ma=%d, cool_down_level=%d\n", icl_ma, wls_status->cool_down);
}
#endif

static int oplus_chg_wls_choose_fastchg_curve(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	enum oplus_chg_temp_region temp_region;
	int batt_soc_plugin, batt_temp_plugin;
	int real_soc = 100;
	int rc = 0;
	int i = 0;

	if (is_comm_ocm_available(wls_dev)) {
		temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	} else {
		pr_err("not find comm ocm\n");
		return -ENODEV;
	}

	if (temp_region <= BATT_TEMP_LITTLE_COLD) {
		batt_temp_plugin = WLS_FAST_TEMP_0_TO_50;
	} else if (temp_region == BATT_TEMP_COOL) {
		batt_temp_plugin = WLS_FAST_TEMP_50_TO_120;
	} else if (temp_region == BATT_TEMP_LITTLE_COOL) {
		batt_temp_plugin = WLS_FAST_TEMP_120_TO_160;
	} else if (temp_region == BATT_TEMP_NORMAL) {
		batt_temp_plugin = WLS_FAST_TEMP_160_TO_400;
	} else if (temp_region == BATT_TEMP_LITTLE_WARM) {
		batt_temp_plugin = WLS_FAST_TEMP_400_TO_440;
	} else {
		batt_temp_plugin = WLS_FAST_TEMP_400_TO_440;
	}

	rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
	if ((rc < 0) || (real_soc >= dynamic_cfg->fastchg_max_soc)) {
		pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
		return -EPERM;
	}

	if (real_soc < 30) {
		batt_soc_plugin = WLS_FAST_SOC_0_TO_30;
	} else if (real_soc < 70) {
		batt_soc_plugin = WLS_FAST_SOC_30_TO_70;
	} else if (real_soc < 90) {
		batt_soc_plugin = WLS_FAST_SOC_70_TO_90;
	} else {
		batt_soc_plugin = WLS_FAST_SOC_70_TO_90;
	}

	if ((batt_temp_plugin != WLS_FAST_TEMP_MAX)
			&& (batt_soc_plugin != WLS_FAST_SOC_MAX)) {
		if (wls_dev->wls_status.adapter_id != WLS_ADAPTER_THIRD_PARTY) {
			wls_dev->wls_fcc_step.max_step =
			    wls_dev->fcc_steps[batt_soc_plugin].fcc_step[batt_temp_plugin].max_step;
			memcpy(&wls_dev->wls_fcc_step.fcc_step,
			    wls_dev->fcc_steps[batt_soc_plugin].fcc_step[batt_temp_plugin].fcc_step,
			    sizeof(struct oplus_chg_wls_range_data) * wls_dev->wls_fcc_step.max_step);
		} else {
			wls_dev->wls_fcc_step.max_step =
			    wls_dev->fcc_third_part_steps[batt_soc_plugin].fcc_step[batt_temp_plugin].max_step;
			memcpy(&wls_dev->wls_fcc_step.fcc_step,
			    wls_dev->fcc_third_part_steps[batt_soc_plugin].fcc_step[batt_temp_plugin].fcc_step,
			    sizeof(struct oplus_chg_wls_range_data) * wls_dev->wls_fcc_step.max_step);
		}
		for (i=0; i < wls_dev->wls_fcc_step.max_step; i++) {
			pr_err("%s: %d %d %d %d %d\n", __func__,
				wls_dev->wls_fcc_step.fcc_step[i].low_threshold,
				wls_dev->wls_fcc_step.fcc_step[i].high_threshold,
				wls_dev->wls_fcc_step.fcc_step[i].curr_ma,
				wls_dev->wls_fcc_step.fcc_step[i].vol_max_mv,
				wls_dev->wls_fcc_step.fcc_step[i].need_wait);
		}
	} else {
		return -EPERM;
	}

	return 0;
}

static int oplus_wls_bcc_choose_curve(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	enum oplus_chg_temp_region temp_region;
	int batt_soc_plugin, batt_temp_plugin;
	int real_soc = 100;
	int rc = 0;
	int i = 0;

	if (is_comm_ocm_available(wls_dev)) {
		temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	} else {
		pr_err("not find comm ocm\n");
		return -ENODEV;
	}

	if (temp_region <= BATT_TEMP_LITTLE_COLD)
		batt_temp_plugin = WLS_BCC_TEMP_0_TO_50;
	else if (temp_region == BATT_TEMP_COOL)
		batt_temp_plugin = WLS_BCC_TEMP_50_TO_120;
	else if (temp_region == BATT_TEMP_LITTLE_COOL)
		batt_temp_plugin = WLS_BCC_TEMP_120_TO_160;
	else if (temp_region == BATT_TEMP_NORMAL)
		batt_temp_plugin = WLS_BCC_TEMP_160_TO_400;
	else if (temp_region == BATT_TEMP_LITTLE_WARM)
		batt_temp_plugin = WLS_BCC_TEMP_400_TO_440;
	else
		batt_temp_plugin = WLS_BCC_TEMP_400_TO_440;

	wls_status->bcc_temp_range = batt_temp_plugin;

	rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
	if ((rc < 0) || (real_soc >= dynamic_cfg->fastchg_max_soc)) {
		pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
		return -EPERM;
	}

	if (real_soc < 30)
		batt_soc_plugin = WLS_BCC_SOC_0_TO_30;
	else if (real_soc < 70)
		batt_soc_plugin = WLS_BCC_SOC_30_TO_70;
	else if (real_soc < 90)
		batt_soc_plugin = WLS_BCC_SOC_70_TO_90;
	else
		batt_soc_plugin = WLS_BCC_SOC_70_TO_90;

	if ((batt_temp_plugin != WLS_BCC_TEMP_MAX) &&
	    (batt_soc_plugin != WLS_BCC_SOC_MAX)) {
		wls_dev->wls_bcc_step.max_step =
			wls_dev->bcc_steps[batt_soc_plugin].bcc_step[batt_temp_plugin].max_step;
		memcpy(&wls_dev->wls_bcc_step.bcc_step,
			wls_dev->bcc_steps[batt_soc_plugin].bcc_step[batt_temp_plugin].bcc_step,
			sizeof(struct oplus_chg_wls_bcc_data) * wls_dev->wls_bcc_step.max_step);
		for (i = 0; i < wls_dev->wls_bcc_step.max_step; i++) {
			pr_err("%s: %d %d %d %d\n", __func__,
				wls_dev->wls_bcc_step.bcc_step[i].max_batt_volt,
				wls_dev->wls_bcc_step.bcc_step[i].max_curr,
				wls_dev->wls_bcc_step.bcc_step[i].min_curr,
				wls_dev->wls_bcc_step.bcc_step[i].exit);
		}
	} else {
		return -EPERM;
	}

	return 0;
}

static void oplus_wls_bcc_get_curve(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_bcc_step *bcc_chg = &wls_dev->wls_bcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int bcc_batt_volt_now;
	int i, rc;
	int curve_idx = 0;

	rc = oplus_wls_bcc_choose_curve(wls_dev);
	if (rc < 0) {
		pr_err("choose bcc curve failed, rc = %d\n", rc);
		return;
	}

	rc = oplus_chg_wls_get_vbat(wls_dev, &bcc_batt_volt_now);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return;
	}

	for (i = 0; i < wls_dev->wls_bcc_step.max_step; i++) {
		pr_err("bcc_batt is %d target_volt is %d\n", bcc_batt_volt_now,
				bcc_chg->bcc_step[i].max_batt_volt);
		if (bcc_batt_volt_now < bcc_chg->bcc_step[i].max_batt_volt) {
			curve_idx = i;
			pr_err("curve idx = %d\n", curve_idx);
			break;
		}
	}

	wls_status->wls_bcc_max_curr = bcc_chg->bcc_step[curve_idx].max_curr;
	wls_status->wls_bcc_min_curr = bcc_chg->bcc_step[curve_idx].min_curr;
	pr_err("choose max curr is %d, min curr is %d\n", wls_status->wls_bcc_max_curr, wls_status->wls_bcc_min_curr);
}

#define CURVE_CHANGE_COUNT 20
static void oplus_wls_bcc_curr_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, wls_bcc_curr_update_work);
	struct oplus_chg_wls_bcc_step *bcc_chg = &wls_dev->wls_bcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int bcc_batt_volt_now;
	int i, rc;
	static int pre_curve_idx = 0;
	static int idx_cnt = 0;

	rc = oplus_chg_wls_get_vbat(wls_dev, &bcc_batt_volt_now);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return;
	}

	for (i = wls_status->bcc_true_idx; i < wls_dev->wls_bcc_step.max_step; i++) {
		pr_err("bcc_batt is %d target_volt now is %d\n", bcc_batt_volt_now,
				bcc_chg->bcc_step[i].max_batt_volt);
		if (bcc_batt_volt_now < bcc_chg->bcc_step[i].max_batt_volt) {
			wls_status->bcc_curve_idx = i;
			pr_err("curve idx = %d\n", wls_status->bcc_curve_idx);
			break;
		}
	}

	if (pre_curve_idx == wls_status->bcc_curve_idx) {
		idx_cnt = idx_cnt + 1;
		if (idx_cnt >= CURVE_CHANGE_COUNT) {
			wls_status->wls_bcc_max_curr = bcc_chg->bcc_step[wls_status->bcc_curve_idx].max_curr;
			wls_status->wls_bcc_min_curr = bcc_chg->bcc_step[wls_status->bcc_curve_idx].min_curr;
			wls_status->bcc_true_idx = wls_status->bcc_curve_idx;
			pr_err("choose max curr is %d, min curr is %d true idx is %d\n",
					wls_status->wls_bcc_max_curr, wls_status->wls_bcc_min_curr, wls_status->bcc_true_idx);
		}
	} else {
		idx_cnt = 0;
	}
	pre_curve_idx = wls_status->bcc_curve_idx;

	schedule_delayed_work(&wls_dev->wls_bcc_curr_update_work, OPLUS_WLS_BCC_UPDATE_INTERVAL);
}

static bool oplus_wls_wake_bcc_update_work(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev)
		return true;

	schedule_delayed_work(&wls_dev->wls_bcc_curr_update_work, OPLUS_WLS_BCC_UPDATE_INTERVAL);

	return true;
}

static void oplus_wls_cancel_bcc_update_work(struct oplus_chg_wls *wls_dev)
{
	if (!wls_dev)
		return;

	cancel_delayed_work_sync(&wls_dev->wls_bcc_curr_update_work);
}

static void oplus_wls_bcc_parms_init(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (!wls_dev)
		return;

	wls_status->wls_bcc_max_curr = OPLUS_WLS_BCC_MAX_CURR_INIT;
	wls_status->wls_bcc_min_curr = OPLUS_WLS_BCC_MIN_CURR_INIT;
	wls_status->wls_bcc_stop_curr = OPLUS_WLS_BCC_STOP_CURR_INIT;
	wls_status->bcc_curve_idx = 0;
	wls_status->bcc_true_idx = 0;
	wls_status->bcc_temp_range = 0;
}

static int oplus_wls_bcc_get_stop_curr(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	enum oplus_chg_temp_region temp_region;
	int batt_soc_plugin, batt_temp_plugin;
	int real_soc = 100;
	int rc = 0;
	int wls_stop_curr = 1000;

	rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
	if ((rc < 0) || (real_soc >= dynamic_cfg->fastchg_max_soc)) {
		pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
		return -EPERM;
	}

	if (real_soc < 30)
		batt_soc_plugin = WLS_BCC_SOC_0_TO_30;
	else if (real_soc < 70)
		batt_soc_plugin = WLS_BCC_SOC_30_TO_70;
	else if (real_soc < 90)
		batt_soc_plugin = WLS_BCC_SOC_70_TO_90;
	else
		batt_soc_plugin = WLS_BCC_SOC_70_TO_90;

	if (is_comm_ocm_available(wls_dev)) {
		temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	} else {
		pr_err("not find comm ocm\n");
		return -ENODEV;
	}

	if (temp_region <= BATT_TEMP_LITTLE_COLD)
		batt_temp_plugin = WLS_BCC_TEMP_0_TO_50;
	else if (temp_region == BATT_TEMP_COOL)
		batt_temp_plugin = WLS_BCC_TEMP_50_TO_120;
	else if (temp_region == BATT_TEMP_LITTLE_COOL)
		batt_temp_plugin = WLS_BCC_TEMP_120_TO_160;
	else if (temp_region == BATT_TEMP_NORMAL)
		batt_temp_plugin = WLS_BCC_TEMP_160_TO_400;
	else if (temp_region == BATT_TEMP_LITTLE_WARM)
		batt_temp_plugin = WLS_BCC_TEMP_400_TO_440;
	else
		batt_temp_plugin = WLS_BCC_TEMP_400_TO_440;

	if (batt_soc_plugin == WLS_BCC_SOC_0_TO_30) {
		pr_err("get stop curr, enter soc is 0-30\n");
		switch(batt_temp_plugin) {
		case WLS_BCC_TEMP_0_TO_50:
			pr_err("bcc stop curr,temp is 0-5\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_0_to_30[WLS_BCC_TEMP_0_TO_50];
			break;
		case WLS_BCC_TEMP_50_TO_120:
			pr_err("bcc stop curr,temp is 5-12\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_0_to_30[WLS_BCC_TEMP_50_TO_120];
			break;
		case WLS_BCC_TEMP_120_TO_160:
			pr_err("bcc stop curr,temp is 12-16\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_0_to_30[WLS_BCC_TEMP_120_TO_160];
			break;
		case WLS_BCC_TEMP_160_TO_400:
			pr_err("bcc stop curr,temp is 16-40\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_0_to_30[WLS_BCC_TEMP_160_TO_400];
			break;
		case WLS_BCC_TEMP_400_TO_440:
			pr_err("bcc stop curr,temp is 40-44\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_0_to_30[WLS_BCC_TEMP_400_TO_440];
			break;
		}
	}

	if (batt_soc_plugin == WLS_BCC_SOC_30_TO_70) {
		pr_err("get stop curr, enter soc is 30-70\n");
		switch(batt_temp_plugin) {
		case WLS_BCC_TEMP_0_TO_50:
			pr_err("bcc stop curr,temp is 0-5\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_30_to_70[WLS_BCC_TEMP_0_TO_50];
			break;
		case WLS_BCC_TEMP_50_TO_120:
			pr_err("bcc stop curr,temp is 5-12\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_30_to_70[WLS_BCC_TEMP_50_TO_120];
			break;
		case WLS_BCC_TEMP_120_TO_160:
			pr_err("bcc stop curr,temp is 12-16\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_30_to_70[WLS_BCC_TEMP_120_TO_160];
			break;
		case WLS_BCC_TEMP_160_TO_400:
			pr_err("bcc stop curr,temp is 16-40\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_30_to_70[WLS_BCC_TEMP_160_TO_400];
			break;
		case WLS_BCC_TEMP_400_TO_440:
			pr_err("bcc stop curr,temp is 40-44\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_30_to_70[WLS_BCC_TEMP_400_TO_440];
			break;
		}
	}

	if (batt_soc_plugin == WLS_BCC_SOC_70_TO_90) {
		pr_err("get stop curr, enter soc is 70-90\n");
		switch(batt_temp_plugin) {
		case WLS_BCC_TEMP_0_TO_50:
			pr_err("bcc stop curr,temp is 0-5\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_70_to_90[WLS_BCC_TEMP_0_TO_50];
			break;
		case WLS_BCC_TEMP_50_TO_120:
			pr_err("bcc stop curr,temp is 5-12\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_70_to_90[WLS_BCC_TEMP_50_TO_120];
			break;
		case WLS_BCC_TEMP_120_TO_160:
			pr_err("bcc stop curr,temp is 12-16\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_70_to_90[WLS_BCC_TEMP_120_TO_160];
			break;
		case WLS_BCC_TEMP_160_TO_400:
			pr_err("bcc stop curr,temp is 16-40\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_70_to_90[WLS_BCC_TEMP_160_TO_400];
			break;
		case WLS_BCC_TEMP_400_TO_440:
			pr_err("bcc stop curr,temp is 40-44\n");
			wls_stop_curr = dynamic_cfg->bcc_stop_curr_70_to_90[WLS_BCC_TEMP_400_TO_440];
			break;
		}
	}

	pr_err("get stop curr is %d\n", wls_stop_curr);

	wls_status->wls_bcc_stop_curr = wls_stop_curr;

	return wls_stop_curr;
}

static void oplus_chg_wls_config(struct oplus_chg_wls *wls_dev)
{
	enum oplus_chg_temp_region temp_region;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	enum oplus_chg_ffc_status ffc_status;
	int icl_max_ma;
	int fcc_max_ma;
	int icl_index;
	static bool pre_temp_abnormal;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm mod not fount\n");
		return;
	}

	ffc_status = oplus_chg_comm_get_ffc_status(wls_dev->comm_ocm);
	if (ffc_status != FFC_DEFAULT) {
		pr_err("ffc charging, exit\n");
		return;
	}

	if (!wls_status->rx_online && !wls_status->online_keep) {
		pr_err("rx is offline\n");
		return;
	}

	if (oplus_chg_comm_batt_vol_over_cl_thr(wls_dev->comm_ocm))
		icl_index = OPLUS_WLS_CHG_BATT_CL_HIGH;
	else
		icl_index = OPLUS_WLS_CHG_BATT_CL_LOW;

	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	switch (temp_region) {
	case BATT_TEMP_UNKNOWN:
	case BATT_TEMP_HOT:
		if (!pre_temp_abnormal) {
			pre_temp_abnormal = true;
			wls_status->online_keep = true;
			oplus_vote(wls_dev->rx_disable_votable, JEITA_VOTER, true, 1, false);
			oplus_vote(wls_dev->nor_icl_votable, JEITA_VOTER, true, WLS_CURR_JEITA_CHG_MA, true);
			schedule_delayed_work(&wls_dev->rx_restore_work, msecs_to_jiffies(500));
		} else {
			oplus_vote(wls_dev->rx_disable_votable, JEITA_VOTER, false, 0, false);
			oplus_vote(wls_dev->nor_icl_votable, JEITA_VOTER, true, WLS_CURR_JEITA_CHG_MA, true);
		}
		break;
	default:
		pre_temp_abnormal = false;
		oplus_vote(wls_dev->rx_disable_votable, JEITA_VOTER, false, 0, false);
		oplus_vote(wls_dev->nor_icl_votable, JEITA_VOTER, false, 0, true);
		break;
	}

	if (wls_status->wls_type == OPLUS_CHG_WLS_VOOC) {
		if (wls_status->temp_region == BATT_TEMP_LITTLE_COLD) {
			if (temp_region == BATT_TEMP_COLD) {
				wls_status->online_keep = true;
				schedule_delayed_work(&wls_dev->rx_restart_work, 0);
			}
		}

		if (wls_status->temp_region == BATT_TEMP_LITTLE_WARM) {
			if (temp_region == BATT_TEMP_WARM) {
				wls_status->online_keep = true;
				schedule_delayed_work(&wls_dev->rx_restart_work, 0);
			}
		}
	}

	wls_status->temp_region = temp_region;

	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_BPP:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_BPP][temp_region];
		fcc_max_ma = 1200;
		break;
	case OPLUS_CHG_WLS_EPP:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_EPP][temp_region];
		fcc_max_ma = 2800;
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_EPP_PLUS][temp_region];
		fcc_max_ma = 4000;
		break;
	case OPLUS_CHG_WLS_VOOC:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_AIRVOOC][temp_region];
		fcc_max_ma = 3600;
		break;
	case OPLUS_CHG_WLS_SVOOC:
	case OPLUS_CHG_WLS_PD_65W:
		icl_max_ma = dynamic_cfg->iclmax_ma[icl_index][OPLUS_WLS_CHG_MODE_AIRSVOOC][temp_region];
		fcc_max_ma = 3600;
		break;
	default:
		pr_err("Unsupported charging mode(=%d)\n", wls_status->wls_type);
		return;
	}
#ifndef CONFIG_OPLUS_CHG_OOS
	oplus_chg_wls_set_cool_down_iout(wls_dev);
#endif
	if (wls_dev->support_wls_chg_bcc)
		oplus_chg_wls_set_bcc_current_iout(wls_dev);
	oplus_vote(wls_dev->nor_icl_votable, MAX_VOTER, true, icl_max_ma, true);
	oplus_vote(wls_dev->nor_fcc_votable, MAX_VOTER, true, fcc_max_ma, false);
	pr_info("chg_type=%d, temp_region=%d, fcc=%d, icl_max=%d\n",
		wls_status->wls_type, temp_region, fcc_max_ma, icl_max_ma);
}

#ifdef OPLUS_CHG_DEBUG

#define UPGRADE_START 0
#define UPGRADE_FW    1
#define UPGRADE_END   2
struct oplus_chg_fw_head {
	u8 magic[4];
	int size;
};
ssize_t oplus_chg_wls_upgrade_fw_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int rc = 0;

	rc = sprintf(buf, "wireless\n");
	return rc;
}

ssize_t oplus_chg_wls_upgrade_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	u8 temp_buf[sizeof(struct oplus_chg_fw_head)];
	static u8 *fw_buf;
	static int upgrade_step = UPGRADE_START;
	static int fw_index;
	static int fw_size;
	struct oplus_chg_fw_head *fw_head;

start:
	switch (upgrade_step) {
	case UPGRADE_START:
		if (count < sizeof(struct oplus_chg_fw_head)) {
			pr_err("<FW UPDATE>image format error\n");
			return -EINVAL;
		}
		memset(temp_buf, 0, sizeof(struct oplus_chg_fw_head));
		memcpy(temp_buf, buf, sizeof(struct oplus_chg_fw_head));
		fw_head = (struct oplus_chg_fw_head *)temp_buf;
		if (fw_head->magic[0] == 0x02 && fw_head->magic[1] == 0x00 &&
		    fw_head->magic[2] == 0x03 && fw_head->magic[3] == 0x00) {
			fw_size = fw_head->size;
			fw_buf = kzalloc(fw_size, GFP_KERNEL);
			if (fw_buf == NULL) {
				pr_err("<FW UPDATE>alloc fw_buf err\n");
				return -ENOMEM;
			}
			wls_dev->fw_buf = fw_buf;
			wls_dev->fw_size = fw_size;
			pr_err("<FW UPDATE>image header verification succeeded, fw_size=%d\n", fw_size);
			memcpy(fw_buf, buf + sizeof(struct oplus_chg_fw_head), count - sizeof(struct oplus_chg_fw_head));
			fw_index = count - sizeof(struct oplus_chg_fw_head);
			pr_info("<FW UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
			if (fw_index >= fw_size) {
				upgrade_step = UPGRADE_END;
				goto start;
			} else {
				upgrade_step = UPGRADE_FW;
			}
		} else {
			pr_err("<FW UPDATE>image format error\n");
			return -EINVAL;
		}
		break;
	case UPGRADE_FW:
		memcpy(fw_buf + fw_index, buf, count);
		fw_index += count;
		pr_info("<FW UPDATE>Receiving image, fw_size=%d, fw_index=%d\n", fw_size, fw_index);
		if (fw_index >= fw_size) {
			upgrade_step = UPGRADE_END;
			goto start;
		}
		break;
	case UPGRADE_END:
		wls_dev->fw_upgrade_by_buf = true;
		schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, 0);
		fw_buf = NULL;
		upgrade_step = UPGRADE_START;
		break;
	default:
		upgrade_step = UPGRADE_START;
		pr_err("<FW UPDATE>status error\n");
		if (fw_buf != NULL) {
			kfree(fw_buf);
			fw_buf = NULL;
			wls_dev->fw_buf = NULL;
			wls_dev->fw_size = 0;
			wls_dev->fw_upgrade_by_buf = false;
		}
		break;
	}

	return count;
}

int oplus_chg_wls_get_break_sub_crux_info(struct device *dev, char *crux_info)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);

	pr_info("start\n");
	oplus_chg_wls_update_track_info(wls_dev, crux_info, false);
	return 0;
}
int oplus_chg_wls_get_max_wireless_power(struct device *dev)
{
	int max_wls_power = 0;
	int max_wls_base_power = 0;
	int max_wls_r_power = 0;
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (!ocm || !wls_dev || !wls_status || !wls_status->rx_online)
		return 0;
	max_wls_base_power = oplus_chg_wls_get_base_power_max(wls_status->adapter_id);
	max_wls_r_power = oplus_chg_wls_get_r_power(wls_dev, wls_status->adapter_power);
	max_wls_r_power = max_wls_base_power > max_wls_r_power ? max_wls_r_power : max_wls_base_power;
	max_wls_power = max_wls_r_power > wls_dev->wls_power_mw ? wls_dev->wls_power_mw : max_wls_r_power;
	if (wls_status->wls_type != OPLUS_CHG_WLS_SVOOC && wls_status->wls_type != OPLUS_CHG_WLS_PD_65W)
		max_wls_power = min(max_wls_power, WLS_VOOC_PWR_MAX_MW);
	/*pr_info("max_wls_power=%d,max_wls_base_power=%d,max_wls_r_power=%d\n", max_wls_power, max_wls_base_power, max_wls_r_power);*/
	if (wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY)
		max_wls_power = 0;

	return max_wls_power;
}

static ssize_t oplus_chg_wls_path_curr_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int nor_curr_ma, fast_curr_ma;
	ssize_t rc;

	rc = sscanf(buf, "%d,%d", &nor_curr_ma, &fast_curr_ma);
	if (rc < 0) {
		pr_err("can't read input string, rc=%d\n", rc);
		return rc;
	}
	nor_curr_ma = nor_curr_ma /1000;
	fast_curr_ma = fast_curr_ma / 1000;

	switch (wls_dev->force_type) {
	case OPLUS_CHG_WLS_FORCE_TYPE_NOEN:
	case OPLUS_CHG_WLS_FORCE_TYPE_AUTO:
		return -EINVAL;
	case OPLUS_CHG_WLS_FORCE_TYPE_BPP:
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP:
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP_PLUS:
		if (nor_curr_ma >= 0) {
			oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, nor_curr_ma, false);
		} else {
			pr_err("Parameter error\n");
			return -EINVAL;
		}
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_FAST:
		if (fast_curr_ma > 0) {
			wls_status->fastchg_started = true;
			oplus_vote(wls_dev->fcc_votable, DEBUG_VOTER, true, fast_curr_ma, false);
		} else {
			wls_status->fastchg_started = false;
			oplus_vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		}
		if (nor_curr_ma >= 0)
			oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, nor_curr_ma, false);
	}

	return count;
}
#endif /* OPLUS_CHG_DEBUG */

static ssize_t oplus_chg_wls_ftm_test_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct oplus_chg_mod *ocm = dev_get_drvdata(dev);
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	int rc;
	ssize_t index = 0;

	if (oplus_chg_wls_is_usb_present(wls_dev) && !wls_dev->support_tx_boost &&
	    !wls_dev->support_wls_and_tx_boost) {
		pr_info("usb online, can't run rx smt test\n");
		index += sprintf(buf + index, "%d,%s\n", WLS_PATH_RX, "usb_online");
		goto skip_rx_check;
	}

	if (wls_dev->wls_status.rx_present) {
		pr_info("wls online, can't run rx smt test\n");
		index += sprintf(buf + index, "%d,%s\n", WLS_PATH_RX, "wls_online");
		goto skip_rx_check;
	}

	oplus_vote(wls_dev->wrx_en_votable, FTM_TEST_VOTER, true, 1, false);
	rc = oplus_chg_wls_rx_smt_test(wls_dev->wls_rx);
	if (rc != 0)
		index += sprintf(buf + index, "%d,%d\n", WLS_PATH_RX, rc);
	oplus_vote(wls_dev->wrx_en_votable, FTM_TEST_VOTER, true, 0, false);

skip_rx_check:
	if (wls_dev->support_fastchg) {
		rc = oplus_chg_wls_fast_smt_test(wls_dev->wls_fast);
		if (rc != 0)
			index += sprintf(buf + index, "%d,%d\n", WLS_PATH_FAST, rc);
	}

	if (index == 0)
		index += sprintf(buf + index, "OK\r\n");
	else
		index += sprintf(buf + index, "ERROR\r\n");

	return index;
}

static enum oplus_chg_mod_property oplus_chg_wls_props[] = {
	OPLUS_CHG_PROP_ONLINE,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_VOLTAGE_MAX,
	OPLUS_CHG_PROP_VOLTAGE_MIN,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_CURRENT_MAX,
	OPLUS_CHG_PROP_INPUT_CURRENT_NOW,
	OPLUS_CHG_PROP_WLS_TYPE,
	OPLUS_CHG_PROP_FASTCHG_STATUS,
	OPLUS_CHG_PROP_ADAPTER_SID,
	OPLUS_CHG_PROP_ADAPTER_TYPE,
	OPLUS_CHG_PROP_DOCK_TYPE,
	OPLUS_CHG_PROP_WLS_SKEW_CURR,
	OPLUS_CHG_PROP_VERITY,
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_TRX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TRX_CURRENT_NOW,
	OPLUS_CHG_PROP_TRX_STATUS,
	OPLUS_CHG_PROP_TRX_ONLINE,
	OPLUS_CHG_PROP_DEVIATED,
#ifdef OPLUS_CHG_DEBUG
	OPLUS_CHG_PROP_FORCE_TYPE,
	OPLUS_CHG_PROP_PATH_CTRL,
#endif
	OPLUS_CHG_PROP_STATUS_DELAY,
	OPLUS_CHG_PROP_QUIET_MODE,
	OPLUS_CHG_PROP_VRECT_NOW,
	OPLUS_CHG_PROP_TRX_POWER_EN,
	OPLUS_CHG_PROP_TRX_POWER_VOL,
	OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT,
	OPLUS_CHG_PROP_FACTORY_MODE,
	OPLUS_CHG_PROP_TX_POWER,
	OPLUS_CHG_PROP_RX_POWER,
	OPLUS_CHG_PROP_RX_MAX_POWER,
	OPLUS_CHG_PROP_FOD_CAL,
	OPLUS_CHG_PROP_BATT_CHG_ENABLE,
	OPLUS_CHG_PROP_ONLINE_KEEP,
#ifndef CONFIG_OPLUS_CHG_OOS
	OPLUS_CHG_PROP_TX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TX_CURRENT_NOW,
	OPLUS_CHG_PROP_CP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_CP_CURRENT_NOW,
	OPLUS_CHG_PROP_WIRELESS_MODE,
	OPLUS_CHG_PROP_WIRELESS_TYPE,
	OPLUS_CHG_PROP_CEP_INFO,
	OPLUS_CHG_PROP_REAL_TYPE,
	OPLUS_CHG_PROP_COOL_DOWN,
#endif /* CONFIG_OPLUS_CHG_OOS */
	OPLUS_CHG_PROP_RX_VOUT_UVP,
	OPLUS_CHG_PROP_FW_UPGRADING,
	OPLUS_CHG_PROP_BCC_SUPPORT,
	OPLUS_CHG_PROP_BCC_CURRENT,
	OPLUS_CHG_PROP_WLS_BCC_MAX,
	OPLUS_CHG_PROP_WLS_BCC_MIN,
	OPLUS_CHG_PROP_WLS_BCC_STOP,
};

static enum oplus_chg_mod_property oplus_chg_wls_uevent_props[] = {
	OPLUS_CHG_PROP_ONLINE,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_WLS_TYPE,
	OPLUS_CHG_PROP_FASTCHG_STATUS,
	OPLUS_CHG_PROP_ADAPTER_SID,
	OPLUS_CHG_PROP_TRX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TRX_CURRENT_NOW,
	OPLUS_CHG_PROP_TRX_STATUS,
	OPLUS_CHG_PROP_TRX_ONLINE,
	OPLUS_CHG_PROP_QUIET_MODE,
	OPLUS_CHG_PROP_BCC_SUPPORT,
	OPLUS_CHG_PROP_BCC_CURRENT,
	OPLUS_CHG_PROP_WLS_BCC_MAX,
	OPLUS_CHG_PROP_WLS_BCC_MIN,
	OPLUS_CHG_PROP_WLS_BCC_STOP,
};

static struct oplus_chg_exten_prop oplus_chg_wls_exten_props[] = {
#ifdef OPLUS_CHG_DEBUG
	OPLUS_CHG_EXTEN_RWATTR(OPLUS_CHG_EXTERN_PROP_UPGRADE_FW, oplus_chg_wls_upgrade_fw),
	OPLUS_CHG_EXTEN_WOATTR(OPLUS_CHG_EXTERN_PROP_PATH_CURRENT, oplus_chg_wls_path_curr),
#endif
	OPLUS_CHG_EXTEN_ROATTR(OPLUS_CHG_PROP_FTM_TEST, oplus_chg_wls_ftm_test),
};

static int oplus_chg_wls_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc = 0;

	switch (prop) {
	case OPLUS_CHG_PROP_ONLINE:
		pval->intval = wls_status->rx_online;
		break;
	case OPLUS_CHG_PROP_PRESENT:
		pval->intval = wls_status->rx_present;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_NOW:
#ifdef CONFIG_OPLUS_CHG_OOS
		pval->intval = wls_status->vout_mv * 1000;
#else
		pval->intval = wls_status->vout_mv;
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
			if (rc < 0)
				pr_err("can't get trx vol, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_vol_mv;
		}
#endif
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		pval->intval = wls_status->vout_mv * 1000;
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		pval->intval = wls_status->vout_mv * 1000;
		break;
	case OPLUS_CHG_PROP_CURRENT_NOW:
#ifdef CONFIG_OPLUS_CHG_OOS
		pval->intval = wls_status->iout_ma * 1000;
#else
		pval->intval = wls_status->iout_ma;
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
			if (rc < 0)
				pr_err("can't get trx curr, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_curr_ma;
		}
#endif
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		pval->intval = wls_status->iout_ma * 1000;
		break;
	case OPLUS_CHG_PROP_INPUT_CURRENT_NOW:
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor,
						      &pval->intval);
		break;
	case OPLUS_CHG_PROP_WLS_TYPE:
		pval->intval = wls_status->wls_type;
		break;
	case OPLUS_CHG_PROP_FASTCHG_STATUS:
		pval->intval = wls_status->fastchg_started;
		break;
	case OPLUS_CHG_PROP_ADAPTER_SID:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_ADAPTER_TYPE:
		pval->intval = wls_status->adapter_type;
		break;
	case OPLUS_CHG_PROP_DOCK_TYPE:
		pval->intval = wls_status->adapter_id;
		break;
	case OPLUS_CHG_PROP_WLS_SKEW_CURR:
		pval->intval = oplus_chg_wls_get_skewing_current(wls_dev);
		break;
	case OPLUS_CHG_PROP_VERITY:
		pval->intval = wls_status->verity_pass;
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		pval->intval = wls_dev->charge_enable;
		break;
	case OPLUS_CHG_PROP_TRX_VOLTAGE_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
			if (rc < 0)
				pr_err("can't get trx vol, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_vol_mv * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_TRX_CURRENT_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
			if (rc < 0)
				pr_err("can't get trx curr, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_curr_ma * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_TRX_STATUS:
		if (wls_status->trx_online)
			pval->intval = OPLUS_CHG_WLS_TRX_STATUS_CHARGING;
		else if (wls_status->trx_present || wls_status->trx_present_keep)
			pval->intval = OPLUS_CHG_WLS_TRX_STATUS_ENABLE;
		else
			pval->intval = OPLUS_CHG_WLS_TRX_STATUS_DISENABLE;
		break;
	case OPLUS_CHG_PROP_TRX_ONLINE:
		pval->intval = wls_status->trx_present;
		break;
	case OPLUS_CHG_PROP_DEVIATED:
		pval->intval = 0;
		break;
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_PROP_FORCE_TYPE:
		pval->intval = wls_dev->force_type;
		break;
	case OPLUS_CHG_PROP_PATH_CTRL:
		rc = 0;
		break;
#endif
	case OPLUS_CHG_PROP_STATUS_DELAY:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_QUIET_MODE:
		pval->intval = wls_status->switch_quiet_mode;
		break;
	case OPLUS_CHG_PROP_VRECT_NOW:
		pval->intval = wls_status->vrect_mv *1000;
		break;
	case OPLUS_CHG_PROP_TRX_POWER_EN:
	case OPLUS_CHG_PROP_TRX_POWER_VOL:
	case OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_FACTORY_MODE:
		pval->intval = wls_dev->factory_mode;
		break;
	case OPLUS_CHG_PROP_TX_POWER:
		pval->intval = wls_status->tx_pwr_mw;
		break;
	case OPLUS_CHG_PROP_RX_POWER:
		pval->intval = wls_status->rx_pwr_mw;
		break;
	case OPLUS_CHG_PROP_RX_MAX_POWER:
		pval->intval = wls_status->pwr_max_mw;
		break;
	case OPLUS_CHG_PROP_FOD_CAL:
		pval->intval = wls_dev->fod_is_cal;
		break;
	case OPLUS_CHG_PROP_ONLINE_KEEP:
		pval->intval = wls_status->online_keep || wls_status->boot_online_keep;
		break;
	case OPLUS_CHG_PROP_BATT_CHG_ENABLE:
		pval->intval = wls_dev->batt_charge_enable;
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_TX_VOLTAGE_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
			if (rc < 0)
				pr_err("can't get trx vol, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_vol_mv * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_TX_CURRENT_NOW:
		if (wls_status->trx_present) {
			rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
			if (rc < 0)
				pr_err("can't get trx curr, rc=%d\n", rc);
			else
				pval->intval = wls_status->trx_curr_ma * 1000;
		} else {
			pval->intval = 0;
		}
		break;
	case OPLUS_CHG_PROP_CP_VOLTAGE_NOW:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_CP_CURRENT_NOW:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_WIRELESS_MODE:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_WIRELESS_TYPE:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_CEP_INFO:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_REAL_TYPE:
		switch (wls_status->wls_type) {
		case OPLUS_CHG_WLS_BPP:
			pval->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		case OPLUS_CHG_WLS_EPP:
			pval->intval = POWER_SUPPLY_TYPE_USB_PD;
			break;
		case OPLUS_CHG_WLS_EPP_PLUS:
			pval->intval = POWER_SUPPLY_TYPE_USB_PD;
			break;
		case OPLUS_CHG_WLS_VOOC:
		case OPLUS_CHG_WLS_SVOOC:
		case OPLUS_CHG_WLS_PD_65W:
			pval->intval = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		default:
			pr_err("Unsupported charging mode(=%d)\n", wls_status->wls_type);
			pval->intval = POWER_SUPPLY_TYPE_UNKNOWN;
			break;
		}
		break;
	case OPLUS_CHG_PROP_COOL_DOWN:
		pval->intval = wls_status->cool_down;
		break;
	case OPLUS_CHG_PROP_BCC_SUPPORT:
		pval->intval = oplus_wls_check_bcc_support(wls_dev);
		break;
	case OPLUS_CHG_PROP_BCC_CURRENT:
		pval->intval = wls_status->bcc_current / BCC_TO_ICL;
		break;
#endif /* CONFIG_OPLUS_CHG_OOS */
	case OPLUS_CHG_PROP_RX_VOUT_UVP:
		pval->intval = 0;
		break;
	case OPLUS_CHG_PROP_FW_UPGRADING:
		pval->intval = wls_status->fw_upgrading;
		break;
	case OPLUS_CHG_PROP_WLS_BCC_MAX:
		pval->intval = wls_status->wls_bcc_max_curr / BCC_TO_ICL;
		break;
	case OPLUS_CHG_PROP_WLS_BCC_MIN:
		pval->intval = wls_status->wls_bcc_min_curr / BCC_TO_ICL;
		break;
	case OPLUS_CHG_PROP_WLS_BCC_STOP:
		pval->intval = oplus_wls_bcc_get_stop_curr(wls_dev);
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

static int oplus_chg_wls_set_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			const union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_wls *wls_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc = 0;

	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
		if (wls_dev->debug_mode) {
			wls_status->fastchg_started = false;
			rc = oplus_chg_wls_rx_set_vout(wls_dev->wls_rx,
				pval->intval / 1000, 0);
			oplus_vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		} else {
			rc = -EINVAL;
			pr_err("need to open debug mode first\n");
		}
		break;
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
		break;
	case OPLUS_CHG_PROP_CURRENT_MAX:
		if (wls_status->fastchg_started)
			rc = oplus_vote(wls_dev->fcc_votable, MAX_VOTER, true, pval->intval / 1000, false);
		else
			rc = 0;
		break;
	case OPLUS_CHG_PROP_CHG_ENABLE:
		wls_dev->charge_enable = !!pval->intval;
		oplus_vote(wls_dev->rx_disable_votable, DEBUG_VOTER, !wls_dev->charge_enable, 1, false);
		break;
	case OPLUS_CHG_PROP_TRX_ONLINE:
		if (wls_status->wls_type == OPLUS_CHG_WLS_TRX && !pval->intval)
			wls_status->trx_present_keep = true;
		rc = oplus_chg_wls_set_trx_enable(wls_dev, !!pval->intval);
		break;
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_PROP_FORCE_TYPE:
		wls_dev->force_type = pval->intval;
		if (wls_dev->force_type == OPLUS_CHG_WLS_FORCE_TYPE_NOEN) {
			wls_dev->debug_mode = false;
			oplus_vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		} if (wls_dev->force_type == OPLUS_CHG_WLS_FORCE_TYPE_AUTO) {
			wls_dev->debug_mode = false;
			oplus_vote(wls_dev->fcc_votable, DEBUG_VOTER, false, 0, false);
		} else {
			wls_dev->debug_mode = true;
		}
		break;
	case OPLUS_CHG_PROP_PATH_CTRL:
		rc = oplus_chg_wls_path_ctrl(wls_dev, pval->intval);
		break;
#endif
	case OPLUS_CHG_PROP_STATUS_DELAY:
		break;
	case OPLUS_CHG_PROP_QUIET_MODE:
		if (!wls_status->rx_present ||
		 wls_status->adapter_type == WLS_ADAPTER_TYPE_USB ||
		 wls_status->adapter_type == WLS_ADAPTER_TYPE_NORMAL ||
		 wls_status->adapter_type == WLS_ADAPTER_TYPE_UNKNOWN) {
			pr_err("wls not present or isn't op_tx, can't %s quiet mode\n", !!pval->intval ? "enable" : "disable");
			rc = -EINVAL;
			break;
		}
		pr_info("%s quiet mode\n", !!pval->intval ? "enable" : "disable");
		wls_status->switch_quiet_mode = !!pval->intval;
		if (wls_status->switch_quiet_mode != wls_status->quiet_mode
				|| wls_status->quiet_mode_init == false) {
			cancel_delayed_work(&wls_dev->wls_rx_sm_work);
			queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, 0);
		}
		break;
	case OPLUS_CHG_PROP_TRX_POWER_EN:
		rc = oplus_chg_wls_nor_set_boost_en(wls_dev->wls_nor, !!pval->intval);
		break;
	case OPLUS_CHG_PROP_TRX_POWER_VOL:
		rc = oplus_chg_wls_nor_set_boost_vol(wls_dev->wls_nor, pval->intval);
		break;
	case OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT:
		rc = oplus_chg_wls_nor_set_boost_curr_limit(wls_dev->wls_nor, pval->intval);
		break;
	case OPLUS_CHG_PROP_FACTORY_MODE:
		wls_dev->factory_mode = !!pval->intval;
		break;
	case OPLUS_CHG_PROP_FOD_CAL:
		wls_dev->fod_is_cal = false;
		schedule_delayed_work(&wls_dev->fod_cal_work, 0);
		break;
	case OPLUS_CHG_PROP_BATT_CHG_ENABLE:
		wls_dev->batt_charge_enable = !!pval->intval;
		cancel_delayed_work_sync(&wls_dev->wls_rx_sm_work);
		queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, 0);
		break;
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_COOL_DOWN:
		wls_status->cool_down = pval->intval < 0 ? 0 : pval->intval;
		pr_info("set cool down level to %d\n", wls_status->cool_down);
		oplus_chg_wls_config(wls_dev);
		break;
#endif
	case OPLUS_CHG_PROP_RX_VOUT_UVP:
		if (!!pval->intval) {
			oplus_vote(wls_dev->nor_input_disable_votable, UOVP_VOTER, true, 0, false);
		} else {
			oplus_vote(wls_dev->nor_input_disable_votable, UOVP_VOTER, false, 0, false);
		}
		break;
	case OPLUS_CHG_PROP_FW_UPGRADING:
		break;
	case OPLUS_CHG_PROP_BCC_CURRENT:
		if (wls_dev->support_wls_chg_bcc && pval->intval > 0) {
			wls_status->bcc_current = pval->intval * BCC_TO_ICL;
			pr_err("set bcc current to %d\n", wls_status->bcc_current);
			oplus_chg_wls_config(wls_dev);
		}
		break;
	default:
		pr_err("set prop %d is not supported\n", prop);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_chg_wls_prop_is_writeable(struct oplus_chg_mod *ocm,
				enum oplus_chg_mod_property prop)
{
	switch (prop) {
	case OPLUS_CHG_PROP_VOLTAGE_MAX:
	case OPLUS_CHG_PROP_VOLTAGE_MIN:
	case OPLUS_CHG_PROP_CURRENT_MAX:
	case OPLUS_CHG_PROP_CHG_ENABLE:
	case OPLUS_CHG_PROP_TRX_ONLINE:
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_PROP_FORCE_TYPE:
	case OPLUS_CHG_PROP_PATH_CTRL:
#endif
	case OPLUS_CHG_PROP_STATUS_DELAY:
	case OPLUS_CHG_PROP_QUIET_MODE:
	case OPLUS_CHG_PROP_FACTORY_MODE:
#ifdef OPLUS_CHG_DEBUG
	case OPLUS_CHG_EXTERN_PROP_UPGRADE_FW:
	case OPLUS_CHG_EXTERN_PROP_PATH_CURRENT:
#endif
	case OPLUS_CHG_PROP_FOD_CAL:
	case OPLUS_CHG_PROP_BATT_CHG_ENABLE:
#ifndef CONFIG_OPLUS_CHG_OOS
	case OPLUS_CHG_PROP_COOL_DOWN:
#endif
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_wls_mod_desc = {
	.name = "wireless",
	.type = OPLUS_CHG_MOD_WIRELESS,
	.properties = oplus_chg_wls_props,
	.num_properties = ARRAY_SIZE(oplus_chg_wls_props),
	.uevent_properties = oplus_chg_wls_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_wls_uevent_props),
	.exten_properties = oplus_chg_wls_exten_props,
	.num_exten_properties = ARRAY_SIZE(oplus_chg_wls_exten_props),
	.get_property = oplus_chg_wls_get_prop,
	.set_property = oplus_chg_wls_set_prop,
	.property_is_writeable	= oplus_chg_wls_prop_is_writeable,
};

static int oplus_chg_wls_event_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_wls *wls_dev = container_of(nb, struct oplus_chg_wls, wls_event_nb);
	struct oplus_chg_mod *owner_ocm = v;

	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb online\n");
			wls_dev->usb_present = true;
			schedule_delayed_work(&wls_dev->usb_int_work, 0);
			if (wls_dev->wls_ocm)
				oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb offline\n");
			wls_dev->usb_present = false;
			schedule_delayed_work(&wls_dev->usb_int_work, 0);
			if (wls_dev->wls_status.upgrade_fw_pending) {
				wls_dev->wls_status.upgrade_fw_pending = false;
				schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, 0);
			}
		}
		break;
	case OPLUS_CHG_EVENT_PRESENT:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n",
				val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "usb")) {
			pr_info("usb present\n");
			wls_dev->usb_present = true;
			schedule_delayed_work(&wls_dev->usb_int_work, 0);
			if (wls_dev->wls_ocm)
				oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_ADSP_STARTED:
		pr_info("adsp started\n");
		adsp_started = true;
		if (online_pending) {
			online_pending = false;
			schedule_delayed_work(&wls_dev->wls_connect_work, 0);
		} else {
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#ifndef CONFIG_OPLUS_CHARGER_MTK
			if (get_boot_mode() != MSM_BOOT_MODE__CHARGE) {
#else
			if (get_boot_mode() != KERNEL_POWER_OFF_CHARGING_BOOT) {
#endif
#endif
				schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, 0);
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
			} else {
				pr_info("check connect\n");
				wls_dev->wls_status.boot_online_keep = true;
				(void)oplus_chg_wls_rx_connect_check(wls_dev->wls_rx);
			}
#endif
		}
		break;
	case OPLUS_CHG_EVENT_OTG_ENABLE:
		oplus_vote(wls_dev->wrx_en_votable, OTG_EN_VOTER, true, 1, false);
		break;
	case OPLUS_CHG_EVENT_OTG_DISABLE:
		oplus_vote(wls_dev->wrx_en_votable, OTG_EN_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_EVENT_CHARGE_DONE:
		wls_dev->wls_status.chg_done = true;
		break;
	case OPLUS_CHG_EVENT_CLEAN_CHARGE_DONE:
		wls_dev->wls_status.chg_done = false;
		wls_dev->wls_status.chg_done_quiet_mode = false;
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_mod_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_wls *wls_dev = container_of(nb, struct oplus_chg_wls, wls_mod_nb);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	enum oplus_chg_wls_rx_mode rx_mode;
	enum oplus_chg_temp_region temp_region;
	int rc;

	switch(val) {
	case OPLUS_CHG_EVENT_ONLINE:
		pr_info("wls online\n");
		wls_status->boot_online_keep = false;
		if (wls_status->rx_online)
			break;
		wls_status->rx_online = true;
		wls_status->online_keep = false;
		if (adsp_started)
			schedule_delayed_work(&wls_dev->wls_connect_work, 0);
		else
			online_pending = true;
		if (wls_dev->wls_ocm) {
			oplus_chg_global_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_ONLINE);
			oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		schedule_delayed_work(&wls_dev->wls_start_work, 0);
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
		pr_info("wls offline\n");
		wls_status->boot_online_keep = false;
		if (!wls_status->rx_online && !wls_status->rx_present)
			break;
		wls_status->rx_present = false;
		wls_status->rx_online = false;
		schedule_delayed_work(&wls_dev->wls_connect_work, 0);
		cancel_delayed_work(&wls_dev->wls_start_work);
		schedule_delayed_work(&wls_dev->wls_break_work, msecs_to_jiffies(OPLUS_CHG_WLS_BREAK_DETECT_DELAY));
		if (wls_dev->wls_ocm) {
			oplus_chg_global_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_OFFLINE);
			oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_PRESENT:
		pr_info("wls present\n");
		wls_status->boot_online_keep = false;
		if (wls_status->rx_present)
			break;
		wls_status->rx_present = true;
		wls_status->online_keep = false;
		oplus_chg_wls_nor_set_vindpm(wls_dev->wls_nor, WLS_VINDPM_BPP);
		if (wls_dev->wls_ocm) {
			oplus_chg_global_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_PRESENT);
			oplus_chg_mod_changed(wls_dev->wls_ocm);
		}
		break;
	case OPLUS_CHG_EVENT_OP_TRX:
		wls_status->is_op_trx = true;
		break;
	case OPLUS_CHG_EVENT_CHECK_TRX:
		if (wls_status->trx_present) {
			cancel_delayed_work(&wls_dev->wls_trx_sm_work);
			queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_trx_sm_work, 0);
		} else {
			pr_err("trx not present\n");
		}
		break;
	case OPLUS_CHG_EVENT_POWER_CHANGED:
		temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
		if ((BATT_TEMP_LITTLE_COLD <= temp_region) && (temp_region <= BATT_TEMP_LITTLE_WARM)) {
			rc = oplus_chg_wls_choose_fastchg_curve(wls_dev);
			if (rc < 0) {
				pr_err("choose fastchg curve failed, rc = %d\n", rc);
			} else {
				wls_status->fastchg_level = 0;
			}
		}
		oplus_chg_wls_config(wls_dev);
		break;
	case OPLUS_CHG_EVENT_LCD_ON:
		pr_info("lcd on\n");
		wls_status->led_on = true;
		break;
	case OPLUS_CHG_EVENT_LCD_OFF:
		pr_info("lcd off\n");
		wls_status->led_on = false;
		break;
	case OPLUS_CHG_EVENT_CALL_ON:
		pr_info("call on\n");
		oplus_vote(wls_dev->fcc_votable, CALL_VOTER, true, dynamic_cfg->wls_fast_chg_call_on_curr_ma, false);
		break;
	case OPLUS_CHG_EVENT_CALL_OFF:
		pr_info("call off\n");
		oplus_vote(wls_dev->fcc_votable, CALL_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_EVENT_CAMERA_ON:
		pr_info("camera on\n");
		oplus_vote(wls_dev->fcc_votable, CAMERA_VOTER, true, dynamic_cfg->wls_fast_chg_camera_on_curr_ma, false);
		break;
	case OPLUS_CHG_EVENT_CAMERA_OFF:
		pr_info("camera off\n");
		oplus_vote(wls_dev->fcc_votable, CAMERA_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_EVENT_RX_IIC_ERR:
		if (wls_status->rx_present){
			pr_info("Restart the rx disable\n");
			wls_dev->wls_status.online_keep = true;
			oplus_vote(wls_dev->rx_disable_votable, RX_IIC_VOTER, true, 1, false);
			schedule_delayed_work(&wls_dev->rx_iic_restore_work, msecs_to_jiffies(500));
		}
		break;
	case OPLUS_CHG_EVENT_RX_FAST_ERR:
		if (wls_status->rx_present){
			pr_info("Wireless fast_chg fault\n");
			schedule_delayed_work(&wls_dev->fast_fault_check_work, 0);
		}
		break;
	case OPLUS_CHG_EVENT_TX_EPP_CAP:
		oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
		if (rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP_5W
				|| rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP
				|| rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP_PLUS)
			oplus_chg_wls_nor_set_vindpm(wls_dev->wls_nor, WLS_VINDPM_EPP);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_changed_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct oplus_chg_wls *wls_dev = container_of(nb, struct oplus_chg_wls, wls_changed_nb);
	struct oplus_chg_mod *owner_ocm = v;

	switch(val) {
	case OPLUS_CHG_EVENT_CHANGED:
		if (!strcmp(owner_ocm->desc->name, "wireless") && is_wls_psy_available(wls_dev))
			power_supply_changed(wls_dev->wls_psy);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_aes_mutual_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	int i;
	struct oplus_chg_wls *wls_dev;
	struct oplus_chg_wls_status *wls_status;
	struct oplus_chg_cmd *p_cmd;
	wls_third_part_auth_result *aes_auth_result;

	wls_dev = container_of(nb, struct oplus_chg_wls, wls_aes_nb);
	wls_status = &wls_dev->wls_status;

	p_cmd = (struct oplus_chg_cmd *)v;
	if (p_cmd->cmd != CMD_WLS_THIRD_PART_AUTH) {
		pr_err("cmd is not matching, should return\n");
		return NOTIFY_OK;
	}

	if (p_cmd->data_size != sizeof(wls_third_part_auth_result)) {
		pr_err("data_len is not ok, datas is invalid\n");
		return NOTIFY_DONE;
	}

	aes_auth_result = (wls_third_part_auth_result *)(p_cmd->data_buf);
	if (aes_auth_result) {
		wls_status->aes_key_num = aes_auth_result->effc_key_index;
		pr_info("aes_key_num:%d\n", wls_status->aes_key_num);

		memcpy(wls_status->aes_verfity_data.aes_random_num,
			aes_auth_result->aes_random_num,
			sizeof(aes_auth_result->aes_random_num));
		for (i = 0; i < WLS_AUTH_AES_ENCODE_LEN; i++)
			pr_info("aes_random_num[%d]:0x%02x\n",
			    i, (wls_status->aes_verfity_data.aes_random_num)[i]);

		memcpy(wls_status->aes_verfity_data.aes_encode_num,
		    aes_auth_result->aes_encode_num,
		    sizeof(aes_auth_result->aes_encode_num));
		wls_status->aes_verity_data_ok = true;
		for (i = 0; i < WLS_AUTH_AES_ENCODE_LEN; i++)
			pr_info("aes_encode_num[%d]:0x%02x ", i,
			    (wls_status->aes_verfity_data.aes_encode_num)[i]);
	}

	return NOTIFY_OK;
}

static int oplus_chg_wls_init_mod(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_mod_config ocm_cfg = {};
	int rc;

	ocm_cfg.drv_data = wls_dev;
	ocm_cfg.of_node = wls_dev->dev->of_node;

	wls_dev->wls_ocm = oplus_chg_mod_register(wls_dev->dev,
					   &oplus_chg_wls_mod_desc,
					   &ocm_cfg);
	if (IS_ERR(wls_dev->wls_ocm)) {
		pr_err("Couldn't register wls ocm\n");
		rc = PTR_ERR(wls_dev->wls_ocm);
		return rc;
	}
	wls_dev->wls_ocm->notifier = &wls_ocm_notifier;
	wls_dev->wls_mod_nb.notifier_call = oplus_chg_wls_mod_notifier_call;
	rc = oplus_chg_reg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
	if (rc) {
		pr_err("register wls mod notifier error, rc=%d\n", rc);
		goto reg_wls_mod_notifier_err;
	}
	wls_dev->wls_event_nb.notifier_call = oplus_chg_wls_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&wls_dev->wls_event_nb);
	if (rc) {
		pr_err("register wls event notifier error, rc=%d\n", rc);
		goto reg_wls_event_notifier_err;
	}
	wls_dev->wls_changed_nb.notifier_call = oplus_chg_wls_changed_notifier_call;
	rc = oplus_chg_reg_changed_notifier(&wls_dev->wls_changed_nb);
	if (rc) {
		pr_err("register wls changed notifier error, rc=%d\n", rc);
		goto reg_wls_changed_notifier_err;
	}

	wls_dev->wls_aes_nb.notifier_call = oplus_chg_wls_aes_mutual_notifier_call;
	rc = oplus_chg_comm_reg_mutual_notifier(&wls_dev->wls_aes_nb);
	if (rc) {
		pr_err("register wls aes mutual notifier error, rc=%d\n", rc);
		goto reg_wls_aes_mutual_notifier_err;
	}
	return 0;

	oplus_chg_comm_unreg_mutual_notifier(&wls_dev->wls_aes_nb);
reg_wls_aes_mutual_notifier_err:
	oplus_chg_unreg_changed_notifier(&wls_dev->wls_changed_nb);
reg_wls_changed_notifier_err:
	oplus_chg_unreg_event_notifier(&wls_dev->wls_event_nb);
reg_wls_event_notifier_err:
	oplus_chg_unreg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
reg_wls_mod_notifier_err:
	oplus_chg_mod_unregister(wls_dev->wls_ocm);
	return rc;
}

static void oplus_chg_wls_usb_int_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, usb_int_work);

	if (wls_dev->usb_present) {
		oplus_vote(wls_dev->rx_disable_votable, USB_VOTER, true, 1, false);
		if (!wls_dev->support_tx_boost && !wls_dev->support_wls_and_tx_boost)
			(void)oplus_chg_wls_set_trx_enable(wls_dev, false);
		oplus_chg_anon_mod_event(wls_dev->wls_ocm, OPLUS_CHG_EVENT_OFFLINE);
	} else {
		oplus_vote(wls_dev->rx_disable_votable, USB_VOTER, false, 0, false);
	}
}

#define OPLUS_CHG_WLS_START_DETECT_CNT 10
static void oplus_chg_wls_start_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_start_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	static int wpc_online_cnt;

	if (wls_status->rx_online) {
		wpc_online_cnt++;
		if (wpc_online_cnt >= OPLUS_CHG_WLS_START_DETECT_CNT) {
			wpc_online_cnt = 0;
			oplus_chg_wls_update_track_info(wls_dev, NULL, false);
			pr_err("wireless chg start after connect 30s\n");
		} else {
			schedule_delayed_work(&wls_dev->wls_start_work, round_jiffies_relative(msecs_to_jiffies(OPLUS_CHG_WLS_START_DETECT_DELAY)));
		}
	} else {
		pr_err("wireless chg not online within connect 30s,cnt=%d\n", wpc_online_cnt);
		wpc_online_cnt = 0;
	}
}

static void oplus_chg_wls_break_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_break_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (wls_status->rx_online) {
		wls_status->break_count++;
		pr_err("wireless disconnect less than 6s, count=%d\n", wls_status->break_count);
	} else {
		pr_err("wireless disconnect more than 6s, charging stop\n");
		wls_status->break_count = 0;
		oplus_chg_wls_update_track_info(wls_dev, NULL, true);
	}
}

static void oplus_chg_wls_connect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_connect_work);
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_wls_chg_rx *wls_rx = wls_dev->wls_rx;
	struct oplus_wls_chg_normal *wls_nor = wls_dev->wls_nor;
	unsigned long delay_time = jiffies + msecs_to_jiffies(500);
	int skin_temp;
	int rc;

	if (wls_status->rx_online) {
		pr_err("!!!!!wls connect >>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
		if (!wls_dev->rx_wake_lock_on) {
			pr_info("acquire rx_wake_lock\n");
			__pm_stay_awake(wls_dev->rx_wake_lock);
			wls_dev->rx_wake_lock_on = true;
		} else {
			pr_err("rx_wake_lock is already stay awake\n");
		}

#ifndef CONFIG_OPLUS_CHG_OOS
		cancel_delayed_work_sync(&wls_dev->wls_clear_trx_work);
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK
		oplus_vote(wls_dev->wrx_en_votable, WLS_ONLINE_VOTER, true, 1, false);
#endif
		oplus_chg_wls_reset_variables(wls_dev);
		/*
		 * The verity_state_keep flag needs to be cleared immediately
		 * after reconnection to ensure that the subsequent verification
		 * function is normal.
		 */
		wls_status->verity_state_keep = false;
		pr_info("nor_fcc_votable: client:%s, result=%d\n",
			oplus_get_effective_client(wls_dev->nor_fcc_votable),
			oplus_get_effective_result(wls_dev->nor_fcc_votable));
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		oplus_rerun_election(wls_dev->nor_fcc_votable, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 100, false);
		oplus_rerun_election(wls_dev->nor_icl_votable, false);
		/* reset charger status */
		oplus_vote(wls_dev->nor_out_disable_votable, USER_VOTER, true, 1, false);
		oplus_vote(wls_dev->nor_out_disable_votable, USER_VOTER, false, 0, false);
		schedule_delayed_work(&wls_dev->wls_data_update_work, 0);
		schedule_delayed_work(&wls_dev->wls_vout_err_work, 0);
		queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, 0);
		rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
		if (rc < 0) {
			pr_err("can't get skin temp, rc=%d\n", rc);
			skin_temp = 250;
		}
		oplus_chg_strategy_init(&wls_dev->wls_fast_chg_led_off_strategy,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data,
			oplus_chg_get_chg_strategy_data_len(
				dynamic_cfg->wls_fast_chg_led_off_strategy_data,
				CHG_STRATEGY_DATA_TABLE_MAX),
			skin_temp);
		oplus_chg_strategy_init(&wls_dev->wls_fast_chg_led_on_strategy,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data,
			oplus_chg_get_chg_strategy_data_len(
				dynamic_cfg->wls_fast_chg_led_on_strategy_data,
				CHG_STRATEGY_DATA_TABLE_MAX),
			skin_temp);
	} else {
		pr_err("!!!!!wls disconnect <<<<<<<<<<<<<<<<<<<<<<<<<<\n");
		oplus_vote(wls_dev->rx_disable_votable, CONNECT_VOTER, true, 1, false);
		/* should clean all resource first after disconnect */
		(void)oplus_chg_wls_clean_msg(wls_dev);
		(void)oplus_chg_wls_rx_clean_source(wls_rx);
		(void)oplus_chg_wls_nor_clean_source(wls_nor);
		cancel_delayed_work_sync(&wls_dev->wls_data_update_work);
		cancel_delayed_work_sync(&wls_dev->wls_rx_sm_work);
		cancel_delayed_work_sync(&wls_dev->wls_trx_sm_work);
		cancel_delayed_work_sync(&wls_dev->wls_verity_work);
		cancel_delayed_work_sync(&wls_dev->wls_skewing_work);
		cancel_delayed_work_sync(&wls_dev->wls_vout_err_work);
		oplus_wls_cancel_bcc_update_work(wls_dev);
		if (wls_dev->support_fastchg) {
			(void)oplus_chg_wls_rx_set_dcdc_enable(wls_dev->wls_rx, false);
			msleep(10);
			oplus_chg_wls_set_ext_pwr_enable(wls_dev, false);
			oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
#ifdef CONFIG_OPLUS_CHARGER_MTK
			msleep(100);
			oplus_vote(wls_dev->wrx_en_votable, WLS_ONLINE_VOTER, false, 0, false);
#endif
		}
		oplus_chg_wls_nor_set_vindpm(wls_dev->wls_nor, WLS_VINDPM_DEFAULT);
		oplus_chg_wls_reset_variables(wls_dev);
		if (time_is_after_jiffies(delay_time)) {
			delay_time = delay_time - jiffies;
			msleep(jiffies_to_msecs(delay_time));
		}
		wls_dev->wls_fast_chg_led_off_strategy.initialized = false;
		wls_dev->wls_fast_chg_led_on_strategy.initialized = false;
		oplus_vote(wls_dev->rx_disable_votable, CONNECT_VOTER, false, 0, false);
		oplus_chg_wls_reset_variables(wls_dev);
		if (wls_status->online_keep) {
			schedule_delayed_work(&wls_dev->online_keep_remove_work, msecs_to_jiffies(2000));
		} else {
			if (wls_dev->rx_wake_lock_on) {
				pr_info("release rx_wake_lock\n");
				__pm_relax(wls_dev->rx_wake_lock);
				wls_dev->rx_wake_lock_on = false;
			} else {
				pr_err("rx_wake_lock is already relax\n");
			}
		}
	}
}

static int oplus_chg_wls_nor_skin_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_skin_step *skin_step;
	int *skin_step_count;
	int skin_temp;
	int curr_ma;
	int rc;

	rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
	if (rc < 0) {
		pr_err("can't get skin temp, rc=%d\n", rc);
		return rc;
	}

	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_BPP:
		skin_step = &wls_dev->bpp_skin_step;
		skin_step_count = &wls_status->bpp_skin_step;
		break;
	case OPLUS_CHG_WLS_EPP:
#ifdef CONFIG_OPLUS_CHG_OOS
		if (wls_status->led_on && !wls_dev->factory_mode) {
			skin_step = &wls_dev->epp_led_on_skin_step;
			skin_step_count = &wls_status->epp_led_on_skin_step;
		} else {
			skin_step = &wls_dev->epp_skin_step;
			skin_step_count = &wls_status->epp_skin_step;
		}
#else
		skin_step = &wls_dev->epp_skin_step;
		skin_step_count = &wls_status->epp_skin_step;
#endif
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
#ifdef CONFIG_OPLUS_CHG_OOS
		if (wls_status->led_on && !wls_dev->factory_mode) {
			skin_step = &wls_dev->epp_plus_led_on_skin_step;
			skin_step_count = &wls_status->epp_plus_led_on_skin_step;
		} else {
			skin_step = &wls_dev->epp_plus_skin_step;
			skin_step_count = &wls_status->epp_plus_skin_step;
		}
#else
		skin_step = &wls_dev->epp_plus_skin_step;
		skin_step_count = &wls_status->epp_plus_skin_step;
#endif
		break;
	case OPLUS_CHG_WLS_VOOC:
	case OPLUS_CHG_WLS_SVOOC:
	case OPLUS_CHG_WLS_PD_65W:
		skin_step = &wls_dev->epp_plus_skin_step;
		skin_step_count = &wls_status->epp_plus_skin_step;
		break;
	default:
		pr_err("Unsupported charging mode(=%d)\n", wls_status->wls_type);
		return -EINVAL;
	}

	if (skin_step->max_step == 0)
		return 0;

	if ((skin_temp < skin_step->skin_step[*skin_step_count].low_threshold) &&
	    (*skin_step_count > 0)) {
		pr_info("skin_temp=%d, switch to the previous gear\n", skin_temp);
		*skin_step_count = *skin_step_count - 1;
	} else if ((skin_temp > skin_step->skin_step[*skin_step_count].high_threshold) &&
		   (*skin_step_count < (skin_step->max_step - 1))) {
		pr_info("skin_temp=%d, switch to the next gear\n", skin_temp);
		*skin_step_count = *skin_step_count + 1;
	}
	curr_ma = skin_step->skin_step[*skin_step_count].curr_ma;
	oplus_vote(wls_dev->nor_icl_votable, SKIN_VOTER, true, curr_ma, true);

	return 0;
}

static void oplus_chg_wls_fast_fcc_param_init(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	int i;

	for(i = 0; i < fcc_chg->max_step; i++) {
		if (fcc_chg->fcc_step[i].low_threshold > 0)
			fcc_chg->allow_fallback[i] = true;
		else
			fcc_chg->allow_fallback[i] = false;
	}
}

static void oplus_chg_wls_fast_switch_next_step(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	enum oplus_chg_temp_region temp_region;
	u32 batt_vol_max = fcc_chg->fcc_step[wls_status->fastchg_level].vol_max_mv;
	int batt_vol_mv;
	int batt_temp;
	int rc;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm mod not fount\n");
		return;
	}
	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	rc = oplus_chg_wls_get_vbat(wls_dev, &batt_vol_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n");
		return;
	}

	if (fcc_chg->fcc_step[wls_status->fastchg_level].need_wait == 0) {
		if (batt_vol_mv >= batt_vol_max) {
			/* Must delay 1 sec and wait for the batt voltage to drop */
			fcc_chg->fcc_wait_timeout = jiffies + HZ * 5;
		} else {
			fcc_chg->fcc_wait_timeout = jiffies;
		}
	} else {
		/* Delay 1 minute and wait for the temperature to drop */
		fcc_chg->fcc_wait_timeout = jiffies + HZ * 60;
	}

	wls_status->fastchg_level++;
	pr_info("switch to next level=%d\n", wls_status->fastchg_level);
	if (wls_status->fastchg_level >= fcc_chg->max_step) {
		if (batt_vol_mv >= batt_vol_max) {
			pr_info("run normal charge ffc\n");
			wls_status->ffc_check = true;
			oplus_chg_wls_track_set_prop(
				wls_dev, OPLUS_CHG_PROP_STATUS,
				TRACK_WLS_FASTCHG_FULL);
		}
	} else {
		wls_status->wait_cep_stable = true;
		oplus_vote(wls_dev->fcc_votable, FCC_VOTER, true,
		     fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
	}
	wls_status->fastchg_level_init_temp = batt_temp;
	if (batt_vol_mv >= batt_vol_max) {
		fcc_chg->allow_fallback[wls_status->fastchg_level] = false;
	}
}

static void oplus_chg_wls_fast_switch_prev_step(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	wls_status->fastchg_level--;
	pr_info("switch to prev level=%d\n", wls_status->fastchg_level);
	oplus_vote(wls_dev->fcc_votable, FCC_VOTER, true,
	     fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
	wls_status->fastchg_level_init_temp = 0;
	fcc_chg->fcc_wait_timeout = jiffies;
}

static int oplus_chg_wls_fast_temp_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	enum oplus_chg_temp_region temp_region;
	int batt_temp;
	int batt_vol_mv;
	int def_curr_ma;
	int fcc_curr_ma;
	int next_curr_ma;
	/*
	 * We want the temperature to drop when switching to a lower current range.
	 * If the temperature rises by 2 degrees before the next gear begins to
	 * detect temperature, then you should immediately switch to a lower gear.
	 */
	int temp_diff;
	u32 batt_vol_max = fcc_chg->fcc_step[wls_status->fastchg_level].vol_max_mv;
	int rc;

	if (!is_comm_ocm_available(wls_dev)) {
		pr_err("comm mod not fount\n");
		return -ENODEV;
	}

	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);

	if ((temp_region < BATT_TEMP_LITTLE_COLD) || (temp_region > BATT_TEMP_LITTLE_WARM)) {
		pr_info("Abnormal battery temperature, exit fast charge\n");
		oplus_vote(wls_dev->fastchg_disable_votable, FCC_VOTER, true, 1, false);
		return -EPERM;
	}
	rc = oplus_chg_wls_get_vbat(wls_dev, &batt_vol_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return rc;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n");
		return rc;
	}
	def_curr_ma = oplus_get_client_vote(wls_dev->fcc_votable, JEITA_VOTER);
	if (def_curr_ma <= 0)
		def_curr_ma = oplus_get_client_vote(wls_dev->fcc_votable, MAX_VOTER);
	else
		def_curr_ma = min(oplus_get_client_vote(wls_dev->fcc_votable, MAX_VOTER), def_curr_ma);
	pr_err("wkcs: def_curr_ma=%d, max_step=%d\n", def_curr_ma, fcc_chg->max_step);
	fcc_curr_ma = fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma;
	if (wls_status->fastchg_level + 1 < fcc_chg->max_step)
		next_curr_ma = fcc_chg->fcc_step[wls_status->fastchg_level + 1].curr_ma;
	else
		next_curr_ma = 0;
	if (wls_status->fastchg_level_init_temp != 0)
		temp_diff = batt_temp - wls_status->fastchg_level_init_temp;
	else
		temp_diff = 0;

	pr_err("battery temp = %d, vol = %d, level = %d, temp_diff = %d, fcc_curr_ma=%d\n",
		 batt_temp, batt_vol_mv, wls_status->fastchg_level, temp_diff, fcc_curr_ma);

	if (wls_status->fastchg_level == 0) {
		if ((batt_temp > fcc_chg->fcc_step[wls_status->fastchg_level].high_threshold) ||
		    (batt_vol_mv >= batt_vol_max)) {
			oplus_chg_wls_fast_switch_next_step(wls_dev);
			return 0;
		}
		oplus_vote(wls_dev->fcc_votable, FCC_VOTER, true,
			fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
	} else if (wls_status->fastchg_level >= fcc_chg->max_step) {  // switch to pmic
		oplus_vote(wls_dev->fastchg_disable_votable, FCC_VOTER, true, 1, false);
		return -EPERM;
	} else {
		if (batt_vol_mv >= dynamic_cfg->batt_vol_max_mv) {
			pr_info("batt voltage too high, switch next step\n");
			oplus_chg_wls_fast_switch_next_step(wls_dev);
			return 0;
		}
		if ((batt_temp < fcc_chg->fcc_step[wls_status->fastchg_level].low_threshold) &&
		    fcc_chg->allow_fallback[wls_status->fastchg_level] &&
		    (def_curr_ma > fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma)) {
			pr_info("target current too low, switch next step\n");
			oplus_chg_wls_fast_switch_prev_step(wls_dev);
			return 0;
		}
		pr_info("jiffies=%u, timeout=%u, high_threshold=%d, batt_vol_max=%d\n",
			jiffies, fcc_chg->fcc_wait_timeout,
			fcc_chg->fcc_step[wls_status->fastchg_level].high_threshold,
			batt_vol_max);
		if (time_after(jiffies, fcc_chg->fcc_wait_timeout) || (temp_diff > 200) ||
		    (oplus_get_effective_result(wls_dev->fcc_votable) <= next_curr_ma)) {
			if ((batt_temp > fcc_chg->fcc_step[wls_status->fastchg_level].high_threshold) ||
			    (batt_vol_mv >= batt_vol_max)) {
				oplus_chg_wls_fast_switch_next_step(wls_dev);
			}
		}
	}

	return 0;
}

#define CEP_ERR_MAX 12/*(3*2s=12*500ms)*/
#define CEP_OK_MAX 10
#define CEP_WAIT_MAX 40
#define CEP_OK_TIMEOUT_MAX 30
static void oplus_chg_wls_fast_cep_check(struct oplus_chg_wls *wls_dev, int cep)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int curr_ma, cep_curr_ma;
	static int wait_cep_count;
	static int cep_err_count;
	static int cep_ok_count;

	if (!wls_status->wait_cep_stable) {
		/* Insufficient energy only when CEP is positive */
		if (cep < 3) {
			cep_ok_count++;
			cep_err_count = 0;
			if ((cep_ok_count >= CEP_OK_MAX) &&
			    time_after(jiffies, wls_status->cep_ok_wait_timeout) &&
			    oplus_is_client_vote_enabled(wls_dev->fcc_votable, CEP_VOTER)) {
				pr_info("skewing: recovery charging current\n");
				cep_ok_count = 0;
				wls_status->wait_cep_stable = true;
				wls_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
				wait_cep_count = 0;
				oplus_vote(wls_dev->fcc_votable, CEP_VOTER, false, 0, false);
			}
		} else {
			cep_ok_count = 0;
			cep_err_count++;
			if (cep_err_count >= CEP_ERR_MAX) {
				pr_info("skewing: reduce charging current\n");
				cep_err_count = 0;
				wls_status->wait_cep_stable = true;
				wait_cep_count = 0;
				if (oplus_is_client_vote_enabled(wls_dev->fcc_votable, CEP_VOTER))
					cep_curr_ma = oplus_get_client_vote(wls_dev->fcc_votable, CEP_VOTER);
				else
					cep_curr_ma = 0;
				if ((cep_curr_ma > 0) && (cep_curr_ma <= WLS_FASTCHG_CURR_MIN_MA)) {
					pr_info("skewing: Energy is too low, exit fast charge\n");
					oplus_vote(wls_dev->fastchg_disable_votable, CEP_VOTER, true, 1, false);
					wls_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
					wls_status->fast_cep_check = -1;
					return;
				} else {
					curr_ma = wls_status->iout_ma;
					/* Target current is adjusted in 50ma steps*/
					curr_ma = curr_ma - (curr_ma % WLS_FASTCHG_CURR_ERR_MA) - WLS_FASTCHG_CURR_ERR_MA;
					if (curr_ma < WLS_FASTCHG_CURR_MIN_MA)
						curr_ma = WLS_FASTCHG_CURR_MIN_MA;
					oplus_vote(wls_dev->fcc_votable, CEP_VOTER, true, curr_ma, false);
				}
				wls_status->cep_ok_wait_timeout = jiffies + CEP_OK_TIMEOUT_MAX * HZ;
			}
		}
	} else {
		if (wait_cep_count < CEP_WAIT_MAX) {
			wait_cep_count++;
		} else {
			wls_status->wait_cep_stable = false;
			wait_cep_count = 0;
			cep_err_count = 0;
			cep_ok_count = 0;
		}
	}

	return;
}

static int oplus_chg_wls_fast_ibat_check(struct oplus_chg_wls *wls_dev)
{
	int ibat_ma = 0;
	int rc;

	rc = oplus_chg_wls_get_ibat(wls_dev, &ibat_ma);
	if (rc < 0) {
		pr_err("can't get ibat, rc=%d\n");
		return rc;
	}

	if (ibat_ma >= WLS_FASTCHG_CURR_DISCHG_MAX_MA) {
		pr_err("discharge current is too large, exit fast charge\n");
		oplus_vote(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER, true, 1, false);
		return -1;
	}

	return 0;
}

#define IOUT_SMALL_COUNT 30
static int oplus_chg_wls_fast_iout_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int iout_ma = 0;

	iout_ma = wls_status->iout_ma;

	if (iout_ma <= WLS_FASTCHG_IOUT_CURR_MIN_MA) {
		wls_status->iout_ma_conunt++;
		if (wls_status->iout_ma_conunt >= IOUT_SMALL_COUNT) {
			pr_err("iout current is too small, exit fast charge\n");
			oplus_vote(wls_dev->fastchg_disable_votable, IOUT_CURR_VOTER, true, 1, false);
			return -1;
		}
	} else {
		wls_status->iout_ma_conunt = 0;
	}
	return 0;
}

static void oplus_chg_wls_fast_skin_temp_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int skin_temp;
	int curr_ma;
	int rc;

	if (wls_dev->factory_mode) {
		oplus_vote(wls_dev->fcc_votable, SKIN_VOTER, false, 0, false);
		return;
	}

	rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
	if (rc < 0) {
		pr_err("can't get skin temp, rc=%d\n", rc);
		skin_temp = 250;
	}

	if (wls_status->led_on) {
#ifdef CONFIG_OPLUS_CHG_OOS
		if (wls_dev->wls_fast_chg_led_on_strategy.initialized) {
			curr_ma = oplus_chg_strategy_get_data(&wls_dev->wls_fast_chg_led_on_strategy,
				&wls_dev->wls_fast_chg_led_on_strategy.temp_region, skin_temp);
			pr_info("led is on, curr = %d\n", curr_ma);
			oplus_vote(wls_dev->fcc_votable, SKIN_VOTER, true, curr_ma, false);
		}
#else
		oplus_vote(wls_dev->fcc_votable, SKIN_VOTER, false, 0, false);
#endif
	} else {
		if (wls_dev->wls_fast_chg_led_off_strategy.initialized) {
			curr_ma = oplus_chg_strategy_get_data(&wls_dev->wls_fast_chg_led_off_strategy,
				&wls_dev->wls_fast_chg_led_off_strategy.temp_region, skin_temp);
			pr_info("led is off, curr = %d\n", curr_ma);
			oplus_vote(wls_dev->fcc_votable, SKIN_VOTER, true, curr_ma, false);
		}
	}
}

#ifndef CONFIG_OPLUS_CHG_OOS
static int oplus_chg_wls_fast_cool_down_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	int cool_down = wls_status->cool_down;

	if (!wls_dev->factory_mode) {
		if (cool_down > 0 && cool_down <= dynamic_cfg->cool_down_12v_thr) {
			pr_err("cool_down level < %d, exit fast charge\n", dynamic_cfg->cool_down_12v_thr);
			oplus_vote(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER, true, 1, false);
			return -1;
		}
	}

	return 0;
}
#endif /* CONFIG_OPLUS_CHG_OOS */

static void oplus_chg_wls_set_quiet_mode(struct oplus_chg_wls *wls_dev, bool quiet_mode)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int fan_pwm_pulse_fastchg = 0;
	int fan_pwm_pulse_silent = 0;

	if (wls_status->adapter_id == WLS_ADAPTER_MODEL_1) {
		fan_pwm_pulse_fastchg = FAN_PWM_PULSE_IN_FASTCHG_MODE_V01;
		fan_pwm_pulse_silent = FAN_PWM_PULSE_IN_SILENT_MODE_V01;
	} else if (wls_status->adapter_id == WLS_ADAPTER_MODEL_2) {
		fan_pwm_pulse_fastchg = FAN_PWM_PULSE_IN_FASTCHG_MODE_V02;
		fan_pwm_pulse_silent = FAN_PWM_PULSE_IN_SILENT_MODE_V02;
	} else if (wls_status->adapter_id <= WLS_ADAPTER_MODEL_7) {
		fan_pwm_pulse_fastchg = FAN_PWM_PULSE_IN_FASTCHG_MODE_V03_07;
		fan_pwm_pulse_silent = FAN_PWM_PULSE_IN_SILENT_MODE_V03_07;
	} else if (wls_status->adapter_id <= WLS_ADAPTER_MODEL_15) {
		fan_pwm_pulse_fastchg = FAN_PWM_PULSE_IN_FASTCHG_MODE_V08_15;
		fan_pwm_pulse_silent = FAN_PWM_PULSE_IN_SILENT_MODE_V08_15;
	} else {
		fan_pwm_pulse_fastchg = FAN_PWM_PULSE_IN_FASTCHG_MODE_DEFAULT;
		fan_pwm_pulse_silent = FAN_PWM_PULSE_IN_SILENT_MODE_DEFAULT;
	}

	if (wls_status->adapter_id == WLS_ADAPTER_MODEL_0)
		(void)oplus_chg_wls_send_msg(wls_dev, quiet_mode ?
			WLS_CMD_SET_QUIET_MODE : WLS_CMD_SET_NORMAL_MODE, 0xff, 2);
	else
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_FAN_SPEED,
			quiet_mode ? fan_pwm_pulse_silent : fan_pwm_pulse_fastchg, 2);

	(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_LED_BRIGHTNESS,
		quiet_mode ? QUIET_MODE_LED_BRIGHTNESS : 100, 2);
}

static void oplus_chg_wls_check_quiet_mode(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST)
		return;

	if (wls_status->switch_quiet_mode) {
		if ((!wls_status->quiet_mode || wls_status->quiet_mode_init == false)
				&& !wls_status->chg_done_quiet_mode)
			(void)oplus_chg_wls_set_quiet_mode(wls_dev, wls_status->switch_quiet_mode);
	} else {
		if (wls_status->quiet_mode || wls_status->quiet_mode_init == false)
			(void)oplus_chg_wls_set_quiet_mode(wls_dev, wls_status->switch_quiet_mode);
	}
}

static void oplus_chg_wls_check_term_charge(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int curr_ma;
	int skin_temp;
	int rc;

	if (!wls_status->cep_timeout_adjusted &&
	    !wls_status->fastchg_started &&
	    (wls_status->charge_type == WLS_CHARGE_TYPE_FAST) &&
	    ((wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_EPP) ||
	     (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_EPP_PLUS) ||
	     (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_FFC) ||
	     (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_DONE))) {
		oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_CEP_TIMEOUT, 0xff, 2);
	}
	curr_ma = oplus_get_client_vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER);
	rc = oplus_chg_wls_get_skin_temp(wls_dev, &skin_temp);
	if (rc < 0) {
		pr_err("can't get skin temp, rc=%d\n", rc);
		skin_temp = 250;
	}

	if (wls_status->chg_done) {
		if (curr_ma <= 0) {
			pr_info("chg done, set icl to %dma\n", WLS_CURR_STOP_CHG_MA);
			oplus_vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
		}
		if (skin_temp < CHARGE_FULL_FAN_THREOD_LO)
			wls_status->chg_done_quiet_mode = true;
		else if (skin_temp > CHARGE_FULL_FAN_THREOD_HI)
			wls_status->chg_done_quiet_mode = false;
	} else {
		if (curr_ma > 0) {
			oplus_vote(wls_dev->nor_icl_votable, CHG_DONE_VOTER, false, 0, true);
		}
		wls_status->chg_done_quiet_mode = false;
	}
}

static int oplus_chg_wls_fastchg_restart_check(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	enum oplus_chg_temp_region temp_region;
	int batt_temp;
	int real_soc = 100;
	int ibat_ma = 0;
	int rc;

	if (!is_comm_ocm_available(wls_dev)){
		pr_err("comm mod not fount\n");
		return -ENODEV;
	}

	if (!wls_status->fastchg_disable || wls_status->switch_quiet_mode ||
	    !wls_dev->batt_charge_enable)
		return -EPERM;

	rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
	if ((rc < 0) || (real_soc >= dynamic_cfg->fastchg_max_soc)) {
		pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
		return -EPERM;
	}

	temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	if ((temp_region < BATT_TEMP_LITTLE_COLD) || (temp_region > BATT_TEMP_LITTLE_WARM)) {
		pr_info("Abnormal battery temperature, can not restart fast charge\n");
		return -EPERM;
	}

	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n", rc);
		return -EPERM;
	}

	rc = oplus_chg_wls_get_ibat(wls_dev, &ibat_ma);
	if (rc < 0) {
		pr_err("can't get ibat, rc=%d\n", rc);
		return rc;
	}

	if (oplus_is_client_vote_enabled(wls_dev->fastchg_disable_votable, RX_FAST_ERR_VOTER) &&
		(wls_status->fastchg_err_count < 2) &&
		time_is_before_jiffies(wls_status->fastchg_err_timer))
		oplus_vote(wls_dev->fastchg_disable_votable, RX_FAST_ERR_VOTER, false, 0, false);

	if (oplus_is_client_vote_enabled(wls_dev->fastchg_disable_votable, FCC_VOTER)) {
		oplus_vote(wls_dev->fastchg_disable_votable, FCC_VOTER, false, 0, false);
	}

	if (oplus_is_client_vote_enabled(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER) && (ibat_ma < 0))
		oplus_vote(wls_dev->fastchg_disable_votable, BATT_CURR_VOTER, false, 0, false);


	if (oplus_is_client_vote_enabled(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER) &&
	    (wls_status->fastchg_retry_count < 10) &&
	    time_is_before_jiffies(wls_status->fastchg_retry_timer))
		oplus_vote(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER, false, 0, false);

	if (oplus_is_client_vote_enabled(wls_dev->fastchg_disable_votable, QUIET_VOTER))
                oplus_vote(wls_dev->fastchg_disable_votable, QUIET_VOTER, false, 0, false);

#ifndef CONFIG_OPLUS_CHG_OOS
	if (oplus_is_client_vote_enabled(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER)) {
		if ((dynamic_cfg->cool_down_12v_thr > 0
				&& wls_status->cool_down > dynamic_cfg->cool_down_12v_thr)
				|| wls_status->cool_down == 0)
			oplus_vote(wls_dev->fastchg_disable_votable, COOL_DOWN_VOTER, false, 0, false);
	}
#endif

	return 0;
}

#define FALLBACK_COUNT_MAX	3
static int oplus_chg_wls_set_non_ffc_current(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	enum oplus_chg_temp_region temp_region;
	static u32 fallback_count = 0;
	int batt_temp = 0;
	int batt_vol_mv = 0;
	int cv_icl = 0;
	int non_ffc_max_step = 0;
	int rc = 0;
	int i = 0;
	int j = 0;

	if (is_comm_ocm_available(wls_dev)) {
		temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	} else {
		pr_err("not find comm ocm\n");
		return -ENODEV;
	}

	rc = oplus_chg_wls_get_vbat(wls_dev, &batt_vol_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return rc;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n");
		return rc;
	}

	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_BPP:
		i = OPLUS_WLS_CHG_MODE_BPP;
		break;
	case OPLUS_CHG_WLS_EPP:
		i = OPLUS_WLS_CHG_MODE_EPP;
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
		i = OPLUS_WLS_CHG_MODE_EPP_PLUS;
		break;
	case OPLUS_CHG_WLS_VOOC:
		i = OPLUS_WLS_CHG_MODE_AIRVOOC;
		break;
	case OPLUS_CHG_WLS_SVOOC:
	case OPLUS_CHG_WLS_PD_65W:
		i = OPLUS_WLS_CHG_MODE_AIRSVOOC;
		break;
	default:
		i = OPLUS_WLS_CHG_MODE_BPP;
		break;
	}

	non_ffc_max_step = wls_dev->non_ffc_step[i].max_step;

	for (j = 0; j < non_ffc_max_step; j++) {
		if (batt_vol_mv < dynamic_cfg->non_ffc_step[i][j].vol_max_mv) {
			if (wls_status->non_fcc_level < j)
				wls_status->non_fcc_level = j;
			break;
		}
	}
	if (dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level].curr_ma > 0) {
		if ((wls_status->non_fcc_level == non_ffc_max_step - 1) && (wls_status->non_fcc_level > 0)) {
			if (batt_vol_mv <= dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level - 1].vol_max_mv - 50)
				fallback_count++;
			else
				fallback_count = 0;
			if (fallback_count >= FALLBACK_COUNT_MAX) {
				wls_status->non_fcc_level = non_ffc_max_step - 2;
				oplus_vote(wls_dev->nor_icl_votable, NON_FFC_VOTER, true,
					dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level].curr_ma, true);
				pr_err("fallback: non_ffc_icl=%d, j=%d, level=%d\n", dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level].curr_ma, j, wls_status->non_fcc_level);
			} else {
				oplus_vote(wls_dev->nor_icl_votable, NON_FFC_VOTER, true,
					dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level].curr_ma, true);
				pr_err("non_ffc_icl=%d, j=%d, level=%d\n", dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level].curr_ma, j, wls_status->non_fcc_level);
			}
		} else {
			fallback_count = 0;
			oplus_vote(wls_dev->nor_icl_votable, NON_FFC_VOTER, true,
				dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level].curr_ma, true);
			pr_err("non_ffc_icl=%d, j=%d, level=%d\n", dynamic_cfg->non_ffc_step[i][wls_status->non_fcc_level].curr_ma, j, wls_status->non_fcc_level);
		}
	}

	for (j = 0; j < wls_dev->cv_step[i].max_step; j++) {
		cv_icl = dynamic_cfg->cv_step[i][j].curr_ma;
		if (temp_region < BATT_TEMP_WARM)
			break;
	}
	if (j > wls_dev->cv_step[i].max_step - 1)
		j = wls_dev->cv_step[i].max_step - 1;
	if (batt_vol_mv >= dynamic_cfg->cv_step[i][j].vol_max_mv && cv_icl > 0) {
		oplus_vote(wls_dev->nor_icl_votable, CV_VOTER, true, cv_icl, true);
	}
	if (batt_vol_mv <= dynamic_cfg->cv_step[i][j].vol_max_mv - 50) {
		oplus_vote(wls_dev->nor_icl_votable, CV_VOTER, false, 0, false);
	}

	return 0;
}

static int oplus_chg_wls_get_third_adapter_ext_cmd_p_id(struct oplus_chg_wls *wls_dev)
{
	int rc = 0;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int try_count = 0;
	char buf[3] = {0};
	int soc = 0, temp = 0;

	if (wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY)
		return rc;

	if (!wls_status->tx_extern_cmd_done) {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_EXTERN_CMD, 0xff, 5);
		if (rc < 0) {
			pr_err("can't extern cmd, rc=%d\n", rc);
			wls_status->tx_extern_cmd_done = false;
			return rc;
		}
		msleep(200);
	}

	if (!wls_status->tx_product_id_done) {
		buf[0] = (wls_dev->wls_phone_id >> 8) & 0xff;
		buf[1] = wls_dev->wls_phone_id & 0xff;
		pr_err("wls_phone_id=0x%x\n", wls_dev->wls_phone_id);
		do {
			rc = oplus_chg_wls_send_data(wls_dev, WLS_CMD_GET_PRODUCT_ID, buf, 5);
			if (rc < 0) {
				if (rc != -EAGAIN)
					try_count++;
				msleep(200);
			}
		} while (rc < 0 && try_count < 2 && wls_status->rx_online);
		if (rc < 0 || !wls_status->rx_online) {
			pr_err("can't get product id, rc=%d\n", rc);
			wls_status->tx_product_id_done = false;
			return rc;
		}
		msleep(200);
	}

	rc = oplus_chg_wls_get_ui_soc(wls_dev, &soc);
	if (rc < 0) {
		pr_err("can't get ui soc, rc=%d\n", rc);
		return rc;
	}
	rc = oplus_chg_wls_get_batt_temp(wls_dev, &temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n", rc);
		return rc;
	}

	buf[0] = (temp >> 8) & 0xff;
	buf[1] = temp & 0xff;
	buf[2] = soc & 0xff;
	pr_info("soc:%d, temp:%d\n", soc, temp);
	oplus_chg_wls_send_data(wls_dev, WLS_CMD_SEND_BATT_TEMP_SOC, buf, 0);

	msleep(1000);
	return rc;
}

static int oplus_chg_wls_get_third_adapter_v_id(struct oplus_chg_wls *wls_dev)
{
	int rc = 0;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (wls_status->adapter_id != WLS_ADAPTER_THIRD_PARTY)
		return rc;

	if (!wls_status->verify_by_aes) {
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_VENDOR_ID, 0xff, 5);
		if (rc < 0) {
			pr_err("can't vendor id, rc=%d\n", rc);
			wls_status->verify_by_aes = false;
			wls_status->adapter_type = WLS_ADAPTER_TYPE_UNKNOWN;
			return rc;
		}
	}

	return rc;
}

#define WAIT_FOR_2CEP_INTERVAL_MS	300
static int oplus_chg_wls_rx_handle_state_default(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
#ifdef WLS_SUPPORT_OPLUS_CHG
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	int real_soc = 100;
#endif
	enum oplus_chg_temp_region temp_region;
	enum oplus_chg_wls_rx_mode rx_mode;
	int rc;

#ifdef WLS_SUPPORT_OPLUS_CHG
	oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
	oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
	switch (rx_mode) {
	case OPLUS_CHG_WLS_RX_MODE_EPP_5W:
		wls_status->epp_5w = true;
	case OPLUS_CHG_WLS_RX_MODE_EPP:
		wls_status->epp_working = true;
		wls_status->wls_type = OPLUS_CHG_WLS_EPP;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		goto out;
	case OPLUS_CHG_WLS_RX_MODE_EPP_PLUS:
		wls_status->epp_working = true;
		wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		goto out;
	default:
		break;
	}

	if (is_comm_ocm_available(wls_dev)) {
		temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	} else {
		pr_err("not find comm ocm\n");
		temp_region = BATT_TEMP_UNKNOWN;
	}

	oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, true, 0, false);

	if (wls_status->verity_pass) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INDENTIFY_ADAPTER, 0xff, 0);
		msleep(WAIT_FOR_2CEP_INTERVAL_MS);
	}

	oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 750, true);
	if (wls_status->verity_pass && wls_status->adapter_type == WLS_ADAPTER_TYPE_UNKNOWN)
		rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INDENTIFY_ADAPTER, 0xff, 5);
	else
		rc = -EINVAL;
	if (rc < 0 && wls_status->adapter_type == WLS_ADAPTER_TYPE_UNKNOWN) {
		pr_info("can't get adapter type, rc=%d\n", rc);
		wls_status->wls_type = OPLUS_CHG_WLS_UNKNOWN;
	} else {
		switch (wls_status->adapter_type) {
		case WLS_ADAPTER_TYPE_USB:
		case WLS_ADAPTER_TYPE_NORMAL:
			wls_status->wls_type = OPLUS_CHG_WLS_BPP;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
			goto out;
		/*case WLS_ADAPTER_TYPE_EPP:
			rc = oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
			if (rc < 0) {
				pr_err("get rx mode error, rc=%d\n", rc);
				wls_status->wls_type = OPLUS_CHG_WLS_EPP;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
				goto out;
			}
			if (rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP_PLUS) {
				wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			} else if (rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP){
				wls_status->wls_type = OPLUS_CHG_WLS_EPP;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
			} else {
				wls_status->wls_type = OPLUS_CHG_WLS_BPP;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
			}
			goto out;*/
		case WLS_ADAPTER_TYPE_VOOC:
			rc = oplus_chg_wls_get_third_adapter_v_id(wls_dev);
			if (rc < 0)
				goto out;
			if (wls_dev->support_fastchg) {
				wls_status->wls_type = OPLUS_CHG_WLS_VOOC;
			} else {
				wls_status->epp_working = true;
				wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			}
			if (temp_region > BATT_TEMP_COLD && temp_region < BATT_TEMP_WARM) {
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			} else {
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
			}
			goto out;
		case WLS_ADAPTER_TYPE_SVOOC:
		case WLS_ADAPTER_TYPE_PD_65W:
			rc = oplus_chg_wls_get_third_adapter_v_id(wls_dev);
			if (rc < 0)
				goto out;

			if (wls_dev->support_fastchg) {
				if (wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC)
					wls_status->wls_type = OPLUS_CHG_WLS_SVOOC;
				else
					wls_status->wls_type = OPLUS_CHG_WLS_PD_65W;
				if (wls_status->switch_quiet_mode)
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
				else if (!wls_dev->batt_charge_enable)
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
				else
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
				/*
		 		* The fan of the 30w wireless charger cannot be reset automatically.
		 		* Actively turn on the fan once when wireless charging is connected.
		 		*/
				if (wls_status->adapter_id == WLS_ADAPTER_MODEL_0 &&
				    !wls_status->quiet_mode_init &&
				    !wls_status->switch_quiet_mode)
					(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_SET_NORMAL_MODE, 0xff, 2);

				rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
				if ((rc < 0) || (real_soc >= dynamic_cfg->fastchg_max_soc)) {
					pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
				}
				if ((temp_region < BATT_TEMP_LITTLE_COLD) || (temp_region > BATT_TEMP_LITTLE_WARM)) {
					pr_err("Abnormal battery temperature, temp_region=%d\n", temp_region);
					oplus_vote(wls_dev->fastchg_disable_votable, QUIET_VOTER, true, 0, false);
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
				}
			} else {
				wls_status->epp_working = true;
				wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			}
			if (wls_dev->debug_mode)
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
			goto out;
		default:
			wls_status->wls_type = OPLUS_CHG_WLS_UNKNOWN;
		}
	}
#endif
	rc = oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
	if (rc < 0) {
		pr_err("get rx mode error, rc=%d\n", rc);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		goto out;
	}

	switch (rx_mode) {
	case OPLUS_CHG_WLS_RX_MODE_BPP:
		wls_status->wls_type = OPLUS_CHG_WLS_BPP;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	case OPLUS_CHG_WLS_RX_MODE_EPP_5W:
		wls_status->epp_5w = true;
	case OPLUS_CHG_WLS_RX_MODE_EPP:
		wls_status->epp_working = true;
		wls_status->wls_type = OPLUS_CHG_WLS_EPP;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		break;
	case OPLUS_CHG_WLS_RX_MODE_EPP_PLUS:
		wls_status->epp_working = true;
		wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		break;
	default:
		wls_status->wls_type = OPLUS_CHG_WLS_BPP;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	}

out:
	if (is_comm_ocm_available(wls_dev))
		oplus_chg_comm_update_config(wls_dev->comm_ocm);
	oplus_chg_wls_config(wls_dev);
	return 0;
}

static int oplus_chg_wls_rx_exit_state_default(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_BPP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_EPP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_EPP_PLUS:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

#define WLS_CURR_OP_TRX_CHG_MA	600
static int oplus_chg_wls_rx_enter_state_bpp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP];
	int wait_time_ms = 0;
#ifdef WLS_SUPPORT_OPLUS_CHG
	int rc = 0;

	switch(wls_status->state_sub_step) {
	case 0:
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1200, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 750, true);
		if (wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) {
			/* (void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, 5000, 0); */
		} else {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_ID, 0xff, 5);
			if (rc < 0)
				pr_info("can't get tx id, it's not op tx\n");
		}
		wait_time_ms = 100;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if (wls_status->is_op_trx) {
			oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, WLS_CURR_OP_TRX_CHG_MA, false);
			oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, false);
			oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
			wls_status->state_sub_step = 0;
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		} else {
			wls_status->state_sub_step = 2;
			wait_time_ms = 100;
		}
		wls_status->adapter_info_cmd_count = 0;
		break;
	case 2:
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#else
	switch (wls_status->state_sub_step) {
	case 0:
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 200, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 2;
		break;
	case 2:
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 400, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 3;
		break;
	case 3:
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 700, false);
		wait_time_ms = 300;
		wls_status->state_sub_step = 4;
		break;
	case 4:
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1000, true);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#endif
	return wait_time_ms;
}

#define A1_COUNT_MAX	30
static int oplus_chg_wls_rx_handle_state_bpp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int iout_ma, vout_mv, vrect_mv;
	int wait_time_ms = 4000;
#ifdef WLS_SUPPORT_OPLUS_CHG
	int rc;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	int real_soc = 100;
	enum oplus_chg_temp_region temp_region;
	enum oplus_chg_wls_rx_mode rx_mode;

	if (is_comm_ocm_available(wls_dev)) {
		temp_region = oplus_chg_comm_get_temp_region(wls_dev->comm_ocm);
	} else {
		pr_err("not find comm ocm\n");
		temp_region = BATT_TEMP_UNKNOWN;
	}

	if (wls_dev->factory_mode && wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	} else {
		if (wls_status->adapter_info_cmd_count < A1_COUNT_MAX && wls_status->verity_pass) {
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INDENTIFY_ADAPTER, 0xff, 0);
			wait_time_ms = 1000;
			wls_status->adapter_info_cmd_count++;
		}
	}
#endif

	if (wls_dev->batt_charge_enable) {
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

#ifdef WLS_SUPPORT_OPLUS_CHG
	switch (wls_status->adapter_type) {
	case WLS_ADAPTER_TYPE_EPP:
		rc = oplus_chg_wls_rx_get_rx_mode(wls_dev->wls_rx, &rx_mode);
		if (rc < 0) {
			pr_err("get rx mode error, rc=%d\n", rc);
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			break;
		}
		if (rx_mode == OPLUS_CHG_WLS_RX_MODE_EPP_PLUS) {
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		} else {
			wls_status->wls_type = OPLUS_CHG_WLS_EPP;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		}
		break;
	case WLS_ADAPTER_TYPE_VOOC:
		rc = oplus_chg_wls_get_third_adapter_v_id(wls_dev);
		if (rc < 0)
			goto out;

		if (wls_dev->support_fastchg) {
			wls_status->wls_type = OPLUS_CHG_WLS_VOOC;
		} else {
			wls_status->epp_working = true;
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
		}
		if (temp_region > BATT_TEMP_COLD && temp_region < BATT_TEMP_WARM) {
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
		} else {
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_BPP;
		}
		break;
	case WLS_ADAPTER_TYPE_SVOOC:
	case WLS_ADAPTER_TYPE_PD_65W:
		rc = oplus_chg_wls_get_third_adapter_v_id(wls_dev);
		if (rc < 0)
			goto out;

		if (wls_dev->support_fastchg) {
			if (wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC)
				wls_status->wls_type = OPLUS_CHG_WLS_SVOOC;
			else
				wls_status->wls_type = OPLUS_CHG_WLS_PD_65W;
			if (wls_status->switch_quiet_mode)
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
			else if (!wls_dev->batt_charge_enable)
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
			else
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
			rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
			if ((rc < 0) || (real_soc >= dynamic_cfg->fastchg_max_soc)) {
				pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			}
			if ((temp_region < BATT_TEMP_COLD) || (temp_region > BATT_TEMP_WARM)) {
				pr_err("Abnormal battery temperature, temp_region=%d\n", temp_region);
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			}
		} else {
			wls_status->epp_working = true;
			wls_status->wls_type = OPLUS_CHG_WLS_EPP_PLUS;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		}
		if (wls_dev->debug_mode)
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		break;
	default:
		goto out;
	}
	wait_time_ms = 100;
	oplus_chg_wls_config(wls_dev);
out:
#endif
	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_set_non_ffc_current(wls_dev);

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);

	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_bpp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_EPP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_EPP_PLUS:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
		break;
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEFAULT:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_DEFAULT;
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

#define SKEWING_WORK_DELAY	3000
static int oplus_chg_wls_rx_enter_state_epp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP];
	int wait_time_ms = 0;
	int iout_ma = 0;
#ifdef WLS_SUPPORT_OPLUS_CHG
	switch(wls_status->state_sub_step) {
	case 0:
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2800, false);
		if (wls_status->adapter_type == WLS_ADAPTER_TYPE_VOOC ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W) {
			oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 100, false);
			oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, true, 0, false);
			oplus_chg_wls_rx_get_iout(wls_dev->wls_rx, &iout_ma);
			if (iout_ma > 200) {
				wait_time_ms = 100;
				break;
			}
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->vooc_vol_mv, 0);
			if(wls_dev->support_fastchg)
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
			else
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0x01, -1);
			wls_status->fod_parm_for_fastchg = true;
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->vooc_vol_mv, 15);

			if (wls_status->pwr_max_mw > 0) {
				oplus_vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, true,
				     wls_status->pwr_max_mw * 1000 /dynamic_cfg->vooc_vol_mv, false);
			}
		}
		wait_time_ms = 100;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if (wls_status->adapter_type == WLS_ADAPTER_TYPE_VOOC
				|| wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC
				|| wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W) {
			if (wls_status->vout_mv > 11500) {
				oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
				oplus_chg_wls_nor_set_vindpm(wls_dev->wls_nor, WLS_VINDPM_AIRVOOC);
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
				wls_status->state_sub_step = 0;
				wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
			} else {
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, true);
			}
		} else {
			if (wls_status->epp_5w || wls_status->vout_mv < 8000)
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 450, true);
			else
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);

			wls_status->state_sub_step = 0;
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
		}
		wait_time_ms = 100;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#else
switch(wls_status->state_sub_step) {
    case 0:
        oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 1;
        break;
    case 1:
        oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
        oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 200, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 2;
        break;
    case 2:
        oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 3;
        break;
    case 3:
        oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 900, false);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP;
        break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#endif
	if (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_EPP)
		schedule_delayed_work(&wls_dev->wls_skewing_work, msecs_to_jiffies(SKEWING_WORK_DELAY));

	return wait_time_ms;
}

static int oplus_chg_wls_rx_handle_state_epp(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP];
	int iout_ma, icl_ma, vout_mv, vrect_mv;
	int nor_input_curr_ma;
	int wait_time_ms = 4000;
	int rc = 0;

#ifdef WLS_SUPPORT_OPLUS_CHG
	if (wls_dev->factory_mode && wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	} else {
		oplus_chg_wls_check_quiet_mode(wls_dev);
	}
#endif

	if (wls_dev->batt_charge_enable) {
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	if (wls_status->epp_5w || wls_status->vout_mv < 8000) {
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 450, true);
		if (!wls_status->epp_5w)
			wait_time_ms = 500;
	} else {
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
	}

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_set_non_ffc_current(wls_dev);

	if ((wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) &&
	     !wls_status->verity_started) {
		icl_ma = oplus_get_effective_result(wls_dev->nor_icl_votable);
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor, &nor_input_curr_ma);
		if (rc < 0)
			nor_input_curr_ma = wls_status->iout_ma;
		if (icl_ma - nor_input_curr_ma < 300)
			schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);
	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_epp(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_enter_state_epp_plus(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS];
	int wait_time_ms = 0;
	int iout_ma = 0;
#ifdef WLS_SUPPORT_OPLUS_CHG
	switch(wls_status->state_sub_step) {
	case 0:
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 4000, false);
		if (wls_status->adapter_type == WLS_ADAPTER_TYPE_VOOC ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC ||
		    wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W) {
			oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 100, false);
			oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, true, 0, true);
			oplus_chg_wls_rx_get_iout(wls_dev->wls_rx, &iout_ma);
			if (iout_ma > 200) {
				wait_time_ms = 100;
				break;
			}
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->vooc_vol_mv, 0);
			if(wls_dev->support_fastchg)
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
			else
				(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0x01, -1);
			wls_status->fod_parm_for_fastchg = true;
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, dynamic_cfg->vooc_vol_mv, 15);
			if (wls_status->pwr_max_mw > 0) {
				oplus_vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, true,
				     wls_status->pwr_max_mw * 1000 / dynamic_cfg->vooc_vol_mv, false);
			} else {
				/*vote a default max icl for vooc*/
				oplus_vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, true,
					12000 * 1000 / dynamic_cfg->vooc_vol_mv, false);
			}
		}
		wait_time_ms = 100;
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if (wls_status->adapter_type == WLS_ADAPTER_TYPE_VOOC
				|| wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC
				|| wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W) {
			if (wls_status->vout_mv > 11500) {
				oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
				oplus_chg_wls_nor_set_vindpm(wls_dev->wls_nor, WLS_VINDPM_AIRVOOC);
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
				wls_status->state_sub_step = 0;
				wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
			} else {
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, true);
			}
		} else {
			if (wls_status->vout_mv < 8000)
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 400, true);
			else
				oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
			wls_status->state_sub_step = 0;
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
		}

		wait_time_ms = 100;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#else
switch(wls_status->state_sub_step) {
    case 0:
        oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 3000, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 1;
        break;
    case 1:
        oplus_chg_wls_nor_set_aicl_enable(wls_dev->wls_nor, true);
        oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 200, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 2;
        break;
    case 2:
        oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 3;
        break;
    case 3:
        oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 900, false);
        wait_time_ms = 300;
        wls_status->state_sub_step = 4;
        break;
    case 4:
        oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1250, false);
		wls_status->state_sub_step = 0;
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_EPP_PLUS;
        break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}
#endif
	if (wls_status->current_rx_state == OPLUS_CHG_WLS_RX_STATE_EPP_PLUS)
		schedule_delayed_work(&wls_dev->wls_skewing_work, msecs_to_jiffies(SKEWING_WORK_DELAY));
	return wait_time_ms;
}

static int oplus_chg_wls_rx_handle_state_epp_plus(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	int icl_max_ma = wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS];
	int iout_ma, icl_ma, vout_mv, vrect_mv;
	int nor_input_curr_ma;
	int wait_time_ms = 4000;
	int rc = 0;

#ifdef WLS_SUPPORT_OPLUS_CHG
	if (wls_dev->factory_mode && wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	} else {
		oplus_chg_wls_check_quiet_mode(wls_dev);
	}
#endif

	if (wls_status->adapter_type == WLS_ADAPTER_TYPE_VOOC
			|| wls_status->adapter_type == WLS_ADAPTER_TYPE_SVOOC
			|| wls_status->adapter_type == WLS_ADAPTER_TYPE_PD_65W) {
		if (wls_status->pwr_max_mw > 0) {
			oplus_vote(wls_dev->nor_icl_votable, RX_MAX_VOTER, true,
				wls_status->pwr_max_mw * 1000 / dynamic_cfg->vooc_vol_mv, false);
		}
	}

	if (wls_dev->batt_charge_enable) {
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	if (wls_status->vout_mv < 8000) {
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 400, true);
		wait_time_ms = 500;
	} else {
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, icl_max_ma, true);
	}

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_set_non_ffc_current(wls_dev);

	if ((wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) &&
	     !wls_status->verity_started) {
		icl_ma = oplus_get_effective_result(wls_dev->nor_icl_votable);
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor, &nor_input_curr_ma);
		if (rc < 0)
			nor_input_curr_ma = wls_status->iout_ma;
		if (icl_ma - nor_input_curr_ma < 300)
			schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);
	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_epp_plus(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

#define CP_OPEN_OFFSET 100
#define CURR_ERR_COUNT_MAX	200
static int oplus_chg_wls_rx_enter_state_fast(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_fcc_step *fcc_chg = &wls_dev->wls_fcc_step;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	int vbat_mv, vout_mv, batt_num, batt_temp;
	static int curr_err_count;
	int delay_ms = 0;
	int real_soc;
	int iout_max_ma;
	int scale_factor;
	int rc;
	int iout_ma = 0;
	int i = 0;

	if (wls_status->switch_quiet_mode
			&& wls_status->state_sub_step > OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_FAST) {
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		return 0;
	}

	if (!wls_dev->batt_charge_enable) {
		if (wls_status->state_sub_step > OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_FAST) {
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
			return 0;
		}
	} else {
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	}

	rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
	if ((rc < 0) || (real_soc >= dynamic_cfg->fastchg_max_soc)) {
		pr_err("can't get real soc or soc is too high, rc=%d\n", rc);
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	rc = oplus_chg_wls_get_vbat(wls_dev, &vbat_mv);
	if (rc < 0) {
		if (printk_ratelimit())
			pr_err("can't get vbat, rc=%d\n", rc);
		delay_ms = 100;
		return delay_ms;
	}

	rc = oplus_chg_wls_get_batt_temp(wls_dev, &batt_temp);
	if (rc < 0) {
		pr_err("can't get batt temp, rc=%d\n", rc);
		delay_ms = 100;
		return delay_ms;
	}

	pr_err("state_sub_step=%d\n", wls_status->state_sub_step);

	switch(wls_status->state_sub_step) {
	case OPLUS_CHG_WLS_FAST_SUB_STATE_INIT:
		rc = oplus_chg_wls_choose_fastchg_curve(wls_dev);
		if (rc < 0) {
			pr_err("choose fastchg curve failed, rc = %d\n", rc);
			return 0;
		}
		oplus_wls_bcc_parms_init(wls_dev);
		oplus_wls_bcc_get_curve(wls_dev);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 100, false);
		oplus_rerun_election(wls_dev->nor_icl_votable, false);
		if (wls_status->vout_mv < 9000)
			oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, true, 0, false);
		oplus_chg_wls_rx_get_iout(wls_dev->wls_rx, &iout_ma);
		if (iout_ma > 200) {
			delay_ms = 100;
			break;
		}
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		wls_status->fod_parm_for_fastchg = true;
		wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_FAST;
		oplus_chg_wls_get_verity_data(wls_dev);
		delay_ms = 100;
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_FAST:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST)
			return 500;
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		oplus_chg_wls_nor_set_vindpm(wls_dev->wls_nor, WLS_VINDPM_AIRSVOOC);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, WLS_CURR_WAIT_FAST_MA, false);
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
		if (wls_status->rx_online)
			(void)oplus_chg_wls_set_ext_pwr_enable(wls_dev, true);
		/*need about 600ms for 9415*/
		msleep(600);
		if (wls_status->rx_online)
			(void)oplus_chg_wls_rx_set_dcdc_enable(wls_dev->wls_rx, true);
		curr_err_count = 0;
		if (wls_status->rx_online && wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY) {
			rc = oplus_chg_wls_get_third_adapter_ext_cmd_p_id(wls_dev);
			if (rc < 0) {
				if (!wls_status->rx_online)
					return 0;
				pr_err("get product id fail\n");
				wls_status->online_keep = true;
				oplus_vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
				schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
				return 500;
			}
		}
		if (wls_status->iout_ma < 100) {
			wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_IOUT;
			curr_err_count++;
			delay_ms = 100;
		} else {
			wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_VOUT;
			curr_err_count = 0;
			delay_ms = 0;
		}
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_IOUT:
		if (wls_status->iout_ma < 100) {
			if (curr_err_count == CURR_ERR_COUNT_MAX / 2)
				oplus_chg_wls_nor_set_aicl_rerun(wls_dev->wls_nor);
			curr_err_count++;
			delay_ms = 100;
		} else {
			curr_err_count = 0;
			delay_ms = 0;
			wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_VOUT;
		}
		if (curr_err_count > CURR_ERR_COUNT_MAX) {
			curr_err_count = 0;
			wls_status->state_sub_step = 0;
			oplus_vote(wls_dev->fastchg_disable_votable, CURR_ERR_VOTER, true, 1, false);
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			delay_ms = 0;
		}
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_VOUT:
		rc = oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx, 4 * vbat_mv + CP_OPEN_OFFSET + 200, 1000, 50);
		if (rc < 0) {
			pr_err("can't set vout to %d, rc=%d\n", 4 * vbat_mv + CP_OPEN_OFFSET, rc);
			wls_status->state_sub_step = 0;
			wls_status->fastchg_retry_count++;
			wls_status->fastchg_retry_timer = jiffies + (unsigned long)(300 * HZ); //5 min
			oplus_vote(wls_dev->fastchg_disable_votable, STARTUP_CEP_VOTER, true, 1, false);
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			delay_ms = 0;
			break;
		}
		wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_CHECK_IOUT;
		delay_ms = 0;
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_CHECK_IOUT:
		rc = oplus_chg_wls_rx_get_vout(wls_dev->wls_rx, &vout_mv);
		if (rc < 0) {
			pr_err("can't get vout, rc=%d\n", rc);
			delay_ms = 100;
			break;
		}
		if (vout_mv < vbat_mv * 4 + CP_OPEN_OFFSET) {
			pr_err("rx vout(=%d) < %d, retry\n", vout_mv, vbat_mv * 4 + CP_OPEN_OFFSET);
			(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx, 4 * vbat_mv + CP_OPEN_OFFSET + 300, 1000, 10);
			delay_ms = 0;
			break;
		}
		wls_status->state_sub_step = OPLUS_CHG_WLS_FAST_SUB_STATE_START;
		delay_ms = 0;
		break;
	case OPLUS_CHG_WLS_FAST_SUB_STATE_START:
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, true);
		(void)oplus_chg_wls_fast_start(wls_dev->wls_fast);
		rc = oplus_chg_wls_get_real_soc(wls_dev, &real_soc);
		if (rc < 0) {
			pr_err("can't get real soc, rc=%d\n", rc);
			goto err;
		}
		rc = oplus_chg_wls_get_batt_num(wls_dev, &batt_num);
		if (rc == 0) {
			scale_factor = WLS_RX_VOL_MAX_MV / 5000 / batt_num;
		} else {
			scale_factor = 1;
		}
		if ((wls_status->wls_type == OPLUS_CHG_WLS_SVOOC) ||
		    (wls_status->wls_type == OPLUS_CHG_WLS_PD_65W)) {
			wls_status->fastchg_ibat_max_ma = dynamic_cfg->fastchg_curr_max_ma * scale_factor;
			iout_max_ma = WLS_FASTCHG_CURR_50W_MAX_MA;
			if (wls_status->wls_type == OPLUS_CHG_WLS_SVOOC && iout_max_ma > WLS_FASTCHG_CURR_45W_MAX_MA
					&& wls_status->adapter_power <= WLS_ADAPTER_POWER_45W_MAX_ID)
				iout_max_ma = WLS_FASTCHG_CURR_45W_MAX_MA;
			oplus_vote(wls_dev->fcc_votable, MAX_VOTER, true, iout_max_ma, false);
		} else {
			wls_status->fastchg_ibat_max_ma = WLS_FASTCHG_CURR_15W_MAX_MA * scale_factor;;
			oplus_vote(wls_dev->fcc_votable, MAX_VOTER, true, WLS_FASTCHG_CURR_15W_MAX_MA, false);
		}
		wls_status->state_sub_step = 0;
		wls_status->fastchg_started = true;
		wls_status->fastchg_level_init_temp = 0;
		wls_status->wait_cep_stable = true;
		wls_status->fastchg_retry_count = 0;
		wls_status->iout_ma_conunt = 0;
		fcc_chg->fcc_wait_timeout = jiffies;

		oplus_wls_wake_bcc_update_work(wls_dev);

		if (!wls_status->fastchg_restart) {
			for (i = 0; i < fcc_chg->max_step; i++) {
				if ((batt_temp < fcc_chg->fcc_step[i].high_threshold) &&
				    (vbat_mv < fcc_chg->fcc_step[i].vol_max_mv))
					break;
			}
			wls_status->fastchg_level = i;
			oplus_vote(wls_dev->fcc_votable, FCC_VOTER, true,
				fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
			oplus_chg_wls_fast_fcc_param_init(wls_dev);
			wls_status->fastchg_restart = true;
		} else {
			oplus_vote(wls_dev->fcc_votable, FCC_VOTER, true,
				fcc_chg->fcc_step[wls_status->fastchg_level].curr_ma, false);
		}
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		oplus_vote(wls_dev->nor_fv_votable, USER_VOTER, true,
			dynamic_cfg->batt_vol_max_mv + 100, false);
		delay_ms = 0;
		break;
	}

	return delay_ms;

err:
	wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
	wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
	return 0;
}

static int oplus_chg_wls_rx_handle_state_fast(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int wait_time_ms = 4000;
	int rc;

	oplus_chg_wls_check_quiet_mode(wls_dev);

	if ((wls_status->iout_ma > 500) && (wls_dev->wls_nor->icl_set_ma != 0)) {
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, true, 0, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 0, false);
	}

	if (wls_dev->factory_mode&& wls_dev->support_get_tx_pwr) {
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, 0);
		wait_time_ms = 3000;
	}

	if (wls_dev->force_type == OPLUS_CHG_WLS_FORCE_TYPE_AUTO)
		return wait_time_ms;

	if (wls_status->switch_quiet_mode) {
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		return 0;
	}

	if (!wls_dev->batt_charge_enable) {
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		return 0;
	}

	if (oplus_is_client_vote_enabled(wls_dev->fastchg_disable_votable, RX_FAST_ERR_VOTER)){
		pr_info("exit wls fast charge by fast_chg err\n");
		wls_status->fastchg_err_count++;
		wls_status->fastchg_err_timer = jiffies + (unsigned long)(300 * HZ); //5 min
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	if (!wls_status->verity_started &&
	    (wls_status->fastchg_target_curr_ma - wls_status->iout_ma <
	     WLS_CURR_ERR_MIN_MA)) {
		schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	rc = oplus_chg_wls_fast_temp_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge, exit_code=%d\n", rc);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	if (wls_status->fast_cep_check < 0) {
		pr_info("exit wls fast charge by cep check\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	rc = oplus_chg_wls_fast_ibat_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge by ibat check\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	rc = oplus_chg_wls_fast_iout_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge by iout check\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	oplus_chg_wls_fast_skin_temp_check(wls_dev);
#ifndef CONFIG_OPLUS_CHG_OOS
	rc = oplus_chg_wls_fast_cool_down_check(wls_dev);
	if (rc < 0) {
		pr_info("exit wls fast charge by cool down\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}
#endif

	if (wls_status->ffc_check) {
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FFC;
		wls_status->ffc_check = false;
		return 0;
	}
	oplus_chg_wls_exchange_batt_mesg(wls_dev);

	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_fast(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	oplus_vote(wls_dev->fcc_votable, EXIT_VOTER, true, WLS_FASTCHG_CURR_EXIT_MA, false);
	while (wls_status->iout_ma > (WLS_FASTCHG_CURR_EXIT_MA + WLS_FASTCHG_CURR_ERR_MA)) {
		if (!wls_status->rx_online) {
			pr_info("wireless charge is offline\n");
			return 0;
		}
		msleep(500);
	}
	wls_status->fastchg_started = false;
	oplus_wls_cancel_bcc_update_work(wls_dev);

	if (is_comm_ocm_available(wls_dev))
		oplus_chg_comm_update_config(wls_dev->comm_ocm);

	(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
		oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
		msleep(100);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		oplus_vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
		msleep(100);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		oplus_vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
		msleep(100);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		oplus_vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
		msleep(100);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		oplus_vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	case OPLUS_CHG_WLS_RX_STATE_FFC:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FFC;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 300, false);
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
		msleep(100);
		(void)oplus_chg_wls_fast_set_enable(wls_dev->wls_fast, false);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 1000, -1);
		oplus_vote(wls_dev->fcc_votable, EXIT_VOTER, false, 0, false);
		break;
	default:
		pr_err("unsupported target status(=%s)\n",
			oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}
	return 0;
}

static int oplus_chg_wls_rx_enter_state_ffc(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	static unsigned long stop_time;
	int wait_time_ms = 0;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		oplus_vote(wls_dev->nor_out_disable_votable, FFC_VOTER, true, 1, false);
		stop_time = jiffies + msecs_to_jiffies(30000);
		wait_time_ms = (int)jiffies_to_msecs(stop_time - jiffies);
		wls_status->fod_parm_for_fastchg = false;

		if(wls_dev->static_config.fastchg_12V_fod_enable)
			(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
				wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
		else
			(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
				oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);

		oplus_chg_wls_get_verity_data(wls_dev);
		wls_status->state_sub_step = 1;
		break;
	case 1:
		if (!time_is_before_jiffies(stop_time)) {
			wait_time_ms = (int)jiffies_to_msecs(stop_time - jiffies);
			break;
		}
		oplus_vote(wls_dev->nor_out_disable_votable, FFC_VOTER, false, 0, false);
		if (is_comm_ocm_available(wls_dev)) {
			rc = oplus_chg_comm_switch_ffc(wls_dev->comm_ocm);
			if (rc < 0) {
				pr_err("can't switch to ffc charge, rc=%d\n", rc);
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			}
		} else {
			pr_err("comm mod not found, can't switch to ffc charge\n");
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		}
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_FFC;
		wls_status->state_sub_step = 0;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return wait_time_ms;
}

static int oplus_chg_wls_rx_handle_state_ffc(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int iout_ma, vout_mv, vrect_mv;
	int rc;

	oplus_chg_wls_check_quiet_mode(wls_dev);

	if (wls_dev->batt_charge_enable) {
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	if (is_comm_ocm_available(wls_dev)) {
		rc = oplus_chg_comm_check_ffc(wls_dev->comm_ocm);
		if (rc < 0) {
			pr_err("ffc check error, exit ffc charge, rc=%d\n", rc);
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			return 0;
		} else if (rc > 0) {
			pr_info("ffc done\n");
			wls_status->ffc_done = true;
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			return 0;
		}
	} else {
		pr_err("comm mod not found, exit ffc charge\n");
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		return 0;
	}

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);

	if (!wls_status->verity_started)
		schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_ffc(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		break;
	case OPLUS_CHG_WLS_RX_STATE_DEBUG:
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_done(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		oplus_chg_wls_get_verity_data(wls_dev);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 100, true);
		oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, true, 0, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 0);
		wls_status->state_sub_step = 1;
	case 1:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST) {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, 0);
			if (rc < 0) {
				pr_err("send WLS_CMD_INTO_FASTCHAGE err, rc=%d\n", rc);
				return 100;
			}
			wls_status->fod_parm_for_fastchg = false;
			return 1000;
		}else{
			if (wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY) {
				rc = oplus_chg_wls_get_third_adapter_ext_cmd_p_id(wls_dev);
				if (rc < 0) {
					if (!wls_status->rx_online)
						return 0;
					pr_err("get product id fail\n");
					wls_status->online_keep = true;
					oplus_vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
					schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
					return 100;
				}
			}

			if(wls_dev->static_config.fastchg_12V_fod_enable)
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			else
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		}
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		wls_status->state_sub_step = 2;
		break;
	case 2:
		if (wls_status->vout_mv > 11500) {
			oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 3600, false);
			oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 950, true);
			oplus_chg_wls_set_non_ffc_current(wls_dev);
			oplus_vote(wls_dev->nor_input_disable_votable, USER_VOTER, false, 0, false);
			oplus_chg_wls_nor_set_vindpm(wls_dev->wls_nor, WLS_VINDPM_AIRSVOOC);
			rc = oplus_chg_wls_choose_fastchg_curve(wls_dev);
			if (rc < 0) {
				pr_err("choose fastchg curve failed, rc = %d\n", rc);
			}
			wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			wls_status->state_sub_step = 0;
		} else {
			return 100;
		}
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_handle_state_done(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int wait_time_ms = 4000;
	int iout_ma, vout_mv, vrect_mv;
	int rc;

	oplus_chg_wls_check_quiet_mode(wls_dev);

	if (wls_dev->batt_charge_enable) {
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

	rc = oplus_chg_wls_fastchg_restart_check(wls_dev);
	if ((rc >= 0) && (!wls_status->fastchg_disable))
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_set_non_ffc_current(wls_dev);
	oplus_chg_wls_exchange_batt_mesg(wls_dev);

	iout_ma = wls_status->iout_ma;
	vout_mv = wls_status->vout_mv;
	vrect_mv = wls_status->vrect_mv;
	pr_err("wkcs: iout=%d, vout=%d, vrect=%d\n", iout_ma, vout_mv, vrect_mv);

	if (!wls_status->verity_started)
		schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));

	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_done(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
			oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_quiet(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, 0);
		wls_status->state_sub_step = 1;
	case 1:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST) {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, 0);
			if (rc < 0) {
				pr_err("send WLS_CMD_INTO_FASTCHAGE err, rc=%d\n", rc);
				return 100;
			}
			wls_status->fod_parm_for_fastchg = false;
			return 1000;
		}else{
			if (wls_status->adapter_id == WLS_ADAPTER_THIRD_PARTY) {
				rc = oplus_chg_wls_get_third_adapter_ext_cmd_p_id(wls_dev);
				if (rc < 0) {
					if (!wls_status->rx_online)
						return 0;
					pr_err("get product id fail\n");
					wls_status->online_keep = true;
					oplus_vote(wls_dev->rx_disable_votable, VERITY_VOTER, true, 1, false);
					schedule_delayed_work(&wls_dev->rx_verity_restore_work, msecs_to_jiffies(500));
					return 100;
				}
			}

			if(wls_dev->static_config.fastchg_12V_fod_enable)
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			else
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		}

		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1250, true);
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		wls_status->state_sub_step = 0;
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_handle_state_quiet(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int icl_ma, nor_input_curr_ma;
	int wait_time_ms = 4000;
	int rc;

	oplus_chg_wls_check_quiet_mode(wls_dev);

	if (!wls_status->switch_quiet_mode || wls_status->chg_done_quiet_mode) {
		if (wls_status->quiet_mode && !wls_status->chg_done_quiet_mode) {
			wait_time_ms = 1000;
			goto out;
		} else {
			if (wls_dev->batt_charge_enable) {
				if (!wls_status->chg_done_quiet_mode)
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
				else
					wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
			} else {
				wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
			}
			return 0;
		}
	}

	if (wls_dev->batt_charge_enable) {
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
	} else {
		if (!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, true, WLS_CURR_STOP_CHG_MA, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 0, false);
		}
	}

out:
	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_set_non_ffc_current(wls_dev);
	oplus_chg_wls_exchange_batt_mesg(wls_dev);

	if ((wls_status->adapter_type != WLS_ADAPTER_TYPE_UNKNOWN) &&
	     !wls_status->verity_started) {
		icl_ma = oplus_get_effective_result(wls_dev->nor_icl_votable);
		rc = oplus_chg_wls_nor_get_input_curr(wls_dev->wls_nor, &nor_input_curr_ma);
		if (rc < 0)
			nor_input_curr_ma = wls_status->iout_ma;
		if (icl_ma - nor_input_curr_ma < 300)
			schedule_delayed_work(&wls_dev->wls_verity_work, msecs_to_jiffies(500));
	}

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_quiet(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		if (!!oplus_get_client_vote(wls_dev->nor_out_disable_votable, STOP_VOTER)) {
			oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
			oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		}
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
			oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		break;
	case OPLUS_CHG_WLS_RX_STATE_STOP:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		break;
	case OPLUS_CHG_WLS_RX_STATE_DONE:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_DONE;
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_stop(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int rc;

	switch(wls_status->state_sub_step) {
	case 0:
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx,
			WLS_VOUT_FASTCHG_INIT_MV, 0);
		wls_status->state_sub_step = 1;
	case 1:
		if (wls_status->charge_type != WLS_CHARGE_TYPE_FAST) {
			rc = oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, 0);
			if (rc < 0) {
				pr_err("send WLS_CMD_INTO_FASTCHAGE err, rc=%d\n", rc);
				return 100;
			}
			wls_status->fod_parm_for_fastchg = false;
			return 1000;
		}else{
			if(wls_dev->static_config.fastchg_12V_fod_enable)
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					wls_dev->static_config.fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
			else
				(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
					oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		}

		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, true, 300, true);
		oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, true, 1, false);
		wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_STOP;
		wls_status->state_sub_step = 0;
		WARN_ON(wls_dev->batt_charge_enable);
		break;
	default:
		wls_status->state_sub_step = 0;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_handle_state_stop(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int wait_time_ms = 4000;
	// int rc;

	if (wls_dev->batt_charge_enable) {
		oplus_vote(wls_dev->nor_icl_votable, STOP_VOTER, false, 0, true);
		oplus_vote(wls_dev->nor_out_disable_votable, STOP_VOTER, false, 0, false);
		if (wls_status->switch_quiet_mode)
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		else
			wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		return 0;
	}

	oplus_chg_wls_check_quiet_mode(wls_dev);

	(void)oplus_chg_wls_nor_skin_check(wls_dev);
	oplus_chg_wls_check_term_charge(wls_dev);
	oplus_chg_wls_set_non_ffc_current(wls_dev);
	return wait_time_ms;
}

static int oplus_chg_wls_rx_exit_state_stop(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	switch (wls_status->target_rx_state) {
	case OPLUS_CHG_WLS_RX_STATE_FAST:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_FAST;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		(void)oplus_chg_wls_rx_set_fod_parm(wls_dev->wls_rx,
			oplus_chg_wls_disable_fod_parm, WLS_FOD_PARM_LENGTH);
		break;
	case OPLUS_CHG_WLS_RX_STATE_QUIET:
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_QUIET;
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, true);
		break;
	default:
		pr_err("unsupported target status(=%s)\n", oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);
		wls_status->target_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		wls_status->next_rx_state = OPLUS_CHG_WLS_RX_STATE_ERROR;
		break;
	}

	return 0;
}

static int oplus_chg_wls_rx_enter_state_debug(struct oplus_chg_wls *wls_dev)
{
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	int scale_factor;
	int batt_num;
	int rc;

	switch (wls_dev->force_type) {
	case OPLUS_CHG_WLS_FORCE_TYPE_BPP:
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, 5000, 0);
		msleep(500);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1000, false);
		if (wls_dev->factory_mode)
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, -1);
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP:
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 1500, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, -1);
		msleep(500);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 850, false);
		if (wls_dev->factory_mode)
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, -1);
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_EPP_PLUS:
		oplus_vote(wls_dev->nor_fcc_votable, USER_VOTER, true, 2000, false);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_EPP_MV, -1);
		msleep(500);
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 1250, false);
		if (wls_dev->factory_mode)
			(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_GET_TX_PWR, 0xff, -1);
		break;
	case OPLUS_CHG_WLS_FORCE_TYPE_FAST:
		oplus_vote(wls_dev->nor_icl_votable, USER_VOTER, true, 500, false);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, 0);
		(void)oplus_chg_wls_send_msg(wls_dev, WLS_CMD_INTO_FASTCHAGE, 0xff, -1);
		(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, WLS_VOUT_FASTCHG_INIT_MV, -1);
		(void)oplus_chg_wls_rx_set_vout_step(wls_dev->wls_rx, 15000, 1000, -1);
		rc = oplus_chg_wls_get_batt_num(wls_dev, &batt_num);
		if (rc == 0) {
			scale_factor = WLS_RX_VOL_MAX_MV / 5000 / batt_num;
		} else {
			scale_factor = 1;
		}
		wls_status->fastchg_ibat_max_ma = dynamic_cfg->fastchg_curr_max_ma * scale_factor;;
		oplus_vote(wls_dev->fcc_votable, MAX_VOTER, true, WLS_FASTCHG_CURR_45W_MAX_MA, false);
		break;
	default:
		break;
	}

	wls_status->current_rx_state = OPLUS_CHG_WLS_RX_STATE_DEBUG;

	return 0;
}

static int oplus_chg_wls_rx_handle_state_debug(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_debug(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_enter_state_ftm(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_handle_state_ftm(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_ftm(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_enter_state_error(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

static int oplus_chg_wls_rx_handle_state_error(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 4000;
}

static int oplus_chg_wls_rx_exit_state_error(struct oplus_chg_wls *wls_dev)
{
	// struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	// int rc;

	return 0;
}

struct oplus_chg_wls_state_handler oplus_chg_wls_rx_state_handler[] = {
	[OPLUS_CHG_WLS_RX_STATE_DEFAULT] = {
		NULL,
		oplus_chg_wls_rx_handle_state_default,
		oplus_chg_wls_rx_exit_state_default
	},
	[OPLUS_CHG_WLS_RX_STATE_BPP] = {
		oplus_chg_wls_rx_enter_state_bpp,
		oplus_chg_wls_rx_handle_state_bpp,
		oplus_chg_wls_rx_exit_state_bpp
	},
	[OPLUS_CHG_WLS_RX_STATE_EPP] = {
		oplus_chg_wls_rx_enter_state_epp,
		oplus_chg_wls_rx_handle_state_epp,
		oplus_chg_wls_rx_exit_state_epp
	},
	[OPLUS_CHG_WLS_RX_STATE_EPP_PLUS] = {
		oplus_chg_wls_rx_enter_state_epp_plus,
		oplus_chg_wls_rx_handle_state_epp_plus,
		oplus_chg_wls_rx_exit_state_epp_plus,
	},
	[OPLUS_CHG_WLS_RX_STATE_FAST] = {
		oplus_chg_wls_rx_enter_state_fast,
		oplus_chg_wls_rx_handle_state_fast,
		oplus_chg_wls_rx_exit_state_fast,
	},
	[OPLUS_CHG_WLS_RX_STATE_FFC] = {
		oplus_chg_wls_rx_enter_state_ffc,
		oplus_chg_wls_rx_handle_state_ffc,
		oplus_chg_wls_rx_exit_state_ffc,
	},
	[OPLUS_CHG_WLS_RX_STATE_DONE] = {
		oplus_chg_wls_rx_enter_state_done,
		oplus_chg_wls_rx_handle_state_done,
		oplus_chg_wls_rx_exit_state_done,
	},
	[OPLUS_CHG_WLS_RX_STATE_QUIET] = {
		oplus_chg_wls_rx_enter_state_quiet,
		oplus_chg_wls_rx_handle_state_quiet,
		oplus_chg_wls_rx_exit_state_quiet,
	},
	[OPLUS_CHG_WLS_RX_STATE_STOP] = {
		oplus_chg_wls_rx_enter_state_stop,
		oplus_chg_wls_rx_handle_state_stop,
		oplus_chg_wls_rx_exit_state_stop,
	},
	[OPLUS_CHG_WLS_RX_STATE_DEBUG] = {
		oplus_chg_wls_rx_enter_state_debug,
		oplus_chg_wls_rx_handle_state_debug,
		oplus_chg_wls_rx_exit_state_debug,
	},
	[OPLUS_CHG_WLS_RX_STATE_FTM] = {
		oplus_chg_wls_rx_enter_state_ftm,
		oplus_chg_wls_rx_handle_state_ftm,
		oplus_chg_wls_rx_exit_state_ftm,
	},
	[OPLUS_CHG_WLS_RX_STATE_ERROR] = {
		oplus_chg_wls_rx_enter_state_error,
		oplus_chg_wls_rx_handle_state_error,
		oplus_chg_wls_rx_exit_state_error,
	},
};

static void oplus_chg_wls_rx_sm(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_rx_sm_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int delay_ms = 4000;

	if (!wls_status->rx_online) {
		pr_info("wireless charge is offline\n");
		return;
	}

	pr_err("curr_state=%s, next_state=%s, target_state=%s\n",
		oplus_chg_wls_rx_state_text[wls_status->current_rx_state],
		oplus_chg_wls_rx_state_text[wls_status->next_rx_state],
		oplus_chg_wls_rx_state_text[wls_status->target_rx_state]);

	if (wls_status->current_rx_state != wls_status->target_rx_state) {
		if (wls_status->current_rx_state != wls_status->next_rx_state) {
			if (oplus_chg_wls_rx_state_handler[wls_status->next_rx_state].enter_state != NULL) {
				delay_ms =oplus_chg_wls_rx_state_handler[wls_status->next_rx_state].enter_state(wls_dev);
			} else {
				delay_ms = 0;
			}
		} else {
			if (oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].exit_state != NULL) {
				delay_ms = oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].exit_state(wls_dev);
			} else {
				delay_ms = 0;
			}
		}
	} else {
		if (oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].handle_state != NULL) {
			delay_ms = oplus_chg_wls_rx_state_handler[wls_status->current_rx_state].handle_state(wls_dev);
		}
	}

	queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_rx_sm_work, msecs_to_jiffies(delay_ms));
}

static void oplus_chg_wls_trx_disconnect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_trx_disconnect_work);
	static int retry_num;
	int rc;

	rc = oplus_chg_wls_set_trx_enable(wls_dev, false);
	if (rc < 0) {
		retry_num++;
		pr_err("can't disable trx, retry_num=%d, rc=%d\n", retry_num, rc);
		if (retry_num < 5)
			schedule_delayed_work(&wls_dev->wls_trx_disconnect_work, msecs_to_jiffies(100));
		else
			retry_num = 0;
	}
	retry_num = 0;
}

#ifndef CONFIG_OPLUS_CHG_OOS
static void oplus_chg_wls_clear_trx_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_clear_trx_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;

	if (!wls_status->rx_present && !wls_status->rx_online) {
		wls_status->trx_close_delay = false;
		if (is_batt_psy_available(wls_dev))
			power_supply_changed(wls_dev->batt_psy);
	}
}
#endif

static void oplus_chg_wls_trx_sm(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_trx_sm_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	char trx_crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN] = {0};
	u8 trx_status, trx_err;
	static int err_count;
	static bool pre_trx_online = false;
	int delay_ms = 5000;
	int rc;

	rc = oplus_chg_wls_rx_get_trx_err(wls_dev->wls_rx, &trx_err);
	if (rc < 0) {
		pr_err("can't get trx err code, rc=%d\n", rc);
		goto out;
	}
	pr_err("wkcs: trx_err=0x%02x\n", trx_err);
	wls_status->trx_err = trx_err;

	if (trx_err & WLS_TRX_ERR_RXAC) {
		pr_err("trx err: RXAC\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
		wls_status->trx_rxac = true;
#ifndef CONFIG_OPLUS_CHG_OOS
		wls_status->trx_close_delay = true;
#endif
	}
	if (trx_err & WLS_TRX_ERR_OCP) {
		pr_err("trx err: OCP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
#ifndef CONFIG_OPLUS_CHG_OOS
		wls_status->trx_close_delay = true;
#endif
	}
	if (trx_err & WLS_TRX_ERR_OVP) {
		pr_err("trx err: OVP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_LVP) {
		pr_err("trx err: LVP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_FOD) {
		pr_err("trx err: FOD\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_OTP) {
		pr_err("trx err: OTP\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (trx_err & WLS_TRX_ERR_CEPTIMEOUT) {
		pr_err("trx err: CEPTIMEOUT\n");
	}
	if (trx_err & WLS_TRX_ERR_RXEPT) {
		pr_err("trx err: RXEPT\n");
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	}
	if (wls_status->trx_state == OPLUS_CHG_WLS_TRX_STATE_OFF)
		goto err;

	rc = oplus_chg_wls_rx_get_trx_status(wls_dev->wls_rx, &trx_status);
	if (rc < 0) {
		pr_err("can't get trx err code, rc=%d\n", rc);
		goto out;
	}
	pr_err("wkcs: trx_status=0x%02x\n", trx_status);
	if (wls_status->wls_type != OPLUS_CHG_WLS_TRX)
		goto out;
	if (trx_status & WLS_TRX_STATUS_READY) {
		wls_status->trx_present = true;
		wls_status->trx_online = false;
		pre_trx_online = false;
		if (is_batt_psy_available(wls_dev))
			power_supply_changed(wls_dev->batt_psy);
		rc = oplus_chg_wls_rx_set_trx_enable(wls_dev->wls_rx, true);
		if (rc < 0) {
			pr_err("can't enable trx, rc=%d\n", rc);
			goto out;
		}
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_READY;
		delay_ms = 200;
	} else if ((trx_status & WLS_TRX_STATUS_DIGITALPING)
			|| (trx_status & WLS_TRX_STATUS_ANALOGPING)) {
		wls_status->trx_present = true;
		wls_status->trx_online = false;
		if (is_batt_psy_available(wls_dev))
			power_supply_changed(wls_dev->batt_psy);
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_WAIT_PING;
		delay_ms = 2000;
	} else if (trx_status & WLS_TRX_STATUS_TRANSFER) {
		wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_TRANSFER;
		wls_status->trx_online = true;
		if (!wls_status->trx_usb_present_once)
			wls_status->trx_usb_present_once = oplus_chg_wls_is_usb_present(wls_dev);
		if (is_batt_psy_available(wls_dev))
			power_supply_changed(wls_dev->batt_psy);
		rc = oplus_chg_wls_rx_get_trx_curr(wls_dev->wls_rx, &wls_status->trx_curr_ma);
		if (rc < 0)
			pr_err("can't get trx curr, rc=%d\n", rc);
		rc = oplus_chg_wls_rx_get_trx_vol(wls_dev->wls_rx, &wls_status->trx_vol_mv);
		if (rc < 0)
			pr_err("can't get trx vol, rc=%d\n", rc);
		pr_err("trx_vol=%d, trx_curr=%d\n", wls_status->trx_vol_mv, wls_status->trx_curr_ma);
		delay_ms = 5000;
	} else if (trx_status == 0) {
		goto out;
	}

	if (pre_trx_online && !wls_status->trx_online) {
		wls_status->trx_transfer_end_time = oplus_chg_wls_get_local_time_s();
		pr_info("trx_online:%d, trx_start_time:%d, trx_end_time:%d,"
			"trx_usb_present_once:%d\n",
			wls_status->trx_online, wls_status->trx_transfer_start_time,
			wls_status->trx_transfer_end_time,
			wls_status->trx_usb_present_once);
		if (wls_status->trx_transfer_end_time - wls_status->trx_transfer_start_time >
		    WLS_TRX_INFO_UPLOAD_THD_2MINS) {
			oplus_chg_wls_update_track_info(
				wls_dev, trx_crux_info, false);
			oplus_chg_wls_track_upload_trx_general_info(
				wls_dev, trx_crux_info,
				wls_status->trx_usb_present_once);
		}
		wls_status->trx_usb_present_once = false;
	} else if (!pre_trx_online && wls_status->trx_online) {
		wls_status->trx_usb_present_once = false;
		wls_status->trx_transfer_start_time =
			oplus_chg_wls_get_local_time_s();
		pr_info("trx_online=%d, trx_start_time=%d, trx_end_time=%d\n",
			wls_status->trx_online, wls_status->trx_transfer_start_time,
			wls_status->trx_transfer_end_time);
	}
	pre_trx_online = wls_status->trx_online;

	pr_err("trx_state=%s\n", oplus_chg_wls_trx_state_text[wls_status->trx_state]);
	err_count = 0;

schedule:
	queue_delayed_work(wls_dev->wls_wq, &wls_dev->wls_trx_sm_work, msecs_to_jiffies(delay_ms));
	return;
out:
	err_count++;
	delay_ms = 200;
	if (err_count > (2 * 60 * 5) || wls_status->wls_type != OPLUS_CHG_WLS_TRX) {
		pr_err("trx status err, exit\n");
		goto err;
	}
	goto schedule;
err:
	wls_status->trx_state = OPLUS_CHG_WLS_TRX_STATE_OFF;
	err_count = 0;
	schedule_delayed_work(&wls_dev->wls_trx_disconnect_work, 0);
}

static void oplus_chg_wls_upgrade_fw_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_upgrade_fw_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	bool usb_present;
	int boot_mode;
	static int retry_num;
	int rc;

	boot_mode = get_boot_mode();
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#ifndef CONFIG_OPLUS_CHARGER_MTK
	if (boot_mode == MSM_BOOT_MODE__FACTORY || boot_mode == MSM_BOOT_MODE__RF) {
#else
	if (boot_mode == META_BOOT || boot_mode == FACTORY_BOOT
			|| boot_mode == ADVMETA_BOOT || boot_mode == ATE_FACTORY_BOOT) {
#endif
		pr_err("is factory mode, can't upgrade fw\n");
		oplus_vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, true, 1, false);
		msleep(1000);
		oplus_vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, false, 0, false);
		retry_num = 0;
		return;
	}
#endif

	usb_present = oplus_chg_wls_is_usb_present(wls_dev);
#ifndef CONFIG_OPLUS_CHARGER_MTK
	if (usb_present && !wls_dev->support_tx_boost) {
#else
	if (usb_present) {
#endif
		pr_info("usb online, wls fw upgrade pending\n");
		wls_status->upgrade_fw_pending = true;
		return;
	}

	if (wls_status->rx_present) {
		pr_err("rx present, exit upgrade\n");
		retry_num = 0;
		return;
	}

	if (wls_dev->fw_upgrade_by_buf) {
		if (wls_dev->fw_buf == NULL || wls_dev->fw_size == 0) {
			pr_err("fw buf is NULL or fw size is 0, can't upgrade\n");
			wls_dev->fw_upgrade_by_buf = false;
			wls_dev->fw_size = 0;
			return;
		}
		oplus_vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, true, 1, false);
		oplus_vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, true, 1, false);
		rc = oplus_chg_wls_rx_upgrade_firmware_by_buf(wls_dev->wls_rx, wls_dev->fw_buf, wls_dev->fw_size);
		oplus_vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, false, 0, false);
		oplus_vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, false, 0, false);
		if (rc < 0)
			pr_err("upgrade error, rc=%d\n", rc);
		kfree(wls_dev->fw_buf);
		wls_dev->fw_buf = NULL;
		wls_dev->fw_upgrade_by_buf = false;
		wls_dev->fw_size = 0;
	} else {
		oplus_vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, true, 1, false);
		oplus_vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, true, 1, false);
		rc = oplus_chg_wls_rx_upgrade_firmware_by_img(wls_dev->wls_rx);
		oplus_vote(wls_dev->wrx_en_votable, UPGRADE_FW_VOTER, false, 0, false);
		oplus_vote(wls_dev->rx_disable_votable, UPGRADE_VOTER, false, 0, false);
		if (rc < 0) {
			retry_num++;
			pr_err("upgrade error, retry_num=%d, rc=%d\n", retry_num, rc);
			goto out;
		}
	}
	pr_info("update successed\n");

	return;

out:
	if (retry_num >= 5) {
		retry_num = 0;
		return;
	}
	schedule_delayed_work(&wls_dev->wls_upgrade_fw_work, msecs_to_jiffies(1000));
}

#define DELTA_IOUT_500MA	500
#define DELTA_IOUT_300MA	300
#define DELTA_IOUT_100MA	100
#define VOUT_200MV		200
#define VOUT_100MV		100
#define VOUT_50MV		50
#define VOUT_20MV		20
static void oplus_chg_wls_data_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_data_update_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int ibat_ma, tmp_val, ibat_err, cep, vol_set_mv;
	int iout_ma, vout_mv, vrect_mv;
	static int cep_err_count;
	bool skip_cep_check = false;
	bool cep_ok = false;
	int rc;

	if (!wls_status->rx_online) {
		pr_err("wireless charge is not online\n");
		return;
	}

	oplus_chg_wls_update_track_info(wls_dev, NULL, false);

	rc = oplus_chg_wls_rx_get_iout(wls_dev->wls_rx, &iout_ma);
	if (rc < 0)
		pr_err("can't get rx iout, rc=%d\n", rc);
	rc = oplus_chg_wls_rx_get_vout(wls_dev->wls_rx, &vout_mv);
	if (rc < 0)
		pr_err("can't get rx vout, rc=%d\n", rc);
	rc = oplus_chg_wls_rx_get_vrect(wls_dev->wls_rx, &vrect_mv);
	if (rc < 0)
		pr_err("can't get rx vrect, rc=%d\n", rc);
	WRITE_ONCE(wls_status->iout_ma, iout_ma);
	WRITE_ONCE(wls_status->vout_mv, vout_mv);
	WRITE_ONCE(wls_status->vrect_mv, vrect_mv);

	if (!wls_status->fastchg_started)
		goto out;

	rc = oplus_chg_wls_get_ibat(wls_dev, &ibat_ma);
	if (rc < 0) {
		pr_err("can't get ibat, rc=%d\n");
		goto out;
	}

	tmp_val = wls_status->fastchg_target_curr_ma - wls_status->iout_ma;
	ibat_err = ((wls_status->fastchg_ibat_max_ma - abs(ibat_ma)) / 4) - (WLS_CURR_ERR_MIN_MA / 2);
	/* Prevent the voltage from increasing too much, ibat exceeds expectations */
	if ((ibat_err > -(WLS_CURR_ERR_MIN_MA / 2)) && (ibat_err < 0) && (tmp_val > 0)) {
		/*
		 * When ibat is greater than 5800mA, the current is not
		 * allowed to continue to increase, preventing fluctuations.
		 */
		tmp_val = 0;
	} else {
		tmp_val = tmp_val > ibat_err ? ibat_err : tmp_val;
	}
	rc = oplus_chg_wls_get_cep_check_update(wls_dev->wls_rx, &cep);
	if (rc < 0) {
		pr_err("can't get cep, rc=%d\n", rc);
		cep_ok = false;
	} else {
		if (abs(cep) < 3)
			cep_ok = true;
		else
			cep_ok = false;
	}
	if (tmp_val < 0) {
		if (!cep_ok)
			cep_err_count++;
		else
			cep_err_count = 0;
		if (!wls_status->fastchg_curr_need_dec || cep_err_count >= WLS_CEP_ERR_MAX) {
			skip_cep_check = true;
			wls_status->fastchg_curr_need_dec = true;
			cep_err_count = 0;
		}
	} else {
		cep_err_count = 0;
		wls_status->fastchg_curr_need_dec = false;
	}
	vol_set_mv = wls_dev->wls_rx->vol_set_mv;
	if (cep_ok || skip_cep_check) {
		if (tmp_val > WLS_CURR_ERR_MIN_MA || tmp_val < -WLS_CURR_ERR_MIN_MA) {
			if (tmp_val > WLS_CURR_ERR_MIN_MA) {
				if (tmp_val > DELTA_IOUT_500MA)
					vol_set_mv += VOUT_200MV;
				else if (tmp_val > DELTA_IOUT_300MA)
					vol_set_mv += VOUT_100MV;
				else if (tmp_val > DELTA_IOUT_100MA)
					vol_set_mv += VOUT_50MV;
				else
					vol_set_mv += VOUT_20MV;
			} else {
				if (tmp_val < -DELTA_IOUT_500MA)
					vol_set_mv -= VOUT_200MV;
				else if (tmp_val < -DELTA_IOUT_300MA)
					vol_set_mv -= VOUT_100MV;
				else if (tmp_val < -DELTA_IOUT_100MA)
					vol_set_mv -= VOUT_50MV;
				else
					vol_set_mv -= VOUT_20MV;
			}
			if (vol_set_mv > WLS_RX_VOL_MAX_MV)
				vol_set_mv = WLS_RX_VOL_MAX_MV;
			if (vol_set_mv < WLS_FASTCHG_MODE_VOL_MIN_MV)
				vol_set_mv = WLS_FASTCHG_MODE_VOL_MIN_MV;
			mutex_lock(&wls_dev->update_data_lock);
			(void)oplus_chg_wls_rx_set_vout(wls_dev->wls_rx, vol_set_mv, 0);
			mutex_unlock(&wls_dev->update_data_lock);
		}
	}

	pr_info("Iout: target=%d, out=%d, ibat_max=%d, ibat=%d; vout=%d; vrect=%d; cep=%d\n",
		wls_status->fastchg_target_curr_ma, wls_status->iout_ma,
		wls_status->fastchg_ibat_max_ma, ibat_ma,
		wls_status->vout_mv, wls_status->vrect_mv,
		cep);
	oplus_chg_wls_fast_cep_check(wls_dev, cep);

out:
	schedule_delayed_work(&wls_dev->wls_data_update_work, msecs_to_jiffies(500));
}

#define CEP_SKEWING_VALUE	3
#define SKEWING_INTERVAL	500
static void oplus_chg_wls_skewing_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_skewing_work);
	struct oplus_chg_wls_dynamic_config *dynamic_cfg = &wls_dev->dynamic_config;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int cep = 0;
	static int cep_ok_count = 0;
	static int cep_err_count = 0;
	int skewing_icl = 0;
	int skewing_max_step = 0;
	int pre_level = 0;
	int rc = 0;
	int i = 0;

	if (!wls_status->rx_online) {
		cep_ok_count = 0;
		cep_err_count = 0;
		return;
	}

	rc = oplus_chg_wls_get_cep(wls_dev->wls_rx, &cep);
	if (rc < 0) {
		pr_err("can't get cep, rc=%d\n", rc);
		goto out;
	}

	switch (wls_status->wls_type) {
	case OPLUS_CHG_WLS_EPP:
		i = OPLUS_WLS_SKEWING_EPP;
		break;
	case OPLUS_CHG_WLS_EPP_PLUS:
		i = OPLUS_WLS_SKEWING_EPP_PLUS;
		break;
	case OPLUS_CHG_WLS_VOOC:
		i = OPLUS_WLS_SKEWING_AIRVOOC;
		break;
	default:
		i = OPLUS_WLS_SKEWING_MAX;
		break;
	}
	if (i >= OPLUS_WLS_SKEWING_MAX)
		goto out;

	skewing_max_step = wls_dev->skewing_step[i].max_step;
	pre_level = wls_status->skewing_level;

	if (cep < CEP_SKEWING_VALUE) {
		cep_ok_count++;
		cep_err_count = 0;
		if (cep_ok_count >= CEP_OK_MAX &&
		    dynamic_cfg->skewing_step[i][wls_status->skewing_level].fallback_step &&
		    time_after(jiffies, wls_status->cep_ok_wait_timeout)) {
			cep_ok_count = 0;
			wls_status->skewing_level =
				dynamic_cfg->skewing_step[i][wls_status->skewing_level].fallback_step - 1;
			wls_status->cep_ok_wait_timeout = jiffies + HZ;
			pr_info("skewing: recovery charging current, level[%d]\n",
				wls_status->skewing_level);
		}
	} else {
		cep_ok_count = 0;
		cep_err_count++;
		if (cep_err_count >= CEP_ERR_MAX) {
			if (wls_status->skewing_level < (skewing_max_step - 1)) {
				cep_err_count = 0;
				wls_status->skewing_level++;
				wls_status->cep_ok_wait_timeout = jiffies + HZ;
				pr_info("skewing: reduce charging current, level[%d]\n",
					wls_status->skewing_level);
			} else {
				if (dynamic_cfg->skewing_step[i][wls_status->skewing_level].switch_to_bpp) {
					pr_info("skewing: switch to bpp, level[%d]\n", wls_status->skewing_level);
					oplus_chg_wls_rx_set_rx_mode(wls_dev->wls_rx, OPLUS_CHG_WLS_RX_MODE_BPP);
				}
			}
		}
	}

	skewing_icl = dynamic_cfg->skewing_step[i][wls_status->skewing_level].curr_ma;
	if (skewing_icl > 0 && pre_level != wls_status->skewing_level)
		oplus_vote(wls_dev->nor_icl_votable, CEP_VOTER, true, skewing_icl, true);

	pr_info("level=%d, icl=%d, cep=%d\n", wls_status->skewing_level, skewing_icl, cep);
out:
	schedule_delayed_work(&wls_dev->wls_skewing_work, msecs_to_jiffies(SKEWING_INTERVAL));
}

static void oplus_chg_wls_usb_connect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, usb_connect_work);
	bool connected, pre_connected;

	pre_connected = oplus_chg_wls_is_usb_connected(wls_dev);
retry:
	msleep(10);
	connected = oplus_chg_wls_is_usb_connected(wls_dev);
	if (connected != pre_connected) {
		pre_connected = connected;
		goto retry;
	}
}

static void oplus_chg_wls_fod_cal_work(struct work_struct *work)
{
}

static void oplus_chg_wls_rx_restore_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_restore_work);

	oplus_vote(wls_dev->rx_disable_votable, JEITA_VOTER, false, 0, false);
	oplus_vote(wls_dev->nor_icl_votable, JEITA_VOTER, true, WLS_CURR_JEITA_CHG_MA, true);
}

static void oplus_chg_wls_rx_iic_restore_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_iic_restore_work);

	oplus_vote(wls_dev->rx_disable_votable, RX_IIC_VOTER, false, 0, false);
}

static void oplus_chg_wls_rx_verity_restore_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_verity_restore_work);

	oplus_vote(wls_dev->rx_disable_votable, VERITY_VOTER, false, 0, false);
	schedule_delayed_work(&wls_dev->verity_state_remove_work, msecs_to_jiffies(10000));
}

#define FAST_FAULT_SIZE 80
static void oplus_chg_wls_fast_fault_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, fast_fault_check_work);
	char *fast_fault;
	int rc;

	fast_fault = kzalloc(FAST_FAULT_SIZE, GFP_KERNEL);
	if (fast_fault == NULL) {
		pr_err("<FAST_FAULT>alloc fast_fault err\n");
		goto alloc_error;
	}

	rc = oplus_chg_wls_fast_get_fault(wls_dev->wls_fast, fast_fault);
	if (rc < 0) {
		pr_err("can't get fast fault code, rc=%d\n", rc);
	}else {
		pr_err("fast fault =%s \n", fast_fault);
	}

	kfree(fast_fault);
alloc_error:
	oplus_vote(wls_dev->fastchg_disable_votable, RX_FAST_ERR_VOTER, true, 1, false);
}

static void oplus_chg_wls_rx_restart_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, rx_restart_work);
	static int retry_count;

	if (!is_rx_ic_available(wls_dev->wls_rx)) {
		if (retry_count > 5) {
			pr_err("can't found wls rx ic\n");
			retry_count = 0;
			return;
		}
		retry_count++;
		schedule_delayed_work(&wls_dev->rx_restart_work, msecs_to_jiffies(500));
	}
	if (!oplus_chg_wls_rx_is_connected(wls_dev->wls_rx)) {
		pr_info("wireless charging is not connected\n");
		return;
	}

	retry_count = 0;
	wls_dev->wls_status.online_keep = true;
	oplus_vote(wls_dev->rx_disable_votable, USER_VOTER, true, 1, false);
	msleep(1000);
	oplus_vote(wls_dev->rx_disable_votable, USER_VOTER, false, 0, false);
	if (READ_ONCE(wls_dev->wls_status.online_keep))
		schedule_delayed_work(&wls_dev->online_keep_remove_work, msecs_to_jiffies(4000));
}

static void oplus_chg_wls_online_keep_remove_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, online_keep_remove_work);

	wls_dev->wls_status.online_keep = false;
	if (!wls_dev->wls_status.rx_online) {
		if (wls_dev->rx_wake_lock_on) {
			pr_info("release rx_wake_lock\n");
			__pm_relax(wls_dev->rx_wake_lock);
			wls_dev->rx_wake_lock_on = false;
		} else {
			pr_err("rx_wake_lock is already relax\n");
		}
	}
}

static void oplus_chg_wls_verity_state_remove_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, verity_state_remove_work);

	wls_dev->wls_status.verity_state_keep = false;
}

#define VOUT_ERR_CHECK_TIME	500
#define VOUT_ERR_CHECK_COUNT	5
#define VOUT_ERR_THRESHOLD_MV	3000
static void oplus_chg_wls_vout_err_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev = container_of(dwork, struct oplus_chg_wls, wls_vout_err_work);
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	int vout_mv;
	static int vout_err_count = 0;

	if (!wls_dev->support_fastchg || !wls_status->rx_online) {
		vout_err_count = 0;
		pr_err("wireless charge is offline or not support fastchg\n");
		return;
	}

	vout_mv = wls_status->vout_mv;

#ifdef WLS_QI_DEBUG
	if (wls_dbg_vout_mv != 0)
		vout_mv = wls_dbg_vout_mv;
#endif

	if (vout_mv < VOUT_ERR_THRESHOLD_MV)
		vout_err_count++;
	else
		vout_err_count = 0;
	if (vout_err_count > VOUT_ERR_CHECK_COUNT) {
		vout_err_count = 0;
		pr_err("vout is err, disable ext pwr!\n");
		oplus_chg_wls_track_upload_rx_err_info(wls_dev, TRACK_WLS_TRX_VOUT_ABNORMAL);
		(void)oplus_chg_wls_rx_set_dcdc_enable(wls_dev->wls_rx, false);
		msleep(10);
		(void)oplus_chg_wls_set_ext_pwr_enable(wls_dev, false);
		return;
	}

	schedule_delayed_work(&wls_dev->wls_vout_err_work, msecs_to_jiffies(VOUT_ERR_CHECK_TIME));
}

static int oplus_chg_wls_dev_open(struct inode *inode, struct file *filp)
{
	struct oplus_chg_wls *wls_dev = container_of(filp->private_data,
		struct oplus_chg_wls, misc_dev);

	filp->private_data = wls_dev;
	pr_debug("%d,%d\n", imajor(inode), iminor(inode));
	return 0;
}

static ssize_t oplus_chg_wls_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct oplus_chg_wls *wls_dev = filp->private_data;
	struct wls_dev_cmd cmd;
	int rc = 0;

	mutex_lock(&wls_dev->read_lock);
	rc = wait_event_interruptible(wls_dev->read_wq, wls_dev->cmd_data_ok);
	mutex_unlock(&wls_dev->read_lock);
	if (rc)
		return rc;
	if (!wls_dev->cmd_data_ok)
		pr_err("wlchg false wakeup, rc=%d\n", rc);
	mutex_lock(&wls_dev->cmd_data_lock);
	wls_dev->cmd_data_ok = false;
	memcpy(&cmd, &wls_dev->cmd, sizeof(struct wls_dev_cmd));
	mutex_unlock(&wls_dev->cmd_data_lock);
	if (copy_to_user(buf, &cmd, sizeof(struct wls_dev_cmd))) {
		pr_err("failed to copy to user space\n");
		return -EFAULT;
	}

	return sizeof(struct wls_dev_cmd);
}

#define WLS_IOC_MAGIC			0xf1
#define WLS_NOTIFY_WLS_AUTH		_IOW(WLS_IOC_MAGIC, 1, struct wls_auth_result)

static long oplus_chg_wls_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct oplus_chg_wls *wls_dev = filp->private_data;
	struct oplus_chg_wls_status *wls_status = &wls_dev->wls_status;
	void __user *argp = (void __user *)arg;
	int rc;

	switch (cmd) {
	case WLS_NOTIFY_WLS_AUTH:
		rc = copy_from_user(&wls_status->verfity_data, argp, sizeof(struct wls_auth_result));
		if (rc) {
			pr_err("failed copy to user space\n");
			return rc;
		}
		wls_status->verity_data_ok = true;
		break;
	default:
		pr_err("bad ioctl %u\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static ssize_t oplus_chg_wls_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	return count;
}

static const struct file_operations oplus_chg_wls_dev_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.write			= oplus_chg_wls_dev_write,
	.read			= oplus_chg_wls_dev_read,
	.open			= oplus_chg_wls_dev_open,
	.unlocked_ioctl	= oplus_chg_wls_dev_ioctl,
};

static irqreturn_t oplus_chg_wls_usb_int_handler(int irq, void *dev_id)
{
	struct oplus_chg_wls *wls_dev = dev_id;

	schedule_delayed_work(&wls_dev->usb_connect_work, 0);
	return IRQ_HANDLED;
}

/*static int read_range_data_from_node(struct device_node *node,
		const char *prop_str, struct oplus_chg_wls_range_data *ranges,
		int max_threshold, u32 max_value)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;
	per_tuple_length = sizeof(struct oplus_chg_wls_range_data) / sizeof(u32);
	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n",
				prop_str, length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;

	if (tuples > WLS_MAX_STEP_CHG_ENTRIES) {
		pr_err("too many entries(%d), only %d allowed\n",
				tuples, WLS_MAX_STEP_CHG_ENTRIES);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].low_threshold >
				ranges[i].high_threshold) {
			pr_err("%s thresholds should be in ascendant ranges\n",
						prop_str);
			rc = -EINVAL;
			goto clean;
		}

		if (ranges[i].low_threshold > max_threshold)
			ranges[i].low_threshold = max_threshold;
		if (ranges[i].high_threshold > max_threshold)
			ranges[i].high_threshold = max_threshold;
		if (ranges[i].curr_ma > max_value)
			ranges[i].curr_ma = max_value;
	}

	return tuples;
clean:
	memset(ranges, 0, tuples * sizeof(struct oplus_chg_wls_range_data));
	return rc;
}*/

static int read_skin_range_data_from_node(struct device_node *node,
		const char *prop_str, struct oplus_chg_wls_skin_range_data *ranges,
		int max_threshold, u32 max_value)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;
	per_tuple_length = sizeof(struct oplus_chg_wls_skin_range_data) / sizeof(u32);
	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n",
				prop_str, length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;

	if (tuples > WLS_MAX_STEP_CHG_ENTRIES) {
		pr_err("too many entries(%d), only %d allowed\n",
				tuples, WLS_MAX_STEP_CHG_ENTRIES);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].low_threshold >
				ranges[i].high_threshold) {
			pr_err("%s thresholds should be in ascendant ranges\n",
						prop_str);
			rc = -EINVAL;
			goto clean;
		}

		if (ranges[i].low_threshold > max_threshold)
			ranges[i].low_threshold = max_threshold;
		if (ranges[i].high_threshold > max_threshold)
			ranges[i].high_threshold = max_threshold;
		if (ranges[i].curr_ma > max_value)
			ranges[i].curr_ma = max_value;
	}

	return tuples;
clean:
	memset(ranges, 0, tuples * sizeof(struct oplus_chg_wls_skin_range_data));
	return rc;
}

static int read_unsigned_data_from_node(struct device_node *node,
		const char *prop_str, u32 *addr, int len_max)
{
	int rc = 0, length;

	if (!node || !prop_str || !addr) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	if (length > len_max) {
		pr_err("too many entries(%d), only %d allowed\n",
				length, len_max);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)addr, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return rc;
}

static const char * const strategy_soc[] = {
	[WLS_FAST_SOC_0_TO_30]	= "strategy_soc_0_to_30",
	[WLS_FAST_SOC_30_TO_70]	= "strategy_soc_30_to_70",
	[WLS_FAST_SOC_70_TO_90]	= "strategy_soc_70_to_90",
};

static const char * const strategy_temp[] = {
	[WLS_FAST_TEMP_0_TO_50]		= "strategy_temp_0_to_50",
	[WLS_FAST_TEMP_50_TO_120]	= "strategy_temp_50_to_120",
	[WLS_FAST_TEMP_120_TO_160]	= "strategy_temp_120_to_160",
	[WLS_FAST_TEMP_160_TO_400]	= "strategy_temp_160_to_400",
	[WLS_FAST_TEMP_400_TO_440]	= "strategy_temp_400_to_440",
};

static const char * const bcc_soc[] = {
	[WLS_BCC_SOC_0_TO_30]	= "bcc_soc_0_to_30",
	[WLS_BCC_SOC_30_TO_70]	= "bcc_soc_30_to_70",
	[WLS_BCC_SOC_70_TO_90]	= "bcc_soc_70_to_90",
};

static const char * const bcc_temp[] = {
	[WLS_BCC_TEMP_0_TO_50]		= "bcc_temp_0_to_50",
	[WLS_BCC_TEMP_50_TO_120]	= "bcc_temp_50_to_120",
	[WLS_BCC_TEMP_120_TO_160]	= "bcc_temp_120_to_160",
	[WLS_BCC_TEMP_160_TO_400]	= "bcc_temp_160_to_400",
	[WLS_BCC_TEMP_400_TO_440]	= "bcc_temp_400_to_440",
};

static const char * const bcc_stop_curr[] = {
	[WLS_BCC_STOP_0_TO_30]	= "bcc_stop_curr_0_to_30",
	[WLS_BCC_STOP_30_TO_70]	= "bcc_stop_curr_30_to_70",
	[WLS_BCC_STOP_70_TO_90]	= "bcc_stop_curr_70_to_90",
};

static const char * const strategy_non_ffc[] = {
	[OPLUS_WLS_CHG_MODE_BPP]	= "non-ffc-bpp",
	[OPLUS_WLS_CHG_MODE_EPP]	= "non-ffc-epp",
	[OPLUS_WLS_CHG_MODE_EPP_PLUS]	= "non-ffc-epp-plus",
	[OPLUS_WLS_CHG_MODE_AIRVOOC]	= "non-ffc-airvooc",
	[OPLUS_WLS_CHG_MODE_AIRSVOOC]	= "non-ffc-airsvooc",
};

static const char * const strategy_cv[] = {
	[OPLUS_WLS_CHG_MODE_BPP]	= "cv-bpp",
	[OPLUS_WLS_CHG_MODE_EPP]	= "cv-epp",
	[OPLUS_WLS_CHG_MODE_EPP_PLUS]	= "cv-epp-plus",
	[OPLUS_WLS_CHG_MODE_AIRVOOC]	= "cv-airvooc",
	[OPLUS_WLS_CHG_MODE_AIRSVOOC]	= "cv-airsvooc",
};

static const char * const strategy_cool_down[] = {
	[OPLUS_WLS_CHG_MODE_BPP]	= "cool-down-bpp",
	[OPLUS_WLS_CHG_MODE_EPP]	= "cool-down-epp",
	[OPLUS_WLS_CHG_MODE_EPP_PLUS]	= "cool-down-epp-plus",
	[OPLUS_WLS_CHG_MODE_AIRVOOC]	= "cool-down-airvooc",
	[OPLUS_WLS_CHG_MODE_AIRSVOOC]	= "cool-down-airsvooc",
};

static const char * const strategy_skewing[] = {
	[OPLUS_WLS_SKEWING_EPP]		= "skewing-epp",
	[OPLUS_WLS_SKEWING_EPP_PLUS]	= "skewing-epp-plus",
	[OPLUS_WLS_SKEWING_AIRVOOC]	= "skewing-airvooc",
};

static int oplus_chg_wls_parse_dt(struct oplus_chg_wls *wls_dev)
{
	struct device_node *node = wls_dev->dev->of_node;
	struct device_node *wls_strategy_node, *soc_strategy_node;
	struct device_node *wls_third_part_strategy_node;
	struct device_node *wls_bcc_table_node, *soc_bcc_node;
	struct oplus_chg_wls_static_config *static_cfg = &wls_dev->static_config;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg =&wls_dev->dynamic_config;
	struct oplus_chg_wls_skin_step *skin_step;
	int i, m, j, k, length;
	int rc;

	wls_dev->support_epp_plus = of_property_read_bool(node, "oplus,support_epp_plus");
	wls_dev->support_fastchg = of_property_read_bool(node, "oplus,support_fastchg");
	wls_dev->support_get_tx_pwr = of_property_read_bool(node, "oplus,support_get_tx_pwr");
	wls_dev->support_tx_boost = of_property_read_bool(node, "oplus,support_tx_boost");
	wls_dev->support_wls_and_tx_boost = of_property_read_bool(node, "oplus,support_wls_and_tx_boost");
	wls_dev->support_wls_chg_bcc = of_property_read_bool(node, "oplus,support_wls_chg_bcc");

	rc = of_property_read_u32(node, "oplus,wls_phone_id",
				  &wls_dev->wls_phone_id);
	if (rc < 0) {
		pr_err("oplus,wls_phone_id reading failed, rc=%d\n", rc);
		wls_dev->wls_phone_id = 0x000A;
	}
	rc = of_property_read_u32(node, "oplus,wls_power_mw",
				  &wls_dev->wls_power_mw);
	if (rc < 0) {
		pr_err("oplus,oplus,wls_power_mw reading failed, rc=%d\n",
		       rc);
		wls_dev->wls_power_mw = 30000;
	}

	rc = of_property_count_elems_of_size(node, "oplus,fastchg-match-q", sizeof(u8));
	if (rc < 0) {
		pr_err("Count oplus,fastchg-match-q failed, rc=%d\n", rc);
	} else {
		length = rc;
		rc = of_property_read_u8_array(node, "oplus,fastchg-match-q",
			(u8 *)&static_cfg->fastchg_match_q, length);
		if (rc < 0)
			pr_err("oplus,fastchg-match-q reading failed, rc=%d\n", rc);
		for (j = 0; j < length / 2; j++)
			pr_err("match-q: 0x%x 0x%x\n", static_cfg->fastchg_match_q[j].id,
				static_cfg->fastchg_match_q[j].q_value);
	}

	static_cfg->fastchg_fod_enable = of_property_read_bool(node, "oplus,fastchg-fod-enable");
	if (static_cfg->fastchg_fod_enable) {
		rc = of_property_read_u8_array(node, "oplus,fastchg-fod-parm",
			(u8 *)&static_cfg->fastchg_fod_parm, WLS_FOD_PARM_LENGTH);
		if (rc < 0) {
			static_cfg->fastchg_fod_enable = false;
			pr_err("Read oplus,fastchg-fod-parm failed, rc=%d\n", rc);
		}
		rc = of_property_read_u8_array(node, "oplus,fastchg-fod-parm-12V",
			(u8 *)&static_cfg->fastchg_fod_parm_12V, WLS_FOD_PARM_LENGTH);
		static_cfg->fastchg_12V_fod_enable = true;
		if (rc < 0) {
			static_cfg->fastchg_12V_fod_enable = false;
			pr_err("Read oplus,fastchg-fod-parm-12V failed, rc=%d\n", rc);
		}
	}

	rc = of_property_read_string(node, "oplus,wls_chg_fw", &wls_dev->wls_chg_fw_name);
	if (rc < 0) {
		pr_err("oplus,wls_chg_fw reading failed, rc=%d\n", rc);
		wls_dev->wls_chg_fw_name = "IDT_P9415_default";
	}

	rc = of_property_read_u32(node, "oplus,max-voltage-mv", &dynamic_cfg->batt_vol_max_mv);
	if (rc < 0) {
		pr_err("oplus,max-voltage-mv reading failed, rc=%d\n", rc);
		dynamic_cfg->batt_vol_max_mv = 4550;
	}

	rc = of_property_read_u32(node, "oplus,fastchg_curr_max_ma", &dynamic_cfg->fastchg_curr_max_ma);
	if (rc < 0) {
		pr_err("oplus,fastchg_curr_max_ma reading failed, rc=%d\n", rc);
		dynamic_cfg->fastchg_curr_max_ma = 1500;
	}

	rc = of_property_read_u32(node, "oplus,verity_curr_max_ma", &dynamic_cfg->verity_curr_max_ma);
	if (rc < 0) {
		pr_err("oplus,verity_curr_max_ma reading failed, rc=%d\n", rc);
		dynamic_cfg->verity_curr_max_ma = dynamic_cfg->fastchg_curr_max_ma;
	}

	/*fcc_step->fcc_step = dynamic_cfg->fcc_step;
	rc = read_range_data_from_node(node, "oplus,fastchg-fcc_step",
				       dynamic_cfg->fcc_step,
				       450, // todo: temp
				       2500); //todo: current
	if (rc < 0) {
		pr_err("Read oplus,fastchg-fcc_step failed, rc=%d\n", rc);
		dynamic_cfg->fcc_step[0].low_threshold = 0;
		dynamic_cfg->fcc_step[0].high_threshold = 405;
		dynamic_cfg->fcc_step[0].curr_ma = 2250;
		dynamic_cfg->fcc_step[0].vol_max_mv = 4420;
		dynamic_cfg->fcc_step[0].need_wait = 1;
		dynamic_cfg->fcc_step[0].max_soc = 50;

		dynamic_cfg->fcc_step[1].low_threshold = 380;
		dynamic_cfg->fcc_step[1].high_threshold = 420;
		dynamic_cfg->fcc_step[1].curr_ma = 1500;
		dynamic_cfg->fcc_step[1].vol_max_mv = 4450;
		dynamic_cfg->fcc_step[1].need_wait = 1;
		dynamic_cfg->fcc_step[1].max_soc = 70;

		dynamic_cfg->fcc_step[2].low_threshold = 390;
		dynamic_cfg->fcc_step[2].high_threshold = 420;
		dynamic_cfg->fcc_step[2].curr_ma = 850;
		dynamic_cfg->fcc_step[2].vol_max_mv = 4480;
		dynamic_cfg->fcc_step[2].need_wait = 1;
		dynamic_cfg->fcc_step[2].max_soc = 90;

		dynamic_cfg->fcc_step[3].low_threshold = 400;
		dynamic_cfg->fcc_step[3].high_threshold = 420;
		dynamic_cfg->fcc_step[3].curr_ma =625;
		dynamic_cfg->fcc_step[3].vol_max_mv = 4480;
		dynamic_cfg->fcc_step[3].need_wait = 0;
		dynamic_cfg->fcc_step[3].max_soc = 90;
		fcc_step->max_step = 4;
	} else {
		fcc_step->max_step = rc;
	}
	for(i = 0; i < fcc_step->max_step; i++) {
		if (fcc_step->fcc_step[i].low_threshold > 0)
			fcc_step->allow_fallback[i] = true;
		else
			fcc_step->allow_fallback[i] = false;
	}*/

	wls_strategy_node = of_get_child_by_name(node, "wireless_fastchg_strategy");
	if (!wls_strategy_node) {
		pr_err("Can not find wireless_fastchg_strategy node\n");
		return -EINVAL;
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		soc_strategy_node = of_get_child_by_name(wls_strategy_node, strategy_soc[i]);
		if (!soc_strategy_node) {
			pr_err("Can not find %s node\n", strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			rc = of_property_count_elems_of_size(soc_strategy_node, strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				pr_err("Count %s failed, rc=%d\n", strategy_temp[j], rc);
				return rc;
			}

			length = rc;
			rc = of_property_read_u32_array(soc_strategy_node, strategy_temp[j],
					(u32 *)wls_dev->fcc_steps[i].fcc_step[j].fcc_step,
					length);
			wls_dev->fcc_steps[i].fcc_step[j].max_step = length/5;
		}
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			for (k = 0; k < wls_dev->fcc_steps[i].fcc_step[j].max_step; k++) {
				pr_err("%s: %d %d %d %d %d\n", __func__,
					wls_dev->fcc_steps[i].fcc_step[j].fcc_step[k].low_threshold,
					wls_dev->fcc_steps[i].fcc_step[j].fcc_step[k].high_threshold,
					wls_dev->fcc_steps[i].fcc_step[j].fcc_step[k].curr_ma,
					wls_dev->fcc_steps[i].fcc_step[j].fcc_step[k].vol_max_mv,
					wls_dev->fcc_steps[i].fcc_step[j].fcc_step[k].need_wait);
			}
		}
	}

	wls_bcc_table_node = of_get_child_by_name(node, "wireless_bcc_table");
	if (!wls_bcc_table_node) {
		pr_err("Can not find wireless_bcc_table node\n");
	} else {
		rc = of_property_read_u32(node, "oplus,wls-bcc-fcc-to-icl-factor",
					  &wls_dev->wls_bcc_fcc_to_icl_factor);
		if (rc < 0) {
			pr_err("oplus,wls-bcc-fcc-to-icl-factor reading failed, rc=%d\n",
				   rc);
			wls_dev->wls_bcc_fcc_to_icl_factor = 2;
		}

		for (i = 0; i < WLS_BCC_SOC_MAX; i++) {
			soc_bcc_node = of_get_child_by_name(wls_bcc_table_node, bcc_soc[i]);
			if (!soc_bcc_node)
				pr_err("Can not find %s node\n", bcc_soc[i]);

			for (j = 0; j < WLS_BCC_TEMP_MAX; j++) {
				rc = of_property_count_elems_of_size(soc_bcc_node, bcc_temp[j], sizeof(u32));
				if (rc < 0) {
					pr_err("Count %s failed, rc=%d\n", bcc_temp[j], rc);
					return rc;
				}

				length = rc;
				rc = of_property_read_u32_array(soc_bcc_node, bcc_temp[j],
						(u32 *)wls_dev->bcc_steps[i].bcc_step[j].bcc_step,
						length);
				wls_dev->bcc_steps[i].bcc_step[j].max_step = length/4;
			}
		}

		for (i = 0; i < WLS_BCC_SOC_MAX; i++) {
			for (j = 0; j < WLS_BCC_TEMP_MAX; j++) {
				for (k = 0; k < wls_dev->bcc_steps[i].bcc_step[j].max_step; k++) {
					pr_err("%s: bcc_step: %d %d %d %d\n", __func__,
						wls_dev->bcc_steps[i].bcc_step[j].bcc_step[k].max_batt_volt,
						wls_dev->bcc_steps[i].bcc_step[j].bcc_step[k].max_curr,
						wls_dev->bcc_steps[i].bcc_step[j].bcc_step[k].min_curr,
						wls_dev->bcc_steps[i].bcc_step[j].bcc_step[k].exit);
				}
			}
		}
	}

	rc = read_unsigned_data_from_node(node, "bcc_stop_curr_0_to_30",
			dynamic_cfg->bcc_stop_curr_0_to_30, WLS_BCC_STOP_CURR_NUM);
	if (rc < 0) {
		pr_err("get wls,bcc_stop_curr_0_to_30 property error, rc=%d\n", rc);
		for (i = 0; i < WLS_BCC_STOP_CURR_NUM; i++)
			dynamic_cfg->bcc_stop_curr_0_to_30[i] = 1000;
	} else {
		for (i = 0; i < WLS_BCC_STOP_CURR_NUM; i++) {
			pr_err("%s: bcc_stop_0_30: %d", __func__,
				dynamic_cfg->bcc_stop_curr_0_to_30[i]);
		}
	}

	rc = read_unsigned_data_from_node(node, "bcc_stop_curr_30_to_70",
			dynamic_cfg->bcc_stop_curr_30_to_70, WLS_BCC_STOP_CURR_NUM);
	if (rc < 0) {
		pr_err("get wls,bcc_stop_curr_30_to_70 property error, rc=%d\n", rc);
		for (i = 0; i < WLS_BCC_STOP_CURR_NUM; i++)
			dynamic_cfg->bcc_stop_curr_30_to_70[i] = 1000;
	} else {
		for (i = 0; i < WLS_BCC_STOP_CURR_NUM; i++) {
			pr_err("%s: bcc_stop_30_70: %d", __func__,
				dynamic_cfg->bcc_stop_curr_30_to_70[i]);
		}
	}

	rc = read_unsigned_data_from_node(node, "bcc_stop_curr_70_to_90",
			dynamic_cfg->bcc_stop_curr_70_to_90, WLS_BCC_STOP_CURR_NUM);
	if (rc < 0) {
		pr_err("get wls,bcc_stop_curr_70_to_90 property error, rc=%d\n", rc);
		for (i = 0; i < WLS_BCC_STOP_CURR_NUM; i++)
			dynamic_cfg->bcc_stop_curr_70_to_90[i] = 1000;
	} else {
		for (i = 0; i < WLS_BCC_STOP_CURR_NUM; i++) {
			pr_err("%s: bcc_stop_70_90: %d", __func__,
				dynamic_cfg->bcc_stop_curr_70_to_90[i]);
		}
	}

	wls_third_part_strategy_node = of_get_child_by_name(node,
	    "wireless_fastchg_third_part_strategy");
	if (!wls_third_part_strategy_node) {
		pr_err("Can not find wls third_part_strategy node\n");
		memcpy(wls_dev->fcc_third_part_steps,
		    wls_dev->fcc_steps, sizeof(wls_dev->fcc_steps));
		goto non_ffc;
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		soc_strategy_node = of_get_child_by_name(
		    wls_third_part_strategy_node, strategy_soc[i]);
		if (!soc_strategy_node) {
			pr_err("Can not find %s node\n", strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			rc = of_property_count_elems_of_size(
			    soc_strategy_node, strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				pr_err("Count %s failed, rc=%d\n", strategy_temp[j], rc);
				return rc;
			}

			length = rc;
			rc = of_property_read_u32_array(soc_strategy_node, strategy_temp[j],
					(u32 *)wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step,
					length);
			wls_dev->fcc_third_part_steps[i].fcc_step[j].max_step = length/5;
		}
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			for (k = 0; k < wls_dev->fcc_third_part_steps[i].fcc_step[j].max_step; k++) {
				pr_err("third_part %s: %d %d %d %d %d\n", __func__,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].low_threshold,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].high_threshold,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].curr_ma,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].vol_max_mv,
				    wls_dev->fcc_third_part_steps[i].fcc_step[j].fcc_step[k].need_wait);
			}
		}
	}

non_ffc:
	/* non-ffc */
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		rc = of_property_count_elems_of_size(node, strategy_non_ffc[i], sizeof(u32));
		if (rc < 0) {
			pr_err("Count %s failed, rc=%d\n", strategy_non_ffc[i], rc);
			return rc;
		}
		length = rc;
		rc = of_property_read_u32_array(node, strategy_non_ffc[i],
			(u32 *)dynamic_cfg->non_ffc_step[i], length);
		wls_dev->non_ffc_step[i].max_step = length / 5;
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		for (j = 0; j < wls_dev->non_ffc_step[i].max_step; j++) {
			pr_err("%s: %d %d %d %d %d\n", __func__,
				dynamic_cfg->non_ffc_step[i][j].low_threshold,
				dynamic_cfg->non_ffc_step[i][j].high_threshold,
				dynamic_cfg->non_ffc_step[i][j].curr_ma,
				dynamic_cfg->non_ffc_step[i][j].vol_max_mv,
				dynamic_cfg->non_ffc_step[i][j].need_wait);
		}
	}

	/* cv */
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		rc = of_property_count_elems_of_size(node, strategy_cv[i], sizeof(u32));
		if (rc < 0) {
			pr_err("Count %s failed, rc=%d\n", strategy_cv[i], rc);
			return rc;
		}
		length = rc;
		rc = of_property_read_u32_array(node, strategy_cv[i],
			(u32 *)dynamic_cfg->cv_step[i], length);
		wls_dev->cv_step[i].max_step = length / 5;
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		for (j = 0; j < wls_dev->cv_step[i].max_step; j++) {
			pr_err("%s: %d %d %d %d %d\n", __func__,
				dynamic_cfg->cv_step[i][j].low_threshold,
				dynamic_cfg->cv_step[i][j].high_threshold,
				dynamic_cfg->cv_step[i][j].curr_ma,
				dynamic_cfg->cv_step[i][j].vol_max_mv,
				dynamic_cfg->cv_step[i][j].need_wait);
		}
	}

	/* cool down */
	rc = of_property_read_u32(node, "oplus,cool-down-12v-thr", &dynamic_cfg->cool_down_12v_thr);
	if (rc < 0) {
		pr_err("cool-down-12v-thr reading failed, rc=%d\n", rc);
		dynamic_cfg->cool_down_12v_thr = -EINVAL;
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		rc = of_property_count_elems_of_size(node, strategy_cool_down[i], sizeof(u32));
		if (rc < 0) {
			pr_err("Count %s failed, rc=%d\n", strategy_cool_down[i], rc);
			return rc;
		}
		length = rc;
		rc = read_unsigned_data_from_node(node, strategy_cool_down[i],
			(u32 *)dynamic_cfg->cool_down_step[i], length);
		wls_dev->cool_down_step[i].max_step = length;
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		for (j = 0; j < wls_dev->cool_down_step[i].max_step; j++) {
			pr_err("%s: %d\n", __func__, dynamic_cfg->cool_down_step[i][j]);
		}
	}

	/* skewing */
	for (i = 0; i < OPLUS_WLS_SKEWING_MAX; i++) {
		rc = of_property_count_elems_of_size(node, strategy_skewing[i], sizeof(u32));
		if (rc < 0) {
			pr_err("Count %s failed, rc=%d\n", strategy_skewing[i], rc);
			return rc;
		}
		length = rc;
		rc = read_unsigned_data_from_node(node, strategy_skewing[i],
			(u32 *)dynamic_cfg->skewing_step[i], length);
		wls_dev->skewing_step[i].max_step = length / 3;
	}
	for (i = 0; i < OPLUS_WLS_SKEWING_MAX; i++) {
		for (j = 0; j < wls_dev->skewing_step[i].max_step; j++) {
			pr_err("%s: %d %d %d\n", __func__,
				dynamic_cfg->skewing_step[i][j].curr_ma,
				dynamic_cfg->skewing_step[i][j].fallback_step,
				dynamic_cfg->skewing_step[i][j].switch_to_bpp);
		}
	}

	rc = of_property_read_u32(node, "oplus,bpp-vol-mv", &dynamic_cfg->bpp_vol_mv);
	if (rc < 0) {
		pr_err("oplus,bpp-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->bpp_vol_mv = 5000;
	}
	rc = of_property_read_u32(node, "oplus,epp-vol-mv", &dynamic_cfg->epp_vol_mv);
	if (rc < 0) {
		pr_err("oplus,epp-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->epp_vol_mv = 10000;
	}
	rc = of_property_read_u32(node, "oplus,epp_plus-vol-mv", &dynamic_cfg->epp_plus_vol_mv);
	if (rc < 0) {
		pr_err("oplus,epp_plus-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->epp_plus_vol_mv = 10000;
	}
	rc = of_property_read_u32(node, "oplus,vooc-vol-mv", &dynamic_cfg->vooc_vol_mv);
	if (rc < 0) {
		pr_err("oplus,vooc-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->vooc_vol_mv = 10000;
	}
	rc = of_property_read_u32(node, "oplus,svooc-vol-mv", &dynamic_cfg->svooc_vol_mv);
	if (rc < 0) {
		pr_err("oplus,svooc-vol-mv reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->svooc_vol_mv = 12000;
	}

	rc = read_unsigned_data_from_node(node, "oplus,iclmax-ma",
		(u32 *)(&dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW]),
		BATT_TEMP_INVALID * OPLUS_WLS_CHG_MODE_MAX);
	if (rc < 0) {
		pr_err("get oplus,iclmax-ma property error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
			for (m = 0; m < BATT_TEMP_INVALID; m++)
				dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m] =
					default_config.iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m];
		}
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		wls_dev->icl_max_ma[i] = 0;
		for (m = 0; m < BATT_TEMP_INVALID; m++) {
			if (dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m] > wls_dev->icl_max_ma[i])
				wls_dev->icl_max_ma[i] = dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][m];
		}
	}

	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		pr_err("OPLUS_WLS_CHG_BATT_CL_LOW: %d %d %d %d %d %d %d %d %d\n",
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][0],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][1],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][2],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][3],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][4],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][5],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][6],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][7],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_LOW][i][8]);
	}
	rc = read_unsigned_data_from_node(node, "oplus,iclmax-batt-high-ma",
		(u32 *)(&dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH]),
		BATT_TEMP_INVALID * OPLUS_WLS_CHG_MODE_MAX);
	if (rc < 0) {
		pr_err("get oplus,iclmax-ma property error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
			for (m = 0; m < BATT_TEMP_INVALID; m++)
				dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m] =
					default_config.iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m];
		}
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		for (m = 0; m < BATT_TEMP_INVALID; m++) {
			if (dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m] > wls_dev->icl_max_ma[i])
				wls_dev->icl_max_ma[i] = dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][m];
		}
	}
	for (i = 0; i < OPLUS_WLS_CHG_MODE_MAX; i++) {
		pr_err("OPLUS_WLS_CHG_BATT_CL_HIGH: %d %d %d %d %d %d %d %d %d\n",
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][0],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][1],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][2],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][3],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][4],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][5],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][6],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][7],
			dynamic_cfg->iclmax_ma[OPLUS_WLS_CHG_BATT_CL_HIGH][i][8]);
	}
	skin_step = &wls_dev->epp_plus_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_plus_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp_plus-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_plus_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_plus_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_plus_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
	}
	for (i = 0; i < skin_step->max_step; i++){
		pr_err("epp_plus-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}

	skin_step = &wls_dev->epp_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
	}
	for (i = 0; i < skin_step->max_step; i++){
		pr_err("epp-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}
	skin_step = &wls_dev->bpp_skin_step;
	skin_step->skin_step = dynamic_cfg->bpp_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,bpp-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.bpp_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.bpp_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.bpp_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
	}
	for (i = 0; i < skin_step->max_step; i++){
		pr_err("bpp-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}
	skin_step = &wls_dev->epp_plus_led_on_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_plus_led_on_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp_plus-led-on-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_plus_led_on_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_plus_led_on_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_plus_led_on_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
	}
	for (i = 0; i < skin_step->max_step; i++){
		pr_err("epp_plus-led-on-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}
	skin_step = &wls_dev->epp_led_on_skin_step;
	skin_step->skin_step = dynamic_cfg->epp_led_on_skin_step;
	rc = read_skin_range_data_from_node(node, "oplus,epp-led-on-skin-step",
					    skin_step->skin_step,
					    WLS_SKIN_TEMP_MAX,
					    wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP]);
	if (rc < 0) {
		for (i = 0; i < WLS_MAX_STEP_CHG_ENTRIES; i++) {
			skin_step->skin_step[i].low_threshold =
				default_config.epp_led_on_skin_step[i].low_threshold;
			skin_step->skin_step[i].high_threshold =
				default_config.epp_led_on_skin_step[i].high_threshold;
			skin_step->skin_step[i].curr_ma =
				default_config.epp_led_on_skin_step[i].curr_ma;
			if ((skin_step->skin_step[i].low_threshold == 0) &&
			    (skin_step->skin_step[i].high_threshold == 0) &&
			    (skin_step->skin_step[i].curr_ma == 0)) {
				skin_step->max_step = i;
				break;
			}
		}
	} else {
		skin_step->max_step = rc;
	}
	for (i = 0; i < skin_step->max_step; i++){
		pr_err("epp-led-on-skin-step: %d %d %d\n",
			skin_step->skin_step[i].low_threshold,
			skin_step->skin_step[i].high_threshold,
			skin_step->skin_step[i].curr_ma);
	}

	pr_err("icl_max_ma BPP EPP EPP_PLUS AIRVOOC AIRSVOOC: %d %d %d %d %d\n",
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_BPP],
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP],
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_EPP_PLUS],
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_AIRVOOC],
		wls_dev->icl_max_ma[OPLUS_WLS_CHG_MODE_AIRSVOOC]);

	rc = read_chg_strategy_data_from_node(
		node, "oplus,wls-fast-chg-led-off-strategy-data",
		dynamic_cfg->wls_fast_chg_led_off_strategy_data);
	if (rc < 0) {
		pr_err("oplus,wls-fast-chg-led-off-strategy-data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("pd9v_chg_led_off_strategy[%d]: %d %d %d %d %d\n", i,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].cool_temp,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].heat_temp,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].curr_data,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].heat_next_index,
			dynamic_cfg->wls_fast_chg_led_off_strategy_data[i].cool_next_index);
	}

	rc = read_chg_strategy_data_from_node(node, "oplus,wls-fast-chg-led-on-strategy-data", dynamic_cfg->wls_fast_chg_led_on_strategy_data);
	if (rc < 0) {
		pr_err("oplus,wls-fast-chg-led-on-strategy-data error, rc=%d\n", rc);
	}
	for (i = 0; i < rc; i++) {
		pr_err("pd9v_chg_led_on_strategy[%d]: %d %d %d %d %d\n", i,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].cool_temp,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].heat_temp,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].curr_data,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].heat_next_index,
			dynamic_cfg->wls_fast_chg_led_on_strategy_data[i].cool_next_index);
	}

	rc = of_property_read_u32(node, "oplus,wls-fast-chg-call-on-curr-ma",
				  &dynamic_cfg->wls_fast_chg_call_on_curr_ma);
	if (rc < 0) {
		pr_err("oplus,oplus,wls-fast-chg-call-on-curr-ma reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->wls_fast_chg_call_on_curr_ma = 600;
	}
	rc = of_property_read_u32(node, "oplus,wls-fast-chg-camera-on-curr-ma",
				  &dynamic_cfg->wls_fast_chg_camera_on_curr_ma);
	if (rc < 0) {
		pr_err("oplus,oplus,wls-fast-chg-camera-on-curr-ma reading failed, rc=%d\n",
		       rc);
		dynamic_cfg->wls_fast_chg_camera_on_curr_ma = 600;
	}

	rc = of_property_read_u32(node, "oplus,fastchg-max-soc",
					&dynamic_cfg->fastchg_max_soc);
	if (rc < 0) {
		pr_err("oplus,fastchg-max-soc reading failed, rc=%d\n", rc);
		dynamic_cfg->fastchg_max_soc = 90;
        }

	return 0;
}

static int oplus_chg_wls_gpio_init(struct oplus_chg_wls *wls_dev)
{
	int rc = 0;
	struct device_node *node = wls_dev->dev->of_node;

	wls_dev->pinctrl = devm_pinctrl_get(wls_dev->dev);
	if (IS_ERR_OR_NULL(wls_dev->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -ENODEV;
	}

	wls_dev->wrx_en_gpio = of_get_named_gpio(node, "oplus,wrx_en-gpio", 0);
	if (!gpio_is_valid(wls_dev->wrx_en_gpio)) {
		pr_err("wrx_en_gpio not specified\n");
		return -ENODEV;
	}
	rc = gpio_request(wls_dev->wrx_en_gpio, "wrx_en-gpio");
	if (rc < 0) {
		pr_err("wrx_en_gpio request error, rc=%d\n", rc);
		return rc;
	}
	wls_dev->wrx_en_active = pinctrl_lookup_state(wls_dev->pinctrl, "wrx_en_active");
	if (IS_ERR_OR_NULL(wls_dev->wrx_en_active)) {
		pr_err("get wrx_en_active fail\n");
		goto free_wrx_en_gpio;
	}
	wls_dev->wrx_en_sleep = pinctrl_lookup_state(wls_dev->pinctrl, "wrx_en_sleep");
	if (IS_ERR_OR_NULL(wls_dev->wrx_en_sleep)) {
		pr_err("get wrx_en_sleep fail\n");
		goto free_wrx_en_gpio;
	}
	gpio_direction_output(wls_dev->wrx_en_gpio, 0);
	pinctrl_select_state(wls_dev->pinctrl, wls_dev->wrx_en_sleep);

	if (!wls_dev->support_fastchg)
		return 0;

	wls_dev->ext_pwr_en_gpio = of_get_named_gpio(node, "oplus,ext_pwr_en-gpio", 0);
	if (!gpio_is_valid(wls_dev->ext_pwr_en_gpio)) {
		pr_err("ext_pwr_en_gpio not specified\n");
		goto free_wrx_en_gpio;
	}
	rc = gpio_request(wls_dev->ext_pwr_en_gpio, "ext_pwr_en-gpio");
	if (rc < 0) {
		pr_err("ext_pwr_en_gpio request error, rc=%d\n", rc);
		goto free_wrx_en_gpio;
	}
	wls_dev->ext_pwr_en_active = pinctrl_lookup_state(wls_dev->pinctrl, "ext_pwr_en_active");
	if (IS_ERR_OR_NULL(wls_dev->ext_pwr_en_active)) {
		pr_err("get ext_pwr_en_active fail\n");
		goto free_ext_pwr_en_gpio;
	}
	wls_dev->ext_pwr_en_sleep = pinctrl_lookup_state(wls_dev->pinctrl, "ext_pwr_en_sleep");
	if (IS_ERR_OR_NULL(wls_dev->ext_pwr_en_sleep)) {
		pr_err("get ext_pwr_en_sleep fail\n");
		goto free_ext_pwr_en_gpio;
	}
	gpio_direction_output(wls_dev->ext_pwr_en_gpio, 0);
	pinctrl_select_state(wls_dev->pinctrl, wls_dev->ext_pwr_en_sleep);

	wls_dev->usb_int_gpio = of_get_named_gpio(node, "oplus,usb_int-gpio", 0);
	if (!gpio_is_valid(wls_dev->usb_int_gpio)) {
		pr_err("usb_int_gpio not specified\n");
		goto out;
	}
	rc = gpio_request(wls_dev->usb_int_gpio, "usb_int-gpio");
	if (rc < 0) {
		pr_err("usb_int_gpio request error, rc=%d\n", rc);
		goto free_ext_pwr_en_gpio;
	}
	gpio_direction_input(wls_dev->usb_int_gpio);
	wls_dev->usb_int_irq = gpio_to_irq(wls_dev->usb_int_gpio);
	rc = devm_request_irq(wls_dev->dev, wls_dev->usb_int_irq,
			      oplus_chg_wls_usb_int_handler,
			      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			      "usb_int_irq", wls_dev);
	if (rc < 0) {
		pr_err("usb_int_irq request error, rc=%d\n", rc);
		goto free_int_gpio;
	}
	enable_irq_wake(wls_dev->usb_int_irq);

out:
	return 0;

free_int_gpio:
	if (!gpio_is_valid(wls_dev->usb_int_gpio))
		gpio_free(wls_dev->usb_int_gpio);
free_ext_pwr_en_gpio:
	if (!gpio_is_valid(wls_dev->ext_pwr_en_gpio))
		gpio_free(wls_dev->ext_pwr_en_gpio);
free_wrx_en_gpio:
	if (!gpio_is_valid(wls_dev->wrx_en_gpio))
		gpio_free(wls_dev->wrx_en_gpio);
	return rc;
}

#ifndef CONFIG_OPLUS_CHG_OOS
static ssize_t oplus_chg_wls_proc_deviated_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[7];
	size_t len = 7;

	memset(page, 0, 7);
	len = snprintf(page, len, "%s\n", "false");
	ret = simple_read_from_buffer(buf, count, ppos, page, len);

	return ret;
}

static const struct proc_ops oplus_chg_wls_proc_deviated_ops = {
	.proc_read = oplus_chg_wls_proc_deviated_read,
	.proc_write = NULL,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_tx_read(struct file *file, char __user *buf,
					  size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10] = { 0 };
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	union oplus_chg_mod_propval val;
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	rc = oplus_chg_mod_get_property(wls_dev->wls_ocm,
					OPLUS_CHG_PROP_TRX_STATUS, &val);
	if (rc < 0) {
		pr_err("can't get wls trx status, rc=%d\n", rc);
		return (ssize_t)rc;
	}
#ifndef CONFIG_OPLUS_CHG_OOS
	/*
	*When the wireless reverse charging error occurs,
	*the upper layer turns off the reverse charging button.
	*/
	if(wls_dev->wls_status.trx_close_delay) {
		val.intval = OPLUS_CHG_WLS_TRX_STATUS_ENABLE;
		schedule_delayed_work(&wls_dev->wls_clear_trx_work, msecs_to_jiffies(3000));
	}
#endif
	switch (val.intval) {
	case OPLUS_CHG_WLS_TRX_STATUS_ENABLE:
		snprintf(page, 10, "%s\n", "enable");
		break;
	case OPLUS_CHG_WLS_TRX_STATUS_CHARGING:
		snprintf(page, 10, "%s\n", "charging");
		break;
	case OPLUS_CHG_WLS_TRX_STATUS_DISENABLE:
	default:
		snprintf(page, 10, "%s\n", "disable");
		break;
	}

	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));
	return ret;
}

static ssize_t oplus_chg_wls_proc_tx_write(struct file *file,
					   const char __user *buf, size_t count,
					   loff_t *lo)
{
	char buffer[5] = { 0 };
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int val;
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (count > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		pr_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	pr_err("buffer=%s", buffer);
	rc = kstrtoint(buffer, 0, &val);
	if (rc != 0)
		return -EINVAL;
	pr_err("val = %d", val);
#ifndef CONFIG_OPLUS_CHG_OOS
	/*
	*When the wireless reverse charging error occurs,
	*the upper layer turns off the reverse charging button.
	*/
	wls_dev->wls_status.trx_close_delay = false;
#endif
	rc = oplus_chg_wls_set_trx_enable(wls_dev, !!val);
	if (rc < 0) {
		pr_err("can't enable trx, rc=%d\n", rc);
		return (ssize_t)rc;
	}
	return count;
}

static const struct proc_ops oplus_chg_wls_proc_tx_ops = {
	.proc_read = oplus_chg_wls_proc_tx_read,
	.proc_write = oplus_chg_wls_proc_tx_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_rx_read(struct file *file, char __user *buf,
					  size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[3];
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	memset(page, 0, 3);
	snprintf(page, 3, "%c\n", wls_dev->charge_enable ? '1' : '0');
	ret = simple_read_from_buffer(buf, count, ppos, page, 3);
	return ret;
}

static ssize_t oplus_chg_wls_proc_rx_write(struct file *file,
					   const char __user *buf, size_t count,
					   loff_t *lo)
{
	char buffer[5] = { 0 };
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int val;
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (count > 5) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		pr_err("%s: error.\n", __func__);
		return -EFAULT;
	}

	pr_err("buffer=%s", buffer);
	rc = kstrtoint(buffer, 0, &val);
	if (rc != 0)
		return -EINVAL;
	pr_err("val = %d", val);

	wls_dev->charge_enable = !!val;
	oplus_vote(wls_dev->rx_disable_votable, DEBUG_VOTER, !wls_dev->charge_enable,
	     1, false);
	pr_info("%s wls rx\n", wls_dev->charge_enable ? "enable" : "disable");
	return count;
}

static const struct proc_ops oplus_chg_wls_proc_rx_ops = {
	.proc_read = oplus_chg_wls_proc_rx_read,
	.proc_write = oplus_chg_wls_proc_rx_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_user_sleep_mode_read(struct file *file,
						       char __user *buf,
						       size_t count,
						       loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	pr_err("quiet_mode_need = %d.\n", wls_dev->wls_status.quiet_mode_need);
	sprintf(page, "%d", wls_dev->wls_status.quiet_mode_need);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_user_sleep_mode_write(struct file *file,
							const char __user *buf,
							size_t len, loff_t *lo)
{
	char buffer[4] = { 0 };
	int pmw_pulse = 0;
	int rc = -1;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	union oplus_chg_mod_propval pval;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (len > 4) {
		pr_err("len[%d] -EFAULT\n", len);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len)) {
		pr_err("copy from user error\n");
		return -EFAULT;
	}

	pr_err("user mode: buffer=%s\n", buffer);
	rc = kstrtoint(buffer, 0, &pmw_pulse);
	if (rc != 0)
		return -EINVAL;
	if (pmw_pulse == WLS_FASTCHG_MODE) {
		pval.intval = 0;
		rc = oplus_chg_mod_set_property(wls_dev->wls_ocm,
				OPLUS_CHG_PROP_QUIET_MODE, &pval);
		if (rc == 0)
			wls_dev->wls_status.quiet_mode_need = WLS_FASTCHG_MODE;
		pr_err("set user mode: %d, fastchg mode, rc: %d\n", pmw_pulse,
		       rc);
	} else if (pmw_pulse == WLS_SILENT_MODE) {
		pval.intval = 1;
		rc = oplus_chg_mod_set_property(wls_dev->wls_ocm,
				OPLUS_CHG_PROP_QUIET_MODE, &pval);
		if (rc == 0)
			wls_dev->wls_status.quiet_mode_need = WLS_SILENT_MODE;
		pr_err("set user mode: %d, silent mode, rc: %d\n", pmw_pulse,
		       rc);
		//nu1619_set_dock_led_pwm_pulse(3);
	} else if (pmw_pulse == WLS_BATTERY_FULL_MODE) {
		pr_err("set user mode: %d, battery full mode\n", pmw_pulse);
		wls_dev->wls_status.quiet_mode_need = WLS_BATTERY_FULL_MODE;
		//nu1619_set_dock_fan_pwm_pulse(60);
	} else if (pmw_pulse == WLS_CALL_MODE) {
		pr_err("set user mode: %d, call mode\n", pmw_pulse);
		//chip->nu1619_chg_status.call_mode = true;
	} else if (pmw_pulse == WLS_EXIT_CALL_MODE) {
		//chip->nu1619_chg_status.call_mode = false;
		pr_err("set user mode: %d, exit call mode\n", pmw_pulse);
	} else {
		pr_err("user sleep mode: pmw_pulse: %d\n", pmw_pulse);
		wls_dev->wls_status.quiet_mode_need = pmw_pulse;
		//nu1619_set_dock_fan_pwm_pulse(pmw_pulse);
	}

	return len;
}

static const struct proc_ops oplus_chg_wls_proc_user_sleep_mode_ops = {
	.proc_read = oplus_chg_wls_proc_user_sleep_mode_read,
	.proc_write = oplus_chg_wls_proc_user_sleep_mode_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_idt_adc_test_read(struct file *file,
						    char __user *buf,
						    size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	int rx_adc_result = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (wls_dev->wls_status.rx_adc_test_pass == true) {
		rx_adc_result = 1;
	} else {
		rx_adc_result = 0;
	}
	rx_adc_result = 1; // needn't test
	/*pr_err("rx_adc_test: result %d.\n", rx_adc_result);*/
	sprintf(page, "%d", rx_adc_result);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_idt_adc_test_write(struct file *file,
						     const char __user *buf,
						     size_t len, loff_t *lo)
{
	char buffer[4] = { 0 };
	int rx_adc_cmd = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (len > 4) {
		pr_err("%s: len[%d] -EFAULT.\n", __func__, len);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len)) {
		pr_err("%s:  error.\n", __func__);
		return -EFAULT;
	}

	rc = kstrtoint(buffer, 0, &rx_adc_cmd);
	if (rc != 0)
		return -EINVAL;
	if (rx_adc_cmd == 0) {
		pr_err("rx_adc_test: set 0.\n");
		wls_dev->wls_status.rx_adc_test_enable = false;
	} else if (rx_adc_cmd == 1) {
		pr_err("rx_adc_test: set 1.\n");
		wls_dev->wls_status.rx_adc_test_enable = true;
	} else {
		pr_err("rx_adc_test: set %d, invalid.\n", rx_adc_cmd);
	}

	return len;
}

static const struct proc_ops oplus_chg_wls_proc_idt_adc_test_ops = {
	.proc_read = oplus_chg_wls_proc_idt_adc_test_read,
	.proc_write = oplus_chg_wls_proc_idt_adc_test_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_rx_power_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16] = { 0 };
	int rx_power = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	rx_power = wls_dev->wls_status.rx_pwr_mw;
	if (wls_dev->wls_status.trx_online)
		rx_power = 1563;

	/*pr_info("rx_power = %d\n", rx_power);*/
	sprintf(page, "%d\n", rx_power);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_rx_power_write(struct file *file,
						 const char __user *buf,
						 size_t count, loff_t *lo)
{
	return count;
}

static const struct proc_ops oplus_chg_wls_proc_rx_power_ops = {
	.proc_read = oplus_chg_wls_proc_rx_power_read,
	.proc_write = oplus_chg_wls_proc_rx_power_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_tx_power_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16] = { 0 };
	int tx_power = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	tx_power = wls_dev->wls_status.tx_pwr_mw;
	if (wls_dev->wls_status.trx_online)
		tx_power = wls_dev->wls_status.trx_vol_mv * wls_dev->wls_status.trx_curr_ma / 1000;

	/*pr_info("tx_power = %d\n", tx_power);*/
	sprintf(page, "%d\n", tx_power);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_tx_power_write(struct file *file,
						 const char __user *buf,
						 size_t count, loff_t *lo)
{
	return count;
}

static const struct proc_ops oplus_chg_wls_proc_tx_power_ops = {
	.proc_read = oplus_chg_wls_proc_tx_power_read,
	.proc_write = oplus_chg_wls_proc_tx_power_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_rx_version_read(struct file *file,
						  char __user *buf,
						  size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16] = { 0 };
	u32 rx_version = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	rc = oplus_chg_wls_rx_get_rx_version(wls_dev->wls_rx, &rx_version);
	if (rc < 0) {
		pr_err("can't get rx version, rc=%d\n", rc);
		return rc;
	}

	/*pr_info("rx_version = 0x%x\n", rx_version);*/
	sprintf(page, "0x%x\n", rx_version);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_rx_version_write(struct file *file,
						   const char __user *buf,
						   size_t count, loff_t *lo)
{
	return count;
}

static const struct proc_ops oplus_chg_wls_proc_rx_version_ops = {
	.proc_read = oplus_chg_wls_proc_rx_version_read,
	.proc_write = oplus_chg_wls_proc_rx_version_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_tx_version_read(struct file *file,
						  char __user *buf,
						  size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[16] = { 0 };
	u32 tx_version = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	rc = oplus_chg_wls_rx_get_trx_version(wls_dev->wls_rx, &tx_version);
	if (rc < 0) {
		pr_err("can't get tx version, rc=%d\n", rc);
		return rc;
	}

	/*pr_info("tx_version = 0x%x\n", tx_version);*/
	sprintf(page, "0x%x\n", tx_version);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t oplus_chg_wls_proc_tx_version_write(struct file *file,
						   const char __user *buf,
						   size_t count, loff_t *lo)
{
	return count;
}

static const struct proc_ops oplus_chg_wls_proc_tx_version_ops = {
	.proc_read = oplus_chg_wls_proc_tx_version_read,
	.proc_write = oplus_chg_wls_proc_tx_version_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static ssize_t oplus_chg_wls_proc_ftm_mode_read(struct file *file,
						char __user *buf, size_t count,
						loff_t *ppos)
{
	uint8_t ret = 0;
	char page[256];
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	sprintf(page, "ftm_mode[%d], engineering_mode[%d]\n", wls_dev->ftm_mode,
		wls_dev->factory_mode);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

#define FTM_MODE_DISABLE 0
#define FTM_MODE_ENABLE 1
#define ENGINEERING_MODE_ENABLE 2
#define ENGINEERING_MODE_DISABLE 3
static ssize_t oplus_chg_wls_proc_ftm_mode_write(struct file *file,
						 const char __user *buf,
						 size_t len, loff_t *lo)
{
	char buffer[4] = { 0 };
	int ftm_mode = 0;
	struct oplus_chg_wls *wls_dev = PDE_DATA(file_inode(file));
	int rc;

	if (wls_dev == NULL) {
		pr_err("wls dev is not fount\n");
		return -ENODEV;
	}

	if (len > 4) {
		pr_err("len[%d] -EFAULT\n", len);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len)) {
		pr_err("copy from user error\n");
		return -EFAULT;
	}

	pr_err("ftm mode: buffer=%s\n", buffer);
	rc = kstrtoint(buffer, 0, &ftm_mode);
	if (rc != 0)
		return -EINVAL;
	if (ftm_mode == FTM_MODE_DISABLE) {
		wls_dev->ftm_mode = false;
	} else if (ftm_mode == FTM_MODE_ENABLE) {
		wls_dev->ftm_mode = true;
	} else if (ftm_mode == ENGINEERING_MODE_ENABLE) {
		wls_dev->factory_mode = true;
	} else if (ftm_mode == ENGINEERING_MODE_DISABLE) {
		wls_dev->factory_mode = false;
	}

	return len;
}

static const struct proc_ops oplus_chg_wls_proc_ftm_mode_ops = {
	.proc_read = oplus_chg_wls_proc_ftm_mode_read,
	.proc_write = oplus_chg_wls_proc_ftm_mode_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};

static int oplus_chg_wls_init_charge_proc(struct oplus_chg_wls *wls_dev)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_da = NULL;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_da = proc_mkdir("wireless", NULL);
	if (prEntry_da == NULL) {
		pr_err("Couldn't create wireless proc entry\n");
		return -ENOMEM;
	}

	prEntry_tmp = proc_create_data("enable_tx", 0664, prEntry_da,
				       &oplus_chg_wls_proc_tx_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create enable_tx proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("deviated", 0664, prEntry_da,
				 &oplus_chg_wls_proc_deviated_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create deviated proc entry\n");
		goto fail;
	}

	prEntry_tmp = proc_create_data("user_sleep_mode", 0664, prEntry_da,
				       &oplus_chg_wls_proc_user_sleep_mode_ops,
				       wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create user_sleep_mode proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("idt_adc_test", 0664, prEntry_da,
				 &oplus_chg_wls_proc_idt_adc_test_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create idt_adc_test proc entry\n");
		goto fail;
	}

	prEntry_tmp = proc_create_data("enable_rx", 0664, prEntry_da,
				       &oplus_chg_wls_proc_rx_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create enable_rx proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("rx_power", 0664, prEntry_da,
				 &oplus_chg_wls_proc_rx_power_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create rx_power proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("tx_power", 0664, prEntry_da,
				 &oplus_chg_wls_proc_tx_power_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create tx_power proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("rx_version", 0664, prEntry_da,
				 &oplus_chg_wls_proc_rx_version_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create rx_version proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("tx_version", 0664, prEntry_da,
				 &oplus_chg_wls_proc_tx_version_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create tx_version proc entry\n");
		goto fail;
	}

	prEntry_tmp =
		proc_create_data("ftm_mode", 0664, prEntry_da,
				 &oplus_chg_wls_proc_ftm_mode_ops, wls_dev);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		pr_err("Couldn't create ftm_mode proc entry\n");
		goto fail;
	}

	return 0;

fail:
	remove_proc_entry("wireless", NULL);
	return ret;
}

#endif

static void oplus_chg_wls_track_trx_info_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, trx_info_load_trigger_work);

	if (!wls_dev)
		return;

	oplus_chg_track_upload_trigger_data(wls_dev->trx_info_load_trigger);
}

static void oplus_chg_wls_track_rx_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wls *wls_dev =
		container_of(dwork, struct oplus_chg_wls, rx_err_load_trigger_work);

	if (!wls_dev->rx_err_load_trigger)
		return;
	oplus_chg_track_upload_trigger_data(*(wls_dev->rx_err_load_trigger));
	if (wls_dev->rx_err_load_trigger) {
		kfree(wls_dev->rx_err_load_trigger);
		wls_dev->rx_err_load_trigger = NULL;
	}
	wls_dev->rx_err_uploading = false;
}

static int oplus_chg_wls_track_init(struct oplus_chg_wls *wls_dev)
{
	wls_dev->trx_info_load_trigger.type_reason =
		TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	wls_dev->trx_info_load_trigger.flag_reason =
		TRACK_NOTIFY_FLAG_WLS_TRX_INFO;

	wls_dev->rx_err_uploading = false;
	wls_dev->rx_err_load_trigger = NULL;

	mutex_init(&wls_dev->track_upload_lock);

	INIT_DELAYED_WORK(&wls_dev->trx_info_load_trigger_work,
		oplus_chg_wls_track_trx_info_load_trigger_work);
	INIT_DELAYED_WORK(&wls_dev->rx_err_load_trigger_work,
		oplus_chg_wls_track_rx_err_load_trigger_work);

	return 0;
}

static int oplus_chg_wls_driver_probe(struct platform_device *pdev)
{
	struct oplus_chg_wls *wls_dev;
	int boot_mode;
	bool usb_present;
	int rc;

	wls_dev = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_wls), GFP_KERNEL);
	if (wls_dev == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}

	wls_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, wls_dev);

	rc = oplus_chg_wls_parse_dt(wls_dev);
	if (rc < 0) {
		pr_err("oplus chg wls parse dts error, rc=%d\n", rc);
		goto parse_dt_err;
	}

	rc = oplus_chg_wls_init_mod(wls_dev);
	if (rc < 0) {
		pr_err("oplus chg wls mod init error, rc=%d\n", rc);
		goto wls_mod_init_err;
	}

	rc = oplus_chg_wls_track_init(wls_dev);
	if (rc < 0) {
		pr_err("oplus chg wls track init error, rc=%d\n", rc);
		goto wls_track_init_err;
	}

	wls_dev->wls_wq = alloc_ordered_workqueue("wls_wq", WQ_FREEZABLE | WQ_HIGHPRI);
	if (!wls_dev->wls_wq) {
		pr_err("alloc wls work error\n");
		rc = -ENOMEM;
		goto alloc_work_err;
	}

	rc = oplus_chg_wls_rx_init(wls_dev);
	if (rc < 0) {
		goto rx_init_err;
	}
	rc = oplus_chg_wls_rx_register_msg_callback(wls_dev->wls_rx, wls_dev,
		oplus_chg_wls_rx_msg_callback);
	if (!rc)
		wls_dev->msg_callback_ok = true;
	rc = oplus_chg_wls_nor_init(wls_dev);
	if (rc < 0) {
		goto nor_init_err;
	}
	if (wls_dev->support_fastchg) {
		rc = oplus_chg_wls_fast_init(wls_dev);
		if (rc < 0) {
			goto fast_init_err;
		}
	}

	rc = oplus_chg_wls_gpio_init(wls_dev);
	if (rc < 0)
		goto gpio_init_err;

	wls_dev->fcc_votable = oplus_create_votable("WLS_FCC", VOTE_MIN,
				oplus_chg_wls_fcc_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->fcc_votable)) {
		rc = PTR_ERR(wls_dev->fcc_votable);
		wls_dev->fcc_votable = NULL;
		goto create_fcc_votable_err;
	}

	wls_dev->fastchg_disable_votable = oplus_create_votable("WLS_FASTCHG_DISABLE",
				VOTE_SET_ANY,
				oplus_chg_wls_fastchg_disable_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->fastchg_disable_votable)) {
		rc = PTR_ERR(wls_dev->fastchg_disable_votable);
		wls_dev->fastchg_disable_votable = NULL;
		goto create_disable_votable_err;
	}
	wls_dev->wrx_en_votable = oplus_create_votable("WLS_WRX_EN",
				VOTE_SET_ANY,
				oplus_chg_wls_wrx_en_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->wrx_en_votable)) {
		rc = PTR_ERR(wls_dev->wrx_en_votable);
		wls_dev->wrx_en_votable = NULL;
		goto create_wrx_en_votable_err;
	}
	wls_dev->nor_icl_votable = oplus_create_votable("WLS_NOR_ICL",
				VOTE_MIN,
				oplus_chg_wls_nor_icl_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_icl_votable)) {
		rc = PTR_ERR(wls_dev->nor_icl_votable);
		wls_dev->nor_icl_votable = NULL;
		goto create_nor_icl_votable_err;
	}
	wls_dev->nor_fcc_votable = oplus_create_votable("WLS_NOR_FCC",
				VOTE_MIN,
				oplus_chg_wls_nor_fcc_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_fcc_votable)) {
		rc = PTR_ERR(wls_dev->nor_fcc_votable);
		wls_dev->nor_fcc_votable = NULL;
		goto create_nor_fcc_votable_err;
	}
	wls_dev->nor_fv_votable = oplus_create_votable("WLS_NOR_FV",
				VOTE_MIN,
				oplus_chg_wls_nor_fv_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_fv_votable)) {
		rc = PTR_ERR(wls_dev->nor_fv_votable);
		wls_dev->nor_fv_votable = NULL;
		goto create_nor_fv_votable_err;
	}
	wls_dev->nor_out_disable_votable = oplus_create_votable("WLS_NOR_OUT_DISABLE",
				VOTE_SET_ANY,
				oplus_chg_wls_nor_out_disable_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_out_disable_votable)) {
		rc = PTR_ERR(wls_dev->nor_out_disable_votable);
		wls_dev->nor_out_disable_votable = NULL;
		goto create_nor_out_disable_votable_err;
	}
	wls_dev->nor_input_disable_votable = oplus_create_votable("WLS_NOR_INPUT_DISABLE",
				VOTE_SET_ANY,
				oplus_chg_wls_nor_input_disable_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->nor_input_disable_votable)) {
		rc = PTR_ERR(wls_dev->nor_input_disable_votable);
		wls_dev->nor_input_disable_votable = NULL;
		goto create_nor_input_disable_votable_err;
	}
	wls_dev->rx_disable_votable = oplus_create_votable("WLS_RX_DISABLE",
				VOTE_SET_ANY,
				oplus_chg_wls_rx_disable_vote_callback,
				wls_dev);
	if (IS_ERR(wls_dev->rx_disable_votable)) {
		rc = PTR_ERR(wls_dev->rx_disable_votable);
		wls_dev->rx_disable_votable = NULL;
		goto create_rx_disable_votable_err;
	}

	wls_dev->misc_dev.minor = MISC_DYNAMIC_MINOR;
	wls_dev->misc_dev.name = "wls_dev";
	wls_dev->misc_dev.fops = &oplus_chg_wls_dev_fops;
	rc = misc_register(&wls_dev->misc_dev);
	if (rc) {
		pr_err("misc_register failed, rc=%d\n", rc);
		goto misc_reg_err;
	}

#ifndef CONFIG_OPLUS_CHG_OOS
	rc = oplus_chg_wls_init_charge_proc(wls_dev);
	if (rc < 0) {
		pr_err("can't init charge proc, rc=%d\n", rc);
		goto init_charge_proc_err;
	}
#endif

	INIT_DELAYED_WORK(&wls_dev->wls_rx_sm_work, oplus_chg_wls_rx_sm);
	INIT_DELAYED_WORK(&wls_dev->wls_trx_sm_work, oplus_chg_wls_trx_sm);
	INIT_DELAYED_WORK(&wls_dev->wls_connect_work, oplus_chg_wls_connect_work);
	INIT_DELAYED_WORK(&wls_dev->wls_send_msg_work, oplus_chg_wls_send_msg_work);
	INIT_DELAYED_WORK(&wls_dev->wls_upgrade_fw_work, oplus_chg_wls_upgrade_fw_work);
	INIT_DELAYED_WORK(&wls_dev->usb_int_work, oplus_chg_wls_usb_int_work);
	INIT_DELAYED_WORK(&wls_dev->wls_data_update_work, oplus_chg_wls_data_update_work);
	INIT_DELAYED_WORK(&wls_dev->wls_trx_disconnect_work, oplus_chg_wls_trx_disconnect_work);
	INIT_DELAYED_WORK(&wls_dev->usb_connect_work, oplus_chg_wls_usb_connect_work);
	INIT_DELAYED_WORK(&wls_dev->wls_start_work, oplus_chg_wls_start_work);
	INIT_DELAYED_WORK(&wls_dev->wls_break_work, oplus_chg_wls_break_work);
	INIT_DELAYED_WORK(&wls_dev->fod_cal_work, oplus_chg_wls_fod_cal_work);
	INIT_DELAYED_WORK(&wls_dev->rx_restore_work, oplus_chg_wls_rx_restore_work);
	INIT_DELAYED_WORK(&wls_dev->rx_iic_restore_work, oplus_chg_wls_rx_iic_restore_work);
	INIT_DELAYED_WORK(&wls_dev->rx_verity_restore_work, oplus_chg_wls_rx_verity_restore_work);
	INIT_DELAYED_WORK(&wls_dev->fast_fault_check_work, oplus_chg_wls_fast_fault_check_work);
	INIT_DELAYED_WORK(&wls_dev->rx_restart_work, oplus_chg_wls_rx_restart_work);
	INIT_DELAYED_WORK(&wls_dev->online_keep_remove_work, oplus_chg_wls_online_keep_remove_work);
	INIT_DELAYED_WORK(&wls_dev->verity_state_remove_work, oplus_chg_wls_verity_state_remove_work);
	INIT_DELAYED_WORK(&wls_dev->wls_verity_work, oplus_chg_wls_verity_work);
	INIT_DELAYED_WORK(&wls_dev->wls_get_third_part_verity_data_work,
		oplus_chg_wls_get_third_part_verity_data_work);
#ifndef CONFIG_OPLUS_CHG_OOS
	INIT_DELAYED_WORK(&wls_dev->wls_clear_trx_work, oplus_chg_wls_clear_trx_work);
#endif
	INIT_DELAYED_WORK(&wls_dev->wls_skewing_work, oplus_chg_wls_skewing_work);
	INIT_DELAYED_WORK(&wls_dev->wls_bcc_curr_update_work, oplus_wls_bcc_curr_update_work);
	INIT_DELAYED_WORK(&wls_dev->wls_vout_err_work, oplus_chg_wls_vout_err_work);
	init_completion(&wls_dev->msg_ack);
	mutex_init(&wls_dev->connect_lock);
	mutex_init(&wls_dev->read_lock);
	mutex_init(&wls_dev->cmd_data_lock);
	mutex_init(&wls_dev->send_msg_lock);
	mutex_init(&wls_dev->update_data_lock);
	init_waitqueue_head(&wls_dev->read_wq);

	wls_dev->rx_wake_lock = wakeup_source_register(wls_dev->dev, "rx_wake_lock");
	wls_dev->trx_wake_lock = wakeup_source_register(wls_dev->dev, "trx_wake_lock");

	wls_dev->charge_enable = true;
	wls_dev->batt_charge_enable = true;

	boot_mode = get_boot_mode();
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#ifndef CONFIG_OPLUS_CHARGER_MTK
	if (boot_mode == MSM_BOOT_MODE__FACTORY)
#else
	if (boot_mode == FACTORY_BOOT || boot_mode == ATE_FACTORY_BOOT)
#endif
		wls_dev->ftm_mode = true;
#endif

	usb_present = oplus_chg_wls_is_usb_present(wls_dev);
	if (usb_present) {
		wls_dev->usb_present = true;
		schedule_delayed_work(&wls_dev->usb_int_work, 0);
	} else {
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#ifndef CONFIG_OPLUS_CHARGER_MTK
		if (boot_mode == MSM_BOOT_MODE__CHARGE)
#else
		if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT)
#endif
			wls_dev->wls_status.boot_online_keep = true;
#endif
		(void)oplus_chg_wls_rx_connect_check(wls_dev->wls_rx);
	}

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	(void)oplus_wls_reg_debug_config(wls_dev);
#endif

	pr_info("probe done\n");

	return 0;

#ifndef CONFIG_OPLUS_CHG_OOS
init_charge_proc_err:
	misc_deregister(&wls_dev->misc_dev);
#endif
misc_reg_err:
	oplus_destroy_votable(wls_dev->rx_disable_votable);
create_rx_disable_votable_err:
	oplus_destroy_votable(wls_dev->nor_input_disable_votable);
create_nor_input_disable_votable_err:
	oplus_destroy_votable(wls_dev->nor_out_disable_votable);
create_nor_out_disable_votable_err:
	oplus_destroy_votable(wls_dev->nor_fv_votable);
create_nor_fv_votable_err:
	oplus_destroy_votable(wls_dev->nor_fcc_votable);
create_nor_fcc_votable_err:
	oplus_destroy_votable(wls_dev->nor_icl_votable);
create_nor_icl_votable_err:
	oplus_destroy_votable(wls_dev->wrx_en_votable);
create_wrx_en_votable_err:
	oplus_destroy_votable(wls_dev->fastchg_disable_votable);
create_disable_votable_err:
	oplus_destroy_votable(wls_dev->fcc_votable);
create_fcc_votable_err:
	disable_irq(wls_dev->usb_int_irq);
	if (!gpio_is_valid(wls_dev->usb_int_gpio))
		gpio_free(wls_dev->usb_int_gpio);
	if (!gpio_is_valid(wls_dev->ext_pwr_en_gpio))
		gpio_free(wls_dev->ext_pwr_en_gpio);
	if (!gpio_is_valid(wls_dev->wrx_en_gpio))
		gpio_free(wls_dev->wrx_en_gpio);
gpio_init_err:
	if (wls_dev->support_fastchg)
		oplus_chg_wls_fast_remove(wls_dev);
fast_init_err:
	oplus_chg_wls_nor_remove(wls_dev);
nor_init_err:
	oplus_chg_wls_rx_remove(wls_dev);
rx_init_err:
	destroy_workqueue(wls_dev->wls_wq);
alloc_work_err:
	oplus_chg_unreg_changed_notifier(&wls_dev->wls_changed_nb);
	oplus_chg_unreg_event_notifier(&wls_dev->wls_event_nb);
	oplus_chg_unreg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
	oplus_chg_mod_unregister(wls_dev->wls_ocm);
wls_track_init_err:
wls_mod_init_err:
parse_dt_err:
	devm_kfree(&pdev->dev, wls_dev);
	return rc;
}

static int oplus_chg_wls_driver_remove(struct platform_device *pdev)
{
	struct oplus_chg_wls *wls_dev = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	oplus_wls_unreg_debug_config(wls_dev);
#endif
#ifndef CONFIG_OPLUS_CHG_OOS
	remove_proc_entry("wireless", NULL);
#endif
	misc_deregister(&wls_dev->misc_dev);
	oplus_destroy_votable(wls_dev->rx_disable_votable);
	oplus_destroy_votable(wls_dev->nor_out_disable_votable);
	oplus_destroy_votable(wls_dev->nor_input_disable_votable);
	oplus_destroy_votable(wls_dev->nor_fv_votable);
	oplus_destroy_votable(wls_dev->nor_fcc_votable);
	oplus_destroy_votable(wls_dev->nor_icl_votable);
	oplus_destroy_votable(wls_dev->wrx_en_votable);
	oplus_destroy_votable(wls_dev->fastchg_disable_votable);
	oplus_destroy_votable(wls_dev->fcc_votable);
	disable_irq(wls_dev->usb_int_irq);
	if (!gpio_is_valid(wls_dev->usb_int_gpio))
		gpio_free(wls_dev->usb_int_gpio);
	if (!gpio_is_valid(wls_dev->ext_pwr_en_gpio))
		gpio_free(wls_dev->ext_pwr_en_gpio);
	if (!gpio_is_valid(wls_dev->wrx_en_gpio))
		gpio_free(wls_dev->wrx_en_gpio);
	if (wls_dev->support_fastchg)
		oplus_chg_wls_fast_remove(wls_dev);
	oplus_chg_wls_nor_remove(wls_dev);
	oplus_chg_wls_rx_remove(wls_dev);
	destroy_workqueue(wls_dev->wls_wq);
	oplus_chg_unreg_changed_notifier(&wls_dev->wls_changed_nb);
	oplus_chg_unreg_event_notifier(&wls_dev->wls_event_nb);
	oplus_chg_unreg_mod_notifier(wls_dev->wls_ocm, &wls_dev->wls_mod_nb);
	oplus_chg_mod_unregister(wls_dev->wls_ocm);
	devm_kfree(&pdev->dev, wls_dev);
	return 0;
}

static const struct of_device_id oplus_chg_wls_match[] = {
	{ .compatible = "oplus,wireless-charge" },
	{},
};

static struct platform_driver oplus_chg_wls_driver = {
	.driver = {
		.name = "oplus_chg_wls",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_wls_match),
	},
	.probe = oplus_chg_wls_driver_probe,
	.remove = oplus_chg_wls_driver_remove,
};

static __init int oplus_chg_wls_driver_init(void)
{
	return platform_driver_register(&oplus_chg_wls_driver);
}

static __exit void oplus_chg_wls_driver_exit(void)
{
	platform_driver_unregister(&oplus_chg_wls_driver);
}

oplus_chg_module_register(oplus_chg_wls_driver);
