/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _FP_LINUX_DIRVER_H_
#define _FP_LINUX_DIRVER_H_
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include "../include/oplus_fp_common.h"

/*#define FP_SPI_DEBUG*/
#define FP_SPI_DEBUG
#ifdef FP_SPI_DEBUG
//#define DEBUG_PRINT(fmt, args...) pr_err(fmt, ## args)
#define DEBUG_PRINT printk

#else
#define DEBUG_PRINT(fmt, args...)
#endif

#define ET713_MAJOR                 100 /* assigned */
#define N_SPI_MINORS                32  /* ... up to 256 */


/* ------------------------- Opcode -------------------------------------*/
#define FP_REGISTER_READ            0x01
#define FP_REGISTER_WRITE           0x02
#define FP_GET_ONE_IMG              0x03
#define FP_SENSOR_RESET             0x04
#define FP_POWER_ONOFF              0x05
#define FP_SET_SPI_CLOCK            0x06
#define FP_RESET_SET                0x07
#define FP_POWER_ON_RESET           0x08
#define FP_CLEAN_TOUCH_FLAG         0x09

/* trigger signal initial routine*/
#define INT_TRIGGER_INIT            0xa4
/* trigger signal close routine*/
#define INT_TRIGGER_CLOSE           0xa5
/* read trigger status*/
#define INT_TRIGGER_READ            0xa6
/* polling trigger status*/
#define INT_TRIGGER_POLLING         0xa7
/* polling abort*/
#define INT_TRIGGER_ABORT           0xa8

#define FP_FREE_GPIO                0xaf
#define FP_SPICLK_ENABLE            0xaa
#define FP_SPICLK_DISABLE           0xab
#define DELETE_DEVICE_NODE          0xac

#define FP_SPIPIN_SETTING           0xad
#define FP_SPIPIN_PULLLOW           0xae

#define FP_POWERSETUP               0xb0
#define FP_WAKELOCK_TIMEOUT_ENABLE  0xb1
#define FP_WAKELOCK_TIMEOUT_DISABLE 0xb2
#define GET_SCREEN_ONOFF            0xb3

#define DRDY_IRQ_ENABLE             1
#define DRDY_IRQ_DISABLE            0

#define EGIS_UI_DISAPPREAR          0
#define EGIS_UI_READY               1

/* interrupt polling */
unsigned int fps_interrupt_poll(
    struct file *file,
    struct poll_table_struct *wait
);
struct interrupt_desc {
    int gpio;
    int number;
    char *name;
    int int_count;
    struct timer_list timer;
    bool finger_on;
    int detect_period;
    int detect_threshold;
    bool drdy_irq_flag;
};

/* ------------------------- Structure ------------------------------*/

struct egistec_data {
    dev_t devt;
    spinlock_t spi_lock;
    struct spi_device  *spi;
    struct platform_device *pd;
    struct list_head device_entry;
    /* buffer is NULL unless this device is open (users > 0) */
    struct mutex buf_lock;
    unsigned users;
    u8 *buffer;
    unsigned int irqPin;        /* interrupt GPIO pin number */
    unsigned int rstPin;         /* Reset GPIO pin number */
    unsigned int vdd_18v_Pin;    /* Reset GPIO pin number */
    unsigned int vcc_33v_Pin;    /* Reset GPIO pin number */
    struct input_dev    *input_dev;
    bool property_navigation_enable;
    struct fp_underscreen_info fp_tpinfo;
    struct notifier_block notifier;
    char fb_black;

#ifdef CONFIG_OF
    struct pinctrl *pinctrl_gpios;
    struct pinctrl *pinctrl_gpios_spi;
    struct pinctrl_state *pins_irq;
    struct pinctrl_state *pstate_spi_6mA;
    struct pinctrl_state *pstate_spi_6mA_pull_low;
    struct pinctrl_state *pstate_default;
    struct pinctrl_state *pstate_cs_func;
    struct pinctrl_state *pins_reset_high, *pins_reset_low;
    struct pinctrl_state *pins_vcc_high, *pins_vcc_low;
    struct pinctrl_state *pins_miso_spi, *pins_miso_pullhigh, *pins_miso_pulllow;
    struct pinctrl_state *pins_mosi_spi, *pins_mosi_pullhigh, *pins_mosi_pulllow;
    struct pinctrl_state *pins_cs_spi, *pins_cs_pullhigh, *pins_cs_pulllow;
    struct pinctrl_state *pins_clk_spi, *pins_clk_pullhigh, *pins_clk_pulllow;
#endif


};

enum NETLINK_CMD {
    EGIS_NET_EVENT_TEST = 0,
    EGIS_NET_EVENT_IRQ = 1,
    EGIS_NET_EVENT_SCR_OFF,
    EGIS_NET_EVENT_SCR_ON,
    EGIS_NET_EVENT_TP_TOUCHDOWN,
    EGIS_NET_EVENT_TP_TOUCHUP,
    EGIS_NET_EVENT_UI_READY,
    EGIS_NET_EVENT_MAX,
};


/* ------------------------- Interrupt ------------------------------*/
/* interrupt init */
int Interrupt_Init(
    struct egistec_data *egistec,
    int int_mode,
    int detect_period,
    int detect_threshold);
/* interrupt free */
int Interrupt_Free(struct egistec_data *egistec);
void fps_interrupt_abort(void);
void egis_sendnlmsg(char *msg);
int egis_netlink_init(void);
void egis_netlink_exit(void);
#endif
