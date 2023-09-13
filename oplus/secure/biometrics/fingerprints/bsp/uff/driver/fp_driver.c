// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt


#if defined(QCOM_PLATFORM)
//#include <asm/uaccess.h>
#include <linux/clk.h>
#include <linux/compat.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/irq.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/version.h>
#include <net/netlink.h>
#include <net/sock.h>
#else
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/time.h>
#include <linux/types.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include <linux/version.h>

#if defined(CONFIG_DRM_MEDIATEK_V2)
#include "mtk_disp_notify.h"
#endif

#endif

#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
#if IS_ENABLED(CONFIG_TOUCHPANEL_NOTIFY)
#include "touchpanel_event_notify.h"
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/uaccess.h>
#endif

#include <linux/input.h>
#include "include/oplus_fp_common.h"
#include "include/wakelock.h"
#include "fp_driver.h"
#include "include/fingerprint_event.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
#define FB_EARLY_EVENT_BLANK 0x10
#endif

#define WAKELOCK_HOLD_IRQ_TIME 500 /* in ms */
#define WAKELOCK_HOLD_CMD_TIME 1000 /* in ms */

#define OPLUS_FP_DEVICE_NAME "oplus,fp_spi"
#define FP_DEV_NAME "fingerprint_dev"

#define CHRD_DRIVER_NAME "fp_chrd_driver"
#define CLASS_NAME "oplus_fp"
#define FP_INPUT_NAME "oplus_fp_input"
#define N_SPI_MINORS 32 /* ... up to 256 */

struct fp_underscreen_info fp_touchinfo;
static unsigned int        lasttouchmode = 0;
static int                 SPIDEV_MAJOR;

typedef enum {
    FINGERPRINT_PROBE_OK,
    FINGERPRINT_PROBE_FAIL,
} fp_probe_statue_t;

fp_probe_statue_t g_fp_probe_statue = FINGERPRINT_PROBE_FAIL;

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static struct wake_lock fp_wakelock;
static struct wake_lock fp_cmd_wakelock;
struct fp_dev           fp_dev_data;

#define MAX_MSGSIZE 32

static int   pid            = -1;

struct fp_key_map fp_key_maps[] = {
    {EV_KEY, FP_KEY_INPUT_HOME},
    {EV_KEY, FP_KEY_INPUT_POWER},
    {EV_KEY, FP_KEY_INPUT_CAMERA},
};

int opticalfp_irq_handler_uff(struct fp_underscreen_info *tp_info);

#if (IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER))
static int fp_check_panel_dt(struct fp_dev *fp_dev);
static void fp_panel_notifier_callback(enum panel_event_notifier_tag tag, struct panel_event_notification *notification, void *client_data);
#endif

static int fp_panel_event_notifier_register(struct fp_dev *fp_dev)
{
#if (IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER))
    int status = 0;
    void          *cookie = NULL;
    unsigned int   check_panel_retry = 0;

    if (fp_dev->is_panel_registered) {
        pr_info("%s, panel is registered.\n", __func__);
        return 0;
    }

    fp_dev->active_panel = NULL;
    fp_dev->notifier_cookie = NULL;
    while (fp_check_panel_dt(fp_dev) < 0) {
        /* retry in 20sec, then break */
        if (check_panel_retry >= 10) {
            pr_err("%s, NO avalibel display panel and out retry!!!\n", __func__);
            status = -1;
            break;
        }
        check_panel_retry++;
        /* retry per 200ms */
        msleep(200);
    }
    pr_err("%s, check_panel retry = %u\n", __func__, check_panel_retry);

    if (fp_dev->active_panel) {
        cookie = panel_event_notifier_register(
            PANEL_EVENT_NOTIFICATION_PRIMARY,
            PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_ONSCREENFINGERPRINT,
            fp_dev->active_panel, &fp_panel_notifier_callback,
            fp_dev);

        if (IS_ERR(cookie)) {
            pr_err("%s panel_event_notifier_register err = %d!\n", __func__, PTR_ERR(cookie));
            status = -1;
        }
        fp_dev->notifier_cookie = cookie;
        pr_err("%s notifier_cookie = %d!\n", __func__, cookie);
    }
    if (0 == status) {
        fp_dev->is_panel_registered = true;
    }
    return status;
#else
    pr_err("%s, implement empty.\n", __func__);
    return 0;
#endif
}

static void fp_kernel_key_input(struct fp_dev *fp_dev, struct fp_key *fp_key) {
    uint32_t key_input = 0;
    if (FP_KEY_HOME == fp_key->key) {
        key_input = FP_KEY_INPUT_HOME;
    } else if (FP_KEY_POWER == fp_key->key) {
        key_input = FP_KEY_INPUT_POWER;
    } else if (FP_KEY_CAMERA == fp_key->key) {
        key_input = FP_KEY_INPUT_CAMERA;
    } else {
        /* add special key define */
        key_input = fp_key->key;
    }
    pr_info("%s: received key event[%d], key=%d, value=%d\n", __func__, key_input, fp_key->key,
        fp_key->value);

    if ((FP_KEY_POWER == fp_key->key || FP_KEY_CAMERA == fp_key->key) && (fp_key->value == 1)) {
        input_report_key(fp_dev->input, key_input, 1);
        input_sync(fp_dev->input);
        input_report_key(fp_dev->input, key_input, 0);
        input_sync(fp_dev->input);
    }

    if (FP_KEY_HOME == fp_key->key) {
        input_report_key(fp_dev->input, key_input, fp_key->value);
        input_sync(fp_dev->input);
    }
}

void fp_nl_data_ready(struct sk_buff *__skb) {
    struct sk_buff * skb;
    struct nlmsghdr *nlh;
    char             str[100];
    skb = skb_get(__skb);
    if (skb->len >= NLMSG_SPACE(0)) {
        nlh = nlmsg_hdr(skb);

        if (nlh != NULL) {
            memcpy(str, NLMSG_DATA(nlh), sizeof(str));
            pid = nlh->nlmsg_pid;
        }

        kfree_skb(skb);
    }
}


static void fp_spi_clk_enable(struct fp_dev *fp_dev) {
    fp_spi_enable_master_clk(fp_dev->pdev);
    pr_info("%s, mt_spi_enable_master_clk\n", __func__);
}

static void fp_spi_clk_disable(struct fp_dev *fp_dev) {
    fp_spi_disable_master_clk(fp_dev->pdev);
    pr_info("%s, mt_spi_disable_master_clk\n", __func__);
}

static void fp_enable_irq(struct fp_dev *fp_dev) {
    if (fp_dev->irq_enabled) {
        pr_warn("IRQ has been enabled.\n");
    } else {
        enable_irq(fp_dev->irq);
        fp_dev->irq_enabled = 1;
    }
}

static void fp_disable_irq(struct fp_dev *fp_dev) {
    if (fp_dev->irq_enabled) {
        fp_dev->irq_enabled = 0;
        disable_irq(fp_dev->irq);
    } else {
        pr_warn("IRQ has been disabled.\n");
    }
}

static int fp_read_irq_value(struct fp_dev *fp_dev) {
    int irq_value = 0;
    if (fp_dev->irq_gpio < 0) {
        pr_warn("err, irq_gpio not init.\n");
        return -1;
    }

    irq_value = gpio_get_value(fp_dev->irq_gpio);
    pr_info("%s, irq_value = %d\n", __func__, irq_value);
    return irq_value;
}

static irqreturn_t fp_irq_handler(int irq, void *handle) {
    char msg = NETLINK_EVENT_IRQ;
    wake_lock_timeout(&fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_IRQ_TIME));
    send_fingerprint_msg(E_FP_SENSOR, msg, NULL, 0);
    return IRQ_HANDLED;
}

static int irq_setup(struct fp_dev *fp_dev) {
    int status;

    fp_dev->irq = fp_irq_num(fp_dev);
    status      = request_threaded_irq(
        fp_dev->irq, NULL, fp_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT, "oplusfp", fp_dev);

    if (status) {
        pr_err("failed to request IRQ:%d\n", fp_dev->irq);
        return status;
    }
    enable_irq_wake(fp_dev->irq);
    fp_dev->irq_enabled = 1;

    return status;
}

static void irq_cleanup(struct fp_dev *fp_dev) {
    fp_dev->irq_enabled = 0;
    disable_irq(fp_dev->irq);
    disable_irq_wake(fp_dev->irq);
    free_irq(fp_dev->irq, fp_dev);  // need modify
}

static void fp_auto_send_touchdown()
{
    struct fp_underscreen_info tp_info = {0};
    tp_info.touch_state = 1;
    opticalfp_irq_handler_uff(&tp_info);
}

static void fp_auto_send_touchup()
{
    struct fp_underscreen_info tp_info = {0};
    tp_info.touch_state = 0;
    opticalfp_irq_handler_uff(&tp_info);
}

static long fp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct fp_dev *fp_dev        = &fp_dev_data;
    int            retval        = 0;
    struct fp_key  fp_key;
    int irq_value = 0;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_OLC)
    struct fp_exception_info fp_exception_info;
#endif

    if (_IOC_TYPE(cmd) != FP_IOC_MAGIC) {
        return -ENODEV;
    }

    if (_IOC_DIR(cmd) & _IOC_READ) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
        retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
#else
        retval = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
#endif
    } else if (_IOC_DIR(cmd) & _IOC_WRITE) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
        retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
#else
        retval = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
#endif
    }
    if (retval) {
        return -EFAULT;
    }

    if (fp_dev->device_available == 0) {
        if ((cmd == FP_IOC_ENABLE_POWER) || (cmd == FP_IOC_DISABLE_POWER) ||
            (cmd == FP_IOC_POWER_RESET)) {
            pr_info("power cmd\n");
        } else {
            pr_info("Sensor is power off currently. \n");
        }
    }

    switch (cmd) {
        case FP_IOC_INIT:
            // pr_info("%s FP_IOC_INIT\n", __func__);
            // if (copy_to_user((void __user *)arg, (void *)&netlink_route, sizeof(u8))) {
            //     retval = -EFAULT;
            //     break;
            // }
            break;
        case FP_IOC_EXIT:
            pr_info("%s FP_IOC_EXIT\n", __func__);
            break;
        case FP_IOC_DISABLE_IRQ:
            pr_info("%s FP_IOC_DISABLE_IRQ\n", __func__);
            fp_disable_irq(fp_dev);
            break;
        case FP_IOC_ENABLE_IRQ:
            pr_info("%s FP_IOC_ENABLE_IRQ\n", __func__);
            fp_enable_irq(fp_dev);
            break;
        case FP_IOC_RESET:
            pr_info("%s FP_IOC_RESET. \n", __func__);
            fp_hw_reset(fp_dev, 10);
            break;
        case FP_IOC_POWER_RESET:
            pr_info("%s FP_IOC_POWER_RESET. \n", __func__);
            fp_power_reset(fp_dev);
            fp_dev->device_available = 1;
            break;
        case FP_IOC_INPUT_KEY_EVENT:
            if (copy_from_user(&fp_key, (struct fp_key *)arg, sizeof(struct fp_key))) {
                pr_info("Failed to copy input key event from user to kernel\n");
                retval = -EFAULT;
                break;
            }

            fp_kernel_key_input(fp_dev, &fp_key);
            break;
        case FP_IOC_ENABLE_SPI_CLK:
            fp_spi_clk_enable(fp_dev);
            pr_info("%s FP_IOC_ENABLE_SPI_CLK\n", __func__);
            break;
        case FP_IOC_DISABLE_SPI_CLK:
            fp_spi_clk_disable(fp_dev);
            pr_info("%s FP_IOC_DISABLE_SPI_CLK\n", __func__);
            break;
        case FP_IOC_ENABLE_POWER:
            pr_info("%s FP_IOC_ENABLE_POWER\n", __func__);
            if (fp_dev->device_available == 1) {
                pr_info("Sensor has already powered-on.\n");
            } else {
                fp_power_on(fp_dev);
            }
            fp_dev->device_available = 1;
            break;
        case FP_IOC_DISABLE_POWER:
            pr_info("%s FP_IOC_DISABLE_POWER\n", __func__);
            if (fp_dev->device_available == 0) {
                pr_info("Sensor has already powered-off.\n");
            } else {
                fp_power_off(fp_dev);
            }
            fp_dev->device_available = 0;
            break;
        case FP_IOC_ENTER_SLEEP_MODE:
            pr_info("%s FP_IOC_ENTER_SLEEP_MODE\n", __func__);
            break;
        case FP_IOC_GET_FW_INFO:
            pr_info("%s FP_IOC_GET_FW_INFO\n", __func__);
            break;

        case FP_IOC_REMOVE:
            irq_cleanup(fp_dev);
            fp_cleanup_device(fp_dev);
            pr_info("%s FP_IOC_REMOVE\n", __func__);
            send_fingerprint_msg(E_FP_HAL, 0, NULL, 0);
            break;
        case FP_IOC_WAKELOCK_TIMEOUT_ENABLE:
            pr_info("%s FP_IOC_WAKELOCK_TIMEOUT_ENABLE\n", __func__);
            wake_lock_timeout(&fp_cmd_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_CMD_TIME));
            break;
        case FP_IOC_WAKELOCK_TIMEOUT_DISABLE:
            pr_info("%s FP_IOC_WAKELOCK_TIMEOUT_DISABLE\n", __func__);
            wake_unlock(&fp_cmd_wakelock);
            break;
        case FP_IOC_CLEAN_TOUCH_FLAG:
            lasttouchmode = 0;
            pr_info("%s FP_IOC_CLEAN_TOUCH_FLAG\n", __func__);
            break;
        case FP_IOC_AUTO_SEND_TOUCHDOWN:
            pr_info("%s FP_IOC_AUTO_SEND_TOUCHDOWN\n", __func__);
            fp_auto_send_touchdown();
            break;
        case FP_IOC_AUTO_SEND_TOUCHUP:
            pr_info("%s FP_IOC_AUTO_SEND_TOUCHUP\n", __func__);
            fp_auto_send_touchup();
            break;
        case FP_IOC_STOP_WAIT_INTERRUPT_EVENT:
            pr_info("%s GF_IOC_STOP_WAIT_INTERRUPT_EVENT\n", __func__);
            send_fingerprint_msg(E_FP_HAL, 0, NULL, 0);
            break;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_OLC)
        case FP_IOC_REPORT_OLC_EVENT:
            pr_info("%s FP_IOC_REPORT_OLC_EVENT\n", __func__);
            if (copy_from_user(&fp_exception_info, (fp_exception_info_t *)arg, sizeof(fp_exception_info_t))) {
                pr_info("Failed to copy exception_info event from user to kernel\n");
                retval = -EFAULT;
                break;
            }
            fp_olc_raise_exception(&fp_exception_info);
            break;
#endif
        case FP_IOC_RD_IRQ_VALUE:
            pr_info("%s FP_IOC_RD_IRQ_VALUE\n", __func__);
            irq_value = fp_read_irq_value(fp_dev);
            retval = __put_user(irq_value, (int32_t __user *)arg);
            break;
        default:
            pr_warn("unsupport cmd:0x%x\n", cmd);
            break;
    }

    return retval;
}

static int fp_open(struct inode *inode, struct file *filp) {
    struct fp_dev *fp_dev = &fp_dev_data;
    int            status = -ENXIO;
    /* reset previous msg incase of reinit in hal*/
    mutex_lock(&device_list_lock);

    list_for_each_entry(fp_dev, &device_list, device_entry) {
        if (fp_dev->devt == inode->i_rdev) {
            pr_info("Found\n");
            status = 0;
            break;
        }
    }

    if (status == 0) {
        fp_dev->users++;
        filp->private_data = fp_dev;
        nonseekable_open(inode, filp);
        pr_info("Succeed to open device. irq = %d\n", fp_dev->irq);
        if (fp_dev->users == 1) {
            status = fp_parse_dts(fp_dev);
            if (status) {
                goto err_parse_dt;
            }
            status = irq_setup(fp_dev);
            if (status) {
                goto err_irq;
            }
            status = fp_panel_event_notifier_register(fp_dev);
            if (status) {
                goto err_panel;
            }
        }
    } else {
        pr_info("No device for minor %d\n", iminor(inode));
    }
    status = init_fingerprint_msg();
    if (status) {
        goto err_msg;
    }
    pr_info("fingerprint open success\n");
    mutex_unlock(&device_list_lock);

    return status;
err_msg:
    pr_info("fifomsg fail\n");
    deinit_fingerprint_msg();
err_irq:
    fp_cleanup_device(fp_dev);
    fp_exception_report_drv(FP_SCENE_DRV_OPEN_FAIL);
err_parse_dt:
    mutex_unlock(&device_list_lock);
    pr_info("fingerprint open fail\n");
err_panel:
    pr_info("panel_register fail\n");
    return status;
}

static int fp_release(struct inode *inode, struct file *filp) {
    struct fp_dev *fp_dev = &fp_dev_data;
    int            status = 0;

    mutex_lock(&device_list_lock);
    fp_dev             = filp->private_data;
    filp->private_data = NULL;

    /*last close?? */
    fp_dev->users--;
    if (!fp_dev->users) {
        gpio_set_value(fp_dev->reset_gpio, 0);
        mdelay(3);
        fp_power_off(fp_dev);
        fp_dev->device_available = 0;
        irq_cleanup(fp_dev);
        fp_cleanup_device(fp_dev);
        /*power off the sensor*/
    }
    deinit_fingerprint_msg();
    mutex_unlock(&device_list_lock);
    return status;
}

ssize_t fp_read(struct file * f, char __user *buf, size_t count, loff_t *offset)
{
    struct fingerprint_message_t rcv_msg = {0};
    pr_info("gf_read enter");
    if (buf == NULL || f == NULL || count != sizeof(struct fingerprint_message_t)) {
        return 0;
    }
    pr_info("begin wait for driver event");
    if (wait_fp_event(NULL, 0, &rcv_msg)) {
        return -2;
    }
    if (copy_to_user(buf, &rcv_msg, count)) {
        return -EFAULT;
    }
    pr_info("end wait for driver event");
    return count;
}


static const struct file_operations fp_fops = {
    .owner = THIS_MODULE,
    /* REVISIT switch to aio primitives, so that userspace
     * gets more complete API coverage.  It'll simplify things
     * too, except for the locking.
     */
    .unlocked_ioctl = fp_ioctl,
    .open           = fp_open,
    .release        = fp_release,
    .read = fp_read,
};

#if (IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER))
static void fp_panel_notifier_callback(enum panel_event_notifier_tag tag, struct panel_event_notification *notification, void *client_data)
{
    if (!notification) {
        pr_err("%s display notification NULL!\n", __func__);
        return;
    }

    switch (notification->notif_type) {
    case DRM_PANEL_EVENT_ONSCREENFINGERPRINT_UI_READY:
        pr_err("[%s] UI ready\n", __func__);
        send_fingerprint_msg(E_FP_LCD, 1, NULL, 0);
        break;
    case DRM_PANEL_EVENT_ONSCREENFINGERPRINT_UI_DISAPPEAR:
        pr_err("[%s] UI disappear\n", __func__);
        send_fingerprint_msg(E_FP_LCD, 0, NULL, 0);
        break;
    default:
        break;
    }
}

static int fp_check_panel_dt(struct fp_dev *fp_dev)
{
    int index = 0;
    int count = 0;
    struct device_node *ref_node = NULL;
    struct device_node *node     = NULL;
    struct drm_panel *panel      = NULL;
    const char *display_dev_node = "oplus,dsi-display-dev";
    const char *panel_prop_nodes = "oplus,dsi-panel-primary";

    ref_node = of_find_node_by_name(NULL, display_dev_node);
    if (!ref_node) {
        pr_err("DTS node %s missing (fp).\n", display_dev_node);
        return 0;
    } else {
        pr_info("DTS node %s found (fp).\n", display_dev_node);
    }

    count = of_count_phandle_with_args(ref_node, panel_prop_nodes, NULL);
    if (count <= 0) {
        pr_err("%s, NOT config %s!\n", __func__, panel_prop_nodes);
        return 0;
    }

    for (index = 0; index < count; index++) {
        node  = of_parse_phandle(ref_node, panel_prop_nodes, index);
        panel = of_drm_find_panel(node);
        of_node_put(node);
        if (!IS_ERR(panel)) {
            pr_err("%s, active_panel = %p\n", __func__, panel);
            fp_dev->active_panel = panel;
            return 0;
        }
    }

    pr_err("%s, NO active panel!\n", __func__);
    return PTR_ERR(panel);
}
#endif /*CONFIG_DRM_PANEL_NOTIFY*/

static int oplus_fb_notifier_call(struct notifier_block *nb, unsigned long val, void *data) {
    struct fb_event *evdata = data;
    char             msg    = 0;

    pr_info("[%s] val = %lu", __func__, val);
    if (val == ONSCREENFINGERPRINT_EVENT) {
        uint8_t op_mode = 0x0;

#if defined(CONFIG_DRM_MEDIATEK_V2)
        (void)evdata;
        op_mode = *(int *)data;
#else
        op_mode = *(uint8_t *)evdata->data;
#endif

        switch (op_mode) {
            case 0:
                pr_info("[%s] UI disappear :%d\n", __func__, op_mode);
                msg = NETLINK_EVENT_UI_DISAPPEAR;
                break;
            case 1:
                pr_info("[%s] UI ready uiready:%d\n", __func__, op_mode);
                msg = NETLINK_EVENT_UI_READY;
                // fp_sendnlmsg(&msg);
                break;
            default:
                pr_info("[%s] Unknown ONSCREENFINGERPRINT_EVENT data \n", __func__);
                break;
        }

        send_fingerprint_msg(E_FP_LCD, (int)op_mode, NULL, 0);
    }

    return NOTIFY_OK;
}
#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
static int oplus_tp_notifier_call(struct notifier_block *nb, unsigned long val, void *data) {
    struct touchpanel_event* tp_event = (struct touchpanel_event*)data;
    char msg = 0;
    (void)nb;

    if (tp_event->touch_state == lasttouchmode) {
        return IRQ_HANDLED;
    }

    pr_info("recv tp event:%d\n", (int)val);
    if (val == EVENT_ACTION_FOR_FINGPRINT) {
        struct fp_underscreen_info fp_info = {0};
        struct fp_underscreen_info* tp_info = &fp_info;

        tp_info->touch_state = tp_event->touch_state;
        tp_info->x = tp_event->x;
        tp_info->y = tp_event->y;

        pr_info("tp_info->touch_state =%d, tp_info->x =%d, tp_info->y =%d,\n",
            tp_info->touch_state, tp_info->x, tp_info->y);

        wake_lock_timeout(&fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_IRQ_TIME));
        if (1 == tp_info->touch_state) {
            pr_info("%s touch down touchdown\n", __func__);
            msg = NETLINK_EVENT_TP_TOUCHDOWN;
            lasttouchmode = tp_info->touch_state;
            send_fingerprint_msg(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
        } else {
            pr_info("%s touch up touchup\n", __func__);
            msg = NETLINK_EVENT_TP_TOUCHUP;
            send_fingerprint_msg(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
            lasttouchmode = tp_info->touch_state;
        }
    }
    return NOTIFY_OK;
}
#endif
static struct notifier_block oplus_fp_notifier_block = {
    .notifier_call = oplus_fb_notifier_call,
};
#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
static struct notifier_block oplus_tp_notifier_block = {
    .notifier_call = oplus_tp_notifier_call,
};
#endif
int opticalfp_irq_handler_uff(struct fp_underscreen_info *tp_info) {
    char msg     = 0;
    fp_touchinfo = *tp_info;

    if (g_fp_probe_statue == FINGERPRINT_PROBE_FAIL) {
        return IRQ_HANDLED;
    }

    if (tp_info->touch_state == lasttouchmode) {
        return IRQ_HANDLED;
    }
    //return
    //add for debug
    pr_info("[%s] tp_info->touch_state =%d, tp_info->x =%d, tp_info->y =%d, \n", __func__, tp_info->touch_state, tp_info->x, tp_info->y);
    wake_lock_timeout(&fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_IRQ_TIME));
    if (1 == tp_info->touch_state) {
        pr_info("%s touch down \n", __func__);
        msg = NETLINK_EVENT_TP_TOUCHDOWN;
        lasttouchmode = tp_info->touch_state;
        send_fingerprint_msg(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
    } else {
        pr_info("%s touch up \n", __func__);
        msg = NETLINK_EVENT_TP_TOUCHUP;
        send_fingerprint_msg(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
        lasttouchmode = tp_info->touch_state;
    }

    return IRQ_HANDLED;
}
EXPORT_SYMBOL(opticalfp_irq_handler_uff);

static struct class *fp_class;

static int fp_probe(oplus_fp_device *pdev) {
    struct fp_dev *fp_dev = &fp_dev_data;
    int            status = -EINVAL;
    unsigned long  minor;
    int            count = 0;
    /* Initialize the driver data */
    INIT_LIST_HEAD(&fp_dev->device_entry);
    fp_dev->pdev = pdev;

    fp_dev->irq_gpio         = -EINVAL;
    fp_dev->reset_gpio       = -EINVAL;
    fp_dev->pwr_gpio         = -EINVAL;
    fp_dev->device_available = 0;
    fp_dev->fb_black         = 0;

    // do oplus_fp_common
    pr_info("enter to fp_probe");

    /* If we can allocate a minor number, hook up this device.
     * Reusing minors is fine so long as udev or mdev is working.
     */
    mutex_lock(&device_list_lock);
    minor = find_first_zero_bit(minors, N_SPI_MINORS);
    if (minor < N_SPI_MINORS) {
        struct device *dev;

        fp_dev->devt = MKDEV(SPIDEV_MAJOR, minor);
        dev    = device_create(fp_class, &fp_dev->pdev->dev, fp_dev->devt, fp_dev, FP_DEV_NAME);
        status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
    } else {
        dev_dbg(&fp_dev->pdev->dev, "no minor number available!\n");
        status = -ENODEV;
        mutex_unlock(&device_list_lock);
        goto error_hw;
    }

    if (status == 0) {
        set_bit(minor, minors);
        list_add(&fp_dev->device_entry, &device_list);
    } else {
        fp_dev->devt = 0;
    }
    mutex_unlock(&device_list_lock);

    if (status == 0) {
        /*input device subsystem */
        fp_dev->input = input_allocate_device();
        if (fp_dev->input == NULL) {
            pr_err("%s, failed to allocate input device\n", __func__);
            status = -ENOMEM;
            goto error_dev;
        }
        for (count = 0; count < ARRAY_SIZE(fp_key_maps); count++) {
            input_set_capability(fp_dev->input, fp_key_maps[count].type, fp_key_maps[count].code);
        }
        fp_dev->input->name = FP_INPUT_NAME;

        status = input_register_device(fp_dev->input);
        if (status) {
            pr_err("failed to register input device\n");
            goto error_input;
        }
    }

    fp_dev->notifier = oplus_fp_notifier_block;
#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
    fp_dev->tp_notifier = oplus_tp_notifier_block;
#endif

#if defined(CONFIG_DRM_MEDIATEK_V2)
    oplus_register_notifier_client("fingerprint", &fp_dev->notifier);
#elif defined(CONFIG_DRM_MSM) || defined(CONFIG_FB)
    status = oplus_register_notifier_client(&fp_dev->notifier);
#endif

#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
    status = touchpanel_event_register_notifier(&fp_dev->tp_notifier);
    pr_info("touchpanel_event_register_notifier ret:%d\n", status);
#endif
    if (status == -1) {
        return status;
    }

    wake_lock_init(&fp_wakelock, WAKE_LOCK_SUSPEND, "fp_wakelock");
    wake_lock_init(&fp_cmd_wakelock, WAKE_LOCK_SUSPEND, "fp_cmd_wakelock");
    g_fp_probe_statue = FINGERPRINT_PROBE_OK;

    pr_err("register oplus_fp end \n");
    return status;

error_input:
    if (fp_dev->input != NULL) {
        input_free_device(fp_dev->input);
    }
error_dev:
    if (fp_dev->devt != 0) {
        pr_info("Err: status = %d\n", status);
        mutex_lock(&device_list_lock);
        list_del(&fp_dev->device_entry);
        device_destroy(fp_class, fp_dev->devt);
        clear_bit(MINOR(fp_dev->devt), minors);
        mutex_unlock(&device_list_lock);
    }
error_hw:
    fp_dev->device_available = 0;

    return status;
}

static int fp_remove(oplus_fp_device *pdev) {
    struct fp_dev *fp_dev = &fp_dev_data;
    g_fp_probe_statue = FINGERPRINT_PROBE_FAIL;
    wake_lock_destroy(&fp_wakelock);
    wake_lock_destroy(&fp_cmd_wakelock);

    fb_unregister_client(&fp_dev->notifier);
#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
    touchpanel_event_unregister_notifier(&fp_dev->tp_notifier);
#endif
#if (IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER))
    if (NULL !=fp_dev->notifier_cookie && !IS_ERR(fp_dev->notifier_cookie)) {
        panel_event_notifier_unregister(fp_dev->notifier_cookie);
        fp_dev->is_panel_registered = false;
    }
#endif
    if (fp_dev->input) {
        input_unregister_device(fp_dev->input);
    }
    input_free_device(fp_dev->input);

    /* prevent new opens */
    mutex_lock(&device_list_lock);
    list_del(&fp_dev->device_entry);
    device_destroy(fp_class, fp_dev->devt);
    clear_bit(MINOR(fp_dev->devt), minors);
    mutex_unlock(&device_list_lock);

    return 0;
}

static struct of_device_id fp_match_table[] = {
    {.compatible = OPLUS_FP_DEVICE_NAME},
    {},
};

// oplus_fp_driver
static oplus_driver fp_driver = {
    .driver =
        {
            .name           = FP_DEV_NAME,
            .owner          = THIS_MODULE,
            .of_match_table = fp_match_table,
        },
    .probe  = fp_probe,
    .remove = fp_remove,
};

static int __init fp_init(void) {
    int status;
    /* Claim our 256 reserved device numbers.  Then register a class
     * that will key udev/mdev to add/remove /dev nodes.  Last, register
     * the driver which manages those device numbers.
     */

    BUILD_BUG_ON(N_SPI_MINORS > 256);
    status = register_chrdev(SPIDEV_MAJOR, CHRD_DRIVER_NAME, &fp_fops);
    if (status < 0) {
        pr_warn("Failed to register char device!\n");
        return status;
    }
    SPIDEV_MAJOR = status;
    fp_class     = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(fp_class)) {
        unregister_chrdev(SPIDEV_MAJOR, fp_driver.driver.name);
        pr_warn("Failed to create class.\n");
        return PTR_ERR(fp_class);
    }

    status = oplus_driver_register(&fp_driver);

    if (status < 0) {
        class_destroy(fp_class);
        unregister_chrdev(SPIDEV_MAJOR, fp_driver.driver.name);
        pr_warn("Failed to register SPI driver.\n");
        return status;
    }

    pr_info("status = 0x%x\n", status);
    return 0;
}
late_initcall(fp_init);

static void __exit fp_exit(void) {
    oplus_driver_unregister(&fp_driver);
    class_destroy(fp_class);
    unregister_chrdev(SPIDEV_MAJOR, fp_driver.driver.name);
}
module_exit(fp_exit);

#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
MODULE_SOFTDEP("pre:mtk_disp_notify");
#endif

#if defined(CONFIG_OPLUS_FEATURE_OLC)
MODULE_SOFTDEP("pre:oplus_log_core");
#endif

#if defined(CONFIG_FP_SUPPLY_MODE_LDO)
MODULE_SOFTDEP("pre:wl2868c");
#endif

MODULE_DESCRIPTION("oplus fingerprint common driver");
MODULE_LICENSE("GPL v2");
