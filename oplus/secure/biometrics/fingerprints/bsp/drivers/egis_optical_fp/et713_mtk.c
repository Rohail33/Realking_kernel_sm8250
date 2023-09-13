// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#endif
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/fb.h>
// #include <linux/wakelock.h>
#include "../include/wakelock.h"
#include <linux/notifier.h>
struct wake_lock et713_wake_lock;

#include "et713_mtk.h"

extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
/*
 * FPS interrupt table
 */
struct interrupt_desc fps_ints = {0 , 0, "BUT0" , 0};
unsigned int bufsiz = 4096;
int gpio_irq;
int request_irq_done = 0;
int egistec_platformInit_done = 0;
struct regulator *buck;

#define EDGE_TRIGGER_FALLING    0x0
#define EDGE_TRIGGER_RISING    0x1
#define LEVEL_TRIGGER_LOW       0x2
#define LEVEL_TRIGGER_HIGH      0x3
#define WAKELOCK_HOLD_TIME 500 /* in ms */
int egistec_platformInit(struct egistec_data *egistec);
int egistec_platformFree(struct egistec_data *egistec);
struct ioctl_cmd {
int int_mode;
int detect_period;
int detect_threshold;
};
int g_screen_onoff = 1;
void delete_device_node(void);
struct egistec_data *g_data;
DECLARE_BITMAP(minors, N_SPI_MINORS);
LIST_HEAD(device_list);
DEFINE_MUTEX(device_list_lock);
struct of_device_id egistec_match_table[] = {
//  { .compatible = "mediatek,finger-fp",},
    { .compatible = "goodix,goodix_fp", },
//  { .compatible = "fp-id",},
    {},
};

struct of_device_id et713_spi_of_match[] = {
//  { .compatible = "mediatek,fingerspi-fp", },
    { .compatible = "oplus,oplus_fp", },
    {}
};

MODULE_DEVICE_TABLE(of, et713_spi_of_match);



MODULE_DEVICE_TABLE(of, egistec_match_table);

void spi_clk_enable(u8 bonoff)
{
    if (bonoff) {
        pr_err("EGISTEC %s line:%d enable spi clk\n", __func__, __LINE__);
        mt_spi_enable_master_clk(g_data->spi);
    } else {
        pr_err("EGISTEC %s line:%d disable spi clk\n", __func__, __LINE__);
        mt_spi_disable_master_clk(g_data->spi);
    }
}

/* ------------------------------ Interrupt -----------------------------*/
/*
 * Interrupt description
 */

#define FP_INT_DETECTION_PERIOD  10
#define FP_DETECTION_THRESHOLD   10

static DECLARE_WAIT_QUEUE_HEAD(interrupt_waitq);

irqreturn_t fp_eint_func(int irq, void *dev_id)
{
    DEBUG_PRINT("EGISTEC fp_eint_func \n");
    disable_irq_nosync(gpio_irq);
    fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
    fps_ints.finger_on = 1;
    wake_up_interruptible(&interrupt_waitq);
    return IRQ_HANDLED;
}

irqreturn_t fp_eint_func_ll(int irq , void *dev_id)
{
    DEBUG_PRINT("[EGISTEC]fp_eint_func_ll\n");
    fps_ints.finger_on = 1;
    disable_irq_nosync(gpio_irq);
    fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
    wake_up_interruptible(&interrupt_waitq);
    return IRQ_RETVAL(IRQ_HANDLED);
}

unsigned int lasttouchmode = 0;
static int egis_opticalfp_irq_handler(struct fp_underscreen_info *tp_info)
{
    char msg = 0;
    printk(KERN_EMERG "guq_warning  egis optical  egis handler start\n");
    g_data->fp_tpinfo = *tp_info;
    if (tp_info->touch_state == lasttouchmode) {
        return IRQ_HANDLED;
    }
    wake_lock_timeout(&et713_wake_lock, msecs_to_jiffies(WAKELOCK_HOLD_TIME));
    if (1 == tp_info->touch_state) {
        msg = EGIS_NET_EVENT_TP_TOUCHDOWN;
        printk(KERN_EMERG "guq_warning  egis touch down start\n");
        DEBUG_PRINT("[egis] NET TOUCH DOWN!\n");
        egis_sendnlmsg(&msg);
        lasttouchmode = tp_info->touch_state;
    } else {
        msg = EGIS_NET_EVENT_TP_TOUCHUP;
        DEBUG_PRINT("[egis] NET TOUCH UP!\n");
        egis_sendnlmsg(&msg);
        lasttouchmode = tp_info->touch_state;
    }

    return IRQ_HANDLED;
}

static int egis_fb_state_chg_callback(struct notifier_block *nb,
        unsigned long val, void *data)
{
    struct egistec_data *e_data;
    struct fb_event *evdata = data;
    unsigned int blank;
    char msg = 0;
    int retval = 0;

    pr_info("[info] %s go to the egis_fb_state_chg_callback value = %d\n",
            __func__, (int)val);
    e_data = container_of(nb, struct egistec_data, notifier);

    if (val == MTK_ONSCREENFINGERPRINT_EVENT) {
        uint8_t op_mode = 0x0;
        op_mode = *(uint8_t *)evdata->data;

        switch (op_mode) {
        case EGIS_UI_DISAPPREAR:
            DEBUG_PRINT("[egis] UI disappear!\n");
            break;
        case EGIS_UI_READY:
            DEBUG_PRINT("[egis] UI ready!\n");
            msg = EGIS_NET_EVENT_UI_READY;
            egis_sendnlmsg(&msg);
            break;
        default:
            DEBUG_PRINT("[egis] Unknown MTK_ONSCREENFINGERPRINT_EVENT!\n");
            break;
        }
        return retval;
    }

    if (evdata && evdata->data && val == FB_EARLY_EVENT_BLANK && e_data) {
        blank = *(int *)(evdata->data);
        switch (blank) {
            case FB_BLANK_POWERDOWN:
                    e_data->fb_black = 1;
                    msg = EGIS_NET_EVENT_SCR_OFF;
                    DEBUG_PRINT("[egis] NET SCREEN OFF!\n");
                    egis_sendnlmsg(&msg);
                break;
            case FB_BLANK_UNBLANK:
                    e_data->fb_black = 0;
                    msg = EGIS_NET_EVENT_SCR_ON;
                    DEBUG_PRINT("[egis] NET SCREEN ON!\n");
                    egis_sendnlmsg(&msg);
                break;
            default:
                DEBUG_PRINT("[egis] Unknown screen state!\n");
                break;
        }
    }
    return NOTIFY_OK;
}

static struct notifier_block egis_noti_block = {
    .notifier_call = egis_fb_state_chg_callback,
};

/*
 *    FUNCTION NAME.
 *        Interrupt_Init
 *
 *    FUNCTIONAL DESCRIPTION.
 *        button initial routine
 *
 *    ENTRY PARAMETERS.
 *        int_mode - determine trigger mode
 *            EDGE_TRIGGER_FALLING    0x0
 *            EDGE_TRIGGER_RAISING    0x1
 *            LEVEL_TRIGGER_LOW        0x2
 *            LEVEL_TRIGGER_HIGH       0x3
 *
 *    EXIT PARAMETERS.
 *        Function Return int
 */

int Interrupt_Init(struct egistec_data *egistec, int int_mode, int detect_period, int detect_threshold)
{
    int err = 0;
    int status = 0;
    struct device_node *node = NULL;
    DEBUG_PRINT("EGISTEC   %s mode = %d period = %d threshold = %d\n", __func__, int_mode, detect_period, detect_threshold);
    DEBUG_PRINT("EGISTEC   %s request_irq_done = %d gpio_irq = %d  pin = %d  \n", __func__, request_irq_done, gpio_irq, egistec->irqPin);

    fps_ints.detect_period = detect_period;
    fps_ints.detect_threshold = detect_threshold;
    fps_ints.int_count = 0;
    fps_ints.finger_on = 0;

    if (request_irq_done == 0) {
        node = of_find_matching_node(node, egistec_match_table);
        if (node) {
            gpio_irq = irq_of_parse_and_map(node, 0);
            printk("EGISTEC fp_irq number %d\n", gpio_irq);
        } else {
            printk("node = of_find_matching_node fail error \n");
        }
        if (gpio_irq < 0) {
            DEBUG_PRINT("EGISTEC %s gpio_to_irq failed\n", __func__);
            status = gpio_irq;
            goto done;
        }

        DEBUG_PRINT("[EGISTEC Interrupt_Init] flag current: %d disable: %d enable: %d\n",
        fps_ints.drdy_irq_flag, DRDY_IRQ_DISABLE, DRDY_IRQ_ENABLE);

        if (int_mode == EDGE_TRIGGER_RISING) {
            DEBUG_PRINT("EGISTEC %s EDGE_TRIGGER_RISING\n", __func__);
            err = request_irq(gpio_irq, fp_eint_func, IRQ_TYPE_EDGE_RISING, "fp_detect-eint", egistec);
            if (err) {
                pr_err("request_irq failed==========%s,%d\n", __func__, __LINE__);
            }
        }
        else if (int_mode == EDGE_TRIGGER_FALLING) {
            DEBUG_PRINT("EGISTEC %s EDGE_TRIGGER_FALLING\n", __func__);
            err = request_irq(gpio_irq, fp_eint_func, IRQ_TYPE_EDGE_FALLING, "fp_detect-eint", egistec);
            if (err) {
                pr_err("request_irq failed==========%s,%d\n", __func__, __LINE__);
            }
        }
        else if (int_mode == LEVEL_TRIGGER_LOW) {
            DEBUG_PRINT("EGISTEC %s LEVEL_TRIGGER_LOW\n", __func__);
            err = request_irq(gpio_irq, fp_eint_func_ll, IRQ_TYPE_LEVEL_LOW, "fp_detect-eint", egistec);
            if (err) {
                pr_err("request_irq failed==========%s,%d\n", __func__, __LINE__);
            }
        }
        else if (int_mode == LEVEL_TRIGGER_HIGH) {
            DEBUG_PRINT("EGISTEC %s LEVEL_TRIGGER_HIGH\n", __func__);
            err = request_irq(gpio_irq, fp_eint_func_ll, IRQ_TYPE_LEVEL_HIGH, "fp_detect-eint", egistec);
            if (err) {
                pr_err("request_irq failed==========%s,%d\n", __func__, __LINE__);
            }
        }
        DEBUG_PRINT("[EGISTEC Interrupt_Init]:gpio_to_irq return: %d\n", gpio_irq);
        DEBUG_PRINT("[EGISTEC Interrupt_Init]:request_irq return: %d\n", err);

        fps_ints.drdy_irq_flag = DRDY_IRQ_ENABLE;
        enable_irq_wake(gpio_irq);
        request_irq_done = 1;
    }
    if (fps_ints.drdy_irq_flag == DRDY_IRQ_DISABLE) {
        fps_ints.drdy_irq_flag = DRDY_IRQ_ENABLE;
        enable_irq_wake(gpio_irq);
        enable_irq(gpio_irq);
    }

done:
    return 0;
}

/*
 *    FUNCTION NAME.
 *        Interrupt_Free
 *
 *    FUNCTIONAL DESCRIPTION.
 *        free all interrupt resource
 *
 *    EXIT PARAMETERS.
 *        Function Return int
 */

int Interrupt_Free(struct egistec_data *egistec)
{
    DEBUG_PRINT("EGISTEC %s\n", __func__);
    fps_ints.finger_on = 0;

    if (fps_ints.drdy_irq_flag == DRDY_IRQ_ENABLE) {
        DEBUG_PRINT("EGISTEC %s (DISABLE IRQ)\n", __func__);
        disable_irq_nosync(gpio_irq);
        fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
    }

    return 0;
}

/*
 *    FUNCTION NAME.
 *        fps_interrupt_re d
 *
 *    FUNCTIONAL DESCRIPTION.
 *        FPS interrupt read status
 *
 *    ENTRY PARAMETERS.
 *        wait poll table structure
 *
 *    EXIT PARAMETERS.
 *        Function Return int
 */

unsigned int fps_interrupt_poll(
struct file *file,
struct poll_table_struct *wait)
{
    unsigned int mask = 0;
    poll_wait(file, &interrupt_waitq, wait);
    if (fps_ints.finger_on) {
        mask |= POLLIN | POLLRDNORM;
    }
    return mask;
}

void fps_interrupt_abort(void)
{
    DEBUG_PRINT("EGISTEC %s\n", __func__);
    fps_ints.finger_on = 0;
    wake_up_interruptible(&interrupt_waitq);
}

/*-------------------------------------------------------------------------*/
void egistec_reset(struct egistec_data *egistec)
{
    DEBUG_PRINT(" %s\n", __func__);
/*
    gpio_set_value(egistec->rstPin, 0);
    mdelay(10);
    gpio_set_value(egistec->rstPin, 1);
    mdelay(10);
*/
}

ssize_t egistec_read(struct file *filp,
    char __user *buf,
    size_t count,
    loff_t *f_pos)
{
    /*Implement by vendor if needed*/
    return 0;
}

ssize_t egistec_write(struct file *filp,
    const char __user *buf,
    size_t count,
    loff_t *f_pos)
{
    /*Implement by vendor if needed*/
    return 0;
}

static void egis_power_onoff(struct egistec_data *egis, int power_onoff)
{
    DEBUG_PRINT("[egis] %s   power_onoff = %d \n", __func__, power_onoff);
    if (power_onoff) {
        gpio_set_value(egis->vcc_33v_Pin, 1);
        mdelay(10);
    } else {
        gpio_set_value(egis->vcc_33v_Pin, 0);
        mdelay(10);
    }
}
static void egis_reset_high_low(struct egistec_data *egis, int reset_high_low)
{
    DEBUG_PRINT("[egis] %s   reset_high_low = %d \n", __func__, reset_high_low);
    if (reset_high_low) {
        gpio_set_value(egis->rstPin, 1);
        mdelay(10);
    } else {
        gpio_set_value(egis->rstPin, 0);
        mdelay(10);
    }
}
static void egis_power_on_reset(struct egistec_data *egis)
{
    DEBUG_PRINT("[egis] %s   egis_power_on_reset \n", __func__);
    gpio_set_value(egis->rstPin, 0);
    mdelay(2);
    gpio_set_value(egis->rstPin, 1);
    mdelay(2);
    gpio_set_value(egis->rstPin, 0);
    mdelay(13);
    gpio_set_value(egis->rstPin, 1);
    mdelay(8);
}

int egistec_spi_pin(struct egistec_data *egistec, int spi_pulllow)
{
    DEBUG_PRINT("%s spi_pulllow = %d\n", __func__, spi_pulllow);
    if (spi_pulllow) {
        pinctrl_select_state(egistec->pinctrl_gpios, egistec->pstate_spi_6mA_pull_low);
    } else {
        pinctrl_select_state(egistec->pinctrl_gpios, egistec->pstate_spi_6mA);
    }
    return 0;
}


long egistec_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    struct egistec_data *egistec;
    struct ioctl_cmd data;
    int status = 0;

    memset(&data, 0, sizeof(data));
    printk(" %s  cmd = 0x%X \n", __func__, cmd);
    egistec = filp->private_data;

    if (!egistec_platformInit_done) {
        /* platform init */
        status = egistec_platformInit(egistec);
    }
    if (status != 0) {
        pr_err(" %s platforminit failed\n", __func__);
    }

    switch (cmd) {
    case INT_TRIGGER_INIT:
        if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
            return -EFAULT;
        }
        DEBUG_PRINT("EGISTEC fp_ioctl >>> fp Trigger function init\n");
        retval = Interrupt_Init(egistec, data.int_mode, data.detect_period, data.detect_threshold);
        DEBUG_PRINT("EGISTEC fp_ioctl trigger init = %x\n", retval);
        break;
    case FP_SENSOR_RESET:
            DEBUG_PRINT("EGISTEC fp_ioctl ioc->opcode == FP_SENSOR_RESET \n");
            egistec_reset(egistec);
        break;
    case FP_POWER_ONOFF:
        if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
            return -EFAULT;
        }
        egis_power_onoff(egistec, data.int_mode);  // Use data.int_mode as power setting. 1 = on, 0 = off.
        DEBUG_PRINT("[egis] %s: egis_power_onoff = %d\n", __func__, data.int_mode);
        break;
    case FP_RESET_SET:
        if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
            return -EFAULT;
        }
        egis_reset_high_low(egistec, data.int_mode);  // Use data.int_mode as reset setting. 1 = on, 0 = off.
        DEBUG_PRINT("[egis] %s: egis_reset_high_low = %d\n", __func__, data.int_mode);
        break;
    case FP_POWER_ON_RESET:
        egis_power_on_reset(egistec);    //    2/2/13/8 Power-on sequence.
        break;
    case FP_SPIPIN_SETTING:
        egistec_spi_pin(egistec, 0);
        break;
    case FP_SPIPIN_PULLLOW:
        egistec_spi_pin(egistec, 1);
        break;

    case INT_TRIGGER_CLOSE:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< fp Trigger function close\n");
            retval = Interrupt_Free(egistec);
            DEBUG_PRINT("EGISTEC fp_ioctl trigger close = %x\n", retval);
        break;
    case INT_TRIGGER_ABORT:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< fp Trigger function close\n");
            fps_interrupt_abort();
        break;
    case FP_FREE_GPIO:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_FREE_GPIO  \n");
            egistec_platformFree(egistec);
        break;
    case FP_WAKELOCK_TIMEOUT_ENABLE: //0Xb1
            DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_WAKELOCK_TIMEOUT_ENABLE  \n");
            wake_lock_timeout(&et713_wake_lock, msecs_to_jiffies(1000));
        break;
    case FP_WAKELOCK_TIMEOUT_DISABLE: //0Xb2
            DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_WAKELOCK_TIMEOUT_DISABLE  \n");
            wake_unlock(&et713_wake_lock);
        break;
    case FP_SPICLK_ENABLE:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_SPICLK_ENABLE  \n");
            spi_clk_enable(1);
        break;
    case FP_SPICLK_DISABLE:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_SPICLK_DISABLE  \n");
            spi_clk_enable(0);
        break;
    case DELETE_DEVICE_NODE:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< DELETE_DEVICE_NODE  \n");
            delete_device_node();
        break;
    case GET_SCREEN_ONOFF:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< GET_SCREEN_ONOFF  \n");
            data.int_mode = g_screen_onoff;
        if (copy_to_user((int __user *)arg, &data, sizeof(data))) {
            return -EFAULT;
        }
        break;
    case FP_CLEAN_TOUCH_FLAG:
            DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_CLEAN_TOUCH_FLAG  \n");
            lasttouchmode = 0;
        break;
    default:
        retval = -ENOTTY;
    break;
    }
    DEBUG_PRINT(" %s done  \n", __func__);
    return (retval);
}

#ifdef CONFIG_COMPAT
long egistec_compat_ioctl(struct file *filp, unsigned int cmd,     unsigned long arg)
{
    return egistec_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define egistec_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

int egistec_open(struct inode *inode, struct file *filp)
{
    struct egistec_data *egistec;
    int status = -ENXIO;
    DEBUG_PRINT("%s\n", __func__);
    mutex_lock(&device_list_lock);
    list_for_each_entry(egistec, &device_list, device_entry)
    {
        if (egistec->devt == inode->i_rdev) {
            status = 0;
            break;
        }
    }
    if (status == 0) {
        if (egistec->buffer == NULL) {
            egistec->buffer = kmalloc(bufsiz, GFP_KERNEL);
            if (egistec->buffer == NULL) {
                pr_debug("open/ENOMEM\n");
                status = -ENOMEM;
            }
        }
        if (status == 0) {
            egistec->users++;
            filp->private_data = egistec;
            nonseekable_open(inode, filp);
        }
    } else {
        pr_debug("%s nothing for minor %d\n", __func__, iminor(inode));
    }
    mutex_unlock(&device_list_lock);
    return status;
}

int egistec_release(struct inode *inode, struct file *filp)
{
    struct egistec_data *egistec;
    DEBUG_PRINT("%s\n", __func__);
    mutex_lock(&device_list_lock);
    egistec = filp->private_data;
    filp->private_data = NULL;
    /* last close? */
    egistec->users--;
    if (egistec->users == 0) {
        int    dofree;
        kfree(egistec->buffer);
        egistec->buffer = NULL;
        /* ... after we unbound from the underlying device? */
        spin_lock_irq(&egistec->spi_lock);
        dofree = (egistec->pd == NULL);
        spin_unlock_irq(&egistec->spi_lock);
        if (dofree) {
            kfree(egistec);
        }
    }
    mutex_unlock(&device_list_lock);
    return 0;
}
int egistec_platformFree(struct egistec_data *egistec)
{
    int status = 0;
    DEBUG_PRINT("%s\n", __func__);
    if (egistec_platformInit_done != 1) {
        return status;
    }
    if (egistec != NULL) {
        if (request_irq_done == 1) {
            free_irq(gpio_irq, NULL);
            request_irq_done = 0;
        }
        gpio_free(egistec->irqPin);
        gpio_free(egistec->rstPin);
    }
    egistec_platformInit_done = 0;
    DEBUG_PRINT("%s successful status=%d\n", __func__, status);
    return status;
}

int egistec_platformInit(struct egistec_data *egistec)
{
    int status = 0;
    DEBUG_PRINT("%s\n", __func__);
    if (egistec != NULL) {
        /* initial 33V power pin */
        status = gpio_request(egistec->vcc_33v_Pin, "egistec-33v-gpio");
        if (status < 0) {
            pr_err("%s gpio_requset egistec_Reset failed\n", __func__);
            gpio_free(egistec->vcc_33v_Pin);
            return status;
        }
        gpio_direction_output(egistec->vcc_33v_Pin, 0);
        gpio_set_value(egistec->vcc_33v_Pin, 0);

        /* Initial Reset Pin */
        status = gpio_request(egistec->rstPin, "egistec-reset-gpio");
        if (status < 0) {
            pr_err("%s gpio_requset egistec_Reset failed\n",    __func__);
            gpio_free(egistec->rstPin);
            return status;
        }
        gpio_direction_output(egistec->rstPin, 0);
        gpio_set_value(egistec->rstPin, 0);
        mdelay(30);
        /* Initial IRQ Pin */
/*
        status = gpio_request(egistec->irqPin, "irq-gpio");
        if (status < 0) {
            pr_err("%s gpio_request egistec_irq failed\n",
                __func__);
            goto egistec_platformInit_irq_failed;
        }
        status = gpio_direction_input(egistec->irqPin);
        if (status < 0) {
            pr_err("%s gpio_direction_input IRQ failed\n",
                __func__);
            goto egistec_platformInit_gpio_init_failed;
        }
*/
    }
    egistec_platformInit_done = 1;
    DEBUG_PRINT("%s successful status=%d\n", __func__, status);
    return status;
}

#if 1
int egistec_parse_dt(struct device *dev, struct egistec_data *data)
{
#ifdef CONFIG_OF
    int ret;
    struct device_node *node = NULL;
    struct platform_device *pdev = NULL;
    printk(KERN_ERR "%s, from dts pinctrl\n", __func__);
    node = of_find_compatible_node(NULL, NULL, "goodix,goodix_fp");
    if (node) {
        data->vcc_33v_Pin = of_get_named_gpio(node, "goodix,pw_en", 0);
        pr_info("vcc_33v_Pin GPIO is %d.  -----\n", data->vcc_33v_Pin);
        if (!gpio_is_valid(data->vcc_33v_Pin)) {
        pr_info("vcc_33v_Pin GPIO is invalid.\n");
        return -ENODEV;
        }

        data->rstPin = of_get_named_gpio(node, "goodix,gpio_reset", 0);
        pr_info("rstPin GPIO is %d.  -----\n", data->rstPin);
        if (!gpio_is_valid(data->rstPin)) {
        pr_info("rstPin GPIO is invalid.\n");
        return -ENODEV;
        }

        pdev = of_find_device_by_node(node);
        printk(KERN_ERR "%s egistec find node enter \n", __func__);
        if (pdev) {
            data->pinctrl_gpios = devm_pinctrl_get(&pdev->dev);
            if (IS_ERR(data->pinctrl_gpios)) {
                ret = PTR_ERR(data->pinctrl_gpios);
                printk(KERN_ERR "%s can't find fingerprint pinctrl\n", __func__);
                return ret;
            }
            data->pstate_spi_6mA = pinctrl_lookup_state(data->pinctrl_gpios, "gf_spi_drive_6mA");
            if (IS_ERR(data->pstate_spi_6mA)) {
                ret = PTR_ERR(data->pstate_spi_6mA);
                printk(KERN_ERR "%s can't find fingerprint pinctrl pstate_spi_6mA\n", __func__);
                return ret;
            }
            data->pstate_spi_6mA_pull_low = pinctrl_lookup_state(data->pinctrl_gpios, "gf_spi_drive_6mA_pulllow");
            if (IS_ERR(data->pstate_spi_6mA_pull_low)) {
                ret = PTR_ERR(data->pstate_spi_6mA_pull_low);
                printk(KERN_ERR "%s can't find fingerprint pinctrl pstate_spi_6mA_pull_low\n", __func__);
                return ret;
            }
            data->pstate_default = pinctrl_lookup_state(data->pinctrl_gpios, "default");
            if (IS_ERR(data->pstate_default)) {
                ret = PTR_ERR(data->pstate_default);
                printk(KERN_ERR "%s can't find fingerprint pinctrl pstate_default\n", __func__);
                return ret;
            }
            pinctrl_select_state(data->pinctrl_gpios, data->pstate_default);
            printk(KERN_ERR "%s, get pinctrl success!\n", __func__);
        } else {
            printk(KERN_ERR "%s platform device is null\n", __func__);
        }
    } else {
        printk(KERN_ERR "%s device node is null\n", __func__);
    }
#endif
    DEBUG_PRINT("%s is successful\n", __func__);
    return 0;
}
#endif
const struct file_operations egistec_fops = {
    .owner = THIS_MODULE,
    .write = egistec_write,
    .read = egistec_read,
    .unlocked_ioctl = egistec_ioctl,
    .compat_ioctl = egistec_compat_ioctl,
    .open = egistec_open,
    .release = egistec_release,
    .llseek = no_llseek,
    .poll = fps_interrupt_poll
};

/*-------------------------------------------------------------------------*/
struct class *egistec_class;
/*-------------------------------------------------------------------------*/
int egistec_probe(struct platform_device *pdev);
int egistec_remove(struct platform_device *pdev);
struct et713_data_t{
    struct spi_device      *spi;
    struct class           *class;
    struct device          *device;
    dev_t                  devno;
    u8                     *huge_buffer;
    size_t                 huge_buffer_size;
    struct input_dev       *input_dev;
};

/* -------------------------------------------------------------------- */

int et713_spi_probe(struct spi_device *spi)
{
    int error = 0;
    struct et713_data_t *et713 = NULL;
    DEBUG_PRINT("EGISTEC %s enter\n", __func__);
    et713 = kzalloc(sizeof(struct et713_data_t), GFP_KERNEL);
    if (!et713) {
        return -ENOMEM;
    }
    printk(KERN_INFO"%s\n", __func__);
    spi_set_drvdata(spi, et713);
    g_data->spi = spi;
    DEBUG_PRINT("EGISTEC %s is successful\n", __func__);
    return error;
}
int et713_spi_remove(struct spi_device *spi)
{
    struct et713_data_t *et713 = spi_get_drvdata(spi);
    spi_clk_enable(0);
    DEBUG_PRINT("%s\n", __func__);
    kfree(et713);
    return 0;
}

struct spi_driver spi_driver = {
    .driver = {
        .name = "et713",
        .owner = THIS_MODULE,
        .of_match_table = et713_spi_of_match,
        .bus = &spi_bus_type,
    },
    .probe = et713_spi_probe,
    .remove = et713_spi_remove
};
struct platform_driver egistec_driver = {
    .driver = {
        .name = "et713",
        .owner = THIS_MODULE,
        .of_match_table = egistec_match_table,
    },
    .probe = egistec_probe,
    .remove = egistec_remove,
};

int egistec_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct egistec_data *egistec = dev_get_drvdata(dev);
    DEBUG_PRINT("%s(#%d)\n", __func__, __LINE__);
    free_irq(gpio_irq, NULL);
    wake_lock_destroy(&et713_wake_lock);
    request_irq_done = 0;
    kfree(egistec);
    return 0;
}
int egistec_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct egistec_data *egistec;
    int status = 0;
    unsigned long minor;
//    int ret;
//    struct platform_device *pdev_t;
//    struct device_node *node = NULL;

    DEBUG_PRINT("%s initial\n", __func__);
    BUILD_BUG_ON(N_SPI_MINORS > 256);
    status = register_chrdev(ET713_MAJOR, "et713", &egistec_fops);
    if (status < 0) {
            pr_err("%s register_chrdev error.\n", __func__);
            return status;
    }
    egistec_class = class_create(THIS_MODULE, "et713");
    if (IS_ERR(egistec_class)) {
        pr_err("%s class_create error.\n", __func__);
        unregister_chrdev(ET713_MAJOR, egistec_driver.driver.name);
        return PTR_ERR(egistec_class);
    }
    /* Allocate driver data */
    egistec = kzalloc(sizeof(struct egistec_data), GFP_KERNEL);
    if (egistec== NULL) {
        pr_err("%s - Failed to kzalloc\n", __func__);
        return -ENOMEM;
    }
    /* device tree call */
    if (pdev->dev.of_node) {
        status = egistec_parse_dt(&pdev->dev, egistec);
    }
    /* Initialize the driver data */
    egistec->pd = pdev;
    g_data = egistec;
    wake_lock_init(&et713_wake_lock, WAKE_LOCK_SUSPEND, "et713_wake_lock");
    pr_err("egistec_module_probe\n");
    spin_lock_init(&egistec->spi_lock);
    mutex_init(&egistec->buf_lock);
    mutex_init(&device_list_lock);

    INIT_LIST_HEAD(&egistec->device_entry);

    /* platform init */
    status = egistec_platformInit(egistec);
    if (status != 0) {
        pr_err("%s platforminit failed \n", __func__);
        goto egistec_probe_platformInit_failed;
    }

    egistec_spi_pin(egistec, 1);

    fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

    /*
     * If we can allocate a minor number, hook up this device.
     * Reusing minors is fine so long as udev or mdev is working.
     */
    mutex_lock(&device_list_lock);
    minor = find_first_zero_bit(minors, N_SPI_MINORS);
    if (minor < N_SPI_MINORS) {
        struct device *fdev;
        egistec->devt = MKDEV(ET713_MAJOR, minor);
        fdev = device_create(egistec_class, &pdev->dev, egistec->devt,
                    egistec, "esfp0");
        status = IS_ERR(fdev) ? PTR_ERR(fdev) : 0;
    } else {
        dev_dbg(&pdev->dev, "no minor number available!\n");
        status = -ENODEV;
    }
    if (status == 0) {
        set_bit(minor, minors);
        list_add(&egistec->device_entry, &device_list);
    }

    mutex_unlock(&device_list_lock);

    if (status == 0) {
        dev_set_drvdata(dev, egistec);
    }
    else {
        goto egistec_probe_failed;
    }
    fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

    egistec->notifier = egis_noti_block;
    status = fb_register_client(&egistec->notifier);
    if (status == -1) {
        goto egistec_probe_failed;
    }
    DEBUG_PRINT("%s : initialize success %d\n", __func__, status);
    request_irq_done = 0;
    return status;
egistec_probe_failed:
    device_destroy(egistec_class, egistec->devt);
    class_destroy(egistec_class);
egistec_probe_platformInit_failed:
    kfree(egistec);
    DEBUG_PRINT("%s is failed\n", __func__);
    return status;
}

void delete_device_node(void)
{
    DEBUG_PRINT("%s\n", __func__);
    device_destroy(egistec_class, g_data->devt);
    DEBUG_PRINT("device_destroy \n");
    list_del(&g_data->device_entry);
    DEBUG_PRINT("list_del \n");
    class_destroy(egistec_class);
    DEBUG_PRINT("class_destroy\n");
    unregister_chrdev(ET713_MAJOR, egistec_driver.driver.name);
    DEBUG_PRINT("unregister_chrdev\n");
    g_data = NULL;
}

int __init egis713_init(void)
{
    int status = 0;

    if (FP_EGIS_OPTICAL_ET713 != get_fpsensor_type()) {
        pr_err("%s, found not egis sensor\n", __func__);
        status = -EINVAL;
        return status;
    }

    printk(KERN_INFO "%s   zq 001 ! \n", __func__);
    status = platform_driver_register(&egistec_driver);
    if (status < 0) {
        pr_err("register spi driver fail%s\n", __func__);
        return -EINVAL;
    }
    if (spi_register_driver(&spi_driver)) {
        printk(KERN_ERR "register spi driver fail%s\n", __func__);
        return -EINVAL;
    }
    egis_netlink_init();
    /*Register for receiving tp touch event.
     * Must register after get_fpsensor_type filtration as only one handler can be registered.
     */
    opticalfp_irq_handler_register(egis_opticalfp_irq_handler);
    DEBUG_PRINT("EGISTEC spi_register_driver init OK! \n");
    return status;
}
void __exit egis713_exit(void)
{
    egis_netlink_exit();
    platform_driver_unregister(&egistec_driver);
    spi_unregister_driver(&spi_driver);
}
late_initcall(egis713_init);
module_exit(egis713_exit);

MODULE_AUTHOR("ZQ Chen, <zq.chen@egistec.com>");
MODULE_DESCRIPTION("Platform driver for ET713");
MODULE_LICENSE("GPL");
