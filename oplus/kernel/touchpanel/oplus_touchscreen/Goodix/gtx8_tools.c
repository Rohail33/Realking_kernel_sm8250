// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "gtx8_tools.h"

/****************************PART1:Log TAG****************************/
#define TPD_DEVICE "Goodix-TOOL"
#define TPD_INFO(fmt, arg...)        pr_err(TPD_DEVICE ": " fmt, ##arg)
#define TPD_DEBUG(fmt, arg...)       do{\
    if (tp_debug)\
    pr_err(TPD_DEVICE ": " fmt, ##arg);\
}while(0)

#define TPD_DEBUG_ARRAY(array, num)    do{\
    s32 i;\
    u8* a = array;\
    if (tp_debug)\
    {\
        pr_err("<< GTP-TOOL-DBG >>");\
        for (i = 0; i < (num); i++)\
        {\
            pr_err("%02x ", (a)[i]);\
            if ((i + 1) % 10 == 0)\
            {\
                pr_err("\n<< GTP-DBG >>");\
            }\
        }\
        pr_err("\n");\
    }\
}while(0)

#define GOODIX_TOOLS_NAME        "gtp_tools"
#define GOODIX_TS_IOC_MAGIC      'G'
#define NEGLECT_SIZE_MASK        (~(_IOC_SIZEMASK << _IOC_SIZESHIFT))

#define GTP_IRQ_ENABLE    _IO(GOODIX_TS_IOC_MAGIC, 0)
#define GTP_DEV_RESET     _IO(GOODIX_TS_IOC_MAGIC, 1)
#define GTP_SEND_COMMAND  (_IOW(GOODIX_TS_IOC_MAGIC, 2, u8) & NEGLECT_SIZE_MASK)
#define GTP_SEND_CONFIG   (_IOW(GOODIX_TS_IOC_MAGIC, 3, u8) & NEGLECT_SIZE_MASK)
#define GTP_ASYNC_READ    (_IOR(GOODIX_TS_IOC_MAGIC, 4, u8) & NEGLECT_SIZE_MASK)
#define GTP_SYNC_READ     (_IOR(GOODIX_TS_IOC_MAGIC, 5, u8) & NEGLECT_SIZE_MASK)
#define GTP_ASYNC_WRITE   (_IOW(GOODIX_TS_IOC_MAGIC, 6, u8) & NEGLECT_SIZE_MASK)
#define GTP_READ_CONFIG   (_IOW(GOODIX_TS_IOC_MAGIC, 7, u8) & NEGLECT_SIZE_MASK)
#define GTP_ESD_ENABLE    _IO(GOODIX_TS_IOC_MAGIC, 8)
#define GTP_CLEAR_RAWDATA_FLAG    _IO(GOODIX_TS_IOC_MAGIC, 9)

#define GOODIX_TS_IOC_MAXNR            10
#define GOODIX_TOOLS_MAX_DATA_LEN      4096
#define I2C_MSG_HEAD_LEN               20

#define GTX8_TOOLS_CLOSE  0
#define GTX8_TOOLS_OPEN  1

#define MISC_DYNAMIC_MINOR    255
int gt8x_rawdiff_mode;

static struct Goodix_tool_info gtx8_tool_info;

/* read data from i2c asynchronous,
** success return bytes read, else return <= 0
*/
static int async_read(void __user *arg)
{
    u8 *databuf = NULL;
    int ret = 0;
    u32 reg_addr = 0;
    u32 length = 0;
    u8 i2c_msg_head[I2C_MSG_HEAD_LEN];

    ret = copy_from_user(&i2c_msg_head, arg, I2C_MSG_HEAD_LEN);
    if (ret) {
        ret = -EFAULT;
        return ret;
    }

    reg_addr = i2c_msg_head[0] + (i2c_msg_head[1] << 8)
               + i2c_msg_head[2] + (i2c_msg_head[3] << 8);
    length = i2c_msg_head[4] + (i2c_msg_head[5] << 8)
             + (i2c_msg_head[6] << 16) + (i2c_msg_head[7] << 24);
    if (length > GOODIX_TOOLS_MAX_DATA_LEN) {
        TPD_INFO("%s: Invalied data length:%d\n", __func__, length);
        return -EFAULT;
    }

    databuf = kzalloc(length, GFP_KERNEL);
    if (!databuf) {
        TPD_INFO("Alloc memory failed\n");
        return -ENOMEM;
    }

    if (touch_i2c_read_block(gtx8_tool_info.client, (u16)reg_addr, length, databuf) >= 0) {
        if (copy_to_user((u8 *)arg + I2C_MSG_HEAD_LEN, databuf, length)) {
            ret = -EFAULT;
            TPD_INFO("Copy_to_user failed\n");
        } else {
            ret = length;
        }
    } else {
        ret = -EBUSY;
        TPD_INFO("Read i2c failed\n");
    }

    if(databuf) {
        kfree(databuf);
        databuf = NULL;
    }
    return ret;
}

/* write data to i2c asynchronous,
** success return bytes write, else return <= 0
*/
static int async_write(void __user *arg)
{
    u8 *databuf = NULL;
    int ret = 0;
    u32 reg_addr = 0;
    u32 length = 0;
    u8 i2c_msg_head[I2C_MSG_HEAD_LEN];

    ret = copy_from_user(&i2c_msg_head, arg, I2C_MSG_HEAD_LEN);
    if (ret) {
        TPD_INFO("Copy data from user failed\n");
        return -EFAULT;
    }

    reg_addr = i2c_msg_head[0] + (i2c_msg_head[1] << 8)
               + i2c_msg_head[2] + (i2c_msg_head[3] << 8);
    length = i2c_msg_head[4] + (i2c_msg_head[5] << 8)
             + (i2c_msg_head[6] << 16) + (i2c_msg_head[7] << 24);

    if (length > GOODIX_TOOLS_MAX_DATA_LEN) {
        TPD_INFO("%s: Invalied data length:%d\n", __func__, length);
        return -EFAULT;
    }
    databuf = kzalloc(length, GFP_KERNEL);
    if (!databuf) {
        TPD_INFO("Alloc memory failed\n");
        return -ENOMEM;
    }

    ret = copy_from_user(databuf, (u8 *)arg + I2C_MSG_HEAD_LEN, length);
    if (ret) {
        ret = -EFAULT;
        TPD_INFO("Copy data from user failed\n");
        goto err_out;
    }

    if (touch_i2c_write_block(gtx8_tool_info.client, (u16)reg_addr, length, databuf) < 0) {
        ret = -EBUSY;
        TPD_INFO("Write data to device failed\n");
    } else {
        ret = length;
    }

err_out:
    if(databuf) {
        kfree(databuf);
        databuf = NULL;
    }
    return ret;
}

static int goodix_tools_open(struct inode *inode, struct file *filp)
{
    TPD_INFO("tools open\n");
    if (gtx8_tool_info.devicecount > 0) {
        return -ERESTARTSYS;
        TPD_INFO("tools open failed!");
    }
    gtx8_tool_info.devicecount++;

    return 0;
}

static int goodix_tools_release(struct inode *inode, struct file *filp)
{
    TPD_INFO("tools release\n");
    gtx8_tool_info.devicecount--;

    return 0;
}

/**
 * goodix_tools_ioctl - ioctl implementation
 *
 * @filp: Pointer to file opened
 * @cmd: Ioctl opertion command
 * @arg: Command data
 * Returns >=0 - succeed, else failed
 */
#define TOOL_CMD_LEN 2
static long goodix_tools_ioctl(struct file *filp, unsigned int cmd,
                               unsigned long arg)
{
    int ret = 0;
    struct i2c_client *client = gtx8_tool_info.client;

    if (_IOC_TYPE(cmd) != GOODIX_TS_IOC_MAGIC) {
        TPD_INFO("Bad magic num:%c\n", _IOC_TYPE(cmd));
        return -ENOTTY;
    }

    if (_IOC_NR(cmd) > GOODIX_TS_IOC_MAXNR) {
        TPD_INFO("Bad cmd num:%d > %d",
                 _IOC_NR(cmd), GOODIX_TS_IOC_MAXNR);
        return -ENOTTY;
    }

    switch (cmd & NEGLECT_SIZE_MASK) {
    case GTP_IRQ_ENABLE:
        if (arg) {
            enable_irq(client->irq);
        } else {
            disable_irq_nosync(client->irq);
        }
        TPD_INFO("set irq mode: %s\n", arg ? "enable" : "disable");
        ret = 0;
        break;
    case GTP_ESD_ENABLE:
        ret = 0;
        if(!gtx8_tool_info.esd_handle_support) {
            TPD_INFO("Unsupport esd operation\n");
        } else {
            esd_handle_switch(gtx8_tool_info.esd_info, !!arg);
            TPD_DEBUG("set esd mode: %s\n", arg ? "enable" : "disable");
        }
        break;
    case GTP_DEV_RESET:
        gtx8_tool_info.reset(gtx8_tool_info.chip_data);
        break;

    case GTP_ASYNC_READ:
        ret = async_read((void __user *)arg);
        if (ret < 0)
            TPD_INFO("Async data read failed\n");
        break;
    case GTP_SYNC_READ:
        gt8x_rawdiff_mode = 1;
        break;
    case GTP_ASYNC_WRITE:
        ret = async_write((void __user *)arg);
        if (ret < 0)
            TPD_INFO("Async data write failed\n");
        break;
    case GTP_CLEAR_RAWDATA_FLAG:
        gt8x_rawdiff_mode = 0;
        ret = 0;
        break;

    default:
        TPD_INFO("Invalid cmd\n");
        ret = -ENOTTY;
        break;
    }

    return ret;
}

#ifdef CONFIG_COMPAT
static long goodix_tools_compat_ioctl(struct file *file, unsigned int cmd,
                                      unsigned long arg)
{
    void __user *arg32 = compat_ptr(arg);

    if (!file->f_op || !file->f_op->unlocked_ioctl)
        return -1;
    return file->f_op->unlocked_ioctl(file, cmd, (unsigned long)arg32);
}
#endif

static const struct file_operations goodix_tools_fops = {
    .owner          = THIS_MODULE,
    .open           = goodix_tools_open,
    .release        = goodix_tools_release,
    .unlocked_ioctl = goodix_tools_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = goodix_tools_compat_ioctl,
#endif
};

static struct miscdevice goodix_tools_miscdev = {
    .minor   = MISC_DYNAMIC_MINOR,
    .name    = GOODIX_TOOLS_NAME,
    .fops    = &goodix_tools_fops,
};

int gtx8_init_tool_node(struct touchpanel_data *ts)
{
    int ret = NO_ERR;

    ret = misc_register(&goodix_tools_miscdev);
    if (ret) {
        TPD_INFO("Debug tools miscdev register failed\n");
    }
    gtx8_tool_info.devicecount = 0;
    gtx8_tool_info.is_suspended = &ts->is_suspended;
    gtx8_tool_info.esd_handle_support = ts->esd_handle_support;
    gtx8_tool_info.esd_info = &ts->esd_info;
    gtx8_tool_info.client = ts->client;
    gtx8_tool_info.chip_data = ts->chip_data;
    gtx8_tool_info.hw_res = &(ts->hw_res);
    gtx8_tool_info.reset = ts->ts_ops->reset;

    return ret;
}

void gtx8_deinit_tool_node(void)
{
    misc_deregister(&goodix_tools_miscdev);
    TPD_INFO("Goodix tools miscdev exit\n");
}