// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/sys_soc.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <soc/oplus/system/oplus_project.h>
#include <soc/oplus/system/boot_mode.h>
#include <linux/soc/qcom/smem.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <soc/qcom/subsystem_restart.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/kobject.h>
#include <linux/pm_wakeup.h>

#define RF_CABLE_OUT        0
#define RF_CABLE_IN            1

#define CABLE_0                0
#define CABLE_1                1

#define INVALID_GPIO          -2

#define PAGESIZE 512
#define SMEM_DRAM_TYPE 136
#define MAX_CABLE_STS 4
#define MAX_STR_SIZE 32

struct rf_info_type {
    union {
        unsigned int        rf_cable_sts;
        unsigned char        rf_cable_gpio_sts[MAX_CABLE_STS];
    };
    union {
        unsigned int        rf_pds_sts;
        unsigned char        rf_pds_gpio_sts[MAX_CABLE_STS];
    };
    unsigned int        reserver[6];

};

struct rf_cable_data {
    int             cable_irq[MAX_CABLE_STS];
    int             cable_gpio[MAX_CABLE_STS];
    union {
        unsigned int        rf_cable_sts;
        unsigned char        rf_cable_gpio_sts[MAX_CABLE_STS];
    };
    int             rf_cable_support_num;
    int             pds_irq[MAX_CABLE_STS];
    int             pds_gpio[MAX_CABLE_STS];
    union {
        unsigned int        rf_pds_sts;
        unsigned char        rf_pds_gpio_sts[MAX_CABLE_STS];
    };
    int             rf_pds_support_num;
    struct             device *dev;
    //struct             wakeup_source cable_ws;
    struct             pinctrl *gpio_pinctrl;
    struct             pinctrl_state *rf_cable_active;
    struct             pinctrl_state *rf_pds_active;
    struct rf_info_type *the_rf_format;
};

enum {
    SMEM_APPS         =  0,                     /**< Apps Processor */
    SMEM_MODEM        =  1,                     /**< Modem processor */
    SMEM_ADSP         =  2,                     /**< ADSP processor */
    SMEM_SSC          =  3,                     /**< Sensor processor */
    SMEM_WCN          =  4,                     /**< WCN processor */
    SMEM_CDSP         =  5,                     /**< Reserved */
    SMEM_RPM          =  6,                     /**< RPM processor */
    SMEM_TZ           =  7,                     /**< TZ processor */
    SMEM_SPSS         =  8,                     /**< Secure processor */
    SMEM_HYP          =  9,                     /**< Hypervisor */
    SMEM_NUM_HOSTS    = 10,                     /**< Max number of host in target */
};

//=====================================
static ssize_t cable_read_proc(struct file *file, char __user *buf, size_t count, loff_t *off)
{
    struct rf_cable_data *rf_data = PDE_DATA(file_inode(file));
    char page[128] = {0};
    int len = 0;
    int i;

    if(!rf_data || !rf_data->the_rf_format) {
        return -EFAULT;
    }

    for (i = 0; i < MAX_CABLE_STS; i++) {
        if (gpio_is_valid(rf_data->cable_gpio[i])) {
            rf_data->rf_cable_gpio_sts[i] = gpio_get_value(rf_data->cable_gpio[i]);
        } else {
            rf_data->rf_cable_gpio_sts[i] = 0xFF;
        }
    }

    rf_data->the_rf_format->rf_cable_sts = rf_data->rf_cable_sts;

	len += sprintf(&page[len], "%hhu,%hhu,%hhu,%hhu\n",
                   rf_data->the_rf_format->rf_cable_gpio_sts[0],
                   rf_data->the_rf_format->rf_cable_gpio_sts[1],
                   rf_data->the_rf_format->rf_cable_gpio_sts[2],
                   rf_data->the_rf_format->rf_cable_gpio_sts[3]);

    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }
    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

static ssize_t pds_read_proc(struct file *file, char __user *buf, size_t count, loff_t *off)
{
    struct rf_cable_data *rf_data = PDE_DATA(file_inode(file));
    char page[128] = {0};
    int len = 0;
    int i;

    if (!rf_data || !rf_data->the_rf_format) {
        return -EFAULT;
    }

    for (i = 0; i < MAX_CABLE_STS; i++) {
        if (gpio_is_valid(rf_data->pds_gpio[i])) {
            rf_data->rf_pds_gpio_sts[i] = gpio_get_value(rf_data->pds_gpio[i]);
        } else {
            rf_data->rf_pds_gpio_sts[i] = 0xFF;
        }
    }

    if(rf_data->the_rf_format->rf_pds_sts != rf_data->rf_pds_sts) {
        rf_data->the_rf_format->rf_pds_sts = rf_data->rf_pds_sts;
    }

	len += sprintf(&page[len], "%hhu,%hhu,%hhu,%hhu\n",
                   rf_data->the_rf_format->rf_pds_gpio_sts[0],
                   rf_data->the_rf_format->rf_pds_gpio_sts[1],
                   rf_data->the_rf_format->rf_pds_gpio_sts[2],
                   rf_data->the_rf_format->rf_pds_gpio_sts[3]);
    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, page, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;
    return (len < count ? len : count);
}

struct file_operations cable_proc_fops_cable = {
    .read = cable_read_proc,
};

struct file_operations cable_proc_fops_pds = {
    .read = pds_read_proc,
};

//=====================================

static irqreturn_t cable_interrupt(int irq, void *_dev)
{
    struct rf_cable_data *rf_data =  (struct rf_cable_data *)_dev;
    int i;

    pr_err("%s enter\n", __func__);
    if (!rf_data || !rf_data->the_rf_format) {
        pr_err("%s the_rf_data null\n", __func__);
        return IRQ_HANDLED;
    }
    //__pm_stay_awake(&rf_data->cable_ws);
    usleep_range(10000, 20000);
    for (i = 0; i < MAX_CABLE_STS; i++) {
        if (gpio_is_valid(rf_data->cable_gpio[i])) {
            rf_data->rf_cable_gpio_sts[i] = gpio_get_value(rf_data->cable_gpio[i]);
        } else {
            rf_data->rf_cable_gpio_sts[i] = 0xFF;
        }
    }

    for (i = 0; i < MAX_CABLE_STS; i++) {
        if (gpio_is_valid(rf_data->pds_gpio[i])) {
            rf_data->rf_pds_gpio_sts[i] = gpio_get_value(rf_data->pds_gpio[i]);
        } else {
            rf_data->rf_pds_gpio_sts[i] = 0xFF;
        }
    }

    rf_data->the_rf_format->rf_cable_sts = rf_data->rf_cable_sts;
    if(rf_data->the_rf_format->rf_pds_sts != rf_data->rf_pds_sts) {
        rf_data->the_rf_format->rf_pds_sts = rf_data->rf_pds_sts;
    }

    //__pm_relax(&rf_data->cable_ws);

    return IRQ_HANDLED;
}

static int rf_cable_initial_request_irq(struct rf_cable_data *rf_data)
{
    int rc = 0;
    int i;

    for (i = 0; i < rf_data->rf_cable_support_num; i++) {
        if (gpio_is_valid(rf_data->cable_gpio[i])) {

            rc = devm_request_threaded_irq(rf_data->dev,rf_data->cable_irq[i], NULL, cable_interrupt,
                                      IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "rf_cable_irq", rf_data);
            if (rc) {
                pr_err("%s cable%d_gpio, request falling fail\n",
                       __func__, i);
                return rc;
            }
        }
    }


    //pds check
    for (i = 0; i < rf_data->rf_pds_support_num; i++) {
        if (gpio_is_valid(rf_data->pds_gpio[i])) {

            rc = devm_request_threaded_irq(rf_data->dev,rf_data->pds_irq[i], NULL, cable_interrupt,
                                      IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "rf_pds_irq", rf_data);
            if (rc) {
                pr_err("%s pds%d_gpio, request falling fail\n",
                       __func__, i);
                return rc;
            }
            //enable_irq(rf_data->pds0_irq);
        }
    }

    return rc;
}


static int rf_cable_gpio_pinctrl_init
(struct platform_device *pdev, struct device_node *np, struct rf_cable_data *rf_data)
{
    int retval = 0;
    char gpio_str[MAX_STR_SIZE];
    int i;

    //request gpio.
    for (i = 0; i < MAX_CABLE_STS; i++) {
        snprintf(gpio_str, MAX_STR_SIZE, "rf,cable%d-gpio", i);
        if (i < rf_data->rf_cable_support_num) {
            rf_data->cable_gpio[i] = of_get_named_gpio(np, gpio_str, 0);
        } else {
            rf_data->cable_gpio[i] = INVALID_GPIO;
        }

        snprintf(gpio_str, MAX_STR_SIZE, "rf,pds%d-gpio", i);
        if (i < rf_data->rf_pds_support_num) {
            rf_data->pds_gpio[i] = of_get_named_gpio(np, gpio_str, 0);
        } else {
            rf_data->pds_gpio[i] = INVALID_GPIO;
        }
    }

    rf_data->gpio_pinctrl = devm_pinctrl_get(&(pdev->dev));

    if (IS_ERR_OR_NULL(rf_data->gpio_pinctrl)) {
        retval = PTR_ERR(rf_data->gpio_pinctrl);
        pr_err("%s get gpio_pinctrl fail, retval:%d\n", __func__, retval);
        goto err_pinctrl_get;
    }

    rf_data->rf_cable_active = pinctrl_lookup_state(rf_data->gpio_pinctrl, "rf_cable_active");
    rf_data->rf_pds_active = pinctrl_lookup_state(rf_data->gpio_pinctrl, "rf_pds_active");
    if (!IS_ERR_OR_NULL(rf_data->rf_cable_active)) {
        retval = pinctrl_select_state(rf_data->gpio_pinctrl,
                                      rf_data->rf_cable_active);
        if (retval < 0) {
            pr_err("%s select pinctrl fail, retval:%d\n", __func__, retval);
            goto err_pinctrl_lookup;
        }
    }

    if (!IS_ERR_OR_NULL(rf_data->rf_pds_active)) {
        retval = pinctrl_select_state(rf_data->gpio_pinctrl,
                                      rf_data->rf_pds_active);
        if (retval < 0) {
            pr_err("%s select pinctrl fail, retval:%d\n", __func__, retval);
            goto err_pinctrl_lookup;
        }
    }
    mdelay(5);
    for (i = 0; i < MAX_CABLE_STS; i++) {
        if (gpio_is_valid(rf_data->cable_gpio[i])) {
            gpio_direction_input(rf_data->cable_gpio[i]);
            rf_data->cable_irq[i] = gpio_to_irq(rf_data->cable_gpio[i]);
            if (rf_data->cable_irq[i] < 0) {
                pr_err("Unable to get irq number for GPIO %d, error %d\n",
                       rf_data->cable_gpio[i], rf_data->cable_irq[i]);
                goto err_pinctrl_lookup;
            }
            rf_data->rf_cable_gpio_sts[i] = gpio_get_value(rf_data->cable_gpio[i]);
        } else {
            rf_data->rf_cable_gpio_sts[i] = 0xFF;
        }
    }

    for (i = 0; i < MAX_CABLE_STS; i++) {
        if (gpio_is_valid(rf_data->pds_gpio[i])) {
            gpio_direction_input(rf_data->pds_gpio[i]);
            rf_data->pds_irq[i] = gpio_to_irq(rf_data->pds_gpio[i]);
            if (rf_data->pds_irq[i] < 0) {
                pr_err("Unable to get irq number for GPIO %d, error %d\n",
                       rf_data->pds_gpio[i], rf_data->pds_irq[i]);
                goto err_pinctrl_lookup;
            }
            rf_data->rf_pds_gpio_sts[i] = gpio_get_value(rf_data->pds_gpio[i]);
        } else {
            rf_data->rf_pds_gpio_sts[i] = 0xFF;
        }
    }


    if(rf_data->the_rf_format) {
        rf_data->the_rf_format->rf_cable_sts = rf_data->rf_cable_sts;
        rf_data->the_rf_format->rf_pds_sts = rf_data->rf_pds_sts;
    }
    return 0;

err_pinctrl_lookup:
    devm_pinctrl_put(rf_data->gpio_pinctrl);
err_pinctrl_get:
    rf_data->gpio_pinctrl = NULL;
    return -1;
}


static int op_rf_cable_probe(struct platform_device *pdev)
{
    int rc = 0, ret = 0;
    size_t smem_size;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    struct rf_cable_data *rf_data = NULL;
    unsigned int len = (sizeof(struct rf_info_type) + 3) & (~0x3);
    struct proc_dir_entry *oplus_rf = NULL;
    int i;

    pr_err("%s enter!\n", __func__);

    rf_data = devm_kzalloc(dev, sizeof(struct rf_cable_data), GFP_KERNEL);
    if (!rf_data) {
        pr_err("%s: failed to allocate memory.\n", __func__);
        rc = -ENOMEM;
        goto exit_nomem;
    }

    rc = of_property_read_u32(np, "rf_cable_support_num", &rf_data->rf_cable_support_num);
    if (rc < 0) {
        rf_data->rf_cable_support_num = 0;
        pr_err("%s: get rf_cable_support_num error.\n", __func__);
    } else {
        if (rf_data->rf_cable_support_num > MAX_CABLE_STS) {
            rf_data->rf_cable_support_num = MAX_CABLE_STS;
            pr_err("%s: rf_cable_support_num is too much.\n", __func__);
        }
    }

    rc = of_property_read_u32(np, "rf_pds__support_num", &rf_data->rf_pds_support_num);
    if (rc < 0) {
        rf_data->rf_pds_support_num = 0;
        pr_err("%s: get rf_pds__support_num error.\n", __func__);
    } else {
        if (rf_data->rf_pds_support_num > MAX_CABLE_STS) {
            rf_data->rf_pds_support_num = MAX_CABLE_STS;
            pr_err("%s: rf_pds_support_num is too much.\n", __func__);
        }
    }

    ret = qcom_smem_alloc(SMEM_APPS, SMEM_DRAM_TYPE, len);
    if (ret < 0 && ret != -EEXIST) {
        pr_err("%s smem_alloc fail.\n", __func__);
        rc = -EFAULT;
        goto exit_efault;
    }

    rf_data->the_rf_format = (struct rf_info_type *)qcom_smem_get(SMEM_APPS, SMEM_DRAM_TYPE, &smem_size);
    if (IS_ERR(rf_data->the_rf_format) || NULL == rf_data->the_rf_format) {
        pr_err("%s smem_get fail.\n", __func__);
        rc = -EFAULT;
        goto exit_efault;
    }

    if (get_boot_mode() == MSM_BOOT_MODE__RF || get_boot_mode() == MSM_BOOT_MODE__WLAN) {
        pr_err("%s: rf/wlan mode FOUND! use 1 always.\n", __func__);
        rf_data->the_rf_format->rf_cable_sts = 0xFFFF;
        for (i = 0; i < rf_data->rf_cable_support_num; i++) {
            rf_data->the_rf_format->rf_cable_gpio_sts[i] = RF_CABLE_IN;
        }
        return 0;
    }

    rf_data->dev = dev;
    dev_set_drvdata(dev, rf_data);
    //wakeup_source_init(&rf_data->cable_ws, "rf_cable_wake_lock");
    rc = rf_cable_gpio_pinctrl_init(pdev, np, rf_data);
    if (rc) {
        pr_err("%s gpio_init fail.\n", __func__);
        goto exit;
    }

    rc = rf_cable_initial_request_irq(rf_data);
    if (rc) {
        pr_err("could not request cable_irq.\n");
        goto exit;
    }

    oplus_rf = proc_mkdir("oplus_rf", NULL);
    if (!oplus_rf) {
        pr_err("can't create oplus_rf proc.\n");
        goto exit;
    }

    if (rf_data->rf_cable_support_num) {
        proc_create_data("rf_cable", S_IRUGO, oplus_rf, &cable_proc_fops_cable, rf_data);
    }

    if (rf_data->rf_pds_support_num) {
        proc_create_data("rf_pds", S_IRUGO, oplus_rf, &cable_proc_fops_pds, rf_data);
    }

    pr_err("%s: probe ok, rf_cable, SMEM_RF_INFO:%d, sts:%d, "\
           "[0_gpio:%d, 0_val:%d], "\
           "[1_gpio:%d, 1_val:%d], "\
           "[2_gpio:%d, 2_val:%d], "\
           "[3_gpio:%d, 3_val:%d], "\
           "0_irq:%d, 1_irq:%d, "\
           "2_irq:%d, 3_irq:%d.\n",
           __func__, SMEM_DRAM_TYPE, rf_data->the_rf_format->rf_cable_sts,
           rf_data->cable_gpio[0], rf_data->the_rf_format->rf_cable_gpio_sts[0],
           rf_data->cable_gpio[1], rf_data->the_rf_format->rf_cable_gpio_sts[1],
           rf_data->cable_gpio[2], rf_data->the_rf_format->rf_cable_gpio_sts[2],
           rf_data->cable_gpio[3], rf_data->the_rf_format->rf_cable_gpio_sts[3],
           rf_data->cable_irq[0], rf_data->cable_irq[1],
           rf_data->cable_irq[2], rf_data->cable_irq[3]);

    pr_err("%s: probe ok, rf_pds, SMEM_RF_INFO:%d, sts:%d, "\
           "[0_gpio:%d, 0_val:%d], "\
           "[1_gpio:%d, 1_val:%d], "\
           "[2_gpio:%d, 2_val:%d], "\
           "[3_gpio:%d, 3_val:%d], "\
           "0_irq:%d, 1_irq:%d, "\
           "2_irq:%d, 3_irq:%d.\n",
           __func__, SMEM_DRAM_TYPE, rf_data->the_rf_format->rf_pds_sts,
           rf_data->pds_gpio[0], rf_data->the_rf_format->rf_pds_gpio_sts[0],
           rf_data->pds_gpio[1], rf_data->the_rf_format->rf_pds_gpio_sts[1],
           rf_data->pds_gpio[2], rf_data->the_rf_format->rf_pds_gpio_sts[2],
           rf_data->pds_gpio[3], rf_data->the_rf_format->rf_pds_gpio_sts[3],
           rf_data->pds_irq[0], rf_data->pds_irq[1],
           rf_data->pds_irq[2], rf_data->pds_irq[3]);

    return 0;

exit:
    //wakeup_source_trash(&rf_data->cable_ws);

exit_efault:
    //kfree(rf_data);
exit_nomem:
    pr_err("%s: probe Fail!\n", __func__);

    return rc;
}

static int op_rf_cable_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct rf_cable_data *rf_data = dev_get_drvdata(dev);

    if (rf_data) {
        pr_err("%s test 1111!\n", __func__);
        remove_proc_subtree("oplus_rf", NULL);
        //wakeup_source_trash(&rf_data->cable_ws);
    }
    pr_err("%s enter!\n", __func__);

	return 0;
}


static const struct of_device_id rf_of_match[] = {
    { .compatible = "oplus,rf_cable", },
    {}
};
MODULE_DEVICE_TABLE(of, rf_of_match);

static struct platform_driver op_rf_cable_driver = {
    .driver = {
        .name       = "rf_cable",
        .owner      = THIS_MODULE,
        .of_match_table = rf_of_match,
    },
    .probe = op_rf_cable_probe,
    .remove = op_rf_cable_remove,
};

static int __init op_rf_cable_init(void)
{
    int ret;

    ret = platform_driver_register(&op_rf_cable_driver);
    if (ret)
        pr_err("rf_cable_driver register failed: %d\n", ret);

    return ret;
}

static void __exit op_rf_cable_exit(void)
{
    platform_driver_unregister(&op_rf_cable_driver);
}


MODULE_LICENSE("GPL v2");
late_initcall(op_rf_cable_init);
module_exit(op_rf_cable_exit);

