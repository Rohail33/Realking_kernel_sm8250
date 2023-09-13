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
#include "oplus_bq25910.h"

#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/iio/consumer.h>
#endif
#include "oplus_da9313.h"

static bool em_mode = false;
static bool is_svooc65_project(void);
static bool is_vooc30_project(void);
static bool is_usbtemp_thread_init_done = false;
struct oplus_chg_chip *g_oplus_chip = NULL;
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
#ifdef OPLUS_FEATURE_CHG_BASIC
static void get_chargeric_temp(struct charger_data *pdata);
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
static int charge_pump_mode = OPLUS_DIVIDER_WORK_MODE_AUTO;
extern int oplus_set_divider_work_mode(int work_mode);
#endif
bool oplus_check_pdphy_ready(void);
bool oplus_check_pd_state_ready(void);

#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
bool oplus_usb_or_otg_is_present(void);
bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_disable(void);
void oplus_ccdetect_enable(void);
int oplus_ccdetect_get_power_role(void);
bool oplus_get_otg_switch_status(void);
bool oplus_ccdetect_support_check(void);
int oplus_chg_ccdetect_parse_dt(struct oplus_chg_chip *chip);
//====================================================================//
#endif /* OPLUS_FEATURE_CHG_BASIC */


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
#define USB_TEMP_HIGH		0x01//bit0
#define USB_WATER_DETECT	0x02//bit1
#define USB_RESERVE2		0x04//bit2
#define USB_RESERVE3		0x08//bit3
#define USB_RESERVE4		0x10//bit4
#define USB_DONOT_USE		0x80000000 /* bit31 */
static int usb_status = 0;

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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#ifndef OPLUS_FEATURE_CHG_BASIC
/* 2020/02/12, remove to mtk_pe40_intf.c, resolve compile error */
bool mtk_is_TA_support_pd_pps(struct charger_manager *pinfo)
{
	if (pinfo->enable_pe_4 == false && pinfo->enable_pe_5 == false)
		return false;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return true;
	return false;
}
#endif
#else
#ifdef OPLUS_FEATURE_CHG_BASIC
/* 2020/02/12, remove to mtk_pe40_intf.c, resolve compile error */
bool mtk_is_TA_support_pd_pps(struct charger_manager *pinfo)
{
	if (pinfo->enable_pe_4 == false && pinfo->enable_pe_5 == false)
		return false;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return true;
	return false;
}
#endif
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	if (!info->charger_wakelock.active)
		__pm_stay_awake(&info->charger_wakelock);
#else
	if (!info->charger_wakelock->active)
		__pm_stay_awake(info->charger_wakelock);
#endif
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

		get_chargeric_temp(pdata);

		consumer->support_ntc_01c_precision = info->support_ntc_01c_precision;
		chr_debug("%s: support_ntc_01c_precision: %d\n", __func__, info->support_ntc_01c_precision);

		pdata->junction_temp_min = pdata->chargeric_temp;
		pdata->junction_temp_max = pdata->chargeric_temp;
		*tchg_min = pdata->chargeric_temp;
		*tchg_max = pdata->chargeric_temp;

		*tchg_min = pdata->junction_temp_min;
		*tchg_max = pdata->junction_temp_max;

		return 0;
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
			chr_err("single_batt Charger Plug Out\n");
		}
		charger_dev_set_input_current(g_oplus_chip->chgic_mtk.oplus_info->chg1_dev, 500000);

	} else {
		if (mt_get_charger_type() != CHARGER_UNKNOWN){
			oplus_wake_up_usbtemp_thread();
			oplus_set_divider_work_mode(OPLUS_DIVIDER_WORK_MODE_FIXED);
			chr_err("charge_pump_mode = %d\n", charge_pump_mode);

			if (mtkhv_flashled_pinctrl.hv_flashled_support){
				mtkhv_flashled_pinctrl.bc1_2_done = true;
				if (g_oplus_chip->camera_on) {
					mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
					mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					chr_err("[%s] camera_on %d\n", __func__, g_oplus_chip->camera_on);
				}
			}
			chr_err("Charger Plug In\n");
		} else {
			oplus_set_divider_work_mode(charge_pump_mode);
			chr_err("charge_pump_mode = %d\n", charge_pump_mode);
			if (mtkhv_flashled_pinctrl.hv_flashled_support){
				mtkhv_flashled_pinctrl.bc1_2_done = false;
				mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
				pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
				mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
			}
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

	chg_err("pd_not_support:%d, qc_not_support:%d\n",info->data.pd_not_support,info->data.qc_not_support);

	info->data.power_path_support =
				of_property_read_bool(np, "power_path_support");
	chr_debug("%s: power_path_support: %d\n",
		__func__, info->data.power_path_support);

	info->support_ntc_01c_precision = of_property_read_bool(np, "qcom,support_ntc_01c_precision");
	chr_debug("%s: support_ntc_01c_precision: %d\n", __func__, info->support_ntc_01c_precision);

	info->usbtemp_lowvbus_detect = of_property_read_bool(np, "qcom,usbtemp_lowvbus_detect");
	chr_debug("%s: usbtemp_lowvbus_detect: %d\n", __func__, info->usbtemp_lowvbus_detect);

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

	chg_err("step1_time: %d, step1_current: %d, step2_time: %d, step2_current: %d, step3_current: %d\n",
		info->data.step1_time, info->data.step1_current_ma, info->data.step2_time, info->data.step2_current_ma, info->data.step3_current_ma);

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

	if (g_oplus_chip->vooc_project == 0) {
		chr_err("match svid but not vooc project return\n");
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
	struct oplus_chg_chip *chip = g_oplus_chip;
	if (!chip) {
		return false;
	}

	if (g_oplus_chip->pd_svooc == true) {
		return oplus_check_pd_state_ready();
	} else {
		return true;
	}
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
			chr_err("MTK_PD_CONNECT_PE_READY_SNK_PD30 in_good_connect true\n");
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
			chr_err("MTK_PD_CONNECT_PE_READY_SNK_PD30 in_good_connect true\n");
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		mt_usb_disconnect();
#else
		mt_usb_disconnect_v1();
#endif
	} else {
		if (musb_hdrc_release == true &&
				g_oplus_chip->unwakelock_chg == 0 &&
				mt_get_charger_type() == NONSTANDARD_CHARGER) {
			musb_hdrc_release = false;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
			mt_usb_connect();
#else
			mt_usb_connect_v1();
#endif
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
		bq25910_dump_registers();
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
		bq25910_hardware_init();
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

		if (bq25910_is_detected() == false){
			g_oplus_chip->slave_pct = 15;
		}

		if (g_oplus_chip->tbatt_status == BATTERY_STATUS__NORMAL) {
			if (chg_curr > pinfo->step_chg_current)
				chg_curr = pinfo->step_chg_current;
		}

		if (g_oplus_chip->dual_charger_support &&
				(g_oplus_chip->slave_charger_enable || em_mode)) {
			main_cur = (chg_curr * (100 - g_oplus_chip->slave_pct))/100;
			main_cur -= main_cur % 100;
			slave_cur = chg_curr - main_cur;

			rc = charger_dev_set_charging_current(chg, main_cur * 1000);
			if (rc < 0) {
				chg_debug("set fast charge current:%d fail\n", main_cur);
			}

			rc = bq25910_charging_current_write_fast(slave_cur);
			if (rc < 0) {
				chg_debug("set sub fast charge current:%d fail\n", slave_cur);
			}
		} else {
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

static void oplus_mt6360_set_aicl_point(int vbatt)
{
	int rc = 0;
	static int hw_aicl_point = 4400;
	struct charger_device *chg = NULL;

	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (!g_oplus_chip->authenticate) {
		printk(KERN_ERR "[OPLUS_CHG][%s]:!g_oplus_chip->authenticate \n", __func__);
		return;
	}

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

	if (hw_aicl_point == 4400 && vbatt > 4100) {
		hw_aicl_point = 4500;
		//sw_aicl_point = 4550;
	} else if (hw_aicl_point == 4500 && vbatt <= 4100) {
		hw_aicl_point = 4400;
		//sw_aicl_point = 4500;
	}
	rc = charger_dev_set_mivr(chg, hw_aicl_point * 1000);
	if (rc < 0) {
		chg_debug("set aicl point:%d fail\n", hw_aicl_point);
	}
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

		if (bq25910_is_detected() == false){
			g_oplus_chip->slave_pct = 15;
		}

		if (g_oplus_chip->dual_charger_support && g_oplus_chip->slave_charger_enable == false) {
			rc = bq25910_disable_charging();
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}

			rc = bq25910_suspend_charger();
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
		if (g_oplus_chip->batt_volt > 4100 )
			aicl_point = 4550;
		else
			aicl_point = 4500;
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
			chg_debug("enable mt6360 and bq25910 for charging\n");
			rc = bq25910_enable_charging();
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}

			rc = bq25910_unsuspend_charger();
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}

			slave_cur = (usb_icl[i] * g_oplus_chip->slave_pct)/100;
			slave_cur -= slave_cur % 100;
			main_cur = usb_icl[i] - slave_cur;

			chg_debug("usb input max current limit aicl: master and salve input current: %d, %d\n",
					main_cur,slave_cur);
			bq25910_input_current_limit_write(slave_cur);
			rc = charger_dev_set_input_current(chg, main_cur * 1000 );
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
			rc = bq25910_enable_charging();
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}

			rc = bq25910_unsuspend_charger();
			if (rc < 0) {
				chg_debug("disable sub charging fail\n");
			}

			slave_cur = (usb_icl[i] * g_oplus_chip->slave_pct)/100;
			slave_cur -= slave_cur % 100;
			main_cur = usb_icl[i] - slave_cur;

			chg_debug("usb input max current limit aicl: master and salve input current: %d, %d\n",
					main_cur,slave_cur);
			bq25910_input_current_limit_write(slave_cur);
			rc = charger_dev_set_input_current(chg, main_cur * 1000 );
		} else {
			rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
		}
	} else {
		rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	}

	goto aicl_rerun;
aicl_rerun:
	mt6360_aicl_enable(true);
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

		rc = bq25910_float_voltage_write(vfloat_mv + DELTA_MV);
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

	chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

 	if (pinfo->data.dual_charger_support) {
		rc = charger_dev_enable(chg, false);
		if (rc < 0) {
			chg_debug("disable charging fail\n");
		}

		rc = bq25910_disable_charging();
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

	if (!is_mtkvooc30_project && !is_mtksvooc_project) {
		chg = g_oplus_chip->chgic_mtk.oplus_info->chg1_dev;

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

static int oplus_mt6360_get_chg_current_step(void)
{
	return 100;
}

int mt_power_supply_type_check(void)
{
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

	switch(mt_get_charger_type()) {
	case CHARGER_UNKNOWN:
		break;
	case STANDARD_HOST:
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
			chg_debug("charger_type[%d] [%d] power_supply_changed.\n", charger_type, g_oplus_chip->charger_type);
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

	if (!g_oplus_chip || !pinfo || !pinfo->tcpc) {
		chg_err("g_oplus_chip not ready!\n");
		return false;
	}

	if(g_oplus_chip->vbatt_num == 2
		&& tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC) {
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

static int oplus_usb_switch_gpio_gpio_init(void)
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
static bool oplus_chg_get_shortc_hw_gpio_status(void)
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
		if (pinfo->data.dual_charger_support) {
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
	u32 pull_up_r = 0;
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
		if (of_property_read_bool(g_oplus_chip->dev->of_node, "qcom,sub_board_pull_up_r_support")) {
			if (of_property_read_u32(g_oplus_chip->dev->of_node, "qcom,sub_board_pull_up_r", &pull_up_r) >= 0) {
				ntc_param.i_rap_pull_up_r = pull_up_r;
			}
		}

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
		ntc_param.i_tap_over_critical_low = 4397119;
		ntc_param.i_rap_pull_up_r = 100000;
		ntc_param.i_rap_pull_up_voltage = 1800;
		ntc_param.i_tap_min = -400;
		ntc_param.i_tap_max = 1250;
		ntc_param.i_25c_volt = 2457;
		ntc_param.pst_temp_table = chargeric_temp_table;
		ntc_param.i_table_size = (sizeof(chargeric_temp_table) / sizeof(struct temp_param));
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
	/* chg_err("chargeric_temp:%d\n", chargeric_temp); */
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

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->chgic_mtk.oplus_info->ccdetect_gpio)) {
		/*chg_debug("[ccdetecct]has gpio ");*/
		return true;
	}

	return false;
}

bool oplus_ccdetect_support_check(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(chip) == true)
		return true;

	chg_err("not support, return false\n");

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
void oplus_ccdetect_work(struct work_struct *work)
{
	int level;
	struct oplus_chg_chip *chip = g_oplus_chip;
	level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	chg_debug("%s: level [%d]", __func__, level);
	if (is_meta_mode()) {
		chg_err("is_meta_mode int \r\n");
		return;
	}
	if (level != 1) {
		oplus_ccdetect_enable();
        oplus_wake_up_usbtemp_thread();
	} else {
		if (g_oplus_chip)
			g_oplus_chip->usbtemp_check = oplus_usb_or_otg_is_present();
		if (oplus_get_otg_switch_status() == false)
			oplus_ccdetect_disable();
		if(g_oplus_chip->usb_status == USB_TEMP_HIGH) {
			schedule_delayed_work(&usbtemp_recover_work, 0);
		}
	}
}
void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}
	chip->chgic_mtk.oplus_info->ccdetect_irq = gpio_to_irq(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	chg_debug("[OPLUS_CHG][%s]: chip->chgic_mtk.oplus_info->ccdetect_gpio[%d]!\n", __func__, chip->chgic_mtk.oplus_info->ccdetect_gpio);
}

#define CCDETECT_DELAY_MS	50
#define USLEEP_DELAY_2000	2000
#define USLEEP_DELAY_2100	2100
#define MSLEEP_DELAY_10	10000
#define MSLEEP_DELAY_11	11000
#define SCHEDULE_DELAY_MS	6000
struct delayed_work ccdetect_work;
irqreturn_t oplus_ccdetect_change_handler(int irq, void *data)
{

	cancel_delayed_work_sync(&ccdetect_work);
	chg_debug("[OPLUS_CHG][%s]: Scheduling ccdetect work!\n", __func__);
	schedule_delayed_work(&ccdetect_work,
			msecs_to_jiffies(CCDETECT_DELAY_MS));
	return IRQ_HANDLED;
}

/**************************************************************
 * bit[0]=0: NO standard typec device/cable connected(ccdetect gpio in high level)
 * bit[0]=1: standard typec device/cable connected(ccdetect gpio in low level)
 * bit[1]=0: NO OTG typec device/cable connected
 * bit[1]=1: OTG typec device/cable connected
 **************************************************************/
#define DISCONNECT						0
#define STANDARD_TYPEC_DEV_CONNECT	BIT(0)
#define OTG_DEV_CONNECT				BIT(1)

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
	int online = 0;
	int level = 0;
	int typec_otg = 0;
	static int pre_level = 1;
	static int pre_typec_otg = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip || !pinfo || !pinfo->tcpc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: g_oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(chip) == true) {
		level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
		if (level != gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio)) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(5000, 5100);
			level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
		}
	} else {
		return oplus_get_otg_online_status_default();
	}
	online = (level == 1) ? DISCONNECT : STANDARD_TYPEC_DEV_CONNECT;

	if (tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC) {
		typec_otg = 1;
	} else {
		typec_otg = 0;
	}
	online = online | ((typec_otg == 1) ? OTG_DEV_CONNECT : DISCONNECT);

	if ((pre_level ^ level) || (pre_typec_otg ^ typec_otg)) {
		pre_level = level;
		pre_typec_otg = typec_otg;
		printk(KERN_ERR "[OPLUS_CHG][%s]: gpio[%s], c-otg[%d], otg_online[%d]\n",
				__func__, level ? "H" : "L", typec_otg, online);
	}

	chip->otg_online = typec_otg;
	return online;
}

bool oplus_get_otg_switch_status(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	return chip->otg_switch;
}

void oplus_ccdetect_irq_register(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (!chip) {
		chg_err("[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	ret = devm_request_threaded_irq(chip->dev,  chip->chgic_mtk.oplus_info->ccdetect_irq,
			NULL, oplus_ccdetect_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "ccdetect-change", chip);
	if (ret < 0) {
		chg_err("Unable to request ccdetect-change irq: %d\n", ret);
	}
	chg_debug("%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(chip->chgic_mtk.oplus_info->ccdetect_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: ccdetect_irq failed %d\n", ret);
	}
}

void oplus_ccdetect_enable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	/* set DRP mode */
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		tcpm_typec_change_role(pinfo->tcpc, TYPEC_ROLE_DRP);
		chg_debug("%s: set drp", __func__);
	}
}

void oplus_ccdetect_disable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	/* set SINK mode */
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		tcpm_typec_change_role(pinfo->tcpc, TYPEC_ROLE_SNK);
		chg_debug("%s: set sink", __func__);
	}
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
				if (rc) {
					chg_err("unable to init ccdetect_gpio:%d\n",
							chip->chgic_mtk.oplus_info->ccdetect_gpio);
				} else {
							oplus_ccdetect_irq_init(chip);
						}
			}
		}
		chg_err("ccdetect-gpio:%d\n", chip->chgic_mtk.oplus_info->ccdetect_gpio);
	}

	return rc;
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

int oplus_get_usbtemp_volt_l(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip == NULL) {
		return 0;
	}

	return chip->usbtemp_volt_l;
}

int oplus_get_usbtemp_volt_r(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip == NULL) {
		return 0;
	}

	return chip->usbtemp_volt_r;
}

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
		chg_err("[OPLUS_CHG][%s]: oplus_chg_ccdetect_parse_dt fail!\n", __func__);
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
		tcpm_typec_change_role(pinfo->tcpc, value ? TYPEC_ROLE_DRP : TYPEC_ROLE_SNK);
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
			(chip->calling_on || chip->soc >= 90 || chip->camera_on || chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr
			|| chip->temperature > 530 || chip->cool_down_force_5v == true)) {
			vbus_mv = VBUS_5V;
			ibus_ma = IBUS_3A;

			printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
			ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
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

	if(pinfo->data.pd_not_support) {
		vbus_mv = VBUS_5V;
		ibus_ma = IBUS_2A;
		ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		return ret;
	}

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

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
		pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
		pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return CHARGER_SUBTYPE_PD;
		
	if (mt6360_get_hvdcp_type() == POWER_SUPPLY_TYPE_USB_HVDCP)
		return CHARGER_SUBTYPE_QC;

	return CHARGER_SUBTYPE_DEFAULT;
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
		mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x18);
		oplus_notify_hvdcp_detect_stat();
		ret = 0;
	} else {
		if (chip->charger_volt > 7500 &&
			(chip->calling_on || chip->soc >= 90 || chip->camera_on
			|| chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr || chip->temperature > 530 || chip->cool_down_force_5v == true)) {
			printk(KERN_ERR "%s: set qc to 5V", __func__);
			mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x15);
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
  		pr_err("not support QC\n");
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
		if (g_oplus_chip->dual_charger_support || (is_vooc_support_single_batt_svooc() == true)) {
			if (g_oplus_chip->camera_on == 1 && g_oplus_chip->charger_exist) {
				if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC){
					oplus_chg_set_qc_config();
				} else if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD){
					oplus_mt6360_pd_setup();
				}
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

void oplus_set_typec_sinkonly()
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: usbtemp occur otg switch[0]\n", __func__);
		tcpm_typec_change_role(pinfo->tcpc, TYPEC_ROLE_SNK);
	}
};
EXPORT_SYMBOL(oplus_set_typec_sinkonly);

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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
#else
	.usb_connect = mt_usb_connect_v1,
	.usb_disconnect = mt_usb_disconnect_v1,
#endif
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
	.oplus_usbtemp_monitor_condition = oplus_usb_or_otg_is_present,
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

extern u8 soc_store[4];
extern u8 night_count;
static ssize_t soc_ajust_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->soc_ajust);
}

static ssize_t soc_ajust_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val == 0) {
		chip->soc_ajust = 0;
		night_count=0;
		chip->modify_soc=0;
	} else {
		chip->soc_ajust = 1;
		soc_store[0]=chip->soc;
		chg_err("[soc_ajust_feature]:set soc_ajust_switch,soc_store0 = [%d].\n", soc_store[0]);
		set_soc_feature();
	}
	chg_err("[soc_ajust_feature]:set soc_ajust_switch = [%d] soc = [%d].\n", val, chip->soc);

	return count;
}
static DEVICE_ATTR_RW(soc_ajust);

static struct device_attribute *oplus_mtk6853_cust_battery_attributes[] = {
	&dev_attr_soc_ajust,
	NULL
};

static int mtk_charger_probe(struct platform_device *pdev)
{
	struct charger_manager *info = NULL;
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;
	int ret;
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_chg_chip *oplus_chip;
	int level = 0;
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
	oplus_chip->authenticate = oplus_gauge_get_batt_authenticate();
	oplus_chip->chg_ops->hardware_init();
	oplus_chg_init(oplus_chip);
	oplus_chg_wake_update_work();
	if (get_boot_mode() != KERNEL_POWER_OFF_CHARGING_BOOT) {
		oplus_tbatt_power_off_task_init(oplus_chip);
	}
#endif

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

	if (oplus_chip->nightstandby_support) {
		oplus_battery_node_add(oplus_mtk6853_cust_battery_attributes);
	}
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

	if (oplus_ccdetect_support_check() == true) {
		INIT_DELAYED_WORK(&ccdetect_work, oplus_ccdetect_work);
		oplus_ccdetect_irq_register(oplus_chip);
		level = gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio);
		usleep_range(USLEEP_DELAY_2000, USLEEP_DELAY_2100);
		if (level != gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio)) {
			chr_err("[OPLUS_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(MSLEEP_DELAY_10, MSLEEP_DELAY_11);
			level = gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio);
		}
		if (level <= 0) {
			schedule_delayed_work(&ccdetect_work, msecs_to_jiffies(SCHEDULE_DELAY_MS));
		}
		chg_debug("[OPLUS_CHG][%s]: ccdetect_gpio ..level[%d]  \n", __func__, level);

		oplus_chip->con_volt = con_volt_20131;
		oplus_chip->con_temp = con_temp_20131;
		oplus_chip->len_array = ARRAY_SIZE(con_temp_20131);
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
#endif
	return 0;
}

static int mtk_charger_remove(struct platform_device *dev)
{
	if (!g_oplus_chip) {
		return -ENOMEM;
	}
	if (g_oplus_chip->nightstandby_support) {
		oplus_battery_node_delete(oplus_mtk6853_cust_battery_attributes);
	}
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_configfs_exit();
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
		enter_ship_mode_function(g_oplus_chip);
	}
#endif	
}

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


MODULE_DESCRIPTION("OPLUS Charger Driver");
MODULE_LICENSE("GPL");
