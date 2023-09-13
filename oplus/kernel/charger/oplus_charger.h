/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_CHARGER_H_
#define _OPLUS_CHARGER_H_

#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/thermal.h>
#include "oplus_chg_core.h"
#if __and(IS_MODULE(CONFIG_OPLUS_CHG), IS_MODULE(CONFIG_OPLUS_CHG_V2))
#include "oplus_chg_symbol.h"
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/i2c.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#include <mt-plat/mtk_boot.h>
#else
#include <mt-plat/mtk_boot_common.h>
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6779
#include "charger_ic/oplus_battery_mtk6779.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6779Q
#include "charger_ic/oplus_battery_mtk6779Q.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6778
#include "charger_ic/oplus_battery_mtk6778R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6779R
#include "charger_ic/oplus_battery_mtk6779R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6768
#include "charger_ic/oplus_battery_mtk6768R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6769R
#include "charger_ic/oplus_battery_mtk6769R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6885
#include "charger_ic/oplus_battery_mtk6885R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6889
#include "charger_ic/oplus_battery_mtk6889R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6893
#include "charger_ic/oplus_battery_mtk6893R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6877
#include "charger_ic/oplus_battery_mtk6877R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6873
#include "charger_ic/oplus_battery_mtk6873R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6853
#include "charger_ic/oplus_battery_mtk6853R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6833
#include "charger_ic/oplus_battery_mtk6833R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6769
#include "charger_ic/oplus_battery_mtk6769.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6765S
#include "charger_ic/oplus_battery_mtk6765S.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6785
#include "charger_ic/oplus_battery_mtk6785R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6781
#include "charger_ic/oplus_battery_mtk6781R.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6983S
#include "charger_ic/oplus_battery_mtk6983S.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6895S
#include "charger_ic/oplus_battery_mtk6983S.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6789S
#include "charger_ic/oplus_battery_mtk6789S.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6985S
#include "charger_ic/oplus_battery_mtk6985S.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6895S
#include "charger_ic/oplus_battery_mtk6895S.h"
#endif
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/qpnp/qpnp-adc.h>
#include <linux/msm_bcl.h>
#endif
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/boot_mode.h>
#endif
#ifdef CONFIG_OPLUS_MSM8953N_CHARGER
#include "charger_ic/oplus_battery_msm8953_N.h"
#elif defined CONFIG_OPLUS_MSM8953_CHARGER
#include "charger_ic/oplus_battery_msm8953.h"
#elif defined CONFIG_OPLUS_MSM8998_CHARGER
#include "charger_ic/oplus_battery_msm8998.h"
#elif defined CONFIG_OPLUS_MSM8998O_CHARGER
#include "charger_ic/oplus_battery_msm8998_O.h"
#elif defined CONFIG_OPLUS_SDM845_CHARGER
#include "charger_ic/oplus_battery_sdm845.h"
#elif defined CONFIG_OPLUS_SDM670_CHARGER
#include "charger_ic/oplus_battery_sdm670.h"
#elif defined CONFIG_OPLUS_SDM670P_CHARGER
#include "charger_ic/oplus_battery_sdm670P.h"
#elif defined CONFIG_OPLUS_SM8150_CHARGER
#include "charger_ic/oplus_battery_msm8150.h"
#elif defined CONFIG_OPLUS_SM8250_CHARGER
#include "charger_ic/oplus_battery_msm8250.h"
#elif defined CONFIG_OPLUS_SM8150R_CHARGER
#include "charger_ic/oplus_battery_msm8150Q.h"
#elif defined CONFIG_OPLUS_SM8150_PRO_CHARGER
#include "charger_ic/oplus_battery_msm8150_pro.h"
#elif defined CONFIG_OPLUS_SM6125_CHARGER
#include "charger_ic/oplus_battery_sm6125P.h"
#elif defined CONFIG_OPLUS_SM7150_CHARGER
#include "charger_ic/oplus_battery_sm7150_P.h"
#elif defined CONFIG_OPLUS_SDM670Q_CHARGER
#include "charger_ic/oplus_battery_sdm670Q.h"
#elif defined CONFIG_OPLUS_SM7250_CHARGER
#include "charger_ic/oplus_battery_msm7250_Q.h"
#elif defined CONFIG_OPLUS_SM7250R_CHARGER
#include "charger_ic/oplus_battery_msm7250_R.h"
#elif defined CONFIG_OPLUS_SM8350_CHARGER
#include "charger_ic/oplus_battery_sm8350.h"
#elif defined CONFIG_OPLUS_SM8450_CHARGER
#include "charger_ic/oplus_battery_sm8450.h"
#define PLATFORM_SUPPORT_TIMESPEC 1
#elif defined CONFIG_OPLUS_SM8550_CHARGER
#include "charger_ic/oplus_battery_sm8550.h"
#elif defined CONFIG_OPLUS_SM6375R_CHARGER
#include "charger_ic/oplus_battery_sm6375.h"
#else /* CONFIG_OPLUS_MSM8953_CHARGER */
#include "charger_ic/oplus_battery_msm8976.h"
#endif /* CONFIG_OPLUS_MSM8953_CHARGER */
#endif /* CONFIG_OPLUS_CHARGER_MTK */

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#elif IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
#include <linux/msm_drm_notify.h>
#elif IS_ENABLED(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
#include "../test-kit/test-kit.h"
#endif

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
#include <linux/soc/qcom/panel_event_notifier.h>
#include <drm/drm_panel.h>
#endif
#include "oplus_chg_track.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#ifndef PLATFORM_SUPPORT_TIMESPEC
#include <uapi/linux/rtc.h>

#ifdef __KERNEL__
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
	__kernel_old_time_t tv_sec; /* seconds */
	long tv_nsec; /* nanoseconds */
};
#endif

struct timeval {
	__kernel_old_time_t tv_sec; /* seconds */
	__kernel_suseconds_t tv_usec; /* microseconds */
};

struct itimerspec {
	struct timespec it_interval; /* timer period */
	struct timespec it_value; /* timer expiration */
};

struct itimerval {
	struct timeval it_interval; /* timer interval */
	struct timeval it_value; /* current value */
};
#endif

extern time64_t rtc_tm_to_time64(struct rtc_time *tm);
extern void rtc_time64_to_tm(time64_t time, struct rtc_time *tm);

static inline void rtc_time_to_tm(unsigned long time, struct rtc_time *tm)
{
	rtc_time64_to_tm(time, tm);
}

static inline int rtc_tm_to_time(struct rtc_time *tm, unsigned long *time)
{
	*time = rtc_tm_to_time64(tm);

	return 0;
}

#if __BITS_PER_LONG == 64
/* timespec64 is defined as timespec here */
static inline struct timespec
timespec64_to_timespec(const struct timespec64 ts64)
{
	return *(const struct timespec *)&ts64;
}

static inline struct timespec64 timespec_to_timespec64(const struct timespec ts)
{
	return *(const struct timespec64 *)&ts;
}

#else
static inline struct timespec
timespec64_to_timespec(const struct timespec64 ts64)
{
	struct timespec ret;

	ret.tv_sec = (time_t)ts64.tv_sec;
	ret.tv_nsec = ts64.tv_nsec;
	return ret;
}

static inline struct timespec64 timespec_to_timespec64(const struct timespec ts)
{
	struct timespec64 ret;

	ret.tv_sec = ts.tv_sec;
	ret.tv_nsec = ts.tv_nsec;
	return ret;
}
#endif

static inline void getnstimeofday(struct timespec *ts)
{
	struct timespec64 ts64;

	ktime_get_real_ts64(&ts64);
	*ts = timespec64_to_timespec(ts64);
}
#endif /*PLATFORM_SUPPORT_TIMESPEC*/
#endif

#define CHG_LOG_CRTI 1
#define CHG_LOG_FULL 2

#define CPU_CHG_FREQ_STAT_UP	1
#define CPU_CHG_FREQ_STAT_AUTO	0

#define OPCHG_PWROFF_HIGH_BATT_TEMP		770
#define OPCHG_PWROFF_EMERGENCY_BATT_TEMP	850
#define FACTORY_PWROFF_HIGH_BATT_TEMP		650
#define FACTORY_PWROFF_EMERGENCY_BATT_TEMP	700

#define OPCHG_INPUT_CURRENT_LIMIT_CHARGER_MA	2000
#define OPCHG_INPUT_CURRENT_LIMIT_USB_MA	500
#define OPCHG_INPUT_CURRENT_LIMIT_CDP_MA	1500
#define OPCHG_INPUT_CURRENT_LIMIT_LED_MA	1200
#define OPCHG_INPUT_CURRENT_LIMIT_CAMERA_MA	1000
#define OPCHG_INPUT_CURRENT_LIMIT_CALLING_MA	1200
#define OPCHG_FAST_CHG_MAX_MA			2000

#define OPCHG_USBTEMP_BATT_TEMP_LOW 50
#define OPCHG_USBTEMP_BATT_TEMP_HIGH 50
#define OPCHG_USBTEMP_NTC_TEMP_LOW 57
#define OPCHG_USBTEMP_NTC_TEMP_HIGH 69
#define OPCHG_USBTEMP_GAP_LOW_WITH_BATT_TEMP 7
#define OPCHG_USBTEMP_GAP_HIGH_WITH_BATT_TEMP 12
#define OPCHG_USBTEMP_GAP_LOW_WITHOUT_BATT_TEMP 12
#define OPCHG_USBTEMP_GAP_HIGH_WITHOUT_BATT_TEMP 24
#define OPCHG_USBTEMP_RISE_FAST_TEMP_LOW 3
#define OPCHG_USBTEMP_RISE_FAST_TEMP_HIGH 3
#define OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_LOW 30
#define OPCHG_USBTEMP_RISE_FAST_TEMP_COUNT_HIGH 20

#define OPCHG_USBTEMP_COOL_DOWN_NTC_LOW 54
#define OPCHG_USBTEMP_COOL_DOWN_NTC_HIGH 65
#define OPCHG_USBTEMP_COOL_DOWN_GAP_LOW 12
#define OPCHG_USBTEMP_COOL_DOWN_GAP_HIGH 20
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_LOW 48
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_NTC_HIGH 60
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_LOW 6
#define OPCHG_USBTEMP_COOL_DOWN_RECOVER_GAP_HIGH 15
#define OPCHG_USBTEMP_COOL_DOWN_RECOUP_HIGH 0
#define OPCHG_USBTEMP_COOL_DOWN_RECOUP_LOW 0

#define FEATURE_PRINT_CHGR_LOG
#define FEATURE_PRINT_BAT_LOG
#define FEATURE_PRINT_GAUGE_LOG
#define FEATURE_PRINT_STATUS_LOG
/*#define FEATURE_PRINT_OTHER_LOG*/
#define FEATURE_PRINT_VOTE_LOG
#define FEATURE_PRINT_ICHGING_LOG
#define FEATURE_VBAT_PROTECT

#define NOTIFY_CHARGER_OVER_VOL			1
#define NOTIFY_CHARGER_LOW_VOL			2
#define NOTIFY_BAT_OVER_TEMP			3
#define NOTIFY_BAT_LOW_TEMP			4
#define NOTIFY_BAT_NOT_CONNECT			5
#define NOTIFY_BAT_OVER_VOL			6
#define NOTIFY_BAT_FULL				7
#define NOTIFY_CHGING_CURRENT			8
#define NOTIFY_CHGING_OVERTIME			9
#define NOTIFY_BAT_FULL_PRE_HIGH_TEMP		10
#define NOTIFY_BAT_FULL_PRE_LOW_TEMP		11
#define NOTIFY_BAT_FULL_THIRD_BATTERY		14
#define NOTIFY_SHORT_C_BAT_CV_ERR_CODE1		15
#define NOTIFY_SHORT_C_BAT_FULL_ERR_CODE2	16
#define NOTIFY_SHORT_C_BAT_FULL_ERR_CODE3	17
#define NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE4	18
#define NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE5	19
#define	NOTIFY_CHARGER_TERMINAL			20
#define NOTIFY_GAUGE_I2C_ERR			21
#define NOTIFY_CHARGER_BATT_TERMINAL		22
#define NOTIFY_FAST_CHG_END_ERROR		23
#define NOTIFY_MOS_OPEN_ERROR			24
#define NOTIFY_CURRENT_UNBALANCE		25

#define OPLUS_CHG_500_CHARGING_CURRENT	500
#define OPLUS_CHG_900_CHARGING_CURRENT	900
#define OPLUS_CHG_1200_CHARGING_CURRENT	1200
#define OPLUS_CHG_1500_CHARGING_CURRENT	1500
#define OPLUS_CHG_1800_CHARGING_CURRENT	1800
#define OPLUS_CHG_2000_CHARGING_CURRENT	2000
#define OPLUS_CHG_3600_CHARGING_CURRENT	3600

#define SMART_VOOC_CHARGER_CURRENT_BIT0 	0X01
#define SMART_VOOC_CHARGER_CURRENT_BIT1 	0X02
#define SMART_VOOC_CHARGER_CURRENT_BIT2 	0X04
#define SMART_VOOC_CHARGER_CURRENT_BIT3 	0X08
#define SMART_VOOC_CHARGER_CURRENT_BIT4 	0X10
#define SMART_VOOC_CHARGER_CURRENT_BIT5 	0X20
#define SMART_VOOC_CHARGER_CURRENT_BIT6 	0X40
#define SMART_VOOC_CHARGER_CURRENT_BIT7 	0X80

#define SMART_COMPATIBLE_VOOC_CHARGER_CURRENT_BIT0 	0X100
#define SMART_COMPATIBLE_VOOC_CHARGER_CURRENT_BIT1 	0X200
#define SMART_COMPATIBLE_VOOC_CHARGER_CURRENT_BIT2 	0X400
#define SMART_CHARGE_USER_USBTEMP	1
#define SMART_CHARGE_USER_OTHER		0
#define USBTEMP_CHARGING_CURRENT_LIMIT	3000
#define USBTEMP_CURR_TABLE_MAX		5

#define SMART_NORMAL_CHARGER_500MA 	0X1000
#define SMART_NORMAL_CHARGER_900MA	0X2000
#define SMART_NORMAL_CHARGER_1200MA	0X4000
#define SMART_NORMAL_CHARGER_1500MA	0X8000
#define SMART_NORMAL_CHARGER_2000MA     0X400
#define SMART_NORMAL_CHARGER_9V1500mA	0X800
#define OPLUS_CHG_GET_SUB_CURRENT          _IOWR('M', 1, char[256])
#define OPLUS_CHG_GET_SUB_VOLTAGE          _IOWR('M', 2, char[256])
#define OPLUS_CHG_GET_SUB_SOC              _IOWR('M', 3, char[256])
#define OPLUS_CHG_GET_SUB_TEMPERATURE      _IOWR('M', 4, char[256])

#define TEMPERATURE_INVALID		(-2740)
#define SUB_BATT_CURRENT_50_MA		50
#define MOS_OPEN			0
#define MOS_TEST_DEFAULT_COOL_DOWN	1

#define PDO_9V 9000
#define PDO_5V 5000
#define PDO_2A 2000
#define PDO_3A 2000

#define FG_I2C_ERROR -400

#define OPLUS_PDQC_5VTO9V 1
#define OPLUS_PDQC_9VTO5V 2
#define chg_info(fmt, ...)                                                     \
	printk(KERN_INFO "[OPLUS_CHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define chg_debug(fmt, ...)                                                    \
	printk(KERN_NOTICE "[OPLUS_CHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define chg_err(fmt, ...)                                                      \
	printk(KERN_ERR "[OPLUS_CHG][%s]" fmt, __func__, ##__VA_ARGS__)

enum {
	PD_INACTIVE = 0,
	PD_ACTIVE,
	PD_PPS_ACTIVE,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
enum oplus_power_supply_type {
	POWER_SUPPLY_TYPE_USB_HVDCP = 13,		/* High Voltage DCP */
	POWER_SUPPLY_TYPE_USB_HVDCP_3,		/* Efficient High Voltage DCP */
	POWER_SUPPLY_TYPE_USB_HVDCP_3P5,	/* Efficient High Voltage DCP */
	POWER_SUPPLY_TYPE_USB_FLOAT,		/* Floating charger */
	POWER_SUPPLY_TYPE_USB_PD_SDP,		/* USB With PD Port*/
};
enum oplus_power_supply_usb_type {
	POWER_SUPPLY_USB_TYPE_PD_SDP = 17,		/* USB With PD Port*/
};
#else
enum oplus_power_supply_type {
	POWER_SUPPLY_TYPE_USB_PD_SDP = 17,		/* USB With PD Port*/
};
enum oplus_power_supply_usb_type {
	POWER_SUPPLY_USB_TYPE_PD_SDP = 17,		/* USB With PD Port*/
};
#endif

typedef enum {
	CHG_NONE = 0,
	CHG_DISABLE,
	CHG_SUSPEND,
} OPLUS_CHG_DISABLE_STATUS;

typedef enum {
	BCC_CURR_DONE_UNKNOW = 0,
	BCC_CURR_DONE_REQUEST,
	BCC_CURR_DONE_ACK,
} OPLUS_BCC_CURR_DONE_STATUS;

typedef enum {
	CHG_STOP_VOTER_NONE = 0,
	CHG_STOP_VOTER__BATTTEMP_ABNORMAL = (1 << 0),
	CHG_STOP_VOTER__VCHG_ABNORMAL = (1 << 1),
	CHG_STOP_VOTER__VBAT_TOO_HIGH = (1 << 2),
	CHG_STOP_VOTER__MAX_CHGING_TIME = (1 << 3),
	CHG_STOP_VOTER__FULL = (1 << 4),
	CHG_STOP_VOTER__VBAT_OVP = (1 << 5),
	CHG_STOP_VOTER_BAD_VOL_DIFF = (1 << 6),
} OPLUS_CHG_STOP_VOTER;

typedef enum {
	CHG_CYCLE_VOTER__NONE		= 0,
	CHG_CYCLE_VOTER__ENGINEER	= (1 << 0),
	CHG_CYCLE_VOTER__USER		= (1 << 1),
}OPLUS_CHG_CYCLE_VOTER;

typedef enum {
	CHARGER_STATUS__GOOD,
	CHARGER_STATUS__VOL_HIGH,
	CHARGER_STATUS__VOL_LOW,
	CHARGER_STATUS__INVALID
} OPLUS_CHG_VCHG_STATUS;

typedef enum {
	BATTERY_STATUS__NORMAL = 0, /*16C~44C*/
	BATTERY_STATUS__REMOVED, /*<-20C*/
	BATTERY_STATUS__LOW_TEMP, /*<-2C*/
	BATTERY_STATUS__HIGH_TEMP, /*>53C*/
	BATTERY_STATUS__COLD_TEMP, /*-2C~0C*/
	BATTERY_STATUS__LITTLE_COLD_TEMP, /*0C~5C*/
	BATTERY_STATUS__COOL_TEMP, /*5C~12C*/
	BATTERY_STATUS__LITTLE_COOL_TEMP, /*12C~16C*/
	BATTERY_STATUS__WARM_TEMP, /*44C~53C*/
	BATTERY_STATUS__INVALID
}
OPLUS_CHG_TBATT_STATUS;

typedef enum {
	BATTERY_STATUS__COLD_PHASE1,	/* -20 ~ -10C */
	BATTERY_STATUS__COLD_PHASE2,	/* -10 ~ 0C */
} OPLUS_CHG_TBATT_COLD_STATUS;

typedef enum {
        BATTERY_STATUS__NORMAL_PHASE1,	/*16~22C*/
        BATTERY_STATUS__NORMAL_PHASE2,	/*22~34C*/
        BATTERY_STATUS__NORMAL_PHASE3,	/*34~37C*/
        BATTERY_STATUS__NORMAL_PHASE4,	/*37~40C*/
        BATTERY_STATUS__NORMAL_PHASE5,	/*40~42C*/
        BATTERY_STATUS__NORMAL_PHASE6,	/*42~45C*/
}
OPLUS_CHG_TBATT_NORMAL_STATUS;

typedef enum {
	LED_TEMP_STATUS__NORMAL = 0, /*<=35c*/
	LED_TEMP_STATUS__WARM, /*>35 && <=37C*/
	LED_TEMP_STATUS__HIGH, /*>37C*/
} OPLUS_CHG_TLED_STATUS;

typedef enum {
	FFC_TEMP_STATUS__NORMAL = 0, /*<=35c*/
	FFC_TEMP_STATUS__WARM, /*>35 && <=40C*/
	FFC_TEMP_STATUS__HIGH, /*>40C*/
	FFC_TEMP_STATUS__LOW, /*<16C*/
} OPLUS_CHG_FFC_TEMP_STATUS;

typedef enum {
	CRITICAL_LOG_NORMAL = 0,
	CRITICAL_LOG_UNABLE_CHARGING,
	CRITICAL_LOG_BATTTEMP_ABNORMAL,
	CRITICAL_LOG_VCHG_ABNORMAL,
	CRITICAL_LOG_VBAT_TOO_HIGH,
	CRITICAL_LOG_CHARGING_OVER_TIME,
	CRITICAL_LOG_VOOC_WATCHDOG,
	CRITICAL_LOG_VOOC_BAD_CONNECTED,
	CRITICAL_LOG_VOOC_BTB,
	CRITICAL_LOG_VOOC_FW_UPDATE_ERR,
	CRITICAL_LOG_VBAT_OVP,
} OPLUS_CHG_CRITICAL_LOG;

typedef enum {
	CHARGING_STATUS_CCCV = 0X01,
	CHARGING_STATUS_FULL = 0X02,
	CHARGING_STATUS_FAIL = 0X03,
} OPLUS_CHG_CHARGING_STATUS;

typedef enum {
	CHARGER_SUBTYPE_DEFAULT = 0,
	CHARGER_SUBTYPE_FASTCHG_VOOC,
	CHARGER_SUBTYPE_FASTCHG_SVOOC,
	CHARGER_SUBTYPE_PD,
	CHARGER_SUBTYPE_QC,
	CHARGER_SUBTYPE_PPS,
	CHARGER_SUBTYPE_UFCS,
	CHARGER_SUBTYPE_PE20
} OPLUS_CHARGER_SUBTYPE;

typedef enum {
	SHIP_MODE_NOT_CONFIG = 0,
	SHIP_MODE_PLATFORM,
} OPLUS_SHIP_MODE_CONFIG;

typedef enum {
	VOOC_TEMP_STATUS__NORMAL = 0, /*<=34c*/
	VOOC_TEMP_STATUS__WARM, /*>34 && <=38C*/
	VOOC_TEMP_STATUS__HIGH, /*>38 && <=45C*/
} OPLUS_CHG_TBAT_VOOC_STATUS;

typedef enum {
	CHG_IC_TYPE_PLAT = 0,
	CHG_IC_TYPE_EXT,
} OPLUS_CHG_IC_TYPE;

typedef enum {
	NO_VOOCPHY = 0,
	ADSP_VOOCPHY,
	AP_SINGLE_CP_VOOCPHY,
	AP_DUAL_CP_VOOCPHY,
	INVALID_VOOCPHY,
} OPLUS_VOOCPHY_TYPE;

typedef enum {
	NO_VOOC = 0,
	VOOC,
	DUAL_BATT_50W,
	DUAL_BATT_65W,
	SINGLE_BATT_50W,
	VOOCPHY_33W = 5,
	VOOCPHY_60W,
	DUAL_BATT_80W,
	DUAL_BATT_100W = 8,
	DUAL_BATT_150W,
	POWER_BANK_66W = 12,
	POWER_BANK_67W = 13,
	POWER_BANK_120W = 14,
	POWER_BANK_44W = 15,
	DUAL_BATT_240W = 16,
	POWER_BANK_200W = 17,
	POWER_BANK_88W = 18,
	POWER_BANK_55W = 19,
	POWER_BANK_125W = 20,
	INVALID_VOOC_PROJECT,
} OPLUS_VOOC_PROJECT_TYPE;

typedef enum {
	OPLUS_USBTEMP_TIMER_STAGE0 = 0,
	OPLUS_USBTEMP_TIMER_STAGE1,
} OPLUS_USBTEMP_TIMER_STAGE;

struct usbtemp_curr {
	int batt_curr;
	int temp_delta;
};

static struct usbtemp_curr temp_curr_table[USBTEMP_CURR_TABLE_MAX] = {
	{ 5000, 12 },
	{ 6000, 18 },
	{ 10000, 20 }
};

struct tbatt_normal_anti_shake {
	int phase1_bound;
	int phase2_bound;
	int phase3_bound;
	int phase4_bound;
	int phase5_bound;
	int phase6_bound;
};

struct tbatt_anti_shake {
	int cold_bound;
	int freeze_bound;
	int little_cold_bound;
	int cool_bound;
	int little_cool_bound;
	int normal_bound;
	int warm_bound;
	int hot_bound;
	int overtemp_bound;
};

struct oplus_chg_limits {
	int force_input_current_ma;
	int input_current_cdp_ma;
	int input_current_charger_ma;
	int pd_input_current_charger_ma;
	int default_pd_input_current_charger_ma;
	int qc_input_current_charger_ma;
	int default_qc_input_current_charger_ma;
	int input_current_usb_ma;
	int input_current_camera_ma;
	int input_current_calling_ma;
	int input_current_led_ma;
	int input_current_cool_down_ma;
	int input_current_vooc_led_ma_high;
	int input_current_vooc_led_ma_warm;
	int input_current_vooc_led_ma_normal;
	int vooc_high_bat_decidegc; /*>=45C*/
	int input_current_vooc_ma_high;
	int default_input_current_vooc_ma_high;
	int vooc_warm_bat_decidegc; /*38C*/
	int vooc_warm_bat_decidegc_antishake; /*38C*/
	int input_current_vooc_ma_warm;
	int default_input_current_vooc_ma_warm;
	int vooc_normal_bat_decidegc; /*34C*/
	int vooc_normal_bat_decidegc_antishake;
	int input_current_vooc_ma_normal; /*<34c*/
	int default_input_current_vooc_ma_normal;
	int charger_current_vooc_ma_normal;
	int iterm_ma;
	int sub_iterm_ma;
	bool iterm_disabled;
	int recharge_mv;
	int usb_high_than_bat_decidegc; /*10C*/
	int removed_bat_decidegc; /*-19C*/
	int cold_bat_decidegc; /*-20C*/
	int temp_cold_vfloat_mv;
	int temp_cold_fastchg_current_ma;
	int temp_cold_fastchg_current_ma_high;
	int temp_cold_fastchg_current_ma_low;
	int pd_temp_cold_fastchg_current_ma_high;
	int pd_temp_cold_fastchg_current_ma_low;
	int qc_temp_cold_fastchg_current_ma_high;
	int qc_temp_cold_fastchg_current_ma_low;

	int freeze_bat_decidegc; /*-10C*/
	int temp_freeze_fastchg_current_ma;
	int temp_freeze_fastchg_current_ma_high;
	int temp_freeze_fastchg_current_ma_low;
	int pd_temp_freeze_fastchg_current_ma_high;
	int pd_temp_freeze_fastchg_current_ma_low;
	int qc_temp_freeze_fastchg_current_ma_high;
	int qc_temp_freeze_fastchg_current_ma_low;

	int little_cold_bat_decidegc; /*0C*/
	int temp_little_cold_vfloat_mv;
	int temp_little_cold_fastchg_current_ma;
	int temp_little_cold_fastchg_current_ma_high;
	int temp_little_cold_fastchg_current_ma_low;
	int pd_temp_little_cold_fastchg_current_ma_high;
	int pd_temp_little_cold_fastchg_current_ma_low;
	int qc_temp_little_cold_fastchg_current_ma_high;
	int qc_temp_little_cold_fastchg_current_ma_low;
	int cool_bat_decidegc; /*5C*/
	int temp_cool_vfloat_mv;
	int temp_cool_fastchg_current_ma_high;
	int temp_cool_fastchg_current_ma_low;
	int pd_temp_cool_fastchg_current_ma_high;
	int pd_temp_cool_fastchg_current_ma_low;
	int qc_temp_cool_fastchg_current_ma_high;
	int qc_temp_cool_fastchg_current_ma_low;
	int little_cool_bat_decidegc; /*12C*/
	int temp_little_cool_vfloat_mv;
	int temp_little_cool_fastchg_current_ma;
	int temp_little_cool_fastchg_current_ma_high;
	int temp_little_cool_fastchg_current_ma_low;
	int pd_temp_little_cool_fastchg_current_ma;
	int pd_temp_little_cool_fastchg_current_ma_high;
	int pd_temp_little_cool_fastchg_current_ma_low;
	int qc_temp_little_cool_fastchg_current_ma;
	int qc_temp_little_cool_fastchg_current_ma_high;
	int qc_temp_little_cool_fastchg_current_ma_low;
	int normal_bat_decidegc; /*16C*/
	int temp_normal_fastchg_current_ma;
	int pd_temp_normal_fastchg_current_ma;
	int qc_temp_normal_fastchg_current_ma;

	int normal_phase1_bat_decidegc; /* 16C ~ 22C */
	int temp_normal_phase1_vfloat_mv;
	int temp_normal_phase1_fastchg_current_ma;
	int normal_phase2_bat_decidegc; /* 22C ~ 34C */
	int temp_normal_phase2_vfloat_mv;
	int temp_normal_phase2_fastchg_current_ma_high;
	int temp_normal_phase2_fastchg_current_ma_low;
	int normal_phase3_bat_decidegc; /* 34 ~ 37C */
	int temp_normal_phase3_vfloat_mv;
	int temp_normal_phase3_fastchg_current_ma_high;
	int temp_normal_phase3_fastchg_current_ma_low;
	int normal_phase4_bat_decidegc; /* 37C ~ 40C */
	int temp_normal_phase4_vfloat_mv;
	int temp_normal_phase4_fastchg_current_ma_high;
	int temp_normal_phase4_fastchg_current_ma_low;
	int normal_phase5_bat_decidegc; /* 40C ~ 42C */
	int temp_normal_phase5_vfloat_mv;
	int temp_normal_phase5_fastchg_current_ma;
	int normal_phase6_bat_decidegc; /*42C_45C*/
	int temp_normal_phase6_vfloat_mv;
	int temp_normal_phase6_fastchg_current_ma;

	int temp_normal_vfloat_mv;
	int warm_bat_decidegc; /*45C*/
	int temp_warm_vfloat_mv;
	int temp_warm_fastchg_current_ma;
	int temp_warm_fastchg_current_ma_led_on;
	int pd_temp_warm_fastchg_current_ma;
	int qc_temp_warm_fastchg_current_ma;
	int hot_bat_decidegc; /*53C*/
	int non_standard_vfloat_mv;
	int non_standard_fastchg_current_ma;
	int max_chg_time_sec;
	int charger_hv_thr;
	int charger_recv_thr;
	int charger_lv_thr;
	int vbatt_full_thr;
	int vbatt_hv_thr;
	int vfloat_step_mv;
	int vfloat_sw_set;
	int vfloat_over_counts;
	int non_standard_vfloat_sw_limit; /*sw full*/
	int cold_vfloat_sw_limit;
	int little_cold_vfloat_sw_limit;
	int cool_vfloat_sw_limit;
	int little_cool_vfloat_sw_limit;
	int normal_vfloat_sw_limit;
	int warm_vfloat_sw_limit;
	int led_high_bat_decidegc; /*>=37C*/
	int led_high_bat_decidegc_antishake;
	int input_current_led_ma_high;
	int led_warm_bat_decidegc; /*35C*/
	int led_warm_bat_decidegc_antishake;
	int input_current_led_ma_warm;
	int input_current_led_ma_normal; /*<35c*/
	int short_c_bat_vfloat_mv;
	int short_c_bat_fastchg_current_ma;
	int short_c_bat_vfloat_sw_limit;
	bool sw_vfloat_over_protect_enable; /*vfloat over check*/
	int non_standard_vfloat_over_sw_limit;
	int cold_vfloat_over_sw_limit;
	int little_cold_vfloat_over_sw_limit;
	int cool_vfloat_over_sw_limit;
	int little_cool_vfloat_over_sw_limit;
	int normal_vfloat_over_sw_limit;
	int warm_vfloat_over_sw_limit;
	int normal_vterm_hw_inc;
	int non_normal_vterm_hw_inc;
	int vbatt_pdqc_to_5v_thr;
	int vbatt_pdqc_to_9v_thr;
	int tbatt_pdqc_to_5v_thr;
	int tbatt_pdqc_to_9v_thr;
	int ff1_normal_fastchg_ma;
	int ff1_warm_fastchg_ma;
	int ff1_exit_step_ma; /*<=35C,700ma*/
	int ff1_warm_exit_step_ma;
	int sub_ff1_exit_step_ma; /*<=35C,700ma*/
	int sub_ff1_warm_exit_step_ma;
	int ffc2_temp_low_decidegc; /*<16C*/
	int ffc2_temp_high_decidegc; /*>=40C*/
	int ffc2_warm_fastchg_ma; /*35~40C,750ma	*/
	int ffc2_temp_warm_decidegc; /*35C*/
	int ffc2_normal_fastchg_ma; /*<=35C,700ma*/
	int ffc2_exit_step_ma; /*<=35C,700ma*/
	int ffc2_warm_exit_step_ma;
	int sub_ffc2_exit_step_ma; /*<=35C*/
	int sub_ffc2_warm_exit_step_ma;
	int ffc1_normal_vfloat_sw_limit; /* 4.45V */
	int ffc1_warm_vfloat_sw_limit;
	int ffc2_normal_vfloat_sw_limit;
	int ffc2_warm_vfloat_sw_limit;
	int ffc_temp_normal_vfloat_mv; /* 4.5v */
	int ffc2_temp_warm_vfloat_mv;
	int ffc_temp_warm_vfloat_mv;
	int ffc1_temp_normal_vfloat_mv; /* 4.5v */
	int ffc2_temp_normal_vfloat_mv; /* 4.5v */
	int ffc_normal_vfloat_over_sw_limit; /* 4.5v */
	int ffc_warm_vfloat_over_sw_limit;
	int ffc1_normal_vfloat_over_sw_limit;		/* 4.5V */
	int ffc2_normal_vfloat_over_sw_limit;		/* 4.5V */
	int default_ffc1_normal_vfloat_sw_limit;
	int default_ffc1_warm_vfloat_sw_limit;
	int default_ffc2_normal_vfloat_sw_limit;
	int default_ffc2_warm_vfloat_sw_limit;
	int default_iterm_ma; /*16~45 default value*/
	int default_sub_iterm_ma; /*16~45 default value*/
	int default_temp_normal_fastchg_current_ma;
	int default_normal_vfloat_sw_limit;
	int default_temp_normal_vfloat_mv;
	int default_normal_vfloat_over_sw_limit;
	int default_temp_little_cool_fastchg_current_ma; /* 12 ~ 16 */
	int default_little_cool_vfloat_sw_limit;
	int default_temp_little_cool_vfloat_mv;
	int default_little_cool_vfloat_over_sw_limit;
	int default_temp_little_cool_fastchg_current_ma_high;
	int default_temp_little_cool_fastchg_current_ma_low;
	int default_temp_little_cold_fastchg_current_ma_high; /* 0 ~ 5 */
	int default_temp_little_cold_fastchg_current_ma_low;
	int default_temp_cold_fastchg_current_ma_high;
	int default_temp_cold_fastchg_current_ma_low;
	int default_temp_cool_fastchg_current_ma_high; /* 5 ~ 12 */
	int default_temp_cool_fastchg_current_ma_low;
	int default_temp_warm_fastchg_current_ma; /* 44 ~ 53 */
	int default_input_current_charger_ma;
};

struct battery_data {
	int BAT_STATUS;
	int BAT_HEALTH;
	int BAT_PRESENT;
	int BAT_TECHNOLOGY;
	int  BAT_CAPACITY;
	/* Add for Battery Service*/
	int BAT_batt_vol;
	int BAT_batt_temp;
	/* Add for EM */
	int BAT_TemperatureR;
	int BAT_TempBattVoltage;
	int BAT_InstatVolt;
	int BAT_BatteryAverageCurrent;
	int BAT_BatterySenseVoltage;
	int BAT_ISenseVoltage;
	int BAT_ChargerVoltage;
	int battery_request_poweroff; /* low battery in sleep */
	int fastcharger;
	int charge_technology;
	/* Dual battery */
	int BAT_MMI_CHG;//for MMI_CHG_TEST
	int BAT_FCC;
	int BAT_SOH;
	int BAT_CC;
};

struct normalchg_gpio_pinctrl {
	int chargerid_switch_gpio;
	int chargerid_switch_gpio_pvt;
	int usbid_gpio;
	int usbid_irq;
	int ship_gpio;
	int shortc_gpio;
	int dischg_gpio;
	int ntcctrl_gpio;
	struct pinctrl *pinctrl;
	struct mutex pinctrl_mutex;
	struct pinctrl_state *chargerid_switch_active;
	struct pinctrl_state *chargerid_switch_sleep;
	struct pinctrl_state *chargerid_switch_default;
	struct pinctrl_state *chargerid_switch_pvt_active;
	struct pinctrl_state *chargerid_switch_pvt_sleep;
	struct pinctrl_state *chargerid_switch_pvt_default;
	struct pinctrl_state *usbid_active;
	struct pinctrl_state *usbid_sleep;
	struct pinctrl_state *ship_active;
	struct pinctrl_state *ship_sleep;
	struct pinctrl_state *shortc_active;
	struct pinctrl_state *charger_gpio_as_output1;
	struct pinctrl_state *charger_gpio_as_output2;
	struct pinctrl_state *dischg_enable;
	struct pinctrl_state *dischg_disable;
	struct pinctrl_state *ntcctrl_high;
	struct pinctrl_state *ntcctrl_low;
	struct pinctrl_state *usb_temp_adc;
	struct pinctrl_state *usb_temp_adc_suspend;
	struct pinctrl_state *uart_bias_disable;
	struct pinctrl_state *uart_pull_down;
	struct pinctrl_state *chargerid_adc_default;
};

struct short_c_batt_data {
	int short_c_bat_cv_mv;
	int batt_chging_cycle_threshold;
	int batt_chging_cycles;
	int cv_timer1;
	int full_timer2;
	int full_timer3;
	int full_timer4;
	int full_timer5;
	int cool_temp_rbatt;
	int little_cool_temp_rbatt;
	int normal_temp_rbatt;
	int full_delta_vbatt1_mv;
	int full_delta_vbatt2_mv;
	int ex2_lower_ibatt_ma;
	int ex2_low_ibatt_ma;
	int ex2_high_ibatt_ma;
	int ex2_lower_ibatt_count;
	int ex2_low_ibatt_count;
	int ex2_high_ibatt_count;
	int dyna1_low_avg_dv_mv;
	int dyna1_high_avg_dv_mv;
	int dyna1_delta_dv_mv;
	int dyna2_low_avg_dv_mv;
	int dyna2_high_avg_dv_mv;
	int dyna2_delta_dv_mv;
	int is_recheck_on;
	int is_switch_on;
	int is_feature_sw_on;
	int is_feature_hw_on;
	u8 ic_volt_threshold;
	bool ic_short_otp_st;
	int err_code;
	int update_change;
	bool in_idle;
	bool cv_satus;
	bool disable_rechg;
	bool limit_chg;
	bool limit_rechg;
	bool shortc_gpio_status;
};

struct oplus_chg_full_data {
	bool hw_full;
	bool sub_hw_full;
	int vbat_counts_hw;
	int sub_vbat_counts_hw;
	int clear_full_check_count;
};

#define SMOOTH_SOC_MAX_FIFO_LEN	4
#define SMOOTH_SOC_MIN_FIFO_LEN	1
#define RESERVE_SOC_MIN		1
#define RESERVE_SOC_DEFAULT	3
#define RESERVE_SOC_MAX		5
#define RESERVE_SOC_OFF		0
#define OPLUS_FULL_SOC		100
#define SOC_JUMP_RANGE_VAL	1
struct reserve_soc_data {
	bool smooth_switch_v2;
	bool is_soc_jump_range;
	int reserve_soc;
	int rus_reserve_soc;

	int smooth_soc_fifo[SMOOTH_SOC_MAX_FIFO_LEN];
	int smooth_soc_index;
	int smooth_soc_avg_cnt;
};

typedef enum {
	AGING_FFC_NOT_SUPPORT,
	AGING_FFC_V1,
	AGING_FFC_VERSION_MAX
} AGING_FFC_VERSION;

#define AGING1_STAGE_CYCLE	500
#define AGING2_STAGE_CYCLE	1000

#define AGING1_FFC1_SINGLE_OFFSET_MV	10
#define AGING1_FFC2_SINGLE_OFFSET_MV	10
#define AGING2_FFC1_SINGLE_OFFSET_MV	15
#define AGING2_FFC2_SINGLE_OFFSET_MV	15

#define AGING1_FFC1_DOUBLE_OFFSET_MV	15
#define AGING1_FFC2_DOUBLE_OFFSET_MV	10
#define AGING2_FFC1_DOUBLE_OFFSET_MV	30
#define AGING2_FFC2_DOUBLE_OFFSET_MV	20

struct oplus_chg_chip {
	struct i2c_client *client;
	struct device *dev;
	const struct oplus_chg_operations *chg_ops;
	struct power_supply *ac_psy;
	struct power_supply_desc ac_psd;
	struct power_supply_config ac_cfg;
	struct power_supply_desc usb_psd;
	struct power_supply_config usb_cfg;
	struct power_supply_desc battery_psd;
	struct power_supply *usb_psy;
	struct power_supply_desc wls_psd;
	struct power_supply *wls_psy;
	struct oplus_chg_mod *wls_ocm;
	struct oplus_chg_mod *comm_ocm;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct qcom_pmic pmic_spmi;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK_CHGIC
	struct mtk_pmic chgic_mtk;
#endif
	struct power_supply *batt_psy;
	/*	struct battery_data battery_main	*/
	struct delayed_work update_work;
	struct delayed_work aging_check_work;
	struct delayed_work ui_soc_decimal_work;
	struct delayed_work mmi_adapter_in_work;
	struct delayed_work reset_adapter_work;
	struct delayed_work turn_on_charging_work;
	struct delayed_work fg_soft_reset_work;
	struct delayed_work parallel_batt_chg_check_work;
	struct alarm usbtemp_alarm_timer;
	struct work_struct usbtemp_restart_work;
	struct delayed_work parallel_chg_mos_test_work;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock suspend_lock;
#else
	struct wakeup_source *suspend_ws;
#endif
	struct oplus_chg_limits limits;
	struct tbatt_anti_shake anti_shake_bound;
	struct tbatt_normal_anti_shake tbatt_normal_anti_shake_bound;
	struct short_c_batt_data short_c_batt;
	atomic_t file_opened;
	atomic_t mos_lock;
	int mos_test_result;
	bool mos_test_started;

	int alarm_clockid;
	bool usbtemp_wq_init_finished;
	bool wireless_support;
	bool wpc_no_chargerpump;
	bool charger_exist;
	bool pre_charger_exist;
	int charger_type;
	int charger_subtype;
	int real_charger_type;
	int charger_volt;
	int charger_volt_pre;
	int charger_current_pre;
	int sw_full_count;
	bool sw_full;
	bool hw_full_by_sw;
	bool hw_full;
	int sw_sub_batt_full_count;
	bool sw_sub_batt_full;
	bool hw_sub_batt_full_by_sw;
	bool hw_sub_batt_full;
	struct oplus_chg_full_data full_data;
	int temperature;
	int qc_abnormal_check_count;
	int tbatt_temp;
	int shell_temp;
	int subboard_temp;
	int tbatt_power_off_cali_temp;
	bool tbatt_use_subboard_temp;
	bool tbatt_shell_status;
	bool support_tbatt_shell;
	int offset_temp;
	int batt_volt;
	int vbatt_num;
	int batt_volt_max;
	int batt_volt_min;
	int vbatt_power_off;
	int sub_vbatt_power_off;
	int vbatt_soc_1;
	int soc_to_0_withchg;
	int soc_to_0_withoutchg;
	int icharging;
	int charger_cycle;
	int ibus;
	int soc;
	int ui_soc;
	int cv_soc;
	int smooth_soc;
	int smooth_switch;
	int soc_load;
	int shutdown_uisoc;
	int ui_soc_decimal;
	int ui_soc_integer;
	int last_decimal_ui_soc;
	int init_decimal_ui_soc;
	int calculate_decimal_time;
	bool boot_completed;
	bool authenticate;
	bool hmac;
	bool nightstandby_support;
	int batt_fcc;
	int batt_cc;
	int batt_soh;
	int batt_rm;
	int batt_capacity_mah;
	int tbatt_pre_shake;
	int tbatt_normal_pre_shake;
	int tbatt_cold_pre_shake;
	bool batt_exist;
	bool batt_full;
	bool real_batt_full;
	int tbatt_when_full;
	bool chging_on;
	bool in_rechging;
	bool adsp_notify_ap_suspend;
	bool voocphy_notify_ap_suspend;
	bool first_enabled_adspvoocphy;
	int charging_state;
	int total_time;
	unsigned long sleep_tm_sec;
	unsigned long save_sleep_tm_sec;
	bool vbatt_over;
	bool chging_over_time;
	int vchg_status;
	int tbatt_status;
	int tbatt_normal_status;
	int tbatt_cold_status;
	int prop_status;
	int keep_prop_status;
	int stop_voter;
	int notify_code;
	int notify_flag;
	int cool_down;
	int normal_cool_down;
	int smart_normal_cool_down;
	int smart_charge_user;
	int usbtemp_cool_down;
	bool usbtemp_check;
	bool led_on;
	bool led_on_change;
	bool led_temp_change;
	int led_temp_status;
	bool vooc_temp_change;
	int vooc_temp_status;
	bool camera_on;
	bool calling_on;
	bool ac_online;
	bool cool_down_done;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	bool usb_online;
#endif
	bool otg_online;
	bool otg_switch;
	bool ui_otg_switch;
	int mmi_chg;
	int unwakelock_chg;
	int stop_chg;
	int mmi_fastchg;
	int boot_reason;
	int boot_mode;
	int vooc_project;
	bool suspend_after_full;
	bool check_batt_full_by_sw;
	bool external_gauge;
	bool external_authenticate;
	bool chg_ctrl_by_lcd;
	bool chg_ctrl_by_lcd_default;
	bool chg_ctrl_by_camera;
	bool chg_ctrl_by_cool_down;
	bool bq25890h_flag;
	bool chg_ctrl_by_calling;
	bool chg_ctrl_by_vooc;
	bool chg_ctrl_by_vooc_default;
	bool fg_bcl_poll;
	bool chg_powersave;
	bool healthd_ready;
#if IS_ENABLED(CONFIG_FB) || IS_ENABLED(CONFIG_DRM_MSM) ||                     \
	IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
	struct notifier_block chg_fb_notify;
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY_CHG)
	struct notifier_block chg_fb_notify;
#endif
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY)
	struct drm_panel *active_panel;
	struct drm_panel *active_panel_sec;
	void *notifier_cookie;
	void *notifier_cookie_sec;
#endif
	struct normalchg_gpio_pinctrl normalchg_gpio;
	int flash_screen_ctrl_status;
	int chargerid_volt;
	bool chargerid_volt_got;
	int enable_shipmode;
	int dod0_counts;
	bool recharge_after_full;
	bool recharge_after_ffc;
	bool ffc_support;
	bool dual_ffc;
	int voocphy_support;
	bool voocphy_support_display_vooc;
	bool fg_info_package_read_support;
	bool new_ui_warning_support;
	bool fastchg_to_ffc;
	bool waiting_for_ffc;
	int fastchg_ffc_status;
	int ffc_temp_status;
	bool allow_swtich_to_fastchg;
	bool platform_fg_flag;
	struct task_struct *shortc_thread;
	int usbtemp_volt;
	int usb_temp;
	int usbtemp_volt_l;
	int usbtemp_volt_r;
	int usb_temp_l;
	int usb_temp_r;
	/* wangjiayuan_wt, BSP.CHG.Basic, 2021/9/8, add for 21027 */
	bool usbtemp_chan_tmp;
	struct task_struct *tbatt_pwroff_task;
	bool dual_charger_support;
	int slave_pct;
	bool slave_charger_enable;
	bool cool_down_force_5v;
	int slave_chg_enable_ma;
	int slave_chg_disable_ma;
	bool dischg_flag;
	int internal_gauge_with_asic;
	bool smart_charging_screenoff;
	bool skip_usbtemp_cool_down;
	int screenoff_curr;
	int usb_status;
	int *con_volt;
	int *con_temp;
	int len_array;
	wait_queue_head_t oplus_usbtemp_wq;
	wait_queue_head_t oplus_usbtemp_wq_new_method;
	wait_queue_head_t oplus_bcc_wq;
	int usbtemp_batttemp_gap;
	int usbtemp_batttemp_recover_gap;
	int usbtemp_batttemp_current_gap;
	bool usbtemp_change_gap;
	int usbtemp_max_temp_diff;
	int usbtemp_cool_down_temp_gap;
	int usbtemp_cool_down_temp_first_gap;
	int usbtemp_cool_down_temp_second_gap;
	int usbtemp_cool_down_recovery_temp_gap;
	int usbtemp_cool_down_recovery_temp_first_gap;
	int usbtemp_cool_down_recovery_temp_second_gap;
	bool new_usbtemp_cool_down_support;
	int usbtemp_max_temp_thr;
	int usbtemp_temp_up_time_thr;
	int smooth_to_soc_gap;
	int smart_charge_version;
	int ui_soc_decimal_speedmin;
	bool decimal_control;
	bool vooc_show_ui_soc_decimal;
	bool em_mode;
	struct thermal_zone_device *shell_themal;
	int svooc_disconnect_count;
	int detect_detach_unexpeactly;
	unsigned long long svooc_detect_time;
	unsigned long long svooc_detach_time;
	struct device_node *fast_node;
	const struct oplus_chg_operations *sub_chg_ops;
	bool is_double_charger_support;
	int pd_svooc;
	int pd_chging;
	int soc_ajust;
	int modify_soc;
	ktime_t first_ktime;
	ktime_t second_ktime;
#ifdef OPLUS_CUSTOM_OP_DEF
	bool hiz_gnd_cable;
	int cool_down_bck;
#endif
	int efttest_fast_switch;
	int ship_mode_config;
	bool flash_led_status;
	char batt_type_string[4];
	bool bat_volt_different;
	int check_battery_vol_count;
	bool is_abnormal_adapter;
	bool bidirect_abnormal_adapter;
	bool support_abnormal_adapter;
	bool icon_debounce;
	int abnormal_adapter_dis_cnt;
	bool disable_ship_mode;
	int ibat_save[10];
	int pd_wait_svid;
	unsigned long reset_mcu_jiffies;

	int wls_status_keep;
	int balancing_bat_stop_chg;
	int balancing_bat_stop_fastchg;
	int balancing_bat_status;
	int sub_batt_volt;
	int sub_batt_icharging;
	int main_batt_soc;
	int sub_batt_soc;
	int main_batt_temperature;
	int sub_batt_temperature;
	int wls_set_boost_vol;
	int batt_target_curr;
	int pre_charging_current;
	bool aicl_done;

	bool support_low_soc_unlimit;
	int unlimit_soc;
	bool force_psy_changed;
	bool screen_off_control_by_batt_temp;
	bool vooc_enable_charger_first;
	bool old_smart_charge_standard;
	bool pd_adapter_support_9v;
	OPLUS_USBTEMP_TIMER_STAGE usbtemp_timer_stage;

	bool pps_svooc_enable;
	bool smart_chg_bcc_support;
	bool smart_chg_soh_support;
	int bcc_current;
	int bcc_cool_down;
	struct mutex bcc_curr_done_mutex;
	int bcc_curr_done;
	bool usbtemp_dischg_by_pmic;
	int transfer_timeout_count;

	bool vooc_start_fail;
	bool gauge_iic_err;
	unsigned long gauge_iic_err_time_record;
	int pd_authentication;
	bool pps_force_svooc;
	bool support_3p6_standard;
	bool pdqc_9v_voltage_adaptive;
	bool suport_pd_9v2a;
	struct timespec quick_mode_time;
	int start_time;
	int quick_mode_start_time;
	int quick_mode_stop_time;
	int quick_mode_gain_time_ms;
	int quick_mode_start_cap;
	int quick_mode_stop_cap;
	int quick_mode_stop_temp;
	int quick_mode_stop_soc;
	int quick_mode_need_update;
	bool quick_mode_gain_support;
	bool dual_panel_support;

	int uisoc_1_start_batt_rm;
	int uisoc_1_start_vbatt_max;
	int uisoc_1_start_vbatt_min;
	int uisoc_1_start_time;
	int debug_vbat_keep_soc_1;
	int debug_force_uisoc_less_than_2;
	int debug_force_vbatt_too_low;
	int debug_force_usbtemp_trigger;
	int debug_force_fast_gpio_err;
	int debug_force_cooldown_match_trigger;
	int debug_batt_cc;
	int aging_ffc_version;
	char chg_power_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	char err_reason[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
	bool cool_down_check_done;
	struct mutex track_upload_lock;

	oplus_chg_track_trigger fast_gpio_err_load_trigger;
	struct delayed_work fast_gpio_err_load_trigger_work;
	oplus_chg_track_trigger uisoc_keep_1_t_load_trigger;
	struct delayed_work uisoc_keep_1_t_load_trigger_work;
	oplus_chg_track_trigger vbatt_too_low_load_trigger;
	struct delayed_work vbatt_too_low_load_trigger_work;
	oplus_chg_track_trigger usbtemp_load_trigger;
	struct delayed_work usbtemp_load_trigger_work;
	oplus_chg_track_trigger vbatt_diff_over_load_trigger;
	struct delayed_work vbatt_diff_over_load_trigger_work;
	oplus_chg_track_trigger cool_down_match_err_load_trigger;
	struct delayed_work cool_down_match_err_load_trigger_work;
	struct reserve_soc_data rsd;
	bool is_gauge_ready;

	bool pd_disable;
	bool support_wd0;

	bool support_usbtemp_protect_v2;
	bool usbtemp_change_across_unplug;
	int usbtemp_curr_status;
	int usbtemp_batt_current;
	int usbtemp_pre_batt_current;
	int usbtemp_batt_temp_low;
	int usbtemp_batt_temp_high;
	int usbtemp_ntc_temp_low;
	int usbtemp_ntc_temp_high;
	int usbtemp_temp_gap_low_with_batt_temp;
	int usbtemp_temp_gap_high_with_batt_temp;
	int usbtemp_temp_gap_low_without_batt_temp;
	int usbtemp_temp_gap_high_without_batt_temp;
	int usbtemp_rise_fast_temp_low;
	int usbtemp_rise_fast_temp_high;
	int usbtemp_rise_fast_temp_count_low;
	int usbtemp_rise_fast_temp_count_high;

	int usbtemp_cool_down_ntc_low;
	int usbtemp_cool_down_ntc_high;
	int usbtemp_cool_down_gap_low;
	int usbtemp_cool_down_gap_high;
	int usbtemp_cool_down_recover_ntc_low;
	int usbtemp_cool_down_recover_ntc_high;
	int usbtemp_cool_down_recover_gap_low;
	int usbtemp_cool_down_recover_gap_high;
	int usbtemp_cool_down_recoup_high;
	int usbtemp_cool_down_recoup_low;
	bool fg_soft_reset_done;
	int fg_soft_reset_fail_cnt;
	int fg_check_ibat_cnt;
	int parallel_error_flag;
	bool soc_not_full_report;
	bool support_subboard_ntc;
	bool full_pre_ffc_judge;
	int full_pre_ffc_mv;
	bool boot_reset_adapter;
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	struct test_feature *chg_switch1_gpio_test;
	struct test_feature *chg_switch2_gpio_test;
	struct test_feature *chg_uart_gpio_test;
	struct test_feature *typec_port_test;
#endif
	bool usbin_abnormal_status;
	bool support_check_usbin_status;
	int check_usbin_from_adsp_cnt;
	int usb_present_vbus0_count;

	int bms_heat_temp_compensation;
	int chg_cycle_status;
};

#define SOFT_REST_VOL_THRESHOLD		4300
#define SOFT_REST_SOC_THRESHOLD		95
#define SOFT_REST_CHECK_DISCHG_MAX_CUR	200
#define SOFT_REST_RETRY_MAX_CNT		2

struct oplus_chg_operations {
	void (*get_props_from_adsp_by_buffer)(void);
	int (*get_charger_cycle)(void);
	void (*get_usbtemp_volt)(struct oplus_chg_chip *chip);
	void (*set_typec_sinkonly)(void);
	void (*set_typec_cc_open)(void);
	void (*really_suspend_charger)(bool en);
	bool (*oplus_usbtemp_monitor_condition)(void);
	int (*recovery_usbtemp)(void *data);
	void (*dump_registers)(void);
	int (*kick_wdt)(void);
	int (*hardware_init)(void);
	int (*charging_current_write_fast)(int cur);
	int (*set_wls_boost_en)(bool enable);
	int (*wls_set_boost_en)(bool en);
	int (*wls_set_boost_vol)(int vol_mv);
	int (*input_current_ctrl_by_vooc_write)(int cur);
	void (*set_aicl_point)(int vbatt);
	int (*input_current_write)(int cur);
	int (*float_voltage_write)(int cur);
	int (*term_current_set)(int cur);
	int (*charging_enable)(void);
	int (*charging_disable)(void);
	int (*get_charging_enable)(void);
	int (*charger_suspend)(void);
	int (*charger_unsuspend)(void);
	bool (*charger_suspend_check)(void);
	int (*set_rechg_vol)(int vol);
	int (*reset_charger)(void);
	int (*read_full)(void);
	int (*otg_enable)(void);
	int (*otg_disable)(void);
	int (*set_charging_term_disable)(void);
	bool (*check_charger_resume)(void);
	int (*get_charger_type)(void);
	int (*get_charger_volt)(void);
	int (*get_ibus)(void);
	int (*get_charger_current)(void);
	int (*get_real_charger_type)(void);
	int (*get_chargerid_volt)(void);
	void (*set_chargerid_switch_val)(int value);
	int (*get_chargerid_switch_val)(void);
	bool (*check_chrdet_status)(void);
	int (*get_boot_mode)(void);
	int (*get_boot_reason)(void);
	int (*get_instant_vbatt)(void);
	int (*get_rtc_soc)(void);
	int (*set_rtc_soc)(int val);
	void (*set_power_off)(void);
	void (*usb_connect)(void);
	void (*usb_disconnect)(void);
	void (*get_platform_gauge_curve)(int index_curve);
#ifndef CONFIG_OPLUS_CHARGER_MTK
	int (*get_aicl_ma)(void);
	void (*rerun_aicl)(void);
	int (*tlim_en)(bool);
	int (*set_system_temp_level)(int);
	int (*otg_pulse_skip_disable)(enum skip_reason, bool);
	int (*set_dp_dm)(int);
	int (*calc_flash_current)(void);
	void (*subcharger_force_enable)(void);
#endif
	int (*get_chg_current_step)(void);
	bool (*need_to_check_ibatt)(void);
#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
	int (*check_rtc_reset)(void);
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */
	int (*get_dyna_aicl_result)(void);
	bool (*get_shortc_hw_gpio_status)(void);
	void (*check_is_iindpm_mode)(void);
	int (*oplus_chg_get_pd_type)(void);
	int (*oplus_chg_pd_setup)(void);
	int (*oplus_chg_pps_setup)(int vbus_mv, int ibus_ma);
	u32 (*oplus_chg_get_pps_status)(void);
	int (*oplus_chg_get_max_cur)(int vbus_mv);
	int (*get_charger_subtype)(void);
	int (*set_qc_config)(void);
	void (*em_mode_enable)(void);
	int (*enable_qc_detect)(void);
	int (*input_current_write_without_aicl)(int current_ma);
	int (*wls_input_current_write)(int current_ma);
	int (*set_charger_vsys_threshold)(int val);
	int (*enable_burst_mode)(bool enable);
	void (*oplus_chg_wdt_enable)(bool wdt_enable);
	void (*adsp_voocphy_set_match_temp)(void);
	int (*oplus_chg_set_high_vbus)(bool en);
	int (*oplus_chg_set_hz_mode)(bool en);
	int (*enable_shipmode)(bool en);
	bool (*check_pdphy_ready)(void);
	int (*pdo_5v)(void);
	int (*set_enable_volatile_writes)(void);
	int (*set_complete_charge_timeout)(int val);
	int (*set_prechg_voltage_threshold)(void);
	int (*set_prechg_current)(int ipre_mA);
	int (*set_vindpm_vol)(int vol);
	void (*rerun_wls_aicl)(void);
	int (*disable_buck_switch)(void);
	int (*disable_async_mode)(void);
	int (*set_switching_frequency)(void);
	int (*set_mps_otg_current)(void);
	int (*set_mps_otg_voltage)(bool is_9v);
	int (*set_mps_second_otg_voltage)(bool is_750mv);
	int (*set_wdt_timer)(int reg);
	int (*set_voltage_slew_rate)(int value);
	int (*otg_wait_vbus_decline)(void);
	void (*vooc_timeout_callback)(bool);
	void (*force_pd_to_dcp)(void);
	bool (*get_otg_enable)(void);
	bool (*check_qchv_condition)(void);
	int (*set_bcc_curr_to_voocphy)(int bcc_curr);
	bool (*is_support_qcpd)(void);
	int (*get_subboard_temp)(void);
	int (*get_ccdetect_online)(void);
	int (*check_cc_mode)(void);
};

int __attribute__((weak))
ppm_sys_boost_min_cpu_freq_set(int freq_min, int freq_mid, int freq_max,
			       unsigned int clear_time)
{
	return 0;
}

int __attribute__((weak)) ppm_sys_boost_min_cpu_freq_clear(void)
{
	return 0;
}

bool __attribute__((weak)) get_ppm_freq_info(void)
{
	return true;
}

#ifdef CONFIG_DISABLE_OPLUS_FUNCTION
unsigned int __attribute__((weak)) get_eng_version(void)
{
	return 0;
}
unsigned int __attribute__((weak)) get_PCB_Version(void)
{
	return 0;
}
unsigned int __attribute__((weak)) get_project(void)
{
	return -1;
}
int __attribute__((weak)) get_boot_mode(void)
{
	return 0;
}
#endif

int oplus_get_report_batt_temp(void);
/*********************************************
 * power_supply usb/ac/battery functions
 **********************************************/
extern int oplus_usb_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val);
extern int oplus_ac_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val);
extern int oplus_battery_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp);
extern int oplus_battery_set_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      const union power_supply_propval *val);
extern int oplus_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val);

/*********************************************
 * oplus_chg_init - initialize oplus_chg_chip
 * @chip: pointer to the oplus_chg_chip
 * @clinet: i2c client of the chip
 *
 * Returns: 0 - success; -1/errno - failed
 **********************************************/
int oplus_chg_parse_svooc_dt(struct oplus_chg_chip *chip);
int oplus_chg_parse_charger_dt(struct oplus_chg_chip *chip);

int oplus_chg_init(struct oplus_chg_chip *chip);
void oplus_charger_detect_check(struct oplus_chg_chip *chip);
int oplus_chg_get_prop_batt_health(struct oplus_chg_chip *chip);

bool oplus_chg_wake_update_work(void);
void oplus_chg_input_current_recheck_work(void);
void oplus_chg_soc_update_when_resume(unsigned long sleep_tm_sec);
void oplus_chg_soc_update(void);
int oplus_chg_get_batt_volt(void);
int oplus_chg_get_cool_bat_decidegc(void);
int oplus_chg_get_little_cool_bat_decidegc(void);
int oplus_chg_get_normal_bat_decidegc(void);
int oplus_chg_get_icharging(void);
bool oplus_chg_get_chging_status(void);

int oplus_chg_get_ui_soc(void);
int oplus_chg_get_soc(void);
int oplus_chg_get_chg_temperature(void);

void oplus_chg_kick_wdt(void);
void oplus_chg_dump_registers(void);
bool oplus_chg_stats(void);
void oplus_chg_enable_charge(void);
void oplus_chg_disable_charge(void);
void oplus_chg_unsuspend_charger(void);
void oplus_chg_suspend_charger(void);

int oplus_chg_get_chg_type(void);
int oplus_chg_get_pps_type(void);

int oplus_chg_get_notify_flag(void);
int oplus_is_vooc_project(void);

int oplus_chg_show_vooc_logo_ornot(void);

bool get_otg_switch(void);

#ifdef CONFIG_OPLUS_CHARGER_MTK
bool oplus_chg_get_otg_online(void);
void oplus_chg_set_otg_online(bool online);
#endif

bool oplus_chg_get_batt_full(void);
bool oplus_chg_get_rechging_status(void);

bool oplus_chg_check_pd_disable(void);

int oplus_chg_check_ui_soc(void);
bool oplus_chg_check_chip_is_null(void);
void oplus_chg_set_charger_type_unknown(void);
int oplus_chg_get_charger_voltage(void);
#ifdef OPLUS_CUSTOM_OP_DEF
int oplus_chg_get_charger_current(void);
#endif
int oplus_chg_update_voltage(void);

void oplus_chg_voter_charging_stop(struct oplus_chg_chip *chip,
				   OPLUS_CHG_STOP_VOTER voter);
void oplus_chg_set_chargerid_switch_val(int value);
int oplus_chg_get_chargerid_switch_val(void);
void oplus_chg_turn_on_charging(struct oplus_chg_chip *chip);
void oplus_chg_turn_on_charging_in_work(void);
void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip);
int oplus_chg_get_cool_down_status(void);
int oplus_chg_get_normal_cool_down_status(void);
void oplus_smart_charge_by_cool_down(struct oplus_chg_chip *chip, int val);
int oplus_convert_current_to_level(struct oplus_chg_chip *chip, int val);
int oplus_convert_pps_current_to_level(struct oplus_chg_chip *chip, int val);
void oplus_smart_charge_by_shell_temp(struct oplus_chg_chip *chip, int val);
int oplus_smart_charge_by_bcc(struct oplus_chg_chip *chip, int val);
int oplus_chg_override_by_shell_temp(int temp);
int oplus_chg_get_shell_temp(void);
void oplus_chg_clear_chargerid_info(void);
int oplus_chg_get_gauge_and_asic_status(void);
#ifndef CONFIG_OPLUS_CHARGER_MTK
void oplus_chg_variables_reset(struct oplus_chg_chip *chip, bool in);
void oplus_chg_external_power_changed(struct power_supply *psy);
#endif
int oplus_is_rf_ftm_mode(void);
int oplus_get_charger_chip_st(void);
void oplus_chg_set_allow_switch_to_fastchg(bool allow);
int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip);
void oplus_tbatt_power_off_task_wakeup(void);
bool oplus_get_chg_powersave(void);
int oplus_get_chg_unwakelock(void);
void oplus_chg_set_input_current_without_aicl(int current_ma);
void oplus_chg_config_charger_vsys_threshold(int val);
void oplus_chg_enable_burst_mode(bool enable);
void oplus_chg_set_charger_otg_enable(bool enable);
int oplus_chg_get_tbatt_status(void);
int oplus_chg_get_tbatt_normal_charging_current(struct oplus_chg_chip *chip);
bool oplus_get_vbatt_higherthan_xmv(void);
bool oplus_chg_wake_up_ui_soc_decimal(void);
void oplus_chg_ui_soc_decimal_init(void);
bool oplus_chg_get_boot_completed(void);
int oplus_chg_match_temp_for_chging(void);
void oplus_chg_reset_adapter(void);
int oplus_chg_get_fast_chg_type(void);
void oplus_chg_pdqc9v_vindpm_vol_switch(int val);

struct oplus_chg_chip *oplus_chg_get_chip(void);
int oplus_chg_get_voocphy_support(void);
int oplus_voocphy_thermal_current_to_level(int ibus);
int oplus_voocphy_cool_down_convert(int bf_cool_down);
bool oplus_voocphy_set_user_exit_fastchg(unsigned char exit);
void oplus_chg_set_flash_led_status(bool val);
bool oplus_voocphy_stage_check(void);
void oplus_chg_platform_gauge_choose_curve(void);
int oplus_chg_get_charger_cycle(void);
void oplus_chg_get_props_from_adsp_by_buffer(void);
bool oplus_get_flash_screen_ctrl(void);
int set_soc_feature(void);
bool oplus_chg_check_disable_charger(void);
bool oplus_chg_is_wls_present(void);
bool oplus_chg_is_wls_fastchg_started(void);
struct oplus_chg_chip *oplus_chg_get_chg_struct(void);

void oplus_chg_check_break(int vbus_rising);

int oplus_chg_set_enable_volatile_writes(void);
int oplus_chg_set_complete_charge_timeout(int val);
int oplus_chg_set_prechg_voltage_threshold(void);
int oplus_chg_set_prechg_current(int ipre_mA);
int oplus_chg_set_vindpm_vol(int vol);
bool opchg_get_shipmode_value(void);
int oplus_chg_disable_buck_switch(void);
int oplus_chg_disable_async_mode(void);
int oplus_chg_set_switching_frequency(void);
int oplus_chg_set_mps_otg_current(void);
int oplus_chg_set_mps_otg_voltage(bool is_9v);
int oplus_chg_set_mps_second_otg_voltage(bool is_750mv);
int oplus_chg_set_wdt_timer(int reg);
int oplus_chg_set_voltage_slew_rate(int value);
int oplus_chg_otg_wait_vbus_decline(void);
int oplus_chg_get_abnormal_adapter_dis_cnt(void);
void oplus_chg_set_abnormal_adapter_dis_cnt(int count);
bool oplus_chg_get_icon_debounce(void);
void oplus_chg_set_icon_debounce(bool status);
void oplus_chg_clear_abnormal_adapter_var(void);
bool oplus_chg_is_abnormal_adapter(void);
bool oplus_chg_fg_package_read_support(void);
int oplus_chg_get_wls_status_keep(void);
void oplus_chg_set_wls_status_keep(int value);

#ifdef OPLUS_CUSTOM_OP_DEF
int oplus_svooc_disconnect_time(void);
#endif

int oplus_chg_set_enable_volatile_writes(void);
int oplus_chg_set_complete_charge_timeout(int val);
int oplus_chg_set_prechg_voltage_threshold(void);
int oplus_chg_set_prechg_current(int ipre_mA);
int oplus_chg_set_vindpm_vol(int vol);
int oplus_chg_disable_buck_switch(void);
int oplus_chg_disable_async_mode(void);
int oplus_chg_set_switching_frequency(void);
int oplus_chg_set_mps_otg_current(void);
int oplus_chg_set_mps_otg_voltage(bool is_9v);
int oplus_chg_set_mps_second_otg_voltage(bool is_750mv);
int oplus_chg_set_wdt_timer(int reg);
int oplus_chg_set_voltage_slew_rate(int value);
int oplus_chg_otg_wait_vbus_decline(void);
bool oplus_chg_get_wait_for_ffc_flag(void);
void oplus_chg_set_wait_for_ffc_flag(bool wait_for_ffc);
void oplus_chg_set_force_psy_changed(void);
int oplus_chg_get_design_capacity(void);
bool oplus_chg_get_bcc_support(void);
void oplus_chg_check_bcc_curr_done(void);
int oplus_chg_get_bcc_curr_done_status(void);
void oplus_chg_vooc_timeout_callback(bool vbus_rising);
void oplus_chg_really_suspend_charger(bool en);
bool oplus_is_ptcrb_version(void);
void oplus_chg_set_adsp_notify_ap_suspend(void);
bool oplus_chg_get_adsp_notify_ap_suspend(void);
int oplus_chg_get_mmi_value(void);
int oplus_chg_get_stop_chg(void);
int oplus_chg_get_batt_num(void);
bool oplus_get_vooc_start_fg(void);
int oplus_chg_get_fv_when_vooc(struct oplus_chg_chip *chip);
void oplus_chg_get_aging_ffc_offset(struct oplus_chg_chip *chip,
	int *ffc1_offset, int *ffc2_offset);
int oplus_get_ccdetect_online(void);
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
void oplus_test_kit_unregister(void);
#endif
//#endif
#endif /*_OPLUS_CHARGER_H_*/
