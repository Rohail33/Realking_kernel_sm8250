// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_chg_module.h"
#include "oplus_switching.h"

struct oplus_mos_dev {
	struct device			*dev;
	struct oplus_switch_chip 	* switching_chip;
	int                             mos_en_gpio;
	struct pinctrl			*mos_en_pinctrl;
	struct pinctrl_state		*mos_en_gpio_active;
	struct pinctrl_state		*mos_en_gpio_sleep;
};

struct oplus_mos_dev *g_mos_dev;

static int oplus_mos_en_gpio_init(struct oplus_mos_dev *dev)
{
	if (!dev) {
		chg_err("oplus_mos_dev null!\n");
		return -EINVAL;
	}

	dev->mos_en_pinctrl = devm_pinctrl_get(dev->dev);
	if (IS_ERR_OR_NULL(dev->mos_en_pinctrl)) {
		chg_err("get mos_en_pinctrl fail\n");
		return -EINVAL;
	}

	dev->mos_en_gpio_active =
		pinctrl_lookup_state(dev->mos_en_pinctrl, "mos_en_gpio_active");
	if (IS_ERR_OR_NULL(dev->mos_en_gpio_active)) {
		chg_err("get mos_en_gpio_active fail\n");
		return -EINVAL;
	}

	dev->mos_en_gpio_sleep =
		pinctrl_lookup_state(dev->mos_en_pinctrl, "mos_en_gpio_sleep");
	if (IS_ERR_OR_NULL(dev->mos_en_gpio_sleep)) {
		chg_err("get mos_en_gpio_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(dev->mos_en_pinctrl, dev->mos_en_gpio_sleep);

	return 0;
}

static int oplus_mos_ctrl_parse_dt(struct oplus_mos_dev *dev)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (!dev) {
		chg_err("oplus_mos_dev null!\n");
		return -1;
	}

	node = dev->dev->of_node;

	dev->mos_en_gpio = of_get_named_gpio(node, "oplus,mos-en-gpio", 0);
	if (dev->mos_en_gpio <= 0) {
		chg_err("Couldn't read oplus,mos-en-gpio\n");
	} else {
		if (gpio_is_valid(dev->mos_en_gpio)) {
			rc = gpio_request(dev->mos_en_gpio, "mos_en-gpio");
			if (rc) {
				chg_err("unable request mos-en-gpio:%d\n", dev->mos_en_gpio);
			} else {
				rc = oplus_mos_en_gpio_init(dev);
				if (rc)
					chg_err("unable init mos-en-gpio, rc = %d\n", rc);
				else
					chg_err("mos_en_gpio level[%d]\n", gpio_get_value(dev->mos_en_gpio));
			}
		}
		chg_err("mos_en-gpio:%d\n", dev->mos_en_gpio);
	}
	rc = of_property_read_u32(node, "qcom,parallel_vbat_gap_abnormal", &dev->switching_chip->parallel_vbat_gap_abnormal);
	if (rc) {
		dev->switching_chip->parallel_vbat_gap_abnormal = 150;
	}

	return 0;
}

static int oplus_mos_ctrl_set_enable(int enable)
{
	if (!g_mos_dev) {
		chg_err("g_mos_dev not specified!\n");
		return 0;
	}

	if (!g_mos_dev->mos_en_pinctrl
	    || !g_mos_dev->mos_en_gpio_active
	    || !g_mos_dev->mos_en_gpio_sleep) {
		chg_err("pinctrl or gpio_active or gpio_sleep not specified!\n");
		return 0;
	}

	if (enable)
		pinctrl_select_state(g_mos_dev->mos_en_pinctrl, g_mos_dev->mos_en_gpio_active);
	else
		pinctrl_select_state(g_mos_dev->mos_en_pinctrl, g_mos_dev->mos_en_gpio_sleep);

	chg_err("enable %d, level %d\n", enable, gpio_get_value(g_mos_dev->mos_en_gpio));

	return 0;
}

static int oplus_mos_ctrl_get_enable(void)
{
	if (!g_mos_dev) {
		chg_err("g_mos_dev not specified!\n");
		return 0;
	}

	if (!gpio_is_valid(g_mos_dev->mos_en_gpio)) {
		chg_err("mos_en_gpio not specified!\n");
		return 0;
	}

	return gpio_get_value(g_mos_dev->mos_en_gpio);
}

struct oplus_switch_operations oplus_mos_ctrl_ops = {
	.switching_hw_enable = oplus_mos_ctrl_set_enable,
	.switching_get_hw_enable = oplus_mos_ctrl_get_enable,
	.switching_set_fastcharge_current = NULL,
	.switching_set_discharge_current = NULL,
	.switching_enable_charge = NULL,
	.switching_get_fastcharge_current = NULL,
	.switching_get_discharge_current = NULL,
	.switching_get_charge_enable = NULL,
};

static int oplus_mos_ctrl_probe(struct platform_device *pdev)
{
	struct oplus_mos_dev *mos_dev;
	struct device *dev = &pdev->dev;
	int batt_volt;
	int sub_batt_volt;
	bool batt_authenticate;
	bool sub_batt_authenticate;
	int error_status = 0;
	int gauge_statue = 0;

	gauge_statue = oplus_gauge_get_batt_soc();
	if (gauge_statue == -1) {
		chg_err("gauge0 not ready, will do after gauge0 init\n");
		return -EPROBE_DEFER;
	}

	gauge_statue = oplus_gauge_get_sub_batt_soc();
	if (gauge_statue == -1) {
		chg_err("gauge1 not ready, will do after gauge1 init\n");
		return -EPROBE_DEFER;
	}

	batt_volt = oplus_gauge_get_batt_mvolts();
	sub_batt_volt = oplus_gauge_get_sub_batt_mvolts();
	batt_authenticate = oplus_gauge_get_batt_authenticate();
	sub_batt_authenticate = oplus_gauge_get_sub_batt_authenticate();
	//s2asl01_boot_mode = get_boot_mode();
	chg_err(" batt_authenticate:%d, sub_batt_authenticate:%d\n", batt_authenticate, sub_batt_authenticate);

	if (batt_authenticate != true || sub_batt_authenticate != true) {
		error_status = SWITCH_ERROR_I2C_ERROR;
		chg_err("gauge i2c error! error_status:%d\n",
			batt_authenticate, sub_batt_authenticate, error_status);
	}

	mos_dev = devm_kzalloc(&pdev->dev, sizeof(*mos_dev), GFP_KERNEL);
	if (!mos_dev)
		return -ENOMEM;

	mos_dev->dev = dev;

	platform_set_drvdata(pdev, mos_dev);

	g_mos_dev = mos_dev;

	g_mos_dev->switching_chip = devm_kzalloc(&pdev->dev,
		sizeof(struct oplus_switch_chip), GFP_KERNEL);
	if (!g_mos_dev->switching_chip) {
		chg_err("g_mos_dev->switching_chip devm_kzalloc failed.\n");
		return -ENOMEM;
	}

	oplus_mos_ctrl_parse_dt(mos_dev);
	if (abs(batt_volt - sub_batt_volt) > g_mos_dev->switching_chip->parallel_vbat_gap_abnormal) {
		error_status = SWITCH_ERROR_OVER_VOLTAGE;
	}
	if (error_status == 0) {
		oplus_mos_ctrl_set_enable(1);
	}

	g_mos_dev->switching_chip->switch_ops = &oplus_mos_ctrl_ops;
	g_mos_dev->switching_chip->dev = mos_dev->dev;
	g_mos_dev->switching_chip->error_status = error_status;
	oplus_switching_init(g_mos_dev->switching_chip, PARALLEL_MOS_CTRL);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if(get_boot_mode() == ATE_FACTORY_BOOT || get_boot_mode() == FACTORY_BOOT) {
#else
	if(get_boot_mode() == MSM_BOOT_MODE__FACTORY) {
#endif
		chg_err("boot_mode: %d, disabled mos\n", get_boot_mode());
		oplus_mos_ctrl_set_enable(0);
	}

	return 0;
}

static int oplus_mos_ctrl_remove(struct platform_device *pdev)
{
	struct oplus_mos_dev *dev = platform_get_drvdata(pdev);

	pinctrl_select_state(dev->mos_en_pinctrl, dev->mos_en_gpio_sleep);

	return 0;
}

static void oplus_mos_ctrl_shutdown(struct platform_device *pdev)
{
	struct oplus_mos_dev *dev = platform_get_drvdata(pdev);

	pinctrl_select_state(dev->mos_en_pinctrl, dev->mos_en_gpio_sleep);
}

static const struct of_device_id oplus_mos_match_table[] = {
	{ .compatible = "oplus,mos-ctrl" },
	{},
};

static struct platform_driver oplus_mos_ctrl_driver = {
	.driver = {
		.name = "oplus_mos_ctrl",
		.of_match_table = oplus_mos_match_table,
	},
	.probe = oplus_mos_ctrl_probe,
	.remove = oplus_mos_ctrl_remove,
	.shutdown = oplus_mos_ctrl_shutdown,
};

static int __init oplus_mos_ctrl_init(void)
{
	int ret;

	ret = platform_driver_register(&oplus_mos_ctrl_driver);

	return ret;
}

static void __exit oplus_mos_ctrl_exit(void)
{
	platform_driver_unregister(&oplus_mos_ctrl_driver);
}

oplus_chg_module_register(oplus_mos_ctrl);

MODULE_DESCRIPTION("Oplus mos ctrl driver");
MODULE_LICENSE("GPL v2");
