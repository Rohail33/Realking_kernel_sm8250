// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/timer.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_battery.h>
#else
#include <mt-plat/v1/charger_type.h>
#include <mt-plat/v1/mtk_battery.h>
#endif

#include <mt-plat/mtk_boot.h>
#include <pmic.h>
#include <mtk_gauge_time_service.h>

//CONFIG_OPLUS_TI_CHGIC_NEW
//#include "mtk_charger_intf.h"
//#include "mtk_charger_init.h"


#ifdef OPLUS_FEATURE_CHG_BASIC
//====================================================================//
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_adapter.h"
#include "../oplus_short.h"
#include "../oplus_configfs.h"
#include "../oplus_chg_track.h"

//#include "../gauge_ic/oplus_bq27541.h"
#include "op_charge.h"
#include "../../../misc/mediatek/typec/tcpc/inc/tcpci.h"
#include "../../../misc/mediatek/pmic/mt6360/inc/mt6360_pmu.h"

#include "oplus_bq25601d.h"
#include "oplus_bq2591x_reg.h"
#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/iio/consumer.h>
#endif
#include "oplus_charge_pump.h"
#include "../voocphy/oplus_voocphy.h"
#include <tcpm.h>

static bool em_mode = false;
#ifdef OPLUS_FEATURE_CHG_BASIC
#define OPLUS_FLASHLIGHT_DEFALUT_TEMP_UV 1636364
static int flashlight_temp_uv_cal = OPLUS_FLASHLIGHT_DEFALUT_TEMP_UV;
static int chargeric_temp_cal = 250;
static int battery_btb_temp_cal = 25;
#endif
static bool is_svooc65_project(void);
static bool is_vooc30_project(void);
static bool is_usbtemp_thread_init_done = false;
static int charger_ic__det_flag = 0;
struct oplus_chg_chip *g_oplus_chip = NULL;
extern struct oplus_chg_operations * oplus_get_chg_ops(void);
extern int battery_meter_get_charger_voltage(void);
static int oplus_mt6360_reset_charger(void);
static int oplus_mt6360_enable_charging(void);
static int oplus_mt6360_disable_charging(void);
static int oplus_mt6360_float_voltage_write(int vflaot_mv);
static int oplus_mt6360_suspend_charger(void);
static int oplus_mt6360_unsuspend_charger(void);
static int oplus_mt6360_charging_current_write_fast(int chg_curr);
static int oplus_mt6360_set_termchg_current(int term_curr);
static int oplus_mt6360_set_rechg_voltage(int rechg_mv);
int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip);
extern int oplus_usbtemp_monitor_common(void *data);
extern void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip);
extern bool set_charge_power_sel(int);
#ifdef OPLUS_FEATURE_CHG_BASIC
static void get_chargeric_temp(struct charger_data *pdata);
static int oplus_chg_get_chargeric_temp_val(void);
int oplus_chg_get_chargeric_temp_cal(void);
int oplus_chg_get_battery_btb_temp_cal(void);
static bool oplus_ntc_ctrl_is_support(void);
struct delayed_work adc_switch_update_work;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#define URGENT_CHECK_THRESHOLD		3
#define NORMAL_CHECK_UPDATE_INTERVAL	5
static struct delayed_work fg_update_work;
static int sleep_time;
static unsigned long suspend_tm_sec = 0;
#endif
static bool oplus_chg_is_support_qcpd(void);
static int oplus_ntcctrl_gpio_init_cal(struct oplus_chg_chip *chip);
#endif

static struct task_struct *oplus_usbtemp_kthread;
/* static struct timer_list usbtemp_timer; */
struct delayed_work usbtemp_recover_work;

static DECLARE_WAIT_QUEUE_HEAD(oplus_usbtemp_wq);
void oplus_set_otg_switch_status(bool value);
void oplus_wake_up_usbtemp_thread(void);

#ifdef CONFIG_OPLUS_CHARGER_MTK
struct mtk_hv_flashled_pinctrl {
	int chgvin_gpio;
	int pmic_chgfunc_gpio;
	int bc1_2_done;
	bool hv_flashled_support;
	struct mutex chgvin_mutex;

	struct pinctrl *pinctrl;
	struct pinctrl_state *chgvin_enable;
	struct pinctrl_state *chgvin_disable;
	struct pinctrl_state *pmic_chgfunc_enable;
	struct pinctrl_state *pmic_chgfunc_disable;
};

struct mtk_hv_flashled_pinctrl mtkhv_flashled_pinctrl;

#define OPLUS_DIVIDER_WORK_MODE_AUTO			1
#define OPLUS_DIVIDER_WORK_MODE_FIXED		0
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_set_typec_cc_open(void);
void oplus_usbtemp_recover_cc_open(void);
#endif /* OPLUS_FEATURE_CHG_BASIC */
bool oplus_check_pdphy_ready(void);
bool oplus_check_pd_state_ready(void);

//====================================================================//
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip);
bool oplus_ccdetect_support_check(void);
int oplus_chg_ccdetect_parse_dt(struct oplus_chg_chip *chip);
#endif /* OPLUS_FEATURE_CHG_BASIC */

//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
#define USB_TEMP_HIGH		0x01//bit0
#define USB_WATER_DETECT	0x02//bit1
#define USB_RESERVE2		0x04//bit2
#define USB_RESERVE3		0x08//bit3
#define USB_RESERVE4		0x10//bit4
#define USB_DONOT_USE		0x80000000 /* bit31 */
#define DEFAULT_PULL_UP_R0      100000 /*pull up 100K*/
#define PULL_UP_R0_390K         390000 /*Pull up 390K*/
static int usb_status = 0;
static int g_rap_pull_up_r0 = DEFAULT_PULL_UP_R0;/*pull_up_r */

#define AICL_POINT_VOL_9V 7600
#define AICL_POINT_VOL_5V 4140
#define HW_AICL_POINT_VOL_5V_PHASE1 4440
#define HW_AICL_POINT_VOL_5V_PHASE2 4520
#define SW_AICL_POINT_VOL_5V_PHASE1 4500
#define SW_AICL_POINT_VOL_5V_PHASE2 4535
#define DELAY_FOR_SWITCH_EFFECT_30MS 30


extern int oplus_usbtemp_monitor_common_new_method(void *data);

static void oplus_set_usb_status(int status)
{
	usb_status = usb_status | status;
}

static void oplus_clear_usb_status(int status)
{
	if (g_oplus_chip->usb_status == USB_TEMP_HIGH) {
		g_oplus_chip->usb_status = g_oplus_chip->usb_status & (~USB_TEMP_HIGH);
	}
	usb_status = usb_status & (~status);
}

int oplus_get_usb_status(void)
{
	if (g_oplus_chip->usb_status == USB_TEMP_HIGH) {
		return g_oplus_chip->usb_status;
	} else {
		return usb_status;
	}
}

int is_svooc65_cfg = false;
static bool is_svooc65_project(void)
{
	return is_svooc65_cfg;
}

int is_vooc30_cfg = false;
static bool is_vooc30_project(void)
{
	return is_vooc30_cfg;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//

static struct charger_manager *pinfo;
static struct list_head consumer_head = LIST_HEAD_INIT(consumer_head);
static DEFINE_MUTEX(consumer_mutex);

#ifdef OPLUS_FEATURE_CHG_BASIC
bool mtk_is_TA_support_pd_pps(struct charger_manager *pinfo)
{
	if (pinfo->enable_pe_4 == false && pinfo->enable_pe_5 == false)
		return false;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return true;
	return false;
}
#endif

bool is_power_path_supported(void)
{
	if (pinfo == NULL)
		return false;

	if (pinfo->data.power_path_support == true)
		return true;

	return false;
}

bool is_disable_charger(void)
{
	if (pinfo == NULL)
		return true;

	if (pinfo->disable_charger == true || IS_ENABLED(CONFIG_POWER_EXT))
		return true;
	else
		return false;
}

void BATTERY_SetUSBState(int usb_state_value)
{
	if (is_disable_charger()) {
		chr_err("[%s] in FPGA/EVB, no service\n", __func__);
	} else {
		if ((usb_state_value < USB_SUSPEND) ||
			((usb_state_value > USB_CONFIGURED))) {
			chr_err("%s Fail! Restore to default value\n",
				__func__);
			usb_state_value = USB_UNCONFIGURED;
		} else {
			chr_err("%s Success! Set %d\n", __func__,
				usb_state_value);
			if (pinfo)
				pinfo->usb_state = usb_state_value;
		}
	}
}

unsigned int set_chr_input_current_limit(int current_limit)
{
	return 500;
}

int get_chr_temperature(int *min_temp, int *max_temp)
{
	*min_temp = 25;
	*max_temp = 30;

	return 0;
}

int set_chr_boost_current_limit(unsigned int current_limit)
{
	return 0;
}

int set_chr_enable_otg(unsigned int enable)
{
	return 0;
}

int mtk_chr_is_charger_exist(unsigned char *exist)
{
	if (mt_get_charger_type() == CHARGER_UNKNOWN)
		*exist = 0;
	else
		*exist = 1;
	return 0;
}

/*=============== fix me==================*/
int chargerlog_level = CHRLOG_ERROR_LEVEL;

int chr_get_debug_level(void)
{
	return chargerlog_level;
}

#ifdef MTK_CHARGER_EXP
#include <linux/string.h>

char chargerlog[1000];
#define LOG_LENGTH 500
int chargerlog_level = 10;
int chargerlogIdx;

int charger_get_debug_level(void)
{
	return chargerlog_level;
}

void charger_log(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsprintf(chargerlog + chargerlogIdx, fmt, args);
	va_end(args);
	chargerlogIdx = strlen(chargerlog);
	if (chargerlogIdx >= LOG_LENGTH) {
		chr_err("%s", chargerlog);
		chargerlogIdx = 0;
		memset(chargerlog, 0, 1000);
	}
}

void charger_log_flash(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsprintf(chargerlog + chargerlogIdx, fmt, args);
	va_end(args);
	chr_err("%s", chargerlog);
	chargerlogIdx = 0;
	memset(chargerlog, 0, 1000);
}
#endif

void _wake_up_charger(struct charger_manager *info)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
		return;
#else
	unsigned long flags;

	if (info == NULL)
		return;

	spin_lock_irqsave(&info->slock, flags);
	if (!info->charger_wakelock.active)
		__pm_stay_awake(&info->charger_wakelock);
	spin_unlock_irqrestore(&info->slock, flags);
	info->charger_thread_timeout = true;
	wake_up(&info->wait_que);
#endif
}

/* charger_manager ops  */
static int _mtk_charger_change_current_setting(struct charger_manager *info)
{
	if (info != NULL && info->change_current_setting)
		return info->change_current_setting(info);

	return 0;
}

static int _mtk_charger_do_charging(struct charger_manager *info, bool en)
{
	if (info != NULL && info->do_charging)
		info->do_charging(info, en);
	return 0;
}
/* charger_manager ops end */


/* user interface */
struct charger_consumer *charger_manager_get_by_name(struct device *dev,
	const char *name)
{
	struct charger_consumer *puser;

	puser = kzalloc(sizeof(struct charger_consumer), GFP_KERNEL);
	if (puser == NULL)
		return NULL;

	mutex_lock(&consumer_mutex);
	puser->dev = dev;

	list_add(&puser->list, &consumer_head);
	if (pinfo != NULL)
		puser->cm = pinfo;

	mutex_unlock(&consumer_mutex);

	return puser;
}
EXPORT_SYMBOL(charger_manager_get_by_name);

int charger_manager_enable_high_voltage_charging(
			struct charger_consumer *consumer, bool en)
{
	struct charger_manager *info = consumer->cm;
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;

	if (!info)
		return -EINVAL;

	pr_debug("[%s] %s, %d\n", __func__, dev_name(consumer->dev), en);

	if (!en && consumer->hv_charging_disabled == false)
		consumer->hv_charging_disabled = true;
	else if (en && consumer->hv_charging_disabled == true)
		consumer->hv_charging_disabled = false;
	else {
		pr_info("[%s] already set: %d %d\n", __func__,
			consumer->hv_charging_disabled, en);
		return 0;
	}

	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		if (ptr->hv_charging_disabled == true) {
			info->enable_hv_charging = false;
			break;
		}
		if (list_is_last(pos, phead))
			info->enable_hv_charging = true;
	}
	mutex_unlock(&consumer_mutex);

	pr_info("%s: user: %s, en = %d\n", __func__, dev_name(consumer->dev),
		info->enable_hv_charging);

	if (mtk_pe50_get_is_connect(info) && !info->enable_hv_charging)
		mtk_pe50_stop_algo(info, true);

	_wake_up_charger(info);

	return 0;
}
EXPORT_SYMBOL(charger_manager_enable_high_voltage_charging);

int charger_manager_enable_power_path(struct charger_consumer *consumer,
	int idx, bool en)
{
	int ret = 0;
	bool is_en = true;
	struct charger_manager *info = consumer->cm;
	struct charger_device *chg_dev;

#ifdef OPLUS_FEATURE_CHG_BASIC
	return 0;
#endif

	if (!info)
		return -EINVAL;

	switch (idx) {
	case MAIN_CHARGER:
		chg_dev = info->chg1_dev;
		break;
	case SLAVE_CHARGER:
		chg_dev = info->chg2_dev;
		break;
	default:
		return -EINVAL;
	}

	ret = charger_dev_is_powerpath_enabled(chg_dev, &is_en);
	if (ret < 0) {
		chr_err("%s: get is power path enabled failed\n", __func__);
		return ret;
	}
	if (is_en == en) {
		chr_err("%s: power path is already en = %d\n", __func__, is_en);
		return 0;
	}

	pr_info("%s: enable power path = %d\n", __func__, en);
	return charger_dev_enable_powerpath(chg_dev, en);
}

static int _charger_manager_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	struct charger_manager *info = consumer->cm;

	chr_err("%s: dev:%s idx:%d en:%d\n", __func__, dev_name(consumer->dev),
		idx, en);

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		if (en == false) {
			_mtk_charger_do_charging(info, en);
			pdata->disable_charging_count++;
		} else {
			if (pdata->disable_charging_count == 1) {
				_mtk_charger_do_charging(info, en);
				pdata->disable_charging_count = 0;
			} else if (pdata->disable_charging_count > 1)
				pdata->disable_charging_count--;
		}
		chr_err("%s: dev:%s idx:%d en:%d cnt:%d\n", __func__,
			dev_name(consumer->dev), idx, en,
			pdata->disable_charging_count);

		return 0;
	}
	return -EBUSY;

}

int charger_manager_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	struct charger_manager *info = consumer->cm;
	int ret = 0;

	mutex_lock(&info->charger_lock);
	ret = _charger_manager_enable_charging(consumer, idx, en);
	mutex_unlock(&info->charger_lock);
	return ret;
}

int charger_manager_set_input_current_limit(struct charger_consumer *consumer,
	int idx, int input_current)
{
	struct charger_manager *info = consumer->cm;

#ifdef OPLUS_FEATURE_CHG_BASIC
	return 0;
#endif

	if (info != NULL) {
		struct charger_data *pdata;

		if (info->data.parallel_vbus) {
			if (idx == TOTAL_CHARGER) {
				info->chg1_data.thermal_input_current_limit =
					input_current;
				info->chg2_data.thermal_input_current_limit =
					input_current;
			} else
				return -ENOTSUPP;
		} else {
			if (idx == MAIN_CHARGER)
				pdata = &info->chg1_data;
			else if (idx == SLAVE_CHARGER)
				pdata = &info->chg2_data;
			else
				return -ENOTSUPP;
			pdata->thermal_input_current_limit = input_current;
		}

		chr_err("%s: dev:%s idx:%d en:%d\n", __func__,
			dev_name(consumer->dev), idx, input_current);
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_set_charging_current_limit(
	struct charger_consumer *consumer, int idx, int charging_current)
{
	struct charger_manager *info = consumer->cm;

#ifdef OPLUS_FEATURE_CHG_BASIC
	return 0;
#endif

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		pdata->thermal_charging_current_limit = charging_current;
		chr_err("%s: dev:%s idx:%d en:%d\n", __func__,
			dev_name(consumer->dev), idx, charging_current);
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_get_charger_temperature(struct charger_consumer *consumer,
	int idx, int *tchg_min,	int *tchg_max)
{
	struct charger_manager *info = consumer->cm;
	int chargeric_temp_thermal = 0;

	if (info != NULL) {
		struct charger_data *pdata;

#ifndef OPLUS_FEATURE_CHG_BASIC
		if (!upmu_get_rgs_chrdet()) {
			pr_err("[%s] No cable in, skip it\n", __func__);
			*tchg_min = -127;
			*tchg_max = -127;
			return -EINVAL;
		}
#endif

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		if (!pdata) {
                pr_err("%s, pdata null\n", __func__);
                return -1;
        }

		if (oplus_chg_get_voocphy_support() && oplus_ntc_ctrl_is_support()) {
			chr_debug("chargeric use adc ctrl!\n");
			//oplus_chg_adc_switch_ctrl();
			chargeric_temp_thermal = oplus_chg_get_chargeric_temp_cal();
		} else {
			get_chargeric_temp(pdata);
		}

		consumer->support_ntc_01c_precision = info->support_ntc_01c_precision;
		chr_debug("%s: support_ntc_01c_precision: %d\n", __func__, info->support_ntc_01c_precision);

		if (oplus_chg_get_voocphy_support() && oplus_ntc_ctrl_is_support()) {
			pdata->junction_temp_min = chargeric_temp_thermal;
			pdata->junction_temp_max = chargeric_temp_thermal;
			*tchg_min = chargeric_temp_thermal;
			*tchg_max = chargeric_temp_thermal;

			*tchg_min = pdata->junction_temp_min;
			*tchg_max = pdata->junction_temp_max;

			return 0;
		} else {
			pdata->junction_temp_min = pdata->chargeric_temp;
			pdata->junction_temp_max = pdata->chargeric_temp;
			*tchg_min = pdata->chargeric_temp;
			*tchg_max = pdata->chargeric_temp;

			*tchg_min = pdata->junction_temp_min;
			*tchg_max = pdata->junction_temp_max;

			return 0;
		}

	}
	return -EBUSY;
}

int charger_manager_get_current_charging_type(struct charger_consumer *consumer)
{
	struct charger_manager *info = consumer->cm;

	if (info != NULL) {
		if (mtk_pe20_get_is_connect(info))
			return 2;
	}

	return 0;
}

int charger_manager_get_zcv(struct charger_consumer *consumer, int idx, u32 *uV)
{
	struct charger_manager *info = consumer->cm;
	int ret = 0;
	struct charger_device *pchg;


	if (info != NULL) {
		if (idx == MAIN_CHARGER) {
			pchg = info->chg1_dev;
			ret = charger_dev_get_zcv(pchg, uV);
		} else if (idx == SLAVE_CHARGER) {
			pchg = info->chg2_dev;
			ret = charger_dev_get_zcv(pchg, uV);
		} else
			ret = -1;

	} else {
		chr_err("%s info is null\n", __func__);
	}
	chr_err("%s zcv:%d ret:%d\n", __func__, *uV, ret);

	return 0;
}
#if 0
int charger_manager_enable_kpoc_shutdown(struct charger_consumer *consumer,
	bool en)
{
#ifndef OPLUS_FEATURE_CHG_BASIC
	struct charger_manager *info = consumer->cm;

	if (en)
		atomic_set(&info->enable_kpoc_shdn, 1);
	else
		atomic_set(&info->enable_kpoc_shdn, 0);
#endif /* OPLUS_FEATURE_CHG_BASIC */
	return 0;
}
#endif
int charger_manager_enable_chg_type_det(struct charger_consumer *consumer,
	bool en)
{
	struct charger_manager *info = consumer->cm;
	struct charger_device *chg_dev;
	int ret = 0;

	if (info != NULL) {
		switch (info->data.bc12_charger) {
		case MAIN_CHARGER:
			chg_dev = info->chg1_dev;
			break;
		case SLAVE_CHARGER:
			chg_dev = info->chg2_dev;
			break;
		default:
			chg_dev = info->chg1_dev;
			chr_err("%s: invalid number, use main charger as default\n",
				__func__);
			break;
		}

		chr_err("%s: chg%d is doing bc12\n", __func__,
			info->data.bc12_charger + 1);
		ret = charger_dev_enable_chg_type_det(chg_dev, en);
		if (ret < 0) {
			chr_err("%s: en chgdet fail, en = %d\n", __func__, en);
			return ret;
		}
	} else {
		chr_err("%s: charger_manager is null\n", __func__);
	}


	return 0;
}

int register_charger_manager_notifier(struct charger_consumer *consumer,
	struct notifier_block *nb)
{
	int ret = 0;
	struct charger_manager *info = consumer->cm;


	mutex_lock(&consumer_mutex);
	if (info != NULL)
		ret = srcu_notifier_chain_register(&info->evt_nh, nb);
	else
		consumer->pnb = nb;
	mutex_unlock(&consumer_mutex);

	return ret;
}

int unregister_charger_manager_notifier(struct charger_consumer *consumer,
				struct notifier_block *nb)
{
	int ret = 0;
	struct charger_manager *info = consumer->cm;

	mutex_lock(&consumer_mutex);
	if (info != NULL)
		ret = srcu_notifier_chain_unregister(&info->evt_nh, nb);
	else
		consumer->pnb = NULL;
	mutex_unlock(&consumer_mutex);

	return ret;
}

/* internal algorithm common function */
bool is_dual_charger_supported(struct charger_manager *info)
{
	if (info->chg2_dev == NULL)
		return false;
	return true;
}

int charger_enable_vbus_ovp(struct charger_manager *pinfo, bool enable)
{
	int ret = 0;
	u32 sw_ovp = 0;

	if (enable)
		sw_ovp = pinfo->data.max_charger_voltage_setting;
	else
		sw_ovp = 15000000;

	/* Enable/Disable SW OVP status */
	pinfo->data.max_charger_voltage = sw_ovp;

	chr_err("[%s] en:%d ovp:%d\n",
			    __func__, enable, sw_ovp);
	return ret;
}

bool is_typec_adapter(struct charger_manager *info)
{
	int rp;

	rp = adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL);
	if (info->pd_type == MTK_PD_CONNECT_TYPEC_ONLY_SNK &&
			rp != 500 &&
			info->chr_type != STANDARD_HOST &&
			info->chr_type != CHARGING_HOST &&
			mtk_pe20_get_is_connect(info) == false &&
			mtk_pe_get_is_connect(info) == false &&
			info->enable_type_c == true)
		return true;

	return false;
}

int charger_get_vbus(void)
{
	int ret = 0;
	int vchr = 0;

	if (pinfo == NULL)
		return 0;
	ret = charger_dev_get_vbus(pinfo->chg1_dev, &vchr);
	if (ret < 0) {
		chr_err("%s: get vbus failed: %d\n", __func__, ret);
		return ret;
	}

	vchr = vchr / 1000;
	return vchr;
}

int mtk_get_dynamic_cv(struct charger_manager *info, unsigned int *cv)
{
	int ret = 0;
	u32 _cv, _cv_temp;
	unsigned int vbat_threshold[4] = {3400000, 0, 0, 0};
	u32 vbat_bif = 0, vbat_auxadc = 0, vbat = 0;
	u32 retry_cnt = 0;

	if (pmic_is_bif_exist()) {
		do {
			vbat_auxadc = battery_get_bat_voltage() * 1000;
			ret = pmic_get_bif_battery_voltage(&vbat_bif);
			vbat_bif = vbat_bif * 1000;
			if (ret >= 0 && vbat_bif != 0 &&
			    vbat_bif < vbat_auxadc) {
				vbat = vbat_bif;
				chr_err("%s: use BIF vbat = %duV, dV to auxadc = %duV\n",
					__func__, vbat, vbat_auxadc - vbat_bif);
				break;
			}
			retry_cnt++;
		} while (retry_cnt < 5);

		if (retry_cnt == 5) {
			ret = 0;
			vbat = vbat_auxadc;
			chr_err("%s: use AUXADC vbat = %duV, since BIF vbat = %duV\n",
				__func__, vbat_auxadc, vbat_bif);
		}

		/* Adjust CV according to the obtained vbat */
		vbat_threshold[1] = info->data.bif_threshold1;
		vbat_threshold[2] = info->data.bif_threshold2;
		_cv_temp = info->data.bif_cv_under_threshold2;

		if (!info->enable_dynamic_cv && vbat >= vbat_threshold[2]) {
			_cv = info->data.battery_cv;
			goto out;
		}

		if (vbat < vbat_threshold[1])
			_cv = 4608000;
		else if (vbat >= vbat_threshold[1] && vbat < vbat_threshold[2])
			_cv = _cv_temp;
		else {
			_cv = info->data.battery_cv;
			info->enable_dynamic_cv = false;
		}
out:
		*cv = _cv;
		chr_err("%s: CV = %duV, enable_dynamic_cv = %d\n",
			__func__, _cv, info->enable_dynamic_cv);
	} else
		ret = -ENOTSUPP;

	return ret;
}

int charger_manager_notifier(struct charger_manager *info, int event)
{
	return srcu_notifier_call_chain(&info->evt_nh, event, NULL);
}

int charger_psy_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, psy_nb);
	struct power_supply *psy = v;
	union power_supply_propval val;
	int ret;
	int tmp = 0;

	if (strcmp(psy->desc->name, "battery") == 0) {
		ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_TEMP, &val);
		if (!ret) {
			tmp = val.intval / 10;
			if (info->battery_temp != tmp
			    && mt_get_charger_type() != CHARGER_UNKNOWN) {
				_wake_up_charger(info);
				chr_err("%s: %ld %s tmp:%d %d chr:%d\n",
					__func__, event, psy->desc->name, tmp,
					info->battery_temp,
					mt_get_charger_type());
			}
		}
	}

	return NOTIFY_DONE;
}

void mtk_charger_int_handler(void)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	chr_err("%s\n", __func__);
	if (is_mtksvooc_project == false) {
		if (mt_get_charger_type() != CHARGER_UNKNOWN) {
			oplus_wake_up_usbtemp_thread();

			pr_err("%s, Charger Plug In\n", __func__);
			charger_manager_notifier(pinfo, CHARGER_NOTIFY_START_CHARGING);
			charger_dev_set_input_current(g_oplus_chip->chgic_mtk.oplus_info->chg1_dev, 500000);
			if (g_oplus_chip->dual_charger_support) {
				pinfo->step_status = STEP_CHG_STATUS_STEP1;
				pinfo->step_status_pre = STEP_CHG_STATUS_INVALID;
				pinfo->step_cnt = 0;
				pinfo->step_chg_current = pinfo->data.step1_current_ma;
				schedule_delayed_work(&pinfo->step_charging_work, msecs_to_jiffies(5000));
			}
		} else {
			pr_err("%s, Charger Plug Out\n", __func__);
			if (g_oplus_chip && g_oplus_chip->usb_psy)
				power_supply_changed(g_oplus_chip->usb_psy);
			charger_dev_set_input_current(g_oplus_chip->chgic_mtk.oplus_info->chg1_dev, 500000);
			if (g_oplus_chip->dual_charger_support) {
				charger_manager_notifier(pinfo, CHARGER_NOTIFY_STOP_CHARGING);
				cancel_delayed_work(&pinfo->step_charging_work);
			}
		}
	} else if (is_vooc_support_single_batt_svooc() == true) {
		if (mt_get_charger_type() != CHARGER_UNKNOWN) {
			oplus_wake_up_usbtemp_thread();
			if (mtkhv_flashled_pinctrl.hv_flashled_support) {
				mtkhv_flashled_pinctrl.bc1_2_done = true;
			}
			chr_err("single_batt Charger Plug In\n");
		} else {
			if (mtkhv_flashled_pinctrl.hv_flashled_support) {
				mtkhv_flashled_pinctrl.bc1_2_done = false;
				if (mt6360_get_vbus_rising() != true) {
					mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
					mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					pr_err("[OPLUS_CHG] chgvin_gpio = %d", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
				}
			}
			g_oplus_chip->charger_current_pre = -1;
			chr_err("single_batt Charger Plug Out\n");
		}
		charger_dev_set_input_current(g_oplus_chip->chgic_mtk.oplus_info->chg1_dev, 500000);

	} else {
		if (mt_get_charger_type() != CHARGER_UNKNOWN){
			oplus_wake_up_usbtemp_thread();			

			if (mtkhv_flashled_pinctrl.hv_flashled_support){
				mtkhv_flashled_pinctrl.bc1_2_done = true;
				if (g_oplus_chip->camera_on) {
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
					chr_err("[%s] camera_on %d\n", __func__, g_oplus_chip->camera_on);
				}
			}
			chr_err("Charger Plug In\n");
		} else {
			if (mtkhv_flashled_pinctrl.hv_flashled_support){
				mtkhv_flashled_pinctrl.bc1_2_done = false;
				pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
			}
			g_oplus_chip->charger_current_pre = -1;
			chr_err("Charger Plug Out\n");
		}
	
		charger_dev_set_input_current(g_oplus_chip->chgic_mtk.oplus_info->chg1_dev, 500000);
		if (g_oplus_chip && g_oplus_chip->vbatt_num == 2) {
			oplus_mt6360_suspend_charger();
		}	
	}

#else
	chr_err("%s\n", __func__);

	if (pinfo == NULL) {
		chr_err("charger is not rdy ,skip1\n");
		return;
	}

	if (pinfo->init_done != true) {
		chr_err("charger is not rdy ,skip2\n");
		return;
	}

	if (mt_get_charger_type() == CHARGER_UNKNOWN) {
		mutex_lock(&pinfo->cable_out_lock);
		pinfo->cable_out_cnt++;
		chr_err("cable_out_cnt=%d\n", pinfo->cable_out_cnt);
		mutex_unlock(&pinfo->cable_out_lock);
		charger_manager_notifier(pinfo, CHARGER_NOTIFY_STOP_CHARGING);
	} else
		charger_manager_notifier(pinfo, CHARGER_NOTIFY_START_CHARGING);

	chr_err("wake_up_charger\n");
	_wake_up_charger(pinfo);
#endif /* OPLUS_FEATURE_CHG_BASIC */
}

int oplus_mtk_hv_flashled_plug(int plug)
{
	chr_err("oplus_mtk_hv_flashled_plug %d\n", plug);

	if (!mtkhv_flashled_pinctrl.hv_flashled_support) {
		return -1;
	}

	if (plug == 1) { /* plug_in */
		if (g_oplus_chip->camera_on) {
			if (is_vooc_support_single_batt_svooc() != true) {
				mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
				pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
				mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
				/* gpio_direction_output(mtkhv_flashled_pinctrl.chgvin_gpio, 1); */
				chr_err("[%s] camera_on %d\n", __func__, g_oplus_chip->camera_on);
				pr_err("[OPLUS_CHG]   %d", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
			}
		}
	} else if (plug == 0) {
		mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
		mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		/* gpio_direction_output(mtkhv_flashled_pinctrl.chgvin_gpio, 0); */
		pr_err("[OPLUS_CHG]   %d", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
	}

	return 0;
}
EXPORT_SYMBOL(oplus_mtk_hv_flashled_plug);

static int mtk_chgstat_notify(struct charger_manager *info)
{
	int ret = 0;
	char *env[2] = { "CHGSTAT=1", NULL };

	chr_err("%s: 0x%x\n", __func__, info->notify_code);
	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		chr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static int mtk_charger_parse_dt(struct charger_manager *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val;

	chr_err("%s: starts\n", __func__);

	if (!np) {
		chr_err("%s: no device node\n", __func__);
		return -EINVAL;
	}

	info->enable_type_c = of_property_read_bool(np, "enable_type_c");

	/* dynamic mivr */
	if (of_property_read_u32(np, "min_charger_voltage_1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else {
		chr_err("use default V_CHARGER_MIN_1:%d\n", V_CHARGER_MIN_1);
		info->data.min_charger_voltage_1 = V_CHARGER_MIN_1;
	}

	if (of_property_read_u32(np, "min_charger_voltage_2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else {
		chr_err("use default V_CHARGER_MIN_2:%d\n", V_CHARGER_MIN_2);
		info->data.min_charger_voltage_2 = V_CHARGER_MIN_2;
	}

	if (of_property_read_u32(np, "max_dmivr_charger_current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else {
		chr_err("use default MAX_DMIVR_CHARGER_CURRENT:%d\n",
			MAX_DMIVR_CHARGER_CURRENT);
		info->data.max_dmivr_charger_current =
					MAX_DMIVR_CHARGER_CURRENT;
	}

	info->data.power_path_support =
				of_property_read_bool(np, "power_path_support");
	chr_debug("%s: power_path_support: %d\n",
		__func__, info->data.power_path_support);

	info->support_ntc_01c_precision = of_property_read_bool(np, "qcom,support_ntc_01c_precision");
	chr_debug("%s: support_ntc_01c_precision: %d\n", __func__, info->support_ntc_01c_precision);

	info->usbtemp_lowvbus_detect = of_property_read_bool(np, "qcom,usbtemp_lowvbus_detect");
	chr_debug("%s: usbtemp_lowvbus_detect: %d\n", __func__, info->usbtemp_lowvbus_detect);

	if (of_property_read_u32(np, "qcom,g_rap_pull_up_r0", &val) >= 0) {
		g_rap_pull_up_r0 = val;
	} else {
		chg_err("use g_rap_pull_up_r0 100k!\n");
		g_rap_pull_up_r0 = DEFAULT_PULL_UP_R0;
	}
	chg_err("wp:g_rap_pull_up_r0 = %d\n", g_rap_pull_up_r0);

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (of_property_read_u32(np, "qcom,pd_not_support", &val) >= 0) {
		info->data.pd_not_support = val;
	} else {
		chg_err("use pd_not_support false\n");
		info->data.pd_not_support = 0;
	}

	if (of_property_read_u32(np, "qcom,qc_not_support", &val) >= 0) {
		info->data.qc_not_support = val;
	} else {
		chg_err("use qc_not_support false\n");
		info->data.qc_not_support = 0;
	}
#endif

	return 0;
}

static int oplus_step_charging_parse_dt(struct charger_manager *info,
                                struct device *dev)
{
        struct device_node *np = dev->of_node;
        u32 val;

        if (!np) {
                chr_err("%s: no device node\n", __func__);
                return -EINVAL;
        }

	if (of_property_read_u32(np, "qcom,dual_charger_support", &val) >= 0) {
		info->data.dual_charger_support = val;
	} else {
		chr_err("use dual_charger_support: disable\n");
		info->data.dual_charger_support = 0;
	}
	chr_err("oplus dual_charger_support: %d\n", info->data.dual_charger_support);
	//info->data.dual_charger_support = 1;

	if (of_property_read_u32(np, "qcom,step1_time", &val) >= 0) {
		info->data.step1_time = val;
	} else {
		chr_err("use step1_time: 300s\n");
		info->data.step1_time = 300;
	}

	if (of_property_read_u32(np, "qcom,step1_current_ma", &val) >= 0) {
		info->data.step1_current_ma = val;
	} else {
		chg_err("use step1_current_ma: 3200mA\n");
		info->data.step1_current_ma = 3200;
	}

        if (of_property_read_u32(np, "qcom,step2_time", &val) >= 0) {
		info->data.step2_time = val;
	} else {
		chg_err("use step2_time: 900s\n");
		info->data.step2_time = 900;
	}

	if (of_property_read_u32(np, "qcom,step2_current_ma", &val) >= 0) {
		info->data.step2_current_ma = val;
	} else {
		chg_err("use step2_current_ma: 3000mA\n");
		info->data.step2_current_ma = 3000;
	}

        if (of_property_read_u32(np, "qcom,step3_current_ma", &val) >= 0) {
		info->data.step3_current_ma = val;
	} else {
		chg_err("use step3_current_ma: 2640mA\n");
		info->data.step3_current_ma = 2640;
	}

	chg_err("step1_time: %d, step1_current: %d, step2_time: %d,step2_current: %d, step3_current: %d\n",
				info->data.step1_time, info->data.step1_current_ma, info->data.step2_time,
				info->data.step2_current_ma, info->data.step3_current_ma);

	return 0;
}

static ssize_t show_BatNotify(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] show_BatteryNotify: 0x%x\n", pinfo->notify_code);

	return sprintf(buf, "%u\n", pinfo->notify_code);
}

static ssize_t store_BatNotify(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] store_BatteryNotify\n");
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->notify_code = reg;
		pr_debug("[Battery] store code: 0x%x\n", pinfo->notify_code);
		mtk_chgstat_notify(pinfo);
	}
	return size;
}

static DEVICE_ATTR(BatteryNotify, 0664, show_BatNotify, store_BatNotify);

/* Create sysfs and procfs attributes */
static int mtk_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
	struct proc_dir_entry *battery_dir = NULL;
	struct charger_manager *info = platform_get_drvdata(pdev);
	/* struct charger_device *chg_dev; */

	/* Battery warning */
	ret = device_create_file(&(pdev->dev), &dev_attr_BatteryNotify);
	if (ret)
		goto _out;
_out:
	return ret;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#define OPLUS_SVID 0x22D9
int oplus_get_adapter_svid(void)
{
	int i = 0;
	uint32_t vdos[VDO_MAX_NR] = {0};
	struct tcpc_device *tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	struct tcpm_svid_list svid_list = {0, {0}};

	if (tcpc_dev == NULL || !g_oplus_chip) {
		chr_err("tcpc_dev is null return\n");
		return -1;
	}

	if (!oplus_is_vooc_project()) {
		chg_err("match svid but not vooc project return\n");
		return -1;
	}

	tcpm_inquire_pd_partner_svids(tcpc_dev, &svid_list);
	for (i = 0; i < svid_list.cnt; i++) {
		chr_err("svid[%d] = %d\n", i, svid_list.svids[i]);
		if (svid_list.svids[i] == OPLUS_SVID) {
			g_oplus_chip->pd_svooc = true;
			chr_err("match svid and this is oplus adapter\n");
			break;
		}
	}

	if (!g_oplus_chip->pd_svooc) {
		tcpm_inquire_pd_partner_inform(tcpc_dev, vdos);
		if ((vdos[0] & 0xFFFF) == OPLUS_SVID) {
				g_oplus_chip->pd_svooc = true;
				chr_err("match svid and this is oplus adapter 11\n");
		}
	}

	return 0;
}

bool oplus_check_pd_state_ready(void)
{
	if (!pinfo) {
		chr_err("pinfo is null return \n");
		return false;
	}

	return (pinfo->in_good_connect);
}
EXPORT_SYMBOL(oplus_check_pd_state_ready);

bool oplus_check_pdphy_ready(void)
{
	return (pinfo != NULL && pinfo->tcpc != NULL && pinfo->tcpc->pd_inited_flag);
}

#endif

void notify_adapter_event(enum adapter_type type, enum adapter_event evt,
	void *val)
{
	chr_err("%s %d %d\n", __func__, type, evt);

	switch (type) {
	case MTK_PD_ADAPTER:
		switch (evt) {
		case  MTK_PD_CONNECT_NONE:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify Detach\n");
			pinfo->pd_type = MTK_PD_CONNECT_NONE;
			mutex_unlock(&pinfo->charger_pd_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
			pinfo->in_good_connect = false;
			chr_err("MTK_PD_CONNECT_NONE in_good_connect false\n");
#endif
			/* reset PE40 */
			break;

		case MTK_PD_CONNECT_HARD_RESET:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify HardReset\n");
			pinfo->pd_type = MTK_PD_CONNECT_NONE;
			pinfo->pd_reset = true;
			mutex_unlock(&pinfo->charger_pd_lock);
			_wake_up_charger(pinfo);
#ifdef OPLUS_FEATURE_CHG_BASIC
			pinfo->in_good_connect = false;
			chr_err("MTK_PD_CONNECT_HARD_RESET in_good_connect false\n");
#endif
			/* reset PE40 */
			break;

		case MTK_PD_CONNECT_PE_READY_SNK:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify fixe voltage ready\n");
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK;
			mutex_unlock(&pinfo->charger_pd_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
			oplus_chg_track_record_chg_type_info();
			pinfo->in_good_connect = true;
			oplus_get_adapter_svid();
			chr_err("MTK_PD_CONNECT_PE_READY_SNK in_good_connect true\n");
#endif
			/* PD is ready */
			break;

		case MTK_PD_CONNECT_PE_READY_SNK_PD30:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify PD30 ready\r\n");
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_PD30;
			mutex_unlock(&pinfo->charger_pd_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
			oplus_chg_track_record_chg_type_info();
			pinfo->in_good_connect = true;
			oplus_get_adapter_svid();
			chr_err("MTK_PD_CONNECT_PE_READY_SNK_PD30 in_good_connect true\n");
#endif
			/* PD30 is ready */
			break;

		case MTK_PD_CONNECT_PE_READY_SNK_APDO:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify APDO Ready\n");
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_APDO;
			mutex_unlock(&pinfo->charger_pd_lock);
			/* PE40 is ready */
			_wake_up_charger(pinfo);
#ifdef OPLUS_FEATURE_CHG_BASIC
			oplus_chg_track_record_chg_type_info();
			pinfo->in_good_connect = true;
			oplus_get_adapter_svid();
			chr_err("MTK_PD_CONNECT_PE_READY_SNK_APDO in_good_connect true\n");
#endif
			break;

		case MTK_PD_CONNECT_TYPEC_ONLY_SNK:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify Type-C Ready\n");
			pinfo->pd_type = MTK_PD_CONNECT_TYPEC_ONLY_SNK;
			mutex_unlock(&pinfo->charger_pd_lock);
			/* type C is ready */
			_wake_up_charger(pinfo);
			break;
		case MTK_TYPEC_WD_STATUS:
			chr_err("wd status = %d\n", *(bool *)val);
			mutex_lock(&pinfo->charger_pd_lock);
			pinfo->water_detected = *(bool *)val;
			mutex_unlock(&pinfo->charger_pd_lock);

			if (pinfo->water_detected == true) {
				pinfo->notify_code |= CHG_TYPEC_WD_STATUS;
#ifdef OPLUS_FEATURE_CHG_BASIC
			oplus_set_usb_status(USB_WATER_DETECT);
			oplus_vooc_set_disable_adapter_output(true);
			if (g_oplus_chip && g_oplus_chip->usb_psy)
				power_supply_changed(g_oplus_chip->usb_psy);
#endif
			mtk_chgstat_notify(pinfo);
		} else {
			pinfo->notify_code &= ~CHG_TYPEC_WD_STATUS;
#ifdef OPLUS_FEATURE_CHG_BASIC
			oplus_clear_usb_status(USB_WATER_DETECT);
			oplus_vooc_set_disable_adapter_output(false);
			if (g_oplus_chip && g_oplus_chip->usb_psy)
				power_supply_changed(g_oplus_chip->usb_psy);
#endif
			mtk_chgstat_notify(pinfo);
		}
		break;
		case MTK_TYPEC_HRESET_STATUS:
			chr_err("hreset status = %d\n", *(bool *)val);
			mutex_lock(&pinfo->charger_pd_lock);
			if (*(bool *)val)
				atomic_set(&pinfo->enable_kpoc_shdn, 1);
			else
				atomic_set(&pinfo->enable_kpoc_shdn, 0);
			mutex_unlock(&pinfo->charger_pd_lock);
		break;
		}
	}
}

#ifdef OPLUS_FEATURE_CHG_BASIC
//====================================================================//
static void oplus_mt6360_dump_registers(void)
{
	struct charger_device *chg = NULL;
	static bool musb_hdrc_release = false;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (musb_hdrc_release == false &&
			g_oplus_chip->unwakelock_chg == 1 &&
			mt_get_charger_type() == NONSTANDARD_CHARGER) {
		musb_hdrc_release = true;
		mt_usb_disconnect();
	} else {
		if (musb_hdrc_release == true &&
				g_oplus_chip->unwakelock_chg == 0 &&
				mt_get_charger_type() == NONSTANDARD_CHARGER) {
			musb_hdrc_release = false;
			mt_usb_connect();
		}
	}

	/*This function runs for more than 400ms, so return when no charger for saving power */
	if (g_oplus_chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN
			|| oplus_get_chg_powersave() == true) {
		return;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	if (pinfo->data.dual_charger_support) {
		charger_dev_dump_registers(chg);
		if (bq25601d_is_detected() == true) {
			bq25601d_dump_registers();
		}
		if (bq2591x_is_detected() == true) {
			oplus_bq2591x_dump_registers();
		}
	} else {
		charger_dev_dump_registers(chg);
	}

	return;
}

static int oplus_mt6360_kick_wdt(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	rc = charger_dev_kick_wdt(chg);
	if (rc < 0) {
		chg_debug("charger_dev_kick_wdt fail\n");
	}
	return 0;
}

static int oplus_mt6360_hardware_init(void)
{
	int hw_aicl_point = 4400;
	//int sw_aicl_point = 4500;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	oplus_mt6360_reset_charger();

	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
		oplus_mt6360_disable_charging();
		oplus_mt6360_float_voltage_write(4400);
		msleep(100);
	}

	oplus_mt6360_float_voltage_write(4385);

	//set_complete_charge_timeout(OVERTIME_DISABLED);
	mt6360_set_register(0x1C, 0xFF, 0xF9);

	//set_prechg_current(300);
	mt6360_set_register(0x18, 0xF, 0x4);

	oplus_mt6360_charging_current_write_fast(512);

	oplus_mt6360_set_termchg_current(150);

	oplus_mt6360_set_rechg_voltage(100);

	charger_dev_set_mivr(chg, hw_aicl_point * 1000);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
			|| get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
		oplus_mt6360_suspend_charger();
		oplus_mt6360_disable_charging();
	} else {
		oplus_mt6360_unsuspend_charger();
		oplus_mt6360_enable_charging();
	}
#else /* CONFIG_OPLUS_CHARGER_MTK */
	oplus_mt6360_unsuspend_charger();
#endif /* CONFIG_OPLUS_CHARGER_MTK */

	//set_wdt_timer(REG05_BQ25601D_WATCHDOG_TIMER_40S);
	//mt6360_set_register(0x1D, 0x80, 0x80);
	mt6360_set_register(0x1D, 0x30, 0x10);

	if (pinfo->data.dual_charger_support){
 		if(bq25601d_is_detected() == true){
			bq25601d_hardware_init();
 		}
		if (bq2591x_is_detected() == true) {
			oplus_bq2591x_hardware_init();
		}
	}
	return 0;
}

static int oplus_mt6360_charging_current_write_fast(int chg_curr)
{
	int rc = 0;
	u32 ret_chg_curr = 0;
	struct charger_device *chg = NULL;
	int main_cur = 0;
	int slave_cur = 0;


	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}

	if (!g_oplus_chip->authenticate) {
		g_oplus_chip->chg_ops->charging_disable();
		printk(KERN_ERR "[OPLUS_CHG][%s]:!g_oplus_chip->authenticate , charging_disable\n", __func__);
		return 0;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	if (pinfo->data.dual_charger_support) {

		if (bq25601d_is_detected() == false && bq2591x_is_detected() == false) {
			g_oplus_chip->slave_pct = 15;
		}

		if (g_oplus_chip->tbatt_status == BATTERY_STATUS__NORMAL) {
#ifdef OPLUS_FEATURE_CHG_BASIC
			if (chg_curr > pinfo->data.step3_current_ma && pinfo->step_chg_current >= pinfo->data.step3_current_ma)
#else
			if (chg_curr > pinfo->step_chg_current)
#endif
				chg_curr = pinfo->step_chg_current;
		}

		if (g_oplus_chip->dual_charger_support &&
				(g_oplus_chip->slave_charger_enable || em_mode)) {
			main_cur = (chg_curr * (100 - g_oplus_chip->slave_pct))/100;
			main_cur -= main_cur % 100;
			slave_cur = chg_curr - main_cur;
			chg_debug("chg_curr [%d],main_cur [%d] slave_cur [%d] \n", chg_curr, main_cur, slave_cur);
			rc = charger_dev_set_charging_current(chg, main_cur * 1000);
			if (rc < 0) {
				chg_debug("set fast charge current:%d fail\n", main_cur);
			}
			if(bq25601d_is_detected() == true){
				rc = bq25601d_charging_current_write_fast(slave_cur);
			}
			if (bq2591x_is_detected() == true) {
				rc = oplus_bq2591x_set_ichg(slave_cur);
			}

			if (rc < 0) {
				chg_debug("set sub fast charge current:%d fail\n", slave_cur);
			}
		} else {
			if (g_oplus_chip->dual_charger_support && g_oplus_chip->slave_charger_enable == false) {
				if (bq25601d_is_detected() == true) {
					rc = bq25601d_disable_charging();
				}
				if (bq2591x_is_detected() == true) {
					rc = oplus_bq2591x_charging_disable();
				}
				if (rc < 0) {
					chg_debug("disable sub charging fail\n");
				}
				if (bq25601d_is_detected() == true) {
					rc = bq25601d_suspend_charger();
				}
				if (bq2591x_is_detected() == true) {
					rc = oplus_bq2591x_charger_suspend();
				}
				if (rc < 0) {
					chg_debug("disable sub charging fail\n");
				}
			}
			rc = charger_dev_set_charging_current(chg, chg_curr * 1000);
			if (rc < 0) {
				chg_debug("set fast charge current:%d fail\n", chg_curr);
			}
		}
	} else {
		rc = charger_dev_set_charging_current(chg, chg_curr * 1000);
		if (rc < 0) {
			chg_debug("set fast charge current:%d fail\n", chg_curr);
		} else {
			charger_dev_get_charging_current(chg, &ret_chg_curr);
			chg_debug("set fast charge current:%d ret_chg_curr = %d\n", chg_curr, ret_chg_curr);
		}
	}
	return 0;
}

#define HW_AICL_POINT_PDQC_SINGLE_BAT	7600
#define HW_AICL_POINT_PDQC_DUAL_BAT		8400
#define PDQC_9V_AICL_SET_THD			7000



static void oplus_mt6360_set_aicl_point(int vbatt)
{
	int rc = 0;
	static int hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
	static int pdqc_hw_aicl_point = 7600;
	struct charger_device *chg = NULL;
	int chg_vol = 0;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (!g_oplus_chip->authenticate) {
		printk(KERN_ERR "[OPLUS_CHG][%s]:!g_oplus_chip->authenticate \n", __func__);
		return;
	}

	if (g_oplus_chip->vbatt_num == 1) {
		pdqc_hw_aicl_point = HW_AICL_POINT_PDQC_SINGLE_BAT;
	} else {
		pdqc_hw_aicl_point = HW_AICL_POINT_PDQC_DUAL_BAT;
	}
	chg_debug("pdqc_hw_aicl_point:%d\n", pdqc_hw_aicl_point);
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	chg_vol = battery_meter_get_charger_voltage();

	if (g_oplus_chip->pdqc_9v_voltage_adaptive) {
		if ((g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC ||
			g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) &&
			(oplus_vooc_get_fastchg_started() == false) && (chg_vol > PDQC_9V_AICL_SET_THD)) {
				hw_aicl_point = pdqc_hw_aicl_point;
		} else {
			if (((hw_aicl_point == 4400) || (hw_aicl_point == pdqc_hw_aicl_point)) && vbatt > 4100) {
				hw_aicl_point = 4500;
			} else if (((hw_aicl_point == 4400) || (hw_aicl_point == pdqc_hw_aicl_point)) && vbatt <= 4100) {
				hw_aicl_point = 4400;
			}
		}
	} else {
		if (hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE1 && vbatt > AICL_POINT_VOL_5V) {
			hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE2;
		} else if (hw_aicl_point == HW_AICL_POINT_VOL_5V_PHASE2 && vbatt <= AICL_POINT_VOL_5V) {
			hw_aicl_point = HW_AICL_POINT_VOL_5V_PHASE1;
		}
	}
	rc = charger_dev_set_mivr(chg, hw_aicl_point * 1000);
	if (rc < 0) {
		chg_debug("set aicl point:%d fail\n", hw_aicl_point);
	}
	chg_debug("set aicl point:%d,chg_vol:%d\n", hw_aicl_point, chg_vol);
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 2000, 2400, 3000,
};

static int oplus_mt6360_input_current_limit_write(int value)
{
	int rc = 0;
	int i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int vbus_mv = 0;
	int ibus_ma = 0;
	int main_cur = 0;
	int slave_cur = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg_debug("usb input max current limit=%d setting %02x\n", value, i);

	if (!g_oplus_chip->authenticate) {
		g_oplus_chip->chg_ops->charging_disable();
		printk(KERN_ERR "[OPLUS_CHG][%s]: !g_oplus_chip->authenticate , charging_disable\n", __func__);
		return 0;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	if (pinfo->data.dual_charger_support) {

		if (bq25601d_is_detected() == false && bq2591x_is_detected() == false) {
			g_oplus_chip->slave_pct = 15;
		}

		if (g_oplus_chip->dual_charger_support && g_oplus_chip->slave_charger_enable == false) {
			if(bq25601d_is_detected() == true){
				rc = bq25601d_disable_charging();
			}
			if (bq2591x_is_detected() == true) {
				rc = oplus_bq2591x_charging_disable();
			}
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}
			if(bq25601d_is_detected() == true){
				rc = bq25601d_suspend_charger();
			}
			if (bq2591x_is_detected() == true) {
				rc = oplus_bq2591x_charger_suspend();
			}
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}
		}
	}

	//aicl_point_temp = g_oplus_chip->sw_aicl_point;
	if (g_oplus_chip->chg_ops->oplus_chg_get_pd_type) {
		if (g_oplus_chip->chg_ops->oplus_chg_get_pd_type() == true) {
			rc = oplus_pdc_get(&vbus_mv, &ibus_ma);
			if (rc >= 0 && ibus_ma >= 500 && ibus_ma < 3000 && value > ibus_ma) {
				value = ibus_ma;
				chg_debug("usb input max current limit=%d(pd)\n", value);
			}
		}
	}

	if (pinfo->data.dual_charger_support) {
		chg_vol = battery_meter_get_charger_voltage();
		if (chg_vol > 7600) {
			aicl_point = 7600;
		} else {
			if (g_oplus_chip->batt_volt > 4100 )
				aicl_point = 4550;
			else
				aicl_point = 4500;
		}
	} else {
		chg_vol = battery_meter_get_charger_voltage();
		if (chg_vol > AICL_POINT_VOL_9V) {
			aicl_point = AICL_POINT_VOL_9V;
		} else {
			if (g_oplus_chip->batt_volt > AICL_POINT_VOL_5V)
				aicl_point = SW_AICL_POINT_VOL_5V_PHASE2;
			else
				aicl_point = SW_AICL_POINT_VOL_5V_PHASE1;
		}
	}

	if (g_oplus_chip->charger_current_pre == value) {
		chg_err("charger_current_pre == value =%d.\n", value);
		if (aicl_point != AICL_POINT_VOL_9V && pinfo->data.dual_charger_support) {
			return rc;
		}
	} else {
		g_oplus_chip->charger_current_pre = value;
	}

	if (value < 500) {
		i = 0;
		goto aicl_end;
	}

	mt6360_aicl_enable(false);

	i = 1; /* 500 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		chg_debug( "use 500 here\n");
		goto aicl_end;
	} else if (value < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 2;
		goto aicl_pre_step;
	}

	i = 5; /* 1500 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 3; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (value < 1500) {
		i = i - 2; //We use 1.2A here
		goto aicl_end;
	} else if (value < 2000)
		goto aicl_end;

	i = 6; /* 2000 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value < 2400)
		goto aicl_end;

	i = 7; /* 2400 */
        rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
        msleep(90);
        chg_vol = battery_meter_get_charger_voltage();
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        } else if (value < 3000)
                goto aicl_end;

	i = 8; /* 3000 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value >= 3000)
		goto aicl_end;

aicl_pre_step:
	chg_debug("usb input max current limit aicl chg_vol=%d i[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	if (pinfo->data.dual_charger_support) {
		if (g_oplus_chip->dual_charger_support &&
				(g_oplus_chip->slave_charger_enable || em_mode)) {
			slave_cur = (usb_icl[i] * g_oplus_chip->slave_pct)/100;
			slave_cur -= slave_cur % 100;
			main_cur = usb_icl[i] - slave_cur;

			chg_debug("usb input max current limit aicl: master and salve input current: %d, %d\n",
					main_cur, slave_cur);
			rc = charger_dev_set_input_current(chg, main_cur * 1000);
			if (bq25601d_is_detected() == true) {
				bq25601d_input_current_limit_write(slave_cur);
			}
			if (bq2591x_is_detected() == true) {
				 oplus_bq2591x_set_ichg(slave_cur);
			}
			chg_debug("enable mt6360 and bq25910 for charging\n");
			if (bq25601d_is_detected() == true) {
				rc = bq25601d_enable_charging();
			}
			if (bq2591x_is_detected() == true) {
				rc = oplus_bq2591x_charging_enable();
			}

			if (rc < 0) {
				chg_debug("enable sub charging fail\n");
			}
			if (bq25601d_is_detected() == true) {
				rc = bq25601d_unsuspend_charger();
			}
			if (bq2591x_is_detected() == true) {
				rc = oplus_bq2591x_charger_unsuspend();
			}
			if (rc < 0) {
				chg_debug("enable sub charging fail\n");
			}
		} else {
			rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
		}
	} else {
		rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	}
	goto aicl_rerun;
aicl_end:		
	chg_debug("usb input max current limit aicl chg_vol=%d i[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);

	if (pinfo->data.dual_charger_support) {
		if (g_oplus_chip->dual_charger_support &&
				(g_oplus_chip->slave_charger_enable || em_mode)) {

			slave_cur = (usb_icl[i] * g_oplus_chip->slave_pct)/100;
			slave_cur -= slave_cur % 100;
			main_cur = usb_icl[i] - slave_cur;

			chg_debug("usb input max current limit aicl: master and salve input current: %d, %d\n",
					main_cur,slave_cur);
			rc = charger_dev_set_input_current(chg, main_cur * 1000 );
			if (rc >= 0) {
				if(bq25601d_is_detected() == true){
					bq25601d_input_current_limit_write(slave_cur);
				}
				if (bq2591x_is_detected() == true) {
					oplus_bq2591x_set_ichg(slave_cur);
				}
			}
			if (bq25601d_is_detected() == true) {
				rc = bq25601d_enable_charging();
			}
			if (bq2591x_is_detected() == true) {
				rc = oplus_bq2591x_charging_enable();
			}
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}
			if (bq25601d_is_detected() == true) {
				rc = bq25601d_unsuspend_charger();
			}
			if (bq2591x_is_detected() == true) {
				rc = oplus_bq2591x_charger_unsuspend();
			}
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}
		} else {
			rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
		}
	} else {
		rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	}

	goto aicl_rerun;
aicl_rerun:
	mt6360_aicl_enable(true);
	g_oplus_chip->charger_current_pre = usb_icl[i];
	return rc;

}

#define DELTA_MV        32
static int oplus_mt6360_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	if (pinfo->data.dual_charger_support) {
		rc = charger_dev_set_constant_voltage(chg, vfloat_mv * 1000);
		if (rc < 0) {
			chg_debug("set float voltage:%d fail\n", vfloat_mv);
		}
		if(bq25601d_is_detected() == true){
			rc = bq25601d_float_voltage_write(vfloat_mv + DELTA_MV);
		}
		if (bq2591x_is_detected() == true) {
			rc = oplus_bq2591x_set_cv(vfloat_mv + DELTA_MV);
		}
		if (rc < 0) {
			chg_debug("set sub float voltage:%d fail\n", vfloat_mv);
		}
	} else {
		rc = charger_dev_set_constant_voltage(chg, vfloat_mv * 1000);
		if (rc < 0) {
			chg_debug("set float voltage:%d fail\n", vfloat_mv);
		}
	}

	return 0;
}

static int oplus_mt6360_set_termchg_current(int term_curr)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	rc = charger_dev_set_eoc_current(chg, term_curr * 1000);
	if (rc < 0) {
		//chg_debug("set termchg_current fail\n");
	}
	return 0;
}

static int oplus_mt6360_enable_charging(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	rc = charger_dev_enable(chg, true);
	if (rc < 0) {
		chg_debug("enable charging fail\n");
	}

	return 0;
}

static int oplus_mt6360_disable_charging(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}

	g_oplus_chip->charger_current_pre = -1;
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

 	if (pinfo->data.dual_charger_support) {
		rc = charger_dev_enable(chg, false);
		if (rc < 0) {
			chg_debug("disable charging fail\n");
		}
		if(bq25601d_is_detected() == true){
			rc = bq25601d_disable_charging();
		}
		if (bq2591x_is_detected() == true) {
			rc = oplus_bq2591x_charging_disable();
		}
		if (rc < 0) {
			chg_debug("disable sub charging fail\n");
		}
	} else {
		rc = charger_dev_enable(chg, false);
		if (rc < 0) {
			chg_debug("disable charging fail\n");
		}
	}

	return 0;
}

static int oplus_mt6360_check_charging_enable(void)
{
	return mt6360_check_charging_enable();
}

static int oplus_mt6360_suspend_charger(void)
{
	int rc = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
			return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	if (!oplus_chg_get_voocphy_support()) {
		chg_err("if support voocphy, do not delay!\n");
		rc = charger_dev_set_input_current(chg, 500000);
		msleep(50);
	}
	rc = mt6360_suspend_charger(true);
	if (rc < 0) {
		chg_debug("suspend charger fail\n");
	}
	if (is_vooc_support_single_batt_svooc() == true && oplus_vooc_get_fastchg_started() == true) {
		mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
		mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		chr_err("[%s] chgvin_disable\n", __func__);
	}
	return 0;
}

static int oplus_mt6360_unsuspend_charger(void)
{
	int rc = 0;

	rc = mt6360_suspend_charger(false);
	if (rc < 0) {
		chg_debug("unsuspend charger fail\n");
	}
	if (is_vooc_support_single_batt_svooc() == true) {
		mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
		mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		chr_err("[%s] chgvin_enable\n", __func__);
	}
	return 0;
}

static int oplus_mt6360_set_rechg_voltage(int rechg_mv)
{
	int rc = 0;

	rc = mt6360_set_rechg_voltage(rechg_mv);
	if (rc < 0) {
		chg_debug("set rechg voltage fail:%d\n", rechg_mv);
	}
	return 0;
}

static int oplus_mt6360_reset_charger(void)
{
	int rc = 0;

	rc = mt6360_reset_charger();
	if (rc < 0) {
		chg_debug("reset charger fail\n");
	}
	return 0;
}

static int oplus_mt6360_registers_read_full(void)
{
	bool full = false;
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	rc = charger_dev_is_charging_done(chg, &full);
	if (rc < 0) {
		chg_debug("registers read full  fail\n");
		full = false;
	} else {
		//chg_debug("registers read full\n");
	}

	return full;
}

static int oplus_mt6360_otg_enable(void)
{
	return 0;
}

static int oplus_mt6360_otg_disable(void)
{
	return 0;
}

static int oplus_mt6360_set_chging_term_disable(void)
{
	int rc = 0;

	rc = mt6360_set_chging_term_disable(true);
	if (rc < 0) {
		chg_debug("disable chging_term fail\n");
	}
	return 0;
}

static bool oplus_mt6360_check_charger_resume(void)
{
	return true;
}

static int oplus_mt6360_get_chg_ibus(void)
{
	struct charger_device *chg = NULL;
	u32 ibus = 0;
	int ret = 0;
	if (g_oplus_chip) {
		chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
		if (chg) {
			ret = charger_dev_get_ibus(chg, &ibus);
			if (!ret)
				return ibus/1000;
		}
	}
	return -1;
}

static int oplus_mt6360_get_chg_current_step(void)
{
	return 100;
}

static bool oplus_pd_without_usb(void)
{
	struct tcpc_device *tcpc;

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!tcpc) {
		chg_err("get type_c_port0 fail\n");
		return true;
	}

	if (!tcpm_inquire_pd_connected(tcpc))
		return true;
	return (tcpm_inquire_dpm_flags(tcpc) &
			DPM_FLAGS_PARTNER_USB_COMM) ? false : true;
}

int mt_power_supply_type_check(void)
{
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

	switch(mt_get_charger_type()) {
	case CHARGER_UNKNOWN:
		break;
	case STANDARD_HOST:
		if (!oplus_pd_without_usb())
			charger_type = POWER_SUPPLY_TYPE_USB_PD_SDP;
		else
			charger_type = POWER_SUPPLY_TYPE_USB;
		break;
	case CHARGING_HOST:
		charger_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case NONSTANDARD_CHARGER:
	case STANDARD_CHARGER:
	case APPLE_2_1A_CHARGER:
	case APPLE_1_0A_CHARGER:
	case APPLE_0_5A_CHARGER:
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	default:
		break;
	}
	chg_debug("charger_type[%d]\n", charger_type);

	if (g_oplus_chip) {
		if ((g_oplus_chip->charger_type != charger_type) && g_oplus_chip->usb_psy) {
			if (g_oplus_chip->charger_type == POWER_SUPPLY_TYPE_USB &&
			    charger_type == POWER_SUPPLY_TYPE_USB_PD_SDP) {
				g_oplus_chip->charger_type = charger_type;
				oplus_chg_turn_on_charging(g_oplus_chip);
			}
			power_supply_changed(g_oplus_chip->usb_psy);
		}
	}
	return charger_type;
}
bool oplus_mt_get_vbus_status(void)
{
	bool vbus_status = false;
	static bool pre_vbus_status = false;
	int ret = 0;

	if(g_oplus_chip && g_oplus_chip->vbatt_num == 2 && oplus_chg_get_otg_online()){
		return false;
	}

	ret = mt6360_get_vbus_rising();
	if (ret < 0) {
		if (g_oplus_chip && g_oplus_chip->unwakelock_chg == 1
				&& g_oplus_chip->charger_type != POWER_SUPPLY_TYPE_UNKNOWN) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: unwakelock_chg=1, use pre status\n", __func__);
			return pre_vbus_status;
		} else {
			return false;
		}
	}
	if (ret == 0)
		vbus_status = false;
	else
		vbus_status = true;
	pre_vbus_status = vbus_status;
	return vbus_status;
}

int oplus_battery_meter_get_battery_voltage(void)
{
	return 4000;
}

int get_rtc_spare_oplus_fg_value(void)
{
	return 0;
}

int set_rtc_spare_oplus_fg_value(int soc)
{
	return 0;
}

void oplus_mt_power_off(void)
{
	struct tcpc_device *tcpc_dev = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (g_oplus_chip->ac_online != true) {
		if (tcpc_dev == NULL) {
			tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
		}
		if (tcpc_dev) {
			if (!(tcpc_dev->pd_wait_hard_reset_complete)
					&& !oplus_mt_get_vbus_status()) {
				kernel_power_off();
			}
		}
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ac_online is true, return!\n", __func__);
	}
}

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oplus_mt6360_chg_get_dyna_aicl_result(void)
{
	int rc = 0;
	int aicl_ma = 0;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;
	rc = charger_dev_get_input_current(chg, &aicl_ma);
	if (rc < 0) {
		chg_debug("get dyna aicl fail\n");
		return 500;
	}
	return aicl_ma / 1000;
}
#endif

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	int rc = 0;
	struct rtc_time tm;
	struct rtc_device *rtc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */
//====================================================================//

int oplus_chg_get_main_ibat(void)
{
	int ibat = 0;
	int ret = 0;
	struct charger_device *chg = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	ret = charger_dev_get_ibat(chg, &ibat);
	if (ret < 0) {
		pr_err("[%s] get ibat fail\n", __func__);
		return -1;
	}

	return ibat / 1000;
}

//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
//extern bool ignore_usb;
static void set_usbswitch_to_rxtx(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	//if (ignore_usb) {
	//	chg_err("ignore_usb is true, do not set_usbswitch_to_rxtx\n");
	//	return;
	//}

	mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output2);
	mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
	}
	chg_err("set_usbswitch_to_rxtx\n");
	return;
}

static void set_usbswitch_to_dpdm(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output1);
	mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return;
	}
	chg_err("set_usbswitch_to_dpdm\n");
}
static bool chargerid_support = false;
static bool is_support_chargerid_check(void)
{
#ifdef CONFIG_OPLUS_CHECK_CHARGERID_VOLT
	return chargerid_support;
#else
	return false;
#endif
}

int mt_get_chargerid_volt(void)
{
	int chargerid_volt = 0;
	int rc = 0;

	if (!pinfo->charger_id_chan) {
                chg_err("charger_id_chan NULL\n");
                return 0;
        }

	if (is_support_chargerid_check() == true) {
		rc = iio_read_channel_processed(pinfo->charger_id_chan, &chargerid_volt);
		if (rc < 0) {
			chg_err("read charger_id_chan fail, rc=%d\n", rc);
			return 0;
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		chargerid_volt = chargerid_volt * 1500 / 4096;
#endif
		chg_debug("chargerid_volt=%d\n", chargerid_volt);
	} else {
		chg_debug("is_support_chargerid_check=false!\n");
		return 0;
	}

	return chargerid_volt;
}

void mt_set_chargerid_switch_val(int value)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (is_support_chargerid_check() == false)
		return;

	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return;
	}
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
			|| IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output1)
			|| IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value == 1) {
		set_usbswitch_to_rxtx(chip);
	} else if (value == 0) {
		set_usbswitch_to_dpdm(chip);
	} else {
		//do nothing
	}
	chg_debug("get_val=%d\n", gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio));

	return;
}

int mt_get_chargerid_switch_val(void)
{
	int gpio_status = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	if (is_support_chargerid_check() == false)
		return 0;

	gpio_status = gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio);

	chg_debug("mt_get_chargerid_switch_val=%d\n", gpio_status);

	return gpio_status;
}

int oplus_usb_switch_gpio_gpio_init(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.chargerid_switch_gpio pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.charger_gpio_as_output1 =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
			"charger_gpio_as_output_low");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output1)) {
		chg_err("get charger_gpio_as_output_low fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.charger_gpio_as_output2 =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
			"charger_gpio_as_output_high");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("get charger_gpio_as_output_high fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
			chip->normalchg_gpio.charger_gpio_as_output1);

	return 0;
}

static int oplus_chg_chargerid_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chargerid_support = of_property_read_bool(node, "qcom,chargerid_support");
	if (chargerid_support == false){
		chg_err("not support chargerid\n");
		//return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_gpio =
			of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("Couldn't read chargerid_switch-gpio rc=%d, chargerid_switch-gpio:%d\n",
				rc, chip->normalchg_gpio.chargerid_switch_gpio);
	} else {
		if (gpio_is_valid(chip->normalchg_gpio.chargerid_switch_gpio)) {
			rc = gpio_request(chip->normalchg_gpio.chargerid_switch_gpio, "charging_switch1-gpio");
			if (rc) {
				chg_err("unable to request chargerid_switch-gpio:%d\n",
						chip->normalchg_gpio.chargerid_switch_gpio);
			} else {
				rc = oplus_usb_switch_gpio_gpio_init();
				if (rc)
					chg_err("unable to init chargerid_switch-gpio:%d\n",
							chip->normalchg_gpio.chargerid_switch_gpio);
			}
		}
		chg_err("chargerid_switch-gpio:%d\n", chip->normalchg_gpio.chargerid_switch_gpio);
	}

	return rc;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/
//====================================================================//


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
static bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio)) {
		return true;
	}

	return false;
}

static int oplus_shortc_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	chip->normalchg_gpio.shortc_active =
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "shortc_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
		chg_err("get shortc_active fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.shortc_active);

	return 0;
}

#ifdef CONFIG_OPLUS_SHORT_HW_CHECK	
bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return shortc_hw_status;
	}

	if (oplus_shortc_check_is_gpio(chip) == true) {	
		shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else /* CONFIG_OPLUS_SHORT_HW_CHECK */
static bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	return shortc_hw_status;
}
#endif /* CONFIG_OPLUS_SHORT_HW_CHECK */

static int oplus_chg_shortc_hw_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

        if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.shortc_gpio = of_get_named_gpio(node, "qcom,shortc-gpio", 0);
	if (chip->normalchg_gpio.shortc_gpio <= 0) {
		chg_err("Couldn't read qcom,shortc-gpio rc=%d, qcom,shortc-gpio:%d\n",
				rc, chip->normalchg_gpio.shortc_gpio);
	} else {
		if (oplus_shortc_check_is_gpio(chip) == true) {
			rc = gpio_request(chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
			if (rc) {
				chg_err("unable to request shortc-gpio:%d\n",
						chip->normalchg_gpio.shortc_gpio);
			} else {
				rc = oplus_shortc_gpio_init(chip);
				if (rc)
					chg_err("unable to init shortc-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
			}
		}
		chg_err("shortc-gpio:%d\n", chip->normalchg_gpio.shortc_gpio);
	}

	return rc;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/
//====================================================================//


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

static int oplus_ship_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	chip->normalchg_gpio.ship_active = 
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
			"ship_active");

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}
	chip->normalchg_gpio.ship_sleep = 
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
				"ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
		chip->normalchg_gpio.ship_sleep);

	return 0;
}

#define SHIP_MODE_CONFIG		0x40
#define SHIP_MODE_MASK			BIT(0)
#define SHIP_MODE_ENABLE		0
#define PWM_COUNT				5
static void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
	int i = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ship_check_is_gpio(chip) == true) {
		chg_err("select gpio control\n");
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active) && !IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
			for (i = 0; i < PWM_COUNT; i++) {
				//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 1);
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
				mdelay(3);
				//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 0);
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
				mdelay(3);
			}
		}
		chg_err("power off after 15s\n");
	}
}
static void enter_ship_mode_function(struct oplus_chg_chip *chip)
{
	struct charger_device *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (chip->enable_shipmode) {
		if (pinfo->data.dual_charger_support || !chip->disable_ship_mode) {
			mt6360_enter_shipmode();
		} else {
			smbchg_enter_shipmode(chip);
		}
		printk(KERN_ERR "[OPLUS_CHG][%s]: enter_ship_mode_function\n", __func__);
	}
}

static int oplus_chg_shipmode_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

        if (chip != NULL)
        	node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->disable_ship_mode = !of_property_read_bool(node, "qcom,enable-ship-mode");
	chip->normalchg_gpio.ship_gpio =
			of_get_named_gpio(node, "qcom,ship-gpio", 0);
	if (chip->normalchg_gpio.ship_gpio <= 0) {
		chg_err("Couldn't read qcom,ship-gpio rc = %d, qcom,ship-gpio:%d\n",
				rc, chip->normalchg_gpio.ship_gpio);
	} else {
		if (oplus_ship_check_is_gpio(chip) == true) {
			rc = gpio_request(chip->normalchg_gpio.ship_gpio, "ship-gpio");
			if (rc) {
				chg_err("unable to request ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
			} else {
				rc = oplus_ship_gpio_init(chip);
				if (rc)
					chg_err("unable to init ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
			}
		}
		chg_err("ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
	}

	return rc;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//

#ifdef OPLUS_FEATURE_CHG_BASIC

#define TEMP_25C 250

static struct temp_param sub_board_temp_table[] = {
	{-40, 4397119}, {-39, 4092874}, {-38, 3811717}, {-37, 3551749}, {-36, 3311236}, {-35, 3088599}, {-34, 2882396}, {-33, 2691310},
	{-32, 2514137}, {-31, 2349778}, {-30, 2197225}, {-29, 2055558}, {-28, 1923932}, {-27, 1801573}, {-26, 1687773}, {-25, 1581881},
	{-24, 1483100}, {-23, 1391113}, {-22, 1305413}, {-21, 1225531}, {-20, 1151037}, {-19, 1081535}, {-18, 1016661}, {-17,  956080},
	{-16,  899481}, {-15,  846579}, {-14,  797111}, {-13,  750834}, {-12,  707524}, {-11,  666972}, {-10,  628988}, {-9,   593342},
	{-8,   559931}, {-7,   528602}, {-6,   499212}, {-5,   471632}, {-4,   445772}, {-3,   421480}, {-2,   398652}, {-1,   377193},
	{0,    357012}, {1,    338006}, {2,    320122}, {3,    303287}, {4,    287434}, {5,    272500}, {6,    258426}, {7,    245160},
	{8,    232649}, {9,    220847}, {10,   209710}, {11,   199196}, {12,   189268}, {13,   179890}, {14,   171028}, {15,   162651},
	{16,   154726}, {17,   147232}, {18,   140142}, {19,   133432}, {20,   127080}, {21,   121066}, {22,   115368}, {23,   109970},
	{24,   104852}, {25,   100000}, {26,   95398 }, {27,   91032 }, {28,   86889 }, {29,   82956 }, {30,   79222 }, {31,   75675 },
	{32,   72306 }, {33,   69104 }, {34,   66061 }, {35,   63167 }, {36,   60415 }, {37,   57797 }, {38,   55306 }, {39,   52934 },
	{40,   50677 }, {41,   48528 }, {42,   46482 }, {43,   44533 }, {44,   42675 }, {45,   40904 }, {46,   39213 }, {47,   37601 },
	{48,   36063 }, {49,   34595 }, {50,   33195 }, {51,   31859 }, {52,   30584 }, {53,   29366 }, {54,   28203 }, {55,   27091 },
	{56,   26028 }, {57,   25013 }, {58,   24042 }, {59,   23113 }, {60,   22224 }, {61,   21374 }, {62,   20561 }, {63,   19782 },
	{64,   19036 }, {65,   18323 }, {66,   17640 }, {67,   16986 }, {68,   16360 }, {69,   15760 }, {70,   15184 }, {71,   14631 },
	{72,   14101 }, {73,   13592 }, {74,   13104 }, {75,   12635 }, {76,   12187 }, {77,   11757 }, {78,   11344 }, {79,   10947 },
	{80,   10566 }, {81,   10200 }, {82,     9848}, {83,     9510}, {84,     9185}, {85,     8873}, {86,     8572}, {87,     8283},
	{88,     8006}, {89,     7738}, {90,     7481}, {91,     7234}, {92,     6997}, {93,     6769}, {94,     6548}, {95,     6337},
	{96,     6132}, {97,     5934}, {98,     5744}, {99,     5561}, {100,    5384}, {101,    5214}, {102,    5051}, {103,    4893},
	{104,    4741}, {105,    4594}, {106,    4453}, {107,    4316}, {108,    4184}, {109,    4057}, {110,    3934}, {111,    3816},
	{112,    3701}, {113,    3591}, {114,    3484}, {115,    3380}, {116,    3281}, {117,    3185}, {118,    3093}, {119,    3003},
	{120,    2916}, {121,    2832}, {122,    2751}, {123,    2672}, {124,    2596}, {125,    2522}
};

static struct temp_param chargeric_temp_table[] = {
	{-40, 4251000}, {-39, 3962000}, {-38, 3695000}, {-37, 3447000}, {-36, 3218000}, {-35, 3005000}, {-34, 2807000}, {-33, 2624000},
	{-32, 2454000}, {-31, 2296000}, {-30, 2149000}, {-29, 2012000}, {-28, 1885000}, {-27, 1767000}, {-26, 1656000}, {-25, 1554000},
	{-24, 1458000}, {-23, 1369000}, {-22, 1286000}, {-21, 1208000}, {-20, 1135000}, {-19, 1068000}, {-18, 1004000}, {-17,  945000},
	{-16,  889600}, {-15,  837800}, {-14,  789300}, {-13,  743900}, {-12,  701300}, {-11,  661500}, {-10,  624100}, {-9,   589000},
	{-8,   556200}, {-7,   525300}, {-6,   496300}, {-5,   469100}, {-4,   443500}, {-3,   419500}, {-2,   396900}, {-1,   375600},
	{0,    355600}, {1,    336800}, {2,    319100}, {3,    302400}, {4,    286700}, {5,    271800}, {6,    257800}, {7,    244700},
	{8,    232200}, {9,    220500}, {10,   209400}, {11,   198900}, {12,   189000}, {13,   179700}, {14,   170900}, {15,   162500},
	{16,   154600}, {17,   147200}, {18,   140100}, {19,   133400}, {20,   127000}, {21,   121000}, {22,   115400}, {23,   110000},
	{24,   104800}, {25,   100000}, {26,   95400 }, {27,   91040 }, {28,   86900 }, {29,   82970 }, {30,   79230 }, {31,   75690 },
	{32,   72320 }, {33,   69120 }, {34,   66070 }, {35,   63180 }, {36,   60420 }, {37,   57810 }, {38,   55310 }, {39,   52940 },
	{40,   50680 }, {41,   48530 }, {42,   46490 }, {43,   44530 }, {44,   42670 }, {45,   40900 }, {46,   39210 }, {47,   37600 },
	{48,   36060 }, {49,   34595 }, {50,   33195 }, {51,   31859 }, {52,   30584 }, {53,   29366 }, {54,   28203 }, {55,   27091 },
	{56,   26028 }, {57,   25013 }, {58,   24042 }, {59,   23113 }, {60,   22224 }, {61,   21374 }, {62,   20561 }, {63,   19782 },
	{64,   19036 }, {65,   18323 }, {66,   17640 }, {67,   16986 }, {68,   16360 }, {69,   15760 }, {70,   15184 }, {71,   14631 },
	{72,   14101 }, {73,   13592 }, {74,   13104 }, {75,   12635 }, {76,   12187 }, {77,   11757 }, {78,   11344 }, {79,   10947 },
	{80,   10566 }, {81,   10200 }, {82,     9848}, {83,     9510}, {84,     9185}, {85,     8873}, {86,     8572}, {87,     8283},
	{88,     8006}, {89,     7738}, {90,     7481}, {91,     7234}, {92,     6997}, {93,     6769}, {94,     6548}, {95,     6337},
	{96,     6132}, {97,     5934}, {98,     5744}, {99,     5561}, {100,    5384}, {101,    5214}, {102,    5051}, {103,    4893},
	{104,    4769}, {105,    4623}, {106,    4482}, {107,    4346}, {108,    4215}, {109,    4088}, {110,    3966}, {111,    3848},
	{112,    3734}, {113,    3624}, {114,    3518}, {115,    3415}, {116,    3316}, {117,    3220}, {118,    3128}, {119,    3038},
	{120,    2952}, {121,    2868}, {122,    2787}, {123,    2709}, {124,    2634}, {125,    2561}
};

static struct temp_param chargeric_temp_table_390k[] = {
	{-40, 4251000}, {-39, 3962000}, {-38, 3695000}, {-37, 3447000}, {-36, 3218000}, {-35, 3005000},
	{-34, 2807000}, {-33, 2624000}, {-32, 2454000}, {-31, 2296000}, {-30, 2149000}, {-29, 2012000},
	{-28, 1885000}, {-27, 1767000}, {-26, 1656000}, {-25, 1554000}, {-24, 1458000}, {-23, 1369000},
	{-22, 1286000}, {-21, 1208000}, {-20, 1135000}, {-19, 1068000}, {-18, 1004000}, {-17, 945000 },
	{-16, 889600 }, {-15, 837800 }, {-14, 789300 }, {-13, 743900 }, {-12, 701300 }, {-11, 661500 },
	{-10, 624100 }, {-9, 589000  }, {-8, 556200  }, {-7, 525300  }, {-6, 496300  }, {-5, 469100  },
	{-4, 443500  }, {-3, 419500  }, {-2, 396900  }, {-1, 375600  }, {0, 355600   }, {1, 336800   },
	{2, 319100   }, {3, 302400   }, {4, 286700   }, {5, 271800   }, {6, 257800   }, {7, 244700   },
	{8, 232200   }, {9, 220500   }, {10, 209400  }, {11, 198900  }, {12, 189000  }, {13, 179700  },
	{14, 170900  }, {15, 162500  }, {16, 154600  }, {17, 147200  }, {18, 140100  }, {19, 133400  },
	{20, 127000  }, {21, 121000  }, {22, 115400  }, {23, 110000  }, {24, 104800  }, {25, 100000  },
	{26, 95400   }, {27, 91040   }, {28, 86900   }, {29, 82970   }, {30, 79230   }, {31, 75690   },
	{32, 72320   }, {33, 69120   }, {34, 66070   }, {35, 63180   }, {36, 60420   }, {37, 57810   },
	{38, 55310   }, {39, 52940   }, {40, 50680   }, {41, 48530   }, {42, 46490   }, {43, 44530   },
	{44, 42670   }, {45, 40900   }, {46, 39210   }, {47, 37600   }, {48, 36060   }, {49, 34600   },
	{50, 33190   }, {51, 31860   }, {52, 30580   }, {53, 29360   }, {54, 28200   }, {55, 27090   },
	{56, 26030   }, {57, 25010   }, {58, 24040   }, {59, 23110   }, {60, 22220   }, {61, 21370   },
	{62, 20560   }, {63, 19780   }, {64, 19040   }, {65, 18320   }, {66, 17640   }, {67, 16990   },
	{68, 16360   }, {69, 15760   }, {70, 15180   }, {71, 14630   }, {72, 14100   }, {73, 13600   },
	{74, 13110   }, {75, 12640   }, {76, 12190   }, {77, 11760   }, {78, 11350   }, {79, 10960   },
	{80, 10580   }, {81, 10210   }, {82, 9859    }, {83, 9522    }, {84, 9198    }, {85, 8887    },
	{86, 8587    }, {87, 8299    }, {88, 8022    }, {89, 7756    }, {90, 7500    }, {91, 7254    },
	{92, 7016    }, {93, 6788    }, {94, 6568    }, {95, 6357    }, {96, 6153    }, {97, 5957    },
	{98, 5768    }, {99, 5586    }, {100, 5410   }, {101, 5241   }, {102, 5078   }, {103, 4921   },
	{104, 4769   }, {105, 4623   }, {106, 4482   }, {107, 4346   }, {108, 4215   }, {109, 4088   },
	{110, 3965   }, {111, 3847   }, {112, 3733   }, {113, 3623   }, {114, 3517   }, {115, 3415   },
	{116, 3315   }, {117, 3220   }, {118, 3127   }, {119, 3038   }, {120, 2951   }, {121, 2868   },
	{122, 2787   }, {123, 2709   }, {124, 2633   }, {125, 2560   }
};

struct NTC_TEMPERATURE {
	int ntc_temp;
	int temperature_r;
};

static struct NTC_TEMPERATURE BATCON_Temperature_Table[] = {
	{-40, 1795971},	{-39, 1795675},	{-38, 1795360},	{-37, 1795024},	{-36, 1794667},	{-35, 1794286},
	{-34, 1793882},	{-33, 1793450},	{-32, 1792993},	{-31, 1792509},	{-30, 1791993},	{-29, 1791445},
	{-28, 1790868},	{-27, 1790249},	{-26, 1789601},	{-25, 1788909},	{-24, 1788181},	{-23, 1787404},
	{-22, 1786587},	{-21, 1785726},	{-20, 1784810},	{-19, 1783842},	{-18, 1782824},	{-17, 1781746},
	{-16, 1780610},	{-15, 1779412},	{-14, 1778150},	{-13, 1776819},	{-12, 1775420},	{-11, 1773943},
	{-10, 1772393},	{-9,  1770760},	{-8,  1769040},	{-7,  1767237},	{-6,  1765345},	{-5,  1763355},
	{-4,  1761265},	{-3,  1759072},	{-2,  1756772},	{-1,  1754361},	{0,   1751846},	{1,   1749196},
	{2,   1746429},	{3,   1743538},	{4,   1740516},	{5,   1737348},	{6,   1734042},	{7,   1730582},
	{8,   1726978},	{9,   1723241},	{10,  1719319},	{11,  1715214},	{12,  1710979},	{13,  1706542},
	{14,  1701907},	{15,  1697084},	{16,  1692151},	{17,  1686935},	{18,  1681579},	{19,  1675948},
	{20,  1670130},	{21,  1664151},	{22,  1657932},	{23,  1651485},	{24,  1644694},	{25,  1637838},
	{26,  1630827},	{27,  1623581},	{28,  1616120},	{29,  1608409},	{30,  1600466},	{31,  1592292},
	{32,  1583862},	{33,  1575169},	{34,  1566264},	{35,  1557085},	{36,  1547687},	{37,  1538029},
	{38,  1528097},	{39,  1517913},	{40,  1507507},	{41,  1496817},	{42,  1485919},	{43,  1474738},
	{44,  1463363},	{45,  1451703},	{46,  1439856},	{47,  1427715},	{48,  1415385},	{49,  1402737},
	{50,  1389977},	{51,  1376968},	{52,  1363742},	{53,  1350225},	{54,  1336680},	{55,  1322800},
	{56,  1308734},	{57,  1294524},	{58,  1280219},	{59,  1265717},	{60,  1250885},	{61,  1236090},
	{62,  1221222},	{63,  1206137},	{64,  1190863},	{65,  1175434},	{66,  1160114},	{67,  1144501},
	{68,  1129109},	{69,  1113239},	{70,  1097698},	{71,  1082010},	{72,  1066205},	{73,  1050312},
	{74,  1034368},	{75,  1018750},	{76,  1002834},	{77,  986992},	{78,  971271},	{79,  955326},
	{80,  939579},	{81,  924088},	{82,  908470},	{83,  893016},	{84,  877585},	{85,  862256},
	{86,  847064},	{87,  831946},	{88,  816931},	{89,  802051},	{90,  787285},	{91,  772661},
	{92,  758152},	{93,  743848},	{94,  729655},	{95,  715663},	{96,  701769},	{97,  688065},
	{98,  674578},	{99,  661264},	{100, 648074},	{101, 635102},	{102, 622298},	{103, 609681},
	{104, 597274},	{105, 585015},	{106, 572922},	{107, 561098},	{108, 549479},	{109, 537993},
	{110, 526745},	{111, 515653},	{112, 504775},	{113, 494100},	{114, 483604},	{115, 473310},
	{116, 463211},	{117, 453299},	{118, 443577},	{119, 434056},	{120, 424705},	{121, 415555},
	{122, 406574},	{123, 397781},	{124, 389176},	{125, 380744}
};


static __s16 oplus_ch_thermistor_conver_temp(__s32 res, struct ntc_temp *ntc_param)
{
	int i = 0;
	int asize = 0;
	__s32 res1 = 0, res2 = 0;
	__s32 tap_value = -2000, tmp1 = 0, tmp2 = 0;

	if (!ntc_param)
		return tap_value;

	asize = ntc_param->i_table_size;

	if (res >= ntc_param->pst_temp_table[0].temperature_r) {
		tap_value = ntc_param->i_tap_min;	/* min */
	} else if (res <= ntc_param->pst_temp_table[asize - 1].temperature_r) {
		tap_value = ntc_param->i_tap_max;	/* max */
	} else {
		res1 = ntc_param->pst_temp_table[0].temperature_r;
		tmp1 = ntc_param->pst_temp_table[0].bts_temp;

		for (i = 0; i < asize; i++) {
			if (res >= ntc_param->pst_temp_table[i].temperature_r) {
				res2 = ntc_param->pst_temp_table[i].temperature_r;
				tmp2 = ntc_param->pst_temp_table[i].bts_temp;
				break;
			}
			res1 = ntc_param->pst_temp_table[i].temperature_r;
			tmp1 = ntc_param->pst_temp_table[i].bts_temp;
		}

		tap_value = (((res - res2) * tmp1) + ((res1 - res) * tmp2)) * 10 / (res1 - res2);
	}

	return tap_value;
}

static __s16 oplus_res_to_temp(struct ntc_temp *ntc_param)
{
	__s32 tres;
	__u64 dwvcrich = 0;
	__s32 chg_tmp = -100;
	__u64 dwvcrich2 = 0;

	if (!ntc_param) {
		return TEMP_25C;
	}
	dwvcrich = ((__u64)ntc_param->i_tap_over_critical_low * (__u64)ntc_param->i_rap_pull_up_voltage);
	dwvcrich2 = (ntc_param->i_tap_over_critical_low + ntc_param->i_rap_pull_up_r);
	do_div(dwvcrich, dwvcrich2);

	if (ntc_param->ui_dwvolt > ((__u32)dwvcrich)) {
		tres = ntc_param->i_tap_over_critical_low;
	} else {
		tres = (ntc_param->i_rap_pull_up_r * ntc_param->ui_dwvolt) / (ntc_param->i_rap_pull_up_voltage - ntc_param->ui_dwvolt);
	}

	/* convert register to temperature */
	chg_tmp = oplus_ch_thermistor_conver_temp(tres, ntc_param);

	return chg_tmp;
}

static int oplus_get_temp_volt(struct ntc_temp *ntc_param)
{
	int rc = 0;
	int ntc_temp_volt = 0;
	struct iio_channel		*temp_chan = NULL;
	if (!ntc_param || !pinfo) {
		return -1;
	}

	switch (ntc_param->e_ntc_type) {
	case NTC_SUB_BOARD:
		if (!pinfo->subboard_temp_chan) {
			chg_err("subboard_temp_chan NULL\n");
			return -1;
		}
		temp_chan = pinfo->subboard_temp_chan;
		break;
	case NTC_CHARGER_IC:
		if (!pinfo->chargeric_temp_chan) {
			chg_err("chargeric_temp_chan NULL\n");
			return -1;
		}
		temp_chan = pinfo->chargeric_temp_chan;
		break;
	default:
		break;
	}

	if (!temp_chan) {
		chg_err("unsupported ntc %d\n", ntc_param->e_ntc_type);
		return -1;
	}
	rc = iio_read_channel_processed(temp_chan, &ntc_temp_volt);
	if (rc < 0) {
		chg_err("read ntc_temp_chan volt failed, rc=%d\n", rc);
	}

	if (ntc_temp_volt <= 0) {
		ntc_temp_volt = ntc_param->i_25c_volt;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	ntc_temp_volt = ntc_temp_volt * 1500 / 4096;
#endif
	/* chg_err("ntc_temp_volt:%d\n", ntc_temp_volt); */

	return ntc_temp_volt;
}

int oplus_force_get_subboard_temp(void)
{
	int subboard_temp = 0;
	static bool is_param_init = false;
	static struct ntc_temp ntc_param = {0};

	if (!pinfo) {
		chg_err("null pinfo\n");
		return TEMP_25C;
	}

	if (!is_param_init) {
		ntc_param.e_ntc_type = NTC_SUB_BOARD;
		ntc_param.i_tap_over_critical_low = 4397119;
		ntc_param.i_rap_pull_up_r = 200000;
		ntc_param.i_rap_pull_up_voltage = 1800;
		ntc_param.i_tap_min = -400;
		ntc_param.i_tap_max = 1250;
		ntc_param.i_25c_volt = 2457;
		ntc_param.pst_temp_table = sub_board_temp_table;
		ntc_param.i_table_size = (sizeof(sub_board_temp_table) / sizeof(struct temp_param));
		is_param_init = true;

		/* chg_err("get_PCB_Version() = %d\n",get_PCB_Version()); */
				chg_err("ntc_type:%d,critical_low:%d,pull_up_r=%d,pull_up_voltage=%d,tap_min=%d,tap_max=%d,table_size=%d\n",
					ntc_param.e_ntc_type, ntc_param.i_tap_over_critical_low, ntc_param.i_rap_pull_up_r,
					ntc_param.i_rap_pull_up_voltage, ntc_param.i_tap_min, ntc_param.i_tap_max, ntc_param.i_table_size);
	}
	ntc_param.ui_dwvolt = oplus_get_temp_volt(&ntc_param);
	subboard_temp = oplus_res_to_temp(&ntc_param);

	if (pinfo->support_ntc_01c_precision) {
		pinfo->i_sub_board_temp = subboard_temp;
	} else {
		pinfo->i_sub_board_temp = subboard_temp / 10;
	}
	/* chg_err("subboard_temp:%d\n", subboard_temp); */
	return pinfo->i_sub_board_temp;
}
EXPORT_SYMBOL(oplus_force_get_subboard_temp);

static void get_chargeric_temp(struct charger_data *pdata)
{
	int chargeric_temp = 0;
	static bool is_param_init = false;
	static struct ntc_temp ntc_param = {0};

	if (!pinfo || !pdata) {
		chg_err("null pinfo or pdata\n");
		return;
	}

	if (!is_param_init) {
		ntc_param.e_ntc_type = NTC_CHARGER_IC;
		ntc_param.i_tap_over_critical_low = 425100;
		ntc_param.i_rap_pull_up_r = g_rap_pull_up_r0;
		ntc_param.i_rap_pull_up_voltage = 1800;
		ntc_param.i_tap_min = -400;
		ntc_param.i_tap_max = 1250;
		ntc_param.i_25c_volt = 2457;
		if(g_rap_pull_up_r0 == PULL_UP_R0_390K) {
			ntc_param.pst_temp_table = chargeric_temp_table_390k;
			ntc_param.i_table_size = (sizeof(chargeric_temp_table_390k) / sizeof(struct temp_param));
		} else {
			ntc_param.pst_temp_table = chargeric_temp_table;
			ntc_param.i_table_size = (sizeof(chargeric_temp_table) / sizeof(struct temp_param));
		}
		is_param_init = true;
		chg_err("ntc_type:%d,critical_low:%d,pull_up_r=%d,pull_up_voltage=%d,tap_min=%d,tap_max=%d,table_size=%d\n",
			ntc_param.e_ntc_type, ntc_param.i_tap_over_critical_low, ntc_param.i_rap_pull_up_r,
			ntc_param.i_rap_pull_up_voltage, ntc_param.i_tap_min, ntc_param.i_tap_max, ntc_param.i_table_size);
	}

	ntc_param.ui_dwvolt = oplus_get_temp_volt(&ntc_param);
	chargeric_temp = oplus_res_to_temp(&ntc_param);

	if (pinfo->support_ntc_01c_precision) {
		pdata->chargeric_temp = chargeric_temp;
	} else {
		pdata->chargeric_temp = chargeric_temp / 10;
	}
	chg_err("chargeric_temp:%d\n", chargeric_temp);
	return;
}
#endif

//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_usbtemp_set_vbus_exist(bool exist)
{
	if (!pinfo) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: pinfo not ready!\n", __func__);
		return;
	}

	printk(KERN_ERR "[OPLUS_CHG][%s]: vbus_exist: %d\n", __func__, exist);
	pinfo->data.vbus_exist = exist;
}
EXPORT_SYMBOL(oplus_usbtemp_set_vbus_exist);

bool usbtemp_lowvbus_detect_support(void)
{
	if (!pinfo) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: pinfo not ready!\n", __func__);
		return false;
	}

	if (pinfo->usbtemp_lowvbus_detect) {
		return true;
	}

	return false;
}
EXPORT_SYMBOL(usbtemp_lowvbus_detect_support);

static bool oplus_usbtemp_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio))
		return true;

	return false;
}

static bool oplus_ntc_ctrl_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.ntcctrl_gpio))
		return true;

	return false;
}

/**************************************************************
 * bit[0]=0: NO standard typec device/cable connected(ccdetect gpio in high level)
 * bit[0]=1: standard typec device/cable connected(ccdetect gpio in low level)
 * bit[1]=0: NO OTG typec device/cable connected
 * bit[1]=1: OTG typec device/cable connected
 **************************************************************/

bool oplus_get_otg_online_status_default(void)
{
	if (!g_oplus_chip || !pinfo || !pinfo->tcpc) {
		chg_err("fail to init oplus_chip\n");
		return false;
	}

	if (tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC)
		g_oplus_chip->otg_online = true;
	else
		g_oplus_chip->otg_online = false;
	return g_oplus_chip->otg_online;
}

int oplus_get_otg_online_status(void)
{
	return oplus_get_otg_online_status_default();
}

int oplus_get_typec_cc_orientation(void)
{
	int val = 0;

	if (pinfo != NULL && pinfo->tcpc != NULL) {
		if (tcpm_inquire_typec_attach_state(pinfo->tcpc) != TYPEC_UNATTACHED) {
			val = (int)tcpm_inquire_cc_polarity(pinfo->tcpc) + 1;
		} else {
			val = 0;
		}
		if (val != 0)
			printk(KERN_ERR "[OPLUS_CHG][%s]: cc[%d]\n", __func__, val);
	} else {
		val = 0;
	}

	return val;
}

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->chgic_mtk.oplus_info->ccdetect_gpio)){
		return true;
	}

	return false;
}

bool oplus_ccdetect_support_check(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(chip) == true)
		return true;

	return false;
}

int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->pinctrl)) {
		chg_err("get ccdetect_pinctrl fail\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->ccdetect_active = pinctrl_lookup_state(chip->chgic_mtk.oplus_info->pinctrl, "ccdetect_active");
	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->ccdetect_active)) {
		chg_err("get ccdetect_active fail\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->ccdetect_sleep = pinctrl_lookup_state(chip->chgic_mtk.oplus_info->pinctrl, "ccdetect_sleep");
	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->ccdetect_sleep)) {
		chg_err("get ccdetect_sleep fail\n");
		return -EINVAL;
	}
	if (chip->chgic_mtk.oplus_info->ccdetect_gpio > 0) {
		gpio_direction_input(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	}

	pinctrl_select_state(chip->chgic_mtk.oplus_info->pinctrl,  chip->chgic_mtk.oplus_info->ccdetect_active);
	return 0;
}

void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}
	chip->chgic_mtk.oplus_info->ccdetect_irq = gpio_to_irq(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	printk(KERN_ERR "[OPLUS_CHG][%s]: chip->chgic_mtk.oplus_info->ccdetect_gpio[%d]!\n", __func__, chip->chgic_mtk.oplus_info->ccdetect_gpio);

}

int oplus_chg_ccdetect_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}
	chip->chgic_mtk.oplus_info->ccdetect_gpio = of_get_named_gpio(node, "qcom,ccdetect-gpio", 0);
	if (chip->chgic_mtk.oplus_info->ccdetect_gpio <= 0) {
		chg_err("Couldn't read qcom,ccdetect-gpio rc=%d, qcom,ccdetect-gpio:%d\n",
				rc, chip->chgic_mtk.oplus_info->ccdetect_gpio);
	} else {
		if (oplus_ccdetect_support_check() == true) {
			rc = gpio_request(chip->chgic_mtk.oplus_info->ccdetect_gpio, "ccdetect-gpio");
			if (rc) {
				chg_err("unable to request ccdetect_gpio:%d\n",
						chip->chgic_mtk.oplus_info->ccdetect_gpio);
			} else {
				rc = oplus_ccdetect_gpio_init(chip);
				if (rc){
					chg_err("unable to init ccdetect_gpio:%d\n",
							chip->chgic_mtk.oplus_info->ccdetect_gpio);
				}else{
					oplus_ccdetect_irq_init(chip);
					}
			}
		}
		chg_err("ccdetect-gpio:%d\n", chip->chgic_mtk.oplus_info->ccdetect_gpio);
	}

	return rc;
}

static bool oplus_usbtemp_check_is_support(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_usbtemp_check_is_gpio(chip) == true)
		return true;

	chg_err("not support, return false\n");

	return false;
}

static int oplus_dischg_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_enable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_enable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
		chg_err("get dischg_enable fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_disable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_disable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable)) {
		chg_err("get dischg_disable fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);

	return 0;
}

void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	int usbtemp_volt = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (!pinfo->usb_temp_v_l_chan
		|| !pinfo->usb_temp_v_r_chan) {
		chg_err("usb_temp_v_l_chan or usb_temp_v_r_chan NULL\n");
		return;
	}

	rc = iio_read_channel_processed(pinfo->usb_temp_v_l_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("read usb_temp_v_l_chan volt failed, rc=%d\n", rc);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	chip->usbtemp_volt_l = usbtemp_volt * 1500 / 4096;
#else
	chip->usbtemp_volt_l = usbtemp_volt;
#endif


	rc = iio_read_channel_processed(pinfo->usb_temp_v_r_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("read usb_temp_v_r_chan volt failed, rc=%d\n", rc);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	chip->usbtemp_volt_r = usbtemp_volt * 1500 / 4096;
#else
	chip->usbtemp_volt_r = usbtemp_volt;
#endif

	/* chg_err("usbtemp_volt: %d, %d\n", chip->usbtemp_volt_r, chip->usbtemp_volt_l); */

	return;
}
EXPORT_SYMBOL(oplus_get_usbtemp_volt);


static bool oplus_chg_get_vbus_status(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	return chip->charger_exist || pinfo->data.vbus_exist;
}

void oplus_usbtemp_recover_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return;
	}
	oplus_usbtemp_recover_func(g_oplus_chip);
}

/*static void usbtemp_callback(unsigned long a)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
	oplus_clear_usb_status(USB_TEMP_HIGH);
	chg_err("usbtemp exit after 4hours \n");
}

static void usbtemp_exit_init (void)
{
	init_timer(& usbtemp_timer);
	usbtemp_timer.expires = jiffies + ( 14400 * HZ);    // 4 hours
	usbtemp_timer.function = &usbtemp_callback ;
	usbtemp_timer.data = ((unsigned long)0);
}*/



static void oplus_usbtemp_thread_init(void)
{
	if (g_oplus_chip->support_usbtemp_protect_v2)
		oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_common_new_method, g_oplus_chip, "usbtemp_kthread");
	else
		oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_common, g_oplus_chip, "usbtemp_kthread");
	if (IS_ERR(oplus_usbtemp_kthread)) {
		chg_err("failed to cread oplus_usbtemp_kthread\n");
	}
}

void oplus_wake_up_usbtemp_thread(void)
{
	if (oplus_usbtemp_check_is_support() == true) {
		if (is_usbtemp_thread_init_done == true) {
			if (g_oplus_chip->support_usbtemp_protect_v2)
				wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq_new_method);
			else
				wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq);
		}
	}
}

static int oplus_chg_usbtemp_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_gpio = of_get_named_gpio(node, "qcom,dischg-gpio", 0);
	if (chip->normalchg_gpio.dischg_gpio <= 0) {
		chg_err("Couldn't read qcom,dischg-gpio rc=%d, qcom,dischg-gpio:%d\n",
				rc, chip->normalchg_gpio.dischg_gpio);
	} else {
		if (oplus_usbtemp_check_is_support() == true) {
			rc = gpio_request(chip->normalchg_gpio.dischg_gpio, "dischg-gpio");
			if (rc) {
				chg_err("unable to request dischg-gpio:%d\n",
						chip->normalchg_gpio.dischg_gpio);
			} else {
				rc = oplus_dischg_gpio_init(chip);
				if (rc)
					chg_err("unable to init dischg-gpio:%d\n",
							chip->normalchg_gpio.dischg_gpio);
			}
		}
		chg_err("dischg-gpio:%d\n", chip->normalchg_gpio.dischg_gpio);
	}

	return rc;
}

static bool oplus_mtk_hv_flashled_check_is_gpio()
{
	if (gpio_is_valid(mtkhv_flashled_pinctrl.chgvin_gpio) && gpio_is_valid(mtkhv_flashled_pinctrl.pmic_chgfunc_gpio)) {
		return true;
	} else {
		chg_err("[%s] fail\n", __func__);
		return false;
	}
}
static int oplus_mtk_hv_flashled_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("[%s] oplus_chip or device tree info. missing\n", __func__);
		return -EINVAL;
	}

	mtkhv_flashled_pinctrl.hv_flashled_support = of_property_read_bool(node, "qcom,hv_flashled_support");
	if (mtkhv_flashled_pinctrl.hv_flashled_support == false) {
		chg_err("[%s] hv_flashled_support not support\n", __func__);
		return -EINVAL;
	}
	mutex_init(&mtkhv_flashled_pinctrl.chgvin_mutex);

	mtkhv_flashled_pinctrl.chgvin_gpio = of_get_named_gpio(node, "qcom,chgvin", 0);
	mtkhv_flashled_pinctrl.pmic_chgfunc_gpio = of_get_named_gpio(node, "qcom,pmic_chgfunc", 0);
	if (mtkhv_flashled_pinctrl.chgvin_gpio <= 0 || mtkhv_flashled_pinctrl.pmic_chgfunc_gpio <= 0) {
		chg_err("read dts fail %d %d\n", mtkhv_flashled_pinctrl.chgvin_gpio, mtkhv_flashled_pinctrl.pmic_chgfunc_gpio);
	} else {
		if (oplus_mtk_hv_flashled_check_is_gpio() == true) {
			rc = gpio_request(mtkhv_flashled_pinctrl.chgvin_gpio, "chgvin");
			if (rc ) {
				chg_err("unable to request chgvin:%d\n",
						mtkhv_flashled_pinctrl.chgvin_gpio);
			} else {
				mtkhv_flashled_pinctrl.pinctrl= devm_pinctrl_get(chip->dev);
				//chgvin
				mtkhv_flashled_pinctrl.chgvin_enable =
					pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "chgvin_enable");
				if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.chgvin_enable)) {
					chg_err("get chgvin_enable fail\n");
					return -EINVAL;
				}

				mtkhv_flashled_pinctrl.chgvin_disable =
					pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "chgvin_disable");
				if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.chgvin_disable)) {
					chg_err("get chgvin_disable fail\n");
					return -EINVAL;
				}
				pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);


				rc = gpio_request(mtkhv_flashled_pinctrl.pmic_chgfunc_gpio, "pmic_chgfunc");
				if (rc ) {
					chg_err("unable to request pmic_chgfunc:%d\n",
							mtkhv_flashled_pinctrl.pmic_chgfunc_gpio);
				} else {

					//pmic_chgfunc
					mtkhv_flashled_pinctrl.pmic_chgfunc_enable =
						pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "pmic_chgfunc_enable");
					if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.pmic_chgfunc_enable)) {
						chg_err("get pmic_chgfunc_enable fail\n");
						return -EINVAL;
					}

					mtkhv_flashled_pinctrl.pmic_chgfunc_disable =
						pinctrl_lookup_state(mtkhv_flashled_pinctrl.pinctrl, "pmic_chgfunc_disable");
					if (IS_ERR_OR_NULL(mtkhv_flashled_pinctrl.pmic_chgfunc_disable)) {
						chg_err("get pmic_chgfunc_disable fail\n");
						return -EINVAL;
					}
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.pmic_chgfunc_disable);
				}

			}
		}
		//chg_err("mtk_hv_flash_led:%d\n", chip->normalchg_gpio.shortc_gpio);
	}

	chg_err("mtk_hv_flash_led:%d\n", rc);
	return rc;

}
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//


//====================================================================//
static int oplus_chg_parse_custom_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;	
	if (chip == NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_chargerid_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_chargerid_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_shipmode_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_shipmode_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_shortc_hw_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_shortc_hw_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_usbtemp_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_usbtemp_parse_dt fail!\n", __func__);
		return -EINVAL;
	}

	rc = oplus_chg_ccdetect_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_ccdetect_parse_dt fail!\n", __func__);
	}

	rc = oplus_mtk_hv_flashled_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_mtk_hv_flashled_dt fail!\n", __func__);
		return -EINVAL;
	}
	return rc;
}
//====================================================================//


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
/************************************************/
/* Power Supply Functions
*************************************************/
static int mt_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;

	rc = oplus_ac_get_property(psy, psp, val);
	return rc;
}

static int mt_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	return oplus_usb_get_property(psy, psp, val);
}

static int battery_prop_is_writeable(struct power_supply *psy,
	enum power_supply_property psp)
{
	return oplus_battery_property_is_writeable(psy, psp);
}

static int battery_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	return oplus_battery_set_property(psy, psp, val);
}

static int battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;
		switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
			if (g_oplus_chip && (g_oplus_chip->ui_soc == 0)) {
				val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
				chg_err("bat pro POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, should shutdown!!!\n");
			}
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			if (g_oplus_chip) {
				val->intval = g_oplus_chip->batt_fcc * 1000;
			}
			break;
		case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
			val->intval = 0;
			break;
		default:
			rc = oplus_battery_get_property(psy, psp, val);
			break;
		}
	return 0;
}

static enum power_supply_property mt_ac_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property mt_usb_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static enum power_supply_property battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static int oplus_power_supply_init(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct oplus_chg_chip *mt_chg = NULL;

	if (chip == NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}
	mt_chg = chip;

	mt_chg->ac_psd.name = "ac";
	mt_chg->ac_psd.type = POWER_SUPPLY_TYPE_MAINS;
	mt_chg->ac_psd.properties = mt_ac_properties;
	mt_chg->ac_psd.num_properties = ARRAY_SIZE(mt_ac_properties);
	mt_chg->ac_psd.get_property = mt_ac_get_property;
	mt_chg->ac_cfg.drv_data = mt_chg;

	mt_chg->usb_psd.name = "usb";
	mt_chg->usb_psd.type = POWER_SUPPLY_TYPE_USB;
	mt_chg->usb_psd.properties = mt_usb_properties;
	mt_chg->usb_psd.num_properties = ARRAY_SIZE(mt_usb_properties);
	mt_chg->usb_psd.get_property = mt_usb_get_property;
	mt_chg->usb_cfg.drv_data = mt_chg;
    
	mt_chg->battery_psd.name = "battery";
	mt_chg->battery_psd.type = POWER_SUPPLY_TYPE_BATTERY;
	mt_chg->battery_psd.properties = battery_properties;
	mt_chg->battery_psd.num_properties = ARRAY_SIZE(battery_properties);
	mt_chg->battery_psd.get_property = battery_get_property;
	mt_chg->battery_psd.set_property = battery_set_property;
	mt_chg->battery_psd.property_is_writeable = battery_prop_is_writeable;

	mt_chg->ac_psy = power_supply_register(mt_chg->dev, &mt_chg->ac_psd,
			&mt_chg->ac_cfg);
	if (IS_ERR(mt_chg->ac_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply ac: %ld\n",
			PTR_ERR(mt_chg->ac_psy));
		ret = PTR_ERR(mt_chg->ac_psy);
		goto err_ac_psy;
	}

	mt_chg->usb_psy = power_supply_register(mt_chg->dev, &mt_chg->usb_psd,
			&mt_chg->usb_cfg);
	if (IS_ERR(mt_chg->usb_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply usb: %ld\n",
			PTR_ERR(mt_chg->usb_psy));
		ret = PTR_ERR(mt_chg->usb_psy);
		goto err_usb_psy;
	}

	mt_chg->batt_psy = power_supply_register(mt_chg->dev, &mt_chg->battery_psd,
			NULL);
	if (IS_ERR(mt_chg->batt_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply battery: %ld\n",
			PTR_ERR(mt_chg->batt_psy));
		ret = PTR_ERR(mt_chg->batt_psy);
		goto err_battery_psy;
	}

	chg_err("%s OK\n", __func__);
	return 0;

err_battery_psy:
	power_supply_unregister(mt_chg->usb_psy);
err_usb_psy:
	power_supply_unregister(mt_chg->ac_psy);
err_ac_psy:

	return ret;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_set_otg_switch_status(bool value)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: otg switch[%d]\n", __func__, value);
		tcpm_typec_change_role_postpone(pinfo->tcpc, value ? TYPEC_ROLE_TRY_SNK : TYPEC_ROLE_SNK, true);
	}
}
EXPORT_SYMBOL(oplus_set_otg_switch_status);
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
int oplus_chg_get_mmi_status(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 1;
	}
	if (chip->mmi_chg == 0)
		printk(KERN_ERR "[OPLUS_CHG][%s]: mmi_chg[%d]\n", __func__, chip->mmi_chg);
	return chip->mmi_chg;
}
EXPORT_SYMBOL(oplus_chg_get_mmi_status);
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
#define VBUS_9V	9000
#define VBUS_5V	5000
#define IBUS_2A	2000
#define IBUS_3A	3000
int oplus_chg_get_pd_type(void)
{
	if (pinfo != NULL) {
		if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
			pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
			pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
			return true;
		//return mtk_pdc_check_charger(pinfo);
	}
	return false;
}

int oplus_check_cc_mode(void) {
	const char *tcpc_name = "type_c_port0";
	struct tcpc_device *tcpc_dev;

	tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (IS_ERR_OR_NULL(tcpc_dev)) {
		chg_err("tcpc info error\n");
		return -EINVAL;
	}

	return tcpm_inquire_typec_role(tcpc_dev);
}

int oplus_mt6360_pd_setup_forsvooc(void)
{
	int vbus_mv = VBUS_5V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int i;

	if (chip->pd_svooc) {
		printk(KERN_ERR, "%s pd_svooc support\n", __func__);
		return 0;
	}

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	printk(KERN_ERR "%s: pd_type %d pd9v svooc [%d %d %d]", __func__, pinfo->pd_type, chip->limits.vbatt_pdqc_to_9v_thr, chip->limits.vbatt_pdqc_to_5v_thr, chip->batt_volt);
	if (oplus_vooc_get_allow_reading() == true) {
		chip->charger_volt = chip->chg_ops->get_charger_volt();
	}
	if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90
		&& chip->temperature <= 530 && chip->cool_down_force_5v == false
		&& (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
		if (is_vooc_support_single_batt_svooc() == true) {
			vooc_enable_cp_for_pdqc();
		}
		if (oplus_chg_get_voocphy_support()) {
			oplus_voocphy_set_pdqc_config();
		}
		if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
			adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
			for (i = 0; i < cap.nr; i++) {
				printk(KERN_ERR "PD APDO cap %d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n", i,
					cap.max_mv[i], cap.min_mv[i], cap.ma[i],
					cap.type[i], cap.pwr_limit[i], cap.pdp);
			}

			for (i = 0; i < cap.nr; i++) {
				if (cap.min_mv[i] <= VBUS_9V && VBUS_9V <= cap.max_mv[i]) {
					vbus_mv = VBUS_9V;
					ibus_ma = cap.ma[i];
					if (ibus_ma > IBUS_2A)
						ibus_ma = IBUS_2A;
					break;
				}
			}
		} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
			|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
			adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
			for (i = 0; i < cap.nr; i++) {
				printk(KERN_ERR "PD cap %d: mV:%d,%d mA:%d type:%d\n", i,
					cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
			}

			for (i = 0; i < cap.nr; i++) {
				if (VBUS_9V <= cap.max_mv[i]) {
					vbus_mv = cap.max_mv[i];
					ibus_ma = cap.ma[i];
					if (ibus_ma > IBUS_2A)
						ibus_ma = IBUS_2A;
					break;
				}
			}
		} else {
			vbus_mv = VBUS_5V;
			ibus_ma = IBUS_2A;
		}

		printk(KERN_ERR "PD request: %dmV, %dmA, pdqc_9v_voltage_adaptive:%d\n", vbus_mv, ibus_ma, chip->pdqc_9v_voltage_adaptive);
		ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		if (chip->pdqc_9v_voltage_adaptive) {
			msleep(300);
			chip->charger_volt = chip->chg_ops->get_charger_volt();
			if ((vbus_mv >= VBUS_9V) && (chip->charger_volt > 7500)) {
				oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_5VTO9V);
			} else {
				oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_9VTO5V);
			}
		}
	} else {
		if (chip->charger_volt > 7500 &&
			(chip->calling_on || chip->soc >= 90 || chip->camera_on || chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr
			|| chip->temperature > 530 || chip->cool_down_force_5v == true)) {
			vbus_mv = VBUS_5V;
			ibus_ma = IBUS_3A;

			printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
			ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
			if (chip->pdqc_9v_voltage_adaptive) {
				oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_9VTO5V);
			}
		}

		printk(KERN_ERR "%s: pd9v svooc  default[%d %d]", __func__, em_mode, chip->batt_volt);
	}

	return ret;
}

int oplus_mt6360_pd_setup_forvooc(void)
{
	int vbus_mv = VBUS_5V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int i;

	if (chip->pd_svooc) {
		printk(KERN_ERR, "%s pd_svooc support\n", __func__);
		return 0;
	}

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	printk(KERN_ERR "%s: pd_type %d pd for vooc30_project [%d]",
			__func__, pinfo->pd_type, chip->batt_volt);

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
		for (i = 0; i < cap.nr; i++) {
			printk(KERN_ERR "PD APDO cap %d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n", i,
				cap.max_mv[i], cap.min_mv[i], cap.ma[i],
				cap.type[i], cap.pwr_limit[i], cap.pdp);
		}

		for (i = 0; i < cap.nr; i++) {
			if (cap.min_mv[i] <= VBUS_5V && VBUS_5V <= cap.max_mv[i]) {
				vbus_mv = VBUS_5V;
				ibus_ma = cap.ma[i];
				if (ibus_ma > IBUS_3A)
					ibus_ma = IBUS_3A;
				break;
			}
		}
	} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
		|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
		for (i = 0; i < cap.nr; i++) {
			printk(KERN_ERR "PD cap %d: mV:%d,%d mA:%d type:%d\n", i,
				cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
		}

		for (i = 0; i < cap.nr; i++) {
			if (VBUS_5V <= cap.max_mv[i]) {
				vbus_mv = cap.max_mv[i];
				ibus_ma = cap.ma[i];
				if (ibus_ma > IBUS_3A)
					ibus_ma = IBUS_3A;
				break;
			}
		}
	} else {
		vbus_mv = VBUS_5V;
		ibus_ma = IBUS_2A;
	}

	printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
	ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);

	return ret;
}

int oplus_mt6360_pd_setup(void)
{
	int vbus_mv = VBUS_5V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int i;

#ifdef OPLUS_FEATURE_CHG_BASIC
	if(pinfo->data.pd_not_support) {
		vbus_mv = VBUS_5V;
		ibus_ma = IBUS_2A;
		ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		return ret;
	}
#endif


	if (is_mtksvooc_project) {
		ret = oplus_mt6360_pd_setup_forsvooc();
	} else if (is_vooc30_project()) {
		ret = oplus_mt6360_pd_setup_forvooc();
	}
	else {
		cap.nr = 0;
		cap.pdp = 0;
		for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
			cap.max_mv[i] = 0;
			cap.min_mv[i] = 0;
			cap.ma[i] = 0;
			cap.type[i] = 0;
			cap.pwr_limit[i] = 0;
		}

		printk(KERN_ERR "pd_type: %d\n", pinfo->pd_type);
		printk(KERN_ERR "%s: [%d-%d-%d-%d-%d-%d]", __func__, chip->calling_on,
		chip->camera_on, chip->charger_volt, chip->soc, chip->temperature, chip->cool_down_force_5v);
		if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90
			&& chip->temperature <= 420 && chip->cool_down_force_5v == false) {
			if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
				adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
				for (i = 0; i < cap.nr; i++) {
					printk(KERN_ERR "PD APDO cap %d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n", i,
						cap.max_mv[i], cap.min_mv[i], cap.ma[i],
						cap.type[i], cap.pwr_limit[i], cap.pdp);
				}

				for (i = 0; i < cap.nr; i++) {
					if (cap.min_mv[i] <= VBUS_9V && VBUS_9V <= cap.max_mv[i]) {
						vbus_mv = VBUS_9V;
						ibus_ma = cap.ma[i];
						if (ibus_ma > IBUS_2A)
							ibus_ma = IBUS_2A;
						break;
					}
				}
			} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
				|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
				adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
				for (i = 0; i < cap.nr; i++) {
					printk(KERN_ERR "PD cap %d: mV:%d,%d mA:%d type:%d\n", i,
						cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
				}

				for (i = 0; i < cap.nr; i++) {
					if (VBUS_9V <= cap.max_mv[i]) {
						vbus_mv = cap.max_mv[i];
						ibus_ma = cap.ma[i];
						if (ibus_ma > IBUS_2A)
							ibus_ma = IBUS_2A;
						break;
					}
				}
			} else {
				vbus_mv = VBUS_5V;
				ibus_ma = IBUS_2A;
			}

			printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
			ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		} else {
			if (chip->charger_volt > 7500 &&
				(chip->calling_on || chip->camera_on || chip->soc >= 90 || chip->batt_volt >= 4450
				|| chip->temperature > 420 || chip->cool_down_force_5v == true)) {
				vbus_mv = VBUS_5V;
				ibus_ma = IBUS_3A;

				printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
				ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
			}
		}
	}

	return ret;
}


int oplus_chg_enable_hvdcp_detect(void);
int oplus_chg_set_pd_config(void)
{
	return oplus_mt6360_pd_setup();
}

int oplus_chg_enable_qc_detect(void)
{
	return oplus_chg_enable_hvdcp_detect();
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
int oplus_chg_get_charger_subtype(void)
{
	if (!pinfo)
		return CHARGER_SUBTYPE_DEFAULT;

	if (!oplus_chg_is_support_qcpd())
		return CHARGER_SUBTYPE_DEFAULT;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
		pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
		pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return CHARGER_SUBTYPE_PD;
		
	if (mt6360_get_hvdcp_type() == POWER_SUPPLY_TYPE_USB_HVDCP)
		return CHARGER_SUBTYPE_QC;

	return CHARGER_SUBTYPE_DEFAULT;
}

void oplus_chg_choose_gauge_curve(int index_curve)
{
	static last_curve_index = -1;
	int target_index_curve = -1;

	if (!pinfo)
		target_index_curve = CHARGER_NORMAL_CHG_CURVE;

	if (index_curve == CHARGER_SUBTYPE_QC
			|| index_curve == CHARGER_SUBTYPE_PD
			|| index_curve == CHARGER_SUBTYPE_FASTCHG_VOOC) {
		target_index_curve = CHARGER_FASTCHG_VOOC_AND_QCPD_CURVE;
	} else if (index_curve == 0) {
		target_index_curve = CHARGER_NORMAL_CHG_CURVE;
	} else {
		target_index_curve = CHARGER_FASTCHG_SVOOC_CURVE;
	}

	printk(KERN_ERR "%s: index_curve() =%d  target_index_curve =%d last_curve_index =%d",
		__func__, index_curve, target_index_curve, last_curve_index);

	if (target_index_curve != last_curve_index) {
		set_charge_power_sel(target_index_curve);
		last_curve_index = target_index_curve;
	}
	return;
}

#define QC_CHARGER_VOLTAGE_HIGH 6500
#define QC_SOC_HIGH 90
#define QC_TEMP_HIGH 420
bool oplus_chg_check_qchv_condition(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("oplus_chip is null\n");
		return false;
	}

	chip->charger_volt = chip->chg_ops->get_charger_volt();
	if (!chip->calling_on && !chip->camera_on && chip->charger_volt < QC_CHARGER_VOLTAGE_HIGH
		&& chip->soc < QC_SOC_HIGH && chip->temperature <= QC_TEMP_HIGH && !chip->cool_down_force_5v) {
		return true;
	}

	return false;
}

int oplus_chg_set_qc_config_forsvooc(void)
{
	int ret = -1;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("oplus_chip is null\n");
		return -1;
	}

	printk(KERN_ERR "%s: qc9v svooc [%d %d %d]", __func__, chip->limits.vbatt_pdqc_to_9v_thr, chip->limits.vbatt_pdqc_to_5v_thr, chip->batt_volt);
	if (oplus_vooc_get_allow_reading() == true) {
		chip->charger_volt = chip->chg_ops->get_charger_volt();
	}
	if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90
		&& chip->temperature <= 530 && chip->cool_down_force_5v == false
		&& (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {	//
		printk(KERN_ERR "%s: set qc to 9V", __func__);
		if (is_vooc_support_single_batt_svooc() == true) {
			vooc_enable_cp_for_pdqc();
		}
		if (oplus_chg_get_voocphy_support()) {
			oplus_voocphy_set_pdqc_config();
		}
		mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x18);
		oplus_notify_hvdcp_detect_stat();
		if (chip->pdqc_9v_voltage_adaptive) {
			msleep(300);
			chip->charger_volt = chip->chg_ops->get_charger_volt();
			if (chip->charger_volt > 7500) {
				oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_5VTO9V);
			}
		}
		ret = 0;
	} else {
		if (chip->charger_volt > 7500 &&
			(chip->calling_on || chip->soc >= 90 || chip->camera_on
			|| chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr || chip->temperature > 530 || chip->cool_down_force_5v == true)) {
			printk(KERN_ERR "%s: set qc to 5V", __func__);
			mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x15);
			if (chip->pdqc_9v_voltage_adaptive) {
				oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_9VTO5V);
			}
			ret = 0;
		}
		printk(KERN_ERR "%s: qc9v svooc  default[%d %d]", __func__, em_mode, chip->batt_volt);
	}

	return ret;
}

int oplus_chg_set_qc_config(void)
{
	int ret = -1;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("oplus_chip is null\n");
		return -1;
	}

	if (is_vooc30_project()) {
		pr_err("vooc30_project, not support QC\n");
		return -1;
	}

	if (pinfo->data.qc_not_support) {
		pr_err("not support QC\n");
		return -1;
	}

	if (is_mtksvooc_project) {
		ret = oplus_chg_set_qc_config_forsvooc();
	} else {
		if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90
			&& chip->temperature <= 420 && chip->cool_down_force_5v == false) {
			printk(KERN_ERR "%s: set qc to 9V", __func__);
			mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x18);
			ret = 0;
		} else {
			if (chip->charger_volt > 7500 &&
				(chip->calling_on || chip->camera_on || chip->soc >= 90 || chip->batt_volt >= 4450
				|| chip->temperature > 420 || chip->cool_down_force_5v == true)) {
				printk(KERN_ERR "%s: set qc to 5V", __func__);
				mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x15);
				ret = 0;
			}
		}
	}
	return ret;
}

int oplus_chg_enable_hvdcp_detect(void)
{
	if (is_vooc30_project()) {
		pr_err("vooc30_project , not support hvdcp\n");
		return 0;
	}

	if (pinfo->data.qc_not_support) {
		pr_err("not support hvdc\n");
		return -1;
	}

	mt6360_enable_hvdcp_detect();

	return 0;
}

static void mt6360_step_charging_work(struct work_struct *work)
{
	int tbat_normal_current = 0;
	int step_chg_current = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("%s, oplus_chip null\n", __func__);
		return;
	}

	if (!pinfo) {
		pr_err("%s, pinfo null\n", __func__);
		return;
	}
	if (pinfo->data.dual_charger_support) {
		if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
			tbat_normal_current = oplus_chg_get_tbatt_normal_charging_current(chip);


				if (pinfo->step_status == STEP_CHG_STATUS_STEP1) {
					pinfo->step_cnt += 5;
					if (pinfo->step_cnt >= pinfo->data.step1_time) {
						pinfo->step_status = STEP_CHG_STATUS_STEP2;
						pinfo->step_cnt = 0;
					}
				} else if (pinfo->step_status == STEP_CHG_STATUS_STEP2) {
					pinfo->step_cnt += 5;
					if (pinfo->step_cnt >= pinfo->data.step2_time) {
						pinfo->step_status = STEP_CHG_STATUS_STEP3;
						pinfo->step_cnt = 0;
					}
				} else {
					 if (pinfo->step_status == STEP_CHG_STATUS_STEP3) {
						pinfo->step_cnt = 0;
					}
			}

			if (pinfo->step_status == STEP_CHG_STATUS_STEP1)
				step_chg_current = pinfo->data.step1_current_ma;
			else if (pinfo->step_status == STEP_CHG_STATUS_STEP2)
				step_chg_current = pinfo->data.step2_current_ma;
			else if (pinfo->step_status == STEP_CHG_STATUS_STEP3)
				step_chg_current = pinfo->data.step3_current_ma;
			else
				step_chg_current = 0;

			if (step_chg_current != 0) {
				if (tbat_normal_current >= step_chg_current) {
					pinfo->step_chg_current = step_chg_current;
				} else {
					pinfo->step_chg_current = tbat_normal_current;
				}
			} else {
				pinfo->step_chg_current = tbat_normal_current;
			}

			if (pinfo->step_status != pinfo->step_status_pre) {
				pr_err("%s, step status: %d, step charging current: %d\n", __func__, pinfo->step_status, pinfo->step_chg_current);
				oplus_mt6360_charging_current_write_fast(pinfo->step_chg_current);
				pinfo->step_status_pre = pinfo->step_status;
			}
		}

		schedule_delayed_work(&pinfo->step_charging_work, msecs_to_jiffies(5000));
	}

	return;
}

void oplus_chg_set_camera_on(bool val)
{
	if (!g_oplus_chip) {
		return;
	} else {
		g_oplus_chip->camera_on = val;
		if (g_oplus_chip->dual_charger_support || oplus_chg_get_voocphy_support()) {
			if (g_oplus_chip->charger_exist) {
				if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
					oplus_chg_set_qc_config();
				} else if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) {
					oplus_mt6360_pd_setup();
				} else {
					oplus_chg_set_flash_led_status(g_oplus_chip->camera_on);
				}
			} else if (!g_oplus_chip->charger_exist) {
				oplus_chg_set_flash_led_status(g_oplus_chip->camera_on);
			}
		}

		if (mtkhv_flashled_pinctrl.hv_flashled_support) {
			chr_err("%s: bc1.2_done = %d camera_on %d\n", __func__, mtkhv_flashled_pinctrl.bc1_2_done, val);
			if (mtkhv_flashled_pinctrl.bc1_2_done) {
				if (val) {
					mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					if(is_vooc_support_single_batt_svooc() != true) {
						pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
						chg_err("chgvin_gpio = %d\n", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
					} else {
						chg_err("cannot set camera, chgvin_gpio = %d\n", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
					}
					mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
				} else {
					mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					if (is_vooc_support_single_batt_svooc() == true) {
						chg_err("when close camera,do not change chgvin_gpio \n");
					} else {
						pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
					}
					mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					chg_err("chgvin_gpio = %d\n", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
					if (g_oplus_chip->charger_exist && POWER_SUPPLY_TYPE_USB_DCP == g_oplus_chip->charger_type) {
						if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
							chr_err("[%s] oplus_chg_set_qc_config\n", __func__);
							oplus_chg_set_qc_config();
						}
						if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) {
							chr_err("[%s] oplus_mt6360_pd_setup\n", __func__);
							oplus_mt6360_pd_setup();
						}
					}
				}
			}
		}
	}
}
EXPORT_SYMBOL(oplus_chg_set_camera_on);
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//

void oplus_set_typec_sinkonly(void)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: usbtemp occur otg switch[0]\n", __func__);
		pinfo->tcpc->typec_role_new = TYPEC_ROLE_SRC;
#ifndef OPLUS_FEATURE_CHG_BASIC
		tcpm_typec_change_role(pinfo->tcpc, TYPEC_ROLE_SNK);
#else
		tcpm_typec_change_role_postpone(pinfo->tcpc, TYPEC_ROLE_SNK, true);
#endif
	}
};
EXPORT_SYMBOL(oplus_set_typec_sinkonly);

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_set_typec_cc_open(void)
{
	if (pinfo == NULL || pinfo->tcpc == NULL)
		return;

	tcpm_typec_disable_function(pinfo->tcpc, true);
	chg_err(" !\n");
}
EXPORT_SYMBOL(oplus_set_typec_cc_open);

void oplus_usbtemp_recover_cc_open(void)
{
	if (pinfo == NULL || pinfo->tcpc == NULL)
		return;

	tcpm_typec_disable_function(pinfo->tcpc, false);
	chg_err(" !\n");
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

bool oplus_usb_or_otg_is_present(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	if(!chip) {
		return false;
	}
	return oplus_chg_get_vbus_status(chip);
}
EXPORT_SYMBOL(oplus_usb_or_otg_is_present);
//====================================================================//

#ifdef CONFIG_OPLUS_CHARGER_MTK
/*modify for cfi*/
static int oplus_get_boot_mode(void)
{
	return (int)get_boot_mode();
}

static int oplus_get_boot_reason(void)
{
	return (int)get_boot_reason();
}
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
static bool oplus_chg_is_support_qcpd(void)
{
	if (!pinfo)
		return false;
	if (pinfo->data.pd_not_support && pinfo->data.qc_not_support)
		return false;
	return true;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

struct oplus_chg_operations  mtk6360_chg_ops = {
	.dump_registers = oplus_mt6360_dump_registers,
	.kick_wdt = oplus_mt6360_kick_wdt,
	.hardware_init = oplus_mt6360_hardware_init,
	.charging_current_write_fast = oplus_mt6360_charging_current_write_fast,
	.set_aicl_point = oplus_mt6360_set_aicl_point,
	.input_current_write = oplus_mt6360_input_current_limit_write,
	.float_voltage_write = oplus_mt6360_float_voltage_write,
	.term_current_set = oplus_mt6360_set_termchg_current,
	.charging_enable = oplus_mt6360_enable_charging,
	.charging_disable = oplus_mt6360_disable_charging,
	.get_charging_enable = oplus_mt6360_check_charging_enable,
	.charger_suspend = oplus_mt6360_suspend_charger,
	.charger_unsuspend = oplus_mt6360_unsuspend_charger,
	.set_rechg_vol = oplus_mt6360_set_rechg_voltage,
	.reset_charger = oplus_mt6360_reset_charger,
	.read_full = oplus_mt6360_registers_read_full,
	.otg_enable = oplus_mt6360_otg_enable,
	.otg_disable = oplus_mt6360_otg_disable,
	.set_charging_term_disable = oplus_mt6360_set_chging_term_disable,
	.check_charger_resume = oplus_mt6360_check_charger_resume,
	.get_chg_current_step = oplus_mt6360_get_chg_current_step,
#ifdef CONFIG_OPLUS_CHARGER_MTK
	.get_charger_type = mt_power_supply_type_check,
	.get_charger_volt = battery_meter_get_charger_voltage,
	.check_chrdet_status = oplus_mt_get_vbus_status,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_boot_mode = oplus_get_boot_mode,
	.get_boot_reason = oplus_get_boot_reason,
	.get_chargerid_volt = mt_get_chargerid_volt,
	.set_chargerid_switch_val = mt_set_chargerid_switch_val ,
	.get_chargerid_switch_val  = mt_get_chargerid_switch_val,
	.get_rtc_soc = get_rtc_spare_oplus_fg_value,
	.set_rtc_soc = set_rtc_spare_oplus_fg_value,
	.set_power_off = oplus_mt_power_off,
	.get_charger_subtype = oplus_chg_get_charger_subtype,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
	.get_platform_gauge_curve = oplus_chg_choose_gauge_curve,
	.get_charger_current = oplus_mt6360_get_chg_ibus,
#else /* CONFIG_OPLUS_CHARGER_MTK */
	.get_charger_type = qpnp_charger_type_get,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.check_chrdet_status = qpnp_lbc_is_usb_chg_plugged_in,
	.get_instant_vbatt = qpnp_get_prop_battery_voltage_now,
	.get_boot_mode = get_boot_mode,
	.get_rtc_soc = qpnp_get_pmic_soc_memory,
	.set_rtc_soc = qpnp_set_pmic_soc_memory,
#endif /* CONFIG_OPLUS_CHARGER_MTK */

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oplus_mt6360_chg_get_dyna_aicl_result,
#endif
	.get_shortc_hw_gpio_status = oplus_chg_get_shortc_hw_gpio_status,
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
	.oplus_chg_get_pd_type = oplus_chg_get_pd_type,
	.oplus_chg_pd_setup = oplus_mt6360_pd_setup,
	.set_qc_config = oplus_chg_set_qc_config,
	.enable_qc_detect = oplus_chg_enable_hvdcp_detect,	//for qc 9v2a
	.check_pdphy_ready = oplus_check_pdphy_ready,
	.get_usbtemp_volt = oplus_get_usbtemp_volt,
	.set_typec_sinkonly = oplus_set_typec_sinkonly,
	.set_typec_cc_open = oplus_set_typec_cc_open,
	.oplus_usbtemp_monitor_condition = oplus_usb_or_otg_is_present,
	.check_qchv_condition = oplus_chg_check_qchv_condition,
	.is_support_qcpd = oplus_chg_is_support_qcpd,
	.check_cc_mode = oplus_check_cc_mode,
};
//====================================================================//

bool oplus_otgctl_by_buckboost(void)
{
	if (!g_oplus_chip)
		return false;

	return g_oplus_chip->vbatt_num == 2;
}

void oplus_otg_enable_by_buckboost(void)
{
	if (!g_oplus_chip || !(g_oplus_chip->chg_ops->charging_disable) || !(g_oplus_chip->chg_ops->charging_disable))
		return;

	g_oplus_chip->chg_ops->charging_disable();
	g_oplus_chip->chg_ops->otg_enable();
}

void oplus_otg_disable_by_buckboost(void)
{
	if (!g_oplus_chip || !(g_oplus_chip->chg_ops->otg_disable))
		return;

	g_oplus_chip->chg_ops->otg_disable();
}


void oplus_gauge_set_event(int event)
{
	if (NULL != pinfo) {
		charger_manager_notifier(pinfo, event);
		chr_err("[%s] notify mtkfuelgauge event = %d\n", __func__, event);
	}
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

void sc_select_charging_current(struct charger_manager *info, struct charger_data *pdata)
{

	if (pinfo->sc.g_scd_pid != 0 && pinfo->sc.disable_in_this_plug == false) {
		chr_debug("sck: %d %d %d %d %d\n",
			info->sc.pre_ibat,
			info->sc.sc_ibat,
			pdata->charging_current_limit,
			pdata->thermal_charging_current_limit,
			info->sc.solution);
		if (info->sc.pre_ibat == -1 || info->sc.solution == SC_IGNORE
			|| info->sc.solution == SC_DISABLE) {
			info->sc.sc_ibat = -1;
		} else {
			if (info->sc.pre_ibat == pdata->charging_current_limit
				&& info->sc.solution == SC_REDUCE
				&& ((pdata->charging_current_limit - 100000) >= 500000)) {
				if (info->sc.sc_ibat == -1)
					info->sc.sc_ibat = pdata->charging_current_limit - 100000;
				else if (info->sc.sc_ibat - 100000 >= 500000)
					info->sc.sc_ibat = info->sc.sc_ibat - 100000;
			}
		}
	}
	info->sc.pre_ibat =  pdata->charging_current_limit;

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
		pinfo->sc.disable_in_this_plug = true;
	} else if ((info->sc.solution == SC_REDUCE || info->sc.solution == SC_KEEP)
		&& info->sc.sc_ibat <
		pdata->charging_current_limit && pinfo->sc.g_scd_pid != 0 &&
		pinfo->sc.disable_in_this_plug == false) {
		pdata->charging_current_limit = info->sc.sc_ibat;
	}
}

#ifdef OPLUS_FEATURE_CHG_BASIC
static int oplus_chg_get_chargeric_temp_val(void)
{
	int chargeric_temp = 0;
	static bool is_param_init = false;
	static struct ntc_temp ntc_param = {0};

	if (!is_param_init) {
		ntc_param.e_ntc_type = NTC_CHARGER_IC;
		ntc_param.i_tap_over_critical_low = 4397119;
		ntc_param.i_rap_pull_up_r = g_rap_pull_up_r0;
		ntc_param.i_rap_pull_up_voltage = 1800;
		ntc_param.i_tap_min = -400;
		ntc_param.i_tap_max = 1250;
		ntc_param.i_25c_volt = 2457;
		if(g_rap_pull_up_r0 ==  PULL_UP_R0_390K) {
			ntc_param.pst_temp_table = chargeric_temp_table_390k;
			ntc_param.i_table_size = (sizeof(chargeric_temp_table_390k) / sizeof(struct temp_param));
		} else {
			ntc_param.pst_temp_table = chargeric_temp_table;
			ntc_param.i_table_size = (sizeof(chargeric_temp_table) / sizeof(struct temp_param));
		}
		is_param_init = true;
		chg_err("ntc_type:%d,critical_low:%d,pull_up_r=%d,pull_up_voltage=%d,tap_min=%d,tap_max=%d,table_size=%d\n",
			ntc_param.e_ntc_type, ntc_param.i_tap_over_critical_low, ntc_param.i_rap_pull_up_r,
			ntc_param.i_rap_pull_up_voltage, ntc_param.i_tap_min, ntc_param.i_tap_max, ntc_param.i_table_size);
	}

	ntc_param.ui_dwvolt = oplus_get_temp_volt(&ntc_param);
	chargeric_temp = oplus_res_to_temp(&ntc_param);
	/*chg_err("chargeric_temp = %d chargeric volt = %d\n", chargeric_temp, ntc_param.ui_dwvolt);*/

	return chargeric_temp;
}

#define TEMPERATURE_TBL_SIZE 166
#define OPLUS_BTB_HIGHPRICISION 1000
#define OPLUS_BTB_DEFALUT_TEMP_UV 1636364
#define OPLUS_BTB_DEFALUT_TEMP 25
static int pre_temp_idx = 70;
static int get_battery_btb_temp(void)
{
	int real_temp, uv_l, uv_r, rc;
	int temp_uv = OPLUS_BTB_DEFALUT_TEMP_UV;
	struct iio_channel	*temp_chan = pinfo->batcon_temp_chan;

	rc = iio_read_channel_processed(temp_chan, &temp_uv);
	if (rc < 0) {
		chg_err("read ntc_temp_chan volt failed, rc=%d\n", rc);
	}

	if (pre_temp_idx > TEMPERATURE_TBL_SIZE - 1) {
		pre_temp_idx = TEMPERATURE_TBL_SIZE - 1;
	} else if (pre_temp_idx < 0) {
		pre_temp_idx = 0;
	}

	if (temp_uv < BATCON_Temperature_Table[pre_temp_idx].temperature_r) {
		for (; pre_temp_idx < TEMPERATURE_TBL_SIZE - 1; pre_temp_idx++) {
			if (temp_uv <= BATCON_Temperature_Table[pre_temp_idx].temperature_r
					&& temp_uv > BATCON_Temperature_Table[pre_temp_idx+1].temperature_r)
				break;
		}
	} else if (temp_uv > BATCON_Temperature_Table[pre_temp_idx].temperature_r) {
		for (; pre_temp_idx > 0; pre_temp_idx--) {
			if (temp_uv <= BATCON_Temperature_Table[pre_temp_idx].temperature_r
					&& temp_uv > BATCON_Temperature_Table[pre_temp_idx+1].temperature_r)
				break;
		}
	}
	if ((temp_uv >= BATCON_Temperature_Table[0].temperature_r) && pre_temp_idx <= 0) {
		real_temp = BATCON_Temperature_Table[0].ntc_temp * OPLUS_BTB_HIGHPRICISION;
	} else if (temp_uv <= BATCON_Temperature_Table[TEMPERATURE_TBL_SIZE-1].temperature_r
				&& pre_temp_idx >= TEMPERATURE_TBL_SIZE-1) {
		real_temp = BATCON_Temperature_Table[TEMPERATURE_TBL_SIZE-1].ntc_temp * OPLUS_BTB_HIGHPRICISION;
	} else {
		uv_l = BATCON_Temperature_Table[pre_temp_idx].temperature_r;
		uv_r = BATCON_Temperature_Table[pre_temp_idx+1].temperature_r;
		real_temp = BATCON_Temperature_Table[pre_temp_idx].ntc_temp * OPLUS_BTB_HIGHPRICISION +
			 ((temp_uv - uv_l) * OPLUS_BTB_HIGHPRICISION) / (uv_r - uv_l);
	}
	real_temp = real_temp/OPLUS_BTB_HIGHPRICISION;
	chg_err("T_BATCON temp_uv[%d] real_temp[%d]\n", temp_uv, real_temp);
        return real_temp;
}

static int oplus_chg_get_battery_btb_temp(void)
{
	int rc = 0;
	int i;
	int batt_btb_volt = 0;
	int batt_btb_temp = 0;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 25;
	}

	if (!oplus_ntc_ctrl_is_support() || (oplus_ntc_ctrl_is_support() && (pinfo->batcon_temp_chan != NULL))) {
                if (!pinfo->batcon_temp_chan) {
                        chg_err("battery btb chan NULL\n");
                        return OPLUS_BTB_DEFALUT_TEMP;
                }
                return get_battery_btb_temp();
        }

	if (!pinfo->chargeric_temp_chan) {
		chg_err("battery btb chan NULL\n");
		batt_btb_volt = 900;
		batt_btb_temp = 25;
		return batt_btb_temp;
	}

	rc = iio_read_channel_processed(pinfo->chargeric_temp_chan, &batt_btb_volt);
	if (rc < 0) {
		chg_err("read subboard_temp_chan volt failed, rc=%d\n", rc);
	}

	if (batt_btb_volt <= 0) {
		batt_btb_volt = 2457;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	batt_btb_volt = batt_btb_volt * 1500 / 4096;
#endif

	for (i = g_oplus_chip->len_array- 1; i >= 0; i--) {
		if (g_oplus_chip->con_volt[i] >= batt_btb_volt)
			break;
		else if (i == 0)
			break;
	}
	batt_btb_temp = g_oplus_chip->con_temp[i];

	/*chg_err("battery btb temp is %d batt_btb_volt= %d\n", batt_btb_temp, batt_btb_volt);*/
	return batt_btb_temp;
}

static bool oplus_ntc_ctrl_is_support(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_ntc_ctrl_is_gpio(chip))
		return true;

	chg_err("not support, return false\n");

	return false;
}

static int oplus_get_flashlight_temp_val(void)
{
	int rc = 0;
	int temp_uv = OPLUS_FLASHLIGHT_DEFALUT_TEMP_UV;
	if (pinfo && pinfo->batcon_temp_chan) {
		rc = iio_read_channel_processed(pinfo->batcon_temp_chan, &temp_uv);
		if (rc < 0) {
			chg_err("read flashlight_temp volt failed, rc=%d\n", rc);
			return rc;
		}
		/* chg_err("read flashlight_temp volt, rc=%d, temp_uv=%d\n", rc, temp_uv); */
		return temp_uv;
	}
	return rc;
}

static void oplus_chg_batcon_adc_switch_ctrl(struct oplus_chg_chip *chip)
{
	int ntcctrl_gpio_value = 0;
	int ret = 0;

	chargeric_temp_cal = oplus_chg_get_chargeric_temp_val();

	if (!oplus_ntc_ctrl_is_support() || !pinfo || !pinfo->batcon_temp_chan) {
	    return;
	}

	ret = oplus_ntcctrl_gpio_init_cal(chip);
	if (ret) {
		chg_err("unable init ntcctrl-gpio:%d\n", chip->normalchg_gpio.ntcctrl_gpio);
		return;
	}

	ntcctrl_gpio_value = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
	if (chip->charger_exist) {
		if (ntcctrl_gpio_value == 0) {
			/* chg_err("charger exist, ntc gpio value = 0! first read flashlight_temperature!\n"); */
			flashlight_temp_uv_cal = oplus_get_flashlight_temp_val();
			mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ntcctrl_high);
			mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
			msleep(100);
			ret = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
			/* chg_err("charger exist, switch ntc gpio value to %d! then read bat btb!\n", ret); */
			battery_btb_temp_cal = oplus_chg_get_battery_btb_temp();
		} else if (ntcctrl_gpio_value == 1) {
			/* chg_err("charger exist, ntc gpio value = 1! first read bat btb!\n"); */
			battery_btb_temp_cal = oplus_chg_get_battery_btb_temp();
			mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ntcctrl_low);
			mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
			msleep(100);
			ret = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
			/* chg_err("charger exist, switch ntc gpio value to %d! then read flashlight!\n", ret); */
			flashlight_temp_uv_cal = oplus_get_flashlight_temp_val();
		} else {
			/*do nothing*/
		}
	} else {
		if (ntcctrl_gpio_value == 0) {
			/* chg_err("charger not exist, ntc gpio value = 0! do not switch ntc ctrl!\n"); */
			flashlight_temp_uv_cal = oplus_get_flashlight_temp_val();
		} else if (ntcctrl_gpio_value == 1) {
			chg_err("charger not exist, ntc gpio value = 1! need switch ntc ctrl to 0!\n");
			mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ntcctrl_low);
			mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
			msleep(100);
			ret = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
			/* chg_err("charger not exist, switch ntc gpio value to %d! then read flashlight!\n", ret); */
			flashlight_temp_uv_cal = oplus_get_flashlight_temp_val();
		} else {
			/*do nothing*/
		}
	}
	chr_debug("chargeric_temp_cal=%d, battery_btb_temp_cal=%d, flashlight_temp_uv_cal=%d\n", chargeric_temp_cal, battery_btb_temp_cal, flashlight_temp_uv_cal);
}

static void oplus_chg_adc_switch_ctrl(void)
{
	int ntcctrl_gpio_value = 0;
	int ret = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (!oplus_ntc_ctrl_is_support()) {
		return;
	}

	if (pinfo && pinfo->batcon_temp_chan) {
		oplus_chg_batcon_adc_switch_ctrl(chip);
		return;
	}

	ntcctrl_gpio_value = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
	if (chip->charger_exist) {
		if (ntcctrl_gpio_value == 0) {
			chg_err("charger exist, ntc gpio value = 0! first read chargeric!\n");
			chargeric_temp_cal = oplus_chg_get_chargeric_temp_val();
			mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ntcctrl_high);
			mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
			msleep(100);
			ret = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
			chg_err("charger exist, switch ntc gpio value to %d! then read bat btb!\n", ret);
			battery_btb_temp_cal = oplus_chg_get_battery_btb_temp();
		} else if (ntcctrl_gpio_value == 1) {
			chg_err("charger exist, ntc gpio value = 1! first read bat btb!\n");
			battery_btb_temp_cal = oplus_chg_get_battery_btb_temp();
			mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ntcctrl_low);
			mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
			msleep(100);
			ret = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
			chg_err("charger exist, switch ntc gpio value to %d! then read chargeric!\n", ret);
			chargeric_temp_cal = oplus_chg_get_chargeric_temp_val();
		} else {
			/*do nothing*/
		}
	} else {
		if (ntcctrl_gpio_value == 0) {
			chg_err("charger not exist, ntc gpio value = 0! do not switch ntc ctrl!\n");
			chargeric_temp_cal = oplus_chg_get_chargeric_temp_val();
		} else if (ntcctrl_gpio_value == 1) {
			chg_err("charger not exist, ntc gpio value = 1! need switch ntc ctrl to 0!\n");
			mutex_lock(&chip->normalchg_gpio.pinctrl_mutex);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ntcctrl_low);
			mutex_unlock(&chip->normalchg_gpio.pinctrl_mutex);
			msleep(100);
			ret = gpio_get_value(chip->normalchg_gpio.ntcctrl_gpio);
			chg_err("charger not exist, switch ntc gpio value to %d! then read chargeric!\n", ret);
			chargeric_temp_cal = oplus_chg_get_chargeric_temp_val();
		} else {
			/*do nothing*/
		}
	}
}

int oplus_get_flashlight_temp_val_cal(int *temp_uv)
{
	if (!oplus_ntc_ctrl_is_support()) {
		return -EINVAL;
	}
	if(!pinfo || !pinfo->batcon_temp_chan) {
		return -EINVAL;
	}
	*temp_uv = flashlight_temp_uv_cal;
	/* chg_err("flashlight_temp=%d\n",*temp_uv); */
	return 0;
}
EXPORT_SYMBOL(oplus_get_flashlight_temp_val_cal);

int oplus_chg_get_chargeric_temp_cal(void)
{
	return chargeric_temp_cal;
}
EXPORT_SYMBOL(oplus_chg_get_chargeric_temp_cal);

int oplus_chg_get_battery_btb_temp_cal(void)
{
	if (!oplus_ntc_ctrl_is_support()) {
		return oplus_chg_get_battery_btb_temp() / OPLUS_BTB_HIGHPRICISION;
	}
	return battery_btb_temp_cal;
}
EXPORT_SYMBOL(oplus_chg_get_battery_btb_temp_cal);

#define OFFSET_IC 1
void set_charger_ic(int sel)
{
	charger_ic__det_flag |= OFFSET_IC<<sel;
}
EXPORT_SYMBOL(set_charger_ic);

static int oplus_ntcctrl_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ntcctrl_high = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ntcctrl_high");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ntcctrl_high)) {
		chg_err("get ntcctrl_high fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ntcctrl_low = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ntcctrl_low");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ntcctrl_low)) {
		chg_err("get ntcctrl_low fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ntcctrl_low);

	return 0;
}

static int oplus_ntcctrl_gpio_init_cal(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ntcctrl_high = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ntcctrl_high");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ntcctrl_high)) {
		chg_err("get ntcctrl_high fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ntcctrl_low = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ntcctrl_low");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ntcctrl_low)) {
		chg_err("get ntcctrl_low fail\n");
		return -EINVAL;
	}
	return 0;
}

static int oplus_ntc_switch_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ntcctrl_gpio = of_get_named_gpio(node, "qcom,ntcctrl-gpio", 0);
	if (chip->normalchg_gpio.ntcctrl_gpio <= 0) {
		chg_err("Couldn't read qcom,ntcctrl_gpio rc=%d, qcom,ntcctrl_gpio:%d\n",
				rc, chip->normalchg_gpio.ntcctrl_gpio);
	} else {
		if (oplus_ntc_ctrl_is_support()) {
			rc = gpio_request(chip->normalchg_gpio.ntcctrl_gpio, "ntcctrl-gpio");
			if (rc) {
				chg_err("unable to request ntcctrl-gpio:%d\n",
						chip->normalchg_gpio.ntcctrl_gpio);
			} else {
				rc = oplus_ntcctrl_gpio_init(chip);
				if (rc)
					chg_err("unable to init ntcctrl-gpio:%d\n",
							chip->normalchg_gpio.ntcctrl_gpio);
			}
		}
		chg_err("ntcctrl-gpio:%d\n", chip->normalchg_gpio.ntcctrl_gpio);
	}

	return rc;
}

int oplus_chg_ntc_init(struct oplus_chg_chip *chip)
{
	if (oplus_ntc_switch_parse_dt(chip)) {
		pr_err("%s ntc gpio init failed\n", __func__);
		return -1;
	}
	pr_info("%s ntc channel init success, %d\n", __func__);

	return 0;
}

void oplus_adc_switch_update_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return;
	}
	oplus_chg_adc_switch_ctrl();
	if (!oplus_voocphy_get_fastchg_start()) {
		schedule_delayed_work(&adc_switch_update_work, msecs_to_jiffies(5000));
	} else {
		schedule_delayed_work(&adc_switch_update_work, msecs_to_jiffies(800));
	}
}

void oplus_fg_update_work(struct work_struct *work)
{
	int ui_soc, real_soc;

	ui_soc = oplus_chg_get_ui_soc();
	real_soc = oplus_chg_get_soc();
	if (abs(ui_soc - real_soc) > URGENT_CHECK_THRESHOLD ||
	    sleep_time > NORMAL_CHECK_UPDATE_INTERVAL)
		oplus_chg_soc_update_when_resume(sleep_time);
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

static int mtk_charger_probe(struct platform_device *pdev)
{
	struct charger_manager *info = NULL;
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;
	int ret;
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_chg_chip *oplus_chip;
#endif

	chr_err("%s: starts support \n", __func__);
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(*oplus_chip), GFP_KERNEL);
	if (!oplus_chip)
		return -ENOMEM;

	oplus_chip->dev = &pdev->dev;
	oplus_chg_parse_svooc_dt(oplus_chip);
	if (oplus_chip->vbatt_num == 1) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oplus_chip->chg_ops = &mtk6360_chg_ops;
	} else {
		if (oplus_gauge_check_chip_is_null() || oplus_vooc_check_chip_is_null()
				|| oplus_adapter_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge || vooc || adapter null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oplus_chip->chg_ops = (oplus_get_chg_ops());
		is_svooc65_cfg = true;
		is_mtksvooc_project = true;
		chg_err("%s is_svooc65_cfg = %d\n", __func__, is_svooc65_cfg);
	}

	if (is_vooc_support_single_batt_svooc() == true) {
		is_mtksvooc_project = true;
		chg_err("%s is_vooc_support_single_batt_svooc\n", __func__);
	}

	if (oplus_chip->vooc_project == 1 && is_mtksvooc_project == false) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		is_vooc30_cfg = true;
		is_mtkvooc30_project = true;
		chg_err("%s is_vooc30_cfg = %d\n", __func__, is_vooc30_cfg);
	}

#endif /* OPLUS_FEATURE_CHG_BASIC */

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	pinfo = info;

	platform_set_drvdata(pdev, info);
	info->pdev = pdev;
	mtk_charger_parse_dt(info, &pdev->dev);

	if (!oplus_chip->vooc_project) {
		oplus_step_charging_parse_dt(info, &pdev->dev);
	}
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chip->chgic_mtk.oplus_info = info;

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev) {
		chg_err("found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	} else {
		chg_err("can't find primary charger!\n");
	}
#endif
	mutex_init(&info->charger_lock);
	mutex_init(&info->charger_pd_lock);

#ifdef OPLUS_FEATURE_CHG_BASIC
	g_oplus_chip = oplus_chip;
	oplus_power_supply_init(oplus_chip);
	oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	oplus_chg_ntc_init(oplus_chip);
	if (oplus_ntc_ctrl_is_support()) {
		INIT_DELAYED_WORK(&adc_switch_update_work, oplus_adc_switch_update_work);
		schedule_delayed_work(&adc_switch_update_work, 0);
	}
	g_oplus_chip->charger_current_pre = -1;
	oplus_chip->authenticate = oplus_gauge_get_batt_authenticate();
	oplus_chip->chg_ops->hardware_init();
	oplus_chg_init(oplus_chip);
	oplus_chg_wake_update_work();
	if (get_boot_mode() != KERNEL_POWER_OFF_CHARGING_BOOT) {
		oplus_tbatt_power_off_task_init(oplus_chip);
	}
#endif

	if (oplus_chg_get_voocphy_support()) {
		is_mtksvooc_project = true;
		chg_err("%s oplus_chg_get_voocphy_support\n", __func__);
	}


#ifdef OPLUS_FEATURE_CHG_BASIC
	pinfo->chargeric_temp_chan = iio_channel_get(oplus_chip->dev, "auxadc3-chargeric_temp");
        if (IS_ERR(pinfo->chargeric_temp_chan)) {
                chg_err("Couldn't get chargeric_temp_chan...\n");
                pinfo->chargeric_temp_chan = NULL;
        }
	pinfo->subboard_temp_chan = iio_channel_get(oplus_chip->dev, "auxadc6-subboard_temp");
        if (IS_ERR(pinfo->subboard_temp_chan)) {
                chg_err("Couldn't get subboard_temp_chan...\n");
                pinfo->subboard_temp_chan = NULL;
        }
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	pinfo->batcon_temp_chan = iio_channel_get(oplus_chip->dev, "auxadc10-batcon_temp");
	if (IS_ERR(pinfo->batcon_temp_chan)) {
		chg_err("Couldn't get batcon_temp_chan...\n");
		pinfo->batcon_temp_chan = NULL;
	}
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	pinfo->charger_id_chan = iio_channel_get(oplus_chip->dev, "auxadc3-charger_id");
        if (IS_ERR(pinfo->charger_id_chan)) {
                chg_err("Couldn't get charger_id_chan...\n");
                pinfo->charger_id_chan = NULL;
        }

	pinfo->usb_temp_v_l_chan = iio_channel_get(oplus_chip->dev, "auxadc4-usb_temp_v_l");
	if (IS_ERR(pinfo->usb_temp_v_l_chan)) {
		chg_err("Couldn't get usb_temp_v_l_chan...\n");
		pinfo->usb_temp_v_l_chan = NULL;
	}

	pinfo->usb_temp_v_r_chan = iio_channel_get(oplus_chip->dev, "auxadc5-usb_temp_v_r");
	if (IS_ERR(pinfo->usb_temp_v_r_chan)) {
		chg_err("Couldn't get usb_temp_v_r_chan...\n");
		pinfo->usb_temp_v_r_chan = NULL;
	}
	pinfo->data.vbus_exist = false;
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (is_svooc65_project() == true)
		oplus_mt6360_suspend_charger();
#endif
	oplus_chip->con_volt = con_volt_19165;
	oplus_chip->con_temp = con_temp_19165;
	oplus_chip->len_array = ARRAY_SIZE(con_temp_19165);
#ifdef OPLUS_FEATURE_CHG_BASIC
	if (oplus_usbtemp_check_is_support() == true) {
		oplus_usbtemp_thread_init();
		is_usbtemp_thread_init_done = true;
	}
	oplus_chg_configfs_init(oplus_chip);
#endif
	INIT_DELAYED_WORK(&usbtemp_recover_work, oplus_usbtemp_recover_work);

	srcu_init_notifier_head(&info->evt_nh);
	ret = mtk_charger_setup_files(pdev);
	if (ret)
		chr_err("Error creating sysfs interface\n");

	info->pd_adapter = get_adapter_by_name("pd_adapter");
	if (info->pd_adapter)
		chr_err("Found PD adapter [%s]\n",
			info->pd_adapter->props.alias_name);
	else
		chr_err("*** Error : can't find PD adapter ***\n");

#ifdef OPLUS_FEATURE_CHG_BASIC
	pinfo->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!pinfo->tcpc) {
		chr_err("%s get tcpc device type_c_port0 fail\n", __func__);
	}
#endif

	mtk_pdc_init(info);

	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		ptr->cm = info;
		if (ptr->pnb != NULL) {
			srcu_notifier_chain_register(&info->evt_nh, ptr->pnb);
			ptr->pnb = NULL;
		}
	}
	mutex_unlock(&consumer_mutex);
#ifdef OPLUS_FEATURE_CHG_BASIC
	if (oplus_chip->dual_charger_support) {
		INIT_DELAYED_WORK(&pinfo->step_charging_work, mt6360_step_charging_work);
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	INIT_DELAYED_WORK(&fg_update_work, oplus_fg_update_work);
#endif
#endif
	return 0;
}

static int mtk_charger_remove(struct platform_device *dev)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_configfs_exit();
#endif

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	oplus_test_kit_unregister();
#endif

	return 0;
}

static void mtk_charger_shutdown(struct platform_device *dev)
{
	struct charger_manager *info = platform_get_drvdata(dev);
	int ret;

	if (mtk_pe20_get_is_connect(info) || mtk_pe_get_is_connect(info)) {
		if (info->chg2_dev)
			charger_dev_enable(info->chg2_dev, false);
		ret = mtk_pe20_reset_ta_vchr(info);
		if (ret == -ENOTSUPP)
			mtk_pe_reset_ta_vchr(info);
		pr_debug("%s: reset TA before shutdown\n", __func__);
	}
#ifdef OPLUS_FEATURE_CHG_BASIC
	if (mtkhv_flashled_pinctrl.hv_flashled_support){
		mtkhv_flashled_pinctrl.bc1_2_done = false;
		mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
		mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
	}
	if (g_oplus_chip) {
		oplus_vooc_reset_mcu();
		oplus_chg_set_chargerid_switch_val(0);
		oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
		msleep(DELAY_FOR_SWITCH_EFFECT_30MS);
	}

	if (g_oplus_chip) {
		enter_ship_mode_function(g_oplus_chip);
	}
#endif	
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc = NULL;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		chg_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		chg_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		chg_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);

	return rc;
}

static int mtk_charger_pm_resume(struct device *dev)
{
	unsigned long resume_tm_sec = 0;
	int ui_soc, real_soc;
	int rc = 0;

	if (!g_oplus_chip)
		return 0;

	if (g_oplus_chip->chg_ops != &mtk6360_chg_ops)
		return 0;

	rc = get_current_time(&resume_tm_sec);
	if (rc || suspend_tm_sec == -1) {
		chg_err("RTC read failed\n");
		sleep_time = 0;
	} else {
		sleep_time = resume_tm_sec - suspend_tm_sec;
	}
	schedule_delayed_work(&fg_update_work, 0);

	return 0;
}

static int mtk_charger_pm_suspend(struct device *dev)
{
	if (!g_oplus_chip) {
		return 0;
	}
	if (g_oplus_chip->chg_ops != &mtk6360_chg_ops) {
		return 0;
	}
	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	cancel_delayed_work_sync(&fg_update_work);

	return 0;
}

static const struct dev_pm_ops mtk_charger_pm_ops = {
	.resume		= mtk_charger_pm_resume,
	.suspend	= mtk_charger_pm_suspend,
};
#endif
#endif

static const struct of_device_id mtk_charger_of_match[] = {
	{.compatible = "mediatek,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_charger_of_match);

struct platform_device charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver charger_driver = {
	.probe = mtk_charger_probe,
	.remove = mtk_charger_remove,
	.shutdown = mtk_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = mtk_charger_of_match,
#ifdef OPLUS_FEATURE_CHG_BASIC
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		   .pm = &mtk_charger_pm_ops,
#endif
#endif
	},
};

static int __init mtk_charger_init(void)
{
	return platform_driver_register(&charger_driver);
}
late_initcall(mtk_charger_init);

static void __exit mtk_charger_exit(void)
{
	platform_driver_unregister(&charger_driver);
}
module_exit(mtk_charger_exit);


MODULE_AUTHOR("LiYue");
MODULE_DESCRIPTION("OPLUS Charger Driver");
MODULE_LICENSE("GPL");
