// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "oplus_switching.h"
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_ufcs.h"
#include "../oplus_pps.h"

#define USB_20C 20
#define USB_30C 30
#define USB_40C	40
#define USB_57C	57
#define USB_50C 50
#define USB_55C 55
#define USB_100C  100

#define OPLUS_USBTEMP_HIGH_CURR_THRD 5000
#define OPLUS_USBTEMP_HIGH_CURR 1
#define OPLUS_USBTEMP_LOW_CURR 0
#define OPLUS_USBTEMP_CURR_CHANGE_TEMP 3
#define OPLUS_USBTEMP_CHANGE_RANGE_TIME 30

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
struct wake_lock usbtemp_wakelock;
#else
struct wakeup_source *usbtemp_wakelock;
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define VBUS_VOLT_THRESHOLD	3000
#else
#define VBUS_VOLT_THRESHOLD	400
#endif
#define USBTEMP_DEFAULT_VOLT_VALUE_MV 950

#define VBUS_MONITOR_INTERVAL	3000//3s

#define MIN_MONITOR_INTERVAL	50//50ms
#define MAX_MONITOR_INTERVAL	50//50ms
#define RETRY_CNT_DELAY         5 //ms
#define HIGH_TEMP_SHORT_CHECK_TIMEOUT 1000 /*ms*/

#define USBTEMP_RECOVER_INTERVAL   (14400*1000) /*4 hours*/
#define USBTEMP_CC_RECOVER_INTERVAL   (300*1000) /*5 mins*/

int __attribute__((weak)) qpnp_get_prop_charger_voltage_now(void) {return 0;}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
int __attribute__((weak)) oplus_chg_set_dischg_enable(bool en)
{
	return 0;
}
#else
__attribute__((weak)) int oplus_chg_set_dischg_enable(bool en);
#endif

static int usbtemp_debug = 0;
module_param(usbtemp_debug, int, 0644);
#define OPEN_LOG_BIT BIT(0)
#define TEST_FUNC_BIT BIT(1)
#define TEST_CURRENT_BIT BIT(2)
MODULE_PARM_DESC(usbtemp_debug, "debug usbtemp");
#define USB_TEMP_HIGH 0x01

static int usbtemp_recover_interval = USBTEMP_RECOVER_INTERVAL;
module_param(usbtemp_recover_interval, int, 0644);

static int usbtemp_dbg_tempr = 0;
module_param(usbtemp_dbg_tempr, int, 0644);
MODULE_PARM_DESC(usbtemp_dbg_tempr, "debug usbtemp temp r");

static int usbtemp_dbg_templ = 0;
module_param(usbtemp_dbg_templ, int, 0644);
MODULE_PARM_DESC(usbtemp_dbg_templ, "debug usbtemp temp l");

static int usbtemp_dbg_curr_status = -1;
module_param(usbtemp_dbg_curr_status, int, 0644);
MODULE_PARM_DESC(usbtemp_dbg_curr_status, "debug usbtemp current status");

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static struct timespec current_kernel_time(void)
{
	struct timespec ts;

	getnstimeofday(&ts);
	return ts;
}
#endif

void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip);

void oplus_set_usb_temp_high(struct oplus_chg_chip *chip)
{
	chip->usb_status |= USB_TEMP_HIGH;
}
bool oplus_usb_temp_is_high(struct oplus_chg_chip *chip)
{
	if (chip && ((chip->usb_status & USB_TEMP_HIGH) == USB_TEMP_HIGH)) {
		return true;
	} else {
		return false;
	}
}

void oplus_clear_usb_temp_high(struct oplus_chg_chip *chip)
{
	chip->usb_status = chip->usb_status & (~USB_TEMP_HIGH);
}

static void get_usb_temp(struct oplus_chg_chip *chg)
{

	int i = 0;
	chg->chg_ops->get_usbtemp_volt(chg);
	if (chg->usbtemp_chan_tmp) {
		chg->usb_temp_l = chg->usbtemp_volt_l;
		chg->usb_temp_r = chg->usbtemp_volt_r;
	} else {
		for (i = chg->len_array- 1; i >= 0; i--) {
			if (chg->con_volt[i] >= chg->usbtemp_volt_l)
				break;
			else if (i == 0)
				break;
		}

		if (usbtemp_dbg_templ != 0)
			chg->usb_temp_l = usbtemp_dbg_templ;
		else
			chg->usb_temp_l = chg->con_temp[i];

		for (i = chg->len_array - 1; i >= 0; i--) {
			if (chg->con_volt[i] >= chg->usbtemp_volt_r)
				break;
			else if (i == 0)
				break;
		}

		if (usbtemp_dbg_tempr != 0)
			chg->usb_temp_r = usbtemp_dbg_tempr;
		else
			chg->usb_temp_r = chg->con_temp[i];
	}
	if(usbtemp_debug & TEST_FUNC_BIT){
		chg->usb_temp_r = 60;
	}
	if(usbtemp_debug & TEST_CURRENT_BIT){
		chg->usb_temp_r = 44;
	}
	if(usbtemp_debug & OPEN_LOG_BIT)
		chg_err("usb_temp_l:%d, usb_temp_r:%d\n",chg->usb_temp_l, chg->usb_temp_r);
	if (oplus_is_pps_charging() && (oplus_pps_get_support_type() == PPS_SUPPORT_3CP)) {
		if(chg->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR)
			chg->usb_temp_r = chg->usb_temp_r - chg->usbtemp_cool_down_recoup_high;
		else
			chg->usb_temp_r = chg->usb_temp_r - chg->usbtemp_cool_down_recoup_low;
	}
}

int oplus_usbtemp_dischg_action(struct oplus_chg_chip *chip)
{
	int rc = 0;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;
	chg = &chip->pmic_spmi.smb5_chip->chg;
#endif

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() != HIGH_TEMP_AGING &&
	    !oplus_is_ptcrb_version()) {
#else
	if (1) {
#endif
		oplus_set_usb_temp_high(chip);
		if (oplus_vooc_get_fastchg_started() == true) {
			oplus_chg_set_chargerid_switch_val(0);
                        oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
                        oplus_vooc_reset_mcu();
			if (oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
				oplus_pps_get_support_type() == PPS_SUPPORT_3CP) {
				oplus_pps_set_pps_mos_enable(false);
			}
		}
		if (oplus_is_pps_charging()) {
			chg_err("oplus_pps_stop_usb_temp\n");
			oplus_pps_stop_usb_temp();
		}

		usleep_range(10000,10000);///msleep(10);
		if (is_vooc_support_single_batt_svooc() == true) {
			vooc_enable_cp_ovp(0);
		}

		if (oplus_is_ufcs_charging()) {
			chg_err("oplus_ufcs_stop_usb_temp\n");
			oplus_ufcs_stop_usb_temp();
		}

		if (chip->chg_ops->really_suspend_charger)
			chip->chg_ops->really_suspend_charger(true);
		else
			chip->chg_ops->charger_suspend();
		usleep_range(5000,5000);
		if (chip->chg_ops->set_typec_cc_open != NULL) {
			chip->chg_ops->set_typec_cc_open();
		} else {
			pr_err("[oplus_usbtemp_dischg_action]: set_typec_cc_open is null");
			if (chip->chg_ops->set_typec_sinkonly != NULL) {
				chip->chg_ops->set_typec_sinkonly();
			} else {
				pr_err("[oplus_usbtemp_dischg_action]: set_typec_sinkonly is null");
			}
		}
		usleep_range(5000, 5000);
		pr_err("[oplus_usbtemp_dischg_action]:run_action");
	}

#ifndef CONFIG_OPLUS_CHARGER_MTK
	mutex_lock(&chg->pinctrl_mutex);
#endif

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() == HIGH_TEMP_AGING ||
	    oplus_is_ptcrb_version()) {
#else
	if (1) {
#endif
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,do not set vbus down \n");
		if (chip->usbtemp_dischg_by_pmic) {
			/* add for MTK 6373 pmic usbtemp hardware scheme */
			oplus_chg_set_dischg_enable(false);
		} else {
			rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
		}
	} else {
		pr_err("[oplus_usbtemp_dischg_action]: set vbus down");
		if (chip->usbtemp_dischg_by_pmic) {
			/* add for MTK 6373 pmic usbtemp hardware scheme */
			oplus_chg_set_dischg_enable(true);
		} else {
			rc = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_enable);
		}
	}

#ifndef CONFIG_OPLUS_CHARGER_MTK
		mutex_unlock(&chg->pinctrl_mutex);
#endif

	return 0;
}

void oplus_set_usbtemp_wakelock(bool value) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if(value) {
		wake_lock(&usbtemp_wakelock);
	} else {
		wake_unlock(&usbtemp_wakelock);
	}
#else
	if(value) {
		__pm_stay_awake(usbtemp_wakelock);
	} else {
		__pm_relax(usbtemp_wakelock);
	}
#endif

}

void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip)
{
	int level;
	int count_time = 0;

#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;
	chg = &chip->pmic_spmi.smb5_chip->chg;
#endif
	if(gpio_is_valid(chip->normalchg_gpio.dischg_gpio)) {
		level = gpio_get_value(chip->normalchg_gpio.dischg_gpio);
	} else if (chip->usbtemp_dischg_by_pmic) {
		/* The method controlled by pmic pin can not read status, the level is always one. */
		level = 1;
	} else {
		return;
	}

	if (level == 1) {
		oplus_set_usbtemp_wakelock(true);
		do {
			get_usb_temp(chip);
			pr_err("[OPLUS_USBTEMP] recovering......");
			msleep(2000);
			count_time++;
		} while (!(((chip->usb_temp_r < USB_55C || chip->usb_temp_r == USB_100C )
			&& (chip->usb_temp_l < USB_55C ||  chip->usb_temp_l == USB_100C )) || count_time == 30));
		oplus_set_usbtemp_wakelock(false);
		if(count_time == 30) {
			pr_err("[OPLUS_USBTEMP] temp still high");
		} else {
			chip->dischg_flag = false;
			chg_err("dischg disable...[%d]\n", chip->usbtemp_volt);
			if (chip->chg_ops->really_suspend_charger)
				chip->chg_ops->really_suspend_charger(false);
			oplus_clear_usb_temp_high(chip);
#ifndef CONFIG_OPLUS_CHARGER_MTK
			mutex_lock(&chg->pinctrl_mutex);
#endif
			if (chip->usbtemp_dischg_by_pmic) {
				oplus_chg_set_dischg_enable(false);
			} else {
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
			}
#ifndef CONFIG_OPLUS_CHARGER_MTK
			mutex_unlock(&chg->pinctrl_mutex);
#endif
			pr_err("[OPLUS_USBTEMP] usbtemp recover");
		}
	}
	return;
}

EXPORT_SYMBOL(oplus_usbtemp_recover_func);

static void usbtemp_restart_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = container_of(work,
			struct oplus_chg_chip, usbtemp_restart_work);
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = &chip->pmic_spmi.smb5_chip->chg;
#endif
	int level = 0;

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio)) {
		level = gpio_get_value(chip->normalchg_gpio.dischg_gpio);
	} else if (chip->usbtemp_dischg_by_pmic) {
		/* The method controlled by pmic pin can not read status, the level is always one. */
		level = 1;
	} else {
		goto done;
	}

	switch (chip->usbtemp_timer_stage) {
	case OPLUS_USBTEMP_TIMER_STAGE0:
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE1;
		chg_err("stage0, set sink only\n");
		if (chip->chg_ops->set_typec_sinkonly)
			chip->chg_ops->set_typec_sinkonly();
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
		chg_err("stage1, set vbus_short output low\n");
		break;
	default:
		chg_err("stage%d not support\n", chip->usbtemp_timer_stage);
		goto done;
	}

	if (level == 1) {
		chip->dischg_flag = false;
		chg_err("dischg disable...\n");
		if (chip->chg_ops->really_suspend_charger)
			chip->chg_ops->really_suspend_charger(false);
		oplus_clear_usb_temp_high(chip);

#ifndef CONFIG_OPLUS_CHARGER_MTK
		mutex_lock(&chg->pinctrl_mutex);
#endif
		if (chip->usbtemp_dischg_by_pmic) {
			oplus_chg_set_dischg_enable(false);
		} else {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);
		}

#ifndef CONFIG_OPLUS_CHARGER_MTK
		mutex_unlock(&chg->pinctrl_mutex);
#endif
		chg_err("usbtemp recover");
	}

done:
	oplus_set_usbtemp_wakelock(false);
}

enum alarmtimer_restart usbtemp_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct oplus_chg_chip *chip = container_of(alarm,
				struct oplus_chg_chip, usbtemp_alarm_timer);;

	chg_err("timer is up now.\n");
	oplus_set_usbtemp_wakelock(true);
	schedule_work(&chip->usbtemp_restart_work);

	return ALARMTIMER_NORESTART;
}
EXPORT_SYMBOL(usbtemp_alarm_timer_func);

static int oplus_ccdetect_is_gpio(struct oplus_chg_chip *chip)
{
	int ret = 0;
	static bool dis_log = false;

#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;
#endif
	if (!chip) {
		if (dis_log == false) {
			chg_err("oplus_chg_chip is null!");
		}
		ret = 1;
		return ret;
	}
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (gpio_is_valid(chip->chgic_mtk.oplus_info->ccdetect_gpio)
	    || chip->support_wd0) {
		if (dis_log == false) {
			chg_err(" oplus_chg_chip mkt has ccdetect_gpio");
		}
		ret = 2;
	}
#else
	chg = &chip->pmic_spmi.smb5_chip->chg;
	if (gpio_is_valid(chg->ccdetect_gpio)) {
		if (dis_log == false) {
			chg_err("oplus_chg_chip qcom has ccdetect_gpio!");
		}
		ret = 2;
	}
#endif
	if (dis_log == false) {
		chg_err(" oplus_chg_chip ret =%d\n", ret);
	}
	dis_log = true;
	return ret;
}

#define USBTEMP_TRIGGER_CONDITION_1	1
#define USBTEMP_TRIGGER_CONDITION_2	2
#define USBTEMP_TRIGGER_CONDITION_COOL_DOWN	3
#define USBTEMP_TRIGGER_CONDITION_COOL_DOWN_RECOVERY 4
static int oplus_chg_track_upload_usbtemp_info(
	struct oplus_chg_chip *chip, int condition,
	int last_usb_temp_l, int last_usb_temp_r, int batt_current)
{
	int index = 0;
	char power_info[OPLUS_CHG_TRACK_CURX_INFO_LEN] = {0};

	memset(chip->usbtemp_load_trigger.crux_info,
		0, sizeof(chip->usbtemp_load_trigger.crux_info));
	oplus_chg_track_obtain_power_info(power_info, sizeof(power_info));
	if (condition == USBTEMP_TRIGGER_CONDITION_1) {
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$reason@@%s", "first_condition");
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$batt_temp@@%d$$usb_temp_l@@%d"
				"$$usb_temp_r@@%d",
				chip->tbatt_temp, chip->usb_temp_l,
				chip->usb_temp_r);
	} else if (condition == USBTEMP_TRIGGER_CONDITION_2) {
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$reason@@%s", "second_condition");
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$batt_temp@@%d$$usb_temp_l@@%d"
				"$$last_usb_temp_l@@%d"
				"$$usb_temp_r@@%d$$last_usb_temp_r@@%d",
				chip->tbatt_temp, chip->usb_temp_l, last_usb_temp_l,
				chip->usb_temp_r, last_usb_temp_r);
	} else if (condition == USBTEMP_TRIGGER_CONDITION_COOL_DOWN) {
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$reason@@%s", "cool_down_condition");
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$batt_temp@@%d$$usb_temp_l@@%d"
				"$$usb_temp_r@@%d$$batt_current@@%d",
				chip->tbatt_temp, chip->usb_temp_l,
				chip->usb_temp_r, batt_current);
	} else if (condition == USBTEMP_TRIGGER_CONDITION_COOL_DOWN_RECOVERY) {
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$reason@@%s", "cool_down_recovery_condition");
		index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$batt_temp@@%d$$usb_temp_l@@%d"
				"$$usb_temp_r@@%d$$batt_current@@%d",
				chip->tbatt_temp, chip->usb_temp_l,
				chip->usb_temp_r, batt_current);
	} else {
		chg_err("!!!condition err\n");
		return -1;
	}

	index += snprintf(&(chip->usbtemp_load_trigger.crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", power_info);

	schedule_delayed_work(&chip->usbtemp_load_trigger_work, 0);
	pr_info("%s\n", chip->usbtemp_load_trigger.crux_info);

	return 0;
}

static void oplus_usbtemp_set_recover(struct oplus_chg_chip *chip)
{
	if (alarmtimer_get_rtcdev()) {
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE0;
		alarm_start_relative(&chip->usbtemp_alarm_timer, ms_to_ktime(USBTEMP_CC_RECOVER_INTERVAL));
	} else {
		chg_err("Failed to get soc alarm-timer");
	}
}

#define RETRY_COUNT		3
void oplus_update_usbtemp_current_status(struct oplus_chg_chip *chip)
{
	static int limit_cur_cnt_r = 0;
	static int limit_cur_cnt_l = 0;
	static int recover_cur_cnt_r = 0;
	static int recover_cur_cnt_l = 0;

	if (!chip) {
		return;
	}

	if (chip->vooc_project == 0) {
		return;
	}

	if((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C)
			&& (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		chip->usbtemp_cool_down = 0;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		return;
	}

	if(chip->new_usbtemp_cool_down_support) {
		if((chip->icharging * -1) > 5000) {
			if(chip->usbtemp_cool_down_temp_gap == chip->usbtemp_cool_down_temp_first_gap) {
				limit_cur_cnt_r = 0;
				recover_cur_cnt_r = 0;
				limit_cur_cnt_l = 0;
				recover_cur_cnt_l = 0;
				chg_err("usbtemp_cool_down_temp_gap to %d",chip->usbtemp_cool_down_temp_second_gap);
			}
			chip->usbtemp_cool_down_temp_gap = chip->usbtemp_cool_down_temp_second_gap;
			chip->usbtemp_cool_down_recovery_temp_gap = chip->usbtemp_cool_down_recovery_temp_second_gap;
		} else {
			if(chip->usbtemp_cool_down_temp_gap == chip->usbtemp_cool_down_temp_second_gap) {
				limit_cur_cnt_r = 0;
				recover_cur_cnt_r = 0;
				limit_cur_cnt_l = 0;
				recover_cur_cnt_l = 0;
				chg_err("usbtemp_cool_down_temp_gap to %d",chip->usbtemp_cool_down_temp_first_gap);
			}
			chip->usbtemp_cool_down_temp_gap = chip->usbtemp_cool_down_temp_first_gap;
			chip->usbtemp_cool_down_recovery_temp_gap = chip->usbtemp_cool_down_recovery_temp_first_gap;
		}
	}

	if ((chip->usb_temp_r  - chip->tbatt_temp/10) >= chip->usbtemp_cool_down_temp_gap) {
		limit_cur_cnt_r++;
		recover_cur_cnt_r = 0;
	} else if ((chip->usb_temp_r  - chip->tbatt_temp/10) <= chip->usbtemp_cool_down_recovery_temp_gap)  {
		recover_cur_cnt_r++;
		limit_cur_cnt_r = 0;
	}

	if ((chip->usb_temp_l  - chip->tbatt_temp/10) >= chip->usbtemp_cool_down_temp_gap) {
		limit_cur_cnt_l++;
		recover_cur_cnt_l = 0;
	} else if ((chip->usb_temp_l  - chip->tbatt_temp/10) <= chip->usbtemp_cool_down_recovery_temp_gap)  {
		recover_cur_cnt_l++;
		limit_cur_cnt_l = 0;
	}

	if (RETRY_COUNT <= limit_cur_cnt_r || RETRY_COUNT <= limit_cur_cnt_l) {
		chip->smart_charge_user = SMART_CHARGE_USER_USBTEMP;
		chip->cool_down_done = true;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
	} else if (RETRY_COUNT <= recover_cur_cnt_r && RETRY_COUNT <= recover_cur_cnt_l) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		chip->usbtemp_cool_down = 0;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
	}

	return;
}

int oplus_usbtemp_monitor_common(void *data)
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
	int condition;
	int batt_current = 0;
	struct oplus_chg_chip *chip = (struct oplus_chg_chip *) data;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;
	chg = &chip->pmic_spmi.smb5_chip->chg;
#endif
	if (alarmtimer_get_rtcdev()) {
		alarm_init(&chip->usbtemp_alarm_timer, ALARM_REALTIME, usbtemp_alarm_timer_func);
		INIT_WORK(&chip->usbtemp_restart_work, usbtemp_restart_work);
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE0;
	} else {
		chg_err("Failed to get soc alarm-timer");
	}

	if (chip->usbtemp_max_temp_thr <= 0) {
		chip->usbtemp_max_temp_thr = USB_57C;
	}

	if (chip->usbtemp_temp_up_time_thr <= 0) {
		chip->usbtemp_temp_up_time_thr = 30;
	}
	pr_err("[%s]:run first chip->usbtemp_max_temp_thr[%d], chip->usbtemp_temp_up_time_thr[%d]!",
		__func__, chip->usbtemp_max_temp_thr, chip->usbtemp_temp_up_time_thr);
	while (!kthread_should_stop()) {
		if(chip->chg_ops->oplus_usbtemp_monitor_condition != NULL){
			wait_event_interruptible(chip->oplus_usbtemp_wq, chip->usbtemp_check);
		} else {
			pr_err("[oplus_usbtemp_monitor_main]:condition pointer is NULL");
			return 0;
		}
		if(chip->dischg_flag == true){
			goto dischg;
		}
		if(chip->chg_ops->get_usbtemp_volt == NULL){
			pr_err("[oplus_usbtemp_monitor_main]:get_usbtemp_volt is NULL");
			return 0;
		}
		get_usb_temp(chip);
		if ((chip->usb_temp_l < USB_50C) && (chip->usb_temp_r < USB_50C)){//get vbus when usbtemp < 50C
			vbus_volt = qpnp_get_prop_charger_voltage_now();
			if (vbus_volt == 0) {
				vbus_volt = chip->charger_volt;
			}
		} else{
			vbus_volt = 0;
		}
		if ((chip->usb_temp_l < USB_40C) && (chip->usb_temp_r < USB_40C)) {
			delay = MAX_MONITOR_INTERVAL;
			total_count = 10;
		} else {
			delay = MIN_MONITOR_INTERVAL;
			total_count = chip->usbtemp_temp_up_time_thr;
		}

		oplus_update_usbtemp_current_status(chip);

		if ((chip->usbtemp_volt_l < USB_50C) && (chip->usbtemp_volt_r < USB_50C) && (vbus_volt < VBUS_VOLT_THRESHOLD))
			delay = VBUS_MONITOR_INTERVAL;
		//condition1  :the temp is higher than 57
		if (chip->tbatt_temp/10 <= USB_50C &&(((chip->usb_temp_l >= chip->usbtemp_max_temp_thr) && (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= chip->usbtemp_max_temp_thr) && (chip->usb_temp_r < USB_100C)))) {
			pr_err("in loop 1");
			for (i = 1; i < retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				get_usb_temp(chip);
				if (chip->usb_temp_r >= chip->usbtemp_max_temp_thr && chip->usb_temp_r < USB_100C)
					count_r++;
				if (chip->usb_temp_l >= chip->usbtemp_max_temp_thr && chip->usb_temp_l < USB_100C)
					count_l++;
				pr_err("countl : %d",count_l);
			}
			if (count_r >= retry_cnt || count_l >= retry_cnt) {
				if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable) ||
				    chip->usbtemp_dischg_by_pmic) {
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
		if (chip->tbatt_temp/10 > USB_50C && (((chip->usb_temp_l >= chip->tbatt_temp/10 + 7) && (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= chip->tbatt_temp/10 + 7) && (chip->usb_temp_r < USB_100C)))) {
			pr_err("in loop 1");
			for (i = 1; i <= retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				get_usb_temp(chip);
				if ((chip->usb_temp_r >= chip->tbatt_temp/10 + 7) && chip->usb_temp_r < USB_100C)
					count_r++;
				if ((chip->usb_temp_l >= chip->tbatt_temp/10 + 7) && chip->usb_temp_l < USB_100C)
					count_l++;
				pr_err("countl : %d", count_l);
			}
			if (count_r >= retry_cnt || count_l >= retry_cnt) {
				if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable) ||
				    chip->usbtemp_dischg_by_pmic) {
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
		if(condition1 == true){
			pr_err("jump_to_dischg");
			goto dischg;
		}

		//condition2  :the temp uprising to fast
		if (((chip->usb_temp_l - chip->tbatt_temp/10) >= chip->usbtemp_batttemp_gap && chip->usb_temp_l < USB_100C)
				|| ((chip->usb_temp_r - chip->tbatt_temp/10) >= chip->usbtemp_batttemp_gap && chip->usb_temp_r < USB_100C)) {
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
					get_usb_temp(chip);
					if ((chip->usb_temp_r - last_usb_temp_r) >= 3 && chip->usb_temp_r < USB_100C)
						count_r++;
					if ((chip->usb_temp_l - last_usb_temp_l) >= 3 && chip->usb_temp_l < USB_100C)
						count_l++;
					pr_err("countl : %d,countr : %d", count_l, count_r);
				}
				current_temp_l = chip->usb_temp_l;
				current_temp_r = chip->usb_temp_r;
				if ((count_l >= retry_cnt &&  chip->usb_temp_l > USB_30C && chip->usb_temp_l < USB_100C)
						|| (count_r >= retry_cnt &&  chip->usb_temp_r > USB_30C  && chip->usb_temp_r < USB_100C))  {
					if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable) ||
					    chip->usbtemp_dischg_by_pmic) {
						chip->dischg_flag = true;
						chg_err("dischg enable3...,current_temp_l=%d,last_usb_temp_l=%d,current_temp_r=%d,last_usb_temp_r =%d\n",
								current_temp_l, last_usb_temp_l, current_temp_r, last_usb_temp_r);
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
	//judge whether to go the action
	dischg:
		if ((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C)
				&& (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
			condition1 = false;
			condition2 = false;
			chip->dischg_flag = false;
		}

		if ((condition1 == true || condition2 == true) && chip->dischg_flag == true) {
			condition = (condition1== true ?
				USBTEMP_TRIGGER_CONDITION_1 :
				USBTEMP_TRIGGER_CONDITION_2);
			oplus_chg_track_upload_usbtemp_info(
				chip, condition, last_usb_temp_l, last_usb_temp_r,
				batt_current);
			oplus_usbtemp_dischg_action(chip);
			condition1 = false;
			condition2 = false;
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
			if ((get_eng_version() != HIGH_TEMP_AGING &&
			    !oplus_is_ptcrb_version()) &&
#else
			if (
#endif
			    (oplus_ccdetect_is_gpio(chip) == 0)) {
				chg_err("start delay work for recover charging");
				count = 0;
				last_usb_temp_r = chip->usb_temp_r;
				last_usb_temp_l = chip->usb_temp_l;
				oplus_usbtemp_set_recover(chip);
			}
		} else if (chip->debug_force_usbtemp_trigger) {
			oplus_chg_track_upload_usbtemp_info(
				chip, chip->debug_force_usbtemp_trigger,
				last_usb_temp_l, last_usb_temp_r, batt_current);
			chip->debug_force_usbtemp_trigger = 0;
		}
		msleep(delay);
		if (usbtemp_debug & OPEN_LOG_BIT) {
			pr_err("usbtemp: delay %d",delay);
			chg_err("==================usbtemp_volt_l[%d], usb_temp_l[%d], usbtemp_volt_r[%d], usb_temp_r[%d]\n",
				chip->usbtemp_volt_l,chip->usb_temp_l, chip->usbtemp_volt_r, chip->usb_temp_r);
		}
	}
	return 0;
}
EXPORT_SYMBOL(oplus_usbtemp_monitor_common);

bool oplus_usbtemp_l_trigger_current_status(struct oplus_chg_chip *chip)
{
	if (!chip)
		return false;

	if (chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C) {
		return false;
	}

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_l >= chip->usbtemp_cool_down_ntc_low) ||
			(chip->usb_temp_l - chip->tbatt_temp / 10) >=
				chip->usbtemp_cool_down_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_l >= chip->usbtemp_cool_down_ntc_high) ||
			(chip->usb_temp_l - chip->tbatt_temp / 10) >=
				chip->usbtemp_cool_down_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

bool oplus_usbtemp_l_recovery_current_status(struct oplus_chg_chip *chip)
{
	if (!chip)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_l <= chip->usbtemp_cool_down_recover_ntc_low) &&
			(chip->usb_temp_l - chip->tbatt_temp / 10) <=
				chip->usbtemp_cool_down_recover_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_l <= chip->usbtemp_cool_down_recover_ntc_high) &&
			(chip->usb_temp_l - chip->tbatt_temp / 10) <=
				chip->usbtemp_cool_down_recover_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

bool oplus_usbtemp_r_trigger_current_status(struct oplus_chg_chip *chip)
{
	if (!chip)
		return false;

	if (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C) {
		return false;
	}

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_r >= chip->usbtemp_cool_down_ntc_low) ||
			(chip->usb_temp_r - chip->tbatt_temp / 10) >=
				chip->usbtemp_cool_down_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_r >= chip->usbtemp_cool_down_ntc_high) ||
			(chip->usb_temp_r - chip->tbatt_temp / 10) >=
				chip->usbtemp_cool_down_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

bool oplus_usbtemp_r_recovery_current_status(struct oplus_chg_chip *chip)
{
	if (!chip)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((chip->usb_temp_r <= chip->usbtemp_cool_down_recover_ntc_low) &&
			(chip->usb_temp_r - chip->tbatt_temp / 10) <=
				chip->usbtemp_cool_down_recover_gap_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((chip->usb_temp_r <= chip->usbtemp_cool_down_recover_ntc_high) &&
			(chip->usb_temp_r - chip->tbatt_temp / 10) <=
				chip->usbtemp_cool_down_recover_gap_high)
			return true;
		return false;
	} else {
		return false;
	}
}

#define RETRY_COUNT		3
void oplus_update_usbtemp_current_status_new_method(struct oplus_chg_chip *chip)
{
	static int limit_cur_cnt_r = 0;
	static int limit_cur_cnt_l = 0;
	static int recover_cur_cnt_r = 0;
	static int recover_cur_cnt_l = 0;
	int condition, batt_current;
	int last_usb_temp_l = 25;
	int last_usb_temp_r = 25;

	if (!chip) {
		return;
	}

	batt_current = chip->usbtemp_batt_current;

	if (chip->vooc_project == 0) {
		return;
	}

	if((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C)
			&& (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		chip->usbtemp_cool_down = 0;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		return;
	}

	if(chip->new_usbtemp_cool_down_support) {
		if((chip->icharging * -1) > 5000) {
			if(chip->usbtemp_cool_down_temp_gap == chip->usbtemp_cool_down_temp_first_gap) {
				limit_cur_cnt_r = 0;
				recover_cur_cnt_r = 0;
				limit_cur_cnt_l = 0;
				recover_cur_cnt_l = 0;
				chg_err("usbtemp_cool_down_temp_gap to %d", chip->usbtemp_cool_down_temp_second_gap);
			}
			chip->usbtemp_cool_down_temp_gap = chip->usbtemp_cool_down_temp_second_gap;
			chip->usbtemp_cool_down_recovery_temp_gap = chip->usbtemp_cool_down_recovery_temp_second_gap;
		} else {
			if(chip->usbtemp_cool_down_temp_gap == chip->usbtemp_cool_down_temp_second_gap) {
				limit_cur_cnt_r = 0;
				recover_cur_cnt_r = 0;
				limit_cur_cnt_l = 0;
				recover_cur_cnt_l = 0;
				chg_err("usbtemp_cool_down_temp_gap to %d", chip->usbtemp_cool_down_temp_first_gap);
			}
			chip->usbtemp_cool_down_temp_gap = chip->usbtemp_cool_down_temp_first_gap;
			chip->usbtemp_cool_down_recovery_temp_gap = chip->usbtemp_cool_down_recovery_temp_first_gap;
		}
	}

	if (oplus_usbtemp_r_trigger_current_status(chip)) {
		limit_cur_cnt_r++;
		recover_cur_cnt_r = 0;
	} else if (oplus_usbtemp_r_recovery_current_status(chip))  {
		recover_cur_cnt_r++;
		limit_cur_cnt_r = 0;
	}

	if (oplus_usbtemp_l_trigger_current_status(chip)) {
		limit_cur_cnt_l++;
		recover_cur_cnt_l = 0;
	} else if (oplus_usbtemp_l_recovery_current_status(chip))  {
		recover_cur_cnt_l++;
		limit_cur_cnt_l = 0;
	}

	if ((RETRY_COUNT <= limit_cur_cnt_r || RETRY_COUNT <= limit_cur_cnt_l)
		&& (chip->smart_charge_user == SMART_CHARGE_USER_OTHER)) {
		chip->smart_charge_user = SMART_CHARGE_USER_USBTEMP;
		chip->cool_down_done = true;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		condition = USBTEMP_TRIGGER_CONDITION_COOL_DOWN;
		oplus_chg_track_upload_usbtemp_info(chip,
				condition, last_usb_temp_l, last_usb_temp_r, batt_current);
	} else if ((RETRY_COUNT <= recover_cur_cnt_r && RETRY_COUNT <= recover_cur_cnt_l)
			&& (chip->smart_charge_user == SMART_CHARGE_USER_USBTEMP)) {
		chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
		chip->usbtemp_cool_down = 0;
		limit_cur_cnt_r = 0;
		recover_cur_cnt_r = 0;
		limit_cur_cnt_l = 0;
		recover_cur_cnt_l = 0;
		condition = USBTEMP_TRIGGER_CONDITION_COOL_DOWN_RECOVERY;
		oplus_chg_track_upload_usbtemp_info(chip,
				condition, last_usb_temp_l, last_usb_temp_r, batt_current);
	}

	return;
}

bool oplus_usbtemp_condition_temp_high(struct oplus_chg_chip *chip)
{
	if (!chip)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if (chip->tbatt_temp / 10 <= chip->usbtemp_batt_temp_low &&
			(((chip->usb_temp_l >= chip->usbtemp_ntc_temp_low)
				&& (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= chip->usbtemp_ntc_temp_low)
				&& (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if (chip->tbatt_temp / 10 <= chip->usbtemp_batt_temp_high &&
			(((chip->usb_temp_l >= chip->usbtemp_ntc_temp_high)
				&& (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= chip->usbtemp_ntc_temp_high)
				&& (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else {
		return false;
	}
}

bool oplus_usbtemp_temp_rise_fast_with_batt_temp(struct oplus_chg_chip *chip)
{
	if (!chip)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if (chip->tbatt_temp / 10 > chip->usbtemp_batt_temp_low &&
			(((chip->usb_temp_l >= chip->tbatt_temp / 10 +
				chip->usbtemp_temp_gap_low_with_batt_temp)
				&& (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= chip->tbatt_temp / 10 +
				chip->usbtemp_temp_gap_low_with_batt_temp)
				&& (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if (chip->tbatt_temp / 10 > chip->usbtemp_batt_temp_high &&
			(((chip->usb_temp_l >= chip->tbatt_temp / 10 +
				chip->usbtemp_temp_gap_high_with_batt_temp)
				&& (chip->usb_temp_l < USB_100C))
			|| ((chip->usb_temp_r >= chip->tbatt_temp / 10 +
				chip->usbtemp_temp_gap_high_with_batt_temp)
				&& (chip->usb_temp_r < USB_100C))))
			return true;
		return false;
	} else {
		return false;
	}
}

bool oplus_usbtemp_temp_rise_fast_without_batt_temp(struct oplus_chg_chip *chip)
{
	if (!chip)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((((chip->usb_temp_l - chip->tbatt_temp / 10) >=
				chip->usbtemp_temp_gap_low_without_batt_temp)
				&& (chip->usb_temp_l < USB_100C)) ||
			(((chip->usb_temp_r - chip->tbatt_temp / 10) >=
				chip->usbtemp_temp_gap_low_without_batt_temp)
				&& (chip->usb_temp_r < USB_100C)))
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((((chip->usb_temp_l - chip->tbatt_temp / 10) >=
				chip->usbtemp_temp_gap_high_without_batt_temp)
				&& (chip->usb_temp_l < USB_100C)) ||
			(((chip->usb_temp_r - chip->tbatt_temp / 10) >=
				chip->usbtemp_temp_gap_high_without_batt_temp)
				&& (chip->usb_temp_r < USB_100C)))
			return true;
		return false;
	} else {
		return false;
	}
}

bool oplus_usbtemp_judge_temp_gap(struct oplus_chg_chip *chip, int current_temp, int last_temp)
{
	if (!chip)
		return false;

	if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
		if ((current_temp - last_temp) >= chip->usbtemp_rise_fast_temp_low)
			return true;
		return false;
	} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
		if ((current_temp - last_temp) >= chip->usbtemp_rise_fast_temp_high)
			return true;
		return false;
	} else {
		return false;
	}
}

bool oplus_usbtemp_change_curr_range(struct oplus_chg_chip *chip, int retry_cnt,
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
	if (((current_curr_change_temp_l - last_curr_change_usb_temp_l) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP)
			|| (current_curr_change_temp_r - last_curr_change_usb_temp_r) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP) {
		for (i = 1; i <= retry_cnt; i++) {
			mdelay(RETRY_CNT_DELAY);
			get_usb_temp(chip);
			if ((chip->usb_temp_r - last_curr_change_usb_temp_r) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP
					&& chip->usb_temp_r < USB_100C)
				count_curr_r++;
			if ((chip->usb_temp_l - last_curr_change_usb_temp_l) >= OPLUS_USBTEMP_CURR_CHANGE_TEMP
					&& chip->usb_temp_l < USB_100C)
				count_curr_l++;
			pr_err("countl : %d,countr : %d", count_curr_l, count_curr_r);
		}
		current_curr_change_temp_l = chip->usb_temp_l;
		current_curr_change_temp_r = chip->usb_temp_r;

		if ((count_curr_l >= retry_cnt &&  chip->usb_temp_l > USB_30C && chip->usb_temp_l < USB_100C)
				|| (count_curr_r >= retry_cnt &&  chip->usb_temp_r > USB_30C  && chip->usb_temp_r < USB_100C)) {
			chg_err("change curr range...,current_temp_l=%d,last_usb_temp_l=%d,current_temp_r=%d,last_usb_temp_r =%d, chip->tbatt_temp = %d\n",
					current_curr_change_temp_l,
					last_curr_change_usb_temp_l,
					current_curr_change_temp_r,
					last_curr_change_usb_temp_r,
					chip->tbatt_temp);
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

bool oplus_usbtemp_trigger_for_high_temp(struct oplus_chg_chip *chip, int retry_cnt,
					int count_r, int count_l)
{
	int i = 0;

	if (!chip)
		return false;

	if (oplus_usbtemp_condition_temp_high(chip)) {
		pr_err("in usbtemp higher than 57 or 69!\n");
		for (i = 1; i < retry_cnt; i++) {
			mdelay(RETRY_CNT_DELAY);
			get_usb_temp(chip);
			if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
				if (chip->usb_temp_r >= chip->usbtemp_ntc_temp_low && chip->usb_temp_r < USB_100C)
					count_r++;
				if (chip->usb_temp_l >= chip->usbtemp_ntc_temp_low && chip->usb_temp_l < USB_100C)
					count_l++;
			} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
				if (chip->usb_temp_r >= chip->usbtemp_ntc_temp_high && chip->usb_temp_r < USB_100C)
					count_r++;
				if (chip->usb_temp_l >= chip->usbtemp_ntc_temp_high && chip->usb_temp_l < USB_100C)
				count_l++;
			}
			pr_err("countl : %d countr : %d", count_l, count_r);
		}
	}
	if (count_r >= retry_cnt || count_l >= retry_cnt) {
		return true;
	}

	return false;
}

bool oplus_usbtemp_trigger_for_rise_fast_temp(struct oplus_chg_chip *chip, int retry_cnt,
					int count_r, int count_l)
{
	int i = 0;

	if (!chip)
		return false;

	if (oplus_usbtemp_temp_rise_fast_with_batt_temp(chip)) {
		pr_err("in usbtemp rise fast with usbtemp!\n");
		for (i = 1; i <= retry_cnt; i++) {
			mdelay(RETRY_CNT_DELAY);
			get_usb_temp(chip);
			if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
				if ((chip->usb_temp_r >= chip->tbatt_temp/10 + chip->usbtemp_temp_gap_low_with_batt_temp)
						&& chip->usb_temp_r < USB_100C)
					count_r++;
				if ((chip->usb_temp_l >= chip->tbatt_temp/10 + chip->usbtemp_temp_gap_low_with_batt_temp)
						&& chip->usb_temp_l < USB_100C)
					count_l++;
			} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
				if ((chip->usb_temp_r >= chip->tbatt_temp/10 + chip->usbtemp_temp_gap_high_with_batt_temp)
						&& chip->usb_temp_r < USB_100C)
					count_r++;
				if ((chip->usb_temp_l >= chip->tbatt_temp/10 + chip->usbtemp_temp_gap_high_with_batt_temp)
						&& chip->usb_temp_l < USB_100C)
					count_l++;
			}
			pr_err("countl : %d countr : %d", count_l, count_r);
		}
		if (count_r >= retry_cnt || count_l >= retry_cnt) {
			return true;
		}
	}

	return false;
}

bool oplus_usbtemp_trigger_for_rise_fast_without_temp(struct oplus_chg_chip *chip, int retry_cnt,
					int count_r, int count_l, int total_count)
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
		if (oplus_usbtemp_judge_temp_gap(chip, current_temp_l, last_usb_temp_l)
				|| oplus_usbtemp_judge_temp_gap(chip, current_temp_r, last_usb_temp_r)) {
			for (i = 1; i <= retry_cnt; i++) {
				mdelay(RETRY_CNT_DELAY);
				get_usb_temp(chip);
				current_temp_l = chip->usb_temp_l;
				current_temp_r = chip->usb_temp_r;
				if (oplus_usbtemp_judge_temp_gap(chip, current_temp_r, last_usb_temp_r)
						&& chip->usb_temp_r < USB_100C)
					count_r++;
				if (oplus_usbtemp_judge_temp_gap(chip, current_temp_l, last_usb_temp_l)
						&& chip->usb_temp_l < USB_100C)
					count_l++;
				pr_err("countl : %d,countr : %d", count_l, count_r);
			}
			current_temp_l = chip->usb_temp_l;
			current_temp_r = chip->usb_temp_r;
			if ((count_l >= retry_cnt &&  chip->usb_temp_l > USB_30C && chip->usb_temp_l < USB_100C)
					|| (count_r >= retry_cnt &&  chip->usb_temp_r > USB_30C  && chip->usb_temp_r < USB_100C))  {
					count = 0;
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
	struct timespec curr_range_change_first_time = (struct timespec){0};
	struct timespec curr_range_change_last_time = (struct timespec){0};
	struct timespec pre_hi_current_time = (struct timespec){0};
	struct timespec now_time = (struct timespec){0};
	bool usbtemp_first_time_in_curr_range = false;
	static current_read_count = 0;
	struct oplus_chg_chip *chip = (struct oplus_chg_chip *) data;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;
	chg = &chip->pmic_spmi.smb5_chip->chg;
#endif

	if (alarmtimer_get_rtcdev()) {
		alarm_init(&chip->usbtemp_alarm_timer, ALARM_REALTIME, usbtemp_alarm_timer_func);
		INIT_WORK(&chip->usbtemp_restart_work, usbtemp_restart_work);
		chip->usbtemp_timer_stage = OPLUS_USBTEMP_TIMER_STAGE0;
	} else {
		chg_err("Failed to get soc alarm-timer");
	}

	if (chip->usbtemp_max_temp_thr <= 0) {
		chip->usbtemp_max_temp_thr = USB_57C;
	}

	if (chip->usbtemp_temp_up_time_thr <= 0) {
		chip->usbtemp_temp_up_time_thr = 30;
	}
	pr_err("[%s]:run first chip->usbtemp_max_temp_thr[%d], chip->usbtemp_temp_up_time_thr[%d]!",
		__func__, chip->usbtemp_max_temp_thr, chip->usbtemp_temp_up_time_thr);
	while (!kthread_should_stop()) {
		if(chip->chg_ops->oplus_usbtemp_monitor_condition != NULL) {
			wait_event_interruptible(chip->oplus_usbtemp_wq_new_method, chip->usbtemp_check);
		} else {
			pr_err("[oplus_usbtemp_monitor_common_new_method]:condition pointer is NULL");
			return 0;
		}
		if(chip->dischg_flag == true) {
			goto dischg;
		}
		if(chip->chg_ops->get_usbtemp_volt == NULL) {
			pr_err("[oplus_usbtemp_monitor_common_new_method]:get_usbtemp_volt is NULL");
			return 0;
		}
		get_usb_temp(chip);
		if ((chip->usb_temp_l < USB_50C) && (chip->usb_temp_r < USB_50C)) {
			vbus_volt = qpnp_get_prop_charger_voltage_now();
			if (vbus_volt == 0) {
				vbus_volt = chip->charger_volt;
			}
		} else {
			vbus_volt = 0;
		}
		if ((chip->usb_temp_l < USB_40C) && (chip->usb_temp_r < USB_40C)) {
			delay = MAX_MONITOR_INTERVAL;
			total_count = 10;
		} else {
			delay = MIN_MONITOR_INTERVAL;
			total_count = chip->usbtemp_temp_up_time_thr;
		}

		current_read_count = current_read_count + 1;
		if (current_read_count == OPLUS_CHG_CURRENT_READ_COUNT) {
			if (oplus_switching_support_parallel_chg()) {
				chip->usbtemp_batt_current = -(oplus_gauge_get_batt_current() +
						oplus_gauge_get_sub_batt_current());
			} else {
				if (oplus_vooc_get_allow_reading()) {
					chip->usbtemp_batt_current = -oplus_gauge_get_batt_current();
				} else {
					chip->usbtemp_batt_current = -oplus_gauge_get_prev_batt_current();
				}
			}
			current_read_count = 0;
		}

		oplus_update_usbtemp_current_status_new_method(chip);

		batt_current = chip->usbtemp_batt_current;

		if (usbtemp_dbg_curr_status < OPLUS_USBTEMP_LOW_CURR
					|| usbtemp_dbg_curr_status > OPLUS_USBTEMP_HIGH_CURR) {
			if (chip->usbtemp_batt_current > OPLUS_USBTEMP_HIGH_CURR_THRD) {
				chip->usbtemp_curr_status = OPLUS_USBTEMP_HIGH_CURR;
			} else if (chip->usbtemp_batt_current > 0 && chip->usbtemp_batt_current <= OPLUS_USBTEMP_HIGH_CURR_THRD) {
				chip->usbtemp_curr_status = OPLUS_USBTEMP_LOW_CURR;
			}
		} else if (usbtemp_dbg_curr_status == OPLUS_USBTEMP_LOW_CURR
					|| usbtemp_dbg_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
			chip->usbtemp_curr_status = usbtemp_dbg_curr_status;
		}

		if (curr_range_change == false && chip->usbtemp_batt_current < OPLUS_USBTEMP_HIGH_CURR_THRD
				&& chip->usbtemp_pre_batt_current >= OPLUS_USBTEMP_HIGH_CURR_THRD) {
			curr_range_change = true;
			curr_range_change_first_time = current_kernel_time();
		} else if (curr_range_change == false && chip->usbtemp_batt_current < OPLUS_USBTEMP_HIGH_CURR_THRD
				&& chip->usbtemp_change_across_unplug) {
			chip->usbtemp_change_across_unplug = false;
			now_time = current_kernel_time();
			if (now_time.tv_sec - pre_hi_current_time.tv_sec < OPLUS_USBTEMP_CHANGE_RANGE_TIME) {
				curr_range_change = true;
				curr_range_change_first_time = pre_hi_current_time;
				chg_err("reconnected when hi_current, need keep %d seconds",
					OPLUS_USBTEMP_CHANGE_RANGE_TIME + pre_hi_current_time.tv_sec - now_time.tv_sec);
			}
		} else if (curr_range_change == true && chip->usbtemp_batt_current >= OPLUS_USBTEMP_HIGH_CURR_THRD
				&& chip->usbtemp_pre_batt_current < OPLUS_USBTEMP_HIGH_CURR_THRD) {
			curr_range_change = false;
		}

		if (curr_range_change == true && chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
			if (oplus_usbtemp_change_curr_range(chip, retry_cnt,
						usbtemp_first_time_in_curr_range, curr_range_change))  {
				chip->usbtemp_curr_status = OPLUS_USBTEMP_LOW_CURR;
				curr_range_change = false;
			}
			if (usbtemp_first_time_in_curr_range == false) {
				usbtemp_first_time_in_curr_range = true;
			}
			curr_range_change_last_time = current_kernel_time();
			if (curr_range_change_last_time.tv_sec - curr_range_change_first_time.tv_sec >=
						OPLUS_USBTEMP_CHANGE_RANGE_TIME) {
				chip->usbtemp_curr_status = OPLUS_USBTEMP_LOW_CURR;
				curr_range_change = false;
			}
		} else {
			usbtemp_first_time_in_curr_range = false;
		}

		if ((chip->usb_temp_l < USB_40C) && (chip->usb_temp_r < USB_40C)) {
			total_count = OPCHG_LOW_USBTEMP_RETRY_COUNT;
		} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_LOW_CURR) {
			total_count = chip->usbtemp_rise_fast_temp_count_low;
		} else if (chip->usbtemp_curr_status == OPLUS_USBTEMP_HIGH_CURR) {
			total_count = chip->usbtemp_rise_fast_temp_count_high;
		}

		if ((chip->usbtemp_volt_l < USB_50C) && (chip->usbtemp_volt_r < USB_50C) && (vbus_volt < VBUS_VOLT_THRESHOLD))
			delay = VBUS_MONITOR_INTERVAL;

		if (oplus_usbtemp_trigger_for_high_temp(chip, retry_cnt, count_r, count_l)) {
			if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable) ||
				chip->usbtemp_dischg_by_pmic) {
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
			if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable) ||
				    chip->usbtemp_dischg_by_pmic) {
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
		if(condition1 == true) {
			pr_err("jump_to_dischg");
			goto dischg;
		}

		if (oplus_usbtemp_trigger_for_rise_fast_without_temp(chip, retry_cnt, count_r, count_l, total_count))  {
			if (!IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable) ||
					    chip->usbtemp_dischg_by_pmic) {
				chip->dischg_flag = true;
				condition2 = true;
			}
		}

	dischg:
		if ((chip->usb_temp_l < USB_30C || chip->usb_temp_l > USB_100C)
				&& (chip->usb_temp_r < USB_30C || chip->usb_temp_r > USB_100C)) {
			condition1 = false;
			condition2 = false;
			chip->dischg_flag = false;
		}

		if ((condition1 == true || condition2 == true) && chip->dischg_flag == true) {
			condition = (condition1== true ?
				USBTEMP_TRIGGER_CONDITION_1 :
				USBTEMP_TRIGGER_CONDITION_2);
			oplus_chg_track_upload_usbtemp_info(
				chip, condition, last_usb_temp_l, last_usb_temp_r,
				batt_current);
			oplus_usbtemp_dischg_action(chip);
			condition1 = false;
			condition2 = false;
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
			if ((get_eng_version() != HIGH_TEMP_AGING &&
			    !oplus_is_ptcrb_version()) &&
#else
			if (
#endif
			    (oplus_ccdetect_is_gpio(chip) == 0)) {
				chg_err("start delay work for recover charging");
				count = 0;
				last_usb_temp_r = chip->usb_temp_r;
				last_usb_temp_l = chip->usb_temp_l;
				oplus_usbtemp_set_recover(chip);
			}
		} else if (chip->debug_force_usbtemp_trigger) {
			oplus_chg_track_upload_usbtemp_info(
				chip, chip->debug_force_usbtemp_trigger,
				last_usb_temp_l, last_usb_temp_r, batt_current);
			chip->debug_force_usbtemp_trigger = 0;
		}
		msleep(delay);
		chip->usbtemp_pre_batt_current = batt_current;
		if (chip->usbtemp_pre_batt_current > OPLUS_USBTEMP_HIGH_CURR_THRD) {
			pre_hi_current_time = current_kernel_time();
		}
		if (usbtemp_debug & OPEN_LOG_BIT) {
			pr_err("usbtemp: delay %d", delay);
			chg_err("==================usbtemp_volt_l[%d], usb_temp_l[%d], usbtemp_volt_r[%d], usb_temp_r[%d]\n",
				chip->usbtemp_volt_l, chip->usb_temp_l, chip->usbtemp_volt_r, chip->usb_temp_r);
		}
	}
	return 0;
}
EXPORT_SYMBOL(oplus_usbtemp_monitor_common_new_method);

