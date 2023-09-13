#define pr_fmt(fmt) "[CHGLIB]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/err.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include <oplus_chg_ic.h>
#include <oplus_hal_vooc.h>
#include <oplus_chg_vooc.h>
#include <oplus_mms_wired.h>
#include <oplus_chg_comm.h>
#include <../voocphy/oplus_voocphy.h>
#include "oplus_chglib.h"

static struct oplus_chg_ic_virq vphy_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_VOOC_DATA },
};

struct vphy_chip *oplus_chglib_get_vphy_chip(struct device *dev)
{
	return ((struct oplus_voocphy_manager *)dev_get_drvdata(dev))->vchip;
}

int oplus_chglib_disable_charger(bool disable)
{
	struct votable *disable_votable;
	int rc;

	disable_votable = find_votable("WIRED_CHARGING_DISABLE");
	if (!disable_votable) {
		chg_err("WIRED_CHARGING_DISABLE votable not found\n");
		return -EINVAL;
	}

	rc = vote(disable_votable, FASTCHG_VOTER, disable,
		  disable ? 1 : 0, false);
	if (rc < 0)
		chg_err("%s charger error, rc = %d\n",
			disable ? "disable" : "enable", rc);
	else
		chg_info("%s charger\n", disable ? "disable" : "enable");

	return rc;
}

int oplus_chglib_suspend_charger(bool suspend)
{
	struct votable *suspend_votable;
	int rc;

	suspend_votable = find_votable("WIRED_CHARGE_SUSPEND");
	if (!suspend_votable) {
		chg_err("WIRED_CHARGE_SUSPEND votable not found\n");
		return -EINVAL;
	}

	rc = vote(suspend_votable, FASTCHG_VOTER, suspend,
		  suspend ? 1 : 0, false);
	if (rc < 0)
		chg_err("%s charger error, rc = %d\n",
			suspend ? "suspend" : "unsuspend", rc);
	else
		chg_info("%s charger\n", suspend ? "suspend" : "unsuspend");

        return rc;
}

int oplus_chglib_vooc_fastchg_disable(const char *client_str, bool disable)
{
	struct votable *vooc_disable_votable;
	int rc;

	vooc_disable_votable = find_votable("VOOC_DISABLE");
	if (!vooc_disable_votable) {
		chg_err("VOOC_DISABLE votable not found\n");
		return -EINVAL;
	}

	rc = vote(vooc_disable_votable, client_str, disable,
		  disable ? 1 : 0, false);
	if (rc < 0)
		chg_err("%s vooc charger error, rc = %d\n",
			disable ? "disable" : "enable", rc);
	else
		chg_info("%s vooc charger\n", disable ? "disable" : "enable");

        return rc;
}

static bool is_gauge_topic_available(struct vphy_chip *chip)
{
	if (!chip->gauge_topic)
		chip->gauge_topic = oplus_mms_get_by_name("gauge");

	return !!chip->gauge_topic;
}

int oplus_chglib_get_soc(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);
	union mms_msg_data data = {0};

	if (is_gauge_topic_available(chip))
		oplus_mms_get_item_data(chip->gauge_topic,
					GAUGE_ITEM_SOC, &data, true);
	else
		chg_err("gauge topic not found\n");

	chg_debug("get soc = %d\n", data.intval);

	return data.intval;
}

int oplus_chglib_gauge_vbatt(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);
	union mms_msg_data data = {0};

	if (is_gauge_topic_available(chip))
		oplus_mms_get_item_data(chip->gauge_topic,
					GAUGE_ITEM_VOL_MAX, &data, false);
	else
		chg_err("gauge topic is NULL\n");

	chg_debug("get battery voltage = %d\n", data.intval);

	return data.intval;
}

int oplus_chglib_gauge_pre_vbatt(struct device *dev)
{
	return oplus_chglib_gauge_vbatt(dev);
}

int oplus_chglib_gauge_sub_vbatt(void)
{
	return 3800;
}

int oplus_chglib_gauge_current(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);
	union mms_msg_data data = {0};

	if (is_gauge_topic_available(chip))
		oplus_mms_get_item_data(chip->gauge_topic,
					GAUGE_ITEM_CURR, &data, false);
	else
		chg_err("gauge_topic is NULL\n");

	chg_debug("current = %d\n", data.intval);

	return data.intval;
}

int oplus_chglib_gauge_sub_current(void)
{
	return 1000;
}

int oplus_chglib_get_shell_temp(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);
	union mms_msg_data data = {0};

	if (chip->common_topic)
		oplus_mms_get_item_data(chip->common_topic,
					COMM_ITEM_SHELL_TEMP, &data, false);
	else
		chg_err("common topic not found\n");

	chg_debug("get shell temp = %d\n", data.intval);

	return data.intval;
}

bool oplus_chglib_get_led_on(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);

	chg_debug("get led status %s\n", chip->led_on ? "on" : "off");

	return chip->led_on;
}

bool oplus_chglib_is_pd_svooc_adapter(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);

	return chip->is_pd_svooc_adapter;
}

bool oplus_chglib_is_switch_temp_range(void)
{
	struct votable *vooc_disable_votable;
	bool enabled = false;

	vooc_disable_votable = find_votable("VOOC_DISABLE");
	if (vooc_disable_votable)
		enabled = is_client_vote_enabled(vooc_disable_votable,
						 SWITCH_RANGE_VOTER);
	else
		chg_err("VOOC_DISABLE not found\n");

	return enabled;
}

bool oplus_chglib_get_flash_led_status(void)
{
	return false;
}

int oplus_chglib_get_battery_btb_temp_cal(void)
{
	return oplus_wired_get_batt_btb_temp();
}

int oplus_chglib_get_usb_btb_temp_cal(void)
{
	return oplus_wired_get_usb_btb_temp();
}

bool oplus_chglib_get_chg_stats(void)
{
	return oplus_wired_is_present();
}

int oplus_chglib_get_charger_voltage(void)
{
	return oplus_wired_get_vbus();
}

bool oplus_chglib_get_vooc_is_started(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);
	union mms_msg_data data = {0};

	oplus_mms_get_item_data(chip->vooc_topic,
				VOOC_ITEM_VOOC_STARTED, &data, false);

	return data.intval ? true : false;
}

bool oplus_chglib_get_vooc_sid_is_config(struct device *dev)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);
	union mms_msg_data data = {0};

	oplus_mms_get_item_data(chip->vooc_topic,
				VOOC_ITEM_SID, &data, false);

	return data.intval ? true : false;
}

int oplus_chglib_notify_ap(struct device *dev, int event)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);

	chip->fastchg_notify_event = event;
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_VOOC_DATA);

	return 0;
}

int oplus_chglib_push_break_code(struct device *dev, int code)
{
	struct vphy_chip *chip = oplus_chglib_get_vphy_chip(dev);
	struct mms_msg *msg;
	int rc;

	if (!chip->vooc_topic) {
		chg_info("vooc topic is null\n");
		return -ENODEV;
	}

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				      VOOC_ITEM_BREAK_CODE, code);
	if (msg == NULL) {
		chg_err("alloc break code msg error\n");
		return -ENOMEM;
	}

	rc = oplus_mms_publish_msg_sync(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish break code msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static void oplus_chglib_check_charger_out_work(struct work_struct *work)
{
	struct vphy_chip *chip = container_of(work,
			struct vphy_chip, check_charger_out_work);
	union mms_msg_data data = {0};

	oplus_mms_get_item_data(chip->wired_topic,
				WIRED_ITEM_ONLINE, &data, false);
	if (data.intval == 0) {
		if (chip->vinf->vphy_disconnect_detect)
			chip->vinf->vphy_disconnect_detect(chip->dev);
	}
}

static void oplus_chglib_wired_subs_callback(struct mms_subscribe *subs,
					     enum mms_msg_type type, u32 id)
{
	struct vphy_chip *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			schedule_work(&chip->check_charger_out_work);
			break;
		case WIRED_TIME_ABNORMAL_ADAPTER:
			oplus_mms_get_item_data(chip->wired_topic,
						WIRED_TIME_ABNORMAL_ADAPTER,
						&data, false);
			chip->is_pd_svooc_adapter = !!data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_chglib_subscribe_wired_topic(struct oplus_mms *topic, void *prv_data)
{
	struct vphy_chip *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs = oplus_mms_subscribe(chip->wired_topic, chip,
					       oplus_chglib_wired_subs_callback,
					       "voocphy_chglib");
	if (IS_ERR_OR_NULL(chip->wired_subs))
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));

	oplus_mms_get_item_data(chip->wired_topic,
				WIRED_TIME_ABNORMAL_ADAPTER, &data, true);
	chip->is_pd_svooc_adapter = !!data.intval;
}

static void oplus_chglib_send_absent_notify(struct vphy_chip *chip)
{
	int status;

	if (chip->vinf->vphy_get_fastchg_notify_status) {
		status = chip->vinf->vphy_get_fastchg_notify_status(chip->dev);
		if (status == FAST_NOTIFY_DUMMY_START) {
			chg_err("send FAST_NOTIFY_ABSENT event\n");
			oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ABSENT);
		}
	} else {
		chg_err("vphy_get_fastchg_notify_status is NULL\n");
	}
}

static void oplus_chglib_send_absent_notify_work(struct work_struct *work)
{
	struct vphy_chip *chip = container_of(work,
			struct vphy_chip, send_absent_notify_work);

	oplus_chglib_send_absent_notify(chip);
}

static void oplus_chglib_send_adapter_id_notify(struct vphy_chip *chip)
{
	union mms_msg_data data = { 0 };
	int fastchg_type;

	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_SID, &data, false);
	if (data.intval != 0)
		return;

	if (chip->vinf->vphy_get_fastchg_type) {
		fastchg_type = chip->vinf->vphy_get_fastchg_type(chip->dev);
		chg_info("fastchg_type = 0x%x\n", fastchg_type);
		if (fastchg_type > FASTCHG_CHARGER_TYPE_VOOC) {
			oplus_chglib_notify_ap(chip->dev, fastchg_type);
		} else if (fastchg_type == FASTCHG_CHARGER_TYPE_VOOC) {
			oplus_chglib_notify_ap(chip->dev, VOOC_ADAPTER_ID);
		} else {
			/* fastchg_type == FASTCHG_CHARGER_TYPE_UNKOWN, no need handle */
		}
	} else {
		chg_err("vphy_get_fastchg_type is NULL\n");
	}
}

static void oplus_chglib_send_adapter_id_work(struct work_struct *work)
{
	struct vphy_chip *chip = container_of(work,
			struct vphy_chip, send_adapter_id_work);

	oplus_chglib_send_adapter_id_notify(chip);
}

static void oplus_chglib_vooc_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct vphy_chip *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case VOOC_ITEM_SID:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data, false);
			if (data.intval)
				schedule_work(&chip->send_absent_notify_work);
			break;
		case VOOC_ITEM_ONLINE_KEEP:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data, false);
			if (data.intval)
				schedule_work(&chip->send_adapter_id_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_chglib_subscribe_vooc_topic(struct oplus_mms *topic,
					      void *prv_data)
{
	struct vphy_chip *chip = prv_data;

	chip->vooc_topic = topic;
	chip->vooc_subs = oplus_mms_subscribe(chip->vooc_topic, chip,
					      oplus_chglib_vooc_subs_callback,
					      "voocphy_chglib");
	if (IS_ERR_OR_NULL(chip->vooc_subs))
		chg_err("subscribe vooc topic error, rc=%ld\n",
			PTR_ERR(chip->vooc_subs));

}

static void oplus_chglib_set_current_work(struct work_struct *work)
{
	struct vphy_chip *chip = container_of(work,
			struct vphy_chip, set_current_work);

	if (chip && chip->vinf && chip->vinf->vphy_set_cool_down)
		chip->vinf->vphy_set_cool_down(chip->dev, chip->cool_down);
}

static void oplus_chglib_common_subs_callback(struct mms_subscribe *subs,
					      enum mms_msg_type type, u32 id)
{
	struct vphy_chip *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_COOL_DOWN:
			oplus_mms_get_item_data(chip->common_topic, id, &data,
						false);
			chip->cool_down = data.intval;
			schedule_work(&chip->set_current_work);
			break;
		case COMM_ITEM_LED_ON:
			oplus_mms_get_item_data(chip->common_topic, id, &data,
						false);
			chip->led_on = !!data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_chglib_subscribe_common_topic(struct oplus_mms *topic,
					      void *prv_data)
{
	struct vphy_chip *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->common_topic = topic;
	chip->common_subs = oplus_mms_subscribe(chip->common_topic, chip,
						oplus_chglib_common_subs_callback,
						"voocphy_chglib");
	if (IS_ERR_OR_NULL(chip->common_subs))
		chg_err("subscribe common topic error, rc=%ld\n",
			PTR_ERR(chip->common_subs));

	oplus_mms_get_item_data(chip->common_topic, COMM_ITEM_LED_ON, &data,
				true);
	chip->led_on = !!data.intval;
}

static int vphy_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;

	return OPLUS_VPHY_IC_VPHY;
}

static int vphy_exit(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = false;

	return 0;
}

static int vphy_reply_data(struct oplus_chg_ic_dev *ic_dev,
			   int data, int data_width, int curr_ma)
{
	struct vphy_chip *chip;

	if (!ic_dev->online)
		return 0;
	chip = oplus_chglib_get_vphy_chip(ic_dev->dev);

	if (chip && chip->vinf && chip->vinf->vphy_set_screenoff_current)
		chip->vinf->vphy_set_screenoff_current(chip->dev, curr_ma);

	return 0;
}

static int vphy_get_data(struct oplus_chg_ic_dev *ic_dev, int *data)
{
	struct vphy_chip *chip;

	if (!ic_dev->online)
		return 0;
	chip = oplus_chglib_get_vphy_chip(ic_dev->dev);
	*data = chip->fastchg_notify_event;

	return 0;
}

static int vphy_set_switch_mode(struct oplus_chg_ic_dev *ic_dev, int mode)
{
	struct vphy_chip *chip;

	if (!ic_dev->online)
		return 0;
	chip = oplus_chglib_get_vphy_chip(ic_dev->dev);

	if (chip && chip->vinf && chip->vinf->vphy_switch_chg_mode)
		chip->vinf->vphy_switch_chg_mode(chip->dev, mode);

	return 0;
}

static int vphy_set_pdqc_config(struct oplus_chg_ic_dev *ic_dev)
{
	struct vphy_chip *chip;

	if (!ic_dev->online)
		return 0;
	chip = oplus_chglib_get_vphy_chip(ic_dev->dev);

	if (chip && chip->vinf && chip->vinf->vphy_set_pdqc_config)
		chip->vinf->vphy_set_pdqc_config(chip->dev);

	return 0;
}

static int vphy_get_cp_vbat(struct oplus_chg_ic_dev *ic_dev, int *cp_vbat)
{
	struct vphy_chip *chip;

	if (!ic_dev->online)
		return 0;
	chip = oplus_chglib_get_vphy_chip(ic_dev->dev);

	if (chip && chip->vinf && chip->vinf->vphy_get_cp_vbat)
		*cp_vbat = chip->vinf->vphy_get_cp_vbat(chip->dev);

	return 0;
}

static int vphy_set_chg_auto_mode(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	struct vphy_chip *chip;

	if (!ic_dev->online)
		return 0;
	chip = oplus_chglib_get_vphy_chip(ic_dev->dev);

	if (chip && chip->vinf && chip->vinf->vphy_set_chg_auto_mode)
		chip->vinf->vphy_set_chg_auto_mode(chip->dev, enable);

	return 0;
}

static void *vphy_get_func(struct oplus_chg_ic_dev *ic_dev,
			    enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, vphy_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, vphy_exit);
		break;
	case OPLUS_IC_FUNC_VOOC_REPLY_DATA:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_REPLY_DATA,
					       vphy_reply_data);
		break;
	case OPLUS_IC_FUNC_VOOC_READ_DATA:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_READ_DATA,
					       vphy_get_data);
		break;
	case OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE,
					       vphy_set_switch_mode);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_PDQC_CONFIG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_PDQC_CONFIG,
					       vphy_set_pdqc_config);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_GET_CP_VBAT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_GET_CP_VBAT,
					       vphy_get_cp_vbat);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_CHG_AUTO_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_CHG_AUTO_MODE,
					       vphy_set_chg_auto_mode);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct vphy_chip *oplus_chglib_register_vphy(struct device *dev, struct hw_vphy_info *vinf)
{
	struct vphy_chip *chip;
	struct device_node *node = dev->of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(dev, sizeof(struct vphy_chip),
			    GFP_KERNEL);
	if (!chip) {
		chg_err("Couldn't allocate memory\n");
		rc = -ENOMEM;
		goto error;
	}

	chip->vinf = vinf;
	chip->dev = dev;

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto error;
	}
	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto error;
	}

	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	sprintf(ic_cfg.manu_name, "vphy");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = vphy_get_func;
	ic_cfg.virq_data = vphy_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(vphy_virq_table);
	chip->ic_dev =
		devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto error;
	}

	INIT_WORK(&chip->set_current_work, oplus_chglib_set_current_work);
	INIT_WORK(&chip->send_adapter_id_work, oplus_chglib_send_adapter_id_work);
	INIT_WORK(&chip->send_absent_notify_work, oplus_chglib_send_absent_notify_work);
	INIT_WORK(&chip->check_charger_out_work, oplus_chglib_check_charger_out_work);
	oplus_mms_wait_topic("wired", oplus_chglib_subscribe_wired_topic, chip);
	oplus_mms_wait_topic("vooc", oplus_chglib_subscribe_vooc_topic, chip);
	oplus_mms_wait_topic("common", oplus_chglib_subscribe_common_topic, chip);
	chg_info("register %s\n", node->name);

	return chip;
error:
	return ERR_PTR(rc);
}
