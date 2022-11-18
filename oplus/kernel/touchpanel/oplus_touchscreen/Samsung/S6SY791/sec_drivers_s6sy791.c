// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/task_work.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#include "sec_drivers_s6sy791.h"

/****************** Start of Log Tag Declear and level define*******************************/
#define TPD_DEVICE "sec-s6sy791"
#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)\
    do{\
        if (LEVEL_DEBUG == tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

#define TPD_DETAIL(a, arg...)\
    do{\
        if (LEVEL_BASIC != tp_debug)\
            pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
    }while(0)

#define TPD_DEBUG_NTAG(a, arg...)\
    do{\
        if (tp_debug)\
            printk(a, ##arg);\
    }while(0)
/******************** End of Log Tag Declear and level define*********************************/

/*************************** start of function delcare****************************************/
static void sec_mdelay(unsigned int ms);
static int sec_reset(void *chip_data);
static int sec_power_control(void *chip_data, bool enable);
static int sec_get_verify_result(struct chip_data_s6sy791 *chip_info);
static int sec_read_mutual(struct chip_data_s6sy791 *chip_info, u8 type, char *data, int len);
static bool check_calibration(struct chip_data_s6sy791 *chip_info);
static void sec_calibrate(struct seq_file *s, void *chip_data);
static bool sec_get_cal_status(struct seq_file *s, void *chip_data);

/**************************** end of function delcare*****************************************/

/*************************** start of global variable delcare****************************************/
static struct chip_data_s6sy791 *g_chip_info;

/**************************** end of global variable delcare*****************************************/

/****** Start of other functions that work for oplus_touchpanel_operations callbacks***********/
static int sec_enable_black_gesture(struct chip_data_s6sy791 *chip_info, bool enable)
{
    int ret = 0;
    int i = 0;
	bool single_tap_support_b = 0;
	bool ear_sense_support_b = 0;
	struct touchpanel_data *ts = i2c_get_clientdata(chip_info->client);

	if (ts) {
		single_tap_support_b = ts->single_tap_support;
		ear_sense_support_b = ts->ear_sense_support;
	}

	TPD_INFO("%s, enable = %d,single_tap_support_b = %d,ear_sense_support_b =%d\n", __func__, enable, single_tap_support_b, ear_sense_support_b);
    if (enable) {
		if (single_tap_support_b) {
		        touch_i2c_write_word(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0xFFFF);
		} else {
		        touch_i2c_write_word(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0xFF1F);
		}

        for (i = 0; i < 20; i++) {
            touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_POWER_MODE, 0x01);
            sec_mdelay(10);
            ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_POWER_MODE);
            if (0x01 == ret)
                break;
        }
#ifdef CONFIG_OPLUS_TP_APK
        if (chip_info->debug_gesture_sta) {
            chip_info->in_gesture = true;
            chip_info->geture_points_count = 0;
            chip_info->debug_gesture_type = 0;
            touch_i2c_write_byte(chip_info->client, SEC_CMD_GESTURE_DEBUG, 0x01);
        }
#endif // end of CONFIG_OPLUS_TP_APK
    } else {
        touch_i2c_write_word(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0x0000);
        if (chip_info->fp_info.touch_state != FINGERPRINT_DOWN_DETECT || *chip_info->in_suspend
            || (chip_info->fp_info.touch_state == FINGERPRINT_DOWN_DETECT && *chip_info->fp_enable == 2)) {
            for (i = 0; i < 20; i++) {
                touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_POWER_MODE, 0x00);
                sec_mdelay(10);
                ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_POWER_MODE);
                if (0x00 == ret)
                    break;
            }
        }
#ifdef CONFIG_OPLUS_TP_APK
        if (chip_info->debug_gesture_sta) {
            chip_info->in_gesture = false;
            touch_i2c_write_byte(chip_info->client, SEC_CMD_GESTURE_DEBUG, 0x00);
        }
#endif // end of CONFIG_OPLUS_TP_APK

    }

    if (i >= 20) {
        ret = -1;
        TPD_INFO("%s: change black gesture failed\n", __func__);
    } else {
        TPD_INFO("%s: %d times change black gesture success\n", __func__, i);
    }
    return ret;
}

static void sec_enable_gesture_mask(void *chip_data, uint32_t enable)
{
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    int ret = -1;
    int i = 0;
	bool single_tap_support_b = 0;
	struct touchpanel_data *ts = i2c_get_clientdata(chip_info->client);

	if (ts) {
		single_tap_support_b = ts->single_tap_support;
	}

	TPD_INFO("%s, enable = %d,single_tap_support_b =%d\n", __func__, enable, single_tap_support_b);

    if (enable) {
        for (i = 0; i < 20; i++) {
			if (single_tap_support_b) {
				touch_i2c_write_word(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0xFFFF);
			} else {
				touch_i2c_write_word(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0xFF1F);
			}

            touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_POWER_MODE, 0x01);
            sec_mdelay(10);
            ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_POWER_MODE);
            if (0x01 == ret)
                break;
        }
    } else {
        touch_i2c_write_word(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE, 0x0000);
        return ;
    }

    if (i >= 20) {
        ret = -1;
        TPD_INFO("%s: change black gesture_mask failed\n", __func__);
    } else {
        TPD_INFO("%s: %d times change black gesture_mask success\n", __func__, i);
    }
}


static int sec_enable_charge_mode(struct chip_data_s6sy791 *chip_info, bool enable)
{
    int ret = -1;

    if (enable) {
        ret = touch_i2c_write_byte(chip_info->client, SET_CMD_SET_CHARGER_MODE, 0x02);
    } else {
        ret = touch_i2c_write_byte(chip_info->client, SET_CMD_SET_CHARGER_MODE, 0x01);
    }

    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_earsense_mode(struct chip_data_s6sy791 *chip_info, bool enable)
{
    int ret = -1;

    if (enable) {
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_HOVER_DETECT, 1);
        ret |= touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_DATA_DELTA);
    } else {
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_HOVER_DETECT, 0);
        ret |= touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_SIGNAL_DATA);
    }

    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_palm_reject(struct chip_data_s6sy791 *chip_info, bool enable)
{
    int ret = -1;

    if (enable) {
        ret = touch_i2c_write_word(chip_info->client, SEC_CMD_PALM_SWITCH, 0x0061);
    } else {
        ret = touch_i2c_write_word(chip_info->client, SEC_CMD_PALM_SWITCH, 0x0041);
    }

    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_game_mode(struct chip_data_s6sy791 *chip_info, bool enable)
{
    int ret = -1;
    char buf[4] = {0x00, 0x00, 0x00, 0x00};


	if (!chip_info->oos_game_switch_support) {
		buf[3] = enable ? 0x12 : 0x18;  /*default value 0x18*/
		ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSETIVE_CTRL, sizeof(buf), buf);
	}
    ret |= touch_i2c_write_byte(chip_info->client, SEC_CMD_GAME_MODE, enable ? 1 : 0);
    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static int sec_enable_headset_mode(struct chip_data_s6sy791 *chip_info, bool enable)
{
    int ret = -1;

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_HEADSET_MODE, enable ? 1 : 0);
    TPD_INFO("%s: state: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
    return ret;
}

static void sec_mdelay(unsigned int ms)
{
    if (ms < 20)
        usleep_range(ms * 1000, ms * 1000);
    else
        msleep(ms);
}

static int sec_wait_for_ready(struct chip_data_s6sy791 *chip_info, unsigned int ack)
{
    int rc = -1;
    int retry = 0, retry_cnt = 100;
    int8_t status = -1;
    u8 tBuff[SEC_EVENT_BUFF_SIZE] = {0,};

    while (touch_i2c_read_block(chip_info->client, SEC_READ_ONE_EVENT, SEC_EVENT_BUFF_SIZE, tBuff) > 0) {
        status = (tBuff[0] >> 2) & 0xF;
        if ((status == TYPE_STATUS_EVENT_INFO) || (status == TYPE_STATUS_EVENT_VENDOR_INFO)) {
            if (tBuff[1] == ack) {
                rc = 0;
                break;
            }
        }

        if (retry++ > retry_cnt) {
            TPD_INFO("%s: Time Over, event_buf: %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X \n", \
                     __func__, tBuff[0], tBuff[1], tBuff[2], tBuff[3], tBuff[4], tBuff[5], tBuff[6], tBuff[7]);
            break;
        }
        sec_mdelay(20);
    }

    return rc;
}

static int sec_enter_fw_mode(struct chip_data_s6sy791 *chip_info)
{
    int ret = -1;
    u8 device_id[3] = {0};
    u8 fw_update_mode_passwd[] = {0x55, 0xAC};

    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_ENTER_FW_MODE, sizeof(fw_update_mode_passwd), fw_update_mode_passwd);
    sec_mdelay(20);
    if (ret < 0) {
        TPD_INFO("%s: write cmd to enter fw mode failed\n", __func__);
        return -1;
    }

    //need soft reset or hard reset
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        gpio_direction_output(chip_info->hw_res->reset_gpio, false);
        sec_mdelay(10);
        gpio_direction_output(chip_info->hw_res->reset_gpio, true);
    } else {
        ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SOFT_RESET, 0, NULL);
        if (ret < 0) {
            TPD_INFO("%s: write soft reset failed\n", __func__);
            return -1;
        }
    }
    sec_mdelay(100);

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);     //after reset, check bootloader again
    if (ret < 0) {
        TPD_INFO("%s: read boot status failed\n", __func__);
        return -1;
    }
    if (ret != SEC_STATUS_BOOT_MODE) {
        TPD_INFO("%s: read boot status, but no in boot mode(%d)\n", __func__, ret);
        return -1;
    }

    sec_mdelay(10);

    ret = touch_i2c_read_block(chip_info->client, SEC_READ_ID, 3, device_id);
    if (ret < 0) {
        TPD_INFO("%s: read 3 byte device id failed\n", __func__);
        return -1;
    }

    chip_info->boot_ver[0] = device_id[0];
    chip_info->boot_ver[1] = device_id[1];
    chip_info->boot_ver[2] = device_id[2];
    chip_info->flash_page_size = 512;
    // The ic is 791 so it must be 0x91.
    //if ((device_id[1] == 0x37) && (device_id[2] == 0x91))
    //    chip_info->flash_page_size = 512;

    return 0;
}

static u8 sec_checksum(u8 *data, int offset, int size)
{
    int i;
    u8 checksum = 0;

    for (i = 0; i < size; i++)
        checksum += data[i + offset];

    return checksum;
}

static int sec_flash_page_erase(struct chip_data_s6sy791 *chip_info, u32 page_idx, u32 page_num)
{
    int ret = -1;
    u8 tCmd[6] = {0};

    tCmd[0] = SEC_CMD_FLASH_ERASE;
    tCmd[1] = (u8)((page_idx >> 8) & 0xFF);
    tCmd[2] = (u8)((page_idx >> 0) & 0xFF);
    tCmd[3] = (u8)((page_num >> 8) & 0xFF);
    tCmd[4] = (u8)((page_num >> 0) & 0xFF);
    tCmd[5] = sec_checksum(tCmd, 1, 4);

    ret = touch_i2c_write(chip_info->client, tCmd, 6);

    return ret;
}

static int sec_flash_page_write(struct chip_data_s6sy791 *chip_info, u32 page_idx, u8 *page_data)
{
    int ret;
    u8 tCmd[1 + 2 + SEC_FW_BLK_SIZE_MAX + 1];
    int flash_page_size = (int)chip_info->flash_page_size;

    tCmd[0] = SEC_CMD_FLASH_WRITE;
    tCmd[1] = (u8)((page_idx >> 8) & 0xFF);
    tCmd[2] = (u8)((page_idx >> 0) & 0xFF);

    memcpy(&tCmd[3], page_data, flash_page_size);
    tCmd[1 + 2 + flash_page_size] = sec_checksum(tCmd, 1, 2 + flash_page_size);

    ret = touch_i2c_write(chip_info->client, tCmd, 1 + 2 + flash_page_size + 1);
    return ret;
}

static bool sec_limited_flash_page_write(struct chip_data_s6sy791 *chip_info, u32 page_idx, u8 *page_data)
{
    int ret = -1;
    u8 *tCmd = NULL;
    u8 copy_data[3 + SEC_FW_BLK_SIZE_MAX];
    int copy_left = (int)chip_info->flash_page_size + 3;
    int copy_size = 0;
    int copy_max = I2C_BURSTMAX - 1;
    int flash_page_size = (int)chip_info->flash_page_size;

    copy_data[0] = (u8)((page_idx >> 8) & 0xFF);    /* addH */
    copy_data[1] = (u8)((page_idx >> 0) & 0xFF);    /* addL */

    memcpy(&copy_data[2], page_data, flash_page_size);    /* DATA */
    copy_data[2 + flash_page_size] = sec_checksum(copy_data, 0, 2 + flash_page_size);    /* CS */

    while (copy_left > 0) {
        int copy_cur = (copy_left > copy_max) ? copy_max : copy_left;

        tCmd = kzalloc(copy_cur + 1, GFP_KERNEL);
        if (!tCmd)
            goto err_write;

        if (copy_size == 0)
            tCmd[0] = SEC_CMD_FLASH_WRITE;
        else
            tCmd[0] = SEC_CMD_FLASH_PADDING;

        memcpy(&tCmd[1], &copy_data[copy_size], copy_cur);

        ret = touch_i2c_write(chip_info->client, tCmd, 1 + copy_cur);
        if (ret < 0) {
            ret = touch_i2c_write(chip_info->client, tCmd, 1 + copy_cur);
            if (ret < 0) {
                TPD_INFO("%s: failed, ret:%d\n", __func__, ret);
            }
        }

        copy_size += copy_cur;
        copy_left -= copy_cur;
        kfree(tCmd);
    }
    return ret;

err_write:
    TPD_INFO("%s: failed to alloc.\n", __func__);
    return -ENOMEM;

}

static int sec_flash_write(struct chip_data_s6sy791 *chip_info, u32 mem_addr, u8 *mem_data, u32 mem_size)
{
    int ret = -1;
    u32 page_idx = 0, size_copy = 0, flash_page_size = 0;
    u32 page_idx_start = 0, page_idx_end = 0, page_num = 0;
    u8 page_buf[SEC_FW_BLK_SIZE_MAX] = {0};

    if (mem_size == 0)
        return 0;

    flash_page_size = chip_info->flash_page_size;
    page_idx_start = mem_addr / flash_page_size;
    page_idx_end = (mem_addr + mem_size - 1) / flash_page_size;
    page_num = page_idx_end - page_idx_start + 1;

    ret = sec_flash_page_erase(chip_info, page_idx_start, page_num);
    if (ret < 0) {
        TPD_INFO("%s: fw erase failed, mem_addr= %08X, pagenum = %d\n", __func__, mem_addr, page_num);
        return -EIO;
    }

    sec_mdelay(page_num + 10);

    size_copy = mem_size % flash_page_size;
    if (size_copy == 0)
        size_copy = flash_page_size;

    memset(page_buf, 0, flash_page_size);

    for (page_idx = page_num - 1;; page_idx--) {
        memcpy(page_buf, mem_data + (page_idx * flash_page_size), size_copy);
        if (chip_info->boot_ver[0] == 0xB2) {
            ret = sec_flash_page_write(chip_info, (page_idx + page_idx_start), page_buf);
            if (ret < 0) {
                sec_mdelay(50);
                ret = sec_flash_page_write(chip_info, (page_idx + page_idx_start), page_buf);
                if (ret < 0) {
                    TPD_INFO("%s: fw write failed, page_idx = %u\n", __func__, page_idx);
                    goto err;
                }
            }
        } else {
            ret = sec_limited_flash_page_write(chip_info, (page_idx + page_idx_start), page_buf);
            if (ret < 0) {
                sec_mdelay(50);
                ret = sec_limited_flash_page_write(chip_info, (page_idx + page_idx_start), page_buf);
                if (ret < 0) {
                    TPD_INFO("%s: fw write failed, page_idx = %u\n", __func__, page_idx);
                    goto err;
                }
            }

        }

        size_copy = flash_page_size;
        sec_mdelay(5);

        if (page_idx == 0) /* end condition (page_idx >= 0)   page_idx type unsinged int */
            break;
    }

    return mem_size;
err:
    return -EIO;
}

static int sec_block_read(struct chip_data_s6sy791 *chip_info, u32 mem_addr, int mem_size, u8 *buf)
{
    int ret;
    u8 cmd[5];
    u8 *data;

    if (mem_size >= 64 * 1024) {
        TPD_INFO("%s: mem size over 64K\n", __func__);
        return -EIO;
    }

    cmd[0] = (u8)SEC_CMD_FLASH_READ_ADDR;
    cmd[1] = (u8)((mem_addr >> 24) & 0xff);
    cmd[2] = (u8)((mem_addr >> 16) & 0xff);
    cmd[3] = (u8)((mem_addr >> 8) & 0xff);
    cmd[4] = (u8)((mem_addr >> 0) & 0xff);

    ret = touch_i2c_write(chip_info->client, cmd, 5);
    if (ret < 0) {
        TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
        return -EIO;
    }

    udelay(10);
    cmd[0] = (u8)SEC_CMD_FLASH_READ_SIZE;
    cmd[1] = (u8)((mem_size >> 8) & 0xff);
    cmd[2] = (u8)((mem_size >> 0) & 0xff);

    ret = touch_i2c_write(chip_info->client, cmd, 3);
    if (ret < 0) {
        TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
        return -EIO;
    }

    udelay(10);
    cmd[0] = (u8)SEC_CMD_FLASH_READ_DATA;
    data = buf;

    ret = touch_i2c_read(chip_info->client, cmd, 1, data, mem_size);
    if (ret < 0) {
        TPD_INFO("%s: memory read failed\n", __func__);
        return -EIO;
    }

    return 0;
}

static int sec_memory_read(struct chip_data_s6sy791 *chip_info, u32 mem_addr, u8 *mem_data, u32 mem_size)
{
    int ret;
    int retry = 3;
    int read_size = 0;
    int unit_size;
    int max_size = I2C_BURSTMAX;
    int read_left = (int)mem_size;
    u8 *tmp_data;

    tmp_data = kmalloc(max_size, GFP_KERNEL);
    if (!tmp_data) {
        TPD_INFO("%s: failed to kmalloc\n", __func__);
        return -ENOMEM;
    }

    while (read_left > 0) {
        unit_size = (read_left > max_size) ? max_size : read_left;
        retry = 3;
        do {
            ret = sec_block_read(chip_info, mem_addr, unit_size, tmp_data);
            if (retry-- == 0) {
                TPD_INFO("%s: fw read fail mem_addr=%08X, unit_size=%d\n", __func__, mem_addr, unit_size);
                kfree(tmp_data);
                return -1;
            }

            memcpy(mem_data + read_size, tmp_data, unit_size);
        } while (ret < 0);

        mem_addr += unit_size;
        read_size += unit_size;
        read_left -= unit_size;
    }

    kfree(tmp_data);

    return read_size;
}

static int sec_chunk_update(struct chip_data_s6sy791 *chip_info, u32 addr, u32 size, u8 *data)
{
    int ii = 0, ret = 0;
    u8 *mem_rb = NULL;
    u32 write_size = 0;
    u32 fw_size = size;

    write_size = sec_flash_write(chip_info, addr, data, fw_size);
    if (write_size != fw_size) {
        TPD_INFO("%s: fw write failed\n", __func__);
        ret = -1;
        goto err_write_fail;
    }

    mem_rb = vzalloc(fw_size);
    if (!mem_rb) {
        TPD_INFO("%s: vzalloc failed\n", __func__);
        ret = -1;
        goto err_write_fail;
    }

    if (sec_memory_read(chip_info, addr, mem_rb, fw_size) >= 0) {
        for (ii = 0; ii < fw_size; ii++) {
            if (data[ii] != mem_rb[ii])
                break;
        }

        if (fw_size != ii) {
            TPD_INFO("%s: fw verify fail at data[%d](%d, %d)\n", __func__, ii, data[ii], mem_rb[ii]);
            ret = -1;
            goto out;
        }
    } else {
        ret = -1;
        goto out;
    }

    TPD_INFO("%s: verify done(%d)\n", __func__, ret);

out:
    vfree(mem_rb);
err_write_fail:
    sec_mdelay(10);

    return ret;
}

static int sec_read_calibration_report(struct chip_data_s6sy791 *chip_info)
{
    int ret;
    u8 buf[5] = { 0 };

    buf[0] = SEC_CMD_READ_CALIBRATION_REPORT;

    ret = touch_i2c_read(chip_info->client, &buf[0], 1, &buf[1], 4);
    if (ret < 0) {
        TPD_INFO("%s: failed to read, ret = %d\n", __func__, ret);
        return ret;
    }

    TPD_INFO("%s: count:%d, pass count:%d, fail count:%d, status:0x%X\n",
             __func__, buf[1], buf[2], buf[3], buf[4]);

    return buf[4];
}

static int sec_execute_force_calibration(struct chip_data_s6sy791 *chip_info)
{
    int rc = -1;

    TPD_INFO("start do force calibration\n");
    if (touch_i2c_write_block(chip_info->client, SEC_CMD_FACTORY_PANELCALIBRATION, 0, NULL) < 0) {
        TPD_INFO("%s: Write Cal commend failed!\n", __func__);
        return rc;
    }

    sec_mdelay(1000);
    rc = sec_wait_for_ready(chip_info, SEC_VENDOR_ACK_OFFSET_CAL_DONE);

    return rc;
}

static void handleFourCornerPoint(struct Coordinate *point, int n)
{
    int i = 0;
    struct Coordinate left_most = point[0], right_most = point[0], top_most = point[0], down_most = point[0];

    if (n < 4)
        return;

    for (i = 0; i < n; i++) {
        if (right_most.x < point[i].x) {   //xmax
            right_most = point[i];
        }
        if (left_most.x > point[i].x) {   //xmin
            left_most = point[i];
        }
        if (down_most.y < point[i].y) {   //ymax
            down_most = point[i];
        }
        if (top_most.y > point[i].y) {   //ymin
            top_most = point[i];
        }
    }
    point[0] = top_most;
    point[1] = left_most;
    point[2] = down_most;
    point[3] = right_most;
}
/****** End of other functions that work for oplus_touchpanel_operations callbacks*************/

/********* Start of implementation of oplus_touchpanel_operations callbacks********************/
static int sec_reset(void *chip_data)
{
    int ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    TPD_INFO("%s is called\n", __func__);
    if (chip_info->is_power_down) { //power off state, no need reset
        return 0;
    }

    disable_irq_nosync(chip_info->client->irq);

    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {    //rsted by rst pin
        TPD_INFO("reset by pull down rst pin");
        gpio_direction_output(chip_info->hw_res->reset_gpio, false);
        sec_mdelay(5);
        gpio_direction_output(chip_info->hw_res->reset_gpio, true);
    } else {    //otherwise by soft reset
        touch_i2c_write_block(chip_info->client, SEC_CMD_SOFT_RESET, 0, NULL);
    }

    sec_mdelay(RESET_TO_NORMAL_TIME);
    sec_wait_for_ready(chip_info, SEC_ACK_BOOT_COMPLETE);
    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
    TPD_INFO("%s: write sense on %s\n", __func__, (ret < 0) ? "failed" : "success");

    enable_irq(chip_info->client->irq);

    return 0;
}

static int sec_ftm_process(void *chip_data)
{
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    TPD_INFO("%s is called!\n", __func__);
    tp_powercontrol_2v8(chip_info->hw_res, false);
    tp_powercontrol_1v8(chip_info->hw_res, false);
    if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
        gpio_direction_output(chip_info->hw_res->reset_gpio, false);
    }

    return 0;
}

static int sec_get_vendor(void *chip_data, struct panel_info *panel_data)
{
    int len = 0;
    char manu_temp[MAX_DEVICE_MANU_LENGTH] = "SEC_";
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    len = strlen(panel_data->fw_name);
    if ((len > 3) && (panel_data->fw_name[len - 3] == 'i') && \
        (panel_data->fw_name[len - 2] == 'm') && (panel_data->fw_name[len - 1] == 'g')) {
        panel_data->fw_name[len - 3] = 'b';
        panel_data->fw_name[len - 2] = 'i';
        panel_data->fw_name[len - 1] = 'n';
    }
    chip_info->tp_type = panel_data->tp_type;
    strlcat(manu_temp, panel_data->manufacture_info.manufacture, MAX_DEVICE_MANU_LENGTH);
    strncpy(panel_data->manufacture_info.manufacture, manu_temp, MAX_DEVICE_MANU_LENGTH);
    TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n", chip_info->tp_type, panel_data->fw_name);

    return 0;
}

static int sec_get_chip_info(void *chip_data)
{
    return 0;
}

static int sec_power_control(void *chip_data, bool enable)
{
    int ret = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    TPD_INFO("%s enable :%d\n", __func__, enable);
    if (true == enable) {
        tp_powercontrol_1v8(chip_info->hw_res, true);
        sec_mdelay(1);
        tp_powercontrol_2v8(chip_info->hw_res, true);

        if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
            TPD_INFO("Set the reset_gpio 1\n");
            gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
        }
        msleep(RESET_TO_NORMAL_TIME);
        sec_wait_for_ready(chip_info, SEC_ACK_BOOT_COMPLETE);
        ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
        TPD_INFO("%s: write sense on %s\n", __func__, (ret < 0) ? "failed" : "success");
        chip_info->is_power_down = false;
        if (chip_info->irq_requested) {
            enable_irq(chip_info->client->irq);
        }
    } else {
        if (chip_info->irq_requested) {
            disable_irq_nosync(chip_info->client->irq);
        }

        if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
            TPD_INFO("Set the reset_gpio 0\n");
            gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
        }
        tp_powercontrol_2v8(chip_info->hw_res, false);
        sec_mdelay(4);
        tp_powercontrol_1v8(chip_info->hw_res, false);

        chip_info->is_power_down = true;
    }

    return ret;
}

static fw_check_state sec_fw_check(void *chip_data, struct resolution_info *resolution_info, struct panel_info *panel_data)
{
    int ret = 0;
    int ver_len = 0;
    unsigned char data[5] = { 0 };
    char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
    bool valid_fw_integrity = false;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_FIRMWARE_INTEGRITY);  //judge whether fw is right
    if (ret < 0) {
        TPD_INFO("%s: failed to do integrity check (%d)\n", __func__, ret);
    } else {
        if (ret & 0x80) {
            valid_fw_integrity = true;
        } else {
            valid_fw_integrity = false;
            TPD_INFO("invalid firmware integrity (%d)\n", ret);
        }
    }

    data[0] = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_TS_STATUS, 4, &data[1]);
    if (ret < 0) {
        TPD_INFO("%s: failed to read touch status\n", __func__);
    }
    if ((((data[0] == SEC_STATUS_APP_MODE) && (data[2] == TOUCH_SYSTEM_MODE_FLASH)) || (ret < 0)) && (valid_fw_integrity == false)) {
        TPD_INFO("%s: fw id abnormal, need update\n", __func__);
        return FW_ABNORMAL;
    }

    memset(data, 0, 5);
    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, data);
    panel_data->TP_FW = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (panel_data->manufacture_info.version) {
        if (panel_data->vid_len == 0) {
            sprintf(panel_data->manufacture_info.version, "0x%x", panel_data->TP_FW);
        } else {
            ver_len = panel_data->vid_len;
            if (ver_len > MAX_DEVICE_VERSION_LENGTH - 4) {
                ver_len = MAX_DEVICE_VERSION_LENGTH - 4;
            }
            snprintf(dev_version, MAX_DEVICE_VERSION_LENGTH,
                     "%X", panel_data->TP_FW);
            strlcpy(&panel_data->manufacture_info.version[ver_len],
                    dev_version, MAX_DEVICE_VERSION_LENGTH - ver_len);

        }
    }

    // ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
    // TPD_INFO("%s: write sense on %s\n", (ret < 0) ? "failed" : "success");
    return FW_NORMAL;
}

static bool check_calibration(struct chip_data_s6sy791 *chip_info)
{
    u8 *data = NULL;
    int16_t temp_delta = 0, err_cnt = 0, judge_threshold = 2000;
    int ret = -1, x = 0, y = 0, z = 0;
    int readbytes = (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return false;
    }

    ret = sec_read_mutual(chip_info, TYPE_AMBIENT_DATA, data, readbytes);
    if (ret < 0) {
        goto OUT;
    }
    for (x = 0; x < chip_info->hw_res->RX_NUM; x++) {
        for (y = chip_info->hw_res->TX_NUM - 1; y >= 0; y--) {
            z = chip_info->hw_res->TX_NUM * x + y;
            temp_delta = (data[z * 2] << 8) | data[z * 2 + 1];
            if (abs(temp_delta) > judge_threshold) {
                err_cnt++;
                TPD_INFO("node[%d][%d] = %d beyond %d\n", y, x, temp_delta, judge_threshold);
            }
        }
    }

OUT:
    if (data)
        kfree(data);
    return err_cnt > 2;
}

static fw_update_state sec_fw_update(void *chip_data, const struct firmware *fw, bool force)
{
    int i = 0, ret = 0;
    u8 buf[4] = {0};
    u8 *fd = NULL;
    uint8_t cal_status = 0;
    sec_fw_chunk *fw_ch = NULL;
    sec_fw_header *fw_hd = NULL;
    fw_update_state update_state = FW_NO_NEED_UPDATE;
    uint32_t fw_version_in_bin = 0, fw_version_in_ic = 0;
    uint32_t config_version_in_bin = 0, config_version_in_ic = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    if (!chip_info) {
        TPD_INFO("Chip info is NULL\n");
        return 0;
    }

    TPD_INFO("%s is called, force update:%d\n", __func__, force);

    fd = (u8 *)(fw->data);
    fw_hd = (sec_fw_header *)(fw->data);
    buf[3] = (fw_hd->img_ver >> 24) & 0xff;
    buf[2] = (fw_hd->img_ver >> 16) & 0xff;
    buf[1] = (fw_hd->img_ver >> 8) & 0xff;
    buf[0] = (fw_hd->img_ver >> 0) & 0xff;
    fw_version_in_bin = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, buf);
    fw_version_in_ic = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    TPD_INFO("img version in bin is 0x%04x, img version in ic is 0x%04x\n", fw_version_in_bin, fw_version_in_ic);

    buf[3] = (fw_hd->para_ver >> 24) & 0xff;
    buf[2] = (fw_hd->para_ver >> 16) & 0xff;
    buf[1] = (fw_hd->para_ver >> 8) & 0xff;
    buf[0] = (fw_hd->para_ver >> 0) & 0xff;
    config_version_in_bin = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_CONFIG_VERSION, 4, buf);
    config_version_in_ic = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    TPD_INFO("config version in bin is 0x%04x, config version in ic is 0x%04x\n", config_version_in_bin, config_version_in_ic);

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
    if (ret == SEC_STATUS_BOOT_MODE) {
        force = 1;
        TPD_INFO("%s: still in bootloader mode, will do force update\n", __func__);
    }

    if (!force) {
        if (fw_version_in_bin == fw_version_in_ic) {
            update_state = FW_NO_NEED_UPDATE;
            goto CAL_CHECK;
        }
    }

    if (sec_enter_fw_mode(chip_info)) {
        TPD_INFO("%s: enter fw mode failed\n", __func__);
        update_state = FW_UPDATE_ERROR;
        goto CAL_CHECK;
    }

    if (fw_hd->signature != SEC_FW_HEADER_SIGN) {
        TPD_INFO("%s: firmware header error(0x%08x)\n", __func__, fw_hd->signature);
        update_state = FW_UPDATE_ERROR;
        goto CAL_CHECK;
    }

    fd += sizeof(sec_fw_header);
    for (i = 0; i < fw_hd->num_chunk; i++) {
        fw_ch = (sec_fw_chunk *)fd;
        TPD_INFO("update %d chunk(addr: 0x%08x, size: 0x%08x)\n", i, fw_ch->addr, fw_ch->size);
        if (fw_ch->signature != SEC_FW_CHUNK_SIGN) {
            TPD_INFO("%s: firmware chunk error(0x%08x)\n", __func__, fw_ch->signature);
            update_state = FW_UPDATE_ERROR;
            goto CAL_CHECK;
        }
        fd += sizeof(sec_fw_chunk);
        ret = sec_chunk_update(chip_info, fw_ch->addr, fw_ch->size, fd);
        if (ret < 0) {
            TPD_INFO("update chunk failed\n");
            update_state = FW_UPDATE_ERROR;
            goto CAL_CHECK;
        }
        fd += fw_ch->size;
    }
    update_state = FW_UPDATE_SUCCESS;

    sec_reset(chip_info);
    cal_status = sec_read_calibration_report(chip_info);    //read out calibration result
    if ((cal_status == 0) || (cal_status == 0xFF) || (config_version_in_ic != config_version_in_bin)) {
        TPD_INFO("start calibration because of cal_status or config version changed\n");
        sec_execute_force_calibration(chip_info);
    }

CAL_CHECK:
    if (check_calibration(chip_info)) {                     //check whether we need do calibration
        TPD_INFO("touchpanel have something wrong, need do calibration\n");
        chip_info->cal_needed = true;                       //set flag to show touchpanel need to be calibrated
        sec_execute_force_calibration(chip_info);           //do force calibration
        if (check_calibration(chip_info)) {
            TPD_INFO("set calibration flag to false\n");
            chip_info->cal_needed = false;                  //set false when still check failed after calibration
        }
    }
    TPD_INFO("update firmware state: %s\n", (update_state == FW_UPDATE_SUCCESS) ? "update success!" : \
             (update_state == FW_UPDATE_ERROR) ? "update failed!" : "no need update!");
    return update_state;
}

static u32 sec_trigger_reason(void *chip_data, int gesture_enable, int is_suspended)
{
    int ret = 0;
    int event_id = 0;
    u8 left_event_cnt = 0;
    struct sec_event_status *p_event_status = NULL;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    if (chip_info->is_power_down) {
        TPD_DETAIL("%s: power down, just return\n", __func__);
        return IRQ_IGNORE;
    }
    memset(chip_info->first_event, 0, SEC_EVENT_BUFF_SIZE);
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_ONE_EVENT, SEC_EVENT_BUFF_SIZE, chip_info->first_event);
    if (ret < 0) {
        TPD_DETAIL("%s: read one event failed\n", __func__);
        return IRQ_IGNORE;
    }

    TPD_DEBUG("first event: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", \
              chip_info->first_event[0], chip_info->first_event[1], chip_info->first_event[2], chip_info->first_event[3], \
              chip_info->first_event[4], chip_info->first_event[5], chip_info->first_event[6], chip_info->first_event[7]);

    if (chip_info->first_event[0] == 0) {
        TPD_DETAIL("%s: event buffer is empty\n", __func__);
        return IRQ_IGNORE;
    }

    left_event_cnt = chip_info->first_event[7] & 0x3F;
    if (left_event_cnt > MAX_EVENT_COUNT - 1) {
        TPD_INFO("%s: event buffer overflow, do clear the buffer\n", __func__);
        ret = touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
        if (ret < 0) {
            TPD_INFO("%s: clear event buffer failed\n", __func__);
        }
        return IRQ_IGNORE;
    }

    event_id = chip_info->first_event[0] & 0x3;
    if (event_id == SEC_STATUS_EVENT) {
        /* watchdog reset -> send SENSEON command */
        p_event_status = (struct sec_event_status *)chip_info->first_event;
        if ((p_event_status->stype == TYPE_STATUS_EVENT_INFO) &&
            (p_event_status->status_id == SEC_ACK_BOOT_COMPLETE) && (p_event_status->status_data_1 == 0x20)) {

            ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
            if (ret < 0) {
                TPD_INFO("%s: write sense on failed\n", __func__);
            }
            return IRQ_FW_AUTO_RESET;
        }

        if ((p_event_status->stype == TYPE_STATUS_EVENT_INFO) && (p_event_status->status_id == SEC_ACK_WET_MODE)) {
            TPD_INFO("wet mode status :%d\n", p_event_status->status_data_1);
        }

        if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO) && (p_event_status->status_id == SEC_VENDOR_INFO_NOISE_MODE)) {
            TPD_INFO("noise mode status :%d\n", p_event_status->status_data_1);
        }

        if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO) && (p_event_status->status_id == SEC_VENDOR_INFO_NOISE_LEVEL_CHANGED)) {
            TPD_INFO("noise mode level :%d\n", p_event_status->status_data_1);
        }

        if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO) && (p_event_status->status_id == SEC_STATUS_TOUCHHOLD)) {
            if (p_event_status->status_data_1 == 0x01) {
                chip_info->fp_info.touch_state = FINGERPRINT_DOWN_DETECT;
                chip_info->fp_info.x = p_event_status->status_data_2;
                chip_info->fp_info.x |= (p_event_status->status_data_3 & 0xF0) << 4;
                chip_info->fp_info.y = (p_event_status->status_data_3 & 0x0F) << 8;
                chip_info->fp_info.y |= p_event_status->status_data_4;
            } else if (p_event_status->status_data_1 == 0x00) {
                chip_info->fp_info.touch_state = FINGERPRINT_UP_DETECT;
                chip_info->fp_info.x = p_event_status->status_data_2;
                chip_info->fp_info.x |= (p_event_status->status_data_3 & 0xF0) << 4;
                chip_info->fp_info.y = (p_event_status->status_data_3 & 0x0F) << 8;
                chip_info->fp_info.y |= p_event_status->status_data_4;
            }
            return IRQ_FINGERPRINT;
        }

        /* event queue full-> all finger release */
        if ((p_event_status->stype == TYPE_STATUS_EVENT_ERR) && (p_event_status->status_id == SEC_ERR_EVENT_QUEUE_FULL)) {
            TPD_INFO("%s: IC Event Queue is full\n", __func__);
            tp_touch_btnkey_release();
        }

        if ((p_event_status->stype == TYPE_STATUS_EVENT_ERR) && (p_event_status->status_id == SEC_ERR_EVENT_ESD)) {
            TPD_INFO("%s: ESD detected. run reset\n", __func__);
            return IRQ_EXCEPTION;
        }
        if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO) && (p_event_status->status_id == SEC_STATUS_EARDETECTED)) {
            TPD_INFO("%s: EAR detected. \n", __func__);
            return IRQ_IGNORE;
        }
    } else if (event_id == SEC_COORDINATE_EVENT) {
        return IRQ_TOUCH;
    } else if (event_id == SEC_GESTURE_EVENT) {
        return IRQ_GESTURE;
    }

    return IRQ_IGNORE;
}

static int sec_get_touch_points(void *chip_data, struct point_info *points, int max_num)
{
    int i = 0;
    int t_id = 0;
    int ret = -1;
    int left_event = 0;
    struct sec_event_coordinate *p_event_coord = NULL;
    uint32_t obj_attention = 0;
    u8 event_buff[MAX_EVENT_COUNT][SEC_EVENT_BUFF_SIZE] = { { 0 } };
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    p_event_coord = (struct sec_event_coordinate *)chip_info->first_event;
    t_id = (p_event_coord->tid - 1);
    if ((t_id < max_num) && ((p_event_coord->tchsta == SEC_COORDINATE_ACTION_PRESS) || (p_event_coord->tchsta == SEC_COORDINATE_ACTION_MOVE))) {
        points[t_id].x = (p_event_coord->x_11_4 << 4) | (p_event_coord->x_3_0);
        points[t_id].y = (p_event_coord->y_11_4 << 4) | (p_event_coord->y_3_0);
        points[t_id].z = p_event_coord->z & 0x3F;
        points[t_id].width_major = p_event_coord->major;
        points[t_id].touch_major = p_event_coord->major;
        points[t_id].status = 1;

        if (points[t_id].z == 0) {
            points[t_id].z = 1;
        }

        obj_attention = obj_attention | (1 << t_id);    //set touch bit


#ifdef CONFIG_OPLUS_TP_APK
        if (chip_info->debug_gesture_sta && chip_info->in_gesture) {
            if (chip_info->geture_points_count < 1000 && t_id < 3) {
                chip_info->gesture_buf[chip_info->geture_points_count * 4] = t_id;
                chip_info->gesture_buf[chip_info->geture_points_count * 4 + 1] = chip_info->first_event[1];
                chip_info->gesture_buf[chip_info->geture_points_count * 4 + 2] = chip_info->first_event[2];
                chip_info->gesture_buf[chip_info->geture_points_count * 4 + 3] = chip_info->first_event[3];
                chip_info->geture_points_count++;
            }
        }
#endif // end of CONFIG_OPLUS_TP_APK
    }

    left_event = chip_info->first_event[7] & 0x3F;
    if (left_event == 0) {
        return obj_attention;
    } else if (left_event > max_num - 1) {
        TPD_INFO("%s: read left event beyond max touch points\n", __func__);
        left_event = max_num - 1;
    }
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_ALL_EVENT, sizeof(u8) * (SEC_EVENT_BUFF_SIZE) * (left_event), (u8 *)event_buff[0]);
    if (ret < 0) {
        TPD_INFO("%s: i2c read all event failed\n", __func__);
        return obj_attention;
    }

    for (i = 0; i < left_event; i++) {
        p_event_coord = (struct sec_event_coordinate *)event_buff[i];
        t_id = (p_event_coord->tid - 1);
        if ((t_id < max_num) && ((p_event_coord->tchsta == SEC_COORDINATE_ACTION_PRESS) || (p_event_coord->tchsta == SEC_COORDINATE_ACTION_MOVE))) {
            points[t_id].x = (p_event_coord->x_11_4 << 4) | (p_event_coord->x_3_0);
            points[t_id].y = (p_event_coord->y_11_4 << 4) | (p_event_coord->y_3_0);
            points[t_id].z = p_event_coord->z & 0x3F;
            points[t_id].width_major = p_event_coord->major;
            points[t_id].touch_major = p_event_coord->major;
            points[t_id].status = 1;

            if (points[t_id].z == 0) {
                points[t_id].z = 1;
            }

            obj_attention = obj_attention | (1 << t_id);    //set touch bit

#ifdef CONFIG_OPLUS_TP_APK
            if (chip_info->debug_gesture_sta && chip_info->in_gesture) {
                if (chip_info->geture_points_count < 1000 && t_id < 3) {
                    chip_info->gesture_buf[chip_info->geture_points_count * 4] = t_id;
                    chip_info->gesture_buf[chip_info->geture_points_count * 4 + 1] = event_buff[i][1];
                    chip_info->gesture_buf[chip_info->geture_points_count * 4 + 2] = event_buff[i][2];
                    chip_info->gesture_buf[chip_info->geture_points_count * 4 + 3] = event_buff[i][3];
                    chip_info->geture_points_count++;
                }
            }
#endif // end of CONFIG_OPLUS_TP_APK

        }
    }

    return obj_attention;
}

#ifdef CONFIG_TOUCHPANEL_ALGORITHM
// only do finger one
static int sec_get_touch_points_specail(void *chip_data, struct point_info *points, int max_num)
{
    int t_id = 0;
    struct sec_event_coordinate *p_event_coord = NULL;
    uint32_t obj_attention = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    u8 t_type;

    p_event_coord = (struct sec_event_coordinate *)chip_info->first_event;
    t_id = (p_event_coord->tid - 1);
    if ((t_id < max_num) && (p_event_coord->tchsta == SEC_COORDINATE_ACTION_RELEASE)) {
        t_type = p_event_coord->ttype_3_2*4+p_event_coord->ttype_1_0;
        if (t_type == TYPE_SPECIAL_RELEASE) {
            points[t_id].x = (p_event_coord->x_11_4 << 4) | (p_event_coord->x_3_0);
            points[t_id].y = (p_event_coord->y_11_4 << 4) | (p_event_coord->y_3_0);
            points[t_id].z = p_event_coord->z & 0x3F;
            points[t_id].width_major = p_event_coord->major;
            points[t_id].touch_major = p_event_coord->major;
            points[t_id].status = 1;

            if (points[t_id].z == 0) {
                points[t_id].z = 1;
            }

            obj_attention = obj_attention | (1 << t_id);    //set touch bit
        }


    }

    return obj_attention;
}
#endif


static int sec_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
    int i = 0, ret = -1;
    uint8_t coord[18] = {0};
    struct Coordinate limitPoint[4];
    struct sec_gesture_status *p_event_gesture = NULL;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
	bool ear_sense_support_b = 0;
	struct touchpanel_data *ts = i2c_get_clientdata(chip_info->client);

	if (ts) {
		ear_sense_support_b = ts->ear_sense_support;
	}

    p_event_gesture = (struct sec_gesture_status *)chip_info->first_event;
    if (p_event_gesture->coordLen > 18) {
        p_event_gesture->coordLen = 18;
    }

	if ((p_event_gesture->gestureId == GESTURE_EARSENSE) && (ear_sense_support_b)) {
        TPD_DETAIL("earsense gesture: away from panel\n");
        return 0;
    }
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_GESTURE_EVENT, p_event_gesture->coordLen, coord);
    if (ret < 0) {
        TPD_INFO("%s: read gesture data failed\n", __func__);
    }

    if (LEVEL_BASIC != tp_debug) {
        TPD_INFO("gesture points:");
        for (i = 0; i < p_event_gesture->coordLen / 3; i++) {
            printk("(%d, %d) ", (coord[3 * i] << 4) | ((coord[3 * i + 2] >> 0) & 0x0F), (coord[3 * i + 1] << 4) | ((coord[3 * i + 2] >> 4) & 0x0F));
        }
    }

	switch (p_event_gesture->gestureId) {   /*judge gesture type*/
    case GESTURE_RIGHT:
        gesture->gesture_type  = Left2RightSwip;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_LEFT:
        gesture->gesture_type  = Right2LeftSwip;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_DOWN:
        gesture->gesture_type  = Up2DownSwip;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_UP:
        gesture->gesture_type  = Down2UpSwip;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_DOUBLECLICK:
        gesture->gesture_type  = DouTap;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end     = gesture->Point_start;
        break;

    case GESTURE_UP_V:
        gesture->gesture_type  = UpVee;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_DOWN_V:
        gesture->gesture_type  = DownVee;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_LEFT_V:
        gesture->gesture_type = LeftVee;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_RIGHT_V:
        gesture->gesture_type  = RightVee;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        break;

    case GESTURE_O:
        gesture->gesture_type = Circle;
        gesture->clockwise = (p_event_gesture->data == 0) ? 1 : 0;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        limitPoint[0].x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);    //ymin
        limitPoint[0].y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        limitPoint[1].x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);    //xmin
        limitPoint[1].y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        limitPoint[2].x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);   //ymax
        limitPoint[2].y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
        limitPoint[3].x   = (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);  //xmax
        limitPoint[3].y   = (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[15] << 4) | ((coord[17] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[16] << 4) | ((coord[17] >> 0) & 0x0F);
        handleFourCornerPoint(&limitPoint[0], 4);
        gesture->Point_1st = limitPoint[0]; //ymin
        gesture->Point_2nd = limitPoint[1]; //xmin
        gesture->Point_3rd = limitPoint[2]; //ymax
        gesture->Point_4th = limitPoint[3]; //xmax
        break;

    case GESTURE_DOUBLE_LINE:
        gesture->gesture_type  = DouSwip;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        gesture->Point_1st.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
        gesture->Point_1st.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        gesture->Point_2nd.x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
        gesture->Point_2nd.y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
        break;

    case GESTURE_M:
        gesture->gesture_type  = Mgestrue;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        gesture->Point_2nd.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
        gesture->Point_2nd.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        gesture->Point_3rd.x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
        gesture->Point_3rd.y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
        break;

    case GESTURE_W:
        gesture->gesture_type  = Wgestrue;
        gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
        gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
        gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
        gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
        gesture->Point_2nd.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
        gesture->Point_2nd.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
        gesture->Point_3rd.x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
        gesture->Point_3rd.y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
        gesture->Point_end.x   = (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);
        gesture->Point_end.y   = (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
        break;
	case GESTURE_SINGLE_TAP:
		gesture->gesture_type  = SingleTap;
		gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		break;
	case GESTURE_S:
		gesture->gesture_type  = SGESTRUE;
		gesture->Point_start.x = (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y = (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_1st.x   = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y   = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		gesture->Point_2nd.x   = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_2nd.y   = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_end.x   = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
		gesture->Point_end.y   = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
		break;
    default:
        gesture->gesture_type = UnkownGesture;
        break;
    }
#ifdef CONFIG_OPLUS_TP_APK
    chip_info->debug_gesture_type = gesture->gesture_type;
#endif
    TPD_INFO("%s, gesture_id: 0x%x, gesture_type: %d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n", \
             __func__, p_event_gesture->gestureId, gesture->gesture_type, gesture->clockwise, \
             gesture->Point_start.x, gesture->Point_start.y, \
             gesture->Point_end.x, gesture->Point_end.y, \
             gesture->Point_1st.x, gesture->Point_1st.y, \
             gesture->Point_2nd.x, gesture->Point_2nd.y, \
             gesture->Point_3rd.x, gesture->Point_3rd.y, \
             gesture->Point_4th.x, gesture->Point_4th.y);

    return 0;
}

static void sec_change_to_np_mode(void *chip_data)
{
    int ret = -1;
    int i = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    if (!*chip_info->in_suspend) {
        for (i = 0; i < 20; i++) {
            ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_SET_POWER_MODE);
            if (0x00 == ret)
                break;
            touch_i2c_write_byte(chip_info->client, SEC_CMD_SET_POWER_MODE, 0x00);
            sec_mdelay(10);
        }
        TPD_INFO("%s: change to np_mode time: %d %s!\n", __func__, i, ret < 0 ? "failed" : "success");
    }
}

static void sec_enable_fingerprint_mode(void *chip_data, uint32_t enable)
{
    int ret = -1;
    int i = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    if (chip_info->is_power_down) {
        sec_power_control(chip_info, true);
    }

    if (enable == 1) {
        for(i = 0; i < 10; i++) {
            ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_TOUCHHOLD_SWITCH);
            ret |= 0x01;
            ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_TOUCHHOLD_SWITCH, ret);
            sec_mdelay(10);
            ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_TOUCHHOLD_SWITCH);
            if (ret == 1)
                break;
        }
    } else if (enable == 0) {
        ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_TOUCHHOLD_SWITCH);
        ret &= 0xFE;
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_TOUCHHOLD_SWITCH, ret);
        sec_change_to_np_mode(chip_data);

        chip_info->fp_info.touch_state = 0;
    }
    TPD_INFO("%s: touchhold_enable: %d %s!\n", __func__, enable, ret < 0 ? "failed" : "success");
	/*old_firmware_flag_check for old 19813 project not support SEC_QUICK_LAUNCH_ENABLE*/
	if (*chip_info->fp_enable == 2 && !chip_info->old_firmware_flag_check) {
       ret = touch_i2c_write_byte(chip_info->client, SEC_QUICK_LAUNCH_ENABLE, 1);
       sec_mdelay(10);
       ret = touch_i2c_read_byte(chip_info->client, SEC_QUICK_LAUNCH_ENABLE);
    } else {
       ret = touch_i2c_write_byte(chip_info->client, SEC_QUICK_LAUNCH_ENABLE, 0);
    }
    TPD_INFO("%s: quick_launch_enable: %d %s!\n", __func__, *chip_info->fp_enable, ret < 0 ? "failed" : "success");
    return;
}

static void sec_get_fingerprint_info(void *chip_data, struct fp_underscreen_info *fp_tpinfo)
{
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    fp_tpinfo->touch_state = chip_info->fp_info.touch_state;
    fp_tpinfo->x = chip_info->fp_info.x;
    fp_tpinfo->y = chip_info->fp_info.y;

    if (chip_info->fp_info.touch_state == FINGERPRINT_UP_DETECT) {
        sec_change_to_np_mode(chip_data);

    }
}

static int sec_mode_switch(void *chip_data, work_mode mode, bool flag)
{
    int ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    if (chip_info->is_power_down) {
        sec_power_control(chip_info, true);
    }

	switch(mode) {
	case MODE_NORMAL:
	    ret = 0;
	    break;

	case MODE_SLEEP:
	    ret = sec_power_control(chip_info, false);
	    if (ret < 0) {
	        TPD_INFO("%s: power down failed\n", __func__);
	    }
	    break;

	case MODE_GESTURE:
	    ret = sec_enable_black_gesture(chip_info, flag);
	    if (ret < 0) {
	        TPD_INFO("%s: sec enable gesture failed.\n", __func__);
	        return ret;
	    }
	    break;

	case MODE_EDGE:
	    /*ret = sec_enable_edge_limit(chip_info, flag);
	        if (ret < 0) {
	        TPD_INFO("%s: sec enable edg limit failed.\n", __func__);
	        return ret;
	     }*/
	    break;

	case MODE_CHARGE:
	    ret = sec_enable_charge_mode(chip_info, flag);
	    if (ret < 0) {
	        TPD_INFO("%s: enable charge mode : %d failed\n", __func__, flag);
	    }
	    break;

	case MODE_EARSENSE:
	    ret = sec_enable_earsense_mode(chip_info, flag);
	    if (ret < 0) {
	        TPD_INFO("%s: enable earsense mode : %d failed\n", __func__, flag);
	    }
	    break;

	case MODE_PALM_REJECTION:
	    ret = sec_enable_palm_reject(chip_info, flag);
	    if (ret < 0) {
	        TPD_INFO("%s: enable palm rejection: %d failed\n", __func__, flag);
	    }
	    break;

	case MODE_GAME:
	    ret = sec_enable_game_mode(chip_info, flag);
	    if (ret < 0) {
	        TPD_INFO("%s: enable game mode: %d failed\n", __func__, flag);
	    }
	    break;
	case MODE_HEADSET:
	    ret = sec_enable_headset_mode(chip_info, flag);
	    if (ret < 0) {
	        TPD_INFO("%s: enable headset mode: %d failed\n", __func__, flag);
	    }
	    break;

    default:
        TPD_INFO("%s: Wrong mode.\n", __func__);
    }

    return ret;
}

//#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
//extern unsigned int upmu_get_rgs_chrdet(void);
//static int sec_get_usb_state(void)
//{
//    return upmu_get_rgs_chrdet();
//}
//#else
//static int sec_get_usb_state(void)
//{
//   return 0;
//}
//#endif

static void sec_set_touch_direction(void *chip_data, uint8_t dir)
{
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    chip_info->touch_direction = dir;
}

static uint8_t sec_get_touch_direction(void *chip_data)
{
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    return chip_info->touch_direction;
}

static void sec_rate_white_list_ctrl(void *chip_data, int value)
{
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    if (value>=0&&value<255) {
        int ret = -1;
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_GAME_MODE, (unsigned char)value);
        TPD_INFO("%s: state: %d %s!\n", __func__, value, ret < 0 ? "failed" : "success");

    }

}

static struct oplus_touchpanel_operations sec_ops = {
    .ftm_process                = sec_ftm_process,
    .get_vendor                 = sec_get_vendor,
    .get_chip_info              = sec_get_chip_info,
    .reset                      = sec_reset,
    .power_control              = sec_power_control,
    .fw_check                   = sec_fw_check,
    .fw_update                  = sec_fw_update,
    .u32_trigger_reason         = sec_trigger_reason,
    .get_touch_points           = sec_get_touch_points,
    .get_gesture_info           = sec_get_gesture_info,
    .mode_switch                = sec_mode_switch,
//    .get_usb_state              = sec_get_usb_state,
    .set_touch_direction        = sec_set_touch_direction,
    .get_touch_direction        = sec_get_touch_direction,
    .enable_fingerprint         = sec_enable_fingerprint_mode,
    .screenon_fingerprint_info  = sec_get_fingerprint_info,
    .enable_gesture_mask        = sec_enable_gesture_mask,
    .rate_white_list_ctrl       = sec_rate_white_list_ctrl,
#ifdef CONFIG_TOUCHPANEL_ALGORITHM
    .special_points_report      = sec_get_touch_points_specail,
#endif
	.calibrate                = sec_calibrate,
	.get_cal_status                = sec_get_cal_status,
};
/********* End of implementation of oplus_touchpanel_operations callbacks**********************/


/**************** Start of implementation of debug_info proc callbacks************************/
static int sec_fix_tmode(struct chip_data_s6sy791 *chip_info, u8 mode, u8 state)
{
    int ret = -1;
    u8 tBuff[2] = { mode, state };

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_STATEMANAGE_ON, STATE_MANAGE_OFF);
    sec_mdelay(20);
    ret |= touch_i2c_write_block(chip_info->client, SEC_CMD_CHG_SYSMODE, sizeof(tBuff), tBuff);
    sec_mdelay(20);

    return ret;
}

static int sec_release_tmode(struct chip_data_s6sy791 *chip_info)
{
    int ret = -1;

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_STATEMANAGE_ON, STATE_MANAGE_ON);
    sec_mdelay(20);

    return ret;
}

static int sec_read_self(struct chip_data_s6sy791 *chip_info, u8 type, char *data, int len)
{
    int ret = 0;
    unsigned int data_len = (chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM) * 2;

    if (len != data_len) {
        return -1;
    }

    ret = sec_fix_tmode(chip_info, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
    if (ret < 0) {
        TPD_INFO("%s: fix touch mode failed\n", __func__);
        goto err_out;
    }

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SELF_RAW_TYPE, type);
    if (ret < 0) {
        TPD_INFO("%s: Set self type failed\n", __func__);
        goto err_out;
    }

    sec_mdelay(50);
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_SELF_RAWDATA, data_len, data);
    if (ret < 0) {
        TPD_INFO("%s: read self failed!\n", __func__);
    }

    /* release data monitory (unprepare AFE data memory) */
    ret |= touch_i2c_write_byte(chip_info->client, SEC_CMD_SELF_RAW_TYPE, TYPE_INVALID_DATA);
    if (ret < 0) {
        TPD_INFO("%s: Set self type failed\n", __func__);
    }

err_out:
    ret |= sec_release_tmode(chip_info);

    return ret;
}

static int sec_read_mutual(struct chip_data_s6sy791 *chip_info, u8 type, char *data, int len)
{
    int ret = 0;
    //u8 buf[2] = {0};
    unsigned int data_len = (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) * 2;

    if ((len > data_len) || (len % chip_info->hw_res->TX_NUM != 0)) {
        return -1;
    }

    ret = sec_fix_tmode(chip_info, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
    if (ret < 0) {
        TPD_INFO("%s: fix touch mode failed\n", __func__);
        goto err_out;
    }

    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, type);
    if (ret < 0) {
        TPD_INFO("%s: Set mutual type failed\n", __func__);
        goto err_out;
    }

    sec_mdelay(20);
    //buf[0] = (u8)((len >> 8) & 0xFF);
    //buf[1] = (u8)(len & 0xFF);
    //touch_i2c_write_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_SETLEN, 2, buf);
    ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_RAWDATA, len, data);
    if (ret < 0) {
        TPD_INFO("%s: read mutual failed!\n", __func__);
    }

    /* release data monitory (unprepare AFE data memory) */
    ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_INVALID_DATA);
    if (ret < 0) {
        TPD_INFO("%s: Set mutual type failed\n", __func__);
    }

err_out:
    ret |= sec_release_tmode(chip_info);

    return ret;
}

static void sec_delta_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, y = 0, z = 0, temp_delta = 0, ret = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    int readbytes = chip_info->hw_res->TX_NUM * (chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    memset(data, 0, readbytes);
    ret = sec_read_mutual(chip_info, TYPE_SIGNAL_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read delta failed\n");
        goto kfree_out;
    }

    seq_printf(s, "SIGNAL DATA:");
    for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
        seq_printf(s, "\n[%2d]", x);
        for (y = chip_info->hw_res->RX_NUM - 1; y >= 0; y--) {
            z = chip_info->hw_res->RX_NUM * x + y;
            temp_delta = ((data[z * 2] << 8) | data[z * 2 + 1]);
            seq_printf(s, "%4d, ", temp_delta);
        }
    }
    seq_printf(s, "\n");

    //read ambient data
    memset(data, 0, readbytes);
    ret = sec_read_mutual(chip_info, TYPE_AMBIENT_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read rawdata failed\n");
        goto kfree_out;
    }

    seq_printf(s, "AMBIENT DATA:");
    for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
        seq_printf(s, "\n[%2d]", x);
        for (y = chip_info->hw_res->RX_NUM - 1; y >= 0; y--) {
            z = chip_info->hw_res->RX_NUM * x + y;
            temp_delta = (data[z * 2] << 8) | data[z * 2 + 1];
            seq_printf(s, "%4d, ", temp_delta);
        }
    }
    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_baseline_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, y = 0, z = 0, temp_delta = 0, ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    int readbytes = (chip_info->hw_res->TX_NUM * chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    //read decoded data
    memset(data, 0, readbytes);
    ret = sec_read_mutual(chip_info, TYPE_DECODED_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read rawdata failed\n");
        goto kfree_out;
    }

    seq_printf(s, "DECODED DATA:");
    for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
        seq_printf(s, "\n[%2d]", x);
        for (y = chip_info->hw_res->RX_NUM - 1; y >= 0; y--) {
            z = chip_info->hw_res->RX_NUM * x + y;
            temp_delta = (data[z * 2] << 8) | data[z * 2 + 1];
            seq_printf(s, "%4d, ", temp_delta);
        }
    }
    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_self_delta_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, rx_offset = 0, temp_delta = 0, ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    int readbytes = (chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    memset(data, 0, readbytes);
    ret = sec_read_self(chip_info, TYPE_SIGNAL_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read self delta failed\n");
        goto kfree_out;
    }

    seq_printf(s, "TX:\n");
    for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
        temp_delta = (data[x * 2] << 8) | data[x * 2 + 1];
        seq_printf(s, "%4d, ", temp_delta);
    }

    seq_printf(s, "\nRX:\n");
    rx_offset = chip_info->hw_res->TX_NUM * 2;
    for (x = 0; x < chip_info->hw_res->RX_NUM; x++) {
        temp_delta = (data[x * 2 + rx_offset] << 8) | data[x * 2 + 1 + rx_offset];
        seq_printf(s, "%4d, ", temp_delta);
    }
    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_self_raw_read(struct seq_file *s, void *chip_data)
{
    u8 *data = NULL;
    int16_t x = 0, rx_offset = 0, temp_delta = 0, ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    int readbytes = (chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM) * 2;

    data = kmalloc(readbytes, GFP_KERNEL);
    if (!data) {
        return;
    }

    memset(data, 0, readbytes);
    ret = sec_read_self(chip_info, TYPE_RAW_DATA, data, readbytes);
    if (ret < 0) {
        seq_printf(s, "read self rawdata failed\n");
        goto kfree_out;
    }

    seq_printf(s, "TX:\n");
    for (x = 0; x < chip_info->hw_res->TX_NUM; x++) {
        temp_delta = (data[x * 2] << 8) | data[x * 2 + 1];
        seq_printf(s, "%4d, ", temp_delta);
    }

    seq_printf(s, "\nRX:\n");
    rx_offset = chip_info->hw_res->TX_NUM * 2;
    for (x = 0; x < chip_info->hw_res->RX_NUM; x++) {
        temp_delta = (data[x * 2 + rx_offset] << 8) | data[x * 2 + 1 + rx_offset];
        seq_printf(s, "%4d, ", temp_delta);
    }
    seq_printf(s, "\n");

kfree_out:
    kfree(data);
    return;
}

static void sec_main_register_read(struct seq_file *s, void *chip_data)
{
    u8 buf[4] = {0};
    u8 grip_buf[5] = {0};
    int state = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    state = sec_read_calibration_report(chip_info);
    seq_printf(s, "calibration status: 0x%02x\n", state);

    state = sec_get_verify_result(chip_info);
    seq_printf(s, "calibration result: 0x%02x\n", state);

    state = touch_i2c_read_byte(chip_info->client, SET_CMD_SET_CHARGER_MODE);
    seq_printf(s, "charger state: 0x%02x\n", state);

    state = touch_i2c_read_byte(chip_info->client, SEC_CMD_NOISE_CTRL);
    seq_printf(s, "noise state: 0x%02x(0x10:normal/0x11:noise mode)\n", state);

    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_ID, 3, buf);
    seq_printf(s, "boot state: 0x%02x\n", buf[0]);

    state = touch_i2c_read_byte(chip_info->client, 0x30);
    seq_printf(s, "touch function: 0x%02x(proximity/wetmode/palm/stylus/glove/cover/hover/touch)\n", state);

    state = touch_i2c_read_byte(chip_info->client, SEC_CMD_WET_SWITCH);
    seq_printf(s, "wetmode switch: 0x%02x\n", state);

    state = touch_i2c_read_byte(chip_info->client, 0x3B);
    seq_printf(s, "wetmode state: 0x%02x\n", state);

    state = touch_i2c_read_word(chip_info->client, SEC_CMD_WAKEUP_GESTURE_MODE);
    seq_printf(s, "gesture mode: 0x%04x\n", state);

    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, buf);
    seq_printf(s, "fw img version: 0x%08x\n", (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);

    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_CONFIG_VERSION, 4, buf);
    seq_printf(s, "config version: 0x%08x\n", (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);

    state = touch_i2c_read_byte(chip_info->client, SEC_CMD_STATEMANAGE_ON);
    seq_printf(s, "auto change enabled: 0x%02x\n", state);

    state = touch_i2c_read_word(chip_info->client, SEC_CMD_PALM_SWITCH);
    seq_printf(s, "palm state: 0x%04x(0x0041-off/0x0061-on)\n", state);

    memset(buf, 0, 4);
    touch_i2c_read_block(chip_info->client, SEC_READ_TS_STATUS, 4, buf);
    seq_printf(s, "power mode: 0x%02x[normal-0x02/lpwg-0x05], 0x%02x[indle-0x00/active-0x02]\n", buf[1], buf[3]);


    memset(grip_buf, 0, 5);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_ENABLE, 1, grip_buf);
    seq_printf(s, "grip enable bit : 0x%02x\n", grip_buf[0]);

    memset(grip_buf, 0, 5);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_DIRECTION, 1, grip_buf);
    seq_printf(s, "touch direction(0x00 (portrait mode), 0x01 (landscape mode), 0x02 (reverse landscape mode), 0x03 (reverse portrait mode)) : 0x%02x\n", grip_buf[0]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_AREA, DEAD_ZONE_TYPE_1);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_AREA, 5, grip_buf);
    seq_printf(s, "dead zone X: 0x%02x 0x%02x, Y: 0x%02x 0x%02x\n", grip_buf[1], grip_buf[2], grip_buf[3], grip_buf[4]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_AREA, DEAD_ZONE_TYPE_2);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_AREA, 5, grip_buf);
    seq_printf(s, "corner triangle dead zone X: 0x%02x 0x%02x, Y: 0x%02x 0x%02x\n", grip_buf[1], grip_buf[2], grip_buf[3], grip_buf[4]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_AREA, LONG_PRESS_ZONE);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_AREA, 5, grip_buf);
    seq_printf(s, "conditon long press zone X: 0x%02x 0x%02x, Y: 0x%02x 0x%02x\n", grip_buf[1], grip_buf[2], grip_buf[3], grip_buf[4]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_PARA, LONG_PRESS_ZONE);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_PARA, 3, grip_buf);
    seq_printf(s, "conditon long press zone delay time: 0x%02x , drag distance: 0x%02x\n", grip_buf[1], grip_buf[2]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_AREA, LARGE_TOUCH_REJECTION);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_AREA, 5, grip_buf);
    seq_printf(s, "large touch zone X: 0x%02x 0x%02x, Y: 0x%02x 0x%02x\n", grip_buf[1], grip_buf[2], grip_buf[3], grip_buf[4]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_PARA, LARGE_TOUCH_REJECTION);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_PARA, 3, grip_buf);
    seq_printf(s, "large touch zone delay time: 0x%02x , touch size: 0x%02x\n", grip_buf[1], grip_buf[2]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_AREA, CORNER_LONG_PRESS_ZONE);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_AREA, 5, grip_buf);
    seq_printf(s, "corner long press zone X: 0x%02x 0x%02x, Y: 0x%02x 0x%02x\n", grip_buf[1], grip_buf[2], grip_buf[3], grip_buf[4]);

    memset(grip_buf, 0, 5);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_GRIP_PARA, CORNER_LONG_PRESS_ZONE);
    touch_i2c_read_block(chip_info->client, SEC_CMD_GRIP_PARA, 3, grip_buf);
    seq_printf(s, "corner long press zone delay time: 0x%02x\n", grip_buf[1]);

    memset(grip_buf, 0, 5);
    touch_i2c_read_block(chip_info->client, SEC_CMD_EDGE_SCREEN, 4, grip_buf);
    seq_printf(s, "grip edage screen zone: 0x%02x, 0x%02x, 0x%02x direction: 0x%02x\n", grip_buf[0], grip_buf[1], grip_buf[2], grip_buf[3]);

    return;
}

static void sec_rtdp_start(struct chip_data_s6sy791 *chip_info)
{
    int ret = -1;
    u8 tpara[4] = {0x0B, 0x06, 0x64, 0x06};    //scan type, mutual rawdata type, trigger threshold, self rawdata type

    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_RTDP_ERASE, 0, NULL);
    if (ret < 0) {
        TPD_INFO("%s: erase rtdp memory failed(0x%02x)\n", __func__, ret);
    }
    sec_mdelay(1000);
    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_RTDP_START, 4, tpara);
    if (ret < 0) {
        TPD_INFO("%s: start rtdp failed(0x%02x)\n", __func__, ret);
    }
    sec_mdelay(1000);
}

static int sec_rtdp_read_event(struct chip_data_s6sy791 *chip_info)
{
    int ret = -1;

    ret = touch_i2c_read_byte(chip_info->client, SEC_READ_RTDP_RESULT);
    return ret;
}

static void sec_rtdp_dump(struct seq_file *s, struct chip_data_s6sy791 *chip_info)
{
    uint8_t temp[2] = {0};
    uint8_t *pRead = NULL;
    int16_t nodeData = 0;
    int ret = -1, iArrayIndex = 0, framenum = 0, i = 0, j = 0, fnum = 0;
    int tx_num = chip_info->hw_res->TX_NUM;
    int rx_num = chip_info->hw_res->RX_NUM;
    const uint32_t readbytes = (tx_num + 1) * rx_num * 2;
    const uint32_t readselfbytes = ((tx_num + 1) + rx_num) * 2;

    pRead = kzalloc(readbytes + readselfbytes, GFP_KERNEL);
    if (!pRead) {
        TPD_INFO("kzalloc space failed\n");
        return;
    }

    ret = touch_i2c_write_block(chip_info->client, SEC_CMD_RTDP_INIT_PTR, 0, NULL);
    if (ret < 0) {
        TPD_INFO("%s: init rtdp read pointer failed\n", __func__);
    }

    ret = touch_i2c_read_block(chip_info->client, SEC_READ_RTDP_FRAMENUM, 2, temp);
    if (ret < 0) {
        TPD_INFO("%s: read rtdp frame num failed\n", __func__);
    }

    framenum = temp[0] | temp[1] << 8;
    for (fnum = 0; fnum < framenum; fnum++) {
        ret = touch_i2c_read_block(chip_info->client, SEC_READ_RTDP_DATA, readbytes + readselfbytes, pRead);
        if (ret < 0) {
            TPD_INFO("%s: dump rtdp data failed\n", __func__);
            goto ERR_OUT;
        }
        for (i = 0; i <  tx_num + 1; i++) {
            seq_printf(s, "\n[%2d]", i);
            for (j = 0; j < rx_num; j++) {
                iArrayIndex = i * rx_num + j;
                nodeData = (pRead[iArrayIndex * 2] << 8) | pRead[iArrayIndex * 2 + 1];
                seq_printf(s, "%4d, ", nodeData);
            }
        }
        seq_printf(s, "\n");
        iArrayIndex = (tx_num + 1) * rx_num;
        for (i = 0; i < tx_num + 1; i++) {
            nodeData = (pRead[(iArrayIndex + i) * 2] << 8) | pRead[(iArrayIndex + i) * 2 + 1];
            seq_printf(s, "%4d, ", nodeData);
        }
        seq_printf(s, "\n");
        iArrayIndex = (tx_num + 1) * rx_num + tx_num + 1;
        for (j = 0; j < rx_num; j++) {
            nodeData = (pRead[(iArrayIndex + j) * 2] << 8) | pRead[(iArrayIndex + j) * 2 + 1];
            seq_printf(s, "%4d, ", nodeData);
        }
    }
ERR_OUT:
    if (pRead) {
        kfree(pRead);
        pRead = NULL;
    }
}

static void sec_reserve_read(struct seq_file *s, void *chip_data)
{
    int state = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    state = sec_rtdp_read_event(chip_info);
    if (0x01 == state) {    //enabled, but not ready for reading
        TPD_INFO("read rtdp enabled, but not ready\n");
        return;
    } else if ((0x00 == state) || (0x0A == state)) {    //abnormal, need start rtdp
        TPD_INFO("start rtdp function\n");
        sec_rtdp_start(chip_info);
    } else if (0x0F == state) { //ready
        TPD_INFO("start dump rtdp\n");
        sec_rtdp_dump(s, chip_info);
    }
}

static struct debug_info_proc_operations debug_info_proc_ops = {
    .limit_read         = sec_limit_read,
    .delta_read         = sec_delta_read,
    .self_delta_read    = sec_self_delta_read,
    .baseline_read      = sec_baseline_read,
    .self_raw_read      = sec_self_raw_read,
    .main_register_read = sec_main_register_read,
    .reserve_read       = sec_reserve_read,
	.baseline_blackscreen_read = sec_baseline_read,
};

static void sec_start_aging_test(void *chip_data){
    int ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    TPD_INFO("%s: start aging test \n", __func__);
    ret = sec_fix_tmode(chip_info, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
    if (!ret)
        TPD_INFO("%s: start aging test failed!\n", __func__);
}

static void sec_finish_aging_test(void *chip_data){
    int ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    TPD_INFO("%s: finish aging test \n", __func__);
    ret = sec_release_tmode(chip_info);
    if (!ret)
        TPD_INFO("%s: finish aging test failed!\n", __func__);
}

static struct aging_test_proc_operations aging_test_proc_ops = {
    .start_aging_test   = sec_start_aging_test,
    .finish_aging_test  = sec_finish_aging_test,
};

static void sec_earsese_rawdata_read(void *chip_data, char *rawdata, int read_len)
{
    int ret = 0;
    u8 buf[2] = {0};
    int i = 0, j = 0;
    int8_t tmp_byte[2] = {0};
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    uint8_t len_x = 0, len_y = 0;

    if ((!chip_info) || (!rawdata))
        return ;
    len_x = chip_info->hw_res->EARSENSE_TX_NUM;
    len_y = chip_info->hw_res->EARSENSE_RX_NUM;

    buf[0] = (u8)((read_len >> 8) & 0xFF);
    buf[1] = (u8)(read_len & 0xFF);
    touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_DATA_RAWDATA);
    touch_i2c_write_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_SETLEN, 2, buf);
    ret = touch_i2c_read_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_READ, read_len, rawdata);
    if (ret < 0) {
        TPD_INFO("read rawdata failed\n");
        return;
    }

    for (i = 0; i < len_y; i++) {
        for (j = 0; j < len_x / 2; j++) {
            tmp_byte[0] = rawdata[2 * (len_x * i + j)];
            tmp_byte[1] = rawdata[2 * (len_x * i + j) + 1];
            rawdata[2 * (len_x * i + j)] = rawdata[2 * (len_x * i + len_x - 1 - j) + 1];
            rawdata[2 * (len_x * i + j) + 1] = rawdata[2 * (len_x * i + len_x - 1 - j)];
            rawdata[2 * (len_x * i + len_x - 1 - j)] = tmp_byte[1];
            rawdata[2 * (len_x * i + len_x - 1 - j) + 1] = tmp_byte[0];
        }
    }
    if (len_x % 2) {
        j = len_x / 2;
        for (i = 0; i < len_y; i++) {
            tmp_byte[0] = rawdata[2 * (len_x * i + j)];
            rawdata[2 * (len_x * i + j)] = rawdata[2 * (len_x * i + j) + 1];
            rawdata[2 * (len_x * i + j) + 1] = tmp_byte[0];
        }
    }
    return;
}

static void sec_earsese_delta_read(void *chip_data, char *earsense_delta, int read_len)
{
    int ret = 0, hover_status = 0, data_type = 0;
    u8 buf[2] = {0};
    int i = 0, j = 0;
    int8_t tmp_byte[2] = {0};
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    uint8_t len_x = 0, len_y = 0;

    if ((!chip_info) || (!earsense_delta))
        return ;
    len_x = chip_info->hw_res->EARSENSE_TX_NUM;
    len_y = chip_info->hw_res->EARSENSE_RX_NUM;

    if (!earsense_delta) {
        TPD_INFO("earsense_delta is NULL\n");
        return;
    }

    buf[0] = (u8)((read_len >> 8) & 0xFF);
    buf[1] = (u8)(read_len & 0xFF);
    hover_status = touch_i2c_read_byte(chip_info->client, SEC_CMD_HOVER_DETECT); //read hover state
    data_type = touch_i2c_read_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE); //read current data type
    if (hover_status && (data_type != TYPE_DATA_DELTA)) {
        touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_DATA_DELTA);
        sec_mdelay(20);
    } else if (!hover_status && (data_type != TYPE_SIGNAL_DATA)) {
        touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, TYPE_SIGNAL_DATA);
        sec_mdelay(20);
    }

    touch_i2c_write_block(chip_info->client, SEC_CMD_TOUCH_RAWDATA_SETLEN, 2, buf);
    ret = touch_i2c_read_block(chip_info->client, SEC_CMD_TOUCH_DELTA_READ, read_len, earsense_delta);
    if (ret < 0) {
        TPD_INFO("read delta failed\n");
        return;
    }

    for (i = 0; i < len_y; i++) {
        for (j = 0; j < len_x / 2; j++) {
            tmp_byte[0] = earsense_delta[2 * (len_x * i + j)];
            tmp_byte[1] = earsense_delta[2 * (len_x * i + j) + 1];
            earsense_delta[2 * (len_x * i + j)] = earsense_delta[2 * (len_x * i + len_x - 1 - j) + 1];
            earsense_delta[2 * (len_x * i + j) + 1] = earsense_delta[2 * (len_x * i + len_x - 1 - j)];
            earsense_delta[2 * (len_x * i + len_x - 1 - j)] = tmp_byte[1];
            earsense_delta[2 * (len_x * i + len_x - 1 - j) + 1] = tmp_byte[0];
        }
    }
    if (len_x % 2) {
        j = len_x / 2;
        for (i = 0; i < len_y; i++) {
            tmp_byte[0] = earsense_delta[2 * (len_x * i + j)];
            earsense_delta[2 * (len_x * i + j)] = earsense_delta[2 * (len_x * i + j) + 1];
            earsense_delta[2 * (len_x * i + j) + 1] = tmp_byte[0];
        }
    }
    return;
}

static void sec_earsese_selfdata_read( void *chip_data, char *self_data, int read_len)
{
    int i = 0, ret = 0;
    int8_t tmp = 0;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    if ((!chip_info) || (!self_data))
        return ;

    ret = touch_i2c_read_block(chip_info->client, SEC_CMD_TOUCH_SELFDATA_READ, read_len, self_data);
    if (ret < 0) {
        TPD_INFO("read selfdata failed\n");
        return;
    }

    for (i = 0; i < chip_info->hw_res->TX_NUM + chip_info->hw_res->RX_NUM; i++) {
        tmp = self_data[2 * i];
        self_data[2 * i] = self_data[2 * i + 1];
        self_data[2 * i + 1] = tmp;
    }
    return;
}


static struct earsense_proc_operations earsense_proc_ops = {
    .rawdata_read = sec_earsese_rawdata_read,
    .delta_read   = sec_earsese_delta_read,
    .self_data_read = sec_earsese_selfdata_read,
};
/***************** End of implementation of debug_info proc callbacks*************************/
static void sec_swap(u8 *a, u8 *b)
{
    u8 temp = *a;
    *a = *b;
    *b = temp;
}

static void rearrange_sft_result(u8 *data, int length)
{
    int i = 0;

    for(i = 0; i < length; i += 4) {
        sec_swap(&data[i], &data[i + 3]);
        sec_swap(&data[i + 1], &data[i + 2]);
    }
}

static void store_to_file(int fd, char *format, ...)
{
    va_list args;
    char buf[64] = {0};

    va_start(args, format);
    vsnprintf(buf, 64, format, args);
    va_end(args);

    if(fd >= 0) {
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
        ksys_write(fd, buf, strlen(buf));
#else
        sys_write(fd, buf, strlen(buf));
#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */
    }
}

static int sec_execute_selftest(struct seq_file *s, int fd, struct chip_data_s6sy791 *chip_info, struct sec_testdata *sec_testdata)
{
    int rc = -1;
    u8 tpara[2] = {0x25, 0x40};
    u8 *rBuff = NULL;
    int i;
    int result_size = SEC_SELFTEST_REPORT_SIZE + sec_testdata->TX_NUM * sec_testdata->RX_NUM * 2;

    /* save selftest result in flash */
	if (chip_info->old_firmware_flag_check) {
	    tpara[0] = 0x21;
	} else {
		tpara[0] = 0x27;
	}

    rBuff = kzalloc(result_size, GFP_KERNEL);
    if (!rBuff) {
        seq_printf(s, "kzalloc space failed\n");
        return -ENOMEM;
    }

    //send self test cmd
    rc = touch_i2c_write_block(chip_info->client, SEC_CMD_SELFTEST, 2, tpara);
    if (rc < 0) {
        TPD_INFO("%s: Send selftest cmd failed!\n", __func__);
        seq_printf(s, "Send selftest cmd failed!\n");
        goto ERR_EXIT;
    }

    sec_mdelay(350);

    rc = sec_wait_for_ready(chip_info, SEC_VENDOR_ACK_SELF_TEST_DONE);
    if (rc < 0) {
        TPD_INFO("%s: Selftest execution time out!\n", __func__);
        seq_printf(s, "Selftest execution time out!\n");
        goto ERR_EXIT;
    }

    rc = touch_i2c_read_block(chip_info->client, SEC_READ_SELFTEST_RESULT, result_size, rBuff);
    if (rc < 0) {
        TPD_INFO("%s: read selftest relest failed\n", __func__);
        seq_printf(s, "read selftest relest failed\n");
        goto ERR_EXIT;
    }
    rearrange_sft_result(rBuff, result_size);

    store_to_file(fd, "sec_ts : \n");
    for (i = 0; i < 80; i += 4) {
        if (i / 4 == 0) store_to_file(fd, "SIG: ");
        else if (i / 4 == 1) store_to_file(fd, "VER: ");
        else if (i / 4 == 2) store_to_file(fd, "SIZ: ");
        else if (i / 4 == 3) store_to_file(fd, "CRC: ");
        else if (i / 4 == 4) store_to_file(fd, "RES: ");
        else if (i / 4 == 5) store_to_file(fd, "COU: ");
        else if (i / 4 == 6) store_to_file(fd, "PAS: ");
        else if (i / 4 == 7) store_to_file(fd, "FAI: ");
        else if (i / 4 == 8) store_to_file(fd, "CHA: ");
        else if (i / 4 == 9) store_to_file(fd, "AMB: ");
        else if (i / 4 == 10) store_to_file(fd, "RXS: ");
        else if (i / 4 == 11) store_to_file(fd, "TXS: ");
        else if (i / 4 == 12) store_to_file(fd, "RXO: ");
        else if (i / 4 == 13) store_to_file(fd, "TXO: ");
        else if (i / 4 == 14) store_to_file(fd, "RXG: ");
        else if (i / 4 == 15) store_to_file(fd, "TXG: ");
        else if (i / 4 == 16) store_to_file(fd, "RXR: ");
        else if (i / 4 == 17) store_to_file(fd, "TXT: ");
        else if (i / 4 == 18) store_to_file(fd, "RXT: ");
        else if (i / 4 == 19) store_to_file(fd, "TXR: ");

        store_to_file(fd, "0x%02X, 0x%02X, 0x%02X, 0x%02X \n", rBuff[i], rBuff[i + 1], rBuff[i + 2], rBuff[i + 3]);
        if (i / 4 == 4) {
            /* RX, RX open check. */
            if ((rBuff[i + 3] & 0x30) != 0) {
                rc = 0;
                seq_printf(s, "RX, RX open check failed\n");
            }
            /* TX, RX GND(VDD) short check. */
            else if ((rBuff[i + 3] & 0xC0) != 0) {
                rc = 0;
                seq_printf(s, "TX, RX GND(VDD) short check failed\n");
            }
            /* RX-RX, TX-TX short check. */
            else if ((rBuff[i + 2] & 0x03) != 0) {
                rc = 0;
                seq_printf(s, "RX-RX, TX-TX short check failed\n");
            }
            /* TX-RX short check. */
            else if ((rBuff[i + 2] & 0x04) != 0) {
                rc = 0;
                seq_printf(s, "TX-RX short check failed\n");
            } else
                rc = 1;
        }
    }
    store_to_file(fd, "\n");

ERR_EXIT:
    kfree(rBuff);
    return rc;
}

static int sec_execute_p2ptest(struct seq_file *s, struct chip_data_s6sy791 *chip_info, struct sec_testdata *sec_testdata)
{
    int rc;
    u8 tpara[2] = {0x0F, 0x11};

    TPD_INFO("%s: P2P test start!\n", __func__);
    rc = touch_i2c_write_block(chip_info->client, SEC_CMD_SET_P2PTEST_MODE, 2, tpara);
    if (rc < 0) {
        seq_printf(s, "%s: Send P2Ptest Mode cmd failed!\n", __func__);
        goto err_exit;
    }

    sec_mdelay(15);

    tpara[0] = 0x00;
    tpara[1] = 0x64;
    rc = touch_i2c_write_block(chip_info->client, SEC_CMD_P2PTEST, 2, tpara);
    if (rc < 0) {
        seq_printf(s, "%s: Send P2Ptest cmd failed!\n", __func__);
        goto err_exit;
    }

    sec_mdelay(1000);

    rc = sec_wait_for_ready(chip_info, SEC_VENDOR_ACK_P2P_TEST_DONE);
    if (rc < 0) {
        seq_printf(s, "%s: P2Ptest execution time out!\n", __func__);
        goto err_exit;
    }

    TPD_INFO("%s: P2P test done!\n", __func__);

err_exit:
    return rc;
}

static uint32_t search_for_item(const struct firmware *fw, int item_cnt, uint8_t item_index)
{
    int i = 0;
    uint32_t item_offset = 0;
    struct sec_test_item_header *item_header = NULL;
    uint32_t *p_item_offset = (uint32_t *)(fw->data + 16);

    for (i = 0; i < item_cnt; i++) {
        item_header = (struct sec_test_item_header *)(fw->data + p_item_offset[i]);
        if (item_header->item_bit == item_index) {      //check the matched item offset
            item_offset = p_item_offset[i];
        }
    }

    return item_offset;
}

static void sec_auto_test(struct seq_file *s, void *chip_data, struct sec_testdata *sec_testdata)
{
    u8 type = 0;
    uint32_t err_cnt = 0, item_offset = 0;
    uint8_t *pRead = NULL;
    int eint_status = 0, eint_count = 0, read_gpio_num = 0;
    int16_t nodeData = 0, Buff16 = 0, Buff16_2 = 0, nodeGap = 0;
    int i = 0, j = 0, item_cnt = 0, iArrayIndex = 0, iArrayIndex_2 = 0, ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;
    const uint32_t readbytes = sec_testdata->TX_NUM * sec_testdata->RX_NUM * 2;
    const uint32_t readselfbytes = (sec_testdata->TX_NUM + sec_testdata->RX_NUM) * 2;
    struct sec_test_item_header *item_header = NULL;
    int32_t *p_mutual_p = NULL, *p_mutual_n = NULL, *p_mutualGap_p = NULL, *p_mutualGap_n = NULL;
    int32_t *p_tx_offset_p = NULL, *p_tx_offset_n = NULL, *p_rx_offset_p = NULL, *p_rx_offset_n = NULL;
    int32_t *p_p2p_p = NULL, *p_p2p_n = NULL;


    //interrupt pin short test
    read_gpio_num = 10;
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 1);   //disable interrupt
    touch_i2c_write_block(chip_info->client, SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);   //clear event buffer
    while (read_gpio_num--) {
        sec_mdelay(5);
        eint_status = gpio_get_value(sec_testdata->irq_gpio);
        if (eint_status == 1) {
            eint_count--;
        } else {
            eint_count++;
        }
    }
    TPD_INFO("TP EINT PIN direct short test! eint_count = %d\n", eint_count);
    if (eint_count == 10) {
        TPD_INFO("error :  TP EINT PIN direct short!\n");
        seq_printf(s, "eint_status is low, TP EINT direct short\n");
        return;
    }
    touch_i2c_write_byte(chip_info->client, SEC_CMD_INTERRUPT_SWITCH, 0);   //enable interrupt

    for (i = 0; i < 8 * sizeof(sec_testdata->test_item); i++) {
        if ((sec_testdata->test_item >> i) & 0x01 ) {
            item_cnt++;
        }
    }

    pRead = kzalloc(readbytes, GFP_KERNEL);
    if (!pRead) {
        TPD_INFO("kzalloc space failed\n");
        seq_printf(s, "kzalloc space failed\n");
        return;
    }

    ret = sec_get_verify_result(chip_info);
    TPD_INFO("%s: calibration verify result(0x%02x)\n", __func__, ret);
    seq_printf(s, "calibration verify result(0x%02x)\n", ret);

    /* execute selftest */
    ret = sec_execute_selftest(s, sec_testdata->fd, chip_info, sec_testdata);
    if (ret <= 0) {
        err_cnt++;
        TPD_INFO("%s: execute selftest failed\n", __func__);
        seq_printf(s, "execute selftest failed\n");
        if (ret < 0)
            goto ERR_OUT;
    }

    //test item mutual_raw offset_data_sec
    if (sec_testdata->test_item & (1 << TYPE_MUTUAL_RAW_OFFSET_DATA_SDC)) {
        TPD_INFO("do test item mutual_raw offset_data_sec\n");

        item_offset = search_for_item(sec_testdata->fw, item_cnt, TYPE_MUTUAL_RAW_OFFSET_DATA_SDC);
        if (item_offset == 0) {
            err_cnt++;
            TPD_INFO("search for item limit offset failed\n");
            seq_printf(s, "search for item limit offset failed\n");
            goto ERR_OUT;
        }
        item_header = (struct sec_test_item_header *)(sec_testdata->fw->data + item_offset);
        if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
            err_cnt++;
            TPD_INFO("test item: %d magic number(%4x) is wrong\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->item_magic);
            seq_printf(s, "test item: %d magic number(%4x) is wrong\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->item_magic);
            goto ERR_OUT;
        }
        if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
            p_mutual_p = (int32_t *)(sec_testdata->fw->data + item_header->top_limit_offset);
            p_mutualGap_p = (int32_t *)(sec_testdata->fw->data + item_header->top_limit_offset + 4 * sec_testdata->TX_NUM * sec_testdata->RX_NUM);
            p_mutual_n = (int32_t *)(sec_testdata->fw->data + item_header->floor_limit_offset);
            p_mutualGap_n = (int32_t *)(sec_testdata->fw->data + item_header->floor_limit_offset + 4 * sec_testdata->TX_NUM * sec_testdata->RX_NUM);
        } else {
            err_cnt++;
            TPD_INFO("item: %d has invalid limit type(%d)\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->item_limit_type);
            seq_printf(s, "item: %d has invalid limit type(%d)\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->item_limit_type);
            goto ERR_OUT;
        }
        if (item_header->para_num == 0) {
        } else {
            err_cnt++;
            TPD_INFO("item: %d has %d parameter\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->para_num);
            seq_printf(s, "item: %d has %d parameter\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->para_num);
            goto ERR_OUT;
        }

        /* set reference data */
        type = TYPE_OFFSET_DATA_SDC;
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, type);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: set OFFSET_DATA_SEC type failed\n", __func__);
            seq_printf(s, "set OFFSET_DATA_SEC type failed\n");
            goto ERR_OUT;
        }

        sec_mdelay(50);
        /* read reference data */
        ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_RAWDATA, readbytes, pRead);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: read mutual rawdata failed!\n", __func__);
            seq_printf(s, "read mutual rawdata failed\n");
            goto ERR_OUT;
        }
        /* store and judge reference data */
        store_to_file(sec_testdata->fd, "TYPE_MUTUAL_RAW_OFFSET_DATA_SDC:\n");
        for (i = 0; i <  sec_testdata->TX_NUM; i++) {
            for (j = 0; j <  sec_testdata->RX_NUM; j++) {
                iArrayIndex = i * sec_testdata->RX_NUM + j;
                nodeData = (pRead[iArrayIndex * 2] << 8) | pRead[iArrayIndex * 2 + 1];
                if (sec_testdata->fd >= 0) {
                    store_to_file(sec_testdata->fd, "%d, ", nodeData);
                }
                if ((nodeData < p_mutual_n[iArrayIndex]) || (nodeData > p_mutual_p[iArrayIndex])) {
                    TPD_INFO(" mutual offset_data failed at data[%d][%d] = %d [%d,%d]\n", i, j, nodeData, p_mutual_n[iArrayIndex], p_mutual_p[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "mutual offset_data failed at data[%d][%d] = %d [%d,%d]\n", i, j, nodeData, p_mutual_n[iArrayIndex], p_mutual_p[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            if (sec_testdata->fd >= 0) {
                store_to_file(sec_testdata->fd, "\n");
            }
        }

        /* CM Offseet gap (left-right)*/
        store_to_file(sec_testdata->fd, "left-right gap:\n");
        for (i = 0; i < sec_testdata->TX_NUM - 1; i++) {
            for (j = 0; j < sec_testdata->RX_NUM; j++) {
                iArrayIndex = i * sec_testdata->RX_NUM + j;
                iArrayIndex_2 = (i + 1) * sec_testdata->RX_NUM + j;
                Buff16 = (pRead[iArrayIndex * 2] << 8) | pRead[iArrayIndex * 2 + 1];
                Buff16_2 = (pRead[iArrayIndex_2 * 2] << 8) | pRead[iArrayIndex_2 * 2 + 1];
                if (Buff16 > Buff16_2) {
                    nodeGap = 100 - (Buff16_2 * 100 / Buff16);
                } else {
                    nodeGap = 100 - (Buff16 * 100 / Buff16_2);
                }
                if (sec_testdata->fd >= 0) {
                    store_to_file(sec_testdata->fd, "%d, ", nodeGap);
                }
                if ((nodeGap > p_mutualGap_p[iArrayIndex]) || (nodeGap < p_mutualGap_n[iArrayIndex])) {
                    TPD_INFO("mutual node[%d, %d]=%d and node[%d, %d]=%d gap beyond [%d, %d]\n", i, j, Buff16, i + 1, j, Buff16_2, p_mutualGap_n[iArrayIndex], p_mutualGap_p[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "mutual node[%d, %d]=%d and node[%d, %d]=%d gap beyond [%d, %d]\n", i, j, Buff16, i + 1, j, Buff16_2, p_mutualGap_n[iArrayIndex], p_mutualGap_p[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            if (sec_testdata->fd >= 0) {
                store_to_file(sec_testdata->fd, "\n");
            }
        }
        /* CM Offseet gap (up-down)*/
        store_to_file(sec_testdata->fd, "up-down gap:\n");
        for (i = 0; i < sec_testdata->TX_NUM; i++) {
            for (j = 0; j < sec_testdata->RX_NUM - 1; j++) {
                iArrayIndex = i * sec_testdata->RX_NUM + j;
                iArrayIndex_2 = i * sec_testdata->RX_NUM + j + 1;
                Buff16 = (pRead[iArrayIndex * 2] << 8) | pRead[iArrayIndex * 2 + 1];
                Buff16_2 = (pRead[iArrayIndex_2 * 2] << 8) | pRead[iArrayIndex_2 * 2 + 1];
                if (Buff16 > Buff16_2) {
                    nodeGap = 100 - (Buff16_2 * 100 / Buff16);
                } else {
                    nodeGap = 100 - (Buff16 * 100 / Buff16_2);
                }
                if (sec_testdata->fd >= 0) {
                    store_to_file(sec_testdata->fd, "%d, ", nodeGap);
                }
                if ((nodeGap > p_mutualGap_p[iArrayIndex]) || (nodeGap < p_mutualGap_n[iArrayIndex])) {
                    TPD_INFO("mutual node[%d, %d]=%d and node[%d, %d]=%d gap beyond [%d, %d]\n", j, i, Buff16, j + 1, i, Buff16_2, p_mutualGap_n[iArrayIndex], p_mutualGap_p[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "mutual node[%d, %d]=%d and node[%d, %d]=%d gap beyond [%d, %d]\n", j, i, Buff16, j + 1, i, Buff16_2, p_mutualGap_n[iArrayIndex], p_mutualGap_p[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            if (sec_testdata->fd >= 0) {
                store_to_file(sec_testdata->fd, "\n");
            }
        }
    }

    //test item self_raw offset_data_sec
    if (sec_testdata->test_item & (1 << TYPE_SELF_RAW_OFFSET_DATA_SDC)) {
        TPD_INFO("do test item self_raw offset_data_sec\n");

        item_offset = search_for_item(sec_testdata->fw, item_cnt, TYPE_SELF_RAW_OFFSET_DATA_SDC);
        if (item_offset == 0) {
            err_cnt++;
            TPD_INFO("search for item limit offset failed\n");
            seq_printf(s, "search for item limit offset failed\n");
            goto ERR_OUT;
        }
        item_header = (struct sec_test_item_header *)(sec_testdata->fw->data + item_offset);
        if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
            err_cnt++;
            TPD_INFO("test item: %d magic number(%4x) is wrong\n", TYPE_SELF_RAW_OFFSET_DATA_SDC, item_header->item_magic);
            seq_printf(s, "test item: %d magic number(%4x) is wrong\n", TYPE_SELF_RAW_OFFSET_DATA_SDC, item_header->item_magic);
            goto ERR_OUT;
        }
        if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
            p_tx_offset_p = (int32_t *)(sec_testdata->fw->data + item_header->top_limit_offset);
            p_tx_offset_n = (int32_t *)(sec_testdata->fw->data + item_header->top_limit_offset + 4 * sec_testdata->TX_NUM);
            p_rx_offset_p = (int32_t *)(sec_testdata->fw->data + item_header->top_limit_offset + 2 * 4 * sec_testdata->TX_NUM);
            p_rx_offset_n = (int32_t *)(sec_testdata->fw->data + item_header->top_limit_offset + 2 * 4 * sec_testdata->TX_NUM + 4 * sec_testdata->RX_NUM);
        } else {
            err_cnt++;
            TPD_INFO("item: %d has invalid limit type(%d)\n", TYPE_SELF_RAW_OFFSET_DATA_SDC, item_header->item_limit_type);
            seq_printf(s, "item: %d has invalid limit type(%d)\n", TYPE_SELF_RAW_OFFSET_DATA_SDC, item_header->item_limit_type);
            goto ERR_OUT;
        }
        if (item_header->para_num == 0) {
        } else {
            err_cnt++;
            TPD_INFO("item: %d has %d parameter\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->para_num);
            seq_printf(s, "item: %d has %d parameter\n", TYPE_MUTUAL_RAW_OFFSET_DATA_SDC, item_header->para_num);
            goto ERR_OUT;
        }

        /* check self offset data */
        type = TYPE_OFFSET_DATA_SDC;
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_SELF_RAW_TYPE, type);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: set self rawdata type failed\n", __func__);
            seq_printf(s, "set self rawdata type failed\n");
            goto ERR_OUT;
        }

        sec_mdelay(100);
        /* read raw data */
        memset(pRead, 0, readbytes); //clear buffer
        ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_SELF_RAWDATA, readselfbytes, pRead);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: read self rawdata failed!\n", __func__);
            seq_printf(s, "read self rawdata failed\n");
            goto ERR_OUT;
        }

        /* store and judge self minimum frame data */
        store_to_file(sec_testdata->fd, "TYPE_SELF_TX_OFFSET_DATA:\n");
        /* check long channel of self data  */
        for (i = 0; i < sec_testdata->TX_NUM; i++) {
            nodeData = (pRead[i * 2] << 8) | pRead[i * 2 + 1];
            if (sec_testdata->fd >= 0) {
                store_to_file(sec_testdata->fd, "%d, ", nodeData);
            }
            if ((nodeData < p_tx_offset_n[i]) || (nodeData > p_tx_offset_p[i])) {
                TPD_INFO("self_offset_tx_data failed at data[%d] = %d [%d,%d]\n", i, nodeData, p_tx_offset_n[i], p_tx_offset_p[i]);
                if (!err_cnt) {
                    seq_printf(s, "self_offset_tx_data failed at data[%d] = %d [%d,%d]\n", i, nodeData, p_tx_offset_n[i], p_tx_offset_p[i]);
                }
                err_cnt++;
            }
        }
        store_to_file(sec_testdata->fd, "\n");

        store_to_file(sec_testdata->fd, "TYPE_SELF_RX_OFFSET_DATA:\n");
        /* check short channel of self data */
        for (i = 0; i < sec_testdata->RX_NUM; i++) {
            nodeData = (pRead[sec_testdata->TX_NUM * 2 + i * 2] << 8) | pRead[sec_testdata->TX_NUM * 2 + i * 2 + 1];
            if (sec_testdata->fd >= 0) {
                store_to_file(sec_testdata->fd, "%d, ", nodeData);
            }
            if ((nodeData < p_rx_offset_n[i]) || (nodeData > p_rx_offset_p[i])) {
                TPD_INFO("self_offset_rx_data failed at data[%d] = %d [%d,%d]\n", i, nodeData, p_rx_offset_n[i], p_rx_offset_p[i]);
                if (!err_cnt) {
                    seq_printf(s, "self_offset_rx_data failed at data[%d] = %d [%d,%d]\n", i, nodeData, p_rx_offset_n[i], p_rx_offset_p[i]);
                }
                err_cnt++;
            }
        }
        store_to_file(sec_testdata->fd, "\n");
    }

    ret = sec_execute_p2ptest(s, chip_info, sec_testdata);
    if (ret < 0) {
        err_cnt++;
        TPD_INFO("%s: p2ptest failed\n", __func__);
        seq_printf(s, "p2ptest failed\n");
        goto ERR_OUT;
    }

    //test item mutual raw noise
    if (sec_testdata->test_item & (1 << TYPE_MUTU_RAW_NOI_P2P)) {
        TPD_INFO("do test item raw noise p2p\n");

        item_offset = search_for_item(sec_testdata->fw, item_cnt, TYPE_MUTU_RAW_NOI_P2P);
        if (item_offset == 0) {
            err_cnt++;
            TPD_INFO("search for item limit offset failed\n");
            seq_printf(s, "search for item limit offset failed\n");
            goto ERR_OUT;
        }
        item_header = (struct sec_test_item_header *)(sec_testdata->fw->data + item_offset);
        if (item_header->item_magic != Limit_ItemMagic && item_header->item_magic != Limit_ItemMagic_V2) {
            err_cnt++;
            TPD_INFO("test item: %d magic number(%4x) is wrong\n", TYPE_MUTU_RAW_NOI_P2P, item_header->item_magic);
            seq_printf(s, "test item: %d magic number(%4x) is wrong\n", TYPE_MUTU_RAW_NOI_P2P, item_header->item_magic);
            goto ERR_OUT;
        }
        if (item_header->item_limit_type == LIMIT_TYPE_EACH_NODE_DATA) {
            p_p2p_p = (int32_t *)(sec_testdata->fw->data + item_header->top_limit_offset);
            p_p2p_n = (int32_t *)(sec_testdata->fw->data + item_header->floor_limit_offset);
        } else {
            err_cnt++;
            TPD_INFO("item: %d has invalid limit type(%d)\n", TYPE_MUTU_RAW_NOI_P2P, item_header->item_limit_type);
            seq_printf(s, "item: %d has invalid limit type(%d)\n", TYPE_MUTU_RAW_NOI_P2P, item_header->item_limit_type);
            goto ERR_OUT;
        }
        if (item_header->para_num == 0) {
        } else {
            err_cnt++;
            TPD_INFO("item: %d has %d parameter\n", TYPE_MUTU_RAW_NOI_P2P, item_header->para_num);
            seq_printf(s, "item: %d has %d parameter\n", TYPE_MUTU_RAW_NOI_P2P, item_header->para_num);
            goto ERR_OUT;
        }

        /* read minimum value frame from p2p test result */
        type = TYPE_NOI_P2P_MIN;
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, type);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: set rawdata type failed\n", __func__);
            seq_printf(s, "set rawdata type failed\n");
            goto ERR_OUT;
        }

        sec_mdelay(100);
        /* read raw data */
        memset(pRead, 0, readbytes); //clear buffer
        ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_RAWDATA, readbytes, pRead);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: read rawdata failed!\n", __func__);
            seq_printf(s, "read rawdata failed\n");
            goto ERR_OUT;
        }

        store_to_file(sec_testdata->fd, "TYPE_P2P_MIN_DATA:\n");
        /* check minimum value */
        for (i = 0; i < sec_testdata->TX_NUM; i++) {
            for (j = 0; j < sec_testdata->RX_NUM; j++) {
                iArrayIndex = i * sec_testdata->RX_NUM + j;
                nodeData = (pRead[iArrayIndex * 2] << 8) | pRead[iArrayIndex * 2 + 1];
                if (sec_testdata->fd >= 0) {
                    store_to_file(sec_testdata->fd, "%d, ", nodeData);
                }
                if ((nodeData < p_p2p_n[iArrayIndex])) {
                    TPD_INFO("p2p_min_data failed at data[%d][%d] = %d [%d]\n", i, j, nodeData, p_p2p_n[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "p2p_min_data failed at data[%d][%d] = %d [%d]\n", i, j, nodeData, p_p2p_n[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            if (sec_testdata->fd >= 0) {
                store_to_file(sec_testdata->fd, "\n");
            }
        }

        /* read maximum value frame from p2p test result */
        type = TYPE_NOI_P2P_MAX;
        ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_MUTU_RAW_TYPE, type);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: set rawdata type failed\n", __func__);
            seq_printf(s, "set rawdata type failed\n");
            goto ERR_OUT;
        }

        sec_mdelay(100);
        /* read raw data */
        memset(pRead, 0, readbytes); //clear buffer
        ret = touch_i2c_read_block(chip_info->client, SEC_READ_TOUCH_RAWDATA, readbytes, pRead);
        if (ret < 0) {
            err_cnt++;
            TPD_INFO("%s: read rawdata failed!\n", __func__);
            seq_printf(s, "read rawdata failed\n");
            goto ERR_OUT;
        }

        store_to_file(sec_testdata->fd, "TYPE_P2P_MAX_DATA:\n");
        /* check maximum value */
        for (i = 0; i < sec_testdata->TX_NUM; i++) {
            for (j = 0; j < sec_testdata->RX_NUM; j++) {
                iArrayIndex = i * sec_testdata->RX_NUM + j;
                nodeData = (pRead[iArrayIndex * 2] << 8) | pRead[iArrayIndex * 2 + 1];
                if (sec_testdata->fd >= 0) {
                    store_to_file(sec_testdata->fd, "%d, ", nodeData);
                }
                if ((nodeData > p_p2p_p[iArrayIndex])) {
                    TPD_INFO("p2p_max_data failed at data[%d][%d] = %d [%d]\n", i, j, nodeData, p_p2p_p[iArrayIndex]);
                    if (!err_cnt) {
                        seq_printf(s, "p2p_max_data failed at data[%d][%d] = %d [%d]\n", i, j, nodeData, p_p2p_p[iArrayIndex]);
                    }
                    err_cnt++;
                }
            }
            if (sec_testdata->fd >= 0) {
                store_to_file(sec_testdata->fd, "\n");
            }
        }
    }

ERR_OUT:
    if (pRead) {
        kfree(pRead);
        pRead = NULL;
    }

    seq_printf(s, "FW:0x%llx\n", sec_testdata->TP_FW);
    seq_printf(s, "%d error(s). %s\n", err_cnt, err_cnt ? "" : "All test passed.");
    TPD_INFO(" TP auto test %d error(s). %s\n", err_cnt, err_cnt ? "" : "All test passed.");
}

static int sec_get_verify_result(struct chip_data_s6sy791 *chip_info)
{
    int ret = -1;

    sec_fix_tmode(chip_info, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
    touch_i2c_write_block(chip_info->client, 0xA7, 0, NULL);    //set to verify calibration
    sec_mdelay(100);
    //get calibration result, 0x0F:FAIL(bit[0]:data condition FAIL, bit[1]:RX gap FAIL, bit[2]:TX gap FAIL, bit[3]:TX/RX peak FAIL)
    ret = touch_i2c_read_byte(chip_info->client, 0xA8);
    sec_release_tmode(chip_info);

    return ret;
}

static void sec_calibrate(struct seq_file *s, void *chip_data)
{
    int ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    ret = sec_execute_force_calibration(chip_info);
    if (ret < 0) {
        TPD_INFO("1 error, do calibration failed\n");
        seq_printf(s, "1 error, do calibration failed\n");
        return;
    }

    if (check_calibration(chip_info)) {
        chip_info->cal_needed = true;
        TPD_INFO("1 error, check calibration failed\n");
        seq_printf(s, "1 error, check calibration failed\n");
        return;
    } else {
        chip_info->cal_needed = false;
    }

    ret = sec_get_verify_result(chip_info);
    if (ret) {
        seq_printf(s, "1 error, calibration verify failed(%d)\n", ret);
    } else {
        seq_printf(s, "0 error, calibration and verify success\n");
    }
    TPD_INFO("get verify result :%d\n", ret);

    return;
}

static void sec_verify_calibration(struct seq_file *s, void *chip_data)
{
    int ret = -1;
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    ret = sec_get_verify_result(chip_info);
    if (ret != 0) {
        seq_printf(s, "1 error, verify calibration result failed(0x%02x)\n", ret);
    } else {
        seq_printf(s, "0 error, verify calibration result successed\n");
    }

    return;
}

static bool sec_get_cal_status(struct seq_file *s, void *chip_data)
{
    struct chip_data_s6sy791 *chip_info = (struct chip_data_s6sy791 *)chip_data;

    return chip_info->cal_needed;
}


static struct sec_proc_operations sec_proc_ops = {
    .auto_test          = sec_auto_test,
    .verify_calibration = sec_verify_calibration,
};

#ifdef CONFIG_OPLUS_TP_APK

static void sec_apk_game_set(void *chip_data, bool on_off)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    sec_mode_switch(chip_data, MODE_GAME, on_off);
    chip_info->lock_point_status = on_off;
}

static bool sec_apk_game_get(void *chip_data)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    return chip_info->lock_point_status;
}

static void sec_apk_gesture_debug(void *chip_data, bool on_off)
{

    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    //get_gesture_fail_reason(on_off);
    if (on_off) {
        if (chip_info->gesture_buf == NULL) {
            chip_info->gesture_buf = kzalloc(4096, GFP_KERNEL);
        }
        if (chip_info->gesture_buf == NULL) {
            chip_info->debug_gesture_sta = false;
            return;

        }
    }
    chip_info->debug_gesture_sta = on_off;
}

static bool  sec_apk_gesture_get(void *chip_data)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    return chip_info->debug_gesture_sta;
}

static int  sec_apk_gesture_info(void *chip_data, char *buf, int len)
{
    int ret = 0;
    int i;
    int j;
    int num;
    int delta_index = 0;
    int count = 0;
    struct chip_data_s6sy791 *chip_info;

    chip_info = (struct chip_data_s6sy791 *)chip_data;

    if(len < 2) {
        return 0;
    }
    buf[0] = 255;

    if (chip_info->debug_gesture_type != 0) {
        buf[0] = chip_info->debug_gesture_type;
    }

    num = chip_info->geture_points_count;

    ret = 2;

    buf[1] = 0;

    if (num > 127) {
        delta_index = (num + 1) / 128;
    }
    //print all data
    for (i = 0; i < 3; i++) {
        int temp;
        temp = delta_index;
        for (j = 0; j < num; j++) {
            if (chip_info->gesture_buf[j * 4] == i) {
                int x;
                int y;

                x = (chip_info->gesture_buf[j * 4 + 1] << 4) | (chip_info->gesture_buf[j * 4 + 3] >> 4);

                y = (chip_info->gesture_buf[j * 4 + 2] << 4) | (chip_info->gesture_buf[j * 4 + 3] & 0x0F);


                //TPD_INFO("nova_apk_gesture_info:gesture x is %d,y is %d.\n", x, y);

                if (len < i * 4 + 2) {
                    break;
                }
                buf[count * 4 + 2] = x & 0xFF;
                buf[count * 4 + 3] = (x >> 8) & 0xFF;
                buf[count * 4 + 4] = y & 0xFF;
                buf[count * 4 + 5] = (y >> 8) & 0xFF;
                temp++;
                if(temp > delta_index) {
                    temp = 0;
                    count++;
                    ret += 4;
                }
            }
        }
        if (temp != 0) {
            count++;
            ret += 4;
        }
    }
    buf[1] = count;

    return ret;
}


static void sec_apk_earphone_set(void *chip_data, bool on_off)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    sec_mode_switch(chip_data, MODE_HEADSET, on_off);
    chip_info->earphone_sta = on_off;
}

static bool sec_apk_earphone_get(void *chip_data)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    return chip_info->earphone_sta;
}

static void sec_apk_charger_set(void *chip_data, bool on_off)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    sec_mode_switch(chip_data, MODE_CHARGE, on_off);
    chip_info->plug_status = on_off;


}

static bool sec_apk_charger_get(void *chip_data)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;

    return chip_info->plug_status;

}

static void sec_apk_water_set(void *chip_data, int type)
{
    struct chip_data_s6sy791 *chip_info;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    //ilitek_mode_switch(chip_data, MODE_CHARGE, on_off);
    if (type > 0) {
        touch_i2c_write_byte(chip_info->client, SEC_CMD_WET_SWITCH, 3);
    } else {
        touch_i2c_write_byte(chip_info->client, SEC_CMD_WET_SWITCH, 0);
    }

    //chip_info->water_sta = type;

}

static int sec_apk_water_get(void *chip_data)
{
    struct chip_data_s6sy791 *chip_info;
    int ret = -1;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    ret = touch_i2c_read_byte(chip_info->client, SEC_CMD_WET_SWITCH);

    if (ret == 3) {
        return 1;
    }
    return 0;

}


static int  sec_apk_tp_info_get(void *chip_data, char *buf, int len)
{
    int ret;
    struct chip_data_s6sy791 *chip_info;
    unsigned char data[5];
    int fw_ver;
    chip_info = (struct chip_data_s6sy791 *)chip_data;
    memset(data, 0, 5);
    touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, data);
    fw_ver = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    ret = snprintf(buf, len, "IC:S6SY%03X\nFW_VER:0x%02X\nCH:%dX%d\n",
                   0x791,
                   fw_ver,
                   chip_info->hw_res->TX_NUM,
                   chip_info->hw_res->RX_NUM);
    if (ret > len) {
        ret = len;
    }

    return ret;
}

static void sec_init_oplus_apk_op(struct touchpanel_data *ts)
{
    ts->apk_op = kzalloc(sizeof(APK_OPERATION), GFP_KERNEL);
    if(ts->apk_op) {
        ts->apk_op->apk_game_set = sec_apk_game_set;
        ts->apk_op->apk_game_get = sec_apk_game_get;
        //ts->apk_op->apk_debug_set = sec_apk_debug_set;
        //ts->apk_op->apk_debug_get = sec_apk_debug_get;
        //apk_op->apk_proximity_set = sec_apk_proximity_set;
        //apk_op->apk_proximity_dis = sec_apk_proximity_dis;
        //ts->apk_op->apk_noise_set = sec_apk_noise_set;
        //ts->apk_op->apk_noise_get = sec_apk_noise_get;
        ts->apk_op->apk_gesture_debug = sec_apk_gesture_debug;
        ts->apk_op->apk_gesture_get = sec_apk_gesture_get;
        ts->apk_op->apk_gesture_info = sec_apk_gesture_info;
        ts->apk_op->apk_earphone_set = sec_apk_earphone_set;
        ts->apk_op->apk_earphone_get = sec_apk_earphone_get;
        ts->apk_op->apk_charger_set = sec_apk_charger_set;
        ts->apk_op->apk_charger_get = sec_apk_charger_get;
        ts->apk_op->apk_tp_info_get = sec_apk_tp_info_get;
        ts->apk_op->apk_water_set = sec_apk_water_set;
        ts->apk_op->apk_water_get = sec_apk_water_get;
        //apk_op->apk_data_type_set = sec_apk_data_type_set;
        //apk_op->apk_rawdata_get = sec_apk_rawdata_get;
        //apk_op->apk_diffdata_get = sec_apk_diffdata_get;
        //apk_op->apk_basedata_get = sec_apk_basedata_get;
        //ts->apk_op->apk_backdata_get = sec_apk_backdata_get;
        //apk_op->apk_debug_info = sec_apk_debug_info;

    } else {
        TPD_INFO("Can not kzalloc apk op.\n");
    }
}
#endif // end of CONFIG_OPLUS_TP_APK


/*********** Start of I2C Driver and Implementation of it's callbacks*************************/
static int sec_tp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct chip_data_s6sy791 *chip_info = NULL;
    struct touchpanel_data *ts = NULL;
    int ret = -1;

    TPD_INFO("%s  is called\n", __func__);

    /* 1. alloc chip info */
    chip_info = kzalloc(sizeof(struct chip_data_s6sy791), GFP_KERNEL);
    if (chip_info == NULL) {
        TPD_INFO("chip info kzalloc error\n");
        ret = -ENOMEM;
        return ret;
    }
    memset(chip_info, 0, sizeof(*chip_info));

    /* 2. Alloc common ts */
    ts = common_touch_data_alloc();
    if (ts == NULL) {
        TPD_INFO("ts kzalloc error\n");
        goto ts_malloc_failed;
    }
    memset(ts, 0, sizeof(*ts));

    /* 3. bind client and dev for easy operate */
    chip_info->client = client;
    ts->debug_info_ops = &debug_info_proc_ops;
    ts->client = client;
    ts->irq = client->irq;
    i2c_set_clientdata(client, ts);
    ts->dev = &client->dev;
    ts->chip_data = chip_info;
    chip_info->hw_res = &ts->hw_res;
    chip_info->in_suspend = &ts->is_suspended;
    chip_info->fp_enable = &ts->fp_enable;
    chip_info->cal_needed = false;
    chip_info->touch_direction = VERTICAL_SCREEN;

    /* 4. file_operations callbacks binding */
    ts->ts_ops = &sec_ops;
    ts->aging_test_ops = &aging_test_proc_ops;
    ts->earsense_ops = &earsense_proc_ops;
	ts->calibration_support = true;

    /* 5. register common touch device*/
    ret = register_common_touch_device(ts);
    if (ret < 0) {
        goto err_register_driver;
    }
    chip_info->irq_requested = true;
    ts->tp_suspend_order = TP_LCD_SUSPEND;
	chip_info->auto_test_need_cal_support = of_property_read_bool(ts->dev->of_node, "auto_test_need_cal_support");
	chip_info->old_firmware_flag_check = of_property_read_bool(ts->dev->of_node, "old_firmware_flag_check");
	chip_info->oos_lcd_tp_refresh_support = of_property_read_bool(ts->dev->of_node, "oos_lcd_tp_refresh_support");
	chip_info->oos_game_switch_support = of_property_read_bool(ts->dev->of_node, "oos_game_switch_support");

    /* 6. create debug interface*/
    sec_raw_device_init(ts);
    sec_create_proc(ts, &sec_proc_ops);

#ifdef CONFIG_OPLUS_TP_APK
    sec_init_oplus_apk_op(ts);
#endif // end of CONFIG_OPLUS_TP_APK


    g_chip_info = chip_info;

    TPD_INFO("%s, probe normal end\n", __func__);
    // update firmware
    if (ts->firmware_in_dts) {
        sec_fw_update(chip_info, ts->firmware_in_dts, false);
    }
    return 0;

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

static int sec_tp_remove(struct i2c_client *client)
{
    struct touchpanel_data *ts = i2c_get_clientdata(client);

    TPD_INFO("%s is called\n", __func__);
    kfree(ts);

    return 0;
}

static int sec_i2c_suspend(struct device *dev)
{
    struct touchpanel_data *ts = dev_get_drvdata(dev);

    TPD_INFO("%s: is called\n", __func__);
    tp_i2c_suspend(ts);

    return 0;
}

static int sec_i2c_resume(struct device *dev)
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
#if defined(CONFIG_FB) || defined(CONFIG_DRM_MSM)
    .suspend = sec_i2c_suspend,
    .resume = sec_i2c_resume,
#endif
};

static struct i2c_driver tp_i2c_driver = {
    .probe = sec_tp_probe,
    .remove = sec_tp_remove,
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

    if (!tp_judge_ic_match(TPD_DEVICE)) {
        return -1;
    }

    if (i2c_add_driver(&tp_i2c_driver) != 0) {
        TPD_INFO("unable to add i2c driver.\n");
        return -1;
    }
    return 0;
}

static void __exit tp_driver_exit(void)
{
    i2c_del_driver(&tp_i2c_driver);
}
#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(tp_driver_init);
#else
module_init(tp_driver_init);
#endif
module_exit(tp_driver_exit);
/***********************End of module init and exit*******************************/

MODULE_AUTHOR("Samsung Driver");
MODULE_DESCRIPTION("Samsung Electronics TouchScreen driver");
MODULE_LICENSE("GPL v2");
