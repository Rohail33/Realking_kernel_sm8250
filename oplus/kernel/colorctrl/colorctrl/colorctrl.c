// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 All rights reserved.
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
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/consumer.h>
#include <soc/oplus/system/oplus_project.h>
#include "colorctrl.h"

#define DRIVER_NAME "color-ctrl"

static void colorctrl_blue_recharge_operation(struct color_ctrl_device *cd);
static void colorctrl_transparent_recharge_operation(struct color_ctrl_device *cd);

static const struct of_device_id colorctrl_match_table[] = {
    {.compatible = "oplus,color-ctrl"},
    {}
};

static void colorctrl_msleep(int time)
{
    COLOR_INFO("wait for time : %d(ms).", time);
    mdelay(time);
}

static void colorctrl_set_awake(struct color_ctrl_device *cd, bool awake)
{
    static bool pm_flag = false;

    if (!cd || !cd->ws) {
        return;
    }

    COLOR_INFO("set pm awake : %s .", awake ? "true" : "false");

    if (awake && !pm_flag) {
        pm_flag = true;
        __pm_stay_awake(cd->ws);
    } else if (!awake && pm_flag) {
        __pm_relax(cd->ws);
        pm_flag = false;
    }
}

static int colorctrl_power_control(struct color_ctrl_hw_resource *hw_res, int vol, bool on)
{
    int ret = 0;

    if (on) {
        if (!IS_ERR_OR_NULL(hw_res->vm)) {
            COLOR_INFO("enable the vm power to voltage : %d(mV).", vol);
            if (vol) {
                ret = regulator_set_voltage(hw_res->vm, vol * UV_PER_MV, vol * UV_PER_MV);
                if (ret) {
                    COLOR_INFO("Regulator vm set voltage failed, ret = %d", ret);
                    return ret;
                }
            }
            ret = regulator_enable(hw_res->vm);
            if (ret) {
                COLOR_INFO("Regulator vm enable failed, ret = %d", ret);
                return ret;
            }
            if (gpio_is_valid(hw_res->vm_enable_gpio)) {
                COLOR_INFO("enable vm_enable_gpio.");
                ret = gpio_direction_output(hw_res->vm_enable_gpio, GPIO_HIGH);
                if(ret) {
                    COLOR_INFO("enable vm_enable_gpio fail.");
                    return ret;
                }
            }
        }
    } else {
        if (!IS_ERR_OR_NULL(hw_res->vm)) {
            if (gpio_is_valid(hw_res->vm_enable_gpio)) {
                COLOR_INFO("disable vm_enable_gpio.");
                ret = gpio_direction_output(hw_res->vm_enable_gpio, GPIO_LOW);
                if(ret) {
                    COLOR_INFO("disable vm_enable_gpio fail.");
                    return ret;
                }
            }
            COLOR_INFO("disable the vm power.");
            ret = regulator_disable(hw_res->vm);
            if (ret) {
                COLOR_INFO("Regulator vm disable failed, ret = %d", ret);
                return ret;
            }
        }
    }

    return ret;
}

static void colorctrl_recharge_work(struct work_struct *work)
{
    struct color_ctrl_device *cd = container_of(work, struct color_ctrl_device, recharge_work);

    if (!cd) {
        COLOR_INFO("no dev find, return.");
        return;
    }
    COLOR_INFO("is call.");

    if (!cd->need_recharge) {
        COLOR_INFO("not need to do the recharge work due to force close.");
        return;
    }

    colorctrl_set_awake(cd, true);
    mutex_lock(&cd->rw_lock);

    if (cd->color_status == BLUE) {
        colorctrl_blue_recharge_operation(cd);
    } else if (cd->color_status == TRANSPARENT) {
        colorctrl_transparent_recharge_operation(cd);
    } else {
        COLOR_INFO("not need to do the recharge work, color_status : %d.", cd->color_status);
    }

    mutex_unlock(&cd->rw_lock);
    colorctrl_set_awake(cd, false);
}

static void colorctrl_reset_hrtimer(struct color_ctrl_device *cd)
{
    if (!cd) {
        COLOR_INFO("no dev find, return.");
        return;
    }

    COLOR_INFO("reset hrtimer expires time : %d(s).", cd->recharge_time);
    hrtimer_cancel(&cd->hrtimer);
    hrtimer_start(&cd->hrtimer, ktime_set(cd->recharge_time, 0), HRTIMER_MODE_REL);
}

static enum hrtimer_restart colorctrl_hrtimer_handler(struct hrtimer *timer)
{
    struct color_ctrl_device *cd = container_of(timer, struct color_ctrl_device, hrtimer);

    if (!cd) {
        COLOR_INFO("no dev find, return.");
        return HRTIMER_NORESTART;
    }

    COLOR_INFO("is call.");

    queue_work(cd->recharge_wq, &cd->recharge_work);
    //hrtimer_forward(timer, timer->base->get_time(), ktime_set(cd->recharge_time, 0));

    return HRTIMER_NORESTART;
}

static int colorctrl_get_fitting_temperature(struct color_ctrl_device *cd, s64 *fitting_temp)
{
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
        } else {
            COLOR_INFO("current %s temperature is : %d", cd->thermal_zone_device_name[i], temp);
        }
        *fitting_temp += (s64)temp * cd->thermal_zone_device_weight[i];
    }

    *fitting_temp += cd->thermal_zone_device_weight[cd->thermal_zone_device_num];
    *fitting_temp = *fitting_temp / 100000;
    COLOR_INFO("fitting temperature is : %d", *fitting_temp);

    return 0;
}

static void colorctrl_update_temperature_status(struct color_ctrl_device *cd)
{
    s64 temp = 0;
    int ret = 0;

    if (!cd) {
        COLOR_INFO("no dev or resources find, return.");
        return;
    }

    ret = colorctrl_get_fitting_temperature(cd, &temp);
    if (ret < 0) {
        COLOR_INFO("failed to get fitting temperature");
        return;
    }

    if (temp <= cd->temp_thd_2 && temp > cd->temp_thd_1) {
        COLOR_INFO("it's temperature range (1)");
        cd->temp_status = TEMP_RANGE_1;
    } else if (temp <= cd->temp_thd_3 && temp > cd->temp_thd_2) {
        COLOR_INFO("it's temperature range (2)");
        cd->temp_status = TEMP_RANGE_2;
    } else if (temp < cd->temp_thd_4 && temp > cd->temp_thd_3) {
        COLOR_INFO("it's temperature range (3)");
        cd->temp_status = TEMP_RANGE_3;
    } else if (temp < cd->temp_thd_5 && temp > cd->temp_thd_4) {
        COLOR_INFO("it's temperature range (4)");
        cd->temp_status = TEMP_RANGE_4;
    } else if (temp < cd->temp_thd_6 && temp > cd->temp_thd_5) {
        COLOR_INFO("it's temperature range (5)");
        cd->temp_status = TEMP_RANGE_5;
    } else if (temp < cd->temp_thd_7 && temp > cd->temp_thd_6) {
        COLOR_INFO("it's temperature range (6)");
        cd->temp_status = TEMP_RANGE_6;
    } else {
        COLOR_INFO("it's abnormal temperature now");
        cd->temp_status = ABNORMAL_TEMP;
    }
}

static void colorctrl_blue_recharge_operation(struct color_ctrl_device *cd)
{
    struct color_ctrl_hw_resource *hw_res = NULL;
    int ret = 0, vm_volt = 0, i = 0;
    int charge_time = 0;
    int charge_volt = 0;
    struct color_ctrl_control_para *para = NULL;

    if (!cd || !cd->hw_res || !cd->blue_control_para) {
        COLOR_INFO("no dev or resources find, return.");
        return;
    }

    colorctrl_update_temperature_status(cd);
    if (cd->temp_status == ABNORMAL_TEMP) {
        COLOR_INFO("abnormal temperature occur, can not do recharge operation");
        colorctrl_reset_hrtimer(cd);
        return;
    }

    hw_res = cd->hw_res;
    para = cd->blue_control_para;

    charge_volt = para->charge_volt[cd->temp_status];
    charge_time = para->charge_time[cd->temp_status];

    ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_HIGH);
    ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    if (ret) {
        COLOR_INFO("config gpio status failed.");
        goto OUT;
    }
    colorctrl_msleep(2);
    ret = iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
    if (ret < 0) {
        COLOR_INFO("iio_read_channel_processed get error ret = %d", ret);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        goto OUT;
    }
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    vm_volt = vm_volt / UV_PER_MV;
    COLOR_INFO("current volt : %d(mV).", vm_volt);

    if (vm_volt < para->recharge_volt_thd_1) {
        COLOR_INFO("volt is too low, try to do normal charging.");
        colorctrl_power_control(cd->hw_res, charge_volt, true);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(charge_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        colorctrl_power_control(cd->hw_res, 0, false);
    } else if (vm_volt >= para->recharge_volt_thd_1 && vm_volt < para->recharge_volt_thd_2) {
        COLOR_INFO("volt is too low, try to do recharging.");
        colorctrl_power_control(cd->hw_res, para->recharge_volt, true);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(para->recharge_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        colorctrl_power_control(cd->hw_res, 0, false);
    } else {
        COLOR_INFO("no need to do recharging.");
        cd->color_status = BLUE;
        colorctrl_reset_hrtimer(cd);
        goto OUT;
    }

    colorctrl_msleep(cd->volt_measure_interval_time);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    colorctrl_msleep(2);
    iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    vm_volt = vm_volt / UV_PER_MV;
    COLOR_INFO("current volt after recharging: %d(mV).", vm_volt);

    for (i = 0; i < 5; i++) {
        if (vm_volt < cd->open_circuit_thd) {
            COLOR_INFO("volt is too low, try to do normal charging.");
            colorctrl_power_control(cd->hw_res, charge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(charge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->blue_short_circuit_thd) {
            COLOR_INFO("volt is too low, try to do recharging.");
            colorctrl_power_control(cd->hw_res, para->recharge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(para->recharge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->blue_short_circuit_thd) {
            break;
        }

        colorctrl_msleep(cd->volt_measure_interval_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(2);
        iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        vm_volt = vm_volt / UV_PER_MV;
        COLOR_INFO("volt : %d(mV), rety %d times.", vm_volt, i + 1);
    }

    if (vm_volt < cd->open_circuit_thd) {
        cd->color_status = OPEN_CIRCUIT;
        COLOR_INFO("open circuit fault detected.");
        ret = -1;
    } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->blue_short_circuit_thd) {
        cd->color_status = SHORT_CIRCUIT;
        COLOR_INFO("short circuit fault detected.");
        ret = -1;
    } else {
        cd->color_status = BLUE;
        colorctrl_reset_hrtimer(cd);
    }

OUT:
    gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    COLOR_INFO("recharge color to blue %s.", ret < 0 ? "failed" : "success");

    return;
}

static void colorctrl_blue_operation(struct color_ctrl_device *cd, bool is_ftm)
{
    struct color_ctrl_hw_resource *hw_res = NULL;
    int ret = 0, vm_volt = 0, i = 0;
    int charge_time = 0;
    int charge_volt = 0;
    struct color_ctrl_control_para *para = NULL;

    if (!cd || !cd->hw_res || !cd->blue_control_para) {
        COLOR_INFO("no dev or resources find, return.");
        return;
    }

    if (cd->color_status == BLUE) {
        COLOR_INFO("device is already in blue status.");
        return;
    }

    colorctrl_update_temperature_status(cd);
    if (cd->temp_status == ABNORMAL_TEMP) {
        COLOR_INFO("abnormal temperature occur, can not do normal charge operation");
        return;
    }

    hw_res = cd->hw_res;
    para = cd->blue_control_para;

    charge_volt = para->charge_volt[cd->temp_status];
    charge_time = para->charge_time[cd->temp_status];

    ret = colorctrl_power_control(cd->hw_res, charge_volt, true);
    if (ret) {
        COLOR_INFO("enable power failed.");
        goto OUT;
    }

    ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_HIGH);
    ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    colorctrl_msleep(charge_time);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    if (ret) {
        COLOR_INFO("config gpio status failed.");
        colorctrl_power_control(cd->hw_res, 0, false);
        goto OUT;
    }
    colorctrl_power_control(cd->hw_res, 0, false);

    if (is_ftm) {
        colorctrl_reset_hrtimer(cd);
        cd->color_status = BLUE;
        COLOR_INFO("ftm mode operation, no need to do voltage detection.");
        goto OUT;
    }

    colorctrl_msleep(cd->volt_measure_interval_time);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    colorctrl_msleep(2);
    ret = iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
    if (ret < 0) {
        COLOR_INFO("iio_read_channel_processed get error ret = %d", ret);
        goto OUT;
    }
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    vm_volt = vm_volt / UV_PER_MV;
    COLOR_INFO("current volt : %d(mV).", vm_volt);

    for (i = 0; i < 5; i++) {
        if (vm_volt < cd->open_circuit_thd) {
            COLOR_INFO("volt is too low, try to do normal charging.");
            colorctrl_power_control(cd->hw_res, charge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(charge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->blue_short_circuit_thd) {
            COLOR_INFO("volt is too low, try to do recharging.");
            colorctrl_power_control(cd->hw_res, para->recharge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(para->recharge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->blue_short_circuit_thd) {
            break;
        }

        colorctrl_msleep(cd->volt_measure_interval_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(2);
        iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        vm_volt = vm_volt / UV_PER_MV;
        COLOR_INFO("volt : %d(mV), rety %d times.", vm_volt, i + 1);
    }

    if (vm_volt < cd->open_circuit_thd) {
        cd->color_status = OPEN_CIRCUIT;
        COLOR_INFO("open circuit fault detected.");
        ret = -1;
    } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->blue_short_circuit_thd) {
        cd->color_status = SHORT_CIRCUIT;
        COLOR_INFO("short circuit fault detected.");
        ret = -1;
    } else {
        cd->color_status = BLUE;
        colorctrl_reset_hrtimer(cd);
    }

OUT:
    gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    COLOR_INFO("change color to blue %s.", ret < 0 ? "failed" : "success");

    return;
}

static void colorctrl_transparent_recharge_operation(struct color_ctrl_device *cd)
{
    struct color_ctrl_hw_resource *hw_res = NULL;
    int ret = 0, vm_volt = 0, i = 0;
    int charge_time = 0;
    int charge_volt = 0;
    struct color_ctrl_control_para *para = NULL;

    if (!cd || !cd->hw_res || !cd->transparent_control_para) {
        COLOR_INFO("no dev or resources find, return.");
        return;
    }

    colorctrl_update_temperature_status(cd);
    if (cd->temp_status == ABNORMAL_TEMP) {
        COLOR_INFO("abnormal temperature occur, can not do recharge operation");
        colorctrl_reset_hrtimer(cd);
        return;
    }

    hw_res = cd->hw_res;
    para = cd->transparent_control_para;

    charge_volt = para->charge_volt[cd->temp_status];
    charge_time = para->charge_time[cd->temp_status];

    ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
    ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_HIGH);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    if (ret) {
        COLOR_INFO("config gpio status failed.");
        goto OUT;
    }
    colorctrl_msleep(2);
    ret = iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
    if (ret < 0) {
        COLOR_INFO("iio_read_channel_processed get error ret = %d", ret);
        goto OUT;
    }
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    vm_volt = vm_volt / UV_PER_MV;
    COLOR_INFO("current volt : %d(mV).", vm_volt);

    if (vm_volt < para->recharge_volt_thd_1) {
        COLOR_INFO("volt is too low, try to do normal charging.");
        colorctrl_power_control(cd->hw_res, charge_volt, true);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(charge_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        colorctrl_power_control(cd->hw_res, 0, false);
    } else if (vm_volt >= para->recharge_volt_thd_1 && vm_volt < para->recharge_volt_thd_2) {
        COLOR_INFO("volt is too low, try to do recharging.");
        colorctrl_power_control(cd->hw_res, para->recharge_volt, true);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(para->recharge_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        colorctrl_power_control(cd->hw_res, 0, false);
    } else {
        COLOR_INFO("no need to do recharging.");
        cd->color_status = TRANSPARENT;
        colorctrl_reset_hrtimer(cd);
        goto OUT;
    }

    colorctrl_msleep(cd->volt_measure_interval_time);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    colorctrl_msleep(2);
    iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    vm_volt = vm_volt / UV_PER_MV;
    COLOR_INFO("current volt after recharging: %d(mV).", vm_volt);

    for (i = 0; i < 5; i++) {
        if (vm_volt < cd->open_circuit_thd) {
            COLOR_INFO("volt is too low, try to do normal charging.");
            colorctrl_power_control(cd->hw_res, charge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(charge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->transparent_short_circuit_thd) {
            COLOR_INFO("volt is too low, try to do recharging.");
            colorctrl_power_control(cd->hw_res, para->recharge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(para->recharge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->transparent_short_circuit_thd) {
            break;
        }

        colorctrl_msleep(cd->volt_measure_interval_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(2);
        iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        vm_volt = vm_volt / UV_PER_MV;
        COLOR_INFO("volt : %d(mV), rety %d times.", vm_volt, i + 1);
    }

    if (vm_volt < cd->open_circuit_thd) {
        cd->color_status = OPEN_CIRCUIT;
        COLOR_INFO("open circuit fault detected.");
        ret = -1;
    } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->transparent_short_circuit_thd) {
        cd->color_status = SHORT_CIRCUIT;
        COLOR_INFO("short circuit fault detected.");
        ret = -1;
    } else {
        cd->color_status = TRANSPARENT;
        colorctrl_reset_hrtimer(cd);
    }

OUT:
    gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    COLOR_INFO("recharge color to transparent %s.", ret < 0 ? "failed" : "success");

    return;
}

static void colorctrl_transparent_operation(struct color_ctrl_device *cd, bool is_ftm)
{
    struct color_ctrl_hw_resource *hw_res = NULL;
    int ret = 0, vm_volt = 0, i = 0;
    int charge_time = 0;
    int charge_volt = 0;
    struct color_ctrl_control_para *para = NULL;

    if (!cd || !cd->hw_res || !cd->transparent_control_para) {
        COLOR_INFO("no dev or resources find, return.");
        return;
    }

    if (cd->color_status == TRANSPARENT) {
        COLOR_INFO("device is already in transparent status.");
        return;
    }

    colorctrl_update_temperature_status(cd);
    if (cd->temp_status == ABNORMAL_TEMP) {
        COLOR_INFO("abnormal temperature occur, can not do normal charge operation");
        return;
    }

    hw_res = cd->hw_res;
    para = cd->transparent_control_para;

    charge_volt = para->charge_volt[cd->temp_status];
    charge_time = para->charge_time[cd->temp_status];

    ret = colorctrl_power_control(cd->hw_res, charge_volt, true);
    if (ret) {
        COLOR_INFO("enable power failed.");
        goto OUT;
    }

    ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
    ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_HIGH);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    colorctrl_msleep(charge_time);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    if (ret) {
        COLOR_INFO("Config gpio status failed.");
        colorctrl_power_control(cd->hw_res, 0, false);
        goto OUT;
    }
    colorctrl_power_control(cd->hw_res, 0, false);

    if (is_ftm) {
        cd->color_status = TRANSPARENT;
        colorctrl_reset_hrtimer(cd);
        COLOR_INFO("ftm mode operation, no need to do voltage detection.");
        goto OUT;
    }

    colorctrl_msleep(cd->volt_measure_interval_time);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    colorctrl_msleep(2);
    ret = iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
    if (ret < 0) {
        COLOR_INFO("iio_read_channel_processed get error ret = %d", ret);
        goto OUT;
    }
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    vm_volt = vm_volt / UV_PER_MV;
    COLOR_INFO("current volt : %d(mV).", vm_volt);

    for (i = 0; i < 5; i++) {
        if (vm_volt < cd->open_circuit_thd) {
            COLOR_INFO("volt is too low, try to do normal charging.");
            colorctrl_power_control(cd->hw_res, charge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(charge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->transparent_short_circuit_thd) {
            COLOR_INFO("volt is too low, try to do recharging.");
            colorctrl_power_control(cd->hw_res, para->recharge_volt, true);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
            colorctrl_msleep(para->recharge_time);
            gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            colorctrl_power_control(cd->hw_res, 0, false);
        } else if (vm_volt >= cd->transparent_short_circuit_thd) {
            break;
        }

        colorctrl_msleep(cd->volt_measure_interval_time);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        colorctrl_msleep(2);
        iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
        gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
        vm_volt = vm_volt / UV_PER_MV;
        COLOR_INFO("volt : %d(mV), rety %d times.", vm_volt, i + 1);
    }

    if (vm_volt < cd->open_circuit_thd) {
        cd->color_status = OPEN_CIRCUIT;
        COLOR_INFO("open circuit fault detected.");
        ret = -1;
    } else if (vm_volt >= cd->open_circuit_thd && vm_volt < cd->transparent_short_circuit_thd) {
        cd->color_status = SHORT_CIRCUIT;
        COLOR_INFO("short circuit fault detected.");
        ret = -1;
    } else {
        cd->color_status = TRANSPARENT;
        colorctrl_reset_hrtimer(cd);
    }

OUT:
    gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    COLOR_INFO("change color to transparent %s.", ret < 0 ? "failed" : "success");

    return;
}

static void colorctrl_light_blue_operation(struct color_ctrl_device *cd)
{
    struct color_ctrl_hw_resource *hw_res = NULL;
    int ret = 0;

    if (!cd || !cd->hw_res) {
        COLOR_INFO("No dev or resources find, return.");
        return;
    }

    if (cd->color_status == LIGHT_BLUE) {
        COLOR_INFO("device is already in light blue status.");
        return;
    }

    hw_res = cd->hw_res;

    ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_HIGH);
    ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_HIGH);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
    colorctrl_msleep(cd->intermediate_wait_time);
    ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
    ret |= gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
    ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
    if (ret) {
        COLOR_INFO("Config gpio status failed.");
    } else {
        cd->color_status = LIGHT_BLUE;
    }
    COLOR_INFO("change color to light blue %s.", ret < 0 ? "failed" : "success");

    return;
}

static ssize_t proc_colorctrl_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[8] = {0};
    int temp = 0;
    struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

    if (!cd || count > 2) {
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        COLOR_INFO("read proc input error.");
        return count;
    }
    sscanf(buf, "%d", &temp);
    COLOR_INFO("write value: %d.", temp);

    if (temp > MAX_CTRL_TYPE) {
        COLOR_INFO("not support change color type.");
        return count;
    }

    if (cd->color_status == OPEN_CIRCUIT || cd->color_status == SHORT_CIRCUIT) {
        COLOR_INFO("device is in bad status, can not do any operation, color_status : %d", cd->color_status);
        return count;
    }

    colorctrl_set_awake(cd, true);
    mutex_lock(&cd->rw_lock);

    switch (temp) {
    case LIGHT_BLUE_FTM : {
        colorctrl_light_blue_operation(cd);
        break;
    }
    case BLUE_FTM : {
        colorctrl_blue_operation(cd, true);
        break;
    }
    case TRANSPARENT_FTM : {
        colorctrl_transparent_operation(cd, true);
        break;
    }
    case BLUE_NORMAL: {
        if (cd->color_status !=  UNKNOWN) {
            colorctrl_blue_operation(cd, false);
        } else {
            colorctrl_blue_recharge_operation(cd);
        }
        break;
    }
    case TRANSPARENT_NORMAL : {
        if (cd->color_status != UNKNOWN) {
            colorctrl_transparent_operation(cd, false);
        } else {
            colorctrl_transparent_recharge_operation(cd);
        }
        break;
    }
    case RESET : {
        COLOR_INFO("reset color to default status.");
        colorctrl_light_blue_operation(cd);
        colorctrl_transparent_operation(cd, false);
        break;
    }
    case RESET_FTM : {
        COLOR_INFO("ftm mode reset color to default status.");
        if (cd->color_status == UNKNOWN || cd->color_status == BLUE) {
            colorctrl_transparent_operation(cd, true);
            colorctrl_light_blue_operation(cd);
        } else if (cd->color_status == TRANSPARENT) {
            colorctrl_light_blue_operation(cd);
        } else {
            COLOR_INFO("current color status is : %d, not need to do any operation.", cd->color_status);
        }
        break;
    }
    case STOP_RECHARGE : {
        cd->need_recharge = false;
        COLOR_INFO("stop recharge work.");
        break;
    }
    case OPEN_RECHARGE : {
        cd->need_recharge = true;
        COLOR_INFO("open recharge work.");
        break;
    }
    default :
        COLOR_INFO("not support color status.");
        break;
    }

    mutex_unlock(&cd->rw_lock);
    colorctrl_set_awake(cd, false);

    return count;
}

static ssize_t proc_colorctrl_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

    if (!cd || *ppos != 0) {
        return 0;
    }

    colorctrl_set_awake(cd, true);
    mutex_lock(&cd->rw_lock);
    snprintf(page, PAGESIZE - 1, "%d\n", (int)cd->color_status);
    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    mutex_unlock(&cd->rw_lock);
    COLOR_INFO("read value: %d.", cd->color_status);
    colorctrl_set_awake(cd, false);

    return ret;
}

static const struct file_operations proc_colorctrl_ops = {
    .read  = proc_colorctrl_read,
    .write = proc_colorctrl_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_temperature_control_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    char page[PAGESIZE] = {0};
    s64 temp = 0;
    struct color_ctrl_device *cd = PDE_DATA(file_inode(file));

    if (!cd || *ppos != 0) {
        return 0;
    }

    colorctrl_set_awake(cd, true);
    mutex_lock(&cd->rw_lock);

    ret = colorctrl_get_fitting_temperature(cd, &temp);
    if (ret < 0) {
        COLOR_INFO("failed to get fitting temperature");
        snprintf(page, PAGESIZE - 1, "failed to get fitting temperature\n");
    } else {
        COLOR_INFO("current fitting temperature is : %d", temp);
        snprintf(page, PAGESIZE - 1, "%lld\n", temp);
    }

    mutex_unlock(&cd->rw_lock);

    ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
    colorctrl_set_awake(cd, false);

    return ret;
}

static const struct file_operations proc_temperature_control_ops = {
    .read  = proc_temperature_control_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t colorctrl_voltage_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    int vm_volt = 0;
    char page[PAGESIZE] = {0};
    struct color_ctrl_device *cd = PDE_DATA(file_inode(file));
    struct color_ctrl_hw_resource *hw_res = NULL;

    if (!cd || *ppos != 0) {
        return 0;
    }

    colorctrl_set_awake(cd, true);
    mutex_lock(&cd->rw_lock);

    hw_res = cd->hw_res;

    if (!hw_res->vm_v_chan) {
        COLOR_INFO("voltage read is not support.", ret);
        snprintf(page, PAGESIZE - 1, "voltage read is not support.\n");
        goto OUT;
    }

    switch (cd->color_status) {
    case LIGHT_BLUE : {
        ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
        ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_HIGH);
        ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        if (ret) {
            COLOR_INFO("Config gpio status failed.");
            goto OUT;
        }
        break;
    }
    case BLUE : {
        ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_HIGH);
        ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
        ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        if (ret) {
            COLOR_INFO("Config gpio status failed.");
            goto OUT;
        }
        break;
    }
    case TRANSPARENT : {
        ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
        ret |= gpio_direction_output(hw_res->si_in_2_gpio, GPIO_HIGH);
        ret |= gpio_direction_output(hw_res->sleep_en_gpio, GPIO_HIGH);
        if (ret) {
            COLOR_INFO("Config gpio status failed.");
            goto OUT;
        }
        break;
    }
    default :
        COLOR_INFO("not support voltage read type, current color status : %d.", cd->color_status);
        goto OUT;
        break;
    }

    colorctrl_msleep(2);

    ret = iio_read_channel_processed(hw_res->vm_v_chan, &vm_volt);
    if (ret < 0) {
        COLOR_INFO("iio_read_channel_processed get error ret = %d", ret);
        goto OUT;
    }

    gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);

    vm_volt = vm_volt / UV_PER_MV;
    COLOR_INFO("vm_volt: %d", vm_volt);
    snprintf(page, PAGESIZE - 1, "%d\n", vm_volt);

OUT:
    mutex_unlock(&cd->rw_lock);
    colorctrl_set_awake(cd, false);
    return simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));
}

static const struct file_operations proc_adc_voltage_ops = {
    .read  = colorctrl_voltage_read,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static int colorctrl_str_to_int(char *in, int start_pos, int end_pos)
{
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

//parse string according to name:value1,value2,value3...
static int colorctrl_str_parse(char *in, char *name, unsigned int max_len, unsigned int *array, unsigned int array_max)
{
    int i = 0, in_cnt = 0, name_index = 0;
    int start_pos = 0, value_cnt = 0;

    if (!array || !in) {
        COLOR_INFO("array or in is null.");
        return -1;
    }

    in_cnt = strlen(in);

    //parse name
    for (i = 0; i < in_cnt; i++) {
        if (':' == in[i]) {     //split name and parameter by ":" symbol
            if (i > max_len) {
                COLOR_INFO("string %s name too long.\n", in);
                return -1;
            }
            name_index = i;
            memcpy(name, in, name_index);   //copy to name buffer
            COLOR_INFO("set name %s.", name);
        }
    }

    //parse parameter and put it into split_value array
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

static int colorctrl_main_parameter_read(struct seq_file *s, void *v)
{
    struct color_ctrl_device *cd = s->private;
    struct color_ctrl_control_para *blue_para = NULL;
    struct color_ctrl_control_para *transparent_para = NULL;

    if (!cd || !cd->blue_control_para || !cd->transparent_control_para) {
        return 0;
    }
    COLOR_INFO(" call.");

    blue_para = cd->blue_control_para;
    transparent_para = cd->transparent_control_para;

    mutex_lock(&cd->rw_lock);
    seq_printf(s, "recharge time : %d(s)\n", cd->recharge_time);
    seq_printf(s, "intermediate state wait time : %d(ms)\n", cd->intermediate_wait_time);
    seq_printf(s, "voltage measure interval time : %d(ms)\n", cd->volt_measure_interval_time);
    seq_printf(s, "open circuit voltage threshold : %d(mV)\n", cd->open_circuit_thd);
    seq_printf(s, "blue short circuit voltage threshold : %d(mV)\n", cd->blue_short_circuit_thd);
    seq_printf(s, "transparent short circuit voltage threshold : %d(mV)\n", cd->transparent_short_circuit_thd);
    seq_printf(s, "tempetature threshold :\n        temp_range_1:[%d, %d], temp_range_2:[%d, %d], temp_range_3:[%d, %d]\n\
        temp_range_4:[%d, %d], temp_range_5:[%d, %d], temp_range_6:[%d, %d]\n",
        cd->temp_thd_1, cd->temp_thd_2, cd->temp_thd_2, cd->temp_thd_3,
        cd->temp_thd_3, cd->temp_thd_4, cd->temp_thd_4, cd->temp_thd_5,
        cd->temp_thd_5, cd->temp_thd_6, cd->temp_thd_6, cd->temp_thd_7);
    seq_printf(s, "blue charge control parameter :\n        temp_range_1:[%d, %d], temp_range_2:[%d, %d], temp_range_3:[%d, %d]\n\
        temp_range_4:[%d, %d], temp_range_5:[%d, %d], temp_range_6:[%d, %d]\n",
        blue_para->charge_volt[0], blue_para->charge_time[0], blue_para->charge_volt[1], blue_para->charge_time[1],
        blue_para->charge_volt[2], blue_para->charge_time[2], blue_para->charge_volt[3], blue_para->charge_time[3],
        blue_para->charge_volt[4], blue_para->charge_time[4], blue_para->charge_volt[5], blue_para->charge_time[5]);
    seq_printf(s, "transparent charge control parameter :\n        temp_range_1:[%d, %d], temp_range_2:[%d, %d], temp_range_3:[%d, %d]\n\
        temp_range_4:[%d, %d], temp_range_5:[%d, %d], temp_range_6:[%d, %d]\n",
        transparent_para->charge_volt[0], transparent_para->charge_time[0], transparent_para->charge_volt[1], transparent_para->charge_time[1],
        transparent_para->charge_volt[2], transparent_para->charge_time[2], transparent_para->charge_volt[3], transparent_para->charge_time[3],
        transparent_para->charge_volt[4], transparent_para->charge_time[4], transparent_para->charge_volt[5], transparent_para->charge_time[5]);
    seq_printf(s, "blue recharge control parameter : [%d, %d]\n", blue_para->recharge_volt, blue_para->recharge_time);
    seq_printf(s, "transparent recharge control parameter : [%d, %d]\n", transparent_para->recharge_volt, transparent_para->recharge_time);
    seq_printf(s, "blue recharge voltage thd : [%d(mV), %d(mV)]\n", blue_para->recharge_volt_thd_1, blue_para->recharge_volt_thd_2);
    seq_printf(s, "transparent recharge voltage thd : [%d(mV), %d(mV)]\n", transparent_para->recharge_volt_thd_1, transparent_para->recharge_volt_thd_2);
    mutex_unlock(&cd->rw_lock);

    return 0;
}

static int main_parameter_open(struct inode *inode, struct file *file)
{
    return single_open(file, colorctrl_main_parameter_read, PDE_DATA(inode));
}

static ssize_t proc_colorctrl_main_parameter_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[PAGESIZE] = {0};
    int value_cnt = 0;
    char name[NAME_TAG_SIZE] = {0};
    unsigned int split_value[MAX_PARAMETER] = {0};
    struct color_ctrl_device *cd = PDE_DATA(file_inode(file));
    struct color_ctrl_control_para *blue_para = NULL;
    struct color_ctrl_control_para *transparent_para = NULL;

    if (!cd || !cd->blue_control_para || !cd->transparent_control_para || count >= PAGESIZE) {
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        COLOR_INFO("read proc input error.");
        return count;
    }

    buf[PAGESIZE - 1] = '\0';

    blue_para = cd->blue_control_para;
    transparent_para = cd->transparent_control_para;

    value_cnt = colorctrl_str_parse(buf, name, NAME_TAG_SIZE, split_value, MAX_PARAMETER);
    if (value_cnt < 0) {
        COLOR_INFO("str parse failed.");
        return count;
    }

    mutex_lock(&cd->rw_lock);

    if (!strcmp(name, "recharge_time") && (value_cnt == 1)) {
        cd->recharge_time = split_value[0];
        COLOR_INFO("%s is change to %d.", name, split_value[0]);
    } else if (!strcmp(name, "intermediate_wait_time") && (value_cnt == 1)) {
        cd->intermediate_wait_time = split_value[0];
        COLOR_INFO("%s is change to %d.", name, split_value[0]);
    } else if (!strcmp(name, "volt_measure_interval_time") && (value_cnt == 1)) {
        cd->volt_measure_interval_time = split_value[0];
        COLOR_INFO("%s is change to %d.", name, split_value[0]);
    } else if (!strcmp(name, "temperature_thd") && (value_cnt == 7)) {
        cd->temp_thd_1 = split_value[0];
        cd->temp_thd_2 = split_value[1];
        cd->temp_thd_3 = split_value[2];
        cd->temp_thd_4 = split_value[3];
        cd->temp_thd_5 = split_value[4];
        cd->temp_thd_6 = split_value[5];
        cd->temp_thd_7 = split_value[6];
        COLOR_INFO("%s is change : low temp [%d, %d], normal temp [%d, %d], high temp [%d, %d].", name, split_value[0], split_value[1], 
            split_value[2], split_value[3], split_value[4], split_value[5]);
    } else if (!strcmp(name, "abnormal_circuit_thd") && (value_cnt == 3)) {
        cd->open_circuit_thd = split_value[0];
        cd->blue_short_circuit_thd = split_value[1];
        cd->transparent_short_circuit_thd = split_value[2];
        COLOR_INFO("%s is change : [%d, %d, %d].", name, split_value[0], split_value[1], split_value[2]);
    } else if (!strcmp(name, "blue_charge_para") && (value_cnt == 12)) {
        blue_para->charge_volt[0] = split_value[0];
        blue_para->charge_time[0] = split_value[1];
        blue_para->charge_volt[1] = split_value[2];
        blue_para->charge_time[1] = split_value[3];
        blue_para->charge_volt[2] = split_value[4];
        blue_para->charge_time[2] = split_value[5];
        blue_para->charge_volt[3] = split_value[6];
        blue_para->charge_time[3] = split_value[7];
        blue_para->charge_volt[4] = split_value[8];
        blue_para->charge_time[4] = split_value[9];
        blue_para->charge_volt[5] = split_value[10];
        blue_para->charge_time[5] = split_value[11];
        COLOR_INFO("%s is change : temp range(1) [%d, %d], temp range(2) [%d, %d], temp range(3) [%d, %d], temp range(4) [%d, %d], temp range(5) [%d, %d], temp range(6) [%d, %d].",
            name, split_value[0], split_value[1], split_value[2], split_value[3], split_value[4], split_value[5],
            split_value[6], split_value[7], split_value[8], split_value[9], split_value[10], split_value[11]);
    } else if (!strcmp(name, "blue_recharge_para") && (value_cnt == 2)) {
        blue_para->recharge_volt = split_value[0];
        blue_para->recharge_time = split_value[1];
        COLOR_INFO("%s is change : [%d, %d].", name, split_value[0], split_value[1]);
    } else if (!strcmp(name, "transparent_charge_para") && (value_cnt == 12)) {
        transparent_para->charge_volt[0] = split_value[0];
        transparent_para->charge_time[0] = split_value[1];
        transparent_para->charge_volt[1] = split_value[2];
        transparent_para->charge_time[1] = split_value[3];
        transparent_para->charge_volt[2] = split_value[4];
        transparent_para->charge_time[2] = split_value[5];
        transparent_para->charge_volt[3] = split_value[6];
        transparent_para->charge_time[3] = split_value[7];
        transparent_para->charge_volt[4] = split_value[8];
        transparent_para->charge_time[4] = split_value[9];
        transparent_para->charge_volt[5] = split_value[10];
        transparent_para->charge_time[5] = split_value[11];
        COLOR_INFO("%s is change : temp range(1) [%d, %d], temp range(2) [%d, %d], temp range(3) [%d, %d], temp range(4) [%d, %d], temp range(5) [%d, %d], temp range(6) [%d, %d].",
            name, split_value[0], split_value[1], split_value[2], split_value[3], split_value[4], split_value[5],
            split_value[6], split_value[7], split_value[8], split_value[9], split_value[10], split_value[11]);
    } else if (!strcmp(name, "transparent_recharge_para") && (value_cnt == 2)) {
        transparent_para->recharge_volt = split_value[0];
        transparent_para->recharge_time = split_value[1];
        COLOR_INFO("%s is change : [%d, %d].", name, split_value[0], split_value[1]);
    } else if (!strcmp(name, "blue_recharge_voltage_thd") && (value_cnt == 2)) {
        blue_para->recharge_volt_thd_1 = split_value[0];
        blue_para->recharge_volt_thd_2 = split_value[1];
        COLOR_INFO("%s is change : [%d, %d].", name, split_value[0], split_value[1]);
    } else if (!strcmp(name, "transparent_recharge_voltage_thd") && (value_cnt == 2)) {
        transparent_para->recharge_volt_thd_1 = split_value[0];
        transparent_para->recharge_volt_thd_2 = split_value[1];
        COLOR_INFO("%s is change : [%d, %d].", name, split_value[0], split_value[1]);
    } else {
        COLOR_INFO("%s is not support or input value count is wrong.", name);
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

static int colorctrl_init_proc(struct color_ctrl_device *cd)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_cr = NULL;
    struct proc_dir_entry *prEntry_tmp = NULL;

    COLOR_INFO("entry");

    //proc files-step1:/proc/colorctrl
    prEntry_cr = proc_mkdir("colorctrl", NULL);
    if (prEntry_cr == NULL) {
        ret = -ENOMEM;
        COLOR_INFO("Couldn't create color ctrl proc entry");
    }

    //proc files-step2:/proc/touchpanel/color_ctrl (color control interface)
    prEntry_tmp = proc_create_data("color_ctrl", 0666, prEntry_cr, &proc_colorctrl_ops, cd);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        COLOR_INFO("Couldn't create color_ctrl proc entry");
    }

    //proc files-step2:/proc/touchpanel/temperature (color control temperature interface)
    prEntry_tmp = proc_create_data("temperature", 0666, prEntry_cr, &proc_temperature_control_ops, cd);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        COLOR_INFO("Couldn't create temperature proc entry");
    }

    //proc files-step2:/proc/touchpanel/voltage (color control voltage interface)
    prEntry_tmp = proc_create_data("voltage", 0666, prEntry_cr, &proc_adc_voltage_ops, cd);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        COLOR_INFO("Couldn't create voltage proc entry");
    }

    // show main_register interface
    prEntry_tmp = proc_create_data("main_parameter", 0666, prEntry_cr, &proc_main_parameter_ops, cd);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        COLOR_INFO("Couldn't create main parameter proc entry");
    }

    cd->prEntry_cr = prEntry_cr;

    return ret;
}

static int colorctrl_parse_dt(struct device *dev, struct color_ctrl_device *cd)
{
    int ret = 0, i = 0;
    int prj_id = 0;
    int temp_array[20] = {0};
    struct device_node *np = dev->of_node;
    struct color_ctrl_hw_resource *hw_res = cd->hw_res;
    struct color_ctrl_control_para *blue_para = NULL;
    struct color_ctrl_control_para *transparent_para = NULL;

    if(!np || !hw_res) {
        COLOR_INFO("Don't has device of_node.");
        return -1;
    }

    blue_para = devm_kzalloc(dev, sizeof(struct color_ctrl_control_para), GFP_KERNEL);
    if(!blue_para) {
        COLOR_INFO("Malloc memory for color control blue para fail.");
        return -ENOMEM;
    }

    transparent_para = devm_kzalloc(dev, sizeof(struct color_ctrl_control_para), GFP_KERNEL);
    if(!transparent_para) {
        COLOR_INFO("Malloc memory for color control transparent para fail.");
        return -ENOMEM;
    }

    cd->blue_control_para = blue_para;
    cd->transparent_control_para = transparent_para;

    cd->project_num = of_property_count_u32_elems(np, "platform_support_project");
    if (cd->project_num <= 0) {
        COLOR_INFO("project not specified, need to config the support project.");
        return -1;
    } else {
        ret = of_property_read_u32_array(np, "platform_support_project", cd->platform_support_project, cd->project_num);
        if (ret) {
            COLOR_INFO("platform_support_project not specified.");
            return -1;
        }
        prj_id = get_project();
        for (i = 0; i < cd->project_num; i++) {
            if (prj_id == cd->platform_support_project[i]) {
                COLOR_INFO("driver match the project.");
                break;
            }
        }
        if (i == cd->project_num) {
            COLOR_INFO("driver does not match the project.");
            return -1;
        }
    }

    cd->thermal_zone_device_num = of_property_count_strings(np, "thermal_zone_device_names");
    if (cd->thermal_zone_device_num <= 0 || cd->thermal_zone_device_num > MAX_THERMAL_ZONE_DEVICE_NUM) {
        COLOR_INFO("thermal zone device num not specified or device num is too large, thermal_zone_device_num : %d.", cd->thermal_zone_device_num);
        return -1;
    } else {
        ret = of_property_read_u32_array(np, "thermal_zone_device_weight", cd->thermal_zone_device_weight, cd->thermal_zone_device_num + 1);
        if (ret) {
            COLOR_INFO("thermal zone device weight is not specified, please check the dts config.");
            return -1;
        }
        ret = of_property_read_u32_array(np, "thermal_zone_device_weight_sigh_bit", cd->thermal_zone_device_weight_sigh_bit, cd->thermal_zone_device_num + 1);
        if (ret) {
            COLOR_INFO("thermal zone device weight sigh bit is not specified, please check the dts config.");
            return -1;
        }

        for (i = 0; i < cd->thermal_zone_device_num + 1; i++) {
            cd->thermal_zone_device_weight[i] = cd->thermal_zone_device_weight[i] * (cd->thermal_zone_device_weight_sigh_bit[i] > 0 ? 1 : -1);
            COLOR_INFO("%d thermal zone device weight is %d.", i, cd->thermal_zone_device_weight[i]);
        }

        for (i = 0; i < cd->thermal_zone_device_num; i++) {
            cd->thermal_zone_device_name[i] = devm_kzalloc(dev, MAX_THERMAL_ZONE_DEVICE_NAME_LEN, GFP_KERNEL);
            ret = of_property_read_string_index(np, "thermal_zone_device_names", i, (const char **)&cd->thermal_zone_device_name[i]);
            if (ret) {
                COLOR_INFO("thermal zone device name is not specified, please check the dts config.");
                return -1;
            }
            /*Request thermal device*/
            cd->thermal_zone_device[i] = thermal_zone_get_zone_by_name(cd->thermal_zone_device_name[i]);
            if (IS_ERR(cd->thermal_zone_device[i])) {
                ret = PTR_ERR(cd->thermal_zone_device[i]);
                cd->thermal_zone_device[i] = NULL;
                COLOR_INFO("fail to get %s thermal_zone_device: %d", cd->thermal_zone_device_name[i], ret);
                return -1;
            } else {
                COLOR_INFO("success get %s thermal_zone_device.", cd->thermal_zone_device_name[i]);
            }
        }
    }


    hw_res->sleep_en_gpio = of_get_named_gpio(np, "gpio-sleep_en", 0);
    if ((!gpio_is_valid(hw_res->sleep_en_gpio))) {
        COLOR_INFO("parse gpio-sleep_en fail.");
        return -1;
    }

    hw_res->si_in_1_gpio = of_get_named_gpio(np, "gpio-si_in_1", 0);
    if ((!gpio_is_valid(hw_res->si_in_1_gpio))) {
        COLOR_INFO("parse gpio-si_in_1 fail.");
        return -1;
    }

    hw_res->si_in_2_gpio = of_get_named_gpio(np, "gpio-si_in_2", 0);
    if ((!gpio_is_valid(hw_res->si_in_2_gpio))) {
        COLOR_INFO("parse gpio-si_in_2 fail.");
        return -1;
    }

    hw_res->vm_enable_gpio = of_get_named_gpio(np, "enable_vm_gpio", 0);
    if (!gpio_is_valid(hw_res->vm_enable_gpio)) {
        COLOR_INFO("enable_vm_gpio is not specified.");
    }

    hw_res->vm = devm_regulator_get(dev, "vm");
    if (IS_ERR_OR_NULL(hw_res->vm)) {
        COLOR_INFO("Regulator vm get failed.");
        return -1;
    }

    hw_res->vm_v_chan = devm_iio_channel_get(dev, "colorctrl_voltage_adc");
    if (IS_ERR(hw_res->vm_v_chan)) {
        ret = PTR_ERR(hw_res->vm_v_chan);
        hw_res->vm_v_chan = NULL;
        COLOR_INFO("vm_v_chan get error or voltage read is not support, ret = %d", ret);
    } else {
        COLOR_INFO("hw_res->vm_v_chan get success.");
    }

    COLOR_INFO("Parse dt ok, get gpios:[sleep_en_gpio:%d si_in_1_gpio:%d si_in_2_gpio:%d]",
               hw_res->sleep_en_gpio, hw_res->si_in_1_gpio, hw_res->si_in_2_gpio);

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,recharge_time", temp_array, 1);
    if (ret) {
        cd->recharge_time = 43200;
        COLOR_INFO("recharge time using default.");
    } else {
        cd->recharge_time = temp_array[0];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,intermediate_wait_time", temp_array, 1);
    if (ret) {
        cd->intermediate_wait_time = 20000;
        COLOR_INFO("intermediate state wait time using default.");
    } else {
        cd->intermediate_wait_time = temp_array[0];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,volt_measure_interval_time", temp_array, 1);
    if (ret) {
        cd->volt_measure_interval_time = 5000;
        COLOR_INFO("voltage measure interval time using default.");
    } else {
        cd->volt_measure_interval_time = temp_array[0];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,temperature_thd", temp_array, 7);
    if (ret) {
        cd->temp_thd_1 = 0;
        cd->temp_thd_2 = 5000;
        cd->temp_thd_3 = 10000;
        cd->temp_thd_4 = 15000;
        cd->temp_thd_5 = 20000;
        cd->temp_thd_6 = 45000;
        cd->temp_thd_7 = 70000;
        COLOR_INFO("temperature threshold using default.");
    } else {
        cd->temp_thd_1 = temp_array[0];
        cd->temp_thd_2 = temp_array[1];
        cd->temp_thd_3 = temp_array[2];
        cd->temp_thd_4 = temp_array[3];
        cd->temp_thd_5 = temp_array[4];
        cd->temp_thd_6 = temp_array[5];
        cd->temp_thd_7 = temp_array[6];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,abnormal_volt_thd", temp_array, 3);
    if (ret) {
        cd->open_circuit_thd = 100;
        cd->blue_short_circuit_thd = 450;
        cd->transparent_short_circuit_thd = 550;
        COLOR_INFO("abnormal circuit voltage using default.");
    } else {
        cd->open_circuit_thd = temp_array[0];
        cd->blue_short_circuit_thd = temp_array[1];
        cd->transparent_short_circuit_thd = temp_array[2];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,blue_charge_para", temp_array, 12);
    if (ret) {
        blue_para->charge_volt[0] = 1200;
        blue_para->charge_time[0] = 30000;
        blue_para->charge_volt[1] = 1200;
        blue_para->charge_time[1] = 25000;
        blue_para->charge_volt[2] = 1200;
        blue_para->charge_time[2] = 15000;
        blue_para->charge_volt[3] = 1000;
        blue_para->charge_time[3] = 15000;
        blue_para->charge_volt[4] = 1000;
        blue_para->charge_time[4] = 8000;
        blue_para->charge_volt[5] = 600;
        blue_para->charge_time[5] = 5000;
        COLOR_INFO("blue charge para using default.");
    } else {
        blue_para->charge_volt[0] = temp_array[0];
        blue_para->charge_time[0] = temp_array[1];
        blue_para->charge_volt[1] = temp_array[2];
        blue_para->charge_time[1] = temp_array[3];
        blue_para->charge_volt[2] = temp_array[4];
        blue_para->charge_time[2] = temp_array[5];
        blue_para->charge_volt[3] = temp_array[6];
        blue_para->charge_time[3] = temp_array[7];
        blue_para->charge_volt[4] = temp_array[8];
        blue_para->charge_time[4] = temp_array[9];
        blue_para->charge_volt[5] = temp_array[10];
        blue_para->charge_time[5] = temp_array[11];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,transparent_charge_para", temp_array, 12);
    if (ret) {
        transparent_para->charge_volt[0] = 1000;
        transparent_para->charge_time[0] = 15000;
        transparent_para->charge_volt[1] = 1000;
        transparent_para->charge_time[1] = 10000;
        transparent_para->charge_volt[2] = 1000;
        transparent_para->charge_time[2] = 8000;
        transparent_para->charge_volt[3] = 800;
        transparent_para->charge_time[3] = 11000;
        transparent_para->charge_volt[4] = 800;
        transparent_para->charge_time[4] = 10000;
        transparent_para->charge_volt[5] = 600;
        transparent_para->charge_time[5] = 10000;
        COLOR_INFO("transparent charge para using default.");
    } else {
        transparent_para->charge_volt[0] = temp_array[0];
        transparent_para->charge_time[0] = temp_array[1];
        transparent_para->charge_volt[1] = temp_array[2];
        transparent_para->charge_time[1] = temp_array[3];
        transparent_para->charge_volt[2] = temp_array[4];
        transparent_para->charge_time[2] = temp_array[5];
        transparent_para->charge_volt[3] = temp_array[6];
        transparent_para->charge_time[3] = temp_array[7];
        transparent_para->charge_volt[4] = temp_array[8];
        transparent_para->charge_time[4] = temp_array[9];
        transparent_para->charge_volt[5] = temp_array[10];
        transparent_para->charge_time[5] = temp_array[11];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,blue_recharge_para", temp_array, 2);
    if (ret) {
        blue_para->recharge_volt = 700;
        blue_para->recharge_time = 7000;
        COLOR_INFO("blue recharge para using default.");
    } else {
        blue_para->recharge_volt = temp_array[0];
        blue_para->recharge_time = temp_array[1];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,transparent_recharge_para", temp_array, 2);
    if (ret) {
        transparent_para->recharge_volt = 700;
        transparent_para->recharge_time = 7000;
        COLOR_INFO("transparent recharge para using default.");
    } else {
        transparent_para->recharge_volt = temp_array[0];
        transparent_para->recharge_time = temp_array[1];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,blue_recharge_volt_thd", temp_array, 2);
    if (ret) {
        blue_para->recharge_volt_thd_1 = 100;
        blue_para->recharge_volt_thd_2 = 300;
        COLOR_INFO("blue recharge voltage threshold using default.");
    } else {
        blue_para->recharge_volt_thd_1 = temp_array[0];
        blue_para->recharge_volt_thd_2 = temp_array[1];
    }

    ret = of_property_read_u32_array(dev->of_node, "colorctrl,transparent_recharge_volt_thd", temp_array, 2);
    if (ret) {
        transparent_para->recharge_volt_thd_1 = 100;
        transparent_para->recharge_volt_thd_2 = 400;
        COLOR_INFO("transparent recharge voltage threshold using default.");
    } else {
        transparent_para->recharge_volt_thd_1 = temp_array[0];
        transparent_para->recharge_volt_thd_2 = temp_array[1];
    }

    return 0;
}

static int colorctrl_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct color_ctrl_hw_resource *hw_res = NULL;
    struct color_ctrl_device *cd = NULL;

    COLOR_INFO("start to probe color ctr driver.");

    /*malloc memory for hardware resource */
    if(pdev->dev.of_node) {
        hw_res = devm_kzalloc(&pdev->dev, sizeof(struct color_ctrl_hw_resource), GFP_KERNEL);
        if(!hw_res) {
            ret = -ENOMEM;
            COLOR_INFO("Malloc memory for hardware resoure fail.");
            goto PROBE_ERR;
        }
    } else {
        hw_res = pdev->dev.platform_data;
    }

    /*malloc memory for color ctrl device*/
    cd = devm_kzalloc(&pdev->dev, sizeof(struct color_ctrl_device), GFP_KERNEL);
    if(!cd) {
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
        if(ret) {
            COLOR_INFO("request sleep_en_gpio fail.");
            goto PROBE_ERR;
        } else {
            /*Enable the sleep en gpio.*/
            ret = gpio_direction_output(hw_res->sleep_en_gpio, GPIO_LOW);
            if(ret) {
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
        if(ret) {
            COLOR_INFO("request si_in_1_gpio fail.");
            goto PROBE_ERR;
        } else {
            ret = gpio_direction_output(hw_res->si_in_1_gpio, GPIO_LOW);
            if(ret) {
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
        if(ret) {
            COLOR_INFO("request si_in_2_gpio fail.");
            goto PROBE_ERR;
        } else {
            ret = gpio_direction_output(hw_res->si_in_2_gpio, GPIO_LOW);
            if(ret) {
                COLOR_INFO("Config si_in_2_gpio gpio output direction fail.");
                goto PROBE_ERR;
            }
        }
    } else {
        hw_res->si_in_2_gpio = -EINVAL;
        COLOR_INFO("si_in_2_gpio gpio is invalid.");
        goto PROBE_ERR;
    }

    if (gpio_is_valid(hw_res->vm_enable_gpio)) {
        ret = devm_gpio_request(&pdev->dev, hw_res->vm_enable_gpio, "vm_enable_gpio");
        if(ret) {
            COLOR_INFO("request vm_enable_gpio fail.");
            goto PROBE_ERR;
        } else {
            ret = gpio_direction_output(hw_res->vm_enable_gpio, GPIO_LOW);
            if(ret) {
                COLOR_INFO("Config vm_enable_gpio gpio output direction fail.");
                goto PROBE_ERR;
            }
        }
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

    cd->ws = wakeup_source_register(cd->dev, "colorctrl wakelock");

    cd->pdev = pdev;
    cd->dev = &pdev->dev;
    cd->color_status = UNKNOWN;
    cd->temp_status = ABNORMAL_TEMP;
    cd->need_recharge = true;
    mutex_init(&cd->rw_lock);
    platform_set_drvdata(pdev, cd);

    ret = colorctrl_init_proc(cd);
    if (ret) {
        COLOR_INFO("creat color ctrl proc error.");
        goto PROBE_ERR;
    }

    COLOR_INFO("color ctrl device probe : normal end.");
    return ret;

PROBE_ERR:
    COLOR_INFO("color ctrl device probe error.");
    return ret;
}

static int colorctrl_remove(struct platform_device *dev)
{
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

static int __init colorctrl_driver_init(void)
{
    return platform_driver_register(&colorctrl_driver);
}

static void __exit colorctrl_driver_exit(void)
{
    return platform_driver_unregister(&colorctrl_driver);
}

late_initcall(colorctrl_driver_init);
module_exit(colorctrl_driver_exit);

//module_platform_driver(colorctrl_driver);

MODULE_DESCRIPTION("Color Ctrl Driver Module");
MODULE_AUTHOR("Zengpeng.Chen");
MODULE_LICENSE("GPL v2");
