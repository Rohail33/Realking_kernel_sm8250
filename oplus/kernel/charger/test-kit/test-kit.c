// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2022 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "[TEST-KIT]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#include "test-kit.h"
#include "gpiolib.h"
#include "../oplus_charger.h"
#if IS_ENABLED(CONFIG_PINCTRL_MTK_V2)
#include "pinctrl-mtk-common-v2.h"
#endif

#define STRING_BUF_SIZE		4096
enum {
	FEATURE_TEST_PASS = 0,
	FEATURE_TEST_DISABLED,
	FEATURE_TEST_FAIL,
};

struct test_kit {
	struct miscdevice test_dev;
	struct mutex list_lock;
	struct mutex io_lock;
	struct list_head feature_list;

	unsigned int feature_index;
	struct test_feature *feature;
	unsigned int feature_num;

	test_kit_gpio_check_func_t qcom_soc_gpio_check;
	test_kit_gpio_check_func_t qcom_spmi_gpio_check;
	test_kit_gpio_check_func_t mtk_soc_gpio_check;
	test_kit_gpio_check_func_t mtk_spmi_gpio_check;
	test_kit_typec_port_check_func_t typec_port_check;
};

struct test_kit *g_test_kit;

/* common test func */
bool test_kit_typec_port_test(struct test_feature *feature,
				char *buf, size_t len)
{
	struct test_kit_typec_port_info *typec_port_info;
	size_t index = 0;
	size_t use_size;
	bool pass = true;

	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return false;
	}
	if (feature == NULL) {
		pr_err("feature is NULL\n");
		index += snprintf(buf + index, len - index, "feature is NULL");
		return false;
	}
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		index += snprintf(buf + index, len - index,
				  "g_test_kit is NULL");
		return false;
	}
	if (g_test_kit->typec_port_check == NULL) {
		pr_err("typec_port_check is NULL\n");
		index += snprintf(buf + index, len - index,
				  "typec port check func is NULL");
		return false;
	}

	typec_port_info = feature->cfg->test_info;
	while(typec_port_info->name) {
		if (!g_test_kit->typec_port_check((void *)typec_port_info,
		    buf + index, len - index, &use_size))
			pass = false;
		index += use_size;
		if (index >= len) {
			pr_err("str buf overflow\n");
			break;
		}
		typec_port_info++;
	}

	return pass;
}
EXPORT_SYMBOL(test_kit_typec_port_test);

bool test_kit_qcom_soc_gpio_test(struct test_feature *feature,
				char *buf, size_t len)
{
	struct test_kit_soc_gpio_info *gpio_info;
	size_t index = 0;
	size_t use_size;
	bool pass = true;

	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return false;
	}
	if (feature == NULL) {
		pr_err("feature is NULL\n");
		index += snprintf(buf + index, len - index, "feature is NULL");
		return false;
	}
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		index += snprintf(buf + index, len - index,
				  "g_test_kit is NULL");
		return false;
	}
	if (g_test_kit->qcom_soc_gpio_check == NULL) {
		pr_err("qcom_soc_gpio_check is NULL\n");
		index += snprintf(buf + index, len - index,
				  "gpio check func is NULL");
		return false;
	}

	gpio_info = feature->cfg->test_info;
	while(gpio_info->name) {
		if (!g_test_kit->qcom_soc_gpio_check((void *)gpio_info,
		    buf + index, len - index, &use_size))
			pass = false;
		index += use_size;
		if (index >= len) {
			pr_err("str buf overflow\n");
			break;
		}
		gpio_info++;
	}

	return pass;
}
EXPORT_SYMBOL(test_kit_qcom_soc_gpio_test);

bool test_kit_mtk_soc_gpio_test(struct test_feature *feature,
				char *buf, size_t len)
{
	struct test_kit_soc_gpio_info *gpio_info;
	size_t index = 0;
	size_t use_size;
	bool pass = true;

	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return false;
	}
	if (feature == NULL) {
		pr_err("feature is NULL\n");
		index += snprintf(buf + index, len - index, "feature is NULL");
		return false;
	}
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		index += snprintf(buf + index, len - index,
				  "g_test_kit is NULL");
		return false;
	}
	if (g_test_kit->mtk_soc_gpio_check == NULL) {
		pr_err("mtk_gpio_check is NULL\n");
		index += snprintf(buf + index, len - index,
				  "gpio check func is NULL");
		return false;
	}

	gpio_info = feature->cfg->test_info;
	while(gpio_info->name) {
		if (!g_test_kit->mtk_soc_gpio_check((void *)gpio_info,
		    buf + index, len - index, &use_size))
			pass = false;
		index += use_size;
		if (index >= len) {
			pr_err("str buf overflow\n");
			break;
		}
		gpio_info++;
	}

	return pass;
}
EXPORT_SYMBOL(test_kit_mtk_soc_gpio_test);


struct test_feature * __must_check
test_feature_register(const struct test_feature_cfg *cfg, void *private_data)
{
	struct test_feature *feature;

	if (cfg == NULL) {
		pr_err("feature is NULL\n");
		return NULL;
	}
	if (private_data == NULL) {
		pr_err("private_data is NULL\n");
		return NULL;
	}
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		return NULL;
	}

	feature = kzalloc(sizeof(struct test_feature), GFP_KERNEL);
	if (feature == NULL) {
		pr_err("alloc test_feature error\n");
		return NULL;
	}
	feature->cfg = cfg;
	feature->private_data = private_data;

	mutex_lock(&g_test_kit->list_lock);
	list_add(&feature->list, &g_test_kit->feature_list);
	g_test_kit->feature_num++;
	mutex_unlock(&g_test_kit->list_lock);

	feature->enable = true;

	return feature;
}
EXPORT_SYMBOL(test_feature_register);

void test_feature_unregister(struct test_feature *feature)
{
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		return;
	}

	mutex_lock(&g_test_kit->list_lock);
	list_del(&feature->list);
	mutex_unlock(&g_test_kit->list_lock);
	WARN_ON(g_test_kit->feature_num == 0);
	if (g_test_kit->feature_num > 0)
		g_test_kit->feature_num--;
	kfree(feature);
}
EXPORT_SYMBOL(test_feature_unregister);

int test_feature_enable(struct test_feature *feature)
{
	if (feature == NULL) {
		pr_err("feature is NULL\n");
		return -EINVAL;
	}
	feature->enable = true;

	return 0;
}
EXPORT_SYMBOL(test_feature_enable);

int test_feature_disable(struct test_feature *feature)
{
	if (feature == NULL) {
		pr_err("feature is NULL\n");
		return -EINVAL;
	}
	feature->enable = false;

	return 0;
}
EXPORT_SYMBOL(test_feature_disable);

int test_kit_reg_typec_port_check(test_kit_typec_port_check_func_t func)
{
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		return -ENODEV;
	}

	g_test_kit->typec_port_check = func;
	return 0;
}
EXPORT_SYMBOL(test_kit_reg_typec_port_check);

void test_kit_unreg_typec_port_check(void)
{
	if (g_test_kit == NULL)
		return;
	g_test_kit->typec_port_check = NULL;
}
EXPORT_SYMBOL(test_kit_unreg_typec_port_check);

int test_kit_reg_qcom_soc_gpio_check(test_kit_gpio_check_func_t func)
{
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		return -ENODEV;
	}

	g_test_kit->qcom_soc_gpio_check = func;
	return 0;
}
EXPORT_SYMBOL(test_kit_reg_qcom_soc_gpio_check);

void test_kit_unreg_qcom_soc_gpio_check(void)
{
	if (g_test_kit == NULL)
		return;
	g_test_kit->qcom_soc_gpio_check = NULL;
}
EXPORT_SYMBOL(test_kit_unreg_qcom_soc_gpio_check);

int test_kit_reg_qcom_spmi_gpio_check(test_kit_gpio_check_func_t func)
{
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		return -ENODEV;
	}

	g_test_kit->qcom_spmi_gpio_check = func;
	return 0;
}
EXPORT_SYMBOL(test_kit_reg_qcom_spmi_gpio_check);

void test_kit_unreg_qcom_spmi_gpio_check(void)
{
	if (g_test_kit == NULL)
		return;
	g_test_kit->qcom_spmi_gpio_check = NULL;
}
EXPORT_SYMBOL(test_kit_unreg_qcom_spmi_gpio_check);

int test_kit_reg_mtk_soc_gpio_check(test_kit_gpio_check_func_t func)
{
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		return -ENODEV;
	}

	g_test_kit->mtk_soc_gpio_check = func;
	return 0;
}
EXPORT_SYMBOL(test_kit_reg_mtk_soc_gpio_check);

void test_kit_unreg_mtk_soc_gpio_check(void)
{
	if (g_test_kit == NULL)
		return;
	g_test_kit->mtk_soc_gpio_check = NULL;
}
EXPORT_SYMBOL(test_kit_unreg_mtk_soc_gpio_check);

int test_kit_reg_mtk_spmi_gpio_check(test_kit_gpio_check_func_t func)
{
	if (g_test_kit == NULL) {
		pr_err("g_test_kit is NULL\n");
		return -ENODEV;
	}

	g_test_kit->mtk_spmi_gpio_check = func;
	return 0;
}
EXPORT_SYMBOL(test_kit_reg_mtk_spmi_gpio_check);

void test_kit_unreg_mtk_spmi_gpio_check(void)
{
	if (g_test_kit == NULL)
		return;
	g_test_kit->mtk_spmi_gpio_check = NULL;
}
EXPORT_SYMBOL(test_kit_unreg_mtk_spmi_gpio_check);

bool test_kit_mtk_gpio_check(void *info, char *buf, size_t len, size_t *use_size)
{
#if IS_ENABLED(CONFIG_PINCTRL_MTK_V2)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	const char *pinctrl_name = "pinctrl_paris";
#else
	const char *pinctrl_name = "pinctrl_mtk_v2";
#endif
	struct test_kit_soc_gpio_info *gpio_info = info;
	unsigned int pin = ARCH_NR_GPIOS - 1;
	struct gpio_desc *gdesc = NULL;
	struct mtk_pinctrl *hw = NULL;
	const struct mtk_pin_desc *desc = NULL;
	bool pass = true;
	int ret = 0;
	unsigned offset;
	int val, val_pullen, val_pullsel;

	static const char * const pulls_no_keeper[] = {
		"no pull",
		"pull down",
		"pull up",
	};

	if (info == NULL) {
		pr_err("[GPIO-CHECK]: info is NULL\n");
		return false;
	}
	if (buf == NULL) {
		pr_err("[GPIO-CHECK]: buf is NULL\n");
		return false;
	}

	*use_size = 0;
	offset = gpio_info->num;

	if (!gpio_is_valid(offset)) {
		pr_err("[GPIO-CHECK]: gpio is unvalid\n");
		return false;
	}

	do {
		gdesc = gpio_to_desc(pin);
		if (gdesc
		 && !strncmp(pinctrl_name,
				gdesc->gdev->chip->label,
				strlen(pinctrl_name))) {
			hw = gpiochip_get_data(gdesc->gdev->chip);
			if (!strcmp(hw->dev->parent->kobj.name, "soc") ||
			    !strcmp(hw->dev->parent->kobj.name, "platform")) {
				if (hw->soc->bias_get_combo &&
				    hw->soc->bias_set_combo) {
					break;
				}
			}
		}
		if (gdesc)
			pin = gdesc->gdev->chip->base - 1;
		if (pin == 0 || !gdesc) {
			pr_err("[GPIO-CHECK]: gpio_chip is NULL\n");
			*use_size += snprintf(buf + *use_size, len - *use_size,
				"[%s][gpio%u]:gpio_chip is NULL\n",
				gpio_info->name, offset);
			return false;
		}
	} while (1);

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[offset];

	ret = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DO, &val);
	if (ret || !!val != gpio_info->is_high) {
		*use_size += snprintf(buf + *use_size, len - *use_size,
			"[%s][gpio%u][level error]:expected:%s, actually:%s\n",
			gpio_info->name, offset,
			gpio_info->is_high ? "high" : "low",
			ret ? "null" : (val ? "high" : "low"));
		pass = false;
	}

	ret = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_MODE, &val);
	if (ret || val != gpio_info->func) {
		*use_size += snprintf(buf + *use_size, len - *use_size,
			"[%s][gpio%u][func error]:expected:%d, actually:%d\n",
			gpio_info->name, offset, gpio_info->func, ret ? -1 : val);
		pass = false;
	}

	ret = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DRV, &val);
	if (!ret)
		val = 2 + 2*val;
	if (ret || val < gpio_info->drive) {
		*use_size += snprintf(buf + *use_size, len - *use_size,
			"[%s][gpio%u][drive error]:expected:%dmA, actually:%dmA\n",
			gpio_info->name, offset, gpio_info->drive,
			ret ? -1 : val);
		pass = false;
	}

	ret = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DIR, &val);
	if (ret) {
		*use_size += snprintf(buf + *use_size, len - *use_size,
			"[%s][gpio%u][direction error]:expected:%s, actually:%s\n",
			gpio_info->name, offset,
			gpio_info->is_out ? "out" : "in",
			"null");
		return false;
	}

	if (val != gpio_info->is_out) {
		*use_size += snprintf(buf + *use_size, len - *use_size,
			"[%s][gpio%u][direction error]:expected:%s, actually:%s\n",
			gpio_info->name, offset,
			gpio_info->is_out ? "out" : "in",
			val ? "out" : "in");
		return false;
	}

	if (!gpio_info->is_out) {
		ret = mtk_pinconf_bias_get_combo(hw, desc, &val_pullsel, &val_pullen);
		if (ret) {
			*use_size += snprintf(buf + *use_size, len - *use_size,
			"[%s][gpio%u][pull error]:expected:%s, actually:%s\n",
			gpio_info->name, offset,
			pulls_no_keeper[gpio_info->pull],
			"null");
			pass = false;
		} else if (!gpio_info->pull) {
			if (val_pullen) {
				*use_size += snprintf(buf + *use_size, len - *use_size,
					"[%s][gpio%u][pull error]:expected:%s, actually:%s\n",
					gpio_info->name, offset,
					pulls_no_keeper[gpio_info->pull],
					pulls_no_keeper[val_pullsel + 1]);
				pass = false;
			}
		} else if (!val_pullen) {
			*use_size += snprintf(buf + *use_size, len - *use_size,
				"[%s][gpio%u][pull error]:expected:%s, actually:%s\n",
				gpio_info->name, offset,
				pulls_no_keeper[gpio_info->pull],
				pulls_no_keeper[0]);
			pass = false;
		} else if (gpio_info->pull != val_pullsel + 1) {
			*use_size += snprintf(buf + *use_size, len - *use_size,
				"[%s][gpio%u][pull error]:expected:%s, actually:%s\n",
				gpio_info->name, offset,
				pulls_no_keeper[gpio_info->pull],
				pulls_no_keeper[val_pullsel + 1]);
			pass = false;
		}
	}

	return pass;
#else
	return false;
#endif
}
EXPORT_SYMBOL(test_kit_mtk_gpio_check);

bool test_kit_qcom_gpio_check(void *info, char *buf, size_t len, size_t *use_size)
{
	return false;
}
EXPORT_SYMBOL(test_kit_qcom_gpio_check);

static int test_kit_set_index(struct test_kit *test_kit, unsigned int index)
{
	struct test_feature *feature;
	unsigned int i = 0;

	test_kit->feature_index = index;

	mutex_lock(&test_kit->list_lock);
	list_for_each_entry(feature, &test_kit->feature_list, list) {
		if (i == index) {
			test_kit->feature = feature;
			break;
		}
		i++;
	}
	mutex_unlock(&test_kit->list_lock);

	if (test_kit->feature == NULL)
		return -EINVAL;
	return 0;
}

static ssize_t test_kit_get_feature_list(struct test_kit *test_kit, char *buf,
					 size_t len)
{
	struct test_feature *feature;
	ssize_t index = 0;
	int i = 0;

	mutex_lock(&test_kit->list_lock);
	list_for_each_entry(feature, &test_kit->feature_list, list) {
		index += snprintf(buf + index, len - index, "%3d: %s:%d\n",
			i, feature->cfg->name, feature->enable);
		if (index >= len) {
			pr_err("str buf overflow\n");
			index = (ssize_t)len;
			break;
		}
		i++;
	}
	mutex_unlock(&test_kit->list_lock);

	index--;
	buf[index] = 0;

	return index;
}

static int test_kit_dev_open(struct inode *inode, struct file *filp)
{
	struct test_kit *test_kit =
		container_of(filp->private_data, struct test_kit, test_dev);

	filp->private_data = test_kit;
	return 0;
}

static ssize_t test_kit_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct test_kit *test_kit = filp->private_data;
	char *str_buf;
	ssize_t rc;
	ssize_t size;

	mutex_lock(&test_kit->io_lock);
	str_buf = kzalloc(STRING_BUF_SIZE, GFP_KERNEL);
	if (str_buf == NULL) {
		pr_err("alloc str buf error\n");
		return -ENOMEM;
	}
	size = test_kit_get_feature_list(test_kit, str_buf, STRING_BUF_SIZE);
	if (size < 0) {
		pr_err("can't get feature list, rc=%ld\n", size);
		rc = size;
		goto out;
	}
	if (size < STRING_BUF_SIZE) {
		str_buf[size] = '\n';
		size++;
	}

	if (size > *offset)
		size -= *offset;
	else
		size = 0;
	rc = copy_to_user(buf, str_buf, size);
	if (rc) {
		pr_err("can't copy feature list, rc=%ld\n", rc);
		goto out;
	}
	*offset += size < count ? size : count;
	rc = size < count ? size : count;

out:
	kfree(str_buf);
	mutex_unlock(&test_kit->io_lock);
	return rc;
}

#define TEST_KIT_IOC_MAGIC		0xf6
#define TEST_KIT_SET_INDEX		_IOW(TEST_KIT_IOC_MAGIC, 0x01, unsigned int)
#define TEST_KIT_GET_INDEX		_IOR(TEST_KIT_IOC_MAGIC, 0x02, unsigned int)
#define TEST_KIT_GET_NAME		_IOC(_IOC_READ, TEST_KIT_IOC_MAGIC, 0x03 , STRING_BUF_SIZE)
#define TEST_KIT_GET_FEATURE_LIST	_IOC(_IOC_READ, TEST_KIT_IOC_MAGIC, 0x04 , STRING_BUF_SIZE)
#define TEST_KIT_GET_FEATURE_NUM	_IOR(TEST_KIT_IOC_MAGIC, 0x05, unsigned int)
#define TEST_KIT_RUN_TEST		_IOC(_IOC_READ, TEST_KIT_IOC_MAGIC, 0x06 , STRING_BUF_SIZE)

static long test_kit_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct test_kit *test_kit = filp->private_data;
	void __user *argp = (void __user *)arg;
	int rc = 0;
	size_t str_len;
	char *str_buf = NULL;
	bool pass;

	mutex_lock(&test_kit->io_lock);

	switch (cmd) {
	case TEST_KIT_SET_INDEX:
		rc = test_kit_set_index(test_kit, (unsigned int)arg);
		if (rc) {
			pr_err("can't set feature index to %u, rc=%d\n",
				(unsigned int)arg, rc);
			goto out;
		}
		pr_info("feature_index=%u\n", test_kit->feature_index);
		break;
	case TEST_KIT_GET_INDEX:
		rc = copy_to_user(argp, &test_kit->feature_index,
				  sizeof(unsigned int));
		if (rc) {
			pr_err("can't copy feature index, rc=%d\n", rc);
			goto out;
		}
		break;
	case TEST_KIT_GET_NAME:
		if (test_kit->feature == NULL) {
			rc = -EFAULT;
			pr_err("feature index not set\n");
			goto out;
		}
		str_len = strlen(test_kit->feature->cfg->name);
		if (str_len > STRING_BUF_SIZE)
			str_len = STRING_BUF_SIZE;
		rc = copy_to_user(argp, test_kit->feature->cfg->name, str_len);
		if (rc) {
			pr_err("can't copy feature name, rc=%d\n", rc);
			goto out;
		}
		break;
	case TEST_KIT_GET_FEATURE_LIST:
		str_buf = kzalloc(STRING_BUF_SIZE, GFP_KERNEL);
		if (str_buf == NULL) {
			pr_err("alloc str buf error\n");
			rc = -ENOMEM;
			goto out;
		}
		rc = test_kit_get_feature_list(test_kit, str_buf,
					       STRING_BUF_SIZE);
		if (rc < 0) {
			pr_err("can't get feature list, rc=%d\n", rc);
			goto out;
		}
		rc = copy_to_user(argp, str_buf, STRING_BUF_SIZE);
		if (rc) {
			pr_err("can't copy feature list, rc=%d\n", rc);
			goto out;
		}
		break;
	case TEST_KIT_GET_FEATURE_NUM:
		rc = copy_to_user(argp, &test_kit->feature_num,
				  sizeof(unsigned int));
		if (rc) {
			pr_err("can't copy feature num, rc=%d\n", rc);
			goto out;
		}
		break;
	case TEST_KIT_RUN_TEST:
		if (test_kit->feature == NULL) {
			rc = -EFAULT;
			pr_err("feature index not set\n");
			goto out;
		}
		if (!test_kit->feature->enable) {
			rc = FEATURE_TEST_DISABLED;
			pr_err("feature is disabled\n");
			goto out;
		}
		str_buf = kzalloc(STRING_BUF_SIZE, GFP_KERNEL);
		if (str_buf == NULL) {
			pr_err("alloc str buf error\n");
			rc = -ENOMEM;
			goto out;
		}
		pass = test_kit->feature->cfg->test_func(test_kit->feature,
			str_buf, STRING_BUF_SIZE);
		if (pass) {
			rc = FEATURE_TEST_PASS;
			goto out;
		}
		rc = copy_to_user(argp, str_buf, STRING_BUF_SIZE);
		if (rc) {
			pr_err("can't copy feature test result, rc=%d\n", rc);
			goto out;
		}
		rc = FEATURE_TEST_FAIL;
		break;
	default:
		pr_err("bad ioctl %u\n", cmd);
		rc = -EINVAL;
		goto out;
	}

out:
	if (str_buf)
		kfree(str_buf);
	mutex_unlock(&test_kit->io_lock);
	return rc;
}

static ssize_t test_kit_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	return count;
}

static const struct file_operations test_kit_dev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= test_kit_dev_write,
	.read		= test_kit_dev_read,
	.open		= test_kit_dev_open,
	.unlocked_ioctl	= test_kit_dev_ioctl,
};

static __init int test_kit_driver_init(void)
{
	int rc;
	struct test_kit *test_kit;

	test_kit = kzalloc(sizeof(struct test_kit), GFP_KERNEL);
	if (test_kit == NULL) {
		pr_err("alloc test_kit buf error\n");
		return -ENOMEM;
	}

	mutex_init(&test_kit->list_lock);
	mutex_init(&test_kit->io_lock);
	INIT_LIST_HEAD(&test_kit->feature_list);

	test_kit->test_dev.minor = MISC_DYNAMIC_MINOR;
	test_kit->test_dev.name = "chg_test_kit";
	test_kit->test_dev.fops = &test_kit_dev_fops;
	rc = misc_register(&test_kit->test_dev);
	if (rc) {
		pr_err("misc_register failed, rc=%d\n", rc);
		goto test_dev_reg_err;
	}

	g_test_kit = test_kit;
	return 0;

test_dev_reg_err:
	kfree(test_kit);
	return rc;
}
module_init(test_kit_driver_init);

static __exit void test_kit_driver_exit(void)
{
	struct test_kit *test_kit = g_test_kit;

	if (g_test_kit == NULL)
		return;

	g_test_kit = NULL;
	misc_deregister(&test_kit->test_dev);
}
module_exit(test_kit_driver_exit);

MODULE_LICENSE("GPL v2");
