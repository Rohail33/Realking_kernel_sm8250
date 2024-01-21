// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>


#define GPIO_DEVICE_NAME "oplus-gpio"

#define log_fmt(fmt) "[line:%d][module:%s][%s] " fmt

#define OPLUS_GPIO_ERR(a, arg...) \
do { \
	printk(KERN_NOTICE log_fmt(a), __LINE__, GPIO_DEVICE_NAME, __func__, ##arg); \
} while (0)

#define OPLUS_GPIO_MSG(a, arg...) \
do { \
	printk(KERN_INFO log_fmt(a), __LINE__, GPIO_DEVICE_NAME, __func__, ##arg); \
} while (0)


#define MAX_GPIOS 3
#define OPLUS_GPIO_MAJOR 0

#define OPLUS_GPIO_MAGIC 'E'
#define OPLUS_GPIO_GET_OUTPUT_VALUE      _IOWR(OPLUS_GPIO_MAGIC, 0, int)
#define OPLUS_GPIO_GET_INPUT_VALUE       _IOWR(OPLUS_GPIO_MAGIC, 1, int)
#define OPLUS_GPIO_SET_OUTPUT_VALUE_HIGH _IOWR(OPLUS_GPIO_MAGIC, 2, int)
#define OPLUS_GPIO_SET_OUTPUT_VALUE_LOW  _IOWR(OPLUS_GPIO_MAGIC, 3, int)
#define OPLUS_GPIO_SET_OUTPUT_MODE       _IOWR(OPLUS_GPIO_MAGIC, 4, int)
#define OPLUS_GPIO_SET_INPUT_MODE        _IOWR(OPLUS_GPIO_MAGIC, 5, int)
#define OPLUS_GPIO_GET_GPIO_MODE         _IOWR(OPLUS_GPIO_MAGIC, 6, int)

#define SIM2_DET_STATUS_PULL_LOW 0
#define SIM2_DET_STATUS_PULL_HIGH 1
#define SIM2_DET_STATUS_NO_PULL 2
#define QMI_OEM_SET_UIM_RESET_PIN 56


typedef int (*proc_init_func)(struct platform_device *pdev, void *gpio_info);

struct oplus_gpio_info {
	char *dts_desc; /* eg : qcom,oplus-gpio-esim */
	char *dev_node_desc; /* node name */
	int   gpio;  /* -1 - invalid */
	int   gpio_mode;  /* 0 - input, 1 -output */
	int   gpio_status; /* 0 - low, 1 - high */
	dev_t devt;
	int   is_proc; /* 0 - not proc, 1- proc */
	proc_init_func proc_init;
	void *proc_data;
	struct mutex gpio_lock;
};

struct oplus_gpio_dev {
	dev_t devt;
	int base_minor;
	struct cdev cdev;
	struct class *oplus_gpio_class;
	struct oplus_gpio_info *gpio_info;
};

struct sim_det_data {
	struct pinctrl *pinctrl;
	int gpio;
	int gpio_status;
	int reset_pin;
	struct pinctrl_state *pull_high_state;
	struct pinctrl_state *pull_low_state;
	struct pinctrl_state *no_pull_state;
};

typedef enum {
	GPIO_TYPE_ESIM,
	GPIO_TYPE_ESIM_PRESENT,
	GPIO_TYPE_DUAL_SIM_DET,
} gpio_enum_type;

#ifdef CONFIG_OEM_QMI
extern int uim_qmi_power_up_req(u8 slot_id);
extern int uim_qmi_power_down_req(u8 slot_id);

#else
static int uim_qmi_power_up_req(u8 slot_id)
{
	return -1;
}
static int uim_qmi_power_down_req(u8 slot_id)
{
	return -1;
}
#endif
static int dual_sim_det_init(struct platform_device *pdev, void *gpio_info);

struct oplus_gpio_info oplus_gpio_info_table[MAX_GPIOS] = {
	[GPIO_TYPE_ESIM] =
	{
		.dts_desc = "oplus,oplus-gpio-esim",
		.dev_node_desc = "esim-gpio",
		.gpio = -1,
		.gpio_mode = 1,
		.gpio_status = 0,
		.devt = 0,
		.is_proc = 0,
	},
	[GPIO_TYPE_ESIM_PRESENT] =
	{
		.dts_desc = "oplus,oplus-esim-det",
		.dev_node_desc = "esim-det",
		.gpio = -1,
		.gpio_mode = 1,
		.gpio_status = 1,
		.devt = 0,
		.is_proc = 0,
	},
	[GPIO_TYPE_DUAL_SIM_DET] =
	{
		.dts_desc = "oplus,oplus-sim2-det",
		.dev_node_desc = "sim2_det",
		.gpio = -1,
		.gpio_mode = 0,
		.gpio_status = 0,
		.devt = 0,
		.is_proc = 1,
		.proc_init = dual_sim_det_init,
	}
};
static struct delayed_work recover_work;

static int dual_sim_det_uim2_to_real_sim(void)
{
	int esim_status = -1;

	if (oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio >= 0) {
		esim_status = oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio_status;

		if (esim_status == 1) {
			OPLUS_GPIO_MSG("esim is up, switch to real sim\n");
			uim_qmi_power_down_req(2);
			msleep(200);
			gpio_direction_output(oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio, 0);
			oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio_status = 0;
		}
	}

	return esim_status;
}

static void dual_sim_det_uim2_to_esim(struct work_struct *work)
{
	if (oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio >= 0) {
		OPLUS_GPIO_MSG("switch to esim back\n");
		gpio_direction_output(oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio, 1);
		oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio_status = 1;
		uim_qmi_power_up_req(2);
	}
}

static int dual_sim_det_show(struct seq_file *m, void *v)
{
	struct sim_det_data *det_info = m->private;
	int esim_status = 0;

	if (det_info) {

		det_info->gpio_status = -1;

		/* uim2 switch to real sim */
		esim_status = dual_sim_det_uim2_to_real_sim();
		if (esim_status == 1) {
			msleep(5);
		}

		/* det gpio pull up */
		pinctrl_select_state(det_info->pinctrl,
			det_info->pull_high_state);

		if (det_info->reset_pin >= 0) {
			/* reset pin set high */
			gpio_direction_output(det_info->reset_pin, 1);
			msleep(20);
			/* get reset pin status*/
			det_info->gpio_status = gpio_get_value(det_info->gpio);
			/* reset pin set low */
			gpio_direction_output(det_info->reset_pin, 0);

		} else {
			/* trigger reset pull up */
			uim_qmi_power_up_req(2);
			msleep(300);

			/* get reset pin status 3 times*/
			det_info->gpio_status = gpio_get_value(det_info->gpio);

			if (det_info->gpio_status != 1) {
				msleep(120);
				det_info->gpio_status = gpio_get_value(det_info->gpio);
			}

			if (det_info->gpio_status != 1) {
				msleep(120);
				det_info->gpio_status = gpio_get_value(det_info->gpio);
			}
		}

		OPLUS_GPIO_MSG("gpio_status: %d\n", det_info->gpio_status);

		/* det gpio set no pull   */
		pinctrl_select_state(det_info->pinctrl,
			det_info->no_pull_state);

		if (esim_status == 1) {
			/* uim2 switch to esim after 4 seconds */
			INIT_DELAYED_WORK(&recover_work, dual_sim_det_uim2_to_esim);
			schedule_delayed_work(&recover_work, msecs_to_jiffies(4100));
		}

		seq_printf(m, "%d\n", det_info->gpio_status);
	}

	return 0;
}

static int dual_sim_det_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dual_sim_det_show, PDE_DATA(inode));
}

static const struct file_operations dual_sim_det_fops = {
	.open = dual_sim_det_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int dual_sim_det_init(struct platform_device *pdev, void *gpio_info_ptr)
{
	struct proc_dir_entry *pentry;
	struct oplus_gpio_info *gpio_info = (struct oplus_gpio_info *)gpio_info_ptr;
	struct sim_det_data *det_info = NULL;
	const char *out_string = NULL;

	if (!pdev || !gpio_info) {
		OPLUS_GPIO_ERR("invalid param!\n");
		return -1;
	}

	det_info = kzalloc(sizeof(*det_info), GFP_KERNEL);

	if (!det_info) {
		OPLUS_GPIO_ERR("det_info alloc failed\n");
		return -1;
	}

	gpio_info->proc_data = det_info;
	det_info->gpio = gpio_info->gpio;
	det_info->gpio_status = -1;

	OPLUS_GPIO_MSG("gpio = %d\n", det_info->gpio);

	det_info->pinctrl = devm_pinctrl_get(&pdev->dev);

	if (IS_ERR_OR_NULL(det_info->pinctrl)) {
		OPLUS_GPIO_ERR("det_info->pinctrl is null\n");
		kfree(det_info);
		return -1;
	}

	det_info->pull_high_state = pinctrl_lookup_state(
			det_info->pinctrl,
			"sim2_det_pull_high");

	if (IS_ERR_OR_NULL(det_info->pull_high_state)) {
		OPLUS_GPIO_ERR("Failed to get the sim2 pull HIGH status\n");
		kfree(det_info);
		return -1;
	}

	det_info->pull_low_state = pinctrl_lookup_state(
			det_info->pinctrl,
			"sim2_det_pull_low");

	if (IS_ERR_OR_NULL(det_info->pull_low_state)) {
		OPLUS_GPIO_ERR("Failed to get the sim2 pull LOW status\n");
		kfree(det_info);
		return -1;
	}

	det_info->no_pull_state = pinctrl_lookup_state(
			det_info->pinctrl,
			"sim2_det_no_pull");

	if (IS_ERR_OR_NULL(det_info->no_pull_state)) {
		OPLUS_GPIO_ERR("Failed to get the sim2 No pull status\n");
		kfree(det_info);
		return -1;
	}

	det_info->reset_pin = of_get_named_gpio(pdev->dev.of_node,
			"oplus,uim-reset-pin", 0);

	if (!gpio_is_valid(det_info->reset_pin)) {
		OPLUS_GPIO_ERR("reset pin invalid\n");

		if (of_property_read_string(pdev->dev.of_node, "oplus,uim-reset-pin",
				&out_string)
			|| strcmp(out_string, "modem_solution")) {
			OPLUS_GPIO_ERR("modem_solution failed\n");
			kfree(det_info);
			return -1;
		}
	}

	OPLUS_GPIO_MSG("reset_pin = %d\n", det_info->reset_pin);

	pentry = proc_create_data(gpio_info->dev_node_desc, 0644, NULL,
			&dual_sim_det_fops, det_info);

	if (!pentry) {
		OPLUS_GPIO_ERR("create proc file failed.\n");
		kfree(det_info);
		return -1;
	}

	return 0;
}

static long oplus_gpio_ioctl(struct file *filp, unsigned int cmd,
	unsigned long data)
{
	int retval = 0;
	void __user *arg = (void __user *)data;
	struct oplus_gpio_info *gpio_info;
	int gpio_status = -1;
	int cmd_abs = cmd - OPLUS_GPIO_GET_OUTPUT_VALUE;

	/* OPLUS_GPIO_MSG("enter\n"); */

	gpio_info = filp->private_data;

	if (gpio_info == NULL) {
		return -EFAULT;
	}

	mutex_lock(&gpio_info->gpio_lock);

	OPLUS_GPIO_MSG("dev = %s cmd = %d\n",
		gpio_info->dev_node_desc ? gpio_info->dev_node_desc : "NULL",
		cmd_abs > 0 ? cmd_abs : -cmd_abs);

	switch (cmd) {
	case OPLUS_GPIO_GET_OUTPUT_VALUE:
		gpio_status = gpio_info->gpio_status;

		if (copy_to_user(arg, &gpio_status, sizeof(gpio_status))) {
			retval = -EFAULT;
		}
		break;

	case OPLUS_GPIO_GET_INPUT_VALUE:
		gpio_status = gpio_get_value(gpio_info->gpio);
		gpio_info->gpio_status = gpio_status;

		if (copy_to_user(arg, &gpio_status, sizeof(gpio_status))) {
			retval = -EFAULT;
		}
		break;

	case OPLUS_GPIO_SET_OUTPUT_VALUE_HIGH:
		gpio_direction_output(gpio_info->gpio, 1);
		gpio_info->gpio_status = 1;
		gpio_info->gpio_mode = 1;
		break;

	case OPLUS_GPIO_SET_OUTPUT_VALUE_LOW:
		gpio_direction_output(gpio_info->gpio, 0);
		gpio_info->gpio_status = 0;
		gpio_info->gpio_mode = 1;
		break;

	case OPLUS_GPIO_SET_OUTPUT_MODE:
		gpio_direction_output(gpio_info->gpio, gpio_info->gpio_status);
		gpio_info->gpio_mode = 1;
		break;

	case OPLUS_GPIO_SET_INPUT_MODE:
		gpio_direction_input(gpio_info->gpio);
		gpio_info->gpio_mode = 0;
		break;

	case OPLUS_GPIO_GET_GPIO_MODE:
		if (copy_to_user(arg, &gpio_info->gpio_mode, sizeof(gpio_info->gpio_mode))) {
			retval = -EFAULT;
		}
		break;

	default:
		retval = -EFAULT;
		break;
	}

	mutex_unlock(&gpio_info->gpio_lock);
	return retval;
}


static int oplus_gpio_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct oplus_gpio_dev *dev = NULL;
	int i;

	/* OPLUS_GPIO_MSG("enter\n"); */

	dev =  container_of(cdev, struct oplus_gpio_dev, cdev);

	filp->private_data = NULL;

	for (i = 0; i < MAX_GPIOS && dev != NULL; i++) {
		if (dev->gpio_info[i].devt == inode->i_rdev) {
			filp->private_data = &(dev->gpio_info[i]);
			break;
		}
	}

	if (!filp->private_data) {
		OPLUS_GPIO_ERR("can not find the gpio info\n");
		return -1;
	}

	return 0;
}


static ssize_t oplus_gpio_read(struct file *filp, char __user *buf, size_t siz,
	loff_t *ppos)
{
	struct oplus_gpio_info *gpio_info;
	char buff[128] = {0};
	ssize_t len = 0;

	/* OPLUS_GPIO_MSG("enter\n"); */

	gpio_info = filp->private_data;

	if (*ppos > 0) {
		return 0;
	}

	mutex_lock(&gpio_info->gpio_lock);

	len = sizeof(buff) / sizeof(buff[0]);

	if (gpio_info->gpio_mode == 0) {
		gpio_info->gpio_status = gpio_get_value(gpio_info->gpio);
	}

	len = snprintf(buff, len, "gpio = %d, gpio mode = %d, gpio status = %d\n",
			gpio_info->gpio,
			gpio_info->gpio_mode,
			gpio_info->gpio_status);

	if (copy_to_user(buf, buff, len)) {
		mutex_unlock(&gpio_info->gpio_lock);
		return -EFAULT;
	}

	mutex_unlock(&gpio_info->gpio_lock);

	*ppos += len;

	return len;
}


static const struct of_device_id oplus_gpio_dt_ids[] = {
	{ .compatible = "oplus,oplus-gpio" },
	{},
};


MODULE_DEVICE_TABLE(of, oplus_gpio_dt_ids);

static const struct file_operations oplus_gpio_fops = {
	.owner =    THIS_MODULE,
	.unlocked_ioctl = oplus_gpio_ioctl,
	.open           = oplus_gpio_open,
	.read           = oplus_gpio_read,
};


extern char *saved_command_line;
static void init_esim_status(void)
{
	if (strstr(saved_command_line, "esim.status=1")) {
		oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio_status = 1;
		oplus_gpio_info_table[GPIO_TYPE_ESIM_PRESENT].gpio_status = 0;
	} else {
		oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio_status = 0;
		oplus_gpio_info_table[GPIO_TYPE_ESIM_PRESENT].gpio_status = 1;
	}
	OPLUS_GPIO_MSG(" %d", oplus_gpio_info_table[GPIO_TYPE_ESIM].gpio_status);
}

static int oplus_gpio_probe(struct platform_device *pdev)
{
	int status = -1;
	struct oplus_gpio_dev *gpio_data = NULL;
	struct device *oplus_gpio_device;
	int valid_gpio = 0;
	int i;

	OPLUS_GPIO_MSG("enter\n");

	gpio_data = kzalloc(sizeof(*gpio_data), GFP_KERNEL);

	if (!gpio_data) {
		status = -ENOMEM;
		OPLUS_GPIO_ERR("failed to alloc memory\n");
		goto err;
	}

	init_esim_status();
	gpio_data->gpio_info = oplus_gpio_info_table;

	status = alloc_chrdev_region(&gpio_data->devt, 0, MAX_GPIOS, GPIO_DEVICE_NAME);

	if (status < 0) {
		OPLUS_GPIO_ERR("failed to alloc chrdev\n");
		goto err_alloc;
	}

	gpio_data->base_minor = MINOR(gpio_data->devt);

	cdev_init(&gpio_data->cdev, &oplus_gpio_fops);
	status = cdev_add(&gpio_data->cdev, gpio_data->devt, MAX_GPIOS);

	if (status < 0) {
		OPLUS_GPIO_ERR("failed to add chrdev\n");
		goto error_add;
	}

	gpio_data->oplus_gpio_class = class_create(THIS_MODULE, GPIO_DEVICE_NAME);

	if (IS_ERR(gpio_data->oplus_gpio_class)) {
		OPLUS_GPIO_ERR("failed to create class\n");
		goto err_class;
	}

	for (i = 0; i < MAX_GPIOS; i++) {
		dev_t devt = MKDEV(MAJOR(gpio_data->devt), gpio_data->base_minor + i);

		if (!gpio_data->gpio_info[i].dts_desc) {
			continue;
		}

		gpio_data->gpio_info[i].gpio = of_get_named_gpio(pdev->dev.of_node,
				gpio_data->gpio_info[i].dts_desc, 0);

		if (gpio_is_valid(gpio_data->gpio_info[i].gpio)) {
			status = gpio_request(gpio_data->gpio_info[i].gpio,
					gpio_data->gpio_info[i].dev_node_desc ? gpio_data->gpio_info[i].dev_node_desc :
					"NULL");

			if (status) {
				OPLUS_GPIO_ERR("failed to request gpio %d\n",
					gpio_data->gpio_info[i].gpio);
				gpio_data->gpio_info[i].gpio = -1;/* invalid */
				continue;
			}

		} else {
			OPLUS_GPIO_MSG("gpio is invalid!\n");
			continue;
		}

		OPLUS_GPIO_MSG("gpio_request: %d\n", gpio_data->gpio_info[i].gpio);

		valid_gpio += 1;

		if (gpio_data->gpio_info[i].dev_node_desc) {
			if (gpio_data->gpio_info[i].is_proc && gpio_data->gpio_info[i].proc_init) {
				if (0 != gpio_data->gpio_info[i].proc_init(pdev, &(gpio_data->gpio_info[i]))) {
					gpio_free(gpio_data->gpio_info[i].gpio);
					gpio_data->gpio_info[i].gpio = -1;
					valid_gpio -= 1;
				}

				continue;
			}

			oplus_gpio_device = device_create(gpio_data->oplus_gpio_class, NULL, devt, NULL,
					gpio_data->gpio_info[i].dev_node_desc);

			if (IS_ERR(oplus_gpio_device)) {
				OPLUS_GPIO_ERR("failed to create device: %s\n",
					gpio_data->gpio_info[i].dev_node_desc);
				gpio_free(gpio_data->gpio_info[i].gpio);
				gpio_data->gpio_info[i].gpio = -1;/* invalid */
				valid_gpio -= 1;
				continue;
			}
		}

		gpio_data->gpio_info[i].devt = devt;

		mutex_init(&(gpio_data->gpio_info[i].gpio_lock));

		/* init gpio */
		if (gpio_data->gpio_info[i].gpio_mode == 1) {
			gpio_direction_output(gpio_data->gpio_info[i].gpio,
				gpio_data->gpio_info[i].gpio_status);

		} else {
			gpio_direction_input(gpio_data->gpio_info[i].gpio);
			gpio_data->gpio_info[i].gpio_status = gpio_get_value(
					gpio_data->gpio_info[i].gpio);
		}
	}

	if (!valid_gpio) {
		OPLUS_GPIO_MSG("no valid oplus gpio\n");
		goto err_no_gpio;
	}

	dev_set_drvdata(&pdev->dev, gpio_data);

	OPLUS_GPIO_MSG("leave\n");

	return 0;

err_no_gpio:
	class_destroy(gpio_data->oplus_gpio_class);
err_class:
	cdev_del(&gpio_data->cdev);
error_add:
	unregister_chrdev_region(gpio_data->devt, MAX_GPIOS);
err_alloc:
	kfree(gpio_data);
err:
	return status;
}


static int oplus_gpio_remove(struct platform_device *pdev)
{
	struct oplus_gpio_dev *gpio_data = NULL;
	int i;

	gpio_data = dev_get_drvdata(&pdev->dev);

	if (gpio_data) {
		if (gpio_data->gpio_info) {
			for (i = 0; i < MAX_GPIOS; i++) {
				if (gpio_data->gpio_info[i].gpio > -1) {
					dev_t devt = MKDEV(MAJOR(gpio_data->devt), gpio_data->base_minor + i);
					gpio_free(gpio_data->gpio_info[i].gpio);
					device_destroy(gpio_data->oplus_gpio_class, devt);

					if (gpio_data->gpio_info[i].proc_data) {
						kfree(gpio_data->gpio_info[i].proc_data);
					}
				}
			}
		}

		class_destroy(gpio_data->oplus_gpio_class);
		cdev_del(&gpio_data->cdev);
		unregister_chrdev_region(gpio_data->devt, MAX_GPIOS);
		kfree(gpio_data);
	}

	return 0;
}


static struct platform_driver oplus_gpio_driver = {
	.probe = oplus_gpio_probe,
	.remove = oplus_gpio_remove,
	.driver = {
		.name = GPIO_DEVICE_NAME,
		.of_match_table = of_match_ptr(oplus_gpio_dt_ids),
	},
};

static int __init oplus_gpio_init(void)
{
	OPLUS_GPIO_MSG("enter\n");

	return platform_driver_register(&oplus_gpio_driver);
}

static void __init oplus_gpio_exit(void)
{
	OPLUS_GPIO_MSG("enter\n");

	platform_driver_unregister(&oplus_gpio_driver);
}


module_init(oplus_gpio_init);
module_exit(oplus_gpio_exit);


MODULE_DESCRIPTION("oplus gpio controller");
MODULE_LICENSE("GPL");
MODULE_ALIAS("gpio:oplus-gpio");
