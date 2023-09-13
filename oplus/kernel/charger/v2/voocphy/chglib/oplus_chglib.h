#ifndef __OPLUS_CHGLIB_H__
#define __OPLUS_CHGLIB_H__

#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_monitor.h>
#include <linux/power_supply.h>

#define VOOC_ADAPTER_ID	0
#define FAST_NOTIFY_UNKNOW                      0x4a
#define FAST_NOTIFY_DUMMY_START                 0x50
#define FAST_NOTIFY_SWITCH_TEMP_RANGE           0x4b
#define FAST_NOTIFY_ADAPTER_COPYCAT             0x4c
#define FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL     0x51
#define FAST_NOTIFY_PRESENT             0x52
#define FAST_NOTIFY_LOW_TEMP_FULL	0x53
#define FAST_NOTIFY_ABSENT              0x54
#define FAST_NOTIFY_ERR_COMMU           0x55
#define FAST_NOTIFY_ONGOING             0x58
#define FAST_NOTIFY_BAD_CONNECTED       0x59
#define FAST_NOTIFY_FULL                0x5a
#define FAST_NOTIFY_BATT_TEMP_OVER      0x5c
#define FAST_NOTIFY_BTB_TEMP_OVER       0x5d
#define FAST_NOTIFY_ADAPTER_MODEL_FACTORY 0x5e
#define FAST_NOTIFY_USER_EXIT_FASTCHG   0x5f
#define FAST_NOTIFY_CHECK_FASTCHG_REAL_ALLOW 0x61

enum {
	FASTCHG_CHARGER_TYPE_UNKOWN,
	FASTCHG_CHARGER_TYPE_VOOC,
};

struct hw_vphy_info {
	/*other info*/
	void (*vphy_set_screenoff_current)(struct device *dev, int curr_ma);
	void (*vphy_set_cool_down)(struct device *dev, int cool_down);
	void (*vphy_set_chg_auto_mode)(struct device *dev, bool enable);
	int (*vphy_get_data)(struct device *dev, bool *data, int read_count);
	int (*vphy_get_fastchg_type)(struct device *dev);
	int (*vphy_get_fastchg_notify_status)(struct device *dev);
	int (*vphy_get_cp_vbat)(struct device *dev);
	void (*vphy_switch_chg_mode)(struct device *dev, int mode);
	void (*vphy_set_pdqc_config)(struct device *dev);
	void (*vphy_disconnect_detect)(struct device *dev);
	void (*vphy_clear_status)(struct device *dev);
};

struct vphy_chip {
	struct device *dev;
	struct oplus_mms *wired_topic;
	struct mms_subscribe *wired_subs;
	struct oplus_mms *gauge_topic;
	struct mms_subscribe *gauge_subs;
	struct oplus_mms *common_topic;
	struct mms_subscribe *common_subs;
	struct oplus_mms *vooc_topic;
	struct mms_subscribe *vooc_subs;
	struct oplus_chg_ic_dev *ic_dev;
	struct hw_vphy_info *vinf;
	struct work_struct check_charger_out_work;
	struct work_struct set_current_work;
	struct work_struct send_adapter_id_work;
	struct work_struct send_absent_notify_work;
	bool led_on;
	bool is_pd_svooc_adapter;
	int cool_down;
	int fastchg_notify_event;
};

extern void oplus_chg_adc_switch_ctrl(void);

struct vphy_chip *oplus_chglib_get_vphy_chip(struct device *dev);
int oplus_chglib_disable_charger(bool disable);
int oplus_chglib_suspend_charger(bool suspend);
int oplus_chglib_vooc_fastchg_disable(const char *client_str, bool disable);
int oplus_chglib_gauge_vbatt(struct device *dev);
int oplus_chglib_gauge_pre_vbatt(struct device *dev);
int oplus_chglib_gauge_sub_vbatt(void);
int oplus_chglib_gauge_current(struct device *dev);
int oplus_chglib_gauge_sub_current(void);
int oplus_chglib_get_soc(struct device *dev);
int oplus_chglib_get_shell_temp(struct device *dev);
bool oplus_chglib_get_led_on(struct device *dev);
bool oplus_chglib_is_pd_svooc_adapter(struct device *dev);
bool oplus_chglib_is_switch_temp_range(void);
int oplus_chglib_get_battery_btb_temp_cal(void);
int oplus_chglib_get_usb_btb_temp_cal(void);
bool oplus_chglib_get_chg_stats(void);
bool oplus_chglib_get_flash_led_status(void);
int oplus_chglib_get_charger_voltage(void);
bool oplus_chglib_get_vooc_is_started(struct device *dev);
bool oplus_chglib_get_vooc_sid_is_config(struct device *dev);
int oplus_chglib_notify_ap(struct device *dev, int event);
int oplus_chglib_push_break_code(struct device *dev, int code);
struct vphy_chip *oplus_chglib_register_vphy(struct device *dev,
					     struct hw_vphy_info *vinf);

#endif /*__OPLUS_CHGLIB_H__*/
