#define pr_fmt(fmt) "[TRACK]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/power_supply.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/rtc.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/sched/clock.h>
#include <soc/oplus/system/boot_mode.h>

#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_comm.h>
#include "oplus_monitor_internal.h"
#include "oplus_chg_track.h"

#define OPLUS_CHG_TRACK_WAIT_TIME_MS			3000
#define OPLUS_CHG_UPDATE_INFO_DELAY_MS			500
#define OPLUS_CHG_TRIGGER_MSG_LEN			(1024 * 2)
#define OPLUS_CHG_TRIGGER_REASON_TAG_LEN		32
#define OPLUS_CHG_TRACK_LOG_TAG				"OplusCharger"
#define OPLUS_CHG_TRACK_EVENT_ID			"charge_monitor"
#define OPLUS_CHG_TRACK_DWORK_RETRY_CNT			3

#define OPLUS_CHG_TRACK_UI_S0C_LOAD_JUMP_THD		5
#define OPLUS_CHG_TRACK_S0C_JUMP_THD			3
#define OPLUS_CHG_TRACK_UI_S0C_JUMP_THD			5
#define OPLUS_CHG_TRACK_UI_SOC_TO_S0C_JUMP_THD		3
#define OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID		0XFF

#define OPLUS_CHG_TRACK_POWER_TYPE_LEN			24
#define OPLUS_CHG_TRACK_BATT_FULL_REASON_LEN		24
#define OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LEN		24
#define OPLUS_CHG_TRACK_COOL_DOWN_STATS_LEN		24
#define OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN		320
#define OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN	24
#define OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN		16
#define OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LENS	160

#define TRACK_WLS_ADAPTER_TYPE_UNKNOWN 			0x00
#define TRACK_WLS_ADAPTER_TYPE_VOOC 			0x01
#define TRACK_WLS_ADAPTER_TYPE_SVOOC 			0x02
#define TRACK_WLS_ADAPTER_TYPE_USB 			0x03
#define TRACK_WLS_ADAPTER_TYPE_NORMAL 			0x04
#define TRACK_WLS_ADAPTER_TYPE_EPP 			0x05
#define TRACK_WLS_ADAPTER_TYPE_SVOOC_50W 		0X06
#define TRACK_WLS_ADAPTER_TYPE_PD_65W 			0x07

#define TRACK_WLS_DOCK_MODEL_0				0x00
#define TRACK_WLS_DOCK_MODEL_1				0x01
#define TRACK_WLS_DOCK_MODEL_2				0x02
#define TRACK_WLS_DOCK_MODEL_3				0x03
#define TRACK_WLS_DOCK_MODEL_4				0x04
#define TRACK_WLS_DOCK_MODEL_5				0x05
#define TRACK_WLS_DOCK_MODEL_6				0x06
#define TRACK_WLS_DOCK_MODEL_7				0x07
#define TRACK_WLS_DOCK_MODEL_8				0x08
#define TRACK_WLS_DOCK_MODEL_9				0x09
#define TRACK_WLS_DOCK_MODEL_10				0x0a
#define TRACK_WLS_DOCK_MODEL_11				0x0b
#define TRACK_WLS_DOCK_MODEL_12				0x0c
#define TRACK_WLS_DOCK_MODEL_13				0x0d
#define TRACK_WLS_DOCK_MODEL_14				0x0e
#define TRACK_WLS_DOCK_MODEL_15				0x0F
#define TRACK_WLS_DOCK_THIRD_PARTY			0X1F

#define TRACK_POWER_2000MW				2000
#define TRACK_POWER_2500MW				2500
#define TRACK_POWER_5000MW				5000
#define TRACK_POWER_6000MW				6000
#define TRACK_POWER_7000MW				7000
#define TRACK_POWER_7500MW				7500
#define TRACK_POWER_10000MW				10000
#define TRACK_POWER_12000MW				12000
#define TRACK_POWER_15000MW				15000
#define TRACK_POWER_18000MW				18000
#define TRACK_POWER_20000MW				20000
#define TRACK_POWER_30000MW				30000
#define TRACK_POWER_40000MW				40000
#define TRACK_POWER_50000MW				50000
#define TRACK_POWER_65000MW				65000
#define TRACK_POWER_100000MW				100000
#define TRACK_POWER_120000MW				120000
#define TRACK_POWER_150000MW				150000

#define TRACK_INPUT_VOL_5000MV				5000
#define TRACK_INPUT_VOL_10000MV				10000
#define TRACK_INPUT_VOL_11000MV				11000
#define TRACK_INPUT_CURR_3000MA				3000
#define TRACK_INPUT_CURR_4000MA				4000
#define TRACK_INPUT_CURR_5000MA				5000
#define TRACK_INPUT_CURR_6000MA				6000
#define TRACK_INPUT_CURR_6100MA				6100
#define TRACK_INPUT_CURR_6500MA				6500
#define TRACK_INPUT_CURR_7300MA				7300
#define TRACK_INPUT_CURR_8000MA				8000
#define TRACK_INPUT_CURR_9100MA				9100
#define TRACK_INPUT_CURR_11000MA			11000

#define TRACK_CYCLE_RECORDIING_TIME_2MIN		120
#define TRACK_CYCLE_RECORDIING_TIME_90S			90

#define TRACK_TIME_1MIN_THD				60
#define TRACK_TIME_30MIN_THD				1800
#define TRACK_TIME_1HOU_THD				3600
#define TRACK_TIME_7S_JIFF_THD				(7 * 1000)
#define TRACK_TIME_500MS_JIFF_THD			(5 * 100)
#define TRACK_TIME_1000MS_JIFF_THD			(1 * 1000)
#define TRACK_TIME_5MIN_JIFF_THD			(5 * 60 * 1000)
#define TRACK_TIME_10MIN_JIFF_THD			(10 * 60 * 1000)
#define TRACK_THRAD_PERIOD_TIME_S			5
#define TRACK_NO_CHRGING_TIME_PCT			70
#define TRACK_COOLDOWN_CHRGING_TIME_PCT			70
#define TRACK_WLS_SKEWING_CHRGING_TIME_PCT		2

#define TRACK_PERIOD_CHG_CAP_MAX_SOC			100
#define TRACK_PERIOD_CHG_CAP_INIT			(-101)
#define TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT		(-10000)
#define TRACK_T_THD_1000_MS				1000
#define TRACK_T_THD_500_MS				500
#define TRACK_T_THD_6000_MS				6000

#define TRACK_LOCAL_T_NS_TO_MS_THD			1000000
#define TRACK_LOCAL_T_NS_TO_S_THD			1000000000

#define TRACK_LED_MONITOR_SOC_POINT			98
#define TRACK_CHG_VOOC_BATT_VOL_DIFF_MV			100

#define TRACK_REF_SOC_50				50
#define TRACK_REF_SOC_75				75
#define TRACK_REF_SOC_90				90
#define TRACK_REF_VOL_5000MV				5000
#define TRACK_REF_VOL_10000MV				10000
#define TRACK_REF_TIME_6S				6
#define TRACK_REF_TIME_8S				8
#define TRACK_REF_TIME_10S				10

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

struct oplus_chg_track_speed_ref {
	int ref_soc;
	int ref_power;
	int ref_curr;
};

struct oplus_chg_track_status {
	int curr_soc;
	int pre_soc;
	int curr_uisoc;
	int pre_uisoc;
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
	int pre_wired_type;

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
	int chg_fast_full_time;
	int chg_normal_full_time;
	int chg_report_full_time;
	bool has_judge_speed;
	bool aging_ffc_trig;
	int aging_ffc_judge_vol[FFC_CHG_STEP_MAX];
	int aging_ffc_start_time;
	int aging_ffc_to_full_time;
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
	char wired_break_crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	char wls_break_crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];

	bool mmi_chg;
	bool once_mmi_chg;
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
	int once_chg_cycle_status;
};

struct oplus_chg_track {
	struct device *dev;
	struct oplus_monitor *monitor;
	struct task_struct *track_upload_kthread;

	struct mms_subscribe *err_subs;

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

	struct oplus_chg_track_status track_status;
	struct oplus_chg_track_cfg track_cfg;

	int uisoc_1_start_vbatt_max;
	int uisoc_1_start_vbatt_min;
	int uisoc_1_start_batt_rm;
	int uisoc_1_start_time;

	oplus_chg_track_trigger uisoc_load_trigger;
	oplus_chg_track_trigger soc_trigger;
	oplus_chg_track_trigger uisoc_trigger;
	oplus_chg_track_trigger uisoc_to_soc_trigger;
	oplus_chg_track_trigger charger_info_trigger;
	oplus_chg_track_trigger no_charging_trigger;
	oplus_chg_track_trigger slow_charging_trigger;
	oplus_chg_track_trigger charging_break_trigger;
	oplus_chg_track_trigger wls_charging_break_trigger;
	oplus_chg_track_trigger usbtemp_load_trigger;
	oplus_chg_track_trigger vbatt_too_low_load_trigger;
	oplus_chg_track_trigger vbatt_diff_over_load_trigger;
	oplus_chg_track_trigger uisoc_keep_1_t_load_trigger;
	oplus_chg_track_trigger ic_err_msg_load_trigger;
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
	struct delayed_work usbtemp_load_trigger_work;
	struct delayed_work vbatt_too_low_load_trigger_work;
	struct delayed_work vbatt_diff_over_load_trigger_work;
	struct delayed_work uisoc_keep_1_t_load_trigger_work;
	struct delayed_work ic_err_msg_trigger_work;

	char voocphy_name[OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN];

	struct mutex access_lock;
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

static int
oplus_chg_track_get_charger_type(struct oplus_monitor *monitor,
				 struct oplus_chg_track_status *track_status,
				 int type);
static int oplus_chg_track_obtain_wls_break_sub_crux_info(
	struct oplus_chg_track *track_chip, char *crux_info);
static int oplus_chg_track_upload_trigger_data(oplus_chg_track_trigger data);
static bool oplus_chg_track_get_mmi_chg(void);

static struct oplus_chg_track_type wired_type_table[] = {
	{ OPLUS_CHG_USB_TYPE_UNKNOWN, TRACK_POWER_2500MW, "unknow" },
	{ OPLUS_CHG_USB_TYPE_SDP, TRACK_POWER_2500MW, "sdp" },
	{ OPLUS_CHG_USB_TYPE_DCP, TRACK_POWER_10000MW, "dcp" },
	{ OPLUS_CHG_USB_TYPE_CDP, TRACK_POWER_7500MW, "cdp" },
	{ OPLUS_CHG_USB_TYPE_ACA, TRACK_POWER_10000MW, "dcp" },
	{ OPLUS_CHG_USB_TYPE_C, TRACK_POWER_10000MW, "dcp" },
	{ OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID, TRACK_POWER_10000MW, "dcp" },
	{ OPLUS_CHG_USB_TYPE_PD_SDP, TRACK_POWER_10000MW, "sdp" },
	{ OPLUS_CHG_USB_TYPE_PD, TRACK_POWER_18000MW, "pd" },
	{ OPLUS_CHG_USB_TYPE_PD_DRP, TRACK_POWER_18000MW, "pd" },
	{ OPLUS_CHG_USB_TYPE_QC2, TRACK_POWER_18000MW, "qc" },
	{ OPLUS_CHG_USB_TYPE_QC3, TRACK_POWER_18000MW, "qc" },
	{ OPLUS_CHG_USB_TYPE_PD_PPS, TRACK_POWER_30000MW, "pps" },
	{ OPLUS_CHG_USB_TYPE_VOOC, TRACK_POWER_20000MW, "vooc" },
	{ OPLUS_CHG_USB_TYPE_SVOOC, 0, "svooc" },
	{ OPLUS_CHG_USB_TYPE_UFCS, TRACK_POWER_100000MW, "ufcs" },
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static struct oplus_chg_track_type wls_adapter_type_table[] = {
	{ OPLUS_CHG_WLS_UNKNOWN, TRACK_POWER_5000MW, "unknow" },
	{ OPLUS_CHG_WLS_VOOC, TRACK_POWER_20000MW, "airvooc" },
	{ OPLUS_CHG_WLS_BPP, TRACK_POWER_5000MW, "bpp" },
	{ OPLUS_CHG_WLS_EPP, TRACK_POWER_10000MW, "epp" },
	{ OPLUS_CHG_WLS_EPP_PLUS, TRACK_POWER_10000MW, "epp" },
	{ OPLUS_CHG_WLS_SVOOC, TRACK_POWER_50000MW, "airsvooc" },
	{ OPLUS_CHG_WLS_PD_65W, TRACK_POWER_65000MW, "airsvooc" },
};
#else
static struct oplus_chg_track_type wls_adapter_type_table[] = {
	{ TRACK_WLS_ADAPTER_TYPE_UNKNOWN, TRACK_POWER_5000MW, "unknow" },
	{ TRACK_WLS_ADAPTER_TYPE_VOOC, TRACK_POWER_20000MW, "airvooc" },
	{ TRACK_WLS_ADAPTER_TYPE_SVOOC, TRACK_POWER_50000MW, "airsvooc" },
	{ TRACK_WLS_ADAPTER_TYPE_USB, TRACK_POWER_5000MW, "bpp" },
	{ TRACK_WLS_ADAPTER_TYPE_NORMAL, TRACK_POWER_5000MW, "bpp" },
	{ TRACK_WLS_ADAPTER_TYPE_EPP, TRACK_POWER_10000MW, "epp" },
	{ TRACK_WLS_ADAPTER_TYPE_SVOOC_50W, TRACK_POWER_50000MW, "airsvooc" },
	{ TRACK_WLS_ADAPTER_TYPE_PD_65W, TRACK_POWER_65000MW, "airsvooc" },
};
#endif

static struct oplus_chg_track_type wls_dock_type_table[] = {
	{ TRACK_WLS_DOCK_MODEL_0, TRACK_POWER_30000MW, "model_0" },
	{ TRACK_WLS_DOCK_MODEL_1, TRACK_POWER_40000MW, "model_1" },
	{ TRACK_WLS_DOCK_MODEL_2, TRACK_POWER_50000MW, "model_2" },
	{ TRACK_WLS_DOCK_MODEL_3, TRACK_POWER_30000MW, "model_3" },
	{ TRACK_WLS_DOCK_MODEL_4, TRACK_POWER_30000MW, "model_4" },
	{ TRACK_WLS_DOCK_MODEL_5, TRACK_POWER_30000MW, "model_5" },
	{ TRACK_WLS_DOCK_MODEL_6, TRACK_POWER_30000MW, "model_6" },
	{ TRACK_WLS_DOCK_MODEL_7, TRACK_POWER_30000MW, "model_7" },
	{ TRACK_WLS_DOCK_MODEL_8, TRACK_POWER_30000MW, "model_8" },
	{ TRACK_WLS_DOCK_MODEL_9, TRACK_POWER_30000MW, "model_9" },
	{ TRACK_WLS_DOCK_MODEL_10, TRACK_POWER_30000MW, "model_10" },
	{ TRACK_WLS_DOCK_MODEL_11, TRACK_POWER_30000MW, "model_11" },
	{ TRACK_WLS_DOCK_MODEL_12, TRACK_POWER_30000MW, "model_12" },
	{ TRACK_WLS_DOCK_MODEL_13, TRACK_POWER_30000MW, "model_13" },
	{ TRACK_WLS_DOCK_MODEL_14, TRACK_POWER_30000MW, "model_14" },
	{ TRACK_WLS_DOCK_MODEL_15, TRACK_POWER_30000MW, "model_15" },
	{ TRACK_WLS_DOCK_THIRD_PARTY, TRACK_POWER_50000MW, "third_party" },
};

static struct oplus_chg_track_vooc_type vooc_type_table[] = {
	{ 0x01, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_4000MA, "vooc" },
	{ 0x13, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_4000MA, "vooc" },
	{ 0x34, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_4000MA, "vooc" },
	{ 0x45, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_4000MA, "vooc" },

	{ 0x19, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_6000MA, "vooc" },
	{ 0x29, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_6000MA, "vooc" },
	{ 0x41, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_6000MA, "vooc" },
	{ 0x42, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_6000MA, "vooc" },
	{ 0x43, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_6000MA, "vooc" },
	{ 0x44, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_6000MA, "vooc" },
	{ 0x46, TRACK_INPUT_VOL_5000MV, TRACK_INPUT_CURR_6000MA, "vooc" },

	{ 0x61, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_3000MA, "svooc" },
	{ 0x49, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_3000MA, "svooc" },
	{ 0x4A, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_3000MA, "svooc" },

	{ 0x11, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_5000MA, "svooc" },
	{ 0x12, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_5000MA, "svooc" },
	{ 0x21, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_5000MA, "svooc" },
	{ 0x31, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_5000MA, "svooc" },
	{ 0x33, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_5000MA, "svooc" },
	{ 0x62, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_5000MA, "svooc" },

	{ 0x14, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_6500MA, "svooc" },
	{ 0x35, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_6500MA, "svooc" },
	{ 0x63, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_6500MA, "svooc" },
	{ 0x6E, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_6500MA, "svooc" },
	{ 0x66, TRACK_INPUT_VOL_10000MV, TRACK_INPUT_CURR_6500MA, "svooc" },

	{ 0x36, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_6000MA, "svooc" },
	{ 0x64, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_6000MA, "svooc" },

	{ 0x6C, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_6100MA, "svooc" },
	{ 0x6D, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_6100MA, "svooc" },

	{ 0x65, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_7300MA, "svooc" },
	{ 0x4B, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_7300MA, "svooc" },
	{ 0x4C, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_7300MA, "svooc" },
	{ 0x4D, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_7300MA, "svooc" },
	{ 0x4E, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_7300MA, "svooc" },

	{ 0x6A, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_9100MA, "svooc" },
	{ 0x69, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_9100MA, "svooc" },

	{ 0x6b, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_11000MA, "svooc" },
	{ 0x32, TRACK_INPUT_VOL_11000MV, TRACK_INPUT_CURR_11000MA, "svooc" },
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

static struct oplus_chg_track_cool_down_stats cool_down_stats_table[] = {
	{ 0, 0, "L_0" },   { 1, 0, "L_1" },   { 2, 0, "L_2" },
	{ 3, 0, "L_3" },   { 4, 0, "L_4" },   { 5, 0, "L_5" },
	{ 6, 0, "L_6" },   { 7, 0, "L_7" },   { 8, 0, "L_8" },
	{ 9, 0, "L_9" },   { 10, 0, "L_10" }, { 11, 0, "L_11" },
	{ 12, 0, "L_12" }, { 13, 0, "L_13" }, { 14, 0, "L_14" },
	{ 15, 0, "L_15" }, { 16, 0, "L_16" }, { 17, 0, "L_17" },
	{ 18, 0, "L_18" }, { 19, 0, "L_19" }, { 20, 0, "L_20" },
	{ 21, 0, "L_21" }, { 22, 0, "L_22" }, { 23, 0, "L_23" },
	{ 24, 0, "L_24" }, { 25, 0, "L_25" }, { 26, 0, "L_26" },
	{ 27, 0, "L_27" }, { 28, 0, "L_28" }, { 29, 0, "L_29" },
	{ 30, 0, "L_30" }, { 31, 0, "L_31" },
};

static struct oplus_chg_track_fastchg_break mcu_voocphy_break_table[] = {
	{ TRACK_MCU_VOOCPHY_FAST_ABSENT, "absent" },
	{ TRACK_MCU_VOOCPHY_BAD_CONNECTED, "bad_connect" },
	{ TRACK_MCU_VOOCPHY_BTB_TEMP_OVER, "btb_temp_over" },
	{ TRACK_MCU_VOOCPHY_DATA_ERROR, "data_error" },
	{ TRACK_MCU_VOOCPHY_OTHER, "other" },
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

static struct oplus_chg_track_speed_ref wired_series_double_cell_125w_150w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_20000MW,
	  TRACK_POWER_20000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_18000MW,
	  TRACK_POWER_18000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_12000MW,
	  TRACK_POWER_12000MW * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref
	wired_series_double_cell_65w_80w_100w[] = {
		{ TRACK_REF_SOC_50, TRACK_POWER_15000MW,
		  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_10000MV },
		{ TRACK_REF_SOC_75, TRACK_POWER_15000MW,
		  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_10000MV },
		{ TRACK_REF_SOC_90, TRACK_POWER_12000MW,
		  TRACK_POWER_12000MW * 1000 / TRACK_REF_VOL_10000MV },
	};

static struct oplus_chg_track_speed_ref wired_equ_single_cell_60w_67w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_15000MW,
	  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_15000MW,
	  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_12000MW,
	  TRACK_POWER_12000MW * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wired_equ_single_cell_30w_33w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_15000MW,
	  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_15000MW,
	  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_12000MW,
	  TRACK_POWER_12000MW * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wired_single_cell_18w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_10000MW,
	  TRACK_POWER_10000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_7000MW,
	  TRACK_POWER_7000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_6000MW,
	  TRACK_POWER_6000MW * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_40w_45w_50w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_15000MW,
	  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_15000MW,
	  TRACK_POWER_15000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_12000MW,
	  TRACK_POWER_12000MW * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_20w_30w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_10000MW,
	  TRACK_POWER_10000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_10000MW,
	  TRACK_POWER_10000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_10000MW,
	  TRACK_POWER_10000MW * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_12w_15w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_equ_single_cell_12w_15w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_epp15w_epp10w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_equ_single_cell_epp15w_epp10w[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_5000MW,
	  TRACK_POWER_5000MW * 1000 / TRACK_REF_VOL_5000MV },
};

static struct oplus_chg_track_speed_ref wls_series_double_cell_bpp[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_2000MW,
	  TRACK_POWER_2000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_2000MW,
	  TRACK_POWER_2000MW * 1000 / TRACK_REF_VOL_10000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_2500MW,
	  TRACK_POWER_2000MW * 1000 / TRACK_REF_VOL_10000MV },
};

static struct oplus_chg_track_speed_ref wls_equ_single_cell_bpp[] = {
	{ TRACK_REF_SOC_50, TRACK_POWER_2000MW,
	  TRACK_POWER_2000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_75, TRACK_POWER_2000MW,
	  TRACK_POWER_2000MW * 1000 / TRACK_REF_VOL_5000MV },
	{ TRACK_REF_SOC_90, TRACK_POWER_2000MW,
	  TRACK_POWER_2000MW * 1000 / TRACK_REF_VOL_5000MV },
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
static struct oplus_chg_track_speed_ref* g_wls_fast_speed_ref_standard[] = {
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

struct dentry *oplus_chg_track_get_debugfs_root(void)
{
	mutex_lock(&debugfs_root_mutex);
	if (!track_debugfs_root) {
		track_debugfs_root =
			debugfs_create_dir("oplus_chg_track", NULL);
	}
	mutex_unlock(&debugfs_root_mutex);

	return track_debugfs_root;
}

int __attribute__((weak))
oplus_chg_wls_get_break_sub_crux_info(struct device *dev, char *crux_info)
{
	return 0;
}

static int oplus_chg_track_clear_cool_down_stats_time(
	struct oplus_chg_track_status *track_status)
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
		if (voocphy_info_table[i].voocphy_type ==
		    track_chip->track_cfg.voocphy_type) {
			strncpy(track_chip->voocphy_name,
				voocphy_info_table[i].name,
				OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN - 1);
			break;
		}
	}

	if (i == ARRAY_SIZE(voocphy_info_table))
		strncpy(track_chip->voocphy_name, voocphy_info_table[0].name,
			OPLUS_CHG_TRACK_VOOCPHY_NAME_LEN - 1);

	return 0;
}

static int
oplus_chg_track_get_wired_type_info(int charge_type,
				    struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(wired_type_table); i++) {
		if (wired_type_table[i].type == charge_type) {
			strncpy(track_status->power_info.wired_info.adapter_type,
				wired_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wired_info.power =
				wired_type_table[i].power;
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int
oplus_chg_track_get_vooc_type_info(int vooc_type,
				   struct oplus_chg_track_status *track_status)
{
	int i;
	int vooc_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(vooc_type_table); i++) {
		if (vooc_type_table[i].chg_type == vooc_type) {
			strncpy(track_status->power_info.wired_info.adapter_type,
				vooc_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wired_info.power =
				vooc_type_table[i].vol *
				vooc_type_table[i].cur / 1000 / 500;
			track_status->power_info.wired_info.power *= 500;
			track_status->power_info.wired_info.adapter_id =
				vooc_type_table[i].chg_type;
			vooc_index = i;
			break;
		}
	}

	return vooc_index;
}

__maybe_unused static int oplus_chg_track_get_wls_adapter_type_info(
	int charge_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(wls_adapter_type_table); i++) {
		if (wls_adapter_type_table[i].type == charge_type) {
			strncpy(track_status->power_info.wls_info.adapter_type,
				wls_adapter_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			track_status->power_info.wls_info.power =
				wls_adapter_type_table[i].power;
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

__maybe_unused static int oplus_chg_track_get_wls_dock_type_info(
	int charge_type, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(wls_dock_type_table); i++) {
		if (wls_dock_type_table[i].type == charge_type) {
			strncpy(track_status->power_info.wls_info.dock_type,
				wls_dock_type_table[i].name,
				OPLUS_CHG_TRACK_POWER_TYPE_LEN - 1);
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_batt_full_reason_info(
	int notify_flag, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(batt_full_reason_table); i++) {
		if (batt_full_reason_table[i].notify_flag == notify_flag) {
			strncpy(track_status->batt_full_reason,
				batt_full_reason_table[i].reason,
				OPLUS_CHG_TRACK_BATT_FULL_REASON_LEN - 1);
			charge_index = i;
			break;
		}
	}

	return charge_index;
}

static int oplus_chg_track_get_chg_abnormal_reason_info(
	int notify_code, struct oplus_chg_track_status *track_status)
{
	int i;
	int charge_index = -EINVAL;
	int index;

	index = strlen(track_status->chg_abnormal_reason);
	for (i = 0; i < ARRAY_SIZE(chg_abnormal_reason_table); i++) {
		if (!chg_abnormal_reason_table[i].happened &&
		    chg_abnormal_reason_table[i].notify_code == notify_code) {
			chg_abnormal_reason_table[i].happened = true;
			if (!index)
				index += snprintf(
					&(track_status
						  ->chg_abnormal_reason[index]),
					OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LENS -
						index,
					"%s",
					chg_abnormal_reason_table[i].reason);
			else
				index += snprintf(
					&(track_status
						  ->chg_abnormal_reason[index]),
					OPLUS_CHG_TRACK_CHG_ABNORMAL_REASON_LENS -
						index,
					",%s",
					chg_abnormal_reason_table[i].reason);
			break;
			charge_index = i;
		}
	}

	return charge_index;
}

static int oplus_chg_track_parse_dt(struct oplus_chg_track *track_dev)
{
	int rc = 0;
	struct device_node *node = track_dev->dev->of_node;

	rc = of_property_read_u32(node, "track,fast_chg_break_t_thd",
				  &(track_dev->track_cfg.fast_chg_break_t_thd));
	if (rc < 0) {
		chg_err("track,fast_chg_break_t_thd reading failed, rc=%d\n",
			rc);
		track_dev->track_cfg.fast_chg_break_t_thd = TRACK_T_THD_1000_MS;
	}

	rc = of_property_read_u32(
		node, "track,general_chg_break_t_thd",
		&(track_dev->track_cfg.general_chg_break_t_thd));
	if (rc < 0) {
		chg_err("track,general_chg_break_t_thd reading failed, rc=%d\n",
			rc);
		track_dev->track_cfg.general_chg_break_t_thd =
			TRACK_T_THD_500_MS;
	}

	rc = of_property_read_u32(node, "track,voocphy_type",
				  &(track_dev->track_cfg.voocphy_type));
	if (rc < 0) {
		chg_err("track,voocphy_type reading failed, rc=%d\n", rc);
		track_dev->track_cfg.voocphy_type = TRACK_NO_VOOCPHY;
	}

	rc = of_property_read_u32(node, "track,wls_chg_break_t_thd",
				  &(track_dev->track_cfg.wls_chg_break_t_thd));
	if (rc < 0) {
		chg_err("track,wls_chg_break_t_thd reading failed, rc=%d\n",
			rc);
		track_dev->track_cfg.wls_chg_break_t_thd = TRACK_T_THD_6000_MS;
	}

	rc = of_property_read_u32(
		node, "track,wired_fast_chg_scheme",
		&(track_dev->track_cfg.wired_fast_chg_scheme));
	if (rc < 0) {
		chg_err("track,wired_fast_chg_scheme reading failed, rc=%d\n",
			rc);
		track_dev->track_cfg.wired_fast_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_fast_chg_scheme",
				  &(track_dev->track_cfg.wls_fast_chg_scheme));
	if (rc < 0) {
		chg_err("track,wls_fast_chg_scheme reading failed, rc=%d\n",
			rc);
		track_dev->track_cfg.wls_fast_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_epp_chg_scheme",
				  &(track_dev->track_cfg.wls_epp_chg_scheme));
	if (rc < 0) {
		chg_err("track,wls_epp_chg_scheme reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_epp_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_bpp_chg_scheme",
				  &(track_dev->track_cfg.wls_bpp_chg_scheme));
	if (rc < 0) {
		chg_err("track,wls_bpp_chg_scheme reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_bpp_chg_scheme = -1;
	}

	rc = of_property_read_u32(node, "track,wls_max_power",
				  &(track_dev->track_cfg.wls_max_power));
	if (rc < 0) {
		chg_err("track,wls_max_power reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wls_max_power = 0;
	}

	rc = of_property_read_u32(node, "track,wired_max_power",
				  &(track_dev->track_cfg.wired_max_power));
	if (rc < 0) {
		chg_err("track,wired_max_power reading failed, rc=%d\n", rc);
		track_dev->track_cfg.wired_max_power = 0;
	}

	rc = of_property_read_u32(node, "track,track_ver",
				  &(track_dev->track_cfg.track_ver));
	if (rc < 0) {
		chg_err("track,track_ver reading failed, rc=%d\n", rc);
		track_dev->track_cfg.track_ver = 3;
	}

	return 0;
}

static void oplus_chg_track_uisoc_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, uisoc_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_load_trigger);
}

static void oplus_chg_track_soc_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip =
		container_of(dwork, struct oplus_chg_track, soc_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->soc_trigger);
}

static void oplus_chg_track_uisoc_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip =
		container_of(dwork, struct oplus_chg_track, uisoc_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_trigger);
}

static void oplus_chg_track_uisoc_to_soc_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, uisoc_to_soc_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_to_soc_trigger);
}

static int
oplus_chg_track_record_general_info(struct oplus_monitor *monitor,
				    struct oplus_chg_track_status *track_status,
				    oplus_chg_track_trigger *p_trigger_data,
				    int index)
{
	if (!monitor || !p_trigger_data || !track_status)
		return -1;

	if (index < 0 || index >= OPLUS_CHG_TRACK_CURX_INFO_LEN) {
		chg_err("index is invalid\n");
		return -1;
	}

	index += snprintf(
		&(p_trigger_data->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$other@@BATTERY[%d %d %d %d %d %d %d %d %d %d 0x%x], "
		"CHARGE[%d %d %d %d], "
		"WIRED[%d %d %d %d %d 0x%x %d %d %d %d %d], "
		"WIRELESS[%d %d %d %d %d 0x%x %d %d], "
		"VOOC[%d %d %d %d 0x%x], "
		"COMMON[%d %d %d 0x%x %d], "
		"SLOW[%d %d %d %d %d %d %d]",
		monitor->batt_temp, monitor->shell_temp, monitor->vbat_mv,
		monitor->vbat_min_mv, monitor->ibat_ma, monitor->batt_soc,
		monitor->ui_soc, monitor->batt_rm, monitor->batt_fcc,
		monitor->batt_exist, monitor->batt_err_code, monitor->fv_mv,
		monitor->fcc_ma, monitor->chg_disable,
		monitor->chg_user_disable, monitor->wired_online,
		monitor->wired_ibus_ma, monitor->wired_vbus_mv,
		monitor->wired_icl_ma, monitor->wired_charge_type,
		monitor->wired_err_code, monitor->wired_suspend,
		monitor->wired_user_suspend, monitor->cc_mode,
		monitor->cc_detect, monitor->otg_enable, monitor->wls_online,
		monitor->wls_ibus_ma, monitor->wls_vbus_mv, monitor->wls_icl_ma,
		monitor->wls_charge_type, monitor->wls_err_code,
		monitor->wls_suspend, monitor->wls_user_suspend,
		monitor->vooc_online, monitor->vooc_started,
		monitor->vooc_charging, monitor->vooc_online_keep,
		monitor->vooc_sid, monitor->temp_region, monitor->ffc_status,
		monitor->cool_down, monitor->notify_code, monitor->led_on,
		track_status->has_judge_speed,
		track_status->soc_low_sect_incr_rm,
		track_status->soc_low_sect_cont_time,
		track_status->soc_medium_sect_incr_rm,
		track_status->soc_medium_sect_cont_time,
		track_status->soc_high_sect_incr_rm,
		track_status->soc_high_sect_cont_time);

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		if (strlen(track_status->wls_break_crux_info))
			index += snprintf(&(p_trigger_data->crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "%s ",
					  track_status->wls_break_crux_info);
	}
	chg_info("index:%d\n", index);
	return 0;
}

static int oplus_chg_track_pack_cool_down_stats(
	struct oplus_chg_track_status *track_status, char *cool_down_pack)
{
	int i;
	int index = 0;

	if (cool_down_pack == NULL || track_status == NULL)
		return -1;

	for (i = 0; i < ARRAY_SIZE(cool_down_stats_table) - 1; i++) {
		index += snprintf(&(cool_down_pack[index]),
				  OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN - index,
				  "%s,%d;", cool_down_stats_table[i].level_name,
				  cool_down_stats_table[i].time);
	}

	index += snprintf(&(cool_down_pack[index]),
			  OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN - index, "%s,%d",
			  cool_down_stats_table[i].level_name,
			  cool_down_stats_table[i].time *
				  TRACK_THRAD_PERIOD_TIME_S);
	chg_info("i=%d, cool_down_pack[%s]\n", i, cool_down_pack);

	return 0;
}

static void
oplus_chg_track_record_charger_info(struct oplus_monitor *monitor,
				    oplus_chg_track_trigger *p_trigger_data,
				    struct oplus_chg_track_status *track_status)
{
	int index = 0;
	char cool_down_pack[OPLUS_CHG_TRACK_COOL_DOWN_PACK_LEN] = { 0 };
	int i;

	if (monitor == NULL || p_trigger_data == NULL || track_status == NULL)
		return;

	memset(p_trigger_data->crux_info, 0, sizeof(p_trigger_data->crux_info));
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$power_mode@@%s",
			  track_status->power_info.power_mode);

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRE) {
		index += snprintf(
			&(p_trigger_data->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$adapter_t@@%s",
			track_status->power_info.wired_info.adapter_type);
		if (track_status->power_info.wired_info.adapter_id)
			index += snprintf(
				&(p_trigger_data->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$adapter_id@@0x%x",
				track_status->power_info.wired_info.adapter_id);
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$power@@%d",
				  track_status->power_info.wired_info.power);
	} else if (track_status->power_info.power_type ==
		   TRACK_CHG_TYPE_WIRELESS) {
		index += snprintf(
			&(p_trigger_data->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$adapter_t@@%s",
			track_status->power_info.wls_info.adapter_type);
		if (strlen(track_status->power_info.wls_info.dock_type))
			index += snprintf(
				&(p_trigger_data->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$dock_type@@%s",
				track_status->power_info.wls_info.dock_type);
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$power@@%d",
				  track_status->power_info.wls_info.power);
	}

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$start_soc@@%d", track_status->chg_start_soc);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$end_soc@@%d", track_status->chg_end_soc);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$start_temp@@%d", track_status->chg_start_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$max_temp@@%d", track_status->chg_max_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_start_temp@@%d",
			  track_status->batt_start_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_max_temp@@%d", track_status->batt_max_temp);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_max_vol@@%d", track_status->batt_max_vol);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_max_curr@@%d", track_status->batt_max_curr);

	index +=
		snprintf(&(p_trigger_data->crux_info[index]),
			 OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			 "$$ledon_time@@%d", track_status->continue_ledon_time);
	if (track_status->ledon_ave_speed)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$ledon_ave_speed@@%d",
				  track_status->ledon_ave_speed);

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$ledoff_time@@%d",
			  track_status->continue_ledoff_time);
	if (track_status->ledoff_ave_speed)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$ledoff_ave_speed@@%d",
				  track_status->ledoff_ave_speed);

	if (track_status->chg_five_mins_cap != TRACK_PERIOD_CHG_CAP_INIT)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_five_mins_cap@@%d",
				  track_status->chg_five_mins_cap);

	if (track_status->chg_ten_mins_cap != TRACK_PERIOD_CHG_CAP_INIT)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_ten_mins_cap@@%d",
				  track_status->chg_ten_mins_cap);

	if (track_status->chg_average_speed !=
	    TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_average_speed@@%d",
				  track_status->chg_average_speed);

	if (track_status->chg_fast_full_time)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$fast_full_time@@%d",
				  track_status->chg_fast_full_time);

	if (track_status->chg_report_full_time)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$report_full_time@@%d",
				  track_status->chg_report_full_time);

	if (track_status->chg_normal_full_time)
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$normal_full_time@@%d",
				  track_status->chg_normal_full_time);

	if (strlen(track_status->batt_full_reason))
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$full_reason@@%s",
				  track_status->batt_full_reason);

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$chg_warm_once@@%d", track_status->tbatt_warm_once);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_fcc@@%d", monitor->batt_fcc);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_soh@@%d", monitor->batt_soh);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$batt_cc@@%d", monitor->batt_cc);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$rechg_counts@@%d", track_status->rechg_counts);

	if (strlen(track_status->chg_abnormal_reason))
		index += snprintf(&(p_trigger_data->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$chg_abnormal@@%s",
				  track_status->chg_abnormal_reason);

	oplus_chg_track_pack_cool_down_stats(track_status, cool_down_pack);
	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$cool_down_sta@@%s", cool_down_pack);

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$plugin_utc_t@@%d", track_status->chg_plugin_utc_t);

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$plugout_utc_t@@%d",
			  track_status->chg_plugout_utc_t);

	if (track_status->aging_ffc_trig && index < OPLUS_CHG_TRACK_CURX_INFO_LEN) {
		index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$aging_ffc@@%d", track_status->aging_ffc_trig);
		for (i = 0; i < FFC_CHG_STEP_MAX; i++) {
			if (track_status->aging_ffc_judge_vol[i] <= 0 ||
			    index >= OPLUS_CHG_TRACK_CURX_INFO_LEN)
				break;
			index += snprintf(&(p_trigger_data->crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  ",%d", track_status->aging_ffc_judge_vol[i]);
		}
		if (track_status->aging_ffc_to_full_time && index < OPLUS_CHG_TRACK_CURX_INFO_LEN) {
			index += snprintf(&(p_trigger_data->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "$$aging_ffc_t@@%d", track_status->aging_ffc_to_full_time);
		}
	}

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$mmi_chg@@%d", track_status->once_mmi_chg);

	index += snprintf(&(p_trigger_data->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$chg_cycle_status@@%d", track_status->once_chg_cycle_status);

	oplus_chg_track_record_general_info(monitor, track_status,
					    p_trigger_data, index);
}

static void oplus_chg_track_charger_info_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, charger_info_trigger_work);

	if (!chip)
		return;

	chip->track_status.wls_need_upload = false;
	chip->track_status.wls_need_upload = false;
	oplus_chg_track_upload_trigger_data(chip->charger_info_trigger);
}

static void oplus_chg_track_no_charging_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, no_charging_trigger_work);

	if (!chip)
		return;

	chip->track_status.wls_need_upload = false;
	chip->track_status.wls_need_upload = false;
	oplus_chg_track_upload_trigger_data(chip->no_charging_trigger);
}

static void oplus_chg_track_slow_charging_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, slow_charging_trigger_work);

	if (!chip)
		return;

	chip->track_status.wls_need_upload = false;
	chip->track_status.wls_need_upload = false;
	oplus_chg_track_upload_trigger_data(chip->slow_charging_trigger);
}

static void
oplus_chg_track_charging_break_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, charging_break_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->charging_break_trigger);
}

static void
oplus_chg_track_wls_charging_break_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, wls_charging_break_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->wls_charging_break_trigger);
}

static void oplus_chg_track_usbtemp_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, usbtemp_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->usbtemp_load_trigger);
}

static void
oplus_chg_track_vbatt_too_low_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, vbatt_too_low_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->vbatt_too_low_load_trigger);
}

static void
oplus_chg_track_vbatt_diff_over_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip =
		container_of(dwork, struct oplus_chg_track,
			     vbatt_diff_over_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->vbatt_diff_over_load_trigger);
}

static void
oplus_chg_track_uisoc_keep_1_t_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip =
		container_of(dwork, struct oplus_chg_track,
			     uisoc_keep_1_t_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->uisoc_keep_1_t_load_trigger);
}

static void oplus_chg_track_ic_err_msg_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, ic_err_msg_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(chip->ic_err_msg_load_trigger);
}

static void
oplus_chg_track_cal_chg_five_mins_capacity_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, cal_chg_five_mins_capacity_work);
	struct oplus_monitor *monitor = chip->monitor;

	chip->track_status.chg_five_mins_cap =
		monitor->batt_soc - chip->track_status.chg_start_soc;
	chg_info(
		"chg_five_mins_soc:%d, start_chg_soc:%d, chg_five_mins_cap:%d\n",
		monitor->batt_soc, chip->track_status.chg_start_soc,
		chip->track_status.chg_five_mins_cap);
}

static void
oplus_chg_track_cal_chg_ten_mins_capacity_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip = container_of(
		dwork, struct oplus_chg_track, cal_chg_ten_mins_capacity_work);
	struct oplus_monitor *monitor = chip->monitor;

	chip->track_status.chg_ten_mins_cap =
		monitor->batt_soc - chip->track_status.chg_start_soc;
	chg_info("chg_ten_mins_soc:%d, start_chg_soc:%d, chg_ten_mins_cap:%d\n",
		 monitor->batt_soc, chip->track_status.chg_start_soc,
		 chip->track_status.chg_ten_mins_cap);
}

static int oplus_chg_track_speed_ref_init(struct oplus_chg_track *chip)
{
	if (!chip)
		return -1;

	if (chip->track_cfg.wired_fast_chg_scheme >= 0 &&
	    chip->track_cfg.wired_fast_chg_scheme <
		    ARRAY_SIZE(g_wired_speed_ref_standard))
		chip->track_status.wired_speed_ref = g_wired_speed_ref_standard
			[chip->track_cfg.wired_fast_chg_scheme];

	if (chip->track_cfg.wls_fast_chg_scheme >= 0 &&
	    chip->track_cfg.wls_fast_chg_scheme <
		    ARRAY_SIZE(g_wls_fast_speed_ref_standard))
		chip->track_status.wls_airvooc_speed_ref =
			g_wls_fast_speed_ref_standard
				[chip->track_cfg.wls_fast_chg_scheme];

	if (chip->track_cfg.wls_epp_chg_scheme >= 0 &&
	    chip->track_cfg.wls_epp_chg_scheme <
		    ARRAY_SIZE(g_wls_epp_speed_ref_standard))
		chip->track_status.wls_epp_speed_ref =
			g_wls_epp_speed_ref_standard
				[chip->track_cfg.wls_epp_chg_scheme];

	if (chip->track_cfg.wls_bpp_chg_scheme >= 0 &&
	    chip->track_cfg.wls_bpp_chg_scheme <
		    ARRAY_SIZE(g_wls_bpp_speed_ref_standard))
		chip->track_status.wls_bpp_speed_ref =
			g_wls_bpp_speed_ref_standard
				[chip->track_cfg.wls_bpp_chg_scheme];

	return 0;
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

	chip->track_status.curr_soc = -EINVAL;
	chip->track_status.curr_uisoc = -EINVAL;
	chip->track_status.pre_soc = -EINVAL;
	chip->track_status.pre_uisoc = -EINVAL;
	chip->track_status.soc_jumped = false;
	chip->track_status.uisoc_jumped = false;
	chip->track_status.uisoc_to_soc_jumped = false;
	chip->track_status.uisoc_load_jumped = false;
	chip->track_status.debug_soc = OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID;
	chip->track_status.debug_uisoc =
		OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID;
	chip->uisoc_load_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->uisoc_load_trigger.flag_reason =
		TRACK_NOTIFY_FLAG_UI_SOC_LOAD_JUMP;
	chip->soc_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->soc_trigger.flag_reason = TRACK_NOTIFY_FLAG_SOC_JUMP;
	chip->uisoc_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->uisoc_trigger.flag_reason = TRACK_NOTIFY_FLAG_UI_SOC_JUMP;
	chip->uisoc_to_soc_trigger.type_reason = TRACK_NOTIFY_TYPE_SOC_JUMP;
	chip->uisoc_to_soc_trigger.flag_reason =
		TRACK_NOTIFY_FLAG_UI_SOC_TO_SOC_JUMP;
	chip->charger_info_trigger.type_reason =
		TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->charger_info_trigger.flag_reason = TRACK_NOTIFY_FLAG_CHARGER_INFO;
	chip->no_charging_trigger.type_reason = TRACK_NOTIFY_TYPE_NO_CHARGING;
	chip->no_charging_trigger.flag_reason = TRACK_NOTIFY_FLAG_NO_CHARGING;
	chip->slow_charging_trigger.type_reason =
		TRACK_NOTIFY_TYPE_CHARGING_SLOW;
	chip->charging_break_trigger.type_reason =
		TRACK_NOTIFY_TYPE_CHARGING_BREAK;
	chip->wls_charging_break_trigger.type_reason =
		TRACK_NOTIFY_TYPE_CHARGING_BREAK;
	chip->uisoc_keep_1_t_load_trigger.type_reason =
		TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->uisoc_keep_1_t_load_trigger.flag_reason =
		TRACK_NOTIFY_FLAG_UISOC_KEEP_1_T_INFO;
	chip->vbatt_too_low_load_trigger.type_reason =
		TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->vbatt_too_low_load_trigger.flag_reason =
		TRACK_NOTIFY_FLAG_VBATT_TOO_LOW_INFO;
	chip->usbtemp_load_trigger.type_reason =
		TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->usbtemp_load_trigger.flag_reason = TRACK_NOTIFY_FLAG_USBTEMP_INFO;
	chip->vbatt_diff_over_load_trigger.type_reason =
		TRACK_NOTIFY_TYPE_GENERAL_RECORD;
	chip->vbatt_diff_over_load_trigger.flag_reason =
		TRACK_NOTIFY_FLAG_VBATT_DIFF_OVER_INFO;
	chip->ic_err_msg_load_trigger.type_reason =
		TRACK_NOTIFY_TYPE_IC_ERROR;
	chip->ic_err_msg_load_trigger.flag_reason =
		TRACK_NOTIFY_FLAG_DEFAULT;

	memset(&(chip->track_status.power_info), 0,
	       sizeof(chip->track_status.power_info));
	strcpy(chip->track_status.power_info.power_mode, "unknow");
	chip->track_status.chg_no_charging_cnt = 0;
	chip->track_status.chg_total_cnt = 0;
	chip->track_status.chg_max_temp = 0;
	chip->track_status.chg_fast_full_time = 0;
	chip->track_status.chg_normal_full_time = 0;
	chip->track_status.chg_report_full_time = 0;
	chip->track_status.debug_normal_charging_state =
		POWER_SUPPLY_STATUS_CHARGING;
	chip->track_status.debug_fast_prop_status = TRACK_FASTCHG_STATUS_UNKOWN;
	chip->track_status.debug_normal_prop_status =
		POWER_SUPPLY_STATUS_UNKNOWN;
	chip->track_status.debug_no_charging = 0;
	chip->track_status.chg_five_mins_cap = TRACK_PERIOD_CHG_CAP_INIT;
	chip->track_status.chg_ten_mins_cap = TRACK_PERIOD_CHG_CAP_INIT;
	chip->track_status.chg_average_speed =
		TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT;
	chip->track_status.chg_attach_time_ms =
		chip->track_cfg.fast_chg_break_t_thd;
	chip->track_status.chg_detach_time_ms = 0;
	chip->track_status.wls_attach_time_ms =
		chip->track_cfg.wls_chg_break_t_thd;
	chip->track_status.wls_detach_time_ms = 0;
	chip->track_status.soc_sect_status = TRACK_SOC_SECTION_DEFAULT;
	chip->track_status.chg_speed_is_slow = false;
	chip->track_status.tbatt_warm_once = false;
	chip->track_status.tbatt_cold_once = false;
	chip->track_status.cool_down_effect_cnt = 0;
	chip->track_status.wls_need_upload = false;
	chip->track_status.wired_need_upload = false;
	chip->track_status.once_mmi_chg = false;
	chip->track_status.once_chg_cycle_status = CHG_CYCLE_VOTER__NONE;

	memset(&(chip->track_status.fastchg_break_info), 0,
	       sizeof(chip->track_status.fastchg_break_info));
	memset(&(chip->track_status.wired_break_crux_info), 0,
	       sizeof(chip->track_status.wired_break_crux_info));
	memset(&(chip->track_status.wls_break_crux_info), 0,
	       sizeof(chip->track_status.wls_break_crux_info));

	memset(&(chip->track_status.batt_full_reason), 0,
	       sizeof(chip->track_status.batt_full_reason));
	oplus_chg_track_clear_cool_down_stats_time(&(chip->track_status));

	memset(&(chip->voocphy_name), 0, sizeof(chip->voocphy_name));
	oplus_chg_track_set_voocphy_name(chip);
	oplus_chg_track_speed_ref_init(chip);

	INIT_DELAYED_WORK(&chip->uisoc_load_trigger_work,
			  oplus_chg_track_uisoc_load_trigger_work);
	INIT_DELAYED_WORK(&chip->soc_trigger_work,
			  oplus_chg_track_soc_trigger_work);
	INIT_DELAYED_WORK(&chip->uisoc_trigger_work,
			  oplus_chg_track_uisoc_trigger_work);
	INIT_DELAYED_WORK(&chip->uisoc_to_soc_trigger_work,
			  oplus_chg_track_uisoc_to_soc_trigger_work);
	INIT_DELAYED_WORK(&chip->charger_info_trigger_work,
			  oplus_chg_track_charger_info_trigger_work);
	INIT_DELAYED_WORK(&chip->cal_chg_five_mins_capacity_work,
			  oplus_chg_track_cal_chg_five_mins_capacity_work);
	INIT_DELAYED_WORK(&chip->cal_chg_ten_mins_capacity_work,
			  oplus_chg_track_cal_chg_ten_mins_capacity_work);
	INIT_DELAYED_WORK(&chip->no_charging_trigger_work,
			  oplus_chg_track_no_charging_trigger_work);
	INIT_DELAYED_WORK(&chip->slow_charging_trigger_work,
			  oplus_chg_track_slow_charging_trigger_work);
	INIT_DELAYED_WORK(&chip->charging_break_trigger_work,
			  oplus_chg_track_charging_break_trigger_work);
	INIT_DELAYED_WORK(&chip->wls_charging_break_trigger_work,
			  oplus_chg_track_wls_charging_break_trigger_work);
	INIT_DELAYED_WORK(&chip->usbtemp_load_trigger_work,
			  oplus_chg_track_usbtemp_load_trigger_work);
	INIT_DELAYED_WORK(&chip->vbatt_too_low_load_trigger_work,
			  oplus_chg_track_vbatt_too_low_load_trigger_work);
	INIT_DELAYED_WORK(&chip->vbatt_diff_over_load_trigger_work,
			  oplus_chg_track_vbatt_diff_over_load_trigger_work);
	INIT_DELAYED_WORK(&chip->uisoc_keep_1_t_load_trigger_work,
			  oplus_chg_track_uisoc_keep_1_t_load_trigger_work);
	INIT_DELAYED_WORK(&chip->ic_err_msg_trigger_work,
			  oplus_chg_track_ic_err_msg_trigger_work);
	return ret;
}

static bool
oplus_chg_track_trigger_data_is_valid(oplus_chg_track_trigger *pdata)
{
	int i;
	int len;
	bool ret = false;
	int type_reason = pdata->type_reason;
	int flag_reason = pdata->flag_reason;

	len = strlen(pdata->crux_info);
	if (!len) {
		chg_err("crux_info lens is invaild\n");
		return ret;
	}

	switch (type_reason) {
	case TRACK_NOTIFY_TYPE_SOC_JUMP:
		for (i = TRACK_NOTIFY_FLAG_UI_SOC_LOAD_JUMP;
		     i <= TRACK_NOTIFY_FLAG_UI_SOC_TO_SOC_JUMP; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_GENERAL_RECORD:
		for (i = TRACK_NOTIFY_FLAG_CHARGER_INFO;
		     i <= TRACK_NOTIFY_FLAG_WLS_TRX_INFO; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_NO_CHARGING:
		for (i = TRACK_NOTIFY_FLAG_NO_CHARGING;
		     i <= TRACK_NOTIFY_FLAG_NO_CHARGING; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_CHARGING_SLOW:
		for (i = TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM;
		     i <= TRACK_NOTIFY_FLAG_CHG_SLOW_OTHER; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_CHARGING_BREAK:
		for (i = TRACK_NOTIFY_FLAG_FAST_CHARGING_BREAK;
		     i <= TRACK_NOTIFY_FLAG_WLS_CHARGING_BREAK; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL:
		for (i = TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
		     i <= TRACK_NOTIFY_FLAG_MAX_CNT; i++) {
			if (flag_reason == i) {
				ret = true;
				break;
			}
		}
		break;
	case TRACK_NOTIFY_TYPE_IC_ERROR:
		for (i = TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
		     i < TRACK_NOTIFY_FLAG_MAX_CNT; i++) {
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
		chg_err("type_reason or flag_reason is invaild\n");
	return ret;
}

static int oplus_chg_track_upload_trigger_data(oplus_chg_track_trigger data)
{
	int rc;
	struct oplus_chg_track *chip = g_track_chip;

	if (!g_track_chip)
		return TRACK_CMD_ERROR_CHIP_NULL;

	if (!oplus_chg_track_trigger_data_is_valid(&data))
		return TRACK_CMD_ERROR_DATA_INVALID;

	mutex_lock(&chip->trigger_ack_lock);
	mutex_lock(&chip->trigger_data_lock);
	memset(&chip->trigger_data, 0, sizeof(oplus_chg_track_trigger));
	chip->trigger_data.type_reason = data.type_reason;
	chip->trigger_data.flag_reason = data.flag_reason;
	strncpy(chip->trigger_data.crux_info, data.crux_info,
		OPLUS_CHG_TRACK_CURX_INFO_LEN - 1);
	chg_info("type_reason:%d, flag_reason:%d, crux_info[%s]\n",
		 chip->trigger_data.type_reason, chip->trigger_data.flag_reason,
		 chip->trigger_data.crux_info);
	chip->trigger_data_ok = true;
	mutex_unlock(&chip->trigger_data_lock);
	reinit_completion(&chip->trigger_ack);
	wake_up(&chip->upload_wq);

	rc = wait_for_completion_timeout(
		&chip->trigger_ack,
		msecs_to_jiffies(OPLUS_CHG_TRACK_WAIT_TIME_MS));
	if (!rc) {
		if (delayed_work_pending(&chip->upload_info_dwork))
			cancel_delayed_work_sync(&chip->upload_info_dwork);
		chg_err("Error, timed out upload trigger data\n");
		mutex_unlock(&chip->trigger_ack_lock);
		return TRACK_CMD_ERROR_TIME_OUT;
	}
	rc = 0;
	chg_info("success\n");
	mutex_unlock(&chip->trigger_ack_lock);

	return rc;
}

static int oplus_chg_track_thread(void *data)
{
	int rc = 0;
	struct oplus_chg_track *chip = (struct oplus_chg_track *)data;

	while (!kthread_should_stop()) {
		mutex_lock(&chip->upload_lock);
		rc = wait_event_interruptible(chip->upload_wq,
					      chip->trigger_data_ok);
		mutex_unlock(&chip->upload_lock);
		if (rc)
			return rc;
		if (!chip->trigger_data_ok)
			chg_err("oplus chg false wakeup, rc=%d\n", rc);
		mutex_lock(&chip->trigger_data_lock);
		chip->trigger_data_ok = false;
		chip->dwork_retry_cnt = OPLUS_CHG_TRACK_DWORK_RETRY_CNT;
		queue_delayed_work(chip->trigger_upload_wq,
				   &chip->upload_info_dwork, 0);
		mutex_unlock(&chip->trigger_data_lock);
	}

	return rc;
}

static int oplus_chg_track_thread_init(struct oplus_chg_track *track_dev)
{
	int rc = 0;
	struct oplus_chg_track *chip = track_dev;

	chip->track_upload_kthread = kthread_run(oplus_chg_track_thread, chip,
						 "track_upload_kthread");
	if (IS_ERR(chip->track_upload_kthread)) {
		chg_err("failed to create oplus_chg_track_thread\n");
		return -1;
	}

	return rc;
}

static int oplus_chg_track_get_current_time_s(struct rtc_time *tm)
{
	struct timespec ts;

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, tm);
	tm->tm_year = tm->tm_year + 1900;
	tm->tm_mon = tm->tm_mon + 1;
	return ts.tv_sec;
}

static int oplus_chg_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	chg_debug("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

/*
* track sub version
* 3: default version for chg track
* 3.1: compatible with historical keywords
*/
#define TRACK_VERSION	"3.1"

static void oplus_chg_track_upload_info_dwork(struct work_struct *work)
{
	int ret = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_track *chip =
		container_of(dwork, struct oplus_chg_track, upload_info_dwork);

	if (!chip)
		return;

	mutex_lock(&chip->dcs_info_lock);
	mutex_unlock(&chip->dcs_info_lock);
	if (!ret)
		complete(&chip->trigger_ack);
	else if (chip->dwork_retry_cnt > 0)
		queue_delayed_work(
			chip->trigger_upload_wq, &chip->upload_info_dwork,
			msecs_to_jiffies(OPLUS_CHG_UPDATE_INFO_DELAY_MS));

	chg_info("retry_cnt: %d, ret = %d\n", chip->dwork_retry_cnt, ret);
	chip->dwork_retry_cnt--;
}

int oplus_chg_track_handle_wired_type_info(
	struct oplus_monitor *monitor, int type)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;

	if (!monitor)
		return -EINVAL;
	track_chip = monitor->track;
	if (!track_chip)
		return -EINVAL;
	track_status = &track_chip->track_status;

	if (track_status->power_info.wired_info.adapter_id) {
		chg_info("has know type and not handle\n");
		return 0;
	}

	track_status->power_info.power_type = TRACK_CHG_TYPE_WIRE;
	memset(track_status->power_info.power_mode, 0,
	       sizeof(track_status->power_info.power_mode));
	strcpy(track_status->power_info.power_mode, "wired");

	if (type == TRACK_CHG_GET_THTS_TIME_TYPE) {
		track_status->pre_wired_type = monitor->wired_charge_type;
		oplus_chg_track_get_wired_type_info(monitor->wired_charge_type,
						    track_status);
	} else {
		oplus_chg_track_get_wired_type_info(
			track_status->pre_wired_type, track_status);
	}

	if (type == TRACK_CHG_GET_THTS_TIME_TYPE) {
		track_status->fast_chg_type =
			sid_to_adapter_id(monitor->vooc_sid);
		oplus_chg_track_get_vooc_type_info(track_status->fast_chg_type,
						   track_status);
	} else {
		oplus_chg_track_get_vooc_type_info(
			track_status->pre_fastchg_type, track_status);
	}

	chg_info("power_mode:%s, type:%s, adapter_id:0x%0x, power:%d\n",
		track_status->power_info.power_mode,
		track_status->power_info.wired_info.adapter_type,
		track_status->power_info.wired_info.adapter_id,
		track_status->power_info.wired_info.power);

	return 0;
}

static int oplus_chg_track_handle_wls_type_info(
	struct oplus_chg_track_status *track_status)
{
	/* TODO */
	return 0;
}

static int oplus_chg_track_handle_batt_full_reason(
	struct oplus_monitor *monitor,
	struct oplus_chg_track_status *track_status)
{
	int notify_flag = NOTIFY_BAT_FULL;

	if (monitor == NULL || track_status == NULL)
		return -1;

	if (monitor->notify_flag == NOTIFY_BAT_FULL ||
	    monitor->notify_flag == NOTIFY_BAT_FULL_PRE_LOW_TEMP ||
	    monitor->notify_flag == NOTIFY_BAT_FULL_PRE_HIGH_TEMP ||
	    monitor->notify_flag == NOTIFY_BAT_NOT_CONNECT ||
	    monitor->notify_flag == NOTIFY_BAT_FULL_THIRD_BATTERY)
		notify_flag = monitor->notify_flag;

	if (track_status->debug_chg_notify_flag)
		notify_flag = track_status->debug_chg_notify_flag;
	if (!strlen(track_status->batt_full_reason))
		oplus_chg_track_get_batt_full_reason_info(notify_flag,
							  track_status);
	chg_info("track_notify_flag:%d, chager_notify_flag:%d, full_reason[%s]\n",
		notify_flag, monitor->notify_flag,
		track_status->batt_full_reason);

	return 0;
}

static int
oplus_chg_track_check_chg_abnormal(struct oplus_monitor *monitor,
				   struct oplus_chg_track_status *track_status)
{
	int notify_code;

	if (monitor == NULL || track_status == NULL)
		return -1;

	if (track_status->debug_chg_notify_code)
		notify_code = track_status->debug_chg_notify_code;
	else
		notify_code = monitor->notify_code;

	if (notify_code & (1 << NOTIFY_CHGING_OVERTIME))
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_CHGING_OVERTIME, track_status);

	if (notify_code & (1 << NOTIFY_CHARGER_OVER_VOL)) {
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_CHARGER_OVER_VOL, track_status);
	} else if (notify_code & (1 << NOTIFY_CHARGER_LOW_VOL)) {
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_CHARGER_LOW_VOL, track_status);
	}

	if (notify_code & (1 << NOTIFY_BAT_OVER_TEMP)) {
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_BAT_OVER_TEMP, track_status);
	} else if (notify_code & (1 << NOTIFY_BAT_LOW_TEMP)) {
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_BAT_LOW_TEMP, track_status);
	}

	if (notify_code & (1 << NOTIFY_BAT_OVER_VOL)) {
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_BAT_OVER_VOL, track_status);
	}

	if (notify_code & (1 << NOTIFY_BAT_NOT_CONNECT)) {
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_BAT_NOT_CONNECT, track_status);
	} else if (notify_code & (1 << NOTIFY_BAT_FULL_THIRD_BATTERY)) {
		oplus_chg_track_get_chg_abnormal_reason_info(
			NOTIFY_BAT_FULL_THIRD_BATTERY, track_status);
	}

	chg_info(
		"track_notify_code:0x%x, chager_notify_code:0x%x, abnormal_reason[%s]\n",
		notify_code, monitor->notify_code,
		track_status->chg_abnormal_reason);

	return 0;
}

static int
oplus_chg_track_cal_chg_common_mesg(struct oplus_monitor *monitor,
				    struct oplus_chg_track_status *track_status)
{
	bool mmi_chg = false;
	if (monitor == NULL || track_status == NULL)
		return -1;

	if (monitor->shell_temp > track_status->chg_max_temp)
		track_status->chg_max_temp = monitor->shell_temp;

	if (monitor->batt_temp > track_status->batt_max_temp)
		track_status->batt_max_temp = monitor->batt_temp;

	if (monitor->ibat_ma < track_status->batt_max_curr)
		track_status->batt_max_curr = monitor->ibat_ma;

	if (monitor->vbat_mv > track_status->batt_max_vol)
		track_status->batt_max_vol = monitor->vbat_mv;

	/* TODO: wireless support */

	mmi_chg = oplus_chg_track_get_mmi_chg();
	if (!track_status->once_mmi_chg && !mmi_chg)
		track_status->once_mmi_chg = true;

	if (!track_status->once_chg_cycle_status && monitor->chg_cycle_status)
		track_status->once_chg_cycle_status = monitor->chg_cycle_status;

	chg_info("chg_max_temp:%d, batt_max_temp:%d, batt_max_curr:%d, "
		"batt_max_vol:%d, once_mmi_chg:%d, once_chg_cycle_status:%d\n",
		track_status->chg_max_temp, track_status->batt_max_temp,
		track_status->batt_max_curr, track_status->batt_max_vol,
		track_status->once_mmi_chg, track_status->once_chg_cycle_status);

	return 0;
}

static int
oplus_chg_track_cal_cool_down_stats(struct oplus_monitor *monitor,
				    struct oplus_chg_track_status *track_status)
{
	int cool_down_max;
	int curr_time;
	bool chg_start;

	if (monitor == NULL || track_status == NULL)
		return -1;

	cool_down_max = ARRAY_SIZE(cool_down_stats_table) - 1;
	if (monitor->cool_down > cool_down_max || monitor->cool_down < 0) {
		chg_err("cool_down is invalid\n");
		return -1;
	}

	if (monitor->batt_status == POWER_SUPPLY_STATUS_CHARGING &&
	    monitor->cool_down)
		track_status->cool_down_effect_cnt++;

	curr_time = oplus_chg_track_get_local_time_s();
	chg_start =
		(track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING &&
		 monitor->batt_status == POWER_SUPPLY_STATUS_CHARGING);
	if (chg_start ||
	    (track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING)) {
		track_status->cool_down_status = monitor->cool_down;
		track_status->cool_down_status_change_t = curr_time;
		return 0;
	}

	if (track_status->cool_down_status != monitor->cool_down) {
		cool_down_stats_table[monitor->cool_down].time +=
			curr_time - track_status->cool_down_status_change_t;
	} else {
		cool_down_stats_table[track_status->cool_down_status].time +=
			curr_time - track_status->cool_down_status_change_t;
	}
	track_status->cool_down_status = monitor->cool_down;
	track_status->cool_down_status_change_t = curr_time;

	chg_info("cool_down_status:%d, cool_down_tatus_change_t:%d\n",
		 track_status->cool_down_status,
		 track_status->cool_down_status_change_t);

	return 0;
}

static int
oplus_chg_track_cal_rechg_counts(struct oplus_monitor *monitor,
				 struct oplus_chg_track_status *track_status)

{
	if (monitor == NULL || track_status == NULL)
		return -1;

	if (!track_status->in_rechging && monitor->rechging)
		track_status->rechg_counts++;

	track_status->in_rechging = monitor->rechging;
	return 0;
}

static int oplus_chg_track_cal_no_charging_stats(
	struct oplus_monitor *monitor,
	struct oplus_chg_track_status *track_status)
{
	if (monitor == NULL || track_status == NULL)
		return -1;

	if (monitor->batt_status == POWER_SUPPLY_STATUS_CHARGING) {
		track_status->chg_total_cnt++;
		if (monitor->ibat_ma > 0)
			track_status->chg_no_charging_cnt++;
	}

	return 0;
}

static int oplus_chg_track_cal_ledon_ledoff_average_speed(
	struct oplus_chg_track_status *track_status)
{
	if (track_status->led_onoff_power_cal)
		return 0;

	if (!track_status->ledon_ave_speed) {
		if (!track_status->ledon_time)
			track_status->ledon_ave_speed = 0;
		else
			track_status->ledon_ave_speed =
				TRACK_TIME_1MIN_THD * track_status->ledon_rm /
				track_status->ledon_time;
	}

	if (!track_status->ledoff_ave_speed) {
		if (!track_status->ledoff_time)
			track_status->ledoff_ave_speed = 0;
		else
			track_status->ledoff_ave_speed =
				TRACK_TIME_1MIN_THD * track_status->ledoff_rm /
				track_status->ledoff_time;
	}

	track_status->led_onoff_power_cal = true;
	chg_info("ledon_ave_speed:%d, ledoff_ave_speed:%d\n",
		 track_status->ledon_ave_speed, track_status->ledoff_ave_speed);
	return 0;
}

static int
oplus_chg_track_cal_led_on_stats(struct oplus_monitor *monitor,
				 struct oplus_chg_track_status *track_status)
{
	int curr_time;
	bool chg_start;
	bool chg_end;
	bool need_update = false;
	bool charger_exist;

	if (monitor == NULL || track_status == NULL)
		return -1;

	charger_exist = monitor->wired_online || monitor->wls_online;

	curr_time = oplus_chg_track_get_local_time_s();
	chg_start =
		(track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING &&
		 monitor->batt_status == POWER_SUPPLY_STATUS_CHARGING);
	chg_end = (track_status->prop_status == POWER_SUPPLY_STATUS_CHARGING &&
		   monitor->batt_status != POWER_SUPPLY_STATUS_CHARGING);
	if (chg_start ||
	    (track_status->prop_status != POWER_SUPPLY_STATUS_CHARGING)) {
		track_status->led_on = monitor->led_on;
		track_status->led_change_t = curr_time;
		track_status->led_change_rm = monitor->batt_rm;
		return 0;
	}

	if (chg_end || track_status->chg_fast_full_time ||
	    monitor->batt_soc >= TRACK_LED_MONITOR_SOC_POINT)
		need_update = true;

	if (track_status->led_on && (!monitor->led_on || need_update)) {
		if (curr_time - track_status->led_change_t >
		    TRACK_CYCLE_RECORDIING_TIME_2MIN) {
			track_status->ledon_rm +=
				monitor->batt_rm - track_status->led_change_rm;
			track_status->ledon_time +=
				curr_time - track_status->led_change_t;
		}
		track_status->continue_ledon_time +=
			curr_time - track_status->led_change_t;
		track_status->led_change_t = curr_time;
		track_status->led_change_rm = monitor->batt_rm;
	} else if (!track_status->led_on && (monitor->led_on || need_update)) {
		if (curr_time - track_status->led_change_t >
		    TRACK_CYCLE_RECORDIING_TIME_2MIN) {
			track_status->ledoff_rm +=
				monitor->batt_rm - track_status->led_change_rm;
			track_status->ledoff_time +=
				curr_time - track_status->led_change_t;
		}
		track_status->continue_ledoff_time +=
			curr_time - track_status->led_change_t;
		track_status->led_change_t = curr_time;
		track_status->led_change_rm = monitor->batt_rm;
	}
	track_status->led_on = monitor->led_on;
	chg_info("continue_ledoff_t:%d, continue_ledon_t:%d\n",
		 track_status->continue_ledoff_time,
		 track_status->continue_ledon_time);

	if (!charger_exist || track_status->chg_report_full_time ||
	    track_status->chg_fast_full_time ||
	    monitor->batt_soc >= TRACK_LED_MONITOR_SOC_POINT)
		oplus_chg_track_cal_ledon_ledoff_average_speed(track_status);

	chg_info("ch_t:%d, ch_rm:%d, ledoff_rm:%d, ledoff_t:%d, ledon_rm:%d, ledon_t:%d\n",
		track_status->led_change_t, track_status->led_change_rm,
		track_status->ledoff_rm, track_status->ledoff_time,
		track_status->ledon_rm, track_status->ledon_time);

	return 0;
}

static bool
oplus_chg_track_is_no_charging(struct oplus_chg_track_status *track_status)
{
	bool ret = false;
	char wired_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
	char wls_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];

	if (track_status == NULL)
		return ret;

	if (!track_status->chg_total_cnt)
		return ret;

	if ((track_status->chg_no_charging_cnt * 100) /
		    track_status->chg_total_cnt >
	    TRACK_NO_CHRGING_TIME_PCT)
		ret = true;

	memcpy(wired_adapter_type,
	       track_status->power_info.wired_info.adapter_type,
	       OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	memcpy(wls_adapter_type, track_status->power_info.wls_info.adapter_type,
	       OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	chg_info("wired_adapter_type:%s, wls_adapter_type:%s\n",
		 wired_adapter_type, wls_adapter_type);
	if (!strcmp(wired_adapter_type, "unknow") ||
	    !strcmp(wired_adapter_type, "sdp") ||
	    !strcmp(wls_adapter_type, "unknow"))
		ret = false;

	if (track_status->debug_no_charging)
		ret = true;

	chg_info("chg_no_charging_cnt:%d, chg_total_cnt:%d, debug_no_charging:%d, ret:%d",
		track_status->chg_no_charging_cnt, track_status->chg_total_cnt,
		track_status->debug_no_charging, ret);

	return ret;
}

static void oplus_chg_track_record_break_charging_info(
	struct oplus_chg_track *track_chip, struct oplus_monitor *monitor,
	struct oplus_chg_track_power power_info, const char *sub_crux_info)
{
	int index = 0;
	struct oplus_chg_track_status *track_status;

	if (track_chip == NULL)
		return;

	chg_info("start, type=%d\n", power_info.power_type);
	track_status = &(track_chip->track_status);

	if (power_info.power_type == TRACK_CHG_TYPE_WIRE) {
		memset(track_chip->charging_break_trigger.crux_info, 0,
		       sizeof(track_chip->charging_break_trigger.crux_info));
		index += snprintf(
			&(track_chip->charging_break_trigger.crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$power_mode@@%s", power_info.power_mode);
		index += snprintf(
			&(track_chip->charging_break_trigger.crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$adapter_t@@%s", power_info.wired_info.adapter_type);
		if (power_info.wired_info.adapter_id)
			index += snprintf(&(track_chip->charging_break_trigger
						    .crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "$$adapter_id@@0x%x",
					  power_info.wired_info.adapter_id);
		index += snprintf(
			&(track_chip->charging_break_trigger.crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$power@@%d",
			power_info.wired_info.power);

		if (strlen(track_status->fastchg_break_info.name)) {
			index += snprintf(&(track_chip->charging_break_trigger
						    .crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "$$voocphy_name@@%s",
					  track_chip->voocphy_name);
			index +=
				snprintf(&(track_chip->charging_break_trigger
						   .crux_info[index]),
					 OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					 "$$reason@@%s",
					 track_status->fastchg_break_info.name);
		}
		if (strlen(sub_crux_info)) {
			index += snprintf(&(track_chip->charging_break_trigger
						    .crux_info[index]),
					  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
					  "$$crux_info@@%s", sub_crux_info);
		}
		oplus_chg_track_record_general_info(
			monitor, track_status,
			&(track_chip->charging_break_trigger), index);
		chg_info("wired[%s]\n",
			 track_chip->charging_break_trigger.crux_info);
	} else if (power_info.power_type == TRACK_CHG_TYPE_WIRELESS) {
		memset(track_chip->wls_charging_break_trigger.crux_info, 0,
		       sizeof(track_chip->wls_charging_break_trigger.crux_info));
		index += snprintf(&(track_chip->wls_charging_break_trigger
					    .crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$power_mode@@%s", power_info.power_mode);
		index += snprintf(&(track_chip->wls_charging_break_trigger
					    .crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$adapter_t@@%s",
				  power_info.wls_info.adapter_type);
		if (strlen(power_info.wls_info.dock_type))
			index += snprintf(
				&(track_chip->wls_charging_break_trigger
					  .crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$dock_type@@%s",
				power_info.wls_info.dock_type);
		index += snprintf(&(track_chip->wls_charging_break_trigger
					    .crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "$$power@@%d", power_info.wls_info.power);
		if (strlen(sub_crux_info)) {
			index += snprintf(
				&(track_chip->wls_charging_break_trigger
					  .crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$crux_info@@%s", sub_crux_info);
		}
		oplus_chg_track_record_general_info(
			monitor, track_status,
			&(track_chip->wls_charging_break_trigger), index);
		chg_info("wls[%s]\n",
			 track_chip->wls_charging_break_trigger.crux_info);
	}
}

static int oplus_chg_track_get_fv_when_vooc(struct oplus_monitor *monitor)
{
	int flv;

	if (monitor->chg_ctrl_by_vooc && (monitor->ffc_status == FFC_DEFAULT))
		flv = monitor->fv_mv;
	else
		flv = 0;
	chg_info("flv=%d\n", flv);

	return flv;
}

int oplus_chg_track_set_fastchg_break_code(int fastchg_break_code)
{
	int flv;
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	struct oplus_monitor *monitor;
	unsigned vooc_sid;

	if (!g_track_chip)
		return -1;

	track_chip = g_track_chip;
	monitor = track_chip->monitor;
	track_status = &track_chip->track_status;
	track_status->fastchg_break_info.code = fastchg_break_code;
	if (track_chip->monitor->vooc_sid != 0)
		vooc_sid = track_chip->monitor->vooc_sid;
	else
		vooc_sid = track_chip->monitor->pre_vooc_sid;
	track_status->pre_fastchg_type = sid_to_adapter_id(vooc_sid);
	flv = oplus_chg_track_get_fv_when_vooc(monitor);
	if ((track_status->pre_fastchg_type == CHARGER_SUBTYPE_FASTCHG_VOOC) &&
	    flv && (flv - TRACK_CHG_VOOC_BATT_VOL_DIFF_MV < monitor->vbat_mv)) {
		if (g_track_chip->track_cfg.voocphy_type ==
		    TRACK_ADSP_VOOCPHY) {
			if (fastchg_break_code ==
			    TRACK_ADSP_VOOCPHY_COMMU_TIME_OUT)
				track_status->fastchg_break_info.code =
					TRACK_ADSP_VOOCPHY_BREAK_DEFAULT;
		} else if (g_track_chip->track_cfg.voocphy_type ==
				   TRACK_AP_SINGLE_CP_VOOCPHY ||
			   g_track_chip->track_cfg.voocphy_type ==
				   TRACK_AP_DUAL_CP_VOOCPHY) {
			if (fastchg_break_code ==
			    TRACK_CP_VOOCPHY_COMMU_TIME_OUT)
				track_status->fastchg_break_info.code =
					TRACK_CP_VOOCPHY_BREAK_DEFAULT;
		}
	}

	return 0;
}

static int oplus_chg_track_match_mcu_voocphy_break_reason(
	struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mcu_voocphy_break_table); i++) {
		if (mcu_voocphy_break_table[i].code ==
		    track_status->fastchg_break_info.code) {
			strncpy(track_status->fastchg_break_info.name,
				mcu_voocphy_break_table[i].name,
				OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN - 1);
			break;
		}
	}

	return 0;
}

static int oplus_chg_track_match_adsp_voocphy_break_reason(
	struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adsp_voocphy_break_table); i++) {
		if (adsp_voocphy_break_table[i].code ==
		    track_status->fastchg_break_info.code) {
			strncpy(track_status->fastchg_break_info.name,
				adsp_voocphy_break_table[i].name,
				OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN - 1);
			break;
		}
	}

	return 0;
}

static int oplus_chg_track_match_ap_voocphy_break_reason(
	struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ap_voocphy_break_table); i++) {
		if (ap_voocphy_break_table[i].code ==
		    track_status->fastchg_break_info.code) {
			strncpy(track_status->fastchg_break_info.name,
				ap_voocphy_break_table[i].name,
				OPLUS_CHG_TRACK_FASTCHG_BREAK_REASON_LEN - 1);
			break;
		}
	}

	return 0;
}

static int
oplus_chg_track_match_fastchg_break_reason(struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_status *track_status;

	if (!track_chip)
		return -1;
	track_status = &track_chip->track_status;

	chg_info("voocphy_type:%d, code:0x%x\n",
		 track_chip->track_cfg.voocphy_type,
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
		chg_info("!!!voocphy type is error, should not go here\n");
		break;
	}

	return 0;
}

static int oplus_chg_track_obtain_wls_break_sub_crux_info(
	struct oplus_chg_track *track_chip, char *crux_info)
{
	return 0;
}

static bool
oplus_chg_track_wls_is_online_keep(struct oplus_chg_track *track_chip)
{
	/* TODO: wireless */
	return false;
}

int oplus_chg_track_check_wls_charging_break(int wls_connect)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	struct oplus_monitor *monitor;
	static struct oplus_chg_track_power power_info;
	static bool break_recording = 0;
	static bool pre_wls_connect = false;

	if (!g_track_chip)
		return -1;

	track_chip = g_track_chip;
	track_status = &track_chip->track_status;
	monitor = track_chip->monitor;

	chg_info("pre_wls_connect[%d], wls_connect[%d], break_recording[%d], online_keep[%d]\n",
		pre_wls_connect, wls_connect, break_recording,
		track_status->wls_online_keep);

	if (wls_connect) {
		if (pre_wls_connect != wls_connect) {
			track_status->wls_attach_time_ms =
				local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
			if ((track_status->wls_attach_time_ms -
				     track_status->wls_detach_time_ms <
			     track_chip->track_cfg.wls_chg_break_t_thd) &&
			    !track_status->wls_online_keep) {
				if (!break_recording) {
					break_recording = true;
					track_chip->wls_charging_break_trigger
						.flag_reason =
						TRACK_NOTIFY_FLAG_WLS_CHARGING_BREAK;
					oplus_chg_track_record_break_charging_info(
						track_chip, monitor, power_info,
						track_status
							->wls_break_crux_info);
					schedule_delayed_work(
						&track_chip
							 ->wls_charging_break_trigger_work,
						0);
				}
				if (!track_status->wired_need_upload) {
					cancel_delayed_work_sync(
						&track_chip
							 ->charger_info_trigger_work);
					cancel_delayed_work_sync(
						&track_chip
							 ->no_charging_trigger_work);
					cancel_delayed_work_sync(
						&track_chip
							 ->slow_charging_trigger_work);
				}
			} else {
				break_recording = 0;
			}
			chg_info("detal_t:%llu, wls_attach_time = %llu\n",
				 track_status->wls_attach_time_ms -
					 track_status->wls_detach_time_ms,
				 track_status->wls_attach_time_ms);
		}
		track_status->wls_online_keep = false;
		pre_wls_connect = wls_connect;
	} else {
		if (pre_wls_connect != wls_connect) {
			track_status->wls_detach_time_ms =
				local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
			track_status->wls_online_keep =
				oplus_chg_track_wls_is_online_keep(track_chip);
			oplus_chg_track_handle_wls_type_info(track_status);
			oplus_chg_track_obtain_wls_break_sub_crux_info(
				track_chip, track_status->wls_break_crux_info);
			power_info = track_status->power_info;
			/* oplus_chg_wake_update_work(); TODO:need check */
			chg_info("wls_detach_time = %llu\n",
				 track_status->wls_detach_time_ms);
		}
		pre_wls_connect = wls_connect;
	}

	return 0;
}

static bool
oplus_chg_track_wired_fastchg_exit_code(struct oplus_chg_track *track_chip)
{
	bool ret = true;
	int code;
	struct oplus_chg_track_status *track_status;

	if (!track_chip)
		return -1;
	track_status = &track_chip->track_status;
	code = track_status->fastchg_break_info.code;

	chg_info("voocphy_type:%d, code:0x%x\n",
		 track_chip->track_cfg.voocphy_type,
		 track_status->fastchg_break_info.code);
	switch (track_chip->track_cfg.voocphy_type) {
	case TRACK_ADSP_VOOCPHY:
		if (!code || code == TRACK_ADSP_VOOCPHY_FULL ||
		    code == TRACK_ADSP_VOOCPHY_BATT_TEMP_OVER ||
		    code == TRACK_ADSP_VOOCPHY_SWITCH_TEMP_RANGE)
			ret = true;
		else
			ret = false;
		break;
	case TRACK_AP_SINGLE_CP_VOOCPHY:
	case TRACK_AP_DUAL_CP_VOOCPHY:
		if (!code || code == TRACK_CP_VOOCPHY_FULL ||
		    code == TRACK_CP_VOOCPHY_BATT_TEMP_OVER ||
		    code == TRACK_CP_VOOCPHY_USER_EXIT_FASTCHG)
			ret = true;
		else
			ret = false;
		break;
	case TRACK_MCU_VOOCPHY:
		if (!code || code == TRACK_MCU_VOOCPHY_TEMP_OVER ||
		    code == TRACK_MCU_VOOCPHY_NORMAL_TEMP_FULL ||
		    code == TRACK_MCU_VOOCPHY_LOW_TEMP_FULL ||
		    code == TRACK_MCU_VOOCPHY_BAT_TEMP_EXIT)
			ret = true;
		else
			ret = false;
		break;
	default:
		chg_info("!!!voocphy type is error, should not go here\n");
		break;
	}

	return ret;
}

static bool oplus_chg_track_get_mmi_chg(void)
{
	struct votable *common_chg_suspend_votable;

	common_chg_suspend_votable = find_votable("CHG_DISABLE");
	if (!common_chg_suspend_votable)
		return true;

	return !get_client_vote(common_chg_suspend_votable, MMI_CHG_VOTER);
}

int oplus_chg_track_check_wired_charging_break(int vbus_rising)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	static struct oplus_chg_track_power power_info;
	struct oplus_monitor *monitor;
	static bool break_recording = 0;
	static bool pre_vbus_rising = false;
	bool fastchg_code_ok;

	if (!g_track_chip)
		return -1;

	track_chip = g_track_chip;
	monitor = track_chip->monitor;
	track_status = &track_chip->track_status;

	chg_info("pre_vbus_rising[%d], vbus_rising[%d], break_recording[%d]\n",
		 pre_vbus_rising, vbus_rising, break_recording);

	if (vbus_rising && (pre_vbus_rising != vbus_rising)) {
		track_status->chg_attach_time_ms =
			local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
		fastchg_code_ok =
			oplus_chg_track_wired_fastchg_exit_code(track_chip);
		chg_info(
			"detal_t:%llu, chg_attach_time = %llu, fastchg_break_code=0x%x\n",
			track_status->chg_attach_time_ms -
				track_status->chg_detach_time_ms,
			track_status->chg_attach_time_ms,
			track_status->fastchg_break_info.code);
		if ((track_status->chg_attach_time_ms -
			     track_status->chg_detach_time_ms <
		     track_chip->track_cfg.fast_chg_break_t_thd) &&
		    !fastchg_code_ok && track_status->mmi_chg) {
			if (!break_recording) {
				break_recording = true;
				track_chip->charging_break_trigger.flag_reason =
					TRACK_NOTIFY_FLAG_FAST_CHARGING_BREAK;
				oplus_chg_track_match_fastchg_break_reason(
					track_chip);
				oplus_chg_track_record_break_charging_info(
					track_chip, monitor, power_info,
					track_status->wired_break_crux_info);
				memset(&(track_status->fastchg_break_info), 0,
				       sizeof(track_status->fastchg_break_info));
				schedule_delayed_work(
					&track_chip->charging_break_trigger_work,
					0);
			}
			if (!track_status->wls_need_upload) {
				cancel_delayed_work_sync(
					&track_chip->charger_info_trigger_work);
				cancel_delayed_work_sync(
					&track_chip->no_charging_trigger_work);
				cancel_delayed_work_sync(
					&track_chip->slow_charging_trigger_work);
			}
		} else if ((track_status->chg_attach_time_ms -
				    track_status->chg_detach_time_ms <
			    track_chip->track_cfg.general_chg_break_t_thd) &&
			   !track_status->fastchg_break_info.code &&
			   track_status->mmi_chg) {
			if (!break_recording) {
				break_recording = true;
				track_chip->charging_break_trigger.flag_reason =
					TRACK_NOTIFY_FLAG_GENERAL_CHARGING_BREAK;
				oplus_chg_track_record_break_charging_info(
					track_chip, monitor, power_info,
					track_status->wired_break_crux_info);
				schedule_delayed_work(
					&track_chip->charging_break_trigger_work,
					0);
			}
			if (!track_status->wls_need_upload) {
				cancel_delayed_work_sync(
					&track_chip->charger_info_trigger_work);
				cancel_delayed_work_sync(
					&track_chip->no_charging_trigger_work);
				cancel_delayed_work_sync(
					&track_chip->slow_charging_trigger_work);
			}
		} else {
			break_recording = 0;
		}
	} else if (!vbus_rising && (pre_vbus_rising != vbus_rising)) {
		track_status->chg_detach_time_ms =
			local_clock() / TRACK_LOCAL_T_NS_TO_MS_THD;
		oplus_chg_track_handle_wired_type_info(
			monitor, TRACK_CHG_GET_LAST_TIME_TYPE);
		power_info = track_status->power_info;
		track_status->mmi_chg = oplus_chg_track_get_mmi_chg();
		chg_info("chg_detach_time = %llu, mmi_chg=%d\n",
			 track_status->chg_detach_time_ms,
			 track_status->mmi_chg);
	}

	if (vbus_rising) {
		track_status->mmi_chg = oplus_chg_track_get_mmi_chg();
		oplus_chg_track_set_fastchg_break_code(
			TRACK_VOOCPHY_BREAK_DEFAULT);
	}
	pre_vbus_rising = vbus_rising;

	return 0;
}

int oplus_chg_track_cal_tbatt_status(struct oplus_monitor *monitor)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;

	if (!monitor)
		return -EINVAL;
	track_chip = monitor->track;
	if (!track_chip)
		return -EINVAL;
	track_status = &track_chip->track_status;

	if (monitor->batt_status != POWER_SUPPLY_STATUS_CHARGING)
	{
		chg_debug("not charging, should return\n");
		return 0;
	}

	if (!track_status->tbatt_warm_once &&
	    ((monitor->temp_region == TEMP_REGION_WARM) ||
	     (monitor->temp_region == TEMP_REGION_HOT)))
		track_status->tbatt_warm_once = true;

	if (!track_status->tbatt_cold_once &&
	    (monitor->temp_region == TEMP_REGION_COLD))
		track_status->tbatt_cold_once = true;

	chg_info("tbatt_warm_once:%d, tbatt_cold_once:%d\n",
		 track_status->tbatt_warm_once, track_status->tbatt_cold_once);

	return 0;
}

int oplus_chg_track_aging_ffc_check(struct oplus_monitor *monitor, int step)
{
	struct oplus_chg_track *track_chip;
	struct oplus_chg_track_status *track_status;
	int rc;
	int cutoff_fv;

	if (!monitor)
		return -EINVAL;
	track_chip = monitor->track;
	if (!track_chip)
		return -EINVAL;
	track_status = &track_chip->track_status;

	if (step >= FFC_CHG_STEP_MAX) {
		chg_err("step(=%d) is too big\n", step);
		return -EINVAL;
	} else if (step < 0) {
		chg_err("step(=%d) is too small\n", step);
		return -EINVAL;
	}

	if (monitor->wired_online) {
		if (step == 0)
			track_status->aging_ffc_start_time = oplus_chg_track_get_local_time_s();
		rc = oplus_comm_get_wired_aging_ffc_offset(monitor->comm_topic, step);
		if (rc <= 0)
			return rc;
		track_status->aging_ffc_trig = true;
		cutoff_fv = oplus_comm_get_current_wired_ffc_cutoff_fv(monitor->comm_topic, step);
		if (cutoff_fv < 0)
			return cutoff_fv;
		track_status->aging_ffc_judge_vol[step] = cutoff_fv + rc;
	}

	return 0;
}

static int oplus_chg_track_cal_section_soc_inc_rm(
	struct oplus_monitor *monitor,
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

	if (!monitor || !track_status)
		return -1;

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
		if (monitor->batt_soc <= TRACK_REF_SOC_50)
			track_status->soc_sect_status = TRACK_SOC_SECTION_LOW;
		else if (monitor->batt_soc <= TRACK_REF_SOC_75)
			track_status->soc_sect_status =
				TRACK_SOC_SECTION_MEDIUM;
		else if (monitor->batt_soc <= TRACK_REF_SOC_90)
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		else
			track_status->soc_sect_status = TRACK_SOC_SECTION_OVER;
		time_go_next_status = oplus_chg_track_get_local_time_s();
		rm_go_next_status = monitor->batt_rm;
	}

	if (monitor->batt_status != POWER_SUPPLY_STATUS_CHARGING) {
		if (monitor->batt_soc <= TRACK_REF_SOC_50)
			track_status->soc_sect_status = TRACK_SOC_SECTION_LOW;
		else if (monitor->batt_soc <= TRACK_REF_SOC_75)
			track_status->soc_sect_status =
				TRACK_SOC_SECTION_MEDIUM;
		else if (monitor->batt_soc <= TRACK_REF_SOC_90)
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		else
			track_status->soc_sect_status = TRACK_SOC_SECTION_OVER;
		pre_soc_low_sect_cont_time =
			track_status->soc_low_sect_cont_time;
		pre_soc_low_sect_incr_rm = track_status->soc_low_sect_incr_rm;
		pre_soc_medium_sect_cont_time =
			track_status->soc_medium_sect_cont_time;
		pre_soc_medium_sect_incr_rm =
			track_status->soc_medium_sect_incr_rm;
		pre_soc_high_sect_cont_time =
			track_status->soc_high_sect_cont_time;
		pre_soc_high_sect_incr_rm = track_status->soc_high_sect_incr_rm;
		time_go_next_status = oplus_chg_track_get_local_time_s();
		rm_go_next_status = monitor->batt_rm;
		return 0;
	}

	switch (track_status->soc_sect_status) {
	case TRACK_SOC_SECTION_LOW:
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->soc_low_sect_cont_time =
			(curr_time - time_go_next_status) +
			pre_soc_low_sect_cont_time;
		track_status->soc_low_sect_incr_rm =
			(monitor->batt_rm - rm_go_next_status) +
			pre_soc_low_sect_incr_rm;
		if (monitor->batt_soc > TRACK_REF_SOC_50) {
			pre_soc_low_sect_cont_time =
				track_status->soc_low_sect_cont_time;
			pre_soc_low_sect_incr_rm =
				track_status->soc_low_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = monitor->batt_rm;
			track_status->soc_sect_status =
				TRACK_SOC_SECTION_MEDIUM;
		}
		break;
	case TRACK_SOC_SECTION_MEDIUM:
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->soc_medium_sect_cont_time =
			(curr_time - time_go_next_status) +
			pre_soc_medium_sect_cont_time;
		track_status->soc_medium_sect_incr_rm =
			(monitor->batt_rm - rm_go_next_status) +
			pre_soc_medium_sect_incr_rm;
		if (monitor->batt_soc <= TRACK_REF_SOC_50) {
			pre_soc_medium_sect_cont_time =
				track_status->soc_medium_sect_cont_time;
			pre_soc_medium_sect_incr_rm =
				track_status->soc_medium_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = monitor->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_LOW;
		} else if (monitor->batt_soc > TRACK_REF_SOC_75) {
			pre_soc_medium_sect_cont_time =
				track_status->soc_medium_sect_cont_time;
			pre_soc_medium_sect_incr_rm =
				track_status->soc_medium_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = monitor->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		}
		break;
	case TRACK_SOC_SECTION_HIGH:
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->soc_high_sect_cont_time =
			(curr_time - time_go_next_status) +
			pre_soc_high_sect_cont_time;
		track_status->soc_high_sect_incr_rm =
			(monitor->batt_rm - rm_go_next_status) +
			pre_soc_high_sect_incr_rm;
		if (monitor->batt_soc <= TRACK_REF_SOC_75) {
			pre_soc_high_sect_cont_time =
				track_status->soc_high_sect_cont_time;
			pre_soc_high_sect_incr_rm =
				track_status->soc_high_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = monitor->batt_rm;
			track_status->soc_sect_status =
				TRACK_SOC_SECTION_MEDIUM;
		} else if (monitor->batt_soc > TRACK_REF_SOC_90) {
			pre_soc_high_sect_cont_time =
				track_status->soc_high_sect_cont_time;
			pre_soc_high_sect_incr_rm =
				track_status->soc_high_sect_incr_rm;
			time_go_next_status = curr_time;
			rm_go_next_status = monitor->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_OVER;
		}
		break;
	case TRACK_SOC_SECTION_OVER:
		curr_time = oplus_chg_track_get_local_time_s();
		if (monitor->batt_soc < TRACK_REF_SOC_90) {
			time_go_next_status = curr_time;
			rm_go_next_status = monitor->batt_rm;
			track_status->soc_sect_status = TRACK_SOC_SECTION_HIGH;
		}
		break;
	default:
		chg_err("!!!soc section status is invalid\n");
		break;
	}

	chg_info(
		"soc_sect_status:%d, time_go_next_status:%d, rm_go_next_status:%d\n",
		track_status->soc_sect_status, time_go_next_status,
		rm_go_next_status);

	chg_info("soc_low_sect_cont_time:%d, soc_low_sect_incr_rm:%d, "
		 "soc_medium_sect_cont_time:%d, soc_medium_sect_incr_rm:%d "
		 "soc_high_sect_cont_time:%d, soc_high_sect_incr_rm:%d\n",
		 track_status->soc_low_sect_cont_time,
		 track_status->soc_low_sect_incr_rm,
		 track_status->soc_medium_sect_cont_time,
		 track_status->soc_medium_sect_incr_rm,
		 track_status->soc_high_sect_cont_time,
		 track_status->soc_high_sect_incr_rm);
	return 0;
}

static bool oplus_chg_track_burst_soc_sect_speed(
	struct oplus_chg_track_status *track_status,
	struct oplus_chg_track_speed_ref *speed_ref)
{
	bool ret = false;
	int soc_low_sect_incr_rm;
	int soc_medium_sect_incr_rm;
	int soc_high_sect_incr_rm;
	int batt_num;

	if (!track_status || !speed_ref)
		return false;

	if (!track_status->soc_high_sect_cont_time &&
	    !track_status->soc_medium_sect_cont_time &&
	    !track_status->soc_low_sect_cont_time)
		return true;

	chg_info("low_ref_curr:%d, medium_ref_curr:%d, high_ref_curr:%d\n",
		 speed_ref[TRACK_SOC_SECTION_LOW - 1].ref_curr,
		 speed_ref[TRACK_SOC_SECTION_MEDIUM - 1].ref_curr,
		 speed_ref[TRACK_SOC_SECTION_HIGH - 1].ref_curr);

	soc_low_sect_incr_rm = track_status->soc_low_sect_incr_rm;
	soc_medium_sect_incr_rm = track_status->soc_medium_sect_incr_rm;
	soc_high_sect_incr_rm = track_status->soc_high_sect_incr_rm;

	batt_num = oplus_gauge_get_batt_num();
	soc_low_sect_incr_rm *= (TRACK_TIME_1HOU_THD / batt_num);
	soc_medium_sect_incr_rm *= (TRACK_TIME_1HOU_THD / batt_num);
	soc_high_sect_incr_rm *= (TRACK_TIME_1HOU_THD / batt_num);

	if ((track_status->soc_low_sect_cont_time > TRACK_REF_TIME_6S) &&
	    ((soc_low_sect_incr_rm / track_status->soc_low_sect_cont_time) <
	     speed_ref[TRACK_SOC_SECTION_LOW - 1].ref_curr)) {
		chg_info("slow charging when soc low section\n");
		ret = true;
	}

	if (!ret &&
	    (track_status->soc_medium_sect_cont_time > TRACK_REF_TIME_8S) &&
	    ((soc_medium_sect_incr_rm /
	      track_status->soc_medium_sect_cont_time) <
	     speed_ref[TRACK_SOC_SECTION_MEDIUM - 1].ref_curr)) {
		chg_info("slow charging when soc medium section\n");
		ret = true;
	}

	if (!ret &&
	    (track_status->soc_high_sect_cont_time > TRACK_REF_TIME_10S) &&
	    ((soc_high_sect_incr_rm / track_status->soc_high_sect_cont_time) <
	     speed_ref[TRACK_SOC_SECTION_HIGH - 1].ref_curr)) {
		chg_info("slow charging when soc high section\n");
		ret = true;
	}

	return ret;
}

static int oplus_chg_track_get_speed_slow_reason(
	struct oplus_chg_track_status *track_status)
{
	struct oplus_chg_track *chip = g_track_chip;
	char wired_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];
	char wls_adapter_type[OPLUS_CHG_TRACK_POWER_TYPE_LEN];

	if (!track_status || !chip)
		return -1;

	memcpy(wired_adapter_type,
	       track_status->power_info.wired_info.adapter_type,
	       OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	memcpy(wls_adapter_type, track_status->power_info.wls_info.adapter_type,
	       OPLUS_CHG_TRACK_POWER_TYPE_LEN);
	chg_info("wired_adapter_type:%s, wls_adapter_type:%s\n",
		 wired_adapter_type, wls_adapter_type);

	if (track_status->tbatt_warm_once)
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM;
	else if (track_status->tbatt_cold_once)
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_COLD;
	else if ((track_status->power_info.power_type ==
		  TRACK_CHG_TYPE_UNKNOW) ||
		 (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRE &&
		  chip->track_cfg.wired_max_power >
			  track_status->power_info.wired_info.power))
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_NON_STANDARD_PA;
	else if ((track_status->power_info.power_type ==
		  TRACK_CHG_TYPE_WIRELESS) &&
		 (chip->track_cfg.wls_max_power - TRACK_POWER_10000MW >
		  track_status->power_info.wls_info.power) &&
		 (strcmp(wls_adapter_type, "bpp")) &&
		 (strcmp(wls_adapter_type, "epp")))
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_NON_STANDARD_PA;
	else if (track_status->chg_start_soc >= TRACK_REF_SOC_90)
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_BATT_CAP_HIGH;
	else if ((track_status->cool_down_effect_cnt * 100 /
		  track_status->chg_total_cnt) >
		 TRACK_COOLDOWN_CHRGING_TIME_PCT)
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_COOLDOWN;
	else if ((track_status->wls_skew_effect_cnt * 100 /
		  track_status->chg_total_cnt) >
			 TRACK_WLS_SKEWING_CHRGING_TIME_PCT &&
		 track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_WLS_SKEW;
	else if (!track_status->chg_verity &&
		 track_status->power_info.power_type == TRACK_CHG_TYPE_WIRELESS)
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_VERITY_FAIL;
	else
		chip->slow_charging_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CHG_SLOW_OTHER;

	if (track_status->debug_slow_charging_reason)
		chip->slow_charging_trigger.flag_reason =
			track_status->debug_slow_charging_reason +
			TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM - 1;

	chg_info("flag_reason:%d\n", chip->slow_charging_trigger.flag_reason);

	return 0;
}

static bool oplus_chg_track_judge_speed_slow(struct oplus_monitor *monitor,
					     struct oplus_chg_track *track_chip)
{
	struct oplus_chg_track_status *track_status;

	if (!track_chip || !monitor)
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
	} else if (track_status->power_info.power_type ==
		   TRACK_CHG_TYPE_WIRELESS) {
		if (!strcmp(track_status->power_info.wls_info.adapter_type,
			    "unknow")) {
			track_status->chg_speed_is_slow = true;
		} else if (!strcmp(track_status->power_info.wls_info
					   .adapter_type,
				   "epp")) {
			if (!track_status->wls_epp_speed_ref)
				return false;
			track_status->chg_speed_is_slow =
				oplus_chg_track_burst_soc_sect_speed(
					track_status,
					track_status->wls_epp_speed_ref);
		} else if (!strcmp(track_status->power_info.wls_info
					   .adapter_type,
				   "bpp")) {
			if (!track_status->wls_bpp_speed_ref)
				return false;
			track_status->chg_speed_is_slow =
				oplus_chg_track_burst_soc_sect_speed(
					track_status,
					track_status->wls_bpp_speed_ref);
		} else {
			if (!track_status->wls_airvooc_speed_ref)
				return false;
			track_status->chg_speed_is_slow =
				oplus_chg_track_burst_soc_sect_speed(
					track_status,
					track_status->wls_airvooc_speed_ref);
		}
	} else {
		if (!track_status->wired_speed_ref)
			return false;
		track_status->chg_speed_is_slow =
			oplus_chg_track_burst_soc_sect_speed(
				track_status, track_status->wired_speed_ref);
	}

	if (track_status->chg_speed_is_slow ||
	    track_status->debug_slow_charging_force) {
		oplus_chg_track_get_speed_slow_reason(track_status);
	}

	return (track_status->chg_speed_is_slow ||
		track_status->debug_slow_charging_force);
}

static int
oplus_chg_track_cal_batt_full_time(struct oplus_monitor *monitor,
				   struct oplus_chg_track_status *track_status)
{
	int curr_time;

	if (!monitor || !track_status || !g_track_chip)
		return -1;

	if (!track_status->chg_fast_full_time &&
	    ((monitor->ffc_status != FFC_DEFAULT) ||
	     track_status->wls_prop_status == TRACK_WLS_FASTCHG_FULL ||
	     track_status->debug_fast_prop_status ==
		     TRACK_FASTCHG_STATUS_NORMAL)) {
		track_status->chg_report_full_time = 0;
		track_status->chg_normal_full_time = 0;
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->chg_fast_full_time =
			curr_time - track_status->chg_start_time;
		track_status->chg_fast_full_time /= TRACK_TIME_1MIN_THD;
		if (!track_status->chg_fast_full_time)
			track_status->chg_fast_full_time =
				TRACK_TIME_1MIN_THD / TRACK_TIME_1MIN_THD;

		if (track_status->chg_average_speed ==
		    TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT)
			track_status->chg_average_speed =
				TRACK_TIME_1MIN_THD *
				(monitor->batt_rm -
				 track_status->chg_start_rm) /
				(curr_time - track_status->chg_start_time);
		chg_info("curr_time:%d, start_time:%d, fast_full_time:%d"
			 "curr_rm:%d, chg_start_rm:%d, chg_average_speed:%d\n",
			 curr_time, track_status->chg_start_time,
			 track_status->chg_fast_full_time, monitor->batt_rm,
			 track_status->chg_start_rm,
			 track_status->chg_average_speed);
	}

	if (!track_status->chg_report_full_time &&
	    (monitor->batt_status == POWER_SUPPLY_STATUS_FULL ||
	     track_status->debug_normal_prop_status ==
		     POWER_SUPPLY_STATUS_FULL)) {
		oplus_chg_track_handle_batt_full_reason(monitor, track_status);
		oplus_chg_track_judge_speed_slow(monitor, g_track_chip);
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->chg_report_full_time =
			curr_time - track_status->chg_start_time;
		track_status->chg_report_full_time /= TRACK_TIME_1MIN_THD;
		if (!track_status->chg_report_full_time)
			track_status->chg_report_full_time =
				TRACK_TIME_1MIN_THD / TRACK_TIME_1MIN_THD;
	}

	if (!track_status->chg_normal_full_time &&
	    (monitor->batt_status == POWER_SUPPLY_STATUS_FULL ||
	     track_status->debug_normal_charging_state ==
		     POWER_SUPPLY_STATUS_FULL)) {
		curr_time = oplus_chg_track_get_local_time_s();
		if (track_status->aging_ffc_start_time)
			track_status->aging_ffc_to_full_time = curr_time - track_status->aging_ffc_start_time;
		track_status->chg_normal_full_time =
			curr_time - track_status->chg_start_time;
		track_status->chg_normal_full_time /= TRACK_TIME_1MIN_THD;
		if (!track_status->chg_normal_full_time)
			track_status->chg_normal_full_time =
				TRACK_TIME_1MIN_THD / TRACK_TIME_1MIN_THD;
		if (track_status->chg_average_speed ==
		    TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT)
			track_status->chg_average_speed =
				TRACK_TIME_1MIN_THD *
				(monitor->batt_rm -
				 track_status->chg_start_rm) /
				(curr_time - track_status->chg_start_time);
		chg_info("curr_time:%d, start_time:%d, normal_full_time:%d"
			 "curr_rm:%d, chg_start_rm:%d, chg_average_speed:%d\n",
			 curr_time, track_status->chg_start_time,
			 track_status->chg_normal_full_time, monitor->batt_rm,
			 track_status->chg_start_rm,
			 track_status->chg_average_speed);
	}

	return 0;
}

static int
oplus_chg_track_get_charger_type(struct oplus_monitor *monitor,
				 struct oplus_chg_track_status *track_status,
				 int type)
{
	if (monitor == NULL || track_status == NULL)
		return -EINVAL;

	if (monitor->wls_online)
		oplus_chg_track_handle_wls_type_info(track_status);
	else if (monitor->wired_online)
		oplus_chg_track_handle_wired_type_info(monitor, type);

	return 0;
}

static int oplus_chg_track_obtain_power_info(char *power_info, int len)
{
	int index = 0;
	struct oplus_chg_track_status temp_track_status;
	struct oplus_chg_track_status *track_status = &temp_track_status;
	struct oplus_monitor *monitor;

	if (!power_info || !g_track_chip)
		return -1;

	monitor = g_track_chip->monitor;
	oplus_chg_track_get_charger_type(monitor, track_status,
					 TRACK_CHG_GET_THTS_TIME_TYPE);

	index += snprintf(&(power_info[index]), len - index, "$$power_mode@@%s",
			  track_status->power_info.power_mode);

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRE) {
		index += snprintf(
			&(power_info[index]), len - index, "$$adapter_t@@%s",
			track_status->power_info.wired_info.adapter_type);
		if (track_status->power_info.wired_info.adapter_id)
			index += snprintf(
				&(power_info[index]), len - index,
				"$$adapter_id@@0x%x",
				track_status->power_info.wired_info.adapter_id);
		index += snprintf(&(power_info[index]), len - index,
				  "$$power@@%d",
				  track_status->power_info.wired_info.power);
	} else if (track_status->power_info.power_type ==
		   TRACK_CHG_TYPE_WIRELESS) {
		index += snprintf(
			&(power_info[index]), len - index, "$$adapter_t@@%s",
			track_status->power_info.wls_info.adapter_type);
		if (strlen(track_status->power_info.wls_info.dock_type))
			index += snprintf(
				&(power_info[index]), len - index,
				"$$dock_type@@%s",
				track_status->power_info.wls_info.dock_type);
		index += snprintf(&(power_info[index]), len - index,
				  "$$power@@%d",
				  track_status->power_info.wls_info.power);
	}

	index += snprintf(&(power_info[index]), len - index, "$$soc@@%d",
			  monitor->batt_soc);
	index += snprintf(&(power_info[index]), len - index, "$$batt_temp@@%d",
			  monitor->batt_temp);
	index += snprintf(&(power_info[index]), len - index, "$$shell_temp@@%d",
			  monitor->shell_temp);
	index += snprintf(&(power_info[index]), len - index, "$$batt_vol@@%d",
			  monitor->vbat_mv);
	index += snprintf(&(power_info[index]), len - index, "$$batt_curr@@%d",
			  monitor->ibat_ma);

	return 0;
}

static int
oplus_chg_track_get_plat_pmic_err_comm_info(struct oplus_chg_track *track,
					    char *buf, int buf_size)
{
	struct oplus_chg_track_status *track_status;
	struct oplus_monitor *monitor;
	int index;

	monitor = track->monitor;
	track_status = &track->track_status;

	oplus_chg_track_get_charger_type(monitor, track_status,
					 TRACK_CHG_GET_THTS_TIME_TYPE);
	index = snprintf(buf, buf_size, "$$power_mode@@%s",
			 track_status->power_info.power_mode);

	if (track_status->power_info.power_type == TRACK_CHG_TYPE_WIRE) {
		index += snprintf(
			buf + index, buf_size - index, "$$adapter_t@@%s",
			track_status->power_info.wired_info.adapter_type);
		if (track_status->power_info.wired_info.adapter_id)
			index += snprintf(
				buf + index, buf_size - index,
				"$$adapter_id@@0x%x",
				track_status->power_info.wired_info.adapter_id);
		index += snprintf(buf + index, buf_size - index, "$$power@@%d",
				  track_status->power_info.wired_info.power);
		index += snprintf(buf + index, buf_size - index, "$$vbus@@%d",
				  monitor->wired_vbus_mv);
	} else if (track_status->power_info.power_type ==
		   TRACK_CHG_TYPE_WIRELESS) {
		index += snprintf(
			buf + index, buf_size - index, "$$adapter_t@@%s",
			track_status->power_info.wls_info.adapter_type);
		if (strlen(track_status->power_info.wls_info.dock_type))
			index += snprintf(
				buf + index, buf_size - index,
				"$$dock_type@@%s",
				track_status->power_info.wls_info.dock_type);
		index += snprintf(buf + index, buf_size - index, "$$power@@%d",
				  track_status->power_info.wls_info.power);
		index += snprintf(buf + index, buf_size - index, "$$vbus@@%d",
				  monitor->wls_vbus_mv);
	}

	return index;
}

static int oplus_chg_track_upload_ic_err_info(struct oplus_chg_track *track)
{
	union mms_msg_data data = { 0 };
	int index;
	int name_index, msg_index;
	int err_type, sub_err_type;
	char *msg_buf, *track_buf;
	int rc;

	rc = oplus_mms_get_item_data(track->monitor->err_topic, ERR_ITEM_IC,
				     &data, false);
	if (rc < 0) {
		chg_err("get msg data error, rc=%d\n", rc);
		return rc;
	}
	msg_buf = data.strval;
	track_buf = track->ic_err_msg_load_trigger.crux_info;

	msg_buf = kzalloc(TOPIC_MSG_STR_BUF, GFP_KERNEL);
	if (msg_buf == NULL) {
		chg_err("alloc msg buf error");
		return -ENOMEM;
	}
	memcpy(msg_buf, data.strval, TOPIC_MSG_STR_BUF);

	rc = oplus_mms_analysis_ic_err_msg(msg_buf, TOPIC_MSG_STR_BUF,
					   &name_index, &err_type,
					   &sub_err_type, &msg_index);
	if (rc < 0) {
		chg_err("error msg could not be analysis,  rc=%d\n", rc);
		kfree(msg_buf);
		return rc;
	}
	chg_info("err_type=%d, sub_err_type=%d\n", err_type, sub_err_type);

	index = snprintf(track_buf, OPLUS_CHG_TRACK_CURX_INFO_LEN,
			 "$$device_id@@%s$$err_type@@%d", msg_buf + name_index,
			 sub_err_type);

	switch (err_type) {
	case OPLUS_IC_ERR_GPIO:
		track->ic_err_msg_load_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_GPIO_ABNORMAL;
		break;
	case OPLUS_IC_ERR_PLAT_PMIC:
		track->ic_err_msg_load_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_PLAT_PMIC_ABNORMAL;
		index += oplus_chg_track_get_plat_pmic_err_comm_info(track,
			track_buf + index, OPLUS_CHG_TRIGGER_MSG_LEN - index);
		break;
	case OPLUS_IC_ERR_BUCK_BOOST:
		track->ic_err_msg_load_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_EXTERN_PMIC_ABNORMAL;
		break;
	case OPLUS_IC_ERR_GAUGE:
		track->ic_err_msg_load_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_GAGUE_ABNORMAL;
		break;
	case OPLUS_IC_ERR_WLS_RX:
		track->ic_err_msg_load_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
		break;
	case OPLUS_IC_ERR_CP:
		track->ic_err_msg_load_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_CP_ABNORMAL;
		break;
	case OPLUS_IC_ERR_CC_LOGIC:
		track->ic_err_msg_load_trigger.flag_reason =
			TRACK_NOTIFY_FLAG_EXTERN_PMIC_ABNORMAL;
		break;
	case OPLUS_IC_ERR_I2C:
	case OPLUS_IC_ERR_UNKNOWN:
	default:
		chg_err("unsupported error type(%d)\n", err_type);
		kfree(msg_buf);
		return -EINVAL;
	}

	index += snprintf(track_buf + index, OPLUS_CHG_TRIGGER_MSG_LEN - index,
			  "$$ic_msg@@%s", msg_buf + msg_index);

	schedule_delayed_work(&track->ic_err_msg_trigger_work, 0);
	chg_info("%s\n", track_buf);
	kfree(msg_buf);

	return 0;
}

static int oplus_chg_track_upload_usbtemp_info(struct oplus_chg_track *track)
{
	union mms_msg_data data = { 0 };
	int index;
	int rc;

	rc = oplus_mms_get_item_data(track->monitor->err_topic,
				     ERR_ITEM_USBTEMP, &data, false);
	if (rc < 0) {
		chg_err("get msg data error, rc=%d\n", rc);
		return rc;
	}
	index = snprintf(track->usbtemp_load_trigger.crux_info,
			 OPLUS_CHG_TRACK_CURX_INFO_LEN, "%s", data.strval);
	oplus_chg_track_obtain_power_info(
		&track->usbtemp_load_trigger.crux_info[index],
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index);

	schedule_delayed_work(&track->usbtemp_load_trigger_work, 0);
	chg_info("%s\n", track->usbtemp_load_trigger.crux_info);

	return 0;
}

static int
oplus_chg_track_upload_vbatt_too_low_info(struct oplus_chg_track *track)
{
	union mms_msg_data data = { 0 };
	int rc;

	rc = oplus_mms_get_item_data(track->monitor->err_topic,
				     ERR_ITEM_VBAT_TOO_LOW, &data, false);
	if (rc < 0) {
		chg_err("get msg data error, rc=%d\n", rc);
		return rc;
	}
	snprintf(track->vbatt_too_low_load_trigger.crux_info,
		 OPLUS_CHG_TRACK_CURX_INFO_LEN, "%s", data.strval);

	schedule_delayed_work(&track->vbatt_too_low_load_trigger_work, 0);
	chg_info("%s\n", track->vbatt_too_low_load_trigger.crux_info);

	return 0;
}

static int
oplus_chg_track_upload_vbatt_diff_over_info(struct oplus_chg_track *track)
{
	struct oplus_monitor *monitor = track->monitor;

	(void)snprintf(track->vbatt_diff_over_load_trigger.crux_info,
		       OPLUS_CHG_TRACK_CURX_INFO_LEN,
		       "$$soc@@%d$$s_soc@@%d$$uisoc@@%d$$vbatt_max@@%d$$vbatt_min@@%d"
		       "$$vbatt_diff@@%d$$charger_exist@@%d$$vdiff_type@@%s",
		       monitor->batt_soc, monitor->smooth_soc, monitor->ui_soc,
		       monitor->vbat_mv, monitor->vbat_min_mv,
		       monitor->vbat_mv - monitor->vbat_min_mv,
		       (monitor->wired_online || monitor->wls_online),
		       (monitor->batt_status == POWER_SUPPLY_STATUS_FULL) ?
			       "full" : "shutdown");

	schedule_delayed_work(&track->vbatt_diff_over_load_trigger_work, 0);
	chg_info("%s\n", track->vbatt_diff_over_load_trigger.crux_info);

	return 0;
}

int oplus_chg_track_charge_full(struct oplus_monitor *monitor)
{
	struct oplus_chg_track *chip;

	if (monitor == NULL) {
		chg_err("monitor is NULL\n");
		return -ENODEV;
	}

	chip = monitor->track;
	if (monitor->batt_status == POWER_SUPPLY_STATUS_FULL)
		oplus_chg_track_upload_vbatt_diff_over_info(chip);

	return 0;
}

static int
oplus_chg_track_upload_uisoc_keep_1_t_info(struct oplus_chg_track *chip)
{
	struct oplus_monitor *monitor = chip->monitor;
	int index = 0;
	int uisoc_1_end_time;

	uisoc_1_end_time = oplus_chg_track_get_local_time_s();
	chg_info("uisoc_1_end_time:%d, uisoc_1_start_time:%d\n",
		 uisoc_1_end_time, chip->uisoc_1_start_time);
	memset(chip->uisoc_keep_1_t_load_trigger.crux_info, 0,
	       sizeof(chip->uisoc_keep_1_t_load_trigger.crux_info));
	index += snprintf(&(chip->uisoc_keep_1_t_load_trigger.crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$uisoc_keep_1_t@@%d",
			  uisoc_1_end_time - chip->uisoc_1_start_time);

	index += snprintf(
		&(chip->uisoc_keep_1_t_load_trigger.crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$pre_vbatt_max@@%d$$pre_vbatt_min@@%d$$curr_vbatt_max@@%d"
		"$$curr_vbatt_min@@%d$$soc@@%d$$s_soc@@%d$$uisoc@@%d$$start_batt_rm@@%d"
		"$$curr_batt_rm@@%d$$batt_curr@@%d",
		chip->uisoc_1_start_vbatt_max, chip->uisoc_1_start_vbatt_min,
		monitor->vbat_mv, monitor->vbat_min_mv, monitor->batt_soc,
		monitor->smooth_soc, monitor->ui_soc, chip->uisoc_1_start_batt_rm,
		monitor->batt_rm, monitor->ibat_ma);

	schedule_delayed_work(&chip->uisoc_keep_1_t_load_trigger_work, 0);
	chg_info("%s\n", chip->uisoc_keep_1_t_load_trigger.crux_info);
	msleep(200);

	return 0;
}

int oplus_chg_track_set_uisoc_1_start(struct oplus_monitor *monitor)
{
	struct oplus_chg_track *chip;

	if (monitor == NULL) {
		chg_err("monitor is NULL\n");
		return -ENODEV;
	}

	chip = monitor->track;
	chip->uisoc_1_start_time = oplus_chg_track_get_local_time_s();
	chip->uisoc_1_start_batt_rm = monitor->batt_rm;
	chip->uisoc_1_start_vbatt_max = monitor->vbat_mv;
	chip->uisoc_1_start_vbatt_min = monitor->vbat_min_mv;

	return 0;
}

static int
oplus_chg_track_cal_period_chg_capaticy(struct oplus_chg_track *track_chip)
{
	int ret = 0;

	if (!track_chip)
		return -EFAULT;

	if (track_chip->track_status.chg_start_soc >
	    TRACK_PERIOD_CHG_CAP_MAX_SOC)
		return ret;

	chg_debug("enter\n");
	schedule_delayed_work(&track_chip->cal_chg_five_mins_capacity_work,
			      msecs_to_jiffies(TRACK_TIME_5MIN_JIFF_THD));
	schedule_delayed_work(&track_chip->cal_chg_ten_mins_capacity_work,
			      msecs_to_jiffies(TRACK_TIME_10MIN_JIFF_THD));

	return ret;
}

static int oplus_chg_track_cancel_cal_period_chg_capaticy(
	struct oplus_chg_track *track_chip)
{
	int ret = 0;

	if (!track_chip)
		return -EFAULT;

	chg_debug("enter\n");
	if (delayed_work_pending(&track_chip->cal_chg_five_mins_capacity_work))
		cancel_delayed_work_sync(
			&track_chip->cal_chg_five_mins_capacity_work);

	if (delayed_work_pending(&track_chip->cal_chg_ten_mins_capacity_work))
		cancel_delayed_work_sync(
			&track_chip->cal_chg_ten_mins_capacity_work);

	return ret;
}

static int oplus_chg_track_cal_period_chg_average_speed(
	struct oplus_chg_track_status *track_status,
	struct oplus_monitor *monitor)
{
	int ret = 0;
	int curr_time;

	if (!track_status || !monitor)
		return -EFAULT;

	chg_debug("enter\n");
	if (track_status->chg_average_speed ==
	    TRACK_PERIOD_CHG_AVERAGE_SPEED_INIT) {
		curr_time = oplus_chg_track_get_local_time_s();
		track_status->chg_average_speed =
			TRACK_TIME_1MIN_THD *
			(monitor->batt_rm - track_status->chg_start_rm) /
			(curr_time - track_status->chg_start_time);
		chg_info("curr_rm:%d, chg_start_rm:%d, curr_time:%d, chg_start_time:%d,"
			"chg_average_speed:%d\n",
			monitor->batt_rm, track_status->chg_start_rm, curr_time,
			track_status->chg_start_time,
			track_status->chg_average_speed);
	}

	return ret;
}

static int
oplus_chg_track_cal_chg_end_soc(struct oplus_monitor *monitor,
				struct oplus_chg_track_status *track_status)
{
	if (!track_status || !monitor)
		return -EFAULT;

	if (monitor->batt_status == POWER_SUPPLY_STATUS_CHARGING)
		track_status->chg_end_soc = monitor->batt_soc;

	return 0;
}

static void oplus_chg_track_reset_chg_abnormal_happened_flag(
	struct oplus_chg_track_status *track_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chg_abnormal_reason_table); i++)
		chg_abnormal_reason_table[i].happened = false;
}

static int
oplus_chg_track_status_reset(struct oplus_chg_track_status *track_status)
{
	memset(&(track_status->power_info), 0,
	       sizeof(track_status->power_info));
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
	track_status->chg_fast_full_time = 0;
	track_status->chg_normal_full_time = 0;
	track_status->chg_report_full_time = 0;
	track_status->aging_ffc_trig = false;
	memset(&(track_status->aging_ffc_judge_vol), 0, sizeof(track_status->aging_ffc_judge_vol));
	track_status->aging_ffc_start_time = 0;
	track_status->aging_ffc_to_full_time = 0;
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
	memset(&(track_status->batt_full_reason), 0,
	       sizeof(track_status->batt_full_reason));
	oplus_chg_track_clear_cool_down_stats_time(track_status);
	memset(&(track_status->chg_abnormal_reason), 0,
	       sizeof(track_status->chg_abnormal_reason));
	oplus_chg_track_reset_chg_abnormal_happened_flag(track_status);

	return 0;
}

static int oplus_chg_track_status_reset_when_plugin(
	struct oplus_monitor *monitor,
	struct oplus_chg_track_status *track_status)
{
	track_status->soc_sect_status = TRACK_SOC_SECTION_DEFAULT;
	track_status->chg_speed_is_slow = false;
	track_status->chg_start_time = oplus_chg_track_get_local_time_s();
	track_status->chg_end_time = track_status->chg_start_time;
	track_status->chg_start_soc = monitor->batt_soc;
	track_status->led_on = monitor->led_on;
	track_status->led_change_t = track_status->chg_start_time;
	track_status->led_change_rm = monitor->batt_rm;
	track_status->chg_start_temp = monitor->shell_temp;
	track_status->batt_start_temp = monitor->batt_temp;
	track_status->batt_max_temp = monitor->batt_temp;
	track_status->batt_max_vol = monitor->vbat_mv;
	track_status->batt_max_curr = monitor->ibat_ma;
	track_status->chg_start_rm = monitor->batt_rm;
	track_status->chg_max_temp = monitor->shell_temp;
	track_status->ledon_time = 0;
	track_status->ledoff_time = 0;
	track_status->continue_ledon_time = 0;
	track_status->continue_ledoff_time = 0;
	track_status->cool_down_status = monitor->cool_down;
	track_status->cool_down_status_change_t = track_status->chg_start_time;
	track_status->has_judge_speed = false;
	track_status->wls_prop_status = TRACK_CHG_DEFAULT;
	track_status->wls_skew_effect_cnt = 0;
	track_status->chg_verity = true;
	track_status->chg_plugin_utc_t = oplus_chg_track_get_current_time_s(
		&track_status->chg_plugin_rtc_t);
	oplus_chg_track_cal_period_chg_capaticy(g_track_chip);
	track_status->prop_status = monitor->batt_status;
	track_status->once_mmi_chg = false;
	track_status->once_chg_cycle_status = CHG_CYCLE_VOTER__NONE;
	chg_info("chg_start_time:%d, chg_start_soc:%d, chg_start_temp:%d, prop_status:%d\n",
		track_status->chg_start_time, track_status->chg_start_soc,
		track_status->chg_start_temp, track_status->prop_status);

	return 0;
}

static int oplus_chg_track_speed_check(struct oplus_monitor *monitor)
{
	int ret = 0;
	int wls_break_work_delay_t;
	int wired_break_work_delay_t;
	int track_recording_time;
	static bool track_reset = true;
	static bool track_record_charger_info = false;
	struct oplus_chg_track_status *track_status;

	if (!g_track_chip)
		return -EFAULT;

	track_status = &g_track_chip->track_status;
	wls_break_work_delay_t = g_track_chip->track_cfg.wls_chg_break_t_thd +
				 TRACK_TIME_1000MS_JIFF_THD;
	wired_break_work_delay_t =
		g_track_chip->track_cfg.fast_chg_break_t_thd +
		TRACK_TIME_500MS_JIFF_THD;

	if (!monitor->wired_online && !monitor->wls_online) {
		if (track_record_charger_info) {
			chg_info("record charger info and upload charger info\n");
			track_status->chg_plugout_utc_t =
				oplus_chg_track_get_current_time_s(
					&track_status->chg_plugout_rtc_t);
			oplus_chg_track_cal_led_on_stats(monitor, track_status);
			oplus_chg_track_cal_period_chg_average_speed(
				track_status, monitor);
			oplus_chg_track_cal_ledon_ledoff_average_speed(
				track_status);
			if (track_status->power_info.power_type ==
			    TRACK_CHG_TYPE_WIRELESS)
				track_status->wls_need_upload = true;
			else
				track_status->wired_need_upload = true;
			if (oplus_chg_track_is_no_charging(track_status)) {
				oplus_chg_track_record_charger_info(
					monitor,
					&g_track_chip->no_charging_trigger,
					track_status);
				if (track_status->power_info.power_type ==
				    TRACK_CHG_TYPE_WIRELESS)
					schedule_delayed_work(
						&g_track_chip
							 ->no_charging_trigger_work,
						msecs_to_jiffies(
							wls_break_work_delay_t));
				else
					schedule_delayed_work(
						&g_track_chip
							 ->no_charging_trigger_work,
						msecs_to_jiffies(
							wired_break_work_delay_t));
			} else if (oplus_chg_track_judge_speed_slow(
					   monitor, g_track_chip)) {
				oplus_chg_track_record_charger_info(
					monitor,
					&g_track_chip->slow_charging_trigger,
					track_status);
				if (track_status->power_info.power_type ==
				    TRACK_CHG_TYPE_WIRELESS)
					schedule_delayed_work(
						&g_track_chip
							 ->slow_charging_trigger_work,
						msecs_to_jiffies(
							wls_break_work_delay_t));
				else
					schedule_delayed_work(
						&g_track_chip
							 ->slow_charging_trigger_work,
						msecs_to_jiffies(
							wired_break_work_delay_t));
			} else {
				oplus_chg_track_record_charger_info(
					monitor,
					&g_track_chip->charger_info_trigger,
					track_status);
				if (track_status->power_info.power_type ==
				    TRACK_CHG_TYPE_WIRELESS)
					schedule_delayed_work(
						&g_track_chip
							 ->charger_info_trigger_work,
						msecs_to_jiffies(
							wls_break_work_delay_t));
				else
					schedule_delayed_work(
						&g_track_chip
							 ->charger_info_trigger_work,
						msecs_to_jiffies(
							wired_break_work_delay_t));
			}
		}
		track_reset = true;
		track_record_charger_info = false;
		oplus_chg_track_cancel_cal_period_chg_capaticy(g_track_chip);
		oplus_chg_track_status_reset(track_status);
		return ret;
	}

	if ((monitor->wired_online || monitor->wls_online) && track_reset) {
		track_reset = false;
		oplus_chg_track_status_reset_when_plugin(monitor, track_status);
	}

	track_status->chg_end_time = oplus_chg_track_get_local_time_s();
	track_recording_time =
		track_status->chg_end_time - track_status->chg_start_time;
	if (!track_record_charger_info &&
	    track_recording_time <= TRACK_CYCLE_RECORDIING_TIME_90S)
		oplus_chg_track_get_charger_type(monitor, track_status,
						 TRACK_CHG_GET_THTS_TIME_TYPE);

	oplus_chg_track_cal_tbatt_status(monitor);
	oplus_chg_track_cal_section_soc_inc_rm(monitor, track_status);
	oplus_chg_track_cal_batt_full_time(monitor, track_status);
	oplus_chg_track_cal_chg_common_mesg(monitor, track_status);
	oplus_chg_track_cal_cool_down_stats(monitor, track_status);
	oplus_chg_track_cal_no_charging_stats(monitor, track_status);
	oplus_chg_track_cal_led_on_stats(monitor, track_status);
	oplus_chg_track_check_chg_abnormal(monitor, track_status);
	oplus_chg_track_cal_rechg_counts(monitor, track_status);
	oplus_chg_track_cal_chg_end_soc(monitor, track_status);

	track_status->prop_status = monitor->batt_status;
	if (!track_record_charger_info &&
	    monitor->batt_status == POWER_SUPPLY_STATUS_FULL &&
	    track_recording_time < TRACK_CYCLE_RECORDIING_TIME_2MIN)
		oplus_chg_track_cancel_cal_period_chg_capaticy(g_track_chip);

	if (!track_record_charger_info &&
	    track_recording_time >= TRACK_CYCLE_RECORDIING_TIME_2MIN)
		track_record_charger_info = true;

	chg_debug("track_recording_time=%d, track_record_charger_info=%d\n",
		  track_recording_time, track_record_charger_info);

	return ret;
}

static int oplus_chg_track_uisoc_soc_jump_check(struct oplus_monitor *monitor)
{
	int ret = 0;
	struct oplus_chg_track_status *track_status;

	if (!g_track_chip)
		return -EFAULT;

	track_status = &g_track_chip->track_status;
	if (track_status->curr_soc == -EINVAL) {
		track_status->curr_soc = monitor->batt_soc;
		track_status->pre_soc = monitor->batt_soc;
		track_status->curr_uisoc = monitor->ui_soc;
		track_status->pre_uisoc = monitor->ui_soc;
		if (abs(track_status->curr_uisoc - track_status->curr_soc) >
		    OPLUS_CHG_TRACK_UI_S0C_LOAD_JUMP_THD) {
			track_status->uisoc_load_jumped = true;
			chg_info(
				"The gap between loaded uisoc and soc is too large\n");
			memset(g_track_chip->uisoc_load_trigger.crux_info, 0,
			       sizeof(g_track_chip->uisoc_load_trigger
					      .crux_info));
			ret = snprintf(
				g_track_chip->uisoc_load_trigger.crux_info,
				OPLUS_CHG_TRACK_CURX_INFO_LEN,
				"$$curr_uisoc@@%d$$curr_soc@@%d$$load_uisoc_soc_gap@@%d$$charger_exist@@%d",
				track_status->curr_uisoc,
				track_status->curr_soc,
				track_status->curr_uisoc -
					track_status->curr_soc,
				(monitor->wired_online || monitor->wls_online));
			schedule_delayed_work(
				&g_track_chip->uisoc_load_trigger_work,
				msecs_to_jiffies(10000));
		}
	} else {
		track_status->curr_soc =
			track_status->debug_soc !=
					OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID ?
				track_status->debug_soc :
				      monitor->batt_soc;
		track_status->curr_uisoc =
			track_status->debug_uisoc !=
					OPLUS_CHG_TRACK_DEBUG_UISOC_SOC_INVALID ?
				track_status->debug_uisoc :
				      monitor->ui_soc;
	}

	if (!track_status->soc_jumped &&
	    abs(track_status->curr_soc - track_status->pre_soc) >
		    OPLUS_CHG_TRACK_S0C_JUMP_THD) {
		track_status->soc_jumped = true;
		chg_info("The gap between curr_soc and pre_soc is too large\n");
		memset(g_track_chip->soc_trigger.crux_info, 0,
		       sizeof(g_track_chip->soc_trigger.crux_info));
		ret = snprintf(
			g_track_chip->soc_trigger.crux_info,
			OPLUS_CHG_TRACK_CURX_INFO_LEN,
			"$$curr_soc@@%d$$pre_soc@@%d$$curr_soc_pre_soc_gap@@%d$$charger_exist@@%d",
			track_status->curr_soc, track_status->pre_soc,
			track_status->curr_soc - track_status->pre_soc,
			(monitor->wired_online || monitor->wls_online));
		schedule_delayed_work(&g_track_chip->soc_trigger_work, 0);
	} else {
		if (track_status->soc_jumped &&
		    track_status->curr_soc == track_status->pre_soc)
			track_status->soc_jumped = false;
	}

	if (!track_status->uisoc_jumped &&
	    abs(track_status->curr_uisoc - track_status->pre_uisoc) >
		    OPLUS_CHG_TRACK_UI_S0C_JUMP_THD) {
		track_status->uisoc_jumped = true;
		chg_info("The gap between curr_uisoc and pre_uisoc is too large\n");
		memset(g_track_chip->uisoc_trigger.crux_info, 0,
		       sizeof(g_track_chip->uisoc_trigger.crux_info));
		ret = snprintf(
			g_track_chip->uisoc_trigger.crux_info,
			OPLUS_CHG_TRACK_CURX_INFO_LEN,
			"$$curr_uisoc@@%d$$pre_uisoc@@%d$$curr_uisoc_pre_uisoc_gap@@%d$$charger_exist@@%d",
			track_status->curr_uisoc, track_status->pre_uisoc,
			track_status->curr_uisoc - track_status->pre_uisoc,
			(monitor->wired_online || monitor->wls_online));
		schedule_delayed_work(&g_track_chip->uisoc_trigger_work, 0);
	} else {
		if (track_status->uisoc_jumped &&
		    track_status->curr_uisoc == track_status->pre_uisoc)
			track_status->uisoc_jumped = false;
	}

	if (!track_status->uisoc_to_soc_jumped &&
	    !track_status->uisoc_load_jumped &&
	    abs(track_status->curr_uisoc - track_status->curr_soc) >
		    OPLUS_CHG_TRACK_UI_SOC_TO_S0C_JUMP_THD) {
		track_status->uisoc_to_soc_jumped = true;
		chg_info(
			"The gap between curr_uisoc and curr_soc is too large\n");
		memset(g_track_chip->uisoc_to_soc_trigger.crux_info, 0,
		       sizeof(g_track_chip->uisoc_to_soc_trigger.crux_info));
		ret = snprintf(
			g_track_chip->uisoc_to_soc_trigger.crux_info,
			OPLUS_CHG_TRACK_CURX_INFO_LEN,
			"$$curr_uisoc@@%d$$curr_soc@@%d$$curr_uisoc_curr_soc_gap@@%d$$charger_exist@@%d",
			track_status->curr_uisoc, track_status->curr_soc,
			track_status->curr_uisoc - track_status->curr_soc,
			(monitor->wired_online || monitor->wls_online));
		schedule_delayed_work(&g_track_chip->uisoc_to_soc_trigger_work,
				      0);
	} else {
		if (track_status->curr_uisoc == track_status->curr_soc) {
			track_status->uisoc_to_soc_jumped = false;
			track_status->uisoc_load_jumped = false;
		}
	}

	chg_debug("debug_soc:0x%x, debug_uisoc:0x%x, pre_soc:%d, curr_soc:%d,"
		  "pre_uisoc:%d, curr_uisoc:%d\n",
		  track_status->debug_soc, track_status->debug_uisoc,
		  track_status->pre_soc, track_status->curr_soc,
		  track_status->pre_uisoc, track_status->curr_uisoc);

	track_status->pre_soc = track_status->curr_soc;
	track_status->pre_uisoc = track_status->curr_uisoc;

	return ret;
}

int oplus_chg_track_comm_monitor(struct oplus_monitor *monitor)
{
	int ret = 0;

	if (!monitor)
		return -EFAULT;

	if (likely(monitor->ui_soc_ready))
		ret = oplus_chg_track_uisoc_soc_jump_check(monitor);
	ret |= oplus_chg_track_speed_check(monitor);

	return ret;
}

static void oplus_chg_track_err_subs_callback(struct mms_subscribe *subs,
					      enum mms_msg_type type, u32 id)
{
	struct oplus_chg_track *track = subs->priv_data;

	switch (type) {
	case MSG_TYPE_ITEM:
		chg_err("err msg id:%d\n", id);
		switch (id) {
		case ERR_ITEM_IC:
			oplus_chg_track_upload_ic_err_info(track);
			break;
		case ERR_ITEM_USBTEMP:
			oplus_chg_track_upload_usbtemp_info(track);
			break;
		case ERR_ITEM_VBAT_TOO_LOW:
			oplus_chg_track_upload_vbatt_too_low_info(track);
			break;
		case ERR_ITEM_VBAT_DIFF_OVER:
			break;
		case ERR_ITEM_UI_SOC_SHUTDOWN:
			oplus_chg_track_upload_vbatt_diff_over_info(track);
			oplus_chg_track_upload_uisoc_keep_1_t_info(track);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static int oplus_chg_track_subscribe_err_topic(struct oplus_chg_track *track)
{
	track->err_subs =
		oplus_mms_subscribe(track->monitor->err_topic, track,
				    oplus_chg_track_err_subs_callback, "track");
	if (IS_ERR_OR_NULL(track->err_subs)) {
		chg_err("subscribe error topic error, rc=%ld\n",
			PTR_ERR(track->err_subs));
		return PTR_ERR(track->err_subs);
	}

	return 0;
}

static int oplus_chg_track_debugfs_init(struct oplus_chg_track *track_dev)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_general;
	struct dentry *debugfs_chg_slow;

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

	debugfs_create_u8("debug_soc", 0644, debugfs_general,
			  &(track_dev->track_status.debug_soc));
	debugfs_create_u8("debug_uisoc", 0644, debugfs_general,
			  &(track_dev->track_status.debug_uisoc));
	debugfs_create_u8("debug_fast_prop_status", 0644, debugfs_general,
			  &(track_dev->track_status.debug_fast_prop_status));
	debugfs_create_u8(
		"debug_normal_charging_state", 0644, debugfs_general,
		&(track_dev->track_status.debug_normal_charging_state));
	debugfs_create_u8("debug_normal_prop_status", 0644, debugfs_general,
			  &(track_dev->track_status.debug_normal_prop_status));
	debugfs_create_u8("debug_no_charging", 0644, debugfs_general,
			  &(track_dev->track_status.debug_no_charging));
	debugfs_create_u8("debug_slow_charging_force", 0644, debugfs_chg_slow,
			  &(track_dev->track_status.debug_slow_charging_force));
	debugfs_create_u8(
		"debug_slow_charging_reason", 0644, debugfs_chg_slow,
		&(track_dev->track_status.debug_slow_charging_reason));
	debugfs_create_u32("debug_fast_chg_break_t_thd", 0644, debugfs_general,
			   &(track_dev->track_cfg.fast_chg_break_t_thd));
	debugfs_create_u32("debug_general_chg_break_t_thd", 0644,
			   debugfs_general,
			   &(track_dev->track_cfg.general_chg_break_t_thd));
	debugfs_create_u32("debug_wls_chg_break_t_thd", 0644, debugfs_general,
			   &(track_dev->track_cfg.wls_chg_break_t_thd));
	debugfs_create_u32("debug_chg_notify_flag", 0644, debugfs_general,
			   &(track_dev->track_status.debug_chg_notify_flag));
	debugfs_create_u32("debug_chg_notify_code", 0644, debugfs_general,
			   &(track_dev->track_status.debug_chg_notify_code));

	return ret;
}

int oplus_chg_track_driver_init(struct oplus_monitor *monitor)
{
	struct oplus_chg_track *track_dev;
	int rc;

	if (!monitor) {
		chg_err("monitor is NULL\n");
		return -ENODEV;
	}

	track_dev = devm_kzalloc(monitor->dev, sizeof(struct oplus_chg_track),
				 GFP_KERNEL);
	if (track_dev == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	monitor->track = track_dev;
	track_dev->monitor = monitor;

	track_dev->dev = monitor->dev;

	rc = oplus_chg_track_debugfs_init(track_dev);
	if (rc < 0) {
		rc = -ENOENT;
		chg_err("oplus chg track debugfs init fail, rc=%d\n", rc);
		goto debugfs_create_fail;
	}

	rc = oplus_chg_track_parse_dt(track_dev);
	if (rc < 0) {
		chg_err("oplus chg track parse dts error, rc=%d\n", rc);
		goto parse_dt_err;
	}

	oplus_chg_track_init(track_dev);
	rc = oplus_chg_track_thread_init(track_dev);
	if (rc < 0) {
		chg_err("oplus chg track mod init error, rc=%d\n", rc);
		goto track_kthread_init_err;
	}

	rc = oplus_chg_track_subscribe_err_topic(track_dev);
	if (rc < 0)
		goto subscribe_err_topic_err;

	track_dev->trigger_upload_wq =
		create_workqueue("oplus_chg_trigger_upload_wq");

	INIT_DELAYED_WORK(&track_dev->upload_info_dwork,
			  oplus_chg_track_upload_info_dwork);
	g_track_chip = track_dev;
	chg_info("probe done\n");

	return 0;

subscribe_err_topic_err:
track_kthread_init_err:
parse_dt_err:
	if (track_debugfs_root)
		debugfs_remove_recursive(track_debugfs_root);
debugfs_create_fail:
	devm_kfree(monitor->dev, track_dev);
	monitor->track = NULL;
	return rc;
}

int oplus_chg_track_driver_exit(struct oplus_monitor *monitor)
{
	struct oplus_chg_track *track_dev;

	if (!monitor) {
		chg_err("monitor is NULL\n");
		return -ENODEV;
	}
	track_dev = monitor->track;
	if (!track_dev) {
		chg_err("track_dev is NULL\n");
		return 0;
	}

	if (!IS_ERR_OR_NULL(track_dev->err_subs))
		oplus_mms_unsubscribe(track_dev->err_subs);

	if (track_debugfs_root)
		debugfs_remove_recursive(track_debugfs_root);
	devm_kfree(monitor->dev, track_dev);
	return 0;
}
