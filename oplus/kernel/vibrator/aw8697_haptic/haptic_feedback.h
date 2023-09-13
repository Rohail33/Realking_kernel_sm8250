#ifndef _HAPTIC_FEEDBACK_H_
#define _HAPTIC_FEEDBACK_H_

#define HAPTIC_EVENT_TAG                  "fb_vibrator"
#define HAPTIC_EVENT_ID                   "vibrator_track"
#define MAX_HAPTIC_EVENT_TAG_LEN             (32)
#define MAX_HAPTIC_EVENT_ID_LEN              (20)
#define OPLUS_HAPTIC_TRACK_WAIT_TIME_MS      (3000)
#define MAX_PAYLOAD_LEN                      (1024)
#define MAX_DETAIL_INFO_LEN                  (512)
#define MAX_FAIL_INFO_LEN                    (128)
#define MAX_FUN_NAME_LEN                     (64)
#define OPLUS_HAPTIC_UPDATE_INFO_DELAY_MS    (500)
#define OPLUS_HAPTIC_FB_RETRY_TIME           (2)
#define UPLOAD_TIME_LIMIT_HOURS              (1)
#define SECSONDS_PER_HOUR                    (60 * 60)

#define MAX_DEV_EVENT_QUEUE_LEN              (3)
#define MAX_MEM_ALLOC_EVENT_QUEUE_LEN        (3)

#define OPLUS_HSPTIC_TRIGGER_MSG_LEN         (2048)

#define HAPTIC_TRACK_EVENT_DEVICE_ERR          "haptic_device_err"
#define HAPTIC_TRACK_EVENT_FRE_CALI_ERR        "haptic_fre_cli_err"
#define HAPTIC_TRACK_EVENT_MEM_ALLOC_ERR       "haptic_mem_alloc_err"

#ifdef CONFIG_OPLUS_CHARGER_MTK    /* mtk platform */
#include <linux/device.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#ifdef __KERNEL__
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
	__kernel_old_time_t	tv_sec;		/* seconds */
	long			tv_nsec;	/* nanoseconds */
};
#endif
#endif

#if __BITS_PER_LONG == 64
/* timespec64 is defined as timespec here */
static inline struct timespec timespec64_to_timespec(const struct timespec64 ts64) {
	return *(const struct timespec *)&ts64;
}

static inline struct timespec64 timespec_to_timespec64(const struct timespec ts) {
	return *(const struct timespec64 *)&ts;
}

#else
static inline struct timespec timespec64_to_timespec(const struct timespec64 ts64) {
	struct timespec ret;

	ret.tv_sec = (time_t)ts64.tv_sec;
	ret.tv_nsec = ts64.tv_nsec;
	return ret;
}

static inline struct timespec64 timespec_to_timespec64(const struct timespec ts) {
	struct timespec64 ret;

	ret.tv_sec = ts.tv_sec;
	ret.tv_nsec = ts.tv_nsec;
	return ret;
}
#endif

static inline void getnstimeofday(struct timespec *ts) {
	struct timespec64 ts64;

	ktime_get_real_ts64(&ts64);
	*ts = timespec64_to_timespec(ts64);
}
#endif
#endif

/*********************************************************
 *
 * Log Format
 *
 *********************************************************/
#define haptic_fb_err(format, ...) \
	pr_err("[haptic_hv]" format, ##__VA_ARGS__)

#define haptic_fb_info(format, ...) \
	pr_info("[haptic_hv]" format, ##__VA_ARGS__)

#define haptic_fb_dbg(format, ...) \
	pr_debug("[haptic_hv]" format, ##__VA_ARGS__)

enum oplus_haptic_track_cmd_error {
	TRACK_CMD_ACK_OK = 0,
	TRACK_CMD_ERROR_CHIP_NULL,
	TRACK_CMD_ERROR_DATA_NULL,
	TRACK_CMD_ERROR_DATA_INVALID,
	TRACK_CMD_ERROR_TIME_OUT,
	TRACK_CMD_ERROR_QUEUE_FULL,
};

enum haptic_fb_track_type {
	HAPTIC_I2C_READ_TRACK_ERR,
	HAPTIC_I2C_WRITE_TRACK_ERR,
	HAPTIC_F0_CALI_TRACK,
	HAPTIC_OSC_CALI_TRACK,
	HAPTIC_MEM_ALLOC_TRACK,
	HAPTIC_SPMI_READ_TRACK_ERR,
	HAPTIC_SPMI_WRITE_TRACK_ERR,
	HAPTIC_TRACK_TYPE_MAX,
};

struct haptic_fb_detail {
	uint32_t track_type;
	char detailData[MAX_DETAIL_INFO_LEN];
};

struct haptic_dev_event_info {
	uint32_t track_type;
	uint32_t reg_addr;
	uint32_t err_code;
};

struct haptic_dev_track_event {
	struct haptic_dev_event_info dev_event_que[MAX_DEV_EVENT_QUEUE_LEN];
	uint32_t que_front;
	uint32_t que_rear;
	struct haptic_fb_detail dev_detail_data;
	struct delayed_work track_dev_err_load_trigger_work;
};

struct haptic_fre_cail_event_info {
	uint32_t track_type;
	uint32_t cali_data;
	uint32_t result_flag;    /* 0 is success, 1 is fail */
	char fail_info[MAX_FAIL_INFO_LEN];
};

struct haptic_fre_cail_track_event {
	struct haptic_fre_cail_event_info fre_event;
	struct haptic_fb_detail fre_cali_detail;
	struct delayed_work track_fre_cail_load_trigger_work;
};

struct haptic_mem_alloc_event_info {
	uint32_t track_type;
	uint32_t alloc_len;
	char fun_name[MAX_FUN_NAME_LEN];
};

struct haptic_mem_alloc_track_event {
	struct haptic_mem_alloc_event_info mem_event_que[MAX_MEM_ALLOC_EVENT_QUEUE_LEN];
	uint32_t que_front;
	uint32_t que_rear;
	struct haptic_fb_detail mem_detail;
	struct delayed_work track_mem_alloc_err_load_trigger_work;
};

struct oplus_haptic_track {
	struct device *dev;
	struct task_struct *track_upload_kthread;
	bool trigger_data_ok;
	struct mutex upload_lock;
	struct mutex trigger_data_lock;
	struct mutex trigger_ack_lock;
	struct completion trigger_ack;
	wait_queue_head_t upload_wq;
	struct workqueue_struct *trigger_upload_wq;
	struct delayed_work upload_info_dwork;
	struct mutex payload_lock;
	int fb_retry_cnt;
	struct timespec lastest_record;
	struct kernel_packet_info *dcs_info;
	struct haptic_dev_track_event dev_track_event;
	struct haptic_fre_cail_track_event fre_cail_track_event;
	struct haptic_mem_alloc_track_event mem_alloc_track_event;
};

struct haptic_fb_info {
	uint32_t fb_event_type;
	char *fb_event_desc;
	char *fb_event_field;
};

int oplus_haptic_track_dev_err(uint32_t track_type, uint32_t reg_addr, uint32_t err_code);
int oplus_haptic_track_fre_cail(uint32_t track_type, uint32_t cali_data, uint32_t result_flag, char* fail_info);
int oplus_haptic_track_mem_alloc_err(uint32_t track_type, uint32_t alloc_len, const char *fun_name);

#endif
