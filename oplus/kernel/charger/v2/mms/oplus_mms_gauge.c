#define pr_fmt(fmt) "[MMS_GAUGE]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
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
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/power_supply.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_voter.h>
#include <oplus_mms.h>
#include <oplus_chg_monitor.h>
#include <oplus_mms_wired.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_vooc.h>

struct oplus_mms_gauge {
	struct device *dev;
	struct oplus_chg_ic_dev *gauge_ic;
	struct oplus_chg_ic_dev *voocphy_ic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *wired_topic;
	struct oplus_mms *vooc_topic;
	struct oplus_mms *err_topic;
	struct mms_subscribe *comm_subs;
	struct mms_subscribe *wired_subs;
	struct mms_subscribe *gauge_subs;
	struct mms_subscribe *vooc_subs;

	struct delayed_work hal_gauge_init_work;
	struct work_struct err_handler_work;
	struct work_struct online_handler_work;
	struct work_struct offline_handler_work;
	struct work_struct resume_handler_work;
	struct work_struct update_change_work;
	struct work_struct gauge_update_work;
	struct work_struct gauge_set_curve_work;

	struct votable *gauge_update_votable;

	int device_type;
	int device_type_for_vooc;
	unsigned int vooc_sid;
	unsigned int err_code;
	int check_batt_vol_count;
	bool pd_svooc;
	bool bat_volt_different;

	bool factory_test_mode;
	bool wired_online;
	bool wls_online;
	bool hmac;
};

static struct oplus_mms_gauge *g_mms_gauge;

static int gauge_dbg_tbat = 0;
module_param(gauge_dbg_tbat, int, 0644);
MODULE_PARM_DESC(gauge_dbg_tbat, "debug battery temperature");

static int gauge_dbg_vbat = 0;
module_param(gauge_dbg_vbat, int, 0644);
MODULE_PARM_DESC(gauge_dbg_vbat, "debug battery voltage");

static int gauge_dbg_ibat = 0;
module_param(gauge_dbg_ibat, int, 0644);
MODULE_PARM_DESC(gauge_dbg_ibat, "debug battery current");

__maybe_unused static bool
is_err_topic_available(struct oplus_mms_gauge *chip)
{
	if (!chip->err_topic)
		chip->err_topic = oplus_mms_get_by_name("error");
	return !!chip->err_topic;
}

int oplus_gauge_get_batt_mvolts(void)
{
	int rc;
	int vol_mv;

	if (!g_mms_gauge)
		return 3800;

	if (gauge_dbg_vbat != 0) {
		chg_info("debug enabled, voltage gauge_dbg_vbat[%d]\n", gauge_dbg_vbat);
		return gauge_dbg_vbat;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX, &vol_mv);
	if (rc < 0) {
		chg_err("get battery voltage error, rc=%d\n", rc);
		return 3800;
	}

	return vol_mv;
}

int oplus_gauge_get_batt_fc(void)
{
	int rc;
	int fc;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_FC, &fc);
	if (rc < 0) {
		chg_err("get battery fc error, rc=%d\n", rc);
		return 0;
	}

	return fc;
}

int oplus_gauge_get_batt_qm(void)
{
	int rc;
	int qm;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_QM, &qm);
	if (rc < 0) {
		chg_err("get battery qm error, rc=%d\n", rc);
		return 0;
	}

	return qm;
}

int oplus_gauge_get_batt_pd(void)
{
	int rc;
	int pd;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_PD, &pd);
	if (rc < 0) {
		chg_err("get battery pd error, rc=%d\n", rc);
		return 0;
	}

	return pd;
}

int oplus_gauge_get_batt_rcu(void)
{
	int rc;
	int rcu;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU, &rcu);
	if (rc < 0) {
		chg_err("get battery rcu error, rc=%d\n", rc);
		return 0;
	}

	return rcu;
}

int oplus_gauge_get_batt_rcf(void)
{
	int rc;
	int rcf;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF, &rcf);
	if (rc < 0) {
		chg_err("get battery rcf error, rc=%d\n", rc);
		return 0;
	}

	return rcf;
}

int oplus_gauge_get_batt_fcu(void)
{
	int rc;
	int fcu;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU, &fcu);
	if (rc < 0) {
		chg_err("get battery fcu error, rc=%d\n", rc);
		return 0;
	}

	return fcu;
}

int oplus_gauge_get_batt_fcf(void)
{
	int rc;
	int fcf;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF, &fcf);
	if (rc < 0) {
		chg_err("get battery fcf error, rc=%d\n", rc);
		return 0;
	}

	return fcf;
}

int oplus_gauge_get_batt_sou(void)
{
	int rc;
	int sou;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU, &sou);
	if (rc < 0) {
		chg_err("get battery sou error, rc=%d\n", rc);
		return 0;
	}

	return sou;
}

int oplus_gauge_get_batt_do0(void)
{
	int rc;
	int do0;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0, &do0);
	if (rc < 0) {
		chg_err("get battery do0 error, rc=%d\n", rc);
		return 0;
	}

	return do0;
}

int oplus_gauge_get_batt_doe(void)
{
	int rc;
	int doe;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE, &doe);
	if (rc < 0) {
		chg_err("get battery doe error, rc=%d\n", rc);
		return 0;
	}

	return doe;
}

int oplus_gauge_get_batt_trm(void)
{
	int rc;
	int trm;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM, &trm);
	if (rc < 0) {
		chg_err("get battery trm error, rc=%d\n", rc);
		return 0;
	}

	return trm;
}

int oplus_gauge_get_batt_pc(void)
{
	int rc;
	int pc;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_PC, &pc);
	if (rc < 0) {
		chg_err("get battery pc error, rc=%d\n", rc);
		return 0;
	}

	return pc;
}

int oplus_gauge_get_batt_qs(void)
{
	int rc;
	int qs;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_QS, &qs);
	if (rc < 0) {
		chg_err("get battery qs error, rc=%d\n", rc);
		return 0;
	}

	return qs;
}

int oplus_gauge_get_batt_mvolts_2cell_max(void)
{
	int rc;
	int vol_mv;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX, &vol_mv);
	if (rc < 0) {
		chg_err("get battery voltage max error, rc=%d\n", rc);
		return 0;
	}

	return vol_mv;
}

int oplus_gauge_get_batt_mvolts_2cell_min(void)
{
	int rc;
	int vol_mv;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN, &vol_mv);
	if (rc < 0) {
		chg_err("get battery voltage min error, rc=%d\n", rc);
		return 0;
	}

	return vol_mv;
}

int oplus_gauge_get_batt_temperature(void)
{
	int rc;
	int temp;

	if (!g_mms_gauge)
		return -400;
	if (gauge_dbg_tbat != 0) {
		chg_err("debug enabled, gauge_dbg_tbat[%d]\n", gauge_dbg_tbat);
		return gauge_dbg_tbat;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP, &temp);
	if (rc < 0) {
		chg_err("get battery temp error, rc=%d\n", rc);
		return -400;
	}
	if (get_eng_version() == HIGH_TEMP_AGING) {
		chg_info("[OPLUS_CHG]CONFIG_HIGH_TEMP_VERSION enable here, "
			 "disable high tbat shutdown\n");
		if (temp > 690)
			temp = 690;
	}

	return temp;
}

int oplus_gauge_get_batt_soc(void)
{
	int rc;
	int soc;

	if (!g_mms_gauge)
		return 50;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC, &soc);
	if (rc < 0) {
		chg_err("get battery soc error, rc=%d\n", rc);
		return 50;
	}

	return soc;
}

int oplus_gauge_get_bcc_parameters(char *buf)
{
	int rc;
	bool gauge_locked;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return 0;
	}

	gauge_locked = oplus_gauge_is_locked();

	if (!gauge_locked) {
		rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS, buf);
		if (rc < 0) {
			chg_err("update cc parms, rc=%d\n", rc);
			return 0;
		}
	}

	return 0;
}

int oplus_gauge_fastchg_update_bcc_parameters(char *buf)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS, buf);
	if (rc < 0) {
		chg_err("update cc parms, rc=%d\n", rc);
		return 0;
	}
	return 0;
}

int oplus_gauge_get_prev_bcc_parameters(char *buf)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS, buf);
	if (rc < 0) {
		chg_err("update cc parms, rc=%d\n", rc);
		return 0;
	}
	return 0;
}

int oplus_gauge_set_bcc_parameters(const char *buf)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS, buf);
	if (rc < 0) {
		chg_err("update cc parms, rc=%d\n", rc);
		return 0;
	}
	return 0;
}

int oplus_gauge_protect_check(void)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return 0;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK);
	if (rc < 0) {
		chg_err("protect_check, rc=%d\n", rc);
		return 0;
	}
	return 0;
}

bool oplus_gauge_afi_update_done(void)
{
	int rc;
	bool status = true;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return true;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE, &status);
	if (rc < 0) {
		chg_err("afi_update_done, rc=%d\n", rc);
		return true;
	}
	return status;
}

bool oplus_gauge_check_reset_condition(void)
{
	int rc;
	bool need_reset = false;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return true;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_CHECK_RESET, &need_reset);
	if (rc < 0) {
		chg_err("need_reset, rc=%d\n", rc);
		return false;
	}
	return need_reset;
}

bool oplus_gauge_reset(void)
{
	int rc;
	bool reset_done = false;

	if (g_mms_gauge == NULL) {
		chg_err("g_gauge_ic is NULL\n");
		return true;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_RESET, &reset_done);
	if (rc < 0) {
		chg_err("reset_done, rc=%d\n", rc);
		return false;
	}
	chg_err("oplus_gauge_reset, reset_done=%d\n", reset_done);
	return reset_done;
}


int oplus_gauge_get_batt_current(void)
{
	int rc;
	int curr_ma;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR, &curr_ma);
	if (rc < 0) {
		chg_err("get battery average current error, rc=%d\n", rc);
		return 0;
	}

	return curr_ma;
}

int oplus_gauge_get_remaining_capacity(void)
{
	int rc;
	int rm;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_RM, &rm);
	if (rc < 0) {
		chg_err("get battery remaining capacity error, rc=%d\n", rc);
		return 0;
	}

	return rm;
}

int oplus_gauge_get_device_type(void)
{
	if (!g_mms_gauge)
		return 0;
	return g_mms_gauge->device_type;
}

int oplus_gauge_get_device_type_for_vooc(void)
{
	if (!g_mms_gauge)
		return 0;
	return g_mms_gauge->device_type_for_vooc;
}

int oplus_gauge_get_batt_fcc(void)
{
	int rc;
	int fcc;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC, &fcc);
	if (rc < 0) {
		chg_err("get battery fcc error, rc=%d\n", rc);
		return 0;
	}

	return fcc;
}

int oplus_gauge_get_batt_cc(void)
{
	int rc;
	int cc;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_CC, &cc);
	if (rc < 0) {
		chg_err("get battery cc error, rc=%d\n", rc);
		return 0;
	}

	return cc;
}

int oplus_gauge_get_batt_soh(void)
{
	int rc;
	int soh;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH, &soh);
	if (rc < 0) {
		chg_err("get battery soh error, rc=%d\n", rc);
		return 0;
	}

	return soh;
}

bool oplus_gauge_get_batt_hmac(void)
{
	int rc;
	bool pass;

	if (!g_mms_gauge)
		return false;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC, &pass);
	if (rc < 0) {
		chg_err("get battery hmac status error, rc=%d\n", rc);
		return false;
	}

	return pass;
}

bool oplus_gauge_get_batt_authenticate(void)
{
	int rc;
	bool pass;

	if (!g_mms_gauge)
		return false;
	if (g_mms_gauge->bat_volt_different)
		return false;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic,
			       OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH, &pass);
	if (rc < 0) {
		chg_err("get battery authenticate status error, rc=%d\n", rc);
		return false;
	}

	return pass;
}

bool oplus_gauge_is_exist(struct oplus_mms *topic)
{
	struct oplus_mms_gauge *chip;
	bool exist;
	int rc;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return false;
	}
	chip = oplus_mms_get_drvdata(topic);

	rc = oplus_chg_ic_func(chip->gauge_ic,
			       OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST, &exist);
	if (rc < 0) {
		chg_err("get battery exist status error, rc=%d\n", rc);
		exist = false;
	}

	return exist;
}

int oplus_gauge_get_batt_capacity_mah(struct oplus_mms *topic)
{
	struct oplus_mms_gauge *chip;
	int rc;
	int cap_mah;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return 2000;
	}
	chip = oplus_mms_get_drvdata(topic);

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic,
			       OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP, &cap_mah);
	if (rc < 0) {
		chg_err("get battery capacity_mah error, rc=%d\n", rc);
		return 2000;
	}

	return cap_mah;
}

void oplus_gauge_set_batt_full(bool full)
{
	int rc;

	if (!g_mms_gauge)
		return;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL, full);
	if (rc < 0)
		chg_err("set battery full error, rc=%d\n", rc);
}

bool oplus_gauge_check_chip_is_null(void)
{
	if (!g_mms_gauge) {
		return true;
	} else {
		return false;
	}
}

int oplus_gauge_update_battery_dod0(void)
{
	int rc;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0);
	if (rc < 0)
		chg_err("update battery dod0 error, rc=%d\n", rc);

	return 0;
}

int oplus_gauge_update_soc_smooth_parameter(void)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_mms_gauge is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH);
	if (rc < 0)
		chg_err("update soc smooth parameter, rc=%d\n", rc);

	return rc;
}

int oplus_gauge_get_battery_cb_status(void)
{
	int rc;
	int cb_status;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS, &cb_status);
	if (rc < 0) {
		chg_err("update soc smooth parameter, rc=%d\n", rc);
		return 0;
	}

	return cb_status;
}

int oplus_gauge_get_i2c_err(void)
{
	return 0; /* nick.hu TODO */
}

void oplus_gauge_clear_i2c_err(void)
{
	/* nick.hu TODO */
}

int oplus_gauge_get_passedchg(int *val)
{
	int rc;

	if (!g_mms_gauge)
		return 0;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG, val);
	if (rc < 0) {
		chg_err("get passedchg error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_gauge_dump_register(void)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_mms_gauge is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_REG_DUMP);
	if (rc == -ENOTSUPP)
		rc = 0;

	return rc;
}

int oplus_gauge_lock(void)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_mms_gauge is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_LOCK, true);
	if (rc == -ENOTSUPP)
		rc = 0;

	return rc;
}

int oplus_gauge_unlock(void)
{
	int rc;

	if (g_mms_gauge == NULL) {
		chg_err("g_mms_gauge is NULL\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_LOCK, false);
	if (rc == -ENOTSUPP)
		rc = 0;

	return rc;
}

bool oplus_gauge_is_locked(void)
{
	int rc;
	bool locked;

	if (g_mms_gauge == NULL) {
		chg_err("g_mms_gauge is NULL\n");
		return true;
	}

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_IS_LOCKED, &locked);
	if (rc == -ENOTSUPP)
		return false;
	else if (rc < 0)
		return true;

	return locked;
}

int oplus_gauge_get_batt_num(void)
{
	int rc;
	int num;

	if (!g_mms_gauge)
		return 1;

	rc = oplus_chg_ic_func(g_mms_gauge->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM, &num);
	if (rc < 0) {
		chg_err("can't get battery num, rc=%d\n", rc);
		return 1;
	}

	return num;
}

static int oplus_mms_gauge_set_err_code(struct oplus_mms_gauge *chip,
					unsigned int err_code)
{
	struct mms_msg *msg;
	int rc;

	if (chip->err_code == err_code)
		return 0;

	chip->err_code = err_code;
	pr_info("set err_code=%08x\n", err_code);

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH,
				  GAUGE_ITEM_ERR_CODE);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish notify code msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static int oplus_mms_gauge_virq_register(struct oplus_mms_gauge *chip);
static int oplus_mms_gauge_topic_init(struct oplus_mms_gauge *chip);

static void oplus_mms_gauge_init_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip = container_of(dwork,
		struct oplus_mms_gauge, hal_gauge_init_work);
	struct device_node *node = chip->dev->of_node;
	static int retry = OPLUS_CHG_IC_INIT_RETRY_MAX;
	int rc;

	chip->gauge_ic = of_get_oplus_chg_ic(node, "oplus,gauge_ic", 0);
	if (chip->gauge_ic == NULL) {
		if (retry > 0) {
			retry--;
			schedule_delayed_work(&chip->hal_gauge_init_work,
				msecs_to_jiffies(OPLUS_CHG_IC_INIT_RETRY_DELAY));
			return;
		} else {
			chg_err("oplus,gauge_ic not found\n");
		}
		retry = 0;
		return;
	}

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_INIT);
	if (rc == -EAGAIN) {
		if (retry > 0) {
			retry--;
			schedule_delayed_work(&chip->hal_gauge_init_work,
				msecs_to_jiffies(OPLUS_CHG_IC_INIT_RETRY_DELAY));
			return;
		} else {
			chg_err("gauge_ic init timeout\n");
		}
		retry = 0;
		return;
	} else if (rc < 0) {
		chg_err("gauge_ic init error, rc=%d\n", rc);
		retry = 0;
		return;
	}
	retry = 0;

	rc = oplus_chg_ic_func(chip->gauge_ic,
			       OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
			       &chip->device_type);
	if (rc < 0) {
		chg_err("can't get device type, rc=%d\n", rc);
		chip->device_type = 0;
	}
	rc = oplus_chg_ic_func(chip->gauge_ic,
			       OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
			       &chip->device_type_for_vooc);
	if (rc < 0) {
		chg_err("can't get device type for vooc, rc=%d\n", rc);
		chip->device_type_for_vooc = 0;
	}
	rc = oplus_chg_ic_func(chip->gauge_ic,
					OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
					&chip->hmac);
	if (rc < 0) {
		chg_err("get battery hmac status error, rc=%d\n", rc);
		chip->hmac = 0;
	} else {
		chg_err("hmac=%d\n", chip->hmac);
	}

	oplus_mms_gauge_virq_register(chip);
	g_mms_gauge = chip;

	(void)oplus_mms_gauge_topic_init(chip);
}

static void oplus_mms_gauge_err_analyze(struct oplus_mms_gauge *chip,
					struct oplus_chg_ic_err_msg *msg)
{
	oplus_mms_gauge_set_err_code(chip, chip->err_code | BIT(msg->type));
}

static void oplus_mms_gauge_err_handler_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip = container_of(work, struct oplus_mms_gauge,
						err_handler_work);
	struct oplus_chg_ic_err_msg *msg, *tmp;
	struct list_head msg_list;

	INIT_LIST_HEAD(&msg_list);
	spin_lock(&chip->gauge_ic->err_list_lock);
	if (!list_empty(&chip->gauge_ic->err_list))
		list_replace_init(&chip->gauge_ic->err_list, &msg_list);
	spin_unlock(&chip->gauge_ic->err_list_lock);

	list_for_each_entry_safe(msg, tmp, &msg_list, list) {
		if (is_err_topic_available(chip))
			oplus_mms_publish_ic_err_msg(chip->err_topic,
						     ERR_ITEM_IC, msg);
		oplus_mms_gauge_err_analyze(chip, msg);
		oplus_print_ic_err(msg);
		list_del(&msg->list);
		kfree(msg);
	}
}

static void oplus_mms_gauge_online_handler_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip = container_of(
		work, struct oplus_mms_gauge, online_handler_work);
	struct mms_msg *msg;
	unsigned int err_code;
	int rc;

	err_code = chip->err_code;
	if (err_code & BIT(OPLUS_ERR_CODE_I2C))
		err_code &= ~BIT(OPLUS_ERR_CODE_I2C);
	oplus_mms_gauge_set_err_code(chip, err_code);

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH,
				  GAUGE_ITEM_BATT_EXIST);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish batt exist msg error, rc=%d\n", rc);
		kfree(msg);
	}

	chg_info("batt_exist=%d\n", oplus_gauge_is_exist(chip->gauge_topic));
}

static void oplus_mms_gauge_offline_handler_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip = container_of(
		work, struct oplus_mms_gauge, offline_handler_work);
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH,
				  GAUGE_ITEM_BATT_EXIST);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish batt exist msg error, rc=%d\n", rc);
		kfree(msg);
	}

	chg_info("batt_exist=%d\n", oplus_gauge_is_exist(chip->gauge_topic));
}

static void oplus_mms_gauge_resume_handler_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip = container_of(
		work, struct oplus_mms_gauge, resume_handler_work);
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH,
				  GAUGE_ITEM_RESUME);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish gauge remuse msg error, rc=%d\n", rc);
		kfree(msg);
	}
}

static void oplus_mms_gauge_err_handler(struct oplus_chg_ic_dev *ic_dev,
					void *virq_data)
{
	struct oplus_mms_gauge *chip = virq_data;
	schedule_work(&chip->err_handler_work);
}

static void oplus_mms_gauge_online_handler(struct oplus_chg_ic_dev *ic_dev,
					    void *virq_data)
{
	struct oplus_mms_gauge *chip = virq_data;
	schedule_work(&chip->online_handler_work);
}

static void oplus_mms_gauge_offline_handler(struct oplus_chg_ic_dev *ic_dev,
					    void *virq_data)
{
	struct oplus_mms_gauge *chip = virq_data;
	schedule_work(&chip->offline_handler_work);
}

static void oplus_mms_gauge_resume_handler(struct oplus_chg_ic_dev *ic_dev,
					    void *virq_data)
{
	struct oplus_mms_gauge *chip = virq_data;
	schedule_work(&chip->resume_handler_work);
}

static int oplus_mms_gauge_virq_register(struct oplus_mms_gauge *chip)
{
	int rc;

	rc = oplus_chg_ic_virq_register(chip->gauge_ic, OPLUS_IC_VIRQ_ERR,
		oplus_mms_gauge_err_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d", rc);

	rc = oplus_chg_ic_virq_register(chip->gauge_ic, OPLUS_IC_VIRQ_ONLINE,
		oplus_mms_gauge_online_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_ONLINE error, rc=%d", rc);

	rc = oplus_chg_ic_virq_register(chip->gauge_ic, OPLUS_IC_VIRQ_OFFLINE,
		oplus_mms_gauge_offline_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_OFFLINE error, rc=%d", rc);

	rc = oplus_chg_ic_virq_register(chip->gauge_ic, OPLUS_IC_VIRQ_RESUME,
		oplus_mms_gauge_resume_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_RESUME error, rc=%d", rc);

	return 0;
}

static int oplus_mms_gauge_update_soc(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int soc;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		soc = 50;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC, &soc);
	if (rc < 0) {
		chg_err("get battery soc error, rc=%d\n", rc);
		soc = 50;
	}

end:
	data->intval = soc;
	return 0;
}

static bool is_voocphy_ic_available(struct oplus_mms_gauge *chip)
{
	if (!chip->voocphy_ic)
		chip->voocphy_ic = of_get_oplus_chg_ic(chip->dev->of_node,
						       "oplus,voocphy_ic", 0);

	return !!chip->voocphy_ic;
}

#define VBATT_OVER_THR_MV 4600
#define MAX_CP_GAUGE_VBATT_DIFF 800
static int oplus_mms_gauge_choice_fit_vol(struct oplus_mms_gauge *chip, int gauge_vol)
{
	int ret;
	int cp_vbat;
	int vbatt_ov_thr_mv;

	if (chip->comm_topic)
		vbatt_ov_thr_mv = oplus_comm_get_vbatt_over_threshold(chip->comm_topic);
	else
		vbatt_ov_thr_mv = VBATT_OVER_THR_MV;

	ret = oplus_chg_ic_func(chip->voocphy_ic,
				OPLUS_IC_FUNC_VOOCPHY_GET_CP_VBAT,
				&cp_vbat);
	if (ret < 0) {
		chg_err("can't get cp voltage, rc=%d\n", ret);
		return gauge_vol;
	}

	if (cp_vbat >= vbatt_ov_thr_mv || cp_vbat <= 0 ||
	    abs(cp_vbat - gauge_vol) > MAX_CP_GAUGE_VBATT_DIFF) {
		chg_info("choice gauge voltage as vbat [%d, %d]\n",
			 cp_vbat, gauge_vol);
		return gauge_vol;
	} else {
		chg_info("choice cp voltage as vbat [%d, %d]\n",
			 cp_vbat, gauge_vol);
		return cp_vbat;
	}
}

static int oplus_mms_gauge_update_vol_max(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int vol;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		vol = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX, &vol);
	if (rc < 0) {
		chg_err("get battery voltage max error, rc=%d\n", rc);
		vol = 0;
	}

	if (chip->wired_online && is_voocphy_ic_available(chip))
		vol = oplus_mms_gauge_choice_fit_vol(chip, vol);
end:
	data->intval = vol;
	return 0;
}

static int oplus_mms_gauge_update_vol_min(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int vol;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		vol = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN, &vol);
	if (rc < 0) {
		chg_err("get battery voltage min error, rc=%d\n", rc);
		vol = 0;
	}

	if (chip->wired_online && is_voocphy_ic_available(chip))
		vol = oplus_mms_gauge_choice_fit_vol(chip, vol);
end:
	data->intval = vol;
	return 0;
}

static int oplus_mms_gauge_update_curr(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int curr;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		curr = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR, &curr);
	if (rc < 0) {
		chg_err("get battery current error, rc=%d\n", rc);
		curr = 0;
	}
end:
	data->intval = curr;
	return 0;
}

static int oplus_mms_gauge_update_temp(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int temp;

	if (mms == NULL) {
		chg_err("mms is NULL");
		temp = -40;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	temp = oplus_gauge_get_batt_temperature();
end:
	data->intval = temp;
	return 0;
}

static int oplus_mms_gauge_update_fcc(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int fcc;
	int rc;
	int batt_num = oplus_gauge_get_batt_num();

	if (mms == NULL) {
		chg_err("mms is NULL");
		fcc = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC, &fcc);
	if (rc < 0) {
		chg_err("get battery fcc error, rc=%d\n", rc);
		fcc = 0;
	}
end:
	data->intval = fcc * batt_num;
	return 0;
}

static int oplus_mms_gauge_update_rm(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int rm;
	int rc;
	int batt_num = oplus_gauge_get_batt_num();

	if (mms == NULL) {
		chg_err("mms is NULL");
		rm = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_RM, &rm);
	if (rc < 0) {
		chg_err("get battery remaining capacity error, rc=%d\n", rc);
		rm = 0;
	}
end:
	data->intval = rm * batt_num;
	return 0;
}

static int oplus_mms_gauge_update_cc(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int cc;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		cc = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_CC, &cc);
	if (rc < 0) {
		chg_err("get battery cc error, rc=%d\n", rc);
		cc = 0;
	}
end:
	data->intval = cc;
	return 0;
}

static int oplus_mms_gauge_update_soh(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int soh;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		soh = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH, &soh);
	if (rc < 0) {
		chg_err("get battery soh error, rc=%d\n", rc);
		soh = 0;
	}
end:
	data->intval = soh;
	return 0;
}

static int oplus_mms_gauge_update_exist(struct oplus_mms *mms,
					union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	bool exist;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		exist = false;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	rc = oplus_chg_ic_func(chip->gauge_ic,
			       OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST, &exist);
	if (rc < 0) {
		chg_err("get battery exist status error, rc=%d\n", rc);
		exist = false;
	}
end:
	data->intval = exist;
	return 0;
}

static int oplus_mms_gauge_update_err_code(struct oplus_mms *mms,
					union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	unsigned int err_code;

	if (mms == NULL) {
		chg_err("mms is NULL");
		err_code = 0;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	err_code = chip->err_code;

end:
	data->intval = err_code;
	return 0;
}

static int oplus_mms_gauge_update_hmac(struct oplus_mms *mms,
				       union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	data->intval = chip->hmac;

	return 0;
}

static int oplus_mms_gauge_update_auth(struct oplus_mms *mms,
				       union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	bool auth;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		auth = false;
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	if (chip->bat_volt_different) {
		auth = false;
		goto end;
	}

	rc = oplus_chg_ic_func(chip->gauge_ic,
			       OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH, &auth);
	if (rc < 0) {
		chg_err("get battery authenticate status error, rc=%d\n", rc);
		auth = false;
	}

end:
	data->intval = auth;
	return 0;
}

static void oplus_mms_gauge_update(struct oplus_mms *mms, bool publish)
{
	struct oplus_mms_gauge *chip;
	struct mms_msg *msg;
	int i, rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}
	chip = oplus_mms_get_drvdata(mms);

	(void)oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_UPDATE);

	for (i = 0; i < mms->desc->update_items_num; i++)
		oplus_mms_item_update(mms, mms->desc->update_items[i], true);
	if (publish) {
		msg = oplus_mms_alloc_msg(MSG_TYPE_TIMER, MSG_PRIO_MEDIUM, 0);
		if (msg == NULL) {
			chg_err("alloc msg buf error\n");
			return;
		}
		rc = oplus_mms_publish_msg(mms, msg);
		if (rc < 0) {
			chg_err("publish msg error, rc=%d\n", rc);
			kfree(msg);
			return;
		}
	}
}

static struct mms_item oplus_mms_gauge_item[] = {
	{
		.desc = {
			.item_id = GAUGE_ITEM_SOC,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_soc,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_VOL_MAX,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_vol_max,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_VOL_MIN,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_vol_min,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_CURR,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_curr,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_TEMP,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_temp,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_FCC,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_fcc,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_RM,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_rm,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_CC,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_cc,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_SOH,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_soh,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_BATT_EXIST,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_exist,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_ERR_CODE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_err_code,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_RESUME,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = NULL,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_HMAC,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_hmac,
		}
	}, {
		.desc = {
			.item_id = GAUGE_ITEM_AUTH,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_mms_gauge_update_auth,
		}
	}
};

static const u32 oplus_mms_gauge_update_item[] = {
	GAUGE_ITEM_SOC,
	GAUGE_ITEM_VOL_MAX,
	GAUGE_ITEM_VOL_MIN,
	GAUGE_ITEM_CURR,
	GAUGE_ITEM_TEMP,
	GAUGE_ITEM_FCC,
	GAUGE_ITEM_CC,
	GAUGE_ITEM_SOH,
	GAUGE_ITEM_RM,
};

static const struct oplus_mms_desc oplus_mms_gauge_desc = {
	.name = "gauge",
	.type = OPLUS_MMS_TYPE_GAUGE,
	.item_table = oplus_mms_gauge_item,
	.item_num = ARRAY_SIZE(oplus_mms_gauge_item),
	.update_items = oplus_mms_gauge_update_item,
	.update_items_num = ARRAY_SIZE(oplus_mms_gauge_update_item),
	.update_interval = 10000, /* ms */
	.update = oplus_mms_gauge_update,
};

#define CHARGE_UPDATE_INTERVAL		5000
static void oplus_mms_gauge_update_change_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip =
		container_of(work, struct oplus_mms_gauge, update_change_work);

/* TODO: factory mode app adaptation
	if (chip->factory_test_mode)
		vote(chip->gauge_update_votable, FACTORY_TEST_VOTER, true, 500, false);
	else
		vote(chip->gauge_update_votable, FACTORY_TEST_VOTER, false, 0, false);
*/

	if (chip->wired_online || chip->wls_online)
		vote(chip->gauge_update_votable, USER_VOTER, true, CHARGE_UPDATE_INTERVAL, false);
	else
		vote(chip->gauge_update_votable, USER_VOTER, false, 0, false);
}

static void oplus_mms_gauge_comm_subs_callback(struct mms_subscribe *subs,
					       enum mms_msg_type type, u32 id)
{
	struct oplus_mms_gauge *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_FACTORY_TEST:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						true);
			chip->factory_test_mode = data.intval;
			schedule_work(&chip->update_change_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_mms_gauge_subscribe_comm_topic(struct oplus_mms *topic,
						 void *prv_data)
{
	struct oplus_mms_gauge *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->comm_topic = topic;
	chip->comm_subs =
		oplus_mms_subscribe(chip->comm_topic, chip,
				    oplus_mms_gauge_comm_subs_callback,
				    "mms_gauge");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe common topic error, rc=%ld\n",
			PTR_ERR(chip->comm_subs));
		return;
	}

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_FACTORY_TEST, &data,
				true);
	chip->factory_test_mode = data.intval;
	schedule_work(&chip->update_change_work);
}

static void oplus_mms_gauge_set_curve_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip =
		container_of(work, struct oplus_mms_gauge, gauge_set_curve_work);
	int type;
	unsigned int adapter_id;

	type = oplus_wired_get_chg_type();
	if ((type != OPLUS_CHG_USB_TYPE_VOOC) &&
	    (type != OPLUS_CHG_USB_TYPE_SVOOC)) {
		if (sid_to_adapter_chg_type(chip->vooc_sid) == CHARGER_TYPE_VOOC)
			type = OPLUS_CHG_USB_TYPE_VOOC;
		else if (sid_to_adapter_chg_type(chip->vooc_sid) == CHARGER_TYPE_SVOOC)
			type = OPLUS_CHG_USB_TYPE_SVOOC;
	}

	switch (type) {
	case OPLUS_CHG_USB_TYPE_UNKNOWN:
		return;
	case OPLUS_CHG_USB_TYPE_QC2:
	case OPLUS_CHG_USB_TYPE_QC3:
		type = CHARGER_SUBTYPE_QC;
		break;
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_DRP:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		type = CHARGER_SUBTYPE_PD;
		break;
	case OPLUS_CHG_USB_TYPE_VOOC:
		type = CHARGER_SUBTYPE_FASTCHG_VOOC;
		break;
	case OPLUS_CHG_USB_TYPE_SVOOC:
		type = CHARGER_SUBTYPE_FASTCHG_SVOOC;
		break;
	default:
		type = CHARGER_SUBTYPE_DEFAULT;
		break;
	}

	adapter_id = sid_to_adapter_id(chip->vooc_sid);
	(void)oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_BATTERY_CURVE,
				type, adapter_id, chip->pd_svooc);
}

static void oplus_mms_gauge_wired_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id)
{
	struct oplus_mms_gauge *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			oplus_mms_get_item_data(chip->wired_topic, id, &data,
						false);
			chip->wired_online = data.intval;
			schedule_work(&chip->update_change_work);
			break;
		case WIRED_ITEM_CHG_TYPE:
			if (chip->wired_online && is_voocphy_ic_available(chip))
				schedule_work(&chip->gauge_set_curve_work);
			break;
		case WIRED_TIME_ABNORMAL_ADAPTER:
			oplus_mms_get_item_data(chip->wired_topic,
						WIRED_TIME_ABNORMAL_ADAPTER,
						&data, false);
			chip->pd_svooc = data.intval;
			if (chip->wired_online && is_voocphy_ic_available(chip))
				schedule_work(&chip->gauge_set_curve_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_mms_gauge_subscribe_wired_topic(struct oplus_mms *topic,
						  void *prv_data)
{
	struct oplus_mms_gauge *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    oplus_mms_gauge_wired_subs_callback,
				    "mms_gauge");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				true);
	chip->wired_online = !!data.intval;
	schedule_work(&chip->update_change_work);
	if (chip->wired_online && is_voocphy_ic_available(chip)) {
		oplus_mms_get_item_data(chip->wired_topic,
					WIRED_TIME_ABNORMAL_ADAPTER,
					&data, false);
		chip->pd_svooc = data.intval;
		schedule_work(&chip->gauge_set_curve_work);
	}
}

static int oplus_mms_gauge_push_hmac(struct oplus_mms_gauge *chip)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH,
				  GAUGE_ITEM_HMAC);
	if (msg == NULL) {
		chg_err("alloc battery hmac msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish battery hmac msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static int oplus_mms_gauge_push_auth(struct oplus_mms_gauge *chip)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH,
				  GAUGE_ITEM_AUTH);
	if (msg == NULL) {
		chg_err("alloc battery auth msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish battery auth msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

#define ALLOW_DIFF_VALUE_MV	1000
#define BATT_VOL_ERR_COUNT_MAX	5
static int oplus_mms_gauge_check_batt_vol_diff(struct oplus_mms_gauge *chip)
{
	int vbat_max;
	int vbat_min;
	int batt_num = oplus_gauge_get_batt_num();
	struct votable *chg_disable_votable;
	union mms_msg_data data = { 0 };
	int rc;

	/* just support 2S battery */
	if (batt_num != 2)
		return 0;

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get vbat_max, rc=%d\n", rc);
		vbat_max = 0;
	} else {
		vbat_max = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		vbat_min = 0;
	} else {
		vbat_min = data.intval;
	}

	if (abs(vbat_max - vbat_min) > ALLOW_DIFF_VALUE_MV) {
		chip->check_batt_vol_count++;
		if (chip->check_batt_vol_count > BATT_VOL_ERR_COUNT_MAX) {
			chg_disable_votable = find_votable("CHG_DISABLE");
			if (chg_disable_votable)
				vote(chg_disable_votable, VOL_DIFF_VOTER,
				     true, 1, false);
			if (!chip->bat_volt_different)
				oplus_mms_gauge_push_auth(chip);
			chip->bat_volt_different = true;
			chip->check_batt_vol_count = 0;
			chg_info("BATTERY_SOFT_DIFF_VOLTAGE disable chg\n");
		}
	} else {
		if (chip->bat_volt_different) {
			chip->check_batt_vol_count++;
			if (chip->check_batt_vol_count >
			    BATT_VOL_ERR_COUNT_MAX) {
				if (chip->bat_volt_different) {
					chip->bat_volt_different = false;
					oplus_mms_gauge_push_auth(chip);
				}
				chip->check_batt_vol_count = 0;
				chg_disable_votable = find_votable("CHG_DISABLE");
				if (chg_disable_votable)
					vote(chg_disable_votable,
					     VOL_DIFF_VOTER, false, 0, false);
				chg_info(
					"Recovery BATTERY_SOFT_DIFF_VOLTAGE\n");
			}
		} else {
			chip->check_batt_vol_count = 0;
		}
	}

	return 0;
}

static void oplus_mms_gauge_gauge_update_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip =
		container_of(work, struct oplus_mms_gauge, gauge_update_work);
	
	oplus_mms_gauge_check_batt_vol_diff(chip);
}

static void oplus_mms_gauge_gauge_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id)
{
	struct oplus_mms_gauge *chip = subs->priv_data;

	switch (type) {
	case MSG_TYPE_TIMER:
		schedule_work(&chip->gauge_update_work);
		break;
	default:
		break;
	}
}

static int oplus_mms_gauge_subscribe_gauge_topic(struct oplus_mms_gauge *chip)
{
	chip->gauge_subs =
		oplus_mms_subscribe(chip->gauge_topic, chip,
				    oplus_mms_gauge_gauge_subs_callback,
				    "mms_gauge");
	if (IS_ERR_OR_NULL(chip->gauge_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->gauge_subs));
		return PTR_ERR(chip->gauge_subs);
	}

	return 0;
}

static void oplus_mms_gauge_vooc_subs_callback(struct mms_subscribe *subs,
					       enum mms_msg_type type, u32 id)
{
	struct oplus_mms_gauge *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case VOOC_ITEM_SID:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_sid = (unsigned int)data.intval;
			if (chip->vooc_sid && chip->wired_online &&
			    is_voocphy_ic_available(chip))
				schedule_work(&chip->gauge_set_curve_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}
static void oplus_mms_gauge_subscribe_vooc_topic(struct oplus_mms *topic,
						 void *prv_data)
{
	struct oplus_mms_gauge *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->vooc_topic = topic;
	chip->vooc_subs =
		oplus_mms_subscribe(chip->vooc_topic, chip,
				    oplus_mms_gauge_vooc_subs_callback,
				    "mms_gauge");
	if (IS_ERR_OR_NULL(chip->vooc_subs)) {
		chg_err("subscribe vooc topic error, rc=%ld\n",
			PTR_ERR(chip->vooc_subs));
		return;
	}

	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_SID, &data,
				true);
	chip->vooc_sid = (unsigned int)data.intval;
	if (chip->wired_online && is_voocphy_ic_available(chip))
		schedule_work(&chip->gauge_set_curve_work);
}

static int oplus_mms_gauge_topic_init(struct oplus_mms_gauge *chip)
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
		if (rc < 0) {
			chg_err("can't read oplus,topic-update-interval, rc=%d\n",
				rc);
			mms_cfg.update_interval = 0;
		}
	}

	chip->gauge_topic = devm_oplus_mms_register(chip->dev, &oplus_mms_gauge_desc, &mms_cfg);
	if (IS_ERR(chip->gauge_topic)) {
		chg_err("Couldn't register gauge topic\n");
		rc = PTR_ERR(chip->gauge_topic);
		return rc;
	}
	vote(chip->gauge_update_votable, DEF_VOTER, true, oplus_mms_gauge_desc.update_interval, false);

	oplus_mms_gauge_subscribe_gauge_topic(chip);
	oplus_mms_gauge_push_hmac(chip);
	oplus_mms_wait_topic("common", oplus_mms_gauge_subscribe_comm_topic, chip);
	oplus_mms_wait_topic("wired", oplus_mms_gauge_subscribe_wired_topic, chip);
	oplus_mms_wait_topic("vooc", oplus_mms_gauge_subscribe_vooc_topic, chip);

	return 0;
}

static int oplus_gauge_update_vote_callback(struct votable *votable, void *data,
					    int time_ms, const char *client,
					    bool step)
{
	struct oplus_mms_gauge *chip = data;
	int rc;

	if (time_ms < 0) {
		chg_err("time_ms=%d, restore default publish interval\n", time_ms);
		oplus_mms_restore_publish(chip->gauge_topic);
		return 0;
	}

	rc = oplus_mms_set_publish_interval(chip->gauge_topic, time_ms);
	if (rc < 0)
		chg_err("can't set gauge publish interval to %d\n", time_ms);
	else
		chg_err("set gauge publish interval to %d\n", time_ms);

	return rc;
}

static int oplus_mms_gauge_probe(struct platform_device *pdev)
{
	struct oplus_mms_gauge *chip;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_mms_gauge),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);

	chip->gauge_update_votable =
		create_votable("GAUGE_UPDATE", VOTE_MIN,
			       oplus_gauge_update_vote_callback, chip);
	if (IS_ERR(chip->gauge_update_votable)) {
		rc = PTR_ERR(chip->gauge_update_votable);
		chip->gauge_update_votable = NULL;
		goto create_vote_err;
	}

	INIT_DELAYED_WORK(&chip->hal_gauge_init_work, oplus_mms_gauge_init_work);
	INIT_WORK(&chip->err_handler_work, oplus_mms_gauge_err_handler_work);
	INIT_WORK(&chip->online_handler_work, oplus_mms_gauge_online_handler_work);
	INIT_WORK(&chip->offline_handler_work, oplus_mms_gauge_offline_handler_work);
	INIT_WORK(&chip->resume_handler_work, oplus_mms_gauge_resume_handler_work);
	INIT_WORK(&chip->update_change_work, oplus_mms_gauge_update_change_work);
	INIT_WORK(&chip->gauge_update_work, oplus_mms_gauge_gauge_update_work);
	INIT_WORK(&chip->gauge_set_curve_work, oplus_mms_gauge_set_curve_work);

	schedule_delayed_work(&chip->hal_gauge_init_work, 0);

	chg_info("probe success\n");
	return 0;

create_vote_err:
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);
	return rc;
}

static int oplus_mms_gauge_remove(struct platform_device *pdev)
{
	struct oplus_mms_gauge *chip = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(chip->comm_subs))
		oplus_mms_unsubscribe(chip->comm_subs);
	if (!IS_ERR_OR_NULL(chip->wired_subs))
		oplus_mms_unsubscribe(chip->wired_subs);
	if (!IS_ERR_OR_NULL(chip->gauge_subs))
		oplus_mms_unsubscribe(chip->gauge_subs);

	destroy_votable(chip->gauge_update_votable);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id oplus_mms_gauge_match[] = {
	{ .compatible = "oplus,mms_gauge" },
	{},
};

static struct platform_driver oplus_mms_gauge_driver = {
	.driver		= {
		.name = "oplus-mms_gauge",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_mms_gauge_match),
	},
	.probe		= oplus_mms_gauge_probe,
	.remove		= oplus_mms_gauge_remove,
};

static __init int oplus_mms_gauge_init(void)
{
	return platform_driver_register(&oplus_mms_gauge_driver);
}

static __exit void oplus_mms_gauge_exit(void)
{
	platform_driver_unregister(&oplus_mms_gauge_driver);
}

oplus_chg_module_register(oplus_mms_gauge);
