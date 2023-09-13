// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[COMM]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/power_supply.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#include <linux/oem/boot_mode.h>
#include <linux/oem/oplus_chg_voter.h>
#else
#include "oplus_chg_core.h"
#include "oplus_chg_voter.h"
#endif
#include <drm/drm_panel.h>
#include <linux/fb.h>
#include "oplus_chg_module.h"
#include "oplus_chg_comm.h"
#include "op_wlchg_v2/oplus_chg_wls.h"
#include "oplus_charger.h"
#include "oplus_gauge.h"

static int camera_on_count;

extern struct oplus_chg_chip *g_oplus_chip;
extern bool oplus_get_wired_chg_present(void);

struct oplus_chg_comm {
	struct device *dev;
	struct oplus_chg_mod *comm_ocm;
	struct notifier_block comm_changed_nb;
	struct notifier_block comm_event_nb;
	struct notifier_block comm_mod_nb;
	struct oplus_chg_mod *batt_ocm;
	struct oplus_chg_mod *usb_ocm;
	struct oplus_chg_mod *wls_ocm;

	struct votable *wls_icl_votable;
	struct votable *wls_fcc_votable;
	struct votable *wls_fv_votable;
	struct votable *wls_nor_out_dis_votable;
	struct votable *wls_rx_dis_votable;

	int batt_temp_dynamic_thr[BATT_TEMP_INVALID - 1];
	int ffc_temp_dynamic_thr[FFC_TEMP_INVALID - 1];
	enum oplus_chg_temp_region temp_region;
	enum oplus_chg_ffc_temp_region ffc_temp_region;
	struct oplus_chg_comm_config comm_cfg;

	struct iio_channel *skin_therm_chan;
	struct drm_panel *lcd_panel;
	struct notifier_block lcd_active_nb;

	bool chg_ovp;
	bool chg_done;
	bool recharge_pending;
	bool recharge_status;
	bool time_out;
	bool vbus_uovp;
	bool is_aging_test;
	bool ffc_charging;
	bool is_power_changed;
	bool batt_vol_high;
	bool batt_ovp;
	bool wls_chging_keep;
	bool initialized;

	int vchg_max_mv;
	int vchg_min_mv;
	int ffc_step;
	int ffc_fcc_count;
	int ffc_fv_count;
	int timeout_count;

	enum oplus_chg_ffc_status ffc_status;
	enum oplus_chg_batt_status battery_status;

	struct delayed_work heartbeat_work;
	struct delayed_work wls_chging_keep_clean_work;
	bool cmd_data_ok;
	bool hidl_handle_cmd_ready;
	struct mutex read_lock;
	struct mutex cmd_data_lock;
	struct mutex cmd_ack_lock;
	struct oplus_chg_cmd cmd;
	struct completion cmd_ack;
	wait_queue_head_t read_wq;
};

static struct oplus_chg_comm_config default_chg = {
	.check_batt_full_by_sw = (uint8_t)true,
	.fv_offset_voltage_mv = 50,
	.little_cold_iterm_ma = 0,
	.sw_iterm_ma = 115,
	.full_count_sw_num = 2,
	.batt_uv_mv = 2500,
	.batt_ov_mv = 4950,
	.batt_oc_ma = 6500,
	.batt_ovd_mv = 1000,
	.batt_temp_thr = { -20, 0, 50, 120, 160, 450, 500 },
	.vbatmax_mv = { 0, 4435, 4435, 4435, 4435, 4435, 4130, 0 },
	.usb_ffc_fv_mv = {4500},
	.usb_ffc_fv_cutoff_mv = {4445},
	.usb_ffc_fcc_ma = { { 350, 400 }, },
	.usb_ffc_fcc_cutoff_ma = { { 300, 350 }, },
	.wls_ffc_step_max = 2,
	.wls_ffc_fv_mv = { 4500, 4500 },
	.wls_ffc_fv_cutoff_mv = { 4445, 4435 },
	.wls_ffc_icl_ma = { { 800, 800 }, { 550, 550 } },
	.wls_ffc_fcc_ma = { { 550, 550 }, { 350, 350 } },
	.wls_ffc_fcc_cutoff_ma = { { 400, 400 }, { 200, 200 } },
	.bpp_vchg_min_mv = 4000,
	.bpp_vchg_max_mv = 8000,
	.epp_vchg_min_mv = 4000,
	.epp_vchg_max_mv = 14000,
	.fast_vchg_min_mv = 4000,
	.fast_vchg_max_mv = 22000,
	.batt_curr_limit_thr_mv = 4180,
};
static ATOMIC_NOTIFIER_HEAD(comm_ocm_notifier);
static ATOMIC_NOTIFIER_HEAD(comm_mutual_notifier);

__maybe_unused static bool is_batt_ocm_available(struct oplus_chg_comm *dev)
{
	if (!dev->batt_ocm)
		dev->batt_ocm = oplus_chg_mod_get_by_name("battery");
	return !!dev->batt_ocm;
}

__maybe_unused static bool is_usb_ocm_available(struct oplus_chg_comm *dev)
{
	if (!dev->usb_ocm)
		dev->usb_ocm = oplus_chg_mod_get_by_name("usb");
	return !!dev->usb_ocm;
}

__maybe_unused static bool is_wls_ocm_available(struct oplus_chg_comm *dev)
{
	if (!dev->wls_ocm)
		dev->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!dev->wls_ocm;
}

static bool is_wls_icl_votable_available(struct oplus_chg_comm *dev)
{
	if (!dev->wls_icl_votable)
		dev->wls_icl_votable = oplus_find_votable("WLS_NOR_ICL");
	return !!dev->wls_icl_votable;
}

static bool is_wls_fcc_votable_available(struct oplus_chg_comm *dev)
{
	if (!dev->wls_fcc_votable)
		dev->wls_fcc_votable = oplus_find_votable("WLS_NOR_FCC");
	return !!dev->wls_fcc_votable;
}

static bool is_wls_fv_votable_available(struct oplus_chg_comm *dev)
{
	if (!dev->wls_fv_votable)
		dev->wls_fv_votable = oplus_find_votable("WLS_NOR_FV");
	return !!dev->wls_fv_votable;
}

static bool is_wls_nor_out_dis_votable_available(struct oplus_chg_comm *dev)
{
	if (!dev->wls_nor_out_dis_votable)
		dev->wls_nor_out_dis_votable = oplus_find_votable("WLS_NOR_OUT_DISABLE");
	return !!dev->wls_nor_out_dis_votable;
}

static bool is_wls_rx_dis_votable_available(struct oplus_chg_comm *dev)
{
	if (!dev->wls_rx_dis_votable)
		dev->wls_rx_dis_votable = oplus_find_votable("WLS_RX_DISABLE");
	return !!dev->wls_rx_dis_votable;
}

static void oplus_chg_set_charge_done(struct oplus_chg_comm *comm_dev, bool done)
{
	comm_dev->chg_done = done;
	oplus_chg_global_event(comm_dev->comm_ocm,
			       done ? OPLUS_CHG_EVENT_CHARGE_DONE : OPLUS_CHG_EVENT_CLEAN_CHARGE_DONE);
}

static int oplus_chg_wls_charger_vol(struct oplus_chg_comm *comm_dev, int *vchg_mv)
{
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_wls_ocm_available(comm_dev)) {
		pr_err("batt ocm not found\n");
		return -ENODEV;
	}
	rc = oplus_chg_mod_get_property(comm_dev->wls_ocm, OPLUS_CHG_PROP_VOLTAGE_NOW, &pval);
	if (rc < 0)
		return rc;
#ifdef CONFIG_OPLUS_CHG_OOS
	*vchg_mv = pval.intval / 1000;
#else
	*vchg_mv = pval.intval;
#endif

	return 0;
}

static int oplus_chg_get_batt_temp(struct oplus_chg_comm *comm_dev)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return BATT_NON_TEMP;
	}

	return g_oplus_chip->temperature;
}

static int oplus_chg_get_ibat(struct oplus_chg_comm *comm_dev, int *ibat_ma)
{
	*ibat_ma = oplus_gauge_get_batt_current();
	return 0;
}

static int oplus_chg_get_vbat(struct oplus_chg_comm *comm_dev, int *vbat_mv)
{
	*vbat_mv = oplus_gauge_get_batt_mvolts();
	return 0;
}

static int oplus_chg_get_batt_soc(struct oplus_chg_comm *comm_dev, int *soc)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return -ENODEV;
	}

	*soc = g_oplus_chip->ui_soc;

	return 0;
}

static int oplus_chg_get_batt_real_soc(struct oplus_chg_comm *comm_dev, int *soc)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return -ENODEV;
	}

	*soc = g_oplus_chip->soc;

	return 0;
}

static int oplus_chg_get_batt_status(struct oplus_chg_comm *comm_dev)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}

	return g_oplus_chip->prop_status;
}

static int oplus_chg_set_notify_code(struct oplus_chg_comm *comm_dev, int bit, bool en)
{
	//TODO 8450
	return 0;
}

static int oplus_chg_clean_notify_code(struct oplus_chg_comm *comm_dev)
{
	//TODO 8450
	return 0;
}

static bool is_battery_present(struct oplus_chg_comm *comm_dev)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return false;
	}

	return g_oplus_chip->batt_exist;
}

static bool is_usb_charger_online(struct oplus_chg_comm *comm_dev)
{
	return oplus_get_wired_chg_present();
}

static bool is_wls_charger_online(struct oplus_chg_comm *comm_dev)
{
	union oplus_chg_mod_propval val;
	bool online = false;
	int rc;

	if (!is_wls_ocm_available(comm_dev)) {
		pr_err("wls ocm not found\n");
		return false;
	}

	rc = oplus_chg_mod_get_property(comm_dev->wls_ocm, OPLUS_CHG_PROP_ONLINE, &val);
	if (rc < 0)
		pr_err("can't get wls online, rc=%d\n", rc);
	else
		online = !!val.intval;

	pr_debug("wls is %s\n", online ? "online" : "offline");

	return online;
}

static bool is_wls_charger_present(struct oplus_chg_comm *comm_dev)
{
	union oplus_chg_mod_propval val;
	bool present = false;
	int rc;

	if (!is_wls_ocm_available(comm_dev)) {
		pr_err("wls ocm not found\n");
		return false;
	}

	rc = oplus_chg_mod_get_property(comm_dev->wls_ocm, OPLUS_CHG_PROP_PRESENT, &val);
	if (rc < 0)
		pr_err("can't get wls present status, rc=%d\n", rc);
	else
		present = !!val.intval;
	rc = oplus_chg_mod_get_property(comm_dev->wls_ocm, OPLUS_CHG_PROP_ONLINE_KEEP, &val);
	if (rc < 0)
		pr_err("can't get wls online keep status, rc=%d\n", rc);
	else
		present = present || (!!val.intval);

	pr_debug("wls is %spresent\n", present ? "" : "not ");

	return present;
}

static bool is_wls_fastchg_started(struct oplus_chg_comm *comm_dev)
{
	union oplus_chg_mod_propval val;
	bool started = false;
	int rc;

	if (!is_wls_ocm_available(comm_dev)) {
		pr_err("wls ocm not found\n");
		return false;
	}

	rc = oplus_chg_mod_get_property(comm_dev->wls_ocm, OPLUS_CHG_PROP_FASTCHG_STATUS, &val);
	if (rc < 0)
		pr_err("can't get fastchg status, rc=%d\n", rc);
	else
		started = !!val.intval;

	pr_debug("wls fastchg is %sstarted\n", started ? "" : "not ");

	return started;
}

static enum oplus_chg_wls_type oplus_chg_get_wls_charge_type(struct oplus_chg_comm *comm_dev)
{
	union oplus_chg_mod_propval val;
	enum oplus_chg_wls_type wls_type;
	int rc;

	if (!is_wls_ocm_available(comm_dev)) {
		pr_err("wls ocm not found\n");
		return OPLUS_CHG_WLS_UNKNOWN;
	}

	rc = oplus_chg_mod_get_property(comm_dev->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &val);
	if (rc < 0) {
		pr_err("can't get wls charger voltage, rc=%d\n", rc);
		return OPLUS_CHG_WLS_UNKNOWN;
	}
	wls_type = val.intval;
	return wls_type;
}

static int oplus_chg_comm_get_skin_temp(struct oplus_chg_comm *comm_dev)
{
	return 250;
}

static enum oplus_chg_mod_property oplus_chg_comm_props[] = {
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_SHIP_MODE,
	OPLUS_CHG_PROP_SKIN_TEMP,
	OPLUS_CHG_PROP_CALL_ON,
	OPLUS_CHG_PROP_CAMERA_ON,
};

static enum oplus_chg_mod_property oplus_chg_comm_uevent_props[] = {
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_SHIP_MODE,
	OPLUS_CHG_PROP_SKIN_TEMP,
};

static int oplus_chg_comm_get_prop(struct oplus_chg_mod *ocm,
			enum oplus_chg_mod_property prop,
			union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_comm *comm_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int rc = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	case OPLUS_CHG_PROP_CHG_ENABLE:
		pval->intval = 1;
		break;
	case OPLUS_CHG_PROP_SHIP_MODE:
		pval->intval = chip->enable_shipmode;
		break;
	case OPLUS_CHG_PROP_SKIN_TEMP:
		pval->intval = oplus_chg_comm_get_skin_temp(comm_dev);
		break;
	case OPLUS_CHG_PROP_CAMERA_ON:
		pval->intval = camera_on_count;
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

static int oplus_chg_comm_set_prop(struct oplus_chg_mod *ocm, enum oplus_chg_mod_property prop,
				   const union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_comm *comm_dev = oplus_chg_mod_get_drvdata(ocm);
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int rc = 0;

	if (!chip) {
		// pr_err("oplus chip is NULL\n");
		return -ENODEV;
	}

	switch (prop) {
	case OPLUS_CHG_PROP_CHG_ENABLE:
		break;
	case OPLUS_CHG_PROP_SHIP_MODE:
		chip->enable_shipmode = pval->intval;
		oplus_gauge_update_soc_smooth_parameter();
		break;
	case OPLUS_CHG_PROP_CALL_ON:
		if (!!pval->intval) {
			if (is_wls_ocm_available(comm_dev))
				oplus_chg_anon_mod_event(comm_dev->wls_ocm, OPLUS_CHG_EVENT_CALL_ON);
		} else {
			if (is_wls_ocm_available(comm_dev))
				oplus_chg_anon_mod_event(comm_dev->wls_ocm, OPLUS_CHG_EVENT_CALL_OFF);
		}
		break;
	case OPLUS_CHG_PROP_CAMERA_ON:
		camera_on_count = pval->intval;
		if (camera_on_count > 0) {
			if (is_wls_ocm_available(comm_dev))
				oplus_chg_anon_mod_event(comm_dev->wls_ocm, OPLUS_CHG_EVENT_CAMERA_ON);
		} else {
			if (is_wls_ocm_available(comm_dev))
				oplus_chg_anon_mod_event(comm_dev->wls_ocm, OPLUS_CHG_EVENT_CAMERA_OFF);
		}
		break;
	default:
		pr_err("set prop %d is not supported\n", prop);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_chg_comm_prop_is_writeable(struct oplus_chg_mod *ocm, enum oplus_chg_mod_property prop)
{
	switch (prop) {
	case OPLUS_CHG_PROP_CHG_ENABLE:
	case OPLUS_CHG_PROP_SHIP_MODE:
	case OPLUS_CHG_PROP_CALL_ON:
	case OPLUS_CHG_PROP_CAMERA_ON:
	case OPLUS_CHG_EXTERN_PROP_CHARGE_PARAMETER:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct oplus_chg_mod_desc oplus_chg_comm_mod_desc = {
	.name = "common",
	.type = OPLUS_CHG_MOD_COMMON,
	.properties = oplus_chg_comm_props,
	.num_properties = ARRAY_SIZE(oplus_chg_comm_props),
	.uevent_properties = oplus_chg_comm_uevent_props,
	.uevent_num_properties = ARRAY_SIZE(oplus_chg_comm_uevent_props),
	.exten_properties = NULL,
	.num_exten_properties = 0,
	.get_property = oplus_chg_comm_get_prop,
	.set_property = oplus_chg_comm_set_prop,
	.property_is_writeable = oplus_chg_comm_prop_is_writeable,
};

static int oplus_chg_comm_event_notifier_call(struct notifier_block *nb, unsigned long val, void *v)
{
	struct oplus_chg_comm *comm_dev = container_of(nb, struct oplus_chg_comm, comm_event_nb);
	struct oplus_chg_mod *owner_ocm = v;

	switch (val) {
	case OPLUS_CHG_EVENT_ONLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n", val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "wireless")) {
			pr_info("wls online\n");
			comm_dev->wls_chging_keep = true;
			schedule_delayed_work(&comm_dev->heartbeat_work,
					      round_jiffies_relative(msecs_to_jiffies(HEARTBEAT_INTERVAL_MS)));
			schedule_delayed_work(&comm_dev->wls_chging_keep_clean_work, msecs_to_jiffies(2000));
		}
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous sending\n", val);
			return NOTIFY_BAD;
		}
		if (!strcmp(owner_ocm->desc->name, "wireless")) {
			pr_info("wls offline\n");
			cancel_delayed_work(&comm_dev->heartbeat_work);
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_comm_mod_notifier_call(struct notifier_block *nb, unsigned long val, void *v)
{
	return NOTIFY_OK;
}

static int oplus_chg_comm_changed_notifier_call(struct notifier_block *nb, unsigned long val, void *v)
{
	return NOTIFY_OK;
}

static int oplus_chg_comm_init_mod(struct oplus_chg_comm *comm_dev)
{
	struct oplus_chg_mod_config ocm_cfg = {};
	int rc;

	ocm_cfg.drv_data = comm_dev;
	ocm_cfg.of_node = comm_dev->dev->of_node;

	comm_dev->comm_ocm = oplus_chg_mod_register(comm_dev->dev, &oplus_chg_comm_mod_desc, &ocm_cfg);
	if (IS_ERR(comm_dev->comm_ocm)) {
		pr_err("Couldn't register comm ocm\n");
		rc = PTR_ERR(comm_dev->comm_ocm);
		return rc;
	}
	comm_dev->comm_ocm->notifier = &comm_ocm_notifier;
	comm_dev->comm_mod_nb.notifier_call = oplus_chg_comm_mod_notifier_call;
	rc = oplus_chg_reg_mod_notifier(comm_dev->comm_ocm, &comm_dev->comm_mod_nb);
	if (rc) {
		pr_err("register comm mod notifier error, rc=%d\n", rc);
		goto reg_comm_mod_notifier_err;
	}
	comm_dev->comm_event_nb.notifier_call = oplus_chg_comm_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&comm_dev->comm_event_nb);
	if (rc) {
		pr_err("register comm event notifier error, rc=%d\n", rc);
		goto reg_comm_event_notifier_err;
	}
	comm_dev->comm_changed_nb.notifier_call = oplus_chg_comm_changed_notifier_call;
	rc = oplus_chg_reg_changed_notifier(&comm_dev->comm_changed_nb);
	if (rc) {
		pr_err("register comm changed notifier error, rc=%d\n", rc);
		goto reg_comm_changed_notifier_err;
	}

	return 0;

	oplus_chg_unreg_changed_notifier(&comm_dev->comm_changed_nb);
reg_comm_changed_notifier_err:
	oplus_chg_unreg_event_notifier(&comm_dev->comm_event_nb);
reg_comm_event_notifier_err:
	oplus_chg_unreg_mod_notifier(comm_dev->comm_ocm, &comm_dev->comm_mod_nb);
reg_comm_mod_notifier_err:
	oplus_chg_mod_unregister(comm_dev->comm_ocm);
	return rc;
}

#ifdef CONFIG_OPLUS_CHG_OOS
static int oplus_chg_comm_lcd_active_call(struct notifier_block *nb, unsigned long val, void *v)
{
	struct oplus_chg_comm *comm_dev = container_of(nb, struct oplus_chg_comm, lcd_active_nb);
	struct drm_panel_notifier *evdata = v;
	unsigned int blank = *(unsigned int *)(evdata->data);

	if (val != DRM_PANEL_EARLY_EVENT_BLANK)
		return NOTIFY_BAD;

	switch (blank) {
	case DRM_PANEL_BLANK_POWERDOWN:
		cancel_delayed_work_sync(&comm_dev->led_power_on_report_work);
		if (is_usb_ocm_available(comm_dev))
			oplus_chg_anon_mod_event(comm_dev->usb_ocm, OPLUS_CHG_EVENT_LCD_OFF);
		if (is_wls_ocm_available(comm_dev))
			oplus_chg_anon_mod_event(comm_dev->wls_ocm, OPLUS_CHG_EVENT_LCD_OFF);
		break;
	case DRM_PANEL_BLANK_UNBLANK:
		cancel_delayed_work_sync(&comm_dev->led_power_on_report_work);
		/* Frequent turning on and off of the screen during the charging scene
		 * may result in slow charging. Adding a 1-minute delay here can effectively
		 * intercept short-lived screen-on events without affecting heat.
		 */
		schedule_delayed_work(&comm_dev->led_power_on_report_work, msecs_to_jiffies(60000));
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}
#endif

static int read_signed_data_from_node(struct device_node *node, const char *prop_str, s32 *addr, int len_max)
{
	int rc = 0, length;

	if (!node || !prop_str || !addr) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(s32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	if (length > len_max) {
		pr_err("too many entries(%d), only %d allowed\n", length, len_max);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str, (u32 *)addr, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return rc;
}

static int read_unsigned_data_from_node(struct device_node *node, const char *prop_str, u32 *addr, int len_max)
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
		pr_err("too many entries(%d), only %d allowed\n", length, len_max);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str, (u32 *)addr, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return rc;
}

static int oplus_chg_comm_parse_dt(struct oplus_chg_comm *comm_dev)
{
	struct device_node *node = comm_dev->dev->of_node;
	struct oplus_chg_comm_config *comm_cfg = &comm_dev->comm_cfg;
	int i, m;
#ifdef CONFIG_OPLUS_CHG_OOS
	struct device_node *node_temp;
	struct drm_panel *panel;
	int count;
#endif
	int rc;

	comm_cfg->check_batt_full_by_sw = (uint8_t)of_property_read_bool(node, "oplus,check-batt-full-by-sw");
	rc = of_property_read_u32(node, "oplus,fv-offset-voltage-mv", &comm_cfg->fv_offset_voltage_mv);
	if (rc < 0) {
		pr_err("get oplus,fv-offset-voltage-mv property error, rc=%d\n", rc);
		comm_cfg->fv_offset_voltage_mv = default_chg.fv_offset_voltage_mv;
	}
	of_property_read_u32(node, "oplus,little-cold-iterm-ma", &comm_cfg->little_cold_iterm_ma);
	rc = of_property_read_u32(node, "oplus,sw-iterm-ma", &comm_cfg->sw_iterm_ma);
	if (rc < 0) {
		pr_err("get oplus,sw-iterm-ma property error, rc=%d\n", rc);
		comm_cfg->sw_iterm_ma = default_chg.sw_iterm_ma;
	}
	rc = of_property_read_u32(node, "oplus,full-count-sw-num", &comm_cfg->full_count_sw_num);
	if (rc < 0) {
		pr_err("get oplus,full-count-sw-num property error, rc=%d\n", rc);
		comm_cfg->full_count_sw_num = default_chg.full_count_sw_num;
	}
	rc = of_property_read_u32(node, "oplus,batt-uv-mv", &comm_cfg->batt_uv_mv);
	if (rc < 0) {
		pr_err("get oplus,batt-uv-mv property error, rc=%d\n", rc);
		comm_cfg->batt_uv_mv = default_chg.batt_uv_mv;
	}
	rc = of_property_read_u32(node, "oplus,batt-ov-mv", &comm_cfg->batt_ov_mv);
	if (rc < 0) {
		pr_err("get oplus,batt-ov-mv property error, rc=%d\n", rc);
		comm_cfg->batt_ov_mv = default_chg.batt_ov_mv;
	}
	rc = of_property_read_u32(node, "oplus,batt-oc-ma", &comm_cfg->batt_oc_ma);
	if (rc < 0) {
		pr_err("get oplus,batt-oc-ma property error, rc=%d\n", rc);
		comm_cfg->batt_oc_ma = default_chg.batt_oc_ma;
	}
	rc = of_property_read_u32(node, "oplus,batt-ovd-mv", &comm_cfg->batt_ovd_mv);
	if (rc < 0) {
		pr_err("get oplus,batt-ovd-mv property error, rc=%d\n", rc);
		comm_cfg->batt_ovd_mv = default_chg.batt_ovd_mv;
	}

	rc = read_signed_data_from_node(node, "oplus,batt-them-thr", (s32 *)comm_cfg->batt_temp_thr,
					BATT_TEMP_INVALID - 1);
	if (rc < 0) {
		pr_err("get oplus,batt-them-th property error, rc=%d\n", rc);
		for (i = 0; i < BATT_TEMP_INVALID - 1; i++)
			comm_cfg->batt_temp_thr[i] = default_chg.batt_temp_thr[i];
	}
	pr_info("wkcs: temp thr = %d, %d, %d, %d, %d, %d, %d, %d\n", comm_cfg->batt_temp_thr[0],
		comm_cfg->batt_temp_thr[1], comm_cfg->batt_temp_thr[2], comm_cfg->batt_temp_thr[3],
		comm_cfg->batt_temp_thr[4], comm_cfg->batt_temp_thr[5], comm_cfg->batt_temp_thr[6],
		comm_cfg->batt_temp_thr[7]);
	rc = read_unsigned_data_from_node(node, "oplus,vbatmax-mv", (u32 *)comm_cfg->vbatmax_mv, BATT_TEMP_INVALID);
	if (rc < 0) {
		pr_err("get oplus,vbatmax-mv property error, rc=%d\n", rc);
		for (i = 0; i < BATT_TEMP_INVALID; i++)
			comm_cfg->vbatmax_mv[i] = default_chg.vbatmax_mv[i];
	}

	rc = read_signed_data_from_node(node, "oplus,ffc-temp-thr", (s32 *)comm_cfg->ffc_temp_thr,
					FFC_TEMP_INVALID - 1);
	if (rc < 0) {
		pr_err("get oplus,ffc-temp-thr property error, rc=%d\n", rc);
		for (i = 0; i < FFC_TEMP_INVALID - 1; i++)
			comm_cfg->ffc_temp_thr[i] = default_chg.ffc_temp_thr[i];
	}

	rc = of_property_read_u32(node, "oplus,usb-ffc-step-max", &comm_cfg->usb_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,usb-ffc-step-max property error, rc=%d\n", rc);
		comm_cfg->usb_ffc_step_max = default_chg.usb_ffc_step_max;
	}
	rc = read_unsigned_data_from_node(node, "oplus,usb-ffc-fv-mv", (u32 *)comm_cfg->usb_ffc_fv_mv,
					  comm_cfg->usb_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,usb-ffc-fv-mv property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->usb_ffc_step_max; i++)
			comm_cfg->usb_ffc_fv_mv[i] = default_chg.usb_ffc_fv_mv[i];
	}
	rc = read_unsigned_data_from_node(node, "oplus,usb-ffc-fv-cutoff-mv", (u32 *)comm_cfg->usb_ffc_fv_cutoff_mv,
					  comm_cfg->usb_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,usb-ffc-fv-cutoff-mv property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->usb_ffc_step_max; i++)
			comm_cfg->usb_ffc_fv_cutoff_mv[i] = default_chg.usb_ffc_fv_cutoff_mv[i];
	}
	rc = read_unsigned_data_from_node(node, "oplus,usb-ffc-fcc-ma", (u32 *)comm_cfg->usb_ffc_fcc_ma,
					  (FFC_TEMP_INVALID - 2) * comm_cfg->usb_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,usb-ffc-fcc-ma property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->usb_ffc_step_max; i++) {
			for (m = 0; m < (FFC_TEMP_INVALID - 2); m++)
				comm_cfg->usb_ffc_fcc_ma[i][m] = default_chg.usb_ffc_fcc_ma[i][m];
		}
	}
	rc = read_unsigned_data_from_node(node, "oplus,usb-ffc-fcc-cutoff-ma", (u32 *)comm_cfg->usb_ffc_fcc_cutoff_ma,
					  (FFC_TEMP_INVALID - 2) * comm_cfg->usb_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,usb-ffc-fcc-cutoff-ma property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->usb_ffc_step_max; i++) {
			for (m = 0; m < (FFC_TEMP_INVALID - 2); m++)
				comm_cfg->usb_ffc_fcc_cutoff_ma[i][m] = default_chg.usb_ffc_fcc_cutoff_ma[i][m];
		}
	}

	rc = of_property_read_u32(node, "oplus,wls-ffc-step-max", &comm_cfg->wls_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,wls-ffc-step-max property error, rc=%d\n", rc);
		comm_cfg->wls_ffc_step_max = default_chg.wls_ffc_step_max;
	}
	rc = read_unsigned_data_from_node(node, "oplus,wls-ffc-fv-mv", (u32 *)comm_cfg->wls_ffc_fv_mv,
					  comm_cfg->wls_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,wls-ffc-fv-mv property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->wls_ffc_step_max; i++)
			comm_cfg->wls_ffc_fv_mv[i] = default_chg.wls_ffc_fv_mv[i];
	}
	rc = read_unsigned_data_from_node(node, "oplus,wls-ffc-fv-cutoff-mv", (u32 *)comm_cfg->wls_ffc_fv_cutoff_mv,
					  comm_cfg->wls_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,wls-ffc-fv-cutoff-mv property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->wls_ffc_step_max; i++)
			comm_cfg->wls_ffc_fv_cutoff_mv[i] = default_chg.wls_ffc_fv_cutoff_mv[i];
	}
	rc = read_unsigned_data_from_node(node, "oplus,wls-ffc-icl-ma", (u32 *)comm_cfg->wls_ffc_icl_ma,
					  (FFC_TEMP_INVALID - 2) * comm_cfg->wls_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,wls-ffc-icl-ma property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->wls_ffc_step_max; i++) {
			for (m = 0; m < (FFC_TEMP_INVALID - 2); m++)
				comm_cfg->wls_ffc_icl_ma[i][m] = default_chg.wls_ffc_icl_ma[i][m];
		}
	}
	rc = read_unsigned_data_from_node(node, "oplus,wls-ffc-fcc-ma", (u32 *)comm_cfg->wls_ffc_fcc_ma,
					  (FFC_TEMP_INVALID - 2) * comm_cfg->wls_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,wls-ffc-fcc-ma property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->wls_ffc_step_max; i++) {
			for (m = 0; m < (FFC_TEMP_INVALID - 2); m++)
				comm_cfg->wls_ffc_fcc_ma[i][m] = default_chg.wls_ffc_fcc_ma[i][m];
		}
	}
	rc = read_unsigned_data_from_node(node, "oplus,wls-ffc-fcc-cutoff-ma", (u32 *)comm_cfg->wls_ffc_fcc_cutoff_ma,
					  (FFC_TEMP_INVALID - 2) * comm_cfg->wls_ffc_step_max);
	if (rc < 0) {
		pr_err("get oplus,wls-ffc-fcc-cutoff-ma property error, rc=%d\n", rc);
		for (i = 0; i < comm_cfg->wls_ffc_step_max; i++) {
			for (m = 0; m < (FFC_TEMP_INVALID - 2); m++)
				comm_cfg->wls_ffc_fcc_cutoff_ma[i][m] = default_chg.wls_ffc_fcc_cutoff_ma[i][m];
		}
	}
	rc = read_unsigned_data_from_node(node, "oplus,wls-vbatdet-mv", (u32 *)comm_cfg->wls_vbatdet_mv,
					  BATT_TEMP_INVALID);
	if (rc < 0) {
		pr_err("get oplus,wls-vbatdet-mv property error, rc=%d\n", rc);
		for (i = 0; i < BATT_TEMP_INVALID; i++)
			comm_cfg->wls_vbatdet_mv[i] = default_chg.wls_vbatdet_mv[i];
	}
	rc = of_property_read_u32(node, "oplus,batt-curr-limit-thr-mv", &comm_cfg->batt_curr_limit_thr_mv);
	if (rc < 0) {
		pr_err("get oplus,batt-curr-limit-thr-mv property error, rc=%d\n", rc);
		comm_cfg->batt_curr_limit_thr_mv = default_chg.batt_curr_limit_thr_mv;
	}

#ifdef CONFIG_OPLUS_CHG_OOS
	count = of_count_phandle_with_args(node, "panel", NULL);
	if (count <= 0) {
		pr_err("read panel error, rc=%d\n", count);
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		node_temp = of_parse_phandle(node, "panel", i);
		panel = of_drm_find_panel(node_temp);
		of_node_put(node_temp);
		if (!IS_ERR(panel)) {
			comm_dev->lcd_panel = panel;
			break;
		}
	}
	if (comm_dev->lcd_panel == NULL) {
		pr_err("active lcd panel not found\n");
		return -EINVAL;
	}
#endif

	comm_cfg->bpp_vchg_max_mv = default_chg.bpp_vchg_max_mv;
	comm_cfg->bpp_vchg_min_mv = default_chg.bpp_vchg_min_mv;
	comm_cfg->epp_vchg_max_mv = default_chg.epp_vchg_max_mv;
	comm_cfg->epp_vchg_min_mv = default_chg.epp_vchg_min_mv;
	comm_cfg->fast_vchg_max_mv = default_chg.fast_vchg_max_mv;
	comm_cfg->fast_vchg_min_mv = default_chg.fast_vchg_min_mv;

	return 0;
}

void oplus_chg_comm_status_init(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	struct oplus_chg_comm_config *comm_cfg = &comm_dev->comm_cfg;
	int i;

	comm_dev->chg_ovp = false;
	comm_dev->recharge_pending = false;
	comm_dev->recharge_status = false;
	comm_dev->time_out = false;
	comm_dev->vbus_uovp = false;
	comm_dev->is_aging_test = false;
	comm_dev->ffc_charging = false;
	comm_dev->is_power_changed = true;
	comm_dev->batt_vol_high = false;
	comm_dev->batt_ovp = false;
	comm_dev->vchg_max_mv = 9900;
	comm_dev->vchg_min_mv = 4500;
	comm_dev->timeout_count = 0;
	comm_dev->ffc_step = 0;
	comm_dev->temp_region = BATT_TEMP_INVALID;
	comm_dev->ffc_temp_region = FFC_TEMP_INVALID;
	oplus_chg_set_charge_done(comm_dev, false);
	oplus_chg_clean_notify_code(comm_dev);

	for (i = 0; i < BATT_TEMP_INVALID - 1; i++)
		comm_dev->batt_temp_dynamic_thr[i] = comm_cfg->batt_temp_thr[i];
	for (i = 0; i < FFC_TEMP_INVALID - 1; i++)
		comm_dev->ffc_temp_dynamic_thr[i] = comm_cfg->ffc_temp_thr[i];

	comm_dev->initialized = true;
}

enum oplus_chg_temp_region oplus_chg_comm_get_temp_region(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev = NULL;
	int temp;
	enum oplus_chg_temp_region temp_region = BATT_TEMP_INVALID;
	int i;

	if (!comm_ocm || !comm_ocm->initialized)
		return BATT_TEMP_NORMAL;

	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	if (!comm_dev || !comm_dev->initialized)
		return BATT_TEMP_NORMAL;

	temp = oplus_chg_get_batt_temp(comm_dev);
	for (i = 0; i < BATT_TEMP_INVALID - 1; i++) {
		if (temp < comm_dev->batt_temp_dynamic_thr[i]) {
			temp_region = i;
			break;
		}
	}
	if (temp_region == BATT_TEMP_INVALID)
		temp_region = BATT_TEMP_HOT;

	return temp_region;
}

static int oplus_chg_comm_check_batt_temp(struct oplus_chg_comm *comm_dev)
{
	enum oplus_chg_temp_region temp_region = BATT_TEMP_INVALID;
	struct oplus_chg_comm_config *comm_cfg = &comm_dev->comm_cfg;
	int batt_vol_mv;
	int i, rc = 0;

	/* todo:nick.hu
	if (!comm_dev->usb_online && !comm_dev->wls_online) {
		pr_err("usb & wls not online\n");
		return -EINVAL;
	}
	*/

	if (comm_dev->ffc_status != FFC_DEFAULT)
		return 0;

	/*if (is_wls_fastchg_started(comm_dev))
		return 0;*/

	temp_region = oplus_chg_comm_get_temp_region(comm_dev->comm_ocm);
	if (comm_dev->temp_region == BATT_TEMP_INVALID)
		comm_dev->temp_region = temp_region;
	if (comm_dev->temp_region > temp_region) {
		for (i = 0; i < BATT_TEMP_INVALID - 1; i++) {
			if (i == temp_region)
				comm_dev->batt_temp_dynamic_thr[i] =
					comm_dev->batt_temp_dynamic_thr[i] + BATT_TEMP_HYST;
			else
				comm_dev->batt_temp_dynamic_thr[i] = comm_cfg->batt_temp_thr[i];
		}
		if (is_wls_fastchg_started(comm_dev)) {
			if (comm_dev->temp_region == BATT_TEMP_LITTLE_COOL && temp_region == BATT_TEMP_COOL) {
				comm_dev->is_power_changed = true;
			}
		} else {
			comm_dev->is_power_changed = true;
		}
		comm_dev->temp_region = temp_region;
	} else if (comm_dev->temp_region < temp_region) {
		for (i = 0; i < BATT_TEMP_INVALID - 1; i++) {
			if (i == (temp_region - 1))
				comm_dev->batt_temp_dynamic_thr[i] =
					comm_dev->batt_temp_dynamic_thr[i] - BATT_TEMP_HYST;
			else
				comm_dev->batt_temp_dynamic_thr[i] = comm_cfg->batt_temp_thr[i];
		}
		if (is_wls_fastchg_started(comm_dev)) {
			if (comm_dev->temp_region == BATT_TEMP_COOL && temp_region == BATT_TEMP_LITTLE_COOL) {
				comm_dev->is_power_changed = true;
			}
		} else {
			comm_dev->is_power_changed = true;
		}
		comm_dev->temp_region = temp_region;
	}

	rc = oplus_chg_get_vbat(comm_dev, &batt_vol_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return false;
	}

	if ((batt_vol_mv > comm_cfg->batt_curr_limit_thr_mv) && !comm_dev->batt_vol_high) {
		comm_dev->batt_vol_high = true;
		comm_dev->is_power_changed = true;
	} else if ((batt_vol_mv < (comm_cfg->batt_curr_limit_thr_mv - BATT_TEMP_HYST)) && comm_dev->batt_vol_high) {
		comm_dev->batt_vol_high = false;
		comm_dev->is_power_changed = true;
	}

	if (comm_dev->is_power_changed) {
		comm_dev->is_power_changed = false;
		pr_info("charge status changed, temp region is %d\n", comm_dev->temp_region);
		if (is_wls_fv_votable_available(comm_dev))
			oplus_vote(comm_dev->wls_fv_votable, USER_VOTER, true, comm_cfg->vbatmax_mv[temp_region],
				   false);
		if (is_wls_ocm_available(comm_dev))
			oplus_chg_anon_mod_event(comm_dev->wls_ocm, OPLUS_CHG_EVENT_POWER_CHANGED);
	}

	return rc;
}

enum oplus_chg_ffc_temp_region oplus_chg_comm_get_ffc_temp_region(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	int temp;
	enum oplus_chg_ffc_temp_region temp_region = FFC_TEMP_INVALID;
	struct oplus_chg_comm_config *comm_cfg = &comm_dev->comm_cfg;
	int i;

	temp = oplus_chg_get_batt_temp(comm_dev);
	for (i = 0; i < FFC_TEMP_INVALID - 1; i++) {
		if (temp < comm_dev->ffc_temp_dynamic_thr[i]) {
			temp_region = i;
			break;
		}
	}
	if (temp_region == FFC_TEMP_INVALID)
		temp_region = FFC_TEMP_WARM;

	if (comm_dev->ffc_temp_region > temp_region) {
		for (i = 0; i < FFC_TEMP_INVALID - 1; i++) {
			if (i == temp_region)
				comm_dev->ffc_temp_dynamic_thr[i] = comm_dev->ffc_temp_dynamic_thr[i] + BATT_TEMP_HYST;
			else
				comm_dev->ffc_temp_dynamic_thr[i] = comm_cfg->ffc_temp_thr[i];
		}
		comm_dev->ffc_temp_region = temp_region;
	} else if (comm_dev->ffc_temp_region < temp_region) {
		for (i = 0; i < BATT_TEMP_INVALID - 1; i++) {
			if (i == (temp_region - 1))
				comm_dev->ffc_temp_dynamic_thr[i] = comm_dev->ffc_temp_dynamic_thr[i] - BATT_TEMP_HYST;
			else
				comm_dev->ffc_temp_dynamic_thr[i] = comm_cfg->ffc_temp_thr[i];
		}
		comm_dev->ffc_temp_region = temp_region;
	}

	return temp_region;
}

int oplus_chg_comm_switch_ffc(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;
	struct oplus_chg_comm_config *comm_cfg;
	enum oplus_chg_ffc_temp_region ffc_temp_region;
	int rc;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return -ENODEV;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	comm_cfg = &comm_dev->comm_cfg;
	ffc_temp_region = oplus_chg_comm_get_ffc_temp_region(comm_ocm);

	comm_dev->ffc_step = 0;
	if (ffc_temp_region == FFC_TEMP_PRE_NORMAL || ffc_temp_region == FFC_TEMP_NORMAL) {
		if (is_usb_charger_online(comm_dev)) {
			pr_err("not support usb ffc\n");
			rc = -EINVAL;
			goto err;
		} else if (is_wls_charger_online(comm_dev)) {
			if (!is_wls_icl_votable_available(comm_dev)) {
				pr_err("wls_icl_votable not found\n");
				rc = -ENODEV;
				goto err;
			}
			if (!is_wls_fcc_votable_available(comm_dev)) {
				pr_err("wls_fcc_votable not found\n");
				rc = -ENODEV;
				goto err;
			}
			if (!is_wls_fv_votable_available(comm_dev)) {
				pr_err("wls_fv_votable not found\n");
				rc = -ENODEV;
				goto err;
			}
			if (!is_wls_nor_out_dis_votable_available(comm_dev)) {
				pr_err("wls_nor_out_dis_votable not found\n");
				rc = -ENODEV;
				goto err;
			}
			oplus_vote(comm_dev->wls_icl_votable, USER_VOTER, true,
				   comm_cfg->wls_ffc_icl_ma[comm_dev->ffc_step][ffc_temp_region - 1], true);
			oplus_vote(comm_dev->wls_fv_votable, USER_VOTER, true,
				   comm_cfg->wls_ffc_fv_mv[comm_dev->ffc_step], false);
			oplus_vote(comm_dev->wls_fcc_votable, USER_VOTER, true,
				   comm_cfg->wls_ffc_fcc_ma[comm_dev->ffc_step][ffc_temp_region - 1], false);
		} else {
			pr_err("Unknown charger type\n");
			rc = -EINVAL;
			goto err;
		}
		comm_dev->ffc_fcc_count = 0;
		comm_dev->ffc_fv_count = 0;
		comm_dev->ffc_charging = true;
		comm_dev->ffc_status = FFC_FAST;
		return 0;
	} else {
		pr_err("FFC charging is not possible in this temp region, temp_region=%d\n", ffc_temp_region);
		rc = -EINVAL;
		goto err;
	}

err:
	comm_dev->ffc_fcc_count = 0;
	comm_dev->ffc_fv_count = 0;
	comm_dev->ffc_charging = false;
	comm_dev->ffc_status = FFC_DEFAULT;
	return rc;
}

int oplus_chg_comm_check_ffc(struct oplus_chg_mod *comm_ocm)
{
	int ibat_ma, vbat_mv, temp;
	struct oplus_chg_comm *comm_dev;
	struct oplus_chg_comm_config *comm_cfg;
	enum oplus_chg_ffc_temp_region ffc_temp_region;
	static enum oplus_chg_ffc_temp_region pre_ffc_temp_region = FFC_TEMP_NORMAL;
	enum oplus_chg_temp_region temp_region;
	bool usb_online, wls_online;
	int fv_max_mv, cutoff_ma, step_max;
	int rc;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return -ENODEV;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	comm_cfg = &comm_dev->comm_cfg;

	if (comm_dev->ffc_status == FFC_DEFAULT) {
		comm_dev->ffc_fcc_count = 0;
		comm_dev->ffc_fv_count = 0;
		comm_dev->ffc_charging = false;
		comm_dev->ffc_step = 0;
		return -EINVAL;
	}

	rc = oplus_chg_get_vbat(comm_dev, &vbat_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return rc;
	}
	rc = oplus_chg_get_ibat(comm_dev, &ibat_ma);
	if (rc < 0) {
		pr_err("can't get batt curr, rc=%d\n", rc);
		return rc;
	}
	temp = oplus_chg_get_batt_temp(comm_dev);
	ffc_temp_region = oplus_chg_comm_get_ffc_temp_region(comm_ocm);
	usb_online = is_usb_charger_online(comm_dev);
	wls_online = is_wls_charger_online(comm_dev);

	switch (comm_dev->ffc_status) {
	case FFC_FAST:
		if (pre_ffc_temp_region != ffc_temp_region) {
			pre_ffc_temp_region = ffc_temp_region;
			if (ffc_temp_region == FFC_TEMP_PRE_NORMAL || ffc_temp_region == FFC_TEMP_NORMAL) {
				if (usb_online) {
					pr_err("not support usb ffc\n");
					rc = -EINVAL;
					goto err;
				} else if (wls_online) {
					if (!is_wls_icl_votable_available(comm_dev)) {
						pr_err("wls_icl_votable not found\n");
						rc = -ENODEV;
						goto err;
					}
					if (!is_wls_fcc_votable_available(comm_dev)) {
						pr_err("wls_fcc_votable not found\n");
						rc = -ENODEV;
						goto err;
					}
					oplus_vote(comm_dev->wls_icl_votable, USER_VOTER, true,
						   comm_cfg->wls_ffc_icl_ma[comm_dev->ffc_step][ffc_temp_region - 1],
						   true);
					oplus_vote(comm_dev->wls_fcc_votable, USER_VOTER, true,
						   comm_cfg->wls_ffc_fcc_ma[comm_dev->ffc_step][ffc_temp_region - 1],
						   false);
				} else {
					pr_err("Unknown charger type\n");
					rc = -EINVAL;
					goto err;
				}
			} else {
				pr_err("FFC charging is not possible in this temp region, temp_region=%d\n",
				       ffc_temp_region);
				goto err;
			}
		}
		if (usb_online) {
			pr_err("not support usb ffc\n");
			rc = -EINVAL;
			goto err;
		} else if (wls_online) {
			fv_max_mv = comm_cfg->wls_ffc_fv_cutoff_mv[comm_dev->ffc_step];
			cutoff_ma = comm_cfg->wls_ffc_fcc_cutoff_ma[comm_dev->ffc_step][ffc_temp_region - 1];
			step_max = comm_cfg->wls_ffc_step_max;
		} else {
			pr_err("Unknown charger type\n");
			rc = -EINVAL;
			goto err;
		}

		if (vbat_mv >= fv_max_mv)
			comm_dev->ffc_fv_count++;
		if (comm_dev->ffc_fv_count > 5) {
			comm_dev->ffc_step++;
			comm_dev->ffc_fv_count = 0;
			pr_info("vbat_mv(=%d) > fv_max_mv(=%d), switch to next step(=%d)\n", vbat_mv, fv_max_mv,
				comm_dev->ffc_step);
		}
		if (abs(ibat_ma) < cutoff_ma)
			comm_dev->ffc_fcc_count++;
		else
			comm_dev->ffc_fcc_count = 0;
		if (comm_dev->ffc_fcc_count > 3) {
			comm_dev->ffc_step++;
			comm_dev->ffc_fv_count = 0;
			pr_info("ibat_ma(=%d) > cutoff_ma(=%d), switch to next step(=%d)\n", ibat_ma, cutoff_ma,
				comm_dev->ffc_step);
		}
		if (comm_dev->ffc_step >= step_max) {
			pr_info("ffc charge done\n");
			if (usb_online) {
				pr_err("not support usb ffc\n");
				rc = -EINVAL;
				goto err;
			} else if (wls_online) {
				if (!is_wls_nor_out_dis_votable_available(comm_dev)) {
					pr_err("wls_nor_out_dis_votable not found\n");
					rc = -ENODEV;
					goto err;
				}
				oplus_vote(comm_dev->wls_nor_out_dis_votable, USER_VOTER, true, 1, false);
			}
			comm_dev->ffc_fcc_count = 0;
			comm_dev->ffc_fv_count = 0;
			comm_dev->ffc_status = FFC_IDLE;
			temp_region = oplus_chg_comm_get_temp_region(comm_ocm);
			oplus_vote(comm_dev->wls_fv_votable, USER_VOTER, true, comm_cfg->vbatmax_mv[temp_region],
				   false);
		} else {
			if (usb_online) {
				pr_err("not support usb ffc\n");
				rc = -EINVAL;
				goto err;
			} else if (wls_online) {
				if (!is_wls_icl_votable_available(comm_dev)) {
					pr_err("wls_icl_votable not found\n");
					rc = -ENODEV;
					goto err;
				}
				if (!is_wls_fcc_votable_available(comm_dev)) {
					pr_err("wls_fcc_votable not found\n");
					rc = -ENODEV;
					goto err;
				}
				if (!is_wls_fv_votable_available(comm_dev)) {
					pr_err("wls_fv_votable not found\n");
					rc = -ENODEV;
					goto err;
				}
				oplus_vote(comm_dev->wls_icl_votable, USER_VOTER, true,
					   comm_cfg->wls_ffc_icl_ma[comm_dev->ffc_step][ffc_temp_region - 1], true);
				oplus_vote(comm_dev->wls_fv_votable, USER_VOTER, true,
					   comm_cfg->wls_ffc_fv_mv[comm_dev->ffc_step], false);
				oplus_vote(comm_dev->wls_fcc_votable, USER_VOTER, true,
					   comm_cfg->wls_ffc_fcc_ma[comm_dev->ffc_step][ffc_temp_region - 1], false);
			}
		}
		break;
	case FFC_IDLE:
		comm_dev->ffc_fcc_count++;
		if (comm_dev->ffc_fcc_count > 6) {
			comm_dev->ffc_charging = false;
			comm_dev->ffc_fcc_count = 0;
			comm_dev->ffc_fv_count = 0;
			if (usb_online) {
				pr_err("not support usb ffc\n");
				rc = -EINVAL;
				goto err;
			} else if (wls_online) {
				if (!is_wls_nor_out_dis_votable_available(comm_dev)) {
					pr_err("wls_nor_out_dis_votable not found\n");
					rc = -ENODEV;
					goto err;
				}
				oplus_vote(comm_dev->wls_nor_out_dis_votable, USER_VOTER, false, 0, false);
			}
			comm_dev->ffc_status = FFC_DEFAULT;
			return 1;
		}
		break;
	default:
		rc = -EINVAL;
		goto err;
	}

	return 0;

err:
	comm_dev->ffc_charging = false;
	comm_dev->ffc_fcc_count = 0;
	comm_dev->ffc_fv_count = 0;
	comm_dev->ffc_status = FFC_DEFAULT;
	return rc;
}

#define FULL_COUNTS_SW 5
#define FULL_COUNTS_HW 3
bool oplus_chg_comm_check_batt_full_by_sw(struct oplus_chg_mod *comm_ocm)
{
	static bool ret_sw;
	static bool ret_hw;
	static int vbat_counts_sw;
	static int vbat_counts_hw;
	int vbatt_full_vol_sw;
	int vbatt_full_vol_hw;
	int term_current;
	int icharging, batt_volt;
	enum oplus_chg_temp_region temp_region;
	struct oplus_chg_comm *comm_dev;
	struct oplus_chg_comm_config *comm_cfg;
	int rc;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return false;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	comm_cfg = &comm_dev->comm_cfg;

	if (!comm_cfg->check_batt_full_by_sw)
		return false;
	if (comm_dev->ffc_status != FFC_DEFAULT)
		return false;
	if (!is_usb_charger_online(comm_dev) && !is_wls_charger_online(comm_dev)) {
		vbat_counts_sw = 0;
		vbat_counts_hw = 0;
		ret_sw = false;
		ret_hw = false;
		return false;
	}

	temp_region = oplus_chg_comm_get_temp_region(comm_ocm);
	vbatt_full_vol_hw = comm_cfg->vbatmax_mv[temp_region];
	if (temp_region == BATT_TEMP_COLD)
		vbatt_full_vol_sw = comm_cfg->vbatmax_mv[temp_region] - comm_cfg->fv_offset_voltage_mv;
	else if (temp_region == BATT_TEMP_LITTLE_COLD)
		vbatt_full_vol_sw = comm_cfg->vbatmax_mv[temp_region] - comm_cfg->fv_offset_voltage_mv;
	else if (temp_region == BATT_TEMP_COOL)
		vbatt_full_vol_sw = comm_cfg->vbatmax_mv[temp_region] - comm_cfg->fv_offset_voltage_mv;
	else if (temp_region == BATT_TEMP_LITTLE_COOL)
		vbatt_full_vol_sw = comm_cfg->vbatmax_mv[temp_region] - comm_cfg->fv_offset_voltage_mv;
	else if (temp_region == BATT_TEMP_NORMAL)
		vbatt_full_vol_sw = comm_cfg->vbatmax_mv[temp_region] - comm_cfg->fv_offset_voltage_mv;
	else if (temp_region == BATT_TEMP_LITTLE_WARM)
		vbatt_full_vol_sw = comm_cfg->vbatmax_mv[temp_region] - comm_cfg->fv_offset_voltage_mv;
	else if (temp_region == BATT_TEMP_WARM)
		vbatt_full_vol_sw = comm_cfg->vbatmax_mv[temp_region] - comm_cfg->fv_offset_voltage_mv;
	else {
		vbat_counts_sw = 0;
		vbat_counts_hw = 0;
		ret_sw = 0;
		ret_hw = 0;
		return false;
	}
	if (comm_cfg->little_cold_iterm_ma > 0 && (temp_region == BATT_TEMP_LITTLE_COLD))
		term_current = comm_cfg->little_cold_iterm_ma;
	else
		term_current = comm_cfg->sw_iterm_ma;

	rc = oplus_chg_get_vbat(comm_dev, &batt_volt);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return false;
	}
	rc = oplus_chg_get_ibat(comm_dev, &icharging);
	if (rc < 0) {
		pr_err("can't get batt curr, rc=%d\n", rc);
		return false;
	}

	/* use SW Vfloat to check */
	if (batt_volt > vbatt_full_vol_sw) {
		if (icharging < 0 && abs(icharging) <= term_current) {
			vbat_counts_sw++;
			if (vbat_counts_sw > FULL_COUNTS_SW * comm_cfg->full_count_sw_num) {
				vbat_counts_sw = 0;
				ret_sw = true;
			}
		} else if (icharging >= 0) {
			vbat_counts_sw++;
			if (vbat_counts_sw > FULL_COUNTS_SW * 2) {
				vbat_counts_sw = 0;
				ret_sw = true;
				pr_info("[BATTERY] Battery full by sw when icharging>=0!!\n");
			}
		} else {
			vbat_counts_sw = 0;
			ret_sw = false;
		}
	} else {
		vbat_counts_sw = 0;
		ret_sw = false;
	}

	/* use HW Vfloat to check */
	if (batt_volt >= vbatt_full_vol_hw + 18) {
		vbat_counts_hw++;
		if (vbat_counts_hw >= FULL_COUNTS_HW) {
			vbat_counts_hw = 0;
			ret_hw = true;
		}
	} else {
		vbat_counts_hw = 0;
		ret_hw = false;
	}

	if (ret_sw == true || ret_hw == true) {
		pr_info("[BATTERY] Battery full by sw[%s] !!\n", (ret_sw == true) ? "S" : "H");
		ret_sw = ret_hw = false;
		return true;
	} else {
		return false;
	}
}

void oplus_chg_comm_check_term_current(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;
	int soc = 0, real_soc = 0, vbat_mv = 0, ibat_ma = 0, temp = 0;
	bool chg_done;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);

	if (comm_dev->chg_done && !comm_dev->recharge_pending)
		return;
	chg_done = oplus_chg_comm_check_batt_full_by_sw(comm_ocm);
	if (chg_done) {
		oplus_chg_set_charge_done(comm_dev, chg_done);
		comm_dev->recharge_pending = false;
		if (is_usb_charger_online(comm_dev)) {
			pr_err("not support usb ffc\n");
			return;
		} else if (is_wls_charger_online(comm_dev)) {
			if (!is_wls_nor_out_dis_votable_available(comm_dev)) {
				pr_err("wls_nor_out_dis_votable not found\n");
				return;
			}
			oplus_vote(comm_dev->wls_nor_out_dis_votable, USER_VOTER, true, 1, false);
		}
		(void)oplus_chg_get_batt_soc(comm_dev, &soc);
		(void)oplus_chg_get_batt_real_soc(comm_dev, &real_soc);
		(void)oplus_chg_get_vbat(comm_dev, &vbat_mv);
		(void)oplus_chg_get_ibat(comm_dev, &ibat_ma);
		temp = oplus_chg_get_batt_temp(comm_dev);
		pr_info("chg_done:SOC=%d, REAL_SOC=%d, VBAT=%d,IBAT=%d, BAT_TEMP=%d\n", soc, real_soc, vbat_mv, ibat_ma,
			temp);
	}
}

enum oplus_chg_ffc_status oplus_chg_comm_get_ffc_status(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return FFC_DEFAULT;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	return comm_dev->ffc_status;
}

bool oplus_chg_comm_get_chg_done(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return FFC_DEFAULT;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	return comm_dev->chg_done;
}

void oplus_chg_comm_update_config(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);

	comm_dev->is_power_changed = true;
	(void)oplus_chg_comm_check_batt_temp(comm_dev);
}

int oplus_chg_comm_get_batt_health(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;
	enum oplus_chg_temp_region temp_region;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	temp_region = oplus_chg_comm_get_temp_region(comm_ocm);

	if (comm_dev->batt_ovp) {
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if (!is_battery_present(comm_dev)) {
		return POWER_SUPPLY_HEALTH_DEAD;
	} else if (temp_region == BATT_TEMP_HOT) {
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if (temp_region == BATT_TEMP_COLD) {
		return POWER_SUPPLY_HEALTH_COLD;
	}

	return POWER_SUPPLY_HEALTH_GOOD;
}
int oplus_chg_comm_get_batt_status(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;
	union oplus_chg_mod_propval val = {
		0,
	};
	bool wls_enable = false;
	bool wls_fastchg = false;
	int icl_ma = 0, fcc_ma = 0;
	int rc;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);

	if (!is_wls_ocm_available(comm_dev)) {
		pr_err("wls mod not found\n");
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}

	if (!is_wls_charger_present(comm_dev)) {
#ifdef CONFIG_OPLUS_CHG_OOS
		return POWER_SUPPLY_STATUS_DISCHARGING;
#else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
#endif
	}

	if (!is_wls_charger_online(comm_dev) || comm_dev->wls_chging_keep) {
		return POWER_SUPPLY_STATUS_CHARGING;
	}

	rc = oplus_chg_mod_get_property(comm_dev->wls_ocm, OPLUS_CHG_PROP_FASTCHG_STATUS, &val);
	if (rc == 0)
		wls_fastchg = !!val.intval;
	if (is_wls_nor_out_dis_votable_available(comm_dev))
		wls_enable = !oplus_get_effective_result(comm_dev->wls_nor_out_dis_votable);
	if (is_wls_icl_votable_available(comm_dev))
		icl_ma = oplus_get_effective_result(comm_dev->wls_icl_votable);
	if (is_wls_fcc_votable_available(comm_dev))
		fcc_ma = oplus_get_effective_result(comm_dev->wls_fcc_votable);
	wls_enable &= ((!!icl_ma && !!fcc_ma) || wls_fastchg);

	if (comm_dev->chg_done || oplus_chg_get_batt_status(comm_dev) == POWER_SUPPLY_STATUS_FULL) {
		return POWER_SUPPLY_STATUS_FULL;
	} else if (!wls_enable) {
#ifdef CONFIG_OPLUS_CHG_OOS
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
#else
		return POWER_SUPPLY_STATUS_DISCHARGING;
#endif
	}

	return POWER_SUPPLY_STATUS_CHARGING;
}

/* Check whether the battery voltage exceeds the current limit threshold */
bool oplus_chg_comm_batt_vol_over_cl_thr(struct oplus_chg_mod *comm_ocm)
{
	struct oplus_chg_comm *comm_dev;
	int batt_vol_mv;
	struct oplus_chg_comm_config *comm_cfg;
	int rc;

	if (comm_ocm == NULL) {
		pr_err("comm mod is null\n");
		return true;
	}
	comm_dev = oplus_chg_mod_get_drvdata(comm_ocm);
	comm_cfg = &comm_dev->comm_cfg;

	rc = oplus_chg_get_vbat(comm_dev, &batt_vol_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return true;
	}

	if (batt_vol_mv > comm_cfg->batt_curr_limit_thr_mv)
		return true;
	else if (batt_vol_mv < (comm_cfg->batt_curr_limit_thr_mv - BATT_TEMP_HYST))
		return false;
	else
		return comm_dev->batt_vol_high;
}

static void oplus_chg_check_charge_timeout(struct oplus_chg_comm *comm_dev)
{
	int batt_status;
	bool usb_online, wls_online;

	if (comm_dev->chg_done)
		return;
#ifdef CONFIG_OPLUS_CHG_OOS
	if (is_aging_test != AGING_TEST_STATUS_DEFAULT)
		return;
#endif

	wls_online = is_wls_charger_present(comm_dev);
	usb_online = is_usb_charger_online(comm_dev);
	batt_status = oplus_chg_get_batt_status(comm_dev);
	if ((usb_online || wls_online) && batt_status == POWER_SUPPLY_STATUS_CHARGING)
		comm_dev->timeout_count++;
	else
		comm_dev->timeout_count = 0;

	if (comm_dev->timeout_count > CHG_TIMEOUT_COUNT) {
		pr_err("chg timeout! stop chaging now\n");
		oplus_chg_set_notify_code(comm_dev, 1 << NOTIFY_CHGING_OVERTIME, true);
		if (usb_online) {
			// TODO: Add usb charging processing logic
		} else if (wls_online) {
			if (is_wls_nor_out_dis_votable_available(comm_dev))
				oplus_vote(comm_dev->wls_nor_out_dis_votable, TIMEOUT_VOTER, true, 1, false);
		} else {
			pr_err("unknown charge type\n");
		}
		comm_dev->time_out = true;
	}
}

static void oplus_chg_check_charger_uovp(struct oplus_chg_comm *comm_dev, int vchg_mv)
{
	static int over_volt_count, not_over_volt_count;
	static bool pre_uovp;
	int detect_time = 3; /* 3 x 5s = 15s */
	struct oplus_chg_comm_config *comm_cfg = &comm_dev->comm_cfg;
	enum oplus_chg_wls_type wls_type;

	wls_type = oplus_chg_get_wls_charge_type(comm_dev);
	switch (wls_type) {
	case OPLUS_CHG_WLS_UNKNOWN:
	case OPLUS_CHG_WLS_BPP:
		comm_dev->vchg_max_mv = comm_cfg->bpp_vchg_max_mv;
		comm_dev->vchg_min_mv = comm_cfg->bpp_vchg_min_mv;
		break;
	case OPLUS_CHG_WLS_EPP:
	case OPLUS_CHG_WLS_EPP_PLUS:
	case OPLUS_CHG_WLS_VOOC:
		comm_dev->vchg_max_mv = comm_cfg->epp_vchg_max_mv;
		comm_dev->vchg_min_mv = comm_cfg->epp_vchg_min_mv;
		break;
	case OPLUS_CHG_WLS_SVOOC:
	case OPLUS_CHG_WLS_PD_65W:
		comm_dev->vchg_max_mv = comm_cfg->fast_vchg_max_mv;
		comm_dev->vchg_min_mv = comm_cfg->fast_vchg_min_mv;
		break;
	case OPLUS_CHG_WLS_TRX:
		return;
	}

	pre_uovp = comm_dev->vbus_uovp;
	if (!comm_dev->chg_ovp) {
		if (vchg_mv > comm_dev->vchg_max_mv || vchg_mv <= comm_dev->vchg_min_mv) {
			pr_err("charger is over voltage, count=%d\n", over_volt_count);
			comm_dev->vbus_uovp = true;
			if (pre_uovp)
				over_volt_count++;
			else
				over_volt_count = 0;

			pr_err("uovp=%d, pre_uovp=%d, over_volt_count=%d\n", comm_dev->vbus_uovp, pre_uovp,
			       over_volt_count);
			pr_info("vchg_max=%d, vchg_min=%d\n", comm_dev->vchg_max_mv, comm_dev->vchg_min_mv);
			if (detect_time <= over_volt_count) {
				/* vchg continuous higher than 5.8v */
				pr_err("charger is over voltage, stop charging\n");
				oplus_chg_set_notify_code(comm_dev, 1 << NOTIFY_CHARGER_OVER_VOL, true);
				if (is_wls_rx_dis_votable_available(comm_dev)) {
					oplus_vote(comm_dev->wls_rx_dis_votable, UOVP_VOTER, true, 1, false);
					msleep(1000);
					oplus_vote(comm_dev->wls_rx_dis_votable, UOVP_VOTER, false, 0, false);
				}
				comm_dev->chg_ovp = true;
			}
		}
	} else {
		if (vchg_mv < comm_dev->vchg_max_mv - 100 && vchg_mv > comm_dev->vchg_min_mv + 100) {
			comm_dev->vbus_uovp = false;
			if (!pre_uovp)
				not_over_volt_count++;
			else
				not_over_volt_count = 0;

			pr_err("uovp_satus=%d, pre_uovp=%d,not_over_volt_count=%d\n", comm_dev->vbus_uovp, pre_uovp,
			       not_over_volt_count);
			if (detect_time <= not_over_volt_count) {
				/* vchg continuous lower than 5.7v */
				pr_err("charger voltage is back to normal\n");
				oplus_chg_set_notify_code(comm_dev, 1 << NOTIFY_CHARGER_OVER_VOL, false);
				if (is_wls_rx_dis_votable_available(comm_dev)) {
					oplus_vote(comm_dev->wls_rx_dis_votable, UOVP_VOTER, false, 0, false);
				}
				comm_dev->chg_ovp = false;
				(void)oplus_chg_comm_check_batt_temp(comm_dev);
			}
		}
	}
}

static void oplus_chg_check_battery_uovp(struct oplus_chg_comm *comm_dev)
{
	int vbat_mv = 0;
	bool usb_online, wls_online;
	int rc;

	wls_online = is_wls_charger_present(comm_dev);
	usb_online = is_usb_charger_online(comm_dev);

	if (!usb_online && !wls_online)
		return;

	rc = oplus_chg_get_vbat(comm_dev, &vbat_mv);
	if (rc < 0) {
		pr_err("can't get batt vol, rc=%d\n", rc);
		return;
	}
	pr_debug("batt vol:%d\n", vbat_mv);
	if (vbat_mv > BATT_SOFT_OVP_MV) {
		if (!comm_dev->batt_ovp) {
			pr_err("vbat is over voltage, stop charging\n");
			comm_dev->batt_ovp = true;
			oplus_chg_set_notify_code(comm_dev, 1 << NOTIFY_BAT_OVER_VOL, true);
			if (is_wls_nor_out_dis_votable_available(comm_dev))
				oplus_vote(comm_dev->wls_nor_out_dis_votable, UOVP_VOTER, true, 1, false);
		}
	} else {
		if (comm_dev->batt_ovp && (vbat_mv < BATT_SOFT_OVP_MV - 50)) {
			pr_err("battery_restore_from_uovp\n");
			comm_dev->batt_ovp = false;
			oplus_chg_set_notify_code(comm_dev, 1 << NOTIFY_BAT_OVER_VOL, true);
			if (is_wls_nor_out_dis_votable_available(comm_dev))
				oplus_vote(comm_dev->wls_nor_out_dis_votable, UOVP_VOTER, false, 0, false);
		}
	}
}

static void oplus_chg_wls_chging_keep_clean_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_comm *comm_dev = container_of(dwork, struct oplus_chg_comm, wls_chging_keep_clean_work);
	comm_dev->wls_chging_keep = false;
}

#ifdef CONFIG_OPLUS_CHG_OOS
static void oplus_chg_led_power_on_report_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_comm *comm_dev = container_of(dwork, struct oplus_chg_comm, led_power_on_report_work);
	if (is_usb_ocm_available(comm_dev))
		oplus_chg_anon_mod_event(comm_dev->usb_ocm, OPLUS_CHG_EVENT_LCD_ON);
	if (is_wls_ocm_available(comm_dev))
		oplus_chg_anon_mod_event(comm_dev->wls_ocm, OPLUS_CHG_EVENT_LCD_ON);
}
#endif

static void oplus_chg_heartbeat_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_comm *comm_dev = container_of(dwork, struct oplus_chg_comm, heartbeat_work);
	struct oplus_chg_comm_config *comm_cfg = &comm_dev->comm_cfg;
	bool charger_online;
	int vchg_mv = 0, vbat_mv = 0;
	int vbatdet_mv;
	enum oplus_chg_temp_region temp_region;

	charger_online = is_wls_charger_present(comm_dev);
	if (!charger_online) {
		pr_err("no charger online\n");
		return;
	}

	if (is_wls_fastchg_started(comm_dev))
		goto out;

	oplus_chg_check_charge_timeout(comm_dev);

	(void)oplus_chg_wls_charger_vol(comm_dev, &vchg_mv);
	pr_info("charger vol=%d\n", vchg_mv);
	oplus_chg_check_charger_uovp(comm_dev, vchg_mv);
	oplus_chg_check_battery_uovp(comm_dev);

	//oplus_chg_comm_check_ffc(chg);
	oplus_chg_comm_check_term_current(comm_dev->comm_ocm);
	temp_region = oplus_chg_comm_get_temp_region(comm_dev->comm_ocm);
	(void)oplus_chg_get_vbat(comm_dev, &vbat_mv);

	vbatdet_mv = comm_cfg->wls_vbatdet_mv[temp_region];
	if (temp_region <= BATT_TEMP_UNKNOWN || temp_region >= BATT_TEMP_HOT)
		vbatdet_mv = 0;
	if (!comm_dev->chg_ovp && comm_dev->chg_done && !comm_dev->recharge_pending &&
	    temp_region > BATT_TEMP_UNKNOWN && temp_region < BATT_TEMP_HOT && vbatdet_mv >= vbat_mv) {
		/*oplus_chg_set_charge_done(comm_dev, false);*/
		comm_dev->recharge_pending = true;
		comm_dev->recharge_status = true;
		if (is_wls_nor_out_dis_votable_available(comm_dev))
			oplus_vote(comm_dev->wls_nor_out_dis_votable, USER_VOTER, false, 0, false);
		pr_debug("temp_region=%d, recharge_pending\n", temp_region);
	}

out:
	if (!comm_dev->chg_ovp && !comm_dev->time_out && comm_dev->battery_status == BATT_STATUS_GOOD) {
		(void)oplus_chg_comm_check_batt_temp(comm_dev);
	}
	/*out:*/
	/*update time 5s*/
	schedule_delayed_work(&comm_dev->heartbeat_work,
			      round_jiffies_relative(msecs_to_jiffies(HEARTBEAT_INTERVAL_MS)));
}

static int oplus_chg_comm_get_iio_channel(struct oplus_chg_comm *comm_dev, const char *propname,
					  struct iio_channel **chan)
{
	struct device_node *node = comm_dev->dev->of_node;
	int rc;

	rc = of_property_match_string(node, "io-channel-names", propname);
	if (rc < 0) {
		pr_err("can't read io-channel-names, rc=%d\n", rc);
		return rc;
	}

	*chan = iio_channel_get(comm_dev->dev, propname);
	if (IS_ERR(*chan)) {
		rc = PTR_ERR(*chan);
		if (rc != -EPROBE_DEFER)
			pr_err("%s channel unavailable, %d\n", propname, rc);
		*chan = NULL;
	}

	return rc;
}

static int oplus_chg_comm_init_adc_channels(struct oplus_chg_comm *comm_dev)
{
	int rc;
	const char *name;
	struct device_node *node = comm_dev->dev->of_node;

	rc = of_property_read_string(node, "oplus,skin_temp_chan", &name);
	if (rc < 0) {
		pr_err("can't get oplus,skin_temp_chan, rc=%d\n", rc);
		return rc;
	}

	rc = oplus_chg_comm_get_iio_channel(comm_dev, name, &comm_dev->skin_therm_chan);
	if (rc < 0)
		return rc;

	return 0;
}

int oplus_chg_comm_reg_mutual_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&comm_mutual_notifier, nb);
}

int oplus_chg_comm_unreg_mutual_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&comm_mutual_notifier, nb);
}

static void oplus_chg_comm_mutual_event(char *buf)
{
	atomic_notifier_call_chain(&comm_mutual_notifier, 1, buf);
}

static int oplus_chg_comm_init_mutual(struct oplus_chg_comm *comm_dev)
{
	int ret = 0;
	struct oplus_chg_comm *chip = comm_dev;

	chip->hidl_handle_cmd_ready = false;
	chip->cmd_data_ok = false;
	mutex_init(&chip->read_lock);
	mutex_init(&chip->cmd_data_lock);
	mutex_init(&chip->cmd_ack_lock);
	init_waitqueue_head(&chip->read_wq);
	init_completion(&chip->cmd_ack);

	return ret;
}

ssize_t oplus_chg_comm_send_mutual_cmd(struct oplus_chg_mod *comm_ocm, char *buf)
{
	int rc = 0;
	struct oplus_chg_cmd cmd;
	struct oplus_chg_cmd *pcmd;
	struct oplus_chg_comm *chip;

	if (!comm_ocm)
		return -EINVAL;

	chip = oplus_chg_mod_get_drvdata(comm_ocm);
	if (!chip)
		return -EINVAL;

	chip->hidl_handle_cmd_ready = true;

	mutex_lock(&chip->read_lock);
	rc = wait_event_interruptible(chip->read_wq, chip->cmd_data_ok);
	mutex_unlock(&chip->read_lock);
	if (rc)
		return rc;
	if (!chip->cmd_data_ok)
		pr_err("oplus chg false wakeup, rc=%d\n", rc);
	pr_info("send\n");
	mutex_lock(&chip->cmd_data_lock);
	chip->cmd_data_ok = false;
	memcpy(&cmd, &chip->cmd, sizeof(struct oplus_chg_cmd));
	mutex_unlock(&chip->cmd_data_lock);
	memcpy(buf, &cmd, sizeof(struct oplus_chg_cmd));
	pcmd = (struct oplus_chg_cmd *)buf;
	pr_info("success to copy to user space cmd:%d, size:%d\n", pcmd->cmd, pcmd->data_size);

	return sizeof(struct oplus_chg_cmd);
}

ssize_t oplus_chg_comm_response_mutual_cmd(struct oplus_chg_mod *comm_ocm, const char *buf, size_t count)
{
	ssize_t ret = 0;
	struct oplus_chg_comm *chip;
	struct oplus_chg_cmd *p_cmd;

	if (!comm_ocm)
		return -EINVAL;

	chip = oplus_chg_mod_get_drvdata(comm_ocm);
	if (!chip)
		return -EINVAL;

	p_cmd = (struct oplus_chg_cmd *)buf;
	if (count != sizeof(struct oplus_chg_cmd)) {
		pr_err("!!!size of buf is not matched\n");
		return -EINVAL;
	}
	pr_info("!!!cmd[%d]\n", p_cmd->cmd);

	oplus_chg_comm_mutual_event((void *)buf);
	complete(&chip->cmd_ack);

	return ret;
}

int oplus_chg_common_set_mutual_cmd(struct oplus_chg_mod *comm_ocm, u32 cmd, u32 data_size, const void *data_buf)
{
	int rc;
	struct oplus_chg_comm *chip;

	if (!comm_ocm)
		return CMD_ERROR_CHIP_NULL;

	chip = oplus_chg_mod_get_drvdata(comm_ocm);
	if (!chip)
		return CMD_ERROR_CHIP_NULL;

	if (!data_buf)
		return CMD_ERROR_DATA_NULL;

	if (data_size > CHG_CMD_DATA_LEN) {
		pr_err("cmd data size is invalid\n");
		return CMD_ERROR_DATA_INVALID;
	}

	if (!chip->hidl_handle_cmd_ready) {
		pr_err("hidl not read\n");
		return CMD_ERROR_HIDL_NOT_READY;
	}

	pr_info("start\n");
	mutex_lock(&chip->cmd_ack_lock);
	mutex_lock(&chip->cmd_data_lock);
	memset(&chip->cmd, 0, sizeof(struct oplus_chg_cmd));
	chip->cmd.cmd = cmd;
	chip->cmd.data_size = data_size;
	memcpy(chip->cmd.data_buf, data_buf, data_size);
	chip->cmd_data_ok = true;
	mutex_unlock(&chip->cmd_data_lock);
	wake_up(&chip->read_wq);

	reinit_completion(&chip->cmd_ack);
	rc = wait_for_completion_timeout(&chip->cmd_ack, msecs_to_jiffies(CHG_CMD_TIME_MS));
	if (!rc) {
		pr_err("Error, timed out sending message\n");
		mutex_unlock(&chip->cmd_ack_lock);
		return CMD_ERROR_TIME_OUT;
	}
	rc = CMD_ACK_OK;
	pr_info("success\n");
	mutex_unlock(&chip->cmd_ack_lock);

	return rc;
}

static int oplus_chg_comm_driver_probe(struct platform_device *pdev)
{
	struct oplus_chg_comm *comm_dev;
	int rc;

	comm_dev = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_comm), GFP_KERNEL);
	if (comm_dev == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}

	comm_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, comm_dev);

	rc = oplus_chg_comm_parse_dt(comm_dev);
	if (rc < 0) {
		pr_err("oplus chg comm parse dts error, rc=%d\n", rc);
		goto parse_dt_err;
	}

	rc = oplus_chg_comm_init_adc_channels(comm_dev);
	if (rc < 0) {
		pr_err("init adc channels error, rc=%d\n", rc);
	}

	rc = oplus_chg_comm_init_mod(comm_dev);
	if (rc < 0) {
		pr_err("oplus chg comm mod init error, rc=%d\n", rc);
		goto comm_mod_init_err;
	}

	oplus_chg_comm_init_mutual(comm_dev);

#ifdef CONFIG_OPLUS_CHG_OOS
	if (comm_dev->lcd_panel) {
		comm_dev->lcd_active_nb.notifier_call = oplus_chg_comm_lcd_active_call;
		rc = drm_panel_notifier_register(comm_dev->lcd_panel, &comm_dev->lcd_active_nb);
		if (rc < 0) {
			pr_err("Unable to register lcd notifier, rc=%d\n", rc);
			goto lcd_notif_reg_err;
		}
	}
#endif

	INIT_DELAYED_WORK(&comm_dev->heartbeat_work, oplus_chg_heartbeat_work);
	INIT_DELAYED_WORK(&comm_dev->wls_chging_keep_clean_work, oplus_chg_wls_chging_keep_clean_work);

	pr_info("probe done\n");

	return 0;

#ifdef CONFIG_OPLUS_CHG_OOS
lcd_notif_reg_err:
	oplus_chg_unreg_changed_notifier(&comm_dev->comm_changed_nb);
	oplus_chg_unreg_event_notifier(&comm_dev->comm_event_nb);
	oplus_chg_unreg_mod_notifier(comm_dev->comm_ocm, &comm_dev->comm_mod_nb);
	oplus_chg_mod_unregister(comm_dev->comm_ocm);
#endif
comm_mod_init_err:
parse_dt_err:
	devm_kfree(&pdev->dev, comm_dev);
	return rc;
}

static int oplus_chg_comm_driver_remove(struct platform_device *pdev)
{
	struct oplus_chg_comm *comm_dev = platform_get_drvdata(pdev);

#ifdef CONFIG_OPLUS_CHG_OOS
	drm_panel_notifier_unregister(comm_dev->lcd_panel, &comm_dev->lcd_active_nb);
#endif
	oplus_chg_unreg_changed_notifier(&comm_dev->comm_changed_nb);
	oplus_chg_unreg_event_notifier(&comm_dev->comm_event_nb);
	oplus_chg_unreg_mod_notifier(comm_dev->comm_ocm, &comm_dev->comm_mod_nb);
	oplus_chg_mod_unregister(comm_dev->comm_ocm);
	devm_kfree(&pdev->dev, comm_dev);
	return 0;
}

static const struct of_device_id oplus_chg_comm_match[] = {
	{ .compatible = "oplus,common-charge" },
	{},
};

static struct platform_driver oplus_chg_comm_driver = {
	.driver = {
		.name = "oplus_chg_comm",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_comm_match),
	},
	.probe = oplus_chg_comm_driver_probe,
	.remove = oplus_chg_comm_driver_remove,
};

static __init int oplus_chg_comm_driver_init(void)
{
	return platform_driver_register(&oplus_chg_comm_driver);
}

static __exit void oplus_chg_comm_driver_exit(void)
{
	platform_driver_unregister(&oplus_chg_comm_driver);
}

oplus_chg_module_register(oplus_chg_comm_driver);
