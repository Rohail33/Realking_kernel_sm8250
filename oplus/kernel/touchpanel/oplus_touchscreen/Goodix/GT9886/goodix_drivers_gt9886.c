// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>

#include "goodix_drivers_gt9886.h"

//struct chip_data_gt9886 *g_chip_info = NULL;
extern int tp_register_times;

//function delcare
static int goodix_reset(void *chip_data);
static u8 goodix_config_version_read(struct chip_data_gt9886 *chip_info);
static void gtx8_check_setting_group(struct gtx8_ts_test *ts_test, struct short_record *r_data);

/********** Start of special i2c tranfer interface used for goodix read/write*****************/
/**
 * gtx8_set_i2c_doze_mode - disable or enable doze mode
 * @enable: true/flase
 * return: 0 - ok; < 0 - error.
 * This func must be used in pairs, when you disable doze
 * mode, then you must enable it again.
 * Between set_doze_false and set_doze_true, do not reset
 * IC!
*/
static int goodix_set_i2c_doze_mode(struct i2c_client *client, int enable)
{
#define GTP_REG_DOZE_CTRL     0x30F0
#define GTP_REG_DOZE_STAT     0x3100
#define DOZE_ENABLE_FLAG      0xCC
#define DOZE_DISABLE_FLAG     0xAA
#define DOZE_CLOSE_OK_FLAG    0xBB
    int result = -EINVAL;
    int i = 0;
    u8 w_data = 0, r_data = 0;
    static u32 doze_mode_set_count = 0;
    static DEFINE_MUTEX(doze_mode_lock);
    int retry_times = 0;

    mutex_lock(&doze_mode_lock);

    if (enable) {
        if (doze_mode_set_count != 0) {
            doze_mode_set_count--;
        }

        /*when count equal 0, allow ic enter doze mode*/
        if (doze_mode_set_count == 0) {
            w_data = DOZE_ENABLE_FLAG;
            for (i = 0; i < 3; i++) {
                if (touch_i2c_write_block(client, GTP_REG_DOZE_CTRL, 1, &w_data) >= 0) {
                    result = 0;
                    goto exit;
                }
                usleep_range(1000, 1100);
            }
        } else {
            result = 0;
            goto exit;
        }
    } else {
        doze_mode_set_count++;
        if (doze_mode_set_count == 1) {
            w_data = DOZE_DISABLE_FLAG;
            if (touch_i2c_write_block(client, GTP_REG_DOZE_CTRL, 1, &w_data) < 0) {
                TPD_INFO("doze mode comunition disable FAILED\n");
                goto exit;
            }
            usleep_range(8000, 8100);
            for (i = 0; i < 20; i++) {
                if (touch_i2c_read_block(client, GTP_REG_DOZE_STAT, 1, &r_data) < 0) {
                    TPD_INFO("doze mode comunition disable FAILED\n");
                    goto exit;
                }
                if (DOZE_CLOSE_OK_FLAG == r_data) {
                    result = 0;
                    goto exit;
                } else if (DOZE_DISABLE_FLAG != r_data) {
                    w_data = DOZE_DISABLE_FLAG;
                    if (touch_i2c_write_block(client, GTP_REG_DOZE_CTRL, 1, &w_data) < 0) {
                        TPD_INFO("doze mode comunition disable FAILED, 0x%x!=0x%x\n", r_data, DOZE_DISABLE_FLAG);
                        goto exit;
                    }
                }
                retry_times++;
                if(retry_times > 5) {
                    TPD_INFO("doze mode wait flag timeout,should clear int\n");
                    w_data = 0;
                    touch_i2c_write_block(client, 0x4100, 1, &w_data);
                }
                usleep_range(10000, 11000);
            }
            TPD_INFO("doze mode disable FAILED\n");
        } else {
            result = 0;
            goto exit;
        }
    }

exit:
    mutex_unlock(&doze_mode_lock);
    return result;
}

/*
 * goodix_i2c_read_wrapper
 *
 * */
static int gt9886_i2c_read(struct i2c_client *client, u16 addr, s32 len, u8 *buffer)
{
    int ret = -EINVAL;

    if (goodix_set_i2c_doze_mode(client, false) != 0) {
        TPD_INFO("gtx8 i2c read:0x%04x ERROR, disable doze mode FAILED\n", addr);
    }
    ret = touch_i2c_read_block(client, addr, len, buffer);

    if (goodix_set_i2c_doze_mode(client, true) != 0)
        TPD_INFO("gtx8 i2c read:0x%04x ERROR, enable doze mode FAILED\n", addr);

    return ret;
}

static int gt9886_i2c_write(struct i2c_client *client, u16 addr, s32 len, u8 *buffer)
{
    int ret = -EINVAL;

    if (goodix_set_i2c_doze_mode(client, false) != 0) {
        TPD_INFO("gtx8 i2c write:0x%04x ERROR, disable doze mode FAILED\n", addr);
    }
    ret = touch_i2c_write_block(client, addr, len, buffer);

    if (goodix_set_i2c_doze_mode(client, true) != 0) {
        TPD_INFO("gtx8 i2c write:0x%04x ERROR, enable doze mode FAILED\n", addr);
    }

    return ret;
}

/*
 *goodix_i2c_read_dbl_check----used to double read and check the reading data
 *@client : Handle to slave device
 *@addr   : address of register where to start read
 *@buffer : buf used to store data read from register
 *@len    : data length we want to read
 return  0: success, non-0: failed
*/
static int goodix_i2c_read_dbl_check(struct i2c_client *client, u16 addr, u8 *buffer, s32 len)
{
    u8 buf[16] = {0};
    u8 confirm_buf[16] = {0};
    int ret = -1;

    if (len > 16) {
        TPD_INFO("i2c_read_dbl_check length %d is too long, exceed %zu\n", len, sizeof(buf));
        return ret;
    }

    memset(buf, 0xAA, sizeof(buf));
    ret = gt9886_i2c_read(client, addr, len, buf);
    if (ret < 0) {
        return ret;
    }

    msleep(5);
    memset(confirm_buf, 0, sizeof(confirm_buf));
    ret = gt9886_i2c_read(client, addr, len, confirm_buf);
    if (ret < 0) {
        return ret;
    }

    if (!memcmp(buf, confirm_buf, len)) {
        memcpy(buffer, confirm_buf, len);
        return 0;
    }

    TPD_INFO("i2c read 0x%04X, %d bytes, double check failed!\n", addr, len);

    return 1;
}

//#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
//extern unsigned int upmu_get_rgs_chrdet(void);
//static int goodix_get_usb_state(void)
//{
//    int ret = 0;
//    ret = upmu_get_rgs_chrdet();
//    TPD_INFO("%s get usb status %d\n", __func__, ret);
//    return ret;
//}
//#else
//static int goodix_get_usb_state(void)
//{
//    return 0;
//}
//#endif

/*
 * goodix_send_cmd----send cmd to Goodix ic to control it's function
 * @chip_data: Handle to slave device
 * @cmd: value for cmd reg(0x8040)
 * @data:value for data reg(0x8041)
 * Return  0: succeed, non-0: failed.
 */

static DEFINE_MUTEX(cmd_mutex);
s32 goodix_send_cmd(void *chip_data, u8 cmd, u8 data)
{
    s32 ret = -1;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    u8 buffer[3] = { cmd, data, 0 };
    u8 buf_read_back = 0;
    int retry = 20;
    int retry_times = 0;

    /* cmd format:
     * reg:0x6f68  cmd control(diffrent cmd indicates diffrent setting for touch IC )
     * reg:0x6f69  data reg(Correspond with 0x8040)
     * reg:0x6f6a  checksum (sum(0x8040~0x8042) == 0) */
    mutex_lock(&cmd_mutex);
    buffer[2] = (u8) ((0 - cmd - data) & 0xFF);

    if (goodix_set_i2c_doze_mode(chip_info->client, false) != 0) {
        TPD_INFO("%s: disable doze mode FAILED\n", __func__);
    }
    ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.GTP_REG_CMD, 3, &buffer[0]);
    if (ret < 0) {
        TPD_INFO("%s send cmd failed,ret:%d\n", __func__, ret);
        goto exit;
    }
    usleep_range(8000, 8100);
    while(retry--) {
        ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_CMD, 1, &buf_read_back);
        if (ret < 0) {
            usleep_range(10000, 11000);
            TPD_INFO("%s,read back failed\n", __func__);
            continue;
        }
        if (buf_read_back != cmd || cmd == TS_CMD_REG_READY) {
            break;
        } else {
            TPD_DEBUG("%s: cmd isn't been handle, wait for it at: %d, readbak: 0x%x\n", __func__, retry, buf_read_back);
        }
        retry_times++;
        if (retry_times > 5) {
            TPD_INFO("%s: wait cmd flag failed, clear int\n", __func__);
            buf_read_back = 0;
            touch_i2c_write_block(chip_info->client, 0x4100, 1, &buf_read_back);
            chip_info->send_cmd_err_count ++;
            tp_healthinfo_report(chip_info->monitor_data_v2, HEALTH_REPORT, "SENT_CMD_ERR");
        }
        usleep_range(10000, 11000);
    }
    if (retry) {
        ret = 0;
    } else {
        TPD_INFO("%s: cmd send to ic,but ic isn't handle it\n", __func__);
        ret = -1;
    }

exit:
    if (goodix_set_i2c_doze_mode(chip_info->client, true) != 0) {
        TPD_INFO("%s: enable doze mode FAILED\n", __func__);
        ret = -1;
    }

    mutex_unlock(&cmd_mutex);

    return ret;
}
/*********** End of special i2c tranfer interface used for goodix read/write******************/


/********* Start of function that work for oplus_touchpanel_operations callbacks***************/
static int goodix_clear_irq(void *chip_data)
{
    int ret = -1;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    u8 clean_flag = 0;

    if (!gt8x_rawdiff_mode) {
        ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, &clean_flag);
        if (ret < 0) {
            TPD_INFO("I2C write end_cmd  error!\n");
        }
    }

    return ret;
}

static void getSpecialCornerPoint(uint8_t *buf, int n, struct Coordinate *point)
{
    int x, y, i;

    point[0].x = (buf[0] & 0xFF) | (buf[1] & 0x0F) << 8;
    point[0].y = (buf[2] & 0xFF) | (buf[3] & 0x0F) << 8;
    point[1] = point[0];
    point[2] = point[0];
    point[3] = point[0];
    for (i = 0; i < n; i++) {
        x = (buf[0 + 4 * i] & 0xFF) | (buf[1 + 4 * i] & 0x0F) << 8;
        y = (buf[2 + 4 * i] & 0xFF) | (buf[3 + 4 * i] & 0x0F) << 8;
        if (point[3].x < x) {   //xmax
            point[3].x = x;
            point[3].y = y;
        }
        if (point[1].x > x) {   //xmin
            point[1].x = x;
            point[1].y = y;
        }
        if (point[2].y < y) {   //ymax
            point[2].y = y;
            point[2].x = x;
        }
        if (point[0].y > y) {   //ymin
            point[0].y = y;
            point[0].x = x;
        }
    }
}

static int clockWise(uint8_t *buf, int n)
{
    int i, j, k;
    int count = 0;
    struct Coordinate p[3];
    long int z;

    if (n < 3)
        return 1;
    for (i = 0; i < n; i++) {
        j = (i + 1) % n;
        k = (i + 2) % n;
        p[0].x = (buf[0 + 4 * i] & 0xFF) | (buf[1 + 4 * i] & 0x0F) << 8;
        p[0].y = (buf[2 + 4 * i] & 0xFF) | (buf[3 + 4 * i] & 0x0F) << 8;
        p[1].x = (buf[0 + 4 * j] & 0xFF) | (buf[1 + 4 * j] & 0x0F) << 8;
        p[1].y = (buf[2 + 4 * j] & 0xFF) | (buf[3 + 4 * j] & 0x0F) << 8;
        p[2].x = (buf[0 + 4 * k] & 0xFF) | (buf[1 + 4 * k] & 0x0F) << 8;
        p[2].y = (buf[2 + 4 * k] & 0xFF) | (buf[3 + 4 * k] & 0x0F) << 8;
        if ((p[0].x == p[1].x) && (p[1].x == p[1].y))
            continue;
        z = (p[1].x - p[0].x) * (p[2].y - p[1].y);
        z -= (p[1].y - p[0].y) * (p[2].x - p[1].x);
        if (z < 0)
            count--;
        else if (z > 0)
            count++;
    }

    TPD_INFO("ClockWise count = %d\n", count);

    if (count > 0)
        return 1;
    else
        return 0;
}

static void goodix_esd_check_enable(struct chip_data_gt9886 *chip_info, bool enable)
{
    TPD_INFO("%s %s\n", __func__, enable ? "enable" : "disable");
    /* enable/disable esd check flag */
    chip_info->esd_check_enabled = enable;
}

static int goodix_enter_sleep(struct chip_data_gt9886 *chip_info, bool config)
{
    s32 retry = 0;

    if (config) {
        while (retry++ < 3) {
            if (!goodix_send_cmd(chip_info, GTP_CMD_SLEEP, 0)) {
                chip_info->halt_status = true;
                TPD_INFO("enter sleep mode!\n");
                return 0;
            }
            msleep(10);
        }
    }

    if (retry >= 3) {
        TPD_INFO("Enter sleep mode failed.\n");
    }

    return -1;
}

static int goodix_enable_gesture(struct chip_data_gt9886 *chip_info, bool enable)
{
    int ret = -1;

    TPD_INFO("%s, gesture enable = %d\n", __func__, enable);

    if (enable) {
        if (chip_info->single_tap_flag) {
            ret = goodix_send_cmd(chip_info, GTP_CMD_GESTURE_ON, 1);
        } else {
            ret = goodix_send_cmd(chip_info, GTP_CMD_GESTURE_ON, 0);
        }
    } else {
        ret = goodix_send_cmd(chip_info, GTP_CMD_GESTURE_OFF, 0);
    }

    return ret;
}


static int goodix_enable_edge_limit(struct chip_data_gt9886 *chip_info, int state)
{
    int ret = -1;

    TPD_INFO("%s, edge limit enable = %d\n", __func__, state);

    if (state == 1 || VERTICAL_SCREEN == chip_info->touch_direction) {
        ret = goodix_send_cmd(chip_info, GTM_CMD_EDGE_LIMIT_VERTICAL, 0x00);
    } else {
        if (LANDSCAPE_SCREEN_90 == chip_info->touch_direction) {
            ret = goodix_send_cmd(chip_info, GTM_CMD_EDGE_LIMIT_LANDSCAPE, 0x00);
        } else if (LANDSCAPE_SCREEN_270 == chip_info->touch_direction) {
            ret = goodix_send_cmd(chip_info, GTM_CMD_EDGE_LIMIT_LANDSCAPE, 0x01);
        }
    }

    return ret;
}


static int goodix_enable_charge_mode(struct chip_data_gt9886 *chip_info, bool enable)
{
    int ret = -1;

    TPD_INFO("%s, charge mode enable = %d\n", __func__, enable);

    if (enable) {
        ret = goodix_send_cmd(chip_info, GTP_CMD_CHARGER_ON, 0);
    } else {
        ret = goodix_send_cmd(chip_info, GTP_CMD_CHARGER_OFF, 0);
    }

    return ret;
}

static int goodix_enable_game_mode(struct chip_data_gt9886 *chip_info, bool enable)
{
    int ret = 0;

    TPD_INFO("%s, game mode enable = %d\n", __func__, enable);
    if(enable) {
        ret = goodix_send_cmd(chip_info, GTP_CMD_GAME_MODE, 0x01);
        TPD_INFO("%s: GTP_CMD_ENTER_GAME_MODE\n", __func__);
    } else {
        ret = goodix_send_cmd(chip_info, GTP_CMD_GAME_MODE, 0x00);
        TPD_INFO("%s: GTP_CMD_EXIT_GAME_MODE\n", __func__);
    }

    return ret;
}

static int goodix_reset_esd_status(struct chip_data_gt9886 *chip_info)
{
    /* reset esd state */
    u8 value = 0xAA;
    int ret;

    ret = gt9886_i2c_write(chip_info->client, chip_info->reg_info.GTP_REG_ESD_WRITE, 1, &value);
    if (ret < 0) {
        TPD_INFO("%s: TP set_reset_status failed\n", __func__);
        return ret;
    }

    return 0;
}

static s32 goodix_read_version(struct chip_data_gt9886 *chip_info, struct panel_info *panel_data)
{
    s32 ret = -1;
    u8 buf[72] = {0};
    u32 mask_id = 0;
    u32 patch_id = 0;
    u8 product_id[5] = {0};
    u8 sensor_id = 0;
    //u8 match_opt = 0;
    int i, retry = 3;
    u8 checksum = 0;
    struct goodix_version_info *ver_info = &chip_info->ver_info;

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);

    while (retry--) {
        ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_PRODUCT_VER, sizeof(buf), buf);
        if (ret > 0) {
            checksum = 0;

            for (i = 0; i < sizeof(buf); i++) {
                checksum += buf[i];
            }

            if (checksum == 0 &&    /* first 3 bytes must be number or char */
                IS_NUM_OR_CHAR(buf[9]) && IS_NUM_OR_CHAR(buf[10]) && IS_NUM_OR_CHAR(buf[11]) && buf[21] != 0xFF) {    /*sensor id == 0xFF, retry */
                break;
            } else {
                TPD_INFO("product version data is error\n");
            }
        } else {
            TPD_INFO("Read product version from 0x452C failed\n");
        }

        TPD_DEBUG("Read product version retry = %d\n", retry);
        msleep(100);
    }

    if (retry <= 0) {
        if (ver_info)
            ver_info->sensor_id = 0;
        TPD_INFO("Maybe the firmware of ic is error\n");
        goodix_set_i2c_doze_mode(chip_info->client, true);
        return -1;
    }

    mask_id = (u32) ((buf[6] << 16) | (buf[7] << 8 ) | buf[8]);
    patch_id = (u32) ((buf[17] << 24) | (buf[18] << 16) | buf[19] << 8 | buf[20]);
    memcpy(product_id, &buf[9], 4);
    sensor_id = buf[21] & 0x0F;
    //match_opt = (buf[10] >> 4) & 0x0F;

    TPD_INFO("goodix fw VERSION:GT%s(Product)_%08X(Patch)_%06X(MaskID)_%02X(SensorID)\n", product_id, patch_id, mask_id, sensor_id);

    if (ver_info != NULL) {
        ver_info->mask_id = mask_id;
        ver_info->patch_id = patch_id;
        memcpy(ver_info->product_id, product_id, 5);
        ver_info->sensor_id = sensor_id;
        //ver_info->match_opt = match_opt;
    }

    /*enable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, true);

    return patch_id;
}

static int goodix_check_cfg_valid(u8 *cfg, int length)
{
    int ret;
    u8 bag_num;
    u8 checksum;
    int i, j;
    int bag_start = 0;
    int bag_end = 0;
    if (!cfg || length < TS_CFG_HEAD_LEN) {
        TPD_INFO("cfg is INVALID, len:%d", length);
        ret = -EINVAL;
        goto exit;
    }

    checksum = 0;
    for (i = 0; i < TS_CFG_HEAD_LEN; i++)
        checksum += cfg[i];
    if (checksum != 0) {
        TPD_INFO("cfg head checksum ERROR, ic type:normandy, checksum:0x%02x",
                 checksum);
        ret = -EINVAL;
        goto exit;
    }
    bag_num = cfg[TS_CFG_BAG_NUM_INDEX];
    bag_start = TS_CFG_BAG_START_INDEX;

    TPD_INFO("cfg bag_num:%d, cfg length:%d", bag_num, length);

    /*check each bag's checksum*/
    for (j = 0; j < bag_num; j++) {
        if (bag_start >= length - 1) {
            TPD_INFO("ERROR, overflow!!bag_start:%d, cfg_len:%d", bag_start, length);
            ret = -EINVAL;
            goto exit;
        }

        bag_end = bag_start + cfg[bag_start + 1] + 3;

        checksum = 0;
        if (bag_end > length) {
            TPD_INFO("ERROR, overflow!!bag:%d, bag_start:%d,  bag_end:%d, cfg length:%d",
                     j, bag_start, bag_end, length);
            ret = -EINVAL;
            goto exit;
        }
        for (i = bag_start; i < bag_end; i++)
            checksum += cfg[i];
        if (checksum != 0) {
            TPD_INFO("cfg INVALID, bag:%d checksum ERROR:0x%02x", j, checksum);
            ret = -EINVAL;
            goto exit;
        }
        bag_start = bag_end;
    }

    ret = 0;
    TPD_INFO("configuration check SUCCESS");

exit:
    return ret;
}


static int goodix_wait_cfg_cmd_ready(struct chip_data_gt9886 *chip_info,
                                     u8 right_cmd, u8 send_cmd)
{
    int try_times = 0;
    u8 cmd_flag = 0;
    u8 cmd_buf[3] = {0};
    u16 command_reg = chip_info->reg_info.GTP_REG_CMD;

    for (try_times = 0; try_times < TS_WAIT_CFG_READY_RETRY_TIMES; try_times++) {
        if (gt9886_i2c_read(chip_info->client, command_reg, 3, cmd_buf) < 0) {
            TPD_INFO("Read cmd_reg error");
            return -EINVAL;
        }
        cmd_flag = cmd_buf[0];
        if (cmd_flag == right_cmd) {
            return 0;
        } else if (cmd_flag != send_cmd) {
            TPD_INFO("Read cmd_reg data abnormal,return:0x%02X, 0x%02X, 0x%02X, send again",
                     cmd_buf[0], cmd_buf[1], cmd_buf[2]);
            if (goodix_send_cmd(chip_info, send_cmd, 0)) {
                TPD_INFO("Resend cmd 0x%02X FAILED", send_cmd);
                return -EINVAL;
            }
        }
        usleep_range(10000, 11000);
    }

    return -EINVAL;
}


static int goodix_send_large_config(struct chip_data_gt9886 *chip_info, u8 *config, int cfg_len)
{
    int r = 0;
    int try_times = 0;
    u8 buf = 0;
    u16 command_reg = chip_info->reg_info.GTP_REG_CMD;
    u16 cfg_reg = chip_info->reg_info.GTP_REG_CONFIG_DATA ;
    int count = 1;

RETRY:
    /*1. Inquire command_reg until it's free*/
    for (try_times = 0; try_times < TS_WAIT_CMD_FREE_RETRY_TIMES; try_times++) {
        if (gt9886_i2c_read(chip_info->client, command_reg, 1, &buf) >= 0 && buf == TS_CMD_REG_READY)
            break;
        usleep_range(10000, 11000);
    }
    if (try_times >= TS_WAIT_CMD_FREE_RETRY_TIMES) {
        TPD_INFO("Send large cfg FAILED, before send, reg:0x%04x is not 0xff", command_reg);
        if (count == 1) {
            count = 0;
            buf = 0x00;
            touch_i2c_write_block(chip_info->client, GTP_REG_COOR, 1, &buf);
            goto RETRY;
        }
        r = -EINVAL;
        goto exit;
    }
    /*2. send "start write cfg" command*/
    if (goodix_send_cmd(chip_info, COMMAND_START_SEND_LARGE_CFG, 0)) {
        TPD_INFO("Send large cfg FAILED, send COMMAND_START_SEND_LARGE_CFG ERROR");
        r = -EINVAL;
        goto exit;
    }

    /*3. wait ic set command_reg to 0x82*/
    if (goodix_wait_cfg_cmd_ready(chip_info, COMMAND_SEND_CFG_PREPARE_OK, COMMAND_START_SEND_LARGE_CFG)) {
        TPD_INFO("Send large cfg FAILED, reg:0x%04x is not 0x82", command_reg);
        r = -EINVAL;
        goto exit;
    }

    /*4. write cfg*/
    if (gt9886_i2c_write(chip_info->client, cfg_reg, cfg_len, config) < 0) {
        TPD_INFO("Send large cfg FAILED, write cfg to fw ERROR");
        r = -EINVAL;
        goto exit;
    }

    /*5. send "end send cfg" command*/
    if (goodix_send_cmd(chip_info, COMMAND_END_SEND_CFG, 0)) {
        TPD_INFO("Send large cfg FAILED, send COMMAND_END_SEND_CFG ERROR");
        r = -EINVAL;
        goto exit;
    }

    /*6. wait ic set command_reg to 0xff*/
    for (try_times = 0; try_times < TS_WAIT_CMD_FREE_RETRY_TIMES; try_times++) {
        if (gt9886_i2c_read(chip_info->client, command_reg, 1, &buf) >= 0 && buf == TS_CMD_REG_READY)
            break;
        usleep_range(10000, 11000);
    }
    if (try_times >= TS_WAIT_CMD_FREE_RETRY_TIMES) {
        TPD_INFO("Send large cfg FAILED, after send, reg:0x%04x is not 0xff", command_reg);
        r = -EINVAL;
        goto exit;
    }
    msleep(100);
    TPD_INFO("Send large cfg SUCCESS");
    r = 0;

exit:
    return r;
}

static s32 goodix_send_config(struct chip_data_gt9886 *chip_info, u8 *config, int cfg_len)
{
    int ret = 0;
    static DEFINE_MUTEX(config_lock);

    if (!config) {
        TPD_INFO("Null config data");
        return -EINVAL;
    }

    /*check configuration valid*/
    ret = goodix_check_cfg_valid(config, cfg_len);
    if (ret != 0) {
        TPD_INFO("cfg check FAILED");
        return -EINVAL;
    }

    TPD_INFO("ver:%02xh,size:%d", config[0], cfg_len);

    mutex_lock(&config_lock);

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);

    ret = goodix_send_large_config(chip_info, config, cfg_len);

    /*enable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, true);

    if (ret != 0)
        TPD_INFO("send_cfg FAILED, cfg_len:%d", cfg_len);

    mutex_unlock(&config_lock);

    return ret;
}

static s32 goodix_request_event_handler(struct chip_data_gt9886 *chip_info)
{
    s32 ret = -1;
    u8 rqst_data = 0;

    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_RQST, 1, &rqst_data);
    if (ret < 0) {
        TPD_INFO("%s: i2c transfer error.\n", __func__);
        return -1;
    }

    TPD_INFO("%s: request state:0x%02x.\n", __func__, rqst_data);

    switch (rqst_data) {
    case GTP_RQST_CONFIG:
        TPD_INFO("HW request config.\n");
        ret = goodix_send_config(chip_info, chip_info->normal_cfg.data, chip_info->normal_cfg.length);
        if (ret) {
            TPD_INFO("request config, send config faild.\n");
        }
        break;

    case GTP_RQST_RESET:
        TPD_INFO("%s: HW requset reset.\n", __func__);
        goodix_reset(chip_info);
        break;

    case GTP_RQST_BASELINE:
        TPD_INFO("%s: HW request baseline.\n", __func__);
        break;

    case GTP_RQST_FRE:
        TPD_INFO("%s: HW request frequence.\n", __func__);
        break;

    case GTP_RQST_IDLE:
        TPD_INFO("%s: HW request idle.\n", __func__);
        break;

    default:
        TPD_INFO("%s: Unknown hw request:%d.\n", __func__, rqst_data);
        break;
    }

    rqst_data = GTP_RQST_RESPONDED;
    ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.GTP_REG_RQST, 1, &rqst_data);
    if (ret) {
        TPD_INFO("%s: failed to write rqst_data:%d.\n", __func__, rqst_data);
    }

    return 0;
}

/*#define getU32(a) ((u32)getUint((u8 *)(a), 4))
#define getU16(a) ((u16)getUint((u8 *)(a), 2))
static u32 getUint(u8 * buffer, int len)
{
    u32 num = 0;
    int i;
    for (i = 0; i < len; i++) {
        num <<= 8;
        num += buffer[i];
    }

    return num;
}*/

void goodix_reset_via_gpio(struct chip_data_gt9886 *chip_info)
{
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
        udelay(2000);
        gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
        msleep(10);
        chip_info->halt_status = false; //reset this flag when ic reset
    } else {
        TPD_INFO("reset gpio is invalid\n");
    }

    msleep(20);
}

static int goodix_reset(void *chip_data)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    TPD_INFO("%s.\n", __func__);

    goodix_reset_via_gpio(chip_info);

    ret = goodix_reset_esd_status(chip_info);
    if (ret < 0) {
        TPD_INFO("goodix reset esd status failed\n");
        return ret;
    }

    return ret;
}

int goodix_get_channel_num(struct i2c_client *client, u32 *sen_num, u32 *drv_num)
{
    int ret = -1;
    u8 buf[2] = {0};

    ret = gt9886_i2c_read(client, SENSOR_NUM_ADDR, 1, buf);
    if (ret < 0) {
        TPD_INFO("Read sen_num fail.");
        return ret;
    }

    *sen_num = buf[0];

    ret = gt9886_i2c_read(client, DRIVER_GROUP_A_NUM_ADDR, 2, buf);
    if (ret < 0) {
        TPD_INFO("Read drv_num fail.");
        return ret;
    }

    *drv_num = buf[0] + buf[1];
    TPD_INFO("sen_num : %d, drv_num : %d", *sen_num, *drv_num);

    return 0;
}

/*********** End of function that work for oplus_touchpanel_operations callbacks***************/


/********* Start of implementation of oplus_touchpanel_operations callbacks********************/
static int goodix_ftm_process(void *chip_data)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    TPD_INFO("%s is called!\n", __func__);
    tp_powercontrol_2v8(chip_info->hw_res, false);
    tp_powercontrol_1v8(chip_info->hw_res, false);
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        gpio_direction_output(chip_info->hw_res->reset_gpio, false);
    }

    return 0;
}

static int goodix_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    char manu_temp[MAX_DEVICE_MANU_LENGTH] = "HD_";
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    len = strlen(panel_data->fw_name);
    chip_info->tp_type = panel_data->tp_type;
    chip_info->p_tp_fw = panel_data->manufacture_info.version;
    strlcat(manu_temp, panel_data->manufacture_info.manufacture, MAX_DEVICE_MANU_LENGTH);
    strncpy(panel_data->manufacture_info.manufacture, manu_temp, MAX_DEVICE_MANU_LENGTH);
    TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n", chip_info->tp_type, panel_data->fw_name);

    return 0;
}

static int goodix_get_chip_info(void *chip_data)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    struct goodix_register   *reg_info = &chip_info->reg_info;

    reg_info->GTP_REG_FW_CHK_MAINSYS        = 0x21E4;    //if chip run in patch
    //reg_info->GTP_REG_FW_CHK_SUBSYS       = 0x5095;
    reg_info->GTP_REG_CONFIG_DATA           = 0x6F78;    //cfg_addr
    reg_info->GTP_REG_READ_COOR             = 0x4100;    //read_coor
    reg_info->GTP_REG_WAKEUP_GESTURE        = 0x4100;    //read_gesture_info
    reg_info->GTP_REG_GESTURE_COOR          = 0x4128;    //read_gesture_coor
    reg_info->GTP_REG_CMD                   = 0x6F68;    //command
    reg_info->GTP_REG_RQST                  = 0x6F6D;    //request by FW
    //reg_info->GTP_REG_NOISE_DETECT        = 0x804B;
    reg_info->GTP_REG_PRODUCT_VER           = 0x452C;    //product version reg
    reg_info->GTP_REG_ESD_WRITE             = 0x30F3;    //ESD write reg
    reg_info->GTP_REG_ESD_READ              = 0x3103;    //ESD read reg
    reg_info->GTP_REG_DEBUG                 = 0x608B;    //debug log enable reg
    reg_info->GTP_REG_DOWN_DIFFDATA         = 0xAFB0;    //down diff log
    reg_info->GTP_REG_EDGE_INFO             = 0x4154;    //read edge points' info

    reg_info->GTP_REG_RAWDATA               = 0x8FA0;    //rawdata_reg
    reg_info->GTP_REG_DIFFDATA              = 0x9D20;    //diffdata_reg
    reg_info->GTP_REG_BASEDATA              = 0xA980;    //basedata_reg
    reg_info->GTP_REG_DETAILED_DEBUG_INFO   = 0x3096;    //basedata_reg

    //return goodix_set_reset_status(chip_info);
    return 0;
}

static int goodix_power_control(void *chip_data, bool enable)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    if (true == enable) {
        ret = tp_powercontrol_2v8(chip_info->hw_res, true);
        if (ret)
            return -1;
        ret = tp_powercontrol_1v8(chip_info->hw_res, true);
        if (ret)
            return -1;

        msleep(20);
        goodix_reset_via_gpio(chip_info);
    } else {
        ret = tp_powercontrol_1v8(chip_info->hw_res, false);
        if (ret)
            return -1;
        ret = tp_powercontrol_2v8(chip_info->hw_res, false);
        if (ret)
            return -1;
        if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
            gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
        }
    }

    return ret;
}

static fw_check_state goodix_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    int retry = 2;
    int ret = 0;
    u8 reg_val[2] = {0};
    u32 fw_ver = 0;
    u8 cfg_ver = 0;
    char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    // TODO need confirm fw state check method

    do {
        goodix_i2c_read_dbl_check(chip_info->client, chip_info->reg_info.GTP_REG_FW_CHK_MAINSYS, reg_val, 1);

        if (reg_val[0] != 0xFC) {
            TPD_INFO("Check fw status failed: reg[0x21e4] = 0x%2X!\n", reg_val[0]);
        } else {
            break;
        }
    } while(--retry);

    if (!retry)
        return FW_ABNORMAL;

    fw_ver = goodix_read_version(chip_info, panel_data);
    if (ret < 0) {
        TPD_INFO("%s: goodix read version failed\n", __func__);
        return FW_ABNORMAL;
    }

    cfg_ver = goodix_config_version_read(chip_info);

    if (panel_data->manufacture_info.version) {
        panel_data->TP_FW = cfg_ver | (fw_ver & 0xFF) << 8;
        sprintf(dev_version, "%04x", panel_data->TP_FW);
        strlcpy(&(panel_data->manufacture_info.version[7]), dev_version, 5);
    }

    TPD_INFO("%s: panel_data->TP_FW = 0x%x \n", __func__, panel_data->TP_FW);
    return FW_NORMAL;
}

/*****************start of GT9886's update function********************/

/**
 * goodix_parse_firmware - parse firmware header infomation
 *    and subsystem infomation from firmware data buffer
 *
 * @fw_data: firmware struct, contains firmware header info
 *    and firmware data.
 * return: 0 - OK, < 0 - error
 */
static int goodix_parse_firmware(struct firmware_data *fw_data)
{
    const struct firmware *firmware;
    struct firmware_info *fw_info;
    unsigned int i, fw_offset, info_offset;
    u16 checksum;
    int r = 0;

    if (!fw_data || !fw_data->firmware) {
        TPD_INFO("Invalid firmware data");
        return -EINVAL;
    }
    fw_info = &fw_data->fw_info;

    /* copy firmware head info */
    firmware = fw_data->firmware;
    if (firmware->size < FW_SUBSYS_INFO_OFFSET) {
        TPD_INFO("Invalid firmware size:%zu", firmware->size);
        r = -EINVAL;
        goto err_size;
    }
    memcpy(fw_info, firmware->data, FW_SUBSYS_INFO_OFFSET);

    /* check firmware size */
    fw_info->size = be32_to_cpu(fw_info->size);            // Notice here
    if (firmware->size != fw_info->size + 6) {
        TPD_INFO("Bad firmware, size not match");
        r = -EINVAL;
        goto err_size;
    }

    /* calculate checksum, note: sum of bytes, but check
     * by u16 checksum */
    for (i = 6, checksum = 0; i < firmware->size; i++)
        checksum += firmware->data[i];

    /* byte order change, and check */
    fw_info->checksum = be16_to_cpu(fw_info->checksum);
    if (checksum != fw_info->checksum) {
        TPD_INFO("Bad firmware, cheksum error");
        r = -EINVAL;
        goto err_size;
    }

    if (fw_info->subsys_num > FW_SUBSYS_MAX_NUM) {
        TPD_INFO("Bad firmware, invalid subsys num: %d", fw_info->subsys_num);
        r = -EINVAL;
        goto err_size;
    }

    /* parse subsystem info */
    fw_offset = FW_HEADER_SIZE;
    for (i = 0; i < fw_info->subsys_num; i++) {
        info_offset = FW_SUBSYS_INFO_OFFSET + i * FW_SUBSYS_INFO_SIZE;
        fw_info->subsys[i].type = firmware->data[info_offset];
        fw_info->subsys[i].size = be32_to_cpup((__be32 *)&firmware->data[info_offset + 1]);
        fw_info->subsys[i].flash_addr = be16_to_cpup((__be16 *)&firmware->data[info_offset + 5]);

        if (fw_offset > firmware->size) {
            TPD_INFO("Sybsys offset exceed Firmware size");
            goto err_size;
        }

        fw_info->subsys[i].data = firmware->data + fw_offset;
        fw_offset += fw_info->subsys[i].size;
    }

    TPD_INFO("Firmware package protocol: V%u", fw_info->protocol_ver);
    TPD_INFO("Fimware PID:GT%s", fw_info->fw_pid);
    TPD_INFO("Fimware VID:%02X%02X%02X%02X", fw_info->fw_vid[0], fw_info->fw_vid[1], fw_info->fw_vid[2], fw_info->fw_vid[3]);
    TPD_INFO("Firmware chip type:%02X", fw_info->chip_type);
    TPD_INFO("Firmware size:%u", fw_info->size);
    TPD_INFO("Firmware subsystem num:%u", fw_info->subsys_num);
    for (i = 0; i < fw_info->subsys_num; i++) {
        TPD_DEBUG("------------------------------------------");
        TPD_DEBUG("Index:%d", i);
        TPD_DEBUG("Subsystem type:%02X", fw_info->subsys[i].type);
        TPD_DEBUG("Subsystem size:%u", fw_info->subsys[i].size);
        TPD_DEBUG("Subsystem flash_addr:%08X", fw_info->subsys[i].flash_addr);
        TPD_DEBUG("Subsystem Ptr:%p", fw_info->subsys[i].data);
    }
    TPD_DEBUG("------------------------------------------");

err_size:
    return r;
}


/**
 * goodix_check_update - compare the version of firmware running in
 *  touch device with the version getting from the firmware file.
 * @fw_info: firmware infomation to be compared
 * return: 0 firmware in the touch device needs to be updated
 *            < 0 no need to update firmware
 */
static int goodix_check_update(struct i2c_client *client,
                               const struct firmware_info *fw_info)
{
    int r = 0;
    int res = 0;
    u8 buffer[4] = {0};

#define PID_REG     0x4535
#define VID_REG     0x453D
#define PID_LEN     4
#define VID_LEN     4

    /*disable doze mode ,just valid for nomandy
    * this func must be used in pairs*/
    r = goodix_set_i2c_doze_mode(client, false);
    if (r == -EINVAL)
        goto need_update;

    /*read version from chip , if we got invalid
    * firmware version ,maybe fimware in flash is
    * incorrect,so we need to update firmware*/

    /*read and compare PID*/
    r = gt9886_i2c_read(client, PID_REG, PID_LEN, buffer);
    if (r < 0) {
        TPD_INFO("Read pid failed");
        goto exit;
    }
    TPD_INFO("PID:%02x %02x %02x %02x", buffer[0], buffer[1],
             buffer[2], buffer[3]);

    if (memcmp(buffer, fw_info->fw_pid, PID_LEN)) {
        TPD_INFO("Product is not match,may header's fw is other product's");
        goto need_update;
    }

    /*read and compare VID*/
    r = gt9886_i2c_read(client, VID_REG, VID_LEN, buffer);
    if (r < 0) {
        TPD_INFO("Read vid failed");
        goto exit;
    }
    TPD_INFO("VID:%02x %02x %02x %02x", buffer[0], buffer[1],
             buffer[2], buffer[3]);

    res = memcmp(buffer, fw_info->fw_vid, VID_LEN);
    if (0 == res) {
        TPD_INFO("FW version is equal tp IC's,skip update");
        r = -EPERM;
        goto exit;
    } else if (res > 0) {
        TPD_INFO("Warning: fw version is lower than IC's");
    }

need_update:
    TPD_INFO("Firmware needs tp be updated");
    r = 0;
exit:
    /*enable doze mode, just valid for normandy
    *this func must be used in pairs*/
    goodix_set_i2c_doze_mode(client, true);
    return r;
}

/**
 * goodix_reg_write_confirm - write register and confirm the value
 *  in the register.
 * @client: pointer to touch device client
 * @addr: register address
 * @data: pointer to data buffer
 * @len: data length
 * return: 0 write success and confirm ok
 *           < 0 failed
 */
static int goodix_reg_write_confirm(struct i2c_client *client,
                                    unsigned int addr, unsigned char *data, unsigned int len)
{
    u8 *cfm, cfm_buf[32];
    int r, i;

    if (len > sizeof(cfm_buf)) {
        cfm = kzalloc(len, GFP_KERNEL);
        if (!cfm) {
            TPD_INFO("Mem alloc failed");
            return -ENOMEM;
        }
    } else {
        cfm = &cfm_buf[0];
    }

    for (i = 0; i < GOODIX_BUS_RETRY_TIMES; i++) {
        r = touch_i2c_write_block(client, addr, len, data);
        if (r < 0)
            goto exit;
        r = touch_i2c_read_block(client, addr, len, cfm);
        if (r < 0)
            goto exit;

        if (memcmp(data, cfm, len)) {
            r = -EMEMCMP;
            continue;
        } else {
            r = 0;
            break;
        }
    }

exit:
    if (cfm != &cfm_buf[0])
        kfree(cfm);
    return r;
}


/**
 * goodix_load_isp - load ISP program to deivce ram
 * @client: pointer to touch device client
 * @fw_data: firmware data
 * return 0 ok, <0 error
 */
static int goodix_load_isp(struct i2c_client *client, struct firmware_data *fw_data)
{
    struct fw_subsys_info *fw_isp;
    u8 reg_val[8] = {0x00};
    int r;
    int i;

    fw_isp = &fw_data->fw_info.subsys[0];

    TPD_INFO("Loading ISP start");
    /* select bank0 */
    reg_val[0] = 0x00;
    r = touch_i2c_write_block(client, HW_REG_BANK_SELECT, 1, reg_val);
    if (r < 0) {
        TPD_INFO("Failed to select bank0");
        return r;
    }
    TPD_DEBUG("Success select bank0, Set 0x%x -->0x00", HW_REG_BANK_SELECT);

    /* enable bank0 access */
    reg_val[0] = 0x01;
    r = touch_i2c_write_block(client, HW_REG_ACCESS_PATCH0, 1, reg_val);
    if (r < 0) {
        TPD_INFO("Failed to enable patch0 access");
        return r;
    }
    TPD_DEBUG("Success select bank0, Set 0x%x -->0x01", HW_REG_ACCESS_PATCH0);

    r = goodix_reg_write_confirm(client, HW_REG_ISP_ADDR, (u8 *)fw_isp->data, fw_isp->size);
    if (r < 0) {
        TPD_INFO("Loading ISP error");
        return r;
    }

    TPD_DEBUG("Success send ISP data to IC");

    /* forbid patch access */
    reg_val[0] = 0x00;
    r = goodix_reg_write_confirm(client, HW_REG_ACCESS_PATCH0, reg_val, 1);
    if (r < 0) {
        TPD_INFO("Failed to disable patch0 access");
        return r;
    }
    TPD_DEBUG("Success forbit bank0 accedd, set 0x%x -->0x00", HW_REG_ACCESS_PATCH0);

    /*clear 0x6006*/
    reg_val[0] = 0x00;
    reg_val[1] = 0x00;
    r = touch_i2c_write_block(client, HW_REG_ISP_RUN_FLAG, 2, reg_val);
    if (r < 0) {
        TPD_INFO("Failed to clear 0x%x", HW_REG_ISP_RUN_FLAG);
        return r;
    }
    TPD_DEBUG("Success clear 0x%x", HW_REG_ISP_RUN_FLAG);

    /* TODO: change address 0xBDE6 set backdoor flag HW_REG_CPU_RUN_FROM */
    memset(reg_val, 0x55, 8);
    r = touch_i2c_write_block(client, HW_REG_CPU_RUN_FROM, 8, reg_val);
    if (r < 0) {
        TPD_INFO("Failed set backdoor flag");
        return r;
    }
    TPD_DEBUG("Success write [8]0x55 to 0x%x", HW_REG_CPU_RUN_FROM);

    /* TODO: change reg_val 0x08---> 0x00 release ss51 */
    reg_val[0] = 0x00;
    r = touch_i2c_write_block(client, HW_REG_CPU_CTRL, 1, reg_val);
    if (r < 0) {
        TPD_INFO("Failed to run isp");
        return r;
    }
    TPD_DEBUG("Success run isp, set 0x%x-->0x00", HW_REG_CPU_CTRL);

    /* check isp work state */
    for (i = 0; i < TS_CHECK_ISP_STATE_RETRY_TIMES; i++) {
        r = touch_i2c_read_block(client, HW_REG_ISP_RUN_FLAG, 2, reg_val);
        if (r < 0 || (reg_val[0] == 0xAA && reg_val[1] == 0xBB))
            break;
        usleep_range(5000, 5100);
    }
    if (reg_val[0] == 0xAA && reg_val[1] == 0xBB) {
        TPD_INFO("ISP working OK");
        return 0;
    } else {
        TPD_INFO("ISP not work,0x%x=0x%x, 0x%x=0x%x",
                 HW_REG_ISP_RUN_FLAG, reg_val[0],
                 HW_REG_ISP_RUN_FLAG + 1, reg_val[1]);
        return -EFAULT;
    }
}

static int goodix_update_prepare(void *chip_data, struct fw_update_ctrl *fwu_ctrl)
{
    u8 reg_val[4] = { 0x00 };
    u8 temp_buf[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    int retry = 20;
    int r;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    gt9886_i2c_write(chip_info->client, HW_REG_CPU_RUN_FROM, 8, temp_buf);

    /*reset IC*/
    TPD_INFO("normandy firmware update, reset");
    gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
    udelay(2000);
    gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
    usleep_range(10000, 11000);

    retry = 20;
    do {
        reg_val[0] = 0x24;
        r = goodix_reg_write_confirm(chip_info->client, HW_REG_CPU_CTRL, reg_val, 1);
        if (r < 0) {
            TPD_INFO("Failed to hold ss51, retry");
            msleep(20);
        } else {
            break;
        }
    } while (--retry);
    if (!retry) {
        TPD_INFO("Failed hold ss51,return =%d", r);
        return -EINVAL;
    }
    TPD_DEBUG("Success hold ss51");

    /* enable DSP & MCU power */
    reg_val[0] = 0x00;
    r = goodix_reg_write_confirm(chip_info->client, HW_REG_DSP_MCU_POWER, reg_val, 1);
    if (r < 0) {
        TPD_INFO("Failed enable DSP&MCU power");
        return r;
    }
    TPD_DEBUG("Success enabled DSP&MCU power,set 0x%x-->0x00", HW_REG_DSP_MCU_POWER);

    /* disable watchdog timer */
    reg_val[0] = 0x00;
    r = touch_i2c_write_block(chip_info->client, HW_REG_CACHE, 1, reg_val);
    if (r < 0) {
        TPD_INFO("Failed to clear cache");
        return r;
    }
    TPD_DEBUG("Success clear cache");

    reg_val[0] = 0x95;
    r = touch_i2c_write_block(chip_info->client, HW_REG_ESD_KEY, 1, reg_val);
    reg_val[0] = 0x00;
    r |= touch_i2c_write_block(chip_info->client, HW_REG_WTD_TIMER, 1, reg_val);

    reg_val[0] = 0x27;
    r |= touch_i2c_write_block(chip_info->client, HW_REG_ESD_KEY, 1, reg_val);
    if (r < 0) {
        TPD_INFO("Failed to disable watchdog");
        return r;
    }
    TPD_DEBUG("Success disable watchdog");

    /* set scramble */
    reg_val[0] = 0x00;
    r = touch_i2c_write_block(chip_info->client, HW_REG_SCRAMBLE, 1, reg_val);
    if (r < 0) {
        TPD_INFO("Failed to set scramble");
        return r;
    }
    TPD_DEBUG("Succcess set scramble");

    /* load ISP code and run form isp */
    r = goodix_load_isp(chip_info->client, &fwu_ctrl->fw_data);
    if (r < 0)
        TPD_INFO("Failed lode and run isp");

    return r;
}

static inline u16 checksum_be16(u8 *data, u32 size)
{
    u16 checksum = 0;
    u32 i;

    for (i = 0; i < size; i += 2)
        checksum += be16_to_cpup((__be16 *)(data + i));
    return checksum;
}


/**
 * goodix_format_fw_packet - formate one flash packet
 * @pkt: target firmware packet
 * @flash_addr: flash address
 * @size: packet size
 * @data: packet data
 */
static int goodix_format_fw_packet(u8 *pkt, u32 flash_addr,
                                   u16 len, const u8 *data)
{
    u16 checksum;
    if (!pkt || !data)
        return -EINVAL;

    /*
     * checksum rule:sum of data in one format is equal to zero
     * data format: byte/le16/be16/le32/be32/le64/be64
     */
    pkt[0] = (len >> 8) & 0xff;
    pkt[1] = len & 0xff;
    pkt[2] = (flash_addr >> 16) & 0xff; /* u16 >> 16bit seems nosense but really important */
    pkt[3] = (flash_addr >> 8) & 0xff;
    memcpy(&pkt[4], data, len);
    checksum = checksum_be16(pkt, len + 4);
    checksum = 0 - checksum;
    pkt[len + 4] = (checksum >> 8) & 0xff;
    pkt[len + 5] = checksum & 0xff;
    return 0;
}

/**
 * goodix_send_fw_packet - send one firmware packet to ISP
 * @dev: target touch device
 * @pkt: firmware packet
 * return 0 ok, <0 error
 */
static int goodix_send_fw_packet(struct i2c_client *client, u8 type,
                                 u8 *pkt, u32 len)
{
    u8 reg_val[4];
    int r, i;
    u32 offset = 0;
    u32 total_size = 0;
    u32 data_size = 0;

    if (!pkt)
        return -EINVAL;

    /*i2c max size is just 4k for mt6771*/
    total_size = len;
    while (total_size > 0) {
        data_size = total_size > I2C_DATA_MAX_BUFFERSIZE ? I2C_DATA_MAX_BUFFERSIZE : total_size;
        TPD_INFO("I2C firmware to %08x,size:%u bytes all len:%u bytes", offset, data_size, len);
        r = goodix_reg_write_confirm(client, HW_REG_ISP_BUFFER + offset, &pkt[offset], data_size);
        if (r < 0) {
            TPD_INFO("Failed to write firmware packet");
            return r;
        }

        offset += data_size;
        total_size -= data_size;
    } /* end while */

    reg_val[0] = 0;
    reg_val[1] = 0;
    /* clear flash flag 0X6022 */
    r = goodix_reg_write_confirm(client, HW_REG_FLASH_FLAG, reg_val, 2);
    if (r < 0) {
        TPD_INFO("Faile to clear flash flag");
        return r;
    }

    /* write subsystem type 0X8020*/
    reg_val[0] = type;
    reg_val[1] = type;
    r = goodix_reg_write_confirm(client, HW_REG_SUBSYS_TYPE, reg_val, 2);
    if (r < 0) {
        TPD_INFO("Failed write subsystem type to IC");
        return r;
    }

    for (i = 0; i < TS_READ_FLASH_STATE_RETRY_TIMES; i++) {
        r = touch_i2c_read_block(client, HW_REG_FLASH_FLAG, 2, reg_val);
        if (r < 0) {
            TPD_INFO("Failed read flash state");
            return r;
        }

        /* flash haven't end */
        if (reg_val[0] == ISP_STAT_WRITING && reg_val[1] == ISP_STAT_WRITING) {
            TPD_DEBUG("Flash not ending...");
            usleep_range(55000, 56000);
            continue;
        }
        if (reg_val[0] == ISP_FLASH_SUCCESS && reg_val[1] == ISP_FLASH_SUCCESS) {
            /* read twice to confirm the result */
            r = touch_i2c_read_block(client, HW_REG_FLASH_FLAG, 2, reg_val);
            if (r >= 0 && reg_val[0] == ISP_FLASH_SUCCESS && reg_val[1] == ISP_FLASH_SUCCESS) {
                TPD_INFO("Flash subsystem ok");
                return 0;
            }
        }
        if (reg_val[0] == ISP_FLASH_ERROR && reg_val[1] == ISP_FLASH_ERROR) {
            TPD_INFO(" Flash subsystem failed");
            return -EAGAIN;
        }
        if (reg_val[0] == ISP_FLASH_CHECK_ERROR) {
            TPD_INFO("Subsystem checksum err");
            return -EAGAIN;
        }

        usleep_range(250, 260);
    }

    TPD_INFO("Wait for flash end timeout, 0x6022= %x %x", reg_val[0], reg_val[1]);
    return -EAGAIN;
}

/**
 * goodix_flash_subsystem - flash subsystem firmware,
 *  Main flow of flashing firmware.
 *    Each firmware subsystem is divided into several
 *    packets, the max size of packet is limited to
 *    @{ISP_MAX_BUFFERSIZE}
 * @client: pointer to touch device client
 * @subsys: subsystem infomation
 * return: 0 ok, < 0 error
 */
static int goodix_flash_subsystem(struct i2c_client *client, struct fw_subsys_info *subsys)
{
    u16 data_size, offset;
    u32 total_size;
    u32 subsys_base_addr = subsys->flash_addr << 8;
    u8 *fw_packet;
    int r = 0, i;

    /*
     * if bus(i2c/spi) error occued, then exit, we will do
     * hardware reset and re-prepare ISP and then retry
     * flashing
     */
    total_size = subsys->size;
    fw_packet = kzalloc(ISP_MAX_BUFFERSIZE + 6, GFP_KERNEL);
    if (!fw_packet) {
        TPD_INFO("Failed alloc memory");
        return -EINVAL;
    }

    offset = 0;
    while (total_size > 0) {
        data_size = total_size > ISP_MAX_BUFFERSIZE ? ISP_MAX_BUFFERSIZE : total_size;
        TPD_INFO("Flash firmware to %08x,size:%u bytes", subsys_base_addr + offset, data_size);

        /* format one firmware packet */
        r = goodix_format_fw_packet(fw_packet, subsys_base_addr + offset, data_size, &subsys->data[offset]);
        if (r < 0) {
            TPD_INFO("Invalid packet params");
            goto exit;
        }

        /* send one firmware packet, retry 3 time if send failed */
        for (i = 0; i < 3; i++) {
            r = goodix_send_fw_packet(client, subsys->type, fw_packet, data_size + 6);
            if (!r)
                break;
        }
        if (r) {
            TPD_INFO("Failed flash subsystem");
            goto exit;
        }
        offset += data_size;
        total_size -= data_size;
    } /* end while */

exit:
    kfree(fw_packet);
    return r;
}



/**
 * goodix_flash_firmware - flash firmware
 * @client: pointer to touch device client
 * @fw_data: firmware data
 * return: 0 ok, < 0 error
 */
static int goodix_flash_firmware(struct i2c_client *client, struct firmware_data *fw_data)
{
    struct fw_update_ctrl *fw_ctrl;
    struct firmware_info  *fw_info;
    struct fw_subsys_info *fw_x;
    int retry = GOODIX_BUS_RETRY_TIMES;
    int i, r = 0, fw_num, prog_step;

    /* start from subsystem 1,
     * subsystem 0 is the ISP program */
    fw_ctrl = container_of(fw_data, struct fw_update_ctrl, fw_data);
    fw_info = &fw_data->fw_info;
    fw_num = fw_info->subsys_num;

    /* we have 80% work here */
    prog_step = 80 / (fw_num - 1);

    for (i = 1; i < fw_num && retry;) {
        TPD_INFO("--- Start to flash subsystem[%d] ---", i);
        fw_x = &fw_info->subsys[i];
        r = goodix_flash_subsystem(client, fw_x);
        if (r == 0) {
            TPD_INFO("--- End flash subsystem[%d]: OK ---", i);
            fw_ctrl->progress += prog_step;
            i++;
        } else if (r == -EAGAIN) {
            retry--;
            TPD_INFO("--- End flash subsystem%d: Fail, errno:%d, retry:%d ---", i, r, GOODIX_BUS_RETRY_TIMES - retry);
        } else if (r < 0) { /* bus error */
            TPD_INFO("--- End flash subsystem%d: Fatal error:%d exit ---", i, r);
            goto exit_flash;
        }
    }

exit_flash:
    return r;
}

/**
 * goodix_update_finish - update finished, free resource
 *  and reset flags---
 * @fwu_ctrl: pointer to fw_update_ctrl structrue
 * return: 0 ok, < 0 error
 */
static int goodix_update_finish(void *chip_data, struct fw_update_ctrl *fwu_ctrl)
{
    u8 reg_val[8] = {0};
    int r = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    /* hold ss51 */
    reg_val[0] = 0x24;
    r = touch_i2c_write_block(chip_info->client, HW_REG_CPU_CTRL, 1, reg_val);
    if (r < 0)
        TPD_INFO("Failed to hold ss51");

    /* clear back door flag */
    memset(reg_val, 0, sizeof(reg_val));
    r = touch_i2c_write_block(chip_info->client, HW_REG_CPU_RUN_FROM, 8, reg_val);
    if (r < 0) {
        TPD_INFO("Failed set CPU run from normal firmware");
        return r;
    }

    /* release ss51 */
    reg_val[0] = 0x00;
    r = touch_i2c_write_block(chip_info->client, HW_REG_CPU_CTRL, 1, reg_val);
    if (r < 0)
        TPD_INFO("Failed to run ss51");

    /*reset*/
    r = goodix_reset(chip_info);

    return r;
}

u32 getUint(u8 *buffer, int len)
{
    u32 num = 0;
    int i = 0;
    for (i = 0; i < len; i++) {
        num <<= 8;
        num += buffer[i];
    }
    return num;
}

static int gtx8_parse_cfg_data(const struct firmware *cfg_bin, char *cfg_type, u8 *cfg, int *cfg_len, u8 sid)
{
    int i = 0, config_status = 0, one_cfg_count = 0;
    int cfgPackageLen = 0;

    u8 bin_group_num = 0, bin_cfg_num = 0;
    u16 cfg_checksum = 0, checksum = 0;
    u8 sid_is_exist = GTX8_NOT_EXIST;
    u16 cfg_offset = 0;

    TPD_DEBUG("%s run,sensor id:%d\n", __func__, sid);

    cfgPackageLen = getU32(cfg_bin->data) + BIN_CFG_START_LOCAL;
    if ( cfgPackageLen > cfg_bin->size) {
        TPD_INFO("%s:Bad firmware!,cfg package len:%d,firmware size:%d\n",
                 __func__, cfgPackageLen, (int)cfg_bin->size);
        goto exit;
    }

    /* check firmware's checksum */
    cfg_checksum = getU16(&cfg_bin->data[4]);

    for (i = BIN_CFG_START_LOCAL; i < (cfgPackageLen) ; i++)
        checksum += cfg_bin->data[i];

    if ((checksum) != cfg_checksum) {
        TPD_INFO("%s:Bad firmware!(checksum: 0x%04X, header define: 0x%04X)\n",
                 __func__, checksum, cfg_checksum);
        goto exit;
    }
    /* check head end  */

    bin_group_num = cfg_bin->data[MODULE_NUM];
    bin_cfg_num = cfg_bin->data[CFG_NUM];
    TPD_DEBUG("%s:bin_group_num = %d, bin_cfg_num = %d\n", __func__, bin_group_num, bin_cfg_num);

    if (!strncmp(cfg_type, GTX8_TEST_CONFIG, strlen(GTX8_TEST_CONFIG)))
        config_status = 0;
    else if (!strncmp(cfg_type, GTX8_NORMAL_CONFIG, strlen(GTX8_NORMAL_CONFIG)))
        config_status = 1;
    else if (!strncmp(cfg_type, GTX8_NORMAL_NOISE_CONFIG, strlen(GTX8_NORMAL_NOISE_CONFIG)))
        config_status = 2;
    else if (!strncmp(cfg_type, GTX8_GLOVE_CONFIG, strlen(GTX8_GLOVE_CONFIG)))
        config_status = 3;
    else if (!strncmp(cfg_type, GTX8_GLOVE_NOISE_CONFIG, strlen(GTX8_GLOVE_NOISE_CONFIG)))
        config_status = 4;
    else if (!strncmp(cfg_type, GTX8_HOLSTER_CONFIG, strlen(GTX8_HOLSTER_CONFIG)))
        config_status = 5;
    else if (!strncmp(cfg_type, GTX8_HOLSTER_NOISE_CONFIG, strlen(GTX8_HOLSTER_NOISE_CONFIG)))
        config_status = 6;
    else if (!strncmp(cfg_type, GTX8_NOISE_TEST_CONFIG, strlen(GTX8_NOISE_TEST_CONFIG)))
        config_status = 7;
    else {
        TPD_INFO("%s: invalid config text field\n", __func__);
        goto exit;
    }

    cfg_offset = CFG_HEAD_BYTES + bin_group_num * bin_cfg_num * CFG_INFO_BLOCK_BYTES ;
    for (i = 0 ; i < bin_group_num * bin_cfg_num; i++) {
        /* find cfg's sid in cfg.bin */
        one_cfg_count = getU16(&cfg_bin->data[CFG_HEAD_BYTES + 2 + i * CFG_INFO_BLOCK_BYTES]);
        if (sid == (cfg_bin->data[CFG_HEAD_BYTES + i * CFG_INFO_BLOCK_BYTES])) {
            sid_is_exist = GTX8_EXIST;
            if (config_status == (cfg_bin->data[CFG_HEAD_BYTES + 1 + i * CFG_INFO_BLOCK_BYTES])) {
                memcpy(cfg, &cfg_bin->data[cfg_offset], one_cfg_count);
                *cfg_len = one_cfg_count;
                TPD_DEBUG("%s:one_cfg_count = %d, cfg_data1 = 0x%02x, cfg_data2 = 0x%02x\n",
                          __func__, one_cfg_count, cfg[0], cfg[1]);
                break;
            }
        }
        cfg_offset += one_cfg_count;
    }

    if (i >= bin_group_num * bin_cfg_num) {
        TPD_INFO("%s:(not find config ,config_status: %d)\n", __func__, config_status);
        goto exit;
    }

    TPD_DEBUG("%s exit\n", __func__);
    return NO_ERR;
exit:
    return RESULT_ERR;
}

static int gtx8_get_cfg_data(void *chip_data_info, const struct firmware *cfg_bin,
                             char *config_name, struct gtx8_ts_config *config)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data_info;
    u8 *cfg_data = NULL;
    int cfg_len = 0;
    int ret = NO_ERR;

    TPD_DEBUG("%s run\n", __func__);

    cfg_data = kzalloc(GOODIX_CFG_MAX_SIZE, GFP_KERNEL);
    if (cfg_data == NULL) {
        TPD_INFO("Memory allco err\n");
        goto exit;
    }

    config->initialized = false;
    mutex_init(&config->lock);
    config->reg_base = 0x00;

    /* parse config data */
    ret = gtx8_parse_cfg_data(cfg_bin, config_name, cfg_data,
                              &cfg_len, chip_info->ver_info.sensor_id);
    if (ret < 0) {
        TPD_INFO("%s: parse %s data failed\n", __func__, config_name);
        ret = -EINVAL;
        goto exit;
    }

    TPD_DEBUG("%s: %s  version:%d , size:%d\n", __func__, config_name, cfg_data[0], cfg_len);
    memcpy(config->data, cfg_data, cfg_len);
    config->length = cfg_len;

    strncpy(config->name, config_name, MAX_STR_LEN);
    config->initialized = true;

exit:
    if (cfg_data) {
        kfree(cfg_data);
        cfg_data = NULL;
    }
    TPD_DEBUG("%s exit\n", __func__);
    return ret;
}


static int gtx8_get_cfg_parms(void *chip_data_info, const struct firmware *firmware)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data_info;
    int ret = 0;

    TPD_DEBUG("%s run\n", __func__);
    if(firmware == NULL) {
        TPD_INFO("%s: firmware is null\n", __func__);
        ret = -1;
        goto exit;
    }

    if (firmware->data == NULL) {
        TPD_INFO("%s:Bad firmware!(config firmware data is null: )\n", __func__);
        ret = -1;
        goto exit;
    }

    TPD_INFO("%s: cfg_bin_size:%d\n", __func__, (int)firmware->size);
    if (firmware->size > 56) {
        TPD_INFO("cfg_bin head info:%*ph\n", 32, firmware->data);
        TPD_INFO("cfg_bin head info:%*ph\n", 24, firmware->data + 32);
    }

    /* parse normal config data */
    ret = gtx8_get_cfg_data(chip_info, firmware, GTX8_NORMAL_CONFIG, &chip_info->normal_cfg);
    if (ret < 0) {
        TPD_INFO("%s: Failed to parse normal_config data:%d\n", __func__, ret);
    } else {
        TPD_DEBUG("%s: parse normal_config data success\n", __func__);
    }

    ret = gtx8_get_cfg_data(chip_info, firmware, GTX8_TEST_CONFIG, &chip_info->test_cfg);
    if (ret < 0) {
        TPD_INFO("%s: Failed to parse test_config data:%d\n", __func__, ret);
    } else {
        TPD_DEBUG("%s: parse test_config data success\n", __func__);
    }

    /* parse normal noise config data */
    ret = gtx8_get_cfg_data(chip_info, firmware, GTX8_NORMAL_NOISE_CONFIG, &chip_info->normal_noise_cfg);
    if (ret < 0) {
        TPD_INFO("%s: Failed to parse normal_noise_config data\n", __func__);
    } else {
        TPD_DEBUG("%s: parse normal_noise_config data success\n", __func__);
    }

    /* parse noise test config data */
    ret = gtx8_get_cfg_data(chip_info, firmware, GTX8_NOISE_TEST_CONFIG, &chip_info->noise_test_cfg);
    if (ret < 0) {
        memcpy(&chip_info->noise_test_cfg, &chip_info->normal_cfg, sizeof(chip_info->noise_test_cfg));
        TPD_INFO("%s: Failed to parse noise_test_config data,use normal_config data\n", __func__);
    } else {
        TPD_DEBUG("%s: parse noise_test_config data success\n", __func__);
    }
exit:
    TPD_DEBUG("%s exit:%d\n", __func__, ret);
    return ret;
}

//get fw firmware from firmware
//return value:
//      0: operate success
//      other: failed
static int gtx8_get_fw_parms(void *chip_data_info, const struct firmware *firmware, struct firmware *fw_firmware)
{
    //struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data_info;
    int ret = 0;
    int cfgPackageLen = 0;
    int fwPackageLen = 0;

    TPD_DEBUG("%s run\n", __func__);
    if(firmware == NULL) {
        TPD_INFO("%s: firmware is null\n", __func__);
        ret = -1;
        goto exit;
    }

    if (firmware->data == NULL) {
        TPD_INFO("%s:Bad firmware!(config firmware data si null)\n", __func__);
        ret = -1;
        goto exit;
    }

    if(fw_firmware == NULL) {
        TPD_INFO("%s:fw_firmware is null\n", __func__);
        ret = -1;
        goto exit;
    }

    TPD_DEBUG("clear fw_firmware\n");
    memset(fw_firmware, 0, sizeof(struct firmware));

    cfgPackageLen = getU32(firmware->data) + BIN_CFG_START_LOCAL;
    TPD_DEBUG("%s cfg package len:%d\n", __func__, cfgPackageLen);

    if(firmware->size <= (cfgPackageLen + 16)) {
        TPD_INFO("%s current firmware does not contain goodix fw\n", __func__);
        TPD_INFO("%s cfg package len:%d,firmware size:%d\n", __func__, cfgPackageLen, (int)firmware->size);
        ret = -1;
        goto exit;
    }

    if(!(firmware->data[cfgPackageLen + 0] == 'G' && firmware->data[cfgPackageLen + 1] == 'X' &&
         firmware->data[cfgPackageLen + 2] == 'F' && firmware->data[cfgPackageLen + 3] == 'W')) {
        TPD_INFO("%s can't find fw package\n", __func__);
        TPD_INFO("Data type:%c %c %c %c,dest type is:GXFW\n", firmware->data[cfgPackageLen + 0],
                 firmware->data[cfgPackageLen + 1], firmware->data[cfgPackageLen + 2],
                 firmware->data[cfgPackageLen + 3]);
        ret = -1;
        goto exit;
    }

    if(firmware->data[cfgPackageLen + 4] != 1) {
        TPD_INFO("%s can't support this ver:%d\n", __func__, firmware->data[cfgPackageLen + 4]);
        ret = -1;
        goto exit;
    }

    fwPackageLen =  getU32(firmware->data + cfgPackageLen + 8);

    TPD_DEBUG("%s fw package len:%d\n", __func__, fwPackageLen);
    if((fwPackageLen + 16 + cfgPackageLen) > firmware->size ) {
        TPD_INFO("%s bad firmware,need len:%d,actual firmware size:%d\n",
                 __func__, fwPackageLen + 16 + cfgPackageLen, (int)firmware->size);
        ret = -1;
        goto exit;
    }

    fw_firmware->size = fwPackageLen;
    fw_firmware->data = firmware->data + cfgPackageLen + 16;

    TPD_DEBUG("success get fw,len:%d\n", fwPackageLen);
    TPD_DEBUG("fw head info:%*ph\n", 32, fw_firmware->data);
    TPD_DEBUG("fw tail info:%*ph\n", 4, &fw_firmware->data[fwPackageLen - 4 - 1]);
    ret = 0;

exit:
    TPD_DEBUG("%s exit:%d\n", __func__, ret);
    return ret;
}

static fw_update_state goodix_fw_update(void *chip_data, const struct firmware *cfg_fw_firmware, bool force)
{

#define FW_UPDATE_RETRY        2

    int retry0;
    int retry1;
    int r;
    int ret;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    struct monitor_data_v2 *monitor_data = chip_info->monitor_data_v2;
    struct fw_update_ctrl *fwu_ctrl = NULL;
    struct firmware fw_firmware;

    fwu_ctrl = kzalloc(sizeof(struct fw_update_ctrl), GFP_KERNEL);
    if (!fwu_ctrl) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "FWU_CTRL_KZL_failed");
        TPD_INFO("Failed to alloc memory for fwu_ctrl");
        return -ENOMEM;
    }

    r = gtx8_get_cfg_parms(chip_data, cfg_fw_firmware);
    if(r < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "GET_CFG_failed");
        TPD_INFO("%s Failed get cfg from firmware\n", __func__);
        //return -ENOMEM;
    } else {
        TPD_INFO("%s success get ic cfg from firmware\n", __func__);
    }

    r = gtx8_get_fw_parms(chip_data, cfg_fw_firmware, &fw_firmware);
    if(r < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "GET_FW_failed");
        TPD_INFO("%s Failed get ic fw from firmware\n", __func__);
        goto err_parse_fw;
    } else {
        TPD_INFO("%s success get ic fw from firmware\n", __func__);
    }

    retry0 = FW_UPDATE_RETRY;
    retry1 = FW_UPDATE_RETRY;
    r = 0;

    fwu_ctrl->fw_data.firmware = &fw_firmware;
    fwu_ctrl->progress = 0;
    fwu_ctrl->status = UPSTA_PREPARING;

    r = goodix_parse_firmware(&fwu_ctrl->fw_data);
    if (r < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "PARSE_FW_failed");
        fwu_ctrl->status = UPSTA_ABORT;
        goto err_parse_fw;
    }

    /* TODO: set force update flag*/
    fwu_ctrl->progress = 10;
    if (force == false) {
        r = goodix_check_update(chip_info->client, &fwu_ctrl->fw_data.fw_info);
        if (r == -EPERM) {
            r = FW_NO_NEED_UPDATE;
            fwu_ctrl->status = UPSTA_ABORT;
            goto err_check_update;
        }
    }

start_update:
    fwu_ctrl->progress = 20;
    fwu_ctrl->status = UPSTA_UPDATING; /* show upgrading status */
    r = goodix_update_prepare(chip_info, fwu_ctrl);

    if ((r == -EBUS || r == -5) && --retry0 > 0) {
        TPD_INFO("Bus error, retry prepare ISP:%d", FW_UPDATE_RETRY - retry0);
        goto start_update;
    } else if (r < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "PPR_ISP_failed");
        TPD_INFO("Failed to prepare ISP, exit update:%d", r);
        fwu_ctrl->status = UPSTA_FAILED;
        goto err_fw_prepare;
    }

    /* progress: 20%~100% */
    r = goodix_flash_firmware(chip_info->client, &fwu_ctrl->fw_data);
    if ((r == -EBUS || r == -ETIMEOUT) && --retry1 > 0) {
        /* we will retry[twice] if returns bus error[i2c/spi]
         * we will do hardware reset and re-prepare ISP and then retry
         * flashing
         */
        TPD_INFO("Bus error, retry firmware update:%d", FW_UPDATE_RETRY - retry1);
        goto start_update;
    } else if (r < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "FLSH_FW_failed");
        TPD_INFO("Fatal error, exit update:%d", r);
        fwu_ctrl->status = UPSTA_FAILED;
        goto err_fw_flash;
    }

    fwu_ctrl->status = UPSTA_SUCCESS;

err_fw_flash:
err_fw_prepare:
    goodix_update_finish(chip_info, fwu_ctrl);
err_check_update:
err_parse_fw:

    ret = goodix_send_config(chip_info, chip_info->normal_cfg.data, chip_info->normal_cfg.length);
    if(ret < 0) {
        tp_healthinfo_report(monitor_data, HEALTH_FW_UPDATE, "SENT_NML_CFG_failed");
        TPD_INFO("%s: send normal cfg failed:%d\n", __func__, ret);
    } else {
        TPD_INFO("%s: send normal cfg success\n", __func__);
        r = FW_UPDATE_SUCCESS;
    }

    msleep(200);

    if (fwu_ctrl->status == UPSTA_SUCCESS) {
        TPD_INFO("Firmware update successfully");
        r = FW_UPDATE_SUCCESS;
    } else if (fwu_ctrl->status == UPSTA_FAILED) {
        TPD_INFO("Firmware update failed");
        r = FW_UPDATE_ERROR;
    }

    if (fwu_ctrl) {
        kfree(fwu_ctrl);
        fwu_ctrl = NULL;
    }

    return r;
}
/*****************end of GT9886's update function********************/

static u32 goodix_u32_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    int ret = -1;
    u8 touch_num = 0;
    u8 check_sum = 0;
    u32 result_event = 0;
    int i = 0;
    bool all_zero = true;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    // TODO we can read 4 + BYTES_PER_COORD to accelerate i2c read speed
    memset(chip_info->touch_data, 0, MAX_GT_IRQ_DATA_LENGTH);
    if (chip_info->kernel_grip_support) {
        memset(chip_info->edge_data, 0, MAX_GT_EDGE_DATA_LENGTH);
    }

    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR,  4 + BYTES_PER_COORD, chip_info->touch_data);
    if (ret < 0) {
        TPD_INFO("%s: i2c transfer error!\n", __func__);
        goto IGNORE_CLEAR_IRQ;
    }

    if ( (chip_info->touch_data[0] & GOODIX_TOUCH_EVENT) == GOODIX_TOUCH_EVENT ) {
        touch_num = chip_info->touch_data[1] & 0x0F;
        touch_num = touch_num < MAX_POINT_NUM ? touch_num : MAX_POINT_NUM;
        if (chip_info->kernel_grip_support) {
            if (touch_num > 0) {
                ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_EDGE_INFO, BYTES_PER_EDGE * touch_num, chip_info->edge_data);
                if (ret < 0) {
                    TPD_INFO("%s: i2c transfer error!\n", __func__);
                    goto IGNORE_CLEAR_IRQ;
                }
            }
        }

        if (touch_num > 1) {
            ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR + 2, \
                    BYTES_PER_COORD * touch_num + 2, &chip_info->touch_data[2]); //read out point data
            if (ret < 0) {
                TPD_INFO("read touch point data from coor_addr failed!\n");
                goto IGNORE_CLEAR_IRQ;
            }

        }

        check_sum = 0;
        for (i = 0; i < 2 + BYTES_PER_COORD * touch_num + 2; i++) {   //do checksum
            check_sum += chip_info->touch_data[i];
        }
        if (check_sum) {
            TPD_INFO("irq touch_data checksum is invalid\n");
            goto IGNORE_CLEAR_IRQ;
        }

    } else if (unlikely(((chip_info->touch_data[0] & GOODIX_GESTURE_EVENT) != GOODIX_GESTURE_EVENT)
        && ((chip_info->touch_data[0] &GOODIX_FINGER_IDLE_EVENT ) != GOODIX_FINGER_IDLE_EVENT))) {
        check_sum = 0;
        for (i = 0; i < 12; i++) {  //do checksum
            check_sum += chip_info->touch_data[i];
            if (chip_info->touch_data[i]) {
                all_zero = false;
            }
        }
        if ((check_sum && chip_info->touch_data[0] == 0) || all_zero) {
            TPD_INFO("data is clear by last irq or all zero!\n");
            TPD_INFO("remaining coor data :%*ph\n", 12, chip_info->touch_data);
            return IRQ_IGNORE;
        } else if (check_sum) {
            TPD_INFO("irq checksum is invalid for first 12 bytes\n");
            goto IGNORE_CLEAR_IRQ;
        }
    }

    if (gesture_enable && is_suspended && (chip_info->touch_data[0] & GOODIX_GESTURE_EVENT) == GOODIX_GESTURE_EVENT) {   //check whether the gesture trigger
        return IRQ_GESTURE;
    } else if (is_suspended) {
        goto IGNORE_CLEAR_IRQ;
    }
    if (unlikely((chip_info->touch_data[0] & (GOODIX_TOUCH_EVENT | GOODIX_FINGER_IDLE_EVENT)) == GOODIX_FINGER_IDLE_EVENT)) {
        goto IGNORE_IDLETOACTIVE_IRQ;
    }
    if (unlikely((chip_info->touch_data[0] & GOODIX_REQUEST_EVENT) == GOODIX_REQUEST_EVENT)) {     //int request
        return IRQ_FW_CONFIG;
    }
    if (unlikely((chip_info->touch_data[0] & GOODIX_FINGER_STATUS_EVENT) == GOODIX_FINGER_STATUS_EVENT)) {
        SET_BIT(result_event, IRQ_FW_HEALTH);
    }

    chip_info->touch_data[BYTES_PER_COORD * touch_num + 2] = 0;
    chip_info->touch_data[BYTES_PER_COORD * touch_num + 3] = 0;

    if ((chip_info->touch_data[0] & GOODIX_TOUCH_EVENT) ==GOODIX_TOUCH_EVENT) {
        SET_BIT(result_event, IRQ_TOUCH);
        if ((chip_info->touch_data[0] & GOODIX_FINGER_PRINT_EVENT) == GOODIX_FINGER_PRINT_EVENT &&
            !is_suspended && (chip_info->fp_down_flag == false)) {
            chip_info->fp_down_flag = true;
            SET_BIT(result_event, IRQ_FINGERPRINT);
        } else if (!is_suspended && (chip_info->touch_data[0] & GOODIX_FINGER_PRINT_EVENT) != GOODIX_FINGER_PRINT_EVENT &&
                (chip_info->fp_down_flag == true) ) {
            chip_info->fp_down_flag = false;
            SET_BIT(result_event, IRQ_FINGERPRINT);
        }
    }
    return  result_event;

IGNORE_CLEAR_IRQ:
    TPD_INFO("error coor data :%*ph\n", 12, chip_info->touch_data);
    ret = goodix_clear_irq(chip_info);
    return IRQ_IGNORE;

IGNORE_IDLETOACTIVE_IRQ:
    TPD_DEBUG("idle to active :%*ph\n", 12, chip_info->touch_data);
    return IRQ_IGNORE;
}

static int goodix_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    int ret, i;
    int touch_map = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    u8 touch_num = 0;
    u8 finger_processed = 0;
    u8 *coor_data = NULL;
    u8 *ew_data = NULL;
    s32 id = 0;

    touch_num = chip_info->touch_data[1] & 0x0F;
    if (touch_num == 0) { //Up event
        goto END_TOUCH;
    }

    coor_data = &chip_info->touch_data[2];
    ew_data = &chip_info->edge_data[0];
    id = coor_data[0] & 0x0F;
    for (i = 0; i < max_num; i++) {
        if (i == id) {
            points[i].x = coor_data[1] | (coor_data[2] << 8);
            points[i].y = coor_data[3] | (coor_data[4] << 8);
            points[i].z = coor_data[5];
            points[i].width_major = 30; // any value
            points[i].status = 1;
            points[i].touch_major = coor_data[6];
            points[i].tx_press = ew_data[0];
            points[i].rx_press = ew_data[1];

            if (coor_data[0] & 0x10) {
                chip_info->fp_coor_report.fp_x_coor = points[i].x;
                chip_info->fp_coor_report.fp_y_coor = points[i].y;
                chip_info->fp_coor_report.fp_area = coor_data[7];
            }

            touch_map |= 0x01 << i;
            coor_data += 8;
            ew_data += BYTES_PER_EDGE;

            if (finger_processed++ < touch_num) {
                id = coor_data[0] & 0x0F;
            }
        }
    }
END_TOUCH:
    ret = goodix_clear_irq(chip_info);
    return touch_map;
}

static int goodix_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
    int ret = -1;
    int i;
    u8 check_sum = 0;
    uint8_t doze_buf[GSX_KEY_DATA_LEN] = {0};
    uint8_t point_data[MAX_GESTURE_POINT_NUM * 4 + 1];
    uint8_t point_num = 0;
    uint8_t gesture_id = 0;
    struct Coordinate limitPoint[4];
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_WAKEUP_GESTURE, sizeof(doze_buf), doze_buf);
    if (ret < 0) {
        TPD_INFO("%s: read gesture info i2c faild\n", __func__);
        return -1;
    }

    gesture_id = doze_buf[2];
    if (gesture_id == 0 || ((doze_buf[0] & GOODIX_GESTURE_EVENT)  == 0)) { //no gesture type, no need handle
        TPD_INFO("Read gesture data faild. doze_buf[0]=0x%x\n", doze_buf[0]);
        goto END_GESTURE;
    }

    for (i = 0, check_sum = 0; i < sizeof(doze_buf); i++) {
        check_sum += doze_buf[i];
    }
    if (check_sum) {
        TPD_INFO("gesture checksum error: %x\n", check_sum);
        goto END_GESTURE;
    }
    /*For FP_DOWN_DETECT FP_UP_DETECT no need to read GTP_REG_GESTURE_COOR data*/
    gesture->clockwise = 2;
    switch (gesture_id) {   //judge gesture type //Jarvis: need check with FW.protocol 3
    case FP_DOWN_DETECT:
        gesture->gesture_type = FingerprintDown;
        chip_info->fp_coor_report.fp_x_coor = (doze_buf[4] & 0xFF) | (doze_buf[5] & 0x0F) << 8;
        chip_info->fp_coor_report.fp_y_coor = (doze_buf[6] & 0xFF) | (doze_buf[7] & 0x0F) << 8;
        chip_info->fp_coor_report.fp_area = (doze_buf[12] & 0xFF) | (doze_buf[13] & 0x0F) << 8;

        gesture->Point_start.x = chip_info->fp_coor_report.fp_x_coor;
        gesture->Point_start.y = chip_info->fp_coor_report.fp_y_coor;
        gesture->Point_end.x   = chip_info->fp_coor_report.fp_area;
        chip_info->fp_down_flag = true;
        goto REPORT_GESTURE;
        break;

    case FP_UP_DETECT:
        gesture->gesture_type = FingerprintUp;
        chip_info->fp_coor_report.fp_x_coor = (doze_buf[4] & 0xFF) | (doze_buf[5] & 0x0F) << 8;
        chip_info->fp_coor_report.fp_y_coor = (doze_buf[6] & 0xFF) | (doze_buf[7] & 0x0F) << 8;
        chip_info->fp_coor_report.fp_area = (doze_buf[12] & 0xFF) | (doze_buf[13] & 0x0F) << 8;

        gesture->Point_start.x = chip_info->fp_coor_report.fp_x_coor;
        gesture->Point_start.y = chip_info->fp_coor_report.fp_y_coor;
        gesture->Point_end.x   = chip_info->fp_coor_report.fp_area;

        chip_info->fp_down_flag = false;
        goto REPORT_GESTURE;
        break;
    }

    memset(point_data, 0, sizeof(point_data));
    point_num = doze_buf[3];
    point_num = point_num < MAX_GESTURE_POINT_NUM ? point_num : MAX_GESTURE_POINT_NUM;

    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_GESTURE_COOR, point_num * 4 +1, point_data); //havn't add checksum here.
    if (ret < 0) {
        TPD_INFO("%s: read gesture data i2c faild\n", __func__);
        goto END_GESTURE;
    }
    switch (gesture_id) {   //judge gesture type //Jarvis: need check with FW.protocol 3
    case RIGHT_SLIDE_DETECT :
        gesture->gesture_type  = Left2RightSwip;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case LEFT_SLIDE_DETECT :
        gesture->gesture_type  = Right2LeftSwip;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case DOWN_SLIDE_DETECT  :
        gesture->gesture_type  = Up2DownSwip;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case UP_SLIDE_DETECT :
        gesture->gesture_type  = Down2UpSwip;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case DTAP_DETECT:
        gesture->gesture_type  = DouTap;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        break;

    case STAP_DETECT:
        gesture->gesture_type  = SingleTap;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        break;

    case UP_VEE_DETECT :
        gesture->gesture_type  = UpVee;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
        gesture->Point_1st.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_1st.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case DOWN_VEE_DETECT :
        gesture->gesture_type  = DownVee;
        getSpecialCornerPoint(&point_data[0], point_num, &limitPoint[0]);
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
        gesture->Point_1st.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_1st.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case LEFT_VEE_DETECT:
        gesture->gesture_type = LeftVee;
        getSpecialCornerPoint(&point_data[0], point_num, &limitPoint[0]);
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
        gesture->Point_1st.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_1st.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case RIGHT_VEE_DETECT ://this gesture is C
    case RIGHT_VEE_DETECT2://this gesture is <
        gesture->gesture_type  = RightVee;
        getSpecialCornerPoint(&point_data[0], point_num, &limitPoint[0]);
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
        gesture->Point_1st.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_1st.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        break;

    case CIRCLE_DETECT  :
        gesture->gesture_type = Circle;
        gesture->clockwise = clockWise(&point_data[0], point_num);
        getSpecialCornerPoint(&point_data[0], point_num, &limitPoint[0]);
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[20] & 0xFF) | (point_data[21] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[22] & 0xFF) | (point_data[23] & 0x0F) << 8;
        gesture->Point_1st = limitPoint[0]; //ymin
        gesture->Point_2nd = limitPoint[1]; //xmin
        gesture->Point_3rd = limitPoint[2]; //ymax
        gesture->Point_4th = limitPoint[3]; //xmax
        break;

    case DOUSWIP_DETECT  :
        gesture->gesture_type  = DouSwip;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x   = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_end.y   = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        gesture->Point_1st.x   = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
        gesture->Point_1st.y   = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
        gesture->Point_2nd.x   = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
        gesture->Point_2nd.y   = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
        break;

    case M_DETECT  :
        gesture->gesture_type  = Mgestrue;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x = (point_data[16] & 0xFF) | (point_data[17] & 0x0F) << 8;
        gesture->Point_end.y = (point_data[18] & 0xFF) | (point_data[19] & 0x0F) << 8;
        gesture->Point_1st.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_1st.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        gesture->Point_2nd.x = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
        gesture->Point_2nd.y = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
        gesture->Point_3rd.x = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
        gesture->Point_3rd.y = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
        break;

    case W_DETECT :
        gesture->gesture_type  = Wgestrue;
        gesture->Point_start.x = (point_data[0] & 0xFF) | (point_data[1] & 0x0F) << 8;
        gesture->Point_start.y = (point_data[2] & 0xFF) | (point_data[3] & 0x0F) << 8;
        gesture->Point_end.x = (point_data[16] & 0xFF) | (point_data[17] & 0x0F) << 8;
        gesture->Point_end.y = (point_data[18] & 0xFF) | (point_data[19] & 0x0F) << 8;
        gesture->Point_1st.x = (point_data[4] & 0xFF) | (point_data[5] & 0x0F) << 8;
        gesture->Point_1st.y = (point_data[6] & 0xFF) | (point_data[7] & 0x0F) << 8;
        gesture->Point_2nd.x = (point_data[8] & 0xFF) | (point_data[9] & 0x0F) << 8;
        gesture->Point_2nd.y = (point_data[10] & 0xFF) | (point_data[11] & 0x0F) << 8;
        gesture->Point_3rd.x = (point_data[12] & 0xFF) | (point_data[13] & 0x0F) << 8;
        gesture->Point_3rd.y = (point_data[14] & 0xFF) | (point_data[15] & 0x0F) << 8;
        break;

    default:
        gesture->gesture_type = UnkownGesture;
        break;
    }

REPORT_GESTURE:
    TPD_INFO("%s: gesture_id = 0x%x, gesture_type = %d, clockWise = %d, point:(%d %d)(%d %d)(%d %d)(%d %d)(%d %d)(%d %d)\n",
             __func__, gesture_id, gesture->gesture_type, gesture->clockwise,
             gesture->Point_start.x, gesture->Point_start.y, gesture->Point_end.x, gesture->Point_end.y,
             gesture->Point_1st.x, gesture->Point_1st.y, gesture->Point_2nd.x, gesture->Point_2nd.y,
             gesture->Point_3rd.x, gesture->Point_3rd.y, gesture->Point_4th.x, gesture->Point_4th.y);

END_GESTURE:
    ret = goodix_clear_irq(chip_info);  //clear int
    return 0;
}

static void goodix_get_health_info(void *chip_data, struct monitor_data *mon_data)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    struct goodix_health_info *health_info;
    struct goodix_health_info *health_local = &chip_info->health_info;
    //u8 log[20];
    struct goodix_health_info health_data;
    int ret = 0;
    u8 clear_flag = 0;

    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_DEBUG, sizeof(struct goodix_health_info), (unsigned char *)&health_data);
    if (ret < 0) {
        TPD_INFO("%s: read debug log data i2c faild\n", __func__);
        goto END_HEALTH;
    }
    TPD_DEBUG("GTP_REG_DEBUG:%*ph\n", sizeof(struct goodix_health_info), &health_data);

    //health_info = (struct goodix_health_info *)log;
    health_info = &health_data;

    if (health_info->shield_water) {
        mon_data->shield_water++;
        if (tp_debug != 0) {
            TPD_INFO("%s: enter water mode\n", __func__);
        }
    }
    if (health_info->baseline_refresh) {
        switch(health_info->baseline_refresh_type) {
            case BASE_DC_COMPONENT:
                mon_data->reserve1++;
                break;
            case BASE_SYS_UPDATE:
                mon_data->reserve2++;
                break;
            case BASE_NEGATIVE_FINGER:
                mon_data->reserve3++;
                break;
            case BASE_MONITOR_UPDATE:
                mon_data->reserve4++;
                break;
            case BASE_CONSISTENCE:
                mon_data->reserve4++;
                break;
            case BASE_FORCE_UPDATE:
                mon_data->baseline_err++;
                break;
            default:
                break;
        }
        if (tp_debug != 0) {
            TPD_INFO("%s: baseline refresh type: %d \n", __func__, health_info->baseline_refresh_type);
        }
    }
    if (health_info->shield_freq != 0) {
        mon_data->noise_count++;
        if (tp_debug != 0) {
            TPD_INFO("%s: freq before: %d HZ, freq after: %d HZ\n", __func__, health_info->freq_before, health_info->freq_after);
        }
    }
    if (health_info->fw_rst != 0) {
        switch(health_info->reset_reason) {
            case RST_MAIN_REG:
                mon_data->hard_rst++;
                break;
            case RST_OVERLAY_ERROR:
                mon_data->inst_rst++;
                break;
            case RST_LOAD_OVERLAY:
                mon_data->parity_rst++;
                break;
            case RST_CHECK_PID:
                mon_data->wd_rst++;
                break;
            case RST_CHECK_RAM:
                mon_data->other_rst++;
                break;
            case RST_CHECK_RAWDATA:
                mon_data->other_rst++;
                break;
            default:
                break;
        }

        if (tp_debug != 0) {
            TPD_INFO("%s: fw reset type : %d\n", __func__, health_info->reset_reason);
        }
    }
    if (health_info->shield_palm != 0) {
        mon_data->shield_palm++;
        TPD_DEBUG("%s: enter palm mode\n", __func__);
    }
    mon_data->shield_esd = chip_info->esd_err_count;
    mon_data->reserve5 =  chip_info->send_cmd_err_count;
    memcpy(health_local, health_info, sizeof(struct goodix_health_info));

    ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.GTP_REG_DEBUG, 1, &clear_flag);
    if (ret < 0) {
        TPD_INFO("%s: clear debug log data i2c faild\n", __func__);
    }

END_HEALTH:
    ret = goodix_clear_irq(chip_info);  //clear int

    return;
}

static void goodix_get_health_info_v2(void *chip_data, struct monitor_data_v2 *mon_data_v2)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    struct goodix_health_info *health_info;
    struct goodix_health_info *health_local = &chip_info->health_info;
    //u8 log[20];
    struct goodix_health_info health_data;
    int ret = 0;
    u8 clear_flag = 0;

    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_DEBUG, sizeof(struct goodix_health_info), (unsigned char *)&health_data);
    if (ret < 0) {
        TPD_INFO("%s: read debug log data i2c faild\n", __func__);
        goto END_HEALTH;
    }
    TPD_DEBUG("GTP_REG_DEBUG:%*ph\n", sizeof(struct goodix_health_info), &health_data);

    //health_info = (struct goodix_health_info *)log;
    health_info = &health_data;

    if (health_info->shield_water) {
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_SHIELD_WATER);
        if (tp_debug != 0) {
            TPD_INFO("%s: enter water mode\n", __func__);
        }
    }
    if (health_info->baseline_refresh) {
        switch(health_info->baseline_refresh_type) {
            case BASE_DC_COMPONENT:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, "BASE_DC_COMPONENT");
                break;
            case BASE_SYS_UPDATE:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, "BASE_SYS_UPDATE");
                break;
            case BASE_NEGATIVE_FINGER:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, "BASE_NEGATIVE_FINGER");
                break;
            case BASE_MONITOR_UPDATE:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, "BASE_MONITOR_UPDATE");
                break;
            case BASE_CONSISTENCE:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, "BASE_CONSISTENCE");
                break;
            case BASE_FORCE_UPDATE:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_BASELINE_ERR);
                break;
            default:
                break;
        }
        if (tp_debug != 0) {
            TPD_INFO("%s: baseline refresh type: %d \n", __func__, health_info->baseline_refresh_type);
        }
    }
    if (health_info->shield_freq != 0) {
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_NOISE);
        if (tp_debug != 0) {
            TPD_INFO("%s: freq before: %d HZ, freq after: %d HZ\n", __func__, health_info->freq_before, health_info->freq_after);
        }
    }
    if (health_info->fw_rst != 0) {
        switch(health_info->reset_reason) {
            case RST_MAIN_REG:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_RST_HARD);
                break;
            case RST_OVERLAY_ERROR:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_RST_INST);
                break;
            case RST_LOAD_OVERLAY:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_RST_PARITY);
                break;
            case RST_CHECK_PID:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_RST_WD);
                break;
            case RST_CHECK_RAM:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_RST_OTHER);
                break;
            case RST_CHECK_RAWDATA:
                tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_RST_OTHER);
                break;
            default:
                break;
        }

        if (tp_debug != 0) {
            TPD_INFO("%s: fw reset type : %d\n", __func__, health_info->reset_reason);
        }
    }
    if (health_info->shield_palm != 0) {
        tp_healthinfo_report(mon_data_v2, HEALTH_REPORT, HEALTH_REPORT_SHIELD_PALM);
        TPD_DEBUG("%s: enter palm mode\n", __func__);
    }
    memcpy(health_local, health_info, sizeof(struct goodix_health_info));

    ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.GTP_REG_DEBUG, 1, &clear_flag);
    if (ret < 0) {
        TPD_INFO("%s: clear debug log data i2c faild\n", __func__);
    }

END_HEALTH:
    ret = goodix_clear_irq(chip_info);  //clear int

    return;
}

static int goodix_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = -1;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    if (chip_info->halt_status && (mode != MODE_NORMAL)) {
        goodix_reset(chip_info);
    }

    switch(mode) {
    case MODE_NORMAL:
        ret = 0;    //after reset, it's already normal
        break;

    case MODE_SLEEP:
        ret = goodix_enter_sleep(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: goodix enter sleep failed\n", __func__);
        }
        break;

    case MODE_GESTURE:
        ret = goodix_enable_gesture(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: goodix enable:(%d) gesture failed.\n", __func__, flag);
            return ret;
        }
        break;

    case MODE_EDGE:
        ret = goodix_enable_edge_limit(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: goodix enable:(%d) edge limit failed.\n", __func__, flag);
            return ret;
        }
        break;

    case MODE_CHARGE:
        ret = goodix_enable_charge_mode(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable charge mode : %d failed\n", __func__, flag);
        }
        break;

    case MODE_GAME:
        ret = goodix_enable_game_mode(chip_info, flag);
        if (ret < 0) {
            TPD_INFO("%s: enable game mode : %d failed\n", __func__, flag);
        }
        break;

    default:
        TPD_INFO("%s: mode %d not support.\n", __func__, mode);
    }

    return ret;
}

static int goodix_esd_handle(void *chip_data) //Jarvis:have not finished
{
    s32 ret = -1;
    u8 esd_buf = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    if (!chip_info->esd_check_enabled) {
        TPD_DEBUG("%s: close\n", __func__);
        return 0;
    }

    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_ESD_READ, 1, &esd_buf);
    if ((ret < 0) || esd_buf == 0xAA) {
        TPD_INFO("%s: esd dynamic esd occur, ret = %d, esd_buf = %d.\n", __func__, ret, esd_buf);
        TPD_INFO("%s: IC works abnormally! Process esd reset.", __func__);
        disable_irq_nosync(chip_info->client->irq);

        goodix_power_control(chip_info, false);
        msleep(30);
        goodix_power_control(chip_info, true);
        msleep(10);

        goodix_reset(chip_data); // reset function have rewrite the esd reg no need to do it again

        tp_touch_btnkey_release();

        enable_irq(chip_info->client->irq);
        TPD_INFO("%s: Goodix esd reset over.", __func__);
        chip_info->esd_err_count ++;
        tp_healthinfo_report(chip_info->monitor_data_v2, HEALTH_REPORT, HEALTH_REPORT_SHIELD_ESD);
        return -1;
    } else {
        esd_buf = 0xAA;
        ret = touch_i2c_write_block(chip_info->client, chip_info->reg_info.GTP_REG_ESD_WRITE, 1, &esd_buf);
        if (ret < 0) {
            TPD_INFO("%s: Failed to reset esd reg.", __func__);
        }
    }

    return 0;
}

static void goodix_enable_fingerprint_underscreen(void *chip_data, uint32_t enable)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    TPD_INFO("%s, enable = %d\n", __func__, enable);
    if (enable) {
        ret = goodix_send_cmd(chip_info, GTP_CMD_FOD_FINGER_PRINT, 0);
    } else {
        ret = goodix_send_cmd(chip_info, GTP_CMD_FOD_FINGER_PRINT, 1);
    }

    return;
}

static void goodix_enable_gesture_mask(void *chip_data, uint32_t enable)
{
    int ret = -1;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    TPD_INFO("%s, enable = %d\n", __func__, enable);
    if (enable) {
        ret = goodix_send_cmd(chip_info, GTP_CMD_GESTURE_MASK, 1);/* enable all gesture */
    } else {
        ret = goodix_send_cmd(chip_info, GTP_CMD_GESTURE_MASK, 0);/* mask all gesture */
    }

    return;
}

static void goodix_screenon_fingerprint_info(void *chip_data, struct fp_underscreen_info *fp_tpinfo)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    if (chip_info->fp_down_flag) {
        fp_tpinfo->x = chip_info->fp_coor_report.fp_x_coor;
        fp_tpinfo->y = chip_info->fp_coor_report.fp_y_coor;
        fp_tpinfo->area_rate = chip_info->fp_coor_report.fp_area;
        fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;
    } else {
        fp_tpinfo->x = chip_info->fp_coor_report.fp_x_coor;
        fp_tpinfo->y = chip_info->fp_coor_report.fp_y_coor;
        fp_tpinfo->area_rate = chip_info->fp_coor_report.fp_area;
        fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;
    }
}

static int goodix_fw_handle(void *chip_data)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    ret = goodix_request_event_handler(chip_info);
    ret |= goodix_clear_irq(chip_info);

    return ret;
}

static void goodix_register_info_read(void *chip_data, uint16_t register_addr, uint8_t *result, uint8_t length)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    gt9886_i2c_read(chip_info->client, register_addr, length, result);         /*read data*/
}

static void goodix_set_touch_direction(void *chip_data, uint8_t dir)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    chip_info->touch_direction = dir;
}

static uint8_t goodix_get_touch_direction(void *chip_data)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    return chip_info->touch_direction;
}

static void goodix_specific_resume_operate(void *chip_data)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    TPD_DEBUG("%s call\n", __func__);
    goodix_esd_check_enable(chip_info, true);
}

static int goodix_enable_single_tap(void *chip_data, bool enable)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    chip_info->single_tap_flag = enable;
    return 0;
}

static struct oplus_touchpanel_operations goodix_ops = {
    .ftm_process                 = goodix_ftm_process,
    .get_vendor                  = goodix_get_vendor,
    .get_chip_info               = goodix_get_chip_info,
    .reset                       = goodix_reset,
    .power_control               = goodix_power_control,
    .fw_check                    = goodix_fw_check,
    .fw_update                   = goodix_fw_update,
    .u32_trigger_reason          = goodix_u32_trigger_reason,
    .get_touch_points            = goodix_get_touch_points,
    .get_gesture_info            = goodix_get_gesture_info,
    .mode_switch                 = goodix_mode_switch,
    .esd_handle                  = goodix_esd_handle,
    .fw_handle                   = goodix_fw_handle,
    .register_info_read          = goodix_register_info_read,
//    .get_usb_state               = goodix_get_usb_state,
    .enable_fingerprint          = goodix_enable_fingerprint_underscreen,
    .enable_gesture_mask         = goodix_enable_gesture_mask,
    .screenon_fingerprint_info   = goodix_screenon_fingerprint_info,
    .set_touch_direction         = goodix_set_touch_direction,
    .get_touch_direction         = goodix_get_touch_direction,
    .specific_resume_operate     = goodix_specific_resume_operate,
    .health_report               = goodix_get_health_info,
    .health_report_v2            = goodix_get_health_info_v2,
    .enable_single_tap           = goodix_enable_single_tap,
};
/********* End of implementation of oplus_touchpanel_operations callbacks**********************/

/******** Start of implementation of debug_info_proc_operations callbacks*********************/
static void goodix_debug_info_read(struct seq_file *s, void *chip_data, debug_type debug_type)
{
    int ret = -1, i = 0, j = 0;
    u8 *kernel_buf = NULL;
    int addr = 0;
    u8 clear_state = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    int TX_NUM = 0;
    int RX_NUM = 0;
    s16 data = 0;

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);
    /*keep in active mode*/
    goodix_send_cmd(chip_info, GTP_CMD_ENTER_DOZE_TIME, 0xFF);

    ret = goodix_get_channel_num(chip_info->client, &RX_NUM, &TX_NUM);
    if (ret < 0) {
        TPD_INFO("get channel num fail, quit!\n");
        goto read_data_exit;
    }

    kernel_buf = kzalloc(TX_NUM * RX_NUM * 2 > PAGE_SIZE ? TX_NUM * RX_NUM * 2 : PAGE_SIZE, GFP_KERNEL);
    if(kernel_buf == NULL) {
        TPD_INFO("%s kmalloc error\n", __func__);
        goodix_set_i2c_doze_mode(chip_info->client, true);
        return;
    }
    switch (debug_type) {
    case GTP_RAWDATA:
        addr = chip_info->reg_info.GTP_REG_RAWDATA;
        break;
    case GTP_DIFFDATA:
        addr = chip_info->reg_info.GTP_REG_DIFFDATA;
        break;
    default:
        addr = chip_info->reg_info.GTP_REG_BASEDATA;
        break;
    }
    gt8x_rawdiff_mode = 1;
    goodix_send_cmd(chip_info, GTP_CMD_RAWDATA, 0);
    msleep(20);
    gt9886_i2c_write(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, &clear_state);

    //wait for data ready
    while(i++ < 10) {
        ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, kernel_buf);
        TPD_INFO("ret = %d  kernel_buf = %d\n", ret, kernel_buf[0]);
        if((ret > 0) && ((kernel_buf[0] & 0x80) == 0x80)) {
            TPD_INFO("Data ready OK");
            break;
        }
        msleep(20);
    }
    if(i >= 10) {
        TPD_INFO("data not ready, quit!\n");
        goto read_data_exit;
    }

    ret = gt9886_i2c_read(chip_info->client, addr, TX_NUM * RX_NUM * 2, kernel_buf);
    msleep(5);

    for(i = 0; i < RX_NUM; i++) {
        seq_printf(s, "[%2d] ", i);
        for(j = 0; j < TX_NUM; j++) {
            data = (kernel_buf[j * RX_NUM * 2 + i * 2] << 8) + kernel_buf[j * RX_NUM * 2 + i * 2 + 1];
            seq_printf(s, "%4d ", data);
        }
        seq_printf(s, "\n");
    }
read_data_exit:
    goodix_send_cmd(chip_info, GTP_CMD_NORMAL, 0);
    gt8x_rawdiff_mode = 0;
    gt9886_i2c_write(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, &clear_state);
    /*be normal in idle*/
    goodix_send_cmd(chip_info, GTP_CMD_DEFULT_DOZE_TIME, 0x00);
    /*enable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, true);
    kfree(kernel_buf);
    return;
}

static void goodix_detailed_debug_info_read(struct seq_file *s, void *chip_data, debug_type debug_type)
{
    int ret = -1, i = 0, j = 0, retry = 0;
    u8 *kernel_buf = NULL;
    int addr = 0;
    u8 clear_state = 0, log_buf = 0, read_data_times = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    int TX_NUM = 0;
    int RX_NUM = 0;
    s16 data = 0;

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);
    /*keep in active mode*/
    goodix_send_cmd(chip_info, GTP_CMD_ENTER_DOZE_TIME, 0xFF);

    ret = goodix_get_channel_num(chip_info->client, &RX_NUM, &TX_NUM);
    if (ret < 0) {
        TPD_INFO("get channel num fail, quit!\n");
        goto read_data_exit;
    }

    kernel_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
    if(kernel_buf == NULL) {
        TPD_INFO("%s kmalloc error\n", __func__);
        goodix_set_i2c_doze_mode(chip_info->client, true);
        return;
    }
    switch (debug_type) {
    case GTP_RAWDATA:
        addr = chip_info->reg_info.GTP_REG_RAWDATA;
        break;
    case GTP_DIFFDATA:
        addr = chip_info->reg_info.GTP_REG_DIFFDATA;
        break;
    default:
        addr = chip_info->reg_info.GTP_REG_BASEDATA;
        break;
    }
    gt8x_rawdiff_mode = 1;

    log_buf = 0xAA;
    gt9886_i2c_write(chip_info->client, chip_info->reg_info.GTP_REG_DETAILED_DEBUG_INFO, 1, &log_buf);
    msleep(10);
    for(i = 0; i < 3; i++) {
        ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_DETAILED_DEBUG_INFO, 1, &log_buf);
        if(log_buf == 0xAA && ret > 0) {
            break;
        }
        msleep(5);
    }

    goodix_send_cmd(chip_info, GTP_CMD_RAWDATA, 0);
    msleep(20);

    while(retry++ < 10) {
        ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, kernel_buf);
        TPD_INFO("ret = %d  kernel_buf = %d\n", ret, kernel_buf[0]);
        if((ret > 0) && ((kernel_buf[0] & 0x80) == 0x80)) {
            TPD_INFO("Data ready OK");
            retry = 0;
            break;
        }
        msleep(20);
    }
    if(retry >= 10) {
        TPD_INFO("data not ready, quit!\n");
    }

    ret = gt9886_i2c_read(chip_info->client, addr, TX_NUM * RX_NUM * 2, kernel_buf);
    msleep(5);

    seq_printf(s, "debug data:\n");
    for(i = 0; i < RX_NUM; i++) {
        seq_printf(s, "[%2d] ", i);
        for(j = 0; j < TX_NUM; j++) {
            data = (kernel_buf[j * RX_NUM * 2 + i * 2] << 8) + kernel_buf[j * RX_NUM * 2 + i * 2 + 1];
            seq_printf(s, "%4d ", data);
        }
        seq_printf(s, "\n");
    }

    log_buf = 0x00;
    gt9886_i2c_write(chip_info->client, chip_info->reg_info.GTP_REG_DETAILED_DEBUG_INFO, 1, &log_buf);
    msleep(10);
    for(i = 0; i < 3; i++) {
        ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_DETAILED_DEBUG_INFO, 1, &log_buf);
        if(log_buf == 0x00 && ret > 0) {
            break;
        }
        msleep(5);
    }

    seq_printf(s, "\n");
    seq_printf(s, "diff data:\n");
    for(read_data_times = 0; read_data_times < 5; read_data_times++) {
        gt9886_i2c_write(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, &clear_state);
        //wait for data ready
        while(retry++ < 10) {
            ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, kernel_buf);
            TPD_INFO("ret = %d  kernel_buf = %d\n", ret, kernel_buf[0]);
            if((ret > 0) && ((kernel_buf[0] & 0x80) == 0x80)) {
                TPD_INFO("Data ready OK");
                retry = 0;
                break;
            }
            msleep(20);
        }
        if(retry >= 10) {
            TPD_INFO("data not ready, quit!\n");
            goto read_data_exit;
        }

        ret = gt9886_i2c_read(chip_info->client, addr, TX_NUM * RX_NUM * 2, kernel_buf);
        msleep(5);

        for(i = 0; i < RX_NUM; i++) {
            seq_printf(s, "[%2d] ", i);
            for(j = 0; j < TX_NUM; j++) {
                data = (kernel_buf[j * RX_NUM * 2 + i * 2] << 8) + kernel_buf[j * RX_NUM * 2 + i * 2 + 1];
                seq_printf(s, "%4d ", data);
            }
            seq_printf(s, "\n");
        }
        seq_printf(s, "\n");
    }

read_data_exit:
    goodix_send_cmd(chip_info, GTP_CMD_NORMAL, 0);
    gt8x_rawdiff_mode = 0;
    gt9886_i2c_write(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR, 1, &clear_state);
    /*be normal in idle*/
    goodix_send_cmd(chip_info, GTP_CMD_DEFULT_DOZE_TIME, 0x00);
    /*enable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, true);
    kfree(kernel_buf);
    return;
}

static void goodix_down_diff_info_read(struct seq_file *s, void *chip_data)
{
    int ret = -1, i = 0, j = 0;
    u8 *kernel_buf = NULL;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    int TX_NUM = 0;
    int RX_NUM = 0;
    s16 diff_data = 0;

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);

    kernel_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
    if(kernel_buf == NULL) {
        goodix_set_i2c_doze_mode(chip_info->client, true);
        TPD_INFO("%s kmalloc error\n", __func__);
        return;
    }

    ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_DOWN_DIFFDATA, 2, kernel_buf);
    if (ret < 0) {
        TPD_INFO("%s failed to read down diff data.\n", __func__);
        goto exit;
    }

    TX_NUM = kernel_buf[0];
    RX_NUM = kernel_buf[1];

    if (TX_NUM > 50 || RX_NUM > 50) {
        TPD_INFO("%s first finger down diff data is invalid.\n", __func__);
        goto exit;
    }

    if ((TX_NUM * RX_NUM * 2+2) > PAGE_SIZE) {
        TPD_INFO("%s: data invalid, tx:%d,rx :%d\n", __func__, TX_NUM, RX_NUM);
        goto exit;
    }

    ret = gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_DOWN_DIFFDATA, TX_NUM * RX_NUM * 2 + 2, kernel_buf);
    if (ret < 0) {
        TPD_INFO("%s failed to read down diff data.\n", __func__);
        goto exit;
    }

    seq_printf(s, "\nfinger first down diff data:\n");

    for(i = 0; i < RX_NUM; i++) {
        seq_printf(s, "[%2d] ", i);
        for(j = 0; j < TX_NUM; j++) {
            diff_data = (kernel_buf[j * RX_NUM * 2 + i * 2 + 2] << 8) + kernel_buf[j * RX_NUM * 2 + i * 2 + 1 + 2];
            seq_printf(s, "%4d ", diff_data);
        }
        seq_printf(s, "\n");
    }

exit:
    goodix_set_i2c_doze_mode(chip_info->client, true);
    if (kernel_buf) {
        kfree(kernel_buf);
    }

    return;
}

//proc/touchpanel/debug_info/delta
static void goodix_delta_read(struct seq_file *s, void *chip_data)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    if (chip_info->detail_debug_info_support) {
        goodix_detailed_debug_info_read(s, chip_data, GTP_DIFFDATA);
    } else {
        goodix_debug_info_read(s, chip_data, GTP_DIFFDATA);
    }
    if (tp_debug) {
        goodix_down_diff_info_read(s, chip_data);
    }
}

//proc/touchpanel/debug_info/baseline
static void goodix_baseline_read(struct seq_file *s, void *chip_data)
{
    goodix_debug_info_read(s, chip_data, GTP_RAWDATA);
}

//proc/touchpanel/debug_info/main_register
static void goodix_main_register_read(struct seq_file *s, void *chip_data)//Jarvis:need change to GT9886's register
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    int ret = 0;
    u8 *touch_data = NULL;

    /*. alloc touch data space */
    touch_data = kzalloc(4 + BYTES_PER_COORD, GFP_KERNEL);
    if (touch_data == NULL) {
        TPD_INFO("touch_data kzalloc error\n");
        return;
    }

    seq_printf(s, "====================================================\n");
    if(chip_info->p_tp_fw) {
        seq_printf(s, "tp fw = 0x%s\n", chip_info->p_tp_fw);
    }
    ret = touch_i2c_read_block(chip_info->client, chip_info->reg_info.GTP_REG_READ_COOR,  4 + BYTES_PER_COORD, touch_data);
    if (ret < 0) {
        TPD_INFO("%s: i2c transfer error!\n", __func__);
        goto out;
    }
    seq_printf(s, "touch_data reg 4100: %*ph\n", 4 + BYTES_PER_COORD, chip_info->touch_data);
    TPD_INFO("%s: touch_data reg 4100: %*ph\n", __func__, 4 + BYTES_PER_COORD, chip_info->touch_data);

    seq_printf(s, "reg 4100: %*ph\n", 4 + BYTES_PER_COORD, touch_data);
    TPD_INFO("%s: reg 4100: %*ph\n", __func__, 4 + BYTES_PER_COORD, touch_data);
    seq_printf(s, "====================================================\n");
out:
    if (touch_data) {
        kfree(touch_data);
        touch_data = NULL;
    }
    return;
}

static struct debug_info_proc_operations debug_info_proc_ops = {
    .limit_read         = goodix_limit_read,
    .delta_read    = goodix_delta_read,
    .baseline_read = goodix_baseline_read,
    .main_register_read = goodix_main_register_read,
};
/********* End of implementation of debug_info_proc_operations callbacks**********************/

/************** Start of callback of proc/Goodix/config_version node**************************/


/* success return config length else return -1 */
static int goodix_do_read_config(void *chip_data, u32 base_addr, u8 *buf)
{
    int sub_bags = 0;
    int offset = 0;
    int subbag_len;
    u8 checksum;
    int i;
    int ret;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);

    ret = gt9886_i2c_read(chip_info->client, base_addr, TS_CFG_HEAD_LEN, buf);
    if (ret < 0)
        goto err_out;

    offset = TS_CFG_BAG_START_INDEX;
    sub_bags = buf[TS_CFG_BAG_NUM_INDEX];
    checksum = checksum_u8(buf, TS_CFG_HEAD_LEN);
    if (checksum) {
        TPD_INFO("Config head checksum err:0x%x,data:%*ph", checksum, TS_CFG_HEAD_LEN, buf);
        ret = -EINVAL;
        goto err_out;
    }

    TPD_INFO("config_version:%u, vub_bags:%u", buf[0], sub_bags);
    for (i = 0; i < sub_bags; i++) {
        /* read sub head [0]: sub bag num, [1]: sub bag length */
        ret = gt9886_i2c_read(chip_info->client, base_addr + offset, 2, buf + offset);
        if (ret < 0)
            goto err_out;

        /* read sub bag data */
        subbag_len = buf[offset + 1];

        TPD_DEBUG("sub bag num:%u,sub bag length:%u", buf[offset], subbag_len);
        ret = gt9886_i2c_read(chip_info->client, base_addr + offset + 2, subbag_len + 1, buf + offset + 2);
        if (ret < 0)
            goto err_out;
        checksum = checksum_u8(buf + offset, subbag_len + 3);
        if (checksum) {
            TPD_INFO("sub bag checksum err:0x%x", checksum);
            ret = -EINVAL;
            goto err_out;
        }
        offset += subbag_len + 3;
        TPD_DEBUG("sub bag %d, data:%*ph", buf[offset], buf[offset + 1] + 3, buf + offset);
    }


    ret = offset;
    TPD_INFO("config_version,offset:%d ", offset);

err_out:
    /*enable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, true);
    TPD_INFO("config_version2,offset:%d ", offset);

    return ret;
}

/* success return config_len, <= 0 failed */
int goodix_read_config(void *chip_data, u8 *config_data, u32 config_len)
{
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    u8 cmd_flag = 0;
    int ret = 0;
    int i = 0;
    u32 cmd_reg = chip_info->reg_info.GTP_REG_CMD;

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);

    /* wait for IC in IDLE state */
    for (i = 0; i < TS_WAIT_CMD_FREE_RETRY_TIMES; i++) {
        cmd_flag = 0;
        ret = gt9886_i2c_read(chip_info->client, cmd_reg, 1, &cmd_flag);
        if (ret < 0 || cmd_flag == TS_CMD_REG_READY)
            break;
        usleep_range(10000, 11000);
    }
    if (cmd_flag != TS_CMD_REG_READY) {
        TPD_INFO("Wait for IC ready IDEL state timeout:addr 0x%x\n", cmd_reg);
        ret = -EAGAIN;
        goto exit;
    }
    /* 0x86 read config command */
    ret = goodix_send_cmd(chip_info, COMMAND_START_READ_CFG, 0);
    if (ret) {
        TPD_INFO("Failed send read config command");
        goto exit;
    }
    /* wait for config data ready */
    if (goodix_wait_cfg_cmd_ready(chip_info, COMMAND_READ_CFG_PREPARE_OK, COMMAND_START_READ_CFG)) {
        TPD_INFO("Wait for config data ready timeout");
        ret = -EAGAIN;
        goto exit;
    }

    if (config_len) {
        TPD_INFO("%s:config_len:%d\n", __func__, config_len);
        ret = gt9886_i2c_read(chip_info->client, cmd_reg + 16, config_len, config_data);
        if (ret < 0)
            TPD_INFO("Failed read config data");
        else
            ret = config_len;
    } else {
        ret = goodix_do_read_config(chip_info, cmd_reg + 16, config_data);
        if (ret < 0)
            TPD_INFO("Failed read config data");
        if (ret > 0)
            TPD_INFO("success read config, len:%d", ret);
    }

    /* clear command */
    goodix_send_cmd(chip_info, TS_CMD_REG_READY, 0);
    TPD_INFO("goodix_send_cmd exit:%d\n", ret);

exit:
    /*enable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, true);
    TPD_INFO("goodix_read_config exit:%d\n", ret);
    return ret;
}

static void goodix_config_info_read(struct seq_file *s, void *chip_data)
{
    int ret = 0, i = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    char temp_data[TS_CFG_MAX_LEN] = {0};

    seq_printf(s, "==== Goodix default config setting in driver====\n");
    for(i = 0; i < TS_CFG_MAX_LEN && i < chip_info->normal_cfg.length; i++) {
        seq_printf(s, "0x%02X, ", chip_info->normal_cfg.data[i]);
        if(i % 10 == 9)
            seq_printf(s, "\n");
    }
    seq_printf(s, "\n");
    seq_printf(s, "==== Goodix test cfg in driver====\n");
    for(i = 0; i < TS_CFG_MAX_LEN && i < chip_info->test_cfg.length; i++) {
        seq_printf(s, "0x%02X, ", chip_info->test_cfg.data[i]);
        if(i % 10 == 9)
            seq_printf(s, "\n");
    }
    seq_printf(s, "\n");
    seq_printf(s, "==== Goodix noise test cfg in driver====\n");
    for(i = 0; i < TS_CFG_MAX_LEN && i < chip_info->noise_test_cfg.length; i++) {
        seq_printf(s, "0x%02X, ", chip_info->noise_test_cfg.data[i]);
        if(i % 10 == 9)
            seq_printf(s, "\n");
    }
    seq_printf(s, "\n");

    seq_printf(s, "==== Goodix config read from chip====\n");

    if (!chip_info->reg_info.GTP_REG_CMD) {
        TPD_INFO("command register ERROR:0x%04x", chip_info->reg_info.GTP_REG_CMD);
        return;
    }

    ret = goodix_read_config(chip_info, temp_data, 0);
    if(ret < 0) {
        TPD_INFO("goodix_config_info_read goodix_read_config error:%d\n", ret);
        goto exit;
    }

    seq_printf(s, "\n");
    seq_printf(s, "==== Goodix Version Info ====\n");

    seq_printf(s, "ConfigVer: %02X\n", temp_data[0]);
    gt9886_i2c_read(chip_info->client, chip_info->reg_info.GTP_REG_PRODUCT_VER, 72, temp_data);
    seq_printf(s, "ProductID: GT%c%c%c%c\n", temp_data[9], temp_data[10], temp_data[11], temp_data[12]);
    seq_printf(s, "PatchID: %02X%02X%02X%02X\n", temp_data[17], temp_data[18], temp_data[19], temp_data[20]);
    seq_printf(s, "MaskID: %02X%02X%02X\n", temp_data[6], temp_data[7], temp_data[8]);
    seq_printf(s, "SensorID: %02X\n", temp_data[21]);


exit:
    return;
}
/*************** End of callback of proc/Goodix/config_version node***************************/

static u8 goodix_config_version_read(struct chip_data_gt9886 *chip_info)
{
    int ret = 0, i = 0;
    u8 cmd_flag;
    u32 cmd_reg = chip_info->reg_info.GTP_REG_CMD;
    char temp_data[TS_CFG_MAX_LEN] = {0};

    if (!chip_info->reg_info.GTP_REG_CMD) {
        TPD_INFO("command register ERROR:0x%04x", chip_info->reg_info.GTP_REG_CMD);
        return -EINVAL;
    }

    /*disable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, false);

    /* wait for IC in IDLE state */
    for (i = 0; i < TS_WAIT_CMD_FREE_RETRY_TIMES; i++) {
        cmd_flag = 0;
        ret = gt9886_i2c_read(chip_info->client, cmd_reg, 1, &cmd_flag);
        if (ret < 0 || cmd_flag == TS_CMD_REG_READY)
            break;
        usleep_range(10000, 11000);
    }
    if (cmd_flag != TS_CMD_REG_READY) {
        TPD_INFO("Wait for IC ready IDEL state timeout:addr 0x%x\n", cmd_reg);
        ret = -EAGAIN;
        goto exit;
    }
    /* 0x86 read config command */
    ret = goodix_send_cmd(chip_info, COMMAND_START_READ_CFG, 0);
    if (ret) {
        TPD_INFO("Failed send read config command");
        goto exit;
    }
    /* wait for config data ready */
    if (goodix_wait_cfg_cmd_ready(chip_info, COMMAND_READ_CFG_PREPARE_OK, COMMAND_START_READ_CFG)) {
        TPD_INFO("Wait for config data ready timeout");
        ret = -EAGAIN;
        goto exit;
    }

    ret = goodix_do_read_config(chip_info, cmd_reg + 16, temp_data);
    if (ret < 0)
        TPD_INFO("Failed read config data");
    if (ret > 0)
        TPD_INFO("success read config, len:%d", ret);

    /* clear command */
    goodix_send_cmd(chip_info, TS_CMD_REG_READY, 0);
    ret = temp_data[0];

exit:
    /*enable doze mode*/
    goodix_set_i2c_doze_mode(chip_info->client, true);
    return ret;
}

/************** Start of atuo test func**************************/

/***********************************************************************
* Function Name  : gtx8_get_cfg_value
* Description    : read config data specified by sub-bag number and inside-bag offset.
* config     : pointer to config data
* buf         : output buffer
* len         : data length want to read, if len = 0, get full bag data
* sub_bag_num    : sub-bag number
* offset         : offset inside sub-bag
* Return         : int(return offset with config[0], < 0 failed)
*******************************************************************************/
static int gtx8_get_cfg_value(u8 *config, u8 *buf, u8 len, u8 sub_bag_num, u8 offset)
{
    u8 *sub_bag_ptr = NULL;
    u8 i = 0;

    sub_bag_ptr = &config[4];
    for (i = 0; i < config[2]; i++) {
        if (sub_bag_ptr[0] == sub_bag_num)
            break;
        sub_bag_ptr += sub_bag_ptr[1] + 3;
    }

    if (i >= config[2]) {
        TPD_INFO("Cann't find the specifiled bag num %d\n", sub_bag_num);
        return -EINVAL;
    }

    if (sub_bag_ptr[1] + 3 < offset + len) {
        TPD_INFO("Sub bag len less then you want to read: %d < %d\n", sub_bag_ptr[1] + 3, offset + len);
        return -EINVAL;
    }

    if (len)
        memcpy(buf, sub_bag_ptr + offset, len);
    else
        memcpy(buf, sub_bag_ptr, sub_bag_ptr[1] + 3);
    return (sub_bag_ptr + offset - config);
}

/*******************************************************************************
* Function Name  : cfg_update_chksum
* Description    : update check sum
* Input          : u32* config
* Input          : u16 cfg_len
* Output         : u8* config
* Return         : none
*******************************************************************************/
static void cfg_update_chksum(u8 *config, u16 cfg_len)
{
    u16 pack_map_len_arr[100];
    u16 packNum = 0;
    u16 pack_len_tmp = 0;
    u16 pack_id_tmp = 0;
    u16 i = 0, j = 0;
    u16 cur_pos = 0;
    u8  check_sum = 0;

    if (cfg_len < 4) return;
    /* Head:4 bytes    |byte0:version|byte1:config refresh|byte2:package total num|byte3:check sum|*/
    pack_map_len_arr[pack_id_tmp] = 4;
    packNum = config[2];
    for (i = 4; i < cfg_len;) {
        pack_id_tmp++;
        pack_len_tmp = config[i + 1] + 3;
        pack_map_len_arr[pack_id_tmp] = pack_len_tmp;
        i += pack_len_tmp;
    }

    cur_pos = 0;
    for (i = 0; i <= pack_id_tmp; i++) {
        check_sum = 0;
        for (j = cur_pos; j < cur_pos + pack_map_len_arr[i] - 1; j++) {
            check_sum += config[j];
        }
        config[cur_pos + pack_map_len_arr[i] - 1] = (u8)(0 - check_sum);
        cur_pos += pack_map_len_arr[i];
    }
}

static int disable_hopping(struct gtx8_ts_test *ts_test, struct goodix_ts_config *test_config)
{
    int ret = 0;
    u8 value = 0;
    u16 offset = 0;
    ret = gtx8_get_cfg_value(test_config->data, &value, 1, 10, 2);
    if (ret < 0 ) {
        TPD_INFO("Failed parse hopping reg\n");
        return -EINVAL;
    }
    offset = ret;
    /* disable hopping */
    value = test_config->data[offset];
    value &= 0xfe;
    test_config->data[offset] = value;
    TPD_INFO("disable_hopping:0x%02x_%d", test_config->data[offset], offset);
    cfg_update_chksum(test_config->data, test_config->length);
    return ret;
}

static int init_test_config(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    if (ts_test->test_config.length == 0) {
        TPD_INFO("switch to orignal config!\n");
        memmove(ts_test->test_config.data, ts_test->orig_config.data, ts_test->orig_config.length);
        ts_test->test_config.length = ts_test->orig_config.length;
    } else {
        ts_test->test_config.data[0] = ts_test->orig_config.data[0];
        ts_test->test_config.data[1] |= 0x01;
        TPD_INFO("switch to test config!\n");
    }
    ts_test->test_config.reg_base = chip_info->reg_info.GTP_REG_CONFIG_DATA;
    mutex_init(&(ts_test->test_config.lock));
    ts_test->test_config.initialized = true;
    strcpy(ts_test->test_config.name, "test_config");
    disable_hopping(ts_test, &ts_test->test_config);
    return ret;
}

static void gtx8_print_testdata(struct gtx8_ts_test *ts_test)
{
    struct ts_test_params *test_params = NULL;
    int i = 0;
    test_params = &ts_test->test_params;

    if (tp_debug < 2)
        return;
    TPD_DEBUG("rawdata:\n");
    for(i = 0; i < test_params->drv_num * test_params->sen_num; i++) {
        TPD_DEBUG("%d,", ts_test->rawdata.data[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }

    TPD_DEBUG("noisedata:\n");
    for(i = 0; i < test_params->drv_num * test_params->sen_num; i++) {
        TPD_DEBUG("%d,", ts_test->noisedata.data[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }

    TPD_DEBUG("self_rawdata:\n");
    for(i = 0; i < test_params->drv_num + test_params->sen_num; i++) {
        TPD_DEBUG("%d,", ts_test->self_rawdata.data[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }

    TPD_DEBUG("self_noisedata:\n");
    for(i = 0; i < test_params->drv_num + test_params->sen_num; i++) {
        TPD_DEBUG("%d,", ts_test->self_noisedata.data[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }
}

static void gtx8_print_test_params(struct gtx8_ts_test *ts_test)
{
    struct ts_test_params *test_params = NULL;
    int i = 0;
    test_params = &ts_test->test_params;

    if (tp_debug < 2)
        return;
    TPD_DEBUG("sen_num:%d,drv_num:%d\n", test_params->sen_num, test_params->drv_num);
    TPD_DEBUG("rawdata_addr:%d,noisedata_addr:%d\n", test_params->rawdata_addr, test_params->noisedata_addr);
    TPD_DEBUG("self_rawdata_addr:%d,self_noisedata_addr:%d\n", test_params->self_rawdata_addr, test_params->self_noisedata_addr);
    TPD_DEBUG("basedata_addr:%d\n", test_params->basedata_addr);

    TPD_DEBUG("max_limits:\n");
    for(i = 0; i < test_params->drv_num * test_params->sen_num; i++) {
        TPD_DEBUG("%d", test_params->max_limits[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }

    TPD_DEBUG("min_limits:\n");
    for(i = 0; i < test_params->drv_num * test_params->sen_num; i++) {
        TPD_DEBUG("%d,", test_params->min_limits[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }

    TPD_DEBUG("deviation_limits:\n");
    for(i = 0; i < test_params->drv_num * test_params->sen_num; i++) {
        TPD_DEBUG("%d,", test_params->deviation_limits[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }

    TPD_DEBUG("self_max_limits:\n");
    for(i = 0; i < test_params->drv_num + test_params->sen_num; i++) {
        TPD_DEBUG("%d,", test_params->self_max_limits[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }

    TPD_DEBUG("self_min_limits:\n");
    for(i = 0; i < test_params->drv_num + test_params->sen_num; i++) {
        TPD_DEBUG("%d,", test_params->self_min_limits[i]);
        if (!((i + 1) % test_params->sen_num) && (i != 0))
            TPD_DEBUG("\n");
    }
}

static int gtx8_init_testlimits(struct gtx8_ts_test *ts_test)
{
    struct goodix_testdata *p_testdata = ts_test->p_testdata;
    struct test_item_info *p_test_item_info = NULL;
    /*for item parameter data*/
    uint32_t *para_data32 = NULL;
    uint32_t para_num = 0;
    int ret = 0;
    struct ts_test_params *test_params = NULL;

    test_params = &ts_test->test_params;
    test_params->drv_num = p_testdata->TX_NUM;
    test_params->sen_num = p_testdata->RX_NUM;
    TPD_INFO("sen_num:%d,drv_num:%d\n", test_params->sen_num, test_params->drv_num);

    para_data32 = getpara_for_item(p_testdata->fw, TYPE_NOISE_DATA_LIMIT, &para_num);
    if(para_data32) {
        ts_test->is_item_support[TYPE_NOISE_DATA_LIMIT] = para_data32[0];
        if (para_num >= 2) {
            /* store data to test_parms */
            test_params->noise_threshold = para_data32[1];
        }
    } else {
        TPD_INFO("%s: Failed get %s\n", __func__, CSV_TP_NOISE_LIMIT);
        ret = -1;
        goto INIT_LIMIT_END;
    }

    para_data32 = getpara_for_item(p_testdata->fw, TYPE_SHORT_THRESHOLD, &para_num);
    if(para_data32) {
        ts_test->is_item_support[TYPE_SHORT_THRESHOLD] = para_data32[0];
        TPD_INFO("get TYPE_SHORT_THRESHOLD data,len:%d\n", para_num);
        if (para_num >= 8) {
            /* store data to test_parms */
            test_params->short_threshold = para_data32[1];
            test_params->r_drv_drv_threshold = para_data32[2];
            test_params->r_drv_sen_threshold = para_data32[3];
            test_params->r_sen_sen_threshold = para_data32[4];
            test_params->r_drv_gnd_threshold = para_data32[5];
            test_params->r_sen_gnd_threshold = para_data32[6];
            test_params->avdd_value = para_data32[7];
        }
    } else {
        TPD_INFO("%s: Failed get %s\n", __func__, CSV_TP_SHORT_THRESHOLD);
        ret = -1;
        goto INIT_LIMIT_END;
    }

    p_test_item_info = get_test_item_info(p_testdata->fw, TYPE_SPECIAL_RAW_MAX_MIN);
    if(p_test_item_info) {
        if (p_test_item_info->para_num) {
            if (p_test_item_info->p_buffer[0]) {
                TPD_INFO("get TYPE_SPECIAL_RAW_MAX_MIN data,len:%d\n", para_num);
                ts_test->is_item_support[TYPE_SPECIAL_RAW_MAX_MIN] = p_test_item_info->p_buffer[0];
                if (p_test_item_info->item_limit_type == LIMIT_TYPE_MAX_MIN_DATA) {
                    test_params->max_limits = (uint32_t *)(p_testdata->fw->data + p_test_item_info->top_limit_offset);
                    test_params->min_limits = (uint32_t *)(p_testdata->fw->data + p_test_item_info->floor_limit_offset);
                } else {
                    TPD_INFO("item: %d get_test_item_info fail\n", TYPE_SPECIAL_RAW_MAX_MIN);
                }
            }
        }
    } else {
        ret = -1;
        TPD_INFO("item: %d get_test_item_info fail\n", TYPE_SPECIAL_RAW_MAX_MIN);
    }

    p_test_item_info = get_test_item_info(p_testdata->fw, TYPE_SPECIAL_RAW_DELTA);
    if (p_test_item_info) {
        if (p_test_item_info->para_num) {
            if (p_test_item_info->p_buffer[0]) {
                TPD_INFO("get TYPE_SPECIAL_RAW_DELTA data,len:%d\n", para_num);
                ts_test->is_item_support[TYPE_SPECIAL_RAW_DELTA] = p_test_item_info->p_buffer[0];
                if (p_test_item_info->item_limit_type == LIMIT_TYPE_MAX_MIN_DATA) {
                    test_params->deviation_limits = (uint32_t *)(p_testdata->fw->data + p_test_item_info->top_limit_offset);
                } else {
                    TPD_INFO("item: %d get_test_item_info fail\n", TYPE_SPECIAL_RAW_DELTA);
                }
            }
        }
    } else {
        ret = -1;
        TPD_INFO("item: %d get_test_item_info fail\n", TYPE_SPECIAL_RAW_DELTA);
    }

    para_data32 = getpara_for_item(p_testdata->fw, TYPE_NOISE_SLEFDATA_LIMIT, &para_num);
    if(para_data32) {
        ts_test->is_item_support[TYPE_NOISE_SLEFDATA_LIMIT] = para_data32[0];
        if (para_num >= 2) {
            /* store data to test_parms */
            test_params->self_noise_threshold = para_data32[1];
        }
    } else {
        TPD_INFO("%s: Failed get %s\n", __func__, CSV_TP_SELFNOISE_LIMIT);
        ret = -1;
        goto INIT_LIMIT_END;
    }

    p_test_item_info = get_test_item_info(p_testdata->fw, TYPE_SPECIAL_SELFRAW_MAX_MIN);
    if(p_test_item_info) {
        if (p_test_item_info->para_num) {
            if (p_test_item_info->p_buffer[0]) {
                ts_test->is_item_support[TYPE_SPECIAL_SELFRAW_MAX_MIN] = p_test_item_info->p_buffer[0];
                if (p_test_item_info->item_limit_type == IMIT_TYPE_SLEFRAW_DATA) {
                    TPD_INFO("get IMIT_TYPE_SLEFRAW_DATA data\n");
                    test_params->self_max_limits = (int32_t *)(p_testdata->fw->data + p_test_item_info->top_limit_offset);
                    test_params->self_min_limits = (int32_t *)(p_testdata->fw->data + p_test_item_info->floor_limit_offset);
                } else {
                    TPD_INFO("item: %d get_test_item_info fail\n", TYPE_SPECIAL_SELFRAW_MAX_MIN);
                }
            } else {
                TPD_INFO("TYPE_SPECIAL_SELFRAW_MAX_MIN para_num is invalid\n");
            }
        }
    } else {
        ret = -1;
        TPD_INFO("item: %d get_test_item_info fail\n", TYPE_SPECIAL_SELFRAW_MAX_MIN);
    }

    ret |= init_test_config(ts_test);

INIT_LIMIT_END:
    if (p_test_item_info) {
        kfree(p_test_item_info);
    }
    return ret;
}

static int gtx8_init_params(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    struct ts_test_params *test_params = &ts_test->test_params;

    TPD_INFO("%s run\n", __func__);

    test_params->rawdata_addr = GTP_RAWDATA_ADDR_9886;
    test_params->noisedata_addr = GTP_NOISEDATA_ADDR_9886;
    test_params->self_rawdata_addr =  GTP_SELF_RAWDATA_ADDR_9886;
    test_params->self_noisedata_addr = GTP_SELF_NOISEDATA_ADDR_9886;
    test_params->basedata_addr = GTP_BASEDATA_ADDR_9886;
    test_params->max_drv_num = MAX_DRV_NUM;
    test_params->max_sen_num = MAX_SEN_NUM;
    test_params->drv_map = gt9886_drv_map;
    test_params->sen_map = gt9886_sen_map;
    TPD_INFO("%s exit:%d\n", __func__, ret);
    return ret;
}

/* init cmd data*/
static int gtx8_init_cmds(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    /* init rawdata cmd */
    ts_test->rawdata_cmd.initialized = 1;
    ts_test->rawdata_cmd.cmd_reg = chip_info->reg_info.GTP_REG_CMD;
    ts_test->rawdata_cmd.length = 3;
    ts_test->rawdata_cmd.cmds[0] = GTX8_CMD_RAWDATA;
    ts_test->rawdata_cmd.cmds[1] = 0x00;
    ts_test->rawdata_cmd.cmds[2] = (u8)((0 - ts_test->rawdata_cmd.cmds[0] -
                                         ts_test->rawdata_cmd.cmds[1]) & 0xff);
    /* init normal cmd */
    ts_test->normal_cmd.initialized = 1;
    ts_test->normal_cmd.cmd_reg = chip_info->reg_info.GTP_REG_CMD;
    ts_test->normal_cmd.length = 3;
    ts_test->normal_cmd.cmds[0] = GTX8_CMD_NORMAL;
    ts_test->normal_cmd.cmds[1] = 0x00;
    ts_test->normal_cmd.cmds[2] = (u8)((0 - ts_test->normal_cmd.cmds[0] -
                                        ts_test->normal_cmd.cmds[1]) & 0xff);

    TPD_INFO("cmd addr.0x%04x\n", ts_test->rawdata_cmd.cmd_reg);
    return ret;
}

/* gtx8_read_origconfig
 *
 * read original config data
 */
static int gtx8_cache_origconfig(struct gtx8_ts_test *ts_test)
{
    int ret = -ENODEV;
    u8 checksum = 0;

    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    TPD_INFO("gtx8_cache_origconfig run\n");

    ret = goodix_read_config(chip_info, &ts_test->orig_config.data[0], 0);
    if (ret < 0) {
        TPD_INFO("Failed to read original config data\n");
        return ret;
    }
    TPD_INFO("gtx8_cache_origconfig read original config success\n");

    ts_test->orig_config.data[1] |= GTX8_CONFIG_REFRESH_DATA;
    checksum = checksum_u8(&ts_test->orig_config.data[0], 3);
    ts_test->orig_config.data[3] = (u8)(0 - checksum);

    mutex_init(&ts_test->orig_config.lock);
    ts_test->orig_config.length = ret;
    strcpy(ts_test->orig_config.name, "original_config");
    ts_test->orig_config.initialized = true;

    TPD_INFO("gtx8_cache_origconfig exit\n");
    return NO_ERR;
}

/* gtx8_tptest_prepare
 *
 * preparation before tp test
 */
static int gtx8_tptest_prepare(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    TPD_INFO("TP test preparation\n");
    ret = gtx8_cache_origconfig(ts_test);
    if (ret) {
        TPD_INFO("Failed cache origin config\n");
        return ret;
    }

    /* init cmd*/
    ret = gtx8_init_cmds(ts_test);
    if (ret) {
        TPD_INFO("Failed init cmd\n");
        return ret;
    }

    /* init reg addr and short cal map */
    ret = gtx8_init_params(ts_test);
    if (ret) {
        TPD_INFO("Failed init register address\n");
        return ret;
    }
    /* parse test limits from csv */
    ret = gtx8_init_testlimits(ts_test);
    if (ret < 0) {
        TPD_INFO("Failed to init testlimits from csv:%d\n", ret);
        ts_test->error_count ++;
        seq_printf(ts_test->p_seq_file, "Failed to init testlimits from csv\n");
    }
    ret = goodix_send_config(chip_info, chip_info->noise_test_cfg.data, chip_info->noise_test_cfg.length);
    if (ret < 0) {
       TPD_INFO("Failed to send noise test config config:%d,noise_config len:%d\n", ret, chip_info->noise_test_cfg.length);
    }
    TPD_INFO("send noise test config success :%d,noise_config len:%d\n", ret, chip_info->noise_test_cfg.length);
    //msleep(50);

    return ret;
}

/* gtx8_tptest_finish
 *
 * finish test
 */
static int gtx8_tptest_finish(struct gtx8_ts_test *ts_test)
{
    int ret = RESULT_ERR;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    TPD_INFO("TP test finish\n");
    goodix_reset_via_gpio(chip_info);

    //    if (goodix_set_i2c_doze_mode(core_data->ts_dev,true))
    //        ts_info("WRNING: doze may enabled after reset\n");
    ret = goodix_send_config(chip_info, ts_test->orig_config.data, ts_test->orig_config.length);
    if(ret < 0) {
        TPD_INFO("gtx8_tptest_finish:Failed to send normal config:%d\n", ret);
    } else {
        TPD_INFO("gtx8_tptest_finish send normal config succesful\n");
    }

    return ret;
}

static int normandy_sync_read_rawdata(struct i2c_client *client,
                                      unsigned int sync_addr, unsigned char sync_mask,
                                      unsigned int reg_addr, unsigned char *data,
                                      unsigned int len)
{
    int ret = 0;
    int retry_times = 0;
    unsigned char sync_data[0];


    TPD_INFO("%s run,reg_addr:0x%04x,len:%d,sync_addr:0x%04x,sync_mask:0x%x\n",
             __func__, reg_addr, len, sync_addr, sync_mask);

    while(retry_times ++ < 200) {
        sync_data[0] = 0x00;
        msleep(10);
        ret = touch_i2c_read_block(client, sync_addr, 1, sync_data);
        if(ret < 0 || ((sync_data[0] & sync_mask) == 0)) {
            TPD_INFO("%s sync invalid,ret:%d,sync data:0x%x\n",
                     __func__, ret, sync_data[0]);
            continue;
        }

        TPD_INFO("%s sync is valid:0x%x\n", __func__, sync_data[0]);

        ret = touch_i2c_read_block(client, reg_addr, len, data);
        if(ret < 0) {
            TPD_INFO("sync read rawdata failed:%d\n", ret);
        } else {
            break;
        }
    }

    if(retry_times >= 200) {
        TPD_INFO("sync read rawdata timeout\n");
        ret = -1;
    } else {
        TPD_INFO("sync read rawdata,get rawdata success\n");
        sync_data[0] = 0x00;
        ret = touch_i2c_write_block(client, sync_addr, 1, sync_data);
        TPD_INFO("sync read rawdata,clear sync\n");
        ret = 0;
    }

    TPD_INFO("%s exit:%d\n", __func__, ret);
    return ret;
}

/**
 * gtx8_cache_rawdata - cache rawdata
 */
static int gtx8_cache_rawdata(struct gtx8_ts_test *ts_test)
{
    int j = 0;
    int i = 0;
    int ret = -EINVAL;
    u32 rawdata_size = 0;
    u16 rawdata_addr = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;
    TPD_INFO("Cache rawdata\n");
    ts_test->rawdata.size = 0;
    rawdata_size = ts_test->test_params.sen_num * ts_test->test_params.drv_num;

    if (rawdata_size > MAX_DRV_NUM * MAX_SEN_NUM || rawdata_size <= 0) {
        TPD_INFO("Invalid rawdata size(%u)\n", rawdata_size);
        return ret;
    }

    rawdata_addr = ts_test->test_params.rawdata_addr;
    TPD_INFO("Rawdata address=0x%x\n", rawdata_addr);

    for (j = 0; j < GTX8_RETRY_NUM_3; j++) {
        /* read rawdata */
        ret = normandy_sync_read_rawdata(chip_info->client,
                                         chip_info->reg_info.GTP_REG_READ_COOR, 0x80, rawdata_addr,
                                         (u8 *)&ts_test->rawdata.data[0], rawdata_size * sizeof(u16));
        if (ret < 0) {
            if (j == GTX8_RETRY_NUM_3 - 1) {
                TPD_INFO("Failed to read rawdata:%d,at:%d\n", ret, j);
                goto cache_exit;
            } else {
                continue;
            }
        }
        for (i = 0; i < rawdata_size; i++)
            ts_test->rawdata.data[i] = be16_to_cpu(ts_test->rawdata.data[i]);
        ts_test->rawdata.size = rawdata_size;
        TPD_INFO("Rawdata ready\n");
        break;
    }

cache_exit:
    return ret;
}

/**
 * gtx8_cache_selfrawdata - cache selfrawdata
 */
static int gtx8_cache_self_rawdata(struct gtx8_ts_test *ts_test)
{
    int i = 0, j = 0;
    int ret = -EINVAL;
    u16 self_rawdata_size = 0;
    u16 self_rawdata_addr = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;
    TPD_INFO("Cache selfrawdata\n");
    ts_test->self_rawdata.size = 0;
    self_rawdata_size = ts_test->test_params.sen_num + ts_test->test_params.drv_num;

    if (self_rawdata_size > MAX_DRV_NUM + MAX_SEN_NUM || self_rawdata_size <= 0) {
        TPD_INFO("Invalid selfrawdata size(%u)\n", self_rawdata_size);
        return ret;
    }

    self_rawdata_addr = ts_test->test_params.self_rawdata_addr;
    TPD_INFO("Selfraw address=0x%x\n", self_rawdata_addr);

    for (j = 0; j < GTX8_RETRY_NUM_3; j++) {
        /* read selfrawdata */
        ret = normandy_sync_read_rawdata(chip_info->client,
                                         chip_info->reg_info.GTP_REG_READ_COOR, 0x80,
                                         self_rawdata_addr, (u8 *)&ts_test->self_rawdata.data[0],
                                         self_rawdata_size * sizeof(u16));
        if (ret < 0) {
            if (j == GTX8_RETRY_NUM_3 - 1) {
                TPD_INFO("Failed to read self_rawdata:%d\n", ret);
                goto cache_exit;
            } else {
                continue;
            }
        }
        for (i = 0; i < self_rawdata_size; i++)
            ts_test->self_rawdata.data[i] = be16_to_cpu(ts_test->self_rawdata.data[i]);
        ts_test->self_rawdata.size = self_rawdata_size;
        TPD_INFO("self_Rawdata ready\n");
        break;
    }

cache_exit:
    TPD_INFO("%s exit:%d\n", __func__, ret);
    return ret;
}

/**
 * gtx8_noisetest_prepare- noisetest prepare
 */
static int gtx8_noisetest_prepare(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    u32 noise_data_size = 0;
    u32 self_noise_data_size = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    TPD_INFO("%s run\n", __func__);
    noise_data_size = ts_test->test_params.sen_num * ts_test->test_params.drv_num;
    self_noise_data_size = ts_test->test_params.sen_num + ts_test->test_params.drv_num;

    if (noise_data_size <= 0 || noise_data_size > MAX_DRV_NUM * MAX_SEN_NUM) {
        TPD_INFO("%s: Bad noise_data_size[%d]\n", __func__, noise_data_size);
        ts_test->test_result[GTP_NOISE_TEST] = SYS_SOFTWARE_REASON;
        return -EINVAL;
    }

    if (self_noise_data_size <= 0 || self_noise_data_size > MAX_DRV_NUM + MAX_SEN_NUM) {
        TPD_INFO("%s: Bad self_noise_data_size[%d]\n", __func__, self_noise_data_size);
        ts_test->test_result[GTP_SELFNOISE_TEST] = SYS_SOFTWARE_REASON;
        return -EINVAL;
    }

    ts_test->noisedata.size = noise_data_size;
    ts_test->self_noisedata.size = self_noise_data_size;

    TPD_INFO("%s: noise test prepare rawdata addr:0x%04x,len:%d",
             __func__, ts_test->rawdata_cmd.cmd_reg,
             ts_test->rawdata_cmd.length);
    /* change to rawdata mode */
    ret = touch_i2c_write_block(chip_info->client, ts_test->rawdata_cmd.cmd_reg,
                                ts_test->rawdata_cmd.length, ts_test->rawdata_cmd.cmds);
    if (ret < 0) {
        TPD_INFO("%s: Failed send rawdata command:ret%d\n", __func__, ret);
        ts_test->test_result[GTP_NOISE_TEST] = SYS_SOFTWARE_REASON;
        ts_test->test_result[GTP_SELFNOISE_TEST] = SYS_SOFTWARE_REASON;
        return ret;
    }
    msleep(50);

    TPD_INFO("%s: Enter rawdata mode\n", __func__);

    return ret;
}

/**
 * gtx8_cache_noisedata- cache noisedata
 */
static int gtx8_cache_noisedata(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    int ret1 = 0;
    u16 noisedata_addr = 0;
    u16 self_noisedata_addr = 0;
    u32 noise_data_size = 0;
    u32 self_noise_data_size = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    noisedata_addr = ts_test->test_params.noisedata_addr;
    self_noisedata_addr = ts_test->test_params.self_noisedata_addr;
    noise_data_size = ts_test->noisedata.size;
    self_noise_data_size = ts_test->self_noisedata.size;

    /* read noise data */
    ret = normandy_sync_read_rawdata(chip_info->client,
                                     chip_info->reg_info.GTP_REG_READ_COOR, 0x80,
                                     noisedata_addr, (u8 *)&ts_test->noisedata.data[0],
                                     noise_data_size * sizeof(u16));
    if (ret < 0) {
        TPD_INFO("%s: Failed read noise data\n", __func__);
        ts_test->noisedata.size = 0;
        ts_test->test_result[GTP_NOISE_TEST] = SYS_SOFTWARE_REASON;
    }

    /* read self noise data */
    ret1 = normandy_sync_read_rawdata(chip_info->client,
                                      chip_info->reg_info.GTP_REG_READ_COOR, 0x80,
                                      self_noisedata_addr, (u8 *)&ts_test->self_noisedata.data[0],
                                      self_noise_data_size * sizeof(u16));
    if (ret1 < 0) {
        TPD_INFO("%s: Failed read self noise data\n", __func__);
        ts_test->self_noisedata.size = 0;
        ts_test->test_result[GTP_SELFNOISE_TEST] = SYS_SOFTWARE_REASON;
    }

    TPD_INFO("%s exit:%d\n", __func__, ret | ret1);
    return ret | ret1;
}

/**
 * gtx8_analyse_noisedata- analyse noisedata
 */
static void gtx8_analyse_noisedata(struct gtx8_ts_test *ts_test)
{
    int i = 0;
    u32 find_bad_node = 0;
    s16 noise_value = 0;

    for (i = 0; i < ts_test->noisedata.size; i++) {
        noise_value = (s16)be16_to_cpu(ts_test->noisedata.data[i]);
        ts_test->noisedata.data[i] = abs(noise_value);

        if (ts_test->noisedata.data[i] > ts_test->test_params.noise_threshold) {
            find_bad_node++;
            TPD_INFO("noise check failed: niose[%d][%d]:%u, > %u\n",
                    (u32)div_s64(i, ts_test->test_params.sen_num),
                    i % ts_test->test_params.sen_num,
                    ts_test->noisedata.data[i],
                    ts_test->test_params.noise_threshold);
        }
    }
    if (find_bad_node) {
        TPD_INFO("%s:noise test find bad node\n", __func__);
        ts_test->test_result[GTP_NOISE_TEST] = GTP_PANEL_REASON;
    } else {
        ts_test->test_result[GTP_NOISE_TEST] = GTP_TEST_PASS;
        TPD_INFO("noise test check pass\n");
    }

    return;
}

/**
 * gtx8_analyse_self_noisedata- analyse self noisedata
 */
static void gtx8_analyse_self_noisedata(struct gtx8_ts_test *ts_test)
{
    int i = 0;
    u32 self_find_bad_node = 0;
    s16 self_noise_value = 0;
    TPD_INFO("%s run\n", __func__);
    for (i = 0; i < ts_test->self_noisedata.size; i++) {
        self_noise_value = (s16)be16_to_cpu(ts_test->self_noisedata.data[i]);
        ts_test->self_noisedata.data[i] = abs(self_noise_value);

        if (ts_test->self_noisedata.data[i] > ts_test->test_params.self_noise_threshold) {
            self_find_bad_node++;
            TPD_INFO("self noise check failed: self_noise[%d][%d]:%u, > %u\n",
                    (u32)div_s64(i, ts_test->test_params.drv_num),
                    i % ts_test->test_params.drv_num,
                    ts_test->self_noisedata.data[i],
                    ts_test->test_params.self_noise_threshold);
        }
    }

    if (self_find_bad_node) {
        TPD_INFO("%s:self_noise test find bad node", __func__);
        ts_test->test_result[GTP_SELFNOISE_TEST] = GTP_PANEL_REASON;
    } else {
        ts_test->test_result[GTP_SELFNOISE_TEST] = GTP_TEST_PASS;
        TPD_INFO("self noise test check passed\n");
    }
    TPD_INFO("%s exit\n", __func__);
    return;
}


/* test noise data */
static void gtx8_test_noisedata(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    int test_cnt = 0;
    u8 buf[1];
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    TPD_INFO("%s run\n", __func__);
    ret = gtx8_noisetest_prepare(ts_test);
    if (ret < 0) {
        TPD_INFO("%s :Noisetest prepare failed\n", __func__);
        goto soft_err_out;
    }

    /* read noisedata and self_noisedata,calculate result */
    for (test_cnt = 0; test_cnt < RAWDATA_TEST_TIMES; test_cnt++) {
        ret = gtx8_cache_noisedata(ts_test);
        if (ret) {
            if (test_cnt == RAWDATA_TEST_TIMES - 1) {
                TPD_INFO("%s: Cache noisedata failed\n", __func__);
                goto soft_err_out;
            } else {
                continue;
            }
        }
        gtx8_analyse_noisedata(ts_test);

        gtx8_analyse_self_noisedata(ts_test);
        break;
    }

    TPD_INFO("%s: Noisedata and Self_noisedata test end\n", __func__);
    goto noise_test_exit;

soft_err_out:
    ts_test->noisedata.size = 0;
    ts_test->test_result[GTP_NOISE_TEST] = SYS_SOFTWARE_REASON;
    ts_test->self_noisedata.size = 0;
    ts_test->test_result[GTP_SELFNOISE_TEST] = SYS_SOFTWARE_REASON;
noise_test_exit:
    //core_data->ts_dev->hw_ops->send_cmd(core_data->ts_dev, &ts_test->normal_cmd);
    ret = touch_i2c_write_block(chip_info->client, ts_test->normal_cmd.cmd_reg,
                                ts_test->normal_cmd.length, ts_test->normal_cmd.cmds);

    buf[0] = 0x00;
    ret = touch_i2c_write_block(chip_info->client, GTP_REG_COOR, 1, buf);
    return;
}

static int gtx8_self_rawcapacitance_test(struct ts_test_self_rawdata *rawdata,
        struct ts_test_params *test_params)
{
    int i = 0;
    int ret = NO_ERR;

    for (i = 0; i < rawdata->size; i++) {
        if (rawdata->data[i] > test_params->self_max_limits[i]) {
            TPD_INFO("self_rawdata[%d][%d]:%u >self_max_limit:%u, NG\n",
                    (u32)div_s64(i, test_params->drv_num), i % test_params->drv_num,
                    rawdata->data[i], test_params->self_max_limits[i]);
            ret = RESULT_ERR;
        }

        if (rawdata->data[i] < test_params->self_min_limits[i]) {
            TPD_INFO("self_rawdata[%d][%d]:%u < min_limit:%u, NG\n",
                    (u32)div_s64(i, test_params->drv_num), i % test_params->drv_num,
                    rawdata->data[i], test_params->self_min_limits[i]);
            ret = RESULT_ERR;
        }
    }

    return ret;
}


/* gtx8_captest_prepare
 *
 * parse test peremeters from dt
 */
static int gtx8_captest_prepare(struct gtx8_ts_test *ts_test)
{
    int ret = -EINVAL;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;
    TPD_INFO("%s run\n", __func__);

    ret = goodix_send_config(chip_info, chip_info->test_cfg.data, chip_info->test_cfg.length);
    if (ret)
        TPD_INFO("Failed to send test config:%d\n", ret);
    else
        TPD_INFO("Success send test config");

    TPD_INFO("%s exit:%d\n", __func__, ret);
    return ret;
}

static int gtx8_rawcapacitance_test(struct ts_test_rawdata *rawdata,
                                    struct ts_test_params *test_params)
{
    int i = 0;
    int ret = NO_ERR;

    for (i = 0; i < rawdata->size; i++) {
        if (rawdata->data[i] > test_params->max_limits[i]) {
            TPD_INFO("rawdata[%d][%d]:%u > max_limit:%u, NG\n",
                    (u32)div_s64(i, test_params->sen_num), i % test_params->sen_num,
                    rawdata->data[i], test_params->max_limits[i]);
            ret = RESULT_ERR;
        }

        if (rawdata->data[i] < test_params->min_limits[i]) {
            TPD_INFO("rawdata[%d][%d]:%u < min_limit:%u, NG\n",
                    (u32)div_s64(i, test_params->sen_num), i % test_params->sen_num,
                    rawdata->data[i], test_params->min_limits[i]);
            ret = RESULT_ERR;
        }
    }

    return ret;
}

static int gtx8_deltacapacitance_test(struct ts_test_rawdata *rawdata,
                                      struct ts_test_params *test_params)
{
    int i = 0;
    int ret = NO_ERR;
    int cols = 0;
    u32 max_val = 0;
    u32 rawdata_val = 0;
    u32 sc_data_num = 0;
    u32 up = 0, down = 0, left = 0, right = 0;

    cols = test_params->sen_num;
    sc_data_num = test_params->drv_num * test_params->sen_num;
    if (cols <= 0) {
        TPD_INFO("%s: parmas invalid\n", __func__);
        return RESULT_ERR;
    }

    for (i = 0; i < sc_data_num; i++) {
        rawdata_val = rawdata->data[i];
        max_val = 0;
        /* calculate deltacpacitance with above node */
        if (i - cols >= 0) {
            up = rawdata->data[i - cols];
            up = abs(rawdata_val - up);
            if (up > max_val)
                max_val = up;
        }

        /* calculate deltacpacitance with bellow node */
        if (i + cols < sc_data_num) {
            down = rawdata->data[i + cols];
            down = abs(rawdata_val - down);
            if (down > max_val)
                max_val = down;
        }

        /* calculate deltacpacitance with left node */
        if (i % cols) {
            left = rawdata->data[i - 1];
            left = abs(rawdata_val - left);
            if (left > max_val)
                max_val = left;
        }

        /* calculate deltacpacitance with right node */
        if ((i + 1) % cols) {
            right = rawdata->data[i + 1];
            right = abs(rawdata_val - right);
            if (right > max_val)
                max_val = right;
        }

        /* float to integer */
        if (rawdata_val) {
            max_val *= FLOAT_AMPLIFIER;
            max_val = (u32)div_s64(max_val, rawdata_val);
            if (max_val > test_params->deviation_limits[i]) {
                TPD_INFO("deviation[%d][%d]:%u > delta_limit:%u, NG\n",
                        (u32)div_s64(i, cols), i % cols, max_val,
                        test_params->deviation_limits[i]);
                ret = RESULT_ERR;
            }
        } else {
            TPD_INFO("Find rawdata=0 when calculate deltacapacitance:[%d][%d]\n",
                    (u32)div_s64(i, cols), i % cols);
            ret = RESULT_ERR;
        }
    }

    return ret;
}

/* gtx8_rawdata_test
 * test rawdata with one frame
 */
static void gtx8_rawdata_test(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    /* read rawdata and calculate result,  statistics fail times */
    ret = gtx8_cache_rawdata(ts_test);
    if (ret < 0) {
        /* Failed read rawdata */
        TPD_INFO("Read rawdata failed\n");
        ts_test->test_result[GTP_CAP_TEST] = SYS_SOFTWARE_REASON;
        ts_test->test_result[GTP_DELTA_TEST] = SYS_SOFTWARE_REASON;
    } else {
        ret = gtx8_rawcapacitance_test(&ts_test->rawdata, &ts_test->test_params);
        if (!ret) {
            ts_test->test_result[GTP_CAP_TEST] = GTP_TEST_PASS;
            TPD_INFO("Rawdata test pass\n");
        } else {
            ts_test->test_result[GTP_CAP_TEST] = GTP_PANEL_REASON;
            TPD_INFO("RawCap test failed\n");
        }

        ret = gtx8_deltacapacitance_test(&ts_test->rawdata, &ts_test->test_params);
        if (!ret)  {
            ts_test->test_result[GTP_DELTA_TEST] = GTP_TEST_PASS;
            TPD_INFO("DeltaCap test pass\n");
        } else {
            ts_test->test_result[GTP_DELTA_TEST] = GTP_PANEL_REASON;
            TPD_INFO("DeltaCap test failed\n");
        }
    }
}

static void gtx8_capacitance_test(struct gtx8_ts_test *ts_test)
{
    int ret = 0;
    u8 buf[1];
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    TPD_INFO("%s run\n", __func__);
    ret = gtx8_captest_prepare(ts_test);
    if (ret) {
        TPD_INFO("Captest prepare failed\n");
        ts_test->test_result[GTP_CAP_TEST] = SYS_SOFTWARE_REASON;
        ts_test->test_result[GTP_DELTA_TEST] = SYS_SOFTWARE_REASON;
        ts_test->test_result[GTP_SELFCAP_TEST] = SYS_SOFTWARE_REASON;
        return;
    }
    /* read rawdata and calculate result,  statistics fail times */
    ret = touch_i2c_write_block(chip_info->client, ts_test->rawdata_cmd.cmd_reg,
                                ts_test->rawdata_cmd.length, ts_test->rawdata_cmd.cmds);
    if (ret < 0) {
        TPD_INFO("%s:Failed send rawdata cmd:ret%d\n", __func__, ret);
        goto capac_test_exit;
    } else {
        TPD_INFO("%s: Success change to rawdata mode\n", __func__);
    }
    msleep(50);

    gtx8_rawdata_test(ts_test);

    /* read selfrawdata and calculate result,  statistics fail times */
    ret = gtx8_cache_self_rawdata(ts_test);
    if (ret < 0) {
        /* Failed read selfrawdata */
        TPD_INFO("Read selfrawdata failed\n");
        ts_test->test_result[GTP_SELFCAP_TEST] = SYS_SOFTWARE_REASON;
        goto capac_test_exit;
    } else {
        ret = gtx8_self_rawcapacitance_test(&ts_test->self_rawdata, &ts_test->test_params);
        if (!ret) {
            ts_test->test_result[GTP_SELFCAP_TEST] = GTP_TEST_PASS;
            TPD_INFO("selfrawdata test pass\n");
        } else {
            ts_test->test_result[GTP_SELFCAP_TEST] = GTP_PANEL_REASON;
            TPD_INFO("selfrawCap test failed\n");
        }
    }
capac_test_exit:
    ret = touch_i2c_write_block(chip_info->client, ts_test->normal_cmd.cmd_reg,
                                ts_test->normal_cmd.length, ts_test->normal_cmd.cmds);

    buf[0] = 0x00;
    ret = touch_i2c_write_block(chip_info->client, GTP_REG_COOR, 1, buf);
    return;
}

static void gtx8_intgpio_test(struct gtx8_ts_test *p_gtx8_ts_test)
{
    /*for interrupt pin test*/
    int eint_status = 0, eint_count = 0, read_gpio_num = 0;//not test int pin

    TPD_INFO("%s, step 0: begin to check INT-GND short item\n", __func__);

    while (read_gpio_num--) {
        msleep(5);
        eint_status = gpio_get_value(p_gtx8_ts_test->p_testdata->irq_gpio);
        if (eint_status == 1) {
            eint_count--;
        } else {
            eint_count++;
        }
        TPD_INFO("%s eint_count = %d  eint_status = %d\n", __func__, eint_count, eint_status);
    }
    TPD_INFO("TP EINT PIN %s direct short! eint_count = %d\n", eint_count == 10 ? "is" : "not", eint_count);
    if (eint_count == 10) {
        TPD_INFO("error :  TP EINT PIN direct short!\n");
        seq_printf(p_gtx8_ts_test->p_seq_file, "eint_status is low, TP EINT direct stort\n");
        eint_count = 0;
        p_gtx8_ts_test->error_count ++;
        p_gtx8_ts_test->test_result[GTP_INTPIN_TEST] = GTP_PANEL_REASON;
    }
    p_gtx8_ts_test->test_result[GTP_INTPIN_TEST] = GTP_TEST_PASS;
}


/* short test */
static int gtx8_short_test_prepare(struct gtx8_ts_test *ts_test)
{
    int ret = 0, i = 0, retry = GTX8_RETRY_NUM_3;
    u8 data[MAX_DRV_NUM + MAX_SEN_NUM] = {0};
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;
    u8 short_cmd[3];
    u16 short_cmd_reg = 0;

    short_cmd[0] = GTP_CMD_SHORT_TEST;
    short_cmd[1] = 0x00;
    short_cmd[2] = (u8)((0 - short_cmd[0] - short_cmd[1]) & 0xff);
    short_cmd_reg = chip_info->reg_info.GTP_REG_CMD;

    TPD_INFO("Short test prepare+\n");
    while (--retry) {
        /* switch to shrot test system */
        ret = gt9886_i2c_write(chip_info->client, short_cmd_reg, 3, short_cmd);
        if (ret < 0) {
            TPD_INFO("Can not switch to short test system\n");
            return ret;
        }
        msleep(50);

        /* check firmware running */
        for (i = 0; i < 20; i++) {
            TPD_INFO("Check firmware running..");
            ret = touch_i2c_read_block(chip_info->client, SHORT_STATUS_REG, 1, &data[0]);
            if (ret < 0) {
                TPD_INFO("Check firmware running failed\n");
                return ret;
            } else if (data[0] == 0xaa) {
                TPD_INFO("Short firmware is running\n");
                break;
            }
            msleep(10);
        }
        if (i < 20) {
            break;
        } else {
            goodix_reset_via_gpio(chip_info);
            //    if (goodix_set_i2c_doze_mode(core_data->ts_dev,false))
            //        ts_info("WRNING: doze may enabled after reset\n");
        }
    }
    if (retry <= 0) {
        ret = -EINVAL;
        TPD_INFO("Switch to short test mode timeout\n");
        return ret;
    }

    data[0] = 0;
    /* turn off watch dog timer */
    ret = touch_i2c_write_block(chip_info->client, WATCH_DOG_TIMER_REG, 1, data);
    if (ret < 0) {
        TPD_INFO("Failed turn off watch dog timer\n");
        return ret;
    }

    TPD_INFO("Firmware in short test mode\n");

    data[0] = (ts_test->test_params.short_threshold >> 8) & 0xff;
    data[1] = ts_test->test_params.short_threshold & 0xff;

    /* write tx/tx, tx/rx, rx/rx short threshold value to 0x8408 */
    ret = touch_i2c_write_block(chip_info->client, TXRX_THRESHOLD_REG, 2, data);
    if (ret < 0) {
        TPD_INFO("Failed write tx/tx, tx/rx, rx/rx short threshold value\n");
        return ret;
    }
    data[0] = (GNDAVDD_SHORT_VALUE >> 8) & 0xff;
    data[1] = GNDAVDD_SHORT_VALUE & 0xff;
    /* write default txrx/gndavdd short threshold value 16 to 0x804A*/
    ret = touch_i2c_write_block(chip_info->client, GNDVDD_THRESHOLD_REG, 2, data);
    if (ret < 0) {
        TPD_INFO("Failed write txrx/gndavdd short threshold value\n");
        return ret;
    }

    /* Write ADC dump data num to 0x840c */
    data[0] = (ADC_DUMP_NUM >> 8) & 0xff;
    data[1] = ADC_DUMP_NUM & 0xff;
    ret = touch_i2c_write_block(chip_info->client, ADC_DUMP_NUM_REG, 2, data);
    if (ret < 0) {
        TPD_INFO("Failed write ADC dump data number\n");
        return ret;
    }

    /* write 0x01 to 0x5095 start short test */
    data[0] = 0x01;
    ret = touch_i2c_write_block(chip_info->client, SHORT_STATUS_REG, 1, data);
    if (ret < 0) {
        TPD_INFO("Failed write running dsp reg\n");
        return ret;
    }

    TPD_INFO("Short test prepare-\n");
    return 0;
}

static u32 map_die2pin(struct ts_test_params *test_params, u32 chn_num)
{
    int i = 0;
    u32 res = 255;

    if (chn_num & DRV_CHANNEL_FLAG)
        chn_num = (chn_num & ~DRV_CHANNEL_FLAG) + test_params->max_sen_num;

    for (i = 0; i < test_params->max_sen_num; i++) {
        if (test_params->sen_map[i] == chn_num) {
            res = i;
            break;
        }
    }

    /* res != 255 mean found the corresponding channel num */
    if (res != 255)
        return res;

    /* if cannot find in SenMap try find in DrvMap */
    for (i = 0; i < test_params->max_drv_num; i++) {
        if (test_params->drv_map[i] == chn_num) {
            res = i;
            break;
        }
    }
    if (i >= test_params->max_drv_num)
        TPD_INFO("Faild found corrresponding channel num:%d\n", chn_num);
    else
        res |= DRV_CHANNEL_FLAG;

    return res;
}

static int gtx8_check_resistance_to_gnd(struct ts_test_params *test_params,
                                        u16 adc_signal, u32 pos)
{
    long r = 0;
    u16 r_th = 0, avdd_value = 0;
    u32 chn_id_tmp = 0;
    u32 pin_num = 0;

    avdd_value = test_params->avdd_value;
    if (adc_signal == 0 || adc_signal == 0x8000)
        adc_signal |= 1;

    if ((adc_signal & 0x8000) == 0)    /* short to GND */
        r = SHORT_TO_GND_RESISTER(adc_signal);
    else    /* short to VDD */
        r = SHORT_TO_VDD_RESISTER(adc_signal, avdd_value);

    r = (long)div_s64(r, 100);
    r = r > MAX_U16_VALUE ? MAX_U16_VALUE : r;
    r = r < 0 ? 0 : r;

    if (pos < MAX_DRV_NUM)
        r_th = test_params->r_drv_gnd_threshold;
    else
        r_th = test_params->r_sen_gnd_threshold;

    chn_id_tmp = pos;
    if (chn_id_tmp < test_params->max_drv_num)
        chn_id_tmp |= DRV_CHANNEL_FLAG;
    else
        chn_id_tmp -= test_params->max_drv_num;

    if (r < r_th) {
        pin_num = map_die2pin(test_params, chn_id_tmp);
        TPD_INFO("%s%d shortcircut to %s,R=%ldK,R_Threshold=%dK\n",
                (pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
                (pin_num & ~DRV_CHANNEL_FLAG),
                (adc_signal & 0x8000) ? "VDD" : "GND",
                r, r_th);

        return RESULT_ERR;
    }
    return NO_ERR;
}

static u32 gtx8_short_resistance_calc(struct gtx8_ts_test *ts_test,
                                      struct short_record *r_data, u16 self_capdata, u8 flag)
{
    u16 lineDrvNum = 0, lineSenNum = 0;
    u8 DieNumber1 = 0, DieNumber2 = 0;
    long r = 0;

    lineDrvNum = ts_test->test_params.max_drv_num;
    lineSenNum = ts_test->test_params.max_sen_num;

    if (flag == 0) {
        if (r_data->group1 != r_data->group2) {    /* different Group */
            r = div_s64(self_capdata * 81 * FLOAT_AMPLIFIER, r_data->short_code);
            r -= (81 * FLOAT_AMPLIFIER);
        } else {
            DieNumber1 = ((r_data->master & 0x80) == 0x80) ? (r_data->master + lineSenNum) : r_data->master;
            DieNumber2 = ((r_data->slave & 0x80) == 0x80) ? (r_data->slave + lineSenNum) : r_data->slave;
            DieNumber1 = (DieNumber1 >= DieNumber2) ? (DieNumber1 - DieNumber2) : (DieNumber2 - DieNumber1);
            if ((DieNumber1 > 3) && (r_data->group1 == 0)) {
                r = div_s64(self_capdata * 81 * FLOAT_AMPLIFIER, r_data->short_code);
                r -= (81 * FLOAT_AMPLIFIER);
            } else {
                r = div_s64(self_capdata * 64 * FLOAT_AMPLIFIER, r_data->short_code);
                r -= (64 * FLOAT_AMPLIFIER);
            }
        }
    } else {
        r = div_s64(self_capdata * 81 * FLOAT_AMPLIFIER, r_data->short_code);
        r -= (81 * FLOAT_AMPLIFIER);
    }

    /*if (r < 6553)
        r *= 10;
    else
        r = 65535;*/
    r = (long)div_s64(r, FLOAT_AMPLIFIER);
    r = r > MAX_U16_VALUE ? MAX_U16_VALUE : r;

    return r >= 0 ? r : 0;
}

static int gtx8_shortcircut_analysis(struct gtx8_ts_test *ts_test)
{
    int ret = 0, err = 0;
    u32 r_threshold = 0, short_r = 0;
    int size = 0, i = 0, j = 0;
    u32 master_pin_num, slave_pin_num;
    u16 adc_signal = 0, data_addr = 0;
    u8 short_flag = 0, *data_buf = NULL, short_status[3] = {0};
    u16 self_capdata[MAX_DRV_NUM + MAX_SEN_NUM] = {0}, short_die_num = 0;
    struct short_record temp_short_info;
    struct ts_test_params *test_params = &ts_test->test_params;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;

    ret = touch_i2c_read_block(chip_info->client, TEST_RESTLT_REG, 1, &short_flag);
    if (ret < 0) {
        TPD_INFO("Read TEST_TESULT_REG falied\n");
        goto shortcircut_analysis_error;
    } else if ((short_flag & 0x0F) == 0x00) {
        TPD_INFO("No shortcircut\n");
        return NO_ERR;
    }
    TPD_INFO("short test flag:0x%x\n", short_flag);

    data_buf = kzalloc((MAX_DRV_NUM + MAX_SEN_NUM) * 2, GFP_KERNEL);
    if (!data_buf) {
        TPD_INFO("Failed to alloc memory\n");
        goto shortcircut_analysis_error;
    }

    /* shortcircut to gnd&vdd */
    if (short_flag & 0x08) {
        /* read diff code, diff code will be used to calculate
          * resistance between channel and GND */
        size = (MAX_DRV_NUM + MAX_SEN_NUM) * 2;
        ret = touch_i2c_read_block(chip_info->client, DIFF_CODE_REG, size, data_buf);
        if (ret < 0) {
            TPD_INFO("Failed read to-gnd rawdata\n");
            goto shortcircut_analysis_error;
        }
        for (i = 0; i < size; i += 2) {
            adc_signal = be16_to_cpup((__be16 *)&data_buf[i]);
            ret = gtx8_check_resistance_to_gnd(test_params,
                                               adc_signal, i >> 1); /* i >> 1 = i / 2 */
            if (ret) {
                TPD_INFO("Resistance to-gnd test failed\n");
                err |= ret;
            }
        }
    }

    /* read self-capdata+ */
    size = (MAX_DRV_NUM + MAX_SEN_NUM) * 2;
    ret = touch_i2c_read_block(chip_info->client, DRV_SELF_CODE_REG, size, data_buf);
    if (ret < 0) {
        TPD_INFO("Failed read selfcap rawdata\n");
        goto shortcircut_analysis_error;
    }
    for (i = 0; i < MAX_DRV_NUM + MAX_SEN_NUM; i++)
        self_capdata[i] = be16_to_cpup((__be16 *)&data_buf[i * 2]) & 0x7fff;
    /* read self-capdata- */

    /* read tx tx short number
    **   short_status[0]: tr tx
    **   short_status[1]: tr rx
    **   short_status[2]: rx rx
    */
    ret = touch_i2c_read_block(chip_info->client, TX_SHORT_NUM, 3, &short_status[0]);
    if (ret < 0) {
        TPD_INFO("Failed read tx-to-tx short rawdata\n");
        goto shortcircut_analysis_error;
    }
    TPD_INFO("Tx&Tx:%d,Rx&Rx:%d,Tx&Rx:%d\n", short_status[0], short_status[1], short_status[2]);

    /* drv&drv shortcircut check */
    data_addr = 0x8460;
    for (i = 0; i < short_status[0]; i++) {
        size = SHORT_CAL_SIZE(MAX_DRV_NUM);    /* 4 + MAX_DRV_NUM * 2 + 2; */
        ret = touch_i2c_read_block(chip_info->client, data_addr, size, data_buf);
        if (ret < 0) {
            TPD_INFO("Failed read drv-to-drv short rawdata\n");
            goto shortcircut_analysis_error;
        }

        r_threshold = test_params->r_drv_drv_threshold;
        short_die_num = be16_to_cpup((__be16 *)&data_buf[0]);
        if (short_die_num > MAX_DRV_NUM + MAX_SEN_NUM ||
            short_die_num < MAX_SEN_NUM) {
            TPD_INFO("invalid short pad num:%d\n", short_die_num);
            continue;
        }

        /* TODO: j start position need recheck */
        short_die_num -= test_params->max_sen_num;
        for (j = short_die_num + 1; j < MAX_DRV_NUM; j++) {
            adc_signal = be16_to_cpup((__be16 *)&data_buf[4 + j * 2]);

            if (adc_signal > test_params->short_threshold) {
                temp_short_info.master = short_die_num | DRV_CHANNEL_FLAG;
                temp_short_info.slave = j | DRV_CHANNEL_FLAG;
                temp_short_info.short_code = adc_signal;
                gtx8_check_setting_group(ts_test, &temp_short_info);

                if (self_capdata[short_die_num] == 0xffff ||
                    self_capdata[short_die_num] == 0) {
                    TPD_INFO("invalid self_capdata:0x%x\n", self_capdata[short_die_num]);
                    continue;
                }

                short_r = gtx8_short_resistance_calc(ts_test, &temp_short_info,
                                                     self_capdata[short_die_num], 0);
                if (short_r < r_threshold) {
                    master_pin_num = map_die2pin(test_params, temp_short_info.master);
                    slave_pin_num = map_die2pin(test_params, temp_short_info.slave);
                    TPD_INFO("Tx/Tx short circut:R=%dK,R_Threshold=%dK\n",
                            short_r, r_threshold);
                    TPD_INFO("%s%d--%s%d shortcircut\n",
                            (master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
                            (master_pin_num & ~DRV_CHANNEL_FLAG),
                            (slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
                            (slave_pin_num & ~DRV_CHANNEL_FLAG));
                    err |= -EINVAL;
                }
            }
        }
        data_addr += size;
    }

    /* sen&sen shortcircut check */
    data_addr = 0x91d0;
    for (i = 0; i < short_status[1]; i++) {
        size =   SHORT_CAL_SIZE(MAX_SEN_NUM);     /* 4 + MAX_SEN_NUM * 2 + 2; */
        ret = touch_i2c_read_block(chip_info->client, data_addr, size, data_buf);
        if (ret < 0) {
            TPD_INFO("Failed read sen-to-sen short rawdata\n");
            goto shortcircut_analysis_error;
        }

        r_threshold = ts_test->test_params.r_sen_sen_threshold;
        short_die_num = be16_to_cpup((__be16 *)&data_buf[0]);
        if (short_die_num > MAX_SEN_NUM)
            continue;

        for (j = short_die_num + 1; j < MAX_SEN_NUM; j++) {
            adc_signal = be16_to_cpup((__be16 *)&data_buf[4 + j * 2]);
            if (adc_signal > ts_test->test_params.short_threshold) {
                temp_short_info.master = short_die_num;
                temp_short_info.slave = j;
                temp_short_info.short_code = adc_signal;
                gtx8_check_setting_group(ts_test, &temp_short_info);

                if (self_capdata[short_die_num + test_params->max_drv_num] == 0xffff ||
                    self_capdata[short_die_num + test_params->max_drv_num] == 0) {
                    TPD_INFO("invalid self_capdata:0x%x\n",
                             self_capdata[short_die_num + test_params->max_drv_num]);
                    continue;
                }

                short_r = gtx8_short_resistance_calc(ts_test, &temp_short_info,
                                                     self_capdata[short_die_num + test_params->max_drv_num], 0);
                if (short_r < r_threshold) {
                    master_pin_num = map_die2pin(test_params, temp_short_info.master);
                    slave_pin_num = map_die2pin(test_params, temp_short_info.slave);
                    TPD_INFO("Rx/Rx short circut:R=%dK,R_Threshold=%dK\n",
                            short_r, r_threshold);
                    TPD_INFO("%s%d--%s%d shortcircut\n",
                            (master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
                            (master_pin_num & ~DRV_CHANNEL_FLAG),
                            (slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
                            (slave_pin_num & ~DRV_CHANNEL_FLAG));
                    err |= -EINVAL;
                }
            }
        }
        data_addr += size;
    }

    /* sen&drv shortcircut check */
    data_addr = 0x9cc8;
    for (i = 0; i < short_status[2]; i++) {
        size =  SHORT_CAL_SIZE(MAX_DRV_NUM);                        /* size = 4 + MAX_SEN_NUM * 2 + 2; */
        ret = touch_i2c_read_block(chip_info->client, data_addr, size, data_buf);
        if (ret < 0) {
            TPD_INFO("Failed read sen-to-drv short rawdata\n");
            goto shortcircut_analysis_error;
        }

        r_threshold = ts_test->test_params.r_drv_sen_threshold;
        short_die_num = be16_to_cpup((__be16 *)&data_buf[0]);
        if (short_die_num > MAX_SEN_NUM)
            continue;

        for (j = 0; j < MAX_DRV_NUM; j++) {
            adc_signal = be16_to_cpup((__be16 *)&data_buf[4 + j * 2]);
            if (adc_signal > ts_test->test_params.short_threshold) {
                temp_short_info.master = short_die_num;
                temp_short_info.slave = j | DRV_CHANNEL_FLAG;
                temp_short_info.short_code = adc_signal;
                gtx8_check_setting_group(ts_test, &temp_short_info);

                if (self_capdata[short_die_num + test_params->max_drv_num] == 0xffff ||
                    self_capdata[short_die_num + test_params->max_drv_num] == 0) {
                    TPD_INFO("invalid self_capdata:0x%x\n",
                             self_capdata[short_die_num + test_params->max_drv_num]);
                    continue;
                }

                short_r = gtx8_short_resistance_calc(ts_test, &temp_short_info,
                                                     self_capdata[short_die_num + test_params->max_drv_num], 0);
                if (short_r < r_threshold) {
                    master_pin_num = map_die2pin(test_params, temp_short_info.master);
                    slave_pin_num = map_die2pin(test_params, temp_short_info.slave);
                    TPD_INFO("Rx/Tx short circut:R=%dK,R_Threshold=%dK\n",
                            short_r, r_threshold);
                    TPD_INFO("%s%d--%s%d shortcircut\n",
                            (master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
                            (master_pin_num & ~DRV_CHANNEL_FLAG),
                            (slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
                            (slave_pin_num & ~DRV_CHANNEL_FLAG));
                    err |= -EINVAL;
                }
            }
        }
        data_addr += size;
    }

    if (data_buf) {
        kfree(data_buf);
        data_buf = NULL;
    }

    TPD_INFO("%s exit:%d\n", __func__, err);
    return err ? -EFAULT :  NO_ERR;
shortcircut_analysis_error:
    if (data_buf != NULL) {
        kfree(data_buf);
        data_buf = NULL;
    }
    TPD_INFO("%s exit with some error\n", __func__);
    return -EINVAL;
}

static void gtx8_shortcircut_test(struct gtx8_ts_test *ts_test)
{
    int i = 0;
    int ret = 0;
    u8 data[2] = {0};
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)ts_test->ts;
    struct ts_test_params *test_params = &ts_test->test_params;
    int N = test_params->drv_num;
    int M = test_params->sen_num;
    int wait_time = 0;

    wait_time = ((N * (N + 1)) + (M * (M + 1))) / 2 + M * N + N + M;
    wait_time = (int)(86 * wait_time /100 + 100);
    TPD_INFO("wait_time:%d(ms),tx:%d,rx:%d\n", wait_time, test_params->drv_num, test_params->sen_num);

    ts_test->test_result[GTP_SHORT_TEST] = GTP_TEST_PASS;
    ret = gtx8_short_test_prepare(ts_test);
    if (ret < 0) {
        TPD_INFO("Failed enter short test mode\n");
        ts_test->test_result[GTP_SHORT_TEST] = SYS_SOFTWARE_REASON;
        return;
    }
    msleep(wait_time);
    for (i = 0; i < 150; i++) {
        msleep(50);
        TPD_INFO("waitting for short test end...:retry=%d\n", i);
        ret = touch_i2c_read_block(chip_info->client, SHORT_TESTEND_REG, 1, data);
        if (ret < 0 )
            TPD_INFO("Failed get short test result: retry%d\n", i);
        else if (data[0] == 0x88)  /* test ok*/
            break;
    }

    if (i < 150) {
        ret = gtx8_shortcircut_analysis(ts_test);
        if (ret) {
            ts_test->test_result[GTP_SHORT_TEST] = GTP_PANEL_REASON;
            TPD_INFO("Short test failed\n");
        } else {
            TPD_INFO("Short test success\n");
        }
    } else {
        TPD_INFO("Wait short test finish timeout:reg_val=0x%x\n", data[0]);
        ts_test->test_result[GTP_SHORT_TEST] = SYS_SOFTWARE_REASON;
        for (i = 0; i < 8 ; i++) {
            udelay(1000);
            data[0] = 0x00;
            ret = touch_i2c_read_block(chip_info->client,0x2244,2,data);
            if(ret < 0){
                TPD_INFO("short test,read 0x2244 failed:%d\n",ret);
                continue;
            }
            TPD_INFO("short test,i:%d,0x2244:0x%x,0x%x\n",i,data[0],data[1]);
        }
    }
    return;
}


static void gtx8_check_setting_group(struct gtx8_ts_test *ts_test, struct short_record *r_data)
{
    u32 dMaster = 0;
    u32 dSlave = 0;

    if (r_data->master & 0x80)
        dMaster = ts_test->test_params.max_sen_num;

    if (r_data->slave & 0x80)
        dSlave = ts_test->test_params.max_sen_num;

    dMaster += (r_data->master & 0x7f);
    dSlave += (r_data->slave & 0x7f);

    if ((dMaster >= 0) && (dMaster < 9))   /* pad s0~s8 */
        r_data->group1 = 5;

    else if ((dMaster >= 9) && (dMaster < 14))   /* pad s9~s13 */
        r_data->group1 = 4;

    else if ((dMaster >= 14) && (dMaster < 18))  /* pad s14~s17 */
        r_data->group1 = 3;

    else if ((dMaster >= 18) && (dMaster < 27))  /* pad s18~s26 */
        r_data->group1 = 2;

    else if ((dMaster >= 27) && (dMaster < 32))  /* pad s27~s31 */
        r_data->group1 = 1;

    else if ((dMaster >= 32) && (dMaster < 36))  /* pad s32~s35 */
        r_data->group1 = 0;

    else if ((dMaster >= 36) && (dMaster < 45))  /* pad d0~d8 */
        r_data->group1 = 5;

    else if ((dMaster >= 45) && (dMaster < 54))  /* pad d9~d17 */
        r_data->group1 = 2;

    else if ((dMaster >= 54) && (dMaster < 59))  /* pad d18~d22 */
        r_data->group1 = 1;

    else if ((dMaster >= 59) && (dMaster < 63))  /*  pad d23~d26 */
        r_data->group1 = 0;

    else if ((dMaster >= 63) && (dMaster < 67))  /* pad d27~d30 */
        r_data->group1 = 3;

    else if ((dMaster >= 67) && (dMaster < 72))  /* pad d31~d35 */
        r_data->group1 = 4;

    else if ((dMaster >= 72) && (dMaster < 76))  /* pad d36~d39 */
        r_data->group1 = 0;


    if ((dSlave > 0) && (dSlave < 9))   /* pad s0~s8 */
        r_data->group2 = 5;

    else if ((dSlave >= 9) && (dSlave < 14))   /* pad s9~s13 */
        r_data->group2 = 4;

    else if ((dSlave >= 14) && (dSlave < 18))  /* pad s14~s17 */
        r_data->group2 = 3;

    else if ((dSlave >= 18) && (dSlave < 27))  /* pad s18~s26 */
        r_data->group2 = 2;

    else if ((dSlave >= 27) && (dSlave < 32))  /* pad s27~s31 */
        r_data->group2 = 1;

    else if ((dSlave >= 32) && (dSlave < 36))  /* pad s32~s35 */
        r_data->group2 = 0;

    else if ((dSlave >= 36) && (dSlave < 45))  /* pad d0~d8 */
        r_data->group2 = 5;

    else if ((dSlave >= 45) && (dSlave < 54))  /* pad d9~d17 */
        r_data->group2 = 2;

    else if ((dSlave >= 54) && (dSlave < 59))  /* pad d18~d22 */
        r_data->group2 = 1;

    else if ((dSlave >= 59) && (dSlave < 63))  /* pad d23~d26 */
        r_data->group2 = 0;

    else if ((dSlave >= 63) && (dSlave < 67))  /* pad d27~d30 */
        r_data->group2 = 3;

    else if ((dSlave >= 67) && (dSlave < 72))  /* pad d31~d35 */
        r_data->group2 = 4;

    else if ((dSlave >= 72) && (dSlave < 76))  /* pad d36~d39 */
        r_data->group2 = 0;
}

static void gtx8_put_test_result(
    struct gtx8_ts_test *ts_test)
{
    uint8_t  data_buf[64];
    struct goodix_testdata *p_testdata;
    int i;
    /*save test fail result*/
    struct timespec now_time;
    struct rtc_time rtc_now_time;
    mm_segment_t old_fs;
    uint8_t file_data_buf[128];
    struct file *filp;

    p_testdata = ts_test->p_testdata;

    for (i = 0; i < MAX_TEST_ITEMS; i++) {
        /* if have tested, show result */
        if (ts_test->test_result[i]) {
            if (GTP_TEST_PASS == ts_test->test_result[i]) {
            } else if(GTP_PANEL_REASON == ts_test->test_result[i]) {
                ts_test->error_count ++;
            } else if(SYS_SOFTWARE_REASON == ts_test->test_result[i]) {
                ts_test->error_count ++;
            }
        }
        TPD_INFO("test_result_info %s[%d]%d\n", test_item_name[i], i, ts_test->test_result[i]);
    }
    //step2: create a file to store test data in /sdcard/Tp_Test
    getnstimeofday(&now_time);
    rtc_time_to_tm(now_time.tv_sec, &rtc_now_time);
    //if test fail,save result to path:/sdcard/TpTestReport/screenOn/NG/
    if(ts_test->error_count) {
        snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOn/NG/tp_testlimit_%02d%02d%02d-%02d%02d%02d-fail-utc.csv",
                 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);
    } else {
        snprintf(file_data_buf, 128, "/sdcard/TpTestReport/screenOn/OK/tp_testlimit_%02d%02d%02d-%02d%02d%02d-pass-utc.csv",
                 (rtc_now_time.tm_year + 1900) % 100, rtc_now_time.tm_mon + 1, rtc_now_time.tm_mday,
                 rtc_now_time.tm_hour, rtc_now_time.tm_min, rtc_now_time.tm_sec);

    }
    old_fs = get_fs();
    set_fs(KERNEL_DS);
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
    ksys_mkdir("/sdcard/TpTestReport", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
    ksys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
#else
    sys_mkdir("/sdcard/TpTestReport", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/NG", 0666);
    sys_mkdir("/sdcard/TpTestReport/screenOn/OK", 0666);
#endif
    filp = filp_open(file_data_buf, O_RDWR | O_CREAT, 0644);
    if (IS_ERR(filp)) {
        TPD_INFO("Open log file '%s' failed. %ld\n", file_data_buf, PTR_ERR(filp));
        set_fs(old_fs);
        /*add for *#899# test for it can not acess sdcard*/
    }
    p_testdata->fp = filp;
    p_testdata->pos = 0;

    memset(data_buf, 0, sizeof(data_buf));
    if (ts_test->rawdata.size) {
        if (!IS_ERR_OR_NULL(p_testdata->fp)) {
            snprintf(data_buf, 64, "%s\n", "[RAW DATA]");
            vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
        }
        for (i = 0; i < ts_test->rawdata.size; i++) {
            if (!IS_ERR_OR_NULL(p_testdata->fp)) {
                snprintf(data_buf, 64, "%d,", ts_test->rawdata.data[i]);
                vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
                if (!((i + 1) % p_testdata->RX_NUM) && (i != 0)) {
                    snprintf(data_buf, 64, "\n");
                    vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
                }
            }
        }
    }
    if (ts_test->noisedata.size) {
        if (!IS_ERR_OR_NULL(p_testdata->fp)) {
            snprintf(data_buf, 64, "\n%s\n", "[NOISE DATA]");
            vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
        }
        for (i = 0; i < ts_test->noisedata.size; i++) {
            if (!IS_ERR_OR_NULL(p_testdata->fp)) {
                sprintf(data_buf, "%d,", ts_test->noisedata.data[i]);
                vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
                if (!((i + 1) % p_testdata->RX_NUM) && (i != 0)) {
                    snprintf(data_buf, 64, "\n");
                    vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
                }
            }
        }
    }

    if (ts_test->self_noisedata.size) {
        if (!IS_ERR_OR_NULL(p_testdata->fp)) {
            snprintf(data_buf, 64, "\n%s\n", "[SELF NOISE DATA]");
            vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
        }
        for (i = 0; i < ts_test->self_noisedata.size; i++) {
            if (!IS_ERR_OR_NULL(p_testdata->fp)) {
                sprintf(data_buf, "%d,", ts_test->self_noisedata.data[i]);
                vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
            }
        }
        if (!IS_ERR_OR_NULL(p_testdata->fp)) {
            sprintf(data_buf, "\n");
            vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
        }
    }
    if (ts_test->self_rawdata.size) {
        if (!IS_ERR_OR_NULL(p_testdata->fp)) {
            snprintf(data_buf, 64, "\n%s\n", "[SELF RAW DATA]");
            vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
        }
        for (i = 0; i < ts_test->self_rawdata.size; i++) {
            if (!IS_ERR_OR_NULL(p_testdata->fp)) {
                sprintf(data_buf, "%d,", ts_test->self_rawdata.data[i]);
                vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
            }
        }
        if (!IS_ERR_OR_NULL(p_testdata->fp)) {
            sprintf(data_buf, "\n");
            vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
        }
    }

    if (!IS_ERR_OR_NULL(p_testdata->fp)) {
        snprintf(data_buf, 64, "TX:%d,RX:%d\n", p_testdata->TX_NUM, p_testdata->RX_NUM);
        vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
    }

    if (!IS_ERR_OR_NULL(p_testdata->fp)) {
        snprintf(data_buf, 64, "Img version:%lld,device version:%lld\n", p_testdata->TP_FW, ts_test->device_tp_fw);
        vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
    }

    for (i = 0; i < MAX_TEST_ITEMS; i++) {
        /* if have tested, show result */
        if (ts_test->test_result[i]) {
            if (GTP_TEST_PASS == ts_test->test_result[i]) {
                if (!IS_ERR_OR_NULL(p_testdata->fp)) {
                    snprintf(data_buf, 64, "\n%s %d:%s\n", test_item_name[i], i, "pass");
                    vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
                }
            } else if(GTP_PANEL_REASON == ts_test->test_result[i]) {
                if (!IS_ERR_OR_NULL(p_testdata->fp)) {
                    snprintf(data_buf, 64, "\n%s %d:%s\n", test_item_name[i], i, "NG:PANEL REASON");
                    vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
                     seq_printf(ts_test->p_seq_file, "%s", data_buf);
                }
            } else if(SYS_SOFTWARE_REASON == ts_test->test_result[i]) {
                if (!IS_ERR_OR_NULL(p_testdata->fp)) {
                    snprintf(data_buf, 64, "\n%s %d:%s\n", test_item_name[i], i, "NG:SOFTWARE_REASON");
                    vfs_write(p_testdata->fp, data_buf, strlen(data_buf), &p_testdata->pos);
                     seq_printf(ts_test->p_seq_file, "%s", data_buf);
                }
            }
        }
    }

    if (!IS_ERR_OR_NULL(p_testdata->fp)) {
        filp_close(p_testdata->fp, NULL);
        set_fs(old_fs);
    }
    TPD_INFO("%s exit\n", __func__);
    return;
}


static void goodix_auto_test(struct seq_file *s, void *chip_data, struct goodix_testdata *p_testdata)
{
    int ret = 0;
    struct gtx8_ts_test *gts_test = NULL;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;
    u16 *p_noisedata = NULL;
    u16 *p_rawdata = NULL;
    u16 *p_self_rawdata = NULL;
    u16 *p_self_noisedata = NULL;
    u32 fw_ver = 0;
    u8 cfg_ver = 0;

    TPD_INFO("%s: enter\n", __func__);

    if (!chip_data) {
        TPD_INFO("%s: chip_data is NULL\n", __func__);
        return;
    }
    if (!p_testdata) {
        TPD_INFO("%s: goodix_testdata is null\n", __func__);
        return;
    }

    gts_test = kzalloc(sizeof(struct gtx8_ts_test), GFP_KERNEL);
    if (!gts_test) {
        TPD_INFO("%s: goodix_testdata is null\n", __func__);
        seq_printf(s, "1 error(s).\n");
        return;
    }

    p_rawdata = kzalloc(sizeof(u16) * (p_testdata->TX_NUM * p_testdata->RX_NUM), GFP_KERNEL);
    if (!p_rawdata) {
        TPD_INFO("Failed to alloc rawdata info memory\n");
        gts_test->error_count ++;
        ret = -1;
        goto exit_finish;
    }
    p_noisedata = kzalloc(sizeof(u16) * (p_testdata->TX_NUM * p_testdata->RX_NUM), GFP_KERNEL);
    if (!p_noisedata) {
        TPD_INFO("Failed to alloc rawdata info memory\n");
        gts_test->error_count ++;
        ret = -1;
        goto exit_finish;
    }

    p_self_rawdata = kzalloc(sizeof(u16) * (p_testdata->TX_NUM + p_testdata->RX_NUM), GFP_KERNEL);
    if (!p_self_rawdata) {
        TPD_INFO("Failed to alloc rawdata info memory\n");
        gts_test->error_count ++;
        ret = -1;
        goto exit_finish;
    }
    p_self_noisedata  = kzalloc(sizeof(u16) * (p_testdata->TX_NUM + p_testdata->RX_NUM), GFP_KERNEL);
    if (!p_self_noisedata) {
        TPD_INFO("Failed to alloc rawdata info memory\n");
        gts_test->error_count ++;
        ret = -1;
        goto exit_finish;
    }
    TPD_INFO("%s: alloc mem:%ld\n", __func__, sizeof(struct gtx8_ts_test) + 4 * (sizeof(u16) * (p_testdata->TX_NUM * p_testdata->RX_NUM)));

    gts_test->p_seq_file = s;
    gts_test->ts = chip_info;
    gts_test->p_testdata = p_testdata;

    gts_test->rawdata.data = p_rawdata;
    gts_test->noisedata.data = p_noisedata;
    gts_test->self_rawdata.data = p_self_rawdata;
    gts_test->self_noisedata.data = p_self_noisedata;

    gtx8_intgpio_test(gts_test);
    ret = gtx8_tptest_prepare(gts_test);
    if (ret) {
        TPD_INFO("%s: Failed parse test peremeters, exit test\n", __func__);
        gts_test->error_count ++;
        goto exit_finish;
    }
    TPD_INFO("%s: TP test prepare OK\n", __func__);

    goodix_set_i2c_doze_mode(chip_info->client, false);

    gtx8_print_test_params(gts_test);
    gtx8_test_noisedata(gts_test); /*3F 7F test*/
    gtx8_capacitance_test(gts_test); /* 1F 2F 6F test*/
    gtx8_shortcircut_test(gts_test); /* 5F test */
    gtx8_print_testdata(gts_test);
    gtx8_tptest_finish(gts_test);

    goodix_set_i2c_doze_mode(chip_info->client, true);
    /*read device fw version*/
    fw_ver = goodix_read_version(chip_info, NULL);
    cfg_ver = goodix_config_version_read(chip_info);
    gts_test->device_tp_fw = cfg_ver | (fw_ver & 0xFF) << 8;
    gtx8_put_test_result(gts_test);

exit_finish:
    tp_healthinfo_report(chip_info->monitor_data_v2, HEALTH_TEST_AUTO, &gts_test->error_count);
    seq_printf(s, "imageid = %lld, deviceid = %lld\n", p_testdata->TP_FW, gts_test->device_tp_fw);
    TPD_INFO("imageid= %lld, deviceid= %lld\n", p_testdata->TP_FW, gts_test->device_tp_fw);
    seq_printf(s, "%d error(s). %s\n", gts_test->error_count, gts_test->error_count ? "" : "All test passed.");
    TPD_INFO(" TP auto test %d error(s). %s\n", gts_test->error_count, gts_test->error_count ? "" : "All test passed.");
    TPD_INFO("%s exit:%d\n", __func__, ret);
    if (p_rawdata) {
        kfree(p_rawdata);
        p_rawdata = NULL;
    }
    if (p_noisedata) {
        kfree(p_noisedata);
        p_noisedata = NULL;
    }
    if (p_self_rawdata) {
        kfree(p_self_rawdata);
        p_self_rawdata = NULL;
    }
    if (p_self_noisedata) {
        kfree(p_self_noisedata);
        p_self_noisedata = NULL;
    }
    if (gts_test) {
        kfree(gts_test);
        gts_test = NULL;
    }
    return;
}
/*************** End of atuo test func***************************/

static int goodix_set_health_info_state (void *chip_data, uint8_t enable)
{
    int ret = 0;
    struct chip_data_gt9886 *chip_info = (struct chip_data_gt9886 *)chip_data;

    TPD_INFO("%s, enable : %d\n", __func__, enable);
    if (enable) {
        ret = goodix_send_cmd(chip_info, GTP_CMD_DEBUG, 0x55);
        ret |= goodix_send_cmd(chip_info, GTP_CMD_DOWN_DELTA, 0x55);
        TPD_INFO("%s: enable debug log %s\n", __func__, ret < 0 ? "failed" : "success");
    } else {
        ret = goodix_send_cmd(chip_info, GTP_CMD_DEBUG, 0x00);
        ret |= goodix_send_cmd(chip_info, GTP_CMD_DOWN_DELTA, 0x55);
        TPD_INFO("%s: disable debug log %s\n", __func__, ret < 0 ? "failed" : "success");
    }

    return ret;
}

static int goodix_get_health_info_state (void *chip_data)
{
    TPD_INFO("%s enter\n", __func__);
    return 0;
}

struct goodix_proc_operations goodix_gt9886_proc_ops = {
    .goodix_config_info_read    = goodix_config_info_read,
    .auto_test                  = goodix_auto_test,
    .set_health_info_state      = goodix_set_health_info_state,
    .get_health_info_state      = goodix_get_health_info_state,
};

/*********** Start of I2C Driver and Implementation of it's callbacks*************************/
static int goodix_tp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct chip_data_gt9886 *chip_info = NULL;
    struct touchpanel_data *ts = NULL;
    u64 time_counter = 0;
    int ret = -1;

    TPD_INFO("%s is called\n", __func__);

    if (tp_register_times > 0) {
        TPD_INFO("TP driver have success loaded %d times, exit\n", tp_register_times);
        return -1;
    }
    reset_healthinfo_time_counter(&time_counter);

    /* 1. Alloc chip_info */
    chip_info = kzalloc(sizeof(struct chip_data_gt9886), GFP_KERNEL);
    if (chip_info == NULL) {
        TPD_INFO("chip info kzalloc error\n");
        ret = -ENOMEM;
        return ret;
    }

    /* 2. Alloc common ts */
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto ts_malloc_failed;
    }

    /* 3. alloc touch data space */
    chip_info->touch_data = kzalloc(MAX_GT_IRQ_DATA_LENGTH, GFP_KERNEL);
    if (chip_info->touch_data == NULL) {
        TPD_INFO("touch_data kzalloc error\n");
        goto err_register_driver;
    }
    chip_info->edge_data = kzalloc(MAX_GT_EDGE_DATA_LENGTH, GFP_KERNEL);
    if (chip_info->edge_data == NULL) {
        TPD_INFO("edge_data kzalloc error\n");
        goto err_touch_data_alloc;
    }

    /* 4. bind client and dev for easy operate */
    chip_info->client = client;
    chip_info->goodix_ops = &goodix_gt9886_proc_ops;
    ts->debug_info_ops = &debug_info_proc_ops;
    ts->client = client;
    ts->irq = client->irq;
    ts->dev = &client->dev;
    ts->chip_data = chip_info;
    chip_info->hw_res = &ts->hw_res;
    i2c_set_clientdata(client, ts);

    /* 5. file_operations callbacks binding */
    ts->ts_ops = &goodix_ops;

    /* 6. register common touch device*/
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        goto err_edge_data_alloc;
    }
    chip_info->kernel_grip_support = ts->kernel_grip_support;
    chip_info->detail_debug_info_support = of_property_read_bool(ts->dev->of_node, "goodix_detail_debug_info_support");
    chip_info->monitor_data_v2 = &ts->monitor_data_v2;

    /* 8. create goodix tool node */
    gtx8_init_tool_node(ts);

    /* 9. create goodix debug files */
    Goodix_create_proc(ts, chip_info->goodix_ops);

    goodix_esd_check_enable(chip_info, true);

    if (ts->health_monitor_v2_support) {
        tp_healthinfo_report(&ts->monitor_data_v2, HEALTH_PROBE, &time_counter);
    }
    TPD_INFO("%s, probe normal end\n", __func__);
    return 0;

err_edge_data_alloc:
    if (chip_info->edge_data) {
        kfree(chip_info->edge_data);
    }
    chip_info->edge_data = NULL;

err_touch_data_alloc:
    if (chip_info->touch_data) {
        kfree(chip_info->touch_data);
    }
    chip_info->touch_data = NULL;

err_register_driver:
    common_touch_data_free(ts);
    ts = NULL;

ts_malloc_failed:
    kfree(chip_info);
    chip_info = NULL;
    ret = -1;

    TPD_INFO("%s, probe error\n", __func__);
    return ret;
}

static int goodix_tp_remove(struct i2c_client *client)
{
    struct touchpanel_data *ts = i2c_get_clientdata(client);

    TPD_INFO("%s is called\n", __func__);
    kfree(ts);

    return 0;
}

static int goodix_i2c_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s: is called\n", __func__);
    tp_i2c_suspend(ts);

    return 0;
}

static int goodix_i2c_resume(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s is called\n", __func__);
    tp_i2c_resume(ts);

    return 0;
}

static const struct i2c_device_id tp_id[] = {
    {TPD_DEVICE, 0},
    {},
};

static struct of_device_id tp_match_table[] = {
    { .compatible = TPD_DEVICE, },
    { },
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
    .suspend = goodix_i2c_suspend,
    .resume = goodix_i2c_resume,
#endif
};

static struct i2c_driver tp_i2c_driver = {
    .probe = goodix_tp_probe,
    .remove = goodix_tp_remove,
    .id_table = tp_id,
    .driver = {
        .name = TPD_DEVICE,
        .owner = THIS_MODULE,
        .of_match_table = tp_match_table,
        .pm = &tp_pm_ops,
    },
};
/******************* End of I2C Driver and It's dev_pm_ops***********************/

/***********************Start of module init and exit****************************/
static int __init tp_driver_init(void)
{
    TPD_INFO("%s is called\n", __func__);

    if (!tp_judge_ic_match(TPD_DEVICE))
        return -1;

    if (i2c_add_driver(&tp_i2c_driver) != 0) {
        TPD_INFO("unable to add i2c driver.\n");
        return -1;
    }

    return 0;
}


static void __exit tp_driver_exit(void)
{
    i2c_del_driver(&tp_i2c_driver);

    return;
}

#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(tp_driver_init);
#else
module_init(tp_driver_init);
#endif

module_exit(tp_driver_exit);
/***********************End of module init and exit*******************************/

MODULE_DESCRIPTION("GTP Touchpanel Driver");
MODULE_LICENSE("GPL v2");

