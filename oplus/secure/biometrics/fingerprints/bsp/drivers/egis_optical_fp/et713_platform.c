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
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <soc/qcom/scm.h>

#ifdef CONFIG_BBK_FP_ID
#include "../fp_id.h"
#endif
#include <linux/pm_wakeup.h>
#include "../include/oplus_fp_common.h"
#include "et713.h"
//#include "navi_input.h"

#define EDGE_TRIGGER_FALLING 0x0
#define EDGE_TRIGGER_RAISING 0x1
#define LEVEL_TRIGGER_LOW 0x2
#define LEVEL_TRIGGER_HIGH 0x3
#define EGIS_NAVI_INPUT 0 /* 1:open ; 0:close */

#define pinctrl_action_error -1

static int egis_parse_dt(struct device *dev, struct egis_data *data);


struct wakeup_source wakeup_source_fp;



struct vreg_config {
	char *name;
	unsigned long vmin;
	unsigned long vmax;
	int ua_load;
};

static const struct vreg_config const vreg_conf[] = {
{
	"ldo5", 3300000UL, 3300000UL, 150000,
},
};

/*
 * FPS interrupt table
 */

struct interrupt_desc fps_ints = {0, 0, "BUT0", 0};
unsigned int bufsiz = 4096;
int gpio_irq;
int request_irq_done;
/* int t_mode = 255; */

#define EDGE_TRIGGER_FALLING 0x0
#define EDGE_TRIGGER_RISING 0x1
#define LEVEL_TRIGGER_LOW 0x2
#define LEVEL_TRIGGER_HIGH 0x3

struct ioctl_cmd {
	int int_mode;
	int detect_period;
	int detect_threshold;
};


static int vreg_setup(struct egis_data *egis, const char *name, bool enable)
{
	int i, rc;
	int found = 0;
	struct regulator *vreg;
	struct device *dev = &(egis->pd->dev);

	DEBUG_PRINT("[egis] %s\n", __func__);
	for (i = 0; i < 3; i++) {
		const char *n = vreg_conf[i].name;

		DEBUG_PRINT("[egis] vreg_conf[%d].name = %s\n", i, name);

		if (!strncmp(n, name, strlen(n))) {
			DEBUG_PRINT("[egis] Regulator %s is found\n", name);
			found = 1;
			break;
		}
	}

	if (found == 0) {
		DEBUG_PRINT("[egis] Regulator %s is not found\n", name);
		return -EINVAL;
	}

	vreg = egis->vreg[i];
	if (enable) {
		if (!vreg) {
			vreg = regulator_get(dev, name);
			if (IS_ERR(vreg)) {
				DEBUG_PRINT("[egis] Unable to get %s\n", name);
				return PTR_ERR(vreg);
			}
		}

		if (regulator_count_voltages(vreg) > 0) {
			rc = regulator_set_voltage(vreg, vreg_conf[i].vmin, vreg_conf[i].vmax);
			if (rc)
				DEBUG_PRINT("[egis] Unable to set voltage on %s, %d\n", name, rc);
		}

		rc = regulator_set_load(vreg, vreg_conf[i].ua_load);
		if (rc < 0)
			DEBUG_PRINT("[egis] Unable to set current on %s, %d\n", name, rc);
		rc = regulator_enable(vreg);
		if (rc) {
			DEBUG_PRINT("[egis] error enabling %s: %d\n", name, rc);
			regulator_put(vreg);
			vreg = NULL;
		}
		egis->vreg[i] = vreg;
	} else {
		if (vreg) {
			if (regulator_is_enabled(vreg)) {
				regulator_disable(vreg);
				DEBUG_PRINT("[egis] disabled %s\n", name);
			}
			regulator_put(vreg);
			egis->vreg[i] = NULL;
		}
		rc = 0;
	}
	return rc;
}


static struct egis_data *g_data;

DECLARE_BITMAP(minors, N_PD_MINORS);
LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

/* ------------------------------ Interrupt -----------------------------*/
/*
 * Interrupt description
 */

#define FP_INT_DETECTION_PERIOD 10
#define FP_DETECTION_THRESHOLD 10
/* struct interrupt_desc fps_ints; */
static DECLARE_WAIT_QUEUE_HEAD(interrupt_waitq);
/*
 *	FUNCTION NAME.
 *		interrupt_timer_routine
 *
 *	FUNCTIONAL DESCRIPTION.
 *		basic interrupt timer inital routine
 *
 *	ENTRY PARAMETERS.
 *		gpio - gpio address
 *
 *	EXIT PARAMETERS.
 *		Function Return
 */

void interrupt_timer_routine(unsigned long _data)
{
	struct interrupt_desc *bdata = (struct interrupt_desc *)_data;

	DEBUG_PRINT("[egis] FPS interrupt count = %d detect_threshold = %d\n", bdata->int_count, bdata->detect_threshold);
	if (bdata->int_count >= bdata->detect_threshold) {
		bdata->finger_on = 1;
		DEBUG_PRINT("[egis] FPS triggered!\n");
	} else
		DEBUG_PRINT("[egis] FPS not triggered!\n");
	bdata->int_count = 0;
	wake_up_interruptible(&interrupt_waitq);
}

static irqreturn_t fp_eint_func(int irq, void *dev_id)
{
	if (!fps_ints.int_count)
		mod_timer(&fps_ints.timer, jiffies + msecs_to_jiffies(fps_ints.detect_period));

	fps_ints.int_count++;
	__pm_wakeup_event(&wakeup_source_fp, msecs_to_jiffies(1500));
	return IRQ_HANDLED;
}

unsigned int lasttouchmode = 0;
static int egis_opticalfp_irq_handler(struct fp_underscreen_info *tp_info)
{
    char msg = 0;
	printk(KERN_EMERG "guq_warning  egis optical  egis handler start\n");
    g_data->fp_tpinfo = *tp_info;
    if(tp_info->touch_state == lasttouchmode){
        return IRQ_HANDLED;
    }
	__pm_wakeup_event(&wakeup_source_fp, msecs_to_jiffies(500));
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
    struct egis_data *e_data;
    struct msm_drm_notifier *evdata = data;
    unsigned int blank;
    char msg = 0;
    int retval = 0;

    e_data = container_of(nb, struct egis_data, notifier);

    if (val == MSM_DRM_ONSCREENFINGERPRINT_EVENT) {
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
			DEBUG_PRINT("[egis] Unknown MSM_DRM_ONSCREENFINGERPRINT_EVENT!\n");
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


static irqreturn_t fp_eint_func_ll(int irq, void *dev_id)
{
	disable_irq_nosync(gpio_irq);
	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
	DEBUG_PRINT("[egis] %s\n", __func__);
	fps_ints.finger_on = 1;
	wake_up_interruptible(&interrupt_waitq);
	__pm_wakeup_event(&wakeup_source_fp, msecs_to_jiffies(1500));
	return IRQ_RETVAL(IRQ_HANDLED);
}

/*
 *	FUNCTION NAME.
 *		Interrupt_Init
 *
 *	FUNCTIONAL DESCRIPTION.
 *		button initial routine
 *
 *	ENTRY PARAMETERS.
 *		int_mode - determine trigger mode
 *			EDGE_TRIGGER_FALLING    0x0
 *			EDGE_TRIGGER_RAISING    0x1
 *			LEVEL_TRIGGER_LOW        0x2
 *			LEVEL_TRIGGER_HIGH       0x3
 *
 *	EXIT PARAMETERS.
 *		Function Return int
 */

int Interrupt_Init(struct egis_data *egis, int int_mode, int detect_period, int detect_threshold)
{
	int err = 0;
	int status = 0;
	if (egis->irqPin <= 0) {
		DEBUG_PRINT(" %s irqPin not avaliable\n", __func__);
		return -1;
	}
	DEBUG_PRINT("[egis] %s: mode = %d period = %d threshold = %d\n", __func__, int_mode, detect_period, detect_threshold);
	DEBUG_PRINT("[egis] %s: request_irq_done = %d gpio_irq = %d  pin = %d\n", __func__, request_irq_done, gpio_irq, egis->irqPin);
	fps_ints.detect_period = detect_period;
	fps_ints.detect_threshold = detect_threshold;
	fps_ints.int_count = 0;
	fps_ints.finger_on = 0;
	if (request_irq_done == 0) {
		gpio_irq = gpio_to_irq(egis->irqPin);
		if (gpio_irq < 0) {
			DEBUG_PRINT("[egis] %s gpio_to_irq failed\n", __func__);
			status = gpio_irq;
			goto done;
		}

		DEBUG_PRINT("[egis] %s: flag current: %d disable: %d enable: %d\n",  __func__, fps_ints.drdy_irq_flag, DRDY_IRQ_DISABLE, DRDY_IRQ_ENABLE);
		if (int_mode == EDGE_TRIGGER_RISING) {
			DEBUG_PRINT("[egis] %s EDGE_TRIGGER_RISING\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func, IRQ_TYPE_EDGE_RISING, "fp_detect-eint", egis);
			if (err)
				DEBUG_PRINT("[egis] request_irq failed==========%s, %d\n", __func__, __LINE__);
		} else if (int_mode == EDGE_TRIGGER_FALLING) {
			DEBUG_PRINT("[egis] %s EDGE_TRIGGER_FALLING\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func, IRQ_TYPE_EDGE_FALLING, "fp_detect-eint", egis);
			if (err)
				DEBUG_PRINT("[egis] request_irq failed==========%s, %d\n", __func__, __LINE__);
		} else if (int_mode == LEVEL_TRIGGER_LOW) {
			DEBUG_PRINT("[egis] %s LEVEL_TRIGGER_LOW\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func_ll, IRQ_TYPE_LEVEL_LOW, "fp_detect-eint", egis);
			if (err)
				DEBUG_PRINT("[egis] request_irq failed==========%s, %d\n", __func__, __LINE__);
		} else if (int_mode == LEVEL_TRIGGER_HIGH) {
			DEBUG_PRINT("[egis] %s LEVEL_TRIGGER_HIGH\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func_ll, IRQ_TYPE_LEVEL_HIGH, "fp_detect-eint", egis);
			if (err)
				DEBUG_PRINT("[egis] request_irq failed==========%s, %d\n", __func__, __LINE__);
		}
		DEBUG_PRINT("[egis] %s: gpio_to_irq return: %d\n", __func__, gpio_irq);
		DEBUG_PRINT("[egis] %s: request_irq return: %d\n", __func__, err);
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
 *	FUNCTION NAME.
 *		Interrupt_Free
 *
 *	FUNCTIONAL DESCRIPTION.
 *		free all interrupt resource
 *
 *	EXIT PARAMETERS.
 *		Function Return int
 */

int Interrupt_Free(struct egis_data *egis)
{
	DEBUG_PRINT("[egis] %s\n", __func__);
	fps_ints.finger_on = 0;

	if (fps_ints.drdy_irq_flag == DRDY_IRQ_ENABLE) {
		DEBUG_PRINT("[egis] %s (DISABLE IRQ)\n", __func__);
		disable_irq_nosync(gpio_irq);
		del_timer_sync(&fps_ints.timer);
		fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
	}
	return 0;
}

/*
 *	FUNCTION NAME.
 *		fps_interrupt_re d
 *
 *	FUNCTIONAL DESCRIPTION.
 *		FPS interrupt read status
 *
 *	ENTRY PARAMETERS.
 *		wait poll table structure
 *
 *	EXIT PARAMETERS.
 *		Function Return int
 */

unsigned int fps_interrupt_poll(
struct file *file,
struct poll_table_struct *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &interrupt_waitq, wait);
	if (fps_ints.finger_on)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

void fps_interrupt_abort(void)
{
	DEBUG_PRINT("[egis] %s", __func__);
	fps_ints.finger_on = 0;
	wake_up_interruptible(&interrupt_waitq);
}



/*-------------------------------------------------------------------------*/

static void egis_reset(struct egis_data *egis)
{
	DEBUG_PRINT("[egis] %s\n", __func__);
		gpio_set_value(egis->rstPin, 0);
	msleep(30);
		gpio_set_value(egis->rstPin, 1);
	msleep(20);
}

static void egis_power_onoff(struct egis_data *egis, int power_onoff)
{
	DEBUG_PRINT("[egis] %s   power_onoff = %d \n", __func__, power_onoff);
	if (power_onoff) {
		vreg_setup(egis, "ldo5", true);
	} else {
		vreg_setup(egis, "ldo5", false);
	}
	msleep(30);
}
static void egis_reset_onoff(struct egis_data *egis, int reset_onoff)
{
	DEBUG_PRINT("[egis] %s   reset_onoff = %d \n", __func__, reset_onoff);
	if (reset_onoff) {
		gpio_set_value(egis->rstPin, 1);
	} else {
		gpio_set_value(egis->rstPin, 0);
	}
}

static ssize_t egis_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	/*if needed*/
	return 0;
}

static ssize_t egis_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	/*if needed*/
	return 0;
}

static long egis_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	struct egis_data *egis;
	struct ioctl_cmd data;

	DEBUG_PRINT("[egis] %s", __func__);
	memset(&data, 0, sizeof(data));
	egis = filp->private_data;

	switch (cmd) {
	case INT_TRIGGER_INIT:
		if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
			retval = -EFAULT;
			break;
		}
		retval = Interrupt_Init(egis, data.int_mode, data.detect_period, data.detect_threshold);
		DEBUG_PRINT("[egis] %s: fp_ioctl trigger init = %x\n", __func__, retval);
		break;
	case FP_SENSOR_RESET:
		DEBUG_PRINT("[egis] %s: FP_SENSOR_RESET\n", __func__);
		egis_reset(egis);
		break;
	case FP_POWER_ONOFF:
		if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
			retval = -EFAULT;
			break;
		}
		egis_power_onoff(egis, data.int_mode);  // Use data.int_mode as power setting. 1 = on, 0 = off.
		DEBUG_PRINT("[egis] %s: egis_power_onoff = %d\n", __func__, data.int_mode);
		break;
	case FP_RESET_SET:
		if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
			retval = -EFAULT;
			break;
		}
		egis_reset_onoff(egis, data.int_mode);  // Use data.int_mode as power setting. 1 = on, 0 = off.
		DEBUG_PRINT("[egis] %s: egis_reset_onoff = %d\n", __func__, data.int_mode);
		break;		
	case INT_TRIGGER_CLOSE:
		retval = Interrupt_Free(egis);
		DEBUG_PRINT("[egis] %s: INT_TRIGGER_CLOSE = %x\n", __func__, retval);
		break;
	case INT_TRIGGER_ABORT:
		DEBUG_PRINT("[egis] %s: INT_TRIGGER_ABORT\n", __func__);
		fps_interrupt_abort();
		break;
	default:
		retval = -ENOTTY;
		break;
	}
	return retval;
}

#ifdef CONFIG_COMPAT
static long egis_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	DEBUG_PRINT("[egis] %s", __func__);
	return egis_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define egis_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int egis_open(struct inode *inode, struct file *filp)
{
	struct egis_data *egis;
	int status = -ENXIO;

	DEBUG_PRINT("[egis] %s\n", __func__);
	mutex_lock(&device_list_lock);

	list_for_each_entry(egis, &device_list, device_entry) {
		if (egis->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}

	if (status == 0) {
		if (egis->buffer == NULL) {
			egis->buffer = kmalloc(bufsiz, GFP_KERNEL);
			if (egis->buffer == NULL)
				status = -ENOMEM;
		}
		if (status == 0) {
			egis->users++;
			filp->private_data = egis;
			nonseekable_open(inode, filp);
		}
	} else
		DEBUG_PRINT("[egis] %s nothing for minor %d\n", __func__, iminor(inode));
	mutex_unlock(&device_list_lock);
	return status;
}

static int egis_release(struct inode *inode, struct file *filp)
{
	struct egis_data *egis;

	DEBUG_PRINT("[egis] %s\n", __func__);
	mutex_lock(&device_list_lock);
	egis = filp->private_data;
	filp->private_data = NULL;

	egis->users--;
	if (egis->users == 0) {
		int dofree;

		kfree(egis->buffer);
		egis->buffer = NULL;

		/* after we unbound from the underlying device */
		spin_lock_irq(&egis->pd_lock);
		dofree = (egis->pd == NULL);
		spin_unlock_irq(&egis->pd_lock);

		if (dofree)
			kfree(egis);
	}
	mutex_unlock(&device_list_lock);
	return 0;
}

int egis_platformInit(struct egis_data *egis)
{
	int status = 0;

	DEBUG_PRINT("[egis] %s\n", __func__);




	if (egis != NULL) {
		/* Initial IRQ Pin*/
		status = gpio_request(egis->irqPin, "irq-gpio");
		if (status < 0) {
			DEBUG_PRINT("[egis] %s gpio_request egis_irq failed\n", __func__);
			goto egis_platformInit_irq_failed;
		}
		status = gpio_direction_input(egis->irqPin);
		if (status < 0) {
			DEBUG_PRINT("[egis] %s gpio_direction_input IRQ failed\n", __func__);
			goto egis_platformInit_gpio_init_failed;
		}
		
		status = gpio_request(egis->rstPin, "rst-gpio");
		if (status < 0) {
			DEBUG_PRINT("[egis] %s gpio_request rstPin failed\n", __func__);
			goto egis_platformInit_irq_failed;
		}
		status = gpio_direction_output(egis->rstPin, 1);
		if (status < 0) {
			DEBUG_PRINT("[egis] %s gpio_direction_output rstPin failed\n", __func__);
			goto egis_platformInit_gpio_init_failed;
		}
	}
	DEBUG_PRINT("[egis] %s: successful, status = %d\n", __func__, status);
	return status;

egis_platformInit_gpio_init_failed:
	gpio_free(egis->irqPin);

egis_platformInit_irq_failed:
//	gpio_free(egis->rstPin);
	DEBUG_PRINT("[egis] %s is failed\n", __func__);
	return status;
}


static int egis_parse_dt(struct device *dev, struct egis_data *data)
{
	struct device_node *np = dev->of_node;
	int errorno = 0;
	int gpio;

	gpio = of_get_named_gpio(np, "egis,gpio_reset", 0);
	if (gpio < 0) {
		errorno = gpio;
		goto dt_exit;
	} else {
		data->rstPin = gpio;
		DEBUG_PRINT("[egis] %s: rstPin = %d\n", __func__, data->rstPin);
	}

	
	gpio = of_get_named_gpio(np, "egis,gpio_irq", 0);
	if (gpio < 0) {
		errorno = gpio;
		goto dt_exit;
	} else {
		data->irqPin = gpio;
		DEBUG_PRINT("[egis] %s: irqPin = %d\n", __func__, data->irqPin);
	}

	
	DEBUG_PRINT("[egis] %s is successful\n", __func__);
	return errorno;
dt_exit:
	DEBUG_PRINT("[egis] %s is failed\n", __func__);
	return errorno;
}


static const struct file_operations egis_fops = {
	.owner = THIS_MODULE,
	.write = egis_write,
	.read = egis_read,
	.unlocked_ioctl = egis_ioctl,
	.compat_ioctl = egis_compat_ioctl,
	.open = egis_open,
	.release = egis_release,
	.llseek = no_llseek,
	.poll = fps_interrupt_poll};

/*-------------------------------------------------------------------------*/

static struct class *egis_class;
static int egis_probe(struct platform_device *pdev);
static int egis_remove(struct platform_device *pdev);
static const struct of_device_id egis_match_table[] = {{
	.compatible = "egis,egis_fp",
	},
	{},
};
MODULE_DEVICE_TABLE(of, egis_match_table);

static struct platform_driver egis_driver = {
	.driver = {
		.name = "et713",
		.owner = THIS_MODULE,
		.of_match_table = egis_match_table,
	},
	.probe = egis_probe,
	.remove = egis_remove,
};

static int egis_remove(struct platform_device *pdev)
{
	DEBUG_PRINT("[egis] %s (#%d)\n", __func__, __LINE__);
	free_irq(gpio_irq, g_data);
	del_timer_sync(&fps_ints.timer);
	wakeup_source_destroy(&wakeup_source_fp);

	request_irq_done = 0;
	return 0;
}

static int egis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct egis_data *egis;
	int status = 0;
	int major_number = 0;
	unsigned long minor;
	struct device *fdev;

	if(FP_EGIS_OPTICAL_ET713 != get_fpsensor_type()) {
	    DEBUG_PRINT("[egis] %s (#%d) not egis chip! exit\n", __func__, __LINE__);
        status = -EINVAL;
        return status;
    }

	DEBUG_PRINT("[egis] %s\n", __func__);
	BUILD_BUG_ON(N_PD_MINORS > 256);
	major_number = register_chrdev(major_number, "et713", &egis_fops);
	if (major_number < 0) {
		DEBUG_PRINT("[egis] %s: register_chrdev error.\n", __func__);
		return major_number;
	} else {
		DEBUG_PRINT("[egis] %s: register_chrdev major_number = %d.\n", __func__, major_number);
	}

	egis_class = class_create(THIS_MODULE, "et713");
	if (IS_ERR(egis_class)) {
		DEBUG_PRINT("[egis] %s: class_create error.\n", __func__);
		unregister_chrdev(major_number, egis_driver.driver.name);
		return PTR_ERR(egis_class);
	}
	/* Allocate driver data */
	egis = kzalloc(sizeof(*egis), GFP_KERNEL);
	if (egis == NULL) {
		DEBUG_PRINT("[egis] %s: Failed to kzalloc\n", __func__);
		return -ENOMEM;
	}
	/* Initialize the driver data */
	egis->pd = pdev;
	g_data = egis;

	spin_lock_init(&egis->pd_lock);
	mutex_init(&egis->buf_lock);
	mutex_init(&device_list_lock);

	INIT_LIST_HEAD(&egis->device_entry);

	status = egis_parse_dt(&pdev->dev, egis);
	/* platform init */
	status = egis_platformInit(egis);
	
	if (status != 0) {
		DEBUG_PRINT("[egis] %s: platform init failed\n", __func__);
		goto egis_probe_platformInit_failed;
	}

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
	/*
	 * If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_PD_MINORS);
	if (minor < N_PD_MINORS) {
		egis->devt = MKDEV(major_number, minor);
		fdev = device_create(egis_class, &pdev->dev, egis->devt, egis, "esfp0");
		status = IS_ERR(fdev) ? PTR_ERR(fdev) : 0;
	} else {
		DEBUG_PRINT("[egis] %s: no minor number available!\n", __func__);
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&egis->device_entry, &device_list);
	}
	mutex_unlock(&device_list_lock);

	if (status == 0)
		dev_set_drvdata(dev, egis);
	else
		goto egis_probe_failed;

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

	/* the timer is for ET310 */
	setup_timer(&fps_ints.timer, interrupt_timer_routine, (unsigned long)&fps_ints);
	add_timer(&fps_ints.timer);
	msm_drm_register_client(&egis_noti_block);

	wakeup_source_init(&wakeup_source_fp, "et713_wakeup");

	DEBUG_PRINT("[egis] %s: initialize success %d\n", __func__, status);

	

	
	egis_reset(egis);
	request_irq_done = 0;

#if EGIS_NAVI_INPUT
	sysfs_egis_init(egis);
	uinput_egis_init(egis);
#endif

	return status;

egis_probe_failed:
	device_destroy(egis_class, egis->devt);
	class_destroy(egis_class);

egis_probe_platformInit_failed:
	//egis_probe_parse_dt_failed:
	kfree(egis);
	DEBUG_PRINT("[egis] %s is failed\n", __func__);
#if EGIS_NAVI_INPUT
	uinput_egis_destroy(egis);
	sysfs_egis_destroy(egis);
#endif
	return status;
}

static int __init et713_init(void)
{
	int status = 0;
#ifdef CONFIG_BBK_FP_ID
	if (get_fp_id() != EGIS_ET713) {
		printk("%s: wrong fp id, not et713 driver, exit\n", __func__);
		return 0;
   }
#endif
	DEBUG_PRINT("[egis] %s\n", __func__);
	status = platform_driver_register(&egis_driver);
	DEBUG_PRINT("[egis] %s done\n", __func__);
	egis_netlink_init();
        /*Register for receiving tp touch event.
         * Must register after get_fpsensor_type filtration as only one handler can be registered.
         */
        opticalfp_irq_handler_register(egis_opticalfp_irq_handler);
	DEBUG_PRINT("[egis] after netlink init et713_init over\n");
	return status;
}

static void __exit et713_exit(void)
{
	DEBUG_PRINT("   -------   [egis] %s platform_driver_unregister\n", __func__);
	egis_netlink_exit();
	platform_driver_unregister(&egis_driver);
}

late_initcall(et713_init);
module_exit(et713_exit);

MODULE_AUTHOR("Wang YuWei, <robert.wang@egistec.com>");
MODULE_DESCRIPTION("Platform Driver Interface for et713");
MODULE_LICENSE("GPL");
