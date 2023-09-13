/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef H_COLORCTRL
#define H_COLORCTRL

#include <linux/printk.h>
#include <linux/thermal.h>

#define GPIO_HIGH (1)
#define GPIO_LOW  (0)
#define PAGESIZE  512
#define UV_PER_MV 1000
#define MAX_PARAMETER 50
#define NAME_TAG_SIZE 50
#define MAX_CTRL_TYPE 10
#define ABNORMAL_ZONE 11
#define MAX_THERMAL_ZONE_DEVICE_NUM 10
#define MAX_THERMAL_ZONE_DEVICE_WEIGHT_NUM (MAX_THERMAL_ZONE_DEVICE_NUM + 1)
#define MAX_THERMAL_ZONE_DEVICE_NAME_LEN 100
#define MAX_CHANGE_VOLTAGE	550

#define EC_CHMOD 0666

#define MAX_AGING_VOL_DATA 20
#define AGING_VOL_PATH "/mnt/vendor/persist/data/ec2_aging_data.txt"

extern unsigned int ec_debug;

#define COLOR_INFO(fmt, args...) \
	pr_info("COLOR-CTRL: %s:" fmt "\n", __func__, ##args)

#define COLOR_DEBUG(fmt, args...) \
	do { \
		if (LEVEL_DEBUG == ec_debug) \
		pr_info("COLOR-CTRL: %s:" fmt "\n", __func__, ##args);\
	}while(0)

typedef enum debug_level {
	LEVEL_BASIC,
	LEVEL_DEBUG,
} ec_debug_level;

enum color_ctrl_write_type {
	LIGHT_COLOR_FTM,
	COLOR_FTM,
	TRANSPARENT_FTM,
	COLOR_NORMAL,
	TRANSPARENT_NORMAL,
	RESET,
	RESET_FTM,
	STOP_RECHARGE,
	OPEN_RECHARGE,
	AGEING_COLOR_PARA2,
	AGEING_FADE_PARA2,
};

enum pcb_version {
	EV1 = 24,
	DVT = 32,
	PVT = 40,
};

typedef enum color_ctrl_type {
	LIGHT_COLOR,
	COLOR,
	TRANSPARENT,
	OPEN_CIRCUIT,
	SHORT_CIRCUIT,
	UNKNOWN = 0xFF,
} color_status;

typedef enum color_operation_status {
	DO_COLORING,
	DO_RECHARGE_COLORING,
	DO_FADE,
	DO_RECHARGE_FADE,
	DO_COLORING_EVT,
} operation_status;

typedef enum color_ctrl_temp_type {
	TEMP_ZONE_1,	/*(-15,-10]*/
	TEMP_ZONE_2,	/*(-10,-5]*/
	TEMP_ZONE_3,	/*(-5, 0]*/
	TEMP_ZONE_4,	/*( 0, 5]*/
	TEMP_ZONE_5,	/*( 5,10]*/
	TEMP_ZONE_6,	/*(10,15]*/
	TEMP_ZONE_7,	/*(15,20]*/
	TEMP_ZONE_8,	/*(20,35]*/
	TEMP_ZONE_9,	/*(35,45]*/
	TEMP_ZONE_10,	/*(45,70]*/
	ABNORMAL_TEMP,
} temp_zone;

struct color_ctrl_hw_resource {
	unsigned int        sleep_en_gpio;
	unsigned int        si_in_1_gpio;
	unsigned int        si_in_2_gpio;
	unsigned int        adc_on_gpio;              /*pm8250 gpio10*/
	unsigned int        oa_en_gpio;                /*sm8250  gpio8*/
	unsigned int        vm_coloring_gpio;    /*PM8250 LDO1 and gpio 11*/
	unsigned int        vm_fade_gpio;            /*PM8008 L2 and gpio 9*/
	struct iio_channel  *vm_v_chan;
	struct regulator     *coloring;   /* EC2 coloring power PM8250 LDO1*/
	struct regulator     *fade;          /* EC2 fade power PM8008 L2*/
};

struct color_ctrl_control_para {
	unsigned int        coloring_charge_para_1[20][10];
	unsigned int        coloring_charge_para_2[20][10];
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
	color_status			color_status;
	operation_status		operation_status;
	unsigned int			temp_status;
	temp_zone			temp_zone;
	int                             platform_support_project[10];
	int				match_project;
	int                             project_num;

	int				boot_status;
	bool				is_first_boot;
	int				temp_num;

	int				red_para_1_num;
	int				red_para_1_format[5];

	int				fade_para_1_num;
	int				fade_para_1_format[5];

	int				pcb_version;

	int				red_para_2_num;
	int				red_para_2_format[5];

	int				fade_para_2_num;
	int				fade_para_2_format[5];

	int                             operation;
	unsigned int			intermediate_wait_time;
	bool                            	need_recharge;
	bool                            	ageing_color_para2;
	bool                            	ageing_fade_para2;

	unsigned int			charge_volt;
	unsigned int			charge_time;
	unsigned int			recharge_volt;
	unsigned int			recharge_time;
	unsigned int			step_para[5];

	unsigned int			fade_recharge_waitime;
	unsigned int			coloring_recharge_waitime;
	unsigned int			fade_mandatory_flag;

	int				wait_check_time[8];
	int				fade_open_circuit_vol[8];
	int				red_open_circuit_vol[8];

	int				fade_to_red_retry_cnt;
	int				fade_to_red_check_volt[10];
	int				red_to_fade_retry_cnt;
	int				red_to_fade_check_volt[10];
	int				reset_ftm_vol[10];
	unsigned int 		open_circuit_flag;
	unsigned int 		recharge_time_flag;
	int				temperature_thd[15];
	unsigned int		temperature_thd_offect;

	int                             thermal_zone_device_num;
	char                            *thermal_zone_device_name[MAX_THERMAL_ZONE_DEVICE_NUM];
	int                             thermal_zone_device_weight[MAX_THERMAL_ZONE_DEVICE_WEIGHT_NUM];
	int
	thermal_zone_device_weight_sigh_bit[MAX_THERMAL_ZONE_DEVICE_WEIGHT_NUM];
	struct color_ctrl_control_para  *color_control_para;
	struct color_ctrl_control_para  *transparent_control_para;
	bool colorctrl_v2_support;                           /*colorctrl 2.0 support*/
	bool ec_step_voltage_support;			/*step voltage support*/
};

#endif
