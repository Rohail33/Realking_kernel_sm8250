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

#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_battery.h>
#else
#include <mt-plat/v1/charger_type.h>
#include <mt-plat/v1/mtk_battery.h>
#endif

#include <mt-plat/mtk_boot.h>
#include <mt-plat/mtk_boot_common.h>

#include <pmic.h>
#include <mtk_gauge_time_service.h>
#include <soc/oplus/system/oplus_project.h>

#include "../voocphy/oplus_voocphy.h"
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

#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/iio/consumer.h>
#endif
#include "oplus_da9313.h"
#include "oplus_mp2650.h"
#include "../oplus_pps.h"


static bool em_mode = false;
static bool is_vooc_project(void);
struct oplus_chg_chip *g_oplus_chip = NULL;
extern struct oplus_chg_operations * oplus_get_chg_ops(void);
extern int oplus_usbtemp_monitor_common(void *data);
extern void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip);

extern bool is_mtksvooc_project;
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
#ifdef OPLUS_FEATURE_CHG_BASIC
static void mtk_chg_get_tchg(struct charger_manager *info);
static void oplus_get_chargeric_temp_volt(struct charger_data *pdata);
static void get_chargeric_temp(struct charger_data *pdata);
#endif

static struct task_struct *oplus_usbtemp_kthread;

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
bool oplus_usbtemp_condition(void);


//====================================================================//
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_disable(void);
void oplus_ccdetect_enable(void);
int oplus_ccdetect_get_power_role(void);
bool oplus_get_otg_switch_status(void);
bool oplus_ccdetect_support_check(void);
int oplus_chg_ccdetect_parse_dt(struct oplus_chg_chip *chip);
int oplus_get_otg_online_status(void);
bool oplus_get_otg_online_status_default(void);
//====================================================================//
#endif /* OPLUS_FEATURE_CHG_BASIC */


//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
#define USB_TEMP_HIGH		0x01 /* bit0 */
#define USB_WATER_DETECT	0x02 /* bit1 */
#define USB_RESERVE2		0x04 /* bit2 */
#define USB_RESERVE3		0x08 /* bit3 */
#define USB_RESERVE4		0x10 /* bit4 */
#define USB_DONOT_USE		0x80000000 /* bit31 */
static int usb_status = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
extern int acm_shortcut(void);
static bool oplus_is_meta_mode(void)
{
	int config = 0;
	config = acm_shortcut();
	if((config == 1) || (config == 2)) {
		return true;
	} else {
		return false;
	}
}
#endif

static void oplus_set_usb_status(int status)
{
	usb_status = usb_status | status;
}

static void oplus_clear_usb_status(int status)
{
	if( g_oplus_chip->usb_status == USB_TEMP_HIGH) {
		g_oplus_chip->usb_status = g_oplus_chip->usb_status & (~USB_TEMP_HIGH);
	}
	usb_status = usb_status & (~status);
}

int oplus_get_usb_status(void)
{
	if(g_oplus_chip->usb_status == USB_TEMP_HIGH) {
		return g_oplus_chip->usb_status;
	} else {
		return usb_status;
	}
}

int is_vooc_cfg = false;
static bool is_vooc_project(void)
{
	return is_vooc_cfg;
}

static bool usb_ntc_support = false;
static int oplus_usb_ntc_parse_dt(struct oplus_chg_chip *chip)
{
	struct device_node *node = NULL;

	if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	usb_ntc_support = of_property_read_bool(node, "qcom,usb_ntc_support");
	if (usb_ntc_support == false) {
		chg_err("not support usb ntc!\n");
	}

	return 0;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */
//====================================================================//
static struct charger_manager *pinfo;
static struct list_head consumer_head = LIST_HEAD_INIT(consumer_head);
static DEFINE_MUTEX(consumer_mutex);


bool mtk_is_TA_support_pd_pps(struct charger_manager *pinfo)
{
	if (pinfo->enable_pe_4 == false && pinfo->enable_pe_5 == false)
		return false;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return true;
	return false;
}


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
	struct list_head *pos = NULL;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr = NULL;

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
	struct charger_device *chg_dev = NULL;


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
		struct charger_data *pdata = NULL;

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
			else if (idx == MAIN_DIVIDER_CHARGER)
				pdata = &info->dvchg1_data;
			else if (idx == SLAVE_DIVIDER_CHARGER)
				pdata = &info->dvchg2_data;
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
		struct charger_data *pdata = NULL;
#ifdef OPLUS_FEATURE_CHG_BASIC
		mtk_chg_get_tchg(info);
#endif

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
		else if (idx == MAIN_DIVIDER_CHARGER)
			pdata = &info->dvchg1_data;
		else if (idx == SLAVE_DIVIDER_CHARGER)
			pdata = &info->dvchg2_data;
		else
			return -ENOTSUPP;

#ifdef OPLUS_FEATURE_CHG_BASIC
		if (!pdata) {
			pr_err("%s, pdata null\n", __func__);
			return -EINVAL;
		}

		if(idx == MAIN_CHARGER) {
			if (pinfo->chargeric_temp_chan) {
				oplus_get_chargeric_temp_volt(pdata);
				get_chargeric_temp(pdata);
				pdata->junction_temp_min = pdata->chargeric_temp;
				pdata->junction_temp_max = pdata->chargeric_temp;
				consumer->support_ntc_01c_precision = info->support_ntc_01c_precision;
			}
		}
#endif

		*tchg_min = pdata->junction_temp_min;
		*tchg_max = pdata->junction_temp_max;

		return 0;
	}
	return -EBUSY;
}

int charger_manager_force_charging_current(struct charger_consumer *consumer,
	int idx, int charging_current)
{
	struct charger_manager *info = consumer->cm;

	if (info != NULL) {
		struct charger_data *pdata = NULL;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		pdata->force_charging_current = charging_current;
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
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
	struct charger_device *pchg = NULL;


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

int charger_manager_enable_chg_type_det(struct charger_consumer *consumer,
	bool en)
{
	struct charger_manager *info = consumer->cm;
	struct charger_device *chg_dev = NULL;
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
	} else
		chr_err("%s: charger_manager is null\n", __func__);



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

/* user interface end*/

/* factory mode */
#define CHARGER_DEVNAME "charger_ftm"
#define GET_IS_SLAVE_CHARGER_EXIST _IOW('k', 13, int)

static struct class *charger_class;
static struct cdev *charger_cdev;
static int charger_major;
static dev_t charger_devno;

static int is_slave_charger_exist(void)
{
	if (get_charger_by_name("secondary_chg") == NULL)
		return 0;
	return 1;
}

static long charger_ftm_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;
	int out_data = 0;
	void __user *user_data = (void __user *)arg;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		out_data = is_slave_charger_exist();
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		chr_err("[%s] SLAVE_CHARGER_EXIST: %d\n", __func__, out_data);
		break;
	default:
		chr_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#ifdef CONFIG_COMPAT
static long charger_ftm_compat_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);
		break;
	default:
		chr_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#endif
static int charger_ftm_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int charger_ftm_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations charger_ftm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = charger_ftm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = charger_ftm_compat_ioctl,
#endif
	.open = charger_ftm_open,
	.release = charger_ftm_release,
};

void charger_ftm_init(void)
{
	struct class_device *class_dev = NULL;
	int ret = 0;

	ret = alloc_chrdev_region(&charger_devno, 0, 1, CHARGER_DEVNAME);
	if (ret < 0) {
		chr_err("[%s]Can't get major num for charger_ftm\n", __func__);
		return;
	}

	charger_cdev = cdev_alloc();
	if (!charger_cdev) {
		chr_err("[%s]cdev_alloc fail\n", __func__);
		goto unregister;
	}
	charger_cdev->owner = THIS_MODULE;
	charger_cdev->ops = &charger_ftm_fops;

	ret = cdev_add(charger_cdev, charger_devno, 1);
	if (ret < 0) {
		chr_err("[%s] cdev_add failed\n", __func__);
		goto free_cdev;
	}

	charger_major = MAJOR(charger_devno);
	charger_class = class_create(THIS_MODULE, CHARGER_DEVNAME);
	if (IS_ERR(charger_class)) {
		chr_err("[%s] class_create failed\n", __func__);
		goto free_cdev;
	}

	class_dev = (struct class_device *)device_create(charger_class,
				NULL, charger_devno, NULL, CHARGER_DEVNAME);
	if (IS_ERR(class_dev)) {
		chr_err("[%s] device_create failed\n", __func__);
		goto free_class;
	}

	pr_debug("%s done\n", __func__);
	return;

free_class:
	class_destroy(charger_class);
free_cdev:
	cdev_del(charger_cdev);
unregister:
	unregister_chrdev_region(charger_devno, 1);
}
/* factory mode end */

void mtk_charger_get_atm_mode(struct charger_manager *info)
{
	char atm_str[64] = {0};
	char *ptr = NULL, *ptr_e = NULL;
	char keyword[] = "androidboot.atm=";
	int size = 0;

	info->atm_enabled = false;

	ptr = strstr(saved_command_line, keyword);
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");
		if (ptr_e == NULL)
			goto end;

		size = ptr_e - (ptr + strlen(keyword));
		if (size <= 0)
			goto end;
		strncpy(atm_str, ptr + strlen(keyword), size);
		atm_str[size] = '\0';

		if (!strncmp(atm_str, "enable", strlen("enable")))
			info->atm_enabled = true;
	}
end:
	pr_info("%s: atm_enabled = %d\n", __func__, info->atm_enabled);
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

/* internal algorithm common function end */

/* sw jeita */
void sw_jeita_state_machine_init(struct charger_manager *info)
{
	struct sw_jeita_data *sw_jeita;

	if (info->enable_sw_jeita == true) {
		sw_jeita = &info->sw_jeita;
		info->battery_temp = battery_get_bat_temperature();

		if (info->battery_temp >= info->data.temp_t4_thres)
			sw_jeita->sm = TEMP_ABOVE_T4;
		else if (info->battery_temp > info->data.temp_t3_thres)
			sw_jeita->sm = TEMP_T3_TO_T4;
		else if (info->battery_temp >= info->data.temp_t2_thres)
			sw_jeita->sm = TEMP_T2_TO_T3;
		else if (info->battery_temp >= info->data.temp_t1_thres)
			sw_jeita->sm = TEMP_T1_TO_T2;
		else if (info->battery_temp >= info->data.temp_t0_thres)
			sw_jeita->sm = TEMP_T0_TO_T1;
		else
			sw_jeita->sm = TEMP_BELOW_T0;

		chr_err("[SW_JEITA] tmp:%d sm:%d\n",
			info->battery_temp, sw_jeita->sm);
	}
}

void do_sw_jeita_state_machine(struct charger_manager *info)
{
	struct sw_jeita_data *sw_jeita;

	sw_jeita = &info->sw_jeita;
	sw_jeita->pre_sm = sw_jeita->sm;
	sw_jeita->charging = true;

	/* JEITA battery temp Standard */
	if (info->battery_temp >= info->data.temp_t4_thres) {
		chr_err("[SW_JEITA] Battery Over high Temperature(%d) !!\n",
			info->data.temp_t4_thres);

		sw_jeita->sm = TEMP_ABOVE_T4;
		sw_jeita->charging = false;
	} else if (info->battery_temp > info->data.temp_t3_thres) {
		/* control 45 degree to normal behavior */
		if ((sw_jeita->sm == TEMP_ABOVE_T4)
		    && (info->battery_temp
			>= info->data.temp_t4_thres_minus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t4_thres_minus_x_degree,
				info->data.temp_t4_thres);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t3_thres,
				info->data.temp_t4_thres);

			sw_jeita->sm = TEMP_T3_TO_T4;
		}
	} else if (info->battery_temp >= info->data.temp_t2_thres) {
		if (((sw_jeita->sm == TEMP_T3_TO_T4)
		     && (info->battery_temp
			 >= info->data.temp_t3_thres_minus_x_degree))
		    || ((sw_jeita->sm == TEMP_T1_TO_T2)
			&& (info->battery_temp
			    <= info->data.temp_t2_thres_plus_x_degree))) {
			chr_err("[SW_JEITA] Battery Temperature not recovery to normal temperature charging mode yet!!\n");
		} else {
			chr_err("[SW_JEITA] Battery Normal Temperature between %d and %d !!\n",
				info->data.temp_t2_thres,
				info->data.temp_t3_thres);
			sw_jeita->sm = TEMP_T2_TO_T3;
		}
	} else if (info->battery_temp >= info->data.temp_t1_thres) {
		if ((sw_jeita->sm == TEMP_T0_TO_T1
		     || sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t1_thres_plus_x_degree)) {
			if (sw_jeita->sm == TEMP_T0_TO_T1) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
					info->data.temp_t1_thres_plus_x_degree,
					info->data.temp_t2_thres);
			}
			if (sw_jeita->sm == TEMP_BELOW_T0) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
					info->data.temp_t1_thres,
					info->data.temp_t1_thres_plus_x_degree);
				sw_jeita->charging = false;
			}
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t1_thres,
				info->data.temp_t2_thres);

			sw_jeita->sm = TEMP_T1_TO_T2;
		}
	} else if (info->battery_temp >= info->data.temp_t0_thres) {
		if ((sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t0_thres_plus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t0_thres,
				info->data.temp_t0_thres_plus_x_degree);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t0_thres,
				info->data.temp_t1_thres);

			sw_jeita->sm = TEMP_T0_TO_T1;
		}
	} else {
		chr_err("[SW_JEITA] Battery below low Temperature(%d) !!\n",
			info->data.temp_t0_thres);
		sw_jeita->sm = TEMP_BELOW_T0;
		sw_jeita->charging = false;
	}

	/* set CV after temperature changed */
	/* In normal range, we adjust CV dynamically */
	if (sw_jeita->sm != TEMP_T2_TO_T3) {
		if (sw_jeita->sm == TEMP_ABOVE_T4)
			sw_jeita->cv = info->data.jeita_temp_above_t4_cv;
		else if (sw_jeita->sm == TEMP_T3_TO_T4)
			sw_jeita->cv = info->data.jeita_temp_t3_to_t4_cv;
		else if (sw_jeita->sm == TEMP_T2_TO_T3)
			sw_jeita->cv = 0;
		else if (sw_jeita->sm == TEMP_T1_TO_T2)
			sw_jeita->cv = info->data.jeita_temp_t1_to_t2_cv;
		else if (sw_jeita->sm == TEMP_T0_TO_T1)
			sw_jeita->cv = info->data.jeita_temp_t0_to_t1_cv;
		else if (sw_jeita->sm == TEMP_BELOW_T0)
			sw_jeita->cv = info->data.jeita_temp_below_t0_cv;
		else
			sw_jeita->cv = info->data.battery_cv;
	} else {
		sw_jeita->cv = 0;
	}

	chr_err("[SW_JEITA]preState:%d newState:%d tmp:%d cv:%d\n",
		sw_jeita->pre_sm, sw_jeita->sm, info->battery_temp,
		sw_jeita->cv);
}

static ssize_t show_sw_jeita(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->enable_sw_jeita);
	return sprintf(buf, "%d\n", pinfo->enable_sw_jeita);
}

static ssize_t store_sw_jeita(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_sw_jeita = false;
		else {
			pinfo->enable_sw_jeita = true;
			sw_jeita_state_machine_init(pinfo);
		}

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR(sw_jeita, 0644, show_sw_jeita,
		   store_sw_jeita);
/* sw jeita end*/

/* pump express series */
bool mtk_is_pep_series_connect(struct charger_manager *info)
{
	if (mtk_pe20_get_is_connect(info) || mtk_pe_get_is_connect(info))
		return true;

	return false;
}

static ssize_t show_pe20(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->enable_pe_2);
	return sprintf(buf, "%d\n", pinfo->enable_pe_2);
}

static ssize_t store_pe20(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_pe_2 = false;
		else
			pinfo->enable_pe_2 = true;

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR(pe20, 0644, show_pe20, store_pe20);

static ssize_t show_pe40(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->enable_pe_4);
	return sprintf(buf, "%d\n", pinfo->enable_pe_4);
}

static ssize_t store_pe40(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_pe_4 = false;
		else
			pinfo->enable_pe_4 = true;

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR(pe40, 0644, show_pe40, store_pe40);

/* pump express series end*/

static ssize_t show_charger_log_level(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	chr_err("%s: %d\n", __func__, chargerlog_level);
	return sprintf(buf, "%d\n", chargerlog_level);
}

static ssize_t store_charger_log_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret = 0;

	chr_err("%s\n", __func__);

	if (buf != NULL && size != 0) {
		chr_err("%s: buf is %s\n", __func__, buf);
		ret = kstrtoul(buf, 10, &val);
		if (ret < 0) {
			chr_err("%s: kstrtoul fail, ret = %d\n", __func__, ret);
			return ret;
		}
		if (val < 0) {
			chr_err("%s: val is inavlid: %ld\n", __func__, val);
			val = 0;
		}
		chargerlog_level = val;
		chr_err("%s: log_level=%d\n", __func__, chargerlog_level);
	}
	return size;
}
static DEVICE_ATTR(charger_log_level, 0664, show_charger_log_level,
		store_charger_log_level);

static ssize_t show_pdc_max_watt_level(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	return sprintf(buf, "%d\n", mtk_pdc_get_max_watt(pinfo));
}

static ssize_t store_pdc_max_watt_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		mtk_pdc_set_max_watt(pinfo, temp);
		chr_err("[store_pdc_max_watt]:%d\n", temp);
	} else
		chr_err("[store_pdc_max_watt]: format error!\n");

	return size;
}
static DEVICE_ATTR(pdc_max_watt, 0644, show_pdc_max_watt_level,
		store_pdc_max_watt_level);

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
	if (is_vooc_project() == false) {
		if (mt_get_charger_type() != CHARGER_UNKNOWN) {
			oplus_wake_up_usbtemp_thread();

			pr_err("%s, Charger Plug In\n", __func__);
			charger_manager_notifier(pinfo, CHARGER_NOTIFY_START_CHARGING);
			pinfo->step_status = STEP_CHG_STATUS_STEP1;
			pinfo->step_status_pre = STEP_CHG_STATUS_INVALID;
			pinfo->step_cnt = 0;
			pinfo->step_chg_current = pinfo->data.step1_current_ma;
			charger_dev_set_input_current(g_oplus_chip->chgic_mtk.oplus_info->chg1_dev, 500000);
			schedule_delayed_work(&pinfo->step_charging_work, msecs_to_jiffies(5000));
		} else {
			pr_err("%s, Charger Plug Out\n", __func__);
#ifdef OPLUS_FEATURE_CHG_BASIC
			if (g_oplus_chip)
				g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();
#endif
			charger_dev_set_input_current(g_oplus_chip->chgic_mtk.oplus_info->chg1_dev, 500000);
			charger_manager_notifier(pinfo, CHARGER_NOTIFY_STOP_CHARGING);
			cancel_delayed_work(&pinfo->step_charging_work);
		}	
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
					pr_err("[OPLUS_CHG]   %d",gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
				}
			}
			chr_err("Charger Plug In\n");
		} else {
			oplus_set_divider_work_mode(charge_pump_mode);
			chr_err("charge_pump_mode = %d\n", charge_pump_mode);
#ifdef OPLUS_FEATURE_CHG_BASIC
			if (g_oplus_chip)
				g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();
#endif
			if (mtkhv_flashled_pinctrl.hv_flashled_support ){
				mtkhv_flashled_pinctrl.bc1_2_done = false;
				if(mt6360_get_vbus_rising() != true) {
					mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
					mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					pr_err("[OPLUS_CHG]   %d",gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
				}
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

	if (!mtkhv_flashled_pinctrl.hv_flashled_support){
		return -1;
	}

	if(plug == 1){//plug_in
		if (g_oplus_chip->camera_on) {
			mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
			pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
			mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
			//gpio_direction_output(mtkhv_flashled_pinctrl.chgvin_gpio, 1);
			chr_err("[%s] camera_on %d\n", __func__, g_oplus_chip->camera_on);
			pr_err("[OPLUS_CHG]   %d",gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
		}
	} else if(plug == 0) {
		mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
		mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		//gpio_direction_output(mtkhv_flashled_pinctrl.chgvin_gpio, 0);
		pr_err("[OPLUS_CHG]   %d",gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
	}

	return 0;
}
EXPORT_SYMBOL(oplus_mtk_hv_flashled_plug);

static int mtk_charger_plug_in(struct charger_manager *info,
				enum charger_type chr_type)
{
	info->chr_type = chr_type;
	info->charger_thread_polling = true;

	info->can_charging = true;
	info->enable_dynamic_cv = true;
	info->safety_timeout = false;
	info->vbusov_stat = false;

	chr_err("mtk_is_charger_on plug in, type:%d\n", chr_type);
	if (info->plug_in != NULL)
		info->plug_in(info);
	
	memset(&pinfo->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	pinfo->sc.disable_in_this_plug = false;
	wakeup_sc_algo_cmd(&pinfo->sc.data, SC_EVENT_PLUG_IN, 0);
	charger_dev_set_input_current(info->chg1_dev,
				info->chg1_data.input_current_limit);
	charger_dev_plug_in(info->chg1_dev);
	return 0;
}

static int mtk_charger_plug_out(struct charger_manager *info)
{
	struct charger_data *pdata1 = &info->chg1_data;
	struct charger_data *pdata2 = &info->chg2_data;

	chr_err("%s\n", __func__);
	info->chr_type = CHARGER_UNKNOWN;
	info->charger_thread_polling = false;
	info->pd_reset = false;

	pdata1->disable_charging_count = 0;
	pdata1->input_current_limit_by_aicl = -1;
	pdata2->disable_charging_count = 0;

	if (info->plug_out != NULL)
		info->plug_out(info);

	memset(&pinfo->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	wakeup_sc_algo_cmd(&pinfo->sc.data, SC_EVENT_PLUG_OUT, 0);
	charger_dev_set_input_current(info->chg1_dev, 100000);
	charger_dev_set_mivr(info->chg1_dev, info->data.min_charger_voltage);
	charger_dev_plug_out(info->chg1_dev);
	return 0;
}

static bool mtk_is_charger_on(struct charger_manager *info)
{
	enum charger_type chr_type;

	chr_type = mt_get_charger_type();
	if (chr_type == CHARGER_UNKNOWN) {
		if (info->chr_type != CHARGER_UNKNOWN) {
			mtk_charger_plug_out(info);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	} else {
		if (info->chr_type == CHARGER_UNKNOWN)
			mtk_charger_plug_in(info, chr_type);
		else
			info->chr_type = chr_type;

		if (info->cable_out_cnt > 0) {
			mtk_charger_plug_out(info);
			mtk_charger_plug_in(info, chr_type);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt--;
			mutex_unlock(&info->cable_out_lock);
		}
	}

	if (chr_type == CHARGER_UNKNOWN)
		return false;

	return true;
}

static void charger_update_data(struct charger_manager *info)
{
	info->battery_temp = battery_get_bat_temperature();
}

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

/* return false if vbus is over max_charger_voltage */
static bool mtk_chg_check_vbus(struct charger_manager *info)
{
	int vchr = 0;

	vchr = battery_get_vbus() * 1000; /* uV */
	if (vchr > info->data.max_charger_voltage) {
		chr_err("%s: vbus(%d mV) > %d mV\n", __func__, vchr / 1000,
			info->data.max_charger_voltage / 1000);
		return false;
	}

	return true;
}

static void mtk_battery_notify_VCharger_check(struct charger_manager *info)
{
#if defined(BATTERY_NOTIFY_CASE_0001_VCHARGER)
	int vchr = 0;

	vchr = battery_get_vbus() * 1000; /* uV */
	if (vchr < info->data.max_charger_voltage)
		info->notify_code &= ~CHG_VBUS_OV_STATUS;
	else {
		info->notify_code |= CHG_VBUS_OV_STATUS;
		chr_err("[BATTERY] charger_vol(%d mV) > %d mV\n",
			vchr / 1000, info->data.max_charger_voltage / 1000);
		mtk_chgstat_notify(info);
	}
#endif
}

static void mtk_battery_notify_VBatTemp_check(struct charger_manager *info)
{
#if defined(BATTERY_NOTIFY_CASE_0002_VBATTEMP)
	if (info->battery_temp >= info->thermal.max_charge_temp) {
		info->notify_code |= CHG_BAT_OT_STATUS;
		chr_err("[BATTERY] bat_temp(%d) out of range(too high)\n",
			info->battery_temp);
		mtk_chgstat_notify(info);
	} else {
		info->notify_code &= ~CHG_BAT_OT_STATUS;
	}

	if (info->enable_sw_jeita == true) {
		if (info->battery_temp < info->data.temp_neg_10_thres) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
	} else {
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
		if (info->battery_temp < info->thermal.min_charge_temp) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
#endif
	}
#endif
}

static void mtk_battery_notify_UI_test(struct charger_manager *info)
{
	switch (info->notify_test_mode) {
	case 1:
		info->notify_code = CHG_VBUS_OV_STATUS;
		pr_debug("[%s] CASE_0001_VCHARGER\n", __func__);
		break;
	case 2:
		info->notify_code = CHG_BAT_OT_STATUS;
		pr_debug("[%s] CASE_0002_VBATTEMP\n", __func__);
		break;
	case 3:
		info->notify_code = CHG_OC_STATUS;
		pr_debug("[%s] CASE_0003_ICHARGING\n", __func__);
		break;
	case 4:
		info->notify_code = CHG_BAT_OV_STATUS;
		pr_debug("[%s] CASE_0004_VBAT\n", __func__);
		break;
	case 5:
		info->notify_code = CHG_ST_TMO_STATUS;
		pr_debug("[%s] CASE_0005_TOTAL_CHARGINGTIME\n", __func__);
		break;
	case 6:
		info->notify_code = CHG_BAT_LT_STATUS;
		pr_debug("[%s] CASE6: VBATTEMP_LOW\n", __func__);
		break;
	case 7:
		info->notify_code = CHG_TYPEC_WD_STATUS;
		pr_debug("[%s] CASE7: Moisture Detection\n", __func__);
		break;
	default:
		pr_debug("[%s] Unknown BN_TestMode Code: %x\n",
			__func__, info->notify_test_mode);
	}
	mtk_chgstat_notify(info);
}

static void mtk_battery_notify_check(struct charger_manager *info)
{
	if (info->notify_test_mode == 0x0000) {
		mtk_battery_notify_VCharger_check(info);
		mtk_battery_notify_VBatTemp_check(info);
	} else {
		mtk_battery_notify_UI_test(info);
	}
}

static void check_battery_exist(struct charger_manager *info)
{
	unsigned int i = 0;
	int count = 0;
	int boot_mode = get_boot_mode();

	if (is_disable_charger())
		return;

	for (i = 0; i < 3; i++) {
		if (pmic_is_battery_exist() == false)
			count++;
	}

	if (count >= 3) {
		if (boot_mode == META_BOOT || boot_mode == ADVMETA_BOOT ||
		    boot_mode == ATE_FACTORY_BOOT)
			chr_info("boot_mode = %d, bypass battery check\n",
				boot_mode);
		else {
			chr_err("battery doesn't exist, shutdown\n");
			orderly_poweroff(true);
		}
	}
}

static void check_dynamic_mivr(struct charger_manager *info)
{
	int vbat = 0;

	if (info->enable_dynamic_mivr) {
		if (!mtk_pe40_get_is_connect(info) &&
			!mtk_pe20_get_is_connect(info) &&
			!mtk_pe_get_is_connect(info) &&
			!mtk_pdc_check_charger(info)) {

			vbat = battery_get_bat_voltage();
			if (vbat <
				info->data.min_charger_voltage_2 / 1000 - 200)
				charger_dev_set_mivr(info->chg1_dev,
					info->data.min_charger_voltage_2);
			else if (vbat <
				info->data.min_charger_voltage_1 / 1000 - 200)
				charger_dev_set_mivr(info->chg1_dev,
					info->data.min_charger_voltage_1);
			else
				charger_dev_set_mivr(info->chg1_dev,
					info->data.min_charger_voltage);
		}
	}
}

static void mtk_chg_get_tchg(struct charger_manager *info)
{
	int ret;
	int tchg_min = -127, tchg_max = -127;
	struct charger_data *pdata;

	pdata = &info->chg1_data;
	ret = charger_dev_get_temperature(info->chg1_dev, &tchg_min, &tchg_max);

	if (ret < 0) {
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
	} else {
		pdata->junction_temp_min = tchg_min;
		pdata->junction_temp_max = tchg_max;
	}

	if (is_slave_charger_exist()) {
		pdata = &info->chg2_data;
		ret = charger_dev_get_temperature(info->chg2_dev,
			&tchg_min, &tchg_max);

		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->dvchg1_dev) {
		pdata = &info->dvchg1_data;
		ret = charger_dev_get_adc(info->dvchg1_dev, ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->dvchg2_dev) {
		pdata = &info->dvchg2_data;
		ret = charger_dev_get_adc(info->dvchg2_dev, ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}
}

static void charger_check_status(struct charger_manager *info)
{
	bool charging = true;
	int temperature = 0;
	struct battery_thermal_protection_data *thermal = NULL;

	if (mt_get_charger_type() == CHARGER_UNKNOWN)
		return;

	temperature = info->battery_temp;
	thermal = &info->thermal;

	if (info->enable_sw_jeita == true) {
		do_sw_jeita_state_machine(info);
		if (info->sw_jeita.charging == false) {
			charging = false;
			goto stop_charging;
		}
	} else {

		if (thermal->enable_min_charge_temp) {
			if (temperature < thermal->min_charge_temp) {
				chr_err("Battery Under Temperature or NTC fail %d %d\n",
					temperature, thermal->min_charge_temp);
				thermal->sm = BAT_TEMP_LOW;
				charging = false;
				goto stop_charging;
			} else if (thermal->sm == BAT_TEMP_LOW) {
				if (temperature >=
				    thermal->min_charge_temp_plus_x_degree) {
					chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
					thermal->min_charge_temp,
					temperature,
					thermal->min_charge_temp_plus_x_degree);
					thermal->sm = BAT_TEMP_NORMAL;
				} else {
					charging = false;
					goto stop_charging;
				}
			}
		}

		if (temperature >= thermal->max_charge_temp) {
			chr_err("Battery over Temperature or NTC fail %d %d\n",
				temperature, thermal->max_charge_temp);
			thermal->sm = BAT_TEMP_HIGH;
			charging = false;
			goto stop_charging;
		} else if (thermal->sm == BAT_TEMP_HIGH) {
			if (temperature
			    < thermal->max_charge_temp_minus_x_degree) {
				chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
				thermal->max_charge_temp,
				temperature,
				thermal->max_charge_temp_minus_x_degree);
				thermal->sm = BAT_TEMP_NORMAL;
			} else {
				charging = false;
				goto stop_charging;
			}
		}
	}

	mtk_chg_get_tchg(info);

	if (!mtk_chg_check_vbus(info)) {
		charging = false;
		goto stop_charging;
	}

	if (info->cmd_discharging)
		charging = false;
	if (info->safety_timeout)
		charging = false;
	if (info->vbusov_stat)
		charging = false;
	if (info->sc.disable_charger == true)
		charging = false;

stop_charging:
	mtk_battery_notify_check(info);

	chr_err("tmp:%d (jeita:%d sm:%d cv:%d en:%d) (sm:%d) en:%d c:%d s:%d ov:%d sc:%d %d %d\n",
		temperature, info->enable_sw_jeita, info->sw_jeita.sm,
		info->sw_jeita.cv, info->sw_jeita.charging, thermal->sm,
		charging, info->cmd_discharging, info->safety_timeout,
		info->vbusov_stat, info->sc.disable_charger,
		info->can_charging, charging);

	if (charging != info->can_charging)
		_charger_manager_enable_charging(info->chg1_consumer,
						0, charging);

	info->can_charging = charging;
}

static void kpoc_power_off_check(struct charger_manager *info)
{
	unsigned int boot_mode = get_boot_mode();
	int vbus = 0;

	if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT
	    || boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {
		if (atomic_read(&info->enable_kpoc_shdn)) {
			vbus = battery_get_vbus();
			if (vbus >= 0 && vbus < 2500 && !mt_charger_plugin()) {
				chr_err("Unplug Charger/USB in KPOC mode, shutdown\n");
				chr_err("%s: system_state=%d\n", __func__,
					system_state);
				if (system_state != SYSTEM_POWER_OFF)
					kernel_power_off();
			}
		}
	}
}

#ifdef CONFIG_PM
static int charger_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	struct timespec now;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pinfo->is_suspend = true;
		chr_debug("%s: enter PM_SUSPEND_PREPARE\n", __func__);
		break;
	case PM_POST_SUSPEND:
		pinfo->is_suspend = false;
		chr_debug("%s: enter PM_POST_SUSPEND\n", __func__);
		get_monotonic_boottime(&now);

		if (timespec_compare(&now, &pinfo->endtime) >= 0 &&
			pinfo->endtime.tv_sec != 0 &&
			pinfo->endtime.tv_nsec != 0) {
			chr_err("%s: alarm timeout, wake up charger\n",
				__func__);
			pinfo->endtime.tv_sec = 0;
			pinfo->endtime.tv_nsec = 0;
			_wake_up_charger(pinfo);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block charger_pm_notifier_func = {
	.notifier_call = charger_pm_event,
	.priority = 0,
};
#endif /* CONFIG_PM */

static enum alarmtimer_restart
	mtk_charger_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct charger_manager *info =
	container_of(alarm, struct charger_manager, charger_timer);
	unsigned long flags;

	if (info->is_suspend == false) {
		chr_err("%s: not suspend, wake up charger\n", __func__);
		_wake_up_charger(info);
	} else {
		chr_err("%s: alarm timer timeout\n", __func__);
		spin_lock_irqsave(&info->slock, flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		if (!info->charger_wakelock.active)
			__pm_stay_awake(&info->charger_wakelock);
#else
		if (!info->charger_wakelock->active)
			__pm_stay_awake(info->charger_wakelock);
#endif
		spin_unlock_irqrestore(&info->slock, flags);
	}

	return ALARMTIMER_NORESTART;
}

static void mtk_charger_start_timer(struct charger_manager *info)
{
	struct timespec time, time_now;
	ktime_t ktime;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&pinfo->charger_timer);
	if (ret < 0) {
		chr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	get_monotonic_boottime(&time_now);
	time.tv_sec = info->polling_interval;
	time.tv_nsec = 0;
	info->endtime = timespec_add(time_now, time);

	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);

	chr_err("%s: alarm timer start:%d, %ld %ld\n", __func__, ret,
		info->endtime.tv_sec, info->endtime.tv_nsec);
	alarm_start(&pinfo->charger_timer, ktime);
}

static void mtk_charger_init_timer(struct charger_manager *info)
{
	alarm_init(&info->charger_timer, ALARM_BOOTTIME,
			mtk_charger_alarm_timer_func);
	mtk_charger_start_timer(info);

#ifdef CONFIG_PM
	if (register_pm_notifier(&charger_pm_notifier_func))
		chr_err("%s: register pm failed\n", __func__);
#endif /* CONFIG_PM */
}

static int charger_routine_thread(void *arg)
{
	struct charger_manager *info = arg;
	unsigned long flags = 0;
	bool is_charger_on = false;
	int bat_current = 0, chg_current = 0;

	while (1) {
		wait_event(info->wait_que,
			(info->charger_thread_timeout == true));

		mutex_lock(&info->charger_lock);
		spin_lock_irqsave(&info->slock, flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		if (!info->charger_wakelock.active)
			__pm_stay_awake(&info->charger_wakelock);
#else
		if (!info->charger_wakelock->active)
			__pm_stay_awake(info->charger_wakelock);
#endif
		spin_unlock_irqrestore(&info->slock, flags);

		info->charger_thread_timeout = false;
		bat_current = battery_get_bat_current();
		chg_current = pmic_get_charging_current();
		chr_err("Vbat=%d,Ibat=%d,I=%d,VChr=%d,T=%d,Soc=%d:%d,CT:%d:%d hv:%d pd:%d:%d\n",
			battery_get_bat_voltage(), bat_current, chg_current,
			battery_get_vbus(), battery_get_bat_temperature(),
			battery_get_soc(), battery_get_uisoc(),
			mt_get_charger_type(), info->chr_type,
			info->enable_hv_charging, info->pd_type,
			info->pd_reset);

		is_charger_on = mtk_is_charger_on(info);

		if (info->charger_thread_polling == true)
			mtk_charger_start_timer(info);

		charger_update_data(info);
		check_battery_exist(info);
		check_dynamic_mivr(info);
		charger_check_status(info);
		kpoc_power_off_check(info);

		if (is_disable_charger() == false) {
			if (is_charger_on == true) {
				if (info->do_algorithm)
					info->do_algorithm(info);
				sc_update(pinfo);
				wakeup_sc_algo_cmd(&pinfo->sc.data, SC_EVENT_CHARGING, 0);
			} else {
				sc_update(pinfo);
				wakeup_sc_algo_cmd(&pinfo->sc.data, SC_EVENT_STOP_CHARGING, 0);
			}
		} else
			chr_debug("disable charging\n");

		spin_lock_irqsave(&info->slock, flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		__pm_relax(&info->charger_wakelock);
#else
		__pm_relax(info->charger_wakelock);
#endif
		spin_unlock_irqrestore(&info->slock, flags);
		chr_debug("%s end , %d\n",
			__func__, info->charger_thread_timeout);
		mutex_unlock(&info->charger_lock);
	}

	return 0;
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

	if (of_property_read_string(np, "algorithm_name",
		&info->algorithm_name) < 0) {
		chr_err("%s: no algorithm_name name\n", __func__);
		info->algorithm_name = "SwitchCharging";
	}

	if (strcmp(info->algorithm_name, "SwitchCharging") == 0) {
		chr_err("found SwitchCharging\n");
		mtk_switch_charging_init(info);
	}
#ifdef CONFIG_MTK_DUAL_CHARGER_SUPPORT
	if (strcmp(info->algorithm_name, "DualSwitchCharging") == 0) {
		pr_debug("found DualSwitchCharging\n");
		mtk_dual_switch_charging_init(info);
	}
#endif

	info->disable_charger = of_property_read_bool(np, "disable_charger");
	info->enable_sw_safety_timer =
			of_property_read_bool(np, "enable_sw_safety_timer");
	info->sw_safety_timer_setting = info->enable_sw_safety_timer;
	info->enable_sw_jeita = of_property_read_bool(np, "enable_sw_jeita");
	info->enable_pe_plus = of_property_read_bool(np, "enable_pe_plus");
	info->enable_pe_2 = of_property_read_bool(np, "enable_pe_2");
	info->enable_pe_4 = of_property_read_bool(np, "enable_pe_4");
	info->enable_pe_5 = of_property_read_bool(np, "enable_pe_5");
	info->enable_type_c = of_property_read_bool(np, "enable_type_c");
	info->enable_dynamic_mivr =
			of_property_read_bool(np, "enable_dynamic_mivr");
	info->disable_pd_dual = of_property_read_bool(np, "disable_pd_dual");

	info->enable_hv_charging = true;

	/* common */
	if (of_property_read_u32(np, "battery_cv", &val) >= 0)
		info->data.battery_cv = val;
	else {
		chr_err("use default BATTERY_CV:%d\n", BATTERY_CV);
		info->data.battery_cv = BATTERY_CV;
	}

	if (of_property_read_u32(np, "max_charger_voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.max_charger_voltage = V_CHARGER_MAX;
	}
	info->data.max_charger_voltage_setting = info->data.max_charger_voltage;

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MIN:%d\n", V_CHARGER_MIN);
		info->data.min_charger_voltage = V_CHARGER_MIN;
	}

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

	/* charging current */
	if (of_property_read_u32(np, "usb_charger_current_suspend", &val) >= 0)
		info->data.usb_charger_current_suspend = val;
	else {
		chr_err("use default USB_CHARGER_CURRENT_SUSPEND:%d\n",
			USB_CHARGER_CURRENT_SUSPEND);
		info->data.usb_charger_current_suspend =
						USB_CHARGER_CURRENT_SUSPEND;
	}

	if (of_property_read_u32(np, "usb_charger_current_unconfigured", &val)
		>= 0) {
		info->data.usb_charger_current_unconfigured = val;
	} else {
		chr_err("use default USB_CHARGER_CURRENT_UNCONFIGURED:%d\n",
			USB_CHARGER_CURRENT_UNCONFIGURED);
		info->data.usb_charger_current_unconfigured =
					USB_CHARGER_CURRENT_UNCONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current_configured", &val)
		>= 0) {
		info->data.usb_charger_current_configured = val;
	} else {
		chr_err("use default USB_CHARGER_CURRENT_CONFIGURED:%d\n",
			USB_CHARGER_CURRENT_CONFIGURED);
		info->data.usb_charger_current_configured =
					USB_CHARGER_CURRENT_CONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current", &val) >= 0) {
		info->data.usb_charger_current = val;
	} else {
		chr_err("use default USB_CHARGER_CURRENT:%d\n",
			USB_CHARGER_CURRENT);
		info->data.usb_charger_current = USB_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_current", &val) >= 0) {
		info->data.ac_charger_current = val;
	} else {
		chr_err("use default AC_CHARGER_CURRENT:%d\n",
			AC_CHARGER_CURRENT);
		info->data.ac_charger_current = AC_CHARGER_CURRENT;
	}

	info->data.pd_charger_current = 3000000;

	if (of_property_read_u32(np, "ac_charger_input_current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else {
		chr_err("use default AC_CHARGER_INPUT_CURRENT:%d\n",
			AC_CHARGER_INPUT_CURRENT);
		info->data.ac_charger_input_current = AC_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "non_std_ac_charger_current", &val) >= 0)
		info->data.non_std_ac_charger_current = val;
	else {
		chr_err("use default NON_STD_AC_CHARGER_CURRENT:%d\n",
			NON_STD_AC_CHARGER_CURRENT);
		info->data.non_std_ac_charger_current =
					NON_STD_AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "charging_host_charger_current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else {
		chr_err("use default CHARGING_HOST_CHARGER_CURRENT:%d\n",
			CHARGING_HOST_CHARGER_CURRENT);
		info->data.charging_host_charger_current =
					CHARGING_HOST_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_1_0a_charger_current", &val) >= 0)
		info->data.apple_1_0a_charger_current = val;
	else {
		chr_err("use default APPLE_1_0A_CHARGER_CURRENT:%d\n",
			APPLE_1_0A_CHARGER_CURRENT);
		info->data.apple_1_0a_charger_current =
					APPLE_1_0A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_2_1a_charger_current", &val) >= 0)
		info->data.apple_2_1a_charger_current = val;
	else {
		chr_err("use default APPLE_2_1A_CHARGER_CURRENT:%d\n",
			APPLE_2_1A_CHARGER_CURRENT);
		info->data.apple_2_1a_charger_current =
					APPLE_2_1A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ta_ac_charger_current", &val) >= 0)
		info->data.ta_ac_charger_current = val;
	else {
		chr_err("use default TA_AC_CHARGING_CURRENT:%d\n",
			TA_AC_CHARGING_CURRENT);
		info->data.ta_ac_charger_current =
					TA_AC_CHARGING_CURRENT;
	}

	/* sw jeita */
	if (of_property_read_u32(np, "jeita_temp_above_t4_cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_CV:%d\n",
			JEITA_TEMP_ABOVE_T4_CV);
		info->data.jeita_temp_above_t4_cv = JEITA_TEMP_ABOVE_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_CV:%d\n",
			JEITA_TEMP_T3_TO_T4_CV);
		info->data.jeita_temp_t3_to_t4_cv = JEITA_TEMP_T3_TO_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_CV:%d\n",
			JEITA_TEMP_T2_TO_T3_CV);
		info->data.jeita_temp_t2_to_t3_cv = JEITA_TEMP_T2_TO_T3_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_CV:%d\n",
			JEITA_TEMP_T1_TO_T2_CV);
		info->data.jeita_temp_t1_to_t2_cv = JEITA_TEMP_T1_TO_T2_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_CV:%d\n",
			JEITA_TEMP_T0_TO_T1_CV);
		info->data.jeita_temp_t0_to_t1_cv = JEITA_TEMP_T0_TO_T1_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_CV:%d\n",
			JEITA_TEMP_BELOW_T0_CV);
		info->data.jeita_temp_below_t0_cv = JEITA_TEMP_BELOW_T0_CV;
	}

	if (of_property_read_u32(np, "temp_t4_thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else {
		chr_err("use default TEMP_T4_THRES:%d\n",
			TEMP_T4_THRES);
		info->data.temp_t4_thres = TEMP_T4_THRES;
	}

	if (of_property_read_u32(np, "temp_t4_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T4_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T4_THRES_MINUS_X_DEGREE);
		info->data.temp_t4_thres_minus_x_degree =
					TEMP_T4_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t3_thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else {
		chr_err("use default TEMP_T3_THRES:%d\n",
			TEMP_T3_THRES);
		info->data.temp_t3_thres = TEMP_T3_THRES;
	}

	if (of_property_read_u32(np, "temp_t3_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T3_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T3_THRES_MINUS_X_DEGREE);
		info->data.temp_t3_thres_minus_x_degree =
					TEMP_T3_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t2_thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else {
		chr_err("use default TEMP_T2_THRES:%d\n",
			TEMP_T2_THRES);
		info->data.temp_t2_thres = TEMP_T2_THRES;
	}

	if (of_property_read_u32(np, "temp_t2_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T2_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T2_THRES_PLUS_X_DEGREE);
		info->data.temp_t2_thres_plus_x_degree =
					TEMP_T2_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t1_thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else {
		chr_err("use default TEMP_T1_THRES:%d\n",
			TEMP_T1_THRES);
		info->data.temp_t1_thres = TEMP_T1_THRES;
	}

	if (of_property_read_u32(np, "temp_t1_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T1_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T1_THRES_PLUS_X_DEGREE);
		info->data.temp_t1_thres_plus_x_degree =
					TEMP_T1_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t0_thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else {
		chr_err("use default TEMP_T0_THRES:%d\n",
			TEMP_T0_THRES);
		info->data.temp_t0_thres = TEMP_T0_THRES;
	}

	if (of_property_read_u32(np, "temp_t0_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T0_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T0_THRES_PLUS_X_DEGREE);
		info->data.temp_t0_thres_plus_x_degree =
					TEMP_T0_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_neg_10_thres", &val) >= 0)
		info->data.temp_neg_10_thres = val;
	else {
		chr_err("use default TEMP_NEG_10_THRES:%d\n",
			TEMP_NEG_10_THRES);
		info->data.temp_neg_10_thres = TEMP_NEG_10_THRES;
	}

	/* battery temperature protection */
	info->thermal.sm = BAT_TEMP_NORMAL;
	info->thermal.enable_min_charge_temp =
		of_property_read_bool(np, "enable_min_charge_temp");

	if (of_property_read_u32(np, "min_charge_temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else {
		chr_err("use default MIN_CHARGE_TEMP:%d\n",
			MIN_CHARGE_TEMP);
		info->thermal.min_charge_temp = MIN_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "min_charge_temp_plus_x_degree", &val)
	    >= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else {
		chr_err("use default MIN_CHARGE_TEMP_PLUS_X_DEGREE:%d\n",
			MIN_CHARGE_TEMP_PLUS_X_DEGREE);
		info->thermal.min_charge_temp_plus_x_degree =
					MIN_CHARGE_TEMP_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "max_charge_temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else {
		chr_err("use default MAX_CHARGE_TEMP:%d\n",
			MAX_CHARGE_TEMP);
		info->thermal.max_charge_temp = MAX_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "max_charge_temp_minus_x_degree", &val)
	    >= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else {
		chr_err("use default MAX_CHARGE_TEMP_MINUS_X_DEGREE:%d\n",
			MAX_CHARGE_TEMP_MINUS_X_DEGREE);
		info->thermal.max_charge_temp_minus_x_degree =
					MAX_CHARGE_TEMP_MINUS_X_DEGREE;
	}

	/* PE */
	info->data.ta_12v_support = of_property_read_bool(np, "ta_12v_support");
	info->data.ta_9v_support = of_property_read_bool(np, "ta_9v_support");

	if (of_property_read_u32(np, "pe_ichg_level_threshold", &val) >= 0)
		info->data.pe_ichg_level_threshold = val;
	else {
		chr_err("use default PE_ICHG_LEAVE_THRESHOLD:%d\n",
			PE_ICHG_LEAVE_THRESHOLD);
		info->data.pe_ichg_level_threshold = PE_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "ta_ac_12v_input_current", &val) >= 0)
		info->data.ta_ac_12v_input_current = val;
	else {
		chr_err("use default TA_AC_12V_INPUT_CURRENT:%d\n",
			TA_AC_12V_INPUT_CURRENT);
		info->data.ta_ac_12v_input_current = TA_AC_12V_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "ta_ac_9v_input_current", &val) >= 0)
		info->data.ta_ac_9v_input_current = val;
	else {
		chr_err("use default TA_AC_9V_INPUT_CURRENT:%d\n",
			TA_AC_9V_INPUT_CURRENT);
		info->data.ta_ac_9v_input_current = TA_AC_9V_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "ta_ac_7v_input_current", &val) >= 0)
		info->data.ta_ac_7v_input_current = val;
	else {
		chr_err("use default TA_AC_7V_INPUT_CURRENT:%d\n",
			TA_AC_7V_INPUT_CURRENT);
		info->data.ta_ac_7v_input_current = TA_AC_7V_INPUT_CURRENT;
	}

	/* PE 2.0 */
	if (of_property_read_u32(np, "pe20_ichg_level_threshold", &val) >= 0)
		info->data.pe20_ichg_level_threshold = val;
	else {
		chr_err("use default PE20_ICHG_LEAVE_THRESHOLD:%d\n",
			PE20_ICHG_LEAVE_THRESHOLD);
		info->data.pe20_ichg_level_threshold =
						PE20_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "ta_start_battery_soc", &val) >= 0)
		info->data.ta_start_battery_soc = val;
	else {
		chr_err("use default TA_START_BATTERY_SOC:%d\n",
			TA_START_BATTERY_SOC);
		info->data.ta_start_battery_soc = TA_START_BATTERY_SOC;
	}

	if (of_property_read_u32(np, "ta_stop_battery_soc", &val) >= 0)
		info->data.ta_stop_battery_soc = val;
	else {
		chr_err("use default TA_STOP_BATTERY_SOC:%d\n",
			TA_STOP_BATTERY_SOC);
		info->data.ta_stop_battery_soc = TA_STOP_BATTERY_SOC;
	}

	/* PE 4.0 */
	if (of_property_read_u32(np, "high_temp_to_leave_pe40", &val) >= 0) {
		info->data.high_temp_to_leave_pe40 = val;
	} else {
		chr_err("use default high_temp_to_leave_pe40:%d\n",
			HIGH_TEMP_TO_LEAVE_PE40);
		info->data.high_temp_to_leave_pe40 = HIGH_TEMP_TO_LEAVE_PE40;
	}

	if (of_property_read_u32(np, "high_temp_to_enter_pe40", &val) >= 0) {
		info->data.high_temp_to_enter_pe40 = val;
	} else {
		chr_err("use default high_temp_to_enter_pe40:%d\n",
			HIGH_TEMP_TO_ENTER_PE40);
		info->data.high_temp_to_enter_pe40 = HIGH_TEMP_TO_ENTER_PE40;
	}

	if (of_property_read_u32(np, "low_temp_to_leave_pe40", &val) >= 0) {
		info->data.low_temp_to_leave_pe40 = val;
	} else {
		chr_err("use default low_temp_to_leave_pe40:%d\n",
			LOW_TEMP_TO_LEAVE_PE40);
		info->data.low_temp_to_leave_pe40 = LOW_TEMP_TO_LEAVE_PE40;
	}

	if (of_property_read_u32(np, "low_temp_to_enter_pe40", &val) >= 0) {
		info->data.low_temp_to_enter_pe40 = val;
	} else {
		chr_err("use default low_temp_to_enter_pe40:%d\n",
			LOW_TEMP_TO_ENTER_PE40);
		info->data.low_temp_to_enter_pe40 = LOW_TEMP_TO_ENTER_PE40;
	}

	/* PE 4.0 single */
	if (of_property_read_u32(np,
		"pe40_single_charger_input_current", &val) >= 0) {
		info->data.pe40_single_charger_input_current = val;
	} else {
		chr_err("use default pe40_single_charger_input_current:%d\n",
			3000);
		info->data.pe40_single_charger_input_current = 3000;
	}

	if (of_property_read_u32(np, "pe40_single_charger_current", &val)
	    >= 0) {
		info->data.pe40_single_charger_current = val;
	} else {
		chr_err("use default pe40_single_charger_current:%d\n", 3000);
		info->data.pe40_single_charger_current = 3000;
	}

	/* PE 4.0 dual */
	if (of_property_read_u32(np, "pe40_dual_charger_input_current", &val)
	    >= 0) {
		info->data.pe40_dual_charger_input_current = val;
	} else {
		chr_err("use default pe40_dual_charger_input_current:%d\n",
			3000);
		info->data.pe40_dual_charger_input_current = 3000;
	}

	if (of_property_read_u32(np, "pe40_dual_charger_chg1_current", &val)
	    >= 0) {
		info->data.pe40_dual_charger_chg1_current = val;
	} else {
		chr_err("use default pe40_dual_charger_chg1_current:%d\n",
			2000);
		info->data.pe40_dual_charger_chg1_current = 2000;
	}

	if (of_property_read_u32(np, "pe40_dual_charger_chg2_current", &val)
	    >= 0) {
		info->data.pe40_dual_charger_chg2_current = val;
	} else {
		chr_err("use default pe40_dual_charger_chg2_current:%d\n",
			2000);
		info->data.pe40_dual_charger_chg2_current = 2000;
	}

	if (of_property_read_u32(np, "dual_polling_ieoc", &val) >= 0)
		info->data.dual_polling_ieoc = val;
	else {
		chr_err("use default dual_polling_ieoc :%d\n", 750000);
		info->data.dual_polling_ieoc = 750000;
	}

	if (of_property_read_u32(np, "pe40_stop_battery_soc", &val) >= 0)
		info->data.pe40_stop_battery_soc = val;
	else {
		chr_err("use default pe40_stop_battery_soc:%d\n", 80);
		info->data.pe40_stop_battery_soc = 80;
	}

	if (of_property_read_u32(np, "pe40_max_vbus", &val) >= 0)
		info->data.pe40_max_vbus = val;
	else {
		chr_err("use default pe40_max_vbus:%d\n", PE40_MAX_VBUS);
		info->data.pe40_max_vbus = PE40_MAX_VBUS;
	}

	if (of_property_read_u32(np, "pe40_max_ibus", &val) >= 0)
		info->data.pe40_max_ibus = val;
	else {
		chr_err("use default pe40_max_ibus:%d\n", PE40_MAX_IBUS);
		info->data.pe40_max_ibus = PE40_MAX_IBUS;
	}

	/* PE 4.0 cable impedance (mohm) */
	if (of_property_read_u32(np, "pe40_r_cable_1a_lower", &val) >= 0)
		info->data.pe40_r_cable_1a_lower = val;
	else {
		chr_err("use default pe40_r_cable_1a_lower:%d\n", 530);
		info->data.pe40_r_cable_1a_lower = 530;
	}

	if (of_property_read_u32(np, "pe40_r_cable_2a_lower", &val) >= 0)
		info->data.pe40_r_cable_2a_lower = val;
	else {
		chr_err("use default pe40_r_cable_2a_lower:%d\n", 390);
		info->data.pe40_r_cable_2a_lower = 390;
	}

	if (of_property_read_u32(np, "pe40_r_cable_3a_lower", &val) >= 0)
		info->data.pe40_r_cable_3a_lower = val;
	else {
		chr_err("use default pe40_r_cable_3a_lower:%d\n", 252);
		info->data.pe40_r_cable_3a_lower = 252;
	}

	/* PD */
	if (of_property_read_u32(np, "pd_vbus_upper_bound", &val) >= 0) {
		info->data.pd_vbus_upper_bound = val;
	} else {
		chr_err("use default pd_vbus_upper_bound:%d\n",
			PD_VBUS_UPPER_BOUND);
		info->data.pd_vbus_upper_bound = PD_VBUS_UPPER_BOUND;
	}

	if (of_property_read_u32(np, "pd_vbus_low_bound", &val) >= 0) {
		info->data.pd_vbus_low_bound = val;
	} else {
		chr_err("use default pd_vbus_low_bound:%d\n",
			PD_VBUS_LOW_BOUND);
		info->data.pd_vbus_low_bound = PD_VBUS_LOW_BOUND;
	}

	if (of_property_read_u32(np, "pd_ichg_level_threshold", &val) >= 0)
		info->data.pd_ichg_level_threshold = val;
	else {
		chr_err("use default pd_ichg_level_threshold:%d\n",
			PD_ICHG_LEAVE_THRESHOLD);
		info->data.pd_ichg_level_threshold = PD_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "pd_stop_battery_soc", &val) >= 0)
		info->data.pd_stop_battery_soc = val;
	else {
		chr_err("use default pd_stop_battery_soc:%d\n",
			PD_STOP_BATTERY_SOC);
		info->data.pd_stop_battery_soc = PD_STOP_BATTERY_SOC;
	}

	if (of_property_read_u32(np, "vsys_watt", &val) >= 0) {
		info->data.vsys_watt = val;
	} else {
		chr_err("use default vsys_watt:%d\n",
			VSYS_WATT);
		info->data.vsys_watt = VSYS_WATT;
	}

	if (of_property_read_u32(np, "ibus_err", &val) >= 0) {
		info->data.ibus_err = val;
	} else {
		chr_err("use default ibus_err:%d\n",
			IBUS_ERR);
		info->data.ibus_err = IBUS_ERR;
	}

	/* dual charger */
	if (of_property_read_u32(np, "chg1_ta_ac_charger_current", &val) >= 0)
		info->data.chg1_ta_ac_charger_current = val;
	else {
		chr_err("use default TA_AC_MASTER_CHARGING_CURRENT:%d\n",
			TA_AC_MASTER_CHARGING_CURRENT);
		info->data.chg1_ta_ac_charger_current =
						TA_AC_MASTER_CHARGING_CURRENT;
	}

	if (of_property_read_u32(np, "chg2_ta_ac_charger_current", &val) >= 0)
		info->data.chg2_ta_ac_charger_current = val;
	else {
		chr_err("use default TA_AC_SLAVE_CHARGING_CURRENT:%d\n",
			TA_AC_SLAVE_CHARGING_CURRENT);
		info->data.chg2_ta_ac_charger_current =
						TA_AC_SLAVE_CHARGING_CURRENT;
	}

	if (of_property_read_u32(np, "slave_mivr_diff", &val) >= 0)
		info->data.slave_mivr_diff = val;
	else {
		chr_err("use default SLAVE_MIVR_DIFF:%d\n", SLAVE_MIVR_DIFF);
		info->data.slave_mivr_diff = SLAVE_MIVR_DIFF;
	}

	/* slave charger */
	if (of_property_read_u32(np, "chg2_eff", &val) >= 0)
		info->data.chg2_eff = val;
	else {
		chr_err("use default CHG2_EFF:%d\n", CHG2_EFF);
		info->data.chg2_eff = CHG2_EFF;
	}

	info->data.parallel_vbus = of_property_read_bool(np, "parallel_vbus");

	/* cable measurement impedance */
	if (of_property_read_u32(np, "cable_imp_threshold", &val) >= 0)
		info->data.cable_imp_threshold = val;
	else {
		chr_err("use default CABLE_IMP_THRESHOLD:%d\n",
			CABLE_IMP_THRESHOLD);
		info->data.cable_imp_threshold = CABLE_IMP_THRESHOLD;
	}

	if (of_property_read_u32(np, "vbat_cable_imp_threshold", &val) >= 0)
		info->data.vbat_cable_imp_threshold = val;
	else {
		chr_err("use default VBAT_CABLE_IMP_THRESHOLD:%d\n",
			VBAT_CABLE_IMP_THRESHOLD);
		info->data.vbat_cable_imp_threshold = VBAT_CABLE_IMP_THRESHOLD;
	}

	/* BIF */
	if (of_property_read_u32(np, "bif_threshold1", &val) >= 0)
		info->data.bif_threshold1 = val;
	else {
		chr_err("use default BIF_THRESHOLD1:%d\n",
			BIF_THRESHOLD1);
		info->data.bif_threshold1 = BIF_THRESHOLD1;
	}

	if (of_property_read_u32(np, "bif_threshold2", &val) >= 0)
		info->data.bif_threshold2 = val;
	else {
		chr_err("use default BIF_THRESHOLD2:%d\n",
			BIF_THRESHOLD2);
		info->data.bif_threshold2 = BIF_THRESHOLD2;
	}

	if (of_property_read_u32(np, "bif_cv_under_threshold2", &val) >= 0)
		info->data.bif_cv_under_threshold2 = val;
	else {
		chr_err("use default BIF_CV_UNDER_THRESHOLD2:%d\n",
			BIF_CV_UNDER_THRESHOLD2);
		info->data.bif_cv_under_threshold2 = BIF_CV_UNDER_THRESHOLD2;
	}

	info->data.power_path_support =
				of_property_read_bool(np, "power_path_support");
	chr_debug("%s: power_path_support: %d\n",
		__func__, info->data.power_path_support);

	if (of_property_read_u32(np, "max_charging_time", &val) >= 0)
		info->data.max_charging_time = val;
	else {
		chr_err("use default MAX_CHARGING_TIME:%d\n",
			MAX_CHARGING_TIME);
		info->data.max_charging_time = MAX_CHARGING_TIME;
	}

	if (of_property_read_u32(np, "bc12_charger", &val) >= 0)
		info->data.bc12_charger = val;
	else {
		chr_err("use default BC12_CHARGER:%d\n",
			DEFAULT_BC12_CHARGER);
		info->data.bc12_charger = DEFAULT_BC12_CHARGER;
	}

	if (strcmp(info->algorithm_name, "SwitchCharging2") == 0) {
		chr_err("found SwitchCharging2\n");
		mtk_switch_charging_init2(info);
	}

	chr_err("algorithm name:%s\n", info->algorithm_name);

#ifdef OPLUS_FEATURE_CHG_BASIC
	info->support_ntc_01c_precision = of_property_read_bool(np, "qcom,support_ntc_01c_precision");
	chr_debug("%s: support_ntc_01c_precision: %d\n",
		__func__, info->support_ntc_01c_precision);
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

	chg_err("step1_time: %d, step1_current: %d, step2_time: %d, step2_current: %d, step3_current: %d\n",
		info->data.step1_time, info->data.step1_current_ma, info->data.step2_time, info->data.step2_current_ma, info->data.step3_current_ma);

	return 0;
}


static ssize_t show_Pump_Express(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;
	int is_ta_detected = 0;

	pr_debug("[%s] chr_type:%d UISOC:%d startsoc:%d stopsoc:%d\n", __func__,
		mt_get_charger_type(), battery_get_uisoc(),
		pinfo->data.ta_start_battery_soc,
		pinfo->data.ta_stop_battery_soc);

	if (IS_ENABLED(CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT)) {
		/* Is PE+20 connect */
		if (mtk_pe20_get_is_connect(pinfo))
			is_ta_detected = 1;
	}

	if (IS_ENABLED(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)) {
		/* Is PE+ connect */
		if (mtk_pe_get_is_connect(pinfo))
			is_ta_detected = 1;
	}

	if (mtk_is_TA_support_pd_pps(pinfo) == true)
		is_ta_detected = 1;

	pr_debug("%s: detected = %d, pe20_connect = %d, pe_connect = %d\n",
		__func__, is_ta_detected,
		mtk_pe20_get_is_connect(pinfo),
		mtk_pe_get_is_connect(pinfo));

	return sprintf(buf, "%u\n", is_ta_detected);
}

static DEVICE_ATTR(Pump_Express, 0444, show_Pump_Express, NULL);

static ssize_t show_input_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg1_data.thermal_input_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg1_data.thermal_input_current_limit);
}

static ssize_t store_input_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg1_data.thermal_input_current_limit = reg;
		if (pinfo->data.parallel_vbus)
			pinfo->chg2_data.thermal_input_current_limit = reg;
		pr_debug("[Battery] %s: %x\n",
			__func__, pinfo->chg1_data.thermal_input_current_limit);
	}
	return size;
}
static DEVICE_ATTR(input_current, 0644, show_input_current,
		store_input_current);

static ssize_t show_chg1_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg1_data.thermal_charging_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg1_data.thermal_charging_current_limit);
}

static ssize_t store_chg1_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg1_data.thermal_charging_current_limit = reg;
		pr_debug("[Battery] %s: %x\n", __func__,
			pinfo->chg1_data.thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg1_current, 0644, show_chg1_current, store_chg1_current);

static ssize_t show_chg2_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg2_data.thermal_charging_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg2_data.thermal_charging_current_limit);
}

static ssize_t store_chg2_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg2_data.thermal_charging_current_limit = reg;
		pr_debug("[Battery] %s: %x\n", __func__,
			pinfo->chg2_data.thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg2_current, 0644, show_chg2_current, store_chg2_current);

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

static DEVICE_ATTR(BatteryNotify, 0644, show_BatNotify, store_BatNotify);

static ssize_t show_BN_TestMode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n", __func__, pinfo->notify_test_mode);
	return sprintf(buf, "%u\n", pinfo->notify_test_mode);
}

static ssize_t store_BN_TestMode(struct device *dev,
		struct device_attribute *attr, const char *buf,  size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->notify_test_mode = reg;
		pr_debug("[Battery] store mode: %x\n", pinfo->notify_test_mode);
	}
	return size;
}
static DEVICE_ATTR(BN_TestMode, 0644, show_BN_TestMode, store_BN_TestMode);

static ssize_t show_ADC_Charger_Voltage(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int vbus = battery_get_vbus();

	if (!atomic_read(&pinfo->enable_kpoc_shdn) || vbus < 0) {
		chr_err("HardReset or get vbus failed, vbus:%d:5000\n", vbus);
		vbus = 5000;
	}

	pr_debug("[%s]: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR(ADC_Charger_Voltage, 0444, show_ADC_Charger_Voltage, NULL);

/* procfs */
static int mtk_chg_current_cmd_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->usb_unlimited, pinfo->cmd_discharging);
	return 0;
}

static ssize_t mtk_chg_current_cmd_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32] = {0};
	int current_unlimited = 0;
	int cmd_discharging = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &current_unlimited, &cmd_discharging) == 2) {
		info->usb_unlimited = current_unlimited;
		if (cmd_discharging == 1) {
			info->cmd_discharging = true;
			charger_dev_enable(info->chg1_dev, false);
			charger_manager_notifier(info,
						CHARGER_NOTIFY_STOP_CHARGING);
		} else if (cmd_discharging == 0) {
			info->cmd_discharging = false;
			charger_dev_enable(info->chg1_dev, true);
			charger_manager_notifier(info,
						CHARGER_NOTIFY_START_CHARGING);
		}

		pr_debug("%s current_unlimited=%d, cmd_discharging=%d\n",
			__func__, current_unlimited, cmd_discharging);
		return count;
	}

	chr_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

static int mtk_chg_en_power_path_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(pinfo->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static ssize_t mtk_chg_en_power_path_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(info->chg1_dev, enable);
		pr_debug("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	chr_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static int mtk_chg_en_safety_timer_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;
	bool safety_timer_en = false;

	charger_dev_is_safety_timer_enabled(pinfo->chg1_dev, &safety_timer_en);
	seq_printf(m, "%d\n", safety_timer_en);

	return 0;
}

static ssize_t mtk_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_safety_timer(info->chg1_dev, enable);
		pr_debug("%s: enable safety timer = %d\n", __func__, enable);

		/* SW safety timer */
		if (info->sw_safety_timer_setting == true) {
			if (enable)
				info->enable_sw_safety_timer = true;
			else
				info->enable_sw_safety_timer = false;
		}

		return count;
	}

	chr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

/* PROC_FOPS_RW(battery_cmd); */
/* PROC_FOPS_RW(discharging_cmd); */
PROC_FOPS_RW(current_cmd);
PROC_FOPS_RW(en_power_path);
PROC_FOPS_RW(en_safety_timer);

/* Create sysfs and procfs attributes */
static int mtk_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
	struct proc_dir_entry *battery_dir = NULL;
	struct charger_manager *info = platform_get_drvdata(pdev);
	/* struct charger_device *chg_dev; */

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_jeita);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_pe20);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_pe40);
	if (ret)
		goto _out;

	/* Battery warning */
	ret = device_create_file(&(pdev->dev), &dev_attr_BatteryNotify);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_BN_TestMode);
	if (ret)
		goto _out;
	/* Pump express */
	ret = device_create_file(&(pdev->dev), &dev_attr_Pump_Express);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_log_level);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_pdc_max_watt);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charger_Voltage);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_input_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chg1_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chg2_current);
	if (ret)
		goto _out;

	battery_dir = proc_mkdir("mtk_battery_cmd", NULL);
	if (!battery_dir) {
		chr_err("[%s]: mkdir /proc/mtk_battery_cmd failed\n", __func__);
		return -ENOMEM;
	}

	proc_create_data("current_cmd", 0640, battery_dir,
			&mtk_chg_current_cmd_fops, info);
	proc_create_data("en_power_path", 0640, battery_dir,
			&mtk_chg_en_power_path_fops, info);
	proc_create_data("en_safety_timer", 0640, battery_dir,
			&mtk_chg_en_safety_timer_fops, info);

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
	struct tcpm_svid_list svid_list= {0, {0}};

	if (tcpc_dev == NULL || !g_oplus_chip) {
		chr_err("tcpc_dev is null return\n");
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
		case MTK_PD_CONNECT_NONE:
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
	mtk_pe50_notifier_call(pinfo, MTK_PE50_NOTISRC_TCP, evt, val);
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
	/*int main_cur = 0;
	int slave_cur = 0;*/
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
	/*
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
	}*/

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
	/*
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
	} else {*/
		if (g_oplus_chip->batt_volt > 4100)
			aicl_point = 4550;
		else
			aicl_point = 4500;

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
	/*
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
	} else {*/
		rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	/* } */
	goto aicl_rerun;
aicl_end:
	chg_debug("usb input max current limit aicl chg_vol=%d i[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	/*
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
	} else {*/
		rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	/* } */
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
	struct oplus_chg_chip *chip = g_oplus_chip;
	int rc = 0;

	//chip->chg_ops->input_current_write(500);
	//msleep(50);

	rc = mt6360_suspend_charger(true);
	if (rc < 0) {
		chg_debug("suspend charger fail\n");
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
		if ((g_oplus_chip->charger_type != charger_type) && g_oplus_chip->usb_psy)
			power_supply_changed(g_oplus_chip->usb_psy);
	}

	return charger_type;
}
bool oplus_mt_get_vbus_status(void)
{
	bool vbus_status = false;
	static bool pre_vbus_status = false;
	int ret = 0;

	if (!g_oplus_chip || !pinfo || !pinfo->tcpc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: g_oplus_chip not ready!\n", __func__);
		return false;
	}

	if(g_oplus_chip->vbatt_num == 2
		&& tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC){
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
	//struct charger_device *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (chip->enable_shipmode) {
		smbchg_enter_shipmode(chip);
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
#define CHARGER_25C_VOLT	2457//mv

static void oplus_get_chargeric_temp_volt(struct charger_data *pdata)
{
	int rc = 0;
	int chargeric_temp_volt = 0;

	if (!pdata) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: charger_data not ready!\n", __func__);
		return;
	}

	if (!pinfo->chargeric_temp_chan) {
		chg_err("chargeric_temp_chan NULL\n");
                return;
	}

	rc = iio_read_channel_processed(pinfo->chargeric_temp_chan, &chargeric_temp_volt);
	if ((rc < 0) || (chargeric_temp_volt < 0)) {
		chg_err("read chargeric_temp_chan volt failed, rc=%d\n", rc);
		chargeric_temp_volt = CHARGER_25C_VOLT;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	pdata->chargeric_temp_volt= chargeric_temp_volt * 1500 / 4096;
#else
	pdata->chargeric_temp_volt= chargeric_temp_volt;
#endif
	//chg_err("chargeric_temp_volt:%d\n", pdata->chargeric_temp_volt);

	return;
}

static void get_chargeric_temp(struct charger_data *pdata)
{
	int i = 0;
	int step_vol = 0;
	int temp_offset = 0;

	if (!pdata) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: charger_data not ready!\n", __func__);
		return;
	}

	for (i = ARRAY_SIZE(con_volt_20131) - 1; i >= 0; i--) {
		if (con_volt_20131[i] >= pdata->chargeric_temp_volt)
			break;
		else if (i == 0)
			break;
	}
	pdata->chargeric_temp = con_temp_20131[i];
	if (pinfo && pinfo->support_ntc_01c_precision) {
		if((0 == i) || (ARRAY_SIZE(con_volt_20131) - 1 == i)) { /* avoid array out of bounds */
			pdata->chargeric_temp = pdata->chargeric_temp * 10;
		} else {
			pdata->chargeric_temp = pdata->chargeric_temp * 10;
			step_vol = ((con_volt_20131[i] - con_volt_20131[i + 1]) * 1000)/ 10;
			temp_offset = ((con_volt_20131[i] - pdata->chargeric_temp_volt) * 1000) / step_vol;
			pdata->chargeric_temp += temp_offset;
		}
	}
	/*chg_debug("chargeric_temp:%d con_volt_20131[%d] = %d con_temp_20131[%d] = %d chargeric_temp_volt = %d step_vol = %d temp_offset = %d\n",
		pdata->chargeric_temp, i, con_volt_20131[i], i, con_temp_20131[i], pdata->chargeric_temp_volt, step_vol, temp_offset);*/

	return;
}
#endif

//====================================================================//
#ifdef OPLUS_FEATURE_CHG_BASIC
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
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->chgic_mtk.oplus_info->ccdetect_gpio)){
		//pr_err("[ccdetecct]has gpio ");
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
struct delayed_work usbtemp_recover_work;
void oplus_ccdetect_work(struct work_struct *work)
{
	int level;
	struct oplus_chg_chip *chip = g_oplus_chip;
	level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	pr_err("%s: level [%d]", __func__, level);
	if (level != 1) {
		oplus_ccdetect_enable();
        oplus_wake_up_usbtemp_thread();
	} else {
#ifdef OPLUS_FEATURE_CHG_BASIC
		if (g_oplus_chip)
			g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();
#endif
		if (oplus_get_otg_switch_status() == false)
			oplus_ccdetect_disable();
		if(g_oplus_chip->usb_status == USB_TEMP_HIGH) {
			schedule_delayed_work(&usbtemp_recover_work, 0);
		}
	}

}

void oplus_usbtemp_recover_work(struct work_struct *work)
{
	oplus_usbtemp_recover_func(g_oplus_chip);
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

#define CCDETECT_DELAY_MS	50
struct delayed_work ccdetect_work ;
irqreturn_t oplus_ccdetect_change_handler(int irq, void *data)
{
	//struct oplus_chg_chip *chip = data;

	cancel_delayed_work_sync(&ccdetect_work);
	//smblib_dbg(chg, PR_INTERRUPT, "Scheduling ccdetect work\n");
    printk(KERN_ERR "[OPLUS_CHG][%s]: Scheduling ccdetect work!\n", __func__);
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
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	return chip->otg_switch;
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

void oplus_ccdetect_irq_register(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	ret = devm_request_threaded_irq(chip->dev,  chip->chgic_mtk.oplus_info->ccdetect_irq,
			NULL, oplus_ccdetect_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "ccdetect-change", chip);
	if (ret < 0) {
		chg_err("Unable to request ccdetect-change irq: %d\n", ret);
	}
	printk(KERN_ERR "%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(chip->chgic_mtk.oplus_info->ccdetect_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: ccdetect_irq failed %d\n", ret);
	}
}

void oplus_ccdetect_enable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	/* set DRP mode */
	if (pinfo != NULL && pinfo->tcpc != NULL){
		tcpm_typec_change_role(pinfo->tcpc,TYPEC_ROLE_DRP);
		pr_err("%s: set drp", __func__);
	}

}

void oplus_ccdetect_disable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	/* set SINK mode */
	if (pinfo != NULL && pinfo->tcpc != NULL){
		tcpm_typec_change_role(pinfo->tcpc,TYPEC_ROLE_SNK);
		pr_err("%s: set sink", __func__);
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

#define USBTEMP_DEFAULT_VOLT_VALUE_MV 950
void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip)
{
	int rc, usbtemp_volt = 0;
	static int usbtemp_volt_l_pre = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	static int usbtemp_volt_r_pre = USBTEMP_DEFAULT_VOLT_VALUE_MV;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: smb5_chg not ready!\n", __func__);
		return ;
	}

	if (IS_ERR_OR_NULL(pinfo->usb_temp_v_l_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: pinfo->usb_temp_v_l_chan  is  NULL !\n", __func__);
		chip->usbtemp_volt_l = usbtemp_volt_l_pre;
		goto usbtemp_next;
	}

	rc = iio_read_channel_processed(pinfo->usb_temp_v_l_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		chip->usbtemp_volt_l = usbtemp_volt_l_pre;
		goto usbtemp_next;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	chip->usbtemp_volt_l = usbtemp_volt * 1500 / 4096;
	usbtemp_volt_l_pre = chip->usbtemp_volt_l;
#else
	chip->usbtemp_volt_l = usbtemp_volt;
	usbtemp_volt_l_pre = chip->usbtemp_volt_l;
#endif
usbtemp_next:
	usbtemp_volt = 0;
	if (IS_ERR_OR_NULL(pinfo->usb_temp_v_r_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: pinfo->usb_temp_v_r_chan  is  NULL !\n", __func__);
		chip->usbtemp_volt_r = usbtemp_volt_r_pre;
		return;
	}

	rc = iio_read_channel_processed(pinfo->usb_temp_v_r_chan, &usbtemp_volt);
	if (rc < 0) {
		chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		chip->usbtemp_volt_r = usbtemp_volt_r_pre;
		return;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	chip->usbtemp_volt_r = usbtemp_volt * 1500 / 4096;
	usbtemp_volt_r_pre = chip->usbtemp_volt_r;
#else
	chip->usbtemp_volt_r = usbtemp_volt;
	usbtemp_volt_r_pre = chip->usbtemp_volt_r;
#endif

	//chg_err("usbtemp_volt_l:%d, usbtemp_volt_r:%d\n",chip->usbtemp_volt_l, chip->usbtemp_volt_r);
	return;
}


static bool oplus_chg_get_vbus_status(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	return chip->charger_exist;
}

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
		g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();
		if (g_oplus_chip->usbtemp_check)
			wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq);
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

	rc = oplus_usb_ntc_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_usb_ntc_parse_dt fail!\n", __func__);
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

static int mt_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int ret = 0;
	switch (psp) {
	default:
		pr_err("writeable prop %d is not supported in usb\n", psp);
		ret = -EINVAL;
		break;
	}
	return 0;
}

static int mt_usb_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
		int ret = 0;
	/*struct oplus_chg_chip *chip = container_of(psy->desc, struct oplus_chg_chip, usb_psd);*/
	struct oplus_chg_chip *chip = g_oplus_chip;

	switch (psp) {
	default:
		pr_err("set prop %d is not supported in usb\n", psp);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int mt_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
		int rc = 0;
		struct oplus_chg_chip *chip = g_oplus_chip;
		switch (psp) {
		default:
			rc = oplus_usb_get_property(psy, psp, val);
	}
	return rc;
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
	mt_chg->usb_psd.set_property = mt_usb_set_property;
	mt_chg->usb_psd.property_is_writeable = mt_usb_prop_is_writeable;
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
		if(oplus_ccdetect_check_is_gpio(g_oplus_chip) == true) {
			if(gpio_get_value(g_oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio) == 0) {
				printk(KERN_ERR "[OPLUS_CHG][oplus_set_otg_switch_status]: gpio[L], should set, return\n");
				return;
			}
		}
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
		chg_err("pd_type: %d\n", pinfo->pd_type);
		if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
				pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
			return PD_ACTIVE;
		} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
			if (oplus_pps_get_chg_status() != PPS_NOT_SUPPORT) {
				return PD_PPS_ACTIVE;
			} else {
				return PD_ACTIVE;
			}
		} else {
			return PD_INACTIVE;
		}
	}

	return PD_INACTIVE;
}
EXPORT_SYMBOL(oplus_chg_get_pd_type);

int oplus_mt6360_pd_setup_forsvooc(void)
{
	int vbus_mv = VBUS_5V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int i;

	if(chip->pd_svooc){
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
	pr_err("%s get_project()=%x\n", __func__, get_project());
	if (get_project() == 0X2169E || get_project() == 0X2069F ||
		get_project() == 21711 || get_project() == 21712 || get_project() == 0X2162D) {
		chip->charger_volt = chip->chg_ops->get_charger_volt();
		if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90
				&& chip->temperature <= 420 && chip->cool_down_force_5v == false) {
			oplus_voocphy_set_pdqc_config();
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

				ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
			}
			printk(KERN_ERR "%s: pd9v svooc  default[%d %d]", __func__, em_mode, chip->batt_volt);
		}
	} else {
		if (!chip->calling_on && chip->charger_volt < 6500 && chip->soc < 90
			&& chip->temperature <= 530 && chip->cool_down_force_5v == false
			&& (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
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
		oplus_chg_suspend_charger();
		oplus_chg_config_charger_vsys_threshold(0x02);//set Vsys Skip threshold 104%
		oplus_chg_enable_burst_mode(false);
		ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		printk(KERN_ERR "%s: PD request: vbus[%d], ibus[%d], ret[%d]\n", __func__, vbus_mv, ibus_ma, ret);
		msleep(300);
		oplus_chg_unsuspend_charger();
	} else {
		if (chip->charger_volt > 7500 &&
			(chip->calling_on || chip->soc >= 90 || chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr
			|| chip->temperature > 530 || chip->cool_down_force_5v == true)) {
			vbus_mv = VBUS_5V;
			ibus_ma = IBUS_3A;

			chip->chg_ops->input_current_write(500);
			oplus_chg_suspend_charger();
			oplus_chg_config_charger_vsys_threshold(0x03);//set Vsys Skip threshold 101%
			ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
			printk(KERN_ERR "%s: PD request:vbus[%d], ibus[%d], ret[%d]\n", __func__, vbus_mv, ibus_ma, ret);
			msleep(300);
			printk(KERN_ERR "%s: charger voltage=%d", __func__, chip->charger_volt);
			oplus_chg_unsuspend_charger();
		}

			printk(KERN_ERR "%s: pd9v svooc  default[%d %d]", __func__, em_mode, chip->batt_volt);
		}
	}
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

	if (is_mtksvooc_project) {
		ret = oplus_mt6360_pd_setup_forsvooc();
	} else {
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
		if (get_project() == 0X2169E || get_project() == 0X2069F ||
				get_project() == 21711 || get_project() == 21712 || get_project() == 0X2162D) {
			chip->charger_volt = chip->chg_ops->get_charger_volt();
			if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90 && chip->temperature <= 420 && chip->cool_down_force_5v == false) {
				printk(KERN_ERR "%s: set qc to 9V", __func__);
				oplus_voocphy_set_pdqc_config();
				mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x18);
	#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
				oplus_notify_hvdcp_detect_stat();
	#endif
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
			printk(KERN_ERR "%s: qc9v svooc  default[%d %d]", __func__, em_mode, chip->batt_volt);
		} else {
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
#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
	if (mt6360_get_hvdcp_type() == POWER_SUPPLY_TYPE_USB_HVDCP)
		return CHARGER_SUBTYPE_QC;
#endif
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
	if (get_project() == 0X2169E || get_project() == 0X2069F ||
				get_project() == 21711 || get_project() == 21712 || get_project() == 0X2162D) {
		chip->charger_volt = chip->chg_ops->get_charger_volt();
		if (!chip->calling_on && !chip->camera_on && chip->charger_volt < 6500 && chip->soc < 90 && chip->temperature <= 420 && chip->cool_down_force_5v == false) {
			printk(KERN_ERR "%s: set qc to 9V", __func__);
			oplus_voocphy_set_pdqc_config();
			mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x18);
#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
			oplus_notify_hvdcp_detect_stat();
#endif
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
			printk(KERN_ERR "%s: qc9v svooc  default[%d %d]", __func__, em_mode, chip->batt_volt);
	} else {
		if (!chip->calling_on && chip->charger_volt < 6500 && chip->soc < 90
			&& chip->temperature <= 530 && chip->cool_down_force_5v == false
			&& (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
			printk(KERN_ERR "%s: set qc to 9V", __func__);
			mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x15);	/*Before request 9V, need to force 5V first.*/
			msleep(300);
			oplus_chg_suspend_charger();
			oplus_chg_config_charger_vsys_threshold(0x02);/*set Vsys Skip threshold 104%*/
			oplus_chg_enable_burst_mode(false);
			mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x18);
#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
			oplus_notify_hvdcp_detect_stat();
#endif
			msleep(300);
			oplus_chg_unsuspend_charger();
			ret = 0;
		} else {
			if (chip->charger_volt > 7500 &&
				(chip->calling_on || chip->soc >= 90
				|| chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr || chip->temperature > 530 || chip->cool_down_force_5v == true)) {
				printk(KERN_ERR "%s: set qc to 5V", __func__);
				chip->chg_ops->input_current_write(500);
				oplus_chg_suspend_charger();
				oplus_chg_config_charger_vsys_threshold(0x03);/*set Vsys Skip threshold 101%*/
				mt6360_set_register(MT6360_PMU_DPDM_CTRL, 0x1F, 0x15);
				msleep(400);
				printk(KERN_ERR "%s: charger voltage=%d", __func__, chip->charger_volt);
				oplus_chg_unsuspend_charger();

				ret = 0;
			}
		}
		printk(KERN_ERR "%s: qc9v svooc  default[%d %d]", __func__, em_mode, chip->batt_volt);
	}

	return ret;
}

int oplus_chg_set_qc_config(void)
{
	int ret = -1;
	struct oplus_chg_chip *chip = g_oplus_chip;

	pr_err("oplus_chg_set_qc_config\n");

	if (!chip) {
		pr_err("oplus_chip is null\n");
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
#ifdef CONFIG_OPLUS_HVDCP_SUPPORT
	mt6360_enable_hvdcp_detect();
#endif
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
		if (g_oplus_chip->dual_charger_support || oplus_chg_get_voocphy_support() == 1) {
			if (g_oplus_chip->camera_on == 1 && g_oplus_chip->charger_exist) {
				if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC){
					oplus_chg_set_qc_config();
				} else if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD){
					oplus_mt6360_pd_setup();
				} else {
					oplus_chg_set_flash_led_status(g_oplus_chip->camera_on);
				}
			} else if (!g_oplus_chip->charger_exist) {
				oplus_chg_set_flash_led_status(g_oplus_chip->camera_on);
			}
		}

		if (mtkhv_flashled_pinctrl.hv_flashled_support) {
			chg_err("bc1.2_done = %d camera_on %d \n", mtkhv_flashled_pinctrl.bc1_2_done, val);
			if (mtkhv_flashled_pinctrl.bc1_2_done) {
				if (val) {
					mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
					mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					chg_err("chgvin_gpio : %d", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
				} else {
					mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
					mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
					chg_err("chgvin_gpio : %d", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
					if (g_oplus_chip->charger_exist && POWER_SUPPLY_TYPE_USB_DCP == g_oplus_chip->charger_type) {
						if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
							chr_err("oplus_chg_enable_qc_detect\n");
							oplus_chg_enable_qc_detect();
							oplus_chg_set_qc_config();
						}
						if (g_oplus_chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) {
							chr_err("oplus_mt6360_pd_setup\n");
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


//====================================================================//
void oplus_set_typec_sinkonly()
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: usbtemp occur otg switch[0]\n", __func__);
		tcpm_typec_change_role(pinfo->tcpc, TYPEC_ROLE_SNK);
	}
};

bool oplus_usbtemp_condition(void)
{
	int level = -1;
	int chg_volt = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if(!chip || !pinfo || !pinfo->tcpc) {
		return false;
	}
	if(oplus_ccdetect_check_is_gpio(g_oplus_chip)){
		level = gpio_get_value(chip->chgic_mtk.oplus_info->ccdetect_gpio);
		if(level == 1
			|| tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_AUDIO
			|| tcpm_inquire_typec_attach_state(pinfo->tcpc) == TYPEC_ATTACHED_SRC){
			return false;
		}
		if (oplus_vooc_get_fastchg_ing() != true) {
			chg_volt = chip->chg_ops->get_charger_volt();
			if(chg_volt < 3000) {
				return false;
			}
		}
		return true;
	}
	return oplus_chg_get_vbus_status(chip);
}	

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
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
};
//====================================================================//
EXPORT_SYMBOL(oplus_set_typec_sinkonly);
EXPORT_SYMBOL(oplus_get_usbtemp_volt);
EXPORT_SYMBOL(oplus_usbtemp_condition);

bool oplus_otgctl_by_buckboost(void)
{
	if (!g_oplus_chip)
		return false;

	return g_oplus_chip->vbatt_num == 2;
}

void oplus_otg_enable_by_buckboost(void)
{
	if (!g_oplus_chip || !(g_oplus_chip->chg_ops->charging_disable) || !(g_oplus_chip->chg_ops->otg_enable))
		return;

	g_oplus_chip->chg_ops->charging_disable();
	g_oplus_chip->chg_ops->otg_enable();

	if (mtkhv_flashled_pinctrl.pinctrl) {
		mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_disable);
		mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		chg_err("chgvin_gpio : %d", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
	}
}

void oplus_otg_disable_by_buckboost(void)
{
	if (!g_oplus_chip || !(g_oplus_chip->chg_ops->otg_disable))
		return;

	g_oplus_chip->chg_ops->otg_disable();

	if (mtkhv_flashled_pinctrl.pinctrl) {
		mutex_lock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		pinctrl_select_state(mtkhv_flashled_pinctrl.pinctrl, mtkhv_flashled_pinctrl.chgvin_enable);
		mutex_unlock(&mtkhv_flashled_pinctrl.chgvin_mutex);
		chg_err("chgvin_gpio : %d", gpio_get_value(mtkhv_flashled_pinctrl.chgvin_gpio));
	}
}


void oplus_gauge_set_event(int event)
{
	if (NULL != pinfo) {
		charger_manager_notifier(pinfo, event);
		chr_err("[%s] notify mtkfuelgauge event = %d\n", __func__, event);
	}
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

static int proc_dump_log_show(struct seq_file *m, void *v)
{
	struct adapter_power_cap cap;
	int i;

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
		seq_puts(m, "********** PD APDO cap Dump **********\n");

		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
		for (i = 0; i < cap.nr; i++) {
			seq_printf(m,
			"%d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n", i,
			cap.max_mv[i], cap.min_mv[i], cap.ma[i],
			cap.type[i], cap.pwr_limit[i], cap.pdp);
		}
	} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
		|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
		seq_puts(m, "********** PD cap Dump **********\n");

		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
		for (i = 0; i < cap.nr; i++) {
			seq_printf(m, "%d: mV:%d,%d mA:%d type:%d\n", i,
			cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
		}
	}

	return 0;
}

static ssize_t proc_write(
	struct file *file, const char __user *buffer,
	size_t count, loff_t *f_pos)
{
	return count;
}


static int proc_dump_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_dump_log_show, NULL);
}

static const struct file_operations charger_dump_log_proc_fops = {
	.open = proc_dump_log_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.write = proc_write,
};

void charger_debug_init(void)
{
	struct proc_dir_entry *charger_dir;

#ifdef OPLUS_FEATURE_CHG_BASIC
	charger_dir = proc_mkdir("charger_mtk", NULL);
#else
	charger_dir = proc_mkdir("charger", NULL);
#endif
	if (!charger_dir) {
		chr_err("fail to mkdir /proc/charger\n");
		return;
	}

	proc_create("dump_log", 0640,
		charger_dir, &charger_dump_log_proc_fops);
}

void scd_ctrl_cmd_from_user(void *nl_data, struct sc_nl_msg_t *ret_msg)
{
	struct sc_nl_msg_t *msg;

	msg = nl_data;
	ret_msg->sc_cmd = msg->sc_cmd;

	switch (msg->sc_cmd) {

	case SC_DAEMON_CMD_PRINT_LOG:
		{
			chr_err("%s", &msg->sc_data[0]);
		}
	break;

	case SC_DAEMON_CMD_SET_DAEMON_PID:
		{
			memcpy(&pinfo->sc.g_scd_pid, &msg->sc_data[0],
				sizeof(pinfo->sc.g_scd_pid));
			chr_err("[fr] SC_DAEMON_CMD_SET_DAEMON_PID = %d(first launch)\n",
				pinfo->sc.g_scd_pid);
		}
	break;

	case SC_DAEMON_CMD_SETTING:
		{
			struct scd_cmd_param_t_1 data;

			memcpy(&data, &msg->sc_data[0],
				sizeof(struct scd_cmd_param_t_1));

			chr_debug("rcv data:%d %d %d %d %d %d %d %d %d %d %d %d %d %d Ans:%d\n",
				data.data[0],
				data.data[1],
				data.data[2],
				data.data[3],
				data.data[4],
				data.data[5],
				data.data[6],
				data.data[7],
				data.data[8],
				data.data[9],
				data.data[10],
				data.data[11],
				data.data[12],
				data.data[13],
				data.data[14]);

			pinfo->sc.solution = data.data[SC_SOLUTION];
			if (data.data[SC_SOLUTION] == SC_DISABLE)
				pinfo->sc.disable_charger = true;
			else if (data.data[SC_SOLUTION] == SC_REDUCE)
				pinfo->sc.disable_charger = false;
			else
				pinfo->sc.disable_charger = false;
		}
	break;
	default:
		chr_err("bad sc_DAEMON_CTRL_CMD_FROM_USER 0x%x\n", msg->sc_cmd);
		break;
	}

}

static void sc_nl_send_to_user(u32 pid, int seq, struct sc_nl_msg_t *reply_msg)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	/* int size=sizeof(struct fgd_nl_msg_t); */
	int size = reply_msg->sc_data_len + SCD_NL_MSG_T_HDR_LEN;
	int len = NLMSG_SPACE(size);
	void *data;
	int ret;

	reply_msg->identity = SCD_NL_MAGIC;

	if (in_interrupt())
		skb = alloc_skb(len, GFP_ATOMIC);
	else
		skb = alloc_skb(len, GFP_KERNEL);

	if (!skb)
		return;

	nlh = nlmsg_put(skb, pid, seq, 0, size, 0);
	data = NLMSG_DATA(nlh);
	memcpy(data, reply_msg, size);
	NETLINK_CB(skb).portid = 0;	/* from kernel */
	NETLINK_CB(skb).dst_group = 0;	/* unicast */

	ret = netlink_unicast(pinfo->sc.daemo_nl_sk, skb, pid, MSG_DONTWAIT);
	if (ret < 0) {
		chr_err("[Netlink] sc send failed %d\n", ret);
		return;
	}

}


static void chg_nl_data_handler(struct sk_buff *skb)
{
	u32 pid;
	kuid_t uid;
	int seq;
	void *data;
	struct nlmsghdr *nlh;
	struct sc_nl_msg_t *sc_msg, *sc_ret_msg;
	int size = 0;

	nlh = (struct nlmsghdr *)skb->data;
	pid = NETLINK_CREDS(skb)->pid;
	uid = NETLINK_CREDS(skb)->uid;
	seq = nlh->nlmsg_seq;

	data = NLMSG_DATA(nlh);

	sc_msg = (struct sc_nl_msg_t *)data;

	size = sc_msg->sc_ret_data_len + SCD_NL_MSG_T_HDR_LEN;

	if (size > (PAGE_SIZE << 1))
		sc_ret_msg = vmalloc(size);
	else {
		if (in_interrupt())
			sc_ret_msg = kmalloc(size, GFP_ATOMIC);
	else
		sc_ret_msg = kmalloc(size, GFP_KERNEL);
	}

	if (sc_ret_msg == NULL) {
		if (size > PAGE_SIZE)
			sc_ret_msg = vmalloc(size);

		if (sc_ret_msg == NULL)
			return;
	}

	memset(sc_ret_msg, 0, size);

	scd_ctrl_cmd_from_user(data, sc_ret_msg);
	sc_nl_send_to_user(pid, seq, sc_ret_msg);

	kvfree(sc_ret_msg);
}

void sc_select_charging_current(struct charger_manager *info, struct charger_data *pdata)
{
	chr_err("sck: en:%d pid:%d %d %d %d %d %d\n",
			info->sc.enable,
			info->sc.g_scd_pid,
			info->sc.pre_ibat,
			info->sc.sc_ibat,
			pdata->charging_current_limit,
			pdata->thermal_charging_current_limit,
			info->sc.solution);


	if (pinfo->sc.g_scd_pid != 0 && pinfo->sc.disable_in_this_plug == false) {
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

void sc_init(struct smartcharging *sc)
{
	sc->enable = false;
	sc->battery_size = 3000;
	sc->start_time = 0;
	sc->end_time = 80000;
	sc->current_limit = 2000;
	sc->target_percentage = 80;
	sc->left_time_for_cv = 3600;
	sc->pre_ibat = -1;
}

void sc_update(struct charger_manager *pinfo)
{
	memset(&pinfo->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	pinfo->sc.data.data[SC_VBAT] = battery_get_bat_voltage();
	pinfo->sc.data.data[SC_BAT_TMP] = battery_get_bat_temperature();
	pinfo->sc.data.data[SC_UISOC] = battery_get_uisoc();
	pinfo->sc.data.data[SC_SOC] = battery_get_soc();

	pinfo->sc.data.data[SC_ENABLE] = pinfo->sc.enable;
	pinfo->sc.data.data[SC_BAT_SIZE] = pinfo->sc.battery_size;
	pinfo->sc.data.data[SC_START_TIME] = pinfo->sc.start_time;
	pinfo->sc.data.data[SC_END_TIME] = pinfo->sc.end_time;
	pinfo->sc.data.data[SC_IBAT_LIMIT] = pinfo->sc.current_limit;
	pinfo->sc.data.data[SC_TARGET_PERCENTAGE] = pinfo->sc.target_percentage;
	pinfo->sc.data.data[SC_LEFT_TIME_FOR_CV] = pinfo->sc.left_time_for_cv;

	charger_dev_get_charging_current(pinfo->chg1_dev, &pinfo->sc.data.data[SC_IBAT_SETTING]);
	pinfo->sc.data.data[SC_IBAT_SETTING] = pinfo->sc.data.data[SC_IBAT_SETTING] / 1000;
	pinfo->sc.data.data[SC_IBAT] = battery_get_bat_current() / 10;
	charger_dev_get_ibus(pinfo->chg1_dev, &pinfo->sc.data.data[SC_IBUS]);
	if (chargerlog_level == 1)
		pinfo->sc.data.data[SC_DBGLV] = 3;
	else
		pinfo->sc.data.data[SC_DBGLV] = 7;

}

int wakeup_sc_algo_cmd(struct scd_cmd_param_t_1 *data, int subcmd, int para1)
{

	if (pinfo->sc.g_scd_pid != 0) {
		struct sc_nl_msg_t *sc_msg;
		int size = SCD_NL_MSG_T_HDR_LEN + sizeof(struct scd_cmd_param_t_1);

		if (size > (PAGE_SIZE << 1))
			sc_msg = vmalloc(size);
		else {
			if (in_interrupt())
				sc_msg = kmalloc(size, GFP_ATOMIC);
		else
			sc_msg = kmalloc(size, GFP_KERNEL);

		}

		if (sc_msg == NULL) {
			if (size > PAGE_SIZE)
				sc_msg = vmalloc(size);

			if (sc_msg == NULL)
				return -1;
		}

		sc_update(pinfo);

		chr_debug(
			"[wakeup_fg_algo] malloc size=%d pid=%d\n",
			size, pinfo->sc.g_scd_pid);
		memset(sc_msg, 0, size);
		sc_msg->sc_cmd = SC_DAEMON_CMD_NOTIFY_DAEMON;
		sc_msg->sc_subcmd = subcmd;
		sc_msg->sc_subcmd_para1 = para1;
		memcpy(sc_msg->sc_data, data, sizeof(struct scd_cmd_param_t_1));
		sc_msg->sc_data_len += sizeof(struct scd_cmd_param_t_1);
		sc_nl_send_to_user(pinfo->sc.g_scd_pid, 0, sc_msg);

		kvfree(sc_msg);

		return 0;
	}
	chr_debug("pid is NULL\n");
	return -1;
}

static ssize_t show_sc_en(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{

	chr_err(
	"[enable smartcharging] : %d\n",
	pinfo->sc.enable);

	return sprintf(buf, "%d\n", pinfo->sc.enable);
}

static ssize_t store_sc_en(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;

	if (buf != NULL && size != 0) {
		chr_err("[enable smartcharging] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[enable smartcharging] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val == 0)
			pinfo->sc.enable = false;
		else
			pinfo->sc.enable = true;

		chr_err(
			"[enable smartcharging]enable smartcharging=%d\n",
			pinfo->sc.enable);
	}
	return size;
}
static DEVICE_ATTR(enable_sc, 0664,
	show_sc_en, store_sc_en);

static ssize_t show_sc_stime(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{

	chr_err(
	"[smartcharging stime] : %d\n",
	pinfo->sc.start_time);

	return sprintf(buf, "%d\n", pinfo->sc.start_time);
}

static ssize_t store_sc_stime(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging stime] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging stime] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			pinfo->sc.start_time = val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			pinfo->sc.start_time);
	}
	return size;
}
static DEVICE_ATTR(sc_stime, 0664,
	show_sc_stime, store_sc_stime);

static ssize_t show_sc_etime(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{

	chr_err(
	"[smartcharging etime] : %d\n",
	pinfo->sc.end_time);

	return sprintf(buf, "%d\n", pinfo->sc.end_time);
}

static ssize_t store_sc_etime(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging etime] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging etime] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			pinfo->sc.end_time = val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			pinfo->sc.end_time);
	}
	return size;
}
static DEVICE_ATTR(sc_etime, 0664,
	show_sc_etime, store_sc_etime);

static ssize_t show_sc_tuisoc(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{

	chr_err(
	"[smartcharging target uisoc] : %d\n",
	pinfo->sc.target_percentage);

	return sprintf(buf, "%d\n", pinfo->sc.target_percentage);
}

static ssize_t store_sc_tuisoc(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging tuisoc] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging tuisoc] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			pinfo->sc.target_percentage = val;

		chr_err(
			"[smartcharging stime]tuisoc=%d\n",
			pinfo->sc.target_percentage);
	}
	return size;
}
static DEVICE_ATTR(sc_tuisoc, 0664,
	show_sc_tuisoc, store_sc_tuisoc);

static ssize_t show_sc_ibat_limit(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{

	chr_err(
	"[smartcharging ibat limit] : %d\n",
	pinfo->sc.current_limit);

	return sprintf(buf, "%d\n", pinfo->sc.current_limit);
}

static ssize_t store_sc_ibat_limit(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging ibat limit] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err(
				"[smartcharging ibat limit] val is %d ??\n",
				(int)val);
			val = 0;
		}

		if (val >= 0)
			pinfo->sc.current_limit = val;

		chr_err(
			"[smartcharging ibat limit]=%d\n",
			pinfo->sc.current_limit);
	}
	return size;
}
static DEVICE_ATTR(sc_ibat_limit, 0664,
	show_sc_ibat_limit, store_sc_ibat_limit);

static int mtk_charger_probe(struct platform_device *pdev)
{
	struct charger_manager *info = NULL;
	struct list_head *pos = NULL;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr = NULL;
	int ret;
	int ret_device_file;
	struct netlink_kernel_cfg cfg = {
		.input = chg_nl_data_handler,
	};
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_chg_chip *oplus_chip;
	int level = 0;
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
/* add for charger_wakelock */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	char *name = NULL;
#endif
#endif

	chr_err("%s: starts support \n", __func__);
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(*oplus_chip), GFP_KERNEL);
	if (!oplus_chip)
		return -ENOMEM;

	oplus_chip->dev = &pdev->dev;
	g_oplus_chip = oplus_chip;
	oplus_chg_parse_svooc_dt(oplus_chip);
	if (oplus_chip->vbatt_num == 1) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oplus_chip->chg_ops = &mtk6360_chg_ops;
	} else {
		chg_err("[oplus_chg_init] gauge[%d]vooc[%d]adapter[%d]\n",
				oplus_gauge_check_chip_is_null(),
				oplus_vooc_check_chip_is_null(),
				oplus_adapter_check_chip_is_null());
		if (oplus_gauge_check_chip_is_null() || oplus_vooc_check_chip_is_null() || oplus_adapter_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge || vooc || adapter null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oplus_chip->chg_ops = oplus_get_chg_ops();
		is_vooc_cfg = true;
		is_mtksvooc_project = true;
		chg_err("%s is_vooc_cfg = %d\n", __func__, is_vooc_cfg);
	}
#endif /* OPLUS_FEATURE_CHG_BASIC */

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	pinfo = info;

	platform_set_drvdata(pdev, info);
	info->pdev = pdev;
	mtk_charger_parse_dt(info, &pdev->dev);

	oplus_step_charging_parse_dt(info, &pdev->dev);

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
	mutex_init(&info->cable_out_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_power_supply_init(oplus_chip);
	oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	oplus_chip->chg_ops->hardware_init();
	oplus_chip->authenticate = oplus_gauge_get_batt_authenticate();
	chg_err("oplus_chg_init!\n");
	oplus_chg_init(oplus_chip);

	if (get_boot_mode() != KERNEL_POWER_OFF_CHARGING_BOOT) {
		oplus_tbatt_power_off_task_init(oplus_chip);
	}

	if(oplus_chg_get_voocphy_support()) {
		is_mtksvooc_project = true;
		chg_err("support voocphy, is_mtksvooc_project is true!\n");
	}
#endif
	atomic_set(&info->enable_kpoc_shdn, 1);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	wakeup_source_init(&info->charger_wakelock, "charger suspend wakelock");
#else
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s",
		"charger suspend wakelock");
	info->charger_wakelock = wakeup_source_register(NULL, name);
#endif
	spin_lock_init(&info->slock);

	/* init thread */
	init_waitqueue_head(&info->wait_que);
	info->polling_interval = CHARGING_INTERVAL;
	info->enable_dynamic_cv = true;

	info->chg1_data.thermal_charging_current_limit = -1;
	info->chg1_data.thermal_input_current_limit = -1;
	info->chg1_data.input_current_limit_by_aicl = -1;
	info->chg2_data.thermal_charging_current_limit = -1;
	info->chg2_data.thermal_input_current_limit = -1;
	info->dvchg1_data.thermal_input_current_limit = -1;
	info->dvchg2_data.thermal_input_current_limit = -1;

	info->sw_jeita.error_recovery_flag = true;

	mtk_charger_init_timer(info);

	kthread_run(charger_routine_thread, info, "charger_thread");

	if (info->chg1_dev != NULL && info->do_event != NULL) {
		info->chg1_nb.notifier_call = info->do_event;
		register_charger_device_notifier(info->chg1_dev,
						&info->chg1_nb);
		charger_dev_set_drvdata(info->chg1_dev, info);
	}

	info->psy_nb.notifier_call = charger_psy_event;
	power_supply_reg_notifier(&info->psy_nb);

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

	pinfo->chargeric_temp_chan = iio_channel_get(oplus_chip->dev, "auxadc3-chargeric_temp");
	if (IS_ERR(pinfo->chargeric_temp_chan)) {
		chg_err("Couldn't get chargeric_temp_chan...\n");
		pinfo->chargeric_temp_chan = NULL;
	}

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

	if (oplus_ccdetect_support_check() == true){
		INIT_DELAYED_WORK(&ccdetect_work,oplus_ccdetect_work);
		INIT_DELAYED_WORK(&usbtemp_recover_work,oplus_usbtemp_recover_work);
		oplus_ccdetect_irq_register(oplus_chip);
		level = gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio);
		usleep_range(2000, 2100);
		if (level != gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio)) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(10000, 11000);
			level = gpio_get_value(oplus_chip->chgic_mtk.oplus_info->ccdetect_gpio);
		}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		if ((level <= 0) && (oplus_is_meta_mode() == false)) {
#else
		if ((level <= 0) && (is_meta_mode() == false)) {
#endif
			schedule_delayed_work(&ccdetect_work, msecs_to_jiffies(6000));
		}
		printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio ..level[%d]  \n", __func__, level);
	}

	oplus_chip->con_volt = con_volt_20131;
	oplus_chip->con_temp = con_temp_20131;
	oplus_chip->len_array = ARRAY_SIZE(con_temp_20131);
	if (usb_ntc_support == true) {
		oplus_chip->con_volt = con_volt_20682;
		oplus_chip->con_temp = con_temp_20682;
		oplus_chip->len_array = ARRAY_SIZE(con_temp_20682);
	}

	if (oplus_usbtemp_check_is_support() == true)
		oplus_usbtemp_thread_init();

	if (is_vooc_project() == true)
		oplus_mt6360_suspend_charger();
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_configfs_init(oplus_chip);
#endif

	if (mtk_pe_init(info) < 0)
		info->enable_pe_plus = false;

	if (mtk_pe20_init(info) < 0)
		info->enable_pe_2 = false;

	if (mtk_pe40_init(info) == false)
		info->enable_pe_4 = false;

	if (mtk_pe50_init(info) < 0)
		info->enable_pe_5 = false;

	mtk_pdc_init(info);
	charger_ftm_init();
	mtk_charger_get_atm_mode(info);
	sw_jeita_state_machine_init(info);

#ifdef CONFIG_MTK_CHARGER_UNLIMITED
	info->usb_unlimited = true;
	info->enable_sw_safety_timer = false;
	charger_dev_enable_safety_timer(info->chg1_dev, false);
#endif

	info->sc.daemo_nl_sk = netlink_kernel_create(&init_net, NETLINK_CHG, &cfg);

	if (info->sc.daemo_nl_sk == NULL)
		chr_err("sc netlink_kernel_create error id:%d\n", NETLINK_CHG);
	else
		chr_err("sc_netlink_kernel_create success id:%d\n", NETLINK_CHG);
	sc_init(&info->sc);

	charger_debug_init();

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

	/* sysfs node */
	ret_device_file = device_create_file(&(pdev->dev),
		&dev_attr_enable_sc);
	ret_device_file = device_create_file(&(pdev->dev),
		&dev_attr_sc_stime);
	ret_device_file = device_create_file(&(pdev->dev),
		&dev_attr_sc_etime);
	ret_device_file = device_create_file(&(pdev->dev),
		&dev_attr_sc_tuisoc);
	ret_device_file = device_create_file(&(pdev->dev),
		&dev_attr_sc_ibat_limit);
	info->chg1_consumer =
		charger_manager_get_by_name(&pdev->dev, "charger_port1");
	info->init_done = true;
	_wake_up_charger(info);
#ifdef OPLUS_FEATURE_CHG_BASIC
	INIT_DELAYED_WORK(&pinfo->step_charging_work, mt6360_step_charging_work);
	chg_err("oplus_chg_wake_update_work!\n");
	oplus_chg_wake_update_work();
#endif
	return 0;
}

static int mtk_charger_remove(struct platform_device *dev)
{
	struct charger_manager *info = platform_get_drvdata(dev);

	mtk_pe50_deinit(info);
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


MODULE_AUTHOR("wy.chuang <wy.chuang@mediatek.com>");
MODULE_DESCRIPTION("MTK Charger Driver");
MODULE_LICENSE("GPL");
