/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __OPLUS_SSC_INTERACTIVE_H__
#define __OPLUS_SSC_INTERACTIVE_H__

#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#ifdef CONFIG_ARM
#include <linux/sched.h>
#else
#include <linux/wait.h>
#endif
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/notifier.h>



enum {
	NONE_TYPE = 0,
	LCM_DC_MODE_TYPE,
	LCM_BRIGHTNESS_TYPE,
	LCM_BRIGHTNESS_TYPE_SEC,
	LCM_POWER_MODE,
	LCM_POWER_MODE_SEC,
	MAX_INFO_TYPE,
};


enum {
	LCM_DC_OFF = 0,
	LCM_DC_ON = 1
};


struct als_info{
	uint16_t brightness;
	uint16_t pad_brightness;
	uint16_t dc_mode;
	uint16_t power_mode;
	uint16_t pad_power_mode;
};

struct fifo_frame{
	uint8_t type;
	uint16_t data;
};

struct dvb_coef{
	uint16_t dvb1;
	uint16_t dvb2;
	uint16_t dvb3;
	uint16_t dvb4;
	uint16_t dvb_l2h;
	uint16_t dvb_h2l;

};

struct ssc_interactive{

	struct als_info a_info;
	struct miscdevice mdev;
	DECLARE_KFIFO_PTR(fifo, struct fifo_frame);
	spinlock_t fifo_lock;
	spinlock_t rw_lock;
	wait_queue_head_t wq;
	struct notifier_block nb;
	struct dvb_coef m_dvb_coef;

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
	bool notify_work_regiseted;
	uint8_t notify_work_retry;
	void *notifier_cookie;
	struct drm_panel *active_panel;
	struct delayed_work regiseter_lcd_notify_work;
#endif
};


#endif
