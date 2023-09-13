/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_WPC_H_
#define _OPLUS_WPC_H_

#include <linux/workqueue.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
#include <linux/wakelock.h>
#endif

#include <linux/timer.h>
#include <linux/slab.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#endif
#include <linux/firmware.h>
#ifndef CONFIG_OPLUS_CHARGER_MTK
#include <linux/soc/qcom/smem.h>
#endif
#include "op_wlchg_v2/oplus_chg_wls.h"

#define SUPPORT_WPC
#define SUPPORT_OPLUS_WPC_VERIFY


#define FASTCHG_CURR_MAX_UA         1500000
#define WPC_20W_DOCK_CURR_MAX_MA         2000

#define WPC_DISCHG_WAIT_READY_EVENT						round_jiffies_relative(msecs_to_jiffies(200))
#define WPC_DISCHG_WAIT_DEVICE_EVENT						round_jiffies_relative(msecs_to_jiffies(90*1000))
#define TEMPERATURE_COLD_LOW_LEVEL							-20
#define TEMPERATURE_COLD_HIGH_LEVEL							0
#define TEMPERATURE_NORMAL_LOW_LEVEL						TEMPERATURE_COLD_HIGH_LEVEL
#define TEMPERATURE_NORMAL_HIGH_LEVEL						440
#define TEMPERATURE_HOT_LOW_LEVEL							TEMPERATURE_NORMAL_HIGH_LEVEL
#define TEMPERATURE_HOT_HIGH_LEVEL							530

#define WPC_CHARGE_FFC_TEMP_MIN								120
#define WPC_CHARGE_FFC_TEMP_MED								350
#define WPC_CHARGE_FFC_TEMP_MAX								420

#define WPC_FASTCHG_TEMP_MIN								0
#define WPC_FASTCHG_TEMP_MAX								450

#define WPC_CHARGE_TEMP_MIN									TEMPERATURE_COLD_LOW_LEVEL
#define WPC_CHARGE_TEMP_MAX									TEMPERATURE_HOT_HIGH_LEVEL

#define TEMPERATURE_STATUS_UNKNOW							0
#define TEMPERATURE_STATUS_FFC_1							1
#define TEMPERATURE_STATUS_FFC_2							2
#define TEMPERATURE_STATUS_OTHER							3

#define TEMPERATURE_STATUS_CHANGE_TIMEOUT					10		//about 10s
#define WPC_CHARGE_CURRENT_LIMIT_300MA						300
#define WPC_CHARGE_CURRENT_LIMIT_200MA						200
#define WPC_CHARGE_CURRENT_ZERO								0		//0mA
#define WPC_CHARGE_CURRENT_200MA							200
#ifdef OPLUS_CUSTOM_OP_DEF
#define WPC_CHARGE_CURRENT_DEFAULT							250		//250mA
#else
#define WPC_CHARGE_CURRENT_DEFAULT							500		//500mA
#endif
#define WPC_CHARGE_CURRENT_BPP_INIT							150
#define WPC_CHARGE_CURRENT_BPP								1000
#define WPC_CHARGE_CURRENT_EPP_INIT							200
#define WPC_CHARGE_CURRENT_EPP								800
#define WPC_CHARGE_CURRENT_EPP_SPEC							300
#define WPC_CHARGE_CURRENT_FASTCHG_INT                      300
#define WPC_CHARGE_CURRENT_FASTCHG_END						700		//300mA
#define WPC_CHARGE_CURRENT_FASTCHG_MID						800		//800mA
#define WPC_CHARGE_CURRENT_FASTCHG							1200	//1500mA
#define WPC_CHARGE_CURRENT_CHANGE_STEP_200MA				200		//200mA
#define WPC_CHARGE_CURRENT_CHANGE_STEP_50MA					50		//50mA
#define WPC_CHARGE_CURRENT_FFC_TO_CV						1000	//1000mA
#define WPC_CHARGE_CURRENT_CHGPUMP_TO_CHARGER				1000

#define WPC_CHARGE_CURRENT_OFFSET							50

#define WPC_CHARGE_VOLTAGE_DEFAULT							5000    //5V

#define WPC_CHARGE_VOLTAGE_FASTCHG_INIT						12000
#define WPC_CHARGE_VOLTAGE_CHGPUMP_TO_CHARGER				12000
#define WPC_CHARGE_VOLTAGE_FTM								16200
#define WPC_CHARGE_VOLTAGE_FASTCHG							12000
#define WPC_CHARGE_VOLTAGE_FASTCHG_MIN						12000
#define WPC_CHARGE_VOLTAGE_FASTCHG_MAX						15000
#define WPC_CHARGE_VOLTAGE_CHANGE_STEP_1V					1000
#define WPC_CHARGE_VOLTAGE_CHANGE_STEP_100MV				100
#define WPC_CHARGE_VOLTAGE_CHANGE_STEP_20MV					20

#define WPC_CHARGE_VOLTAGE_CHGPUMP_MAX						20100
#define WPC_CHARGE_VOLTAGE_CHGPUMP_MIN						5000

#define WPC_TERMINATION_VOLTAGE_FFC							4500
#define WPC_TERMINATION_VOLTAGE_FFC1						4445
#define WPC_TERMINATION_VOLTAGE_FFC2						4445
#define WPC_TERMINATION_VOLTAGE_OTHER						4500
#define WPC_TERMINATION_VOLTAGE_CV  						4435

#define WPC_TERMINATION_CURRENT_FFC1						200
#define WPC_TERMINATION_CURRENT_FFC2						250
#define WPC_TERMINATION_CURRENT_OTHER						0

#define WPC_TERMINATION_CURRENT								100
#define WPC_TERMINATION_VOLTAGE								4500

#define WPC_RECHARGE_VOLTAGE_OFFSET							200
#define WPC_CHARGER_INPUT_CURRENT_LIMIT_INIT				200
#define WPC_CHARGER_INPUT_CURRENT_LIMIT_DEFAULT				500

#define DCP_TERMINATION_CURRENT								600
#define DCP_TERMINATION_VOLTAGE								4380
#define DCP_RECHARGE_VOLTAGE_OFFSET							200
#define DCP_PRECHARGE_CURRENT								300
#define DCP_CHARGER_INPUT_CURRENT_LIMIT_DEFAULT				1000
#define DCP_CHARGE_CURRENT_DEFAULT							1500

#define WPC_BATT_FULL_CNT									4
#define WPC_RECHARGE_CNT									5

#define WPC_INCREASE_CURRENT_DELAY                          2
#define WPC_ADJUST_CV_DELAY									10
#define WPC_ADJUST_CURRENT_DELAY							0
#define WPC_SKEW_DETECT_DELAY								2

#define FAN_PWM_PULSE_IN_SILENT_MODE						60
#define FAN_PWM_PULSE_IN_SILENT_MODE_DEFAULT			52
#define FAN_PWM_PULSE_IN_SILENT_MODE_V01				FAN_PWM_PULSE_IN_SILENT_MODE
#define FAN_PWM_PULSE_IN_SILENT_MODE_V02				FAN_PWM_PULSE_IN_SILENT_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_SILENT_MODE_V03_07				FAN_PWM_PULSE_IN_SILENT_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_SILENT_MODE_V08_15				40

#define FAN_PWM_PULSE_IN_FASTCHG_MODE					93
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_DEFAULT			80
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V01				FAN_PWM_PULSE_IN_FASTCHG_MODE
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V02				FAN_PWM_PULSE_IN_FASTCHG_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V03_07			FAN_PWM_PULSE_IN_FASTCHG_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V08_15			70

#define DOCK_OAWV00										0
#define DOCK_OAWV01										1
#define DOCK_OAWV02										2
#define DOCK_OAWV03										3
#define DOCK_OAWV04										4
#define DOCK_OAWV05										5
#define DOCK_OAWV06										6
#define DOCK_OAWV07										7
#define DOCK_OAWV08										8
#define DOCK_OAWV09										9
#define DOCK_OAWV10										10
#define DOCK_OAWV11										11
#define DOCK_OAWV15										15
#define DOCK_OAWV16										16
#define DOCK_OAWV17										17
#define DOCK_OAWV18										18
#define DOCK_OAWV19										19
#define DOCK_THIRD										0x1f

#define DOCK_VERIFY_UNKOWN								0
#define DOCK_VERIFY_OK										1
#define DOCK_VERIFY_FAIL									2
#define DOCK_VERIFY_RETRY_TIMES							5
#define DOCK_VERIFY_TIMEOUT								40
#define DOCK_SEND_TEMP_SOC_TIMEOUT						8

#define BPP_CURRENT_INCREASE_TIME							20

#define CHARGEPUMP_DETECT_CNT								40

#define ADAPTER_TYPE_UNKNOW									0
#define ADAPTER_TYPE_VOOC									1
#define ADAPTER_TYPE_SVOOC									2
#define ADAPTER_TYPE_USB									3
#define ADAPTER_TYPE_NORMAL_CHARGE							4
#define ADAPTER_TYPE_EPP									5
#define ADAPTER_TYPE_SVOOC_50W								6
#define ADAPTER_TYPE_PD_65W									7
#define ADAPTER_TYPE_THIRD_PARTY 							8

#define WPC_CHARGE_TYPE_DEFAULT								0
#define WPC_CHARGE_TYPE_FAST								1
#define WPC_CHARGE_TYPE_USB									2
#define WPC_CHARGE_TYPE_NORMAL								3
#define WPC_CHARGE_TYPE_EPP									4



#define WLS_AUTH_AES_DATA_LEN 							16
#define WPC_TRX_ERR_REASON_LEN	16
#define WLS_AUTH_AES_RANDOM_LEN 							16
#define WLS_AUTH_AES_ENCODE_LEN 							16

enum {
WPC_CHG_STATUS_DEFAULT,
WPC_CHG_STATUS_READY_FOR_FASTCHG,
WPC_CHG_STATUS_WAIT_IOUT_READY,
WPC_CHG_STATUS_WAITING_FOR_TX_INTO_FASTCHG,
WPC_CHG_STATUS_INCREASE_VOLTAGE_FOR_CHARGEPUMP,
WPC_CHG_STATUS_INCREASE_CURRENT_FOR_CHARGER,
WPC_CHG_STATUS_ADJUST_VOL_AFTER_INC_CURRENT,
WPC_CHG_STATUS_FAST_CHARGING_FROM_CHARGER,
WPC_CHG_STATUS_FAST_CHARGING_FROM_CHGPUMP,
WPC_CHG_STATUS_READY_FOR_EPP,
WPC_CHG_STATUS_CHARGER_FASTCHG_INIT,
WPC_CHG_STATUS_CHAREPUMP_FASTCHG_INIT,
WPC_CHG_STATUS_EPP_WORKING,
WPC_CHG_STATUS_READY_FOR_FTM,
WPC_CHG_STATUS_FTM_WORKING,
WPC_CHG_STATUS_DECREASE_IOUT_TO_200MA,
WPC_CHG_STATUS_DECREASE_VOUT_TO_12V,
WPC_CHG_STATUS_DECREASE_VOUT_FOR_RESTART,
WPC_CHG_STATUS_CHARGEPUMP_INIT,
#ifdef SUPPORT_OPLUS_WPC_VERIFY
WPC_CHG_STATUS_START_VERIFY,
WPC_CHG_STATUS_WAITING_VERIFY,
WPC_CHG_STATUS_VERIFY_OK,
WPC_CHG_STATUS_VERIFY_FAIL,
#endif /*SUPPORT_OPLUS_WPC_VERIFY*/
};

enum {
WPC_TERMINATION_STATUS_UNDEFINE,
WPC_TERMINATION_STATUS_DEFAULT,
WPC_TERMINATION_STATUS_FFC,
WPC_TERMINATION_STATUS_COLD,
WPC_TERMINATION_STATUS_COOL,
WPC_TERMINATION_STATUS_NORMAL,
WPC_TERMINATION_STATUS_WARM,
WPC_TERMINATION_STATUS_HOT,
WPC_TERMINATION_STATUS_ABNORMAL,
};

typedef enum
{
FASTCHARGE_LEVEL_UNKNOW,
FASTCHARGE_LEVEL_1,
FASTCHARGE_LEVEL_2,
FASTCHARGE_LEVEL_3,
FASTCHARGE_LEVEL_4,
FASTCHARGE_LEVEL_5,
FASTCHARGE_LEVEL_6,
FASTCHARGE_LEVEL_7,
FASTCHARGE_LEVEL_8,
FASTCHARGE_LEVEL_9,
FASTCHARGE_LEVEL_10,
FASTCHARGE_LEVEL_NUM,
}E_FASTCHARGE_LEVEL;

typedef enum
{
RX_RUNNING_MODE_UNKNOW,
RX_RUNNING_MODE_EPP,
RX_RUNNING_MODE_EPP_8W,
RX_RUNNING_MODE_EPP_15W,
RX_RUNNING_MODE_BPP,
RX_RUNNING_MODE_OTHERS,
}E_RX_MODE;

typedef enum
{
ADAPTER_POWER_NUKNOW,
ADAPTER_POWER_20W,
ADAPTER_POWER_30W,
ADAPTER_POWER_50W,
ADAPTER_POWER_65W,
ADAPTER_POWER_MAX = ADAPTER_POWER_65W,
}E_ADAPTER_POWER;

typedef enum {
	ADAPTER_POWER_THIRD_NUKNOW,
	ADAPTER_POWER_THIRD_20W,
	ADAPTER_POWER_THIRD_30W,
	ADAPTER_POWER_THIRD_40W,
	ADAPTER_POWER_THIRD_50W,
	ADAPTER_POWER_THIRD_MAX = ADAPTER_POWER_THIRD_50W,
}E_ADAPTER_THIRD_POWER;


typedef enum {
    WPC_DISCHG_STATUS_OFF,
    WPC_DISCHG_STATUS_ON,
    WPC_DISCHG_IC_READY,
    WPC_DISCHG_IC_PING_DEVICE,
    WPC_DISCHG_IC_TRANSFER,
    WPC_DISCHG_IC_ERR_TX_RXAC,
    WPC_DISCHG_IC_ERR_TX_OCP,
    WPC_DISCHG_IC_ERR_TX_OVP,
    WPC_DISCHG_IC_ERR_TX_LVP,
    WPC_DISCHG_IC_ERR_TX_FOD,
    WPC_DISCHG_IC_ERR_TX_OTP,
    WPC_DISCHG_IC_ERR_TX_CEPTIMEOUT,
    WPC_DISCHG_IC_ERR_TX_RXEPT,
    WPC_DISCHG_STATUS_UNKNOW,
}E_WPC_DISCHG_STATUS;

struct wpc_trx_err_reason_table {
	u8 trx_err;
	const char reason[WPC_TRX_ERR_REASON_LEN];
};

typedef enum {
	WPC_CHG_IC_ERR_NULL,
	WPC_CHG_IC_ERR_RX_OCP,
	WPC_CHG_IC_ERR_RX_OVP,
	WPC_CHG_IC_ERR_RX_OTP,
	WPC_CHG_IC_ERR_TX_RXAC,
	WPC_CHG_IC_ERR_TX_OCP,
	WPC_CHG_IC_ERR_TX_OVP,
	WPC_CHG_IC_ERR_TX_LVP,
	WPC_CHG_IC_ERR_TX_FOD,
	WPC_CHG_IC_ERR_TX_OTP,
	WPC_CHG_IC_ERR_TX_CEPTIMEOUT,
	WPC_CHG_IC_ERR_UNKNOW,
}E_WPC_CHG_ERR_TYPE;

enum wireless_mode {
	WIRELESS_MODE_NULL,
	WIRELESS_MODE_TX,
	WIRELESS_MODE_RX,
};
enum FASTCHG_STARTUP_STEP {
	FASTCHG_EN_CHGPUMP1_STEP,
	FASTCHG_EN_PMIC_CHG_STEP,
	FASTCHG_WAIT_PMIC_STABLE_STEP,
	FASTCHG_SET_CHGPUMP2_VOL_STEP,
	FASTCHG_SET_CHGPUMP2_VOL_AGAIN_STEP,
	FASTCHG_EN_CHGPUMP2_STEP,
	FASTCHG_CHECK_CHGPUMP2_STEP,
	FASTCHG_CHECK_CHGPUMP2_AGAIN_STEP,
};
enum WLCHG_TEMP_REGION_TYPE {
	WLCHG_BATT_TEMP_COLD = 0,
	WLCHG_BATT_TEMP_LITTLE_COLD,
	WLCHG_BATT_TEMP_COOL,
	WLCHG_BATT_TEMP_LITTLE_COOL,
	WLCHG_BATT_TEMP_PRE_NORMAL,
	WLCHG_BATT_TEMP_NORMAL,
	WLCHG_BATT_TEMP_WARM,
	WLCHG_BATT_TEMP_HOT,
	WLCHG_TEMP_REGION_MAX,
};

enum NORMAL_TEMP_REGION_TYPE {
	NORMAL_TEMP_REGION_UNKNOW,
	NORMAL_TEMP_REGION1,/*12~37.5C*/
	NORMAL_TEMP_REGION2,/*37.5~40C*/
	NORMAL_TEMP_REGION3,/*40~45C*/
};

struct wpc_chg_param_t{
	int pre_input_ma;
	int iout_ma;
	int bpp_input_ma;
	int epp_input_ma;
	int epp_8w_input_ma;
	int epp_15w_input_ma;
	int epp_temp_warm_input_ma;
	int epp_input_step_ma;
	int vooc_input_ma;
	int vooc_iout_ma;
	int svooc_input_ma;
	int svooc_65w_iout_ma;
	int svooc_50w_iout_ma;
	int svooc_40w_iout_ma;
	int bpp_temp_cold_fastchg_ma;						// -2
	int vooc_temp_little_cold_fastchg_ma;		// 0
	int svooc_temp_little_cold_iout_ma;
	int svooc_temp_little_cold_fastchg_ma;
	int bpp_temp_little_cold_fastchg_ma;
	int epp_temp_little_cold_fastchg_ma;
	int epp_15w_temp_little_cold_fastchg_ma;
	int vooc_temp_cool_fastchg_ma;				// 5
	int svooc_temp_cool_iout_ma;
	int svooc_temp_cool_fastchg_ma;
	int bpp_temp_cool_fastchg_ma;
	int epp_temp_cool_fastchg_ma;
	int epp_15w_temp_cool_fastchg_ma;
	int vooc_temp_little_cool_fastchg_ma;		// 12
	int svooc_temp_little_cool_fastchg_ma;
	int bpp_temp_little_cool_fastchg_ma;
	int epp_temp_little_cool_fastchg_ma;
	int epp_15w_temp_little_cool_fastchg_ma;
	int vooc_temp_normal_fastchg_ma;		// 16
	int svooc_temp_normal_fastchg_ma;
	int bpp_temp_normal_fastchg_ma;
	int epp_temp_normal_fastchg_ma;
	int epp_15w_temp_normal_fastchg_ma;
	int vooc_temp_warm_fastchg_ma;		// 45
	int svooc_temp_warm_fastchg_ma;
	int bpp_temp_warm_fastchg_ma;
	int epp_temp_warm_fastchg_ma;
	int epp_15w_temp_warm_fastchg_ma;
	int svooc_target_ichg_cold_fastchg_ma;       // -2
	int svooc_target_ichg_little_cold_fastchg_ma;      // 0
	int vooc_target_ichg_little_cold_fastchg_ma;
	int bpp_target_ichg_little_cold_fastchg_ma;
	int epp1_target_ichg_little_cold_fastchg_ma;
	int epp2_target_ichg_little_cold_fastchg_ma;
	int epp3_target_ichg_little_cold_fastchg_ma;
	int svooc_target_ichg_cool_fastchg_ma;      //5
	int vooc_target_ichg_cool_fastchg_ma;
	int bpp_target_ichg_cool_fastchg_ma;
	int epp1_target_ichg_cool_fastchg_ma;
	int epp2_target_ichg_cool_fastchg_ma;
	int epp3_target_ichg_cool_fastchg_ma;
	int svooc_target_ichg_normal_region1_fastchg_ma;      //12
	int vooc_target_ichg_normal_region1_fastchg_ma;
	int bpp_target_ichg_normal_region1_fastchg_ma;
	int epp1_target_ichg_normal_region1_fastchg_ma;
	int epp2_target_ichg_normal_region1_fastchg_ma;
	int epp3_target_ichg_normal_region1_fastchg_ma;
	int svooc_target_ichg_normal_region2_fastchg_ma;      //37.5
	int vooc_target_ichg_normal_region2_fastchg_ma;
	int bpp_target_ichg_normal_region2_fastchg_ma;
	int epp1_target_ichg_normal_region2_fastchg_ma;
	int epp2_target_ichg_normal_region2_fastchg_ma;
	int epp3_target_ichg_normal_region2_fastchg_ma;
	int svooc_target_ichg_normal_region3_fastchg_ma;      //40
	int vooc_target_ichg_normal_region3_fastchg_ma;
	int bpp_target_ichg_normal_region3_fastchg_ma;
	int epp_target_ichg_normal_region3_fastchg_ma;
	int svooc_target_ichg_warm_fastchg_ma;      //45
	int curr_cp_to_charger;
	int target_ichg;
	int pre_target_ichg;
	int wireless_vooc_min_temp;
	int wireless_vooc_max_temp;
	int wireless_svooc_min_temp;
	int wireless_svooc_max_temp;
};

typedef struct {
	int effc_key_index;
	u8 aes_random_num[WLS_AUTH_AES_RANDOM_LEN];
	u8 aes_encode_num[WLS_AUTH_AES_ENCODE_LEN];
} wls_third_part_auth_result_v1;

struct wpc_data{
	char charge_status;
	bool aes_verify_data_ok;
	int vendor_id;
	bool tx_extern_cmd_done;
	u32 product_id;
	u32 extern_cmd;
	u8 aes_key_num;
	bool verify_by_aes;
	bool tx_product_id_done;

	wls_third_part_auth_result_v1 aes_verfith_data;
	u8 aes_encrypt_data[WLS_AUTH_AES_RANDOM_LEN];

	E_WPC_DISCHG_STATUS wpc_dischg_status;
	int wpc_chg_err;
	enum FASTCHG_STARTUP_STEP fastchg_startup_step;
	enum WLCHG_TEMP_REGION_TYPE temp_region;
	struct wpc_chg_param_t wpc_chg_param;
	bool tx_online;
	int trx_transfer_start_time;
	int trx_transfer_end_time;
	bool trx_usb_present_once;
	bool tx_present;
	bool charge_online;
	int send_message;
	int send_msg_timer;
	int send_msg_cnt;
	int adapter_type;
	int charge_type;
	int dock_version;
	int charge_voltage;
	int max_charge_voltage;
	int max_charge_current;
	int charge_current_index;
	int charge_current;
	int cc_value_limit;
	int vout;
	int vrect;
	int iout;
	int terminate_voltage;
	int terminate_current;
	int epp_current_limit;
	int epp_current_step;
	bool epp_chg_allow;
	bool check_fw_update;
	bool idt_fw_updating;
	bool				CEP_ready;
	bool fastchg_ing;
	bool epp_working;
	bool vbatt_full;
	bool ftm_mode;
	bool wpc_reach_stable_charge;
	bool wpc_reach_4370mv;
	bool wpc_reach_4500mv;
	bool wpc_ffc_charge;
	bool wpc_skewing_proc;
	E_FASTCHARGE_LEVEL fastcharge_level;
	E_FASTCHARGE_LEVEL skewing_fastcharge_level;
	int iout_stated_current;
	int fastchg_current_limit;
	int fastchg_allow;
	int dock_fan_pwm_pulse;
	int dock_fan_pwm_pulse_fastchg;
	int dock_fan_pwm_pulse_silent;
	int dock_led_pwm_pulse;
	int					BPP_fastchg_current_ma;
	int					BPP_current_limit;
	int					BPP_current_step_cnt;
	bool has_reach_max_temperature;
	char				CEP_value;
	E_RX_MODE rx_runing_mode;
	E_ADAPTER_POWER adapter_power;
	int wireless_power;
	int freq_threshold;
	int freq_check_count;
	bool freq_thr_inc;
	bool is_deviation;
	bool deviation_check_done;
	bool vbat_too_high;
	bool curr_limit_mode;
	bool vol_set_ok;
	bool curr_set_ok;
	bool vol_set_start;
	bool fastchg_mode;
	bool startup_fast_chg;
	bool cep_err_flag;
	bool cep_exit_flag;
	bool cep_check;
	/* Exit fast charge, check ffc condition */
	bool ffc_check;
	/* Record if the last current needs to drop */
	bool curr_need_dec;
	bool vol_not_step;
	bool is_power_changed;
	int max_current;
	int target_curr;
	int target_vol;
	int vol_set;
	bool vout_debug_mode;
	bool iout_debug_mode;
	bool work_silent_mode;
	bool call_mode;
	bool wpc_self_reset;
	bool idt_adc_test_enable;
	bool idt_adc_test_result;
	bool need_doublecheck_to_cp;
	bool doublecheck_ok;
	char FOD_parameter;
	bool skewing_info;
	int cep_info;
	int rx_power;
	int tx_power;
	bool engineering_mode;
	int boot_version;
	int rx_version;
	int tx_version;
	int rx_temperature;
	int break_count;
	int highest_temp;
	int max_iout;
	int min_cool_down;
	int min_skewing_current;
	int wls_auth_fail;
	int normal_temp_region;
	bool chg_in_cv;
	bool mldo_en;
	int charger_dect_count;
	int set_fastchg_count;
	int rerun_wls_aicl_count;
	int target_iin;
	int support_airsvooc;
	int phone_id;
	unsigned long send_soc_temp_start;
#ifdef SUPPORT_OPLUS_WPC_VERIFY
	char random_num[8];
	char noise_num[9];
	char rx_encode_num[8];
	char tx_encode_num[8];
	int dock_verify_retry;
	int dock_verify_status;
	unsigned long dock_verify_start;
#endif /*SUPPORT_OPLUS_WPC_VERIFY*/
};

struct wpc_gpio_ctrl {
	int wrx_en_gpio;
	int wrx_otg_en_gpio;

	struct pinctrl *pinctrl;
	struct pinctrl_state *wrx_en_active;
	struct pinctrl_state *wrx_en_sleep;
	struct pinctrl_state *wrx_en_default;
	struct pinctrl_state *wrx_otg_en_active;
	struct pinctrl_state *wrx_otg_en_sleep;
	struct pinctrl_state *wrx_otg_en_default;
};

#ifdef SUPPORT_OPLUS_WPC_VERIFY
#define SMEM_RESERVED_BOOT_INFO_FOR_APPS	418
#define GAUGE_AUTH_MSG_LEN					20
#define WLS_AUTH_RANDOM_LEN					8
#define WLS_AUTH_ENCODE_LEN					8

struct gauge_auth_result {
	int result;
	unsigned char msg[GAUGE_AUTH_MSG_LEN];
	unsigned char rcv_msg[GAUGE_AUTH_MSG_LEN];
};


struct oplus_chg_auth_result {
	struct gauge_auth_result rst_k0;
	struct gauge_auth_result rst_k1;
	struct wls_auth_result wls_auth_data;
	struct gauge_auth_result rst_k2;
};
#endif /*SUPPORT_OPLUS_WPC_VERIFY*/

struct oplus_wpc_chip {
	struct i2c_client *client;
	struct device *dev;
	struct wpc_data *wpc_chg_data;
	struct wpc_gpio_ctrl wpc_gpios;
	struct oplus_wpc_operations *wpc_ops;

};

struct oplus_wpc_operations {
	int (*wpc_get_online_status)(void);
	int (*wpc_get_voltage_now)(void);
	int (*wpc_get_current_now)(void);
	int (*wpc_get_real_type)(void);
	int (*wpc_get_max_wireless_power)(void);
	bool (*wpc_get_wireless_charge_start)(void);
	bool (*wpc_get_normal_charging)(void);
	bool (*wpc_get_fast_charging)(void);
	bool (*wpc_get_otg_charging)(void);
	bool (*wpc_get_ffc_charging)(void);
	bool (*wpc_get_fw_updating)(void);
	int (*wpc_get_adapter_type)(void);
	int (*wpc_get_skewing_curr)(void);
	void (*wpc_set_vbat_en)(int value);
	void (*wpc_set_booster_en)(int value);
	void (*wpc_set_ext1_wired_otg_en)(int value);
	void (*wpc_set_ext2_wireless_otg_en)(int value);
	void (*wpc_set_rtx_function)(bool enable);
	void (*wpc_set_rtx_function_prepare)(void);
	void (*wpc_dis_wireless_chg)(int value);
	void (*wpc_set_wrx_en)(int value);
	void (*wpc_set_wrx_otg_en)(int value);
	int (*wpc_set_tx_start)(void);
	void (*wpc_set_wls_pg)(int value);
	void (*wpc_dis_tx_power)(void);
	void (*wpc_print_log)(void);
	int (*wpc_get_break_sub_crux_info)(char *crux_info);
	bool (*wpc_get_verity)(void);
/*
	bool nu1619_wireless_charge_start(void);
	bool nu1619_wpc_get_fast_charging(void);
	bool nu1619_wpc_get_ffc_charging(void);
	bool nu1619_wpc_get_fw_updating(void);
	int nu1619_wpc_get_adapter_type(void);
	void nu1619_wpc_print_log(void);
*/
};

int oplus_wpc_get_online_status(void);
int oplus_wpc_get_voltage_now(void);
int oplus_wpc_get_current_now(void);
int oplus_wpc_get_real_type(void);
int oplus_wpc_get_max_wireless_power(void);
void oplus_wpc_init(struct oplus_wpc_chip *chip);

void oplus_wpc_set_wrx_en_value(int value);

void oplus_wpc_set_wrx_otg_en_value(int value);

int oplus_wpc_get_wrx_en_val(void);

int oplus_wpc_get_wrx_otg_en_val(void);

void oplus_wpc_set_otg_en_val(int value);

int oplus_wpc_get_otg_en_val(struct oplus_chg_chip *chip);

int oplus_wpc_get_idt_en_val(void);


void oplus_wpc_set_vbat_en_val(int value);

void oplus_wpc_set_booster_en_val(int value);

void oplus_wpc_set_ext1_wired_otg_en_val(int value);

void oplus_wpc_set_ext2_wireless_otg_en_val(int value);

void oplus_wpc_set_rtx_function_prepare(void);
void oplus_wpc_set_rtx_function(bool enable);
void oplus_wpc_dis_wireless_chg(int value);

bool oplus_wpc_get_normal_charging(void);
bool oplus_wpc_get_fast_charging(void);
bool oplus_wpc_get_otg_charging(void);
bool oplus_wpc_check_chip_is_null(void);

bool oplus_wpc_get_wired_otg_online(void);

bool oplus_wpc_get_wired_chg_present(void);

void oplus_wpc_dcin_irq_enable(bool enable);

bool oplus_wpc_get_wireless_charge_start(void);

bool oplus_wpc_get_fast_charging(void);

bool oplus_wpc_get_ffc_charging(void);

bool oplus_wpc_get_fw_updating(void);

int oplus_wpc_get_adapter_type(void);

int oplus_wpc_get_dock_type(void);

int oplus_wpc_set_tx_start(void);
void oplus_wpc_set_wls_pg_value(int value);
void oplus_wpc_dis_tx_power(void);
void oplus_wpc_print_log(void);
void oplus_get_wpc_chip_handle(struct oplus_wpc_chip **chip);
int oplus_wpc_get_break_sub_crux_info(char *sub_crux_info);
int oplus_wpc_get_skewing_curr(void);
bool oplus_wpc_get_verity(void);

#endif	/* OPLUS_WPC_H */

