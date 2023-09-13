#define pr_fmt(fmt) "[GKI]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/power_supply.h>
#include <soc/oplus/system/oplus_project.h>
#include <oplus_chg.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_vooc.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_mms_wired.h>

struct oplus_gki_device {
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct power_supply *wls_psy;
	struct mms_subscribe *gauge_subs;
	struct mms_subscribe *wired_subs;
	struct mms_subscribe *wls_subs;
	struct mms_subscribe *comm_subs;
	struct mms_subscribe *vooc_subs;
	struct oplus_mms *wired_topic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *wls_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *vooc_topic;

	struct work_struct gauge_update_work;

	struct votable *wired_icl_votable;
	struct votable *wired_fcc_votable;
	struct votable *fv_votable;
	struct votable *vooc_curr_votable;

	bool led_on;

	bool batt_exist;
	bool batt_auth;
	bool batt_hmac;
	bool ui_soc_ready;
	int vbat_mv;
	int vbat_min_mv;
	int temperature;
	int soc;
	int batt_fcc;
	int batt_rm;
	int batt_status;
	int batt_health;
	int batt_chg_type;
	int ui_soc;
	int batt_capacity_mah;

	bool wired_online;
	int wired_type;
	int vbus_mv;
	int charger_cycle;
	bool vooc_charging;
	bool vooc_started;

	bool wls_online;
	int wls_type;

	bool smart_charging_screenoff;
};

static struct oplus_gki_device *g_gki_dev;

__maybe_unused static bool
is_wired_icl_votable_available(struct oplus_gki_device *chip)
{
	if (!chip->wired_icl_votable)
		chip->wired_icl_votable = find_votable("WIRED_ICL");
	return !!chip->wired_icl_votable;
}

__maybe_unused static bool
is_wired_fcc_votable_available(struct oplus_gki_device *chip)
{
	if (!chip->wired_fcc_votable)
		chip->wired_fcc_votable = find_votable("WIRED_FCC");
	return !!chip->wired_fcc_votable;
}

__maybe_unused static bool
is_fv_votable_available(struct oplus_gki_device *chip)
{
	if (!chip->fv_votable)
		chip->fv_votable = find_votable("FV_MAX");
	return !!chip->fv_votable;
}

__maybe_unused static bool
is_vooc_curr_votable_available(struct oplus_gki_device *chip)
{
	if (!chip->vooc_curr_votable)
		chip->vooc_curr_votable = find_votable("VOOC_CURR");
	return !!chip->vooc_curr_votable;
}

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct oplus_gki_device *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		pval->intval = chip->wls_online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		break;
	default:
		chg_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		chg_err("Couldn't get prop %d rc = %d\n", prop, rc);
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

static const struct power_supply_desc wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= wls_props,
	.num_properties		= ARRAY_SIZE(wls_props),
	.get_property		= wls_psy_get_prop,
	.set_property		= wls_psy_set_prop,
	.property_is_writeable	= wls_psy_prop_is_writeable,
};


static int oplus_to_psy_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_TYPE_USB_DCP,
};

static int oplus_to_power_supply_type[] = {
	POWER_SUPPLY_TYPE_UNKNOWN,
	POWER_SUPPLY_TYPE_USB,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_CDP,
	POWER_SUPPLY_TYPE_USB_ACA,
	POWER_SUPPLY_TYPE_USB_TYPE_C,
	POWER_SUPPLY_TYPE_USB_PD,
	POWER_SUPPLY_TYPE_USB_PD_DRP,
	POWER_SUPPLY_TYPE_USB_PD,
	POWER_SUPPLY_TYPE_USB,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_DCP,
};

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct oplus_gki_device *chip = power_supply_get_drvdata(psy);
	union mms_msg_data data = { 0 };
	int usb_temp_l, usb_temp_r;
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		pval->intval = chip->wired_online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		pval->intval = oplus_wired_get_vbus() * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		pval->intval =
			oplus_wired_get_voltage_max(chip->wired_topic) * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		pval->intval =  oplus_wired_get_ibus() * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (is_wired_icl_votable_available(chip))
			pval->intval = get_client_vote(chip->wired_icl_votable,
						       SPEC_VOTER) *
				       1000;
		else
			rc = -ENOTSUPP;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (is_wired_icl_votable_available(chip))
			pval->intval =
				get_effective_result(chip->wired_icl_votable) *
				1000;
		else
			rc = -ENOTSUPP;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		pval->intval = oplus_to_psy_usb_type[chip->wired_type];
		break;
	case POWER_SUPPLY_PROP_TEMP:
		oplus_mms_get_item_data(chip->wired_topic,
					WIRED_ITEM_USB_TEMP_L, &data, true);
		usb_temp_l = data.intval;
		oplus_mms_get_item_data(chip->wired_topic,
					WIRED_ITEM_USB_TEMP_R, &data, true);
		usb_temp_r = data.intval;
		pval->intval = max(usb_temp_l, usb_temp_r);
		break;
	default:
		chg_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		chg_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}

	return 0;
}

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;
	default:
		chg_err("set prop %d is not supported\n", prop);
		return -EINVAL;
	}

	return rc;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_usb_type usb_psy_supported_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

static struct power_supply_desc usb_psy_desc = {
	.name			= "usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= usb_props,
	.num_properties		= ARRAY_SIZE(usb_props),
	.get_property		= usb_psy_get_prop,
	.set_property		= usb_psy_set_prop,
	.usb_types		= usb_psy_supported_types,
	.num_usb_types		= ARRAY_SIZE(usb_psy_supported_types),
	.property_is_writeable	= usb_psy_prop_is_writeable,
};

static int oplus_gki_get_ui_soc(struct oplus_gki_device *chip)
{
	union mms_msg_data data = { 0 };
	int ui_soc = 50; /* default soc to use on error */
	int rc = 0;

	if (chip->ui_soc_ready)
		return chip->ui_soc;

	chg_err("ui_soc not ready, use chip soc\n");
	if (chip->gauge_topic == NULL) {
		chg_err("gauge_topic is NULL, use default soc\n");
		return ui_soc;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data,
				     true);
	if (rc < 0) {
		chg_err("can't get battery soc, rc=%d\n", rc);
		return ui_soc;
	}
	ui_soc = data.intval;

	return ui_soc;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct oplus_gki_device *chip = power_supply_get_drvdata(psy);
	union mms_msg_data data = { 0 };
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		pval->intval = chip->batt_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		pval->intval = chip->batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		pval->intval = chip->batt_exist;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		pval->intval = chip->batt_chg_type;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		pval->intval = oplus_gki_get_ui_soc(chip);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		if (is_fv_votable_available(chip))
			pval->intval =
				(get_client_vote(chip->fv_votable, SPEC_VOTER) +
				 50) *
				1000;
		else
			rc = -ENOTSUPP;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = oplus_mms_get_item_data(chip->gauge_topic,
					     GAUGE_ITEM_VOL_MAX, &data, false);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		pval->intval = data.intval;
#else
		pval->intval = data.intval * 1000;
#endif
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = oplus_mms_get_item_data(chip->gauge_topic,
					     GAUGE_ITEM_VOL_MAX, &data, false);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		pval->intval = data.intval;
#else
		pval->intval = data.intval * 1000;
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR,
					     &data, (chip->wired_online || chip->wls_online));
		pval->intval = data.intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = 6500000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		/* not support CHARGE_CONTROL_LIMIT, just 1 level */
		pval->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		pval->intval = chip->temperature;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		pval->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		pval->intval = chip->batt_rm * 1000;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		pval->intval = chip->charger_cycle;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		pval->intval = chip->batt_capacity_mah * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		pval->intval = chip->batt_capacity_mah * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		if (!chip->batt_auth || !chip->batt_hmac)
			pval->strval = "uncertified battery";
		else
			pval->strval = "oplus battery";
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		pval->intval = 1800;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		pval->intval = 1800;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		pval->intval = 3600;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
	case POWER_SUPPLY_PROP_POWER_AVG:
		pval->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (chip->wired_online && chip->vooc_started == true &&
				chip->wired_type == OPLUS_CHG_USB_TYPE_SVOOC)
			pval->intval = 10000;
		else if (chip->wls_online)
			pval->intval = 0; /* TODO */
		else
			pval->intval = oplus_wired_get_vbus();
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		rc = oplus_mms_get_item_data(chip->gauge_topic,
					     GAUGE_ITEM_VOL_MIN, &data, false);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		pval->intval = data.intval;
#else
		pval->intval = data.intval * 1000;
#endif
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		pval->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		if (oplus_gki_get_ui_soc(chip) == 0) {
			pval->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
			chg_err("POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, should shutdown!!!\n");
			}
		break;
	default:
		chg_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}
	if (rc < 0) {
		chg_err("Couldn't get prop %d rc = %d\n", prop, rc);
		return -ENODATA;
	}

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct oplus_gki_device *chip = power_supply_get_drvdata(psy);
	int val;
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!chip->smart_charging_screenoff)
			return -ENOTSUPP;
		val = pval->intval & 0XFFFF;
		if (chip->wired_online &&
		    is_wired_icl_votable_available(chip)) {
			if (chip->led_on) {
				vote(chip->wired_icl_votable, HIDL_VOTER, false, 0, true);
				if (is_vooc_curr_votable_available(chip))
					vote(chip->vooc_curr_votable, HIDL_VOTER, false, 0, false);
			} else {
				vote(chip->wired_icl_votable, HIDL_VOTER, (val == 0) ? false : true, val, true);
				if (is_vooc_curr_votable_available(chip))
					vote(chip->vooc_curr_votable, HIDL_VOTER, (val == 0) ? false : true, val, false);
			}
		} else {
			rc = -ENOTSUPP;
		}
		break;
	default:
		chg_err("set prop %d is not supported\n", prop);
		return -EINVAL;
	}

	return rc;
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static const struct power_supply_desc batt_psy_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= battery_props,
	.num_properties		= ARRAY_SIZE(battery_props),
	.get_property		= battery_psy_get_prop,
	.set_property		= battery_psy_set_prop,
	.property_is_writeable	= battery_psy_prop_is_writeable,
};

#define FORCE_UPDATE_TIME	60
static void oplus_gki_gauge_update_work(struct work_struct *work)
{
	struct oplus_gki_device *chip =
		container_of(work, struct oplus_gki_device, gauge_update_work);
	union mms_msg_data data = { 0 };
	int eng_version;
	static unsigned long update_time = 0;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				false);
	chip->vbat_mv = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data,
				false);
	chip->vbat_min_mv = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data,
				false);
	chip->soc = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_FCC, &data,
				false);
	chip->batt_fcc = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_RM, &data,
				false);
	chip->batt_rm = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data,
				false);
	chip->temperature = data.intval;

	if (chip->wired_online)
		chip->charger_cycle = oplus_wired_get_charger_cycle();

	eng_version = get_eng_version();
	if (eng_version == AGING || eng_version == HIGH_TEMP_AGING ||
	    eng_version == FACTORY || chip->wired_online || chip->wls_online) {
		if (chip->batt_psy)
			power_supply_changed(chip->batt_psy);
	} else {
		if (time_is_before_jiffies(update_time)) {
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			update_time = jiffies + FORCE_UPDATE_TIME * HZ;
		}
	}
}

static void oplus_gki_gauge_subs_callback(struct mms_subscribe *subs,
					  enum mms_msg_type type, u32 id)
{
	struct oplus_gki_device *chip = subs->priv_data;
	union mms_msg_data data = { 0 };
	int rc;

	switch (type) {
	case MSG_TYPE_TIMER:
		schedule_work(&chip->gauge_update_work);
		break;
	case MSG_TYPE_ITEM:
		switch (id) {
		case GAUGE_ITEM_BATT_EXIST:
			rc = oplus_mms_get_item_data(chip->gauge_topic,
						     GAUGE_ITEM_BATT_EXIST,
						     &data, false);
			if (rc < 0)
				chip->batt_exist = false;
			else
				chip->batt_exist = data.intval;
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case GAUGE_ITEM_AUTH:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n",
					rc);
				chip->batt_auth = false;
			} else {
				chip->batt_auth = !!data.intval;
			}
			break;
		case GAUGE_ITEM_HMAC:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n",
					rc);
				chip->batt_hmac = false;
			} else {
				chip->batt_hmac = !!data.intval;
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

static void oplus_gki_subscribe_gauge_topic(struct oplus_mms *topic, void *prv_data)
{
	struct oplus_gki_device *chip = prv_data;
	struct power_supply_config psy_cfg = {};
	union mms_msg_data data = { 0 };
	int rc;

	chip->gauge_topic = topic;
	chip->gauge_subs =
		oplus_mms_subscribe(chip->gauge_topic, chip,
				    oplus_gki_gauge_subs_callback, "gki");
	if (IS_ERR_OR_NULL(chip->gauge_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->gauge_subs));
		return;
	}

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				true);
	chip->vbat_mv = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data,
				true);
	chip->vbat_min_mv = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data,
				true);
	chip->soc = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_FCC, &data,
				true);
	chip->batt_fcc = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_RM, &data,
				true);
	chip->batt_rm = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data,
				true);
	chip->temperature = data.intval;
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_BATT_EXIST,
				     &data, true);
	if (rc < 0)
		chip->batt_exist = false;
	else
		chip->batt_exist = data.intval;

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_AUTH, &data,
				     true);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n", rc);
		chip->batt_auth = false;
	} else {
		chip->batt_auth = !!data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_HMAC, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n", rc);
		chip->batt_hmac = false;
	} else {
		chip->batt_hmac = !!data.intval;
	}

	chip->batt_capacity_mah = oplus_gauge_get_batt_capacity_mah(chip->gauge_topic);

	psy_cfg.drv_data = chip;
	chip->batt_psy = devm_power_supply_register(&topic->dev, &batt_psy_desc, &psy_cfg);
	if (IS_ERR(chip->batt_psy)) {
		chg_err("Failed to register battery power supply, rc=%ld\n",
			PTR_ERR(chip->batt_psy));
		goto psy_err;
	}

	return;

psy_err:
	chip->batt_psy = NULL;
	oplus_mms_unsubscribe(chip->gauge_subs);
}

static bool oplus_gki_bc12_is_completed(struct oplus_gki_device *chip)
{
	union mms_msg_data data = { 0 };

	if (chip->wired_topic == NULL)
		return false;

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_BC12_COMPLETED,
				&data, true);
	return !!data.intval;
}

static void oplus_gki_wired_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct oplus_gki_device *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			oplus_mms_get_item_data(chip->wired_topic, id, &data,
				false);
			chip->wired_online = data.intval;
			oplus_mms_get_item_data(chip->wired_topic,
						WIRED_ITEM_CHG_TYPE, &data,
						false);
			chip->wired_type = data.intval;
			chg_info("wired_online=%d\n", chip->wired_online);
			if (!chip->wired_online) {
				usb_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
			} else {
				usb_psy_desc.type = oplus_to_power_supply_type[chip->wired_type];
				if ((usb_psy_desc.type == POWER_SUPPLY_TYPE_UNKNOWN) &&
				    oplus_gki_bc12_is_completed(chip))
					usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
			}
			chg_info("psy_type=%d\n", usb_psy_desc.type);
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case WIRED_ITEM_CHG_TYPE:
			oplus_mms_get_item_data(chip->wired_topic, id, &data,
				false);
			chip->wired_type = data.intval;
			if (chip->wired_online) {
				usb_psy_desc.type = oplus_to_power_supply_type[chip->wired_type];
				if ((usb_psy_desc.type == POWER_SUPPLY_TYPE_UNKNOWN) &&
				    oplus_gki_bc12_is_completed(chip))
					usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
			}
			chg_info("psy_type=%d\n", usb_psy_desc.type);
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case WIRED_ITEM_BC12_COMPLETED:
			if (chip->wired_type == OPLUS_CHG_USB_TYPE_UNKNOWN)
				usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		default:
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_gki_subscribe_wired_topic(struct oplus_mms *topic, void *prv_data)
{
	struct oplus_gki_device *chip = prv_data;
	struct power_supply_config psy_cfg = {};
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    oplus_gki_wired_subs_callback, "gki");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				true);
	chip->wired_online = data.intval;
	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CHG_TYPE, &data,
				true);
	chip->wired_type = data.intval;
	if (chip->wired_online) {
		usb_psy_desc.type = oplus_to_power_supply_type[chip->wired_type];
		if ((usb_psy_desc.type == POWER_SUPPLY_TYPE_UNKNOWN) &&
		    oplus_gki_bc12_is_completed(chip))
			usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
	} else {
		usb_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	}
	chg_info("psy_type=%d\n", usb_psy_desc.type);

	chip->charger_cycle = oplus_wired_get_charger_cycle();

	psy_cfg.drv_data = chip;
	chip->usb_psy = devm_power_supply_register(&topic->dev, &usb_psy_desc,
						   &psy_cfg);
	if (IS_ERR(chip->usb_psy)) {
		chg_err("Failed to register usb power supply, rc=%ld\n",
			PTR_ERR(chip->usb_psy));
		goto psy_err;
	}

	return;

psy_err:
	chip->usb_psy = NULL;
	oplus_mms_unsubscribe(chip->wired_subs);
}

static void oplus_gki_wls_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	switch (type) {
	case MSG_TYPE_ITEM:
		break;
	default:
		break;
	}
}

static void oplus_gki_subscribe_wls_topic(struct oplus_mms *topic, void *prv_data)
{
	struct oplus_gki_device *chip = prv_data;
	struct power_supply_config psy_cfg = {};

	chip->wls_topic = topic;
	chip->wls_subs =
		oplus_mms_subscribe(chip->wls_topic, chip,
				    oplus_gki_wls_subs_callback, "gki");
	if (IS_ERR_OR_NULL(chip->wls_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->wls_subs));
		return;
	}

	psy_cfg.drv_data = chip;
	chip->wls_psy = devm_power_supply_register(&topic->dev, &batt_psy_desc, &psy_cfg);
	if (IS_ERR(chip->wls_psy)) {
		chg_err("Failed to register wireless power supply, rc=%ld\n",
			PTR_ERR(chip->wls_psy));
		goto psy_err;
	}

	return;

psy_err:
	chip->wls_psy = NULL;
	oplus_mms_unsubscribe(chip->wls_subs);
}

static void oplus_gki_comm_subs_callback(struct mms_subscribe *subs,
					 enum mms_msg_type type, u32 id)
{
	struct oplus_gki_device *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_BATT_STATUS:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->batt_status = data.intval;
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case COMM_ITEM_BATT_HEALTH:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->batt_health = data.intval;
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case COMM_ITEM_BATT_CHG_TYPE:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->batt_chg_type = data.intval;
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case COMM_ITEM_UI_SOC:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->ui_soc = data.intval;
			if (chip->ui_soc < 0) {
				chip->ui_soc_ready = false;
				chip->ui_soc = 0;
			} else {
				chip->ui_soc_ready = true;
			}
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case COMM_ITEM_LED_ON:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->led_on = data.intval;
			if (chip->led_on &&
			    is_vooc_curr_votable_available(chip))
				vote(chip->vooc_curr_votable, HIDL_VOTER, false, 0, false);
			break;
		case COMM_ITEM_CHARGING_DISABLE:
		case COMM_ITEM_CHARGE_SUSPEND:
		case COMM_ITEM_NOTIFY_CODE:
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_gki_subscribe_comm_topic(struct oplus_mms *topic,
						void *prv_data)
{
	struct oplus_gki_device *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->comm_topic = topic;
	chip->comm_subs = oplus_mms_subscribe(chip->comm_topic, chip,
					      oplus_gki_comm_subs_callback,
					      "gki");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe common topic error, rc=%ld\n",
			PTR_ERR(chip->comm_subs));
		return;
	}

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_BATT_STATUS, &data,
				true);
	chip->batt_status = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_BATT_HEALTH, &data,
				true);
	chip->batt_health = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_BATT_CHG_TYPE,
				&data, true);
	chip->batt_chg_type = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UI_SOC, &data,
				true);
	chip->ui_soc = data.intval;
	if (chip->ui_soc < 0) {
		chip->ui_soc_ready = false;
		chip->ui_soc = 0;
	} else {
		chip->ui_soc_ready = true;
	}
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_LED_ON, &data,
				true);
	chip->led_on = data.intval;
}

static void oplus_gki_vooc_subs_callback(struct mms_subscribe *subs,
					 enum mms_msg_type type, u32 id)
{
	struct oplus_gki_device *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case VOOC_ITEM_VOOC_CHARGING:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_charging = data.intval;
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		case VOOC_ITEM_VOOC_STARTED:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_started = data.intval;
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		default:
			if (chip->batt_psy)
				power_supply_changed(chip->batt_psy);
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_gki_subscribe_vooc_topic(struct oplus_mms *topic,
					   void *prv_data)
{
	struct oplus_gki_device *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->vooc_topic = topic;
	chip->vooc_subs = oplus_mms_subscribe(
		chip->vooc_topic, chip, oplus_gki_vooc_subs_callback, "gki");
	if (IS_ERR_OR_NULL(chip->vooc_subs)) {
		chg_err("subscribe vooc topic error, rc=%ld\n",
			PTR_ERR(chip->vooc_subs));
		return;
	}

	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_VOOC_CHARGING,
				&data, true);
	chip->vooc_charging = !!data.intval;

	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_VOOC_STARTED,
				&data, true);
	chip->vooc_started = !!data.intval;
}

static __init int oplus_chg_gki_init(void)
{
	struct oplus_gki_device *gki_dev;
	struct device_node *node;

	gki_dev = kzalloc(sizeof(struct oplus_gki_device), GFP_KERNEL);
	if (gki_dev == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	g_gki_dev = gki_dev;

	node = of_find_node_by_path("/soc/oplus_chg_core");
	if (node == NULL)
		gki_dev->smart_charging_screenoff = false;
	else
		gki_dev->smart_charging_screenoff = of_property_read_bool(
			node, "oplus,smart_charging_screenoff");

	INIT_WORK(&gki_dev->gauge_update_work, oplus_gki_gauge_update_work);

	oplus_mms_wait_topic("gauge", oplus_gki_subscribe_gauge_topic, gki_dev);
	oplus_mms_wait_topic("wired", oplus_gki_subscribe_wired_topic, gki_dev);
	oplus_mms_wait_topic("wireless", oplus_gki_subscribe_wls_topic, gki_dev);
	oplus_mms_wait_topic("common", oplus_gki_subscribe_comm_topic, gki_dev);
	oplus_mms_wait_topic("vooc", oplus_gki_subscribe_vooc_topic, gki_dev);

	return 0;
}

static __exit void oplus_chg_gki_exit(void)
{
	if (g_gki_dev == NULL)
		return;

	if (g_gki_dev->batt_psy)
		power_supply_unregister(g_gki_dev->batt_psy);
	if (g_gki_dev->usb_psy)
		power_supply_unregister(g_gki_dev->usb_psy);
	if (g_gki_dev->wls_psy)
		power_supply_unregister(g_gki_dev->wls_psy);
	if (!IS_ERR_OR_NULL(g_gki_dev->wired_subs))
		oplus_mms_unsubscribe(g_gki_dev->wired_subs);
	if (!IS_ERR_OR_NULL(g_gki_dev->gauge_subs))
		oplus_mms_unsubscribe(g_gki_dev->gauge_subs);
	if (!IS_ERR_OR_NULL(g_gki_dev->wls_subs))
		oplus_mms_unsubscribe(g_gki_dev->wls_subs);

	kfree(g_gki_dev);
	g_gki_dev = NULL;
}

oplus_chg_module_register(oplus_chg_gki);
