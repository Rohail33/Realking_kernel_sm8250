#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/iio/consumer.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include "oplus_chg_track.h"
#include "oplus_charger.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_wireless.h"
#include "oplus_chg_comm.h"
#include "oplus_chg_core.h"
#include "oplus_chg_module.h"
#include "oplus_pps.h"
#include "oplus_adapter.h"
#include "voocphy/oplus_voocphy.h"
#include "charger_ic/oplus_switching.h"
#include "oplus_ufcs.h"

#undef pr_fmt
#define pr_fmt(fmt) "OPLUS_CHG[TRACK]: %s[%d]: " fmt, __func__, __LINE__

#define OPLUS_CHG_TRACK_WAIT_TIME_MS		3000
#define OPLUS_CHG_UPDATE_INFO_DELAY_MS		500
#define OPLUS_CHG_TRIGGER_MSG_LEN		(1024 * 2)
#define OPLUS_CHG_TRIGGER_REASON_TAG_LEN	32
#define OPLUS_CHG_TRACK_LOG_TAG			"OplusCharger"
#define OPLUS_CHG_TRACK_EVENT_ID		"charge_monitor"
#define OPLUS_CHG_TRACK_DWORK_RETRY_CNT		3

#define OPLUS_CHG_TRACK_UI_S0C_LOAD_JUMP_THD		5
#define OPLUS_CHG_TRACK_S0C_JUMP_THD			3
#define OPLUS_CHG_TRACK_UI_S0C_JUMP_THD			5
#define OPLUS_CHG_TRACK_UI_SOC_TO_S0C_JUMP_THD		3
#define OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID		0xFF

#define OPLUS_CHG_TRACK_POWER_TYPE_LEN			24
#define OPLUS_CHG_TRACK_BATT_FULL_REASON_LEN		24
#define OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LEN		24
#define OPLUS_CHG_TRACK_COOL_DOWN_STATS_LEN		24
#define OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN		320
#define OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN	24
#define OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN		16
#define OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LENS	160

#define TRACK_WLS_ADAPTER_TYPE_UNKNOWN		0x00
#define TRACK_WLS_ADAPTER_TYPE_VOOC		0x01
#define TRACK_WLS_ADAPTER_TYPE_SVOOC		0x02
#define TRACK_WLS_ADAPTER_TYPE_USB		0x03
#define TRACK_WLS_ADAPTER_TYPE_NORMAL		0x04
#define TRACK_WLS_ADAPTER_TYPE_EPP		0x05
#define TRACK_WLS_ADAPTER_TYPE_SVOOC_50W	0x06
#define TRACK_WLS_ADAPTER_TYPE_PD_65W		0x07

#define TRACK_WLS_DOCK_MODEL_0		0x00
#define TRACK_WLS_DOCK_MODEL_1		0x01
#define TRACK_WLS_DOCK_MODEL_2		0x02
#define TRACK_WLS_DOCK_MODEL_3		0x03
#define TRACK_WLS_DOCK_MODEL_4		0x04
#define TRACK_WLS_DOCK_MODEL_5		0x05
#define TRACK_WLS_DOCK_MODEL_6		0x06
#define TRACK_WLS_DOCK_MODEL_7		0x07
#define TRACK_WLS_DOCK_MODEL_8		0x08
#define TRACK_WLS_DOCK_MODEL_9		0x09
#define TRACK_WLS_DOCK_MODEL_10		0x0a
#define TRACK_WLS_DOCK_MODEL_11		0x0b
#define TRACK_WLS_DOCK_MODEL_12		0x0c
#define TRACK_WLS_DOCK_MODEL_13		0x0d
#define TRACK_WLS_DOCK_MODEL_14		0x0e
#define TRACK_WLS_DOCK_MODEL_15		0x0F
#define TRACK_WLS_DOCK_THIRD_PARTY	0x1F

#define TRACK_POWER_MW(x)	(x)
#define TRACK_INPUT_VOL_MV(x)	(x)
#define TRACK_INPUT_CURR_MA(x)	(x)

#define TRACK_CYCLE_RECORDIING_TIME_2MIN	120
#define TRACK_CYCLE_RECORDIING_TIME_90S		90
#define TRACK_UTC_BASE_TIME			1900

#define TRACK_TIME_1MIN_THD			60
#define TRACK_TIME_30MIN_THD			1800
#define TRACK_TIME_1HOU_THD			3600
#define TRACK_TIME_7S_JIFF_THD			(7 * 1000)
#define TRACK_TIME_500MS_JIFF_THD		(5 * 100)
#define TRACK_TIME_1000MS_JIFF_THD		(1 * 1000)
#define TRACK_TIME_5MIN_JIFF_THD		(5 * 60 * 1000)
#define TRACK_TIME_10MIN_JIFF_THD		(10 * 60 * 1000)
#define TRACK_TIME_SCHEDULE_UI_SOC_LOAD_JUMP	90000
#define TRACK_THRAD_PERIOD_TIME_S		5
#define TRACK_NO_CHRGING_TIME_PCT		70
#define TRACK_COOLDOWN_CHRGING_TIME_PCT		70
#define TRACK_WLS_SKEWING_CHRGING_TIME_PCT	2

#define TRACK_PERIOD_CHG_CAP_MAX_SOC		100
#define TRACK_PERIOD_CHG_CAP_INIT		(-101)
#define TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT	(-10000)
#define TRACK_T_THD_2000_MS			2000
#define TRACK_T_THD_1000_MS			1000
#define TRACK_T_THD_500_MS			500
#define TRACK_T_THD_6000_MS			6000

#define TRACK_LOCAL_T_NS_TO_MS_THD		1000000
#define TRACK_LOCAL_T_NS_TO_S_THD		1000000000

#define TRACK_CHG_GET_THTS_TIME_TYPE		0
#define TRACK_CHG_GET_LAST_TIME_TYPE		1
#define TRACK_LED_MONITOR_SOC_POINT		98
#define TRACK_CHG_VOOC_BATT_VOL_DIFF_MV		100

#define TRACK_REF_SOC_50		50
#define TRACK_REF_SOC_75		75
#define TRACK_REF_SOC_90		90
#define TRACK_REF_VOL_5000MV		5000
#define TRACK_REF_VOL_10000MV		10000
#define TRACK_REF_TIME_6S		6
#define TRACK_REF_TIME_8S		8
#define TRACK_REF_TIME_10S		10

#define TRACK_ACTION_OTG		"otg"
#define TRACK_ACTION_WRX		"wrx"
#define TRACK_ACTION_WTX		"wtx"
#define TRACK_ACTION_WIRED		"wired"
#define TRACK_ACTION_OTHER		"other"
#define TRACK_ACTION_WRX_OTG		"wrx_otg"
#define TRACK_ACTION_WTX_OTG		"wtx_otg"
#define TRACK_ACTION_WTX_WIRED		"wtx_wired"
#define TRACK_ACTION_LENS		16

#define TRACK_ONLINE_CHECK_COUNT_THD	40
#define TRACK_ONLINE_CHECK_T_MS		25

#define TRACK_HIDL_DATA_LEN			512
#define TRACK_HIDL_BCC_INFO_COUNT		8
#define TRACK_HIDL_BCC_CMD_LEN			48
#define TRACK_HIDL_BCC_ERR_LEN			256
#define TRACK_HIDL_BCC_ERR_REASON_LEN		24
#define TRACK_HIDL_WLS_THIRD_ERR_LEN		256
#define TRACK_HIDL_WLS_THIRD_ERR_REASON_LEN	24
#define TRACK_HIDL_BCC_INFO_LEN			(TRACK_HIDL_BCC_INFO_COUNT * TRACK_HIDL_BCC_CMD_LEN)
#define TRACK_HIDL_BMS_INFO_LEN			256
#define TRACK_HIDL_UISOH_INFO_LEN		512
#define TRACK_HIDL_HYPER_INFO_LEN		256
#define TRACK_SOFT_ABNORMAL_UPLOAD_PERIOD	(24 * 3600)
#define TRACK_SOFT_UPLOAD_COUNT_MAX		10
#define TRACK_SOFT_SOH_UPLOAD_COUNT_MAX		50

enum adsp_track_debug_type {
	ADSP_TRACK_DEBUG_DEFAULT,
	ADSP_TRACK_DEBUG_GAGUE_I2C_ERR,
	ADSP_TRACK_DEBUG_GAGUE_SEAL_ERR,
	ADSP_TRACK_DEBUG_GAGUE_UNSEAL_ERR,
	ADSP_TRACK_DEBUG_DCHG_ERR,
	ADSP_TRACK_DEBUG_MAX_ERR = ADSP_TRACK_DEBUG_DCHG_ERR,
};

enum adsp_track_info_type {
	ADSP_TRACK_NOTIFY_TYPE_DEFAULT,
	ADSP_TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL,
	ADSP_TRACK_NOTIFY_TYPE_MAX,
};

enum adsp_track_info_flag {
	ADSP_TRACK_NOTIFY_FLAG_DEFAULT,
	ADSP_TRACK_NOTIFY_FLAG_GAGUE_ABNORMAL,
	ADSP_TRACK_NOTIFY_FLAG_DCHG_ABNORMAL,
	ADSP_TRACK_NOTIFY_FLAG_MAX_CNT,
};

enum oplus_chg_track_voocphy_type {
	TRACK_NO_VOOCPHY = 0,
	TRACK_ADSP_VOOCPHY,
	TRACK_AP_SINGLE_CP_VOOCPHY,
	TRACK_AP_DUAL_CP_VOOCPHY,
	TRACK_MCU_VOOCPHY,
};

enum oplus_chg_track_fastchg_status {
	TRACK_FASTCHG_STATUS_UNKOWN = 0,
	TRACK_FASTCHG_STATUS_START,
	TRACK_FASTCHG_STATUS_NORMAL,
	TRACK_FASTCHG_STATUS_WARM,
	TRACK_FASTCHG_STATUS_DUMMY,
};

enum oplus_chg_track_power_type {
	TRACK_CHG_TYPE_UNKNOW,
	TRACK_CHG_TYPE_WIRE,
	TRACK_CHG_TYPE_WIRELESS,
};

enum oplus_chg_track_soc_section {
	TRACK_SOC_SECTION_DEFAULT,
	TRACK_SOC_SECTION_LOW,
	TRACK_SOC_SECTION_MEDIUM,
	TRACK_SOC_SECTION_HIGH,
	TRACK_SOC_SECTION_OVER,
};

struct oplus_chg_track_type_mapping {
	enum adsp_track_info_type adsp_type;
	enum oplus_chg_track_info_type ap_type;
};

struct oplus_chg_track_flag_mapping {
	enum adsp_track_info_flag adsp_flag;
	enum oplus_chg_track_info_flag ap_flag;
};

enum oplus_chg_track_hidl_type {
	TRACK_HIDL_DEFAULT,
	TRACK_HIDL_BCC_INFO,
	TRACK_HIDL_BCC_ERR,
	TRACK_HIDL_BMS_INFO,
	TRACK_HIDL_HYPER_INFO,
	TRACK_HIDL_WLS_THIRD_ERR,
	TRACK_HIDL_UISOH_INFO,
};

struct oplus_chg_track_vooc_type {
	int chg_type;
	int vol;
	int cur;
	char name[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
};

struct oplus_chg_track_type {
	int type;
	int power;
	char name[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
};

struct oplus_chg_track_wired_type {
	int power;
	int adapter_id;
	char adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
};

struct oplus_chg_track_wls_type {
	int power;
	char adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
	char dock_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
};

struct oplus_chg_track_power {
	int power_type;
	char power_mode[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
	struct oplus_chg_track_wired_type wired_info;
	struct oplus_chg_track_wls_type wls_info;
};

struct oplus_chg_track_batt_full_reason {
	int notify_flag;
	char reason[OPLUS_CHG_TRACK_BATT_FULL_REASON_LEN];
};

struct oplus_chg_track_chg_abnormal_reason {
	int notify_code;
	char reason[OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LEN];
	bool happened;
};

struct oplus_chg_track_cool_down_stats {
	int level;
	int time;
	char level_name[OPLUS_CHG_TRACK_COOL_DOWN_STATS_LEN];
};

struct oplus_chg_track_cfg {
	int track_ver;
	int fast_chg_break_t_thd;
	int ufcs_chg_break_t_thd;
	int general_chg_break_t_thd;
	int wls_chg_break_t_thd;
	int voocphy_type;
	int wired_fast_chg_scheme;
	int wls_fast_chg_scheme;
	int wls_epp_chg_scheme;
	int wls_bpp_chg_scheme;
	int wls_max_power;
	int wired_max_power;
};

struct oplus_chg_track_fastchg_break {
	int code;
	char name[OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN];
};

struct oplus_chg_track_voocphy_info {
	int voocphy_type;
	char name[OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN];
};

struct oplus_chg_track_i2c_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_wls_trx_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_ufcs_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_gpio_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_cp_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_pmic_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_gague_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_track_cooldown_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_SOFT_ERR_NAME_LEN];
};

struct oplus_chg_track_pen_match_err_reason {
	int err_type;
	char err_name[OPLUS_CHG_TRACK_SOFT_ERR_NAME_LEN];
};

struct oplus_chg_track_speed_ref {
	int ref_soc;
	int ref_power;
	int ref_curr;
};

struct oplus_chg_track_hidl_cmd {
	u32 cmd;
	u32 data_size;
	u8 data_buf[TRACK_HIDL_DATA_LEN];
};

struct oplus_chg_track_hidl_bcc_info_cmd {
	u32 err_code;
	u8 data_buf[TRACK_HIDL_BCC_CMD_LEN];
};

struct oplus_chg_track_hidl_bcc_info {
	u8 count;
	u32 err_code;
	u8 data_buf[TRACK_HIDL_BCC_INFO_LEN];
};

struct oplus_chg_track_hidl_bcc_err_cmd {
	u8 err_reason[TRACK_HIDL_BCC_ERR_REASON_LEN];
	u8 data_buf[TRACK_HIDL_BCC_ERR_LEN];
};

struct oplus_chg_track_hidl_wls_third_err_cmd {
	u8 err_sence[TRACK_HIDL_WLS_THIRD_ERR_REASON_LEN];
	u8 err_reason[TRACK_HIDL_WLS_THIRD_ERR_REASON_LEN];
	u8 data_buf[TRACK_HIDL_WLS_THIRD_ERR_LEN];
};

struct oplus_chg_track_hidl_bcc_err {
	struct mutex track_bcc_err_lock;
	bool bcc_err_uploading;
	oplus_chg_track_trigger *bcc_err_load_trigger;
	struct delayed_work bcc_err_load_trigger_work;
	struct oplus_chg_track_hidl_bcc_err_cmd bcc_err;
};

struct oplus_chg_track_hidl_uisoh_info_cmd {
	u8 data_buf[TRACK_HIDL_UISOH_INFO_LEN];
};

struct oplus_chg_track_hidl_uisoh_info{
	struct mutex track_uisoh_info_lock;
	bool uisoh_info_uploading;
	oplus_chg_track_trigger *uisoh_info_load_trigger;
	struct delayed_work uisoh_info_load_trigger_work;
	struct oplus_chg_track_hidl_uisoh_info_cmd uisoh_info;
};

struct oplus_chg_track_hidl_hyper_info {
	char hyper_en;
};

struct oplus_chg_track_hidl_wls_third_err {
	struct mutex track_wls_third_err_lock;
	bool wls_third_err_uploading;
	oplus_chg_track_trigger *wls_third_err_load_trigger;
	struct delayed_work wls_third_err_load_trigger_work;
	struct oplus_chg_track_hidl_wls_third_err_cmd wls_third_err;
};

struct oplus_chg_track_status {
	int curr_soc;
	int pre_soc;
	int curr_uisoc;
	int pre_uisoc;
	int pre_vbatt;
	int pre_time_utc;
	bool soc_jumped;
	bool uisoc_jumped;
	bool uisoc_to_soc_jumped;
	bool uisoc_load_jumped;

	u8 debug_soc;
	u8 debug_uisoc;
	u8 debug_normal_charging_state;
	u8 debug_fast_prop_status;
	u8 debug_normal_prop_status;
	u8 debug_no_charging;
	u8 debug_slow_charging_force;
	u32 debug_chg_notify_flag;
	u32 debug_chg_notify_code;
	u8 debug_slow_charging_reason;

	struct oplus_chg_track_power power_info;
	int fast_chg_type;
	int pre_fastchg_type;
	int pre_chg_subtype;
	int real_chg_type;
	int charger_type_backup;

	int chg_start_rm;
	int chg_start_soc;
	int chg_end_soc;
	int chg_start_temp;
	int batt_start_temp;
	int batt_max_temp;
	int batt_max_vol;
	int batt_max_curr;
	int chg_start_time;
	int chg_end_time;
	int ffc_start_time;
	int cv_start_time;
	int chg_ffc_time;
	int ffc_start_main_soc;
	int ffc_start_sub_soc;
	int ffc_end_main_soc;
	int ffc_end_sub_soc;
	int chg_cv_time;
	int chg_fast_full_time;
	int chg_normal_full_time;
	int chg_report_full_time;
	bool has_judge_speed;
	bool aging_ffc_trig;
	int aging_ffc1_judge_vol;
	int aging_ffc2_judge_vol;
	int aging_ffc1_start_time;
	int aging_ffc1_to_full_time;
	int chg_plugin_utc_t;
	int chg_plugout_utc_t;
	int rechg_counts;
	int in_rechging;
	struct rtc_time chg_plugin_rtc_t;
	struct rtc_time chg_plugout_rtc_t;

	int chg_five_mins_cap;
	int chg_ten_mins_cap;
	int chg_average_speed;
	char batt_full_reason[OPLUS_CHG_TRACK_BATT_FULL_REASON_LEN];

	int chg_max_temp;
	int chg_no_charging_cnt;
	int continue_ledon_time;
	int continue_ledoff_time;
	int ledon_ave_speed;
	int ledoff_ave_speed;
	int ledon_rm;
	int ledoff_rm;
	int ledon_time;
	int ledoff_time;
	int led_on;
	int led_change_t;
	int prop_status;
	int led_change_rm;
	bool led_onoff_power_cal;
	int chg_total_cnt;
	unsigned long long chg_attach_time_ms;
	unsigned long long chg_detach_time_ms;
	unsigned long long wls_attach_time_ms;
	unsigned long long wls_detach_time_ms;
	struct oplus_chg_track_fastchg_break fastchg_break_info;

	bool wired_online;
	bool wired_online_check_stop;
	int wired_online_check_count;
	bool mmi_chg;
	bool once_mmi_chg;
	bool fastchg_to_normal;
	bool chg_speed_is_slow;
	bool tbatt_warm_once;
	bool tbatt_cold_once;
	int cool_down_effect_cnt;
	int cool_down_status;
	int cool_down_status_change_t;
	int soc_sect_status;
	int soc_low_sect_incr_rm;
	int soc_low_sect_cont_time;
	int soc_medium_sect_incr_rm;
	int soc_medium_sect_cont_time;
	int soc_high_sect_incr_rm;
	int soc_high_sect_cont_time;
	struct oplus_chg_track_speed_ref *wired_speed_ref;
	struct oplus_chg_track_speed_ref *wls_airvooc_speed_ref;
	struct oplus_chg_track_speed_ref *wls_epp_speed_ref;
	struct oplus_chg_track_speed_ref *wls_bpp_speed_ref;

	bool wls_online_keep;
	bool wls_need_upload;
	bool wired_need_upload;
	int wls_prop_status;
	int wls_skew_effect_cnt;
	bool chg_verity;
	char chg_abnormal_reason[OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LENS];
	struct oplus_chg_track_hidl_bcc_info *bcc_info;
	struct oplus_chg_track_hidl_bcc_err bcc_err;
	struct oplus_chg_track_hidl_uisoh_info uisoh_info_s;
	u8 bms_info[TRACK_HIDL_BMS_INFO_LEN];
	u8 hyper_info[TRACK_HIDL_HYPER_INFO_LEN];

	char hyper_en;
	int hyper_stop_soc;
	int hyper_stop_temp;
	int hyper_last_time;
	int hyper_est_save_time;
	int hyper_ave_speed;

	struct oplus_chg_track_hidl_wls_third_err wls_third_err;
	int once_chg_cycle_status;
};

struct oplus_chg_track {
	struct device *dev;
	struct oplus_chg_mod *track_ocm;
	struct oplus_chg_mod *wls_ocm;
	struct oplus_chg_mod *comm_ocm;
	struct notifier_block track_event_nb;
	struct task_struct *track_upload_kthread;

	bool trigger_data_ok;
	struct mutex upload_lock;
	struct mutex trigger_data_lock;
	struct mutex trigger_ack_lock;
	oplus_chg_track_trigger trigger_data;
	struct completion trigger_ack;
	wait_queue_head_t upload_wq;

	struct workqueue_struct *trigger_upload_wq;
	struct delayed_work upload_info_dwork;
	struct mutex dcs_info_lock;
	int dwork_retry_cnt;

	struct kfifo adsp_fifo;
	struct task_struct *adsp_track_upload_kthread;
	struct mutex adsp_upload_lock;
	wait_queue_head_t adsp_upload_wq;

	struct oplus_chg_track_status track_status;
	struct oplus_chg_track_cfg track_cfg;

	oplus_chg_track_trigger uisoc_load_trigger;
	oplus_chg_track_trigger soc_trigger;
	oplus_chg_track_trigger uisoc_trigger;
	oplus_chg_track_trigger uisoc_to_soc_trigger;
	oplus_chg_track_trigger charger_info_trigger;
	oplus_chg_track_trigger no_charging_trigger;
	oplus_chg_track_trigger slow_charging_trigger;
	oplus_chg_track_trigger charging_break_trigger;
	oplus_chg_track_trigger wls_charging_break_trigger;
	struct delayed_work uisoc_load_trigger_work;
	struct delayed_work soc_trigger_work;
	struct delayed_work uisoc_trigger_work;
	struct delayed_work uisoc_to_soc_trigger_work;
	struct delayed_work charger_info_trigger_work;
	struct delayed_work cal_chg_five_mins_capacity_work;
	struct delayed_work cal_chg_ten_mins_capacity_work;
	struct delayed_work no_charging_trigger_work;
	struct delayed_work slow_charging_trigger_work;
	struct delayed_work charging_break_trigger_work;
	struct delayed_work wls_charging_break_trigger_work;
	struct delayed_work check_wired_online_work;

	char voocphy_name[OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN];

	char wired_break_crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	char wls_break_crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	char chg_power_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];

	struct mutex access_lock;
	struct mutex online_hold_lock;
};

struct type_reason_table {
	int type_reason;
	char type_reason_tag[OPLUS_CHG_TRIGGER_REASON_TAG_LEN];
};

struct flag_reason_table {
	int flag_reason;
	char flag_reason_tag[OPLUS_CHG_TRIGGER_REASON_TAG_LEN];
};

static struct oplus_chg_track *g_track_chip;
static struct dentry *track_debugfs_root;
static DEFINE_MUTEX(debugfs_root_mutex);
static DEFINE_SPINLOCK(adsp_fifo_lock);

static int oplus_chg_track_get_charger_type(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status,
					    int type);
static int oplus_chg_track_obtain_wls_break_sub_crux_info(struct oplus_chg_track *track_chip, char *crux_info);
static int oplus_chg_track_get_local_time_s(void);

static struct oplus_chg_track_type base_type_table[] = {
	{ POWER_SUPPLY_TYPE_UNKNOWN, TRACK_POWER_MW(2500), "unknow" },
	{ POWER_SUPPLY_TYPE_USB, TRACK_POWER_MW(2500), "sdp" },
	{ POWER_SUPPLY_TYPE_USB_DCP, TRACK_POWER_MW(10000), "dcp" },
	{ POWER_SUPPLY_TYPE_USB_CDP, TRACK_POWER_MW(7500), "cdp" },
};

static struct oplus_chg_track_type enhance_type_table[] = {
	{ CHARGER_SUBTYPE_PD, TRACK_POWER_MW(18000), "pd" },
	{ CHARGER_SUBTYPE_QC, TRACK_POWER_MW(18000), "qc" },
	{ CHARGER_SUBTYPE_PPS, TRACK_POWER_MW(30000), "pps" },
	{ CHARGER_SUBTYPE_UFCS, TRACK_POWER_MW(100000), "ufcs" },
};

static struct oplus_chg_track_type new_wls_adapter_type_table[] = {
	{ OPLUS_CHG_WLS_UNKNOWN, TRACK_POWER_MW(5000), "unknow" },
	{ OPLUS_CHG_WLS_VOOC, TRACK_POWER_MW(20000), "airvooc" },
	{ OPLUS_CHG_WLS_BPP, TRACK_POWER_MW(5000), "bpp" },
	{ OPLUS_CHG_WLS_EPP, TRACK_POWER_MW(10000), "epp" },
	{ OPLUS_CHG_WLS_EPP_PLUS, TRACK_POWER_MW(10000), "epp" },
	{ OPLUS_CHG_WLS_SVOOC, TRACK_POWER_MW(50000), "airsvooc" },
	{ OPLUS_CHG_WLS_PD_65W, TRACK_POWER_MW(65000), "airsvooc" },
};

static struct oplus_chg_track_type old_wls_adapter_type_table[] = {
	{ TRACK_WLS_ADAPTER_TYPE_UNKNOWN, TRACK_POWER_MW(5000), "unknow" },
	{ TRACK_WLS_ADAPTER_TYPE_VOOC, TRACK_POWER_MW(20000), "airvooc" },
	{ TRACK_WLS_ADAPTER_TYPE_SVOOC, TRACK_POWER_MW(50000), "airsvooc" },
	{ TRACK_WLS_ADAPTER_TYPE_USB, TRACK_POWER_MW(5000), "bpp" },
	{ TRACK_WLS_ADAPTER_TYPE_NORMAL, TRACK_POWER_MW(5000), "bpp" },
	{ TRACK_WLS_ADAPTER_TYPE_EPP, TRACK_POWER_MW(10000), "epp" },
	{ TRACK_WLS_ADAPTER_TYPE_SVOOC_50W, TRACK_POWER_MW(50000), "airsvooc" },
	{ TRACK_WLS_ADAPTER_TYPE_PD_65W, TRACK_POWER_MW(65000), "airsvooc" },
};

static struct oplus_chg_track_type wls_dock_type_table[] = {
	{ TRACK_WLS_DOCK_MODEL_0, TRACK_POWER_MW(30000), "model_0" },
	{ TRACK_WLS_DOCK_MODEL_1, TRACK_POWER_MW(40000), "model_1" },
	{ TRACK_WLS_DOCK_MODEL_2, TRACK_POWER_MW(50000), "model_2" },
	{ TRACK_WLS_DOCK_MODEL_3, TRACK_POWER_MW(30000), "model_3" },
	{ TRACK_WLS_DOCK_MODEL_4, TRACK_POWER_MW(30000), "model_4" },
	{ TRACK_WLS_DOCK_MODEL_5, TRACK_POWER_MW(30000), "model_5" },
	{ TRACK_WLS_DOCK_MODEL_6, TRACK_POWER_MW(30000), "model_6" },
	{ TRACK_WLS_DOCK_MODEL_7, TRACK_POWER_MW(30000), "model_7" },
	{ TRACK_WLS_DOCK_MODEL_8, TRACK_POWER_MW(30000), "model_8" },
	{ TRACK_WLS_DOCK_MODEL_9, TRACK_POWER_MW(30000), "model_9" },
	{ TRACK_WLS_DOCK_MODEL_10, TRACK_POWER_MW(30000), "model_10" },
	{ TRACK_WLS_DOCK_MODEL_11, TRACK_POWER_MW(30000), "model_11" },
	{ TRACK_WLS_DOCK_MODEL_12, TRACK_POWER_MW(30000), "model_12" },
	{ TRACK_WLS_DOCK_MODEL_13, TRACK_POWER_MW(30000), "model_13" },
	{ TRACK_WLS_DOCK_MODEL_14, TRACK_POWER_MW(30000), "model_14" },
	{ TRACK_WLS_DOCK_MODEL_15, TRACK_POWER_MW(30000), "model_15" },
	{ TRACK_WLS_DOCK_THIRD_PARTY, TRACK_POWER_MW(50000), "third_party" },
};

static struct oplus_chg_track_vooc_type vooc_type_table[] = {
	{ 0x01, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(4000), "vooc" },
	{ 0x13, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(4000), "vooc" },
	{ 0x34, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(4000), "vooc" },
	{ 0x45, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(4000), "vooc" },

	{ 0x19, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(6000), "vooc" },
	{ 0x29, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(6000), "vooc" },
	{ 0x41, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(6000), "vooc" },
	{ 0x42, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(6000), "vooc" },
	{ 0x43, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(6000), "vooc" },
	{ 0x44, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(6000), "vooc" },
	{ 0x46, TRACK_INPUT_VOL_MV(5000), TRACK_INPUT_CURR_MA(6000), "vooc" },

	{ 0x61, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(3000), "svooc" },
	{ 0x49, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(3000), "svooc" },
	{ 0x4A, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(3000), "svooc" },

	{ 0x11, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(5000), "svooc" },
	{ 0x12, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(5000), "svooc" },
	{ 0x21, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(5000), "svooc" },
	{ 0x31, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(5000), "svooc" },
	{ 0x33, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(5000), "svooc" },
	{ 0x62, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(5000), "svooc" },

	{ 0x14, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(6500), "svooc" },
	{ 0x35, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(6500), "svooc" },
	{ 0x63, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(6500), "svooc" },
	{ 0x6E, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(6500), "svooc" },
	{ 0x66, TRACK_INPUT_VOL_MV(10000), TRACK_INPUT_CURR_MA(6500), "svooc" },

	{ 0x36, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(6000), "svooc" },
	{ 0x64, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(6000), "svooc" },

	{ 0x6C, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(6100), "svooc" },
	{ 0x6D, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(6100), "svooc" },

	{ 0x65, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(7300), "svooc" },
	{ 0x4B, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(7300), "svooc" },
	{ 0x4C, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(7300), "svooc" },
	{ 0x4D, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(7300), "svooc" },
	{ 0x4E, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(7300), "svooc" },

	{ 0x6A, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(9100), "svooc" },
	{ 0x69, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(9100), "svooc" },

	{ 0x6b, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(11000), "svooc" },
	{ 0x32, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(11000), "svooc" },
};

static struct oplus_chg_track_vooc_type ufcs_type_table[] = {
	{ 0x8211, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(3000), "ufcs_third" },
	{ 0x4211, TRACK_INPUT_VOL_MV(11000), TRACK_INPUT_CURR_MA(3000), "ufcs_oplus" },
};

static struct oplus_chg_track_batt_full_reason batt_full_reason_table[] = {
	{ NOTIFY_BAT_FULL, "full_normal" },
	{ NOTIFY_BAT_FULL_PRE_LOW_TEMP, "full_low_temp" },
	{ NOTIFY_BAT_FULL_PRE_HIGH_TEMP, "full_high_temp" },
	{ NOTIFY_BAT_FULL_THIRD_BATTERY, "full_third_batt" },
	{ NOTIFY_BAT_NOT_CONNECT, "full_no_batt" },
};

static struct oplus_chg_track_chg_abnormal_reason chg_abnormal_reason_table[] = {
	{ NOTIFY_CHGING_OVERTIME, "chg_over_time", 0 },
	{ NOTIFY_CHARGER_OVER_VOL, "chg_over_vol", 0 },
	{ NOTIFY_CHARGER_LOW_VOL, "chg_low_vol", 0 },
	{ NOTIFY_BAT_OVER_TEMP, "batt_over_temp", 0 },
	{ NOTIFY_BAT_LOW_TEMP, "batt_low_temp", 0 },
	{ NOTIFY_BAT_OVER_VOL, "batt_over_vol", 0 },
	{ NOTIFY_BAT_NOT_CONNECT, "batt_no_conn", 0 },
	{ NOTIFY_BAT_FULL_THIRD_BATTERY, "batt_no_auth", 0 },
};

static struct oplus_chg_track_i2c_err_reason i2c_err_reason_table[] = {
	{ -EPERM, "operat_not_permit" },
	{ -ENOENT, "no_such_file_or_dir" },
	{ -ESRCH, "no_such_process" },
	{ -EIO, "io_error" },
	{ -ENXIO, "no_such_dev_or_addr" },
	{ -E2BIG, "args_too_long" },
	{ -ENOEXEC, "exec_format_error" },
	{ -EBADF, "bad_file_number" },
	{ -ECHILD, "no_child_process" },
	{ -EAGAIN, "try_again" },
	{ -ENOMEM, "out_of_mem" },
	{ -EACCES, "permission_denied" },
	{ -EFAULT, "bad_address" },
	{ -ENOTBLK, "block_dev_required" },
	{ -EOPNOTSUPP, "not_support" },
	{ -ENOTCONN, "device_not_conn" },
	{ -ETIMEDOUT, "timed_out" },
};

static struct oplus_chg_track_ufcs_err_reason ufcs_err_reason_table[] = {
	{ TRACK_UFCS_ERR_IBUS_LIMIT, "ibus_limit" },
	{ TRACK_UFCS_ERR_CP_ENABLE, "cp_enable" },
	{ TRACK_UFCS_ERR_R_COOLDOWN, "r_cooldown" },
	{ TRACK_UFCS_ERR_BTB_OVER, "btb_over" },
	{ TRACK_UFCS_ERR_USBTEMP_OVER, "usbtemp_over" },
	{ TRACK_UFCS_ERR_TFG_OVER, "tfg_over" },
	{ TRACK_UFCS_ERR_VBAT_DIFF, "vbat_diff" },
};

static struct oplus_chg_track_wls_trx_err_reason wls_trx_err_reason_table[] = {
	{ TRACK_WLS_TRX_ERR_RXAC, "rxac" },
	{ TRACK_WLS_TRX_ERR_OCP, "ocp" },
	{ TRACK_WLS_TRX_ERR_OVP, "ovp" },
	{ TRACK_WLS_TRX_ERR_LVP, "lvp" },
	{ TRACK_WLS_TRX_ERR_FOD, "fod" },
	{ TRACK_WLS_TRX_ERR_CEPTIMEOUT, "timeout" },
	{ TRACK_WLS_TRX_ERR_RXEPT, "rxept" },
	{ TRACK_WLS_TRX_ERR_OTP, "otp" },
	{ TRACK_WLS_TRX_ERR_VOUT, "vout_err" },
	{ TRACK_WLS_UPDATE_ERR_I2C, "i2c_err" },
	{ TRACK_WLS_UPDATE_ERR_CRC, "crc_err" },
	{ TRACK_WLS_UPDATE_ERR_OTHER, "other" },
	{ TRACK_WLS_TRX_VOUT_ABNORMAL, "vout_abnormal" },
};

static struct oplus_chg_track_gpio_err_reason gpio_err_reason_table[] = {
	{ TRACK_GPIO_ERR_CHARGER_ID, "charger_id_err" },
	{ TRACK_GPIO_ERR_VOOC_SWITCH, "vooc_switch_err" },
};

static struct oplus_chg_track_pmic_err_reason pmic_err_reason_table[] = {
	{ TRACK_PMIC_ERR_ICL_VBUS_COLLAPSE, "vbus_collapse" },
	{ TRACK_PMIC_ERR_ICL_VBUS_LOW_POINT, "vbus_low_point" },
};

static struct oplus_chg_track_cp_err_reason cp_err_reason_table[] = {
	{ TRACK_CP_ERR_NO_WORK, "not_work" },
	{ TRACK_CP_ERR_CFLY_CDRV_FAULT, "cfly_cdrv_fault" },
	{ TRACK_CP_ERR_VBAT_OVP, "vbat_ovp" },
	{ TRACK_CP_ERR_IBAT_OCP, "ibat_ocp" },
	{ TRACK_CP_ERR_VBUS_OVP, "vbus_ocp" },
	{ TRACK_CP_ERR_IBUS_OCP, "ibus_ocp" },
};

static struct oplus_chg_track_cp_err_reason bidirect_cp_err_reason_table[] = {
	{ TRACK_BIDIRECT_CP_ERR_SC_EN_STAT, "SC_EN_STAT" },
	{ TRACK_BIDIRECT_CP_ERR_V2X_OVP, "V2X_OVP" },
	{ TRACK_BIDIRECT_CP_ERR_V1X_OVP, "V1X_OVP" },
	{ TRACK_BIDIRECT_CP_ERR_VAC_OVP, "VAC_OVP" },
	{ TRACK_BIDIRECT_CP_ERR_FWD_OCP, "FWD_OCP" },
	{ TRACK_BIDIRECT_CP_ERR_RVS_OCP, "RVS_OCP" },
	{ TRACK_BIDIRECT_CP_ERR_TSHUT, "TSHUT" },
	{ TRACK_BIDIRECT_CP_ERR_VAC2V2X_OVP, "VAC2V2X_OVP" },
	{ TRACK_BIDIRECT_CP_ERR_VAC2V2X_UVP, "VAC2V2X_UVP" },
	{ TRACK_BIDIRECT_CP_ERR_V1X_ISS_OPP, "V1X_ISS_OPP" },
	{ TRACK_BIDIRECT_CP_ERR_WD_TIMEOUT, "WD_TIMEOUT" },
	{ TRACK_BIDIRECT_CP_ERR_LNC_SS_TIMEOUT, "LNC_SS_TIMEOUT" },
};

static struct oplus_chg_track_gague_err_reason gague_err_reason_table[] = {
	{ TRACK_GAGUE_ERR_SEAL, "seal_fail" },
	{ TRACK_GAGUE_ERR_UNSEAL, "unseal_fail" },
};

static struct oplus_chg_track_cooldown_err_reason cooldown_err_reason_table[] = {
	{ TRACK_COOLDOWN_NOT_MATCH, "current_not_match" },
	{ TRACK_COOLDOWN_INVALID, "value_invalid" },
};

static struct oplus_chg_track_pen_match_err_reason pen_match_err_reason_table[] = {
	{ TRACK_PEN_MATCH_TIMEOUT, "pen_match_timeout"},
	{ TRACK_PEN_MATCH_VERIFY_FAILED, "pen_match_verify_faild"},
	{ TRACK_PEN_MATCH_TIMEOUT_AND_VERIFY_FAILED, "pen_match_timeout_verify_faild"},
};

static struct oplus_chg_track_type_mapping type_mapping_table[] = {
	{ ADSP_TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL, TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL },
};

static struct oplus_chg_track_flag_mapping flag_mapping_table[] = {
	{ ADSP_TRACK_NOTIFY_FLAG_GAGUE_ABNORMAL, TRACK_NOTIFY_FLAG_GAGUE_ABNORMAL },
	{ ADSP_TRACK_NOTIFY_FLAG_DCHG_ABNORMAL, TRACK_NOTIFY_FLAG_DCHG_ABNORMAL },
};

static struct oplus_chg_track_cool_down_stats cool_down_stats_table[] = {
	{ 0, 0, "L_0" },   { 1, 0, "L_1" },   { 2, 0, "L_2" },	 { 3, 0, "L_3" },   { 4, 0, "L_4" },
	{ 5, 0, "L_5" },   { 6, 0, "L_6" },   { 7, 0, "L_7" },	 { 8, 0, "L_8" },   { 9, 0, "L_9" },
	{ 10, 0, "L_10" }, { 11, 0, "L_11" }, { 12, 0, "L_12" }, { 13, 0, "L_13" }, { 14, 0, "L_14" },
	{ 15, 0, "L_15" }, { 16, 0, "L_16" }, { 17, 0, "L_17" }, { 18, 0, "L_18" }, { 19, 0, "L_19" },
	{ 20, 0, "L_20" }, { 21, 0, "L_21" }, { 22, 0, "L_22" }, { 23, 0, "L_23" }, { 24, 0, "L_24" },
	{ 25, 0, "L_25" }, { 26, 0, "L_26" }, { 27, 0, "L_27" }, { 28, 0, "L_28" }, { 29, 0, "L_29" },
	{ 30, 0, "L_30" }, { 31, 0, "L_31" },
};

static struct oplus_chg_track_fastchg_break mcu_voocphy_break_table[] = {
	{ TRACK_MCU_VOOCPHY_FAST_ABSENT, "absent" },
	{ TRACK_MCU_VOOCPHY_BAD_CONNECTED, "bad_connect" },
	{ TRACK_MCU_VOOCPHY_BTB_TEMP_OVER, "btb_temp_over" },
	{ TRACK_MCU_VOOCPHY_DATA_ERROR, "data_error" },
	{ TRACK_MCU_VOOCPHY_OTHER, "other" },
	{ TRACK_MCU_VOOCPHY_HEAD_ERROR, "head_error" },
	{ TRACK_MCU_VOOCPHY_ADAPTER_FW_UPDATE, "adapter_fw_update" },
};

static struct oplus_chg_track_fastchg_break adsp_voocphy_break_table[] = {
	{ TRACK_ADSP_VOOCPHY_BAD_CONNECTED, "bad_connect" },
	{ TRACK_ADSP_VOOCPHY_FRAME_H_ERR, "frame_head_error" },
	{ TRACK_ADSP_VOOCPHY_CLK_ERR, "clk_error" },
	{ TRACK_ADSP_VOOCPHY_HW_VBATT_HIGH, "hw_vbatt_high" },
	{ TRACK_ADSP_VOOCPHY_HW_TBATT_HIGH, "hw_ibatt_high" },
	{ TRACK_ADSP_VOOCPHY_COMMU_TIME_OUT, "commu_time_out" },
	{ TRACK_ADSP_VOOCPHY_ADAPTER_COPYCAT, "adapter_copycat" },
	{ TRACK_ADSP_VOOCPHY_BTB_TEMP_OVER, "btb_temp_over" },
	{ TRACK_ADSP_VOOCPHY_OTHER, "other" },
};

static struct oplus_chg_track_fastchg_break ap_voocphy_break_table[] = {
	{ TRACK_CP_VOOCPHY_FAST_ABSENT, "absent" },
	{ TRACK_CP_VOOCPHY_BAD_CONNECTED, "bad_connect" },
	{ TRACK_CP_VOOCPHY_FRAME_H_ERR, "frame_head_error" },
	{ TRACK_CP_VOOCPHY_BTB_TEMP_OVER, "btb_temp_over" },
	{ TRACK_CP_VOOCPHY_COMMU_TIME_OUT, "commu_time_out" },
	{ TRACK_CP_VOOCPHY_ADAPTER_COPYCAT, "adapter_copycat" },
	{ TRACK_CP_VOOCPHY_OTHER, "other" },
};

static struct oplus_chg_track_voocphy_info voocphy_info_table[] = {
	{ TRACK_NO_VOOCPHY, "unknow" },
	{ TRACK_ADSP_VOOCPHY, "adsp" },
	{ TRACK_AP_SINGLE_CP_VOOCPHY, "ap" },
	{ TRACK_AP_DUAL_CP_VOOCPHY, "ap" },
	{ TRACK_MCU_VOOCPHY, "mcu" },
};

static struct oplus_chg_track_err_reason mos_err_reason_table[] = {
	{ TRACK_MOS_I2C_ERROR, "i2c_error" },
	{ TRACK_MOS_OPEN_ERROR, "mos_open_error" },
	{ TRACK_MOS_SUB_BATT_FULL, "sub_batt_full" },
	{ TRACK_MOS_VBAT_GAP_BIG, "vbat_gap_big" },
	{ TRACK_MOS_SOC_NOT_FULL, "full_at_soc_not_full" },
	{ TRACK_MOS_CURRENT_UNBALANCE, "current_unbalance" },
	{ TRACK_MOS_SOC_GAP_TOO_BIG, "soc_gap_too_big" },
	{ TRACK_MOS_RECORD_SOC, "exit_fastchg_record_soc" },
};

static struct oplus_chg_track_speed_ref wired_series_double_cell_125w_150w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(20000), TRACK_POWER_MW(20000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(18000), TRACK_POWER_MW(18000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(12000), TRACK_POWER_MW(12000) * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wired_series_double_cell_65w_80w_100w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(12000), TRACK_POWER_MW(12000) * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wired_equ_single_cell_60w_67w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(12000), TRACK_POWER_MW(12000) * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wired_equ_single_cell_30w_33w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(12000), TRACK_POWER_MW(12000) * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wired_single_cell_18w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(10000), TRACK_POWER_MW(10000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(7000), TRACK_POWER_MW(7000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(6000), TRACK_POWER_MW(6000) * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_40w_45w_50w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(15000), TRACK_POWER_MW(15000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(12000), TRACK_POWER_MW(12000) * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_20w_30w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(10000), TRACK_POWER_MW(10000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(10000), TRACK_POWER_MW(10000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(10000), TRACK_POWER_MW(10000) * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_12w_15w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_equ_single_cell_12w_15w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_epp15w_epp10w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_equ_single_cell_epp15w_epp10w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(5000), TRACK_POWER_MW(5000) * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_bpp[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(2000), TRACK_POWER_MW(2000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(2000), TRACK_POWER_MW(2000) * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(2000), TRACK_POWER_MW(2000) * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_equ_single_cell_bpp[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_MW(2000), TRACK_POWER_MW(2000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_MW(2000), TRACK_POWER_MW(2000) * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_MW(2000), TRACK_POWER_MW(2000) * 1000 / TRACK_REF_VOL_5000MV },
};

/*
* digital represents wls bpp charging scheme
* 0: wls 5w series dual cell bpp charging scheme
* 1: wls 5w single cell bpp charging scheme
* 2: wls 5w parallel double cell bpp charging scheme
*/
static struct oplus_chg_track_speed_ref *g_wls_bpp_speed_ref_standard[] = {
	wls_series_double_cell_bpp,
	wls_equ_single_cell_bpp,
	wls_equ_single_cell_bpp,
};

/*
* digital represents wls epp charging scheme
* 0: wls 10w or 15w series dual cell epp charging scheme
* 1: wls 10w or 15w single cell epp charging scheme
* 2: wls 10w or 15w parallel double cell epp charging scheme
*/
static struct oplus_chg_track_speed_ref *g_wls_epp_speed_ref_standard[] = {
	wls_series_double_cell_epp15w_epp10w,
	wls_equ_single_cell_epp15w_epp10w,
	wls_equ_single_cell_epp15w_epp10w,
};

/*
* digital represents wls fast charging scheme
* 0: wls 40w or 45w or 50w series dual cell charging scheme
* 1: wls 20w or 30w series dual cell charging scheme
* 2: wls 12w or 15w series dual cell charging scheme
* 3: wls 12w or 15w parallel double cell charging scheme
*/
static struct oplus_chg_track_speed_ref *g_wls_fast_speed_ref_standard[] = {
	wls_series_double_cell_40w_45w_50w,
	wls_series_double_cell_20w_30w,
	wls_series_double_cell_12w_15w,
	wired_equ_single_cell_60w_67w,
	wls_equ_single_cell_12w_15w,
	wls_equ_single_cell_12w_15w,
};

/*
* digital represents wired fast charging scheme
* 0: wired 120w or 150w series dual cell charging scheme
* 1: wired 65w or 80w and 100w series dual cell charging scheme
* 2: wired 60w or 67w single cell charging scheme
* 3: wired 60w or 67w parallel double cell charging scheme
* 4: wired 30w or 33w single cell charging scheme
* 5: wired 30w or 33w parallel double cell charging scheme
* 6: wired 18w single cell charging scheme
*/
static struct oplus_chg_track_speed_ref *g_wired_speed_ref_standard[] = {
	wired_series_double_cell_125w_150w,
	wired_series_double_cell_65w_80w_100w,
	wired_equ_single_cell_60w_67w,
	wired_equ_single_cell_60w_67w,
	wired_equ_single_cell_30w_33w,
	wired_equ_single_cell_30w_33w,
	wired_single_cell_18w,
};

static int oplus_chg_track_set_hidl_bcc_info(struct oplus_chg_track_hidl_cmd *cmd, struct oplus_chg_track *track_chip)
{
	int len;
	struct oplus_chg_track_hidl_bcc_info_cmd *bcc_info_cmd;

	if (!cmd)
		return -EINVAL;

	if (cmd->data_size != sizeof(struct oplus_chg_track_hidl_bcc_info_cmd)) {
		pr_err("!!!size not match struct, ignore\n");
		return -EINVAL;
	}

	if (track_chip->track_status.bcc_info->count >= TRACK_HIDL_BCC_INFO_COUNT) {
		pr_err("!!!bcc record count has arrive max, ignore\n");
		return -EINVAL;
	}

	bcc_info_cmd = (struct oplus_chg_track_hidl_bcc_info_cmd *)(cmd->data_buf);
	track_chip->track_status.bcc_info->err_code |= bcc_info_cmd->err_code;
	track_chip->track_status.bcc_info->count++;
	len = strlen(track_chip->track_status.bcc_info->data_buf);
	if (!len)
		snprintf(&(track_chip->track_status.bcc_info->data_buf[len]), TRACK_HIDL_BCC_INFO_LEN - len, "%s",
			 bcc_info_cmd->data_buf);
	else
		snprintf(&(track_chip->track_status.bcc_info->data_buf[len]), TRACK_HIDL_BCC_INFO_LEN - len, ";%s",
			 bcc_info_cmd->data_buf);
	return 0;
}

static int oplus_chg_track_set_hidl_bcc_err(struct oplus_chg_track_hidl_cmd *cmd, struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_hidl_bcc_err_cmd *bcc_err_cmd;
	struct oplus_chg_track_hidl_bcc_err *bcc_err;

	if (!cmd)
		return -EINVAL;

	if (cmd->data_size != sizeof(struct oplus_chg_track_hidl_bcc_err_cmd)) {
		pr_err("!!!size not match struct, ignore\n");
		return -EINVAL;
	}

	bcc_err = &(track_chip->track_status.bcc_err);
	mutex_lock(&bcc_err->track_bcc_err_lock);
	if (bcc_err->bcc_err_uploading) {
		pr_debug("bcc_err_uploading, should return\n");
		mutex_unlock(&bcc_err->track_bcc_err_lock);
		return 0;
	}
	bcc_err_cmd = (struct oplus_chg_track_hidl_bcc_err_cmd *)(cmd->data_buf);
	memcpy(&bcc_err->bcc_err, bcc_err_cmd, sizeof(*bcc_err_cmd));
	mutex_unlock(&bcc_err->track_bcc_err_lock);

	schedule_delayed_work(&bcc_err->bcc_err_load_trigger_work, 0);
	return 0;
}

static int oplus_chg_track_set_hidl_uisoh_info(
	struct oplus_chg_track_hidl_cmd *cmd, struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_hidl_uisoh_info_cmd *uisoh_info_cmd;
	struct oplus_chg_track_hidl_uisoh_info *uisoh_info_p;

	if (!cmd)
		return -EINVAL;

	if (cmd->data_size != sizeof(struct oplus_chg_track_hidl_uisoh_info_cmd)) {
		pr_err("!!!size not match struct, ignore\n");
		return -EINVAL;
	}

	uisoh_info_p = &(track_chip->track_status.uisoh_info_s);
	mutex_lock(&uisoh_info_p->track_uisoh_info_lock);
	if (uisoh_info_p->uisoh_info_uploading) {
		pr_debug("uisoh_info_uploading, should return\n");
		mutex_unlock(&uisoh_info_p->track_uisoh_info_lock);
		return 0;
	}
	uisoh_info_cmd = (struct oplus_chg_track_hidl_uisoh_info_cmd *)(cmd->data_buf);
	memcpy(&uisoh_info_p->uisoh_info, uisoh_info_cmd, sizeof(*uisoh_info_cmd));
	mutex_unlock(&uisoh_info_p->track_uisoh_info_lock);

	schedule_delayed_work(&uisoh_info_p->uisoh_info_load_trigger_work, 0);
	return 0;
}

static void oplus_track_upload_bcc_err_info(struct work_struct *work)
{
	int index = 0;
	int curr_time;
	char power_info[OPLUS_CHG_TRACK_POWER_INFO_LEN] = { 0 };
	static int upload_count = 0;
	static int pre_upload_time = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track_hidl_bcc_err *bcc_err =
		container_of(dwork, struct oplus_chg_track_hidl_bcc_err, bcc_err_load_trigger_work);

	if (!bcc_err)
		return;

	curr_time = oplus_chg_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_SOFT_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_SOFT_UPLOAD_COUNT_MAX)
		return;

	mutex_lock(&bcc_err->track_bcc_err_lock);
	if (bcc_err->bcc_err_uploading) {
		pr_debug("bcc_err_uploading, should return\n");
		mutex_unlock(&bcc_err->track_bcc_err_lock);
		return;
	}

	if (!strlen(bcc_err->bcc_err.err_reason)) {
		pr_debug("bcc_err len is invalid\n");
		mutex_unlock(&bcc_err->track_bcc_err_lock);
		return;
	}

	if (bcc_err->bcc_err_load_trigger)
		kfree(bcc_err->bcc_err_load_trigger);
	bcc_err->bcc_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!bcc_err->bcc_err_load_trigger) {
		pr_err("bcc_err_load_trigger memery alloc fail\n");
		mutex_unlock(&bcc_err->track_bcc_err_lock);
		return;
	}
	bcc_err->bcc_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL;
	bcc_err->bcc_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_SMART_CHG_ABNORMAL;
	bcc_err->bcc_err_uploading = true;
	upload_count++;
	pre_upload_time = oplus_chg_track_get_local_time_s();
	mutex_unlock(&bcc_err->track_bcc_err_lock);

	index += snprintf(&(bcc_err->bcc_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", "bcc_err");

	index += snprintf(&(bcc_err->bcc_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", bcc_err->bcc_err.err_reason);

	oplus_chg_track_obtain_power_info(power_info, sizeof(power_info));
	index += snprintf(&(bcc_err->bcc_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "%s", power_info);
	index += snprintf(&(bcc_err->bcc_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$curx_info@@%s", bcc_err->bcc_err.data_buf);

	oplus_chg_track_upload_trigger_data(*(bcc_err->bcc_err_load_trigger));
	if (bcc_err->bcc_err_load_trigger) {
		kfree(bcc_err->bcc_err_load_trigger);
		bcc_err->bcc_err_load_trigger = NULL;
	}
	memset(&bcc_err->bcc_err, 0, sizeof(bcc_err->bcc_err));
	bcc_err->bcc_err_uploading = false;
	pr_debug("success\n");
}

static void oplus_track_upload_uisoh_info(struct work_struct *work)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track_hidl_uisoh_info *uisoh_info_p = container_of(
		dwork, struct oplus_chg_track_hidl_uisoh_info, uisoh_info_load_trigger_work);

	if (!uisoh_info_p)
		return;

	curr_time = oplus_chg_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_SOFT_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_SOFT_SOH_UPLOAD_COUNT_MAX)
		return;

	mutex_lock(&uisoh_info_p->track_uisoh_info_lock);
	if (uisoh_info_p->uisoh_info_uploading) {
		pr_debug("uisoh_info_uploading, should return\n");
		mutex_unlock(&uisoh_info_p->track_uisoh_info_lock);
		return;
	}

	if (uisoh_info_p->uisoh_info_load_trigger)
		kfree(uisoh_info_p->uisoh_info_load_trigger);
	uisoh_info_p->uisoh_info_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!uisoh_info_p->uisoh_info_load_trigger) {
		pr_err("uisoh_info_load_trigger memery alloc fail\n");
		mutex_unlock(&uisoh_info_p->track_uisoh_info_lock);
		return;
	}
	uisoh_info_p->uisoh_info_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL;
	uisoh_info_p->uisoh_info_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_SMART_CHG_ABNORMAL;
	uisoh_info_p->uisoh_info_uploading = true;
	upload_count++;
	pre_upload_time = oplus_chg_track_get_local_time_s();
	mutex_unlock(&uisoh_info_p->track_uisoh_info_lock);

	index += snprintf(
		&(uisoh_info_p->uisoh_info_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		"uisoh_info");

	index += snprintf(&(uisoh_info_p->uisoh_info_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$curx_info@@%s", uisoh_info_p->uisoh_info.data_buf);

	oplus_chg_track_upload_trigger_data(*(uisoh_info_p->uisoh_info_load_trigger));
	if (uisoh_info_p->uisoh_info_load_trigger) {
		kfree(uisoh_info_p->uisoh_info_load_trigger);
		uisoh_info_p->uisoh_info_load_trigger = NULL;
	}
	memset(&uisoh_info_p->uisoh_info, 0, sizeof(uisoh_info_p->uisoh_info));
	uisoh_info_p->uisoh_info_uploading = false;
	pr_debug("success\n");
}

static int oplus_chg_track_bcc_err_init(struct oplus_chg_track *chip)
{
	struct oplus_chg_track_hidl_bcc_err *bcc_err;

	if (!chip)
		return -EINVAL;

	bcc_err = &(chip->track_status.bcc_err);
	mutex_init(&bcc_err->track_bcc_err_lock);
	bcc_err->bcc_err_uploading = false;
	bcc_err->bcc_err_load_trigger = NULL;

	memset(&bcc_err->bcc_err, 0, sizeof(bcc_err->bcc_err));
	INIT_DELAYED_WORK(&bcc_err->bcc_err_load_trigger_work, oplus_track_upload_bcc_err_info);

	return 0;
}

static int oplus_chg_track_uisoh_err_init(struct oplus_chg_track *chip)
{
	struct oplus_chg_track_hidl_uisoh_info *uisoh_info_p;

	if (!chip)
		return - EINVAL;

	uisoh_info_p = &(chip->track_status.uisoh_info_s);
	mutex_init(&uisoh_info_p->track_uisoh_info_lock);
	uisoh_info_p->uisoh_info_uploading = false;
	uisoh_info_p->uisoh_info_load_trigger = NULL;

	memset(&uisoh_info_p->uisoh_info, 0, sizeof(uisoh_info_p->uisoh_info));
	INIT_DELAYED_WORK(&uisoh_info_p->uisoh_info_load_trigger_work,
		oplus_track_upload_uisoh_info);

	return 0;
}
static int oplus_chg_track_set_hidl_bms_info(struct oplus_chg_track_hidl_cmd *cmd, struct oplus_chg_track *track_chip)
{
	int len;

	if (!cmd)
		return -EINVAL;

	if (cmd->data_size != sizeof(track_chip->track_status.bms_info)) {
		pr_err("!!!size not match struct, ignore\n");
		return -EINVAL;
	}

	len = strlen(cmd->data_buf);
	if (len)
		snprintf(&(track_chip->track_status.bms_info[0]), TRACK_HIDL_BMS_INFO_LEN, "%s", cmd->data_buf);
	pr_err("bms_info: %s\n", track_chip->track_status.bms_info);
	return len;
}

static int oplus_chg_track_set_hidl_hyper_info(struct oplus_chg_track_hidl_cmd *cmd, struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_hidl_hyper_info *hidl_hyper_info;

	if (!cmd)
		return -EINVAL;

	if (cmd->data_size != sizeof(struct oplus_chg_track_hidl_hyper_info)) {
		pr_err("!!!size not match struct, ignore\n");
		return -EINVAL;
	}

	hidl_hyper_info = (struct oplus_chg_track_hidl_hyper_info *)(cmd->data_buf);
	track_chip->track_status.hyper_en = hidl_hyper_info->hyper_en;

	return 0;
}

static int oplus_chg_track_set_hidl_wls_thrid_err(struct oplus_chg_track_hidl_cmd *cmd,
						  struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_hidl_wls_third_err_cmd *wls_third_cmd;
	struct oplus_chg_track_hidl_wls_third_err *wls_third_err;

	if (!cmd)
		return -EINVAL;

	if (cmd->data_size != sizeof(struct oplus_chg_track_hidl_wls_third_err_cmd)) {
		pr_err("!!!size not match struct, ignore\n");
		return -EINVAL;
	}

	wls_third_err = &(track_chip->track_status.wls_third_err);
	mutex_lock(&wls_third_err->track_wls_third_err_lock);
	if (wls_third_err->wls_third_err_uploading) {
		pr_debug("wls_third_err_uploading, should return\n");
		mutex_unlock(&wls_third_err->track_wls_third_err_lock);
		return 0;
	}
	wls_third_cmd = (struct oplus_chg_track_hidl_wls_third_err_cmd *)(cmd->data_buf);
	memcpy(&wls_third_err->wls_third_err, wls_third_cmd, sizeof(*wls_third_cmd));
	mutex_unlock(&wls_third_err->track_wls_third_err_lock);

	schedule_delayed_work(&wls_third_err->wls_third_err_load_trigger_work, 0);
	return 0;
}

static void oplus_track_upload_wls_third_err_info(struct work_struct *work)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track_hidl_wls_third_err *wls_third_err =
		container_of(dwork, struct oplus_chg_track_hidl_wls_third_err, wls_third_err_load_trigger_work);

	if (!wls_third_err)
		return;

	curr_time = oplus_chg_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_SOFT_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_SOFT_UPLOAD_COUNT_MAX)
		return;

	mutex_lock(&wls_third_err->track_wls_third_err_lock);
	if (wls_third_err->wls_third_err_uploading) {
		pr_debug("wls_third_err_uploading, should return\n");
		mutex_unlock(&wls_third_err->track_wls_third_err_lock);
		return;
	}

	if (!strlen(wls_third_err->wls_third_err.err_reason)) {
		pr_debug("wls_third_err len is invalid\n");
		mutex_unlock(&wls_third_err->track_wls_third_err_lock);
		return;
	}

	if (wls_third_err->wls_third_err_load_trigger)
		kfree(wls_third_err->wls_third_err_load_trigger);
	wls_third_err->wls_third_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!wls_third_err->wls_third_err_load_trigger) {
		pr_err("wls_third_err_load_trigger memery alloc fail\n");
		mutex_unlock(&wls_third_err->track_wls_third_err_lock);
		return;
	}
	if (!strcmp(wls_third_err->wls_third_err.err_reason, "v_k_id_change")) {
		wls_third_err->wls_third_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_GENERAL_RECORD;
		wls_third_err->wls_third_err_load_trigger->flag_reason =
			TRACK_NOTIFY_FLAG_SERVICE_UPDATE_WLS_THIRD_INFO;
	} else {
		wls_third_err->wls_third_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL;
		wls_third_err->wls_third_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_WLS_THIRD_ENCRY_ABNORMAL;
	}
	wls_third_err->wls_third_err_uploading = true;
	upload_count++;
	pre_upload_time = oplus_chg_track_get_local_time_s();
	mutex_unlock(&wls_third_err->track_wls_third_err_lock);

	index += snprintf(&(wls_third_err->wls_third_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
			  wls_third_err->wls_third_err.err_sence);

	index += snprintf(&(wls_third_err->wls_third_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_reason@@%s",
			  wls_third_err->wls_third_err.err_reason);

	index += snprintf(&(wls_third_err->wls_third_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$curx_info@@%s",
			  wls_third_err->wls_third_err.data_buf);

	oplus_chg_track_upload_trigger_data(*(wls_third_err->wls_third_err_load_trigger));
	if (wls_third_err->wls_third_err_load_trigger) {
		kfree(wls_third_err->wls_third_err_load_trigger);
		wls_third_err->wls_third_err_load_trigger = NULL;
	}
	memset(&wls_third_err->wls_third_err, 0, sizeof(wls_third_err->wls_third_err));
	wls_third_err->wls_third_err_uploading = false;
	pr_debug("success\n");
}

static int oplus_chg_track_wls_third_err_init(struct oplus_chg_track *chip)
{
	struct oplus_chg_track_hidl_wls_third_err *wls_third_err;

	if (!chip)
		return -EINVAL;

	wls_third_err = &(chip->track_status.wls_third_err);
	mutex_init(&wls_third_err->track_wls_third_err_lock);
	wls_third_err->wls_third_err_uploading = false;
	wls_third_err->wls_third_err_load_trigger = NULL;

	memset(&wls_third_err->wls_third_err, 0, sizeof(wls_third_err->wls_third_err));
	INIT_DELAYED_WORK(&wls_third_err->wls_third_err_load_trigger_work, oplus_track_upload_wls_third_err_info);

	return 0;
}

int oplus_chg_track_set_hidl_info(const char *buf, size_t count)
{
	struct oplus_chg_track *track_chip = g_track_chip;
	struct oplus_chg_track_hidl_cmd *p_cmd;

	if (!track_chip)
		return -EINVAL;

	p_cmd = (struct oplus_chg_track_hidl_cmd *)buf;
	if (count != sizeof(struct oplus_chg_track_hidl_cmd)) {
		pr_err("!!!size of buf is not matched\n");
		return -EINVAL;
	}

	pr_debug("!!!cmd[%d]\n", p_cmd->cmd);
	switch (p_cmd->cmd) {
	case TRACK_HIDL_BCC_INFO:
		oplus_chg_track_set_hidl_bcc_info(p_cmd, track_chip);
		break;
	case TRACK_HIDL_BCC_ERR:
		oplus_chg_track_set_hidl_bcc_err(p_cmd, track_chip);
		break;
	case TRACK_HIDL_BMS_INFO:
		oplus_chg_track_set_hidl_bms_info(p_cmd, track_chip);
		break;
	case TRACK_HIDL_HYPER_INFO:
		oplus_chg_track_set_hidl_hyper_info(p_cmd, track_chip);
		break;
	case TRACK_HIDL_WLS_THIRD_ERR:
		oplus_chg_track_set_hidl_wls_thrid_err(p_cmd, track_chip);
		break;
	case TRACK_HIDL_UISOH_INFO:
		oplus_chg_track_set_hidl_uisoh_info(p_cmd, track_chip);
		break;
	default:
		pr_err("!!!cmd error\n");
		break;
	}

	return 0;
}
__maybe_unused static bool is_wls_ocm_available(struct oplus_chg_track *chip)
{
	if (!chip->wls_ocm)
		chip->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!chip->wls_ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_track *chip)
{
	if (!chip->comm_ocm)
		chip->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!chip->comm_ocm;
}

struct dentry *oplus_chg_track_get_debugfs_root(void)
{
	mutex_lock(&debugfs_root_mutex);
	if (!track_debugfs_root) {
		track_debugfs_root = debugfs_create_dir("oplus_chg_track", NULL);
	}
	mutex_unlock(&debugfs_root_mutex);

	return track_debugfs_root;
}

int __attribute__((weak)) oplus_chg_wired_get_break_sub_crux_info(char *crux_info)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_wls_get_break_sub_crux_info(struct device *dev, char *crux_info)
{
	return 0;
}

static int oplus_chg_track_clear_cool_down_stats_time(struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cool_down_stats_table); i++)
		cool_down_stats_table[i].time = 0;
	return 0;
}

static int oplus_chg_track_set_voocphy_name(struct oplus_chg_track *track_chip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(voocphy_info_table); i++) {
		if (voocphy_info_table[i].voocphy_type == track_chip->track_cfg.voocphy_type) {
			strncpy(track_chip->voocphy_name, voocphy_info_table[i].name,
				OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN - 1);
			break;
		}
	}

	if (i == ARRAY_SIZE(voocphy_info_table))
		strncpy(track_chip->voocphy_name, voocphy_info_table[0].name, OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN - 1);

	return 0;
}

static int oplus_chg_track_get_base_type_info(int charge_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(base_type_table); i++) {
		if (base_type_table[i].type == charge_type) {
			strncpy(track_status->power_info.wired_info.adapter_type, base_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wired_info.power = base_type_table[i].power;
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_enhance_type_info(int charge_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(enhance_type_table); i++) {
		if (enhance_type_table[i].type == charge_type) {
			strncpy(track_status->power_info.wired_info.adapter_type, enhance_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wired_info.power = enhance_type_table[i].power;
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_vooc_type_info(int vooc_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int vooc_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(vooc_type_table); i++) {
		if (vooc_type_table[i].chg_type == vooc_type) {
			strncpy(track_status->power_info.wired_info.adapter_type, vooc_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wired_info.power =
				vooc_type_table[i].vol * vooc_type_table[i].cur / 1000 / 500;
			track_status->power_info.wired_info.power *= 500;
			track_status->power_info.wired_info.adapter_id = vooc_type_table[i].chg_type;
			vooc_index = i;
			break;
		}
	}

	return vooc_index;
}

#define OPLUS_UFCS_ID_MIN 256
static int oplus_chg_track_get_ufcs_type_info(int vooc_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int vooc_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ufcs_type_table); i++) {
		if (ufcs_type_table[i].chg_type == vooc_type) {
			strncpy(track_status->power_info.wired_info.adapter_type, ufcs_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wired_info.power =
				ufcs_type_table[i].vol * ufcs_type_table[i].cur / 1000 / 500;
			track_status->power_info.wired_info.power *= 500;
			track_status->power_info.wired_info.adapter_id = ufcs_type_table[i].chg_type;
			vooc_index = i;
			break;
		}
	}

	return vooc_index;
}

static int oplus_chg_track_get_wls_adapter_type_info(int charge_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int len;
	int charge_index = -EINVAL;
	struct oplus_chg_track_type *wls_adapter_type_table;

	if (!g_track_chip)
		return -EINVAL;

	if (is_comm_ocm_available(g_track_chip)) {
		wls_adapter_type_table = new_wls_adapter_type_table;
		len = ARRAY_SIZE(new_wls_adapter_type_table);
	} else {
		wls_adapter_type_table = old_wls_adapter_type_table;
		len = ARRAY_SIZE(old_wls_adapter_type_table);
	}

	for (i = 0; i < len; i++) {
		if (wls_adapter_type_table[i].type == charge_type) {
			strncpy(track_status->power_info.wls_info.adapter_type, wls_adapter_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wls_info.power = wls_adapter_type_table[i].power;
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_wls_dock_type_info(int charge_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(wls_dock_type_table); i++) {
		if (wls_dock_type_table[i].type == charge_type) {
			strncpy(track_status->power_info.wls_info.dock_type, wls_dock_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_batt_full_reason_info(int notify_flag, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(batt_full_reason_table); i++) {
		if (batt_full_reason_table[i].notify_flag == notify_flag) {
			strncpy(track_status->batt_full_reason, batt_full_reason_table[i].reason,
				OPLUS_CHG_TRACK_BATT_FULL_REASON_LEN - 1);
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

int oplus_chg_track_get_mos_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(mos_err_reason_table); i++) {
		if (mos_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, mos_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(mos_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_ufcs_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(ufcs_err_reason_table); i++) {
		if (ufcs_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, ufcs_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(ufcs_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_i2c_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(i2c_err_reason_table); i++) {
		if (i2c_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, i2c_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(i2c_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_wls_trx_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(wls_trx_err_reason_table); i++) {
		if (wls_trx_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, wls_trx_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(wls_trx_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_gpio_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(gpio_err_reason_table); i++) {
		if (gpio_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, gpio_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(gpio_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_pmic_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(pmic_err_reason_table); i++) {
		if (pmic_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, pmic_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(pmic_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_bidirect_cp_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(bidirect_cp_err_reason_table); i++) {
		if (bidirect_cp_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, bidirect_cp_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(cp_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_cp_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(cp_err_reason_table); i++) {
		if (cp_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, cp_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(cp_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_gague_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(gague_err_reason_table); i++) {
		if (gague_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, gague_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(gague_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_track_get_cooldown_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(cooldown_err_reason_table); i++) {
		if (cooldown_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, cooldown_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(cooldown_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

int oplus_chg_get_track_pen_match_err_reason(int err_type, char *err_reason, int len)
{
	int i;
	int charge_index = -EINVAL;

	if (!err_reason || !len)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(pen_match_err_reason_table); i++) {
		if (pen_match_err_reason_table[i].err_type == err_type) {
			strncpy(err_reason, pen_match_err_reason_table[i].err_name, len);
			charge_index = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(pen_match_err_reason_table))
		strncpy(err_reason, "unknow_err", len);

	return charge_index;
}

static int oplus_chg_track_adsp_to_ap_type_mapping(int adsp_type, int *ap_type)
{
	int i;
	int charge_index = -EINVAL;

	if (!ap_type)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(type_mapping_table); i++) {
		if (type_mapping_table[i].adsp_type == adsp_type) {
			*ap_type = type_mapping_table[i].ap_type;
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_adsp_to_ap_flag_mapping(int adsp_flag, int *ap_flag)
{
	int i;
	int charge_index = -EINVAL;

	if (!ap_flag)
		return charge_index;

	for (i = 0; i < ARRAY_SIZE(flag_mapping_table); i++) {
		if (flag_mapping_table[i].adsp_flag == adsp_flag) {
			*ap_flag = flag_mapping_table[i].ap_flag;
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_chg_abnormal_reason_info(int notify_code, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;
	int index;

	index = strlen(track_status->chg_abnormal_reason);
	for (i = 0; i < ARRAY_SIZE(chg_abnormal_reason_table); i++) {
		if (!chg_abnormal_reason_table[i].happened && chg_abnormal_reason_table[i].notify_code == notify_code) {
			chg_abnormal_reason_table[i].happened = true;
			if (!index)
				index += snprintf(&(track_status->chg_abnormal_reason[index]),
						  OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LENS - index, "%s",
						  chg_abnormal_reason_table[i].reason);
			else
				index += snprintf(&(track_status->chg_abnormal_reason[index]),
						  OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LENS - index, ",%s",
						  chg_abnormal_reason_table[i].reason);
			break;
			charge_index = i;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_prop(struct oplus_chg_mod *ocm, enum oplus_chg_mod_property prop,
				    union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_track *track_dev = oplus_chg_mod_get_drvdata(ocm);

	if (!track_dev)
		return -ENODEV;

	switch (prop) {
	case OPLUS_CHG_PROP_STATUS:
		pval->intval = track_dev->track_status.wls_prop_status;
		break;
	default:
		pr_err("get prop %d is not supported\n", prop);
		return -EINVAL;
	}

	return 0;
}

static int oplus_chg_track_set_prop(struct oplus_chg_mod *ocm, enum oplus_chg_mod_property prop,
				    const union oplus_chg_mod_propval *pval)
{
	struct oplus_chg_track *chip = oplus_chg_mod_get_drvdata(ocm);
	int rc = 0;

	if (!chip)
		return -ENODEV;

	switch (prop) {
	case OPLUS_CHG_PROP_STATUS:
		chip->track_status.wls_prop_status = pval->intval;
		pr_debug("wls_prop_status:%d\n", pval->intval);
		break;
	default:
		pr_err("set prop %d is not supported\n", prop);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_chg_track_prop_is_writeable(struct oplus_chg_mod *ocm, enum oplus_chg_mod_property prop)
{
	switch (prop) {
	case OPLUS_CHG_PROP_STATUS:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum oplus_chg_mod_property oplus_chg_track_props[] = {
	OPLUS_CHG_PROP_STATUS,
};

static const struct oplus_chg_mod_desc oplus_chg_track_mod_desc = {
	.name = "track",
	.type = OPLUS_CHG_MOD_TRACK,
	.properties = oplus_chg_track_props,
	.num_properties = ARRAY_SIZE(oplus_chg_track_props),
	.uevent_properties = NULL,
	.uevent_num_properties = 0,
	.exten_properties = NULL,
	.num_exten_properties = 0,
	.get_property = oplus_chg_track_get_prop,
	.set_property = oplus_chg_track_set_prop,
	.property_is_writeable = oplus_chg_track_prop_is_writeable,
};

static int oplus_chg_track_event_notifier_call(struct notifier_block *nb, unsigned long val, void *v)
{
	struct oplus_chg_mod *owner_ocm = v;

	switch (val) {
	case OPLUS_CHG_EVENT_PRESENT:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous "
			       "sending\n",
			       val);
			return NOTIFY_BAD;
		}

		if (!strcmp(owner_ocm->desc->name, "wireless")) {
			pr_debug("%s wls present\n", __func__);
			oplus_chg_track_check_wls_charging_break(true);
		}
		break;
	case OPLUS_CHG_EVENT_OFFLINE:
		if (owner_ocm == NULL) {
			pr_err("This event(=%d) does not support anonymous "
			       "sending\n",
			       val);
			return NOTIFY_BAD;
		}

		if (!strcmp(owner_ocm->desc->name, "wireless")) {
			pr_debug("%s wls offline\n", __func__);
			oplus_chg_track_check_wls_charging_break(false);
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int oplus_chg_track_init_mod(struct oplus_chg_track *track_dev)
{
	struct oplus_chg_mod_config ocm_cfg = {};
	int rc;

	ocm_cfg.drv_data = track_dev;
	ocm_cfg.of_node = track_dev->dev->of_node;

	track_dev->track_ocm = oplus_chg_mod_register(track_dev->dev, &oplus_chg_track_mod_desc, &ocm_cfg);
	if (IS_ERR(track_dev->track_ocm)) {
		pr_err("Couldn't register track ocm\n");
		rc = PTR_ERR(track_dev->track_ocm);
		return rc;
	}

	track_dev->track_event_nb.notifier_call = oplus_chg_track_event_notifier_call;
	rc = oplus_chg_reg_event_notifier(&track_dev->track_event_nb);
	if (rc) {
		pr_err("register track event notifier error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int oplus_chg_track_parse_dt(struct oplus_chg_track *track_dev)
{
	int rc;
	struct device_node *node = track_dev->dev->of_node;

	rc = of_property_read_u32(node, "track,fast_chg_break_t_thd", &(track_dev->track_cfg.fast_chg_break_t_thd));
	if (rc < 0) {
		pr_err("track,fast_chg_break_t_thd reading failed, rc=%d\n", rc);
		track_dev->track_cfg.fast_chg_break_t_thd = TRACK_T_THD_1000_MS;
	}
	rc = of_property_read_u32(node, "track,ufcs_chg_break_t_thd", &(track_dev->track_cfg.ufcs_chg_break_t_thd));
	if (rc < 0) {
		pr_err("track,ufcs_chg_break_t_thd reading failed, rc=%d\n", rc);
		track_dev->track_cfg.ufcs_chg_break_t_thd = TRACK_T_THD_2000_MS;
	}

	rc = of_property_read_u32(node, "track,general_chg_break_t_thd",
				  &(track_dev->track_cfg.general_chg_break_t_thd));
	if (rc < 0) {
		pr_err("track,general_chg_break_t_thd reading failed, rc=%d\n", rc);
		track_dev->track_cfg.general_chg_break_t_thd = TRACK_T_THD_500_MS;
	}

	rc = of_property_read_u32(node, "track,voocphy_type", &(track_dev->track_cfg.voocphy_type));
	if (rc < 0) {
		pr_err("track,voocphy_type reading failed, rc=%d\n", rc);
		track_dev->track_cfg.voocphy_type = TRACK_NO_VOOCPHY;
	}

	rc = of_property_read_u32(node, "track,wls_chg_break_t_thd", &(track_dev->track_cfg.wls_chg_break_t_thd));
	if (rc < 0) {
		pr_err("track,wls_chg_break_t_thd reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_chg_break_t_thd = TRACK_T_THD_6000_MS;
	}

	rc = of_property_read_u32(node, "track,wired_fast_chg_scheme", &(track_dev->track_cfg.wired_fast_chg_scheme));
	if (rc < 0) {
		pr_err("track,wired_fast_chg_scheme reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wired_fast_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_fast_chg_scheme", &(track_dev->track_cfg.wls_fast_chg_scheme));
	if (rc < 0) {
		pr_err("track,wls_fast_chg_scheme reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_fast_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_epp_chg_scheme", &(track_dev->track_cfg.wls_epp_chg_scheme));
	if (rc < 0) {
		pr_err("track,wls_epp_chg_scheme reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_epp_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_bpp_chg_scheme", &(track_dev->track_cfg.wls_bpp_chg_scheme));
	if (rc < 0) {
		pr_err("track,wls_bpp_chg_scheme reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_bpp_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_max_power", &(track_dev->track_cfg.wls_max_power));
	if (rc < 0) {
		pr_err("track,wls_max_power reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_max_power = 0;
	}

	rc = of_property_read_u32(node, "track,wired_max_power", &(track_dev->track_cfg.wired_max_power));
	if (rc < 0) {
		pr_err("track,wired_max_power reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wired_max_power = 0;
	}

	rc = of_property_read_u32(node, "track,track_ver", &(track_dev->track_cfg.track_ver));
	if (rc < 0) {
		pr_err("track,track_ver reading failed, rc=%d\n", rc);
		track_dev->track_cfg.track_ver = 3;
	}

	return 0;
}

static void oplus_chg_track_uisoc_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, uisoc_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_load_trigger);
}

static void oplus_chg_track_soc_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, soc_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->soc_trigger);
}

static void oplus_chg_track_uisoc_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, uisoc_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_trigger);
}

static void oplus_chg_track_uisoc_to_soc_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, uisoc_to_soc_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_to_soc_trigger);
}

int oplus_chg_track_obtain_general_info(u8 *curx, int index, int len)
{
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!curx || !chip || !g_track_chip || index > len)
		return -EINVAL;

	index += snprintf(
		&(curx[index]), len - index,
		"$$other@@CHGR[ %d %d %d %d %d], "
		"BAT[ %d %d %d %d %d %4d ], "
		"GAUGE[ %3d %3d %d %d %4d %7d %3d %3d %3d %3d %4d], "
		"STATUS[ %d %4d %d %d %d 0x%-4x %d %d %d], "
		"OTHER[ %d %d %d %d %d %d %3d %3d ], "
		"SLOW[%d %d %d %d %d %d %d], "
		"VOOCPHY[ %d %d %d %d %d 0x%0x], "
		"BREAK[0x%0x %d %d]",
		chip->charger_exist, chip->charger_type, chip->charger_volt, chip->prop_status, chip->boot_mode,
		chip->batt_exist, chip->batt_full, chip->chging_on, chip->in_rechging, chip->charging_state,
		chip->total_time, chip->temperature, chip->tbatt_temp, chip->batt_volt, chip->batt_volt_min,
		chip->icharging, chip->ibus, chip->soc, chip->ui_soc, chip->soc_load, chip->batt_rm, chip->batt_fcc,
		chip->vbatt_over, chip->chging_over_time, chip->vchg_status, chip->tbatt_status, chip->stop_voter,
		chip->notify_code, chip->sw_full, chip->hw_full_by_sw, chip->hw_full, chip->otg_switch, chip->mmi_chg,
		chip->boot_reason, chip->boot_mode, chip->chargerid_volt, chip->chargerid_volt_got,
		chip->shell_temp, chip->subboard_temp,
		g_track_chip->track_status.has_judge_speed, g_track_chip->track_status.soc_low_sect_incr_rm,
		g_track_chip->track_status.soc_low_sect_cont_time, g_track_chip->track_status.soc_medium_sect_incr_rm,
		g_track_chip->track_status.soc_medium_sect_cont_time, g_track_chip->track_status.soc_high_sect_incr_rm,
		g_track_chip->track_status.soc_high_sect_cont_time, oplus_voocphy_get_fastchg_start(),
		oplus_voocphy_get_fastchg_ing(), oplus_voocphy_get_fastchg_dummy_start(),
		oplus_voocphy_get_fastchg_to_normal(), oplus_voocphy_get_fastchg_to_warm(),
		oplus_voocphy_get_fast_chg_type(), g_track_chip->track_status.pre_fastchg_type,
		g_track_chip->track_cfg.voocphy_type, g_track_chip->track_status.fastchg_break_info.code);
	pr_debug("curx:%s\n", curx);

	return 0;
}

static int oplus_chg_track_record_general_info(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status,
					       oplus_chg_track_trigger *p_trigger_data, int index)
{
	if (!chip || !p_trigger_data || !track_status)
		return -1;

	if (index < 0 || index >= OPLUS_CHG_TRACK_CURX_INFO_LEN) {
		pr_err("index is invalid\n");
		return -1;
	}

	index +=
		snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			 "$$other@@CHGR[%d %d %d %d %d], "
			 "BAT[%d %d %d %d %d %4d ], "
			 "GAUGE[%3d %3d %d %d %4d %7d %3d %3d %3d %3d %3d %4d], "
			 "STATUS[ %d %4d %d %d %d 0x%-4x %d %d %d], "
			 "OTHER[ %d %d %d %d %d %d %3d %3d ], "
			 "SLOW[%d %d %d %d %d %d %d], "
			 "VOOCPHY[ %d %d %d %d %d 0x%0x]",
			 chip->charger_exist, chip->charger_type, chip->charger_volt, chip->prop_status,
			 chip->boot_mode, chip->batt_exist, chip->batt_full, chip->chging_on, chip->in_rechging,
			 chip->charging_state, chip->total_time, chip->temperature, chip->tbatt_temp, chip->batt_volt,
			 chip->batt_volt_min, chip->icharging, chip->ibus, chip->soc, chip->smooth_soc, chip->ui_soc,
			 chip->soc_load, chip->batt_rm, chip->batt_fcc, chip->vbatt_over, chip->chging_over_time,
			 chip->vchg_status, chip->tbatt_status, chip->stop_voter, chip->notify_code, chip->sw_full,
			 chip->hw_full_by_sw, chip->hw_full, chip->otg_switch, chip->mmi_chg, chip->boot_reason,
			 chip->boot_mode, chip->chargerid_volt, chip->chargerid_volt_got,
			 chip->shell_temp, chip->subboard_temp, track_status->has_judge_speed,
			 track_status->soc_low_sect_incr_rm, track_status->soc_low_sect_cont_time,
			 track_status->soc_medium_sect_incr_rm, track_status->soc_medium_sect_cont_time,
			 track_status->soc_high_sect_incr_rm, track_status->soc_high_sect_cont_time,
			 oplus_voocphy_get_fastchg_start(), oplus_voocphy_get_fastchg_ing(),
			 oplus_voocphy_get_fastchg_dummy_start(), oplus_voocphy_get_fastchg_to_normal(),
			 oplus_voocphy_get_fastchg_to_warm(), oplus_voocphy_get_fast_chg_type());

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		if (strlen(g_track_chip->wls_break_crux_info))
			index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "%s ", g_track_chip->wls_break_crux_info);
	}
	pr_debug("index:%d\n", index);
	return 0;
}

static int oplus_chg_track_pack_cool_down_stats(struct oplus_chg_track_status *track_status, char *cool_down_pack)
{
	int i;
	int index = 0;

	if (cool_down_pack == NULL || track_status == NULL)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cool_down_stats_table) - 1; i++) {
		index += snprintf(&(cool_down_pack[index]), OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN - index, "%s,%d;",
				  cool_down_stats_table[i].level_name, cool_down_stats_table[i].time);
	}

	index += snprintf(&(cool_down_pack[index]), OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN - index, "%s,%d",
			  cool_down_stats_table[i].level_name,
			  cool_down_stats_table[i].time * TRACK_THRAD_PERIOD_TIME_S);
	pr_debug("i=%d, cool_down_pack[%s]\n", i, cool_down_pack);

	return 0;
}

static void oplus_chg_track_record_charger_info(struct oplus_chg_chip *chip, oplus_chg_track_trigger *p_trigger_data,
						struct oplus_chg_track_status *track_status)
{
	int index = 0;
	char cool_down_pack[OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN] = { 0 };

	if (chip == NULL || p_trigger_data == NULL || track_status == NULL)
		return;

	pr_debug("start\n");
	memset(p_trigger_data->crux_info, 0, sizeof(p_trigger_data->crux_info));
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$power_mode@@%s", track_status->power_info.power_mode);

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRE) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$adapter_t@@%s", track_status->power_info.wired_info.adapter_type);
		if (track_status->power_info.wired_info.adapter_id)
			index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "$$adapter_id@@0x%x", track_status->power_info.wired_info.adapter_id);
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$power@@%d", track_status->power_info.wired_info.power);
	} else if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$adapter_t@@%s", track_status->power_info.wls_info.adapter_type);
		if (strlen(track_status->power_info.wls_info.dock_type))
			index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "$$dock_type@@%s", track_status->power_info.wls_info.dock_type);
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$power@@%d", track_status->power_info.wls_info.power);
	}

	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$start_soc@@%d",
			  track_status->chg_start_soc);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$end_soc@@%d",
			  track_status->chg_end_soc);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$start_temp@@%d", track_status->chg_start_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$max_temp@@%d",
			  track_status->chg_max_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_start_temp@@%d", track_status->batt_start_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_max_temp@@%d", track_status->batt_max_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_max_vol@@%d", track_status->batt_max_vol);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_max_curr@@%d", track_status->batt_max_curr);

	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$ledon_time@@%d", track_status->continue_ledon_time);
	if (track_status->ledon_ave_speed)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$ledon_ave_speed@@%d", track_status->ledon_ave_speed);

	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$ledoff_time@@%d", track_status->continue_ledoff_time);
	if (track_status->ledoff_ave_speed)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$ledoff_ave_speed@@%d", track_status->ledoff_ave_speed);

	if (track_status->chg_five_mins_cap != TRACK_PERIOD_CHG_CAP_INIT)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_five_mins_cap@@%d", track_status->chg_five_mins_cap);

	if (track_status->chg_ten_mins_cap != TRACK_PERIOD_CHG_CAP_INIT)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_ten_mins_cap@@%d", track_status->chg_ten_mins_cap);

	if (track_status->chg_average_speed != TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_average_speed@@%d", track_status->chg_average_speed);

	if (track_status->chg_fast_full_time)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$fast_full_time@@%d", track_status->chg_fast_full_time);

	if (track_status->chg_report_full_time)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$report_full_time@@%d", track_status->chg_report_full_time);

	if (track_status->chg_normal_full_time)
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$normal_full_time@@%d", track_status->chg_normal_full_time);

	if (strlen(track_status->batt_full_reason))
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$full_reason@@%s", track_status->batt_full_reason);

	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$chg_warm_once@@%d", track_status->tbatt_warm_once);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$batt_fcc@@%d",
			  chip->batt_fcc);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$batt_soh@@%d",
			  chip->batt_soh);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$batt_cc@@%d",
			  chip->batt_cc);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$rechg_counts@@%d", track_status->rechg_counts);

	if (strlen(track_status->chg_abnormal_reason))
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_abnormal@@%s", track_status->chg_abnormal_reason);

	oplus_chg_track_pack_cool_down_stats(track_status, cool_down_pack);
	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$cool_down_sta@@%s", cool_down_pack);

	if (strlen(track_status->bcc_info->data_buf)) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$bcc_trig_sta@@%s", track_status->bcc_info->data_buf);
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$bcc_code@@0x%x", track_status->bcc_info->err_code);
	}

	if (strlen(track_status->bms_info)) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$bms_sta@@%s", track_status->bms_info);
	}

	if (track_status->hyper_en == 1) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$hyper_sta@@%s", track_status->hyper_info);
	} else {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$hyper_sta@@hyper_en=%d", track_status->hyper_en);
	}

	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$plugin_utc_t@@%d", track_status->chg_plugin_utc_t);

	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$plugout_utc_t@@%d", track_status->chg_plugout_utc_t);
	if (track_status->chg_ffc_time) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$ffc_cv_status@@"
				  "%d,%d,%d,%d,%d,%d",
				  track_status->chg_ffc_time, track_status->ffc_start_main_soc,
				  track_status->ffc_start_sub_soc, track_status->ffc_end_main_soc,
				  track_status->ffc_end_sub_soc, track_status->chg_cv_time);
	}

	if (track_status->aging_ffc_trig) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$aging_ffc@@%d,%d,%d", track_status->aging_ffc_trig,
				  track_status->aging_ffc1_judge_vol, track_status->aging_ffc2_judge_vol);
		if (track_status->aging_ffc1_to_full_time) {
			index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "$$aging_ffc_t@@%d", track_status->aging_ffc1_to_full_time);
		}
	}

	index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$mmi_chg@@%d",
			  track_status->once_mmi_chg);

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$chg_cycle_status@@%d", track_status->once_chg_cycle_status);

	oplus_chg_track_record_general_info(chip, track_status, p_trigger_data, index);
}

static void oplus_chg_track_charger_info_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, charger_info_trigger_work);

	if (!chip)
		return;

	chip->track_status.wls_need_upload = false;
	chip->track_status.wls_need_upload = false;
	oplus_chg_track_upload_trigger_data(chip->charger_info_trigger);
}

static void oplus_chg_track_no_charging_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, no_charging_trigger_work);

	if (!chip)
		return;

	chip->track_status.wls_need_upload = false;
	chip->track_status.wls_need_upload = false;
	oplus_chg_track_upload_trigger_data(chip->no_charging_trigger);
}

static void oplus_chg_track_slow_charging_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, slow_charging_trigger_work);

	if (!chip)
		return;

	chip->track_status.wls_need_upload = false;
	chip->track_status.wls_need_upload = false;
	oplus_chg_track_upload_trigger_data(chip->slow_charging_trigger);
}

static void oplus_chg_track_charging_break_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, charging_break_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->charging_break_trigger);
}

static void oplus_chg_track_wls_charging_break_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, wls_charging_break_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->wls_charging_break_trigger);
}

static void oplus_chg_track_cal_chg_five_mins_capacity_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, cal_chg_five_mins_capacity_work);
	struct oplus_chg_chip *charger_chip = oplus_chg_get_chg_struct();

	if (!chip || !charger_chip)
		return;

	chip->track_status.chg_five_mins_cap = charger_chip->soc - chip->track_status.chg_start_soc;
	pr_debug("chg_five_mins_soc:%d, start_chg_soc:%d, "
		"chg_five_mins_cap:%d\n",
		charger_chip->soc, chip->track_status.chg_start_soc, chip->track_status.chg_five_mins_cap);
}

static void oplus_chg_track_cal_chg_ten_mins_capacity_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, cal_chg_ten_mins_capacity_work);
	struct oplus_chg_chip *charger_chip = oplus_chg_get_chg_struct();

	if (!chip || !charger_chip)
		return;

	chip->track_status.chg_ten_mins_cap = charger_chip->soc - chip->track_status.chg_start_soc;
	pr_debug("chg_ten_mins_soc:%d, start_chg_soc:%d, chg_ten_mins_cap:%d\n", charger_chip->soc,
		chip->track_status.chg_start_soc, chip->track_status.chg_ten_mins_cap);
}

static int oplus_chg_track_speed_ref_init(struct oplus_chg_track *chip)
{
	if (!chip)
		return -1;

	if (chip->track_cfg.wired_fast_chg_scheme >= 0 &&
	    chip->track_cfg.wired_fast_chg_scheme < ARRAY_SIZE(g_wired_speed_ref_standard))
		chip->track_status.wired_speed_ref = g_wired_speed_ref_standard[chip->track_cfg.wired_fast_chg_scheme];

	if (chip->track_cfg.wls_fast_chg_scheme >= 0 &&
	    chip->track_cfg.wls_fast_chg_scheme < ARRAY_SIZE(g_wls_fast_speed_ref_standard))
		chip->track_status.wls_airvooc_speed_ref =
			g_wls_fast_speed_ref_standard[chip->track_cfg.wls_fast_chg_scheme];

	if (chip->track_cfg.wls_epp_chg_scheme >= 0 &&
	    chip->track_cfg.wls_epp_chg_scheme < ARRAY_SIZE(g_wls_epp_speed_ref_standard))
		chip->track_status.wls_epp_speed_ref = g_wls_epp_speed_ref_standard[chip->track_cfg.wls_epp_chg_scheme];

	if (chip->track_cfg.wls_bpp_chg_scheme >= 0 &&
	    chip->track_cfg.wls_bpp_chg_scheme < ARRAY_SIZE(g_wls_bpp_speed_ref_standard))
		chip->track_status.wls_bpp_speed_ref = g_wls_bpp_speed_ref_standard[chip->track_cfg.wls_bpp_chg_scheme];

	return 0;
}

static void oplus_chg_track_check_wired_online_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	struct oplus_chg_track_status *track_status;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *g_chip = container_of(dwork, struct oplus_chg_track, check_wired_online_work);

	if (!g_chip || !chip)
		return;

	track_status = &(g_chip->track_status);
	mutex_lock(&g_chip->online_hold_lock);
	if (track_status->wired_online_check_stop) {
		pr_debug("!!!online check_stop. should return\n");
		mutex_unlock(&g_chip->online_hold_lock);
		return;
	}

	if (track_status->wired_online) {
		if (!(oplus_vooc_get_fastchg_started() || oplus_vooc_get_fastchg_to_normal() ||
		      oplus_vooc_get_fastchg_to_warm() || oplus_vooc_get_fastchg_dummy_started() ||
		      (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
		      oplus_vooc_get_btb_temp_over() || (!chip->mmi_fastchg) || chip->chg_ops->check_chrdet_status())) {
			track_status->wired_online = 0;
			track_status->wired_online_check_count = 0;
			pr_debug("!!!break. wired_online:%d\n", track_status->wired_online);
		}
	} else {
		track_status->wired_online_check_count = 0;
	}

	if (track_status->wired_online_check_count--)
		schedule_delayed_work(&g_chip->check_wired_online_work, msecs_to_jiffies(TRACK_ONLINE_CHECK_T_MS));
	mutex_unlock(&g_chip->online_hold_lock);

	pr_debug("online:%d, online_check_count:%d\n", track_status->wired_online,
		track_status->wired_online_check_count);
}

static int oplus_chg_track_init(struct oplus_chg_track *track_dev)
{
	int ret = 0;
	struct oplus_chg_track *chip = track_dev;

	chip->trigger_data_ok = false;
	mutex_init(&chip->upload_lock);
	mutex_init(&chip->trigger_data_lock);
	mutex_init(&chip->trigger_ack_lock);
	init_waitqueue_head(&chip->upload_wq);
	init_completion(&chip->trigger_ack);
	mutex_init(&track_dev->dcs_info_lock);
	mutex_init(&chip->access_lock);

	mutex_init(&chip->adsp_upload_lock);
	init_waitqueue_head(&chip->adsp_upload_wq);
	mutex_init(&chip->online_hold_lock);
	chip->track_status.curr_soc = -EINVAL;
	chip->track_status.curr_uisoc = -EINVAL;
	chip->track_status.pre_soc = -EINVAL;
	chip->track_status.pre_uisoc = -EINVAL;
	chip->track_status.soc_jumped = false;
	chip->track_status.uisoc_jumped = false;
	chip->track_status.uisoc_to_soc_jumped = false;
	chip->track_status.uisoc_load_jumped = false;
	chip->track_status.debug_soc = OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID;
	chip->track_status.debug_uisoc = OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID;
	chip->uisoc_load_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->uisoc_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_UI_SOC_LOAD_JUMP;
	chip->soc_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->soc_trigger.flag_reason = TRACK_NOTIFY_FLAG_SOC_JUMP;
	chip->uisoc_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->uisoc_trigger.flag_reason = TRACK_NOTIFY_FLAG_UI_SOC_JUMP;
	chip->uisoc_to_soc_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->uisoc_to_soc_trigger.flag_reason = TRACK_NOTIFY_FLAG_UI_SOC_TO_SOC_JUMP;
	chip->charger_info_trigger.type_reason = TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->charger_info_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHARGER_INFO;
	chip->no_charging_trigger.type_reason = TRACK_NOTIFY_TYPE_NO_CHARGING;
	chip->no_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_NO_CHARGING;
	chip->slow_charging_trigger.type_reason = TRACK_NOTIFY_TYPE_CHARGING_SLOW;
	chip->charging_break_trigger.type_reason = TRACK_NOTIFY_TYPE_CHARGING_BREAK;
	chip->wls_charging_break_trigger.type_reason = TRACK_NOTIFY_TYPE_CHARGING_BREAK;

	memset(&(chip->track_status.power_info), 0, sizeof(chip->track_status.power_info));
	strcpy(chip->track_status.power_info.power_mode, "unknow");
	chip->track_status.chg_no_charging_cnt = 0;
	chip->track_status.chg_total_cnt = 0;
	chip->track_status.chg_max_temp = 0;
	chip->track_status.ffc_start_time = 0;
	chip->track_status.cv_start_time = 0;
	chip->track_status.chg_ffc_time = 0;
	chip->track_status.chg_cv_time = 0;
	chip->track_status.ffc_start_main_soc = 0;
	chip->track_status.ffc_start_sub_soc = 0;
	chip->track_status.ffc_end_main_soc = 0;
	chip->track_status.ffc_end_sub_soc = 0;
	chip->track_status.chg_fast_full_time = 0;
	chip->track_status.chg_normal_full_time = 0;
	chip->track_status.chg_report_full_time = 0;
	chip->track_status.aging_ffc_trig = false;
	chip->track_status.aging_ffc1_judge_vol = 0;
	chip->track_status.aging_ffc2_judge_vol = 0;
	chip->track_status.aging_ffc1_start_time = 0;
	chip->track_status.aging_ffc1_to_full_time = 0;
	chip->track_status.debug_normal_charging_state = CHARGING_STATUS_CCCV;
	chip->track_status.debug_fast_prop_status = TRACK_FASTCHG_STATUS_UNKOWN;
	chip->track_status.debug_normal_prop_status = POWER_SUPPLY_STATUS_UNKNOWN;
	chip->track_status.debug_no_charging = 0;
	chip->track_status.chg_five_mins_cap = TRACK_PERIOD_CHG_CAP_INIT;
	chip->track_status.chg_ten_mins_cap = TRACK_PERIOD_CHG_CAP_INIT;
	chip->track_status.chg_average_speed = TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT;
	chip->track_status.chg_attach_time_ms = chip->track_cfg.fast_chg_break_t_thd;
	chip->track_status.chg_detach_time_ms = 0;
	chip->track_status.wls_attach_time_ms = chip->track_cfg.wls_chg_break_t_thd;
	chip->track_status.wls_detach_time_ms = 0;
	chip->track_status.soc_sect_status = TRACK_SOC_SECTION_DEFAULT;
	chip->track_status.chg_speed_is_slow = false;
	chip->track_status.tbatt_warm_once = false;
	chip->track_status.tbatt_cold_once = false;
	chip->track_status.cool_down_effect_cnt = 0;
	chip->track_status.wls_need_upload = false;
	chip->track_status.wired_need_upload = false;
	chip->track_status.real_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->track_status.charger_type_backup = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->track_status.once_mmi_chg = false;
	chip->track_status.once_chg_cycle_status = CHG_CYCLE_VOTER__NONE;
	chip->track_status.hyper_en = 0;

	memset(&(chip->track_status.fastchg_break_info), 0, sizeof(chip->track_status.fastchg_break_info));
	memset(chip->wired_break_crux_info, 0, sizeof(chip->wired_break_crux_info));
	memset(chip->wls_break_crux_info, 0, sizeof(chip->wls_break_crux_info));

	memset(chip->track_status.batt_full_reason, 0, sizeof(chip->track_status.batt_full_reason));
	oplus_chg_track_clear_cool_down_stats_time(&(chip->track_status));

	memset(chip->voocphy_name, 0, sizeof(chip->voocphy_name));
	oplus_chg_track_set_voocphy_name(chip);
	oplus_chg_track_speed_ref_init(chip);

	INIT_DELAYED_WORK(&chip->uisoc_load_trigger_work, oplus_chg_track_uisoc_load_trigger_work);
	INIT_DELAYED_WORK(&chip->soc_trigger_work, oplus_chg_track_soc_trigger_work);
	INIT_DELAYED_WORK(&chip->uisoc_trigger_work, oplus_chg_track_uisoc_trigger_work);
	INIT_DELAYED_WORK(&chip->uisoc_to_soc_trigger_work, oplus_chg_track_uisoc_to_soc_trigger_work);
	INIT_DELAYED_WORK(&chip->charger_info_trigger_work, oplus_chg_track_charger_info_trigger_work);
	INIT_DELAYED_WORK(&chip->cal_chg_five_mins_capacity_work, oplus_chg_track_cal_chg_five_mins_capacity_work);
	INIT_DELAYED_WORK(&chip->cal_chg_ten_mins_capacity_work, oplus_chg_track_cal_chg_ten_mins_capacity_work);
	INIT_DELAYED_WORK(&chip->no_charging_trigger_work, oplus_chg_track_no_charging_trigger_work);
	INIT_DELAYED_WORK(&chip->slow_charging_trigger_work, oplus_chg_track_slow_charging_trigger_work);
	INIT_DELAYED_WORK(&chip->charging_break_trigger_work, oplus_chg_track_charging_break_trigger_work);
	INIT_DELAYED_WORK(&chip->wls_charging_break_trigger_work, oplus_chg_track_wls_charging_break_trigger_work);
	INIT_DELAYED_WORK(&chip->check_wired_online_work, oplus_chg_track_check_wired_online_work);
	return ret;
}

static bool oplus_chg_track_trigger_data_is_valid(oplus_chg_track_trigger *pdata)
{
	int i;
	int len;
	bool ret = false;
	int type_reason = pdata->type_reason;
	int flag_reason = pdata->flag_reason;

	len = strlen(pdata->crux_info);
	if (!len) {
		pr_err("crux_info lens is invaild\n");
		return ret;
	}

	switch (type_reason) {
	case TRACK_NOTIFY_TYPE_SOC_JUMP:
		for (i = TRACK_NOTIFY_FLAG_UI_SOC_LOAD_JUMP; i <= TRACK_NOTIFY_FLAG_UI_SOC_TO_SOC_JUMP; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_GENERAL_RECORD:
		for (i = TRACK_NOTIFY_FLAG_CHARGER_INFO; i <= TRACK_NOTIFY_FLAG_WLS_TRX_INFO; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_NO_CHARGING:
		for (i = TRACK_NOTIFY_FLAG_NO_CHARGING; i <= TRACK_NOTIFY_FLAG_NO_CHARGING; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_CHARGING_SLOW:
		for (i = TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM; i <= TRACK_NOTIFY_FLAG_CHG_SLOW_OTHER; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_CHARGING_BREAK:
		for (i = TRACK_NOTIFY_FLAG_FAST_CHARGING_BREAK; i <= TRACK_NOTIFY_FLAG_WLS_CHARGING_BREAK; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL:
		for (i = TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL; i <= TRACK_NOTIFY_FLAG_MOS_ERROR_ABNORMAL; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL:
		for (i = TRACK_NOTIFY_FLAG_UFCS_ABNORMAL; i < TRACK_NOTIFY_FLAG_MAX_CNT; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	default:
		ret = false;
		break;
	}

	if (!ret)
		pr_err("type_reason or flag_reason is invaild\n");
	return ret;
}

int oplus_chg_track_upload_trigger_data(oplus_chg_track_trigger data)
{
	int rc;
	struct oplus_chg_track *chip = g_track_chip;

	if (!g_track_chip)
		return TRACK_CMD_ERROR_CHIP_NULL;

	if (!oplus_chg_track_trigger_data_is_valid(&data))
		return TRACK_CMD_ERROR_DATA_INVALID;

	pr_debug("start\n");
	mutex_lock(&chip->trigger_ack_lock);
	mutex_lock(&chip->trigger_data_lock);
	memset(&chip->trigger_data, 0, sizeof(oplus_chg_track_trigger));
	chip->trigger_data.type_reason = data.type_reason;
	chip->trigger_data.flag_reason = data.flag_reason;
	strncpy(chip->trigger_data.crux_info, data.crux_info, OPLUS_CHG_TRACK_CURX_INFO_LEN - 1);
	pr_debug("type_reason:%d, flag_reason:%d, crux_info[%s]\n", chip->trigger_data.type_reason,
		chip->trigger_data.flag_reason, chip->trigger_data.crux_info);
	chip->trigger_data_ok = true;
	mutex_unlock(&chip->trigger_data_lock);
	reinit_completion(&chip->trigger_ack);
	wake_up(&chip->upload_wq);

	rc = wait_for_completion_timeout(&chip->trigger_ack, msecs_to_jiffies(OPLUS_CHG_TRACK_WAIT_TIME_MS));
	if (!rc) {
		if (delayed_work_pending(&chip->upload_info_dwork))
			cancel_delayed_work_sync(&chip->upload_info_dwork);
		pr_err("Error, timed out upload trigger data\n");
		mutex_unlock(&chip->trigger_ack_lock);
		return TRACK_CMD_ERROR_TIME_OUT;
	}
	rc = 0;
	pr_debug("success\n");
	mutex_unlock(&chip->trigger_ack_lock);

	return rc;
}

static int oplus_chg_track_thread(void *data)
{
	int rc = 0;
	struct oplus_chg_track *chip = (struct oplus_chg_track *)data;

	pr_debug("start\n");
	while (!kthread_should_stop()) {
		mutex_lock(&chip->upload_lock);
		rc = wait_event_interruptible(chip->upload_wq, chip->trigger_data_ok);
		mutex_unlock(&chip->upload_lock);
		if (rc)
			return rc;
		if (!chip->trigger_data_ok)
			pr_err("oplus chg false wakeup, rc=%d\n", rc);
		mutex_lock(&chip->trigger_data_lock);
		chip->trigger_data_ok = false;
		chip->dwork_retry_cnt = OPLUS_CHG_TRACK_DWORK_RETRY_CNT;
		queue_delayed_work(chip->trigger_upload_wq, &chip->upload_info_dwork, 0);
		mutex_unlock(&chip->trigger_data_lock);
	}

	return rc;
}

static int oplus_chg_track_thread_init(struct oplus_chg_track *track_dev)
{
	int rc = 0;
	struct oplus_chg_track *chip = track_dev;

	chip->track_upload_kthread = kthread_run(oplus_chg_track_thread, chip, "track_upload_kthread");
	if (IS_ERR(chip->track_upload_kthread)) {
		pr_err("failed to create oplus_chg_track_thread\n");
		return -EINVAL;
	}

	return rc;
}

int __attribute__((weak)) oplus_chg_track_get_adsp_debug(void)
{
	return 0;
}

void __attribute__((weak)) oplus_chg_track_set_adsp_debug(u32 val)
{
}

static int adsp_track_debug_get(void *data, u64 *val)
{
	*val = oplus_chg_track_get_adsp_debug();

	return 0;
}

static int adsp_track_debug_set(void *data, u64 val)
{
	int mid_val = 0;

	if (val > ADSP_TRACK_DEBUG_MAX_ERR)
		return -EINVAL;
	mid_val = (u32)val;
	oplus_chg_track_set_adsp_debug(mid_val);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adsp_track_debug_ops, adsp_track_debug_get, adsp_track_debug_set, "%lld\n");

int oplus_chg_track_handle_adsp_info(u8 *crux_info, int len)
{
	int count;
	struct oplus_chg_track *chip = g_track_chip;

	if (!crux_info || !chip)
		return -EINVAL;

	if (len != sizeof(adsp_track_trigger)) {
		pr_err("len is invalid, len[%d], standard[%d]\n", len, sizeof(adsp_track_trigger));
		return -EINVAL;
	}

	count = kfifo_in_spinlocked(&chip->adsp_fifo, crux_info, sizeof(adsp_track_trigger), &adsp_fifo_lock);
	if (count != sizeof(adsp_track_trigger)) {
		pr_err("kfifo in error\n");
		return -EINVAL;
	}

	wake_up(&chip->adsp_upload_wq);
	return 0;
}

static int oplus_chg_adsp_track_thread(void *data)
{
	int index;
	int count;
	int rc;
	adsp_track_trigger *adsp_data;
	oplus_chg_track_trigger *ap_data;
	struct oplus_chg_track *chip = (struct oplus_chg_track *)data;

	adsp_data = kmalloc(sizeof(adsp_track_trigger), GFP_KERNEL);
	if (adsp_data == NULL) {
		pr_err("alloc ap_data or adsp_data buf error\n");
		return -ENOMEM;
	}
	ap_data = kmalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (ap_data == NULL) {
		kfree(adsp_data);
		pr_err("alloc ap_data or adsp_data buf error\n");
		return -ENOMEM;
	}
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	pr_debug("start\n");
	while (!kthread_should_stop()) {
		mutex_lock(&chip->adsp_upload_lock);
		rc = wait_event_interruptible(chip->adsp_upload_wq, !kfifo_is_empty(&chip->adsp_fifo));
		mutex_unlock(&chip->adsp_upload_lock);
		if (rc) {
			kfree(adsp_data);
			kfree(ap_data);
			return rc;
		}
		count = kfifo_out_spinlocked(&chip->adsp_fifo, adsp_data, sizeof(adsp_track_trigger), &adsp_fifo_lock);
		if (count != sizeof(adsp_track_trigger)) {
			pr_err("adsp_data size is error\n");
			kfree(adsp_data);
			kfree(ap_data);
			return rc;
		}

		if (oplus_chg_track_adsp_to_ap_type_mapping(adsp_data->adsp_type_reason, &(ap_data->type_reason)) >=
			    0 &&
		    oplus_chg_track_adsp_to_ap_flag_mapping(adsp_data->adsp_flag_reason, &(ap_data->flag_reason)) >=
			    0) {
			index = 0;
			memset(ap_data->crux_info, 0, sizeof(ap_data->crux_info));
			index += snprintf(&(ap_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
					  adsp_data->adsp_crux_info);
			oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
			index += snprintf(&(ap_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
					  chip->chg_power_info);
			oplus_chg_track_upload_trigger_data(*ap_data);
		}
		pr_debug("crux_info:%s\n", adsp_data->adsp_crux_info);
	}

	kfree(adsp_data);
	kfree(ap_data);
	return rc;
}

static int oplus_chg_adsp_track_thread_init(struct oplus_chg_track *track_dev)
{
	int rc = 0;
	struct oplus_chg_track *chip = track_dev;

	chip->adsp_track_upload_kthread = kthread_run(oplus_chg_adsp_track_thread, chip, "adsp_track_upload_kthread");
	if (IS_ERR(chip->adsp_track_upload_kthread)) {
		pr_err("failed to create oplus_chg_adsp_track_thread\n");
		return -1;
	}

	return rc;
}

static int oplus_chg_track_get_current_time_s(struct rtc_time *tm)
{
	struct timespec ts;

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, tm);
	tm->tm_year = tm->tm_year + TRACK_UTC_BASE_TIME;
	tm->tm_mon = tm->tm_mon + 1;
	return ts.tv_sec;
}

static int oplus_chg_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_debug("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

/*
* track sub version
* 3: default version for chg track
* 3.1: add for solve the problem of incorrect PPS records and power mode record error code
* 3.2: add for solve the problem of adapter_t symbol NULL
* 3.3: break records and mmi_chg and fastchg_to_normal and soc jump optimize
* 3.4: add app record track feature, smart chg feature, wls third feature
*/

static void oplus_chg_track_upload_info_dwork(struct work_struct *work)
{
	int ret = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(dwork, struct oplus_chg_track, upload_info_dwork);

	if (!chip)
		return;

	mutex_lock(&chip->dcs_info_lock);
	mutex_unlock(&chip->dcs_info_lock);
	if (!ret)
		complete(&chip->trigger_ack);
	else if (chip->dwork_retry_cnt > 0)
		queue_delayed_work(chip->trigger_upload_wq, &chip->upload_info_dwork,
				   msecs_to_jiffies(OPLUS_CHG_UPDATE_INFO_DELAY_MS));

	pr_debug("retry_cnt: %d, ret = %d\n", chip->dwork_retry_cnt, ret);
	chip->dwork_retry_cnt--;
}

static int oplus_chg_track_handle_wired_type_info(struct oplus_chg_chip *chip,
						  struct oplus_chg_track_status *track_status, int type)
{
	int charger_type = 0;

	if (track_status->power_info.wired_info.adapter_id) {
		pr_debug("has know type and not handle\n");
		return charger_type;
	}

	if (strlen(track_status->power_info.wired_info.adapter_type) &&
	    strcmp(track_status->power_info.wired_info.adapter_type, "dcp") &&
	    strcmp(track_status->power_info.wired_info.adapter_type, "unknow")) {
		if (!((type == TRACK_CHG_GET_THTS_TIME_TYPE && oplus_vooc_get_fast_chg_type()) ||
		      (type == TRACK_CHG_GET_LAST_TIME_TYPE && track_status->pre_fastchg_type))) {
			pr_debug("non dcp or unknow or svooc or ufcs, not handle\n");
			return charger_type;
		}
	}

	track_status->power_info.power_type = TRACK_CHG_TYPE_WIRE;
	memset(track_status->power_info.power_mode, 0, sizeof(track_status->power_info.power_mode));
	strcpy(track_status->power_info.power_mode, "wired");

	/* first handle base type */
	oplus_chg_track_get_base_type_info(
		(chip->charger_type != POWER_SUPPLY_TYPE_UNKNOWN) ? chip->charger_type : track_status->charger_type_backup,
		track_status);

	/* second handle enhance type */
	if (chip->chg_ops->get_charger_subtype) {
		if (type == TRACK_CHG_GET_THTS_TIME_TYPE) {
			charger_type = chip->chg_ops->get_charger_subtype();
			track_status->pre_chg_subtype = charger_type;
			oplus_chg_track_get_enhance_type_info(charger_type, track_status);
		} else {
			oplus_chg_track_get_enhance_type_info(track_status->pre_chg_subtype, track_status);
		}
	}

	if (type == TRACK_CHG_GET_THTS_TIME_TYPE) {
		charger_type = oplus_ufcs_get_fastchg_type();
		if (charger_type) {
			track_status->pre_fastchg_type = charger_type;
			oplus_chg_track_get_ufcs_type_info(charger_type, track_status);
		}
	} else {
		if (track_status->pre_fastchg_type > OPLUS_UFCS_ID_MIN) {
			charger_type = track_status->pre_fastchg_type;
			oplus_chg_track_get_ufcs_type_info(charger_type, track_status);
		}
	}

	/* final handle fastchg type */
	if (type == TRACK_CHG_GET_THTS_TIME_TYPE) {
		track_status->fast_chg_type = oplus_vooc_get_fast_chg_type();
		oplus_chg_track_get_vooc_type_info(track_status->fast_chg_type, track_status);
	} else {
		oplus_chg_track_get_vooc_type_info(track_status->pre_fastchg_type, track_status);
	}

	if (!strcmp(track_status->power_info.wired_info.adapter_type, "pps"))
		track_status->power_info.wired_info.power = oplus_pps_get_power()*1000;

	pr_debug("power_mode:%s, type:%s, adapter_id:0x%0x, power:%d\n", track_status->power_info.power_mode,
		track_status->power_info.wired_info.adapter_type, track_status->power_info.wired_info.adapter_id,
		track_status->power_info.wired_info.power);

	return charger_type;
}

static int oplus_chg_track_handle_wls_type_info(struct oplus_chg_track_status *track_status)
{
	int adapter_type;
	int dock_type;
	int max_wls_power;
	struct oplus_chg_track *chip = g_track_chip;
	int rc;
	union oplus_chg_mod_propval pval;

	track_status->power_info.power_type = TRACK_CHG_TYPE_WIRELESS;
	memset(track_status->power_info.power_mode, 0, sizeof(track_status->power_info.power_mode));
	strcpy(track_status->power_info.power_mode, "wireless");

	adapter_type = oplus_wpc_get_adapter_type();
	dock_type = oplus_wpc_get_dock_type();
	max_wls_power = oplus_wpc_get_max_wireless_power();

	if (is_wls_ocm_available(chip)) {
		rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &pval);
		if (rc == 0)
			adapter_type = pval.intval;

		rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_DOCK_TYPE, &pval);
		if (rc == 0)
			dock_type = pval.intval;

		rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_RX_MAX_POWER, &pval);
		if (rc == 0)
			max_wls_power = pval.intval;
	}

	oplus_chg_track_get_wls_adapter_type_info(adapter_type, track_status);
	oplus_chg_track_get_wls_dock_type_info(dock_type, track_status);
	if (max_wls_power)
		track_status->power_info.wls_info.power = max_wls_power;

	pr_debug("power_mode:%s, type:%s, dock_type:%s, power:%d\n", track_status->power_info.power_mode,
		track_status->power_info.wls_info.adapter_type, track_status->power_info.wls_info.dock_type,
		track_status->power_info.wls_info.power);

	return 0;
}

static int oplus_chg_track_handle_batt_full_reason(struct oplus_chg_chip *chip,
						   struct oplus_chg_track_status *track_status)
{
	int notify_flag = NOTIFY_BAT_FULL;

	if (chip == NULL || track_status == NULL)
		return -EINVAL;

	if (chip->notify_flag == NOTIFY_BAT_FULL || chip->notify_flag == NOTIFY_BAT_FULL_PRE_LOW_TEMP ||
	    chip->notify_flag == NOTIFY_BAT_FULL_PRE_HIGH_TEMP || chip->notify_flag == NOTIFY_BAT_NOT_CONNECT ||
	    chip->notify_flag == NOTIFY_BAT_FULL_THIRD_BATTERY)
		notify_flag = chip->notify_flag;

	if (track_status->debug_chg_notify_flag)
		notify_flag = track_status->debug_chg_notify_flag;
	if (!strlen(track_status->batt_full_reason))
		oplus_chg_track_get_batt_full_reason_info(notify_flag, track_status);
	pr_debug("track_notify_flag:%d, chager_notify_flag:%d, "
		"full_reason[%s]\n",
		notify_flag, chip->notify_flag, track_status->batt_full_reason);

	return 0;
}

static int oplus_chg_track_check_chg_abnormal(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)
{
	int notify_code;

	if (chip == NULL || track_status == NULL)
		return -EINVAL;

	if (track_status->debug_chg_notify_code)
		notify_code = track_status->debug_chg_notify_code;
	else
		notify_code = chip->notify_code;

	if (notify_code & (1 << NOTIFY_CHGING_OVERTIME))
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_CHGING_OVERTIME, track_status);

	if (notify_code & (1 << NOTIFY_CHARGER_OVER_VOL)) {
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_CHARGER_OVER_VOL, track_status);
	} else if (notify_code & (1 << NOTIFY_CHARGER_LOW_VOL)) {
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_CHARGER_LOW_VOL, track_status);
	}

	if (notify_code & (1 << NOTIFY_BAT_OVER_TEMP)) {
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_BAT_OVER_TEMP, track_status);
	} else if (notify_code & (1 << NOTIFY_BAT_LOW_TEMP)) {
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_BAT_LOW_TEMP, track_status);
	}

	if (notify_code & (1 << NOTIFY_BAT_OVER_VOL)) {
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_BAT_OVER_VOL, track_status);
	}

	if (notify_code & (1 << NOTIFY_BAT_NOT_CONNECT)) {
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_BAT_NOT_CONNECT, track_status);
	} else if (notify_code & (1 << NOTIFY_BAT_FULL_THIRD_BATTERY)) {
		oplus_chg_track_get_chg_abnormal_reason_info(NOTIFY_BAT_FULL_THIRD_BATTERY, track_status);
	}

	pr_debug("track_notify_code:0x%x, chager_notify_code:0x%x, "
		"abnormal_reason[%s]\n",
		notify_code, chip->notify_code, track_status->chg_abnormal_reason);

	return 0;
}

static int oplus_chg_track_cal_chg_common_mesg(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)
{
	struct oplus_chg_track *track_chip = g_track_chip;
	int rc;
	union oplus_chg_mod_propval pval;

	if (chip == NULL || track_status == NULL)
		return -EINVAL;

	if (chip->temperature > track_status->chg_max_temp)
		track_status->chg_max_temp = chip->temperature;

	if (oplus_get_report_batt_temp() > track_status->batt_max_temp)
		track_status->batt_max_temp = oplus_get_report_batt_temp();

	if (chip->icharging < track_status->batt_max_curr)
		track_status->batt_max_curr = chip->icharging;

	if (chip->batt_volt > track_status->batt_max_vol)
		track_status->batt_max_vol = chip->batt_volt;

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS && is_wls_ocm_available(track_chip)) {
		rc = oplus_chg_mod_get_property(track_chip->wls_ocm, OPLUS_CHG_PROP_WLS_SKEW_CURR, &pval);
		if (rc == 0 && chip->prop_status == POWER_SUPPLY_STATUS_CHARGING && pval.intval)
			track_status->wls_skew_effect_cnt += 1;
		rc = oplus_chg_mod_get_property(track_chip->wls_ocm, OPLUS_CHG_PROP_VERITY, &pval);
		if (rc == 0)
			track_status->chg_verity = pval.intval;
	} else if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		if (oplus_wpc_get_skewing_curr() && chip->prop_status == POWER_SUPPLY_STATUS_CHARGING)
			track_status->wls_skew_effect_cnt += 1;
		track_status->chg_verity = oplus_wpc_get_verity();
	}

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		pr_debug("wls_skew_cnt:%d, chg_verity:%d\n", track_status->wls_skew_effect_cnt,
			track_status->chg_verity);
	}

	if (!track_status->once_mmi_chg && !chip->mmi_chg)
		track_status->once_mmi_chg = true;

	if (!track_status->once_chg_cycle_status && chip->chg_cycle_status)
		track_status->once_chg_cycle_status = chip->chg_cycle_status;

	pr_debug("chg_max_temp:%d, batt_max_temp:%d, batt_max_curr:%d, "
		"batt_max_vol:%d, once_mmi_chg:%d, once_chg_cycle_status:%d\n",
		track_status->chg_max_temp, track_status->batt_max_temp,
		track_status->batt_max_curr, track_status->batt_max_vol,
		track_status->once_mmi_chg, track_status->once_chg_cycle_status);

	return 0;
}

static int oplus_chg_track_cal_cool_down_stats(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)
{
	int cool_down_max;
	int curr_time;
	bool chg_start;

	if (chip == NULL || track_status == NULL)
		return -EINVAL;

	cool_down_max = ARRAY_SIZE(cool_down_stats_table) - 1;
	if (chip->cool_down > cool_down_max || chip->cool_down < 0) {
		pr_err("cool_down is invalid\n");
		return -EINVAL;
	}

	if (chip->prop_status == POWER_SUPPLY_STATUS_CHARGING && chip->cool_down)
		track_status->cool_down_effect_cnt++;

	curr_time = oplus_chg_track_get_local_time_s();
	chg_start = (track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING &&
		     chip->prop_status == POWER_SUPPLY_STATUS_CHARGING);
	if (chg_start || (track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING)) {
		track_status->cool_down_status = chip->cool_down;
		track_status->cool_down_status_change_t = curr_time;
		return 0;
	}

	if (track_status->cool_down_status != chip->cool_down) {
		cool_down_stats_table[chip->cool_down].time += curr_time - track_status->cool_down_status_change_t;
	} else {
		cool_down_stats_table[track_status->cool_down_status].time +=
			curr_time - track_status->cool_down_status_change_t;
	}
	track_status->cool_down_status = chip->cool_down;
	track_status->cool_down_status_change_t = curr_time;

	pr_debug("cool_down_status:%d, cool_down_tatus_change_t:%d\n", track_status->cool_down_status,
		track_status->cool_down_status_change_t);

	return 0;
}

static int oplus_chg_track_cal_rechg_counts(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)

{
	if (chip == NULL || track_status == NULL)
		return -EINVAL;

	if (!track_status->in_rechging && chip->in_rechging)
		track_status->rechg_counts++;

	track_status->in_rechging = chip->in_rechging;
	return 0;
}

static int oplus_chg_track_cal_no_charging_stats(struct oplus_chg_chip *chip,
						 struct oplus_chg_track_status *track_status)
{
	if (chip == NULL || track_status == NULL)
		return -EINVAL;

	if (chip->prop_status == POWER_SUPPLY_STATUS_CHARGING) {
		track_status->chg_total_cnt++;
		if (oplus_switching_support_parallel_chg()) {
			if ((chip->icharging + chip->sub_batt_icharging) > 0)
				track_status->chg_no_charging_cnt++;
		} else {
			if (chip->icharging > 0)
				track_status->chg_no_charging_cnt++;
		}
	}

	return 0;
}

static int oplus_chg_track_cal_ledon_ledoff_average_speed(struct oplus_chg_track_status *track_status)
{
	if (track_status->led_onoff_power_cal)
		return 0;

	if (!track_status->ledon_ave_speed) {
		if (!track_status->ledon_time)
			track_status->ledon_ave_speed = 0;
		else
			track_status->ledon_ave_speed =
				TRACK_TIME_1MIN_THD * track_status->ledon_rm / track_status->ledon_time;
	}

	if (!track_status->ledoff_ave_speed) {
		if (!track_status->ledoff_time)
			track_status->ledoff_ave_speed = 0;
		else
			track_status->ledoff_ave_speed =
				TRACK_TIME_1MIN_THD * track_status->ledoff_rm / track_status->ledoff_time;
	}

	track_status->led_onoff_power_cal = true;
	pr_debug("ledon_ave_speed:%d, ledoff_ave_speed:%d\n", track_status->ledon_ave_speed,
		track_status->ledoff_ave_speed);
	return 0;
}

static bool oplus_chg_track_charger_exist(struct oplus_chg_chip *chip)
{
	if (!chip || !g_track_chip)
		return false;

	if (chip->charger_exist)
		return true;

	if (g_track_chip->track_cfg.voocphy_type != TRACK_ADSP_VOOCPHY &&
	    (oplus_vooc_get_fastchg_started() || oplus_vooc_get_fastchg_to_normal() ||
	     oplus_vooc_get_fastchg_to_warm() || oplus_vooc_get_fastchg_dummy_started()))
		return true;

	return false;
}

static int oplus_chg_track_cal_led_on_stats(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)
{
	int curr_time;
	bool chg_start;
	bool chg_end;
	bool need_update = false;

	if (chip == NULL || track_status == NULL)
		return -EINVAL;

	curr_time = oplus_chg_track_get_local_time_s();
	chg_start = (track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING &&
		     chip->prop_status == POWER_SUPPLY_STATUS_CHARGING);
	chg_end = (track_status->prop_status == POWER_SUPPLY_STATUS_CHARGING &&
		   chip->prop_status != POWER_SUPPLY_STATUS_CHARGING);
	if (chg_start || (track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING)) {
		track_status->led_on = chip->led_on;
		track_status->led_change_t = curr_time;
		track_status->led_change_rm = chip->batt_rm;
		return 0;
	}

	if (chg_end || track_status->chg_fast_full_time || chip->soc >= TRACK_LED_MONITOR_SOC_POINT)
		need_update = true;

	if (track_status->led_on && (!chip->led_on || need_update)) {
		if (curr_time - track_status->led_change_t > TRACK_CYCLE_RECORDIING_TIME_2MIN) {
			track_status->ledon_rm += chip->batt_rm - track_status->led_change_rm;
			track_status->ledon_time += curr_time - track_status->led_change_t;
		}
		track_status->continue_ledon_time += curr_time - track_status->led_change_t;
		track_status->led_change_t = curr_time;
		track_status->led_change_rm = chip->batt_rm;
	} else if (!track_status->led_on && (chip->led_on || need_update)) {
		if (curr_time - track_status->led_change_t > TRACK_CYCLE_RECORDIING_TIME_2MIN) {
			track_status->ledoff_rm += chip->batt_rm - track_status->led_change_rm;
			track_status->ledoff_time += curr_time - track_status->led_change_t;
		}
		track_status->continue_ledoff_time += curr_time - track_status->led_change_t;
		track_status->led_change_t = curr_time;
		track_status->led_change_rm = chip->batt_rm;
	}
	track_status->led_on = chip->led_on;
	pr_debug("continue_ledoff_t:%d, continue_ledon_t:%d\n", track_status->continue_ledoff_time,
		track_status->continue_ledon_time);

	if (!oplus_chg_track_charger_exist(chip) || track_status->chg_report_full_time ||
	    track_status->chg_fast_full_time || chip->soc >= TRACK_LED_MONITOR_SOC_POINT)
		oplus_chg_track_cal_ledon_ledoff_average_speed(track_status);

	pr_debug("ch_t:%d, ch_rm:%d, ledoff_rm:%d, ledoff_t:%d, ledon_rm:%d, "
		"ledon_t:%d\n",
		track_status->led_change_t, track_status->led_change_rm, track_status->ledoff_rm,
		track_status->ledoff_time, track_status->ledon_rm, track_status->ledon_time);
	return 0;
}

static bool oplus_chg_track_is_no_charging(struct oplus_chg_track_status *track_status)
{
	bool ret = false;
	char wired_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
	char wls_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];

	if (track_status == NULL)
		return ret;

	if (!track_status->chg_total_cnt)
		return ret;

	if ((track_status->chg_no_charging_cnt * 100) / track_status->chg_total_cnt > TRACK_NO_CHRGING_TIME_PCT)
		ret = true;

	memcpy(wired_adapter_type, track_status->power_info.wired_info.adapter_type, OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	memcpy(wls_adapter_type, track_status->power_info.wls_info.adapter_type, OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	pr_debug("wired_adapter_type:%s, wls_adapter_type:%s\n", wired_adapter_type, wls_adapter_type);
	if (!strcmp(wired_adapter_type, "unknow") || !strcmp(wired_adapter_type, "sdp") ||
	    !strcmp(wls_adapter_type, "unknow"))
		ret = false;

	if (track_status->debug_no_charging)
		ret = true;

	pr_debug("chg_no_charging_cnt:%d, chg_total_cnt:%d, "
		"debug_no_charging:%d, ret:%d",
		track_status->chg_no_charging_cnt, track_status->chg_total_cnt, track_status->debug_no_charging, ret);

	return ret;
}

static void oplus_chg_track_record_break_charging_info(struct oplus_chg_track *track_chip, struct oplus_chg_chip *chip,
						       struct oplus_chg_track_power power_info,
						       const char *sub_crux_info)
{
	int index = 0;
	struct oplus_chg_track_status *track_status;

	if (track_chip == NULL)
		return;

	pr_debug("start, type=%d\n", power_info.power_type);
	track_status = &(track_chip->track_status);

	if (power_info.power_type == TRACK_CHG_TYPE_WIRE) {
		memset(track_chip->charging_break_trigger.crux_info, 0,
		       sizeof(track_chip->charging_break_trigger.crux_info));
		index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$power_mode@@%s", power_info.power_mode);
		index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$adapter_t@@%s",
				  power_info.wired_info.adapter_type);
		if (power_info.wired_info.adapter_id)
			index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$adapter_id@@0x%x",
					  power_info.wired_info.adapter_id);
		index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$power@@%d", power_info.wired_info.power);

		index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$online@@%d", track_status->wired_online);
		if (strlen(track_status->fastchg_break_info.name)) {
			index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$voocphy_name@@%s",
					  track_chip->voocphy_name);
			index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$reason@@%s",
					  track_status->fastchg_break_info.name);
		}
		if (strlen(sub_crux_info)) {
			index += snprintf(&(track_chip->charging_break_trigger.crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$crux_info@@%s", sub_crux_info);
		}
		pr_debug("wired[%s]\n", track_chip->charging_break_trigger.crux_info);
	} else if (power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		memset(track_chip->wls_charging_break_trigger.crux_info, 0,
		       sizeof(track_chip->wls_charging_break_trigger.crux_info));
		index += snprintf(&(track_chip->wls_charging_break_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$power_mode@@%s", power_info.power_mode);
		index += snprintf(&(track_chip->wls_charging_break_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$adapter_t@@%s",
				  power_info.wls_info.adapter_type);
		if (strlen(power_info.wls_info.dock_type))
			index += snprintf(&(track_chip->wls_charging_break_trigger.crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$dock_type@@%s",
					  power_info.wls_info.dock_type);
		index += snprintf(&(track_chip->wls_charging_break_trigger.crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$power@@%d", power_info.wls_info.power);
		if (strlen(sub_crux_info)) {
			index += snprintf(&(track_chip->wls_charging_break_trigger.crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$crux_info@@%s", sub_crux_info);
		}
		oplus_chg_track_record_general_info(chip, track_status, &(track_chip->wls_charging_break_trigger),
						    index);
		pr_debug("wls[%s]\n", track_chip->wls_charging_break_trigger.crux_info);
	}
}

static int oplus_chg_track_check_fastchg_to_normal(struct oplus_chg_track *track_chip, int fastchg_break_code)
{
	struct oplus_chg_track_status *track_status;
	if (!track_chip)
		return -EINVAL;

	track_status = &track_chip->track_status;
	switch (track_chip->track_cfg.voocphy_type) {
	case TRACK_ADSP_VOOCPHY:
		if (fastchg_break_code == TRACK_ADSP_VOOCPHY_FULL)
			track_status->fastchg_to_normal = true;
		break;
	case TRACK_AP_SINGLE_CP_VOOCPHY:
	case TRACK_AP_DUAL_CP_VOOCPHY:
		if (fastchg_break_code == TRACK_CP_VOOCPHY_FULL)
			track_status->fastchg_to_normal = true;
		break;
	case TRACK_MCU_VOOCPHY:
		if (fastchg_break_code == TRACK_MCU_VOOCPHY_NORMAL_TEMP_FULL ||
		    fastchg_break_code == TRACK_MCU_VOOCPHY_LOW_TEMP_FULL)
			track_status->fastchg_to_normal = true;
		break;
	default:
		pr_debug("!!!voocphy type is error, should not go here\n");
		break;
	}

	pr_debug("!!!fastchg_to_normal: %d\n", track_status->fastchg_to_normal);
	return 0;
}

int oplus_chg_track_set_fastchg_break_code(int fastchg_break_code)
{
	int flv;
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!g_track_chip)
		return -EINVAL;

	pr_debug("oplus_chg_track_set_fastchg_break_code[%d]\n", fastchg_break_code);
	track_chip = g_track_chip;
	track_status = &track_chip->track_status;
	track_status->fastchg_break_info.code = fastchg_break_code;
	oplus_chg_track_check_fastchg_to_normal(track_chip, fastchg_break_code);
	if (oplus_vooc_get_fast_chg_type())
		track_status->pre_fastchg_type = oplus_vooc_get_fast_chg_type();
	pr_debug("fastchg_break_code[%d], pre_fastchg_type[0x%x]\n", fastchg_break_code, track_status->pre_fastchg_type);
	flv = oplus_chg_get_fv_when_vooc(chip);
	if ((track_status->pre_fastchg_type == CHARGER_SUBTYPE_FASTCHG_VOOC) && flv &&
	    (flv - TRACK_CHG_VOOC_BATT_VOL_DIFF_MV < chip->batt_volt)) {
		if (g_track_chip->track_cfg.voocphy_type == TRACK_ADSP_VOOCPHY) {
			if (fastchg_break_code == TRACK_ADSP_VOOCPHY_COMMU_TIME_OUT)
				track_status->fastchg_break_info.code = TRACK_ADSP_VOOCPHY_BREAK_DEFAULT;
		} else if (g_track_chip->track_cfg.voocphy_type == TRACK_AP_SINGLE_CP_VOOCPHY ||
			   g_track_chip->track_cfg.voocphy_type == TRACK_AP_DUAL_CP_VOOCPHY) {
			if (fastchg_break_code == TRACK_CP_VOOCPHY_COMMU_TIME_OUT)
				track_status->fastchg_break_info.code = TRACK_CP_VOOCPHY_BREAK_DEFAULT;
		} else if (g_track_chip->track_cfg.voocphy_type == TRACK_MCU_VOOCPHY) {
			if (fastchg_break_code == TRACK_MCU_VOOCPHY_FAST_ABSENT)
				track_status->fastchg_break_info.code = TRACK_CP_VOOCPHY_BREAK_DEFAULT;
		}
	}

	return 0;
}

static int oplus_chg_track_match_mcu_voocphy_break_reason(struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mcu_voocphy_break_table); i++) {
		if (mcu_voocphy_break_table[i].code == track_status->fastchg_break_info.code) {
			strncpy(track_status->fastchg_break_info.name, mcu_voocphy_break_table[i].name,
				OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN - 1);
			break;
		}
	}

	return 0;
}

static int oplus_chg_track_match_adsp_voocphy_break_reason(struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adsp_voocphy_break_table); i++) {
		if (adsp_voocphy_break_table[i].code == track_status->fastchg_break_info.code) {
			strncpy(track_status->fastchg_break_info.name, adsp_voocphy_break_table[i].name,
				OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN - 1);
			break;
		}
	}

	return 0;
}

static int oplus_chg_track_match_ap_voocphy_break_reason(struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ap_voocphy_break_table); i++) {
		if (ap_voocphy_break_table[i].code == track_status->fastchg_break_info.code) {
			strncpy(track_status->fastchg_break_info.name, ap_voocphy_break_table[i].name,
				OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN - 1);
			break;
		}
	}

	return 0;
}

static int oplus_chg_track_match_fastchg_break_reason(struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_status *track_status;

	if (!track_chip)
		return -EINVAL;
	track_status = &track_chip->track_status;

	pr_debug("voocphy_type:%d, code:0x%x\n", track_chip->track_cfg.voocphy_type,
		track_status->fastchg_break_info.code);
	switch (track_chip->track_cfg.voocphy_type) {
	case TRACK_ADSP_VOOCPHY:
		oplus_chg_track_match_adsp_voocphy_break_reason(track_status);
		break;
	case TRACK_AP_SINGLE_CP_VOOCPHY:
	case TRACK_AP_DUAL_CP_VOOCPHY:
		oplus_chg_track_match_ap_voocphy_break_reason(track_status);
		break;
	case TRACK_MCU_VOOCPHY:
		oplus_chg_track_match_mcu_voocphy_break_reason(track_status);
		break;
	default:
		pr_debug("!!!voocphy type is error, should not go here\n");
		break;
	}

	return 0;
}

void oplus_chg_track_record_chg_type_info(void)
{
	int chg_type = 0;
	int sub_chg_type = 0;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!chip || !g_track_chip)
		return;

	if (chip->chg_ops->get_charger_type)
		chg_type = chip->chg_ops->get_charger_type();
	if (chip->chg_ops->get_charger_subtype)
		sub_chg_type = chip->chg_ops->get_charger_subtype();
	g_track_chip->track_status.real_chg_type = chg_type | (sub_chg_type << 8);
	pr_debug("real_chg_type:0x%04x\n", g_track_chip->track_status.real_chg_type);
}

static int oplus_chg_track_obtain_wired_break_sub_crux_info(struct oplus_chg_track_status *track_status,
							    char *crux_info)
{
	int ret;

	ret = oplus_chg_wired_get_break_sub_crux_info(crux_info);
	if (!ret)
		ret = track_status->real_chg_type;

	return ret;
}

static int oplus_chg_track_obtain_wls_break_sub_crux_info(struct oplus_chg_track *track_chip, char *crux_info)
{
	if (!track_chip || !crux_info)
		return -EINVAL;

	oplus_wpc_get_break_sub_crux_info(crux_info);

	if (is_wls_ocm_available(track_chip))
		oplus_chg_wls_get_break_sub_crux_info(&track_chip->wls_ocm->dev, crux_info);

	return 0;
}

int oplus_chg_track_obtain_wls_general_crux_info(char *crux_info, int len)
{
	if (!g_track_chip || !crux_info)
		return -1;

	if (len != OPLUS_CHG_TRACK_CURX_INFO_LEN)
		return -1;

	oplus_wpc_get_break_sub_crux_info(crux_info);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	if (is_wls_ocm_available(g_track_chip))
		oplus_chg_wls_get_break_sub_crux_info(&g_track_chip->wls_ocm->dev, crux_info);
#endif

	return 0;
}

static bool oplus_chg_track_wls_is_online_keep(struct oplus_chg_track *track_chip)
{
	int rc;
	union oplus_chg_mod_propval temp_val = { 0 };
	bool present = false;

	if (!is_wls_ocm_available(track_chip)) {
		present = oplus_wpc_get_wireless_charge_start();
	} else {
		rc = oplus_chg_mod_get_property(track_chip->wls_ocm, OPLUS_CHG_PROP_ONLINE_KEEP, &temp_val);
		if (rc < 0)
			present = false;
		else
			present = temp_val.intval;
	}

	return present;
}

int oplus_chg_track_check_wls_charging_break(int wls_connect)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	static struct oplus_chg_track_power power_info;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	static bool break_recording = 0;
	static bool pre_wls_connect = false;

	if (!g_track_chip || !chip)
		return -EINVAL;

	track_chip = g_track_chip;
	track_status = &track_chip->track_status;

	pr_debug("pre_wls_connect[%d], wls_connect[%d], break_recording[%d], "
		"online_keep[%d]\n",
		pre_wls_connect, wls_connect, break_recording, track_status->wls_online_keep);

	if (wls_connect && (pre_wls_connect != wls_connect)) {
		track_status->wls_attach_time_ms = local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
		if ((track_status->wls_attach_time_ms - track_status->wls_detach_time_ms <
		     track_chip->track_cfg.wls_chg_break_t_thd) &&
		    !track_status->wls_online_keep) {
			if (!break_recording) {
				break_recording = true;
				track_chip->wls_charging_break_trigger.flag_reason =
					TRACK_NOTIFY_FLAG_WLS_CHARGING_BREAK;
				oplus_chg_track_record_break_charging_info(track_chip, chip, power_info,
									   track_chip->wls_break_crux_info);
				schedule_delayed_work(&track_chip->wls_charging_break_trigger_work, 0);
			}
			if (!track_status->wired_need_upload) {
				cancel_delayed_work_sync(&track_chip->charger_info_trigger_work);
				cancel_delayed_work_sync(&track_chip->no_charging_trigger_work);
				cancel_delayed_work_sync(&track_chip->slow_charging_trigger_work);
			}
		} else {
			break_recording = 0;
		}
		pr_debug("detal_t:%d, wls_attach_time = %d\n",
			track_status->wls_attach_time_ms - track_status->wls_detach_time_ms,
			track_status->wls_attach_time_ms);
	} else if (!wls_connect && (pre_wls_connect != wls_connect)) {
		track_status->wls_detach_time_ms = local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
		track_status->wls_online_keep = oplus_chg_track_wls_is_online_keep(track_chip);
		oplus_chg_track_handle_wls_type_info(track_status);
		oplus_chg_track_obtain_wls_break_sub_crux_info(track_chip, track_chip->wls_break_crux_info);
		power_info = track_status->power_info;
		oplus_chg_wake_update_work();
		pr_debug("wls_detach_time = %d\n", track_status->wls_detach_time_ms);
	}
	if (wls_connect)
		track_status->wls_online_keep = false;

	pre_wls_connect = wls_connect;

	return 0;
}

static bool oplus_chg_track_wired_fastchg_good_exit_code(struct oplus_chg_track *track_chip)
{
	bool ret = true;
	int code;
	struct oplus_chg_track_status *track_status;

	if (!track_chip)
		return true;

	track_status = &track_chip->track_status;
	code = track_status->fastchg_break_info.code;

	pr_debug("voocphy_type:%d, code:0x%x\n", track_chip->track_cfg.voocphy_type,
		track_status->fastchg_break_info.code);
	switch (track_chip->track_cfg.voocphy_type) {
	case TRACK_ADSP_VOOCPHY:
		if (!code || code == TRACK_ADSP_VOOCPHY_FULL || code == TRACK_ADSP_VOOCPHY_BATT_TEMP_OVER ||
		    code == TRACK_ADSP_VOOCPHY_SWITCH_TEMP_RANGE)
			ret = true;
		else
			ret = false;
		break;
	case TRACK_AP_SINGLE_CP_VOOCPHY:
	case TRACK_AP_DUAL_CP_VOOCPHY:
		if (!code || code == TRACK_CP_VOOCPHY_FULL || code == TRACK_CP_VOOCPHY_BATT_TEMP_OVER ||
		    code == TRACK_CP_VOOCPHY_USER_EXIT_FASTCHG || code == TRACK_CP_VOOCPHY_SWITCH_TEMP_RANGE)
			ret = true;
		else
			ret = false;
		break;
	case TRACK_MCU_VOOCPHY:
		if (!code || code == TRACK_MCU_VOOCPHY_TEMP_OVER || code == TRACK_MCU_VOOCPHY_NORMAL_TEMP_FULL ||
		    code == TRACK_MCU_VOOCPHY_LOW_TEMP_FULL || code == TRACK_MCU_VOOCPHY_BAT_TEMP_EXIT)
			ret = true;
		else
			ret = false;
		break;
	default:
		pr_debug("!!!voocphy type is error, should not go here\n");
		break;
	}

	return ret;
}

int oplus_chg_track_check_wired_charging_break(int vbus_rising)
{
	int real_chg_type;
	bool fastchg_code_ok;
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	static struct oplus_chg_track_power power_info;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	static bool break_recording = 0;
	static bool pre_vbus_rising = false;

	if (!g_track_chip || !chip)
		return -EINVAL;

	track_chip = g_track_chip;
	track_status = &track_chip->track_status;

	pr_debug("pre_vbus_rising[%d], vbus_rising[%d], break_recording[%d]\n", pre_vbus_rising, vbus_rising,
		break_recording);

	if (vbus_rising && (pre_vbus_rising != vbus_rising)) {
		track_status->pre_chg_subtype = CHARGER_SUBTYPE_DEFAULT;
		if (!chip->chg_ops->check_chrdet_status())
			track_status->chg_attach_time_ms =
				local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD + TRACK_TIME_5MIN_JIFF_THD;
		else
			track_status->chg_attach_time_ms = local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
		fastchg_code_ok = oplus_chg_track_wired_fastchg_good_exit_code(track_chip);
		pr_debug("detal_t:%d, chg_attach_time = %d, "
			"fastchg_break_code=0x%x\n",
			track_status->chg_attach_time_ms - track_status->chg_detach_time_ms,
			track_status->chg_attach_time_ms, track_status->fastchg_break_info.code);
		pr_debug("fastchg_code_ok:%d, pre_fastchg_type = %d\n", fastchg_code_ok, track_status->pre_fastchg_type);
		if ((((track_status->chg_attach_time_ms - track_status->chg_detach_time_ms <
		       track_chip->track_cfg.fast_chg_break_t_thd) &&
		      (!fastchg_code_ok)) ||
		     ((track_status->chg_attach_time_ms - track_status->chg_detach_time_ms <
		       track_chip->track_cfg.ufcs_chg_break_t_thd) &&
		      !strcmp(power_info.wired_info.adapter_type, "ufcs"))) &&
		    track_status->mmi_chg && track_status->pre_fastchg_type) {
			pr_debug("attach[%d], detach[%d], ufcs[%d] "
				"adapter_type[%s]\n",
				track_status->chg_attach_time_ms, track_status->chg_detach_time_ms,
				track_chip->track_cfg.ufcs_chg_break_t_thd, power_info.wired_info.adapter_type);
			if (!break_recording) {
				pr_debug("should report\n");
				break_recording = true;
				track_chip->charging_break_trigger.flag_reason = TRACK_NOTIFY_FLAG_FAST_CHARGING_BREAK;
				oplus_chg_track_match_fastchg_break_reason(track_chip);
				oplus_chg_track_record_break_charging_info(track_chip, chip, power_info,
									   track_chip->wired_break_crux_info);
				schedule_delayed_work(&track_chip->charging_break_trigger_work, 0);
			}
			if (!track_status->wls_need_upload && (!track_status->wired_online)) {
				cancel_delayed_work_sync(&track_chip->charger_info_trigger_work);
				cancel_delayed_work_sync(&track_chip->no_charging_trigger_work);
				cancel_delayed_work_sync(&track_chip->slow_charging_trigger_work);
			}
		} else if ((track_status->chg_attach_time_ms - track_status->chg_detach_time_ms <
			    track_chip->track_cfg.general_chg_break_t_thd) &&
			   !track_status->fastchg_break_info.code && track_status->mmi_chg) {
			if (!break_recording) {
				break_recording = true;
				track_chip->charging_break_trigger.flag_reason =
					TRACK_NOTIFY_FLAG_GENERAL_CHARGING_BREAK;
				oplus_chg_track_record_break_charging_info(track_chip, chip, power_info,
									   track_chip->wired_break_crux_info);
				schedule_delayed_work(&track_chip->charging_break_trigger_work, 0);
			}
			if (!track_status->wls_need_upload && (!track_status->wired_online)) {
				cancel_delayed_work_sync(&track_chip->charger_info_trigger_work);
				cancel_delayed_work_sync(&track_chip->no_charging_trigger_work);
				cancel_delayed_work_sync(&track_chip->slow_charging_trigger_work);
			}
		} else {
			break_recording = 0;
		}

		memset(track_chip->wired_break_crux_info, 0, sizeof(track_chip->wired_break_crux_info));
		memset(&(track_status->fastchg_break_info), 0, sizeof(track_status->fastchg_break_info));
		track_status->mmi_chg = chip->mmi_chg;
		oplus_chg_track_set_fastchg_break_code(TRACK_VOOCPHY_BREAK_DEFAULT);
		track_status->pre_fastchg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		if (delayed_work_pending(&track_chip->check_wired_online_work))
			cancel_delayed_work_sync(&track_chip->check_wired_online_work);
		mutex_lock(&track_chip->online_hold_lock);
		track_status->wired_online_check_stop = true;
		if (chip->chg_ops->check_chrdet_status())
			track_status->wired_online = true;
		mutex_unlock(&track_chip->online_hold_lock);
	} else if (!vbus_rising && (pre_vbus_rising != vbus_rising)) {
		if (!track_status->wired_online)
			track_status->chg_detach_time_ms = 0;
		else
			track_status->chg_detach_time_ms = local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
		if (delayed_work_pending(&track_chip->check_wired_online_work))
			cancel_delayed_work_sync(&track_chip->check_wired_online_work);
		mutex_lock(&track_chip->online_hold_lock);
		track_status->wired_online_check_stop = false;
		track_status->wired_online_check_count = TRACK_ONLINE_CHECK_COUNT_THD;
		mutex_unlock(&track_chip->online_hold_lock);
		schedule_delayed_work(&track_chip->check_wired_online_work, 0);
		real_chg_type = oplus_chg_track_obtain_wired_break_sub_crux_info(track_status,
										 track_chip->wired_break_crux_info);
		if (real_chg_type != POWER_SUPPLY_TYPE_UNKNOWN) {
			if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
				track_status->charger_type_backup = real_chg_type & 0xff;
			track_status->pre_chg_subtype = (real_chg_type >> 8) & 0xff;
			pr_debug("need exchange real_chg_type:%d\n", real_chg_type);
		}
		if (track_status->pre_chg_subtype == CHARGER_SUBTYPE_DEFAULT)
			track_status->pre_chg_subtype = chip->charger_subtype;
		oplus_chg_track_handle_wired_type_info(chip, track_status, TRACK_CHG_GET_LAST_TIME_TYPE);
		track_status->charger_type_backup = POWER_SUPPLY_TYPE_UNKNOWN;
		oplus_chg_track_obtain_general_info(track_chip->wired_break_crux_info,
						    strlen(track_chip->wired_break_crux_info),
						    sizeof(track_chip->wired_break_crux_info));
		power_info = track_status->power_info;
		track_status->mmi_chg = chip->mmi_chg;
		pr_debug("chg_detach_time = %d, mmi_chg=%d, wired_online=%d\n", track_status->chg_detach_time_ms,
			track_status->mmi_chg, track_status->wired_online);
	}

	pre_vbus_rising = vbus_rising;

	return 0;
}

void oplus_chg_track_aging_ffc_trigger(bool ffc1_stage)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!g_track_chip || !chip)
		return;

	track_chip = g_track_chip;
	track_chip->track_status.aging_ffc_trig = true;
	if (ffc1_stage) {
		track_chip->track_status.aging_ffc1_start_time = oplus_chg_track_get_local_time_s();
		track_chip->track_status.aging_ffc1_judge_vol = chip->limits.ffc1_normal_vfloat_sw_limit;
	} else {
		track_chip->track_status.aging_ffc2_judge_vol = chip->limits.ffc2_normal_vfloat_sw_limit;
	}
}

void oplus_chg_track_record_cv_start_time(void)
{
	struct oplus_chg_track *track_chip;

	if (!g_track_chip)
		return;

	track_chip = g_track_chip;
	track_chip->track_status.cv_start_time = oplus_chg_track_get_local_time_s();
}

void oplus_chg_track_record_ffc_start_info(void)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!g_track_chip || !chip)
		return;

	track_chip = g_track_chip;

	track_status = &track_chip->track_status;
	track_status->ffc_start_time = oplus_chg_track_get_local_time_s();
	track_status->ffc_start_main_soc = chip->main_batt_soc;
	track_status->ffc_start_sub_soc = chip->sub_batt_soc;
}

void oplus_chg_track_record_ffc_end_info(void)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int curr_time;

	if (!g_track_chip || !chip)
		return;

	track_chip = g_track_chip;
	track_status = &track_chip->track_status;
	if (!track_status->ffc_start_time)
		return;
	curr_time = oplus_chg_track_get_local_time_s();

	if (track_status->ffc_start_time &&
	    track_status->chg_ffc_time == 0) {
		track_status->chg_ffc_time = curr_time - track_status->ffc_start_time;
		track_status->ffc_end_main_soc = chip->main_batt_soc;
		track_status->ffc_end_sub_soc = chip->sub_batt_soc;
		oplus_chg_track_record_cv_start_time();
	}
}

static int oplus_chg_track_cal_tbatt_status(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)
{
	if (!chip || !track_status)
		return -EINVAL;

	if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
		pr_debug("!!!not chging, should return\n");
		return 0;
	}

	if (!track_status->tbatt_warm_once &&
	    ((chip->tbatt_status == BATTERY_STATUS__WARM_TEMP) || (chip->tbatt_status == BATTERY_STATUS__HIGH_TEMP)))
		track_status->tbatt_warm_once = true;

	if (!track_status->tbatt_cold_once &&
	    ((chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) || (chip->tbatt_status == BATTERY_STATUS__LOW_TEMP) ||
	     (chip->tbatt_status == BATTERY_STATUS__REMOVED)))
		track_status->tbatt_cold_once = true;

	pr_debug("tbatt_warm_once:%d, tbatt_cold_once:%d\n", track_status->tbatt_warm_once,
		track_status->tbatt_cold_once);

	return 0;
}

static int oplus_chg_track_cal_section_soc_inc_rm(struct oplus_chg_chip *chip,
						  struct oplus_chg_track_status *track_status)
{
	static int time_go_next_status;
	static int rm_go_next_status;
	int curr_time;
	static int pre_soc_low_sect_incr_rm;
	static int pre_soc_low_sect_cont_time;
	static int pre_soc_medium_sect_incr_rm;
	static int pre_soc_medium_sect_cont_time;
	static int pre_soc_high_sect_incr_rm;
	static int pre_soc_high_sect_cont_time;

	if (!chip || !track_status)
		return -EINVAL;

	if (track_status->soc_sect_status == TRACK_SOC_SECTION_DEFAULT) {
		track_status->soc_low_sect_incr_rm = 0;
		track_status->soc_low_sect_cont_time = 0;
		track_status->soc_medium_sect_incr_rm = 0;
		track_status->soc_medium_sect_cont_time = 0;
		track_status->soc_high_sect_incr_rm = 0;
		track_status->soc_high_sect_cont_time = 0;
		pre_soc_low_sect_incr_rm = 0;
		pre_soc_low_sect_cont_time = 0;
		pre_soc_medium_sect_incr_rm = 0;
		pre_soc_medium_sect_cont_time = 0;
		pre_soc_high_sect_incr_rm = 0;
		pre_soc_high_sect_cont_time = 0;
		if (chip->soc <= TRACK_REF_SOC_50)
			track_status->soc_sect_status = TRACK_SOC_SECTION_LOW;
		else if (chip->soc <= TRACK_REF_SOC_75)
			track_status->soc_sect_status = TRACK_SOC_SECTION_MEDIUM;
		else if (chip->soc <= TRACK_REF_SOC_90)
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		else
			track_status->soc_sect_status = TRACK_SOC_SECTION_OVER;
		time_go_next_status = oplus_chg_track_get_local_time_s();
		rm_go_next_status = chip->batt_rm;
	}

	if (chip->prop_status != POWER_SUPPLY_STATUS_CHARGING) {
		if (chip->soc <= TRACK_REF_SOC_50)
			track_status->soc_sect_status = TRACK_SOC_SECTION_LOW;
		else if (chip->soc <= TRACK_REF_SOC_75)
			track_status->soc_sect_status = TRACK_SOC_SECTION_MEDIUM;
		else if (chip->soc <= TRACK_REF_SOC_90)
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		else
			track_status->soc_sect_status = TRACK_SOC_SECTION_OVER;
		pre_soc_low_sect_cont_time = track_status->soc_low_sect_cont_time;
		pre_soc_low_sect_incr_rm = track_status->soc_low_sect_incr_rm;
		pre_soc_medium_sect_cont_time = track_status->soc_medium_sect_cont_time;
		pre_soc_medium_sect_incr_rm = track_status->soc_medium_sect_incr_rm;
		pre_soc_high_sect_cont_time = track_status->soc_high_sect_cont_time;
		pre_soc_high_sect_incr_rm = track_status->soc_high_sect_incr_rm;
		time_go_next_status = oplus_chg_track_get_local_time_s();
		rm_go_next_status = chip->batt_rm;
		return 0;
	}

	switch (track_status->soc_sect_status) {
	case TRACK_SOC_SECTION_LOW:
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->soc_low_sect_cont_time = (curr_time - time_go_next_status) + pre_soc_low_sect_cont_time;
		track_status->soc_low_sect_incr_rm = (chip->batt_rm - rm_go_next_status) + pre_soc_low_sect_incr_rm;
		if (chip->soc > TRACK_REF_SOC_50) {
			pre_soc_low_sect_cont_time = track_status->soc_low_sect_cont_time;
			pre_soc_low_sect_incr_rm = track_status->soc_low_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = chip->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_MEDIUM;
		}
		break;
	case TRACK_SOC_SECTION_MEDIUM:
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->soc_medium_sect_cont_time =
			(curr_time - time_go_next_status) + pre_soc_medium_sect_cont_time;
		track_status->soc_medium_sect_incr_rm =
			(chip->batt_rm - rm_go_next_status) + pre_soc_medium_sect_incr_rm;
		if (chip->soc <= TRACK_REF_SOC_50) {
			pre_soc_medium_sect_cont_time = track_status->soc_medium_sect_cont_time;
			pre_soc_medium_sect_incr_rm = track_status->soc_medium_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = chip->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_LOW;
		} else if (chip->soc > TRACK_REF_SOC_75) {
			pre_soc_medium_sect_cont_time = track_status->soc_medium_sect_cont_time;
			pre_soc_medium_sect_incr_rm = track_status->soc_medium_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = chip->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		}
		break;
	case TRACK_SOC_SECTION_HIGH:
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->soc_high_sect_cont_time = (curr_time - time_go_next_status) + pre_soc_high_sect_cont_time;
		track_status->soc_high_sect_incr_rm = (chip->batt_rm - rm_go_next_status) + pre_soc_high_sect_incr_rm;
		if (chip->soc <= TRACK_REF_SOC_75) {
			pre_soc_high_sect_cont_time = track_status->soc_high_sect_cont_time;
			pre_soc_high_sect_incr_rm = track_status->soc_high_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = chip->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_MEDIUM;
		} else if (chip->soc > TRACK_REF_SOC_90) {
			pre_soc_high_sect_cont_time = track_status->soc_high_sect_cont_time;
			pre_soc_high_sect_incr_rm = track_status->soc_high_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = chip->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_OVER;
		}
		break;
	case TRACK_SOC_SECTION_OVER:
		curr_time = oplus_chg_track_get_local_time_s();
		if (chip->soc < TRACK_REF_SOC_90) {
			time_go_next_status = curr_time;
			rm_go_next_status = chip->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		}
		break;
	default:
		pr_err("!!!soc section status is invalid\n");
		break;
	}

	pr_debug("soc_sect_status:%d, time_go_next_status:%d, "
		"rm_go_next_status:%d\n",
		track_status->soc_sect_status, time_go_next_status, rm_go_next_status);

	pr_debug("soc_low_sect_cont_time:%d, soc_low_sect_incr_rm:%d, \
		soc_medium_sect_cont_time:%d, soc_medium_sect_incr_rm:%d \
		soc_high_sect_cont_time:%d, soc_high_sect_incr_rm:%d\n",
		track_status->soc_low_sect_cont_time, track_status->soc_low_sect_incr_rm,
		track_status->soc_medium_sect_cont_time, track_status->soc_medium_sect_incr_rm,
		track_status->soc_high_sect_cont_time, track_status->soc_high_sect_incr_rm);
	return 0;
}

static bool oplus_chg_track_burst_soc_sect_speed(struct oplus_chg_chip *chip,
						 struct oplus_chg_track_status *track_status,
						 struct oplus_chg_track_speed_ref *speed_ref)
{
	bool ret = false;
	int soc_low_sect_incr_rm;
	int soc_medium_sect_incr_rm;
	int soc_high_sect_incr_rm;

	if (!track_status || !speed_ref || !chip)
		return false;

	if (!track_status->soc_high_sect_cont_time && !track_status->soc_medium_sect_cont_time &&
	    !track_status->soc_low_sect_cont_time)
		return true;

	pr_debug("low_ref_curr:%d, medium_ref_curr:%d, high_ref_curr:%d\n",
		speed_ref[TRACK_SOC_SECTION_LOW - 1].ref_curr, speed_ref[TRACK_SOC_SECTION_MEDIUM - 1].ref_curr,
		speed_ref[TRACK_SOC_SECTION_HIGH - 1].ref_curr);

	soc_low_sect_incr_rm = track_status->soc_low_sect_incr_rm;
	soc_medium_sect_incr_rm = track_status->soc_medium_sect_incr_rm;
	soc_high_sect_incr_rm = track_status->soc_high_sect_incr_rm;

	soc_low_sect_incr_rm *= (TRACK_TIME_1HOU_THD / chip->vbatt_num);
	soc_medium_sect_incr_rm *= (TRACK_TIME_1HOU_THD / chip->vbatt_num);
	soc_high_sect_incr_rm *= (TRACK_TIME_1HOU_THD / chip->vbatt_num);

	if ((track_status->soc_low_sect_cont_time > TRACK_REF_TIME_6S) &&
	    ((soc_low_sect_incr_rm / track_status->soc_low_sect_cont_time) <
	     speed_ref[TRACK_SOC_SECTION_LOW - 1].ref_curr)) {
		pr_debug("slow charging when soc low section\n");
		ret = true;
	}

	if (!ret && (track_status->soc_medium_sect_cont_time > TRACK_REF_TIME_8S) &&
	    ((soc_medium_sect_incr_rm / track_status->soc_medium_sect_cont_time) <
	     speed_ref[TRACK_SOC_SECTION_MEDIUM - 1].ref_curr)) {
		pr_debug("slow charging when soc medium section\n");
		ret = true;
	}

	if (!ret && (track_status->soc_high_sect_cont_time > TRACK_REF_TIME_10S) &&
	    ((soc_high_sect_incr_rm / track_status->soc_high_sect_cont_time) <
	     speed_ref[TRACK_SOC_SECTION_HIGH - 1].ref_curr)) {
		pr_debug("slow charging when soc high section\n");
		ret = true;
	}

	return ret;
}

static int oplus_chg_track_get_speed_slow_reason(struct oplus_chg_track_status *track_status)
{
	struct oplus_chg_track *chip = g_track_chip;
	char wired_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
	char wls_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];

	if (!track_status || !chip)
		return -EINVAL;

	memcpy(wired_adapter_type, track_status->power_info.wired_info.adapter_type, OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	memcpy(wls_adapter_type, track_status->power_info.wls_info.adapter_type, OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	pr_debug("wired_adapter_type:%s, wls_adapter_type:%s\n", wired_adapter_type, wls_adapter_type);

	if (track_status->tbatt_warm_once)
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM;
	else if (track_status->tbatt_cold_once)
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_COLD;
	else if ((track_status->cool_down_effect_cnt * 100 / track_status->chg_total_cnt) >
		 TRACK_COOLDOWN_CHRGING_TIME_PCT)
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_COOLDOWN;
	else if (track_status->chg_start_soc >= TRACK_REF_SOC_90)
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_BATT_CAP_HIGH;
	else if ((track_status->power_info.power_type == TRACK_CHG_TYPE_UNKNOW) ||
		 (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRE &&
		  chip->track_cfg.wired_max_power > track_status->power_info.wired_info.power))
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_NON_STANDARD_PA;
	else if ((track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) &&
		 (chip->track_cfg.wls_max_power - TRACK_POWER_MW(10000) > track_status->power_info.wls_info.power) &&
		 (strcmp(wls_adapter_type, "bpp")) && (strcmp(wls_adapter_type, "epp")))
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_NON_STANDARD_PA;
	else if ((track_status->wls_skew_effect_cnt * 100 / track_status->chg_total_cnt) >
			 TRACK_WLS_SKEWING_CHRGING_TIME_PCT &&
		 track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_WLS_SKEW;
	else if (!track_status->chg_verity && track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_VERITY_FAIL;
	else
		chip->slow_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHG_SLOW_OTHER;

	if (track_status->debug_slow_charging_reason)
		chip->slow_charging_trigger.flag_reason =
			track_status->debug_slow_charging_reason + TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM - 1;

	pr_debug("flag_reason:%d\n", chip->slow_charging_trigger.flag_reason);

	return 0;
}

static bool oplus_chg_track_judge_speed_slow(struct oplus_chg_chip *chip, struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_status *track_status;

	if (!track_chip || !chip)
		return 0;

	track_status = &(track_chip->track_status);
	mutex_lock(&track_chip->access_lock);
	if (track_status->has_judge_speed) {
		mutex_unlock(&track_chip->access_lock);
		return track_status->chg_speed_is_slow;
	}
	track_status->has_judge_speed = true;
	mutex_unlock(&track_chip->access_lock);

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_UNKNOW) {
		track_status->chg_speed_is_slow = true;
	} else if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		if (!strcmp(track_status->power_info.wls_info.adapter_type, "unknow")) {
			track_status->chg_speed_is_slow = true;
		} else if (!strcmp(track_status->power_info.wls_info.adapter_type, "epp")) {
			if (!track_status->wls_epp_speed_ref)
				return false;
			track_status->chg_speed_is_slow = oplus_chg_track_burst_soc_sect_speed(
				chip, track_status, track_status->wls_epp_speed_ref);
		} else if (!strcmp(track_status->power_info.wls_info.adapter_type, "bpp")) {
			if (!track_status->wls_bpp_speed_ref)
				return false;
			track_status->chg_speed_is_slow = oplus_chg_track_burst_soc_sect_speed(
				chip, track_status, track_status->wls_bpp_speed_ref);
		} else {
			if (!track_status->wls_airvooc_speed_ref)
				return false;
			track_status->chg_speed_is_slow = oplus_chg_track_burst_soc_sect_speed(
				chip, track_status, track_status->wls_airvooc_speed_ref);
		}
	} else {
		if (!track_status->wired_speed_ref)
			return false;
		track_status->chg_speed_is_slow =
			oplus_chg_track_burst_soc_sect_speed(chip, track_status, track_status->wired_speed_ref);
	}

	if (track_status->chg_speed_is_slow || track_status->debug_slow_charging_force) {
		oplus_chg_track_get_speed_slow_reason(track_status);
	}

	return (track_status->chg_speed_is_slow || track_status->debug_slow_charging_force);
}

static int oplus_chg_track_cal_batt_full_time(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)
{
	static int pre_wls_ffc_status = 0, wls_ffc_status = 0;
	static int pre_wls_chg_done = 0, wls_chg_done = 0;
	int curr_time;

	if (!chip || !track_status || !g_track_chip)
		return -EINVAL;

	wls_ffc_status = oplus_wpc_get_ffc_charging();
	if (is_comm_ocm_available(g_track_chip))
		wls_chg_done = oplus_chg_comm_get_chg_done(g_track_chip->comm_ocm);

	pr_debug("pre_wls_ffc_status:%d, wls_ffc_status:%d,\
	    pre_wls_chg_done:%d,wls_chg_done:%d",
		pre_wls_ffc_status, wls_ffc_status, pre_wls_chg_done, wls_chg_done);
	if (!track_status->chg_fast_full_time &&
	    (track_status->fastchg_to_normal || oplus_ufcs_get_ffc_started() ||
	     (!pre_wls_ffc_status && wls_ffc_status) || track_status->wls_prop_status == TRACK_WLS_FASTCHG_FULL ||
	     track_status->debug_fast_prop_status == TRACK_FASTCHG_STATUS_NORMAL)) {
		track_status->chg_report_full_time = 0;
		track_status->chg_normal_full_time = 0;
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->chg_fast_full_time = curr_time - track_status->chg_start_time;
		track_status->chg_fast_full_time /= TRACK_TIME_1MIN_THD;
		if (!track_status->chg_fast_full_time)
			track_status->chg_fast_full_time = TRACK_TIME_1MIN_THD / TRACK_TIME_1MIN_THD;
		if (track_status->chg_average_speed == TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT)
			track_status->chg_average_speed = TRACK_TIME_1MIN_THD *
							  (chip->batt_rm - track_status->chg_start_rm) /
							  (curr_time - track_status->chg_start_time);
		pr_debug("curr_time:%d, start_time:%d, fast_full_time:%d \
			curr_rm:%d, chg_start_rm:%d, chg_average_speed:%d\n",
			curr_time, track_status->chg_start_time, track_status->chg_fast_full_time, chip->batt_rm,
			track_status->chg_start_rm, track_status->chg_average_speed);
	}

	if (!track_status->chg_report_full_time &&
	    (chip->prop_status == POWER_SUPPLY_STATUS_FULL ||
	     track_status->debug_normal_prop_status == POWER_SUPPLY_STATUS_FULL)) {
		oplus_chg_track_handle_batt_full_reason(chip, track_status);
		oplus_chg_track_judge_speed_slow(chip, g_track_chip);
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->chg_report_full_time = curr_time - track_status->chg_start_time;
		track_status->chg_report_full_time /= TRACK_TIME_1MIN_THD;
		if (!track_status->chg_report_full_time)
			track_status->chg_report_full_time = TRACK_TIME_1MIN_THD / TRACK_TIME_1MIN_THD;
	}

	if (!track_status->chg_normal_full_time &&
	    (chip->charging_state == CHARGING_STATUS_FULL || (!pre_wls_chg_done && wls_chg_done) ||
	     track_status->debug_normal_charging_state == CHARGING_STATUS_FULL)) {
		curr_time = oplus_chg_track_get_local_time_s();
		if (track_status->cv_start_time)
			track_status->chg_cv_time = curr_time - track_status->cv_start_time;
		if (track_status->aging_ffc1_start_time)
			track_status->aging_ffc1_to_full_time = curr_time - track_status->aging_ffc1_start_time;
		track_status->chg_normal_full_time = curr_time - track_status->chg_start_time;
		track_status->chg_normal_full_time /= TRACK_TIME_1MIN_THD;
		if (!track_status->chg_normal_full_time)
			track_status->chg_normal_full_time = TRACK_TIME_1MIN_THD / TRACK_TIME_1MIN_THD;
		if (track_status->chg_average_speed == TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT)
			track_status->chg_average_speed = TRACK_TIME_1MIN_THD *
							  (chip->batt_rm - track_status->chg_start_rm) /
							  (curr_time - track_status->chg_start_time);
		pr_debug("curr_time:%d, start_time:%d, normal_full_time:%d \
			curr_rm:%d, chg_start_rm:%d, chg_average_speed:%d\n",
			curr_time, track_status->chg_start_time, track_status->chg_normal_full_time, chip->batt_rm,
			track_status->chg_start_rm, track_status->chg_average_speed);
	}

	pre_wls_ffc_status = wls_ffc_status;
	pre_wls_chg_done = wls_chg_done;
	return 0;
}

static int oplus_chg_track_get_charger_type(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status,
					    int type)
{
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

	if (chip == NULL || track_status == NULL)
		return POWER_SUPPLY_TYPE_UNKNOWN;

	charger_type = chip->charger_type;
	if (charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
		return POWER_SUPPLY_TYPE_UNKNOWN;

	if (charger_type == POWER_SUPPLY_TYPE_WIRELESS)
		oplus_chg_track_handle_wls_type_info(track_status);
	else
		oplus_chg_track_handle_wired_type_info(chip, track_status, type);

	return charger_type;
}

static int oplus_chg_track_obtain_action_info(struct oplus_chg_chip *chip, char *action_info, int len)
{
	int index = 0;
	bool tx_mode;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	int rc;
	union oplus_chg_mod_propval pval;
#endif

	if (!action_info || !len || !chip || !g_track_chip)
		return -1;

	memset(action_info, 0, len);

	tx_mode = oplus_wpc_get_otg_charging();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	if (is_wls_ocm_available(g_track_chip)) {
		rc = oplus_chg_mod_get_property(g_track_chip->wls_ocm, OPLUS_CHG_PROP_TRX_STATUS, &pval);
		if (rc == 0 && pval.intval != OPLUS_CHG_WLS_TRX_STATUS_DISENABLE)
			tx_mode = true;
	}
#endif

	if (chip->charger_type && chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS) {
		if (chip->otg_online)
			index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_WRX_OTG);
		else
			index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_WRX);
	} else if (chip->charger_type && chip->charger_type != POWER_SUPPLY_TYPE_WIRELESS) {
		if (tx_mode)
			index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_WTX_WIRED);
		else
			index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_WIRED);
	} else if (tx_mode) {
		if (chip->otg_online)
			index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_WTX_OTG);
		else
			index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_WTX);
	} else if (chip->otg_online) {
		index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_OTG);
	} else {
		index += snprintf(&(action_info[index]), len - index, "%s", TRACK_ACTION_OTHER);
	}

	return 0;
}

int oplus_chg_track_obtain_power_info(char *power_info, int len)
{
	int index = 0;
	struct oplus_chg_track_status temp_track_status;
	struct oplus_chg_track_status *track_status = &temp_track_status;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	char action_info[TRACK_ACTION_LENS] = { 0 };

	if (!power_info || !chip || !g_track_chip)
		return -EINVAL;

	if (len < OPLUS_CHG_TRACK_POWER_INFO_LEN) {
		pr_err("len is invalid\n");
		return -1;
	}

	memset(&temp_track_status, 0, sizeof(temp_track_status));
	oplus_chg_track_obtain_action_info(chip, action_info, sizeof(action_info));
	oplus_chg_track_get_charger_type(chip, track_status, TRACK_CHG_GET_THTS_TIME_TYPE);

	memset(power_info, 0, len);
	index += snprintf(&(power_info[index]), len - index, "$$power_mode@@%s", track_status->power_info.power_mode);

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRE) {
		index += snprintf(&(power_info[index]), len - index, "$$adapter_t@@%s",
				  track_status->power_info.wired_info.adapter_type);
		if (track_status->power_info.wired_info.adapter_id)
			index += snprintf(&(power_info[index]), len - index, "$$adapter_id@@0x%x",
					  track_status->power_info.wired_info.adapter_id);
		index += snprintf(&(power_info[index]), len - index, "$$power@@%d",
				  track_status->power_info.wired_info.power);
	} else if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		index += snprintf(&(power_info[index]), len - index, "$$adapter_t@@%s",
				  track_status->power_info.wls_info.adapter_type);
		if (strlen(track_status->power_info.wls_info.dock_type))
			index += snprintf(&(power_info[index]), len - index, "$$dock_type@@%s",
					  track_status->power_info.wls_info.dock_type);
		index += snprintf(&(power_info[index]), len - index, "$$power@@%d",
				  track_status->power_info.wls_info.power);
	}

	index += snprintf(&(power_info[index]), len - index, "$$soc@@%d", chip->soc);
	index += snprintf(&(power_info[index]), len - index, "$$batt_temp@@%d", chip->tbatt_temp);
	index += snprintf(&(power_info[index]), len - index, "$$shell_temp@@%d", chip->shell_temp);
	index += snprintf(&(power_info[index]), len - index, "$$subboard_temp@@%d", chip->subboard_temp);
	index += snprintf(&(power_info[index]), len - index, "$$batt_vol@@%d", chip->batt_volt);
	index += snprintf(&(power_info[index]), len - index, "$$batt_curr@@%d", chip->icharging);
	index += snprintf(&(power_info[index]), len - index, "$$action@@%s", action_info);

	return 0;
}

static int oplus_chg_track_cal_period_chg_capaticy(struct oplus_chg_track *track_chip)
{
	int ret = 0;

	if (!track_chip)
		return -EFAULT;

	if (track_chip->track_status.chg_start_soc > TRACK_PERIOD_CHG_CAP_MAX_SOC)
		return ret;

	pr_debug("enter\n");
	schedule_delayed_work(&track_chip->cal_chg_five_mins_capacity_work, msecs_to_jiffies(TRACK_TIME_5MIN_JIFF_THD));
	schedule_delayed_work(&track_chip->cal_chg_ten_mins_capacity_work, msecs_to_jiffies(TRACK_TIME_10MIN_JIFF_THD));

	return ret;
}

static int oplus_chg_track_cancel_cal_period_chg_capaticy(struct oplus_chg_track *track_chip)
{
	int ret = 0;

	if (!track_chip)
		return -EFAULT;

	pr_debug("enter\n");
	if (delayed_work_pending(&track_chip->cal_chg_five_mins_capacity_work))
		cancel_delayed_work_sync(&track_chip->cal_chg_five_mins_capacity_work);

	if (delayed_work_pending(&track_chip->cal_chg_ten_mins_capacity_work))
		cancel_delayed_work_sync(&track_chip->cal_chg_ten_mins_capacity_work);

	return ret;
}

static int oplus_chg_track_cal_period_chg_average_speed(struct oplus_chg_track_status *track_status,
							struct oplus_chg_chip *chip)
{
	int ret = 0;
	int curr_time;

	if (!track_status || !chip)
		return -EFAULT;

	pr_debug("enter\n");
	if (track_status->chg_average_speed == TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT) {
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->chg_average_speed = TRACK_TIME_1MIN_THD * (chip->batt_rm - track_status->chg_start_rm) /
						  (curr_time - track_status->chg_start_time);
		pr_debug("curr_rm:%d, chg_start_rm:%d, curr_time:%d, chg_start_time:%d,\
			chg_average_speed:%d\n",
			chip->batt_rm, track_status->chg_start_rm, curr_time, track_status->chg_start_time,
			track_status->chg_average_speed);
	}

	return ret;
}

static int oplus_chg_track_cal_chg_end_soc(struct oplus_chg_chip *chip, struct oplus_chg_track_status *track_status)
{
	if (!track_status || !chip)
		return -EFAULT;

	if (chip->prop_status == POWER_SUPPLY_STATUS_CHARGING)
		track_status->chg_end_soc = chip->soc;

	return 0;
}

static int oplus_chg_track_cal_hyper_speed_status(struct oplus_chg_chip *chip,
						  struct oplus_chg_track_status *track_status)
{
	if (!track_status || !chip)
		return -EFAULT;

	track_status->hyper_stop_soc = chip->quick_mode_stop_soc;
	track_status->hyper_stop_temp = chip->quick_mode_stop_temp;
	track_status->hyper_last_time = chip->quick_mode_stop_time - chip->quick_mode_start_time;
	track_status->hyper_ave_speed =
		(chip->quick_mode_stop_cap - chip->quick_mode_start_cap) / (track_status->hyper_last_time / 60);
	track_status->hyper_est_save_time = chip->quick_mode_gain_time_ms / 1000;
	if ((chip->quick_mode_start_time != 0) && (track_status->hyper_en == 0))
		track_status->hyper_en = 1;

	return 0;
}

static void oplus_chg_track_hyper_speed_record(struct oplus_chg_track_status *track_status)
{
	int index = 0;

	if (track_status->hyper_en) {
		index += snprintf(&(track_status->hyper_info[index]), TRACK_HIDL_HYPER_INFO_LEN - index,
				  "hyper_en=%d,", track_status->hyper_en);

		index += snprintf(&(track_status->hyper_info[index]), TRACK_HIDL_HYPER_INFO_LEN - index,
				  "hyper_stop_cap=%d,", track_status->hyper_stop_soc);

		index += snprintf(&(track_status->hyper_info[index]), TRACK_HIDL_HYPER_INFO_LEN - index,
				  "hyper_stop_temp=%d,", track_status->hyper_stop_temp);

		index += snprintf(&(track_status->hyper_info[index]), TRACK_HIDL_HYPER_INFO_LEN - index,
				  "hyper_last_t=%d,", track_status->hyper_last_time);

		index += snprintf(&(track_status->hyper_info[index]), TRACK_HIDL_HYPER_INFO_LEN - index,
				  "hyper_ave_speed=%d,", track_status->hyper_ave_speed);

		index += snprintf(&(track_status->hyper_info[index]), TRACK_HIDL_HYPER_INFO_LEN - index,
				  "hyper_est_save_t=%d", track_status->hyper_est_save_time);
	}
}

static void oplus_chg_track_reset_chg_abnormal_happened_flag(struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chg_abnormal_reason_table); i++)
		chg_abnormal_reason_table[i].happened = false;
}

static int oplus_chg_track_status_reset(struct oplus_chg_track_status *track_status)
{
	memset(&(track_status->power_info), 0, sizeof(track_status->power_info));
	strcpy(track_status->power_info.power_mode, "unknow");
	track_status->chg_no_charging_cnt = 0;
	track_status->ledon_ave_speed = 0;
	track_status->ledoff_ave_speed = 0;
	track_status->ledon_rm = 0;
	track_status->ledoff_rm = 0;
	track_status->ledon_time = 0;
	track_status->ledoff_time = 0;
	track_status->continue_ledon_time = 0;
	track_status->continue_ledoff_time = 0;
	track_status->chg_total_cnt = 0;
	track_status->ffc_start_time = 0;
	track_status->cv_start_time = 0;
	track_status->chg_ffc_time = 0;
	track_status->chg_cv_time = 0;
	track_status->ffc_start_main_soc = 0;
	track_status->ffc_start_sub_soc = 0;
	track_status->ffc_end_main_soc = 0;
	track_status->ffc_end_sub_soc = 0;
	track_status->chg_fast_full_time = 0;
	track_status->chg_normal_full_time = 0;
	track_status->chg_report_full_time = 0;
	track_status->aging_ffc_trig = false;
	track_status->aging_ffc1_judge_vol = 0;
	track_status->aging_ffc2_judge_vol = 0;
	track_status->aging_ffc1_start_time = 0;
	track_status->aging_ffc1_to_full_time = 0;
	track_status->chg_five_mins_cap = TRACK_PERIOD_CHG_CAP_INIT;
	track_status->chg_ten_mins_cap = TRACK_PERIOD_CHG_CAP_INIT;
	track_status->chg_average_speed = TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT;
	track_status->soc_sect_status = TRACK_SOC_SECTION_DEFAULT;
	track_status->tbatt_warm_once = false;
	track_status->tbatt_cold_once = false;
	track_status->cool_down_effect_cnt = 0;
	track_status->chg_speed_is_slow = false;
	track_status->in_rechging = false;
	track_status->rechg_counts = 0;
	track_status->led_onoff_power_cal = false;
	track_status->cool_down_status = 0;
	track_status->has_judge_speed = false;
	track_status->wls_prop_status = TRACK_CHG_DEFAULT;
	track_status->wls_skew_effect_cnt = 0;
	track_status->chg_verity = true;
	track_status->real_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	memset(track_status->batt_full_reason, 0, sizeof(track_status->batt_full_reason));
	oplus_chg_track_clear_cool_down_stats_time(track_status);
	memset(track_status->chg_abnormal_reason, 0, sizeof(track_status->chg_abnormal_reason));
	oplus_chg_track_reset_chg_abnormal_happened_flag(track_status);
	memset(track_status->bcc_info, 0, sizeof(struct oplus_chg_track_hidl_bcc_info));

	return 0;
}

static int oplus_chg_track_status_reset_when_plugin(struct oplus_chg_chip *chip,
						    struct oplus_chg_track_status *track_status)
{
	track_status->soc_sect_status = TRACK_SOC_SECTION_DEFAULT;
	track_status->chg_speed_is_slow = false;
	track_status->chg_start_time = oplus_chg_track_get_local_time_s();
	track_status->chg_end_time = track_status->chg_start_time;
	track_status->chg_start_soc = chip->soc;
	track_status->led_on = chip->led_on;
	track_status->led_change_t = track_status->chg_start_time;
	track_status->led_change_rm = chip->batt_rm;
	track_status->chg_start_temp = chip->temperature;
	track_status->batt_start_temp = oplus_get_report_batt_temp();
	track_status->batt_max_temp = oplus_get_report_batt_temp();
	track_status->batt_max_vol = chip->batt_volt;
	track_status->batt_max_curr = chip->icharging;
	track_status->chg_start_rm = chip->batt_rm;
	track_status->chg_max_temp = chip->temperature;
	track_status->ledon_time = 0;
	track_status->ledoff_time = 0;
	track_status->continue_ledon_time = 0;
	track_status->continue_ledoff_time = 0;
	track_status->cool_down_status = chip->cool_down;
	track_status->cool_down_status_change_t = track_status->chg_start_time;
	track_status->has_judge_speed = false;
	track_status->wls_prop_status = TRACK_CHG_DEFAULT;
	track_status->wls_skew_effect_cnt = 0;
	track_status->chg_verity = true;
	track_status->chg_plugin_utc_t = oplus_chg_track_get_current_time_s(&track_status->chg_plugin_rtc_t);
	oplus_chg_track_cal_period_chg_capaticy(g_track_chip);
	track_status->prop_status = chip->prop_status;
	track_status->once_mmi_chg = false;
	track_status->once_chg_cycle_status = CHG_CYCLE_VOTER__NONE;
	track_status->fastchg_to_normal = false;
	pr_debug("chg_start_time:%d, chg_start_soc:%d, chg_start_temp:%d, "
		"prop_status:%d\n",
		track_status->chg_start_time, track_status->chg_start_soc, track_status->chg_start_temp,
		track_status->prop_status);

	return 0;
}

static int oplus_chg_need_record(struct oplus_chg_chip *chip, struct oplus_chg_track *track_chip)
{
	int wls_break_work_delay_t;
	int wired_break_work_delay_t;
	struct oplus_chg_track_status *track_status;

	if (delayed_work_pending(&track_chip->no_charging_trigger_work) ||
	    delayed_work_pending(&track_chip->slow_charging_trigger_work) ||
	    delayed_work_pending(&track_chip->charger_info_trigger_work)) {
		pr_err("work is pending, should not handle\n");
		return -EFAULT;
	}

	pr_debug("good record charger info and upload charger info\n");
	track_status = &track_chip->track_status;
	track_status->chg_plugout_utc_t = oplus_chg_track_get_current_time_s(&track_status->chg_plugout_rtc_t);
	wls_break_work_delay_t = track_chip->track_cfg.wls_chg_break_t_thd + TRACK_TIME_1000MS_JIFF_THD;
	if (!strcmp(track_status->power_info.wls_info.adapter_type, "ufcs"))
		wired_break_work_delay_t = track_chip->track_cfg.ufcs_chg_break_t_thd + TRACK_TIME_500MS_JIFF_THD;
	else
		wired_break_work_delay_t = track_chip->track_cfg.fast_chg_break_t_thd + TRACK_TIME_500MS_JIFF_THD;

	oplus_chg_track_cal_led_on_stats(chip, track_status);
	oplus_chg_track_cal_period_chg_average_speed(track_status, chip);
	oplus_chg_track_cal_ledon_ledoff_average_speed(track_status);
	oplus_chg_track_hyper_speed_record(track_status);
	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
		track_status->wls_need_upload = true;
	else
		track_status->wired_need_upload = true;
	if (oplus_chg_track_is_no_charging(track_status)) {
		oplus_chg_track_record_charger_info(chip, &track_chip->no_charging_trigger, track_status);
		if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
			schedule_delayed_work(&track_chip->no_charging_trigger_work,
					      msecs_to_jiffies(wls_break_work_delay_t));
		else
			schedule_delayed_work(&track_chip->no_charging_trigger_work,
					      msecs_to_jiffies(wired_break_work_delay_t));
	} else if (oplus_chg_track_judge_speed_slow(chip, track_chip)) {
		oplus_chg_track_record_charger_info(chip, &track_chip->slow_charging_trigger, track_status);
		if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
			schedule_delayed_work(&track_chip->slow_charging_trigger_work,
					      msecs_to_jiffies(wls_break_work_delay_t));
		else
			schedule_delayed_work(&track_chip->slow_charging_trigger_work,
					      msecs_to_jiffies(wired_break_work_delay_t));
	} else {
		oplus_chg_track_record_charger_info(chip, &track_chip->charger_info_trigger, track_status);
		if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
			schedule_delayed_work(&track_chip->charger_info_trigger_work,
					      msecs_to_jiffies(wls_break_work_delay_t));
		else
			schedule_delayed_work(&track_chip->charger_info_trigger_work,
					      msecs_to_jiffies(wired_break_work_delay_t));
	}

	return 0;
}

static int oplus_chg_track_speed_check(struct oplus_chg_chip *chip)
{
	int ret = 0;
	int track_recording_time;
	static bool track_reset = true;
	static bool track_record_charger_info = false;
	struct oplus_chg_track_status *track_status;

	if (!g_track_chip)
		return -EFAULT;

	track_status = &g_track_chip->track_status;
	if (!oplus_chg_track_charger_exist(chip)) {
		if (track_record_charger_info) {
			oplus_chg_need_record(chip, g_track_chip);
			memset(track_status->bms_info, 0, TRACK_HIDL_BMS_INFO_LEN);
		}

		track_reset = true;
		track_record_charger_info = false;
		oplus_chg_track_cancel_cal_period_chg_capaticy(g_track_chip);
		oplus_chg_track_status_reset(track_status);
		return ret;
	}

	if (oplus_chg_track_charger_exist(chip) && track_reset) {
		track_reset = false;
		oplus_chg_track_status_reset_when_plugin(chip, track_status);
	}

	track_status->chg_end_time = oplus_chg_track_get_local_time_s();
	track_recording_time = track_status->chg_end_time - track_status->chg_start_time;
	if (!track_record_charger_info && track_recording_time <= TRACK_CYCLE_RECORDIING_TIME_90S)
		oplus_chg_track_get_charger_type(chip, track_status, TRACK_CHG_GET_THTS_TIME_TYPE);

	oplus_chg_track_cal_tbatt_status(chip, track_status);
	oplus_chg_track_cal_section_soc_inc_rm(chip, track_status);
	oplus_chg_track_cal_batt_full_time(chip, track_status);
	oplus_chg_track_cal_chg_common_mesg(chip, track_status);
	oplus_chg_track_cal_cool_down_stats(chip, track_status);
	oplus_chg_track_cal_no_charging_stats(chip, track_status);
	oplus_chg_track_cal_led_on_stats(chip, track_status);
	oplus_chg_track_check_chg_abnormal(chip, track_status);
	oplus_chg_track_cal_rechg_counts(chip, track_status);
	oplus_chg_track_cal_chg_end_soc(chip, track_status);
	oplus_chg_track_cal_hyper_speed_status(chip, track_status);

	track_status->prop_status = chip->prop_status;
	if (!track_record_charger_info && chip->prop_status == POWER_SUPPLY_STATUS_FULL &&
	    track_recording_time < TRACK_CYCLE_RECORDIING_TIME_2MIN)
		oplus_chg_track_cancel_cal_period_chg_capaticy(g_track_chip);

	if (!track_record_charger_info && track_recording_time >= TRACK_CYCLE_RECORDIING_TIME_2MIN)
		track_record_charger_info = true;

	pr_debug("track_recording_time=%d, track_record_charger_info=%d\n", track_recording_time,
		track_record_charger_info);

	return ret;
}

static int oplus_chg_track_uisoc_soc_jump_check(struct oplus_chg_chip *chip)
{
	int ret = 0;
	int curr_time_utc;
	int curr_vbatt;
	struct oplus_chg_track_status *track_status;
	int judge_curr_soc = 0;

	if (!g_track_chip)
		return -EFAULT;

	if (!chip->is_gauge_ready) {
		chg_err("gauge is not ready, waiting...\n");
		return ret;
	}

	curr_time_utc = oplus_chg_track_get_local_time_s();
	curr_vbatt = chip->batt_volt;

	track_status = &g_track_chip->track_status;
	if (track_status->curr_soc == -EINVAL) {
		track_status->curr_soc = chip->soc;
		track_status->pre_soc = chip->soc;
		track_status->curr_uisoc = chip->ui_soc;
		track_status->pre_uisoc = chip->ui_soc;
		track_status->pre_vbatt = curr_vbatt;
		track_status->pre_time_utc = curr_time_utc;
		if (chip->rsd.smooth_switch_v2 && chip->rsd.reserve_soc)
			judge_curr_soc =
				track_status->curr_soc * OPLUS_FULL_SOC / (OPLUS_FULL_SOC - chip->rsd.reserve_soc);
		else
			judge_curr_soc = track_status->curr_soc;
		if (abs(track_status->curr_uisoc - judge_curr_soc) > OPLUS_CHG_TRACK_UI_S0C_LOAD_JUMP_THD) {
			track_status->uisoc_load_jumped = true;
			pr_debug("The gap between loaded uisoc and soc is too "
				"large\n");
			memset(g_track_chip->uisoc_load_trigger.crux_info, 0,
			       sizeof(g_track_chip->uisoc_load_trigger.crux_info));
			ret = snprintf(g_track_chip->uisoc_load_trigger.crux_info, OPLUS_CHG_TRACK_CURX_INFO_LEN,
				       "$$curr_uisoc@@%d"
				       "$$curr_soc@@%d$$load_uisoc_soc_gap@@%d"
				       "$$pre_vbatt@@%d$$curr_vbatt@@%d"
				       "$$pre_time_utc@@%d$$curr_time_utc@@%d"
				       "$$charger_exist@@%d",
				       track_status->curr_uisoc, track_status->curr_soc,
				       track_status->curr_uisoc - track_status->curr_soc, track_status->pre_vbatt,
				       curr_vbatt, track_status->pre_time_utc, curr_time_utc, chip->charger_exist);
			schedule_delayed_work(&g_track_chip->uisoc_load_trigger_work, msecs_to_jiffies(TRACK_TIME_SCHEDULE_UI_SOC_LOAD_JUMP));
		}
	} else {
		track_status->curr_soc = track_status->debug_soc != OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID ?
						 track_status->debug_soc :
						 chip->soc;
		track_status->curr_uisoc = track_status->debug_uisoc != OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID ?
						   track_status->debug_uisoc :
						   chip->ui_soc;
	}

	if (!track_status->soc_jumped &&
	    abs(track_status->curr_soc - track_status->pre_soc) > OPLUS_CHG_TRACK_S0C_JUMP_THD) {
		track_status->soc_jumped = true;
		pr_debug("The gap between curr_soc and pre_soc is too large\n");
		memset(g_track_chip->soc_trigger.crux_info, 0, sizeof(g_track_chip->soc_trigger.crux_info));
		ret = snprintf(g_track_chip->soc_trigger.crux_info, OPLUS_CHG_TRACK_CURX_INFO_LEN,
			       "$$curr_soc@@%d"
			       "$$pre_soc@@%d$$curr_soc_pre_soc_gap@@%d"
			       "$$pre_vbatt@@%d$$curr_vbatt@@%d"
			       "$$pre_time_utc@@%d$$curr_time_utc@@%d"
			       "$$charger_exist@@%d",
			       track_status->curr_soc, track_status->pre_soc,
			       track_status->curr_soc - track_status->pre_soc, track_status->pre_vbatt, curr_vbatt,
			       track_status->pre_time_utc, curr_time_utc, chip->charger_exist);
		schedule_delayed_work(&g_track_chip->soc_trigger_work, 0);
	} else {
		if (track_status->soc_jumped && track_status->curr_soc == track_status->pre_soc)
			track_status->soc_jumped = false;
	}

	if (!track_status->uisoc_jumped &&
	    abs(track_status->curr_uisoc - track_status->pre_uisoc) > OPLUS_CHG_TRACK_UI_S0C_JUMP_THD) {
		track_status->uisoc_jumped = true;
		pr_debug("The gap between curr_uisoc and pre_uisoc is too "
			"large\n");
		memset(g_track_chip->uisoc_trigger.crux_info, 0, sizeof(g_track_chip->uisoc_trigger.crux_info));
		ret = snprintf(g_track_chip->uisoc_trigger.crux_info, OPLUS_CHG_TRACK_CURX_INFO_LEN,
			       "$$curr_uisoc@@%d"
			       "$$pre_uisoc@@%d$$curr_uisoc_pre_uisoc_gap@@%d"
			       "$$pre_vbatt@@%d$$curr_vbatt@@%d"
			       "$$pre_time_utc@@%d$$curr_time_utc@@%d"
			       "$$charger_exist@@%d",
			       track_status->curr_uisoc, track_status->pre_uisoc,
			       track_status->curr_uisoc - track_status->pre_uisoc, track_status->pre_vbatt, curr_vbatt,
			       track_status->pre_time_utc, curr_time_utc, chip->charger_exist);
		schedule_delayed_work(&g_track_chip->uisoc_trigger_work, 0);
	} else {
		if (track_status->uisoc_jumped && track_status->curr_uisoc == track_status->pre_uisoc)
			track_status->uisoc_jumped = false;
	}

	if (chip->rsd.smooth_switch_v2 && chip->rsd.reserve_soc)
		judge_curr_soc = track_status->curr_soc * OPLUS_FULL_SOC / (OPLUS_FULL_SOC - chip->rsd.reserve_soc);
	else
		judge_curr_soc = track_status->curr_soc;

	if (!track_status->uisoc_to_soc_jumped && !track_status->uisoc_load_jumped &&
	    abs(track_status->curr_uisoc - judge_curr_soc) > OPLUS_CHG_TRACK_UI_SOC_TO_S0C_JUMP_THD) {
		track_status->uisoc_to_soc_jumped = true;
		memset(g_track_chip->uisoc_to_soc_trigger.crux_info, 0,
		       sizeof(g_track_chip->uisoc_to_soc_trigger.crux_info));
		ret = snprintf(g_track_chip->uisoc_to_soc_trigger.crux_info, OPLUS_CHG_TRACK_CURX_INFO_LEN,
			       "$$curr_uisoc@@%d"
			       "$$curr_soc@@%d$$curr_uisoc_curr_soc_gap@@%d"
			       "$$pre_vbatt@@%d$$curr_vbatt@@%d"
			       "$$pre_time_utc@@%d$$curr_time_utc@@%d"
			       "$$charger_exist@@%d",
			       track_status->curr_uisoc, track_status->curr_soc,
			       track_status->curr_uisoc - track_status->curr_soc, track_status->pre_vbatt, curr_vbatt,
			       track_status->pre_time_utc, curr_time_utc, chip->charger_exist);
		schedule_delayed_work(&g_track_chip->uisoc_to_soc_trigger_work, 0);
	} else {
		if (track_status->curr_uisoc == judge_curr_soc) {
			track_status->uisoc_to_soc_jumped = false;
			track_status->uisoc_load_jumped = false;
		}
	}

	pr_debug("debug_soc:0x%x, debug_uisoc:0x%x, pre_soc:%d, curr_soc:%d,\
		pre_uisoc:%d, curr_uisoc:%d\n",
		track_status->debug_soc, track_status->debug_uisoc, track_status->pre_soc, track_status->curr_soc,
		track_status->pre_uisoc, track_status->curr_uisoc);

	track_status->pre_soc = track_status->curr_soc;
	track_status->pre_uisoc = track_status->curr_uisoc;
	track_status->pre_vbatt = curr_vbatt;
	track_status->pre_time_utc = curr_time_utc;

	return ret;
}

int oplus_chg_track_comm_monitor(void)
{
	int ret = 0;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!chip)
		return -EFAULT;

	ret = oplus_chg_track_uisoc_soc_jump_check(chip);
	ret |= oplus_chg_track_speed_check(chip);

	return ret;
}

static int oplus_chg_track_debugfs_init(struct oplus_chg_track *track_dev)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_general;
	struct dentry *debugfs_chg_slow;
	struct dentry *debugfs_adsp;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_general = debugfs_create_dir("general", debugfs_root);
	if (!debugfs_general) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_chg_slow = debugfs_create_dir("chg_slow", debugfs_general);
	if (!debugfs_chg_slow) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_adsp =
		debugfs_create_file("adsp_debug", S_IFREG | 0444, debugfs_root, track_dev, &adsp_track_debug_ops);
	if (!debugfs_adsp) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_create_u8("debug_soc", 0644, debugfs_general, &(track_dev->track_status.debug_soc));
	debugfs_create_u8("debug_uisoc", 0644, debugfs_general, &(track_dev->track_status.debug_uisoc));
	debugfs_create_u8("debug_fast_prop_status", 0644, debugfs_general,
			  &(track_dev->track_status.debug_fast_prop_status));
	debugfs_create_u8("debug_normal_charging_state", 0644, debugfs_general,
			  &(track_dev->track_status.debug_normal_charging_state));
	debugfs_create_u8("debug_normal_prop_status", 0644, debugfs_general,
			  &(track_dev->track_status.debug_normal_prop_status));
	debugfs_create_u8("debug_no_charging", 0644, debugfs_general, &(track_dev->track_status.debug_no_charging));
	debugfs_create_u8("debug_slow_charging_force", 0644, debugfs_chg_slow,
			  &(track_dev->track_status.debug_slow_charging_force));
	debugfs_create_u8("debug_slow_charging_reason", 0644, debugfs_chg_slow,
			  &(track_dev->track_status.debug_slow_charging_reason));
	debugfs_create_u32("debug_fast_chg_break_t_thd", 0644, debugfs_general,
			   &(track_dev->track_cfg.fast_chg_break_t_thd));
	debugfs_create_u32("debug_ufcs_chg_break_t_thd", 0644, debugfs_general,
			   &(track_dev->track_cfg.ufcs_chg_break_t_thd));
	debugfs_create_u32("debug_general_chg_break_t_thd", 0644, debugfs_general,
			   &(track_dev->track_cfg.general_chg_break_t_thd));
	debugfs_create_u32("debug_wls_chg_break_t_thd", 0644, debugfs_general,
			   &(track_dev->track_cfg.wls_chg_break_t_thd));
	debugfs_create_u32("debug_chg_notify_flag", 0644, debugfs_general,
			   &(track_dev->track_status.debug_chg_notify_flag));
	debugfs_create_u32("debug_chg_notify_code", 0644, debugfs_general,
			   &(track_dev->track_status.debug_chg_notify_code));

	return ret;
}

static int oplus_chg_track_driver_probe(struct platform_device *pdev)
{
	struct oplus_chg_track *track_dev;
	int rc;

	track_dev = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_track), GFP_KERNEL);
	if (!track_dev) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}

	rc = kfifo_alloc(&(track_dev->adsp_fifo), (ADSP_TRACK_FIFO_NUMS * sizeof(adsp_track_trigger)), GFP_KERNEL);
	if (rc) {
		pr_err("kfifo_alloc error\n");
		rc = -ENOMEM;
		goto kfifo_err;
	}

	track_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, track_dev);

	track_dev->track_status.bcc_info = (struct oplus_chg_track_hidl_bcc_info *)kzalloc(
		sizeof(struct oplus_chg_track_hidl_bcc_info), GFP_KERNEL);
	if (!track_dev->track_status.bcc_info) {
		rc = -ENOMEM;
		goto bcc_info_kzmalloc_fail;
	}

	rc = oplus_chg_track_debugfs_init(track_dev);
	if (rc < 0) {
		rc = -ENOENT;
		pr_err("oplus chg track debugfs init fail, rc=%d\n", rc);
		goto debugfs_create_fail;
	}

	rc = oplus_chg_track_parse_dt(track_dev);
	if (rc < 0) {
		pr_err("oplus chg track parse dts error, rc=%d\n", rc);
		goto parse_dt_err;
	}

	oplus_chg_track_init(track_dev);
	rc = oplus_chg_track_thread_init(track_dev);
	if (rc < 0) {
		pr_err("oplus chg track mod init error, rc=%d\n", rc);
		goto track_kthread_init_err;
	}

	oplus_chg_track_bcc_err_init(track_dev);
	oplus_chg_track_wls_third_err_init(track_dev);
	oplus_chg_track_uisoh_err_init(track_dev);

	rc = oplus_chg_adsp_track_thread_init(track_dev);
	if (rc < 0) {
		pr_err("oplus chg adsp track mod init error, rc=%d\n", rc);
		goto adsp_track_kthread_init_err;
	}

	track_dev->trigger_upload_wq = create_workqueue("oplus_chg_trigger_upload_wq");

	rc = oplus_chg_track_init_mod(track_dev);
	if (rc < 0) {
		pr_err("oplus chg track mod init error, rc=%d\n", rc);
		goto track_mod_init_err;
	}
	INIT_DELAYED_WORK(&track_dev->upload_info_dwork, oplus_chg_track_upload_info_dwork);
	g_track_chip = track_dev;
	pr_debug("probe done\n");

	return 0;

track_mod_init_err:
adsp_track_kthread_init_err:
track_kthread_init_err:
parse_dt_err:
debugfs_create_fail:
bcc_info_kzmalloc_fail:
	kfifo_free(&(track_dev->adsp_fifo));
kfifo_err:
	devm_kfree(&pdev->dev, track_dev);
	return rc;
}

static int oplus_chg_track_driver_remove(struct platform_device *pdev)
{
	struct oplus_chg_track *track_dev = platform_get_drvdata(pdev);

	oplus_chg_mod_unregister(track_dev->track_ocm);

	if (track_debugfs_root)
		debugfs_remove_recursive(track_debugfs_root);
	kfree(track_dev->track_status.bcc_info);
	kfifo_free(&(track_dev->adsp_fifo));
	devm_kfree(&pdev->dev, track_dev);
	return 0;
}

static const struct of_device_id oplus_chg_track_match[] = {
	{ .compatible = "oplus,track-charge" },
	{},
};

static struct platform_driver oplus_chg_track_driver = {
	.driver =
		{
			.name = "oplus_chg_track",
			.owner = THIS_MODULE,
			.of_match_table = of_match_ptr(oplus_chg_track_match),
		},
	.probe = oplus_chg_track_driver_probe,
	.remove = oplus_chg_track_driver_remove,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static __init int oplus_chg_track_driver_init(void)
{
	return platform_driver_register(&oplus_chg_track_driver);
}

static __exit void oplus_chg_track_driver_exit(void)
{
	platform_driver_unregister(&oplus_chg_track_driver);
}

oplus_chg_module_register(oplus_chg_track_driver);
#else
module_platform_driver(oplus_chg_track_driver);
#endif
