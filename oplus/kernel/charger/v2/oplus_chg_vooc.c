#define pr_fmt(fmt) "[VOOC]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/power_supply.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/sched/clock.h>
#include <linux/proc_fs.h>
#include <linux/firmware.h>

#include <oplus_chg.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_ic.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_monitor.h>
#include <oplus_chg_vooc.h>

#define VOOC_BAT_VOLT_REGION	4
#define VOOC_SOC_RANGE_NUM	3
#define VOOC_TEMP_RANGE_NUM	5
#define VOOC_FW_4BIT		4
#define VOOC_FW_7BIT		7
#define VOOC_DEF_REPLY_DATA	0x2
#define VOOC_BCC_STOP_CURR_NUM	6

#define BTB_TEMP_OVER_MAX_INPUT_CUR	1000
#define OPLUS_VOOC_BCC_UPDATE_TIME	500
#define OPLUS_VOOC_BCC_UPDATE_INTERVAL	\
	round_jiffies_relative(msecs_to_jiffies(OPLUS_VOOC_BCC_UPDATE_TIME))

struct oplus_vooc_spec_config {
	int32_t vooc_over_low_temp;
	int32_t vooc_low_temp;
	int32_t vooc_little_cold_temp;
	int32_t vooc_cool_temp;
	int32_t vooc_little_cool_temp;
	int32_t vooc_normal_low_temp;
	int32_t vooc_normal_high_temp;
	int32_t vooc_high_temp;
	int32_t vooc_over_high_temp;
	uint32_t vooc_low_soc;
	uint32_t vooc_high_soc;
	uint32_t vooc_warm_vol_thr;
	uint32_t vooc_warm_soc_thr;
	uint32_t vooc_bad_volt[VOOC_BAT_VOLT_REGION];
	uint32_t vooc_bad_volt_suspend[VOOC_BAT_VOLT_REGION];
	uint32_t vooc_soc_range[VOOC_SOC_RANGE_NUM];
	uint32_t vooc_temp_range[VOOC_TEMP_RANGE_NUM];
	uint32_t bcc_stop_curr_0_to_50[VOOC_BCC_STOP_CURR_NUM];
	uint32_t bcc_stop_curr_51_to_75[VOOC_BCC_STOP_CURR_NUM];
	uint32_t bcc_stop_curr_76_to_85[VOOC_BCC_STOP_CURR_NUM];
	uint32_t bcc_stop_curr_86_to_90[VOOC_BCC_STOP_CURR_NUM];
} __attribute__((packed));

struct oplus_vooc_config {
	uint8_t data_width;
	uint8_t max_curr_level;
	uint8_t support_vooc_by_normal_charger_path;
	uint8_t svooc_support;
	uint8_t vooc_bad_volt_check_support;
	uint32_t max_power_w;
	uint32_t voocphy_support;
	uint32_t vooc_project;
	uint32_t vooc_version;
	uint8_t *strategy_name;
	uint8_t *strategy_data;
	uint32_t strategy_data_size;
	int32_t *abnormal_adapter_cur_array;
} __attribute__((packed));

struct oplus_chg_vooc {
	struct device *dev;
	struct oplus_chg_ic_dev *vooc_ic;
	struct oplus_mms *vooc_topic;
	struct oplus_mms *wired_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *err_topic;
	struct mms_subscribe *wired_subs;
	struct mms_subscribe *comm_subs;
	struct mms_subscribe *gauge_subs;

	struct oplus_vooc_spec_config spec;
	struct oplus_vooc_config config;

	struct work_struct fastchg_work;
	struct work_struct plugin_work;
	struct work_struct chg_type_change_work;
	struct work_struct temp_region_update_work;
	struct work_struct gauge_update_work;
	struct work_struct err_handler_work;
	struct work_struct abnormal_adapter_check_work;
	struct work_struct comm_charge_disable_work;
	struct delayed_work vooc_init_work;
	struct delayed_work vooc_switch_check_work;
	struct delayed_work check_charger_out_work;
	struct delayed_work fw_update_work;
	struct delayed_work fw_update_work_fix;
	struct delayed_work bcc_get_max_min_curr;

	struct power_supply *usb_psy;
	struct power_supply *batt_psy;

	struct votable *vooc_curr_votable;
	struct votable *vooc_disable_votable;
	struct votable *vooc_not_allow_votable;
	struct votable *wired_charging_disable_votable;
	struct votable *wired_charge_suspend_votable;
	struct votable *pd_svooc_votable;
	struct votable *wired_icl_votable;
	struct votable *common_chg_suspend_votable;
	struct votable *vooc_chg_auto_mode_votable;

	struct work_struct vooc_watchdog_work;
	struct timer_list watchdog;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock vooc_wake_lock;
#else
	struct wakeup_source *vooc_ws;
#endif

	struct oplus_chg_strategy *general_strategy;

	int switch_retry_count;
	int adapter_id;
	unsigned int sid;
	int batt_volt;
	int icharging;
	int temperature;
	int soc;
	int curr_level;
	int temp_over_count;
	int cool_down;
	bool fw_update_flag;

	bool wired_online;
	bool fastchg_disable;
	bool fastchg_allow;
	bool fastchg_started;
	bool fastchg_dummy_started;
	bool fastchg_ing;
	bool vooc_online;
	bool vooc_online_keep;
	bool mcu_update_ing_fix;
	bool pd_svooc;
	bool fastchg_force_exit;
	bool vooc_chg_bynormal_path;
	bool batt_hmac;
	bool batt_auth;

	bool wired_present;
	int cc_detect;
	int typec_state;
	bool is_abnormal_adapter;
	bool pre_is_abnormal_adapter;
	bool support_abnormal_adapter;
	bool mcu_vote_detach;
	bool icon_debounce;
	int abnormal_allowed_current_max;
	int abnormal_adapter_dis_cnt;
	int abnormal_adapter_cur_arraycnt;
	unsigned long long svooc_detect_time;
	unsigned long long svooc_detach_time;
	enum oplus_fast_chg_status fast_chg_status;
	enum oplus_temp_region bat_temp_region;

	int efficient_vooc_little_cold_temp;
	int efficient_vooc_cool_temp;
	int efficient_vooc_little_cool_temp;
	int efficient_vooc_normal_low_temp;
	int efficient_vooc_normal_high_temp;
	int fastchg_batt_temp_status;
	int vooc_temp_cur_range;
	int vooc_strategy_change_count;

	bool smart_chg_bcc_support;
	int bcc_target_vbat;
	int bcc_curve_max_current;
	int bcc_curve_min_current;
	int bcc_exit_curve;
	struct batt_bcc_curves svooc_batt_curve[1];
	int bcc_max_curr;
	int bcc_min_curr;
	int bcc_exit_curr;
	bool bcc_wake_up_done;
	bool bcc_choose_curve_done;
	int bcc_curve_idx;
	int bcc_true_idx;

	int bcc_soc_range;
	int bcc_temp_range;
	int bcc_curr_count;
};

struct oplus_adapter_struct {
	unsigned char id_min;
	unsigned char id_max;
	unsigned int power_vooc;
	unsigned int power_svooc;
	enum oplus_adapter_type adapter_type;
	enum oplus_adapter_chg_type adapter_chg_type;
};

struct current_level {
	int level;
	int curr_ma;
};

#define VOOC_NOTIFY_FAST_PRESENT		0x52
#define VOOC_NOTIFY_FAST_ABSENT			0x54
#define VOOC_NOTIFY_ALLOW_READING_IIC		0x58
#define VOOC_NOTIFY_NORMAL_TEMP_FULL		0x5a
#define VOOC_NOTIFY_LOW_TEMP_FULL		0x53
#define VOOC_NOTIFY_DATA_UNKNOWN		0x55
#define VOOC_NOTIFY_FIRMWARE_UPDATE		0x56
#define VOOC_NOTIFY_BAD_CONNECTED		0x59
#define VOOC_NOTIFY_TEMP_OVER			0x5c
#define VOOC_NOTIFY_ADAPTER_FW_UPDATE		0x5b
#define VOOC_NOTIFY_BTB_TEMP_OVER		0x5d
#define VOOC_NOTIFY_ADAPTER_MODEL_FACTORY	0x5e

static const char *const strategy_soc[] = {
	[BCC_BATT_SOC_0_TO_50] = "strategy_soc_0_to_50",
	[BCC_BATT_SOC_50_TO_75] = "strategy_soc_50_to_75",
	[BCC_BATT_SOC_75_TO_85] = "strategy_soc_75_to_85",
	[BCC_BATT_SOC_85_TO_90] = "strategy_soc_85_to_90",
};

static const char * const strategy_temp[] = {
	[BATT_BCC_CURVE_TEMP_LITTLE_COLD]	= "strategy_temp_little_cold",/*0-5*/
	[BATT_BCC_CURVE_TEMP_COOL]		= "strategy_temp_cool", /*5-12*/
	[BATT_BCC_CURVE_TEMP_LITTLE_COOL]	= "strategy_temp_little_cool", /*12-20*/
	[BATT_BCC_CURVE_TEMP_NORMAL_LOW]	= "strategy_temp_normal_low", /*20-35*/
	[BATT_BCC_CURVE_TEMP_NORMAL_HIGH] 	= "strategy_temp_normal_high", /*35-44*/
	[BATT_BCC_CURVE_TEMP_WARM] 		= "strategy_temp_warm", /*44-53*/
};

/*svooc curv*/
/*0~50*/
struct batt_bcc_curves bcc_curves_soc0_2_50[BATT_BCC_ROW_MAX] = { 0 };
/*50~75*/
struct batt_bcc_curves bcc_curves_soc50_2_75[BATT_BCC_ROW_MAX] = { 0 };
/*75~85*/
struct batt_bcc_curves bcc_curves_soc75_2_85[BATT_BCC_ROW_MAX] = { 0 };
/*85~90*/
struct batt_bcc_curves bcc_curves_soc85_2_90[BATT_BCC_ROW_MAX] = { 0 };

struct batt_bcc_curves svooc_curves_target_soc_curve[BATT_BCC_ROW_MAX] = { 0 };
struct batt_bcc_curves svooc_curves_target_curve[1] = { 0 };

static struct oplus_vooc_spec_config default_spec_config = {
	.vooc_over_low_temp = -5,
	.vooc_low_temp = 0,
	.vooc_little_cold_temp = 50,
	.vooc_cool_temp = 120,
	.vooc_little_cool_temp = 160,
	.vooc_normal_low_temp = 250,
	.vooc_normal_high_temp = -EINVAL,
	.vooc_high_temp = 430,
	.vooc_over_high_temp = 440,
	.vooc_low_soc = 1,
	.vooc_high_soc = 90,
	.vooc_warm_vol_thr = -EINVAL,
	.vooc_warm_soc_thr = -EINVAL,
};
__maybe_unused static struct oplus_vooc_config default_config = {};

static int oplus_svooc_curr_table[CURR_LIMIT_MAX - 1] = { 2500, 2000, 3000,
							  4000, 5000, 6500 };
static int oplus_vooc_curr_table[CURR_LIMIT_MAX - 1] = { 3600, 2500, 3000,
							 4000, 5000, 6000 };
static int oplus_vooc_7bit_curr_table[CURR_LIMIT_7BIT_MAX - 1] = {
	1000, 1500,  2000,  2500,  3000,  3500,	 4000,	4500, 5000,
	5500, 6000,  6300,  6500,  7000,  7500,	 8000,	8500, 9000,
	9500, 10000, 10500, 11000, 11500, 12000, 12500,
};

struct current_level svooc_2_0_curr_table[] = {
	{ 1, 1000 },   { 2, 1500 },   { 3, 2000 },   { 4, 2500 },   { 5, 3000 },   { 6, 3500 },	 { 7, 4000 },
	{ 8, 4500 },   { 9, 5000 },   { 10, 5500 },  { 11, 6000 },  { 12, 6300 },  { 13, 6500 }, { 14, 7000 },
	{ 15, 7500 },  { 15, 7300 },  { 16, 8000 },  { 17, 8500 },  { 18, 9000 },  { 19, 9500 }, { 20, 10000 },
	{ 21, 10500 }, { 22, 11000 }, { 23, 11500 }, { 24, 12000 }, { 25, 12500 },
};

struct current_level svooc_1_0_curr_table[] = {
	{ 1, 2000 },   { 2, 2000 },   { 3, 2000 },   { 4, 3000 },  { 5, 4000 },
	{ 6, 5000 },   { 7, 6000 },   { 8, 7000 },   { 9, 8000 },  { 10, 9000 },
	{ 11, 10000 }, { 12, 11000 }, { 13, 12000 }, { 14, 12600 }
};

struct current_level old_svooc_1_0_curr_table[] = { /*20638 RT5125 50W*/
	{ 1, 2000 },  { 2, 3000 },  { 3, 4000 },   { 4, 5000 },
	{ 5, 6000 },  { 6, 7000 },  { 7, 8000 },   { 8, 9000 },
	{ 9, 10000 }, { 10, 1200 }, { 11, 12000 }, { 12, 12600 }
};

static struct oplus_adapter_struct adapter_id_table[] = {
	{ 0x0,  0x0,   0,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x1,  0x1,  20,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x11, 0x12, 30,  50, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x13, 0x13, 20,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x14, 0x14, 30,  65, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x15, 0x16, 20,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x17, 0x19, 30,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x1a, 0x1b, 15,  33, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x1c, 0x1e, 22,  44, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x21, 0x21, 30,  50, ADAPTER_TYPE_CAR, CHARGER_TYPE_SVOOC },
	{ 0x23, 0x23, 30,  50, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x24, 0x27, 30,  55, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x28, 0x28, 30,  65, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x29, 0x29, 30,   0, ADAPTER_TYPE_CAR, CHARGER_TYPE_VOOC },
	{ 0x2a, 0x2a, 30,  65, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x2b, 0x2b, 30,  66, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x2c, 0x2e, 30,  67, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x31, 0x31, 30,  50, ADAPTER_TYPE_PB,  CHARGER_TYPE_SVOOC },
	{ 0x32, 0x32, 30, 120, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x33, 0x33, 30,  50, ADAPTER_TYPE_PB,  CHARGER_TYPE_SVOOC },
	{ 0x34, 0x34, 20,  20, ADAPTER_TYPE_PB,  CHARGER_TYPE_NORMAL },
	{ 0x35, 0x35, 30,  65, ADAPTER_TYPE_PB,  CHARGER_TYPE_SVOOC },
	{ 0x36, 0x36, 30,  66, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x37, 0x3a, 30,  88, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x3b, 0x3e, 30, 100, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x41, 0x44, 30,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x45, 0x45, 20,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x46, 0x46, 30,   0, ADAPTER_TYPE_AC,  CHARGER_TYPE_VOOC },
	{ 0x47, 0x48, 30, 120, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x49, 0x4a, 15,  33, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x4b, 0x4e, 30,  80, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x51, 0x51, 30, 125, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x61, 0x61, 15,  33, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x62, 0x62, 30,  50, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x63, 0x63, 30,  65, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x64, 0x64, 30,  66, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x65, 0x65, 30,  80, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x66, 0x66, 30,  65, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x67, 0x68, 30, 125, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x69, 0x6a, 30, 100, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x6b, 0x6b, 30, 120, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x6c, 0x6d, 30,  67, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
	{ 0x6e, 0x6e, 30,  65, ADAPTER_TYPE_AC,  CHARGER_TYPE_SVOOC },
};

static void oplus_vooc_reset_temp_range(struct oplus_chg_vooc *chip);
static int oplus_vooc_choose_bcc_fastchg_curve(struct oplus_chg_vooc *chip);
static int oplus_chg_bcc_get_stop_curr(struct oplus_chg_vooc *chip);
static bool oplus_vooc_get_bcc_support(struct oplus_chg_vooc *chip);
static void oplus_vooc_bcc_parms_init(struct oplus_chg_vooc *chip);
static int oplus_vooc_get_bcc_max_curr(struct oplus_mms *topic,
				       union mms_msg_data *data);
static int oplus_vooc_get_bcc_min_curr(struct oplus_mms *topic,
				       union mms_msg_data *data);
static int oplus_vooc_get_bcc_stop_curr(struct oplus_mms *topic,
					union mms_msg_data *data);
static int oplus_vooc_get_bcc_temp_range(struct oplus_mms *topic,
					 union mms_msg_data *data);
static int oplus_vooc_get_svooc_type(struct oplus_mms *topic,
				     union mms_msg_data *data);
void oplus_vooc_cancel_bcc_update_work_sync(struct oplus_chg_vooc *chip);
bool oplus_vooc_wake_bcc_update_work(struct oplus_chg_vooc *chip);
static void oplus_vooc_bcc_get_curve(struct oplus_chg_vooc *chip);
static void oplus_vooc_bcc_get_curr_func(struct work_struct *work);
int oplus_mcu_bcc_svooc_batt_curves(struct oplus_chg_vooc *chip,
				    struct device_node *node);
int oplus_mcu_bcc_stop_curr_dt(struct oplus_chg_vooc *chip,
			       struct device_node *node);
static int oplus_vooc_afi_update_condition(struct oplus_mms *topic,
					   union mms_msg_data *data);

__maybe_unused static bool is_err_topic_available(struct oplus_chg_vooc *chip)
{
	if (!chip->err_topic)
		chip->err_topic = oplus_mms_get_by_name("error");
	return !!chip->err_topic;
}

static int find_current_to_level(int val, struct current_level *table, int len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		if (table[i].level == val)
			return table[i].curr_ma;
	}
	return 0;
}

static int find_level_to_current(int val, struct current_level *table, int len)
{
	int i = 0;
	bool find_out_flag = false;

	for (i = 0; i < len; i++) {
		if (table[i].curr_ma > val) {
			find_out_flag = true;
			break;
		}
		find_out_flag = false;
	}
	if (find_out_flag && i != 0)
		return table[i - 1].level;

	return 0;
}

static unsigned int oplus_get_adapter_sid(struct oplus_chg_vooc *chip,
					  unsigned char id)
{
	struct oplus_adapter_struct *adapter_info;
	enum oplus_adapter_chg_type adapter_chg_type;
	int i;
	unsigned int sid, power;

	for (i = 0; i < ARRAY_SIZE(adapter_id_table); i++) {
		adapter_info = &adapter_id_table[i];
		if (adapter_info->id_min > adapter_info->id_max)
			continue;
		if (id >= adapter_info->id_min && id <= adapter_info->id_max) {
			if (!chip->config.svooc_support &&
			    adapter_info->adapter_chg_type == CHARGER_TYPE_SVOOC) {
				adapter_chg_type = CHARGER_TYPE_VOOC;
				power = adapter_info->power_vooc;
			} else {
				adapter_chg_type = adapter_info->adapter_chg_type;
				switch (adapter_chg_type) {
				case CHARGER_TYPE_UNKNOWN:
				case CHARGER_TYPE_NORMAL:
					power = 0;
					break;
				case CHARGER_TYPE_VOOC:
					power = adapter_info->power_vooc;
					break;
				case CHARGER_TYPE_SVOOC:
					power = adapter_info->power_svooc;
					break;
				}
			}
			if (power > chip->config.max_power_w)
				power = chip->config.max_power_w;
			sid = adapter_info_to_sid(id, power,
						  adapter_info->adapter_type,
						  adapter_chg_type);
			chg_info("sid = 0x%08x\n", sid);
			return sid;
		}
	}

	chg_err("unsupported adapter ID\n");
	return 0;
}

__maybe_unused static bool is_usb_psy_available(struct oplus_chg_vooc *chip)
{
	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	return !!chip->usb_psy;
}

__maybe_unused static bool is_batt_psy_available(struct oplus_chg_vooc *chip)
{
	if (!chip->batt_psy)
		chip->batt_psy = power_supply_get_by_name("battery");
	return !!chip->batt_psy;
}

__maybe_unused static bool
is_wired_charging_disable_votable_available(struct oplus_chg_vooc *chip)
{
	if (!chip->wired_charging_disable_votable)
		chip->wired_charging_disable_votable =
			find_votable("WIRED_CHARGING_DISABLE");
	return !!chip->wired_charging_disable_votable;
}

__maybe_unused static bool
is_wired_charge_suspend_votable_available(struct oplus_chg_vooc *chip)
{
	if (!chip->wired_charge_suspend_votable)
		chip->wired_charge_suspend_votable =
			find_votable("WIRED_CHARGE_SUSPEND");
	return !!chip->wired_charge_suspend_votable;
}

static void oplus_vooc_set_vooc_started(struct oplus_chg_vooc *chip,
					bool started)
{
	struct mms_msg *msg;
	int rc;

	if (chip->fastchg_started == started)
		return;
	chip->fastchg_started = started;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  VOOC_ITEM_VOOC_STARTED);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish vooc_started msg error, rc=%d\n", rc);
		kfree(msg);
	}

	if (chip->fastchg_started)
		chip->switch_retry_count = 0;

	chg_info("fastchg_started = %s\n", started ? "true" : "false");
}

static void oplus_vooc_set_online(struct oplus_chg_vooc *chip, bool online)
{
	struct mms_msg *msg;
	int rc;

	if (chip->vooc_online == online)
		return;
	chip->vooc_online = online;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  VOOC_ITEM_ONLINE);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish online msg error, rc=%d\n", rc);
		kfree(msg);
	}

	chg_info("vooc_online = %s\n", online ? "true" : "false");
}

static void oplus_vooc_set_online_keep(struct oplus_chg_vooc *chip, bool keep)
{
	struct mms_msg *msg;
	int rc;

	if (chip->vooc_online_keep == keep)
		return;
	chip->vooc_online_keep = keep;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  VOOC_ITEM_ONLINE_KEEP);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish online keep msg error, rc=%d\n", rc);
		kfree(msg);
	}

	chg_info("vooc_online_keep = %s\n", keep ? "true" : "false");
}

static void oplus_vooc_set_vooc_charging(struct oplus_chg_vooc *chip,
					 bool charging)
{
	struct mms_msg *msg;
	int rc;

	if (chip->fastchg_ing == charging)
		return;
	chip->fastchg_ing = charging;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  VOOC_ITEM_VOOC_CHARGING);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish vooc_charging msg error, rc=%d\n", rc);
		kfree(msg);
	}

	chg_info("fastchg_ing = %s\n", charging ? "true" : "false");
}

static void oplus_chg_clear_abnormal_adapter_var(struct oplus_chg_vooc *chip)
{
	chip->icon_debounce = false;
	chip->is_abnormal_adapter = false;
	chip->abnormal_adapter_dis_cnt = 0;
}

static void oplus_set_fast_status(struct oplus_chg_vooc *chip,
				  enum oplus_fast_chg_status status)
{
	if (chip->fast_chg_status != status) {
		chip->fast_chg_status = status;
		chg_info("fast status = %d\n", chip->fast_chg_status);
	}
}

static void oplus_select_abnormal_max_cur(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;
	int index = chip->abnormal_adapter_dis_cnt;

	if (chip->support_abnormal_adapter && index > 0) {
		if (index < chip->abnormal_adapter_cur_arraycnt) {
			chip->abnormal_allowed_current_max =
				config->abnormal_adapter_cur_array[index];
		} else {
			chip->abnormal_allowed_current_max =
				config->abnormal_adapter_cur_array[0];
		}
	}

	chg_info("abnormal info [%d %d %d %d %d]\n",
		chip->pd_svooc,
		chip->support_abnormal_adapter,
		chip->abnormal_adapter_dis_cnt,
		chip->abnormal_adapter_cur_arraycnt,
		chip->abnormal_allowed_current_max);
}

static void oplus_vooc_set_sid(struct oplus_chg_vooc *chip, unsigned int sid)
{
	struct mms_msg *msg;
	int rc;

	if (chip->sid == sid)
		return;
	chip->sid = sid;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  VOOC_ITEM_SID);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish sid msg error, rc=%d\n", rc);
		kfree(msg);
	}

	chg_info("sid = 0x%08x\n", sid);
}

static void oplus_vooc_chg_bynormal_path(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;
	struct mms_msg *msg;
	int rc;

	if (config->support_vooc_by_normal_charger_path &&
	    sid_to_adapter_chg_type(chip->sid) == CHARGER_TYPE_VOOC)
		chip->vooc_chg_bynormal_path = true;
	else
		chip->vooc_chg_bynormal_path = false;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  VOOC_ITEM_VOOC_BY_NORMAL_PATH);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return;
	}
	rc = oplus_mms_publish_msg(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish sid msg error, rc=%d\n", rc);
		kfree(msg);
	}
	chg_info("vooc_chg_bynormal_path [%s]\n",
		 chip->vooc_chg_bynormal_path == true ? "true" : "false");
}

#define VOOC_BAD_VOLT_INDEX 2
static bool oplus_vooc_batt_volt_is_good(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	struct oplus_vooc_config *config = &chip->config;
	bool charger_suspend = false;
	int bad_vol = 0;
	int index = 0;
	union mms_msg_data data = { 0 };
	enum oplus_temp_region temp_region;
	int rc;

	if (chip->fastchg_started)
		return true;
	if (!config->vooc_bad_volt_check_support)
		return true;

	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_TEMP_REGION,
				     &data, false);
	if (rc < 0) {
		chg_err("can't get remp region, rc=%d\n", rc);
		return false;
	}
	temp_region = data.intval;

	if (is_wired_charge_suspend_votable_available(chip) &&
	    get_effective_result(chip->wired_charge_suspend_votable))
		charger_suspend = true;

	switch (temp_region) {
	case TEMP_REGION_COLD:
	case TEMP_REGION_LITTLE_COLD:
	case TEMP_REGION_COOL:
		index = TEMP_REGION_COOL - VOOC_BAD_VOLT_INDEX;
		break;
	case TEMP_REGION_LITTLE_COOL:
		index = TEMP_REGION_LITTLE_COOL - VOOC_BAD_VOLT_INDEX;
		break;
	case TEMP_REGION_PRE_NORMAL:
		index = TEMP_REGION_PRE_NORMAL - VOOC_BAD_VOLT_INDEX;
		break;
	case TEMP_REGION_NORMAL:
	case TEMP_REGION_WARM:
	case TEMP_REGION_HOT:
		index = TEMP_REGION_NORMAL - VOOC_BAD_VOLT_INDEX;
		break;
	default:
		chg_err("unknown temp region, %d\n", temp_region);
		return false;
	}

	if (charger_suspend)
		bad_vol = spec->vooc_bad_volt_suspend[index];
	else
		bad_vol = spec->vooc_bad_volt[index];

	if (chip->batt_volt < bad_vol)
		return false;
	else
		return true;
}

static void oplus_vooc_bad_volt_check(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;

	/*
	 * For the ASIC solution, when the battery voltage is too low, the ASIC
	 * will not work, and fast charging needs to be disabled.
	 */
	if (config->vooc_bad_volt_check_support) {
		if (oplus_vooc_batt_volt_is_good(chip)) {
			vote(chip->vooc_not_allow_votable, BAD_VOLT_VOTER,
			     false, 0, false);
		} else {
			vote(chip->vooc_not_allow_votable, BAD_VOLT_VOTER, true,
			     1, false);
			chg_info("bad battery voltage, disable fast charge\n");
		}
	}
}

static bool oplus_fastchg_is_allow_retry(struct oplus_chg_vooc *chip)
{
	union mms_msg_data data = { 0 };
	int chg_type = OPLUS_CHG_USB_TYPE_UNKNOWN;

	if (!chip->wired_online ||
	    !is_client_vote_enabled(chip->vooc_disable_votable, TIMEOUT_VOTER))
		return false;

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CHG_TYPE,
				&data, false);
	chg_type = data.intval;

	switch (chg_type) {
	case OPLUS_CHG_USB_TYPE_DCP:
	case OPLUS_CHG_USB_TYPE_ACA:
	case OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID:
	case OPLUS_CHG_USB_TYPE_SVOOC:
	case OPLUS_CHG_USB_TYPE_VOOC:
		break;
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		if (!chip->pd_svooc)
			return false;
		break;
	default:
		chg_info("chg_type=%d, no support to fastchg retry \n",
			 chg_type);
		return false;
	}
	return true;
}

static void oplus_fastchg_check_retry(struct oplus_chg_vooc *chip)
{
	union mms_msg_data data = { 0 };
	enum oplus_temp_region temp_region = TEMP_REGION_MAX;

	if (!oplus_fastchg_is_allow_retry(chip))
		return;

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_TEMP_REGION,
				&data, false);
	temp_region = data.intval;

	if (chip->bat_temp_region == TEMP_REGION_MAX) {
		chip->bat_temp_region = temp_region;
		return;
	}

	if (temp_region == chip->bat_temp_region) {
		chg_info("no change in temperature range \n");
		return;
	}

	if (chip->bat_temp_region <= TEMP_REGION_LITTLE_COLD &&
	    temp_region >= TEMP_REGION_COOL &&
	    temp_region <= TEMP_REGION_NORMAL) {
		chg_info("rise to cool retry fastchg\n");
		oplus_set_fast_status(chip, CHARGER_STATUS_TIMEOUT_RETRY);
		vote(chip->vooc_disable_votable, TIMEOUT_VOTER, false, 0,
		     false);
	}

	if (chip->bat_temp_region >= TEMP_REGION_WARM &&
	    temp_region <= TEMP_REGION_NORMAL &&
	    temp_region >= TEMP_REGION_COOL) {
		chg_info("drop to normal retry fastchg\n");
		oplus_set_fast_status(chip, CHARGER_STATUS_TIMEOUT_RETRY);
		vote(chip->vooc_disable_votable, TIMEOUT_VOTER, false, 0,
		     false);
	}
	chg_info("pre_temp_region[%d] cur_temp_region[%d]\n",
		 chip->bat_temp_region, temp_region);

	chip->bat_temp_region = temp_region;
}

static bool oplus_vooc_is_allow_fast_chg(struct oplus_chg_vooc *chip);
static void
oplus_vooc_fastchg_allow_or_enable_check(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	struct oplus_vooc_config *config = &chip->config;

	if (chip->fastchg_started)
		return;

	if (chip->fastchg_allow)
		goto enable_check;

	if (is_client_vote_enabled(chip->vooc_not_allow_votable,
				   BATT_TEMP_VOTER)) {
		if (chip->temperature > spec->vooc_low_temp &&
		    chip->temperature < spec->vooc_high_temp) {
			chg_info("allow fastchg, temp=%d\n", chip->temperature);
			vote(chip->vooc_not_allow_votable, BATT_TEMP_VOTER,
			     false, 0, false);
		}
	}
	if (is_client_vote_enabled(chip->vooc_not_allow_votable,
				   BATT_SOC_VOTER)) {
		if (chip->soc >= spec->vooc_low_soc &&
		    chip->soc <= spec->vooc_high_soc) {
			chg_info("allow fastchg, soc=%d\n", chip->soc);
			vote(chip->vooc_not_allow_votable, BATT_SOC_VOTER,
			     false, 0, false);
		}
	}

	if (config->vooc_bad_volt_check_support &&
	    is_client_vote_enabled(chip->vooc_not_allow_votable,
				   BAD_VOLT_VOTER)) {
		if (oplus_vooc_batt_volt_is_good(chip)) {
			vote(chip->vooc_not_allow_votable, BAD_VOLT_VOTER,
			     false, 0, false);
		}
	}

	if (spec->vooc_normal_high_temp == -EINVAL)
		goto skip_vooc_warm_check;

	if (is_client_vote_enabled(chip->vooc_not_allow_votable,
				   WARM_SOC_VOTER)) {
		if (chip->soc < spec->vooc_warm_soc_thr ||
		    chip->temperature <
			    (chip->efficient_vooc_normal_high_temp)) {
			vote(chip->vooc_not_allow_votable, WARM_SOC_VOTER,
			     false, 0, false);
		}
	}
	if (is_client_vote_enabled(chip->vooc_not_allow_votable,
				   WARM_VOL_VOTER)) {
		if (chip->batt_volt < (spec->vooc_warm_vol_thr) ||
		    chip->temperature <
			    (chip->efficient_vooc_normal_high_temp)) {
			vote(chip->vooc_not_allow_votable, WARM_VOL_VOTER,
			     false, 0, false);
		}
	}

skip_vooc_warm_check:
enable_check:
	if (!chip->fastchg_disable)
		return;
	if (is_client_vote_enabled(chip->vooc_disable_votable,
				   WARM_FULL_VOTER)) {
		if (chip->temperature <
		    (chip->efficient_vooc_normal_high_temp - BATT_TEMP_HYST)) {
			vote(chip->vooc_disable_votable, WARM_FULL_VOTER, false,
			     0, false);
		}
	}

	if (is_client_vote_enabled(chip->vooc_disable_votable,
				   BATT_TEMP_VOTER)) {
		if (chip->temperature > spec->vooc_low_temp &&
		    chip->temperature < spec->vooc_high_temp)
			vote(chip->vooc_disable_votable, BATT_TEMP_VOTER, false,
			     0, false);
	}

	if (is_client_vote_enabled(chip->vooc_disable_votable,
	    SWITCH_RANGE_VOTER) && oplus_vooc_is_allow_fast_chg(chip)) {
		vote(chip->vooc_disable_votable, SWITCH_RANGE_VOTER, false, 0,
		     false);
	}

	oplus_fastchg_check_retry(chip);
}

static bool oplus_vooc_is_allow_fast_chg(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	bool fastchg_to_warm_full = false;
	int warmfull_fastchg_temp = 0;
	union mms_msg_data data = { 0 };

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data, true);
	chip->soc = data.intval;

	if (chip->temperature < spec->vooc_low_temp ||
	    chip->temperature > spec->vooc_high_temp) {
		vote(chip->vooc_not_allow_votable, BATT_TEMP_VOTER, true, 1,
		     false);
	} else {
		vote(chip->vooc_not_allow_votable, BATT_TEMP_VOTER, false, 0,
		     false);
	}
	if (chip->soc < spec->vooc_low_soc || chip->soc > spec->vooc_high_soc) {
		vote(chip->vooc_not_allow_votable, BATT_SOC_VOTER, true, 1,
		     false);
	} else {
		vote(chip->vooc_not_allow_votable, BATT_SOC_VOTER, false, 0,
		     false);
	}

	oplus_vooc_bad_volt_check(chip);

	if (spec->vooc_normal_high_temp == -EINVAL)
		goto skip_vooc_warm_check;

	if (chip->temperature > chip->efficient_vooc_normal_high_temp) {
		if (chip->soc >= spec->vooc_warm_soc_thr)
			vote(chip->vooc_not_allow_votable, WARM_SOC_VOTER, true,
			     1, false);
		else
			vote(chip->vooc_not_allow_votable, WARM_SOC_VOTER,
			     false, 0, false);

		if (chip->batt_volt >= spec->vooc_warm_vol_thr)
			vote(chip->vooc_not_allow_votable, WARM_VOL_VOTER, true,
			     1, false);
		else
			vote(chip->vooc_not_allow_votable, WARM_VOL_VOTER,
			     false, 0, false);
	} else {
		vote(chip->vooc_not_allow_votable, WARM_SOC_VOTER, false, 0,
		     false);
		vote(chip->vooc_not_allow_votable, WARM_VOL_VOTER, false, 0,
		     false);
	}

	fastchg_to_warm_full = is_client_vote_enabled(
		chip->vooc_disable_votable, WARM_FULL_VOTER);
	warmfull_fastchg_temp =
		chip->efficient_vooc_normal_high_temp - VOOC_TEMP_RANGE_THD;

	if (fastchg_to_warm_full && chip->temperature > warmfull_fastchg_temp) {
		vote(chip->vooc_not_allow_votable, WARM_FULL_VOTER, true, 1,
		     false);
		chg_info(" oplus_vooc_get_fastchg_to_warm_full is true\n");
	} else {
		vote(chip->vooc_not_allow_votable, WARM_FULL_VOTER, false, 0,
		     false);
	}

skip_vooc_warm_check:
	return chip->fastchg_allow;
}

static void oplus_vooc_switch_fast_chg(struct oplus_chg_vooc *chip)
{
	switch_fast_chg(chip->vooc_ic);
	if (!is_client_vote_enabled(chip->vooc_disable_votable,
				    UPGRADE_FW_VOTER) &&
	    !oplus_vooc_asic_fw_status(chip->vooc_ic)) {
		chg_err("check fw fail, go to update fw!\n");
		switch_normal_chg(chip->vooc_ic);
		chg_err("asic didn't work, update fw!\n");
		if (chip->mcu_update_ing_fix) {
			chg_err("check asic fw work already runing, return !\n");
			return;
		}
		vote(chip->vooc_disable_votable, UPGRADE_FW_VOTER, true, 1,
		     false);
		chip->mcu_update_ing_fix = true;
		schedule_delayed_work(&chip->fw_update_work_fix,
				      round_jiffies_relative(msecs_to_jiffies(
					      FASTCHG_FW_INTERVAL_INIT)));
	}
}

static void oplus_vooc_awake_init(struct oplus_chg_vooc *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->vooc_wake_lock, WAKE_LOCK_SUSPEND,
		       "vooc_wake_lock");
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 102) &&                      \
       LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 999))
	chip->vooc_ws = wakeup_source_register("vooc_wake_lock");
#else
	chip->vooc_ws = wakeup_source_register(NULL, "vooc_wake_lock");
#endif
}

static void oplus_vooc_awake_exit(struct oplus_chg_vooc *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_destroy(&chip->vooc_wake_lock);
#else
	wakeup_source_unregister(chip->vooc_ws);
#endif
}

static void oplus_vooc_set_awake(struct oplus_chg_vooc *chip, bool awake)
{
	static bool pm_flag = false;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if (awake && !pm_flag) {
		pm_flag = true;
		wake_lock(&chip->vooc_wake_lock);
	} else if (!awake && pm_flag) {
		wake_unlock(&chip->vooc_wake_lock);
		pm_flag = false;
	}
#else
	if (!chip || !chip->vooc_ws) {
		return;
	}
	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->vooc_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->vooc_ws);
		pm_flag = false;
	}
#endif
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void oplus_vooc_watchdog(unsigned long data)
{
	struct oplus_chg_vooc *chip = (struct oplus_chg_vooc *)data;
#else
static void oplus_vooc_watchdog(struct timer_list *t)
{
	struct oplus_chg_vooc *chip = from_timer(chip, t, watchdog);
#endif

	chg_err("watchdog bark: cannot receive mcu data\n");
	schedule_work(&chip->vooc_watchdog_work);
}

static void oplus_vooc_init_watchdog_timer(struct oplus_chg_vooc *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	init_timer(&chip->watchdog);
	chip->watchdog.data = (unsigned long)chip;
	chip->watchdog.function = oplus_vooc_watchdog;
#else
	timer_setup(&chip->watchdog, oplus_vooc_watchdog, 0);
#endif
}

static void oplus_vooc_del_watchdog_timer(struct oplus_chg_vooc *chip)
{
	del_timer(&chip->watchdog);
}

static void oplus_vooc_setup_watchdog_timer(struct oplus_chg_vooc *chip,
					    unsigned int ms)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	mod_timer(&chip->watchdog, jiffies + msecs_to_jiffies(25000));
#else
	del_timer(&chip->watchdog);
	chip->watchdog.expires = jiffies + msecs_to_jiffies(ms);
	add_timer(&chip->watchdog);
#endif
}

static void oplus_vooc_watchdog_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip =
		container_of(work, struct oplus_chg_vooc, vooc_watchdog_work);

	oplus_gauge_unlock();
	vote(chip->vooc_disable_votable, FASTCHG_DUMMY_VOTER, false, 0, false);
	if (is_wired_charging_disable_votable_available(chip)) {
		vote(chip->wired_charging_disable_votable, FASTCHG_VOTER, false,
		     0, false);
	}
	if (is_wired_charge_suspend_votable_available(chip)) {
		vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER, false,
		     0, false);
	}

	oplus_vooc_set_vooc_started(chip, false);
	oplus_vooc_set_online(chip, false);
	oplus_vooc_set_online_keep(chip, false);
	switch_normal_chg(chip->vooc_ic);
	oplus_vooc_set_reset_sleep(chip->vooc_ic);
	oplus_vooc_set_vooc_charging(chip, false);
	oplus_vooc_set_sid(chip, 0);
	oplus_vooc_chg_bynormal_path(chip);
	oplus_vooc_set_awake(chip, false);
	oplus_vooc_reset_temp_range(chip);
	chip->icon_debounce = false;
}

static void oplus_vooc_switch_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_vooc *chip = container_of(dwork, struct oplus_chg_vooc,
						   vooc_switch_check_work);
	int chg_type;
	unsigned long schedule_delay = 0;
	static unsigned long fastchg_check_timeout;

	chg_info("vooc switch check\n");

	if (!chip->config.svooc_support) {
		chg_info("The SVOOC project does not allow fast charge"
			 "again after the VOOC adapter is recognized\n");
		return;
	}

	if (!chip->wired_online) {
		chip->switch_retry_count = 0;
		chg_info("wired_online is false, return\n");
		return;
	}

	if (chip->abnormal_adapter_dis_cnt > 0 &&
	    chip->support_abnormal_adapter &&
	    chip->abnormal_adapter_dis_cnt >=
		    chip->abnormal_adapter_cur_arraycnt) {
		chg_info("abnormal adapter dis cnt >= vooc_max,return \n");
		if (oplus_chg_vooc_get_switch_mode(chip->vooc_ic) !=
		    VOOC_SWITCH_MODE_NORMAL) {
			switch_normal_chg(chip->vooc_ic);
			oplus_vooc_set_reset_sleep(chip->vooc_ic);
		}
		return;
	}

	if (chip->fastchg_disable) {
		chip->switch_retry_count = 0;
		chg_info("fastchg disable, return\n");
		return;
	}
	if (chip->fastchg_started) {
		chip->switch_retry_count = 0;
		chg_err("wkcs: fastchg_started=%d\n", chip->fastchg_started);
		return;
	}
	chg_type = oplus_wired_get_chg_type();
	switch (chg_type) {
	case OPLUS_CHG_USB_TYPE_DCP:
	case OPLUS_CHG_USB_TYPE_ACA:
	case OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID:
	case OPLUS_CHG_USB_TYPE_SVOOC:
	case OPLUS_CHG_USB_TYPE_VOOC:
		break;
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		if (!chip->pd_svooc) {
			chip->switch_retry_count = 0;
			chg_info("pd_svooc=false\n");
			return;
		}
		break;
	default:
		chip->switch_retry_count = 0;
		chg_info("chg_type=%d, not support fastchg\n", chg_type);
		return;
	}

	chg_info("switch_retry_count=%d, fastchg_check_timeout=%lu\n",
		 chip->switch_retry_count, fastchg_check_timeout);

	chg_info("fast_chg_status=%d\n", chip->fast_chg_status);

	if (chip->switch_retry_count == 0) {
		if ((chip->fast_chg_status ==
			     CHARGER_STATUS_SWITCH_TEMP_RANGE ||
		     chip->fast_chg_status == CHARGER_STATUS_FAST_TO_WARM ||
		     chip->fast_chg_status == CHARGER_STATUS_FAST_DUMMY ||
		     chip->fast_chg_status == CHARGER_STATUS_TIMEOUT_RETRY) &&
		    oplus_vooc_is_allow_fast_chg(chip) &&
		    is_wired_charge_suspend_votable_available(chip)) {
			chg_info("fast_chg_status=%d reset adapter\n",
				 chip->fast_chg_status);
			/* Reset adapter */
			vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER,
			     true, 1, false);
			msleep(1000);
			vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER,
			     false, 0, false);
			msleep(50);
		}

		fastchg_check_timeout = jiffies;
		chg_err("switch to fastchg, jiffies=%lu\n",
			fastchg_check_timeout);
		chip->switch_retry_count = 1;
		oplus_vooc_switch_fast_chg(chip);
		schedule_delay = msecs_to_jiffies(15000);
		goto out;
	}

	if (time_is_after_jiffies(fastchg_check_timeout +
				  (unsigned long)(15 * HZ))) {
		schedule_delay = fastchg_check_timeout +
				 (unsigned long)(15 * HZ) - jiffies;
		goto out;
	} else if (time_is_before_jiffies(fastchg_check_timeout +
					  (unsigned long)(15 * HZ)) &&
		   time_is_after_jiffies(fastchg_check_timeout +
					 (unsigned long)(30 * HZ))) {
		if (chip->switch_retry_count != 1) {
			schedule_delay = fastchg_check_timeout +
					 (unsigned long)(30 * HZ) - jiffies;
			goto out;
		}
		chg_err("retry\n");
		if (is_wired_charge_suspend_votable_available(chip)) {
			/* Reset adapter */
			vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER,
			     true, 1, false);
			msleep(1000);
			vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER,
			     false, 0, false);
			msleep(50);
		}
		if (chip->wired_online &&
		    (oplus_chg_vooc_get_switch_mode(chip->vooc_ic) !=
		     VOOC_SWITCH_MODE_VOOC)) {
			switch_fast_chg(chip->vooc_ic);
			chg_err("D+D- did not switch to VOOC mode, try switching\n");
		}
		oplus_vooc_set_reset_active(chip->vooc_ic);
		chip->switch_retry_count = 2;
		schedule_delay = msecs_to_jiffies(15000);
	} else if (time_is_before_jiffies(fastchg_check_timeout +
					  (unsigned long)(30 * HZ))) {
		fastchg_check_timeout = 0;
		chip->switch_retry_count = 0;
		switch_normal_chg(chip->vooc_ic);
		oplus_vooc_set_reset_sleep(chip->vooc_ic);
		if (chg_type == OPLUS_CHG_USB_TYPE_DCP) {
			chg_err("detect qc\n");
			oplus_wired_qc_detect_enable(true);
		}
		vote(chip->vooc_disable_votable, TIMEOUT_VOTER, true, 1, false);
		vote(chip->pd_svooc_votable, DEF_VOTER, false, 0, false);
		vote(chip->pd_svooc_votable, SVID_VOTER, false, 0, false);
		return;
	} else {
		schedule_delay = 0;
	}

out:
	chg_info("schedule_delay = %ums\n", jiffies_to_msecs(schedule_delay));
	schedule_delayed_work(&chip->vooc_switch_check_work, schedule_delay);
}

static void oplus_vooc_check_charger_out_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_vooc *chip = container_of(dwork, struct oplus_chg_vooc,
						   check_charger_out_work);
	enum oplus_wired_cc_detect_status cc_detect = CC_DETECT_NULL;
	union mms_msg_data topic_data = { 0 };
	bool wired_online, present;
	int vbus;

	/* Here need to get the real connection status */
	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CC_DETECT,
				&topic_data, true);
	cc_detect = topic_data.intval;
	vbus = oplus_wired_get_vbus();
	present = oplus_wired_is_present();
	if (cc_detect == CC_DETECT_NULL || cc_detect == CC_DETECT_PLUGIN)
		wired_online = true;
	else
		wired_online = false;
	chg_info("cc_detect=%d, present=%d, vbus=%d", cc_detect, present, vbus);
	wired_online = wired_online && present && (vbus > 2500);
	if (!wired_online || chip->fastchg_force_exit) {
		/*
		 * Here are some resources that will only be available after
		 * the quick charge is successful (0x52)
		 */
		chg_info("charger out, fastchg_force_exit=%d\n",
			 chip->fastchg_force_exit);
		chip->fastchg_force_exit = false;
		chip->icon_debounce = false;
		if (!is_client_vote_enabled(chip->vooc_disable_votable,
					    FASTCHG_DUMMY_VOTER) ||
		    !wired_online) {
			oplus_vooc_set_online(chip, false);
			oplus_vooc_set_sid(chip, 0);
			oplus_vooc_chg_bynormal_path(chip);
		}
		oplus_vooc_set_vooc_started(chip, false);
		oplus_gauge_unlock();
		oplus_vooc_set_vooc_charging(chip, false);
		oplus_vooc_del_watchdog_timer(chip);
		oplus_vooc_set_awake(chip, false);
		oplus_vooc_reset_temp_range(chip);
	}
	/* Need to be set after clearing the fast charge state */
	oplus_vooc_set_online_keep(chip, false);
}

static void oplus_vooc_fastchg_exit(struct oplus_chg_vooc *chip,
				    bool restore_nor_chg)
{
	/* Clean up vooc charging related settings*/
	cancel_delayed_work_sync(&chip->vooc_switch_check_work);
	oplus_vooc_set_vooc_charging(chip, false);
	if (!is_client_vote_enabled(chip->vooc_disable_votable,
				    FASTCHG_DUMMY_VOTER)) {
		switch_normal_chg(chip->vooc_ic);
		oplus_vooc_set_reset_sleep(chip->vooc_ic);
		oplus_gauge_unlock();
		oplus_vooc_del_watchdog_timer(chip);
	} else {
		if (chip->config.vooc_version < VOOC_VERSION_5_0)
			switch_normal_chg(chip->vooc_ic);
	}
	oplus_vooc_set_vooc_started(chip, false);

	chip->bcc_curr_count = 0;
	if (oplus_vooc_get_bcc_support(chip)) {
		oplus_vooc_cancel_bcc_update_work_sync(chip);
	}

	if (!restore_nor_chg)
		return;

	if (is_wired_charging_disable_votable_available(chip)) {
		vote(chip->wired_charging_disable_votable, FASTCHG_VOTER, false,
		     0, false);
	}
	if (is_wired_charge_suspend_votable_available(chip)) {
		vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER, false,
		     0, false);
	}

	/* If fast charge is not disabled, try to reload fast charge */
	if (chip->wired_online &&
	    !get_effective_result(chip->vooc_disable_votable))
		schedule_delayed_work(&chip->vooc_switch_check_work, 0);
}

static int oplus_vooc_get_min_curr_level(int level_base, int level_new,
					 unsigned int sid, bool fw_7bit)
{
	int curr_base, curr_new;

	if (fw_7bit) {
		if (level_base >= CURR_LIMIT_7BIT_MAX ||
		    level_new >= CURR_LIMIT_7BIT_MAX) {
			pr_err("current limit level error\n");
			return level_base;
		}
		return level_new < level_base ? level_new : level_base;
	} else {
		if (level_base >= CURR_LIMIT_MAX ||
		    level_new >= CURR_LIMIT_MAX) {
			pr_err("current limit level error\n");
			return level_base;
		}

		if (sid_to_adapter_chg_type(sid) == CHARGER_TYPE_VOOC) {
			curr_base = oplus_vooc_curr_table[level_base - 1];
			curr_new = oplus_vooc_curr_table[level_new - 1];
		} else if (sid_to_adapter_chg_type(sid) == CHARGER_TYPE_SVOOC) {
			curr_base = oplus_svooc_curr_table[level_base - 1];
			curr_new = oplus_svooc_curr_table[level_new - 1];
		} else {
			chg_err("unknown adapter chg type(=%d)\n",
				sid_to_adapter_chg_type(sid));
			return level_base;
		}

		if (curr_base <= curr_new)
			return level_base;
		else
			return level_new;
	}
}

static int oplus_vooc_get_temp_range(struct oplus_chg_vooc *chip,
				     int vbat_temp_cur)
{
	int ret = 0;

	if (vbat_temp_cur < chip->efficient_vooc_little_cold_temp) { /*0-5C*/
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
	} else if (vbat_temp_cur < chip->efficient_vooc_cool_temp) { /*5-12C*/
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
	} else if (vbat_temp_cur <
		   chip->efficient_vooc_little_cool_temp) { /*12-18C*/
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
	} else if (vbat_temp_cur <
		   chip->efficient_vooc_normal_low_temp) { /*16-35C*/
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
	} else { /*25C-43C*/
		if (chip->spec.vooc_normal_high_temp == -EINVAL ||
		    vbat_temp_cur <
			    chip->efficient_vooc_normal_high_temp) { /*35C-43C*/
			chip->vooc_temp_cur_range =
				FASTCHG_TEMP_RANGE_NORMAL_HIGH;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
		} else {
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_WARM;
			chip->fastchg_batt_temp_status = BAT_TEMP_WARM;
		}
	}
	chg_info("vooc_temp_cur_range[%d], vbat_temp_cur[%d]",
		 chip->vooc_temp_cur_range, vbat_temp_cur);

	if (chip->vooc_temp_cur_range) {
		ret = chip->vooc_temp_cur_range - 1;
		chip->bcc_temp_range = chip->vooc_temp_cur_range - 1;
	}

	return ret;
}

static int oplus_get_cur_bat_soc(struct oplus_chg_vooc *chip)
{
	int soc = 0;
	union mms_msg_data data = { 0 };
	if (chip->gauge_topic != NULL) {
		oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC,
					&data, true);
		soc = data.intval;
	} else {
		soc = 50; /* default soc is 50% */
	}
	return soc;
}

static void oplus_vooc_reset_temp_range(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	chip->efficient_vooc_little_cold_temp = spec->vooc_little_cold_temp;
	chip->efficient_vooc_cool_temp = spec->vooc_cool_temp;
	chip->efficient_vooc_little_cool_temp = spec->vooc_little_cool_temp;
	chip->efficient_vooc_normal_low_temp = spec->vooc_normal_low_temp;
	chip->efficient_vooc_normal_high_temp = spec->vooc_normal_high_temp;
	chg_info("[%d %d %d %d %d]\n", chip->efficient_vooc_little_cold_temp,
		 chip->efficient_vooc_cool_temp,
		 chip->efficient_vooc_little_cool_temp,
		 chip->efficient_vooc_normal_low_temp,
		 chip->efficient_vooc_normal_high_temp);
}

static void oplus_vooc_rang_rise_update(struct oplus_chg_vooc *chip)
{
	int pre_vooc_temp_rang = chip->vooc_temp_cur_range;

	if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_WARM ||
	    (chip->spec.vooc_normal_high_temp == -EINVAL &&
	     pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_NORMAL_HIGH)) {
		chip->vooc_strategy_change_count++;
		if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
			chip->vooc_strategy_change_count = 0;
			oplus_set_fast_status(chip,
					      CHARGER_STATUS_FAST_TO_WARM);
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
		}
	} else if (chip->spec.vooc_normal_high_temp != -EINVAL &&
		   pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_NORMAL_HIGH) {
		oplus_set_fast_status(chip, CHARGER_STATUS_SWITCH_TEMP_RANGE);
		chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_normal_high_temp -= VOOC_TEMP_RANGE_THD;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_NORMAL_LOW) {
		chip->efficient_vooc_normal_low_temp -= VOOC_TEMP_RANGE_THD;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_LITTLE_COOL) {
		chip->efficient_vooc_little_cool_temp -= VOOC_TEMP_RANGE_THD;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_COOL) {
		if (oplus_get_cur_bat_soc(chip) <=
		    COOL_REANG_SWITCH_LIMMIT_SOC) {
			oplus_set_fast_status(chip,
					      CHARGER_STATUS_SWITCH_TEMP_RANGE);
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
			chip->vooc_temp_cur_range =
				FASTCHG_TEMP_RANGE_LITTLE_COOL;
		}
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_cool_temp -= VOOC_TEMP_RANGE_THD;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_LITTLE_COLD) {
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_little_cold_temp -= VOOC_TEMP_RANGE_THD;
	} else {
		chg_info("no support range\n");
	}
}

static void oplus_vooc_rang_drop_update(struct oplus_chg_vooc *chip)
{
	int pre_vooc_temp_rang = chip->vooc_temp_cur_range;

	if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_WARM) {
		oplus_set_fast_status(chip, CHARGER_STATUS_SWITCH_TEMP_RANGE);
		chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_normal_high_temp += VOOC_TEMP_RANGE_THD;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_NORMAL_HIGH) {
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_normal_low_temp += VOOC_TEMP_RANGE_THD;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_NORMAL_LOW) {
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_little_cool_temp += VOOC_TEMP_RANGE_THD;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_LITTLE_COOL) {
		if (oplus_get_cur_bat_soc(chip) <=
		    COOL_REANG_SWITCH_LIMMIT_SOC) {
			oplus_set_fast_status(chip,
					      CHARGER_STATUS_SWITCH_TEMP_RANGE);
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		}
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_cool_temp += VOOC_TEMP_RANGE_THD;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_COOL) {
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
		oplus_vooc_reset_temp_range(chip);
		chip->efficient_vooc_little_cold_temp += VOOC_TEMP_RANGE_THD;
	} else if (pre_vooc_temp_rang == FASTCHG_TEMP_RANGE_LITTLE_COLD) {
		chip->vooc_strategy_change_count++;
		if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
			chip->vooc_strategy_change_count = 0;
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			oplus_set_fast_status(chip,
					      CHARGER_STATUS_FAST_TO_WARM);
		}
	} else {
		chg_info("no support range\n");
	}
}

static void oplus_vooc_check_temp_range(struct oplus_chg_vooc *chip,
					int cur_temp)
{
	int rang_start = 0;
	int rang_end = 0;

	if (chip->vooc_temp_cur_range == FASTCHG_TEMP_RANGE_INIT)
		oplus_vooc_get_temp_range(chip, cur_temp);

	switch (chip->vooc_temp_cur_range) {
	case FASTCHG_TEMP_RANGE_WARM: /*43~52*/
		rang_start = chip->efficient_vooc_normal_high_temp;
		rang_end = chip->spec.vooc_over_high_temp;
		break;
	case FASTCHG_TEMP_RANGE_NORMAL_HIGH: /*35~43*/
		rang_start = chip->efficient_vooc_normal_low_temp;
		if (chip->spec.vooc_normal_high_temp != -EINVAL)
			rang_end = chip->efficient_vooc_normal_high_temp;
		else
			rang_end = chip->spec.vooc_over_high_temp;
		break;
	case FASTCHG_TEMP_RANGE_NORMAL_LOW: /*16~35*/
		rang_start = chip->efficient_vooc_little_cool_temp;
		rang_end = chip->efficient_vooc_normal_low_temp;
		break;
	case FASTCHG_TEMP_RANGE_LITTLE_COOL: /*12~16*/
		rang_start = chip->efficient_vooc_cool_temp;
		rang_end = chip->efficient_vooc_little_cool_temp;
		break;
	case FASTCHG_TEMP_RANGE_COOL: /*5 ~ 12*/
		rang_start = chip->efficient_vooc_little_cold_temp;
		rang_end = chip->efficient_vooc_cool_temp;
		break;
	case FASTCHG_TEMP_RANGE_LITTLE_COLD: /*0 ~ 5*/
		rang_start = chip->spec.vooc_over_low_temp;
		rang_end = chip->efficient_vooc_little_cold_temp;
		break;
	default:
		break;
	}

	chg_info("vooc_temp_cur_range[%d] cur_temp[%d] rang[%d %d]\n",
		 chip->vooc_temp_cur_range, cur_temp, rang_start, rang_end);

	if (cur_temp > rang_end)
		oplus_vooc_rang_rise_update(chip);
	else if (cur_temp < rang_start)
		oplus_vooc_rang_drop_update(chip);
	else
		chg_info("temperature range does not change\n");

	if ((cur_temp < chip->spec.vooc_over_low_temp ||
	     cur_temp > chip->spec.vooc_over_high_temp) &&
	    chip->fast_chg_status == CHARGER_STATUS_SWITCH_TEMP_RANGE) {
		oplus_set_fast_status(chip, CHARGER_STATUS_FAST_TO_WARM);
		chip->vooc_strategy_change_count = 0;
		chg_info("High and low temperature stop charging "
			 "reach across temperature range\n");
	}

	chg_info("after check vooc_temp_cur_range[%d]\n",
		 chip->vooc_temp_cur_range);
}

static int oplus_vooc_fastchg_process(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;
	union mms_msg_data data = { 0 };
	bool normal_chg_disable;
	int ret_info = 0;
	char buf[1] = { 0 };

	if (chip->fastchg_allow) {
		oplus_vooc_set_vooc_charging(chip, true);
		oplus_gauge_unlock();
		if (chip->gauge_topic != NULL)
			oplus_mms_topic_update(chip->gauge_topic, false);
		if (oplus_vooc_get_bcc_support(chip)) {
			oplus_gauge_fastchg_update_bcc_parameters(buf);
		}
		oplus_gauge_lock();
		if (chip->gauge_topic != NULL) {
			oplus_mms_get_item_data(chip->gauge_topic,
						GAUGE_ITEM_VOL_MAX, &data,
						false);
			chip->batt_volt = data.intval;
			oplus_mms_get_item_data(chip->gauge_topic,
						GAUGE_ITEM_CURR, &data, false);
			chip->icharging = data.intval;
		}
		if (chip->comm_topic != NULL) {
			oplus_mms_get_item_data(chip->comm_topic,
						COMM_ITEM_SHELL_TEMP, &data,
						true);
			chip->temperature = data.intval;
		}
		if (chip->wired_topic)
			oplus_wired_kick_wdt(chip->wired_topic);

		if (is_wired_charging_disable_votable_available(chip) &&
		    !is_client_vote_enabled(chip->wired_charging_disable_votable,
					    FASTCHG_VOTER)) {
			normal_chg_disable = false;
		} else {
			normal_chg_disable = true;
		}
		if (config->support_vooc_by_normal_charger_path) {
			if (is_wired_charge_suspend_votable_available(chip) &&
			    !is_client_vote_enabled(
				    chip->wired_charge_suspend_votable,
				    FASTCHG_VOTER)) {
				normal_chg_disable = false;
			}
			if (!normal_chg_disable && (chip->sid != 0) &&
			    (sid_to_adapter_chg_type(chip->sid) ==
			     CHARGER_TYPE_SVOOC)) {
				if (is_wired_charging_disable_votable_available(
					    chip)) {
					vote(chip->wired_charging_disable_votable,
					     FASTCHG_VOTER, true, 1, false);
				}
				if (is_wired_charge_suspend_votable_available(
					    chip)) {
					vote(chip->wired_charge_suspend_votable,
					     FASTCHG_VOTER, true, 1, false);
				}
			}
		} else {
			if (!normal_chg_disable && chip->icharging < -2000) {
				vote(chip->wired_charging_disable_votable,
				     FASTCHG_VOTER, true, 1, false);
			}
		}
	}

	oplus_vooc_setup_watchdog_timer(chip, 25000);
	ret_info = chip->curr_level;

	oplus_vooc_check_temp_range(chip, chip->temperature);

	if (chip->cool_down > 0) {
		ret_info =
			oplus_vooc_get_min_curr_level(ret_info, chip->cool_down,
						      chip->sid,
						      config->data_width == 7);
	}

	if (config->support_vooc_by_normal_charger_path &&
	    sid_to_adapter_chg_type(chip->sid) == CHARGER_TYPE_VOOC)
		ret_info = 0x02;

	chg_info("volt=%d, temp=%d, soc=%d, curr=%d, cool_down=%d\n",
		 chip->batt_volt, chip->temperature, chip->soc, chip->icharging,
		 chip->cool_down);

	return ret_info;
}

static int oplus_vooc_get_soc_range(struct oplus_chg_vooc *chip, int soc)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	int range = 0;
	int i;

	for (i = 0; i < VOOC_SOC_RANGE_NUM; i++) {
		if (soc > spec->vooc_soc_range[i])
			range++;
		else
			break;
	}
	chg_info("soc_range[%d], soc[%d]", range, soc);
	chip->bcc_soc_range = range;
	return range;
}

static int oplus_vooc_check_soc_and_temp_range(struct oplus_chg_vooc *chip)
{
	union mms_msg_data data = { 0 };
	int soc, temp;
	int ret;

	if (chip->gauge_topic != NULL) {
		oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC,
					&data, true);
		soc = data.intval;
	} else {
		soc = 50; /* default soc is 50% */
	}
	if (chip->comm_topic != NULL) {
		oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SHELL_TEMP,
					&data, true);
		temp = data.intval;
	} else {
		temp = 250; /* default temp is 25C */
	}

	/*
	 * Return Data Format:
	 * +--------+--------+
	 * |6      4|3     0 |
	 * +--------+--------+
	 *     |         |
	 *     |         +---> [3:0] temperature range
	 *     +-------------> [6:4] battery soc range
	 */
	ret = oplus_vooc_get_soc_range(chip, soc) << 4;
	ret |= oplus_vooc_get_temp_range(chip, temp);

	return ret;
}

static void oplus_vooc_bcc_get_curve(struct oplus_chg_vooc *chip)
{
	int bcc_batt_volt_now;
	int i;
	int curve_idx = 0;

	if (chip->bcc_choose_curve_done == false) {
		oplus_vooc_choose_bcc_fastchg_curve(chip);
		chip->bcc_choose_curve_done = true;
	}

	bcc_batt_volt_now = chip->batt_volt;
	for (i = 0; i < chip->svooc_batt_curve[0].bcc_curv_num; i++) {
		chg_err("bcc_batt is %d target_volt now is %d\n",
			bcc_batt_volt_now,
			chip->svooc_batt_curve[0].batt_bcc_curve[i].target_volt);
		if (bcc_batt_volt_now <
		    chip->svooc_batt_curve[0].batt_bcc_curve[i].target_volt) {
			curve_idx = i;
			chg_err("curve idx = %d\n", curve_idx);
			break;
		}
	}

	chip->bcc_max_curr =
		chip->svooc_batt_curve[0].batt_bcc_curve[curve_idx].max_ibus;
	chip->bcc_min_curr =
		chip->svooc_batt_curve[0].batt_bcc_curve[curve_idx].min_ibus;
	chg_err("choose max curr is %d, min curr is %d\n", chip->bcc_max_curr,
		chip->bcc_min_curr);
}

static void oplus_vooc_bcc_get_curr_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_vooc *chip = container_of(dwork, struct oplus_chg_vooc,
						   bcc_get_max_min_curr);
	int bcc_batt_volt_now;
	int i;
	static int pre_curve_idx = 0;
	static int idx_cnt = 0;

	bcc_batt_volt_now = chip->batt_volt;
	for (i = chip->bcc_true_idx; i < chip->svooc_batt_curve[0].bcc_curv_num;
	     i++) {
		chg_err("bcc_batt is %d target_volt now is %d i is %d\n",
			bcc_batt_volt_now,
			chip->svooc_batt_curve[0].batt_bcc_curve[i].target_volt,
			i);
		if (bcc_batt_volt_now <
		    chip->svooc_batt_curve[0].batt_bcc_curve[i].target_volt) {
			chip->bcc_curve_idx = i;
			chg_err("curve idx = %d\n", chip->bcc_curve_idx);
			break;
		}
	}

	if (pre_curve_idx == chip->bcc_curve_idx) {
		idx_cnt = idx_cnt + 1;
		if (idx_cnt >= 20) {
			chip->bcc_max_curr =
				chip->svooc_batt_curve[0]
					.batt_bcc_curve[chip->bcc_curve_idx]
					.max_ibus;
			chip->bcc_min_curr =
				chip->svooc_batt_curve[0]
					.batt_bcc_curve[chip->bcc_curve_idx]
					.min_ibus;
			chip->bcc_true_idx = chip->bcc_curve_idx;
			chg_err("choose max curr is %d, min curr is %d true idx is %d\n",
				chip->bcc_max_curr, chip->bcc_min_curr,
				chip->bcc_true_idx);
		}
	} else {
		idx_cnt = 0;
	}
	pre_curve_idx = chip->bcc_curve_idx;

	schedule_delayed_work(&chip->bcc_get_max_min_curr,
			      OPLUS_VOOC_BCC_UPDATE_INTERVAL);
}

static int oplus_vooc_push_break_code(struct oplus_chg_vooc *chip, int code)
{
	struct mms_msg *msg;
	int rc;

	if (chip->vooc_ic->type == OPLUS_CHG_IC_VIRTUAL_VPHY)
		return 0;

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				      VOOC_ITEM_BREAK_CODE, code);
	if (msg == NULL) {
		chg_err("alloc break code msg error\n");
		return -ENOMEM;
	}

	rc = oplus_mms_publish_msg_sync(chip->vooc_topic, msg);
	if (rc < 0) {
		chg_err("publish break code msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

bool oplus_vooc_wake_bcc_update_work(struct oplus_chg_vooc *chip)
{
	if (!chip) {
		chg_err(" g_vooc_chip NULL,return\n");
		return true;
	}
	schedule_delayed_work(&chip->bcc_get_max_min_curr,
			      OPLUS_VOOC_BCC_UPDATE_INTERVAL);
	chip->bcc_wake_up_done = true;
	return true;
}

void oplus_vooc_cancel_bcc_update_work_sync(struct oplus_chg_vooc *chip)
{
	if (!chip) {
		return;
	}
	cancel_delayed_work_sync(&chip->bcc_get_max_min_curr);
	chip->bcc_wake_up_done = false;
	chip->bcc_choose_curve_done = false;
	chip->bcc_curve_idx = 0;
	chip->bcc_true_idx = 0;
	chip->bcc_soc_range = 0;
	chip->bcc_temp_range = 0;
}

static bool oplus_vooc_fastchg_range_switch(struct oplus_chg_vooc *chip)
{
	bool ret = false;

	if (chip->fastchg_batt_temp_status == BAT_TEMP_EXIT &&
	    (chip->fast_chg_status == CHARGER_STATUS_SWITCH_TEMP_RANGE ||
	     chip->fast_chg_status == CHARGER_STATUS_FAST_TO_WARM) &&
	    !is_client_vote_enabled(chip->vooc_disable_votable,
				    FASTCHG_DUMMY_VOTER)) {
		if (chip->fast_chg_status == CHARGER_STATUS_SWITCH_TEMP_RANGE) {
			vote(chip->vooc_disable_votable, SWITCH_RANGE_VOTER,
			     true, 1, false);
			chg_info("CHARGER_STATUS_SWITCH_TEMP_RANGE \n");
		}
		if (chip->fast_chg_status == CHARGER_STATUS_FAST_TO_WARM) {
			vote(chip->vooc_disable_votable, BATT_TEMP_VOTER, true,
			     1, false);
			vote(chip->vooc_not_allow_votable, BATT_TEMP_VOTER,
			     true, 1, false);
			chg_info("CHARGER_STATUS_FAST_TO_WARM \n");
			oplus_vooc_push_break_code(chip,
						   TRACK_MCU_VOOCPHY_TEMP_OVER);
		}
		oplus_vooc_fastchg_exit(chip, true);
		oplus_vooc_del_watchdog_timer(chip);
		oplus_vooc_set_awake(chip, false);
		ret = true;
	}

	return ret;
}

static void oplus_vooc_fastchg_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip =
		container_of(work, struct oplus_chg_vooc, fastchg_work);
	struct oplus_vooc_spec_config *spec = &chip->spec;
	struct oplus_vooc_config *config = &chip->config;
	enum oplus_wired_cc_detect_status cc_detect = CC_DETECT_NULL;
	union mms_msg_data topic_data = { 0 };
	static bool fw_ver_info = false;
	static bool adapter_fw_ver_info = false;
	static bool adapter_model_factory = false;
	bool ignore_device_type = false;
	bool charger_delay_check = false;
	bool data_err = false;
	int data = 0;
	int ret_info = 0, ret_tmp;
	int rc;
	char buf[1] = { 0 };

	usleep_range(2000, 2000);
	/* TODO: check data gpio val */
	oplus_vooc_eint_unregister(chip->vooc_ic);
	data = oplus_vooc_read_ap_data(chip->vooc_ic);
	if (((data & 0xf0) != 0x50) && (!fw_ver_info) &&
	    (!adapter_fw_ver_info) && (!adapter_model_factory)) {
		/* data recvd not start from "101" */
		chg_err("data err:0x%x\n", data);
		if (chip->fastchg_started) {
			/* TODO clean fast charge flag */
			oplus_set_fast_status(chip, CHARGER_STATUS_UNKNOWN);
			oplus_vooc_push_break_code(chip, TRACK_MCU_VOOCPHY_DATA_ERROR);
			oplus_vooc_fastchg_exit(chip, true);
			data_err = true;
		}
			goto out;
	}

	chg_info("recv data: 0x%02x\n", data);

	switch (data) {
	case VOOC_NOTIFY_FAST_PRESENT:
		oplus_vooc_set_awake(chip, true);
		adapter_model_factory = false;
		oplus_vooc_set_online(chip, true);
		oplus_vooc_set_online_keep(chip, true);
		chip->temp_over_count = 0;
		oplus_gauge_lock();
		if (oplus_vooc_is_allow_fast_chg(chip)) {
			oplus_set_fast_status(chip, CHARGER_STATUS_UNKNOWN);
			cancel_delayed_work_sync(&chip->check_charger_out_work);
			oplus_vooc_set_vooc_started(chip, true);
			vote(chip->vooc_disable_votable, FASTCHG_DUMMY_VOTER,
			     false, 0, false);
			oplus_vooc_set_vooc_charging(chip, false);
			if (chip->general_strategy != NULL)
				oplus_chg_strategy_init(chip->general_strategy);
			oplus_vooc_setup_watchdog_timer(chip, 25000);
		} else {
			chg_info("not allow fastchg\n");
			oplus_set_fast_status(chip, CHARGER_STATUS_FAST_DUMMY);
			oplus_vooc_set_vooc_started(chip, false);
			vote(chip->vooc_disable_votable, FASTCHG_DUMMY_VOTER,
			     true, 1, false);
			oplus_vooc_fastchg_exit(chip, false);
		}
		/*
		 * Regardless of whether it is fast charging or not,
		 * a watchdog must be set here.
		 */
		chip->switch_retry_count = 0;
		if (config->vooc_version >= VOOC_VERSION_5_0)
			adapter_model_factory = true;
		oplus_vooc_setup_watchdog_timer(chip, 25000);
		oplus_select_abnormal_max_cur(chip);
		chip->vooc_strategy_change_count = 0;
		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	case VOOC_NOTIFY_FAST_ABSENT:
		adapter_model_factory = false;
		chip->mcu_vote_detach = true;
		oplus_vooc_push_break_code(chip, TRACK_MCU_VOOCPHY_FAST_ABSENT);
		if (!is_client_vote_enabled(chip->vooc_disable_votable,
					    FASTCHG_DUMMY_VOTER)) {
			if (!chip->icon_debounce) {
				oplus_vooc_set_online(chip, false);
				oplus_vooc_set_online_keep(chip, false);
				oplus_vooc_set_sid(chip, 0);
				oplus_vooc_chg_bynormal_path(chip);
			}
			oplus_set_fast_status(chip, CHARGER_STATUS_UNKNOWN);
		} else {
			oplus_set_fast_status(chip, CHARGER_STATUS_FAST_DUMMY);
			oplus_vooc_set_reset_sleep(chip->vooc_ic);
		}
		oplus_vooc_fastchg_exit(chip, true);
		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	case VOOC_NOTIFY_ALLOW_READING_IIC:
		chip->mcu_vote_detach = false;
		oplus_set_fast_status(chip, CHARGER_STATUS_FAST_CHARING);
		ret_info = oplus_vooc_fastchg_process(chip);
		if ((!chip->fastchg_allow || chip->fastchg_disable) &&
		    !is_client_vote_enabled(chip->vooc_disable_votable,
					    FASTCHG_DUMMY_VOTER)) {
			chg_err("exit the fast charge\n");
			charger_delay_check = true;
			oplus_vooc_push_break_code(chip,
						   TRACK_MCU_VOOCPHY_OTHER);
			oplus_vooc_fastchg_exit(chip, true);
			break;
		}
		if (chip->general_strategy != NULL) {
			rc = oplus_chg_strategy_get_data(chip->general_strategy,
							 &ret_tmp);
			if (rc < 0)
				chg_err("get strategy data error, rc=%d", rc);
			else
				ret_info = oplus_vooc_get_min_curr_level(
					ret_info, ret_tmp, chip->sid,
					config->data_width == 7);
		}

		if (chip->support_abnormal_adapter &&
		    chip->abnormal_adapter_dis_cnt > 0) {
			ret_info = oplus_vooc_get_min_curr_level(
				ret_info, chip->abnormal_allowed_current_max,
				chip->sid, config->data_width == 7);
		}

		if (oplus_wired_get_bcc_curr_done_status(chip->wired_topic) ==
		    BCC_CURR_DONE_REQUEST) {
			chip->bcc_curr_count = chip->bcc_curr_count + 1;
			if (chip->bcc_curr_count > 1) {
				oplus_wired_check_bcc_curr_done(
					chip->wired_topic);
				chip->bcc_curr_count = 0;
			}
		} else {
			chip->bcc_curr_count = 0;
		}

		if (oplus_vooc_get_bcc_support(chip) &&
		    chip->bcc_wake_up_done == false) {
			oplus_vooc_wake_bcc_update_work(chip);
		}

		break;
	case VOOC_NOTIFY_NORMAL_TEMP_FULL:
		charger_delay_check = true;
		if (spec->vooc_normal_high_temp != -EINVAL &&
		    chip->vooc_temp_cur_range == FASTCHG_TEMP_RANGE_WARM) {
			vote(chip->vooc_disable_votable, WARM_FULL_VOTER, true,
			     1, false);
			oplus_vooc_reset_temp_range(chip);
			oplus_set_fast_status(chip,
					      CHARGER_STATUS_FAST_TO_WARM);
		} else {
			vote(chip->vooc_disable_votable, CHG_FULL_VOTER, true,
			     1, false);
			oplus_set_fast_status(chip,
					      CHARGER_STATUS_FAST_TO_NORMAL);
		}
		oplus_vooc_push_break_code(chip,
					   TRACK_MCU_VOOCPHY_NORMAL_TEMP_FULL);
		oplus_vooc_fastchg_exit(chip, false);
		if (is_wired_charge_suspend_votable_available(chip)) {
			vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER,
			     false, 0, false);
		}
		oplus_comm_switch_ffc(chip->comm_topic);
		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	case VOOC_NOTIFY_LOW_TEMP_FULL:
		ignore_device_type = true;
		oplus_set_fast_status(chip, CHARGER_STATUS_UNKNOWN);
		oplus_vooc_bcc_parms_init(chip);
		if (config->data_width == VOOC_FW_7BIT) {
			ret_info = oplus_vooc_check_soc_and_temp_range(chip);
		} else {
			chg_err("Unsupported firmware data width(%d)\n",
				config->data_width);
			ret_info = VOOC_DEF_REPLY_DATA;
		}

		if (oplus_vooc_get_bcc_support(chip)) {
			oplus_gauge_fastchg_update_bcc_parameters(buf);
		}
		oplus_vooc_push_break_code(chip,
					   TRACK_MCU_VOOCPHY_LOW_TEMP_FULL);

		break;
	case VOOC_NOTIFY_DATA_UNKNOWN:
		charger_delay_check = true;
		oplus_set_fast_status(chip, CHARGER_STATUS_FAST_TO_NORMAL);
		oplus_vooc_fastchg_exit(chip, true);
		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	case VOOC_NOTIFY_FIRMWARE_UPDATE:
		break;
	case VOOC_NOTIFY_BAD_CONNECTED:
		charger_delay_check = true;
		oplus_set_fast_status(chip, CHARGER_STATUS_FAST_TO_NORMAL);
		vote(chip->vooc_disable_votable, BAD_CONNECTED_VOTER, true, 1,
		     false);
		oplus_vooc_push_break_code(chip,
					   TRACK_MCU_VOOCPHY_BAD_CONNECTED);
		oplus_vooc_fastchg_exit(chip, true);
		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	case VOOC_NOTIFY_TEMP_OVER:
		charger_delay_check = true;
		oplus_set_fast_status(chip, CHARGER_STATUS_FAST_TO_WARM);
		oplus_vooc_push_break_code(chip, TRACK_MCU_VOOCPHY_TEMP_OVER);
		oplus_vooc_fastchg_exit(chip, true);
		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	case VOOC_NOTIFY_ADAPTER_FW_UPDATE:
		break;
	case VOOC_NOTIFY_BTB_TEMP_OVER:
		charger_delay_check = true;
		oplus_set_fast_status(chip, CHARGER_STATUS_FAST_BTB_TEMP_OVER);
		vote(chip->vooc_disable_votable, BTB_TEMP_OVER_VOTER, true, 1,
		     false);
		oplus_vooc_push_break_code(chip,
					   TRACK_MCU_VOOCPHY_BTB_TEMP_OVER);
		oplus_vooc_fastchg_exit(chip, true);
		if (chip->wired_icl_votable)
			vote(chip->wired_icl_votable, BTB_TEMP_OVER_VOTER, true,
			     BTB_TEMP_OVER_MAX_INPUT_CUR, true);

		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	case VOOC_NOTIFY_ADAPTER_MODEL_FACTORY:
		adapter_model_factory = true;
		ret_info = VOOC_DEF_REPLY_DATA;
		break;
	default:
		if (adapter_model_factory) {
			adapter_model_factory = false;
			chip->adapter_id = data;
			/* TODO: error type check */
			oplus_vooc_set_sid(chip,
					   oplus_get_adapter_sid(chip, data));
			oplus_vooc_chg_bynormal_path(chip);
			if (is_client_vote_enabled(chip->vooc_disable_votable,
						   FASTCHG_DUMMY_VOTER) &&
			    config->vooc_version >= VOOC_VERSION_5_0)
				switch_normal_chg(chip->vooc_ic);

			ret_info = VOOC_DEF_REPLY_DATA;
			break;
		}
		/* TODO: Data error handling process */
		charger_delay_check = true;
		oplus_vooc_set_vooc_charging(chip, false);
		switch_normal_chg(chip->vooc_ic);
		oplus_vooc_set_reset_active(chip->vooc_ic);
		oplus_gauge_unlock();
		oplus_vooc_del_watchdog_timer(chip);
		oplus_vooc_set_vooc_started(chip, false);
		if (is_wired_charging_disable_votable_available(chip)) {
			vote(chip->wired_charging_disable_votable,
			     FASTCHG_VOTER, false, 0, false);
		}
		if (is_wired_charge_suspend_votable_available(chip)) {
			vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER,
			     false, 0, false);
		}
		oplus_vooc_push_break_code(chip, TRACK_MCU_VOOCPHY_OTHER);
		goto out;
	}

	if (oplus_vooc_fastchg_range_switch(chip))
		charger_delay_check = true;

	msleep(2);
	oplus_vooc_set_data_sleep(chip->vooc_ic);
	chg_info("ret_info=0x%02x\n", ret_info);
	if (ignore_device_type)
		oplus_vooc_reply_data_no_type(chip->vooc_ic, ret_info,
					      chip->config.data_width,
					      get_effective_result(chip->vooc_curr_votable));
	else
		oplus_vooc_reply_data(chip->vooc_ic, ret_info,
				      oplus_gauge_get_device_type_for_vooc(),
				      chip->config.data_width,
				      get_effective_result(chip->vooc_curr_votable));

	if (data == VOOC_NOTIFY_LOW_TEMP_FULL) {
		chg_err("after 0x53, get bcc curve\n");
		oplus_vooc_bcc_get_curve(chip);
	}

out:
	if (charger_delay_check) {
		chg_info("check charger out\n");
		oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CC_DETECT,
					&topic_data, true);
		cc_detect = topic_data.intval;
		schedule_delayed_work(&chip->check_charger_out_work,
				      (cc_detect == CC_DETECT_NOTPLUG) ?
					      0 :
					      msecs_to_jiffies(3000));
	}
	oplus_vooc_set_data_active(chip->vooc_ic);
	oplus_vooc_set_clock_active(chip->vooc_ic);
	usleep_range(10000, 10000);
	oplus_vooc_set_clock_sleep(chip->vooc_ic);
	usleep_range(25000, 25000);

	if ((data == VOOC_NOTIFY_FAST_ABSENT && !charger_delay_check &&
	     !chip->icon_debounce) ||
	    data_err) {
		chip->fastchg_force_exit = true;
		schedule_delayed_work(&chip->check_charger_out_work, 0);
	}

	if (chip->support_abnormal_adapter && data == VOOC_NOTIFY_FAST_ABSENT &&
	    chip->icon_debounce) {
		chip->fastchg_force_exit = false;
		chg_info("abnormal adapter check charger out\n");
		schedule_delayed_work(&chip->check_charger_out_work,
				      msecs_to_jiffies(3000));
	}

	oplus_vooc_eint_register(chip->vooc_ic);

	if (!chip->fastchg_started)
		oplus_vooc_set_awake(chip, false);
}

static void oplus_vooc_wired_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct oplus_chg_vooc *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			schedule_work(&chip->plugin_work);
			break;
		case WIRED_ITEM_CHG_TYPE:
			schedule_work(&chip->chg_type_change_work);
			break;
		case WIRED_ITEM_CC_MODE:
			break;
		case WIRED_ITEM_CC_DETECT:
			oplus_mms_get_item_data(chip->wired_topic,
						WIRED_ITEM_CC_DETECT, &data,
						false);
			chip->cc_detect = data.intval;
			if (chip->cc_detect == CC_DETECT_NOTPLUG)
				oplus_chg_clear_abnormal_adapter_var(chip);
			break;
		case WIRED_TIME_TYPEC_STATE:
			oplus_mms_get_item_data(chip->wired_topic,
						WIRED_TIME_TYPEC_STATE, &data,
						false);
			chip->typec_state = data.intval;
			if (chip->typec_state == 0) {
				oplus_chg_clear_abnormal_adapter_var(chip);
			}
			break;

		case WIRED_ITEM_PRESENT:
			schedule_work(&chip->abnormal_adapter_check_work);
			break;
		case WIRED_TIME_ABNORMAL_ADAPTER:
			oplus_mms_get_item_data(chip->wired_topic,
						WIRED_TIME_ABNORMAL_ADAPTER,
						&data, false);
			chip->is_abnormal_adapter = data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_vooc_subscribe_wired_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_vooc *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    oplus_vooc_wired_subs_callback, "vooc");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				true);
	chip->wired_online = !!data.intval | chip->vooc_online;
	if (chip->wired_online)
		schedule_delayed_work(&chip->vooc_switch_check_work, 0);

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_PRESENT, &data,
				true);
	chip->wired_present = !!data.intval;

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CC_DETECT, &data,
				true);
	chip->cc_detect = data.intval;
	oplus_mms_get_item_data(chip->wired_topic, WIRED_TIME_TYPEC_STATE, &data,
				true);
	chip->typec_state = data.intval;
}

static void oplus_vooc_plugin_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip =
		container_of(work, struct oplus_chg_vooc, plugin_work);
	union mms_msg_data data = { 0 };
	int ret = 0;

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				false);
	chip->wired_online = data.intval;
	if (chip->wired_online) {
		if (chip->comm_topic != NULL) {
			ret = oplus_mms_get_item_data(chip->comm_topic,
						COMM_ITEM_TEMP_REGION, &data,
						true);
			if (ret < 0) {
				chip->bat_temp_region = TEMP_REGION_NORMAL;
				chg_err("can't get COMM_ITEM_TEMP_REGION status, ret=%d", ret);
			} else {
				chip->bat_temp_region = data.intval;
			}
		} else {
			chip->bat_temp_region = TEMP_REGION_MAX;
		}
		schedule_delayed_work(&chip->vooc_switch_check_work, 0);
	} else {
		chg_info("wired charge offline\n");
		chip->bat_temp_region = TEMP_REGION_MAX;
		/* Clean up normal charging related settings */
		vote(chip->vooc_disable_votable, TIMEOUT_VOTER, false, 0,
		     false);
		vote(chip->vooc_disable_votable, FASTCHG_DUMMY_VOTER, false, 0,
		     false);
		vote(chip->vooc_disable_votable, CHG_FULL_VOTER, false, 0,
		     false);
		vote(chip->vooc_disable_votable, WARM_FULL_VOTER, false, 0,
		     false);
		vote(chip->vooc_disable_votable, BAD_CONNECTED_VOTER, false, 0,
		     false);
		vote(chip->vooc_disable_votable, SWITCH_RANGE_VOTER, false, 0,
		     false);
		vote(chip->vooc_disable_votable, BATT_TEMP_VOTER, false, 0,
		     false);
		vote(chip->vooc_disable_votable, BTB_TEMP_OVER_VOTER, false, 0,
		     false);
		if (chip->wired_icl_votable)
			vote(chip->wired_icl_votable, BTB_TEMP_OVER_VOTER,
			     false, 0, true);

		if (is_wired_charging_disable_votable_available(chip)) {
			vote(chip->wired_charging_disable_votable,
			     FASTCHG_VOTER, false, 0, false);
		}
		if (is_wired_charge_suspend_votable_available(chip)) {
			vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER,
			     false, 0, false);
		}
		/* USER_VOTER and HIDL_VOTER need to be invalid when the usb is unplugged */
		vote(chip->vooc_curr_votable, USER_VOTER, false, 0, false);
		vote(chip->vooc_curr_votable, HIDL_VOTER, false, 0, false);
		vote(chip->vooc_chg_auto_mode_votable, CHARGE_SUSPEND_VOTER,
		     false, 0, false);
		vote(chip->vooc_chg_auto_mode_votable, CHAEGE_DISABLE_VOTER,
		     false, 0, false);
		if (oplus_chg_vooc_get_switch_mode(chip->vooc_ic) !=
		    VOOC_SWITCH_MODE_NORMAL) {
			switch_normal_chg(chip->vooc_ic);
			oplus_vooc_set_reset_sleep(chip->vooc_ic);
		}
		/* Clear the status of dummy charge */
		oplus_vooc_set_online(chip, false);
		oplus_vooc_set_sid(chip, 0);
		oplus_vooc_chg_bynormal_path(chip);
		oplus_vooc_set_vooc_started(chip, false);
		oplus_vooc_set_online_keep(chip, false);
		oplus_vooc_set_vooc_charging(chip, false);
		oplus_vooc_set_awake(chip, false);
		oplus_vooc_reset_temp_range(chip);

		/* clean vooc switch status */
		chip->switch_retry_count = 0;
		oplus_set_fast_status(chip, CHARGER_STATUS_UNKNOWN);
		cancel_delayed_work_sync(&chip->vooc_switch_check_work);
	}
}

static void oplus_vooc_chg_type_change_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip =
		container_of(work, struct oplus_chg_vooc, chg_type_change_work);

	if (chip->wired_online)
		schedule_delayed_work(&chip->vooc_switch_check_work, 0);
	else
		chg_err("chg_type changed, but wired_online is false\n");
}

static void oplus_vooc_temp_region_update_work(struct work_struct *work)
{
	/*
	struct oplus_chg_vooc *chip = container_of(
		work, struct oplus_chg_vooc, temp_region_update_work);
	*/
}

static void oplus_abnormal_adapter_check_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip = container_of(work, struct oplus_chg_vooc,
						   abnormal_adapter_check_work);
	union mms_msg_data data = { 0 };
	int mmi_chg = 0;
	int down_to_up_time = 0;

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_PRESENT, &data,
				false);
	if (!chip->common_chg_suspend_votable)
		chip->common_chg_suspend_votable = find_votable("CHG_DISABLE");

	if (!chip->support_abnormal_adapter) {
		oplus_chg_clear_abnormal_adapter_var(chip);
		return;
	}

	if (chip->wired_present == (!!data.intval))
		return;

	if (chip->common_chg_suspend_votable)
		mmi_chg = !get_client_vote(chip->common_chg_suspend_votable,
					   MMI_CHG_VOTER);

	WRITE_ONCE(chip->wired_present, !!data.intval);
	if (!chip->wired_present) {
		chip->svooc_detach_time = local_clock() / 1000000;
		chip->pre_is_abnormal_adapter = chip->is_abnormal_adapter;
		chip->is_abnormal_adapter = false;

		if (mmi_chg == 0 || chip->mcu_vote_detach ||
		    chip->fast_chg_status == CHARGER_STATUS_FAST_TO_WARM ||
		    chip->fast_chg_status == CHARGER_STATUS_FAST_TO_NORMAL ||
		    chip->fast_chg_status == CHARGER_STATUS_FAST_DUMMY ||
		    chip->fast_chg_status ==
			    CHARGER_STATUS_FAST_BTB_TEMP_OVER ||
		    chip->fast_chg_status == CHARGER_STATUS_SWITCH_TEMP_RANGE ||
		    !chip->fastchg_ing)
			chip->pre_is_abnormal_adapter = false;

		if (chip->pre_is_abnormal_adapter)
			chip->icon_debounce = true;
		else
			chip->icon_debounce = false;
	} else {
		chip->svooc_detect_time = local_clock() / 1000000;
		down_to_up_time =
			chip->svooc_detect_time - chip->svooc_detach_time;

		if (mmi_chg && chip->pre_is_abnormal_adapter &&
		    down_to_up_time <= ABNORMAL_ADAPTER_BREAK_CHECK_TIME &&
		    (chip->mcu_vote_detach || chip->fastchg_ing)) {
			chip->abnormal_adapter_dis_cnt++;
		} else {
			chip->abnormal_adapter_dis_cnt = 0;
			chip->icon_debounce = false;
		}
	}
	chg_info("%s [%d %d %d %d %d %d %d %d]\n",
		 chip->wired_present == true ? "vbus up" : "vbus down", mmi_chg,
		 chip->fastchg_ing, chip->mcu_vote_detach,
		 chip->pre_is_abnormal_adapter, chip->fast_chg_status,
		 chip->icon_debounce, chip->abnormal_adapter_dis_cnt,
		 down_to_up_time);
}

static void oplus_vooc_comm_subs_callback(struct mms_subscribe *subs,
					  enum mms_msg_type type, u32 id)
{
	struct oplus_chg_vooc *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_TEMP_REGION:
			break;
		case COMM_ITEM_FCC_GEAR:
			break;
		case COMM_ITEM_COOL_DOWN:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->cool_down = data.intval;
			break;
		case COMM_ITEM_CHARGING_DISABLE:
		case COMM_ITEM_CHARGE_SUSPEND:
			schedule_work(&chip->comm_charge_disable_work);
			break;
		case COMM_ITEM_SHELL_TEMP:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->temperature = data.intval;
			break;
		case COMM_ITEM_UNWAKELOCK:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			vote(chip->vooc_disable_votable, DEBUG_VOTER,
			     data.intval, data.intval, false);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_vooc_subscribe_comm_topic(struct oplus_mms *topic,
					    void *prv_data)
{
	struct oplus_chg_vooc *chip = prv_data;
	bool disable;
	union mms_msg_data data = { 0 };
	int rc;

	chip->comm_topic = topic;
	chip->comm_subs = oplus_mms_subscribe(
		chip->comm_topic, chip, oplus_vooc_comm_subs_callback, "vooc");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe common topic error, rc=%ld\n",
			PTR_ERR(chip->comm_subs));
		return;
	}

	chg_err("init parameters");
	oplus_mms_get_item_data(chip->comm_topic,
				COMM_ITEM_TEMP_REGION, &data,
				true);
	chip->bat_temp_region = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_COOL_DOWN, &data,
				true);
	chip->cool_down = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SHELL_TEMP, &data,
				true);
	chip->temperature = data.intval;
	rc = oplus_mms_get_item_data(chip->comm_topic,
				     COMM_ITEM_CHARGING_DISABLE, &data, true);
	if (rc < 0) {
		pr_err("can't get charging disable status, rc=%d", rc);
		disable = false;
	} else {
		disable = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_CHARGE_SUSPEND,
				     &data, true);
	if (rc < 0) {
		pr_err("can't get charge suspend status, rc=%d", rc);
		data.intval = false;
	} else {
		if (!!data.intval)
			disable = true;
	}
	vote(chip->vooc_disable_votable, USER_VOTER, disable, disable, false);

	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UNWAKELOCK,
				     &data, true);
	if (rc < 0) {
		pr_err("can't get unwakelock status, rc=%d", rc);
		data.intval = 0;
	}
	vote(chip->vooc_disable_votable, DEBUG_VOTER, data.intval, data.intval,
	     false);
}

static void oplus_vooc_gauge_update_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip =
		container_of(work, struct oplus_chg_vooc, gauge_update_work);
	union mms_msg_data data = { 0 };
	int rc;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				false);
	chip->batt_volt = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				false);
	chip->icharging = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data,
				false);
	chip->soc = data.intval;

	/*
	 * The shell temperature update will not be released in real time,
	 * and the shell temperature needs to be actively obtained when
	 * the battery temperature is updated.
	 */
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SHELL_TEMP,
				     &data, false);
	if (!rc)
		chip->temperature = data.intval;

	oplus_vooc_fastchg_allow_or_enable_check(chip);
}

static void oplus_vooc_gauge_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id)
{
	struct oplus_chg_vooc *chip = subs->priv_data;
	union mms_msg_data data = { 0 };
	int rc;

	switch (type) {
	case MSG_TYPE_TIMER:
		schedule_work(&chip->gauge_update_work);
		break;
	case MSG_TYPE_ITEM:
		switch (id) {
		case GAUGE_ITEM_AUTH:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n",
					rc);
				chip->batt_auth = false;
			} else {
				chip->batt_auth = !!data.intval;
			}
			vote(chip->vooc_disable_votable, NON_STANDARD_VOTER,
			     (!chip->batt_hmac || !chip->batt_auth), 0, false);
			break;
		case GAUGE_ITEM_HMAC:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n",
					rc);
				chip->batt_hmac = false;
			} else {
				chip->batt_hmac = !!data.intval;
			}
			vote(chip->vooc_disable_votable, NON_STANDARD_VOTER,
			     (!chip->batt_hmac || !chip->batt_auth), 0, false);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_vooc_subscribe_gauge_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_vooc *chip = prv_data;
	union mms_msg_data data = { 0 };
	int rc;

	chip->gauge_topic = topic;
	chip->gauge_subs =
		oplus_mms_subscribe(chip->gauge_topic, chip,
				    oplus_vooc_gauge_subs_callback, "vooc");
	if (IS_ERR_OR_NULL(chip->gauge_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->gauge_subs));
		return;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_HMAC, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n", rc);
		chip->batt_hmac = false;
	} else {
		chip->batt_hmac = !!data.intval;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_AUTH, &data,
				     true);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n", rc);
		chip->batt_auth = false;
	} else {
		chip->batt_auth = !!data.intval;
	}

	if (!chip->batt_hmac || !chip->batt_auth) {
		vote(chip->vooc_disable_votable, NON_STANDARD_VOTER, true, 1,
		     false);
	}
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				true);
	chip->batt_volt = data.intval;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				true);
	chip->icharging = data.intval;

	(void)oplus_vooc_is_allow_fast_chg(chip);
}

static void oplus_chg_vooc_err_handler_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip =
		container_of(work, struct oplus_chg_vooc, err_handler_work);
	struct oplus_chg_ic_err_msg *msg, *tmp;
	struct list_head msg_list;

	INIT_LIST_HEAD(&msg_list);
	spin_lock(&chip->vooc_ic->err_list_lock);
	if (!list_empty(&chip->vooc_ic->err_list))
		list_replace_init(&chip->vooc_ic->err_list, &msg_list);
	spin_unlock(&chip->vooc_ic->err_list_lock);

	list_for_each_entry_safe (msg, tmp, &msg_list, list) {
		if (is_err_topic_available(chip))
			oplus_mms_publish_ic_err_msg(chip->err_topic,
						     ERR_ITEM_IC, msg);
		oplus_print_ic_err(msg);
		list_del(&msg->list);
		kfree(msg);
	}
}

static void oplus_comm_charge_disable_work(struct work_struct *work)
{
	struct oplus_chg_vooc *chip = container_of(work, struct oplus_chg_vooc,
						   comm_charge_disable_work);
	union mms_msg_data data = { 0 };
	bool disable;
	int rc;

	if (chip == NULL)
		return;

	rc = oplus_mms_get_item_data(chip->comm_topic,
				     COMM_ITEM_CHARGING_DISABLE, &data, false);
	if (rc < 0) {
		chg_err("can't get charging disable status, rc=%d", rc);
		disable = false;
	} else {
		disable = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_CHARGE_SUSPEND,
				     &data, false);
	if (rc < 0) {
		chg_err("can't get charge suspend status, rc=%d", rc);
		data.intval = false;
	} else {
		if (!!data.intval)
			disable = true;
	}

	if (disable) {
		cancel_delayed_work_sync(&chip->vooc_switch_check_work);
		chg_info("cancel_delayed_work_sync vooc_switch_check_work\n");
	}

	if (is_wired_charging_disable_votable_available(chip) && !disable) {
		vote(chip->wired_charging_disable_votable, FASTCHG_VOTER, false,
		     0, false);
	}

	if (is_wired_charge_suspend_votable_available(chip) && !disable) {
		vote(chip->wired_charge_suspend_votable, FASTCHG_VOTER, false,
		     0, false);
	}

	vote(chip->vooc_disable_votable, USER_VOTER, disable, disable, false);

	if (!chip->fastchg_started && disable &&
	    !is_client_vote_enabled(chip->vooc_disable_votable,
				    FASTCHG_DUMMY_VOTER)) {
		if (oplus_chg_vooc_get_switch_mode(chip->vooc_ic) !=
		    VOOC_SWITCH_MODE_NORMAL) {
			switch_normal_chg(chip->vooc_ic);
			oplus_vooc_set_reset_sleep(chip->vooc_ic);
		}
	}

	if (disable)
		return;

	vote(chip->vooc_disable_votable, TIMEOUT_VOTER, false, 0, false);
	vote(chip->vooc_disable_votable, CHG_FULL_VOTER, false, 0, false);
	vote(chip->vooc_disable_votable, WARM_FULL_VOTER, false, 0, false);
	vote(chip->vooc_disable_votable, SWITCH_RANGE_VOTER, false, 0, false);
	vote(chip->vooc_disable_votable, BATT_TEMP_VOTER, false, 0, false);

	return;
}

static void oplus_chg_vooc_err_handler(struct oplus_chg_ic_dev *ic_dev,
				       void *virq_data)
{
	struct oplus_chg_vooc *chip = virq_data;

	schedule_work(&chip->err_handler_work);
}

static void oplus_chg_vooc_data_handler(struct oplus_chg_ic_dev *ic_dev,
					void *virq_data)
{
	struct oplus_chg_vooc *chip = virq_data;

	schedule_work(&chip->fastchg_work);
}

static int oplus_chg_vooc_virq_register(struct oplus_chg_vooc *chip)
{
	int rc;

	rc = oplus_chg_ic_virq_register(chip->vooc_ic, OPLUS_IC_VIRQ_ERR,
					oplus_chg_vooc_err_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d", rc);
	rc = oplus_chg_ic_virq_register(chip->vooc_ic, OPLUS_IC_VIRQ_VOOC_DATA,
					oplus_chg_vooc_data_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_VOOC_DATA error, rc=%d", rc);

	return 0;
}

static int oplus_vooc_update_vooc_started(struct oplus_mms *topic,
					  union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	bool started = false;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(topic);

	started = chip->fastchg_started;

end:
	data->intval = started;
	return 0;
}

static int oplus_vooc_update_vooc_charging(struct oplus_mms *topic,
					   union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	bool charging = false;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(topic);

	charging = chip->fastchg_ing;

end:
	data->intval = charging;
	return 0;
}

static int oplus_vooc_update_online(struct oplus_mms *topic,
				    union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	bool online = false;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(topic);

	online = chip->vooc_online;

end:
	data->intval = online;
	return 0;
}

static int oplus_vooc_update_sid(struct oplus_mms *topic,
				 union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	unsigned int sid = 0;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(topic);

	sid = chip->sid;

end:
	data->intval = (int)sid;
	return 0;
}

static int oplus_vooc_update_online_keep(struct oplus_mms *topic,
					 union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	bool online_keep = false;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(topic);

	online_keep = chip->vooc_online_keep;

end:
	data->intval = online_keep;
	return 0;
}

static int oplus_vooc_update_bynormal_path(struct oplus_mms *topic,
					   union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	bool vooc_chg_by_normal = false;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(topic);
	if (chip)
		vooc_chg_by_normal = chip->vooc_chg_bynormal_path;

	data->intval = vooc_chg_by_normal;
	return 0;
}

static void oplus_vooc_update(struct oplus_mms *mms, bool publish)
{
}

static struct mms_item oplus_vooc_item[] = {
	{
		.desc = {
			.item_id = VOOC_ITEM_VOOC_STARTED,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_update_vooc_started,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_VOOC_CHARGING,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_update_vooc_charging,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_ONLINE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_update_online,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_SID,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_update_sid,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_ONLINE_KEEP,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_update_online_keep,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_VOOC_BY_NORMAL_PATH,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_update_bynormal_path,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_GET_BCC_MAX_CURR,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_get_bcc_max_curr,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_GET_BCC_MIN_CURR,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_get_bcc_min_curr,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_GET_BCC_STOP_CURR,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_get_bcc_stop_curr,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_GET_BCC_TEMP_RANGE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_get_bcc_temp_range,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_GET_BCC_SVOOC_TYPE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_get_svooc_type,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_GET_AFI_CONDITION,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = oplus_vooc_afi_update_condition,
		}
	},
	{
		.desc = {
			.item_id = VOOC_ITEM_BREAK_CODE,
			.str_data = false,
			.up_thr_enable = false,
			.down_thr_enable = false,
			.dead_thr_enable = false,
			.update = NULL,
		}
	},
};

static const struct oplus_mms_desc oplus_vooc_desc = {
	.name = "vooc",
	.type = OPLUS_MMS_TYPE_VOOC,
	.item_table = oplus_vooc_item,
	.item_num = ARRAY_SIZE(oplus_vooc_item),
	.update_items = NULL,
	.update_items_num = 0,
	.update_interval = 0, /* ms */
	.update = oplus_vooc_update,
};

static int oplus_vooc_topic_init(struct oplus_chg_vooc *chip)
{
	struct oplus_mms_config mms_cfg = {};
	int rc;

	mms_cfg.drv_data = chip;
	mms_cfg.of_node = chip->dev->of_node;

	if (of_property_read_bool(mms_cfg.of_node,
				  "oplus,topic-update-interval")) {
		rc = of_property_read_u32(mms_cfg.of_node,
					  "oplus,topic-update-interval",
					  &mms_cfg.update_interval);
		if (rc < 0)
			mms_cfg.update_interval = 0;
	}

	chip->vooc_topic =
		devm_oplus_mms_register(chip->dev, &oplus_vooc_desc, &mms_cfg);
	if (IS_ERR(chip->vooc_topic)) {
		chg_err("Couldn't register vooc topic\n");
		rc = PTR_ERR(chip->vooc_topic);
		return rc;
	}

	return 0;
}

static int oplus_chg_vooc_parse_dt(struct oplus_chg_vooc *chip,
				   struct device_node *node)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	struct oplus_vooc_config *config = &chip->config;
	int rc;

	rc = of_property_read_s32(node, "oplus_spec,vooc_low_temp",
				  &spec->vooc_low_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_low_temp reading failed, rc=%d\n", rc);
		spec->vooc_low_temp = default_spec_config.vooc_low_temp;
	}
	spec->vooc_over_low_temp = spec->vooc_low_temp - 5;

	rc = of_property_read_s32(node, "oplus_spec,vooc_little_cold_temp",
				  &spec->vooc_little_cold_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_little_cold_temp reading failed, rc=%d\n",
			rc);
		spec->vooc_little_cold_temp =
			default_spec_config.vooc_little_cold_temp;
	}

	rc = of_property_read_s32(node, "oplus_spec,vooc_cool_temp",
				  &spec->vooc_cool_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_cool_temp reading failed, rc=%d\n",
			rc);
		spec->vooc_cool_temp = default_spec_config.vooc_cool_temp;
	}

	rc = of_property_read_s32(node, "oplus_spec,vooc_little_cool_temp",
				  &spec->vooc_little_cool_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_little_cool_temp reading failed, rc=%d\n",
			rc);
		spec->vooc_little_cool_temp =
			default_spec_config.vooc_little_cool_temp;
	}

	rc = of_property_read_s32(node, "oplus_spec,vooc_normal_low_temp",
				  &spec->vooc_normal_low_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_normal_low_temp reading failed, rc=%d\n",
			rc);
		spec->vooc_normal_low_temp =
			default_spec_config.vooc_normal_low_temp;
	}

	rc = of_property_read_s32(node, "oplus_spec,vooc_normal_high_temp",
				  &spec->vooc_normal_high_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_normal_high_temp reading failed, rc=%d\n",
			rc);
		spec->vooc_normal_high_temp =
			default_spec_config.vooc_normal_high_temp;
	}

	rc = of_property_read_s32(node, "oplus_spec,vooc_high_temp",
				  &spec->vooc_high_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_high_temp reading failed, rc=%d\n",
			rc);
		spec->vooc_high_temp = default_spec_config.vooc_high_temp;
	}

	rc = of_property_read_s32(node, "oplus_spec,vooc_over_high_temp",
				  &spec->vooc_over_high_temp);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_over_high_temp reading failed, rc=%d\n",
			rc);
		spec->vooc_over_high_temp =
			default_spec_config.vooc_over_high_temp;
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_low_soc",
				  &spec->vooc_low_soc);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_low_soc reading failed, rc=%d\n", rc);
		spec->vooc_low_soc = default_spec_config.vooc_low_soc;
	}
	rc = of_property_read_u32(node, "oplus_spec,vooc_high_soc",
				  &spec->vooc_high_soc);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_high_soc reading failed, rc=%d\n", rc);
		spec->vooc_high_soc = default_spec_config.vooc_high_soc;
	}

	rc = of_property_read_u32(node, "oplus,vooc-version",
				  &config->vooc_version);
	if (rc < 0) {
		chg_err("oplus,vooc-version reading failed, rc=%d\n", rc);
		config->vooc_version = VOOC_VERSION_DEFAULT;
	}
	chg_info("vooc version is %d\n", config->vooc_version);

	if (spec->vooc_normal_high_temp != -EINVAL) {
		rc = of_property_read_u32(node, "oplus_spec,vooc_warm_vol_thr",
					  &spec->vooc_warm_vol_thr);
		if (rc < 0) {
			chg_err("oplus_spec,vooc_warm_vol_thr reading failed, rc=%d\n",
				rc);
			spec->vooc_warm_vol_thr =
				default_spec_config.vooc_warm_vol_thr;
		}
		rc = of_property_read_u32(node, "oplus_spec,vooc_warm_soc_thr",
					  &spec->vooc_warm_soc_thr);
		if (rc < 0) {
			chg_err("oplus_spec,vooc_warm_soc_thr reading failed, rc=%d\n",
				rc);
			spec->vooc_warm_soc_thr =
				default_spec_config.vooc_warm_soc_thr;
		}
	} else {
		spec->vooc_warm_vol_thr = default_spec_config.vooc_warm_vol_thr;
		spec->vooc_warm_soc_thr = default_spec_config.vooc_warm_soc_thr;
	}

	oplus_vooc_reset_temp_range(chip);

	if (!of_property_read_bool(node, "oplus_spec,vooc_bad_volt") ||
	    !of_property_read_bool(node, "oplus_spec,vooc_bad_volt_suspend")) {
		config->vooc_bad_volt_check_support = false;
		chg_info("not support vool bat vol check\n");
		goto skip_vooc_bad_volt_check;
	}
	rc = read_unsigned_data_from_node(node, "oplus_spec,vooc_bad_volt",
					  spec->vooc_bad_volt,
					  VOOC_BAT_VOLT_REGION);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_bad_volt reading failed, rc=%d\n", rc);
		config->vooc_bad_volt_check_support = false;
		goto skip_vooc_bad_volt_check;
	}
	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,vooc_bad_volt_suspend",
					  spec->vooc_bad_volt_suspend,
					  VOOC_BAT_VOLT_REGION);
	if (rc < 0) {
		chg_err("oplus_spec,vooc_bad_volt_suspend reading failed, rc=%d\n",
			rc);
		config->vooc_bad_volt_check_support = false;
		goto skip_vooc_bad_volt_check;
	}
	config->vooc_bad_volt_check_support = true;

skip_vooc_bad_volt_check:
	return 0;
}

static int oplus_vooc_info_init(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;

	if (config->data_width == 7) {
		vote(chip->vooc_curr_votable, DEF_VOTER, true,
		     oplus_vooc_7bit_curr_table[config->max_curr_level - 1],
		     false);
	} else {
		chip->curr_level = CURR_LIMIT_MAX - 1;
	}

	return 0;
}

static void oplus_vooc_fw_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_vooc *chip =
		container_of(dwork, struct oplus_chg_vooc, fw_update_work);
	int rc;

	rc = oplus_vooc_fw_check_then_recover(chip->vooc_ic);
	if (rc == FW_CHECK_MODE)
		chg_debug("update finish\n");
	vote(chip->vooc_disable_votable, UPGRADE_FW_VOTER, false, 0, false);
	if (chip->wired_icl_votable)
		vote(chip->wired_icl_votable, UPGRADE_FW_VOTER, false, 0, true);
}

static void oplus_vooc_fw_update_fix_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_vooc *chip =
		container_of(dwork, struct oplus_chg_vooc, fw_update_work_fix);
	int rc;

	rc = oplus_vooc_fw_check_then_recover_fix(chip->vooc_ic);
	if (rc == FW_CHECK_MODE)
		chg_debug("update finish\n");
	chip->mcu_update_ing_fix = false;
	vote(chip->vooc_disable_votable, UPGRADE_FW_VOTER, false, 0, false);
}

void oplus_vooc_fw_update_work_init(struct oplus_chg_vooc *chip)
{
	vote(chip->vooc_disable_votable, UPGRADE_FW_VOTER, true, 1, false);
	chip->wired_icl_votable = find_votable("WIRED_ICL");
	if (chip->wired_icl_votable)
		vote(chip->wired_icl_votable, UPGRADE_FW_VOTER, true, 500,
		     true);
	schedule_delayed_work(&chip->fw_update_work,
			      round_jiffies_relative(msecs_to_jiffies(
				      FASTCHG_FW_INTERVAL_INIT)));
}

static void oplus_vooc_init_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_vooc *chip =
		container_of(dwork, struct oplus_chg_vooc, vooc_init_work);
	struct device_node *node = chip->dev->of_node;
	static int retry = OPLUS_CHG_IC_INIT_RETRY_MAX;
	struct oplus_chg_ic_dev *real_vooc_ic = NULL;
	int rc;

	chip->vooc_ic = of_get_oplus_chg_ic(node, "oplus,vooc_ic", 0);
	if (chip->vooc_ic == NULL) {
		if (retry > 0) {
			retry--;
			schedule_delayed_work(
				&chip->vooc_init_work,
				msecs_to_jiffies(
					OPLUS_CHG_IC_INIT_RETRY_DELAY));
			return;
		} else {
			chg_err("oplus,vooc_ic not found\n");
		}
		retry = 0;
		return;
	}

	rc = oplus_chg_ic_func(chip->vooc_ic, OPLUS_IC_FUNC_INIT);
	if (rc == -EAGAIN) {
		if (retry > 0) {
			retry--;
			schedule_delayed_work(
				&chip->vooc_init_work,
				msecs_to_jiffies(
					OPLUS_CHG_IC_INIT_RETRY_DELAY));
			return;
		} else {
			chg_err("vooc_ic init timeout\n");
		}
		retry = 0;
		return;
	} else if (rc < 0) {
		chg_err("vooc_ic init error, rc=%d\n", rc);
		retry = 0;
		return;
	}
	retry = 0;

	rc = oplus_chg_ic_func(chip->vooc_ic, OPLUS_IC_FUNC_VOOC_GET_IC_DEV,
			       &real_vooc_ic);
	if (rc < 0) {
		chg_err("get real vooc ic error, rc=%d\n", rc);
		return;
	}
	if (real_vooc_ic == NULL) {
		chg_err("real vooc ic not found\n");
		rc = -ENODEV;
		goto get_real_vooc_ic_err;
	}
	rc = oplus_hal_vooc_init(chip->vooc_ic);
	if (rc < 0)
		goto hal_vooc_init_err;
	rc = oplus_chg_vooc_parse_dt(chip, real_vooc_ic->dev->of_node);
	if (rc < 0)
		goto parse_dt_err;
	oplus_vooc_fw_update_work_init(chip);
	rc = oplus_vooc_info_init(chip);
	if (rc < 0)
		goto vooc_init_err;
	rc = oplus_chg_vooc_virq_register(chip);
	if (rc < 0)
		goto virq_reg_err;
	oplus_vooc_eint_register(chip->vooc_ic);

	oplus_mcu_bcc_svooc_batt_curves(chip, real_vooc_ic->dev->of_node);
	oplus_mcu_bcc_stop_curr_dt(chip, real_vooc_ic->dev->of_node);

	oplus_mms_wait_topic("wired", oplus_vooc_subscribe_wired_topic, chip);
	oplus_mms_wait_topic("common", oplus_vooc_subscribe_comm_topic, chip);
	oplus_mms_wait_topic("gauge", oplus_vooc_subscribe_gauge_topic, chip);

	return;

virq_reg_err:
vooc_init_err:
parse_dt_err:
hal_vooc_init_err:
get_real_vooc_ic_err:
	oplus_chg_ic_func(chip->vooc_ic, OPLUS_IC_FUNC_EXIT);
	chip->vooc_ic = NULL;
	chg_err("vooc init error, rc=%d\n", rc);
}

static int oplus_vooc_get_curr_level(int curr_ma, int *buf, int len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		if (buf[i] == curr_ma)
			return ++i;
	}

	return 0;
}

static int oplus_vooc_curr_vote_callback(struct votable *votable, void *data,
					 int curr_ma, const char *client,
					 bool step)
{
	struct oplus_chg_vooc *chip = data;
	int curr_level;

	chg_info("%s vote vooc curr %d  \n", client, curr_ma);

	if (chip->config.data_width == 7) {
		curr_level = oplus_vooc_get_curr_level(
			curr_ma, oplus_vooc_7bit_curr_table,
			ARRAY_SIZE(oplus_vooc_7bit_curr_table));
	} else {
		if (sid_to_adapter_chg_type(chip->sid) == CHARGER_TYPE_VOOC) {
			curr_level = oplus_vooc_get_curr_level(
				curr_ma, oplus_vooc_curr_table,
				ARRAY_SIZE(oplus_vooc_curr_table));
		} else if (sid_to_adapter_chg_type(chip->sid) ==
			   CHARGER_TYPE_SVOOC) {
			curr_level = oplus_vooc_get_curr_level(
				curr_ma, oplus_svooc_curr_table,
				ARRAY_SIZE(oplus_svooc_curr_table));
		} else {
			chg_err("unknown adapter chg type(=%d)\n",
				sid_to_adapter_chg_type(chip->sid));
			curr_level = 0;
		}
	}

	if (curr_level > 0) {
		chip->curr_level = curr_level;
		chg_info("set curr level to %d\n", curr_level);
	} else {
		if (chip->config.data_width < 7)
			chip->curr_level = CURR_LIMIT_MAX - 1;
		chg_err("unsupported current gear, curr=%d curr_level=%d \n",
			curr_ma, chip->curr_level);
	}

	return 0;
}

static int oplus_vooc_disable_vote_callback(struct votable *votable, void *data,
					    int disable, const char *client,
					    bool step)
{
	struct oplus_chg_vooc *chip = data;

	chip->fastchg_disable = disable;
	chg_info("%s vooc charge\n", disable ? "disable" : "enable");

	if (chip->fastchg_disable)
		chip->switch_retry_count = 0;
	else if (chip->wired_online)
		schedule_delayed_work(&chip->vooc_switch_check_work, 0);

	return 0;
}

static int oplus_vooc_not_allow_vote_callback(struct votable *votable,
					      void *data, int not_allow,
					      const char *client, bool step)
{
	struct oplus_chg_vooc *chip = data;

	chip->fastchg_allow = !not_allow;
	chg_info("%sallow vooc charge\n", not_allow ? "not " : "");

	if (chip->fastchg_allow && chip->wired_online) {
		vote(chip->vooc_disable_votable, FASTCHG_DUMMY_VOTER, false, 0,
		     false);
		schedule_delayed_work(&chip->vooc_switch_check_work, 0);
	}

	return 0;
}

static int oplus_vooc_pd_svooc_vote_callback(struct votable *votable,
					     void *data, int allow,
					     const char *client, bool step)
{
	struct oplus_chg_vooc *chip = data;

	chip->pd_svooc = !!allow;
	chg_info("%sallow pd svooc\n", !allow ? "not " : "");

	if (chip->pd_svooc && chip->wired_online)
		schedule_delayed_work(&chip->vooc_switch_check_work, 0);

	return 0;
}

static int oplus_vooc_chg_auto_mode_vote_callback(struct votable *votable,
						  void *data, int enable,
						  const char *client, bool step)
{
	struct oplus_chg_vooc *chip = data;
	int rc;

	rc = set_chg_auto_mode(chip->vooc_ic, enable);
	if (rc)
		chg_err("falied to %s %s chg auto mode, rc= %d\n", client,
			 enable ? "enable" : "disable", rc);
	else
		chg_info("%s %s chg auto mode\n", client,
			 enable ? "enable" : "disable");

	return rc;
}

static int oplus_vooc_vote_init(struct oplus_chg_vooc *chip)
{
	int rc;

	chip->vooc_curr_votable = create_votable(
		"VOOC_CURR", VOTE_MIN, oplus_vooc_curr_vote_callback, chip);
	if (IS_ERR(chip->vooc_curr_votable)) {
		rc = PTR_ERR(chip->vooc_curr_votable);
		chip->vooc_curr_votable = NULL;
		return rc;
	}

	chip->vooc_disable_votable =
		create_votable("VOOC_DISABLE", VOTE_SET_ANY,
			       oplus_vooc_disable_vote_callback, chip);
	if (IS_ERR(chip->vooc_disable_votable)) {
		rc = PTR_ERR(chip->vooc_disable_votable);
		chip->vooc_disable_votable = NULL;
		goto creat_disable_votable_err;
	}

	chip->vooc_not_allow_votable =
		create_votable("VOOC_NOT_ALLOW", VOTE_SET_ANY,
			       oplus_vooc_not_allow_vote_callback, chip);
	if (IS_ERR(chip->vooc_not_allow_votable)) {
		rc = PTR_ERR(chip->vooc_not_allow_votable);
		chip->vooc_not_allow_votable = NULL;
		goto creat_not_allow_votable_err;
	}

	chip->pd_svooc_votable =
		create_votable("PD_SVOOC", VOTE_SET_ANY,
			       oplus_vooc_pd_svooc_vote_callback, chip);
	if (IS_ERR(chip->pd_svooc_votable)) {
		rc = PTR_ERR(chip->pd_svooc_votable);
		chip->pd_svooc_votable = NULL;
		goto creat_pd_svooc_votable_err;
	}

	chip->vooc_chg_auto_mode_votable =
		create_votable("VOOC_CHG_AUTO_MODE", VOTE_SET_ANY,
			       oplus_vooc_chg_auto_mode_vote_callback, chip);
	if (IS_ERR(chip->vooc_chg_auto_mode_votable)) {
		rc = PTR_ERR(chip->vooc_chg_auto_mode_votable);
		chip->vooc_chg_auto_mode_votable = NULL;
		goto creat_vooc_chg_auto_mode_votable_err;
	}

	return 0;
creat_vooc_chg_auto_mode_votable_err:
	destroy_votable(chip->pd_svooc_votable);
creat_pd_svooc_votable_err:
	destroy_votable(chip->vooc_not_allow_votable);
creat_not_allow_votable_err:
	destroy_votable(chip->vooc_disable_votable);
creat_disable_votable_err:
	destroy_votable(chip->vooc_curr_votable);
	return rc;
}

static int oplus_abnormal_adapter_pase_dt(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;
	struct device_node *node = chip->dev->of_node;
	int loop, rc;

	chip->support_abnormal_adapter = false;
	chip->abnormal_adapter_cur_arraycnt = of_property_count_elems_of_size(
		node, "oplus,abnormal_adapter_current",
		sizeof(*config->abnormal_adapter_cur_array));

	if (chip->abnormal_adapter_cur_arraycnt > 0) {
		chg_info("abnormal_adapter_cur_arraycnt[%d]\n",
			 chip->abnormal_adapter_cur_arraycnt);
		config->abnormal_adapter_cur_array = devm_kcalloc(
			chip->dev, chip->abnormal_adapter_cur_arraycnt,
			sizeof(*config->abnormal_adapter_cur_array),
			GFP_KERNEL);
		if (!config->abnormal_adapter_cur_array) {
			chg_info(
				"devm_kcalloc abnormal_adapter_current fail\n");
			return 0;
		}
		rc = of_property_read_u32_array(
			node, "oplus,abnormal_adapter_current",
			config->abnormal_adapter_cur_array,
			chip->abnormal_adapter_cur_arraycnt);
		if (rc) {
			chg_info("qcom,abnormal_adapter_current error\n");
			return rc;
		}

		for (loop = 0; loop < chip->abnormal_adapter_cur_arraycnt;
		     loop++) {
			chg_info("abnormal_adapter_current[%d]\n",
				 config->abnormal_adapter_cur_array[loop]);
		}
		chip->support_abnormal_adapter = true;
	}
	chip->abnormal_allowed_current_max = config->max_curr_level;
	oplus_chg_clear_abnormal_adapter_var(chip);
	return 0;
}

int oplus_mcu_bcc_svooc_batt_curves(struct oplus_chg_vooc *chip,
				    struct device_node *node)
{
	struct device_node *svooc_node, *soc_node;
	int rc = 0, i, j, length;

	svooc_node =
		of_get_child_by_name(node, "oplus_spec,svooc_charge_curve");
	if (!svooc_node) {
		chg_err("Can not find svooc_charge_strategy node\n");
		return -EINVAL;
	}

	for (i = 0; i < BCC_BATT_SOC_90_TO_100; i++) {
		soc_node = of_get_child_by_name(svooc_node, strategy_soc[i]);
		if (!soc_node) {
			chg_err("Can not find %s node\n", strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < BATT_BCC_CURVE_MAX; j++) {
			rc = of_property_count_elems_of_size(
				soc_node, strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				if (j == BATT_BCC_CURVE_TEMP_WARM) {
					continue;
				} else {
					chg_err("Count %s failed, rc=%d\n",
						strategy_temp[j], rc);
					return rc;
				}
			}

			length = rc;

			switch (i) {
			case BCC_BATT_SOC_0_TO_50:
				rc = of_property_read_u32_array(
					soc_node, strategy_temp[j],
					(u32 *)bcc_curves_soc0_2_50[j]
						.batt_bcc_curve,
					length);
				bcc_curves_soc0_2_50[j].bcc_curv_num =
					length / 4;
				break;
			case BCC_BATT_SOC_50_TO_75:
				rc = of_property_read_u32_array(
					soc_node, strategy_temp[j],
					(u32 *)bcc_curves_soc50_2_75[j]
						.batt_bcc_curve,
					length);
				bcc_curves_soc50_2_75[j].bcc_curv_num =
					length / 4;
				break;
			case BCC_BATT_SOC_75_TO_85:
				rc = of_property_read_u32_array(
					soc_node, strategy_temp[j],
					(u32 *)bcc_curves_soc75_2_85[j]
						.batt_bcc_curve,
					length);
				bcc_curves_soc75_2_85[j].bcc_curv_num =
					length / 4;
				break;
			case BCC_BATT_SOC_85_TO_90:
				rc = of_property_read_u32_array(
					soc_node, strategy_temp[j],
					(u32 *)bcc_curves_soc85_2_90[j]
						.batt_bcc_curve,
					length);
				bcc_curves_soc85_2_90[j].bcc_curv_num =
					length / 4;
				break;
			default:
				break;
			}
		}
	}

	return rc;
}

int oplus_mcu_bcc_stop_curr_dt(struct oplus_chg_vooc *chip,
			       struct device_node *node)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	int rc;

	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,bcc_stop_curr_0_to_50",
					  spec->bcc_stop_curr_0_to_50,
					  VOOC_BCC_STOP_CURR_NUM);
	if (rc < 0) {
		chg_err("oplus_spec,bcc_stop_curr_0_to_50 reading failed, rc=%d\n",
			rc);
		goto read_bcc_stop_curr_fail;
	}

	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,bcc_stop_curr_51_to_75",
					  spec->bcc_stop_curr_51_to_75,
					  VOOC_BCC_STOP_CURR_NUM);
	if (rc < 0) {
		chg_err("oplus_spec,bcc_stop_curr_51_to_75 reading failed, rc=%d\n",
			rc);
		goto read_bcc_stop_curr_fail;
	}

	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,bcc_stop_curr_76_to_85",
					  spec->bcc_stop_curr_76_to_85,
					  VOOC_BCC_STOP_CURR_NUM);
	if (rc < 0) {
		chg_err("oplus_spec,bcc_stop_curr_76_to_85  reading failed, rc=%d\n",
			rc);
		goto read_bcc_stop_curr_fail;
	}

	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,bcc_stop_curr_86_to_90",
					  spec->bcc_stop_curr_86_to_90,
					  VOOC_BCC_STOP_CURR_NUM);
	if (rc < 0) {
		chg_err("oplus_spec,bcc_stop_curr_86_to_90 reading failed, rc=%d\n",
			rc);
		goto read_bcc_stop_curr_fail;
	}

read_bcc_stop_curr_fail:
	return rc;
}

static int oplus_vooc_parse_dt(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;
	struct oplus_vooc_spec_config *spec = &chip->spec;
	struct device_node *node = chip->dev->of_node;
	int data;
	int rc;

	chip->smart_chg_bcc_support =
		of_property_read_bool(node, "oplus,smart_chg_bcc_support");
	chg_err("oplus,smart_chg_bcc_support is %d\n",
		chip->smart_chg_bcc_support);

	rc = of_property_read_u32(node, "oplus,vooc_data_width", &data);
	if (rc < 0) {
		chg_err("oplus,vooc_data_width reading failed, rc=%d\n", rc);
		config->data_width = 7;
	} else {
		config->data_width = (uint8_t)data;
	}
	rc = of_property_read_u32(node, "oplus,vooc_curr_max", &data);
	if (rc < 0) {
		chg_err("oplus,vooc_curr_max reading failed, rc=%d\n", rc);
		config->max_curr_level = CURR_LIMIT_7BIT_6_3A;
	} else {
		config->max_curr_level = (uint8_t)data;
	}
	rc = of_property_read_u32(node, "oplus,vooc_power_max_w", &data);
	if (rc < 0) {
		chg_err("oplus,vooc_power_max_w reading failed, rc=%d\n", rc);
		config->max_power_w = 65;
	} else {
		config->max_power_w = (uint32_t)data;
	}
	rc = of_property_read_u32(node, "oplus,voocphy_support", &data);
	if (rc < 0) {
		chg_err("oplus,voocphy_support reading failed, rc=%d\n", rc);
		config->voocphy_support = NO_VOOCPHY;
	} else {
		config->voocphy_support = (uint8_t)data;
	}
	rc = of_property_read_u32(node, "oplus,vooc_project", &data);
	if (rc < 0) {
		chg_err("oplus,vooc_project reading failed, rc=%d\n", rc);
		config->vooc_project = 0;
	} else {
		config->vooc_project = (uint8_t)data;
	}
	if (config->vooc_project > 1)
		config->svooc_support = true;
	else
		config->svooc_support = false;
	config->support_vooc_by_normal_charger_path = of_property_read_bool(
		node, "oplus,support_vooc_by_normal_charger_path");

	chg_info("read support_vooc_by_normal_charger_path=%d\n",
		 config->support_vooc_by_normal_charger_path);

	rc = of_property_read_string(node, "oplus,general_strategy_name",
				     (const char **)&config->strategy_name);
	if (rc >= 0) {
		chg_info("strategy_name=%s\n", config->strategy_name);
		rc = oplus_chg_strategy_read_data(chip->dev,
						  "oplus,general_strategy_data",
						  &config->strategy_data);
		if (rc < 0) {
			chg_err("read oplus,general_strategy_data failed, rc=%d\n",
				rc);
			config->strategy_data = NULL;
			config->strategy_data_size = 0;
		} else {
			chg_info("oplus,general_strategy_data size is %d\n",
				 rc);
			config->strategy_data_size = rc;
		}
	}

	rc = read_unsigned_data_from_node(node, "oplus_spec,vooc_soc_range",
					  spec->vooc_soc_range,
					  VOOC_SOC_RANGE_NUM);
	if (rc != VOOC_SOC_RANGE_NUM) {
		chg_err("oplus_spec,vooc_soc_range reading failed, rc=%d\n",
			rc);

/*
		 * use default data, refer to Charging Specification 3.6
		 *
		 * the default soc range is as follows:
		 * 0% -- 50% -- 75% -- 85% -- 100%
		 */
#define VOOC_DEFAULT_SOC_RANGE_1 50
#define VOOC_DEFAULT_SOC_RANGE_2 75
#define VOOC_DEFAULT_SOC_RANGE_3 85

		spec->vooc_soc_range[0] = VOOC_DEFAULT_SOC_RANGE_1;
		spec->vooc_soc_range[1] = VOOC_DEFAULT_SOC_RANGE_2;
		spec->vooc_soc_range[2] = VOOC_DEFAULT_SOC_RANGE_3;
	}
	rc = read_unsigned_data_from_node(node, "oplus_spec,vooc_temp_range",
					  spec->vooc_temp_range,
					  VOOC_TEMP_RANGE_NUM);
	if (rc != VOOC_TEMP_RANGE_NUM) {
		chg_err("oplus_spec,vooc_temp_range reading failed, rc=%d\n",
			rc);

/*
		 * use default data, refer to Charging Specification 3.6
		 *
		 * the default temp range is as follows:
		 * 0C -- 5C -- 12C -- 16C -- 35C -- 44C
		 */
#define VOOC_DEFAULT_TEMP_RANGE_1 50
#define VOOC_DEFAULT_TEMP_RANGE_2 120
#define VOOC_DEFAULT_TEMP_RANGE_3 160
#define VOOC_DEFAULT_TEMP_RANGE_4 350
#define VOOC_DEFAULT_TEMP_RANGE_5 450

		spec->vooc_temp_range[0] = VOOC_DEFAULT_TEMP_RANGE_1;
		spec->vooc_temp_range[1] = VOOC_DEFAULT_TEMP_RANGE_2;
		spec->vooc_temp_range[2] = VOOC_DEFAULT_TEMP_RANGE_3;
		spec->vooc_temp_range[3] = VOOC_DEFAULT_TEMP_RANGE_4;
		spec->vooc_temp_range[4] = VOOC_DEFAULT_TEMP_RANGE_5;
	}

	oplus_abnormal_adapter_pase_dt(chip);

	return 0;
}

static int oplus_vooc_strategy_init(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;

	chip->general_strategy =
		oplus_chg_strategy_alloc(config->strategy_name,
					 config->strategy_data,
					 config->strategy_data_size);
	if (chip->general_strategy == NULL)
		chg_err("%s strategy alloc error", config->strategy_name);

	return 0;
}

static ssize_t proc_fastchg_fw_update_write(struct file *file,
					    const char __user *buff, size_t len,
					    loff_t *data)
{
	struct oplus_chg_vooc *chip = PDE_DATA(file_inode(file));
	char write_data[32] = { 0 };

	if (len > sizeof(write_data)) {
		return -EINVAL;
	}

	if (copy_from_user(&write_data, buff, len)) {
		chg_err("fastchg_fw_update error.\n");
		return -EFAULT;
	}

	if (write_data[0] == '1') {
		chg_info("fastchg_fw_update\n");
		chip->fw_update_flag = true;
		vote(chip->vooc_disable_votable, UPGRADE_FW_VOTER, true, 1,
		     false);
		schedule_delayed_work(&chip->fw_update_work, 0);
	} else {
		chip->fw_update_flag = false;
		chg_info("Disable fastchg_fw_update\n");
	}

	return len;
}

static ssize_t proc_fastchg_fw_update_read(struct file *file, char __user *buff,
					   size_t count, loff_t *off)
{
	struct oplus_chg_vooc *chip = PDE_DATA(file_inode(file));
	char page[256] = { 0 };
	char read_data[32] = { 0 };
	int len = 0;

	if (chip->fw_update_flag) {
		read_data[0] = '1';
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
static const struct file_operations fastchg_fw_update_proc_fops = {
	.write = proc_fastchg_fw_update_write,
	.read = proc_fastchg_fw_update_read,
};
#else
static const struct proc_ops fastchg_fw_update_proc_fops = {
	.proc_write = proc_fastchg_fw_update_write,
	.proc_read = proc_fastchg_fw_update_read,
};
#endif
static int oplus_vooc_proc_init(struct oplus_chg_vooc *chip)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create_data("fastchg_fw_update", 0664, NULL,
			     &fastchg_fw_update_proc_fops, chip);
	if (!p)
		chg_err("proc_create fastchg_fw_update_proc_fops fail\n");
	return 0;
}

static void oplus_turn_off_fastchg(struct oplus_chg_vooc *chip)
{
	if (!chip) {
		return;
	}

	oplus_vooc_set_online(chip, false);
	oplus_vooc_set_online_keep(chip, false);
	oplus_vooc_set_sid(chip, 0);
	oplus_vooc_chg_bynormal_path(chip);
	oplus_set_fast_status(chip, CHARGER_STATUS_UNKNOWN);
	oplus_vooc_fastchg_exit(chip, true);
}

static int oplus_vooc_probe(struct platform_device *pdev)
{
	struct oplus_chg_vooc *chip;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_vooc),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);

	rc = oplus_vooc_parse_dt(chip);
	if (rc < 0)
		goto parse_dt_err;
	rc = oplus_vooc_vote_init(chip);
	if (rc < 0)
		goto vote_init_err;
	rc = oplus_vooc_topic_init(chip);
	if (rc < 0)
		goto topic_init_err;
	rc = oplus_vooc_strategy_init(chip);
	if (rc < 0)
		goto strategy_init_err;

	INIT_DELAYED_WORK(&chip->fw_update_work, oplus_vooc_fw_update_work);
	INIT_DELAYED_WORK(&chip->fw_update_work_fix,
			  oplus_vooc_fw_update_fix_work);
	rc = oplus_vooc_proc_init(chip);
	if (rc < 0)
		goto proc_init_err;

	INIT_DELAYED_WORK(&chip->vooc_init_work, oplus_vooc_init_work);
	INIT_DELAYED_WORK(&chip->vooc_switch_check_work,
			  oplus_vooc_switch_check_work);
	INIT_DELAYED_WORK(&chip->check_charger_out_work,
			  oplus_vooc_check_charger_out_work);
	INIT_WORK(&chip->fastchg_work, oplus_vooc_fastchg_work);
	INIT_WORK(&chip->plugin_work, oplus_vooc_plugin_work);
	INIT_WORK(&chip->abnormal_adapter_check_work,
		  oplus_abnormal_adapter_check_work);
	INIT_WORK(&chip->chg_type_change_work, oplus_vooc_chg_type_change_work);
	INIT_WORK(&chip->temp_region_update_work,
		  oplus_vooc_temp_region_update_work);
	INIT_WORK(&chip->gauge_update_work, oplus_vooc_gauge_update_work);
	INIT_WORK(&chip->vooc_watchdog_work, oplus_vooc_watchdog_work);
	INIT_WORK(&chip->err_handler_work, oplus_chg_vooc_err_handler_work);
	INIT_WORK(&chip->comm_charge_disable_work,
		  oplus_comm_charge_disable_work);

	INIT_DELAYED_WORK(&chip->bcc_get_max_min_curr,
			  oplus_vooc_bcc_get_curr_func);

	oplus_vooc_init_watchdog_timer(chip);
	oplus_vooc_awake_init(chip);

	schedule_delayed_work(&chip->vooc_init_work, 0);

	chg_info("probe success\n");
	return 0;

proc_init_err:
	if (chip->general_strategy)
		oplus_chg_strategy_release(chip->general_strategy);
strategy_init_err:
topic_init_err:
	destroy_votable(chip->vooc_chg_auto_mode_votable);
	destroy_votable(chip->pd_svooc_votable);
	destroy_votable(chip->vooc_not_allow_votable);
	destroy_votable(chip->vooc_disable_votable);
	destroy_votable(chip->vooc_curr_votable);
vote_init_err:
	if (chip->config.strategy_data)
		devm_kfree(&pdev->dev, chip->config.strategy_data);
parse_dt_err:
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);
	chg_err("probe error, rc=%d\n", rc);
	return rc;
}

static int oplus_vooc_remove(struct platform_device *pdev)
{
	struct oplus_chg_vooc *chip = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(chip->comm_subs))
		oplus_mms_unsubscribe(chip->comm_subs);
	if (!IS_ERR_OR_NULL(chip->wired_subs))
		oplus_mms_unsubscribe(chip->wired_subs);
	oplus_vooc_awake_exit(chip);
	remove_proc_entry("fastchg_fw_update", NULL);
	if (chip->general_strategy)
		oplus_chg_strategy_release(chip->general_strategy);
	destroy_votable(chip->pd_svooc_votable);
	destroy_votable(chip->vooc_not_allow_votable);
	destroy_votable(chip->vooc_disable_votable);
	destroy_votable(chip->vooc_curr_votable);
	destroy_votable(chip->vooc_chg_auto_mode_votable);
	if (chip->config.strategy_data)
		devm_kfree(&pdev->dev, chip->config.strategy_data);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id oplus_vooc_match[] = {
	{ .compatible = "oplus,vooc" },
	{},
};

static struct platform_driver oplus_vooc_driver = {
	.driver		= {
		.name = "oplus-vooc",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_vooc_match),
	},
	.probe		= oplus_vooc_probe,
	.remove		= oplus_vooc_remove,
};

static __init int oplus_vooc_init(void)
{
	return platform_driver_register(&oplus_vooc_driver);
}

static __exit void oplus_vooc_exit(void)
{
	platform_driver_unregister(&oplus_vooc_driver);
}

oplus_chg_module_register(oplus_vooc);

/* vooc API */
uint32_t oplus_vooc_get_project(struct oplus_mms *topic)
{
	struct oplus_chg_vooc *chip;

	if (topic == NULL)
		return 0;
	chip = oplus_mms_get_drvdata(topic);

	return chip->config.vooc_project;
}

uint32_t oplus_vooc_get_voocphy_support(struct oplus_mms *topic)
{
	struct oplus_chg_vooc *chip;

	if (topic == NULL)
		return 0;
	chip = oplus_mms_get_drvdata(topic);

	return chip->config.voocphy_support;
}

/*TODO*/
void oplus_api_switch_normal_chg(struct oplus_mms *topic)
{
	struct oplus_chg_vooc *chip;

	if (topic == NULL)
		return;
	chip = oplus_mms_get_drvdata(topic);

	switch_normal_chg(chip->vooc_ic);
}

int oplus_api_vooc_set_reset_sleep(struct oplus_mms *topic)
{
	struct oplus_chg_vooc *chip;

	if (topic == NULL)
		return 0;
	chip = oplus_mms_get_drvdata(topic);

	oplus_vooc_set_reset_sleep(chip->vooc_ic);
	return 0;
}

void oplus_api_vooc_turn_off_fastchg(struct oplus_mms *topic)
{
	struct oplus_chg_vooc *chip;

	if (topic == NULL)
		return;

	chip = oplus_mms_get_drvdata(topic);

	oplus_turn_off_fastchg(chip);
	return;
}

static bool oplus_vooc_get_bcc_support(struct oplus_chg_vooc *chip)
{
	if (!chip) {
		return false;
	}
	return chip->smart_chg_bcc_support;
}

static int oplus_vooc_choose_bcc_fastchg_curve(struct oplus_chg_vooc *chip)
{
	int batt_soc_plugin = FAST_SOC_0_TO_50;
	int batt_temp_plugin = FAST_TEMP_200_TO_350;
	int idx;
	int i;

	if (chip == NULL) {
		chg_err("vooc chip is NULL,return!\n");
		return -EINVAL;
	}

	if (chip->bcc_temp_range == BCC_TEMP_RANGE_LITTLE_COLD) {
		batt_temp_plugin = FAST_TEMP_0_TO_50;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_COOL) {
		batt_temp_plugin = FAST_TEMP_50_TO_120;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_LITTLE_COOL) {
		batt_temp_plugin = FAST_TEMP_120_TO_200;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_NORMAL_LOW) {
		batt_temp_plugin = FAST_TEMP_200_TO_350;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_NORMAL_HIGH) {
		batt_temp_plugin = FAST_TEMP_350_TO_430;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_WARM) {
		batt_temp_plugin = FAST_TEMP_430_TO_530;
	} else {
		batt_temp_plugin = FAST_TEMP_200_TO_350;
	}

	if (chip->bcc_soc_range == BCC_SOC_0_TO_50) {
		batt_soc_plugin = FAST_SOC_0_TO_50;
	} else if (chip->bcc_soc_range == BCC_SOC_50_TO_75) {
		batt_soc_plugin = FAST_SOC_50_TO_75;
	} else if (chip->bcc_soc_range == BCC_SOC_75_TO_85) {
		batt_soc_plugin = FAST_SOC_75_TO_85;
	} else if (chip->bcc_soc_range == BCC_SOC_85_TO_90) {
		batt_soc_plugin = FAST_SOC_85_TO_90;
	}

	if (batt_soc_plugin == FAST_SOC_0_TO_50) {
		chg_err("soc is 0~50!\n");
		for (i = 0; i < BATT_BCC_ROW_MAX; i++) {
			svooc_curves_target_soc_curve[i].bcc_curv_num =
				bcc_curves_soc0_2_50[i].bcc_curv_num;
			memcpy((&svooc_curves_target_soc_curve[i])
				       ->batt_bcc_curve,
			       (&bcc_curves_soc0_2_50[i])->batt_bcc_curve,
			       sizeof(struct batt_bcc_curve) *
				       (bcc_curves_soc0_2_50[i].bcc_curv_num));
		}
	} else if (batt_soc_plugin == FAST_SOC_50_TO_75) {
		chg_err("soc is 50~75!\n");
		for (i = 0; i < BATT_BCC_ROW_MAX; i++) {
			svooc_curves_target_soc_curve[i].bcc_curv_num =
				bcc_curves_soc50_2_75[i].bcc_curv_num;
			memcpy((&svooc_curves_target_soc_curve[i])
				       ->batt_bcc_curve,
			       (&bcc_curves_soc50_2_75[i])->batt_bcc_curve,
			       sizeof(struct batt_bcc_curve) *
				       (bcc_curves_soc50_2_75[i].bcc_curv_num));
		}
	} else if (batt_soc_plugin == FAST_SOC_75_TO_85) {
		chg_err("soc is 75~85!\n");
		for (i = 0; i < BATT_BCC_ROW_MAX; i++) {
			svooc_curves_target_soc_curve[i].bcc_curv_num =
				bcc_curves_soc75_2_85[i].bcc_curv_num;
			memcpy((&svooc_curves_target_soc_curve[i])
				       ->batt_bcc_curve,
			       (&bcc_curves_soc75_2_85[i])->batt_bcc_curve,
			       sizeof(struct batt_bcc_curve) *
				       (bcc_curves_soc75_2_85[i].bcc_curv_num));
		}
	} else if (batt_soc_plugin == FAST_SOC_85_TO_90) {
		chg_err("soc is 85~90!\n");
		for (i = 0; i < BATT_BCC_ROW_MAX; i++) {
			svooc_curves_target_soc_curve[i].bcc_curv_num =
				bcc_curves_soc85_2_90[i].bcc_curv_num;
			memcpy((&svooc_curves_target_soc_curve[i])
				       ->batt_bcc_curve,
			       (&bcc_curves_soc85_2_90[i])->batt_bcc_curve,
			       sizeof(struct batt_bcc_curve) *
				       (bcc_curves_soc85_2_90[i].bcc_curv_num));
		}
	}

	switch (batt_temp_plugin) {
	case FAST_TEMP_0_TO_50:
		chg_err("bcc get curve, temp is 0-5!\n");
		svooc_curves_target_curve[0].bcc_curv_num =
			svooc_curves_target_soc_curve
				[BATT_BCC_CURVE_TEMP_LITTLE_COLD]
					.bcc_curv_num;
		memcpy((&svooc_curves_target_curve[0])->batt_bcc_curve,
		       (&(svooc_curves_target_soc_curve
				  [BATT_BCC_CURVE_TEMP_LITTLE_COLD]))
			       ->batt_bcc_curve,
		       sizeof(struct batt_bcc_curve) *
			       (svooc_curves_target_soc_curve
					[BATT_BCC_CURVE_TEMP_LITTLE_COLD]
						.bcc_curv_num));
		break;
	case FAST_TEMP_50_TO_120:
		chg_err("bcc get curve, temp is 5-12!\n");
		svooc_curves_target_curve[0].bcc_curv_num =
			svooc_curves_target_soc_curve[BATT_BCC_CURVE_TEMP_COOL]
				.bcc_curv_num;
		memcpy((&svooc_curves_target_curve[0])->batt_bcc_curve,
		       (&(svooc_curves_target_soc_curve
				  [BATT_BCC_CURVE_TEMP_COOL]))
			       ->batt_bcc_curve,
		       sizeof(struct batt_bcc_curve) *
			       (svooc_curves_target_soc_curve
					[BATT_BCC_CURVE_TEMP_COOL]
						.bcc_curv_num));
		break;
	case FAST_TEMP_120_TO_200:
		chg_err("bcc get curve, temp is 12-20!\n");
		svooc_curves_target_curve[0].bcc_curv_num =
			svooc_curves_target_soc_curve
				[BATT_BCC_CURVE_TEMP_LITTLE_COOL]
					.bcc_curv_num;
		memcpy((&svooc_curves_target_curve[0])->batt_bcc_curve,
		       (&(svooc_curves_target_soc_curve
				  [BATT_BCC_CURVE_TEMP_LITTLE_COOL]))
			       ->batt_bcc_curve,
		       sizeof(struct batt_bcc_curve) *
			       (svooc_curves_target_soc_curve
					[BATT_BCC_CURVE_TEMP_LITTLE_COOL]
						.bcc_curv_num));
		break;
	case FAST_TEMP_200_TO_350:
		chg_err("bcc get curve, temp is 20-35!\n");
		svooc_curves_target_curve[0].bcc_curv_num =
			svooc_curves_target_soc_curve
				[BATT_BCC_CURVE_TEMP_NORMAL_LOW]
					.bcc_curv_num;
		memcpy((&svooc_curves_target_curve[0])->batt_bcc_curve,
		       (&(svooc_curves_target_soc_curve
				  [BATT_BCC_CURVE_TEMP_NORMAL_LOW]))
			       ->batt_bcc_curve,
		       sizeof(struct batt_bcc_curve) *
			       (svooc_curves_target_soc_curve
					[BATT_BCC_CURVE_TEMP_NORMAL_LOW]
						.bcc_curv_num));
		break;
	case FAST_TEMP_350_TO_430:
		chg_err("bcc get curve, temp is 35-43!\n");
		svooc_curves_target_curve[0].bcc_curv_num =
			svooc_curves_target_soc_curve
				[BATT_BCC_CURVE_TEMP_NORMAL_HIGH]
					.bcc_curv_num;
		memcpy((&svooc_curves_target_curve[0])->batt_bcc_curve,
		       (&(svooc_curves_target_soc_curve
				  [BATT_BCC_CURVE_TEMP_NORMAL_HIGH]))
			       ->batt_bcc_curve,
		       sizeof(struct batt_bcc_curve) *
			       (svooc_curves_target_soc_curve
					[BATT_BCC_CURVE_TEMP_NORMAL_HIGH]
						.bcc_curv_num));
		break;
	case FAST_TEMP_430_TO_530:
		if (batt_soc_plugin == FAST_SOC_0_TO_50) {
			chg_err("soc is 0-50 bcc get curve, temp is 43-53!\n");
			svooc_curves_target_curve[0].bcc_curv_num =
				svooc_curves_target_soc_curve
					[BATT_BCC_CURVE_TEMP_WARM]
						.bcc_curv_num;
			memcpy((&svooc_curves_target_curve[0])->batt_bcc_curve,
			       (&(svooc_curves_target_soc_curve
					  [BATT_BCC_CURVE_TEMP_WARM]))
				       ->batt_bcc_curve,
			       sizeof(struct batt_bcc_curve) *
				       (svooc_curves_target_soc_curve
						[BATT_BCC_CURVE_TEMP_WARM]
							.bcc_curv_num));
		}
		break;
	default:
		break;
	}
	for (idx = 0; idx < svooc_curves_target_curve[0].bcc_curv_num; idx++) {
		chip->bcc_target_vbat = svooc_curves_target_curve[0]
						.batt_bcc_curve[idx]
						.target_volt;
		chip->bcc_curve_max_current = svooc_curves_target_curve[0]
						      .batt_bcc_curve[idx]
						      .max_ibus;
		chip->bcc_curve_min_current = svooc_curves_target_curve[0]
						      .batt_bcc_curve[idx]
						      .min_ibus;
		chip->bcc_exit_curve =
			svooc_curves_target_curve[0].batt_bcc_curve[idx].exit;

		chg_err("bcc para idx:%d, vbat:%d, max_ibus:%d, min_ibus:%d, exit:%d",
			idx, chip->bcc_target_vbat, chip->bcc_curve_max_current,
			chip->bcc_curve_min_current, chip->bcc_exit_curve);
	}

	chip->svooc_batt_curve[0].bcc_curv_num =
		svooc_curves_target_curve[0].bcc_curv_num;
	memcpy((&(chip->svooc_batt_curve[0]))->batt_bcc_curve,
	       (&(svooc_curves_target_curve[0]))->batt_bcc_curve,
	       sizeof(struct batt_bcc_curve) *
		       (svooc_curves_target_curve[0].bcc_curv_num));

	for (idx = 0; idx < chip->svooc_batt_curve[0].bcc_curv_num; idx++) {
		chg_err("chip svooc bcc para idx:%d vbat:%d, max_ibus:%d, min_ibus:%d, exit:%d curve num:%d\n",
			idx,
			chip->svooc_batt_curve[0]
				.batt_bcc_curve[idx]
				.target_volt,
			chip->svooc_batt_curve[0].batt_bcc_curve[idx].max_ibus,
			chip->svooc_batt_curve[0].batt_bcc_curve[idx].min_ibus,
			chip->svooc_batt_curve[0].batt_bcc_curve[idx].exit,
			chip->svooc_batt_curve[0].bcc_curv_num);
	}

	return 0;
}

static int oplus_chg_bcc_get_stop_curr(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_spec_config *spec = &chip->spec;
	int batt_soc_plugin = FAST_SOC_0_TO_50;
	int batt_temp_plugin = FAST_TEMP_200_TO_350;
	int svooc_stop_curr = 1000;

	if (chip == NULL) {
		chg_err("vooc chip is NULL,return!\n");
		return 1000;
	}

	if (chip->bcc_temp_range == BCC_TEMP_RANGE_LITTLE_COLD) {
		batt_temp_plugin = FAST_TEMP_0_TO_50;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_COOL) {
		batt_temp_plugin = FAST_TEMP_50_TO_120;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_LITTLE_COOL) {
		batt_temp_plugin = FAST_TEMP_120_TO_200;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_NORMAL_LOW) {
		batt_temp_plugin = FAST_TEMP_200_TO_350;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_NORMAL_HIGH) {
		batt_temp_plugin = FAST_TEMP_350_TO_430;
	} else if (chip->bcc_temp_range == BCC_TEMP_RANGE_WARM) {
		batt_temp_plugin = FAST_TEMP_430_TO_530;
	} else {
		batt_temp_plugin = FAST_TEMP_200_TO_350;
	}

	if (chip->bcc_soc_range == BCC_SOC_0_TO_50) {
		batt_soc_plugin = FAST_SOC_0_TO_50;
	} else if (chip->bcc_soc_range == BCC_SOC_50_TO_75) {
		batt_soc_plugin = FAST_SOC_50_TO_75;
	} else if (chip->bcc_soc_range == BCC_SOC_75_TO_85) {
		batt_soc_plugin = FAST_SOC_75_TO_85;
	} else if (chip->bcc_soc_range == BCC_SOC_85_TO_90) {
		batt_soc_plugin = FAST_SOC_85_TO_90;
	}

	if (batt_soc_plugin == FAST_SOC_0_TO_50) {
		chg_err("get stop curr, enter soc is 0-50\n");
		switch (batt_temp_plugin) {
		case FAST_TEMP_0_TO_50:
			chg_err("bcc get stop curr, temp is 0-5!\n");
			svooc_stop_curr = spec->bcc_stop_curr_0_to_50
						  [BCC_TEMP_RANGE_LITTLE_COLD];
			break;
		case FAST_TEMP_50_TO_120:
			chg_err("bcc get stop curr, temp is 5-12!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_0_to_50[BCC_TEMP_RANGE_COOL];
			break;
		case FAST_TEMP_120_TO_200:
			chg_err("bcc get stop curr, temp is 12-20!\n");
			svooc_stop_curr = spec->bcc_stop_curr_0_to_50
						  [BCC_TEMP_RANGE_LITTLE_COOL];
			break;
		case FAST_TEMP_200_TO_350:
			chg_err("bcc get stop curr, temp is 20-35!\n");
			svooc_stop_curr = spec->bcc_stop_curr_0_to_50
						  [BCC_TEMP_RANGE_NORMAL_LOW];
			break;
		case FAST_TEMP_350_TO_430:
			chg_err("bcc get stop curr, temp is 35-43!\n");
			svooc_stop_curr = spec->bcc_stop_curr_0_to_50
						  [BCC_TEMP_RANGE_NORMAL_HIGH];
			break;
		case FAST_TEMP_430_TO_530:
			chg_err("bcc get stop curr, temp is 43-53!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_0_to_50[BCC_TEMP_RANGE_WARM];
			break;
		default:
			break;
		}
	}

	if (batt_soc_plugin == FAST_SOC_50_TO_75) {
		chg_err("get stop curr, enter soc is 51-75\n");
		switch (batt_temp_plugin) {
		case FAST_TEMP_0_TO_50:
			chg_err("bcc get stop curr, temp is 0-5!\n");
			svooc_stop_curr = spec->bcc_stop_curr_51_to_75
						  [BCC_TEMP_RANGE_LITTLE_COLD];
			break;
		case FAST_TEMP_50_TO_120:
			chg_err("bcc get stop curr, temp is 5-12!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_51_to_75[BCC_TEMP_RANGE_COOL];
			break;
		case FAST_TEMP_120_TO_200:
			chg_err("bcc get stop curr, temp is 12-20!\n");
			svooc_stop_curr = spec->bcc_stop_curr_51_to_75
						  [BCC_TEMP_RANGE_LITTLE_COOL];
			break;
		case FAST_TEMP_200_TO_350:
			chg_err("bcc get stop curr, temp is 20-35!\n");
			svooc_stop_curr = spec->bcc_stop_curr_51_to_75
						  [BCC_TEMP_RANGE_NORMAL_LOW];
			break;
		case FAST_TEMP_350_TO_430:
			chg_err("bcc get stop curr, temp is 35-43!\n");
			svooc_stop_curr = spec->bcc_stop_curr_51_to_75
						  [BCC_TEMP_RANGE_NORMAL_HIGH];
			break;
		case FAST_TEMP_430_TO_530:
			chg_err("bcc get stop curr, temp is 43-53!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_51_to_75[BCC_TEMP_RANGE_WARM];
			break;
		default:
			break;
		}
	}

	if (batt_soc_plugin == FAST_SOC_75_TO_85) {
		chg_err("get stop curr, enter soc is 76-85\n");
		switch (batt_temp_plugin) {
		case FAST_TEMP_0_TO_50:
			chg_err("bcc get stop curr, temp is 0-5!\n");
			svooc_stop_curr = spec->bcc_stop_curr_76_to_85
						  [BCC_TEMP_RANGE_LITTLE_COLD];
			break;
		case FAST_TEMP_50_TO_120:
			chg_err("bcc get stop curr, temp is 5-12!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_76_to_85[BCC_TEMP_RANGE_COOL];
			break;
		case FAST_TEMP_120_TO_200:
			chg_err("bcc get stop curr, temp is 12-20!\n");
			svooc_stop_curr = spec->bcc_stop_curr_76_to_85
						  [BCC_TEMP_RANGE_LITTLE_COOL];
			break;
		case FAST_TEMP_200_TO_350:
			chg_err("bcc get stop curr, temp is 20-35!\n");
			svooc_stop_curr = spec->bcc_stop_curr_76_to_85
						  [BCC_TEMP_RANGE_NORMAL_LOW];
			break;
		case FAST_TEMP_350_TO_430:
			chg_err("bcc get stop curr, temp is 35-43!\n");
			svooc_stop_curr = spec->bcc_stop_curr_76_to_85
						  [BCC_TEMP_RANGE_NORMAL_HIGH];
			break;
		case FAST_TEMP_430_TO_530:
			chg_err("bcc get stop curr, temp is 43-53!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_76_to_85[BCC_TEMP_RANGE_WARM];
			break;
		default:
			break;
		}
	}

	if (batt_soc_plugin == FAST_SOC_85_TO_90) {
		chg_err("get stop curr, enter soc is 86-90\n");
		switch (batt_temp_plugin) {
		case FAST_TEMP_0_TO_50:
			chg_err("bcc get stop curr, temp is 0-5!\n");
			svooc_stop_curr = spec->bcc_stop_curr_86_to_90
						  [BCC_TEMP_RANGE_LITTLE_COLD];
			break;
		case FAST_TEMP_50_TO_120:
			chg_err("bcc get stop curr, temp is 5-12!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_86_to_90[BCC_TEMP_RANGE_COOL];
			break;
		case FAST_TEMP_120_TO_200:
			chg_err("bcc get stop curr, temp is 12-20!\n");
			svooc_stop_curr = spec->bcc_stop_curr_86_to_90
						  [BCC_TEMP_RANGE_LITTLE_COOL];
			break;
		case FAST_TEMP_200_TO_350:
			chg_err("bcc get stop curr, temp is 20-35!\n");
			svooc_stop_curr = spec->bcc_stop_curr_86_to_90
						  [BCC_TEMP_RANGE_NORMAL_LOW];
			break;
		case FAST_TEMP_350_TO_430:
			chg_err("bcc get stop curr, temp is 35-43!\n");
			svooc_stop_curr = spec->bcc_stop_curr_86_to_90
						  [BCC_TEMP_RANGE_NORMAL_HIGH];
			break;
		case FAST_TEMP_430_TO_530:
			chg_err("bcc get stop curr, temp is 43-53!\n");
			svooc_stop_curr =
				spec->bcc_stop_curr_86_to_90[BCC_TEMP_RANGE_WARM];
			break;
		default:
			break;
		}
	}

	chg_err("get stop curr is %d!\n", svooc_stop_curr);

	return svooc_stop_curr;
}

static int oplus_vooc_get_bcc_max_curr(struct oplus_mms *topic,
				       union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	int bcc_max_curr = 0;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		goto end;
	}
	chip = oplus_mms_get_drvdata(topic);

	bcc_max_curr = chip->bcc_max_curr;

end:
	if (data != NULL)
		data->intval = bcc_max_curr;
	return 0;
}

static int oplus_vooc_get_bcc_min_curr(struct oplus_mms *topic,
				       union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	int bcc_min_curr = 0;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		goto end;
	}
	chip = oplus_mms_get_drvdata(topic);

	bcc_min_curr = chip->bcc_min_curr;

end:
	if (data != NULL)
		data->intval = bcc_min_curr;
	return 0;
}

static int oplus_vooc_get_bcc_stop_curr(struct oplus_mms *topic,
					union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	int bcc_stop_curr = 0;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		goto end;
	}
	chip = oplus_mms_get_drvdata(topic);

	chip->bcc_exit_curr = oplus_chg_bcc_get_stop_curr(chip);
	bcc_stop_curr = chip->bcc_exit_curr;

end:
	if (data != NULL)
		data->intval = bcc_stop_curr;
	return 0;
}

#define BCC_TEMP_RANGE_OK 1
#define BCC_TEMP_RANGE_WRONG 0
static int oplus_vooc_get_bcc_temp_range(struct oplus_mms *topic,
					 union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	int bcc_temp_range = 0;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		goto end;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (chip->bcc_temp_range == BCC_TEMP_RANGE_NORMAL_LOW ||
	    chip->bcc_temp_range == BCC_TEMP_RANGE_NORMAL_HIGH) {
		bcc_temp_range = BCC_TEMP_RANGE_OK;
	} else {
		bcc_temp_range = BCC_TEMP_RANGE_WRONG;
	}
end:
	if (data != NULL)
		data->intval = bcc_temp_range;
	return 0;
}

#define OPLUS_BCC_MAX_CURR_INIT 73
#define OPLUS_BCC_MIN_CURR_INIT 63
#define OPLUS_BCC_CURRENT_TIMES 100
#define OPLUS_BCC_EXIT_CURR_INIT 1000
#define OPLUS_BCC_OFFSET 200
static void oplus_vooc_bcc_parms_init(struct oplus_chg_vooc *chip)
{
	int bcc_current = 0;

	if (!chip)
		return;

	chip->bcc_wake_up_done = false;
	chip->bcc_choose_curve_done = false;
	chip->bcc_max_curr = OPLUS_BCC_MAX_CURR_INIT;
	chip->bcc_min_curr = OPLUS_BCC_MIN_CURR_INIT;
	chip->bcc_exit_curr = OPLUS_BCC_EXIT_CURR_INIT;
	chip->bcc_curve_idx = 0;
	chip->bcc_true_idx = 0;
	/* set 7500mA as default */
	bcc_current =
		chip->bcc_max_curr * OPLUS_BCC_CURRENT_TIMES + OPLUS_BCC_OFFSET;
	vote(chip->vooc_curr_votable, BCC_VOTER, true, bcc_current, false);
}

#define BCC_TYPE_IS_SVOOC 1
#define BCC_TYPE_IS_VOOC 0
static int oplus_vooc_get_svooc_type(struct oplus_mms *topic,
				     union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	int bcc_type = BCC_TYPE_IS_SVOOC;

	if (topic == NULL) {
		chg_err("topic is NULL");
		goto end;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		goto end;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (chip->config.support_vooc_by_normal_charger_path &&
	    sid_to_adapter_chg_type(chip->sid) == CHARGER_TYPE_VOOC)
		bcc_type = BCC_TYPE_IS_VOOC;
	else
		bcc_type = BCC_TYPE_IS_SVOOC;

end:
	if (data != NULL)
		data->intval = bcc_type;
	return 0;
}

static int oplus_vooc_get_ffc_status(void)
{
	int ffc_status = 0;
	struct oplus_mms *comm_topic;
	union mms_msg_data data = { 0 };
	int rc;

	comm_topic = oplus_mms_get_by_name("common");
	if (!comm_topic)
		return 0;

	rc = oplus_mms_get_item_data(comm_topic, COMM_ITEM_FFC_STATUS, &data,
				     true);
	if (!rc)
		ffc_status = data.intval;

	return ffc_status;
}

static int oplus_vooc_get_batt_full_status(void)
{
	int batt_full = 0;
	struct oplus_mms *comm_topic;
	union mms_msg_data data = { 0 };
	int rc;

	comm_topic = oplus_mms_get_by_name("common");
	if (!comm_topic)
		return 0;

	rc = oplus_mms_get_item_data(comm_topic, COMM_ITEM_CHG_FULL, &data,
				     true);
	if (!rc)
		batt_full = data.intval;

	chg_err("%s, get batt_full status = %d\n", __func__, batt_full);
	return batt_full;
}

static int oplus_vooc_get_wired_type(void)
{
	int wired_type = 0;
	struct oplus_mms *wired_topic;
	union mms_msg_data data = { 0 };
	int rc;

	wired_topic = oplus_mms_get_by_name("wired");
	if (!wired_topic)
		return 0;

	rc = oplus_mms_get_item_data(wired_topic, WIRED_ITEM_CHG_TYPE, &data,
				     true);
	if (!rc)
		wired_type = data.intval;

	chg_err("%s, get wired type = %d\n", __func__, wired_type);
	return wired_type;
}

static bool oplus_check_afi_update_condition(struct oplus_chg_vooc *chip)
{
	struct oplus_vooc_config *config = &chip->config;

	if (!chip) {
		chg_err("chip is null!\n");
		return false;
	}
	if (chip->wired_online) {
		if (oplus_vooc_get_wired_type() == OPLUS_CHG_USB_TYPE_UNKNOWN) {
			return false;
		}

		if (config->support_vooc_by_normal_charger_path &&
		    sid_to_adapter_chg_type(chip->sid) == CHARGER_TYPE_VOOC) {
			chg_err(" true 1: vooc\n");
			return true;
		} else {
			if (sid_to_adapter_chg_type(chip->sid) ==
			    CHARGER_TYPE_SVOOC) {
				if ((chip->fast_chg_status ==
				     CHARGER_STATUS_FAST_TO_NORMAL) ||
				    (chip->fast_chg_status ==
				     CHARGER_STATUS_FAST_TO_WARM) ||
				    (chip->fast_chg_status ==
				     CHARGER_STATUS_FAST_DUMMY)) {
					if (oplus_vooc_get_ffc_status() ==
						    FFC_WAIT ||
					    oplus_vooc_get_ffc_status() ==
						    FFC_FAST ||
					    oplus_vooc_get_batt_full_status()) {
						chg_err(" true 2: svooc\n");
						return true;
					} else {
						return false;
					}
				}
				return false;
			} else {
				chg_err(" true 3: normal charger or others unkown\n");
				return true;
			}
		}
	}
	return false;
}

static int oplus_vooc_afi_update_condition(struct oplus_mms *topic,
					   union mms_msg_data *data)
{
	struct oplus_chg_vooc *chip;
	int afi_condition = 0;

	if (topic == NULL) {
		chg_err("topic is NULL");
		return 0;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return 0;
	}
	chip = oplus_mms_get_drvdata(topic);

	afi_condition = oplus_check_afi_update_condition(chip);

	data->intval = afi_condition;
	return 0;
}

int oplus_vooc_current_to_level(struct oplus_mms *topic, int curr)
{
	struct oplus_chg_vooc *chip;
	int level = 0;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return -ENODEV;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (curr == 0)
		return level;

	/* TODO: add adsp voocphy check */
	if (chip->config.data_width == 7)
		level = find_level_to_current(curr, svooc_2_0_curr_table, ARRAY_SIZE(svooc_2_0_curr_table));
	else
		level = curr / 1000;

	return level;
}

int oplus_vooc_level_to_current(struct oplus_mms *topic, int level)
{
	struct oplus_chg_vooc *chip;
	int curr = 0;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return -ENODEV;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (level == 0)
		return 0;

	/* TODO: add adsp voocphy check */
	if (chip->config.data_width == 7)
		curr = find_current_to_level(level, svooc_2_0_curr_table, ARRAY_SIZE(svooc_2_0_curr_table));

	return curr;
}

int oplus_vooc_get_batt_curve_current(struct oplus_mms *topic)
{
	struct oplus_chg_vooc *chip;
	int curr = 0;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return -ENODEV;
	}
	chip = oplus_mms_get_drvdata(topic);

	/* TODO: add voocphy curve_current support */
	curr = -chip->icharging;

	return curr;
}
