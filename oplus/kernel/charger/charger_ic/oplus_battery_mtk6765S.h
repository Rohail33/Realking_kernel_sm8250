/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_BATTERY_MTK6765R_H__
#define __OPLUS_BATTERY_MTK6765R_H__

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/alarmtimer.h>
#include <uapi/linux/sched/types.h>
#include <linux/uaccess.h>

#include "../../../../../kernel-4.19/drivers/power/supply/charger_class.h"
#include "../../../../../kernel-4.19/drivers/power/supply/adapter_class.h"
#include "../../../../../kernel-4.19/drivers/power/supply/mtk_pd.h"
#include "../../../../../kernel-4.19/drivers/power/supply/mtk_charger_algorithm_class.h"
#include "../../../../../kernel-4.19/drivers/misc/mediatek/typec/tcpc/inc/tcpm.h"

#define MAX_ALG_NO 10

#define CHARGING_INTERVAL 10
#define CHARGING_FULL_INTERVAL 20

#define CHRLOG_ERROR_LEVEL   1
#define CHRLOG_DEBUG_LEVEL   2
extern int chr_get_debug_level(void);

#define chr_err(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

#define chr_info(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_INFO_LEVEL) {	\
		pr_notice_ratelimited(fmt, ##args);		\
	}							\
} while (0)

#define chr_debug(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_DEBUG_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)


/* procfs */
#define PROC_FOPS_RW(name)						\
static int mtk_chg_##name##_open(struct inode *node, struct file *file)	\
{									\
	return single_open(file, mtk_chg_##name##_show, PDE_DATA(node));\
}									\
static const struct file_operations mtk_chg_##name##_fops = {		\
	.owner = THIS_MODULE,						\
	.open = mtk_chg_##name##_open,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
	.write = mtk_chg_##name##_write,				\
}

/* ==================================================================== */
/* mtk_charger_init.h begin */
#define BATTERY_CV 4350000
#define V_CHARGER_MAX 6500000 /* 6.5 V */
#define V_CHARGER_MIN 4600000 /* 4.6 V */

#define USB_CHARGER_CURRENT_SUSPEND		0 /* def CONFIG_USB_IF */
#define USB_CHARGER_CURRENT_UNCONFIGURED	70000 /* 70mA */
#define USB_CHARGER_CURRENT_CONFIGURED		500000 /* 500mA */
#define USB_CHARGER_CURRENT			500000 /* 500mA */
#define AC_CHARGER_CURRENT			2050000
#define AC_CHARGER_INPUT_CURRENT		3200000
#define NON_STD_AC_CHARGER_CURRENT		500000
#define CHARGING_HOST_CHARGER_CURRENT		650000
#define APPLE_1_0A_CHARGER_CURRENT		650000
#define APPLE_2_1A_CHARGER_CURRENT		800000
#define TA_AC_CHARGING_CURRENT	3000000

#define V_CHARGER_MIN_1 4400000 /* 4.4 V */
#define V_CHARGER_MIN_2 4200000 /* 4.2 V */
#define MAX_DMIVR_CHARGER_CURRENT 1400000 /* 1.4 A */

/* sw jeita */
#define JEITA_TEMP_ABOVE_T4_CV	4240000
#define JEITA_TEMP_T3_TO_T4_CV	4240000
#define JEITA_TEMP_T2_TO_T3_CV	4340000
#define JEITA_TEMP_T1_TO_T2_CV	4240000
#define JEITA_TEMP_T0_TO_T1_CV	4040000
#define JEITA_TEMP_BELOW_T0_CV	4040000
#define TEMP_T4_THRES  50
#define TEMP_T4_THRES_MINUS_X_DEGREE 47
#define TEMP_T3_THRES  45
#define TEMP_T3_THRES_MINUS_X_DEGREE 39
#define TEMP_T2_THRES  10
#define TEMP_T2_THRES_PLUS_X_DEGREE 16
#define TEMP_T1_THRES  0
#define TEMP_T1_THRES_PLUS_X_DEGREE 6
#define TEMP_T0_THRES  0
#define TEMP_T0_THRES_PLUS_X_DEGREE  0
#define TEMP_NEG_10_THRES 0

/* Battery Temperature Protection */
#define MIN_CHARGE_TEMP  0
#define MIN_CHARGE_TEMP_PLUS_X_DEGREE	6
#define MAX_CHARGE_TEMP  50
#define MAX_CHARGE_TEMP_MINUS_X_DEGREE	47

/* pe */
#define PE_ICHG_LEAVE_THRESHOLD 1000000 /* uA */
#define TA_AC_12V_INPUT_CURRENT 3200000
#define TA_AC_9V_INPUT_CURRENT	3200000
#define TA_AC_7V_INPUT_CURRENT	3200000
#define TA_9V_SUPPORT
#define TA_12V_SUPPORT

/* pe2.0 */
#define PE20_ICHG_LEAVE_THRESHOLD 1000000 /* uA */
#define TA_START_BATTERY_SOC	0
#define TA_STOP_BATTERY_SOC	85

/* dual charger */
#define TA_AC_MASTER_CHARGING_CURRENT 1500000
#define TA_AC_SLAVE_CHARGING_CURRENT 1500000

/* slave charger */
#define CHG2_EFF 90

/* cable measurement impedance */
#define CABLE_IMP_THRESHOLD 699
#define VBAT_CABLE_IMP_THRESHOLD 3900000 /* uV */

/* bif */
#define BIF_THRESHOLD1 4250000	/* UV */
#define BIF_THRESHOLD2 4300000	/* UV */
#define BIF_CV_UNDER_THRESHOLD2 4450000	/* UV */
#define BIF_CV BATTERY_CV /* UV */

#define R_SENSE 56 /* mohm */

#define MAX_CHARGING_TIME (12 * 60 * 60) /* 12 hours */

#define DEFAULT_BC12_CHARGER 0 /* MAIN_CHARGER */

#define PD_VBUS_UPPER_BOUND 10000000	/* uv */
#define PD_VBUS_LOW_BOUND 5000000	/* uv */
#define PD_ICHG_LEAVE_THRESHOLD 1000000 /* uA */
#define PD_STOP_BATTERY_SOC 80

#define VSYS_WATT 5000000
#define IBUS_ERR 14

/* battery warning */
#define BATTERY_NOTIFY_CASE_0001_VCHARGER
#define BATTERY_NOTIFY_CASE_0002_VBATTEMP
/* mtk_charger_init.h end */

#define CHR_CC		(0x0001)
#define CHR_TOPOFF	(0x0002)
#define CHR_TUNING	(0x0003)
#define CHR_POSTCC	(0x0004)
#define CHR_BATFULL	(0x0005)
#define CHR_ERROR	(0x0006)
#define	CHR_PE40_INIT	(0x0007)
#define	CHR_PE40_CC	(0x0008)
#define	CHR_PE40_TUNING	(0x0009)
#define	CHR_PE40_POSTCC	(0x000A)
#define CHR_PE30	(0x000B)

/* charging abnormal status */
#define CHG_VBUS_OV_STATUS	(1 << 0)
#define CHG_BAT_OT_STATUS	(1 << 1)
#define CHG_OC_STATUS		(1 << 2)
#define CHG_BAT_OV_STATUS	(1 << 3)
#define CHG_ST_TMO_STATUS	(1 << 4)
#define CHG_BAT_LT_STATUS	(1 << 5)
#define CHG_TYPEC_WD_STATUS	(1 << 6)

#ifdef OPLUS_FEATURE_CHG_BASIC
typedef enum {
	NTC_BATTERY,
	NTC_BATTERY_CON,
	NTC_CHARGER_IC,
	NTC_SUB_BOARD,
}NTC_TYPE;

struct temp_param {
	__s32 bts_temp;
	__s32 temperature_r;
};

struct ntc_temp{
	NTC_TYPE e_ntc_type;
	int i_tap_over_critical_low;
	int i_rap_pull_up_r;
	int i_rap_pull_up_voltage;
	int i_tap_min;
	int i_tap_max;
	unsigned int i_25c_volt;
	unsigned int ui_dwvolt;
	struct temp_param *pst_temp_table;
	int i_table_size;
};
#endif

struct mtk_charger;
/* charger_algorithm notify charger_dev */
enum {
	EVENT_EOC,
	EVENT_RECHARGE,
};


/*
 * Software JEITA
 * T0: -10 degree Celsius
 * T1: 0 degree Celsius
 * T2: 10 degree Celsius
 * T3: 45 degree Celsius
 * T4: 50 degree Celsius
 */
enum sw_jeita_state_enum {
	TEMP_BELOW_T0 = 0,
	TEMP_T0_TO_T1,
	TEMP_T1_TO_T2,
	TEMP_T2_TO_T3,
	TEMP_T3_TO_T4,
	TEMP_ABOVE_T4
};

struct sw_jeita_data {
	int sm;
	int pre_sm;
	int cv;
	bool charging;
	bool error_recovery_flag;
};

/* battery thermal protection */
enum bat_temp_state_enum {
	BAT_TEMP_LOW = 0,
	BAT_TEMP_NORMAL,
	BAT_TEMP_HIGH
};

struct battery_thermal_protection_data {
	int sm;
	bool enable_min_charge_temp;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
};

#define RT9471D 0
#define RT9467 1
#define BQ2589X 2
#define BQ2591X 3
#define SY6970 4
#define SGM4151X 5
#define BQ2560X 6
#define SGM41512 8

typedef enum {
        AP_TEMP_BELOW_T0 = 0,
        AP_TEMP_T0_TO_T1,
        AP_TEMP_T1_TO_T2,
        AP_TEMP_T2_TO_T3,
        AP_TEMP_ABOVE_T3
}OPLUS_CHG_AP_TEMP_STAT;

enum charger_type {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,      /* USB : 450mA */
	CHARGING_HOST,
	NONSTANDARD_CHARGER,    /* AC : 450mA~1A */
	STANDARD_CHARGER,   /* AC : ~1A */
	APPLE_2_1A_CHARGER, /* 2.1A apple charger */
	APPLE_1_0A_CHARGER, /* 1A apple charger */
	APPLE_0_5A_CHARGER, /* 0.5A apple charger */
	WIRELESS_CHARGER,
};

/* charger_manager notify charger_consumer */
enum {
	CHARGER_NOTIFY_EOC,
	CHARGER_NOTIFY_START_CHARGING,
	CHARGER_NOTIFY_STOP_CHARGING,
	CHARGER_NOTIFY_ERROR,
	CHARGER_NOTIFY_NORMAL,
};

enum {
	MAIN_CHARGER = 0,
	SLAVE_CHARGER = 1,
	TOTAL_CHARGER = 2,
	DIRECT_CHARGER = 10,
	MAIN_DIVIDER_CHARGER = 20,
	SLAVE_DIVIDER_CHARGER = 21,
};

extern enum charger_type mt_get_charger_type(void);

struct gtimer {
	char *name;
	struct device *dev;
	struct timespec endtime;
	int interval;
	int (*callback)(struct gtimer *gt);
	struct list_head list;
};


struct mtk_charger_algorithm {
	int (*do_algorithm)(struct mtk_charger *info);
	int (*plug_in)(struct mtk_charger *);
	int (*plug_out)(struct mtk_charger *);
	int (*do_charging)(struct mtk_charger *, bool en);
	int (*enable_charging)(struct mtk_charger *info, bool en);
	int (*do_event)(struct notifier_block *nb, unsigned long ev, void *v);
	int (*change_current_setting)(struct mtk_charger *info);
	void *algo_data;
};

/* PD charging */
struct mtk_pdc {
	struct tcpc_device *tcpc;
	struct adapter_power_cap cap;
	int pdc_max_watt;
	int pdc_max_watt_setting;

	bool check_impedance;
	int pd_cap_max_watt;
	int pd_idx;
	int pd_reset_idx;
	int pd_boost_idx;
	int pd_buck_idx;
	int vbus_l;
	int vbus_h;

	struct mutex access_lock;
	struct mutex pmic_sync_lock;
	struct wakeup_source suspend_lock;
	int ta_vchr_org;
	bool to_check_chr_type;
	bool to_tune_ta_vchr;
	bool is_cable_out_occur;
	bool is_connect;
	bool is_enabled;
};

struct charger_custom_data {
	int battery_cv;	/* uv */
	int max_charger_voltage;
	int max_charger_voltage_setting;
	int min_charger_voltage;

	int usb_charger_current_suspend;
	int usb_charger_current_unconfigured;
	int usb_charger_current_configured;
	int usb_charger_current;
	int ac_charger_current;
	int ac_charger_input_current;
	int non_std_ac_charger_current;
	int charging_host_charger_current;
	int apple_1_0a_charger_current;
	int apple_2_1a_charger_current;
	int ta_ac_charger_current;
	int pd_charger_current;

	/* dynamic mivr */
	int min_charger_voltage_1;
	int min_charger_voltage_2;
	int max_dmivr_charger_current;

	/* sw jeita */
	int jeita_temp_above_t4_cv;
	int jeita_temp_t3_to_t4_cv;
	int jeita_temp_t2_to_t3_cv;
	int jeita_temp_t1_to_t2_cv;
	int jeita_temp_t0_to_t1_cv;
	int jeita_temp_below_t0_cv;
	int temp_t4_thres;
	int temp_t4_thres_minus_x_degree;
	int temp_t3_thres;
	int temp_t3_thres_minus_x_degree;
	int temp_t2_thres;
	int temp_t2_thres_plus_x_degree;
	int temp_t1_thres;
	int temp_t1_thres_plus_x_degree;
	int temp_t0_thres;
	int temp_t0_thres_plus_x_degree;
	int temp_neg_10_thres;

	/* battery temperature protection */
	int mtk_temperature_recharge_support;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;
#ifndef CONFIG_OPLUS_CHARGER_MTK6765R
	/* pe */
	int pe_ichg_level_threshold;	/* ma */
	int ta_ac_12v_input_current;
	int ta_ac_9v_input_current;
	int ta_ac_7v_input_current;
	bool ta_12v_support;
	bool ta_9v_support;

	/* pe2.0 */
	int pe20_ichg_level_threshold;	/* ma */
	int ta_start_battery_soc;
	int ta_stop_battery_soc;

	/* pe4.0 */
	int pe40_single_charger_input_current;	/* ma */
	int pe40_single_charger_current;
	int pe40_dual_charger_input_current;
	int pe40_dual_charger_chg1_current;
	int pe40_dual_charger_chg2_current;
	int pe40_stop_battery_soc;
	int pe40_max_vbus;
	int pe40_max_ibus;
	int high_temp_to_leave_pe40;
	int high_temp_to_enter_pe40;
	int low_temp_to_leave_pe40;
	int low_temp_to_enter_pe40;

	/* pe4.0 cable impedance threshold (mohm) */
	u32 pe40_r_cable_1a_lower;
	u32 pe40_r_cable_2a_lower;
	u32 pe40_r_cable_3a_lower;
#endif
	/* dual charger */
	u32 chg1_ta_ac_charger_current;
	u32 chg2_ta_ac_charger_current;
	int slave_mivr_diff;
	u32 dual_polling_ieoc;

	/* slave charger */
	int chg2_eff;
	bool parallel_vbus;

	/* cable measurement impedance */
	int cable_imp_threshold;
	int vbat_cable_imp_threshold;

	/* bif */
	int bif_threshold1;	/* uv */
	int bif_threshold2;	/* uv */
	int bif_cv_under_threshold2;	/* uv */

	/* power path */
	bool power_path_support;

	int max_charging_time; /* second */

	int bc12_charger;

	/* pd */
	int pd_vbus_upper_bound;
	int pd_vbus_low_bound;
	int pd_ichg_level_threshold;
	int pd_stop_battery_soc;

	int vsys_watt;
	int ibus_err;
};

struct charger_data {
	int force_charging_current;
	int thermal_input_current_limit;
	int thermal_charging_current_limit;
	int input_current_limit;
	int charging_current_limit;
	int disable_charging_count;
	int input_current_limit_by_aicl;
	int junction_temp_min;
	int junction_temp_max;
};

struct hvdcp_v20 {
	bool is_enabled;
	bool is_connect;
};

enum chg_data_idx_enum {
	CHG1_SETTING,
	CHG2_SETTING,
	CHGS_SETTING_MAX,
};

struct mtk_charger {
	struct platform_device *pdev;
	struct charger_device *chg1_dev;
	struct notifier_block chg1_nb;
	struct charger_device *chg2_dev;
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct charger_device *dvchg1_dev;
	struct notifier_block dvchg1_nb;
	struct charger_data dvchg1_data;
	struct charger_consumer *chg1_consumer;

	struct charger_device *dvchg2_dev;
	struct notifier_block dvchg2_nb;
	struct charger_data dvchg2_data;
#endif
	struct charger_data chg_data[CHGS_SETTING_MAX];
	struct chg_limit_setting setting;
	enum charger_configuration config;

	struct power_supply_desc psy_desc1;
	struct power_supply_config psy_cfg1;
	struct power_supply *psy1;

	struct power_supply_desc psy_desc2;
	struct power_supply_config psy_cfg2;
	struct power_supply *psy2;

	struct adapter_device *pd_adapter;
	struct notifier_block pd_nb;
	struct mutex pd_lock;
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct iio_channel      *charger_id_chan;
	struct iio_channel      *usb_temp_v_l_chan;
	struct iio_channel      *usb_temp_v_r_chan;
	struct delayed_work	step_charging_work;
	int step_status;
	int step_status_pre;
	int step_cnt;
	int step_chg_current;
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	int ccdetect_gpio;
	int ccdetect_irq;
	struct pinctrl_state *ccdetect_active;
	struct pinctrl_state *ccdetect_sleep;
	struct pinctrl *pinctrl;
#endif
	struct mtk_pdc pdc;
	int pd_type;
	bool pd_reset;
	bool disable_pd_dual;
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct tcpc_device *tcpc;
#endif

	bool init_done;

	bool enable_type_c;

	bool water_detected;

	struct hvdcp_v20 hvdcp;

	u32 bootmode;
	u32 boottype;

	int chr_type;
	int usb_state;

	struct wakeup_source charger_wakelock;
	struct mutex cable_out_lock;
	struct mutex charger_pd_lock;
	int cable_out_cnt;

	/* system lock */
	spinlock_t slock;
	struct mutex charger_lock;

	/* thread related */
	wait_queue_head_t  wait_que;
	bool charger_thread_timeout;
	unsigned int polling_interval;
	bool charger_thread_polling;

	/* alarm timer */
	struct alarm charger_timer;
	struct timespec endtime;
	bool is_suspend;
	struct notifier_block pm_notifier;

	/* notify charger user */
	struct srcu_notifier_head evt_nh;

	struct notifier_block psy_nb;
	/* common info */
	int log_level;
	bool usb_unlimited;
	bool disable_charger;
	int battery_temp;
	bool can_charging;
	bool cmd_discharging;
	bool safety_timeout;
	bool vbusov_stat;
	bool is_chg_done;
	/* ATM */
	bool atm_enabled;

	const char *algorithm_name;
	struct mtk_charger_algorithm algo;

	/* dtsi custom data */
	struct charger_custom_data data;

	/* battery warning */
	unsigned int notify_code;
	unsigned int notify_test_mode;

	/* sw safety timer */
	bool enable_sw_safety_timer;
	bool sw_safety_timer_setting;
	struct timespec charging_begin_time;

	/* sw jeita */
	bool enable_sw_jeita;
	struct sw_jeita_data sw_jeita;

	/* battery thermal protection */
	struct battery_thermal_protection_data thermal;

	struct chg_alg_device *alg[MAX_ALG_NO];
	struct notifier_block chg_alg_nb;
	bool enable_hv_charging;
#ifdef OPLUS_FEATURE_CHG_BASIC
	bool chargerid_disable;
	bool chargerid_vol_disable;
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct iio_channel *chg_temp_chan;
	int chargeric_temp_volt;
	int chargeric_temp;
	bool support_ntc_01c_precision;
#endif
	bool in_good_connect;

	bool enable_dynamic_mivr;

	atomic_t enable_kpoc_shdn;
};

struct charger_consumer {
	struct device *dev;
	void *cm;
	struct notifier_block *pnb;
	struct list_head list;
	bool hv_charging_disabled;
	bool support_ntc_01c_precision;
	/*end*/
};

typedef enum {
        BATT_TEMP_EXTEND_BELOW_T0 = 0,
        BATT_TEMP_EXTEND_T0_TO_T1,
        BATT_TEMP_EXTEND_T1_TO_T2,
        BATT_TEMP_EXTEND_T2_TO_T3,
        BATT_TEMP_EXTEND_T3_TO_T4,
        BATT_TEMP_EXTEND_T4_TO_T5,
        BATT_TEMP_EXTEND_T5_TO_T6,
        BATT_TEMP_EXTEND_T6_TO_T7,
        BATT_TEMP_EXTEND_ABOVE_T7
}OPLUS_CHG_BATT_TEMP_EXTEND_STAT;

struct mtk_pmic {
	struct mtk_charger* oplus_info;
};

extern int mtk_chg_enable_vbus_ovp(bool enable);
extern enum charger_type mt_get_charger_type(void);

extern int mtk_basic_charger_init(struct mtk_charger *info);
extern int mtk_pulse_charger_init(struct mtk_charger *info);
extern int get_uisoc(struct mtk_charger *info);
extern int get_battery_voltage(struct mtk_charger *info);
extern int get_battery_temperature(struct mtk_charger *info);
extern int get_battery_current(struct mtk_charger *info);
extern int get_vbus(struct mtk_charger *info);
extern int get_ibus(struct mtk_charger *info);
extern bool is_battery_exist(struct mtk_charger *info);
extern int get_charger_type(struct mtk_charger *info);
extern int disable_hw_ovp(struct mtk_charger *info, int en);
extern bool is_charger_exist(struct mtk_charger *info);
extern int get_charger_temperature(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_charging_current(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_input_current(struct mtk_charger *info,
	struct charger_device *chg);
extern void _wake_up_charger(struct mtk_charger *info);
extern int get_chgntc_adc_temp(struct charger_device *chg_dev,
	int *tmin, int *tmax);
#endif /* __OPLUS_BATTERY_MTK6765R_H__ */

