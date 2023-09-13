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
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
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

#include <linux/printk.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include "../include/oplus_fp_common.h"
#include <linux/msm_drm_notify.h>
#include <linux/notifier.h>
#include <linux/fb.h>
/*#define FP_PD_DEBUG*/
#define FP_PD_DEBUG

#ifdef FP_PD_DEBUG
#define DEBUG_PRINT(fmt, args...) pr_err(fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)
#endif

#define ET713_MAJOR					100 /* assigned */
#define N_PD_MINORS				32  /* ... up to 256 */

#define FP_ADDRESS_0				0x00
#define FP_WRITE_ADDRESS			0xAC
#define FP_READ_DATA				0xAF
#define FP_WRITE_DATA				0xAE

/* ------------------------- Register Definition ------------------------*/
/*
 * Sensor Registers
 */

#define FDATA_FP_ADDR				0x00
#define FSTATUS_FP_ADDR				0x01
/*
 * Detect Define
 */
#define FRAME_READY_MASK			0x01

/* ------------------------- Opcode -------------------------------------*/
#define FP_REGISTER_READ			0x01
#define FP_REGISTER_WRITE			0x02
#define FP_GET_ONE_IMG				0x03
#define FP_SENSOR_RESET				0x04
#define FP_POWER_ONOFF				0x05
#define FP_SET_SPI_CLOCK			0x06
#define FP_RESET_SET				0x07

#define WAIT_TP_TOUCH_DOWN			0x16
#define WAIT_UI_READY				0x18
#define WAIT_TP_TOUCH_UP			0x17
/* trigger signal initial routine*/
#define INT_TRIGGER_INIT			0xa4
/* trigger signal close routine*/
#define INT_TRIGGER_CLOSE			0xa5
/* read trigger status*/
#define INT_TRIGGER_READ			0xa6
/* polling trigger status*/
#define INT_TRIGGER_POLLING			0xa7
/* polling abort*/
#define INT_TRIGGER_ABORT			0xa8
#define FP_TRANSFER_SYNC			0xAA

#define DRDY_IRQ_ENABLE				1
#define DRDY_IRQ_DISABLE			0

#define EGIS_UI_DISAPPREAR		0
#define EGIS_UI_READY			1

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
struct egis_ioc_transfer {
	u8 *tx_buf;
	u8 *rx_buf;

	__u32 len;
	__u32 speed_hz;

	__u16 delay_usecs;
	__u8 bits_per_word;
	__u8 cs_change;
	__u8 opcode;
	__u8 pad[3];

};

#define EGIS_IOC_MAGIC			'k'
#define EGIS_MSGSIZE(N) \
	((((N)*(sizeof(struct egis_ioc_transfer))) < (1 << _IOC_SIZEBITS)) \
		? ((N)*(sizeof(struct egis_ioc_transfer))) : 0)
#define EGIS_IOC_MESSAGE(N) _IOW(EGIS_IOC_MAGIC, 0, char[EGIS_MSGSIZE(N)])

static const char * const pctl_names[] = {
	"et713_reset_reset",
	"et713_reset_active",
	"et713_irq_active",
};

struct egis_data {
	dev_t devt;
	spinlock_t pd_lock;
	struct platform_device *pd;
	struct list_head device_entry;

	struct pinctrl *fingerprint_pinctrl;
	struct pinctrl_state *pinctrl_state[ARRAY_SIZE(pctl_names)];
//	struct regulator *vreg[ARRAY_SIZE(vreg_conf)];
	struct regulator *vreg[3];

	/* buffer is NULL unless this device is open (users > 0) */
	struct mutex buf_lock;
	unsigned int users;
	u8 *buffer;

	unsigned int irqPin;	    /* interrupt GPIO pin number */
	unsigned int rstPin;	    /* Reset GPIO pin number */

	struct input_dev	*input_dev;
	bool property_navigation_enable;
    struct fp_underscreen_info fp_tpinfo;
	struct notifier_block notifier;
	char fb_black;
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
	struct egis_data *egis,
	int int_mode,
	int detect_period,
	int detect_threshold);

/* interrupt free */
int Interrupt_Free(struct egis_data *egis);

void fps_interrupt_abort(void);
void egis_sendnlmsg(char *msg);
int egis_netlink_init(void);
void egis_netlink_exit(void);

/* ------------------------- Data Transfer --------------------------*/
/*[REM] int fp_io_read_register(struct fp_data *fp, u8 *addr, u8 *buf); */
/*[REM] int fp_io_write_register(struct fp_data *fp, u8 *buf);*/
/*[REM] int fp_io_get_one_image(struct fp_data *fp, u8 *buf, u8 *image_buf);*/
/*[REM] int fp_read_register(struct fp_data *fp, u8 addr, u8 *buf);*/
/*[REM] int fp_mass_read(struct fp_data *fp, u8 addr, u8 *buf, int read_len);*/
#endif
