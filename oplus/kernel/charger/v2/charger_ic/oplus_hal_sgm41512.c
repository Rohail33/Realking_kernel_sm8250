// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[SGM41512]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/boot_mode.h>

#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>

#ifndef I2C_ERR_MAX
#define I2C_ERR_MAX 2
#endif

#define BC12_TIMEOUT_MS		msecs_to_jiffies(5000)

#define REG_MAX 0x0b

#define HIZ_MODE_REG		0x00
#define HIZ_MODE_BIT		BIT(7)
#define CHG_CONFIG_REG		0x01
#define CHG_EN_BIT		BIT(4)
#define WATCHGDOG_CONFIG_REG	0x05
#define WATCHGDOG_CONFIG_BIT	(BIT(4) | BIT(5))
#define VINDPM_OVP_CONFIG_REG	0x06
#define VINDPM_CONFIG_BIT	(BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define BC12_REG		0x07
#define BC12_RERUN_BIT		BIT(7)
#define BC12_RESULT_REG		0x08
#define BC12_RESULT_BIT		(BIT(5) | BIT(6) | BIT(7))
#define IRQ_MASK_REG		0x0a
#define VINDPM_IRQ_MASK_BIT	BIT(1)
#define IINDPM_IRQ_MASK_BIT	BIT(0)

static atomic_t i2c_err_count;

struct sgm41512_chip {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;

	struct mutex pinctrl_lock;
	struct pinctrl *pinctrl;
	struct pinctrl_state *event_default;
	struct pinctrl_state *dis_vbus_active;
	struct pinctrl_state *dis_vbus_sleep;
	struct mutex i2c_lock;
	struct regmap *regmap;

	struct mutex dpdm_lock;
	struct regulator *dpdm_reg;

	struct work_struct event_work;
	struct work_struct plugin_work;
	struct delayed_work bc12_timeout_work;
	struct oplus_mms *wired_topic;
	struct mms_subscribe *wired_subs;

	int event_gpio;
	int event_irq;
	int dis_vbus_gpio;

	atomic_t charger_suspended;

	bool vbus_present;
	bool bc12_retry;
	bool otg_mode;
	bool auto_bc12;
	bool bc12_complete;
	int charge_type;
	bool event_irq_enabled;
};

enum {
	CHARGE_TYPE_NO_INPUT = 0,
	CHARGE_TYPE_SDP,
	CHARGE_TYPE_CDP,
	CHARGE_TYPE_DCP,
	CHARGE_TYPE_VBUS_TYPE_UNKNOWN = 5,
	CHARGE_TYPE_OCP,
	CHARGE_TYPE_OTG,
};

static int sgm41512_hw_init(struct sgm41512_chip *chip);

static struct regmap_config sgm41512_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

static int sgm41512_reg_dump(struct oplus_chg_ic_dev *ic_dev);

static __inline__ void sgm41512_i2c_err_inc(struct sgm41512_chip *chip)
{
	if (atomic_inc_return(&i2c_err_count) > I2C_ERR_MAX) {
		atomic_set(&i2c_err_count, 0);
		oplus_chg_ic_creat_err_msg(chip->ic_dev, OPLUS_IC_ERR_I2C, 0,
					   "continuous error");
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
	}
}

static __inline__ void sgm41512_i2c_err_clr(void)
{
	atomic_set(&i2c_err_count, 0);
}

static void sgm41512_enable_irq(struct sgm41512_chip *chip, bool en)
{
	if (chip->event_irq_enabled && !en) {
		chip->event_irq_enabled = false;
		disable_irq(chip->event_irq);
	} else if (!chip->event_irq_enabled && en) {
		chip->event_irq_enabled = true;
		enable_irq(chip->event_irq);
	} else {
		chg_info("event_irq_enabled:%s, en:%s\n",
			 true_or_false_str(chip->event_irq_enabled),
			 true_or_false_str(chip->event_irq));
	}
}

static int sgm41512_read_byte(struct sgm41512_chip *chip, u8 addr, u8 *data)
{
	int rc;
	bool is_err = false;
	int retry = 3;

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err) {
			usleep_range(5000, 5000);
		}

		rc = i2c_master_send(chip->client, &addr, 1);
		if (rc < 1) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}

		rc = i2c_master_recv(chip->client, data, 1);
		if (rc < 1) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err) {
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	sgm41512_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sgm41512_i2c_err_inc(chip);
	return rc;
}

__maybe_unused static int sgm41512_read_data(struct sgm41512_chip *chip,
					     u8 addr, u8 *buf, int len)
{
	int rc;
	bool is_err = false;
	int retry = 3;

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err) {
			usleep_range(5000, 5000);
		}

		rc = i2c_master_send(chip->client, &addr, 1);
		if (rc < 1) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}

		rc = i2c_master_recv(chip->client, buf, len);
		if (rc < len) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err) {
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	sgm41512_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sgm41512_i2c_err_inc(chip);
	return rc;
}

static int sgm41512_write_byte(struct sgm41512_chip *chip, u8 addr, u8 data)
{
	u8 buf_temp[2] = { addr, data };
	int rc;
	bool is_err = false;
	int retry = 3;

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err) {
			usleep_range(5000, 5000);
		}

		rc = i2c_master_send(chip->client, buf_temp, 2);
		if (rc < 2) {
			chg_err("write 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err) {
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	sgm41512_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sgm41512_i2c_err_inc(chip);
	return rc;
}

__maybe_unused static int sgm41512_write_data(struct sgm41512_chip *chip,
					      u8 addr, u8 *buf, int len)
{
	u8 *buf_temp;
	int i;
	int rc;
	bool is_err = false;
	int retry = 3;

	buf_temp = kzalloc(len + 1, GFP_KERNEL);
	if (!buf_temp) {
		chg_err("alloc memary error\n");
		return -ENOMEM;
	}

	buf_temp[0] = addr;
	for (i = 0; i < len; i++)
		buf_temp[i + 1] = buf[i];

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err) {
			usleep_range(5000, 5000);
		}

		rc = i2c_master_send(chip->client, buf_temp, len + 1);
		if (rc < (len + 1)) {
			chg_err("write 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err) {
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	kfree(buf_temp);
	sgm41512_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	kfree(buf_temp);
	sgm41512_i2c_err_inc(chip);
	return rc;
}

__maybe_unused static int sgm41512_read_byte_mask(struct sgm41512_chip *chip,
						  u8 addr, u8 mask, u8 *data)
{
	u8 temp;
	int rc;

	rc = sgm41512_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;

	*data = mask & temp;

	return 0;
}

__maybe_unused static int sgm41512_write_byte_mask(struct sgm41512_chip *chip,
						   u8 addr, u8 mask, u8 data)
{
	u8 temp;
	int rc;

	rc = sgm41512_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;
	temp = (data & mask) | (temp & (~mask));
	rc = sgm41512_write_byte(chip, addr, temp);
	if (rc < 0)
		return rc;

	return 0;
}

static int sgm41512_request_dpdm(struct sgm41512_chip *chip, bool enable)
{
	int rc = 0;

	/* fetch the DPDM regulator */
	if (!chip->dpdm_reg &&
	    of_get_property(chip->dev->of_node, "dpdm-supply", NULL)) {
		chip->dpdm_reg = devm_regulator_get_optional(chip->dev, "dpdm");
		if (IS_ERR(chip->dpdm_reg)) {
			rc = PTR_ERR(chip->dpdm_reg);
			chg_err("Couldn't get dpdm regulator, rc=%d\n", rc);
			chip->dpdm_reg = NULL;
			return rc;
		}
	}

	mutex_lock(&chip->dpdm_lock);
	if (enable) {
		if (chip->dpdm_reg) {
			chg_info("enabling DPDM regulator\n");
			rc = regulator_enable(chip->dpdm_reg);
			if (rc < 0)
				chg_err("Couldn't enable dpdm regulator rc=%d\n",
					rc);
		}
	} else {
		if (chip->dpdm_reg) {
			chg_err("disabling DPDM regulator\n");
			rc = regulator_disable(chip->dpdm_reg);
			if (rc < 0)
				chg_err("Couldn't disable dpdm regulator rc=%d\n",
					rc);
		}
	}
	mutex_unlock(&chip->dpdm_lock);

	return rc;
}

static int sgm41512_enable_hiz_mode(struct sgm41512_chip *chip, bool en)
{
	int rc;

	rc = sgm41512_write_byte_mask(chip, HIZ_MODE_REG, HIZ_MODE_BIT,
				      en ? HIZ_MODE_BIT : 0x00);
	if (rc < 0) {
		chg_err("can't %s hiz mode, rc=%d\n",
			en ? "enable" : "disable", rc);
		return rc;
	}

	cancel_delayed_work_sync(&chip->bc12_timeout_work);
	if (!en)
		schedule_delayed_work(&chip->bc12_timeout_work,
				      BC12_TIMEOUT_MS);

	return 0;
}

static void sgm41512_event_work(struct work_struct *work)
{
	struct sgm41512_chip *chip =
		container_of(work, struct sgm41512_chip, event_work);
	u8 data;
	int rc;

	if (!READ_ONCE(chip->vbus_present)) {
		chg_info("charger is offline\n");
		sgm41512_request_dpdm(chip, false);
		chip->charge_type = CHARGE_TYPE_NO_INPUT;
		chip->bc12_complete = false;
		return;
	}
	if (chip->otg_mode) {
		chg_info("is otg mode\n");
		return;
	}
	if (chip->bc12_complete) {
		chg_info("bc1.2 has been completed\n");
		return;
	}

	rc = sgm41512_read_byte_mask(chip, BC12_REG, BC12_RERUN_BIT, &data);
	if (rc < 0) {
		chg_err("can't read bc1.2 completed status, rc=%d\n", rc);
		return;
	}
	if (data != 0) {
		chg_info("wait bc1.2 completed\n");
		return;
	}

	if (READ_ONCE(chip->auto_bc12)) {
		/* discard the result of auto BC1.2 */
		chg_info("discard the result of auto BC1.2");
		chip->auto_bc12 = false;
		rc = sgm41512_write_byte_mask(chip, BC12_REG, BC12_RERUN_BIT,
					      BC12_RERUN_BIT);
		if (rc < 0) {
			chg_err("can't rerun bc1.2, rc=%d", rc);
			rc = sgm41512_enable_hiz_mode(chip, true);
			if (rc < 0)
				chg_err("can't enable hiz mode, rc=%d\n", rc);
			data = CHARGE_TYPE_NO_INPUT;
			goto bc12_completed;
		}
		chip->bc12_retry = false;
		return;
	}

	rc = sgm41512_read_byte_mask(chip, BC12_RESULT_REG, BC12_RESULT_BIT,
				     &data);
	if (rc < 0) {
		chg_err("can't read charge type, rc=%d\n", rc);
		data = 0;
	}
	/* BC1.2 result bit is bit5-7*/
	data = data >> 5;
	if (data == CHARGE_TYPE_NO_INPUT) {
		chg_info("bc1.2 result is no input\n");
		return;
	}
	chg_info("bc1.2 completed, data=%d\n", data);

	if (chip->bc12_retry) {
		chip->bc12_retry = false;
		rc = sgm41512_enable_hiz_mode(chip, true);
		if (rc < 0)
			chg_err("can't enable hiz mode, rc=%d\n", rc);
		goto bc12_completed;
	} else {
		switch (data) {
		case CHARGE_TYPE_SDP:
			chg_info("sdp, retry bc1.2\n");
			rc = sgm41512_write_byte_mask(
				chip, BC12_REG, BC12_RERUN_BIT, BC12_RERUN_BIT);
			if (rc < 0) {
				chg_err("can't rerun bc1.2, rc=%d", rc);
				chip->bc12_retry = false;
			} else {
				chip->bc12_retry = true;
			}
			break;
		default:
			chip->bc12_retry = false;
			break;
		}
		if (!chip->bc12_retry) {
			rc = sgm41512_enable_hiz_mode(chip, true);
			if (rc < 0)
				chg_err("can't enable hiz mode, rc=%d\n", rc);
			goto bc12_completed;
		}
	}

	if (chip->charge_type != data) {
		chip->charge_type = data;
		oplus_chg_ic_virq_trigger(chip->ic_dev,
					  OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
	}

	return;

bc12_completed:
	chip->charge_type = data;
	chip->bc12_complete = true;
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_BC12_COMPLETED);
}

static void sgm41512_bc12_boot_check(struct sgm41512_chip *chip)
{
	u8 data;
	int rc;

	/* set vindpm thr to 4V */
	rc = sgm41512_write_byte_mask(
		chip, VINDPM_OVP_CONFIG_REG, VINDPM_CONFIG_BIT, 0x01);
	if (rc < 0)
		chg_err("set vindpm thr error, rc=%d\n", rc);
	rc = sgm41512_write_byte_mask(
		chip, IRQ_MASK_REG, VINDPM_IRQ_MASK_BIT | IINDPM_IRQ_MASK_BIT,
		VINDPM_IRQ_MASK_BIT | IINDPM_IRQ_MASK_BIT);
	if (rc < 0)
		chg_err("disable vindpm & iindpm interrupt error, rc=%d\n", rc);

	rc = sgm41512_write_byte_mask(chip, WATCHGDOG_CONFIG_REG,
				      WATCHGDOG_CONFIG_BIT, 0x00);
	if (rc < 0)
		chg_err("disable watchdog error, rc=%d\n", rc);

	rc = sgm41512_write_byte_mask(chip, CHG_CONFIG_REG, CHG_EN_BIT, 0x00);
	if (rc < 0)
		chg_err("disable charge error, rc=%d\n", rc);

	rc = sgm41512_read_byte_mask(chip, BC12_RESULT_REG, BC12_RESULT_BIT,
				     &data);
	if (rc < 0) {
		chg_err("can't read charge type, rc=%d\n", rc);
		data = 0;
	}
	sgm41512_enable_irq(chip, true);

	/* BC1.2 result bit is bit5-7*/
	data = data >> 5;
	chg_info("chg_type=%u\n", data);
	if (data == CHARGE_TYPE_CDP) {
		chg_info("bc1.2 result is no input\n");
		chip->charge_type = CHARGE_TYPE_CDP;
		chip->bc12_complete = true;
		oplus_chg_ic_virq_trigger(chip->ic_dev,
					  OPLUS_IC_VIRQ_BC12_COMPLETED);
		return;
	}

	chip->bc12_retry = false;
	WRITE_ONCE(chip->auto_bc12, false);
	rc = sgm41512_write_byte_mask(chip, BC12_REG, BC12_RERUN_BIT,
				      BC12_RERUN_BIT);
	if (rc < 0)
		chg_err("can't rerun bc1.2, rc=%d", rc);
}

static bool sgm41512_is_first_boot(struct sgm41512_chip *chip)
{
	u8 data;
	int rc;

	rc = sgm41512_read_byte_mask(chip, WATCHGDOG_CONFIG_REG,
				     WATCHGDOG_CONFIG_BIT, &data);
	if (rc < 0) {
		chg_err("can't read watchdog info, rc=%d\n", rc);
		return false;
	}

	chg_err("watchdog=%02x\n", data);
	if (data == 0)
		return true;
	return false;
}

static void sgm41512_plugin_work(struct work_struct *work)
{
	struct sgm41512_chip *chip =
		container_of(work, struct sgm41512_chip, plugin_work);
	union mms_msg_data data = { 0 };
	int rc;

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_PRESENT, &data,
				false);
	if (chip->vbus_present != (!!data.intval)) {
		WRITE_ONCE(chip->vbus_present, !!data.intval);
		chip->bc12_complete = false;
		chg_info("vbus_present=%d, otg_mode=%d\n", chip->vbus_present,
			 chip->otg_mode);
		if (chip->vbus_present && !chip->otg_mode) {
			sgm41512_request_dpdm(chip, true);
			if (sgm41512_is_first_boot(chip)) {
				sgm41512_bc12_boot_check(chip);
			} else {
				WRITE_ONCE(chip->auto_bc12, true);
				chip->bc12_retry = false;
				sgm41512_hw_init(chip);

				rc = sgm41512_write_byte_mask(chip, BC12_REG,
							      BC12_RERUN_BIT,
							      BC12_RERUN_BIT);
				if (rc < 0)
					chg_err("can't rerun bc1.2, rc=%d", rc);
			}
		}
	}

	if (!chip->vbus_present) {
		sgm41512_enable_irq(chip, false);
		sgm41512_request_dpdm(chip, false);
		chip->charge_type = CHARGE_TYPE_NO_INPUT;
		chip->bc12_complete = false;
		return;
	}
}

static void sgm41512_bc12_timeout_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sgm41512_chip *chip =
		container_of(dwork, struct sgm41512_chip, bc12_timeout_work);
	int rc;

	chg_err("BC1.2 check timeout\n");
	rc = sgm41512_write_byte_mask(chip, HIZ_MODE_REG, HIZ_MODE_BIT,
				      HIZ_MODE_BIT);
	if (rc < 0)
		chg_err("can't enable hiz mode, rc=%d\n", rc);
}

static irqreturn_t sgm41512_event_handler(int irq, void *dev_id)
{
	struct sgm41512_chip *chip = dev_id;

	chg_debug("sgm41512 event irq\n");
	schedule_work(&chip->event_work);
	return IRQ_HANDLED;
}

struct oplus_chg_ic_virq sgm41512_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_PLUGIN },
	{ .virq_id = OPLUS_IC_VIRQ_CHG_TYPE_CHANGE },
	{ .virq_id = OPLUS_IC_VIRQ_BC12_COMPLETED },
};

static int sgm41512_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;
	return 0;
}

static int sgm41512_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	return 0;
}

static int sgm41512_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct sgm41512_chip *chip;
	u8 buf[REG_MAX + 1];
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	rc = regmap_bulk_read(chip->regmap, 0x00, buf, ARRAY_SIZE(buf));
	if (rc < 0) {
		chg_err("can't dump register, rc=%d", rc);
		return rc;
	}
	print_hex_dump(KERN_ERR, "OPLUS_CHG[SGM41512]: ", DUMP_PREFIX_OFFSET,
		       32, 1, buf, ARRAY_SIZE(buf), false);
	return 0;
}

static int sgm41512_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[],
			     int len)
{
	return 0;
}

static int sgm41512_input_present(struct oplus_chg_ic_dev *ic_dev,
				  bool *present)
{
	struct sgm41512_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*present = chip->vbus_present;

	return 0;
}

static int sgm41512_get_charger_type(struct oplus_chg_ic_dev *ic_dev, int *type)
{
	struct sgm41512_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (chip->charge_type) {
	case CHARGE_TYPE_SDP:
		*type = OPLUS_CHG_USB_TYPE_SDP;
		break;
	case CHARGE_TYPE_CDP:
		*type = OPLUS_CHG_USB_TYPE_CDP;
		break;
	case CHARGE_TYPE_DCP:
	case CHARGE_TYPE_VBUS_TYPE_UNKNOWN:
		*type = OPLUS_CHG_USB_TYPE_DCP;
		break;
	case CHARGE_TYPE_OCP:
		*type = OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID;
		break;
	default:
		*type = OPLUS_CHG_USB_TYPE_UNKNOWN;
		break;
	}

	return 0;
}

static int sgm41512_rerun_bc12(struct oplus_chg_ic_dev *ic_dev)
{
	struct sgm41512_chip *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	chg_info("rerun bc1.2\n");
	sgm41512_request_dpdm(chip, true);
	/* no need to retry */
	chip->bc12_retry = true;
	chip->auto_bc12 = false;
	chip->bc12_complete = false;
	rc = sgm41512_enable_hiz_mode(chip, false);
	if (rc < 0) {
		chg_err("can't disable hiz mode, rc=%d\n", rc);
		goto err;
	}
	rc = sgm41512_write_byte_mask(chip, BC12_REG, BC12_RERUN_BIT,
				      BC12_RERUN_BIT);
	if (rc < 0) {
		chg_err("can't rerun bc1.2, rc=%d", rc);
		goto err;
	}

	return 0;

err:
	chip->bc12_complete = true;
	return rc;
}

static int sgm41512_disable_vbus(struct oplus_chg_ic_dev *ic_dev, bool en,
				 bool delay)
{
	struct sgm41512_chip *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	chip->otg_mode = en;
	mutex_lock(&chip->pinctrl_lock);
	if (en) {
		rc = pinctrl_select_state(chip->pinctrl, chip->dis_vbus_active);
	} else {
		/* Wait for VBUS to be completely powered down, usually 20ms */
		if (delay)
			msleep(20);
		rc = pinctrl_select_state(chip->pinctrl, chip->dis_vbus_sleep);
	}
	mutex_unlock(&chip->pinctrl_lock);
	if (rc < 0)
		chg_err("can't set disable vbus gpio to %s, rc=%d\n",
			en ? "active" : "sleep", rc);
	else
		chg_err("set disable vbus gpio to %s\n",
			en ? "active" : "sleep");

	return rc;
}

static void sgm41512_wired_subs_callback(struct mms_subscribe *subs,
					 enum mms_msg_type type, u32 id)
{
	struct sgm41512_chip *chip = subs->priv_data;

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_PRESENT:
			schedule_work(&chip->plugin_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void sgm41512_subscribe_wired_topic(struct oplus_mms *topic,
					   void *prv_data)
{
	struct sgm41512_chip *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    sgm41512_wired_subs_callback, "sgm41512");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_PRESENT, &data,
				true);
	chip->vbus_present = !!data.intval;
	if (chip->vbus_present && !chip->otg_mode) {
		sgm41512_request_dpdm(chip, true);
		sgm41512_bc12_boot_check(chip);
	}
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
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       sgm41512_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       sgm41512_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       sgm41512_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       sgm41512_smt_test);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_PRESENT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_PRESENT,
					       sgm41512_input_present);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE,
			sgm41512_get_charger_type);
		break;
	case OPLUS_IC_FUNC_BUCK_RERUN_BC12:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_RERUN_BC12,
					       sgm41512_rerun_bc12);
		break;
	case OPLUS_IC_FUNC_DISABLE_VBUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_DISABLE_VBUS,
					       sgm41512_disable_vbus);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static int sgm41512_gpio_init(struct sgm41512_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	chip->dis_vbus_gpio = of_get_named_gpio(node, "oplus,dis_vbus-gpio", 0);
	if (!gpio_is_valid(chip->dis_vbus_gpio)) {
		chg_err("dis_vbus_gpio not specified\n");
		return -ENODEV;
	}
	rc = gpio_request(chip->dis_vbus_gpio, "sgm41512_dis_vbus-gpio");
	if (rc < 0) {
		chg_err("event_gpio request error, rc=%d\n", rc);
		return rc;
	}
	chip->dis_vbus_active =
		pinctrl_lookup_state(chip->pinctrl, "dis_vbus_active");
	if (IS_ERR_OR_NULL(chip->dis_vbus_active)) {
		chg_err("get dis_vbus_active fail\n");
		goto free_dis_vbus_gpio;
	}
	chip->dis_vbus_sleep =
		pinctrl_lookup_state(chip->pinctrl, "dis_vbus_sleep");
	if (IS_ERR_OR_NULL(chip->dis_vbus_sleep)) {
		chg_err("get dis_vbus_sleep fail\n");
		goto free_dis_vbus_gpio;
	}
	pinctrl_select_state(chip->pinctrl, chip->dis_vbus_sleep);

	chip->event_gpio = of_get_named_gpio(node, "oplus,event-gpio", 0);
	if (!gpio_is_valid(chip->event_gpio)) {
		chg_err("event_gpio not specified\n");
		rc = -ENODEV;
		goto free_dis_vbus_gpio;
	}
	rc = gpio_request(chip->event_gpio, "sgm41512_event-gpio");
	if (rc < 0) {
		chg_err("event_gpio request error, rc=%d\n", rc);
		goto free_dis_vbus_gpio;
	}
	chip->event_default =
		pinctrl_lookup_state(chip->pinctrl, "event_default");
	if (IS_ERR_OR_NULL(chip->event_default)) {
		chg_err("get event_default fail\n");
		goto free_event_gpio;
	}
	gpio_direction_input(chip->event_gpio);
	pinctrl_select_state(chip->pinctrl, chip->event_default);
	chip->event_irq = gpio_to_irq(chip->event_gpio);
	rc = devm_request_irq(chip->dev, chip->event_irq,
			      sgm41512_event_handler, IRQF_TRIGGER_FALLING,
			      "sgm41512_event-irq", chip);
	if (rc < 0) {
		chg_err("event_irq request error, rc=%d\n", rc);
		goto free_event_gpio;
	}
	chip->event_irq_enabled = true;
	sgm41512_enable_irq(chip, false);

	return 0;

free_event_gpio:
	if (gpio_is_valid(chip->event_gpio))
		gpio_free(chip->event_gpio);
free_dis_vbus_gpio:
	if (gpio_is_valid(chip->dis_vbus_gpio))
		gpio_free(chip->dis_vbus_gpio);

	return rc;
}

static int sgm41512_hw_init(struct sgm41512_chip *chip)
{
	int rc;

	/* set vindpm thr to 4V */
	rc = sgm41512_write_byte_mask(
		chip, VINDPM_OVP_CONFIG_REG, VINDPM_CONFIG_BIT, 0x01);
	if (rc < 0)
		chg_err("set vindpm thr error, rc=%d\n", rc);
	rc = sgm41512_write_byte_mask(
		chip, IRQ_MASK_REG, VINDPM_IRQ_MASK_BIT | IINDPM_IRQ_MASK_BIT,
		VINDPM_IRQ_MASK_BIT | IINDPM_IRQ_MASK_BIT);
	if (rc < 0)
		chg_err("disable vindpm & iindpm interrupt error, rc=%d\n", rc);
	rc = sgm41512_write_byte_mask(chip, WATCHGDOG_CONFIG_REG,
				      WATCHGDOG_CONFIG_BIT, 0x00);
	if (rc < 0)
		chg_err("disable watchdog error, rc=%d\n", rc);
	rc = sgm41512_write_byte_mask(chip, CHG_CONFIG_REG, CHG_EN_BIT, 0x00);
	if (rc < 0)
		chg_err("disable charge error, rc=%d\n", rc);
	rc = sgm41512_enable_hiz_mode(chip, false);
	if (rc < 0)
		chg_err("can't disable hiz mode, rc=%d\n", rc);

	sgm41512_enable_irq(chip, true);

	return 0;
}

static int sgm41512_driver_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int rc = 0;
	struct sgm41512_chip *chip;
	struct device_node *node = client->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;

	chip = devm_kzalloc(&client->dev, sizeof(struct sgm41512_chip),
			    GFP_KERNEL);
	if (!chip) {
		chg_err("kzalloc failed\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);
	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->dpdm_lock);
	mutex_init(&chip->pinctrl_lock);
	INIT_WORK(&chip->event_work, sgm41512_event_work);
	INIT_WORK(&chip->plugin_work, sgm41512_plugin_work);
	INIT_DELAYED_WORK(&chip->bc12_timeout_work, sgm41512_bc12_timeout_work);

	chip->dpdm_reg = devm_regulator_get_optional(chip->dev, "dpdm");
	if (IS_ERR(chip->dpdm_reg)) {
		rc = PTR_ERR(chip->dpdm_reg);
		chg_err("Couldn't get dpdm regulator, rc=%d\n", rc);
		chip->dpdm_reg = NULL;
	}

	chip->regmap = devm_regmap_init_i2c(client, &sgm41512_regmap_config);
	if (!chip->regmap) {
		rc = -ENODEV;
		goto regmap_init_err;
	}

	atomic_set(&chip->charger_suspended, 0);
	rc = sgm41512_gpio_init(chip);
	if (rc < 0) {
		chg_err("gpio init error, rc=%d\n", rc);
		goto gpio_init_err;
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
	sprintf(ic_cfg.manu_name, "BC1.2-SGM41512");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = sgm41512_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(sgm41512_virq_table);
	chip->ic_dev =
		devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}

	oplus_mms_wait_topic("wired", sgm41512_subscribe_wired_topic, chip);

	chg_debug("success\n");

	return rc;

reg_ic_err:
	sgm41512_enable_irq(chip, false);
	if (gpio_is_valid(chip->event_gpio))
		gpio_free(chip->event_gpio);
gpio_init_err:
regmap_init_err:
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, chip);
	chg_err("probe error, rc=%d\n", rc);
	return rc;
}

static struct i2c_driver sgm41512_i2c_driver;

static int sgm41512_driver_remove(struct i2c_client *client)
{
	struct sgm41512_chip *chip = i2c_get_clientdata(client);

	sgm41512_enable_irq(chip, false);
	if (gpio_is_valid(chip->event_gpio))
		gpio_free(chip->event_gpio);
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, chip);
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int sgm41512_pm_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct sgm41512_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);
	return 0;
}

static int sgm41512_pm_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct sgm41512_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 1);
	return 0;
}

static const struct dev_pm_ops sgm41512_pm_ops = {
	.resume = sgm41512_pm_resume,
	.suspend = sgm41512_pm_suspend,
};
#else
static int sgm41512_resume(struct i2c_client *client)
{
	struct sgm41512_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);
	return 0;
}

static int sgm41512_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct sgm41512_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 1);
	return 0;
}
#endif

static void sgm41512_shutdown(struct i2c_client *client)
{
	struct sgm41512_chip *chip = i2c_get_clientdata(client);

	/*
	 * HIZ mode needs to be disabled on shutdown to ensure activation
	 * signal is available.
	 */
	if (READ_ONCE(chip->vbus_present))
		sgm41512_write_byte_mask(chip, HIZ_MODE_REG, HIZ_MODE_BIT, 0);
}

static const struct of_device_id sgm41512_match[] = {
	{ .compatible = "oplus,sgm41512-charger" },
	{},
};

static const struct i2c_device_id sgm41512_id[] = {
	{ "sgm41512-charger", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm41512_id);

static struct i2c_driver sgm41512_i2c_driver = {
	.driver		= {
		.name = "sgm41512-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sgm41512_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &sgm41512_pm_ops,
#endif
	},
	.probe		= sgm41512_driver_probe,
	.remove		= sgm41512_driver_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= sgm41512_resume,
	.suspend	= sgm41512_suspend,
#endif
	.shutdown	= sgm41512_shutdown,
	.id_table	= sgm41512_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(sgm41512_i2c_driver);
#else
int sgm41512_driver_init(void)
{
	int rc;

	rc = i2c_add_driver(&sgm41512_i2c_driver);
	if (rc < 0)
		chg_err("failed to register sgm41512 i2c driver, rc=%d\n", rc);
	else
		chg_debug("Success to register sgm41512 i2c driver.\n");

	return rc;
}

void sgm41512_driver_exit(void)
{
	i2c_del_driver(&sgm41512_i2c_driver);
}
oplus_chg_module_register(sgm41512_driver);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/
MODULE_DESCRIPTION("Driver for sgm41512 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:sgm41512-charger");
