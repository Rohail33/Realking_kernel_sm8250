// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_WLS_H__
#define __OPLUS_CHG_WLS_H__

#include <linux/completion.h>
#include <linux/mutex.h>
#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
#include <oplus_cfg.h>
#endif
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#include <linux/oem/oplus_chg_voter.h>
#else
#include "../oplus_chg_core.h"
#include "../oplus_chg_voter.h"
#endif
#include <linux/miscdevice.h>
#include "../oplus_chg_comm.h"
#include "oplus_chg_strategy.h"
#include "../oplus_chg_track.h"

#define JEITA_VOTER		"JEITA_VOTER"
#define STEP_VOTER		"STEP_VOTER"
#define USER_VOTER		"USER_VOTER"
#define DEF_VOTER		"DEF_VOTER"
#define MAX_VOTER		"MAX_VOTER"
#define BASE_MAX_VOTER		"BASE_MAX_VOTER"
#define RX_MAX_VOTER		"RX_MAX_VOTER"
#define EXIT_VOTER		"EXIT_VOTER"
#define FCC_VOTER		"FCC_VOTER"
#define CEP_VOTER		"CEP_VOTER"
#define QUIET_VOTER		"QUIET_VOTER"
#define BATT_VOL_VOTER		"BATT_VOL_VOTER"
#define BATT_CURR_VOTER		"BATT_CURR_VOTER"
#define IOUT_CURR_VOTER		"IOUT_CURR_VOTER"
#define SKIN_VOTER		"SKIN_VOTER"
#define STARTUP_CEP_VOTER	"STARTUP_CEP_VOTER"
#define HW_ERR_VOTER		"HW_ERR_VOTER"
#define CURR_ERR_VOTER		"CURR_ERR_VOTER"
#define OTG_EN_VOTER		"OTG_EN_VOTER"
#define TRX_EN_VOTER		"TRX_EN_VOTER"
#define UPGRADE_FW_VOTER	"UPGRADE_FW_VOTER"
#define DEBUG_VOTER		"DEBUG_VOTER"
#define FFC_VOTER		"FFC_VOTER"
#define USB_VOTER		"USB_VOTER"
#define UPGRADE_VOTER		"UPGRADE_VOTER"
#define CONNECT_VOTER		"CONNECT_VOTER"
#define UOVP_VOTER		"UOVP_VOTER"
#define STOP_VOTER		"STOP_VOTER"
#define FTM_TEST_VOTER		"FTM_TEST_VOTER"
#define CAMERA_VOTER		"CAMERA_VOTER"
#define CALL_VOTER		"CALL_VOTER"
#define COOL_DOWN_VOTER		"COOL_DOWN_VOTER"
#define BCC_CURRENT_VOTER	"BCC_CURRENT_VOTER"
#define RX_IIC_VOTER		"RX_IIC_VOTER"
#define TIMEOUT_VOTER		"TIMEOUT_VOTER"
#define CHG_DONE_VOTER		"CHG_DONE_VOTER"
#define RX_FAST_ERR_VOTER	"RX_FAST_ERR_VOTER"
#define VERITY_VOTER		"VERITY_VOTER"
#define NON_FFC_VOTER		"NON_FFC_VOTER"
#define CV_VOTER		"CV_VOTER"
#define RXAC_VOTER		"RXAC_VOTER"
#define WLS_ONLINE_VOTER	"WLS_ONLINE_VOTER"

#define WLS_SUPPORT_OPLUS_CHG
#define OPLUS_CHG_DEBUG

#define OPLUS_CHG_WLS_AUTH_ENABLE

#define WLS_TRX_STATUS_READY		BIT(0)
#define WLS_TRX_STATUS_DIGITALPING	BIT(1)
#define WLS_TRX_STATUS_ANALOGPING	BIT(2)
#define WLS_TRX_STATUS_TRANSFER		BIT(3)

#define WLS_TRX_ERR_RXAC		BIT(0)
#define WLS_TRX_ERR_OCP			BIT(1)
#define WLS_TRX_ERR_OVP			BIT(2)
#define WLS_TRX_ERR_LVP			BIT(3)
#define WLS_TRX_ERR_FOD			BIT(4)
#define WLS_TRX_ERR_OTP			BIT(5)
#define WLS_TRX_ERR_CEPTIMEOUT		BIT(6)
#define WLS_TRX_ERR_RXEPT		BIT(7)
#define WLS_TRX_ERR_DEBUG_INFO_MASK	0x7f

#define WLS_CMD_INDENTIFY_ADAPTER	0xA1
#define WLS_CMD_INTO_FASTCHAGE		0xA2
#define WLS_CMD_GET_VENDOR_ID		0xA3
#define WLS_CMD_GET_EXTERN_CMD		0xA4
#define WLS_CMD_SET_NORMAL_MODE		0xA5
#define WLS_CMD_SET_QUIET_MODE		0xA6
#define WLS_CMD_SET_LED_BRIGHTNESS	0xAC
#define WLS_CMD_SET_CEP_TIMEOUT		0xAD
#define WLS_CMD_SET_FAN_SPEED		0xAE
#define WLS_CMD_GET_ENCRYPT_DATA1	0xB1
#define WLS_CMD_GET_ENCRYPT_DATA2	0xB2
#define WLS_CMD_GET_ENCRYPT_DATA3	0xB3
#define WLS_CMD_GET_ENCRYPT_DATA4	0xB4
#define WLS_CMD_GET_ENCRYPT_DATA5	0xB5
#define WLS_CMD_GET_ENCRYPT_DATA6	0xB6

#define WLS_CMD_GET_PRODUCT_ID		0xC4
#define WLS_CMD_SEND_BATT_TEMP_SOC	0xC5

#define WLS_CMD_SET_AES_DATA1		0xD1
#define WLS_CMD_SET_AES_DATA2		0xD2
#define WLS_CMD_SET_AES_DATA3		0xD3
#define WLS_CMD_SET_AES_DATA4		0xD4
#define WLS_CMD_SET_AES_DATA5		0xD5
#define WLS_CMD_SET_AES_DATA6		0xD6
#define WLS_CMD_GET_AES_DATA1		0xD7
#define WLS_CMD_GET_AES_DATA2		0xD8
#define WLS_CMD_GET_AES_DATA3		0xD9
#define WLS_CMD_GET_AES_DATA4		0xDA
#define WLS_CMD_GET_AES_DATA5		0xDB
#define WLS_CMD_GET_AES_DATA6		0xDC

#define WLS_CMD_GET_TX_ID		0x05
#define WLS_CMD_GET_TX_PWR		0x4A

#define WLS_MSG_TYPE_EXTENDED_MSG	0x5F
#define WLS_MSG_TYPE_STANDARD_MSG	0x4F

#define WLS_RESPONE_PRODUCT_ID		0x84
#define WLS_RESPONE_BATT_TEMP_SOC	0x85

#define WLS_RESPONE_ADAPTER_TYPE	0xF1
#define WLS_RESPONE_INTO_FASTCHAGE	0xF2
#define WLS_RESPONE_VENDOR_ID		0xF3
#define WLS_RESPONE_EXTERN_CMD		0xF4
#define WLS_RESPONE_INTO_NORMAL_MODE	0xF5
#define WLS_RESPONE_INTO_QUIET_MODE	0xF6
#define WLS_RESPONE_LED_BRIGHTNESS	0xFC
#define WLS_RESPONE_CEP_TIMEOUT		0xFD
#define WLS_RESPONE_FAN_SPEED		0xFE
#define WLS_RESPONE_READY_FOR_EPP	0xFA
#define WLS_RESPONE_WORKING_IN_EPP	0xFB
#define WLS_RESPONE_ENCRYPT_DATA1	0xE1
#define WLS_RESPONE_ENCRYPT_DATA2	0xE2
#define WLS_RESPONE_ENCRYPT_DATA3	0xE3
#define WLS_RESPONE_ENCRYPT_DATA4	0xE4
#define WLS_RESPONE_ENCRYPT_DATA5	0xE5
#define WLS_RESPONE_ENCRYPT_DATA6	0xE6
#define WLS_RESPONE_SET_AES_DATA1	0x91
#define WLS_RESPONE_SET_AES_DATA2	0x92
#define WLS_RESPONE_SET_AES_DATA3	0x93
#define WLS_RESPONE_SET_AES_DATA4	0x94
#define WLS_RESPONE_SET_AES_DATA5	0x95
#define WLS_RESPONE_SET_AES_DATA6	0x96
#define WLS_RESPONE_GET_AES_DATA1	0x97
#define WLS_RESPONE_GET_AES_DATA2	0x98
#define WLS_RESPONE_GET_AES_DATA3	0x99
#define WLS_RESPONE_GET_AES_DATA4	0x9A
#define WLS_RESPONE_GET_AES_DATA5	0x9B
#define WLS_RESPONE_GET_AES_DATA6	0x9C

#define WLS_ADAPTER_TYPE_MASK		0x07
#define WLS_ADAPTER_ID_MASK		0xF8
#define WLS_ADAPTER_POWER_MASK		0xF5
#define WLS_ADAPTER_TYPE_UNKNOWN	0x00
#define WLS_ADAPTER_TYPE_VOOC		0x01
#define WLS_ADAPTER_TYPE_SVOOC		0x02
#define WLS_ADAPTER_TYPE_USB		0x03
#define WLS_ADAPTER_TYPE_NORMAL		0x04
#define WLS_ADAPTER_TYPE_EPP		0x05
#define WLS_ADAPTER_TYPE_PD_65W		0x07

#define WLS_CHARGE_TYPE_DEFAULT		0x00
#define WLS_CHARGE_TYPE_FAST		0x01
#define WLS_CHARGE_TYPE_USB		0x02
#define WLS_CHARGE_TYPE_NORMAL		0x03
#define WLS_CHARGE_TYPE_EPP		0x04

#define WLS_TRX_MODE_VOL_MV		9500
#define WLS_FW_UPGRADE_VOL_MV		5500

#define WLS_CEP_ERR_MAX			3
#define WLS_RX_VOL_MAX_MV		20000
#define WLS_VOL_ADJUST_MAX		150
#define WLS_IBAT_MAX_MA			6000
#define WLS_CURR_ERR_MIN_MA		50

#define WLS_VOUT_DEFAULT_MV		5000
#define WLS_VOUT_EPP_MV			10000
#define WLS_VOUT_EPP_PLUS_MV		WLS_VOUT_EPP_MV
#define WLS_FASTCHG_MODE_VOL_MIN_MV	WLS_VOUT_EPP_MV
#define WLS_VOUT_FTM_MV			17000
#define WLS_VOUT_FASTCHG_INIT_MV	12000

#define WLS_CURR_WAIT_FAST_MA		300
#define WLS_CURR_STOP_CHG_MA		300
#define WLS_CURR_JEITA_CHG_MA		300
#define WLS_FASTCHG_CURR_50W_MAX_MA	2500
#define WLS_FASTCHG_CURR_45W_MAX_MA	2250
#define WLS_FASTCHG_CURR_40W_MAX_MA	2000
#define WLS_FASTCHG_CURR_30W_MAX_MA	1500
#define WLS_FASTCHG_CURR_20W_MAX_MA	1000
#define WLS_FASTCHG_CURR_15W_MAX_MA	800
#define WLS_FASTCHG_CURR_EXIT_MA	500
#define WLS_FASTCHG_CURR_ERR_MA		50
#define WLS_FASTCHG_CURR_MIN_MA		600
#define WLS_FASTCHG_CURR_DISCHG_MAX_MA	1000
#define WLS_FASTCHG_IOUT_CURR_MIN_MA	450

#define WLS_VOOC_PWR_MAX_MW		15000

#define WLS_MAX_STEP_CHG_ENTRIES	8
#define BCC_MAX_STEP_ENTRIES	5

#define WLS_SKIN_TEMP_MAX		500

#define WLS_BCC_STOP_CURR_NUM	5

#define CHARGE_FULL_FAN_THREOD_LO	350
#define CHARGE_FULL_FAN_THREOD_HI	380
#define QUIET_MODE_LED_BRIGHTNESS	3
#define QUIET_MODE_FAN_THR_SPEED	0

#define WLS_ADAPTER_MODEL_0		0x00
#define WLS_ADAPTER_MODEL_1		0x01
#define WLS_ADAPTER_MODEL_2		0x02
#define WLS_ADAPTER_MODEL_7		0x07
#define WLS_ADAPTER_MODEL_15		0x0F
#define WLS_ADAPTER_THIRD_PARTY		0x1F

#define WLS_AUTH_AES_RANDOM_LEN		16
#define WLS_AUTH_RANDOM_LEN		8
#define WLS_AUTH_ENCODE_LEN		8
#define WLS_AUTH_AES_ENCODE_LEN		16
#define WLS_ENCODE_MASK			3

#define WLS_RECEIVE_POWER_DEFAULT	12000
#define WLS_ADAPTER_POWER_45W_MAX_ID	4
#define WLS_RECEIVE_POWER_PD65W		50000

#define WLS_COOL_DOWN_LEVEL_MAX		32
#define WLS_BASE_NUM_MAX		32
#define WLS_TRX_ERR_REASON_LEN	16

#define WLS_QUIET_MODE_UNKOWN	-1
#define WLS_FASTCHG_MODE			0
#define WLS_SILENT_MODE			1
#define WLS_BATTERY_FULL_MODE		2
#define WLS_CALL_MODE				598
#define WLS_EXIT_CALL_MODE		599

#define FAN_PWM_PULSE_IN_SILENT_MODE				60
#define FAN_PWM_PULSE_IN_SILENT_MODE_DEFAULT			52
#define FAN_PWM_PULSE_IN_SILENT_MODE_V01			FAN_PWM_PULSE_IN_SILENT_MODE
#define FAN_PWM_PULSE_IN_SILENT_MODE_V02			FAN_PWM_PULSE_IN_SILENT_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_SILENT_MODE_V03_07			FAN_PWM_PULSE_IN_SILENT_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_SILENT_MODE_V08_15			40

#define FAN_PWM_PULSE_IN_FASTCHG_MODE				93
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_DEFAULT			80
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V01			FAN_PWM_PULSE_IN_FASTCHG_MODE
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V02			FAN_PWM_PULSE_IN_FASTCHG_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V03_07			FAN_PWM_PULSE_IN_FASTCHG_MODE_DEFAULT
#define FAN_PWM_PULSE_IN_FASTCHG_MODE_V08_15			70

#define FAN_PWM_PULSE_IN_SILENT_MODE_THR			(FAN_PWM_PULSE_IN_FASTCHG_MODE_V08_15 - 1)

/*#define WLS_QI_DEBUG*/
/*#define PM_REG_DEBUG*/

struct oplus_chg_wls;

struct oplus_wls_chg_rx {
	struct oplus_chg_wls *wls_dev;
	struct device *dev;
	struct oplus_chg_ic_dev *rx_ic;
	struct delayed_work cep_check_work;
	struct completion cep_ok_ack;
	struct oplus_chg_mod *wls_ocm;
	struct oplus_chg_mod *usb_ocm;
	struct wakeup_source *update_fw_wake_lock;

	bool cep_is_ok;
	bool clean_source;

	int vol_set_mv;
};

struct oplus_wls_chg_fast {
	struct oplus_chg_wls *wls_dev;
	struct device *dev;
	struct oplus_chg_ic_dev *fast_ic;
};

struct oplus_wls_chg_normal {
	struct oplus_chg_wls *wls_dev;
	struct device *dev;
	struct oplus_chg_ic_dev *nor_ic;
	struct delayed_work icl_set_work;
	struct delayed_work fcc_set_work;
	struct oplus_chg_mod *batt_ocm;

	struct mutex icl_lock;
	struct mutex fcc_lock;

	int icl_target_ma;
	int icl_step_ma;
	int icl_set_ma;
	int fcc_target_ma;
	int fcc_step_ma;
	int fcc_set_ma;

	bool clean_source;
};

enum oplus_chg_wls_rx_state {
	OPLUS_CHG_WLS_RX_STATE_DEFAULT,
	OPLUS_CHG_WLS_RX_STATE_BPP,
	OPLUS_CHG_WLS_RX_STATE_EPP,
	OPLUS_CHG_WLS_RX_STATE_EPP_PLUS,
	OPLUS_CHG_WLS_RX_STATE_FAST,
	OPLUS_CHG_WLS_RX_STATE_FFC,
	OPLUS_CHG_WLS_RX_STATE_DONE,
	OPLUS_CHG_WLS_RX_STATE_QUIET,
	OPLUS_CHG_WLS_RX_STATE_STOP,
	OPLUS_CHG_WLS_RX_STATE_DEBUG,
	OPLUS_CHG_WLS_RX_STATE_FTM,
	OPLUS_CHG_WLS_RX_STATE_ERROR,
};

enum oplus_chg_wls_fast_sub_state {
	OPLUS_CHG_WLS_FAST_SUB_STATE_INIT,
	OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_FAST,
	OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_IOUT,
	OPLUS_CHG_WLS_FAST_SUB_STATE_WAIT_VOUT,
	OPLUS_CHG_WLS_FAST_SUB_STATE_CHECK_IOUT,
	OPLUS_CHG_WLS_FAST_SUB_STATE_START,
};

enum oplus_chg_wls_trx_state {
	OPLUS_CHG_WLS_TRX_STATE_DEFAULT,
	OPLUS_CHG_WLS_TRX_STATE_READY,
	OPLUS_CHG_WLS_TRX_STATE_WAIT_PING,
	OPLUS_CHG_WLS_TRX_STATE_TRANSFER,
	OPLUS_CHG_WLS_TRX_STATE_OFF,
};

enum oplus_chg_wls_force_type {
	OPLUS_CHG_WLS_FORCE_TYPE_NOEN,
	OPLUS_CHG_WLS_FORCE_TYPE_BPP,
	OPLUS_CHG_WLS_FORCE_TYPE_EPP,
	OPLUS_CHG_WLS_FORCE_TYPE_EPP_PLUS,
	OPLUS_CHG_WLS_FORCE_TYPE_FAST,
	OPLUS_CHG_WLS_FORCE_TYPE_AUTO,
};

enum oplus_chg_wls_path_ctrl_type {
	OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_ALL,
	OPLUS_CHG_WLS_PATH_CTRL_TYPE_ENABLE_NOR,
	OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_NOR,
	OPLUS_CHG_WLS_PATH_CTRL_TYPE_ENABLE_FAST,
	OPLUS_CHG_WLS_PATH_CTRL_TYPE_DISABLE_FAST,
};

enum oplus_chg_wls_chg_mode {
	OPLUS_WLS_CHG_MODE_BPP,
	OPLUS_WLS_CHG_MODE_EPP,
	OPLUS_WLS_CHG_MODE_EPP_PLUS,
	OPLUS_WLS_CHG_MODE_AIRVOOC,
	OPLUS_WLS_CHG_MODE_AIRSVOOC,
	OPLUS_WLS_CHG_MODE_MAX,
};

enum oplus_chg_wls_batt_cl {
	OPLUS_WLS_CHG_BATT_CL_LOW,
	OPLUS_WLS_CHG_BATT_CL_HIGH,
	OPLUS_WLS_CHG_BATT_CL_MAX,
};

enum wls_status_keep_type {
	WLS_SK_NULL,
	WLS_SK_BY_KERNEL,
	WLS_SK_BY_HAL,
	WLS_SK_WAIT_TIMEOUT,
};

enum {
	WLS_VINDPM_DEFAULT,
	WLS_VINDPM_BPP,
	WLS_VINDPM_EPP,
	WLS_VINDPM_AIRVOOC,
	WLS_VINDPM_AIRSVOOC,
};

struct oplus_chg_rx_msg {
	u8 msg_type;
	u8 data;
	u8 buf[3];
	u8 respone_type;
	bool pending;
	bool long_data;
};

enum wls_dev_cmd_type {
	WLS_DEV_CMD_WLS_AUTH,
	WLS_DEV_CMD_FOD_PARAM,
};
struct wls_dev_cmd {
	unsigned int cmd;
	unsigned int data_size;
	unsigned char data_buf[128];
};

struct wls_auth_result {
	u8 random_num[WLS_AUTH_RANDOM_LEN];
	u8 encode_num[WLS_AUTH_ENCODE_LEN];
};

typedef struct {
	int effc_key_index;
	u8 aes_random_num[WLS_AUTH_AES_RANDOM_LEN];
	u8 aes_encode_num[WLS_AUTH_AES_ENCODE_LEN];
} wls_third_part_auth_result;

struct oplus_chg_wls_status {
	u8 adapter_type;
	u8 adapter_id;
	int vendor_id;
	bool tx_extern_cmd_done;
	u32 product_id;
	u8 aes_key_num;
	bool verify_by_aes;
	u8 adapter_power;
	u8 charge_type;
	bool tx_product_id_done;
	enum oplus_chg_wls_rx_state current_rx_state;
	enum oplus_chg_wls_rx_state next_rx_state;
	enum oplus_chg_wls_rx_state target_rx_state;
	enum oplus_chg_wls_rx_state target_rx_state_backup;
	enum oplus_chg_wls_trx_state trx_state;
	enum oplus_chg_wls_type wls_type;

	int batt_temp_dynamic_thr[BATT_TEMP_INVALID - 1];
	int ffc_temp_dynamic_thr[FFC_TEMP_INVALID - 1];
	enum oplus_chg_temp_region temp_region;
	enum oplus_chg_ffc_temp_region ffc_temp_region;

	int state_sub_step;
	int iout_ma;
	int iout_ma_conunt;
	int vout_mv;
	int vrect_mv;
	int trx_curr_ma;
	int trx_vol_mv;
	int fastchg_target_curr_ma;
	int fastchg_target_vol_mv;
	int fastchg_curr_max_ma;
	int fastchg_ibat_max_ma;
	int fastchg_level;
	// Record the initial temperature when switching to the next gear.
	int fastchg_level_init_temp;
	int tx_pwr_mw;
	int rx_pwr_mw;
	int ploos_mw;
	int epp_plus_skin_step;
	int epp_skin_step;
	int bpp_skin_step;
	int epp_plus_led_on_skin_step;
	int epp_led_on_skin_step;
	int pwr_max_mw;
	int quiet_mode_need;
	int adapter_info_cmd_count;
	int fastchg_retry_count;
	int fastchg_err_count;
	int non_fcc_level;
	int skewing_level;
	int fast_cep_check;
#ifndef CONFIG_OPLUS_CHG_OOS
	int cool_down;
	bool trx_close_delay;
#endif
	int bcc_current;
	int wls_bcc_max_curr;
	int wls_bcc_min_curr;
	int wls_bcc_stop_curr;
	int bcc_curve_idx;
	int bcc_true_idx;
	int bcc_temp_range;

	unsigned long cep_ok_wait_timeout;
	unsigned long fastchg_retry_timer;
	unsigned long fastchg_err_timer;
	bool rx_online;
	bool rx_present;
	bool trx_online;
	bool trx_usb_present_once;
	int trx_transfer_start_time;
	int trx_transfer_end_time;
	bool trx_present;
	bool trx_rxac;
	bool trx_present_keep;
	bool is_op_trx;
	bool epp_working;
	bool epp_5w;
	bool quiet_mode;
	bool switch_quiet_mode;
	bool quiet_mode_init;
	bool cep_timeout_adjusted;
	bool upgrade_fw_pending;
	bool fw_upgrading;
	bool fastchg_started;
	bool fastchg_disable;
	bool fastchg_vol_set_ok;
	bool fastchg_curr_set_ok;
	bool fastchg_curr_need_dec;
	bool normal_chg_disabled;
	bool ffc_check;
	bool wait_cep_stable;
	bool fastchg_restart;
	bool ffc_done;
	bool online_keep;
	bool boot_online_keep; // The driver loading phase of the shutdown charging is set to true
	bool led_on;
	bool rx_adc_test_enable;
	bool rx_adc_test_pass;
	bool fod_parm_for_fastchg;
	bool chg_done;
	bool chg_done_quiet_mode;

	bool aes_verity_done;
	bool verity_pass;
	bool verity_data_ok;
	bool aes_verity_data_ok;
	bool verity_started;
	bool verity_state_keep;
	int verity_count;
	int break_count;
	u8 trx_err;
	u8 encrypt_data[WLS_AUTH_RANDOM_LEN];
	u8 aes_encrypt_data[WLS_AUTH_AES_RANDOM_LEN];
	struct wls_auth_result verfity_data;
	unsigned long verity_wait_timeout;
	wls_third_part_auth_result aes_verfity_data;
};

#define WLS_MAX_STEP_CHG_ENTRIES	8
#define WLS_FOD_PARM_LENGTH		12
struct oplus_chg_wls_range_data {
	int32_t low_threshold;
	int32_t high_threshold;
	uint32_t curr_ma;
	uint32_t vol_max_mv;
	int32_t need_wait;
} __attribute__ ((packed));

struct oplus_chg_wls_bcc_data {
	uint32_t max_batt_volt;
	uint32_t max_curr;
	uint32_t min_curr;
	int32_t exit;
}__attribute__((packed));

enum {
	WLS_FAST_TEMP_0_TO_50,
	WLS_FAST_TEMP_50_TO_120,
	WLS_FAST_TEMP_120_TO_160,
	WLS_FAST_TEMP_160_TO_400,
	WLS_FAST_TEMP_400_TO_440,
	WLS_FAST_TEMP_MAX,
};

enum {
	WLS_FAST_SOC_0_TO_30,
	WLS_FAST_SOC_30_TO_70,
	WLS_FAST_SOC_70_TO_90,
	WLS_FAST_SOC_MAX,
};

enum {
	OPLUS_WLS_SKEWING_EPP,
	OPLUS_WLS_SKEWING_EPP_PLUS,
	OPLUS_WLS_SKEWING_AIRVOOC,
	OPLUS_WLS_SKEWING_MAX,
};

enum {
	WLS_BCC_TEMP_0_TO_50,
	WLS_BCC_TEMP_50_TO_120,
	WLS_BCC_TEMP_120_TO_160,
	WLS_BCC_TEMP_160_TO_400,
	WLS_BCC_TEMP_400_TO_440,
	WLS_BCC_TEMP_MAX,
};

enum {
	WLS_BCC_STOP_0_TO_30,
	WLS_BCC_STOP_30_TO_70,
	WLS_BCC_STOP_70_TO_90,
	WLS_BCC_STOP_MAX,
};

enum {
	WLS_BCC_SOC_0_TO_30,
	WLS_BCC_SOC_30_TO_70,
	WLS_BCC_SOC_70_TO_90,
	WLS_BCC_SOC_MAX,
};

struct oplus_chg_wls_skin_range_data {
	int32_t low_threshold;
	int32_t high_threshold;
	uint32_t curr_ma;
} __attribute__ ((packed));

struct oplus_chg_wls_skewing_data {
	uint32_t curr_ma;
	int32_t fallback_step;
	int32_t switch_to_bpp;
}__attribute__((packed));

struct oplus_chg_wls_fcc_step {
	int max_step;
	struct oplus_chg_wls_range_data fcc_step[WLS_MAX_STEP_CHG_ENTRIES];
	bool allow_fallback[WLS_MAX_STEP_CHG_ENTRIES];
	unsigned long fcc_wait_timeout;
} __attribute__ ((packed));

struct oplus_chg_wls_fcc_steps {
        struct oplus_chg_wls_fcc_step fcc_step[WLS_FAST_TEMP_MAX];
} __attribute__((packed));

struct oplus_chg_wls_bcc_step {
	int max_step;
	struct oplus_chg_wls_bcc_data bcc_step[BCC_MAX_STEP_ENTRIES];
}__attribute__((packed));

struct oplus_chg_wls_bcc_steps {
	struct oplus_chg_wls_bcc_step bcc_step[WLS_BCC_TEMP_MAX];
}__attribute__((packed));

struct oplus_chg_wls_non_ffc_step {
	int max_step;
	struct oplus_chg_wls_range_data *non_ffc_step;
};

struct oplus_chg_wls_cv_step {
	int max_step;
	struct oplus_chg_wls_range_data *cv_step;
};

struct oplus_chg_wls_cool_down_step {
	int max_step;
	int *cool_down_step;
};

struct oplus_chg_wls_skewing_step {
	int max_step;
	struct oplus_chg_wls_skewing_data *skewing_step;
};

struct oplus_chg_wls_skin_step {
	int max_step;
	struct oplus_chg_wls_skin_range_data *skin_step;
};

struct wls_match_q_type {
	u8 id;
	u8 q_value;
};

struct oplus_chg_wls_static_config {
	bool fastchg_fod_enable;
	bool fastchg_12V_fod_enable;
	u8 fastchg_fod_parm[WLS_FOD_PARM_LENGTH];
	u8 fastchg_fod_parm_12V[WLS_FOD_PARM_LENGTH];
	struct wls_match_q_type fastchg_match_q[WLS_BASE_NUM_MAX];
};

struct oplus_chg_wls_dynamic_config {
	int32_t batt_vol_max_mv;
	int32_t iclmax_ma[OPLUS_WLS_CHG_BATT_CL_MAX][OPLUS_WLS_CHG_MODE_MAX][BATT_TEMP_INVALID];
	struct oplus_chg_wls_range_data fcc_step[WLS_MAX_STEP_CHG_ENTRIES];
	struct oplus_chg_wls_fcc_steps fcc_steps[WLS_FAST_SOC_MAX];
	struct oplus_chg_wls_range_data non_ffc_step[OPLUS_WLS_CHG_MODE_MAX][WLS_MAX_STEP_CHG_ENTRIES];
	struct oplus_chg_wls_range_data cv_step[OPLUS_WLS_CHG_MODE_MAX][WLS_MAX_STEP_CHG_ENTRIES];
	int32_t cool_down_step[OPLUS_WLS_CHG_MODE_MAX][WLS_COOL_DOWN_LEVEL_MAX];
	struct oplus_chg_wls_skewing_data skewing_step[OPLUS_WLS_SKEWING_MAX][WLS_MAX_STEP_CHG_ENTRIES];
	struct oplus_chg_wls_skin_range_data epp_plus_skin_step[WLS_MAX_STEP_CHG_ENTRIES];
	struct oplus_chg_wls_skin_range_data epp_skin_step[WLS_MAX_STEP_CHG_ENTRIES];
	struct oplus_chg_wls_skin_range_data bpp_skin_step[WLS_MAX_STEP_CHG_ENTRIES];
	int32_t fastchg_curr_max_ma;
	struct oplus_chg_strategy_data wls_fast_chg_led_off_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_strategy_data wls_fast_chg_led_on_strategy_data[CHG_STRATEGY_DATA_TABLE_MAX];
	struct oplus_chg_wls_skin_range_data epp_plus_led_on_skin_step[WLS_MAX_STEP_CHG_ENTRIES];
	struct oplus_chg_wls_skin_range_data epp_led_on_skin_step[WLS_MAX_STEP_CHG_ENTRIES];
	int32_t wls_fast_chg_call_on_curr_ma;
	int32_t wls_fast_chg_camera_on_curr_ma;
	int32_t bpp_vol_mv;
	int32_t epp_vol_mv;
	int32_t epp_plus_vol_mv;
	int32_t vooc_vol_mv;
	int32_t svooc_vol_mv;
	int32_t fastchg_max_soc;
	int32_t cool_down_12v_thr;
	int32_t verity_curr_max_ma;
	uint32_t bcc_stop_curr_0_to_30[WLS_BCC_STOP_CURR_NUM];
	uint32_t bcc_stop_curr_30_to_70[WLS_BCC_STOP_CURR_NUM];
	uint32_t bcc_stop_curr_70_to_90[WLS_BCC_STOP_CURR_NUM];
} __attribute__ ((packed));

struct oplus_chg_wls_fod_cal_data {
	int fod_bpp;
	int fod_epp;
	int fod_fast;
};

struct wls_base_type {
	u8 id;
	int power_max_mw;
};

struct wls_pwr_table {
	u8 f2_id;
	int r_power;/*watt*/
	int t_power;/*watt*/
};

struct oplus_chg_wls {
	struct device *dev;
	wait_queue_head_t read_wq;
	struct miscdevice misc_dev;
	struct oplus_chg_mod *wls_ocm;
	struct oplus_chg_mod *track_ocm;
	struct power_supply *wls_psy;
	struct power_supply *batt_psy;
	struct oplus_chg_mod *comm_ocm;
	struct notifier_block wls_changed_nb;
	struct notifier_block wls_event_nb;
	struct notifier_block wls_mod_nb;
	struct notifier_block wls_aes_nb;
	struct workqueue_struct	*wls_wq;
	struct delayed_work wls_rx_sm_work;
	struct delayed_work wls_trx_sm_work;
	struct delayed_work wls_connect_work;
	struct delayed_work wls_send_msg_work;
	struct delayed_work wls_upgrade_fw_work;
	struct delayed_work wls_data_update_work;
	struct delayed_work usb_int_work;
	struct delayed_work wls_trx_disconnect_work;
	struct delayed_work usb_connect_work;
	struct delayed_work wls_start_work;
	struct delayed_work wls_break_work;
	struct delayed_work fod_cal_work;
	struct delayed_work rx_restore_work;
	struct delayed_work fast_fault_check_work;
	struct delayed_work rx_iic_restore_work;
	struct delayed_work rx_restart_work;
	struct delayed_work rx_verity_restore_work;
	struct delayed_work online_keep_remove_work;
	struct delayed_work verity_state_remove_work;
	struct delayed_work wls_verity_work;
	struct delayed_work wls_get_third_part_verity_data_work;
#ifndef CONFIG_OPLUS_CHG_OOS
	struct delayed_work wls_clear_trx_work;
#endif
	struct delayed_work wls_skewing_work;
	struct delayed_work wls_bcc_curr_update_work;
	struct delayed_work wls_vout_err_work;
	struct wakeup_source *rx_wake_lock;
	struct wakeup_source *trx_wake_lock;
	struct mutex connect_lock;
	struct mutex read_lock;
	struct mutex cmd_data_lock;
	struct mutex send_msg_lock;
	struct mutex update_data_lock;

	struct votable *fcc_votable;
	struct votable *fastchg_disable_votable;
	struct votable *rx_disable_votable;
	struct votable *wrx_en_votable;
	struct votable *nor_icl_votable;
	struct votable *nor_fcc_votable;
	struct votable *nor_fv_votable;
	struct votable *nor_out_disable_votable;
	struct votable *nor_input_disable_votable;

	struct oplus_wls_chg_rx *wls_rx;
	struct oplus_wls_chg_fast *wls_fast;
	struct oplus_wls_chg_normal *wls_nor;

	struct oplus_chg_wls_status wls_status;
	struct oplus_chg_wls_static_config static_config;
	struct oplus_chg_wls_dynamic_config dynamic_config;
	struct oplus_chg_wls_bcc_step wls_bcc_step;
	struct oplus_chg_wls_bcc_steps bcc_steps[WLS_BCC_SOC_MAX];
	struct oplus_chg_wls_fcc_step wls_fcc_step;
	struct oplus_chg_wls_fcc_steps fcc_steps[WLS_FAST_SOC_MAX];
	struct oplus_chg_wls_fcc_steps fcc_third_part_steps[WLS_FAST_SOC_MAX];
	struct oplus_chg_wls_non_ffc_step non_ffc_step[OPLUS_WLS_CHG_MODE_MAX];
	struct oplus_chg_wls_cv_step cv_step[OPLUS_WLS_CHG_MODE_MAX];
	struct oplus_chg_wls_cool_down_step cool_down_step[OPLUS_WLS_CHG_MODE_MAX];
	struct oplus_chg_wls_skewing_step skewing_step[OPLUS_WLS_SKEWING_MAX];
	struct oplus_chg_wls_skin_step epp_plus_skin_step;
	struct oplus_chg_wls_skin_step epp_skin_step;
	struct oplus_chg_wls_skin_step bpp_skin_step;
	struct oplus_chg_strategy wls_fast_chg_led_off_strategy;
	struct oplus_chg_strategy wls_fast_chg_led_on_strategy;
	struct oplus_chg_wls_skin_step epp_plus_led_on_skin_step;
	struct oplus_chg_wls_skin_step epp_led_on_skin_step;

	enum oplus_chg_wls_type wls_type;
	struct oplus_chg_rx_msg rx_msg;
	struct completion msg_ack;
	struct wls_dev_cmd cmd;

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	struct oplus_cfg debug_cfg;
#endif

	u8 *fw_buf;
	int fw_size;
	bool fw_upgrade_by_buf;

	int wrx_en_gpio;
	int ext_pwr_en_gpio;
	int usb_int_gpio;
	int usb_int_irq;
	struct pinctrl *pinctrl;
	struct pinctrl_state *wrx_en_active;
	struct pinctrl_state *wrx_en_sleep;
	struct pinctrl_state *ext_pwr_en_active;
	struct pinctrl_state *ext_pwr_en_sleep;

	bool support_fastchg;
	bool support_get_tx_pwr;
	bool support_epp_plus;
	bool support_tx_boost;
	bool support_wls_and_tx_boost;
	bool support_wls_chg_bcc;
	bool rx_wake_lock_on;
	bool trx_wake_lock_on;
	bool usb_present;
	bool charge_enable;
	bool batt_charge_enable;
	bool ftm_mode;
	bool debug_mode;
	bool factory_mode;
	bool fod_is_cal;  // fod is calibrated
	bool fod_cal_data_ok;
	bool msg_callback_ok;
	bool cmd_data_ok;

	struct oplus_chg_wls_fod_cal_data cal_data;

	enum oplus_chg_wls_force_type force_type;
	enum oplus_chg_wls_path_ctrl_type path_ctrl_type;

	int icl_max_ma[OPLUS_WLS_CHG_MODE_MAX];

	const char *wls_chg_fw_name;
	int32_t wls_power_mw;
	u32 wls_phone_id;
	unsigned int wls_bcc_fcc_to_icl_factor;
	oplus_chg_track_trigger trx_info_load_trigger;
	struct delayed_work trx_info_load_trigger_work;
	struct mutex track_upload_lock;
	bool rx_err_uploading;
	oplus_chg_track_trigger *rx_err_load_trigger;
	struct delayed_work rx_err_load_trigger_work;
};

#ifdef OPLUS_CHG_DEBUG
ssize_t oplus_chg_wls_upgrade_fw_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);

ssize_t oplus_chg_wls_upgrade_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);
#endif

extern __attribute__ ((weak)) bool oplus_get_wired_chg_present(void);

int oplus_chg_wls_get_max_wireless_power(struct device *dev);
#endif /* __OPLUS_CHG_WLS_H__ */
