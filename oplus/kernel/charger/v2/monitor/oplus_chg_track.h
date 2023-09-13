#ifndef __OPLUS_CHG_TRACK_H__
#define __OPLUS_CHG_TRACK_H__

#include <linux/version.h>
#include <linux/of.h>

#define OPLUS_CHG_TRACK_CURX_INFO_LEN 1536
#define TRACK_CHG_GET_THTS_TIME_TYPE 0
#define TRACK_CHG_GET_LAST_TIME_TYPE 1

struct oplus_chg_track;
struct oplus_monitor;

enum oplus_chg_track_cmd_error {
	TRACK_CMD_ACK_OK,
	TRACK_CMD_ERROR_CHIP_NULL = 1,
	TRACK_CMD_ERROR_DATA_NULL,
	TRACK_CMD_ERROR_DATA_INVALID,
	TRACK_CMD_ERROR_TIME_OUT,
};

enum oplus_chg_track_info_type {
	TRACK_NOTIFY_TYPE_DEFAULT,
	TRACK_NOTIFY_TYPE_SOC_JUMP,
	TRACK_NOTIFY_TYPE_GENERAL_RECORD,
	TRACK_NOTIFY_TYPE_NO_CHARGING,
	TRACK_NOTIFY_TYPE_CHARGING_SLOW,
	TRACK_NOTIFY_TYPE_CHARGING_BREAK,
	TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL,
	TRACK_NOTIFY_TYPE_IC_ERROR,
	TRACK_NOTIFY_TYPE_MAX,
};

enum oplus_chg_track_info_flag {
	TRACK_NOTIFY_FLAG_DEFAULT,
	TRACK_NOTIFY_FLAG_UI_SOC_LOAD_JUMP,
	TRACK_NOTIFY_FLAG_SOC_JUMP,
	TRACK_NOTIFY_FLAG_UI_SOC_JUMP,
	TRACK_NOTIFY_FLAG_UI_SOC_TO_SOC_JUMP,

	TRACK_NOTIFY_FLAG_CHARGER_INFO,
	TRACK_NOTIFY_FLAG_UISOC_KEEP_1_T_INFO,
	TRACK_NOTIFY_FLAG_VBATT_TOO_LOW_INFO,
	TRACK_NOTIFY_FLAG_USBTEMP_INFO,
	TRACK_NOTIFY_FLAG_VBATT_DIFF_OVER_INFO,
	TRACK_NOTIFY_FLAG_WLS_TRX_INFO,

	TRACK_NOTIFY_FLAG_NO_CHARGING,

	TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_WARM,
	TRACK_NOTIFY_FLAG_CHG_SLOW_TBATT_COLD,
	TRACK_NOTIFY_FLAG_CHG_SLOW_NON_STANDARD_PA,
	TRACK_NOTIFY_FLAG_CHG_SLOW_BATT_CAP_HIGH,
	TRACK_NOTIFY_FLAG_CHG_SLOW_COOLDOWN,
	TRACK_NOTIFY_FLAG_CHG_SLOW_WLS_SKEW,
	TRACK_NOTIFY_FLAG_CHG_SLOW_VERITY_FAIL,
	TRACK_NOTIFY_FLAG_CHG_SLOW_OTHER,

	TRACK_NOTIFY_FLAG_FAST_CHARGING_BREAK,
	TRACK_NOTIFY_FLAG_GENERAL_CHARGING_BREAK,
	TRACK_NOTIFY_FLAG_WLS_CHARGING_BREAK,

	TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL,
	TRACK_NOTIFY_FLAG_GPIO_ABNORMAL,
	TRACK_NOTIFY_FLAG_CP_ABNORMAL,
	TRACK_NOTIFY_FLAG_PLAT_PMIC_ABNORMAL,
	TRACK_NOTIFY_FLAG_EXTERN_PMIC_ABNORMAL,
	TRACK_NOTIFY_FLAG_GAGUE_ABNORMAL,
	TRACK_NOTIFY_FLAG_DCHG_ABNORMAL,
	TRACK_NOTIFY_FLAG_MAX_CNT,
};

typedef struct {
	unsigned int type_reason;
	unsigned int flag_reason;
	unsigned char crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
} __attribute__ ((packed)) oplus_chg_track_trigger;

int oplus_chg_track_comm_monitor(struct oplus_monitor *monitor);
int oplus_chg_track_check_wired_charging_break(int vbus_rising);
int oplus_chg_track_set_fastchg_break_code(int fastchg_break_code);
int oplus_chg_track_driver_init(struct oplus_monitor *monitor);
int oplus_chg_track_driver_exit(struct oplus_monitor *monitor);
int oplus_chg_track_set_uisoc_1_start(struct oplus_monitor *monitor);
int oplus_chg_track_charge_full(struct oplus_monitor *monitor);
int oplus_chg_track_cal_tbatt_status(struct oplus_monitor *monitor);
int oplus_chg_track_handle_wired_type_info(struct oplus_monitor *monitor, int type);
int oplus_chg_track_aging_ffc_check(struct oplus_monitor *monitor, int step);

#endif /* __OPLUS_CHG_TRACK_H__ */
