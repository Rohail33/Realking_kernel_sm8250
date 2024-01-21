// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
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
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/rtc.h>
#include <linux/gpio.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>

#ifdef CONFIG_OPLUS_CHARGER_MTK
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
#include <uapi/linux/sched/types.h>
#endif
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/usb/typec.h>
#include <linux/usb/usbpd.h>
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
#include <linux/spmi.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/soc/qcom/smem.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/qpnp/qpnp-adc.h>
#else
#include <uapi/linux/sched/types.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
#include <linux/batterydata-lib.h>
#include <linux/of_batterydata.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/msm_bcl.h>
#endif
#endif

#include "oplus_charger.h"
#include "oplus_ufcs.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_adapter.h"
#include "voocphy/oplus_voocphy.h"

static struct oplus_ufcs_chip *g_ufcs_chip = NULL;
static struct pm_qos_request ufcs_pm_qos_req;
static u8 ufcs_auth_msg[UFCS_AUTH_MSG_LEN] = { 0 };

static void oplus_ufcs_stop_work(struct work_struct *work);
static void oplus_ufcs_update_work(struct work_struct *work);
static int oplus_ufcs_psy_changed(struct oplus_ufcs_chip *chip);
static void oplus_ufcs_r_init(struct oplus_ufcs_chip *chip);
static void oplus_ufcs_count_init(struct oplus_ufcs_chip *chip);
static bool oplus_ufcs_check_oplus_id(void);
extern int ppm_sys_boost_min_cpu_freq_set(int freq_min, int freq_mid,
					  int freq_max,
					  unsigned int clear_time);
extern int ppm_sys_boost_min_cpu_freq_clear(void);
extern bool oplus_is_power_off_charging(struct oplus_vooc_chip *chip);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static void oplus_ufcs_pm_qos_update(int new_value)
{
	static int last_value = 0;
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	if (!pm_qos_request_active(&ufcs_pm_qos_req))
		pm_qos_add_request(&ufcs_pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
				   new_value);
	else
		pm_qos_update_request(&ufcs_pm_qos_req, new_value);

	if (last_value != new_value) {
		last_value = new_value;

		if (new_value == PM_QOS_DEFAULT_VALUE)
			ufcs_err("oplus_ufcs_pm_qos_update "
				 "PM_QOS_DEFAULT_VALUE \n");
		else
			ufcs_err("oplus_ufcs_pm_qos_update value =%d \n",
				 new_value);
	}
}
#else
static void oplus_ufcs_pm_qos_update(int new_value)
{
	static int last_value = 0;
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	if (!cpu_latency_qos_request_active(&ufcs_pm_qos_req))
		cpu_latency_qos_add_request(&ufcs_pm_qos_req, new_value);
	else
		cpu_latency_qos_update_request(&ufcs_pm_qos_req, new_value);

	if (last_value != new_value) {
		last_value = new_value;

		if (new_value == PM_QOS_DEFAULT_VALUE)
			ufcs_err(" PM_QOS_DEFAULT_VALUE \n");
		else
			ufcs_err(" value =%d \n", new_value);
	}
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static void cpuboost_ufcs_event(int flag)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return;
	}
	if (flag == CPU_CHG_FREQ_STAT_UP) {
		ufcs_err("!!\n");
		atomic_set(&chip->ufcs_freq_state, 1);
		ppm_sys_boost_min_cpu_freq_set(chip->ufcs_freq_mincore,
						   chip->ufcs_freq_midcore,
						   chip->ufcs_freq_maxcore, 15000);
	} else {
		ufcs_err(" RESET_DELAY_30S!!\n");
		atomic_set(&chip->ufcs_freq_state, 0);
		ppm_sys_boost_min_cpu_freq_clear();
	}
#else
	struct cpufreq_policy *policy;
	int ret;
	static bool initialized = false;
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	static struct freq_qos_request ufcs_freq_qos_req;

	if (!chip || !chip->ufcs_support_type)
		return;

	policy = cpufreq_cpu_get(0);
	if (!policy) {
		ufcs_err(": cpufreq policy not found for cpu0\n");
		return;
	}

	if (!initialized) {
		ret = freq_qos_add_request(&policy->constraints,
					   &ufcs_freq_qos_req, FREQ_QOS_MIN,
					   policy->cpuinfo.min_freq);
		if (ret < 0) {
			ufcs_err(":  freq_qos_add_request failed!!! ret=%d \n",
				 ret);
			return;
		} else {
			initialized = true;
		}
	}

	if (flag == CPU_CHG_FREQ_STAT_UP) {
		ret = freq_qos_update_request(&ufcs_freq_qos_req,
					      chip->ufcs_cpu_up_freq);
	} else {
		ret = freq_qos_update_request(&ufcs_freq_qos_req,
					      policy->cpuinfo.min_freq);
	}
	ufcs_err(" set cpufreq boost value=%d ret=%d\n", flag, ret);
#endif
}
#else
static void cpuboost_ufcs_event(int flag)
{
	return;
}
#endif

static void ufcs_cpufreq_init(void)
{
	cpuboost_ufcs_event(CPU_CHG_FREQ_STAT_UP);
}

static void ufcs_cpufreq_release(void)
{
	cpuboost_ufcs_event(CPU_CHG_FREQ_STAT_AUTO);
}

static const char * const ufcs_strategy_soc[] = {
	[UFCS_BATT_CURVE_SOC_RANGE_MIN]		= "strategy_soc_range_min",
	[UFCS_BATT_CURVE_SOC_RANGE_LOW]		= "strategy_soc_range_low",
	[UFCS_BATT_CURVE_SOC_RANGE_MID_LOW]	= "strategy_soc_range_mid_low",
	[UFCS_BATT_CURVE_SOC_RANGE_MID]		= "strategy_soc_range_mid",
	[UFCS_BATT_CURVE_SOC_RANGE_MID_HIGH]	= "strategy_soc_range_mid_high",
	[UFCS_BATT_CURVE_SOC_RANGE_HIGH]		= "strategy_soc_range_high",
};

static const char * const ufcs_strategy_temp[] = {
	[UFCS_BATT_CURVE_TEMP_RANGE_LITTLE_COLD]	= "strategy_temp_little_cold",
	[UFCS_BATT_CURVE_TEMP_RANGE_COOL]		= "strategy_temp_cool",
	[UFCS_BATT_CURVE_TEMP_RANGE_LITTLE_COOL]	= "strategy_temp_little_cool",
	[UFCS_BATT_CURVE_TEMP_RANGE_NORMAL_LOW]	= "strategy_temp_normal_low",
	[UFCS_BATT_CURVE_TEMP_RANGE_NORMAL_HIGH]	= "strategy_temp_normal_high",
	[UFCS_BATT_CURVE_TEMP_RANGE_WARM]		= "strategy_temp_warm",
};

static const char * const strategy_low_curr_full[] = {
	[UFCS_LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL]	= "strategy_temp_little_cool",
	[UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW]	= "strategy_temp_normal_low",
	[UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH]	= "strategy_temp_normal_high",
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static struct timespec current_kernel_time(void)
{
	struct timespec ts;
	getnstimeofday(&ts);
	return ts;
}
#endif

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int ufcs_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	ufcs_debug("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int oplus_chg_track_pack_ufcs_stats(struct oplus_ufcs_chip *chip,
					   u8 *curx, int *index)
{
	if (!chip || !curx || !index)
		return -EINVAL;

	*index += snprintf(
		&(curx[*index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - *index,
		"$$ufcs_msg@@status[%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d] curve[%d, %d, %d, %d] [%d, %d, %d, %d, %d] \
		data1[%d, %d, %d, %d] [%d, %d, %d, %d] data2[%d, %d, %d, %d] [%d, %d, %d, %d, %d] pdo[%d, %d, %d, %d] R[%d, %d, %d, %d, %d]",
		chip->ufcs_support_type, chip->ufcs_fastchg_type,
		chip->ufcs_authentication, chip->ufcs_ping, chip->ufcs_chging,
		chip->ufcs_status, chip->ufcs_stop_status,
		chip->ufcs_dummy_started, chip->ufcs_fastchg_started,
		chip->ufcs_ping, chip->cp_mode, oplus_ufcs_check_oplus_id(),
		chip->ufcs_power, chip->ufcs_imax, chip->ufcs_vmax,

		chip->batt_curve_index, chip->current_batt_curve,
		chip->current_batt_temp, chip->ufcs_fastchg_batt_temp_status,
		chip->ufcs_temp_cur_range, chip->current_bcc,
		chip->current_cool_down, chip->cp_ibus_down, chip->cp_r_down,

		chip->ap_batt_volt, chip->ap_batt_current, chip->ap_batt_soc,
		chip->ap_batt_temperature, chip->ap_fg_temperature,
		chip->charger_output_volt, chip->charger_output_current,
		chip->current_adapter_max,

		chip->ap_input_volt, chip->ap_input_current,
		chip->cp_master_ibus, chip->cp_master_vac, chip->cp_master_vout,
		chip->slave_input_volt, chip->cp_slave_ibus, chip->cp_slave_vac,
		chip->cp_slave_vout,

		chip->target_charger_volt, chip->target_charger_current,
		chip->ask_charger_volt, chip->ask_charger_current,
		chip->r_avg.r0, chip->r_avg.r1, chip->r_avg.r2, chip->r_avg.r3,
		chip->r_avg.r4);
	return 0;
}

static int oplus_ufcs_track_upload_err_info(struct oplus_ufcs_chip *chip,
					    int err_type, int value)
{
	int index = 0;
	int curr_time;
	char power_info[OPLUS_CHG_TRACK_CURX_INFO_LEN] = { 0 };
	char err_reason[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN] = { 0 };
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	curr_time = ufcs_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_UPLOAD_COUNT_MAX)
		return 0;

	if (chip->debug_force_ufcs_err)
		err_type = chip->debug_force_ufcs_err;

	mutex_lock(&chip->track_ufcs_err_lock);
	if (chip->ufcs_err_uploading) {
		ufcs_debug("ufcs_err_uploading, should return\n");
		mutex_unlock(&chip->track_ufcs_err_lock);
		return 0;
	}

	if (chip->ufcs_err_load_trigger)
		kfree(chip->ufcs_err_load_trigger);
	chip->ufcs_err_load_trigger =
		kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->ufcs_err_load_trigger) {
		ufcs_debug("ufcs_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_ufcs_err_lock);
		return -ENOMEM;
	}
	chip->ufcs_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_SOFTWARE_ABNORMAL;
	chip->ufcs_err_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_UFCS_ABNORMAL;
	chip->ufcs_err_uploading = true;
	upload_count++;
	pre_upload_time = ufcs_track_get_local_time_s();
	mutex_unlock(&chip->track_ufcs_err_lock);

	index += snprintf(&(chip->ufcs_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_UFCS_ERR);

	oplus_chg_track_get_ufcs_err_reason(err_type, err_reason,
					    sizeof(err_reason));
	index += snprintf(&(chip->ufcs_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s$$value@@%d", err_reason, value);

	oplus_chg_track_pack_ufcs_stats(
		chip, chip->ufcs_err_load_trigger->crux_info, &index);
	oplus_chg_track_obtain_power_info(power_info, sizeof(power_info));
	index += snprintf(&(chip->ufcs_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  power_info);
	memset(power_info, 0, sizeof(power_info));
	oplus_chg_track_obtain_general_info(power_info, strlen(power_info),
					    sizeof(power_info));
	index += snprintf(&(chip->ufcs_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  power_info);
	schedule_delayed_work(&chip->ufcs_err_load_trigger_work, 0);
	ufcs_debug("success\n");

	return 0;
}

static void ufcs_track_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_ufcs_chip *chip = container_of(
		dwork, struct oplus_ufcs_chip, ufcs_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->ufcs_err_load_trigger));
	if (chip->ufcs_err_load_trigger) {
		kfree(chip->ufcs_err_load_trigger);
		chip->ufcs_err_load_trigger = NULL;
	}
	chip->ufcs_err_uploading = false;
}

static int ufcs_track_debugfs_init(struct oplus_ufcs_chip *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_ufcs;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_ufcs = debugfs_create_dir("ufcs", debugfs_root);
	if (!debugfs_ufcs) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_ufcs_err = 0;
	debugfs_create_u32("debug_force_ufcs_err", 0644, debugfs_ufcs,
			   &(chip->debug_force_ufcs_err));

	return ret;
}

static int ufcs_track_init(struct oplus_ufcs_chip *chip)
{
	int rc;

	if (!chip)
		return -EINVAL;

	mutex_init(&chip->track_ufcs_err_lock);
	chip->ufcs_err_uploading = false;
	chip->ufcs_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->ufcs_err_load_trigger_work,
			  ufcs_track_err_load_trigger_work);
	rc = ufcs_track_debugfs_init(chip);
	if (rc < 0) {
		ufcs_debug("ufcs debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static void oplus_ufcs_error_log(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int ret = 0;

	if (!chip || !chip->ops || !chip->ops->ufcs_get_error) {
		return;
	}

	ret = chip->ops->ufcs_get_error(chip->ufcs_error);

	if (!chip->ufcs_error || ret < 0) {
		ufcs_err("ufcs_error is null\n");
		return;
	}
	ufcs_err("[0x%x, 0x%x, 0x%x, 0x%x]\n", chip->ufcs_error->i2c_error,
		 chip->ufcs_error->rcv_hardreset,
		 chip->ufcs_error->hardware_error,
		 chip->ufcs_error->power_rdy_timeout);
}

static int oplus_ufcs_ufcs_exit(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int ret = 0;

	if (!chip || !chip->ufcs_support_type || !chip->ops->ufcs_exit ||
	    !chip->ops->ufcs_exit) {
		return 0;
	}
	ret = chip->ops->ufcs_exit();
	if (ret < 0) {
		ufcs_err(", ret = %d\n", ret);
		oplus_ufcs_error_log();
	}
	ret = chip->ops->ufcs_chip_disable();
	if (ret < 0) {
		oplus_ufcs_error_log();
	}

	return 0;
}

static int oplus_ufcs_delay_exit(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type || !chip->ufcs_exit_ms ||
	    oplus_chg_get_boot_completed() || oplus_is_power_off_charging(NULL)) {
		return 0;
	}
	msleep(chip->ufcs_exit_ms);
	chip->ufcs_exit_ms = 0;

	return 0;
}

static int oplus_ufcs_parse_charge_strategy(struct oplus_ufcs_chip *chip)
{
	int rc;
	int length = 0;
	int soc_tmp[7] = { 0, 15, 30, 50, 75, 85, 95 };
	int rang_temp_tmp[7] = { 0, 50, 120, 200, 350, 440, 510 };
	int high_temp_tmp[6] = { 425, 430, 435, 400, 415, 420 };
	int high_curr_tmp[6] = { 4000, 3000, 2000, 3000, 4000, 4000 };
	int low_curr_temp_tmp[4] = { 120, 200, 300, 440 };

	struct device_node *node;

	node = chip->dev->of_node;
	rc = of_property_read_u32(node, "oplus,ufcs_support_type",
				  &chip->ufcs_support_type);
	if (rc || chip->ufcs_support_type == 0) {
		chip->ufcs_support_type = 0;
		return -ENODEV;
	} else {
		ufcs_err("oplus,ufcs_support_type is %d\n",
			 chip->ufcs_support_type);
	}
	rc = of_property_read_u32(node, "oplus,ufcs_cpu_up_freq",
				  &chip->ufcs_cpu_up_freq);
	if (rc) {
		chip->ufcs_cpu_up_freq = 1000000;
	} else {
		ufcs_err("oplus,ufcs_cpu_up_freq is %d\n",
			 chip->ufcs_cpu_up_freq);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_warm_allow_vol",
				  &chip->limits.ufcs_warm_allow_vol);
	if (rc) {
		chip->limits.ufcs_warm_allow_vol = 4000;
	} else {
		ufcs_err("oplus,ufcs_warm_allow_vol is %d\n",
			 chip->limits.ufcs_warm_allow_vol);
	}
	rc = of_property_read_u32(node, "oplus,ufcs_warm_allow_soc",
				  &chip->limits.ufcs_warm_allow_soc);
	if (rc) {
		chip->limits.ufcs_warm_allow_soc = 50;
	} else {
		ufcs_err("oplus,ufcs_warm_allow_soc is %d\n",
			 chip->limits.ufcs_warm_allow_soc);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_strategy_normal_current",
				  &chip->limits.ufcs_strategy_normal_current);
	if (rc) {
		chip->limits.ufcs_strategy_normal_current = 6000;
	} else {
		ufcs_err("oplus,ufcs_strategy_normal_current is %d\n",
			 chip->limits.ufcs_strategy_normal_current);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_over_high_or_low_current",
				  &chip->limits.ufcs_over_high_or_low_current);
	if (rc) {
		chip->limits.ufcs_over_high_or_low_current = -EINVAL;
	} else {
		ufcs_err("oplus,ufcs_over_high_or_low_current is %d\n",
			 chip->limits.ufcs_over_high_or_low_current);
	}
	rc = of_property_read_u32(node, "oplus,ufcs_full_cool_sw_vbat",
				  &chip->limits.ufcs_full_cool_sw_vbat);
	if (rc) {
		chip->limits.ufcs_full_cool_sw_vbat = 4430;
	} else {
		ufcs_err("oplus,ufcs_full_cool_sw_vbat is %d\n",
			 chip->limits.ufcs_full_cool_sw_vbat);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_full_normal_sw_vbat",
				  &chip->limits.ufcs_full_normal_sw_vbat);
	if (rc) {
		chip->limits.ufcs_full_normal_sw_vbat = 4495;
	} else {
		ufcs_err("oplus,ufcs_full_normal_sw_vbat %d\n",
			 chip->limits.ufcs_full_normal_sw_vbat);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_full_normal_hw_vbat",
				  &chip->limits.ufcs_full_normal_hw_vbat);
	if (rc) {
		chip->limits.ufcs_full_normal_hw_vbat = 4500;
	} else {
		ufcs_err("oplus,ufcs_full_normal_hw_vbat is %d\n",
			 chip->limits.ufcs_full_normal_hw_vbat);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_full_cool_sw_vbat_third",
				  &chip->limits.ufcs_full_cool_sw_vbat_third);
	if (rc) {
		chip->limits.ufcs_full_cool_sw_vbat_third = 4430;
	} else {
		ufcs_err("oplus,ufcs_full_cool_sw_vbat_third is %d\n",
			 chip->limits.ufcs_full_cool_sw_vbat_third);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_full_normal_sw_vbat_third",
				  &chip->limits.ufcs_full_normal_sw_vbat_third);
	if (rc) {
		chip->limits.ufcs_full_normal_sw_vbat_third = 4430;
	} else {
		ufcs_err("oplus,ufcs_full_normal_sw_vbat_third is %d\n",
			 chip->limits.ufcs_full_normal_sw_vbat_third);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_full_normal_hw_vbat_third",
				  &chip->limits.ufcs_full_normal_hw_vbat_third);
	if (rc) {
		chip->limits.ufcs_full_normal_hw_vbat_third = 4440;
	} else {
		ufcs_err("oplus,ufcs_full_normal_hw_vbat_third is %d\n",
			 chip->limits.ufcs_full_normal_hw_vbat_third);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_full_ffc_vbat",
				  &chip->limits.ufcs_full_ffc_vbat);
	if (rc) {
		chip->limits.ufcs_full_ffc_vbat = 4420;
	} else {
		ufcs_err("oplus,ufcs_full_ffc_vbat is %d\n",
			 chip->limits.ufcs_full_ffc_vbat);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_full_warm_vbat",
				  &chip->limits.ufcs_full_warm_vbat);
	if (rc) {
		chip->limits.ufcs_full_warm_vbat = 4150;
	} else {
		ufcs_err("oplus,ufcs_full_warm_vbat is %d\n",
			 chip->limits.ufcs_full_warm_vbat);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_timeout_third",
				  &chip->limits.ufcs_timeout_third);
	if (rc) {
		chip->limits.ufcs_timeout_third = 9000;
	} else {
		ufcs_err("oplus,ufcs_timeout_third is %d\n",
			 chip->limits.ufcs_timeout_third);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_timeout_oplus",
				  &chip->limits.ufcs_timeout_oplus);
	if (rc) {
		chip->limits.ufcs_timeout_oplus = 7200;
	} else {
		ufcs_err("oplus,ufcs_timeout_oplus is %d\n",
			 chip->limits.ufcs_timeout_oplus);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_ibat_over_third",
				  &chip->limits.ufcs_ibat_over_third);
	if (rc) {
		chip->limits.ufcs_ibat_over_third = 4000;
	} else {
		ufcs_err("oplus,ufcs_ibat_over_third is %d\n",
			 chip->limits.ufcs_ibat_over_third);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_ibat_over_oplus",
				  &chip->limits.ufcs_ibat_over_oplus);
	if (rc) {
		chip->limits.ufcs_ibat_over_oplus = 17000;
	} else {
		ufcs_err("oplus,ufcs_ibat_over_oplus is %d\n",
			 chip->limits.ufcs_ibat_over_oplus);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_freq_mincore",
				  &chip->ufcs_freq_mincore);
	if (rc) {
		chip->ufcs_freq_mincore = 925000;
	}
	ufcs_err("ufcs_freq_mincore is %d\n", chip->ufcs_freq_mincore);

	rc = of_property_read_u32(node, "oplus,ufcs_freq_midcore",
				  &chip->ufcs_freq_midcore);
	if (rc) {
		chip->ufcs_freq_midcore = 925000;
	}
	ufcs_err("ufcs_freq_midcore is %d\n", chip->ufcs_freq_midcore);

	rc = of_property_read_u32(node, "oplus,ufcs_freq_maxcore",
				  &chip->ufcs_freq_maxcore);
	if (rc) {
		chip->ufcs_freq_maxcore = 925000;
	}
	ufcs_err("ufcs_freq_maxcore is %d\n", chip->ufcs_freq_maxcore);
	rc = of_property_read_u32(node, "oplus,ufcs_current_change_timeout",
				  &chip->ufcs_current_change_timeout);
	if (rc) {
		chip->ufcs_current_change_timeout = 100;
	}
	ufcs_err("ufcs_current_change_timeout is %d\n",
		 chip->ufcs_current_change_timeout);

	rc = of_property_read_u32(node, "oplus,ufcs_exit_ms",
					  &chip->ufcs_exit_ms);
	if (rc) {
		chip->ufcs_exit_ms = 0;
	}
	ufcs_err("ufcs_exit_ms is %d\n", chip->ufcs_exit_ms);

	rc = of_property_read_u32(node, "oplus,ufcs_bcc_max_curr",
				  &chip->bcc_max_curr);
	if (rc) {
		chip->bcc_max_curr = 15000;
	} else {
		ufcs_err("oplus,ufcs_bcc_max_curr is %d\n", chip->bcc_max_curr);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_bcc_min_curr",
				  &chip->bcc_min_curr);
	if (rc) {
		chip->bcc_min_curr = 1000;
	} else {
		ufcs_err("oplus,ufcs_bcc_min_curr is %d\n", chip->bcc_min_curr);
	}

	rc = of_property_read_u32(node, "oplus,ufcs_bcc_exit_curr",
				  &chip->bcc_exit_curr);
	if (rc) {
		chip->bcc_exit_curr = 1000;
	} else {
		ufcs_err("oplus,ufcs_bcc_exit_curr is %d\n",
			 chip->bcc_exit_curr);
	}

	rc = of_property_count_elems_of_size(
		node, "oplus,ufcs_strategy_batt_high_temp", sizeof(u32));
	if (rc >= 0) {
		length = rc;
		if (length > 6)
			length = 6;
		rc = of_property_read_u32_array(node, "oplus,ufcs_strategy_"
						      "batt_high_temp",
						(u32 *)high_temp_tmp, length);
		if (rc < 0) {
			ufcs_err("parse ufcs_strategy_batt_high_temp failed, "
				 "rc=%d\n",
				 rc);
		}
	} else {
		length = 6;
		ufcs_err("parse "
			 "ufcs_strategy_batt_high_temp,of_property_count_elems_"
			 "of_size rc=%d\n",
			 rc);
	}

	chip->limits.ufcs_strategy_batt_high_temp0 = high_temp_tmp[0];
	chip->limits.ufcs_strategy_batt_high_temp1 = high_temp_tmp[1];
	chip->limits.ufcs_strategy_batt_high_temp2 = high_temp_tmp[2];
	chip->limits.ufcs_strategy_batt_low_temp0 = high_temp_tmp[3];
	chip->limits.ufcs_strategy_batt_low_temp1 = high_temp_tmp[4];
	chip->limits.ufcs_strategy_batt_low_temp2 = high_temp_tmp[5];
	ufcs_err(", length = %d, high_temp[%d, %d, %d, %d, %d, %d]\n", length,
		 high_temp_tmp[0], high_temp_tmp[1], high_temp_tmp[2],
		 high_temp_tmp[3], high_temp_tmp[4], high_temp_tmp[5]);

	rc = of_property_count_elems_of_size(
		node, "oplus,ufcs_strategy_high_current", sizeof(u32));
	if (rc >= 0) {
		length = rc;
		if (length > 6)
			length = 6;
		rc = of_property_read_u32_array(node, "oplus,ufcs_strategy_"
						      "high_current",
						(u32 *)high_curr_tmp, length);
		if (rc < 0) {
			ufcs_err("parse ufcs_strategy_high_current failed, "
				 "rc=%d\n",
				 rc);
		}
	} else {
		length = 6;
		ufcs_err("parse "
			 "ufcs_strategy_high_current,of_property_count_elems_"
			 "of_size rc=%d\n",
			 rc);
	}
	chip->limits.ufcs_strategy_high_current0 = high_curr_tmp[0];
	chip->limits.ufcs_strategy_high_current1 = high_curr_tmp[1];
	chip->limits.ufcs_strategy_high_current2 = high_curr_tmp[2];
	chip->limits.ufcs_strategy_low_current0 = high_curr_tmp[3];
	chip->limits.ufcs_strategy_low_current1 = high_curr_tmp[4];
	chip->limits.ufcs_strategy_low_current2 = high_curr_tmp[5];
	ufcs_err(",length = %d, high_current[%d, %d, %d, %d, %d, %d]\n", length,
		 high_curr_tmp[0], high_curr_tmp[1], high_curr_tmp[2],
		 high_curr_tmp[3], high_curr_tmp[4], high_curr_tmp[5]);

	rc = of_property_count_elems_of_size(
		node, "oplus,ufcs_charge_strategy_temp", sizeof(u32));
	if (rc >= 0) {
		chip->limits.ufcs_strategy_temp_num = rc;
		if (chip->limits.ufcs_strategy_temp_num > 7)
			chip->limits.ufcs_strategy_temp_num = 7;
		rc = of_property_read_u32_array(
			node, "oplus,ufcs_charge_strategy_temp",
			(u32 *)rang_temp_tmp,
			chip->limits.ufcs_strategy_temp_num);
		if (rc < 0) {
			ufcs_err("parse ufcs_charge_strategy_temp failed, "
				 "rc=%d\n",
				 rc);
		}
	} else {
		chip->limits.ufcs_strategy_temp_num = 7;
		ufcs_err("parse "
			 "ufcs_strategy_temp_num,of_property_count_elems_of_"
			 "size rc=%d\n",
			 rc);
	}
	chip->limits.ufcs_batt_over_low_temp = rang_temp_tmp[0];
	chip->limits.ufcs_little_cold_temp = rang_temp_tmp[1];
	chip->limits.ufcs_cool_temp = rang_temp_tmp[2];
	chip->limits.ufcs_little_cool_temp = rang_temp_tmp[3];
	chip->limits.ufcs_normal_low_temp = rang_temp_tmp[4];
	chip->limits.ufcs_normal_high_temp = rang_temp_tmp[5];
	chip->limits.ufcs_batt_over_high_temp = rang_temp_tmp[6];
	ufcs_err(",ufcs_charge_strategy_temp num = %d, [%d, %d, %d, %d, %d, "
		 "%d, %d]\n",
		 chip->limits.ufcs_strategy_temp_num, rang_temp_tmp[0],
		 rang_temp_tmp[1], rang_temp_tmp[2], rang_temp_tmp[3],
		 rang_temp_tmp[4], rang_temp_tmp[5], rang_temp_tmp[6]);
	chip->limits.default_ufcs_normal_high_temp =
		chip->limits.ufcs_normal_high_temp;
	chip->limits.default_ufcs_normal_low_temp =
		chip->limits.ufcs_normal_low_temp;
	chip->limits.default_ufcs_little_cool_temp =
		chip->limits.ufcs_little_cool_temp;
	chip->limits.default_ufcs_cool_temp = chip->limits.ufcs_cool_temp;
	chip->limits.default_ufcs_little_cold_temp =
		chip->limits.ufcs_little_cold_temp;

	rc = of_property_count_elems_of_size(
		node, "oplus,ufcs_charge_strategy_soc", sizeof(u32));
	if (rc >= 0) {
		chip->limits.ufcs_strategy_soc_num = rc;
		if (chip->limits.ufcs_strategy_soc_num > 7)
			chip->limits.ufcs_strategy_soc_num = 7;
		rc = of_property_read_u32_array(
			node, "oplus,ufcs_charge_strategy_soc", (u32 *)soc_tmp,
			chip->limits.ufcs_strategy_soc_num);
		if (rc < 0) {
			ufcs_err("parse ufcs_charge_strategy_soc failed, "
				 "rc=%d\n",
				 rc);
		}
	} else {
		chip->limits.ufcs_strategy_soc_num = 7;
		ufcs_err("parse "
			 "ufcs_charge_strategy_soc,of_property_count_elems_of_"
			 "size rc=%d\n",
			 rc);
	}

	chip->limits.ufcs_strategy_soc_over_low = soc_tmp[0];
	chip->limits.ufcs_strategy_soc_min = soc_tmp[1];
	chip->limits.ufcs_strategy_soc_low = soc_tmp[2];
	chip->limits.ufcs_strategy_soc_mid_low = soc_tmp[3];
	chip->limits.ufcs_strategy_soc_mid = soc_tmp[4];
	chip->limits.ufcs_strategy_soc_mid_high = soc_tmp[5];
	chip->limits.ufcs_strategy_soc_high = soc_tmp[6];
	ufcs_err(",ufcs_charge_strategy_soc num = %d, [%d, %d, %d, %d, %d, %d, "
		 "%d]\n",
		 chip->limits.ufcs_strategy_soc_num, soc_tmp[0], soc_tmp[1],
		 soc_tmp[2], soc_tmp[3], soc_tmp[4], soc_tmp[5], soc_tmp[6]);

	rc = of_property_count_elems_of_size(
		node, "oplus,ufcs_low_curr_full_strategy_temp", sizeof(u32));

	if (rc >= 0) {
		length = rc;
		if (length > 4)
			length = 4;
		rc = of_property_read_u32_array(
			node, "oplus,ufcs_low_curr_full_strategy_temp",
			(u32 *)low_curr_temp_tmp, length);
		if (rc < 0) {
			ufcs_err("parse ufcs_low_curr_full_strategy_temp "
				 "failed, rc=%d\n",
				 rc);
		}
	} else {
		length = 4;
		ufcs_err("parse "
			 "ufcs_low_curr_full_strategy_temp,of_property_count_"
			 "elems_of_size rc=%d\n",
			 rc);
	}

	chip->limits.ufcs_low_curr_full_cool_temp = low_curr_temp_tmp[0];
	chip->limits.ufcs_low_curr_full_little_cool_temp = low_curr_temp_tmp[1];
	chip->limits.ufcs_low_curr_full_normal_low_temp = low_curr_temp_tmp[2];
	chip->limits.ufcs_low_curr_full_normal_high_temp = low_curr_temp_tmp[3];
	ufcs_err(",length = %d, low_curr_temp[%d, %d, %d, %d]\n", length,
		 low_curr_temp_tmp[0], low_curr_temp_tmp[1],
		 low_curr_temp_tmp[2], low_curr_temp_tmp[3]);

	return rc;
}

static int oplus_ufcs_parse_resistense_strategy(struct oplus_ufcs_chip *chip)
{
	int rc;
	int length = UFCS_R_ROW_NUM;
	int r_tmp[UFCS_R_ROW_NUM] = { 85, 10, 10, 15, 15, 15, 15 };
	struct oplus_ufcs_r_limit limit_tmp = { 280, 200, 140, 90, 50 };

	struct device_node *node;
	if (!chip || !chip->ufcs_support_type) {
		return -ENODEV;
	}
	node = chip->dev->of_node;
	rc = of_property_read_u32(node, "oplus,ufcs_rmos_mohm",
				  &chip->rmos_mohm);
	if (rc < 0) {
		ufcs_err("Could't find oplus,ufcs_rmos_mohm failed, rc=%d\n",
			 rc);
		chip->rmos_mohm = 8;
	}

	rc = of_property_count_elems_of_size(node, "oplus,ufcs_r_limit",
					     sizeof(u32));
	if (rc < 0) {
		ufcs_err("Could't find oplus,ufcs_r_limit failed, rc=%d\n", rc);
		chip->r_limit = limit_tmp;
	} else {
		length = rc;
		if (length > 5)
			length = 5;
		rc = of_property_read_u32_array(node, "oplus,ufcs_r_limit",
						(u32 *)&limit_tmp, length);
		if (rc < 0) {
			ufcs_err("parse ufcs_r_limit failed, rc=%d\n", rc);
		}
		chip->r_limit = limit_tmp;
	}
	ufcs_err("parse rmos_mohm = %d, r_limit[%d %d %d %d %d]\n",
		 chip->rmos_mohm, chip->r_limit.limit_exit_mohm,
		 chip->r_limit.limit_1a_mohm, chip->r_limit.limit_2a_mohm,
		 chip->r_limit.limit_3a_mohm, chip->r_limit.limit_4a_mohm);
	rc = of_property_count_elems_of_size(node, "oplus,ufcs_r_default",
					     sizeof(u32));
	if (rc < 0) {
		ufcs_err("Could't find oplus,ufcs_r_default failed, rc=%d\n",
			 rc);
	}
	length = rc;
	if (length > 7)
		length = 7;
	ufcs_err("parse ufcs_r_default, length=%d rc =%d\n", length, rc);
	rc = of_property_read_u32_array(node, "oplus,ufcs_r_default",
					(u32 *)r_tmp, length);
	if (rc < 0) {
		ufcs_err("parse ufcs_r_default failed, rc=%d\n", rc);
	}
	chip->r_default.r0 = r_tmp[0];
	chip->r_default.r1 = -r_tmp[1];
	chip->r_default.r2 = -r_tmp[2];
	chip->r_default.r3 = -r_tmp[3];
	chip->r_default.r4 = -r_tmp[4];
	chip->r_default.r5 = -r_tmp[5];
	chip->r_default.r6 = -r_tmp[6];

	memcpy(&chip->r_avg, &chip->r_default, sizeof(struct oplus_ufcs_r_info));

	ufcs_err("chip->r_avg[%d %d %d %d %d %d %d]\n", chip->r_avg.r0,
		 chip->r_avg.r1, chip->r_avg.r2, chip->r_avg.r3, chip->r_avg.r4,
		 chip->r_avg.r5, chip->r_avg.r6);
	ufcs_err("chip->r_default[%d %d %d %d %d %d %d]\n", chip->r_default.r0,
		 chip->r_default.r1, chip->r_default.r2, chip->r_default.r3,
		 chip->r_default.r4, chip->r_default.r5, chip->r_default.r6);

	return rc;
}

static int oplus_ufcs_parse_batt_curves_third(struct oplus_ufcs_chip *chip)
{
	struct device_node *node, *ufcs_node, *soc_node;
	int rc = 0, i, j, k, length;

	if (!chip || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	node = chip->dev->of_node;

	ufcs_node = of_get_child_by_name(node, "ufcs_charge_third_strategy");

	for (i = 0; i < chip->limits.ufcs_strategy_soc_num - 1; i++) {
		soc_node =
			of_get_child_by_name(ufcs_node, ufcs_strategy_soc[i]);
		if (!soc_node) {
			ufcs_err("Can not find third %s node\n",
				 ufcs_strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < chip->limits.ufcs_strategy_temp_num - 1; j++) {
			rc = of_property_count_elems_of_size(
				soc_node, ufcs_strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				ufcs_err("Count third %s failed, rc=%d\n",
					 ufcs_strategy_temp[j], rc);
				return rc;
			}
			length = rc;

			switch (i) {
			case UFCS_BATT_CURVE_SOC_RANGE_MIN:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_third_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_third_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_LOW:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_third_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_third_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_MID_LOW:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_third_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_third_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_MID:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_third_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_third_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_MID_HIGH:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_third_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_third_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_HIGH:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_third_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_third_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;

			default:
				break;
			}
		}
	}

	for (i = 0; i < chip->limits.ufcs_strategy_soc_num - 1; i++) {
		for (j = 0; j < chip->limits.ufcs_strategy_temp_num - 1; j++) {
			for (k = 0; k < chip->batt_curves_third_soc[i]
						.batt_curves_temp[j]
						.batt_curve_num;
			     k++) {
				ufcs_debug("third: i=%d,j=%d %d %d %d %d %d\n",
					   i, j, chip->batt_curves_third_soc[i]
							 .batt_curves_temp[j]
							 .batt_curves[k]
							 .target_vbus,
					   chip->batt_curves_third_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .target_vbat,
					   chip->batt_curves_third_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .target_ibus,
					   chip->batt_curves_third_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .exit,
					   chip->batt_curves_third_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .target_time);
			}
		}
	}

	return rc;
}

static int oplus_ufcs_parse_batt_curves_oplus(struct oplus_ufcs_chip *chip)
{
	struct device_node *node, *ufcs_node, *soc_node;
	int rc = 0, i, j, k, length;

	if (!chip || !chip->ufcs_support_type) {
		return -ENODEV;
	}
	node = chip->dev->of_node;

	ufcs_node = of_get_child_by_name(node, "ufcs_charge_oplus_strategy");
	if (!ufcs_node) {
		ufcs_err("Can not find ufcs_charge_oplus_strategy node\n");
		return -EINVAL;
	}
	for (i = 0; i < chip->limits.ufcs_strategy_soc_num - 1; i++) {
		soc_node =
			of_get_child_by_name(ufcs_node, ufcs_strategy_soc[i]);
		if (!soc_node) {
			ufcs_err("Can not find oplus ufcs %s node\n",
				 ufcs_strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < chip->limits.ufcs_strategy_temp_num - 1; j++) {
			rc = of_property_count_elems_of_size(
				soc_node, ufcs_strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				ufcs_err("Count oplus %s failed, rc=%d\n",
					 ufcs_strategy_temp[j], rc);
				return rc;
			}

			length = rc;

			switch (i) {
			case UFCS_BATT_CURVE_SOC_RANGE_MIN:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_oplus_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_oplus_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_LOW:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_oplus_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_oplus_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_MID_LOW:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_oplus_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_oplus_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_MID:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_oplus_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_oplus_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_MID_HIGH:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_oplus_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_oplus_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			case UFCS_BATT_CURVE_SOC_RANGE_HIGH:
				rc = of_property_read_u32_array(
					soc_node, ufcs_strategy_temp[j],
					(u32 *)chip->batt_curves_oplus_soc[i]
						.batt_curves_temp[j]
						.batt_curves,
					length);
				chip->batt_curves_oplus_soc[i]
					.batt_curves_temp[j]
					.batt_curve_num = length / 5;
				break;
			default:
				break;
			}
		}
	}

	for (i = 0; i < chip->limits.ufcs_strategy_soc_num - 1; i++) {
		for (j = 0; j < chip->limits.ufcs_strategy_temp_num - 1; j++) {
			for (k = 0; k < chip->batt_curves_oplus_soc[i]
						.batt_curves_temp[j]
						.batt_curve_num;
			     k++) {
				ufcs_debug(":oplus i = %d,j = %d  %d %d %d %d "
					   "%d\n",
					   i, j, chip->batt_curves_oplus_soc[i]
							 .batt_curves_temp[j]
							 .batt_curves[k]
							 .target_vbus,
					   chip->batt_curves_oplus_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .target_vbat,
					   chip->batt_curves_oplus_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .target_ibus,
					   chip->batt_curves_oplus_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .exit,
					   chip->batt_curves_oplus_soc[i]
						   .batt_curves_temp[j]
						   .batt_curves[k]
						   .target_time);
			}
		}
	}

	return rc;
}

static int oplus_ufcs_parse_low_curr_full_curves(struct oplus_ufcs_chip *chip)
{
	struct device_node *node, *full_node;
	int rc = 0, i, j, length;

	if (!chip || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	node = chip->dev->of_node;

	full_node = of_get_child_by_name(node, "ufcs_charge_low_curr_full");
	if (!full_node) {
		ufcs_err("Can not find ufcs_charge_low_curr_full node\n");
		return -EINVAL;
	}

	for (i = 0; i < UFCS_LOW_CURR_FULL_CURVE_TEMP_MAX; i++) {
		rc = of_property_count_elems_of_size(
			full_node, strategy_low_curr_full[i], sizeof(u32));
		if (rc < 0) {
			ufcs_err("Count low curr %s failed, rc=%d\n",
				 strategy_low_curr_full[i], rc);
			return rc;
		}

		length = rc;

		switch (i) {
		case UFCS_LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL:
			rc = of_property_read_u32_array(
				full_node, strategy_low_curr_full[i],
				(u32 *)chip->low_curr_full_curves_temp[i]
					.full_curves,
				length);
			chip->low_curr_full_curves_temp[i].full_curve_num =
				length / 3;
			break;
		case UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW:
			rc = of_property_read_u32_array(
				full_node, strategy_low_curr_full[i],
				(u32 *)chip->low_curr_full_curves_temp[i]
					.full_curves,
				length);
			chip->low_curr_full_curves_temp[i].full_curve_num =
				length / 3;
			break;
		case UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH:
			rc = of_property_read_u32_array(
				full_node, strategy_low_curr_full[i],
				(u32 *)chip->low_curr_full_curves_temp[i]
					.full_curves,
				length);
			chip->low_curr_full_curves_temp[i].full_curve_num =
				length / 3;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < UFCS_LOW_CURR_FULL_CURVE_TEMP_MAX; i++) {
		for (j = 0;
		     j < chip->low_curr_full_curves_temp[i].full_curve_num;
		     j++) {
			ufcs_debug(": i = %d,  %d %d %d\n", i,
				   chip->low_curr_full_curves_temp[i]
					   .full_curves[j]
					   .iterm,
				   chip->low_curr_full_curves_temp[i]
					   .full_curves[j]
					   .vterm,
				   chip->low_curr_full_curves_temp[i]
					   .full_curves[j]
					   .exit);
		}
	}
	return rc;
}

static int oplus_ufcs_parse_dt(struct oplus_ufcs_chip *chip)
{
	struct device_node *node;

	if (!chip) {
		ufcs_err("g_ufcs_chip null!\n");
		return -ENODEV;
	}

	node = chip->dev->of_node;
	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -ENODEV;
	}

	oplus_ufcs_parse_charge_strategy(chip);
	oplus_ufcs_parse_resistense_strategy(chip);
	oplus_ufcs_parse_batt_curves_third(chip);
	oplus_ufcs_parse_batt_curves_oplus(chip);
	oplus_ufcs_parse_low_curr_full_curves(chip);

	return 0;
}

static int oplus_ufcs_get_curve_vbus(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_vbus;
}

static int oplus_ufcs_get_curve_ibus(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;
}

static int oplus_ufcs_get_curve_time(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_time;
}

static int oplus_ufcs_get_last_curve_vbat(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index - 1]
		.target_vbat;
}

static int oplus_ufcs_get_curve_vbat(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_vbat;
}

static int oplus_ufcs_get_icurr_ratio(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type || !chip->ops) {
		return UFCS_BYPASS_MODE;
	}

	if (!chip->ops->ufcs_cp_mode_init) {
		return UFCS_BYPASS_MODE;
	}
	if (chip->cp_mode == UFCS_SC_MODE) {
		return UFCS_BYPASS_MODE;
	} else {
		return UFCS_SC_MODE;
	}
}

static int oplus_ufcs_get_cp_ratio(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type || !chip->ops) {
		return UFCS_BYPASS_MODE;
	}

	if (!chip->ops->ufcs_cp_mode_init) {
		return UFCS_BYPASS_MODE;
	}
	if (chip->cp_mode == UFCS_SC_MODE) {
		return UFCS_SC_MODE;
	} else {
		return UFCS_BYPASS_MODE;
	}
}

static int oplus_ufcs_get_ufcs_status(struct oplus_ufcs_chip *chip)
{
	int volt = 0, curr = 0, ret = 0;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	if (!chip->ops->ufcs_get_src_info) {
		return -ENODEV;
	}

	ret = chip->ops->ufcs_get_src_info(&volt, &curr);
	if (ret < 0) {
		ufcs_err(", ret = %d\n", ret);
		oplus_ufcs_error_log();
		if (chip->ufcs_error->rcv_hardreset ||
		    chip->ufcs_error->rcv_hardreset)
			chip->ufcs_stop_status = UFCS_STOP_VOTER_PDO_ERROR;
	} else {
		ufcs_err(" volt = %d, curr = %d\n", volt, curr);
	}

	chip->charger_output_volt = volt;
	chip->charger_output_current = curr;

	return 0;
}

static void oplus_ufcs_sink_data(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int ret = 0;

	struct oplus_ufcs_sink_info *sink_data;
	if (!chip || !chip->ops || !chip->ops->ufcs_transfer_sink_info) {
		return;
	}

	sink_data = kzalloc(sizeof(struct oplus_ufcs_sink_info), GFP_KERNEL);
	if (!sink_data)
		return;

	sink_data->batt_temp = chip->ap_batt_temperature;
	sink_data->usb_temp = chip->ap_batt_temperature;
	sink_data->batt_volt = chip->ap_batt_volt;
	sink_data->batt_curr = chip->ap_batt_current;

	ret = chip->ops->ufcs_transfer_sink_info(sink_data);

	if (ret < 0) {
		ufcs_err("oplus_ufcs_sink_data errr\n");
	}
	kfree(sink_data);
}

static int oplus_ufcs_get_charging_data(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	oplus_chg_disable_charge();
	oplus_chg_suspend_charger();

	chip->ap_batt_soc = oplus_chg_get_ui_soc();
	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();
	chip->ap_fg_temperature = oplus_gauge_get_batt_temperature();
	chip->ap_batt_current = oplus_gauge_get_batt_current();
	chip->current_adapter_max =
		chip->ops->get_ufcs_max_cur(oplus_ufcs_get_curve_vbus(chip));
	oplus_ufcs_get_ufcs_status(chip);
	chip->batt_input_current =
		abs(chip->ap_batt_current / oplus_ufcs_get_cp_ratio());
	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY ||
	    oplus_ufcs_get_support_type() == UFCS_SUPPORT_2CP ||
	    oplus_ufcs_get_support_type() == UFCS_SUPPORT_3CP) {
		chip->ap_input_current = chip->batt_input_current;
		chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();

		chip->ap_input_volt = oplus_voocphy_get_cp_vbus();
		chip->cp_master_ibus = chip->ops->ufcs_get_cp_master_ibus();
		chip->cp_master_vac = chip->ops->ufcs_get_cp_master_vac();
		chip->cp_master_vout = chip->ops->ufcs_get_cp_master_vout();

		chip->slave_input_volt = 0;
		chip->cp_slave_ibus = 0;
		chip->cp_slave_vac = 0;
		chip->cp_slave_vout = 0;
	} else if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_MCU ||
		   oplus_ufcs_get_support_type() == UFCS_SUPPORT_ADSP_VOOCPHY) {
		chip->ap_input_current = chip->batt_input_current;
		chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
		chip->ap_input_volt = oplus_chg_get_charger_voltage();
		chip->cp_master_ibus = 0;
		chip->cp_master_vac = 0;
		chip->cp_master_vout = 0;

		chip->slave_input_volt = 0;
		chip->cp_slave_ibus = 0;
		chip->cp_slave_vac = 0;
		chip->cp_slave_vout = 0;
	}
	oplus_ufcs_sink_data();

	ufcs_err(" [%d, %d, %d, %d, %d, %d]\n", chip->ap_batt_volt,
		 chip->ap_batt_current, chip->ap_input_volt,
		 chip->ap_input_current, chip->charger_output_volt,
		 chip->charger_output_current);
	return 0;
}

static int oplus_ufcs_check_bcc_curr(struct oplus_ufcs_chip *chip)
{
	int index_vbat, index_min = 0, index_max = 0, bcc_min = 0, bcc_max = 0;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		ufcs_err("g_ufcs_chip null!\n");
		return -ENODEV;
	}
	index_vbat = chip->batt_curves.batt_curves[chip->batt_curve_index]
			     .target_vbat;

	for (index_min = 0; index_min < chip->batt_curves.batt_curve_num;
	     index_min++) {
		ufcs_debug(
			" [%d, %d, %d, %d] [%d, %d, %d, %d]\n", index_vbat,
			index_min,
			chip->batt_curves.batt_curves[index_min].target_vbus,
			chip->batt_curves.batt_curves[index_min].target_vbat,
			chip->batt_curves.batt_curves[index_min].target_ibus,
			chip->batt_curves.batt_curves[index_min].exit,
			chip->batt_curves.batt_curves[index_min].target_time);
		if (index_vbat ==
		    chip->batt_curves.batt_curves[index_min].target_vbat) {
			ufcs_err("index_min found!\n");
			break;
		}
	}

	if (index_min >= chip->batt_curves.batt_curve_num) {
		index_min = chip->batt_curves.batt_curve_num - 1;
	}
	for (index_max = index_min + 1;
	     index_max < chip->batt_curves.batt_curve_num; index_max++) {
		ufcs_debug(
			" index_max index_vbat = %d,  [%d, %d, %d, %d, %d, "
			"%d]\n",
			index_vbat, index_max,
			chip->batt_curves.batt_curves[index_max].target_vbus,
			chip->batt_curves.batt_curves[index_max].target_vbat,
			chip->batt_curves.batt_curves[index_max].target_ibus,
			chip->batt_curves.batt_curves[index_max].exit,
			chip->batt_curves.batt_curves[index_max].target_time);
		if (index_vbat !=
		    chip->batt_curves.batt_curves[index_max].target_vbat) {
			index_max = index_max - 1;
			ufcs_err("index_max found!\n");
			break;
		}
	}
	if (index_max < index_min) {
		index_max = index_min;
	}
	if (index_max >= chip->batt_curves.batt_curve_num) {
		index_max = chip->batt_curves.batt_curve_num - 1;
	}

	bcc_max = chip->batt_curves.batt_curves[index_min].target_ibus *
		  oplus_ufcs_get_icurr_ratio() / 100;
	bcc_min = chip->batt_curves.batt_curves[index_max].target_ibus *
		  oplus_ufcs_get_icurr_ratio() / 100;
	chip->bcc_exit_curr =
		chip->batt_curves
			.batt_curves[chip->batt_curves.batt_curve_num - 1]
			.target_ibus *
		oplus_ufcs_get_icurr_ratio();

	if ((bcc_min - UFCS_BCC_CURRENT_MIN) <
	    (chip->bcc_exit_curr / UFCS_BATT_CURR_TO_BCC_CURR))
		bcc_min = chip->bcc_exit_curr / UFCS_BATT_CURR_TO_BCC_CURR;
	else
		bcc_min = bcc_min - UFCS_BCC_CURRENT_MIN;

	chip->bcc_max_curr = bcc_max;
	chip->bcc_min_curr = bcc_min;
	ufcs_err("[%d, %d, %d, %d, %d]\n", index_vbat, chip->batt_curve_index,
		 chip->bcc_max_curr, chip->bcc_min_curr, chip->bcc_exit_curr);
	return 0;
}

static int oplus_ufcs_cp_mode_init(int mode)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int ret = 0;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	if (!chip->ops->ufcs_cp_mode_init) {
		ufcs_err(", ufcs_cp_mode_init null!\n");
		return -EINVAL;
	}
	chip->cp_mode = mode;

	ret = chip->ops->ufcs_cp_mode_init(mode);
	return ret;
}

static int oplus_ufcs_cp_mode_check(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type || !chip->ops) {
		return -EINVAL;
	}

	if (oplus_ufcs_get_curve_vbus(chip) == UFCS_VOL_MAX_V2) {
		oplus_ufcs_cp_mode_init(UFCS_SC_MODE);
	} else {
		if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY ||
		    oplus_ufcs_get_support_type() == UFCS_SUPPORT_2CP ||
		    oplus_ufcs_get_support_type() == UFCS_SUPPORT_3CP)
			oplus_ufcs_cp_mode_init(UFCS_SC_MODE);
		else
			oplus_ufcs_cp_mode_init(UFCS_BYPASS_MODE);
	}

	return 0;
}

static int oplus_ufcs_choose_curves(struct oplus_ufcs_chip *chip)
{
	int i;
	int batt_soc_plugin, batt_temp_plugin;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	chip->ap_batt_soc = oplus_chg_get_ui_soc();
	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();

	if (chip->ap_batt_soc <= chip->limits.ufcs_strategy_soc_min) {
		batt_soc_plugin = UFCS_BATT_CURVE_SOC_RANGE_MIN;
	} else if (chip->ap_batt_soc <= chip->limits.ufcs_strategy_soc_low) {
		batt_soc_plugin = UFCS_BATT_CURVE_SOC_RANGE_LOW;
	} else if (chip->ap_batt_soc <=
		   chip->limits.ufcs_strategy_soc_mid_low) {
		batt_soc_plugin = UFCS_BATT_CURVE_SOC_RANGE_MID_LOW;
	} else if (chip->ap_batt_soc <= chip->limits.ufcs_strategy_soc_mid) {
		batt_soc_plugin = UFCS_BATT_CURVE_SOC_RANGE_MID;
	} else if (chip->ap_batt_soc <=
		   chip->limits.ufcs_strategy_soc_mid_high) {
		batt_soc_plugin = UFCS_BATT_CURVE_SOC_RANGE_MID_HIGH;
	} else if (chip->ap_batt_soc <= chip->limits.ufcs_strategy_soc_high) {
		batt_soc_plugin = UFCS_BATT_CURVE_SOC_RANGE_HIGH;
	} else {
		batt_soc_plugin = UFCS_BATT_CURVE_SOC_RANGE_MAX;
		ufcs_err("batt soc high, stop ufcs\n");
		chip->ufcs_stop_status = UFCS_STOP_VOTER_OTHER_ABORMAL;
		return -EINVAL;
	}

	if (chip->ap_batt_temperature < chip->limits.ufcs_batt_over_low_temp) {
		ufcs_err("batt temp low, stop ufcs\n");
		chip->ufcs_stop_status = UFCS_STOP_VOTER_TBATT_OVER;
		return -EINVAL;
	} else if (chip->ap_batt_temperature <
		   chip->limits.ufcs_little_cold_temp) { /*0-5C*/
		batt_temp_plugin = UFCS_BATT_CURVE_TEMP_RANGE_LITTLE_COLD;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_LITTLE_COLD;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_LITTLE_COLD;
	} else if (chip->ap_batt_temperature <
		   chip->limits.ufcs_cool_temp) { /*5-12C*/
		batt_temp_plugin = UFCS_BATT_CURVE_TEMP_RANGE_COOL;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_COOL;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_COOL;
	} else if (chip->ap_batt_temperature <
		   chip->limits.ufcs_little_cool_temp) { /*12-16C*/
		batt_temp_plugin = UFCS_BATT_CURVE_TEMP_RANGE_LITTLE_COOL;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_LITTLE_COOL;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_LITTLE_COOL;
	} else if (chip->ap_batt_temperature <
		   chip->limits.ufcs_normal_low_temp) { /*16-35C*/
		batt_temp_plugin = UFCS_BATT_CURVE_TEMP_RANGE_NORMAL_LOW;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_NORMAL_LOW;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_NORMAL_LOW;
	} else if (chip->ap_batt_temperature <
		   chip->limits.ufcs_normal_high_temp) { /*25C-43C*/
		batt_temp_plugin = UFCS_BATT_CURVE_TEMP_RANGE_NORMAL_HIGH;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_NORMAL_HIGH;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_NORMAL_HIGH;
	} else if (chip->ap_batt_temperature <
		   chip->limits.ufcs_batt_over_high_temp) {
		batt_temp_plugin = UFCS_BATT_CURVE_TEMP_RANGE_WARM;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_WARM;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_WARM;
	} else {
		ufcs_err("batt temp high, stop ufcs\n");
		chip->ufcs_stop_status = UFCS_STOP_VOTER_TBATT_OVER;
		batt_temp_plugin = UFCS_BATT_CURVE_TEMP_RANGE_MAX;
		return -EINVAL;
	}

	if (!chip->ufcs_authentication) {
		memcpy(&chip->batt_curves,
		       chip->batt_curves_third_soc[batt_soc_plugin]
			       .batt_curves_temp[batt_temp_plugin]
			       .batt_curves,
		       sizeof(struct ufcs_batt_curves));
	} else {
		memcpy(&chip->batt_curves,
		       chip->batt_curves_oplus_soc[batt_soc_plugin]
			       .batt_curves_temp[batt_temp_plugin]
			       .batt_curves,
		       sizeof(struct ufcs_batt_curves));
	}

	ufcs_err("[%d, %d, %d, %d, %d, %d]", chip->ap_batt_temperature,
		 chip->ap_batt_soc, batt_soc_plugin, batt_temp_plugin,
		 chip->ufcs_temp_cur_range,
		 chip->ufcs_fastchg_batt_temp_status);

	for (i = 0; i < chip->batt_curves.batt_curve_num; i++) {
		ufcs_err("i= %d: %d %d %d %d %d\n", i,
			 chip->batt_curves.batt_curves[i].target_vbus,
			 chip->batt_curves.batt_curves[i].target_vbat,
			 chip->batt_curves.batt_curves[i].target_ibus,
			 chip->batt_curves.batt_curves[i].exit,
			 chip->batt_curves.batt_curves[i].target_time);
	}

	chip->batt_curve_index = 0;

	oplus_ufcs_cp_mode_check();

	while (chip->ops->get_ufcs_max_cur(oplus_ufcs_get_curve_vbus(chip)) <
		       oplus_ufcs_get_curve_ibus(chip) &&
	       chip->batt_curve_index < chip->batt_curves.batt_curve_num) {
		ufcs_err(" [%d, %d, %d, %d]\r\n",
			 oplus_ufcs_get_curve_vbus(chip),
			 chip->ops->get_ufcs_max_cur(
				 oplus_ufcs_get_curve_vbus(chip)),
			 oplus_ufcs_get_curve_ibus(chip),
			 chip->batt_curve_index);

		chip->batt_curve_index++;
		chip->timer.batt_curve_time = 0;
	}
	if (chip->batt_curve_index >= chip->batt_curves.batt_curve_num) {
		chip->batt_curve_index = chip->batt_curves.batt_curve_num - 1;
	}
	chip->current_batt_curve =
		chip->batt_curves.batt_curves[chip->batt_curve_index]
			.target_ibus *
		oplus_ufcs_get_icurr_ratio();
	oplus_ufcs_check_bcc_curr(chip);

	return 0;
}

static int oplus_ufcs_variables_init(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int ret = 0;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	oplus_ufcs_r_init(chip);
	oplus_ufcs_count_init(chip);
	chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_NATURAL;
	chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_INIT;
	ret = oplus_ufcs_choose_curves(chip);
	if (ret < 0) {
		return ret;
	}

	oplus_ufcs_get_charging_data(chip);
	if (!chip->ufcs_authentication) {
		chip->timer.ufcs_timeout_time = chip->limits.ufcs_timeout_third;
	} else {
		chip->timer.ufcs_timeout_time = chip->limits.ufcs_timeout_oplus;
	}

	chip->ufcs_status = OPLUS_UFCS_STATUS_START;
	chip->ufcs_low_curr_full_temp_status =
		UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW;
	chip->ufcs_stop_status = UFCS_STOP_VOTER_NONE;
	chip->timer.batt_curve_time = 0;
	chip->timer.set_pdo_flag = 0;
	chip->timer.set_temp_flag = 0;
	chip->timer.check_ibat_flag = 0;
	chip->target_charger_volt = 0;
	chip->target_charger_current = 0;
	chip->target_charger_current_pre = 0;
	chip->ask_charger_volt_last = 0;
	chip->ask_charger_current_last = 0;
	chip->ask_charger_volt = UFCS_VOL_CURVE_LMAX;
	chip->ask_charger_current = 0;
	chip->cp_r_down = OPLUS_UFCS_CURRENT_LIMIT_MAX;
	chip->cp_ibus_down = OPLUS_UFCS_CURRENT_LIMIT_MAX;
	chip->current_bcc = OPLUS_UFCS_CURRENT_LIMIT_MAX;

	chip->timer.fastchg_timer = current_kernel_time();
	chip->timer.temp_timer = current_kernel_time();
	chip->timer.pdo_timer = current_kernel_time();
	chip->timer.ibat_timer = current_kernel_time();
	chip->current_batt_curve =
		chip->batt_curves.batt_curves[chip->batt_curve_index]
			.target_ibus *
		oplus_ufcs_get_icurr_ratio();
	chip->ufcs_error = kzalloc(sizeof(struct oplus_ufcs_error), GFP_KERNEL);
	if (!chip->ufcs_error)
		return -ENOMEM;

	return 0;
}

static int oplus_ufcs_charging_enable_master(struct oplus_ufcs_chip *chip,
					     bool on)
{
	int status = 0;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	if (on) {
		status = chip->ops->ufcs_mos_ctrl(1);
	} else {
		status = chip->ops->ufcs_mos_ctrl(0);
	}
	return status;
}

static void oplus_ufcs_reset_temp_range(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	chip->limits.ufcs_normal_high_temp =
		chip->limits.default_ufcs_normal_high_temp;
	chip->limits.ufcs_little_cold_temp =
		chip->limits.default_ufcs_little_cold_temp;
	chip->limits.ufcs_cool_temp = chip->limits.default_ufcs_cool_temp;
	chip->limits.ufcs_little_cool_temp =
		chip->limits.default_ufcs_little_cool_temp;
	chip->limits.ufcs_normal_low_temp =
		chip->limits.default_ufcs_normal_low_temp;
}

static int oplus_ufcs_set_current_warm_range(struct oplus_ufcs_chip *chip,
					     int vbat_temp_cur)
{
	int ret = chip->limits.ufcs_strategy_normal_current;

	if (chip->limits.ufcs_batt_over_high_temp != -EINVAL &&
	    vbat_temp_cur > chip->limits.ufcs_batt_over_high_temp) {
		chip->limits.ufcs_strategy_change_count++;
		if (chip->limits.ufcs_strategy_change_count >=
		    UFCS_TEMP_OVER_COUNTS) {
			chip->limits.ufcs_strategy_change_count = 0;
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_EXIT;
			ret = chip->limits.ufcs_over_high_or_low_current;
			ufcs_err(" temp_over:%d", vbat_temp_cur);
		}
	} else if (vbat_temp_cur < chip->limits.ufcs_normal_high_temp) {
		chip->limits.ufcs_strategy_change_count++;
		if (chip->limits.ufcs_strategy_change_count >=
		    UFCS_TEMP_OVER_COUNTS) {
			chip->limits.ufcs_strategy_change_count = 0;
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_NORMAL_HIGH;
			chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_INIT;
			ret = chip->limits.ufcs_strategy_normal_current;
			oplus_ufcs_reset_temp_range(chip);
			oplus_ufcs_choose_curves(chip);
			chip->limits.ufcs_normal_high_temp +=
				UFCS_TEMP_WARM_RANGE_THD;
		}

	} else {
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_WARM;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_WARM;
		ret = chip->limits.ufcs_strategy_normal_current;
	}

	return ret;
}

static int
oplus_ufcs_set_current_temp_normal_range(struct oplus_ufcs_chip *chip,
					 int vbat_temp_cur)
{
	int ret = chip->limits.ufcs_strategy_normal_current;

	switch (chip->ufcs_fastchg_batt_temp_status) {
	case UFCS_BAT_TEMP_NORMAL_HIGH:
		if (vbat_temp_cur >
		    chip->limits.ufcs_strategy_batt_high_temp0) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH0;
			ret = chip->limits.ufcs_strategy_high_current0;
		} else if (vbat_temp_cur >= chip->limits.ufcs_normal_low_temp) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_NORMAL_HIGH;
			ret = chip->limits.ufcs_strategy_normal_current;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_NORMAL_LOW;
			chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_NORMAL_LOW;
			ret = chip->limits.ufcs_strategy_normal_current;
			oplus_ufcs_reset_temp_range(chip);
			chip->limits.ufcs_normal_low_temp +=
				UFCS_TEMP_WARM_RANGE_THD;
		}
		break;
	case UFCS_BAT_TEMP_HIGH0:
		if (vbat_temp_cur >
		    chip->limits.ufcs_strategy_batt_high_temp1) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH1;
			ret = chip->limits.ufcs_strategy_high_current1;
		} else if (vbat_temp_cur <
			   chip->limits.ufcs_strategy_batt_low_temp0) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW0;
			ret = chip->limits.ufcs_strategy_low_current0;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH0;
			ret = chip->limits.ufcs_strategy_high_current0;
		}
		break;
	case UFCS_BAT_TEMP_HIGH1:
		if (vbat_temp_cur >
		    chip->limits.ufcs_strategy_batt_high_temp2) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH2;
			ret = chip->limits.ufcs_strategy_high_current2;
		} else if (vbat_temp_cur <
			   chip->limits.ufcs_strategy_batt_low_temp1) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW1;
			ret = chip->limits.ufcs_strategy_low_current1;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH1;
			ret = chip->limits.ufcs_strategy_high_current1;
		}
		break;
	case UFCS_BAT_TEMP_HIGH2:
		if (chip->limits.ufcs_normal_high_temp != -EINVAL &&
		    vbat_temp_cur > chip->limits.ufcs_normal_high_temp) {
			chip->limits.ufcs_strategy_change_count++;
			if (chip->limits.ufcs_strategy_change_count >=
			    UFCS_TEMP_OVER_COUNTS) {
				chip->limits.ufcs_strategy_change_count = 0;
				if (chip->ap_batt_soc <
					    chip->limits.ufcs_warm_allow_soc &&
				    chip->ap_batt_volt <
					    chip->limits.ufcs_warm_allow_vol) {
					chip->ufcs_fastchg_batt_temp_status =
						UFCS_BAT_TEMP_WARM;
					chip->ufcs_temp_cur_range =
						UFCS_TEMP_RANGE_INIT;
					ret = chip->limits
						      .ufcs_strategy_high_current2;
					oplus_ufcs_reset_temp_range(chip);
					oplus_ufcs_choose_curves(chip);
					chip->limits.ufcs_normal_high_temp -=
						UFCS_TEMP_WARM_RANGE_THD;
				} else {
					ufcs_err("high temp_over:%d",
						 vbat_temp_cur);
					chip->ufcs_fastchg_batt_temp_status =
						UFCS_BAT_TEMP_EXIT;
					ret = chip->limits
						      .ufcs_over_high_or_low_current;
				}
			}
		} else if (chip->limits.ufcs_batt_over_high_temp != -EINVAL &&
			   vbat_temp_cur >
				   chip->limits.ufcs_batt_over_high_temp) {
			chip->limits.ufcs_strategy_change_count++;
			if (chip->limits.ufcs_strategy_change_count >=
			    UFCS_TEMP_OVER_COUNTS) {
				chip->limits.ufcs_strategy_change_count = 0;
				chip->ufcs_fastchg_batt_temp_status =
					UFCS_BAT_TEMP_EXIT;
				ret = chip->limits.ufcs_over_high_or_low_current;
				ufcs_err(" over_high temp_over:%d",
					 vbat_temp_cur);
			}
		} else if (vbat_temp_cur <
			   chip->limits.ufcs_strategy_batt_low_temp2) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW2;
			ret = chip->limits.ufcs_strategy_low_current2;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH2;
			ret = chip->limits.ufcs_strategy_high_current2;
		}
		break;
	case UFCS_BAT_TEMP_LOW0:
		if (vbat_temp_cur >
		    chip->limits.ufcs_strategy_batt_high_temp0) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH0;
			ret = chip->limits.ufcs_strategy_high_current0;
		} else if (vbat_temp_cur <
			   chip->limits.ufcs_strategy_batt_low_temp0) { /*T<39C*/
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_NORMAL_HIGH;
			ret = chip->limits.ufcs_strategy_normal_current;
			oplus_ufcs_reset_temp_range(chip);
			chip->limits.ufcs_strategy_batt_low_temp0 +=
				UFCS_TEMP_WARM_RANGE_THD;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW0;
			ret = chip->limits.ufcs_strategy_low_current0;
		}
		break;
	case UFCS_BAT_TEMP_LOW1:
		if (vbat_temp_cur >
		    chip->limits.ufcs_strategy_batt_high_temp1) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH1;
			ret = chip->limits.ufcs_strategy_high_current2;
		} else if (vbat_temp_cur <
			   chip->limits.ufcs_strategy_batt_low_temp0) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW0;
			ret = chip->limits.ufcs_strategy_low_current0;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW1;
			ret = chip->limits.ufcs_strategy_low_current1;
		}
		break;
	case UFCS_BAT_TEMP_LOW2:
		if (vbat_temp_cur >
		    chip->limits.ufcs_strategy_batt_high_temp2) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_HIGH2;
			ret = chip->limits.ufcs_strategy_high_current2;
		} else if (vbat_temp_cur <
			   chip->limits.ufcs_strategy_batt_low_temp1) {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW1;
			ret = chip->limits.ufcs_strategy_low_current1;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LOW2;
			ret = chip->limits.ufcs_strategy_low_current2;
		}
		break;
	default:
		break;
	}
	ufcs_err("the ret: %d, the temp =%d, status = %d\r\n", ret,
		 vbat_temp_cur, chip->ufcs_fastchg_batt_temp_status);
	return ret;
}

static int
oplus_ufcs_set_current_temp_low_normal_range(struct oplus_ufcs_chip *chip,
					     int vbat_temp_cur)
{
	int ret = chip->limits.ufcs_strategy_normal_current;

	if (vbat_temp_cur < chip->limits.ufcs_normal_low_temp &&
	    vbat_temp_cur >=
		    chip->limits.ufcs_little_cool_temp) { /*20C<=T<35C*/
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_NORMAL_LOW;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_NORMAL_LOW;
		ret = chip->limits.ufcs_strategy_normal_current;
	} else {
		if (vbat_temp_cur >= chip->limits.ufcs_normal_low_temp) {
			chip->limits.ufcs_normal_low_temp -=
				UFCS_TEMP_LOW_RANGE_THD;
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_NORMAL_HIGH;
			ret = chip->limits.ufcs_strategy_normal_current;
			chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_NORMAL_HIGH;
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_LITTLE_COOL;
			chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_LITTLE_COOL;
			ret = chip->limits.ufcs_strategy_normal_current;
			oplus_ufcs_reset_temp_range(chip);
			chip->limits.ufcs_little_cool_temp +=
				UFCS_TEMP_LOW_RANGE_THD;
		}
	}

	return ret;
}

static int
oplus_ufcs_set_current_temp_little_cool_range(struct oplus_ufcs_chip *chip,
					      int vbat_temp_cur)
{
	int ret = 0;

	if (vbat_temp_cur < chip->limits.ufcs_little_cool_temp &&
	    vbat_temp_cur >= chip->limits.ufcs_cool_temp) { /*12C<=T<20C*/
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_LITTLE_COOL;
		ret = chip->limits.ufcs_strategy_normal_current;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_LITTLE_COOL;
	} else if (vbat_temp_cur >= chip->limits.ufcs_little_cool_temp) {
		chip->limits.ufcs_little_cool_temp -= UFCS_TEMP_LOW_RANGE_THD;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_NORMAL_LOW;
		ret = chip->limits.ufcs_strategy_normal_current;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_NORMAL_LOW;
	} else {
		if (oplus_chg_get_ui_soc() <=
		    chip->limits.ufcs_strategy_soc_high) {
			chip->limits.ufcs_strategy_change_count++;
			if (chip->limits.ufcs_strategy_change_count >=
			    UFCS_TEMP_OVER_COUNTS) {
				chip->limits.ufcs_strategy_change_count = 0;
				chip->ufcs_fastchg_batt_temp_status =
					UFCS_BAT_TEMP_COOL;
				chip->ufcs_temp_cur_range =
					UFCS_TEMP_RANGE_INIT;
				oplus_ufcs_choose_curves(chip);
				ufcs_err(" switch temp range:%d",
					 vbat_temp_cur);
			}
		} else {
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_COOL;
			chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_COOL;
		}
		ret = chip->limits.ufcs_strategy_normal_current;
		oplus_ufcs_reset_temp_range(chip);
		chip->limits.ufcs_cool_temp += UFCS_TEMP_LOW_RANGE_THD;
	}

	return ret;
}

static int oplus_ufcs_set_current_temp_cool_range(struct oplus_ufcs_chip *chip,
						  int vbat_temp_cur)
{
	int ret = 0;
	if (chip->limits.ufcs_batt_over_low_temp != -EINVAL &&
	    vbat_temp_cur < chip->limits.ufcs_batt_over_low_temp) {
		chip->limits.ufcs_strategy_change_count++;
		if (chip->limits.ufcs_strategy_change_count >=
		    UFCS_TEMP_OVER_COUNTS) {
			chip->limits.ufcs_strategy_change_count = 0;
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_EXIT;
			ret = chip->limits.ufcs_over_high_or_low_current;
			ufcs_err(" temp_over:%d", vbat_temp_cur);
		}
	} else if (vbat_temp_cur < chip->limits.ufcs_cool_temp &&
		   vbat_temp_cur >=
			   chip->limits.ufcs_little_cold_temp) { /*5C <=T<12C*/
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_COOL;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_COOL;
		ret = chip->limits.ufcs_strategy_normal_current;
	} else if (vbat_temp_cur >= chip->limits.ufcs_cool_temp) {
		chip->limits.ufcs_strategy_change_count++;
		if (chip->limits.ufcs_strategy_change_count >=
		    UFCS_TEMP_OVER_COUNTS) {
			chip->limits.ufcs_strategy_change_count = 0;
			if (oplus_chg_get_ui_soc() <=
			    chip->limits.ufcs_strategy_soc_high) {
				chip->ufcs_fastchg_batt_temp_status =
					UFCS_BAT_TEMP_LITTLE_COOL;
				chip->ufcs_temp_cur_range =
					UFCS_TEMP_RANGE_INIT;
				oplus_ufcs_choose_curves(chip);
				ufcs_err(" switch temp range:%d",
					 vbat_temp_cur);
			} else {
				chip->ufcs_fastchg_batt_temp_status =
					UFCS_BAT_TEMP_LITTLE_COOL;
				chip->ufcs_temp_cur_range =
					UFCS_TEMP_RANGE_LITTLE_COOL;
			}
			oplus_ufcs_reset_temp_range(chip);
			chip->limits.ufcs_cool_temp -= UFCS_TEMP_LOW_RANGE_THD;
		}

		ret = chip->limits.ufcs_strategy_normal_current;
	} else {
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_LITTLE_COLD;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_LITTLE_COLD;
		ret = chip->limits.ufcs_strategy_normal_current;
		oplus_ufcs_reset_temp_range(chip);
		chip->limits.ufcs_little_cold_temp += UFCS_TEMP_LOW_RANGE_THD;
	}
	return ret;
}

static int
oplus_ufcs_set_current_temp_little_cold_range(struct oplus_ufcs_chip *chip,
					      int vbat_temp_cur)
{
	int ret = 0;
	if (chip->limits.ufcs_batt_over_low_temp != -EINVAL &&
	    vbat_temp_cur < chip->limits.ufcs_batt_over_low_temp) {
		chip->limits.ufcs_strategy_change_count++;
		if (chip->limits.ufcs_strategy_change_count >=
		    UFCS_TEMP_OVER_COUNTS) {
			chip->limits.ufcs_strategy_change_count = 0;
			ret = chip->limits.ufcs_over_high_or_low_current;
			chip->ufcs_fastchg_batt_temp_status =
				UFCS_BAT_TEMP_EXIT;
			ufcs_err(" temp_over:%d", vbat_temp_cur);
		}
	} else if (vbat_temp_cur <
		   chip->limits.ufcs_little_cold_temp) { /*0C<=T<5C*/
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_LITTLE_COLD;
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_LITTLE_COLD;
		ret = chip->limits.ufcs_strategy_normal_current;
	} else {
		chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_COOL;
		ret = chip->limits.ufcs_strategy_normal_current;
		chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_COOL;
		oplus_ufcs_reset_temp_range(chip);
		chip->limits.ufcs_little_cold_temp -= UFCS_TEMP_LOW_RANGE_THD;
	}

	return ret;
}

static int oplus_ufcs_get_batt_temp_curr(struct oplus_ufcs_chip *chip)

{
	int ret = 0;
	int vbat_temp_cur = 350;

	if (!chip || !chip->ufcs_support_type) {
		return -ENODEV;
	}
	vbat_temp_cur = chip->ap_batt_temperature;
	ret = chip->limits.ufcs_strategy_normal_current;
	switch (chip->ufcs_temp_cur_range) {
	case UFCS_TEMP_RANGE_WARM:
		ret = oplus_ufcs_set_current_warm_range(chip, vbat_temp_cur);
		break;
	case UFCS_TEMP_RANGE_NORMAL_HIGH:
		ret = oplus_ufcs_set_current_temp_normal_range(chip,
							       vbat_temp_cur);
		break;
	case UFCS_TEMP_RANGE_NORMAL_LOW:
		ret = oplus_ufcs_set_current_temp_low_normal_range(
			chip, vbat_temp_cur);
		break;
	case UFCS_TEMP_RANGE_LITTLE_COOL:
		ret = oplus_ufcs_set_current_temp_little_cool_range(
			chip, vbat_temp_cur);
		break;
	case UFCS_TEMP_RANGE_COOL:
		ret = oplus_ufcs_set_current_temp_cool_range(chip,
							     vbat_temp_cur);
		break;
	case UFCS_TEMP_RANGE_LITTLE_COLD:
		ret = oplus_ufcs_set_current_temp_little_cold_range(
			chip, vbat_temp_cur);
		break;
	default:
		break;
	}

	ufcs_err("the ret: %d, the temp =%d, temp_status = %d, temp_range = "
		 "%d\r\n",
		 ret, vbat_temp_cur, chip->ufcs_fastchg_batt_temp_status,
		 chip->ufcs_temp_cur_range);

	chip->current_batt_temp = ret * oplus_ufcs_get_icurr_ratio();
	return ret;
}

static void oplus_ufcs_check_low_curr_temp_status(struct oplus_ufcs_chip *chip)
{
	static int t_cnts = 0;
	int vbat_temp_cur = 350;

	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	vbat_temp_cur = chip->ap_batt_temperature;

	if (((vbat_temp_cur >=
	      chip->limits.ufcs_low_curr_full_normal_high_temp) ||
	     (vbat_temp_cur < chip->limits.ufcs_low_curr_full_cool_temp)) &&
	    (chip->ufcs_low_curr_full_temp_status !=
	     UFCS_LOW_CURR_FULL_CURVE_TEMP_MAX)) {
		t_cnts++;
		if (t_cnts >= UFCS_TEMP_OVER_COUNTS) {
			chip->ufcs_low_curr_full_temp_status =
				UFCS_LOW_CURR_FULL_CURVE_TEMP_MAX;
			t_cnts = 0;
		}
	} else if ((vbat_temp_cur >=
		    chip->limits.ufcs_low_curr_full_normal_low_temp) &&
		   (chip->ufcs_low_curr_full_temp_status !=
		    UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH)) {
		t_cnts++;
		if (t_cnts >= UFCS_TEMP_OVER_COUNTS) {
			chip->ufcs_low_curr_full_temp_status =
				UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH;
			t_cnts = 0;
		}
	} else if ((vbat_temp_cur >=
		    chip->limits.ufcs_low_curr_full_little_cool_temp) &&
		   (chip->ufcs_low_curr_full_temp_status !=
		    UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW)) {
		t_cnts++;
		if (t_cnts >= UFCS_TEMP_OVER_COUNTS) {
			chip->ufcs_low_curr_full_temp_status =
				UFCS_LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW;
			t_cnts = 0;
		}
	} else if (chip->ufcs_low_curr_full_temp_status !=
		   UFCS_LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL) {
		t_cnts++;
		if (t_cnts >= UFCS_TEMP_OVER_COUNTS) {
			chip->ufcs_low_curr_full_temp_status =
				UFCS_LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL;
			t_cnts = 0;
		}
	} else {
		t_cnts = 0;
	}
	ufcs_err("[%d, %d, %d, %d, %d]\n", chip->ap_batt_current,
		 chip->ap_batt_volt, vbat_temp_cur,
		 chip->ufcs_low_curr_full_temp_status, t_cnts);
}

static int oplus_ufcs_get_batt_curve_curr(struct oplus_ufcs_chip *chip)
{
	int curve_volt_change = 0, curve_time_change = 0;
	static int curve_ov_count = 0;
	static unsigned int toal_curve_time = 0;
	static int curve_ov_entry_ibus = 0;
	int index = 0;
	if (chip->ap_batt_volt > oplus_ufcs_get_curve_vbat(chip)) {
		if (curve_ov_count > UFCS_CURVE_OV_CNT) {
			curve_volt_change = 1;
			curve_ov_count = 0;
		} else {
			curve_ov_count++;
			if (curve_ov_count == 1) {
				curve_ov_entry_ibus = chip->ask_charger_current;
			}
		}
	} else {
		curve_ov_count = 0;
	}

	if (oplus_ufcs_get_curve_time(chip) > 0) {
		if ((curve_volt_change) &&
		    (chip->timer.batt_curve_time <
		     (oplus_ufcs_get_curve_time(chip) + toal_curve_time))) {
			toal_curve_time = oplus_ufcs_get_curve_time(chip) +
					  toal_curve_time -
					  chip->timer.batt_curve_time;
		}

		if (chip->timer.batt_curve_time >
		    (oplus_ufcs_get_curve_time(chip) + toal_curve_time)) {
			curve_time_change = 1;
			toal_curve_time = 0;
		}
	} else {
		toal_curve_time = 0;
	}

	ufcs_err(" [%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d]\n",
		 curve_volt_change, curve_time_change, chip->batt_curve_index,
		 chip->ap_batt_volt, oplus_ufcs_get_curve_vbat(chip),
		 chip->ap_input_current, chip->ask_charger_current,
		 chip->timer.batt_curve_time, toal_curve_time,
		 oplus_ufcs_get_curve_time(chip));
	if ((curve_volt_change) || (curve_time_change)) {
		chip->batt_curve_index++;
		if (curve_volt_change) {
			for (index = chip->batt_curve_index;
			     index < chip->batt_curves.batt_curve_num - 1;
			     index++) {
				if ((chip->batt_curves.batt_curves[index - 1]
					     .target_vbus ==
				     chip->batt_curves.batt_curves[index]
					     .target_vbus) &&
				    (chip->batt_curves.batt_curves[index - 1]
					     .target_vbat ==
				     chip->batt_curves.batt_curves[index]
					     .target_vbat) &&
				    (curve_ov_entry_ibus <=
				     chip->batt_curves.batt_curves[index]
					     .target_ibus)) {
					ufcs_err("entry ibus =%d change "
						 "batt_curve_index from %d to "
						 "%d\n",
						 curve_ov_entry_ibus,
						 chip->batt_curve_index,
						 index + 1);
					chip->batt_curve_index = index + 1;
				} else {
					break;
				}
			}
		}
		chip->timer.batt_curve_time = 0;
		curve_volt_change = 0;
		curve_time_change = 0;
		if (chip->batt_curve_index >=
		    chip->batt_curves.batt_curve_num) {
			chip->batt_curve_index =
				chip->batt_curves.batt_curve_num - 1;
			chip->ufcs_stop_status = UFCS_STOP_VOTER_FULL;
		}
		chip->current_batt_curve =
			chip->batt_curves.batt_curves[chip->batt_curve_index]
				.target_ibus *
			oplus_ufcs_get_icurr_ratio();
		if ((chip->batt_curve_index > 0) &&
		    (oplus_ufcs_get_last_curve_vbat(chip) !=
		     oplus_ufcs_get_curve_vbat(chip)))
			oplus_ufcs_check_bcc_curr(chip);
		ufcs_err("ufcs_stop_status = %d, batt_curve_index = %d, "
			 "batt_curve_num = %d\n",
			 chip->ufcs_stop_status, chip->batt_curve_index,
			 chip->batt_curves.batt_curve_num);
	}

	return 0;
}

static const int ufcs_cool_down_third_curve[] = { 3000, 1000, 1000, 1200, 1500, 1700,
					     2000, 2200, 2500, 2700, 3000 };
static const int ufcs_cool_down_oplus_curve[] = {
	3000, 1000, 1000, 1200, 1500, 1700,  2000,  2200,  2500,  2700,  3000,
	3200, 3500, 3700, 4000, 4500, 5000,  5500,  6000,  6300,  6500,  7000,
	7500, 8000, 8500, 9000, 9500, 10000, 10500, 11000, 11500, 12000, 12500
};

static int oplus_ufcs_get_cool_down_curr(struct oplus_ufcs_chip *chip)
{
	int cool_down = 0;
	if (!chip || !chip->ufcs_support_type) {
		ufcs_err("g_ufcs_chip null!\n");
		return -ENODEV;
	}
	cool_down = oplus_chg_get_cool_down_status();
	if (!chip->ufcs_authentication) {
		if (cool_down >=
		    (sizeof(ufcs_cool_down_third_curve) / sizeof(int))) {
			cool_down =
				sizeof(ufcs_cool_down_third_curve) / sizeof(int) - 1;
		}
		chip->current_cool_down = ufcs_cool_down_third_curve[cool_down] *
					  oplus_ufcs_get_cp_ratio();
	} else {
		if (cool_down >=
		    (sizeof(ufcs_cool_down_oplus_curve) / sizeof(int))) {
			cool_down =
				sizeof(ufcs_cool_down_oplus_curve) / sizeof(int) - 1;
		}
		chip->current_cool_down = ufcs_cool_down_oplus_curve[cool_down];
	}
	return 0;
}

static int oplus_ufcs_check_ibus_curr(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		ufcs_err("oplus_ufcs_chip null!\n");
		return -ENODEV;
	}

	if (chip->ufcs_status <= OPLUS_UFCS_STATUS_OPEN_MOS) {
		return 0;
	}

	if (chip->charger_output_current > UFCS_CP_IBUS_MAX) {
		chip->count.ibus_over++;
		if (chip->count.ibus_over > UFCS_CP_IBUS_OVER_COUNTS) {
			chip->cp_ibus_down = OPLUS_UFCS_CURRENT_LIMIT_2A;
			chip->count.ibus_over = 0;
			oplus_ufcs_track_upload_err_info(
				chip, TRACK_UFCS_ERR_IBUS_LIMIT,
				chip->charger_output_current);
		}
	} else {
		chip->count.ibus_over = 0;
	}

	ufcs_err("charger_output_current = %d, cp_ibus_down = %d, ibus_over = "
		 "%d\n",
		 chip->charger_output_current, chip->cp_ibus_down,
		 chip->count.ibus_over);
	return 0;
}

static int oplus_ufcs_get_target_current(struct oplus_ufcs_chip *chip)
{
	int target_current_temp = 0;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		ufcs_err("g_ufcs_chip null!\n");
		return -ENODEV;
	}

	ufcs_err("[%d, %d, %d, %d, %d, %d]\n", chip->current_batt_curve,
		 chip->current_batt_temp, chip->current_cool_down,
		 chip->cp_ibus_down, chip->current_bcc, chip->cp_r_down);

	target_current_temp =
		chip->current_batt_curve < chip->current_batt_temp ?
			chip->current_batt_curve :
			chip->current_batt_temp;
	target_current_temp = target_current_temp < chip->current_cool_down ?
				      target_current_temp :
				      chip->current_cool_down;
	target_current_temp = target_current_temp < chip->cp_ibus_down ?
				      target_current_temp :
				      chip->cp_ibus_down;
	target_current_temp = target_current_temp < chip->current_bcc ?
				      target_current_temp :
				      chip->current_bcc;
	target_current_temp = target_current_temp < chip->cp_r_down ?
				      target_current_temp :
				      chip->cp_r_down;

	return target_current_temp;
}

static void oplus_ufcs_tick_timer(struct oplus_ufcs_chip *chip)
{
	struct timespec ts_current;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}

	ts_current = current_kernel_time();
	if (oplus_chg_get_bcc_support() &&
	    (ts_current.tv_sec - chip->timer.fastchg_timer.tv_sec) >=
		    UFCS_UPDATE_FASTCHG_TIME) {
		/*oplus_gauge_fastchg_update_bcc_parameters(buf);*/
	}

	if ((ts_current.tv_sec - chip->timer.fastchg_timer.tv_sec) >=
	    UFCS_UPDATE_FASTCHG_TIME) {
		chip->timer.fastchg_timer = ts_current;
		if (oplus_ufcs_get_curve_time(chip) > 0) {
			chip->timer.batt_curve_time++;
		}
		if (chip->timer.ufcs_timeout_time > 0) {
			chip->timer.ufcs_timeout_time--;
		}
	}

	if ((ts_current.tv_sec - chip->timer.pdo_timer.tv_sec) >=
	    UFCS_UPDATE_PDO_TIME) {
		chip->timer.pdo_timer = ts_current;
		chip->timer.set_pdo_flag = 1;
	}
	if ((ts_current.tv_sec - chip->timer.temp_timer.tv_sec) >=
	    UFCS_UPDATE_TEMP_TIME) {
		chip->timer.temp_timer = ts_current;
		chip->timer.set_temp_flag = 1;
	}

	if ((ts_current.tv_sec - chip->timer.ibat_timer.tv_sec) >=
	    UFCS_UPDATE_IBAT_TIME) {
		chip->timer.ibat_timer = ts_current;
		chip->timer.check_ibat_flag = 1;
	}
}

static void oplus_ufcs_check_temp(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}

	if (chip->ufcs_status <= OPLUS_UFCS_STATUS_OPEN_MOS) {
		return;
	}

	if (chip->timer.set_temp_flag) {
		if (chip->ops->ufcs_get_btb_temp_status() == false) {
			chip->count.btb_high++;
			if (chip->count.btb_high >= UFCS_BTB_OV_CNT) {
				chip->ufcs_stop_status =
					UFCS_STOP_VOTER_BTB_OVER;
				chip->count.btb_high = 0;
				oplus_ufcs_track_upload_err_info(
					chip, TRACK_UFCS_ERR_BTB_OVER, false);
			}
		} else {
			chip->count.btb_high = 0;
		}

		if (chip->ufcs_fastchg_batt_temp_status == UFCS_BAT_TEMP_EXIT) {
			ufcs_err("ufcs battery temp out of range!!!\n");
			chip->count.tbatt_over++;
			if (chip->count.tbatt_over >= UFCS_TBATT_OV_CNT) {
				chip->ufcs_stop_status =
					UFCS_STOP_VOTER_TBATT_OVER;
			}
		} else {
			chip->count.tbatt_over = 0;
		}

		if (chip->ap_fg_temperature >= UFCS_FG_TEMP_PROTECTION) {
			ufcs_err("ufcs tfg > 80!!!\n");
			chip->count.tfg_over++;
			if (chip->count.tfg_over >= UFCS_TFG_OV_CNT) {
				chip->ufcs_stop_status =
					UFCS_STOP_VOTER_TFG_OVER;
				chip->count.tfg_over = 0;
				oplus_ufcs_track_upload_err_info(
					chip, TRACK_UFCS_ERR_TFG_OVER,
					chip->ap_fg_temperature);
			}
		} else {
			chip->count.tfg_over = 0;
		}

		chip->timer.set_temp_flag = 0;
	}
	ufcs_err(" [%d, %d, %d, %d, %d, %d]\n", chip->timer.set_temp_flag,
		 chip->ap_batt_temperature, chip->ap_fg_temperature,
		 chip->count.btb_high, chip->count.tbatt_over,
		 chip->count.tfg_over);
}

static void oplus_ufcs_check_sw_full(struct oplus_ufcs_chip *chip)
{
	int cool_sw_vth = 0, normal_sw_vth = 0, normal_hw_vth = 0;

	if (!chip->ufcs_authentication) {
		cool_sw_vth = chip->limits.ufcs_full_cool_sw_vbat_third;
		normal_sw_vth = chip->limits.ufcs_full_normal_sw_vbat_third;
		normal_hw_vth = chip->limits.ufcs_full_normal_hw_vbat_third;
	} else {
		cool_sw_vth = chip->limits.ufcs_full_cool_sw_vbat;
		normal_sw_vth = chip->limits.ufcs_full_normal_sw_vbat;
		normal_hw_vth = chip->limits.ufcs_full_normal_hw_vbat;
	}

	if (chip->timer.ufcs_timeout_time == 0) {
		chip->ufcs_stop_status = UFCS_STOP_VOTER_TIME_OVER;
	}

	if ((chip->ap_batt_temperature < chip->limits.ufcs_cool_temp) &&
	    (chip->ap_batt_volt > cool_sw_vth)) {
		chip->count.cool_fw++;
		if (chip->count.cool_fw >= UFCS_FULL_COUNTS_COOL) {
			chip->count.cool_fw = 0;
			chip->ufcs_stop_status = UFCS_STOP_VOTER_FULL;
		}
	} else {
		chip->count.cool_fw = 0;
	}

	if ((chip->ap_batt_temperature >= chip->limits.ufcs_cool_temp) &&
	    (chip->ap_batt_temperature <
	     chip->limits.ufcs_batt_over_high_temp)) {
		if ((chip->ap_batt_volt > normal_sw_vth) &&
		    (chip->batt_curve_index ==
		     chip->batt_curves.batt_curve_num - 1)) {
			chip->count.sw_full++;
			if (chip->count.sw_full >= UFCS_FULL_COUNTS_SW) {
				chip->count.sw_full = 0;
				chip->ufcs_stop_status = UFCS_STOP_VOTER_FULL;
			}
		}

		if ((chip->ap_batt_volt > normal_hw_vth)) {
			chip->count.hw_full++;
			if (chip->count.hw_full >= UFCS_FULL_COUNTS_HW) {
				chip->count.hw_full = 0;
				chip->ufcs_stop_status = UFCS_STOP_VOTER_FULL;
			}
		}
	} else {
		chip->count.sw_full = 0;
		chip->count.hw_full = 0;
	}
	if ((chip->ufcs_fastchg_batt_temp_status == UFCS_BAT_TEMP_WARM) &&
	    (chip->ap_batt_volt > chip->limits.ufcs_full_warm_vbat)) {
		chip->count.cool_fw++;
		if (chip->count.cool_fw >= UFCS_FULL_COUNTS_COOL) {
			chip->count.cool_fw = 0;
			chip->ufcs_stop_status = UFCS_STOP_VOTER_FULL;
		}
	} else {
		chip->count.cool_fw = 0;
	}
	ufcs_err(" [%d, %d, %d, %d, %d, %d], [%d, %d, %d, %d, %d, %d]\n",
		 chip->count.cool_fw, chip->count.sw_full, chip->count.hw_full,
		 chip->ap_batt_volt, chip->ap_batt_temperature,
		 chip->timer.ufcs_timeout_time, chip->ufcs_stop_status,
		 cool_sw_vth, normal_sw_vth, normal_hw_vth,
		 chip->batt_curve_index, chip->batt_curves.batt_curve_num);
}

static void oplus_ufcs_check_low_curr_full(struct oplus_ufcs_chip *chip)
{
	int i, temp_current, temp_vbatt, temp_status, iterm, vterm;
	bool low_curr = false;

	oplus_ufcs_check_low_curr_temp_status(chip);

	temp_current = -chip->ap_batt_current;
	temp_vbatt = chip->ap_batt_volt;
	temp_status = chip->ufcs_low_curr_full_temp_status;

	if (temp_status == UFCS_LOW_CURR_FULL_CURVE_TEMP_MAX)
		return;

	for (i = 0;
	     i < chip->low_curr_full_curves_temp[temp_status].full_curve_num;
	     i++) {
		iterm = chip->low_curr_full_curves_temp[temp_status]
				.full_curves[i]
				.iterm;
		vterm = chip->low_curr_full_curves_temp[temp_status]
				.full_curves[i]
				.vterm;

		if ((temp_current <= iterm) && (temp_vbatt >= vterm)) {
			low_curr = true;
			break;
		}
	}

	if (low_curr) {
		chip->count.low_curr_full++;

		if (chip->count.low_curr_full > UFCS_FULL_COUNTS_LOW_CURR) {
			chip->count.low_curr_full = 0;
			chip->ufcs_stop_status = UFCS_STOP_VOTER_FULL;
		}
	} else {
		chip->count.low_curr_full = 0;
	}
}

int oplus_ufcs_get_ffc_vth(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return 4420;
	}
	return chip->limits.ufcs_full_ffc_vbat;
}

static void oplus_ufcs_check_full(struct oplus_ufcs_chip *chip)
{
	if (chip->ufcs_status == OPLUS_UFCS_STATUS_CHECK) {
		oplus_ufcs_check_sw_full(chip);
		oplus_ufcs_check_low_curr_full(chip);
	}
}

static void oplus_ufcs_check_ibat_safety(struct oplus_ufcs_chip *chip)
{
	int chg_ith = 0;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}

	if ((!chip->timer.check_ibat_flag) ||
	    (chip->ufcs_status <= OPLUS_UFCS_STATUS_OPEN_MOS)) {
		return;
	}

	chip->timer.check_ibat_flag = 0;
	if (chip->ap_batt_current > UFCS_IBAT_LOW_MIN) {
		chip->count.ibat_low++;
		if (chip->count.ibat_low >= UFCS_IBAT_LOW_CNT) {
			chip->ufcs_stop_status = UFCS_STOP_VOTER_IBAT_OVER;
		}
	} else {
		chip->count.ibat_low = 0;
	}

	if (!chip->ufcs_authentication)
		chg_ith = -chip->limits.ufcs_ibat_over_third;
	else
		chg_ith = -chip->limits.ufcs_ibat_over_oplus;
	if (chip->ap_batt_current < chg_ith) {
		chip->count.ibat_high++;
		if (chip->count.ibat_high >= UFCS_IBAT_HIGH_CNT) {
			chip->ufcs_stop_status = UFCS_STOP_VOTER_IBAT_OVER;
		}
	} else {
		chip->count.ibat_high = 0;
	}
	ufcs_err("[%d, %d, %d, %d]!\n", chip->ap_batt_current,
		 chip->count.ibat_low, chip->count.ibat_high,
		 chip->ufcs_stop_status);
}

static void oplus_ufcs_count_init(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	chip->count.cool_fw = 0;
	chip->count.sw_full = 0;
	chip->count.hw_full = 0;
	chip->count.low_curr_full = 0;
	chip->count.ibat_low = 0;
	chip->count.ibat_high = 0;
	chip->count.btb_high = 0;
	chip->count.tbatt_over = 0;
	chip->count.tfg_over = 0;
	chip->count.output_low = 0;
	chip->count.ibus_over = 0;
}

static void oplus_ufcs_r_init(struct oplus_ufcs_chip *chip)
{
	int i;
	if (!chip || !chip->ufcs_support_type) {
		return;
	}
	for (i = 0; i < UFCS_R_AVG_NUM; i++) {
		chip->r_column[i] = chip->r_default;
	}
	chip->r_avg = chip->r_default;
}

static struct oplus_ufcs_r_info oplus_ufcs_get_r_average(struct oplus_ufcs_r_info *ufcs_r)
{
	struct oplus_ufcs_r_info r_sum = { 0 };
	struct oplus_ufcs_r_info r_average = { 0 };
	int i;

	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return r_average;
	}

	for (i = 0; i < UFCS_R_AVG_NUM - 1; i++) {
		r_sum.r0 += chip->r_column[i].r0;
		r_sum.r1 += chip->r_column[i].r1;
		r_sum.r2 += chip->r_column[i].r2;
		r_sum.r3 += chip->r_column[i].r3;
		r_sum.r4 += chip->r_column[i].r4;
		r_sum.r5 += chip->r_column[i].r5;
		r_sum.r6 += chip->r_column[i].r6;

		chip->r_column[i].r0 = chip->r_column[i + 1].r0;
		chip->r_column[i].r1 = chip->r_column[i + 1].r1;
		chip->r_column[i].r2 = chip->r_column[i + 1].r2;
		chip->r_column[i].r3 = chip->r_column[i + 1].r3;
		chip->r_column[i].r4 = chip->r_column[i + 1].r4;
		chip->r_column[i].r5 = chip->r_column[i + 1].r5;
		chip->r_column[i].r6 = chip->r_column[i + 1].r6;
	}
	chip->r_column[i].r0 = ufcs_r->r0;
	chip->r_column[i].r1 = ufcs_r->r1;
	chip->r_column[i].r2 = ufcs_r->r2;
	chip->r_column[i].r3 = ufcs_r->r3;
	chip->r_column[i].r4 = ufcs_r->r4;
	chip->r_column[i].r5 = ufcs_r->r5;
	chip->r_column[i].r6 = ufcs_r->r6;

	r_sum.r0 += ufcs_r->r0;
	r_sum.r1 += ufcs_r->r1;
	r_sum.r2 += ufcs_r->r2;
	r_sum.r3 += ufcs_r->r3;
	r_sum.r4 += ufcs_r->r4;
	r_sum.r5 += ufcs_r->r5;
	r_sum.r6 += ufcs_r->r6;

	chip->r_avg.r0 = r_sum.r0 / UFCS_R_AVG_NUM;
	chip->r_avg.r1 = r_sum.r1 / UFCS_R_AVG_NUM;
	chip->r_avg.r2 = r_sum.r2 / UFCS_R_AVG_NUM;
	chip->r_avg.r3 = r_sum.r3 / UFCS_R_AVG_NUM;
	chip->r_avg.r4 = r_sum.r4 / UFCS_R_AVG_NUM;
	chip->r_avg.r5 = r_sum.r5 / UFCS_R_AVG_NUM;
	chip->r_avg.r6 = r_sum.r6 / UFCS_R_AVG_NUM;

	return chip->r_avg;
}

enum oplus_ufcs_r_cool_down_ilimit oplus_ufcs_get_cool_down_by_resistense(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	struct oplus_ufcs_r_info r_avg = chip->r_default;
	struct oplus_ufcs_r_info r_tmp = chip->r_default;
	int r0_result = 0, r1_result = 0, r2_result = 0, r3_result = 0,
	    r4_result = 0;
	int vout_chg = 0, iout_chg = 0, vac_master = 0, ibus_master = 0,
	    ibus_slave = 0;
	int vbus_master = 0, vbus_slave = 0, vout_master = 0, vout_slave = 0,
	    vbatt = 0;

	enum oplus_ufcs_r_cool_down_ilimit cool_down_target = UFCS_R_COOL_DOWN_NOLIMIT;
	enum oplus_ufcs_r_cool_down_ilimit cool_down_r0 = UFCS_R_COOL_DOWN_NOLIMIT,
				cool_down_r1 = UFCS_R_COOL_DOWN_NOLIMIT;
	enum oplus_ufcs_r_cool_down_ilimit cool_down_r2 = UFCS_R_COOL_DOWN_NOLIMIT,
				cool_down_r3 = UFCS_R_COOL_DOWN_NOLIMIT,
				cool_down_r4 = UFCS_R_COOL_DOWN_NOLIMIT;

	vout_chg = chip->charger_output_volt;
	iout_chg = chip->charger_output_current;
	vac_master = chip->cp_master_vac;
	ibus_master = chip->cp_master_ibus;
	ibus_slave = chip->cp_slave_ibus;
	vbus_master = chip->ap_input_volt;
	vbus_slave = chip->slave_input_volt;
	vout_master = chip->cp_master_vout;
	vout_slave = chip->cp_slave_vout;
	vbatt = chip->ap_batt_volt;
	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY ||
	    oplus_ufcs_get_support_type() == UFCS_SUPPORT_2CP ||
	    oplus_ufcs_get_support_type() == UFCS_SUPPORT_3CP) {
		if (ibus_master > UFCS_R_IBUS_MIN && vout_chg > UFCS_R_VBUS_MIN)
			r_tmp.r0 = (vout_chg - vac_master) * 1000 /
				   (ibus_master + ibus_slave);

		if (ibus_master > UFCS_R_IBUS_MIN &&
		    vout_chg > UFCS_R_VBUS_MIN) {
			r_tmp.r1 = (vac_master -
				    chip->rmos_mohm * (ibus_master) / 1000 -
				    vbus_master) *
				   1000 / (ibus_master);
			r_tmp.r3 = (vout_master -
				    oplus_chg_get_batt_num() * vbatt) *
				   1000 /
				   (ibus_master * oplus_ufcs_get_cp_ratio());
		}
		if (ibus_slave > UFCS_R_IBUS_MIN &&
		    vout_chg > UFCS_R_VBUS_MIN) {
			r_tmp.r2 =
				(vac_master - vbus_slave) * 1000 / (ibus_slave);
			r_tmp.r4 = (vout_slave -
				    oplus_chg_get_batt_num() * vbatt) *
				   1000 /
				   (ibus_slave * oplus_ufcs_get_cp_ratio());
		}
	} else if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_MCU ||
		   oplus_ufcs_get_support_type() == UFCS_SUPPORT_ADSP_VOOCPHY) {
		r_tmp.r0 = (vout_chg - vbus_master) * 1000 / (iout_chg);
		r_tmp.r3 = (vbus_master - oplus_chg_get_batt_num() * vbatt) *
			   1000 / (iout_chg);
	}

	r_avg = oplus_ufcs_get_r_average(&r_tmp);

	r0_result = r_avg.r0 - chip->r_default.r0;
	r1_result = r_avg.r1 - chip->r_default.r1;
	r2_result = r_avg.r2 - chip->r_default.r2;
	r3_result = r_avg.r3 - chip->r_default.r3;
	r4_result = r_avg.r4 - chip->r_default.r4;

	ufcs_err("[%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d] [%d, %d, %d, %d, "
		 "%d] [%d, %d, %d, %d, %d]",
		 r_avg.r0, r_avg.r1, r_avg.r2, r_avg.r3, r_avg.r4, r0_result,
		 r1_result, r2_result, r3_result, r4_result, ibus_master,
		 ibus_slave, vout_chg, iout_chg, vac_master, vbus_master,
		 vbus_slave, vout_master, vout_slave, vbatt);

	if (r0_result > chip->r_limit.limit_exit_mohm)
		cool_down_r0 = UFCS_R_COOL_DOWN_EXIT;
	else if (r0_result > chip->r_limit.limit_2a_mohm)
		cool_down_r0 = UFCS_R_COOL_DOWN_2A;
	else if (r0_result > chip->r_limit.limit_3a_mohm)
		cool_down_r0 = UFCS_R_COOL_DOWN_3A;

	if ((ibus_master + ibus_slave) < OPLUS_UFCS_CURRENT_LIMIT_2A) {
		return cool_down_r0;
	}

	if (r1_result > chip->r_limit.limit_exit_mohm)
		cool_down_r1 = UFCS_R_COOL_DOWN_EXIT;
	else if (r1_result > chip->r_limit.limit_2a_mohm)
		cool_down_r1 = UFCS_R_COOL_DOWN_2A;
	else if (r1_result > chip->r_limit.limit_3a_mohm)
		cool_down_r1 = UFCS_R_COOL_DOWN_3A;

	if (r2_result > chip->r_limit.limit_exit_mohm)
		cool_down_r2 = UFCS_R_COOL_DOWN_EXIT;
	else if (r2_result > chip->r_limit.limit_2a_mohm)
		cool_down_r2 = UFCS_R_COOL_DOWN_2A;
	else if (r2_result > chip->r_limit.limit_3a_mohm)
		cool_down_r2 = UFCS_R_COOL_DOWN_3A;

	if (r3_result > chip->r_limit.limit_exit_mohm)
		cool_down_r3 = UFCS_R_COOL_DOWN_EXIT;
	else if (r3_result > chip->r_limit.limit_2a_mohm)
		cool_down_r3 = UFCS_R_COOL_DOWN_2A;
	else if (r3_result > chip->r_limit.limit_3a_mohm)
		cool_down_r3 = UFCS_R_COOL_DOWN_3A;

	if (r4_result > chip->r_limit.limit_exit_mohm)
		cool_down_r4 = UFCS_R_COOL_DOWN_EXIT;
	else if (r4_result > chip->r_limit.limit_2a_mohm)
		cool_down_r4 = UFCS_R_COOL_DOWN_2A;
	else if (r4_result > chip->r_limit.limit_3a_mohm)
		cool_down_r4 = UFCS_R_COOL_DOWN_3A;

	cool_down_target = cool_down_r0;
	cool_down_target = cool_down_target > cool_down_r1 ? cool_down_target :
							     cool_down_r1;
	cool_down_target = cool_down_target > cool_down_r2 ? cool_down_target :
							     cool_down_r2;
	cool_down_target = cool_down_target > cool_down_r3 ? cool_down_target :
							     cool_down_r3;
	cool_down_target = cool_down_target > cool_down_r4 ? cool_down_target :
							     cool_down_r4;
	if ((ibus_master + ibus_slave) < OPLUS_UFCS_CURRENT_LIMIT_4A) {
		ufcs_err("!!!!![%d, %d, %d, %d, %d, %d]", cool_down_target,
			 cool_down_r0, cool_down_r1, cool_down_r2, cool_down_r3,
			 cool_down_r4);
		return cool_down_target;
	}
	return UFCS_R_COOL_DOWN_NOLIMIT;
}

static void oplus_ufcs_check_resistense(struct oplus_ufcs_chip *chip)
{
	enum oplus_ufcs_r_cool_down_ilimit r_cool_down = UFCS_R_COOL_DOWN_NOLIMIT;

	if (chip->ufcs_status != OPLUS_UFCS_STATUS_CHECK) {
		return;
	}

	r_cool_down = oplus_ufcs_get_cool_down_by_resistense();

	if (r_cool_down == UFCS_R_COOL_DOWN_EXIT)
		chip->ufcs_stop_status = UFCS_STOP_VOTER_RESISTENSE_OVER;
	else if (r_cool_down == UFCS_R_COOL_DOWN_2A)
		chip->cp_r_down = OPLUS_UFCS_CURRENT_LIMIT_2A;
	else
		chip->cp_r_down = OPLUS_UFCS_CURRENT_LIMIT_MAX;

	if (r_cool_down >= UFCS_R_COOL_DOWN_4A)
		oplus_ufcs_track_upload_err_info(
			chip, TRACK_UFCS_ERR_R_COOLDOWN, r_cool_down);

	ufcs_err("r_cool_down:%d, ufcs_stop_status:%d\n", r_cool_down,
		 chip->ufcs_stop_status);
}

static void oplus_ufcs_check_disconnect(struct oplus_ufcs_chip *chip)
{
	if (!chip->ufcs_authentication) {
	}

	if (chip->ufcs_status <= OPLUS_UFCS_STATUS_OPEN_MOS) {
		return;
	}

	if (chip->ufcs_status > OPLUS_UFCS_STATUS_OPEN_MOS) {
		if (chip->charger_output_current < UFCS_DISCONNECT_IOUT_MIN) {
			chip->count.output_low++;
			ufcs_err("charger_output_current:%d, "
				 "ufcs_disconnect_count:%d\n",
				 chip->charger_output_current,
				 chip->count.output_low);
			if (chip->count.output_low >=
			    UFCS_DISCONNECT_IOUT_CNT) {
				chip->ufcs_stop_status =
					UFCS_STOP_VOTER_DISCONNECT_OVER;
				chip->count.output_low = 0;
			}
		} else {
			chip->count.output_low = 0;
		}
	} else {
		chip->count.output_low = 0;
	}
}

static void oplus_ufcs_check_mmi(struct oplus_ufcs_chip *chip)
{
	if (chip->ufcs_status <= OPLUS_UFCS_STATUS_OPEN_MOS) {
		return;
	}

	if (!oplus_chg_get_mmi_value()) {
		chip->ufcs_stop_status = UFCS_STOP_VOTER_MMI_TEST;
	} else
		chip->ufcs_stop_status &= ~UFCS_STOP_VOTER_MMI_TEST;
}

static void oplus_ufcs_protection_check(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	oplus_ufcs_tick_timer(chip);
	oplus_ufcs_check_full(chip);
	oplus_ufcs_check_ibat_safety(chip);
	oplus_ufcs_check_temp(chip);
	oplus_ufcs_check_mmi(chip);

	oplus_ufcs_check_resistense(chip);
	oplus_ufcs_check_ibus_curr(chip);
	oplus_ufcs_check_disconnect(chip);
}

static int oplus_ufcs_get_ufcs_pdo_vmin(struct oplus_ufcs_chip *chip)
{
	int pdo_vmin = UFCS_VOL_CURVE_LMAX;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return pdo_vmin;
	}
	return pdo_vmin;
}

static int oplus_ufcs_action_status_start(struct oplus_ufcs_chip *chip)
{
	int update_size = 0, vbat = 0;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	vbat = chip->ap_batt_volt;

	if (oplus_ufcs_get_curve_vbus(chip) == UFCS_VOL_MAX_V2) {
		chip->target_charger_volt = ((vbat * 4) / 100) * 100 +
					    UFCS_ACTION_START_DIFF_VOLT_V2;
		chip->target_charger_current = UFCS_ACTION_CURR_MIN;
	} else if (oplus_ufcs_get_curve_vbus(chip) == UFCS_VOL_MAX_V1) {
		chip->target_charger_volt = ((vbat * 2) / 100) * 100 +
					    UFCS_ACTION_START_DIFF_VOLT_V1;
		chip->target_charger_current = UFCS_ACTION_CURR_MIN;
	} else {
		ufcs_err("Invalid argument!\n");
		chip->ufcs_stop_status = UFCS_STOP_VOTER_PDO_ERROR;
		return -EINVAL;
	}

	if (abs(chip->ap_input_volt - chip->target_charger_volt) >=
	    OPLUS_UFCS_VOLT_UPDATE_V5) {
		update_size = OPLUS_UFCS_VOLT_UPDATE_V5;
	} else if (abs(chip->ap_input_volt - chip->target_charger_volt) >=
		   OPLUS_UFCS_VOLT_UPDATE_V4) {
		update_size = OPLUS_UFCS_VOLT_UPDATE_V4;
	} else if (abs(chip->ap_input_volt - chip->target_charger_volt) >=
		   OPLUS_UFCS_VOLT_UPDATE_V3) {
		update_size = OPLUS_UFCS_VOLT_UPDATE_V3;
	} else if (abs(chip->ap_input_volt - chip->target_charger_volt) >=
		   OPLUS_UFCS_VOLT_UPDATE_V2) {
		update_size = OPLUS_UFCS_VOLT_UPDATE_V2;
	} else {
		update_size = OPLUS_UFCS_VOLT_UPDATE_V1;
	}

	chip->ask_charger_current = chip->target_charger_current;
	if (chip->ap_input_volt < chip->target_charger_volt) {
		chip->ask_charger_volt += update_size;
	} else if (chip->ap_input_volt > (chip->target_charger_volt +
					  UFCS_ACTION_START_DIFF_VOLT_V1)) {
		chip->ask_charger_volt -= update_size;
	} else {
		ufcs_err("ufcs chargering volt okay\n");
		chip->ufcs_status = OPLUS_UFCS_STATUS_OPEN_MOS;
		if (!chip->ufcs_authentication) {
			chip->ufcs_fastchg_started = true;
		} else {
			chip->ufcs_fastchg_started = true;
		}
		chip->ufcs_dummy_started = false;
		oplus_ufcs_psy_changed(chip);
	}

	chip->timer.work_delay = UFCS_ACTION_START_DELAY;
	return 0;
}

static int oplus_ufcs_action_open_mos(struct oplus_ufcs_chip *chip)
{
	int ret = 0;
	if (!chip) {
		return -ENODEV;
	}

	ret = oplus_ufcs_charging_enable_master(chip, true);

	chip->ufcs_status = OPLUS_UFCS_STATUS_VOLT_CHANGE;
	chip->timer.batt_curve_time = 0;
	chip->timer.work_delay = UFCS_ACTION_MOS_DELAY;

	return 0;
}

static int oplus_ufcs_action_volt_change(struct oplus_ufcs_chip *chip)
{
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		ufcs_err("g_ufcs_chip null!\n");
		return -ENODEV;
	}

	chip->target_charger_volt = oplus_ufcs_get_curve_vbus(chip);
	if (oplus_ufcs_get_curve_vbus(chip) == UFCS_VOL_MAX_V2) {
		chip->target_charger_current = UFCS_ACTION_CURR_MIN;
	} else if (oplus_ufcs_get_curve_vbus(chip) == UFCS_VOL_MAX_V1) {
		chip->target_charger_current = UFCS_ACTION_CURR_MIN;
	} else {
		ufcs_err("Invalid argument!\n");
		chip->ufcs_stop_status = UFCS_STOP_VOTER_PDO_ERROR;
		return -EINVAL;
	}

	chip->ask_charger_volt = chip->target_charger_volt;
	chip->ufcs_status = OPLUS_UFCS_STATUS_CUR_CHANGE;

	chip->timer.work_delay = UFCS_ACTION_VOLT_DELAY;

	return 0;
}

static int oplus_ufcs_action_curr_change(struct oplus_ufcs_chip *chip)
{
	int update_size = 0;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	oplus_ufcs_get_batt_curve_curr(chip);
	oplus_ufcs_get_cool_down_curr(chip);
	oplus_ufcs_get_batt_temp_curr(chip);

	chip->target_charger_volt = oplus_ufcs_get_curve_vbus(chip);
	chip->target_charger_current = oplus_ufcs_get_target_current(chip);

	if (abs(chip->ask_charger_current - chip->target_charger_current) >=
	    OPLUS_UFCS_CURR_UPDATE_V4) {
		update_size = OPLUS_UFCS_CURR_UPDATE_V4;
	} else {
		update_size = OPLUS_UFCS_CURR_UPDATE_V2;
	}

	chip->ask_charger_volt = chip->target_charger_volt;

	if ((chip->ask_charger_current > chip->target_charger_current) &&
	    ((chip->ask_charger_current - chip->target_charger_current) >=
	     update_size)) {
		chip->ask_charger_current -= update_size;
	} else if ((chip->target_charger_current > chip->ask_charger_current) &&
		   ((chip->target_charger_current -
		     chip->ask_charger_current) >= update_size)) {
		chip->ask_charger_current += update_size;
	} else {
		chip->ask_charger_current = chip->target_charger_current;
		ufcs_err("ufcs chargering current 2 okay\n");
		chip->ufcs_status = OPLUS_UFCS_STATUS_CHECK;
	}

	chip->timer.work_delay = UFCS_ACTION_CURR_DELAY;

	return 0;
}

static int oplus_ufcs_action_status_check(struct oplus_ufcs_chip *chip)
{
	static int curr_over_target_cnt = 0;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return -ENODEV;
	}

	oplus_ufcs_get_batt_curve_curr(chip);
	oplus_ufcs_get_cool_down_curr(chip);
	oplus_ufcs_get_batt_temp_curr(chip);

	chip->target_charger_volt = oplus_ufcs_get_curve_vbus(chip);
	chip->target_charger_current = oplus_ufcs_get_target_current(chip);

	if (chip->ask_charger_volt != chip->target_charger_volt) {
		chip->ask_charger_volt = chip->target_charger_volt;
		if (oplus_ufcs_get_curve_vbus(chip) == UFCS_VOL_MAX_V2) {
			chip->ask_charger_current = UFCS_ACTION_CURR_MIN;
		} else if (oplus_ufcs_get_curve_vbus(chip) == UFCS_VOL_MAX_V1) {
			chip->ask_charger_current = UFCS_ACTION_CURR_MIN;
		} else {
			chip->ufcs_stop_status = UFCS_STOP_VOTER_PDO_ERROR;
			ufcs_err("check curve Invalid argument!\n");
			return -EINVAL;
		}
		chip->ufcs_status = OPLUS_UFCS_STATUS_VOLT_CHANGE;
	} else {
		if (chip->batt_input_current > chip->target_charger_current) {
			curr_over_target_cnt++;
		} else {
			curr_over_target_cnt = 0;
		}

		if (chip->target_charger_current !=
			    chip->target_charger_current_pre ||
		    curr_over_target_cnt > UFCS_ACTION_CHECK_ICURR_CNT) {
			chip->ufcs_status = OPLUS_UFCS_STATUS_CUR_CHANGE;
			curr_over_target_cnt = 0;
		}
	}
	chip->timer.work_delay = UFCS_ACTION_CHECK_DELAY;

	return 0;
}

static int oplus_ufcs_action_check(struct oplus_ufcs_chip *chip)
{
	ufcs_err(" %d, %d, %d\n", chip->ufcs_status, chip->target_charger_volt,
		 chip->target_charger_current);
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		ufcs_err("g_ufcs_chip null!\n");
		return -ENODEV;
	}

	chip->target_charger_current_pre = chip->target_charger_current;

	switch (chip->ufcs_status) {
	case OPLUS_UFCS_STATUS_START:
		oplus_ufcs_action_status_start(chip);
		break;
	case OPLUS_UFCS_STATUS_OPEN_MOS:
		oplus_ufcs_action_open_mos(chip);
		break;
	case OPLUS_UFCS_STATUS_VOLT_CHANGE:
		oplus_ufcs_action_volt_change(chip);
		break;
	case OPLUS_UFCS_STATUS_CUR_CHANGE:
		oplus_ufcs_action_curr_change(chip);
		break;
	case OPLUS_UFCS_STATUS_CHECK:
		oplus_ufcs_action_status_check(chip);
		break;
	default:
		chip->ufcs_stop_status = UFCS_STOP_VOTER_OTHER_ABORMAL;
		ufcs_err("wrong status!\n");
		return -EINVAL;
	}

	if (chip->ask_charger_volt < oplus_ufcs_get_ufcs_pdo_vmin(chip)) {
		chip->ask_charger_volt = oplus_ufcs_get_ufcs_pdo_vmin(chip);
	}
	ufcs_err("[%d, %d, %d, %d, %d, %d, %d, %d]\n", chip->ufcs_status,
		 chip->target_charger_volt, chip->target_charger_current,
		 chip->ap_input_volt, chip->ask_charger_volt,
		 chip->ask_charger_current, chip->ap_batt_current,
		 chip->ap_batt_volt);

	return 0;
}

static int oplus_ufcs_set_pdo(struct oplus_ufcs_chip *chip)
{
	int ret = 0, max_cur = 0;

	if ((chip->ask_charger_volt != chip->ask_charger_volt_last) ||
	    (chip->ask_charger_current != chip->ask_charger_current_last)) {
		chip->ask_charger_volt_last = chip->ask_charger_volt;
		chip->ask_charger_current_last = chip->ask_charger_current;
		chip->timer.set_pdo_flag = 1;
	}

	max_cur = chip->ops->get_ufcs_max_cur(chip->ask_charger_volt);
	if (chip->ask_charger_current > max_cur) {
		ufcs_err("pdo cannot support %dmA,max current:%d\n",
			 chip->ask_charger_current, max_cur);
		chip->ask_charger_current = max_cur;
	}

	if (chip->timer.set_pdo_flag == 1) {
		if (oplus_chg_get_bcc_curr_done_status() ==
		    BCC_CURR_DONE_REQUEST) {
			oplus_chg_check_bcc_curr_done();
		}

		ret = chip->ops->ufcs_request_pdo(chip->ask_charger_volt,
						  chip->ask_charger_current);
		if (ret) {
			ufcs_err("ufcs set pdo fail\n");
			chip->ufcs_stop_status = UFCS_STOP_VOTER_PDO_ERROR;
			return -EINVAL;
		}
		chip->timer.set_pdo_flag = 0;
	}

	ufcs_err(" ask_volt, ask_cur : %d, %d\n", chip->ask_charger_volt,
		 chip->ask_charger_current);

	return ret;
}

static void oplus_ufcs_voter_charging_stop(struct oplus_ufcs_chip *chip)
{
	int stop_voter = chip->ufcs_stop_status;
	ufcs_err(",ufcs_stop_status = %d\n", stop_voter);

	switch (stop_voter) {
	case UFCS_STOP_VOTER_USB_TEMP:
	case UFCS_STOP_VOTER_DISCONNECT_OVER:
		oplus_chg_set_charger_type_unknown();
		mod_delayed_work(system_highpri_wq, &chip->ufcs_stop_work, 0);
		break;
	case UFCS_STOP_VOTER_FULL:
	case UFCS_STOP_VOTER_TIME_OVER:
	case UFCS_STOP_VOTER_PDO_ERROR:
	case UFCS_STOP_VOTER_TBATT_OVER:
	case UFCS_STOP_VOTER_IBAT_OVER:
	case UFCS_STOP_VOTER_RESISTENSE_OVER:
	case UFCS_STOP_VOTER_VBATT_DIFF:
	case UFCS_STOP_VOTER_SOURCE_EXIT:
	case UFCS_STOP_VOTER_OTHER_ABORMAL:
	case UFCS_STOP_VOTER_BTB_OVER:
	case UFCS_STOP_VOTER_MMI_TEST:
	case UFCS_STOP_VOTER_TFG_OVER:
		mod_delayed_work(system_highpri_wq, &chip->ufcs_stop_work, 0);
		break;
	default:
		break;
	}
}

void oplus_ufcs_source_exit(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}
	chip->ufcs_stop_status = UFCS_STOP_VOTER_SOURCE_EXIT;
	oplus_ufcs_voter_charging_stop(chip);
}

void oplus_ufcs_stop_usb_temp(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}
	chip->ufcs_stop_status = UFCS_STOP_VOTER_USB_TEMP;
	oplus_ufcs_voter_charging_stop(chip);
	oplus_ufcs_track_upload_err_info(chip, TRACK_UFCS_ERR_TFG_OVER, 0);
}

void oplus_ufcs_set_vbatt_diff(bool diff)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}

	if (diff)
		chip->ufcs_stop_status = UFCS_STOP_VOTER_VBATT_DIFF;
	else
		chip->ufcs_stop_status &= ~UFCS_STOP_VOTER_VBATT_DIFF;
}

static bool oplus_ufcs_voter_charging_start(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int stop_voter = chip->ufcs_stop_status;
	bool restart_voter = false;
	if (!chip || !chip->ufcs_support_type) {
		return restart_voter;
	}

	switch (stop_voter) {
	case UFCS_STOP_VOTER_TBATT_OVER:
		if (oplus_ufcs_is_allow_real()) {
			restart_voter = true;
			chip->ufcs_stop_status &= ~UFCS_STOP_VOTER_TBATT_OVER;
		}
		break;
	case UFCS_STOP_VOTER_MMI_TEST:
		if (oplus_chg_get_mmi_value()) {
			restart_voter = true;
			chip->ufcs_stop_status &= ~UFCS_STOP_VOTER_MMI_TEST;
		}
		break;
	default:
		break;
	}
	ufcs_err(" stop_voter = %d, restart_voter = %d\n", stop_voter,
		 restart_voter);
	return restart_voter;
}

static int oplus_ufcs_psy_changed(struct oplus_ufcs_chip *chip)
{
	if (chip->ufcs_batt_psy) {
		power_supply_changed(chip->ufcs_batt_psy);
	}
	return 0;
}

int oplus_ufcs_get_ffc_started(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return false;
	}
	if (chip->ufcs_status == OPLUS_UFCS_STATUS_FFC)
		return true;
	else
		return false;
}

int oplus_ufcs_get_ufcs_mos_started(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return false;
	}
	if (chip->ufcs_status >= OPLUS_UFCS_STATUS_OPEN_MOS)
		return true;
	else
		return false;
}

void oplus_ufcs_set_ufcs_dummy_started(bool enable)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}

	if (enable) {
		chip->ufcs_dummy_started = true;
	} else {
		chip->ufcs_dummy_started = false;
	}
	ufcs_err(" enable = %d\n", enable);
}

bool oplus_ufcs_get_ufcs_dummy_started(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return false;
	} else {
		return chip->ufcs_dummy_started;
	}
}

bool oplus_ufcs_get_ufcs_fastchg_started(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return false;
	} else {
		return chip->ufcs_fastchg_started;
	}
}

bool oplus_ufcs_get_force_check_status(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return true;
	} else {
		return chip->ufcs_force_check;
	}
}

static bool oplus_ufcs_get_ufcs_ping(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return false;
	} else {
		return chip->ufcs_ping;
	}
}

static bool oplus_ufcs_check_uct_mode(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	bool uct_mode = false;

	if (!chip || !chip->ops || !chip->ops->ufcs_check_uct_mode) {
		return false;
	}
	uct_mode = chip->ops->ufcs_check_uct_mode();
	return uct_mode;
}

static bool oplus_ufcs_check_quit_uct_mode(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	bool quit_uct = false;

	if (!chip || !chip->ops || !chip->ops->ufcs_quit_uct_mode) {
		return false;
	}
	quit_uct = chip->ops->ufcs_quit_uct_mode();
	return quit_uct;
}

static bool oplus_ufcs_check_oplus_id(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	bool oplus_id = false;

	if (!chip || !chip->ops || !chip->ops->ufcs_check_oplus_id) {
		return false;
	}
	oplus_id = chip->ops->ufcs_check_oplus_id();
	return oplus_id;
}

static bool oplus_ufcs_check_abnormal_id(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	bool abnormal_id = false;

	if (!chip || !chip->ops || !chip->ops->ufcs_check_abnormal_id) {
		return false;
	}
	abnormal_id = chip->ops->ufcs_check_abnormal_id();
	return abnormal_id;
}

int oplus_ufcs_get_stop_status(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return UFCS_STOP_VOTER_NONE;
	}

	return chip->ufcs_stop_status;
}

int oplus_ufcs_set_ffc_started(bool status)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return 0;
	}
	if (!status)
		chip->ufcs_status = OPLUS_UFCS_STATUS_START;
	else
		chip->ufcs_status = OPLUS_UFCS_STATUS_FFC;
	return 0;
}

int __attribute__((weak)) oplus_adsp_reset_voocphy(void)
{
	return 0;
}

void oplus_ufcs_restart_check(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}

	if (oplus_ufcs_get_chg_status() == UFCS_CHARGE_END &&
	    oplus_ufcs_get_ufcs_ping() && oplus_ufcs_voter_charging_start()) {
		if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
			oplus_adsp_voocphy_turn_off();
		} else if (oplus_chg_get_voocphy_support() ==
				   AP_SINGLE_CP_VOOCPHY ||
			   oplus_chg_get_voocphy_support() ==
				   AP_DUAL_CP_VOOCPHY) {
			oplus_voocphy_reset_voocphy();
			oplus_chg_suspend_charger();
			msleep(1000);
			oplus_chg_unsuspend_charger();
		}
		schedule_delayed_work(&chip->ufcs_start_work, 0);
	}
}

static void oplus_ufcs_update_work(struct work_struct *work)
{
	struct oplus_ufcs_chip *chip = container_of(
		work, struct oplus_ufcs_chip, update_ufcs_work.work);

	if (oplus_ufcs_get_chg_status() == UFCS_CHARGERING) {
		oplus_ufcs_get_charging_data(chip);
		oplus_ufcs_action_check(chip);
		oplus_ufcs_protection_check(chip);
		oplus_ufcs_set_pdo(chip);

		oplus_ufcs_voter_charging_stop(chip);

		if (chip->ufcs_stop_status == UFCS_STOP_VOTER_NONE)
			schedule_delayed_work(
				&chip->update_ufcs_work,
				msecs_to_jiffies(chip->timer.work_delay));
	} else {
		chip->ufcs_stop_status = UFCS_STOP_VOTER_TYPE_ERROR;
		schedule_delayed_work(&chip->ufcs_stop_work, 0);
	}
}

static void oplus_ufcs_cancel_update_work_sync(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return;
	}
	cancel_delayed_work_sync(&chip->update_ufcs_work);
}

static void oplus_ufcs_cancel_stop_work(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return;
	}
	cancel_delayed_work(&chip->ufcs_stop_work);
}

static void oplus_ufcs_stop_work(struct work_struct *work)
{
	struct oplus_ufcs_chip *chip =
		container_of(work, struct oplus_ufcs_chip, ufcs_stop_work.work);

	if (oplus_ufcs_get_chg_status() == UFCS_CHARGE_END ||
	    oplus_ufcs_get_chg_status() == UFCS_NOT_SUPPORT) {
		return;
	} else {
		oplus_ufcs_set_chg_status(UFCS_CHARGE_END);
	}
	ufcs_err(" ufcs_stop_status = %d \n", chip->ufcs_stop_status);
	oplus_ufcs_cancel_update_work_sync();

	/*if (chip->ufcs_authentication)*/
	chip->ufcs_dummy_started = true;
	chip->ufcs_fastchg_started = false;

	oplus_ufcs_ufcs_exit();
	msleep(UFCS_STOP_DELAY_TIME);
	oplus_ufcs_charging_enable_master(chip, false);
	oplus_ufcs_cp_mode_init(UFCS_BYPASS_MODE);
	if (chip->ufcs_stop_status == UFCS_STOP_VOTER_MMI_TEST) {
		chip->ufcs_status = OPLUS_UFCS_STATUS_START;
	} else if (chip->ufcs_stop_status == UFCS_STOP_VOTER_FULL) {
		oplus_chg_unsuspend_charger();
		oplus_chg_disable_charge();
		chip->ufcs_status = OPLUS_UFCS_STATUS_FFC;
	} else {
		oplus_chg_unsuspend_charger();
		oplus_chg_enable_charge();
		chip->ufcs_status = OPLUS_UFCS_STATUS_START;
	}

	ufcs_cpufreq_release();
	oplus_ufcs_pm_qos_update(PM_QOS_DEFAULT_VALUE);
	oplus_chg_wake_update_work();
	oplus_ufcs_cancel_stop_work();
}

static void oplus_ufcs_uct_work(struct work_struct *work)
{
	struct oplus_ufcs_chip *chip =
		container_of(work, struct oplus_ufcs_chip, ufcs_uct_work.work);
	bool quit = false, uct = false;

	if (!chip || !chip->ufcs_support_type) {
		return;
	}
	quit = oplus_ufcs_check_quit_uct_mode();
	uct = oplus_ufcs_check_uct_mode();
	chip->ap_batt_soc = oplus_chg_get_ui_soc();
	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();
	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY) {
		chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
	} else {
		chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
	}
	chip->ap_batt_current = oplus_gauge_get_batt_current();
	if (oplus_ufcs_check_quit_uct_mode() || !oplus_ufcs_check_uct_mode()) {
		oplus_ufcs_error_log();
		oplus_ufcs_ufcs_exit();
		ufcs_cpufreq_release();
		oplus_ufcs_pm_qos_update(PM_QOS_DEFAULT_VALUE);

		oplus_ufcs_set_chg_status(UFCS_CHARGE_END);

		chip->ufcs_dummy_started = false;
		chip->ufcs_fastchg_started = false;
	} else {
		oplus_ufcs_sink_data();
		schedule_delayed_work(&chip->ufcs_uct_work, UFCS_UCT_TIME);
	}

	ufcs_err(" [%d %d %d %d %d %d]\n", chip->ap_batt_soc,
		 chip->ap_batt_temperature, chip->ap_batt_volt,
		 chip->ap_batt_current, quit, uct);
}

static void oplus_ufcs_modify_cpufeq_work(struct work_struct *work)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	if (atomic_read(&chip->ufcs_freq_state) == 1)
		ppm_sys_boost_min_cpu_freq_set(
			chip->ufcs_freq_mincore, chip->ufcs_freq_midcore,
			chip->ufcs_freq_maxcore,
			chip->ufcs_current_change_timeout);
	else
		ppm_sys_boost_min_cpu_freq_clear();
}

static bool oplus_ufcs_check_adapter_status(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	int ret = 0;

	if (!chip || !chip->ops || !chip->ops->ufcs_chip_enable ||
	    !chip->ops->ufcs_handshake || !chip->ops->ufcs_ping) {
		return false;
	}

	ret = chip->ops->ufcs_chip_enable();
	if (ret < 0) {
		return false;
	}
	ret = chip->ops->ufcs_handshake();
	if (ret < 0) {
		return false;
	}
	ret = chip->ops->ufcs_ping();
	if (ret < 0) {
		return false;
	}
	chip->ufcs_ping = true;
	return true;
}

static bool oplus_ufcs_wdt_config(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	int ret = 0;

	if (!chip || !chip->ops || !chip->ops->ufcs_config_watchdog) {
		return false;
	}

	ret = chip->ops->ufcs_config_watchdog(UFCS_WDT_TIMER);
	if (ret < 0) {
		return false;
	}

	return true;
}

int oplus_ufcs_get_fastchg_type(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return UFCS_FASTCHG_TYPE_UNKOWN;
	}

	return chip->ufcs_fastchg_type;
}

static int oplus_ufcs_convert_fastchg_type(int fast_chg_type)
{
	int ret_fast_chg = UFCS_FASTCHG_TYPE_UNKOWN;

	switch ((fast_chg_type & UFCS_FASTCHG_TYPE_MASK) >>
		UFCS_FASTCHG_TYPE_SHIFT) {
	case UFCS_POWER_TYPE_V1:
		ret_fast_chg = UFCS_FASTCHG_TYPE_V1;
		break;
	case UFCS_POWER_TYPE_UNKOWN:
		ret_fast_chg = UFCS_FASTCHG_TYPE_V1;
		break;
	default:
		ret_fast_chg = UFCS_FASTCHG_TYPE_V1;
		break;
	}
	return ret_fast_chg;
}

static bool oplus_ufcs_get_adapter_version(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	int adapter_id = 0;

	if (!chip || !chip->ops || !chip->ops->get_adapter_id) {
		return false;
	}
	adapter_id = chip->ops->get_adapter_id();
	if (adapter_id < 0 || !oplus_ufcs_check_oplus_id()) {
		chip->ufcs_fastchg_type = UFCS_FASTCHG_TYPE_THIRD;
		return false;
	} else {
		chip->ufcs_fastchg_type =
			oplus_ufcs_convert_fastchg_type(adapter_id);
	}
	return true;
}

static int oplus_ufcs_get_encrypt_data(unsigned char random_a[16],
				unsigned char random_b[16],
				unsigned char auth_msg[16],
				unsigned char encrypt_data[32])
{
	unsigned char i, j;
	unsigned int a, b, c, d, e, f, g, h, t1, t2, m[64];

	for (i = 0; i < 16; i++) {
		ufcs_source_data[i] = random_a[i];
	}

	for (i = 0; i < 16; i++) {
		ufcs_source_data[32 + i] = random_b[i];
	}

	for (i = 0; i < 16; i++) {
		ufcs_source_data[16 + i] = auth_msg[i];
	}

	for (i = 0, j = 0; i < 16; ++i, j += 4) {
		m[i] = (ufcs_source_data[j] << 24) |
		       (ufcs_source_data[j + 1] << 16) |
		       (ufcs_source_data[j + 2] << 8) |
		       (ufcs_source_data[j + 3]);
	}

	for (i = 16; i < 64; ++i) {
		m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
	}

	a = UFCS_ENCRYPTION_HASH_A;
	b = UFCS_ENCRYPTION_HASH_B;
	c = UFCS_ENCRYPTION_HASH_C;
	d = UFCS_ENCRYPTION_HASH_D;
	e = UFCS_ENCRYPTION_HASH_E;
	f = UFCS_ENCRYPTION_HASH_F;
	g = UFCS_ENCRYPTION_HASH_G;
	h = UFCS_ENCRYPTION_HASH_H;

	for (i = 0; i < 64; ++i) {
		t1 = h + EP1(e) + CH(e, f, g) + ufcs_verify[i] + m[i];
		t2 = EP0(a) + MAJ(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	a += UFCS_ENCRYPTION_HASH_A;
	b += UFCS_ENCRYPTION_HASH_B;
	c += UFCS_ENCRYPTION_HASH_C;
	d += UFCS_ENCRYPTION_HASH_D;
	e += UFCS_ENCRYPTION_HASH_E;
	f += UFCS_ENCRYPTION_HASH_F;
	g += UFCS_ENCRYPTION_HASH_G;
	h += UFCS_ENCRYPTION_HASH_H;

	for (i = 0; i < 4; ++i) {
		encrypt_data[i] = (a >> (24 - i * 8)) & 0x000000ff;
		encrypt_data[i + 4] = (b >> (24 - i * 8)) & 0x000000ff;
		encrypt_data[i + 8] = (c >> (24 - i * 8)) & 0x000000ff;
		encrypt_data[i + 12] = (d >> (24 - i * 8)) & 0x000000ff;
		encrypt_data[i + 16] = (e >> (24 - i * 8)) & 0x000000ff;
		encrypt_data[i + 20] = (f >> (24 - i * 8)) & 0x000000ff;
		encrypt_data[i + 24] = (g >> (24 - i * 8)) & 0x000000ff;
		encrypt_data[i + 28] = (h >> (24 - i * 8)) & 0x000000ff;
	}

	return 0;
}

static bool oplus_ufcs_check_authen_data(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	int i, ret = 0;

	struct oplus_ufcs_encry_array *encry_array_adapter;
	unsigned char random_a[UFCS_AUTH_MSG_LEN], random_b[UFCS_AUTH_MSG_LEN],
		encrypt_data[UFCS_ENCRYP_DATA_LEN] = { 0 };

	if (!chip || !chip->ops || !chip->ops->ufcs_adapter_encryption) {
		ufcs_err(", chip null!\n");
		return false;
	}

	encry_array_adapter =
		kzalloc(sizeof(struct oplus_ufcs_encry_array), GFP_KERNEL);
	if (!encry_array_adapter)
		return false;

	for (i = 0; i < UFCS_AUTH_MSG_LEN; i++) {
		/*get_random_bytes_wait(&(encry_array_adapter->phone_random[i]), 1);*/
		get_random_bytes(&(encry_array_adapter->phone_random[i]), 1);
	}

	ret = chip->ops->ufcs_adapter_encryption(encry_array_adapter);
	if (ret < 0) {
		return true;
	}

	memcpy(random_a, encry_array_adapter->adapter_random, UFCS_AUTH_MSG_LEN);
	memcpy(random_b, encry_array_adapter->phone_random, UFCS_AUTH_MSG_LEN);
	oplus_ufcs_get_encrypt_data(random_a, random_b, ufcs_auth_msg,
				    encrypt_data);
	for (i = 0; i < UFCS_ENCRYP_DATA_LEN; i++) {
		ufcs_err(" encrypt_data[%d] = 0x%x\n", i, encrypt_data[i]);
		if (encrypt_data[i] !=
		    encry_array_adapter->adapter_encry_result[i])
			return false;
	}
	return true;
}

bool oplus_ufcs_get_src_cap(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	struct oplus_ufcs_src_cap *src_cap;
	int i = 0, ret = 0;
	bool vbus_ok = false;

	if (!chip || !chip->ops || !chip->ops->ufcs_get_src_cap) {
		return false;
	}

	src_cap = kzalloc(sizeof(struct oplus_ufcs_src_cap), GFP_KERNEL);
	if (!src_cap)
		return false;

	ret = chip->ops->ufcs_get_src_cap(src_cap);
	if (ret < 0) {
		kfree(src_cap);
		return false;
	}

	for (i = 0; i < src_cap->cap_num; i++) {
		ufcs_err("i = %d, [%d, %d, %d, %d]\n", i, src_cap->max_volt[i],
			 src_cap->min_volt[i], src_cap->max_curr[i],
			 src_cap->min_curr[i]);
		if ((src_cap->max_volt[i] != src_cap->min_volt[i]) &&
		    (src_cap->max_volt[i] >= UFCS_VOL_CURVE_HMIN)) {
			chip->ufcs_imax = src_cap->max_curr[i];
			chip->ufcs_vmax = src_cap->max_volt[i];
			vbus_ok = true;
		}
	}
	kfree(src_cap);
	return vbus_ok;
}

static void oplus_ufcs_start_work(struct work_struct *work)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip) {
		goto fail;
	}
	ufcs_cpufreq_init();
	oplus_ufcs_pm_qos_update(UFCS_QOS_UP_VALUE);
	oplus_ufcs_set_chg_status(UFCS_CHARGERING);

	if (oplus_ufcs_check_adapter_status() == false) {
		goto fail;
	}

	oplus_ufcs_get_adapter_version();
	oplus_ufcs_check_power();
	oplus_ufcs_wdt_config();

	if (oplus_ufcs_get_src_cap() == false) {
		goto fail;
	}

	if (oplus_ufcs_check_oplus_id()) {
		goto fail;
	}

	if (oplus_ufcs_check_authen_data() == false) {
		chip->ufcs_authentication = false;
	} else {
		chip->ufcs_authentication = true;
	}

	if (oplus_ufcs_get_src_cap() == false) {
		goto fail;
	}
	if (oplus_ufcs_variables_init())
		goto fail;

	ufcs_cpufreq_release();
	schedule_delayed_work(&chip->update_ufcs_work, 0);
	return;

fail:
	oplus_ufcs_variables_reset(true);
	if (chip->ufcs_ping) {
		oplus_ufcs_set_chg_status(UFCS_CHARGE_END);
		chip->ufcs_dummy_started = true;
		chip->ufcs_fastchg_started = false;
	} else {
		chip->ufcs_dummy_started = false;
		chip->ufcs_fastchg_started = false;
	}
	oplus_ufcs_error_log();
	oplus_ufcs_ufcs_exit();
	ufcs_cpufreq_release();
	oplus_ufcs_pm_qos_update(PM_QOS_DEFAULT_VALUE);
}

bool oplus_ufcs_force_ufcs_check(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip) {
		return false;
	}
	ufcs_cpufreq_init();
	oplus_ufcs_pm_qos_update(UFCS_QOS_UP_VALUE);
	oplus_ufcs_set_chg_status(UFCS_CHARGERING);

	if (oplus_ufcs_check_adapter_status() == false) {
		goto fail;
	}

	oplus_ufcs_get_adapter_version();
	oplus_ufcs_check_power();

	if (oplus_ufcs_check_uct_mode()) {
		chip->ufcs_fastchg_started = true;
		chip->ufcs_dummy_started = false;
		chip->ufcs_force_check = true;
		schedule_delayed_work(&chip->ufcs_uct_work, 0);
		return true;
	}

	oplus_ufcs_wdt_config();

	if (oplus_ufcs_get_src_cap() == false) {
		goto fail;
	}

	if (oplus_ufcs_check_abnormal_id()) {
		goto fail;
	}

	if (oplus_ufcs_check_oplus_id()) {
		goto fail;
	}

	if (!oplus_ufcs_is_allow_real()) {
		chip->ufcs_stop_status = UFCS_STOP_VOTER_TBATT_OVER;
		goto fail;
	}

	if (oplus_ufcs_check_authen_data() == false) {
		chip->ufcs_authentication = false;
	} else {
		chip->ufcs_authentication = true;
	}

	if (oplus_ufcs_get_src_cap() == false) {
		goto fail;
	}

	if (oplus_ufcs_variables_init())
		goto fail;

	ufcs_cpufreq_release();
	schedule_delayed_work(&chip->update_ufcs_work, 0);
	return true;

fail:
	chip->ufcs_force_check = true;
	oplus_ufcs_variables_reset(true);
	if (chip->ufcs_ping) {
		oplus_ufcs_set_chg_status(UFCS_CHARGE_END);
		chip->ufcs_dummy_started = true;
		chip->ufcs_fastchg_started = false;
	} else {
		chip->ufcs_dummy_started = false;
		chip->ufcs_fastchg_started = false;
	}
	oplus_ufcs_error_log();
	oplus_ufcs_ufcs_exit();
	oplus_ufcs_delay_exit();
	ufcs_cpufreq_release();
	oplus_ufcs_pm_qos_update(PM_QOS_DEFAULT_VALUE);
	return chip->ufcs_dummy_started;
}

int oplus_ufcs_get_chg_status(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int ret;

	if (!chip) {
		return UFCS_NOT_SUPPORT;
	}

	if (chip->ufcs_support_type == 0) {
		return UFCS_NOT_SUPPORT;
	}
	ret = chip->ufcs_chging;
	return ret;
}

int oplus_ufcs_get_support_type(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip) {
		return UFCS_SUPPORT_NOT;
	}

	if (chip->ufcs_support_type == 1) {
		return UFCS_SUPPORT_MCU;
	} else if (chip->ufcs_support_type == 2) {
		return UFCS_SUPPORT_2CP;
	} else if (chip->ufcs_support_type == 3) {
		return UFCS_SUPPORT_3CP;
	} else if (chip->ufcs_support_type == 4) {
		return UFCS_SUPPORT_AP_VOOCPHY;
	} else if (chip->ufcs_support_type == 5) {
		return UFCS_SUPPORT_ADSP_VOOCPHY;
	} else {
		return UFCS_SUPPORT_NOT;
	}
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
static bool oplus_ufcs_node_init_auth_msg(u8 *auth_msg)
{
	int ret = 0, i = 0;
	char oplus_ufcs_offset[64] = { 0 };
	struct device_node *np = NULL;

	np = of_find_node_by_name(NULL, "oplus_ufcs");
	if (np) {
		for (i = 0; i < UFCS_AUTH_MSG_LEN; i++) {
			sprintf(oplus_ufcs_offset, "ufcs_key_%d", i);
			ret = of_property_read_u8(np, oplus_ufcs_offset,
						  &(auth_msg[i]));
			if (ret) {
				ufcs_err(" ufcs_key_[%x]: %s %d fail,ret = %d",
					 i, oplus_ufcs_offset, auth_msg[i],
					 ret);
			}
		}
		ufcs_err(" OK!\n");
	}
	return true;
}
#else
static bool oplus_ufcs_smem_init_auth_msg(u8 *auth_msg)
{
	size_t smem_size;
	void *smem_addr;
	oplus_ufcs_auth_info_type *smem_data, *chg_smem_info;

	chg_smem_info = kzalloc(sizeof(oplus_ufcs_auth_info_type), GFP_KERNEL);
	if (!chg_smem_info)
		return false;

	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
				  UFCS_SMEM_RESERVED_BOOT_INFO_FOR_APPS,
				  &smem_size);
	if (IS_ERR(smem_addr)) {
		ufcs_err("unable to acquire smem "
			 "SMEM_RESERVED_BOOT_INFO_FOR_APPS entry\n");
		return false;
	} else {
		smem_data = (oplus_ufcs_auth_info_type *)smem_addr;
		if (smem_data == ERR_PTR(-EPROBE_DEFER)) {
			smem_data = NULL;
			ufcs_err("fail to get smem_data\n");
			return false;
		} else {
			memcpy(chg_smem_info, smem_data,
			       sizeof(oplus_ufcs_auth_info_type));
		}
	}
	memcpy(auth_msg, chg_smem_info->ufcs_k0_msg, UFCS_AUTH_MSG_LEN);
	return true;
}

#endif

static void oplus_ufcs_init_auth_msg(void)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	oplus_ufcs_node_init_auth_msg(ufcs_auth_msg);
#else
	oplus_ufcs_smem_init_auth_msg(ufcs_auth_msg);
#endif
}

int oplus_ufcs_set_chg_status(int status)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return 0;
	}
	ufcs_err(":%d\n", status);

	chip->ufcs_chging = status;
	return 0;
}

void oplus_ufcs_clear_dbg_info(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	chip->ufcs_iic_err = 0;
	chip->ufcs_iic_err_num = 0;
	memset(chip->int_column, 0, UFCS_DUMP_REG_CNT);
}

void oplus_ufcs_variables_reset(bool in)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return;
	}
	if (!in || oplus_ufcs_check_oplus_id()) {
		chip->ufcs_ping = false;
		chip->ufcs_fastchg_type = UFCS_FASTCHG_TYPE_UNKOWN;
	}
	if (!in) {
		chip->ufcs_stop_status = UFCS_STOP_VOTER_NONE;
		chip->ufcs_imax = 0;
		chip->ufcs_vmax = 0;
		chip->ufcs_power = 0;
		chip->ufcs_force_check = false;
	}
	oplus_ufcs_set_chg_status(UFCS_CHECKING);
	oplus_ufcs_set_ffc_started(false);
	oplus_ufcs_r_init(chip);
	oplus_ufcs_count_init(chip);
	oplus_ufcs_reset_temp_range(chip);
	chip->ufcs_authentication = false;
	chip->cp_mode = UFCS_BYPASS_MODE;
	chip->ufcs_dummy_started = false;
	chip->ufcs_fastchg_started = false;
	chip->ufcs_fastchg_batt_temp_status = UFCS_BAT_TEMP_NATURAL;
	chip->ufcs_temp_cur_range = UFCS_TEMP_RANGE_INIT;
	atomic_set(&chip->ufcs_freq_state, 0);

	if (chip->update_ufcs_work.work.func) {
		cancel_delayed_work_sync(&chip->update_ufcs_work);
	}
}

int oplus_ufcs_init(struct oplus_ufcs_chip *chip)
{
	struct power_supply *batt_psy;

	g_ufcs_chip = chip;

	oplus_ufcs_parse_dt(chip);
	oplus_ufcs_init_auth_msg();

	batt_psy = power_supply_get_by_name("battery");
	if (batt_psy)
		chip->ufcs_batt_psy = batt_psy;

	INIT_DELAYED_WORK(&chip->ufcs_stop_work, oplus_ufcs_stop_work);
	INIT_DELAYED_WORK(&chip->update_ufcs_work, oplus_ufcs_update_work);
	INIT_DELAYED_WORK(&chip->ufcs_start_work, oplus_ufcs_start_work);
	INIT_DELAYED_WORK(&chip->ufcs_uct_work, oplus_ufcs_uct_work);
	INIT_DELAYED_WORK(&chip->modify_cpufeq_work,
			  oplus_ufcs_modify_cpufeq_work);

	oplus_ufcs_variables_reset(false);
	oplus_ufcs_clear_dbg_info();
	ufcs_track_init(chip);
	return 0;
}

bool oplus_ufcs_is_allow_real(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return false;
	}

	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();
	chip->ap_batt_soc = oplus_chg_get_ui_soc();
	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY) {
		chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
	} else {
		chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
	}
	ufcs_err("chip->fg[%d %d %d] temp[%d %d %d %d] warm[%d %d %d]\n",
		 chip->ap_batt_temperature, chip->ap_batt_soc,
		 chip->ap_batt_volt, chip->limits.ufcs_batt_over_high_temp,
		 chip->limits.ufcs_batt_over_low_temp,
		 chip->limits.ufcs_strategy_soc_high,
		 chip->limits.ufcs_strategy_soc_over_low,
		 chip->limits.ufcs_normal_high_temp,
		 chip->limits.ufcs_warm_allow_soc,
		 chip->limits.ufcs_warm_allow_vol);
	if (!oplus_chg_get_mmi_value()) {
		return false;
	}
	if (chip->ap_batt_temperature >=
	    chip->limits.ufcs_batt_over_high_temp - UFCS_TEMP_WARM_RANGE_THD) {
		return false;
	}

	if (chip->ap_batt_temperature < chip->limits.ufcs_batt_over_low_temp) {
		return false;
	}
	/*
	if (chip->ops->check_btb_temp() < 0) {
		return false;
	}
*/
	if (chip->ap_batt_soc > chip->limits.ufcs_strategy_soc_high) {
		return false;
	}
	if (chip->ap_batt_soc < chip->limits.ufcs_strategy_soc_over_low) {
		return false;
	}

	if (chip->limits.ufcs_normal_high_temp != -EINVAL &&
	    (chip->ap_batt_temperature > chip->limits.ufcs_normal_high_temp) &&
	    (!(chip->ap_batt_soc < chip->limits.ufcs_warm_allow_soc &&
	       (chip->ap_batt_volt < chip->limits.ufcs_warm_allow_vol)))) {
		return false;
	}

	return true;
}

bool oplus_is_ufcs_charging(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return false;
	}
	return (oplus_ufcs_get_ufcs_dummy_started() ||
		oplus_ufcs_get_ufcs_fastchg_started() ||
		oplus_ufcs_get_ufcs_ping());
}

bool oplus_ufcs_show_vooc(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return false;
	}
	return (chip->ufcs_fastchg_type == PROTOCOL_CHARGING_UFCS_OPLUS && oplus_is_ufcs_charging());
}

int oplus_ufcs_get_protocol_status(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	int protocol_charging_ufcs = PROTOCOL_CHARGING_UNKNOWN;
	enum ufcs_fastchg_type ufcs_chg_type = chip->ufcs_fastchg_type;
	/*enum ufcs_fastchg_type ufcs_chg_type = UFCS_FASTCHG_TYPE_V1;*/
	if (!chip || !chip->ufcs_support_type || !oplus_is_ufcs_charging()) {
		return protocol_charging_ufcs;
	}

	switch (ufcs_chg_type) {
	case UFCS_FASTCHG_TYPE_V1:
		protocol_charging_ufcs = PROTOCOL_CHARGING_UFCS_OPLUS;
		break;
	case UFCS_FASTCHG_TYPE_THIRD:
		protocol_charging_ufcs = PROTOCOL_CHARGING_UFCS_THIRD;
		break;
	default:
		protocol_charging_ufcs = PROTOCOL_CHARGING_UFCS_OPLUS;
	}
	return protocol_charging_ufcs;
}

void oplus_ufcs_check_power(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	enum ufcs_fastchg_type ufcs_chg_type = UFCS_FASTCHG_TYPE_V1;
	enum ufcs_power_type ufcs_pwr_type = UFCS_POWER_TYPE_V1;
	if (!chip || !chip->ufcs_support_type) {
		return;
	}
	ufcs_chg_type = chip->ufcs_fastchg_type;

	switch (ufcs_chg_type) {
	case UFCS_FASTCHG_TYPE_V1:
		ufcs_pwr_type = UFCS_POWER_TYPE_V1;
		break;
	case UFCS_FASTCHG_TYPE_THIRD:
		ufcs_pwr_type = UFCS_POWER_TYPE_V1;
		break;
	default:
		ufcs_pwr_type = UFCS_POWER_TYPE_V1;
	}
	chip->ufcs_power = ufcs_pwr_type;
	ufcs_err(" %d, %d, %d, \n", chip->ufcs_power, chip->ufcs_imax,
		 chip->ufcs_vmax);
}

int oplus_ufcs_get_power(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type) {
		return 0;
	}
	return chip->ufcs_power;
}

void oplus_ufcs_print_log(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ufcs_support_type) {
		return;
	}

	ufcs_err("[%d / %d / %d / %d / %d / %d / %d] [%d / %d / %d / %d / %d / "
		 "%d / %x]\n",
		 chip->ufcs_support_type, chip->ufcs_status, chip->ufcs_chging,
		 chip->ufcs_dummy_started, chip->ufcs_fastchg_started,
		 chip->ufcs_stop_status, chip->ufcs_power, chip->ufcs_imax,
		 chip->ufcs_vmax, chip->ufcs_force_check, chip->ufcs_ping,
		 chip->cp_mode, oplus_ufcs_check_oplus_id(),
		 chip->ufcs_fastchg_type);
}

void oplus_ufcs_set_bcc_current(int val)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;

	if (!chip || !chip->ops || !chip->ufcs_support_type) {
		return;
	}
	return; /*do nothing now*/
	chip->current_bcc = val / oplus_ufcs_get_cp_ratio();
}

int oplus_ufcs_get_bcc_max_curr(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type || !chip->ops) {
		return 0;
	} else {
		return chip->bcc_max_curr;
	}
}

int oplus_ufcs_get_bcc_min_curr(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type || !chip->ops) {
		return 0;
	} else {
		return chip->bcc_min_curr;
	}
}

int oplus_ufcs_get_bcc_exit_curr(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type || !chip->ops) {
		return 0;
	} else {
		return chip->bcc_exit_curr;
	}
}

bool oplus_ufcs_bcc_get_temp_range(void)
{
	struct oplus_ufcs_chip *chip = g_ufcs_chip;
	if (!chip || !chip->ufcs_support_type || !chip->ops) {
		return 0;
	} else {
		if (chip->ufcs_temp_cur_range == UFCS_TEMP_RANGE_NORMAL_LOW ||
		    chip->ufcs_temp_cur_range == UFCS_TEMP_RANGE_NORMAL_HIGH) {
			return true;
		} else {
			return false;
		}
	}
}
