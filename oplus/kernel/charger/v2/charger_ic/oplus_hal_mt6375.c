// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[MT6375]([%s][%d]): " fmt, __func__, __LINE__
#include <linux/version.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/sched/clock.h>

#include <tcpci_core.h>

#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include <oplus_mms.h>
#include <oplus_chg_monitor.h>

#define PROBE_CNT_MAX			100
#define LOCAL_T_NS_TO_MS_THD		1000000

#define MT6375_REG_MTINT2		0x99
#define MT6375_REG_MTST1		0x9F

#define TCPC_V10_REG_POWER_CTRL		0x1C
#define TCPC_V10_REG_CC_STATUS		0x1D
#define TCPC_V10_REG_POWER_STATUS	0x1E
#define TCPC_V10_REG_FAULT_STATUS	0x1F

#define MT6375_VCOON_OVP_MASK		BIT(4)
#define MT6375_VCOON_RVP_MASK		BIT(2)
#define MT6375_VCOON_OCP_MASK		BIT(1)
#define MT6375_VBUS_PRESENT_MASK	BIT(2)
#define MT6375_VSAFE_0V_MASK		BIT(1)
#define MT6375_VCOON_EN_MASK		BIT(0)

enum mt6375_debug_msg_type {
	DEBUG_MSG_VCONN_RVP,
	DEBUG_MSG_VCONN_OCP,
	DEBUG_MSG_VCONN_OVP,
	DEBUG_MSG_VCONN_UVP,
	DEBUG_MSG_VCONN_OPEN,
	DEBUG_MSG_VCONN_CLOSE,
	DEBUG_MSG_POWER_STATUS_CHANGE,
};

struct mt6375_device {
	struct device *dev;
	struct regmap *rmap;
	struct tcpc_device *tcpc;
	struct oplus_chg_ic_dev *ic_dev;

	struct work_struct vcoon_rvp_work;
	struct work_struct vcoon_ocp_work;
	struct work_struct vcoon_ovp_work;
	struct work_struct vcoon_uvp_work;
	struct work_struct vcoon_open_work;
	struct work_struct vcoon_close_work;
	struct work_struct power_status_change_work;

	int vconn_en_time;
	bool vbus_err;
};

extern int tcpc_mt6375_reg_debug_msg_handler(struct tcpc_device *tcpc,
					     void (*msg_handler)(void *, int),
					     void *priv_data);
extern void tcpc_mt6375_unreg_debug_msg_handler(struct tcpc_device *tcpc);

static int mt6375_get_local_time_ms(void)
{
	int local_time_ms;

	local_time_ms = local_clock() / LOCAL_T_NS_TO_MS_THD;

	return local_time_ms;
}

__maybe_unused static int mt6375_vcoon_is_rvp(struct mt6375_device *chip)
{
	int rc;
	unsigned int data;

	rc = regmap_read(chip->rmap, MT6375_REG_MTINT2, &data);
	if (rc < 0) {
		chg_err("failed to 0x%02x, rc=%d\n", MT6375_REG_MTINT2, rc);
		return rc;
	}

	return (data & MT6375_VCOON_RVP_MASK) == MT6375_VCOON_RVP_MASK;
}

__maybe_unused static int mt6375_vcoon_is_ocp(struct mt6375_device *chip)
{
	int rc;
	unsigned int data;

	rc = regmap_read(chip->rmap, TCPC_V10_REG_FAULT_STATUS, &data);
	if (rc < 0) {
		chg_err("failed to 0x%02x, rc=%d\n", TCPC_V10_REG_FAULT_STATUS,
			rc);
		return rc;
	}

	return (data & MT6375_VCOON_OCP_MASK) == MT6375_VCOON_OCP_MASK;
}

__maybe_unused static int mt6375_vcoon_is_ovp(struct mt6375_device *chip)
{
	int rc;
	unsigned int data;

	rc = regmap_read(chip->rmap, MT6375_REG_MTINT2, &data);
	if (rc < 0) {
		chg_err("failed to 0x%02x, rc=%d\n", MT6375_REG_MTINT2, rc);
		return rc;
	}

	return (data & MT6375_VCOON_OVP_MASK) == MT6375_VCOON_OVP_MASK;
}

static int mt6375_vbus_is_error(struct mt6375_device *chip)
{
	int rc;
	unsigned int data;
	bool vbus_present, vsafe_0v;

	rc = regmap_read(chip->rmap, TCPC_V10_REG_POWER_STATUS, &data);
	if (rc < 0) {
		chg_err("failed to 0x%02x, rc=%d\n", TCPC_V10_REG_POWER_STATUS,
			rc);
		return rc;
	}
	vbus_present = !!(data & MT6375_VBUS_PRESENT_MASK);

	rc = regmap_read(chip->rmap, MT6375_REG_MTST1, &data);
	if (rc < 0) {
		chg_err("failed to 0x%02x, rc=%d\n", MT6375_REG_MTST1, rc);
		return rc;
	}
	vsafe_0v = !!(data & MT6375_VSAFE_0V_MASK);

	return vbus_present && vsafe_0v;
}

static int mt6375_vcoon_is_open(struct mt6375_device *chip)
{
	int rc;
	unsigned int data;

	rc = regmap_read(chip->rmap, TCPC_V10_REG_POWER_CTRL, &data);
	if (rc < 0) {
		chg_err("failed to 0x%02x, rc=%d\n", TCPC_V10_REG_POWER_CTRL,
			rc);
		return rc;
	}

	return (data & MT6375_VCOON_EN_MASK) == MT6375_VCOON_EN_MASK;
}

static int mt6375_get_cc_status(struct mt6375_device *chip, int *cc1, int *cc2)
{
	struct tcpc_device *tcpc = chip->tcpc;
	int rc;

	if (tcpc == NULL) {
		chg_err("tcpc is NULL\n");
		return -ENODEV;
	}
	if (tcpc->ops == NULL) {
		chg_err("tcpc ops is NULL\n");
		return -ENODEV;
	}
	if (tcpc->ops->get_cc == NULL) {
		chg_err("tcpc get_cc is NULL\n");
		return -ENOTSUPP;
	}

	rc = tcpc->ops->get_cc(tcpc, cc1, cc2);
	if (rc < 0)
		chg_err("failed to get cc status, rc=%d\n", rc);

	return rc;
}

static void mt6375_vcoon_rvp_work(struct work_struct *work)
{
	struct mt6375_device *chip =
		container_of(work, struct mt6375_device, vcoon_rvp_work);
	int vbus_err, vcoon_en, cc1, cc2;
	int rc;

	vbus_err = mt6375_vbus_is_error(chip);
	vcoon_en = mt6375_vcoon_is_open(chip);
	rc = mt6375_get_cc_status(chip, &cc1, &cc2);
	if (rc < 0)
		cc1 = cc2 = rc;

	oplus_chg_ic_creat_err_msg(
		chip->ic_dev, OPLUS_IC_ERR_PLAT_PMIC, PLAT_PMIC_ERR_VCONN_RVP,
		"vbus_err[%d],vcoon_en[%d],cc1[%d],cc2[%d]",
		vbus_err, vcoon_en, cc1, cc2);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void mt6375_vcoon_ocp_work(struct work_struct *work)
{
	struct mt6375_device *chip =
		container_of(work, struct mt6375_device, vcoon_ocp_work);
	int vbus_err, vcoon_en, cc1, cc2;
	int rc;

	vbus_err = mt6375_vbus_is_error(chip);
	vcoon_en = mt6375_vcoon_is_open(chip);
	rc = mt6375_get_cc_status(chip, &cc1, &cc2);
	if (rc < 0)
		cc1 = cc2 = rc;

	oplus_chg_ic_creat_err_msg(
		chip->ic_dev, OPLUS_IC_ERR_PLAT_PMIC, PLAT_PMIC_ERR_VCONN_OCP,
		"vbus_err[%d],vcoon_en[%d],cc1[%d],cc2[%d]",
		vbus_err, vcoon_en, cc1, cc2);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void mt6375_vcoon_ovp_work(struct work_struct *work)
{
	struct mt6375_device *chip =
		container_of(work, struct mt6375_device, vcoon_ovp_work);
	int vbus_err, vcoon_en, cc1, cc2;
	int rc;

	vbus_err = mt6375_vbus_is_error(chip);
	vcoon_en = mt6375_vcoon_is_open(chip);
	rc = mt6375_get_cc_status(chip, &cc1, &cc2);
	if (rc < 0)
		cc1 = cc2 = rc;

	oplus_chg_ic_creat_err_msg(
		chip->ic_dev, OPLUS_IC_ERR_PLAT_PMIC, PLAT_PMIC_ERR_VCONN_OVP,
		"vbus_err[%d],vcoon_en[%d],cc1[%d],cc2[%d]",
		vbus_err, vcoon_en, cc1, cc2);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void mt6375_vcoon_uvp_work(struct work_struct *work)
{
	struct mt6375_device *chip =
		container_of(work, struct mt6375_device, vcoon_uvp_work);
	int vbus_err, vcoon_en, cc1, cc2;
	int rc;

	vbus_err = mt6375_vbus_is_error(chip);
	vcoon_en = mt6375_vcoon_is_open(chip);
	rc = mt6375_get_cc_status(chip, &cc1, &cc2);
	if (rc < 0)
		cc1 = cc2 = rc;

	oplus_chg_ic_creat_err_msg(
		chip->ic_dev, OPLUS_IC_ERR_PLAT_PMIC, PLAT_PMIC_ERR_VCONN_UVP,
		"vbus_err[%d],vcoon_en[%d],cc1[%d],cc2[%d]",
		vbus_err, vcoon_en, cc1, cc2);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void mt6375_vcoon_open_work(struct work_struct *work)
{
	struct mt6375_device *chip =
		container_of(work, struct mt6375_device, vcoon_open_work);

	chip->vconn_en_time = mt6375_get_local_time_ms();
}

static void mt6375_vcoon_close_work(struct work_struct *work)
{
	struct mt6375_device *chip =
		container_of(work, struct mt6375_device, vcoon_close_work);
	int time = -EINVAL;
	int cc1, cc2;
	int rc;

	if (chip->vconn_en_time >= 0) {
		time = mt6375_get_local_time_ms() - chip->vconn_en_time;
		chip->vconn_en_time = -EINVAL;
	}
	rc = mt6375_get_cc_status(chip, &cc1, &cc2);
	if (rc < 0)
		cc1 = cc2 = rc;

	oplus_chg_ic_creat_err_msg(chip->ic_dev, OPLUS_IC_ERR_PLAT_PMIC,
				   PLAT_PMIC_ERR_VCONN_CLOSE,
				   "open_time[%d],cc1[%d],cc2[%d]", time, cc1,
				   cc2);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

#define BOOT_TIME_THR_MS	60000
static void mt6375_power_status_change_work(struct work_struct *work)
{
	struct mt6375_device *chip = container_of(work, struct mt6375_device,
						  power_status_change_work);
	static struct oplus_mms *err_topic;
	static int boot_time_ms;
	int vbus_err, vcoon_en, cc1, cc2;
	int rc;

	vbus_err = mt6375_vbus_is_error(chip);
	chg_debug("vbus_err=%d\n", vbus_err);
	if (!vbus_err || vbus_err < 0) {
		chip->vbus_err = false;
		return;
	}
	if (chip->vbus_err)
		return;

	if (boot_time_ms < BOOT_TIME_THR_MS) {
		boot_time_ms = mt6375_get_local_time_ms();
		if (!err_topic)
			err_topic = oplus_mms_get_by_name("error");
	}
	/*
	 * error msg reported before the error topic is not
	 * registered will be lost
	 */
	if (boot_time_ms > BOOT_TIME_THR_MS || err_topic)
		chip->vbus_err = true;

	vcoon_en = mt6375_vcoon_is_open(chip);
	rc = mt6375_get_cc_status(chip, &cc1, &cc2);
	if (rc < 0)
		cc1 = cc2 = rc;

	oplus_chg_ic_creat_err_msg(
		chip->ic_dev, OPLUS_IC_ERR_PLAT_PMIC,
		PLAT_PMIC_ERR_VBUS_ABNORMAL,
		"vbus_err[%d],vcoon_en[%d],cc1[%d],cc2[%d]",
		vbus_err, vcoon_en, cc1, cc2);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void mt6375_debug_msg_handler(void *data, int msg_type)
{
	struct mt6375_device *chip;

	if (data == NULL)
		return;

	chip = (struct mt6375_device *)data;

	chg_debug("msg_type=%d\n", msg_type);
	switch (msg_type) {
	case DEBUG_MSG_VCONN_RVP:
		schedule_work(&chip->vcoon_rvp_work);
		break;
	case DEBUG_MSG_VCONN_OCP:
		schedule_work(&chip->vcoon_ocp_work);
		break;
	case DEBUG_MSG_VCONN_OVP:
		schedule_work(&chip->vcoon_ovp_work);
		break;
	case DEBUG_MSG_VCONN_UVP:
		schedule_work(&chip->vcoon_uvp_work);
		break;
	case DEBUG_MSG_VCONN_OPEN:
		schedule_work(&chip->vcoon_open_work);
		break;
	case DEBUG_MSG_VCONN_CLOSE:
		schedule_work(&chip->vcoon_close_work);
		break;
	case DEBUG_MSG_POWER_STATUS_CHANGE:
		schedule_work(&chip->power_status_change_work);
		break;
	default:
		chg_err("unknown msg type, type=%d\n", msg_type);
		return;
	}
}

static int mt6375_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;
	return 0;
}

static int mt6375_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	return 0;
}

static void *oplus_chg_get_func(struct oplus_chg_ic_dev *ic_dev,
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
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, mt6375_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, mt6375_exit);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq mt6375_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
};

static int mt6375_probe(struct platform_device *pdev)
{
	static int probe_cnt;
	struct mt6375_device *chip;
	const char *tcpc_name;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	struct device_node *node = pdev->dev.of_node;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct mt6375_device),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("mem alloc error\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	rc = of_property_read_string(node, "oplus,tcpc_name", &tcpc_name);
	if (rc < 0) {
		chg_err("oplus,tcpc_name not found, rc=%d\n", rc);
		tcpc_name = "type_c_port0";
	}
	chip->tcpc = tcpc_dev_get_by_name(tcpc_name);
	if (!chip->tcpc) {
		chg_err("get tcpc dev fail\n");
		if (probe_cnt >= PROBE_CNT_MAX)
			rc = -ENODEV;
		else
			rc = -EPROBE_DEFER;
		goto err_get_tcpc_dev;
	}

	chip->rmap = dev_get_regmap(chip->tcpc->dev.parent->parent, NULL);
	if (chip->rmap == NULL) {
		chg_err("failed to get regmap\n");
		rc = -ENODEV;
		goto err_get_regmap;
	}

	INIT_WORK(&chip->vcoon_rvp_work, mt6375_vcoon_rvp_work);
	INIT_WORK(&chip->vcoon_ocp_work, mt6375_vcoon_ocp_work);
	INIT_WORK(&chip->vcoon_ovp_work, mt6375_vcoon_ovp_work);
	INIT_WORK(&chip->vcoon_uvp_work, mt6375_vcoon_uvp_work);
	INIT_WORK(&chip->vcoon_open_work, mt6375_vcoon_open_work);
	INIT_WORK(&chip->vcoon_close_work, mt6375_vcoon_close_work);
	INIT_WORK(&chip->power_status_change_work,
		  mt6375_power_status_change_work);
	chip->vconn_en_time = -EINVAL;

	rc = tcpc_mt6375_reg_debug_msg_handler(chip->tcpc,
					       mt6375_debug_msg_handler, chip);
	if (rc < 0) {
		chg_err("failed to register debug msg handler, rc=%d\n", rc);
		goto err_reg_msg_handler;
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
	sprintf(ic_cfg.manu_name, "MT6375");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = mt6375_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(mt6375_virq_table);
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}

	chg_err("probe success\n");

	return 0;

reg_ic_err:
	tcpc_mt6375_unreg_debug_msg_handler(chip->tcpc);
err_reg_msg_handler:
err_get_regmap:
err_get_tcpc_dev:
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);

	chg_err("probe_cnt=%d\n", probe_cnt);
	if (rc == -EPROBE_DEFER)
		probe_cnt++;

	return rc;
}

static int mt6375_remove(struct platform_device *pdev)
{
	struct mt6375_device *chip = platform_get_drvdata(pdev);

	tcpc_mt6375_unreg_debug_msg_handler(chip->tcpc);
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);

	return 0;
}

static const struct of_device_id mt6375_match_table[] = {
	{ .compatible = "oplus,hal_mt6375" },
	{},
};

static struct platform_driver mt6375_debug_driver = {
	.driver = {
		.name = "oplus_mt6375_debug",
		.of_match_table = mt6375_match_table,
	},
	.probe = mt6375_probe,
	.remove = mt6375_remove,
};

int mt6375_driver_init(void)
{
	int rc;

	rc = platform_driver_register(&mt6375_debug_driver);
	if (rc < 0)
		chg_err("failed to register mt6375 debug driver, rc=%d\n", rc);

	return rc;
}

void mt6375_driver_exit(void)
{
	platform_driver_unregister(&mt6375_debug_driver);
}
oplus_chg_module_register(mt6375_driver);
