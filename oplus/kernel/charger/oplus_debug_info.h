// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_DEBUG_INFO__H
#define __OPLUS_DEBUG_INFO__H

#include "oplus_charger.h"

#define BCC_LINE_LENGTH_MAX 1024

enum oplus_chg_debug_info_notify_type {
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_SOC_JUMP,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_BATT_FCC,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_SLOW,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_CHG_BREAK,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_GAUGE_ERROR,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_WIRELESS,/*add for wireless chg*/
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_RECHG,/*add for rechg counts*/
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_SC8517,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_SC8571,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_BCC_ERR,
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_SC85x7,/*ic end*/
	OPLUS_CHG_DEBUG_NOTIFY_TYPE_MAX,
};

enum oplus_chg_debug_info_notify_flag {
	OPLUS_NOTIFY_BATT_SOC_CAPCITY_LOAD_JUMP,
	OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_LOAD_JUMP,
	OPLUS_NOTIFY_BATT_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_BATT_UI_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_BATT_UI_TO_SOC_CAPCITY_JUMP,
	OPLUS_NOTIFY_CHG_BATT_FULL_NON_100_CAP,//soc end
	OPLUS_NOTIFY_CHG_SLOW_BATT_WARM_TEMP,//slow start
	OPLUS_NOTIFY_CHG_SLOW_BATT_COLD_TEMP,
	OPLUS_NOTIFY_CHG_SLOW_LOW_CHARGE_CURRENT_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_CHG_TYPE_SDP,
	OPLUS_NOTIFY_CHG_SLOW_VOOC_NON_START,
	OPLUS_NOTIFY_CHG_SLOW_VOOC_ADAPTER_NON_MAX_POWER,
	OPLUS_NOTIFY_CHG_SLOW_COOLDOWN_LONG_TIME,
	OPLUS_NOTIFY_CHG_SLOW_SYS_POWER_CONSUME_HIGH,
	OPLUS_NOTIFY_CHG_SLOW_LED_ON_LONG_TIME,//slow end
	OPLUS_NOTIFY_CHG_SLOW_BATT_NON_AUTH,//ic start
	OPLUS_NOTIFY_CHG_SLOW_CHARGER_OV,
	OPLUS_NOTIFY_GAUGE_SEAL_FAIL,
	OPLUS_NOTIFY_GAUGE_UNSEAL_FAIL,
	OPLUS_NOTIFY_CHG_UNSUSPEND,/*add for unsuspend platPmic*/
	OPLUS_NOTIFY_VOOCPHY_ERR,/*add for voocPhy chg*/
	OPLUS_NOTIFY_MCU_UPDATE_FAIL,
	OPLUS_NOTIFY_SC85x7_ERROR, /* ic end */
	OPLUS_NOTIFY_SC8517_ERROR,
	OPLUS_NOTIFY_SC8571_ERROR,
	OPLUS_NOTIFY_CHG_BATT_RECHG,/*add for rechg counts*/
	OPLUS_NOTIFY_BATT_AGING_CAP,
	OPLUS_NOTIFY_CHG_VOOC_BREAK,
	OPLUS_NOTIFY_CHG_GENERAL_BREAK,
	OPLUS_NOTIFY_CHARGER_INFO,
	OPLUS_NOTIFY_FAST_CHARGER_TIME,
	OPLUS_NOTIFY_MCU_ERROR_CODE,
	OPLUS_NOTIFY_WIRELESS_BOOTUP,/*add for wireless chg start*/
	OPLUS_NOTIFY_WIRELESS_START_CHG,
	OPLUS_NOTIFY_WIRELESS_WIRELESS_CHG_BREAK,
	OPLUS_NOTIFY_WIRELESS_WIRELESS_CHG_END,/*add for wireless chg end*/
	OPLUS_NOTIFY_WIRELESS_START_TX,
	OPLUS_NOTIFY_WIRELESS_STOP_TX,
	OPLUS_NOTIFY_BCC_ANODE_POTENTIAL_OVER_TIME,
	OPLUS_NOTIFY_BCC_CURR_ADJUST_ERR,
	OPLUS_NOTIFY_PARALLEL_LIMITIC_ERROR, /*add for parallel chg*/
	OPLUS_NOTIFY_PARALLEL_FULL_NON_100_ERROR, /*add for parallel chg*/
	OPLUS_NOTIFY_FG_CAN_NOT_UPDATE, /*add for soc do not update issue*/
	OPLUS_NOTIFY_CHG_MAX_CNT,
};

struct wireless_pen_status {
	bool support;
	u64 ble_timeout_cnt;
	u64 verify_failed_cnt;
};/*add for wireless pen*/

struct wireless_chg_debug_info {
	int boot_version;
	int rx_version;
	int tx_version;
	int dock_version;
	int adapter_type;
	bool fastchg_ing;
	int vout;
	int iout;
	int rx_temperature;
	int wpc_dischg_status;
	int work_silent_mode;
	int break_count;
	int wpc_chg_err;
	int highest_temp;
	int max_iout;
	int min_cool_down;
	int min_skewing_current;
	int wls_auth_fail;
};/*add for wireless chg*/


struct oplus_chg_debug_info {
	int initialized;

	int pre_soc;
	int cur_soc;
	int pre_ui_soc;
	int cur_ui_soc;
	int soc_load_flag;
	unsigned long sleep_tm_sec;
	int soc_notified_flag;
#define SOC_LOAD_DELAY (60 * 1000)
	struct delayed_work soc_load_dwork;
	int fast_chg_type;
	int real_charger_type;
	int pre_prop_status;
	int chg_start_ui_soc;
	int chg_start_temp;
	int chg_start_time;
	int chg_start_batt_volt;
	int chg_end_soc;
	int chg_end_temp;
	int chg_end_time;
	int chg_end_batt_volt;
	int chg_total_time;
	int total_time;
	int total_time_count;

	int pre_led_state;
	int led_off_start_time;

	int chg_cnt[OPLUS_NOTIFY_CHG_MAX_CNT];

	int notify_type;
	unsigned long long notify_flag;
	struct mutex nflag_lock; 

	struct power_supply *usb_psy;
	struct power_supply *batt_psy;

	int fastchg_stop_cnt;
	int cool_down_by_user;
	int chg_current_by_tbatt;
	int chg_current_by_cooldown;
	int fastchg_input_current;

	struct vooc_charge_strategy *vooc_charge_strategy;
	int vooc_charge_input_current_index;
	int vooc_charge_cur_state_chg_time;

	int vooc_max_input_volt;
	int vooc_max_input_current;
	int fcc_design;
	int chg_full_notified_flag;
	int rechg_counts;/*add for rechg counts*/
	struct workqueue_struct *oplus_chg_debug_wq;

	char flag_reason[32];
	char type_reason[32];
	char sc85x7_error_reason[32];
	char sc8571_error_reason[32];
	struct timespec charge_start_ts;
	int vooc_mcu_error;
	bool report_soh;
	int batt_soh;
	int batt_cc;
	struct wireless_chg_debug_info wireless_info;/*add for wireless chg*/
	struct wireless_pen_status wirelesspen_info;/*add for wireless pen*/
	char bcc_buf[BCC_LINE_LENGTH_MAX];
	int fg_error_count;
	bool fg_error_flag;
	char *fg_info;	/* pointer only use for mtk platform FG */
};

enum GAUGE_SEAL_UNSEAL_ERROR{
	OPLUS_GAUGE_SEAL_FAIL,
	OPLUS_GAUGE_UNSEAL_FAIL,
};

extern int oplus_chg_debug_info_init(struct oplus_chg_chip *chip);
extern int oplus_chg_debug_chg_monitor(struct oplus_chg_chip *chip);
extern int oplus_chg_debug_set_cool_down_by_user(int is_cool_down);
extern int oplus_chg_debug_get_cooldown_current(int chg_current_by_tbatt, int chg_current_by_cooldown);
extern int oplus_chg_debug_set_soc_info(struct oplus_chg_chip *chip);
extern void oplus_chg_gauge_seal_unseal_fail(int type);
extern void oplus_chg_vooc_mcu_error( int error );
extern void oplus_chg_set_fast_chg_type(int value);
int oplus_chg_get_soh_report(void);
int oplus_chg_get_cc_report(void);
extern void oplus_chg_wireless_error(int error,  struct wireless_chg_debug_info *wireless_param);/*add for wireless chg*/
extern int oplus_chg_unsuspend_plat_pmic(struct oplus_chg_chip *chip);/*add for unsuspend platPmic*/
extern int oplus_chg_voocphy_err(void);/*add for voocPhy chg*/
extern int oplus_switching_support_parallel_chg(void);
extern int oplus_switching_get_hw_enable(void);
extern int oplus_switching_get_charge_enable(void);
extern int oplus_switching_get_fastcharge_current(void);
extern int oplus_switching_get_discharge_current(void);
extern int oplus_chg_bcc_err(const char* buf);
extern void oplus_chg_sc8571_error(int report_flag, int *buf, int ret);
#endif
