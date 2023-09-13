#ifndef __OPLUS_CHG_CORE_H__
#define __OPLUS_CHG_CORE_H__

#include <linux/version.h>
#include <oplus_chg_symbol.h>

extern int oplus_log_level;

enum {
	LOG_LEVEL_OFF = 0,
	LOG_LEVEL_ERR,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_ALL_INFO,
	LOG_LEVEL_ALL_ERR,
};

#define chg_err(fmt, ...)                                                      \
	({                                                                     \
		if (oplus_log_level >= LOG_LEVEL_ERR)                          \
			printk(KERN_ERR "[ERROR]: OPLUS_CHG" pr_fmt(fmt),      \
			       ##__VA_ARGS__);                                 \
	})
#define chg_info(fmt, ...)                                                     \
	({                                                                     \
		if (oplus_log_level >= LOG_LEVEL_ALL_ERR)                      \
			printk(KERN_ERR "[INFO]: OPLUS_CHG" pr_fmt(fmt),       \
			       ##__VA_ARGS__);                                 \
		else if (oplus_log_level >= LOG_LEVEL_INFO)                    \
			printk(KERN_INFO "[INFO]: OPLUS_CHG" pr_fmt(fmt),      \
			       ##__VA_ARGS__);                                 \
	})
#define chg_debug(fmt, ...)                                                    \
	({                                                                     \
		if (oplus_log_level >= LOG_LEVEL_ALL_ERR)                      \
			printk(KERN_ERR "[DEBUG]: OPLUS_CHG" pr_fmt(fmt),      \
			       ##__VA_ARGS__);                                 \
		else if (oplus_log_level >= LOG_LEVEL_ALL_INFO)                \
			printk(KERN_INFO "[DEBUG]: OPLUS_CHG" pr_fmt(fmt),     \
			       ##__VA_ARGS__);                                 \
		else if (oplus_log_level >= LOG_LEVEL_DEBUG)                   \
			printk(KERN_NOTICE "[DEBUG]: OPLUS_CHG" pr_fmt(fmt),   \
			       ##__VA_ARGS__);                                 \
	})

#define true_or_false_str(condition) (condition ? "true" : "false")

#ifdef CONFIG_OPLUS_CHARGER_MTK
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <uapi/linux/rtc.h>

//for dx1 bringup
#ifdef __KERNEL__
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
	__kernel_old_time_t	tv_sec;		/* seconds */
	long			tv_nsec;	/* nanoseconds */
};
#endif

struct timeval {
	__kernel_old_time_t	tv_sec;		/* seconds */
	__kernel_suseconds_t	tv_usec;	/* microseconds */
};

struct itimerspec {
	struct timespec it_interval;/* timer period */
	struct timespec it_value;	/* timer expiration */
};

struct itimerval {
	struct timeval it_interval;/* timer interval */
	struct timeval it_value;	/* current value */
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
static inline struct timespec timespec64_to_timespec(const struct timespec64 ts64)
{
	return *(const struct timespec *)&ts64;
}

static inline struct timespec64 timespec_to_timespec64(const struct timespec ts)
{
	return *(const struct timespec64 *)&ts;
}

#else
static inline struct timespec timespec64_to_timespec(const struct timespec64 ts64)
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

static inline struct timespec current_kernel_time(void)
{
	struct timespec64 ts64;

	ktime_get_coarse_real_ts64(&ts64);

	return timespec64_to_timespec(ts64);
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)) */
#endif /* CONFIG_OPLUS_CHARGER_MTK */

#define PD_SRC_PDO_TYPE(pdo)		(((pdo) >> 30) & 3)
#define PD_SRC_PDO_TYPE_FIXED		0
#define PD_SRC_PDO_TYPE_BATTERY		1
#define PD_SRC_PDO_TYPE_VARIABLE	2
#define PD_SRC_PDO_TYPE_AUGMENTED	3

#define PD_SRC_PDO_FIXED_PR_SWAP(pdo)		(((pdo) >> 29) & 1)
#define PD_SRC_PDO_FIXED_USB_SUSP(pdo)		(((pdo) >> 28) & 1)
#define PD_SRC_PDO_FIXED_EXT_POWERED(pdo)	(((pdo) >> 27) & 1)
#define PD_SRC_PDO_FIXED_USB_COMM(pdo)		(((pdo) >> 26) & 1)
#define PD_SRC_PDO_FIXED_DR_SWAP(pdo)		(((pdo) >> 25) & 1)
#define PD_SRC_PDO_FIXED_PEAK_CURR(pdo)		(((pdo) >> 20) & 3)
#define PD_SRC_PDO_FIXED_VOLTAGE(pdo)		(((pdo) >> 10) & 0x3FF)
#define PD_SRC_PDO_FIXED_MAX_CURR(pdo)		((pdo) & 0x3FF)

#define PD_SRC_PDO_VAR_BATT_MAX_VOLT(pdo)	(((pdo) >> 20) & 0x3FF)
#define PD_SRC_PDO_VAR_BATT_MIN_VOLT(pdo)	(((pdo) >> 10) & 0x3FF)
#define PD_SRC_PDO_VAR_BATT_MAX(pdo)		((pdo) & 0x3FF)

#define PD_APDO_PPS(pdo)			(((pdo) >> 28) & 3)
#define PD_APDO_MAX_VOLT(pdo)			(((pdo) >> 17) & 0xFF)
#define PD_APDO_MIN_VOLT(pdo)			(((pdo) >> 8) & 0xFF)
#define PD_APDO_MAX_CURR(pdo)			((pdo) & 0x7F)

int oplus_is_rf_ftm_mode(void);

typedef enum {
	CHARGER_SUBTYPE_DEFAULT = 0,
	CHARGER_SUBTYPE_FASTCHG_VOOC,
	CHARGER_SUBTYPE_FASTCHG_SVOOC,
	CHARGER_SUBTYPE_PD,
	CHARGER_SUBTYPE_QC,
} OPLUS_CHARGER_SUBTYPE;

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
} OPLUS_CHG_CRITICAL_LOG;

enum {
	NOTIFY_CHARGER_OVER_VOL = 1,
	NOTIFY_CHARGER_LOW_VOL,
	NOTIFY_BAT_OVER_TEMP,
	NOTIFY_BAT_LOW_TEMP,
	NOTIFY_BAT_NOT_CONNECT,
	NOTIFY_BAT_OVER_VOL,
	NOTIFY_BAT_FULL,
	NOTIFY_CHGING_CURRENT,
	NOTIFY_CHGING_OVERTIME,
	NOTIFY_BAT_FULL_PRE_HIGH_TEMP,
	NOTIFY_BAT_FULL_PRE_LOW_TEMP,
	NOTIFY_BAT_FULL_THIRD_BATTERY = 14,
	NOTIFY_SHORT_C_BAT_CV_ERR_CODE1,
	NOTIFY_SHORT_C_BAT_FULL_ERR_CODE2,
	NOTIFY_SHORT_C_BAT_FULL_ERR_CODE3,
	NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE4,
	NOTIFY_SHORT_C_BAT_DYNAMIC_ERR_CODE5,
	NOTIFY_CHARGER_TERMINAL,
	NOTIFY_GAUGE_I2C_ERR,
};

enum oplus_chg_err_code {
	OPLUS_ERR_CODE_I2C,
	OPLUS_ERR_CODE_OCP,
	OPLUS_ERR_CODE_OVP,
	OPLUS_ERR_CODE_UCP,
	OPLUS_ERR_CODE_UVP,
	OPLUS_ERR_CODE_TIMEOUT,
	OPLUS_ERR_CODE_OVER_HEAT,
	OPLUS_ERR_CODE_COLD,
};

enum oplus_chg_qc_version {
	OPLUS_CHG_QC_2_0,
	OPLUS_CHG_QC_3_0
};

enum oplus_chg_typec_port_role_type {
	TYPEC_PORT_ROLE_DRP,
	TYPEC_PORT_ROLE_SNK,
	TYPEC_PORT_ROLE_SRC,
	TYPEC_PORT_ROLE_TRY_SNK,
	TYPEC_PORT_ROLE_TRY_SRC,
	TYPEC_PORT_ROLE_DISABLE,
	TYPEC_PORT_ROLE_ENABLE,
	TYPEC_PORT_ROLE_INVALID,
};

enum oplus_chg_usb_type {
	OPLUS_CHG_USB_TYPE_UNKNOWN = 0,
	OPLUS_CHG_USB_TYPE_SDP,
	OPLUS_CHG_USB_TYPE_DCP,
	OPLUS_CHG_USB_TYPE_CDP,
	OPLUS_CHG_USB_TYPE_ACA,
	OPLUS_CHG_USB_TYPE_C,
	OPLUS_CHG_USB_TYPE_PD,
	OPLUS_CHG_USB_TYPE_PD_DRP,
	OPLUS_CHG_USB_TYPE_PD_PPS,
	OPLUS_CHG_USB_TYPE_PD_SDP,
	OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID,
	OPLUS_CHG_USB_TYPE_QC2,
	OPLUS_CHG_USB_TYPE_QC3,
	OPLUS_CHG_USB_TYPE_VOOC,
	OPLUS_CHG_USB_TYPE_SVOOC,
	OPLUS_CHG_USB_TYPE_UFCS,
	OPLUS_CHG_USB_TYPE_MAX,
};

enum oplus_chg_wls_type {
	OPLUS_CHG_WLS_UNKNOWN,
	OPLUS_CHG_WLS_BPP,
	OPLUS_CHG_WLS_EPP,
	OPLUS_CHG_WLS_EPP_PLUS,
	OPLUS_CHG_WLS_VOOC,
	OPLUS_CHG_WLS_SVOOC,
	OPLUS_CHG_WLS_PD_65W,
	OPLUS_CHG_WLS_TRX,
};

enum oplus_chg_temp_region_type {
	OPLUS_CHG_BATT_TEMP_COLD = 0,
	OPLUS_CHG_BATT_TEMP_LITTLE_COLD,
	OPLUS_CHG_BATT_TEMP_COOL,
	OPLUS_CHG_BATT_TEMP_LITTLE_COOL,
	OPLUS_CHG_BATT_TEMP_PRE_NORMAL,
	OPLUS_CHG_BATT_TEMP_NORMAL,
	OPLUS_CHG_BATT_TEMP_WARM,
	OPLUS_CHG_BATT_TEMP_HOT,
	OPLUS_CHG_BATT_TEMP_INVALID,
};

enum oplus_chg_wls_rx_mode {
	OPLUS_CHG_WLS_RX_MODE_UNKNOWN,
	OPLUS_CHG_WLS_RX_MODE_BPP,
	OPLUS_CHG_WLS_RX_MODE_EPP,
	OPLUS_CHG_WLS_RX_MODE_EPP_PLUS,
	OPLUS_CHG_WLS_RX_MODE_EPP_5W,
};

enum oplus_chg_wls_trx_status {
	OPLUS_CHG_WLS_TRX_STATUS_ENABLE,
	OPLUS_CHG_WLS_TRX_STATUS_CHARGING,
	OPLUS_CHG_WLS_TRX_STATUS_DISENABLE,
};

#define USB_TEMP_HIGH		BIT(0)
#define USB_WATER_DETECT	BIT(1)
#define USB_RESERVE2		BIT(2)
#define USB_RESERVE3		BIT(3)
#define USB_RESERVE4		BIT(4)
#define USB_DONOT_USE		BIT(31)

bool oplus_is_power_off_charging(void);
bool oplus_is_charger_reboot(void);
struct timespec oplus_current_kernel_time(void);
bool oplus_is_ptcrb_version(void);

#endif /* __OPLUS_CHG_CORE_H__ */
