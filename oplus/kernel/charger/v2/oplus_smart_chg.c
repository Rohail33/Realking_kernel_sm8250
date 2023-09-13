#define pr_fmt(fmt) "[SMART_CHG]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>

#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_mms_wired.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_comm.h>
#include <oplus_smart_chg.h>

#define QUICK_MODE_POWER_THR_W	100

struct oplus_smart_charge {
	struct device *dev;
	struct oplus_mms *vooc_topic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *wired_topic;
	struct mms_subscribe *vooc_subs;
	struct mms_subscribe *gauge_subs;
	struct mms_subscribe *wired_subs;
	struct votable *cool_down_votable;

	struct work_struct quick_mode_check_work;

	bool wired_online;
	bool vooc_online;
	unsigned int vooc_sid;

	int normal_cool_down;
	int smart_normal_cool_down;
	struct timespec quick_mode_time;
	long start_time;
	long quick_mode_start_time;
	long quick_mode_stop_time;
	long quick_mode_gain_time_ms;
	int quick_mode_start_cap;
	int quick_mode_stop_cap;
	int quick_mode_stop_temp;
	int quick_mode_stop_soc;
	bool quick_mode_need_update;
	bool smart_chg_soh_support;

	int batt_soc;
	int batt_rm;
	int shell_temp;
};

static struct oplus_smart_charge *g_smart_chg;

static bool is_cool_down_votable_available(struct oplus_smart_charge *smart_chg)
{
	if (!smart_chg->cool_down_votable)
		smart_chg->cool_down_votable = find_votable("COOL_DOWN");
	return !!smart_chg->cool_down_votable;
}

static void oplus_smart_chg_quick_mode_init(struct oplus_smart_charge *smart_chg)
{
	smart_chg->quick_mode_time = oplus_current_kernel_time();
	smart_chg->start_time = smart_chg->quick_mode_time.tv_sec;
	smart_chg->quick_mode_gain_time_ms = 0;
	smart_chg->quick_mode_start_time = 0;
	smart_chg->quick_mode_stop_time = 0;
	smart_chg->quick_mode_start_cap = -1;
	smart_chg->quick_mode_stop_cap = -1;
	smart_chg->quick_mode_stop_temp = -1;
	smart_chg->quick_mode_stop_soc = -1;
}

static void oplus_smart_chg_gauge_update(struct oplus_smart_charge *smart_chg)
{
	union mms_msg_data data = { 0 };

	oplus_mms_get_item_data(smart_chg->gauge_topic, GAUGE_ITEM_RM, &data, false);
	smart_chg->batt_rm = data.intval;
	oplus_mms_get_item_data(smart_chg->gauge_topic, GAUGE_ITEM_SOC, &data, false);
	smart_chg->batt_soc = data.intval;

	if (smart_chg->comm_topic) {
		oplus_mms_get_item_data(smart_chg->comm_topic, COMM_ITEM_SHELL_TEMP, &data, false);
		smart_chg->shell_temp = data.intval;
	}
}

static void oplus_smart_chg_quick_mode_check(struct oplus_smart_charge *smart_chg)
{
	bool quick_mode_check = false;
	struct timespec ts_now;
	long diff_time, gain_time_ms;
	int batt_curve_current, cool_down, current_cool_down, current_normal_cool_down;

	if (!is_cool_down_votable_available(smart_chg)) {
		chg_err("cool_down votable not found\n");
		return;
	}
	cool_down = get_effective_result(smart_chg->cool_down_votable);

	if (smart_chg->vooc_online &&
	    (sid_to_adapter_power(smart_chg->vooc_sid) > QUICK_MODE_POWER_THR_W))
		quick_mode_check = true;
	/* TODO: PPS */

	if (!quick_mode_check)
		return;

	ts_now = oplus_current_kernel_time();
	diff_time = ts_now.tv_sec - smart_chg->quick_mode_time.tv_sec;
	smart_chg->quick_mode_time = ts_now;

	if (smart_chg->vooc_online) {
		if (smart_chg->vooc_topic == NULL) {
			chg_err("vooc topic not found\n");
			return;
		}
		batt_curve_current = oplus_vooc_get_batt_curve_current(smart_chg->vooc_topic);
		if (batt_curve_current < 0) {
			chg_err("can't get batt_curve_current, rc=%d\n",
				batt_curve_current);
			return;
		}
		current_cool_down = oplus_vooc_level_to_current(smart_chg->vooc_topic, cool_down);
		if (current_cool_down < 0) {
			chg_err("can't get current_cool_down, cool_down=%d, rc=%d\n",
				cool_down, current_cool_down);
			return;
		}
		current_normal_cool_down =
			oplus_vooc_level_to_current(smart_chg->vooc_topic, smart_chg->normal_cool_down);
		if (current_normal_cool_down < 0) {
			chg_err("can't get current_normal_cool_down, cool_down=%d, rc=%d\n",
				cool_down, current_normal_cool_down);
			return;
		}
	} else {
		/* TODO: PPS */
		return;
	}

	if ((current_cool_down != current_normal_cool_down) && (current_cool_down > 0)) {
		if (smart_chg->quick_mode_start_time == 0) {
			smart_chg->quick_mode_start_time = ts_now.tv_sec;
			smart_chg->quick_mode_start_cap = smart_chg->batt_rm;
		}
		smart_chg->quick_mode_need_update = true;
	}
	if ((current_cool_down == current_normal_cool_down) && smart_chg->quick_mode_need_update) {
		smart_chg->quick_mode_stop_time = ts_now.tv_sec;
		smart_chg->quick_mode_stop_cap = smart_chg->batt_rm;
		smart_chg->quick_mode_stop_temp = smart_chg->shell_temp;
		smart_chg->quick_mode_stop_soc = smart_chg->batt_soc;
		smart_chg->quick_mode_need_update = false;
	}

	if (current_cool_down > batt_curve_current) {
		if (current_normal_cool_down > batt_curve_current) {
			gain_time_ms = 0;
		} else {
			gain_time_ms = ((batt_curve_current * diff_time * 1000) / current_normal_cool_down) -
				       (diff_time * 1000);
		}
	} else {
		gain_time_ms = ((current_cool_down * diff_time * 1000) / current_normal_cool_down) - (diff_time * 1000);
	}
	smart_chg->quick_mode_gain_time_ms = smart_chg->quick_mode_gain_time_ms + gain_time_ms;

	chg_err("quick_mode_gain_time_ms:%ld, start_cap:%d, stop_cap:%d, "
		"start_time:%ld, stop_time:%ld, diff_time:%ld, gain_time_ms:%ld\n",
		smart_chg->quick_mode_gain_time_ms, smart_chg->quick_mode_start_cap, smart_chg->quick_mode_stop_cap,
		smart_chg->quick_mode_start_time, smart_chg->quick_mode_time.tv_sec, diff_time, gain_time_ms);
}

static void oplus_smart_chg_quick_mode_check_work(struct work_struct *work)
{
	struct oplus_smart_charge *smart_chg =
		container_of(work, struct oplus_smart_charge, quick_mode_check_work);

	if (smart_chg->wired_online && smart_chg->vooc_online) {
		oplus_smart_chg_quick_mode_check(smart_chg);
	} else if (smart_chg->quick_mode_need_update) {
		smart_chg->quick_mode_time = oplus_current_kernel_time();
		smart_chg->quick_mode_stop_time = smart_chg->quick_mode_time.tv_sec;
		smart_chg->quick_mode_stop_temp = smart_chg->shell_temp;
		smart_chg->quick_mode_stop_soc = smart_chg->batt_soc;
		smart_chg->quick_mode_need_update = false;
	}
}

static void oplus_smart_chg_vooc_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id)
{
	struct oplus_smart_charge *smart_chg = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_TIMER:
		break;
	case MSG_TYPE_ITEM:
		switch (id) {
		case VOOC_ITEM_ONLINE:
			oplus_mms_get_item_data(smart_chg->vooc_topic, id, &data, false);
			smart_chg->vooc_online = !!data.intval;
			break;
		case VOOC_ITEM_SID:
			oplus_mms_get_item_data(smart_chg->vooc_topic, id, &data, false);
			smart_chg->vooc_sid = (unsigned int)data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_smart_chg_subscribe_vooc_topic(struct oplus_mms *topic,
						 void *prv_data)
{
	struct oplus_smart_charge *smart_chg = prv_data;
	union mms_msg_data data = { 0 };
	int rc;

	smart_chg->vooc_topic = topic;
	smart_chg->vooc_subs =
		oplus_mms_subscribe(smart_chg->vooc_topic, smart_chg,
				    oplus_smart_chg_vooc_subs_callback, "smart_chg");
	if (IS_ERR_OR_NULL(smart_chg->vooc_subs)) {
		chg_err("subscribe vooc topic error, rc=%ld\n",
			PTR_ERR(smart_chg->vooc_subs));
		return;
	}

	rc = oplus_mms_get_item_data(smart_chg->vooc_topic, VOOC_ITEM_ONLINE, &data, true);
	if (rc < 0) {
		chg_err("can't get vooc online status, rc=%d\n", rc);
		smart_chg->vooc_online = false;
	} else {
		smart_chg->vooc_online = !!data.intval;
	}
	rc = oplus_mms_get_item_data(smart_chg->vooc_topic, VOOC_ITEM_SID, &data, false);
	if (rc < 0) {
		chg_err("can't get vooc sid, rc=%d\n", rc);
		smart_chg->vooc_sid = 0;
	} else {
		smart_chg->vooc_sid = (unsigned int)data.intval;
	}
}

static void oplus_smart_chg_gauge_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id)
{
	struct oplus_smart_charge *smart_chg = subs->priv_data;

	switch (type) {
	case MSG_TYPE_TIMER:
		oplus_smart_chg_gauge_update(smart_chg);
		schedule_work(&smart_chg->quick_mode_check_work);
		break;
	case MSG_TYPE_ITEM:
		break;
	default:
		break;
	}
}

static void oplus_smart_chg_subscribe_gauge_topic(struct oplus_mms *topic,
						  void *prv_data)
{
	struct oplus_smart_charge *smart_chg = prv_data;
	union mms_msg_data data = { 0 };

	smart_chg->gauge_topic = topic;
	smart_chg->gauge_subs =
		oplus_mms_subscribe(smart_chg->gauge_topic, smart_chg,
				    oplus_smart_chg_gauge_subs_callback, "smart_chg");
	if (IS_ERR_OR_NULL(smart_chg->gauge_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(smart_chg->gauge_subs));
		return;
	}

	oplus_mms_get_item_data(smart_chg->gauge_topic, GAUGE_ITEM_RM, &data, true);
	smart_chg->batt_rm = data.intval;
	oplus_mms_get_item_data(smart_chg->gauge_topic, GAUGE_ITEM_SOC, &data, true);
	smart_chg->batt_soc = data.intval;
}

static void oplus_smart_chg_wired_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id)
{
	struct oplus_smart_charge *smart_chg = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			oplus_mms_get_item_data(smart_chg->wired_topic, id, &data, false);
			smart_chg->wired_online = !!data.intval;
			if (smart_chg->wired_online && !smart_chg->vooc_online)
				oplus_smart_chg_quick_mode_init(smart_chg);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_smart_chg_subscribe_wired_topic(struct oplus_mms *topic,
						  void *prv_data)
{
	struct oplus_smart_charge *smart_chg = prv_data;
	union mms_msg_data data = { 0 };

	smart_chg->wired_topic = topic;
	smart_chg->wired_subs =
		oplus_mms_subscribe(smart_chg->wired_topic, smart_chg,
				    oplus_smart_chg_wired_subs_callback, "smart_chg");
	if (IS_ERR_OR_NULL(smart_chg->wired_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(smart_chg->wired_subs));
		return;
	}

	oplus_mms_get_item_data(smart_chg->wired_topic, WIRED_ITEM_ONLINE, &data, true);
	smart_chg->wired_online = !!data.intval;
}

static void oplus_smart_chg_common_topic_ready(struct oplus_mms *topic,
						void *prv_data)
{
	struct oplus_smart_charge *smart_chg = prv_data;
	union mms_msg_data data = { 0 };

	smart_chg->comm_topic = topic;
	oplus_mms_get_item_data(smart_chg->comm_topic, COMM_ITEM_SHELL_TEMP, &data, true);
	smart_chg->shell_temp = data.intval;

	oplus_mms_wait_topic("wired", oplus_smart_chg_subscribe_wired_topic, smart_chg);
	oplus_mms_wait_topic("vooc", oplus_smart_chg_subscribe_vooc_topic, smart_chg);
	oplus_mms_wait_topic("gauge", oplus_smart_chg_subscribe_gauge_topic, smart_chg);
	/* TODO: wait pps & wireless topic? */
}

static int oplus_smart_charge_parse_dt(struct oplus_smart_charge *smart_chg)
{
	struct device_node *node = smart_chg->dev->of_node;

	smart_chg->smart_chg_soh_support =
		of_property_read_bool(node, "oplus,smart_chg_soh_support");
	chg_info("oplus,smart_chg_soh_support is %d\n",
		smart_chg->smart_chg_soh_support);

	return 0;
}


static int oplus_smart_charge_probe(struct platform_device *pdev)
{
	struct oplus_smart_charge *smart_chg;

	smart_chg = devm_kzalloc(&pdev->dev, sizeof(struct oplus_smart_charge), GFP_KERNEL);
	if (smart_chg == NULL) {
		chg_err("alloc smart_chg buffer error\n");
		return 0;
	}
	smart_chg->dev = &pdev->dev;
	platform_set_drvdata(pdev, smart_chg);

	INIT_WORK(&smart_chg->quick_mode_check_work, oplus_smart_chg_quick_mode_check_work);
	oplus_mms_wait_topic("common", oplus_smart_chg_common_topic_ready, smart_chg);

	oplus_smart_charge_parse_dt(smart_chg);
	g_smart_chg = smart_chg;

	return 0;
}

static int oplus_smart_charge_remove(struct platform_device *pdev)
{
	struct oplus_smart_charge *smart_chg = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(smart_chg->gauge_subs))
		oplus_mms_unsubscribe(smart_chg->gauge_subs);
	if (!IS_ERR_OR_NULL(smart_chg->vooc_subs))
		oplus_mms_unsubscribe(smart_chg->vooc_subs);
	if (!IS_ERR_OR_NULL(smart_chg->wired_subs))
		oplus_mms_unsubscribe(smart_chg->wired_subs);
	devm_kfree(&pdev->dev, smart_chg);

	return 0;
}

static const struct of_device_id oplus_smart_charge_match[] = {
	{ .compatible = "oplus,smart_charge" },
	{},
};

static struct platform_driver oplus_smart_charge_driver = {
	.driver		= {
		.name = "oplus-smart_charge",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_smart_charge_match),
	},
	.probe		= oplus_smart_charge_probe,
	.remove		= oplus_smart_charge_remove,
};

/* never return an error value */
static __init int oplus_smart_charge_init(void)
{
	return platform_driver_register(&oplus_smart_charge_driver);
}

static __exit void oplus_smart_charge_exit(void)
{
	platform_driver_unregister(&oplus_smart_charge_driver);
}

oplus_chg_module_register(oplus_smart_charge);

/* smart charge module api */
int oplus_smart_chg_set_normal_current(int curr)
{
	bool led_on = false;
	union mms_msg_data data = { 0 };
	int rc;

	if (g_smart_chg == NULL)
		return -ENODEV;

	if (g_smart_chg->comm_topic) {
		oplus_mms_get_item_data(g_smart_chg->comm_topic, COMM_ITEM_LED_ON, &data, false);
		led_on = !!data.intval;
	}
	if (led_on)
		return 0;
	if (g_smart_chg->smart_normal_cool_down != 0)
		return 0;

	/* TODO: PPS */
	if (g_smart_chg->vooc_topic) {
		rc = oplus_vooc_current_to_level(g_smart_chg->vooc_topic, curr);
		if (rc < 0) {
			chg_err("can't get normal_cool_down, curr=%d, rc=%d\n",
				curr, rc);
			return rc;
		}
		g_smart_chg->normal_cool_down = rc;
	}
	chg_info("set normal_cool_down=%d\n", g_smart_chg->normal_cool_down);

	return 0;
}

int oplus_smart_chg_set_normal_cool_down(int cool_down)
{
	if (g_smart_chg == NULL)
		return -ENODEV;

	g_smart_chg->smart_normal_cool_down = cool_down;
	g_smart_chg->normal_cool_down = cool_down;
	chg_info("set normal_cool_down=%d\n", cool_down);

	return 0;
}

long oplus_smart_chg_get_quick_mode_time_gain(void)
{
	long total_time, gain_time;

	if (g_smart_chg == NULL)
		return -ENODEV;

	total_time = g_smart_chg->quick_mode_time.tv_sec - g_smart_chg->start_time;
	gain_time = g_smart_chg->quick_mode_gain_time_ms / 1000;
	if (gain_time < 0)
		gain_time = 0;
	chg_info("total_time:%ld, gain_time:%ld, quick_mode_gain_time_ms:%ld\n",
		 total_time, gain_time, g_smart_chg->quick_mode_gain_time_ms);

	return gain_time;
}

int oplus_smart_chg_get_quick_mode_percent_gain(void)
{
	int percent;
	long total_time, gain_time;

	if (g_smart_chg == NULL)
		return -ENODEV;

	total_time = g_smart_chg->quick_mode_time.tv_sec - g_smart_chg->start_time;
	gain_time = g_smart_chg->quick_mode_gain_time_ms / 1000;
	percent = (gain_time * 100) / (total_time + gain_time);
	chg_info("total_time:%ld, gain_time:%ld, percent:%d\n",
		 total_time, gain_time, percent);

	return percent;
}

int oplus_smart_chg_get_soh_support(void)
{
	if (g_smart_chg == NULL)
		return -ENODEV;

	return g_smart_chg->smart_chg_soh_support;
}
