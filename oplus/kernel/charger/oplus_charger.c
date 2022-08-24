// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/sched/clock.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/oplus_project.h>
#endif
#include <linux/miscdevice.h>

#ifdef CONFIG_OPLUS_CHARGER_MTK
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#include <mt-plat/mtk_boot.h>
#endif
#include <linux/gpio.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
#include <uapi/linux/sched/types.h>
#endif
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/of.h>

#include <linux/bitops.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/spmi.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/rtc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/qpnp/qpnp-adc.h>
#else
#include <uapi/linux/sched/types.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
#include <linux/batterydata-lib.h>
#include <linux/of_batterydata.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/msm_bcl.h>
#endif
#include <linux/ktime.h>
#include <linux/kernel.h>
#endif

#include "oplus_charger.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_short.h"
#include "oplus_adapter.h"
#include "charger_ic/oplus_short_ic.h"
#include "charger_ic/oplus_switching.h"
#include "oplus_debug_info.h"
#include "voocphy/oplus_voocphy.h"
#ifndef WPC_NEW_INTERFACE
#ifndef OPLUS_CUSTOM_OP_DEF
#include "oplus_wireless.h"
#else
#include "oplus_wlchg_policy.h"
#endif
#include "wireless_ic/oplus_chargepump.h"
#else
#include "oplus_wireless.h"
#endif
#include "oplus_chg_ops_manager.h"
#include "oplus_chg_comm.h"
#include "oplus_pps.h"
#include "op_wlchg_v2/oplus_chg_wls.h"
#include "oplus_ufcs.h"
#include "oplus_quirks.h"
#include <linux/jiffies.h>

static struct oplus_chg_chip *g_charger_chip = NULL;

#define FLASH_SCREEN_CTRL_OTA	0X01
#define FLASH_SCREEN_CTRL_DTSI	0X02

#define MAX_UI_DECIMAL_TIME	24
#define UPDATE_TIME		1
#define QC_SWITCH_VOL		7500

#define OPLUS_CHG_UPDATE_INTERVAL_SEC			5
#define OPLUS_CHG_UPDATE_NO_CHARGE_INTERVAL_SEC		10
/* first run after init 10s */
#define OPLUS_CHG_UPDATE_INIT_DELAY	round_jiffies_relative(msecs_to_jiffies(500))
/* update cycle 5s */
#define OPLUS_CHG_UPDATE_INTERVAL(time)	round_jiffies_relative(msecs_to_jiffies(time * 1000))

#define OPLUS_CHG_DEFAULT_CHARGING_CURRENT	512
#define OPLUS_CHG_ONE_HOUR			3600
#define OPLUS_CHG_CAPACITY_PARAMETER		250
#define TEM_ANTI_SHAKE				20
#define SALE_MODE_COOL_DOWN			501
#define SALE_MODE_COOL_DOWN_TWO			502

#define DOUBLE_BATTERY	2

#define CURR_STEP		100
#define PARALLEL_CHECK_TIME	1

#define SOC_NOT_FULL_REPORT		97
#define BTBOVER_TEMP_MAX_INPUT_CURRENT	1000
#define MIN_DELTA_SOC		1

#define OPLUS_BMS_HEAT_THRE 200

int enable_charger_log = 2;
int charger_abnormal_log = 0;
int tbatt_pwroff_enable = 1;
static int mcu_status = 0;
extern bool oplus_is_power_off_charging(struct oplus_vooc_chip *chip);
extern bool oplus_voocphy_chip_is_null(void);

#define charger_xlog_printk(num, fmt, ...)                                                                             \
	do {                                                                                                           \
		if (enable_charger_log >= (int)num) {                                                                  \
			printk(KERN_NOTICE pr_fmt("[OPLUS_CHG][%s]" fmt), __func__, ##__VA_ARGS__);                    \
		}                                                                                                      \
	} while (0)

void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip);
void oplus_chg_turn_on_charging(struct oplus_chg_chip *chip);
void oplus_chg_deviation_disable_charge(void);
void oplus_chg_deviation_enable_charge(void);

static void oplus_chg_smooth_to_soc(struct oplus_chg_chip *chip);
static void oplus_chg_smooth_to_soc_v2(struct oplus_chg_chip *chip, bool force);
static void oplus_chg_variables_init(struct oplus_chg_chip *chip);
static void oplus_chg_update_work(struct work_struct *work);
static void oplus_aging_check_work(struct work_struct *work);
static void oplus_chg_reset_adapter_work(struct work_struct *work);
static void oplus_chg_turn_on_charging_work(struct work_struct *work);
static void oplus_parallel_chg_mos_test_work(struct work_struct *work);
static void oplus_parallel_batt_chg_check_work(struct work_struct *work);
static void oplus_chg_protection_check(struct oplus_chg_chip *chip);
static void oplus_chg_get_battery_data(struct oplus_chg_chip *chip);
static void oplus_chg_check_tbatt_status(struct oplus_chg_chip *chip);
static void oplus_chg_check_tbatt_normal_status(struct oplus_chg_chip *chip);
static void oplus_chg_get_chargerid_voltage(struct oplus_chg_chip *chip);
static void oplus_chg_fast_switch_check(struct oplus_chg_chip *chip);
static void oplus_chg_chargerid_switch_check(struct oplus_chg_chip *chip);
static void oplus_chg_switch(struct oplus_chg_chip *chip);
void oplus_chg_set_input_current_limit(struct oplus_chg_chip *chip);
static void oplus_chg_set_charging_current(struct oplus_chg_chip *chip);
static void oplus_chg_battery_update_status(struct oplus_chg_chip *chip);
static void oplus_chg_pdqc_to_normal(struct oplus_chg_chip *chip);
static void oplus_get_smooth_soc_switch(struct oplus_chg_chip *chip);
static void oplus_chg_pps_config(struct oplus_chg_chip *chip);
static void oplus_chg_pd_config(struct oplus_chg_chip *chip);
static void oplus_chg_qc_config(struct oplus_chg_chip *chip);
static void oplus_chg_voter_charging_start(struct oplus_chg_chip *chip, OPLUS_CHG_STOP_VOTER voter);
static void oplus_chg_check_abnormal_adapter(int vbus_rising);
#if IS_ENABLED(CONFIG_FB) || IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
static int fb_notifier_callback(struct notifier_block *nb, unsigned long event, void *data);
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG)
static int chg_mtk_drm_notifier_callback(struct notifier_block *nb, unsigned long event, void *data);
#endif
void oplus_chg_ui_soc_decimal_init(void);
void oplus_chg_ui_soc_decimal_deinit(void);
int oplus_get_fast_chg_type(void);
extern int print_voocphy_log_buf(void);
extern void voocphy_cpufreq_release(void);
extern void oplus_voocphy_dump_reg(void);
static void oplus_chg_show_ui_soc_decimal(struct work_struct *work);
static void battery_notify_charge_batt_terminal_check(struct oplus_chg_chip *chip);
static int parallel_chg_check_balance_bat_status(void);
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
static void chg_panel_notifier_callback(enum panel_event_notifier_tag tag,
					struct panel_event_notification *notification, void *client_data);
#endif
int oplus_chg_get_curr_time_ms(unsigned long *time_ms);
static void oplus_fg_soft_reset_work(struct work_struct *work);
static void oplus_comm_check_fgreset(struct oplus_chg_chip *chip);
static void oplus_comm_fginfo_reset(struct oplus_chg_chip *chip);

static void oplus_chg_bcc_thread_init(void);

static struct task_struct *oplus_bcc_kthread;

static void quick_mode_check(void);
void oplus_first_enable_adsp_voocphy(void);

extern int sub_gauge_dbg_tbat;
extern int gauge_dbg_tbat;

static int chgr_dbg_vchg = 0;
module_param(chgr_dbg_vchg, int, 0644);
MODULE_PARM_DESC(chgr_dbg_vchg, "debug charger voltage");

static int chgr_dbg_total_time = 0;
module_param(chgr_dbg_total_time, int, 0644);
MODULE_PARM_DESC(chgr_dbg_total_time, "debug charger total time");

bool __attribute__((weak)) oplus_is_ptcrb_version(void)
{
	return false;
}

/****************************************/
#define RESET_MCU_DELAY_30S 6
static int reset_mcu_delay = 0;
static bool mcu_update = false;
static int efttest_count = 1;
static bool high_vol_light = false;
static bool suspend_charger = false;
static bool vbatt_higherthan_4180mv = false;
static bool vbatt_lowerthan_3300mv = false;
#define TIME_INTERVALE_S 10800
u8 soc_store[4] = { 0 };
u8 night_count = 0;
static bool soc_ajusting = false;
static int get_soc_feature(void);
int set_soc_feature(void);
static void get_time_interval(void);
static int aicl_delay_count = 0;
static bool chg_ctrl_by_sale_mode = false;

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
enum cc_mode_type {
	MODE_DEFAULT = 0,
	MODE_SINK,
	MODE_SRC,
	MODE_DRP
};
enum situations_type {
	SITUATION_DEFAULT = 0,
	SITUATION_IDLE,
	SITUATION_OTG,
	SITUATION_CHARGING
};

struct test_kit_typec_port_info g_typec_port_info[] = {
	{
		.name = "typec_port_idle",
		.case_num = 1,
		.status = MODE_SINK,
		.situation = SITUATION_IDLE,
	},
	{}
};

const struct test_feature_cfg g_typec_port_test_cfg = {
	.name = "typec_port_test",
	.test_info = (void *)g_typec_port_info,
	.test_func = test_kit_typec_port_test,
};

bool test_kit_typec_port_check(void *info, char *buf, size_t len, size_t *use_size)
{
	struct test_kit_typec_port_info *typec_port_info = info;
	int situation = SITUATION_DEFAULT;
	bool pass = true;
	bool val = false;

	pr_err("[TYPEC-PORT-CHECK]: buf is NULL\n");
	if (info == NULL) {
		pr_err("[TYPEC-PORT-CHECK]: info is NULL\n");
		return false;
	}
	if (buf == NULL) {
		pr_err("[TYPEC-PORT-CHECK]: buf is NULL\n");
		return false;
	}
	*use_size = 0;

	if (oplus_chg_get_chging_status() == true)
		situation = SITUATION_CHARGING;
	else if (oplus_chg_get_otg_online() == true)
		situation = SITUATION_OTG;
	else
		situation = SITUATION_IDLE;

	if (situation != typec_port_info->situation) {
		*use_size += snprintf(buf + *use_size, len - *use_size,
			"[%s][typec check case: %d],situation expected: %d, actually: %d\n",
			typec_port_info->name, typec_port_info->case_num,
			typec_port_info->situation, situation);
		pass = false;
	}

	if ((g_charger_chip != NULL) && (g_charger_chip->chg_ops != NULL) &&
		(g_charger_chip->chg_ops->check_cc_mode != NULL)) {
		pr_err("check_cc_mode exists!\n");
		val = g_charger_chip->chg_ops->check_cc_mode();

		if (val != typec_port_info->status) {
			*use_size += snprintf(buf + *use_size, len - *use_size,
				"[%s][typec check case: %d],status expected: %d, actually: %d\n",
				typec_port_info->name, typec_port_info->case_num,
				typec_port_info->status, val);
			pass = false;
		}
		return pass;
	}
	else {
		pr_err("check_cc_mode not exists!\n");
		pass = false;
	}

	return pass;
}

static int oplus_typec_port_test_kit_init(struct oplus_chg_chip *chip)
{
	chip->typec_port_test = test_feature_register(&g_typec_port_test_cfg, chip);
	if (IS_ERR_OR_NULL(chip->typec_port_test))
		chg_err("typec_port_test register error");

	return 0;
}

#define DET_DELAY_TIME 5
#define DET_TESTKIT_MAX_TIMES 3
#define DET_SUCCESS 0
static int g_det_typec_count;
static struct delayed_work det_typec_testkit_work;
static void oplus_chg_det_typec_testkit_work(struct work_struct *work)
{
	int ret;

	ret = 	test_kit_reg_typec_port_check(test_kit_typec_port_check);
	if (g_det_typec_count < DET_TESTKIT_MAX_TIMES &&
		ret != DET_SUCCESS) {
		g_det_typec_count++;
		pr_err("detect test kit module not ready, redetect! count:%d\n", g_det_typec_count);
		schedule_delayed_work(&det_typec_testkit_work, msecs_to_jiffies(DET_DELAY_TIME * 1000));
	}
}

#define SWITCH1_GPIO_INFO_INDEX	0
#define SWITCH2_GPIO_INFO_INDEX	0
#define UART_TX_GPIO_INFO_INDEX	0
#define UART_RX_GPIO_INFO_INDEX	1

#if IS_ENABLED(CONFIG_OPLUS_CHARGER_MTK)
struct test_kit_soc_gpio_info g_chg_switch1_gpio_info[] = {
	{
		.name = "switch1_gpio",
		.is_out = true,
		.is_high = false,
		.func = 0,
		.pull = 1,/* 0:no pull; 1:pull down;2:pull up */
		.drive = 2,
	},
	{}
};

struct test_kit_soc_gpio_info g_chg_switch2_gpio_info[] = {
	{
		.name = "switch2_gpio",
		.is_out = true,
		.is_high = false,
		.func = 0,
		.pull = 0,
		.drive = 2,
	},
	{}
};

struct test_kit_soc_gpio_info g_chg_uart_gpio_info[] = {
	{
		.name = "uart_tx",
		.is_out = false,
		.is_high = false,
		.func = 0,
		.pull = 0,
		.drive = 2,
	},
	{
		.name = "uart_rx",
		.is_out = false,
		.is_high = false,
		.func = 0,
		.pull = 0,
		.drive = 2,
	},
	{}
};
#else
struct test_kit_soc_gpio_info g_chg_switch1_gpio_info[] = {
	{
		.name = "switch1_gpio",
		.is_out = true,
		.is_high = false,
		.func = 0,
		.pull = 1,/* 0:no pull; 1:pull down;2:pull up */
		.drive = 2,
	},
	{}
};

struct test_kit_soc_gpio_info g_chg_switch2_gpio_info[] = {
	{
		.name = "switch2_gpio",
		.is_out = true,
		.is_high = false,
		.func = 0,
		.pull = 0,
		.drive = 2,
	},
	{}
};

struct test_kit_soc_gpio_info g_chg_uart_gpio_info[] = {
	{
		.name = "uart_tx",
		.is_out = false,
		.is_high = false,
		.func = 0,
		.pull = 1,
		.drive = 2,
	},
	{
		.name = "uart_rx",
		.is_out = false,
		.is_high = false,
		.func = 0,
		.pull = 1,
		.drive = 2,
	},
	{}
};
#endif

const struct test_feature_cfg g_chg_switch1_gpio_test_cfg = {
	.name = "chg_switch1_gpio_test",
	.test_info = (void *)g_chg_switch1_gpio_info,
#if IS_ENABLED(CONFIG_OPLUS_CHARGER_MTK)
	.test_func = test_kit_mtk_soc_gpio_test,
#else
	.test_func = test_kit_qcom_soc_gpio_test,
#endif
};

const struct test_feature_cfg g_chg_switch2_gpio_test_cfg = {
	.name = "chg_switch2_gpio_test",
	.test_info = (void *)g_chg_switch2_gpio_info,
#if IS_ENABLED(CONFIG_OPLUS_CHARGER_MTK)
	.test_func = test_kit_mtk_soc_gpio_test,
#else
	.test_func = test_kit_qcom_soc_gpio_test,
#endif
};

const struct test_feature_cfg g_chg_uart_gpio_test_cfg = {
	.name = "chg_uart_gpio_test",
	.test_info = (void *)g_chg_uart_gpio_info,
#if IS_ENABLED(CONFIG_OPLUS_CHARGER_MTK)
	.test_func = test_kit_mtk_soc_gpio_test,
#else
	.test_func = test_kit_qcom_soc_gpio_test,
#endif
};

#define DETECT_DELAY_TIME 10
#define DETECT_TESTKIT_MAX_TIMES 3
#define DETECT_SUCCESS 0
static int g_det_gpio_count;
static struct delayed_work det_gpio_testkit_work;
static void oplus_chg_det_gpio_testkit_work(struct work_struct *work)
{
	int ret;

	pr_info("test_kit detect work\n");

#if IS_ENABLED(CONFIG_OPLUS_CHARGER_MTK)
	ret = test_kit_reg_mtk_soc_gpio_check(test_kit_mtk_gpio_check);
#else
	ret = test_kit_reg_qcom_soc_gpio_check(test_kit_qcom_gpio_check);
#endif

	if (g_det_gpio_count < DETECT_TESTKIT_MAX_TIMES &&
		ret != DETECT_SUCCESS) {
		g_det_gpio_count++;
		pr_info("detect test kit module not ready, redetect! count:%d\n", g_det_gpio_count);
		schedule_delayed_work(&det_gpio_testkit_work, msecs_to_jiffies(DETECT_DELAY_TIME * 1000));
	}
}
#endif /* CONFIG_OPLUS_CHG_TEST_KIT */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static struct timespec current_kernel_time(void)
{
	struct timespec ts;
	getnstimeofday(&ts);
	return ts;
}
#endif
__maybe_unused static bool is_wls_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->wls_ocm)
		chip->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!chip->wls_ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->comm_ocm)
		chip->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!chip->comm_ocm;
}

bool oplus_chg_is_wls_present(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	union oplus_chg_mod_propval temp_val = { 0 };
	bool present = false;
	int rc;

	if (!chip) {
		return false;
	}

	if (!is_wls_ocm_available(chip))
		return false;

	rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_ONLINE_KEEP, &temp_val);
	if (rc < 0)
		present = false;
	else
		present = temp_val.intval;
	rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_PRESENT, &temp_val);
	if (rc >= 0)
		present = present || (!!temp_val.intval);
	return present;
}

bool oplus_chg_is_wls_fastchg_started(void)
{
	int rc = 0;
	union oplus_chg_mod_propval pval = {
		0,
	};
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return false;
	}

	if (!is_wls_ocm_available(chip))
		return false;
	rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_FASTCHG_STATUS, &pval);
	if (rc < 0)
		return false;

	return !!pval.intval;
}

bool oplus_chg_is_wls_ffc(void)
{
	bool norchg_ffc = false;
	bool fastchg_started = false;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return false;
	}

	if (!is_comm_ocm_available(chip))
		return false;

	fastchg_started = oplus_chg_is_wls_fastchg_started();
	norchg_ffc = oplus_chg_comm_get_ffc_status(chip->comm_ocm) != FFC_DEFAULT ? true : false;

	return (fastchg_started | norchg_ffc);
}

int oplus_chg_get_wls_type(void)
{
	int rc = 0;
	union oplus_chg_mod_propval pval = {
		0,
	};
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return OPLUS_CHG_WLS_UNKNOWN;
	}

	if (!is_wls_ocm_available(chip))
		return OPLUS_CHG_WLS_UNKNOWN;
	rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &pval);
	if (rc < 0)
		return OPLUS_CHG_WLS_UNKNOWN;

	return pval.intval;
}

static bool oplus_chg_is_wls_fast_type(void)
{
	if (oplus_chg_get_wls_type() == OPLUS_CHG_WLS_SVOOC || oplus_chg_get_wls_type() == OPLUS_CHG_WLS_PD_65W) {
		return true;
	}
	return false;
}

bool oplus_chg_is_wls_fw_upgrading(void)
{
	int rc = 0;
	union oplus_chg_mod_propval pval = {
		0,
	};
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return false;
	}

	if (!is_wls_ocm_available(chip)) {
		return false;
	}

	rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_FW_UPGRADING, &pval);
	if (rc < 0)
		return false;

	return !!pval.intval;
}

int oplus_get_report_batt_temp(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int report_temp = 250;

	if (!chip) {
		chg_err("chip is null,return 25C\n");
		return 250;
	}

	if (chip->subboard_temp != TEMPERATURE_INVALID)
		report_temp = chip->subboard_temp;
	else
		report_temp = chip->tbatt_temp;

	if (chip->bms_heat_temp_compensation != 0 && oplus_chg_match_temp_for_chging() < OPLUS_BMS_HEAT_THRE)
		report_temp = report_temp - chip->bms_heat_temp_compensation;

	return report_temp;
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
int oplus_usb_get_property(struct power_supply *psy, enum power_supply_property psp, union power_supply_propval *val)
{
	int ret = 0;

	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return -EINVAL;
	}

	if (chip->charger_exist) {
		if ((chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP ||
		     chip->charger_type == POWER_SUPPLY_TYPE_USB_PD_SDP) &&
		    chip->stop_chg == 1) {
			chip->usb_online = true;
			if (chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)
				chip->usb_psd.type = POWER_SUPPLY_TYPE_USB_CDP;
			else
				chip->usb_psd.type = POWER_SUPPLY_TYPE_USB;
		} else if (!(chip->charger_type == POWER_SUPPLY_TYPE_USB ||
			     chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
			chip->usb_online = false;
		}
	} else {
		chip->usb_online = false;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 500000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 5000000;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->usb_online;
		break;
	default:
		pr_err("get prop %d is not supported in usb\n", psp);
		ret = -EINVAL;
		break;
	}
	return ret;
}
EXPORT_SYMBOL(oplus_usb_get_property);

static void usb_update(struct oplus_chg_chip *chip)
{
	if (!chip) {
		return;
	}

	if (chip->charger_exist) {
		if (chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_PD_SDP) {
			chip->usb_online = true;
			chip->usb_psd.type = POWER_SUPPLY_TYPE_USB;
		} else if (chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
			chip->usb_online = true;
			chip->usb_psd.type = POWER_SUPPLY_TYPE_USB_CDP;
		}
	} else {
		chip->usb_online = false;
	}
	power_supply_changed(chip->usb_psy);
}
#endif

enum power_supply_property oplus_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

int oplus_ac_get_property(struct power_supply *psy, enum power_supply_property psp, union power_supply_propval *val)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return -EINVAL;
	}

	if (chip->charger_exist) {
		if ((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP || suspend_charger) ||
		    (oplus_vooc_get_fastchg_started() == true) || (oplus_vooc_get_fastchg_to_normal() == true) ||
		    (oplus_vooc_get_fastchg_to_warm() == true) || (oplus_vooc_get_fastchg_dummy_started() == true) ||
		    (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
		    (oplus_vooc_get_btb_temp_over() == true) ||
		    (!chip->mmi_fastchg && chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;
		}
	} else {
		if ((oplus_vooc_get_fastchg_started() == true) || (oplus_vooc_get_fastchg_to_normal() == true) ||
		    (oplus_vooc_get_fastchg_to_warm() == true) || (oplus_vooc_get_fastchg_dummy_started() == true) ||
		    (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
		    (oplus_vooc_get_btb_temp_over() == true) || chip->mmi_fastchg == 0) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;
		}
#ifdef OPLUS_CUSTOM_OP_DEF
		if (chip->hiz_gnd_cable && chip->icon_debounce)
			chip->ac_online = true;
#endif
	}
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->ac_online;
		break;
	default:
		pr_err("get prop %d is not supported in ac\n", psp);
		ret = -EINVAL;
		break;
	}

	if (chip->ac_online) {
		charger_xlog_printk(CHG_LOG_CRTI, "%d %d %d %d %d %d %d %d %d ac_online:%d\n", chip->charger_type,
				    oplus_vooc_get_fastchg_started(), oplus_vooc_get_fastchg_to_normal(),
				    oplus_vooc_get_fastchg_to_warm(), oplus_vooc_get_fastchg_dummy_started(),
				    oplus_vooc_get_adapter_update_status(), oplus_vooc_get_btb_temp_over(),
				    chip->mmi_fastchg, chip->charger_exist, chip->ac_online);
	}

	return ret;
}
EXPORT_SYMBOL(oplus_ac_get_property);

int oplus_battery_property_is_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return ret;
	}

	switch (psp) {
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (chip && chip->smart_charging_screenoff) {
			ret = 1;
		} else {
			ret = 0;
		}
		break;
#endif
	default:
		ret = 0;
		break;
	}
	return ret;
}
EXPORT_SYMBOL(oplus_battery_property_is_writeable);

int oplus_battery_set_property(struct power_supply *psy, enum power_supply_property psp,
			       const union power_supply_propval *val)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return ret;
	}

	switch (psp) {
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (chip->smart_charging_screenoff) {
			oplus_smart_charge_by_shell_temp(chip, val->intval);
			break;
		} else {
			ret = -EINVAL;
			break;
		}
#endif
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
EXPORT_SYMBOL(oplus_battery_set_property);

#define OPLUS_MIDAS_CHG_DEBUG 1
#ifdef OPLUS_MIDAS_CHG_DEBUG
#define midas_debug(fmt, args...) pr_notice("[OPLUS_MIDAS_CHG_DEBUG]" fmt, ##args)
#else
#define midas_debug(fmt, args...)
#endif /* OPLUS_MIDAS_CHG_DEBUG */

static struct oplus_midas_chg {
	int cali_passed_chg;
	int passed_chg;
	int accu_delta;

	int prev_chg_stat; /* 1--charger-on, 0 -- otherwise */

	unsigned int reset_counts;
} midas_chg;

static void oplus_midas_chg_info(const char *name)
{
	return;
}

/* TODO: how to determine passedchg is reset precisely ? */
#define __abs(a, b) ((a > b) ? (a - b) : (b - a))
#define ZEROTH 5
#define COMSUMETH 10
static bool oplus_midas_passedchg_reset(int prev, int val)
{
	if (__abs(val, prev) > COMSUMETH) {
		return true;
	} else if (__abs(val, 0) > ZEROTH) {
		return false;
	}

	if (prev < 0 && val - prev >= ZEROTH) {
		return true;
	}
	if (prev >= 0 && val < prev) {
		return true;
	}
	return false;
}

static int oplus_chg_voocphy_get_cp_ibus(void)
{
	int cp_current = 0;

	if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
		cp_current = oplus_voocphy_get_ichg();
		chg_err("cp_current = %d, chip->icharging = %d\n", cp_current, g_charger_chip->icharging);
	}
	return cp_current;
}

static int oplus_chg_voocphy_get_slave_cp_ibus(void)
{
	int slave_cp_current = 0;

	if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
		slave_cp_current = oplus_voocphy_get_slave_ichg();
		chg_err("slave_cp_current = %d, chip->icharging = %d\n", slave_cp_current, g_charger_chip->icharging);
	}
	return slave_cp_current;
}

#define DEVIATION_NUM 30
static int oplus_chg_voocphy_get_cp_vbat_deviation(void)
{
	int cp_vbat, gauge_bat, vbat_deviation;
	u8 cp_adc_reg;
	int retry = 5;

	if (oplus_chg_get_voocphy_support() == NO_VOOCPHY) {
		return 0;
	}

	if (oplus_voocphy_get_external_gauge_support())
		return 0;

	while (retry > 0) {
		oplus_voocphy_get_adc_enable(&cp_adc_reg);
		if (cp_adc_reg == 0)
			oplus_voocphy_set_adc_enable(true);
		cp_vbat = oplus_voocphy_get_cp_vbat();

		gauge_bat = oplus_gauge_get_batt_mvolts();
		vbat_deviation = (gauge_bat > cp_vbat) ? gauge_bat - cp_vbat : cp_vbat - gauge_bat;

		if (cp_vbat == 0 || gauge_bat == 0 || vbat_deviation >= DEVIATION_NUM) {
			msleep(100);
			retry--;
		} else {
			break;
		}
	}

	if (g_charger_chip->charger_exist == false) {
		oplus_voocphy_get_adc_enable(&cp_adc_reg);
		if (cp_adc_reg == 1)
			oplus_voocphy_set_adc_enable(false);
	}
	chg_err("cp_vbat_deviation_read cp_vbat = %d gauge_bat=%d vbat_deviation=%d retry:%d\n", cp_vbat, gauge_bat,
		vbat_deviation, retry);

	return vbat_deviation;
}

static void oplus_midas_chg_data_init(void)
{
	int val, ret;
	midas_chg.accu_delta = 0;
	midas_chg.reset_counts = 0;
	if (oplus_vooc_get_allow_reading() == true) {
		ret = oplus_gauge_get_passedchg(&val);
		if (ret) {
			pr_err("%s: get passedchg error %d\n", __FUNCTION__, val);
			midas_chg.cali_passed_chg = midas_chg.passed_chg = 0;
		} else {
			midas_chg.cali_passed_chg = midas_chg.passed_chg = val;
		}
	} else {
		pr_err("%s: not allow reading", __FUNCTION__);
	}
}

static void oplus_midas_chg_processing(struct oplus_chg_chip *chip)
{
	static int inited = 0;
	int val, ret = 0;

	if (!inited) {
		oplus_midas_chg_data_init();
		inited = 1;
		return;
	}

	if (chip->charger_exist) {
		midas_chg.prev_chg_stat = 1;
		oplus_midas_chg_info(__FUNCTION__);
		return;
	}

	if (oplus_vooc_get_allow_reading() == true) {
		ret = oplus_gauge_get_passedchg(&val);
		if (ret) {
			pr_err("%s: get passedchg error %d\n", __FUNCTION__, val);
			return;
		}
	} else {
		pr_err("%s: not allow reading", __FUNCTION__);
	}
	if (midas_chg.prev_chg_stat) {
		/* re-init passedchg after charge */
		midas_chg.cali_passed_chg = midas_chg.passed_chg = val;
		midas_chg.accu_delta = 0;
		midas_chg.prev_chg_stat = 0;
	} else {
		if (oplus_midas_passedchg_reset(midas_chg.passed_chg, val)) {
			/* handling passedchg reset ... */
			midas_chg.cali_passed_chg += midas_chg.accu_delta;
			midas_chg.reset_counts++;
		} else {
			/* accumulate normally */
			midas_chg.accu_delta = val - midas_chg.passed_chg;
			midas_chg.cali_passed_chg += midas_chg.accu_delta;
		}
		midas_chg.passed_chg = val;
	}

	oplus_midas_chg_info(__FUNCTION__);
}

#define OPLUS_NS_TO_MS (1000 * 1000)
int oplus_chg_get_curr_time_ms(unsigned long *time_ms)
{
	u64 ts_nsec;

	ts_nsec = local_clock();
	*time_ms = ts_nsec / OPLUS_NS_TO_MS;

	return *time_ms;
}

#define KPOC_FORCE_VBUS_MV 5000
#define FORCE_VBUS_5V_TIME 10000
int oplus_battery_get_property(struct power_supply *psy, enum power_supply_property psp,
			       union power_supply_propval *val)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_charger_chip;
	union oplus_chg_mod_propval temp_val = {
		0,
	};
	int batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
	bool wls_online = false;
	static int pre_batt_status;

	unsigned long cur_chg_time = 0;

	if (chip == NULL) {
		pr_err("get prop %d is not ready in batt\n", psp);
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (chip->wls_status_keep) {
			val->intval = pre_batt_status;
		} else {
			if (oplus_chg_show_vooc_logo_ornot()) {
				if (oplus_vooc_get_fastchg_started() || oplus_vooc_get_fastchg_to_normal() ||
				    oplus_vooc_get_fastchg_to_warm() || oplus_vooc_get_fastchg_dummy_started()) {
					if (chip->prop_status != POWER_SUPPLY_STATUS_FULL && chip->mmi_chg)
						val->intval = POWER_SUPPLY_STATUS_CHARGING;
					else
						val->intval = chip->prop_status;
				} else {
					val->intval = chip->prop_status;
				}
				if (chip->tbatt_status == BATTERY_STATUS__HIGH_TEMP ||
				    chip->tbatt_status == BATTERY_STATUS__LOW_TEMP ||
				    chip->tbatt_status == BATTERY_STATUS__REMOVED)
					val->intval = chip->prop_status;
			} else if (!chip->authenticate) {
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			} else {
				val->intval = chip->prop_status;
			}
			if (oplus_wpc_get_online_status() || oplus_chg_is_wls_present())
				pre_batt_status = val->intval;
		}
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (is_wls_ocm_available(chip)) {
			ret = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_PRESENT, &temp_val);
			if (ret == 0)
				wls_online = !!temp_val.intval;
			else
				ret = 0;
		}

		if (is_comm_ocm_available(chip) && wls_online)
			batt_health = oplus_chg_comm_get_batt_health(chip->comm_ocm);

		if ((batt_health == POWER_SUPPLY_HEALTH_UNKNOWN) || (batt_health == POWER_SUPPLY_HEALTH_GOOD)) {
			val->intval = oplus_chg_get_prop_batt_health(chip);
		} else {
			val->intval = batt_health;
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = chip->batt_exist;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (chip->vooc_show_ui_soc_decimal == true && chip->decimal_control) {
			val->intval = (chip->ui_soc_integer + chip->ui_soc_decimal) / 1000;
		} else {
			val->intval = chip->ui_soc;
		}
		if (val->intval > 100) {
			val->intval = 100;
		}

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
#ifdef CONFIG_OPLUS_CHARGER_MTK
		val->intval = chip->batt_volt;
#else
		val->intval = chip->batt_volt * 1000;
#endif
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
#ifdef CONFIG_OPLUS_CHARGER_MTK
		val->intval = chip->batt_volt_min;
#else
		val->intval = chip->batt_volt_min * 1000;
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (oplus_chg_get_voocphy_support() == NO_VOOCPHY && oplus_vooc_get_fastchg_started() == true) {
			chip->icharging = oplus_gauge_get_prev_batt_current();
		} else {
			chip->icharging = oplus_gauge_get_batt_current();
		}
		val->intval = chip->icharging;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (oplus_vooc_get_fastchg_started() == true)
			val->intval = oplus_get_report_batt_temp() - chip->offset_temp;
		else
			val->intval = oplus_get_report_batt_temp() - chip->offset_temp;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (oplus_vooc_get_fastchg_started() == true &&
		    (chip->vbatt_num == 2 || is_vooc_support_single_batt_svooc() == true) &&
		    oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC) {
			val->intval = 10000;
		} else if (oplus_vooc_get_fastchg_started() == true &&
			   (!g_charger_chip->external_gauge && !oplus_voocphy_chip_is_null()) &&
			   oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC) {
			val->intval = 10000;
		} else {
			if (oplus_chg_get_voocphy_support() == NO_VOOCPHY) {
				val->intval = chip->charger_volt;
			} else {
				val->intval = oplus_chg_get_charger_voltage();
			}
		}
		if (oplus_is_power_off_charging(NULL) &&
		    (oplus_chg_get_curr_time_ms(&cur_chg_time) < FORCE_VBUS_5V_TIME))
			val->intval = KPOC_FORCE_VBUS_MV;

		if (is_wls_ocm_available(chip)) {
			oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_ONLINE, &temp_val);
			if (!!temp_val.intval) {
				ret = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_VOLTAGE_NOW,
								 &temp_val);
				val->intval = temp_val.intval / 1000;
			}
		}
		break;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = chip->ui_soc * chip->batt_capacity_mah * 1000 / 100;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 2000;
		break;
#endif
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = chip->batt_fcc * 1000;
		break;
	default:
		pr_err("get prop %d is not supported in batt\n", psp);
		ret = -EINVAL;
		break;
	}
	return ret;
}
EXPORT_SYMBOL(oplus_battery_get_property);

static ssize_t proc_batt_param_noplug_write(struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	return len;
}

static int noplug_temperature = 0;
static int noplug_batt_volt_max = 0;
static int noplug_batt_volt_min = 0;
static ssize_t proc_batt_param_noplug_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[128] = { 0 };
	int len = 0;

	sprintf(read_data, "%d %d %d", noplug_temperature, noplug_batt_volt_max, noplug_batt_volt_min);
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations batt_param_noplug_proc_fops = {
	.write = proc_batt_param_noplug_write,
	.read = proc_batt_param_noplug_read,
};
#else
static const struct proc_ops batt_param_noplug_proc_fops = {
	.proc_write = proc_batt_param_noplug_write,
	.proc_read = proc_batt_param_noplug_read,
	.proc_lseek = seq_lseek,
};
#endif

static int init_proc_batt_param_noplug(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("batt_param_noplug", 0664, NULL, &batt_param_noplug_proc_fops);
	if (!p) {
		chg_err("proc_create  fail!\n");
	}
	return 0;
}

static ssize_t proc_tbatt_pwroff_write(struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	char buffer[2] = { 0 };

	if (len > 2) {
		return -EFAULT;
	}
	if (copy_from_user(buffer, buf, 2)) {
		chg_err("%s:  error.\n", __func__);
		return -EFAULT;
	}
	if (buffer[0] == '0') {
		tbatt_pwroff_enable = 0;
	} else if (buffer[0] == '1') {
		tbatt_pwroff_enable = 1;
		oplus_tbatt_power_off_task_wakeup();
	} else if (buffer[0] == '2') {
		g_charger_chip->flash_screen_ctrl_status &= (~FLASH_SCREEN_CTRL_OTA);
	} else if (buffer[0] == '3') {
		g_charger_chip->flash_screen_ctrl_status |= FLASH_SCREEN_CTRL_OTA;
	}
	chg_err("%s:tbatt_pwroff_enable = %d. flash_screen_ctrl_status=%d\n", __func__, tbatt_pwroff_enable,
		g_charger_chip->flash_screen_ctrl_status);
	return len;
}

static ssize_t proc_tbatt_pwroff_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[3] = { 0 };
	int len = 0;

	if (tbatt_pwroff_enable == 1) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	read_data[1] = '\0';
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations tbatt_pwroff_proc_fops = {
	.write = proc_tbatt_pwroff_write,
	.read = proc_tbatt_pwroff_read,
};
#else
static const struct proc_ops tbatt_pwroff_proc_fops = {
	.proc_write = proc_tbatt_pwroff_write,
	.proc_read = proc_tbatt_pwroff_read,
	.proc_lseek = seq_lseek,
};
#endif

static int init_proc_tbatt_pwroff(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("tbatt_pwroff", 0664, NULL, &tbatt_pwroff_proc_fops);
	if (!p) {
		chg_err("proc_create  fail!\n");
	}
	return 0;
}

static ssize_t chg_log_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = { 0 };

	if (len > sizeof(write_data)) {
		chg_err("bat_log_write error.\n");
		return -EFAULT;
	}
	if (copy_from_user(&write_data, buff, len)) {
		chg_err("bat_log_write error.\n");
		return -EFAULT;
	}
	if (write_data[0] == '1') {
		charger_xlog_printk(CHG_LOG_CRTI, "enable battery driver log system\n");
		enable_charger_log = 1;
	} else if ((write_data[0] >= '2') && (write_data[0] <= '9')) {
		charger_xlog_printk(CHG_LOG_CRTI, "enable battery driver log system:2\n");
		enable_charger_log = 2;
	} else {
		charger_xlog_printk(CHG_LOG_CRTI, "Disable battery driver log system\n");
		enable_charger_log = 0;
	}
	return len;
}

static ssize_t chg_log_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[32] = { 0 };
	int len = 0;

	if (enable_charger_log == 1) {
		read_data[0] = '1';
	} else if (enable_charger_log == 2) {
		read_data[0] = '2';
	} else {
		read_data[0] = '0';
	}
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations chg_log_proc_fops = {
	.write = chg_log_write,
	.read = chg_log_read,
};
#else
static const struct proc_ops chg_log_proc_fops = {
	.proc_write = chg_log_write,
	.proc_read = chg_log_read,
	.proc_lseek = seq_lseek,
};
#endif

static int init_proc_chg_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("charger_log", 0664, NULL, &chg_log_proc_fops);
	if (!p) {
		chg_err("proc_create chg_log_proc_fops fail!\n");
	}
	return 0;
}

static void oplus_chg_set_awake(struct oplus_chg_chip *chip, bool awake);

static ssize_t chg_cycle_read(struct file *file, char __user *buff, size_t count, loff_t *ppos)
{
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;

	if (g_charger_chip->notify_code & (1 << NOTIFY_GAUGE_I2C_ERR)) {
		strcpy(read_data, "FAIL");
	} else {
		strcpy(read_data, "PASS");
	}
	len = sprintf(page, "%s", read_data);
	if (len > *ppos) {
		len -= *ppos;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		chg_err("chg_cycle_read error.\n");
		return -EFAULT;
	}
	*ppos += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t chg_cycle_write(struct file *file, const char __user *buff,
			       size_t count, loff_t *ppos)
{
	char proc_chg_cycle_data[16];
	unsigned char *buf;
	int val = 0;

	if (count >= 16) {
		count = 16;
	}
	if (copy_from_user(&proc_chg_cycle_data, buff, count)) {
		chg_err("chg_cycle_write error.\n");
		return -EFAULT;
	}
	if ((strncmp(proc_chg_cycle_data, "en808", 5) == 0) ||
	    (strncmp(proc_chg_cycle_data, "user_enable", 11) == 0)) {
		if(g_charger_chip->unwakelock_chg == 1) {
			charger_xlog_printk(CHG_LOG_CRTI, "unwakelock testing , this test not allowed.\n");
			return -EPERM;
		}
		if (strncmp(proc_chg_cycle_data, "en808", 5) == 0) {
			g_charger_chip->chg_cycle_status &= ~(int)CHG_CYCLE_VOTER__ENGINEER;
		} else if (g_charger_chip->chg_cycle_status & CHG_CYCLE_VOTER__USER) {
			g_charger_chip->chg_cycle_status &= ~(int)CHG_CYCLE_VOTER__USER;
		} else {
			chg_err("user_enable already true %d\n", g_charger_chip->chg_cycle_status);
			return -EPERM;
		}

		charger_xlog_printk(CHG_LOG_CRTI, "%s allow charging status=%d\n",
			proc_chg_cycle_data, g_charger_chip->chg_cycle_status);
		if (g_charger_chip->chg_cycle_status != CHG_CYCLE_VOTER__NONE) {
			charger_xlog_printk(CHG_LOG_CRTI, "voter not allow charging\n");
			return -EPERM;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "allow charging.\n");
		if (oplus_voocphy_get_bidirect_cp_support()) {
			oplus_voocphy_set_chg_auto_mode(false);
		}
		g_charger_chip->chg_ops->charger_unsuspend();
		g_charger_chip->chg_ops->charging_enable();
		g_charger_chip->mmi_chg = 1;
		g_charger_chip->mmi_fastchg = 1;
		g_charger_chip->stop_chg = 1;
		g_charger_chip->batt_full = false;
		g_charger_chip->total_time = 0;
		g_charger_chip->charging_state = CHARGING_STATUS_CCCV;
		reset_mcu_delay = 0;
		if (g_charger_chip->dual_charger_support) {
			g_charger_chip->slave_charger_enable = false;
			oplus_chg_set_charging_current(g_charger_chip);
		}
		oplus_chg_set_input_current_limit(g_charger_chip);
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_adsp_voocphy_turn_on();
		}
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			oplus_voocphy_reset_fastchg_state();
		}
		if (g_charger_chip->internal_gauge_with_asic == 1 && mcu_status == 1) {
			oplus_vooc_set_mcu_sleep();
			mcu_status = 0;
		}
		cancel_delayed_work_sync(&g_charger_chip->update_work);
		schedule_delayed_work(&g_charger_chip->update_work, round_jiffies_relative(msecs_to_jiffies(1500)));
		charger_xlog_printk(CHG_LOG_CRTI, "wake up update_work\n");
	} else if ((strncmp(proc_chg_cycle_data, "dis808", 6) == 0) ||
		    (strncmp(proc_chg_cycle_data, "user_disable", 12) == 0)) {
		if(g_charger_chip->unwakelock_chg == 1) {
			charger_xlog_printk(CHG_LOG_CRTI, "unwakelock testing , this test not allowed.\n");
			return -EPERM;
		}
		if (strncmp(proc_chg_cycle_data, "dis808", 5) == 0) {
			g_charger_chip->chg_cycle_status |= (int)CHG_CYCLE_VOTER__ENGINEER;
		} else if ((g_charger_chip->chg_cycle_status & CHG_CYCLE_VOTER__USER) == 0) {
			g_charger_chip->chg_cycle_status |= (int)CHG_CYCLE_VOTER__USER;
		} else {
			chg_err("user_disable already true %d\n", g_charger_chip->chg_cycle_status);
			return -EPERM;
		}

		charger_xlog_printk(CHG_LOG_CRTI, "%s not allow charging status=%d\n",
			proc_chg_cycle_data, g_charger_chip->chg_cycle_status);
		charger_xlog_printk(CHG_LOG_CRTI, "not allow charging.\n");
		g_charger_chip->chg_ops->charging_disable();
		oplus_chg_suspend_charger();
		if (oplus_voocphy_get_bidirect_cp_support() && g_charger_chip->chg_ops->check_chrdet_status()) {
			oplus_voocphy_set_chg_auto_mode(true);
		}
		g_charger_chip->mmi_chg = 0;
		g_charger_chip->stop_chg = 0;
		g_charger_chip->batt_full = false;
		g_charger_chip->total_time = 0;
		g_charger_chip->charging_state = CHARGING_STATUS_CCCV;
		if (oplus_vooc_get_fastchg_started() == true)
			g_charger_chip->mmi_fastchg = 0;
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_adsp_voocphy_turn_off();
		}

		if (oplus_chg_get_chargerid_switch_val() == 1 ||
			((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
		    !g_charger_chip->chg_ops->get_chargerid_switch_val)) {
			oplus_vooc_turn_off_fastchg();
		}
	} else if (strncmp(proc_chg_cycle_data, "wakelock", 8) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "set wakelock.\n");
		g_charger_chip->unwakelock_chg = 0;
		oplus_chg_set_awake(g_charger_chip, true);
		if (oplus_voocphy_get_bidirect_cp_support()) {
			oplus_voocphy_set_chg_auto_mode(false);
		}
		g_charger_chip->chg_ops->charger_unsuspend();
		g_charger_chip->chg_ops->charging_enable();
		g_charger_chip->mmi_chg = 1;
		g_charger_chip->stop_chg = 1;
		g_charger_chip->total_time = 0;
		g_charger_chip->batt_full = false;
		g_charger_chip->charging_state = CHARGING_STATUS_CCCV;
		g_charger_chip->chg_powersave = false;
		if (g_charger_chip->chg_ops->oplus_chg_wdt_enable)
			g_charger_chip->chg_ops->oplus_chg_wdt_enable(true);
		if (g_charger_chip->mmi_fastchg == 0) {
			oplus_chg_clear_chargerid_info();
		}
		g_charger_chip->mmi_fastchg = 1;
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_adsp_voocphy_turn_on();
		}
	} else if (strncmp(proc_chg_cycle_data, "unwakelock", 10) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "set unwakelock.\n");
		g_charger_chip->chg_ops->charging_disable();
		if (oplus_voocphy_get_bidirect_cp_support() && g_charger_chip->chg_ops->check_chrdet_status()) {
			oplus_voocphy_set_chg_auto_mode(true);
		}
		g_charger_chip->mmi_chg = 0;
		g_charger_chip->stop_chg = 0;
		g_charger_chip->batt_full = false;
		g_charger_chip->charging_state = CHARGING_STATUS_CCCV;
		g_charger_chip->unwakelock_chg = 1;
		g_charger_chip->chg_powersave = true;
		if (oplus_vooc_get_fastchg_started() == true) {
			oplus_vooc_turn_off_fastchg();
			g_charger_chip->mmi_fastchg = 0;
		}
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_adsp_voocphy_turn_off();
		}
		oplus_chg_set_awake(g_charger_chip, false);
		if (g_charger_chip->chg_ops->oplus_chg_wdt_enable)
			g_charger_chip->chg_ops->oplus_chg_wdt_enable(false);
	} else if (strncmp(proc_chg_cycle_data, "powersave", 9) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "powersave: stop usbtemp monitor, etc.\n");
		g_charger_chip->chg_powersave = true;
	} else if (strncmp(proc_chg_cycle_data, "unpowersave", 11) == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "unpowersave: start usbtemp monitor, etc.\n");
		g_charger_chip->chg_powersave = false;
	} else if (strncmp(proc_chg_cycle_data, "gaugei2ctest", 12) == 0) {
		buf = (unsigned char *)get_zeroed_page(GFP_KERNEL);
		charger_xlog_printk(CHG_LOG_CRTI, "gaugei2ctest: enter.\n");
		oplus_chg_get_battery_data(g_charger_chip);
		if (oplus_vooc_get_reply_bits() == 7 &&
				oplus_chg_get_voocphy_support() == NO_VOOCPHY) {
			val = oplus_gauge_get_prev_bcc_parameters(buf);
		} else {
			val = oplus_gauge_get_bcc_parameters(buf);
		}

		free_page((unsigned long)buf);
	} else {
		return -EFAULT;
	}
	return count;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations chg_cycle_proc_fops = {
	.read = chg_cycle_read,
	.write = chg_cycle_write,
	.llseek = noop_llseek,
};
#else
static const struct proc_ops chg_cycle_proc_fops = {
	.proc_read = chg_cycle_read,
	.proc_write = chg_cycle_write,
	.proc_lseek = noop_llseek,
};
#endif

static void init_proc_chg_cycle(void)
{
	if (!proc_create("charger_cycle", 0662,
			 NULL, &chg_cycle_proc_fops)) {
		chg_err("proc_create chg_cycle_proc_fops fail!\n");
	}
}
static ssize_t critical_log_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[32] = { 0 };
	int len = 0;

	if (charger_abnormal_log >= 10) {
		charger_abnormal_log = 10;
	}
	read_data[0] = '0' + charger_abnormal_log % 10;
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t critical_log_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = { 0 };
	int critical_log = 0;

	if (len > sizeof(write_data) || len < 1) {
		return -EINVAL;
	}
	if (copy_from_user(&write_data, buff, len)) {
		pr_err("bat_log_write error.\n");
		return -EFAULT;
	}
	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}
	critical_log = (int)simple_strtoul(write_data, NULL, 10);
	if (critical_log > 256) {
		critical_log = 256;
	}
	charger_abnormal_log = critical_log;
	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations chg_critical_log_proc_fops = {
	.write = critical_log_write,
	.read = critical_log_read,
};
#else
static const struct proc_ops chg_critical_log_proc_fops = {
	.proc_write = critical_log_write,
	.proc_read = critical_log_read,
	.proc_lseek = seq_lseek,
};
#endif

static void init_proc_critical_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("charger_critical_log", 0664, NULL, &chg_critical_log_proc_fops);
	if (!p) {
		pr_err("proc_create chg_critical_log_proc_fops fail!\n");
	}
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
/* wenbin.liu add for det rtc reset */
static ssize_t rtc_reset_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[32] = { 0 };
	int len = 0;
	int rc = 0;

	if (!g_charger_chip) {
		return -EFAULT;
	} else {
		rc = g_charger_chip->chg_ops->check_rtc_reset();
	}
	if (rc < 0 || rc > 1) {
		rc = 0;
	}
	read_data[0] = '0' + rc % 10;
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations rtc_reset_det_fops = {
	.read = rtc_reset_read,
};
#else
static const struct proc_ops rtc_reset_det_fops = {
	.proc_read = rtc_reset_read,
	.proc_lseek = seq_lseek,
};
#endif

static void init_proc_rtc_det(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("rtc_reset", 0664, NULL, &rtc_reset_det_fops);
	if (!p) {
		pr_err("proc_create rtc_reset_det_fops fail!\n");
	}
}
static ssize_t vbat_low_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[32] = { 0 };
	int len = 0;
	int rc = 0;

	if (!g_charger_chip)
		return -EFAULT;
	if (vbatt_lowerthan_3300mv) {
		rc = 1;
	}
	read_data[0] = '0' + rc % 10;
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations vbat_low_det_fops = {
	.read = vbat_low_read,
};
#else
static const struct proc_ops vbat_low_det_fops = {
	.proc_read = vbat_low_read,
	.proc_lseek = seq_lseek,
};
#endif

static void init_proc_vbat_low_det(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("vbat_low", 0664, NULL, &vbat_low_det_fops);
	if (!p) {
		pr_err("proc_create vbat_low_det_fops fail!\n");
	}
}

#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

static int oplus_chg_update_slow(struct oplus_chg_chip *chip)
{
	int ibat_sum = 0;
	int ibat_average;
	bool slow = 0;
	int i;

	for (i = 0; i < (ARRAY_SIZE(chip->ibat_save) - 1); i++) {
		chip->ibat_save[i] = chip->ibat_save[i + 1];
		ibat_sum += chip->ibat_save[i];
	}
	if (oplus_switching_support_parallel_chg() && chip->sub_batt_temperature != FG_I2C_ERROR) {
		chip->ibat_save[i] = chip->icharging + chip->sub_batt_icharging;
	} else {
		chip->ibat_save[i] = chip->icharging;
	}
	ibat_sum += chip->ibat_save[i];
	ibat_average = ibat_sum / ARRAY_SIZE(chip->ibat_save);

#ifndef CONFIG_OPLUS_CHARGER_MTK
	slow = !((chip->wireless_support && oplus_wpc_get_wireless_charge_start() == true) || chip->charger_exist ||
		 (ibat_average >= 500) || (chip->soc <= 15) || (chip->tbatt_temp >= 500) || (chip->soc >= 90));
#else
	slow = !(chip->charger_exist || (ibat_average >= 500) || (chip->soc <= 15) || (chip->tbatt_temp >= 500) ||
		 (chip->soc >= 90));
#endif

	pr_debug("ibat_average = %d, slow = %d\n", ibat_average, slow);

	return slow ? OPLUS_CHG_UPDATE_NO_CHARGE_INTERVAL_SEC : OPLUS_CHG_UPDATE_INTERVAL_SEC;
}

int oplus_get_vbatt_pdqc_to_9v_thr(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int rc, ret;

	if (chip->dev->of_node) {
		rc = of_property_read_u32(chip->dev->of_node, "qcom,vbatt_pdqc_to_9v_thr", &ret);
		if (rc < 0) {
			ret = 4000;
		}
	} else {
		ret = 4000;
	}
	return ret;
}
static ssize_t proc_charger_factorymode_test_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
	char buffer[2] = { 0 };
	struct oplus_chg_chip *chip = g_charger_chip;

	if (chip == NULL) {
		chg_err("%s: g_charger_chip driver is not ready\n", __func__);
		return -1;
	}

	if (count > 2) {
		return -1;
	}
	if (copy_from_user(buffer, buf, 1)) {
		chg_err("%s: error.\n", __func__);
		return -1;
	}
	if (buffer[0] == '6') {
		oplus_chg_get_props_from_adsp_by_buffer();
	}
	if (buffer[0] == '1') {
		if (chip->vbatt_num == 2) {
			if (oplus_voocphy_get_bidirect_cp_support() == false) {
				chip->limits.vbatt_pdqc_to_9v_thr = 4100;
			} else {
				chip->limits.vbatt_pdqc_to_9v_thr = oplus_get_vbatt_pdqc_to_9v_thr();
			}
			chg_err("vbatt_pdqc_to_9v_thr=%d\n", chip->limits.vbatt_pdqc_to_9v_thr);
			oplus_chg_pd_config(chip);
			oplus_chg_qc_config(chip);
		}
	}
	if (buffer[0] == '0') {
		chip->limits.vbatt_pdqc_to_9v_thr = oplus_get_vbatt_pdqc_to_9v_thr();
		chg_err("vbatt_pdqc_to_9v_thr=%d\n", chip->limits.vbatt_pdqc_to_9v_thr);
	}

	return count;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_charger_factorymode_test_ops = {
	.write = proc_charger_factorymode_test_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_charger_factorymode_test_ops = {
	.proc_write = proc_charger_factorymode_test_write,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};
#endif

static ssize_t oplus_chg_read(struct file *fp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	int len;

	len = sprintf(page, "sub_current=%d\nsub_voltage=%d\nsub_soc=%d\nsub_temperature=%d\n",
		      oplus_gauge_get_sub_batt_current(), oplus_gauge_get_sub_batt_mvolts(),
		      oplus_gauge_get_sub_batt_soc(), oplus_gauge_get_sub_batt_temperature());

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		chg_err("copy_to_user error\n");
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t oplus_chg_write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	return count;
}

static long oplus_chg_ioctl(struct file *fp, unsigned code, unsigned long value)
{
	char src[256] = { 0 };
	int ret;

	switch (code) {
	case OPLUS_CHG_GET_SUB_CURRENT:
		ret = sprintf(src, "sub_current=%d\n", oplus_gauge_get_sub_batt_current());
		break;
	case OPLUS_CHG_GET_SUB_VOLTAGE:
		ret = sprintf(src, "sub_voltage=%d\n", oplus_gauge_get_sub_batt_mvolts());
		break;
	case OPLUS_CHG_GET_SUB_SOC:
		ret = sprintf(src, "sub_soc=%d\n", oplus_gauge_get_sub_batt_soc());
		break;
	case OPLUS_CHG_GET_SUB_TEMPERATURE:
		ret = sprintf(src, "sub_temperature=%d\n", oplus_gauge_get_sub_batt_temperature());
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user((void __user *)value, src, ret))
		ret = -EFAULT;
	return ret;
}

static int oplus_chg_open(struct inode *ip, struct file *fp)
{
	if (atomic_xchg(&g_charger_chip->file_opened, 1)) {
		chg_err("oplus_chg_open failed\n");
		return -EBUSY;
	}
	fp->private_data = g_charger_chip;
	return 0;
}

static int oplus_chg_release(struct inode *ip, struct file *fp)
{
	WARN_ON(!atomic_xchg(&g_charger_chip->file_opened, 0));
	/* indicate that we are disconnected
	 * still could be online so don't touch online flag
	 */
	return 0;
}

static const struct file_operations oplus_chg_fops = {
	.owner = THIS_MODULE,
	.read = oplus_chg_read,
	.write = oplus_chg_write,
	.unlocked_ioctl = oplus_chg_ioctl,
	.open = oplus_chg_open,
	.release = oplus_chg_release,
};

static struct miscdevice oplus_chg_device = {
	.name = "oplus_chg",
	.fops = &oplus_chg_fops,
};

static ssize_t proc_integrate_gauge_fcc_flag_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[32] = { 0 };
	int len = 0;

	read_data[0] = '1';
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_integrate_gauge_fcc_flag_ops = {
	.read = proc_integrate_gauge_fcc_flag_read,
	.open = simple_open,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_integrate_gauge_fcc_flag_ops = {
	.proc_read = proc_integrate_gauge_fcc_flag_read,
	.proc_open = simple_open,
	.proc_lseek = seq_lseek,
};
#endif

#define RESERVE_SOC_BUF_LEN 4
static ssize_t proc_reserve_soc_debug_write(struct file *file, const char __user *user_buf, size_t len, loff_t *data)
{
	struct oplus_chg_chip *chip = PDE_DATA(file_inode(file));

	char buf[RESERVE_SOC_BUF_LEN] = { 0 };
	int reserve_chg_soc, reserve_dis_soc;

	if (len < 1 || len > RESERVE_SOC_BUF_LEN) {
		chg_err("len %d invalid\n", len);
		return -EFAULT;
	}

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[RESERVE_SOC_BUF_LEN - 1] = '\0';

	if (sscanf(buf, "%d,%d", &reserve_chg_soc, &reserve_dis_soc) == 2) {
		if (reserve_chg_soc != chip->rsd.reserve_soc) {
			chg_err("chg:%d dis:%d\n", reserve_chg_soc, reserve_dis_soc);
			if (reserve_chg_soc < RESERVE_SOC_MIN || reserve_chg_soc > RESERVE_SOC_MAX) {
				chip->rsd.reserve_soc = RESERVE_SOC_OFF;
				chip->rsd.smooth_switch_v2 = false;
				chip->smooth_soc = chip->soc;
			} else {
				chip->rsd.reserve_soc = reserve_chg_soc;
				chip->rsd.smooth_switch_v2 = true;
				oplus_chg_smooth_to_soc_v2(chip, true);
			}
		} else {
			chg_err("same val,ignore\n");
		}
	}
	return len;
}

static ssize_t proc_reserve_soc_debug_read(struct file *file, char __user *user_buf, size_t count, loff_t *off)
{
	struct oplus_chg_chip *chip = PDE_DATA(file_inode(file));
	char buf[256] = { 0 };
	int len = 0;

	len = sprintf(buf, "%d,%d", chip->rsd.reserve_soc, chip->rsd.reserve_soc);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(user_buf, buf, (len < count ? len : count)))
		return -EFAULT;

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_reserve_soc_debug_ops = {
	.write = proc_reserve_soc_debug_write,
	.read = proc_reserve_soc_debug_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_reserve_soc_debug_ops = {
	.proc_write = proc_reserve_soc_debug_write,
	.proc_read = proc_reserve_soc_debug_read,
	.proc_lseek = seq_lseek,
};
#endif

#define FORCE_IBUS_BUF_LEN 5
static ssize_t proc_force_current_limit_write(struct file *file, const char __user *user_buf, size_t len, loff_t *data)
{
	struct oplus_chg_chip *chip = PDE_DATA(file_inode(file));
	char buf[FORCE_IBUS_BUF_LEN] = { 0 };
	int val = 0;

	if (len < 1 || len > FORCE_IBUS_BUF_LEN) {
		chg_err("len %d invalid\n", len);
		return -EFAULT;
	}

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[FORCE_IBUS_BUF_LEN - 1] = '\0';

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val < 0 || val > chip->limits.input_current_charger_ma) {
		chg_err("val %d invalid, 0~%d\n", val, chip->limits.input_current_charger_ma);
		return -EINVAL;
	}

	if (val != chip->limits.force_input_current_ma) {
		chip->limits.force_input_current_ma = val;
		chg_err("force_input_current_ma=%d\n", chip->limits.force_input_current_ma);
		oplus_chg_set_input_current_limit(chip);
	}
	return len;
}

static ssize_t proc_force_current_limit_read(struct file *file, char __user *user_buf, size_t count, loff_t *off)
{
	struct oplus_chg_chip *chip = PDE_DATA(file_inode(file));
	char buf[FORCE_IBUS_BUF_LEN] = { 0 };
	size_t len;
	ssize_t rc;

	len = snprintf(buf, FORCE_IBUS_BUF_LEN, "%d", chip->limits.force_input_current_ma);
	rc = simple_read_from_buffer(user_buf, count, off, buf, len);
	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_force_current_limit_ops = {
	.write = proc_force_current_limit_write,
	.read = proc_force_current_limit_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_force_current_limit_ops = {
	.proc_write = proc_force_current_limit_write,
	.proc_read = proc_force_current_limit_read,
	.proc_lseek = seq_lseek,
};
#endif

static ssize_t proc_hmac_write(struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char buffer[2] = { 0 };

	if (NULL == chip)
		return -EFAULT;

	if (len > 2) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, 2)) {
		chg_err("%s:  error.\n", __func__);
		return -EFAULT;
	}
	if (buffer[0] == '0') {
		chip->hmac = false;
	} else {
		chip->hmac = true;
	}
	return len;
}

static ssize_t proc_hmac_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char page[256] = { 0 };
	char read_data[3] = { 0 };
	int len = 0;

	if (NULL == chip)
		return -EFAULT;

	if (true == chip->hmac) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	read_data[1] = '\0';
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		chg_err("%s: copy_to_user error hmac = %d.\n", __func__, chip->hmac);
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations hmac_proc_fops = {
	.write = proc_hmac_write,
	.read = proc_hmac_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops hmac_proc_fops = {
	.proc_write = proc_hmac_write,
	.proc_read = proc_hmac_read,
	.proc_lseek = seq_lseek,
};
#endif

static ssize_t proc_charger_input_current_now_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[128] = { 0 };
	int len = 0;

	struct oplus_chg_chip *chip = g_charger_chip;
	if (!chip) {
		return 0;
	}
	sprintf(read_data, "%d", chip->ibus / 1000);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t proc_charger_passedchg_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[128] = { 0 };
	int len = 0;

	struct oplus_chg_chip *chip = g_charger_chip;
	if (!chip) {
		return 0;
	}

	sprintf(read_data, "%d", midas_chg.cali_passed_chg);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t proc_charger_passedchg_reset_count_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[128] = { 0 };
	int len = 0;

	struct oplus_chg_chip *chip = g_charger_chip;
	if (!chip) {
		return 0;
	}
	sprintf(read_data, "%d", midas_chg.reset_counts);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t qg_vbat_deviation_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	int len = 0;
	int plat_vbat = 0;
	int gauge_bat = 0;
	int vbat_deviation = 0;
	static int final_deviation = 0;
	static bool first_pass = false;
	int retry = 5;

	if (!first_pass) {
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			oplus_chg_deviation_disable_charge();
			vbat_deviation = oplus_chg_voocphy_get_cp_vbat_deviation();
			oplus_chg_deviation_enable_charge();
		} else {
			if (oplus_plat_gauge_is_support() != 1) {
				return 0;
			}
			oplus_chg_deviation_disable_charge();
			while (retry > 0) {
				plat_vbat = oplus_gauge_get_plat_batt_mvolts();
				gauge_bat = oplus_gauge_get_batt_mvolts();
				vbat_deviation =
					(gauge_bat > plat_vbat) ? gauge_bat - plat_vbat : plat_vbat - gauge_bat;
				if (plat_vbat == 0 || gauge_bat == 0 || vbat_deviation >= DEVIATION_NUM) {
					msleep(100);
					retry--;
				} else {
					break;
				}
			}
		}
		if (vbat_deviation < DEVIATION_NUM) {
			final_deviation = vbat_deviation;
			first_pass = true;
		}
	} else {
		vbat_deviation = final_deviation;
	}
	chg_err("kilody: qg_vbat_deviation_read rkvbat = %d,gauge_bat=%d,vbat_deviation=%d,final_deviation=%d,retry:%d\n",
		plat_vbat, gauge_bat, vbat_deviation, final_deviation, retry);

	len = sprintf(page, "%d", vbat_deviation);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations qg_vbat_deviation_proc_fops = {
	.read = qg_vbat_deviation_read,
	.llseek = noop_llseek,
};
#else
static const struct proc_ops qg_vbat_deviation_proc_fops = {
	.proc_read = qg_vbat_deviation_read,
	.proc_lseek = noop_llseek,
};
#endif

static ssize_t fastcharge_fail_count_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	int len = 0;
	int fastcharge_fail_count = vooc_get_fastcharge_fail_count();

	len = sprintf(page, "%d", fastcharge_fail_count);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations fastcharge_fail_count_proc_fops = {
	.read = fastcharge_fail_count_read,
	.llseek = noop_llseek,
};

static const struct file_operations proc_charger_input_current_now_ops = {
	.read = proc_charger_input_current_now_read,
	.owner = THIS_MODULE,
};

static const struct file_operations proc_charger_passedchg_ops = {
	.read = proc_charger_passedchg_read,
	.owner = THIS_MODULE,
};

static const struct file_operations proc_charger_passedchg_reset_count_ops = {
	.read = proc_charger_passedchg_reset_count_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops fastcharge_fail_count_proc_fops = {
	.proc_read = fastcharge_fail_count_read,
	.proc_lseek = noop_llseek,
};

static const struct proc_ops proc_charger_input_current_now_ops = {
	.proc_read = proc_charger_input_current_now_read,
	.proc_lseek = seq_lseek,
};

static const struct proc_ops proc_charger_passedchg_ops = {
	.proc_read = proc_charger_passedchg_read,
	.proc_lseek = seq_lseek,
};

static const struct proc_ops proc_charger_passedchg_reset_count_ops = {
	.proc_read = proc_charger_passedchg_reset_count_read,
	.proc_lseek = seq_lseek,
};
#endif
static ssize_t proc_start_test_external_write(struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char buffer[32] = { 0 };
	int count;

	if (NULL == chip)
		return -EFAULT;

	if (len > 32) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, len > 32 ? 32 : len)) {
		chg_err("%s:  error.\n", __func__);
		return -EFAULT;
	}
	chg_err("count : %s\n", buffer);
	if (kstrtoint(buffer, 10, &count)) {
		return -EINVAL;
	}
	chg_err("count : %d\n", count);
	oplus_gauge_start_test_external_hmac(count);
	return len;
}

static ssize_t proc_get_hmac_test_result_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char page[256] = { 0 };
	int len = 0;
	int count_total = 0;
	int count_now = 0;
	int fail_count = 0;

	if (NULL == chip)
		return -EFAULT;
	oplus_gauge_get_external_hmac_test_result(&count_total, &count_now, &fail_count);

	len = sprintf(page, "test external result:%d/%d,fail count:%d\n", count_now, count_total, fail_count);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		chg_err("%s: copy_to_user error hmac = %d.\n", __func__, chip->hmac);
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations test_external_hmac_proc_fops = {
	.write = proc_start_test_external_write,
	.read = proc_get_hmac_test_result_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops test_external_hmac_proc_fops = {
	.proc_write = proc_start_test_external_write,
	.proc_read = proc_get_hmac_test_result_read,
	.proc_lseek = seq_lseek,
};
#endif

static ssize_t proc_external_hmac_status_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char page[256] = { 0 };
	int len = 0;
	int status = 0;
	int fail_count = 0;
	int total_count = 0;
	int real_fail_count = 0;
	int real_total_count = 0;

	if (NULL == chip)
		return -EFAULT;
	oplus_gauge_get_external_hmac_status(&status, &fail_count, &total_count, &real_fail_count, &real_total_count);
	len = sprintf(page,
		      "external status:%d, fail count:%d, total count:%d, real fail count:%d, real total count:%d\n",
		      status, fail_count, total_count, real_fail_count, real_total_count);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		chg_err("%s: copy_to_user error hmac = %d.\n", __func__, chip->hmac);
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations external_hmac_status_proc_fops = {
	.read = proc_external_hmac_status_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops external_hmac_status_proc_fops = {
	.proc_read = proc_external_hmac_status_read,
	.proc_lseek = seq_lseek,
};
#endif

static ssize_t proc_chg_ctl(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char page[256] = { 0 };
	int len = 0;
	int asic_current = 0;
	int cp_ibus = 0, slave_cp_ibus = 0, slave_b_cp_ibus = 0, cp_vbat_deviation = 0;

	if (NULL == chip)
		return -EFAULT;
	if (oplus_voocphy_get_bidirect_cp_support()) {
		return -EFAULT;
	}
	if (chip->internal_gauge_with_asic == 1) {
		asic_current = oplus_gauge_get_plat_batt_current();
		chg_err("asic_current = %d, chip->icharging = %d\n", asic_current, chip->icharging);
	} else {
		asic_current = chip->icharging;
		chg_err("can't use asic_current, chip->icharging = %d\n", asic_current);
	}
	if (oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY ||
	    oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY) {
		cp_ibus = -oplus_chg_voocphy_get_cp_ibus();
		slave_cp_ibus = -oplus_chg_voocphy_get_slave_cp_ibus();
		slave_b_cp_ibus = 0;
		cp_vbat_deviation = oplus_chg_voocphy_get_cp_vbat_deviation();
	} else if (oplus_pps_get_support_type() == PPS_SUPPORT_2CP) {
		cp_ibus = -oplus_pps_get_master_ibus();
		slave_cp_ibus = -oplus_pps_get_slave_ibus();
		slave_b_cp_ibus = 0;
		cp_vbat_deviation = 0;
	} else if (oplus_pps_get_support_type() == PPS_SUPPORT_3CP) {
		cp_ibus = -oplus_pps_get_master_ibus();
		slave_cp_ibus = -oplus_pps_get_slave_ibus();
		slave_b_cp_ibus = -oplus_pps_get_slave_b_ibus();
		cp_vbat_deviation = 0;
	} else {
		cp_ibus = -g_charger_chip->icharging;
		slave_cp_ibus = -g_charger_chip->icharging;
		slave_b_cp_ibus = 0;
		cp_vbat_deviation = 0;
	}
	len = sprintf(page, "asic_current=%d\nmain_cp_ichg=%d\nslave_cp_ichg=%d\nslave_b_cp_ichg=%d\ncp_vbat_deviation=%d\n",
			asic_current, cp_ibus, slave_cp_ibus, slave_b_cp_ibus, cp_vbat_deviation);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		chg_err("copy_to_user error current = %d.\n", asic_current);
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations chg_ctl_proc_fops = {
	.read = proc_chg_ctl,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops chg_ctl_proc_fops = {
	.proc_read = proc_chg_ctl,
	.proc_lseek = seq_lseek,
};
#endif

static int init_charger_proc(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_da = NULL;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_da = proc_mkdir("charger", NULL);
	if (prEntry_da == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create charger proc entry\n", __func__);
	}

	prEntry_tmp = proc_create_data("charger_factorymode_test", 0666, prEntry_da, &proc_charger_factorymode_test_ops,
				       chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("hmac", 0666, prEntry_da, &hmac_proc_fops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create hmac proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp =
		proc_create_data("input_current_now", 0666, prEntry_da, &proc_charger_input_current_now_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create input_current_now proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("passedchg", 0666, prEntry_da, &proc_charger_passedchg_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create passedchg proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("passedchg_reset_count", 0666, prEntry_da,
				       &proc_charger_passedchg_reset_count_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create passedchg_reset_count proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("qg_vbat_deviation", 0444, prEntry_da, &qg_vbat_deviation_proc_fops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create qg_vbat_deviation proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp =
		proc_create_data("fastcharge_fail_count", 0444, prEntry_da, &fastcharge_fail_count_proc_fops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create proc fastcharge_fail_count entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("test_external_hmac", 0666, prEntry_da, &test_external_hmac_proc_fops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create test_external_hmac proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("external_hmac_status", 0666, prEntry_da, &external_hmac_status_proc_fops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create external_hmac_status proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("chg_ctl", 0666, prEntry_da, &chg_ctl_proc_fops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create chg_ctl proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("integrate_gauge_fcc_flag", 0666, prEntry_da, &proc_integrate_gauge_fcc_flag_ops,
				       chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create proc_integrate_gauge_fcc_flag_ops entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("reserve_soc_debug", 0666, prEntry_da, &proc_reserve_soc_debug_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create proc_reserve_soc_debug_ops entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("force_current_limit", 0664, prEntry_da, &proc_force_current_limit_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("Couldn't create proc_force_current_limit_ops entry, %d\n");
	}

	ret = misc_register(&oplus_chg_device);

	return 0;
}

static ssize_t proc_ui_soc_decimal_write(struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	char buffer[2] = { 0 };

	if (NULL == chip)
		return -EFAULT;

	if (len > 2) {
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, 2)) {
		chg_err("%s:  error.\n", __func__);
		return -EFAULT;
	}
	if (buffer[0] == '0') {
		chip->boot_completed = false;
	} else {
		chip->boot_completed = true;
	}
	pr_err("proc_ui_soc_decimal_write write");
	return len;
}
static ssize_t proc_ui_soc_decimal_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = { 0 };
	char read_data[128] = { 0 };
	int len = 0;
	int schedule_work = 0;
	int val;
	bool svooc_is_control_by_vooc;
	bool control_by_wireless = false;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return 0;
	}

	if (chip->vooc_show_ui_soc_decimal) {
		svooc_is_control_by_vooc =
			oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC;
		control_by_wireless =
			chip->wireless_support && (oplus_wpc_get_wireless_charge_start() || oplus_chg_is_wls_present());
		if (svooc_is_control_by_vooc != true && chip->calculate_decimal_time == 0 &&
		    control_by_wireless == false && oplus_chg_is_wls_fast_type() == false &&
		    oplus_chg_show_vooc_logo_ornot() == true) {
			cancel_delayed_work_sync(&chip->ui_soc_decimal_work);
			oplus_chg_ui_soc_decimal_init();
			schedule_work = mod_delayed_work(system_wq, &chip->ui_soc_decimal_work, 0);
		}

		val = (chip->ui_soc_integer + chip->ui_soc_decimal) / 10;
		if (chip->decimal_control == false) {
			val = 0;
		}
	} else {
		val = 0;
	}

	sprintf(read_data, "%d, %d", chip->init_decimal_ui_soc / 10, val);
	pr_err("APK successful, %d,%d", chip->init_decimal_ui_soc / 10, val);
	len = sprintf(page, "%s", read_data);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations ui_soc_decimal_ops = {
	.write = proc_ui_soc_decimal_write,
	.read = proc_ui_soc_decimal_read,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops ui_soc_decimal_ops = {
	.proc_write = proc_ui_soc_decimal_write,
	.proc_read = proc_ui_soc_decimal_read,
	.proc_lseek = seq_lseek,
};
#endif

static int init_ui_soc_decimal_proc(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_tmp = proc_create_data("ui_soc_decimal", 0666, NULL, &ui_soc_decimal_ops, chip);
	if (prEntry_tmp == NULL) {
		ret = -1;
		chg_debug("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}
	return 0;
}

void oplus_chg_ui_soc_decimal_init(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;

	if (chip->charger_exist) {
		oplus_chg_get_props_from_adsp_by_buffer();
	}

	if (oplus_vooc_get_fastchg_started() == true) {
		chip->batt_rm = oplus_gauge_get_prev_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc = oplus_gauge_get_prev_batt_fcc() * chip->vbatt_num;
	} else {
		chip->batt_rm = oplus_gauge_get_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc = oplus_gauge_get_batt_fcc() * chip->vbatt_num;
	}
	if (chip->batt_rm < 0) {
		chip->batt_rm = 0;
	}
	pr_err("[oplus_chg_ui_soc_decimal_init]!!!soc:%d", (int)((chip->batt_rm * 100000) / chip->batt_fcc));

	if (chip->ui_soc == 100) {
		chip->ui_soc_integer = chip->ui_soc * 1000;
		chip->ui_soc_decimal = 0;
	} else {
		chip->ui_soc_integer = chip->ui_soc * 1000;
		chip->ui_soc_decimal =
			chip->batt_rm * 100000 / chip->batt_fcc - (chip->batt_rm * 100 / chip->batt_fcc) * 1000;
		if (chip->rsd.smooth_switch_v2 && chip->rsd.reserve_soc)
			chip->ui_soc_decimal =
				chip->ui_soc_decimal * OPLUS_FULL_SOC / (OPLUS_FULL_SOC - chip->rsd.reserve_soc);
		if ((chip->ui_soc_integer + chip->ui_soc_decimal) > chip->last_decimal_ui_soc &&
		    chip->last_decimal_ui_soc != 0) {
			chip->ui_soc_decimal = ((chip->last_decimal_ui_soc % 1000 - 5) > 0) ?
						       (chip->last_decimal_ui_soc % 1000 - 5) :
						       0;
		}
	}
	chip->init_decimal_ui_soc = chip->ui_soc_integer + chip->ui_soc_decimal;
	if (chip->init_decimal_ui_soc > 100000) {
		chip->init_decimal_ui_soc = 100000;
		chip->ui_soc_integer = 100000;
		chip->ui_soc_decimal = 0;
	}
	chip->decimal_control = true;
	pr_err("[oplus_chg_ui_soc_decimal_init]!!! 2VBUS ui_soc_decimal:%d",
	       chip->ui_soc_integer + chip->ui_soc_decimal);

	chip->calculate_decimal_time = 1;
}
void oplus_chg_ui_soc_decimal_deinit(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	chip->ui_soc_integer = (chip->ui_soc_integer + chip->ui_soc_decimal) / 1000;
	if (chip->ui_soc_integer != 0) {
		chip->ui_soc = chip->ui_soc_integer;
		if (chip->ui_soc_decimal != 0 && chip->ui_soc < chip->smooth_soc) {
			chip->ui_soc = chip->ui_soc + 1;
		}
	}
	chip->decimal_control = false;
	pr_err("[oplus_chg_ui_soc_decimal_deinit] ui_soc[%d], soc[%d], smooth_soc[%d] chip->ui_soc_decimal[%d]",
	       chip->ui_soc, chip->soc, chip->smooth_soc, chip->ui_soc_decimal);
	chip->ui_soc_integer = 0;
	chip->ui_soc_decimal = 0;
	chip->init_decimal_ui_soc = 0;
}
#define MIN_DECIMAL_CURRENT 2000
static void oplus_chg_show_ui_soc_decimal(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	long int speed, icharging;
	int ratio = 1;

	/*update the battery data*/
	if (chip->charger_exist) {
		oplus_chg_get_props_from_adsp_by_buffer();
	}

	if (oplus_chg_get_voocphy_support() == NO_VOOCPHY && oplus_vooc_get_fastchg_started() == true) {
		chip->batt_rm = oplus_gauge_get_prev_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc = oplus_gauge_get_prev_batt_fcc() * chip->vbatt_num;
#ifdef OPLUS_CUSTOM_OP_DEF
		chip->icharging = oplus_gauge_get_prev_batt_current();
#else
		chip->icharging = oplus_gauge_get_batt_current();
#endif
	} else {
		chip->batt_rm = oplus_gauge_get_remaining_capacity() * chip->vbatt_num;
		chip->batt_fcc = oplus_gauge_get_batt_fcc() * chip->vbatt_num;
		if (oplus_switching_support_parallel_chg()) {
			chip->sub_batt_icharging = oplus_gauge_get_sub_batt_current();
		}
		chip->icharging = oplus_gauge_get_batt_current();
	}
	if (chip->batt_rm < 0) {
		chip->batt_rm = 0;
	}
	if (oplus_switching_support_parallel_chg()) {
		chip->sub_batt_icharging = oplus_gauge_get_sub_batt_current();
		icharging = (chip->icharging + chip->sub_batt_icharging) * (-1);
	} else {
		icharging = chip->icharging * (-1);
	}

	/*calculate the speed*/
	if (chip->ui_soc - chip->smooth_soc > 3) {
		ratio = 2;
	} else {
		ratio = 1;
	}
	if (icharging > 0) {
		speed = 100000 * icharging * UPDATE_TIME * chip->vbatt_num / (chip->batt_fcc * 3600);
		if (chip->rsd.smooth_switch_v2 && chip->rsd.reserve_soc)
			speed = speed * OPLUS_FULL_SOC / (OPLUS_FULL_SOC - chip->rsd.reserve_soc);
		pr_err("[oplus_chg_show_ui_soc_decimal] icharging = %d, batt_fcc :%d", chip->icharging, chip->batt_fcc);
		if (oplus_pps_get_adapter_type() == PPS_ADAPTER_OPLUS_V2 || oplus_pps_get_adapter_type() == PPS_ADAPTER_OPLUS_V3) {
			if (chip->ui_soc > chip->smooth_soc) {
				speed = speed / 2;
			}
		} else {
			if ((chip->ui_soc_integer + chip->ui_soc_decimal) / 1000 - chip->smooth_soc > 2) {
				ratio = 2;
				speed = speed / 2;
			} else if ((chip->ui_soc_integer + chip->ui_soc_decimal) / 1000 < chip->smooth_soc) {
				speed = speed * 2;
			}
		}
	} else {
		speed = 0;
		if (chip->batt_full) {
			speed = chip->ui_soc_decimal_speedmin;
		}
	}
	if (speed > 500) {
		speed = 500;
	}
	chip->ui_soc_decimal += speed;
	pr_err("[oplus_chg_ui_soc_decimal]chip->ui_soc_decimal+chip_ui_soc: %d , speed: %d, soc :%d\n ",
	       (chip->ui_soc_decimal + chip->ui_soc_integer), speed, ((chip->batt_rm * 100000) / chip->batt_fcc));
	if (chip->ui_soc_integer + chip->ui_soc_decimal >= 100000) {
		chip->ui_soc_integer = 100000;
		chip->ui_soc_decimal = 0;
	}

	if (chip->calculate_decimal_time <= MAX_UI_DECIMAL_TIME) {
		chip->calculate_decimal_time++;
		schedule_delayed_work(&chip->ui_soc_decimal_work, msecs_to_jiffies(UPDATE_TIME * 1000));
	} else {
		oplus_chg_ui_soc_decimal_deinit();
	}
}

static int charging_limit_time_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.max_chg_time_sec);
	return 0;
}
static int charging_limit_time_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = single_open(file, charging_limit_time_show, NULL);
	return ret;
}

static ssize_t charging_limit_time_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	int limit_time;
	char temp[16];

	if (len > sizeof(temp)) {
		return -EINVAL;
	}
	if (copy_from_user(temp, buff, len)) {
		pr_err("charging_limit_time_write error.\n");
		return -EFAULT;
	}
	sscanf(temp, "%d", &limit_time);
	if (g_charger_chip) {
		g_charger_chip->limits.max_chg_time_sec = limit_time;
		printk(KERN_EMERG "charging_feature:max_chg_time_sec = %d\n", g_charger_chip->limits.max_chg_time_sec);
	}
	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations charging_limit_time_fops = {
	.open = charging_limit_time_open,
	.write = charging_limit_time_write,
	.read = seq_read,
};
#else
static const struct proc_ops charging_limit_time_fops = {
	.proc_open = charging_limit_time_open,
	.proc_write = charging_limit_time_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};
#endif

static int charging_limit_current_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.input_current_led_ma_high);
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.input_current_led_ma_warm);
	seq_printf(seq_filp, "%d\n", g_charger_chip->limits.input_current_led_ma_normal);
	return 0;
}
static int charging_limit_current_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = single_open(file, charging_limit_current_show, NULL);
	return ret;
}

static ssize_t charging_limit_current_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	int limit_current;
	char temp[16];

	if (len > sizeof(temp)) {
		return -EINVAL;
	}
	if (copy_from_user(temp, buff, len)) {
		pr_err("charging_limit_current_write error.\n");
		return -EFAULT;
	}
	sscanf(temp, "%d", &limit_current);
	if (g_charger_chip) {
		g_charger_chip->limits.input_current_led_ma_high = limit_current;
		g_charger_chip->limits.input_current_led_ma_warm = limit_current;
		g_charger_chip->limits.input_current_led_ma_normal = limit_current;
		printk(KERN_EMERG "charging_feature:limit_current = %d\n", limit_current);
	}
	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations charging_limit_current_fops = {
	.open = charging_limit_current_open,
	.write = charging_limit_current_write,
	.read = seq_read,
	.release = single_release,
};
#else
static const struct proc_ops charging_limit_current_fops = {
	.proc_open = charging_limit_current_open,
	.proc_write = charging_limit_current_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};
#endif

static void init_proc_charging_feature(void)
{
	struct proc_dir_entry *p_time = NULL;
	struct proc_dir_entry *p_current = NULL;

	p_time = proc_create("charging_limit_time", 0664, NULL, &charging_limit_time_fops);
	if (!p_time) {
		pr_err("proc_create charging_feature_fops fail!\n");
	}
	p_current = proc_create("charging_limit_current", 0664, NULL, &charging_limit_current_fops);
	if (!p_current) {
		pr_err("proc_create charging_feature_fops fail!\n");
	}
}

/*ye.zhang add end*/
static void mmi_adapter_in_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, mmi_adapter_in_work);
	chip->mmi_fastchg = 1;
	charger_xlog_printk(CHG_LOG_CRTI, "clear mmi_fastchg\n");
	oplus_chg_set_force_psy_changed();
}

static void oplus_mmi_fastchg_in(struct oplus_chg_chip *chip)
{
	charger_xlog_printk(CHG_LOG_CRTI, "  call\n");
	schedule_delayed_work(&chip->mmi_adapter_in_work, round_jiffies_relative(msecs_to_jiffies(3000)));
}

int oplus_chg_get_mmi_value(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 1;
	}
	if (chip->mmi_chg == 0)
		printk(KERN_ERR "[OPLUS_CHG][%s]: mmi_chg[%d]\n", __func__, chip->mmi_chg);
	return chip->mmi_chg;
}

static void oplus_chg_awake_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND, "battery suspend wakelock");

#else
	chip->suspend_ws = wakeup_source_register(NULL, "battery suspend wakelock");
#endif
}

static void oplus_chg_set_awake(struct oplus_chg_chip *chip, bool awake)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if (chip->unwakelock_chg == 1 && awake == true) {
		charger_xlog_printk(CHG_LOG_CRTI, "error, unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (awake) {
		wake_lock(&chip->suspend_lock);
	} else {
		wake_unlock(&chip->suspend_lock);
	}
#else
	static bool pm_flag = false;

	if (chip->unwakelock_chg == 1 && awake == true) {
		charger_xlog_printk(CHG_LOG_CRTI, "error, unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (!chip || !chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
	}
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __ref shortc_thread_main(void *data)
#else
static int shortc_thread_main(void *data)
#endif
{
	struct oplus_chg_chip *chip = data;
	int rc = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	struct cred *new;

	new = prepare_creds();
	if (!new) {
		chg_err("init err\n");
		rc = -1;
		return rc;
	}
	new->fsuid = new->euid = KUIDT_INIT(1000);
	commit_creds(new);
#endif
	while (!kthread_should_stop()) {
		set_current_state(TASK_RUNNING);
		oplus_chg_short_c_battery_check(chip);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
	return rc;
}

#define CHG_LOCAL_T_NS_TO_MS_THD 1000000
#define CHG_LOCAL_T_NS_TO_S_THD 1000000000
#define ENSURE_TRACK_UPLOAD_TIMES 200
static int oplus_chg_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / CHG_LOCAL_T_NS_TO_S_THD;
	chg_err("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int oplus_chg_track_upload_vbatt_diff_over_info(struct oplus_chg_chip *chip, int vbat_cell_max,
						       int vbat_cell_min, int status)
{
	int index = 0;

	memset(chip->vbatt_diff_over_load_trigger.crux_info, 0, sizeof(chip->vbatt_diff_over_load_trigger.crux_info));

	index += snprintf(&(chip->vbatt_diff_over_load_trigger.crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$soc@@%d"
			  "$$s_soc@@%d$$uisoc@@%d$$vbatt_max@@%d$$vbatt_min@@%d"
			  "$$vbatt_diff@@%d$$charger_exist@@%d",
			  chip->soc, chip->smooth_soc, chip->ui_soc, vbat_cell_max, vbat_cell_min,
			  (vbat_cell_max - vbat_cell_min), chip->charger_exist);

	if (status == POWER_SUPPLY_STATUS_FULL)
		index += snprintf(&(chip->vbatt_diff_over_load_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$vdiff_type@@full");
	else
		index += snprintf(&(chip->vbatt_diff_over_load_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$vdiff_type@@shutdown");
	schedule_delayed_work(&chip->vbatt_diff_over_load_trigger_work, 0);
	chg_err("%s\n", chip->vbatt_diff_over_load_trigger.crux_info);

	return 0;
}

static int oplus_chg_track_upload_vbatt_too_low_info(struct oplus_chg_chip *chip)
{
	int index = 0;

	memset(chip->vbatt_too_low_load_trigger.crux_info, 0, sizeof(chip->vbatt_too_low_load_trigger.crux_info));

	index += snprintf(&(chip->vbatt_too_low_load_trigger.crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$soc@@%d"
			  "$$s_soc@@%d$$uisoc@@%d$$vbatt_max@@%d$$vbatt_min@@%d",
			  chip->soc, chip->smooth_soc, chip->ui_soc, chip->batt_volt_max, chip->batt_volt_min);

	schedule_delayed_work(&chip->vbatt_too_low_load_trigger_work, 0);
	chg_debug("%s\n", chip->vbatt_too_low_load_trigger.crux_info);

	return 0;
}

static int oplus_chg_track_upload_uisoc_keep_1_t_info(struct oplus_chg_chip *chip)
{
	int index = 0;
	int uisoc_1_end_time;

	uisoc_1_end_time = oplus_chg_get_local_time_s();
	chg_debug("uisoc_1_end_time:%d, uisoc_1_start_time:%d\n", uisoc_1_end_time, chip->uisoc_1_start_time);
	memset(chip->uisoc_keep_1_t_load_trigger.crux_info, 0, sizeof(chip->uisoc_keep_1_t_load_trigger.crux_info));
	index += snprintf(&(chip->uisoc_keep_1_t_load_trigger.crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$uisoc_keep_1_t@@%d", uisoc_1_end_time - chip->uisoc_1_start_time);

	index += snprintf(&(chip->uisoc_keep_1_t_load_trigger.crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$pre_vbatt_max@@%d"
			  "$$pre_vbatt_min@@%d$$curr_vbatt_max@@%d$$curr_vbatt_min@@%d"
			  "$$soc@@%d$$s_soc@@%d$$uisoc@@%d$$start_batt_rm@@%d"
			  "$$curr_batt_rm@@%d$$batt_curr@@%d$$charger_exist@@%d",
			  chip->uisoc_1_start_vbatt_max, chip->uisoc_1_start_vbatt_min, chip->batt_volt_max,
			  chip->batt_volt_min, chip->soc, chip->smooth_soc, chip->ui_soc, chip->uisoc_1_start_batt_rm,
			  chip->batt_rm, chip->icharging, chip->charger_exist);

	schedule_delayed_work(&chip->uisoc_keep_1_t_load_trigger_work, 0);
	chg_debug("%s\n", chip->uisoc_keep_1_t_load_trigger.crux_info);
	msleep(ENSURE_TRACK_UPLOAD_TIMES);

	return 0;
}

static void oplus_chg_track_upload_fast_gpio_info(struct oplus_chg_chip *chip)
{
	int index = 0;
	bool need_upload = false;

	if (!chip || !chip->vooc_project)
		return;

	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY)
		return;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	memset(chip->fast_gpio_err_load_trigger.crux_info, 0, sizeof(chip->fast_gpio_err_load_trigger.crux_info));

	index += snprintf(&(chip->fast_gpio_err_load_trigger.crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@gpio_%d", chip->normalchg_gpio.chargerid_switch_gpio);

	index += snprintf(&(chip->fast_gpio_err_load_trigger.crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_GPIO_LEVEL_ERR);
	if (!chip->chg_ops->get_chargerid_switch_val()) {
		oplus_chg_track_get_gpio_err_reason(TRACK_GPIO_ERR_CHARGER_ID, chip->err_reason,
						    sizeof(chip->err_reason));
		index += snprintf(&(chip->fast_gpio_err_load_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_reason@@%s", chip->err_reason);
		need_upload = true;
	} else if (!oplus_vooc_get_vooc_switch_val()) {
		oplus_chg_track_get_gpio_err_reason(TRACK_GPIO_ERR_VOOC_SWITCH, chip->err_reason,
						    sizeof(chip->err_reason));
		index += snprintf(&(chip->fast_gpio_err_load_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_reason@@%s", chip->err_reason);
		need_upload = true;
	}

	if (need_upload || chip->debug_force_fast_gpio_err) {
		oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
		index += snprintf(&(chip->fast_gpio_err_load_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->chg_power_info);
		schedule_delayed_work(&chip->fast_gpio_err_load_trigger_work, 0);
	}
	mutex_unlock(&chip->track_upload_lock);
}

static void oplus_chg_track_fast_gpio_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, fast_gpio_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->fast_gpio_err_load_trigger);
}

#define COOL_DOWN_CHECK_15S 3
#define VOLTAGE_RATIO 15
int oplus_convert_level_to_current(struct oplus_chg_chip *chip, int val);

static int oplus_track_upload_cool_down_match_err_info(struct oplus_chg_chip *chip, int err_type)
{
	int index = 0;
	int in_current;

	if (!chip)
		return 0;

	memset(chip->cool_down_match_err_load_trigger.crux_info, 0,
	       sizeof(chip->cool_down_match_err_load_trigger.crux_info));
	index += snprintf(&(chip->cool_down_match_err_load_trigger.crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_COOLDOWN_ERR);

	oplus_chg_track_get_cooldown_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->cool_down_match_err_load_trigger.crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_reason@@%s", chip->err_reason);
	in_current = oplus_convert_level_to_current(chip, chip->cool_down);
	index += snprintf(&(chip->cool_down_match_err_load_trigger.crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$cool_down@@%d"
			  "$$icharging@@%d$$current@@%d",
			  chip->cool_down, (chip->icharging * -1), in_current);
	schedule_delayed_work(&chip->cool_down_match_err_load_trigger_work, 0);
	chip->cool_down_check_done = true;
	chg_err("%s\n", chip->cool_down_match_err_load_trigger.crux_info);

	return 0;
}

static void oplus_chg_cool_down_match_err_check(struct oplus_chg_chip *chip)
{
	int in_current;
	static int cnt = 0;
	static int pre_cool_down = 0;
	int scale = 0;

	if (!chip)
		return;
	if (chip->cool_down == 0)
		return;
	if (oplus_vooc_get_fastchg_started() == false)
		return;
	if (oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC)
		return;
	if (chip->cool_down_check_done == true)
		return;
	if (chip->debug_force_cooldown_match_trigger == true) {
		oplus_track_upload_cool_down_match_err_info(chip, TRACK_COOLDOWN_NOT_MATCH);
		chip->debug_force_cooldown_match_trigger = false;
	}
	if (cnt <= COOL_DOWN_CHECK_15S) {
		cnt++;
		return;
	}
	if (pre_cool_down != chip->cool_down) {
		cnt = 0;
		pre_cool_down = chip->cool_down;
		return;
	}

	if ((chip->charger_volt * 10) / (chip->batt_volt * 10 * chip->vbatt_num) >= VOLTAGE_RATIO)
		scale = 2;
	else
		scale = 1;

	in_current = oplus_convert_level_to_current(chip, chip->cool_down);
	if (in_current == 0)
		return;
	if ((chip->icharging * -1) > (in_current * scale))
		oplus_track_upload_cool_down_match_err_info(chip, TRACK_COOLDOWN_NOT_MATCH);
}

static void oplus_chg_track_uisoc_keep_1_t_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, uisoc_keep_1_t_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_keep_1_t_load_trigger);
}

static void oplus_chg_track_vbatt_too_low_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, vbatt_too_low_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->vbatt_too_low_load_trigger);
}

static void oplus_chg_track_usbtemp_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, usbtemp_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->usbtemp_load_trigger);
}

static void oplus_chg_track_vbatt_diff_over_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, vbatt_diff_over_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->vbatt_diff_over_load_trigger);
}

static void oplus_chg_track_cool_down_match_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, cool_down_match_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->cool_down_match_err_load_trigger);
}

static int oplus_chg_track_debugfs_init(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_keep_1_t;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root)
		return -ENOENT;

	debugfs_keep_1_t = debugfs_create_dir("debug_keep_1_t", debugfs_root);
	if (!debugfs_keep_1_t) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_vbatt_too_low = false;
	chip->debug_force_uisoc_less_than_2 = false;
	chip->debug_vbat_keep_soc_1 = 0;
	chip->debug_force_usbtemp_trigger = 0;
	chip->debug_force_fast_gpio_err = 0;
	chip->debug_force_cooldown_match_trigger = false;
	debugfs_create_u32("debug_force_uisoc_less_than_2", 0644, debugfs_keep_1_t,
			   &(chip->debug_force_uisoc_less_than_2));
	debugfs_create_u32("debug_vbat_keep_soc_1", 0644, debugfs_keep_1_t, &(chip->debug_vbat_keep_soc_1));
	debugfs_create_u32("debug_force_vbatt_too_low", 0644, debugfs_root, &(chip->debug_force_vbatt_too_low));
	debugfs_create_u32("debug_force_usbtemp_trigger", 0644, debugfs_root, &(chip->debug_force_usbtemp_trigger));
	debugfs_create_u32("debug_force_fast_gpio_err", 0644, debugfs_root, &(chip->debug_force_fast_gpio_err));
	debugfs_create_u32("debug_force_cooldown_match_trigger", 0644, debugfs_root,
			   &(chip->debug_force_cooldown_match_trigger));

	return ret;
}

static int oplus_chg_track_init(struct oplus_chg_chip *chip)
{
	int rc;

	chip->uisoc_keep_1_t_load_trigger.type_reason = TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->uisoc_keep_1_t_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_UISOC_KEEP_1_T_INFO;
	chip->vbatt_too_low_load_trigger.type_reason = TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->vbatt_too_low_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_VBATT_TOO_LOW_INFO;
	chip->usbtemp_load_trigger.type_reason = TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->usbtemp_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_USBTEMP_INFO;
	chip->vbatt_diff_over_load_trigger.type_reason = TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->vbatt_diff_over_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_VBATT_DIFF_OVER_INFO;
	chip->fast_gpio_err_load_trigger.type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->fast_gpio_err_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_GPIO_ABNORMAL;
	chip->cool_down_match_err_load_trigger.type_reason = TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL;
	chip->cool_down_match_err_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_COOLDOWN_ABNORMAL;
	mutex_init(&chip->track_upload_lock);

	INIT_DELAYED_WORK(&chip->uisoc_keep_1_t_load_trigger_work, oplus_chg_track_uisoc_keep_1_t_load_trigger_work);
	INIT_DELAYED_WORK(&chip->vbatt_too_low_load_trigger_work, oplus_chg_track_vbatt_too_low_load_trigger_work);
	INIT_DELAYED_WORK(&chip->usbtemp_load_trigger_work, oplus_chg_track_usbtemp_load_trigger_work);
	INIT_DELAYED_WORK(&chip->vbatt_diff_over_load_trigger_work, oplus_chg_track_vbatt_diff_over_load_trigger_work);
	INIT_DELAYED_WORK(&chip->fast_gpio_err_load_trigger_work, oplus_chg_track_fast_gpio_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cool_down_match_err_load_trigger_work,
			  oplus_chg_track_cool_down_match_err_load_trigger_work);
	rc = oplus_chg_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("oplus chg chg debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
void oplus_chg_test_gpio_info_init(struct oplus_chg_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct gpio_chip *gpio_chip;
	int uart_tx = 0, uart_rx = 0, switch2_gpio = 0;

	if (gpio_is_valid(chip->normalchg_gpio.chargerid_switch_gpio)) {
		gpio_chip = gpio_to_chip(chip->normalchg_gpio.chargerid_switch_gpio);
		g_chg_switch1_gpio_info[SWITCH1_GPIO_INFO_INDEX].chip = gpio_chip;
		g_chg_switch1_gpio_info[SWITCH1_GPIO_INFO_INDEX].num =
			chip->normalchg_gpio.chargerid_switch_gpio - gpio_chip->base;
		chip->chg_switch1_gpio_test = test_feature_register(&g_chg_switch1_gpio_test_cfg, chip);
		if (IS_ERR_OR_NULL(chip->chg_switch1_gpio_test))
			chg_err("chg_switch1_gpio_test register error");
	}

	switch2_gpio = of_get_named_gpio(node, "oplus,switch2-ctrl-gpio", 0);
	if (gpio_is_valid(switch2_gpio)) {
		gpio_chip = gpio_to_chip(switch2_gpio);
		g_chg_switch2_gpio_info[SWITCH2_GPIO_INFO_INDEX].chip = gpio_chip;
		g_chg_switch2_gpio_info[SWITCH2_GPIO_INFO_INDEX].num =
			switch2_gpio - gpio_chip->base;
		chip->chg_switch2_gpio_test = test_feature_register(&g_chg_switch2_gpio_test_cfg, chip);
		if (IS_ERR_OR_NULL(chip->chg_switch2_gpio_test))
			chg_err("chg_switch2_gpio_test register error");
	}

	uart_tx = of_get_named_gpio(node, "oplus,uart_tx-gpio", 0);
	uart_rx = of_get_named_gpio(node, "oplus,uart_rx-gpio", 0);
	if (gpio_is_valid(uart_tx) && gpio_is_valid(uart_rx)) {
		gpio_chip = gpio_to_chip(uart_tx);
		g_chg_uart_gpio_info[UART_TX_GPIO_INFO_INDEX].chip = gpio_chip;
		g_chg_uart_gpio_info[UART_TX_GPIO_INFO_INDEX].num =
			uart_tx - gpio_chip->base;
		gpio_chip = gpio_to_chip(uart_rx);
		g_chg_uart_gpio_info[UART_RX_GPIO_INFO_INDEX].chip = gpio_chip;
		g_chg_uart_gpio_info[UART_RX_GPIO_INFO_INDEX].num =
			uart_rx - gpio_chip->base;
		chip->chg_uart_gpio_test = test_feature_register(&g_chg_uart_gpio_test_cfg, chip);
		if (IS_ERR_OR_NULL(chip->chg_uart_gpio_test))
			chg_err("chg_uart_gpio_test register error");
	}
	chg_err("test_kit gpio switch2  = %d, uart_tx = %d, uart_rx = %d\n", switch2_gpio, uart_tx, uart_rx);
}
#endif /* CONFIG_OPLUS_CHG_TEST_KIT */

int oplus_chg_init(struct oplus_chg_chip *chip)
{
	int rc = 0;
	char *thread_name = "shortc_thread";
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
	void *cookie = NULL;
#endif

	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	struct power_supply *ac_psy;
#endif

	if (!chip->chg_ops) {
		dev_err(chip->dev, "charger operations cannot be NULL\n");
		return -1;
	}
	oplus_chg_track_init(chip);
	oplus_chg_variables_init(chip);
	oplus_get_smooth_soc_switch(chip);
	oplus_chg_get_battery_data(chip);
	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		dev_err(chip->dev, "USB psy not found; deferring probe\n");
		goto power_psy_reg_failed;
	}

	chip->usb_psy = usb_psy;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	ac_psy = power_supply_get_by_name("ac");
	if (!ac_psy) {
		dev_err(chip->dev, "ac psy not found; deferring probe\n");
		goto power_psy_reg_failed;
	}
	chip->ac_psy = ac_psy;
#endif

	batt_psy = power_supply_get_by_name("battery");
	if (!batt_psy) {
		dev_err(chip->dev, "battery psy not found; deferring probe\n");
		goto power_psy_reg_failed;
	}
	chip->batt_psy = batt_psy;

#ifndef CONFIG_OPLUS_CHARGER_MTK
	chip->pmic_spmi.psy_registered = true;
#endif

	chip->flash_led_status = false;
	g_charger_chip = chip;
	oplus_chg_awake_init(chip);
	INIT_DELAYED_WORK(&chip->update_work, oplus_chg_update_work);
	INIT_DELAYED_WORK(&chip->aging_check_work, oplus_aging_check_work);
	INIT_DELAYED_WORK(&chip->ui_soc_decimal_work, oplus_chg_show_ui_soc_decimal);
	INIT_DELAYED_WORK(&chip->reset_adapter_work, oplus_chg_reset_adapter_work);
	INIT_DELAYED_WORK(&chip->turn_on_charging_work, oplus_chg_turn_on_charging_work);
	INIT_DELAYED_WORK(&chip->parallel_chg_mos_test_work, oplus_parallel_chg_mos_test_work);
	INIT_DELAYED_WORK(&chip->fg_soft_reset_work, oplus_fg_soft_reset_work);
	INIT_DELAYED_WORK(&chip->parallel_batt_chg_check_work, oplus_parallel_batt_chg_check_work);
	chip->shortc_thread = kthread_create(shortc_thread_main, (void *)chip, thread_name);
	if (!chip->shortc_thread) {
		chg_err("Can't create shortc_thread\n");
		rc = -EPROBE_DEFER;
		goto power_psy_reg_failed;
	}
	oplus_chg_bcc_thread_init();
	oplus_pps_init(chip);
	oplus_quirks_init(chip);

#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
	chip->chg_fb_notify.notifier_call = fb_notifier_callback;
	rc = msm_drm_register_client(&chip->chg_fb_notify);
#elif IS_ENABLED(CONFIG_FB)
	chip->chg_fb_notify.notifier_call = fb_notifier_callback;
	rc = fb_register_client(&chip->chg_fb_notify);
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG)
	chip->chg_fb_notify.notifier_call = chg_mtk_drm_notifier_callback;
	rc = mtk_disp_notifier_register("Oplus_Chg_v2", &chip->chg_fb_notify);
#endif /* CONFIG_FB */
	if (rc) {
		pr_err("Unable to register chg_fb_notify: %d\n", rc);
	}

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
	if (chip->active_panel) {
		cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
						       PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_CHG, chip->active_panel,
						       &chg_panel_notifier_callback, chip);
		if (!cookie) {
			pr_err("Unable to register chg_panel_notifier\n");
			goto err_check_functionality_failed;
		} else {
			pr_err("success register chg_panel_notifier\n");
			chip->notifier_cookie = cookie;
		}
	}

	if (chip->dual_panel_support && chip->active_panel_sec) {
		cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_SECONDARY,
						       PANEL_EVENT_NOTIFIER_CLIENT_SECONDARY_CHG,
						       chip->active_panel_sec, &chg_panel_notifier_callback, chip);
		if (!cookie) {
			pr_err("Unable to register panel_sec chg_panel_notifier\n");
			goto err_check_functionality_sec_failed;
		} else {
			pr_err("success register panel_sec chg_panel_notifier\n");
			chip->notifier_cookie_sec = cookie;
		}
	}
#endif

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	oplus_chg_test_gpio_info_init(chip);
	INIT_DELAYED_WORK(&det_gpio_testkit_work, oplus_chg_det_gpio_testkit_work);
	schedule_delayed_work(&det_gpio_testkit_work, msecs_to_jiffies(DETECT_DELAY_TIME * 1000));
	oplus_typec_port_test_kit_init(chip);
	INIT_DELAYED_WORK(&det_typec_testkit_work, oplus_chg_det_typec_testkit_work);
	schedule_delayed_work(&det_typec_testkit_work, msecs_to_jiffies(DET_DELAY_TIME * 1000));
#endif
	oplus_chg_debug_info_init(chip);
	init_proc_chg_log();
	init_proc_chg_cycle();
	init_proc_critical_log();
	init_proc_tbatt_pwroff();
	init_proc_batt_param_noplug();

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	init_proc_rtc_det();
	init_proc_vbat_low_det();
#endif

	init_proc_charging_feature();
	rc = init_ui_soc_decimal_proc(chip);
	rc = init_charger_proc(chip);
	/*ye.zhang add end*/
	schedule_delayed_work(&chip->update_work, OPLUS_CHG_UPDATE_INIT_DELAY);
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if ((get_eng_version() == HIGH_TEMP_AGING) || oplus_is_ptcrb_version())
		schedule_delayed_work(&chip->aging_check_work,
				      OPLUS_CHG_UPDATE_INTERVAL(OPLUS_CHG_UPDATE_INTERVAL_SEC));
#endif
	INIT_DELAYED_WORK(&chip->mmi_adapter_in_work, mmi_adapter_in_work_func);
	chip->shell_themal = thermal_zone_get_zone_by_name("shell_back");
	if (IS_ERR(chip->shell_themal)) {
		chg_err("Can't get shell_back\n");
	}
	chip->temperature = oplus_chg_match_temp_for_chging();
	charger_xlog_printk(CHG_LOG_CRTI, " end\n");
	return 0;

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
err_check_functionality_sec_failed:
	if (chip->dual_panel_support) {
		if (chip->active_panel_sec && chip->notifier_cookie_sec) {
			panel_event_notifier_unregister(chip->notifier_cookie_sec);
		}
	}
err_check_functionality_failed:
	if (chip->active_panel && chip->notifier_cookie) {
		panel_event_notifier_unregister(chip->notifier_cookie);
	}
#endif
power_psy_reg_failed:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	if (chip->ac_psy)
		power_supply_unregister(chip->ac_psy);
#endif
	if (chip->usb_psy)
		power_supply_unregister(chip->usb_psy);
	if (chip->batt_psy)
		power_supply_unregister(chip->batt_psy);
	charger_xlog_printk(CHG_LOG_CRTI, " Failed, rc = %d\n", rc);

	if (oplus_switching_support_parallel_chg()) {
		parallel_chg_check_balance_bat_status();
	}
	return rc;
}
EXPORT_SYMBOL(oplus_chg_init);

/*--------------------------------------------------------*/
int oplus_chg_parse_svooc_dt(struct oplus_chg_chip *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	mutex_init(&chip->normalchg_gpio.pinctrl_mutex);

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_num", &chip->vbatt_num);
	if (rc) {
		chip->vbatt_num = 1;
	}
	rc = of_property_read_u32(node, "qcom,vooc_project", &chip->vooc_project);
	if (rc < 0) {
		chip->vooc_project = 0;
	}

	chip->platform_fg_flag = of_property_read_bool(node, "qcom,platform_fg_flag");
	chg_err("oplus_parse_svooc_dt, chip->vbatt_num = %d,chip->vooc_project = %d.\n", chip->vbatt_num,
		chip->vooc_project);

	chip->usbtemp_dischg_by_pmic = of_property_read_bool(node, "qcom,usbtemp_dischg_by_pmic");
	charger_xlog_printk(CHG_LOG_CRTI, "usbtemp_dischg_by_pmic = %d\n", chip->usbtemp_dischg_by_pmic);

	oplus_get_chg_ops_name_from_dt(node);

	return 0;
}
EXPORT_SYMBOL(oplus_chg_parse_svooc_dt);

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
static int oplus_chg_check_panel_dt(struct oplus_chg_chip *chip)
{
	int i;
	int count;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;
	struct device_node *np = NULL;

	np = of_find_node_by_name(NULL, "oplus,dsi-display-dev");
	if (!np) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}

	count = of_count_phandle_with_args(np, "oplus,dsi-panel-primary", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "oplus,dsi-panel-primary", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			chip->active_panel = panel;
			dev_err(chip->dev, "find active_panel\n");
			break;
		}
	}

	if (chip->dual_panel_support) {
		count = of_count_phandle_with_args(np, "oplus,dsi-panel-secondary", NULL);
		if (count <= 0)
			return 0;

		for (i = 0; i < count; i++) {
			node = of_parse_phandle(np, "oplus,dsi-panel-secondary", i);
			panel = of_drm_find_panel(node);
			of_node_put(node);
			if (!IS_ERR(panel)) {
				chip->active_panel_sec = panel;
				dev_err(chip->dev, "find active_panel_sec\n");
				break;
			}
		}
	}

	return PTR_ERR(panel);
}
#endif

static bool reserve_soc_by_rus(struct oplus_chg_chip *chip)
{
	struct device_node *np;
	const char *bootparams = NULL;
	int rus_soc = 0, last_uisoc = 0;
	char *str;

	chip->shutdown_uisoc = -1;
	np = of_find_node_by_path("/chosen");
	if (np) {
		of_property_read_string(np, "bootargs", &bootparams);
		if (!bootparams) {
			chg_err("failed to get bootargs property\n");
			return false;
		}

		str = strstr(bootparams, "oplus_uisoc=");
		if (str) {
			str += strlen("oplus_uisoc=");
			get_option(&str, &last_uisoc);
			chg_info("oplus_uisoc=%d\n", last_uisoc);
			if (((last_uisoc >> 8) & 0xFF) + (last_uisoc & 0xFF) == 0x7F) {
				chip->shutdown_uisoc = (last_uisoc >> 8) & 0xFF;
				chg_info("update shutdown_uisoc = %d\n", chip->shutdown_uisoc);
			}
		}

		str = strstr(bootparams, "reserve_soc=");
		if (str) {
			str += strlen("reserve_soc=");
			get_option(&str, &rus_soc);
			chg_err("reserve_soc=%d\n", rus_soc);
			chip->rsd.rus_reserve_soc = (rus_soc >> 8) & 0xFF;
			return true;
		}
	}

	return false;
}

void oplus_chg_smooth_switch_v2_parse_dt(struct oplus_chg_chip *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return;
	}

	if (reserve_soc_by_rus(chip)) {
		chip->rsd.smooth_switch_v2 = true;
		chip->rsd.reserve_soc = chip->rsd.rus_reserve_soc;
		chg_err("override by rus %d %d\n", chip->rsd.rus_reserve_soc);
	} else {
		chip->rsd.smooth_switch_v2 = of_property_read_bool(node, "oplus,smooth_switch_v2");
		if (chip->rsd.smooth_switch_v2) {
			rc = of_property_read_u32(node, "oplus,reserve_chg_soc", &chip->rsd.reserve_soc);
			if (rc)
				chip->rsd.reserve_soc = RESERVE_SOC_DEFAULT;
		}
		chg_err("read from dts %d %d\n", chip->rsd.smooth_switch_v2, chip->rsd.reserve_soc);
	}

	if (chip->rsd.smooth_switch_v2) {
		if (chip->rsd.reserve_soc < RESERVE_SOC_MIN || chip->rsd.reserve_soc > RESERVE_SOC_MAX) {
			chip->rsd.reserve_soc = RESERVE_SOC_OFF;
			chip->rsd.smooth_switch_v2 = false;
		}
	}

	chg_err("smooth_switch_v2 %d reserve_soc %d \n", chip->rsd.smooth_switch_v2, chip->rsd.reserve_soc);
}

void oplus_chg_aging_ffc_variable_reset(struct oplus_chg_chip *chip)
{
	chip->limits.ffc1_normal_vfloat_sw_limit = chip->limits.default_ffc1_normal_vfloat_sw_limit;
	chip->limits.ffc1_warm_vfloat_sw_limit = chip->limits.default_ffc1_warm_vfloat_sw_limit;
	chip->limits.ffc2_normal_vfloat_sw_limit = chip->limits.default_ffc2_normal_vfloat_sw_limit;
	chip->limits.ffc2_warm_vfloat_sw_limit = chip->limits.default_ffc2_warm_vfloat_sw_limit;
}

void oplus_chg_get_aging_ffc_offset(struct oplus_chg_chip *chip, int *ffc1_offset, int *ffc2_offset)
{
	int batt_cc = 0;

	if (!chip || !ffc1_offset || !ffc2_offset)
		return;

	*ffc1_offset = 0;
	*ffc2_offset = 0;

	if (chip->aging_ffc_version == AGING_FFC_NOT_SUPPORT)
		return;

	if (oplus_switching_support_parallel_chg() && chip->sub_batt_temperature == FG_I2C_ERROR) {
		chg_err("sub battery error\n");
		return;
	}

	if (chip->debug_batt_cc)
		batt_cc = chip->debug_batt_cc;
	else
		batt_cc = chip->batt_cc;

	if (chip->vbatt_num == 2) {
		if (batt_cc >= AGING2_STAGE_CYCLE) {
			*ffc1_offset = AGING2_FFC1_DOUBLE_OFFSET_MV;
			*ffc2_offset = AGING2_FFC2_DOUBLE_OFFSET_MV;
		} else if (batt_cc >= AGING1_STAGE_CYCLE) {
			*ffc1_offset = AGING1_FFC1_DOUBLE_OFFSET_MV;
			*ffc2_offset = AGING1_FFC2_DOUBLE_OFFSET_MV;
		}
	} else {
		if (batt_cc >= AGING2_STAGE_CYCLE) {
			*ffc1_offset = AGING2_FFC1_SINGLE_OFFSET_MV;
			*ffc2_offset = AGING2_FFC2_SINGLE_OFFSET_MV;
		} else if (batt_cc >= AGING1_STAGE_CYCLE) {
			*ffc1_offset = AGING1_FFC1_SINGLE_OFFSET_MV;
			*ffc2_offset = AGING1_FFC2_SINGLE_OFFSET_MV;
		}
	}
}

void oplus_chg_aging_ffc_action(struct oplus_chg_chip *chip, bool ffc1_stage)
{
	int ffc1_voltage_offset = 0;
	int ffc2_voltage_offset = 0;

	if (chip->aging_ffc_version == AGING_FFC_NOT_SUPPORT)
		return;

	oplus_chg_aging_ffc_variable_reset(chip);

	oplus_chg_get_aging_ffc_offset(chip, &ffc1_voltage_offset, &ffc2_voltage_offset);

	chip->limits.ffc1_normal_vfloat_sw_limit =
		chip->limits.default_ffc1_normal_vfloat_sw_limit + ffc1_voltage_offset;
	chip->limits.ffc1_warm_vfloat_sw_limit = chip->limits.default_ffc1_warm_vfloat_sw_limit + ffc1_voltage_offset;
	chip->limits.ffc2_normal_vfloat_sw_limit =
		chip->limits.default_ffc2_normal_vfloat_sw_limit + ffc2_voltage_offset;
	chip->limits.ffc2_warm_vfloat_sw_limit = chip->limits.default_ffc2_warm_vfloat_sw_limit + ffc2_voltage_offset;

	if (ffc1_voltage_offset && ffc2_voltage_offset)
		oplus_chg_track_aging_ffc_trigger(ffc1_stage);

	chg_err("batt_cc=%d %d [%d %d %d %d]\n", chip->debug_batt_cc, chip->batt_cc,
		chip->limits.ffc1_normal_vfloat_sw_limit, chip->limits.ffc1_warm_vfloat_sw_limit,
		chip->limits.ffc2_normal_vfloat_sw_limit, chip->limits.ffc2_warm_vfloat_sw_limit);
}

void oplus_chg_usbtemp_v2_parse_dt(struct oplus_chg_chip *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_batt_temp_low", &chip->usbtemp_batt_temp_low);
	if (rc) {
		chip->usbtemp_batt_temp_low = OPCHG_USBTEMP_BATT_TEMP_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_batt_temp_high", &chip->usbtemp_batt_temp_high);
	if (rc) {
		chip->usbtemp_batt_temp_high = OPCHG_USBTEMP_BATT_TEMP_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_ntc_temp_low", &chip->usbtemp_ntc_temp_low);
	if (rc) {
		chip->usbtemp_ntc_temp_low = OPCHG_USBTEMP_NTC_TEMP_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_ntc_temp_high", &chip->usbtemp_ntc_temp_high);
	if (rc) {
		chip->usbtemp_ntc_temp_high = OPCHG_USBTEMP_NTC_TEMP_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_temp_gap_low_with_batt_temp",
				  &chip->usbtemp_temp_gap_low_with_batt_temp);
	if (rc) {
		chip->usbtemp_temp_gap_low_with_batt_temp = OPCHG_USBTEMP_GAP_LOW_WITH_BATT_TEMP;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_temp_gap_high_with_batt_temp",
				  &chip->usbtemp_temp_gap_high_with_batt_temp);
	if (rc) {
		chip->usbtemp_temp_gap_high_with_batt_temp = OPCHG_USBTEMP_GAP_HIGH_WITH_BATT_TEMP;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_temp_gap_low_without_batt_temp",
				  &chip->usbtemp_temp_gap_low_without_batt_temp);
	if (rc) {
		chip->usbtemp_temp_gap_low_without_batt_temp = OPCHG_USBTEMP_GAP_LOW_WITHOUT_BATT_TEMP;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_temp_gap_high_without_batt_temp",
				  &chip->usbtemp_temp_gap_high_without_batt_temp);
	if (rc) {
		chip->usbtemp_temp_gap_high_without_batt_temp = OPCHG_USBTEMP_GAP_HIGH_WITHOUT_BATT_TEMP;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_rise_fast_temp_low", &chip->usbtemp_rise_fast_temp_low);
	if (rc) {
		chip->usbtemp_rise_fast_temp_low = OPCHG_USBTEMP_RISE_FAST_TEMP_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_rise_fast_temp_high", &chip->usbtemp_rise_fast_temp_high);
	if (rc) {
		chip->usbtemp_rise_fast_temp_high = OPCHG_USBTEMP_RISE_FAST_TEMP_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_rise_fast_temp_count_low",
				  &chip->usbtemp_rise_fast_temp_count_low);
	if (rc) {
		chip->usbtemp_rise_fast_temp_count_low = OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_rise_fast_temp_count_high",
				  &chip->usbtemp_rise_fast_temp_count_high);
	if (rc) {
		chip->usbtemp_rise_fast_temp_count_high = OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_ntc_low", &chip->usbtemp_cool_down_ntc_low);
	if (rc) {
		chip->usbtemp_cool_down_ntc_low = OPCHG_USBTEMP_COOL_DOWN_NTC_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_ntc_high", &chip->usbtemp_cool_down_ntc_high);
	if (rc) {
		chip->usbtemp_cool_down_ntc_high = OPCHG_USBTEMP_COOL_DOWN_NTC_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_gap_low", &chip->usbtemp_cool_down_gap_low);
	if (rc) {
		chip->usbtemp_cool_down_gap_low = OPCHG_USBTEMP_COOL_DOWN_GAP_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_gap_high", &chip->usbtemp_cool_down_gap_high);
	if (rc) {
		chip->usbtemp_cool_down_gap_high = OPCHG_USBTEMP_COOL_DOWN_GAP_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recover_ntc_low",
				  &chip->usbtemp_cool_down_recover_ntc_low);
	if (rc) {
		chip->usbtemp_cool_down_recover_ntc_low = OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recover_ntc_high",
				  &chip->usbtemp_cool_down_recover_ntc_high);
	if (rc) {
		chip->usbtemp_cool_down_recover_ntc_high = OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recover_gap_low",
				  &chip->usbtemp_cool_down_recover_gap_low);
	if (rc) {
		chip->usbtemp_cool_down_recover_gap_low = OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_LOW;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recover_gap_high",
				  &chip->usbtemp_cool_down_recover_gap_high);
	if (rc) {
		chip->usbtemp_cool_down_recover_gap_high = OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_HIGH;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recoup_high",
				  &chip->usbtemp_cool_down_recoup_high);
	if (rc) {
		chip->usbtemp_cool_down_recoup_high = OPCHG_USBTEMP_COOL_DOWN_RECOUP_HIGH;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recoup_low",
				  &chip->usbtemp_cool_down_recoup_low);
	if (rc) {
		chip->usbtemp_cool_down_recoup_low = OPCHG_USBTEMP_COOL_DOWN_RECOUP_LOW;
	}
}

int oplus_chg_parse_charger_dt(struct oplus_chg_chip *chip)
{
	int rc, i, length = 0;
	struct device_node *node = chip->dev->of_node;
	int batt_cold_degree_negative;
	int batt_removed_degree_negative;
	const char *batt_str = chip->batt_type_string;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,input_current_charger_ma", &chip->limits.input_current_charger_ma);
	if (rc) {
		chip->limits.input_current_charger_ma = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
	}
	rc = of_property_read_u32(node, "qcom,pd_input_current_charger_ma", &chip->limits.pd_input_current_charger_ma);
	if (rc) {
		chip->limits.pd_input_current_charger_ma = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
	}
	chip->pd_disable = of_property_read_bool(node, "qcom,pd_disable");
	rc = of_property_read_u32(node, "qcom,qc_input_current_charger_ma", &chip->limits.qc_input_current_charger_ma);
	if (rc) {
		chip->limits.qc_input_current_charger_ma = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
	}

	rc = of_property_read_u32(node, "qcom,input_current_usb_ma", &chip->limits.input_current_usb_ma);
	if (rc) {
		chip->limits.input_current_usb_ma = OPCHG_INPUT_CURRENT_LIMIT_USB_MA;
	}

	rc = of_property_read_u32(node, "qcom,input_current_cdp_ma", &chip->limits.input_current_cdp_ma);
	if (rc) {
		chip->limits.input_current_cdp_ma = OPCHG_INPUT_CURRENT_LIMIT_USB_MA;
	}

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version()) {
		chip->limits.input_current_led_ma = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,led on current 2A \n");
	} else
#endif
	{
		rc = of_property_read_u32(node, "qcom,input_current_led_ma", &chip->limits.input_current_led_ma);
		if (rc) {
			chip->limits.input_current_led_ma = OPCHG_INPUT_CURRENT_LIMIT_LED_MA;
		}
	}

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version()) {
		chip->limits.input_current_led_ma_high = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here, led_ma_high on current 2A \n");
	} else
#endif
	{
		rc = of_property_read_u32(node, "qcom,input_current_led_ma_high",
					  &chip->limits.input_current_led_ma_high);
		if (rc) {
			chip->limits.input_current_led_ma_high = chip->limits.input_current_led_ma;
		}
	}

	rc = of_property_read_u32(node, "qcom,led_high_bat_decidegc", &chip->limits.led_high_bat_decidegc);
	if (rc) {
		chip->limits.led_high_bat_decidegc = 370;
	}

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version()) {
		chip->limits.input_current_led_ma_warm = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here, led_ma_warm on current 2A \n");
	} else
#endif
	{
		rc = of_property_read_u32(node, "qcom,input_current_led_ma_warm",
					  &chip->limits.input_current_led_ma_warm);
		if (rc) {
			chip->limits.input_current_led_ma_warm = chip->limits.input_current_led_ma;
		}
	}

	rc = of_property_read_u32(node, "qcom,led_warm_bat_decidegc", &chip->limits.led_warm_bat_decidegc);
	if (rc) {
		chip->limits.led_warm_bat_decidegc = 350;
	}

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version()) {
		chip->limits.input_current_led_ma_normal = OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA;
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here, led_ma_normal on current 2A \n");
	} else
#endif
	{
		rc = of_property_read_u32(node, "qcom,input_current_led_ma_normal",
					  &chip->limits.input_current_led_ma_normal);
		if (rc) {
			chip->limits.input_current_led_ma_normal = chip->limits.input_current_led_ma;
		}
	}

	rc = of_property_read_u32(node, "qcom,input_current_camera_ma", &chip->limits.input_current_camera_ma);
	if (rc) {
		chip->limits.input_current_camera_ma = OPCHG_INPUT_CURRENT_LIMIT_CAMERA_MA;
	}
	chip->limits.iterm_disabled = of_property_read_bool(node, "qcom,iterm_disabled");
	rc = of_property_read_u32(node, "qcom,iterm_ma", &chip->limits.iterm_ma);
	if (rc < 0) {
		chip->limits.iterm_ma = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,sub_iterm_ma", &chip->limits.sub_iterm_ma);
	if (rc < 0) {
		chip->limits.sub_iterm_ma = -EINVAL;
	}
	chip->skip_usbtemp_cool_down = of_property_read_bool(node, "qcom,skip_usbtemp_cool_down");
	chip->smart_charging_screenoff = of_property_read_bool(node, "qcom,smart_charging_screenoff");
	rc = of_property_read_u32(node, "qcom,input_current_calling_ma", &chip->limits.input_current_calling_ma);
	if (rc) {
		chip->limits.input_current_calling_ma = OPCHG_INPUT_CURRENT_LIMIT_CALLING_MA;
	}
	rc = of_property_read_u32(node, "qcom,recharge-mv", &chip->limits.recharge_mv);
	if (rc < 0) {
		chip->limits.recharge_mv = -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,usb_high_than_bat_decidegc", &chip->limits.usb_high_than_bat_decidegc);
	if (rc < 0) {
		chip->limits.usb_high_than_bat_decidegc = 100;
	}
	chg_err("usb_high_than_bat_decidegc:%d\n", chip->limits.usb_high_than_bat_decidegc);

	/*-19C*/
	rc = of_property_read_u32(node, "qcom,removed_bat_decidegc", &batt_removed_degree_negative);
	if (rc < 0) {
		chip->limits.removed_bat_decidegc = -190;
	} else {
		chip->limits.removed_bat_decidegc = -batt_removed_degree_negative;
	}
	/*-3~0 C*/

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version()) {
		chg_err(" CONFIG_HIGH_TEMP_VERSION enable here,disable low tbat chg \n");
		batt_cold_degree_negative = 170;
		chip->limits.cold_bat_decidegc = -batt_cold_degree_negative;
	} else
#endif
	{
		chg_err(" CONFIG_HIGH_TEMP_VERSION disabled\n");
		rc = of_property_read_u32(node, "qcom,cold_bat_decidegc", &batt_cold_degree_negative);
		if (rc < 0) {
			chip->limits.cold_bat_decidegc = -EINVAL;
		} else {
			chip->limits.cold_bat_decidegc = -batt_cold_degree_negative;
		}
	}

	rc = of_property_read_u32(node, "qcom,temp_cold_vfloat_mv", &chip->limits.temp_cold_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_cold_vfloat_mv fail\n");
	}
	rc = of_property_read_u32(node, "qcom,temp_cold_fastchg_current_ma",
				  &chip->limits.temp_cold_fastchg_current_ma);
	if (rc < 0) {
		chg_err(" temp_cold_fastchg_current_ma fail\n");
	}
	rc = of_property_read_u32(node, "qcom,temp_cold_fastchg_current_ma_high",
				  &chip->limits.temp_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_cold_fastchg_current_ma_high = chip->limits.temp_cold_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,temp_cold_fastchg_current_ma_low",
				  &chip->limits.temp_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_cold_fastchg_current_ma_low = chip->limits.temp_cold_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_cold_fastchg_current_ma_high",
				  &chip->limits.pd_temp_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.pd_temp_cold_fastchg_current_ma_high = chip->limits.temp_cold_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_cold_fastchg_current_ma_low",
				  &chip->limits.pd_temp_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.pd_temp_cold_fastchg_current_ma_low = chip->limits.temp_cold_fastchg_current_ma_low;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_cold_fastchg_current_ma_high",
				  &chip->limits.qc_temp_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.qc_temp_cold_fastchg_current_ma_high = chip->limits.temp_cold_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_cold_fastchg_current_ma_low",
				  &chip->limits.qc_temp_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.qc_temp_cold_fastchg_current_ma_low = chip->limits.temp_cold_fastchg_current_ma_low;
	}

	rc = of_property_read_u32(node, "qcom,freeze_bat_decidegc", &batt_cold_degree_negative);
	if (rc < 0) {
		chip->limits.freeze_bat_decidegc = chip->limits.cold_bat_decidegc;
	} else {
		chip->limits.freeze_bat_decidegc = -batt_cold_degree_negative;
	}

	rc = of_property_read_u32(node, "qcom,temp_freeze_fastchg_current_ma",
				  &chip->limits.temp_freeze_fastchg_current_ma);
	if (rc < 0) {
		chg_err(" temp_freeze_fastchg_current_ma fail\n");
	}
	rc = of_property_read_u32(node, "qcom,temp_freeze_fastchg_current_ma_high",
				  &chip->limits.temp_freeze_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_freeze_fastchg_current_ma_high = chip->limits.temp_freeze_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,temp_freeze_fastchg_current_ma_low",
				  &chip->limits.temp_freeze_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_freeze_fastchg_current_ma_low = chip->limits.temp_freeze_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_freeze_fastchg_current_ma_high",
				  &chip->limits.pd_temp_freeze_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.pd_temp_freeze_fastchg_current_ma_high = chip->limits.temp_freeze_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_freeze_fastchg_current_ma_low",
				  &chip->limits.pd_temp_freeze_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.pd_temp_freeze_fastchg_current_ma_low = chip->limits.temp_freeze_fastchg_current_ma_low;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_freeze_fastchg_current_ma_high",
				  &chip->limits.qc_temp_freeze_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.qc_temp_freeze_fastchg_current_ma_high = chip->limits.temp_freeze_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_freeze_fastchg_current_ma_low",
				  &chip->limits.qc_temp_freeze_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.qc_temp_freeze_fastchg_current_ma_low = chip->limits.temp_freeze_fastchg_current_ma_low;
	}
	chg_err(" freeze current [%d, %d, %d, %d]\n", chip->limits.freeze_bat_decidegc,
		chip->limits.temp_freeze_fastchg_current_ma, chip->limits.temp_freeze_fastchg_current_ma_high,
		chip->limits.temp_freeze_fastchg_current_ma_low);

	/*0~5 C*/
	rc = of_property_read_u32(node, "qcom,little_cold_bat_decidegc", &chip->limits.little_cold_bat_decidegc);
	if (rc < 0) {
		chip->limits.little_cold_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_vfloat_mv", &chip->limits.temp_little_cold_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cold_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_fastchg_current_ma",
				  &chip->limits.temp_little_cold_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_little_cold_fastchg_current_ma = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_fastchg_current_ma_high",
				  &chip->limits.temp_little_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_little_cold_fastchg_current_ma_high =
			chip->limits.temp_little_cold_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cold_fastchg_current_ma_low",
				  &chip->limits.temp_little_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_little_cold_fastchg_current_ma_low = chip->limits.temp_little_cold_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_little_cold_fastchg_current_ma_high",
				  &chip->limits.pd_temp_little_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.pd_temp_little_cold_fastchg_current_ma_high =
			chip->limits.temp_little_cold_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_little_cold_fastchg_current_ma_low",
				  &chip->limits.pd_temp_little_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.pd_temp_little_cold_fastchg_current_ma_low =
			chip->limits.temp_little_cold_fastchg_current_ma_low;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_little_cold_fastchg_current_ma_high",
				  &chip->limits.qc_temp_little_cold_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.qc_temp_little_cold_fastchg_current_ma_high =
			chip->limits.temp_little_cold_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_little_cold_fastchg_current_ma_low",
				  &chip->limits.qc_temp_little_cold_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.qc_temp_little_cold_fastchg_current_ma_low =
			chip->limits.temp_little_cold_fastchg_current_ma_low;
	}

	/*5~12 C*/
	rc = of_property_read_u32(node, "qcom,cool_bat_decidegc", &chip->limits.cool_bat_decidegc);
	if (rc < 0) {
		chip->limits.cool_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_cool_vfloat_mv", &chip->limits.temp_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_cool_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_cool_fastchg_current_ma_high",
				  &chip->limits.temp_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_cool_fastchg_current_ma_high = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_cool_fastchg_current_ma_low",
				  &chip->limits.temp_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_cool_fastchg_current_ma_low = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_cool_fastchg_current_ma_high",
				  &chip->limits.pd_temp_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.pd_temp_cool_fastchg_current_ma_high = chip->limits.temp_cool_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_cool_fastchg_current_ma_low",
				  &chip->limits.pd_temp_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.pd_temp_cool_fastchg_current_ma_low = chip->limits.temp_cool_fastchg_current_ma_low;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_cool_fastchg_current_ma_high",
				  &chip->limits.qc_temp_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.qc_temp_cool_fastchg_current_ma_high = chip->limits.temp_cool_fastchg_current_ma_high;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_cool_fastchg_current_ma_low",
				  &chip->limits.qc_temp_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.qc_temp_cool_fastchg_current_ma_low = chip->limits.temp_cool_fastchg_current_ma_low;
	}

	/*12~16 C*/
	rc = of_property_read_u32(node, "qcom,little_cool_bat_decidegc", &chip->limits.little_cool_bat_decidegc);
	if (rc < 0) {
		chip->limits.little_cool_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cool_vfloat_mv", &chip->limits.temp_little_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cool_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_little_cool_fastchg_current_ma",
				  &chip->limits.temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_little_cool_fastchg_current_ma = -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cool_fastchg_current_ma_high",
				  &chip->limits.temp_little_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.temp_little_cool_fastchg_current_ma_high =
			chip->limits.temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cool_fastchg_current_ma_low",
				  &chip->limits.temp_little_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.temp_little_cool_fastchg_current_ma_low = chip->limits.temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,pd_temp_little_cool_fastchg_current_ma",
				  &chip->limits.pd_temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.pd_temp_little_cool_fastchg_current_ma = chip->limits.temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,pd_temp_little_cool_fastchg_current_ma_high",
				  &chip->limits.pd_temp_little_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.pd_temp_little_cool_fastchg_current_ma_high =
			chip->limits.pd_temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,pd_temp_little_cool_fastchg_current_ma_low",
				  &chip->limits.pd_temp_little_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.pd_temp_little_cool_fastchg_current_ma_low =
			chip->limits.pd_temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,qc_temp_little_cool_fastchg_current_ma",
				  &chip->limits.qc_temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.qc_temp_little_cool_fastchg_current_ma = chip->limits.temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,qc_temp_little_cool_fastchg_current_ma_high",
				  &chip->limits.qc_temp_little_cool_fastchg_current_ma_high);
	if (rc < 0) {
		chip->limits.qc_temp_little_cool_fastchg_current_ma_high =
			chip->limits.qc_temp_little_cool_fastchg_current_ma;
	}

	rc = of_property_read_u32(node, "qcom,qc_temp_little_cool_fastchg_current_ma_low",
				  &chip->limits.qc_temp_little_cool_fastchg_current_ma_low);
	if (rc < 0) {
		chip->limits.qc_temp_little_cool_fastchg_current_ma_low =
			chip->limits.qc_temp_little_cool_fastchg_current_ma;
	}

	/*16~45 C*/
	rc = of_property_read_u32(node, "qcom,normal_bat_decidegc", &chip->limits.normal_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_bat_decidegc fail\n");
	}
	rc = of_property_read_u32(node, "qcom,temp_normal_fastchg_current_ma",
				  &chip->limits.temp_normal_fastchg_current_ma);
	if (rc) {
		chip->limits.temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}
	rc = of_property_read_u32(node, "qcom,temp_normal_vfloat_mv", &chip->limits.temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_normal_vfloat_mv = 4320;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_normal_fastchg_current_ma",
				  &chip->limits.pd_temp_normal_fastchg_current_ma);
	if (rc) {
		chip->limits.pd_temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_normal_fastchg_current_ma",
				  &chip->limits.qc_temp_normal_fastchg_current_ma);
	if (rc) {
		chip->limits.qc_temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}

	/* 16C ~ 22C */
	rc = of_property_read_u32(node, "qcom,normal_phase1_bat_decidegc", &chip->limits.normal_phase1_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase1_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase1_vfloat_mv",
				  &chip->limits.temp_normal_phase1_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase1_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase1_fastchg_current_ma",
				  &chip->limits.temp_normal_phase1_fastchg_current_ma);
	if (rc < 0) {
		chg_err(" temp_normal_phase1_fastchg_current_ma fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase2_bat_decidegc", &chip->limits.normal_phase2_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase2_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase2_vfloat_mv",
				  &chip->limits.temp_normal_phase2_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase2_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase2_fastchg_current_ma_high",
				  &chip->limits.temp_normal_phase2_fastchg_current_ma_high);
	if (rc < 0) {
		chg_err(" temp_normal_phase2_fastchg_current_ma_high fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase2_fastchg_current_ma_low",
				  &chip->limits.temp_normal_phase2_fastchg_current_ma_low);
	if (rc < 0) {
		chg_err(" temp_normal_phase2_fastchg_current_ma_low fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase3_bat_decidegc", &chip->limits.normal_phase3_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase3_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase3_vfloat_mv",
				  &chip->limits.temp_normal_phase3_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase3_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase3_fastchg_current_ma_high",
				  &chip->limits.temp_normal_phase3_fastchg_current_ma_high);
	if (rc < 0) {
		chg_err(" temp_normal_phase3_fastchg_current_ma_high fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase3_fastchg_current_ma_low",
				  &chip->limits.temp_normal_phase3_fastchg_current_ma_low);
	if (rc < 0) {
		chg_err(" temp_normal_phase3_fastchg_current_ma_low fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase4_bat_decidegc", &chip->limits.normal_phase4_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase4_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase4_vfloat_mv",
				  &chip->limits.temp_normal_phase4_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase4_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase4_fastchg_current_ma_high",
				  &chip->limits.temp_normal_phase4_fastchg_current_ma_high);
	if (rc < 0) {
		chg_err(" temp_normal_phase4_fastchg_current_ma_high fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase4_fastchg_current_ma_low",
				  &chip->limits.temp_normal_phase4_fastchg_current_ma_low);
	if (rc < 0) {
		chg_err(" temp_normal_phase4_fastchg_current_ma_low fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase5_bat_decidegc", &chip->limits.normal_phase5_bat_decidegc);
	if (rc < 0) {
		chg_err(" normal_phase5_bat_decidegc fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase5_vfloat_mv",
				  &chip->limits.temp_normal_phase5_vfloat_mv);
	if (rc < 0) {
		chg_err(" temp_normal_phase5_vfloat_mv fail\n");
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase5_fastchg_current_ma",
				  &chip->limits.temp_normal_phase5_fastchg_current_ma);
	if (rc < 0) {
		chg_err(" temp_normal_phase5_fastchg_current_ma fail\n");
	}

	rc = of_property_read_u32(node, "qcom,normal_phase6_bat_decidegc", &chip->limits.normal_phase6_bat_decidegc);
	if (rc < 0) {
		chip->limits.normal_phase6_bat_decidegc = 420;
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase6_vfloat_mv",
				  &chip->limits.temp_normal_phase6_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_normal_phase6_vfloat_mv = chip->limits.temp_normal_phase5_vfloat_mv;
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_phase6_fastchg_current_ma",
				  &chip->limits.temp_normal_phase6_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_normal_phase6_fastchg_current_ma = chip->limits.temp_normal_phase5_fastchg_current_ma;
	}

	chip->vooc_enable_charger_first = of_property_read_bool(node, "qcom,vooc_enable_charger_first");
	chip->smart_chg_bcc_support = of_property_read_bool(node, "qcom,smart_chg_bcc_support");
	chg_err("qcom,smart_chg_bcc_support is %d\n", chip->smart_chg_bcc_support);

	chip->smart_chg_soh_support = of_property_read_bool(node, "qcom,smart_chg_soh_support");
	chg_err("qcom,smart_chg_soh_support is %d\n", chip->smart_chg_soh_support);

	charger_xlog_printk(
		CHG_LOG_CRTI, "normal_phase1_bat_decidegc = %d, \
			temp_normal_phase1_vfloat_mv = %d, \
			temp_normal_phase1_fastchg_current_ma = %d, \
			normal_phase2_bat_decidegc = %d, \
			temp_normal_phase2_vfloat_mv = %d, \
			temp_normal_phase2_fastchg_current_ma_high = %d, \
			temp_normal_phase2_fastchg_current_ma_low = %d, \
			normal_phase3_bat_decidegc = %d, \
			temp_normal_phase3_vfloat_mv = %d, \
			temp_normal_phase3_fastchg_current_ma_high = %d, \
			temp_normal_phase3_fastchg_current_ma_low = %d, \
			normal_phase4_bat_decidegc = %d, \
			temp_normal_phase4_vfloat_mv = %d, \
			temp_normal_phase4_fastchg_current_ma_high = %d, \
			temp_normal_phase4_fastchg_current_ma_low = %d, \
			normal_phase5_bat_decidegc = %d, \
			temp_normal_phase5_vfloat_mv = %d, \
			temp_normal_phase5_fastchg_current_ma = %d, \
			normal_phase6_bat_decidegc = %d, \
			temp_normal_phase6_vfloat_mv = %d, \
			temp_normal_phase6_fastchg_current_ma = %d\n",
		chip->limits.normal_phase1_bat_decidegc, chip->limits.temp_normal_phase1_vfloat_mv,
		chip->limits.temp_normal_phase1_fastchg_current_ma, chip->limits.normal_phase2_bat_decidegc,
		chip->limits.temp_normal_phase2_vfloat_mv, chip->limits.temp_normal_phase2_fastchg_current_ma_high,
		chip->limits.temp_normal_phase2_fastchg_current_ma_low, chip->limits.normal_phase3_bat_decidegc,
		chip->limits.temp_normal_phase3_vfloat_mv, chip->limits.temp_normal_phase3_fastchg_current_ma_high,
		chip->limits.temp_normal_phase3_fastchg_current_ma_low, chip->limits.normal_phase4_bat_decidegc,
		chip->limits.temp_normal_phase4_vfloat_mv, chip->limits.temp_normal_phase4_fastchg_current_ma_high,
		chip->limits.temp_normal_phase4_fastchg_current_ma_low, chip->limits.normal_phase5_bat_decidegc,
		chip->limits.temp_normal_phase5_vfloat_mv, chip->limits.temp_normal_phase5_fastchg_current_ma,
		chip->limits.normal_phase6_bat_decidegc, chip->limits.temp_normal_phase6_vfloat_mv,
		chip->limits.temp_normal_phase6_fastchg_current_ma);

	/*45~55 C*/
	rc = of_property_read_u32(node, "qcom,warm_bat_decidegc", &chip->limits.warm_bat_decidegc);
	if (rc < 0) {
		chip->limits.warm_bat_decidegc = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_warm_vfloat_mv", &chip->limits.temp_warm_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_warm_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,temp_warm_fastchg_current_ma",
				  &chip->limits.temp_warm_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.temp_warm_fastchg_current_ma = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,pd_temp_warm_fastchg_current_ma",
				  &chip->limits.pd_temp_warm_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.pd_temp_warm_fastchg_current_ma = chip->limits.temp_warm_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,qc_temp_warm_fastchg_current_ma",
				  &chip->limits.qc_temp_warm_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.qc_temp_warm_fastchg_current_ma = chip->limits.temp_warm_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,temp_warm_fastchg_current_ma_led_on",
				  &chip->limits.temp_warm_fastchg_current_ma_led_on);
	if (rc < 0) {
		chip->limits.temp_warm_fastchg_current_ma_led_on = chip->limits.temp_warm_fastchg_current_ma;
	}

	/*>55 C*/
	rc = of_property_read_u32(node, "qcom,hot_bat_decidegc", &chip->limits.hot_bat_decidegc);
	if (rc < 0) {
		chip->limits.hot_bat_decidegc = -EINVAL;
	}
	/*offset temperature, only for userspace, default 0*/
	rc = of_property_read_u32(node, "qcom,offset_temp", &chip->offset_temp);
	if (rc < 0) {
		chip->offset_temp = 0;
	}
	/*non standard battery*/
	rc = of_property_read_u32(node, "qcom,non_standard_vfloat_mv", &chip->limits.non_standard_vfloat_mv);
	if (rc < 0) {
		chip->limits.non_standard_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,non_standard_fastchg_current_ma",
				  &chip->limits.non_standard_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.non_standard_fastchg_current_ma = -EINVAL;
	}
	/*short circuit battery*/
	rc = of_property_read_u32(node, "qcom,short_c_bat_cv_mv", &chip->short_c_batt.short_c_bat_cv_mv);
	if (rc < 0) {
		chip->short_c_batt.short_c_bat_cv_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,short_c_bat_vfloat_mv", &chip->limits.short_c_bat_vfloat_mv);
	if (rc < 0) {
		chip->limits.short_c_bat_vfloat_mv = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,short_c_bat_fastchg_current_ma",
				  &chip->limits.short_c_bat_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.short_c_bat_fastchg_current_ma = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,short_c_bat_vfloat_sw_limit", &chip->limits.short_c_bat_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.short_c_bat_vfloat_sw_limit = -EINVAL;
	}

	/*vfloat_sw_limit*/
	rc = of_property_read_u32(node, "qcom,non_standard_vfloat_sw_limit",
				  &chip->limits.non_standard_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.non_standard_vfloat_sw_limit = 3960;
	}
	rc = of_property_read_u32(node, "qcom,cold_vfloat_sw_limit", &chip->limits.cold_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.cold_vfloat_sw_limit = 3960;
	}
	rc = of_property_read_u32(node, "qcom,little_cold_vfloat_sw_limit", &chip->limits.little_cold_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.little_cold_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,cool_vfloat_sw_limit", &chip->limits.cool_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.cool_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,little_cool_vfloat_sw_limit", &chip->limits.little_cool_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.little_cool_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,normal_vfloat_sw_limit", &chip->limits.normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.normal_vfloat_sw_limit = 4330;
	}
	rc = of_property_read_u32(node, "qcom,warm_vfloat_sw_limit", &chip->limits.warm_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.warm_vfloat_sw_limit = 4060;
	}

	/*vfloat_over_sw_limit*/
	chip->limits.sw_vfloat_over_protect_enable = of_property_read_bool(node, "qcom,sw_vfloat_over_protect_enable");
	rc = of_property_read_u32(node, "qcom,non_standard_vfloat_over_sw_limit",
				  &chip->limits.non_standard_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.non_standard_vfloat_over_sw_limit = 3980;
	}
	rc = of_property_read_u32(node, "qcom,cold_vfloat_over_sw_limit", &chip->limits.cold_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.cold_vfloat_over_sw_limit = 3980;
	}
	rc = of_property_read_u32(node, "qcom,little_cold_vfloat_over_sw_limit",
				  &chip->limits.little_cold_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cold_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,cool_vfloat_over_sw_limit", &chip->limits.cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.cool_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,little_cool_vfloat_over_sw_limit",
				  &chip->limits.little_cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cool_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,normal_vfloat_over_sw_limit", &chip->limits.normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.normal_vfloat_over_sw_limit = 4390;
	}
	rc = of_property_read_u32(node, "qcom,warm_vfloat_over_sw_limit", &chip->limits.warm_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.warm_vfloat_over_sw_limit = 4080;
	}
	rc = of_property_read_u32(node, "qcom,charger_hv_thr", &chip->limits.charger_hv_thr);
	if (rc < 0) {
		chip->limits.charger_hv_thr = 5800;
	}
	rc = of_property_read_u32(node, "qcom,charger_recv_thr", &chip->limits.charger_recv_thr);
	if (rc < 0) {
		chip->limits.charger_recv_thr = 5800;
	}
	rc = of_property_read_u32(node, "qcom,charger_lv_thr", &chip->limits.charger_lv_thr);
	if (rc < 0) {
		chip->limits.charger_lv_thr = 3400;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_full_thr", &chip->limits.vbatt_full_thr);
	if (rc < 0) {
		chip->limits.vbatt_full_thr = 4400;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_hv_thr", &chip->limits.vbatt_hv_thr);
	if (rc < 0) {
		chip->limits.vbatt_hv_thr = 4500;
	}
	rc = of_property_read_u32(node, "qcom,vfloat_step_mv", &chip->limits.vfloat_step_mv);
	if (rc < 0) {
		chip->limits.vfloat_step_mv = 16;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_power_off", &chip->vbatt_power_off);
	if (rc < 0) {
		chip->vbatt_power_off = 3300;
	}
	rc = of_property_read_u32(node, "qcom,sub_vbatt_power_off", &chip->sub_vbatt_power_off);
	if (rc < 0) {
		chip->sub_vbatt_power_off = 3300;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_soc_1", &chip->vbatt_soc_1);
	if (rc < 0) {
		chip->vbatt_soc_1 = 3410;
	}
	rc = of_property_read_u32(node, "qcom,soc_to_0_withchg", &chip->soc_to_0_withchg);
	if (rc < 0) {
		chip->soc_to_0_withchg = 3200;
	}
	rc = of_property_read_u32(node, "qcom,soc_to_0_withoutchg", &chip->soc_to_0_withoutchg);
	if (rc < 0) {
		chip->soc_to_0_withoutchg = 3300;
	}
	rc = of_property_read_u32(node, "qcom,normal_vterm_hw_inc", &chip->limits.normal_vterm_hw_inc);
	if (rc < 0) {
		chip->limits.normal_vterm_hw_inc = 18;
	}
	rc = of_property_read_u32(node, "qcom,non_normal_vterm_hw_inc", &chip->limits.non_normal_vterm_hw_inc);
	if (rc < 0) {
		chip->limits.non_normal_vterm_hw_inc = 18;
	}
	rc = of_property_read_u32(node, "qcom,vbatt_pdqc_to_5v_thr", &chip->limits.vbatt_pdqc_to_5v_thr);
	if (rc < 0) {
		chip->limits.vbatt_pdqc_to_5v_thr = -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,vbatt_pdqc_to_9v_thr", &chip->limits.vbatt_pdqc_to_9v_thr);
	if (rc < 0) {
		chip->limits.vbatt_pdqc_to_9v_thr = 4000;
	}

	rc = of_property_read_u32(node, "qcom,tbatt_pdqc_to_5v_thr", &chip->limits.tbatt_pdqc_to_5v_thr);
	if (rc < 0) {
		chip->limits.tbatt_pdqc_to_5v_thr = -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,tbatt_pdqc_to_9v_thr", &chip->limits.tbatt_pdqc_to_9v_thr);
	if (rc < 0)
		chip->limits.tbatt_pdqc_to_9v_thr = chip->limits.tbatt_pdqc_to_5v_thr - TEM_ANTI_SHAKE;

	charger_xlog_printk(CHG_LOG_CRTI, "vbatt_power_off = %d, \
			vbatt_soc_1 = %d, \
			normal_vterm_hw_inc = %d, \
			, \
			non_normal_vterm_hw_inc = %d, \
			vbatt_pdqc_to_9v_thr = %d, \
			vbatt_pdqc_to_5v_thr = %d \
			tbatt_pdqc_to_5v_thr = %d \
			tbatt_pdqc_to_9v_thr = %d\n",
			    chip->vbatt_power_off, chip->vbatt_soc_1, chip->limits.normal_vterm_hw_inc,
			    chip->limits.non_normal_vterm_hw_inc, chip->limits.vbatt_pdqc_to_9v_thr,
			    chip->limits.vbatt_pdqc_to_5v_thr, chip->limits.tbatt_pdqc_to_5v_thr,
			    chip->limits.tbatt_pdqc_to_9v_thr);

	rc = of_property_read_u32(node, "qcom,ff1_normal_fastchg_ma", &chip->limits.ff1_normal_fastchg_ma);
	if (rc) {
		chip->limits.ff1_normal_fastchg_ma = 1000;
	}
	rc = of_property_read_u32(node, "qcom,ff1_warm_fastchg_ma", &chip->limits.ff1_warm_fastchg_ma);
	if (rc) {
		chip->limits.ff1_warm_fastchg_ma = chip->limits.ff1_normal_fastchg_ma;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_warm_decidegc", &chip->limits.ffc2_temp_warm_decidegc);
	if (rc) {
		chip->limits.ffc2_temp_warm_decidegc = 350;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_high_decidegc", &chip->limits.ffc2_temp_high_decidegc);
	if (rc) {
		chip->limits.ffc2_temp_high_decidegc = 400;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_low_decidegc", &chip->limits.ffc2_temp_low_decidegc);
	if (rc) {
		chip->limits.ffc2_temp_low_decidegc = 160;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_normal_fastchg_ma", &chip->limits.ffc2_normal_fastchg_ma);
	if (rc < 0) {
		chip->limits.ffc2_normal_fastchg_ma = 700;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_warm_fastchg_ma", &chip->limits.ffc2_warm_fastchg_ma);
	if (rc < 0) {
		chip->limits.ffc2_warm_fastchg_ma = 750;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_exit_step_ma", &chip->limits.ffc2_exit_step_ma);
	if (rc < 0) {
		chip->limits.ffc2_exit_step_ma = 100;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_warm_exit_step_ma", &chip->limits.ffc2_warm_exit_step_ma);
	if (rc < 0) {
		chip->limits.ffc2_warm_exit_step_ma = chip->limits.ffc2_exit_step_ma;
	}
	rc = of_property_read_u32(node, "qcom,sub_ffc2_exit_step_ma", &chip->limits.sub_ffc2_exit_step_ma);
	if (rc < 0) {
		chip->limits.sub_ffc2_exit_step_ma = 100;
	}
	rc = of_property_read_u32(node, "qcom,sub_ffc2_warm_exit_step_ma", &chip->limits.sub_ffc2_warm_exit_step_ma);
	if (rc < 0) {
		chip->limits.sub_ffc2_warm_exit_step_ma = chip->limits.sub_ffc2_exit_step_ma;
	}
	rc = of_property_read_u32(node, "qcom,ff1_exit_step_ma", &chip->limits.ff1_exit_step_ma);
	if (rc < 0) {
		chip->limits.ff1_exit_step_ma = 400;
	}
	rc = of_property_read_u32(node, "qcom,ff1_warm_exit_step_ma", &chip->limits.ff1_warm_exit_step_ma);
	if (rc < 0) {
		chip->limits.ff1_warm_exit_step_ma = 350;
	}
	rc = of_property_read_u32(node, "qcom,sub_ff1_exit_step_ma", &chip->limits.sub_ff1_exit_step_ma);
	if (rc < 0) {
		chip->limits.sub_ff1_exit_step_ma = 400;
	}
	rc = of_property_read_u32(node, "qcom,sub_ff1_warm_exit_step_ma", &chip->limits.sub_ff1_warm_exit_step_ma);
	if (rc < 0) {
		chip->limits.sub_ff1_warm_exit_step_ma = 350;
	}
	rc = of_property_read_u32(node, "qcom,ffc_normal_vfloat_sw_limit", &chip->limits.ffc1_normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc1_normal_vfloat_sw_limit = 4450;
	}
	rc = of_property_read_u32(node, "qcom,ffc_warm_vfloat_sw_limit", &chip->limits.ffc1_warm_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc1_warm_vfloat_sw_limit = chip->limits.ffc1_normal_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_normal_vfloat_sw_limit", &chip->limits.ffc2_normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc2_normal_vfloat_sw_limit = chip->limits.ffc1_normal_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_warm_vfloat_sw_limit", &chip->limits.ffc2_warm_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.ffc2_warm_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc_temp_normal_vfloat_mv", &chip->limits.ffc_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.ffc_temp_normal_vfloat_mv = 4500;
	}
	rc = of_property_read_u32(node, "qcom,ffc1_temp_normal_vfloat_mv", &chip->limits.ffc1_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.ffc1_temp_normal_vfloat_mv = chip->limits.ffc_temp_normal_vfloat_mv;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_temp_normal_vfloat_mv", &chip->limits.ffc2_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.ffc2_temp_normal_vfloat_mv = chip->limits.ffc_temp_normal_vfloat_mv;
	}
	rc = of_property_read_u32(node, "qcom,ffc_normal_vfloat_over_sw_limit",
				  &chip->limits.ffc_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.ffc_normal_vfloat_over_sw_limit = 4500;
	}
	rc = of_property_read_u32(node, "qcom,ffc1_normal_vfloat_over_sw_limit",
				  &chip->limits.ffc1_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.ffc1_normal_vfloat_over_sw_limit = chip->limits.ffc_normal_vfloat_over_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,ffc2_normal_vfloat_over_sw_limit",
				  &chip->limits.ffc2_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.ffc2_normal_vfloat_over_sw_limit = chip->limits.ffc_normal_vfloat_over_sw_limit;
	}

	chip->limits.default_ffc1_normal_vfloat_sw_limit = chip->limits.ffc1_normal_vfloat_sw_limit;
	chip->limits.default_ffc1_warm_vfloat_sw_limit = chip->limits.ffc1_warm_vfloat_sw_limit;
	chip->limits.default_ffc2_normal_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
	chip->limits.default_ffc2_warm_vfloat_sw_limit = chip->limits.ffc2_warm_vfloat_sw_limit;

	charger_xlog_printk(CHG_LOG_CRTI, "ff1_normal_fastchg_ma = %d, \
			ffc2_temp_warm_decidegc = %d, \
			ffc2_temp_high_decidegc = %d, \
			ffc2_normal_fastchg_ma = %d, \
			chip->limits.ffc2_warm_fastchg_ma = %d, \
			ffc2_exit_step_ma = %d, \
			ffc_normal_vfloat_sw_limit = %d, \
			ffc_warm_vfloat_sw_limit = %d, \
			ffc2_normal_vfloat_sw_limit = %d, \
			ffc2_warm_vfloat_sw_limit = %d, \
			ffc1_temp_normal_vfloat_mv = %d, \
			ffc2_temp_normal_vfloat_mv = %d, \
			ffc_normal_vfloat_over_sw_limit = %d \
			ffc2_temp_low_decidegc = %d \
			limits.ff1_exit_step_ma = %d \
			limits.ff1_warm_exit_step_ma = %d \
			pd_input_current_charger_ma = %d \
			qc_input_current_charger_ma = %d\n",
			    chip->limits.ff1_normal_fastchg_ma, chip->limits.ffc2_temp_warm_decidegc,
			    chip->limits.ffc2_temp_high_decidegc, chip->limits.ffc2_normal_fastchg_ma,
			    chip->limits.ffc2_warm_fastchg_ma, chip->limits.ffc2_exit_step_ma,
			    chip->limits.ffc1_normal_vfloat_sw_limit, chip->limits.ffc1_warm_vfloat_sw_limit,
			    chip->limits.ffc2_normal_vfloat_sw_limit, chip->limits.ffc2_warm_vfloat_sw_limit,
			    chip->limits.ffc1_temp_normal_vfloat_mv, chip->limits.ffc2_temp_normal_vfloat_mv,
			    chip->limits.ffc_normal_vfloat_over_sw_limit, chip->limits.ffc2_temp_low_decidegc,
			    chip->limits.ff1_exit_step_ma, chip->limits.ff1_warm_exit_step_ma,
			    chip->limits.pd_input_current_charger_ma, chip->limits.qc_input_current_charger_ma);

	rc = of_property_read_u32(node, "qcom,default_iterm_ma", &chip->limits.default_iterm_ma);
	if (rc < 0) {
		chip->limits.default_iterm_ma = 100;
	}
	rc = of_property_read_u32(node, "qcom,default_sub_iterm_ma", &chip->limits.default_sub_iterm_ma);
	if (rc < 0) {
		chip->limits.default_sub_iterm_ma = 100;
	}
	rc = of_property_read_u32(node, "qcom,default_temp_normal_fastchg_current_ma",
				  &chip->limits.default_temp_normal_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.default_temp_normal_fastchg_current_ma = OPCHG_FAST_CHG_MAX_MA;
	}
	rc = of_property_read_u32(node, "qcom,default_normal_vfloat_sw_limit",
				  &chip->limits.default_normal_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.default_normal_vfloat_sw_limit = 4350;
	}
	rc = of_property_read_u32(node, "qcom,default_temp_normal_vfloat_mv",
				  &chip->limits.default_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.default_temp_normal_vfloat_mv = 4370;
	}
	rc = of_property_read_u32(node, "qcom,default_normal_vfloat_over_sw_limit",
				  &chip->limits.default_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.default_normal_vfloat_over_sw_limit = 4373;
	}

	charger_xlog_printk(CHG_LOG_CRTI, "default_iterm_ma = %d, \
			default_sub_iterm_ma = %d, \
			default_temp_normal_fastchg_current_ma = %d, \
			default_normal_vfloat_sw_limit = %d, \
			default_temp_normal_vfloat_mv = %d, \
			default_normal_vfloat_over_sw_limit = %d\n",
			    chip->limits.default_iterm_ma, chip->limits.default_sub_iterm_ma,
			    chip->limits.default_temp_normal_fastchg_current_ma,
			    chip->limits.default_normal_vfloat_sw_limit, chip->limits.default_temp_normal_vfloat_mv,
			    chip->limits.default_normal_vfloat_over_sw_limit);

	rc = of_property_read_u32(node, "qcom,default_temp_little_cool_fastchg_current_ma",
				  &chip->limits.default_temp_little_cool_fastchg_current_ma);
	if (rc < 0) {
		chip->limits.default_temp_little_cool_fastchg_current_ma =
			chip->limits.temp_little_cool_fastchg_current_ma;
	}
	rc = of_property_read_u32(node, "qcom,default_little_cool_vfloat_sw_limit",
				  &chip->limits.default_little_cool_vfloat_sw_limit);
	if (rc < 0) {
		chip->limits.default_little_cool_vfloat_sw_limit = chip->limits.little_cool_vfloat_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,default_temp_little_cool_vfloat_mv",
				  &chip->limits.default_temp_little_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.default_temp_little_cool_vfloat_mv = chip->limits.temp_little_cool_vfloat_mv;
	}
	rc = of_property_read_u32(node, "qcom,default_little_cool_vfloat_over_sw_limit",
				  &chip->limits.default_little_cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.default_little_cool_vfloat_over_sw_limit = chip->limits.little_cool_vfloat_over_sw_limit;
	}
	rc = of_property_read_u32(node, "qcom,smart_charge_version", &chip->smart_charge_version);
	if (rc < 0) {
		chip->smart_charge_version = 1;
	}
	charger_xlog_printk(CHG_LOG_CRTI, "qcom,smart_charge_version = %d\n", chip->smart_charge_version);
	chip->old_smart_charge_standard = of_property_read_bool(node, "qcom,old_smart_charge_standard");
	charger_xlog_printk(CHG_LOG_CRTI, "qcom,old_smart_charge_standard = %d\n", chip->old_smart_charge_standard);

	rc = of_property_read_string(node, "qcom,bat_type_string", &batt_str);

	memcpy(chip->batt_type_string, batt_str, 4);

	charger_xlog_printk(CHG_LOG_CRTI, "qcom,bat_type_string = %s %s\n", chip->batt_type_string, batt_str);
	charger_xlog_printk(CHG_LOG_CRTI, "default_temp_little_cool_fastchg_current_ma = %d, \
			default_little_cool_vfloat_sw_limit = %d, \
			default_temp_little_cool_vfloat_mv = %d, \
			default_little_cool_vfloat_over_sw_limit = %d\n",
			    chip->limits.default_temp_little_cool_fastchg_current_ma,
			    chip->limits.default_little_cool_vfloat_sw_limit,
			    chip->limits.default_temp_little_cool_vfloat_mv,
			    chip->limits.default_little_cool_vfloat_over_sw_limit);

	chip->limits.default_temp_little_cold_fastchg_current_ma_high =
		chip->limits.temp_little_cold_fastchg_current_ma_high;
	chip->limits.default_temp_little_cold_fastchg_current_ma_low =
		chip->limits.temp_little_cold_fastchg_current_ma_low;
	chip->limits.default_temp_cold_fastchg_current_ma_high = chip->limits.temp_cold_fastchg_current_ma_high;
	chip->limits.default_temp_cold_fastchg_current_ma_low = chip->limits.temp_cold_fastchg_current_ma_low;
	chip->limits.default_temp_cool_fastchg_current_ma_high = chip->limits.temp_cool_fastchg_current_ma_high;
	chip->limits.default_temp_cool_fastchg_current_ma_low = chip->limits.temp_cool_fastchg_current_ma_low;
	chip->limits.default_temp_little_cool_fastchg_current_ma_high =
		chip->limits.temp_little_cool_fastchg_current_ma_high;
	chip->limits.default_temp_little_cool_fastchg_current_ma_low =
		chip->limits.temp_little_cool_fastchg_current_ma_low;
	chip->limits.default_temp_warm_fastchg_current_ma = chip->limits.temp_warm_fastchg_current_ma;
	chip->limits.default_input_current_charger_ma = chip->limits.input_current_charger_ma;
	rc = of_property_read_u32(node, "qcom,batt_capacity_mah", &chip->batt_capacity_mah);
	if (rc < 0) {
		chip->batt_capacity_mah = 2000;
	}

	chip->chg_ctrl_by_vooc = of_property_read_bool(node, "qcom,chg_ctrl_by_vooc");
	chip->chg_ctrl_by_vooc_default = of_property_read_bool(node, "qcom,chg_ctrl_by_vooc");

	rc = of_property_read_u32(node, "qcom,input_current_vooc_ma_normal",
				  &chip->limits.input_current_vooc_ma_normal);
	if (rc) {
		chip->limits.input_current_vooc_ma_normal = 3600;
	}
	rc = of_property_read_u32(node, "qcom,input_current_vooc_led_ma_high",
				  &chip->limits.input_current_vooc_led_ma_high);
	if (rc) {
		chip->limits.input_current_vooc_led_ma_high = 1800;
	}
	rc = of_property_read_u32(node, "qcom,input_current_vooc_led_ma_warm",
				  &chip->limits.input_current_vooc_led_ma_warm);
	if (rc) {
		chip->limits.input_current_vooc_led_ma_warm = 1800;
	}
	rc = of_property_read_u32(node, "qcom,input_current_vooc_led_ma_normal",
				  &chip->limits.input_current_vooc_led_ma_normal);
	if (rc) {
		chip->limits.input_current_vooc_led_ma_normal = 1800;
	}
	rc = of_property_read_u32(node, "qcom,vooc_temp_bat_normal_decidegc", &chip->limits.vooc_normal_bat_decidegc);
	if (rc) {
		chip->limits.vooc_normal_bat_decidegc = 340;
	}
	rc = of_property_read_u32(node, "qcom,input_current_vooc_ma_warm", &chip->limits.input_current_vooc_ma_warm);
	if (rc) {
		chip->limits.input_current_vooc_ma_warm = 3000;
	}
	rc = of_property_read_u32(node, "qcom,vooc_temp_bat_warm_decidegc", &chip->limits.vooc_warm_bat_decidegc);
	if (rc) {
		chip->limits.vooc_warm_bat_decidegc = 380;
	}
	rc = of_property_read_u32(node, "qcom,input_current_vooc_ma_high", &chip->limits.input_current_vooc_ma_high);
	if (rc) {
		chip->limits.input_current_vooc_ma_high = 2600;
	}
	rc = of_property_read_u32(node, "qcom,vooc_temp_bat_hot_decidegc", &chip->limits.vooc_high_bat_decidegc);
	if (rc) {
		chip->limits.vooc_high_bat_decidegc = 450;
	}
	rc = of_property_read_u32(node, "qcom,charger_current_vooc_ma_normal",
				  &chip->limits.charger_current_vooc_ma_normal);
	if (rc) {
		chip->limits.charger_current_vooc_ma_normal = 1000;
	}

	chip->limits.default_input_current_vooc_ma_high = chip->limits.input_current_vooc_ma_high;
	chip->limits.default_input_current_vooc_ma_warm = chip->limits.input_current_vooc_ma_warm;
	chip->limits.default_input_current_vooc_ma_normal = chip->limits.input_current_vooc_ma_normal;
	chip->limits.default_pd_input_current_charger_ma = chip->limits.pd_input_current_charger_ma;
	chip->limits.default_qc_input_current_charger_ma = chip->limits.qc_input_current_charger_ma;
	chip->suspend_after_full = of_property_read_bool(node, "qcom,suspend_after_full");
	chip->check_batt_full_by_sw = of_property_read_bool(node, "qcom,check_batt_full_by_sw");
	chip->external_gauge = of_property_read_bool(node, "qcom,external_gauge");
	chip->external_authenticate = of_property_read_bool(node, "qcom,external_authenticate");
	chip->fg_bcl_poll = of_property_read_bool(node, "qcom,fg_bcl_poll_enable");

	chip->wireless_support = of_property_read_bool(node, "qcom,wireless_support");
	chip->wpc_no_chargerpump = of_property_read_bool(node, "qcom,wpc_no_chargerpump");
	chip->chg_ctrl_by_lcd = of_property_read_bool(node, "qcom,chg_ctrl_by_lcd");
	chip->chg_ctrl_by_lcd_default = of_property_read_bool(node, "qcom,chg_ctrl_by_lcd");
	chip->chg_ctrl_by_camera = of_property_read_bool(node, "qcom,chg_ctrl_by_camera");
	chip->bq25890h_flag = of_property_read_bool(node, "qcom,bq25890_flag");
	chip->chg_ctrl_by_calling = of_property_read_bool(node, "qcom,chg_ctrl_by_calling");
	chip->usbtemp_change_gap = of_property_read_bool(node, "qcom,usbtemp_change_gap");
	chip->ffc_support = of_property_read_bool(node, "qcom,ffc_support");
	chip->dual_ffc = of_property_read_bool(node, "qcom,dual_ffc");
	chip->fg_info_package_read_support = of_property_read_bool(node, "qcom,fg_info_package_read_support");
	chip->new_ui_warning_support = of_property_read_bool(node, "qcom,new_ui_warning_support");
	chip->recharge_after_full = of_property_read_bool(node, "recharge_after_full");
	chip->smooth_switch = of_property_read_bool(node, "qcom,smooth_switch");
	chip->voocphy_support_display_vooc = of_property_read_bool(node, "voocphy_support_display_vooc");
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version() || get_eng_version() == AGING ||
	    get_eng_version() == FACTORY) {
		chg_err(" HIGH_TEMP_AGING/AGING/FACTORY,disable chg timeout \n");
		chip->limits.max_chg_time_sec = -1;
	} else
#endif
	{
		chip->limits.max_chg_time_sec =
			chip->batt_capacity_mah / OPLUS_CHG_CAPACITY_PARAMETER * OPLUS_CHG_ONE_HOUR;
	}
	chip->efttest_fast_switch = of_property_read_bool(node, "qcom,efttest_fast_switch");

	rc = of_property_read_u32(node, "qcom,voocphy_support", &chip->voocphy_support);
	if (rc) {
		chip->voocphy_support = NO_VOOCPHY;
	}

	charger_xlog_printk(CHG_LOG_CRTI, "input_current_charger_ma = %d, \
			input_current_usb_ma = %d, \
			input_current_led_ma = %d, \
			input_current_led_ma_normal = %d, \
			input_current_led_ma_warm = %d, \
			input_current_led_ma_high = %d, \
			temp_normal_fastchg_current_ma = %d, \
			temp_normal_vfloat_mv = %d, \
			iterm_ma = %d, \
			recharge_mv = %d, \
			cold_bat_decidegc = %d, \
			temp_cold_vfloat_mv = %d, \
			temp_cold_fastchg_current_ma = %d, \
			little_cold_bat_decidegc = %d, \
			temp_little_cold_vfloat_mv = %d, \
			temp_little_cold_fastchg_current_ma = %d, \
			cool_bat_decidegc = %d, \
			temp_cool_vfloat_mv = %d, \
			temp_cool_fastchg_current_ma_high = %d, \
			temp_cool_fastchg_current_ma_low = %d, \
			little_cool_bat_decidegc = %d, \
			temp_little_cool_vfloat_mv = %d, \
			temp_little_cool_fastchg_current_ma = %d, \
			normal_bat_decidegc = %d, \
			warm_bat_decidegc = %d, \
			temp_warm_vfloat_mv = %d, \
			temp_warm_fastchg_current_ma = %d, \
			hot_bat_decidegc = %d, \
			non_standard_vfloat_mv = %d, \
			non_standard_fastchg_current_ma = %d, \
			max_chg_time_sec = %d, \
			charger_hv_thr = %d, \
			charger_lv_thr = %d, \
			vbatt_full_thr = %d, \
			vbatt_hv_thr = %d, \
			vfloat_step_mv = %d, \
			vooc_project = %d, \
			suspend_after_full = %d, \
			ext_gauge = %d, \
			sw_vfloat_enable = %d, \
			chip->limits.temp_little_cold_fastchg_current_ma_low = %d, \
			chip->limits.temp_little_cold_fastchg_current_ma_high = %d, \
			chip->limits.temp_cold_fastchg_current_ma_low = %d, \
			chip->limits.temp_cold_fastchg_current_ma_high = %d, \
			chip->limits.charger_current_vooc_ma_normal = %d, \
			chip->ffc_support = %d\
			chip->dual_ffc = %d\
			chip->new_ui_warning_support = %d\
			chip->smooth_switch = %d, \
			chip->voocphy_support = %d\n",
			    chip->limits.input_current_charger_ma, chip->limits.input_current_usb_ma,
			    chip->limits.input_current_led_ma, chip->limits.input_current_led_ma_normal,
			    chip->limits.input_current_led_ma_warm, chip->limits.input_current_led_ma_high,
			    chip->limits.temp_normal_fastchg_current_ma, chip->limits.temp_normal_vfloat_mv,
			    chip->limits.iterm_ma, chip->limits.recharge_mv, chip->limits.cold_bat_decidegc,
			    chip->limits.temp_cold_vfloat_mv, chip->limits.temp_cold_fastchg_current_ma,
			    chip->limits.little_cold_bat_decidegc, chip->limits.temp_little_cold_vfloat_mv,
			    chip->limits.temp_little_cold_fastchg_current_ma, chip->limits.cool_bat_decidegc,
			    chip->limits.temp_cool_vfloat_mv, chip->limits.temp_cool_fastchg_current_ma_high,
			    chip->limits.temp_cool_fastchg_current_ma_low, chip->limits.little_cool_bat_decidegc,
			    chip->limits.temp_little_cool_vfloat_mv, chip->limits.temp_little_cool_fastchg_current_ma,
			    chip->limits.normal_bat_decidegc, chip->limits.warm_bat_decidegc,
			    chip->limits.temp_warm_vfloat_mv, chip->limits.temp_warm_fastchg_current_ma,
			    chip->limits.hot_bat_decidegc, chip->limits.non_standard_vfloat_mv,
			    chip->limits.non_standard_fastchg_current_ma, chip->limits.max_chg_time_sec,
			    chip->limits.charger_hv_thr, chip->limits.charger_lv_thr, chip->limits.vbatt_full_thr,
			    chip->limits.vbatt_hv_thr, chip->limits.vfloat_step_mv, chip->vooc_project,
			    chip->suspend_after_full, chip->external_gauge, chip->limits.sw_vfloat_over_protect_enable,
			    chip->limits.temp_little_cold_fastchg_current_ma_low,
			    chip->limits.temp_little_cold_fastchg_current_ma_high,
			    chip->limits.temp_cold_fastchg_current_ma_low,
			    chip->limits.temp_cold_fastchg_current_ma_high, chip->limits.charger_current_vooc_ma_normal,
			    chip->ffc_support, chip->dual_ffc, chip->new_ui_warning_support, chip->smooth_switch,
			    chip->voocphy_support);

	chip->dual_charger_support = of_property_read_bool(node, "qcom,dual_charger_support");

	rc = of_property_read_u32(node, "qcom,slave_pct", &chip->slave_pct);
	if (rc) {
		chip->slave_pct = 50;
	}

	rc = of_property_read_u32(node, "qcom,slave_chg_enable_ma", &chip->slave_chg_enable_ma);
	if (rc) {
		chip->slave_chg_enable_ma = 2100;
	}

	rc = of_property_read_u32(node, "qcom,slave_chg_disable_ma", &chip->slave_chg_disable_ma);
	if (rc) {
		chip->slave_chg_disable_ma = 1700;
	}

	rc = of_property_read_u32(node, "qcom,internal_gauge_with_asic", &chip->internal_gauge_with_asic);
	if (rc) {
		chip->internal_gauge_with_asic = 0;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_batttemp_gap", &chip->usbtemp_batttemp_gap);
	if (rc) {
		chip->usbtemp_batttemp_gap = 18;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_temp_gap", &chip->usbtemp_cool_down_temp_gap);
	if (rc) {
		chip->usbtemp_cool_down_temp_gap = 12;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recovery_temp_gap",
				  &chip->usbtemp_cool_down_recovery_temp_gap);
	if (rc) {
		chip->usbtemp_cool_down_recovery_temp_gap = 6;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_temp_first_gap",
				  &chip->usbtemp_cool_down_temp_first_gap);
	if (rc) {
		chip->usbtemp_cool_down_temp_first_gap = 12;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recovery_temp_first_gap",
				  &chip->usbtemp_cool_down_recovery_temp_first_gap);
	if (rc) {
		chip->usbtemp_cool_down_recovery_temp_first_gap = 6;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_temp_second_gap",
				  &chip->usbtemp_cool_down_temp_second_gap);
	if (rc) {
		chip->usbtemp_cool_down_temp_second_gap = 17;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_cool_down_recovery_temp_second_gap",
				  &chip->usbtemp_cool_down_recovery_temp_second_gap);
	if (rc) {
		chip->usbtemp_cool_down_recovery_temp_second_gap = 11;
	}

	chip->new_usbtemp_cool_down_support = of_property_read_bool(node, "qcom,new_usbtemp_cool_down_support");

	rc = of_property_read_u32(node, "qcom,usbtemp_batttemp_recover_gap", &chip->usbtemp_batttemp_recover_gap);
	if (rc) {
		chip->usbtemp_batttemp_recover_gap = 6;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_batttemp_current_gap", &chip->usbtemp_batttemp_current_gap);
	if (rc) {
		chip->usbtemp_batttemp_current_gap = 12;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_max_temp_diff", &chip->usbtemp_max_temp_diff);
	if (rc) {
		chip->usbtemp_max_temp_diff = 7;
	}

	rc = of_property_read_u32(node, "qcom,usbtemp_max_temp_thr", &chip->usbtemp_max_temp_thr);
	if (rc) {
		chip->usbtemp_max_temp_thr = 57;
	}
	rc = of_property_read_u32(node, "qcom,usbtemp_temp_up_time_thr", &chip->usbtemp_temp_up_time_thr);
	if (rc) {
		chip->usbtemp_temp_up_time_thr = 30;
	}
	charger_xlog_printk(CHG_LOG_CRTI,
			    "chip->usbtemp_temp_up_time_thr=%d, "
			    "chip->usbtemp_max_temp_thr=%d, "
			    "chip->usbtemp_batttemp_gap[%d] "
			    "chip->usbtemp_cool_down_temp_gap[%d] "
			    "chip->usbtemp_cool_down_temp_first_gap[%d] "
			    "chip->usbtemp_cool_down_temp_second_gap[%d]\n",
			    chip->usbtemp_temp_up_time_thr, chip->usbtemp_max_temp_thr, chip->usbtemp_batttemp_gap,
			    chip->usbtemp_cool_down_temp_gap, chip->usbtemp_cool_down_temp_first_gap,
			    chip->usbtemp_cool_down_temp_second_gap);

	rc = of_property_read_u32(node, "qcom,ship-mode-config", &chip->ship_mode_config);
	if (rc) {
		chip->ship_mode_config = 0;
	}
	charger_xlog_printk(CHG_LOG_CRTI, "qcom,ship-mode-config=%d\n", chip->ship_mode_config);

	chip->vooc_show_ui_soc_decimal = of_property_read_bool(node, "qcom,vooc_show_ui_soc_decimal");

	rc = of_property_read_u32(node, "qcom,ui_soc_decimal_speedmin", &chip->ui_soc_decimal_speedmin);
	if (rc) {
		chip->ui_soc_decimal_speedmin = 2;
	}

	chip->support_abnormal_adapter = of_property_read_bool(node, "qcom,support_abnormal_adapter");
	chip->nightstandby_support = of_property_read_bool(node, "qcom,nightstandby_support");
	charger_xlog_printk(CHG_LOG_CRTI,
			    "dual_charger_support=%d, slave_pct=%d, slave_chg_enable_ma=%d, slave_chg_disable_ma=%d\n",
			    chip->dual_charger_support, chip->slave_pct, chip->slave_chg_enable_ma,
			    chip->slave_chg_disable_ma);

	if (of_property_read_bool(node, "qcom,flash_screen_ctrl_dtsi")) {
		chip->flash_screen_ctrl_status |= FLASH_SCREEN_CTRL_DTSI;
		charger_xlog_printk(CHG_LOG_CRTI, "flash_screen_ctrl_dtsi=%d\n", chip->flash_screen_ctrl_status);
	}
#ifdef OPLUS_CUSTOM_OP_DEF
	chip->hiz_gnd_cable = of_property_read_bool(node, "qcom,supported_hiz_gnd_cable");
#endif

	chip->dual_panel_support = of_property_read_bool(node, "qcom,dual_panel_support");
	charger_xlog_printk(CHG_LOG_CRTI, "dual_panel_support = %d\n", chip->dual_panel_support);

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
	oplus_chg_check_panel_dt(chip);
#endif

	chip->support_low_soc_unlimit = of_property_read_bool(node, "qcom,support_low_soc_unlimit");
	rc = of_property_read_u32(node, "qcom,unlimit_soc", &chip->unlimit_soc);
	if (rc) {
		chip->unlimit_soc = 0;
	}

	chip->nightstandby_support = of_property_read_bool(node, "qcom,nightstandby_support");
	chip->screen_off_control_by_batt_temp = of_property_read_bool(node, "screen_off_control_by_batt_temp");
	charger_xlog_printk(CHG_LOG_CRTI, "support_low_soc_unlimit=%d, unlimit_soc=%d\n", chip->support_low_soc_unlimit,
			    chip->unlimit_soc);

	rc = of_property_count_elems_of_size(node, "usbtemp_curr_table", sizeof(u32));
	if (rc < 0 || rc > (USBTEMP_CURR_TABLE_MAX * 2))
		charger_xlog_printk(CHG_LOG_CRTI, "Count usbtemp_curr_table failed, rc = %d\n", rc);
	else {
		length = rc;
		rc = of_property_read_u32_array(node, "usbtemp_curr_table", (u32 *)temp_curr_table, length);
		if (rc)
			charger_xlog_printk(CHG_LOG_CRTI, "Read usbtemp_curr_table failed, rc = %d\n", rc);
		else {
			for (i = 0; i < length / 2; i++) {
				charger_xlog_printk(CHG_LOG_CRTI, "temp_curr_table[%d, %d]\n",
						    temp_curr_table[i].batt_curr, temp_curr_table[i].temp_delta);
			}
		}
	}

	chip->full_pre_ffc_judge = of_property_read_bool(node, "full_pre_ffc_judge");
	rc = of_property_read_u32(node, "full-pre-ffc-mv", &chip->full_pre_ffc_mv);
	if (rc < 0) {
		chg_err("get full-pre-ffc-mv property error, rc=%d\n", rc);
		chip->full_pre_ffc_mv = 4455;
	}
	chg_err("full-pre-ffc-mv=%d, full_pre_ffc_judge:%d\n", chip->full_pre_ffc_mv, chip->full_pre_ffc_judge);

	chip->support_subboard_ntc = of_property_read_bool(node, "qcom,support_subboard_ntc");
	if (of_property_read_bool(node, "qcom,support_tbatt_shell"))
		chip->support_tbatt_shell = true;
	else
		chip->support_tbatt_shell = false;
	charger_xlog_printk(CHG_LOG_CRTI, "qcom,support_tbatt_shell = %d\n", chip->support_tbatt_shell);

	chip->support_3p6_standard = of_property_read_bool(node, "qcom,support_3p6_standard");
	charger_xlog_printk(CHG_LOG_CRTI, "qcom,support_3p6_standard = %d\n", chip->support_3p6_standard);

	chip->suport_pd_9v2a = of_property_read_bool(node, "qcom,suport_pd_9v2a");
	charger_xlog_printk(CHG_LOG_CRTI, "qcom,suport_pd_9v2a = %d\n", chip->suport_pd_9v2a);

	oplus_chg_smooth_switch_v2_parse_dt(chip);
	rc = of_property_read_u32(node, "oplus,aging_ffc_version", &chip->aging_ffc_version);
	if (rc) {
		chip->aging_ffc_version = AGING_FFC_NOT_SUPPORT;
	}

	chip->support_wd0 = of_property_read_bool(node, "qcom,ccdetect_by_wd0");
	chip->support_usbtemp_protect_v2 = of_property_read_bool(node, "qcom,support_usbtemp_protect_v2");
	charger_xlog_printk(CHG_LOG_CRTI, "qcom,support_usbtemp_protect_v2 = %d\n", chip->support_usbtemp_protect_v2);
	if (chip->support_usbtemp_protect_v2)
		oplus_chg_usbtemp_v2_parse_dt(chip);

	chip->pdqc_9v_voltage_adaptive = false;
	chip->pdqc_9v_voltage_adaptive = of_property_read_bool(node, "oplus,pdqc_9v_voltage_adaptive");
	chg_err("pdqc_9v_voltage_adaptive %s\n", chip->pdqc_9v_voltage_adaptive == true ? "ture" : "false");

	chip->support_check_usbin_status = false;
	chip->support_check_usbin_status = of_property_read_bool(node, "oplus,support_check_usbin_status");
	chg_err("support_check_usbin_status %s\n", chip->support_check_usbin_status == true ? "ture" : "false");


	rc = of_property_read_u32(node, "oplus,tbatt_power_off_cali_temp", &chip->tbatt_power_off_cali_temp);
	if (rc)
		chip->tbatt_power_off_cali_temp = 0;

	chip->boot_reset_adapter = of_property_read_bool(node, "oplus,boot_reset_adapter");
	chip->quick_mode_gain_support = of_property_read_bool(node, "oplus,quick_mode_gain_support");
	return 0;
}
EXPORT_SYMBOL(oplus_chg_parse_charger_dt);

int oplus_chg_get_tbatt_normal_charging_current(struct oplus_chg_chip *chip)
{
	int charging_current = OPLUS_CHG_DEFAULT_CHARGING_CURRENT;

	switch (chip->tbatt_normal_status) {
	case BATTERY_STATUS__NORMAL_PHASE1:
		charging_current = chip->limits.temp_normal_phase1_fastchg_current_ma;
		break;
	case BATTERY_STATUS__NORMAL_PHASE2:
		if (vbatt_higherthan_4180mv) {
			charging_current = chip->limits.temp_normal_phase2_fastchg_current_ma_low;
		} else {
			charging_current = chip->limits.temp_normal_phase2_fastchg_current_ma_high;
		}
		break;
	case BATTERY_STATUS__NORMAL_PHASE3:
		if (vbatt_higherthan_4180mv) {
			charging_current = chip->limits.temp_normal_phase3_fastchg_current_ma_low;
		} else {
			charging_current = chip->limits.temp_normal_phase3_fastchg_current_ma_high;
		}
		break;
	case BATTERY_STATUS__NORMAL_PHASE4:
		if (vbatt_higherthan_4180mv) {
			charging_current = chip->limits.temp_normal_phase4_fastchg_current_ma_low;
		} else {
			charging_current = chip->limits.temp_normal_phase4_fastchg_current_ma_high;
		}
		break;
	case BATTERY_STATUS__NORMAL_PHASE5:
		charging_current = chip->limits.temp_normal_phase5_fastchg_current_ma;
		break;
	case BATTERY_STATUS__NORMAL_PHASE6:
		charging_current = chip->limits.temp_normal_phase6_fastchg_current_ma;
		break;
	default:
		break;
	}
	return charging_current;
}

int oplus_chg_get_tbatt_cold_charging_current(struct oplus_chg_chip *chip)
{
	int charging_current = OPLUS_CHG_DEFAULT_CHARGING_CURRENT;

	switch (chip->tbatt_cold_status) {
	case BATTERY_STATUS__COLD_PHASE1:
		if (vbatt_higherthan_4180mv) {
			charging_current = chip->limits.temp_cold_fastchg_current_ma_low;
		} else {
			charging_current = chip->limits.temp_cold_fastchg_current_ma_high;
		}
		break;
	case BATTERY_STATUS__COLD_PHASE2:
		if (vbatt_higherthan_4180mv) {
			charging_current = chip->limits.temp_freeze_fastchg_current_ma_low;
		} else {
			charging_current = chip->limits.temp_freeze_fastchg_current_ma_high;
		}
		break;
	default:
		break;
	}
	return charging_current;
}

static void oplus_chg_set_charging_current(struct oplus_chg_chip *chip)
{
	int charging_current = OPLUS_CHG_DEFAULT_CHARGING_CURRENT;
#ifndef WPC_NEW_INTERFACE
	if (chip->wireless_support && (oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present())) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return;
	}
#else
	if (oplus_wpc_get_status() != 0) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return;
	}
#endif
	if (oplus_vooc_get_fastchg_started() == true &&
	    !(oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC && chip->chg_ctrl_by_vooc)) {
		chg_err(" svooc fastchg started, ignore.\n");
		return;
	}

	switch (chip->tbatt_status) {
	case BATTERY_STATUS__INVALID:
	case BATTERY_STATUS__REMOVED:
	case BATTERY_STATUS__LOW_TEMP:
	case BATTERY_STATUS__HIGH_TEMP:
		return;
	case BATTERY_STATUS__COLD_TEMP:
		charging_current = oplus_chg_get_tbatt_cold_charging_current(chip);
		charger_xlog_printk(CHG_LOG_CRTI, "vbatt_higherthan_4180mv [%d], charging_current[%d]\n",
				    vbatt_higherthan_4180mv, charging_current);
		break;
	case BATTERY_STATUS__LITTLE_COLD_TEMP:
		if (vbatt_higherthan_4180mv) {
			charging_current = chip->limits.temp_little_cold_fastchg_current_ma_low;
		} else {
			charging_current = chip->limits.temp_little_cold_fastchg_current_ma_high;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "vbatt_higherthan_4180mv [%d], charging_current[%d]\n",
				    vbatt_higherthan_4180mv, charging_current);
		break;
	case BATTERY_STATUS__COOL_TEMP:
		if (vbatt_higherthan_4180mv) {
			charging_current = chip->limits.temp_cool_fastchg_current_ma_low;
		} else {
			charging_current = chip->limits.temp_cool_fastchg_current_ma_high;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "vbatt_higherthan_4180mv [%d], charging_current[%d]\n",
				    vbatt_higherthan_4180mv, charging_current);
		break;
	case BATTERY_STATUS__LITTLE_COOL_TEMP:
		if (chip->dual_charger_support) {
			if (vbatt_higherthan_4180mv) {
				charging_current = chip->limits.temp_little_cool_fastchg_current_ma_low;
			} else {
				charging_current = chip->limits.temp_little_cool_fastchg_current_ma_high;
			}
			charger_xlog_printk(CHG_LOG_CRTI, "vbatt_higherthan_4180mv [%d], charging_current[%d]\n",
					    vbatt_higherthan_4180mv, charging_current);
		} else {
			charging_current = chip->limits.temp_little_cool_fastchg_current_ma;
		}
		break;
	case BATTERY_STATUS__NORMAL:
		if (chip->dual_charger_support) {
			charging_current = oplus_chg_get_tbatt_normal_charging_current(chip);
		} else
			charging_current = chip->limits.temp_normal_fastchg_current_ma;
		break;
	case BATTERY_STATUS__WARM_TEMP:
		charging_current = chip->limits.temp_warm_fastchg_current_ma;
		break;
	default:
		break;
	}
	if (((!chip->authenticate) || (!chip->hmac)) &&
	    (charging_current > chip->limits.non_standard_fastchg_current_ma)) {
		charging_current = chip->limits.non_standard_fastchg_current_ma;
		charger_xlog_printk(CHG_LOG_CRTI, "no high battery, set charging current = %d\n",
				    chip->limits.non_standard_fastchg_current_ma);
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		if (charging_current > chip->limits.short_c_bat_fastchg_current_ma) {
			charging_current = chip->limits.short_c_bat_fastchg_current_ma;
			charger_xlog_printk(CHG_LOG_CRTI, "short circuit battery, set charging current = %d\n",
					    chip->limits.short_c_bat_fastchg_current_ma);
		}
	}
	if ((chip->chg_ctrl_by_lcd) && (chip->led_on) &&
	    (charging_current > chip->limits.temp_warm_fastchg_current_ma_led_on)) {
		if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
			charging_current = chip->limits.temp_warm_fastchg_current_ma_led_on;
		}
		if (chip->dual_charger_support && chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE6) {
			charging_current = chip->limits.temp_warm_fastchg_current_ma_led_on;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]LED ON, charging current: %d\n", charging_current);
	}
	if (charging_current == 0) {
		return;
	}
	if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL) {
		if (chip->batt_target_curr > 0 && chip->batt_target_curr < charging_current)
			charging_current = chip->batt_target_curr;
		if (chip->pre_charging_current == charging_current && chip->batt_target_curr != 0 &&
		    chip->pre_charging_current != 0)
			return;
		chip->pre_charging_current = charging_current;
	}
	charger_xlog_printk(CHG_LOG_CRTI, "set charging current = %d\n", charging_current);
	chip->chg_ops->charging_current_write_fast(charging_current);
}

void oplus_chg_set_input_current_limit(struct oplus_chg_chip *chip)
{
	int current_limit = 0;
	bool is_mcu_fastchg = false;
	is_mcu_fastchg = ((oplus_vooc_get_fastchg_started() &&
			   (chip->vbatt_num != 2 || oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC)) ||
			  (oplus_pps_get_chg_status() == PPS_CHARGERING));

	if (is_mcu_fastchg) {
		chg_err("MCU_READING_iic,return");
		return;
	}

	if (oplus_vooc_get_fastchg_started() == true &&
	    !(oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC && chip->chg_ctrl_by_vooc)) {
		chg_err(" svooc fastchg started, ignore.\n");
		return;
	}

#ifndef WPC_NEW_INTERFACE
	if (chip->wireless_support && (oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present())) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return;
	}
#else
	if (oplus_wpc_get_status() != 0) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return;
	}
#endif
	switch (chip->charger_type) {
	case POWER_SUPPLY_TYPE_UNKNOWN:
		return;
	case POWER_SUPPLY_TYPE_USB:
		current_limit = chip->limits.input_current_usb_ma;
		break;
	case POWER_SUPPLY_TYPE_USB_DCP:
		current_limit = chip->limits.input_current_charger_ma;
		break;
	case POWER_SUPPLY_TYPE_USB_CDP:
		current_limit = chip->limits.input_current_cdp_ma;
		break;
	case POWER_SUPPLY_TYPE_USB_PD_SDP:
		current_limit = chip->limits.input_current_charger_ma;
		break;
	default:
		return;
	}

	if ((chip->chg_ctrl_by_lcd) && (chip->led_on)) {
		if (!chip->dual_charger_support || (chip->dual_charger_support && chip->charger_volt > 7500)) {
			if (chip->led_temp_status == LED_TEMP_STATUS__HIGH) {
				if (current_limit > chip->limits.input_current_led_ma_high) {
					current_limit = chip->limits.input_current_led_ma_high;
				}
			} else if (chip->led_temp_status == LED_TEMP_STATUS__WARM) {
				if (current_limit > chip->limits.input_current_led_ma_warm) {
					current_limit = chip->limits.input_current_led_ma_warm;
				}
			} else {
				if (current_limit > chip->limits.input_current_led_ma_normal) {
					current_limit = chip->limits.input_current_led_ma_normal;
				}
			}
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]LED STATUS CHANGED, IS ON\n");
		}
		if ((chip->chg_ctrl_by_camera) && (chip->camera_on) &&
		    (current_limit > chip->limits.input_current_camera_ma)) {
			current_limit = chip->limits.input_current_camera_ma;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]CAMERA STATUS CHANGED, IS ON\n");
		}
	} else if ((chip->chg_ctrl_by_camera) && (chip->camera_on) &&
		   (current_limit > chip->limits.input_current_camera_ma)) {
		current_limit = chip->limits.input_current_camera_ma;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]CAMERA STATUS CHANGED, IS ON\n");
	}
	if ((chip->chg_ctrl_by_calling) && (chip->calling_on) &&
	    (current_limit > chip->limits.input_current_calling_ma)) {
		current_limit = chip->limits.input_current_calling_ma;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY]calling STATUS CHANGED, IS ON\n");
	}
	if (chip->chg_ctrl_by_vooc && chip->vbatt_num == 2 &&
	    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC &&
	    oplus_vooc_get_fastchg_started() == true) {
		if (chip->led_on) {
			if (chip->vooc_temp_status == VOOC_TEMP_STATUS__HIGH) {
				current_limit = chip->limits.input_current_vooc_led_ma_high;
			} else if (chip->vooc_temp_status == VOOC_TEMP_STATUS__WARM) {
				current_limit = chip->limits.input_current_vooc_led_ma_warm;
			} else if (chip->vooc_temp_status == VOOC_TEMP_STATUS__NORMAL) {
				current_limit = chip->limits.input_current_vooc_led_ma_normal;
			}
		} else if (!(chip->chg_ctrl_by_calling && chip->calling_on)) {
			if (chip->vooc_temp_status == VOOC_TEMP_STATUS__HIGH) {
				current_limit = chip->limits.input_current_vooc_ma_high;
			} else if (chip->vooc_temp_status == VOOC_TEMP_STATUS__WARM) {
				current_limit = chip->limits.input_current_vooc_ma_warm;
			} else if (chip->vooc_temp_status == VOOC_TEMP_STATUS__NORMAL) {
				current_limit = chip->limits.input_current_vooc_ma_normal;
			}
		}
		if (chip->chg_ctrl_by_cool_down && (current_limit > chip->limits.input_current_cool_down_ma) &&
		    (chip->limits.input_current_cool_down_ma != 0)) {
			current_limit = chip->limits.input_current_cool_down_ma;
		}
		chg_err("chg_ctrl_by_vooc,  \
				led_on = %d, \
				calling_on = %d, \
				current_limit[%d], \
				chip->vooc_temp_status[%d]\n",
			chip->led_on, chip->calling_on, current_limit, chip->vooc_temp_status);
		if (chip->chg_ops->input_current_ctrl_by_vooc_write) {
			chip->chg_ops->input_current_ctrl_by_vooc_write(current_limit);
			return;
		}
	}
	if (chip->chg_ctrl_by_cool_down && (current_limit > chip->limits.input_current_cool_down_ma) &&
	    (chip->limits.input_current_cool_down_ma != 0)) {
		current_limit = chip->limits.input_current_cool_down_ma;
	}

	if ((oplus_voocphy_get_btb_temp_over() || oplus_vooc_get_btb_temp_over()) &&
	    oplus_vooc_get_fastchg_to_normal() && current_limit > BTBOVER_TEMP_MAX_INPUT_CURRENT) {
		current_limit = BTBOVER_TEMP_MAX_INPUT_CURRENT;
	}

	if (chg_ctrl_by_sale_mode) {
		if (chip->cool_down == SALE_MODE_COOL_DOWN)
			current_limit = OPLUS_CHG_900_CHARGING_CURRENT;
		else if (chip->cool_down == SALE_MODE_COOL_DOWN_TWO)
			current_limit = OPLUS_CHG_500_CHARGING_CURRENT;
	}
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if ((get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version() || get_eng_version() == AGING) &&
	    chip->charger_type != POWER_SUPPLY_TYPE_USB) {
		chg_err(" HIGH_TEMP_AGING/AGING/FACTORY,set current_limit 2A\n");
		if (chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)
			current_limit = 1500;
		else
			current_limit = 2000;
	}
#endif

#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (get_boot_mode() == MSM_BOOT_MODE__AGING) {
		chg_err(" MSM_BOOT_MODE__AGING,set current_limit 2A\n");
		current_limit = 2000;
	}
#endif
#endif

	if (chip->limits.force_input_current_ma) {
		chg_err("force_input_current_ma %d\n", chip->limits.force_input_current_ma);
		current_limit = chip->limits.force_input_current_ma;
	}

	charger_xlog_printk(CHG_LOG_CRTI, " led_on = %d, \
		current_limit = %d, \
		led_temp_status = %d, \
		cool_down = %d, \
		cool_down_force_5v = %d.\n",
			    chip->led_on, current_limit, chip->led_temp_status, chip->cool_down,
			    chip->cool_down_force_5v);

	chip->chg_ops->input_current_write(current_limit);
}

static int oplus_chg_get_float_voltage(struct oplus_chg_chip *chip)
{
	int flv = chip->limits.temp_normal_vfloat_mv;

	switch (chip->tbatt_status) {
	case BATTERY_STATUS__INVALID:
	case BATTERY_STATUS__REMOVED:
	case BATTERY_STATUS__LOW_TEMP:
	case BATTERY_STATUS__HIGH_TEMP:
		return flv;
	case BATTERY_STATUS__COLD_TEMP:
		flv = chip->limits.temp_cold_vfloat_mv;
		break;
	case BATTERY_STATUS__LITTLE_COLD_TEMP:
		flv = chip->limits.temp_little_cold_vfloat_mv;
		break;
	case BATTERY_STATUS__COOL_TEMP:
		flv = chip->limits.temp_cool_vfloat_mv;
		break;
	case BATTERY_STATUS__LITTLE_COOL_TEMP:
		flv = chip->limits.temp_little_cool_vfloat_mv;
		break;
	case BATTERY_STATUS__NORMAL:
		flv = chip->limits.temp_normal_vfloat_mv;
		break;
	case BATTERY_STATUS__WARM_TEMP:
		flv = chip->limits.temp_warm_vfloat_mv;
		break;
	default:
		break;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip) && flv > chip->limits.short_c_bat_vfloat_mv) {
		flv = chip->limits.short_c_bat_vfloat_mv;
	}
	return flv;
}

static void oplus_chg_set_float_voltage(struct oplus_chg_chip *chip)
{
	int flv = oplus_chg_get_float_voltage(chip);

	if (oplus_vooc_get_fastchg_started() == true &&
	    !(oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC && chip->chg_ctrl_by_vooc)) {
		chg_err(" voocphy_support && svooc fastchg started, ignore.\n");
		return;
	}

	if (((!chip->authenticate) || (!chip->hmac)) && (flv > chip->limits.non_standard_vfloat_mv)) {
		flv = chip->limits.non_standard_vfloat_mv;
		charger_xlog_printk(CHG_LOG_CRTI, "no authenticate or no hmac battery, set float voltage = %d\n",
				    chip->limits.non_standard_vfloat_mv);
	}

	if (oplus_voocphy_get_bidirect_cp_support() == false) {
		if (chip->batt_volt >= (flv - 10) && (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP ||
						      chip->tbatt_status == BATTERY_STATUS__WARM_TEMP)) {
			chg_err("charger stop: CHG_STOP_VOTER__VBAT_OVP!\n");
			oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__VBAT_OVP);
		} else {
			if (chip->stop_voter & CHG_STOP_VOTER__VBAT_OVP) {
				chg_err("charger restart: CHG_STOP_VOTER__VBAT_OVP!\n");
				oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__VBAT_OVP);
			}
			chip->chg_ops->float_voltage_write(flv * chip->vbatt_num);
		}
	} else {
		chip->chg_ops->float_voltage_write(flv);
	}
	chip->limits.vfloat_sw_set = flv;
}

int oplus_chg_get_fv_when_vooc(struct oplus_chg_chip *chip)
{
	int flv;

	if (!chip)
		return 0;

	if (chip->chg_ctrl_by_vooc)
		flv = chip->limits.vfloat_sw_set;
	else
		flv = 0;

	return flv;
}

static void oplus_chg_set_iterm(struct oplus_chg_chip *chip)
{
	int iterm = chip->limits.iterm_ma;

	if (oplus_voocphy_get_bidirect_cp_support()) {
		iterm = 2 * iterm;
	}
	chip->chg_ops->term_current_set(chip->limits.iterm_ma);
}

#define VFLOAT_OVER_NUM 2
static void oplus_chg_vfloat_over_check(struct oplus_chg_chip *chip)
{
	if (!chip->mmi_chg) {
		return;
	}
	if (chip->charging_state == CHARGING_STATUS_FULL) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == false) {
		return;
	}
	if (chip->check_batt_full_by_sw && (chip->limits.sw_vfloat_over_protect_enable == false)) {
		return;
	}
	if (oplus_vooc_get_fastchg_ing() == true) {
		return;
	}
	if (oplus_pps_get_chg_status() == PPS_CHARGERING) {
		return;
	}
	if (oplus_ufcs_get_chg_status() == UFCS_CHARGERING) {
		return;
	}

	if (chip->wireless_support && (oplus_wpc_get_ffc_charging() == true || oplus_chg_is_wls_ffc())) {
		return;
	}

	if (chip->limits.sw_vfloat_over_protect_enable) {
		if ((chip->batt_volt >= chip->limits.cold_vfloat_over_sw_limit &&
		     chip->tbatt_status == BATTERY_STATUS__COLD_TEMP &&
		     (chip->stop_voter & CHG_STOP_VOTER__VBAT_OVP) == 0) ||
		    (chip->batt_volt >= chip->limits.little_cold_vfloat_over_sw_limit &&
		     chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) ||
		    (chip->batt_volt >= chip->limits.cool_vfloat_over_sw_limit &&
		     chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) ||
		    (chip->batt_volt >= chip->limits.little_cool_vfloat_over_sw_limit &&
		     chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) ||
		    (chip->batt_volt >= chip->limits.normal_vfloat_over_sw_limit &&
		     chip->tbatt_status == BATTERY_STATUS__NORMAL) ||
		    (chip->batt_volt >= chip->limits.warm_vfloat_over_sw_limit &&
		     chip->tbatt_status == BATTERY_STATUS__WARM_TEMP &&
		     (chip->stop_voter & CHG_STOP_VOTER__VBAT_OVP) == 0) ||
		    (((!chip->authenticate) || (!chip->hmac)) &&
		     (chip->batt_volt >= chip->limits.non_standard_vfloat_over_sw_limit))) {
			chip->limits.vfloat_over_counts++;
			if (chip->limits.vfloat_over_counts > VFLOAT_OVER_NUM) {
				chip->limits.vfloat_over_counts = 0;
				chip->limits.vfloat_sw_set -= chip->limits.vfloat_step_mv;
				if (oplus_voocphy_get_bidirect_cp_support() == false) {
					chip->chg_ops->float_voltage_write(chip->limits.vfloat_sw_set *
									   chip->vbatt_num);
				} else {
					chip->chg_ops->float_voltage_write(chip->limits.vfloat_sw_set);
				}
				charger_xlog_printk(CHG_LOG_CRTI, "bat_volt:%d, \
					tbatt:%d, \
					sw_vfloat_set:%d\n",
						    chip->batt_volt, chip->tbatt_status, chip->limits.vfloat_sw_set);
			}
		} else {
			chip->limits.vfloat_over_counts = 0;
		}
		return;
	}
}

static void oplus_chg_check_vbatt_higher_than_4180mv(struct oplus_chg_chip *chip)
{
	static bool vol_high_pre = false;
	static int lower_count = 0, higher_count = 0;
	static int tbatt_status_pre = BATTERY_STATUS__NORMAL;

	if (!chip->mmi_chg) {
		vbatt_higherthan_4180mv = false;
		vol_high_pre = false;
		lower_count = 0;
		higher_count = 0;
		return;
	}
	if (oplus_vooc_get_fastchg_started() == true) {
		return;
	}
	if (tbatt_status_pre != chip->tbatt_status) {
		tbatt_status_pre = chip->tbatt_status;
		vbatt_higherthan_4180mv = false;
		vol_high_pre = false;
		lower_count = 0;
		higher_count = 0;
	}
	if (chip->batt_volt > 4180) {
		higher_count++;
		if (higher_count > 2) {
			lower_count = 0;
			higher_count = 3;
			vbatt_higherthan_4180mv = true;
		}
	} else if (vbatt_higherthan_4180mv) {
		if ((chip->batt_volt < 4000) && (chip->temperature > chip->limits.freeze_bat_decidegc)) {
			lower_count++;
			if (lower_count > 2) {
				lower_count = 3;
				higher_count = 0;
				vbatt_higherthan_4180mv = false;
			}
		}
	}
	if (vol_high_pre != vbatt_higherthan_4180mv) {
		vol_high_pre = vbatt_higherthan_4180mv;
		oplus_chg_set_charging_current(chip);
	}
}

#define TEMP_FFC_COUNTS 2
static void oplus_chg_check_ffc_temp_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_FFC_TEMP_STATUS temp_status = chip->ffc_temp_status;
	static int high_counts = 0, warm_counts = 0, normal_counts = 0;
	static int low_counts = 0;
	if (oplus_switching_support_parallel_chg()) {
		if (chip->temperature >= chip->limits.ffc2_temp_high_decidegc ||
		    chip->sub_batt_temperature >= chip->limits.ffc2_temp_high_decidegc) { /*>=40C*/
			high_counts++;
			if (high_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__HIGH;
			}
		} else if (chip->temperature >= chip->limits.ffc2_temp_warm_decidegc &&
			   chip->sub_batt_temperature >= chip->limits.ffc2_temp_warm_decidegc) { /*>=35C && < 40*/
			warm_counts++;
			if (warm_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__WARM;
			}
		} else if (chip->temperature >= chip->limits.ffc2_temp_low_decidegc &&
			   chip->sub_batt_temperature >= chip->limits.ffc2_temp_low_decidegc) { /*>=16C&&<35C*/
			normal_counts++;
			if (normal_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__NORMAL;
			}
		} else { /*<16C*/
			low_counts++;
			if (low_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__LOW;
			}
		}
	} else {
		if (batt_temp >= chip->limits.ffc2_temp_high_decidegc) { /*>=40C*/
			high_counts++;
			if (high_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__HIGH;
			}
		} else if (batt_temp >= chip->limits.ffc2_temp_warm_decidegc) { /*>=35C && < 40*/
			warm_counts++;
			if (warm_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__WARM;
			}
		} else if (batt_temp >= chip->limits.ffc2_temp_low_decidegc) { /*>=16C&&<35C*/
			normal_counts++;
			if (normal_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__NORMAL;
			}
		} else { /*<16C*/
			low_counts++;
			if (low_counts >= TEMP_FFC_COUNTS) {
				low_counts = 0;
				high_counts = 0;
				warm_counts = 0;
				normal_counts = 0;
				temp_status = FFC_TEMP_STATUS__LOW;
			}
		}
	}
	if (temp_status != chip->ffc_temp_status) {
		chip->ffc_temp_status = temp_status;
	}
}

void oplus_chg_turn_on_ffc1(struct oplus_chg_chip *chip)
{
	if ((!chip->authenticate) || (!chip->hmac)) {
		return;
	}
	if (!chip->mmi_chg) {
		return;
	}
	if (chip->balancing_bat_stop_chg) {
		chg_err("balancing_bat,stop charge");
		return;
	}
	if (oplus_vooc_get_allow_reading() == false) {
		return;
	}
	chip->chg_ops->hardware_init();
	if (chip->stop_chg == 0 &&
	    (chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
		oplus_chg_suspend_charger();
	}
	if (chip->check_batt_full_by_sw) {
		chip->chg_ops->set_charging_term_disable();
	}

	if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL)
		oplus_chg_track_record_ffc_start_info();

	chg_err("--------\r\n");
	chip->chg_ctrl_by_lcd = false;
	chip->fastchg_to_ffc = true;
	chip->fastchg_ffc_status = 1;
	chip->chg_ctrl_by_vooc = false;
	chip->recharge_after_ffc = true;

	oplus_chg_aging_ffc_action(chip, true);

	if (chip->temperature >= chip->limits.ffc2_temp_warm_decidegc) {
		chip->limits.temp_normal_fastchg_current_ma = chip->limits.ff1_warm_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.ff1_warm_fastchg_ma;
	} else {
		chip->limits.temp_normal_fastchg_current_ma = chip->limits.ff1_normal_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.ff1_normal_fastchg_ma;
	}
	chip->limits.normal_vfloat_sw_limit = chip->limits.ffc1_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv = chip->limits.ffc1_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit = chip->limits.ffc1_normal_vfloat_over_sw_limit;
	chip->limits.little_cool_vfloat_sw_limit = chip->limits.ffc1_normal_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv = chip->limits.ffc1_temp_normal_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit = chip->limits.ffc1_normal_vfloat_over_sw_limit;
	oplus_chg_check_tbatt_status(chip);
	oplus_chg_set_float_voltage(chip);
	oplus_chg_set_charging_current(chip);
	oplus_chg_set_input_current_limit(chip);
	oplus_chg_set_iterm(chip);
}

void oplus_chg_turn_on_ffc2(struct oplus_chg_chip *chip)
{
	if ((!chip->authenticate) || (!chip->hmac)) {
		return;
	}
	if (!chip->mmi_chg) {
		return;
	}
	if (chip->balancing_bat_stop_chg) {
		chg_err("balancing_bat,stop charge");
		return;
	}
	if (oplus_vooc_get_allow_reading() == false) {
		return;
	}
	chip->chg_ops->hardware_init();

	if (chip->check_batt_full_by_sw) {
		chip->chg_ops->set_charging_term_disable();
	}

	chg_err("--------\r\n");
	chip->chg_ctrl_by_lcd = false;
	chip->fastchg_to_ffc = true;
	chip->fastchg_ffc_status = 2;
	chip->chg_ctrl_by_vooc = false;
	chip->recharge_after_ffc = true;

	oplus_chg_aging_ffc_action(chip, false);

	if (chip->temperature >= chip->limits.ffc2_temp_warm_decidegc) {
		chip->limits.temp_normal_fastchg_current_ma = chip->limits.ffc2_warm_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.ffc2_warm_fastchg_ma;
	} else {
		chip->limits.temp_normal_fastchg_current_ma = chip->limits.ffc2_normal_fastchg_ma;
		chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.ffc2_normal_fastchg_ma;
	}

	chip->limits.normal_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv = chip->limits.ffc2_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit = chip->limits.ffc2_normal_vfloat_over_sw_limit;

	chip->limits.little_cool_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv = chip->limits.ffc2_temp_normal_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit = chip->limits.ffc2_normal_vfloat_over_sw_limit;

	oplus_chg_check_tbatt_status(chip);
	oplus_chg_set_float_voltage(chip);
	oplus_chg_set_charging_current(chip);
	oplus_chg_set_input_current_limit(chip);
	oplus_chg_set_iterm(chip);
}

void oplus_chg_turn_on_charging(struct oplus_chg_chip *chip)
{
	if (!chip->authenticate) {
		return;
	}

	if (!chip->mmi_chg) {
		return;
	}

	if (chip->balancing_bat_stop_chg) {
		chg_err("balancing_bat,stop charge");
		return;
	}
	if (oplus_voocphy_get_bidirect_cp_support() && oplus_voocphy_int_disable_chg()) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == false) {
		return;
	}
	chg_err("-----------\r\n");
	chip->chg_ops->hardware_init();
	if (chip->stop_chg == 0 &&
	    (chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
		oplus_chg_suspend_charger();
	}
	if (chip->check_batt_full_by_sw) {
		chip->chg_ops->set_charging_term_disable();
	}

	oplus_chg_check_tbatt_status(chip);
	oplus_chg_set_float_voltage(chip);
	oplus_chg_set_charging_current(chip);
	oplus_chg_set_input_current_limit(chip);
	oplus_chg_set_iterm(chip);
}

void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip)
{
	if (oplus_vooc_get_allow_reading() == false) {
		return;
	}

	switch (chip->tbatt_status) {
	case BATTERY_STATUS__INVALID:
	case BATTERY_STATUS__REMOVED:
	case BATTERY_STATUS__LOW_TEMP:
		break;
	case BATTERY_STATUS__HIGH_TEMP:
		break;
	case BATTERY_STATUS__COLD_TEMP:
		chip->chg_ops->charging_current_write_fast(chip->limits.temp_cold_fastchg_current_ma);
		msleep(50);
		break;
	case BATTERY_STATUS__LITTLE_COLD_TEMP:
	case BATTERY_STATUS__COOL_TEMP:
		chip->chg_ops->charging_current_write_fast(chip->limits.temp_cold_fastchg_current_ma);
		msleep(50);
		break;
	case BATTERY_STATUS__LITTLE_COOL_TEMP:
	case BATTERY_STATUS__NORMAL:
		chip->chg_ops->charging_current_write_fast(chip->limits.temp_cool_fastchg_current_ma_high);
		msleep(50);
		chip->chg_ops->charging_current_write_fast(chip->limits.temp_cold_fastchg_current_ma);
		msleep(50);
		break;
	case BATTERY_STATUS__WARM_TEMP:
		chip->chg_ops->charging_current_write_fast(chip->limits.temp_cold_fastchg_current_ma);
		msleep(50);
		break;
	default:
		break;
	}
	chip->chg_ops->charging_disable();
	chip->pre_charging_current = 0;
	/*charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] oplus_chg_turn_off_charging !!\n");*/
}
/*
static int oplus_chg_check_suspend_or_disable(struct oplus_chg_chip *chip)
{
	if(chip->suspend_after_full) {
		if(chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__COOL_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__NORMAL) {
			return CHG_SUSPEND;
		} else if(chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
			return CHG_DISABLE;
		} else {
			return CHG_NO_SUSPEND_NO_DISABLE;
		}
	} else {
		if(chip->tbatt_status == BATTERY_STATUS__COLD_TEMP
			|| chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
			return CHG_DISABLE;
		} else {
			return CHG_NO_SUSPEND_NO_DISABLE;
		}
	}
}
*/

static int oplus_chg_check_suspend_or_disable(struct oplus_chg_chip *chip)
{
	if (chip->suspend_after_full) {
		if ((chip->tbatt_status == BATTERY_STATUS__HIGH_TEMP ||
		     chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) &&
		    (chip->batt_volt < 4250)) {
			return CHG_DISABLE;
		} else {
			return CHG_SUSPEND;
		}
	} else {
		return CHG_DISABLE;
	}
}

static void oplus_chg_voter_charging_start(struct oplus_chg_chip *chip, OPLUS_CHG_STOP_VOTER voter)
{
	if (!chip->authenticate) {
		return;
	}
	if (!chip->mmi_chg) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == false) {
		return;
	}
	if (chip->balancing_bat_stop_chg) {
		chg_err("balancing_bat,stop charge");
		return;
	}
	chip->chging_on = true;
	chip->stop_voter &= ~(int)voter;
	if (oplus_voocphy_get_bidirect_cp_support()) {
		oplus_voocphy_set_chg_auto_mode(false);
	}
	oplus_chg_turn_on_charging(chip);

	switch (voter) {
	case CHG_STOP_VOTER__FULL:
		chip->charging_state = CHARGING_STATUS_CCCV;
		if (oplus_vooc_get_allow_reading() == true) {
			if (oplus_switching_support_parallel_chg()) {
				oplus_switching_enable_charge(1);
			}
			chip->chg_ops->charger_unsuspend();
			chip->chg_ops->charging_enable();
		}
		break;
	case CHG_STOP_VOTER__VCHG_ABNORMAL:
		chip->charging_state = CHARGING_STATUS_CCCV;
		if (oplus_vooc_get_allow_reading() == true) {
			chip->chg_ops->charger_unsuspend();
		}
		break;
	case CHG_STOP_VOTER_BAD_VOL_DIFF:
	case CHG_STOP_VOTER__BATTTEMP_ABNORMAL:
	case CHG_STOP_VOTER__VBAT_TOO_HIGH:
	case CHG_STOP_VOTER__MAX_CHGING_TIME:
	case CHG_STOP_VOTER__VBAT_OVP:
		chip->charging_state = CHARGING_STATUS_CCCV;
		break;
	default:
		break;
	}
}

void oplus_chg_voter_charging_stop(struct oplus_chg_chip *chip, OPLUS_CHG_STOP_VOTER voter)
{
	chip->chging_on = false;
	chip->stop_voter |= (int)voter;

	switch (voter) {
	case CHG_STOP_VOTER__FULL:
		chip->charging_state = CHARGING_STATUS_FULL;
		if (oplus_vooc_get_allow_reading() == true) {
			if (oplus_chg_check_suspend_or_disable(chip) == CHG_SUSPEND) {
				oplus_chg_suspend_charger();
			} else {
				oplus_chg_turn_off_charging(chip);
			}
		}
		break;
	case CHG_STOP_VOTER__VCHG_ABNORMAL:
		chip->charging_state = CHARGING_STATUS_FAIL;
		chip->total_time = 0;
		if (oplus_vooc_get_allow_reading() == true) {
			chip->chg_ops->input_current_write(500);
			msleep(50);
			oplus_chg_suspend_charger();
		}
		oplus_chg_turn_off_charging(chip);
		break;
	case CHG_STOP_VOTER_BAD_VOL_DIFF:
		chip->charging_state = CHARGING_STATUS_FAIL;
		chip->total_time = 0;
		oplus_chg_turn_off_charging(chip);
		if (oplus_vooc_get_fastchg_started() == true) {
			chip->chg_ops->charger_suspend();
		}
		break;
	case CHG_STOP_VOTER__BATTTEMP_ABNORMAL:
	case CHG_STOP_VOTER__VBAT_TOO_HIGH:
		chip->charging_state = CHARGING_STATUS_FAIL;
		chip->total_time = 0;
		oplus_chg_turn_off_charging(chip);
		break;
	case CHG_STOP_VOTER__MAX_CHGING_TIME:
		chip->charging_state = CHARGING_STATUS_FAIL;
		oplus_chg_turn_off_charging(chip);
		break;
	case CHG_STOP_VOTER__VBAT_OVP:
		oplus_chg_turn_off_charging(chip);
		break;
	default:
		break;
	}

	if (oplus_voocphy_get_bidirect_cp_support() && chip->chg_ops->check_chrdet_status()) {
		oplus_voocphy_set_chg_auto_mode(true);
	}
}

#define HYSTERISIS_DECIDEGC 20
#define HYSTERISIS_DECIDEGC_0C 5
#define TBATT_PRE_SHAKE_INVALID 999
static void battery_temp_anti_shake_handle(struct oplus_chg_chip *chip)
{
	int tbatt_cur_shake = chip->temperature, low_shake = 0, high_shake = 0;
	int low_shake_0c = 0, high_shake_0c = 0;

	if (tbatt_cur_shake > chip->tbatt_pre_shake) { /*get warmer*/
		low_shake = -HYSTERISIS_DECIDEGC;
		high_shake = 0;
		low_shake_0c = -HYSTERISIS_DECIDEGC_0C;
		high_shake_0c = 0;
	} else if (tbatt_cur_shake < chip->tbatt_pre_shake) { /*get cooler*/
		low_shake = 0;
		high_shake = HYSTERISIS_DECIDEGC;
		low_shake_0c = 0;
		high_shake_0c = HYSTERISIS_DECIDEGC_0C;
	}
	if (chip->tbatt_status == BATTERY_STATUS__HIGH_TEMP) { /*>53C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound; /*-3C*/
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound; /*0C*/
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound; /*5C*/
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound; /*12C*/
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound; /*16C*/
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound; /*45C*/
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound + low_shake; /*53C*/
	} else if (chip->tbatt_status == BATTERY_STATUS__LOW_TEMP) { /*<-3C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound + high_shake;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) { /*-3C~0C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound + high_shake_0c;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) { /*0C-5C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound + low_shake_0c;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound + high_shake;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) { /*5C~12C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound + low_shake;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound + high_shake;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) { /*12C~16C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound + low_shake;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound + high_shake;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) { /*16C~45C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound + low_shake;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound + high_shake;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) { /*45C~53C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound + low_shake;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound + high_shake;
	} else { /*BATTERY_STATUS__REMOVED								<-19C*/
		chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
		chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
		chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
		chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
		chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
		chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
		chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	}

	chg_err("[%d-%d-%d-%d-%d-%d-%d] t=[%d %d] s=%d\n", chip->limits.cold_bat_decidegc,
		chip->limits.little_cold_bat_decidegc, chip->limits.cool_bat_decidegc,
		chip->limits.little_cool_bat_decidegc, chip->limits.normal_bat_decidegc, chip->limits.warm_bat_decidegc,
		chip->limits.hot_bat_decidegc, chip->tbatt_pre_shake, tbatt_cur_shake, chip->tbatt_status);
	chip->tbatt_pre_shake = tbatt_cur_shake;
}

#define TEMP_CNT 3
static bool oplus_chg_check_tbatt_is_good(struct oplus_chg_chip *chip)
{
	static bool ret = true;
	static int temp_counts = 0;
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBATT_STATUS tbatt_status = chip->tbatt_status;

	if (batt_temp > chip->limits.hot_bat_decidegc || batt_temp < chip->limits.cold_bat_decidegc) {
		temp_counts++;
		if (temp_counts >= TEMP_CNT) {
			temp_counts = 0;
			ret = false;
			if (batt_temp <= chip->limits.removed_bat_decidegc) {
				tbatt_status = BATTERY_STATUS__REMOVED;
			} else if (batt_temp > chip->limits.hot_bat_decidegc) {
				tbatt_status = BATTERY_STATUS__HIGH_TEMP;
			} else {
				tbatt_status = BATTERY_STATUS__LOW_TEMP;
			}
		}
	} else {
		temp_counts = 0;
		ret = true;
		if (batt_temp >= chip->limits.warm_bat_decidegc) { /*45C*/
			tbatt_status = BATTERY_STATUS__WARM_TEMP;
		} else if (batt_temp >= chip->limits.normal_bat_decidegc) { /*16C*/
			tbatt_status = BATTERY_STATUS__NORMAL;
		} else if (batt_temp >= chip->limits.little_cool_bat_decidegc) { /*12C*/
			tbatt_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
		} else if (batt_temp >= chip->limits.cool_bat_decidegc) { /*5C*/
			tbatt_status = BATTERY_STATUS__COOL_TEMP;
		} else if (batt_temp >= chip->limits.little_cold_bat_decidegc) { /*0C*/
			tbatt_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
		} else if (batt_temp >= chip->limits.cold_bat_decidegc) { /*-3C*/
			tbatt_status = BATTERY_STATUS__COLD_TEMP;
		} else {
			tbatt_status = BATTERY_STATUS__COLD_TEMP;
		}
	}
	if (tbatt_status == BATTERY_STATUS__REMOVED) {
		chip->batt_exist = false;
	} else {
		chip->batt_exist = true;
	}
	if (chip->tbatt_pre_shake == TBATT_PRE_SHAKE_INVALID) {
		chip->tbatt_pre_shake = batt_temp;
	}
	if (tbatt_status != chip->tbatt_status) {
		if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP ||
		    chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
			if (chip->soc != 100 && chip->batt_full == true &&
			    chip->charging_state == CHARGING_STATUS_FULL) {
				chip->batt_full = false;
				chip->real_batt_full = false;
				chip->tbatt_when_full = 200;
				oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__FULL);
			}
			if (oplus_switching_support_parallel_chg()) {
				chip->hw_sub_batt_full_by_sw = false;
				chip->sw_sub_batt_full = false;
			}
		}

		if ((chip->tbatt_status == BATTERY_STATUS__WARM_TEMP && tbatt_status == BATTERY_STATUS__NORMAL) ||
		    (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP &&
		     tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP)) {
			if (reset_mcu_delay > RESET_MCU_DELAY_30S && chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP &&
			    chip->chg_ops->get_charger_subtype &&
			    chip->chg_ops->get_charger_subtype() != CHARGER_SUBTYPE_QC) {
				reset_mcu_delay = 0;
				chg_err("warm to normal or cold to little cold, reset mcu delay %d to 0\n",
					reset_mcu_delay);
			}
		}
		chip->tbatt_status = tbatt_status;
		vbatt_higherthan_4180mv = false;
		if ((!(oplus_chg_get_voocphy_support() != NO_VOOCPHY) && oplus_vooc_get_allow_reading() == true) ||
		    ((oplus_chg_get_voocphy_support() != NO_VOOCPHY) && oplus_vooc_get_fastchg_started() == false)) {
			oplus_chg_set_float_voltage(chip);
			oplus_chg_set_charging_current(chip);
		}
		battery_temp_anti_shake_handle(chip);
	}
	return ret;
}

static void oplus_chg_check_tbatt_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBATT_STATUS tbatt_status = chip->tbatt_status;

	if (batt_temp > chip->limits.hot_bat_decidegc) { /*53C*/
		tbatt_status = BATTERY_STATUS__HIGH_TEMP;
	} else if (batt_temp >= chip->limits.warm_bat_decidegc) { /*45C*/
		tbatt_status = BATTERY_STATUS__WARM_TEMP;
	} else if (batt_temp >= chip->limits.normal_bat_decidegc) { /*16C*/
		tbatt_status = BATTERY_STATUS__NORMAL;
	} else if (batt_temp >= chip->limits.little_cool_bat_decidegc) { /*12C*/
		tbatt_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
	} else if (batt_temp >= chip->limits.cool_bat_decidegc) { /*5C*/
		tbatt_status = BATTERY_STATUS__COOL_TEMP;
	} else if (batt_temp >= chip->limits.little_cold_bat_decidegc) { /*0C*/
		tbatt_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
	} else if (batt_temp >= chip->limits.cold_bat_decidegc) { /*-3C*/
		tbatt_status = BATTERY_STATUS__COLD_TEMP;
	} else if (batt_temp > chip->limits.removed_bat_decidegc) { /*-20C*/
		tbatt_status = BATTERY_STATUS__LOW_TEMP;
	} else {
		tbatt_status = BATTERY_STATUS__REMOVED;
	}
	if (tbatt_status == BATTERY_STATUS__REMOVED) {
		chip->batt_exist = false;
	} else {
		chip->batt_exist = true;
	}

	if ((chip->tbatt_status == BATTERY_STATUS__WARM_TEMP && tbatt_status == BATTERY_STATUS__NORMAL) ||
	    (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP && tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP)) {
		if (reset_mcu_delay > RESET_MCU_DELAY_30S && chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP &&
		    chip->chg_ops->get_charger_subtype && chip->chg_ops->get_charger_subtype() != CHARGER_SUBTYPE_QC) {
			reset_mcu_delay = 0;
			chg_err("warm to normal or cold to little cold, reset mcu delay %d to 0\n", reset_mcu_delay);
		}
	}
	chip->tbatt_status = tbatt_status;
}

static void battery_temp_normal_anti_shake_handle(struct oplus_chg_chip *chip)
{
	int tbatt_normal_cur_shake = chip->temperature, low_shake = 0, high_shake = 0;

	if (tbatt_normal_cur_shake > chip->tbatt_normal_pre_shake) { /*get warmer*/
		low_shake = -HYSTERISIS_DECIDEGC;
		high_shake = 0;
	} else if (tbatt_normal_cur_shake < chip->tbatt_normal_pre_shake) { /*get cooler*/
		low_shake = 0;
		high_shake = HYSTERISIS_DECIDEGC;
	}

	if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE1) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound + high_shake;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE2) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound + low_shake;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound + high_shake;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE3) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound + low_shake;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound + high_shake;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE4) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound + low_shake;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound + high_shake;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE5) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound + low_shake;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound + high_shake;
	} else if (chip->tbatt_normal_status == BATTERY_STATUS__NORMAL_PHASE6) {
		chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
		chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
		chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
		chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
		chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
		chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound + low_shake;
	} else {
		/*do nothing*/
	}

	chip->tbatt_normal_pre_shake = tbatt_normal_cur_shake;
}

static void oplus_chg_check_tbatt_normal_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBATT_NORMAL_STATUS tbatt_normal_status = chip->tbatt_normal_status;

	if (!chip->dual_charger_support) {
		return;
	}
	if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		if (batt_temp >= chip->limits.normal_phase6_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE6;
		} else if (batt_temp >= chip->limits.normal_phase5_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE5;
		} else if (batt_temp >= chip->limits.normal_phase4_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE4;
		} else if (batt_temp >= chip->limits.normal_phase3_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE3;
		} else if (batt_temp >= chip->limits.normal_phase2_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE2;
		} else if (batt_temp >= chip->limits.normal_phase1_bat_decidegc) {
			tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE1;
		} else {
			/*do nothing*/
		}
	}

	if (chip->tbatt_normal_pre_shake == TBATT_PRE_SHAKE_INVALID) {
		chip->tbatt_pre_shake = batt_temp;
		chip->tbatt_normal_pre_shake = batt_temp;
	}

	if (chip->tbatt_normal_status != tbatt_normal_status) {
		chip->tbatt_normal_status = tbatt_normal_status;
		if (oplus_vooc_get_allow_reading() == true) {
			oplus_chg_set_float_voltage(chip);
			oplus_chg_set_charging_current(chip);
		}
		battery_temp_normal_anti_shake_handle(chip);
		chg_debug("tbatt_normal_status status change, [%d %d %d %d %d %d %d]\n", chip->tbatt_normal_status,
			  chip->limits.normal_phase1_bat_decidegc, chip->limits.normal_phase2_bat_decidegc,
			  chip->limits.normal_phase3_bat_decidegc, chip->limits.normal_phase4_bat_decidegc,
			  chip->limits.normal_phase5_bat_decidegc, chip->limits.normal_phase6_bat_decidegc);
	}
}

static void oplus_chg_check_tbatt_cold_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBATT_COLD_STATUS tbatt_cold_status = BATTERY_STATUS__COLD_PHASE1;
	int shake = 0;

	if (chip->anti_shake_bound.freeze_bound == chip->anti_shake_bound.cold_bound ||
	    chip->tbatt_status != BATTERY_STATUS__COLD_TEMP) {
		chip->tbatt_cold_status = BATTERY_STATUS__COLD_PHASE1;
		chip->tbatt_cold_pre_shake = TBATT_PRE_SHAKE_INVALID;
		chip->limits.freeze_bat_decidegc = chip->anti_shake_bound.freeze_bound;
		return;
	}

	if (batt_temp >= chip->limits.freeze_bat_decidegc) {
		tbatt_cold_status = BATTERY_STATUS__COLD_PHASE2;
	} else {
		tbatt_cold_status = BATTERY_STATUS__COLD_PHASE1;
	}

	if (chip->tbatt_cold_pre_shake == TBATT_PRE_SHAKE_INVALID) {
		chip->tbatt_cold_pre_shake = batt_temp;
	}

	if (chip->tbatt_cold_status != tbatt_cold_status) {
		chip->tbatt_cold_status = tbatt_cold_status;
		if (oplus_vooc_get_allow_reading() == true) {
			oplus_chg_set_float_voltage(chip);
			oplus_chg_set_charging_current(chip);
		}

		if (batt_temp > chip->tbatt_cold_pre_shake) {
			shake = -HYSTERISIS_DECIDEGC;
		} else if (batt_temp < chip->tbatt_cold_pre_shake) {
			shake = HYSTERISIS_DECIDEGC;
		}

		chip->limits.freeze_bat_decidegc = chip->anti_shake_bound.freeze_bound + shake;
		chg_debug("tbatt_cold_status status change, [%d %d %d %d]\n", chip->tbatt_cold_status,
			  chip->limits.freeze_bat_decidegc, chip->tbatt_cold_pre_shake, batt_temp);
		chip->tbatt_cold_pre_shake = batt_temp;
	}
}

#define VCHG_CNT 2
static bool oplus_chg_check_vchg_is_good(struct oplus_chg_chip *chip)
{
	static bool ret = true;
	static int vchg_counts = 0;
	int chg_volt = chip->charger_volt;
	OPLUS_CHG_VCHG_STATUS vchg_status = chip->vchg_status;

#ifndef WPC_NEW_INTERFACE
	if (chip->wireless_support && (oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present())) {
		chg_err(" test wireless fastchg charge start\n");
		return true;
	}
#else
	if (oplus_wpc_get_status() != 0) {
		chg_err(" test do not set ichging , wireless charge start \n");
		return;
	}
#endif

	if (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) {
		return true;
	}
	if (oplus_vooc_get_fastchg_started() == true) {
		return true;
	}
	if (oplus_is_pps_charging() == true) {
		return true;
	}

	if (chg_volt > chip->limits.charger_hv_thr) {
		vchg_counts++;
		if (vchg_counts >= VCHG_CNT) {
			vchg_counts = 0;
			ret = false;
			vchg_status = CHARGER_STATUS__VOL_HIGH;
		}
	} else if (chg_volt <= chip->limits.charger_recv_thr) {
		vchg_counts = 0;
		ret = true;
		vchg_status = CHARGER_STATUS__GOOD;
	}
	if (vchg_status != chip->vchg_status) {
		chip->vchg_status = vchg_status;
	}
	return ret;
}

#define VBAT_CNT 3

static bool oplus_chg_check_vbatt_is_good(struct oplus_chg_chip *chip)
{
	static bool ret = true;
	static int vbat_counts = 0;
	int batt_volt = chip->batt_volt;

	if (batt_volt >= chip->limits.vbatt_hv_thr) {
		vbat_counts++;
		if (vbat_counts >= VBAT_CNT) {
			vbat_counts = 0;
			ret = false;
			chip->vbatt_over = true;
		}
	} else {
		vbat_counts = 0;
		ret = true;
		chip->vbatt_over = false;
	}
	return ret;
}

static bool oplus_chg_check_time_is_good(struct oplus_chg_chip *chip)
{
#ifdef SELL_MODE
	chip->chging_over_time = false;
	printk("oplus_chg_check_time_is_good_sell_mode\n");
	return true;
#endif /* SELL_MODE */

	if (chip->limits.max_chg_time_sec < 0) {
		chip->chging_over_time = false;
		return true;
	}
	if (chip->total_time >= chip->limits.max_chg_time_sec) {
		chip->total_time = chip->limits.max_chg_time_sec;
		chip->chging_over_time = true;
		return false;
	} else {
		chip->chging_over_time = false;
		return true;
	}
}

#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
static int fb_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	int blank;
	struct msm_drm_notifier *evdata = data;

	if (!g_charger_chip) {
		return 0;
	}
	if (!evdata || (evdata->id != 0)) {
		return 0;
	}

	if (event == MSM_DRM_EARLY_EVENT_BLANK) {
		blank = *(int *)(evdata->data);
		if (blank == MSM_DRM_BLANK_UNBLANK) {
			g_charger_chip->led_on = true;
			if (oplus_get_flash_screen_ctrl() == true && g_charger_chip->real_batt_full) {
				oplus_chg_suspend_charger();
				g_charger_chip->limits.recharge_mv = 60;
			}
			g_charger_chip->led_on_change = true;
		} else if (blank == MSM_DRM_BLANK_POWERDOWN) {
			g_charger_chip->led_on = false;
			if (oplus_get_flash_screen_ctrl() == true && g_charger_chip->real_batt_full) {
				g_charger_chip->chg_ops->charger_unsuspend();
				g_charger_chip->limits.recharge_mv = 100;
			}
			g_charger_chip->led_on_change = true;
		} else {
			pr_err("%s: receives wrong data EARLY_BLANK:%d\n", __func__, blank);
		}
	}
	return 0;
}

void oplus_chg_set_led_status(bool val)
{
	/*Do nothing*/
}
EXPORT_SYMBOL(oplus_chg_set_led_status);
#elif IS_ENABLED(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	int blank;
	struct fb_event *evdata = data;

	if (!g_charger_chip) {
		return 0;
	}
	if (evdata && evdata->data) {
		if (event == FB_EVENT_BLANK) {
			blank = *(int *)evdata->data;
			if (blank == FB_BLANK_UNBLANK) {
				g_charger_chip->led_on = true;
				g_charger_chip->led_on_change = true;
			} else if (blank == FB_BLANK_POWERDOWN) {
				g_charger_chip->led_on = false;
				g_charger_chip->led_on_change = true;
			}
		}
	}
	return 0;
}

void oplus_chg_set_led_status(bool val)
{
	/*Do nothing*/
}
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG)
static int chg_mtk_drm_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	int *blank = (int *)data;

	if (!blank) {
		chg_err("get disp statu err, blank is NULL!");
		g_charger_chip->led_on = true;
		g_charger_chip->led_on_change = true;
		return 0;
	}

	if (!event) {
		chg_err("Invalid event\n");
		return 0;
	}

	if (!g_charger_chip) {
		return 0;
	}

	switch (event) {
	case MTK_DISP_EARLY_EVENT_BLANK:
		if (*blank == MTK_DISP_BLANK_UNBLANK) {
			g_charger_chip->led_on = true;
			g_charger_chip->led_on_change = true;
		} else if (*blank == MTK_DISP_BLANK_POWERDOWN) {
			g_charger_chip->led_on = false;
			g_charger_chip->led_on_change = true;
		} else {
			pr_err("%s: receives wrong data EARLY_BLANK:%d\n", __func__, blank);
		}
	case MTK_DISP_EVENT_BLANK:
		if (*blank == MTK_DISP_BLANK_UNBLANK) {
			g_charger_chip->led_on = true;
			g_charger_chip->led_on_change = true;
		} else if (*blank == MTK_DISP_BLANK_POWERDOWN) {
			g_charger_chip->led_on = false;
			g_charger_chip->led_on_change = true;
		} else {
			pr_err("%s: receives wrong data BLANK:%d\n", __func__, blank);
		}
	}
	return 0;
}
#else
void oplus_chg_set_led_status(bool val)
{
	charger_xlog_printk(CHG_LOG_CRTI, " val = %d\n", val);
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->led_on = val;
		g_charger_chip->led_on_change = true;
	}
}
EXPORT_SYMBOL(oplus_chg_set_led_status);
#endif

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
static void chg_panel_notifier_callback(enum panel_event_notifier_tag tag,
					struct panel_event_notification *notification, void *client_data)
{
	if (!notification) {
		charger_xlog_printk(CHG_LOG_CRTI, "Invalid notification\n");
		return;
	}

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		charger_xlog_printk(CHG_LOG_CRTI, "received unblank event: %d\n",
				    notification->notif_data.early_trigger);
		if (notification->notif_data.early_trigger) {
			g_charger_chip->led_on = true;
			g_charger_chip->led_on_change = true;
		}
		break;
	case DRM_PANEL_EVENT_BLANK:
		charger_xlog_printk(CHG_LOG_CRTI, "received blank event: %d\n", notification->notif_data.early_trigger);
		if (notification->notif_data.early_trigger) {
			g_charger_chip->led_on = false;
			g_charger_chip->led_on_change = true;
		}
		break;
	case DRM_PANEL_EVENT_BLANK_LP:
		charger_xlog_printk(CHG_LOG_CRTI, "received lp event\n");
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		break;
	default:
		break;
	}
}
#endif

void oplus_chg_set_allow_switch_to_fastchg(bool allow)
{
	charger_xlog_printk(CHG_LOG_CRTI, " allow = %d\n", allow);
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->allow_swtich_to_fastchg = allow;
	}
}

void oplus_chg_set_camera_status(bool val)
{
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->camera_on = val;
	}
}
EXPORT_SYMBOL(oplus_chg_set_camera_status);

extern void oplus_voocphy_notify(int event);
void oplus_chg_set_flash_led_status(bool val)
{
	if (!g_charger_chip)
		return;

	chg_debug("flash_led_status:%d->%d\n", g_charger_chip->flash_led_status, val);
	if (oplus_is_power_off_charging(NULL)) {
		return;
	}

	if (g_charger_chip->flash_led_status == val) {
		return;
	}

	g_charger_chip->flash_led_status = val;
	if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
		oplus_voocphy_notify(g_charger_chip->flash_led_status);
	} else {
	}
}

bool oplus_chg_get_flash_led_status(void)
{
	if (!g_charger_chip)
		return false;
	chg_debug("flash_led_status:%d\n", g_charger_chip->flash_led_status);
	return g_charger_chip->flash_led_status;
}
EXPORT_SYMBOL(oplus_chg_set_flash_led_status);

#define TLED_CHANGE_COUNTS 4
#define TLED_HYSTERISIS_DECIDEGC 10
static void oplus_chg_check_tled_status(struct oplus_chg_chip *chip)
{
	OPLUS_CHG_TLED_STATUS tled_status = chip->led_temp_status;
	static int high_counts = 0, warm_counts = 0, normal_counts = 0;

	if (chip->temperature > chip->limits.led_high_bat_decidegc_antishake) { /* >37C */
		high_counts++;
		if (high_counts >= TLED_CHANGE_COUNTS) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tled_status = LED_TEMP_STATUS__HIGH;
		}
	} else if (chip->temperature > chip->limits.led_warm_bat_decidegc_antishake) { /* >35C && <= 37 */
		warm_counts++;
		if (warm_counts >= TLED_CHANGE_COUNTS) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tled_status = LED_TEMP_STATUS__WARM;
		}
	} else {
		normal_counts++;
		if (normal_counts >= TLED_CHANGE_COUNTS) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tled_status = LED_TEMP_STATUS__NORMAL;
		}
	}
	if (tled_status != chip->led_temp_status) {
		chip->limits.led_warm_bat_decidegc_antishake = chip->limits.led_warm_bat_decidegc;
		chip->limits.led_high_bat_decidegc_antishake = chip->limits.led_high_bat_decidegc;
		if (tled_status > chip->led_temp_status && tled_status == LED_TEMP_STATUS__WARM) {
			chip->limits.led_warm_bat_decidegc_antishake =
				chip->limits.led_warm_bat_decidegc - TLED_HYSTERISIS_DECIDEGC;
		} else if (tled_status > chip->led_temp_status && tled_status == LED_TEMP_STATUS__HIGH) {
			chip->limits.led_high_bat_decidegc_antishake =
				chip->limits.led_high_bat_decidegc - TLED_HYSTERISIS_DECIDEGC;
		} else if (tled_status < chip->led_temp_status && tled_status == LED_TEMP_STATUS__NORMAL) {
			chip->limits.led_warm_bat_decidegc_antishake =
				chip->limits.led_warm_bat_decidegc + TLED_HYSTERISIS_DECIDEGC;
		} else if (tled_status < chip->led_temp_status && tled_status == LED_TEMP_STATUS__WARM) {
			chip->limits.led_high_bat_decidegc_antishake =
				chip->limits.led_high_bat_decidegc + TLED_HYSTERISIS_DECIDEGC;
		}
		chg_debug("tled status change, [%d %d %d %d]\n", tled_status, chip->led_temp_status,
			  chip->limits.led_warm_bat_decidegc_antishake, chip->limits.led_high_bat_decidegc_antishake);
		chip->led_temp_change = true;
		chip->led_temp_status = tled_status;
	}
}

static void oplus_chg_check_led_on_ichging(struct oplus_chg_chip *chip)
{
	if (chip->led_on_change || (chip->led_on && chip->led_temp_change)) {
		chip->led_on_change = false;
		chip->led_temp_change = false;
		if (chip->chg_ctrl_by_vooc && chip->vbatt_num == 2) {
			if (oplus_vooc_get_fastchg_started() == true &&
			    oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC) {
				return;
			}
			if (oplus_vooc_get_allow_reading() == true ||
			    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC) {
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
			}
		} else {
			if (oplus_vooc_get_fastchg_started() == true) {
				return;
			}
			if (oplus_vooc_get_allow_reading() == true) {
				if (chip->dual_charger_support) {
					chip->slave_charger_enable = false;
				}
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
			}
		}
	}
}

#define TVOOC_COUNTS 2
#define TVOOC_HYSTERISIS_DECIDEGC 10

static void oplus_chg_check_vooc_temp_status(struct oplus_chg_chip *chip)
{
	int batt_temp = chip->temperature;
	OPLUS_CHG_TBAT_VOOC_STATUS tbat_vooc_status = chip->vooc_temp_status;
	static int high_counts = 0, warm_counts = 0, normal_counts = 0;
	static bool vooc_first_set_input_current_flag = false;

	if (chip->vbatt_num != 2 || oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC ||
	    oplus_vooc_get_fastchg_started() == false) {
		vooc_first_set_input_current_flag = false;
		high_counts = 0;
		warm_counts = 0;
		normal_counts = 0;
		return;
	}
	if (batt_temp > chip->limits.vooc_high_bat_decidegc) { /*>45C*/
		if (oplus_vooc_get_fastchg_started() == true) {
			chg_err("tbatt > 45, quick out vooc");
			oplus_chg_set_chargerid_switch_val(0);
			oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
		}
	} else if (batt_temp > chip->limits.vooc_warm_bat_decidegc_antishake) { /*>38C && <= 45*/
		high_counts++;
		if (high_counts >= TVOOC_COUNTS || vooc_first_set_input_current_flag == false) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tbat_vooc_status = VOOC_TEMP_STATUS__HIGH;
		}
	} else if (batt_temp >= chip->limits.vooc_normal_bat_decidegc_antishake) { /*>34C && <= 38*/
		warm_counts++;
		if (warm_counts >= TVOOC_COUNTS || vooc_first_set_input_current_flag == false) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tbat_vooc_status = VOOC_TEMP_STATUS__WARM;
		}
	} else { /* < 34 */
		normal_counts++;
		if (normal_counts >= TVOOC_COUNTS || vooc_first_set_input_current_flag == false) {
			high_counts = 0;
			warm_counts = 0;
			normal_counts = 0;
			tbat_vooc_status = VOOC_TEMP_STATUS__NORMAL;
		}
	}
	chg_err("tbat_vooc_status[%d],chip->vooc_temp_status[%d] ", tbat_vooc_status, chip->vooc_temp_status);
	if (vooc_first_set_input_current_flag == false) {
		chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.charger_current_vooc_ma_normal;
		chip->limits.temp_normal_fastchg_current_ma = chip->limits.charger_current_vooc_ma_normal;
		oplus_chg_set_charging_current(chip);
		chg_err("set charger current ctrl by vooc[%d]\n", chip->limits.temp_little_cool_fastchg_current_ma);
	}
	if (tbat_vooc_status != chip->vooc_temp_status || vooc_first_set_input_current_flag == false) {
		chip->limits.vooc_warm_bat_decidegc_antishake = chip->limits.vooc_warm_bat_decidegc;
		chip->limits.vooc_normal_bat_decidegc_antishake = chip->limits.vooc_normal_bat_decidegc;
		if (tbat_vooc_status > chip->vooc_temp_status && tbat_vooc_status == VOOC_TEMP_STATUS__WARM) {
			chip->limits.vooc_normal_bat_decidegc_antishake =
				chip->limits.vooc_normal_bat_decidegc - TVOOC_HYSTERISIS_DECIDEGC;
		} else if (tbat_vooc_status > chip->vooc_temp_status && tbat_vooc_status == VOOC_TEMP_STATUS__HIGH) {
			chip->limits.vooc_warm_bat_decidegc_antishake =
				chip->limits.vooc_warm_bat_decidegc - TVOOC_HYSTERISIS_DECIDEGC;
		} else if (tbat_vooc_status < chip->vooc_temp_status && tbat_vooc_status == VOOC_TEMP_STATUS__NORMAL) {
			chip->limits.vooc_normal_bat_decidegc_antishake =
				chip->limits.vooc_normal_bat_decidegc + TVOOC_HYSTERISIS_DECIDEGC;
		} else if (tbat_vooc_status < chip->vooc_temp_status && tbat_vooc_status == VOOC_TEMP_STATUS__WARM) {
			chip->limits.vooc_warm_bat_decidegc_antishake =
				chip->limits.vooc_warm_bat_decidegc + TVOOC_HYSTERISIS_DECIDEGC;
		}
		chg_debug("tled status change, [%d %d %d %d]\n", tbat_vooc_status, chip->vooc_temp_status,
			  chip->limits.vooc_warm_bat_decidegc_antishake,
			  chip->limits.vooc_normal_bat_decidegc_antishake);
		vooc_first_set_input_current_flag = true;
		chip->vooc_temp_change = true;
		chip->vooc_temp_status = tbat_vooc_status;
	}
}

static void oplus_chg_check_vooc_ichging(struct oplus_chg_chip *chip)
{
	if (chip->vbatt_num == 2 && chip->vooc_temp_change) {
		chip->vooc_temp_change = false;
		if (oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC) {
			oplus_chg_set_input_current_limit(chip);
		}
	}
}

static void oplus_chg_check_cool_down_ichging(struct oplus_chg_chip *chip)
{
	chip->cool_down_done = false;
	if (oplus_vooc_get_allow_reading() == true) {
		if (chip->dual_charger_support) {
			chip->slave_charger_enable = false;
			oplus_chg_set_charging_current(chip);
		}
		oplus_chg_set_input_current_limit(chip);
	}
}

static void oplus_chg_check_camera_on_ichging(struct oplus_chg_chip *chip)
{
	static bool camera_pre = false;

	if (chip->camera_on != camera_pre) {
		camera_pre = chip->camera_on;
		if (oplus_vooc_get_fastchg_started() == true) {
			return;
		}
		if (oplus_vooc_get_allow_reading() == true) {
			if (chip->dual_charger_support) {
				chip->slave_charger_enable = false;
				oplus_chg_set_charging_current(chip);
			}
			oplus_chg_set_input_current_limit(chip);
		}
	}
}

static void oplus_chg_check_calling_on_ichging(struct oplus_chg_chip *chip)
{
	static bool calling_pre = false;

	if (chip->calling_on != calling_pre) {
		calling_pre = chip->calling_on;
		if (oplus_vooc_get_fastchg_started() == true) {
			return;
		}
		if (oplus_vooc_get_allow_reading() == true) {
			if (chip->dual_charger_support) {
				chip->slave_charger_enable = false;
				oplus_chg_set_charging_current(chip);
			}
			oplus_chg_set_input_current_limit(chip);
		}
	}
}

static void oplus_chg_battery_authenticate_check(struct oplus_chg_chip *chip)
{
	static bool charger_exist_pre = false;

	if (charger_exist_pre ^ chip->charger_exist) {
		charger_exist_pre = chip->charger_exist;
		if (chip->charger_exist && !chip->authenticate) {
			if (oplus_switching_support_parallel_chg()) {
				chip->authenticate = true;
			} else {
				chip->authenticate = oplus_gauge_get_batt_authenticate();
			}
			if (chip->bat_volt_different)
				chip->authenticate = false;
		}
	}
}

void oplus_chg_variables_reset(struct oplus_chg_chip *chip, bool in)
{
	int i;
	if (in) {
		chip->charger_exist = true;
		chip->chging_on = true;
		chip->slave_charger_enable = false;
		if (chip->hmac == false && chip->external_authenticate) {
			chip->hmac = oplus_gauge_get_batt_external_hmac();
		}
		if (chip->hmac == false && chip->external_gauge) {
			chip->hmac = oplus_gauge_get_batt_hmac();
			if (oplus_switching_support_parallel_chg() &&
			    chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS8)
				chip->hmac = false;
		}

		if (!oplus_chg_show_vooc_logo_ornot() && chip->pre_charger_exist == false) {
			chip->quick_mode_time = current_kernel_time();
			chip->start_time = chip->quick_mode_time.tv_sec;
			chip->quick_mode_gain_time_ms = 0;
			chip->quick_mode_start_time = 0;
			chip->quick_mode_stop_time = 0;
			chip->quick_mode_start_cap = -1;
			chip->quick_mode_stop_cap = -1;
			chip->quick_mode_stop_temp = -1;
			chip->quick_mode_stop_soc = -1;
		}
		if (chip->balancing_bat_status != PARALLEL_BAT_BALANCE_ERROR_STATUS8 &&
		    chip->balancing_bat_status != PARALLEL_BAT_BALANCE_ERROR_STATUS9) {
			chip->balancing_bat_stop_chg = 0;
			chip->balancing_bat_status = -1;
			chip->balancing_bat_stop_fastchg = 0;
		}
		oplus_first_enable_adsp_voocphy();
		oplus_comm_fginfo_reset(chip);
	} else {
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_adsp_voocphy_clear_status();
		}
		if (!oplus_chg_show_vooc_logo_ornot()) {
			if (chip->decimal_control) {
				cancel_delayed_work_sync(&g_charger_chip->ui_soc_decimal_work);
				chip->last_decimal_ui_soc = (chip->ui_soc_integer + chip->ui_soc_decimal);
				oplus_chg_ui_soc_decimal_deinit();
				pr_err("[oplus_chg_variables_reset]cancel last_decimal_ui_soc:%d",
				       chip->last_decimal_ui_soc);
			}
			chip->calculate_decimal_time = 0;
			chip->bms_heat_temp_compensation = 0;
		}
		if ((chip->mmi_fastchg == 1) && (chip->chg_cycle_status & CHG_CYCLE_VOTER__USER)) {
			chip->chg_cycle_status &= ~(int)CHG_CYCLE_VOTER__USER;
			if (!chip->chg_cycle_status) {
				chip->stop_chg = 1;
				if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY)
					oplus_adsp_voocphy_turn_on();
			}
		}
		chip->unwakelock_chg = 0;
		chip->allow_swtich_to_fastchg = 1;
		chip->charger_exist = false;
		chip->chging_on = false;
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->charger_subtype = CHARGER_SUBTYPE_DEFAULT;
		chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->usbtemp_batt_current = 0;
		chip->usbtemp_pre_batt_current = 0;
		chip->usbtemp_change_across_unplug = true;
		vbatt_higherthan_4180mv = false;
		mcu_update = false;
		suspend_charger = false;
		chip->pd_svooc = false;
		chip->notify_flag = 0;
		chip->fg_soft_reset_done = true;
	}
	chip->pre_charger_exist = chip->charger_exist;
	chip->qc_abnormal_check_count = 0;
	chip->limits.vbatt_pdqc_to_9v_thr = oplus_get_vbatt_pdqc_to_9v_thr();
	chip->sw_full_count = 0;
	chip->sw_full = false;
	chip->full_data.vbat_counts_hw = 0;
	chip->full_data.sub_vbat_counts_hw = 0;
	chip->full_data.hw_full = false;
	chip->full_data.sub_hw_full = false;
	chip->full_data.clear_full_check_count = 0;
	chip->hw_full_by_sw = false;
	chip->sw_sub_batt_full_count = 0;
	chip->sw_sub_batt_full = false;
	chip->hw_sub_batt_full_by_sw = false;
	chip->vchg_status = CHARGER_STATUS__GOOD;
	chip->batt_full = false;
	chip->real_batt_full = false;
	chip->tbatt_when_full = 200;
	chip->recharge_after_ffc = false;
	chip->tbatt_status = BATTERY_STATUS__NORMAL;
	chip->tbatt_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE1;
	chip->tbatt_normal_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->tbatt_cold_status = BATTERY_STATUS__COLD_PHASE1;
	chip->tbatt_cold_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->vbatt_over = 0;
	chip->total_time = 0;
	chip->chging_over_time = 0;
	chip->in_rechging = 0;
	chip->adsp_notify_ap_suspend = 0;
	chip->stop_voter = 0x00;
	chip->charging_state = CHARGING_STATUS_CCCV;
#ifndef SELL_MODE
	if (chip->mmi_fastchg == 0) {
		chip->mmi_chg = 0;
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_adsp_voocphy_turn_off();
		}
	} else {
		if (!in)
			chip->mmi_chg = 1;
	}
#endif /* SELL_MODE */
	chip->unwakelock_chg = 0;
	chip->notify_code = 0;
	if (!((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) || (oplus_vooc_get_fastchg_started() == true) ||
	      (oplus_vooc_get_fastchg_to_warm() == true) || (oplus_vooc_get_fastchg_dummy_started() == true) ||
	      (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
	      (oplus_vooc_get_btb_temp_over() == true))) {
		chip->cool_down = 0;
		chip->cool_down_done = false;
	}
	chip->cool_down_force_5v = false;
	chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
	chip->limits.freeze_bat_decidegc = chip->anti_shake_bound.freeze_bound;
	chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
	chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
	chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
	chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
	chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
	chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;

	chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
	chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
	chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
	chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
	chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
	chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;

	chip->limits.vfloat_over_counts = 0;
	chip->limits.led_warm_bat_decidegc_antishake = chip->limits.led_warm_bat_decidegc;
	chip->limits.led_high_bat_decidegc_antishake = chip->limits.led_high_bat_decidegc;
	chip->led_temp_change = false;
	chip->limits.vooc_warm_bat_decidegc_antishake = chip->limits.vooc_warm_bat_decidegc;
	chip->limits.vooc_normal_bat_decidegc_antishake = chip->limits.vooc_normal_bat_decidegc;
	chip->vooc_temp_change = false;
	if (chip->temperature > chip->limits.led_high_bat_decidegc) {
		chip->led_temp_status = LED_TEMP_STATUS__HIGH;
	} else if (chip->temperature > chip->limits.led_warm_bat_decidegc) {
		chip->led_temp_status = LED_TEMP_STATUS__WARM;
	} else {
		chip->led_temp_status = LED_TEMP_STATUS__NORMAL;
	}
	chip->dod0_counts = 0;
	chip->fastchg_to_ffc = false;
	chip->fastchg_ffc_status = 0;
	chip->chg_ctrl_by_lcd = chip->chg_ctrl_by_lcd_default;
	chip->chg_ctrl_by_vooc = chip->chg_ctrl_by_vooc_default;
	chip->ffc_temp_status = FFC_TEMP_STATUS__NORMAL;
	chip->vooc_temp_status = VOOC_TEMP_STATUS__NORMAL;
	chip->limits.iterm_ma = chip->limits.default_iterm_ma;
	chip->limits.sub_iterm_ma = chip->limits.default_sub_iterm_ma;
	chip->limits.temp_normal_fastchg_current_ma = chip->limits.default_temp_normal_fastchg_current_ma;
	chip->limits.normal_vfloat_sw_limit = chip->limits.default_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv = chip->limits.default_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit = chip->limits.default_normal_vfloat_over_sw_limit;
	chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.default_temp_little_cool_fastchg_current_ma;
	chip->limits.little_cool_vfloat_sw_limit = chip->limits.default_little_cool_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv = chip->limits.default_temp_little_cool_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit = chip->limits.default_little_cool_vfloat_over_sw_limit;
	chip->limits.temp_little_cool_fastchg_current_ma_high =
		chip->limits.default_temp_little_cool_fastchg_current_ma_high;
	chip->limits.temp_little_cool_fastchg_current_ma_low =
		chip->limits.default_temp_little_cool_fastchg_current_ma_low;
	chip->limits.temp_little_cold_fastchg_current_ma_high =
		chip->limits.default_temp_little_cold_fastchg_current_ma_high;
	chip->limits.temp_little_cold_fastchg_current_ma_low =
		chip->limits.default_temp_little_cold_fastchg_current_ma_low;
	chip->limits.temp_cold_fastchg_current_ma_high = chip->limits.default_temp_cold_fastchg_current_ma_high;
	chip->limits.temp_cold_fastchg_current_ma_low = chip->limits.default_temp_cold_fastchg_current_ma_low;
	chip->limits.temp_cool_fastchg_current_ma_high = chip->limits.default_temp_cool_fastchg_current_ma_high;
	chip->limits.temp_cool_fastchg_current_ma_low = chip->limits.default_temp_cool_fastchg_current_ma_low;
	chip->limits.temp_warm_fastchg_current_ma = chip->limits.default_temp_warm_fastchg_current_ma;
	chip->limits.input_current_charger_ma = chip->limits.default_input_current_charger_ma;
	chip->limits.input_current_vooc_ma_high = chip->limits.default_input_current_vooc_ma_high;
	chip->limits.input_current_vooc_ma_warm = chip->limits.default_input_current_vooc_ma_warm;
	chip->limits.input_current_vooc_ma_normal = chip->limits.default_input_current_vooc_ma_normal;
	chip->limits.pd_input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
	chip->limits.qc_input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
	oplus_chg_aging_ffc_variable_reset(chip);
	reset_mcu_delay = 0;
	efttest_count = 1;
	high_vol_light = 0;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	chip->pmic_spmi.aicl_suspend = false;
#endif
	oplus_chg_battery_authenticate_check(chip);
	chip->chargerid_volt = 0;
	chip->chargerid_volt_got = false;
	chip->short_c_batt.in_idle = true; /* defualt in idle for userspace */
	chip->short_c_batt.cv_satus = false; /* defualt not in cv chg */
	chip->short_c_batt.disable_rechg = false;
	chip->short_c_batt.limit_chg = false;
	chip->short_c_batt.limit_rechg = false;
	chip->chg_ctrl_by_cool_down = false;
	chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
	chip->usbtemp_cool_down = 0;
	chip->pd_chging = false;
	chip->pd_wait_svid = true;
	chip->vooc_start_fail = false;
	for (i = 0; i < ARRAY_SIZE(chip->ibat_save); i++)
		chip->ibat_save[i] = 500;
	chip->pd_wait_svid = true;
	chg_ctrl_by_sale_mode = false;
	chip->pd_adapter_support_9v = true;
	chip->vooc_start_fail = false;
	chip->cool_down_check_done = false;
	chip->pd_authentication = -ENODATA;
	chip->pps_force_svooc = false;
	chip->usbtemp_curr_status = 0;
	oplus_pps_variables_reset(in);
	oplus_ufcs_variables_reset(false);
	chip->bcc_cool_down = 0;
	chip->bcc_curr_done = BCC_CURR_DONE_UNKNOW;
	chip->limits.force_input_current_ma = 0;
	chip->batt_target_curr = 0;
	chip->pre_charging_current = 0;
	chip->aicl_done = false;
	if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL) {
		oplus_chg_parellel_variables_reset();
		oplus_init_parallel_temp_threshold();
	}
	chip->parallel_error_flag &= ~(REASON_SOC_NOT_FULL);
	chip->soc_not_full_report = false;
	chip->voocphy_notify_ap_suspend = false;
	chg_err("charger detect is %d\n", in);
}

static void oplus_chg_variables_init(struct oplus_chg_chip *chip)
{
	chip->charger_exist = false;
	chip->pre_charger_exist = chip->charger_exist;
	chip->chging_on = false;
	chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->charger_subtype = CHARGER_SUBTYPE_DEFAULT;
	chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->charger_volt = 0;
	chip->vchg_status = CHARGER_STATUS__GOOD;
	chip->prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	chip->sw_full_count = 0;
	chip->sw_full = false;
	chip->full_data.vbat_counts_hw = 0;
	chip->full_data.sub_vbat_counts_hw = 0;
	chip->full_data.hw_full = false;
	chip->full_data.sub_hw_full = false;
	chip->full_data.clear_full_check_count = 0;
	chip->hw_full_by_sw = false;
	chip->sw_sub_batt_full_count = 0;
	chip->sw_sub_batt_full = false;
	chip->hw_sub_batt_full_by_sw = false;
	chip->batt_exist = true;
	chip->batt_full = false;
	chip->real_batt_full = false;
	chip->tbatt_when_full = 200;
	chip->recharge_after_ffc = false;
	chip->tbatt_status = BATTERY_STATUS__NORMAL;
	chip->tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE1;
	chip->tbatt_cold_status = BATTERY_STATUS__COLD_PHASE1;
	chip->tbatt_cold_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->vbatt_over = 0;
	chip->total_time = 0;
	chip->chging_over_time = 0;
	chip->in_rechging = 0;
	chip->batt_volt = 3800;
	chip->icharging = 0;
	chip->temperature = 250;
	chip->shell_temp = TEMPERATURE_INVALID;
	chip->subboard_temp = TEMPERATURE_INVALID;
	chip->tbatt_shell_status = false;
	chip->soc = 0;
	chip->ui_soc = 50;
	chip->notify_code = 0;
	chip->notify_flag = 0;
	chip->cool_down = 0;
	chip->tbatt_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->tbatt_normal_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->led_on = true;
	chip->camera_on = 0;
	chip->stop_voter = 0x00;
	chip->charging_state = CHARGING_STATUS_CCCV;
	chip->mmi_chg = 1;
	chip->unwakelock_chg = 0;
	chip->chg_cycle_status = CHG_CYCLE_VOTER__NONE;
	chip->chg_powersave = false;
	chip->allow_swtich_to_fastchg = 1;
	chip->stop_chg = 1;
	chip->mmi_fastchg = 1;
	chip->cool_down_done = false;
	chip->healthd_ready = false;
	chip->is_gauge_ready = false;
	chip->dischg_flag = false;
	chip->usb_status = 0;
	chip->reset_mcu_jiffies = jiffies;
	init_waitqueue_head(&chip->oplus_usbtemp_wq);
	init_waitqueue_head(&chip->oplus_bcc_wq);
	if (chip->support_usbtemp_protect_v2)
		init_waitqueue_head(&chip->oplus_usbtemp_wq_new_method);
	chip->usbtemp_wq_init_finished = true;
	chip->smooth_to_soc_gap = 5;
	chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
	chip->usbtemp_cool_down = 0;
	chip->em_mode = false;
	chip->detect_detach_unexpeactly = 0;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	chip->usb_online = false;
	chip->otg_online = false;
#else
/*	chip->pmic_spmi.usb_online = false;
		IC have init already	*/
#endif
	if (chip->external_gauge) {
		chg_debug("use oplus_gauge_get_batt_authenticate\n");
		chip->authenticate = oplus_gauge_get_batt_authenticate();
		chip->hmac = oplus_gauge_get_batt_hmac();
		if (!chip->authenticate) {
			chg_debug(" retry to get the authenticate\n");
			chip->authenticate = oplus_gauge_get_batt_authenticate();
		}

		if (!chip->hmac) {
			chg_debug(" retry to get the battery hmac\n");
			chip->hmac = oplus_gauge_get_batt_hmac();
		}
	} else {
		chg_debug("use get_oplus_high_battery_status\n");
		chip->authenticate = oplus_gauge_get_batt_authenticate();
		if (chip->external_authenticate) {
			chip->hmac = oplus_gauge_get_batt_external_hmac();
		} else {
			chip->hmac = true;
		}
	}

	if (chip->bat_volt_different)
		chip->authenticate = false;
	if (!chip->authenticate)
		chip->chg_ops->charging_disable();

	chip->otg_switch = false;
	chip->ui_otg_switch = false;
	chip->boot_mode = chip->chg_ops->get_boot_mode();
	chip->boot_reason = chip->chg_ops->get_boot_reason();
	chip->anti_shake_bound.cold_bound = chip->limits.cold_bat_decidegc;
	chip->anti_shake_bound.freeze_bound = chip->limits.freeze_bat_decidegc;
	chip->anti_shake_bound.little_cold_bound = chip->limits.little_cold_bat_decidegc;
	chip->anti_shake_bound.cool_bound = chip->limits.cool_bat_decidegc;
	chip->anti_shake_bound.little_cool_bound = chip->limits.little_cool_bat_decidegc;
	chip->anti_shake_bound.normal_bound = chip->limits.normal_bat_decidegc;
	chip->anti_shake_bound.warm_bound = chip->limits.warm_bat_decidegc;
	chip->anti_shake_bound.hot_bound = chip->limits.hot_bat_decidegc;

	chip->tbatt_normal_anti_shake_bound.phase1_bound = chip->limits.normal_phase1_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase2_bound = chip->limits.normal_phase2_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase3_bound = chip->limits.normal_phase3_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase4_bound = chip->limits.normal_phase4_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase5_bound = chip->limits.normal_phase5_bat_decidegc;
	chip->tbatt_normal_anti_shake_bound.phase6_bound = chip->limits.normal_phase6_bat_decidegc;

	chip->limits.led_warm_bat_decidegc_antishake = chip->limits.led_warm_bat_decidegc;
	chip->limits.led_high_bat_decidegc_antishake = chip->limits.led_high_bat_decidegc;
	chip->led_temp_change = false;
	chip->limits.vooc_warm_bat_decidegc_antishake = chip->limits.vooc_warm_bat_decidegc;
	chip->limits.vooc_normal_bat_decidegc_antishake = chip->limits.vooc_normal_bat_decidegc;
	chip->vooc_temp_change = false;

	if (chip->temperature > chip->limits.led_high_bat_decidegc)
		chip->led_temp_status = LED_TEMP_STATUS__HIGH;
	else if (chip->temperature > chip->limits.led_warm_bat_decidegc)
		chip->led_temp_status = LED_TEMP_STATUS__WARM;
	else
		chip->led_temp_status = LED_TEMP_STATUS__NORMAL;
	chip->limits.vfloat_over_counts = 0;
	chip->chargerid_volt = 0;
	chip->chargerid_volt_got = false;
	chip->enable_shipmode = 0;
	chip->dod0_counts = 0;
	chip->fastchg_to_ffc = false;
	chip->fastchg_ffc_status = 0;
	chip->waiting_for_ffc = false;
	chip->ffc_temp_status = FFC_TEMP_STATUS__NORMAL;
	chip->vooc_temp_status = VOOC_TEMP_STATUS__NORMAL;
	chip->short_c_batt.err_code = oplus_short_c_batt_err_code_init();
	chip->short_c_batt.is_switch_on = oplus_short_c_batt_chg_switch_init();
	chip->short_c_batt.is_feature_sw_on = oplus_short_c_batt_feature_sw_status_init();
	chip->short_c_batt.is_feature_hw_on = oplus_short_c_batt_feature_hw_status_init();
	chip->short_c_batt.shortc_gpio_status = 1;
	chip->short_c_batt.disable_rechg = false;
	chip->short_c_batt.limit_chg = false;
	chip->short_c_batt.limit_rechg = false;
	chip->slave_charger_enable = false;
	chip->cool_down_force_5v = false;
	chip->chg_ctrl_by_cool_down = false;
	chip->ui_soc_decimal = 0;
	chip->ui_soc_integer = 0;
	chip->last_decimal_ui_soc = 0;
	chip->decimal_control = false;
	chip->calculate_decimal_time = 0;
	chip->boot_completed = false;
	chip->pd_chging = false;
	chip->pd_authentication = -ENODATA;
	chip->pps_force_svooc = false;
	chip->pd_svooc = false;
	chip->usbtemp_check = false;
	chip->flash_screen_ctrl_status |= FLASH_SCREEN_CTRL_OTA;
	chip->modify_soc = 0;
	chip->soc_ajust = 0;
	chip->em_mode = false;
	chip->svooc_detach_time = 0;
	chip->svooc_detect_time = 0;
	chip->icon_debounce = false;
	chip->is_abnormal_adapter = false;
	chip->abnormal_adapter_dis_cnt = 0;
	chip->pd_wait_svid = true;
	chip->pd_adapter_support_9v = true;
	chip->bcc_cool_down = 0;
	mutex_init(&chip->bcc_curr_done_mutex);
	chip->bcc_curr_done = BCC_CURR_DONE_UNKNOW;
	chip->vooc_start_fail = false;
	chip->bidirect_abnormal_adapter = false;
	chip->cool_down_check_done = false;
	chip->limits.force_input_current_ma = 0;
	atomic_set(&chip->mos_lock, 1);
	chip->mos_test_result = 0;
	chip->mos_test_started = false;
	chip->fg_soft_reset_done = false;
	chip->fg_check_ibat_cnt = 0;
	chip->fg_soft_reset_fail_cnt = 0;
	chip->batt_target_curr = 0;
	chip->pre_charging_current = 0;
	chip->aicl_done = false;
	chip->parallel_error_flag = 0;
	chip->soc_not_full_report = false;
	chip->quick_mode_need_update = false;
	chip->voocphy_notify_ap_suspend = false;
	chip->usbin_abnormal_status = false;
	chip->check_usbin_from_adsp_cnt = 0;
	chip->usb_present_vbus0_count = 0;
}

static void oplus_chg_fail_action(struct oplus_chg_chip *chip)
{
	chg_err("[BATTERY] BAD Battery status... Charging Stop !!\n");
	chip->charging_state = CHARGING_STATUS_FAIL;
	chip->chging_on = false;
	chip->batt_full = false;
	chip->tbatt_when_full = 200;
	chip->in_rechging = 0;
}

#define D_RECHGING_CNT 5
static void oplus_chg_check_rechg_status(struct oplus_chg_chip *chip)
{
	int recharging_vol;
	int nbat_vol = chip->batt_volt;
	static int rechging_cnt = 0;
	if (chip->mmi_chg == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, " mmi_chg,return\n");
		return;
	}

	if (chip->mmi_chg == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, " mmi_chg,return\n");
		return;
	}

	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) { /* 4.0 */
		recharging_vol = oplus_chg_get_float_voltage(chip) - 300;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) { /* 4.4 */
		recharging_vol = oplus_chg_get_float_voltage(chip) - 200;
	} else {
		recharging_vol = oplus_chg_get_float_voltage(chip); /* warm 4.1 */
		if (recharging_vol > chip->limits.temp_normal_vfloat_mv) {
			recharging_vol = chip->limits.temp_normal_vfloat_mv;
		}
		recharging_vol = recharging_vol - chip->limits.recharge_mv;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		recharging_vol = chip->limits.non_standard_vfloat_mv - 400; /* 3.80 */
	}
	if (nbat_vol <= recharging_vol) {
		rechging_cnt++;
	} else {
		rechging_cnt = 0;
	}

	/*don't rechg here unless prohibit rechg is false*/
	if (oplus_short_c_batt_is_disable_rechg(chip)) {
		if (rechging_cnt >= D_RECHGING_CNT) {
			charger_xlog_printk(CHG_LOG_CRTI,
					    "[Battery] disable rechg! batt_volt = %d, nReChgingVol = %d\r\n", nbat_vol,
					    recharging_vol);
			rechging_cnt = D_RECHGING_CNT;
		}
	}
	if (rechging_cnt > D_RECHGING_CNT) {
		charger_xlog_printk(CHG_LOG_CRTI,
				    "[Battery] Battery rechg begin! batt_volt = %d, recharging_vol = %d\n", nbat_vol,
				    recharging_vol);
		rechging_cnt = 0;
		chip->in_rechging = true;
		chip->real_batt_full = false;
		oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__FULL); /*now rechging!*/
		oplus_comm_fginfo_reset(chip);
	}
}

#define ALLOW_DIFF_VALUE 1000
static void oplus_check_battery_vol_diff(struct oplus_chg_chip *chg)
{
	int vbat_cell_max = 0;
	int vbat_cell_min = 0;

	if (!chg)
		return;

	if (chg->vbatt_num != 2)
		return;

	vbat_cell_max = chg->batt_volt_max;
	vbat_cell_min = chg->batt_volt_min;

	if (abs(vbat_cell_max - vbat_cell_min) > ALLOW_DIFF_VALUE) {
		chg->check_battery_vol_count++;
		if (chg->check_battery_vol_count > 5) {
			if (oplus_vooc_get_fastchg_started() == true) {
				oplus_vooc_turn_off_fastchg();
			}
			oplus_pps_set_vbatt_diff(true);
			oplus_ufcs_set_vbatt_diff(true);
			oplus_chg_voter_charging_stop(chg, CHG_STOP_VOTER_BAD_VOL_DIFF);
			oplus_vooc_set_fastchg_allow(false);
			chg->bat_volt_different = true;
			chg->authenticate = false;
			chg->check_battery_vol_count = 0;
			pr_info("BATTERY_SOFT_DIFF_VOLTAGE disable chg\n");
		}
	} else {
		if (chg->bat_volt_different) {
			chg->check_battery_vol_count++;
			if (chg->check_battery_vol_count > 5) {
				chg->bat_volt_different = false;
				chg->authenticate = true;
				chg->check_battery_vol_count = 0;
				pr_info("Recovery BATTERY_SOFT_DIFF_VOLTAGE\n");
				oplus_pps_set_vbatt_diff(false);
				oplus_ufcs_set_vbatt_diff(false);
			}
		} else {
			chg->check_battery_vol_count = 0;
		}
	}
}

static void oplus_chg_full_action(struct oplus_chg_chip *chip)
{
	charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Battery full !!\n");
	oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__FULL);
	if (chip->batt_full == false) {
		chip->tbatt_when_full = chip->temperature;
	}
	chip->batt_full = true;
	chip->total_time = 0;
	chip->in_rechging = false;
	chip->limits.vfloat_over_counts = 0;
	oplus_chg_check_rechg_status(chip);
	battery_notify_charge_batt_terminal_check(chip);
}

extern bool oplus_wpc_get_fw_updating(void);

bool oplus_chg_stats(void)
{
	if (!g_charger_chip) {
		charger_xlog_printk(CHG_LOG_CRTI, "g_charger_chip null\n");
		return false;
	}

	return g_charger_chip->chg_ops->check_chrdet_status();
}

static void oplus_chg_ufcs_switch_check(struct oplus_chg_chip *chip)
{
	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_NOT) {
		return;
	}

	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		charger_xlog_printk(CHG_LOG_CRTI, " short_c_battery, return\n");
		return;
	}
	if (chip->mmi_chg == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, " mmi_chg,return\n");
		return;
	}
	if (chip->balancing_bat_stop_chg == 1 || chip->balancing_bat_stop_fastchg == 1) {
		charger_xlog_printk(CHG_LOG_CRTI, " balancing_bat,return\n");
		return;
	}
	if (chip->allow_swtich_to_fastchg == false) {
		charger_xlog_printk(CHG_LOG_CRTI, " allow_swtich_to_fastchg == 0\n");
		return;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		charger_xlog_printk(CHG_LOG_CRTI, "non authenticate or hmac\n");
		return;
	}

	if (chip->notify_flag == NOTIFY_BAT_OVER_VOL) {
		charger_xlog_printk(CHG_LOG_CRTI, " battery over voltage\n");
		return;
	}
#ifdef SUPPORT_WPC
	if (chip->wireless_support && (oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present())) {
		charger_xlog_printk(CHG_LOG_CRTI, "is in WPC, switch return\n");
		return;
	}
#endif

	if (chip->vbatt_num == DOUBLE_BATTERY) {
		if (oplus_gauge_afi_update_done() == false) {
			chg_err("zy gauge afi_update_done ing...\n");
			return;
		}
	}
	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP || oplus_vooc_get_fastchg_started() ||
	    oplus_vooc_get_fastchg_dummy_started() || oplus_vooc_get_fastchg_to_normal() ||
	    oplus_vooc_get_fastchg_to_warm()) {
		chg_err("type or fast charing\n");
		return;
	}
	if (!oplus_is_ufcs_charging() && !oplus_ufcs_get_force_check_status()) {
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_chg_reset_adapter();
		} else if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			   oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			oplus_chg_set_chargerid_switch_val(1);
			oplus_vooc_switch_mode(VOOC_CHARGER_MODE);
			oplus_voocphy_reset_voocphy();
		}
		if (!oplus_ufcs_force_ufcs_check()) {
			oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
		}
	}
	oplus_ufcs_restart_check();
}

static void oplus_chg_switch(struct oplus_chg_chip *chip)
{
	oplus_chg_get_chargerid_voltage(chip);
	oplus_chg_ufcs_switch_check(chip);
	oplus_chg_pps_config(chip);
	oplus_chg_fast_switch_check(chip);
	oplus_chg_chargerid_switch_check(chip);
}

#define UNKNOW_TYPE_DETECT_RETRY_TIME 2
void oplus_charger_detect_check(struct oplus_chg_chip *chip)
{
	static bool charger_resumed = true;
	static int unknow_type_check = 0;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	static int charger_flag = 0;
#endif
	if (chip->chg_ops->check_chrdet_status()) {
		oplus_chg_set_awake(chip, true);
		if (chip->wireless_support && (oplus_wpc_get_fw_updating() || oplus_chg_is_wls_fw_upgrading())) {
			return;
		}

		if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			noplug_temperature = oplus_get_report_batt_temp();
			noplug_batt_volt_max = chip->batt_volt_max;
			noplug_batt_volt_min = chip->batt_volt_min;
			oplus_chg_variables_reset(chip, true);
			if (oplus_switching_support_parallel_chg()) {
				parallel_chg_check_balance_bat_status();
			}
#ifdef CONFIG_OPLUS_CHARGER_MTK
			if (is_meta_mode() == true) {
				chip->charger_type = POWER_SUPPLY_TYPE_USB;
				chip->real_charger_type = POWER_SUPPLY_TYPE_USB;
			} else {
				chip->charger_type = chip->chg_ops->get_charger_type();
				if (chip->chg_ops->get_real_charger_type) {
					chip->real_charger_type = chip->chg_ops->get_real_charger_type();
				}
			}
			if ((chip->chg_ops->usb_connect) && (chip->charger_type == POWER_SUPPLY_TYPE_USB ||
							     chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP)) {
				chip->chg_ops->usb_connect();
				charger_flag = 1;
			}
#else
			chip->charger_type = chip->chg_ops->get_charger_type();
			if (chip->chg_ops->get_real_charger_type) {
				chip->real_charger_type = chip->chg_ops->get_real_charger_type();
			}
#endif
			charger_xlog_printk(CHG_LOG_CRTI, "Charger in 1 charger_type=%d, unknow_type_check[%d]\n",
					    chip->charger_type, unknow_type_check);
			if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
				unknow_type_check++;
			} else {
				unknow_type_check = 0;
			}
			if (unknow_type_check >= UNKNOW_TYPE_DETECT_RETRY_TIME &&
			    ((oplus_vooc_get_fastchg_dummy_started() == true) ||
			     (oplus_vooc_get_fastchg_to_normal() == true) ||
			     (oplus_vooc_get_fastchg_to_warm() == true) || (oplus_vooc_get_btb_temp_over() == true))) {
				chip->charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			}

			if (oplus_vooc_get_fastchg_to_normal() == true || oplus_vooc_get_fastchg_to_warm() == true ||
			    oplus_pps_get_pps_mos_started() == true || oplus_ufcs_get_ufcs_mos_started() == true) {
				charger_xlog_printk(CHG_LOG_CRTI,
						    "fast_to_normal or to_warm 1,don't turn on charge here\n");
				if (oplus_vooc_get_reset_adapter_st()) {
					oplus_chg_suspend_charger();
					msleep(1000);
					oplus_chg_unsuspend_charger();
				}
				oplus_chg_switch(chip);
			} else if (oplus_vooc_get_fastchg_dummy_started() == true &&
				   oplus_vooc_get_reset_adapter_st() == true) {
				charger_xlog_printk(CHG_LOG_CRTI, "dummy_start is true,don't turn on charge here\n");
				oplus_chg_turn_on_charging(chip);
				oplus_chg_switch(chip);
			} else if (oplus_wpc_get_wireless_charge_start() == false && !oplus_chg_is_wls_present()) {
				charger_xlog_printk(CHG_LOG_CRTI, "oplus_wpc_get_wireless_charge_start == false\n");
				charger_resumed = chip->chg_ops->check_charger_resume();
				oplus_chg_turn_on_charging(chip);
				oplus_chg_switch(chip);
			} else {
				oplus_chg_switch(chip);
			}
			if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL &&
			    oplus_support_normal_batt_spec_check()) {
				chip->batt_target_curr = 0;
				chip->pre_charging_current = 0;
				oplus_chg_parellel_variables_reset();
				schedule_delayed_work(&chip->parallel_batt_chg_check_work,
						      OPLUS_CHG_UPDATE_INTERVAL(PARALLEL_CHECK_TIME));
			}
		} else {
			if (oplus_vooc_get_fastchg_to_normal() == true || oplus_vooc_get_fastchg_to_warm() == true ||
			    oplus_pps_get_pps_mos_started() == true || oplus_ufcs_get_ufcs_mos_started() == true) {
				charger_xlog_printk(CHG_LOG_CRTI,
						    "fast_to_normal or to_warm 2,don't turn on charge here\n");
				if (oplus_vooc_get_reset_adapter_st()) {
					oplus_chg_unsuspend_charger();
				}
				oplus_chg_switch(chip);
			} else if (oplus_vooc_get_fastchg_started() == false && charger_resumed == false) {
				charger_resumed = chip->chg_ops->check_charger_resume();
				if (oplus_vooc_get_reply_bits() == 7) {
					oplus_chg_switch(chip);
					oplus_chg_turn_on_charging(chip);
				} else {
					oplus_chg_turn_on_charging(chip);
					oplus_chg_switch(chip);
				}

			} else {
				oplus_chg_switch(chip);
			}
		}
		if ((chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP) &&
		    chip->internal_gauge_with_asic == 1 && mcu_status == 0) {
			if (true != opchg_get_mcu_update_state()) {
				oplus_vooc_reset_mcu();
				mcu_status = 1;
			}
		}
	} else {
		if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL) {
			parallel_chg_check_balance_bat_status();
			if (oplus_support_normal_batt_spec_check()) {
				chip->batt_target_curr = 0;
				chip->pre_charging_current = 0;
				oplus_chg_parellel_variables_reset();
				cancel_delayed_work_sync(&chip->parallel_batt_chg_check_work);
			}
		}

		unknow_type_check = 0;
		oplus_chg_variables_reset(chip, false);
		if (oplus_switching_support_parallel_chg()) {
			parallel_chg_check_balance_bat_status();
		}
		if (!chip->mmi_fastchg) {
			oplus_mmi_fastchg_in(chip);
		}
		oplus_gauge_set_batt_full(false);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if (chip->chg_ops->usb_disconnect && charger_flag == 1) {
			chip->chg_ops->usb_disconnect();
			charger_flag = 0;
		}
#endif
		if (chip->chg_ops->get_charging_enable() == true) {
			oplus_chg_turn_off_charging(chip);
		}
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if (chip->boot_mode != KERNEL_POWER_OFF_CHARGING_BOOT) {
			oplus_chg_set_awake(chip, false);
		}
#else
		oplus_chg_set_awake(chip, false);
#endif

		if (chip->internal_gauge_with_asic == 1 && mcu_status == 1) {
			if (true != opchg_get_mcu_update_state()) {
				if (!oplus_vooc_get_fastchg_started() && !oplus_vooc_get_fastchg_dummy_started() &&
				    !oplus_vooc_get_fastchg_to_normal() && !oplus_vooc_get_fastchg_to_warm()) {
					pr_info("%s delay800ms to set mcu sleep\n", __func__);
					oplus_vooc_set_mcu_sleep();
					mcu_status = 0;
				}
			}
		}
	}
}
static void oplus_get_smooth_soc_switch(struct oplus_chg_chip *chip)
{
	/*
	char *substr;
	static int save_num = -1;

	if (save_num == -1) {
		substr = strstr(saved_command_line, "smooth_soc_switch=");
		if(NULL == substr) {
			save_num = 0;
		} else {
			substr += strlen("smooth_soc_switch=");
			if (strncmp(substr, "1", 1) == 0) {
				save_num = 1;
			} else
				save_num = 0;
		}
	}

	chip->smooth_switch = save_num;
	if(chip->vbatt_num == 1)
		chip->smooth_switch = 1;
	else
		chip->smooth_switch = 0;*/
	chg_debug("smooth_switch = %d\n", chip->smooth_switch);
}

bool oplus_check_afi_update_condition(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int chg_fast_type = 0;
	if (!chip) {
		return false;
	}

	chg_fast_type = oplus_vooc_get_fast_chg_type();
	oplus_vooc_print_log();

	if (chip->charger_exist) {
		if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			return false;
		}

		if (CHARGER_SUBTYPE_FASTCHG_VOOC == chg_fast_type) {
			chg_err(" true 1: vooc\n");
			return true;
		} else {
			if (chg_fast_type) {
				if ((oplus_vooc_get_fastchg_to_normal() || oplus_vooc_get_fastchg_to_warm() ||
				     oplus_vooc_get_fastchg_dummy_started())) {
					if ((chip->fastchg_to_ffc == true && chip->fastchg_ffc_status) /* ffc */
					    || chip->batt_full) { /* full */
						chg_err(" true 2: svooc\n");
						return true;
					} else {
						return false;
					}
				}
				return false;
			} else { /* normal charger or unkown */
				chg_err(" true 3: normal charger or others unkown\n");
				return true;
			}
		}
	}
	return false;
}

#define RETRY_COUNTS 60
static void oplus_chg_get_battery_data(struct oplus_chg_chip *chip)
{
	static int ui_soc_cp_flag = 0;
	static int soc_load = 0;
	int remain_100_thresh = 97;
	static int retry_counts = 0;
	int plat_volt = 0;
	int gauge_volt = 0;
	int cp_vbat = 0;
	u8 cp_adc_reg = 0;

	oplus_chg_get_props_from_adsp_by_buffer();
	if (oplus_switching_support_parallel_chg()) {
		chip->sub_batt_volt = oplus_gauge_get_sub_batt_mvolts();
		chip->sub_batt_icharging = oplus_gauge_get_sub_batt_current();
		chip->sub_batt_soc = oplus_gauge_get_sub_batt_soc();
		chip->main_batt_soc = oplus_gauge_get_main_batt_soc();
		chip->sub_batt_temperature = oplus_gauge_get_sub_batt_temperature();
		oplus_chg_check_is_soc_gap_big(chip->main_batt_soc, chip->sub_batt_soc);
		chg_err("sub_batt volt:%d, current:%d, soc:%d, temperature:%d\n", chip->sub_batt_volt,
			chip->sub_batt_icharging, chip->sub_batt_soc, chip->sub_batt_temperature);
	}
	if (oplus_chg_get_voocphy_support() == NO_VOOCPHY && oplus_vooc_get_fastchg_started() == true) {
		if (oplus_plat_gauge_is_support() && chip->mmi_chg != 0 &&
		    (mcu_status == 1 && oplus_vooc_get_reset_gpio_status() == 1)) {
			plat_volt = oplus_gauge_get_plat_batt_mvolts();
			gauge_volt = oplus_gauge_get_prev_batt_mvolts();
			if (plat_volt >= chip->limits.vbatt_hv_thr || plat_volt <= 0 ||
			    abs(plat_volt - gauge_volt) > 800) {
				chg_err("error asic volt plat_volt[%d] gauge_volt[%d]\n", plat_volt, gauge_volt);
				chip->batt_volt = gauge_volt;
			} else {
				chip->batt_volt = plat_volt;
			}
		} else {
			chip->batt_volt = oplus_gauge_get_prev_batt_mvolts();
		}
		chip->batt_volt_max = oplus_gauge_get_prev_batt_mvolts_2cell_max();
		chip->batt_volt_min = oplus_gauge_get_prev_batt_mvolts_2cell_min();
		chip->icharging = oplus_gauge_get_prev_batt_current();
		chip->temperature = oplus_chg_match_temp_for_chging();
		chip->soc = oplus_gauge_get_prev_batt_soc();
		chip->batt_rm = oplus_gauge_get_prev_remaining_capacity() * chip->vbatt_num;
	} else {
		if (oplus_plat_gauge_is_support() && chip->mmi_chg != 0 &&
		    (mcu_status == 1 && oplus_vooc_get_reset_gpio_status() == 1)) {
			if (chip->charger_exist) {
				plat_volt = oplus_gauge_get_plat_batt_mvolts();
				gauge_volt = oplus_gauge_get_prev_batt_mvolts();
				if (plat_volt >= chip->limits.vbatt_hv_thr || plat_volt <= 0 ||
				    abs(plat_volt - gauge_volt) > 500) {
					chg_err("error asic volt plat_volt[%d] gauge_volt[%d]\n", plat_volt,
						gauge_volt);
					chip->batt_volt = gauge_volt;
				} else {
					chip->batt_volt = plat_volt;
				}
			} else {
				chip->batt_volt = oplus_gauge_get_batt_mvolts();
			}
		} else {
			chip->batt_volt = oplus_gauge_get_batt_mvolts();
		}
		chip->batt_volt_max = oplus_gauge_get_batt_mvolts_2cell_max();
		chip->batt_volt_min = oplus_gauge_get_batt_mvolts_2cell_min();
		chip->icharging = oplus_gauge_get_batt_current();
		chip->temperature = oplus_chg_match_temp_for_chging();
		if (!(chip->nightstandby_support)) {
			chip->soc = oplus_gauge_get_batt_soc();
		} else {
			if (get_soc_feature() && !chip->charger_exist) {
				chip->modify_soc = oplus_gauge_get_batt_soc();
				chg_err("get_battery_data [soc_ajust_feature]:chip->modify_soc[%d]\n",
					chip->modify_soc);
			} else {
				chip->soc = oplus_gauge_get_batt_soc();
				chg_err("get_battery_data [soc_ajust_feature]:chip->soc[%d]\n", chip->soc);
			}
		}
		chip->batt_fcc = oplus_gauge_get_batt_fcc() * chip->vbatt_num;
		chip->batt_cc = oplus_gauge_get_batt_cc();
		chip->batt_soh = oplus_gauge_get_batt_soh();
		chip->batt_rm = oplus_gauge_get_remaining_capacity() * chip->vbatt_num;
		oplus_midas_chg_processing(chip);
	}

	if (chip->nightstandby_support) {
		if (soc_ajusting) {
			set_soc_feature();
		}
	}

	if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
	    oplus_voocphy_get_external_gauge_support() == 0) {
		if (oplus_vooc_get_fastchg_started() == true) {
			gauge_volt = chip->batt_volt;

			cp_vbat = oplus_voocphy_get_cp_vbat();
			if (cp_vbat >= chip->limits.vbatt_hv_thr || cp_vbat <= 0 || abs(cp_vbat - gauge_volt) > 800) {
				chg_err("error voocphy cp volt cp_volt[%d] gauge_volt[%d]\n", cp_vbat, gauge_volt);
				chip->batt_volt = gauge_volt;
				chg_err("use pmic_vbat as chip vbat!\n");
			} else {
				chip->batt_volt = cp_vbat;
				chg_err("use cp_vbat as chip vbat!\n");
			}
		} else {
			if (chip->charger_exist) {
				gauge_volt = chip->batt_volt;

				oplus_voocphy_get_adc_enable(&cp_adc_reg);
				if (cp_adc_reg == 0)
					oplus_voocphy_set_adc_enable(true);

				cp_vbat = oplus_voocphy_get_cp_vbat();
				if (cp_vbat >= chip->limits.vbatt_hv_thr || cp_vbat <= 0 ||
				    abs(cp_vbat - gauge_volt) > 800) {
					chg_err("error voocphy cp volt cp_volt[%d] gauge_volt[%d]\n", cp_vbat,
						gauge_volt);
					chip->batt_volt = gauge_volt;
					chg_err("use pmic_vbat as chip vbat!\n");
				} else {
					chip->batt_volt = cp_vbat;
					chg_err("use cp_vbat as chip vbat!\n");
				}
			} else {
				chip->batt_volt = oplus_gauge_get_batt_mvolts();

				oplus_voocphy_get_adc_enable(&cp_adc_reg);
				if (cp_adc_reg == 1)
					oplus_voocphy_set_adc_enable(false);
			}
		}
	}

	chip->charger_cycle = oplus_chg_get_charger_cycle();
	if (chip->vbatt_num == DOUBLE_BATTERY) {
		if (oplus_check_afi_update_condition()) {
			oplus_gauge_protect_check();
		}
	}
	if (chgr_dbg_vchg != 0) {
		chip->charger_volt = chgr_dbg_vchg;
	} else {
		if (oplus_vooc_get_allow_reading() == true) {
			chip->charger_volt = chip->chg_ops->get_charger_volt();
			if (chip->charger_volt > 3000 && chip->charger_exist) {
				if (chip->usbtemp_wq_init_finished) {
					chip->usbtemp_check = true;
					if (chip->support_usbtemp_protect_v2)
						wake_up_interruptible(&chip->oplus_usbtemp_wq_new_method);
					else
						wake_up_interruptible(&chip->oplus_usbtemp_wq);
				}
			} else {
				if ((oplus_voocphy_get_bidirect_cp_support() && oplus_vooc_get_fastchg_started())) {
					if (chip->usbtemp_wq_init_finished) {
						chip->usbtemp_check = true;
						if (chip->support_usbtemp_protect_v2)
							wake_up_interruptible(&chip->oplus_usbtemp_wq_new_method);
						else
							wake_up_interruptible(&chip->oplus_usbtemp_wq);
					}
				} else {
					chip->usbtemp_check = false;
				}
			}
		}
	}
	if (chip->charger_exist) {
		wake_up_interruptible(&chip->oplus_bcc_wq);
	}
	if ((chip->chg_ops->get_charger_current && oplus_vooc_get_allow_reading() == true) &&
	    (chip->chg_ops->get_charging_enable && (chip->chg_ops->get_charging_enable() == true))) {
		chip->ibus = chip->chg_ops->get_charger_current();
	} else {
		chip->ibus = -1;
	}

	if (!chip->is_gauge_ready && chip->soc >= 0 && chip->soc <= OPLUS_FULL_SOC)
		chip->is_gauge_ready = true;

	if (chip->rsd.smooth_switch_v2) {
		oplus_chg_smooth_to_soc_v2(chip, false);
	} else if (chip->smooth_switch) {
		oplus_chg_smooth_to_soc(chip);
	} else {
		chip->smooth_soc = chip->soc;
	}
	if (!chip->healthd_ready && chip->smooth_switch == 1 && retry_counts < RETRY_COUNTS) {
		chg_err(" test gauge soc[%d] \n", chip->soc);
		chip->soc = -1;
	}

	if (ui_soc_cp_flag == 0) {
		if ((chip->soc < 0 || chip->soc > 100) && retry_counts < RETRY_COUNTS) {
			charger_xlog_printk(CHG_LOG_CRTI,
					    "[Battery]oplus_chg_get_battery_data, chip->soc[%d],retry_counts[%d]\n",
					    chip->soc, retry_counts);
			retry_counts++;
			chip->soc = 50;
			goto next;
		}
		chip->is_gauge_ready = true;
		if (RETRY_COUNTS == retry_counts) {
			if (chip->rsd.smooth_switch_v2)
				oplus_chg_smooth_to_soc_v2(chip, false);
			else if (chip->smooth_switch)
				oplus_chg_smooth_to_soc(chip);
			else
				chip->smooth_soc = chip->soc;
		}
		ui_soc_cp_flag = 1;
		chg_err("[rtc=%d shutdown=%d soc=%d]\n", chip->chg_ops->get_rtc_soc(), chip->shutdown_uisoc, chip->soc);
		if (chip->chg_ops->get_rtc_soc() > 100 || (chip->chg_ops->get_rtc_soc() <= 0)) {
			if (chip->shutdown_uisoc >= 0 && chip->shutdown_uisoc <= OPLUS_FULL_SOC &&
			    abs(chip->shutdown_uisoc - chip->smooth_soc) <= 5) {
				soc_load = chip->shutdown_uisoc;
			} else {
				if (chip->rsd.smooth_switch_v2 && chip->rsd.is_soc_jump_range)
					soc_load = (chip->soc > (chip->smooth_soc - 1)) ? chip->soc : (chip->smooth_soc - 1);
				else
					soc_load = chip->smooth_soc;
			}
		} else {
			soc_load = chip->chg_ops->get_rtc_soc();
		}

		chip->soc_load = soc_load;
		if (chip->smooth_switch)
			chip->smooth_soc = soc_load;
		if ((chip->soc < 0 || chip->soc > 100) && soc_load > 0 && soc_load <= 100) {
			chip->soc = soc_load;
		}
		if ((soc_load != 0) && ((abs(soc_load - chip->soc)) <= 20)) {
			if (chip->suspend_after_full && chip->external_gauge) {
				remain_100_thresh = 95;
			} else if (chip->suspend_after_full && !chip->external_gauge) {
				remain_100_thresh = 94;
			} else if (!chip->suspend_after_full && chip->external_gauge) {
				remain_100_thresh = 97;
			} else if (!chip->suspend_after_full && !chip->external_gauge) {
				remain_100_thresh = 95;
			} else {
				remain_100_thresh = 97;
			}
			if (chip->soc < soc_load && chip->smooth_switch == 1) {
				if (soc_load == 100 && chip->soc > remain_100_thresh) {
					chip->ui_soc = soc_load;
				} else {
					chip->ui_soc = soc_load - 1;
				}
			} else {
				chip->ui_soc = soc_load;
			}
		} else {
			if (chip->rsd.smooth_switch_v2 && chip->rsd.is_soc_jump_range)
				chip->ui_soc = (chip->soc > (chip->smooth_soc - 1)) ? chip->soc : (chip->smooth_soc - 1);
			else
				chip->ui_soc = chip->smooth_soc;
			if (!chip->external_gauge && soc_load == 0 && chip->soc < 5) {
				chip->ui_soc = 0;
			}

			if (chip->ui_soc == 0) {
				chip->batt_volt = oplus_gauge_get_batt_mvolts();
				if (chip->batt_volt > chip->vbatt_power_off) {
					chg_err(" batt_mvolt[%d] > vbatt_power_off [%d], set ui_soc as 1.\n",
						chip->batt_volt, chip->vbatt_power_off);
					chip->ui_soc = 1;
				}
			}
		}
		chg_err("[soc ui_soc soc_load smooth_soc smooth_switch] = [%d %d %d %d %d]\n", chip->soc, chip->ui_soc,
			chip->soc_load, chip->smooth_soc, chip->smooth_switch);
	}
	if (chip->charger_exist && oplus_chg_show_vooc_logo_ornot()) {
		quick_mode_check();
	} else if (chip->quick_mode_need_update == true) {
		chip->quick_mode_time = current_kernel_time();
		chip->quick_mode_stop_time = chip->quick_mode_time.tv_sec;
		chip->quick_mode_stop_temp = chip->temperature;
		chip->quick_mode_stop_soc = chip->soc;
		chip->quick_mode_need_update = false;
	}
next:
	return;
}

/*need to extend it*/
static void oplus_chg_set_aicl_point(struct oplus_chg_chip *chip)
{
	if (oplus_vooc_get_allow_reading() == true) {
		chip->chg_ops->set_aicl_point(chip->batt_volt);
	}
}

#define AICL_DELAY_15MIN 180
static void oplus_chg_check_aicl_input_limit(struct oplus_chg_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (chip->charging_state == CHARGING_STATUS_FAIL || chip->batt_full == true ||
	    ((chip->tbatt_status != BATTERY_STATUS__NORMAL) &&
	     (chip->tbatt_status != BATTERY_STATUS__LITTLE_COOL_TEMP)) ||
	    chip->ui_soc > 85 || oplus_vooc_get_fastchg_started() == true) {
		aicl_delay_count = 0;
		return;
	}
	if (aicl_delay_count > AICL_DELAY_15MIN) {
		aicl_delay_count = 0;
		if (oplus_vooc_get_allow_reading() == true) {
			if (chip->dual_charger_support) {
				chip->slave_charger_enable = false;
				oplus_chg_set_charging_current(chip);
			}
			charger_xlog_printk(CHG_LOG_CRTI, "15 minutes aicl\n");
			oplus_chg_set_input_current_limit(chip);
		}
	} else {
		aicl_delay_count++;
	}
#else
	if (chip->charging_state == CHARGING_STATUS_FAIL || chip->batt_full == true ||
	    ((chip->tbatt_status != BATTERY_STATUS__NORMAL) &&
	     (chip->tbatt_status != BATTERY_STATUS__LITTLE_COOL_TEMP)) ||
	    ((chip->ui_soc > 85) && (chip->pmic_spmi.aicl_suspend == false)) ||
	    oplus_vooc_get_fastchg_started() == true) {
		aicl_delay_count = 0;
		return;
	}
	if (aicl_delay_count > AICL_DELAY_15MIN) {
		aicl_delay_count = 0;
		if (oplus_vooc_get_allow_reading() == true) {
			charger_xlog_printk(CHG_LOG_CRTI, "15 minutes aicl\n");
			oplus_chg_set_input_current_limit(chip);
		}
	} else if (chip->pmic_spmi.aicl_suspend == true && chip->charger_volt > 4450 && chip->charger_volt < 5800) {
		aicl_delay_count = 0;
		if (oplus_vooc_get_allow_reading() == true) {
			chip->chg_ops->rerun_aicl();
			oplus_chg_set_input_current_limit(chip);
		}
		charger_xlog_printk(CHG_LOG_CRTI, "chip->charger_volt=%d\n", chip->charger_volt);
	} else {
		aicl_delay_count++;
	}
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB || chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
		chip->pmic_spmi.usb_hc_count++;
		if (chip->pmic_spmi.usb_hc_count >= 3) {
			chip->pmic_spmi.usb_hc_mode = true;
			chip->pmic_spmi.usb_hc_count = 3;
		}
	}
	if (oplus_vooc_get_allow_reading() == true && chip->pmic_spmi.usb_hc_mode && !chip->pmic_spmi.hc_mode_flag) {
		oplus_chg_set_input_current_limit(chip);
		chip->pmic_spmi.hc_mode_flag = true;
	}
#endif
}

static void oplus_chg_aicl_check(struct oplus_chg_chip *chip)
{
	if (oplus_vooc_get_fastchg_started() == false) {
		oplus_chg_set_aicl_point(chip);
		oplus_chg_check_aicl_input_limit(chip);
	}
}

static void oplus_chg_protection_check(struct oplus_chg_chip *chip)
{
	if (false == oplus_chg_check_tbatt_is_good(chip)) {
		chg_err("oplus_chg_check_tbatt_is_good func ,false!\n");
		if (oplus_get_flash_screen_ctrl() == true) {
			if (chip->batt_volt > 4350) {
				if (chip->led_on) {
					oplus_chg_suspend_charger();
					chip->limits.recharge_mv = 60;
				} else {
					chip->chg_ops->charger_unsuspend();
					chip->limits.recharge_mv = 100;
				}
			} else {
				if (chip->batt_volt < 4300) {
					chip->chg_ops->charger_unsuspend();
				}
			}
		}
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__BATTTEMP_ABNORMAL);
	} else {
		if ((chip->stop_voter & CHG_STOP_VOTER__BATTTEMP_ABNORMAL) == CHG_STOP_VOTER__BATTTEMP_ABNORMAL) {
			charger_xlog_printk(CHG_LOG_CRTI, "oplus_chg_check_tbatt_is_good func ,true! To Normal\n");
			if (oplus_get_flash_screen_ctrl() == true) {
				chip->chg_ops->charger_unsuspend();
			}
			oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__BATTTEMP_ABNORMAL);
		}
	}
	if (false == oplus_chg_check_vchg_is_good(chip)) {
		chg_err("oplus_chg_check_vchg_is_good func ,false!\n");
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__VCHG_ABNORMAL);
	} else {
		if ((chip->stop_voter & CHG_STOP_VOTER__VCHG_ABNORMAL) == CHG_STOP_VOTER__VCHG_ABNORMAL) {
			charger_xlog_printk(CHG_LOG_CRTI, "oplus_chg_check_vchg_is_good func ,true! To Normal\n");
			oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__VCHG_ABNORMAL);
		}
	}
#ifdef FEATURE_VBAT_PROTECT
	if (false == oplus_chg_check_vbatt_is_good(chip)) {
		chg_err("oplus_chg_check_vbatt_is_good func ,false!\n");
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__VBAT_TOO_HIGH);
	}
#endif
	if (false == oplus_chg_check_time_is_good(chip)) {
		chg_err("oplus_chg_check_time_is_good func ,false!\n");
		oplus_chg_voter_charging_stop(chip, CHG_STOP_VOTER__MAX_CHGING_TIME);
	}
	oplus_chg_check_vbatt_higher_than_4180mv(chip);
	oplus_chg_vfloat_over_check(chip);
	oplus_chg_check_ffc_temp_status(chip);
	if (chip->chg_ctrl_by_lcd) {
		oplus_chg_check_tled_status(chip);
		oplus_chg_check_led_on_ichging(chip);
	}
	if (chip->chg_ctrl_by_camera) {
		oplus_chg_check_camera_on_ichging(chip);
	}
	if (chip->chg_ctrl_by_calling) {
		oplus_chg_check_calling_on_ichging(chip);
	}
	if (chip->chg_ctrl_by_vooc) {
		oplus_chg_check_vooc_temp_status(chip);
		oplus_chg_check_vooc_ichging(chip);
	}
	if (chip->cool_down_done) {
		oplus_chg_check_cool_down_ichging(chip);
	}
	if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		if (chip->chg_ops->adsp_voocphy_set_match_temp) {
			chip->chg_ops->adsp_voocphy_set_match_temp();
		}
	}
	oplus_chg_check_tbatt_cold_status(chip);
}

static void battery_notify_tbat_check(struct oplus_chg_chip *chip)
{
	static int count_removed = 0;
	static int count_high = 0;
	if (BATTERY_STATUS__HIGH_TEMP == chip->tbatt_status) {
		count_high++;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d), BATTERY_STATUS__HIGH_TEMP count[%d]\n",
				    chip->temperature, count_high);
		if (chip->charger_exist && count_high > 10) {
			count_high = 11;
			chip->notify_code |= 1 << NOTIFY_BAT_OVER_TEMP;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d) > 55'C\n", chip->temperature);
		}
	} else {
		count_high = 0;
	}
	if (BATTERY_STATUS__LOW_TEMP == chip->tbatt_status) {
		if (chip->charger_exist) {
			chip->notify_code |= 1 << NOTIFY_BAT_LOW_TEMP;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d) < -10'C\n", chip->temperature);
		}
	}
	if (BATTERY_STATUS__REMOVED == chip->tbatt_status) {
		count_removed++;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d), BATTERY_STATUS__REMOVED count[%d]\n",
				    chip->temperature, count_removed);
		if (count_removed > 10) {
			count_removed = 11;
			chip->notify_code |= 1 << NOTIFY_BAT_NOT_CONNECT;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_temp(%d) < -19'C\n", chip->temperature);
		}
	} else {
		count_removed = 0;
	}
}

static void battery_notify_authenticate_check(struct oplus_chg_chip *chip)
{
	if (!chip->authenticate) {
		chip->notify_code |= 1 << NOTIFY_BAT_NOT_CONNECT;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_authenticate is false!\n");
	}
}
static void battery_notify_hmac_check(struct oplus_chg_chip *chip)
{
	if (!chip->hmac) {
		chip->notify_code |= 1 << NOTIFY_BAT_FULL_THIRD_BATTERY;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] bat_hmac is false!\n");
	}
}

static void battery_notify_vcharger_check(struct oplus_chg_chip *chip)
{
	if (CHARGER_STATUS__VOL_HIGH == chip->vchg_status) {
		chip->notify_code |= 1 << NOTIFY_CHARGER_OVER_VOL;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] check_charger_off_vol(%d) > 5800mV\n", chip->charger_volt);
	}

	if (CHARGER_STATUS__VOL_LOW == chip->vchg_status) {
		chip->notify_code |= 1 << NOTIFY_CHARGER_LOW_VOL;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] check_charger_off_vol(%d) < 3400mV\n", chip->charger_volt);
	}
}

static void battery_notify_vbat_check(struct oplus_chg_chip *chip)
{
	static int count = 0;

	if (true == chip->vbatt_over) {
		count++;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Battery is over VOL, count[%d] \n", count);
		if (count > 10) {
			count = 11;
			chip->notify_code |= 1 << NOTIFY_BAT_OVER_VOL;
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Battery is over VOL! Notify \n");
		}
	} else {
		count = 0;
		if ((chip->batt_full) && (chip->charger_exist)) {
			if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP && chip->ui_soc != 100) {
				chip->notify_code |= 1 << NOTIFY_BAT_FULL_PRE_HIGH_TEMP;
			} else if ((chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) && (chip->ui_soc != 100)) {
				chip->notify_code |= 1 << NOTIFY_BAT_FULL_PRE_LOW_TEMP;
			} else if (!chip->authenticate) {
				chip->notify_code |= 1 << NOTIFY_BAT_NOT_CONNECT;
			} else if (!chip->hmac) {
				chip->notify_code |= 1 << NOTIFY_BAT_FULL_THIRD_BATTERY;
			} else {
				if (chip->ui_soc == 100) {
					chip->notify_code |= 1 << NOTIFY_BAT_FULL;
				}
			}
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] FULL,tbatt_status:%d,notify_code:%d\n",
					    chip->tbatt_status, chip->notify_code);
		}
	}
}

static void battery_notify_max_charging_time_check(struct oplus_chg_chip *chip)
{
	if (true == chip->chging_over_time) {
		chip->notify_code |= 1 << NOTIFY_CHGING_OVERTIME;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Charging is OverTime!Notify \n");
	}
}

static void battery_notify_short_c_battery_check(struct oplus_chg_chip *chip)
{
	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__CV_ERR_CODE1) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_CV_ERR_CODE1;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] battery is short circuit! err_code1!\n");
	}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__FULL_ERR_CODE2) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_FULL_ERR_CODE2;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] battery is short circuit! err_code2!\n");
	}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__FULL_ERR_CODE3) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_FULL_ERR_CODE3;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] battery is short circuit! err_code3!\n");
	}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__DYNAMIC_ERR_CODE4) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE4;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] battery is short circuit! err_code4!\n");
	}

	if (chip->short_c_batt.err_code == SHORT_C_BATT_STATUS__DYNAMIC_ERR_CODE5) {
		chip->notify_code |= 1 << NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE5;
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] battery is short circuit! err_code5!\n");
	}
}

static void battery_notify_flag_check(struct oplus_chg_chip *chip)
{
	if (chip->notify_code & (1 << NOTIFY_CHGING_OVERTIME)) {
		chip->notify_flag = NOTIFY_CHGING_OVERTIME;
	} else if (chip->notify_code & (1 << NOTIFY_CHARGER_OVER_VOL)) {
		chip->notify_flag = NOTIFY_CHARGER_OVER_VOL;
	} else if (chip->notify_code & (1 << NOTIFY_CHARGER_LOW_VOL)) {
		chip->notify_flag = NOTIFY_CHARGER_LOW_VOL;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_OVER_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_OVER_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_LOW_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_LOW_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_NOT_CONNECT)) {
		chip->notify_flag = NOTIFY_BAT_NOT_CONNECT;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL_THIRD_BATTERY)) {
		chip->notify_flag = NOTIFY_BAT_FULL_THIRD_BATTERY;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_OVER_VOL)) {
		chip->notify_flag = NOTIFY_BAT_OVER_VOL;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL_PRE_HIGH_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_FULL_PRE_HIGH_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL_PRE_LOW_TEMP)) {
		chip->notify_flag = NOTIFY_BAT_FULL_PRE_LOW_TEMP;
	} else if (chip->notify_code & (1 << NOTIFY_BAT_FULL)) {
		chip->notify_flag = NOTIFY_BAT_FULL;
	} else {
		chip->notify_flag = 0;
	}
}

static void battery_notify_charge_terminal_check(struct oplus_chg_chip *chip)
{
	if (chip->batt_full == true && chip->charger_exist == true) {
		chip->notify_code |= 1 << NOTIFY_CHARGER_TERMINAL;
	}
}

static void battery_notify_gauge_i2c_err_check(struct oplus_chg_chip *chip)
{
	if (oplus_gauge_get_i2c_err() > 0) {
		chip->notify_code |= 1 << NOTIFY_GAUGE_I2C_ERR;
	}

	oplus_gauge_clear_i2c_err();
}

static void battery_notify_charge_batt_terminal_check(struct oplus_chg_chip *chip)
{
	chip->notify_code |= 1 << NOTIFY_CHARGER_BATT_TERMINAL;
}

static void battery_notify_parallel_error_check(struct oplus_chg_chip *chip)
{
	if (chip->parallel_error_flag & REASON_SOC_NOT_FULL)
		chip->notify_code |= 1 << NOTIFY_FAST_CHG_END_ERROR;
	if (chip->parallel_error_flag & REASON_MOS_OPEN_ERROR)
		chip->notify_code |= 1 << NOTIFY_MOS_OPEN_ERROR;
	if (chip->parallel_error_flag & REASON_I2C_ERROR)
		chip->notify_code |= 1 << NOTIFY_GAUGE_I2C_ERR;
	if (chip->parallel_error_flag & (REASON_SOC_GAP_TOO_BIG | REASON_VBAT_GAP_BIG))
		chip->notify_code |= 1 << NOTIFY_CURRENT_UNBALANCE;
}

static void oplus_chg_battery_notify_check(struct oplus_chg_chip *chip)
{
	chip->notify_code = 0x0000;
	battery_notify_tbat_check(chip);
	battery_notify_authenticate_check(chip);
	battery_notify_hmac_check(chip);
	battery_notify_vcharger_check(chip);
	battery_notify_vbat_check(chip);
	battery_notify_max_charging_time_check(chip);
	battery_notify_short_c_battery_check(chip);
	battery_notify_charge_terminal_check(chip);
	battery_notify_gauge_i2c_err_check(chip);
	battery_notify_parallel_error_check(chip);
	battery_notify_flag_check(chip);
}

int oplus_chg_get_prop_batt_health(struct oplus_chg_chip *chip)
{
	int bat_health = POWER_SUPPLY_HEALTH_GOOD;
	bool vbatt_over = chip->vbatt_over;
	OPLUS_CHG_TBATT_STATUS tbatt_status = chip->tbatt_status;

	if (vbatt_over == true) {
		bat_health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if (tbatt_status == BATTERY_STATUS__REMOVED) {
		bat_health = POWER_SUPPLY_HEALTH_DEAD;
	} else if (tbatt_status == BATTERY_STATUS__HIGH_TEMP) {
		bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if (tbatt_status == BATTERY_STATUS__LOW_TEMP) {
		bat_health = POWER_SUPPLY_HEALTH_COLD;
	} else {
		bat_health = POWER_SUPPLY_HEALTH_GOOD;
	}
	return bat_health;
}

static bool oplus_chg_soc_reduce_slow_when_1(struct oplus_chg_chip *chip)
{
	static int reduce_count = 0;
	static bool uisoc_keep_1_recorded = false;
	int reduce_count_limit = 0;
	int vbat_keep_soc_1 = chip->vbatt_soc_1;

	if (chip->batt_exist == false) {
		return false;
	}
	if (chip->charger_exist) {
		reduce_count_limit = 12;
		if (chip->charger_type == POWER_SUPPLY_TYPE_USB) {
			vbat_keep_soc_1 = 3200;
		}
	} else {
		reduce_count_limit = 4;
	}
	if (chip->batt_volt_min < vbat_keep_soc_1 || chip->batt_volt_min < chip->debug_vbat_keep_soc_1) {
		reduce_count++;
	} else {
		reduce_count = 0;
	}
	charger_xlog_printk(CHG_LOG_CRTI, "batt_vol:%d, batt_volt_min:%d, reduce_count:%d, vbat_keep_soc_1[%d]\n",
			    chip->batt_volt, chip->batt_volt_min, reduce_count, vbat_keep_soc_1);
	if (reduce_count > reduce_count_limit) {
		if (!uisoc_keep_1_recorded) {
			uisoc_keep_1_recorded = true;
			oplus_chg_track_upload_vbatt_diff_over_info(chip, chip->batt_volt_max, chip->batt_volt_min,
								    chip->prop_status);
			oplus_chg_track_upload_uisoc_keep_1_t_info(chip);
		}
		reduce_count = reduce_count_limit + 1;
		return true;
	} else {
		uisoc_keep_1_recorded = false;
		return false;
	}
}

static bool oplus_chg_soc_reduce_to_shutdown(struct oplus_chg_chip *chip)
{
	static int reduce_count = 0;
	static int last_chg_exit = 0;
	int reduce_count_limit = 7;
	static bool uisoc_keep_1_recorded = false;

	if (chip->batt_exist == false) {
		return false;
	}

	if (chip->charger_exist != last_chg_exit)
		reduce_count = 0;

	if (chip->charger_exist) {
		last_chg_exit = 1;
		if (chip->batt_volt_min < chip->soc_to_0_withchg) {
			reduce_count++;
		} else {
			reduce_count = 0;
		}
	} else {
		last_chg_exit = 0;
		if (chip->batt_volt_min < chip->soc_to_0_withoutchg) {
			reduce_count++;
		} else {
			reduce_count = 0;
		}
	}

	charger_xlog_printk(CHG_LOG_CRTI, "batt_vol:%d, batt_volt_min:%d, reduce_count:%d, last_chg_exit:%d\n",
			    chip->batt_volt, chip->batt_volt_min, reduce_count, last_chg_exit);
	if (reduce_count > reduce_count_limit) {
		if (!uisoc_keep_1_recorded) {
			uisoc_keep_1_recorded = true;
			oplus_chg_track_upload_vbatt_diff_over_info(chip, chip->batt_volt_max, chip->batt_volt_min,
								    chip->prop_status);
			oplus_chg_track_upload_uisoc_keep_1_t_info(chip);
		}
		reduce_count = reduce_count_limit + 1;
		return true;
	} else {
		uisoc_keep_1_recorded = false;
		return false;
	}
}

#define SOC_SYNC_UP_RATE_10S 2
#define SOC_SYNC_UP_RATE_60S 12
#define SOC_SYNC_DOWN_RATE_300S 60
#define SOC_SYNC_DOWN_RATE_150S 30
#define SOC_SYNC_DOWN_RATE_120S 24
#define SOC_SYNC_DOWN_RATE_90S 18
#define SOC_SYNC_DOWN_RATE_80S 16
#define SOC_SYNC_DOWN_RATE_75S 15
#define SOC_SYNC_DOWN_RATE_70S 14
#define SOC_SYNC_DOWN_RATE_60S 12
#define SOC_SYNC_DOWN_RATE_50S 10
#define SOC_SYNC_DOWN_RATE_45S 9
#define SOC_SYNC_DOWN_RATE_40S 8
#define SOC_SYNC_DOWN_RATE_30S 6
#define SOC_SYNC_DOWN_RATE_15S 3
#define TEN_MINUTES 600
#define ONE_MINUTE 60
#define CHARGING_STATUS 1
#define DISCHARGING_STATUS 0

static void oplus_chg_smooth_to_soc(struct oplus_chg_chip *chip)
{
	static int time = 0;
	static int capacity = -1;
	static int smooth_diff = -1;
	static int soc_pre = -1;
	static int status = DISCHARGING_STATUS;
	pr_err("[oplus_chg_smooth_to_soc] enter the func");
	if (chip->charger_exist && chip->batt_exist && (CHARGING_STATUS_FAIL != chip->charging_state) &&
	    chip->mmi_chg && (chip->stop_chg == 1 || chip->charger_type == 5) && status == DISCHARGING_STATUS) {
		status = CHARGING_STATUS;
		time = 0;
		capacity = chip->batt_rm;
		soc_pre = chip->soc;
		smooth_diff = capacity / (2 * chip->soc - chip->smooth_soc);
	} else if (!(chip->charger_exist && chip->batt_exist && chip->mmi_chg &&
		     (chip->batt_full || CHARGING_STATUS_FAIL != chip->charging_state) &&
		     (chip->stop_chg == 1 || chip->charger_type == 5)) &&
		   status == CHARGING_STATUS) {
		status = DISCHARGING_STATUS;
		time = 0;
		capacity = -1;
		soc_pre = -1;
		smooth_diff = -1;
	}

	if (status == DISCHARGING_STATUS) {
		if (chip->soc >= 96 && chip->soc <= 100 && chip->ui_soc == 100) {
			chip->smooth_soc = 100;
		} else {
			if (chip->smooth_soc > chip->soc) {
				if (capacity == -1) {
					time = 0;
					capacity = chip->batt_rm;
					soc_pre = chip->soc;
					smooth_diff = capacity / chip->smooth_soc;
				}
				if ((capacity - chip->batt_rm) >= smooth_diff ||
				    ((chip->smooth_soc - chip->soc) > chip->smooth_to_soc_gap)) {
					chip->smooth_soc--;
					capacity = chip->batt_rm;
					smooth_diff = capacity / chip->smooth_soc;
				}
				if (chip->soc == 0) {
					time++;
					if (time >= SOC_SYNC_DOWN_RATE_15S) {
						chip->smooth_soc--;
						time = 0;
					}
				}
				pr_err("[oplus_chg_smooth_to_soc] smooth_soc[%d],capacity[%d],smooth_diff[%d]",
				       chip->smooth_soc, capacity, smooth_diff);
			} else {
				chip->smooth_soc = chip->soc;
			}
		}
	} else {
		if (chip->smooth_soc > chip->soc) {
			if (soc_pre < chip->soc && (chip->batt_rm - capacity) >= smooth_diff &&
			    chip->smooth_soc < 100) {
				chip->smooth_soc++;
				capacity = chip->batt_rm;
				soc_pre = chip->soc;
				smooth_diff = capacity / (2 * chip->soc - chip->smooth_soc);
			}
			if (chip->soc < soc_pre) {
				chip->smooth_soc--;
				soc_pre = chip->soc;
			}
			pr_err("[oplus_chg_smooth_to_soc]charging smooth_soc[%d],capacity[%d],smooth_diff[%d],soc_pre[%d]",
			       chip->smooth_soc, capacity, smooth_diff, soc_pre);
		} else {
			chip->smooth_soc = chip->soc;
		}
	}
}

static const int soc_jump_table[RESERVE_SOC_MAX + 1][RESERVE_SOC_MAX] = {
	{ -1, -1, -1, -1, -1 }, /* reserve 0 */
	{ 55, -1, -1, -1, -1 }, /* reserve 1 */
	{ 36, 71, -1, -1, -1 }, /* reserve 2 */
	{ 29, 53, 76, -1, -1 }, /* reserve 3 */
	{ 25, 43, 60, 78, -1 }, /* reserve 4 */
	{ 22, 36, 50, 64, 78 }, /* reserve 5 */
};
static void oplus_chg_smooth_to_soc_v2(struct oplus_chg_chip *chip, bool force)
{
	static int pre_soc = -EINVAL;
	int soc = chip->soc;
	int reserve_soc = chip->rsd.reserve_soc;
	int temp_soc = soc;
	int sum = 0, cnt, index, i;
	int valid_soc_cnt = 0;

	if (!chip->rsd.smooth_switch_v2)
		goto reserve_soc_error;

	if (reserve_soc < RESERVE_SOC_MIN || reserve_soc > RESERVE_SOC_MAX) {
		chg_err("reserve_soc %d invalid\n", reserve_soc);
		goto reserve_soc_error;
	}

	if (!chip->is_gauge_ready) {
		chg_err("gauge is not ready, waiting...\n");
		chip->smooth_soc = chip->soc;
		return;
	}

	if (force || pre_soc == -EINVAL) { /* force or first entry */
		chip->rsd.is_soc_jump_range = false;
		chip->rsd.smooth_soc_avg_cnt = SMOOTH_SOC_MIN_FIFO_LEN;
		for (i = 0; i < SMOOTH_SOC_MAX_FIFO_LEN; i++)
			chip->rsd.smooth_soc_fifo[i] = -EINVAL;
		chip->rsd.smooth_soc_index = 0;
	}

	for (i = reserve_soc - 1; i >= 0; i--) {
		if (soc_jump_table[reserve_soc][i] < 0) {
			chg_err("soc_jump_table invalid, please check it.\n");
			goto reserve_soc_error;
		}

		if (soc >= soc_jump_table[reserve_soc][i]) {
			temp_soc = soc + i + 1;
			break;
		}
	}

	temp_soc = (temp_soc <= OPLUS_FULL_SOC) ? temp_soc : OPLUS_FULL_SOC;
	chip->rsd.smooth_soc_fifo[chip->rsd.smooth_soc_index] = temp_soc;

	if (force || pre_soc != soc) {
		chip->rsd.is_soc_jump_range = false;
		if (abs(pre_soc - soc) > 1)
			chip->rsd.smooth_soc_avg_cnt = SMOOTH_SOC_MIN_FIFO_LEN;

		for (i = 0; i < reserve_soc; i++) {
			if (soc >= (soc_jump_table[reserve_soc][i] - SOC_JUMP_RANGE_VAL) &&
			    soc <= (soc_jump_table[reserve_soc][i] + SOC_JUMP_RANGE_VAL)) {
				chg_err("soc:%d index:%d soc_jump:%d\n", soc, i, soc_jump_table[reserve_soc][i]);
				chip->rsd.is_soc_jump_range = true;
				chip->rsd.smooth_soc_avg_cnt = SMOOTH_SOC_MAX_FIFO_LEN;
				break;
			}
		}
	}

	if (chip->rsd.smooth_soc_avg_cnt == 1) {
		chip->smooth_soc = chip->rsd.smooth_soc_fifo[chip->rsd.smooth_soc_index];
	} else {
		cnt = chip->rsd.smooth_soc_avg_cnt;
		index = chip->rsd.smooth_soc_index;
		while (cnt--) {
			if (chip->rsd.smooth_soc_fifo[index] > 0) {
				valid_soc_cnt++;
				sum += chip->rsd.smooth_soc_fifo[index];
			}
			index = (index + SMOOTH_SOC_MAX_FIFO_LEN - 1) % SMOOTH_SOC_MAX_FIFO_LEN;
		}

		if (valid_soc_cnt > 0)
			chip->smooth_soc = sum / valid_soc_cnt;
		else
			chip->smooth_soc = chip->rsd.smooth_soc_fifo[chip->rsd.smooth_soc_index];
	}

	chg_err("soc[%d %d %d %d %d %d] avg[%d %d %d %d %d] fifo[%d %d %d %d]\n", chip->rsd.rus_reserve_soc,
		reserve_soc, pre_soc, soc, chip->smooth_soc, chip->ui_soc, chip->rsd.smooth_soc_index,
		chip->rsd.smooth_soc_avg_cnt, valid_soc_cnt, sum, chip->rsd.is_soc_jump_range,
		chip->rsd.smooth_soc_fifo[0], chip->rsd.smooth_soc_fifo[1], chip->rsd.smooth_soc_fifo[2],
		chip->rsd.smooth_soc_fifo[3]);

	pre_soc = soc;
	chip->rsd.smooth_soc_index++;
	chip->rsd.smooth_soc_index = chip->rsd.smooth_soc_index % SMOOTH_SOC_MAX_FIFO_LEN;
	if (!chip->rsd.is_soc_jump_range && chip->rsd.smooth_soc_avg_cnt > SMOOTH_SOC_MIN_FIFO_LEN)
		chip->rsd.smooth_soc_avg_cnt--;
	return;

reserve_soc_error:
	chip->rsd.smooth_switch_v2 = false;
	chip->smooth_soc = chip->soc;
}

static bool oplus_usbin_abnormal(struct oplus_chg_chip *chip)
{
	if (chip->support_check_usbin_status && chip->usbin_abnormal_status)
		return true;
	else
		return false;
}

static void oplus_chg_update_ui_soc(struct oplus_chg_chip *chip)
{
	static int soc_down_count = 0;
	static int soc_up_count = 0;
	static int ui_soc_pre = 50;
	static int delta_soc = MIN_DELTA_SOC;
	static int cnt = 0;
	static bool allow_uisoc_down = false;
	static bool pre_vbatt_too_low = false;
	static int pre_prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	int soc_down_limit = 0;
	int soc_up_limit = 0;
	unsigned long sleep_tm = 0;
	unsigned long soc_reduce_margin = 0;
	bool vbatt_too_low = false;
	vbatt_lowerthan_3300mv = false;

	if (chip->nightstandby_support) {
		if (get_soc_feature()) {
			get_time_interval();
		}
	}

	if (chip->ui_soc >= 70) {
		if (oplus_get_flash_screen_ctrl() == true) {
			if (chip->ui_soc == 100) {
				soc_down_limit = SOC_SYNC_DOWN_RATE_300S;
			} else if (chip->ui_soc >= 98) {
				soc_down_limit = SOC_SYNC_DOWN_RATE_300S;
			} else if (chip->ui_soc >= 90) {
				soc_down_limit = SOC_SYNC_DOWN_RATE_120S;
			} else if (chip->ui_soc >= 80) {
				soc_down_limit = SOC_SYNC_DOWN_RATE_80S;
			} else {
				soc_down_limit = SOC_SYNC_DOWN_RATE_70S;
			}
		} else {
			if (chip->ui_soc == 100) {
				soc_down_limit = SOC_SYNC_DOWN_RATE_300S;
			} else if (chip->ui_soc >= 95) {
				soc_down_limit = SOC_SYNC_DOWN_RATE_150S;
			} else {
				soc_down_limit = SOC_SYNC_DOWN_RATE_60S;
			}
		}
	} else if (chip->ui_soc >= 60) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_60S;
	} else if (chip->ui_soc >= 50) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_50S;
	} else if (chip->charger_exist && chip->ui_soc == 1) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_90S;
	} else {
		soc_down_limit = SOC_SYNC_DOWN_RATE_40S;
	}
	if (chip->batt_exist &&
	    (((chip->batt_volt_min < chip->vbatt_power_off ||
	       (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL && oplus_switching_get_hw_enable() &&
		chip->sub_batt_volt < chip->sub_vbatt_power_off)) &&
	      (chip->batt_volt_min > 2500)) ||
	     chip->debug_force_vbatt_too_low)) {
		soc_down_limit = SOC_SYNC_DOWN_RATE_15S;
		vbatt_too_low = true;
		vbatt_lowerthan_3300mv = true;
		charger_xlog_printk(CHG_LOG_CRTI, "batt_volt:%d, batt_volt_min:%d, vbatt_too_low:%d\n", chip->batt_volt,
				    chip->batt_volt_min, vbatt_too_low);
		if (vbatt_too_low && !pre_vbatt_too_low)
			oplus_chg_track_upload_vbatt_too_low_info(chip);
	}
	pre_vbatt_too_low = vbatt_too_low;

	if (chip->batt_full) {
		soc_up_limit = SOC_SYNC_UP_RATE_10S;
	} else {
		soc_up_limit = SOC_SYNC_UP_RATE_10S;
	}

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if ((get_eng_version() == HIGH_TEMP_AGING || get_eng_version() == AGING || oplus_is_ptcrb_version()) &&
	    (chip->smooth_soc >= 0 && chip->smooth_soc <= 100)) {
		chg_err(" HIGH_TEMP_AGING/AGING,force ui_soc = smooth_soc \n");
		chip->ui_soc = chip->smooth_soc;
		soc_up_count = -1;
		soc_down_count = -1;
	}
#endif

#ifndef WPC_NEW_INTERFACE
	if ((chip->charger_exist || oplus_wpc_get_wireless_charge_start()) && chip->batt_exist && chip->batt_full &&
	    chip->mmi_chg &&
	    (chip->stop_chg == 1 || chip->charger_type == 5 || chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS) &&
	    !oplus_pps_get_pps_disconnect() && !oplus_usbin_abnormal(chip)) {
#else
	if ((chip->charger_exist || oplus_wpc_get_status()) && chip->batt_exist && chip->batt_full && chip->mmi_chg &&
	    (chip->stop_chg == 1 || chip->charger_type == 5 || chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS) &&
	    !oplus_pps_get_pps_disconnect() && !oplus_usbin_abnormal(chip)) {
#endif
		chip->sleep_tm_sec = 0;
		chip->save_sleep_tm_sec = 0;
		if (oplus_short_c_batt_is_prohibit_chg(chip)) {
			chip->prop_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else if ((chip->hmac) &&
			   ((chip->tbatt_status == BATTERY_STATUS__NORMAL) ||
			    (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) ||
			    (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) ||
			    (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) ||
			    ((chip->support_3p6_standard) && (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP)))) {
			soc_down_count = 0;
			soc_up_count++;
			if (soc_up_count >= soc_up_limit) {
				soc_up_count = 0;
				chip->ui_soc++;
			}
			if (chip->ui_soc >= 100) {
				chip->ui_soc = 100;
				chip->prop_status = POWER_SUPPLY_STATUS_FULL;
			} else {
				chip->prop_status = POWER_SUPPLY_STATUS_CHARGING;
			}
		} else {
			chip->prop_status = POWER_SUPPLY_STATUS_FULL;
		}
		if (chip->ui_soc != ui_soc_pre) {
			chg_debug("full [soc ui_soc smooth_soc up_limit] = [%d %d %d %d]\n", chip->soc, chip->ui_soc,
				  chip->smooth_soc, soc_up_limit);
		}
		if (pre_prop_status != chip->prop_status && chip->prop_status == POWER_SUPPLY_STATUS_FULL)
			oplus_chg_track_upload_vbatt_diff_over_info(chip, chip->batt_volt_max, chip->batt_volt_min,
								    chip->prop_status);
		chip->keep_prop_status = chip->prop_status;
#ifndef WPC_NEW_INTERFACE
	} else if ((chip->charger_exist || oplus_wpc_get_wireless_charge_start()) && chip->batt_exist &&
		   (CHARGING_STATUS_FAIL != chip->charging_state) && chip->mmi_chg &&
		   (chip->stop_chg == 1 || chip->charger_type == 5 ||
		    chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS) &&
		   !oplus_pps_get_pps_disconnect() && !oplus_usbin_abnormal(chip)) {
#else
	} else if ((chip->charger_exist || oplus_wpc_get_status()) && chip->batt_exist &&
		   (CHARGING_STATUS_FAIL != chip->charging_state) && chip->mmi_chg &&
		   (chip->stop_chg == 1 || chip->charger_type == 5 ||
		    chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS) &&
		   !oplus_pps_get_pps_disconnect() && !oplus_usbin_abnormal(chip)) {
#endif
		chip->sleep_tm_sec = 0;
		chip->save_sleep_tm_sec = 0;
		chip->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (chip->smooth_soc == chip->ui_soc) {
			soc_down_count = 0;
			soc_up_count = 0;
			delta_soc = MIN_DELTA_SOC;
		} else if (chip->smooth_soc > chip->ui_soc) {
			soc_down_count = 0;
			soc_up_count++;
			if (soc_up_count >= soc_up_limit) {
				soc_up_count = 0;
				chip->ui_soc++;
			}
		} else if (chip->smooth_soc < chip->ui_soc) {
			soc_up_count = 0;
			soc_down_count++;
			if (soc_down_count >= soc_down_limit) {
				soc_down_count = 0;
				if ((chip->ui_soc - chip->smooth_soc > delta_soc || allow_uisoc_down) &&
				    (oplus_chg_show_vooc_logo_ornot() == false ||
				     chip->vooc_show_ui_soc_decimal == false || !chip->decimal_control)) {
					chip->ui_soc--;
					allow_uisoc_down = true;
					chg_debug(
						"chg [soc ui_soc smooth_soc down_limit up_limit] = [%d %d %d %d %d]\n",
						chip->soc, chip->ui_soc, chip->smooth_soc, soc_down_limit,
						soc_up_limit);
				}
			}
		}
		if (chip->ui_soc != ui_soc_pre) {
			chg_debug("full [soc ui_soc smooth_soc down_limit up_limit] = [%d %d %d %d %d]\n", chip->soc,
				  chip->ui_soc, chip->smooth_soc, soc_down_limit, soc_up_limit);
		}
		charger_xlog_printk(CHG_LOG_CRTI,
				    "ui_soc:%d,waiting_for_ffc:%d,fastchg_to_ffc:%d,fastchg_start:%d,chg_type=0x%x\n",
				    chip->ui_soc, chip->waiting_for_ffc, chip->fastchg_to_ffc,
				    oplus_vooc_get_fastchg_started(), oplus_vooc_get_fast_chg_type());
		if (chip->ui_soc == 100 && (chip->rsd.smooth_switch_v2 || (chip->fastchg_to_ffc == false && !oplus_chg_is_wls_ffc())) &&
		    ((oplus_vooc_get_fastchg_started() == false) ||
		     (chip->chg_ctrl_by_vooc && oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC)) &&
		    (oplus_pps_get_pps_mos_started() == false) && (oplus_ufcs_get_ufcs_mos_started() == false)) {
			if (++cnt >= 12) {
				chg_err("force batt full when ui_soc=100\n");
				chip->batt_full = true;
				cnt = 0;
				chip->in_rechging = false;
				chip->limits.vfloat_over_counts = 0;
				oplus_chg_check_rechg_status(chip);
			}
		} else {
			cnt = 0;
		}
		chip->keep_prop_status = chip->prop_status;
	} else {
		cnt = 0;
		chip->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
		soc_up_count = 0;
		allow_uisoc_down = false;
		if (chip->smooth_soc <= chip->ui_soc || vbatt_too_low) {
			if (soc_down_count > soc_down_limit) {
				soc_down_count = soc_down_limit + 1;
			} else {
				soc_down_count++;
			}
			sleep_tm = chip->sleep_tm_sec;
			chip->sleep_tm_sec = 0;
			if (sleep_tm > 0) {
				soc_reduce_margin = sleep_tm / TEN_MINUTES;
				if (soc_reduce_margin == 0) {
					chip->save_sleep_tm_sec += sleep_tm;
					if (chip->save_sleep_tm_sec >= ONE_MINUTE &&
					    (chip->ui_soc - chip->smooth_soc) > 2) {
						chip->ui_soc--;
						soc_down_count = 0;
						chip->save_sleep_tm_sec = 0;
					} else if (sleep_tm < OPLUS_CHG_UPDATE_INTERVAL_SEC) {
						soc_down_count--;
					}
				} else if (soc_reduce_margin < (chip->ui_soc - chip->smooth_soc)) {
					chip->ui_soc -= soc_reduce_margin;
					soc_down_count = 0;
					chip->save_sleep_tm_sec = 0;
				} else if (soc_reduce_margin >= (chip->ui_soc - chip->smooth_soc)) {
					chip->ui_soc = chip->smooth_soc;
					soc_down_count = 0;
					chip->save_sleep_tm_sec = 0;
				}

				if (chip->ui_soc != ui_soc_pre)
					chg_err("sleep_tm %d %d %d %d %d %d %d %d %d\n", sleep_tm, soc_reduce_margin,
						chip->save_sleep_tm_sec, chip->soc, chip->smooth_soc, chip->ui_soc,
						ui_soc_pre, soc_down_count, soc_down_limit);
			}
			if (soc_down_count >= soc_down_limit && (chip->smooth_soc < chip->ui_soc || vbatt_too_low)) {
				chip->save_sleep_tm_sec = 0;
				soc_down_count = 0;
				chip->ui_soc--;
			}
		}
		delta_soc = abs(chip->ui_soc - chip->smooth_soc);
		if (delta_soc < MIN_DELTA_SOC)
			delta_soc = MIN_DELTA_SOC;
	}
	if (chip->ui_soc < 2 || chip->debug_force_uisoc_less_than_2) {
		cnt = 0;
		if (oplus_chg_soc_reduce_slow_when_1(chip) == true || oplus_chg_soc_reduce_to_shutdown(chip) == true) {
			chip->ui_soc = 0;
		} else {
			chip->ui_soc = 1;
		}
	}

	if (chip->bat_volt_different && !chip->charger_exist) {
		chip->ui_soc = 0;
	}

	if (chip->ui_soc != ui_soc_pre) {
		if (chip->ui_soc == 1 && (ui_soc_pre > chip->ui_soc)) {
			chip->uisoc_1_start_vbatt_max = chip->batt_volt_max;
			chip->uisoc_1_start_vbatt_min = chip->batt_volt_min;
			chip->uisoc_1_start_batt_rm = chip->batt_rm;
			chip->uisoc_1_start_time = oplus_chg_get_local_time_s();
		}
		ui_soc_pre = chip->ui_soc;
		if (chip->is_gauge_ready) {
			chip->chg_ops->set_rtc_soc(chip->ui_soc);
			if (chip->chg_ops->get_rtc_soc() != chip->ui_soc)
				chip->chg_ops->set_rtc_soc(chip->ui_soc);
		}
	}
	if (chip->decimal_control) {
		soc_down_count = 0;
		soc_up_count = 0;
	}
	pre_prop_status = chip->prop_status;
}

static void fg_update(struct oplus_chg_chip *chip)
{
	static int ui_soc_pre_fg = 50;
	static struct power_supply *bms_psy = NULL;
	if (!bms_psy) {
		bms_psy = power_supply_get_by_name("bms");
		charger_xlog_printk(CHG_LOG_CRTI, "bms_psy null\n");
	}
	if (bms_psy) {
		if (chip->ui_soc != ui_soc_pre_fg) {
			power_supply_changed(bms_psy);
			charger_xlog_printk(CHG_LOG_CRTI, "ui_soc:%d, soc:%d, ui_soc_pre:%d \n", chip->ui_soc,
					    chip->soc, ui_soc_pre_fg);
		}
		if (chip->ui_soc != ui_soc_pre_fg) {
			ui_soc_pre_fg = chip->ui_soc;
		}
	}
}

#define BATTERY_UPDATE_THD 4
static void battery_update(struct oplus_chg_chip *chip)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	static int pre_ui_soc = -1;
	static int batt_update_count = 0;
	static int pre_charger_exist = 0;
#endif

	oplus_chg_update_ui_soc(chip);

	if (chip->fg_bcl_poll) {
		fg_update(chip);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	batt_update_count++;
	if (chip->charger_exist || pre_ui_soc != chip->ui_soc || pre_charger_exist != chip->charger_exist ||
	    batt_update_count == BATTERY_UPDATE_THD || chip->force_psy_changed) {
		pre_ui_soc = chip->ui_soc;
		pre_charger_exist = chip->charger_exist;
		batt_update_count = 0;
		chip->force_psy_changed = false;
		power_supply_changed(chip->batt_psy);
	}
#else
	power_supply_changed(&chip->batt_psy);
#endif
}

static void oplus_chg_battery_update_status(struct oplus_chg_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	usb_update(chip);
#endif
	battery_update(chip);
}

static void oplus_chg_get_chargerid_voltage(struct oplus_chg_chip *chip)
{
	if (chip->chg_ops->set_chargerid_switch_val == NULL || chip->chg_ops->get_chargerid_switch_val == NULL ||
	    chip->chg_ops->get_chargerid_volt == NULL) {
		return;
	} else if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		return;
	}
	if (chip->chg_ops->check_pdphy_ready && chip->chg_ops->check_pdphy_ready() == false) {
		chg_err("OPLUS CHG PD_PHY NOT READY");
		return;
	}
	if (reset_mcu_delay > RESET_MCU_DELAY_30S) {
		return;
	}
	if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
	    oplus_voocphy_stage_check() == false) {
		chg_err("OPLUS voocphy stage false");
		return;
	}
	if (chip->vbatt_num == DOUBLE_BATTERY) {
		if (oplus_gauge_afi_update_done() == false) {
			chg_err("zy gauge afi_update_done ing...\n");
			return;
		}
	}
	if (oplus_vooc_get_vooc_switch_val() == 1) {
		if (chip->chargerid_volt_got == false) {
			chip->chg_ops->set_chargerid_switch_val(1);
#ifdef CONFIG_OPLUS_CHARGER_MTK
			if (oplus_vooc_get_fastchg_started() == false) {
				oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
			}
			usleep_range(100000, 110000);
#else
			usleep_range(20000, 22000);
#endif /* CONFIG_OPLUS_CHARGER_MTK */
			chip->chargerid_volt = chip->chg_ops->get_chargerid_volt();
			chip->chargerid_volt_got = true;
		} else {
			if (chip->chg_ops->get_chargerid_switch_val() == 0) {
				chip->chg_ops->set_chargerid_switch_val(1);
			} else {
				/* do nothing*/
			}
		}
	} else if (oplus_vooc_get_vooc_switch_val() == 0) {
		if (chip->chargerid_volt_got == false) {
			chip->chg_ops->set_chargerid_switch_val(1);
			oplus_vooc_set_vooc_chargerid_switch_val(1);
#ifdef CONFIG_OPLUS_CHARGER_MTK
			usleep_range(100000, 110000);
#else
			usleep_range(20000, 22000);
#endif /* CONFIG_OPLUS_CHARGER_MTK */
			chip->chargerid_volt = chip->chg_ops->get_chargerid_volt();
			chip->chargerid_volt_got = true;
			oplus_vooc_set_vooc_chargerid_switch_val(0);
			if (chip->vooc_project == false) {
				chip->chg_ops->set_chargerid_switch_val(0);
			}
		} else {
			if (chip->chg_ops->get_chargerid_switch_val() == 1) {
				chip->chg_ops->set_chargerid_switch_val(0);
			} else {
				/* do nothing*/
			}
		}
	} else {
		charger_xlog_printk(CHG_LOG_CRTI, "do nothing\n");
	}
}

static void oplus_chg_chargerid_switch_check(struct oplus_chg_chip *chip)
{
	return oplus_chg_get_chargerid_voltage(chip);
}

#define RESET_MCU_DELAY_15S 3
#define RESET_MCU_DELAY_45S 9
#define RESET_MCU_DELAY_150S 30

static void oplus_efttest_fast_switch(struct oplus_chg_chip *chip)
{
	if (reset_mcu_delay == RESET_MCU_DELAY_45S && chip->chg_ops->get_charger_subtype() != CHARGER_SUBTYPE_QC &&
	    chip->chg_ops->get_charger_subtype() != CHARGER_SUBTYPE_PD) {
		suspend_charger = false;
		reset_mcu_delay = RESET_MCU_DELAY_45S + 1;
		charger_xlog_printk(CHG_LOG_CRTI, "  RESET_MCU_DELAY_45S\n");
		chip->chg_ops->set_chargerid_switch_val(1);
		oplus_vooc_switch_fast_chg();
		mcu_status = 1;
	} else if (reset_mcu_delay == (RESET_MCU_DELAY_150S * efttest_count) && efttest_count < 4 &&
		   chip->chg_ops->get_charger_subtype() != CHARGER_SUBTYPE_QC &&
		   chip->chg_ops->get_charger_subtype() != CHARGER_SUBTYPE_PD) {
		reset_mcu_delay = RESET_MCU_DELAY_150S + 1;
		charger_xlog_printk(CHG_LOG_CRTI, "  RESET_MCU_DELAY_150S\n");
		oplus_chg_suspend_charger();
		msleep(1000);
		if (g_charger_chip->mmi_chg) {
			oplus_chg_unsuspend_charger();
			oplus_vooc_set_ap_clk_high();
			oplus_vooc_reset_mcu();
			oplus_chg_set_input_current_limit(chip);
			mcu_status = 1;
		}
		efttest_count++;
	}
}

int __attribute__((weak)) oplus_adsp_reset_voocphy(void)
{
	return 0;
}

static void oplus_chg_qc_config(struct oplus_chg_chip *chip);
static void oplus_chg_fast_switch_check(struct oplus_chg_chip *chip)
{
	static bool mcu_update = false;

	if (!(chip->vooc_project))
		return;

	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		charger_xlog_printk(CHG_LOG_CRTI, " short_c_battery, return\n");
		return;
	}
	if (chip->mmi_chg == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, " mmi_chg,return\n");
		return;
	}
	if (chip->balancing_bat_stop_chg == 1 || chip->balancing_bat_stop_fastchg == 1) {
		charger_xlog_printk(CHG_LOG_CRTI, " balancing_bat,return\n");
		return;
	}
	if (chip->allow_swtich_to_fastchg == false) {
		charger_xlog_printk(CHG_LOG_CRTI, " allow_swtich_to_fastchg == 0,return\n");
		return;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		charger_xlog_printk(CHG_LOG_CRTI, "non authenticate or hmac,switch return\n");
		return;
	}

	if (chip->pd_wait_svid && chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) {
		charger_xlog_printk(CHG_LOG_CRTI, "voocphy use PD, wait 50ms\n");
		msleep(50);
		chip->pd_wait_svid = false;
	}
	if (chip->pd_svooc == false && chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) {
		charger_xlog_printk(CHG_LOG_CRTI, "voocphy use PD,switch return\n");
		return;
	}
	if (chip->notify_flag == NOTIFY_BAT_OVER_VOL) {
		charger_xlog_printk(CHG_LOG_CRTI, " battery over voltage,return\n");
		return;
	}
#ifdef OPLUS_CUSTOM_OP_DEF
	if (chip->hiz_gnd_cable && chip->svooc_disconnect_count > 3) {
		charger_xlog_printk(CHG_LOG_CRTI, " disconnect 3 times,return\n");
		return;
	}
#endif

#ifdef SUPPORT_WPC
	if (chip->wireless_support && (oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present())) {
		charger_xlog_printk(CHG_LOG_CRTI, "is in WPC, switch return\n");
		return;
	}
#endif
	if (oplus_is_ufcs_charging()) {
		charger_xlog_printk(CHG_LOG_CRTI, "ufcs charging\n");
		return;
	}

	if (oplus_get_quirks_plug_status(QUIRKS_STOP_ADSP_VOOCPHY)) {
		charger_xlog_printk(CHG_LOG_CRTI, "QUIRKS_STOP_ADSP_VOOCPHY\n");
		return;
	}

	if (true == opchg_get_mcu_update_state()) {
		reset_mcu_delay = 0;
		mcu_update = true;
		return;
	}
	if (chip->chg_ops->check_pdphy_ready && chip->chg_ops->check_pdphy_ready() == false) {
		chg_err("OPLUS CHG PD_PHY NOT READY");
#ifdef CONFIG_OPLUS_CHARGER_MTK
		/*10 seconds after typec init, PD starts to communicate, so set true force reset adapter.only for MTK*/
		if (oplus_is_power_off_charging(NULL)) {
			mcu_update = true;
		}
#endif
		return;
	}

	if (oplus_pps_get_chg_status() != PPS_NOT_SUPPORT && chip->pps_force_svooc == false &&
	    chip->chg_ops->oplus_chg_get_pd_type() == PD_PPS_ACTIVE) {
		return;
	}

	if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
	    oplus_voocphy_stage_check() == false) {
		chg_err("OPLUS voocphy stage false");
		high_vol_light = true;
		return;
	}

	if (oplus_vooc_get_abnormal_adapter_current_cnt() > 0 &&
	    chip->abnormal_adapter_dis_cnt >= oplus_vooc_get_abnormal_adapter_current_cnt()) {
		charger_xlog_printk(CHG_LOG_CRTI, " abnormal adapter dis cnt >= vooc_max,return\n");
		return;
	}
	if (chip->vbatt_num == DOUBLE_BATTERY) {
		if (oplus_gauge_afi_update_done() == false) {
			chg_err("zy gauge afi_update_done ing...\n");
			return;
		}
	}
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
		if (oplus_vooc_get_fastchg_started() == false && reset_mcu_delay < RESET_MCU_DELAY_30S) {
			if (chip->boot_reset_adapter && !chip->boot_completed && !oplus_is_power_off_charging(NULL)) {
				chg_err("boot reset adapter\n");
				mcu_update = true;
			}
			if (mcu_update == true) {
				chip->boot_reset_adapter = false;
				oplus_chg_suspend_charger();
				msleep(2000);
				oplus_chg_unsuspend_charger();
				mcu_update = false;
				chg_debug("mcu_update, reset\n");
				msleep(2000);
				oplus_chg_set_chargerid_switch_val(1);
			}
			if (g_charger_chip->adsp_notify_ap_suspend == true ||
			    ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			      oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
			     (high_vol_light == true || (oplus_vooc_get_fastchg_dummy_started() == true &&
							 oplus_voocphy_get_real_fastchg_allow())))) {
				oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
				oplus_chg_suspend_charger();
				msleep(1500);
				oplus_chg_unsuspend_charger();
				msleep(500);
				high_vol_light = false;
				g_charger_chip->adsp_notify_ap_suspend = false;
				chg_debug("high_vol_light, reset\n");
			}
			if (oplus_pps_get_chg_status() != PPS_NOT_SUPPORT && chip->pps_force_svooc == false
					&& chip->chg_ops->oplus_chg_get_pd_type() == PD_PPS_ACTIVE) {
				chg_err("wait,PD_PPS_ACTIVE,switch return\n");/*debug for pd*/
				return;
			}
			oplus_vooc_switch_fast_chg();
			mcu_status = 1;
		}
		if (!oplus_vooc_get_fastchg_started() && !oplus_vooc_get_fastchg_dummy_started() &&
		    !oplus_vooc_get_fastchg_to_normal() && !oplus_vooc_get_fastchg_to_warm()) {
			if (suspend_charger) {
				reset_mcu_delay = RESET_MCU_DELAY_15S;
				suspend_charger = false;
				oplus_vooc_set_ap_clk_high();
				oplus_vooc_reset_mcu();
			}
			if (time_is_before_jiffies(chip->reset_mcu_jiffies + msecs_to_jiffies(5000))) {
				reset_mcu_delay++;
				chip->reset_mcu_jiffies = jiffies;
			}
			if (g_charger_chip->voocphy_notify_ap_suspend &&
			    (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
			    reset_mcu_delay != RESET_MCU_DELAY_15S &&
			    reset_mcu_delay < RESET_MCU_DELAY_30S) {
				oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
				oplus_chg_suspend_charger();
				msleep(1500);
				oplus_chg_unsuspend_charger();
				msleep(500);
				g_charger_chip->voocphy_notify_ap_suspend = false;
				oplus_vooc_switch_fast_chg();
			}
			if (reset_mcu_delay == RESET_MCU_DELAY_15S) {
				charger_xlog_printk(CHG_LOG_CRTI, "  reset mcu again\n");
				if (suspend_charger == false) {
					if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
					    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
						oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
						oplus_chg_suspend_charger();
						msleep(1500);
						oplus_chg_unsuspend_charger();
						msleep(500);
						oplus_vooc_switch_fast_chg();
					} else {
						oplus_chg_reset_adapter();
						suspend_charger = true;
					}
					chg_debug(" fastchg start failed, reset adapter\n");
				} else {
					oplus_vooc_set_ap_clk_high();
					oplus_vooc_reset_mcu();
					mcu_status = 1;
				}
			} else if (reset_mcu_delay == RESET_MCU_DELAY_30S &&
				   (chip->vbatt_num == 2 || is_vooc_support_single_batt_svooc() == true ||
				    (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
				     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY))) {
				suspend_charger = false;
				reset_mcu_delay = RESET_MCU_DELAY_30S + 1;
				if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
				    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
					voocphy_cpufreq_release();
				}
				if (oplus_pps_get_chg_status() != PPS_NOT_SUPPORT) {
					oplus_pps_set_mcu_vooc_mode();
				}
				charger_xlog_printk(CHG_LOG_CRTI, "  RESET_MCU_DELAY_30S\n");
				oplus_chg_track_upload_fast_gpio_info(chip);
				if (chip->charger_volt <= 7500 &&
				    chip->chg_ops->oplus_chg_get_pd_type() != PD_PPS_ACTIVE) {
					oplus_vooc_reset_fastchg_after_usbout();
					chip->chg_ops->set_chargerid_switch_val(0);
					if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
						oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
						oplus_voocphy_reset_voocphy();
					}
					if (chip->chg_ops->enable_qc_detect) {
						chg_err("enable_qc_detect");
						chip->chg_ops->enable_qc_detect();
					}
				}
			} else if (chip->efttest_fast_switch) {
				oplus_efttest_fast_switch(chip);
			}
		} else {
			suspend_charger = false;
			mcu_update = false;
			high_vol_light = false;
			if (chip->internal_gauge_with_asic == 1 && oplus_vooc_get_reset_gpio_status() == 0) {
				oplus_vooc_set_ap_clk_high();
				oplus_vooc_reset_mcu();
				mcu_status = 1;
			}
		}
		if (reset_mcu_delay > RESET_MCU_DELAY_30S) {
			chip->pd_svooc = false;
			if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
			    chip->vooc_start_fail == false) {
				oplus_voocphy_dump_reg();
				chip->vooc_start_fail = true;
			}
		}
	} else {
		mcu_update = false;
	}
}

#ifdef OPLUS_CUSTOM_OP_DEF
int oplus_svooc_disconnect_time(void)
{
	if (g_charger_chip != NULL)
		return g_charger_chip->svooc_disconnect_count;
	else
		return 0;
}
#endif

#define FULL_COUNTS_SW 5
#define FULL_COUNTS_HW 4

static int oplus_chg_check_sw_full(struct oplus_chg_chip *chip)
{
	int vbatt_full_vol_sw = 0;

	if (!chip->charger_exist) {
		chip->sw_full_count = 0;
		chip->sw_full = false;
		return false;
	}

	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		vbatt_full_vol_sw = chip->limits.cold_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		vbatt_full_vol_sw = chip->limits.little_cold_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		vbatt_full_vol_sw = chip->limits.cool_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		vbatt_full_vol_sw = chip->limits.little_cool_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		vbatt_full_vol_sw = chip->limits.normal_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		vbatt_full_vol_sw = chip->limits.warm_vfloat_sw_limit;
	} else {
		chip->sw_full_count = 0;
		chip->sw_full = 0;
		return false;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		vbatt_full_vol_sw = chip->limits.non_standard_vfloat_sw_limit;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		vbatt_full_vol_sw = chip->limits.short_c_bat_vfloat_sw_limit;
	}
	/* use SW Vfloat to check */
	if (chip->batt_volt > vbatt_full_vol_sw) {
		if (chip->icharging < 0 && (chip->icharging * -1) <= chip->limits.iterm_ma) {
			chip->sw_full_count++;
			if (chip->sw_full_count > FULL_COUNTS_SW) {
				chip->sw_full_count = 0;
				chip->sw_full = true;
			}
		} else if (chip->icharging >= 0) {
			chip->sw_full_count++;
			if (chip->sw_full_count > FULL_COUNTS_SW * 2) {
				chip->sw_full_count = 0;
				chip->sw_full = true;
				charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Battery full by sw when icharging>=0!!\n");
			}
		} else {
			chip->sw_full_count = 0;
			chip->sw_full = false;
		}
	} else {
		chip->sw_full_count = 0;
		chip->sw_full = false;
	}
	return chip->sw_full;
}

static int oplus_chg_check_hw_full(struct oplus_chg_chip *chip)
{
	int vbatt_full_vol_hw = 0;

	if (!chip->charger_exist) {
		chip->full_data.vbat_counts_hw = 0;
		chip->full_data.hw_full = false;
		chip->hw_full_by_sw = false;
		return false;
	}
	vbatt_full_vol_hw = oplus_chg_get_float_voltage(chip);
	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_cold_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_little_cold_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_cool_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_little_cool_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		vbatt_full_vol_hw = chip->limits.temp_normal_vfloat_mv + chip->limits.normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		vbatt_full_vol_hw = chip->limits.temp_warm_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else {
		chip->full_data.vbat_counts_hw = 0;
		chip->full_data.hw_full = false;
		chip->hw_full_by_sw = false;
		return false;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		vbatt_full_vol_hw = chip->limits.non_standard_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		vbatt_full_vol_hw = chip->limits.short_c_bat_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	}
	/* use HW Vfloat to check */
	if (chip->batt_volt >= vbatt_full_vol_hw) {
		chip->full_data.vbat_counts_hw++;
		if (chip->full_data.vbat_counts_hw >= FULL_COUNTS_HW) {
			chip->full_data.vbat_counts_hw = 0;
			chip->full_data.hw_full = true;
		}
	} else {
		chip->full_data.vbat_counts_hw = 0;
		chip->full_data.hw_full = false;
	}

	chip->hw_full_by_sw = chip->full_data.hw_full;
	return chip->full_data.hw_full;
}

static int oplus_chg_check_sw_sub_batt_full(struct oplus_chg_chip *chip)
{
	int sub_vbatt_full_vol_sw = 0;

	if (!chip->charger_exist) {
		chip->sw_sub_batt_full_count = 0;
		chip->sw_sub_batt_full = false;
		return false;
	}

	if (chip->sw_sub_batt_full && !chip->in_rechging) {
		return true;
	}

	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		sub_vbatt_full_vol_sw = chip->limits.cold_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		sub_vbatt_full_vol_sw = chip->limits.little_cold_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		sub_vbatt_full_vol_sw = chip->limits.cool_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		sub_vbatt_full_vol_sw = chip->limits.little_cool_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		sub_vbatt_full_vol_sw = chip->limits.normal_vfloat_sw_limit;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		sub_vbatt_full_vol_sw = chip->limits.warm_vfloat_sw_limit;
	} else {
		chip->sw_sub_batt_full_count = 0;
		chip->sw_sub_batt_full = 0;
		return false;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		sub_vbatt_full_vol_sw = chip->limits.non_standard_vfloat_sw_limit;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		sub_vbatt_full_vol_sw = chip->limits.short_c_bat_vfloat_sw_limit;
	}
#ifdef SUPPORT_WPC
	chg_err("*****[Sub_Batt vol: %d] [Sub_Batt max vol: %d] [Sub_Batt min vol: %d] [Sub_Chg Curr: %d] [Sub_Term Vol: S%d] [Sub_Term Curr: %d]*****\n",
		chip->sub_batt_volt, chip->batt_volt_max, chip->batt_volt_min, chip->sub_batt_icharging,
		sub_vbatt_full_vol_sw, chip->limits.iterm_ma);
#endif
	/* use SW Vfloat to check */
	if (chip->sub_batt_volt > sub_vbatt_full_vol_sw) {
		if (chip->sub_batt_icharging < 0 && (chip->sub_batt_icharging * -1) <= chip->limits.sub_iterm_ma) {
			chip->sw_sub_batt_full_count++;
			if (chip->sw_sub_batt_full_count > FULL_COUNTS_SW) {
				chip->sw_sub_batt_full_count = 0;
				chip->sw_sub_batt_full = true;
			}
		} else if (chip->sub_batt_icharging >= 0) {
			chip->sw_sub_batt_full_count++;
			if (chip->sw_sub_batt_full_count > FULL_COUNTS_SW * 2) {
				chip->sw_sub_batt_full_count = 0;
				chip->sw_sub_batt_full = true;
				charger_xlog_printk(CHG_LOG_CRTI,
						    "[BATTERY] Sub_Battery full by sw when icharging>=0!!\n");
			}
		} else {
			chip->sw_sub_batt_full_count = 0;
			chip->sw_sub_batt_full = false;
		}
	} else {
		chip->sw_sub_batt_full_count = 0;
		chip->sw_sub_batt_full = false;
	}
	return chip->sw_sub_batt_full;
}

static int oplus_chg_check_hw_sub_batt_full(struct oplus_chg_chip *chip)
{
	int sub_vbatt_full_vol_hw = 0;

	if (!chip->charger_exist) {
		chip->full_data.sub_vbat_counts_hw = 0;
		chip->full_data.sub_hw_full = false;
		chip->hw_sub_batt_full_by_sw = false;
		return false;
	}

	if (chip->hw_sub_batt_full_by_sw && !chip->in_rechging) {
		return true;
	}

	sub_vbatt_full_vol_hw = oplus_chg_get_float_voltage(chip);
	if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		sub_vbatt_full_vol_hw = chip->limits.temp_cold_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		sub_vbatt_full_vol_hw = chip->limits.temp_little_cold_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		sub_vbatt_full_vol_hw = chip->limits.temp_cool_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		sub_vbatt_full_vol_hw = chip->limits.temp_little_cool_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		sub_vbatt_full_vol_hw = chip->limits.temp_normal_vfloat_mv + chip->limits.normal_vterm_hw_inc;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		sub_vbatt_full_vol_hw = chip->limits.temp_warm_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	} else {
		chip->full_data.sub_vbat_counts_hw = 0;
		chip->full_data.sub_hw_full = false;
		chip->hw_sub_batt_full_by_sw = false;
		return false;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		sub_vbatt_full_vol_hw = chip->limits.non_standard_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		sub_vbatt_full_vol_hw = chip->limits.short_c_bat_vfloat_mv + chip->limits.non_normal_vterm_hw_inc;
	}
#ifdef SUPPORT_WPC
	chg_err("*****[Batt vol: %d] [Batt max vol: %d] [Batt min vol: %d] [Chg Curr: %d] [Term Vol: H%d] [Term Curr: %d]*****\n",
		chip->sub_batt_volt, chip->batt_volt_max, chip->batt_volt_min, chip->icharging, sub_vbatt_full_vol_hw,
		chip->limits.iterm_ma);
#endif
	/* use HW Vfloat to check */
	if (chip->sub_batt_volt >= sub_vbatt_full_vol_hw) {
		chip->full_data.sub_vbat_counts_hw++;
		if (chip->full_data.sub_vbat_counts_hw >= FULL_COUNTS_HW) {
			chip->full_data.sub_vbat_counts_hw = 0;
			chip->full_data.sub_hw_full = true;
		}
	} else {
		chip->full_data.sub_vbat_counts_hw = 0;
		chip->full_data.sub_hw_full = false;
	}

	chip->hw_sub_batt_full_by_sw = chip->full_data.sub_hw_full;
	return chip->full_data.sub_hw_full;
}
#define FFC_VOLT_COUNTS 4
#define FFC_CURRENT_COUNTS 3

static void oplus_chg_ffc_variable_reset(struct oplus_chg_chip *chip)
{
	chip->fastchg_to_ffc = false;
	chip->fastchg_ffc_status = 0;
	chip->chg_ctrl_by_lcd = chip->chg_ctrl_by_lcd_default;
	chip->chg_ctrl_by_vooc = chip->chg_ctrl_by_vooc_default;
	chip->limits.iterm_ma = chip->limits.default_iterm_ma;
	chip->limits.sub_iterm_ma = chip->limits.default_sub_iterm_ma;
	chip->limits.normal_vfloat_sw_limit = chip->limits.default_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv = chip->limits.default_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit = chip->limits.default_normal_vfloat_over_sw_limit;
	chip->limits.temp_normal_fastchg_current_ma = chip->limits.default_temp_normal_fastchg_current_ma;
	chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.default_temp_little_cool_fastchg_current_ma;
	chip->limits.little_cool_vfloat_sw_limit = chip->limits.default_little_cool_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv = chip->limits.default_temp_little_cool_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit = chip->limits.default_little_cool_vfloat_over_sw_limit;
	oplus_chg_aging_ffc_variable_reset(chip);
	oplus_pps_set_ffc_started(false);
	oplus_ufcs_set_ffc_started(false);
}

static bool oplus_chg_check_sub_vbat_ffc_end(struct oplus_chg_chip *chip)
{
	int ffc_vfloat_sw_limit = 4450;
	if (!oplus_switching_support_parallel_chg()) {
		return false;
	}
	if (chip->fastchg_ffc_status == 1) {
		ffc_vfloat_sw_limit = chip->limits.ffc1_normal_vfloat_sw_limit;
	} else if (chip->fastchg_ffc_status == 2) {
		if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM) {
			ffc_vfloat_sw_limit = chip->limits.ffc2_warm_vfloat_sw_limit;
		} else {
			ffc_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
		}
	} else {
		return false;
	}

	if (chip->sub_batt_volt > ffc_vfloat_sw_limit) {
		chg_err("sub_vbat_ffc_end !!! sub_batt_volt:%d, ffc_vfloat_sw_limit:%d, fastchg_ffc_status:%d",
			chip->sub_batt_volt, ffc_vfloat_sw_limit, chip->ffc_temp_status);
		return true;
	} else {
		chg_err("sub_batt_volt:%d, ffc_vfloat_sw_limit:%d, fastchg_ffc_status:%d", chip->sub_batt_volt,
			ffc_vfloat_sw_limit, chip->ffc_temp_status);
		return false;
	}
}

static bool oplus_chg_check_sub_ibat_ffc_end(struct oplus_chg_chip *chip)
{
	int ffc_ibatt_sw_limit = 750;
	if (!oplus_switching_support_parallel_chg()) {
		return false;
	}
	if (chip->fastchg_ffc_status == 1) {
		if (chip->ffc_temp_status == FFC_TEMP_STATUS__NORMAL) {
			ffc_ibatt_sw_limit = chip->limits.ff1_normal_fastchg_ma - chip->limits.sub_ff1_exit_step_ma;
		} else if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM) {
			ffc_ibatt_sw_limit = chip->limits.ff1_warm_fastchg_ma - chip->limits.sub_ff1_warm_exit_step_ma;
		} else {
			return false;
		}
	} else if (chip->fastchg_ffc_status == 2) {
		if (chip->ffc_temp_status == FFC_TEMP_STATUS__NORMAL) {
			ffc_ibatt_sw_limit = chip->limits.ffc2_normal_fastchg_ma - chip->limits.sub_ffc2_exit_step_ma;
		} else if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM) {
			ffc_ibatt_sw_limit =
				chip->limits.ffc2_warm_fastchg_ma - chip->limits.sub_ffc2_warm_exit_step_ma;
		} else {
			return false;
		}
	} else {
		return false;
	}

	if ((chip->sub_batt_icharging * -1) < ffc_ibatt_sw_limit) {
		chg_err("sub_ibat_ffc_end !!! sub_batt_icharging:%d, ffc_ibatt_sw_limit:%d, fastchg_ffc_status:%d",
			chip->sub_batt_icharging, ffc_ibatt_sw_limit, chip->ffc_temp_status);
		return true;
	} else {
		chg_err("sub_batt_icharging:%d, ffc_ibatt_sw_limit:%d, fastchg_ffc_status:%d", chip->sub_batt_icharging,
			ffc_ibatt_sw_limit, chip->ffc_temp_status);
		return false;
	}
}

static bool oplus_chg_check_ffc_status(struct oplus_chg_chip *chip)
{
	static int vffc1_counts = 0;
	static int vffc2_counts = 0;
	static int warm_counts = 0;
	static int normal_counts = 0;
	static int ffc_vfloat_sw_limit = 4450;
	bool sub_vbat_ffc_end_status = 0;
	bool sub_ibat_ffc_end_status = 0;

	if (chip->fastchg_to_ffc == true) {
		if (oplus_switching_support_parallel_chg()) {
			sub_vbat_ffc_end_status = oplus_chg_check_sub_vbat_ffc_end(chip);
			sub_ibat_ffc_end_status = oplus_chg_check_sub_ibat_ffc_end(chip);
		}
		if (chip->fastchg_ffc_status == 1) {
			if (chip->batt_volt >= chip->limits.ffc1_normal_vfloat_sw_limit || sub_vbat_ffc_end_status) {
				vffc1_counts++;
				if (vffc1_counts >= FFC_VOLT_COUNTS) {
					oplus_chg_turn_on_ffc2(chip);
					return false;
				}
			}
			if (chip->ffc_temp_status == FFC_TEMP_STATUS__NORMAL) {
				if ((chip->icharging * -1) <
					    (chip->limits.ff1_normal_fastchg_ma - chip->limits.ff1_exit_step_ma) ||
				    sub_ibat_ffc_end_status) {
					normal_counts++;
					if (normal_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						return true;
					}
				} else {
					normal_counts = 0;
				}
			} else if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM) {
				if ((chip->icharging * -1) <
					    (chip->limits.ff1_warm_fastchg_ma - chip->limits.ff1_warm_exit_step_ma) ||
				    sub_ibat_ffc_end_status) {
					warm_counts++;
					if (warm_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						return true;
					}
				} else {
					warm_counts = 0;
				}
			} else {
				warm_counts = normal_counts = 0;
				oplus_chg_ffc_variable_reset(chip);
				oplus_chg_turn_on_charging(chip);
				return true;
			}
			return false;
		}
		if (chip->fastchg_ffc_status == 2) {
			if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM)
				ffc_vfloat_sw_limit = chip->limits.ffc2_warm_vfloat_sw_limit;
			else
				ffc_vfloat_sw_limit = chip->limits.ffc2_normal_vfloat_sw_limit;
			if (chip->batt_volt >= ffc_vfloat_sw_limit || sub_vbat_ffc_end_status) {
				vffc2_counts++;
				if (vffc2_counts >= FFC_VOLT_COUNTS) {
					oplus_chg_ffc_variable_reset(chip);
					oplus_chg_turn_on_charging(chip);
					return true;
				}
			}
			if (chip->ffc_temp_status == FFC_TEMP_STATUS__NORMAL) {
				if ((chip->icharging * -1) <
					    (chip->limits.ffc2_normal_fastchg_ma - chip->limits.ffc2_exit_step_ma) ||
				    sub_ibat_ffc_end_status) {
					normal_counts++;
					if (normal_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						return true;
					}
				} else {
					normal_counts = 0;
				}
			} else if (chip->ffc_temp_status == FFC_TEMP_STATUS__WARM) {
				if ((chip->icharging * -1) <
					    (chip->limits.ffc2_warm_fastchg_ma - chip->limits.ffc2_warm_exit_step_ma) ||
				    sub_ibat_ffc_end_status) {
					warm_counts++;
					if (warm_counts >= FFC_CURRENT_COUNTS) {
						oplus_chg_ffc_variable_reset(chip);
						oplus_chg_turn_on_charging(chip);
						return true;
					}
				} else {
					warm_counts = 0;
				}
			} else {
				warm_counts = normal_counts = 0;
				oplus_chg_ffc_variable_reset(chip);
				oplus_chg_turn_on_charging(chip);
				return true;
			}
		}
		return false;
	}
	vffc1_counts = 0;
	vffc2_counts = 0;
	warm_counts = 0;
	normal_counts = 0;
	return true;
}

static bool oplus_chg_check_vbatt_is_full_by_sw(struct oplus_chg_chip *chip)
{
	bool ret_sw = false;
	bool ret_hw = false;
	bool ret_sub_sw = false;
	bool ret_sub_hw = false;

	if (!chip->check_batt_full_by_sw) {
		return false;
	}

	if (oplus_switching_support_parallel_chg()) {
		ret_sub_sw = oplus_chg_check_sw_sub_batt_full(chip);
		ret_sub_hw = oplus_chg_check_hw_sub_batt_full(chip);
		if (ret_sub_sw == true || ret_sub_hw == true) {
			charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Sub_Battery full by sw[%s] !!\n",
					    (ret_sw == true) ? "S" : "H");
			oplus_switching_enable_charge(0);
		}
	}
	ret_sw = oplus_chg_check_sw_full(chip);
	ret_hw = oplus_chg_check_hw_full(chip);
	if (ret_sw == true || ret_hw == true) {
		charger_xlog_printk(CHG_LOG_CRTI, "[BATTERY] Battery full by sw[%s] !!\n",
				    (ret_sw == true) ? "S" : "H");
		if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL && !chip->soc_not_full_report &&
		    (chip->main_batt_soc <= SOC_NOT_FULL_REPORT || chip->sub_batt_soc <= SOC_NOT_FULL_REPORT) &&
		    (chip->tbatt_status == BATTERY_STATUS__NORMAL ||
		     chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP ||
		     chip->tbatt_status == BATTERY_STATUS__COOL_TEMP ||
		     chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP ||
		     chip->tbatt_status == BATTERY_STATUS__COLD_TEMP)) {
			chg_err("soc too low %d %d when full", chip->main_batt_soc, chip->sub_batt_soc);
			oplus_chg_track_parallel_mos_error(REASON_SOC_NOT_FULL);
			chip->parallel_error_flag |= REASON_SOC_NOT_FULL;
			chip->soc_not_full_report = true;
		}
		return true;
	} else {
		return false;
	}
}

static int oplus_chg_allow_skip_ffc(struct oplus_chg_chip *chip) {
	if (!chip) {
		return false;
	}
	if (!chip->full_pre_ffc_judge) {
		return false;
	}
	if (chip->batt_volt >= chip->full_pre_ffc_mv && chip->soc == 100) {
		chg_err("skip ffc, batt_volt = %d, full_pre_ffc_mv = %d, soc:%d\n",
				chip->batt_volt, chip->full_pre_ffc_mv, chip->soc);
		return true;
	} else {
		chg_err("skip ffc, batt_volt = %d, full_pre_ffc_mv = %d, soc:%d\n",
				chip->batt_volt, chip->full_pre_ffc_mv, chip->soc);
	}
	return false;
}

#define FULL_DELAY_COUNTS 4
#define DOD0_COUNTS (8 * 60 / 5)
#define CLEAR_FULL_CNT 4
#define CLEAR_FULL_VOLT_THD 100

static void oplus_chg_check_status_full(struct oplus_chg_chip *chip)
{
	int is_batt_full = 0;
	static int fastchg_present_wait_count = 0;
	static int pps_to_ffc_full_count = 0;
	static int last_recharging_vol = 0;
	int recharging_vol = 0;

	if (chip->chg_ctrl_by_vooc) {
		if (oplus_vooc_get_fastchg_ing() == true &&
		    oplus_vooc_get_fast_chg_type() != CHARGER_SUBTYPE_FASTCHG_VOOC)
			return;
	} else {
		if (oplus_vooc_get_fastchg_ing() == true)
			return;
	}

	if (chip->wireless_support && oplus_chg_is_wls_present()) {
		if (oplus_chg_comm_get_batt_status(chip->comm_ocm) == POWER_SUPPLY_STATUS_FULL) {
			chip->batt_full = true;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "in wireless charging\n");
		return;
	}

	if (oplus_pps_get_chg_status() == PPS_CHARGERING) {
		return;
	}
	if (oplus_ufcs_get_chg_status() == UFCS_CHARGERING) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == false) {
		is_batt_full = 0;
		fastchg_present_wait_count = 0;
		pps_to_ffc_full_count = 0;
	} else {
		if (((oplus_vooc_get_fastchg_to_normal() == true) || (oplus_vooc_get_fastchg_to_warm() == true) ||
		     (oplus_pps_get_ffc_started() == true) || (oplus_ufcs_get_ffc_started() == true)) &&
		    (fastchg_present_wait_count <= FULL_DELAY_COUNTS)) {
			chg_err("fastchg_present_wait_count = %d, pps_to_ffc_full_count = %d,chip->batt_volt = %d\n",
				fastchg_present_wait_count, pps_to_ffc_full_count, chip->batt_volt);
			is_batt_full = 0;
			fastchg_present_wait_count++;

			if (fastchg_present_wait_count == FULL_DELAY_COUNTS)
				chip->waiting_for_ffc = false;

			if ((oplus_pps_get_ffc_started() == true || oplus_ufcs_get_ffc_started() == true || chip->full_pre_ffc_judge) &&
			    (chip->fastchg_to_ffc == false)) {
				if ((oplus_is_pps_charging() == true && chip->batt_volt >= oplus_pps_get_ffc_vth()) ||
				    (oplus_is_ufcs_charging() == true && chip->batt_volt >= oplus_ufcs_get_ffc_vth()) ||
				    oplus_chg_allow_skip_ffc(chip))
					pps_to_ffc_full_count++;
				if ((fastchg_present_wait_count == FULL_DELAY_COUNTS) &&
				    (pps_to_ffc_full_count == FULL_DELAY_COUNTS) &&
				    ((!chip->full_pre_ffc_judge) ||(chip->full_pre_ffc_judge &&
				    chip->chg_ops->get_charging_enable() == false))) {
					oplus_chg_ffc_variable_reset(chip);
					chip->charging_state = CHARGING_STATUS_FULL;
				}
			}

			if (fastchg_present_wait_count == FULL_DELAY_COUNTS &&
			    chip->chg_ops->get_charging_enable() == false &&
			    chip->charging_state != CHARGING_STATUS_FULL &&
			    chip->charging_state != CHARGING_STATUS_FAIL) {
				if (chip->ffc_support && chip->ffc_temp_status != FFC_TEMP_STATUS__HIGH &&
				    chip->ffc_temp_status != FFC_TEMP_STATUS__LOW &&
				    (oplus_voocphy_get_btb_temp_over() != true &&
				    oplus_vooc_get_btb_temp_over() != true)) {
					if (chip->vbatt_num == 2 && chip->dual_ffc == false) {
						oplus_chg_turn_on_ffc2(chip);
					} else {
						oplus_chg_turn_on_ffc1(chip);
					}
				} else {
					if (oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
						oplus_pps_get_support_type() == PPS_SUPPORT_3CP ||
					    oplus_ufcs_get_support_type() != UFCS_SUPPORT_NOT)
						oplus_chg_ffc_variable_reset(chip);
					if (chip->tbatt_status != BATTERY_STATUS__REMOVED &&
					    chip->tbatt_status != BATTERY_STATUS__LOW_TEMP &&
					    chip->tbatt_status != BATTERY_STATUS__HIGH_TEMP) {
						oplus_chg_turn_on_charging(chip);
					}
				}
			}
		} else {
			is_batt_full = chip->chg_ops->read_full();
			chip->hw_full = is_batt_full;
			fastchg_present_wait_count = 0;
			chip->waiting_for_ffc = false;
			pps_to_ffc_full_count = 0;
		}
	}
	if (oplus_chg_check_ffc_status(chip) == false) {
		return;
	}

	if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL)
		oplus_chg_track_record_ffc_end_info();

	if (chip->wireless_support == true && oplus_wpc_get_ffc_charging() == true) {
		if (chip->batt_volt > chip->limits.normal_vfloat_sw_limit)
			charger_xlog_printk(CHG_LOG_CRTI, "in wpc ffc charging\n");
		return;
	}

	if ((is_batt_full == 1) || (chip->charging_state == CHARGING_STATUS_FULL) ||
	    oplus_chg_check_vbatt_is_full_by_sw(chip)) {
		charger_xlog_printk(CHG_LOG_CRTI, "is_batt_full : %d,  chip->charging_state= %d\n", is_batt_full,
				    chip->charging_state);
		if (oplus_get_flash_screen_ctrl() == true) {
			if (chip->batt_volt > 4350) {
				chip->real_batt_full = true;
				if (chip->led_on) {
					oplus_chg_suspend_charger();
					chip->limits.recharge_mv = 60;
				} else {
					chip->chg_ops->charger_unsuspend();
					chip->limits.recharge_mv = 100;
				}
			} else {
				if (chip->batt_volt < 4300) {
					chip->real_batt_full = false;
					chip->chg_ops->charger_unsuspend();
					chip->limits.recharge_mv = 100;
				}
			}
		}

		oplus_chg_full_action(chip);
		if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP ||
		    chip->tbatt_status == BATTERY_STATUS__COOL_TEMP ||
		    chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP ||
		    chip->tbatt_status == BATTERY_STATUS__NORMAL ||
		    ((chip->support_3p6_standard) && (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP))) {
			oplus_gauge_set_batt_full(true);
		}

		if ((chip->recharge_after_full == true || chip->vbatt_num == 2) &&
		    (chip->tbatt_when_full <= 450 && chip->tbatt_when_full >= 0) &&
		    (chip->temperature <= 350 && chip->temperature >= 0)) {
			if (chip->dod0_counts == DOD0_COUNTS) {
				if (chip->vbatt_num == 2) {
					oplus_gauge_update_battery_dod0();
					charger_xlog_printk(CHG_LOG_CRTI,
							    "oplus_chg_check_status_full,dod0_counts = %d\n",
							    chip->dod0_counts);
				}
				if ((chip->recharge_after_full == true && chip->recharge_after_ffc == true &&
				     oplus_get_flash_screen_ctrl() == false) ||
				    (oplus_get_flash_screen_ctrl() == true && chip->chg_ops->charger_suspend_check &&
				     chip->chg_ops->charger_suspend_check() == false)) {
					chip->in_rechging = true;
					chip->sw_full_count = 0;
					chip->sw_full = false;
					chip->recharge_after_ffc = false;
					chip->real_batt_full = false;
					oplus_chg_voter_charging_start(chip, CHG_STOP_VOTER__FULL);
					charger_xlog_printk(CHG_LOG_CRTI, "recharge after full, dod0_counts = %d\n",
							    chip->dod0_counts);
				}
				chip->dod0_counts = DOD0_COUNTS + 1;
			}
		}
	} else if (chip->charging_state == CHARGING_STATUS_FAIL) {
		oplus_chg_fail_action(chip);
	} else {
		chip->charging_state = CHARGING_STATUS_CCCV;
	}

	if (chip->batt_full && chip->mmi_chg) {
		if (!chip->authenticate || !chip->hmac) {
			recharging_vol = chip->limits.non_standard_vfloat_mv - 400;
		} else if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
			recharging_vol = oplus_chg_get_float_voltage(chip) - 300;
		} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
			recharging_vol = oplus_chg_get_float_voltage(chip) - 200;
		} else {
			recharging_vol = oplus_chg_get_float_voltage(chip);
			if (recharging_vol > chip->limits.temp_normal_vfloat_mv)
				recharging_vol = chip->limits.temp_normal_vfloat_mv;
			recharging_vol = recharging_vol - chip->limits.recharge_mv;
		}

		if (last_recharging_vol != recharging_vol)
			chip->full_data.clear_full_check_count = 0;

		last_recharging_vol = recharging_vol;

		if (chip->batt_volt < (recharging_vol - CLEAR_FULL_VOLT_THD))
			chip->full_data.clear_full_check_count++;
		else
			chip->full_data.clear_full_check_count = 0;

		if (chip->full_data.clear_full_check_count > CLEAR_FULL_CNT) {
			chg_err("soc %d %d %d vbat %d < %d clear full\n",
				chip->soc, chip->smooth_soc, chip->ui_soc,
				chip->batt_volt, (recharging_vol - CLEAR_FULL_VOLT_THD));
			if (chip->ui_soc == OPLUS_FULL_SOC)
				chip->ui_soc--;
			chip->full_data.clear_full_check_count = 0;
			chip->sw_full_count = 0;
			chip->sw_full = false;
			chip->full_data.vbat_counts_hw = 0;
			chip->full_data.sub_vbat_counts_hw = 0;
			chip->full_data.hw_full = false;
			chip->full_data.sub_hw_full = false;
			chip->hw_full_by_sw = false;
			chip->sw_sub_batt_full_count = 0;
			chip->sw_sub_batt_full = false;
			chip->hw_sub_batt_full_by_sw = false;
			chip->batt_full = false;
			chip->real_batt_full = false;
			chip->tbatt_when_full = chip->temperature;
			chip->in_rechging = false;
			oplus_gauge_set_batt_full(false);
		}
	} else {
		chip->full_data.clear_full_check_count = 0;
	}
}

static void oplus_chg_kpoc_power_off_check(struct oplus_chg_chip *chip)
{
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
	if (chip->boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT ||
	    chip->boot_mode == LOW_POWER_OFF_CHARGING_BOOT) { /*vbus < 2.5V*/
		if ((chip->chg_ops->check_chrdet_status() == false) && (chip->charger_volt < 2500)) {
			msleep(3000);
			charger_xlog_printk(CHG_LOG_CRTI, "pmic_thread_kthread]Unplug Charger/USB double check!\n");
			if ((chip->chg_ops->check_chrdet_status() == false) &&
			    (chip->chg_ops->get_charger_volt() < 2500)) {
				if ((oplus_vooc_get_fastchg_to_normal() == false) &&
				    (oplus_vooc_get_fastchg_to_warm() == false) &&
				    (oplus_vooc_get_adapter_update_status() != ADAPTER_FW_NEED_UPDATE) &&
				    (oplus_vooc_get_btb_temp_over() == false)) {
					charger_xlog_printk(CHG_LOG_CRTI, "[pmic_thread_kthread]Unplug Charger/USB \
						In Kernel Power Off Charging Mode Shutdown OS!\n");
					chip->chg_ops->set_power_off();
				}
			}
		}
	}
#endif
}

static void oplus_chg_print_log(struct oplus_chg_chip *chip)
{
	if (chip->vbatt_num == 1) {
		charger_xlog_printk(CHG_LOG_CRTI,
			"CHGR[ %d / %d / %d / %d / %d  / %d ], "
			"BAT[ %d / %d / %d / %d / %d / %d ], "
			"GAUGE[ %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d / %d ], "
			"STATUS[ 0x%x / %d / %d / %d / %d / 0x%x ], "
			"OTHER[ %d / %d / %d / %d / %d / %d / %d / %d / %d ], "
			"VOOCPHY[ %d / %d / %d / %d / %d / 0x%0x ]\n",
			chip->charger_exist, chip->charger_type, chip->charger_volt, chip->prop_status, chip->boot_mode, chip->charger_subtype,
			chip->batt_exist, chip->batt_full, chip->chging_on, chip->in_rechging, chip->charging_state,
			chip->total_time, chip->temperature, chip->tbatt_temp, chip->batt_volt, chip->batt_volt_min,
			chip->icharging, chip->ibus, chip->soc, chip->ui_soc, chip->soc_load, chip->batt_rm,
			oplus_gauge_get_batt_fc(), oplus_gauge_get_batt_qm(), oplus_gauge_get_batt_pd(),
			oplus_gauge_get_batt_rcu(), oplus_gauge_get_batt_rcf(), oplus_gauge_get_batt_fcu(),
			oplus_gauge_get_batt_fcf(), oplus_gauge_get_batt_sou(), oplus_gauge_get_batt_do0(),
			oplus_gauge_get_batt_doe(), oplus_gauge_get_batt_trm(), oplus_gauge_get_batt_pc(),
			oplus_gauge_get_batt_qs(), chip->vbatt_over, chip->chging_over_time, chip->vchg_status,
			chip->tbatt_status, chip->stop_voter, chip->notify_code, chip->otg_switch, chip->mmi_chg,
			chip->boot_reason, chip->boot_mode, chip->chargerid_volt, chip->chargerid_volt_got,
			chip->shell_temp, chip->subboard_temp, oplus_is_vooc_project(),
			oplus_voocphy_get_fastchg_start(), oplus_voocphy_get_fastchg_ing(),
			oplus_voocphy_get_fastchg_dummy_start(), oplus_voocphy_get_fastchg_to_normal(),
			oplus_voocphy_get_fastchg_to_warm(), oplus_voocphy_get_fast_chg_type(), oplus_voocphy_get_fastchg_commu_ing());
	} else {
		if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
			oplus_gauge_dump_register();
		}
		charger_xlog_printk(CHG_LOG_CRTI,
			"CHGR[ %d / %d / %d / %d / %d / %d ], "
			"BAT[ %d / %d / %d / %d / %d / %d ], "
			"GAUGE[ %d / %d / %d / %d / %d / %d / %d / %d / %d / %d ], "
			"STATUS[ 0x%x / %d / %d / %d / %d / 0x%x / %d ], "
			"OTHER[ %d / %d / %d / %d / %d / %d / %d / %d / %d / %d ], "
			"VOOCPHY[ %d / %d / %d / %d / %d / 0x%0x ]\n",
			chip->charger_exist, chip->charger_type, chip->charger_volt, chip->prop_status, chip->boot_mode, chip->charger_subtype,
			chip->batt_exist, chip->batt_full, chip->chging_on,
			chip->in_rechging, chip->charging_state, chip->total_time, chip->temperature,
			chip->tbatt_temp, chip->batt_volt, chip->batt_volt_min, chip->icharging, chip->ibus,
			chip->soc, chip->ui_soc, chip->soc_load, chip->batt_rm, chip->vbatt_over,
			chip->chging_over_time, chip->vchg_status, chip->tbatt_status, chip->stop_voter,
			chip->notify_code, chip->usb_status, chip->otg_online, chip->otg_switch,
			chip->mmi_chg, chip->boot_reason, chip->boot_mode, chip->chargerid_volt,
			chip->chargerid_volt_got, chip->shell_temp, chip->subboard_temp, oplus_is_vooc_project(),
			oplus_voocphy_get_fastchg_start(), oplus_voocphy_get_fastchg_ing(),
			oplus_voocphy_get_fastchg_dummy_start(), oplus_voocphy_get_fastchg_to_normal(),
			oplus_voocphy_get_fastchg_to_warm(), oplus_voocphy_get_fast_chg_type());
	}

	if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
		if (chip->tbatt_temp <= -400) {
			chip->gauge_iic_err = true;
			oplus_chg_get_curr_time_ms(&chip->gauge_iic_err_time_record);
		} else {
			if (chip->gauge_iic_err == true) {
				unsigned long curr_time = 0;
				oplus_chg_get_curr_time_ms(&curr_time);
				chip->gauge_iic_err_time_record = curr_time - chip->gauge_iic_err_time_record;
				chip->gauge_iic_err = false;
			}
		}
	}

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
		oplus_vooc_print_log();
	}
#endif
	print_voocphy_log_buf();
	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
		oplus_pps_print_log();
		oplus_ufcs_print_log();
	}
}

static void oplus_chg_print_bcc_log(struct oplus_chg_chip *chip)
{
	if (oplus_pps_get_pps_fastchg_started()) {
		oplus_chg_get_battery_data(chip);
		oplus_chg_get_charger_voltage();
		oplus_chg_battery_update_status(chip);
	}
	if (oplus_vooc_get_fastchg_started() &&
	    oplus_chg_get_voocphy_support() == ADSP_VOOCPHY)
		oplus_pps_read_ibus();
		chg_err("BCC[%d / %d / %d / %d / %d / %d / %d / %d]\n",
			    chip->soc, chip->ui_soc, chip->smooth_soc,
			    chip->batt_volt, chip->icharging, chip->temperature,
			    chip->charger_volt, chip->pps_force_svooc);
}

int oplus_chg_bcc_monitor_common(void *data)
{
	struct oplus_chg_chip *chip = (struct oplus_chg_chip *)data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->oplus_bcc_wq, chip->charger_exist);
		oplus_chg_print_bcc_log(chip);
		msleep(2000);
	}
	return 0;
}

static void oplus_chg_bcc_thread_init(void)
{
	oplus_bcc_kthread = kthread_run(oplus_chg_bcc_monitor_common, g_charger_chip, "bcc_kthread");
	if (IS_ERR(oplus_bcc_kthread)) {
		chg_err("failed to cread oplus_bcc_kthread\n");
	}
}

#define CHARGER_ABNORMAL_DETECT_TIME 24

static void oplus_chg_critical_log(struct oplus_chg_chip *chip)
{
	static int chg_abnormal_count = 0;

	if (chip->charger_exist) {
		if (chip->stop_voter == 0 && chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP && chip->soc <= 75 &&
		    chip->icharging >= -20) {
			chg_abnormal_count++;
			if (chg_abnormal_count >= CHARGER_ABNORMAL_DETECT_TIME) {
				chg_abnormal_count = CHARGER_ABNORMAL_DETECT_TIME;
				charger_abnormal_log = CRITICAL_LOG_UNABLE_CHARGING;
			}
			charger_xlog_printk(CHG_LOG_CRTI, " unable charging, count=%d, charger_abnormal_log=%d\n",
					    chg_abnormal_count, charger_abnormal_log);
		} else {
			chg_abnormal_count = 0;
		}
		if ((chip->stop_voter & CHG_STOP_VOTER__BATTTEMP_ABNORMAL) == CHG_STOP_VOTER__BATTTEMP_ABNORMAL) {
			charger_abnormal_log = CRITICAL_LOG_BATTTEMP_ABNORMAL;
		} else if ((chip->stop_voter & CHG_STOP_VOTER__VCHG_ABNORMAL) == CHG_STOP_VOTER__VCHG_ABNORMAL) {
			charger_abnormal_log = CRITICAL_LOG_VCHG_ABNORMAL;
		} else if ((chip->stop_voter & CHG_STOP_VOTER__VBAT_TOO_HIGH) == CHG_STOP_VOTER__VBAT_TOO_HIGH) {
			charger_abnormal_log = CRITICAL_LOG_VBAT_TOO_HIGH;
		} else if ((chip->stop_voter & CHG_STOP_VOTER__MAX_CHGING_TIME) == CHG_STOP_VOTER__MAX_CHGING_TIME) {
			charger_abnormal_log = CRITICAL_LOG_CHARGING_OVER_TIME;
		} else if ((chip->stop_voter & CHG_STOP_VOTER__VBAT_OVP) == CHG_STOP_VOTER__VBAT_OVP) {
			charger_abnormal_log = CRITICAL_LOG_VBAT_OVP;
		} else {
			/*do nothing*/
		}
	} else if (oplus_vooc_get_btb_temp_over() == true || oplus_vooc_get_fastchg_to_normal() == true) {
		/*Do not clear 0x5d and 0x59*/
		charger_xlog_printk(CHG_LOG_CRTI, " btb_temp_over or fastchg_to_normal, charger_abnormal_log=%d\n",
				    charger_abnormal_log);
	} else {
		charger_abnormal_log = 0;
	}
}

#define OPLUS_CHOOSE_CURVE_COUNT (10)
static void oplus_chg_other_thing(struct oplus_chg_chip *chip)
{
	static unsigned long start_chg_time = 0;
	unsigned long cur_chg_time = 0;
	static int choose_curve_count = OPLUS_CHOOSE_CURVE_COUNT;

	if (oplus_vooc_get_fastchg_started() == false) {
		chip->chg_ops->kick_wdt();
		chip->chg_ops->dump_registers();
	}
	if (chip->charger_exist) {
		if (chgr_dbg_total_time != 0) {
			chip->total_time = chgr_dbg_total_time;
		} else {
			if (!chip->total_time) {
				oplus_chg_get_curr_time_ms(&start_chg_time);
				start_chg_time = start_chg_time / 1000;
				chip->total_time += OPLUS_CHG_UPDATE_INTERVAL_SEC;
			} else {
				oplus_chg_get_curr_time_ms(&cur_chg_time);
				cur_chg_time = cur_chg_time / 1000;
				chip->total_time = OPLUS_CHG_UPDATE_INTERVAL_SEC + cur_chg_time - start_chg_time;
			}
		}
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
		    oplus_voocphy_get_external_gauge_support() == 0 && choose_curve_count > 0) {
			oplus_chg_platform_gauge_choose_curve();
			choose_curve_count = choose_curve_count - 1;
		}
#endif
	}

	if (!chip->charger_exist) {
		choose_curve_count = OPLUS_CHOOSE_CURVE_COUNT;
	}
	oplus_chg_track_comm_monitor();
	if (oplus_pps_get_support_type() == PPS_SUPPORT_2CP || oplus_pps_get_support_type() == PPS_SUPPORT_3CP)
		oplus_chg_debug_chg_monitor(chip);
	oplus_chg_print_log(chip);
	oplus_chg_critical_log(chip);
#ifndef CONFIG_OPLUS_CHARGER_MTK
#ifndef OPLUS_CUSTOM_OP_DEF
#ifndef WPC_NEW_INTERFACE
	if (chip->wireless_support) {
		oplus_wpc_print_log();
		chargepump_print_log();
	}
#endif
#endif
#endif
}

#define IBATT_COUNT 10

static void oplus_chg_ibatt_check_and_set(struct oplus_chg_chip *chip)
{
	static int average_current = 0;
	static int ibatt_count = 0;
	static int current_adapt = 0;
	static int pre_tbatt_status = BATTERY_STATUS__INVALID;
	static int fail_count = 0;
	bool set_current_flag = false;
	int recharge_volt = 0;
	int current_limit = 0;
	int current_init = 0;
	int threshold = 0;
	int current_step = 0;

	if ((chip->chg_ops->need_to_check_ibatt && chip->chg_ops->need_to_check_ibatt() == false) ||
	    !chip->chg_ops->need_to_check_ibatt) {
		return;
	}
	if (!chip->charger_exist || (oplus_vooc_get_fastchg_started() == true)) {
		current_adapt = 0;
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (oplus_short_c_batt_is_prohibit_chg(chip)) {
		return;
	}
	if (current_adapt == 0) {
		if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
			current_adapt = chip->limits.temp_little_cold_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
			current_adapt = chip->limits.temp_warm_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__WARM_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
			current_adapt = chip->limits.temp_cold_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__COLD_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
			current_adapt = chip->limits.temp_normal_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__NORMAL;
		} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
			if (chip->batt_volt > 4180) {
				current_adapt = chip->limits.temp_cool_fastchg_current_ma_low;
			} else {
				current_adapt = chip->limits.temp_cool_fastchg_current_ma_high;
			}
			pre_tbatt_status = BATTERY_STATUS__COOL_TEMP;
		} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
			current_adapt = chip->limits.temp_little_cool_fastchg_current_ma;
			pre_tbatt_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
		}
	}
	if (chip->tbatt_status != pre_tbatt_status) {
		current_adapt = 0;
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
		recharge_volt = chip->limits.temp_little_cold_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_little_cold_fastchg_current_ma;
		current_limit = chip->batt_capacity_mah * 15 / 100;
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) {
		recharge_volt = chip->limits.temp_warm_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_warm_fastchg_current_ma;
		current_limit = chip->batt_capacity_mah * 25 / 100;
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
		recharge_volt = chip->limits.temp_cold_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_cold_fastchg_current_ma;
		current_limit = 350;
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__NORMAL) {
		recharge_volt = chip->limits.temp_normal_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_normal_fastchg_current_ma;
		if (chip->vooc_project) {
			current_limit = 2000;
		} else {
			current_limit = chip->batt_capacity_mah * 65 / 100;
		}
		threshold = 70;
	} else if (chip->tbatt_status == BATTERY_STATUS__COOL_TEMP) {
		recharge_volt = chip->limits.temp_cool_vfloat_mv - chip->limits.recharge_mv;
		if (vbatt_higherthan_4180mv) {
			current_init = chip->limits.temp_cool_fastchg_current_ma_low;
			current_limit = chip->batt_capacity_mah * 15 / 100;
		} else {
			current_init = chip->limits.temp_cool_fastchg_current_ma_high;
			current_limit = chip->batt_capacity_mah * 25 / 100;
		}
		threshold = 50;
	} else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {
		recharge_volt = chip->limits.temp_little_cool_vfloat_mv - chip->limits.recharge_mv;
		current_init = chip->limits.temp_little_cool_fastchg_current_ma;
		current_limit = chip->batt_capacity_mah * 45 / 100;
		threshold = 70;
	}
	if (chip->batt_volt > recharge_volt || chip->led_on) {
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		current_step = chip->chg_ops->get_chg_current_step();
	} else {
		current_adapt = 0;
		ibatt_count = 0;
		average_current = 0;
		fail_count = 0;
		return;
	}
	if (chip->icharging < 0) {
		ibatt_count++;
		average_current = average_current + chip->icharging;
	}
	/*charge current larger than limit*/
	if ((-1 * chip->icharging) > current_limit) {
		if (current_adapt > current_init) {
			current_adapt = current_init;
		} else {
			current_adapt -= 2 * current_step;
		}
		set_current_flag = true;
		fail_count++;
	} else if (ibatt_count == IBATT_COUNT) {
		average_current = -1 * average_current / ibatt_count;
		threshold += fail_count * current_step;
		if (average_current < current_limit - threshold) {
			current_adapt += current_step;
			set_current_flag = true;
		} else {
			ibatt_count = 0;
			average_current = 0;
		}
	}
	if (set_current_flag == true) {
		if (current_adapt > (current_limit + 100)) {
			current_adapt = current_limit + 100;
		} else if (current_adapt < 103) { /*(512*20%)*/
			current_adapt = 103;
		}
		charger_xlog_printk(CHG_LOG_CRTI, "charging_current_write_fast[%d] step[%d]\n", current_adapt,
				    current_step);
		chip->chg_ops->charging_current_write_fast(current_adapt);
		chip->pre_charging_current = current_adapt;
		ibatt_count = 0;
		average_current = 0;
	}
}

static void oplus_chg_turn_on_pd(struct oplus_chg_chip *chip)
{
	chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.pd_temp_little_cool_fastchg_current_ma;
	chip->limits.temp_little_cool_fastchg_current_ma_high =
		chip->limits.pd_temp_little_cool_fastchg_current_ma_high;
	chip->limits.temp_little_cool_fastchg_current_ma_low = chip->limits.pd_temp_little_cool_fastchg_current_ma_low;
	chip->limits.temp_normal_fastchg_current_ma = chip->limits.pd_temp_normal_fastchg_current_ma;
	chip->limits.temp_little_cold_fastchg_current_ma_high =
		chip->limits.pd_temp_little_cold_fastchg_current_ma_high;
	chip->limits.temp_little_cold_fastchg_current_ma_low = chip->limits.pd_temp_little_cold_fastchg_current_ma_low;
	chip->limits.temp_cold_fastchg_current_ma_high = chip->limits.pd_temp_cold_fastchg_current_ma_high;
	chip->limits.temp_cold_fastchg_current_ma_low = chip->limits.pd_temp_cold_fastchg_current_ma_low;
	chip->limits.temp_freeze_fastchg_current_ma_high = chip->limits.pd_temp_freeze_fastchg_current_ma_high;
	chip->limits.temp_freeze_fastchg_current_ma_low = chip->limits.pd_temp_freeze_fastchg_current_ma_low;
	chip->limits.temp_cool_fastchg_current_ma_high = chip->limits.pd_temp_cool_fastchg_current_ma_high;
	chip->limits.temp_cool_fastchg_current_ma_low = chip->limits.pd_temp_cool_fastchg_current_ma_low;
	chip->limits.temp_warm_fastchg_current_ma = chip->limits.pd_temp_warm_fastchg_current_ma;
	chip->limits.input_current_charger_ma = chip->limits.pd_input_current_charger_ma;
	oplus_chg_set_charging_current(chip);
	oplus_chg_set_input_current_limit(chip);
	oplus_chg_enable_burst_mode(true);
	oplus_chg_get_charger_voltage();
}

extern void oplus_get_pps_parameters_from_adsp(void);
extern void oplus_pps_get_adapter_status(struct oplus_chg_chip *chip);
static void oplus_chg_pd_stage1(struct oplus_chg_chip *chip)
{
	int i = 0;
	chg_err("chip->pd_svooc = %d,pd_chging = %d, pps_force_svooc = %d\n", chip->pd_svooc, chip->pd_chging,
		chip->pps_force_svooc);
	if (chip->chg_ops->oplus_chg_get_pd_type() > 0 && chip->pd_chging == false) {
		if (chip->pd_svooc == false) {
			if (chip->cool_down_force_5v == false &&
			    (chip->limits.tbatt_pdqc_to_5v_thr < 0 ||
			     (chip->limits.tbatt_pdqc_to_5v_thr > 0 &&
			      chip->temperature <= chip->limits.tbatt_pdqc_to_5v_thr))) {
				chip->chg_ops->oplus_chg_pd_setup();
				oplus_chg_turn_on_pd(chip);
			}
		} else {
			if (oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
			    oplus_pps_get_support_type() == PPS_SUPPORT_3CP) {
#ifndef CONFIG_OPLUS_CHARGER_MTK
				chip->pd_authentication = oplus_pps_get_adsp_authenticate();
				if (chip->pd_authentication == 0) {
					msleep(500);
					i++;
					chip->pd_authentication = oplus_pps_get_adsp_authenticate();
					if (chip->pd_authentication == 0) {
						msleep(500);
						chip->pd_authentication = oplus_pps_get_adsp_authenticate();
						i++;
					}
				} else {
					oplus_get_pps_parameters_from_adsp();
				}
#else
				i = 0;
				oplus_pps_get_adapter_status(chip);
				chip->pd_authentication = oplus_pps_get_authenticate();
#endif
			} else {
				chip->pd_authentication = 0;
			}
		}
		chip->pd_chging = true;
	}
}

static void oplus_chg_pd_stage2(struct oplus_chg_chip *chip)
{
	if (chip->chg_ops->oplus_chg_get_pd_type() > 0 && chip->pd_chging == true) {
		if (chip->pd_svooc == false) {
			msleep(2000);
			if (chip->chg_ops->oplus_chg_get_pd_type() == PD_PPS_ACTIVE &&
			    oplus_pps_check_third_pps_support() == true) {
				if ((oplus_pps_get_chg_status() == PPS_CHECKING) &&
				    (oplus_pps_is_allow_real() == true)) {
					oplus_pps_start(PPS_ADAPTER_THIRD);
				} else if (oplus_pps_get_chg_status() == PPS_CHARGE_END) {
					if (oplus_pps_voter_charging_start()) {
						oplus_pps_start(PPS_ADAPTER_THIRD);
					}
					chg_err(":pps status:PPS_CHARGE_END \n");
				} else {
					/*return;*/
				}
			}
		} else {
			if ((chip->pd_authentication == -ENODATA) &&
			    (oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
				oplus_pps_get_support_type() == PPS_SUPPORT_3CP)) {
#ifndef CONFIG_OPLUS_CHARGER_MTK
				chip->pd_authentication = oplus_pps_get_adsp_authenticate();
				if (chip->pd_authentication) {
					oplus_get_pps_parameters_from_adsp();
				}
#else
				chip->pd_authentication = oplus_pps_get_authenticate();
#endif
			}
			if (chip->pd_authentication == true) {
				if ((oplus_pps_get_chg_status() == PPS_CHECKING)) {
					if (oplus_pps_check_adapter_ability()) {
						chg_err("authen_result = %d, chip->pd_svooc == %d,START pps\n",
							chip->pd_authentication, chip->pd_svooc);
						if (oplus_pps_is_allow_real() == true) {
							oplus_pps_start(PPS_ADAPTER_OPLUS_V2);
						} else {
							oplus_pps_set_pps_dummy_started(true);
						}
					} else {
						chip->pps_force_svooc = true;
						oplus_adsp_voocphy_reset();
						chg_err("authen_result = %d, chip->pd_svooc == %d,SVOOC\n",
							chip->pd_authentication, chip->pd_svooc);
					}
				} else if (oplus_pps_get_chg_status() == PPS_CHARGE_END) {
					if (oplus_pps_voter_charging_start() && (oplus_pps_check_adapter_ability())) {
						oplus_pps_start(PPS_ADAPTER_OPLUS_V2);
					}
					chg_err(":pps status:PPS_CHARGE_END \n");
				} else {
					chg_err("authen_result = %d, chip->pd_svooc == %d,PPS_CHECKING\n",
						chip->pd_authentication, chip->pd_svooc);
				}
			} else {
				chip->pps_force_svooc = true;
				oplus_adsp_voocphy_reset();
				chg_err("authen_result = %d, pd_svooc = %d, do thing return\n", chip->pd_authentication,
					chip->pd_svooc);
			}
		}
	}
}

static void oplus_chg_pps_config(struct oplus_chg_chip *chip)
{
	if (chip->mmi_chg == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, " mmi_chg,return\n");
		return;
	}
	if (chip->chg_ops->check_pdphy_ready && chip->chg_ops->check_pdphy_ready() == false) {
		chg_err("OPLUS CHG PD_PHY NOT READY");
		return;
	}

	if (oplus_get_quirks_plug_status(QUIRKS_STOP_PPS)) {
		chg_err("QUIRKS_STOP_PPS return");
		return;
	}

	if (oplus_pps_get_support_type() == PPS_SUPPORT_NOT) {
		return;
	}
	if (chip->pps_force_svooc == true) {
		chg_err("pps_force_svooc return");
		return;
	}
	if (oplus_is_ufcs_charging() == true) {
		chg_err(" oplus_is_ufcs_charging\n");
		return;
	}
	if ((!chip->authenticate) || (!chip->hmac)) {
		charger_xlog_printk(CHG_LOG_CRTI, "non authenticate or hmac,switch return\n");
		return;
	}
	if (chip->pd_wait_svid == true && chip->pd_svooc == false) {
		msleep(50);
		chip->pd_wait_svid = false;
	}

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		chip->pd_chging = false;
		return;
	}
	if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
	    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC) {
		return;
	}
	if (!chip->chg_ops->oplus_chg_pd_setup || !chip->chg_ops->oplus_chg_get_pd_type) {
		return;
	}

	if (chip->dual_charger_support || is_vooc_support_single_batt_svooc() == true ||
	    (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) ||
	     oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
	     oplus_pps_get_support_type() == PPS_SUPPORT_3CP) {
		if (chip->pd_adapter_support_9v) {
			if (chip->charger_volt > 7500)
				chip->pd_chging = true;
			else
				chip->pd_chging = false;
		}
	}

	if (oplus_pps_get_ffc_started() == true) {
		oplus_chg_unsuspend_charger();
		return;
	}

	oplus_chg_pd_stage1(chip);

	if (oplus_pps_get_chg_status() == PPS_NOT_SUPPORT) {
		return;
	}

	oplus_chg_pd_stage2(chip);
}

static void oplus_chg_pd_config(struct oplus_chg_chip *chip)
{
	int pd_type = 0;
	if ((!chip->authenticate) || (!chip->hmac)) {
		charger_xlog_printk(CHG_LOG_CRTI, "non authenticate or hmac,switch return\n");
		return;
	}
	if (oplus_pps_get_support_type() != PPS_SUPPORT_NOT) {
		return;
	}
	if (chip->chg_ops->get_charger_subtype)
		chip->charger_subtype = chip->chg_ops->get_charger_subtype();

	if (chip->chg_ops->check_pdphy_ready && chip->chg_ops->check_pdphy_ready() == false) {
		chg_err("OPLUS CHG PD_PHY NOT READY");
		return;
	}
	if (chip->pd_wait_svid == true && chip->pd_svooc == false) {
		msleep(50);
		chip->pd_wait_svid = false;
	}
	if (chip->pd_svooc == true) {
		return;
	}
	if (chip->pps_force_svooc == true) {
		return;
	}

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		chip->pd_chging = false;
		return;
	}
	if (oplus_is_ufcs_charging() == true) {
		chg_err("oplus_is_ufcs_charging\n");
		return;
	}
	if (chip->pd_disable == true) {
		return;
	}
	if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
	    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC) {
		return;
	}
	if (!chip->chg_ops->oplus_chg_pd_setup || !chip->chg_ops->oplus_chg_get_pd_type) {
		return;
	}

	if (chip->dual_charger_support || is_vooc_support_single_batt_svooc() == true ||
	    (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY)) {
		if (chip->pd_adapter_support_9v) {
			if (chip->charger_volt > 7500)
				chip->pd_chging = true;
			else
				chip->pd_chging = false;
		}
	}

	if (chip->chg_ops->oplus_chg_get_pd_type() > 0) {
		pd_type = chip->chg_ops->oplus_chg_get_pd_type();
		if (chip->pd_chging == false) {
			chip->chg_ops->oplus_chg_pd_setup();
			chip->pd_chging = true;
			chip->limits.temp_little_cool_fastchg_current_ma =
				chip->limits.pd_temp_little_cool_fastchg_current_ma;
			chip->limits.temp_little_cool_fastchg_current_ma_high =
				chip->limits.pd_temp_little_cool_fastchg_current_ma_high;
			chip->limits.temp_little_cool_fastchg_current_ma_low =
				chip->limits.pd_temp_little_cool_fastchg_current_ma_low;
			chip->limits.temp_normal_fastchg_current_ma = chip->limits.pd_temp_normal_fastchg_current_ma;
			chip->limits.temp_little_cold_fastchg_current_ma_high =
				chip->limits.pd_temp_little_cold_fastchg_current_ma_high;
			chip->limits.temp_little_cold_fastchg_current_ma_low =
				chip->limits.pd_temp_little_cold_fastchg_current_ma_low;
			chip->limits.temp_cold_fastchg_current_ma_high =
				chip->limits.pd_temp_cold_fastchg_current_ma_high;
			chip->limits.temp_cold_fastchg_current_ma_low =
				chip->limits.pd_temp_cold_fastchg_current_ma_low;
			chip->limits.temp_freeze_fastchg_current_ma_high =
				chip->limits.pd_temp_freeze_fastchg_current_ma_high;
			chip->limits.temp_freeze_fastchg_current_ma_low =
				chip->limits.pd_temp_freeze_fastchg_current_ma_low;
			chip->limits.temp_cool_fastchg_current_ma_high =
				chip->limits.pd_temp_cool_fastchg_current_ma_high;
			chip->limits.temp_cool_fastchg_current_ma_low =
				chip->limits.pd_temp_cool_fastchg_current_ma_low;
			chip->limits.temp_warm_fastchg_current_ma = chip->limits.pd_temp_warm_fastchg_current_ma;
			chip->limits.input_current_charger_ma = chip->limits.pd_input_current_charger_ma;
			oplus_chg_set_charging_current(chip);
			oplus_chg_set_input_current_limit(chip);
			oplus_chg_enable_burst_mode(true);
			oplus_chg_get_charger_voltage();
		}
	} else {
		chg_err("oplus_chg_pd_config:get pd_type failed\n");
	}
}

static void oplus_chg_pdqc_to_normal(struct oplus_chg_chip *chip)
{
	int ret = 0;
	static int pdqc_9v = false;

	if (!chip->chg_ops->get_charger_subtype) {
		return;
	}
	if (!chip->chg_ops->oplus_chg_pd_setup || !chip->chg_ops->set_qc_config) {
		return;
	}
	if (oplus_pps_get_chg_status() == PPS_CHARGERING) {
		return;
	}
	if (chip->limits.vbatt_pdqc_to_5v_thr < 0) {
		return;
	}

	if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
	     oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY) &&
	    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC) {
		return;
	}
	if (oplus_chg_show_vooc_logo_ornot() == true) {
		return;
	}
	if (chip->charger_volt > 7500) {
		pdqc_9v = true;
	} else {
		pdqc_9v = false;
	}
	if (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) {
		if ((chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr || chip->cool_down_force_5v ||
		     (chip->limits.tbatt_pdqc_to_5v_thr > 0 && chip->temperature > chip->limits.tbatt_pdqc_to_5v_thr)) &&
		    pdqc_9v == true) {
			ret = chip->chg_ops->oplus_chg_pd_setup();
			if (ret >= 0) {
				pdqc_9v = false;
				chip->limits.temp_normal_fastchg_current_ma =
					chip->limits.default_temp_normal_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma =
					chip->limits.default_temp_little_cool_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma_high =
					chip->limits.default_temp_little_cool_fastchg_current_ma_high;
				chip->limits.temp_little_cool_fastchg_current_ma_low =
					chip->limits.default_temp_little_cool_fastchg_current_ma_low;
				chip->limits.temp_little_cold_fastchg_current_ma_high =
					chip->limits.default_temp_little_cold_fastchg_current_ma_high;
				chip->limits.temp_little_cold_fastchg_current_ma_low =
					chip->limits.default_temp_little_cold_fastchg_current_ma_low;
				chip->limits.temp_cold_fastchg_current_ma_high =
					chip->limits.default_temp_cold_fastchg_current_ma_high;
				chip->limits.temp_cold_fastchg_current_ma_low =
					chip->limits.default_temp_cold_fastchg_current_ma_low;
				chip->limits.temp_cool_fastchg_current_ma_high =
					chip->limits.default_temp_cool_fastchg_current_ma_high;
				chip->limits.temp_cool_fastchg_current_ma_low =
					chip->limits.default_temp_cool_fastchg_current_ma_low;
				chip->limits.temp_warm_fastchg_current_ma =
					chip->limits.default_temp_warm_fastchg_current_ma;
				if (!chip->cool_down || chip->limits.input_current_charger_ma >
								chip->limits.default_input_current_charger_ma) {
					chip->limits.input_current_charger_ma =
						chip->limits.default_input_current_charger_ma;
				}
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
				oplus_chg_get_charger_voltage();
			}
			oplus_chg_enable_burst_mode(true);
		}
	} else if (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
		if ((chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr || chip->cool_down_force_5v ||
		     (chip->limits.tbatt_pdqc_to_5v_thr > 0 && chip->temperature > chip->limits.tbatt_pdqc_to_5v_thr)) &&
		    pdqc_9v == true) {
			ret = chip->chg_ops->set_qc_config();
			if (ret >= 0) {
				pdqc_9v = false;
				chip->limits.temp_normal_fastchg_current_ma =
					chip->limits.default_temp_normal_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma =
					chip->limits.default_temp_little_cool_fastchg_current_ma;
				chip->limits.temp_little_cool_fastchg_current_ma_high =
					chip->limits.default_temp_little_cool_fastchg_current_ma_high;
				chip->limits.temp_little_cool_fastchg_current_ma_low =
					chip->limits.default_temp_little_cool_fastchg_current_ma_low;
				chip->limits.temp_little_cold_fastchg_current_ma_high =
					chip->limits.default_temp_little_cold_fastchg_current_ma_high;
				chip->limits.temp_little_cold_fastchg_current_ma_low =
					chip->limits.default_temp_little_cold_fastchg_current_ma_low;
				chip->limits.temp_cold_fastchg_current_ma_high =
					chip->limits.default_temp_cold_fastchg_current_ma_high;
				chip->limits.temp_cold_fastchg_current_ma_low =
					chip->limits.default_temp_cold_fastchg_current_ma_low;
				chip->limits.temp_cool_fastchg_current_ma_high =
					chip->limits.default_temp_cool_fastchg_current_ma_high;
				chip->limits.temp_cool_fastchg_current_ma_low =
					chip->limits.default_temp_cool_fastchg_current_ma_low;
				chip->limits.temp_warm_fastchg_current_ma =
					chip->limits.default_temp_warm_fastchg_current_ma;
				if (!chip->cool_down || chip->limits.input_current_charger_ma >
								chip->limits.default_input_current_charger_ma) {
					chip->limits.input_current_charger_ma =
						chip->limits.default_input_current_charger_ma;
				}
				oplus_chg_set_charging_current(chip);
				oplus_chg_set_input_current_limit(chip);
				oplus_chg_get_charger_voltage();
			}
			oplus_chg_enable_burst_mode(true);
		}
	}
}

#define QC_CHARGER_VOLTAGE_HIGH 7500
#define QC_ABNORMAL_CHECK_COUNT 3

static void oplus_chg_qc_config(struct oplus_chg_chip *chip)
{
	static bool qc_chging = false;
	int subtype = CHARGER_SUBTYPE_DEFAULT;
	int ret = 0;

	if (chip == NULL) {
		chg_err("chip is NULL\n");
		return;
	}

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		qc_chging = false;
		return;
	}

	if (!chip->chg_ops || !chip->chg_ops->get_charger_subtype)
		return;

	subtype = chip->chg_ops->get_charger_subtype();
	chip->charger_subtype = subtype;
	chg_err("chip->charger_type[%d], subtype[%d]\n", chip->charger_type, subtype);

	if (!chip->chg_ops->set_qc_config)
		return;

	if (subtype != CHARGER_SUBTYPE_QC) {
		qc_chging = false;
		return;
	}

	if (chip->dual_charger_support) {
		if (chip->charger_volt <= QC_SWITCH_VOL) {
			qc_chging = false;
		}
	}

	if (!chip->cool_down_force_5v && (chip->vbatt_num == 2 || chip->limits.tbatt_pdqc_to_9v_thr < 0 ||
					  chip->temperature < chip->limits.tbatt_pdqc_to_9v_thr)) {
		if (qc_chging && (chip->charger_volt < QC_CHARGER_VOLTAGE_HIGH)) {
			chip->qc_abnormal_check_count++;
			if (chip->qc_abnormal_check_count >= QC_ABNORMAL_CHECK_COUNT) {
				if (chip->chg_ops->pdo_5v && chip->qc_abnormal_check_count == QC_ABNORMAL_CHECK_COUNT) {
					chip->chg_ops->pdo_5v();
					chg_err("Abnormal qc adapter.Do not config qc to 9V\n");

					chip->limits.temp_normal_fastchg_current_ma =
						chip->limits.default_temp_normal_fastchg_current_ma;
					chip->limits.temp_little_cool_fastchg_current_ma =
						chip->limits.default_temp_little_cool_fastchg_current_ma;
					chip->limits.temp_little_cool_fastchg_current_ma_high =
						chip->limits.default_temp_little_cool_fastchg_current_ma_high;
					chip->limits.temp_little_cool_fastchg_current_ma_low =
						chip->limits.default_temp_little_cool_fastchg_current_ma_low;
					chip->limits.temp_little_cold_fastchg_current_ma_high =
						chip->limits.default_temp_little_cold_fastchg_current_ma_high;
					chip->limits.temp_little_cold_fastchg_current_ma_low =
						chip->limits.default_temp_little_cold_fastchg_current_ma_low;
					chip->limits.temp_cold_fastchg_current_ma_high =
						chip->limits.default_temp_cold_fastchg_current_ma_high;
					chip->limits.temp_cold_fastchg_current_ma_low =
						chip->limits.default_temp_cold_fastchg_current_ma_low;
					chip->limits.temp_freeze_fastchg_current_ma_high =
						chip->limits.qc_temp_freeze_fastchg_current_ma_high;
					chip->limits.temp_freeze_fastchg_current_ma_low =
						chip->limits.qc_temp_freeze_fastchg_current_ma_low;
					chip->limits.temp_cool_fastchg_current_ma_high =
						chip->limits.default_temp_cool_fastchg_current_ma_high;
					chip->limits.temp_cool_fastchg_current_ma_low =
						chip->limits.default_temp_cool_fastchg_current_ma_low;
					chip->limits.temp_warm_fastchg_current_ma =
						chip->limits.default_temp_warm_fastchg_current_ma;
					chip->limits.input_current_charger_ma =
						chip->limits.default_input_current_charger_ma;
					oplus_chg_set_charging_current(chip);
					oplus_chg_set_input_current_limit(chip);
					oplus_chg_enable_burst_mode(true);
#ifndef CONFIG_OPLUS_CHARGER_MTK
					if (chip->chg_ops->rerun_aicl) {
						chip->chg_ops->rerun_aicl();
						chg_err("Abnormal qc adapter.rerun aicl after set 5V\n");
					}
#endif
				}
				return;
			}
		} else {
			chip->qc_abnormal_check_count = 0;
		}
	}

	if (chip->vbatt_num == 1) {
		if (chip->chg_ops->check_qchv_condition && chip->chg_ops->check_qchv_condition() &&
		    chip->qc_abnormal_check_count < QC_ABNORMAL_CHECK_COUNT && qc_chging &&
		    chip->charger_volt < QC_CHARGER_VOLTAGE_HIGH) {
			chg_err("QC can recovery to HV");
			qc_chging = false;
		}
	}

	if (!qc_chging && chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) {
		ret = chip->chg_ops->set_qc_config();
		if (ret >= 0) {
			qc_chging = true;
			chg_err("QC  config success");
			chip->limits.temp_little_cool_fastchg_current_ma_high =
				chip->limits.qc_temp_little_cool_fastchg_current_ma_high;
			chip->limits.temp_little_cool_fastchg_current_ma_low =
				chip->limits.qc_temp_little_cool_fastchg_current_ma_low;
			chip->limits.temp_little_cool_fastchg_current_ma =
				chip->limits.qc_temp_little_cool_fastchg_current_ma;
			chip->limits.temp_normal_fastchg_current_ma = chip->limits.qc_temp_normal_fastchg_current_ma;
			chip->limits.temp_little_cold_fastchg_current_ma_high =
				chip->limits.qc_temp_little_cold_fastchg_current_ma_high;
			chip->limits.temp_little_cold_fastchg_current_ma_low =
				chip->limits.qc_temp_little_cold_fastchg_current_ma_low;
			chip->limits.temp_cold_fastchg_current_ma_high =
				chip->limits.qc_temp_cold_fastchg_current_ma_high;
			chip->limits.temp_cold_fastchg_current_ma_low =
				chip->limits.qc_temp_cold_fastchg_current_ma_low;
			chip->limits.temp_freeze_fastchg_current_ma_high =
				chip->limits.qc_temp_freeze_fastchg_current_ma_high;
			chip->limits.temp_freeze_fastchg_current_ma_low =
				chip->limits.qc_temp_freeze_fastchg_current_ma_low;
			chip->limits.temp_cool_fastchg_current_ma_high =
				chip->limits.qc_temp_cool_fastchg_current_ma_high;
			chip->limits.temp_cool_fastchg_current_ma_low =
				chip->limits.qc_temp_cool_fastchg_current_ma_low;
			chip->limits.temp_warm_fastchg_current_ma = chip->limits.qc_temp_warm_fastchg_current_ma;
			chip->limits.input_current_charger_ma = chip->limits.qc_input_current_charger_ma;
			oplus_chg_set_input_current_limit(chip);
			oplus_chg_set_charging_current(chip);
			oplus_chg_get_charger_voltage();
		}
		oplus_chg_enable_burst_mode(true);
	}
}

static void oplus_chg_dual_charger_config(struct oplus_chg_chip *chip)
{
	static int enable_slave_cnt = 0;
	static int disable_slave_cnt = 0;

	if (!chip->dual_charger_support) {
		return;
	}

	if (chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		return;
	}

	if (chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_DEFAULT) {
		return;
	}

	if (chip->slave_charger_enable == false) {
		if (chip->icharging < 0 && (chip->icharging * -1) > chip->slave_chg_enable_ma) {
			enable_slave_cnt++;
		} else {
			enable_slave_cnt = 0;
		}

		if (enable_slave_cnt >= 3) {
			chg_err("Enable slave charger!!\n");
			chip->slave_charger_enable = true;
			oplus_chg_set_input_current_limit(chip);
			oplus_chg_set_charging_current(chip);
			enable_slave_cnt = 0;
		}
	} else {
		if (chip->slave_charger_enable == true) {
			if (chip->icharging < 0 && (chip->icharging * -1) < chip->slave_chg_disable_ma) {
				disable_slave_cnt++;
			} else {
				disable_slave_cnt = 0;
			}

			if (disable_slave_cnt >= 3) {
				chg_err("Disable slave charger!!\n");
				chip->slave_charger_enable = false;
				oplus_chg_set_input_current_limit(chip);
				oplus_chg_set_charging_current(chip);
				disable_slave_cnt = 0;
			}
		}
	}
}

static void oplus_parallel_chg_mos_test_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int pre_cool_down = chip->cool_down;
	int sub_current;

	if (!chip) {
		chg_err("chip is NULL\n");
		return;
	}
	chg_err("mos test start!\n");
	chip->mos_test_started = true;
	oplus_voocphy_adjust_current_by_cool_down(MOS_TEST_DEFAULT_COOL_DOWN);
	msleep(1500); /* wait 1.5s until current adjusts */

	if (!atomic_dec_and_test(&chip->mos_lock)) {
		chg_err("mos is changing, wait 500ms!\n");
		atomic_inc(&chip->mos_lock);
		msleep(500);
		if (!atomic_dec_and_test(&chip->mos_lock)) {
			chg_err("set_balance_bat_status error, return!\n");
			atomic_inc(&chip->mos_lock);
			return;
		}
	}
	if ((oplus_switching_get_hw_enable() == MOS_OPEN) ||
	    (chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS8) ||
	    (chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS9)) {
		chg_err("mos: %d, test next time!\n", oplus_switching_get_hw_enable());
		chip->cool_down = pre_cool_down;
		oplus_voocphy_adjust_current_by_cool_down(pre_cool_down);
		atomic_inc(&chip->mos_lock);
		return;
	}
	oplus_switching_hw_enable(0);
	msleep(2000); /* wait 2s until mos open */

	sub_current = oplus_gauge_get_sub_batt_current();
	chg_err("after mos open, sub_current = %d!\n", sub_current);
	chip->cool_down = pre_cool_down;
	oplus_switching_hw_enable(1);
	msleep(1000); /* wait 1s after mos on, make sure guage has updated*/

	atomic_inc(&chip->mos_lock);
	oplus_voocphy_adjust_current_by_cool_down(pre_cool_down);
	if (-sub_current < SUB_BATT_CURRENT_50_MA) {
		chip->mos_test_result = 1;
	}
	chip->mos_test_started = false;
	chg_err("mos test end!\n");
}

static void oplus_chg_reset_adapter_work(struct work_struct *work)
{
	oplus_chg_suspend_charger();
	msleep(1000);
	if (g_charger_chip->mmi_chg) {
		if ((oplus_vooc_get_fastchg_ing() == false || oplus_vooc_get_fastchg_dummy_started() ||
			oplus_vooc_get_fastchg_to_normal() || oplus_vooc_get_fastchg_to_warm()) &&
			!oplus_adsp_voocphy_get_fastchg_start()) {
			oplus_chg_unsuspend_charger();
			msleep(50);
			oplus_vooc_set_ap_clk_high();
			oplus_vooc_reset_mcu();
			mcu_status = 1;
			if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
				oplus_adsp_voocphy_reset();
			}
	    }
	}
}

void oplus_chg_turn_on_charging_in_work(void)
{
	if (g_charger_chip)
		schedule_delayed_work(&g_charger_chip->turn_on_charging_work, 0);
}

static void oplus_chg_turn_on_charging_work(struct work_struct *work)
{
	if (g_charger_chip)
		oplus_chg_turn_on_charging(g_charger_chip);
}

static void oplus_parallel_batt_chg_check_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int target_curr = 0;
	int ibat_status;
	int curr_step = CURR_STEP;
	int batt_temp;
	int sub_batt_temp;

	if (chip->aicl_done && chip->charger_exist && chip->batt_exist &&
	    (CHARGING_STATUS_CCCV == chip->charging_state) && chip->mmi_chg && !oplus_vooc_get_fastchg_started()) {
		batt_temp = oplus_gauge_get_batt_temperature();
		sub_batt_temp = oplus_gauge_get_sub_batt_temperature();
		ibat_status = oplus_chg_is_parellel_ibat_over_spec(batt_temp, sub_batt_temp, &target_curr);
		if (chip->chg_ops->get_chg_current_step)
			curr_step = chip->chg_ops->get_chg_current_step();
		if (curr_step > 0) {
			target_curr = target_curr / curr_step * curr_step;
		}
		chip->batt_target_curr = target_curr;
		if (ibat_status >= MAIN_BATT_OVER_CURR)
			chg_err("charging current over, limit to %d\n", chip->batt_target_curr);
		oplus_chg_set_charging_current(chip);
	} else {
		chip->batt_target_curr = 0;
		chip->pre_charging_current = 0;
	}
	schedule_delayed_work(&chip->parallel_batt_chg_check_work, OPLUS_CHG_UPDATE_INTERVAL(PARALLEL_CHECK_TIME));
}

static void oplus_aging_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, aging_check_work);
	static int chg_count = 1;

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (chip->charger_exist && ((get_eng_version() == HIGH_TEMP_AGING) || oplus_is_ptcrb_version())) {
		chg_count++;
		if (chip->ibus < 1500 && chg_count > 100) {
			charger_xlog_printk(CHG_LOG_CRTI, " PQE: led_on = %d, \
				current_limit = %d, \
				led_temp_status = %d\n",
					    chip->led_on, chip->ibus, chip->led_temp_status);
			chip->chg_ops->input_current_write(OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA);
			chg_count = 0;
		}
	} else
#endif
	{
		chg_count = 0;
	}

	schedule_delayed_work(&chip->aging_check_work, OPLUS_CHG_UPDATE_INTERVAL(OPLUS_CHG_UPDATE_INTERVAL_SEC));
}

static void oplus_comm_fginfo_reset(struct oplus_chg_chip *chip)
{
	chip->fg_soft_reset_done = false;
	chip->fg_check_ibat_cnt = 0;
	chip->fg_soft_reset_fail_cnt = 0;
	cancel_delayed_work_sync(&chip->fg_soft_reset_work);
}

static void oplus_comm_check_fgreset(struct oplus_chg_chip *chip)
{
	bool is_need_check = true;

	if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN || chip->charger_type == POWER_SUPPLY_TYPE_USB ||
	    chip->tbatt_status != BATTERY_STATUS__NORMAL || chip->batt_volt_min < SOFT_REST_VOL_THRESHOLD ||
	    chip->fg_soft_reset_done || chip->vbatt_num != 2)
		is_need_check = false;

	if (oplus_chg_get_voocphy_support() != ADSP_VOOCPHY) {
		if (oplus_vooc_get_allow_reading() != true) {
			is_need_check = false;
		}
	}

	if (!chip->sw_full && !chip->hw_full_by_sw)
		is_need_check = false;

	if (!chip->charger_exist && !oplus_chg_is_wls_present())
		is_need_check = false;

	if (!chip->fg_soft_reset_done && chip->fg_soft_reset_fail_cnt > SOFT_REST_RETRY_MAX_CNT)
		is_need_check = false;

	if (oplus_gauge_afi_update_done() == false) {
		is_need_check = false;
	}

	if (!is_need_check) {
		chip->fg_check_ibat_cnt = 0;
		return;
	}

	schedule_delayed_work(&chip->fg_soft_reset_work, 0);
}

static void oplus_fg_soft_reset_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, fg_soft_reset_work);
	int rc = 0;

	if (chip->vbatt_num == 2 && (chip->soc < SOFT_REST_SOC_THRESHOLD || oplus_gauge_check_rc_sfr())) {
		if (abs(chip->icharging) < SOFT_REST_CHECK_DISCHG_MAX_CUR)
			chip->fg_check_ibat_cnt++;
		else
			chip->fg_check_ibat_cnt = 0;

		if (chip->fg_check_ibat_cnt < SOFT_REST_RETRY_MAX_CNT + 1)
			return;

		rc = oplus_gauge_soft_reset_rc_sfr();
		if (rc == 0) {
			chip->fg_soft_reset_done = true;
			chip->fg_soft_reset_fail_cnt = 0;
		} else if (rc == 1) {
			chip->fg_soft_reset_done = false;
			chip->fg_soft_reset_fail_cnt++;
		}
		chip->fg_check_ibat_cnt = 0;
	} else {
		chip->fg_check_ibat_cnt = 0;
	}

	chg_err("reset_done [%s] ibat_cnt[%d] fail_cnt[%d] \n", chip->fg_soft_reset_done == true ? "true" : "false",
		chip->fg_check_ibat_cnt, chip->fg_soft_reset_fail_cnt);
}

static void oplus_chg_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, update_work);

	oplus_charger_detect_check(chip);
	oplus_chg_get_battery_data(chip);
	oplus_check_battery_vol_diff(chip);
	if (chip->charger_exist) {
		oplus_chg_aicl_check(chip);
		oplus_chg_protection_check(chip);
		oplus_chg_check_tbatt_normal_status(chip);
		oplus_chg_check_status_full(chip);
		oplus_chg_battery_notify_check(chip);
		oplus_comm_check_fgreset(chip);
		oplus_chg_pd_config(chip);
	} else {
		aicl_delay_count = 0;
	}
	oplus_chg_dual_charger_config(chip);
	oplus_chg_qc_config(chip);
	oplus_chg_pdqc_to_normal(chip);
	oplus_chg_ibatt_check_and_set(chip);
	if (chip->shortc_thread)
		wake_up_process(chip->shortc_thread);
	oplus_chg_battery_update_status(chip);
	oplus_chg_kpoc_power_off_check(chip);
	oplus_chg_cool_down_match_err_check(chip);
	oplus_chg_other_thing(chip);
	/* run again after interval */
	if (timer_pending(&chip->update_work.timer) && !delayed_work_pending(&chip->update_work)) {
		mod_delayed_work(system_wq, &chip->update_work,
				 OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	} else {
		schedule_delayed_work(&chip->update_work, OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	}
}
void oplus_chg_cancel_update_work_sync(void)
{
	if (!g_charger_chip) {
		return;
	}

	cancel_delayed_work_sync(&g_charger_chip->update_work);
}

void oplus_chg_restart_update_work(void)
{
	if (!g_charger_chip) {
		return;
	}

	schedule_delayed_work(&g_charger_chip->update_work, 0);
}
bool oplus_chg_wake_update_work(void)
{
	if (!g_charger_chip) {
		chg_err(" g_charger_chip NULL,return\n");
		return true;
	}

	if (g_charger_chip->update_work.work.func) {
		mod_delayed_work(system_wq, &g_charger_chip->update_work, 0);
	}
	return true;
}
EXPORT_SYMBOL(oplus_chg_wake_update_work);

void oplus_chg_input_current_recheck_work(void)
{
	if (!g_charger_chip) {
		chg_err(" g_charger_chip NULL,return\n");
		return;
	}
	oplus_chg_set_input_current_limit(g_charger_chip);
	return;
}

void oplus_chg_reset_adapter(void)
{
	if (!g_charger_chip) {
		return;
	}
	schedule_delayed_work(&g_charger_chip->reset_adapter_work, 0);
}

void oplus_chg_kick_wdt(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		g_charger_chip->chg_ops->kick_wdt();
	}
}

void oplus_chg_dump_registers(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		g_charger_chip->chg_ops->dump_registers();
	}
}

void oplus_chg_disable_charge(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		g_charger_chip->chg_ops->charging_disable();
	}
}

void oplus_chg_deviation_disable_charge(void)
{
	oplus_chg_voter_charging_stop(g_charger_chip, CHG_STOP_VOTER__FULL);
}

void oplus_chg_deviation_enable_charge(void)
{
	oplus_chg_voter_charging_start(g_charger_chip, CHG_STOP_VOTER__FULL);
}

void oplus_chg_enable_charge(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		g_charger_chip->chg_ops->charging_enable();
	}
}

void oplus_chg_suspend_charger(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		g_charger_chip->chg_ops->charger_suspend();
	}
}

bool oplus_chg_check_disable_charger(void)
{
	bool ret = false;

	if (!g_charger_chip) {
		return false;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		ret = g_charger_chip->chg_ops->get_charging_enable();
	}

	return ret;
}

void oplus_chg_unsuspend_charger(void)
{
	if (!g_charger_chip) {
		return;
	}
	if (oplus_vooc_get_allow_reading() == true) {
		g_charger_chip->chg_ops->charger_unsuspend();
	}
}

int oplus_chg_get_batt_volt(void)
{
	if (!g_charger_chip) {
		return 4000;
	} else {
		return g_charger_chip->batt_volt;
	}
}

void oplus_chg_platform_gauge_choose_curve(void)
{
	int type = 0;

	if (!g_charger_chip) {
		return;
	}

	type = oplus_vooc_get_fast_chg_type();
	if (type == 0 && g_charger_chip->chg_ops->get_charger_subtype) {
		type = g_charger_chip->chg_ops->get_charger_subtype();
	}

	if (g_charger_chip->chg_ops->get_platform_gauge_curve)
		g_charger_chip->chg_ops->get_platform_gauge_curve(type);
}

int oplus_chg_get_cool_bat_decidegc(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		return g_charger_chip->limits.cool_bat_decidegc;
	}
}

int oplus_chg_get_little_cool_bat_decidegc(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		return g_charger_chip->limits.little_cool_bat_decidegc;
	}
}

int oplus_chg_get_normal_bat_decidegc(void)
{
	if (!g_charger_chip) {
		return -EINVAL;
	} else {
		return g_charger_chip->limits.normal_bat_decidegc;
	}
}

int oplus_chg_get_icharging(void)
{
	if (!g_charger_chip) {
		return 4000;
	} else {
		return g_charger_chip->icharging;
	}
}

int oplus_chg_get_voocphy_support(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->voocphy_support;
	}
}
EXPORT_SYMBOL(oplus_chg_get_voocphy_support);

int oplus_chg_get_voocphy_support_display_vooc(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->voocphy_support_display_vooc;
	}
}
EXPORT_SYMBOL(oplus_chg_get_voocphy_support_display_vooc);

bool oplus_chg_fg_package_read_support(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->fg_info_package_read_support;
	}
}

int oplus_chg_get_ui_soc(void)
{
	if (!g_charger_chip) {
		return 50;
	} else {
		return g_charger_chip->ui_soc;
	}
}
EXPORT_SYMBOL(oplus_chg_get_ui_soc);

int oplus_chg_get_soc(void)
{
	if (!g_charger_chip) {
		return 50;
	} else {
		return g_charger_chip->soc;
	}
}
EXPORT_SYMBOL(oplus_chg_get_notify_flag);

void oplus_chg_soc_update_when_resume(unsigned long sleep_tm_sec)
{
	int new_soc;
	if (!g_charger_chip) {
		return;
	}

	if (g_charger_chip->nightstandby_support) {
		if (get_soc_feature() && !g_charger_chip->charger_exist) {
			g_charger_chip->modify_soc = oplus_gauge_get_batt_soc();
			chg_err("soc_ajust_feature resume g_charger_chip->modify_soc[%d]\n",
				g_charger_chip->modify_soc);
		} else {
			g_charger_chip->sleep_tm_sec = sleep_tm_sec;
			new_soc = oplus_gauge_get_batt_soc();
			if (new_soc != g_charger_chip->soc) {
				g_charger_chip->smooth_soc -= (g_charger_chip->soc - new_soc);
			}
			g_charger_chip->soc = new_soc;
			if (g_charger_chip->rsd.smooth_switch_v2) {
				oplus_chg_smooth_to_soc_v2(g_charger_chip, false);
			} else if (g_charger_chip->smooth_switch) {
				oplus_chg_smooth_to_soc(g_charger_chip);
			}
			chg_err("soc_ajust_feature resume g_charger_chip->soc[%d]\n", g_charger_chip->soc);
		}
		chg_err("soc_ajust_feature resume g_charger_chip->soc[%d], g_charger_chip->modify_soc[%d]\n",
			g_charger_chip->soc, g_charger_chip->modify_soc);
	} else {
		g_charger_chip->sleep_tm_sec = sleep_tm_sec;
		new_soc = oplus_gauge_get_batt_soc();
		if (new_soc != g_charger_chip->soc) {
			g_charger_chip->smooth_soc -= (g_charger_chip->soc - new_soc);
		}
		g_charger_chip->soc = new_soc;
		if (g_charger_chip->rsd.smooth_switch_v2) {
			oplus_chg_smooth_to_soc_v2(g_charger_chip, false);
		} else if (g_charger_chip->smooth_switch) {
			oplus_chg_smooth_to_soc(g_charger_chip);
		}
	}
	oplus_chg_update_ui_soc(g_charger_chip);
}

void oplus_chg_soc_update(void)
{
	if (!g_charger_chip) {
		return;
	}
	oplus_chg_update_ui_soc(g_charger_chip);
	oplus_chg_debug_set_soc_info(g_charger_chip);
}

int oplus_chg_get_chg_type(void)
{
	if (!g_charger_chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	} else {
		return g_charger_chip->charger_type;
	}
}

int oplus_chg_get_chg_temperature(void)
{
	if (!g_charger_chip) {
		return 250;
	} else {
		return g_charger_chip->temperature;
	}
}

int oplus_chg_get_notify_flag(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->notify_flag;
	}
}

int oplus_is_vooc_project(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->vooc_project;
	}
}
EXPORT_SYMBOL(oplus_is_vooc_project);

int oplus_chg_show_vooc_logo_ornot(void)
{
	if (!g_charger_chip) {
		return 0;
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
	if (g_charger_chip->wireless_support && oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_SVOOC &&
	    g_charger_chip->prop_status != POWER_SUPPLY_STATUS_FULL &&
	    (g_charger_chip->stop_voter == CHG_STOP_VOTER__FULL || g_charger_chip->stop_voter == CHG_STOP_VOTER_NONE))
		return 1;
#endif

	if (oplus_chg_is_wls_fast_type() == true)
		return 1;

	if (oplus_voocphy_get_dual_cp_support() == false) {
		if (oplus_voocphy_get_bidirect_cp_support() &&
		    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC) {
			return 1;
		}
		if ((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
		    (oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC &&
		     oplus_chg_get_voocphy_support_display_vooc() == false)) {
			return 0;
		}
	}
	if (g_charger_chip->chg_ctrl_by_vooc) {
		if (oplus_vooc_get_fastchg_started() == true &&
		    oplus_vooc_get_fast_chg_type() == CHARGER_SUBTYPE_FASTCHG_VOOC) {
			if (g_charger_chip->prop_status != POWER_SUPPLY_STATUS_FULL &&
			    (g_charger_chip->stop_voter == CHG_STOP_VOTER__FULL ||
			     g_charger_chip->stop_voter == CHG_STOP_VOTER_NONE)) {
				return 1;
			} else {
				return 0;
			}
		}
	}
	if (oplus_vooc_get_fastchg_started()) {
		return 1;
	} else if (oplus_pps_get_pps_dummy_started() ||
		   oplus_pps_get_pps_fastchg_started()) {
		return 1;
	} else if (oplus_ufcs_show_vooc()) {
		return 1;
	} else if (oplus_vooc_get_fastchg_to_normal() == true ||
		   oplus_vooc_get_fastchg_to_warm() == true ||
		   oplus_vooc_get_fastchg_dummy_started() == true ||
		   oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) {
		if ((g_charger_chip->vooc_project == 1 ||
		     g_charger_chip->vooc_project == 5 ||
		     g_charger_chip->vooc_project == 13 ||
			 g_charger_chip->vooc_project == DUAL_BATT_80W ||
			 g_charger_chip->vooc_project == DUAL_BATT_100W ||
		     oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
		     oplus_pps_get_support_type() == PPS_SUPPORT_3CP) &&
		    g_charger_chip->prop_status == POWER_SUPPLY_STATUS_FULL &&
		    (g_charger_chip->tbatt_status == BATTERY_STATUS__COLD_TEMP ||
		     g_charger_chip->tbatt_status == BATTERY_STATUS__WARM_TEMP)) {
			return 1;
		}
		if (g_charger_chip->prop_status != POWER_SUPPLY_STATUS_FULL &&
		    (g_charger_chip->stop_voter == CHG_STOP_VOTER__FULL ||
		     g_charger_chip->stop_voter == CHG_STOP_VOTER__BATTTEMP_ABNORMAL ||
		     g_charger_chip->stop_voter == CHG_STOP_VOTER_NONE ||
		     g_charger_chip->stop_voter == CHG_STOP_VOTER__VBAT_OVP)) {
			return 1;
		} else {
			return 0;
		}
	} else if (oplus_is_vooc_project() == DUAL_BATT_150W || oplus_is_vooc_project() == DUAL_BATT_240W) {
		chg_err("keep_connect_check:%d!", oplus_quirks_keep_connect_status());
		return oplus_quirks_keep_connect_status();
	} else {
		return 0;
	}
}
EXPORT_SYMBOL(oplus_chg_show_vooc_logo_ornot);

bool get_otg_switch(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->otg_switch;
	}
}

bool oplus_chg_get_otg_online(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->otg_online;
	}
}

void oplus_chg_set_otg_online(bool online)
{
	if (g_charger_chip) {
		g_charger_chip->otg_online = online;
	}
	if (oplus_voocphy_get_bidirect_cp_support()) {
		if (online)
			oplus_voocphy_set_chg_auto_mode(true);
		else
			oplus_voocphy_set_chg_auto_mode(false);
	}
}

bool oplus_chg_get_batt_full(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->batt_full;
	}
}

bool oplus_chg_get_rechging_status(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->in_rechging;
	}
}

bool oplus_chg_get_chging_status(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->chging_on;
	}
}

int oplus_chg_get_ffc_status(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->fastchg_ffc_status;
	}
}

int oplus_chg_get_gauge_and_asic_status(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->internal_gauge_with_asic;
	}
}

bool oplus_chg_check_pd_disable(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->pd_disable;
	}
}

bool oplus_chg_check_chip_is_null(void)
{
	if (!g_charger_chip) {
		return true;
	} else {
		return false;
	}
}
EXPORT_SYMBOL(oplus_chg_check_chip_is_null);

int oplus_chg_check_ui_soc(void)
{
	if ((g_charger_chip == NULL) || ((g_charger_chip->soc_load == 0) && (g_charger_chip->smooth_soc < 0)))
		return false;
	else
		return true;
}
EXPORT_SYMBOL(oplus_chg_check_ui_soc);

void oplus_chg_set_charger_type_unknown(void)
{
	if (g_charger_chip) {
		g_charger_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		g_charger_chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	}
}

int oplus_chg_get_charger_voltage(void)
{
	if (!g_charger_chip) {
		return -500;
	} else {
		if (chgr_dbg_vchg != 0)
			g_charger_chip->charger_volt = chgr_dbg_vchg;
		else
			g_charger_chip->charger_volt = g_charger_chip->chg_ops->get_charger_volt();
	}
	return g_charger_chip->charger_volt;
}

#ifdef OPLUS_CUSTOM_OP_DEF
int oplus_chg_get_charger_current(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops) {
		return -1;
	} else {
		return g_charger_chip->chg_ops->get_charger_current();
	}
}
#endif

int oplus_chg_update_voltage(void)
{
	if (!g_charger_chip) {
		g_charger_chip->charger_volt = -500;
	} else {
		g_charger_chip->charger_volt = g_charger_chip->chg_ops->get_charger_volt();
	}
	return 0;
}

void oplus_chg_set_chargerid_switch_val(int value)
{
	if (g_charger_chip && g_charger_chip->chg_ops->set_chargerid_switch_val) {
		g_charger_chip->chg_ops->set_chargerid_switch_val(value);
	}
}

int oplus_chg_get_chargerid_switch_val(void)
{
	if (g_charger_chip && g_charger_chip->chg_ops->get_chargerid_switch_val) {
		return g_charger_chip->chg_ops->get_chargerid_switch_val();
	}

	return 0;
}

void oplus_chg_clear_chargerid_info(void)
{
	if (g_charger_chip && g_charger_chip->chg_ops->set_chargerid_switch_val) {
		g_charger_chip->chargerid_volt = 0;
		g_charger_chip->chargerid_volt_got = false;
	}
}

int oplus_chg_get_normal_cool_down_status(void)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return 0;
	}
	ret = chip->normal_cool_down;
	charger_xlog_printk(CHG_LOG_CRTI, "normal_cool_down=%d\n", chip->normal_cool_down);

	return ret;
}

int oplus_chg_get_cool_down_status(void)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return 0;
	}

#ifdef OPLUS_CUSTOM_OP_DEF
	if (chip->hiz_gnd_cable && chip->svooc_disconnect_count > 0 && chip->cool_down_bck > 0) {
		chip->cool_down = chip->cool_down_bck;
		chip->cool_down_bck = 0;
	}
#endif

	if (!chip->skip_usbtemp_cool_down &&
	    (chip->smart_charge_user == SMART_CHARGE_USER_USBTEMP)) {
		if (chip->smart_charging_screenoff) {
			if (oplus_is_ufcs_charging() ||
				((oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
			     oplus_pps_get_support_type() == PPS_SUPPORT_3CP) &&
			    ((oplus_pps_get_adapter_type() == 1) ||
			     (oplus_pps_get_adapter_type() == 2) ||
			     (oplus_pps_get_adapter_type() == 3)))) {
				chip->usbtemp_cool_down =
					oplus_convert_pps_current_to_level(chip, USBTEMP_CHARGING_CURRENT_LIMIT);
			} else {
				chip->usbtemp_cool_down =
					oplus_convert_current_to_level(chip, USBTEMP_CHARGING_CURRENT_LIMIT);
			}
		} else {
			chip->usbtemp_cool_down = 3;
		}
		if (chip->cool_down) {
			ret = (chip->usbtemp_cool_down < chip->cool_down) ? chip->usbtemp_cool_down : chip->cool_down;
		} else {
			ret = chip->usbtemp_cool_down;
		}
	} else {
		ret = chip->cool_down;
	}

	if (chip->support_low_soc_unlimit && chip->ui_soc < chip->unlimit_soc) {
		return 0;
	}

	if (oplus_chg_get_voocphy_support() == NO_VOOCPHY && (oplus_is_pps_charging() == false) &&
	    (oplus_is_ufcs_charging() == false)) {
		if (chip->bcc_cool_down) {
			if (ret == 0) {
				ret = chip->bcc_cool_down;
			} else {
				ret = (chip->bcc_cool_down < ret) ? chip->bcc_cool_down : ret;
			}
		}
	}

	charger_xlog_printk(CHG_LOG_CRTI, "ret = 0x%x, cool_down=%d,usbtemp_cool_down=%d, bcc_cool_down=%d\n", ret,
			    chip->cool_down, chip->usbtemp_cool_down, chip->bcc_cool_down);

	return ret;
}

struct current_level {
	int level;
	int icharging;
};

struct current_level SVOOC_2_0[] = {
	{ 1, 1000 },   { 2, 1500 },   { 3, 2000 },   { 4, 2500 },   { 5, 3000 },   { 6, 3500 },	 { 7, 4000 },
	{ 8, 4500 },   { 9, 5000 },   { 10, 5500 },  { 11, 6000 },  { 12, 6300 },  { 13, 6500 }, { 14, 7000 },
	{ 15, 7300 },  { 15, 7500 },  { 16, 8000 },  { 17, 8500 },  { 18, 9000 },  { 19, 9500 }, { 20, 10000 },
	{ 21, 10500 }, { 22, 11000 }, { 23, 11500 }, { 24, 12000 }, { 25, 12500 },
};

struct current_level SVOOC_1_0[] = {
	{ 1, 2000 },   { 2, 2000 },   { 3, 2000 },   { 4, 3000 },  { 5, 4000 },
	{ 6, 5000 },   { 7, 6000 },   { 8, 7000 },   { 9, 8000 },  { 10, 9000 },
	{ 11, 10000 }, { 12, 11000 }, { 13, 12000 }, { 14, 12600 }
};

struct current_level pps_oplus[] = {
	{ 1, 1000 },   { 2, 1000 },   { 3, 1200 },   { 4, 1500 },   { 5, 1700 },  { 6, 2000 },	 { 7, 2200 },
	{ 8, 2500 },   { 9, 2700 },   { 10, 3000 },  { 11, 3200 },  { 12, 3500 }, { 13, 3700 },	 { 14, 4000 },
	{ 15, 4500 },  { 16, 5000 },  { 17, 5500 },  { 18, 6000 },  { 19, 6300 }, { 20, 6500 },	 { 21, 7000 },
	{ 22, 7500 },  { 23, 8000 },  { 24, 8500 },  { 25, 9000 },  { 26, 9500 }, { 27, 10000 }, { 28, 10500 },
	{ 29, 11000 }, { 30, 11500 }, { 31, 12000 }, { 32, 12500 },
};

struct current_level OLD_SVOOC_1_0[] = { /*20638 RT5125 50W*/
	{ 1, 2000 },  { 2, 3000 },  { 3, 4000 },   { 4, 5000 },
	{ 5, 6000 },  { 6, 7000 },  { 7, 8000 },   { 8, 9000 },
	{ 9, 10000 }, { 10, 1200 }, { 11, 12000 }, { 12, 12600 }
};
static int c_level_index = 0;

int find_current_to_level(int val, struct current_level *table, int len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		if (table[i].level == val) {
			return table[i].icharging;
		}
	}
	return 0;
}

int find_level_to_current(int val, struct current_level *table, int len)
{
	int i = 0;
	bool find_out_flag = false;

	for (i = 0; i < len; i++) {
		if (table[i].icharging > val) {
			find_out_flag = true;
			break;
		}
		find_out_flag = false;
	}
	if (find_out_flag == true && i != 0) {
		return table[i - 1].level;
	} else {
		return 0;
	}
}

int oplus_convert_level_to_current(struct oplus_chg_chip *chip, int val)
{
	int in_current = 0;
	int reply_bits = 0;

	reply_bits = oplus_vooc_get_reply_bits();
	if (!val || (!reply_bits && (oplus_chg_get_voocphy_support() != ADSP_VOOCPHY)))
		return in_current;

	if (reply_bits == 7 || oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		if (is_vooc_support_single_batt_svooc() == 1 ||
		    (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY)) {
			if (is_vooc_support_old_svooc_1_0() == 1) { /*20638 RT5125 50W*/
				in_current = find_current_to_level(val, OLD_SVOOC_1_0,
								   ARRAY_SIZE(OLD_SVOOC_1_0)); /*20638 RT5125 50W*/
				charger_xlog_printk(CHG_LOG_CRTI,
						    "use old current level {1, 2000}, {2, 3000}, {3, 4000} ...\n");
			} else {
				in_current = find_current_to_level(val, SVOOC_1_0, ARRAY_SIZE(SVOOC_1_0));
			}
		} else {
			in_current = find_current_to_level(val, SVOOC_2_0, ARRAY_SIZE(SVOOC_2_0));
		}
	}

	return in_current;
}

int oplus_convert_current_to_level(struct oplus_chg_chip *chip, int val)
{
	int level = 0, reply_bits = 0;
	reply_bits = oplus_vooc_get_reply_bits();

	if (!val || (!reply_bits && (oplus_chg_get_voocphy_support() != ADSP_VOOCPHY))) {
		return level;
	}
	if (reply_bits == 7 || oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
		if (is_vooc_support_single_batt_svooc() == 1 ||
		    (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY)) {
			if (is_vooc_support_old_svooc_1_0() == 1) { /*20638 RT5125 50W*/
				level = find_level_to_current(val, OLD_SVOOC_1_0,
							      ARRAY_SIZE(OLD_SVOOC_1_0)); /*20638 RT5125 50W*/
				charger_xlog_printk(CHG_LOG_CRTI,
						    "use old current level {1, 2000}, {2, 3000}, {3, 4000} ...\n");
			} else {
				level = find_level_to_current(val, SVOOC_1_0, ARRAY_SIZE(SVOOC_1_0));
			}
		} else {
			level = find_level_to_current(val, SVOOC_2_0, ARRAY_SIZE(SVOOC_2_0));
		}
	} else {
		level = val / 1000;
	}
	return level;
}

int oplus_convert_pps_current_to_level(struct oplus_chg_chip *chip, int val)
{
	int level = 0;
	if (!val) {
		return level;
	}
	level = find_level_to_current(val, pps_oplus, ARRAY_SIZE(pps_oplus));
	return level;
}

static const int cool_down_current_limit_normal[6] = { 1200, 1500, 2000, 2000, 2000, 2000 };
static const int cool_down_current_limit_onebat[6] = { 1200, 1500, 2000, 1500, 2000, 2000 };
static const int cool_down_current_limit_onebat_nohv[6] = { 1200, 1500, 2000, 2000, 2000, 2000 };
static const int old_cool_down_current_limit_onebat[6] = {
	500, 900, 1200, 1500, 2000, 1500
}; /* use for old 18w smart charger */
static const int cool_down_current_limit_vooc[6] = { 1500, 1500, 2000, 2500, 3000, 3500 };

#define BATT_NTC_CTRL_THRESHOLD_LOW 320
#define BATT_NTC_CTRL_THRESHOLD_HIGH 600
int oplus_chg_override_by_shell_temp(int temp)
{
	bool charger_exist = false;

	if (!g_charger_chip) {
		return 0;
	}
	if (oplus_is_power_off_charging(NULL)) {
		return 0;
	}
#ifdef SUPPORT_WPC
	if (g_charger_chip->wireless_support &&
	    (oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present())) {
		return 0;
	}
#endif

	if (g_charger_chip->charger_exist || oplus_vooc_get_fastchg_started() || oplus_vooc_get_fastchg_to_normal() ||
	    oplus_vooc_get_fastchg_to_warm() || oplus_vooc_get_fastchg_dummy_started())
		charger_exist = true;

	if (charger_exist && g_charger_chip->smart_charging_screenoff && (temp > BATT_NTC_CTRL_THRESHOLD_LOW) &&
	    (temp < BATT_NTC_CTRL_THRESHOLD_HIGH)) {
		/*charger_xlog_printk(CHG_LOG_CRTI, "charging override by shell temperature\n");*/
		return 1;
	}
	return 0;
}

int oplus_chg_get_shell_temp(void)
{
	int temp_val = 0, rc = -EINVAL;

	if (!g_charger_chip) {
		return TEMPERATURE_INVALID;
	}

	rc = thermal_zone_get_temp(g_charger_chip->shell_themal, &temp_val);
	if (rc) {
		g_charger_chip->shell_themal = thermal_zone_get_zone_by_name("shell_back");
		rc = thermal_zone_get_temp(g_charger_chip->shell_themal, &temp_val);
		if (rc) {
			chg_err("thermal_zone_get_temp get error");
			return g_charger_chip->shell_temp;
		}
	}
	g_charger_chip->shell_temp = temp_val / 100;
	return g_charger_chip->shell_temp;
}

#define COOL_DOWN_TO_CURRENT 100
#define COOL_DOWN_LEVEL_10 10
int oplus_smart_charge_by_bcc(struct oplus_chg_chip *chip, int val)
{
	int subtype = 0;
	int ret = 0;

	if (!(oplus_vooc_get_bcc_support() || chip->smart_chg_bcc_support) || (val < 0)) {
		charger_xlog_printk(CHG_LOG_CRTI, "not support smart chg bcc mode\n");
		return 0;
	}

	chip->bcc_current = val * COOL_DOWN_TO_CURRENT;
	if (val == 0) {
		charger_xlog_printk(CHG_LOG_CRTI, "val is invalid\n");
		return 0;
	}

	subtype = oplus_chg_get_fast_chg_type();
	charger_xlog_printk(CHG_LOG_CRTI, "get subtype = [%d], ", subtype);

	switch (subtype) {
	case CHARGER_SUBTYPE_DEFAULT:
	case CHARGER_SUBTYPE_FASTCHG_VOOC:
	case CHARGER_SUBTYPE_PD:
	case CHARGER_SUBTYPE_QC:
		break;
	case CHARGER_SUBTYPE_PPS:
		oplus_pps_set_bcc_current(chip->bcc_current);
		break;
	case CHARGER_SUBTYPE_UFCS:
		oplus_ufcs_set_bcc_current(chip->bcc_current);
		break;
	case CHARGER_SUBTYPE_FASTCHG_SVOOC:
		if (chip->chg_ops && chip->chg_ops->set_bcc_curr_to_voocphy)
			ret = chip->chg_ops->set_bcc_curr_to_voocphy(val);

		oplus_voocphy_set_bcc_current(val);
		chip->bcc_cool_down = oplus_convert_current_to_level(chip, chip->bcc_current);
		break;
	default:
		if (subtype > COOL_DOWN_LEVEL_10) {
			if (chip->chg_ops && chip->chg_ops->set_bcc_curr_to_voocphy)
				ret = chip->chg_ops->set_bcc_curr_to_voocphy(val);
			oplus_voocphy_set_bcc_current(val);
		}
		chip->bcc_cool_down = oplus_convert_current_to_level(chip, chip->bcc_current);
		break;
	}

	charger_xlog_printk(CHG_LOG_CRTI, "bcc_current[%d], bcc_cool_down[%d]\n", chip->bcc_current,
			    chip->bcc_cool_down);

	return ret;
}

int choose_little_current(int val1, int val2)
{
	if (val1 >= val2)
		return val2;
	else
		return val1;
}
void oplus_smart_charge_by_shell_temp(struct oplus_chg_chip *chip, int val)
{
	int subtype = 0, rc = -EINVAL;
	static int pre_shell_temp_current = 0;
	int onebat_index_temp = 0;
	int normal_index_temp = 0;
	int vooc_index_temp = 0;
	int min_temp = 0;

	if (!chip) {
		return;
	}

	if (chip->shell_themal) {
		rc = thermal_zone_get_temp(chip->shell_themal, &chip->shell_temp);
		if (rc) {
			g_charger_chip->shell_themal = thermal_zone_get_zone_by_name("shell_back");
			rc = thermal_zone_get_temp(g_charger_chip->shell_themal, &chip->shell_temp);
			if (rc) {
				chip->shell_temp = (val >> 16) & 0XFFFF;
				chg_err("thermal_zone_get_temp get error");
			} else {
				chip->shell_temp = chip->shell_temp / 100;
			}
		} else {
			chip->shell_temp = chip->shell_temp / 100;
		}
	} else {
		chip->shell_temp = (val >> 16) & 0XFFFF;
	}
	val = val & 0XFFFF;

	charger_xlog_printk(
		CHG_LOG_CRTI,
		"val->intval = [%04x], shell_temp = [%04x], set shell_temp_down = [%d] pre_shell_temp_current =[%d],led_on=%d .\n",
		val, chip->shell_temp, chip->cool_down, pre_shell_temp_current, chip->led_on);

	if (chip->led_on) {
		return;
	}

	if (pre_shell_temp_current == 0 && val == 0)
		return;

	pre_shell_temp_current = val;
	onebat_index_temp = ARRAY_SIZE(cool_down_current_limit_onebat) < c_level_index ?
				    ARRAY_SIZE(cool_down_current_limit_onebat) :
				    c_level_index;
	normal_index_temp = ARRAY_SIZE(cool_down_current_limit_normal) < c_level_index ?
				    ARRAY_SIZE(cool_down_current_limit_normal) :
				    c_level_index;
	vooc_index_temp = ARRAY_SIZE(cool_down_current_limit_vooc) < c_level_index ?
				  ARRAY_SIZE(cool_down_current_limit_vooc) :
				  c_level_index;

	subtype = oplus_chg_get_fast_chg_type();
	charger_xlog_printk(CHG_LOG_CRTI, "get subtype = [%d]\n", subtype);
	if (!val) {
		if (c_level_index <= 0) {
			chip->cool_down = 0;

			if (CHARGER_SUBTYPE_PD == subtype)
				chip->limits.input_current_charger_ma =
					chip->limits.default_pd_input_current_charger_ma;
			else if (CHARGER_SUBTYPE_QC == subtype)
				chip->limits.input_current_charger_ma =
					chip->limits.default_qc_input_current_charger_ma;
			else
				chip->limits.input_current_charger_ma = chip->limits.default_input_current_charger_ma;

			chip->limits.input_current_vooc_ma_high = chip->limits.default_input_current_vooc_ma_high;
			chip->limits.input_current_vooc_ma_warm = chip->limits.default_input_current_vooc_ma_warm;
			chip->limits.input_current_vooc_ma_normal = chip->limits.default_input_current_vooc_ma_normal;
			chip->limits.pd_input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
			chip->limits.qc_input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
		} else {
			chip->cool_down = c_level_index;
			if (chip->vbatt_num == 1) {
				chip->limits.pd_input_current_charger_ma =
					choose_little_current(cool_down_current_limit_onebat[onebat_index_temp - 1],
							      chip->limits.default_pd_input_current_charger_ma);
				chip->limits.qc_input_current_charger_ma =
					choose_little_current(cool_down_current_limit_onebat[onebat_index_temp - 1],
							      chip->limits.default_qc_input_current_charger_ma);
				if (CHARGER_SUBTYPE_PD == subtype)
					chip->limits.input_current_charger_ma =
						chip->limits.pd_input_current_charger_ma;
				else if (CHARGER_SUBTYPE_QC == subtype)
					chip->limits.input_current_charger_ma =
						chip->limits.qc_input_current_charger_ma;
				else
					chip->limits.input_current_charger_ma = choose_little_current(
						cool_down_current_limit_onebat[onebat_index_temp - 1],
						chip->limits.default_input_current_charger_ma);
			} else {
				chip->limits.pd_input_current_charger_ma =
					choose_little_current(cool_down_current_limit_normal[normal_index_temp - 1],
							      chip->limits.default_pd_input_current_charger_ma);
				chip->limits.qc_input_current_charger_ma =
					choose_little_current(cool_down_current_limit_normal[normal_index_temp - 1],
							      chip->limits.default_qc_input_current_charger_ma);
				if (CHARGER_SUBTYPE_PD == subtype)
					chip->limits.input_current_charger_ma =
						chip->limits.pd_input_current_charger_ma;
				else if (CHARGER_SUBTYPE_QC == subtype)
					chip->limits.input_current_charger_ma =
						chip->limits.qc_input_current_charger_ma;
				else
					chip->limits.input_current_charger_ma = choose_little_current(
						cool_down_current_limit_normal[normal_index_temp - 1],
						chip->limits.default_input_current_charger_ma);
			}

			chip->limits.input_current_vooc_ma_high =
				choose_little_current(cool_down_current_limit_vooc[vooc_index_temp - 1],
						      chip->limits.default_input_current_vooc_ma_high);
			chip->limits.input_current_vooc_ma_warm =
				choose_little_current(cool_down_current_limit_vooc[vooc_index_temp - 1],
						      chip->limits.default_input_current_vooc_ma_warm);
			chip->limits.input_current_vooc_ma_normal =
				choose_little_current(cool_down_current_limit_vooc[vooc_index_temp - 1],
						      chip->limits.default_input_current_vooc_ma_normal);
		}
		chip->cool_down_done = true;
		chip->cool_down_force_5v = false;
		chip->chg_ctrl_by_cool_down = false;
		chg_ctrl_by_sale_mode = false;
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			chip->screenoff_curr = val;
		}
		return;
	}

	switch (subtype) {
	case CHARGER_SUBTYPE_DEFAULT:
		break;
	case CHARGER_SUBTYPE_FASTCHG_SVOOC:
		/*for 7bits 25 current steps*/
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			chip->screenoff_curr = val;
		} else {
			if (c_level_index > 0) {
				chip->cool_down = oplus_convert_current_to_level(chip, val);
				chip->cool_down = chip->cool_down < c_level_index ? chip->cool_down : c_level_index;
			} else {
				chip->cool_down = oplus_convert_current_to_level(chip, val);
			}
		}
		break;
	case CHARGER_SUBTYPE_FASTCHG_VOOC:
		if (chip->chg_ctrl_by_vooc == true) {
			if (c_level_index <= 0) {
				chip->limits.input_current_cool_down_ma = val;
				chip->limits.input_current_charger_ma =
					choose_little_current(val, chip->limits.default_input_current_charger_ma);
				chip->limits.pd_input_current_charger_ma =
					choose_little_current(val, chip->limits.default_pd_input_current_charger_ma);
				chip->limits.qc_input_current_charger_ma =
					choose_little_current(val, chip->limits.default_qc_input_current_charger_ma);
				chip->limits.input_current_vooc_ma_high =
					choose_little_current(val, chip->limits.default_input_current_vooc_ma_high);
				chip->limits.input_current_vooc_ma_warm =
					choose_little_current(val, chip->limits.default_input_current_vooc_ma_warm);
				chip->limits.input_current_vooc_ma_normal =
					choose_little_current(val, chip->limits.default_input_current_vooc_ma_normal);
			} else {
				if (chip->vbatt_num == 1) {
					chip->limits.input_current_cool_down_ma = choose_little_current(
						val, cool_down_current_limit_onebat[onebat_index_temp - 1]);
				} else {
					chip->limits.input_current_cool_down_ma = choose_little_current(
						val, cool_down_current_limit_normal[normal_index_temp - 1]);
				}
				chip->limits.input_current_charger_ma =
					choose_little_current(chip->limits.input_current_cool_down_ma,
							      chip->limits.default_input_current_charger_ma);
				chip->limits.pd_input_current_charger_ma =
					choose_little_current(chip->limits.input_current_cool_down_ma,
							      chip->limits.default_pd_input_current_charger_ma);
				chip->limits.qc_input_current_charger_ma =
					choose_little_current(chip->limits.input_current_cool_down_ma,
							      chip->limits.default_qc_input_current_charger_ma);
				min_temp =
					choose_little_current(val, cool_down_current_limit_vooc[vooc_index_temp - 1]);
				chip->limits.input_current_vooc_ma_high = choose_little_current(
					min_temp, chip->limits.default_input_current_vooc_ma_high);
				chip->limits.input_current_vooc_ma_warm = choose_little_current(
					min_temp, chip->limits.default_input_current_vooc_ma_high);
				chip->limits.input_current_vooc_ma_normal = choose_little_current(
					min_temp, chip->limits.default_input_current_vooc_ma_normal);
			}
		} else {
			if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
				chip->screenoff_curr = val;
			} else {
				if (c_level_index > 0)
					chip->cool_down = oplus_convert_current_to_level(chip, val) < c_level_index ?
								  oplus_convert_current_to_level(chip, val) :
								  c_level_index;
				else
					chip->cool_down = oplus_convert_current_to_level(chip, val);
			}
		}
		chip->cool_down_done = true;
		chip->cool_down_force_5v = false;
		chip->chg_ctrl_by_cool_down = true;
		chg_ctrl_by_sale_mode = false;
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			chip->screenoff_curr = val;
		}
		break;
	case CHARGER_SUBTYPE_PPS:
	case CHARGER_SUBTYPE_UFCS:
		chip->cool_down = oplus_convert_pps_current_to_level(chip, val);
		chg_err("pps chip->cool_down = %d\n", chip->cool_down);
		break;
	case CHARGER_SUBTYPE_PD:
	case CHARGER_SUBTYPE_QC:
		if (c_level_index <= 0) {
			chip->limits.input_current_cool_down_ma = val;
			chip->limits.input_current_charger_ma =
				choose_little_current(val, chip->limits.default_input_current_charger_ma);
			chip->limits.pd_input_current_charger_ma =
				choose_little_current(val, chip->limits.default_pd_input_current_charger_ma);
			chip->limits.qc_input_current_charger_ma =
				choose_little_current(val, chip->limits.default_qc_input_current_charger_ma);
			chip->limits.input_current_vooc_ma_high =
				choose_little_current(val, chip->limits.default_input_current_vooc_ma_high);
			chip->limits.input_current_vooc_ma_warm =
				choose_little_current(val, chip->limits.default_input_current_vooc_ma_warm);
			chip->limits.input_current_vooc_ma_normal =
				choose_little_current(val, chip->limits.default_input_current_vooc_ma_normal);
		} else {
			if (chip->vbatt_num == 1) {
				chip->limits.input_current_cool_down_ma =
					val < cool_down_current_limit_onebat[onebat_index_temp - 1] ?
						val :
						cool_down_current_limit_onebat[onebat_index_temp - 1];
			} else {
				chip->limits.input_current_cool_down_ma =
					val < cool_down_current_limit_normal[normal_index_temp - 1] ?
						val :
						cool_down_current_limit_normal[normal_index_temp - 1];
			}

			if (subtype == CHARGER_SUBTYPE_PD)
				chip->limits.input_current_cool_down_ma =
					choose_little_current(chip->limits.input_current_cool_down_ma,
							      chip->limits.default_pd_input_current_charger_ma);
			else
				chip->limits.input_current_cool_down_ma =
					choose_little_current(chip->limits.input_current_cool_down_ma,
							      chip->limits.default_qc_input_current_charger_ma);

			chip->limits.input_current_charger_ma = choose_little_current(
				chip->limits.input_current_cool_down_ma, chip->limits.default_input_current_charger_ma);
			chip->limits.pd_input_current_charger_ma =
				choose_little_current(chip->limits.input_current_cool_down_ma,
						      chip->limits.default_pd_input_current_charger_ma);
			chip->limits.qc_input_current_charger_ma =
				choose_little_current(chip->limits.input_current_cool_down_ma,
						      chip->limits.default_qc_input_current_charger_ma);
			min_temp = choose_little_current(val, cool_down_current_limit_vooc[vooc_index_temp - 1]);
			chip->limits.input_current_vooc_ma_high =
				choose_little_current(min_temp, chip->limits.input_current_vooc_ma_high);
			chip->limits.input_current_vooc_ma_warm =
				choose_little_current(min_temp, chip->limits.default_input_current_vooc_ma_warm);
			chip->limits.input_current_vooc_ma_normal =
				choose_little_current(min_temp, chip->limits.default_input_current_vooc_ma_normal);
		}
		chip->cool_down_done = true;
		chip->cool_down_force_5v = false;
		chip->chg_ctrl_by_cool_down = true;
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			chip->screenoff_curr = val;
		}
		break;
	default:
		if (subtype > 10) {
			if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
				chip->screenoff_curr = val;
			} else {
				if (c_level_index > 0) {
					chip->cool_down = oplus_convert_current_to_level(chip, val);
					chip->cool_down =
						chip->cool_down < c_level_index ? chip->cool_down : c_level_index;
				} else {
					chip->cool_down = oplus_convert_current_to_level(chip, val);
				}
			}
		}
		break;
	}
	chg_ctrl_by_sale_mode = false;
	charger_xlog_printk(CHG_LOG_CRTI, "val = [%d],  subtype = [%d], vbatt_num = %d, c_level_index = %d, pd = %d, \
		qc = %d, input_current = %d, input_cool = %d, vooc = %d %d %d \n",
			    val, subtype, chip->vbatt_num, c_level_index, chip->limits.pd_input_current_charger_ma,
			    chip->limits.qc_input_current_charger_ma, chip->limits.input_current_charger_ma,
			    chip->limits.input_current_cool_down_ma, chip->limits.input_current_vooc_ma_high,
			    chip->limits.input_current_vooc_ma_warm, chip->limits.input_current_vooc_ma_normal);
}

#define OLD_FORCE_5V_LIMIT 6
#define NEW_FORCE_5V_LIMIT 4
void oplus_smart_charge_by_cool_down(struct oplus_chg_chip *chip, int val)
{
	OPLUS_CHARGER_SUBTYPE esubtype = CHARGER_SUBTYPE_DEFAULT;
	int onebat_index_temp = 0;
	int normal_index_temp = 0;
	int vooc_index_temp = 0;
	int m_cool_down_current_limit_onebat[6] = { 0 };
	int cool_down_force_5v_limit_onebat = 0;

	if (!chip) {
		return;
	}

	c_level_index = val;

	onebat_index_temp =
		ARRAY_SIZE(cool_down_current_limit_onebat) < val ? ARRAY_SIZE(cool_down_current_limit_onebat) : val;
	normal_index_temp =
		ARRAY_SIZE(cool_down_current_limit_normal) < val ? ARRAY_SIZE(cool_down_current_limit_normal) : val;
	vooc_index_temp =
		ARRAY_SIZE(cool_down_current_limit_vooc) < val ? ARRAY_SIZE(cool_down_current_limit_vooc) : val;

	esubtype = chip->chg_ops->get_charger_subtype();

	if (chip->old_smart_charge_standard == true) {
		memcpy(m_cool_down_current_limit_onebat, old_cool_down_current_limit_onebat, sizeof(int) * 6);
		cool_down_force_5v_limit_onebat = OLD_FORCE_5V_LIMIT;
	} else if (chip->chg_ops->is_support_qcpd && !chip->chg_ops->is_support_qcpd()) {
		memcpy(m_cool_down_current_limit_onebat, cool_down_current_limit_onebat_nohv, sizeof(int) * 6);
		cool_down_force_5v_limit_onebat = NEW_FORCE_5V_LIMIT;
	} else {
		memcpy(m_cool_down_current_limit_onebat, cool_down_current_limit_onebat, sizeof(int) * 6);
		cool_down_force_5v_limit_onebat = NEW_FORCE_5V_LIMIT;
	}

	if (chip->smart_charge_version == 1) {
		if (val == chip->cool_down)
			return;

		if (!val) {
			chip->cool_down = 0;
			if (CHARGER_SUBTYPE_PD == esubtype)
				chip->limits.input_current_charger_ma =
					chip->limits.default_pd_input_current_charger_ma;
			else if (CHARGER_SUBTYPE_QC == esubtype)
				chip->limits.input_current_charger_ma =
					chip->limits.default_qc_input_current_charger_ma;
			else
				chip->limits.input_current_charger_ma = chip->limits.default_input_current_charger_ma;
			chip->limits.input_current_vooc_ma_high = chip->limits.default_input_current_vooc_ma_high;
			chip->limits.input_current_vooc_ma_warm = chip->limits.default_input_current_vooc_ma_warm;
			chip->limits.input_current_vooc_ma_normal = chip->limits.default_input_current_vooc_ma_normal;
			chip->limits.pd_input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
			chip->limits.qc_input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
			chip->cool_down_done = true;
			chip->cool_down_force_5v = false;
			chg_ctrl_by_sale_mode = false;
			charger_xlog_printk(CHG_LOG_CRTI, "val->intval = [%04x], set cool_down = [%d].\n", val,
					    chip->cool_down);
			return;
		} else if ((val == SALE_MODE_COOL_DOWN || val == SALE_MODE_COOL_DOWN_TWO) &&
			   esubtype == CHARGER_SUBTYPE_DEFAULT) {
			if (chg_ctrl_by_sale_mode && chip->cool_down == val)
				return;
			chip->cool_down = val;
			chip->cool_down_done = true;
			chg_ctrl_by_sale_mode = true;
			charger_xlog_printk(CHG_LOG_CRTI, "val->intval = [%d], set cool_down = [%d].\n", val,
					    chip->cool_down);
			return;
		}
		chip->cool_down = val;
		chg_ctrl_by_sale_mode = false;

		if (chip->vbatt_num == 1) {
			chip->limits.pd_input_current_charger_ma =
				choose_little_current(m_cool_down_current_limit_onebat[onebat_index_temp - 1],
						      chip->limits.default_pd_input_current_charger_ma);
			chip->limits.qc_input_current_charger_ma =
				choose_little_current(m_cool_down_current_limit_onebat[onebat_index_temp - 1],
						      chip->limits.default_qc_input_current_charger_ma);
			if (CHARGER_SUBTYPE_PD == esubtype)
				chip->limits.input_current_charger_ma = chip->limits.pd_input_current_charger_ma;
			else if (CHARGER_SUBTYPE_QC == esubtype)
				chip->limits.input_current_charger_ma = chip->limits.qc_input_current_charger_ma;
			else
				chip->limits.input_current_charger_ma =
					choose_little_current(m_cool_down_current_limit_onebat[onebat_index_temp - 1],
							      chip->limits.default_input_current_charger_ma);
		} else {
			chip->limits.pd_input_current_charger_ma =
				choose_little_current(cool_down_current_limit_normal[normal_index_temp - 1],
						      chip->limits.default_pd_input_current_charger_ma);
			chip->limits.qc_input_current_charger_ma =
				choose_little_current(cool_down_current_limit_normal[normal_index_temp - 1],
						      chip->limits.default_qc_input_current_charger_ma);
			if (CHARGER_SUBTYPE_PD == esubtype)
				chip->limits.input_current_charger_ma = chip->limits.pd_input_current_charger_ma;
			else if (CHARGER_SUBTYPE_QC == esubtype)
				chip->limits.input_current_charger_ma = chip->limits.qc_input_current_charger_ma;
			else
				chip->limits.input_current_charger_ma =
					choose_little_current(cool_down_current_limit_normal[normal_index_temp - 1],
							      chip->limits.default_input_current_charger_ma);
		}

		chip->limits.input_current_vooc_ma_high =
			choose_little_current(cool_down_current_limit_vooc[vooc_index_temp - 1],
					      chip->limits.default_input_current_vooc_ma_high);
		chip->limits.input_current_vooc_ma_warm =
			choose_little_current(cool_down_current_limit_vooc[vooc_index_temp - 1],
					      chip->limits.default_input_current_vooc_ma_warm);
		chip->limits.input_current_vooc_ma_normal =
			choose_little_current(cool_down_current_limit_vooc[vooc_index_temp - 1],
					      chip->limits.default_input_current_vooc_ma_normal);
		chip->cool_down_done = true;

		if ((chip->vbatt_num == 1) && (val < cool_down_force_5v_limit_onebat)) {
			chip->cool_down_force_5v = true;
		} else {
			chip->cool_down_force_5v = false;
		}
	} else {
		if (val & SMART_VOOC_CHARGER_CURRENT_BIT0) {
			if (oplus_vooc_get_vooc_multistep_adjust_current_support() == false)
				chip->cool_down = 1;
			else
				chip->cool_down = 1;
		} else if (val & SMART_VOOC_CHARGER_CURRENT_BIT1) {
			if (oplus_vooc_get_vooc_multistep_adjust_current_support() == false)
				chip->cool_down = 1;
			else
				chip->cool_down = 2;
		} else if (val & SMART_VOOC_CHARGER_CURRENT_BIT2) {
			if (oplus_vooc_get_vooc_multistep_adjust_current_support() == false)
				chip->cool_down = 1;
			else
				chip->cool_down = 3;
		} else if (val & SMART_VOOC_CHARGER_CURRENT_BIT3) {
			if (oplus_vooc_get_vooc_multistep_adjust_current_support() == false)
				chip->cool_down = 1;
			else
				chip->cool_down = 4;
		} else if (val & SMART_VOOC_CHARGER_CURRENT_BIT4) {
			if (oplus_vooc_get_vooc_multistep_adjust_current_support() == false)
				chip->cool_down = 1;
			else
				chip->cool_down = 5;
		} else if (val & SMART_VOOC_CHARGER_CURRENT_BIT5) {
			if (oplus_vooc_get_vooc_multistep_adjust_current_support() == false)
				chip->cool_down = 1;
			else
				chip->cool_down = 6;
		}

		/*add for 9V2A cool_down*/
		if (val & SMART_NORMAL_CHARGER_9V1500mA) { /*9V1.5A*/
			chip->limits.input_current_cool_down_ma = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->limits.pd_input_current_charger_ma = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->limits.qc_input_current_charger_ma = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}

		if (val & SMART_NORMAL_CHARGER_2000MA) { /*5V2A*/
			chip->limits.input_current_cool_down_ma = OPLUS_CHG_2000_CHARGING_CURRENT;
			chip->limits.pd_input_current_charger_ma = OPLUS_CHG_2000_CHARGING_CURRENT;
			chip->limits.qc_input_current_charger_ma = OPLUS_CHG_2000_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}

		/*9V2A end */

		if (val & SMART_NORMAL_CHARGER_1500MA) {
			chip->limits.input_current_cool_down_ma = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->limits.pd_input_current_charger_ma = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->limits.qc_input_current_charger_ma = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}
		if (val & SMART_NORMAL_CHARGER_1200MA) {
			chip->limits.input_current_cool_down_ma = OPLUS_CHG_1200_CHARGING_CURRENT;
			chip->limits.pd_input_current_charger_ma = OPLUS_CHG_1200_CHARGING_CURRENT;
			chip->limits.qc_input_current_charger_ma = OPLUS_CHG_1200_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}
		if (val & SMART_NORMAL_CHARGER_900MA) {
			chip->limits.input_current_cool_down_ma = OPLUS_CHG_900_CHARGING_CURRENT;
			chip->limits.pd_input_current_charger_ma = OPLUS_CHG_900_CHARGING_CURRENT;
			chip->limits.qc_input_current_charger_ma = OPLUS_CHG_900_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}
		if (val & SMART_NORMAL_CHARGER_500MA) {
			chip->limits.input_current_cool_down_ma = OPLUS_CHG_500_CHARGING_CURRENT;
			chip->limits.pd_input_current_charger_ma = OPLUS_CHG_500_CHARGING_CURRENT;
			chip->limits.qc_input_current_charger_ma = OPLUS_CHG_500_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}
		if (val & SMART_COMPATIBLE_VOOC_CHARGER_CURRENT_BIT0) {
			chip->limits.input_current_vooc_ma_high = OPLUS_CHG_1200_CHARGING_CURRENT;
			chip->limits.input_current_vooc_ma_warm = OPLUS_CHG_1200_CHARGING_CURRENT;
			chip->limits.input_current_vooc_ma_normal = OPLUS_CHG_1200_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}
		if (val & SMART_COMPATIBLE_VOOC_CHARGER_CURRENT_BIT1) {
			chip->limits.input_current_vooc_ma_high = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->limits.input_current_vooc_ma_warm = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->limits.input_current_vooc_ma_normal = OPLUS_CHG_1500_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}
		if (val & SMART_COMPATIBLE_VOOC_CHARGER_CURRENT_BIT2) {
			chip->limits.input_current_vooc_ma_high = OPLUS_CHG_1800_CHARGING_CURRENT;
			chip->limits.input_current_vooc_ma_warm = OPLUS_CHG_1800_CHARGING_CURRENT;
			chip->limits.input_current_vooc_ma_normal = OPLUS_CHG_1800_CHARGING_CURRENT;
			chip->cool_down_done = true;
		}
		if (!val) {
			chip->cool_down = 0;
			esubtype = chip->chg_ops->get_charger_subtype();
			if (CHARGER_SUBTYPE_PD == esubtype)
				chip->limits.input_current_charger_ma =
					chip->limits.default_pd_input_current_charger_ma;
			else if (CHARGER_SUBTYPE_QC == esubtype)
				chip->limits.input_current_charger_ma =
					chip->limits.default_qc_input_current_charger_ma;
			else
				chip->limits.input_current_charger_ma = chip->limits.default_input_current_charger_ma;

			chip->limits.input_current_vooc_ma_high = chip->limits.default_input_current_vooc_ma_high;
			chip->limits.input_current_vooc_ma_warm = chip->limits.default_input_current_vooc_ma_warm;
			chip->limits.input_current_vooc_ma_normal = chip->limits.default_input_current_vooc_ma_normal;
			chip->limits.pd_input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
			chip->limits.qc_input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
			chip->cool_down_done = true;
			charger_xlog_printk(CHG_LOG_CRTI, "val->intval = [%04x], set cool_down = [%d].\n", val,
					    chip->cool_down);
		}
	}
#ifdef OPLUS_CUSTOM_OP_DEF
	chip->cool_down_bck = 0;
#endif
	charger_xlog_printk(CHG_LOG_CRTI, "val->intval = [%04x], set cool_down = [%d], vbatt_num = %d, pd_input = %d, \
	qc_input = %d, input_current = %d, input_current_vooc_ma_high warm low = %d %d %d\n",
			    val, chip->cool_down, chip->vbatt_num, chip->limits.pd_input_current_charger_ma,
			    chip->limits.qc_input_current_charger_ma, chip->limits.input_current_charger_ma,
			    chip->limits.input_current_vooc_ma_high, chip->limits.input_current_vooc_ma_warm,
			    chip->limits.input_current_vooc_ma_normal);
}

int oplus_is_rf_ftm_mode(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int boot_mode = get_boot_mode();
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (boot_mode == META_BOOT || boot_mode == FACTORY_BOOT || boot_mode == ADVMETA_BOOT ||
	    boot_mode == ATE_FACTORY_BOOT) {
		chg_debug(" boot_mode:%d, return\n", boot_mode);
		return true;
	} else
#endif
	{
		return false;
	}
#else
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN || boot_mode == MSM_BOOT_MODE__FACTORY) {
		chg_debug(" boot_mode:%d, return\n", boot_mode);
		return true;
	} else
#endif
	{
		return false;
	}
#endif
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
int oplus_get_prop_status(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->prop_status;
	}
}
EXPORT_SYMBOL(oplus_get_prop_status);
#endif

#define OPLUS_TBATT_HIGH_PWROFF_COUNT (18)
#define OPLUS_TBATT_EMERGENCY_PWROFF_COUNT (6)

DECLARE_WAIT_QUEUE_HEAD(oplus_tbatt_pwroff_wq);

static int oplus_tbatt_power_off_kthread(void *arg)
{
	int over_temp_count = 0, emergency_count = 0;
	int batt_temp = 0;
	int sub_batt_temp = 0;
	int main_batt_temp = 0;
	int pwroff_over_temp_th = OPCHG_PWROFF_HIGH_BATT_TEMP;
	int pwroff_emerg_th = OPCHG_PWROFF_EMERGENCY_BATT_TEMP;
	struct sched_param param = {.sched_priority = MAX_RT_PRIO-1};

	sched_setscheduler(current, SCHED_FIFO, &param);
	tbatt_pwroff_enable = 1;
	if (get_eng_version() == FACTORY) {
		pwroff_over_temp_th = FACTORY_PWROFF_HIGH_BATT_TEMP;
		pwroff_emerg_th = FACTORY_PWROFF_EMERGENCY_BATT_TEMP;
	}
	chg_err(" tbatt_pwroff_th:%d tbatt_emergency pwroff_th[%d] \n", pwroff_over_temp_th, pwroff_emerg_th);

	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(round_jiffies_relative(msecs_to_jiffies(5 * 1000)));
		if (!tbatt_pwroff_enable) {
			emergency_count = 0;
			over_temp_count = 0;
			wait_event_interruptible(oplus_tbatt_pwroff_wq, tbatt_pwroff_enable == 1);
		}
		if (oplus_chg_get_voocphy_support() == NO_VOOCPHY && oplus_vooc_get_fastchg_started() == true) {
			batt_temp = oplus_gauge_get_prev_batt_temperature();
		} else {
			if (oplus_switching_support_parallel_chg()) {
				main_batt_temp = g_charger_chip->tbatt_temp;
				sub_batt_temp = g_charger_chip->sub_batt_temperature;
				batt_temp = main_batt_temp > sub_batt_temp ? main_batt_temp : sub_batt_temp;
			} else {
				batt_temp = oplus_get_report_batt_temp() + g_charger_chip->tbatt_power_off_cali_temp;
			}
		}
		if (batt_temp > pwroff_emerg_th) {
			emergency_count++;
			chg_err(" emergency_count:%d \n", emergency_count);
		} else {
			emergency_count = 0;
		}
		if (batt_temp > pwroff_over_temp_th) {
			over_temp_count++;
			chg_err("over_temp_count[%d] \n", over_temp_count);
		} else {
			over_temp_count = 0;
		}
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
		if (get_eng_version() != HIGH_TEMP_AGING && !oplus_is_ptcrb_version()) {
			if (over_temp_count >= OPLUS_TBATT_HIGH_PWROFF_COUNT ||
			    emergency_count >= OPLUS_TBATT_EMERGENCY_PWROFF_COUNT) {
				chg_err("ERROR: battery temperature is too high, goto power off\n");
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
#if (LINUX_VERSION_CODE == KERNEL_VERSION(4, 14, 186))
				kernel_power_off();
#else
				machine_power_off();
#endif
#else
				kernel_power_off();
#endif
			}
		}
#endif
	}
	return 0;
}

int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		return -1;
	}
	chip->tbatt_pwroff_task = kthread_create(oplus_tbatt_power_off_kthread, chip, "tbatt_pwroff");
	if (chip->tbatt_pwroff_task) {
		wake_up_process(chip->tbatt_pwroff_task);
	} else {
		chg_err("ERROR: chip->tbatt_pwroff_task creat fail\n");
		return -1;
	}
	return 0;
}

void oplus_tbatt_power_off_task_wakeup(void)
{
	wake_up_interruptible(&oplus_tbatt_pwroff_wq);
	return;
}

bool oplus_get_chg_powersave(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->chg_powersave;
	}
}

int oplus_get_chg_unwakelock(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->unwakelock_chg;
	}
}

bool oplus_get_vbatt_higherthan_xmv(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return vbatt_higherthan_4180mv;
	}
}

void oplus_chg_set_input_current_without_aicl(int current_ma)
{
	if (!g_charger_chip) {
		return;
	} else {
		if (g_charger_chip->chg_ops->input_current_write_without_aicl &&
		    oplus_vooc_get_allow_reading() == true) {
			g_charger_chip->chg_ops->input_current_write_without_aicl(current_ma);
			chg_err("current_ma[%d]\n", current_ma);
		}
	}
}

void oplus_chg_config_charger_vsys_threshold(int val)
{
	if (!g_charger_chip) {
		return;
	} else {
		if (g_charger_chip->chg_ops->set_charger_vsys_threshold && oplus_vooc_get_allow_reading() == true) {
			g_charger_chip->chg_ops->set_charger_vsys_threshold(val);
			chg_err("set val[%d]\n", val);
		}
	}
}

void oplus_chg_pdqc9v_vindpm_vol_switch(int val)
{
	if (!g_charger_chip) {
		return;
	} else {
		if (g_charger_chip->pdqc_9v_voltage_adaptive && g_charger_chip->chg_ops->set_aicl_point &&
		    oplus_vooc_get_fastchg_started() == false) {
			oplus_chg_set_aicl_point(g_charger_chip);
			chg_err("set val[%d]\n", val);
		}
	}
}

void oplus_chg_enable_burst_mode(bool enable)
{
	if (!g_charger_chip) {
		return;
	} else {
		if (g_charger_chip->chg_ops->enable_burst_mode && oplus_vooc_get_allow_reading() == true) {
			g_charger_chip->chg_ops->enable_burst_mode(enable);
			chg_err("set val[%d]\n", enable);
		}
	}
}

int oplus_chg_get_tbatt_status(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->tbatt_status;
	}
}

void oplus_chg_vooc_timeout_callback(bool vbus_rising)
{
	if (g_charger_chip && g_charger_chip->chg_ops && g_charger_chip->chg_ops->vooc_timeout_callback)
		g_charger_chip->chg_ops->vooc_timeout_callback(vbus_rising);
}

void oplus_chg_really_suspend_charger(bool en)
{
	if (g_charger_chip && g_charger_chip->chg_ops && g_charger_chip->chg_ops->really_suspend_charger)
		g_charger_chip->chg_ops->really_suspend_charger(en);
}

#define BATT_MIN_CURR_CHECK 2000
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FAULT_INJECT_CHG)
noinline
#endif
int oplus_chg_match_temp_for_chging(void)
{
	int batt_temp = 0;
	int sub_batt_temp = 0;
	int shell_temp = 0;
	int diff = 0;
	int sub_diff = 0;
	int chging_temp = 0;
	int batt_current = 0;
	int parallel_batt_temp = 0;

	if (!g_charger_chip) {
		return chging_temp;
	}

	if (oplus_switching_support_parallel_chg()) {
		batt_temp = oplus_gauge_get_batt_temperature();
		sub_batt_temp = oplus_gauge_get_sub_batt_temperature();
		g_charger_chip->main_batt_temperature = batt_temp;
		g_charger_chip->sub_batt_temperature = sub_batt_temp;
		g_charger_chip->tbatt_temp = batt_temp;
		parallel_batt_temp = batt_temp > sub_batt_temp ? batt_temp : sub_batt_temp;

		if (oplus_chg_override_by_shell_temp(parallel_batt_temp)) {
			shell_temp = oplus_chg_get_shell_temp();
			diff = shell_temp - batt_temp;
			sub_diff = shell_temp - sub_batt_temp;
			if (diff < 150 && diff > -150 && (batt_temp >= 320 || sub_batt_temp >= 320) &&
			    batt_temp < g_charger_chip->limits.hot_bat_decidegc && sub_diff < 150 && sub_diff > -150 &&
			    sub_batt_temp < g_charger_chip->limits.hot_bat_decidegc) {
				chging_temp = shell_temp;
			} else {
				if (sub_batt_temp == FG_I2C_ERROR) {
					chging_temp = batt_temp;
				} else if (batt_temp >= 320 || sub_batt_temp >= 320) {
					chging_temp = batt_temp > sub_batt_temp ? batt_temp : sub_batt_temp;
				} else {
					chging_temp = batt_temp < sub_batt_temp ? batt_temp : sub_batt_temp;
				}
			}
		} else {
			if (sub_batt_temp == FG_I2C_ERROR) {
				chging_temp = batt_temp;
			} else if (batt_temp >= 320 || sub_batt_temp >= 320) {
				chging_temp = batt_temp > sub_batt_temp ? batt_temp : sub_batt_temp;
			} else {
				chging_temp = batt_temp < sub_batt_temp ? batt_temp : sub_batt_temp;
			}
		}
	} else {
		if ((oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
		     oplus_pps_get_support_type() == PPS_SUPPORT_3CP ||
		     g_charger_chip->vooc_project == DUAL_BATT_100W || g_charger_chip->support_subboard_ntc) &&
		     g_charger_chip->chg_ops->get_subboard_temp) {
			g_charger_chip->subboard_temp = g_charger_chip->chg_ops->get_subboard_temp();
			batt_temp = oplus_gauge_get_batt_temperature();
			if (get_eng_version() == HIGH_TEMP_AGING || oplus_is_ptcrb_version()) {
				chg_err("[OPLUS_CHG]CONFIG_HIGH_TEMP_VERSION "
					"enable here, disable high tbat shutdown\n");
				if (batt_temp > 690)
					batt_temp = 690;
				if (g_charger_chip->subboard_temp > 690)
					g_charger_chip->subboard_temp = 690;
			}
			if ((gauge_dbg_tbat != 0) || (sub_gauge_dbg_tbat != 0) || (batt_temp <= -400))
				g_charger_chip->subboard_temp = batt_temp;
		} else if (oplus_chg_get_voocphy_support() == NO_VOOCPHY && oplus_vooc_get_fastchg_started() == true) {
			batt_temp = oplus_gauge_get_prev_batt_temperature();
		} else {
			batt_temp = oplus_gauge_get_batt_temperature();
		}

		if (g_charger_chip->subboard_temp != TEMPERATURE_INVALID) {
			if (g_charger_chip->tbatt_use_subboard_temp)
				g_charger_chip->tbatt_temp = g_charger_chip->subboard_temp;
			else
				g_charger_chip->tbatt_temp = batt_temp;
			batt_temp = g_charger_chip->subboard_temp;
		} else {
			g_charger_chip->tbatt_temp = batt_temp;
		}
		if (oplus_chg_override_by_shell_temp(batt_temp)) {
			shell_temp = oplus_chg_get_shell_temp();
			diff = shell_temp - batt_temp;
			if (diff < 150 && diff > -150 && batt_temp >= 320 &&
			    batt_temp < g_charger_chip->limits.hot_bat_decidegc) {
				chging_temp = shell_temp;
			} else {
				chging_temp = batt_temp;
			}

			if (g_charger_chip->support_tbatt_shell) {
				if (oplus_chg_get_voocphy_support() == NO_VOOCPHY &&
				    oplus_vooc_get_fastchg_started() == true)
					batt_current = -oplus_gauge_get_prev_batt_current();
				else
					batt_current = -oplus_gauge_get_batt_current();

				if (!g_charger_chip->tbatt_shell_status &&
				    (shell_temp > g_charger_chip->limits.warm_bat_decidegc ||
				     (batt_temp > g_charger_chip->limits.warm_bat_decidegc &&
				      batt_current <= BATT_MIN_CURR_CHECK))) {
					g_charger_chip->tbatt_shell_status = true;
				} else if (g_charger_chip->tbatt_shell_status &&
					   (shell_temp < g_charger_chip->limits.warm_bat_decidegc &&
					    batt_temp < g_charger_chip->limits.warm_bat_decidegc)) {
					g_charger_chip->tbatt_shell_status = false;
				}
				if (g_charger_chip->tbatt_shell_status)
					chging_temp = shell_temp > batt_temp ? shell_temp : batt_temp;
			}
		} else {
			chging_temp = batt_temp;
		}
	}

	if (g_charger_chip->bms_heat_temp_compensation != 0
		&& chging_temp < OPLUS_BMS_HEAT_THRE)
		chging_temp = chging_temp - g_charger_chip->bms_heat_temp_compensation;


	return chging_temp;
}

int oplus_chg_get_charger_cycle(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int ret = 0;

	if (!chip) {
		return 0;
	}

	if (chip->chg_ops->get_charger_cycle)
		ret = chip->chg_ops->get_charger_cycle();
	return ret;
}

void oplus_chg_get_props_from_adsp_by_buffer(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return;
	}

	if (chip->fg_info_package_read_support == false)
		return;

	if (chip->chg_ops->get_props_from_adsp_by_buffer)
		chip->chg_ops->get_props_from_adsp_by_buffer();
}

bool oplus_get_flash_screen_ctrl(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;

	if (!chip) {
		return false;
	}

	if (chip->flash_screen_ctrl_status & FLASH_SCREEN_CTRL_OTA) {
		if (chip->flash_screen_ctrl_status & FLASH_SCREEN_CTRL_DTSI) {
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

static void soc_modfiy_callback(void)
{
	soc_store[1] = g_charger_chip->modify_soc;
	soc_store[2] = 1;
	if (get_soc_feature()) {
		soc_ajusting = true;
		night_count++;
		chg_err("[soc_ajust_feature]:soc_store0[%d], soc_store1[%d], "
			"g_charger_chip->soc[%d], night_count[%d]\n",
			soc_store[0], soc_store[1], g_charger_chip->soc, night_count);
		if (night_count == 1) {
			g_charger_chip->soc = g_charger_chip->soc - soc_store[0] + soc_store[1];
		} else {
			if (soc_store[0] > soc_store[1]) {
				g_charger_chip->soc = g_charger_chip->soc - soc_store[0] + soc_store[1] + 1;
			} else {
				g_charger_chip->soc = g_charger_chip->soc - soc_store[0] + soc_store[1];
			}
		}
		soc_store[0] = g_charger_chip->modify_soc;
	} else {
		soc_ajusting = false;
		chg_err("[soc_ajust_feature]:turned_off\n");
	}
	chg_err("[soc_ajust_feature]:modify_soc[%d], soc[%d], soc_ajusting[%d], real_soc[%d]\n",
		g_charger_chip->modify_soc, g_charger_chip->soc, soc_ajusting, oplus_gauge_get_batt_soc());
	chg_err("[soc_ajust_feature]:soc_store[%d], [%d], [%d]\n", soc_store[0], soc_store[1], soc_store[2]);
}

static void get_time_interval(void)
{
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->second_ktime = ktime_to_ms(ktime_get_boottime()) / 1000;
		chg_err("[soc_ajust_feature]:interval[%d], [%d]\n", g_charger_chip->first_ktime,
			g_charger_chip->second_ktime);
		if ((g_charger_chip->second_ktime - g_charger_chip->first_ktime) >= TIME_INTERVALE_S) {
			chg_err("[soc_ajust_feature]:set_interval[%d], [%d]\n", g_charger_chip->first_ktime,
				g_charger_chip->second_ktime);
			soc_modfiy_callback();
			g_charger_chip->first_ktime = g_charger_chip->second_ktime;
		}
	}
}

static int get_soc_feature(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		chg_err("get_soc_feature [soc_ajust_feature]:soc_ajust[%d]\n", g_charger_chip->soc_ajust);
		return g_charger_chip->soc_ajust;
	}
}

int set_soc_feature(void)
{
	chg_err("[soc_ajust_feature]: soc_ajust[%d]\n", g_charger_chip->soc_ajust);
	if (g_charger_chip->modify_soc) {
		soc_store[0] = g_charger_chip->modify_soc;
		chg_err("[soc_ajust_feature]:soc_store0=[%d]\n", soc_store[0]);
	}
	soc_ajusting = false;
	g_charger_chip->first_ktime = ktime_to_ms(ktime_get_boottime()) / 1000;
	return 0;
}

struct oplus_chg_chip *oplus_chg_get_chg_struct(void)
{
	return g_charger_chip;
}

int oplus_chg_get_fast_chg_type(void)
{
	int type = 0;
	int rc;
	union oplus_chg_mod_propval pval;

	if (!g_charger_chip) {
		return 0;
	}

	type = oplus_vooc_get_fast_chg_type();
	if (type == 0 && g_charger_chip->chg_ops->get_charger_subtype) {
		type = g_charger_chip->chg_ops->get_charger_subtype();
	}

	if (type == 0) {
		if (is_wls_ocm_available(g_charger_chip)) {
			rc = oplus_chg_mod_get_property(g_charger_chip->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &pval);
			if (rc == 0) {
				if ((pval.intval == OPLUS_CHG_WLS_VOOC) || (pval.intval == OPLUS_CHG_WLS_SVOOC) ||
				    (pval.intval == OPLUS_CHG_WLS_PD_65W)) {
					rc = oplus_chg_mod_get_property(g_charger_chip->wls_ocm,
									OPLUS_CHG_PROP_ADAPTER_TYPE, &pval);
					if (rc == 0)
						type = pval.intval;
					if (type == WLS_ADAPTER_TYPE_PD_65W)
						type = WLS_ADAPTER_TYPE_SVOOC;
				}
			}
		} else {
			if (oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_VOOC ||
			    oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_SVOOC)
				type = oplus_wpc_get_adapter_type();
		}
	}

	return type;
}

static void parallel_chg_set_balance_bat_status(int status)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	switch (status) {
	case PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE:
	case PARALLEL_NEED_BALANCE_BAT_STATUS1__STOP_CHARGE:
	case PARALLEL_NEED_BALANCE_BAT_STATUS2__STOP_CHARGE:
	case PARALLEL_NEED_BALANCE_BAT_STATUS3__STOP_CHARGE:
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS4__STOP_CHARGE:
		if (chip->balancing_bat_status == PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE &&
		    chip->charger_exist) {
			status = PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE;
		}
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE:
	case PARALLEL_BAT_BALANCE_ERROR_STATUS6__STOP_CHARGE:
	case PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE:
		break;
	}

	if (chip->balancing_bat_status != status) {
		switch (status) {
		case PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE:
			chip->balancing_bat_stop_chg = 0;
			chip->balancing_bat_stop_fastchg = 0;
			if (chip->charger_exist && chip->balancing_bat_status != -1 && chip->mmi_chg == 1 &&
			    oplus_switching_support_parallel_chg() != PARALLEL_MOS_CTRL) {
				oplus_chg_turn_on_charging(chip);
			}
			break;
		case PARALLEL_NEED_BALANCE_BAT_STATUS1__STOP_CHARGE:
		case PARALLEL_NEED_BALANCE_BAT_STATUS2__STOP_CHARGE:
		case PARALLEL_NEED_BALANCE_BAT_STATUS3__STOP_CHARGE:
		case PARALLEL_NEED_BALANCE_BAT_STATUS4__STOP_CHARGE:
		case PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE:
			if (chip->charger_exist) {
				oplus_chg_turn_off_charging(chip);
			}
			chip->balancing_bat_stop_chg = 1;
			break;
		case PARALLEL_BAT_BALANCE_ERROR_STATUS6__STOP_CHARGE:
			chip->balancing_bat_stop_fastchg = 1;
			break;
		case PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE:
			chip->balancing_bat_stop_chg = 0;
			chip->balancing_bat_stop_fastchg = 1;
			if (chip->charger_exist && chip->balancing_bat_status != -1 && chip->mmi_chg == 1) {
				oplus_chg_turn_on_charging(chip);
			}
			break;
		case PARALLEL_BAT_BALANCE_ERROR_STATUS8:
			chip->balancing_bat_stop_fastchg = 1;
			if (oplus_vooc_get_fastchg_started() == true) {
				oplus_chg_set_chargerid_switch_val(0);
				oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
			}
			chip->hmac = 0;
			break;
		default:
			break;
		}
		chip->balancing_bat_status = status;
		oplus_switching_set_balance_bat_status(chip->balancing_bat_status);
	}
}

static int parallel_chg_check_balance_bat_status(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int status;
	int i = 0;
	if (!chip) {
		return 0;
	}
	if (!atomic_read(&chip->mos_lock)) {
		chg_err("mos test start, check next time!");
		return 0;
	}

	for (i = 0; i < 3; i++) {
		status = oplus_switching_get_if_need_balance_bat(chip->batt_volt, chip->sub_batt_volt);
		if (status == PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE) {
			break;
		}
		msleep(50);
		chip->sub_batt_volt = oplus_gauge_get_sub_batt_mvolts();
		chip->batt_volt = oplus_gauge_get_batt_mvolts();
	}
	chg_err("balancing_bat_status:%d, current_status:%d", chip->balancing_bat_status, status);

	if (chip->charger_exist && chip->balancing_bat_status == PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE &&
	    status != PARALLEL_BAT_BALANCE_ERROR_STATUS8 && status != PARALLEL_BAT_BALANCE_ERROR_STATUS9) {
		return 0;
	}
	if (chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS8) {
		parallel_chg_set_balance_bat_status(chip->balancing_bat_status);
		return 0;
	}

	if ((oplus_vooc_get_fastchg_to_normal() == true || oplus_vooc_get_fastchg_to_warm() == true) &&
	    status != PARALLEL_BAT_BALANCE_ERROR_STATUS8 && status != PARALLEL_BAT_BALANCE_ERROR_STATUS9) {
		chg_err("fastchg_to_normal or fastchg_to_warm,no need balance");
		chip->balancing_bat_status = PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
		oplus_switching_set_balance_bat_status(chip->balancing_bat_status);
		return 0;
	}

	if (!atomic_dec_and_test(&chip->mos_lock)) {
		chg_err("mos test start, check next time!");
		atomic_inc(&chip->mos_lock);
		return 0;
	}
	parallel_chg_set_balance_bat_status(status);
	atomic_inc(&chip->mos_lock);
	return 0;
}
void oplus_chg_check_break(int vbus_rising)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	if (chip == NULL) {
		chg_err("chip is NUL\n");
		return;
	}
	if (vbus_rising) {
		chip->svooc_detect_time = local_clock() / 1000000;
		oplus_chg_check_abnormal_adapter(vbus_rising);
		if (((chip->svooc_detect_time - chip->svooc_detach_time) <= 1000) &&
		    oplus_vooc_get_detach_unexpectly() && chip->temperature <= 440 && chip->temperature >= 45) {
			chip->detect_detach_unexpeactly = 1;
			if (chip->svooc_disconnect_count < 3) {
				chip->svooc_disconnect_count += 1;
			}
			chg_err("svooc_disconnect_count = %d\n", chip->svooc_disconnect_count);
		} else if (((chip->svooc_detect_time - chip->svooc_detach_time) <= 100)) {
			chip->detect_detach_unexpeactly = 2;
		} else {
			if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
				oplus_voocphy_clear_cnt_info();
				oplus_voocphy_clear_dbg_info();
			}
			chip->svooc_disconnect_count = 0;
		}
		chg_err("svooc_detect_time = %d, chg->svooc_detach_time = %d\n", chip->svooc_detect_time,
			chip->svooc_detach_time);
		chip->svooc_detect_time = 0;
		chip->svooc_detach_time = 0;
		oplus_vooc_set_detach_unexpectly(false);
	} else {
		chip->svooc_detach_time = local_clock() / 1000000;
		oplus_chg_check_abnormal_adapter(vbus_rising);
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			oplus_voocphy_get_dbg_info();
		}
		chg_err("svooc_detach_time = %d\n", chip->svooc_detach_time);
	}
	return;
}

int oplus_chg_set_enable_volatile_writes(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_enable_volatile_writes) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_enable_volatile_writes();
	}
}

int oplus_chg_set_complete_charge_timeout(int val)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_complete_charge_timeout) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_complete_charge_timeout(val);
	}
}

int oplus_chg_set_prechg_voltage_threshold(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_prechg_voltage_threshold) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_prechg_voltage_threshold();
	}
}

int oplus_chg_set_prechg_current(int ipre_mA)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_prechg_current) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_prechg_current(ipre_mA);
	}
}

int oplus_chg_set_vindpm_vol(int vol)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_vindpm_vol) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_vindpm_vol(vol);
	}
}

int oplus_chg_disable_buck_switch(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->disable_buck_switch) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->disable_buck_switch();
	}
}

int oplus_chg_disable_async_mode(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->disable_async_mode) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->disable_async_mode();
	}
}

int oplus_chg_set_switching_frequency(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_switching_frequency) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_switching_frequency();
	}
}

int oplus_chg_set_mps_otg_current(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_mps_otg_current) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_mps_otg_current();
	}
}

int oplus_chg_set_mps_otg_voltage(bool is_9v)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_mps_otg_voltage) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_mps_otg_voltage(is_9v);
	}
}

int oplus_chg_set_mps_second_otg_voltage(bool is_750mv)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_mps_second_otg_voltage) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_mps_second_otg_voltage(is_750mv);
	}
}

int oplus_chg_set_wdt_timer(int reg)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_wdt_timer) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_wdt_timer(reg);
	}
}

int oplus_chg_set_voltage_slew_rate(int value)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->set_voltage_slew_rate) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->set_voltage_slew_rate(value);
	}
}

int oplus_chg_otg_wait_vbus_decline(void)
{
	if (!g_charger_chip || !g_charger_chip->chg_ops || !g_charger_chip->chg_ops->otg_wait_vbus_decline) {
		return 0;
	} else {
		return g_charger_chip->chg_ops->otg_wait_vbus_decline();
	}
}

static void oplus_chg_check_abnormal_adapter(int vbus_rising)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	static bool pre_is_abnormal_adapter = false;
	static bool pre_pd_svooc = false;

	if (!chip) {
		return;
	}

	if (!chip->support_abnormal_adapter) {
		return;
	}

	if (oplus_chg_fg_package_read_support()) {
		if (!vbus_rising) {
			pre_pd_svooc = chip->pd_svooc;
			pre_is_abnormal_adapter = chip->is_abnormal_adapter;

			if (!chip->mmi_chg
					|| oplus_vooc_get_fastchg_to_warm()
					|| oplus_vooc_get_fastchg_to_normal()
					|| !oplus_vooc_get_detach_unexpectly()) {
				pre_is_abnormal_adapter = false;
				chg_err("pre_is_abnormal_adapter is false,set set_icon_debounce false");
				oplus_chg_set_icon_debounce(false);
			}

			chg_err("vbus_down [%d %d %d %d %d]\n", pre_pd_svooc, pre_is_abnormal_adapter, chip->mmi_chg,
				chip->icon_debounce, chip->abnormal_adapter_dis_cnt);
			return;
		}

		if (chip->mmi_chg
				&& pre_is_abnormal_adapter
				&& (oplus_vooc_get_detach_unexpectly() || oplus_vooc_get_fastchg_ing())
				&& (chip->svooc_detect_time - chip->svooc_detach_time) <= 1500) {
			chip->abnormal_adapter_dis_cnt++;
		} else {
			oplus_chg_clear_abnormal_adapter_var();
		}

		chg_err("vbus_up [%d %d %d %d %d]\n", pre_pd_svooc, pre_is_abnormal_adapter, chip->mmi_chg, chip->icon_debounce,
			chip->abnormal_adapter_dis_cnt);
	} else {
		if (!vbus_rising) {
			pre_pd_svooc = chip->pd_svooc;
			pre_is_abnormal_adapter = chip->is_abnormal_adapter;
			chip->is_abnormal_adapter = false;

			if (!chip->mmi_chg || oplus_vooc_get_fastchg_to_warm() || oplus_vooc_get_fastchg_to_normal() ||
			    oplus_vooc_get_fastchg_dummy_started() ||
			    (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
			    oplus_vooc_get_btb_temp_over() || !oplus_vooc_get_fastchg_ing() ||
			    oplus_vooc_get_detach_unexpectly()) {
				pre_is_abnormal_adapter = false;
			}

			if (pre_pd_svooc && pre_is_abnormal_adapter) {
				chip->icon_debounce = true;
			} else {
				chip->icon_debounce = false;
			}
			chg_err("vbus_down [%d %d %d %d %d]\n", pre_pd_svooc, pre_is_abnormal_adapter, chip->mmi_chg,
				chip->icon_debounce, chip->abnormal_adapter_dis_cnt);
			return;
		}

		if (chip->mmi_chg && pre_pd_svooc && pre_is_abnormal_adapter &&
		    (oplus_vooc_get_detach_unexpectly() || oplus_vooc_get_fastchg_ing()) &&
		    (chip->svooc_detect_time - chip->svooc_detach_time) <= 1500) {
			chip->abnormal_adapter_dis_cnt++;
		} else {
			chip->abnormal_adapter_dis_cnt = 0;
			chip->icon_debounce = false;
		}

		chg_err("vbus_up [%d %d %d %d %d]\n", pre_pd_svooc, pre_is_abnormal_adapter, chip->mmi_chg, chip->icon_debounce,
			chip->abnormal_adapter_dis_cnt);
	}
}

int oplus_chg_get_abnormal_adapter_dis_cnt(void)
{
	if (!g_charger_chip) {
		return 0;
	}

	return g_charger_chip->abnormal_adapter_dis_cnt;
}

void oplus_chg_set_abnormal_adapter_dis_cnt(int count)
{
	if (g_charger_chip) {
		g_charger_chip->abnormal_adapter_dis_cnt = count;
	}
}

bool oplus_chg_get_icon_debounce(void)
{
	if (!g_charger_chip) {
		return false;
	}
	return g_charger_chip->icon_debounce;
}

void oplus_chg_set_icon_debounce(bool status)
{
	if (g_charger_chip) {
		g_charger_chip->icon_debounce = status;
	}
}

void oplus_chg_clear_abnormal_adapter_var(void)
{
	chg_err("clear abnormal_adapter var\n");
	if (g_charger_chip) {
		g_charger_chip->icon_debounce = false;
		g_charger_chip->is_abnormal_adapter = false;
		g_charger_chip->bidirect_abnormal_adapter = false;
		g_charger_chip->abnormal_adapter_dis_cnt = 0;
	}
}

bool oplus_chg_is_abnormal_adapter(void)
{
	if (!g_charger_chip) {
		return false;
	}
	return g_charger_chip->is_abnormal_adapter;
}

int oplus_chg_get_pps_type(void)
{
	if (!g_charger_chip) {
		return PD_INACTIVE;
	}

	if (!g_charger_chip->chg_ops || !g_charger_chip->chg_ops->oplus_chg_get_pd_type) {
		return PD_INACTIVE;
	}

	return g_charger_chip->chg_ops->oplus_chg_get_pd_type();
}

#ifndef CONFIG_OPLUS_CHARGER_MTK
int oplus_chg_get_wls_status_keep(void)
{
	if (!g_charger_chip) {
		return WLS_SK_NULL;
	}
	return g_charger_chip->wls_status_keep;
}

void oplus_chg_set_wls_status_keep(int value)
{
	if (g_charger_chip) {
		g_charger_chip->wls_status_keep = value;
	}
}
#endif

bool oplus_chg_get_dischg_flag(void)
{
	if (!g_charger_chip) {
		return false;
	}

	return g_charger_chip->dischg_flag;
}
EXPORT_SYMBOL(oplus_chg_get_dischg_flag);

bool opchg_get_shipmode_value(void)
{
	if (!g_charger_chip) {
		return 0;
	} else {
		return g_charger_chip->enable_shipmode;
	}
}

bool oplus_chg_get_wait_for_ffc_flag(void)
{
	if (!g_charger_chip)
		return false;

	return g_charger_chip->waiting_for_ffc;
}

void oplus_chg_set_wait_for_ffc_flag(bool wait_for_ffc)
{
	if (g_charger_chip)
		g_charger_chip->waiting_for_ffc = wait_for_ffc;
}

void oplus_chg_set_force_psy_changed(void)
{
	if (!g_charger_chip) {
		return;
	} else {
		g_charger_chip->force_psy_changed = true;
	}
}

int oplus_chg_get_design_capacity(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return (g_charger_chip->batt_capacity_mah);
	}
}

bool oplus_chg_get_bcc_support(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return (g_charger_chip->smart_chg_bcc_support || oplus_vooc_get_bcc_support());
	}
}

void oplus_chg_check_bcc_curr_done(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;

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

int oplus_chg_get_bcc_curr_done_status(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
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

int oplus_chg_get_batt_num(void)
{
	if (!g_charger_chip) {
		return 2;
	} else {
		return g_charger_chip->vbatt_num;
	}
}

void  oplus_chg_set_adsp_notify_ap_suspend(void)
{
	if (!g_charger_chip) {
		return;
	}

	g_charger_chip->adsp_notify_ap_suspend = true;
}

bool oplus_chg_get_adsp_notify_ap_suspend(void)
{
	if (!g_charger_chip) {
		return 0;
	}

	return g_charger_chip->adsp_notify_ap_suspend;
}

bool oplus_chg_get_boot_completed(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->boot_completed;
	}
}

bool oplus_get_vooc_start_fg(void)
{
	if (!g_charger_chip) {
		return false;
	} else {
		return g_charger_chip->vooc_start_fail;
	}
}

int oplus_chg_get_stop_chg(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return 0;
	}
	return chip->stop_chg;
}

static void quick_mode_check(void)
{
	struct oplus_chg_chip *chip = g_charger_chip;
	int diff_time = 0, gain_time_ms = 0, batt_curve_current = 0, current_cool_down = 0,
	    current_normal_cool_down = 0;
	struct timespec ts_now;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (!chip->quick_mode_gain_support) {
		chg_err("%s, not support!\n", __func__);
		return;
	}

	if (oplus_pps_get_support_type() != PPS_SUPPORT_NOT) {
		if (oplus_pps_get_chg_status() != PPS_CHARGERING)
			return;
		batt_curve_current = oplus_chg_pps_get_batt_curve_current();
		current_cool_down = oplus_chg_pps_get_current_cool_down();
		current_normal_cool_down = oplus_chg_pps_get_current_normal_cool_down();
	} else {
		if (oplus_vooc_get_fastchg_started() == false)
			return;
		batt_curve_current = oplus_voocphy_get_batt_curve_current();
		current_cool_down = oplus_convert_level_to_current(chip, chip->cool_down);
		current_normal_cool_down = oplus_convert_level_to_current(chip, chip->normal_cool_down);
		chg_err("%s, batt_curv=%d, current_now=%d, normal_cool_down=%d \n", __func__, batt_curve_current, current_cool_down, current_normal_cool_down);
	}

	ts_now = current_kernel_time();
	diff_time = ts_now.tv_sec - chip->quick_mode_time.tv_sec;
	chip->quick_mode_time = ts_now;
	if ((current_cool_down != current_normal_cool_down) && (current_cool_down != 0)) {
		if (chip->quick_mode_start_time == 0) {
			chip->quick_mode_start_time = ts_now.tv_sec;
			chip->quick_mode_start_cap = chip->batt_rm;
		}
		chip->quick_mode_need_update = true;
	}

	if ((current_cool_down == current_normal_cool_down) && (chip->quick_mode_need_update == true)) {
		chip->quick_mode_stop_time = ts_now.tv_sec;
		chip->quick_mode_stop_cap = chip->batt_rm;
		chip->quick_mode_stop_temp = chip->temperature;
		chip->quick_mode_stop_soc = chip->soc;
		chip->quick_mode_need_update = false;
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

	if (current_cool_down == current_normal_cool_down) {
		gain_time_ms = 0;
	} else if (current_cool_down == 0) {
		if (current_normal_cool_down < batt_curve_current)
			gain_time_ms = ((batt_curve_current * diff_time * 1000) / current_normal_cool_down) -
				(diff_time * 1000);
		else
			gain_time_ms = 0;
	}

	if (gain_time_ms < 0) {
		chg_err("%s:gain_time_ms:%d, force set 0 \n", __func__, gain_time_ms);
		gain_time_ms = 0;
	}

	chip->quick_mode_gain_time_ms = chip->quick_mode_gain_time_ms + gain_time_ms;

	chg_err("quick_mode_gain_time_ms:%d, start_cap:%d, stop_cap:%d, "
		"start_time:%d, stop_time:%d, diff_time:%d, gain_time_ms:%d\n",
		chip->quick_mode_gain_time_ms, chip->quick_mode_start_cap, chip->quick_mode_stop_cap,
		chip->quick_mode_start_time, chip->quick_mode_time.tv_sec, diff_time, gain_time_ms);
}

void oplus_first_enable_adsp_voocphy(void)
{
	if (!g_charger_chip || (oplus_chg_get_voocphy_support() != ADSP_VOOCPHY)) {
		return;
	}

	if (g_charger_chip->first_enabled_adspvoocphy != false) {
		return;
	} else {
		g_charger_chip->first_enabled_adspvoocphy = true;
		oplus_adsp_voocphy_turn_off();
		oplus_chg_suspend_charger();
		msleep(1500);
		oplus_chg_unsuspend_charger();
		msleep(500);
		oplus_adsp_voocphy_turn_on();
		return;
	}
}

int oplus_get_ccdetect_online(void) {
	struct oplus_chg_chip *chip = g_charger_chip;
	int online = 0;

	if (!chip) {
		chg_err(": oplus_chip not ready!\n");
		return 0;
	}
	if (!chip->chg_ops->get_ccdetect_online) {
		chg_err(": get_ccdetect_online not ready!\n");
		return 0;
	}
	online = chip->chg_ops->get_ccdetect_online();
	chg_err(" ccdetect_online:%d\n", online);
	return online;
}

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
void oplus_test_kit_unregister(void)
{
	if (IS_ERR_OR_NULL(g_charger_chip))
		return;

	test_kit_unreg_mtk_soc_gpio_check();
	test_kit_unreg_qcom_soc_gpio_check();
	if (!IS_ERR_OR_NULL(g_charger_chip->chg_switch1_gpio_test))
		test_feature_unregister(g_charger_chip->chg_switch1_gpio_test);
	if (!IS_ERR_OR_NULL(g_charger_chip->chg_switch2_gpio_test))
		test_feature_unregister(g_charger_chip->chg_switch2_gpio_test);
	if (!IS_ERR_OR_NULL(g_charger_chip->chg_uart_gpio_test))
		test_feature_unregister(g_charger_chip->chg_uart_gpio_test);

	if (!IS_ERR_OR_NULL(g_charger_chip->typec_port_test)) {
		test_kit_unreg_typec_port_check();
		test_feature_unregister(g_charger_chip->typec_port_test);
	}
}
#endif
