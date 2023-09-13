// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/consumer.h>
#include <soc/oplus/system/oplus_project.h>
#include "colorctrl.h"

#define DRIVER_NAME "color-ctrl_v2"

unsigned int ec_debug;

static void colorctrl_reset_hrtimer(struct color_ctrl_device *cd, unsigned int time);
static int ec_recharge_operation(struct color_ctrl_device *cd, bool is_ftm);
static int ec_charge_operation(struct color_ctrl_device *cd, bool is_ftm);
static int check_adc_status(struct color_ctrl_device *cd);
static ssize_t proc_ageing_para_read(struct file *file, char __user *user_buf, size_t count,
                                   loff_t *ppos);
static ssize_t proc_ageing_para_write(struct file *file, const char __user *buffer, size_t count,
                                    loff_t *ppos);

static int ec_reset_operation(struct color_ctrl_device *cd);

static const struct of_device_id colorctrl_match_table[] = {
	{.compatible = "oplus,color-ctrl_v2"},
	{}
};

static void colorctrl_msleep(int time) {
	if(time > 50)
		COLOR_INFO("wait for time : %dms.", time);
	msleep(time);
}

static int check_adc_status(struct color_ctrl_device *cd) {
	struct color_ctrl_hw_resource *hw_res = cd->hw_res;
	int vm_volt  = 0;
	int ret = 0;
	/*need to low*/
	colorctrl_msleep(1);
	gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
	gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
	gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
	colorctrl_msleep(1);
	gpio_direction_output(hw_res->vm_coloring_gpio, GPIO_LOW);
	gpio_direction_output(hw_res->vm_fade_gpio, GPIO_LOW);
	/*need to high*/
	colorctrl_msleep(1);
	gpio_direction_output(hw_res->adc_on_gpio, GPIO_HIGH);
	gpio_direction_output(hw_res->oa_en_gpio, GPIO_HIGH);
	colorctrl_msleep(2);
	/*to get adc value*/
	ret = iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);

	if (ret < 0) {
		COLOR_INFO("iio_read_channel_processed get error ret = %d, err_vm = %d", ret, vm_volt);
		goto OUT;
	}
	vm_volt = vm_volt / UV_PER_MV;
OUT:
	/*need to low*/
	gpio_direction_output(hw_res->oa_en_gpio, GPIO_LOW);
	colorctrl_msleep(2);
	gpio_direction_output(hw_res->adc_on_gpio, GPIO_LOW);
	return vm_volt;
}

static int coloring_power_control(struct color_ctrl_device *cd, int vol, bool on) {
	struct color_ctrl_hw_resource *hw_res = cd->hw_res;
	int ret = 0;

	if (on) {
		if (!IS_ERR_OR_NULL(hw_res->coloring)) {
			COLOR_INFO("enable the coloring power to voltage : %dmV.", vol);

			if (vol) {
				ret = regulator_set_voltage(hw_res->coloring, vol * UV_PER_MV, vol * UV_PER_MV);

				if (ret) {
					COLOR_INFO("Regulator coloring set voltage failed, ret = %d", ret);
					goto OUT;
				}
			}

			ret = regulator_enable(hw_res->coloring);

			if (ret) {
				COLOR_INFO("Regulator coloring enable failed, ret = %d", ret);
				goto OUT;
			}

			if (gpio_is_valid(hw_res->vm_coloring_gpio)) {
				COLOR_INFO("enable vm_coloring_gpio.");
				ret = gpio_direction_output(hw_res->vm_coloring_gpio, GPIO_HIGH);

				if (ret) {
					COLOR_INFO("enable vm_coloring_gpio fail.");
					goto OUT;
				}
			}
		}
	} else {
		if (!IS_ERR_OR_NULL(hw_res->coloring)) {
			if (gpio_is_valid(hw_res->vm_coloring_gpio)) {
				COLOR_INFO("disable vm_coloring_gpio.");
				ret = gpio_direction_output(hw_res->vm_coloring_gpio, GPIO_LOW);

				if (ret) {
					COLOR_INFO("disable vm_coloring_gpio fail.");
					goto OUT;
				}
			}

			COLOR_INFO("disable the coloring power.");
			ret = regulator_disable(hw_res->coloring);

			if (ret) {
				COLOR_INFO("Regulator coloring disable failed, ret = %d", ret);
				goto OUT;
			}
		}
	}

	return 0;
OUT:
	return -1;
}


static int fade_power_control(struct color_ctrl_device *cd, int vol, bool on) {
	struct color_ctrl_hw_resource *hw_res = cd->hw_res;
	int ret = 0;

	if (on) {
		if (!IS_ERR_OR_NULL(hw_res->fade)) {
			COLOR_INFO("enable the fade power to voltage : %dmV.", vol);

			if (vol) {
				ret = regulator_set_voltage(hw_res->fade, vol * UV_PER_MV, vol * UV_PER_MV);

				if (ret) {
					COLOR_INFO("Regulator fade set voltage failed, ret = %d", ret);
					goto OUT;
				}
			}

			ret = regulator_enable(hw_res->fade);

			if (ret) {
				COLOR_INFO("Regulator fade enable failed, ret = %d", ret);
				goto OUT;
			}

			if (gpio_is_valid(hw_res->vm_fade_gpio)) {
				COLOR_INFO("enable vm_fade_gpio.");
				ret = gpio_direction_output(hw_res->vm_fade_gpio, GPIO_HIGH);

				if (ret) {
					COLOR_INFO("enable vm_fade_gpio fail.");
					goto OUT;
				}
			}
		}
	} else {
		if (!IS_ERR_OR_NULL(hw_res->fade)) {
			if (gpio_is_valid(hw_res->vm_fade_gpio)) {
				COLOR_INFO("disable vm_fade_gpio.");
				ret = gpio_direction_output(hw_res->vm_fade_gpio, GPIO_LOW);

				if (ret) {
					COLOR_INFO("disable vm_fade_gpio fail.");
					goto OUT;
				}
			}

			COLOR_INFO("disable the fade power.");
			ret = regulator_disable(hw_res->fade);

			if (ret) {
				COLOR_INFO("Regulator fade disable failed, ret = %d", ret);
				goto OUT;
			}
		}
	}

	return 0;
OUT:
	return -1;
}

static void ready_to_recharge(struct color_ctrl_device *cd)
{
	int vm_volt;

	vm_volt = check_adc_status(cd);

	switch (cd->color_status) {
	case COLOR: /*red_open_circuit_vol  = <1350 1550 1550>;*/
		if (cd->is_first_boot) {
			if (vm_volt > cd->red_open_circuit_vol[2]) {
				COLOR_INFO("[first boot] vm_volt is %dmV>%dmV, nothing to do",
					vm_volt, cd->red_open_circuit_vol[2]);
				goto OUT;
			} else if (vm_volt <= cd->red_open_circuit_vol[1] && vm_volt > cd->red_open_circuit_vol[0]) {
				COLOR_INFO("[first boot] vm_volt is %dmV->(%dmV,%dmV], need to recharge",
								vm_volt, cd->red_open_circuit_vol[0], cd->red_open_circuit_vol[1]);
				ec_recharge_operation(cd, false);
			} else if (vm_volt <= cd->red_open_circuit_vol[0]) {
				COLOR_INFO("[first boot] vm_volt is %dmV<=%dmV, must to recharge",
					vm_volt, cd->red_open_circuit_vol[0]);
				if (cd->pcb_version >= PVT) {
					cd->operation = DO_COLORING;
				} else {
					cd->operation = DO_COLORING_EVT;
				}
				cd->color_status = UNKNOWN;
				ec_charge_operation(cd, false);
			}
		} else {
			if (vm_volt > cd->red_open_circuit_vol[1]) {
				COLOR_INFO("[normal boot] vm_volt is %dmV>%dmV, nothing to red recharge, reset timer\n", vm_volt, cd->red_open_circuit_vol[1]);
				colorctrl_reset_hrtimer(cd, cd->coloring_recharge_waitime);
				goto OUT;
			} else if (vm_volt <= cd->red_open_circuit_vol[1]) {
				COLOR_INFO("[normal boot] vm_volt is %dmV<=%dmV\n, need to recharge", vm_volt, cd->red_open_circuit_vol[1]);
				ec_recharge_operation(cd, false);
			}
		}
	break;
	case TRANSPARENT: /*fade_open_circuit_vol = <1100 1300 1350>;*/
		if (cd->fade_mandatory_flag) {
			COLOR_INFO("fade ctrl test, must to recharge");
			ec_recharge_operation(cd, false);
		}
		if (vm_volt > cd->fade_open_circuit_vol[1]) {
			cd->color_status = UNKNOWN;
			cd->operation = DO_FADE;
			COLOR_INFO("vm_volt is %dmV>%dmV , need to fade charge", vm_volt, cd->fade_open_circuit_vol[1]);
			ec_charge_operation(cd, false);
			cd->operation = UNKNOWN;
		} else if (vm_volt <= cd->fade_open_circuit_vol[1] && vm_volt >= cd->fade_open_circuit_vol[0]) {
			COLOR_INFO("vm_volt is %dmV->[%dmV,%dmV] , need to fade recharge",
					vm_volt, cd->fade_open_circuit_vol[0], cd->fade_open_circuit_vol[1]);
			ec_recharge_operation(cd, false);
		} else if (vm_volt < cd->fade_open_circuit_vol[0]) {
			COLOR_INFO("vm_volt is %dmV<%dmV, nothing to fade recharge, reset timer\n", vm_volt, cd->fade_open_circuit_vol[0]);
			colorctrl_reset_hrtimer(cd, cd->fade_recharge_waitime);
		}
	break;
	default:
		COLOR_INFO("nothing to do");
	break;
	}

OUT:
	return;
}

static void colorctrl_recharge_work(struct work_struct *work) {
	struct color_ctrl_device *cd = container_of(work, struct color_ctrl_device, recharge_work);

	if (!cd) {
		COLOR_INFO("no dev find, return.");
		goto OUT;
	}

	COLOR_INFO("is call. color_status: %d, operation: %d", cd->color_status, cd->operation);

	if (!cd->need_recharge) {
		COLOR_INFO("not need to do the recharge work due to force close.");
		goto OUT;
	}

	mutex_lock(&cd->rw_lock);
	ready_to_recharge(cd);
	mutex_unlock(&cd->rw_lock);
OUT:
	return;
}

static void colorctrl_reset_hrtimer(struct color_ctrl_device *cd, unsigned int time) {
	if (!cd) {
		COLOR_INFO("no dev find, return.");
		return;
	}
	COLOR_INFO("reset hrtimer expires time : %ds.", time);
	hrtimer_cancel(&cd->hrtimer);
	hrtimer_start(&cd->hrtimer, ktime_set(time, 0), HRTIMER_MODE_REL);
}

static enum hrtimer_restart colorctrl_hrtimer_handler(struct hrtimer *timer) {
	struct color_ctrl_device *cd = container_of(timer, struct color_ctrl_device, hrtimer);

	if (!cd) {
		COLOR_INFO("no dev find, return.");
		return HRTIMER_NORESTART;
	}

	COLOR_INFO("is call.");
	queue_work(cd->recharge_wq, &cd->recharge_work);
	/*hrtimer_forward(timer, timer->base->get_time(), ktime_set(cd->recharge_waitime, 0));*/
	return HRTIMER_NORESTART;
}

static int get_fitting_temperature(struct color_ctrl_device *cd, s64 *fitting_temp) {
	int i = 0, ret = 0, temp = 0;

	if (!cd) {
		COLOR_INFO("no dev or resources find, return.");
		return -1;
	}

	for (i = 0; i < cd->thermal_zone_device_num; i++) {
		ret = thermal_zone_get_temp(cd->thermal_zone_device[i], &temp);

		if (ret) {
			COLOR_INFO("fail to get %s temperature: %d", cd->thermal_zone_device_name[i], ret);
			return -1;
		}

		*fitting_temp += (s64)temp * cd->thermal_zone_device_weight[i];
		COLOR_DEBUG("current %s , temperature is : %d, fit_temp is %d", cd->thermal_zone_device_name[i], temp,  *fitting_temp);
	}

	*fitting_temp = *fitting_temp / 10000000;
	*fitting_temp += cd->thermal_zone_device_weight[cd->thermal_zone_device_num] /10000;
	*fitting_temp = *fitting_temp * 1000;

	COLOR_INFO("fitting temperature is %d'C", *fitting_temp / 1000);
	return 0;
}

static void update_temperature_status(struct color_ctrl_device *cd) {
	s64 temp = 0;
	int ret = 0;
	int i = 0;

	if (!cd) {
		COLOR_INFO("no dev or resources find, return.");
		return;
	}

	ret = get_fitting_temperature(cd, &temp);
	if (ret < 0) {
		COLOR_INFO("failed to get fitting temperature");
		return;
	}

	for(i = 0; i < cd->temp_num; i++) {
		if(temp >= cd->temperature_thd[i] && temp < cd->temperature_thd[i+1]) {
			cd->temp_status = i;
			COLOR_DEBUG("match %d success !, temp * 1000 is %d include (%d ~ %d)", i, temp, cd->temperature_thd[i], cd->temperature_thd[i+1]);
			return;
		}
	}
	COLOR_DEBUG("it's abnormal temperature now");
	cd->temp_status = ABNORMAL_ZONE;

	return;
}

static int close_all_gpio_operation(struct color_ctrl_device *cd) {
	struct color_ctrl_hw_resource *hw_res = cd->hw_res;
	int ret = 0;

	colorctrl_msleep(1);
	gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
	colorctrl_msleep(1);
	gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
	gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
	colorctrl_msleep(1);
	gpio_direction_output(hw_res->adc_on_gpio, GPIO_LOW);
	gpio_direction_output(hw_res->oa_en_gpio, GPIO_LOW);
	colorctrl_msleep(20);
	return ret;
}

static int set_gpio_status(struct color_ctrl_device *cd) {
	struct color_ctrl_hw_resource *hw_res = cd->hw_res;
	int ret = 0;

	switch (cd->operation) {
	case DO_COLORING_EVT:
	case DO_COLORING:
		COLOR_INFO(" do coloring gpio operation");
		colorctrl_msleep(1);
		gpio_direction_output(hw_res->si_in_1_gpio, GPIO_HIGH);
		gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
		break;
	case DO_FADE:
	case DO_RECHARGE_FADE:
		COLOR_INFO("do fade gpio operation");
		colorctrl_msleep(1);
		gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
		gpio_direction_output(hw_res->si_in_2_gpio, GPIO_HIGH);
		break;
	case DO_RECHARGE_COLORING:
		COLOR_INFO("do recharge coloring operation");
		colorctrl_msleep(1);
		gpio_direction_output(hw_res->si_in_1_gpio, GPIO_HIGH);
		gpio_direction_output(hw_res->si_in_2_gpio, GPIO_HIGH);
		break;
	default:
		COLOR_INFO("nothing do_gpio_operation");
		return -1;
	}

	colorctrl_msleep(10);
	gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
	return ret;
}

static int do_operation(struct color_ctrl_device *cd, int charge_volt, int charge_time) {
	int ret = 0;

	if (!cd) {
		COLOR_INFO("!cd");
		return -1;
	}

	COLOR_INFO("charge_volt is %dmV, charge time is %dms, status: %d, operation: %d",
				charge_volt, charge_time, cd->color_status, cd->operation);

	switch (cd->operation) {
	case DO_COLORING_EVT:
	case DO_COLORING:
		if(cd->ec_step_voltage_support && cd->pcb_version >= PVT) {
			cd->operation = DO_RECHARGE_COLORING;
			set_gpio_status(cd);
			colorctrl_msleep(charge_time);
			close_all_gpio_operation(cd);

			cd->operation = DO_COLORING;
			charge_volt = cd->step_para[0];
			charge_time = cd->step_para[1];
			COLOR_DEBUG("step voltage is %dmV, time is %ds", charge_volt, charge_time);
			if (0 == charge_volt && 0 == charge_time && cd->ec_step_voltage_support && cd->pcb_version >= PVT) {
				COLOR_INFO("high temp mode , not to charge");
				break;
			}
		}

		if(charge_volt >= MAX_CHANGE_VOLTAGE) {
			COLOR_INFO("now vol is %dmV, need to enable PM8008 power.", charge_volt);
			ret = fade_power_control(cd, charge_volt, true);
		} else {
			ret = coloring_power_control(cd, charge_volt, true);
		}

		if (ret) {
			COLOR_INFO("enable coloring power failed.");
			goto OUT;
		}

		set_gpio_status(cd);
		colorctrl_msleep(charge_time);
		close_all_gpio_operation(cd);

		if(charge_volt >= MAX_CHANGE_VOLTAGE) {
			COLOR_INFO("now vol is %dmV, need to disable PM8008 power.", charge_volt);
			ret = fade_power_control(cd, 0, false);
		} else {
			ret = coloring_power_control(cd, 0, false);
		}

		if (ret) {
			COLOR_INFO("disable coloring %d power failed");
			goto OUT;
		}
		break;

	case DO_RECHARGE_COLORING:
		set_gpio_status(cd);
		colorctrl_msleep(charge_time);
		close_all_gpio_operation(cd);
		break;

	case DO_FADE:
	case DO_RECHARGE_FADE:
		ret = fade_power_control(cd, charge_volt, true);
		if (ret) {
			COLOR_INFO("enable fade power failed.");
			goto OUT;
		}

		set_gpio_status(cd);
		colorctrl_msleep(charge_time);
		close_all_gpio_operation(cd);
		fade_power_control(cd, 0, false);
		if (ret) {
			COLOR_INFO("disable fade power failed.");
			goto OUT;
		}
		break;

	default :
		COLOR_INFO("err do_operation %d", cd->operation);
		goto OUT;
		break;
	}

	return 0;
OUT:
	close_all_gpio_operation(cd);
	return -1;
}

static int get_cur_para(struct color_ctrl_device *cd, struct color_ctrl_control_para *para) {
	int ret = 0;
	bool ageing_flag = false;

	if (!cd) {
		COLOR_INFO("!cd");
		return -1;
	}

	if (cd->temp_status > cd->temp_num) {
		COLOR_INFO("temp_status is %d , over limit!", cd->temp_num);
		return ret;
	}

	if ((cd->color_status == COLOR || cd->operation == DO_COLORING_EVT || cd->operation == DO_COLORING)
		&& cd->ageing_color_para2) {
		COLOR_INFO("set ageing coloring para");
		ageing_flag = true;
	} else if ((cd->color_status == TRANSPARENT || cd->operation == DO_FADE) && cd->ageing_fade_para2) {
		COLOR_INFO("set ageing fade para");
		ageing_flag = true;
	}

	if (!ageing_flag) {
		cd->charge_volt = para->coloring_charge_para_1[cd->temp_status][0];
		cd->charge_time = para->coloring_charge_para_1[cd->temp_status][1];
		cd->recharge_volt = para->coloring_charge_para_1[cd->temp_status][2];
		cd->recharge_time = para->coloring_charge_para_1[cd->temp_status][3];

		if (cd->ec_step_voltage_support == true && cd->operation == DO_COLORING) {
			cd->step_para[0] =  para->coloring_charge_para_1[cd->temp_status][4];
			cd->step_para[1] =  para->coloring_charge_para_1[cd->temp_status][5];
			COLOR_DEBUG("step para is %d and %d", cd->step_para[0], cd->step_para[1]);
		}
	} else if (ageing_flag) {
		cd->charge_volt = para->coloring_charge_para_2[cd->temp_status][0];
		cd->charge_time = para->coloring_charge_para_2[cd->temp_status][1];
		cd->recharge_volt = para->coloring_charge_para_2[cd->temp_status][2];
		cd->recharge_time = para->coloring_charge_para_2[cd->temp_status][3];
	}

	COLOR_DEBUG(" ageing_flag:[%d] color_status: %d, operation: %d, get_cur_para[%d]: charge_volt[%d], charge_time[%d], recharge_volt[%d], recharge_time[%d]",
			ageing_flag, cd->color_status, cd->operation, cd->temp_status, cd->charge_volt, cd->charge_time, cd->recharge_volt, cd->recharge_time);

	return ret;
}

static int effective_check(struct color_ctrl_device *cd)
{
	int i = 0, vm_volt = 0;
	int get_open_circuit_vol[3] = {0};

	if (!cd) {
		COLOR_INFO("!cd");
		return -1;
	}

	for(i = 0; i < 2; i++) {
		colorctrl_msleep(cd->wait_check_time[i]);
		vm_volt = check_adc_status(cd);
		if (vm_volt < 0) {
			COLOR_INFO("iio_read_channel_processed get error ret = %d", vm_volt);
			goto OUT;
		}
		get_open_circuit_vol[i] = vm_volt;
	}
	COLOR_INFO("V1:%dmV, V2:%dmV, do operation=%d", get_open_circuit_vol[0], get_open_circuit_vol[1], cd->operation);
	switch (cd->operation) {
	case DO_RECHARGE_COLORING:	/*fade_to_red_check_volt = <1450 1780 1820>*/
	case	DO_COLORING:
	case DO_COLORING_EVT:
		if(get_open_circuit_vol[0] > cd->fade_to_red_check_volt[1] &&
			get_open_circuit_vol[0] < cd->fade_to_red_check_volt[2] &&
			get_open_circuit_vol[1] > cd->fade_to_red_check_volt[1] &&
			get_open_circuit_vol[1] < cd->fade_to_red_check_volt[2]) {
			COLOR_INFO("(%ldmV<V1:%ldmV<%ldmV) && (%ldmV<V2:%ldmV<%ldmV)",
					cd->fade_to_red_check_volt[1], get_open_circuit_vol[0],
					cd->fade_to_red_check_volt[2],
					cd->fade_to_red_check_volt[1], get_open_circuit_vol[1],
					cd->fade_to_red_check_volt[2]);
				cd->open_circuit_flag++;
				if(cd->open_circuit_flag > cd->fade_to_red_retry_cnt) {
					cd->color_status = OPEN_CIRCUIT;
					COLOR_INFO("WARNING!!!  fade to coloring [OPEN_CIRCUIT]");
					goto OUT;
				} else {
					COLOR_INFO("try again coloring recharge");
					cd->color_status = COLOR;
					ec_recharge_operation(cd, false);
				}
		} else if (get_open_circuit_vol[1] < cd->fade_to_red_check_volt[0]) {
				COLOR_INFO("V2:%ldmV<%ldmV",
					get_open_circuit_vol[1], cd->fade_to_red_check_volt[0]);
				cd->recharge_time_flag++;
				if (cd->recharge_time_flag > cd->fade_to_red_retry_cnt) {
					cd->color_status = SHORT_CIRCUIT;
					COLOR_INFO("WARNING!!!  fade to coloring [SHORT_CIRCUIT]");
					goto OUT;
				} else {
					COLOR_INFO("try again coloring recharge");
					cd->color_status = COLOR;
					ec_recharge_operation(cd, false);
				}
		} else if (get_open_circuit_vol[1] >= cd->fade_to_red_check_volt[0]) {
			COLOR_INFO("SUCCESS!!!   do coloring [V2:%ldmV>=%ldmV] start %ds timer",
				get_open_circuit_vol[1], cd->fade_to_red_check_volt[0], cd->coloring_recharge_waitime);
			cd->color_status = COLOR;
			colorctrl_reset_hrtimer(cd, cd->coloring_recharge_waitime);
			goto OUT;
		}
	break;
	case DO_FADE: /*red_to_fade_check_volt = <1100 1780 1820>*/
	case DO_RECHARGE_FADE:
		if(get_open_circuit_vol[1] > cd->red_to_fade_check_volt[0]) {
			cd->open_circuit_flag++;
			COLOR_INFO("V2:%ldmV>%ldmV", get_open_circuit_vol[1], cd->red_to_fade_check_volt[0]);
			if(cd->open_circuit_flag > cd->red_to_fade_retry_cnt) {
				if (get_open_circuit_vol[1] > cd->red_to_fade_check_volt[1] &&
					get_open_circuit_vol[1] < cd->red_to_fade_check_volt[2]) {
					COLOR_INFO("WARNING!!!  coloring to fade[OPEN_CIRCUIT]");
					cd->color_status = OPEN_CIRCUIT;
				} else if (get_open_circuit_vol[1] > cd->red_to_fade_check_volt[0] &&
					get_open_circuit_vol[1] <= cd->red_to_fade_check_volt[1]) {
					COLOR_INFO("WARNING!!!  coloring to fade[SHORT_CIRCUIT]");
					cd->color_status = SHORT_CIRCUIT;
				}
				goto OUT;
			} else  {
				COLOR_INFO("try again to fade recharge");
				cd->color_status = TRANSPARENT;
				ec_recharge_operation(cd, false);
			}
		 } else if (get_open_circuit_vol[1] <= cd->red_to_fade_check_volt[0]) {
			COLOR_INFO("SUCCESS!!!   do fade [V2:%ldmV<=%ldmV] start %ds timer",
				get_open_circuit_vol[1], cd->red_to_fade_check_volt[0], cd->fade_recharge_waitime);
			cd->color_status = TRANSPARENT;
			colorctrl_reset_hrtimer(cd, cd->fade_recharge_waitime);
			goto OUT;
		}
	break;
	default:
	break;
		}
	return 0;
OUT:
	cd->recharge_time_flag = 0;
	cd->open_circuit_flag = 0;
	cd->operation = UNKNOWN;
	COLOR_INFO("effect check over, operation: %d, color_status:%s",
		cd->operation, cd->color_status > 2 ? (cd->color_status > 3 ? "open circurt" : "short circurt") : (cd->color_status > 1 ? "fade" : "red"));
	return 0;
}

static int ec_recharge_operation(struct color_ctrl_device *cd, bool is_ftm)
{
	struct color_ctrl_control_para *para = NULL;
	int ret = 0;
	if (!cd) {
		COLOR_INFO("!cd");
		return 0;
	}
	if (!cd->color_control_para||!cd->transparent_control_para) {
		COLOR_INFO("no dev or resources find, return.");
		goto OUT;
	}

	COLOR_DEBUG("ec_recharge_operation, status:%d operation:%d", cd->color_status, cd->operation);

	switch (cd->color_status) {
	case COLOR:
		para = cd->color_control_para;
		cd->operation = DO_RECHARGE_COLORING;
	break;
	case TRANSPARENT:
		para = cd->transparent_control_para;
		cd->operation = DO_RECHARGE_FADE;
	break;
	default:
		COLOR_INFO("nothing ec_recharge_operation to do");
	break;
	}

	update_temperature_status(cd);
	if (cd->temp_status == ABNORMAL_ZONE) {
		COLOR_INFO("abnormal temperature occur, can not do recharge operation");
		goto OUT;
	}

	ret = get_cur_para(cd, para);
	if (ret < 0) {
		COLOR_INFO("get cur para Failed!");
		goto OUT;
	}

	ret = do_operation(cd, cd->recharge_volt, cd->recharge_time);
	if (ret) {
		COLOR_INFO("enable power failed.");
		goto OUT;
	}

	if (is_ftm) {
		if (cd->operation == DO_COLORING_EVT || cd->operation == DO_RECHARGE_COLORING || cd->operation == DO_COLORING) {
			cd->color_status = COLOR;
			colorctrl_reset_hrtimer(cd, cd->coloring_recharge_waitime);
		} else if (cd->operation == DO_FADE || cd->operation == DO_RECHARGE_FADE) {
			cd->color_status = TRANSPARENT;
			colorctrl_reset_hrtimer(cd, cd->fade_recharge_waitime);
		}
		COLOR_INFO("set recharge FTM success!");
		goto OUT;
	}

	effective_check(cd);
OUT:
	cd->operation = UNKNOWN;
	COLOR_INFO("color status is %s\n",
		cd->color_status > 2 ? (cd->color_status > 3 ? "short circuit" : "open circuit") : (cd->color_status > 1 ? "fade" : "red"));
	close_all_gpio_operation(cd);
	return 0;
}

static int ec_charge_operation(struct color_ctrl_device *cd, bool is_ftm)
{
	struct color_ctrl_control_para *para;
	int ret = 0;

	if (!cd) {
		COLOR_INFO("!cd");
		return -1;
	}
	if (!cd->color_control_para||!cd->transparent_control_para) {
		COLOR_INFO("no dev or resources find, return.");
		goto OUT;
	}

	COLOR_DEBUG("ec_charge_operation, status:%d operation:%d", cd->color_status, cd->operation);

	switch (cd->operation) {
	case DO_COLORING_EVT:
	case DO_COLORING:
	case DO_RECHARGE_COLORING:
		if (cd->color_status == COLOR) {
			COLOR_INFO("already color");
			goto OUT;
		}
		para = cd->color_control_para;
	break;
	case DO_FADE:
		if (cd->color_status == TRANSPARENT) {
			COLOR_INFO("already transparent");
			goto OUT;
		}
		para = cd->transparent_control_para;
	break;
	default:
		COLOR_INFO("nothing ec_charge_operation to do");
	break;
	}

	update_temperature_status(cd);
	if (cd->temp_status == ABNORMAL_ZONE) {
		COLOR_INFO("abnormal temperature occur, can not do recharge operation");
		goto OUT;
	}

	ret = get_cur_para(cd, para);
	if (ret < 0) {
		COLOR_INFO("get cur para Failed!");
		goto OUT;
	}

	ret = do_operation(cd, cd->charge_volt, cd->charge_time);
	if (ret) {
		COLOR_INFO("enable power failed.");
		goto OUT;
	}

	if (is_ftm) {
		if (cd->operation == DO_COLORING_EVT || cd->operation == DO_RECHARGE_COLORING || cd->operation == DO_COLORING) {
			cd->color_status = COLOR;
			colorctrl_reset_hrtimer(cd, cd->coloring_recharge_waitime);
		} else if (cd->operation == DO_FADE || cd->operation == DO_RECHARGE_FADE) {
			cd->color_status = TRANSPARENT;
			colorctrl_reset_hrtimer(cd, cd->fade_recharge_waitime);
		}
		COLOR_INFO("set charge FTM success!");
		goto OUT;
	}

	effective_check(cd);
OUT:
	cd->operation = UNKNOWN;
	COLOR_INFO("color status is %s\n",
		cd->color_status > 2 ? (cd->color_status > 3 ? "short circuit" : "open circuit") : (cd->color_status > 1 ? "fade" : "red"));
	close_all_gpio_operation(cd);
	return 0;
}

static int ec_reset_operation(struct color_ctrl_device *cd)
{
	int vm_volt;

	/*reset_ftm_voltage = <1350 1620 1620>;*/
	COLOR_INFO("FTM: check red status start");
	vm_volt = check_adc_status(cd);
	if (vm_volt > cd->reset_ftm_vol[0] && vm_volt <= cd->reset_ftm_vol[2]) {
		COLOR_INFO("reset_ftm_vol:%dmv is (%dmV , %dmV], need to recharge",
					vm_volt, cd->reset_ftm_vol[0], cd->reset_ftm_vol[2]);
		cd->color_status = COLOR;
		ec_recharge_operation(cd, false);
	} else if (vm_volt <= cd->reset_ftm_vol[0]) {
		COLOR_INFO("reset_ftm_vol:%dmv < %dmV, need to charge",
					vm_volt, cd->reset_ftm_vol[0]);
		cd->color_status = UNKNOWN;
		if (cd->pcb_version >= PVT) {
			cd->operation = DO_COLORING;
		} else {
			cd->operation = DO_COLORING_EVT;
		}
		ec_charge_operation(cd, true);
	} else if (vm_volt > cd->reset_ftm_vol[2]) {
		COLOR_INFO("reset_ftm_vol:%dmv > %dmV, not to charge",
					vm_volt, cd->reset_ftm_vol[2]);
	}

	colorctrl_msleep(2000);

	vm_volt = check_adc_status(cd);
	if (vm_volt >= cd->reset_ftm_vol[1]) {
		COLOR_INFO("reset ftm vol is ok, now set status is color");
		cd->color_status = COLOR;
	} else {
		COLOR_INFO("reset ftm vol is err, now set status is unknown");
		cd->color_status = UNKNOWN;
	}

	return 0;
}


static ssize_t proc_colorctrl_write(struct file *file, const char __user *buffer, size_t count,
                                    loff_t *ppos) {
	char buf[8] = {0};
	int temp = 0;
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

	if (!cd) {
		COLOR_INFO("!cd");
		return -1;
	}

	mutex_lock(&cd->rw_lock);

	if (count >= 8) {
		COLOR_INFO("count over size");
		goto OUT;
	}

	if (copy_from_user(buf, buffer, count)) {
		COLOR_INFO("read proc input error.");
		goto OUT;
	}

	sscanf(buf, "%d", &temp);
	COLOR_INFO("write value: %d.", temp);

	if (temp > MAX_CTRL_TYPE) {
		COLOR_INFO("not support change color type.");
		goto OUT;
	}

	if (cd->color_status == OPEN_CIRCUIT || cd->color_status == SHORT_CIRCUIT) {
		COLOR_INFO("WARHING!!! Equipment abnormal . now is %s\n",
		           cd->color_status > 3 ? "short circuit" : "open circuit");
		goto OUT;
	}

	switch (temp) {
	case LIGHT_COLOR_FTM:
		COLOR_INFO("not support");
		break;
	case COLOR_FTM:
		if (cd->pcb_version >= PVT) {
			cd->operation = DO_COLORING;
		} else {
			cd->operation = DO_COLORING_EVT;
		}
		ec_charge_operation(cd, true);
		cd->operation = UNKNOWN;
		break;
	case TRANSPARENT_FTM:
		cd->operation = DO_FADE;
		ec_charge_operation(cd, true);
		cd->operation = UNKNOWN;
		break;
	case COLOR_NORMAL:
		if (cd->pcb_version >= PVT) {
			cd->operation = DO_COLORING;
		} else {
			cd->operation = DO_COLORING_EVT;
		}
		if(cd->color_status == UNKNOWN) {
			cd->color_status = COLOR;
			COLOR_INFO("boot phone need to red recharge");
			ready_to_recharge(cd);
		} else {
			ec_charge_operation(cd, false);
		}
		cd->operation = UNKNOWN;
		break;
	case TRANSPARENT_NORMAL:
		cd->operation = DO_FADE;
		if(cd->color_status == UNKNOWN) {
			cd->color_status = TRANSPARENT;
			COLOR_INFO("boot phone need to fade recharge");
			ready_to_recharge(cd);
		} else {
			ec_charge_operation(cd, false);
		}
		cd->operation = UNKNOWN;
		break;
	case RESET:	/* 5 */
		cd->is_first_boot = true;
		cd->color_status = COLOR;
		ready_to_recharge(cd);
		cd->is_first_boot = false;
		break;
	case RESET_FTM:
		ec_reset_operation(cd);
		break;
	case STOP_RECHARGE:
		cd->need_recharge = false;
		COLOR_INFO("stop recharge work.");
		break;
	case OPEN_RECHARGE:
		cd->need_recharge = true;
		COLOR_INFO("open recharge work.");
		break;
	case AGEING_COLOR_PARA2:
		/*cd->ageing_color_para2 = true;*/
		COLOR_INFO("ageing_color_para2 ture.");
		break;
	case AGEING_FADE_PARA2:
		/*cd->ageing_fade_para2 = true;*/
		COLOR_INFO("ageing_fade_para2 ture..");
		break;
	default :
		COLOR_INFO("not support color status.");
		break;
	}
OUT:
	mutex_unlock(&cd->rw_lock);
	return count;
}

static ssize_t proc_colorctrl_read(struct file *file, char __user *user_buf, size_t count,
                                   loff_t *ppos) {
	ssize_t ret = 0;
	char page[PAGESIZE] = {0};
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

	if (!cd || *ppos != 0) {
		return 0;
	}

	mutex_lock(&cd->rw_lock);

	snprintf(page, PAGESIZE - 1, "%d\n", (int)cd->color_status);
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	mutex_unlock(&cd->rw_lock);
	COLOR_INFO("read value: %d.", cd->color_status);
	return ret;
}

static const struct file_operations proc_colorctrl_ops = {
	.read  = proc_colorctrl_read,
	.write = proc_colorctrl_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_other_status_read(struct file *file, char __user *user_buf, size_t count,
                                    loff_t *ppos) {
	ssize_t ret = 0;
	char page[PAGESIZE] = {0};
	int vm_volt;

	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

	if (!cd || *ppos != 0) {
		return 0;
	}

	mutex_lock(&cd->rw_lock);
	vm_volt = check_adc_status(cd);

	if (vm_volt < 0) {
		COLOR_INFO("iio_read_channel_processed get error ret = %d", vm_volt);
		snprintf(page, PAGESIZE - 1, "adc:[err]\n");
		ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
		goto OUT;
	}

	snprintf(page, PAGESIZE - 1, "adc: %2dmV, version status: %s\n",
		vm_volt, cd->pcb_version > DVT ? "PVT" : cd->pcb_version < DVT ? "EVT" : "DVT");

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	mutex_unlock(&cd->rw_lock);
	COLOR_INFO("read value: %d.", cd->color_status);
OUT:
	return ret;
}

static const struct file_operations proc_other_status_ops = {
	.read  = proc_other_status_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_cover_color_read(struct file *file, char __user *user_buf, size_t count,
                                    loff_t *ppos) {
	ssize_t ret = 0;
	char page[PAGESIZE] = {0};
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

	if (!cd || *ppos != 0) {
		return 0;
	}

	mutex_lock(&cd->rw_lock);

	snprintf(page, PAGESIZE - 1, "%s", "red");

	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

	mutex_unlock(&cd->rw_lock);

	COLOR_INFO("proc_cover_color_read end.");

	return ret;
}


static const struct file_operations proc_cover_color_ops = {
	.read  = proc_cover_color_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_temperature_control_read(struct file *file, char __user *user_buf, size_t count,
                                             loff_t *ppos) {
	ssize_t ret = 0;
	char page[PAGESIZE] = {0};
	s64 temp = 0;
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

	if (!cd || *ppos != 0) {
		return 0;
	}

	mutex_lock(&cd->rw_lock);
	ret = get_fitting_temperature(cd, &temp);

	if (ret < 0) {
		COLOR_INFO("failed to get fitting temperature");
		snprintf(page, PAGESIZE - 1, "failed to get fitting temperature\n");
	} else {
		COLOR_INFO("current fitting temperature is : %d\n", temp);
		snprintf(page, PAGESIZE - 1, "%lld", temp);
	}

	mutex_unlock(&cd->rw_lock);
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	return ret;
}

static const struct file_operations proc_temperature_control_ops = {
	.read  = proc_temperature_control_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t colorctrl_voltage_read(struct file *file, char __user *user_buf, size_t count,
                                      loff_t *ppos) {
	ssize_t ret = 0;
	int vm_volt = 0;
	char page[PAGESIZE] = {0};
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));
	struct color_ctrl_hw_resource *hw_res = NULL;

	if (!cd || *ppos != 0) {
		return 0;
	}

	mutex_lock(&cd->rw_lock);
	hw_res = cd->hw_res;
	COLOR_INFO("colorctrl_voltage_read.", ret);

	if (!hw_res->vm_v_chan) {
		COLOR_INFO("voltage read is not support.", ret);
		snprintf(page, PAGESIZE - 1, "voltage read is not support.\n");
		goto OUT;
	}

	vm_volt = check_adc_status(cd);

	if (vm_volt < 0) {
		COLOR_INFO("iio_read_channel_processed get error ret = %d", vm_volt);
		goto OUT;
	}

	vm_volt = vm_volt - 1800;
	COLOR_INFO("vm_volt: %d", vm_volt);
	snprintf(page, PAGESIZE - 1, "%d\n", vm_volt);
OUT:
	close_all_gpio_operation(cd);
	mutex_unlock(&cd->rw_lock);
	return simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
}

static const struct file_operations proc_adc_voltage_ops = {
	.read  = colorctrl_voltage_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static int colorctrl_str_to_int(char *in, int start_pos, int end_pos) {
	int i = 0, value = 0;

	if (start_pos > end_pos) {
		return -1;
	}

	for (i = start_pos; i <= end_pos; i++) {
		value = (value * 10) + (in[i] - '0');
	}

	COLOR_INFO("return %d.", value);
	return value;
}

static int colorctrl_str_parse(char *in, char *name, unsigned int max_len, unsigned int *array,
                               unsigned int array_max) {
	int i = 0, in_cnt = 0, name_index = 0;
	int start_pos = 0, value_cnt = 0;

	if (!array || !in) {
		COLOR_INFO("array or in is null.");
		return -1;
	}

	in_cnt = strlen(in);

	/*parse name*/
	for (i = 0; i < in_cnt; i++) {
		if (':' == in[i]) {     /*split name and parameter by ":" symbol*/
			if (i > max_len) {
				COLOR_INFO("string %s name too long.\n", in);
				return -1;
			}

			name_index = i;
			memcpy(name, in, name_index);   /*copy to name buffer*/
			COLOR_INFO("set name %s.", name);
		}
	}

	/*parse parameter and put it into split_value array*/
	start_pos = name_index + 1;

	for (i = name_index + 1; i <= in_cnt; i++) {
		if (in[i] < '0' || in[i] > '9') {
			if ((' ' == in[i]) || (0 == in[i]) || ('\n' == in[i]) || (',' == in[i])) {
				if (value_cnt <= array_max) {
					array[value_cnt++] = colorctrl_str_to_int(in, start_pos, i - 1);
					start_pos = i + 1;
				} else {
					COLOR_INFO("too many parameter(%s).", in);
					return -1;
				}
			} else {
				COLOR_INFO("incorrect char 0x%02x in %s.", in[i], in);
				return -1;
			}
		}
	}

	value_cnt = value_cnt - 1;
	COLOR_INFO("input para count is %d.", value_cnt);
	return value_cnt;
}

static int colorctrl_main_parameter_read(struct seq_file *s, void *v) {
	struct color_ctrl_device *cd = s->private;
	struct color_ctrl_control_para *color_para = NULL;
	struct color_ctrl_control_para *transparent_para = NULL;
	int i = 0, j = 0;

	if (!cd || !cd->color_control_para || !cd->transparent_control_para) {
		return 0;
	}

	COLOR_INFO(" call.");
	seq_printf(s, "--------\n");
	color_para = cd->color_control_para;
	transparent_para = cd->transparent_control_para;
	mutex_lock(&cd->rw_lock);

	/*ageing enable*/
	seq_printf(s, "now ageing enable : coloring_ageing: %d, fade_ageing:%d, s\n",
			cd->ageing_color_para2, cd->ageing_fade_para2);
	seq_printf(s, "--------\n");

	/*fade_recharge_waitime && coloring_recharge_waitime*/
	seq_printf(s, "fade_recharge_waitime : %ds\n", cd->fade_recharge_waitime);
	seq_printf(s, "coloring_recharge_waitime : %ds\n", cd->coloring_recharge_waitime);
	seq_printf(s, "--------\n");

	/*wait_check_time*/
	seq_printf(s, "wait_check_time <%d , %d>\n", cd->wait_check_time[0], cd->wait_check_time[1]);
	seq_printf(s, "--------\n");

	/***open_circuit_vol***/
	seq_printf(s, "fade_open_circuit_vol: [%d][%d][%d]\n", cd->fade_open_circuit_vol[0],
							cd->fade_open_circuit_vol[1], cd->fade_open_circuit_vol[2]);
	seq_printf(s, "red_open_circuit_vol: [%d][%d][%d]\n", cd->red_open_circuit_vol[0],
							cd->red_open_circuit_vol[1], cd->red_open_circuit_vol[2]);
	seq_printf(s, "--------\n");

	/*fade_to_red_check_volt and red_to_fade_check_volt*/
	seq_printf(s, "fade_to_red_retry_cnt : %d\n", cd->fade_to_red_retry_cnt);
	seq_printf(s, "red_to_fade_retry_cnt : %d\n", cd->red_to_fade_retry_cnt);
	seq_printf(s, "--------\n");

	seq_printf(s, "fade_to_red_check_volt:[%d][%d][%d]\n", cd->fade_to_red_check_volt[0],
			cd->fade_to_red_check_volt[1], cd->fade_to_red_check_volt[2]);
	seq_printf(s, "red_to_fade_check_volt:[%d][%d][%d]\n", cd->red_to_fade_check_volt[0],
			cd->red_to_fade_check_volt[1], cd->red_to_fade_check_volt[2]);
	seq_printf(s, "--------\n");

	seq_printf(s, "reset_ftm_vol:[%d][%d][%d]\n", cd->reset_ftm_vol[0],
			cd->reset_ftm_vol[1], cd->reset_ftm_vol[2]);
	seq_printf(s, "--------\n");

	/*thermal_zone_device_weight*/
	seq_printf(s, "thermal_zone_device_weight : \n");
	for (i = 0; i < 6; i++) {
		seq_printf(s, " [%d] ", cd->thermal_zone_device_weight[i]);
	}
	seq_printf(s, "\n");
	seq_printf(s, "--------\n");

	/*temperature_thd_offect*/
	seq_printf(s, "temperature_thd_offect <%d>\n", cd->temperature_thd_offect);

	/*temperature_thd offect tmp*/
	seq_printf(s, "temperature_thd offect tmp : \n");
	for (i = 0; i < cd->temp_num; i++) {
		seq_printf(s, " [%d] ", cd->temperature_thd[i] + cd->temperature_thd_offect);
	}
	seq_printf(s, "\n");
	seq_printf(s, "--------\n");

	/*temperature_thd*/
	seq_printf(s, "temperature_thd : \n");
	for (i = 0; i < cd->temp_num; i++) {
		seq_printf(s, " [%d] ", cd->temperature_thd[i]);
	}
	seq_printf(s, "\n");
	seq_printf(s, "--------\n");
	/*coloring_charge_para_1*/
	seq_printf(s, "red_charge_para_1, [%2d][%2d]: \n", cd->red_para_1_format[0], cd->red_para_1_format[1]);
	for(i = 0; i < cd->red_para_1_format[0]; i++) {
		seq_printf(s, "%2d ~ %2d:", cd->temperature_thd[i]/1000, cd->temperature_thd[i+1]/1000);
		for(j = 0; j < cd->red_para_1_format[1]; j++) {
			seq_printf(s, " %6d ", color_para->coloring_charge_para_1[i][j]);
		}
		seq_printf(s, "\n");
	}
	seq_printf(s, "--------\n");

	/*fade_charge_para_1*/
	seq_printf(s, "fade_charge_para_1, [%2d][%2d]: \n", cd->fade_para_1_format[0], cd->fade_para_1_format[1]);
	for(i = 0; i < cd->fade_para_1_format[0]; i++) {
		seq_printf(s, "%2d ~ %2d:", cd->temperature_thd[i]/1000, cd->temperature_thd[i+1]/1000);
		for(j = 0; j < cd->fade_para_1_format[1]; j++) {
			seq_printf(s, " %6d ", transparent_para->coloring_charge_para_1[i][j]);
		}
		seq_printf(s, "\n");
	}
	seq_printf(s, "--------\n");

	/*coloring_charge_para_2*/
	seq_printf(s, "red_charge_para_2, [%2d][%2d]: \n", cd->red_para_2_format[0], cd->red_para_2_format[1]);
	for(i = 0; i < cd->red_para_2_format[0]; i++) {
		seq_printf(s, "%2d ~ %2d:", cd->temperature_thd[i]/1000, cd->temperature_thd[i+1]/1000);
		for(j = 0; j < cd->red_para_2_format[1]; j++) {
			seq_printf(s, " %6d ", color_para->coloring_charge_para_2[i][j]);
		}
		seq_printf(s, "\n");
	}
	seq_printf(s, "--------\n");
	/*fade_charge_para_2*/
	seq_printf(s, "fade_charge_para_2, [%2d][%2d]: \n", cd->fade_para_2_format[0], cd->fade_para_2_format[1]);
	for(i = 0; i < cd->fade_para_2_format[0]; i++) {
		seq_printf(s, "%2d ~ %2d:", cd->temperature_thd[i]/1000, cd->temperature_thd[i+1]/1000);
		for(j = 0; j < cd->fade_para_2_format[1]; j++) {
			seq_printf(s, " %6d ", transparent_para->coloring_charge_para_2[i][j]);
		}
		seq_printf(s, "\n");
	}
	seq_printf(s, "--------\n");

	mutex_unlock(&cd->rw_lock);
	return 0;
}

static int main_parameter_open(struct inode *inode, struct file *file) {
	return single_open(file, colorctrl_main_parameter_read, PDE_DATA(inode));
}

static ssize_t proc_colorctrl_main_parameter_write(struct file *file, const char __user *buffer,
                                                   size_t count, loff_t *ppos) {
	char buf[PAGESIZE] = {0};
	int value_cnt = 0, i = 0, j = 0, val = 0;
	char name[NAME_TAG_SIZE] = {0};
	unsigned int split_value[MAX_PARAMETER] = {0};
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));
	struct color_ctrl_control_para *color_para = NULL;
	struct color_ctrl_control_para *transparent_para = NULL;

	if (!cd || !cd->color_control_para || !cd->transparent_control_para || count >= PAGESIZE) {
		return count;
	}

	if (copy_from_user(buf, buffer, count)) {
		COLOR_INFO("%s:count > %d\n", PAGESIZE);
		return count;
	}
	color_para = cd->color_control_para;
	transparent_para = cd->transparent_control_para;
	value_cnt = colorctrl_str_parse(buf, name, NAME_TAG_SIZE, split_value, MAX_PARAMETER);

	if (value_cnt < 0) {
		COLOR_INFO("str parse failed.");
		return count;
	}

	mutex_lock(&cd->rw_lock);

	if (strstr(name, "thermal_zone_device_weight") && (value_cnt == 5)) {
		for (i = 0; i < 5; i++) {
			cd->thermal_zone_device_weight[i] = split_value[i];
		}
		COLOR_INFO("thermal_zone_device_weight success");
	}

	if(strstr(name, "reset_ftm_vol") && (value_cnt == 3)) {
		for (i = 0; i < 3; i++) {
			cd->reset_ftm_vol[i] = split_value[i];
			COLOR_DEBUG("reset_ftm_vol success is %d", cd->fade_to_red_check_volt[i]);
		}
		COLOR_INFO("reset_ftm_vol success");
	}

	if (strstr(name, "fade_recharge_waitime") && (value_cnt == 1)) {
		cd->fade_recharge_waitime = split_value[0];
		COLOR_INFO("fade_recharge_waitime success");
	} else if (strstr(name, "fade_recharge_waitime") && (value_cnt == 2)) {
		cd->fade_mandatory_flag = split_value[0];
		cd->fade_recharge_waitime = split_value[1];
		colorctrl_reset_hrtimer(cd, cd->fade_recharge_waitime);
		COLOR_INFO("fade_recharge_waitime success,need to retimer -> %d, fade_mandatory_flag is %d",
										cd->fade_recharge_waitime, cd->fade_mandatory_flag);
	} else if (strstr(name, "coloring_recharge_waitime") && (value_cnt == 1)) {
		cd->coloring_recharge_waitime = split_value[0];
		COLOR_INFO("coloring_recharge_waitime success is %d", cd->coloring_recharge_waitime);
	} else if (strstr(name, "coloring_recharge_waitime") && (value_cnt == 2)) {
		val = split_value[0];
		cd->coloring_recharge_waitime = split_value[1];
		colorctrl_reset_hrtimer(cd, cd->coloring_recharge_waitime);
		COLOR_INFO("coloring_recharge_waitime success,need to retimer -> %d, val is %d",
										cd->coloring_recharge_waitime, val);
	} else if (strstr(name, "wait_check_time") && (value_cnt == 2)) {
		cd->wait_check_time[0] = split_value[0];
		cd->wait_check_time[1] = split_value[1];
		COLOR_INFO("wait_check_time success");
	} else if (strstr(name, "fade_open_circuit_vol") && (value_cnt == 3)) {
		for (i = 0; i < 3; i++) {
			cd->fade_open_circuit_vol[i] = split_value[i];
		}
		COLOR_INFO("fade_open_circuit_vol success");
	} else if (strstr(name, "red_open_circuit_vol") && (value_cnt == 3)) {
		for (i = 0; i < 3; i++) {
			cd->red_open_circuit_vol[i] = split_value[i];
		}
		COLOR_INFO("red_open_circuit_vol success");
	} else if (strstr(name, "fade_to_red_retry_cnt") && (value_cnt == 1)) {
		cd->fade_to_red_retry_cnt = split_value[0];
		COLOR_INFO("fade_to_red_retry_cnt success");
	} else if (strstr(name, "red_to_fade_retry_cnt") && (value_cnt == 1)) {
		cd->red_to_fade_retry_cnt = split_value[0];
		COLOR_INFO("red_to_fade_retry_cnt success");
	} else if (strstr(name, "fade_to_red_check_volt") && (value_cnt == 3)) {
		for (i = 0; i < 3; i++) {
			cd->fade_to_red_check_volt[i] = split_value[i];
			COLOR_INFO("red_to_fade_check_volt success is %d", cd->fade_to_red_check_volt[i]);
		}
		COLOR_INFO("fade_to_red_check_volt success");
	} else if (strstr(name, "red_to_fade_check_volt") && (value_cnt == 3)) {
		for (i = 0; i < 3; i++) {
			cd->red_to_fade_check_volt[i] = split_value[i];
			COLOR_INFO("red_to_fade_check_volt success is %d", cd->red_to_fade_check_volt[i]);
		}
	} else if (strstr(name, "temperature_thd") && (value_cnt == cd->temp_num)) {
		for (i = 0; i < cd->temp_num; i++) {
			cd->temperature_thd[i] = split_value[i]-cd->temperature_thd_offect;
			COLOR_INFO("temperature_thd success is %d", cd->temperature_thd[i]);
		}
	} else if (strstr(name, "temperature_thd_offect") && (value_cnt == 1)) {
		cd->temperature_thd_offect = split_value[0];
		COLOR_INFO("temperature_thd_offect success is %d", cd->temperature_thd_offect);
	}

	/*red_charge_para_1 && fade_charge_para_1*/
	if (strstr(name, "red_charge_para_1") && (value_cnt == cd->red_para_1_format[1]+1)) {
		i = split_value[0];
		for (j = 0; j < cd->red_para_1_format[1]; j++) {
			color_para->coloring_charge_para_1[i][j] = split_value[j+1];
			COLOR_INFO(" coloring_charge_para_1[%d][%d] = %d",
				split_value[0], j, split_value[j+1]);
		}
	} else if (strstr(name, "fade_charge_para_1") && (value_cnt == cd->fade_para_1_format[1]+1)) {
		i = split_value[0];
		for (j = 0; j < cd->fade_para_1_format[1]; j++) {
			transparent_para->coloring_charge_para_1[i][j] = split_value[j+1];
			COLOR_DEBUG("fade_charge_para_1[%d][%d] = %d",
				split_value[0], j, split_value[j+1]);
		}
	}

	/*red_charge_para_2 && fade_charge_para_2*/
	if (strstr(name, "red_charge_para_2") && (value_cnt == cd->red_para_2_format[1]+1)) {
		i = split_value[0];
		for (j = 0; j < cd->red_para_2_format[1]; j++) {
			color_para->coloring_charge_para_2[i][j] = split_value[j+1];
			COLOR_DEBUG("coloring_charge_para_2[%d][%d] = %d",
				split_value[0], j, split_value[j+1]);
		}
	} else if (strstr(name, "fade_charge_para_2") && (value_cnt == cd->fade_para_2_format[1]+1)) {
		i = split_value[0];
		for (j = 0; j < cd->fade_para_2_format[1]; j++) {
			transparent_para->coloring_charge_para_2[i][j] = split_value[j+1];
			COLOR_DEBUG("fade_charge_para_2[%d][%d] = %d",
				split_value[0], j, split_value[j+1]);
		}
	}

	mutex_unlock(&cd->rw_lock);
	return count;
}

static const struct file_operations proc_main_parameter_ops = {
	.owner = THIS_MODULE,
	.open  = main_parameter_open,
	.read  = seq_read,
	.write = proc_colorctrl_main_parameter_write,
	.release = single_release,
};

static ssize_t proc_ageing_para_read(struct file *file, char __user *user_buf, size_t count,
                                   loff_t *ppos) {
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));
	struct file *fp = NULL;
	mm_segment_t old_fs;
	ssize_t ret = 0;
	int str_size = 0;
	char page[PAGESIZE] = {0};
	loff_t  pos;
	char *str;

	if (!cd) {
		COLOR_INFO("! cd\n");
		return -1;
	}

	mutex_lock(&cd->rw_lock);

	str = kzalloc(MAX_AGING_VOL_DATA, GFP_KERNEL);
	if (IS_ERR(str) || str == NULL) {
		COLOR_INFO("str is null or err\n");
		kfree(str);
		goto OUT;
	}

	fp = filp_open(AGING_VOL_PATH, O_RDWR | O_CREAT, EC_CHMOD);
	if(IS_ERR(fp)) {
		COLOR_INFO("Can not open file %s\n", AGING_VOL_PATH);
		filp_close(fp, NULL);
		kfree(str);
		goto OUT;
	}

	str_size = fp->f_inode->i_size;
	if (str_size <= 0 ||str_size > MAX_AGING_VOL_DATA) {
		COLOR_INFO("The size of file is invaild: %d, max data is %d\n", str_size, MAX_AGING_VOL_DATA);
		kfree(str);
		filp_close(fp, NULL);
		goto OUT;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = fp->f_pos;
	vfs_read(fp, str, str_size, &pos);
	fp->f_pos = pos;
	set_fs(old_fs);
	filp_close(fp, NULL);

	COLOR_DEBUG("read aging voltage is %smV", str);

	snprintf(page, PAGESIZE - 1, "%s", str);
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
	kfree(str);

	mutex_unlock(&cd->rw_lock);
	COLOR_INFO(" read aging voltage success \n");
	return ret;
OUT:
	mutex_unlock(&cd->rw_lock);
	COLOR_INFO(" read aging voltage error \n");
	return ret;
}

static ssize_t proc_ageing_para_write(struct file *file, const char __user *buffer, size_t count,
                                    loff_t *ppos)
{
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));
	struct file *fp = NULL;
	char buf[PAGESIZE] = {0};
	mm_segment_t old_fs;
	loff_t  pos;
	int aging_vol = 0;
	char *str = NULL;

	if (!cd) {
		COLOR_INFO("! cd\n");
		return -1;
	}

	mutex_lock(&cd->rw_lock);

	if (count > sizeof(buf)) {
		COLOR_INFO("! cd or count over size");
		goto OUT;
	}

	if (copy_from_user(buf, buffer, count)) {
		COLOR_INFO("read proc input error.");
		goto OUT;
	}

	sscanf(buf, "%d", &aging_vol);
	COLOR_DEBUG("aging_vol:%d < MAX_AGING_VOL_DATA:%d", sizeof(aging_vol), MAX_AGING_VOL_DATA);
	if (sizeof(aging_vol) > MAX_AGING_VOL_DATA) {
		COLOR_INFO("aging_vol:%dmV > MAX_AGING_VOL_DATA:%d", sizeof(aging_vol), MAX_AGING_VOL_DATA);
		goto OUT;
	}

	COLOR_DEBUG("write aging_vol is %dmV", aging_vol);

	str = kzalloc(MAX_AGING_VOL_DATA, GFP_KERNEL);
	if (IS_ERR(str) || str == NULL) {
		kfree(str);
		goto OUT;
	}

	snprintf(str, sizeof(aging_vol) + 1, "%d", aging_vol);

	fp = filp_open(AGING_VOL_PATH, O_WRONLY | O_CREAT | O_TRUNC, EC_CHMOD);
	if(IS_ERR(fp)) {
		COLOR_INFO("Can not open file %s\n", AGING_VOL_PATH);
		kfree(str);
		filp_close(fp, NULL);
		goto OUT;
	}

	COLOR_DEBUG("open file success , now to write , path: %s, str:%s, vol: %dmV\n", AGING_VOL_PATH, str, aging_vol);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = fp->f_pos;
	vfs_write(fp, str, strlen(str), &pos);
	fp->f_pos = pos;
	set_fs(old_fs);

	filp_close(fp, NULL);
	kfree(str);

	mutex_unlock(&cd->rw_lock);
	COLOR_INFO(" write aging voltage success \n");
	return count;
OUT:
	mutex_unlock(&cd->rw_lock);
	COLOR_INFO(" write aging voltage failed \n");
	return count;
}

static const struct file_operations proc_ave_aging_vol_ops = {
	.read  = proc_ageing_para_read,
	.write = proc_ageing_para_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static ssize_t proc_ec_debug_level_read(struct file *file, char __user *user_buf, size_t count,
                                   loff_t *ppos) {
	char page[PAGESIZE] = {0};
	ssize_t ret = 0;

	COLOR_DEBUG(" ec_debug is %d \n", ec_debug);
	snprintf(page, PAGESIZE - 1, "%u", ec_debug);
	ret = simple_read_from_buffer(user_buf, count, ppos, page, sizeof(page));
	return ret;
}

static ssize_t proc_ec_debug_level_write(struct file *file, const char __user *buffer, size_t count,
                                    loff_t *ppos)
{
	struct color_ctrl_device *cd = PDE_DATA(file_inode(file));
	char buf[PAGESIZE] = {0};
	int tmp = 0;

	if (!cd) {
		COLOR_INFO("!cd");
		return -1;
	}

	mutex_lock(&cd->rw_lock);

	if (count >= sizeof(buf)) {
		COLOR_INFO("count over size");
		goto OUT;
	}

	if (copy_from_user(buf, buffer, count)) {
		COLOR_INFO("write proc input error.");
		goto OUT;
	}

	if(!sscanf(buf, "%d", &tmp)) {
		COLOR_INFO("write proc input error.");
		goto OUT;
	}

	ec_debug = tmp;
	COLOR_DEBUG(" ec_debug is %d \n", ec_debug);

	mutex_unlock(&cd->rw_lock);
	COLOR_INFO(" write ec_debug_level success \n");
	return count;
OUT:
	mutex_unlock(&cd->rw_lock);
	COLOR_INFO(" write ec_debug_level failed \n");
	return -ENOMEM;
}


static const struct file_operations proc_ec_debug_level_ops = {
	.read  = proc_ec_debug_level_read,
	.write = proc_ec_debug_level_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};

static int colorctrl_init_proc(struct color_ctrl_device *cd) {
	int ret = 0;
	struct proc_dir_entry *prEntry_cr = NULL;
	struct proc_dir_entry *prEntry_tmp = NULL;
	COLOR_INFO("entry");
	/*proc files-step1:/proc/colorctrl*/
	prEntry_cr = proc_mkdir("colorctrl", NULL);

	if (prEntry_cr == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create color ctrl proc entry");
	}

	/*proc files-step2:/proc/colorctrl/color_ctrl (color control interface)*/
	prEntry_tmp = proc_create_data("color_ctrl", EC_CHMOD, prEntry_cr, &proc_colorctrl_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create color_ctrl proc entry");
	}

	/*proc files-step2:/proc/temperature (color control temperature interface)*/
	prEntry_tmp = proc_create_data("temperature", EC_CHMOD, prEntry_cr, &proc_temperature_control_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create temperature proc entry");
	}

	/*proc files-step2:/proc/voltage (color control voltage interface)*/
	prEntry_tmp = proc_create_data("voltage", EC_CHMOD, prEntry_cr, &proc_adc_voltage_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create voltage proc entry");
	}

	/*show main_register interface*/
	prEntry_tmp = proc_create_data("main_parameter", EC_CHMOD, prEntry_cr, &proc_main_parameter_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create main parameter proc entry");
	}

	/* show other status interface*/
	prEntry_tmp = proc_create_data("other_status", EC_CHMOD, prEntry_cr, &proc_other_status_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create adc proc entry");
	}

	/* show back cover color*/
	prEntry_tmp = proc_create_data("cover_color", EC_CHMOD, prEntry_cr, &proc_cover_color_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create adc proc entry");
	}

	/* proc average vol aging data*/
	prEntry_tmp = proc_create_data("ave_aging_vol", EC_CHMOD, prEntry_cr, &proc_ave_aging_vol_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create adc proc entry");
	}

	/*debug level proc*/
	prEntry_tmp = proc_create_data("debug_level", EC_CHMOD, prEntry_cr, &proc_ec_debug_level_ops, cd);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		COLOR_INFO("Couldn't create adc proc entry");
	}

	cd->prEntry_cr = prEntry_cr;
	return ret;
}

static int colorctrl_parse_dt(struct device *dev, struct color_ctrl_device *cd) {
	int ret = 0, i = 0, j = 0;
	int num = 0;
	int prj_id = 0;
	int temp_array[70] = {0};

	struct device_node *np = dev->of_node;
	struct color_ctrl_hw_resource *hw_res = cd->hw_res;
	struct color_ctrl_control_para *color_para = NULL;
	struct color_ctrl_control_para *transparent_para = NULL;

	if (!np || !hw_res) {
		COLOR_INFO("Don't has device of_node.");
		goto OUT;
	}

	color_para = devm_kzalloc(dev, sizeof(struct color_ctrl_control_para), GFP_KERNEL);

	if (!color_para) {
		COLOR_INFO("Malloc memory for color control blue para fail.");
		return -ENOMEM;
	}

	transparent_para = devm_kzalloc(dev, sizeof(struct color_ctrl_control_para), GFP_KERNEL);

	if (!transparent_para) {
		COLOR_INFO("Malloc memory for color control transparent para fail.");
		return -ENOMEM;
	}

	cd->color_control_para = color_para;
	cd->transparent_control_para = transparent_para;
	cd->project_num  = of_property_count_u32_elems(np, "platform_support_project");

	if (cd->project_num <= 0) {
		COLOR_INFO("project not specified, need to config the support project.");
		goto OUT;
	} else {
		ret = of_property_read_u32_array(np, "platform_support_project", cd->platform_support_project,
		                                 cd->project_num);

		if (ret) {
			COLOR_INFO("platform_support_project not specified.");
			goto OUT;
		}

		prj_id = get_project();
		COLOR_INFO("now project [%d]", prj_id);

		cd->pcb_version = get_PCB_Version();
		COLOR_INFO("now pcb_version [%d]", cd->pcb_version);

		for (i = 0; i < cd->project_num; i++) {
			if (prj_id == cd->platform_support_project[i]) {
				COLOR_INFO("project [%d] match OK!", cd->platform_support_project[i]);
				cd->match_project = cd->platform_support_project[i];
				break;
			}

			COLOR_INFO("project [%d] is not match", cd->platform_support_project[i]);
		}

		if (i == cd->project_num) {
			COLOR_INFO("driver does not match the project.");
			goto OUT;
		}
	}

	cd->colorctrl_v2_support =  of_property_read_bool(np, "colorctrl_v2_support");
	cd->ec_step_voltage_support =  of_property_read_bool(np, "ec_step_voltage_support");

	if (cd->colorctrl_v2_support) {
		COLOR_INFO("now is EC 2.0!!");
	}

	cd->thermal_zone_device_num = of_property_count_strings(np, "thermal_zone_device_names");

	if (cd->thermal_zone_device_num <= 0 || cd->thermal_zone_device_num > MAX_THERMAL_ZONE_DEVICE_NUM) {
		COLOR_INFO("thermal zone device num not specified or device num is too large, thermal_zone_device_num : %d.",
		           cd->thermal_zone_device_num);
		goto OUT;
	} else {
		ret = of_property_read_u32_array(np, "thermal_zone_device_weight", cd->thermal_zone_device_weight,
		                                 cd->thermal_zone_device_num + 1);

		if (ret) {
			COLOR_INFO("thermal zone device weight is not specified, please check the dts config.");
			goto OUT;
		}

		ret = of_property_read_u32_array(np, "thermal_zone_device_weight_sigh_bit",
		                                 cd->thermal_zone_device_weight_sigh_bit, cd->thermal_zone_device_num + 1);

		if (ret) {
			COLOR_INFO("thermal zone device weight sigh bit is not specified, please check the dts config.");
			goto OUT;
		}

		for (i = 0; i < cd->thermal_zone_device_num + 1; i++) {
			cd->thermal_zone_device_weight[i] = cd->thermal_zone_device_weight[i] *
			                                    (cd->thermal_zone_device_weight_sigh_bit[i] > 0 ? 1 : -1);
			COLOR_INFO("%d thermal zone device weight is %d.", i, cd->thermal_zone_device_weight[i]);
		}

		for (i = 0; i < cd->thermal_zone_device_num; i++) {
			cd->thermal_zone_device_name[i] = devm_kzalloc(dev, MAX_THERMAL_ZONE_DEVICE_NAME_LEN, GFP_KERNEL);
			ret = of_property_read_string_index(np, "thermal_zone_device_names", i,
			                                    (const char **)&cd->thermal_zone_device_name[i]);

			if (ret) {
				COLOR_INFO("thermal zone device name is not specified, please check the dts config.");
				goto OUT;
			}

			/*Request thermal device*/
			cd->thermal_zone_device[i] = thermal_zone_get_zone_by_name(cd->thermal_zone_device_name[i]);

			if (IS_ERR(cd->thermal_zone_device[i])) {
				ret = PTR_ERR(cd->thermal_zone_device[i]);
				cd->thermal_zone_device[i] = NULL;
				COLOR_INFO("fail to get %s thermal_zone_device: %d", cd->thermal_zone_device_name[i], ret);
				goto OUT;
			} else {
				COLOR_INFO("success get %s thermal_zone_device.", cd->thermal_zone_device_name[i]);
			}
		}
	}

	/*sleep_en pm855b gpio5*/
	hw_res->sleep_en_gpio = of_get_named_gpio(np, "gpio-sleep_en", 0);
	COLOR_INFO("parse gpio-sleep_en .");

	if ((!gpio_is_valid(hw_res->sleep_en_gpio))) {
		COLOR_INFO("parse gpio-sleep_en fail.");
		goto OUT;
	}

	/*si_in_1 pm855a gpio8*/
	hw_res->si_in_1_gpio = of_get_named_gpio(np, "gpio-si_in_1", 0);
	COLOR_INFO("parse gpio-si_in_1  .");

	if ((!gpio_is_valid(hw_res->si_in_1_gpio))) {
		COLOR_INFO("parse gpio-si_in_1 fail.");
		goto OUT;
	}

	/*si_in_2 pm855a gpio11*/
	hw_res->si_in_2_gpio = of_get_named_gpio(np, "gpio-si_in_2", 0);
	COLOR_INFO("parse gpio-si_in_2  .");

	if ((!gpio_is_valid(hw_res->si_in_2_gpio))) {
		COLOR_INFO("parse gpio-si_in_2 fail.");
		goto OUT;
	}

	COLOR_INFO("EC 2.0 new power set start");
	/*enable coloring power*/
	hw_res->vm_coloring_gpio = of_get_named_gpio(np, "enable_coloring_gpio", 0);
	COLOR_INFO("vm_coloring_gpio i.");

	if (!gpio_is_valid(hw_res->vm_coloring_gpio)) {
		COLOR_INFO("vm_coloring_gpio is not specified.");
	}

	hw_res->coloring = devm_regulator_get(dev, "coloring");
	COLOR_INFO("Regulator coloring get  .");

	if (IS_ERR_OR_NULL(hw_res->coloring)) {
		COLOR_INFO("Regulator coloring get failed.");
		goto OUT;
	}

	/*enable fade power*/
	hw_res->vm_fade_gpio = of_get_named_gpio(np, "enable_fade_gpio", 0);
	COLOR_INFO("enable_fade_gpio i .");

	if (!gpio_is_valid(hw_res->vm_fade_gpio)) {
		COLOR_INFO("enable_fade_gpio is not specified.");
	}

	hw_res->fade = devm_regulator_get(dev, "fade");
	COLOR_INFO("Regulator fade get  ");

	if (IS_ERR_OR_NULL(hw_res->fade)) {
		COLOR_INFO("Regulator fade get failed.");
		goto OUT;
	}

	/*enable adc on*/
	hw_res->adc_on_gpio = of_get_named_gpio(np, "gpio-adc_on", 0);
	COLOR_INFO("parse gpio-adc_on  .");

	if ((!gpio_is_valid(hw_res->adc_on_gpio))) {
		COLOR_INFO("parse gpio-adc_on fail.");
		goto OUT;
	}

	/*enable oa en*/
	hw_res->oa_en_gpio = of_get_named_gpio(np, "gpio-oa_en", 0);
	COLOR_INFO("parse gpio-oa_en  .");

	if ((!gpio_is_valid(hw_res->oa_en_gpio))) {
		COLOR_INFO("parse gpio-oa_en fail.");
		goto OUT;
	}

	hw_res->vm_v_chan = devm_iio_channel_get(dev, "colorctrl_voltage_adc");

	if (IS_ERR(hw_res->vm_v_chan)) {
		ret = PTR_ERR(hw_res->vm_v_chan);
		hw_res->vm_v_chan = NULL;
		COLOR_INFO("vm_v_chan get error or voltage read is not support, ret = %d\n", ret);
	} else {
		COLOR_INFO("hw_res->vm_v_chan get success.\n");
	}

	COLOR_DEBUG("Parse V2 dt ok, [sleep_en_gpio:%d][si_in_1_gpio:%d]\
			[ si_in_2_gpio:%d][gpio-adc_on:%d][gpio-oa_en:%d]\
			[enable_coloring_gpio:%d][enable_fade_gpio:%d]",
	           hw_res->sleep_en_gpio, hw_res->si_in_1_gpio, hw_res->si_in_2_gpio, hw_res->adc_on_gpio,
	           hw_res->oa_en_gpio, hw_res->vm_coloring_gpio, hw_res->vm_fade_gpio);

	/*******************para parse********************/

	/*fade_recharge_waitime*/
	ret = of_property_read_u32(np, "fade_recharge_waitime", &cd->fade_recharge_waitime);
	if (ret) {
		COLOR_INFO("fade_recharge_waitime not set ,err quit!!");
		goto OUT;
	}
	COLOR_DEBUG("fade_recharge_waitime [%d]", cd->fade_recharge_waitime);

	/*coloring_recharge_waitime*/
	ret = of_property_read_u32(np, "coloring_recharge_waitime", &cd->coloring_recharge_waitime);
	if (ret) {
		COLOR_INFO("coloring_recharge_waitime not set ,err quit!!");
		goto OUT;
	}
	COLOR_DEBUG("coloring_recharge_waitime [%d]", cd->coloring_recharge_waitime);

	/*wait_check_time*/
	ret = of_property_read_u32_array(np, "wait_check_time", cd->wait_check_time, 2);
	if (ret) {
		COLOR_INFO("intermediate state wait time not set ,err quit!!");
		goto OUT;
	}
	COLOR_DEBUG("wait_check_time-[%d]-[%d]", cd->wait_check_time[0], cd->wait_check_time[1]);

	/*fade_open_circuit_vol = <1200 1450 1550>*/
	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(np, "fade_open_circuit_vol", temp_array, 3);
	} else {
		ret = of_property_read_u32_array(np, "evt1_fade_open_circuit_vol", temp_array, 3);
	}

	if (ret) {
		COLOR_INFO("fade_open_circuit_vol not set ,err quit!!");
		goto OUT;
	}
	for (i = 0; i < 3; i++) {
		cd->fade_open_circuit_vol[i] = temp_array[i];
	}
	COLOR_DEBUG("fade_open_circuit_vol-[%d]-[%d]-[%d]", cd->fade_open_circuit_vol[0],
							cd->fade_open_circuit_vol[1], cd->fade_open_circuit_vol[2]);

	/*red_open_circuit_vol  = <1350 1400 1400>*/
	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(np, "red_open_circuit_vol", temp_array, 3);
	} else {
		ret = of_property_read_u32_array(np, "evt1_red_to_fade_check_volt", temp_array, 3);
	}

	if (ret) {
		COLOR_INFO("red_open_circuit_vol not set ,err quit!!");
		goto OUT;
	}
	for (i = 0; i < 3; i++) {
		cd->red_open_circuit_vol[i] = temp_array[i];
	}
	COLOR_DEBUG("red_open_circuit_vol-[%d]-[%d]-[%d]", cd->red_open_circuit_vol[0],
							cd->red_open_circuit_vol[1], cd->red_open_circuit_vol[2]);

	/*fade_to_red_check_volt = <1550 1780 1820>*/
	ret = of_property_read_u32_array(np, "fade_to_red_check_volt", temp_array, 3);
	if (ret) {
		COLOR_INFO("fade_to_red_check_volt not set ,err quit!!");
		goto OUT;
	}
	for (i = 0; i < 3; i++) {
		cd->fade_to_red_check_volt[i] = temp_array[i];
	}
	COLOR_DEBUG("fade_to_red_check_volt-[%d]-[%d]-[%d]", cd->fade_to_red_check_volt[0],
				cd->fade_to_red_check_volt[1], cd->fade_to_red_check_volt[2]);

	/*fade_to_red_retry_cnt*/
	ret = of_property_read_u32(np, "fade_to_red_retry_cnt", &cd->fade_to_red_retry_cnt);
	if (ret) {
		COLOR_INFO("fade_to_red_retry_cnt not set ,err quit!!");
		goto OUT;
	}
	COLOR_DEBUG("fade_to_red_retry_cnt-[%d]", cd->fade_to_red_retry_cnt);

	/*red_to_fade_check_volt = <1100 1780 1820>*/
	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(np, "red_to_fade_check_volt", temp_array, 3);
	} else {
		ret = of_property_read_u32_array(np, "evt1_red_to_fade_check_volt", temp_array, 3);
	}

	if (ret) {
		COLOR_INFO("red_to_fade_check_volt not set ,err quit!!");
		goto OUT;
	}
	for (i = 0; i < 3; i++) {
		cd->red_to_fade_check_volt[i] = temp_array[i];
	}
	COLOR_DEBUG("red_to_fade_check_volt-[%d]-[%d]-[%d]", cd->red_to_fade_check_volt[0],
				cd->red_to_fade_check_volt[1], cd->red_to_fade_check_volt[2]);

	/*reset_ftm_voltage = <1350 1450 1450>*/
	ret = of_property_read_u32_array(np, "reset_ftm_voltage", temp_array, 3);
	if (ret) {
		COLOR_INFO("reset_ftm_vol not set ,err quit!!");
		goto OUT;
	}
	for (i = 0; i < 3; i++) {
		cd->reset_ftm_vol[i] = temp_array[i];
	}
	COLOR_DEBUG("reset_ftm_vol-[%d]-[%d]-[%d]", cd->reset_ftm_vol[0],
					cd->reset_ftm_vol[1], cd->reset_ftm_vol[2]);

	/*red_to_fade_retry_cnt*/
	ret = of_property_read_u32(np, "red_to_fade_retry_cnt", &cd->red_to_fade_retry_cnt);
	if (ret) {
		COLOR_INFO("red_to_fade_retry_cnt not set ,err quit!!");
		goto OUT;
	}
	COLOR_DEBUG("red_to_fade_retry_cnt-[%d]", cd->red_to_fade_retry_cnt);

	/*temperature_thd_offect*/
	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32(np, "temperature_thd_offect", &cd->temperature_thd_offect);
	} else {
		ret = of_property_read_u32(np, "evt1_temperature_thd_offect", &cd->temperature_thd_offect);
	}

	if (ret) {
		COLOR_INFO("temperature_thd_offect not set ,err quit!!");
		goto OUT;
	}
	COLOR_DEBUG("temperature_thd_offect is %d", cd->temperature_thd_offect);

	/*temperature_thd = <-5000 0 5000 10000 15000 20000 35000 45000 70000>; +15000*/
	if (cd->pcb_version >= PVT) {
		cd->temp_num = of_property_count_u32_elems(np, "temperature_thd");
	} else {
		cd->temp_num = of_property_count_u32_elems(np, "evt1_temperature_thd");
	}

	if (cd->temp_num <= 0) {
		COLOR_INFO("temperature_thd num is null ,err quit!!");
		goto OUT;
	}

	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(np, "temperature_thd", temp_array, cd->temp_num);
	} else {
		ret = of_property_read_u32_array(np, "evt1_temperature_thd", temp_array, cd->temp_num);
	}

	if (ret) {
		COLOR_INFO("temperature_thd not set ,err quit!!");
		goto OUT;
	}
	for (i = 0; i < cd->temp_num; i++) {
		cd->temperature_thd[i] = temp_array[i] - cd->temperature_thd_offect;
	}

	/*coloring_charge_para_1*/
	if (cd->pcb_version >= PVT) {
		cd->red_para_1_num	= of_property_count_u32_elems(np, "red_charge_para_1");
	} else {
		cd->red_para_1_num	= of_property_count_u32_elems(np, "evt1_red_charge_para_1");
	}

	if (cd->red_para_1_num <= 0) {
		COLOR_INFO("red_para_1_num not specified");
		goto OUT;
	}

	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(np, "red_para_1_format", temp_array, 2);
	} else {
		ret = of_property_read_u32_array(np, "evt1_red_para_1_format", temp_array, 2);
	}

	if (ret) {
		COLOR_INFO("red_para_1_format not set ,err quit!!");
		goto OUT;
	} else {
		cd->red_para_1_format[0] = temp_array[0];
		cd->red_para_1_format[1] = temp_array[1];
		COLOR_DEBUG("red_para_1_format is %d, %d", cd->red_para_1_format[0], cd->red_para_1_format[1]);
	}

	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(dev->of_node, "red_charge_para_1", temp_array, cd->red_para_1_num);
	} else {
		ret = of_property_read_u32_array(dev->of_node, "evt1_red_charge_para_1", temp_array, cd->red_para_1_num);
	}

	if (ret) {
		COLOR_INFO("red_charge_para_1 not set ,err quit!!");
		goto OUT;
	} else {
		for(j = 0; j < cd->red_para_1_format[0]; j++) {
			for(i = 0; i < cd->red_para_1_format[1]; i++) {
				color_para->coloring_charge_para_1[j][i] = temp_array[num];
				num++;
			}
		}
		num = 0;
	}

	/*fade_charge_para_1*/
	if (cd->pcb_version >= PVT) {
		cd->fade_para_1_num  = of_property_count_u32_elems(np, "fade_charge_para_1");
	} else {
		cd->fade_para_1_num  = of_property_count_u32_elems(np, "evt1_fade_charge_para_1");
	}

	if (cd->fade_para_1_num <= 0) {
		COLOR_INFO("fade_charge_para_1 not specified");
		goto OUT;
	}

	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(dev->of_node, "fade_para_1_format", temp_array, 2);
	} else {
		ret = of_property_read_u32_array(dev->of_node, "evt1_fade_para_1_format", temp_array, 2);
	}

	if (ret) {
		COLOR_INFO("fade_para_1_format not set ,err quit!!");
		goto OUT;
	} else {
		cd->fade_para_1_format[0] = temp_array[0];
		cd->fade_para_1_format[1] = temp_array[1];
		COLOR_DEBUG("fade_para_1_format is %d, %d", cd->fade_para_1_format[0], cd->fade_para_1_format[1]);
	}

	if (cd->pcb_version >= PVT) {
		ret = of_property_read_u32_array(dev->of_node, "fade_charge_para_1", temp_array, cd->fade_para_1_num);
	} else {
		ret = of_property_read_u32_array(dev->of_node, "evt1_fade_charge_para_1", temp_array, cd->fade_para_1_num);
	}

	if (ret) {
		COLOR_INFO("fade_charge_para_1 not set ,err quit!!");
		goto OUT;
	} else {
		for(j = 0; j < cd->fade_para_1_format[0] ; j++) {
			for(i = 0; i < cd->fade_para_1_format[1] ; i++) {
				transparent_para->coloring_charge_para_1[j][i] = temp_array[num];
				num++;
			}
		}
		num = 0;
	}

	/*coloring_charge_para_2*/

	cd->red_para_2_num  = of_property_count_u32_elems(np, "red_charge_para_2");
	if (cd->red_para_2_num <= 0) {
		COLOR_INFO("red_para_2_num not specified");
		goto OUT;
	}

	ret = of_property_read_u32_array(dev->of_node, "red_para_2_format", temp_array, 2);
	if (ret) {
		COLOR_INFO("red_para_2_format not set ,err quit!!");
		goto OUT;
	} else {
		cd->red_para_2_format[0] = temp_array[0];
		cd->red_para_2_format[1] = temp_array[1];
		COLOR_DEBUG("red_para_2_format is %d, %d", cd->red_para_2_format[0], cd->red_para_2_format[1]);
	}

	ret = of_property_read_u32_array(dev->of_node, "red_charge_para_2", temp_array, cd->red_para_2_num);
	if (ret) {
		COLOR_INFO("coloring_charge_para_2 not set ,err quit!!");
		goto OUT;
	} else {
		for(j = 0; j < cd->red_para_2_format[0]; j++) {
			for(i = 0; i < cd->red_para_2_format[1]; i++) {
				color_para->coloring_charge_para_2[j][i] = temp_array[num];
				num++;
			}
		}
		num = 0;
	}

	/*fade_charge_para_2*/
	cd->fade_para_2_num  = of_property_count_u32_elems(np, "fade_charge_para_2");
	if (cd->fade_para_2_num <= 0) {
		COLOR_INFO("fade_para_2_num not specified");
		goto OUT;
	}

	ret = of_property_read_u32_array(dev->of_node, "fade_para_2_format", temp_array, 2);
	if (ret) {
		COLOR_INFO("fade_para_2_format not set ,err quit!!");
		goto OUT;
	} else {
		cd->fade_para_2_format[0] = temp_array[0];
		cd->fade_para_2_format[1] = temp_array[1];
		COLOR_DEBUG("fade_para_2_format is %d, %d", cd->fade_para_2_format[0], cd->fade_para_2_format[1]);
	}

	ret = of_property_read_u32_array(dev->of_node, "fade_charge_para_2", temp_array, cd->fade_para_2_num);
	if (ret) {
		COLOR_INFO("fade_charge_para_2 not set ,err quit!!");
		goto OUT;
	} else {
		for(j = 0; j < cd->fade_para_2_format[0]; j++) {
			for(i = 0; i < cd->fade_para_2_format[1]; i++) {
				transparent_para->coloring_charge_para_2[j][i] = temp_array[num];
				num++;
			}
		}
		num = 0;
	}

	return 0;
OUT:
	return -1;
}

static int get_init_status(struct color_ctrl_device *cd) {
	int vm_volt;
	int ret;
	vm_volt = check_adc_status(cd);
	ret = vm_volt - 1800;
	return ret;
}

static int colorctrl_probe(struct platform_device *pdev) {
	int ret = 0;
	int vol_mv = 0;
	struct color_ctrl_hw_resource *hw_res = NULL;
	struct color_ctrl_device *cd = NULL;
	COLOR_INFO("start to probe color ctr driver.");

	/*malloc memory for hardware resource */
	if (pdev->dev.of_node) {
		hw_res = devm_kzalloc(&pdev->dev, sizeof(struct color_ctrl_hw_resource), GFP_KERNEL);

		if (!hw_res) {
			ret = -ENOMEM;
			COLOR_INFO("Malloc memory for hardware resoure fail.");
			goto PROBE_ERR;
		}
	} else {
		hw_res = pdev->dev.platform_data;
	}

	/*malloc memory for color ctrl device*/
	cd = devm_kzalloc(&pdev->dev, sizeof(struct color_ctrl_device), GFP_KERNEL);

	if (!cd) {
		COLOR_INFO("Malloc memory for color ctr device fail.");
		ret = -ENOMEM;
		goto PROBE_ERR;
	}

	cd->hw_res = hw_res;
	ret = colorctrl_parse_dt(&pdev->dev, cd);

	if (ret) {
		COLOR_INFO("parse dts fail.");
		goto PROBE_ERR;
	}

	/*Request and config these gpios*/
	if (gpio_is_valid(hw_res->sleep_en_gpio)) {
		ret = devm_gpio_request(&pdev->dev, hw_res->sleep_en_gpio, "sleep_en_gpio");

		if (ret) {
			COLOR_INFO("request sleep_en_gpio fail.");
			goto PROBE_ERR;
		} else {
			/*Enable the sleep en gpio.*/
			ret = gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);

			if (ret) {
				COLOR_INFO("Config sleep_en_gpio gpio output direction fail.");
				goto PROBE_ERR;
			}
		}
	} else {
		hw_res->sleep_en_gpio = -EINVAL;
		COLOR_INFO("sleep_en_gpio gpio is invalid.");
		goto PROBE_ERR;
	}

	if (gpio_is_valid(hw_res->si_in_1_gpio)) {
		ret = devm_gpio_request(&pdev->dev, hw_res->si_in_1_gpio, "si_in_1_gpio");

		if (ret) {
			COLOR_INFO("request si_in_1_gpio fail.");
			goto PROBE_ERR;
		} else {
			ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);

			if (ret) {
				COLOR_INFO("Config si_in_1_gpio gpio output direction fail.");
				goto PROBE_ERR;
			}
		}
	} else {
		hw_res->si_in_1_gpio = -EINVAL;
		COLOR_INFO("si_in_1_gpio gpio is invalid.");
		goto PROBE_ERR;
	}

	if (gpio_is_valid(hw_res->si_in_2_gpio)) {
		ret = devm_gpio_request(&pdev->dev, hw_res->si_in_2_gpio, "si_in_2_gpio");

		if (ret) {
			COLOR_INFO("request si_in_2_gpio fail.");
			goto PROBE_ERR;
		} else {
			ret = gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);

			if (ret) {
				COLOR_INFO("Config si_in_2_gpio gpio output direction fail.");
				goto PROBE_ERR;
			}
		}
	} else {
		hw_res->si_in_2_gpio = -EINVAL;
		COLOR_INFO("si_in_2_gpio gpio is invalid.");
		goto PROBE_ERR;
	}

	if (gpio_is_valid(hw_res->vm_coloring_gpio)) {
		ret = devm_gpio_request(&pdev->dev, hw_res->vm_coloring_gpio, "vm_coloring_gpio");

		if (ret) {
			COLOR_INFO("request vm_coloring_gpio fail.");
			goto PROBE_ERR;
		} else {
			ret = gpio_direction_output(hw_res->vm_coloring_gpio, GPIO_LOW);

			if (ret) {
				COLOR_INFO("Config vm_coloring_gpio gpio output direction fail.");
				goto PROBE_ERR;
			}
		}
	}

	if (gpio_is_valid(hw_res->vm_fade_gpio)) {
		ret = devm_gpio_request(&pdev->dev, hw_res->vm_fade_gpio, "vm_fade_gpio");

		if (ret) {
			COLOR_INFO("request vm_fade_gpio fail.");
			goto PROBE_ERR;
		} else {
			ret = gpio_direction_output(hw_res->vm_fade_gpio, GPIO_LOW);

			if (ret) {
				COLOR_INFO("Config vm_fade_gpio gpio output direction fail.");
				goto PROBE_ERR;
			}
		}
	}

	if (gpio_is_valid(hw_res->adc_on_gpio)) {
		ret = devm_gpio_request(&pdev->dev, hw_res->adc_on_gpio, "adc_on_gpio");

		if (ret) {
			COLOR_INFO("request adc_on_gpio fail.");
			goto PROBE_ERR;
		} else {
			ret = gpio_direction_output(hw_res->adc_on_gpio, GPIO_LOW);

			if (ret) {
				COLOR_INFO("Config adc_on_gpio  output direction fail.");
				goto PROBE_ERR;
			}
		}
	} else {
		hw_res->adc_on_gpio = -EINVAL;
		COLOR_INFO("adc_on_gpio gpio is invalid.");
		goto PROBE_ERR;
	}

	if (gpio_is_valid(hw_res->oa_en_gpio)) {
		ret = devm_gpio_request(&pdev->dev, hw_res->oa_en_gpio, "oa_en_gpio");

		if (ret) {
			COLOR_INFO("request oa_en_gpio fail.");
			goto PROBE_ERR;
		} else {
			ret = gpio_direction_output(hw_res->oa_en_gpio, GPIO_LOW);

			if (ret) {
				COLOR_INFO("Config oa_en_gpio gpio output direction fail.");
				goto PROBE_ERR;
			}
		}
	} else {
		hw_res->oa_en_gpio = -EINVAL;
		COLOR_INFO("oa_en_gpio gpio is invalid.");
		goto PROBE_ERR;
	}

	/*setup color control device hrtimer*/
	hrtimer_init(&cd->hrtimer, CLOCK_BOOTTIME, HRTIMER_MODE_REL);
	cd->hrtimer.function = colorctrl_hrtimer_handler;
	cd->recharge_wq = create_singlethread_workqueue("recharge_wq");

	if (!cd->recharge_wq) {
		ret = -ENOMEM;
		goto PROBE_ERR;
	}

	INIT_WORK(&cd->recharge_work, colorctrl_recharge_work);
	cd->pdev = pdev;
	cd->dev = &pdev->dev;
	cd->color_status = UNKNOWN;
	cd->temp_status = UNKNOWN;
	cd->need_recharge = true;
	cd->ageing_fade_para2 = false;
	cd->ageing_color_para2 = false;
	cd->is_first_boot = false;
	mutex_init(&cd->rw_lock);
	platform_set_drvdata(pdev, cd);
	ret = colorctrl_init_proc(cd);

	if (ret) {
		COLOR_INFO("creat color ctrl proc error.");
		goto PROBE_ERR;
	}

	vol_mv = get_init_status(cd);
	COLOR_INFO("color ctrl device probe : normal end, now default is %d mV. color_status: %d",
			vol_mv, cd->color_status);
	return ret;
PROBE_ERR:
	COLOR_INFO("color ctrl device probe error.");
	return ret;
}

static int colorctrl_remove(struct platform_device *dev) {
	struct color_ctrl_device *cd = platform_get_drvdata(dev);
	COLOR_INFO("start remove the color ctrl platform dev.");

	if (cd) {
		proc_remove(cd->prEntry_cr);
		cd->prEntry_cr = NULL;
	}

	return 0;
}

static struct platform_driver colorctrl_driver = {
	.probe = colorctrl_probe,
	.remove = colorctrl_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name  = DRIVER_NAME,
		.of_match_table = colorctrl_match_table,
	},
};

static int __init colorctrl_driver_init(void) {
	return platform_driver_register(&colorctrl_driver);
}

static void __exit colorctrl_driver_exit(void) {
	return platform_driver_unregister(&colorctrl_driver);
}

late_initcall(colorctrl_driver_init);
module_exit(colorctrl_driver_exit);

/*module_platform_driver(colorctrl_driver);*/

MODULE_DESCRIPTION("Color Ctrl Driver Module");
MODULE_AUTHOR("Xuhang.Li");
MODULE_LICENSE("GPL v2");
