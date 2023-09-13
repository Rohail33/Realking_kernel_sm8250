/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _TOUCHPANEL_COMMON_GOODIX_H_
#define _TOUCHPANEL_COMMON_GOODIX_H_

#include <linux/uaccess.h>
#include "../touchpanel_common.h"
#include "../touchpanel_healthinfo.h"

#include <linux/firmware.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>

#define IS_NUM_OR_CHAR(x) (((x) >= 'A' && (x) <= 'Z') || ((x) >= '0' && (x) <= '9'))

/****************************PART1:auto test define*************************************/
#define Limit_MagicNum1     0x494D494C
#define Limit_MagicNum2     0x474D4954
#define Limit_ItemMagic     0x4F50504F
#define Limit_ItemMagic_V2  0x4F504C53

struct auto_test_header {
    uint32_t magic1;
    uint32_t magic2;
    uint64_t test_item;
};

struct auto_test_item_header {
    uint32_t    item_magic;
    uint32_t    item_size;
    uint16_t    item_bit;
    uint16_t    item_limit_type;
    uint32_t    top_limit_offset;
    uint32_t    floor_limit_offset;
    uint32_t    para_num;
};

/*get item information*/
struct test_item_info {
    uint32_t    item_magic;
    uint32_t    item_size;
    uint16_t    item_bit;
    uint16_t    item_limit_type;
    uint32_t    top_limit_offset;
    uint32_t    floor_limit_offset;
    uint32_t    para_num;               /*number of parameter*/
    int32_t     *p_buffer;              /*pointer to item parameter buffer*/
    uint32_t    item_offset;            /*item offset*/
};

struct goodix_testdata {
    int           TX_NUM;
    int           RX_NUM;

    struct file   *fp;
    loff_t        pos;
    int           irq_gpio;
    int           key_TX;
    int           key_RX;
    uint64_t      TP_FW;
    const struct  firmware *fw;
    uint64_t      test_item;
};

enum {
    LIMIT_TYPE_NO_DATA          = 0x00,     //means no limit data
    LIMIT_TYPE_CERTAIN_DATA     = 0x01,     //means all nodes limit data is a certain data
    LIMIT_TYPE_MAX_MIN_DATA     = 0x02,     //means all nodes have it's own limit
    IMIT_TYPE_DELTA_DATA        = 0x03,     //means all nodes have it's own limit
    IMIT_TYPE_PARA_DATA         = 0x04,     //means all nodes have it's own limit
    IMIT_TYPE_SLEFRAW_DATA      = 0x05,     //means all nodes have it's own limit
    LIMIT_TYPE_INVALID_DATA     = 0xFF,     //means wrong limit data type
};

//test item
enum {
    //TYPE_ERROR                              = 0x00,
    TYPE_MUTUAL_RAW_OFFSET_DATA_SDC         = 0x01,
    TYPE_MUTUAL_RAW_DATA                    = 0x02,
    TYPE_SELF_RAW_OFFSET_DATA_SDC           = 0x03,
    TYPE_MUTU_RAW_NOI_P2P                   = 0x04,
    //TYPE_MAX                                = 0xFF,
};

//test item
enum {
    TYPE_ERROR                              = 0x00,
    TYPE_NOISE_DATA_LIMIT                   = 0x01,
    TYPE_SHORT_THRESHOLD                    = 0x02,
    TYPE_SPECIAL_RAW_MAX_MIN                = 0x03,
    TYPE_SPECIAL_RAW_DELTA                  = 0x04,
    TYPE_NOISE_SLEFDATA_LIMIT               = 0x05,
    TYPE_SPECIAL_SELFRAW_MAX_MIN            = 0x06,
    TYPE_MAX                                = 0xFF,
};

/****************************PART2:FW Update define*************************************/
#define FW_HEAD_SIZE                         128
#define FW_HEAD_SUBSYSTEM_INFO_SIZE          8
#define FW_HEAD_OFFSET_SUBSYSTEM_INFO_BASE   32
#define PACK_SIZE                            256
#define UPDATE_STATUS_IDLE                   0
#define UPDATE_STATUS_RUNNING                1
#define UPDATE_STATUS_ABORT                  2
#define FW_SECTION_TYPE_SS51_PATCH           0x02

typedef enum {
    GTP_RAWDATA,
    GTP_DIFFDATA,
    GTP_BASEDATA,
} debug_type;

struct goodix_proc_operations {
    void     (*goodix_config_info_read) (struct seq_file *s, void *chip_data);
    size_t   (*goodix_water_protect_write) (struct file *file, const char *buff,  size_t len, loff_t *pos);
    size_t   (*goodix_water_protect_read) (struct file *file, char *buff, size_t len, loff_t *pos);
    void     (*auto_test) (struct seq_file *s, void *chip_data, struct goodix_testdata *p_testdata);
    int      (*set_health_info_state)  (void *chip_data, uint8_t enable);
    int      (*get_health_info_state)  (void *chip_data);
};

struct fw_update_info {
    u8              *buffer;
    u8              *firmware_file_data;
    u32             fw_length;
    int             status;
    int             progress;
    int             max_progress;
    struct fw_info  *firmware;
};

typedef enum {
    BASE_DC_COMPONENT =0X01,
    BASE_SYS_UPDATE =0X02,
    BASE_NEGATIVE_FINGER = 0x03,
    BASE_MONITOR_UPDATE= 0x04,
    BASE_CONSISTENCE = 0x06,
    BASE_FORCE_UPDATE = 0x07,
} BASELINE_ERR;

typedef enum {
    RST_MAIN_REG = 0x01,
    RST_OVERLAY_ERROR = 0x02,
    RST_LOAD_OVERLAY = 0x03,
    RST_CHECK_PID = 0x04,
    RST_CHECK_RAM = 0x06,
    RST_CHECK_RAWDATA = 0x07,
} RESET_REASON;

struct goodix_health_info {
    uint8_t   shield_water:1;
    uint8_t   shield_freq:1;
    uint8_t   baseline_refresh:1;
    uint8_t   fw_rst:1;
    uint8_t   shield_palm:1;
    uint8_t   reserve_bit:3;
    uint8_t   reserve;
    uint8_t   freq_before;
    uint8_t   freq_after;
    uint8_t   baseline_refresh_type;
    uint8_t   reset_reason;
    uint8_t   reserve1;
    uint8_t   reserve2;
    uint8_t   reserve3;
    uint8_t   reserve4;
    uint8_t   reserve5;
    uint8_t   reserve6;
    uint8_t   reserve7;
    uint8_t   reserve8;
    uint8_t   reserve9;
    uint8_t   reserve10;
    uint8_t   reserve11;
    uint8_t   reserve12;
    uint8_t   reserve13;
    uint8_t   reserve14;
    uint8_t   checksum;
};

/****************************PART3:FUNCTION*************************************/
void GetCirclePoints(struct Coordinate *input_points, int number, struct Coordinate  *pPnts);
int ClockWise(struct Coordinate *p, int n);
int Goodix_create_proc(struct touchpanel_data *ts, struct goodix_proc_operations *goodix_ops);

uint32_t search_for_item_offset(const struct firmware *fw, int item_cnt, uint8_t item_index);
int32_t *getpara_for_item(const struct firmware *fw, uint8_t item_index, uint32_t *para_num);
struct test_item_info *get_test_item_info(const struct firmware *fw, uint8_t item_index);
void goodix_limit_read(struct seq_file *s, struct touchpanel_data *ts);
void tp_kfree(void **mem);

#endif

