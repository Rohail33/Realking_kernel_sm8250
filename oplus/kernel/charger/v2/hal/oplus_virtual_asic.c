// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[VIRTUAL_ASIC]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/of_irq.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_hal_vooc.h>
#include <oplus_mms_gauge.h>

struct oplus_virtual_asic_gpio {
	int switch_ctrl_gpio;
	int asic_switch1_gpio;
	int asic_switch2_gpio;
	int reset_gpio;
	int clock_gpio;
	int data_gpio;
	int asic_id_gpio;
	int data_irq;

	struct pinctrl *pinctrl;
	struct mutex pinctrl_mutex;

	struct pinctrl_state *asic_switch_normal;
	struct pinctrl_state *asic_switch_vooc;
	struct pinctrl_state *gpio_switch_ctrl_ap;
	struct pinctrl_state *gpio_switch_ctrl_asic;

	struct pinctrl_state *asic_clock_active;
	struct pinctrl_state *asic_clock_sleep;
	struct pinctrl_state *asic_data_active;
	struct pinctrl_state *asic_data_sleep;
	struct pinctrl_state *asic_reset_active;
	struct pinctrl_state *asic_reset_sleep;
	struct pinctrl_state *asic_id_active;
	struct pinctrl_state *asic_id_sleep;
};

struct oplus_virtual_asic_child {
	struct oplus_chg_ic_dev *ic_dev;
	bool initialized;
};

struct oplus_virtual_asic_ic {
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct oplus_chg_ic_dev *asic;
	struct oplus_virtual_asic_child *asic_list;
	struct oplus_virtual_asic_gpio gpio;
	int asic_num;

	enum oplus_chg_vooc_ic_type vooc_ic_type;
	enum oplus_chg_vooc_switch_mode switch_mode;

	struct work_struct data_handler_work;
};

static int oplus_va_virq_register(struct oplus_virtual_asic_ic *chip);

static int oplus_chg_va_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	struct device_node *node;
	bool retry = false;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	node = va->dev->of_node;

	ic_dev->online = true;
	for (i = 0; i < va->asic_num; i++) {
		if (va->asic_list[i].initialized)
			continue;
		va->asic_list[i].ic_dev =
			of_get_oplus_chg_ic(node, "oplus,asic_ic", i);
		if (va->asic_list[i].ic_dev == NULL) {
			chg_debug("asic[%d] not found\n", i);
			retry = true;
			continue;
		}
		va->asic_list[i].ic_dev->parent = ic_dev;
		rc = oplus_chg_ic_func(va->asic_list[i].ic_dev, OPLUS_IC_FUNC_INIT);
		va->asic_list[i].initialized = true;
		if (rc >= 0) {
			chg_info("asic(=%s) init success\n",
				 va->asic_list[i].ic_dev->name);
			va->asic = va->asic_list[i].ic_dev;
			va->vooc_ic_type = rc;
			goto init_done;
		} else {
			chg_err("asic(=%s) init error, rc=%d\n",
				va->asic_list[i].ic_dev->name, rc);
		}
	}

	if (retry) {
		return -EAGAIN;
	} else {
		chg_err("all asic init error\n");
		return -EINVAL;
	}

init_done:
	oplus_va_virq_register(va);
	return rc;
}

static int oplus_chg_va_exit(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;

	va = oplus_chg_ic_get_drvdata(ic_dev);
	ic_dev->online = false;

	oplus_chg_ic_virq_release(va->asic, OPLUS_IC_VIRQ_ERR, va);
	oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_EXIT);
	ic_dev->parent = NULL;

	return 0;
}

static int oplus_chg_va_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_REG_DUMP);
	if (rc < 0)
		chg_err("reg dump error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_va_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_SMT_TEST, buf, len);
	if (rc < 0) {
		if (rc != -ENOTSUPP) {
			chg_err("smt test error, rc=%d\n", rc);
			rc = snprintf(buf, len, "[%s]-[%s]:%d\n",
				      va->asic->manu_name, "FUNC_ERR", rc);
		} else {
			rc = 0;
		}
	} else {
		if ((rc > 0) && buf[rc - 1] != '\n')
			buf[rc] = '\n';
	}

	return rc;
}

static int oplus_chg_va_fw_upgrade(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_VOOC_FW_UPGRADE);
	if (rc < 0)
		chg_err("upgrade fw error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_va_user_fw_upgrade(struct oplus_chg_ic_dev *ic_dev,
					const u8 *fw_buf, u32 fw_size)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE,
			       fw_buf, fw_size);
	if (rc < 0)
		chg_err("user upgrade fw error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_va_fw_check_then_recover(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(va->asic,
			       OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER);
	if (rc != FW_CHECK_MODE)
		chg_err("fw_check_then_recover error, rc=%d\n", rc);

	return rc;
}

static int
oplus_chg_va_fw_check_then_recover_fix(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(va->asic,
			       OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX);
	if (rc < 0)
		chg_err("fw check error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_va_get_fw_version(struct oplus_chg_ic_dev *ic_dev,
				       u32 *version)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_VOOC_GET_FW_VERSION,
			       version);
	if (rc < 0)
		chg_err("get fw version error, rc=%d\n", rc);

	return rc;
}

static bool oplus_chg_va_is_boot_by_gpio(struct oplus_virtual_asic_ic *va)
{
	bool boot_by_gpio;
	int rc;

	if (va->asic == NULL) {
		chg_err("no active asic found");
		return false;
	}
	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_VOOC_BOOT_BY_GPIO,
			       &boot_by_gpio);
	if (rc < 0) {
		chg_err("get boot_by_gpio status error, rc=%d\n", rc);
		return false;
	}

	return boot_by_gpio;
}

static bool oplus_chg_va_is_upgrading(struct oplus_virtual_asic_ic *va)
{
	bool upgrading;
	int rc;

	if (va->asic == NULL) {
		chg_err("no active asic found");
		return false;
	}
	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_VOOC_UPGRADING,
			       &upgrading);
	if (rc < 0) {
		chg_err("get upgrade status error, rc=%d\n", rc);
		return false;
	}

	return upgrading;
}

static int oplus_chg_va_set_data_active(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (IS_ERR_OR_NULL(va->gpio.pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	gpio_direction_input(va->gpio.data_gpio);/* TODO */
	rc = pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_data_active);
	if (rc < 0)
		chg_err("set asic_data_active error, rc=%d\n", rc);
	mutex_unlock(&va->gpio.pinctrl_mutex);

	return rc;
}

static int oplus_chg_va_set_data_sleep(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (IS_ERR_OR_NULL(va->gpio.pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	rc = pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_data_sleep);
	gpio_direction_output(va->gpio.data_gpio, 0);/* TODO */
	if (rc < 0)
		chg_err("set asic_data_sleep error, rc=%d\n", rc);
	mutex_unlock(&va->gpio.pinctrl_mutex);

	return rc;
}

static int oplus_chg_va_set_clock_active(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (IS_ERR_OR_NULL(va->gpio.pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	if (oplus_chg_va_is_boot_by_gpio(va)) {
		chg_debug("boot_by_gpio, return\n");
		return 0;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	rc = pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_clock_active);
	gpio_direction_output(va->gpio.clock_gpio, 0);/* TODO */
	if (rc < 0)
		chg_err("set asic_clock_active error, rc=%d\n", rc);
	mutex_unlock(&va->gpio.pinctrl_mutex);

	return rc;
}

static int oplus_chg_va_set_clock_sleep(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (IS_ERR_OR_NULL(va->gpio.pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	if (oplus_chg_va_is_boot_by_gpio(va)) {
		chg_debug("boot_by_gpio, return\n");
		return 0;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	rc = pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_clock_sleep);
	gpio_direction_output(va->gpio.clock_gpio, 1);/* TODO */
	if (rc < 0)
		chg_err("set asic_clock_sleep error, rc=%d\n", rc);
	mutex_unlock(&va->gpio.pinctrl_mutex);

	return rc;
}

static int oplus_chg_va_reset_sleep(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	if (oplus_chg_va_is_upgrading(va)) {
		chg_debug("asic upgrade\n");
		return 0;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	gpio_direction_output(va->gpio.asic_switch1_gpio, 0);
	gpio_direction_output(va->gpio.reset_gpio, 0);
	usleep_range(10000, 10000);
#ifdef CONFIG_OPLUS_CHARGER_MTK
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_reset_sleep);
#else
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_reset_active);
#endif
	gpio_set_value(va->gpio.reset_gpio, 1);
	usleep_range(10000, 10000);
	gpio_direction_output(va->gpio.reset_gpio, 0);
	usleep_range(1000, 1000);
	mutex_unlock(&va->gpio.pinctrl_mutex);

	chg_info("asic reset sleep\n");
	return 0;
}

static int oplus_chg_va_reset_active_force(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int active_level = 1;
	int sleep_level = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	if (va->vooc_ic_type == OPLUS_VOOC_IC_RK826 && oplus_is_rf_ftm_mode()) {
		chg_debug("rk826, rf or ftm mode\n");
		return 0;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	gpio_direction_output(va->gpio.asic_switch1_gpio, 0);
	gpio_direction_output(va->gpio.reset_gpio, active_level);
#ifdef CONFIG_OPLUS_CHARGER_MTK
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_reset_sleep);
#else
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_reset_active);
#endif
	gpio_set_value(va->gpio.reset_gpio, active_level);
	usleep_range(10000, 10000);
	gpio_set_value(va->gpio.reset_gpio, sleep_level);
	usleep_range(10000, 10000);
	gpio_set_value(va->gpio.reset_gpio, active_level);
	usleep_range(2500, 2500);
	if (va->switch_mode == VOOC_SWITCH_MODE_VOOC)
		gpio_direction_output(va->gpio.asic_switch1_gpio, 1);
	mutex_unlock(&va->gpio.pinctrl_mutex);

	chg_info("asic reset active\n");
	return 0;
}

static int oplus_chg_va_reset_active(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	if (oplus_chg_va_is_upgrading(va)) {
		chg_debug("asic upgrade\n");
		return 0;
	}
	rc = oplus_chg_va_reset_active_force(va->ic_dev);

	return rc;
}

static irqreturn_t irq_rx_handler(int irq, void *dev_id)
{
	struct oplus_virtual_asic_ic *va = dev_id;

	if (va->ic_dev->ic_virtual_enable) {
		chg_info("%s virtual function enable, ignore interrupt\n", va->ic_dev->name);
		return IRQ_HANDLED;
	}

	schedule_work(&va->data_handler_work);
	return IRQ_HANDLED;
}

static void oplus_va_data_handler_work(struct work_struct *work)
{
	struct oplus_virtual_asic_ic *va = container_of(
		work, struct oplus_virtual_asic_ic, data_handler_work);

	usleep_range(2000, 2001);
	if (gpio_get_value(va->gpio.data_gpio) != 1)
		return;
	oplus_chg_ic_virq_trigger(va->ic_dev, OPLUS_IC_VIRQ_VOOC_DATA);
}

static int oplus_chg_va_eint_register(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	static int register_status = 0;
	struct device_node *node = NULL;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	node = of_find_compatible_node(NULL, NULL,
				       "mediatek, VOOC_EINT_NEW_FUNCTION");
	if (node) {
		oplus_chg_va_set_data_active(va->ic_dev);
		rc = request_irq(va->gpio.data_irq,
				 (irq_handler_t)irq_rx_handler,
				 IRQF_TRIGGER_RISING, "VOOC_AP_DATA-eint", va);
		if (rc < 0) {
			chg_err("rc = %d, oplus_vooc_eint_register failed to request_irq \n",
				rc);
		}
	} else {
		if (!register_status) {
			oplus_chg_va_set_data_active(va->ic_dev);
			rc = request_irq(va->gpio.data_irq,
					 (irq_handler_t)irq_rx_handler,
					 IRQF_TRIGGER_RISING,
					 "VOOC_AP_DATA-eint", va);
			if (rc) {
				chg_err("rc = %d, oplus_vooc_eint_register failed to request_irq \n",
					rc);
			}
			register_status = 1;
		} else {
			chg_debug("enable_irq!\r\n");
			enable_irq(va->gpio.data_irq);
		}
	}
#else
	oplus_chg_va_set_data_active(va->ic_dev);
	rc = request_irq(va->gpio.data_irq, irq_rx_handler, IRQF_TRIGGER_RISING,
			 "asic_data", va);
	if (rc < 0) {
		chg_err("request ap rx irq failed, rc=%d\n", rc);
	}
#endif

	return rc;
}

static int oplus_chg_va_eint_unregister(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_asic_ic *va;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	struct device_node *node = NULL;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
#ifdef CONFIG_OPLUS_CHARGER_MTK
	node = of_find_compatible_node(NULL, NULL,
				       "mediatek, VOOC_EINT_NEW_FUNCTION");
	chg_debug("disable_irq_mtk!\r\n");
	if (node) {
		free_irq(va->gpio.data_irq, va);
	} else {
		disable_irq(va->gpio.data_irq);
	}
#else
	free_irq(va->gpio.data_irq, va);
#endif

	return 0;
}

static int oplus_chg_va_get_reset_gpio_val(struct oplus_chg_ic_dev *ic_dev,
					   int *val)
{
	struct oplus_virtual_asic_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	*val = gpio_get_value(va->gpio.reset_gpio);

	return 0;
}

static int oplus_chg_va_get_clock_gpio_val(struct oplus_chg_ic_dev *ic_dev,
					   int *val)
{
	struct oplus_virtual_asic_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	*val = gpio_get_value(va->gpio.clock_gpio);

	return 0;
}

static int oplus_chg_va_get_data_gpio_val(struct oplus_chg_ic_dev *ic_dev,
					  int *val)
{
	struct oplus_virtual_asic_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	*val = gpio_get_value(va->gpio.data_gpio);

	return 0;
}

#define DATA_SHIFT_BIT	6
static int oplus_chg_va_get_data(struct oplus_chg_ic_dev *ic_dev, int *data)
{
	struct oplus_virtual_asic_ic *va;
	int i;
	int bit;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	for (i = 0; i < 7; i++) {
		rc = oplus_chg_va_set_clock_active(va->ic_dev);
		if (rc < 0) {
			chg_err("set clock active error\n");
			return -ENODEV;
		}
		usleep_range(1000, 1000);
		rc = oplus_chg_va_set_clock_sleep(va->ic_dev);
		if (rc < 0) {
			chg_err("set clock sleep error\n");
			return -ENODEV;
		}
		usleep_range(19000, 19000);
		bit = gpio_get_value(va->gpio.data_gpio);
		*data |= bit << (DATA_SHIFT_BIT - i);
	}

	return 0;
}

static int oplus_chg_va_reply_data(struct oplus_chg_ic_dev *ic_dev, int data,
				   int data_width, int curr_ma)
{
	struct oplus_virtual_asic_ic *va;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	chg_info("reply mcu: data=0x%x, width=%d\n", data, data_width);
	for (i = 0; i < data_width; i++) {
		gpio_set_value(va->gpio.data_gpio,
			       (data >> (data_width - i - 1)) & 0x01);
		chg_info("send_bit[%d] = %d\n", i, (data >> (data_width - i - 1)) & 0x01);
		rc = oplus_chg_va_set_clock_active(va->ic_dev);
		if (rc < 0) {
			chg_err("set clock active error\n");
			return -ENODEV;
		}
		usleep_range(1000, 1000);
		rc = oplus_chg_va_set_clock_sleep(va->ic_dev);
		if (rc < 0) {
			chg_err("set clock sleep error\n");
			return -ENODEV;
		}
		usleep_range(19000, 19000);
	}

	return 0;
}

static int oplus_chg_va_set_switch_mode(struct oplus_chg_ic_dev *ic_dev,
					int mode)
{
	struct oplus_virtual_asic_ic *va;
	int retry = 10;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (IS_ERR_OR_NULL(va->gpio.pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	if ((mode == VOOC_SWITCH_MODE_VOOC) && oplus_chg_va_is_upgrading(va)) {
		chg_err("firmware upgrading, can't switch to vooc mode\n");
		return -EINVAL;
	}

	if (oplus_gauge_afi_update_done() == false) {
		chg_err("zy gauge afi_update_done ing...\n");
		return -EINVAL;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	switch (mode) {
	case VOOC_SWITCH_MODE_VOOC:
		do {
			if (gpio_get_value(va->gpio.reset_gpio) == 1) {
				rc = pinctrl_select_state(
					va->gpio.pinctrl,
					va->gpio.asic_switch_vooc);
				if (rc < 0) {
					chg_err("set asic_switch_vooc error, rc=%d\n",
						rc);
					goto err;
				}
				rc = pinctrl_select_state(
					va->gpio.pinctrl,
					va->gpio.gpio_switch_ctrl_asic);
				if (rc < 0) {
					chg_err("set gpio_switch_ctrl_asic error, rc=%d\n",
						rc);
					goto err;
				}
				chg_info("switch to vooc mode\n");
				break;
			}
			usleep_range(5000, 5000);
		} while (--retry > 0);
		break;
	case VOOC_SWITCH_MODE_HEADPHONE:
		chg_info("switch to headphone mode\n");
		break;
	case VOOC_SWITCH_MODE_NORMAL:
	default:
		rc = pinctrl_select_state(va->gpio.pinctrl,
					  va->gpio.asic_switch_normal);
		if (rc < 0) {
			chg_err("set asic_switch_normal error, rc=%d\n", rc);
			goto err;
		}
		rc = pinctrl_select_state(va->gpio.pinctrl,
					  va->gpio.gpio_switch_ctrl_ap);
		if (rc < 0) {
			chg_err("set gpio_switch_ctrl_ap error, rc=%d\n", rc);
			goto err;
		}
		chg_info("switch to normal mode\n");
		break;
	}
	va->switch_mode = mode;
	mutex_unlock(&va->gpio.pinctrl_mutex);
	return 0;

err:
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.asic_switch_normal);
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.gpio_switch_ctrl_ap);
	mutex_unlock(&va->gpio.pinctrl_mutex);
	return rc;
}

static int oplus_chg_va_get_switch_mode(struct oplus_chg_ic_dev *ic_dev,
					int *mode)
{
	struct oplus_virtual_asic_ic *va;
	int switch1_val, switch2_val;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	if (!gpio_is_valid(va->gpio.asic_switch2_gpio))
		switch2_val = 1;
	else
		switch2_val = gpio_get_value(va->gpio.asic_switch2_gpio);
	switch1_val = gpio_get_value(va->gpio.asic_switch1_gpio);

	if (switch1_val == 0 && switch2_val == 1)
		*mode = VOOC_SWITCH_MODE_NORMAL;
	else if (switch1_val == 1 && switch2_val == 1)
		*mode = VOOC_SWITCH_MODE_VOOC;
	else if (switch1_val == 1 && switch2_val == 0)
		*mode = VOOC_SWITCH_MODE_HEADPHONE;
	else
		return -EINVAL;

	return 0;
}

static int oplus_chg_va_get_fw_status(struct oplus_chg_ic_dev *ic_dev,
				      bool *pass)
{
	struct oplus_virtual_asic_ic *va;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	if (va->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(va->asic, OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS, pass);
	if (rc < 0) {
		chg_err("get firmware status error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int oplus_chg_va_get_asic(struct oplus_chg_ic_dev *ic_dev,
				 struct oplus_chg_ic_dev **asic)
{
	struct oplus_virtual_asic_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	*asic = va->asic;

	return 0;
}

static void *oplus_chg_va_get_func(struct oplus_chg_ic_dev *ic_dev,
				   enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_chg_va_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_chg_va_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_chg_va_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       oplus_chg_va_smt_test);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_UPGRADE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_FW_UPGRADE,
					       oplus_chg_va_fw_upgrade);
		break;
	case OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE,
			oplus_chg_va_user_fw_upgrade);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER,
			oplus_chg_va_fw_check_then_recover);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX,
			oplus_chg_va_fw_check_then_recover_fix);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_FW_VERSION:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_FW_VERSION,
			oplus_chg_va_get_fw_version);
		break;
	case OPLUS_IC_FUNC_VOOC_SET_DATA_ACTIVE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_SET_DATA_ACTIVE,
			oplus_chg_va_set_data_active);
		break;
	case OPLUS_IC_FUNC_VOOC_SET_DATA_SLEEP:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_SET_DATA_SLEEP,
			oplus_chg_va_set_data_sleep);
		break;
	case OPLUS_IC_FUNC_VOOC_SET_CLOCK_ACTIVE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_SET_CLOCK_ACTIVE,
			oplus_chg_va_set_clock_active);
		break;
	case OPLUS_IC_FUNC_VOOC_SET_CLOCK_SLEEP:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_SET_CLOCK_SLEEP,
			oplus_chg_va_set_clock_sleep);
		break;
	case OPLUS_IC_FUNC_VOOC_RESET_SLEEP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_RESET_SLEEP,
					       oplus_chg_va_reset_sleep);
		break;
	case OPLUS_IC_FUNC_VOOC_RESET_ACTIVE_FORCE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_RESET_ACTIVE_FORCE,
			oplus_chg_va_reset_active_force);
		break;
	case OPLUS_IC_FUNC_VOOC_RESET_ACTIVE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_RESET_ACTIVE,
					       oplus_chg_va_reset_active);
		break;
	case OPLUS_IC_FUNC_VOOC_EINT_REGISTER:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_EINT_REGISTER,
					       oplus_chg_va_eint_register);
		break;
	case OPLUS_IC_FUNC_VOOC_EINT_UNREGISTER:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_EINT_UNREGISTER,
			oplus_chg_va_eint_unregister);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_RESET_GPIO_VAL:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_RESET_GPIO_VAL,
			oplus_chg_va_get_reset_gpio_val);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_CLOCK_GPIO_VAL:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_CLOCK_GPIO_VAL,
			oplus_chg_va_get_clock_gpio_val);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_DATA_GPIO_VAL:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_DATA_GPIO_VAL,
			oplus_chg_va_get_data_gpio_val);
		break;
	case OPLUS_IC_FUNC_VOOC_READ_DATA:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_READ_DATA,
					       oplus_chg_va_get_data);
		break;
	case OPLUS_IC_FUNC_VOOC_REPLY_DATA:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_REPLY_DATA,
					       oplus_chg_va_reply_data);
		break;
	case OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE,
			oplus_chg_va_set_switch_mode);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_SWITCH_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_SWITCH_MODE,
			oplus_chg_va_get_switch_mode);
		break;
	case OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS,
					       oplus_chg_va_get_fw_status);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_IC_DEV:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_GET_IC_DEV,
					       oplus_chg_va_get_asic);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static int oplus_va_child_init(struct oplus_virtual_asic_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	rc = of_property_count_elems_of_size(node, "oplus,asic_ic",
					     sizeof(u32));
	if (rc < 0) {
		chg_err("can't get asic number, rc=%d\n", rc);
		return rc;
	}
	chip->asic_num = rc;
	chip->asic_list = devm_kzalloc(chip->dev,
				       sizeof(struct oplus_virtual_asic_child) *
					       chip->asic_num,
				       GFP_KERNEL);
	if (chip->asic_list == NULL) {
		rc = -ENOMEM;
		chg_err("alloc asic table memory error\n");
		return rc;
	}

	return 0;
}

static void oplus_va_asic_data_irq_init(struct oplus_virtual_asic_ic *chip)
{
	struct oplus_virtual_asic_gpio *asic_gpio = &chip->gpio;

#ifdef CONFIG_OPLUS_CHARGER_MTK
	struct device_node *node = NULL;
	struct device_node *node_new = NULL;
	u32 intr[2] = { 0, 0 };

	node = of_find_compatible_node(NULL, NULL,
				       "mediatek, VOOC_AP_DATA-eint");
	node_new = of_find_compatible_node(NULL, NULL,
					   "mediatek, VOOC_EINT_NEW_FUNCTION");
	if (node) {
		if (node_new) {
			asic_gpio->data_irq = gpio_to_irq(asic_gpio->data_gpio);
			chg_err("vooc_gpio.data_irq:%d\n", asic_gpio->data_irq);
		} else {
			of_property_read_u32_array(node, "interrupts", intr,
						   ARRAY_SIZE(intr));
			chg_debug(" intr[0]  = %d, intr[1]  = %d\r\n", intr[0],
				  intr[1]);
			asic_gpio->data_irq = irq_of_parse_and_map(node, 0);
		}
	} else {
		chg_err(" node not exist!\r\n");
		asic_gpio->data_irq = gpio_to_irq(asic_gpio->data_gpio);
	}
#else
	asic_gpio->data_irq = gpio_to_irq(asic_gpio->data_gpio);
#endif
}

static int oplus_va_gpio_init(struct oplus_virtual_asic_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct oplus_virtual_asic_gpio *asic_gpio = &chip->gpio;
	int rc = 0;

	asic_gpio->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(asic_gpio->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	asic_gpio->switch_ctrl_gpio =
		of_get_named_gpio(node, "oplus,switch_ctrl-gpio", 0);
	if (!gpio_is_valid(asic_gpio->switch_ctrl_gpio)) {
		chg_err("Couldn't read oplus,switch_ctrl-gpio, %d\n",
			asic_gpio->switch_ctrl_gpio);
		return -ENODEV;
	}
	rc = gpio_request(asic_gpio->switch_ctrl_gpio, "switch_ctrl-gpio");
	if (rc != 0) {
		chg_err("unable to request switch_ctrl-gpio:%d\n",
			asic_gpio->switch_ctrl_gpio);
		return rc;
	}
	asic_gpio->gpio_switch_ctrl_ap =
		pinctrl_lookup_state(asic_gpio->pinctrl, "switch_ctrl_ap");
	if (IS_ERR_OR_NULL(asic_gpio->gpio_switch_ctrl_ap)) {
		chg_err("get switch_ctrl_ap fail\n");
		rc = -EINVAL;
		goto free_switch_ctrl_gpio;
	}
	asic_gpio->gpio_switch_ctrl_asic =
		pinctrl_lookup_state(asic_gpio->pinctrl, "switch_ctrl_asic");
	if (IS_ERR_OR_NULL(asic_gpio->gpio_switch_ctrl_asic)) {
		chg_err("get switch_ctrl_asic fail\n");
		rc = -EINVAL;
		goto free_switch_ctrl_gpio;
	}
	rc = pinctrl_select_state(asic_gpio->pinctrl,
				  asic_gpio->gpio_switch_ctrl_ap);
	if (rc < 0) {
		chg_err("set gpio switch_ctrl_ap error, rc=%d\n", rc);
		goto free_switch_ctrl_gpio;
	}

	asic_gpio->asic_switch1_gpio =
		of_get_named_gpio(node, "oplus,asic_switch1-gpio", 0);
	if (!gpio_is_valid(asic_gpio->asic_switch1_gpio)) {
		chg_err("Couldn't read oplus,asic_switch1-gpio, %d\n",
			asic_gpio->asic_switch1_gpio);
		rc = -ENODEV;
		goto free_switch_ctrl_gpio;
	}
	rc = gpio_request(asic_gpio->asic_switch1_gpio, "asic_switch1-gpio");
	if (rc != 0) {
		chg_err("unable to request asic_switch1-gpio:%d\n",
			asic_gpio->asic_switch1_gpio);
		goto free_switch_ctrl_gpio;
	}
	asic_gpio->asic_switch2_gpio =
		of_get_named_gpio(node, "oplus,asic_switch2-gpio", 0);
	if (!gpio_is_valid(asic_gpio->asic_switch2_gpio)) {
		chg_info("Couldn't read oplus,asic_switch2-gpio, %d\n",
			 asic_gpio->asic_switch2_gpio);
	} else {
		rc = gpio_request(asic_gpio->asic_switch2_gpio,
				  "asic_switch2-gpio");
		if (rc != 0) {
			chg_err("unable to request asic_switch2-gpio:%d\n",
				asic_gpio->asic_switch2_gpio);
			goto free_asic_switch1_gpio;
		}
	}
	asic_gpio->asic_switch_normal =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_switch_normal");
	if (IS_ERR_OR_NULL(asic_gpio->asic_switch_normal)) {
		chg_err("get asic_switch_normal fail\n");
		rc = -EINVAL;
		goto free_asic_switch2_gpio;
	}
	asic_gpio->asic_switch_vooc =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_switch_vooc");
	if (IS_ERR_OR_NULL(asic_gpio->asic_switch_vooc)) {
		chg_err("get asic_switch_vooc fail\n");
		rc = -EINVAL;
		goto free_asic_switch2_gpio;
	}
	rc = pinctrl_select_state(asic_gpio->pinctrl,
				  asic_gpio->asic_switch_normal);
	if (rc < 0) {
		chg_err("set gpio asic_switch_normal error, rc=%d\n", rc);
		goto free_asic_switch2_gpio;
	}

	asic_gpio->reset_gpio =
		of_get_named_gpio(node, "oplus,asic_reset-gpio", 0);
	if (!gpio_is_valid(asic_gpio->reset_gpio)) {
		chg_err("Couldn't read oplus,asic_reset-gpio, %d\n",
			asic_gpio->reset_gpio);
		rc = -ENODEV;
		goto free_asic_switch2_gpio;
	}
	rc = gpio_request(asic_gpio->reset_gpio, "asic_reset-gpio");
	if (rc != 0) {
		chg_err("unable to request asic_reset-gpio:%d\n",
			asic_gpio->reset_gpio);
		goto free_asic_switch2_gpio;
	}
	asic_gpio->asic_reset_active =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_reset_active");
	if (IS_ERR_OR_NULL(asic_gpio->asic_reset_active)) {
		chg_err("get asic_reset_active fail\n");
		rc = -EINVAL;
		goto free_reset_gpio;
	}
	asic_gpio->asic_reset_sleep =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_reset_sleep");
	if (IS_ERR_OR_NULL(asic_gpio->asic_reset_sleep)) {
		chg_err("get asic_reset_sleep fail\n");
		rc = -EINVAL;
		goto free_reset_gpio;
	}
	rc = pinctrl_select_state(asic_gpio->pinctrl,
				  asic_gpio->asic_reset_sleep);
	if (rc < 0) {
		chg_err("set gpio asic_reset_sleep error, rc=%d\n", rc);
		goto free_reset_gpio;
	}

	asic_gpio->clock_gpio =
		of_get_named_gpio(node, "oplus,asic_clock-gpio", 0);
	if (!gpio_is_valid(asic_gpio->clock_gpio)) {
		chg_err("Couldn't read oplus,asic_clock-gpio, %d\n",
			asic_gpio->clock_gpio);
		rc = -ENODEV;
		goto free_reset_gpio;
	}
	rc = gpio_request(asic_gpio->clock_gpio, "asic_clock-gpio");
	if (rc != 0) {
		chg_err("unable to request asic_reset-gpio:%d\n",
			asic_gpio->clock_gpio);
		goto free_reset_gpio;
	}
	asic_gpio->asic_clock_active =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_clock_active");
	if (IS_ERR_OR_NULL(asic_gpio->asic_clock_active)) {
		chg_err("get asic_clock_active fail\n");
		rc = -EINVAL;
		goto free_clock_gpio;
	}
	asic_gpio->asic_clock_sleep =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_clock_sleep");
	if (IS_ERR_OR_NULL(asic_gpio->asic_clock_sleep)) {
		chg_err("get asic_clock_sleep fail\n");
		rc = -EINVAL;
		goto free_clock_gpio;
	}
	rc = pinctrl_select_state(asic_gpio->pinctrl,
				  asic_gpio->asic_clock_sleep);
	if (rc < 0) {
		chg_err("set gpio asic_clock_sleep error, rc=%d\n", rc);
		goto free_clock_gpio;
	}

	asic_gpio->data_gpio =
		of_get_named_gpio(node, "oplus,asic_data-gpio", 0);
	if (!gpio_is_valid(asic_gpio->data_gpio)) {
		chg_err("Couldn't read oplus,asic_data-gpio, %d\n",
			asic_gpio->data_gpio);
		rc = -ENODEV;
		goto free_clock_gpio;
	}
	rc = gpio_request(asic_gpio->data_gpio, "asic_data-gpio");
	if (rc != 0) {
		chg_err("unable to request asic_reset-gpio:%d\n",
			asic_gpio->data_gpio);
		goto free_clock_gpio;
	}
	asic_gpio->asic_data_active =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_data_active");
	if (IS_ERR_OR_NULL(asic_gpio->asic_data_active)) {
		chg_err("get asic_data_active fail\n");
		rc = -EINVAL;
		goto free_data_gpio;
	}
	asic_gpio->asic_data_sleep =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_data_sleep");
	if (IS_ERR_OR_NULL(asic_gpio->asic_data_sleep)) {
		chg_err("get asic_data_sleep fail\n");
		rc = -EINVAL;
		goto free_data_gpio;
	}
	rc = pinctrl_select_state(asic_gpio->pinctrl,
				  asic_gpio->asic_data_sleep);
	if (rc < 0) {
		chg_err("set gpio asic_data_sleep error, rc=%d\n", rc);
		goto free_data_gpio;
	}
	oplus_va_asic_data_irq_init(chip);

	mutex_init(&asic_gpio->pinctrl_mutex);

	return 0;

free_data_gpio:
	if (gpio_is_valid(asic_gpio->data_gpio))
		gpio_free(asic_gpio->data_gpio);
free_clock_gpio:
	if (gpio_is_valid(asic_gpio->clock_gpio))
		gpio_free(asic_gpio->clock_gpio);
free_reset_gpio:
	if (gpio_is_valid(asic_gpio->reset_gpio))
		gpio_free(asic_gpio->reset_gpio);
free_asic_switch2_gpio:
	if (gpio_is_valid(asic_gpio->asic_switch2_gpio))
		gpio_free(asic_gpio->asic_switch2_gpio);
free_asic_switch1_gpio:
	if (gpio_is_valid(asic_gpio->asic_switch1_gpio))
		gpio_free(asic_gpio->asic_switch1_gpio);
free_switch_ctrl_gpio:
	if (gpio_is_valid(asic_gpio->switch_ctrl_gpio))
		gpio_free(asic_gpio->switch_ctrl_gpio);

	return rc;
}

static int oplus_va_asic_id_check(struct oplus_virtual_asic_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct oplus_virtual_asic_gpio *asic_gpio = &chip->gpio;
	int rc = 0;

	rc = of_property_read_u32(node, "oplus,asic_id", &chip->vooc_ic_type);
	if (rc == 0) {
		chg_info("the id is %d specified by dts.", chip->vooc_ic_type);
		return 0;
	}

	asic_gpio->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(asic_gpio->pinctrl)) {
		chg_err("get pinctrl fail\n");
		rc = -ENODEV;
		goto err;
	}

	asic_gpio->asic_id_gpio =
		of_get_named_gpio(node, "oplus,asic_id-gpio", 0);
	if (!gpio_is_valid(asic_gpio->asic_id_gpio)) {
		chg_debug("Couldn't read oplus,asic_id-gpio, %d\n",
			  asic_gpio->asic_id_gpio);
		rc = 0;
		goto err;
	}
	rc = gpio_request(asic_gpio->asic_id_gpio, "asic_id-gpio");
	if (rc != 0) {
		chg_err("unable to request asic_id-gpio:%d\n",
			asic_gpio->asic_id_gpio);
		goto err;
	}
	asic_gpio->asic_id_active =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_id_active");
	if (IS_ERR_OR_NULL(asic_gpio->asic_id_active)) {
		chg_err("get asic_id_active fail\n");
		rc = -EINVAL;
		goto free_gpio;
	}
	asic_gpio->asic_id_sleep =
		pinctrl_lookup_state(asic_gpio->pinctrl, "asic_id_sleep");
	if (IS_ERR_OR_NULL(asic_gpio->asic_id_sleep)) {
		chg_err("get asic_id_sleep fail\n");
		rc = -EINVAL;
		goto free_gpio;
	}

	rc = pinctrl_select_state(asic_gpio->pinctrl, asic_gpio->asic_id_sleep);
	if (rc < 0) {
		chg_err("set gpio asic_id_sleep error, rc=%d\n", rc);
		goto free_gpio;
	}
	usleep_range(10000, 10000);
	if (gpio_get_value(asic_gpio->asic_id_gpio) == 1) {
		chg_debug("it is rk826\n");
		chip->vooc_ic_type = OPLUS_VOOC_IC_RK826;
		return 0;
	}
	rc = pinctrl_select_state(asic_gpio->pinctrl,
				  asic_gpio->asic_id_active);
	if (rc < 0) {
		chg_err("set gpio asic_id_active error, rc=%d\n", rc);
		goto free_gpio;
	}
	usleep_range(10000, 10000);
	if (gpio_get_value(asic_gpio->asic_id_gpio) == 1) {
		chg_debug("it is rt5125\n");
		chip->vooc_ic_type = OPLUS_VOOC_IC_RT5125;
		return 0;
	}
	chg_debug("it is op10\n");
	chip->vooc_ic_type = OPLUS_VOOC_IC_OP10;
	return 0;

free_gpio:
	if (gpio_is_valid(asic_gpio->asic_id_gpio))
		gpio_free(asic_gpio->asic_id_gpio);
err:
	chip->vooc_ic_type = OPLUS_VOOC_IC_UNKNOWN;
	return rc;
}

static struct oplus_chg_ic_virq oplus_va_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_VOOC_DATA },
};

static void oplus_va_err_handler(struct oplus_chg_ic_dev *ic_dev, void *virq_data)
{
	struct oplus_virtual_asic_ic *chip = virq_data;

	oplus_chg_ic_move_err_msg(chip->ic_dev, ic_dev);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static int oplus_va_virq_register(struct oplus_virtual_asic_ic *chip)
{
	int rc;

	if (chip->asic == NULL) {
		chg_err("no active asic found");
		return -ENODEV;
	}

	rc = oplus_chg_ic_virq_register(chip->asic,
		OPLUS_IC_VIRQ_ERR, oplus_va_err_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d", rc);

	return 0;
}

static int oplus_virtual_asic_probe(struct platform_device *pdev)
{
	struct oplus_virtual_asic_ic *chip;
	struct device_node *node = pdev->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_virtual_asic_ic),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	INIT_WORK(&chip->data_handler_work, oplus_va_data_handler_work);

	rc = oplus_va_child_init(chip);
	if (rc < 0) {
		chg_err("child list init error, rc=%d\n", rc);
		goto child_init_err;
	}
	rc = oplus_va_gpio_init(chip);
	if (rc < 0) {
		chg_err("gpio init error, rc=%d\n", rc);
		goto gpio_init_err;
	}
	(void)oplus_va_asic_id_check(chip);

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto reg_ic_err;
	}
	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto reg_ic_err;
	}
	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	sprintf(ic_cfg.manu_name, "virtual asic");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_va_get_func;
	ic_cfg.virq_data = oplus_va_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(oplus_va_virq_table);
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}

	chg_err("probe success\n");
	return 0;

reg_ic_err:
	if (gpio_is_valid(chip->gpio.asic_id_gpio))
		gpio_free(chip->gpio.asic_id_gpio);
	if (gpio_is_valid(chip->gpio.data_gpio))
		gpio_free(chip->gpio.data_gpio);
	if (gpio_is_valid(chip->gpio.clock_gpio))
		gpio_free(chip->gpio.clock_gpio);
	if (gpio_is_valid(chip->gpio.reset_gpio))
		gpio_free(chip->gpio.reset_gpio);
	if (gpio_is_valid(chip->gpio.asic_switch2_gpio))
		gpio_free(chip->gpio.asic_switch2_gpio);
	if (gpio_is_valid(chip->gpio.asic_switch1_gpio))
		gpio_free(chip->gpio.asic_switch1_gpio);
	if (gpio_is_valid(chip->gpio.switch_ctrl_gpio))
		gpio_free(chip->gpio.switch_ctrl_gpio);
gpio_init_err:
	devm_kfree(&pdev->dev, chip->asic_list);
child_init_err:
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	chg_err("probe error\n");
	return rc;
}

static int oplus_virtual_asic_remove(struct platform_device *pdev)
{
	struct oplus_virtual_asic_ic *chip = platform_get_drvdata(pdev);

	if (chip == NULL)
		return -ENODEV;

	if (chip->ic_dev->online)
		oplus_chg_va_exit(chip->ic_dev);
	devm_oplus_chg_ic_unregister(&pdev->dev, chip->ic_dev);
	if (gpio_is_valid(chip->gpio.asic_id_gpio))
		gpio_free(chip->gpio.asic_id_gpio);
	if (gpio_is_valid(chip->gpio.data_gpio))
		gpio_free(chip->gpio.data_gpio);
	if (gpio_is_valid(chip->gpio.clock_gpio))
		gpio_free(chip->gpio.clock_gpio);
	if (gpio_is_valid(chip->gpio.reset_gpio))
		gpio_free(chip->gpio.reset_gpio);
	if (gpio_is_valid(chip->gpio.asic_switch2_gpio))
		gpio_free(chip->gpio.asic_switch2_gpio);
	if (gpio_is_valid(chip->gpio.asic_switch1_gpio))
		gpio_free(chip->gpio.asic_switch1_gpio);
	if (gpio_is_valid(chip->gpio.switch_ctrl_gpio))
		gpio_free(chip->gpio.switch_ctrl_gpio);
	devm_kfree(&pdev->dev, chip->asic_list);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void oplus_virtual_asic_shutdown(struct platform_device *pdev)
{
/* TODO
	struct oplus_virtual_asic_ic *chip = platform_get_drvdata(pdev);

	oplus_chg_va_set_switch_mode(chip->ic_dev, VOOC_SWITCH_MODE_NORMAL);
	msleep(10);
	if (oplus_vooc_get_fastchg_started() == true) {
		oplus_chg_va_set_clock_sleep(chip->ic_dev);
		msleep(10);
		oplus_chg_va_reset_active(chip->ic_dev);
	}
	msleep(80);
*/
}

static const struct of_device_id oplus_virtual_asic_match[] = {
	{ .compatible = "oplus,virtual_asic" },
	{},
};

static struct platform_driver oplus_virtual_asic_driver = {
	.driver		= {
		.name = "oplus-virtual_asic",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_virtual_asic_match),
	},
	.probe		= oplus_virtual_asic_probe,
	.remove		= oplus_virtual_asic_remove,
	.shutdown	= oplus_virtual_asic_shutdown,
};

static __init int oplus_virtual_asic_init(void)
{
	return platform_driver_register(&oplus_virtual_asic_driver);
}

static __exit void oplus_virtual_asic_exit(void)
{
	platform_driver_unregister(&oplus_virtual_asic_driver);
}

oplus_chg_module_register(oplus_virtual_asic);
