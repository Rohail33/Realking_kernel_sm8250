// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include "oplus_switching.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include "../oplus_charger.h"
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"

#define VBAT_GAP_STATUS1	800
#define VBAT_GAP_STATUS2	600
#define VBAT_GAP_STATUS3	150
#define VBAT_GAP_STATUS7	400
#define MAIN_STATUS_CHG		1
#define SUB_STATUS_CHG		2
#define ALL_STATUS_CHG		3
#define HYSTERISIS_DECIDEGC	20
#define HYSTERISIS_DECIDEGC_0C	5
#define RATIO_ACC		100
#define OUT_OF_BALANCE_COUNT	10

struct oplus_switch_chip * g_switching_chip;
extern struct oplus_chg_chip* oplus_chg_get_chg_struct(void);

int oplus_switching_get_error_status(void)
{
	if (!g_switching_chip || g_switching_chip->switch_ops ||
	    g_switching_chip->switch_ops->switching_get_fastcharge_current) {
		return 0;
	}

	if (g_switching_chip->error_status) {
		chg_err("error_status:%d\n", g_switching_chip->error_status);
		return g_switching_chip->error_status;
	} else {
		g_switching_chip->switch_ops->switching_get_fastcharge_current();
		chg_err("error_status:%d\n", g_switching_chip->error_status);
		return g_switching_chip->error_status;
	}

	return 0;
}

int oplus_switching_hw_enable(int en)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops ||
	    !g_switching_chip->switch_ops->switching_hw_enable) {
		return -1;
	}

	return g_switching_chip->switch_ops->switching_hw_enable(en);
}

int oplus_switching_set_fastcharge_current(int curr_ma)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops ||
	    !g_switching_chip->switch_ops->switching_set_fastcharge_current) {
		return -1;
	}

	return g_switching_chip->switch_ops->switching_set_fastcharge_current(curr_ma);

}

int oplus_switching_enable_charge(int en)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops ||
	    !g_switching_chip->switch_ops->switching_enable_charge) {
		return -1;
	} else {
		chg_err("success\n");
		return g_switching_chip->switch_ops->switching_enable_charge(en);
	}
}

bool oplus_switching_get_hw_enable(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_hw_enable) {
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_hw_enable();
	}
}

bool oplus_switching_get_charge_enable(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_charge_enable) {
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_charge_enable();
	}
}

int oplus_switching_get_fastcharge_current(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_fastcharge_current) {
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_fastcharge_current();
	}
}

int oplus_switching_get_discharge_current(void)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_get_discharge_current) {
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_get_discharge_current();
	}
}

int oplus_switching_set_current(int current_ma)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_set_fastcharge_current) {
		return -1;
	}

	chg_err("current_ma:%d\n", current_ma);
	g_switching_chip->switch_ops->switching_set_fastcharge_current(current_ma);

	return 0;
}

int oplus_switching_set_discharge_current(int current_ma)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops
		|| !g_switching_chip->switch_ops->switching_set_discharge_current) {
		return -1;
	}

	chg_err("current_ma:%d\n", current_ma);
	g_switching_chip->switch_ops->switching_set_discharge_current(current_ma);

	return 0;
}

int oplus_switching_set_discharge_mode(int mode)
{
	if (!g_switching_chip || !g_switching_chip->switch_ops ||
		!g_switching_chip->switch_ops->switching_set_discharge_mode) {
		chg_err("fail\n");
		return -1;
	} else {
		return g_switching_chip->switch_ops->switching_set_discharge_mode(mode);
	}
}

int oplus_switching_get_if_need_balance_bat(int vbat0_mv, int vbat1_mv)
{
	int diff_volt = 0;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	static int error_count = 0;
	static int fg_error_count = 0;
	static int pre_error_reason = 0;
	int error_reason = 0;

	chg_err("vbat0_mv:%d, vbat1_mv:%d\n", vbat0_mv, vbat1_mv);
	if (!atomic_read(&chip->mos_lock)) {
		chg_err("mos test start, check next time!");
		return 0;
	}

	if (!g_switching_chip) {
		chg_err("fail\n");
		return -1;
	} else {
		diff_volt = abs(vbat0_mv - vbat1_mv);
		if (chip->sub_batt_temperature == FG_I2C_ERROR || chip->main_batt_temperature == FG_I2C_ERROR) {
			if (fg_error_count <= BATT_FGI2C_RETRY_COUNT)
				fg_error_count++;
			else
				error_reason |= REASON_I2C_ERROR;
		} else {
			fg_error_count = 0;
		}

		if (oplus_switching_get_error_status()
		    && oplus_switching_support_parallel_chg() == PARALLEL_SWITCH_IC) {
			return PARALLEL_BAT_BALANCE_ERROR_STATUS8;
		}

		if (oplus_vooc_get_fastchg_started() == true && atomic_read(&chip->mos_lock)) {
			if (oplus_switching_get_hw_enable() &&
			    (abs(chip->sub_batt_icharging) < g_switching_chip->parallel_mos_abnormal_litter_curr ||
			    abs(chip->icharging) < g_switching_chip->parallel_mos_abnormal_litter_curr) &&
			    (abs(chip->icharging - chip->sub_batt_icharging) >= g_switching_chip->parallel_mos_abnormal_gap_curr)) {
				if (error_count < BATT_OPEN_RETRY_COUNT) {
					error_count++;
				} else {
					error_reason |= REASON_MOS_OPEN_ERROR;
				}
			} else {
				error_count = 0;
			}
		}

		if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL && atomic_read(&chip->mos_lock)) {
			if (chip->tbatt_status != BATTERY_STATUS__WARM_TEMP
			    && (chip->sw_sub_batt_full || chip->hw_sub_batt_full_by_sw)
			    && !(chip->sw_full || chip->hw_full_by_sw)
			    && chip->charger_exist
			    && diff_volt < g_switching_chip->parallel_vbat_gap_full
			    && chip->mmi_chg) {
		    		error_reason |= REASON_SUB_BATT_FULL;
			}
			if (diff_volt >= g_switching_chip->parallel_vbat_gap_abnormal) {
				error_reason |= REASON_VBAT_GAP_BIG;
			}
			if ((pre_error_reason & REASON_SUB_BATT_FULL)
			    && !oplus_switching_get_hw_enable()
			    && (diff_volt > g_switching_chip->parallel_vbat_gap_full
			    || !chip->mmi_chg)) {
				error_reason &= ~REASON_SUB_BATT_FULL;
				chg_err("sub full,but diff_volt > %d need to recovery MOS\n", g_switching_chip->parallel_vbat_gap_full);
			}
			if ((pre_error_reason & REASON_VBAT_GAP_BIG)
			    && diff_volt < g_switching_chip->parallel_vbat_gap_recov) {
				error_reason &= ~REASON_VBAT_GAP_BIG;
			}
		}

		if (g_switching_chip->debug_force_mos_err)
			oplus_chg_track_parallel_mos_error(g_switching_chip->debug_force_mos_err);

		if (error_reason != 0) {
			if (pre_error_reason != error_reason
			    && (error_reason & REASON_VBAT_GAP_BIG
				|| error_reason & REASON_I2C_ERROR
				|| error_reason & REASON_MOS_OPEN_ERROR))
				oplus_chg_track_parallel_mos_error(error_reason);
			pre_error_reason = error_reason;
			chip->parallel_error_flag &= ~(REASON_I2C_ERROR | REASON_MOS_OPEN_ERROR
						       | REASON_SUB_BATT_FULL | REASON_VBAT_GAP_BIG);
			chip->parallel_error_flag |= error_reason;
			chg_err("mos open %d\n", error_reason);
			if ((error_reason & (REASON_I2C_ERROR | REASON_MOS_OPEN_ERROR)) != 0) {
				return PARALLEL_BAT_BALANCE_ERROR_STATUS8;
			}
			return PARALLEL_BAT_BALANCE_ERROR_STATUS9;
		} else if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL) {
			pre_error_reason = error_reason;
			chip->parallel_error_flag &= ~(REASON_I2C_ERROR | REASON_MOS_OPEN_ERROR
						       | REASON_SUB_BATT_FULL | REASON_VBAT_GAP_BIG);
			chip->parallel_error_flag |= error_reason;
			return PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
		}

		if (diff_volt < VBAT_GAP_STATUS3) {
			return PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
		}

		if (vbat0_mv >= 3400 && vbat1_mv < 3400) {
			if (vbat1_mv < 3100) {
				if (vbat0_mv - vbat1_mv <= 1000) {
					return PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE;
				} else {
					return PARALLEL_BAT_BALANCE_ERROR_STATUS6__STOP_CHARGE;
				}
			} else {
				return PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE;
			}
		} else if (vbat0_mv >= vbat1_mv) {
			if (diff_volt >= VBAT_GAP_STATUS1) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS1__STOP_CHARGE;
			} else if (diff_volt >= VBAT_GAP_STATUS2) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS2__STOP_CHARGE;
			} else if (diff_volt >= VBAT_GAP_STATUS3) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS3__STOP_CHARGE;
			} else {
				return PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
			}
		} else if (vbat0_mv < vbat1_mv) {
			if (diff_volt >= VBAT_GAP_STATUS7) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE;
			} else if (diff_volt >= VBAT_GAP_STATUS3) {
				return PARALLEL_NEED_BALANCE_BAT_STATUS4__STOP_CHARGE;
			} else {
				return PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE;
			}
		}
	}
	return -1;
}

int oplus_switching_set_balance_bat_status(int status)
{
	chg_err("status:%d\n", status);
	switch (status) {
	case PARALLEL_NOT_NEED_BALANCE_BAT__START_CHARGE:
		oplus_switching_hw_enable(1);
		oplus_switching_set_discharge_current(2800);
		oplus_switching_set_discharge_mode(NO_REGULATION_FULLY_ON);
		oplus_switching_set_current(2800);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS1__STOP_CHARGE:
		oplus_switching_hw_enable(1);
		oplus_switching_set_discharge_current(500);
		oplus_switching_set_discharge_mode(CURRENT_REGULATION);
		oplus_switching_set_current(500);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS2__STOP_CHARGE:
		oplus_switching_hw_enable(1);
		oplus_switching_set_discharge_current(1000);
		oplus_switching_set_discharge_mode(CURRENT_REGULATION);
		oplus_switching_set_current(1000);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS3__STOP_CHARGE:
		oplus_switching_hw_enable(1);
		oplus_switching_set_discharge_current(2800);
		oplus_switching_set_discharge_mode(NO_REGULATION_FULLY_ON);
		oplus_switching_set_current(2800);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS4__STOP_CHARGE:
		oplus_switching_hw_enable(1);
		oplus_switching_set_discharge_current(2800);
		oplus_switching_set_discharge_mode(NO_REGULATION_FULLY_ON);
		oplus_switching_set_current(2800);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS5__STOP_CHARGE:
		oplus_switching_hw_enable(1);
		oplus_switching_set_discharge_current(200);
		oplus_switching_set_discharge_mode(CURRENT_REGULATION);
		oplus_switching_set_current(200);
		oplus_switching_enable_charge(1);
		break;
	case PARALLEL_BAT_BALANCE_ERROR_STATUS6__STOP_CHARGE:
		oplus_switching_hw_enable(0);
		break;
	case PARALLEL_NEED_BALANCE_BAT_STATUS7__START_CHARGE:
		oplus_switching_hw_enable(0);
		break;
	case PARALLEL_BAT_BALANCE_ERROR_STATUS8:
	case PARALLEL_BAT_BALANCE_ERROR_STATUS9:
		oplus_switching_hw_enable(0);
		break;
	default:
		break;
	}
	return 0;
}

static const char * const batt_temp_table[] = {
	[BATTERY_STATUS__COLD_TEMP]		= "cold_temp",
	[BATTERY_STATUS__LITTLE_COLD_TEMP]	= "little_cold_temp",
	[BATTERY_STATUS__COOL_TEMP]		= "cool_temp",
	[BATTERY_STATUS__LITTLE_COOL_TEMP]	= "little_cool_temp",
	[BATTERY_STATUS__NORMAL] 		= "normal_temp",
	[BATTERY_STATUS__WARM_TEMP] 		= "warm_temp",
	[BATTERY_STATUS__REMOVED] 		= "NA",
	[BATTERY_STATUS__LOW_TEMP] 		= "NA",
	[BATTERY_STATUS__HIGH_TEMP] 		= "NA",
};

static int oplus_switching_parse_dt(struct oplus_switch_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;
	struct device_node *temp_node = NULL;
	int i;
	int j;
	int length = 0;

	if (!chip || !chip->dev) {
		chg_err("oplus_mos_dev null!\n");
		return -1;
	}

	node = chip->dev->of_node;

	rc = of_property_read_u32(node, "qcom,parallel_vbat_gap_abnormal", &chip->parallel_vbat_gap_abnormal);
	if (rc) {
		chip->parallel_vbat_gap_abnormal = 150;
	}

	rc = of_property_read_u32(node, "qcom,parallel_vbat_gap_full", &chip->parallel_vbat_gap_full);
	if (rc) {
		chip->parallel_vbat_gap_full = 200;
	}

	rc = of_property_read_u32(node, "qcom,parallel_vbat_gap_recov", &chip->parallel_vbat_gap_recov);
	if (rc) {
		chip->parallel_vbat_gap_recov = 100;
	}

	rc = of_property_read_u32(node, "qcom,parallel_mos_abnormal_litter_curr", &chip->parallel_mos_abnormal_litter_curr);
	if (rc) {
		chip->parallel_mos_abnormal_litter_curr = 100;
	}

	rc = of_property_read_u32(node, "qcom,parallel_mos_abnormal_gap_curr", &chip->parallel_mos_abnormal_gap_curr);
	if (rc) {
		chip->parallel_mos_abnormal_gap_curr = 2000;
	}
	chg_err("parallel_vbat_gap_abnormal %d,"
		"parallel_vbat_gap_full %d,"
		"parallel_vbat_gap_recov %d,"
		"parallel_mos_abnormal_litter_curr %d,"
		"parallel_mos_abnormal_gap_curr %d \n",
		chip->parallel_vbat_gap_abnormal, chip->parallel_vbat_gap_full,
		chip->parallel_vbat_gap_recov, chip->parallel_mos_abnormal_litter_curr,
		chip->parallel_mos_abnormal_gap_curr);

	chip->normal_chg_check = of_property_read_bool(node, "normal_chg_check_support");

	rc = of_property_read_u32(node, "track_unbalance_high", &chip->track_unbalance_high);
	if (rc) {
		chip->track_unbalance_high = 100;
	}
	rc = of_property_read_u32(node, "track_unbalance_low", &chip->track_unbalance_low);
	if (rc) {
		chip->track_unbalance_low = 0;
	}
	rc = of_property_read_u32(node, "track_unbalance_soc", &chip->track_unbalance_soc);
	if (rc) {
		chip->track_unbalance_soc = 100;
	}
	temp_node = of_get_child_by_name(node, "parallel_bat_table");
	chip->parallel_bat_data = devm_kzalloc(chip->dev, BATTERY_STATUS__INVALID * sizeof(struct parallel_bat_table), GFP_KERNEL);
	if (temp_node && chip->parallel_bat_data) {
		for (i = 0; i < BATTERY_STATUS__INVALID; i++) {
			rc = of_property_count_elems_of_size(temp_node, batt_temp_table[i], sizeof(u32));
			if (rc > 0 && rc % (sizeof(struct batt_spec)/sizeof(int)) == 0) {
				length = rc;
				chip->parallel_bat_data[i].length = length / (sizeof(struct batt_spec) / sizeof(int));
				chip->parallel_bat_data[i].batt_table = devm_kzalloc(chip->dev, length * sizeof(struct batt_spec), GFP_KERNEL);
				if (chip->parallel_bat_data[i].batt_table) {
					rc = of_property_read_u32_array(temp_node, batt_temp_table[i],
									(u32 *)chip->parallel_bat_data[i].batt_table,
									length);
					if (rc < 0) {
						chg_err("parse bat_table failed, rc=%d\n", rc);
						chip->parallel_bat_data[i].length = 0;
						devm_kfree(chip->dev, chip->parallel_bat_data[i].batt_table);
					} else {
						chg_err("%s length =%d\n",
							batt_temp_table[i], chip->parallel_bat_data[i].length);
						for (j = 0; j < chip->parallel_bat_data[i].length; j++) {
							chg_err("vbatt: %d main_curr: %d sub_curr:%d \n",
								chip->parallel_bat_data[i].batt_table[j].volt,
								chip->parallel_bat_data[i].batt_table[j].main_curr,
								chip->parallel_bat_data[i].batt_table[j].sub_curr);
						}
					}
				}
			}
		}
	}
	chip->parallel_mos_debug = of_property_read_bool(node, "track,parallel_mos_debug");

	return 0;
}

void oplus_chg_parellel_variables_reset(void)
{
	struct oplus_switch_chip *chip = g_switching_chip;

	if (!chip) {
		chg_err("chip not ready\n");
		return;
	}

	chip->pre_spec_index = -1;
	chip->pre_sub_spec_index = -1;
}

static void track_mos_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_switch_chip *chip =
		container_of(
			dwork, struct oplus_switch_chip,
			parallel_mos_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->mos_err_load_trigger));
	if (chip->mos_err_load_trigger) {
		kfree(chip->mos_err_load_trigger);
		chip->mos_err_load_trigger = NULL;
	}
	chip->mos_err_uploading = false;
}

static int mos_match_err_value(int reason)
{
	int err_type = TRACK_MOS_ERR_DEFAULT;

	if (reason == REASON_SOC_NOT_FULL)
		err_type = TRACK_MOS_SOC_NOT_FULL;
	else if (reason == REASON_CURRENT_UNBALANCE)
		err_type = TRACK_MOS_CURRENT_UNBALANCE;
	else if (reason == REASON_SOC_GAP_TOO_BIG)
		err_type = TRACK_MOS_SOC_GAP_TOO_BIG;
	else if (reason == REASON_RECORD_SOC)
		err_type = TRACK_MOS_RECORD_SOC;

	return err_type;
}

int oplus_chg_track_parallel_mos_error(int reason)
{
	int index = 0;
	char err_reason[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN] = {0};
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	int err_type;

	if (!g_switching_chip || !chip) {
		chg_err("oplus_switch_chip not specified!");
		return -EINVAL;
	}
	if (reason == REASON_RECORD_SOC
	    && !g_switching_chip->parallel_mos_debug) {
		chg_err("no config debug, return");
		return 0;
	}

	if (!reason && !g_switching_chip->debug_force_mos_err) {
		chg_err("reason and debug is null, return");
		return 0;
	}
	mutex_lock(&g_switching_chip->track_mos_err_lock);
	if (g_switching_chip->mos_err_uploading) {
		chg_err("mos_err_uploading, should return\n");
		mutex_unlock(&g_switching_chip->track_mos_err_lock);
		return 0;
	}

	if (g_switching_chip->mos_err_load_trigger)
		kfree(g_switching_chip->mos_err_load_trigger);
	g_switching_chip->mos_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!g_switching_chip->mos_err_load_trigger) {
		chg_err("mos_err_load_trigger memery alloc fail\n");
		mutex_unlock(&g_switching_chip->track_mos_err_lock);
		return -ENOMEM;
	}
	g_switching_chip->mos_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	g_switching_chip->mos_err_uploading = true;
	mutex_unlock(&g_switching_chip->track_mos_err_lock);

	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
			  "mos");
	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
			  OPLUS_CHG_TRACK_SCENE_MOS_ERR);

	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$error_reason@@");
	switch (reason) {
	case REASON_SOC_NOT_FULL:
	case REASON_CURRENT_UNBALANCE:
	case REASON_SOC_GAP_TOO_BIG:
	case REASON_RECORD_SOC:
		err_type = mos_match_err_value(reason);
		g_switching_chip->mos_err_load_trigger->flag_reason =
			TRACK_NOTIFY_FLAG_PARALLEL_UNBALANCE_ABNORMAL;
		memset(err_reason, 0, sizeof(err_reason));
		oplus_chg_track_get_mos_err_reason(err_type, err_reason,
						   sizeof(err_reason));
		index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "%s", err_reason);
		break;
	default:
		g_switching_chip->mos_err_load_trigger->flag_reason =
			TRACK_NOTIFY_FLAG_MOS_ERROR_ABNORMAL;
		break;
	}

	if (reason & REASON_I2C_ERROR) {
		memset(err_reason, 0, sizeof(err_reason));
		oplus_chg_track_get_mos_err_reason(TRACK_MOS_I2C_ERROR, err_reason,
						   sizeof(err_reason));
		index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "%s,", err_reason);
	}
	if (reason & REASON_MOS_OPEN_ERROR) {
		memset(err_reason, 0, sizeof(err_reason));
		oplus_chg_track_get_mos_err_reason(TRACK_MOS_OPEN_ERROR, err_reason,
						   sizeof(err_reason));
		index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "%s,", err_reason);
	}
	if (reason & REASON_SUB_BATT_FULL) {
		memset(err_reason, 0, sizeof(err_reason));
		oplus_chg_track_get_mos_err_reason(TRACK_MOS_SUB_BATT_FULL, err_reason,
						   sizeof(err_reason));
		index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "%s,", err_reason);
	}
	if (reason & REASON_VBAT_GAP_BIG) {
		memset(err_reason, 0, sizeof(err_reason));
		oplus_chg_track_get_mos_err_reason(TRACK_MOS_VBAT_GAP_BIG, err_reason,
						   sizeof(err_reason));
		index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
				  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				  "%s", err_reason);
	}

	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$mos_err_status@@"
			  "main_sub_soc %d %d, ",
			  chip->main_batt_soc, chip->sub_batt_soc);
	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "main_sub_volt %d %d, ",
			  chip->batt_volt, chip->sub_batt_volt);
	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "main_sub_curr %d %d, ",
			  chip->icharging, chip->sub_batt_icharging);
	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "main_sub_temp %d %d, ",
			  chip->main_batt_temperature, chip->sub_batt_temperature);
	index += snprintf(&(g_switching_chip->mos_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "mos %s, ",
			  oplus_switching_get_hw_enable() ? "on" : "off");
	schedule_delayed_work(&g_switching_chip->parallel_mos_trigger_work, 0);
	chg_err("upload parallel_charging_unbalance\n");

	return 0;
}

static int mos_track_debugfs_init(struct oplus_switch_chip *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_mos;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_mos = debugfs_create_dir("mos", debugfs_root);
	if (!debugfs_mos) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_mos_err = TRACK_MOS_ERR_DEFAULT;
	debugfs_create_u32("debug_force_mos_err", 0644,
	    debugfs_mos, &(chip->debug_force_mos_err));

	return ret;
}

static int mos_track_init(struct oplus_switch_chip *chip)
{
	int rc;

	if (!chip)
		return - EINVAL;

	mutex_init(&chip->track_mos_err_lock);
	chip->mos_err_uploading = false;
	chip->mos_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->parallel_mos_trigger_work,
			  track_mos_err_load_trigger_work);

	rc = mos_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("mos debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

void oplus_switching_init(struct oplus_switch_chip *chip, int type)
{
	if (!chip) {
		chg_err("oplus_switch_chip not specified!\n");
		return;
	}

	g_switching_chip = chip;
	g_switching_chip->ctrl_type = type;

	chg_err("ctrl_type: %d\n", g_switching_chip->ctrl_type);

	oplus_switching_parse_dt(chip);

	oplus_chg_parellel_variables_reset();
	mos_track_init(chip);
}

int oplus_switching_support_parallel_chg(void)
{
	if (!g_switching_chip) {
		return NO_PARALLEL_TYPE;
        } else {
		return g_switching_chip->ctrl_type;
	}
}

bool oplus_support_normal_batt_spec_check(void)
{
	if (!g_switching_chip) {
		return false;
        } else {
		return g_switching_chip->normal_chg_check;
	}
}

void oplus_init_parallel_temp_threshold(void)
{
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!chip || !g_switching_chip) {
		chg_err("oplus_chg_chip not specified!\n");
		return;
	}

	g_switching_chip->main_hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	g_switching_chip->main_warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
	g_switching_chip->main_normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
	g_switching_chip->main_little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
	g_switching_chip->main_cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
	g_switching_chip->main_little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
	g_switching_chip->main_cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
	g_switching_chip->main_removed_bat_decidegc = chip->limits.removed_bat_decidegc;

	g_switching_chip->sub_hot_bat_decidegc = chip->anti_shake_bound.hot_bound;
	g_switching_chip->sub_warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
	g_switching_chip->sub_normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
	g_switching_chip->sub_little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
	g_switching_chip->sub_cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
	g_switching_chip->sub_little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
	g_switching_chip->sub_cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
	g_switching_chip->sub_removed_bat_decidegc = chip->limits.removed_bat_decidegc;
}

static void parallel_battery_anti_shake_handle(int status_change, int main_temp, int sub_temp)
{
	struct oplus_switch_chip *chip = g_switching_chip;
	struct oplus_chg_chip *chg_chip = oplus_chg_get_chg_struct();
	int tbatt_cur_shake, low_shake, high_shake;
	int low_shake_0c, high_shake_0c;

	if (status_change == MAIN_STATUS_CHG || status_change == ALL_STATUS_CHG) {
		tbatt_cur_shake = main_temp;
		if (tbatt_cur_shake > chip->main_pre_shake) {			/* get warmer */
			low_shake = -HYSTERISIS_DECIDEGC;
			high_shake = 0;
			low_shake_0c = -HYSTERISIS_DECIDEGC_0C;
			high_shake_0c = 0;
		} else if (tbatt_cur_shake < chip->main_pre_shake) {	/* get cooler */
			low_shake = 0;
			high_shake = HYSTERISIS_DECIDEGC;
			low_shake_0c = 0;
			high_shake_0c = HYSTERISIS_DECIDEGC_0C;
		}
		if (chip->main_tbatt_status == BATTERY_STATUS__HIGH_TEMP) {								/* >53C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound + low_shake;
		} else if (chip->main_tbatt_status == BATTERY_STATUS__LOW_TEMP) {							/* <-10C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound + high_shake;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->main_tbatt_status == BATTERY_STATUS__COLD_TEMP) {							/* -10C~0C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound + high_shake_0c;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->main_tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {						/* 0C-5C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound + low_shake_0c;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound + high_shake;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->main_tbatt_status == BATTERY_STATUS__COOL_TEMP) {							/* 5C~12C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound + low_shake;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound + high_shake;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->main_tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {						/* 12C~16C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound + low_shake;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound + high_shake;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->main_tbatt_status == BATTERY_STATUS__NORMAL) {								/* 16C~45C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound + low_shake;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound + high_shake;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->main_tbatt_status == BATTERY_STATUS__WARM_TEMP) {							/* 45C~53C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound + low_shake;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound + high_shake;
		} else {														/* <-19C */
			chip->main_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->main_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->main_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->main_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->main_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->main_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->main_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		}
		chg_err("MAIN_BAT [%d-%d-%d-%d-%d-%d-%d] t=[%d %d] s=%d\n",
			chip->main_cold_bat_decidegc,
			chip->main_little_cold_bat_decidegc,
			chip->main_cool_bat_decidegc,
			chip->main_little_cool_bat_decidegc,
			chip->main_normal_bat_decidegc,
			chip->main_warm_bat_decidegc,
			chip->main_hot_bat_decidegc,
			chip->main_pre_shake,
			tbatt_cur_shake,
			chip->main_tbatt_status);
		chip->main_pre_shake = tbatt_cur_shake;
	}

	if (status_change == SUB_STATUS_CHG || status_change == ALL_STATUS_CHG) {
		tbatt_cur_shake = sub_temp;
		if (tbatt_cur_shake > chip->sub_pre_shake) {			/* get warmer */
			low_shake = -HYSTERISIS_DECIDEGC;
			high_shake = 0;
			low_shake_0c = -HYSTERISIS_DECIDEGC_0C;
			high_shake_0c = 0;
		} else if (tbatt_cur_shake < chip->sub_pre_shake) {	/* get cooler */
			low_shake = 0;
			high_shake = HYSTERISIS_DECIDEGC;
			low_shake_0c = 0;
			high_shake_0c = HYSTERISIS_DECIDEGC_0C;
		}
		if (chip->sub_tbatt_status == BATTERY_STATUS__HIGH_TEMP) {								/* >53C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound + low_shake;
		} else if (chip->sub_tbatt_status == BATTERY_STATUS__LOW_TEMP) {							/* <-10C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound + high_shake;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->sub_tbatt_status == BATTERY_STATUS__COLD_TEMP) {							/* -10C~0C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound + high_shake_0c;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->sub_tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {						/* 0C-5C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound + low_shake_0c;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound + high_shake;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->sub_tbatt_status == BATTERY_STATUS__COOL_TEMP) {							/* 5C~12C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound + low_shake;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound + high_shake;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->sub_tbatt_status == BATTERY_STATUS__LITTLE_COOL_TEMP) {						/* 12C~16C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound + low_shake;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound + high_shake;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->sub_tbatt_status == BATTERY_STATUS__NORMAL) {								/* 16C~45C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound + low_shake;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound + high_shake;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		} else if (chip->sub_tbatt_status == BATTERY_STATUS__WARM_TEMP) {							/* 45C~53C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound + low_shake;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound + high_shake;
		} else {														/* <-19C */
			chip->sub_cold_bat_decidegc = chg_chip->anti_shake_bound.cold_bound;
			chip->sub_little_cold_bat_decidegc = chg_chip->anti_shake_bound.little_cold_bound;
			chip->sub_cool_bat_decidegc = chg_chip->anti_shake_bound.cool_bound;
			chip->sub_little_cool_bat_decidegc = chg_chip->anti_shake_bound.little_cool_bound;
			chip->sub_normal_bat_decidegc = chg_chip->anti_shake_bound.normal_bound;
			chip->sub_warm_bat_decidegc = chg_chip->anti_shake_bound.warm_bound;
			chip->sub_hot_bat_decidegc = chg_chip->anti_shake_bound.hot_bound;
		}
		chg_err("SUB_BAT [%d-%d-%d-%d-%d-%d-%d] t=[%d %d] s=%d\n",
			chip->sub_cold_bat_decidegc,
			chip->sub_little_cold_bat_decidegc,
			chip->sub_cool_bat_decidegc,
			chip->sub_little_cool_bat_decidegc,
			chip->sub_normal_bat_decidegc,
			chip->sub_warm_bat_decidegc,
			chip->sub_hot_bat_decidegc,
			chip->sub_pre_shake,
			tbatt_cur_shake,
			chip->sub_tbatt_status);
		chip->sub_pre_shake = tbatt_cur_shake;
	}
}

static void oplus_chg_check_parallel_tbatt_status(int main_temp, int sub_temp, int *main_status, int *sub_status)
{
	struct oplus_switch_chip *chip = g_switching_chip;
	int status_change = 0;

	if (main_temp > chip->main_hot_bat_decidegc) {
		*main_status = BATTERY_STATUS__HIGH_TEMP;
	} else if (main_temp >= chip->main_warm_bat_decidegc) {
		*main_status = BATTERY_STATUS__WARM_TEMP;
	} else if (main_temp >= chip->main_normal_bat_decidegc) {
		*main_status = BATTERY_STATUS__NORMAL;
	} else if (main_temp >= chip->main_little_cool_bat_decidegc) {
		*main_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
	} else if (main_temp >= chip->main_cool_bat_decidegc) {
		*main_status = BATTERY_STATUS__COOL_TEMP;
	} else if (main_temp >= chip->main_little_cold_bat_decidegc) {
		*main_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
	} else if (main_temp >= chip->main_cold_bat_decidegc) {
		*main_status = BATTERY_STATUS__COLD_TEMP;
	} else if (main_temp > chip->main_removed_bat_decidegc) {
		*main_status = BATTERY_STATUS__LOW_TEMP;
	} else {
		*main_status = BATTERY_STATUS__REMOVED;
	}

	if (sub_temp > chip->sub_hot_bat_decidegc) {
		*sub_status = BATTERY_STATUS__HIGH_TEMP;
	} else if (sub_temp >= chip->sub_warm_bat_decidegc) {
		*sub_status = BATTERY_STATUS__WARM_TEMP;
	} else if (sub_temp >= chip->sub_normal_bat_decidegc) {
		*sub_status = BATTERY_STATUS__NORMAL;
	} else if (sub_temp >= chip->sub_little_cool_bat_decidegc) {
		*sub_status = BATTERY_STATUS__LITTLE_COOL_TEMP;
	} else if (sub_temp >= chip->sub_cool_bat_decidegc) {
		*sub_status = BATTERY_STATUS__COOL_TEMP;
	} else if (sub_temp >= chip->sub_little_cold_bat_decidegc) {
		*sub_status = BATTERY_STATUS__LITTLE_COLD_TEMP;
	} else if (sub_temp >= chip->sub_cold_bat_decidegc) {
		*sub_status = BATTERY_STATUS__COLD_TEMP;
	} else if (sub_temp > chip->sub_removed_bat_decidegc) {
		*sub_status = BATTERY_STATUS__LOW_TEMP;
	} else {
		*sub_status = BATTERY_STATUS__REMOVED;
	}

	if (chip->main_tbatt_status != *main_status && chip->sub_tbatt_status != *sub_status)
		status_change = ALL_STATUS_CHG;
	else if (chip->main_tbatt_status != *main_status)
		status_change = MAIN_STATUS_CHG;
	else if (chip->sub_tbatt_status != *sub_status)
		status_change = SUB_STATUS_CHG;

	chip->main_tbatt_status = *main_status;
	chip->sub_tbatt_status = *sub_status;
	if (status_change)
		parallel_battery_anti_shake_handle(status_change, main_temp, sub_temp);
}

int oplus_chg_is_parellel_ibat_over_spec(int main_temp, int sub_temp, int *target_curr)
{
	struct oplus_switch_chip *chip = g_switching_chip;
	struct oplus_chg_chip *charger_chip = oplus_chg_get_chg_struct();
	int main_volt;
	int sub_volt;
	int main_curr_now;
	int sub_curr_now;
	int i;
	int curr_radio = 0;
	int main_curr_radio = 0;
	int sub_curr_radio = 0;
	int curr_need_change = 0;
	int main_curr_need_change = 0;
	int sub_curr_need_change = 0;
	int index_main = 0;
	int index_sub = 0;
	int target_main_curr = 0;
	int target_sub_curr = 0;
	int main_tstatus = 0;
	int sub_tstatus = 0;
	static bool init_temp_thr = false;
	static unsigned int unbalance_count;
	static int pre_main_tstatus = 0;
	static int pre_sub_tstatus = 0;

	if (!chip) {
		chg_err("chip not ready\n");
		return 0;
	}

	if (!init_temp_thr) {
		oplus_init_parallel_temp_threshold();
		init_temp_thr = true;
	}

	oplus_chg_check_parallel_tbatt_status(main_temp, sub_temp, &main_tstatus, &sub_tstatus);
	if (main_tstatus != pre_main_tstatus) {
		pre_main_tstatus = main_tstatus;
		chip->pre_spec_index = -1;
	}
	if (sub_tstatus != pre_sub_tstatus) {
		pre_sub_tstatus = sub_tstatus;
		chip->pre_sub_spec_index = -1;
	}

	if (!batt_temp_table[main_tstatus]
	    || !batt_temp_table[sub_tstatus]
	    || !chip->parallel_bat_data
	    || !chip->parallel_bat_data[main_tstatus].batt_table
	    || !chip->parallel_bat_data[sub_tstatus].batt_table) {
		chg_err("parallel batt spec not fond, limit to min curr\n");
		return -1;
	}

	main_volt = oplus_gauge_get_batt_mvolts();
	main_curr_now = -oplus_gauge_get_batt_current();
	sub_volt = oplus_gauge_get_sub_batt_mvolts();
	sub_curr_now = -oplus_gauge_get_sub_batt_current();
	if ((main_curr_now <= 0 || sub_curr_now <= 0)
	     && oplus_switching_get_hw_enable()) {
		chg_err("current is negative %d %d\n", main_curr_now, sub_curr_now);
		return 0;
	}

	if (!oplus_switching_get_hw_enable()) {
		curr_radio = RATIO_ACC;
		main_curr_radio = RATIO_ACC;
		sub_curr_radio = 0;
	} else {
		if (main_curr_now + sub_curr_now != 0)
			curr_radio = (main_curr_now * RATIO_ACC) / (main_curr_now + sub_curr_now);
		if (main_curr_now > 0)
			main_curr_radio = (main_curr_now + sub_curr_now) * RATIO_ACC / main_curr_now;
		if (sub_curr_now > 0)
			sub_curr_radio = (main_curr_now + sub_curr_now) * RATIO_ACC / sub_curr_now;
		if (oplus_vooc_get_fastchg_started()
		    && (curr_radio > chip->track_unbalance_high
		     || curr_radio < chip->track_unbalance_low)) {
			if (unbalance_count < OUT_OF_BALANCE_COUNT)
				unbalance_count++;
			if (unbalance_count == OUT_OF_BALANCE_COUNT) {
				unbalance_count = OUT_OF_BALANCE_COUNT + 1;
				oplus_chg_track_parallel_mos_error(REASON_CURRENT_UNBALANCE);
				charger_chip->parallel_error_flag |= REASON_CURRENT_UNBALANCE;
			}
		} else if (unbalance_count != OUT_OF_BALANCE_COUNT + 1) {
			unbalance_count = 0;
		}
	}

	for (i = 0; i < chip->parallel_bat_data[main_tstatus].length; i++) {
		if (main_volt > chip->parallel_bat_data[main_tstatus].batt_table[i].volt)
			continue;

		if (main_curr_now > chip->parallel_bat_data[main_tstatus].batt_table[i].main_curr) {
			main_curr_need_change = MAIN_BATT_OVER_CURR;
		}
		index_main = i;
		break;
	}

	for (i = 0; i < chip->parallel_bat_data[sub_tstatus].length; i++) {
		if (sub_volt > chip->parallel_bat_data[sub_tstatus].batt_table[i].volt)
			continue;

		if (sub_curr_now > chip->parallel_bat_data[sub_tstatus].batt_table[i].sub_curr) {
			sub_curr_need_change = SUB_BATT_OVER_CURR;
		}
		index_sub = i;
		break;
	}
	chg_err("pre_spec_index: %d index_main: %d pre_sub_spec_index: %d index_sub: %d\n",
		chip->pre_spec_index, index_main, chip->pre_sub_spec_index, index_sub);

	if (chip->pre_spec_index != -1 && chip->pre_spec_index > index_main)
		index_main = chip->pre_spec_index;
	if (chip->pre_sub_spec_index != -1 && chip->pre_sub_spec_index > index_sub)
		index_sub = chip->pre_sub_spec_index;

	target_main_curr = chip->parallel_bat_data[main_tstatus].batt_table[index_main].main_curr;
	target_main_curr = target_main_curr * main_curr_radio / RATIO_ACC;

	target_sub_curr = chip->parallel_bat_data[sub_tstatus].batt_table[index_sub].sub_curr;
	target_sub_curr = target_sub_curr * sub_curr_radio / RATIO_ACC;

	if (main_curr_need_change == MAIN_BATT_OVER_CURR && sub_curr_need_change == SUB_BATT_OVER_CURR)
		curr_need_change = (target_main_curr > target_sub_curr) ? SUB_BATT_OVER_CURR : MAIN_BATT_OVER_CURR;
	else if (main_curr_need_change == MAIN_BATT_OVER_CURR)
		curr_need_change = MAIN_BATT_OVER_CURR;
	else if (sub_curr_need_change == SUB_BATT_OVER_CURR)
		curr_need_change = SUB_BATT_OVER_CURR;

	if (!oplus_switching_get_hw_enable())
		*target_curr = target_main_curr;
	else
		*target_curr = (target_main_curr > target_sub_curr) ? target_sub_curr : target_main_curr;

	if (*target_curr < 0)
		*target_curr = 0;

	if (curr_need_change)
		chg_err("%s battery over current", (curr_need_change == MAIN_BATT_OVER_CURR) ? "main" : "sub");

	chip->pre_spec_index = index_main;
	chip->pre_sub_spec_index = index_sub;

	chg_err("main_volt:%d main_curr_now:%d sub_volt: %d sub_curr_now: %d "
		  "main_temp: %d sub_temp: %d main_tstatus: %s sub_tstatus: %s "
		  "curr_radio: %d target_main_curr: %d target_sub_curr: %d "
		  "target_curr: %d curr_need_change: %d\n",
		  main_volt, main_curr_now, sub_volt, sub_curr_now,
		  main_temp, sub_temp, batt_temp_table[main_tstatus], batt_temp_table[sub_tstatus],
		  curr_radio, target_main_curr, target_sub_curr,
		  *target_curr, curr_need_change);

	return curr_need_change;
}

bool oplus_chg_check_is_soc_gap_big(int main_soc, int sub_soc)
{
	struct oplus_chg_chip *charger_chip = oplus_chg_get_chg_struct();
	struct oplus_switch_chip *chip = g_switching_chip;
	static bool track_soc_gap_big;

	if (!chip || !charger_chip) {
		chg_err("chip not ready");
		return false;
	}

	if (abs(main_soc - sub_soc) > chip->track_unbalance_soc
	    && !track_soc_gap_big) {
		track_soc_gap_big = true;
		oplus_chg_track_parallel_mos_error(REASON_SOC_GAP_TOO_BIG);
		charger_chip->parallel_error_flag |= REASON_SOC_GAP_TOO_BIG;
		chg_err("soc gap too big: main %d, sub %d, gap %d",
			main_soc, sub_soc, chip->track_unbalance_soc);
		return true;
	} else if (abs(main_soc - sub_soc) < chip->track_unbalance_soc) {
		track_soc_gap_big = false;
		charger_chip->parallel_error_flag &= ~REASON_SOC_GAP_TOO_BIG;
	}

	return false;
}

