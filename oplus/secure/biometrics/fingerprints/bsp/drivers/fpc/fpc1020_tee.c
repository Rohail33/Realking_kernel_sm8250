// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include "../include/wakelock.h"
#include <soc/qcom/scm.h>
#include "../include/oplus_fp_common.h"

#undef dev_info
#define dev_info dev_err
#undef dev_dbg
#define dev_dbg dev_err

#define   FPC1020_RESET_LOW_US                              1000
#define   FPC1020_RESET_HIGH1_US                            100
#define   FPC1020_RESET_HIGH2_US                            1250
#define   FPC_TTW_HOLD_TIME                                 1000
#define   FPC_IRQ_WAKELOCK_TIMEOUT                          500

#define   WAKELOCK_DISABLE                                  0
#define   WAKELOCK_ENABLE                                   1
#define   WAKELOCK_TIMEOUT_ENABLE                           2
#define   WAKELOCK_TIMEOUT_DISABLE                          3

//#define wake_lock_kernel_4_9

struct vreg_config {
        char *name;
        unsigned long vmin;
        unsigned long vmax;
        int ua_load;
};

static const struct vreg_config const vreg_conf[] = {
        { "vdd_io", 1800000UL, 1800000UL, 10000, },
        { "vmch", 2960000UL, 2960000UL, 10000, },
        { "avdd_io", 3300000UL, 3300000UL, 10000, },
};

struct fpc1020_data {
        struct device *dev;
        int irq_gpio;
        int irq_num;
        struct mutex lock;
        bool prepared;

//#ifdef OPLUS_FEATURE_FINGERPRINT
        int irq_enabled;
//#endif

        struct pinctrl                                  *ts_pinctrl;
        struct pinctrl_state                *gpio_state_active;
        struct pinctrl_state                *gpio_state_suspend;
        struct wake_lock                                ttw_wl;
        struct wake_lock                                fpc_wl;
        struct wake_lock                                fpc_irq_wl;
        struct regulator                                *vreg[ARRAY_SIZE(vreg_conf)];
};

static int fpc1020_request_named_gpio(struct fpc1020_data *fpc1020,
                const char *label, int *gpio)
{
        struct device *dev = fpc1020->dev;
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

        /*dev_info(dev, "%s - gpio: %d\n", label, *gpio);*/
        return 0;
}

static int vreg_setup(struct fpc1020_data *fpc1020, const char *name,
                bool enable)
{
        size_t i;
        int rc;
        struct regulator *vreg;
        struct device *dev = fpc1020->dev;

        for (i = 0; i < ARRAY_SIZE(fpc1020->vreg); i++) {
                const char *n = vreg_conf[i].name;
                if (!strncmp(n, name, strlen(n))) {
                        goto found;
                }
        }
        dev_err(dev, "Regulator %s not found\n", name);
        return -EINVAL;

found:
        vreg = fpc1020->vreg[i];
        if (enable) {
                if (!vreg) {
                        vreg = regulator_get(dev, name);
                        if (IS_ERR(vreg)) {
                                dev_err(dev, "Unable to get  %s\n", name);
                                return PTR_ERR(vreg);
                        }
                }
                if (regulator_count_voltages(vreg) > 0) {
                        rc = regulator_set_voltage(vreg, vreg_conf[i].vmin,
                                        vreg_conf[i].vmax);
                        if (rc) {
                                dev_err(dev,
                                                "Unable to set voltage on %s, %d\n",
                                                name, rc);
                        }
                }

                rc = regulator_set_load(vreg, vreg_conf[i].ua_load);

                if (rc < 0) {
                        dev_err(dev, "Unable to set current on %s, %d\n",
                                        name, rc);
                }
                rc = regulator_enable(vreg);
                if (rc) {
                        dev_err(dev, "error enabling %s: %d\n", name, rc);
                        regulator_put(vreg);
                        vreg = NULL;
                }
                fpc1020->vreg[i] = vreg;
        } else {
                if (vreg) {
                        if (regulator_is_enabled(vreg)) {
                                regulator_disable(vreg);
                                dev_dbg(dev, "disabled %s\n", name);
                        }
                        regulator_put(vreg);
                        fpc1020->vreg[i] = NULL;
                }
                rc = 0;
        }
        return rc;
}

//#ifdef OPLUS_FEATURE_FINGERPRINT
static DEFINE_SPINLOCK(fpc1020_lock);

static int fpc1020_enable_irq(struct fpc1020_data *fpc1020, bool enable)
{
        spin_lock_irq(&fpc1020_lock);
        if (enable) {
                if (!fpc1020->irq_enabled) {
                        enable_irq(gpio_to_irq(fpc1020->irq_gpio));
                        enable_irq_wake(gpio_to_irq(fpc1020->irq_gpio));
                        fpc1020->irq_enabled = 1;
                        dev_info(fpc1020->dev, "%s: enable\n", __func__);
                } else {
                        /*dev_info(fpc1020->dev, "%s: no need enable\n", __func__);*/
                }
        } else {
                if (fpc1020->irq_enabled) {
                        disable_irq_wake(gpio_to_irq(fpc1020->irq_gpio));
                        disable_irq_nosync(gpio_to_irq(fpc1020->irq_gpio));
                        fpc1020->irq_enabled = 0;
                        dev_info(fpc1020->dev, "%s: disable\n", __func__);
                } else {
                        /*dev_info(fpc1020->dev, "%s: no need disable\n", __func__);*/
                }
        }
        spin_unlock_irq(&fpc1020_lock);

        return 0;
}
//#endif

/**
 * sysf node to check the interrupt status of the sensor, the interrupt
 * handler should perform sysf_notify to allow userland to poll the node.
 */
static ssize_t irq_get(struct device *device,
                struct device_attribute *attribute,
                char *buffer)
{
        struct fpc1020_data* fpc1020 = dev_get_drvdata(device);
        int irq = gpio_get_value(fpc1020->irq_gpio);
        return scnprintf(buffer, PAGE_SIZE, "%i\n", irq);
}


/**
 * writing to the irq node will just drop a printk message
 * and return success, used for latency measurement.
 */
static ssize_t irq_ack(struct device *device,
                struct device_attribute* attribute,
                const char *buffer, size_t count)
{
        struct fpc1020_data* fpc1020 = dev_get_drvdata(device);
        dev_dbg(fpc1020->dev, "%s\n", __func__);
        return count;
}


static ssize_t regulator_enable_set(struct device *dev,
                struct device_attribute *attribute, const char *buffer, size_t count)
{
        int op = 0;
        bool enable = false;
        int rc = 0;
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
        if (1 == sscanf(buffer, "%d", &op)) {
                if (op == 1) {
                        enable = true;
                }
                else if (op == 0) {
                        enable = false;
                }
        } else {
                printk("invalid content: '%s', length = %zd\n", buffer, count);
                return -EINVAL;
        }
        rc = vreg_setup(fpc1020, "vdd_io", enable);
        rc = vreg_setup(fpc1020, "vmch", enable);
        rc = vreg_setup(fpc1020, "avdd_io", enable);
        return rc ? rc : count;
}

//#ifdef OPLUS_FEATURE_FINGERPRINT
static ssize_t irq_enable_set(struct device *dev,
                struct device_attribute *attribute, const char *buffer, size_t count)
{
        int op = 0;
        bool enable = false;
        int rc = 0;
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
        if (1 == sscanf(buffer, "%d", &op)) {
                if (op == 1) {
                        enable = true;
                }
                else if (op == 0) {
                        enable = false;
                }
        } else {
                printk("invalid content: '%s', length = %zd\n", buffer, count);
                return -EINVAL;
        }
        rc = fpc1020_enable_irq(fpc1020,  enable);
        return rc ? rc : count;
}

static ssize_t irq_enable_get(struct device *dev,
                struct device_attribute* attribute,
                char *buffer)
{
        struct fpc1020_data* fpc1020 = dev_get_drvdata(dev);
        return scnprintf(buffer, PAGE_SIZE, "%i\n", fpc1020->irq_enabled);
}
//#endif

static ssize_t wakelock_enable_set(struct device *dev,
                struct device_attribute *attribute, const char *buffer, size_t count)
{
        int op = 0;
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
        if (1 == sscanf(buffer, "%d", &op)) {
                if (op == WAKELOCK_ENABLE) {
                        wake_lock(&fpc1020->fpc_wl);
                        /*dev_info(dev, "%s, fpc wake_lock\n", __func__);*/
                } else if (op == WAKELOCK_DISABLE) {
                        wake_unlock(&fpc1020->fpc_wl);
                        /*dev_info(dev, "%s, fpc wake_unlock\n", __func__);*/
                } else if (op == WAKELOCK_TIMEOUT_ENABLE) {
                        wake_lock_timeout(&fpc1020->ttw_wl, msecs_to_jiffies(FPC_TTW_HOLD_TIME));
                        /*dev_info(dev, "%s, fpc wake_lock timeout\n", __func__);*/
                } else if (op == WAKELOCK_TIMEOUT_DISABLE) {
                        wake_unlock(&fpc1020->ttw_wl);
                        /*dev_info(dev, "%s, fpc wake_unlock timeout\n", __func__);*/
                }
        } else {
                printk("invalid content: '%s', length = %zd\n", buffer, count);
                return -EINVAL;
        }

        return count;
}

static DEVICE_ATTR(irq, S_IRUSR | S_IWUSR, irq_get, irq_ack);
static DEVICE_ATTR(regulator_enable, S_IWUSR, NULL, regulator_enable_set);

//#ifdef OPLUS_FEATURE_FINGERPRINT
static DEVICE_ATTR(irq_enable, S_IWUSR, irq_enable_get, irq_enable_set);
//#endif

static DEVICE_ATTR(wakelock_enable, S_IWUSR, NULL, wakelock_enable_set);

static struct attribute *attributes[] = {
        /*
           &dev_attr_hw_reset.attr,
           */
        &dev_attr_irq.attr,
        &dev_attr_regulator_enable.attr,
        &dev_attr_irq_enable.attr,
        &dev_attr_wakelock_enable.attr,
        NULL
};

static const struct attribute_group attribute_group = {
        .attrs = attributes,
};

static irqreturn_t fpc1020_irq_handler(int irq, void *handle)
{
        struct fpc1020_data *fpc1020 = handle;
        dev_info(fpc1020->dev, "%s\n", __func__);

        /* Make sure 'wakeup_enabled' is updated before using it
         ** since this is interrupt context (other thread...) */
        smp_rmb();
        /*
           if (fpc1020->wakeup_enabled ) {
           wake_lock_timeout(&fpc1020->ttw_wl, msecs_to_jiffies(FPC_TTW_HOLD_TIME));
           }
           */

        wake_lock_timeout(&fpc1020->fpc_irq_wl, msecs_to_jiffies(FPC_IRQ_WAKELOCK_TIMEOUT));

        sysfs_notify(&fpc1020->dev->kobj, NULL, dev_attr_irq.attr.name);

        return IRQ_HANDLED;
}

static int fpc1020_probe(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;
        int rc = 0;
        int irqf;
        struct device_node *np = dev->of_node;

        struct fpc1020_data *fpc1020 = devm_kzalloc(dev, sizeof(*fpc1020),
                        GFP_KERNEL);

        if (!fpc1020) {
                dev_err(dev, "failed to allocate memory for struct fpc1020_data\n");
                rc = -ENOMEM;
                goto ERR_ALLOC;
        }

        fpc1020->dev = dev;
        dev_info(fpc1020->dev, "-->%s\n", __func__);
        dev_set_drvdata(dev, fpc1020);

        if (!np) {
                dev_err(dev, "no of node found\n");
                rc = -EINVAL;
                goto ERR_BEFORE_WAKELOCK;
        }

        if ((FP_FPC_1140 != get_fpsensor_type())
                        &&(FP_FPC_1260 != get_fpsensor_type())
                        &&(FP_FPC_1022 != get_fpsensor_type())
                        &&(FP_FPC_1023 != get_fpsensor_type())
                        &&(FP_FPC_1023_GLASS != get_fpsensor_type())
                        &&(FP_FPC_1270 != get_fpsensor_type())
                        &&(FP_FPC_1511 != get_fpsensor_type())
                        &&(FP_FPC_1541 != get_fpsensor_type())
                        &&(FP_FPC_1542 != get_fpsensor_type())) {
                dev_err(dev, "found not fpc sensor\n");
                rc = -EINVAL;
                goto ERR_BEFORE_WAKELOCK;
        }
        dev_info(dev, "found fpc sensor\n");

        wake_lock_init(&fpc1020->ttw_wl, WAKE_LOCK_SUSPEND, "fpc_ttw_wl");
        wake_lock_init(&fpc1020->fpc_wl, WAKE_LOCK_SUSPEND, "fpc_wl");
        wake_lock_init(&fpc1020->fpc_irq_wl, WAKE_LOCK_SUSPEND, "fpc_irq_wl");

        rc = fpc1020_request_named_gpio(fpc1020, "fpc,irq-gpio",
                        &fpc1020->irq_gpio);
        if (rc) {
                goto ERR_AFTER_WAKELOCK;
        }

        /*dev_info(fpc1020->dev, "fpc1020 requested gpio finished \n");*/

        irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
        mutex_init(&fpc1020->lock);
        rc = devm_request_threaded_irq(dev, gpio_to_irq(fpc1020->irq_gpio),
                        NULL, fpc1020_irq_handler, irqf,
                        dev_name(dev), fpc1020);
        if (rc) {
                dev_err(dev, "could not request irq %d\n",
                                gpio_to_irq(fpc1020->irq_gpio));
                goto ERR_AFTER_WAKELOCK;
        }
        /*dev_info(fpc1020->dev, "requested irq %d\n", gpio_to_irq(fpc1020->irq_gpio));*/

        /* Request that the interrupt should be wakeable */
        /*enable_irq_wake( gpio_to_irq( fpc1020->irq_gpio ) );*/

//#ifdef OPLUS_FEATURE_FINGERPRINT
        disable_irq_nosync(gpio_to_irq(fpc1020->irq_gpio));
        fpc1020->irq_enabled = 0;
//#endif

        rc = sysfs_create_group(&dev->kobj, &attribute_group);
        if (rc) {
                dev_err(dev, "could not create sysfs\n");
                goto ERR_AFTER_WAKELOCK;
        }

        rc = vreg_setup(fpc1020, "vdd_io", true);
        rc = vreg_setup(fpc1020, "vmch", true);
        rc = vreg_setup(fpc1020, "avdd_io", true);
        if (rc) {
                dev_err(fpc1020->dev,
                                "vreg_setup failed.\n");
                goto ERR_AFTER_WAKELOCK;
        }

        dev_info(fpc1020->dev, "%s: ok\n", __func__);
        return 0;

ERR_AFTER_WAKELOCK:
        wake_lock_destroy(&fpc1020->ttw_wl);
        wake_lock_destroy(&fpc1020->fpc_wl);
        wake_lock_destroy(&fpc1020->fpc_irq_wl);
ERR_BEFORE_WAKELOCK:
        dev_err(fpc1020->dev, "%s failed rc = %d\n", __func__, rc);
        devm_kfree(fpc1020->dev, fpc1020);
ERR_ALLOC:
        return rc;
}


static struct of_device_id fpc1020_of_match[] = {
        { .compatible = "fpc,fpc1020", },
        {}
};
MODULE_DEVICE_TABLE(of, fpc1020_of_match);

static struct platform_driver fpc1020_driver = {
        .driver = {
                .name                   = "fpc1020",
                .owner                  = THIS_MODULE,
                .of_match_table = fpc1020_of_match,
        },
        .probe = fpc1020_probe,
};

static int __init fpc1020_init(void)
{
        if (platform_driver_register(&fpc1020_driver)) {
                pr_err("platform_driver_register fail");
                return -EINVAL;
        }
        return 0;
}

static void __exit fpc1020_exit(void)
{
        platform_driver_unregister(&fpc1020_driver);
}

late_initcall(fpc1020_init);
module_exit(fpc1020_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aleksej Makarov");
MODULE_AUTHOR("Henrik Tillman <henrik.tillman@fingerprints.com>");
MODULE_DESCRIPTION("FPC1020 Fingerprint sensor device driver.");
