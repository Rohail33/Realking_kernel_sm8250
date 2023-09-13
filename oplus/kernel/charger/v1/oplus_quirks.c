// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/rtc.h>

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/of.h>

#include <linux/bitops.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/jiffies.h>

#include "oplus_charger.h"
#include "oplus_pps.h"
#include "oplus_quirks.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_adapter.h"
#include "chargepump_ic/oplus_pps_cp.h"
#include "oplus_pps_ops_manager.h"
#include "voocphy/oplus_voocphy.h"

static struct oplus_quirks_chip *g_quirks_chip;
static void oplus_quirks_update_plugin_timer(struct oplus_quirks_chip *chip, unsigned int ms);

int oplus_get_quirks_plug_status(int type) {
	struct oplus_quirks_chip *chip = g_quirks_chip;
	int mask = 0;
	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return 0;
	mask = 1 << type;
	chg_err(":%d, mask:%d\n", chip->quirks_plugin_status, (chip->quirks_plugin_status & mask) >> type);
	return (chip->quirks_plugin_status & mask) >> type;
}

int oplus_set_quirks_plug_status(int type, int status) {
	struct oplus_quirks_chip *chip = g_quirks_chip;
	int enable = 0;
	int mask = 0;
	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return 0;
	enable = status << type;
	mask = 1 << type;
	chip->quirks_plugin_status = (chip->quirks_plugin_status & ~mask) | (enable & mask);
	chg_err(":%d, mask:%d,enable:%d,type:%d,status:%d\n", chip->quirks_plugin_status, mask, enable, type, status);
	return 0;
}

int oplus_clear_quirks_plug_status(void) {
	struct oplus_quirks_chip *chip = g_quirks_chip;
	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return 0;
	chg_err(":%d\n", chip->quirks_plugin_status);
	chip->quirks_plugin_status = QUIRKS_NORMAL;
	return chip->quirks_plugin_status;
}

static void oplus_quirks_awake_init(struct oplus_quirks_chip *chip)
{
	if (!chip) {
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND, "battery quirks connect suspend wakelock");

#else
	chip->awake_lock = wakeup_source_register(NULL, "oplus_quirks_wakelock");
#endif
}

static void oplus_quirks_set_awake(struct oplus_quirks_chip *chip, int time_ms)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))

	if (awake) {
		wake_lock(&chip->suspend_lock);
	} else {
		wake_unlock(&chip->suspend_lock);
	}
#else
	if (!chip || !chip->awake_lock)
		return;
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return;
	chg_err(":%d, :%p\n", time_ms, chip->awake_lock->timer.function);

	__pm_wakeup_event(chip->awake_lock, time_ms);

	chg_err("end :%d\n", time_ms);
#endif
}
int abnormal_diconnect_count(void) {
	int count = 0;
	struct plug_info *info;
	struct list_head *pos, *n;
	struct oplus_quirks_chip *chip = g_quirks_chip;
	int i = 0;
	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}
	if (list_empty(&g_quirks_chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return 0;
	}
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return 0;
	list_for_each_safe(pos, n, &g_quirks_chip->plug_info_head.list) {
		info = list_entry(pos, struct plug_info, list);
		if (time_is_after_jiffies(info->plugout_jiffies + msecs_to_jiffies(KEEP_CONNECT_TIME_OUT))) {
			if (info->abnormal_diconnect == 1)
				count++;
		}
		i++;
		chg_err("info%d[%d] plugout_jiffies:%lu,plugin_jiffies:%lu, jiffies:%lu, abnormal_diconnect:%d, count:%d, in_20s:%d\n",
				info->number, i, info->plugout_jiffies, info->plugin_jiffies, jiffies, info->abnormal_diconnect, count,
				time_is_after_jiffies(info->plugin_jiffies + msecs_to_jiffies(KEEP_CONNECT_TIME_OUT)));
	}
	return count;
}

void clear_abnormal_diconnect_count(void) {
	struct plug_info *info;
	struct list_head *pos, *n;
	struct oplus_quirks_chip *chip = g_quirks_chip;
	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return;
	}
	if (list_empty(&g_quirks_chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return;
	}
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return;
	list_for_each_safe(pos, n, &g_quirks_chip->plug_info_head.list) {
		info = list_entry(pos, struct plug_info, list);
		info->abnormal_diconnect = 0;
	}
	chg_err("!!\n");
	return;
}

static void oplus_quirks_voocphy_turn_on(int enable)
{
	struct oplus_quirks_chip *chip = g_quirks_chip;

	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return;
	}
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return;
	chip->quirks_adsp_voocphy_en = enable;
	chg_err("adsp_voocphy_en:%d\n", chip->quirks_adsp_voocphy_en);
	schedule_delayed_work(&chip->voocphy_turn_on, 0);

	return;
}

static void oplus_quirks_voocphy_turn_on_work(struct work_struct *work)
{
	struct oplus_quirks_chip *chip = g_quirks_chip;
	int voocphy_enable;

	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return;
	}
	if(oplus_is_vooc_project() != DUAL_BATT_150W && oplus_is_vooc_project() != DUAL_BATT_240W)
		return;
	voocphy_enable = oplus_get_adsp_voocphy_enable();
	chg_err("quirks_adsp_voocphy_en:%d, voocphy_enable:%d\n", chip->quirks_adsp_voocphy_en, voocphy_enable);
	if (voocphy_enable != chip->quirks_adsp_voocphy_en) {
		if (chip->quirks_adsp_voocphy_en) {
			oplus_adsp_voocphy_turn_on();
		} else {
			oplus_adsp_voocphy_turn_off();
		}
	}
	return;
}
int oplus_quirks_keep_connect_status(void) {
	struct oplus_quirks_chip *chip = g_quirks_chip;
	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}
	if (list_empty(&g_quirks_chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return 0;
	}
	if(oplus_is_vooc_project() == DUAL_BATT_150W || oplus_is_vooc_project() == DUAL_BATT_240W) {
		if (chip->keep_connect) {
			pps_err("keep_connect!:last_plugin_status:%d, keep_connect:%d, keep_connect_jiffies:%lu, jiffies:%lu\n",
					chip->last_plugin_status, chip->keep_connect, chip->keep_connect_jiffies, jiffies);
			return 1;
		}
	}
	return 0;
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void update_plugin_status(unsigned long data)
#else
static void update_plugin_status(struct timer_list *unused)
#endif
{
	struct oplus_quirks_chip *chip = g_quirks_chip;
	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return;
	}
	if (list_empty(&g_quirks_chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return;
	}
	chg_err(":last_plugin_status:%d, keep_connect:%d, keep_connect_jiffies:%lu, jiffies:%lu\n",
			atomic_read(&chip->last_plugin_status), chip->keep_connect, chip->keep_connect_jiffies, jiffies);
	if (atomic_read(&chip->last_plugin_status) == 0) {
		oplus_pps_clear_last_charging_status();
		oplus_voocphy_clear_last_fast_chg_type();
		oplus_clear_quirks_plug_status();
		oplus_adsp_voocphy_force_svooc(0);
		clear_abnormal_diconnect_count();
		if (oplus_chg_get_mmi_value() == 1) {
			if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
				oplus_quirks_voocphy_turn_on(1);
			}
		}

		chip->keep_connect = 0;
		if (chip->batt_psy)
			power_supply_changed(chip->batt_psy);
		chg_err("update_plugin!");

		if (chip->plugout_retry < PLUGOUT_RETRY) {
			chg_err("retry\n");
			chip->plugout_retry++;
			oplus_quirks_set_awake(chip, PLUGOUT_WAKEUP_TIMEOUT);
			mod_timer(&chip->update_plugin_timer, jiffies + msecs_to_jiffies(ABNORMAL_DISCONNECT_INTERVAL));
		} else {
			chg_err("unwakeup\n");
		}
	} else {
		chg_err("do nothing\n");
	}
	return;
}

static void oplus_quirks_update_plugin_timer(struct oplus_quirks_chip *chip, unsigned int ms)
{
	if (!chip) {
		chg_err("oplus_vooc_chip is not ready\n");
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mod_timer(&chip->update_plugin_timer, jiffies + msecs_to_jiffies(25000));
#else
	try_to_del_timer_sync(&chip->update_plugin_timer);
	chip->update_plugin_timer.expires  = jiffies + msecs_to_jiffies(ms);
	add_timer(&chip->update_plugin_timer);
#endif
}

int oplus_quirks_notify_plugin(int plugin) {
	struct oplus_quirks_chip *chip = g_quirks_chip;
	struct plug_info *info;
	static struct plug_info *last_plug_info = NULL;
	int count;
	int cc_online = 0;
	static int last_plugin = -1;

	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}

	if (atomic_read(&chip->oplus_quirks_init_status) == 0) {
		chg_err("oplus_quirks_init is not completed!\n");
		return 0;
	}

	if (list_empty(&chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return 0;
	}
	if(last_plug_info == NULL) {
		last_plug_info = list_entry(&g_quirks_chip->plug_info_head.list, struct plug_info, list);
	}

	chg_err("plugin:%d, last_plugin:%d\n", plugin, last_plugin);
	if (last_plugin == plugin) {
		chg_err("plugin:%d, last_plugin:%d, return\n", plugin, last_plugin);
		return 0;
	}
	last_plugin = plugin;
	if (!list_empty(&chip->plug_info_head.list)) {
		if (atomic_read(&chip->last_plugin_status) == plugin || plugin == 1)
			info = last_plug_info;
		else
			info = list_entry(last_plug_info->list.next, struct plug_info, list);
		last_plug_info = info;
		atomic_set(&chip->last_plugin_status, plugin);
		if (plugin == 0) {
			chip->plugout_retry = 0;
			count = abnormal_diconnect_count();
			info->plugout_jiffies = jiffies;
			chip->keep_connect_jiffies = jiffies;
			cc_online = oplus_get_ccdetect_online();
			chg_err("%d:count:%d, cc_online:%d, do not keep connect", __LINE__, count, cc_online);
			oplus_quirks_set_awake(chip, PLUGOUT_WAKEUP_TIMEOUT);
			if (cc_online == 1) {
				if (count >= PPS_CONNECT_ERROR_COUNT_LEVEL_2) {
					if (count >= PPS_CONNECT_ERROR_COUNT_LEVEL_3) {
						chip->keep_connect = 0;
						chg_err("abnormal_diconnect_count:%d, cc_online:%d, force 5V2A do not keep connect", count, cc_online);
					} else {
						chip->keep_connect = 1;
						chg_err("abnormal_diconnect_count:%d, cc_online:%d, force 5V2A keep connect", count, cc_online);
					}
					oplus_pps_clear_last_charging_status();
					oplus_voocphy_clear_last_fast_chg_type();
					oplus_set_quirks_plug_status(QUIRKS_STOP_PPS, 1);
					oplus_set_quirks_plug_status(QUIRKS_STOP_ADSP_VOOCPHY, 1);
					oplus_adsp_voocphy_force_svooc(0);
					if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
						oplus_quirks_voocphy_turn_on(0);
					}
					oplus_quirks_update_plugin_timer(chip, ABNORMAL_DISCONNECT_INTERVAL);
				} else if (count >= PPS_CONNECT_ERROR_COUNT_LEVEL_1) {
					chip->keep_connect = 1;
					oplus_set_quirks_plug_status(QUIRKS_STOP_PPS, 1);
					oplus_adsp_voocphy_force_svooc(1);
					oplus_quirks_update_plugin_timer(chip, ABNORMAL_DISCONNECT_INTERVAL);
					chg_err("abnormal_diconnect_count:%d, cc_online:%d, do not keep connect, force svooc", count, cc_online);
				} else {
					chip->keep_connect = 1;
					oplus_adsp_voocphy_force_svooc(0);
					oplus_quirks_update_plugin_timer(chip, ABNORMAL_DISCONNECT_INTERVAL);
					chg_err("abnormal_diconnect_count:%d, cc_online:%d, after 1s, check update_plugin again", count, cc_online);
				}
			} else {
				chip->keep_connect = 0;
				oplus_pps_clear_last_charging_status();
				oplus_voocphy_clear_last_fast_chg_type();
				oplus_clear_quirks_plug_status();
				clear_abnormal_diconnect_count();
				if (oplus_chg_get_mmi_value() == 1) {
					if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
						oplus_quirks_voocphy_turn_on(1);
					}
				}
				oplus_adsp_voocphy_force_svooc(0);
				chg_err("abnormal_diconnect_count:%d, cc_online:%d, do not keep connect", count, cc_online);
			}
		} else {
			info->plugin_jiffies = jiffies;
			if (time_is_after_jiffies(info->plugout_jiffies + msecs_to_jiffies(ABNORMAL_DISCONNECT_INTERVAL))) {
				info->abnormal_diconnect = 1;
				chg_err("abnormal_diconnect, info->plugout_jiffies:%lu, info->plugin_jiffies:%lu", info->plugout_jiffies, info->plugin_jiffies);
			}
		}
	}
	chg_err(":last_plugin_status:%d, keep_connect:%d keep_connect_jiffies:%lu, jiffies:%lu\n",
			atomic_read(&chip->last_plugin_status), chip->keep_connect, chip->keep_connect_jiffies, jiffies);
	oplus_pps_clear_startup_retry();
	return 0;
}

int oplus_quirks_init(struct oplus_chg_chip *chg_chip)
{
	int i = 0;
	struct plug_info *info;
	struct list_head *pos, *n;
	struct oplus_quirks_chip *quirks_chip_init;

	chg_err("start\n");

	quirks_chip_init = kzalloc(sizeof(struct oplus_quirks_chip), GFP_KERNEL);
	if (!quirks_chip_init) {
		chg_err("quirks_chip_init already kzalloc init fail!\n");
		return 0;
	}
	g_quirks_chip = quirks_chip_init;

	oplus_quirks_awake_init(g_quirks_chip);
	INIT_DELAYED_WORK(&g_quirks_chip->voocphy_turn_on, oplus_quirks_voocphy_turn_on_work);
	g_quirks_chip->batt_psy = power_supply_get_by_name("battery");
	if (!g_quirks_chip->batt_psy) {
		chg_err("battery psy not found\n");
	}

	INIT_LIST_HEAD(&g_quirks_chip->plug_info_head.list);

	for(i = 0 ; i < PLUGINFO_MAX_NUM; i++) {
		info = kmalloc(sizeof(struct plug_info), GFP_KERNEL);
		if (!info)
			goto error;
		memset(info, 0, sizeof(struct plug_info));
		list_add(&info->list, &g_quirks_chip->plug_info_head.list);
		info->number = i;
		chg_err("%d\n", i);
	}

	atomic_set(&g_quirks_chip->last_plugin_status, 0);
	g_quirks_chip->keep_connect = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	init_timer(g_quirks_chip->update_plugin_timer);
	g_quirks_chip->update_plugin_timer.data = (unsigned long)g_quirks_chip;
	g_quirks_chip->update_plugin_timer.function = update_plugin_status;
#else
	timer_setup(&g_quirks_chip->update_plugin_timer, update_plugin_status, 0);
#endif
	atomic_set(&g_quirks_chip->oplus_quirks_init_status, 1);

	return 0;
error:
	chg_err("init error\n");
	if (!list_empty(&g_quirks_chip->plug_info_head.list)) {
		list_for_each_safe(pos, n, &g_quirks_chip->plug_info_head.list) {
			info = list_entry(pos, struct plug_info, list);
			list_del(&info->list);
			kfree(info);
		}
	}
	kfree(g_quirks_chip);
	return -1;
}
