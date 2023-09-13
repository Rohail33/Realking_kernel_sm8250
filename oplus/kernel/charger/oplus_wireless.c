// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/device_info.h>
#endif

#include "oplus_charger.h"
#include "oplus_vooc.h"
#include "oplus_gauge.h"
#include "oplus_adapter.h"
#ifdef OPLUS_CUSTOM_OP_DEF
#ifdef CONFIG_OP_WIRELESSCHG
#include "oplus_wlchg_policy.h"
#endif // CONFIG_OP_WIRELESS_CHG
#else
#include "oplus_wireless.h"
#endif // OPLUS_CUSTOM_OP_DEF
#include "oplus_debug_info.h"

static struct oplus_wpc_chip *g_wpc_chip = NULL;

int oplus_wpc_get_online_status(void)
{
	if (!g_wpc_chip) {
		return 0;
	}

	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_online_status)
		return g_wpc_chip->wpc_ops->wpc_get_online_status();
	else
		return 0;
}

int oplus_wpc_get_voltage_now(void)
{
	if (!g_wpc_chip) {
		return 0;
	}

	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_voltage_now)
		return g_wpc_chip->wpc_ops->wpc_get_voltage_now();
	else
		return 0;
}

int oplus_wpc_get_current_now(void)
{
	if (!g_wpc_chip) {
		return 0;
	}

	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_current_now)
		return g_wpc_chip->wpc_ops->wpc_get_current_now();
	else
		return 0;
}

int oplus_wpc_get_real_type(void)
{
	if (!g_wpc_chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}

	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_real_type)
		return g_wpc_chip->wpc_ops->wpc_get_real_type();
	else
		return POWER_SUPPLY_TYPE_UNKNOWN;
}

int oplus_wpc_get_max_wireless_power(void)
{
	if (!g_wpc_chip) {
		return -EINVAL;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_max_wireless_power) {
		return g_wpc_chip->wpc_ops->wpc_get_max_wireless_power();
	} else {
		return 0;
	}
}

int oplus_wpc_get_break_sub_crux_info(char *sub_crux_info)
{
	if (!g_wpc_chip)
		return -EINVAL;

	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_break_sub_crux_info)
		return g_wpc_chip->wpc_ops->wpc_get_break_sub_crux_info(sub_crux_info);

	return 0;
}

void oplus_wpc_set_otg_en_val(int value)
{
	return;
}

int oplus_wpc_get_otg_en_val(struct oplus_chg_chip *chip)
{
	return 0;
}

void oplus_wpc_set_vbat_en_val(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_vbat_en) {
		g_wpc_chip->wpc_ops->wpc_set_vbat_en(value);
		return;
	} else {
		return;
	}
}

void oplus_wpc_set_booster_en_val(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_booster_en) {
		g_wpc_chip->wpc_ops->wpc_set_booster_en(value);
		return;
	} else {
		return;
	}
}

void oplus_wpc_set_ext1_wired_otg_en_val(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_ext1_wired_otg_en) {
		g_wpc_chip->wpc_ops->wpc_set_ext1_wired_otg_en(value);
		return;
	} else {
		return;
	}
}

void oplus_wpc_set_ext2_wireless_otg_en_val(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_ext2_wireless_otg_en) {
		g_wpc_chip->wpc_ops->wpc_set_ext2_wireless_otg_en(value);
		return;
	} else {
		return;
	}
}

void oplus_wpc_set_rtx_function_prepare(void)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_rtx_function_prepare) {
		g_wpc_chip->wpc_ops->wpc_set_rtx_function_prepare();
		return;
	} else {
		return;
	}
}

void oplus_wpc_set_rtx_function(bool enable)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_rtx_function) {
		g_wpc_chip->wpc_ops->wpc_set_rtx_function(enable);
		return;
	} else {
		return;
	}
}

void oplus_wpc_dis_wireless_chg(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_dis_wireless_chg) {
		g_wpc_chip->wpc_ops->wpc_dis_wireless_chg(value);
		return;
	} else {
		return;
	}
}

int oplus_wpc_get_idt_en_val(void)
{
	return 0;
}

bool oplus_wpc_get_wired_otg_online(void)
{
	return false;
}

bool oplus_wpc_get_wired_chg_present(void)
{
	return false;
}

void oplus_wpc_dcin_irq_enable(bool enable)
{
	return;
}

bool oplus_wpc_get_wireless_charge_start(void)
{
	if (!g_wpc_chip) {
		return false;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_wireless_charge_start) {
		return g_wpc_chip->wpc_ops->wpc_get_wireless_charge_start();
	} else {
		return false;
	}
}

bool oplus_wpc_get_normal_charging(void)
{
	if (!g_wpc_chip) {
		return false;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_normal_charging) {
		return g_wpc_chip->wpc_ops->wpc_get_normal_charging();
	} else {
		return false;
	}
}

bool oplus_wpc_get_fast_charging(void)
{
	if (!g_wpc_chip) {
		return false;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_fast_charging) {
		return g_wpc_chip->wpc_ops->wpc_get_fast_charging();
	} else {
		return false;
	}
}

bool oplus_wpc_get_otg_charging(void)
{
	if (!g_wpc_chip) {
		return false;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_otg_charging) {
		return g_wpc_chip->wpc_ops->wpc_get_otg_charging();
	} else {
		return false;
	}
}

bool oplus_wpc_get_ffc_charging(void)
{
	if (!g_wpc_chip) {
		return false;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_ffc_charging) {
		return g_wpc_chip->wpc_ops->wpc_get_ffc_charging();
	} else {
		return false;
	}
}

bool oplus_wpc_check_chip_is_null(void)
{
	if (!g_wpc_chip) {
		return true;
	}
	return false;
}

bool oplus_wpc_get_fw_updating(void)
{
	if (!g_wpc_chip) {
		return false;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_fw_updating) {
		return g_wpc_chip->wpc_ops->wpc_get_fw_updating();
	} else {
		return false;
	}
}

int oplus_wpc_get_adapter_type(void)
{
	if (!g_wpc_chip) {
		return -EINVAL;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_adapter_type) {
		return g_wpc_chip->wpc_ops->wpc_get_adapter_type();
	} else {
		return 0;
	}
}

int oplus_wpc_get_skewing_curr(void)
{
	if (!g_wpc_chip)
		return 0;

	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_skewing_curr)
		return g_wpc_chip->wpc_ops->wpc_get_skewing_curr();

	return 0;
}

bool oplus_wpc_get_verity(void)
{
	if (!g_wpc_chip)
		return false;

	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_get_verity)
		return g_wpc_chip->wpc_ops->wpc_get_verity();

	return false;
}

int oplus_wpc_set_tx_start(void)
{
	if (!g_wpc_chip) {
		return -EINVAL;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_tx_start) {
		return g_wpc_chip->wpc_ops->wpc_set_tx_start();
	} else {
		return 0;
	}
}

void oplus_wpc_dis_tx_power(void)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops->wpc_dis_tx_power) {
		g_wpc_chip->wpc_ops->wpc_dis_tx_power();
		return;
	} else {
		return;
	}
}

void oplus_wpc_print_log(void)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_print_log) {
		g_wpc_chip->wpc_ops->wpc_print_log();
		return;
	} else {
		return;
	}
}

void oplus_wpc_set_wrx_en_value(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_wrx_en) {
		g_wpc_chip->wpc_ops->wpc_set_wrx_en(value);
		return;
	} else {
		return;
	}
}

int oplus_wpc_get_wrx_en_val(void)
{
	struct oplus_wpc_chip *chip = g_wpc_chip;
	if (!chip) {
		return 0;
	}
	if (chip->wpc_gpios.wrx_en_gpio <= 0) {
		chg_err("wrx_en_gpio not exist, return\n");
		return 0;
	}
	if (IS_ERR_OR_NULL(chip->wpc_gpios.pinctrl) || IS_ERR_OR_NULL(chip->wpc_gpios.wrx_en_active) ||
	    IS_ERR_OR_NULL(chip->wpc_gpios.wrx_en_sleep)) {
		chg_err("pinctrl null, return\n");
		return 0;
	}
	return gpio_get_value(chip->wpc_gpios.wrx_en_gpio);
}

int oplus_wpc_get_wrx_otg_en_val(void)
{
	struct oplus_wpc_chip *chip = g_wpc_chip;
	if (!chip) {
		return 0;
	}
	if (chip->wpc_gpios.wrx_otg_en_gpio <= 0) {
		chg_err("wrx_otg_en_gpio not exist, return\n");
		return 0;
	}
	if (IS_ERR_OR_NULL(chip->wpc_gpios.pinctrl) || IS_ERR_OR_NULL(chip->wpc_gpios.wrx_otg_en_active) ||
	    IS_ERR_OR_NULL(chip->wpc_gpios.wrx_otg_en_sleep)) {
		chg_err("pinctrl null, return\n");
		return 0;
	}
	return gpio_get_value(chip->wpc_gpios.wrx_otg_en_gpio);
}

void oplus_wpc_set_wrx_otg_en_value(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_wrx_otg_en) {
		g_wpc_chip->wpc_ops->wpc_set_wrx_otg_en(value);
		return;
	} else {
		return;
	}
}

void oplus_wpc_set_wls_pg_value(int value)
{
	if (!g_wpc_chip) {
		return;
	}
	if (g_wpc_chip->wpc_ops && g_wpc_chip->wpc_ops->wpc_set_wls_pg) {
		g_wpc_chip->wpc_ops->wpc_set_wls_pg(value);
		return;
	} else {
		return;
	}
}

int oplus_wpc_get_boot_version(void)
{
	if (!g_wpc_chip) {
		return -1;
	}
	if (g_wpc_chip->wpc_chg_data) {
		return g_wpc_chip->wpc_chg_data->boot_version;
	} else {
		return -1;
	}
}

int oplus_wpc_get_rx_version(void)
{
	if (!g_wpc_chip) {
		return -1;
	}
	if (g_wpc_chip->wpc_chg_data) {
		return g_wpc_chip->wpc_chg_data->rx_version;
	} else {
		return -1;
	}
}

int oplus_wpc_get_tx_version(void)
{
	if (!g_wpc_chip) {
		return -1;
	}
	if (g_wpc_chip->wpc_chg_data) {
		return g_wpc_chip->wpc_chg_data->tx_version;
	} else {
		return -1;
	}
}

int oplus_wpc_get_vout(void)
{
	if (!g_wpc_chip) {
		return -1;
	}
	if (g_wpc_chip->wpc_chg_data) {
		return g_wpc_chip->wpc_chg_data->vout;
	} else {
		return -1;
	}
}

int oplus_wpc_get_iout(void)
{
	if (!g_wpc_chip) {
		return -1;
	}
	if (g_wpc_chip->wpc_chg_data) {
		return g_wpc_chip->wpc_chg_data->iout;
	} else {
		return -1;
	}
}

bool oplus_wpc_get_silent_mode(void)
{
	if (!g_wpc_chip) {
		return -1;
	}
	if (g_wpc_chip->wpc_chg_data) {
		return g_wpc_chip->wpc_chg_data->work_silent_mode;
	} else {
		return -1;
	}
}

int oplus_wpc_get_dock_type(void)
{
	if (!g_wpc_chip) {
		return -1;
	}
	if (g_wpc_chip->wpc_chg_data) {
		return g_wpc_chip->wpc_chg_data->dock_version;
	} else {
		return -1;
	}
}

void oplus_get_wpc_chip_handle(struct oplus_wpc_chip **chip)
{
	*chip = g_wpc_chip;
}

void oplus_wpc_init(struct oplus_wpc_chip *chip)
{
	g_wpc_chip = chip;
}
