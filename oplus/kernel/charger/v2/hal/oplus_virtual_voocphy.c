#define pr_fmt(fmt) "[VIRTUAL_VOOCPHY]([%s][%d]): " fmt, __func__, __LINE__

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


struct oplus_virtual_vphy_gpio {
	int switch_ctrl_gpio;
	int vphy_switch1_gpio;
	int vphy_switch2_gpio;
	int vphy_id_gpio;
	int data_irq;

	struct pinctrl *pinctrl;
	struct mutex pinctrl_mutex;

	struct pinctrl_state *vphy_switch_normal;
	struct pinctrl_state *vphy_switch_vooc;
	struct pinctrl_state *gpio_switch_ctrl_ap;
	struct pinctrl_state *gpio_switch_ctrl_vphy;

	struct pinctrl_state *vphy_id_active;
	struct pinctrl_state *vphy_id_sleep;
};

struct oplus_virtual_vphy_child {
	struct oplus_chg_ic_dev *ic_dev;
	bool initialized;
};

struct oplus_virtual_vphy_ic {
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct oplus_chg_ic_dev *vphy;
	struct oplus_virtual_vphy_child *vphy_list;
	struct oplus_virtual_vphy_gpio gpio;
	int vphy_num;

	enum oplus_chg_vooc_ic_type vooc_ic_type;
	enum oplus_chg_vooc_switch_mode switch_mode;

	struct work_struct data_handler_work;
};


static struct oplus_chg_ic_virq oplus_vphy_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_VOOC_DATA },
};

static void oplus_vphy_err_handler(struct oplus_chg_ic_dev *ic_dev, void *virq_data)
{
	struct oplus_virtual_vphy_ic *chip = virq_data;

	oplus_chg_ic_move_err_msg(chip->ic_dev, ic_dev);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void oplus_vphy_data_handler(struct oplus_chg_ic_dev *ic_dev, void *virq_data)
{
	struct oplus_virtual_vphy_ic *chip = virq_data;

	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_VOOC_DATA);
}

static int oplus_vphy_virq_register(struct oplus_virtual_vphy_ic *chip)
{
	int rc;
	int i;

	if (chip->vphy == NULL) {
		chg_err("no active vphy found");
		return -ENODEV;
	}

	rc = oplus_chg_ic_virq_register(chip->vphy,
		OPLUS_IC_VIRQ_ERR, oplus_vphy_err_handler, chip);
	if (rc < 0)
		chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d", rc);

	for (i = 0; i < chip->vphy_num; i++) {
		rc = oplus_chg_ic_virq_register(chip->vphy_list[i].ic_dev,
						OPLUS_IC_VIRQ_VOOC_DATA,
						oplus_vphy_data_handler, chip);
		if (rc < 0)
			chg_err("register OPLUS_IC_VIRQ_VOOC_DATA error, rc=%d", rc);
	};

	return 0;
}

static int oplus_chg_vphy_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_vphy_ic *va;
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
	for (i = 0; i < va->vphy_num; i++) {
		if (va->vphy_list[i].initialized)
			continue;
		va->vphy_list[i].ic_dev =
			of_get_oplus_chg_ic(node, "oplus,vphy_ic", i);
		if (va->vphy_list[i].ic_dev == NULL) {
			chg_debug("vphy[%d] not found\n", i);
			retry = true;
			continue;
		}
		va->vphy_list[i].ic_dev->parent = ic_dev;
		rc = oplus_chg_ic_func(va->vphy_list[i].ic_dev, OPLUS_IC_FUNC_INIT);
		va->vphy_list[i].initialized = true;
		if (rc >= 0) {
			chg_info("vphy(=%s) init success\n",
				 va->vphy_list[i].ic_dev->name);
			va->vphy = va->vphy_list[i].ic_dev;
			va->vooc_ic_type = rc;
			goto init_done;
		} else {
			chg_err("vphy(=%s) init error, rc=%d\n",
				va->vphy_list[i].ic_dev->name, rc);
		}
	}

	if (retry) {
		return -EAGAIN;
	} else {
		chg_err("all vphy init error\n");
		return -EINVAL;
	}

init_done:
	oplus_vphy_virq_register(va);
	return rc;
}

static int oplus_chg_vphy_exit(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_vphy_ic *va;
	int i;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;

	va = oplus_chg_ic_get_drvdata(ic_dev);
	ic_dev->online = false;

	oplus_chg_ic_virq_release(va->vphy, OPLUS_IC_VIRQ_ERR, va);
	for (i = 0; i < va->vphy_num; i++) {
		oplus_chg_ic_virq_release(va->vphy_list[i].ic_dev,
					  OPLUS_IC_VIRQ_VOOC_DATA, va);
	};

	oplus_chg_ic_func(va->vphy, OPLUS_IC_FUNC_EXIT);
	ic_dev->parent = NULL;

	return 0;
}

static int oplus_chg_vvphy_reply_data(struct oplus_chg_ic_dev *ic_dev,
				      int data, int data_width, int curr_ma)
{
	struct oplus_virtual_vphy_ic *vvphy;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	vvphy = oplus_chg_ic_get_drvdata(ic_dev);
	if (vvphy->vphy == NULL) {
		chg_err("no active vphy found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(vvphy->vphy, OPLUS_IC_FUNC_VOOC_REPLY_DATA,
			       data, data_width, curr_ma);
	if (rc < 0)
		chg_err("reply error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_vvphy_get_data(struct oplus_chg_ic_dev *ic_dev, int *data)
{
	struct oplus_virtual_vphy_ic *vvphy;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	vvphy = oplus_chg_ic_get_drvdata(ic_dev);
	if (vvphy->vphy == NULL) {
		chg_err("no active vphy found");
		return -ENODEV;
	}
	rc = oplus_chg_ic_func(vvphy->vphy, OPLUS_IC_FUNC_VOOC_READ_DATA, data);
	if (rc < 0)
		chg_err("get data error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_vphy_set_switch_mode(struct oplus_chg_ic_dev *ic_dev,
					int mode)
{
	struct oplus_virtual_vphy_ic *va;
	int retry = 10;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (IS_ERR_OR_NULL(va->gpio.pinctrl)) {
		chg_err("get pinctrl fail\n");
		goto exit;
	}

	mutex_lock(&va->gpio.pinctrl_mutex);
	switch (mode) {
	case VOOC_SWITCH_MODE_VOOC:
		do {
			rc = pinctrl_select_state(
				va->gpio.pinctrl,
				va->gpio.vphy_switch_vooc);
			if (rc < 0) {
				chg_err("set vphy_switch_vooc error, rc=%d\n",
					rc);
				goto err;
			}
			rc = pinctrl_select_state(
				va->gpio.pinctrl,
				va->gpio.gpio_switch_ctrl_vphy);
			if (rc < 0) {
				chg_err("set gpio_switch_ctrl_vphy error, rc=%d\n",
					rc);
				goto err;
			}
			chg_info("switch to vooc mode\n");
			break;
			usleep_range(5000, 5000);
		} while (--retry > 0);
		break;
	case VOOC_SWITCH_MODE_HEADPHONE:
		chg_info("switch to headphone mode\n");
		break;
	case VOOC_SWITCH_MODE_NORMAL:
	default:
		rc = pinctrl_select_state(va->gpio.pinctrl,
					  va->gpio.vphy_switch_normal);
		if (rc < 0) {
			chg_err("set vphy_switch_normal error, rc=%d\n", rc);
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
exit:
	rc = oplus_chg_ic_func(va->vphy, OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE, mode);
	if (rc < 0) {
		chg_err("set switch mode(%d) error, rc=%d\n", mode, rc);
		goto err;
	}

	return 0;
err:
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.vphy_switch_normal);
	pinctrl_select_state(va->gpio.pinctrl, va->gpio.gpio_switch_ctrl_ap);
	mutex_unlock(&va->gpio.pinctrl_mutex);
	return rc;
}

static int oplus_chg_vphy_get_switch_mode(struct oplus_chg_ic_dev *ic_dev,
					int *mode)
{
	struct oplus_virtual_vphy_ic *va;
	int switch1_val, switch2_val;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	va = oplus_chg_ic_get_drvdata(ic_dev);

	if (!gpio_is_valid(va->gpio.vphy_switch2_gpio))
		switch2_val = 1;
	else
		switch2_val = gpio_get_value(va->gpio.vphy_switch2_gpio);
	switch1_val = gpio_get_value(va->gpio.vphy_switch1_gpio);

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

static int oplus_chg_vphy_set_pdqc_config(struct oplus_chg_ic_dev *ic_dev)
{
	int rc;
	struct oplus_virtual_vphy_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->vphy == NULL) {
		chg_err("no active vphy found");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(va->vphy, OPLUS_IC_FUNC_VOOCPHY_SET_PDQC_CONFIG);
	if (rc < 0)
		chg_err("set pdqc config error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_vphy_get_cp_vbat(struct oplus_chg_ic_dev *ic_dev,
				      int *cp_vbat)
{
	int rc;
	struct oplus_virtual_vphy_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->vphy == NULL) {
		chg_err("no active vphy found");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(va->vphy, OPLUS_IC_FUNC_VOOCPHY_GET_CP_VBAT,
			       cp_vbat);
	if (rc < 0)
		chg_err("get cp vbat error, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_vphy_set_chg_auto_mode(struct oplus_chg_ic_dev *ic_dev,
					    bool enable)
{
	int rc;
	struct oplus_virtual_vphy_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	va = oplus_chg_ic_get_drvdata(ic_dev);
	if (va->vphy == NULL) {
		chg_err("no active vphy found");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(va->vphy,
			       OPLUS_IC_FUNC_VOOCPHY_SET_CHG_AUTO_MODE,
			       enable);
	if (rc < 0)
		chg_err("failed to %s chg auto mode, rc=%d\n",
			enable ? "enable" : "disable", rc);

	return rc;
}

static int oplus_chg_vphy_get_vphy(struct oplus_chg_ic_dev *ic_dev,
				   struct oplus_chg_ic_dev **vphy)
{
	struct oplus_virtual_vphy_ic *va;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	va = oplus_chg_ic_get_drvdata(ic_dev);
	*vphy = va->vphy;

	return 0;
}

static void *oplus_chg_vphy_get_func(struct oplus_chg_ic_dev *ic_dev,
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
					       oplus_chg_vphy_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_chg_vphy_exit);
		break;
	case OPLUS_IC_FUNC_VOOC_READ_DATA:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_READ_DATA,
					       oplus_chg_vvphy_get_data);
		break;
	case OPLUS_IC_FUNC_VOOC_REPLY_DATA:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_REPLY_DATA,
					       oplus_chg_vvphy_reply_data);
		break;
	case OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_SET_SWITCH_MODE,
			oplus_chg_vphy_set_switch_mode);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_SWITCH_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_SWITCH_MODE,
			oplus_chg_vphy_get_switch_mode);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_PDQC_CONFIG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_PDQC_CONFIG,
					       oplus_chg_vphy_set_pdqc_config);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_GET_CP_VBAT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_GET_CP_VBAT,
					       oplus_chg_vphy_get_cp_vbat);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_CHG_AUTO_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_CHG_AUTO_MODE,
					       oplus_chg_vphy_set_chg_auto_mode);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_IC_DEV:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_GET_IC_DEV,
					       oplus_chg_vphy_get_vphy);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static void oplus_vphy_data_handler_work(struct work_struct *work)
{
	struct oplus_virtual_vphy_ic *va = container_of(
		work, struct oplus_virtual_vphy_ic, data_handler_work);

	oplus_chg_ic_virq_trigger(va->ic_dev, OPLUS_IC_VIRQ_VOOC_DATA);
}

static int oplus_vphy_child_init(struct oplus_virtual_vphy_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	rc = of_property_count_elems_of_size(node, "oplus,vphy_ic",
					     sizeof(u32));
	if (rc < 0) {
		chg_err("can't get vphy number, rc=%d\n", rc);
		return rc;
	}
	chip->vphy_num = rc;
	chip->vphy_list = devm_kzalloc(chip->dev,
				       sizeof(struct oplus_virtual_vphy_child) *
					       chip->vphy_num,
				       GFP_KERNEL);
	if (chip->vphy_list == NULL) {
		rc = -ENOMEM;
		chg_err("alloc vphy table memory error\n");
		return rc;
	}

	return 0;
}

static int oplus_vphy_gpio_init(struct oplus_virtual_vphy_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct oplus_virtual_vphy_gpio *vphy_gpio = &chip->gpio;
	int rc = 0;

	vphy_gpio->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(vphy_gpio->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	vphy_gpio->switch_ctrl_gpio =
		of_get_named_gpio(node, "oplus,switch_ctrl-gpio", 0);
	if (!gpio_is_valid(vphy_gpio->switch_ctrl_gpio)) {
		chg_err("Couldn't read oplus,switch_ctrl-gpio, %d\n",
			vphy_gpio->switch_ctrl_gpio);
		return -ENODEV;
	}
	rc = gpio_request(vphy_gpio->switch_ctrl_gpio, "switch_ctrl-gpio");
	if (rc != 0) {
		chg_err("unable to request switch_ctrl-gpio:%d\n",
			vphy_gpio->switch_ctrl_gpio);
		return rc;
	}
	vphy_gpio->gpio_switch_ctrl_ap =
		pinctrl_lookup_state(vphy_gpio->pinctrl, "switch_ctrl_ap");
	if (IS_ERR_OR_NULL(vphy_gpio->gpio_switch_ctrl_ap)) {
		chg_err("get switch_ctrl_ap fail\n");
		rc = -EINVAL;
		goto free_switch_ctrl_gpio;
	}
	vphy_gpio->gpio_switch_ctrl_vphy =
		pinctrl_lookup_state(vphy_gpio->pinctrl, "switch_ctrl_vphy");
	if (IS_ERR_OR_NULL(vphy_gpio->gpio_switch_ctrl_vphy)) {
		chg_err("get switch_ctrl_vphy fail\n");
		rc = -EINVAL;
		goto free_switch_ctrl_gpio;
	}
	rc = pinctrl_select_state(vphy_gpio->pinctrl,
				  vphy_gpio->gpio_switch_ctrl_ap);
	if (rc < 0) {
		chg_err("set gpio switch_ctrl_ap error, rc=%d\n", rc);
		goto free_switch_ctrl_gpio;
	}

	vphy_gpio->vphy_switch1_gpio =
		of_get_named_gpio(node, "oplus,vphy_switch1-gpio", 0);
	if (!gpio_is_valid(vphy_gpio->vphy_switch1_gpio)) {
		chg_err("Couldn't read oplus,vphy_switch1-gpio, %d\n",
			vphy_gpio->vphy_switch1_gpio);
		rc = -ENODEV;
		goto free_switch_ctrl_gpio;
	}
	rc = gpio_request(vphy_gpio->vphy_switch1_gpio, "vphy_switch1-gpio");
	if (rc != 0) {
		chg_err("unable to request vphy_switch1-gpio:%d\n",
			vphy_gpio->vphy_switch1_gpio);
		goto free_switch_ctrl_gpio;
	}
	vphy_gpio->vphy_switch2_gpio =
		of_get_named_gpio(node, "oplus,vphy_switch2-gpio", 0);
	if (!gpio_is_valid(vphy_gpio->vphy_switch2_gpio)) {
		chg_info("Couldn't read oplus,vphy_switch2-gpio, %d\n",
			 vphy_gpio->vphy_switch2_gpio);
	} else {
		rc = gpio_request(vphy_gpio->vphy_switch2_gpio,
				  "vphy_switch2-gpio");
		if (rc != 0) {
			chg_err("unable to request vphy_switch2-gpio:%d\n",
				vphy_gpio->vphy_switch2_gpio);
			goto free_vphy_switch1_gpio;
		}
	}
	vphy_gpio->vphy_switch_normal =
		pinctrl_lookup_state(vphy_gpio->pinctrl, "vphy_switch_normal");
	if (IS_ERR_OR_NULL(vphy_gpio->vphy_switch_normal)) {
		chg_err("get vphy_switch_normal fail\n");
		rc = -EINVAL;
		goto free_vphy_switch2_gpio;
	}
	vphy_gpio->vphy_switch_vooc =
		pinctrl_lookup_state(vphy_gpio->pinctrl, "vphy_switch_vooc");
	if (IS_ERR_OR_NULL(vphy_gpio->vphy_switch_vooc)) {
		chg_err("get vphy_switch_vooc fail\n");
		rc = -EINVAL;
		goto free_vphy_switch2_gpio;
	}
	rc = pinctrl_select_state(vphy_gpio->pinctrl,
				  vphy_gpio->vphy_switch_normal);
	if (rc < 0) {
		chg_err("set gpio vphy_switch_normal error, rc=%d\n", rc);
		goto free_vphy_switch2_gpio;
	}

	mutex_init(&vphy_gpio->pinctrl_mutex);

	return 0;

free_vphy_switch2_gpio:
	if (gpio_is_valid(vphy_gpio->vphy_switch2_gpio))
		gpio_free(vphy_gpio->vphy_switch2_gpio);
free_vphy_switch1_gpio:
	if (gpio_is_valid(vphy_gpio->vphy_switch1_gpio))
		gpio_free(vphy_gpio->vphy_switch1_gpio);
free_switch_ctrl_gpio:
	if (gpio_is_valid(vphy_gpio->switch_ctrl_gpio))
		gpio_free(vphy_gpio->switch_ctrl_gpio);

	return rc;
}

static int oplus_virtual_vphy_probe(struct platform_device *pdev)
{
	struct oplus_virtual_vphy_ic *chip;
	struct device_node *node = pdev->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_virtual_vphy_ic),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	INIT_WORK(&chip->data_handler_work, oplus_vphy_data_handler_work);

	rc = oplus_vphy_gpio_init(chip);
	if (rc < 0)
		chg_err("gpio init error, rc=%d\n", rc);

	rc = oplus_vphy_child_init(chip);
	if (rc < 0) {
		chg_err("child list init error, rc=%d\n", rc);
		goto child_init_err;
	}

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
	sprintf(ic_cfg.manu_name, "virtual vphy");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_vphy_get_func;
	ic_cfg.virq_data = oplus_vphy_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(oplus_vphy_virq_table);
	chip->ic_dev =
		devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}

	chg_err("probe success\n");
	return 0;

reg_ic_err:
	if (gpio_is_valid(chip->gpio.vphy_id_gpio))
		gpio_free(chip->gpio.vphy_id_gpio);
	if (gpio_is_valid(chip->gpio.vphy_switch2_gpio))
		gpio_free(chip->gpio.vphy_switch2_gpio);
	if (gpio_is_valid(chip->gpio.vphy_switch1_gpio))
		gpio_free(chip->gpio.vphy_switch1_gpio);
	if (gpio_is_valid(chip->gpio.switch_ctrl_gpio))
		gpio_free(chip->gpio.switch_ctrl_gpio);
	devm_kfree(&pdev->dev, chip->vphy_list);
child_init_err:
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	chg_err("probe error\n");
	return rc;
}

static int oplus_virtual_vphy_remove(struct platform_device *pdev)
{
	struct oplus_virtual_vphy_ic *chip = platform_get_drvdata(pdev);

	if (chip == NULL)
		return -ENODEV;

	if (chip->ic_dev->online)
		oplus_chg_vphy_exit(chip->ic_dev);
	devm_oplus_chg_ic_unregister(&pdev->dev, chip->ic_dev);
	if (gpio_is_valid(chip->gpio.vphy_id_gpio))
		gpio_free(chip->gpio.vphy_id_gpio);
	if (gpio_is_valid(chip->gpio.vphy_switch2_gpio))
		gpio_free(chip->gpio.vphy_switch2_gpio);
	if (gpio_is_valid(chip->gpio.vphy_switch1_gpio))
		gpio_free(chip->gpio.vphy_switch1_gpio);
	if (gpio_is_valid(chip->gpio.switch_ctrl_gpio))
		gpio_free(chip->gpio.switch_ctrl_gpio);
	devm_kfree(&pdev->dev, chip->vphy_list);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void oplus_virtual_vphy_shutdown(struct platform_device *pdev)
{
/* TODO
	struct oplus_virtual_vphy_ic *chip = platform_get_drvdata(pdev);

	oplus_chg_vphy_set_switch_mode(chip->ic_dev, VOOC_SWITCH_MODE_NORMAL);
	msleep(10);
	if (oplus_vooc_get_fastchg_started() == true) {
		oplus_chg_vphy_set_clock_sleep(chip->ic_dev);
		msleep(10);
		oplus_chg_vphy_reset_active(chip->ic_dev);
	}
	msleep(80);
*/
}

static const struct of_device_id oplus_virtual_vphy_match[] = {
	{ .compatible = "oplus,virtual_vphy" },
	{},
};

static struct platform_driver oplus_virtual_vphy_driver = {
	.driver		= {
		.name = "oplus-virtual_vphy",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_virtual_vphy_match),
	},
	.probe		= oplus_virtual_vphy_probe,
	.remove		= oplus_virtual_vphy_remove,
	.shutdown	= oplus_virtual_vphy_shutdown,
};

static __init int oplus_virtual_vphy_init(void)
{
	return platform_driver_register(&oplus_virtual_vphy_driver);
}

static __exit void oplus_virtual_vphy_exit(void)
{
	platform_driver_unregister(&oplus_virtual_vphy_driver);
}

oplus_chg_module_register(oplus_virtual_vphy);
