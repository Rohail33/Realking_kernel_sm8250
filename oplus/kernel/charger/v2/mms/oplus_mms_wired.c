#define pr_fmt(fmt) "[MMS_WIRED]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
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
#include <linux/kthread.h>
#include <linux/usb/typec.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_ic.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_comm.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_monitor.h>
#include <oplus_mms_wired.h>

#include "../charger_ic/op_charge.h"

enum oplus_usbtemp_timer_stage {
	OPLUS_USBTEMP_TIMER_STAGE0 = 0,
	OPLUS_USBTEMP_TIMER_STAGE1,
};

struct oplus_usbtemp_spec_config {
	int usbtemp_batt_temp_low;
	int usbtemp_batt_temp_high;
	int usbtemp_ntc_temp_low;
	int usbtemp_ntc_temp_high;
	int usbtemp_temp_gap_low_with_batt_temp;
	int usbtemp_temp_gap_high_with_batt_temp;
	int usbtemp_temp_gap_low_without_batt_temp;
	int usbtemp_temp_gap_high_without_batt_temp;
	int usbtemp_rise_fast_temp_low;
	int usbtemp_rise_fast_temp_high;
	int usbtemp_rise_fast_temp_count_low;
	int usbtemp_rise_fast_temp_count_high;

	int usbtemp_cool_down_ntc_low;
	int usbtemp_cool_down_ntc_high;
	int usbtemp_cool_down_gap_low;
	int usbtemp_cool_down_gap_high;
	int usbtemp_cool_down_recover_ntc_low;
	int usbtemp_cool_down_recover_ntc_high;
	int usbtemp_cool_down_recover_gap_low;
	int usbtemp_cool_down_recover_gap_high;

	int usbtemp_batttemp_gap;
	int usbtemp_batttemp_recover_gap;
	int usbtemp_batttemp_current_gap;
	bool usbtemp_change_gap;
	int usbtemp_max_temp_diff;
	int usbtemp_cool_down_temp_gap;
	int usbtemp_cool_down_temp_first_gap;
	int usbtemp_cool_down_temp_second_gap;
	int usbtemp_cool_down_recovery_temp_gap;
	int usbtemp_cool_down_recovery_temp_first_gap;
	int usbtemp_cool_down_recovery_temp_second_gap;
	int usbtemp_max_temp_thr;
	int usbtemp_temp_up_time_thr;
};

struct oplus_mms_wired {
	struct device *dev;
	struct oplus_chg_ic_dev *buck_ic;
	struct oplus_chg_ic_dev *voocphy_ic;
	struct oplus_mms *wired_topic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *vooc_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *err_topic;
	struct mms_subscribe *gauge_subs;
	struct mms_subscribe *vooc_subs;
	struct mms_subscribe *comm_subs;

	struct votable *chg_icl_votable;
	struct votable *chg_suspend_votable;
	struct votable *chg_disable_votable;
	struct votable *cool_down_votable;
	struct votable *pd_svooc_votable;
	struct votable *vooc_chg_auto_mode_votable;

	struct delayed_work mms_wired_init_work;
	struct delayed_work usbtemp_recover_work;
	struct delayed_work ccdetect_work;
	struct delayed_work typec_state_change_work;
	struct delayed_work typec_state_notify_work;
	struct delayed_work svid_handler_work;
	struct work_struct err_handler_work;
	struct work_struct plugin_handler_work;
	struct work_struct chg_type_change_handler_work;
	struct work_struct gauge_update_work;
	struct work_struct otg_enable_handler_work;
	struct work_struct voltage_change_work;
	struct work_struct current_change_work;
	struct work_struct bc12_completed_work;
	struct work_struct data_role_changed_handler_work;
	struct work_struct back_ui_soc_work;

	struct wakeup_source *usbtemp_wakelock;
	struct adc_vol_temp_info *adc_vol_temp_info;
	struct task_struct *oplus_usbtemp_kthread;
	wait_queue_head_t oplus_usbtemp_wq;
	struct alarm usbtemp_alarm_timer;
	struct work_struct usbtemp_restart_work;
	struct oplus_usbtemp_spec_config usbtemp_spec;

	int vbat_mv;
	int batt_temp;
	int batt_curr;
	int shell_temp;
	int usbtemp_volt_l;
	int usbtemp_volt_r;
	int usb_temp_l;
	int usb_temp_r;
	int smart_charge_user;

	enum oplus_usbtemp_timer_stage usbtemp_timer_stage;
	bool support_usbtemp_protect_v2;
	bool new_usbtemp_cool_down_support;
	int usbtemp_curr_status;
	int usbtemp_batt_current;
	int usbtemp_pre_batt_current;

	bool wired_present;
	bool wired_online;
	bool vooc_started;
	bool vooc_online;
	bool vooc_charging;
	bool vooc_online_keep;
	bool ship_mode;
	bool dischg_flag;
	bool usbtemp_check;
	bool otg_enable;
	bool charge_suspend;
	bool charging_disable;
	bool bc12_completed;
	bool cc_detect_support;
	unsigned int vooc_sid;
	unsigned int usb_status;
	bool abnormal_adapter;
	int ui_soc;
	struct mutex fcc_lock;

	int bcc_current;
	int bcc_cool_down;
	struct mutex bcc_curr_done_mutex;
	int bcc_curr_done;
};

static struct oplus_mms_wired *g_mms_wired;

#define USBTEMP_CHARGING_CURRENT_LIMIT	3000
#define USBTEMP_CURR_TABLE_MAX		5
#define OPLUS_USBTEMP_HIGH_CURR		1
#define OPLUS_USBTEMP_LOW_CURR		0
#define OPLUS_USBTEMP_CURR_CHANGE_TEMP	3
#define OPLUS_USBTEMP_CHANGE_RANGE_TIME	30

#define OPCHG_USBTEMP_BATT_TEMP_LOW			50
#define OPCHG_USBTEMP_BATT_TEMP_HIGH			50
#define OPCHG_USBTEMP_NTC_TEMP_LOW			57
#define OPCHG_USBTEMP_NTC_TEMP_HIGH			69
#define OPCHG_USBTEMP_GAP_LOW_WITH_BATT_TEMP		7
#define OPCHG_USBTEMP_GAP_HIGH_WITH_BATT_TEMP		12
#define OPCHG_USBTEMP_GAP_LOW_WITHOUT_BATT_TEMP		12
#define OPCHG_USBTEMP_GAP_HIGH_WITHOUT_BATT_TEMP	24
#define OPCHG_USBTEMP_RISE_FAST_TEMP_LOW		3
#define OPCHG_USBTEMP_RISE_FAST_TEMP_HIGH		3
#define OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_LOW		30
#define OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_HIGH		20

#define OPCHG_USBTEMP_COOL_DOWN_NTC_LOW			54
#define OPCHG_USBTEMP_COOL_DOWN_NTC_HIGH		65
#define OPCHG_USBTEMP_COOL_DOWN_GAP_LOW			12
#define OPCHG_USBTEMP_COOL_DOWN_GAP_HIGH		20
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_LOW		48
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_HIGH	60
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_LOW		6
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_HIGH	15

#define NORMAL_CURRENT_MA		2000
#define USBTEMP_RECOVER_INTERVAL	(14400 * 1000) /*4 hours*/
#define USBTEMP_CC_RECOVER_INTERVAL	(300 * 1000) /*5 mins*/
static int usbtemp_recover_interval = USBTEMP_RECOVER_INTERVAL;
module_param(usbtemp_recover_interval, int, 0644);

static int usbtemp_recover_test = 0;
module_param(usbtemp_recover_test, int, 0644);

static int usbtemp_debug = 0;
module_param(usbtemp_debug, int, 0644);
#define OPEN_LOG_BIT BIT(0)
#define TEST_FUNC_BIT BIT(1)
#define TEST_CURRENT_BIT BIT(2)
MODULE_PARM_DESC(usbtemp_debug, "debug usbtemp");

static int usbtemp_dbg_tempr = 0;
module_param(usbtemp_dbg_tempr, int, 0644);
MODULE_PARM_DESC(usbtemp_dbg_tempr, "debug usbtemp temp r");

static int usbtemp_dbg_templ = 0;
module_param(usbtemp_dbg_templ, int, 0644);
MODULE_PARM_DESC(usbtemp_dbg_templ, "debug usbtemp temp l");

static int usbtemp_dbg_curr_status = -1;
module_param(usbtemp_dbg_curr_status, int, 0644);
MODULE_PARM_DESC(usbtemp_dbg_curr_status, "debug usbtemp current status");

__maybe_unused static voltage_max_table_mv[OPLUS_CHG_USB_TYPE_MAX] = {
	5000,	/* OPLUS_CHG_USB_TYPE_UNKNOWN */
	5000,	/* OPLUS_CHG_USB_TYPE_SDP */
	5000,	/* OPLUS_CHG_USB_TYPE_DCP */
	5000,	/* OPLUS_CHG_USB_TYPE_CDP */
	5000,	/* OPLUS_CHG_USB_TYPE_ACA */
	5000,	/* OPLUS_CHG_USB_TYPE_C */
	9000,	/* OPLUS_CHG_USB_TYPE_PD */
	9000,	/* OPLUS_CHG_USB_TYPE_PD_DRP */
	9000,	/* OPLUS_CHG_USB_TYPE_PD_PPS */
	5000,	/* OPLUS_CHG_USB_TYPE_PD_SDP */
	5000,	/* OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID */
	9000,	/* OPLUS_CHG_USB_TYPE_QC2 */
	9000,	/* OPLUS_CHG_USB_TYPE_QC3 */
	5000,	/* OPLUS_CHG_USB_TYPE_VOOC */
	10000,	/* OPLUS_CHG_USB_TYPE_SVOOC */
	10000,	/* OPLUS_CHG_USB_TYPE_UFCS TODO:power */
};

__maybe_unused static bool
is_chg_icl_votable_available(struct oplus_mms_wired *chip)
{
	if (!chip->chg_icl_votable)
		chip->chg_icl_votable = find_votable("WIRED_ICL");
	return !!chip->chg_icl_votable;
}

__maybe_unused static bool
is_chg_suspend_votable_available(struct oplus_mms_wired *chip)
{
	if (!chip->chg_suspend_votable)
		chip->chg_suspend_votable = find_votable("WIRED_CHARGE_SUSPEND");
	return !!chip->chg_suspend_votable;
}

__maybe_unused static bool
is_chg_disable_votable_available(struct oplus_mms_wired *chip)
{
	if (!chip->chg_disable_votable)
		chip->chg_disable_votable = find_votable("WIRED_CHARGING_DISABLE");
	return !!chip->chg_disable_votable;
}

__maybe_unused static bool
is_cool_down_votable_available(struct oplus_mms_wired *chip)
{
	if (!chip->cool_down_votable)
		chip->cool_down_votable = find_votable("COOL_DOWN");
	return !!chip->cool_down_votable;
}

__maybe_unused static bool
is_pd_svooc_votable_available(struct oplus_mms_wired *chip)
{
	if (!chip->pd_svooc_votable)
		chip->pd_svooc_votable =
			find_votable("PD_SVOOC");
	return !!chip->pd_svooc_votable;
}

__maybe_unused static bool
is_err_topic_available(struct oplus_mms_wired *chip)
{
	if (!chip->err_topic)
		chip->err_topic = oplus_mms_get_by_name("error");
	return !!chip->err_topic;
}

__maybe_unused static bool
is_vooc_chg_auto_mode_votable_available(struct oplus_mms_wired *chip)
{
	if (!chip->vooc_chg_auto_mode_votable)
		chip->vooc_chg_auto_mode_votable =
			find_votable("VOOC_CHG_AUTO_MODE");
	return !!chip->vooc_chg_auto_mode_votable;
}

int oplus_wired_get_charger_cycle(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int cycle = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
				OPLUS_IC_FUNC_GET_CHARGER_CYCLE,
				&cycle);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err("error: get charger cycle, rc=%d\n", rc);
	} else {
		chg_info("charger_cycle = %d\n", cycle);
	}

	return cycle;
}

void oplus_wired_dump_regs(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_REG_DUMP);
	if (rc < 0)
		chg_err("can't dump wired reg, rc=%d\n", rc);
}

int oplus_wired_get_vbus(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int vbus_mv;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
				OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL,
				&vbus_mv);
	if (rc < 0) {
		chg_err("can't get vbus, rc=%d\n", rc);
		return rc;
	}

	return vbus_mv;
}

bool oplus_wired_get_vbus_collapse_status(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	bool collapse;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return false;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_VBUS_COLLAPSE_STATUS,
			       &collapse);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err("can't get wired vbus collapse status, rc=%d\n",
				rc);
		return false;
	}

	return collapse;
}

int oplus_wired_set_icl(int icl_ma, bool step)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	if (chip->charge_suspend) {
		chg_info("charge suspend, set icl to 0\n");
		icl_ma = 0;
		step = false;
	}

	if ((get_eng_version() == HIGH_TEMP_AGING || get_eng_version() == AGING) &&
			(oplus_wired_get_chg_type() != OPLUS_CHG_USB_TYPE_CDP &&
			 oplus_wired_get_chg_type() != OPLUS_CHG_USB_TYPE_SDP)) {
		icl_ma = NORMAL_CURRENT_MA;
		chg_err("HIGH_TEMP_AGING/AGING, set input_limit 2A\n");
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_SET_ICL,
			       false, step, icl_ma);
	if (rc < 0)
		chg_err("set icl to %d error, rc=%d\n", icl_ma, rc);
	else
		chg_info("set icl to %d, step=%s\n", icl_ma,
			 step ? "true" : "false");

	return rc;
}

int oplus_wired_set_icl_by_vooc(struct oplus_mms *topic, int icl_ma)
{
	struct oplus_mms_wired *chip;
	int rc;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return -ENODEV;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (chip->charge_suspend) {
		chg_info("charge suspend, set icl to 0\n");
		oplus_wired_set_icl(0, false);
		return 0;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_SET_ICL,
			       true, false, icl_ma);
	if (rc)
		chg_err("set icl to %d mA fail, rc=%d\n", icl_ma, rc);
	else
		chg_info("set icl to %d mA\n", icl_ma);

	return rc;
}

int oplus_wired_set_fcc(int fcc_ma)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	mutex_lock(&chip->fcc_lock);
	if (READ_ONCE(chip->charging_disable)) {
		chg_info("charging disable\n");
		fcc_ma = 0;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
				OPLUS_IC_FUNC_BUCK_SET_FCC,
				fcc_ma);
	if (rc < 0)
		chg_err("set fcc to %d error, rc=%d\n", fcc_ma, rc);
	else
		chg_info("set fcc to %d\n", fcc_ma);
	mutex_unlock(&chip->fcc_lock);

	return rc;
}

int oplus_wired_smt_test(char buf[], int len)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_SMT_TEST, buf, len);

	return rc;
}

bool oplus_wired_is_present(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	bool present;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return false;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_INPUT_PRESENT,
			       &present);
	if (rc < 0) {
		chg_err("can't get wired present status, rc=%d\n", rc);
		return false;
	}

	return present;
}

static int oplus_wired_hardware_init(struct oplus_mms_wired *chip)
{
	int rc = 0;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_HARDWARE_INIT);
	if (rc < 0)
		chg_err("can't hardware_init, rc=%d\n", rc);
	chg_info("hardware_init\n");

	return rc;
}

int oplus_wired_input_enable(bool enable)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND,
			       !enable);
	if (rc < 0)
		chg_err("can't %s wired input, rc=%d\n",
			enable ? "enable" : "disable", rc);
	else
		chg_info("%s wired input\n", enable ? "enable" : "disable");

	chip->charge_suspend = !enable;

	return rc;
}

bool oplus_wired_input_is_enable(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	bool enable;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return false;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND,
			       &enable);
	if (rc < 0) {
		chg_err("can't get wired input enable status, rc=%d\n", rc);
		return rc;
	}

	return !enable;
}

int oplus_wired_output_enable(bool enable)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	static bool hw_init = false;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	if(enable && !hw_init) {
		hw_init = true;
		oplus_wired_hardware_init(chip);
	} else {
		hw_init = enable;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND,
			       !enable);
	if (rc < 0)
		chg_err("can't %s wired output, rc=%d\n",
			enable ? "enable" : "disable", rc);
	else
		chg_info("%s wired output\n", enable ? "enable" : "disable");

	chip->charging_disable = !enable;

	return rc;
}

bool oplus_wired_output_is_enable(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	bool enable;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return false;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND,
			       &enable);
	if (rc < 0) {
		chg_err("can't get wired output enable status, rc=%d\n", rc);
		return false;
	}

	return !enable;
}

int oplus_wired_get_icl(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int icl_ma;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_ICL,
			       &icl_ma);
	if (rc < 0) {
		chg_err("can't get wired icl, rc=%d\n", rc);
		return rc;
	}

	return icl_ma;
}

int oplus_wired_set_fv(int fv_mv)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
				OPLUS_IC_FUNC_BUCK_SET_FV,
				fv_mv);
	if (rc < 0)
		chg_err("set fv to %d error, rc=%d\n", fv_mv, rc);
	else
		chg_info("set fv to %d\n", fv_mv);

	return rc;
}

int oplus_wired_set_iterm(int iterm_ma)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
				OPLUS_IC_FUNC_BUCK_SET_ITERM,
				iterm_ma);
	if (rc < 0)
		chg_err("set iterm to %d error, rc=%d\n", iterm_ma, rc);
	else
		chg_info("set iterm to %d\n", iterm_ma);

	return rc;
}

int oplus_wired_set_rechg_vol(int vol_mv)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
				OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL,
				vol_mv);
	if (rc < 0)
		chg_err("set rechg vol to %d error, rc=%d\n", vol_mv, rc);
	else
		chg_info("set rechg vol to %d\n", vol_mv);

	return rc;
}

int oplus_wired_get_ibus(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int ibus_ma;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR,
			       &ibus_ma);
	if (rc < 0) {
		chg_err("can't get wired ibus, rc=%d\n", rc);
		return rc;
	}

	return ibus_ma;
}

int oplus_wired_get_chg_type(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int type;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return OPLUS_CHG_USB_TYPE_UNKNOWN;
	}

	if (chip->wired_present) {
		rc = oplus_chg_ic_func(chip->buck_ic,
				       OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE,
				       &type);
		if (rc < 0) {
			chg_err("can't get wired chg type, rc=%d\n", rc);
			type = OPLUS_CHG_USB_TYPE_UNKNOWN;
		}
	} else {
		type = OPLUS_CHG_USB_TYPE_UNKNOWN;
	}

	if (chip->wired_online) {
		if (sid_to_adapter_chg_type(chip->vooc_sid) == CHARGER_TYPE_VOOC)
			type = OPLUS_CHG_USB_TYPE_VOOC;
		else if (sid_to_adapter_chg_type(chip->vooc_sid) == CHARGER_TYPE_SVOOC)
			type = OPLUS_CHG_USB_TYPE_SVOOC;
	}

	return type;
}

int oplus_wired_otg_boost_enable(bool enable)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_OTG_BOOST_ENABLE,
			       enable);
	if (rc < 0) {
		chg_err("can't %s otg boost, rc=%d\n",
			enable ? "enable" : "disable", rc);
		return rc;
	}

	if (is_vooc_chg_auto_mode_votable_available(chip))
		vote(chip->vooc_chg_auto_mode_votable, OTG_ONLINE_VOTER,
		     enable, enable, false);
	else
		chg_err("vooc_chg_auto_mode_votable not found\n");

	chg_info("%s otg boost\n", enable ? "enable" : "disable");

	return rc;
}

int oplus_wired_set_otg_boost_vol(int vol_mv)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_SET_OTG_BOOST_VOL,
			       vol_mv);
	if (rc < 0)
		chg_err("can't set otg boost vol to %d, rc=%d\n", vol_mv, rc);
	chg_info("set otg boost vol to %d\n", vol_mv);

	return rc;
}

int oplus_wired_set_otg_boost_curr_limit(int curr_ma)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT,
			       curr_ma);
	if (rc < 0)
		chg_err("can't set otg boost curr limit to %d, rc=%d\n", curr_ma, rc);
	chg_info("set otg boost curr limit to %d\n", curr_ma);

	return rc;
}

int oplus_wired_aicl_enable(bool enable)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_AICL_ENABLE,
			       enable);
	if (rc < 0)
		chg_err("can't %s aicl, rc=%d\n",
			enable ? "enable" : "disable", rc);
	chg_info("%s aicl\n", enable ? "enable" : "disable");

	return rc;
}

int oplus_wired_aicl_rerun(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_AICL_RERUN);
	if (rc < 0)
		chg_err("can't rerun aicl, rc=%d\n", rc);
	chg_info("rerun aicl\n");

	return rc;
}

int oplus_wired_aicl_reset(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_AICL_RESET);
	if (rc < 0)
		chg_err("can't reset aicl, rc=%d\n", rc);
	chg_info("reset aicl\n");

	return rc;
}

int oplus_wired_get_cc_orientation(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int cc_orientation;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION,
			       &cc_orientation);
	if (rc < 0) {
		chg_err("can't get cc orientation, rc=%d\n", rc);
		return rc;
	}

	return cc_orientation;
}

int oplus_wired_get_hw_detect(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int hw_detect;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return 0;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_HW_DETECT,
			       &hw_detect);
	if (rc < 0) {
		chg_err("can't get hw detect status, rc=%d\n", rc);
		if (rc == -ENOTSUPP)
			return CC_DETECT_NULL;
		else
			return rc;
	}

	if (!!hw_detect)
		return CC_DETECT_PLUGIN;
	else
		return CC_DETECT_NOTPLUG;
}

int oplus_wired_rerun_bc12(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_RERUN_BC12);
	if (rc < 0)
		chg_err("can't rerun bc1.2, rc=%d\n", rc);
	chg_info("rerun bc1.2\n");

	return rc;
}

int oplus_wired_qc_detect_enable(bool enable)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_QC_DETECT_ENABLE,
			       enable);
	if (rc < 0)
		chg_err("can't %s qc detect, rc=%d\n",
			enable ? "enable" : "disable", rc);
	chg_info("%s qc detect\n", enable ? "enable" : "disable");

	return rc;
}

int oplus_wired_shipmode_enable(bool enable)
{
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	chip->ship_mode = enable;

	return 0;
}

static bool is_voocphy_ic_available(struct oplus_mms_wired *chip)
{
	if (!chip->voocphy_ic)
		chip->voocphy_ic = of_get_oplus_chg_ic(chip->dev->of_node,
						       "oplus,voocphy_ic", 0);

	return !!chip->voocphy_ic;
}

int oplus_wired_set_qc_config(enum oplus_chg_qc_version version, int vol_mv)
{
	int rc;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	if (is_voocphy_ic_available(chip)) {
		if (vol_mv > 5000) {
			rc = oplus_chg_ic_func(chip->voocphy_ic,
					       OPLUS_IC_FUNC_VOOCPHY_SET_PDQC_CONFIG);
			if (rc < 0) {
				chg_err("can't set pdqc config, rc=%d\n", rc);
				return rc;
			}
		}
	} else {
		chg_info("oplus,voocphy_ic not found\n");
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
				   OPLUS_IC_FUNC_BUCK_SET_QC_CONFIG,
				   version, vol_mv);
	if (rc < 0)
		chg_err("can't set qc to %dmV, rc=%d\n", vol_mv, rc);
	else
		chg_info("set qc to %dmV\n", vol_mv);

	return rc;
}

int oplus_wired_set_pd_config(u32 pdo)
{
	int rc;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	if (is_voocphy_ic_available(chip)) {
		if ((PD_SRC_PDO_TYPE(pdo) == PD_SRC_PDO_TYPE_FIXED)
		     && ((PD_SRC_PDO_FIXED_VOLTAGE(pdo) * 50) > 5000)) {
			rc = oplus_chg_ic_func(chip->voocphy_ic,
					       OPLUS_IC_FUNC_VOOCPHY_SET_PDQC_CONFIG);
			if (rc < 0) {
				chg_err("can't set pdqc config,rc=%d\n", rc);
				return rc;
			}
		}
	} else {
		chg_info("oplus,voocphy_ic not found\n");
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG,
			       pdo);
	if (rc < 0)
		chg_err("can't set pdo(=0x%08x), rc=%d\n", pdo, rc);
	chg_info("set pdo(=0x%08x)\n", pdo);

	return rc;
}

#define USB_TEMP_DEF	25
int oplus_wired_get_usb_temp_volt(int *vol_l, int *vol_r)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_GET_USB_TEMP_VOLT,
			       vol_l, vol_r);
	if (rc < 0)
		chg_err("can't get usb temp volt, rc=%d\n", rc);

	return rc;
}

int oplus_wired_get_usb_temp(int *temp_l, int *temp_r)
{
	int i;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	if (chip->adc_vol_temp_info == NULL)
		chip->adc_vol_temp_info = &adc_vol_temp_info_table[0];

	if (temp_l == NULL)
		goto next;

	for (i = chip->adc_vol_temp_info->con_volt_table_size - 1; i >= 0;
	     i--) {
		if (chip->adc_vol_temp_info->con_volt_table[i] >=
		    chip->usbtemp_volt_l)
			break;
		else if (i == 0)
			break;
	}

	*temp_l = chip->adc_vol_temp_info->con_temp_table[i];
	if(usbtemp_debug & OPEN_LOG_BIT)
		chg_err("usb_temp_l:%d,\n", *temp_l);

next:
	if (temp_r == NULL)
		return 0;

	for (i = chip->adc_vol_temp_info->con_volt_table_size - 1; i >= 0;
	     i--) {
		if (chip->adc_vol_temp_info->con_volt_table[i] >=
		    chip->usbtemp_volt_r)
			break;
		else if (i == 0)
			break;
	}

	*temp_r = chip->adc_vol_temp_info->con_temp_table[i];

	if(usbtemp_debug & OPEN_LOG_BIT)
		chg_err(" usb_temp_r:%d\n", *temp_r);

	return 0;
}

bool oplus_wired_usb_temp_check_is_support(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	bool support;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return false;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_USB_TEMP_CHECK_IS_SUPPORT,
			       &support);
	if (rc < 0) {
		chg_err("can't get usb temp check support status, rc=%d\n", rc);
		return false;
	}

	return support;
}

int oplus_wired_get_usb_btb_temp(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int usb_btb_temp = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_USB_BTB_TEMP,
			       &usb_btb_temp);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err("can't get usb btb temp, rc=%d\n", rc);
	}

	return usb_btb_temp;
}

int oplus_wired_get_batt_btb_temp(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	int batt_btb_temp = 0;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_BATT_BTB_TEMP,
			       &batt_btb_temp);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err("can't get batt btb temp, rc=%d\n", rc);
	}

	return batt_btb_temp;
}

enum oplus_chg_typec_port_role_type oplus_wired_get_typec_mode(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	enum oplus_chg_typec_port_role_type mode;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return TYPEC_PORT_ROLE_TRY_SNK;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_GET_TYPEC_MODE,
			       &mode);
	if (rc < 0) {
		chg_err("can't get typec mode, rc=%d\n", rc);
		return TYPEC_PORT_ROLE_TRY_SNK;
	}

	return mode;
}

int oplus_wired_set_typec_mode(enum oplus_chg_typec_port_role_type mode)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_SET_TYPEC_MODE,
			       mode);
	if (rc < 0)
		chg_err("can't set typec mode to %d, rc=%d\n", mode, rc);
	chg_info("set typec mode to %d, rc=%d\n", mode, rc);

	return rc;
}

int oplus_wired_set_otg_switch_status(bool en)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_SET_OTG_SWITCH_STATUS,
			       en);
	if (rc < 0)
		chg_err("can't %s otg switch, rc=%d\n",
			en ? "enable" : "disable", rc);
	chg_info("%s otg switch\n", en ? "enable" : "disable");

	return rc;
}

bool oplus_wired_get_otg_switch_status(void)
{
	int rc = 0;
	struct oplus_mms_wired *chip = g_mms_wired;
	bool en;

	if (chip == NULL) {
		chg_err("chip is NULL");
		return false;
	}

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_GET_OTG_SWITCH_STATUS,
			       &en);
	if (rc < 0) {
		chg_err("can't get otg switch status, rc=%d\n", rc);
		return false;
	}

	return en;
}

int oplus_wired_wdt_enable(struct oplus_mms *topic, bool enable)
{
	struct oplus_mms_wired *chip;
	int rc;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return -ENODEV;
	}
	chip = oplus_mms_get_drvdata(topic);

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_WDT_ENABLE,
			       enable);
	if (rc < 0) {
		chg_err("can't %s wdt, rc=%d\n", enable ? "enable" : "disable",
			rc);
		return rc;
	}

	return 0;
}

int oplus_wired_kick_wdt(struct oplus_mms *topic)
{
	struct oplus_mms_wired *chip;
	int rc;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return -ENODEV;
	}
	chip = oplus_mms_get_drvdata(topic);

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_KICK_WDT);
	if (rc < 0) {
		chg_err("can't kick wdt, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_wired_get_shutdown_soc(struct oplus_mms *topic)
{
	int rc = 0;
	struct oplus_mms_wired *chip;
	int soc;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return -ENODEV;
	}

	if ((get_eng_version() == HIGH_TEMP_AGING) ||
	    (get_eng_version() == AGING)) {
		chg_info("HIGH_TEMP_AGING or AGING no support backup soc\n");
		return -ENOTSUPP;
	}

	chip = oplus_mms_get_drvdata(topic);

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_GET_SHUTDOWN_SOC,
			       &soc);
	if (rc < 0) {
		chg_err("can't get shutdown soc, rc=%d\n", rc);
		return rc;
	}

	return soc;
}

int oplus_wired_backup_soc(struct oplus_mms *topic, int soc)
{
	int rc = 0;
	struct oplus_mms_wired *chip;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return -ENODEV;
	}

	if ((get_eng_version() == HIGH_TEMP_AGING) ||
	    (get_eng_version() == AGING)) {
		chg_info("HIGH_TEMP_AGING or AGING no support backup soc\n");
		return -ENOTSUPP;
	}

	chip = oplus_mms_get_drvdata(topic);

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BACKUP_SOC, soc);
	if (rc < 0)
		chg_err("can't backup soc, rc=%d\n", rc);

	return rc;
}

int oplus_wired_get_voltage_max(struct oplus_mms *topic)
{
	if (topic == NULL) {
		chg_err("topic is NULL");
		return 5000;
	}

	return voltage_max_table_mv[oplus_wired_get_chg_type()];
}

static bool oplus_usbtemp_check_is_support(struct oplus_mms_wired *chip)
{
	bool support;
	int rc;

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_USB_TEMP_CHECK_IS_SUPPORT,
			       &support);
	if (rc < 0)
		return false;
	return support;
}

static bool oplus_wired_ccdetect_is_support(void)
{
	int boot_mode = get_boot_mode();

	if (boot_mode == MSM_BOOT_MODE__RF ||
	    boot_mode == MSM_BOOT_MODE__WLAN ||
	    boot_mode == MSM_BOOT_MODE__FACTORY)
		return false;

	return true;
}

static int oplus_wired_ccdetect_enable(struct oplus_mms_wired *chip, bool en)
{
	int rc;

	if (!oplus_wired_ccdetect_is_support())
		return 0;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_SET_TYPEC_MODE,
		en ? TYPEC_PORT_ROLE_TRY_SNK : TYPEC_PORT_ROLE_SNK);
	if (rc < 0)
		chg_err("%s ccdetect error, rc=%d\n",
			en ? "enable" : "disable", rc);

	return rc;
}

int oplus_wired_get_otg_online_status(struct oplus_mms *topic)
{
	struct oplus_mms_wired *chip;
	int online;
	int rc = 0;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return 0;
	}
	chip = oplus_mms_get_drvdata(topic);

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_GET_OTG_ONLINE_STATUS, &online);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err("can't get otg online status, rc=%d\n", rc);
		return 0;
	}
	/* chip->otg_online = online; */

	return online;
}

int oplus_wired_set_usb_status(struct oplus_mms_wired *chip, unsigned int status)
{
	unsigned int usb_status;
	struct mms_msg *msg;
	int rc;

	usb_status = chip->usb_status | status;

	if (chip->usb_status == usb_status)
		return 0;
	chip->usb_status = usb_status;
	chg_info("usb_status=0x%08x\n", usb_status);
	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_USB_STATUS);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish usb status msg error, rc=%d\n", rc);
		kfree(msg);
		return rc;
	}

	return 0;
}

int oplus_wired_clear_usb_status(struct oplus_mms_wired *chip, unsigned int status)
{
	unsigned int usb_status;
	struct mms_msg *msg;
	int rc;

	usb_status = chip->usb_status & (~status);

	if (chip->usb_status == usb_status)
		return 0;
	chip->usb_status = usb_status;
	chg_info("usb_status=0x%08x\n", usb_status);
	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_USB_STATUS);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish usb status msg error, rc=%d\n", rc);
		kfree(msg);
		return rc;
	}

	return 0;
}

#define USB_20C 20
#define USB_40C 40
#define USB_30C 30
#define USB_50C 50
#define USB_55C 55
#define USB_57C 57
#define USB_100C 100
#define USB_50C_VOLT 467
#define USB_55C_VOLT 400
#define USB_57C_VOLT 376
#define USB_100C_VOLT 100
#define VBUS_VOLT_THRESHOLD 400
#define VBUS_MONITOR_INTERVAL 3000
#define MIN_MONITOR_INTERVAL 50
#define MAX_MONITOR_INTERVAL 50
#define RETRY_CNT_DELAY 5
#define HIGH_TEMP_SHORT_CHECK_TIMEOUT 1000

static void oplus_init_usbtemp_wakelock(struct oplus_mms_wired *chip)
{
	static bool is_awake_init = false;
	if (!is_awake_init) {
		chg_err(" init usbtemp wakelock.\n");
		chip->usbtemp_wakelock = wakeup_source_register(
			NULL, "usbtemp suspend wakelock");
		is_awake_init = true;
	}
	return;
}

static void oplus_set_usbtemp_wakelock(struct oplus_mms_wired *chip, bool value)
{
	static bool pm_flag = false;
	if (value && !pm_flag) {
		__pm_stay_awake(chip->usbtemp_wakelock);
		pm_flag = true;
	} else if (!value && pm_flag) {
		__pm_relax(chip->usbtemp_wakelock);
		pm_flag = false;
	}
}

static void oplus_usbtemp_recover_func(struct oplus_mms_wired *chip)
{
	int count_time = 0;
	int rc;

	oplus_set_usbtemp_wakelock(chip, false);

	if (chip->usb_status == USB_TEMP_HIGH) {
		oplus_set_usbtemp_wakelock(chip, true);
		do {
			oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l,
						      &chip->usbtemp_volt_r);
			oplus_wired_get_usb_temp(&chip->usb_temp_l,
						 &chip->usb_temp_r);
			msleep(2000);
			count_time++;
		} while (!(((chip->usb_temp_r < USB_55C ||
			     chip->usb_temp_r == USB_100C) &&
			    (chip->usb_temp_l < USB_55C ||
			     chip->usb_temp_l == USB_100C)) ||
			   count_time == 30));
		oplus_set_usbtemp_wakelock(chip, false);
		if (count_time == 30) {
			chg_err("[OPLUS_USBTEMP] temp still high");
		} else {
			chip->dischg_flag = false;
			oplus_wired_clear_usb_status(chip, USB_TEMP_HIGH);
			(void)oplus_chg_ic_func(
				chip->buck_ic,
				OPLUS_IC_FUNC_SET_USB_DISCHG_ENABLE, false);
			chg_err("[OPLUS_USBTEMP] usbtemp recover");
			if (is_chg_suspend_votable_available(chip))
				rc = vote(chip->chg_suspend_votable, USB_VOTER,
					  false, 0, false);
			else
				rc = -ENOTSUPP;
			if (rc < 0)
				chg_err("can't set charge unsuspend, rc=%d\n",
					rc);
		}
	}
	return;
}

static void oplus_usbtemp_recover_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_wired *chip = container_of(dwork,
		struct oplus_mms_wired, usbtemp_recover_work);

	oplus_usbtemp_recover_func(chip);
}

static void  oplus_usbtemp_restart_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip = container_of(
		work, struct oplus_mms_wired, usbtemp_restart_work);
	bool dischg;
	int rc;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_GET_USB_DISCHG_STATUS, &dischg);
	if (rc < 0) {
		chg_err("can't get usb dischg status, rc=%d\n", rc);
		goto done;
	}

	switch (chip->usbtemp_timer_stage) {
	case OPLUS_USBTEMP_TIMER_STAGE0:
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE1;
		chg_info("stage0, set try sink\n");
		rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_SET_TYPEC_MODE, TYPEC_PORT_ROLE_TRY_SNK);
		if (rc < 0)
			chg_err("can't set typec mode to try sink, rc=%d\n", rc);
		if (alarmtimer_get_rtcdev()) {
			if (usbtemp_recover_interval > USBTEMP_CC_RECOVER_INTERVAL)
				alarm_start_relative(&chip->usbtemp_alarm_timer, ms_to_ktime(usbtemp_recover_interval- USBTEMP_CC_RECOVER_INTERVAL));
			else
				alarm_start_relative(&chip->usbtemp_alarm_timer, ms_to_ktime(10000));
		} else {
			chg_err("Failed to get soc alarm-timer");
		}
		goto done;
	case OPLUS_USBTEMP_TIMER_STAGE1:
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE0;
		chg_info("stage1, set vbus_short output low\n");
		break;
	default:
		chg_info("stage%d not support\n", chip->usbtemp_timer_stage);
		goto done;
	}

	if (dischg) {
		chip->dischg_flag = false;
		chg_info("dischg disable...\n");
		oplus_wired_clear_usb_status(chip, USB_TEMP_HIGH);
		(void)oplus_chg_ic_func(
			chip->buck_ic, OPLUS_IC_FUNC_SET_USB_DISCHG_ENABLE, false);
		chg_info("[OPLUS_USBTEMP] usbtemp recover");
		if (is_chg_suspend_votable_available(chip))
			rc = vote(chip->chg_suspend_votable, USB_VOTER, false, 0, false);
		else
			rc = -ENOTSUPP;
		if (rc < 0)
			chg_err("can't set charge unsuspend, rc=%d\n", rc);
		chg_info("usbtemp recover");
	}

done:
	oplus_set_usbtemp_wakelock(chip, false);
}

static enum alarmtimer_restart oplus_usbtemp_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct oplus_mms_wired *chip = container_of(
		alarm, struct oplus_mms_wired, usbtemp_alarm_timer);

	chg_info("timer is up now.\n");
	oplus_set_usbtemp_wakelock(chip, true);
	schedule_work(&chip->usbtemp_restart_work);

	return ALARMTIMER_NORESTART;
}

static int oplus_usbtemp_dischg_action(struct oplus_mms_wired *chip)
{
	int rc = 0;
	struct oplus_mms *vooc_topic;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	if (get_eng_version() != HIGH_TEMP_AGING) {
		oplus_wired_set_usb_status(chip, USB_TEMP_HIGH);

		/*TODO*/
		if(chip->vooc_charging) {
			oplus_api_switch_normal_chg(vooc_topic);
			oplus_api_vooc_set_reset_sleep(vooc_topic);
		}
		usleep_range(10000, 10000);
		if (is_chg_suspend_votable_available(chip))
			rc = vote(chip->chg_suspend_votable, USB_VOTER,
				  true, 1, false);
		else
			rc = -ENOTSUPP;
		if (rc < 0)
			chg_err("can't set charge suspend, rc=%d\n", rc);
		usleep_range(5000, 5000);
		rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_SET_TYPEC_MODE,
				  TYPEC_PORT_ROLE_DISABLE);
		if (rc < 0)
			chg_err("can't open cc, rc=%d\n", rc);
		usleep_range(5000, 5000);
		chg_err("set vbus down");
		oplus_chg_ic_func(chip->buck_ic,
				  OPLUS_IC_FUNC_SET_USB_DISCHG_ENABLE, true);
		if(chip->vooc_charging)
			oplus_api_vooc_turn_off_fastchg(vooc_topic);
	} else {
		chg_err("CONFIG_HIGH_TEMP_VERSION enable here,do not set vbus down \n");
		oplus_chg_ic_func(chip->buck_ic,
				  OPLUS_IC_FUNC_SET_USB_DISCHG_ENABLE, false);
	}

	return 0;
}

static void oplus_usbtemp_set_recover(struct oplus_mms_wired *chip)
{
	if (alarmtimer_get_rtcdev()) {
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE0;
		alarm_start_relative(&chip->usbtemp_alarm_timer, ms_to_ktime(USBTEMP_CC_RECOVER_INTERVAL));
	} else {
		chg_err("Failed to get soc alarm-timer");
	}
}

#define RETRY_COUNT 3
static void oplus_update_usbtemp_current_status(struct oplus_mms_wired *chip)
{
	static int limit_cur_cnt_r = 0;
	static int limit_cur_cnt_l = 0;
	static int recover_cur_cnt_r = 0;
	static int recover_cur_cnt_l = 0;

	if ((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C) &&
	    (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		if (is_cool_down_votable_available(chip)) {
			vote(chip->cool_down_votable, USB_VOTER, false, 0,
			     false);
		}
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		return;
	}

	if ((chip->usb_temp_r - chip->batt_temp / 10) >= 12) {
		limit_cur_cnt_r++;
		if (limit_cur_cnt_r >= RETRY_COUNT) {
			limit_cur_cnt_r = RETRY_COUNT;
		}
		recover_cur_cnt_r = 0;
	} else if ((chip->usb_temp_r - chip->batt_temp / 10) <= 6) {
		recover_cur_cnt_r++;
		if (recover_cur_cnt_r >= RETRY_COUNT) {
			recover_cur_cnt_r = RETRY_COUNT;
		}
		limit_cur_cnt_r = 0;
	}

	if ((chip->usb_temp_l - chip->batt_temp / 10) >= 12) {
		limit_cur_cnt_l++;
		if (limit_cur_cnt_l >= RETRY_COUNT) {
			limit_cur_cnt_l = RETRY_COUNT;
		}
		recover_cur_cnt_l = 0;
	} else if ((chip->usb_temp_l - chip->batt_temp / 10) <= 6) {
		recover_cur_cnt_l++;
		if (recover_cur_cnt_l >= RETRY_COUNT) {
			recover_cur_cnt_l = RETRY_COUNT;
		}
		limit_cur_cnt_l = 0;
	}

	if ((RETRY_COUNT <= limit_cur_cnt_r ||
	     RETRY_COUNT <= limit_cur_cnt_l) &&
	    (chip->smart_charge_user == SMART_CHARGE_USER_OTHER)) {
		chip->smart_charge_user = SMART_CHARGE_USER_USBTEMP;
		if (is_cool_down_votable_available(chip)) {
			/* TODO: LCD on or off? */
			vote(chip->cool_down_votable, USB_VOTER, true, 3,
			     false);
		}
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
	} else if ((RETRY_COUNT <= recover_cur_cnt_r &&
		    RETRY_COUNT <= recover_cur_cnt_l) &&
		   (chip->smart_charge_user == SMART_CHARGE_USER_USBTEMP)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		if (is_cool_down_votable_available(chip)) {
			vote(chip->cool_down_votable, USB_VOTER, false, 0,
			     false);
		}
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
	}

	return;
}

#define USBTEMP_TRIGGER_CONDITION_1			1
#define USBTEMP_TRIGGER_CONDITION_2			2
#define USBTEMP_TRIGGER_CONDITION_COOL_DOWN		3
#define USBTEMP_TRIGGER_CONDITION_COOL_DOWN_RECOVERY	4
static int oplus_usbtemp_push_err_msg(struct oplus_mms_wired *chip,
				      int condition, int last_usb_temp_l,
				      int last_usb_temp_r, int batt_curr)
{
	struct mms_msg *msg;
	int rc;

	if (!is_err_topic_available(chip)) {
		chg_err("error topic not found\n");
		return -ENODEV;
	}

	if (condition == USBTEMP_TRIGGER_CONDITION_1) {
		msg = oplus_mms_alloc_str_msg(
			MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, ERR_ITEM_USBTEMP,
			"$$reason@@%s$$batt_temp@@%d"
			"$$usb_temp_l@@%d$$usb_temp_r@@%d",
			"first_condition", chip->batt_temp, chip->usb_temp_l,
			chip->usb_temp_r);
	} else if (condition == USBTEMP_TRIGGER_CONDITION_2) {
		msg = oplus_mms_alloc_str_msg(
			MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, ERR_ITEM_USBTEMP,
			"$$reason@@%s$$batt_temp@@%d"
			"$$usb_temp_l@@%d$$last_usb_temp_l@@%d"
			"$$usb_temp_r@@%d$$last_usb_temp_r@@%d",
			"second_condition", chip->batt_temp, chip->usb_temp_l,
			last_usb_temp_l, chip->usb_temp_r, last_usb_temp_r);
	} else if (condition == USBTEMP_TRIGGER_CONDITION_COOL_DOWN) {
		msg = oplus_mms_alloc_str_msg(
			MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, ERR_ITEM_USBTEMP,
			"$$reason@@%s$$batt_temp@@%d"
			"$$usb_temp_l@@%d$$usb_temp_r@@%d"
			"$$batt_current@@%d",
			"cool_down_condition", chip->batt_temp,
			chip->usb_temp_l, chip->usb_temp_r, batt_curr);
	} else if (condition == USBTEMP_TRIGGER_CONDITION_COOL_DOWN_RECOVERY) {
		msg = oplus_mms_alloc_str_msg(
			MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, ERR_ITEM_USBTEMP,
			"$$reason@@%s$$batt_temp@@%d"
			"$$usb_temp_l@@%d$$usb_temp_r@@%d"
			"$$batt_current@@%d",
			"cool_down_recovery_condition", chip->batt_temp,
			chip->usb_temp_l, chip->usb_temp_r, batt_curr);
	} else {
		chg_err("unknown condition\n");
		return -EINVAL;
	}
	if (msg == NULL) {
		chg_err("alloc usbtemp error msg error\n");
		return -ENOMEM;
	}

	/* track module also needs to collect some other information synchronously */
	rc = oplus_mms_publish_msg_sync(chip->err_topic, msg);
	if (rc < 0) {
		chg_err("publish usbtemp error msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static int oplus_usbtemp_monitor_main(void *data)
{
	int delay = 0;
	int vbus_volt = 0;
	static int count = 0;
	static int total_count = 0;
	static int last_usb_temp_l = 25;
	static int current_temp_l = 25;
	static int last_usb_temp_r = 25;
	static int current_temp_r = 25;
	int retry_cnt = 3, i = 0;
	int count_r = 1, count_l = 1;
	bool condition1 = false;
	bool condition2 = false;
	struct oplus_mms_wired *chip = data;
	static int log_count = 0;
	struct oplus_usbtemp_spec_config *spec = &chip->usbtemp_spec;

	chg_info("[oplus_usbtemp_monitor_main]:run first!");
	spec = &chip->usbtemp_spec;
	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->oplus_usbtemp_wq, chip->usbtemp_check);
		if (chip->dischg_flag) {
			goto dischg;
		}
		oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
		oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
		if ((chip->usb_temp_l < USB_50C) && (chip->usb_temp_r < USB_50C)) { /*get vbus when usbtemp < 50C*/
			vbus_volt = oplus_wired_get_vbus();
		} else {
			vbus_volt = 0;
		}
		if ((chip->usb_temp_l < USB_40C) && (chip->usb_temp_r < USB_40C)) {
			delay = MAX_MONITOR_INTERVAL;
			total_count = 10;
		} else {
			delay = MIN_MONITOR_INTERVAL;
			total_count = 30;
		}

		oplus_update_usbtemp_current_status(chip);

		if ((chip->usbtemp_volt_l < USB_50C) && (chip->usbtemp_volt_r < USB_50C) &&
		    (vbus_volt < VBUS_VOLT_THRESHOLD))
			delay = VBUS_MONITOR_INTERVAL;

		/*condition1  :the temp is higher than 57*/
		if (chip->batt_temp / 10 <= USB_50C &&
		    (((chip->usb_temp_l >= USB_57C) && (chip->usb_temp_l < USB_100C)) ||
		     ((chip->usb_temp_r >= USB_57C) && (chip->usb_temp_r < USB_100C)))) {
			chg_err("in loop 1");
			for (i = 1; i < retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
				oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
				if (chip->usb_temp_r >= USB_57C && chip->usb_temp_r < USB_100C)
					count_r++;
				if (chip->usb_temp_l >= USB_57C && chip->usb_temp_l < USB_100C)
					count_l++;
				chg_err("countl : %d", count_l);
			}
			if (count_r >= retry_cnt || count_l >= retry_cnt) {
				if (oplus_usbtemp_check_is_support(chip)) {
					chip->dischg_flag = true;
					condition1 = true;
					chg_err("dischg enable1...[%d, %d]\n", chip->usb_temp_l, chip->usb_temp_r);
				}
			}
			count_r = 1;
			count_l = 1;
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}
		if (chip->batt_temp / 10 > USB_50C &&
		    (((chip->usb_temp_l >= chip->batt_temp / 10 + 7) && (chip->usb_temp_l < USB_100C)) ||
		     ((chip->usb_temp_r >= chip->batt_temp / 10 + 7) && (chip->usb_temp_r < USB_100C)))) {
			chg_err("in loop 1");
			for (i = 1; i <= retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
				oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
				if ((chip->usb_temp_r >= chip->batt_temp / 10 + 7) && chip->usb_temp_r < USB_100C)
					count_r++;
				if ((chip->usb_temp_l >= chip->batt_temp / 10 + 7) && chip->usb_temp_l < USB_100C)
					count_l++;
				chg_err("countl : %d", count_l);
			}
			if (count_r >= retry_cnt || count_l >= retry_cnt) {
				if (oplus_usbtemp_check_is_support(chip)) {
					chip->dischg_flag = true;
					condition1 = true;
					chg_err("dischg enable1...[%d, %d]\n", chip->usb_temp_l, chip->usb_temp_r);
				}
			}
			count_r = 1;
			count_l = 1;
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}
		if (condition1) {
			chg_err("jump_to_dischg");
			goto dischg;
		}

		/*condition2  :the temp uprising to fast*/
		if ((((chip->usb_temp_l - chip->batt_temp / 10) > spec->usbtemp_batttemp_gap) &&
		     (chip->usb_temp_l < USB_100C)) ||
		    (((chip->usb_temp_r - chip->batt_temp / 10) > spec->usbtemp_batttemp_gap) &&
		     (chip->usb_temp_r < USB_100C))) {
			if (count == 0) {
				last_usb_temp_r = chip->usb_temp_r;
				last_usb_temp_l = chip->usb_temp_l;
			} else {
				current_temp_r = chip->usb_temp_r;
				current_temp_l = chip->usb_temp_l;
			}
			if (((current_temp_l - last_usb_temp_l) >= 3) || (current_temp_r - last_usb_temp_r) >= 3) {
				for (i = 1; i <= retry_cnt; i++) {
					mdelay(RETRY_CNT_DELAY);
					oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
					oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
					if ((chip->usb_temp_r - last_usb_temp_r) >= 3 && chip->usb_temp_r < USB_100C)
						count_r++;
					if ((chip->usb_temp_l - last_usb_temp_l) >= 3 && chip->usb_temp_l < USB_100C)
						count_l++;
					chg_err("countl : %d,countr : %d", count_l, count_r);
				}
				current_temp_l = chip->usb_temp_l;
				current_temp_r = chip->usb_temp_r;
				if ((count_l >= retry_cnt && chip->usb_temp_l > USB_30C &&
				     chip->usb_temp_l < USB_100C) ||
				    (count_r >= retry_cnt && chip->usb_temp_r > USB_30C &&
				     chip->usb_temp_r < USB_100C)) {
					if (oplus_usbtemp_check_is_support(chip)) {
						chip->dischg_flag = true;
						chg_err("dischg enable3...,current_temp_l=%d,last_usb_temp_l=%d,current_temp_r=%d,last_usb_temp_r =%d\n",
							current_temp_l, last_usb_temp_l, current_temp_r,
							last_usb_temp_r);
						condition2 = true;
					}
				}
				count_r = 1;
				count_l = 1;
			}
			count++;
			if (count > total_count)
				count = 0;
		} else {
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}
	/*judge whether to go the action*/
	dischg:
		if ((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C) &&
		    (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
			condition1 = false;
			condition2 = false;
			chip->dischg_flag = false;
		}

		if (usbtemp_recover_test || ((condition1 || condition2) && chip->dischg_flag)) {
			oplus_usbtemp_push_err_msg(
				chip,
				condition1 ? USBTEMP_TRIGGER_CONDITION_1 :
						   USBTEMP_TRIGGER_CONDITION_2,
				last_usb_temp_l, last_usb_temp_r, chip->batt_curr);
			if (!usbtemp_recover_test) {
				oplus_usbtemp_dischg_action(chip);
			}
			usbtemp_recover_test = 0;
			condition1 = false;
			condition2 = false;
			chg_info("start delay work for recover charging");
			oplus_init_usbtemp_wakelock(chip);
			oplus_set_usbtemp_wakelock(chip, true);
			cancel_delayed_work(&chip->usbtemp_recover_work);
			schedule_delayed_work(&chip->usbtemp_recover_work, msecs_to_jiffies(usbtemp_recover_interval));
		}
		msleep(delay);
		log_count++;
		/* about 1 minute */
		if (log_count == 960) {
			chg_info(
				"usbtemp_volt_l[%d], usb_temp_l[%d], usbtemp_volt_r[%d], usb_temp_r[%d]\n",
				chip->usbtemp_volt_l, chip->usb_temp_l,
				chip->usbtemp_volt_r, chip->usb_temp_r);
			log_count = 0;
		}
	}

	return 0;
}

static bool oplus_usbtemp_l_trigger_current_status(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_l >= spec->usbtemp_cool_down_ntc_low) ||
		    (chip->usb_temp_l - chip->batt_temp / 10) >= spec->usbtemp_cool_down_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_l >= spec->usbtemp_cool_down_ntc_high) ||
		    (chip->usb_temp_l - chip->batt_temp / 10) >= spec->usbtemp_cool_down_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

static bool oplus_usbtemp_l_recovery_current_status(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_l <= spec->usbtemp_cool_down_recover_ntc_low) &&
		    (chip->usb_temp_l - chip->batt_temp / 10) <= spec->usbtemp_cool_down_recover_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_l <= spec->usbtemp_cool_down_recover_ntc_high) &&
		    (chip->usb_temp_l - chip->batt_temp / 10) <= spec->usbtemp_cool_down_recover_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

static bool oplus_usbtemp_r_trigger_current_status(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_r >= spec->usbtemp_cool_down_ntc_low) ||
		    (chip->usb_temp_r - chip->batt_temp / 10) >= spec->usbtemp_cool_down_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_r >= spec->usbtemp_cool_down_ntc_high) ||
		    (chip->usb_temp_r - chip->batt_temp / 10) >= spec->usbtemp_cool_down_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

static bool oplus_usbtemp_r_recovery_current_status(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_r <= spec->usbtemp_cool_down_recover_ntc_low) &&
		    (chip->usb_temp_r - chip->batt_temp / 10) <= spec->usbtemp_cool_down_recover_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_r <= spec->usbtemp_cool_down_recover_ntc_high) &&
		    (chip->usb_temp_r - chip->batt_temp / 10) <= spec->usbtemp_cool_down_recover_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

#define RETRY_COUNT 3
static void oplus_update_usbtemp_current_status_new_method(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec = &chip->usbtemp_spec;
	static int limit_cur_cnt_r = 0;
	static int limit_cur_cnt_l = 0;
	static int recover_cur_cnt_r = 0;
	static int recover_cur_cnt_l = 0;
	int condition, batt_current;
	int last_usb_temp_l = 25;
	int last_usb_temp_r = 25;
	int cool_down;

	batt_current = chip->usbtemp_batt_current;
	if (oplus_vooc_get_project(chip->vooc_topic) == 0)
		return;

	if ((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C) &&
	    (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		if (is_cool_down_votable_available(chip))
			vote(chip->cool_down_votable, USB_VOTER, false, 0, false);
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		return;
	}

	if (chip->new_usbtemp_cool_down_support) {
		if (batt_current > 5000) {
			if (spec->usbtemp_cool_down_temp_gap == spec->usbtemp_cool_down_temp_first_gap) {
				limit_cur_cnt_r = 0;
				recover_cur_cnt_r = 0;
				limit_cur_cnt_l = 0;
				recover_cur_cnt_l = 0;
				chg_info("usbtemp_cool_down_temp_gap to %d", spec->usbtemp_cool_down_temp_second_gap);
			}
			spec->usbtemp_cool_down_temp_gap = spec->usbtemp_cool_down_temp_second_gap;
			spec->usbtemp_cool_down_recovery_temp_gap = spec->usbtemp_cool_down_recovery_temp_second_gap;
		} else {
			if (spec->usbtemp_cool_down_temp_gap == spec->usbtemp_cool_down_temp_second_gap) {
				limit_cur_cnt_r = 0;
				recover_cur_cnt_r = 0;
				limit_cur_cnt_l = 0;
				recover_cur_cnt_l = 0;
				chg_info("usbtemp_cool_down_temp_gap to %d", spec->usbtemp_cool_down_temp_first_gap);
			}
			spec->usbtemp_cool_down_temp_gap = spec->usbtemp_cool_down_temp_first_gap;
			spec->usbtemp_cool_down_recovery_temp_gap = spec->usbtemp_cool_down_recovery_temp_first_gap;
		}
	}

	if (oplus_usbtemp_r_trigger_current_status(chip)) {
		limit_cur_cnt_r++;
		recover_cur_cnt_r = 0;
	} else if (oplus_usbtemp_r_recovery_current_status(chip)) {
		recover_cur_cnt_r++;
		limit_cur_cnt_r = 0;
	}

	if (oplus_usbtemp_l_trigger_current_status(chip)) {
		limit_cur_cnt_l++;
		recover_cur_cnt_l = 0;
	} else if (oplus_usbtemp_l_recovery_current_status(chip)) {
		recover_cur_cnt_l++;
		limit_cur_cnt_l = 0;
	}

	if ((RETRY_COUNT <= limit_cur_cnt_r || RETRY_COUNT <= limit_cur_cnt_l) &&
	    (chip->smart_charge_user == SMART_CHARGE_USER_OTHER)) {
		chip->smart_charge_user = SMART_CHARGE_USER_USBTEMP;
		cool_down = oplus_vooc_current_to_level(chip->vooc_topic, USBTEMP_CHARGING_CURRENT_LIMIT);
		if (is_cool_down_votable_available(chip) && cool_down > 0)
			vote(chip->cool_down_votable, USB_VOTER, true, cool_down, false);
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		condition = USBTEMP_TRIGGER_CONDITION_COOL_DOWN;
		oplus_usbtemp_push_err_msg(chip, condition, last_usb_temp_l, last_usb_temp_r, batt_current);
	} else if ((RETRY_COUNT <= recover_cur_cnt_r && RETRY_COUNT <= recover_cur_cnt_l) &&
		   (chip->smart_charge_user == SMART_CHARGE_USER_USBTEMP)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		if (is_cool_down_votable_available(chip))
			vote(chip->cool_down_votable, USB_VOTER, false, 0, false);
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		condition = USBTEMP_TRIGGER_CONDITION_COOL_DOWN_RECOVERY;
		oplus_usbtemp_push_err_msg(chip, condition, last_usb_temp_l, last_usb_temp_r, batt_current);
	}

	return;
}

static bool oplus_usbtemp_condition_temp_high(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if (chip->batt_temp / 10 <= spec->usbtemp_batt_temp_low &&
		    (((chip->usb_temp_l >= spec->usbtemp_ntc_temp_low) && (chip->usb_temp_l < USB_100C)) ||
		     ((chip->usb_temp_r >= spec->usbtemp_ntc_temp_low) && (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if (chip->batt_temp / 10 <= spec->usbtemp_batt_temp_high &&
		    (((chip->usb_temp_l >= spec->usbtemp_ntc_temp_high) && (chip->usb_temp_l < USB_100C)) ||
		     ((chip->usb_temp_r >= spec->usbtemp_ntc_temp_high) && (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else {
		return false;
	}
}

static bool oplus_usbtemp_temp_rise_fast_with_batt_temp(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if (chip->batt_temp / 10 > spec->usbtemp_batt_temp_low &&
		    (((chip->usb_temp_l >= chip->batt_temp / 10 + spec->usbtemp_temp_gap_low_with_batt_temp) &&
		      (chip->usb_temp_l < USB_100C)) ||
		     ((chip->usb_temp_r >= chip->batt_temp / 10 + spec->usbtemp_temp_gap_low_with_batt_temp) &&
		      (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if (chip->batt_temp / 10 > spec->usbtemp_batt_temp_high &&
		    (((chip->usb_temp_l >= chip->batt_temp / 10 + spec->usbtemp_temp_gap_high_with_batt_temp) &&
		      (chip->usb_temp_l < USB_100C)) ||
		     ((chip->usb_temp_r >= chip->batt_temp / 10 + spec->usbtemp_temp_gap_high_with_batt_temp) &&
		      (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else {
		return false;
	}
}

static bool oplus_usbtemp_temp_rise_fast_without_batt_temp(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((((chip->usb_temp_l - chip->batt_temp / 10) >= spec->usbtemp_temp_gap_low_without_batt_temp) &&
		     (chip->usb_temp_l < USB_100C)) ||
		    (((chip->usb_temp_r - chip->batt_temp / 10) >= spec->usbtemp_temp_gap_low_without_batt_temp) &&
		     (chip->usb_temp_r < USB_100C)))
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((((chip->usb_temp_l - chip->batt_temp / 10) >= spec->usbtemp_temp_gap_high_without_batt_temp) &&
		     (chip->usb_temp_l < USB_100C)) ||
		    (((chip->usb_temp_r - chip->batt_temp / 10) >= spec->usbtemp_temp_gap_high_without_batt_temp) &&
		     (chip->usb_temp_r < USB_100C)))
			return true;
		return false;
	} else {
		return false;
	}
}

static bool oplus_usbtemp_judge_temp_gap(
	struct oplus_mms_wired *chip, int current_temp, int last_temp)
{
	struct oplus_usbtemp_spec_config *spec;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((current_temp - last_temp) >= spec->usbtemp_rise_fast_temp_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((current_temp - last_temp) >= spec->usbtemp_rise_fast_temp_high)
			return true;
		return false;
	} else {
		return false;
	}
}

static bool oplus_usbtemp_change_curr_range(
	struct oplus_mms_wired *chip, int retry_cnt,
	int usbtemp_first_time_in_curr_range, bool curr_range_change)
{
	static int last_curr_change_usb_temp_l = 25;
	static int current_curr_change_temp_l = 25;
	static int last_curr_change_usb_temp_r = 25;
	static int current_curr_change_temp_r = 25;
	int count_curr_r = 1, count_curr_l = 1;
	int i = 0;

	if (!chip)
		return false;

	chip->usbtemp_curr_status = OPLUS_USBTEMP_HIGH_CURR;
	if (usbtemp_first_time_in_curr_range == false) {
		last_curr_change_usb_temp_r = chip->usb_temp_r;
		last_curr_change_usb_temp_l = chip->usb_temp_l;
	} else {
		current_curr_change_temp_r = chip->usb_temp_r;
		current_curr_change_temp_l = chip->usb_temp_l;
	}
	if (((current_curr_change_temp_l - last_curr_change_usb_temp_l) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP) ||
	    (current_curr_change_temp_r - last_curr_change_usb_temp_r) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP) {
		for (i = 1; i <= retry_cnt; i++) {
			mdelay(RETRY_CNT_DELAY);
			oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
			oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
			if ((chip->usb_temp_r - last_curr_change_usb_temp_r) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP &&
			    chip->usb_temp_r < USB_100C)
				count_curr_r++;
			if ((chip->usb_temp_l - last_curr_change_usb_temp_l) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP &&
			    chip->usb_temp_l < USB_100C)
				count_curr_l++;
			chg_info("countl: %d, countr: %d", count_curr_l, count_curr_r);
		}
		current_curr_change_temp_l = chip->usb_temp_l;
		current_curr_change_temp_r = chip->usb_temp_r;

		if ((count_curr_l >= retry_cnt && chip->usb_temp_l > USB_30C && chip->usb_temp_l < USB_100C) ||
		    (count_curr_r >= retry_cnt && chip->usb_temp_r > USB_30C && chip->usb_temp_r < USB_100C)) {
			chg_info("change curr range...,current_temp_l=%d,last_usb_temp_l=%d,current_temp_r=%d,last_usb_temp_r =%d, chip->batt_temp = %d\n",
				current_curr_change_temp_l, last_curr_change_usb_temp_l, current_curr_change_temp_r,
				last_curr_change_usb_temp_r, chip->batt_temp);
			count_curr_r = 1;
			count_curr_l = 1;
			return true;
		}
	}

	if (curr_range_change == false || chip->usbtemp_curr_status != OPLUS_USBTEMP_LOW_CURR) {
		last_curr_change_usb_temp_r = chip->usb_temp_r;
		last_curr_change_usb_temp_l = chip->usb_temp_l;
	}

	return false;
}

static bool oplus_usbtemp_trigger_for_high_temp(
	struct oplus_mms_wired *chip, int retry_cnt, int count_r, int count_l)
{
	struct oplus_usbtemp_spec_config *spec;
	int i = 0;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (oplus_usbtemp_condition_temp_high(chip)) {
		chg_info("in usbtemp higher than 57 or 69!\n");
		for (i = 1; i < retry_cnt; i++) {
			mdelay(RETRY_CNT_DELAY);
			oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
			oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
			if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
				if (chip->usb_temp_r >= spec->usbtemp_ntc_temp_low && chip->usb_temp_r < USB_100C)
					count_r++;
				if (chip->usb_temp_l >= spec->usbtemp_ntc_temp_low && chip->usb_temp_l < USB_100C)
					count_l++;
			} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
				if (chip->usb_temp_r >= spec->usbtemp_ntc_temp_high && chip->usb_temp_r < USB_100C)
					count_r++;
				if (chip->usb_temp_l >= spec->usbtemp_ntc_temp_high && chip->usb_temp_l < USB_100C)
					count_l++;
			}
			chg_info("countl: %d countr: %d", count_l, count_r);
		}
	}
	if (count_r >= retry_cnt || count_l >= retry_cnt)
		return true;

	return false;
}

static bool oplus_usbtemp_trigger_for_rise_fast_temp(
	struct oplus_mms_wired *chip, int retry_cnt, int count_r, int count_l)
{
	struct oplus_usbtemp_spec_config *spec;
	int i = 0;

	if (!chip)
		return false;
	spec = &chip->usbtemp_spec;

	if (oplus_usbtemp_temp_rise_fast_with_batt_temp(chip)) {
		chg_info("in usbtemp rise fast with usbtemp!\n");
		for (i = 1; i <= retry_cnt; i++) {
			mdelay(RETRY_CNT_DELAY);
			oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
			oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
			if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
				if ((chip->usb_temp_r >=
				     chip->batt_temp / 10 + spec->usbtemp_temp_gap_low_with_batt_temp) &&
				    chip->usb_temp_r < USB_100C)
					count_r++;
				if ((chip->usb_temp_l >=
				     chip->batt_temp / 10 + spec->usbtemp_temp_gap_low_with_batt_temp) &&
				    chip->usb_temp_l < USB_100C)
					count_l++;
			} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
				if ((chip->usb_temp_r >=
				     chip->batt_temp / 10 + spec->usbtemp_temp_gap_high_with_batt_temp) &&
				    chip->usb_temp_r < USB_100C)
					count_r++;
				if ((chip->usb_temp_l >=
				     chip->batt_temp / 10 + spec->usbtemp_temp_gap_high_with_batt_temp) &&
				    chip->usb_temp_l < USB_100C)
					count_l++;
			}
			chg_info("countl: %d countr: %d", count_l, count_r);
		}
		if (count_r >= retry_cnt || count_l >= retry_cnt)
			return true;
	}

	return false;
}

static bool oplus_usbtemp_trigger_for_rise_fast_without_temp(
	struct oplus_mms_wired *chip, int retry_cnt, int count_r, int count_l, int total_count)
{
	static int count = 0;
	static int last_usb_temp_l = 25;
	static int current_temp_l = 25;
	static int last_usb_temp_r = 25;
	static int current_temp_r = 25;
	int i = 0;

	if (!chip)
		return false;

	if (oplus_usbtemp_temp_rise_fast_without_batt_temp(chip)) {
		if (count == 0) {
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
			current_temp_r = chip->usb_temp_r;
			current_temp_l = chip->usb_temp_l;
		} else {
			current_temp_r = chip->usb_temp_r;
			current_temp_l = chip->usb_temp_l;
		}
		if (oplus_usbtemp_judge_temp_gap(chip, current_temp_l, last_usb_temp_l) ||
		    oplus_usbtemp_judge_temp_gap(chip, current_temp_r, last_usb_temp_r)) {
			for (i = 1; i <= retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
				oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);
				current_temp_l = chip->usb_temp_l;
				current_temp_r = chip->usb_temp_r;
				if (oplus_usbtemp_judge_temp_gap(chip, current_temp_r, last_usb_temp_r) &&
				    chip->usb_temp_r < USB_100C)
					count_r++;
				if (oplus_usbtemp_judge_temp_gap(chip, current_temp_l, last_usb_temp_l) &&
				    chip->usb_temp_l < USB_100C)
					count_l++;
				chg_info("countl : %d,countr : %d", count_l, count_r);
			}
			current_temp_l = chip->usb_temp_l;
			current_temp_r = chip->usb_temp_r;
			if ((count_l >= retry_cnt && chip->usb_temp_l > USB_30C && chip->usb_temp_l < USB_100C) ||
			    (count_r >= retry_cnt && chip->usb_temp_r > USB_30C && chip->usb_temp_r < USB_100C)) {
				return true;
			}
			count_r = 1;
			count_l = 1;
		}
		count++;
		if (count > total_count)
			count = 0;
	} else {
		count = 0;
		last_usb_temp_r = chip->usb_temp_r;
		last_usb_temp_l = chip->usb_temp_l;
	}

	return false;
}

#define OPCHG_LOW_USBTEMP_RETRY_COUNT 10
#define OPLUS_CHG_CURRENT_READ_COUNT 15
int oplus_usbtemp_monitor_common_new_method(void *data)
{
	int delay = 0;
	int vbus_volt = 0;
	static int count = 0;
	static int total_count = 0;
	static int last_usb_temp_l = 25;
	static int last_usb_temp_r = 25;
	int retry_cnt = 3;
	int count_r = 1, count_l = 1;
	bool condition1 = false;
	bool condition2 = false;
	int condition;
	static bool curr_range_change = false;
	int batt_current = 0;
	struct timespec curr_range_change_first_time = (struct timespec){ 0 };
	struct timespec curr_range_change_last_time = (struct timespec){ 0 };
	bool usbtemp_first_time_in_curr_range = false;
	static current_read_count = 0;
	struct oplus_mms_wired *chip = (struct oplus_mms_wired *)data;
	struct oplus_usbtemp_spec_config *spec = &chip->usbtemp_spec;;

	if (alarmtimer_get_rtcdev()) {
		alarm_init(&chip->usbtemp_alarm_timer, ALARM_REALTIME, oplus_usbtemp_alarm_timer_func);
		INIT_WORK(&chip->usbtemp_restart_work, oplus_usbtemp_restart_work);
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE0;
	} else {
		chg_err("Failed to get soc alarm-timer");
	}

	if (spec->usbtemp_max_temp_thr <= 0)
		spec->usbtemp_max_temp_thr = USB_57C;
	if (spec->usbtemp_temp_up_time_thr <= 0)
		spec->usbtemp_temp_up_time_thr = 30;
	chg_info("run first, usbtemp_max_temp_thr[%d], usbtemp_temp_up_time_thr[%d]!",
		 spec->usbtemp_max_temp_thr, spec->usbtemp_temp_up_time_thr);

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->oplus_usbtemp_wq, chip->usbtemp_check);
		if (chip->dischg_flag)
			goto dischg;
		oplus_wired_get_usb_temp_volt(&chip->usbtemp_volt_l, &chip->usbtemp_volt_r);
		oplus_wired_get_usb_temp(&chip->usb_temp_l, &chip->usb_temp_r);

		if ((chip->usb_temp_l < USB_50C) && (chip->usb_temp_r < USB_50C)) /*get vbus when usbtemp < 50C*/
			vbus_volt = oplus_wired_get_vbus();
		else
			vbus_volt = 0;
		if ((chip->usb_temp_l < USB_40C) && (chip->usb_temp_r < USB_40C)) {
			delay = MAX_MONITOR_INTERVAL;
			total_count = 10;
		} else {
			delay = MIN_MONITOR_INTERVAL;
			total_count = spec->usbtemp_temp_up_time_thr;
		}

		current_read_count = current_read_count + 1;
		if (current_read_count == OPLUS_CHG_CURRENT_READ_COUNT) {
			/* TODO: parallel charge */
			chip->usbtemp_batt_current = -oplus_gauge_get_batt_current();
			current_read_count = 0;
		}
		oplus_update_usbtemp_current_status_new_method(chip);
		batt_current = chip->usbtemp_batt_current;

		if (usbtemp_dbg_curr_status < OPLUS_USBTEMP_LOW_CURR ||
		    usbtemp_dbg_curr_status > OPLUS_USBTEMP_HIGH_CURR) {
			if (batt_current > 5000)
				chip->usbtemp_curr_status = OPLUS_USBTEMP_HIGH_CURR;
			else if (batt_current > 0 && batt_current <= 5000)
				chip->usbtemp_curr_status = OPLUS_USBTEMP_LOW_CURR;
		} else if (usbtemp_dbg_curr_status == OPLUS_USBTEMP_LOW_CURR ||
			   usbtemp_dbg_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
			chip->usbtemp_curr_status = usbtemp_dbg_curr_status;
		}

		if (!curr_range_change && batt_current < 5000 &&
		    chip->usbtemp_pre_batt_current >= 5000) {
			curr_range_change = true;
			curr_range_change_first_time = oplus_current_kernel_time();
		} else if (curr_range_change && batt_current >= 5000 &&
			   chip->usbtemp_pre_batt_current < 5000) {
			curr_range_change = false;
		}

		if (curr_range_change && chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
			if (oplus_usbtemp_change_curr_range(chip, retry_cnt, usbtemp_first_time_in_curr_range,
							    curr_range_change)) {
				chip->usbtemp_curr_status = OPLUS_USBTEMP_LOW_CURR;
				curr_range_change = false;
			}
			if (!usbtemp_first_time_in_curr_range)
				usbtemp_first_time_in_curr_range = true;
			curr_range_change_last_time = oplus_current_kernel_time();
			if ((curr_range_change_last_time.tv_sec - curr_range_change_first_time.tv_sec) >=
			    OPLUS_USBTEMP_CHANGE_RANGE_TIME)
				chip->usbtemp_curr_status = OPLUS_USBTEMP_LOW_CURR;
		} else {
			usbtemp_first_time_in_curr_range = false;
		}

		if ((chip->usb_temp_l < USB_40C) && (chip->usb_temp_r < USB_40C))
			total_count = OPCHG_LOW_USBTEMP_RETRY_COUNT;
		else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR)
			total_count = spec->usbtemp_rise_fast_temp_count_low;
		else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR)
			total_count = spec->usbtemp_rise_fast_temp_count_high;

		if ((chip->usbtemp_volt_l < USB_50C) && (chip->usbtemp_volt_r < USB_50C) &&
		    (vbus_volt < VBUS_VOLT_THRESHOLD))
			delay = VBUS_MONITOR_INTERVAL;

		if (oplus_usbtemp_trigger_for_high_temp(chip, retry_cnt, count_r, count_l)) {
			if (oplus_usbtemp_check_is_support(chip)) {
				chip->dischg_flag = true;
				condition1 = true;
				chg_err("dischg enable1...[%d, %d]\n", chip->usb_temp_l, chip->usb_temp_r);
			}
			count_r = 1;
			count_l = 1;
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}

		if (oplus_usbtemp_trigger_for_rise_fast_temp(chip, retry_cnt, count_r, count_l)) {
			if (oplus_usbtemp_check_is_support(chip)) {
				chip->dischg_flag = true;
				condition1 = true;
				chg_err("dischg enable1...[%d, %d]\n", chip->usb_temp_l, chip->usb_temp_r);
			}
			count_r = 1;
			count_l = 1;
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
		}
		if (condition1) {
			chg_err("jump_to_dischg");
			goto dischg;
		}

		if (oplus_usbtemp_trigger_for_rise_fast_without_temp(chip, retry_cnt, count_r, count_l, total_count)) {
			if (oplus_usbtemp_check_is_support(chip)) {
				chip->dischg_flag = true;
				condition2 = true;
			}
		}

	dischg:
		if ((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C) &&
		    (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
			condition1 = false;
			condition2 = false;
			chip->dischg_flag = false;
		}

		if ((condition1 || condition2) && chip->dischg_flag) {
			condition = condition1 ? USBTEMP_TRIGGER_CONDITION_1 : USBTEMP_TRIGGER_CONDITION_2;
			oplus_usbtemp_push_err_msg(chip, condition, last_usb_temp_l,
						   last_usb_temp_r, batt_current);
			oplus_usbtemp_dischg_action(chip);
			condition1 = false;
			condition2 = false;
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
			if ((get_eng_version() != HIGH_TEMP_AGING && !oplus_is_ptcrb_version()) &&
#else
			if (
#endif
			    (!chip->cc_detect_support)) {
				chg_info("start delay work for recover charging");
				count = 0;
				last_usb_temp_r = chip->usb_temp_r;
				last_usb_temp_l = chip->usb_temp_l;
				oplus_usbtemp_set_recover(chip);
			}
		}
		msleep(delay);
		chip->usbtemp_pre_batt_current = batt_current;
		if (usbtemp_debug & OPEN_LOG_BIT) {
			chg_info("usbtemp: delay %d", delay);
			chg_info("usbtemp_volt_l[%d], usb_temp_l[%d], usbtemp_volt_r[%d], usb_temp_r[%d]\n",
				 chip->usbtemp_volt_l, chip->usb_temp_l, chip->usbtemp_volt_r, chip->usb_temp_r);
		}
	}
	return 0;
}

static void oplus_usbtemp_thread_init(struct oplus_mms_wired *chip)
{
	init_waitqueue_head(&chip->oplus_usbtemp_wq);
	if (chip->support_usbtemp_protect_v2)
		chip->oplus_usbtemp_kthread = kthread_run(
			oplus_usbtemp_monitor_common_new_method, chip, "usbtemp_kthread");
	else
		chip->oplus_usbtemp_kthread = kthread_run(
			oplus_usbtemp_monitor_main, chip, "usbtemp_kthread");
	if (IS_ERR(chip->oplus_usbtemp_kthread))
		chg_err("failed to cread oplus_usbtemp_kthread\n");
}

void oplus_wake_up_usbtemp_thread(struct oplus_mms_wired *chip)
{
	if (oplus_usbtemp_check_is_support(chip))
		wake_up_interruptible(&chip->oplus_usbtemp_wq);
}

static void oplus_wired_gauge_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct oplus_mms_wired *chip = subs->priv_data;

	switch (type) {
	case MSG_TYPE_TIMER:
		schedule_work(&chip->gauge_update_work);
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_gauge_topic(struct oplus_mms *topic,
					      void *prv_data)
{
	struct oplus_mms_wired *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->gauge_topic = topic;
	chip->gauge_subs = oplus_mms_subscribe(chip->gauge_topic, chip,
					       oplus_wired_gauge_subs_callback,
					       "mms_wired");
	if (IS_ERR_OR_NULL(chip->gauge_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->gauge_subs));
		return;
	}

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				false);
	chip->vbat_mv = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data,
				false);
	chip->batt_temp = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				false);
	chip->batt_curr = data.intval;
}

static void oplus_wired_gauge_update_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip = container_of(work, struct oplus_mms_wired,
						gauge_update_work);
	union mms_msg_data data = { 0 };

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX,
				&data, false);
	chip->vbat_mv = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data,
				false);
	chip->batt_temp = data.intval;

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SHELL_TEMP, &data,
				false);
	chip->shell_temp = data.intval;
	(void)oplus_chg_ic_func(chip->buck_ic,
				OPLUS_IC_FUNC_VOOCPHY_SET_MATCH_TEMP,
				chip->shell_temp);
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				false);
	chip->batt_curr = data.intval;
}

static void oplus_wired_otg_enable_handler_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip =
		container_of(work, struct oplus_mms_wired, otg_enable_handler_work);
	struct mms_msg *msg;
	bool enable;
	enum typec_data_role role;
	int rc;

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_GET_OTG_ENABLE, &enable);
	if (rc < 0)
		enable = false;

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_GET_DATA_ROLE, (int *)&role);
	if (rc < 0)
		role = enable ? TYPEC_HOST : TYPEC_DEVICE;

	/* TODO:set wls wrx_en */

	if (enable) {
		rc = oplus_chg_ic_func(chip->buck_ic,
				       OPLUS_IC_FUNC_DISABLE_VBUS, true, false);
		if (rc < 0 && rc != -ENOTSUPP)
			chg_err("can't disable vbus\n");
	}
	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_OTG_BOOST_ENABLE, enable);
	if (rc < 0) {
		chg_err("can't %s otg power supply\n", enable ? "enable" : "disable");
		return;
	}
	chg_info("%s otg power supply, data_role=%s\n",
		 enable ? "enable" : "disable",
		 (role == TYPEC_HOST) ? "host" : "device");
	if (!enable && role == TYPEC_DEVICE) {
		rc = oplus_chg_ic_func(chip->buck_ic,
				       OPLUS_IC_FUNC_DISABLE_VBUS, false, true);
		if (rc < 0 && rc != -ENOTSUPP)
			chg_err("can't enable vbus\n");
	}

	if (chip->otg_enable == enable)
		return;
	chip->otg_enable = enable;
	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_OTG_ENABLE);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish otg enable msg error, rc=%d\n", rc);
		kfree(msg);
	}

	if (is_vooc_chg_auto_mode_votable_available(chip))
		vote(chip->vooc_chg_auto_mode_votable, OTG_ONLINE_VOTER,
		     enable, enable, false);
	else
		chg_err("vooc_chg_auto_mode_votable not found\n");
}

static void oplus_wired_data_role_changed_handler_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip = container_of(
		work, struct oplus_mms_wired, data_role_changed_handler_work);
	struct votable *vooc_disable_votable;
	bool enable;
	enum typec_data_role role;
	int rc;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_GET_OTG_ENABLE,
			       &enable);
	if (rc < 0)
		enable = false;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_GET_DATA_ROLE,
			       (int *)&role);
	if (rc < 0)
		role = enable ? TYPEC_HOST : TYPEC_DEVICE;

	chg_info("otg=%s, data_role=%s\n",
		 enable ? "enable" : "disable",
		 (role == TYPEC_HOST) ? "host" : "device");
	vooc_disable_votable = find_votable("VOOC_DISABLE");
	if (role == TYPEC_DEVICE) {
		if (!IS_ERR(vooc_disable_votable))
			vote(vooc_disable_votable, TYPEC_VOTER, false, 0, false);
		if (!enable) {
			rc = oplus_chg_ic_func(chip->buck_ic,
					OPLUS_IC_FUNC_DISABLE_VBUS, false, true);
			if (rc < 0 && rc != -ENOTSUPP)
				chg_err("can't enable vbus\n");
		}
	} else if (role == TYPEC_HOST) {
		if (!IS_ERR(vooc_disable_votable))
			vote(vooc_disable_votable, TYPEC_VOTER, true, 1, false);
	}
}

static void oplus_wired_voltage_change_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip =
		container_of(work, struct oplus_mms_wired, voltage_change_work);
	struct mms_msg *msg;
	int rc;

	/*
	 * Only send the message with the maximum voltage,
	 * the receiving place needs to process two
	 */
	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_CHARGER_VOL_MAX);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish charger vol max msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void oplus_wired_current_change_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip =
		container_of(work, struct oplus_mms_wired, current_change_work);
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_CHARGER_CURR_MAX);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish charger curr max msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void oplus_wired_bc12_completed_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip =
		container_of(work, struct oplus_mms_wired, bc12_completed_work);
	struct mms_msg *msg;
	int rc;

	oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_BC12_COMPLETED);

	chip->bc12_completed = true;
	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_BC12_COMPLETED);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish bc1.2 completed msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void oplus_back_ui_soc_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip =
		container_of(work, struct oplus_mms_wired, back_ui_soc_work);
	union mms_msg_data data = { 0 };

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UI_SOC, &data,
				false);
	chip->ui_soc = data.intval;
	if (chip->ui_soc < 0) {
		chg_err("ui soc not ready, rc=%d\n", chip->ui_soc);
		return;
	}
	oplus_wired_backup_soc(chip->wired_topic, chip->ui_soc);
}


static void oplus_wired_vooc_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct oplus_mms_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case VOOC_ITEM_VOOC_STARTED:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_started = data.intval;
			break;
		case VOOC_ITEM_VOOC_CHARGING:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_charging = data.intval;
			break;
		case VOOC_ITEM_ONLINE:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_online = data.intval;
			break;
		case VOOC_ITEM_SID:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_sid = (unsigned int)data.intval;
			schedule_work(&chip->chg_type_change_handler_work);
			break;
		case VOOC_ITEM_ONLINE_KEEP:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_online_keep = (unsigned int)data.intval;
			schedule_work(&chip->plugin_handler_work);
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
	struct oplus_mms_wired *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->vooc_topic = topic;
	chip->vooc_subs = oplus_mms_subscribe(chip->vooc_topic, chip,
					      oplus_wired_vooc_subs_callback,
					      "mms_wired");
	if (IS_ERR_OR_NULL(chip->vooc_subs)) {
		chg_err("subscribe vooc topic error, rc=%ld\n",
			PTR_ERR(chip->vooc_subs));
		return;
	}

	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_VOOC_STARTED, &data,
				true);
	chip->vooc_started = data.intval;
	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_VOOC_CHARGING,
				&data, true);
	chip->vooc_charging = data.intval;
	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_ONLINE, &data,
				true);
	chip->vooc_online = data.intval;
	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_ONLINE_KEEP, &data,
				true);
	chip->vooc_online_keep = data.intval;
	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_SID, &data, true);
	chip->vooc_sid = (unsigned int)data.intval;
}

static void oplus_wired_comm_subs_callback(struct mms_subscribe *subs,
					 enum mms_msg_type type, u32 id)
{
	struct oplus_mms_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_UI_SOC:
			schedule_work(&chip->back_ui_soc_work);
			break;
		case COMM_ITEM_SHELL_TEMP:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->shell_temp = data.intval;
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
	struct oplus_mms_wired *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->comm_topic = topic;
	chip->comm_subs = oplus_mms_subscribe(chip->comm_topic, chip,
					      oplus_wired_comm_subs_callback,
					      "mms_wired");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe common topic error, rc=%ld\n",
			PTR_ERR(chip->comm_subs));
		return;
	}

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UI_SOC, &data,
				true);
	chip->ui_soc = data.intval;
	if (chip->ui_soc < 0) {
		chg_err("ui soc not ready, rc=%d\n", chip->ui_soc);
		chip->ui_soc = 0;
	}
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SHELL_TEMP, &data,
				true);
	chip->shell_temp = data.intval;
}

static int oplus_mms_wired_virq_register(struct oplus_mms_wired *chip);
static int oplus_mms_wired_topic_init(struct oplus_mms_wired *chip);

static void oplus_mms_wired_init_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_wired *chip = container_of(dwork,
		struct oplus_mms_wired, mms_wired_init_work);
	struct device_node *node = chip->dev->of_node;
	static int retry = OPLUS_CHG_IC_INIT_RETRY_MAX;
	int cc_detect;
	int rc;

	chip->buck_ic = of_get_oplus_chg_ic(node, "oplus,buck_ic", 0);
	if (chip->buck_ic == NULL) {
		if (retry > 0) {
			retry--;
			schedule_delayed_work(&chip->mms_wired_init_work,
				msecs_to_jiffies(OPLUS_CHG_IC_INIT_RETRY_DELAY));
			return;
		} else {
			chg_err("oplus,buck_ic not found\n");
		}
		retry = 0;
		return;
	}

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_INIT);
	if (rc == -EAGAIN) {
		if (retry > 0) {
			retry--;
			schedule_delayed_work(&chip->mms_wired_init_work,
				msecs_to_jiffies(OPLUS_CHG_IC_INIT_RETRY_DELAY));
			return;
		} else {
			chg_err("buck_ic init timeout\n");
		}
		retry = 0;
		return;
	} else if (rc < 0) {
		chg_err("buck_ic init error, rc=%d\n", rc);
		retry = 0;
		return;
	}
	retry = 0;

	chg_info("mms wired OPLUS_IC_FUNC_INIT success\n");

	mutex_init(&chip->fcc_lock);
	oplus_mms_wired_virq_register(chip);
	g_mms_wired = chip;
	oplus_wired_input_enable(false);
	oplus_wired_output_enable(false);
	cc_detect = oplus_wired_get_hw_detect();
	if (cc_detect < 0) {
		chg_err("can't get cc_detect status, rc=%d\n", rc);
		chip->cc_detect_support = false;
	} else if (cc_detect == CC_DETECT_NULL) {
		chg_info("cc_detect not support\n");
		chip->cc_detect_support = false;
	} else {
		chip->cc_detect_support = true;
	}
	if (oplus_usbtemp_check_is_support(chip))
		oplus_usbtemp_thread_init(chip);
	(void)oplus_mms_wired_topic_init(chip);
	oplus_mms_wait_topic("gauge", oplus_wired_subscribe_gauge_topic, chip);
	oplus_mms_wait_topic("vooc", oplus_wired_subscribe_vooc_topic, chip);
	oplus_mms_wait_topic("common", oplus_wired_subscribe_comm_topic, chip);

	oplus_chg_ic_virq_trigger(chip->buck_ic, OPLUS_IC_VIRQ_CC_DETECT);
	if (oplus_wired_is_present())
		oplus_chg_ic_virq_trigger(chip->buck_ic, OPLUS_IC_VIRQ_PLUGIN);
}

static void oplus_mms_wired_err_handler_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip = container_of(work, struct oplus_mms_wired,
						err_handler_work);
	struct oplus_chg_ic_err_msg *msg, *tmp;
	struct list_head msg_list;

	INIT_LIST_HEAD(&msg_list);
	spin_lock(&chip->buck_ic->err_list_lock);
	if (!list_empty(&chip->buck_ic->err_list))
		list_replace_init(&chip->buck_ic->err_list, &msg_list);
	spin_unlock(&chip->buck_ic->err_list_lock);

	list_for_each_entry_safe(msg, tmp, &msg_list, list) {
		if (is_err_topic_available(chip))
			oplus_mms_publish_ic_err_msg(chip->err_topic,
						     ERR_ITEM_IC, msg);
		oplus_print_ic_err(msg);
		list_del(&msg->list);
		kfree(msg);
	}
}

static void oplus_mms_wired_bcc_parms_reset(struct oplus_mms_wired *chip)
{
	if (!chip) {
		return;
	}
	chip->bcc_cool_down = 0;
	chip->bcc_curr_done = BCC_CURR_DONE_UNKNOW;
}

static void oplus_mms_wired_plugin_handler_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip =
		container_of(work, struct oplus_mms_wired, plugin_handler_work);
	struct mms_msg *msg;
	union mms_msg_data data = { 0 };
	bool online, present;
	bool present_changed = false;
	enum typec_data_role role;
	int rc;

	if (chip->wired_topic == NULL) {
		chg_err("wired_topic not ready\n");
		return;
	}

	if (chip->vooc_topic) {
		oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_ONLINE_KEEP,
					&data, false);
		chip->vooc_online = data.intval;
	} else {
		chip->vooc_online = false;
	}

	present = oplus_wired_is_present();
	if (!present)
		chip->bc12_completed = false;

	if (chip->wired_present != present) {
		chg_info("wired_present=%d\n", present);
		chip->wired_present = present;
		present_changed = true;
		msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
					  WIRED_ITEM_PRESENT);
		if (msg == NULL) {
			chg_err("alloc msg error\n");
			goto skip_present;
		}
		rc = oplus_mms_publish_msg(chip->wired_topic, msg);
		if (rc < 0) {
			chg_err("publish wired present msg error, rc=%d\n", rc);
			kfree(msg);
		}

		schedule_work(&chip->chg_type_change_handler_work);
	}

	oplus_mms_wired_bcc_parms_reset(chip);

skip_present:
	online = present || chip->vooc_online;
	chg_info("present=%d, vooc_online=%d, pre_online=%d", present,
		 chip->vooc_online, chip->wired_online);
	if (chip->wired_online == online) {
		chip->usbtemp_curr_status = 0;
		/*
		 * the ICL current needs to be reconfigured when the VBUS is
		 * reconnected and there is no online notification to ensure
		 * that the ICL is configured correctly.
		 */
		if (online && present_changed && present &&
		    is_chg_icl_votable_available(chip))
			rerun_election(chip->chg_icl_votable, true);
		/* Ensure the charging status and current are reset */
		if (present && is_chg_suspend_votable_available(chip))
			rerun_election(chip->chg_suspend_votable, false);
		goto check_data_role;
	}

	if (is_pd_svooc_votable_available(chip) && !online)
		vote(chip->pd_svooc_votable, SVID_VOTER, false, 0, false);

	chip->wired_online = online;
	chip->usbtemp_check = online;
	if (chip->usbtemp_check)
		oplus_wake_up_usbtemp_thread(chip);
	/* TODO: add otg */

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_ONLINE);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		goto check_data_role;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish wired online msg error, rc=%d\n", rc);
		kfree(msg);
	}

check_data_role:
	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_GET_DATA_ROLE,
			       (int *)&role);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err("can't get typec data role, rc=%d\n", rc);
		return;
	}

	/* HW does not trigger type recognition when switch power role */
	if (role == TYPEC_HOST)
		oplus_chg_ic_virq_trigger(chip->buck_ic,
			OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
}

static void
oplus_mms_wired_chg_type_change_handler_work(struct work_struct *work)
{
	struct oplus_mms_wired *chip = container_of(
		work, struct oplus_mms_wired, chg_type_change_handler_work);
	struct mms_msg *msg;
	int rc;

	if (chip->wired_topic == NULL) {
		chg_err("wired_topic not ready\n");
		return;
	}

	if (chip->wired_online != oplus_wired_is_present())
		oplus_chg_ic_virq_trigger(chip->buck_ic, OPLUS_IC_VIRQ_PLUGIN);

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_CHG_TYPE);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish charge type change msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void oplus_mms_wired_ccdetect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_wired *chip =
		container_of(dwork, struct oplus_mms_wired, ccdetect_work);
	struct mms_msg *msg;
	int hw_detect;
	int rc;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_GET_HW_DETECT, &hw_detect);
	if (rc < 0) {
		chg_err("can't get hw detect status, rc=%d\n", rc);
		return;
	}

	chg_info("hw_detect=%d\n", hw_detect);
	if (hw_detect == 1) {
		oplus_wired_ccdetect_enable(chip, true);
		if (chip->usb_status == USB_TEMP_HIGH) {
			cancel_delayed_work(&chip->usbtemp_recover_work);
			schedule_delayed_work(&chip->usbtemp_recover_work, 0);
		}
	} else {
		chip->usbtemp_check = false;
		chip->abnormal_adapter = false;
		if(chip->usb_status == USB_TEMP_HIGH) {
			cancel_delayed_work(&chip->usbtemp_recover_work);
			schedule_delayed_work(&chip->usbtemp_recover_work, 0);
		}
		if (!oplus_wired_get_otg_switch_status()) {
			oplus_wired_ccdetect_enable(chip, false);
		}
	}
	(void)oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_CC_DETECT_HAPPENED);

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_ITEM_CC_DETECT);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish cc detect msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void oplus_mms_wired_typec_state_notify_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_wired *chip =
		container_of(dwork, struct oplus_mms_wired, typec_state_notify_work);
	struct mms_msg *msg;
	int typec_state;
	int rc;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_GET_TYPEC_STATE, &typec_state);
	if (rc < 0) {
		chg_err("can't get hw detect status, rc=%d\n", rc);
		return;
	}

	chg_info("typec_state=%d\n", typec_state);

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_TIME_TYPEC_STATE);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish typec_state msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void oplus_mms_wired_typec_state_change_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_wired *chip = container_of(
		dwork, struct oplus_mms_wired, typec_state_change_work);
	int hw_detect;
	int rc;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_GET_HW_DETECT, &hw_detect);
	if (rc < 0) {
		chg_err("can't get hw detect status, rc=%d\n", rc);
		return;
	}

	chg_info("hw_detect=%d\n", hw_detect);

	if(oplus_wired_ccdetect_is_support()) {
		if (hw_detect == 0 && !oplus_wired_get_otg_switch_status())
			oplus_wired_ccdetect_enable(chip, false);
	}
}

static void oplus_mms_wired_svid_handler_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_wired *chip =
		container_of(dwork, struct oplus_mms_wired, svid_handler_work);
	struct votable *pd_boost_disable_votable;
	bool oplus_svid;
	int rc;
	struct mms_msg *msg;

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_IS_OPLUS_SVID,
			       &oplus_svid);
	if (rc < 0) {
		chg_err("can't get oplus svid status, rc=%d\n", rc);
		oplus_svid = false;
	} else {
		chg_info("oplus_svid=%s\n", oplus_svid ? "true" : "false");
	}

	chip->abnormal_adapter = oplus_svid;
	if (oplus_svid && is_pd_svooc_votable_available(chip))
		vote(chip->pd_svooc_votable, SVID_VOTER, true, 1, false);

	pd_boost_disable_votable = find_votable("PD_BOOST_DISABLE");
	if (pd_boost_disable_votable)
		vote(pd_boost_disable_votable, SVID_VOTER, false, 0, false);

	oplus_chg_ic_virq_trigger(chip->buck_ic, OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  WIRED_TIME_ABNORMAL_ADAPTER);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish oplus svid msg error, rc=%d\n", rc);
		kfree(msg);
		return;
	}
}

static void oplus_wired_err_handler(struct oplus_chg_ic_dev *ic_dev,
					void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;
	schedule_work(&chip->err_handler_work);
}

static void oplus_wired_plugin_handler(struct oplus_chg_ic_dev *ic_dev,
					void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	if (virq_data != NULL)
		schedule_work(&chip->plugin_handler_work);
	else
		chg_info("wired plugin virq_data null\n");
}

static void oplus_wired_type_change_handler(struct oplus_chg_ic_dev *ic_dev,
					void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;
	schedule_work(&chip->chg_type_change_handler_work);
}

static void oplus_wired_svid_handler(struct oplus_chg_ic_dev *ic_dev,
					void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;
	schedule_delayed_work(&chip->svid_handler_work, 0);
}

static void oplus_wired_otg_enable_handler(struct oplus_chg_ic_dev *ic_dev,
					   void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;
	schedule_work(&chip->otg_enable_handler_work);
}

#define CCDETECT_DELAY_MS	50
static void oplus_wired_cc_detect_handler(struct oplus_chg_ic_dev *ic_dev,
					  void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	cancel_delayed_work(&chip->ccdetect_work);
	schedule_delayed_work(&chip->ccdetect_work, msecs_to_jiffies(CCDETECT_DELAY_MS));
}

#define TYPEC_STATE_DELAY_MS	50
static void oplus_wired_typec_state_notify_handler(struct oplus_chg_ic_dev *ic_dev,
					  void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	cancel_delayed_work(&chip->typec_state_notify_work);
	schedule_delayed_work(&chip->typec_state_notify_work, msecs_to_jiffies(CCDETECT_DELAY_MS));
}

static void oplus_wired_cc_changed_handler(struct oplus_chg_ic_dev *ic_dev,
					  void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	schedule_delayed_work(&chip->typec_state_change_work, 0);
}

static void oplus_wired_voltage_changed_handler(struct oplus_chg_ic_dev *ic_dev,
						void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	schedule_work(&chip->voltage_change_work);
}

static void oplus_wired_current_changed_handler(struct oplus_chg_ic_dev *ic_dev,
						void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	schedule_work(&chip->current_change_work);
}

static void oplus_wired_bc12_completed_handler(struct oplus_chg_ic_dev *ic_dev,
					       void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	schedule_work(&chip->bc12_completed_work);
	schedule_work(&chip->chg_type_change_handler_work);
}

static void
oplus_wired_data_role_changed_handler(struct oplus_chg_ic_dev *ic_dev,
				      void *virq_data)
{
	struct oplus_mms_wired *chip = virq_data;

	schedule_work(&chip->data_role_changed_handler_work);
}

static int oplus_mms_wired_virq_register(struct oplus_mms_wired *chip)
{
	int rc;

	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_ERR,
		oplus_wired_err_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_PLUGIN,
		oplus_wired_plugin_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_PLUGIN error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_CHG_TYPE_CHANGE,
		oplus_wired_type_change_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_CHG_TYPE_CHANGE error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_SVID,
		oplus_wired_svid_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_SVID error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_OTG_ENABLE,
		oplus_wired_otg_enable_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_OTG_ENABLE error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_CC_DETECT,
		oplus_wired_cc_detect_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_CC_DETECT error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_CC_CHANGED,
		oplus_wired_cc_changed_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_CC_CHANGED error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_VOLTAGE_CHANGED,
		oplus_wired_voltage_changed_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_VOLTAGE_CHANGED error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_CURRENT_CHANGED,
		oplus_wired_current_changed_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_CURRENT_CHANGED error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_BC12_COMPLETED,
		oplus_wired_bc12_completed_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_BC12_COMPLETED error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->buck_ic, OPLUS_IC_VIRQ_TYPEC_STATE,
		oplus_wired_typec_state_notify_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_TYPEC_STATE error, rc=%d", rc);

	rc = oplus_chg_ic_virq_register(chip->buck_ic,
					OPLUS_IC_VIRQ_DATA_ROLE_CHANGED,
					oplus_wired_data_role_changed_handler,
					chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_DATA_ROLE_CHANGED error, rc=%d",
			rc);

	return 0;
}

static int oplus_mms_wired_update_present(struct oplus_mms *mms,
					  union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	bool present = false;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	present = chip->wired_present;

end:
	data->intval = present;
	return 0;
}

static int oplus_mms_wired_update_online(struct oplus_mms *mms,
					 union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	bool present = false;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	present = chip->wired_online;

end:
	data->intval = present;
	return 0;
}

static int oplus_mms_wired_update_chg_type(struct oplus_mms *mms,
					   union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int type = OPLUS_CHG_USB_TYPE_UNKNOWN;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	type = oplus_wired_get_chg_type();
	if (type < 0) {
		chg_err("can't get charger type, rc=%d", type);
		goto end;
	}

end:
	chg_info("chg_type update to %d\n", type);
	data->intval = type;
	return 0;
}

static int oplus_mms_wired_update_bc12_completed(struct oplus_mms *mms,
						 union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	bool bc12_completed = false;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	bc12_completed = chip->bc12_completed;

end:
	data->intval = bc12_completed;
	return 0;
}

static int oplus_mms_wired_update_cc_mode(struct oplus_mms *mms,
					  union mms_msg_data *data)
{
	/* TODO */
	return 0;
}

static int oplus_mms_wired_update_cc_detect(struct oplus_mms *mms,
					    union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	enum oplus_wired_cc_detect_status cc_detect_status = CC_DETECT_NULL;
	int cc_detect;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_BUCK_GET_HW_DETECT,
			       &cc_detect);
	if (rc < 0) {
		chg_err("can't get cc detect status, rc=%d\n", rc);
		cc_detect_status = CC_DETECT_NULL;
		goto end;
	}

	if (!!cc_detect)
		cc_detect_status = CC_DETECT_PLUGIN;
	else
		cc_detect_status = CC_DETECT_NOTPLUG;

end:
	data->intval = cc_detect_status;
	return 0;
}

static int oplus_mms_wired_update_usb_status(struct oplus_mms *mms,
					     union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int usb_status = 0;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	usb_status = (int)chip->usb_status;
end:
	data->intval = usb_status;
	return 0;
}

#define USBTEMP_DEFAULT_VOLT_VALUE_MV 950
static int oplus_mms_wired_update_usb_temp_volt_l(struct oplus_mms *mms,
						  union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int volt = USBTEMP_DEFAULT_VOLT_VALUE_MV;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	volt = chip->usbtemp_volt_l;
end:
	data->intval = volt;
	return 0;
}

static int oplus_mms_wired_update_usb_temp_volt_r(struct oplus_mms *mms,
						  union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int volt = USBTEMP_DEFAULT_VOLT_VALUE_MV;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	volt = chip->usbtemp_volt_r;
end:
	data->intval = volt;
	return 0;
}

static int oplus_mms_wired_update_usb_temp_l(struct oplus_mms *mms,
					     union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int temp = 25;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	temp = chip->usb_temp_l;
end:
	data->intval = temp;
	return 0;
}

static int oplus_mms_wired_update_usb_temp_r(struct oplus_mms *mms,
					     union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int temp = 25;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	temp = chip->usb_temp_l;
end:
	data->intval = temp;
	return 0;
}

static int oplus_mms_wired_update_otg_enable_status(struct oplus_mms *mms,
						    union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	bool enable = false;

	if (mms == NULL) {
		chg_err("mms is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	enable = chip->otg_enable;
end:
	data->intval = enable;
	return 0;
}

static int oplus_mms_wired_update_chg_curr_max(struct oplus_mms *mms,
					       union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int curr_max;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->buck_ic,
			       OPLUS_IC_FUNC_GET_CHARGER_CURR_MAX, &curr_max);
	if (rc < 0) {
		chg_err("can't get charger current max, rc=%d\n", rc);
		return rc;
	}
	chg_info("Maximum allowable charger current [%d]\n", curr_max);
	data->intval = curr_max;

	return 0;
}

void oplus_wired_check_bcc_curr_done(struct oplus_mms *topic)
{
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		return;
	}

	mutex_lock(&chip->bcc_curr_done_mutex);
	if (chip->bcc_curr_done == BCC_CURR_DONE_REQUEST) {
		chip->bcc_curr_done = BCC_CURR_DONE_ACK;
		chg_err("bcc_curr_done:%d\n", chip->bcc_curr_done);
	}
	mutex_unlock(&chip->bcc_curr_done_mutex);
}

void oplus_wired_set_bcc_curr_request(struct oplus_mms *topic)
{
	struct oplus_mms_wired *chip = g_mms_wired;

	if (!chip) {
		return;
	}

	mutex_lock(&chip->bcc_curr_done_mutex);
	chip->bcc_curr_done = BCC_CURR_DONE_REQUEST;
	chg_err("bcc_curr_done:%d\n", chip->bcc_curr_done);
	mutex_unlock(&chip->bcc_curr_done_mutex);
}

int oplus_wired_get_bcc_curr_done_status(struct oplus_mms *topic)
{
	struct oplus_mms_wired *chip = g_mms_wired;
	int ret = 0;

	if (!chip) {
		return 0;
	}

	mutex_lock(&chip->bcc_curr_done_mutex);
	ret = chip->bcc_curr_done;
	mutex_unlock(&chip->bcc_curr_done_mutex);

	chg_err("bcc_curr_done:%d\n", ret);
	return ret;
}

static int oplus_mms_wired_update_chg_vol_max(struct oplus_mms *mms,
					      union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int vol_max;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_GET_CHARGER_VOL_MAX,
			       &vol_max);
	if (rc < 0) {
		chg_err("can't get charger voltage max, rc=%d\n", rc);
		return rc;
	}
	chg_info("Maximum allowable charger voltage [%d]\n", vol_max);
	data->intval = vol_max;

	return 0;
}

static int oplus_mms_wired_update_chg_vol_min(struct oplus_mms *mms,
					      union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int vol_min;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_GET_CHARGER_VOL_MIN,
			       &vol_min);
	if (rc < 0) {
		chg_err("can't get charger voltage min, rc=%d\n", rc);
		return rc;
	}
	chg_info("Minimum allowable charger voltage [%d]\n", vol_min);
	data->intval = vol_min;

	return 0;
}

static int oplus_mms_abnoramal_adapter(struct oplus_mms *mms,
				       union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);
	data->intval = chip->abnormal_adapter;
	chg_info("abnormal adapter %d \n", data->intval);
	return 0;
}

static int oplus_mms_get_typec_state(struct oplus_mms *mms,
				       union mms_msg_data *data)
{
	struct oplus_mms_wired *chip;
	int typec_state;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->buck_ic, OPLUS_IC_FUNC_BUCK_GET_TYPEC_STATE,
			       &typec_state);
	if (rc < 0) {
		chg_err("can't get typec state fail, rc=%d\n", rc);
		return rc;
	}
	chg_info("typec_state = [%d]\n", typec_state);
	data->intval = typec_state;

	return 0;

}

static void oplus_mms_wired_update(struct oplus_mms *mms, bool publish)
{
}

static struct mms_item oplus_mms_wired_item[] = {
	{
		.desc = {
			.item_id = WIRED_ITEM_PRESENT,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_present,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_ONLINE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_online,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_ERR_CODE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = NULL,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_CHG_TYPE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_chg_type,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_BC12_COMPLETED,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_bc12_completed,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_CC_MODE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_cc_mode,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_CC_DETECT,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_cc_detect,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_USB_STATUS,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_usb_status,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_USB_TEMP_VOLT_L,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_usb_temp_volt_l,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_USB_TEMP_VOLT_R,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_usb_temp_volt_r,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_USB_TEMP_L,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_usb_temp_l,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_USB_TEMP_R,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_usb_temp_r,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_OTG_ENABLE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_otg_enable_status,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_CHARGER_CURR_MAX,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_chg_curr_max,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_CHARGER_VOL_MAX,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_chg_vol_max,
		}
	},
	{
		.desc = {
			.item_id = WIRED_ITEM_CHARGER_VOL_MIN,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_wired_update_chg_vol_min,
		}
	},
	{
		.desc = {
			.item_id = WIRED_TIME_ABNORMAL_ADAPTER,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_abnoramal_adapter,
		}
	},
	{
		.desc = {
			.item_id = WIRED_TIME_TYPEC_STATE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_get_typec_state,
		}
	},
};

static const struct oplus_mms_desc oplus_mms_wired_desc = {
	.name = "wired",
	.type = OPLUS_MMS_TYPE_USB,
	.item_table = oplus_mms_wired_item,
	.item_num = ARRAY_SIZE(oplus_mms_wired_item),
	.update_items = NULL,
	.update_items_num = 0,
	.update_interval = 0, /* ms */
	.update = oplus_mms_wired_update,
};

static int oplus_mms_wired_topic_init(struct oplus_mms_wired *chip)
{
	struct oplus_mms_config mms_cfg = {};
	int rc;

	mms_cfg.drv_data = chip;
	mms_cfg.of_node = chip->dev->of_node;

	if (of_property_read_bool(mms_cfg.of_node,
				  "oplus,topic-update-interval")) {
		rc = of_property_read_u32(mms_cfg.of_node,
					  "oplus,topic-update-interval",
					  &mms_cfg.update_interval);
		if (rc < 0)
			mms_cfg.update_interval = 0;
	}

	chip->wired_topic = devm_oplus_mms_register(chip->dev, &oplus_mms_wired_desc, &mms_cfg);
	if (IS_ERR(chip->wired_topic)) {
		chg_err("Couldn't register wired topic\n");
		rc = PTR_ERR(chip->wired_topic);
		return rc;
	}

	schedule_work(&chip->plugin_handler_work);
	schedule_work(&chip->chg_type_change_handler_work);

	return 0;
}

static void oplus_mms_wired_usbtemp_v2_parse_dt(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec = &chip->usbtemp_spec;
	struct device_node *node = chip->dev->of_node;
	int rc;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_batt_temp_low", &spec->usbtemp_batt_temp_low);
	if (rc)
		spec->usbtemp_batt_temp_low = OPCHG_USBTEMP_BATT_TEMP_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_batt_temp_high", &spec->usbtemp_batt_temp_high);
	if (rc)
		spec->usbtemp_batt_temp_high = OPCHG_USBTEMP_BATT_TEMP_HIGH;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_ntc_temp_low", &spec->usbtemp_ntc_temp_low);
	if (rc)
		spec->usbtemp_ntc_temp_low = OPCHG_USBTEMP_NTC_TEMP_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_ntc_temp_high", &spec->usbtemp_ntc_temp_high);
	if (rc)
		spec->usbtemp_ntc_temp_high = OPCHG_USBTEMP_NTC_TEMP_HIGH;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_temp_gap_low_with_batt_temp",
				  &spec->usbtemp_temp_gap_low_with_batt_temp);
	if (rc)
		spec->usbtemp_temp_gap_low_with_batt_temp = OPCHG_USBTEMP_GAP_LOW_WITH_BATT_TEMP;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_temp_gap_high_with_batt_temp",
				  &spec->usbtemp_temp_gap_high_with_batt_temp);
	if (rc)
		spec->usbtemp_temp_gap_high_with_batt_temp = OPCHG_USBTEMP_GAP_HIGH_WITH_BATT_TEMP;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_temp_gap_low_without_batt_temp",
				  &spec->usbtemp_temp_gap_low_without_batt_temp);
	if (rc)
		spec->usbtemp_temp_gap_low_without_batt_temp = OPCHG_USBTEMP_GAP_LOW_WITHOUT_BATT_TEMP;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_temp_gap_high_without_batt_temp",
				  &spec->usbtemp_temp_gap_high_without_batt_temp);
	if (rc)
		spec->usbtemp_temp_gap_high_without_batt_temp = OPCHG_USBTEMP_GAP_HIGH_WITHOUT_BATT_TEMP;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_rise_fast_temp_low", &spec->usbtemp_rise_fast_temp_low);
	if (rc)
		spec->usbtemp_rise_fast_temp_low = OPCHG_USBTEMP_RISE_FAST_TEMP_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_rise_fast_temp_high", &spec->usbtemp_rise_fast_temp_high);
	if (rc)
		spec->usbtemp_rise_fast_temp_high = OPCHG_USBTEMP_RISE_FAST_TEMP_HIGH;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_rise_fast_temp_count_low",
				  &spec->usbtemp_rise_fast_temp_count_low);
	if (rc)
		spec->usbtemp_rise_fast_temp_count_low = OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_rise_fast_temp_count_high",
				  &spec->usbtemp_rise_fast_temp_count_high);
	if (rc)
		spec->usbtemp_rise_fast_temp_count_high = OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_HIGH;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_ntc_low", &spec->usbtemp_cool_down_ntc_low);
	if (rc)
		spec->usbtemp_cool_down_ntc_low = OPCHG_USBTEMP_COOL_DOWN_NTC_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_ntc_high", &spec->usbtemp_cool_down_ntc_high);
	if (rc)
		spec->usbtemp_cool_down_ntc_high = OPCHG_USBTEMP_COOL_DOWN_NTC_HIGH;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_gap_low", &spec->usbtemp_cool_down_gap_low);
	if (rc)
		spec->usbtemp_cool_down_gap_low = OPCHG_USBTEMP_COOL_DOWN_GAP_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_gap_high", &spec->usbtemp_cool_down_gap_high);
	if (rc)
		spec->usbtemp_cool_down_gap_high = OPCHG_USBTEMP_COOL_DOWN_GAP_HIGH;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_recover_ntc_low",
				  &spec->usbtemp_cool_down_recover_ntc_low);
	if (rc)
		spec->usbtemp_cool_down_recover_ntc_low = OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_recover_ntc_high",
				  &spec->usbtemp_cool_down_recover_ntc_high);
	if (rc)
		spec->usbtemp_cool_down_recover_ntc_high = OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_HIGH;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_recover_gap_low",
				  &spec->usbtemp_cool_down_recover_gap_low);
	if (rc)
		spec->usbtemp_cool_down_recover_gap_low = OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_LOW;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_recover_gap_high",
				  &spec->usbtemp_cool_down_recover_gap_high);
	if (rc)
		spec->usbtemp_cool_down_recover_gap_high = OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_HIGH;
}

static void oplus_mms_wired_parse_dt(struct oplus_mms_wired *chip)
{
	struct oplus_usbtemp_spec_config *spec = &chip->usbtemp_spec;
	struct device_node *node = chip->dev->of_node;
	int rc;

	rc = of_property_read_u32(node, "oplus_spec,usbtemp_batttemp_gap", &spec->usbtemp_batttemp_gap);
	if (rc)
		spec->usbtemp_batttemp_gap = 18;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_temp_gap", &spec->usbtemp_cool_down_temp_gap);
	if (rc)
		spec->usbtemp_cool_down_temp_gap = 12;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_recovery_temp_gap",
				  &spec->usbtemp_cool_down_recovery_temp_gap);
	if (rc)
		spec->usbtemp_cool_down_recovery_temp_gap = 6;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_temp_first_gap",
				  &spec->usbtemp_cool_down_temp_first_gap);
	if (rc)
		spec->usbtemp_cool_down_temp_first_gap = 12;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_recovery_temp_first_gap",
				  &spec->usbtemp_cool_down_recovery_temp_first_gap);
	if (rc)
		spec->usbtemp_cool_down_recovery_temp_first_gap = 6;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_temp_second_gap",
				  &spec->usbtemp_cool_down_temp_second_gap);
	if (rc)
		spec->usbtemp_cool_down_temp_second_gap = 17;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_cool_down_recovery_temp_second_gap",
				  &spec->usbtemp_cool_down_recovery_temp_second_gap);
	if (rc)
		spec->usbtemp_cool_down_recovery_temp_second_gap = 11;

	chip->new_usbtemp_cool_down_support = of_property_read_bool(node, "oplus,new_usbtemp_cool_down_support");
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_batttemp_recover_gap", &spec->usbtemp_batttemp_recover_gap);
	if (rc)
		spec->usbtemp_batttemp_recover_gap = 6;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_batttemp_current_gap", &spec->usbtemp_batttemp_current_gap);
	if (rc)
		spec->usbtemp_batttemp_current_gap = 12;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_max_temp_diff", &spec->usbtemp_max_temp_diff);
	if (rc)
		spec->usbtemp_max_temp_diff = 7;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_max_temp_thr", &spec->usbtemp_max_temp_thr);
	if (rc)
		spec->usbtemp_max_temp_thr = 57;
	rc = of_property_read_u32(node, "oplus_spec,usbtemp_temp_up_time_thr", &spec->usbtemp_temp_up_time_thr);
	if (rc)
		spec->usbtemp_temp_up_time_thr = 30;

	chip->support_usbtemp_protect_v2 = of_property_read_bool(node, "oplus,support_usbtemp_protect_v2");
	chg_info("support_usbtemp_protect_v2=%s\n", chip->support_usbtemp_protect_v2 ? "true" : "false");
	if (chip->support_usbtemp_protect_v2)
		oplus_mms_wired_usbtemp_v2_parse_dt(chip);
}

static int oplus_mms_wired_probe(struct platform_device *pdev)
{
	struct oplus_mms_wired *chip;
	struct oplus_usbtemp_spec_config *spec;
	const char *name;
	int rc, i;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_mms_wired),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);
	spec = &chip->usbtemp_spec;

	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);
	oplus_mms_wired_parse_dt(chip);
	rc = of_property_read_string(chip->dev->of_node, "oplus,adc_info_name", &name);
	if (rc < 0) {
		chg_err("read oplus,adc_info_name error, rc=%d\n", rc);
		name = "default";
	}
	for (i = 0; i < ARRAY_SIZE(adc_vol_temp_info_table); i++) {
		if (strcmp(adc_vol_temp_info_table[i].name, name) == 0) {
			chip->adc_vol_temp_info = &adc_vol_temp_info_table[i];
		}
	}
	if (chip->adc_vol_temp_info == NULL) {
		chg_err("%s adc_vol_temp_info not found\n", name);
		chip->adc_vol_temp_info = &adc_vol_temp_info_table[0];
	}

	INIT_DELAYED_WORK(&chip->mms_wired_init_work,
			  oplus_mms_wired_init_work);
	INIT_DELAYED_WORK(&chip->usbtemp_recover_work, oplus_usbtemp_recover_work);
	INIT_DELAYED_WORK(&chip->ccdetect_work, oplus_mms_wired_ccdetect_work);
	INIT_DELAYED_WORK(&chip->typec_state_notify_work, oplus_mms_wired_typec_state_notify_work);
	INIT_DELAYED_WORK(&chip->typec_state_change_work, oplus_mms_wired_typec_state_change_work);
	INIT_DELAYED_WORK(&chip->svid_handler_work, oplus_mms_wired_svid_handler_work);
	INIT_WORK(&chip->err_handler_work, oplus_mms_wired_err_handler_work);
	INIT_WORK(&chip->plugin_handler_work,
		  oplus_mms_wired_plugin_handler_work);
	INIT_WORK(&chip->chg_type_change_handler_work,
		  oplus_mms_wired_chg_type_change_handler_work);
	INIT_WORK(&chip->gauge_update_work, oplus_wired_gauge_update_work);
	INIT_WORK(&chip->otg_enable_handler_work, oplus_wired_otg_enable_handler_work);
	INIT_WORK(&chip->voltage_change_work, oplus_wired_voltage_change_work);
	INIT_WORK(&chip->current_change_work, oplus_wired_current_change_work);
	INIT_WORK(&chip->bc12_completed_work, oplus_wired_bc12_completed_work);
	INIT_WORK(&chip->data_role_changed_handler_work,
		  oplus_wired_data_role_changed_handler_work);
	INIT_WORK(&chip->back_ui_soc_work, oplus_back_ui_soc_work);

	chip->dischg_flag = false;

	schedule_delayed_work(&chip->mms_wired_init_work, 0);

	mutex_init(&chip->bcc_curr_done_mutex);
	oplus_mms_wired_bcc_parms_reset(chip);

	chg_info("probe success\n");
	return 0;
}

static int oplus_mms_wired_remove(struct platform_device *pdev)
{
	struct oplus_mms_wired *chip = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(chip->gauge_subs))
		oplus_mms_unsubscribe(chip->gauge_subs);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void oplus_mms_wired_shutdown(struct platform_device *pdev)
{
	struct oplus_mms_wired *chip = platform_get_drvdata(pdev);
	int rc;

	if (chip->ship_mode) {
		rc = oplus_chg_ic_func(chip->buck_ic,
				       OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE,
				       true);
		if (rc < 0)
			chg_err("can't enable ship mode, rc=%d\n", rc);
		else
			chg_err("enable ship mode\n");
		msleep(1000);
	}
}

static const struct of_device_id oplus_mms_wired_match[] = {
	{ .compatible = "oplus,mms_wired" },
	{},
};

static struct platform_driver oplus_mms_wired_driver = {
	.driver		= {
		.name = "oplus-mms_wired",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_mms_wired_match),
	},
	.probe		= oplus_mms_wired_probe,
	.remove		= oplus_mms_wired_remove,
	.shutdown	= oplus_mms_wired_shutdown,
};

static __init int oplus_mms_wired_init(void)
{
	return platform_driver_register(&oplus_mms_wired_driver);
}

static __exit void oplus_mms_wired_exit(void)
{
	platform_driver_unregister(&oplus_mms_wired_driver);
}

oplus_chg_module_register(oplus_mms_wired);
