/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_QUIRKS_H_
#define _OPLUS_QUIRKS_H_

#define PLUGINFO_MAX_NUM                12
#define ABNORMAL_DISCONNECT_INTERVAL    1500
#define KEEP_CONNECT_TIME_OUT           80*1000
#define PPS_CONNECT_ERROR_COUNT_LEVEL_1 3
#define PPS_CONNECT_ERROR_COUNT_LEVEL_2 6
#define PPS_CONNECT_ERROR_COUNT_LEVEL_3 9
#define PLUGOUT_RETRY                   1
#define PLUGOUT_RETRY                   1
#define PLUGOUT_WAKEUP_TIMEOUT          ABNORMAL_DISCONNECT_INTERVAL + 200

typedef enum {
	QUIRKS_NORMAL = 0,
	QUIRKS_STOP_ADSP_VOOCPHY,
	QUIRKS_STOP_PPS,
} QUIRKS_STATUS;

struct plug_info {
	unsigned long plugin_jiffies;
	unsigned long plugout_jiffies;
	bool abnormal_diconnect;
	int number;
	struct list_head	list;
};

struct oplus_quirks_chip {
	struct device *dev;
	atomic_t last_plugin_status;
	atomic_t oplus_quirks_init_status;
	unsigned long  keep_connect_jiffies;
	int plugout_retry;
	int quirks_plugin_status;
	int quirks_adsp_voocphy_en;
	int keep_connect;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock suspend_lock;
#else
	struct wakeup_source *awake_lock;
#endif
	struct plug_info plug_info_head;
	struct timer_list update_plugin_timer;
	struct delayed_work voocphy_turn_on;
	struct power_supply *batt_psy;
};
int oplus_quirks_notify_plugin(int plugin);
int oplus_quirks_init(struct oplus_chg_chip *chip);
int oplus_set_quirks_plug_status(int mask, int status);
int oplus_get_quirks_plug_status(int mask);
int oplus_clear_quirks_plug_status(void);
int oplus_quirks_keep_connect_status(void);
#endif /*_OPLUS_CHARGER_H_*/
