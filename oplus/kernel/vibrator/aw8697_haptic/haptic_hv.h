#ifndef _HAPTIC_HV_H_
#define _HAPTIC_HV_H_

#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <sound/control.h>
#include <sound/soc.h>

/*********************************************************
 *
 * Haptic_HV CHIPID
 *
 *********************************************************/
#define AW_REG_CHIPID				(0x00) /* AW869X */
#define AW_REG_CHIPIDH				(0x57) /* AW8692X */
#define AW_REG_CHIPIDL				(0x58) /* AW8692X */
#define AW8695_CHIPID				(0x95)
#define AW8697_CHIPID				(0x97)
#define AW86925_CHIPID				(0x9250)
#define AW86926_CHIPID				(0x9260)
#define AW86927_CHIPID				(0x9270)
#define AW86928_CHIPID				(0x9280)

/*********************************************************
 *
 * Marco
 *
 *********************************************************/
#define AW_I2C_NAME				"haptic_hv"
#define HAPTIC_NAME				"awinic_haptic"
#define AW_I2C_RETRIES				(5)
#define AW_I2C_RETRY_DELAY			(2)
#define AW_READ_CHIPID_RETRIES			(5)
#define AW_READ_CHIPID_RETRY_DELAY		(2)
#define AW_SEQUENCER_SIZE			(8)
#define AW_I2C_BYTE_ONE				(1)
#define AW_I2C_BYTE_TWO				(2)
#define AW_I2C_BYTE_THREE			(3)
#define AW_I2C_BYTE_FOUR			(4)
#define AW_I2C_BYTE_FIVE			(5)
#define AW_I2C_BYTE_SIX				(6)
#define AW_I2C_BYTE_SEVEN			(7)
#define AW_I2C_BYTE_EIGHT			(8)

#define AW_SEQUENCER_LOOP_SIZE			(4)
#define AW_RTP_NAME_MAX				(64)
#define AW_PM_QOS_VALUE_VB			(400)
#define AW_VBAT_REFER				(4200)
#define AW_VBAT_MIN				(3000)
#define AW_RAM_WORK_DELAY_INTERVAL		(8000)
#define AW_OSC_TRIM_PARAM			(50)
#define AW_OSC_CALI_ACCURACY			(24)
#define AW_OSC_CALI_MAX_LENGTH			(5100000)
#define AW_TRIG_NUM				(3)
#define AW_RAMDATA_RD_BUFFER_SIZE		(1024)
#define AW_RAMDATA_WR_BUFFER_SIZE		(2048)

#define AW_GLBRD_STATE_MASK			(15<<0)
#define AW_STATE_STANDBY			(0x00)
#define AW_STATE_RTP				(0x08)
#define AW_BIT_RESET				(0xAA)
#define AW_CONTAINER_DEFAULT_SIZE		(2 * 1024 * 1024)
#define AW_RTP_NUM				(6)

/*********************************************************
 *
 * Macro Control
 *
 *********************************************************/
#define AW_CHECK_RAM_DATA
#define AW_READ_BIN_FLEXBALLY
#define AW_CHECK_QUALIFY
/* #define AW_BOOT_OSC_CALI */
#define AW_ENABLE_RTP_PRINT_LOG
/* #define AW_ENABLE_PIN_CONTROL */
#define AWINIC_RAM_UPDATE_DELAY
//#define OPLUS_FEATURE_CHG_BASIC
#define AAC_RICHTAP
/* -----motor config----- */
#define LRA_0619
/* #define LRA_0832 */
/*********************************************************
 *
 * Conditional Marco
 *
 *********************************************************/
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 4, 1)
#define TIMED_OUTPUT
#endif

#ifdef TIMED_OUTPUT
#include <../../../drivers/staging/android/timed_output.h>
typedef struct timed_output_dev cdev_t;
#else
typedef struct led_classdev cdev_t;
#endif
enum aw8692x_tm_config {
	AW_LOCK = 1,
	AW_UNLOCK = 2,
};

#ifdef AAC_RICHTAP
enum {
	RICHTAP_UNKNOWN = -1,
	RICHTAP_HAPTIC_HV = 0x05,
};

enum {
	MMAP_BUF_DATA_VALID = 0x55,
	MMAP_BUF_DATA_FINISHED = 0xAA,
	MMAP_BUF_DATA_INVALID = 0xFF,
};
#define RICHTAP_IOCTL_GROUP		(0x52)
#define RICHTAP_GET_HWINFO		_IO(RICHTAP_IOCTL_GROUP, 0x03)
#define RICHTAP_SET_FREQ		_IO(RICHTAP_IOCTL_GROUP, 0x04)
#define RICHTAP_SETTING_GAIN		_IO(RICHTAP_IOCTL_GROUP, 0x05)
#define RICHTAP_OFF_MODE		_IO(RICHTAP_IOCTL_GROUP, 0x06)
#define RICHTAP_TIMEOUT_MODE		_IO(RICHTAP_IOCTL_GROUP, 0x07)
#define RICHTAP_RAM_MODE		_IO(RICHTAP_IOCTL_GROUP, 0x08)
#define RICHTAP_RTP_MODE		_IO(RICHTAP_IOCTL_GROUP, 0x09)
#define RICHTAP_STREAM_MODE		_IO(RICHTAP_IOCTL_GROUP, 0x0A)
#define RICHTAP_UPDATE_RAM		_IO(RICHTAP_IOCTL_GROUP, 0x10)
#define RICHTAP_GET_F0			_IO(RICHTAP_IOCTL_GROUP, 0x11)
#define RICHTAP_STOP_MODE		_IO(RICHTAP_IOCTL_GROUP, 0x12)

#define RICHTAP_MMAP_BUF_SIZE		(1000)
#define RICHTAP_MMAP_PAGE_ORDER		(2)
#define RICHTAP_MMAP_BUF_SUM		(16)

#pragma pack(4)
struct mmap_buf_format {
	uint8_t status;
	uint8_t bit;
	int16_t length;
	uint32_t reserve;
	struct mmap_buf_format *kernel_next;
	struct mmap_buf_format *user_next;
	uint8_t data[RICHTAP_MMAP_BUF_SIZE];
};
#pragma pack()
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define F0_VAL_MAX_0815				(1800)
#define F0_VAL_MIN_0815				(1600)
#define F0_VAL_MAX_081538			(1600)
#define F0_VAL_MIN_081538			(1400)
#define F0_VAL_MAX_0832				(2350)
#define F0_VAL_MIN_0832				(2250)
#define F0_VAL_MAX_0833				(2380)
#define F0_VAL_MIN_0833				(2260)

#define AW_HAPTIC_BASE_VOLTAGE			(6000)
#define AW_HAPTIC_MAX_VOLTAGE			(10000)
#define AW_HAPTIC_LOW_LEVEL_VOL			(800)
#define AW_HAPTIC_LOW_LEVEL_REG_VAL		(0)
#define AW_HAPTIC_MEDIUM_LEVEL_VOL		(1600)
#define AW_HAPTIC_MEDIUM_LEVEL_REG_VAL		(0)
#define AW_HAPTIC_HIGH_LEVEL_VOL		(2500)
#define AW_HAPTIC_HIGH_LEVEL_REG_VAL		(0x5E)//max boost 9.408V

#define AW_HAPTIC_RAM_VBAT_COMP_GAIN		(0x80)

#define AW_WAVEFORM_INDEX_TRADITIONAL_1		(1)
#define AW_WAVEFORM_INDEX_TRADITIONAL_2		(2)
#define AW_WAVEFORM_INDEX_TRADITIONAL_3		(3)
#define AW_WAVEFORM_INDEX_TRADITIONAL_4		(4)
#define AW_WAVEFORM_INDEX_TRADITIONAL_5		(5)
#define AW_WAVEFORM_INDEX_TRADITIONAL_6		(6)
#define AW_WAVEFORM_INDEX_TRADITIONAL_7		(7)
#define AW_WAVEFORM_INDEX_TRADITIONAL_8		(8)
#define AW_WAVEFORM_INDEX_TRADITIONAL_9		(9)
#define AW_WAVEFORM_INDEX_TRADITIONAL_10	(10)
#define AW_WAVEFORM_INDEX_TRADITIONAL_11	(11)
#define AW_WAVEFORM_INDEX_TRADITIONAL_12	(12)
#define AW_WAVEFORM_INDEX_TRADITIONAL_13	(13)
#define AW_WAVEFORM_INDEX_TRADITIONAL_14	(14)
#define AW_WAVEFORM_INDEX_TRADITIONAL_15	(15)

#define AW_RTP_LONG_SOUND_INDEX			(44)
#define AUDIO_READY_STATUS			(1024)
#define RINGTONES_START_INDEX			(1)
#define RINGTONES_END_INDEX			(40)
#define RINGTONES_SIMPLE_INDEX			(48)
#define RINGTONES_PURE_INDEX			(49)
#define NEW_RING_START				(118)
#define NEW_RING_END				(160)
#define OS12_NEW_RING_START			(70)
#define OS12_NEW_RING_END			(89)
#define OPLUS_RING_START			(161)
#define OPLUS_RING_END				(170)


#define AW_WAVEFORM_INDEX_CS_PRESS		(16)
#define AW_WAVEFORM_INDEX_TRANSIENT		(8)
#define AW_WAVEFORM_INDEX_SINE_CYCLE		(9)
#define AW_WAVEFORM_INDEX_HIGH_TEMP		(51)
#define AW_WAVEFORM_INDEX_OLD_STEADY		(52)
#define AW_WAVEFORM_INDEX_LISTEN_POP		(53)

enum aw_haptic_custom_level {
	HAPTIC_CUSTOM_LEVEL_WEAK = 0,	/* 3V */
	HAPTIC_CUSTOM_LEVEL_MEDIUM = 1,	/* 6V */
	HAPTIC_CUSTOM_LEVEL_STRONG = 2,	/* 9V */
};


enum aw_haptic_custom_vibration_mode {
	VIBRATION_MODE_TRADITIONAL = 0,
	VIBRATION_MODE_RING = 1,
	VIBRATION_MODE_GAME = 2,
};


enum aw_haptic_motor_old_test_mode {
	MOTOR_OLD_TEST_TRANSIENT = 1,
	MOTOR_OLD_TEST_STEADY = 2,
	MOTOR_OLD_TEST_HIGH_TEMP_HUMIDITY = 3,
	MOTOR_OLD_TEST_LISTEN_POP = 4,
	MOTOR_OLD_TEST_ALL_NUM,
};
#endif

/*********************************************************
 *
 * haptic config (COMMON)
 *
 *********************************************************/
#ifdef LRA_0619
#define AW_HAPTIC_F0_PRE			(1700)
#endif

#ifdef LRA_0832
#define AW_HAPTIC_F0_PRE			(2350)
#endif
#define AW_HAPTIC_F0_CALI_PERCEN		(7)
/*********************************************************
 *
 * haptic config (AW8692X)
 *
 *********************************************************/
#define AW8692X_MAX_BST_VOL			(0x7F)
#define AW8692X_D2S_GAIN			(0x04)
#define AW8692X_BST_VOL_DEFAULT		(AW_HAPTIC_HIGH_LEVEL_REG_VAL)
#define AW8692X_BST_VOL_RAM			(AW_HAPTIC_HIGH_LEVEL_REG_VAL)
#define AW8692X_BST_VOL_RTP			(AW_HAPTIC_HIGH_LEVEL_REG_VAL)

/* need to check */
#ifdef LRA_0619			/* 170HZ */
#ifdef OPLUS_FEATURE_CHG_BASIC
#define AW8692X_CONT_DRV1_LVL			(0x7F)
#else
#define AW8692X_CONT_DRV1_LVL			(0x7F)
#endif
#define AW8692X_CONT_DRV2_LVL			(0x50)
#define AW8692X_CONT_DRV1_TIME			(0x04)
#define AW8692X_CONT_DRV2_TIME			(0x06)
#define AW8692X_CONT_DRV_WIDTH			(0x6A)
#define AW8692X_CONT_WAIT_NUM			(0x06)
#define AW8692X_CONT_BRK_TIME			(0x08)
#define AW8692X_CONT_TRACK_MARGIN		(0x0C)
#define AW8692X_BRK_BST_MD			(0x00)
#define AW8692X_CONT_TEST			(0x06)
#define AW8692X_CONT_BEMF_SET			(0x02)
#define AW8692X_CONT_BST_BRK_GAIN		(0x05)
#define AW8692X_CONT_BRK_GAIN			(0x08)
#endif

/* need to check */
#ifdef LRA_0832			/* 235HZ */
#define AW8692X_CONT_DRV1_LVL			(0x7F)
#define AW8692X_CONT_DRV2_LVL			(0x50)
#define AW8692X_CONT_DRV1_TIME			(0x04)
#define AW8692X_CONT_DRV2_TIME			(0x06)
#define AW8692X_CONT_DRV_WIDTH			(0x6A)
#define AW8692X_CONT_WAIT_NUM			(0x06)
#define AW8692X_CONT_BRK_TIME			(0x08)
#define AW8692X_CONT_TRACK_MARGIN		(0x0C)
#define AW8692X_CONT_TEST			(0x06)
#define AW8692X_CONT_BEMF_SET			(0x02)
#define AW8692X_CONT_BST_BRK_GAIN		(0x05)
#define AW8692X_CONT_BRK_GAIN			(0x08)
#define AW8692X_BRK_BST_MD			(0x00)
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define AW8692X_0832_F0_PRE			(2350)
#define AW8692X_0832_F0_CALI_PERCEN		(7)
/* need to check */
#define AW8692X_0832_CONT_DRV1_LVL		(0x00)
#define AW8692X_0832_CONT_DRV2_LVL		(0x00)
#define AW8692X_0832_CONT_DRV1_TIME		(0x00)
#define AW8692X_0832_CONT_DRV2_TIME		(0x00)
#define AW8692X_0832_CONT_DRV_WIDTH		(0x00)
#define AW8692X_0832_CONT_WAIT_NUM		(0x00)
#define AW8692X_0832_CONT_BRK_TIME		(0x00)
#define AW8692X_0832_CONT_TRACK_MARGIN		(0x00)
#define AW8692X_0832_BRK_BST_MD			(0x00)
#define AW8692X_0832_CONT_TEST			(0x00)
#define AW8692X_0832_CONT_BEMF_SET		(0x00)
#define AW8692X_0832_CONT_BST_BRK_GAIN		(0x00)
#define AW8692X_0832_CONT_BRK_GAIN		(0x00)

#define AW8692X_0815_F0_PRE			(1700)
#define AW8692X_0815_F0_CALI_PERCEN		(7)
/* need to check */
#define AW8692X_0815_CONT_DRV1_LVL		(0x7F)
#define AW8692X_0815_CONT_DRV2_LVL		(0x29)
#define AW8692X_0815_CONT_DRV1_TIME		(0x04)
#define AW8692X_0815_CONT_DRV2_TIME		(0x06)
#define AW8692X_0815_CONT_DRV_WIDTH		(0x6A)
#define AW8692X_0815_CONT_WAIT_NUM		(0x06)
#define AW8692X_0815_CONT_BRK_TIME		(0x08)
#define AW8692X_0815_CONT_TRACK_MARGIN		(0x0C)
#define AW8692X_0815_BRK_BST_MD			(0x00)
#define AW8692X_0815_CONT_TEST			(0x06)
#define AW8692X_0815_CONT_BEMF_SET		(0x02)
#define AW8692X_0815_CONT_BST_BRK_GAIN		(0x05)
#define AW8692X_0815_CONT_BRK_GAIN		(0x08)

#define AW8692X_081538_F0_PRE			(1500)
#define AW8692X_081538_F0_CALI_PERCEN		(7)
/* need to check */
#define AW8692X_081538_CONT_DRV1_LVL		(0x00)
#define AW8692X_081538_CONT_DRV2_LVL		(0x00)
#define AW8692X_081538_CONT_DRV1_TIME		(0x00)
#define AW8692X_081538_CONT_DRV2_TIME		(0x00)
#define AW8692X_081538_CONT_DRV_WIDTH		(0x00)
#define AW8692X_081538_CONT_WAIT_NUM		(0x00)
#define AW8692X_081538_CONT_BRK_TIME		(0x00)
#define AW8692X_081538_CONT_TRACK_MARGIN	(0x00)
#define AW8692X_081538_BRK_BST_MD		(0x00)
#define AW8692X_081538_CONT_TEST		(0x00)
#define AW8692X_081538_CONT_BEMF_SET		(0x00)
#define AW8692X_081538_CONT_BST_BRK_GAIN	(0x00)
#define AW8692X_081538_CONT_BRK_GAIN		(0x00)
#endif

#define AW8692X_TRIG1_DUAL_LEVEL		(1)
#define AW8692X_TRIG2_DUAL_LEVEL		(1)
#define AW8692X_TRIG3_DUAL_LEVEL		(1)

#define AW8692X_TRIG1_DUAL_POLAR		(0)
#define AW8692X_TRIG2_DUAL_POLAR		(0)
#define AW8692X_TRIG3_DUAL_POLAR		(0)

#define AW8692X_TRIG1_POS_ENABLE		(1)
#define AW8692X_TRIG2_POS_ENABLE		(1)
#define AW8692X_TRIG3_POS_ENABLE		(1)
#define AW8692X_TRIG1_POS_DISABLE		(0)
#define AW8692X_TRIG2_POS_DISABLE		(0)
#define AW8692X_TRIG3_POS_DISABLE		(0)

#define AW8692X_TRIG1_POS_SEQ			(1)
#define AW8692X_TRIG2_POS_SEQ			(1)
#define AW8692X_TRIG3_POS_SEQ			(1)

#define AW8692X_TRIG1_NEG_ENABLE		(1)
#define AW8692X_TRIG2_NEG_ENABLE		(1)
#define AW8692X_TRIG3_NEG_ENABLE		(1)
#define AW8692X_TRIG1_NEG_DISABLE		(0)
#define AW8692X_TRIG2_NEG_DISABLE		(0)
#define AW8692X_TRIG3_NEG_DISABLE		(0)

#define AW8692X_TRIG1_NEG_SEQ			(2)
#define AW8692X_TRIG2_NEG_SEQ			(2)
#define AW8692X_TRIG3_NEG_SEQ			(2)

#define AW8692X_TRIG1_BRK_ENABLE		(1)
#define AW8692X_TRIG2_BRK_ENABLE		(1)
#define AW8692X_TRIG3_BRK_ENABLE		(1)
#define AW8692X_TRIG1_BRK_DISABLE		(0)
#define AW8692X_TRIG2_BRK_DISABLE		(0)
#define AW8692X_TRIG3_BRK_DISABLE		(0)

#define AW8692X_TRIG1_BST_ENABLE		(1)
#define AW8692X_TRIG2_BST_ENABLE		(1)
#define AW8692X_TRIG3_BST_ENABLE		(1)
#define AW8692X_TRIG1_BST_DISABLE		(0)
#define AW8692X_TRIG2_BST_DISABLE		(0)
#define AW8692X_TRIG3_BST_DISABLE		(0)

/*********************************************************
 *
 * haptic config (AW869X)
 *
 *********************************************************/
#define AW869X_MAX_BST_VOL			(0x1F)
#define AW869X_F0_COEFF				(260)
#define AW869X_TEST				(0x12)
#define AW869X_R_SPARE				(0x68)

#ifdef LRA_0619
#ifdef OPLUS_FEATURE_CHG_BASIC
#define AW869X_CONT_DRV_LVL			(52)
#else
#define AW869X_CONT_DRV_LVL			(105)
#endif
#define AW869X_CONT_DRV_LVL_OV			(125)
#define AW869X_CONT_TD				(0x009A)
#define AW869X_CONT_ZC_THR			(0x0FF1)
#define AW869X_CONT_NUM_BRK			(3)
#endif

#ifdef LRA_0832
#define AW869X_CONT_DRV_LVL			(125)
#define AW869X_CONT_DRV_LVL_OV			(155)
#define AW869X_CONT_TD				(0x006C)
#define AW869X_CONT_ZC_THR			(0x0FF1)
#define AW869X_CONT_NUM_BRK			(3)
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define AW869X_0832_F0_PRE			(2350)
#define AW869X_0832_F0_CALI_PERCEN		(7)
#define AW869X_0832_CONT_DRV_LVL		(105)
#define AW869X_0832_CONT_DRV_LVL_OV		(125)
#define AW869X_0832_CONT_TD			(0x006c)
#define AW869X_0832_CONT_ZC_THR			(0x0ff1)
#define AW869X_0832_CONT_NUM_BRK		(3)

#define AW869X_0815_F0_PRE			(1700)
#define AW869X_0815_F0_CALI_PERCEN		(7)
#define AW869X_0815_CONT_DRV_LVL		(60)
#define AW869X_0815_CONT_DRV_LVL_OV		(125)
#define AW869X_0815_CONT_TD			(0x009a)
#define AW869X_0815_CONT_ZC_THR			(0x0ff1)
#define AW869X_0815_CONT_NUM_BRK		(3)

#define AW869X_081538_F0_PRE			(1500)
#define AW869X_081538_F0_CALI_PERCEN		(7)
#define AW869X_081538_CONT_DRV_LVL		(118)
#define AW869X_081538_CONT_DRV_LVL_OV		(118)
#define AW869X_081538_CONT_TD			(0x009a)
#define AW869X_081538_CONT_ZC_THR		(0x0ff1)
#define AW869X_081538_CONT_NUM_BRK		(3)
#endif


#define AW869X_TRG1_ENABLE			(1)
#define AW869X_TRG2_ENABLE			(1)
#define AW869X_TRG3_ENABLE			(1)

#define AW869X_TRG1_DUAL_EDGE			(1)
#define AW869X_TRG2_DUAL_EDGE			(1)
#define AW869X_TRG3_DUAL_EDGE			(1)

#define AW869X_TRG1_DEFAULT_LEVEL		(1)
#define AW869X_TRG2_DEFAULT_LEVEL		(1)
#define AW869X_TRG3_DEFAULT_LEVEL		(1)

#define AW869X_TRG1_FIRST_EDGE_SEQ		(1)
#define AW869X_TRG1_SECOND_EDGE_SEQ		(2)
#define AW869X_TRG2_FIRST_EDGE_SEQ		(1)
#define AW869X_TRG2_SECOND_EDGE_SEQ		(2)
#define AW869X_TRG3_FIRST_EDGE_SEQ		(1)
#define AW869X_TRG3_SECOND_EDGE_SEQ		(2)
/*********************************************************
 *
 * AW869X
 *
 *********************************************************/
#define AW869X_REG_SUM				(80)
#define AW869X_VBAT_MAX				(4500)
#define AW869X_LRA_FORMULA(lra_code)		(298 * (lra_code))
#define AW869X_VBAT_FORMULA(vbat_code)		(6100 * (vbat_code) / 256)
#define AW869X_RAM_ADDR_H(base_addr)		((base_addr) >> 8)
#define AW869X_RAM_ADDR_L(base_addr)		((base_addr) & 0x00FF)
#define AW869X_FIFO_AE_ADDR_H(base_addr)	(((base_addr) >> 1) >> 8)
#define AW869X_FIFO_AE_ADDR_L(base_addr)	(((base_addr) >> 1) & 0x00ff)
#define AW869X_FIFO_AF_ADDR_H(base_addr)	(((base_addr) - (base_addr >> 2)) >> 8)
#define AW869X_FIFO_AF_ADDR_L(base_addr)	(((base_addr) - ((base_addr) >> 2)) & 0x00ff)
/*********************************************************
 *
 * AW8692X
 *
 *********************************************************/
#define AW8692X_REG_SUM				(88)
#define AW8692X_VBAT_MAX			(5500)
#define AW8692X_LRA_FORMULA(lra, d2s_gain)	((6075 * 100 * (lra)) / \
						(1024 * (d2s_gain)))
#define AW8692X_VBAT_FORMULA(vbat)		(5 * 1215 * (vbat) / 1024)
#define AW8692X_SET_RAMADDR_H(base_addr)	((base_addr) >> 8)
#define AW8692X_SET_RAMADDR_L(base_addr)	((base_addr) & 0x00FF)
#define AW8692X_SET_BASEADDR_H(base_addr)	((base_addr) >> 8)
#define AW8692X_SET_BASEADDR_L(base_addr)	((base_addr) & 0x00FF)
#define AW8692X_SET_FIFO_AE_ADDR_H(base_addr)	((((base_addr) >> 1) >> 4) & 0xF0)
#define AW8692X_SET_FIFO_AE_ADDR_L(base_addr)	(((base_addr) >> 1) & 0x00ff)
#define AW8692X_SET_FIFO_AF_ADDR_H(base_addr)	((((base_addr) - (base_addr >> 2)) >> 8) & 0x0F)
#define AW8692X_SET_FIFO_AF_ADDR_L(base_addr)	(((base_addr) - ((base_addr) >> 2)) & 0x00ff)
/*********************************************************
 *
 * Log Format
 *
 *********************************************************/
#define aw_dev_err(format, ...) \
	pr_err("[haptic_hv]" format, ##__VA_ARGS__)

#define aw_dev_info(format, ...) \
	pr_info("[haptic_hv]" format, ##__VA_ARGS__)

#define aw_dev_dbg(format, ...) \
	pr_debug("[haptic_hv]" format, ##__VA_ARGS__)

/*********************************************************
 *
 * Enum Define
 *
 *********************************************************/
enum aw_haptic_flags {
	AW_FLAG_NONR = 0,
	AW_FLAG_SKIP_INTERRUPTS = 1,
};

enum aw_haptic_wav_seq_flags {
	AW_INDEX = 0,
	AW_SEQ = 1,
};

enum aw_haptic_work_mode {
	AW_RAM_LOOP_MODE = 0,
	AW_CONT_MODE = 1,
	AW_RAM_MODE = 2,
	AW_RTP_MODE = 3,
	AW_TRIG_MODE = 4,
	AW_STANDBY_MODE = 5,
};

enum aw_haptic_bst_mode {
	AW_BST_BYPASS_MODE = 0,
	AW_BST_BOOST_MODE = 1,
};

enum aw_haptic_bst_pc {
	AW_BST_PC_L1 = 0,
	AW_BST_PC_L2 = 1,
};

enum aw_haptic_cont_vbat_comp_mode {
	AW_CONT_VBAT_SW_COMP_MODE = 0,
	AW_CONT_VBAT_HW_COMP_MODE = 1,
};

enum aw_haptic_ram_vbat_comp_mode {
	AW_RAM_VBAT_COMP_DISABLE = 0,
	AW_RAM_VBAT_COMP_ENABLE = 1,
};

enum aw_haptic_f0_flag {
	AW_LRA_F0 = 0,
	AW_CALI_F0 = 1,
};

enum aw_haptic_pwm_mode {
	AW_PWM_48K = 0,
	AW_PWM_24K = 1,
	AW_PWM_12K = 2,
};

enum aw_haptic_play {
	AW_PLAY_NULL = 0,
	AW_PLAY_ENABLE = 1,
	AW_PLAY_STOP = 2,
	AW_PLAY_GAIN = 8,
};

enum aw_haptic_cmd {
	AW_CMD_NULL = 0,
	AW_CMD_ENABLE = 1,
	AW_CMD_HAPTIC = 0x0f,
	AW_CMD_TP = 0x10,
	AW_CMD_SYS = 0xf0,
	AW_CMD_STOP = 255,
};

enum aw_haptic_cali_lra {
	AW_WRITE_ZERO = 0,
	AW_F0_CALI_LRA = 1,
	AW_OSC_CALI_LRA = 2,
};

enum aw_haptic_awrw_flag {
	AW_SEQ_WRITE = 0,
	AW_SEQ_READ = 1,
};

enum aw_haptic_vibration_style {
	AW_HAPTIC_VIBRATION_CRISP_STYLE = 0,
	AW_HAPTIC_VIBRATION_SOFT_STYLE = 1,
};

enum aw_haptic_read_write {
	AW_HAPTIC_CMD_READ_REG = 0,
	AW_HAPTIC_CMD_WRITE_REG = 1,
};
/*********************************************************
 *
 * Enum aw8692x
 *
 *********************************************************/
enum aw8692x_haptic_rck_fre {
	AW8692X_RCK_FRE_24K,
	AW8692X_RCK_FRE_32K,
	AW8692X_RCK_FRE_48K,
	AW8692X_RCK_FRE_96K,
};

enum aw8692x_haptic_trig {
	AW8692X_TRIG1,
	AW8692X_TRIG2,
	AW8692X_TRIG3,
};

/*********************************************************
 *
 * Struct Define
 *
 *********************************************************/
struct trig {
	/* AW869X */
	uint8_t enable;
	uint8_t dual_edge;
	uint8_t frist_seq;
	uint8_t second_seq;
	uint8_t default_level;

	/* AW869XX */
	uint8_t trig_brk;
	uint8_t trig_bst;
	uint8_t trig_level;
	uint8_t trig_polar;
	uint8_t pos_enable;
	uint8_t neg_enable;
	uint8_t pos_sequence;
	uint8_t neg_sequence;
};

struct aw_haptic_ram {
	uint32_t len;
	uint32_t check_sum;
	uint32_t base_addr;
	uint8_t ram_num;
	uint8_t version;
	uint8_t ram_shift;
	uint8_t baseaddr_shift;
};

struct aw_haptic_ctr {
	uint8_t cnt;
	uint8_t cmd;
	uint8_t play;
	uint8_t loop;
	uint8_t gain;
	uint8_t wavseq;
	struct list_head list;
};

struct aw_i2c_info {
	uint32_t flag;
	uint32_t reg_num;
	uint8_t *reg_data;
};

struct fileops {
	uint8_t cmd;
	uint8_t reg;
	uint8_t ram_addrh;
	uint8_t ram_addrl;
};

struct aw_vmax_map {
	int level;
	int vmax;
	int gain;
};

struct aw_haptic_audio {
	int tz_num;
	int tz_cnt_thr;
	int tz_cnt_max;
	int tz_high_num;
	int delay_val;
	int timer_val;
	uint32_t uevent_report_flag;
	uint32_t hap_cnt_outside_tz;
	uint32_t hap_cnt_max_outside_tz;
	struct mutex lock;
	struct hrtimer timer;
	struct work_struct work;
	struct list_head list;
	struct list_head ctr_list;
	struct aw_haptic_ctr ctr;
};

struct aw_haptic_dts_info {
	uint8_t mode;
	uint8_t f0_cali_percent;
	uint8_t max_bst_vol;
	uint32_t f0_pre;

	/* AW869X */
	uint8_t tset;
	uint8_t r_spare;
	uint8_t sw_brake;
	uint8_t parameter1;
	uint8_t cont_drv_lvl;
	uint8_t cont_num_brk;
	uint8_t cont_drv_lvl_ov;
	uint8_t bstdbg[6];
	uint8_t bemf_config[4];
	uint8_t duration_time[3];
	uint8_t f0_trace_parameter[4];
	uint32_t f0_coeff;
	uint32_t cont_td;
	uint32_t cont_zc_thr;

	/* AW869XX */
	uint8_t d2s_gain;
	uint8_t brk_bst_md;
	uint8_t bst_vol_ram;
	uint8_t bst_vol_rtp;
	uint8_t bst_vol_default;
	uint8_t cont_tset;
	uint8_t cont_drv1_lvl;
	uint8_t cont_drv2_lvl;
	uint8_t cont_wait_num;
	uint8_t cont_brk_time;
	uint8_t cont_bemf_set;
	uint8_t cont_brk_gain;
	uint8_t cont_drv1_time;
	uint8_t cont_drv2_time;
	uint8_t cont_drv_width;
	uint8_t cont_track_margin;
	uint8_t cont_bst_brk_gain;
	uint8_t bstcfg[5];
	uint8_t prctmode[3];
	uint8_t sine_array[4];
	uint8_t trig_cfg[24];
	bool is_enabled_auto_bst;
	bool is_enabled_i2s;
	bool is_enabled_one_wire;
};

struct aw_haptic {
	/* AW869X */
	uint32_t interval_us;

	/* AW869XX */
	bool i2s_config;
	bool rtp_init;
	bool ram_init;
	bool haptic_ready;
	bool audio_ready;

	/* COMMON */
	uint8_t flags;
	uint8_t bst_pc;
	uint8_t play_mode;
	uint8_t auto_boost;
	uint8_t max_pos_beme;
	uint8_t max_neg_beme;
	uint8_t activate_mode;
	uint8_t ram_vbat_comp;
	uint8_t rtp_routine_on;
	uint8_t haptic_real_f0;
	uint8_t vibration_style;
	uint8_t seq[AW_SEQUENCER_SIZE];
	uint8_t loop[AW_SEQUENCER_SIZE];

	int vmax;
	int gain;
	int rate;
	int width;
	int state;
	int index;
	int chipid;
	int sysclk;
	int irq_gpio;
	int duration;
	int amplitude;
	int reset_gpio;
	int device_id;
	int pre_haptic_number;


	uint32_t f0;
	uint32_t lra;
	uint32_t vbat;
	uint32_t cont_f0;
	uint32_t rtp_cnt;
	uint32_t rtp_len;
	uint32_t gun_type;
	uint32_t bullet_nr;
	uint32_t gun_mode;
	uint32_t theory_time;
	uint32_t f0_cali_data;
	uint32_t rtp_file_num;
	uint32_t timeval_flags;
	uint32_t osc_cali_data;
	uint32_t rtp_loop;
	uint32_t rtp_cycle_flag;
	uint32_t osc_cali_flag;
	uint32_t ram_test_flag_0;
	uint32_t ram_test_flag_1;
	uint32_t rtp_serial[AW_RTP_NUM];
	unsigned long microsecond;

	cdev_t vib_dev;

	ktime_t kend;
	ktime_t kstart;
	ktime_t kcurrent_time;
	ktime_t kpre_enter_time;

	struct device *dev;
	struct i2c_client *i2c;
	struct mutex lock;
	struct mutex qos_lock;
	struct mutex rtp_lock;
	struct hrtimer timer;
	struct work_struct rtp_work;
	struct work_struct rtp_single_cycle_work;
	struct work_struct rtp_regroup_work;
	struct delayed_work ram_work;
	struct work_struct vibrator_work;
	struct aw_haptic_ram ram;
	struct aw_haptic_dts_info info;
	struct aw_haptic_audio haptic_audio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_state;
	struct aw_haptic_func *func;
	struct aw_i2c_info i2c_info;
	struct trig trig[AW_TRIG_NUM];
	struct fileops fileops;
	struct proc_dir_entry *prEntry_da;
	struct proc_dir_entry *prEntry_tmp;
#ifdef AAC_RICHTAP
	uint8_t *rtp_ptr;
	struct mmap_buf_format *start_buf;
	struct work_struct haptic_rtp_work;
	/* wait_queue_head_t doneQ; */
	bool done_flag;
	bool haptic_rtp_mode;
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	struct work_struct  motor_old_test_work;
	unsigned int motor_old_test_mode;
#endif

};

struct aw_haptic_container {
	int len;
	uint8_t data[];
};

struct aw_haptic_func {
	int (*creat_node)(struct aw_haptic *);
	int (*get_irq_state)(struct aw_haptic *);
	int (*juge_rtp_going)(struct aw_haptic *);
	int (*container_update)(struct aw_haptic *,
				struct aw_haptic_container *);
	int (*get_f0)(struct aw_haptic *);
	void (*play_stop)(struct aw_haptic *);
	void (*get_vbat)(struct aw_haptic *);
	void (*cont_config)(struct aw_haptic *);
	void (*offset_cali)(struct aw_haptic *);
	void (*read_f0)(struct aw_haptic *);
	void (*play_go)(struct aw_haptic *, bool);
	void (*ram_init)(struct aw_haptic *, bool);
	void (*set_bst_peak_cur)(struct aw_haptic *);
	void (*get_lra_resistance)(struct aw_haptic *);
	void (*set_pwm)(struct aw_haptic *, uint8_t);
	void (*set_gain)(struct aw_haptic *, uint8_t);
	void (*play_mode)(struct aw_haptic *, uint8_t);
	void (*set_bst_vol)(struct aw_haptic *, uint8_t);
	void (*set_repeat_seq)(struct aw_haptic *, uint8_t);
	void (*auto_bst_enable)(struct aw_haptic *, uint8_t);
	void (*vbat_mode_config)(struct aw_haptic *, uint8_t);
	void (*set_wav_seq)(struct aw_haptic *, uint8_t, uint8_t);
	void (*set_wav_loop)(struct aw_haptic *, uint8_t, uint8_t);
	void (*set_rtp_data)(struct aw_haptic *, uint8_t *, uint32_t);
	void (*protect_config)(struct aw_haptic *, uint8_t, uint8_t);
	void (*parse_dt)(struct device *, struct aw_haptic *,
			 struct device_node *);
	void (*trig_init)(struct aw_haptic *);
	void (*irq_clear)(struct aw_haptic *);
	void (*set_ram_addr)(struct aw_haptic *, uint32_t);
	void (*misc_para_init)(struct aw_haptic *);
	void (*interrupt_setup)(struct aw_haptic *);
	void (*set_rtp_aei)(struct aw_haptic *, bool);
	void (*upload_lra)(struct aw_haptic *, uint32_t);
	void (*bst_mode_config)(struct aw_haptic *, uint8_t);
	size_t (*get_wav_loop)(struct aw_haptic *, char *);
	ssize_t (*get_reg)(struct aw_haptic *, ssize_t, char *);
	uint8_t (*get_prctmode)(struct aw_haptic *);
	void (*get_ram_data)(struct aw_haptic *, uint8_t *, uint32_t);
	void (*get_first_wave_addr)(struct aw_haptic *, uint8_t *);
	uint8_t (*get_glb_state)(struct aw_haptic *);
	uint8_t (*get_chip_state)(struct aw_haptic *);
	uint8_t (*read_irq_state)(struct aw_haptic *);
	uint8_t (*get_osc_status)(struct aw_haptic *);
	uint8_t (*rtp_get_fifo_afs)(struct aw_haptic *);
	uint8_t (*rtp_get_fifo_aes)(struct aw_haptic *);
	void (*get_wav_seq)(struct aw_haptic *, uint8_t *, uint8_t);
	unsigned long (*get_theory_time)(struct aw_haptic *);
	void (*haptic_value_init)(struct aw_haptic *);
	void (*set_ram_data)(struct aw_haptic *, uint8_t *, uint32_t);
	void (*dump_rtp_regs)(struct aw_haptic *);
	void (*aw_test)(struct aw_haptic *);
	int (*check_qualify)(struct aw_haptic *aw_haptic);

};

/*********************************************************
 *
 * ioctl
 *
 ********************************************************/
struct aw_seq_loop {
	unsigned char loop[AW_SEQUENCER_SIZE];
};

struct aw_que_seq {
	unsigned char index[AW_SEQUENCER_SIZE];
};

#define AW_HAPTIC_IOCTL_MAGIC		'h'

#define AW_HAPTIC_SET_QUE_SEQ		_IOWR(AW_HAPTIC_IOCTL_MAGIC, 1, struct aw_que_seq*)
#define AW_HAPTIC_SET_SEQ_LOOP		_IOWR(AW_HAPTIC_IOCTL_MAGIC, 2, struct aw_seq_loop*)
#define AW_HAPTIC_PLAY_QUE_SEQ		_IOWR(AW_HAPTIC_IOCTL_MAGIC, 3, unsigned int)
#define AW_HAPTIC_SET_BST_VOL		_IOWR(AW_HAPTIC_IOCTL_MAGIC, 4, unsigned int)
#define AW_HAPTIC_SET_BST_PEAK_CUR	_IOWR(AW_HAPTIC_IOCTL_MAGIC, 5, unsigned int)
#define AW_HAPTIC_SET_GAIN		_IOWR(AW_HAPTIC_IOCTL_MAGIC, 6, unsigned int)
#define AW_HAPTIC_PLAY_REPEAT_SEQ	_IOWR(AW_HAPTIC_IOCTL_MAGIC, 7, unsigned int)


/*********************************************************
 *
 * Function Call
 *
 *********************************************************/
extern struct aw_haptic_func aw869x_func_list;
extern struct aw_haptic_func aw8692x_func_list;
extern struct pm_qos_request aw_pm_qos_req_vb;


extern int i2c_r_bytes(struct aw_haptic *, uint8_t, uint8_t *, uint32_t);
extern int i2c_w_bytes(struct aw_haptic *, uint8_t, uint8_t *, uint32_t);
extern int i2c_w_bits(struct aw_haptic *, uint8_t, uint32_t, uint8_t);

#endif
