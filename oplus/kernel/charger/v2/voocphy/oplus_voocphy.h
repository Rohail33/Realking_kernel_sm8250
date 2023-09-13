//SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#ifndef _OPLUS_VOOCPHY_H_
#define _OPLUS_VOOCPHY_H_

#include <linux/workqueue.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
#include <linux/wakelock.h>
#endif
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>

#define IRQ_EVNET_NUM				8
#define BIDIRECT_IRQ_EVNET_NUM			12
#define IRQ_EVNET_NUM_HL7138			16
#define DEFAULT_CP_IBUS_DEVATION		800
#define MAX_IGNORE				6
#define FIRST_FRAME				0xA8
#define SVOOC_INIT_VBUS_VOL_LOW			5000
#define SVOOC_INIT_VBUS_VOL_HIGH		8500
#define SVOOC_COPYCAT_VBUS_DOWN_DELAY_MS	500000

/* int flag */
#define VOUT_OVP_FLAG_MASK		BIT(7)
#define VBAT_OVP_FLAG_MASK		BIT(6)
#define IBAT_OCP_FLAG_MASK		BIT(5)
#define VBUS_OVP_FLAG_MASK		BIT(4)
#define IBUS_OCP_FLAG_MASK		BIT(3)
#define IBUS_UCP_FLAG_MASK		BIT(2)
#define ADAPTER_INSERT_FLAG_MASK	BIT(1)
#define VBAT_INSERT_FLAG_MASK		BIT(0)

/* bidirect int flag */
#define SC_EN_STAT_FLAG_MASK		BIT(0)

#define V2X_OVP_FLAG_MASK		BIT(7)
#define V1X_OVP_FLAG_MASK		BIT(6)
#define VAC_OVP_FLAG_MASK		BIT(5)
#define FWD_OCP_FLAG_MASK		BIT(4)
#define RVS_OCP_FLAG_MASK		BIT(3)
#define TSHUT_FLAG_MASK			BIT(0)

#define VAC2V2X_OVP_FLAG_MASK		BIT(7)
#define VAC2V2X_UVP_FLAG_MASK		BIT(6)
#define V1X_ISS_OPP_FLAG_MASK		BIT(4)
#define WD_TIMEOUT_FLAG_MASK		BIT(1)
#define LNC_SS_TIMEOUT_FLAG_MASK	BIT(0)

/* vooc flag */
#define PULSE_FILTERED_STAT_MASK	BIT(7)
#define NINTH_CLK_ERR_FLAG_MASK		BIT(6)
#define TXSEQ_DONE_FLAG_MASK		BIT(5)
#define ERR_TRANS_DET_FLAG_MASK		BIT(4)
#define TXDATA_WR_FAIL_FLAG_MASK	BIT(3)
#define RX_START_FLAG_MASK		BIT(2)
#define RXDATA_DONE_FLAG_MASK		BIT(1)
#define TXDATA_DONE_FALG_MASK		BIT(0)
#define ADAPTER_CHECK_VOOC_HEAD_TIMES	0x2
#define ADAPTER_CHECK_CMD_DATA_TIMES	0x2
#define FAST_CHARGER_MOS_DISABLE	0

enum {
	VOOC_CHARGER_MODE,
	HEADPHONE_MODE,
	NORMAL_CHARGER_MODE,
};

enum {
	ADC_AUTO_MODE,
	ADC_FORCEDLY_ENABLED,
	ADC_FORCEDLY_DISABLED_10,
	ADC_FORCEDLY_DISABLED_11,
};

enum {
	VOOC_VBUS_UNKNOW,	/* unknow adata vbus status */
	VOOC_VBUS_NORMAL,	/* vooc vbus in normal status */
	VOOC_VBUS_HIGH,		/* vooc vbus in high status */
	VOOC_VBUS_LOW,		/* vooc vbus in low status */
	VOOC_VBUS_INVALID,	/* vooc vbus in invalid status */
};

enum {
	VOOCPHY_BATT_TEMP_HIGH,
	VOOCPHY_BATT_TEMP_WARM,
	VOOCPHY_BATT_TEMP_NORMAL,
	VOOCPHY_BATT_TEMP_LITTLE_COOL,
	VOOCPHY_BATT_TEMP_COOL,
	VOOCPHY_BATT_TEMP_LITTLE_COLD,
	VOOCPHY_BATT_TEMP_COLD,
	VOOCPHY_BATT_TEMP_REMOVE,
};

enum {
	BATT_SOC_0_TO_50,
	BATT_SOC_50_TO_75,
	BATT_SOC_75_TO_85,
	BATT_SOC_85_TO_90,
	BATT_SOC_90_TO_100,
};

enum {
	VOOCPHY_SUCCESS,
	/*Level: Warning          0x1XX*/
	VOOCPHY_WARN_NO_OP     = 0x100,            /*Warn: No Operation*/
	/*Level: Error            0x2XX*/
	VOOCPHY_EFAILED        = 0x200,            /*Error: Operation Failed*/
	VOOCPHY_EUNSUPPORTED,                      /*Error: Not Supported*/
	VOOCPHY_EINVALID,                          /*Error: Invalid Argument*/
	VOOCPHY_EINVALID_FP,                       /*Error: Invalid Function Pointer*/
	VOOCPHY_ENOTALLOWED,                       /*Error: Operation Not Allowed*/
	VOOCPHY_EMEM,                              /*Error: Not Enough Memory to Perform Operation*/
	VOOCPHY_EBADPTR,                           /*Error: NULL Pointer*/
	VOOCPHY_ETESTMODE,                         /*Error: Test Mode*/
	VOOCPHY_EFATAL          = -1               /*Error: Fatal*/
};

enum {
	ADAPTER_VOOC20,
	ADAPTER_VOOC30,
	ADAPTER_SVOOC,
};

enum {
	VOOC_THREAD_TIMER_AP,
	VOOC_THREAD_TIMER_CURR,
	VOOC_THREAD_TIMER_SOC,
	VOOC_THREAD_TIMER_VOL,
	VOOC_THREAD_TIMER_TEMP,
	VOOC_THREAD_TIMER_BTB,
	VOOC_THREAD_TIMER_SAFE,
	VOOC_THREAD_TIMER_COMMU,
	VOOC_THREAD_TIMER_DISCON,
	VOOC_THREAD_TIMER_FASTCHG_CHECK,
	VOOC_THREAD_TIMER_TEST_CHECK,
	VOOC_THREAD_TIMER_IBUS_CHECK,
	VOOC_THREAD_TIMER_INVALID,
	VOOC_THREAD_TIMER_MAX = VOOC_THREAD_TIMER_INVALID
};

enum {
	/*General Event*/
	VOOCPHY_REQUEST_RESET,
	VOOCPHY_REQUEST_UPDATE_DATA,
	VOOCPHY_REQUEST_INVALID
};

enum {
	/*General Event*/
	SETTING_REASON_PROBE,
	SETTING_REASON_RESET,
	SETTING_REASON_SVOOC,
	SETTING_REASON_VOOC,
	SETTING_REASON_5V2A,
	SETTING_REASON_PDQC,
	SETTING_REASON_COPYCAT_SVOOC
};

struct irqinfo {
        int mask;
        char except_info[30];
        int mark_except;
};

static struct irqinfo vooc_flag[IRQ_EVNET_NUM] = {
	{PULSE_FILTERED_STAT_MASK       , "PULSE_FILTERED_STAT", 1},
	{NINTH_CLK_ERR_FLAG_MASK        , "NINTH_CLK_ERR_FLAG", 1},
	{TXSEQ_DONE_FLAG_MASK           , "TXSEQ_DONE_FLAG", 0},
	{ERR_TRANS_DET_FLAG_MASK        , "ERR_TRANS_DET_FLAG", 1},
	{TXDATA_WR_FAIL_FLAG_MASK       , "TXDATA_WR_FAIL_FLAG", 1},
	{RX_START_FLAG_MASK                     , "RX_START_FLAG", 0},
	{RXDATA_DONE_FLAG_MASK          , "RXDATA_DONE_FLAG", 0},
	{TXDATA_DONE_FALG_MASK          , "TXDATA_DONE_FALG", 0},
};

static struct irqinfo int_flag[IRQ_EVNET_NUM] = {
	{VOUT_OVP_FLAG_MASK, "VOUT_OVP", 0},
	{VBAT_OVP_FLAG_MASK, "VBAT_OVP", 1},
	{IBAT_OCP_FLAG_MASK, "IBAT_OCP", 1},
	{VBUS_OVP_FLAG_MASK, "VBUS_OVP", 1},
	{IBUS_OCP_FLAG_MASK, "IBUS_OCP", 1},
	{IBUS_UCP_FLAG_MASK, "IBUS_UCP", 1},
	{ADAPTER_INSERT_FLAG_MASK, "ADAPTER_INSERT", 1},
	{VBAT_INSERT_FLAG_MASK,   "VBAT_INSERT", 1},
};

static struct irqinfo bidirect_int_flag[BIDIRECT_IRQ_EVNET_NUM] = {
	{SC_EN_STAT_FLAG_MASK,	"SC_EN_STAT", 1},/*0A*/
	{V2X_OVP_FLAG_MASK, "V2X_OVP", 1},/*0C*/
	{V1X_OVP_FLAG_MASK, "V1X_OVP ", 1},
	{VAC_OVP_FLAG_MASK, "VAC_OVP", 1},
	{FWD_OCP_FLAG_MASK, "FWD_OCP", 1},
	{RVS_OCP_FLAG_MASK, "RVS_OCP", 1},
	{TSHUT_FLAG_MASK,	"TSHUT", 1},
	{VAC2V2X_OVP_FLAG_MASK, "VAC2V2X_OVP", 1},/*0E*/
	{VAC2V2X_UVP_FLAG_MASK,	"VAC2V2X_UVP", 1},
	{V1X_ISS_OPP_FLAG_MASK, "V1X_ISS_OPP", 1},
	{WD_TIMEOUT_FLAG_MASK,	"WD_TIMEOUT", 1},
	{LNC_SS_TIMEOUT_FLAG_MASK,	"LNC_SS_TIMEOUT", 1},
};

#define VOOC_DEFAULT_HEAD			SVOOC_INVERT_HEAD

/* 1.R_DET */
#define R_DET_BIT4                              1
#define R_DET_BIT5                              1
#define R_DET_BIT6                              0
#define R_DET_BIT7                              0
#define R_DET_BIT8                              0
#define R_DET_BIT9                              0


#define VOOC2_CIRCUIT_R_H_DEF		((R_DET_BIT9 << TX1_DET_BIT1_SHIFT) | (R_DET_BIT8 << TX1_DET_BIT0_SHIFT))
#define VOOC2_CIRCUIT_R_L_DEF		((R_DET_BIT5 << TX0_DET_BIT2_SHIFT) | (R_DET_BIT6 << TX0_DET_BIT3_SHIFT) \
						| (R_DET_BIT7 << TX0_DET_BIT4_SHIFT))
#define NO_VOOC2_CIRCUIT_R_H_DEF	((R_DET_BIT9 << TX1_DET_BIT1_SHIFT) | (R_DET_BIT8 << TX1_DET_BIT0_SHIFT))
#define NO_VOOC2_CIRCUIT_R_L_DEF	((R_DET_BIT4 << TX0_DET_BIT1_SHIFT) | (R_DET_BIT5 << TX0_DET_BIT2_SHIFT) \
						| (R_DET_BIT6 << TX0_DET_BIT3_SHIFT) | (R_DET_BIT7 << TX0_DET_BIT4_SHIFT))
#define BIDRECT_CIRCUIT_R_L_DEF	((R_DET_BIT5 << TX0_DET_BIT2_SHIFT) \
							| (R_DET_BIT6 << TX0_DET_BIT3_SHIFT) | (R_DET_BIT7 << TX0_DET_BIT4_SHIFT))
#define BURR_VOOC2_CIRCUIT_R		((R_DET_BIT4 << 4) | (R_DET_BIT5 << 3) | (R_DET_BIT6 << 2) \
						|(R_DET_BIT7 << 1) | (R_DET_BIT8))

#define R_DET_BIT_VOOC30_4			0
#define R_DET_BIT_VOOC30_5			1
#define R_DET_BIT_VOOC30_6			1
#define R_DET_BIT_VOOC30_7			0
#define R_DET_BIT_VOOC30_8			0
#define R_DET_BIT_VOOC30_9			0
#define NO_VOOC3_CIRCUIT_R_H_DEF	((R_DET_BIT_VOOC30_9 << TX1_DET_BIT1_SHIFT) | (R_DET_BIT_VOOC30_8 << TX1_DET_BIT0_SHIFT))
#define NO_VOOC3_CIRCUIT_R_L_DEF ((R_DET_BIT_VOOC30_4 << TX0_DET_BIT1_SHIFT) | (R_DET_BIT_VOOC30_5 << TX0_DET_BIT2_SHIFT) \
									| (R_DET_BIT_VOOC30_6 << TX0_DET_BIT3_SHIFT) | (R_DET_BIT_VOOC30_7 << TX0_DET_BIT4_SHIFT))

#define R_DET_PARALLEL_BIT4                              1
#define R_DET_PARALLEL_BIT5                              1
#define R_DET_PARALLEL_BIT6                              0
#define R_DET_PARALLEL_BIT7                              1
#define R_DET_PARALLEL_BIT8                              1
#define R_DET_PARALLEL_BIT9                              0


#define VOOC2_CIRCUIT_PARALLEL_R_H_DEF		((R_DET_PARALLEL_BIT9 << TX1_DET_BIT1_SHIFT) | (R_DET_PARALLEL_BIT8 << TX1_DET_BIT0_SHIFT))
#define VOOC2_CIRCUIT_PARALLEL_R_L_DEF		((R_DET_PARALLEL_BIT5 << TX0_DET_BIT2_SHIFT) | (R_DET_PARALLEL_BIT6 << TX0_DET_BIT3_SHIFT) \
						| (R_DET_PARALLEL_BIT7 << TX0_DET_BIT4_SHIFT))
#define NO_VOOC2_CIRCUIT_PARALLEL_R_H_DEF	((R_DET_PARALLEL_BIT9 << TX1_DET_BIT1_SHIFT) | (R_DET_PARALLEL_BIT8 << TX1_DET_BIT0_SHIFT))
#define NO_VOOC2_CIRCUIT_PARALLEL_R_L_DEF	((R_DET_PARALLEL_BIT4 << TX0_DET_BIT1_SHIFT) | (R_DET_PARALLEL_BIT5 << TX0_DET_BIT2_SHIFT) \
						| (R_DET_PARALLEL_BIT6 << TX0_DET_BIT3_SHIFT) | (R_DET_PARALLEL_BIT7 << TX0_DET_BIT4_SHIFT))
#define BURR_VOOC2_CIRCUIT_PARALLEL_R		((R_DET_PARALLEL_BIT4 << 4) | (R_DET_PARALLEL_BIT5 << 3) | (R_DET_PARALLEL_BIT6 << 2) \
						|(R_DET_PARALLEL_BIT7 << 1) | (R_DET_PARALLEL_BIT8))

#define R_DET_PARALLEL_BIT_VOOC30_4			1
#define R_DET_PARALLEL_BIT_VOOC30_5			1
#define R_DET_PARALLEL_BIT_VOOC30_6			0
#define R_DET_PARALLEL_BIT_VOOC30_7			1
#define R_DET_PARALLEL_BIT_VOOC30_8			1
#define R_DET_PARALLEL_BIT_VOOC30_9			0
#define NO_VOOC3_CIRCUIT_PARALLEL_R_H_DEF	((R_DET_PARALLEL_BIT_VOOC30_9 << TX1_DET_BIT1_SHIFT) | (R_DET_PARALLEL_BIT_VOOC30_8 << TX1_DET_BIT0_SHIFT))
#define NO_VOOC3_CIRCUIT_PARALLEL_R_L_DEF ((R_DET_PARALLEL_BIT_VOOC30_4 << TX0_DET_BIT1_SHIFT) | (R_DET_PARALLEL_BIT_VOOC30_5 << TX0_DET_BIT2_SHIFT) \
									| (R_DET_PARALLEL_BIT_VOOC30_6 << TX0_DET_BIT3_SHIFT) | (R_DET_PARALLEL_BIT_VOOC30_7 << TX0_DET_BIT4_SHIFT))
/* 2.batt sys array*/
#define BATT_SYS_ARRAY 0x6
#define BATT_SYS_VOL_CUR_PAIR 0X0C

/* 3.allow fastchg temp range and soc rang*/
#define VOOC_LOW_TEMP						0
#define VOOC_HIGH_TEMP						430
#define VOOC_LOW_SOC						0
#define VOOC_HIGH_SOC						90

/* 4.setting fastchg temperature range*/
#define BATT_TEMP_OVER_L						45
#define BATT_TEMP_OVER_H						440

/* 5.setting up overcurrent protection Point */
#define VOOC_OVER_CURRENT_VALUE                      11500

/* 6.low current term condition */
#define LOW_CURRENT_TERM_LO                     900
#define LOW_CURRENT_TERM_VBAT_LO                4450
#define LOW_CURRENT_TERM_HI                     1000
#define LOW_CURRENT_TERM_VBAT_HI                4470

#define VOOC_TEMP_RANGE_THD	20
#define VOOC_TEMP_OVER_COUNTS	2

struct low_curr_full_condition{
	u32 curr;
	u32 vbatt;
};

/* 7.adjust current according to battery voltage */

/* 8.adapter copydat vol & curr thd*/
#define BATT_PWD_CURR_THD1						4300
#define BATT_PWD_VOL_THD1						4304

/* 9.Battery Charging Full Voltage */
#define BAT_FULL_1TIME_THD                      4490
#define BAT_FULL_NTIME_THD                      4490


/*littile cold temp range set charger current*/
#define TEMP0_2_TEMP45_FULL_VOLTAGE			4430

/* 10. cool temp range set charger current */
#define TEMP45_2_TEMP115_FULL_VOLTAGE			4430

/* 11. little cool temp range set charger current */

/* 12. according soc set charger current*/
#define VOOC_SOC_HIGH							75

/* 13. adjust current according to battery temp*/

/* 14.Fast charging timeout */
#define FASTCHG_TOTAL_TIME			100000
#define FASTCHG_3C_TIMEOUT                      3600
#define FASTCHG_2_9C_TIMEOUT                    3000
#define FASTCHG_2_8C_TIMEOUT                    2100

#define FASTCHG_3000MA_TIMEOUT                    72000
#define FASTCHG_2500MA_TIMEOUT                    67800
#define FASTCHG_2000MA_TIMEOUT                    60000


/* 15. ap request current_max & current_min */

/* 16. Fast charging hardware monitor arguements setup*/
#define VBATT_HW_OV_THD							(BAT_FULL_1TIME_THD + 50)
#define VBATT_HW_OV_ANA							(BAT_FULL_1TIME_THD + 50)
#define BATT_THOT_85C							0x05e5
#define CONN_THOT_85C							0x05e5

/* 17. force charger current for old vooc2.0*/
#define FORCE2A_VBATT_REPORT                    58
#define FORCE3A_VBATT_REPORT                    57

#define VOOC_THREAD_STK_SIZE		0x800
#define VOOC_WAIT_EVENT			(1 << VOOC_EVENT_INVALID) - 1
#define VOOC_AP_EVENT_TIME		2000
#define VOOC_CURR_EVENT_TIME		1000
#define VOOC_SOC_EVENT_TIME		10
#define VOOC_VOL_EVENT_TIME		500
#define VOOC_TEMP_EVENT_TIME		500
#define VOOC_BTB_EVENT_TIME		800
#define VOOC_BTB_OVER_EVENT_TIME	10000
#define VOOC_SAFE_EVENT_TIME		100
#define VOOC_COMMU_EVENT_TIME		200
#define VOOC_DISCON_EVENT_TIME		350
#define VOOC_FASTCHG_CHECK_TIME		5000
#define VOOC_TEST_CHECK_TIME		20
#define VOOC_IBUS_CHECK_TIME		100
#define VOOC_BASE_TIME_STEP		5

/* Convert Event enum to bitmask of event */
#define VOOC_EVENTTYPE_TO_EVENT(x) 		(1 << x)
#define VOOC_EVENT_EXIT_MASK			0x01
#define VOOC_EVENT_AP_TIMEOUT_MASK		0x02
#define VOOC_EVENT_CURR_TIMEOUT_MASK		0x04
#define VOOC_EVENT_SOC_TIMEOUT_MASK		0x08
#define VOOC_EVENT_VOL_TIMEOUT_MASK		0x10
#define VOOC_EVENT_TEMP_TIMEOUT_MASK		0x20
#define VOOC_EVENT_BTB_TIMEOUT_MASK		0x40
#define VOOC_EVENT_SAFE_TIMEOUT_MASK		0x80
#define VOOC_EVENT_COMMU_TIMEOUT_MASK		0x100
#define VOOC_EVENT_MONITOR_START_MASK		0x200
#define VOOC_EVENT_VBUS_VBATT_GET_MASK		0X400
#define VOOC_EVENT_DISCON_TIMEOUT_MASK		0X800
#define VOOC_EVENT_FASTCHG_CHECK_TIMEOUT_MASK	0X1000
#define VOOC_EVENT_CHG_OUT_CHECK_TIMEOUT_MASK	0X2000
#define VOOC_EVENT_TEST_TIMEOUT_MASK		0X4000

#define VOOCPHY_TX_LEN 2
#define VOOC_MESG_READ_MASK 0X1E
#define VOOC_MESG_MOVE_READ_MASK 0X0F

#define BIT_ACTIVE 0x1
#define BIT_SLEEP 0x0

#define RX_DET_BIT0_MASK		0x1
#define RX_DET_BIT1_MASK		0x2
#define RX_DET_BIT2_MASK		0x4
#define RX_DET_BIT3_MASK		0x8
#define RX_DET_BIT4_MASK		0x10
#define RX_DET_BIT5_MASK		0x20
#define RX_DET_BIT6_MASK		0x40
#define RX_DET_BIT7_MASK		0x80

#define VOOC2_HEAD					0x05
#define VOOC3_HEAD					0x03
#define SVOOC_HEAD					0x04
#define VOOC2_INVERT_HEAD			0x05
#define VOOC3_INVERT_HEAD			0x06
#define SVOOC_INVERT_HEAD			0x01
#define VOOC_HEAD_MASK			0xe0
#define VOOC_INVERT_HEAD_MASK 	0x07
#define VOOC_HEAD_SHIFT			0x05
#define VOOC_MOVE_HEAD_MASK		0x70
#define VOOC_MOVE_HEAD_SHIFT		0x04

#define TX0_DET_BIT3_TO_BIT6_MASK 0x78
#define TX0_DET_BIT3_TO_BIT7_MASK 0xf8
#define TX1_DET_BIT0_TO_BIT1_MASK 0x3
#define TX0_DET_BIT0_MASK 	0x1
#define TX0_DEF_BIT1_MASK	0x2
#define TX0_DET_BIT2_MASK	0x4
#define TX0_DET_BIT3_MASK	0x8
#define TX0_DET_BIT4_MASK 	0x10
#define TX0_DET_BIT5_MASK	0x20
#define TX0_DET_BIT6_MASK	0x40
#define TX0_DET_BIT7_MASK	0x80
#define TX1_DET_BIT0_MASK	0x1
#define TX1_DET_BIT1_MASK      0x2
#define TX0_DET_BIT0_SHIFT 0x0
#define TX0_DET_BIT1_SHIFT 0x1
#define TX0_DET_BIT2_SHIFT 0x2
#define TX0_DET_BIT3_SHIFT 0x3
#define TX0_DET_BIT4_SHIFT 0x4
#define TX0_DET_BIT5_SHIFT 0x5
#define TX0_DET_BIT6_SHIFT 0x6
#define TX0_DET_BIT7_SHIFT 0x7
#define TX1_DET_BIT0_SHIFT 0x0
#define TX1_DET_BIT1_SHIFT 0x1

#define VOOC_CMD_UNKNOW			0xff
#define VOOC_CMD_NULL				0x00
#define VOOC_CMD_GET_BATT_VOL       0x01
#define VOOC_CMD_IS_VUBS_OK         0x02
#define VOOC_CMD_ASK_CURRENT_LEVEL  0x03
#define VOOC_CMD_ASK_FASTCHG_ORNOT  0x04
#define VOOC_CMD_TELL_MODEL         0x05
#define VOOC_CMD_ASK_POWER_BANK    0x06
#define VOOC_CMD_TELL_FW_VERSION    0x07
#define VOOC_CMD_ASK_UPDATE_ORNOT   0x08
#define VOOC_CMD_TELL_USB_BAD       0x09
#define VOOC_CMD_ASK_AP_STATUS      0x0A
#define VOOC_CMD_ASK_CUR_UPDN       0x0B
#define VOOC_CMD_IDENTIFICATION     0x0C
#define VOOC_CMD_ASK_BAT_MODEL      0x0D
#define VOOC_CMD_ADAPTER_CHECK_COMMU_PROCESS 0xF1
#define VOOC_CMD_TELL_MODEL_PROCESS 0xF2
#define VOOC_CMD_ASK_BATT_SYS_PROCESS 0xF3

#define VOOC_CMD_RECEVICE_DATA_0E   0x0E
#define VOOC_CMD_RECEVICE_DATA_0F   0x0F


#define OPLUS_IS_VUBS_OK_PREDATA_VOOC20		((u16)0xa002)
#define OPLUS_IS_VUBS_OK_PREDATA_VOOC30		((u16)0xa001)
#define OPLUS_IS_VUBS_OK_PREDATA_SVOOC		((u16)0x2002)
#define OPLUS_BIDIRECTION_PREDATA_VOOC20	((u16)0xc002)
#define OPLUS_BIDIRECTION_PREDATA_VOOC30	((u16)0xc001)

#define ADAPTER_CHECK_COMMU_TIMES 0x6 /* adapter verify need commu 6 times */
#define ADAPTER_MODEL_TIMES 0x3 /* adapter model need by 3 times */

#define VBATT_BASE_FOR_ADAPTER                  3404
#define VBATT_DIV_FOR_ADAPTER                   10

#define VOOC_RX_RECEIVED_STATUS	0x03
#define VOOC_RX_STARTED_STATUS  	0X01
#define VOOC_TX_TRANSMI_STATUS	0x04

#define VOOC_SHIFT_FROM_MASK(x) ((x & 0x01) ? 0 : \
				 (x & 0x02) ? 1 : \
				 (x & 0x04) ? 2 : \
				 (x & 0x08) ? 3 : \
				 (x & 0x10) ? 4 : \
				 (x & 0x20) ? 5 : \
				 (x & 0x40) ? 6 : \
				 (x & 0x80) ? 7 : \
				 (x & 0x100) ? 8 : \
				 (x & 0x200) ? 9 : \
				 (x & 0x400) ? 10 : \
				 (x & 0x800) ? 11 : 0)

#define VOOCPHY_DATA_MASK	0xff
#define VOOCPHY_DATA_H_SHIFT	0x08
#define VOOCPHY_DATA16_SPLIT(data_src, data_l, data_h)	\
do {				\
	data_l = (data_src) & VOOCPHY_DATA_MASK;	\
	data_h = ((data_src) >> VOOCPHY_DATA_H_SHIFT) & VOOCPHY_DATA_MASK;	\
}while(0);

#define MONITOR_EVENT_NUM	VOOC_THREAD_TIMER_INVALID
#define VOOC_MONITOR_START	1
#define VOOC_MONITOR_STOP	0

#define ADJ_CUR_STEP_DEFAULT		0
#define ADJ_CUR_STEP_REPLY_VBATT	1
#define ADJ_CUR_STEP_SET_CURR_DONE	2

#define OPLUS_FASTCHG_STAGE_1   	1
#define	OPLUS_FASTCHG_STAGE_2	  	2
#define OPLUS_FASTCHG_RECOVER_TIME   	(15000/VOOC_FASTCHG_CHECK_TIME)

struct vooc_monitor_event {
	int status;
	int cnt;
	bool timeout;
	int expires;
	unsigned long data;
	int (*mornitor_hdler)(struct device *dev, unsigned long data);
};

struct batt_sys_curve {
	unsigned int target_vbat;
	unsigned int target_ibus;
	bool exit;
	unsigned int target_time;
	unsigned int chg_time;
};

#define BATT_SYS_ROW_MAX        13
#define BATT_SYS_COL_MAX        7
#define BATT_SYS_MAX            6

#define DUMP_REG_CNT 49

struct batt_sys_curves {
	struct batt_sys_curve batt_sys_curve[BATT_SYS_ROW_MAX];
	unsigned char sys_curv_num;
};

enum {
	BATT_SYS_CURVE_TEMP_LITTLE_COLD,
	BATT_SYS_CURVE_TEMP_COOL,
	BATT_SYS_CURVE_TEMP_LITTLE_COOL,
	BATT_SYS_CURVE_TEMP_NORMAL_LOW,
	BATT_SYS_CURVE_TEMP_NORMAL_HIGH,
	BATT_SYS_CURVE_TEMP_WARM,
	BATT_SYS_CURVE_MAX,
};

enum {
	CHIP_ID_DEFAULT = 0,
	CHIP_ID_SC8547,
	CHIP_ID_HL7138,
};

enum oplus_voocphy_ovp_ctrl {
	MASTER_CP_ID,
	SLAVE_CP_ID,
	INVALID_CP_ID,
};

struct oplus_voocphy_manager {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_voocphy_operations *ops;
	struct i2c_client *slave_client;
	struct device *slave_dev;
	struct oplus_voocphy_operations *slave_ops;
	struct vphy_chip *vchip;
	struct power_supply *batt_psy;

	int irq_gpio;
	int irq;

	int switch1_gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *charging_inter_active;
	struct pinctrl_state *charging_inter_sleep;
	struct pinctrl_state *slave_charging_inter_default;

	bool voocphy_dual_cp_support;
	bool voocphy_bidirect_cp_support;
	bool external_gauge_support;
	bool version_judge_support;
	bool impedance_calculation_newmethod;

	unsigned char voocphy_rx_buff;
	unsigned char voocphy_tx_buff[VOOCPHY_TX_LEN];
	unsigned char adapter_mesg;
	unsigned char adapter_model_detect_count;
	unsigned char fastchg_adapter_ask_cmd;
	unsigned char pre_adapter_ask_cmd;
	unsigned char vooc2_next_cmd;
	unsigned char adapter_ask_check_count;
	unsigned char adapter_check_cmmu_count;
	unsigned char adapter_is_vbus_ok_count;
	unsigned char adapter_ask_fastchg_ornot_count;
	unsigned char adapter_rand_h; /* adapter checksum high byte */
	unsigned char adapter_rand_l;  /* adapter checksum low byte */
	unsigned int code_id_local; /* identification code at voocphy */
	unsigned int code_id_far;	 /* identification code from adapter */
	unsigned int batt_soc;

	int vooc_multistep_initial_batt_temp;
	int vooc_temp_cur_range;
	int vooc_little_cool_temp;
	int vooc_cool_temp;
	int vooc_little_cold_temp;
	int vooc_normal_low_temp;
	int vooc_normal_high_temp;
	int vooc_normal_high_temp_default;
	int vooc_little_cool_temp_default;
	int vooc_cool_temp_default;
	int vooc_little_cold_temp_default;
	int vooc_normal_low_temp_default;
	int vooc_low_temp;
	int vooc_high_temp;
	int vooc_low_soc;
	int vooc_high_soc;
	int vooc_strategy_normal_current;
	int vooc_strategy1_batt_high_temp0;
	int vooc_strategy1_batt_high_temp1;
	int vooc_strategy1_batt_high_temp2;
	int vooc_strategy1_batt_low_temp2;
	int vooc_strategy1_batt_low_temp1;
	int vooc_strategy1_batt_low_temp0;
	int vooc_strategy1_high_current0;
	int vooc_strategy1_high_current1;
	int vooc_strategy1_high_current2;
	int vooc_strategy1_low_current2;
	int vooc_strategy1_low_current1;
	int vooc_strategy1_low_current0;
	int vooc_strategy1_high_current0_vooc;
	int vooc_strategy1_high_current1_vooc;
	int vooc_strategy1_high_current2_vooc;
	int vooc_strategy1_low_current2_vooc;
	int vooc_strategy1_low_current1_vooc;
	int vooc_strategy1_low_current0_vooc;
	int vooc_strategy2_batt_up_temp1;
	int vooc_strategy2_batt_up_down_temp2;
	int vooc_strategy2_batt_up_temp3;
	int vooc_strategy2_batt_up_down_temp4;
	int vooc_strategy2_batt_up_temp5;
	int vooc_strategy2_batt_up_temp6;
	int vooc_strategy2_high0_current;
	int vooc_strategy2_high1_current;
	int vooc_strategy2_high2_current;
	int vooc_strategy2_high3_current;
	int vooc_batt_over_high_temp;
	int vooc_batt_over_low_temp;
	int vooc_strategy_change_count;
	int vooc_warm_allow_vol;
	int vooc_warm_allow_soc;
	int cool_down;

	unsigned int plug_in_batt_temp;

	unsigned char  code_id_temp_l; /* identification code temp save */
	unsigned char  code_id_temp_h;
	unsigned char adapter_model_ver;
	unsigned char adapter_model_count; /* obtain adapter_model need times */
	unsigned char ask_batt_sys; /* batt_sys */
	unsigned int svooc_cool_down_current_limit[16];
	unsigned int svooc_cool_down_num;
	unsigned int vooc_cool_down_current_limit[16];
	unsigned int vooc_cool_down_num;
	unsigned int current_default;
	unsigned int current_expect;
	unsigned int current_max;
	unsigned int current_spec;
	unsigned int current_ap;
	unsigned int current_batt_temp;
	unsigned char ap_need_change_current;
	unsigned char adjust_curr;
	unsigned char adjust_fail_cnt;
	unsigned char vooc_head; 	   /* vooc_frame head */
	unsigned int fastchg_batt_temp_status; /* batt temp status */
	unsigned int fastchg_timeout_time; /* fastchg_total time */
	unsigned int fastchg_3c_timeout_time; /* fastchg_3c_total_time */
	unsigned int gauge_vbatt;		   /* batt voltage */
	unsigned int pre_gauge_vbatt;
	unsigned int bq27541_pre_vbatt;
	unsigned int cp_vbus;
	unsigned int cp_vsys;
	unsigned int cp_ichg;
	unsigned int master_cp_ichg;
	unsigned int cp_vbat;
	unsigned int cp_vac;
	unsigned int slave_cp_ichg;
	unsigned int slave_cp_vbus;
	unsigned int slave_cp_vsys;
	unsigned int slave_cp_vbat;
	unsigned int icharging;
	int main_batt_icharging;
	unsigned int main_vbatt;
	unsigned int sub_vbatt;
	int sub_batt_icharging;
	unsigned int ask_current_first;
	unsigned int vbus;
	unsigned int current_pwd;	/* copycat adapter current thd */
	unsigned int curr_pwd_count;	/* count for copycat adapter is ornot */
	bool copycat_icheck;

	unsigned int svooc_circuit_r_l;
	unsigned int svooc_circuit_r_h;
	unsigned int cp_ibus_devation;
	unsigned int slave_cp_enable_thr;
	unsigned int slave_cp_disable_thr_high;
	unsigned int slave_cp_disable_thr_low;

	int batt_temp_plugin; /* batt_temp at plugin */
	int batt_soc_plugin; /* batt_soc at plugin */
	bool adapter_rand_start; /* adapter checksum need start */
	bool adapter_check_ok;
	bool fastchg_allow;
	bool start_vaild_frame;
	bool ask_bat_model_finished;
	bool reply_bat_model_end;
	bool ask_vol_again;
	bool ignore_first_frame;
	bool ask_vooc3_detect;
	bool force_2a;
	bool force_2a_flag;
	bool force_3a;
	bool force_3a_flag;
	bool btb_temp_over;
	bool btb_err_first;
	bool usb_bad_connect;
	bool fastchg_ing;
	bool fastchg_dummy_start;
	bool fastchg_start;
	bool fastchg_to_normal;
	bool fastchg_to_warm;
	bool fastchg_to_warm_full;
	int fast_chg_type;
	bool fastchg_err_commu;
	bool fastchg_reactive;
	bool fastchg_real_allow;
	bool fastchg_commu_stop;
	bool fastchg_check_stop;
	bool fastchg_monitor_stop;
	bool fastchg_commu_ing;
	bool vooc_move_head;
	bool copycat_vooc_adapter;
	bool user_exit_fastchg;
	unsigned char fastchg_stage;
	bool fastchg_need_reset;
	bool fastchg_recovering;
	unsigned int fastchg_recover_cnt;

	int screenoff_current;
	int vooc_vbus_status;
	int vbus_vbatt;
	int adapter_type;
	unsigned int fastchg_notify_status;

	struct hrtimer monitor_btimer;	/* monitor base timer */
	ktime_t moniotr_kt;

	int voocphy_request;
	struct delayed_work voocphy_service_work;
	struct delayed_work notify_fastchg_work;
	struct delayed_work monitor_work;
	struct delayed_work monitor_start_work;

	struct delayed_work modify_cpufeq_work;
	struct delayed_work voocphy_check_charger_out_work;
	struct delayed_work check_chg_out_work;
	struct delayed_work clear_boost_work;
	struct delayed_work voocphy_send_ongoing_notify;
	atomic_t  voocphy_freq_state;
	int voocphy_freq_mincore;
	int voocphy_freq_midcore;
	int voocphy_freq_maxcore;
	int voocphy_current_change_timeout;
	int voocphy_ibat_over_current;
	int voocphy_svooc_cp_max_ibus;
	int voocphy_vooc_cp_max_ibus;
	int voocphy_cp_max_ibus;
	int voocphy_vbus_low;
	int voocphy_vbus_high;
	int voocphy_max_main_ibat;
	int voocphy_max_sub_ibat;

	int batt_pwd_curr_thd1;
	int batt_pwd_vol_thd1;

	/* batt sys curv */
	bool batt_sys_curv_found;
	struct batt_sys_curves *batt_sys_curv_by_soc;
	struct batt_sys_curves *batt_sys_curv_by_tmprange;
	unsigned char cur_sys_curv_idx;
	int sys_curve_temp_idx;

	struct vooc_monitor_event mornitor_evt[MONITOR_EVENT_NUM];

	/* record debug info */
	int irq_total_num;
	int irq_rcvok_num;
	int irq_rcverr_num;
	int vooc_flag;
	int interrupt_flag;
	int irq_tx_timeout_num;
	int irq_tx_timeout;
	int irq_hw_timeout_num;

	int irq_rxdone_num;
	int irq_tx_fail_num;

	int batt_fake_temp;
	int batt_fake_soc;

	/* adsp some status */
	int adsp_voocphy_rx_data;
	int parallel_charge_support;
	int parallel_switch_current;
	int parallel_change_current_count;

	struct low_curr_full_condition *range1_low_curr_full;
	struct low_curr_full_condition *range2_low_curr_full;
	unsigned int range1_low_curr_full_num;
	unsigned int range2_low_curr_full_num;
	struct low_curr_full_condition *sub_range1_low_curr_full;
	struct low_curr_full_condition *sub_range2_low_curr_full;
	unsigned int sub_range1_low_curr_full_num;
	unsigned int sub_range2_low_curr_full_num;
	int low_curr_full_t1;
	int low_curr_full_t2;
	int low_curr_full_t3;
	u32 fastchg_timeout_time_init;
	unsigned int vooc_little_cold_full_voltage;
	unsigned int vooc_cool_full_voltage;
	unsigned int vooc_warm_full_voltage;
	unsigned int vooc_1time_full_voltage;
	unsigned int vooc_ntime_full_voltage;
	int ovp_reg;
	int ocp_reg;
	int adapter_check_vooc_head_count;
	int adapter_check_cmd_data_count;

	bool	voocphy_iic_err;
	bool	slave_voocphy_iic_err;
	int	voocphy_iic_err_num;
	int	slave_voocphy_iic_err_num;
	int	r_state;
	int	voocphy_vooc_irq_flag;
	int	voocphy_cp_irq_flag;
	int	ap_disconn_issue;
	int	disconn_pre_vbus;
	int	disconn_pre_ibus;
	int	disconn_pre_vbat;
	int	disconn_pre_ibat;
	int	disconn_pre_vbat_calc;
	int	voocphy_enable;
	int	slave_voocphy_enable;
	int	vbus_adjust_cnt;
	unsigned int vbat_calc;
	int ap_handle_timeout_num;
	int rcv_done_200ms_timeout_num;
	int rcv_date_err_num;
	bool detach_unexpectly;
	unsigned int max_div_cp_ichg;
	unsigned int vbat_deviation_check;
	unsigned char reg_dump[DUMP_REG_CNT];
	unsigned char slave_reg_dump[DUMP_REG_CNT];
	unsigned int master_error_ibus;
	unsigned int slave_error_ibus;
	unsigned int error_current_expect;
	u8 int_column[6];
	u8 int_column_pre[6];
	bool copycat_vooc_support;
	int chip_id;
	enum oplus_voocphy_ovp_ctrl ovp_ctrl_cpindex;
};

struct oplus_voocphy_operations {
	void (*hardware_init)(struct oplus_voocphy_manager *chip);
	int (*hw_setting)(struct oplus_voocphy_manager *chip, int reason);
	int (*init_vooc)(struct oplus_voocphy_manager *chip);
	int (*set_predata)(struct oplus_voocphy_manager *chip, u16 val);
	int (*set_txbuff)(struct oplus_voocphy_manager *chip, u16 val);
	int (*get_adapter_info)(struct oplus_voocphy_manager *chip);
	void (*update_data)(struct oplus_voocphy_manager *chip);
	int (*get_chg_enable)(struct oplus_voocphy_manager *chip, u8 *data);
	int (*set_chg_enable)(struct oplus_voocphy_manager *chip, bool enable);
	int (*get_adc_enable)(struct oplus_voocphy_manager *chip, u8 *data);
	int (*set_adc_enable)(struct oplus_voocphy_manager *chip, bool enable);
	int (*set_adc_forcedly_enable)(struct oplus_voocphy_manager *chip, int mode);
	int (*reset_voocphy)(struct oplus_voocphy_manager *chip);
	int (*reactive_voocphy)(struct oplus_voocphy_manager *chip);
	void (*send_handshake)(struct oplus_voocphy_manager *chip);
	int (*get_ichg)(struct oplus_voocphy_manager *chip);
	int (*get_cp_vbat)(struct oplus_voocphy_manager *chip);
	int (*get_cp_vbus)(struct oplus_voocphy_manager *chip);
	u8 (*get_int_value)(struct oplus_voocphy_manager *chip);
	int (*get_cp_status)(struct oplus_voocphy_manager *chip);
	void (*set_pd_svooc_config)(struct oplus_voocphy_manager *chip, bool enable);
	bool (*get_pd_svooc_config)(struct oplus_voocphy_manager *chip);
	int (*adsp_voocphy_enable)(bool enable);
	int (*adsp_voocphy_reset_again)(void);
	u8 (*get_vbus_status)(struct oplus_voocphy_manager *chip);
	int (*set_chg_auto_mode)(struct oplus_voocphy_manager *chip, bool enable);
	int (*clear_interrupts)(struct oplus_voocphy_manager *chip);
	int (*get_voocphy_enable)(struct oplus_voocphy_manager *chip, u8 *data);
	void (*dump_voocphy_reg)(struct oplus_voocphy_manager *chip);
	int (*get_chip_id)(struct oplus_voocphy_manager *chip);
	int (*reset_voocphy_ovp)(struct oplus_voocphy_manager *chip);
	bool (*check_cp_int_happened)(struct oplus_voocphy_manager *chip, bool *dump_reg, bool *send_info);
};

#define VOOCPHY_LOG_BUF_LEN 1024

struct voocphy_info {
	int kernel_time;
	int irq_took_time;
	int recv_adapter_msg;
	unsigned char fastchg_adapter_ask_cmd;
	int predata;
	int send_msg;
};

struct voocphy_log_buf {
	struct voocphy_info voocphy_info_buf[VOOCPHY_LOG_BUF_LEN];
	int point;
};

bool oplus_voocphy_chip_is_null(void);
void oplus_voocphy_slave_init(struct oplus_voocphy_manager *chip);
void oplus_voocphy_get_chip(struct oplus_voocphy_manager **chip);
int oplus_register_voocphy(struct oplus_voocphy_manager *chip);

int __attribute__((weak)) ppm_sys_boost_min_cpu_freq_set(int freq_min, int freq_mid, int freq_max, unsigned int clear_time)
{
	return 0;
}

int __attribute__((weak)) ppm_sys_boost_min_cpu_freq_clear(void)
{
	return 0;
}
#endif /* _OPLUS_VOOCPHY_H_ */
