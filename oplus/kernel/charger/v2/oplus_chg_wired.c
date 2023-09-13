#define pr_fmt(fmt) "[CHG_WIRED]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/power_supply.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>

#include <oplus_chg.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_ic.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>
#include <oplus_mms_gauge.h>
#include <oplus_strategy.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_wired.h>

#define PDQC_CONFIG_WAIT_TIME_MS	15000
#define WIRED_COOL_DOWN_LEVEL_MAX	8
#define FACTORY_MODE_PDQC_9V_THR	4100
#define PDQC_BUCK_DEF_CURR_MA		500

struct oplus_wired_spec_config {
	int32_t pd_iclmax_ma;
	int32_t qc_iclmax_ma;
	int32_t non_standard_ibatmax_ma;
	int32_t input_power_mw[OPLUS_WIRED_CHG_MODE_MAX];
	int32_t led_on_fcc_max_ma[TEMP_REGION_MAX];
	int32_t fcc_ma[2][OPLUS_WIRED_CHG_MODE_MAX][TEMP_REGION_MAX];
	int32_t vbatt_pdqc_to_5v_thr;
	int32_t vbatt_pdqc_to_9v_thr;
	int32_t cool_down_pdqc_vol_mv[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_pdqc_curr_ma[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_vooc_curr_ma[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_normal_curr_ma[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_pdqc_level_max;
	int32_t cool_down_vooc_level_max;
	int32_t cool_down_normal_level_max;
	int32_t vbus_uv_thr_mv[OPLUS_VBUS_MAX];
	int32_t vbus_ov_thr_mv[OPLUS_VBUS_MAX];
} __attribute__((packed));

struct oplus_wired_config {
	uint8_t *vooc_strategy_name;
	uint8_t *vooc_strategy_data;
	uint32_t vooc_strategy_data_size;
} __attribute__((packed));

struct oplus_chg_wired {
	struct device *dev;
	struct oplus_mms *wired_topic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *vooc_topic;
	struct mms_subscribe *gauge_subs;
	struct mms_subscribe *wired_subs;
	struct mms_subscribe *comm_subs;
	struct mms_subscribe *vooc_subs;

	struct oplus_wired_spec_config spec;
	struct oplus_wired_config config;

	struct oplus_chg_strategy *vooc_strategy;

	struct work_struct gauge_update_work;
	struct work_struct plugin_work;
	struct work_struct chg_type_change_work;
	struct work_struct temp_region_update_work;
	struct work_struct qc_config_work;
	struct work_struct charger_current_changed_work;
	struct work_struct led_on_changed_work;
	struct work_struct icl_changed_work;
	struct work_struct fcc_changed_work;
	struct delayed_work pd_config_work;

	struct power_supply *usb_psy;
	struct power_supply *batt_psy;

	struct votable *fcc_votable;
	struct votable *icl_votable;
	struct votable *input_suspend_votable;
	struct votable *output_suspend_votable;
	struct votable *pd_svooc_votable;
	struct votable *pd_boost_disable_votable;
	struct votable *vooc_chg_auto_mode_votable;

	struct completion qc_action_ack;
	struct completion pd_action_ack;

	bool unwakelock_chg;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock suspend_lock;
#else
	struct wakeup_source *suspend_ws;
#endif

	bool chg_online;
	bool vooc_support;
	bool adjust_pdqc_vol_thr_support;
	bool authenticate;
	bool hmac;
	bool vooc_started;
	bool pd_boost_disable;

	int chg_type;
	int vbus_set_mv;
	int vbus_mv;
	int vbat_mv;
	enum oplus_temp_region temp_region;
	enum oplus_wired_charge_mode chg_mode;
	enum comm_topic_item fcc_gear;
	enum oplus_wired_action qc_action;
	enum oplus_wired_action pd_action;
	int cool_down;
	int pd_retry_count;
	unsigned int err_code;
	struct mutex icl_lock;
	struct mutex current_lock;
};

/* default parameters used when dts is not configured */
static struct oplus_wired_spec_config default_config = {
	.pd_iclmax_ma = 3000,
	.qc_iclmax_ma = 2000,
	.input_power_mw = {
		2500, 2500, 7500, 10000, 18000, 18000, 18000
	},
	.led_on_fcc_max_ma = { 0, 540, 2000, 2500, 2500, 2500, 500, 0 },
	.fcc_ma = {
		{
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_UNKNOWN */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_SDP */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_CDP */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_DCP */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_VOOC */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_QC */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_PD */
		},
		{
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_UNKNOWN */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_SDP */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_CDP */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_DCP */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_VOOC */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_QC */
			{ 0, 0, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_PD */
		}
	}
};

static const char *const oplus_wired_chg_type_text[] = {
	[OPLUS_CHG_USB_TYPE_UNKNOWN] = "unknown",
	[OPLUS_CHG_USB_TYPE_SDP] = "sdp",
	[OPLUS_CHG_USB_TYPE_DCP] = "dcp",
	[OPLUS_CHG_USB_TYPE_CDP] = "cdp",
	[OPLUS_CHG_USB_TYPE_ACA] = "aca",
	[OPLUS_CHG_USB_TYPE_C] = "type-c",
	[OPLUS_CHG_USB_TYPE_PD] = "pd",
	[OPLUS_CHG_USB_TYPE_PD_DRP] = "pd_drp",
	[OPLUS_CHG_USB_TYPE_PD_PPS] = "pps",
	[OPLUS_CHG_USB_TYPE_PD_SDP] = "pd_sdp",
	[OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID] = "ocp",
	[OPLUS_CHG_USB_TYPE_QC2] = "qc2",
	[OPLUS_CHG_USB_TYPE_QC3] = "qc3",
	[OPLUS_CHG_USB_TYPE_VOOC] = "vooc",
	[OPLUS_CHG_USB_TYPE_SVOOC] = "svooc",
	[OPLUS_CHG_USB_TYPE_UFCS] = "ufcs",
	[OPLUS_CHG_USB_TYPE_MAX] = "invalid",
};

static const char *const oplus_wired_chg_mode_text[] = {
	[OPLUS_WIRED_CHG_MODE_UNKNOWN] = "unknown",
	[OPLUS_WIRED_CHG_MODE_SDP] = "sdp",
	[OPLUS_WIRED_CHG_MODE_CDP] = "cdp",
	[OPLUS_WIRED_CHG_MODE_DCP] = "dcp",
	[OPLUS_WIRED_CHG_MODE_VOOC] = "vooc",
	[OPLUS_WIRED_CHG_MODE_QC] = "qc",
	[OPLUS_WIRED_CHG_MODE_PD] = "pd",
	[OPLUS_WIRED_CHG_MODE_MAX] = "invalid",
};

__maybe_unused static bool is_usb_psy_available(struct oplus_chg_wired *chip)
{
	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	return !!chip->usb_psy;
}

__maybe_unused static bool is_batt_psy_available(struct oplus_chg_wired *chip)
{
	if (!chip->batt_psy)
		chip->batt_psy = power_supply_get_by_name("battery");
	return !!chip->batt_psy;
}

__maybe_unused static bool
is_pd_svooc_votable_available(struct oplus_chg_wired *chip)
{
	if (!chip->pd_svooc_votable)
		chip->pd_svooc_votable = find_votable("PD_SVOOC");
	return !!chip->pd_svooc_votable;
}

__maybe_unused static bool
is_vooc_chg_auto_mode_votable_available(struct oplus_chg_wired *chip)
{
	if (!chip->vooc_chg_auto_mode_votable)
		chip->vooc_chg_auto_mode_votable =
			find_votable("VOOC_CHG_AUTO_MODE");
	return !!chip->vooc_chg_auto_mode_votable;
}

const char *oplus_wired_get_chg_type_str(enum oplus_chg_usb_type type)
{
	return oplus_wired_chg_type_text[type];
}

const char *
oplus_wired_get_chg_mode_region_str(enum oplus_wired_charge_mode mode)
{
	return oplus_wired_chg_mode_text[mode];
}

static void oplus_wired_awake_init(struct oplus_chg_wired *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND,
		       "wired wakelock");
#else
	chip->suspend_ws = wakeup_source_register(NULL, "wired wakelock");
#endif
}

static void oplus_wired_awake_exit(struct oplus_chg_wired *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_destroy(&chip->suspend_lock);
#else
	wakeup_source_unregister(chip->suspend_ws);
#endif
}

static void oplus_wired_set_awake(struct oplus_chg_wired *chip, bool awake)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if (chip->unwakelock_chg && awake) {
		chg_err("unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (awake) {
		wake_lock(&chip->suspend_lock);
	} else {
		wake_unlock(&chip->suspend_lock);
	}
#else
	static bool pm_flag = false;

	if (chip->unwakelock_chg && awake) {
		chg_err("unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (!chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
	}
#endif
}

static int oplus_wired_set_err_code(struct oplus_chg_wired *chip,
				    unsigned int err_code)
{
	struct mms_msg *msg;
	int rc;

	if (chip->err_code == err_code)
		return 0;

	chip->err_code = err_code;
	pr_info("set err_code=%08x\n", err_code);

	if (err_code & (BIT(OPLUS_ERR_CODE_OVP) | BIT(OPLUS_ERR_CODE_UVP)))
		vote(chip->input_suspend_votable, UOVP_VOTER, true, 1, false);
	else
		vote(chip->input_suspend_votable, UOVP_VOTER, false, 0, false);

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				      WIRED_ITEM_ERR_CODE, err_code);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish error code msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

#define VBUS_CHECK_COUNT 2
#define VBUS_OV_OFFSET 500
#define VBUS_UV_OFFSET 300
static void oplus_wired_vbus_check(struct oplus_chg_wired *chip)
{
	struct oplus_wired_spec_config *spec = &chip->spec;
	static int ov_count, uv_count;
	enum oplus_wired_vbus_vol vbus_type;
	int uv_mv, ov_mv;
	unsigned int err_code = 0;

	if (chip->vooc_started)
		goto done;

	if (chip->vbus_set_mv == 9000)
		vbus_type = OPLUS_VBUS_9V;
	else
		vbus_type = OPLUS_VBUS_5V;

	if (chip->err_code & BIT(OPLUS_ERR_CODE_OVP))
		ov_mv = spec->vbus_ov_thr_mv[vbus_type] - VBUS_OV_OFFSET;
	else
		ov_mv = spec->vbus_ov_thr_mv[vbus_type];
	if (chip->err_code & BIT(OPLUS_ERR_CODE_UVP))
		uv_mv = spec->vbus_uv_thr_mv[vbus_type] + VBUS_UV_OFFSET;
	else
		uv_mv = spec->vbus_uv_thr_mv[vbus_type];

	if (chip->vbus_mv > ov_mv) {
		uv_count = 0;
		if (ov_count > VBUS_CHECK_COUNT) {
			err_code |= BIT(OPLUS_ERR_CODE_OVP);
		} else {
			ov_count++;
		}
	} else if (chip->vbus_mv < uv_mv) {
		ov_count = 0;
		if (uv_count > VBUS_CHECK_COUNT) {
			err_code |= BIT(OPLUS_ERR_CODE_UVP);
		} else {
			uv_count++;
		}
	} else {
		uv_count = 0;
		ov_count = 0;
	}

done:
	oplus_wired_set_err_code(chip, err_code);
}

static int oplus_wired_current_set(struct oplus_chg_wired *chip,
				   bool vbus_changed)
{
	struct oplus_wired_spec_config *spec = &chip->spec;
	int icl_ma, icl_tmp_ma;
	int fcc_ma;
	int cool_down, cool_down_curr;
	bool led_on = false;
	bool icl_changed;
	union mms_msg_data data = { 0 };
	int rc;

	if (!chip->chg_online)
		return 0;

	/* Make sure you get the correct charger type */
	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CHG_TYPE, &data,
				false);
	chip->chg_type = data.intval;

	switch (chip->chg_type) {
	case OPLUS_CHG_USB_TYPE_DCP:
	case OPLUS_CHG_USB_TYPE_ACA:
	case OPLUS_CHG_USB_TYPE_C:
	case OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_DCP;
		break;
	case OPLUS_CHG_USB_TYPE_QC2:
	case OPLUS_CHG_USB_TYPE_QC3:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_QC;
		break;
	case OPLUS_CHG_USB_TYPE_CDP:
	case OPLUS_CHG_USB_TYPE_PD_SDP:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_CDP;
		break;
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_DRP:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_PD;
		break;
	case OPLUS_CHG_USB_TYPE_VOOC:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_VOOC;
		break;
	case OPLUS_CHG_USB_TYPE_SVOOC:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_DCP;
		break;
	case OPLUS_CHG_USB_TYPE_SDP:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_SDP;
		break;
	default:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_UNKNOWN;
		break;
	}

	cool_down = chip->cool_down;
	switch (chip->chg_mode) {
	case OPLUS_WIRED_CHG_MODE_QC:
	case OPLUS_WIRED_CHG_MODE_PD:
		if (cool_down > spec->cool_down_pdqc_level_max)
			cool_down = spec->cool_down_pdqc_level_max;
		if (cool_down > 0)
			cool_down_curr =
				spec->cool_down_pdqc_curr_ma[cool_down - 1];
		else
			cool_down_curr = 0;
		break;
	case OPLUS_WIRED_CHG_MODE_VOOC:
		if (cool_down > spec->cool_down_vooc_level_max)
			cool_down = spec->cool_down_vooc_level_max;
		if (cool_down > 0)
			cool_down_curr =
				spec->cool_down_vooc_curr_ma[cool_down - 1];
		else
			cool_down_curr = 0;
		break;
	default:
		if (cool_down > spec->cool_down_normal_level_max)
			cool_down = spec->cool_down_normal_level_max;
		if (cool_down > 0)
			cool_down_curr =
				spec->cool_down_normal_curr_ma[cool_down - 1];
		else
			cool_down_curr = 0;
		break;
	}

	icl_ma =
		spec->input_power_mw[chip->chg_mode] * 1000 / chip->vbus_set_mv;
	switch (chip->chg_mode) {
	case OPLUS_WIRED_CHG_MODE_QC:
		icl_ma = min(icl_ma, spec->qc_iclmax_ma);
		break;
	case OPLUS_WIRED_CHG_MODE_PD:
		icl_ma = min(icl_ma, spec->pd_iclmax_ma);
		break;
	default:
		break;
	}
	fcc_ma =
		spec->fcc_ma[chip->fcc_gear][chip->chg_mode][chip->temp_region];
	chg_info(
		"chg_type=%s, chg_mode=%s, spec_icl=%d, spec_fcc=%d, cool_down_icl=%d\n",
		oplus_wired_get_chg_type_str(chip->chg_type),
		oplus_wired_get_chg_mode_region_str(chip->chg_mode), icl_ma,
		fcc_ma, cool_down_curr);

	mutex_lock(&chip->current_lock);
	icl_tmp_ma = get_effective_result(chip->icl_votable);
	vote(chip->fcc_votable, SPEC_VOTER, true, fcc_ma, false);
	vote(chip->icl_votable, SPEC_VOTER, true, icl_ma, true);
	if (!chip->authenticate || !chip->hmac) {
		vote(chip->fcc_votable, NON_STANDARD_VOTER, true,
		     spec->non_standard_ibatmax_ma, false);
		chg_err("!authenticate or !hmac, set nonstandard current\n");
	} else {
		vote(chip->fcc_votable, NON_STANDARD_VOTER, false, 0, false);
	}

	/* cool down */
	if (chip->comm_topic) {
		rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_LED_ON,
					     &data, false);
		if (!rc)
			led_on = !!data.intval;
	}
	if (led_on && cool_down_curr > 0)
		vote(chip->icl_votable, COOL_DOWN_VOTER, true, cool_down_curr,
		     true);
	else
		vote(chip->icl_votable, COOL_DOWN_VOTER, false, 0, true);

	if (led_on) {
		if (chip->chg_type == OPLUS_CHG_USB_TYPE_VOOC)
			vote(chip->fcc_votable, LED_ON_VOTER, false, 0, false);
		else
			vote(chip->fcc_votable, LED_ON_VOTER, true,
			     spec->led_on_fcc_max_ma[chip->temp_region], false);
	} else {
		vote(chip->fcc_votable, LED_ON_VOTER, false, 0, false);
	}
	icl_changed = (icl_tmp_ma != get_effective_result(chip->icl_votable));
	chg_info("vbus_changed=%s, icl_changed=%s\n",
		 true_or_false_str(vbus_changed),
		 true_or_false_str(icl_changed));
	/* If ICL has changed, no need to reset the current */
	if (vbus_changed && !icl_changed) {
		chg_info("vbus changed, need rerun icl vote\n");
		rerun_election(chip->icl_votable, true);
	}
	mutex_unlock(&chip->current_lock);

	return 0;
}

static void oplus_wired_variables_init(struct oplus_chg_wired *chip)
{
	chip->chg_online = false;

	chip->chg_type = OPLUS_CHG_USB_TYPE_UNKNOWN;
	chip->vbus_set_mv = 5000;
	chip->temp_region = TEMP_REGION_HOT;
	chip->chg_mode = OPLUS_WIRED_CHG_MODE_UNKNOWN;
	chip->qc_action = OPLUS_ACTION_NULL;
	chip->pd_action = OPLUS_ACTION_NULL;
	chip->pd_retry_count = 0;
	mutex_init(&chip->icl_lock);
	mutex_init(&chip->current_lock);
}

static int oplus_wired_get_vbatt_pdqc_to_9v_thr(struct oplus_chg_wired *chip)
{
	struct device_node *node = chip->dev->of_node;
	int thr;
	int rc;

	if (node != NULL) {
		rc = of_property_read_u32(
			node, "oplus_spec,vbatt_pdqc_to_9v_thr", &thr);
		if (rc < 0) {
			chg_err("oplus_spec,vbatt_pdqc_to_9v_thr reading failed, rc=%d\n",
				rc);
			thr = default_config.vbatt_pdqc_to_9v_thr;
		}
	} else {
		thr = default_config.vbatt_pdqc_to_9v_thr;
	}

	return thr;
}

static void oplus_wired_qc_config_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, qc_config_work);
	struct oplus_wired_spec_config *spec = &chip->spec;
	int cool_down, cool_down_vol;
	int vbus_set_mv = 5000; /* vbus default setting voltage is 5V */
	bool vbus_changed = false;
	int rc;

	if (chip->chg_mode != OPLUS_WIRED_CHG_MODE_QC)
		goto set_curr;

	if (chip->cool_down > 0) {
		cool_down = chip->cool_down > spec->cool_down_pdqc_level_max ?
				    spec->cool_down_pdqc_level_max :
				    chip->cool_down;
		cool_down_vol = spec->cool_down_pdqc_vol_mv[cool_down - 1];
	} else {
		cool_down_vol = 0;
	}

	switch (chip->qc_action) {
	case OPLUS_ACTION_BOOST:
		if (cool_down_vol > 0 && cool_down_vol < 9000) {
			chg_info("cool down limit, qc cannot be boosted\n");
			chip->qc_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		if (spec->vbatt_pdqc_to_9v_thr > 0 &&
		    chip->vbat_mv < spec->vbatt_pdqc_to_9v_thr) {
			chg_info("qc starts to boost\n");
			mutex_lock(&chip->icl_lock);
			rc = oplus_wired_set_qc_config(OPLUS_CHG_QC_2_0, 9000);
			mutex_unlock(&chip->icl_lock);
			if (rc < 0) {
				chip->qc_action = OPLUS_ACTION_NULL;
				goto set_curr;
			}
		} else {
			chg_info(
				"battery voltage too high, qc cannot be boosted\n");
			chip->qc_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = 9000;
		break;
	case OPLUS_ACTION_BUCK:
		chg_info("qc starts to buck\n");
		/* Set the current to 500ma before stepping down */
		vote(chip->icl_votable, SPEC_VOTER, true, PDQC_BUCK_DEF_CURR_MA,
		     true);
		mutex_lock(&chip->icl_lock);
		rc = oplus_wired_set_qc_config(OPLUS_CHG_QC_2_0, 5000);
		mutex_unlock(&chip->icl_lock);
		if (rc < 0) {
			chip->qc_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = 5000;
		break;
	default:
		goto set_curr;
	}

	oplus_wired_current_set(chip, true);
	reinit_completion(&chip->qc_action_ack);
	if (!READ_ONCE(chip->chg_online)) {
		chg_info("charger offline\n");
		return;
	}
	rc = wait_for_completion_timeout(
		&chip->qc_action_ack,
		msecs_to_jiffies(PDQC_CONFIG_WAIT_TIME_MS));
	if (rc < 0) {
		chg_err("qc config timeout\n");
		chip->vbus_mv = oplus_wired_get_vbus();
		if (chip->vbus_mv >= 7500)
			vbus_set_mv = 9000;
		else
			vbus_set_mv = 5000;
		chip->qc_action = OPLUS_ACTION_NULL;
	}
	if (chip->qc_action == OPLUS_ACTION_BOOST)
		vbus_set_mv = 9000;
	else if (chip->qc_action == OPLUS_ACTION_BUCK)
		vbus_set_mv = 5000;
	chip->qc_action = OPLUS_ACTION_NULL;

	if (vbus_set_mv != chip->vbus_set_mv) {
		chip->vbus_set_mv = vbus_set_mv;
		vbus_changed = true;
	}

set_curr:
	/* The configuration fails and the current needs to be reset */
	oplus_wired_current_set(chip, vbus_changed);
}

static int oplus_wired_get_afi_condition(void)
{
	int afi_condition = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_GET_AFI_CONDITION,
				     &data, true);
	if (!rc)
		afi_condition = data.intval;

	pr_err("%s, get afi condition = %d\n", __func__, afi_condition);
	return afi_condition;
}

#define PD_RETRY_DELAY msecs_to_jiffies(1000)
#define PD_RETRY_COUNT_MAX 3
static void oplus_wired_pd_config_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, pd_config_work.work);
	struct oplus_wired_spec_config *spec = &chip->spec;
	int cool_down, cool_down_vol;
	int vbus_set_mv = 5000; /* vbus default setting voltage is 5V */
	bool vbus_changed = false;
	int rc;

#define OPLUS_PD_5V_PDO 0x31912c
#define OPLUS_PD_9V_PDO 0x32d12c

	if (chip->chg_mode != OPLUS_WIRED_CHG_MODE_PD) {
		chg_err("chg_mode(=%d) error\n", chip->chg_mode);
		goto set_curr;
	}

	if (chip->cool_down > 0) {
		cool_down = chip->cool_down > spec->cool_down_pdqc_level_max ?
				    spec->cool_down_pdqc_level_max :
				    chip->cool_down;
		cool_down_vol = spec->cool_down_pdqc_vol_mv[cool_down - 1];
	} else {
		cool_down_vol = 0;
	}

	switch (chip->pd_action) {
	case OPLUS_ACTION_BOOST:
		if (is_pd_svooc_votable_available(chip) &&
		    !!get_effective_result(chip->pd_svooc_votable)) {
			chg_info("pd_svooc check, pd cannot be boosted\n");
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		if (chip->pd_boost_disable) {
			chg_info("pd boost is disable\n");
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		if (cool_down_vol > 0 && cool_down_vol < 9000) {
			chg_info("cool down limit, pd cannot be boosted\n");
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		if (spec->vbatt_pdqc_to_9v_thr > 0 &&
		    chip->vbat_mv < spec->vbatt_pdqc_to_9v_thr) {
			chg_info("pd starts to boost\n");
			mutex_lock(&chip->icl_lock);
			rc = oplus_wired_set_pd_config(OPLUS_PD_9V_PDO);
			mutex_unlock(&chip->icl_lock);
			if (rc < 0) {
				if (chip->pd_retry_count < PD_RETRY_COUNT_MAX) {
					chip->pd_retry_count++;
					schedule_delayed_work(
						&chip->pd_config_work,
						PD_RETRY_DELAY);
					return;
				} else {
					chip->pd_retry_count = 0;
					chip->pd_action = OPLUS_ACTION_NULL;
					vote(chip->pd_boost_disable_votable,
					     TIMEOUT_VOTER, true, 1, false);
					chg_err("set pd boost timeout\n");
					goto set_curr;
				}
			}
			chip->pd_retry_count = 0;
		} else {
			chg_info("battery voltage too high, pd cannot be boosted\n");
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = 9000;
		break;
	case OPLUS_ACTION_BUCK:
		chg_info("pd starts to buck\n");
		/* Set the current to 500ma before stepping down */
		vote(chip->icl_votable, SPEC_VOTER, true, PDQC_BUCK_DEF_CURR_MA,
		     true);
		mutex_lock(&chip->icl_lock);
		rc = oplus_wired_set_pd_config(OPLUS_PD_5V_PDO);
		mutex_unlock(&chip->icl_lock);
		if (rc < 0) {
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = 5000;
		break;
	default:
		goto set_curr;
	}

	oplus_wired_current_set(chip, true);
	reinit_completion(&chip->pd_action_ack);
	if (!READ_ONCE(chip->chg_online)) {
		chg_info("charger offline\n");
		return;
	}
	rc = wait_for_completion_timeout(
		&chip->pd_action_ack,
		msecs_to_jiffies(PDQC_CONFIG_WAIT_TIME_MS));
	if (rc < 0) {
		chg_err("pd config timeout\n");
		chip->vbus_mv = oplus_wired_get_vbus();
		if (chip->vbus_mv >= 7500)
			vbus_set_mv = 9000;
		else
			vbus_set_mv = 5000;
		chip->pd_action = OPLUS_ACTION_NULL;
	}
	if (chip->pd_action == OPLUS_ACTION_BOOST)
		vbus_set_mv = 9000;
	else if (chip->pd_action == OPLUS_ACTION_BUCK)
		vbus_set_mv = 5000;
	chip->pd_action = OPLUS_ACTION_NULL;

	if (vbus_set_mv != chip->vbus_set_mv) {
		chip->vbus_set_mv = vbus_set_mv;
		vbus_changed = true;
	}

set_curr:
	/* The configuration fails and the current needs to be reset */
	oplus_wired_current_set(chip, vbus_changed);
}

static void oplsu_wired_strategy_update(struct oplus_chg_wired *chip)
{
	struct oplus_chg_strategy *strategy;
	int tmp;
	int rc;

	switch (chip->chg_mode) {
	case OPLUS_WIRED_CHG_MODE_VOOC:
		strategy = chip->vooc_strategy;
		break;
	default:
		strategy = NULL;
		break;
	}
	if (strategy == NULL)
		return;

	rc = oplus_chg_strategy_get_data(strategy, &tmp);
	if (rc < 0) {
		vote(chip->icl_votable, STRATEGY_VOTER, false, 0, true);
		chg_err("get strategy data error, rc=%d", rc);
	} else {
		vote(chip->icl_votable, STRATEGY_VOTER, true, tmp, true);
	}
}

static void oplus_wired_gauge_update_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, gauge_update_work);
	struct oplus_wired_spec_config *spec = &chip->spec;
	union mms_msg_data data = { 0 };
	int cool_down_vol = 0;
	int cool_down;

	if (!chip->chg_online)
		return;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				false);
	chip->vbat_mv = data.intval;
	chip->vbus_mv = oplus_wired_get_vbus();
	if (chip->vbus_mv < 0)
		chip->vbus_mv = 0;

	if ((chip->qc_action == OPLUS_ACTION_BOOST && chip->vbus_mv > 7500) ||
	    (chip->qc_action == OPLUS_ACTION_BUCK && chip->vbus_mv < 7500))
		complete(&chip->qc_action_ack);
	if ((chip->pd_action == OPLUS_ACTION_BOOST && chip->vbus_mv > 7500) ||
	    (chip->pd_action == OPLUS_ACTION_BUCK && chip->vbus_mv < 7500))
		complete(&chip->pd_action_ack);

	if (chip->cool_down > 0) {
		cool_down = chip->cool_down > spec->cool_down_pdqc_level_max ?
				    spec->cool_down_pdqc_level_max :
				    chip->cool_down;
		cool_down_vol = spec->cool_down_pdqc_vol_mv[cool_down - 1];
	}
	if (chip->vbus_mv > 7500 &&
	    ((spec->vbatt_pdqc_to_5v_thr > 0 &&
	      chip->vbat_mv >= spec->vbatt_pdqc_to_5v_thr) ||
	     (cool_down_vol > 0 && cool_down_vol < 9000))) {
		if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_QC) {
			chip->qc_action = OPLUS_ACTION_BUCK;
			schedule_work(&chip->qc_config_work);
		} else if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_PD) {
			chip->pd_action = OPLUS_ACTION_BUCK;
			schedule_delayed_work(&chip->pd_config_work, 0);
		}
	}

	if (oplus_wired_get_afi_condition())
		oplus_gauge_protect_check();

	oplsu_wired_strategy_update(chip);
	oplus_wired_vbus_check(chip);
	if (!chip->vooc_started)
		oplus_wired_kick_wdt(chip->wired_topic);
}

static void oplus_wired_gauge_subs_callback(struct mms_subscribe *subs,
					    enum mms_msg_type type, u32 id)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };
	int rc;

	switch (type) {
	case MSG_TYPE_TIMER:
		schedule_work(&chip->gauge_update_work);
		break;
	case MSG_TYPE_ITEM:
		switch (id) {
		case GAUGE_ITEM_AUTH:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n",
					rc);
				chip->authenticate = false;
			} else {
				chip->authenticate = !!data.intval;
			}
			break;
		case GAUGE_ITEM_HMAC:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n",
					rc);
				chip->hmac = false;
			} else {
				chip->hmac = !!data.intval;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_gauge_topic(struct oplus_mms *topic,
					      void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };
	int rc;

	chip->gauge_topic = topic;
	chip->gauge_subs = oplus_mms_subscribe(chip->gauge_topic, chip,
					       oplus_wired_gauge_subs_callback,
					       "chg_wired");
	if (IS_ERR_OR_NULL(chip->gauge_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->gauge_subs));
		return;
	}

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				false);
	chip->vbat_mv = data.intval;
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_HMAC, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n", rc);
		chip->hmac = false;
	} else {
		chip->hmac = !!data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_AUTH, &data,
				     true);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n", rc);
		chip->authenticate = false;
	} else {
		chip->authenticate = !!data.intval;
	}
	chg_info("hmac=%d, authenticate=%d\n", chip->hmac, chip->authenticate);
}

static void oplus_wired_wired_subs_callback(struct mms_subscribe *subs,
					    enum mms_msg_type type, u32 id)
{
	struct oplus_chg_wired *chip = subs->priv_data;

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			schedule_work(&chip->plugin_work);
			break;
		case WIRED_ITEM_CHG_TYPE:
			schedule_work(&chip->chg_type_change_work);
			break;
		case WIRED_ITEM_CC_MODE:
		case WIRED_ITEM_CC_DETECT:
			break;
		case WIRED_ITEM_CHARGER_CURR_MAX:
			schedule_work(&chip->charger_current_changed_work);
			break;
		case WIRED_ITEM_CHARGER_VOL_MAX:
			/* TODO */
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_wired_topic(struct oplus_mms *topic,
					      void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };
	int boot_mode = get_boot_mode();

	chip->wired_topic = topic;
	chip->wired_subs = oplus_mms_subscribe(chip->wired_topic, chip,
					       oplus_wired_wired_subs_callback,
					       "chg_wired");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	if (!chip->vooc_support)
		oplus_wired_qc_detect_enable(true);
	else
		oplus_wired_qc_detect_enable(false);

	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN)
		vote(chip->input_suspend_votable, WLAN_VOTER, true, 1, false);

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				true);
	chip->chg_online = data.intval;
	if (!chip->chg_online)
		schedule_work(&chip->plugin_work);
}

static void oplus_wired_plugin_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, plugin_work);
	union mms_msg_data data = { 0 };

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				false);
	chip->chg_online = data.intval;
	if (chip->chg_online) {
		oplus_wired_set_awake(chip, true);
		if (chip->gauge_topic != NULL) {
			oplus_mms_get_item_data(chip->gauge_topic,
						GAUGE_ITEM_VOL_MAX, &data,
						true);
			chip->vbat_mv = data.intval;
			chg_info("vbat_mv %d\n", chip->vbat_mv);
		}
		/* Ensure that the charging status is as expected */
		rerun_election(chip->output_suspend_votable, false);
		rerun_election(chip->input_suspend_votable, false);
		/*
		 * Ensure that the instantaneous current when charging is
		 * turned on will not be too large
		 */
		vote(chip->icl_votable, SPEC_VOTER, true, 500, true);
		vote(chip->output_suspend_votable, DEF_VOTER, false, 0, false);
		vote(chip->input_suspend_votable, DEF_VOTER, false, 0, false);
		oplus_wired_current_set(chip, false);
		if (chip->vooc_strategy != NULL)
			oplus_chg_strategy_init(chip->vooc_strategy);
	} else {
		/*
		 * Set during plug out to prevent untimely settings
		 * during plug in
		 */
		vote(chip->pd_boost_disable_votable, SVID_VOTER, true, 1,
		     false);
		vote(chip->pd_boost_disable_votable, TIMEOUT_VOTER, false, 0,
		     false);
		vote(chip->input_suspend_votable, DEF_VOTER, true, 1, false);
		vote(chip->output_suspend_votable, DEF_VOTER, true, 1, false);
		vote(chip->icl_votable, SPEC_VOTER, true, 0, true);
		vote(chip->fcc_votable, SPEC_VOTER, true, 0, true);

		/* USER_VOTER and HIDL_VOTER need to be invalid when the usb is unplugged */
		vote(chip->icl_votable, USER_VOTER, false, 0, true);
		vote(chip->fcc_votable, USER_VOTER, false, 0, true);
		vote(chip->icl_votable, HIDL_VOTER, false, 0, true);
		vote(chip->icl_votable, MAX_VOTER, false, 0, true);
		vote(chip->icl_votable, STRATEGY_VOTER, false, 0, true);
		chip->pd_retry_count = 0;
		chip->qc_action = OPLUS_ACTION_NULL;
		chip->pd_action = OPLUS_ACTION_NULL;
		complete_all(&chip->qc_action_ack);
		complete_all(&chip->pd_action_ack);
		cancel_work_sync(&chip->qc_config_work);
		cancel_delayed_work_sync(&chip->pd_config_work);
		chip->vbus_set_mv = 5000;
		oplus_wired_set_err_code(chip, 0);

		if (is_pd_svooc_votable_available(chip))
			vote(chip->pd_svooc_votable, DEF_VOTER, false, 0,
			     false);
		oplus_wired_set_awake(chip, false);
	}

	if (chip->gauge_topic != NULL)
		oplus_mms_topic_update(chip->gauge_topic, true);
}

static void oplus_wired_chg_type_change_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip = container_of(
		work, struct oplus_chg_wired, chg_type_change_work);

	chip->chg_type = oplus_wired_get_chg_type();
	if (chip->chg_type < 0)
		chip->chg_type = OPLUS_CHG_USB_TYPE_UNKNOWN;
	chg_info("chg_type = %d\n", chip->chg_type);

	switch (chip->chg_type) {
	case OPLUS_CHG_USB_TYPE_QC2:
	case OPLUS_CHG_USB_TYPE_QC3:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_QC;
		chip->qc_action = OPLUS_ACTION_BOOST;
		schedule_work(&chip->qc_config_work);
		break;
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_DRP:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_PD;
		chip->pd_action = OPLUS_ACTION_BOOST;
		schedule_delayed_work(&chip->pd_config_work, 0);
		break;
	default:
		oplus_wired_current_set(chip, false);
		break;
	}
}

static void oplus_wired_charger_current_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip = container_of(
		work, struct oplus_chg_wired, charger_current_changed_work);
	union mms_msg_data data = { 0 };
	int rc;

	rc = oplus_mms_get_item_data(chip->wired_topic,
				     WIRED_ITEM_CHARGER_CURR_MAX, &data, false);
	if (rc < 0) {
		chg_err("can't get charger curr max msg data\n");
		return;
	}
	vote(chip->icl_votable, MAX_VOTER, true, data.intval, true);
}

static void oplus_wired_temp_region_update_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip = container_of(
		work, struct oplus_chg_wired, temp_region_update_work);

	if (chip->temp_region == TEMP_REGION_HOT ||
	    chip->temp_region == TEMP_REGION_COLD)
		vote(chip->output_suspend_votable, BATT_TEMP_VOTER, true, 0,
		     false);
	else
		vote(chip->output_suspend_votable, BATT_TEMP_VOTER, false, 0,
		     false);

	oplus_wired_current_set(chip, false);
}

static void oplus_wired_led_on_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, led_on_changed_work);

	if (!chip->chg_online)
		return;

	oplus_wired_current_set(chip, false);
}

static void oplus_wired_icl_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, icl_changed_work);

	rerun_election(chip->icl_votable, true);
}

static void oplus_wired_fcc_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, fcc_changed_work);

	rerun_election(chip->fcc_votable, false);
}

static void oplus_wired_comm_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	struct oplus_wired_spec_config *spec = &chip->spec;
	union mms_msg_data data = { 0 };
	int rc;

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_TEMP_REGION:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->temp_region = data.intval;
			schedule_work(&chip->temp_region_update_work);
			break;
		case COMM_ITEM_FCC_GEAR:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->fcc_gear = data.intval;
			schedule_work(&chip->temp_region_update_work);
			break;
		case COMM_ITEM_COOL_DOWN:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->cool_down = data.intval;
			/*
			 * Need to recheck the type and check whether the
			 * charging voltage needs to be adjusted.
			 */
			schedule_work(&chip->chg_type_change_work);
			break;
		case COMM_ITEM_CHARGING_DISABLE:
			rc = oplus_mms_get_item_data(chip->comm_topic, id,
						     &data, false);
			if (rc < 0)
				pr_err("can't get charging disable status, rc=%d",
				       rc);
			else
				vote(chip->output_suspend_votable, USER_VOTER,
				     !!data.intval, data.intval, false);
			break;
		case COMM_ITEM_CHARGE_SUSPEND:
			rc = oplus_mms_get_item_data(chip->comm_topic, id,
						     &data, false);
			if (rc < 0)
				pr_err("can't get charge suspend status, rc=%d",
				       rc);
			else
				vote(chip->input_suspend_votable, USER_VOTER,
				     !!data.intval, data.intval, false);
			break;
		case COMM_ITEM_UNWAKELOCK:
			rc = oplus_mms_get_item_data(chip->comm_topic, id,
						     &data, false);
			if (rc < 0)
				break;
			chip->unwakelock_chg = data.intval;
			oplus_wired_set_awake(chip, !chip->unwakelock_chg);
			/* charger WDT enable/disable */
			oplus_wired_wdt_enable(chip->wired_topic,
					       !chip->unwakelock_chg);
			break;
		case COMM_ITEM_FACTORY_TEST:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			if (!!data.intval && spec->vbatt_pdqc_to_9v_thr > 0) {
				if (oplus_gauge_get_batt_num() == 2) {
					if (chip->adjust_pdqc_vol_thr_support)
						spec->vbatt_pdqc_to_9v_thr =
							FACTORY_MODE_PDQC_9V_THR;
					else
						spec->vbatt_pdqc_to_9v_thr =
							oplus_wired_get_vbatt_pdqc_to_9v_thr(chip);
					schedule_work(&chip->chg_type_change_work);
				}
			} else {
				spec->vbatt_pdqc_to_9v_thr =
					oplus_wired_get_vbatt_pdqc_to_9v_thr(
						chip);
			}
			break;
		case COMM_ITEM_LED_ON:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			schedule_work(&chip->led_on_changed_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_comm_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };
	int rc;

	chip->comm_topic = topic;
	chip->comm_subs = oplus_mms_subscribe(chip->comm_topic, chip,
					      oplus_wired_comm_subs_callback,
					      "chg_wired");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->comm_subs));
		return;
	}

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_TEMP_REGION, &data,
				true);
	chip->temp_region = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_FCC_GEAR, &data,
				true);
	chip->fcc_gear = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_COOL_DOWN, &data,
				true);
	chip->cool_down = data.intval;
	rc = oplus_mms_get_item_data(chip->comm_topic,
				     COMM_ITEM_CHARGING_DISABLE, &data, true);
	if (rc < 0)
		pr_err("can't get charging disable status, rc=%d", rc);
	else
		vote(chip->output_suspend_votable, USER_VOTER, !!data.intval,
		     data.intval, false);
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_CHARGE_SUSPEND,
				     &data, true);
	if (rc < 0)
		pr_err("can't get charge suspend status, rc=%d", rc);
	else
		vote(chip->input_suspend_votable, USER_VOTER, !!data.intval,
		     data.intval, false);
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UNWAKELOCK,
				     &data, true);
	if (rc < 0) {
		pr_err("can't get unwakelock_chg status, rc=%d", rc);
		chip->unwakelock_chg = false;
	} else {
		chip->unwakelock_chg = data.intval;
		oplus_wired_set_awake(chip, !chip->unwakelock_chg);
	}
}

static void oplus_wired_vooc_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case VOOC_ITEM_VOOC_STARTED:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_started = data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_vooc_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->vooc_topic = topic;
	chip->vooc_subs = oplus_mms_subscribe(chip->vooc_topic, chip,
					      oplus_wired_vooc_subs_callback,
					      "chg_wired");
	if (IS_ERR_OR_NULL(chip->vooc_subs)) {
		chg_err("subscribe vooc topic error, rc=%ld\n",
			PTR_ERR(chip->vooc_subs));
		return;
	}

	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_VOOC_STARTED, &data,
				true);
	chip->vooc_started = data.intval;
}

static int oplus_wired_fcc_vote_callback(struct votable *votable, void *data,
					 int fcc_ma, const char *client,
					 bool step)
{
	struct oplus_chg_wired *chip = data;
	int rc;

	if (fcc_ma < 0)
		return 0;

	rc = oplus_wired_set_fcc(fcc_ma);

	if (is_batt_psy_available(chip))
		power_supply_changed(chip->batt_psy);

	return rc;
}

static int oplus_wired_icl_vote_callback(struct votable *votable, void *data,
					 int icl_ma, const char *client,
					 bool step)
{
	struct oplus_chg_wired *chip = data;
	int rc;

	if (icl_ma < 0)
		return 0;

	chg_info("icl vote clent %s\n", client);
	mutex_lock(&chip->icl_lock);
	if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_VOOC && chip->vooc_started)
		rc = oplus_wired_set_icl_by_vooc(chip->wired_topic, icl_ma);
	else
		rc = oplus_wired_set_icl(icl_ma, step);
	mutex_unlock(&chip->icl_lock);

	if (is_usb_psy_available(chip))
		power_supply_changed(chip->usb_psy);

	return rc;
}

static int oplus_wired_input_suspend_vote_callback(struct votable *votable,
						   void *data, int disable,
						   const char *client,
						   bool step)
{
	struct oplus_chg_wired *chip = data;
	static bool suspend = true;
	int rc;

	chg_info("charger suspend change to %s by %s\n",
		 disable ? "true" : "false", client);
	rc = oplus_wired_input_enable(!disable);
	if (chip->chg_online) {
		if (is_vooc_chg_auto_mode_votable_available(chip))
			vote(chip->vooc_chg_auto_mode_votable,
			     CHARGE_SUSPEND_VOTER, disable, disable, false);
		else
			chg_err("vooc_chg_auto_mode_votable not found\n");
	}

	/* Restore current setting */
	if (!disable && suspend) {
		chg_info("rerun icl vote\n");
		suspend = false;
		schedule_work(&chip->icl_changed_work);
	} else {
		suspend = disable;
	}

	if (is_usb_psy_available(chip))
		power_supply_changed(chip->usb_psy);

	return rc;
}

static int oplus_wired_output_suspend_vote_callback(struct votable *votable,
						    void *data, int disable,
						    const char *client,
						    bool step)
{
	struct oplus_chg_wired *chip = data;
	static bool suspend = true;
	int rc;

	chg_info("charging disabled change to %s by %s\n",
		 disable ? "true" : "false", client);
	rc = oplus_wired_output_enable(!disable);
	if (chip->chg_online) {
		if (is_vooc_chg_auto_mode_votable_available(chip))
			vote(chip->vooc_chg_auto_mode_votable,
			     CHAEGE_DISABLE_VOTER, disable, disable, false);
		else
			chg_err("vooc_chg_auto_mode_votable not found\n");
	}

	/* Restore current setting */
	if (!disable && suspend) {
		chg_info("rerun fcc/icl vote\n");
		suspend = false;
		schedule_work(&chip->fcc_changed_work);
		schedule_work(&chip->icl_changed_work);
	} else {
		suspend = disable;
	}

	if (is_batt_psy_available(chip))
		power_supply_changed(chip->batt_psy);

	return rc;
}

static int oplus_wired_pd_boost_disable_vote_callback(struct votable *votable,
						      void *data, int disable,
						      const char *client,
						      bool step)
{
	struct oplus_chg_wired *chip = data;

	chip->pd_boost_disable = !!disable;
	if (chip->pd_boost_disable)
		chg_info("pd boost disable by %s\n", client);
	else
		chg_info("pd boost enable\n");

	return 0;
}

static int oplus_wired_vote_init(struct oplus_chg_wired *chip)
{
	int rc;

	chip->fcc_votable = create_votable("WIRED_FCC", VOTE_MIN,
					   oplus_wired_fcc_vote_callback, chip);
	if (IS_ERR(chip->fcc_votable)) {
		rc = PTR_ERR(chip->fcc_votable);
		chip->fcc_votable = NULL;
		return rc;
	}

	chip->icl_votable = create_votable("WIRED_ICL", VOTE_MIN,
					   oplus_wired_icl_vote_callback, chip);
	if (IS_ERR(chip->icl_votable)) {
		rc = PTR_ERR(chip->icl_votable);
		chip->icl_votable = NULL;
		goto create_icl_votable_err;
	}

	chip->input_suspend_votable =
		create_votable("WIRED_CHARGE_SUSPEND", VOTE_SET_ANY,
			       oplus_wired_input_suspend_vote_callback, chip);
	if (IS_ERR(chip->input_suspend_votable)) {
		rc = PTR_ERR(chip->input_suspend_votable);
		chip->input_suspend_votable = NULL;
		goto create_input_suspend_votable_err;
	}

	chip->output_suspend_votable =
		create_votable("WIRED_CHARGING_DISABLE", VOTE_SET_ANY,
			       oplus_wired_output_suspend_vote_callback, chip);
	if (IS_ERR(chip->output_suspend_votable)) {
		rc = PTR_ERR(chip->output_suspend_votable);
		chip->output_suspend_votable = NULL;
		goto create_output_suspend_votable_err;
	}

	chip->pd_boost_disable_votable =
		create_votable("PD_BOOST_DISABLE", VOTE_SET_ANY,
			       oplus_wired_pd_boost_disable_vote_callback,
			       chip);
	if (IS_ERR(chip->pd_boost_disable_votable)) {
		rc = PTR_ERR(chip->pd_boost_disable_votable);
		chip->pd_boost_disable_votable = NULL;
		goto create_pd_boost_disable_votable_err;
	}
	/* boost is disabled by default, need to wait for SVID recognition */
	vote(chip->pd_boost_disable_votable, SVID_VOTER, true, 1, false);

	return 0;

create_pd_boost_disable_votable_err:
	destroy_votable(chip->output_suspend_votable);
create_output_suspend_votable_err:
	destroy_votable(chip->input_suspend_votable);
create_input_suspend_votable_err:
	destroy_votable(chip->icl_votable);
create_icl_votable_err:
	destroy_votable(chip->fcc_votable);
	return rc;
}

static int oplus_wired_parse_dt(struct oplus_chg_wired *chip)
{
	struct oplus_wired_spec_config *spec = &chip->spec;
	struct oplus_wired_config *config = &chip->config;
	struct device_node *node = chip->dev->of_node;
	int i, m;
	int rc;

	chip->vooc_support = of_property_read_bool(node, "oplus,vooc-support");
	chip->adjust_pdqc_vol_thr_support = of_property_read_bool(node,
						"oplus,adjust-pdqc-vol-thr-support");

	rc = of_property_read_u32(node, "oplus_spec,pd-iclmax-ma",
				  &spec->pd_iclmax_ma);
	if (rc < 0) {
		chg_err("oplus_spec,pd-iclmax-ma reading failed, rc=%d\n", rc);
		spec->pd_iclmax_ma = default_config.pd_iclmax_ma;
	}
	rc = of_property_read_u32(node, "oplus_spec,qc-iclmax-ma",
				  &spec->qc_iclmax_ma);
	if (rc < 0) {
		chg_err("oplus_spec,qc-iclmax-ma reading failed, rc=%d\n", rc);
		spec->qc_iclmax_ma = default_config.qc_iclmax_ma;
	}
	rc = of_property_read_u32(node, "oplus_spec,non-standard-ibatmax-ma",
				  &spec->non_standard_ibatmax_ma);
	if (rc < 0) {
		chg_err("oplus_spec,non-standard-ibatmax-ma reading failed, rc=%d\n",
			rc);
		spec->non_standard_ibatmax_ma =
			default_config.non_standard_ibatmax_ma;
	}

	rc = read_unsigned_data_from_node(node, "oplus_spec,input-power-mw",
					  (u32 *)(spec->input_power_mw),
					  OPLUS_WIRED_CHG_MODE_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,input-power-mw error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++)
			spec->input_power_mw[i] =
				default_config.input_power_mw[i];
	}

	rc = read_unsigned_data_from_node(node, "oplus_spec,led_on-fccmax-ma",
					  (u32 *)(spec->led_on_fcc_max_ma),
					  TEMP_REGION_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,led_on-fccmax-ma error, rc=%d\n", rc);
		for (i = 0; i < TEMP_REGION_MAX; i++)
			spec->led_on_fcc_max_ma[i] =
				default_config.led_on_fcc_max_ma[i];
	}

	rc = read_unsigned_data_from_node(
		node, "oplus_spec,fccmax-ma-lv", (u32 *)(spec->fcc_ma[0]),
		OPLUS_WIRED_CHG_MODE_MAX * TEMP_REGION_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,fccmax-ma-lv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
			for (m = 0; m < TEMP_REGION_MAX; m++)
				spec->fcc_ma[0][i][m] =
					default_config.fcc_ma[0][i][m];
		}
	}

	rc = read_unsigned_data_from_node(
		node, "oplus_spec,fccmax-ma-hv", (u32 *)(spec->fcc_ma[1]),
		OPLUS_WIRED_CHG_MODE_MAX * TEMP_REGION_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,fccmax-ma-hv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
			for (m = 0; m < TEMP_REGION_MAX; m++)
				spec->fcc_ma[1][i][m] =
					default_config.fcc_ma[1][i][m];
		}
	}

	rc = of_property_read_u32(node, "oplus_spec,vbatt_pdqc_to_5v_thr",
				  &spec->vbatt_pdqc_to_5v_thr);
	if (rc < 0) {
		chg_err("oplus_spec,vbatt_pdqc_to_5v_thr reading failed, rc=%d\n",
			rc);
		spec->vbatt_pdqc_to_5v_thr =
			default_config.vbatt_pdqc_to_5v_thr;
	}
	rc = of_property_read_u32(node, "oplus_spec,vbatt_pdqc_to_9v_thr",
				  &spec->vbatt_pdqc_to_9v_thr);
	if (rc < 0) {
		chg_err("oplus_spec,vbatt_pdqc_to_9v_thr reading failed, rc=%d\n",
			rc);
		spec->vbatt_pdqc_to_9v_thr =
			default_config.vbatt_pdqc_to_9v_thr;
	}

	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,cool_down_pdqc_vol_mv",
					  (u32 *)(spec->cool_down_pdqc_vol_mv),
					  WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_pdqc_vol_mv error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_pdqc_vol_mv[i] =
				default_config.cool_down_pdqc_vol_mv[i];
			spec->cool_down_pdqc_level_max =
				default_config.cool_down_pdqc_level_max;
		}
	} else {
		spec->cool_down_pdqc_level_max = rc;
	}
	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,cool_down_pdqc_curr_ma",
					  (u32 *)(spec->cool_down_pdqc_curr_ma),
					  WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0 || spec->cool_down_pdqc_level_max != rc) {
		chg_err("get oplus_spec,cool_down_pdqc_curr_ma error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_pdqc_curr_ma[i] =
				default_config.cool_down_pdqc_curr_ma[i];
			spec->cool_down_pdqc_level_max =
				default_config.cool_down_pdqc_level_max;
		}
	}
	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,cool_down_vooc_curr_ma",
					  (u32 *)(spec->cool_down_vooc_curr_ma),
					  WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_vooc_curr_ma error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_vooc_curr_ma[i] =
				default_config.cool_down_vooc_curr_ma[i];
			spec->cool_down_vooc_level_max =
				default_config.cool_down_vooc_level_max;
		}
	} else {
		spec->cool_down_vooc_level_max = rc;
	}
	rc = read_unsigned_data_from_node(
		node, "oplus_spec,cool_down_normal_curr_ma",
		(u32 *)(spec->cool_down_normal_curr_ma),
		WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_normal_curr_ma error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_normal_curr_ma[i] =
				default_config.cool_down_normal_curr_ma[i];
			spec->cool_down_normal_level_max =
				default_config.cool_down_normal_level_max;
		}
	} else {
		spec->cool_down_normal_level_max = rc;
	}

	rc = read_unsigned_data_from_node(node, "oplus_spec,vbus_ov_thr_mv",
					  (u32 *)(spec->vbus_ov_thr_mv),
					  OPLUS_VBUS_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,vbus_ov_thr_mv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_VBUS_MAX; i++)
			spec->vbus_ov_thr_mv[i] =
				default_config.vbus_ov_thr_mv[i];
	}
	rc = read_unsigned_data_from_node(node, "oplus_spec,vbus_uv_thr_mv",
					  (u32 *)(spec->vbus_uv_thr_mv),
					  OPLUS_VBUS_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,vbus_uv_thr_mv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_VBUS_MAX; i++)
			spec->vbus_uv_thr_mv[i] =
				default_config.vbus_uv_thr_mv[i];
	}

	rc = of_property_read_string(
		node, "oplus,vooc_strategy_name",
		(const char **)&config->vooc_strategy_name);
	if (rc >= 0) {
		chg_info("vooc_strategy_name=%s\n", config->vooc_strategy_name);
		rc = oplus_chg_strategy_read_data(chip->dev,
						  "oplus,vooc_strategy_data",
						  &config->vooc_strategy_data);
		if (rc < 0) {
			chg_err("read oplus,vooc_strategy_data failed, rc=%d\n",
				rc);
			config->vooc_strategy_data = NULL;
			config->vooc_strategy_data_size = 0;
		} else {
			chg_info("oplus,vooc_strategy_data size is %d\n", rc);
			config->vooc_strategy_data_size = rc;
		}
	}

	return 0;
}

static int oplus_wired_strategy_init(struct oplus_chg_wired *chip)
{
	struct oplus_wired_config *config = &chip->config;
	int boot_mode = get_boot_mode();

	if (boot_mode == MSM_BOOT_MODE__CHARGE) {
		/* just use in poweroff charge mode */
		chip->vooc_strategy = oplus_chg_strategy_alloc(
			config->vooc_strategy_name, config->vooc_strategy_data,
			config->vooc_strategy_data_size);
		if (chip->vooc_strategy == NULL)
			chg_err("vooc strategy alloc error");
	}

	return 0;
}

static int oplus_wired_probe(struct platform_device *pdev)
{
	struct oplus_chg_wired *chip;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_wired),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);

	rc = oplus_wired_parse_dt(chip);
	if (rc < 0)
		goto parse_dt_err;

	/*
	 * We need to initialize the resources that may be used in the
	 * subsequent initialization process in advance
	 */
	init_completion(&chip->qc_action_ack);
	init_completion(&chip->pd_action_ack);
	INIT_WORK(&chip->plugin_work, oplus_wired_plugin_work);
	INIT_WORK(&chip->chg_type_change_work,
		  oplus_wired_chg_type_change_work);
	INIT_WORK(&chip->temp_region_update_work,
		  oplus_wired_temp_region_update_work);
	INIT_WORK(&chip->gauge_update_work, oplus_wired_gauge_update_work);
	INIT_WORK(&chip->qc_config_work, oplus_wired_qc_config_work);
	INIT_DELAYED_WORK(&chip->pd_config_work, oplus_wired_pd_config_work);
	INIT_WORK(&chip->charger_current_changed_work,
		  oplus_wired_charger_current_changed_work);
	INIT_WORK(&chip->led_on_changed_work, oplus_wired_led_on_changed_work);
	INIT_WORK(&chip->icl_changed_work, oplus_wired_icl_changed_work);
	INIT_WORK(&chip->fcc_changed_work, oplus_wired_fcc_changed_work);

	rc = oplus_wired_vote_init(chip);
	if (rc < 0)
		goto vote_init_err;

	rc = oplus_wired_strategy_init(chip);
	if (rc < 0)
		goto strategy_init_err;

	oplus_wired_variables_init(chip);

	oplus_wired_awake_init(chip);

	oplus_mms_wait_topic("gauge", oplus_wired_subscribe_gauge_topic, chip);
	oplus_mms_wait_topic("wired", oplus_wired_subscribe_wired_topic, chip);
	oplus_mms_wait_topic("common", oplus_wired_subscribe_comm_topic, chip);
	oplus_mms_wait_topic("vooc", oplus_wired_subscribe_vooc_topic, chip);

	chg_info("probe success\n");
	return 0;

strategy_init_err:
	destroy_votable(chip->pd_boost_disable_votable);
	destroy_votable(chip->output_suspend_votable);
	destroy_votable(chip->input_suspend_votable);
	destroy_votable(chip->icl_votable);
	destroy_votable(chip->fcc_votable);
vote_init_err:
	if (chip->config.vooc_strategy_data)
		devm_kfree(&pdev->dev, chip->config.vooc_strategy_data);
parse_dt_err:
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);
	chg_err("probe error, rc=%d\n", rc);
	return rc;
}

static int oplus_wired_remove(struct platform_device *pdev)
{
	struct oplus_chg_wired *chip = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(chip->comm_subs))
		oplus_mms_unsubscribe(chip->comm_subs);
	if (!IS_ERR_OR_NULL(chip->wired_subs))
		oplus_mms_unsubscribe(chip->wired_subs);
	if (!IS_ERR_OR_NULL(chip->gauge_subs))
		oplus_mms_unsubscribe(chip->gauge_subs);
	oplus_wired_awake_exit(chip);
	if (chip->vooc_strategy)
		oplus_chg_strategy_release(chip->vooc_strategy);
	destroy_votable(chip->pd_boost_disable_votable);
	destroy_votable(chip->output_suspend_votable);
	destroy_votable(chip->input_suspend_votable);
	destroy_votable(chip->icl_votable);
	destroy_votable(chip->fcc_votable);
	if (chip->config.vooc_strategy_data)
		devm_kfree(&pdev->dev, chip->config.vooc_strategy_data);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id oplus_wired_match[] = {
	{ .compatible = "oplus,wired" },
	{},
};

static struct platform_driver oplus_wired_driver = {
	.driver		= {
		.name = "oplus-wired",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_wired_match),
	},
	.probe		= oplus_wired_probe,
	.remove		= oplus_wired_remove,
};

static __init int oplus_wired_init(void)
{
	return platform_driver_register(&oplus_wired_driver);
}

static __exit void oplus_wired_exit(void)
{
	platform_driver_unregister(&oplus_wired_driver);
}

oplus_chg_module_register(oplus_wired);
