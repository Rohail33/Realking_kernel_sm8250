/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef TOUCHPANEL_GOODIX_9886_H
#define TOUCHPANEL_GOODIX_9886_H

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include "../goodix_common.h"
#include "../gtx8_tools.h"

#define TPD_DEVICE "Goodix-gt9886"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
        do {\
                if (LEVEL_DEBUG == tp_debug) {\
                        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DETAIL(a, arg...)\
        do {\
                if (LEVEL_BASIC != tp_debug) {\
                        pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
                }\
        }while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
        do {\
                if (tp_debug) {\
                        printk(a, ##arg);\
                }\
        }while(0)

/****************************Start of define declare****************************/

#define set_reg_bit(reg, pos, val)        ((reg) = ((reg) & (~(1 << (pos)))) | (!!(val) << (pos)))

#define MAX_POINT_NUM            10       //max touch point number this ic support
#define MAX_GT_IRQ_DATA_LENGTH   90       //irq data(points,key,checksum) size read from irq
#define MAX_GT_EDGE_DATA_LENGTH   50       //irq edge data read from irq

#define MAX_GESTURE_POINT_NUM    128      //max point number of black gesture

#define GTP_DRIVER_SEND_CFG      1        // send config to TP while initializing (for no config built in TP's flash)

//Request type define
#define GTP_RQST_RESPONDED      0x00
#define GTP_RQST_CONFIG         0x01
#define GTP_RQST_BASELINE       0x02
#define GTP_RQST_RESET          0x03
#define GTP_RQST_FRE            0x04
#define GTP_RQST_IDLE           0xFF

//triger event
#define GOODIX_TOUCH_EVENT              0x80
#define GOODIX_REQUEST_EVENT            0x40
#define GOODIX_GESTURE_EVENT            0x20
#define GOODIX_FINGER_PRINT_EVENT       0x08
#define GOODIX_FINGER_STATUS_EVENT      0x02
#define GOODIX_FINGER_IDLE_EVENT        0x04

//config define
#define BYTES_PER_COORD                 8
#define BYTES_PER_EDGE                  2
#define TS_MAX_SENSORID                 5
#define TS_CFG_MAX_LEN                  1024
#define TS_CFG_HEAD_LEN                 4
#define TS_CFG_BAG_NUM_INDEX            2
#define TS_CFG_BAG_START_INDEX          4
#define GOODIX_CFG_MAX_SIZE             1024
#define TS_WAIT_CFG_READY_RETRY_TIMES   30
#define TS_WAIT_CMD_FREE_RETRY_TIMES    10


//command define
#define GTP_CMD_NORMAL                  0x00
#define GTP_CMD_RAWDATA                 0x01
#define GTP_CMD_SLEEP                   0x05
#define GTP_CMD_CHARGER_ON              0x06
#define GTP_CMD_CHARGER_OFF             0x07
#define GTP_CMD_GESTURE_ON              0x08
#define GTP_CMD_GESTURE_OFF             0x09
#define GTP_CMD_SHORT_TEST              0x0B
#define GTM_CMD_EDGE_LIMIT_LANDSCAPE    0x0E
#define GTM_CMD_EDGE_LIMIT_VERTICAL     0x0F
#define GTP_CMD_ENTER_DOZE_TIME         0x10
#define GTP_CMD_DEFULT_DOZE_TIME        0x11
#define GTP_CMD_FACE_DETECT_ON          0x12
#define GTP_CMD_FACE_DETECT_OFF         0x13
#define GTP_CMD_FOD_FINGER_PRINT        0x19
#define GTP_CMD_GESTURE_ENTER_IDLE      0x1A

#define GTP_CMD_FINGER_PRINT_AREA       0x40 //change the finger print detect area
#define GTP_CMD_FILTER                  0x41 //changer filter
#define GTP_CMD_DEBUG                   0x42
#define GTP_CMD_DOWN_DELTA              0x43
#define GTP_CMD_GESTURE_DEBUG           0x44
#define GTP_CMD_GAME_MODE               0x45
#define GTP_CMD_GESTURE_MASK            0x46

//config cmd
#define COMMAND_START_SEND_LARGE_CFG    0x80
#define COMMAND_START_SEND_SMALL_CFG    0x81
#define COMMAND_SEND_CFG_PREPARE_OK     0x82
#define COMMAND_END_SEND_CFG            0x83
#define COMMAND_READ_CFG_PREPARE_OK     0x85
#define COMMAND_START_READ_CFG          0x86
#define COMMAND_FLASH_READY             0x89

#define TS_CMD_REG_READY                0xFF


//gesture type fw send
#define DTAP_DETECT                     0xCC
#define STAP_DETECT                     0x4C
#define UP_VEE_DETECT                   0x76
#define DOWN_VEE_DETECT                 0x5e
#define LEFT_VEE_DETECT                 0x3e
#define RIGHT_VEE_DETECT                0x63 //this gesture is C
#define RIGHT_VEE_DETECT2               0x3c //this gesture is <
#define CIRCLE_DETECT                   0x6f
#define DOUSWIP_DETECT                  0x48
#define RIGHT_SLIDE_DETECT              0xAA
#define LEFT_SLIDE_DETECT               0xbb
#define DOWN_SLIDE_DETECT               0xAB
#define UP_SLIDE_DETECT                 0xBA
#define M_DETECT                        0x6D
#define W_DETECT                        0x77
#define FP_DOWN_DETECT                  0x46
#define FP_UP_DETECT                    0x55

//gesture define
#define GSX_KEY_DATA_LEN                37
#define GSX_GESTURE_TYPE_LEN            32

//debug info define
#define SENSOR_NUM_ADDR                 0x5473
#define DRIVER_GROUP_A_NUM_ADDR         0x5477
//#define DRIVER_GROUP_B_NUM_ADDR            0x5478

/****************************End of define declare***************************/

/****************************Start of auto test ********************/
#define GTX8_RETRY_NUM_3               3
#define GTX8_CONFIG_REFRESH_DATA       0x01

#define FLOAT_AMPLIFIER                1000
#define MAX_U16_VALUE                  65535
#define RAWDATA_TEST_TIMES             10

#define MAX_TEST_ITEMS                 10 /* 0P-1P-2P-3P-5P total test items */

#define GTP_INTPIN_TEST                0
#define GTP_CAP_TEST                   1
#define GTP_DELTA_TEST                 2
#define GTP_NOISE_TEST                 3
#define GTP_SHORT_TEST                 5
#define GTP_SELFCAP_TEST               6
#define GTP_SELFNOISE_TEST             7

static char *test_item_name[MAX_TEST_ITEMS] = {
    "GTP_INTPIN_TEST",
    "GTP_CAP_TEST",
    "GTP_DELTA_TEST",
    "GTP_NOISE_TEST",
    "no test",
    "GTP_SHORT_TEST",
    "GTP_SELFCAP_TEST",
    "GTP_SELFNOISE_TEST",
    "no test",
    "no test",
};

#define GTP_TEST_PASS                1
#define GTP_PANEL_REASON             2
#define SYS_SOFTWARE_REASON          3

/* error code */
#define NO_ERR                     0
#define RESULT_ERR                -1
#define RAWDATA_SIZE_LIMIT        -2

/*param key word in .csv */
#define CSV_TP_SPECIAL_RAW_MIN        "specail_raw_min"
#define CSV_TP_SPECIAL_RAW_MAX        "specail_raw_max"
#define CSV_TP_SPECIAL_RAW_DELTA      "special_raw_delta"
#define CSV_TP_SHORT_THRESHOLD        "shortciurt_threshold"
#define CSV_TP_SPECIAL_SELFRAW_MAX    "special_selfraw_max"
#define CSV_TP_SPECIAL_SELFRAW_MIN    "special_selfraw_min"
#define CSV_TP_SELFNOISE_LIMIT        "noise_selfdata_limit"
#define CSV_TP_TEST_CONFIG            "test_config"
#define CSV_TP_NOISE_CONFIG           "noise_config"
#define CSV_TP_NOISE_LIMIT            "noise_data_limit"

/*GTX8 CMD*/
#define GTX8_CMD_NORMAL                0x00
#define GTX8_CMD_RAWDATA               0x01
/* Regiter for rawdata test*/
#define GTP_RAWDATA_ADDR_9886          0x8FA0
#define GTP_NOISEDATA_ADDR_9886        0x9D20
#define GTP_BASEDATA_ADDR_9886         0xA980
#define GTP_SELF_RAWDATA_ADDR_9886     0x4C0C
#define GTP_SELF_NOISEDATA_ADDR_9886   0x4CA4

#define GTP_REG_COOR                   0x4100
/*  short  test*/
#define SHORT_TO_GND_RESISTER(sig)        (div_s64(5266285, (sig) & (~0x8000)) - 40 * 100)    /* (52662.85/code-40) * 100 */
#define SHORT_TO_VDD_RESISTER(sig, value) (div_s64(36864 * ((value) - 9) * 100, (((sig) & (~0x8000)) * 7)) - 40 * 100)

#define DRV_CHANNEL_FLAG            0x80
#define SHORT_STATUS_REG            0x5095
#define WATCH_DOG_TIMER_REG         0x20B0

#define TXRX_THRESHOLD_REG          0x8408
#define GNDVDD_THRESHOLD_REG        0x840A
#define ADC_DUMP_NUM_REG            0x840C

#define GNDAVDD_SHORT_VALUE         16
#define ADC_DUMP_NUM                200
#define SHORT_CAL_SIZE(a)           (4 + (a) * 2 + 2)

#define SHORT_TESTEND_REG           0x8400
#define TEST_RESTLT_REG             0x8401
#define TX_SHORT_NUM                0x8402

#define DIFF_CODE_REG               0xA97A
#define DRV_SELF_CODE_REG           0xA8E0
#define TX_SHORT_NUM_REG            0x8802

#define MAX_DRV_NUM                 40
#define MAX_SEN_NUM                 36
/*  end  */

static u8 gt9886_drv_map[] = {46, 48, 49, 47, 45, 50, 56, 52, 51, 53, 55, 54, 59, 64, 57, 60, 62, 58, 65, 63, 61, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
static u8 gt9886_sen_map[] = {32, 34, 35, 30, 31, 33, 27, 28, 29, 10, 25, 26, 23, 13, 24, 12, 9, 11, 8, 7, 5, 6, 4, 3, 2, 1, 0, 73, 75, 74, 39, 72, 40, 36, 37, 38};

/****************************End of define declare***************************/

/****************************Start of Firmware update info********************/
#define FW_HEADER_SIZE              256
#define FW_SUBSYS_INFO_SIZE         8
#define FW_SUBSYS_INFO_OFFSET       32
#define FW_SUBSYS_MAX_NUM           28

#define GOODIX_BUS_RETRY_TIMES      3

/*mtk max i2c size is 4k*/
#define ISP_MAX_BUFFERSIZE          (1024 * 4)

#define I2C_DATA_MAX_BUFFERSIZE     (1024 * 3)

#define HW_REG_CPU_CTRL             0x2180
#define HW_REG_DSP_MCU_POWER        0x2010
#define HW_REG_RESET                0x2184
#define HW_REG_SCRAMBLE             0x2218
#define HW_REG_BANK_SELECT          0x2048
#define HW_REG_ACCESS_PATCH0        0x204D
#define HW_REG_EC_SRM_START         0x204F
#define HW_REG_CPU_RUN_FROM         0x4506 /* for nor_L is 0x4006 */
#define HW_REG_ISP_RUN_FLAG         0x6006
#define HW_REG_ISP_ADDR             0xC000
#define HW_REG_ISP_BUFFER           0x6100
#define HW_REG_SUBSYS_TYPE          0x6020
#define HW_REG_FLASH_FLAG           0x6022
#define HW_REG_CACHE                0x204B
#define HW_REG_ESD_KEY              0x2318
#define HW_REG_WTD_TIMER            0x20B0

#define CPU_CTRL_PENDING            0x00
#define CPU_CTRL_RUNNING            0x01

#define ISP_STAT_IDLE               0xFF
#define ISP_STAT_READY              0xAA
#define ISP_STAT_WRITING            0xAA
#define ISP_FLASH_SUCCESS           0xBB
#define ISP_FLASH_ERROR             0xCC
#define ISP_FLASH_CHECK_ERROR       0xDD
#define ISP_CMD_PREPARE             0x55
#define ISP_CMD_FLASH               0xAA

#define TS_CHECK_ISP_STATE_RETRY_TIMES     200
#define TS_READ_FLASH_STATE_RETRY_TIMES    200

/* cfg parse from bin */
#define CFG_BIN_SIZE_MIN        279
#define BIN_CFG_START_LOCAL     6
#define MODULE_NUM              22
#define CFG_NUM                 23
#define CFG_INFO_BLOCK_BYTES    8
#define CFG_HEAD_BYTES          32

#define GTX8_EXIST              1
#define GTX8_NOT_EXIST          0

/* GTX8 cfg name */
#define GTX8_NORMAL_CONFIG              "normal_config"             //config_type: 0x01
#define GTX8_TEST_CONFIG                "tptest_config"             //config_type: 0x00, test config
#define GTX8_NORMAL_NOISE_CONFIG        "normal_noise_config"       //config_type: 0x02,normal sensitivity, use for charging
#define GTX8_GLOVE_CONFIG               "glove_config"              //config_type: 0x03,high sensitivity
#define GTX8_GLOVE_NOISE_CONFIG         "glove_noise_config"        //config_type: 0x04,high sensitivity, use for charging
#define GTX8_HOLSTER_CONFIG             "holster_config"            //config_type: 0x05,holster
#define GTX8_HOLSTER_NOISE_CONFIG       "holster_noise_config"      //config_type: 0x06,holster ,use for charging
#define GTX8_NOISE_TEST_CONFIG          "tpnoise_test_config"       //config_type: 0x07,noise test config

#define getU32(a) ((u32)getUint((u8 *)(a), 4))
#define getU16(a) ((u16)getUint((u8 *)(a), 2))
/****************************End of Firmware update info********************/

/****************************Start of config data****************************/
#define GTP_CONFIG_MIN_LENGTH    186
#define GTP_CONFIG_MAX_LENGTH    10000

//define offset in the config
#define RESOLUTION_LOCATION      234
#define TRIGGER_LOCATION         286

//Normal config setting
/*****************************End of config data*****************************/

/***************************Start of struct declare**************************/
struct fw_subsystem_info {
    int type;
    int length;
    u32 address;
    int offset;
};

struct fw_info {
    u32 length;
    u16 checksum;
    u8  target_mask[6];
    u8  target_mask_version[3];
    u8  pid[6];
    u8  version[3];
    u8  subsystem_count;
    u8  chip_type;
    u8  reserved[6];
    struct fw_subsystem_info subsystem[12];
};

//stuct of firmware update

/**
 * fw_subsys_info - subsytem firmware infomation
 * @type: sybsystem type
 * @size: firmware size
 * @flash_addr: flash address
 * @data: firmware data
 */
struct fw_subsys_info {
    u8       type;
    u32      size;
    u16      flash_addr;
    const u8 *data;
};

/**
 * firmware_info
 * @size: fw total length
 * @checksum: checksum of fw
 * @hw_pid: mask pid string
 * @hw_pid: mask vid code
 * @fw_pid: fw pid string
 * @fw_vid: fw vid code
 * @subsys_num: number of fw subsystem
 * @chip_type: chip type
 * @protocol_ver: firmware packing
 *     protocol version
 * @subsys: sybsystem info
 */
struct firmware_info {
    u32 size;
    u16 checksum;
    u8  hw_pid[6];
    u8  hw_vid[3];
    u8  fw_pid[8];
    u8  fw_vid[4];
    u8  subsys_num;
    u8  chip_type;
    u8  protocol_ver;
    u8  reserved[2];
    struct fw_subsys_info subsys[FW_SUBSYS_MAX_NUM];
};

/**
 * firmware_data - firmware data structure
 * @fw_info: firmware infomation
 * @firmware: firmware data structure
 */
struct firmware_data {
    struct firmware_info  fw_info;
    const struct firmware *firmware;
};

enum update_status {
    UPSTA_NOTWORK = 0,
    UPSTA_PREPARING,
    UPSTA_UPDATING,
    UPSTA_ABORT,
    UPSTA_SUCCESS,
    UPSTA_FAILED
};

/**
 * fw_update_ctrl - sturcture used to control the
 *    firmware update process
 * @status: update status
 * @progress: indicate the progress of update
 * @fw_data: firmware data
 * @ts_dev: touch device
 */
struct fw_update_ctrl {
    enum update_status    status;
    unsigned int progress;
    bool force_update;
    struct firmware_data fw_data;
};


struct goodix_register {
    uint16_t GTP_REG_FW_CHK_MAINSYS;        /*mainsys reg used to check fw status*/
    uint16_t GTP_REG_FW_CHK_SUBSYS;         /*subsys reg used to check fw status*/
    uint16_t GTP_REG_CONFIG_DATA;           /*configure firmware*/
    uint16_t GTP_REG_READ_COOR;             /*touch state and info*/
    uint16_t GTP_REG_PRODUCT_VER;           /*product id & version*/
    uint16_t GTP_REG_WAKEUP_GESTURE;        /*gesture type*/
    uint16_t GTP_REG_GESTURE_COOR;          /*gesture point data*/
    uint16_t GTP_REG_CMD;                   /*recevice cmd from host*/
    uint16_t GTP_REG_RQST;                  /*request from ic*/
    uint16_t GTP_REG_NOISE_DETECT;          /*noise state*/
    uint16_t GTP_REG_ESD_WRITE;             /*esd state write*/
    uint16_t GTP_REG_ESD_READ;              /*esd state read*/
    uint16_t GTP_REG_DEBUG;                 /*debug log*/
    uint16_t GTP_REG_DOWN_DIFFDATA;         /*down diff data log*/
    uint16_t GTP_REG_EDGE_INFO;             /*edge points' info:ewx/ewy/xer/yer*/

    uint16_t GTP_REG_RAWDATA;
    uint16_t GTP_REG_DIFFDATA;
    uint16_t GTP_REG_BASEDATA;
    uint16_t GTP_REG_DETAILED_DEBUG_INFO;
};

struct config_info {
    u8      goodix_int_type;
    u32     goodix_abs_x_max;
    u32     goodix_abs_y_max;
};

struct goodix_version_info {
    u8   product_id[5];
    u32  patch_id;
    u32  mask_id;
    u8   sensor_id;
    u8   match_opt;
};
#define MAX_STR_LEN                 32

/*
 * struct gtx8_ts_config - chip config data
 * @initialized: whether intialized
 * @name: name of this config
 * @lock: mutex
 * @reg_base: register base of config data
 * @length: bytes of the config
 * @delay: delay time after sending config
 * @data: config data buffer
 */
struct gtx8_ts_config {
    bool initialized;
    char name[MAX_STR_LEN + 1];
    struct mutex lock;
    unsigned int reg_base;
    unsigned int length;
    unsigned int delay; /*ms*/
    unsigned char data[GOODIX_CFG_MAX_SIZE];
};

struct goodix_fp_coor {
    u16 fp_x_coor;
    u16 fp_y_coor;
    u16 fp_area;
};

struct  chip_data_gt9886 {
    bool                                halt_status;                    //1: need ic reset
    u8                                  *touch_data;
    u8                                  *edge_data;
    tp_dev                              tp_type;
    u16                                 *spuri_fp_touch_raw_data;
    struct i2c_client                   *client;
    struct goodix_version_info          ver_info;
    struct config_info                  config_info;
    struct goodix_register              reg_info;
    struct fw_update_info               update_info;
    struct hw_resource                  *hw_res;
    struct monitor_data_v2              *monitor_data_v2;
    struct goodix_proc_operations       *goodix_ops;                    //goodix func provide for debug
    struct goodix_fp_coor               fp_coor_report;
    struct goodix_health_info           health_info;
    struct gtx8_ts_config               normal_cfg;
    struct gtx8_ts_config               normal_noise_cfg;
    struct gtx8_ts_config               test_cfg;
    struct gtx8_ts_config               noise_test_cfg;

    bool                                esd_check_enabled;
    bool                                fp_down_flag;
    bool                                single_tap_flag;
    uint8_t                             touch_direction;
    char                                *p_tp_fw;
    bool                                kernel_grip_support;
    bool                                detail_debug_info_support;
    //add for healthinfo
    unsigned int esd_err_count;
    unsigned int send_cmd_err_count;
};

/**
 * struct ts_test_params - test parameters
 * drv_num: touch panel tx(driver) number
 * sen_num: touch panel tx(sensor) number
 * max_limits: max limits of rawdata
 * min_limits: min limits of rawdata
 * deviation_limits: channel deviation limits
 * short_threshold: short resistance threshold
 * r_drv_drv_threshold: resistance threshold between drv and drv
 * r_drv_sen_threshold: resistance threshold between drv and sen
 * r_sen_sen_threshold: resistance threshold between sen and sen
 * r_drv_gnd_threshold: resistance threshold between drv and gnd
 * r_sen_gnd_threshold: resistance threshold between sen and gnd
 * avdd_value: avdd voltage value
 */
struct ts_test_params {
    u16 rawdata_addr;
    u16 noisedata_addr;
    u16 self_rawdata_addr;
    u16 self_noisedata_addr;

    u16 basedata_addr;
    u32 max_drv_num;
    u32 max_sen_num;
    u32 drv_num;
    u32 sen_num;
    u8 *drv_map;
    u8 *sen_map;

    u32 *max_limits;
    u32 *min_limits;

    u32 *deviation_limits;;
    u32 *self_max_limits;
    u32 *self_min_limits;

    u32 noise_threshold;
    u32 self_noise_threshold;
    u32 short_threshold;
    u32 r_drv_drv_threshold;
    u32 r_drv_sen_threshold;
    u32 r_sen_sen_threshold;
    u32 r_drv_gnd_threshold;
    u32 r_sen_gnd_threshold;
    u32 avdd_value;
};

/**
 * struct ts_test_rawdata - rawdata structure
 * data: rawdata buffer
 * size: rawdata size
 */
struct ts_test_rawdata {
    u16 *data;
    u32 size;
};

struct ts_test_self_rawdata {
    u16 *data;
    u32 size;
};

/*
 * struct goodix_ts_cmd - command package
 * @initialized: whether initialized
 * @cmd_reg: command register
 * @length: command length in bytes
 * @cmds: command data,0:cmd,1:data,2:checksum
 */
#pragma pack(4)
struct goodix_ts_cmd {
    u32 initialized;
    u32 cmd_reg;
    u32 length;
    u8 cmds[3];
};
#pragma pack()

/*
 * struct goodix_ts_config - chip config data
 * @initialized: whether intialized
 * @name: name of this config
 * @lock: mutex
 * @reg_base: register base of config data
 * @length: bytes of the config
 * @delay: delay time after sending config
 * @data: config data buffer
 */
struct goodix_ts_config {
    bool initialized;
    char name[24];
    struct mutex lock;
    unsigned int reg_base;
    unsigned int length;
    unsigned int delay; /*ms*/
    unsigned char data[GOODIX_CFG_MAX_SIZE];
};

/**
 * struct gtx8_ts_test - main data structrue
 * ts: gtx8 touch screen data
 * test_config: test mode config data
 * orig_config: original config data
 * noise_config: noise config data
 * test_param: test parameters from limit img
 * rawdata: raw data structure from ic data
 * noisedata: noise data structure from ic data
 * self_rawdata: self raw data structure from ic data
 * self_noisedata: self noise data structure from ic data
 * test_result: test result string
 */
struct gtx8_ts_test {
    void *ts;
    struct goodix_ts_config test_config;
    struct goodix_ts_config orig_config;
    struct goodix_ts_config noise_config;
    struct goodix_ts_cmd rawdata_cmd;
    struct goodix_ts_cmd normal_cmd;
    struct ts_test_params test_params;
    bool   is_item_support[MAX_TEST_ITEMS];
    struct ts_test_rawdata rawdata;
    struct ts_test_rawdata noisedata;
    struct ts_test_self_rawdata self_rawdata;
    struct ts_test_self_rawdata self_noisedata;

    struct goodix_testdata *p_testdata;
    struct seq_file *p_seq_file;
    /*[0][0][0][0][0]..  0 without test; 1 pass, 2 panel failed; 3 software failed */
    char test_result[MAX_TEST_ITEMS];
    int error_count;
    uint64_t      device_tp_fw;
};

struct short_record {
    u32 master;
    u32 slave;
    u16 short_code;
    u8 group1;
    u8 group2;
};

/****************************End of struct declare***************************/

extern int gt8x_rawdiff_mode;

static inline u8 checksum_u8(u8 *data, u32 size)
{
    u8 checksum = 0;
    u32 i = 0;
    for(i = 0; i < size; i++) {
        checksum += data[i];
    }
    return checksum;
}

#endif/*TOUCHPANEL_GOODIX_9886_H*/
