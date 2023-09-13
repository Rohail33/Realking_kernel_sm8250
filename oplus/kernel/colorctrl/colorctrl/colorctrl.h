/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 All rights reserved.
 */

#ifndef H_COLORCTRL
#define H_COLORCTRL

#include <linux/printk.h>
#include <linux/thermal.h>

#define COLOR_INFO(fmt, args...) \
    pr_info("COLOR-CTRL: %s:" fmt "\n", __func__, ##args)

#define GPIO_HIGH (1)
#define GPIO_LOW  (0)
#define PAGESIZE  512
#define UV_PER_MV 1000
#define MAX_PARAMETER 50
#define NAME_TAG_SIZE 50
#define MAX_CTRL_TYPE 8
#define MAX_THERMAL_ZONE_DEVICE_NUM 10
#define MAX_THERMAL_ZONE_DEVICE_WEIGHT_NUM (MAX_THERMAL_ZONE_DEVICE_NUM + 1)
#define MAX_THERMAL_ZONE_DEVICE_NAME_LEN 100

enum color_ctrl_write_type {
    LIGHT_BLUE_FTM,
    BLUE_FTM,
    TRANSPARENT_FTM,
    BLUE_NORMAL,
    TRANSPARENT_NORMAL,
    RESET,
    RESET_FTM,
    STOP_RECHARGE,
    OPEN_RECHARGE,
};

typedef enum color_ctrl_type {
    LIGHT_BLUE,
    BLUE,
    TRANSPARENT,
    OPEN_CIRCUIT,
    SHORT_CIRCUIT,
    UNKNOWN = 0xFF,
} color_status;

typedef enum color_ctrl_temp_type {
    TEMP_RANGE_1,
    TEMP_RANGE_2,
    TEMP_RANGE_3,
    TEMP_RANGE_4,
    TEMP_RANGE_5,
    TEMP_RANGE_6,
    ABNORMAL_TEMP,
    MAX_TEMP_RANGE,
} temp_status;

struct color_ctrl_hw_resource {
    unsigned int        sleep_en_gpio;
    unsigned int        si_in_1_gpio;
    unsigned int        si_in_2_gpio;
    unsigned int        vm_enable_gpio;
    struct iio_channel  *vm_v_chan;
    struct regulator    *vm;        /*driver power*/
};

struct color_ctrl_control_para {
    unsigned int        charge_volt[MAX_TEMP_RANGE];
    unsigned int        charge_time[MAX_TEMP_RANGE];
    unsigned int        recharge_volt;
    unsigned int        recharge_time;
    unsigned int        recharge_volt_thd_1;
    unsigned int        recharge_volt_thd_2;
};

struct color_ctrl_device {
    struct platform_device          *pdev;
    struct device                   *dev;
    struct mutex                    rw_lock;
    struct proc_dir_entry           *prEntry_cr;
    struct color_ctrl_hw_resource   *hw_res;
    struct thermal_zone_device      *thermal_zone_device[MAX_THERMAL_ZONE_DEVICE_NUM];
    struct hrtimer                  hrtimer;
    struct work_struct              recharge_work;
    struct workqueue_struct         *recharge_wq;
    struct wakeup_source            *ws;
    color_status                    color_status;
    temp_status                     temp_status;
    int                             platform_support_project[10];
    int                             project_num;
    unsigned int                    intermediate_wait_time;
    unsigned int                    volt_measure_interval_time;
    bool                            need_recharge;
    unsigned int                    recharge_time;
    int                             temp_thd_1;
    int                             temp_thd_2;
    int                             temp_thd_3;
    int                             temp_thd_4;
    int                             temp_thd_5;
    int                             temp_thd_6;
    int                             temp_thd_7;
    unsigned int                    open_circuit_thd;
    unsigned int                    blue_short_circuit_thd;
    unsigned int                    transparent_short_circuit_thd;
    struct color_ctrl_control_para  *blue_control_para;
    struct color_ctrl_control_para  *transparent_control_para;
    int                             thermal_zone_device_num;
    char                            *thermal_zone_device_name[MAX_THERMAL_ZONE_DEVICE_NUM];
    int                             thermal_zone_device_weight[MAX_THERMAL_ZONE_DEVICE_WEIGHT_NUM];
    int                             thermal_zone_device_weight_sigh_bit[MAX_THERMAL_ZONE_DEVICE_WEIGHT_NUM];
};

#endif
