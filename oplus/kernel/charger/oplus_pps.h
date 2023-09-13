// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2022 Oplus. All rights reserved.
 */

#ifndef _OPLUS_PPS_H_
#define _OPLUS_PPS_H_
#include <linux/time.h>
#include <linux/list.h>
#ifndef CONFIG_OPLUS_CHARGER_MTK
#include <linux/usb/typec.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
#include <linux/usb/usbpd.h>
#endif
#endif
#include <linux/random.h>
#include <linux/device.h>
#include <linux/types.h>
#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
#include <oplus_cfg.h>
#endif
#define pps_err(fmt, ...)                                                      \
	printk(KERN_ERR "[OPLUS_PPS][%s]" fmt, __func__, ##__VA_ARGS__)

#define PPS_MANAGER_VERSION "1.3.0"
/*pps curve*/
#define PPS_VOL_MAX_V1 11000
#define PPS_VOL_MAX_V2 20000
#define PPS_VOL_CURVE_LMAX 5500
#define PPS_EXIT_VBUS_MIN 6000
#define PPS_VBAT_DIFF_TIME round_jiffies_relative(msecs_to_jiffies(600 * 1000))
#define PPS_BCC_CURRENT_MIN (1000 / 100)
#define PPS_BATT_CURR_TO_BCC_CURR 100
#define BATT_PPS_SYS_MAX 40
#define FULL_PPS_SYS_MAX 6
#define PPS_R_AVG_NUM 10
#define PPS_R_ROW_NUM 7

/*pps update time*/
#define UPDATE_PDO_TIME 5
#define UPDATE_FASTCHG_TIME 1
#define UPDATE_TEMP_TIME 1
#define UPDATE_IBAT_TIME 1
#define UPDATE_BCC_TIME 2
#define UPDATE_POWER_TIME 1
#define UPDATE_FULL_TIME_NS 500 * 1000 * 1000
#define UPDATE_CURVE_TIME_NS 500 * 1000 * 1000
#define UPDATE_FULL_TIME_S 1
#define UPDATE_CURVE_TIME_S 1

/*pps power*/
#define OPLUS_EXTEND_IMIN 6250
#define OPLUS_EXTEND_VMIN 20000
#define OPLUS_PPS_IMIN_V3 11000
#define OPLUS_PPS_IMIN_V1 5000

/*pps protection*/
#define PPS_MASTER_CP_I2C_ERROR_COUNTS 14
#define PPS_SLAVE_CP_I2C_ERROR_COUNTS 8
#define PPS_CP_IBUS_OVER_COUNTS 3
#define PPS_CP_IBUS_MAX 6500
#define PPS_CP_IBUS_DIFF 2000

#define PPS_CP_TDIE_OVER_COUNTS 5
#define PPS_CP_TDIE_EXIT_COUNTS 20
#define PPS_CP_TDIE_MAX 110

#define PPS_TEMP_OVER_COUNTS 2
#define PPS_TEMP_WARM_RANGE_THD 10
#define PPS_TEMP_LOW_RANGE_THD 20

#define PPS_CURVE_OV_CNT 4
#define PPS_CP_IBUS_ABORNAL_MIN 100
#define PPS_CURVE_VBAT_DELTA_MV 5
#define PPS_CURVE_VBAT_DELTA_CNT 2
#define PPS_CURVE_IBUS_DELTA_MA 50


#define PPS_FG_TEMP_PROTECTION 800
#define PPS_TFG_OV_CNT 6
#define PPS_BTB_OV_CNT 8
#define PPS_TBATT_OV_CNT 1
#define PPS_DISCONNECT_IOUT_MIN 300
#define PPS_DISCONNECT_IOUT_CNT 3
#define BTB_CHECK_MAX_CNT 3
#define BTB_CHECK_TIME_US 10000

#define PPS_FULL_COUNTS_HW 3
#define PPS_FULL_COUNTS_COOL 6
#define PPS_FULL_COUNTS_SW 6
#define PPS_FULL_COUNTS_LOW_CURR 6

#define PPS_IBAT_LOW_MIN 2000
#define PPS_IBAT_LOW_CNT 4
#define PPS_IBAT_HIGH_CNT 8

#define PPS_POWER_SC_CNT 10
#define PPS_POWER_BP_CNT 20
#define PPS_SWITCH_VOL_V1 10000
#define PPS_SWITCH_VOL_V2 20000

#define PPS_VBAT_NUM 2
#define PPS_R_IBUS_MIN 100

#define PPS_IBUS_ABNORMAL_MIN 250
#define PPS_IBUS_SLAVE_DISABLE_MIN 2000
#define PPS_IBUS_SLAVE_ENABLE_MIN 2500
#define PPS_MASTER_ENALBE_CHECK_CNTS 10
#define PPS_SLAVLE_ENALBE_CHECK_CNTS 2
/*pps aciton*/
#define PPS_ACTION_START_DIFF_VOLT_V1 300
#define PPS_ACTION_START_DIFF_VOLT_V2 600
#define PPS_ACTION_CURR_MIN 1500

#define PPS_ACTION_START_DELAY 300
#define PPS_ACTION_MOS_DELAY 50
#define PPS_ACTION_VOLT_DELAY 200
#define PPS_ACTION_CURR_DELAY 200
#define PPS_ACTION_CHECK_DELAY 500
#define PPS_ACTION_CHECK_ICURR_CNT 3

#define PPS_RETRY_COUNT 1
#define PPS_CHANGE_VBUS_ERR_TIMES 3
#define PPS2SVOOC_VBUS_MIN 4000
#define PPS2SVOOC_VBUS_MAX 6000

/*pps other*/
#define PPS_DUMP_REG_CNT 10
#define PD_PPS_STATUS_VOLT(pps_status) (((pps_status) >> 0) & 0xFFFF)
#define PD_PPS_STATUS_CUR(pps_status) (((pps_status) >> 16) & 0xFF)

enum { PPS_BYPASS_MODE = 1,
       PPS_SC_MODE,
       PPS_UNKNOWN_MODE = 100,
};

enum {
	PPS_BAT_TEMP_NATURAL = 0,
	PPS_BAT_TEMP_HIGH0,
	PPS_BAT_TEMP_HIGH1,
	PPS_BAT_TEMP_HIGH2,
	PPS_BAT_TEMP_HIGH3,
	PPS_BAT_TEMP_HIGH4,
	PPS_BAT_TEMP_HIGH5,
	PPS_BAT_TEMP_LOW0,
	PPS_BAT_TEMP_LOW1,
	PPS_BAT_TEMP_LOW2,
	PPS_BAT_TEMP_LITTLE_COOL,
	PPS_BAT_TEMP_LITTLE_COOL_LOW,
	PPS_BAT_TEMP_COOL,
	PPS_BAT_TEMP_NORMAL_LOW, /*13*/
	PPS_BAT_TEMP_NORMAL_HIGH,
	PPS_BAT_TEMP_LITTLE_COLD,
	PPS_BAT_TEMP_WARM,
	PPS_BAT_TEMP_EXIT,
};
enum {
	PPS_TEMP_RANGE_INIT = 0,
	PPS_TEMP_RANGE_LITTLE_COLD, /*0 ~ 5*/
	PPS_TEMP_RANGE_COOL, /*5 ~ 12*/
	PPS_TEMP_RANGE_LITTLE_COOL, /*12~16*/
	PPS_TEMP_RANGE_NORMAL_LOW, /*16~25*/
	PPS_TEMP_RANGE_NORMAL_HIGH, /*25~43*/
	PPS_TEMP_RANGE_WARM, /*43-52*/
	PPS_TEMP_RANGE_NORMAL,
};

enum { OPLUS_PPS_STATUS_START = 0,
       OPLUS_PPS_STATUS_OPEN_MOS,
       OPLUS_PPS_STATUS_VOLT_CHANGE,
       OPLUS_PPS_STATUS_CUR_CHANGE,
       OPLUS_PPS_STATUS_CHECK,
       OPLUS_PPS_STATUS_FFC,
};

enum { PPS_ADAPTER_THIRD = 0,
       PPS_ADAPTER_OPLUS_V1,
       PPS_ADAPTER_OPLUS_V2,
       PPS_ADAPTER_OPLUS_V3,
       PPS_ADAPTER_UNKNOWN = 100,
};

enum { OPLUS_PPS_VOLT_UPDATE_V1 = 100,
       OPLUS_PPS_VOLT_UPDATE_V2 = 200,
       OPLUS_PPS_VOLT_UPDATE_V3 = 500,
       OPLUS_PPS_VOLT_UPDATE_V4 = 1000,
       OPLUS_PPS_VOLT_UPDATE_V5 = 2000,
       OPLUS_PPS_VOLT_UPDATE_V6 = 5000,
};

enum { OPLUS_PPS_CURR_UPDATE_V1 = 50,
       OPLUS_PPS_CURR_UPDATE_V2 = 100,
       OPLUS_PPS_CURR_UPDATE_V3 = 200,
       OPLUS_PPS_CURR_UPDATE_V4 = 300,
       OPLUS_PPS_CURR_UPDATE_V5 = 400,
       OPLUS_PPS_CURR_UPDATE_V6 = 500,
       OPLUS_PPS_CURR_UPDATE_V7 = 1000,
       OPLUS_PPS_CURR_UPDATE_V8 = 1500,
       OPLUS_PPS_CURR_UPDATE_V9 = 2000,
       OPLUS_PPS_CURR_UPDATE_V10 = 2500,
       OPLUS_PPS_CURR_UPDATE_V11 = 3000,
};

enum { OPLUS_PPS_CURRENT_LIMIT_1A = 1000,
       OPLUS_PPS_CURRENT_LIMIT_2A = 2000,
       OPLUS_PPS_CURRENT_LIMIT_3A = 3000,
       OPLUS_PPS_CURRENT_LIMIT_4A = 4000,
       OPLUS_PPS_CURRENT_LIMIT_5A = 5000,
       OPLUS_PPS_CURRENT_LIMIT_7A = 7000,
       OPLUS_PPS_CURRENT_LIMIT_8A = 8000,
       OPLUS_PPS_CURRENT_LIMIT_MAX = 12000,
};

enum { PPS_NOT_SUPPORT = 0,
       PPS_CHECKING,
       PPS_CHARGERING,
       PPS_CHARGE_END,
};

enum { PPS_SUPPORT_NOT = 0,
       PPS_SUPPORT_MCU,
       PPS_SUPPORT_2CP,
       PPS_SUPPORT_3CP,
       PPS_SUPPORT_VOOCPHY,
};

enum { OPLUS_PPS_POWER_CLR = 0,
	   OPLUS_PPS_POWER_THIRD = 0x1E,
       OPLUS_PPS_POWER_V1 = 0x7D,
       OPLUS_PPS_POWER_V2 = 0x96,
       OPLUS_PPS_POWER_V3 = 0xF0,
       OPLUS_PPS_POWER_MAX = 0xFFFF,
};

enum {
	PPS_BATT_CURVE_TEMP_RANGE_LITTLE_COLD, /*0 ~ 5*/
	PPS_BATT_CURVE_TEMP_RANGE_COOL, /*5 ~ 12*/
	PPS_BATT_CURVE_TEMP_RANGE_LITTLE_COOL, /*12~20*/
	PPS_BATT_CURVE_TEMP_RANGE_NORMAL_LOW, /*20~35*/
	PPS_BATT_CURVE_TEMP_RANGE_NORMAL_HIGH, /*35~43*/
	PPS_BATT_CURVE_TEMP_RANGE_WARM, /*43~51*/
	PPS_BATT_CURVE_TEMP_RANGE_MAX,
};

enum { PPS_BATT_CURVE_SOC_RANGE_MIN,
       PPS_BATT_CURVE_SOC_RANGE_LOW,
       PPS_BATT_CURVE_SOC_RANGE_MID_LOW,
       PPS_BATT_CURVE_SOC_RANGE_MID,
       PPS_BATT_CURVE_SOC_RANGE_MID_HIGH,
       PPS_BATT_CURVE_SOC_RANGE_HIGH,
       PPS_BATT_CURVE_SOC_RANGE_MAX,
};

enum { PPS_LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL,
       PPS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW,
       PPS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH,
       PPS_LOW_CURR_FULL_CURVE_TEMP_MAX,
};

enum { PPS_REPORT_ERROR_MASTER_I2C = 0,
       PPS_REPORT_ERROR_SLAVE_I2C,
       PPS_REPORT_ERROR_IBUS_LIMIT,
       PPS_REPORT_ERROR_CP_ENABLE,
       PPS_REPORT_ERROR_OVP_OCP,
       PPS_REPORT_ERROR_R_COOLDOWN,
       PPS_REPORT_ERROR_BTB_OVER,
       PPS_REPORT_ERROR_POWER_V1,
       PPS_REPORT_ERROR_POWER_V0,
       PPS_REPORT_ERROR_DR_FAIL,
       PPS_REPORT_ERROR_AUTH_FAIL,
       PPS_REPORT_ERROR_UVDM_POWER,
       PPS_REPORT_ERROR_EXTEND_MAXI,
       PPS_REPORT_ERROR_USBTEMP_OVER,
       PPS_REPORT_ERROR_TFG_OVER,
       PPS_REPORT_ERROR_VBAT_DIFF,
       PPS_REPORT_ERROR_TDIE_OVER,
       PPS_REPORT_ERROR_STARTUP_FAIL,
};

typedef enum {
	PPS_STOP_VOTER_NONE = 0,
	PPS_STOP_VOTER_FULL = (1 << 0),
	PPS_STOP_VOTER_BTB_OVER = (1 << 1),
	PPS_STOP_VOTER_TBATT_OVER = (1 << 2),
	PPS_STOP_VOTER_RESISTENSE_OVER = (1 << 3),
	PPS_STOP_VOTER_IBAT_OVER = (1 << 4),
	PPS_STOP_VOTER_DISCONNECT_OVER = (1 << 5),
	PPS_STOP_VOTER_CHOOSE_CURVE = (1 << 6),
	PPS_STOP_VOTER_TIME_OVER = (1 << 7),
	PPS_STOP_VOTER_PDO_ERROR = (1 << 8),
	PPS_STOP_VOTER_TYPE_ERROR = (1 << 9),
	PPS_STOP_VOTER_MMI_TEST = (1 << 10),
	PPS_STOP_VOTER_USB_TEMP = (1 << 11),
	PPS_STOP_VOTER_TDIE_OVER = (1 << 12),
	PPS_STOP_VOTER_TFG_OVER = (1 << 13),
	PPS_STOP_VOTER_VBATT_DIFF = (1 << 14),
	PPS_STOP_VOTER_CP_ERROR = (1 << 15),
	PPS_STOP_VOTER_STARTUP_FAIL = (1 << 16),
	PPS_STOP_VOTER_OTHER_ABORMAL = (1 << 31),
} PPS_STOP_VOTER;

struct batt_curve {
	unsigned int target_vbus;
	unsigned int target_vbat;
	unsigned int target_ibus;
	bool exit;
	unsigned int target_time;
};

struct batt_curves {
	struct batt_curve batt_curves[BATT_PPS_SYS_MAX];
	int batt_curve_num;
};

struct batt_curves_soc {
	struct batt_curves batt_curves_temp[PPS_BATT_CURVE_TEMP_RANGE_MAX];
};

struct full_curve {
	unsigned int iterm;
	unsigned int vterm;
	bool exit;
};

struct full_curves_temp {
	struct full_curve full_curves[FULL_PPS_SYS_MAX];
	int full_curve_num;
};

struct oplus_pps_r_info {
	int r0;
	int r1;
	int r2;
	int r3;
	int r4;
	int r5;
	int r6;
};

struct oplus_pps_r_limit {
	int limit_exit_mohm;
	int limit_1a_mohm;
	int limit_2a_mohm;
	int limit_3a_mohm;
	int limit_5a_mohm;
};

enum oplus_pps_r_cool_down_ilimit {
	PPS_R_COOL_DOWN_NOLIMIT = 0,
	PPS_R_COOL_DOWN_5A,
	PPS_R_COOL_DOWN_4A,
	PPS_R_COOL_DOWN_3A,
	PPS_R_COOL_DOWN_2A,
	PPS_R_COOL_DOWN_1A,
	PPS_R_COOL_DOWN_EXIT,
};

struct oplus_pps_timer {
	struct timespec pdo_timer;
	struct timespec fastchg_timer;
	struct timespec temp_timer;
	struct timespec ibat_timer;
	struct timespec full_check_timer;
	struct timespec curve_check_timer;
	struct timespec power_timer;
	int batt_curve_time;
	int pps_timeout_time;
	bool set_pdo_flag;
	bool set_temp_flag;
	bool check_ibat_flag;
	bool full_check_flag;
	bool curve_check_flag;
	bool check_power_flag;
	int work_delay;
};

struct pps_protection_counts {
	int cool_fw;
	int sw_full;
	int hw_full;
	int low_curr_full;
	int ibat_low;
	int ibat_high;
	int btb_high;
	int tbatt_over;
	int tfg_over;
	int output_low;
	int ibus_over;
	int tdie_over;
	int tdie_exit;
	int power_over;
};

struct pps_switch_limit {
	int soc_v2;
	int power_v2;
	int soc_v3;
	int power_v3;
};

/*pps charging data*/
struct pps_charging_data {
	int batt_input_current;
	int ap_batt_volt;
	int vbat0;
	int ap_batt_current;
	int ap_batt_soc;
	int ap_batt_temperature;
	int ap_fg_temperature;
	int charger_output_volt;
	int charger_output_current;
	int current_adapter_max;
	int ap_input_current;

	int ap_input_volt;
	int cp_master_ibus;
	int cp_master_vac;
	int cp_master_vout;

	int slave_input_volt;
	int cp_slave_ibus;
	int cp_slave_vac;
	int cp_slave_vout;
	int cp_master_tdie;
	int cp_slave_tdie;

	int slave_b_input_volt;
	int cp_slave_b_ibus;
	int cp_slave_b_vac;
	int cp_slave_b_vout;
	int cp_slave_b_tdie;
};

/*pps current*/
struct pps_current_limits {
	int current_batt_curve;
	int current_batt_temp;
	int current_bcc;
	int current_cool_down;
	int current_normal_cool_down;
	int cp_ibus_down;
	int cp_r_down;
	int cp_tdie_down;
};

struct oplus_pps_limits {
	int default_pps_normal_high_temp;
	int default_pps_little_cool_temp;
	int default_pps_cool_temp;
	int default_pps_little_cold_temp;
	int default_pps_normal_low_temp;
	int pps_warm_allow_vol;
	int pps_warm_allow_soc;

	int pps_batt_over_low_temp;
	int pps_little_cold_temp;
	int pps_cool_temp;
	int pps_little_cool_temp;
	int pps_normal_low_temp;
	int pps_normal_high_temp;
	int pps_batt_over_high_temp;
	int pps_strategy_temp_num;

	int pps_strategy_soc_over_low;
	int pps_strategy_soc_min;
	int pps_strategy_soc_low;
	int pps_strategy_soc_mid_low;
	int pps_strategy_soc_mid;
	int pps_strategy_soc_mid_high;
	int pps_strategy_soc_high;
	int pps_strategy_soc_num;

	int pps_strategy_normal_current;
	int pps_strategy_batt_high_temp0;
	int pps_strategy_batt_high_temp1;
	int pps_strategy_batt_high_temp2;
	int pps_strategy_batt_low_temp2;
	int pps_strategy_batt_low_temp1;
	int pps_strategy_batt_low_temp0;
	int pps_strategy_high_current0;
	int pps_strategy_high_current1;
	int pps_strategy_high_current2;
	int pps_strategy_low_current2;
	int pps_strategy_low_current1;
	int pps_strategy_low_current0;

	int pps_low_curr_full_cool_temp;
	int pps_low_curr_full_little_cool_temp;
	int pps_low_curr_full_normal_low_temp;
	int pps_low_curr_full_normal_high_temp;

	int pps_over_high_or_low_current;
	int pps_strategy_change_count;
	int pps_full_cool_sw_vbat;
	int pps_full_normal_sw_vbat;
	int pps_full_normal_hw_vbat;
	int pps_full_warm_vbat;
	int pps_full_ffc_vbat;
	int pps_full_cool_sw_vbat_third;
	int pps_full_normal_sw_vbat_third;
	int pps_full_normal_hw_vbat_third;
	int pps_timeout_third;
	int pps_timeout_oplus;
	int pps_ibat_over_third;
	int pps_ibat_over_oplus;
};

struct pps_cp_status {
	int master_enable;
	int slave_enable;
	int slave_b_enable;
	int master_error_count;
	int slave_error_count;

	int pmid2vout_enable;
	int master_abnormal;
	int slave_abnormal;
	int slave_b_abnormal;

	int iic_err;
	int iic_err_num;
	int master_enable_err_num;
	int slave_enable_err_num;
	int slave_b_enable_err_num;
};

struct oplus_pps_chip {
	struct device *dev;
	struct oplus_pps_operations *ops;

	struct power_supply *pps_batt_psy;
	struct delayed_work pps_stop_work;
	struct delayed_work update_pps_work;
	struct delayed_work check_vbat_diff_work;
	struct delayed_work ready_force2svooc_work;

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	struct oplus_cfg debug_cfg;
#endif

	/*curve data*/
	struct batt_curves_soc
		batt_curves_third_soc[PPS_BATT_CURVE_SOC_RANGE_MAX];
	struct batt_curves_soc
		batt_curves_oplus_soc[PPS_BATT_CURVE_SOC_RANGE_MAX];
	struct batt_curves batt_curves;
	struct full_curves_temp
		low_curr_full_curves_temp[PPS_LOW_CURR_FULL_CURVE_TEMP_MAX];
	struct oplus_pps_limits limits;
	struct oplus_pps_timer timer;
	struct oplus_pps_r_info r_column[PPS_R_AVG_NUM];
	struct oplus_pps_r_info r_avg;
	struct oplus_pps_r_info r_default;
	struct oplus_pps_r_limit r_limit;

	struct pps_protection_counts count;
	struct pps_switch_limit mode_switch;

	struct pps_charging_data data;
	struct pps_current_limits ilimit;
	struct pps_cp_status cp;

	/*action data*/
	int target_charger_volt;
	int target_charger_current;
	int ask_charger_volt;
	int ask_charger_current;
	int ask_charger_volt_last;
	int ask_charger_current_last;
	int target_charger_current_pre;

	/*curve data*/
	int batt_curve_index;
	int bcc_max_curr;
	int bcc_min_curr;
	int bcc_exit_curr;

	/*pps status*/
	int cp_mode;
	int pps_fastchg_batt_temp_status;
	int pps_temp_cur_range;
	int pps_low_curr_full_temp_status;
	int pps_chging;
	int pps_power;
	int pps_imax;
	int pps_vmax;
	int pps_adapter_type;
	int pps_status;
	int pps_stop_status;
	int pps_support_type;
	int pps_support_third;
	int pps_dummy_started;
	int pps_fastchg_started;
	int pps_power_changed;

	/*quirks*/
	int last_pps_power;
	int pps_keep_last_status;
	int last_plugin_status;
	int pps_startup_retry_times;

	/*other*/
	int rmos_mohm;
	int pps_exit_ms;
	u8 int_column[PPS_DUMP_REG_CNT];
	u8 reg_dump[PPS_DUMP_REG_CNT];
};

struct oplus_pps_operations {
	void (*set_mcu_pps_mode)(bool mode);
	int (*get_mcu_pps_mode)(void);
	int (*get_vbat0_volt)(void);
	int (*check_btb_temp)(void);

	int (*pps_get_authentiate)(void);
	int (*pps_pdo_select)(int vbus_mv, int ibus_ma);
	u32 (*get_pps_status)(void);
	int (*get_pps_max_cur)(int vbus_mv);

	void (*pps_cp_hardware_init)(void);
	void (*pps_cp_reset)(void);
	int (*pps_cp_mode_init)(int mode);
	void (*pps_cp_pmid2vout_enable)(bool enable);

	int (*pps_mos_ctrl)(int on);
	int (*pps_get_cp_master_vbus)(void);
	int (*pps_get_cp_master_ibus)(void);
	int (*pps_get_ucp_flag)(void);
	int (*pps_get_cp_master_vac)(void);
	int (*pps_get_cp_master_vout)(void);
	int (*pps_get_cp_master_tdie)(void);

	int (*pps_get_cp_slave_vbus)(void);
	int (*pps_get_cp_slave_ibus)(void);
	int (*pps_mos_slave_ctrl)(int on);
	int (*pps_get_cp_slave_vac)(void);
	int (*pps_get_cp_slave_vout)(void);
	int (*pps_get_cp_slave_tdie)(void);

	int (*pps_get_cp_slave_b_vbus)(void);
	int (*pps_get_cp_slave_b_ibus)(void);
	int (*pps_mos_slave_b_ctrl)(int on);
	int (*pps_get_cp_slave_b_vac)(void);
	int (*pps_get_cp_slave_b_vout)(void);
	int (*pps_get_cp_slave_b_tdie)(void);
};

struct oplus_pps_chip *oplus_pps_get_pps_chip(void);
int oplus_pps_register_ops(struct oplus_pps_operations *ops);
int oplus_pps_init(struct oplus_chg_chip *chip);
int oplus_pps_get_mcu_pps_mode(void);
void oplus_pps_set_mcu_pps_mode(void);
void oplus_pps_set_mcu_vooc_mode(void);
void oplus_pps_variables_reset(bool in);
int oplus_pps_check_third_pps_support(void);
int oplus_pps_get_chg_status(void);
int oplus_pps_set_chg_status(int status);
int oplus_pps_start(int authen);
void oplus_pps_hardware_init(void);
void oplus_pps_cp_reset(void);
void oplus_pps_stop_disconnect(void);
void oplus_pps_stop_usb_temp(void);
void oplus_pps_set_vbatt_diff(bool diff);
bool oplus_is_pps_charging(void);
void oplus_pps_set_power(int pps_ability, int imax, int vmax);
int oplus_pps_get_power(void);
int oplus_pps_show_power(void);
bool oplus_pps_check_adapter_ability(void);
bool oplus_pps_is_allow_real(void);
void oplus_get_props_from_adsp_by_buffer(void);
int oplus_pps_get_authenticate(void);
int oplus_chg_set_pps_config(int vbus_mv, int ibus_ma);
int oplus_pps_pd_exit(void);
u32 oplus_chg_get_pps_status(void);
int oplus_chg_pps_get_max_cur(int vbus_mv);
int oplus_pps_cp_mode_init(int mode);
/*void oplus_pps_power_switch_check(struct oplus_pps_chip *chip);*/
int oplus_pps_get_ffc_vth(void);
int oplus_pps_get_ffc_started(void);
int oplus_pps_set_ffc_started(bool status);
int oplus_pps_get_pps_mos_started(void);
void oplus_pps_set_pps_mos_enable(bool enable);
void oplus_pps_set_svooc_mos_enable(bool enable);
int oplus_pps_get_pps_disconnect(void);
void oplus_pps_reset_stop_status(void);
int oplus_pps_get_adapter_type(void);
int oplus_pps_get_stop_status(void);
bool oplus_pps_get_pps_dummy_started(void);
void oplus_pps_set_pps_dummy_started(bool enable);
bool oplus_pps_get_pps_fastchg_started(void);
void oplus_pps_print_log(void);
bool oplus_pps_voter_charging_start(void);
int oplus_pps_get_support_type(void);
int oplus_pps_get_master_ibus(void);
int oplus_pps_get_slave_ibus(void);
int oplus_pps_get_slave_b_ibus(void);
void oplus_pps_read_ibus(void);
void oplus_pps_clear_dbg_info(void);
int oplus_pps_print_dbg_info(struct oplus_pps_chip *chip);
void oplus_pps_set_bcc_current(int val);
int oplus_pps_get_bcc_max_curr(void);
int oplus_pps_get_bcc_min_curr(void);
int oplus_pps_get_bcc_exit_curr(void);
bool oplus_pps_bcc_get_temp_range(void);
int oplus_pps_get_adsp_authenticate(void);
int oplus_chg_pps_get_batt_curve_current(void);
int oplus_chg_pps_get_current_cool_down(void);
int oplus_chg_pps_get_current_normal_cool_down(void);
void oplus_pps_notify_master_cp_error(void);
void oplus_pps_notify_slave_cp_error(void);
void oplus_pps_clear_startup_retry(void);
bool oplus_pps_get_last_charging_status(void);
int oplus_keep_connect_check(void);
int oplus_pps_get_last_power(void);
void oplus_pps_clear_last_charging_status(void);
#endif /*_OPLUS_PPS_H_*/
