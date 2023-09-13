/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
/*#include <linux/wakelock.h>*/
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/alarmtimer.h>
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
#include <tcpm.h>
#include <mtk_gauge_time_service.h>

#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_configfs.h"
#include "../../../power/supply/mediatek/charger/mtk_charger_intf.h"
#include "../../../power/supply/mediatek/charger/mtk_charger_init.h"
#include "op_charge.h"
#include "../../../misc/mediatek/typec/tcpc/inc/tcpci.h"
#include "../oplus_chg_track.h"
#include <linux/iio/consumer.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
extern void musb_ctrl_host(bool on_off);
#endif
extern int charger_ic_flag;

extern bool oplus_chg_check_chip_is_null(void);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
extern bool oplus_tchg_01c_precision(void);
#endif
/************ kpoc_charger *******************/
extern int oplus_chg_get_ui_soc(void);
extern int oplus_chg_get_notify_flag(void);
extern int oplus_chg_show_vooc_logo_ornot(void);
extern bool pmic_chrdet_status(void);
extern int oplus_get_prop_status(void);
struct oplus_chg_chip *g_oplus_chip = NULL;
int oplus_usb_switch_gpio_gpio_init(void);
static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_ship_gpio_init(struct oplus_chg_chip *chip);
void smbchg_enter_shipmode(struct oplus_chg_chip *chip);
bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_shortc_gpio_init(struct oplus_chg_chip *chip);
extern struct oplus_chg_operations  bq24190_chg_ops;
extern struct oplus_chg_operations  bq25890h_chg_ops;
extern struct oplus_chg_operations  bq25601d_chg_ops;

extern struct oplus_chg_operations  oplus_chg_rt9471_ops;
extern struct oplus_chg_operations  oplus_chg_rt9467_ops;
extern struct oplus_chg_operations  oplus_chg_bq2589x_ops;
extern struct oplus_chg_operations  oplus_chg_bq2591x_ops;
extern struct oplus_chg_operations  oplus_chg_sy6974_ops;

extern struct oplus_chg_operations  oplus_chg_sgm41512_ops;
struct iio_channel *usb_chan1; /*usb_temp_auxadc_channel 1*/
struct iio_channel *usb_chan2; /*usb_temp_auxadc_channel 2*/
static int usbtemp_log_control = 0;
static int usb_debug_temp = 65535;
static int charger_ic__det_flag = 0;
int ap_temp_debug = 65535;
module_param(usb_debug_temp, int, 0644);
module_param(ap_temp_debug, int, 0644);
module_param(usbtemp_log_control, int, 0644);
#define USBTEMP_LOG_PRINT(fmt, args...)					\
do {								\
	if (usbtemp_log_control) {	\
		pr_debug(fmt, ##args);				\
	}							\
} while (0)

static struct charger_manager *pinfo;
static struct list_head consumer_head = LIST_HEAD_INIT(consumer_head);
static DEFINE_MUTEX(consumer_mutex);

extern struct oplus_chg_operations * oplus_get_chg_ops(void);

extern bool oplus_gauge_ic_chip_is_null(void);
extern bool oplus_vooc_check_chip_is_null(void);
extern bool oplus_adapter_check_chip_is_null(void);
int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip);
extern bool is_kthread_get_adc(void);
extern int get_charger_ntc_volt(void);
extern int get_bat_con_ntc_volt(void);

#define CHG_BQ25601D 2
#define CHG_BQ25890H 1
#define CHG_BQ24190  0
#define CHG_SMB1351  3
#define CHG_MT6370	4

extern int oplus_get_rtc_ui_soc(void);
extern int oplus_set_rtc_ui_soc(int value);
static struct task_struct *oplus_usbtemp_kthread;
static DECLARE_WAIT_QUEUE_HEAD(oplus_usbtemp_wq);
void oplus_set_otg_switch_status(bool value);
void oplus_wake_up_usbtemp_thread(void);
extern void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip);
/*====================================================================*/

#define USB_TEMP_HIGH		0x01 /*bit0*/
#define USB_WATER_DETECT	0x02 /*bit1*/
#define USB_RESERVE2		0x04 /*bit2*/
#define USB_RESERVE3		0x08 /*bit3*/
#define USB_RESERVE4		0x10 /*bit4*/
#define USB_DONOT_USE		0x80000000 /*bit31*/
#define DEFAULT_PULL_UP_R0      100000 /*pull up 100K*/
#define PULL_UP_R0_390K         390000 /*Pull up 390K*/
static int usb_status = 0;

#ifndef CONFIG_HIGH_TEMP_VERSION
static void oplus_set_usb_status(int status)
{
	usb_status = usb_status | status;
}
#endif

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

void oplus_gauge_set_event(int event)
{
	if (NULL != pinfo) {
		charger_manager_notifier(pinfo, event);
		chr_err("[%s] notify mtkfuelgauge event = %d\n", __func__, event);
	}
}

/*add for usb port over temp protection*/

static void oplus_clear_usb_status(int status)
{
	if( g_oplus_chip->usb_status == USB_TEMP_HIGH) {
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

void charger_ic_enable_ship_mode(struct oplus_chg_chip *chip)
{
	if (!chip->is_double_charger_support) {
		if (chip->chg_ops->enable_shipmode){
			chip->chg_ops->enable_shipmode(true);
		}
		} else {
		if (chip->sub_chg_ops->enable_shipmode){
			chip->sub_chg_ops->enable_shipmode(true);
		}
		if (chip->chg_ops->enable_shipmode){
			chip->chg_ops->enable_shipmode(true);
		}
	}
}

/* ==================================================================== */


int oplus_battery_meter_get_battery_voltage(void)
{
	return 4000;
}

bool is_vooc_project(void)
{
	return false;
}

static bool is_even_c_prj = false;
static bool is_even_c_project(void)
{
	return is_even_c_prj;
}

static int oplus_even_c_prj_parse_dt(struct oplus_chg_chip *chip)
{
	struct device_node *node = NULL;

	if (chip != NULL)
		node = chip->dev->of_node;

	if (chip == NULL || node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	is_even_c_prj = of_property_read_bool(node, "qcom,is_even_c_prj");
	if (is_even_c_prj == false) {
		chg_err("not even-c project!\n");
	}

	return 0;
}

bool meter_fg_30_get_battery_authenticate(void);
int charger_ic_flag = 1;
int oplus_which_charger_ic(void)
{
    return charger_ic_flag;
}

int get_charger_ic_det(struct oplus_chg_chip *chip)
{
	int count = 0;
	int n = charger_ic__det_flag;

	if (!chip) {
		return charger_ic__det_flag;
	}

	while (n) {
		++ count;
		n = (n - 1) & n;
	}

	chip->is_double_charger_support = count>1 ? true:false;
	chg_err("charger_ic__det_flag:%d\n", charger_ic__det_flag);
	return charger_ic__det_flag;
}

void set_charger_ic(int sel)
{
	charger_ic__det_flag |= 1<<sel;
}
EXPORT_SYMBOL(set_charger_ic);

extern enum charger_type mt_charger_type_detection(void);
extern enum charger_type mt_charger_type_detection_bq25890h(void);
extern enum charger_type mt_charger_type_detection_bq25601d(void);
extern bool upmu_is_chr_det(void);
extern enum charger_type g_chr_type;

static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

int oplus_ship_gpio_init(struct oplus_chg_chip *chip)
{
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
void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
	int i = 0;
	chg_err("enter smbchg_enter_shipmode\n");

	if (oplus_ship_check_is_gpio(chip) == true) {
		chg_err("select gpio control\n");
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active) && !IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
			for (i = 0; i < PWM_COUNT; i++) {
				/*gpio_direction_output(chip->normalchg_gpio.ship_gpio, 1);*/
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
				mdelay(3);
				/*gpio_direction_output(chip->normalchg_gpio.ship_gpio, 0);*/
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
				mdelay(3);
			}
		}
		chg_err("power off after 15s\n");
	}
}
void enter_ship_mode_function(struct oplus_chg_chip *chip)
{
	if(chip != NULL){
		if (chip->enable_shipmode) {
			printk("enter_ship_mode_function\n");
			smbchg_enter_shipmode(chip);
			charger_ic_enable_ship_mode(chip);
		}
	}
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
int set_chr_enable_otg(unsigned int enable)
{
	if (!g_oplus_chip) {
		printk(KERN_ERR "[oplus_CHG][%s]: otg [%s] failed.\n", __func__, enable?"Enable":"Disable");
		return -1;
}

	if (enable) {
			g_oplus_chip->chg_ops->otg_enable();
	} else {
			g_oplus_chip->chg_ops->otg_disable();
	}
	return 0;
}
#else
int set_chr_enable_otg(unsigned int enable)
{
	return 0;
}
#endif
int mtk_chr_is_charger_exist(unsigned char *exist)
{
	if (mt_get_charger_type() == CHARGER_UNKNOWN)
		*exist = 0;
	else
		*exist = 1;
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
void oplus_set_otg_switch_status(bool value)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[oplus_CHG][%s]: otg switch[%d]\n", __func__, value);
		tcpm_typec_change_role_postpone(pinfo->tcpc, value ? TYPEC_ROLE_TRY_SNK : TYPEC_ROLE_SNK, true);
	} else {
		printk(KERN_ERR "[oplus_CHG][%s]: tcpc device fail\n", __func__);
	}
}
#else
void oplus_set_otg_switch_status(bool value)
{
	if (pinfo != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: otg switch[%d]\n", __func__, value);
		musb_ctrl_host(value);
	}
}
#endif
EXPORT_SYMBOL(oplus_set_otg_switch_status);

bool mtk_is_TA_support_pd_pps(struct charger_manager *pinfo)
{
	if (pinfo->enable_pe_4 == false && pinfo->enable_pe_5 == false)
		return false;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return true;
	return false;
}


/*=============== fix me==================*/
int chargerlog_level = CHRLOG_ERROR_LEVEL;

int chr_get_debug_level(void)
{
	return chargerlog_level;
}

#ifdef MTK_CHARGER_EXP
#include <linux/string.h>

#define LOG_LENGTH 500
#define CHARGER_LOG_LENGTH 1000
char chargerlog[CHARGER_LOG_LENGTH];
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
		memset(chargerlog, 0, CHARGER_LOG_LENGTH);
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
	memset(chargerlog, 0, CHARGER_LOG_LENGTH);
}
#endif /*MTK_CHARGER_EXP*/

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
#endif /*OPLUS_FEATURE_CHG_BASIC*/
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
bool oplus_tchg_01c_precision(void)
{
	if (!pinfo) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: charger_data not ready!\n", __func__);
		return false;
	}

	return pinfo->support_ntc_01c_precision;
}
EXPORT_SYMBOL(oplus_tchg_01c_precision);
#endif

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
#ifdef OPLUS_FEATURE_CHG_BASIC
	return -EBUSY;
#endif

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
	return -EBUSY;
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
	return -EBUSY;
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

#ifdef OPLUS_FEATURE_CHG_BASIC
	return -EBUSY;
#endif

	if (info != NULL) {
		struct charger_data *pdata;

		if (!upmu_get_rgs_chrdet()) {
			pr_debug("[%s] No cable in, skip it\n", __func__);
			*tchg_min = -127;
			*tchg_max = -127;
			return -EINVAL;
		}

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

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
		struct charger_data *pdata;

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

bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio))
	{
		return true;
	}
	return false;
}

int oplus_shortc_gpio_init(struct oplus_chg_chip *chip)
{
	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	chip->normalchg_gpio.shortc_active =
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
			"shortc_active");

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
		chg_err("get shortc_active fail\n");
		return -EINVAL;
        }

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
		chip->normalchg_gpio.shortc_active);
	return 0;
}
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	if(oplus_shortc_check_is_gpio(g_oplus_chip) == true) {
		shortc_hw_status = !!(gpio_get_value(g_oplus_chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else
bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	return shortc_hw_status;
}
#endif

int usbtemp_channel_init(struct device *dev)
{
	int ret = 0;
	usb_chan1 = iio_channel_get(dev, "usbtemp-ch3");
	if (IS_ERR_OR_NULL(usb_chan1)) {
		chg_err("usb_chan1 init fial,err = %d",PTR_ERR(usb_chan1));
		ret = -1;
	}

	usb_chan2 = iio_channel_get(dev, "usbtemp-ch4");
	if(IS_ERR_OR_NULL(usb_chan2)){
		chg_err("usb_chan2 init fial,err = %d",PTR_ERR(usb_chan2));
		ret = -1;
	}

	return ret;
}

static void set_usbswitch_to_rxtx(struct oplus_chg_chip *chip)
{
	int ret = 0;
	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output2);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return ;
	}
	chg_err("set_usbswitch_to_rxtx \n");
}
static void set_usbswitch_to_dpdm(struct oplus_chg_chip *chip)
{
	int ret = 0;
	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output1);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return ;
	}
	chg_err("set_usbswitch_to_dpdm \n");
}
static bool is_support_chargerid_check(void)
{

#ifdef CONFIG_OPLUS_CHECK_CHARGERID_VOLT
	return true;
#else
	return false;
#endif

}
int mt_get_chargerid_volt (void)
{
	int chargerid_volt = 0;
	if(is_support_chargerid_check() == true)
	{
		chg_debug("chargerid_volt = %d \n",
					   chargerid_volt);
	}
		else
		{
		chg_debug("is_support_chargerid_check = false !\n");
		return 0;
	}
	return chargerid_volt;
		}


void mt_set_chargerid_switch_val(int value)
{
	chg_debug("set_value= %d\n",value);
	if(NULL == g_oplus_chip)
		return;
	if(is_support_chargerid_check() == false)
		return;
	if(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return;
	}
	if(IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.pinctrl)
		|| IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output1)
		|| IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("pinctrl null, return\n");
		return;
	}
	if(1 == value){
			set_usbswitch_to_rxtx(g_oplus_chip);
	}else if(0 == value){
		set_usbswitch_to_dpdm(g_oplus_chip);
	}else{
		/* do nothing */
	}
	chg_debug("get_val:%d\n",gpio_get_value(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio));
}

int mt_get_chargerid_switch_val(void)
{
	int gpio_status = 0;
	if(NULL == g_oplus_chip)
		return 0;
	if(is_support_chargerid_check() == false)
		return 0;
	gpio_status = gpio_get_value(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio);

	chg_debug("mt_get_chargerid_switch_val:%d\n",gpio_status);
	return gpio_status;
}


int oplus_usb_switch_gpio_gpio_init(void)
{
	g_oplus_chip->normalchg_gpio.pinctrl = devm_pinctrl_get(g_oplus_chip->dev);
	if (IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.chargerid_switch_gpio pinctrl falil\n");
		return -EINVAL;
	}
	g_oplus_chip->normalchg_gpio.charger_gpio_as_output1 = pinctrl_lookup_state(g_oplus_chip->normalchg_gpio.pinctrl,
								"charger_gpio_as_output_low");
	if (IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output1)) {
		chg_err("get charger_gpio_as_output_low fail\n");
		return -EINVAL;
	}
	g_oplus_chip->normalchg_gpio.charger_gpio_as_output2 = pinctrl_lookup_state(g_oplus_chip->normalchg_gpio.pinctrl,
								"charger_gpio_as_output_high");
	if (IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("get charger_gpio_as_output_high fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(g_oplus_chip->normalchg_gpio.pinctrl,g_oplus_chip->normalchg_gpio.charger_gpio_as_output1);
	return 0;
}


int charger_pretype_get(void)
{
	int chg_type = STANDARD_HOST;
	/* chg_type = hw_charging_get_charger_type(); */
	return chg_type;
}


bool oplus_pmic_check_chip_is_null(void)
{
	if (!is_vooc_project()) {
		return true;
	} else {
		return false;
	}
}

/*====================================================================*/
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

#define USB_20C		20 /*degreeC*/
#define USB_30C		30 /*degreeC*/
#define USB_50C		50
#define USB_57C		57
#define USB_100C	100
#define USB_25C_VOLT	1192	/*1192mv*/
#define USB_50C_VOLT	450
#define USB_55C_VOLT	448
#define USB_60C_VOLT	327
#define VBUS_VOLT_THRESHOLD	3000 /*3V*/
#define RETRY_CNT_DELAY		5 /*ms*/
#define MIN_MONITOR_INTERVAL	50 /*50ms*/
#define VBUS_MONITOR_INTERVAL	3000 /*3s*/

#define USB_PORT_PULL_UP_R      390000 /*390K*/
#define USB_PORT_PULL_UP_R_100     100000 /*100k even-c*/
#define USB_PORT_PULL_UP_VOLT   1800  /*1.8V*/
#define USB_NTC_TABLE_SIZE 74
#define USB_NTC_PULLUP_NOTFROM_DTS 0
static int usb_port_ntc_pullup = 0;

struct USB_PORT_TEMPERATURE {
	signed int Temp;
	signed int TemperatureR;
};

struct USB_PORT_TEMPERATURE Usb_Port_Temperature_Table[USB_NTC_TABLE_SIZE] = {
/* NCP15WF104F03RC(100K) */
	{-40, 4397119},
	{-35, 3088599},
	{-30, 2197225},
	{-25, 1581881},
	{-20, 1151037},
	{-15, 846579},
	{-10, 628988},
	{-5, 471632},
	{0, 357012},
	{5, 272500},
	{10, 209710},
	{15, 162651},
	{20, 127080},
	{25, 100000},		/* 100K */
	{30, 79222},
	{35, 63167},
	{40, 50677},
	{41, 48528},
	{42, 46482},
	{43, 44533},
	{44, 42675},
	{45, 40904},
	{46, 39213},
	{47, 37601},
	{48, 36063},
	{49, 34595},
	{50, 33195},
	{51, 31859},
	{52, 30584},
	{53, 29366},
	{54, 28203},
	{55, 27091},
	{56, 26028},
	{57, 25013},
	{58, 24042},
	{59, 23113},
	{60, 22224},
	{61, 21374},
	{62, 20560},
	{63, 19782},
	{64, 19036},
	{65, 18322},
	{66, 17640},
	{67, 16986},
	{68, 16360},
	{69, 15759},
	{70, 15184},
	{71, 14631},
	{72, 14100},
	{73, 13591},
	{74, 13103},
	{75, 12635},
	{76, 12187},
	{77, 11756},
	{78, 11343},
	{79, 10946},
	{80, 10565},
	{81, 10199},
	{82,  9847},
	{83,  9509},
	{84,  9184},
	{85,  8872},
	{86,  8572},
	{87,  8283},
	{88,  8005},
	{89,  7738},
	{90,  7481},
	{95,  6337},
	{100, 5384},
	{105, 4594},
	{110, 3934},
	{115, 3380},
	{120, 2916},
	{125, 2522}
};

static void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip)
{
	int usbtemp_volt = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}
	iio_read_channel_processed(usb_chan1, &usbtemp_volt);
	if (usbtemp_volt <= 0) {
		usbtemp_volt = USB_25C_VOLT;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	chip->usbtemp_volt_r = usbtemp_volt * 1500 / 4096;
#else
	chip->usbtemp_volt_r = usbtemp_volt;
#endif

	iio_read_channel_processed(usb_chan2, &usbtemp_volt);
	if (usbtemp_volt <= 0) {
		usbtemp_volt = USB_25C_VOLT;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	chip->usbtemp_volt_l = usbtemp_volt * 1500 / 4096;
#else
	chip->usbtemp_volt_l = usbtemp_volt;
#endif

	/* chg_err("usbtemp_volt: %d, %d\n", chip->usbtemp_volt_r, chip->usbtemp_volt_l); */

	return;
}

int usb_port_volt_to_temp(int volt)
{
	int i = 0;
	int usb_r = 0;
	int RES1 = 0, RES2 = 0;
	int Usb_temp_Value = 25, TMP1 = 0, TMP2 = 0;

	if(usb_debug_temp != 65535)
		return usb_debug_temp;

	if (usb_port_ntc_pullup == 0) {
		if (is_even_c_project())
			usb_r = volt * USB_PORT_PULL_UP_R_100 / (USB_PORT_PULL_UP_VOLT - volt);
		else
			usb_r = volt * USB_PORT_PULL_UP_R / (USB_PORT_PULL_UP_VOLT - volt);
	} else {
		usb_r = volt * usb_port_ntc_pullup / (USB_PORT_PULL_UP_VOLT - volt);
	}

	USBTEMP_LOG_PRINT("[UsbTemp] NTC_R = %d\n",usb_r);
	if (usb_r >= Usb_Port_Temperature_Table[0].TemperatureR) {
		Usb_temp_Value = -40;
	} else if (usb_r <= Usb_Port_Temperature_Table[USB_NTC_TABLE_SIZE - 1].TemperatureR) {
		Usb_temp_Value = 125;
	} else {
		RES1 = Usb_Port_Temperature_Table[0].TemperatureR;
		TMP1 = Usb_Port_Temperature_Table[0].Temp;

		for (i = 0; i < USB_NTC_TABLE_SIZE; i++) {
			if (usb_r >= Usb_Port_Temperature_Table[i].TemperatureR) {
				RES2 = Usb_Port_Temperature_Table[i].TemperatureR;
				TMP2 = Usb_Port_Temperature_Table[i].Temp;
				break;
			}
			{	/* hidden else */
				RES1 = Usb_Port_Temperature_Table[i].TemperatureR;
				TMP1 = Usb_Port_Temperature_Table[i].Temp;
			}
		}

		Usb_temp_Value = (((usb_r - RES2) * TMP1) +
			((RES1 - usb_r) * TMP2)) / (RES1 - RES2);
	}

	USBTEMP_LOG_PRINT(
		"[%s] %d %d %d %d %d %d\n",
		__func__,
		RES1, RES2, usb_r, TMP1,
		TMP2, Usb_temp_Value);
	if (Usb_temp_Value >= 50 && Usb_temp_Value <= 55)
		Usb_temp_Value = Usb_temp_Value  - ((20/5*(Usb_temp_Value-50))+5)/10;
	else if (Usb_temp_Value > 55 && Usb_temp_Value <= 60)
		Usb_temp_Value -= 2;
	else if (Usb_temp_Value > 60 && Usb_temp_Value <= 65)
		Usb_temp_Value = Usb_temp_Value  - (20/5*(5-(Usb_temp_Value-60))+5)/10;
	else
		USBTEMP_LOG_PRINT("usbtemp not between 50-65 \n");

	return Usb_temp_Value;
}

static void get_usb_temp(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	chip->usb_temp_r = usb_port_volt_to_temp(chip->usbtemp_volt_r);
	chip->usb_temp_l = usb_port_volt_to_temp(chip->usbtemp_volt_l);
	USBTEMP_LOG_PRINT("usbtemp: %d, %d\n", chip->usb_temp_r, chip->usb_temp_l);
	return;
}

static bool oplus_chg_get_vbus_status(struct oplus_chg_chip *chip)
{
    int charger_type;
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	charger_type = chip->chg_ops->get_charger_type();
	if(charger_type)
		return true;
	else
		return false;
}

#define RETRY_COUNT		3
static int oplus_usbtemp_monitor_main(void *data)
{
	int i = 0;
	int delay = 0;
	int vbus_volt = 0;
	int count_r = 0;
	int count_l = 0;
	int time_count = 0;
	static int count = 0;
	static int last_usb_temp_r = 25;
	static int last_usb_temp_l = 25;
	static bool init_flag = true;
	static int retry_usb_temp_r = 25;
	static int retry_usb_temp_l = 25;
	struct oplus_chg_chip *chip = g_oplus_chip;

	while (!kthread_should_stop()) {
		set_current_state(TASK_RUNNING);

		if (init_flag == true) {
			init_flag = false;
			if (oplus_chg_get_vbus_status(chip) == false
					&& oplus_chg_get_otg_online() == false) {
				delay = MIN_MONITOR_INTERVAL * 20;
				goto check_again;
			}
		}

		oplus_get_usbtemp_volt(chip);
		get_usb_temp(chip);

		if (chip->usb_temp_r < USB_50C && chip->usb_temp_l < USB_50C)/*get vbus when usbtemp < 50C*/
			vbus_volt = battery_get_vbus();
		else
			vbus_volt = 0;

		delay = MIN_MONITOR_INTERVAL;
		time_count = 30;

		if (chip->usb_temp_r < USB_50C && chip->usb_temp_l < USB_50C && vbus_volt < VBUS_VOLT_THRESHOLD)
			delay = VBUS_MONITOR_INTERVAL;

		if ((chip->usb_temp_r >= USB_57C)
				|| (chip->usb_temp_l >= USB_57C)) {
			for (i = 0; i < RETRY_COUNT; i++) {
				mdelay(RETRY_CNT_DELAY);
				oplus_get_usbtemp_volt(chip);
				get_usb_temp(chip);
				if (((chip->usb_temp_r >= USB_57C)  && (chip->usb_temp_r <= USB_100C))&&(abs(retry_usb_temp_r-chip->usb_temp_r)<5))
					count_r++;
				if (((chip->usb_temp_l >= USB_57C)  && (chip->usb_temp_l <= USB_100C))&&(abs(retry_usb_temp_l-chip->usb_temp_l)<5))
					count_l++;

				retry_usb_temp_r = chip->usb_temp_r;
				retry_usb_temp_l = chip->usb_temp_l;
				pr_debug("high temp: %d,%d,%d,%d,%d,%d\n", chip->usb_temp_r, chip->usb_temp_l,retry_usb_temp_r,retry_usb_temp_l,count_r,count_l);
			}

			if (count_r >= RETRY_COUNT || count_l >= RETRY_COUNT) {
				if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
					chip->dischg_flag = true;
					chg_err("dischg enable...usb_temp[%d,%d], usb_volt[%d,%d]\n",
							chip->usb_temp_r, chip->usb_temp_l, chip->usbtemp_volt_r, chip->usbtemp_volt_l);
#ifndef CONFIG_HIGH_TEMP_VERSION
					oplus_set_usb_status(USB_TEMP_HIGH);
					if (oplus_chg_get_otg_online() == true) {
						oplus_set_otg_switch_status(false);
						for(i = 0; i < 100 ; i++){
							if(battery_get_vbus() < 2500)
								break;
							usleep_range(10000, 11000);
						}
					}
					if (oplus_vooc_get_fastchg_started() == true) {
						oplus_chg_set_chargerid_switch_val(0);
						oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
						oplus_vooc_reset_mcu();
						/*msleep(20);*/ /*wait for turn-off fastchg MOS*/
					}
					chip->chg_ops->charging_disable();
					if(chip->is_double_charger_support)
						chip->sub_chg_ops->charging_disable();
					usleep_range(10000, 11000);
					if(chip->chg_ops->oplus_chg_set_hz_mode != NULL)
						chip->chg_ops->oplus_chg_set_hz_mode(true);

					chip->chg_ops->charger_suspend();
					if(chip->is_double_charger_support)
						chip->sub_chg_ops->charger_suspend();
					usleep_range(10000, 11000);
					pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_enable);
					alarm_start_relative(&chip->usbtemp_alarm_timer, ktime_set(15300, 0));
#endif /*CONFIG_HIGH_TEMP_VERSION*/
				}
			}

			count_r = 0;
			count_l = 0;
			count = 0;
		} else if (((chip->usb_temp_r - chip->temperature/10) > 10)
				|| ((chip->usb_temp_l - chip->temperature/10) > 10)) {
			if (count <= time_count) {
				if (count == 0) {
					last_usb_temp_r = chip->usb_temp_r;
					last_usb_temp_l = chip->usb_temp_l;
				}

				if (((chip->usb_temp_r - last_usb_temp_r) >= 3 && chip->usb_temp_r >= USB_30C && chip->usb_temp_r <= USB_100C)
						|| ((chip->usb_temp_l - last_usb_temp_l) >= 3 &&  chip->usb_temp_l >= USB_30C && chip->usb_temp_l <= USB_100C)) {
					for (i = 0; i < RETRY_COUNT; i++) {
						mdelay(RETRY_CNT_DELAY);
						oplus_get_usbtemp_volt(chip);
						get_usb_temp(chip);
						if ((chip->usb_temp_r - last_usb_temp_r) >= 3)
							count_r++;
						if ((chip->usb_temp_l - last_usb_temp_l) >= 3)
							count_l++;
					}

					if (count_r >= RETRY_COUNT || count_l >= RETRY_COUNT) {
						if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
							chip->dischg_flag = true;

							chg_err("dischg enable...current_usb_temp[%d,%d], last_usb_temp[%d,%d], count[%d]\n",
									chip->usb_temp_r, chip->usb_temp_l, last_usb_temp_r, last_usb_temp_l, count);
#ifndef CONFIG_HIGH_TEMP_VERSION
							oplus_set_usb_status(USB_TEMP_HIGH);
							if (oplus_chg_get_otg_online() == true) {
								oplus_set_otg_switch_status(false);
								for(i = 0; i < 100 ; i++){
									if(battery_get_vbus() < 2500)
										break;
									usleep_range(10000, 11000);
								}
							}
							if (oplus_vooc_get_fastchg_started() == true) {
								oplus_chg_set_chargerid_switch_val(0);
								oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
								oplus_vooc_reset_mcu();
								/*msleep(20);*/ /*wait for turn-off fastchg MOS*/
							}
							chip->chg_ops->charging_disable();
							if(chip->is_double_charger_support)
								chip->sub_chg_ops->charging_disable();

							if(chip->chg_ops->oplus_chg_set_hz_mode != NULL)
								chip->chg_ops->oplus_chg_set_hz_mode(true);
							usleep_range(10000, 11000);
							chip->chg_ops->charger_suspend();
							if(chip->is_double_charger_support)
								chip->sub_chg_ops->charger_suspend();

							usleep_range(10000, 11000);
							pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_enable);

							alarm_start_relative(&chip->usbtemp_alarm_timer, ktime_set(15300, 0));
#endif /*CONFIG_HIGH_TEMP_VERSION*/
						}
					}
					count_r = 0;
					count_l = 0;
				}

				count++;
				if (count > time_count) {
					count = 0;
				}
			}
			msleep(delay);
		} else {
check_again:
			count = 0;
			last_usb_temp_r = chip->usb_temp_r;
			last_usb_temp_l = chip->usb_temp_l;
			msleep(delay);
			wait_event_interruptible(oplus_usbtemp_wq,
				(oplus_chg_get_vbus_status(chip) == true || oplus_chg_get_otg_online() == true)
				&& (chip->unwakelock_chg == false));
		}
		if(chip->dischg_flag) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule();
		}
	}
	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC

#define TEMP_25C 25
#define OPLUS_TCHG_01C_PRECISION	10
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

		tap_value = (((res - res2) * tmp1) + ((res1 - res) * tmp2)) * OPLUS_TCHG_01C_PRECISION / (res1 - res2);
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

static int get_chargeric_temp(void)
{
	int chargeric_temp = 0;
	static bool is_param_init = false;
	static struct ntc_temp ntc_param = {0};

	if (!pinfo) {
		chg_err("null pinfo or pdata\n");
		return TEMP_25C;
	}

	if (!is_param_init) {
		ntc_param.e_ntc_type = NTC_CHARGER_IC;
		ntc_param.i_tap_over_critical_low = 425100;
		ntc_param.i_rap_pull_up_r = PULL_UP_R0_390K;
		ntc_param.i_rap_pull_up_voltage = 1800;
		ntc_param.i_tap_min = -400;
		ntc_param.i_tap_max = 1250;
		ntc_param.i_25c_volt = 2457;
		if(ntc_param.i_rap_pull_up_r == PULL_UP_R0_390K) {
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

	ntc_param.ui_dwvolt =  get_charger_ntc_volt();
	chargeric_temp = oplus_res_to_temp(&ntc_param);

	chg_err("chargeric_temp:%d\n", chargeric_temp);
	return chargeric_temp;
}

int oplus_chg_get_chargeric_temp_cal(void)
{
	int chargeric_temp_cal = 0;

	if (!pinfo) {
		chg_err("null pinfo or pdata\n");
		return TEMP_25C;
	}

	chargeric_temp_cal = get_chargeric_temp();
	if (pinfo->support_ntc_01c_precision) {
		return chargeric_temp_cal;
	} else {
		return chargeric_temp_cal / OPLUS_TCHG_01C_PRECISION;
	}
}
EXPORT_SYMBOL(oplus_chg_get_chargeric_temp_cal);

static int get_batcon_temp(void)
{
	int batt_btb_temp = 0;
	static bool is_param_init = false;
	static struct ntc_temp ntc_param = {0};

	if (!pinfo) {
		chg_err("null pinfo or pdata\n");
		return TEMP_25C;
	}

	if (!is_param_init) {
		ntc_param.e_ntc_type = NTC_BATTERY;
		ntc_param.i_tap_over_critical_low = 425100;
		ntc_param.i_rap_pull_up_r = DEFAULT_PULL_UP_R0;
		ntc_param.i_rap_pull_up_voltage = 1800;
		ntc_param.i_tap_min = -400;
		ntc_param.i_tap_max = 1250;
		ntc_param.i_25c_volt = 2457;
		if(ntc_param.i_rap_pull_up_r == PULL_UP_R0_390K) {
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

	ntc_param.ui_dwvolt =  get_bat_con_ntc_volt();
	batt_btb_temp = oplus_res_to_temp(&ntc_param);

	chg_err("batt_btb_temp:%d\n", batt_btb_temp);
	return batt_btb_temp;
}

int oplus_chg_get_battery_btb_temp_cal(void)
{
	int battery_btb_temp_cal = 0;

	if (!pinfo) {
		chg_err("null pinfo or pdata\n");
		return TEMP_25C;
	}
	battery_btb_temp_cal = get_batcon_temp();
	if (pinfo->support_ntc_01c_precision) {
		return battery_btb_temp_cal;
	} else {
		return battery_btb_temp_cal / OPLUS_TCHG_01C_PRECISION;
	}
}
EXPORT_SYMBOL(oplus_chg_get_battery_btb_temp_cal);
#endif

/* factory mode start*/
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
	char atm_str[64];
	char *ptr, *ptr_e;

	memset(atm_str, 0x0, sizeof(atm_str));
	ptr = strstr(saved_command_line, "androidboot.atm=");
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");

		if (ptr_e != 0) {
			strncpy(atm_str, ptr + 16, ptr_e - ptr - 16);
			atm_str[ptr_e - ptr - 16] = '\0';
		}

		if (!strncmp(atm_str, "enable", strlen("enable")))
			info->atm_enabled = true;
		else
			info->atm_enabled = false;
	} else
		info->atm_enabled = false;

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
		else
			pinfo->enable_sw_jeita = true;

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR(sw_jeita, 0664, show_sw_jeita,
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

static DEVICE_ATTR(pe20, 0664, show_pe20, store_pe20);

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
static DEVICE_ATTR(pdc_max_watt, 0664, show_pdc_max_watt_level,
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

int notify_battery_full(void)
{
	printk("notify_battery_full_is_ok\n");

	if (charger_manager_notifier(pinfo, CHARGER_NOTIFY_EOC)) {
		printk("notifier fail\n");
		return 1;
	} else {
		return 0;
	}
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
	} else{
#ifdef OPLUS_FEATURE_CHG_BASIC
		oplus_wake_up_usbtemp_thread();
#endif /*OPLUS_FEATURE_CHG_BASIC*/
		charger_manager_notifier(pinfo, CHARGER_NOTIFY_START_CHARGING);
	}
	chr_err("wake_up_charger\n");
	_wake_up_charger(pinfo);
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
			__pm_relax(&pinfo->charger_wakelock);
#else
			__pm_relax(pinfo->charger_wakelock);
#endif
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

	if (info->is_suspend == false) {
		chr_err("%s: not suspend, wake up charger\n", __func__);
		_wake_up_charger(info);
	} else {
		chr_err("%s: alarm timer timeout\n", __func__);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		__pm_stay_awake(&info->charger_wakelock);
#else
		__pm_stay_awake(info->charger_wakelock);
#endif
	}

	return ALARMTIMER_NORESTART;
}

static void mtk_charger_start_timer(struct charger_manager *info)
{
	struct timespec time, time_now;
	ktime_t ktime;

	get_monotonic_boottime(&time_now);
	time.tv_sec = info->polling_interval;
	time.tv_nsec = 0;
	info->endtime = timespec_add(time_now, time);

	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);

	chr_err("%s: alarm timer start:%ld %ld\n", __func__,
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

	if (of_property_read_u32(np, "qcom,usb_ntc_pullup", &val) >= 0)
		usb_port_ntc_pullup = val;
	else {
		usb_port_ntc_pullup = USB_NTC_PULLUP_NOTFROM_DTS;
		chr_err("do not set usb prot ntc pullup by dts\n");
	}
	chr_err("algorithm name:%s\n", info->algorithm_name);

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
static DEVICE_ATTR(input_current, 0664, show_input_current,
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
static DEVICE_ATTR(chg1_current, 0664, show_chg1_current, store_chg1_current);

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
static DEVICE_ATTR(chg2_current, 0664, show_chg2_current, store_chg2_current);

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
static DEVICE_ATTR(BN_TestMode, 0664, show_BN_TestMode, store_BN_TestMode);

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
	char desc[32];
	int current_unlimited = 0;
	int cmd_discharging = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

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
	char desc[32];
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));


	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

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
	char desc[32];
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

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

	proc_create_data("current_cmd", 0644, battery_dir,
			&mtk_chg_current_cmd_fops, info);
	proc_create_data("en_power_path", 0644, battery_dir,
			&mtk_chg_en_power_path_fops, info);
	proc_create_data("en_safety_timer", 0644, battery_dir,
			&mtk_chg_en_safety_timer_fops, info);

_out:
	return ret;
}

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
			/* reset PE40 */
			break;

		case MTK_PD_CONNECT_HARD_RESET:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify HardReset\n");
			pinfo->pd_type = MTK_PD_CONNECT_NONE;
			pinfo->pd_reset = true;
			mutex_unlock(&pinfo->charger_pd_lock);
			_wake_up_charger(pinfo);
			/* reset PE40 */
			break;

		case MTK_PD_CONNECT_PE_READY_SNK:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify fixe voltage ready\n");
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK;
			mutex_unlock(&pinfo->charger_pd_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
			oplus_chg_track_record_chg_type_info();
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
#endif
			/* PD30 is ready */
			break;

		case MTK_PD_CONNECT_PE_READY_SNK_APDO:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify APDO Ready\n");
			pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_APDO;
			mutex_unlock(&pinfo->charger_pd_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
			oplus_chg_track_record_chg_type_info();
#endif
			/* PE40 is ready */
			_wake_up_charger(pinfo);
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

			if (pinfo->water_detected == true)
				pinfo->notify_code |= CHG_TYPEC_WD_STATUS;
			else
				pinfo->notify_code &= ~CHG_TYPEC_WD_STATUS;

			mtk_chgstat_notify(pinfo);
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
		};
	}
}

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

	charger_dir = proc_mkdir("mtk_charger", NULL);
	if (!charger_dir) {
		chr_err("fail to mkdir /proc/charger\n");
		return;
	}

	proc_create("dump_log", 0644,
		charger_dir, &charger_dump_log_proc_fops);
}

int oplus_get_typec_cc_orientation(void)
{
	int typec_cc_orientation = 0;
	static struct tcpc_device *tcpc_dev = NULL;

	if (tcpc_dev == NULL)
		tcpc_dev = tcpc_dev_get_by_name("type_c_port0");

	if (tcpc_dev != NULL) {
		if (tcpm_inquire_typec_attach_state(tcpc_dev) != TYPEC_UNATTACHED) {
			typec_cc_orientation = (int)tcpm_inquire_cc_polarity(tcpc_dev) + 1;
		} else {
			typec_cc_orientation = 0;
		}
		if (typec_cc_orientation != 0)
			printk(KERN_ERR "[OPLUS_CHG][%s]: cc[%d]\n", __func__, typec_cc_orientation);
	} else {
		typec_cc_orientation = 0;
	}
	return typec_cc_orientation;
}

int oplus_get_otg_online_status(void)
{
	if (oplus_chg_get_otg_online() == 1) {
		return 1;
	} else {
		return 0;
	}
}

static void mt6768_usbtemp_restart_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = container_of(work,
					struct oplus_chg_chip, usbtemp_restart_work);

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
	oplus_clear_usb_status(USB_TEMP_HIGH);
	chip->dischg_flag = false;
	if(chip->chg_ops->oplus_chg_set_hz_mode != NULL)
		chip->chg_ops->oplus_chg_set_hz_mode(false);

	wake_up_process(oplus_usbtemp_kthread);
	chg_err("recheck usb port temp\n");
}

static enum alarmtimer_restart recover_charge_hrtimer_func(
	struct alarm *alarm, ktime_t now)
{
	if(g_oplus_chip == NULL) {
		chg_err("get chip status latter\n");
		return ALARMTIMER_NORESTART;
	}

	schedule_work(&g_oplus_chip->usbtemp_restart_work);
	return ALARMTIMER_NORESTART;
}

static void oplus_usbtemp_thread_init(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	alarm_init(&chip->usbtemp_alarm_timer, ALARM_BOOTTIME, recover_charge_hrtimer_func);
	INIT_WORK(&chip->usbtemp_restart_work, mt6768_usbtemp_restart_work);
	oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_main, 0, "usbtemp_kthread");
	if (IS_ERR(oplus_usbtemp_kthread)) {
		chg_err("failed to cread oplus_usbtemp_kthread\n");
	}
}

void oplus_wake_up_usbtemp_thread(void)
{
	if (oplus_usbtemp_check_is_support() == true) {
		wake_up_interruptible(&oplus_usbtemp_wq);
		chg_debug("wake_up_usbtemp_thread, vbus:%d, otg:%d",oplus_chg_get_vbus_status(g_oplus_chip),oplus_chg_get_otg_online());
	}
}
EXPORT_SYMBOL(oplus_wake_up_usbtemp_thread);

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

	return 0;
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

				rc = usbtemp_channel_init(chip->dev);
				if (rc)
					chg_err("unable to init usbtemp_channel\n");
			}
		}
		chg_err("dischg-gpio:%d\n", chip->normalchg_gpio.dischg_gpio);
	}

	return rc;
}

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

	rc = oplus_even_c_prj_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_even_c_prj_parse_dt fail!\n", __func__);
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

	return rc;
}

/*====================================================================*/

/************************************************/
/* Power Supply Functions
*************************************************/
static int mt_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
    int rc = 0;
    rc = oplus_ac_get_property(psy, psp, val);

	return 0;
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

static int mt_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;
	switch (psp) {
	default:
		rc = oplus_usb_get_property(psy, psp, val);
	}
	return rc;
}

static int mt_usb_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	int ret = 0;
	switch (psp) {
	default:
		pr_err("set prop %d is not supported in usb\n", psp);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int battery_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int rc = 0;
	rc = oplus_battery_property_is_writeable(psy, psp);
	return rc;
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
			pr_err("bat pro POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, should shutdown!!!\n");
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

#ifdef OPLUS_FEATURE_CHG_BASIC
static int mt_mastercharger_prop_is_writeable(struct power_supply *psy,
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

static int mastercharger_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = oplus_chg_get_chargeric_temp_cal();
		pr_err("chargeric_temp_cal : %d", val->intval);
		break;
	default:
		break;
	}
	return 0;
}

static enum power_supply_property mastercharger_properties[] = {
	POWER_SUPPLY_PROP_TEMP,
};
#endif

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
    mt_chg->battery_psd.property_is_writeable = battery_prop_is_writeable,


    mt_chg->ac_psy = power_supply_register(mt_chg->dev, &mt_chg->ac_psd,
        &mt_chg->ac_cfg);
    if (IS_ERR(mt_chg->ac_psy)) {
        dev_err(mt_chg->dev, "Failed to register power supply: %ld\n",
            PTR_ERR(mt_chg->ac_psy));
        ret = PTR_ERR(mt_chg->ac_psy);
        goto err_ac_psy;
    }
    mt_chg->usb_psy = power_supply_register(mt_chg->dev, &mt_chg->usb_psd,
        &mt_chg->usb_cfg);
    if (IS_ERR(mt_chg->usb_psy)) {
        dev_err(mt_chg->dev, "Failed to register power supply: %ld\n",
            PTR_ERR(mt_chg->usb_psy));
        ret = PTR_ERR(mt_chg->usb_psy);
        goto err_usb_psy;
    }
    mt_chg->batt_psy = power_supply_register(mt_chg->dev, &mt_chg->battery_psd,
        NULL);
    if (IS_ERR(mt_chg->batt_psy)) {
        dev_err(mt_chg->dev, "Failed to register power supply: %ld\n",
            PTR_ERR(mt_chg->batt_psy));
        ret = PTR_ERR(mt_chg->batt_psy);
        goto err_battery_psy;
    }
    pr_info("%s\n", __func__);
    return 0;

err_usb_psy:
    power_supply_unregister(mt_chg->ac_psy);
err_ac_psy:
    power_supply_unregister(mt_chg->usb_psy);
err_battery_psy:
    power_supply_unregister(mt_chg->batt_psy);

    return ret;
}

static int masterchg_power_supply_init(struct master_chg_psy *master_chg)
{
	struct master_chg_psy *mastercharger_psy = NULL;
	int ret = 0;

	mastercharger_psy = master_chg;
	if (!mastercharger_psy) {
		chr_err("register mtk-master-charger error\n");
		return -EINVAL;
	}
	chr_err("register mtk-master-charger\n");
	mastercharger_psy->mastercharger_psy_desc.name = "mtk-master-charger";
	mastercharger_psy->mastercharger_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	mastercharger_psy->mastercharger_psy_desc.properties = mastercharger_properties;
	mastercharger_psy->mastercharger_psy_desc.num_properties = ARRAY_SIZE(mastercharger_properties);
	mastercharger_psy->mastercharger_psy_desc.get_property = mastercharger_get_property;
	mastercharger_psy->mastercharger_psy_desc.property_is_writeable =
		mt_mastercharger_prop_is_writeable;
	mastercharger_psy->mastercharger_psy_cfg.drv_data = mastercharger_psy;
	mastercharger_psy->mastercharger_psy = power_supply_register(mastercharger_psy->dev, &mastercharger_psy->mastercharger_psy_desc,
	&mastercharger_psy->mastercharger_psy_cfg);
	if (IS_ERR(mastercharger_psy->mastercharger_psy)) {
		chr_err("register mastercharger_psy fail\n");
		ret = PTR_ERR(mastercharger_psy->mastercharger_psy);
		goto err_mastercharger_psy;
	}
	return 0;
err_mastercharger_psy:
	power_supply_unregister(mastercharger_psy->mastercharger_psy);

	return ret;
}
static int oplus_chg_parse_charger_dt_2nd_override(struct oplus_chg_chip *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,iterm_ma_2nd", &chip->limits.iterm_ma);
	if (rc < 0) {
		chip->limits.iterm_ma = 300;
	}

	rc = of_property_read_u32(node, "qcom,recharge-mv_2nd", &chip->limits.recharge_mv);
	if (rc < 0) {
		chip->limits.recharge_mv = 121;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cold_vfloat_mv_2nd",
			&chip->limits.temp_little_cold_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cold_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,temp_cool_vfloat_mv_2nd",
			&chip->limits.temp_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_cool_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cool_vfloat_mv_2nd",
			&chip->limits.temp_little_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cool_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_vfloat_mv_2nd",
			&chip->limits.temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_normal_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,little_cold_vfloat_over_sw_limit_2nd",
			&chip->limits.little_cold_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cold_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,cool_vfloat_over_sw_limit_2nd",
			&chip->limits.cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.cool_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,little_cool_vfloat_over_sw_limit_2nd",
			&chip->limits.little_cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cool_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,normal_vfloat_over_sw_limit_2nd",
			&chip->limits.normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.normal_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,default_iterm_ma_2nd",
			&chip->limits.default_iterm_ma);
	if (rc < 0) {
		chip->limits.default_iterm_ma = 300;
	}

	rc = of_property_read_u32(node, "qcom,default_temp_normal_vfloat_mv_2nd",
			&chip->limits.default_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.default_temp_normal_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,default_normal_vfloat_over_sw_limit_2nd",
			&chip->limits.default_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.default_normal_vfloat_over_sw_limit = 4395;
	}

	chg_err("iterm_ma=%d, recharge_mv=%d, temp_little_cold_vfloat_mv=%d, \
temp_cool_vfloat_mv=%d, temp_little_cool_vfloat_mv=%d, \
temp_normal_vfloat_mv=%d, little_cold_vfloat_over_sw_limit=%d, \
cool_vfloat_over_sw_limit=%d, little_cool_vfloat_over_sw_limit=%d, \
normal_vfloat_over_sw_limit=%d, default_iterm_ma=%d, \
default_temp_normal_vfloat_mv=%d, default_normal_vfloat_over_sw_limit=%d\n",
			chip->limits.iterm_ma, chip->limits.recharge_mv, chip->limits.temp_little_cold_vfloat_mv,
			chip->limits.temp_cool_vfloat_mv, chip->limits.temp_little_cool_vfloat_mv,
			chip->limits.temp_normal_vfloat_mv, chip->limits.little_cold_vfloat_over_sw_limit,
			chip->limits.cool_vfloat_over_sw_limit, chip->limits.little_cool_vfloat_over_sw_limit,
			chip->limits.normal_vfloat_over_sw_limit, chip->limits.default_iterm_ma,
			chip->limits.default_temp_normal_vfloat_mv, chip->limits.default_normal_vfloat_over_sw_limit);

	return rc;
}

/*====================================================================*/
static ssize_t show_StopCharging_Test(struct device *dev,struct device_attribute *attr, char *buf)
{
	g_oplus_chip->stop_chg = false;
	oplus_chg_turn_off_charging(g_oplus_chip);
	printk("StopCharging_Test\n");
	return sprintf(buf, "chr=%d\n", g_oplus_chip->stop_chg);
}

static ssize_t store_StopCharging_Test(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
    return -1;
}
static DEVICE_ATTR(StopCharging_Test, 0664, show_StopCharging_Test, store_StopCharging_Test);

static ssize_t show_StartCharging_Test(struct device *dev,struct device_attribute *attr, char *buf)
{
	g_oplus_chip->stop_chg = true;
	oplus_chg_turn_on_charging(g_oplus_chip);
	printk("StartCharging_Test\n");
	return sprintf(buf, "chr=%d\n", g_oplus_chip->stop_chg);
}
static ssize_t store_StartCharging_Test(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
    return -1;
}
static DEVICE_ATTR(StartCharging_Test, 0664, show_StartCharging_Test, store_StartCharging_Test);

void oplus_chg_default_method0(void)
{
	pr_err("charger ic default %d\n", charger_ic__det_flag);
}

int oplus_chg_default_method1(void)
{
	return 0;
}

int oplus_chg_default_method2(int n)
{
	return 0;
}

void oplus_chg_default_method3(int n)
{

}

bool oplus_chg_default_method4(void)
{
	return false;
}
struct oplus_chg_operations  oplus_chg_default_ops = {
	.dump_registers = oplus_chg_default_method0,
	.kick_wdt = oplus_chg_default_method1,
	.hardware_init = oplus_chg_default_method1,
	.charging_current_write_fast = oplus_chg_default_method2,
	.set_aicl_point = oplus_chg_default_method3,
	.input_current_write = oplus_chg_default_method2,
	.float_voltage_write = oplus_chg_default_method2,
	.term_current_set = oplus_chg_default_method2,
	.charging_enable = oplus_chg_default_method1,
	.charging_disable = oplus_chg_default_method1,
	.get_charging_enable = oplus_chg_default_method1,
	.charger_suspend = oplus_chg_default_method1,
	.charger_unsuspend = oplus_chg_default_method1,
	.set_rechg_vol = oplus_chg_default_method2,
	.reset_charger = oplus_chg_default_method1,
	.read_full = oplus_chg_default_method1,
	.otg_enable = oplus_chg_default_method1,
	.otg_disable = oplus_chg_default_method1,
	.set_charging_term_disable = oplus_chg_default_method1,
	.check_charger_resume = oplus_chg_default_method4,

	.get_charger_type = oplus_chg_default_method1,
	.get_charger_volt = oplus_chg_default_method1,
	/*int (*get_charger_current)(void);*/
	.get_chargerid_volt = NULL,
    .set_chargerid_switch_val = oplus_chg_default_method3,
    .get_chargerid_switch_val = oplus_chg_default_method1,
	.check_chrdet_status = (bool (*) (void)) pmic_chrdet_status,

	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = oplus_get_rtc_ui_soc,
	.set_rtc_soc = oplus_set_rtc_ui_soc,
	.set_power_off = mt_power_off,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
#else
	.usb_connect = mt_usb_connect_v1,
	.usb_disconnect = mt_usb_disconnect_v1,
#endif
    .get_chg_current_step = oplus_chg_default_method1,
    .need_to_check_ibatt = oplus_chg_default_method4,
    .get_dyna_aicl_result = oplus_chg_default_method1,
    .get_shortc_hw_gpio_status = oplus_chg_default_method4,
	/*void (*check_is_iindpm_mode) (void);*/
    .oplus_chg_get_pd_type = NULL,
    .oplus_chg_pd_setup = NULL,
	.get_charger_subtype = oplus_chg_default_method1,
	.set_qc_config = NULL,
	.enable_qc_detect = NULL,
	.oplus_chg_set_high_vbus = NULL,
};


static int oplus_charger_probe(struct platform_device *pdev)
{
	struct charger_manager *info = NULL;
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;
	int ret = 0;
	struct oplus_chg_chip *oplus_chip = NULL;
	struct master_chg_psy *masterchg_psy = NULL;

#ifdef OPLUS_FEATURE_CHG_BASIC
/* add for charger_wakelock */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	char *name = NULL;
#endif
#endif
	/*struct mt_charger *mt_chg = NULL;*/
	chr_err("%s: starts\n", __func__);
	oplus_chip = devm_kzalloc(&pdev->dev,sizeof(struct oplus_chg_chip), GFP_KERNEL);
	if (!oplus_chip) {
		chg_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	masterchg_psy = devm_kzalloc(&pdev->dev, sizeof(struct master_chg_psy), GFP_KERNEL);
	if (!masterchg_psy) {
		chg_err(" masterchg_psy kzalloc() failed\n");
		return -ENOMEM;
	}
	oplus_chip->dev = &pdev->dev;
	masterchg_psy->dev = &pdev->dev;
	ret = oplus_chg_parse_svooc_dt(oplus_chip);
	if (oplus_chip->vbatt_num == 1) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("gauge chip null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		oplus_chip->is_double_charger_support = 0;

		charger_ic__det_flag = get_charger_ic_det(oplus_chip);
		if (charger_ic__det_flag == 0) {
			chg_err("charger IC is null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}

		if(oplus_chip->is_double_charger_support) {
			switch(charger_ic__det_flag) {
			case ((1 << BQ2591X) | (1 << BQ2589X)):
				oplus_chip->chg_ops = &oplus_chg_bq2589x_ops;
				oplus_chip->sub_chg_ops = &oplus_chg_bq2591x_ops;
				chg_err("charger IC match successful\n");
				break;
			case ((1 << BQ2589X) | (1 << RT9471D)):
				oplus_chip->chg_ops = &oplus_chg_bq2589x_ops;
				oplus_chip->sub_chg_ops = &oplus_chg_rt9471_ops;
				chg_err("charger IC match successful\n");
				break;
			case ((1 << SY6974) | (1 << BQ2589X)):
				oplus_chip->chg_ops = &oplus_chg_bq2589x_ops;
				oplus_chip->sub_chg_ops = &oplus_chg_sy6974_ops;
				chg_err("charger IC match successful\n");
				break;
			default:
				oplus_chip->chg_ops = &oplus_chg_default_ops;
				oplus_chip->sub_chg_ops = &oplus_chg_default_ops;
			}
		} else {
			switch(charger_ic__det_flag) {
			case (1 << RT9471D):
				oplus_chip->chg_ops = &oplus_chg_rt9471_ops;
				break;
			case (1 << SGM41512):
				oplus_chip->chg_ops = &oplus_chg_sgm41512_ops;
				chg_err("charger IC sgm41512 successful\n");
				break;
			default:
				oplus_chip->chg_ops = &oplus_chg_default_ops;
			}
		}
	} else {
		if (oplus_gauge_ic_chip_is_null() || oplus_vooc_check_chip_is_null()
					|| oplus_adapter_check_chip_is_null()) {
			chg_err("[oplus_chg_init] vooc || gauge || chg not ready, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
            /*oplus_chip->chg_ops = (oplus_get_chg_ops());*/
	}

/*move from oplus_mtk_charger.c begin*/
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	pinfo = info;

	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	info->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!info->tcpc) {
		chr_err("%s get tcpc device type_c_port0 fail\n", __func__);
	}
#endif

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev) {
		chr_err("found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	} else {
		chr_err("can't find primary charger!\n");
	}

	mtk_charger_parse_dt(info, &pdev->dev);

	mutex_init(&info->charger_lock);
	mutex_init(&info->charger_pd_lock);
	mutex_init(&info->cable_out_lock);
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

	info->sw_jeita.error_recovery_flag = true;

	mtk_charger_init_timer(info);

	if (info->chg1_dev != NULL && info->do_event != NULL) {
		info->chg1_nb.notifier_call = info->do_event;
		register_charger_device_notifier(info->chg1_dev,
						&info->chg1_nb);
		charger_dev_set_drvdata(info->chg1_dev, info);
	}

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

	oplus_chg_configfs_init(oplus_chip);
	if (mtk_pe_init(info) < 0)
		info->enable_pe_plus = false;

	if (mtk_pe20_init(info) < 0)
		info->enable_pe_2 = false;

	if (mtk_pe40_init(info) == false)
		info->enable_pe_4 = false;

#ifdef OPLUS_FEATURE_CHG_BASIC
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	mtk_hvdcp_v20_init(info);
#endif
#endif
	mtk_pdc_init(info);

	charger_ftm_init();
	mtk_charger_get_atm_mode(info);

#ifdef CONFIG_MTK_CHARGER_UNLIMITED
	info->usb_unlimited = true;
	info->enable_sw_safety_timer = false;
	charger_dev_enable_safety_timer(info->chg1_dev, false);
#endif

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
	info->chg1_consumer =
		charger_manager_get_by_name(&pdev->dev, "charger_port1");
	info->init_done = true;
	_wake_up_charger(info);
	/*move from oplus_mtk_charger.c end*/

	ret = oplus_power_supply_init(oplus_chip);
	if (of_property_read_bool(pdev->dev.of_node, "qcom,mastercharger_psy_set")) {
		ret = masterchg_power_supply_init(masterchg_psy);
	}

	printk("oplus_charger_probe end %p, prev %p, next %p\n",
			&oplus_chip->batt_psy->dev.power.wakeup->entry,
			oplus_chip->batt_psy->dev.power.wakeup->entry.prev,
			oplus_chip->batt_psy->dev.power.wakeup->entry.next);

	g_oplus_chip = oplus_chip;
	oplus_chip->chg_ops->hardware_init();
	/*keep "Ibus < 200mA" for 1s, so vooc/svooc adapter go into idle and release D+*/
    if (oplus_which_charger_ic() == 1) {/*for bq25890h*/
		oplus_chip->chg_ops->input_current_write(100);
	}
	oplus_chip->authenticate = oplus_gauge_get_batt_authenticate();
	oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	if (is_even_c_project()) {
		gpio_direction_output(oplus_chip->normalchg_gpio.chargerid_switch_gpio, 1);/* even-c */
	}
	if (oplus_which_charger_ic() == 2) {/*for bq25601d*/
		oplus_chg_parse_charger_dt_2nd_override(oplus_chip);
	}
	oplus_chg_init(oplus_chip);
	platform_set_drvdata(pdev, oplus_chip);
	device_init_wakeup(&pdev->dev, 1);
	oplus_chg_wake_update_work();
	oplus_tbatt_power_off_task_init(oplus_chip);
	if (oplus_usbtemp_check_is_support() == true)
		oplus_usbtemp_thread_init();
	if (IS_ERR(oplus_chip->batt_psy) == 0) {
		ret = device_create_file(&oplus_chip->batt_psy->dev, &dev_attr_StopCharging_Test);/*stop charging*/
		ret = device_create_file(&oplus_chip->batt_psy->dev, &dev_attr_StartCharging_Test);
	}

	return 0;
}

static int oplus_charger_remove(struct platform_device *dev)
{
	oplus_chg_configfs_exit();
	return 0;
}

static void oplus_charger_shutdown(struct platform_device *dev)
{
	if (g_oplus_chip != NULL)
		enter_ship_mode_function(g_oplus_chip);
}

static const struct of_device_id oplus_charger_of_match[] = {
	{.compatible = "mediatek,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, oplus_charger_of_match);

struct platform_device oplus_charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver oplus_charger_driver = {
	.probe = oplus_charger_probe,
	.remove = oplus_charger_remove,
	.shutdown = oplus_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = oplus_charger_of_match,
		   },
};


static int __init oplus_charger_init(void)
{
	return platform_driver_register(&oplus_charger_driver);
}
late_initcall(oplus_charger_init);


static void __exit mtk_charger_exit(void)
{
	platform_driver_unregister(&oplus_charger_driver);
}
module_exit(mtk_charger_exit);


MODULE_DESCRIPTION("MTK Charger Driver");
MODULE_LICENSE("GPL");
