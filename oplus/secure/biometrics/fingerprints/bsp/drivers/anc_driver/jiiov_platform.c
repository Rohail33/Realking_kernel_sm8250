/**
 * The device control driver for JIIOV's fingerprint sensor.
 *
 * Copyright (C) 2020 JIIOV Corporation. <http://www.jiiov.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
**/

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
//#include <linux/wakelock.h>
#include "../include/wakelock.h"
#include <linux/cdev.h>
#include <net/sock.h>
#include "jiiov_platform.h"

//#define ANC_CONFIG_PM_WAKELOCKS 0


#ifdef ANC_CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include "../include/wakelock.h"
#endif

#ifdef ANC_USE_SPI
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#endif

#include "../include/oplus_fp_common.h"
#include <linux/msm_drm_notify.h>
#include <linux/notifier.h>
#include <linux/fb.h>


#define ANC_COMPATIBLE_SW_FP    "jiiov,fingerprint"
#define ANC_DEVICE_NAME         "jiiov_fp"

#define ANC_DEVICE_MAJOR        0    /* default to dynamic major */
static int anc_major_num = ANC_DEVICE_MAJOR;

#define ANC_WAKELOCK_HOLD_TIME  500  /* ms */

#ifdef ANC_USE_SPI
#define SPI_BUFFER_SIZE         (50 * 1024)
#define ANC_DEFAULT_SPI_SPEED   (18 * 1000 * 1000)
static uint8_t *spi_buffer = NULL;
#endif

#ifdef ANC_USE_SPI
typedef struct spi_device anc_device_t;
typedef struct spi_driver anc_driver_t;

static anc_device_t *anc_spi_device = NULL;
#else
typedef struct platform_device anc_device_t;
typedef struct platform_driver anc_driver_t;
#endif


static int anc_gpio_pwr_flag = 0;

static const char * const pctl_names[] = {
    "anc_reset_reset",
    "anc_reset_active",
#ifdef ANC_USE_IRQ
    "anc_irq_active",
#endif
};

#ifndef ANC_USE_POWER_GPIO
struct vreg_config {
	char *name;
	unsigned long vmin;
	unsigned long vmax;
	int ua_load;
};

#define ANC_VREG_LDO_NAME    "ldo"
static const struct vreg_config const vreg_conf[] = {
    { ANC_VREG_LDO_NAME, 3300000UL, 3300000UL, 150000, },
};
#endif

struct anc_data {
    struct device *dev;
    struct class *dev_class;
    dev_t dev_num;
    struct cdev cdev;

    struct pinctrl *fingerprint_pinctrl;
    struct pinctrl_state *pinctrl_state[ARRAY_SIZE(pctl_names)];
#ifndef ANC_USE_POWER_GPIO
    struct regulator *vreg[ARRAY_SIZE(vreg_conf)];
#endif
#ifdef ANC_CONFIG_PM_WAKELOCKS
    struct wakeup_source fp_wakelock;
#else
    struct wake_lock fp_wakelock;
#endif
#ifdef ANC_USE_IRQ
    struct work_struct work_queue;
    int irq_gpio;
    int irq;
    atomic_t irq_enabled;
#endif

    int pwr_gpio;
    int rst_gpio;
    struct mutex lock;

#ifdef ANC_USE_NETLINK
    struct fp_underscreen_info fp_tpinfo;
    struct notifier_block notifier;
    char fb_black;
#endif
};

static struct anc_data *g_anc_data;


#ifndef ANC_USE_POWER_GPIO
static int vreg_setup(struct anc_data *data, const char *name, bool enable)
{
    size_t i;
    int rc;
    bool is_found = false;
    struct regulator *vreg;
    struct device *dev = data->dev;

    for (i = 0; i < ARRAY_SIZE(data->vreg); i++) {
        const char *n = vreg_conf[i].name;

        if (!strncmp(n, name, strlen(n))) {
            is_found = true;
            break;
        }
    }

    if (!is_found) {
        dev_err(dev, "Regulator %s not found\n", name);
        return -EINVAL;
    }

    vreg = data->vreg[i];
    if (enable) {
        if (!vreg) {
            vreg = regulator_get(dev, name);
            if (IS_ERR(vreg)) {
                dev_err(dev, "Unable to get %s\n", name);
                return PTR_ERR(vreg);
            }
        }

        if (regulator_count_voltages(vreg) > 0) {
            rc = regulator_set_voltage(vreg, vreg_conf[i].vmin, vreg_conf[i].vmax);
            if (rc)
                dev_err(dev, "Unable to set voltage on %s, %d\n", name, rc);
        }

        rc = regulator_set_load(vreg, vreg_conf[i].ua_load);
        if (rc < 0)
            dev_err(dev, "Unable to set current on %s, %d\n", name, rc);

        rc = regulator_enable(vreg);
        if (rc) {
            dev_err(dev, "error enabling %s: %d\n", name, rc);
            regulator_put(vreg);
            vreg = NULL;
        }
        data->vreg[i] = vreg;
    } else {
        if (vreg) {
            if (regulator_is_enabled(vreg)) {
                regulator_disable(vreg);
                dev_info(dev, "disabled %s\n", name);
            }
            regulator_put(vreg);
            data->vreg[i] = NULL;
        }
        rc = 0;
    }

    return rc;
}
#endif

/*-----------------------------------netlink-------------------------------*/
#ifdef ANC_USE_NETLINK
unsigned int lasttouchmode = 0;

typedef struct anc_netlink_msg {
    char event;
    uint8_t area_rate;
    uint16_t x;
    uint16_t y;
}__attribute__((packed)) anc_netlink_msg_t;

static int anc_opticalfp_tp_handler(struct fp_underscreen_info *tp_info)
{
    int rc = 0;
    anc_netlink_msg_t netlink_msg;
    memset(&netlink_msg,0,sizeof(netlink_msg));

    netlink_msg.event = (char)ANC_NETLINK_EVENT_INVALID;

    pr_info("[anc] %s\n", __func__);

    g_anc_data->fp_tpinfo = *tp_info;
    if(tp_info->touch_state == lasttouchmode){
        return rc;
    }
#ifdef ANC_CONFIG_PM_WAKELOCKS
    __pm_wakeup_event(&g_anc_data->fp_wakelock, msecs_to_jiffies(ANC_WAKELOCK_HOLD_TIME));
#else
    wake_lock_timeout(&g_anc_data->fp_wakelock, msecs_to_jiffies(ANC_WAKELOCK_HOLD_TIME));
#endif
    netlink_msg.area_rate = tp_info->area_rate;
    netlink_msg.x = tp_info->x;
    netlink_msg.y = tp_info->y;
    pr_info("[anc] Netlink touch info area:%d,x:%d,y:%d",netlink_msg.area_rate,netlink_msg.x, netlink_msg.y);

    if (1 == tp_info->touch_state) {
        netlink_msg.event = (char)ANC_NETLINK_EVENT_TOUCH_DOWN;
        pr_info("[anc] Netlink touch down!");
        netlink_send_message_to_user((const char *)(&netlink_msg), sizeof(netlink_msg));
        lasttouchmode = tp_info->touch_state;
    } else {
        netlink_msg.event = (char)ANC_NETLINK_EVENT_TOUCH_UP;
        pr_info("[anc] Netlink touch up!");
        netlink_send_message_to_user((const char *)(&netlink_msg), sizeof(netlink_msg));
        lasttouchmode = tp_info->touch_state;
    }

    return rc;
}

static int anc_fb_state_chg_callback(struct notifier_block *nb,
        unsigned long val, void *data)
{
    struct anc_data *anc_data;
    struct msm_drm_notifier *evdata = data;
    unsigned int blank;
    char netlink_msg = (char)ANC_NETLINK_EVENT_INVALID;
    int rc = 0;

    pr_info("[anc] %s\n", __func__);

    anc_data = container_of(nb, struct anc_data, notifier);

    if (val == MSM_DRM_ONSCREENFINGERPRINT_EVENT) {
        uint8_t op_mode = 0x0;
        op_mode = *(uint8_t *)evdata->data;
        pr_info("[anc] op_mode = %d\n", op_mode);

        switch (op_mode) {
            case ANC_UI_DISAPPREAR:
                pr_info("[anc] UI disappear\n");
                break;
            case ANC_UI_READY:
                pr_info("[anc] UI ready\n");
                netlink_msg = ANC_NETLINK_EVENT_UI_READY;
                netlink_send_message_to_user(&netlink_msg, sizeof(netlink_msg));
                break;
            default:
                pr_err("[anc] Unknown MSM_DRM_ONSCREENFINGERPRINT_EVENT!\n");
                break;
        }
        return rc;
    }

    /*if (evdata && evdata->data && val == FB_EARLY_EVENT_BLANK && anc_data) {
        blank = *(int *)(evdata->data);
        switch (blank) {
            case FB_BLANK_POWERDOWN:
                    anc_data->fb_black = 1;
                    msg = JIIOV_NET_EVENT_SCR_OFF;                 
                    pr_err("[anc] NET SCREEN OFF!\n");
                    netlink_send_message_to_user(&msg, length);
                break;
            case FB_BLANK_UNBLANK:
                    anc_data->fb_black = 0;
                    msg = JIIOV_NET_EVENT_SCR_ON;
                    pr_err("[anc] NET SCREEN ON!\n");
                    netlink_send_message_to_user(&msg, length);
                break;
            default:
                pr_err("[anc] Unknown screen state!\n");
                break;
        }
    }*/
	
   if (evdata && evdata->data && (val == MSM_DRM_EARLY_EVENT_BLANK) && anc_data) {
        blank = *(int *)(evdata->data);
        switch (blank) {
            case MSM_DRM_BLANK_POWERDOWN:
                anc_data->fb_black = 1;
                netlink_msg = ANC_NETLINK_EVENT_SCR_OFF;
                pr_info("[anc] NET SCREEN OFF!\n");
                netlink_send_message_to_user(&netlink_msg, sizeof(netlink_msg));
                break;
            case MSM_DRM_BLANK_UNBLANK:
                anc_data->fb_black = 0;
                netlink_msg = ANC_NETLINK_EVENT_SCR_ON;
                pr_info("[anc] NET SCREEN ON!\n");
                netlink_send_message_to_user(&netlink_msg, sizeof(netlink_msg));
                break;
            default:
                pr_err("[anc] Unknown screen state!\n");
                break;
        }
    }
    return NOTIFY_OK;
}

static struct notifier_block anc_noti_block = {
 .notifier_call = anc_fb_state_chg_callback,
};

/**
 * sysfs node to forward netlink event
 */
static ssize_t forward_netlink_event_set(struct device *p_dev,
	struct device_attribute *p_attr, const char *p_buffer, size_t count)
{
    char netlink_msg = (char)ANC_NETLINK_EVENT_INVALID;

    pr_info("forward netlink event: %s\n", p_buffer);
    if (!strncmp(p_buffer, "test", strlen("test"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_TEST;
    } else if (!strncmp(p_buffer, "irq", strlen("irq"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_IRQ;
    } else if (!strncmp(p_buffer, "screen_off", strlen("screen_off"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_SCR_OFF;
    } else if (!strncmp(p_buffer, "screen_on", strlen("screen_on"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_SCR_ON;
    } else if (!strncmp(p_buffer, "touch_down", strlen("touch_down"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_TOUCH_DOWN;
    } else if (!strncmp(p_buffer, "touch_up", strlen("touch_up"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_TOUCH_UP;
    } else if (!strncmp(p_buffer, "ui_ready", strlen("ui_ready"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_UI_READY;
    } else if (!strncmp(p_buffer, "exit", strlen("exit"))) {
        netlink_msg = (char)ANC_NETLINK_EVENT_EXIT;
    } else {
        pr_err("don't support the netlink evnet: %s\n", p_buffer);
        return -EINVAL;
    }

    return netlink_send_message_to_user(&netlink_msg, sizeof(netlink_msg));
}
static DEVICE_ATTR(netlink_event, S_IWUSR, NULL, forward_netlink_event_set);
#endif
/*-------------------------------------------------------------------------*/

/**
 * sysfs node to select the set of pins (GPIOS) defined in a pin control node of
 * the device tree
 */
static int select_pin_ctl(struct anc_data *data, const char *name)
{
    size_t i;
    int rc;
    struct device *dev = data->dev;

    dev_info(dev, "%s: name is %s\n", __func__, name);
    for (i = 0; i < ARRAY_SIZE(data->pinctrl_state); i++) {
        const char *n = pctl_names[i];

        if (!strncmp(n, name, strlen(n))) {
            rc = pinctrl_select_state(data->fingerprint_pinctrl, data->pinctrl_state[i]);
            if (rc)
                dev_err(dev, "cannot select %s\n", name);
            else
                dev_info(dev, "Selected %s\n", name);
            goto exit;
        }
    }

    rc = -EINVAL;
    dev_err(dev, "%s: %s not found\n", __func__, name);

exit:
    return rc;
}

static ssize_t pinctl_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    struct anc_data *data = dev_get_drvdata(dev);

    mutex_lock(&data->lock);
    rc = select_pin_ctl(data, buf);
    mutex_unlock(&data->lock);

    return rc ? rc : count;
}
static DEVICE_ATTR(pinctl_set, S_IWUSR, NULL, pinctl_set);

static int anc_reset(struct anc_data *data)
{
    int rc;
    pr_err("anc reset\n");
    mutex_lock(&data->lock);
    rc = select_pin_ctl(data, "anc_reset_reset");
    //T2 >= 10ms
    mdelay(10);
    rc |= select_pin_ctl(data, "anc_reset_active");
    mdelay(10);
    mutex_unlock(&data->lock);

    return rc;
}

static ssize_t hw_reset_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    struct anc_data *data = dev_get_drvdata(dev);

    if (!strncmp(buf, "reset", strlen("reset"))) {
        pr_info("hw_reset\n");
        rc = anc_reset(data);
    } else {
        rc = -EINVAL;
    }

    return rc ? rc : count;
}
static DEVICE_ATTR(hw_reset, S_IWUSR, NULL, hw_reset_set);


/*static int vreg_setup (struct anc_data *data, const char *name, bool enable)
{
    //size_t i;
    int rc;
    struct regulator *vreg;
    struct device *dev = data->dev;
    if (NULL == name) {
        pr_err("name is NULL\n");
        return -EINVAL;
    }
    if (1) {
        if (!vreg) {
            vreg = regulator_get(dev, name);
            if (IS_ERR(vreg)) {
                pr_err("Unable to get  %s\n", name);
                return PTR_ERR(vreg);
            }
        }
        if (regulator_count_voltages(vreg) > 0) {
            rc = regulator_set_voltage(vreg, 1500000, 3300000);
            if (rc)
                pr_err("Unable to set voltage on %s, %d\n",
                        name, rc);
        }
        rc = regulator_set_load(vreg, 3300000);
        if (rc < 0)
            pr_err("Unable to set current on %s, %d\n",
                    name, rc);
        rc = regulator_enable(vreg);
        if (rc) {
            pr_err("error enabling %s: %d\n", name, rc);
            regulator_put(vreg);
            vreg = NULL;
        }
    }
    return rc;
}*/






static void anc_power_onoff(struct anc_data *data, int power_onoff)
{
    pr_info("%s: power_onoff = %d \n", __func__, power_onoff);
    if (anc_gpio_pwr_flag == 1) {
        gpio_set_value(data->pwr_gpio, power_onoff);
    } else {
        vreg_setup(data, ANC_VREG_LDO_NAME, power_onoff);
    }
}

static void device_power_up(struct anc_data *data)
{
    pr_info("device power up\n");
    anc_power_onoff(data, 1);
}

/**
 * sysfs node to power on/power off the sensor
 */
static ssize_t device_power_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    ssize_t rc = count;
    struct anc_data *data = dev_get_drvdata(dev);

    mutex_lock(&data->lock);
    if (!strncmp(buf, "on", strlen("on"))) {
        pr_info("device power on\n");
        anc_power_onoff(data, 1);
    } else if (!strncmp(buf, "off", strlen("off"))) {
        pr_info("device power off\n");
        anc_power_onoff(data, 0);
    } else {
        rc = -EINVAL;
    }
    mutex_unlock(&data->lock);

    return rc;
}
static DEVICE_ATTR(device_power, S_IWUSR, NULL, device_power_set);

#ifdef ANC_USE_SPI
static uint32_t anc_read_sensor_id(struct anc_data *data);
/**
 * sysfs node to read sensor id
 */
static ssize_t sensor_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct anc_data *data = dev_get_drvdata(dev);
    uint32_t senor_chip_id = anc_read_sensor_id(data);

    return scnprintf(buf, PAGE_SIZE, "0x%04x\n", senor_chip_id);
}
static DEVICE_ATTR(sensor_id, S_IRUSR, sensor_id_show, NULL);
#endif

#ifdef ANC_USE_IRQ
static void anc_enable_irq(struct anc_data *data)
{
    pr_info("enable irq\n");
    if (atomic_read(&data->irq_enabled)) {
        pr_warn("IRQ has been enabled\n");
    } else {
        enable_irq(data->irq);
        atomic_set(&data->irq_enabled, 1);
    }
}

static void anc_disable_irq(struct anc_data *data)
{
    pr_info("disable irq\n");
    if (atomic_read(&data->irq_enabled)) {
        disable_irq(data->irq);
        atomic_set(&data->irq_enabled, 0);
    } else {
        pr_warn("IRQ has been disabled\n");
    }
}

/**
 * sysfs node for controlling whether the driver is allowed
 * to wake up the platform on interrupt.
 */
static ssize_t irq_control_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    ssize_t rc = count;
    struct anc_data *data = dev_get_drvdata(dev);

    mutex_lock(&data->lock);
    if (!strncmp(buf, "enable", strlen("enable")))
    {
        anc_enable_irq(data);
    } else if (!strncmp(buf, "disable", strlen("disable"))) {
        anc_disable_irq(data);
    } else {
        rc = -EINVAL;
    }
    mutex_unlock(&data->lock);

    return rc;
}
static DEVICE_ATTR(irq_set, S_IWUSR, NULL, irq_control_set);
#endif

static struct attribute *attributes[] = {
    &dev_attr_pinctl_set.attr,
    &dev_attr_device_power.attr,
    &dev_attr_hw_reset.attr,
#ifdef ANC_USE_IRQ
    &dev_attr_irq_set.attr,
#endif
#ifdef ANC_USE_NETLINK
    &dev_attr_netlink_event.attr,
#endif
#ifdef ANC_USE_SPI
    &dev_attr_sensor_id.attr,
#endif
    NULL
};

static const struct attribute_group attribute_group = {
    .attrs = attributes,
};

#ifdef ANC_USE_IRQ
static void anc_do_irq_work(struct work_struct *ws)
{
    char netlink_msg = (char)ANC_NETLINK_EVENT_IRQ;
#ifdef ANC_USE_NETLINK
    netlink_send_message_to_user(&netlink_msg, sizeof(netlink_msg));
#endif
}

static irqreturn_t anc_irq_handler(int irq, void *handle)
{
    struct anc_data *data = handle;

    pr_info("irq handler\n");
#ifdef ANC_CONFIG_PM_WAKELOCKS
    __pm_wakeup_event(&data->fp_wakelock, msecs_to_jiffies(ANC_WAKELOCK_HOLD_TIME));
#else
    wake_lock_timeout(&data->fp_wakelock, msecs_to_jiffies(ANC_WAKELOCK_HOLD_TIME));
#endif
    schedule_work(&data->work_queue);

    return IRQ_HANDLED;
}
#endif

static int anc_request_named_gpio(struct anc_data *data, const char *label, int *gpio)
{
    struct device *dev = data->dev;
    struct device_node *np = dev->of_node;
    int rc = of_get_named_gpio(np, label, 0);

    if (rc < 0) {
        dev_err(dev, "failed to get '%s'\n", label);
        return rc;
    }
    *gpio = rc;

    rc = devm_gpio_request(dev, *gpio, label);
    if (rc) {
        dev_err(dev, "failed to request gpio %d\n", *gpio);
        return rc;
    }
    dev_info(dev, "%s %d\n", label, *gpio);

    return 0;
}

static int anc_gpio_init(struct device *dev, struct anc_data *data)
{
    int rc = 0;
    size_t i;

    struct device_node *np = dev->of_node;
    if (!np) {
        dev_err(dev, "no of node found\n");
        rc = -EINVAL;
        goto exit;
    }

    if (of_property_read_bool(dev->of_node, "anc,enable-via-gpio")) {
        dev_err(dev, "%s, Using GPIO Power \n", __func__);
        anc_gpio_pwr_flag = 1;
    }


    rc = anc_request_named_gpio(data, "anc,gpio_rst", &data->rst_gpio);
    if (rc)
        goto exit;

#ifdef ANC_USE_IRQ
    rc = anc_request_named_gpio(data, "anc,gpio_irq", &data->irq_gpio);
    if (rc)
        goto exit;

    rc = gpio_direction_input(data->irq_gpio);
    if (rc)
        goto exit;
#endif

    if (anc_gpio_pwr_flag == 1 ) {
        rc = anc_request_named_gpio(data, "anc,gpio_pwr", &data->pwr_gpio);
        if (rc)
            goto exit;

        rc = gpio_direction_output(data->pwr_gpio, 0);
        if (rc)
            goto exit;
    }

    data->fingerprint_pinctrl = devm_pinctrl_get(dev);
    if (IS_ERR(data->fingerprint_pinctrl)) {
        if (PTR_ERR(data->fingerprint_pinctrl) == -EPROBE_DEFER) {
            dev_info(dev, "pinctrl not ready\n");
            rc = -EPROBE_DEFER;
            goto exit;
        }
        dev_err(dev, "Target does not use pinctrl\n");
        data->fingerprint_pinctrl = NULL;
        rc = -EINVAL;
        goto exit;
    }

    for (i = 0; i < ARRAY_SIZE(data->pinctrl_state); i++) {
        const char *n = pctl_names[i];
        struct pinctrl_state *state = pinctrl_lookup_state(data->fingerprint_pinctrl, n);
        if (IS_ERR(state)) {
            dev_err(dev, "cannot find '%s'\n", n);
            rc = -EINVAL;
            goto exit;
        }
        dev_info(dev, "found pin control %s\n", n);
        data->pinctrl_state[i] = state;
    }

exit:
    return rc;
}

#ifdef ANC_USE_IRQ
static int anc_irq_init(struct device *dev, struct anc_data *data)
{
    int rc = 0;
    int irqf = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;  // IRQF_TRIGGER_FALLING or IRQF_TRIGGER_RISING

    rc = select_pin_ctl(data, "anc_irq_active");
    if (rc)
        goto exit;

    data->irq = gpio_to_irq(data->irq_gpio);
    rc = devm_request_threaded_irq(dev, data->irq, NULL, anc_irq_handler, irqf, dev_name(dev), data);
    if (rc) {
        dev_err(dev, "%s: Could not request irq %d\n", __func__, data->irq);
        goto exit;
    }

    /* Request that the interrupt should be wakeable */
    enable_irq_wake(data->irq);
    atomic_set(&data->irq_enabled, 1);

exit:
    return rc;
}
#endif

static int anc_open(struct inode *inode, struct file *filp)
{
    struct anc_data *dev_data;
    dev_data = container_of(inode->i_cdev, struct anc_data, cdev);
    filp->private_data = dev_data;
    return 0;
}

static long anc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int rc = 0;
    struct anc_data *dev_data = filp->private_data;

    if (_IOC_TYPE(cmd) != ANC_IOC_MAGIC)
        return -ENOTTY;

    pr_info("%s: cmd = %d\n", __func__, _IOC_NR(cmd));

    switch (cmd) {
    case ANC_IOC_RESET:
        pr_info("%s: reset\n", __func__);
        rc = anc_reset(dev_data);
        break;
    case ANC_IOC_ENABLE_POWER:
        pr_info("%s: enable power\n", __func__);
        anc_power_onoff(dev_data, 1);
        break;
    case ANC_IOC_DISABLE_POWER:
        pr_info("%s: disable power\n", __func__);
        anc_power_onoff(dev_data, 0);
        break;
    case ANC_IOC_CLEAR_FLAG:
#ifdef ANC_USE_NETLINK
        lasttouchmode = 0;
        pr_info("%s: clear tp flag\n", __func__);
#endif
        break;
#ifdef ANC_USE_IRQ
    case ANC_IOC_ENABLE_IRQ:
        pr_info("%s: enable irq\n", __func__);
        anc_enable_irq(dev_data);
        break;
    case ANC_IOC_DISABLE_IRQ:
        pr_info("%s: disable irq\n", __func__);
        anc_disable_irq(dev_data);
        break;
#endif
#ifdef ANC_USE_SPI
    case ANC_IOC_SPI_SPEED:
        anc_spi_device->max_speed_hz = arg;
        spi_setup(anc_spi_device);
        break;
#endif
    default:
        rc = -EINVAL;
        break;
    }
    return rc;
}

#ifdef CONFIG_COMPAT
static long anc_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return anc_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

#ifdef ANC_USE_SPI
static int anc_spi_transfer(const uint8_t *txbuf, uint8_t *rxbuf, int len) {
    struct spi_transfer t;
    struct spi_message m;

    memset(&t, 0, sizeof(t));
    spi_message_init(&m);
    t.tx_buf = txbuf;
    t.rx_buf = rxbuf;
    t.bits_per_word = 8;
    t.len = len;
    t.speed_hz = anc_spi_device->max_speed_hz;
    spi_message_add_tail(&t, &m);
    return spi_sync(anc_spi_device, &m);
}

static ssize_t anc_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t status = 0;
    // struct anc_data *dev_data = filp->private_data;

    pr_info("%s: count = %zu\n", __func__, count);

    if (count > SPI_BUFFER_SIZE) {
        return (-EMSGSIZE);
    }

    if (copy_from_user(spi_buffer, buf, count)) {
        pr_err("%s: copy_from_user failed\n", __func__);
        return (-EMSGSIZE);
    }

    status = anc_spi_transfer(spi_buffer, spi_buffer, count);

    if (status == 0) {
        status = copy_to_user(buf, spi_buffer, count);

        if (status != 0) {
            status = -EFAULT;
        }
        else {
            status = count;
        }
    }
    else {
        pr_err("%s: spi_transfer failed\n", __func__);
    }

    return status;
}

static ssize_t anc_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t status = 0;
    // struct anc_data *dev_data = filp->private_data;

    pr_info("%s: count = %zu\n", __func__, count);

    if (count > SPI_BUFFER_SIZE) {
        return (-EMSGSIZE);
    }

    if (copy_from_user(spi_buffer, buf, count)) {
        pr_err("%s: copy_from_user failed\n", __func__);
        return (-EMSGSIZE);
    }

    status = anc_spi_transfer(spi_buffer, NULL, count);

    if (status == 0) {
        status = count;
    }
    else {
        pr_err("%s: spi_transfer failed\n", __func__);
    }

    return status;
}
#endif

static const struct file_operations anc_fops = {
    .owner = THIS_MODULE,
    .open = anc_open,
    .unlocked_ioctl = anc_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = anc_compat_ioctl,
#endif
#ifdef ANC_USE_SPI
    .write  = anc_write,
    .read = anc_read,
#endif
};

#ifdef ANC_USE_SPI
static uint32_t anc_read_sensor_id(struct anc_data *data)
{
    int rc = -1;
    int trytimes = 3;
    uint32_t sensor_chip_id = 0;

    do {
        memset(spi_buffer, 0, 4);
        spi_buffer[0] = 0x30;
        spi_buffer[1] = (char)(~0x30);

        anc_reset(data);

        rc = anc_spi_transfer(spi_buffer, spi_buffer, 4);
        if (rc != 0) {
            pr_err("%s: spi_transfer failed\n", __func__);
            continue;
        }

        sensor_chip_id = (uint32_t)((spi_buffer[3] & 0x00FF) | ((spi_buffer[2] << 8) & 0xFF00));
        pr_info("%s: sensor chip_id = %#x\n", __func__, sensor_chip_id);

        if (sensor_chip_id == 0x6311) {
            pr_info("%s: Read Sensor Id Success\n", __func__);
            return 0;
        } else {
            pr_err("%s: Read Sensor Id Fail\n", __func__);
        }
    }
    while (trytimes--);

    return sensor_chip_id;
}
#endif

static int anc_probe(anc_device_t *pdev)
{
    struct device *dev = &pdev->dev;
    int rc = 0;
    struct anc_data *dev_data;
    struct device *device_ptr;

    dev_info(dev, "Anc Probe\n");

    /* Allocate device data */
    dev_data = devm_kzalloc(dev, sizeof(*dev_data), GFP_KERNEL);
    if (!dev_data) {
        dev_err(dev, "%s: Failed to allocate memory for device data\n", __func__);
        rc = -ENOMEM;
        goto device_data_err;
    }

#ifdef ANC_USE_SPI
    /* Allocate SPI transfer DMA buffer */
    spi_buffer = kmalloc(SPI_BUFFER_SIZE, GFP_KERNEL | GFP_DMA);
    if (!spi_buffer) {
        pr_err("%s: Unable to allocate memory for spi buffer\n", __func__);
        rc = -ENOMEM;
        goto spi_buffer_err;
    }
#endif

    dev_data->dev = dev;
    g_anc_data = dev_data;
#ifdef ANC_USE_SPI
    spi_set_drvdata(pdev, dev_data);
    anc_spi_device = pdev;
    /* setup spi config */
    anc_spi_device->mode            = SPI_MODE_0;
    anc_spi_device->bits_per_word   = 8;
    anc_spi_device->max_speed_hz    = ANC_DEFAULT_SPI_SPEED;
    spi_setup(anc_spi_device);
#else
    platform_set_drvdata(pdev, dev_data);
#endif

    dev_data->dev_class = class_create(THIS_MODULE, ANC_DEVICE_NAME);
    if (IS_ERR(dev_data->dev_class)) {
        dev_err(dev, "%s: class_create error\n", __func__);
        rc = -ENODEV;
        goto device_class_err;
    }

    if (anc_major_num) {
        dev_data->dev_num = MKDEV(anc_major_num, 0);
        rc = register_chrdev_region(dev_data->dev_num, 1, ANC_DEVICE_NAME);
    } else {
        rc = alloc_chrdev_region(&dev_data->dev_num, 0, 1, ANC_DEVICE_NAME);
        if (rc < 0) {
            dev_err(dev, "%s: Failed to allocate char device region\n", __func__);
            goto device_region_err;
        }
        anc_major_num = MAJOR(dev_data->dev_num);
        dev_info(dev, "%s: Major number of device = %d\n", __func__, anc_major_num);
    }

    device_ptr = device_create(dev_data->dev_class, NULL, dev_data->dev_num, dev_data, ANC_DEVICE_NAME);
    if (IS_ERR(device_ptr)) {
        dev_err(dev, "%s: Failed to create char device\n", __func__);
        rc = -ENODEV;
        goto device_create_err;
    }

    cdev_init(&dev_data->cdev, &anc_fops);
    dev_data->cdev.owner = THIS_MODULE;
    rc = cdev_add(&dev_data->cdev, dev_data->dev_num, 1);
    if (rc < 0) {
        dev_err(dev, "%s: Failed to add char device\n", __func__);
        goto cdev_add_err;
    }

    mutex_init(&dev_data->lock);

    rc = anc_gpio_init(dev, dev_data);
    if (rc) {
        dev_err(dev, "%s: Failed to init gpio", __func__);
        goto exit;
    }

    dev_info(dev, "%s, Enabling hardware\n", __func__);
    device_power_up(dev_data);

#ifdef ANC_USE_SPI
    anc_read_sensor_id(dev_data);
#endif

#ifdef ANC_USE_IRQ
    rc = anc_irq_init(dev, dev_data);
    if (rc) {
        dev_err(dev, "%s: Failed to init irq", __func__);
        goto exit;
    }
    INIT_WORK(&dev_data->work_queue, anc_do_irq_work);
#endif

#ifdef ANC_CONFIG_PM_WAKELOCKS
    wakeup_source_init(&dev_data->fp_wakelock, "anc_fp_wakelock");
#else
    wake_lock_init(&dev_data->fp_wakelock, WAKE_LOCK_SUSPEND, "anc_fp_wakelock");
#endif

#ifdef ANC_USE_NETLINK
    /* Register fb notifier callback */
    dev_data->notifier = anc_noti_block;
    rc = msm_drm_register_client(&dev_data->notifier);
    if (rc < 0) {
        dev_err(dev, "%s: Failed to register fb notifier client\n", __func__);
        goto exit;
    }
#endif

    dev_info(dev, "%s: Create sysfs path = %s\n", __func__, (&dev->kobj)->name);
    rc = sysfs_create_group(&dev->kobj, &attribute_group);
    if (rc) {
        dev_err(dev, "%s: Could not create sysfs\n", __func__);
        goto exit;
    }

    dev_info(dev, "%s: Probe Success\n", __func__);
    return 0;

exit:
cdev_add_err:
    device_destroy(dev_data->dev_class, dev_data->dev_num);
device_create_err:
    unregister_chrdev_region(dev_data->dev_num, 1);
device_region_err:
    class_destroy(dev_data->dev_class);
device_class_err:
#ifdef ANC_USE_SPI
    kfree(spi_buffer);
    spi_buffer = NULL;
spi_buffer_err:
#endif
    devm_kfree(dev, dev_data);
    dev_data = NULL;
device_data_err:
    dev_err(dev, "%s: Probe Failed, rc = %d\n", __func__, rc);
    return rc;
}

static int anc_remove(anc_device_t *pdev)
{
#ifdef ANC_USE_SPI
    struct anc_data *data = spi_get_drvdata(pdev);
#else
    struct anc_data *data = platform_get_drvdata(pdev);
#endif

    sysfs_remove_group(&pdev->dev.kobj, &attribute_group);
    mutex_destroy(&data->lock);
#ifdef ANC_USE_IRQ
    cancel_work_sync(&data->work_queue);
#endif
#ifdef ANC_CONFIG_PM_WAKELOCKS
    wakeup_source_trash(&data->fp_wakelock);
#else
    wake_lock_destroy(&data->fp_wakelock);
#endif
#ifdef ANC_USE_NETLINK
    msm_drm_unregister_client(&data->notifier);
#endif
    cdev_del(&data->cdev);
    device_destroy(data->dev_class, data->dev_num);
    unregister_chrdev_region(data->dev_num, 1);
    class_destroy(data->dev_class);
#ifdef ANC_USE_SPI
    kfree(spi_buffer);
#endif
    return 0;
}

static struct of_device_id anc_of_match[] = {
    { .compatible = ANC_COMPATIBLE_SW_FP, },
    {}
};
MODULE_DEVICE_TABLE(of, anc_of_match);

static anc_driver_t anc_driver = {
    .driver = {
        .name  = ANC_DEVICE_NAME,
        .owner = THIS_MODULE,
        .of_match_table = anc_of_match,
    },
    .probe  = anc_probe,
    .remove = anc_remove,
};

static int __init ancfp_init(void)
{
    int rc;

    if ((FP_JIIOV_0302 != get_fpsensor_type()) && (FP_JIIOV_0301 != get_fpsensor_type())) {
        pr_err("%s, found not jiiov sensor\n", __func__);
        rc = -EINVAL;
        return rc;
    }

#ifdef ANC_USE_SPI
    rc = spi_register_driver(&anc_driver);
#else
    rc = platform_driver_register(&anc_driver);
#endif
    if (!rc) {
        pr_info("%s OK\n", __func__);
    } else {
        pr_err("%s %d\n", __func__, rc);
    }

#ifdef ANC_USE_NETLINK
    anc_netlink_init();
    /*Register for receiving tp touch event.
     * Must register after get_fpsensor_type filtration as only one handler can be registered.
    */
    opticalfp_irq_handler_register(anc_opticalfp_tp_handler);
    pr_info("register tp event handler");
#endif

    return rc;
}

static void __exit ancfp_exit(void)
{
    pr_info("%s\n", __func__);
#ifdef ANC_USE_NETLINK
    anc_netlink_exit();
#endif
#ifdef ANC_USE_SPI
    spi_unregister_driver(&anc_driver);
#else
    platform_driver_unregister(&anc_driver);
#endif
}

late_initcall(ancfp_init);
module_exit(ancfp_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JIIOV");
MODULE_DESCRIPTION("JIIOV fingerprint sensor device driver");

