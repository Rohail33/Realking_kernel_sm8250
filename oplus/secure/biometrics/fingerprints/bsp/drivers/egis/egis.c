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

//#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>
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
#include <linux/pm_wakeup.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include "../include/oplus_fp_common.h"

//#include <mt_spi_hal.h>
//#include <mt_spi.h>

//#include "mtk_spi.h"

//#include <mt-plat/mt_gpio.h>
#include "egis.h"
#include "ets_navi_input.h"

#define EGIS_NAVI_INPUT 1 // 1:open ; 0:close

#define FP_SPI_DEBUG
#define EDGE_TRIGGER_FALLING    0x0
#define	EDGE_TRIGGER_RAISING    0x1
#define	LEVEL_TRIGGER_LOW       0x2
#define	LEVEL_TRIGGER_HIGH      0x3

#define GPIO_PIN_IRQ  126 
#define GPIO_PIN_RESET 93
#define GPIO_PIN_33V 94

int g_egis_pmic_disable;
int g_spi_cs_mode;

extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);


//void mt_spi_enable_clk(struct mt_spi_t *ms);
//void mt_spi_disable_clk(struct mt_spi_t *ms);

//struct mt_spi_t *egistec_mt_spi_t;

/*
 * FPS interrupt table
 */
 
struct interrupt_desc fps_ints = {0 , 0, "BUT0" , 0};


unsigned int bufsiz = 4096;

int gpio_irq;
int request_irq_done = 0;
int egistec_platformInit_done = 0;
static struct wakeup_source wakeup_source_fp;
static struct regulator *finger_regulator;

#define EDGE_TRIGGER_FALLING    0x0
#define EDGE_TRIGGER_RISING    0x1
#define LEVEL_TRIGGER_LOW       0x2
#define LEVEL_TRIGGER_HIGH      0x3

int egistec_platformInit(struct egistec_data *egistec);
int egistec_platformFree(struct egistec_data *egistec);

struct ioctl_cmd {
int int_mode;
int detect_period;
int detect_threshold;
}; 

static void delete_device_node(void);
static struct egistec_data *g_data;

DECLARE_BITMAP(minors, N_SPI_MINORS);
LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static struct of_device_id egistec_match_table[] = {
	{ .compatible = "mediatek,finger-fp",},
//        { .compatible = "goodix,goodix-fp",},
	{},
};

static struct of_device_id et512_spi_of_match[] = {
	{ .compatible = "mediatek,fingerspi-fp", },
	{}
};


MODULE_DEVICE_TABLE(of, et512_spi_of_match);

// add for dual sensor start
#if 0
static struct of_device_id fpswitch_match_table[] = {
	{ .compatible = "fp_id,fp_id",},
	{},
};
#endif
//add for dual sensor end
MODULE_DEVICE_TABLE(of, egistec_match_table);


/* ------------------------------ Interrupt -----------------------------*/
/*
 * Interrupt description
 */

#define FP_INT_DETECTION_PERIOD  10
#define FP_DETECTION_THRESHOLD	10

static DECLARE_WAIT_QUEUE_HEAD(interrupt_waitq);
#if 1
//static void spi_clk_enable(struct egistec_data *egistec, u8 bonoff)
static void spi_clk_enable(u8 bonoff)
{


	if (bonoff) {
		pr_err("%s line:%d enable spi clk\n", __func__,__LINE__);
		mt_spi_enable_master_clk(g_data->spi);
//		count = 1;
	} else //if ((count > 0) && (bonoff == 0)) 
	{
		pr_err("%s line:%d disable spi clk\n", __func__,__LINE__);

		mt_spi_disable_master_clk(g_data->spi);

//		count = 0;
	}


}

#endif

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

void egis_irq_enable(bool enable)
{
    unsigned long nIrqFlag;
    struct irq_desc *desc;
    desc = irq_to_desc(gpio_irq);
    spin_lock_irqsave(&g_data->irq_lock, nIrqFlag);
    if (1 == enable && 0 == g_data->irq_enable_flag) {
        enable_irq(gpio_irq);
        g_data->irq_enable_flag = 1;
    } else if (0 == enable && 1 == g_data->irq_enable_flag) {
        disable_irq_nosync(gpio_irq);
        //__disable_irq(desc, gpio_irq, 0);
        g_data->irq_enable_flag = 0;
    }
    DEBUG_PRINT("g_data->irq_enable_flag = %d,desc->depth = %d\n", g_data->irq_enable_flag, desc->depth);
    spin_unlock_irqrestore(&g_data->irq_lock, nIrqFlag);
}

static irqreturn_t fp_eint_func_ll(int irq , void *dev_id)
{
	DEBUG_PRINT("[egis]fp_eint_func_ll\n");
	fps_ints.finger_on = 1;
        g_data->irq_enable_flag = 1;
        egis_irq_enable(0);
	wake_up_interruptible(&interrupt_waitq);
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

int Interrupt_Init(struct egistec_data *egistec,int int_mode,int detect_period,int detect_threshold)
{

	int err = 0;
	int status = 0;
    struct device_node *node = NULL;
    struct irq_desc *desc = NULL;
    struct irq_desc *desc1 = NULL;

DEBUG_PRINT("FP --  %s mode = %d period = %d threshold = %d\n",__func__,int_mode,detect_period,detect_threshold);
DEBUG_PRINT("FP --  %s request_irq_done = %d gpio_irq = %d  pin = %d  \n",__func__,request_irq_done,gpio_irq, egistec->irqPin);


	fps_ints.detect_period = detect_period;
	fps_ints.detect_threshold = detect_threshold;
	fps_ints.int_count = 0;
	fps_ints.finger_on = 0;


	if (request_irq_done == 0)
	{
//		gpio_irq = gpio_to_irq(egistec->irqPin);
        node = of_find_matching_node(node, egistec_match_table);
//        printk("ttt-fp_irq number %d\n", node? 1:2);

        if (node){
                gpio_irq = irq_of_parse_and_map(node, 0);
                printk("fp_irq number %d\n", gpio_irq);
		}else
		printk("node = of_find_matching_node fail error  \n");

		if (gpio_irq < 0) {
			DEBUG_PRINT("%s gpio_to_irq failed\n", __func__);
			status = gpio_irq;
			goto done;
		}


		DEBUG_PRINT("[Interrupt_Init] flag current: %d disable: %d enable: %d\n",
		fps_ints.drdy_irq_flag, DRDY_IRQ_DISABLE, DRDY_IRQ_ENABLE);

		if (int_mode == EDGE_TRIGGER_RISING){
		DEBUG_PRINT("%s EDGE_TRIGGER_RISING\n", __func__);
        err = request_irq(gpio_irq, fp_eint_func_ll, IRQ_TYPE_EDGE_RISING, "fp_detect-eint", egistec);
        if (err) {
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
                    }
		}
		else if (int_mode == EDGE_TRIGGER_FALLING){
			DEBUG_PRINT("%s EDGE_TRIGGER_FALLING\n", __func__);
            err = request_irq(gpio_irq, fp_eint_func_ll, IRQ_TYPE_EDGE_FALLING, "fp_detect-eint", egistec);
            if (err) {
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
                    }
		}
		else if (int_mode == LEVEL_TRIGGER_LOW) {
			DEBUG_PRINT("%s LEVEL_TRIGGER_LOW\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func_ll,IRQ_TYPE_LEVEL_LOW,"fp_detect-eint", egistec);
			if (err){
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
				}
		}
		else if (int_mode == LEVEL_TRIGGER_HIGH){
			DEBUG_PRINT("%s LEVEL_TRIGGER_HIGH\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func_ll,IRQ_TYPE_LEVEL_HIGH,"fp_detect-eint", egistec);
			if (err){
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
				}
		}
		DEBUG_PRINT("[Interrupt_Init]:gpio_to_irq return: %d\n", gpio_irq);
		DEBUG_PRINT("[Interrupt_Init]:request_irq return: %d\n", err);

		fps_ints.drdy_irq_flag = DRDY_IRQ_ENABLE;
		enable_irq_wake(gpio_irq);

desc1 = irq_to_desc(gpio_irq);
		DEBUG_PRINT("huzhonghua:****depth state =  %d\n", desc1->depth);
		request_irq_done = 1;
                g_data->irq_enable_flag = 1;
	}

        egis_irq_enable(1);
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

int Interrupt_Free(struct egistec_data *egistec)
{
	DEBUG_PRINT("%s\n", __func__);
	fps_ints.finger_on = 0;

        if (1 == g_data->irq_enable_flag) {
            egis_irq_enable(0);
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
	if (fps_ints.finger_on) {
		mask |= POLLIN | POLLRDNORM;
	}
	return mask;
}

void fps_interrupt_abort(void)
{
	DEBUG_PRINT("%s\n", __func__);
	fps_ints.finger_on = 0;
	wake_up_interruptible(&interrupt_waitq);
}

/*-------------------------------------------------------------------------*/
static void egistec_reset(struct egistec_data *egistec)
{
	DEBUG_PRINT("%s\n", __func__);
	
	#ifdef CONFIG_OF
	pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_reset_low);
	mdelay(15);
	pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_reset_high);
	#endif	
	
}

static ssize_t egistec_read(struct file *filp,
	char __user *buf,
	size_t count,
	loff_t *f_pos)
{
	/*Implement by vendor if needed*/
	return 0;
}

static ssize_t egistec_write(struct file *filp,
	const char __user *buf,
	size_t count,
	loff_t *f_pos)
{
	/*Implement by vendor if needed*/
	return 0;
}

static long egistec_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int retval = 0;
	struct egistec_data *egistec;
	struct ioctl_cmd data;
	int status = 0;

	memset(&data, 0, sizeof(data));
	printk("%s  ---------   zq  001  ---  cmd = 0x%X \n", __func__, cmd);	
	egistec = filp->private_data;


	if(!egistec_platformInit_done) {
		/* platform init */
		status = egistec_platformInit(egistec);
		if (status != 0) {
			pr_err("%s platforminit failed\n", __func__);
			//goto egistec_probe_platformInit_failed;
		}
	}

	switch (cmd) {
	case INT_TRIGGER_INIT:

		if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
			retval = -EFAULT;
		goto done;
		}

		//DEBUG_PRINT("fp_ioctl IOCTL_cmd.int_mode %u,
		//		IOCTL_cmd.detect_period %u,
		//		IOCTL_cmd.detect_period %u (%s)\n",
		//		data.int_mode,
		//		data.detect_period,
		//		data.detect_threshold, __func__);

		DEBUG_PRINT("fp_ioctl >>> fp Trigger function init\n");
		retval = Interrupt_Init(egistec, data.int_mode,data.detect_period,data.detect_threshold);
		DEBUG_PRINT("fp_ioctl trigger init = %x\n", retval);
	break;

	case FP_SENSOR_RESET:
			//gpio_request later
			DEBUG_PRINT("fp_ioctl ioc->opcode == FP_SENSOR_RESET --");
			egistec_reset(egistec);
		goto done;
	case INT_TRIGGER_CLOSE:
			DEBUG_PRINT("fp_ioctl <<< fp Trigger function close\n");
			retval = Interrupt_Free(egistec);
			DEBUG_PRINT("fp_ioctl trigger close = %x\n", retval);
		goto done;
	case INT_TRIGGER_ABORT:
			DEBUG_PRINT("fp_ioctl <<< fp Trigger function close\n");
			fps_interrupt_abort();
		goto done;
	case FP_FREE_GPIO:
			DEBUG_PRINT("fp_ioctl <<< FP_FREE_GPIO -------  \n");
			egistec_platformFree(egistec);
		goto done;

	case FP_SPICLK_ENABLE:
			DEBUG_PRINT("fp_ioctl <<< FP_SPICLK_ENABLE -------  \n");
			spi_clk_enable(1);
			//mt_spi_enable_clk(egistec_mt_spi_t);
		goto done;		
	case FP_SPICLK_DISABLE:
			DEBUG_PRINT("fp_ioctl <<< FP_SPICLK_DISABLE -------  \n");
			spi_clk_enable(0);
			//mt_spi_disable_clk(egistec_mt_spi_t);
		goto done;		
	case DELETE_DEVICE_NODE:
			DEBUG_PRINT("fp_ioctl <<< DELETE_DEVICE_NODE -------  \n");
			delete_device_node();
		goto done;
	case FP_WAKELOCK_TIMEOUT_ENABLE: //0Xb1
			DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_WAKELOCK_TIMEOUT_ENABLE  \n");
			__pm_wakeup_event(&wakeup_source_fp, msecs_to_jiffies(1500));
		goto done;
	case FP_WAKELOCK_TIMEOUT_DISABLE: //0Xb2
			DEBUG_PRINT("EGISTEC fp_ioctl <<< FP_WAKELOCK_TIMEOUT_DISABLE  \n");
			__pm_relax(&wakeup_source_fp);
		goto done;
	default:
	retval = -ENOTTY;
	break;
	}
	
	
	
done:

	DEBUG_PRINT("%s ----------- zq done  \n", __func__);
	return (retval);
}

#ifdef CONFIG_COMPAT
static long egistec_compat_ioctl(struct file *filp,
	unsigned int cmd,
	unsigned long arg)
{
	return egistec_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define egistec_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int egistec_open(struct inode *inode, struct file *filp)
{
	struct egistec_data *egistec;
	int			status = -ENXIO;

	DEBUG_PRINT("%s\n", __func__);
	printk("%s  ---------   zq    \n", __func__);
	
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
//				dev_dbg(&egistec->spi->dev, "open/ENOMEM\n");
				status = -ENOMEM;
			}
		}
		if (status == 0) {
			egistec->users++;
			filp->private_data = egistec;
			nonseekable_open(inode, filp);
		}
	} else {
		pr_debug("%s nothing for minor %d\n"
			, __func__, iminor(inode));
	}
	mutex_unlock(&device_list_lock);
	return status;
}

static int egistec_release(struct inode *inode, struct file *filp)
{
	struct egistec_data *egistec;

	DEBUG_PRINT("%s\n", __func__);

	mutex_lock(&device_list_lock);
	egistec = filp->private_data;
	filp->private_data = NULL;

	/* last close? */
	egistec->users--;
	if (egistec->users == 0) {
		int	dofree;

		kfree(egistec->buffer);
		egistec->buffer = NULL;

		/* ... after we unbound from the underlying device? */
		spin_lock_irq(&egistec->spi_lock);
		dofree = (egistec->pd == NULL);
		spin_unlock_irq(&egistec->spi_lock);

		if (dofree)
			kfree(egistec);		
	}
	mutex_unlock(&device_list_lock);
	return 0;

}

int egistec_platformFree(struct egistec_data *egistec)
{
	int status = 0;
	DEBUG_PRINT("%s\n", __func__);
	if (egistec_platformInit_done != 1)
	return status;
	if (egistec != NULL) {
		if (request_irq_done==1)
		{
		free_irq(gpio_irq, NULL);
		request_irq_done = 0;
		}
	gpio_free(egistec->irqPin);
	gpio_free(GPIO_PIN_RESET);
	}

	egistec_platformInit_done = 0;

	DEBUG_PRINT("%s successful status=%d\n", __func__, status);
	return status;
}


int egistec_platformInit(struct egistec_data *egistec)
{
	int status = 0;
    struct platform_device *pdev = egistec->pd;
    DEBUG_PRINT("%s\n", __func__);

	if (egistec != NULL) {

		/* Initial Reset Pin
		status = gpio_request(egistec->rstPin, "reset-gpio");
		if (status < 0) {
			pr_err("%s gpio_requset egistec_Reset failed\n",
				__func__);
			goto egistec_platformInit_rst_failed;
		}
		*/
//		gpio_direction_output(egistec->rstPin, 1);
//		if (status < 0) {
//			pr_err("%s gpio_direction_output Reset failed\n",
//					__func__);
//			status = -EBUSY;
//			goto egistec_platformInit_rst_failed;
//		}
		
		//added to initialize it as high
//		gpio_set_value(GPIO_PIN_RESET, 1);
//		msleep(30);
		
//		gpio_set_value(GPIO_PIN_RESET, 0);
//		msleep(30);
//		gpio_set_value(GPIO_PIN_RESET, 1);
//		msleep(20);

		/* initial 33V power pin */
//		gpio_direction_output(egistec->vcc_33v_Pin, 1);
//		gpio_set_value(egistec->vcc_33v_Pin, 1);

/*		status = gpio_request(egistec->vcc_33v_Pin, "33v-gpio");
		if (status < 0) {
			pr_err("%s gpio_requset egistec_Reset failed\n",
				__func__);
			goto egistec_platformInit_rst_failed;
		}
		gpio_direction_output(egistec->vcc_33v_Pin, 1);
		if (status < 0) {
			pr_err("%s gpio_direction_output Reset failed\n",
					__func__);
			status = -EBUSY;
			goto egistec_platformInit_rst_failed;
		}

		gpio_set_value(egistec->vcc_33v_Pin, 1);
*/


		/* Initial IRQ Pin
		status = gpio_request(egistec->irqPin, "irq-gpio");
		if (status < 0) {
			pr_err("%s gpio_request egistec_irq failed\n",
				__func__);
			goto egistec_platformInit_irq_failed;
		}
		*/
/*		
		status = gpio_direction_input(egistec->irqPin);
		if (status < 0) {
			pr_err("%s gpio_direction_input IRQ failed\n",
				__func__);
//			goto egistec_platformInit_gpio_init_failed;
		}
*/
        if (g_egis_pmic_disable == 1) {
        finger_regulator = devm_regulator_get(&pdev->dev, "vmch");
            if (IS_ERR(finger_regulator)) {
                printk(KERN_ERR "%s, get regulator err %ld!\n", __func__, PTR_ERR(finger_regulator));
                status = -1;
                goto err;
                }

            status = regulator_set_voltage(finger_regulator, 3300000, 3300000);
            printk(KERN_ERR "%s, szl regulator err %d!\n", __func__, status);
            status = regulator_enable(finger_regulator);
            printk(KERN_ERR "%s, szl le regulator err %d!\n", __func__, status);
            }

            else {
            status = pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_power_high);
            mdelay(2);
            if (!status) {
                DEBUG_PRINT("%s successful to select power active state\n", __func__);
            } else {
                DEBUG_PRINT("%s Failed to select power active state=%d\n", __func__, status);
                goto err;
            }
        }
}

	egistec_platformInit_done = 1;
        mdelay(2);
        if (g_spi_cs_mode == 1) {
            if (egistec->pins_spi_cs_mode) {
                pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_spi_cs_mode);
            }
            pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_reset_high);
    }

	DEBUG_PRINT("%s successful status=%d\n", __func__, status);
	return status;
/*
egistec_platformInit_gpio_init_failed:
	gpio_free(egistec->irqPin);
//	gpio_free(egistec->vcc_33v_Pin);
egistec_platformInit_irq_failed:
	gpio_free(egistec->rstPin);
egistec_platformInit_rst_failed:
*/
err:
	pr_err("%s is failed\n", __func__);
	return status;
}



static int egistec_parse_dt(struct device *dev,
	struct egistec_data *data)
{
//	struct device_node *np = dev->of_node;
	int errorno = 0;
    int rc = 0;

#ifdef CONFIG_OF
	int ret;

	struct device_node *node = NULL;
	struct platform_device *pdev = NULL;

	printk(KERN_ERR "%s, from dts pinctrl\n", __func__);

	node = of_find_compatible_node(NULL, NULL, "mediatek,finger-fp");
	if (node) {
		pdev = of_find_device_by_node(node);
		if (pdev) {
			data->pinctrl_gpios = devm_pinctrl_get(&pdev->dev);
			if (IS_ERR(data->pinctrl_gpios)) {
				ret = PTR_ERR(data->pinctrl_gpios);
				printk(KERN_ERR "%s can't find fingerprint pinctrl\n", __func__);
				return ret;
			}
/*
			data->pins_irq = pinctrl_lookup_state(data->pinctrl_gpios, "default");
			if (IS_ERR(data->pins_irq)) {
				ret = PTR_ERR(data->pins_irq);
				printk(KERN_ERR "%s can't find fingerprint pinctrl irq\n", __func__);
				return ret;
			}
			data->pins_miso_spi = pinctrl_lookup_state(data->pinctrl_gpios, "miso_spi");
			if (IS_ERR(data->pins_miso_spi)) {
				ret = PTR_ERR(data->pins_miso_spi);
				printk(KERN_ERR "%s can't find fingerprint pinctrl miso_spi\n", __func__);
				//return ret;
			}
			data->pins_miso_pullhigh = pinctrl_lookup_state(data->pinctrl_gpios, "miso_pullhigh");
			if (IS_ERR(data->pins_miso_pullhigh)) {
				ret = PTR_ERR(data->pins_miso_pullhigh);
				printk(KERN_ERR "%s can't find fingerprint pinctrl miso_pullhigh\n", __func__);
				//return ret;
			}
			data->pins_miso_pulllow = pinctrl_lookup_state(data->pinctrl_gpios, "miso_pulllow");
			if (IS_ERR(data->pins_miso_pulllow)) {
				ret = PTR_ERR(data->pins_miso_pulllow);
				printk(KERN_ERR "%s can't find fingerprint pinctrl miso_pulllow\n", __func__);
				//return ret;
			}

*/
            rc = of_property_read_u32(node, "egis,pmic_disable", &g_egis_pmic_disable);
            if (rc) {
                dev_err(&pdev->dev, "failed to request egis,egis_pmic_disable, ret = %d\n", rc);
                g_egis_pmic_disable = 0;
            }

            rc = of_property_read_u32(node, "egis,spi_cs_mode", &g_spi_cs_mode);
            if (rc) {
                dev_err(&pdev->dev, "failed to request egis,spi_cs_mode, ret = %d\n", rc);
                g_spi_cs_mode = 0;
            }

			data->pins_reset_high = pinctrl_lookup_state(data->pinctrl_gpios, "rst-high");
			if (IS_ERR(data->pins_reset_high)) {
				ret = PTR_ERR(data->pins_reset_high);
				printk(KERN_ERR "%s can't find fingerprint pinctrl reset_high\n", __func__);
				goto pinctrl_err;
			}
			data->pins_reset_low = pinctrl_lookup_state(data->pinctrl_gpios, "rst-low");
			if (IS_ERR(data->pins_reset_low)) {
				ret = PTR_ERR(data->pins_reset_low);
				printk(KERN_ERR "%s can't find fingerprint pinctrl reset_low\n", __func__);
				goto pinctrl_err;
			}

            if (g_egis_pmic_disable != 1) {
                data->pins_power_high = pinctrl_lookup_state(data->pinctrl_gpios, "power_high");
                if (IS_ERR(data->pins_power_high)) {
                    ret = PTR_ERR(data->pins_power_high);
                    printk(KERN_ERR "%s can't find fingerprint pinctrl power_high\n", __func__);
                    goto pinctrl_err;
                }
                data->pins_power_low = pinctrl_lookup_state(data->pinctrl_gpios, "power_low");
                if (IS_ERR(data->pins_power_low)) {
                    ret = PTR_ERR(data->pins_power_low);
                    printk(KERN_ERR "%s can't find fingerprint pinctrl power_low\n", __func__);
                    goto pinctrl_err;
                    }
                }

			printk(KERN_ERR "%s, get pinctrl success!!!!!\n", __func__);
		} else {
			printk(KERN_ERR "%s platform device is null\n", __func__);
		}
	} else {
		printk(KERN_ERR "%s device node is null\n", __func__);
		return -1;
	}

#endif
	DEBUG_PRINT("%s is successful\n", __func__);
	return 0;	

pinctrl_err:	
	pinctrl_put(data->pinctrl_gpios);
	pr_err("%s is failed\n", __func__);
	return errorno;
}

static const struct file_operations egistec_fops = {
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

static struct class *egistec_class;

/*-------------------------------------------------------------------------*/

static int egistec_probe(struct platform_device *pdev);
static int egistec_remove(struct platform_device *pdev);




typedef struct {
	struct spi_device      *spi;
	struct class           *class;
	struct device          *device;
//	struct cdev            cdev;
	dev_t                  devno;
	u8                     *huge_buffer;
	size_t                 huge_buffer_size;
	struct input_dev       *input_dev;
} et512_data_t;



/* -------------------------------------------------------------------- */
static int et512_spi_probe(struct spi_device *spi)
{
//	struct device *dev = &spi->dev;
	int error = 0;
	et512_data_t *et512 = NULL;
	/* size_t buffer_size; */

	printk(KERN_ERR "et512_spi_probe enter++++++\n");
#if 1
	et512 = kzalloc(sizeof(*et512), GFP_KERNEL);
	if (!et512) {
		/*
		dev_err(&spi->dev,
		"failed to allocate memory for struct et512_data\n");
		*/
		return -ENOMEM;
	}
	printk(KERN_INFO"%s\n", __func__);

	spi_set_drvdata(spi, et512);
#endif	
	g_data->spi = spi;
//	egistec_mt_spi_t=spi_master_get_devdata(spi->master);

	return error;
}

/* -------------------------------------------------------------------- */
static int et512_spi_remove(struct spi_device *spi)
{
	et512_data_t *et512 = spi_get_drvdata(spi);
	spi_clk_enable(0);
//	pr_debug("%s\n", __func__);

//	et512_manage_sysfs(et512, spi, false);

	//et512_sleep(et512, true);

	//cdev_del(&et512->cdev);

	//unregister_chrdev_region(et512->devno, 1);

	//et512_cleanup(et512, spi);
	kfree(et512);

	return 0;
}


static struct spi_driver spi_driver = {
	.driver = {
		.name	= "et512",
		.owner	= THIS_MODULE,
		.of_match_table = et512_spi_of_match,
        .bus	= &spi_bus_type,
	},
	.probe	= et512_spi_probe,
	.remove	= et512_spi_remove,
};

struct spi_board_info spi_board_devs[] __initdata = {
    [0] = {
        .modalias="et512",
        .bus_num = 0,
        .chip_select=1,
        .mode = SPI_MODE_0,
    },
};


static struct platform_driver egistec_driver = {
	.driver = {
		.name		= "et512",
		.owner		= THIS_MODULE,
		.of_match_table = egistec_match_table,
	},
    .probe =    egistec_probe,
    .remove =   egistec_remove,
};


static int egistec_remove(struct platform_device *pdev)
{
//	#if EGIS_NAVI_INPUT
	struct device *dev = &pdev->dev;
	struct egistec_data *egistec = dev_get_drvdata(dev);
//	#endif
	
    DEBUG_PRINT("%s(#%d)\n", __func__, __LINE__);
	free_irq(gpio_irq, NULL);
	
	#if EGIS_NAVI_INPUT
	uinput_egis_destroy(egistec);
	sysfs_egis_destroy(egistec);
	#endif
    #if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 14, 0)
    wakeup_source_trash(&wakeup_source_fp);
	del_timer_sync(&fps_ints.timer);
    #endif
	request_irq_done = 0;
	kfree(egistec);
    return 0;
}


static int egistec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct egistec_data *egistec;
	int status = 0;
	unsigned long minor;

	DEBUG_PRINT("%s initial\n", __func__);
	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(ET512_MAJOR, "et512", &egistec_fops);
	if (status < 0) {
			pr_err("%s register_chrdev error.\n", __func__);
			return status;
	}

	egistec_class = class_create(THIS_MODULE, "et512");
	if (IS_ERR(egistec_class)) {
		pr_err("%s class_create error.\n", __func__);
		unregister_chrdev(ET512_MAJOR, egistec_driver.driver.name);
		return PTR_ERR(egistec_class);
	}

	/* Allocate driver data */
	egistec = kzalloc(sizeof(*egistec), GFP_KERNEL);
	if (egistec== NULL) {
		pr_err("%s - Failed to kzalloc\n", __func__);
		return -ENOMEM;
	}

	/* device tree call */
	if (pdev->dev.of_node) {
		status = egistec_parse_dt(&pdev->dev, egistec);
		if (status) {
			pr_err("%s - Failed to parse DT\n", __func__);
			goto egistec_probe_parse_dt_failed;
		}
	}

	
//	egistec->rstPin = GPIO_PIN_RESET;
//	egistec->irqPin = GPIO_PIN_IRQ;
//	egistec->vcc_33v_Pin = GPIO_PIN_33V;
	
	
	
	/* Initialize the driver data */
	egistec->pd = pdev;
	g_data = egistec;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 14, 0)
    wakeup_source_init(&wakeup_source_fp, "et580_wakeup");
#else
    wakeup_source_prepare(&wakeup_source_fp, "et580_wakeup");
    wakeup_source_add(&wakeup_source_fp);
#endif
	spin_lock_init(&egistec->spi_lock);
	mutex_init(&egistec->buf_lock);
	mutex_init(&device_list_lock);
        spin_lock_init(&g_data->irq_lock);
        g_data->irq_enable_flag = 0;

	INIT_LIST_HEAD(&egistec->device_entry);
#if 1	
	/* platform init */
	status = egistec_platformInit(egistec);
	if (status != 0) {
		pr_err("%s platforminit failed\n", __func__);
		goto egistec_probe_platformInit_failed;
	}
#endif
	
#if 0 //gpio_request later
	/* platform init */
	status = egistec_platformInit(egistec);
	if (status != 0) {
		pr_err("%s platforminit failed\n", __func__);
		goto egistec_probe_platformInit_failed;
	}
#endif

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

	/*
	 * If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *fdev;
		egistec->devt = MKDEV(ET512_MAJOR, minor);
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

	if (status == 0){
		dev_set_drvdata(dev, egistec);
	}
	else {
		goto egistec_probe_failed;
	}

	////gpio_request later
	//egistec_reset(egistec);

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 14, 0)
	/* the timer is for ET310 */
	add_timer(&fps_ints.timer);
#endif

/*	

	struct device_node *node = NULL;
	int value;

	node = of_find_compatible_node(NULL, NULL, "goodix,goodix-fp");

	mt_spi_enable_master_clk(gf_dev->spi);

*/





	#if EGIS_NAVI_INPUT
	/*
	 * William Add.
	 */
	sysfs_egis_init(egistec);
	uinput_egis_init(egistec);
	#endif

	DEBUG_PRINT("%s : initialize success %d\n", __func__, status);	

	request_irq_done = 0;
	return status;

egistec_probe_failed:
	device_destroy(egistec_class, egistec->devt);
	class_destroy(egistec_class);

egistec_probe_platformInit_failed:
egistec_probe_parse_dt_failed:
	kfree(egistec);
	pr_err("%s is failed\n", __func__);
	return status;
}

static void delete_device_node(void)
{
	//int retval;
	DEBUG_PRINT("%s\n", __func__);
	//del_timer_sync(&fps_ints.timer);
	//spi_clk_enable(0);
	device_destroy(egistec_class, g_data->devt);
	DEBUG_PRINT("device_destroy \n");
	list_del(&g_data->device_entry);
	DEBUG_PRINT("list_del \n");
	class_destroy(egistec_class);
	DEBUG_PRINT("class_destroy\n");
        //spi_unregister_driver(&spi_driver);
	//DEBUG_PRINT("spi_unregister_driver\n");
	unregister_chrdev(ET512_MAJOR, egistec_driver.driver.name);
	DEBUG_PRINT("unregister_chrdev\n");
	g_data = NULL;
	//platform_driver_unregister(&egistec_driver);
	//DEBUG_PRINT("platform_driver_unregister\n");
}


static int __init egis512_init(void)
{
	int status = 0;
	int rc = 0;
    if ((FP_EGIS_520 != get_fpsensor_type())) {
        printk("%s, found not FP-egis sensor\n", __func__);
        status = -EINVAL;
        return status;
        }

    printk("%s, Need to register egistec FP driver\n", __func__);
	printk(KERN_INFO "%s\n", __func__);
    status = platform_driver_register(&egistec_driver);	
	if(status) {
		printk(KERN_ERR "%s  platform_driver_register fail  \n", __func__);
	}

	rc = spi_register_driver(&spi_driver);
	if (rc)
	{
		printk(KERN_ERR "registespi driver fail%s ,%d\n", __func__,rc);
		return -EINVAL;
	}
	printk(KERN_ERR "register spi driver fail 11%s ,%d\n", __func__,rc);

//	spi_clk_enable(1);
//	mt_spi_enable_clk(egistec_mt_spi_t);//temp
//	printk(KERN_ERR "spi enabled----\n");
	 printk(KERN_ERR "%s: probe successful !!!!!\n", __func__);

     return status;
}

static void __exit egis512_exit(void)
{

      platform_driver_unregister(&egistec_driver);
      spi_unregister_driver(&spi_driver);
}

module_init(egis512_init);
module_exit(egis512_exit);

MODULE_AUTHOR("Wang YuWei, <robert.wang@egistec.com>");
MODULE_DESCRIPTION("SPI Interface for ET512");
MODULE_LICENSE("GPL");
