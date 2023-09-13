// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SC8547A_H_
#define _OPLUS_SC8547A_H_

#define UFCS_BTB_TEMP_MAX 80
#define UFCS_USB_TEMP_MAX 80
#define UFCS_BTB_USB_OVER_CNTS 9

#define UFCS_UCT_POWER_VOLT 5000
#define UFCS_UCT_POWER_CURR 1000

#define UFCS_HARDRESET_RETRY_CNTS 3
#define UFCS_HANDSHAKE_RETRY_CNTS 1

/************************Timer***********************************/
#define UFCS_HANDSHAKE_TIMEOUT (300)
#define UFCS_PING_TIMEOUT (300)
#define UFCS_ACK_TIMEOUT (50)
#define UFCS_MSG_TIMEOUT (300)
#define UFCS_POWER_RDY_TIMEOUT (550)
/********************** I2C Slave Addr **************************/
#define SC8547A_I2C_ADDR (0x6F)
#define SC8547A_MAX_REG (0x0264)
#define SC8547A_FLAG_NUM (3)

/********************** SC2201 I2C reg map **********************/
#define SC8547A_ENABLE_REG_NUM (6)

#define SC8547A_ADDR_DEVICE_ID (0x00)
#define SC8547A_ADDR_DEVICE_ID1 (0x01)

#define SC8547A_ADDR_DPDM_CTRL (0x21)
#define SC8547A_CMD_DPDM_EN (0x01)

#define SC8547A_ADDR_OTG_EN (0x3B)

#define SC8547A_ADDR_UFCS_CTRL0 (0x40)
#define SEND_SOURCE_HARDRESET BIT(0)
#define SEND_CABLE_HARDRESET BIT(1)
#define FLAG_BAUD_RATE_VALUE (BIT(4) | BIT(3))
#define FLAG_BAUD_NUM_SHIFT (3)
#define SC8547A_CMD_EN_CHIP (0x80)
#define SC8547A_CMD_DIS_CHIP (0X00)
#define SC8547A_MASK_EN_HANDSHAKE BIT(5)
#define SC8547A_CMD_EN_HANDSHAKE BIT(5)

#define SC8547A_ADDR_GENERAL_INT_FLAG1 (0x41)
#define SC8547A_CMD_MASK_ACK_DISCARD (0x80)

#define SC8547A_ADDR_GENERAL_INT_FLAG2 (0x42)
#define SC8547A_FLAG_ACK_RECEIVE_TIMEOUT BIT(0)
#define SC8547A_FLAG_HARD_RESET BIT(1)
#define SC8547A_FLAG_DATA_READY BIT(2)
#define SC8547A_FLAG_SENT_PACKET_COMPLETE BIT(3)
#define SC8547A_FLAG_CRC_ERROR BIT(4)
#define SC8547A_FLAG_BAUD_RATE_ERROR BIT(5)
#define SC8547A_FLAG_HANDSHAKE_SUCCESS BIT(6)
#define SC8547A_FLAG_HANDSHAKE_FAIL BIT(7)

#define SC8547A_ADDR_GENERAL_INT_FLAG3 (0x43)
#define SC8547A_FLAG_TRAINING_BYTE_ERROR BIT(0)
#define SC8547A_FLAG_MSG_TRANS_FAIL BIT(1)
#define SC8547A_FLAG_DATA_BYTE_TIMEOUT BIT(3)
#define SC8547A_FLAG_BAUD_RATE_CHANGE BIT(4)
#define SC8547A_FLAG_LENGTH_ERROR BIT(5)
#define SC8547A_FLAG_RX_OVERFLOW BIT(6)
#define SC8547A_FLAG_BUS_CONFLICT BIT(7)

#define SC8547A_ADDR_UFCS_INT_MASK0 (0x44)
#define SC8547A_CMD_MASK_ACK_TIMEOUT (0x01)

#define SC8547A_ADDR_UFCS_INT_MASK1 (0x45)
#define SC8547A_MASK_TRANING_BYTE_ERROR (0x01)

/*tx_buffer*/
#define SC8547A_ADDR_TX_LENGTH (0x47)

#define SC8547A_ADDR_TX_BUFFER0 (0x48)

#define SC8547A_ADDR_TX_BUFFER35 (0x6B)

/*rx_buffer*/
#define SC8547A_ADDR_RX_LENGTH (0x6C)
#define SC8547A_ADDR_RX_BUFFER0 (0x6D)
#define SC8547A_ADDR_RX_BUFFER124 (0xE9)
#define SC8547A_LEN_MAX 64

#define SC8547A_ADDR_TXRX_BUFFER_CTRL (0xEA)
#define SC8547A_CMD_CLR_TX_RX (0xC0)

#define SC8547A_DEVICE_ID 0x67

/*********************command buffer**************/
/*avoid to rewrite the baudrate flags*/
#define SC8547A_CMD_SND_CMP BIT(2)
#define SC8547A_MASK_SND_CMP BIT(2)

/*********************message defination**********/
enum UFCS_MACHINE_STATE {
	STATE_NULL = 0,
	STATE_HANDSHAKE,
	STATE_PING,
	STATE_IDLE,
	STATE_WAIT_ACK,
	STATE_WAIT_MSG,
};

enum UFCS_ADDR {
	UFCS_ADDR_SRC = 1,
	UFCS_ADDR_SNK,
	UFCS_ADDR_CABLE,
};

enum UFCS_HEADER_TYPE {
	HEAD_TYPE_CTRL = 0,
	HEAD_TYPE_DATA = 1,
	HEAD_TYPE_VENDER = 2,
	HEAD_TYPE_NULL = 3,
};

enum UFCS_CTRL_MSG_TYPE {
	UFCS_CTRL_PING = 0,
	UFCS_CTRL_ACK,
	UFCS_CTRL_NCK,
	UFCS_CTRL_ACCEPT,
	UFCS_CTRL_SOFT_RESET,
	UFCS_CTRL_POWER_READY,
	UFCS_CTRL_GET_OUTPUT_CAP,
	UFCS_CTRL_GET_SOURCE_INFO,
	UFCS_CTRL_GET_SINK_INFO,
	UFCS_CTRL_GET_CABLE_INFO,
	UFCS_CTRL_GET_DEVICE_INFO,
	UFCS_CTRL_GET_ERROR_INFO,
	UFCS_CTRL_DETECT_CABLE_INFO,
	UFCS_CTRL_START_CABLE_DETECT,
	UFCS_CTRL_END_CABLE_DETECT,
	UFCS_CTRL_EXIT_UFCS_MODE,
	UFCS_CTRL_MAX,
};

enum UFCS_DATA_MSG_TYPE {
	UFCS_DATA_OUTPUT_CAP = 1,
	UFCS_DATA_REQUEST,
	UFCS_DATA_SOURCE_INFO,
	UFCS_DATA_SINK_INFO,
	UFCS_DATA_CABLE_INFO,
	UFCS_DATA_DEVICE_INFO,
	UFCS_DATA_ERROR_INFO,
	UFCS_DATA_CONFIG_WATCHDOG,
	UFCS_DATA_REFUSE,
	UFCS_DATA_VERIFY_REQUEST,
	UFCS_DATA_VERIFY_RESPONSE,
	UFCS_DATA_POWER_CHANGE,
	UFCS_DATA_UCT_REQUEST = 0xFF,
	UFCS_DATA_MAX,
};

enum UFCS_VND_MSG_TYPE {
	UFCS_VND_CHECK_POWER = 1,
	UFCS_VND_MAX,
};

enum UFCS_BAUD_RATE {
	UFCS_BAUD_115200 = 0,
	UFCS_BAUD_57600,
	UFCS_BAUD_38400,
	UFCS_BAUD_19200,
};

enum UFCS_REFUSE_REASON {
	REASON_CMD_NOT_DETECT = 1,
	REASON_ID_NOT_SUPPORT = 2,
	REASON_DEVICE_BUSY = 3,
	REASON_OVER_LIMIT = 4,
	REASON_OTHERS = 5,
};

struct hardware_error {
	bool dp_ovp;
	bool dm_ovp;
	bool temp_shutdown;
	bool wtd_timeout;
	bool hardreset;
};

struct receive_error {
	bool sent_cmp;
	bool msg_trans_fail;
	bool ack_rcv_timeout;
	bool data_rdy;
};

struct communication_error {
	bool baud_error;
	bool training_error;
	bool start_fail;
	bool byte_timeout;
	bool rx_len_error;
	bool rx_overflow;
	bool crc_error;
	bool stop_error;
	bool baud_change;
	bool bus_conflict;
};

struct sc8574a_error_flag {
	struct hardware_error hd_error;
	struct receive_error rcv_error;
	struct communication_error commu_error;
};

struct comm_msg {
	u16 header;
	u8 command;
	u16 vender;
	u8 len;
	u8 *buf;
};

struct source_cap {
	u64 min_cur : 8;
	u64 max_cur : 16;
	u64 min_vol : 16;
	u64 max_vol : 16;
	u64 vol_step : 1;
	u64 cur_step : 3;
	u64 id : 4;
};

struct request_pdo {
	u64 cur : 16;
	u64 vol : 16;
	u64 reserve : 28;
	u64 index : 4;
};

struct charger_power {
	u64 vnd_cmd : 16;
	u64 vnd_p1 : 16;
	u64 vnd_p2 : 16;
	u64 vnd_p3 : 16;
};

struct verify_request {
	u8 id;
	u8 phone_random[16];
};

struct verify_response {
	u8 encrypt_result[32];
	u8 adapter_random[16];
};

struct device_info {
	u64 software_ver : 16;
	u64 hardware_ver : 16;
	u64 ic_vender : 16;
	u64 dev_vender : 16;
};

struct src_info {
	u64 cur : 16;
	u64 vol : 16;
	u64 usb_temp : 8;
	u64 dev_temp : 8;
	u64 reserve : 16;
};

struct sink_info {
	u64 cur : 16;
	u64 vol : 16;
	u64 usb_temp : 8;
	u64 dev_temp : 8;
	u64 reserve : 16;
};

struct error_info {
	u32 output_ovp : 1;
	u32 output_uvp : 1;
	u32 output_ocp : 1;
	u32 output_scp : 1;
	u32 usb_otp : 1;
	u32 batt_otp : 1;
	u32 cc_ovp : 1;
	u32 dm_ovp : 1;
	u32 dp_ovp : 1;
	u32 input_ovp : 1;
	u32 input_uvp : 1;
	u32 drain_ovp : 1;
	u32 input_lost : 1;
	u32 crc : 1;
	u32 watchdog : 1;
	u32 reserve : 16;
};

struct config_watchdog {
	u16 timer;
};

struct refuse_msg {
	u32 reason : 8;
	u32 cmd : 8;
	u32 msg_type : 3;
	u32 reserve2 : 5;
	u32 msg_id : 4;
	u32 reserve1 : 4;
};

struct uct_mode {
	u16 msg_num : 8;
	u16 msg_type : 3;
	u16 device_addr : 3;
	u16 voltage_mode : 1;
	u16 reserved : 1;
};

/****************Message Construction Helper*********/
#define UFCS_HEADER_REV 0x01
#define UFCS_KEY_VER 0x01
#define OPLUS_DEV_VENDOR 0x22d9
#define OPLUS_IC_VENDOR 0x0

#define SC8547A_VENDOR_ID 0x0006
#define SC8547A_HARDWARE_VER 0x0001
#define SC8547A_SOFTWARE_VER 0x0001
#define DATA_LENGTH_BLANK 0x0
#define UCT_MODE_WATCHDOG 3000
#define UCT_MODE_IC_VEND 0xFFFF
#define UCT_MODE_DEV_VEND 0xFFFF

#define ABNORMAL_HARDWARE_VER_A_MIN 0x1006
#define ABNORMAL_HARDWARE_VER_A_MAX 0x1008
#define ABNORMAL_SOFTWARE_VER_A 0x0006
#define ABNORMAL_IC_VEND 0x0
#define ABNORMAL_DEV_VEND 0x0

#define VND_LENGTH_BLANK 0x2

#define UFCS_HEADER_DEV_TYPE_SHIFT 13
#define UFCS_HEADER_DEV_TYPE_MASK 0x7
#define UFCS_HEADER_MSG_ID_SHIFT 9
#define UFCS_HEADER_MSG_ID_MASK 0xF
#define UFCS_HEADER_REV_SHIFT 3
#define UFCS_HEADER_REV_MASK 0x3F
#define UFCS_HEADER_MSG_TYPE_SHIFT 0
#define UFCS_HEADER_MSG_TYPE_MASK 0x7

#define UFCS_HEADER(dev_type, msg_seq, msg_type)                               \
	((u16)(((dev_type)&UFCS_HEADER_DEV_TYPE_MASK)                          \
	       << UFCS_HEADER_DEV_TYPE_SHIFT) |                                \
	 (((msg_seq)&UFCS_HEADER_MSG_ID_MASK) << UFCS_HEADER_MSG_ID_SHIFT) |   \
	 (((UFCS_HEADER_REV)&UFCS_HEADER_REV_MASK) << UFCS_HEADER_REV_SHIFT) | \
	 (((msg_type)&UFCS_HEADER_MSG_TYPE_MASK)                               \
	  << UFCS_HEADER_MSG_TYPE_SHIFT))

#define UFCS_HEADER_ADDR(header) (header >> 13)
#define UFCS_HEADER_ID(header) ((header >> 9) & 0x0F)
#define UFCS_HEADER_VER(header) ((header >> 3) & 0x3F)
#define UFCS_HEADER_TYPE(header) (header & 0x07)

/*Protocol Data Length*/
#define COM_DATA_HEADER_POS 0
#define COM_DATA_CMD_TYPE_POS 2
#define COM_DATA_DATA_SIZE_POS 3
#define COM_DATA_DATA_POS 4

#define COM_HEADER_LEN 2
#define COM_CMD_ID_LEN 1
#define COM_VENDER_ID_LEN 2
#define COM_DATA_SIZE_LEN 1

#define COM_FIXED_FIELD_LEN                                                    \
	(COM_HEADER_LEN + COM_CMD_ID_LEN + COM_DATA_SIZE_LEN)

#define WRT_DATA_BGN_POS 4

#define READ_DATA_LEN_POS 3
#define READ_DATA_BGN_POS 4

#define READ_VENDER_LEN_POS 4
#define READ_VENDER_BGN_POS 5

#define MAX_RCV_BUFFER_SIZE 124
#define MAX_TX_BUFFER_SIZE 35
#define MAX_TX_MSG_SIZE (MAX_TX_BUFFER_SIZE - COM_FIXED_FIELD_LEN)

#define REQUEST_PDO_LENGTH 8
#define SINK_INFO_LENGTH 8
#define CONFIG_WTD_LENGTH 2
#define REFUSE_MSG_LENGTH 4
#define DEVICE_INFO_LENGTH 8
#define VERIFY_REQUEST_LENGTH 17
#define PHONE_RANDOM_LENGTH 16
#define VERIFY_RESPONSE_LENGTH 48
#define ADAPTER_RANDOM_LENGTH 16
#define ADAPTER_ENCRYPT_LENGTH 32
#define REQUEST_POWER_LENGTH 3

#define ATTRIBUTE_NOT_SUPPORTED 0
/***************************message parse helper**************************/
#define SWAP64(val)                                                            \
	((((val)&0xff00000000000000) >> 56) |                                  \
	 (((val)&0x00ff000000000000) >> 40) |                                  \
	 (((val)&0x0000ff0000000000) >> 24) |                                  \
	 (((val)&0x000000ff00000000) >> 8) |                                   \
	 (((val)&0x00000000ff000000) << 8) |                                   \
	 (((val)&0x0000000000ff0000) << 24) |                                  \
	 (((val)&0x000000000000ff00) << 40) |                                  \
	 (((val)&0x00000000000000ff) << 56))

#define SWAP32(val)                                                            \
	((((val)&0xff000000) >> 24) | (((val)&0x00ff0000) >> 8) |              \
	 (((val)&0x0000ff00) << 8) | (((val)&0x000000ff) << 24))

#define SWAP16(val) ((((val)&0xff00) >> 8) | (((val)&0x00ff) << 8))

#define POWER_EXCHANGE_GET_VOLT(val)                                           \
	(((val)&0x0000ff0000) >> 8) | (((val)&0x00ff000000) >> 24)

#define POWER_EXCHANGE_GET_CURR(val)                                           \
	(((val)&0x00000000ff) << 8) | (((val)&0x000000ff00) >> 8)

#define PDO_LENGTH 8
#define POWER_CHANGE_LENGTH 3
#define MAX_PDO_NUM 7
#define MIN_PDO_NUM 1
#define ERROR_INFO_NUM 16
#define ERROR_INFO_LEN 4
#define VALID_ERROR_INFO_BITS 0x7fff

const char *adapter_error_info[16] = {
	"adapter output OVP!",
	"adapter outout UVP!",
	"adapter output OCP!",
	"adapter output SCP!",
	"adapter USB OTP!",
	"adapter inside OTP!",
	"adapter CCOVP!",
	"adapter D-OVP!",
	"adapter D+OVP!",
	"adapter input OVP!",
	"adapter input UVP!",
	"adapter drain over current!",
	"adapter input current loss!",
	"adapter CRC error!",
	"adapter watchdog timeout!",
	"invalid msg!",
};

struct oplus_sc8574a_ufcs {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;

	struct mutex chip_lock;
	atomic_t suspended;
	atomic_t i2c_err_count;
	struct kthread_worker *wq;
	struct kthread_work rcv_work;
	struct work_struct reply_work;
	struct delayed_work ufcs_service_work;
	struct completion rcv_cmp;
	struct wakeup_source *chip_ws;

	enum UFCS_MACHINE_STATE state;
	struct comm_msg dev_msg;
	struct comm_msg rcv_msg;
	struct oplus_ufcs_src_cap src_cap;
	struct sc8574a_error_flag flag;
	struct oplus_ufcs_error error;
	struct oplus_ufcs_sink_info info;
	u8 dev_buffer[MAX_TX_BUFFER_SIZE];
	u8 rcv_buffer[MAX_RCV_BUFFER_SIZE];
	int msg_send_id;
	int msg_recv_id;

	int get_flag_failed;
	int handshake_success;
	int ack_received;
	int msg_received;
	int soft_reset;
	int uct_mode;
	bool oplus_id;
	bool ufcs_enable;
	bool abnormal_id;
};
#endif /*_OPLUS_SC8547A_H_*/
