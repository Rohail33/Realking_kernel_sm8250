// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[P9415]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/sched/clock.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include "../../oplus_chg_core.h"
#endif
#include "../../oplus_chg_module.h"
#include "../oplus_chg_wls.h"
#include "../hal/oplus_chg_ic.h"
#include "oplus_p9415_fw.h"
#include "oplus_p9415_reg.h"
#include "../oplus_chg_track.h"

#ifndef I2C_ERR_MAX
#define I2C_ERR_MAX 6
#endif

#define LDO_ON_MV	4200

#define P9415_CHECK_LDO_ON_DELAY round_jiffies_relative(msecs_to_jiffies(2000))

static atomic_t i2c_err_count;

struct rx_msg_struct {
	void (*msg_call_back)(void *dev_data, u8 data[]);
	void *dev_data;
};

struct oplus_p9415 {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;

	int rx_con_gpio;
	int rx_con_irq;
	int rx_event_gpio;
	int rx_event_irq;
	int rx_en_gpio;
	int rx_mode_gpio;

	bool fw_updating;
	bool check_fw_update;
	bool connected_ldo_on;

	bool rx_connected;
	int adapter_type;
	int rx_pwr_cap;
	bool support_epp_11w;

	unsigned char *fw_data;
	int fw_length;
	u32 fw_boot_version;
	u32 fw_rx_version;
	u32 fw_tx_version;

	struct mutex i2c_lock;

	struct pinctrl *pinctrl;
	struct pinctrl_state *rx_con_default;
	struct pinctrl_state *rx_event_default;
	struct pinctrl_state *rx_en_active;
	struct pinctrl_state *rx_en_sleep;
	struct pinctrl_state *rx_mode_default;

	struct delayed_work update_work;
	struct delayed_work event_work;
	struct delayed_work connect_work;
	struct delayed_work check_ldo_on_work;
	struct delayed_work check_event_work;

	struct regmap *regmap;
	struct oplus_chg_mod *wls_ocm;
	struct rx_msg_struct rx_msg;
	struct completion ldo_on;

	char chg_power_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	char err_reason[OPLUS_CHG_TRACK_DEVICE_ERR_NAME_LEN];
	char wls_crux_info[OPLUS_CHG_TRACK_CURX_INFO_LEN];
	struct mutex track_upload_lock;

	struct mutex track_i2c_err_lock;
	u32 debug_force_i2c_err;
	bool i2c_err_uploading;
	oplus_chg_track_trigger *i2c_err_load_trigger;
	struct delayed_work i2c_err_load_trigger_work;

	struct mutex track_rx_err_lock;
	u32 debug_force_rx_err;
	bool rx_err_uploading;
	oplus_chg_track_trigger *rx_err_load_trigger;
	struct delayed_work rx_err_load_trigger_work;

	struct mutex track_tx_err_lock;
	u32 debug_force_tx_err;
	bool tx_err_uploading;
	oplus_chg_track_trigger *tx_err_load_trigger;
	struct delayed_work tx_err_load_trigger_work;

	struct mutex track_update_err_lock;
	u32 debug_force_update_err;
	bool update_err_uploading;
	oplus_chg_track_trigger *update_err_load_trigger;
	struct delayed_work update_err_load_trigger_work;

	u32 debug_force_upload_period;
};

static int p9415_track_upload_i2c_err_info(
	struct oplus_p9415 *chip, int err_type, u16 reg);

static bool p9415_rx_is_connected(struct oplus_chg_ic_dev *dev)
{
	struct oplus_p9415 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (!gpio_is_valid(chip->rx_con_gpio)) {
		pr_err("rx_con_gpio invalid\n");
		return false;
	}

	return !!gpio_get_value(chip->rx_con_gpio);
}

static bool is_wls_ocm_available(struct oplus_p9415 *dev)
{
	if (!dev->wls_ocm)
		dev->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!dev->wls_ocm;
}

static int p9415_get_wls_type(struct oplus_p9415 *chip)
{
	int rc = 0;
	union oplus_chg_mod_propval pval = {0, };

	if (!is_wls_ocm_available(chip)) {
		return OPLUS_CHG_WLS_UNKNOWN;
	}

	rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &pval);
	if (rc < 0)
		return OPLUS_CHG_WLS_UNKNOWN;

	return pval.intval;
}

static __inline__ void p9415_i2c_err_inc(struct oplus_p9415 *chip)
{
	if (p9415_rx_is_connected(chip->ic_dev) && (atomic_inc_return(&i2c_err_count) > I2C_ERR_MAX)) {
		atomic_set(&i2c_err_count, 0);
		pr_err("read i2c error\n");
		oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_RX_IIC_ERR);
		// todo, add i2c error callback
	}
}

static __inline__ void p9415_i2c_err_clr(void)
{
	atomic_set(&i2c_err_count, 0);
}

static int p9415_read_byte(struct oplus_p9415 *chip, u16 addr, u8 *data)
{
	char cmd_buf[2] = { addr >> 8, addr & 0xff };
	int rc = 0;

	mutex_lock(&chip->i2c_lock);
	rc = i2c_master_send(chip->client, cmd_buf, 2);
	if (rc < 2) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		p9415_track_upload_i2c_err_info(chip, rc, addr);
		goto error;
	}

	rc = i2c_master_recv(chip->client, data, 1);
	if (rc < 1) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	p9415_i2c_err_clr();
	if (chip->debug_force_i2c_err)
		p9415_track_upload_i2c_err_info(chip, rc, addr);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	p9415_i2c_err_inc(chip);
	return rc;
}

static int p9415_read_data(struct oplus_p9415 *chip, u16 addr,
			   u8 *buf, int len)
{
	char cmd_buf[2] = { addr >> 8, addr & 0xff };
	int rc = 0;

	mutex_lock(&chip->i2c_lock);
	rc = i2c_master_send(chip->client, cmd_buf, 2);
	if (rc < 2) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		p9415_track_upload_i2c_err_info(chip, rc, addr);
		goto error;
	}

	rc = i2c_master_recv(chip->client, buf, len);
	if (rc < len) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		p9415_track_upload_i2c_err_info(chip, rc, addr);
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	p9415_i2c_err_clr();
	if (chip->debug_force_i2c_err)
		p9415_track_upload_i2c_err_info(chip, rc, addr);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	p9415_i2c_err_inc(chip);
	return rc;
}

static int p9415_write_byte(struct oplus_p9415 *chip, u16 addr, u8 data)
{
	u8 buf_temp[3] = { addr >> 8, addr & 0xff, data };
	int rc;

	mutex_lock(&chip->i2c_lock);
	rc = i2c_master_send(chip->client, buf_temp, 3);
	if (rc < 3) {
		pr_err("write 0x%04x error, rc=%d\n", addr, rc);
		mutex_unlock(&chip->i2c_lock);
		p9415_i2c_err_inc(chip);
		rc = rc < 0 ? rc : -EIO;
		p9415_track_upload_i2c_err_info(chip, rc, addr);
		return rc;
	}
	mutex_unlock(&chip->i2c_lock);
	p9415_i2c_err_clr();

	return 0;
}

static int p9415_write_data(struct oplus_p9415 *chip, u16 addr,
			   u8 *buf, int len)
{
	u8 *buf_temp;
	int i;
	int rc;

	buf_temp = kzalloc(len + 2, GFP_KERNEL);
	if (!buf_temp) {
		pr_err("alloc memary error\n");
		return -ENOMEM;
	}

	buf_temp[0] = addr >> 8;
	buf_temp[1] = addr & 0xff;
	for (i = 0; i < len; i++)
		buf_temp[i + 2] = buf[i];

	mutex_lock(&chip->i2c_lock);
	rc = i2c_master_send(chip->client, buf_temp, len + 2);
	if (rc < 3) {
		pr_err("write 0x%04x error, rc=%d\n", addr, rc);
		mutex_unlock(&chip->i2c_lock);
		kfree(buf_temp);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&chip->i2c_lock);
	kfree(buf_temp);

	return 0;
}

__maybe_unused static int p9415_read_byte_mask(struct oplus_p9415 *chip, u16 addr,
				u8 mask, u8 *data)
{
	u8 temp;
	int rc;

	rc = p9415_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;

	*data = mask & temp;

	return 0;
}

static int p9415_write_byte_mask(struct oplus_p9415 *chip, u16 addr,
				 u8 mask, u8 data)
{
	u8 temp;
	int rc;

	rc = p9415_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;
	temp = (data & mask) | (temp & (~mask));
	rc = p9415_write_byte(chip, addr, temp);
	if (rc < 0)
		return rc;

	return 0;
}

#ifdef WLS_QI_DEBUG
static int p9415_write_reg = 0;
static int p9415_read_reg = 0;
static ssize_t p9415_write_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	u8 reg_val = 0;
	struct oplus_p9415 *chip = dev_get_drvdata(dev);

	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL");
		return -EINVAL;
	}
	p9415_read_byte(chip, (u16)p9415_write_reg, &reg_val);
	count += snprintf(buf+count, PAGE_SIZE - count, "reg[0x%02x]=0x%02x\n", p9415_write_reg, reg_val);

	return count;
}

static ssize_t p9415_write_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int databuf[2] = {0, 0};
	struct oplus_p9415 *chip = dev_get_drvdata(dev);

	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL");
		return -EINVAL;
	}
	if (2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
		pr_err("%s: reg[0x%02x]=0x%02x\n", __FUNCTION__, databuf[0], databuf[1]);
		p9415_write_reg = databuf[0];
		p9415_write_byte(chip, (u16)databuf[0], (u8)databuf[1]);
	}

	return count;
}

static ssize_t p9415_read_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	u8 reg_val = 0;
	struct oplus_p9415 *chip = dev_get_drvdata(dev);

	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL");
		return -EINVAL;
	}

	p9415_read_byte(chip, (u16)p9415_read_reg, &reg_val);
	count += snprintf(buf+count, PAGE_SIZE - count, "reg[0x%02x]=0x%02x\n", p9415_read_reg, reg_val);

	return count;
}

static ssize_t p9415_read_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int databuf[2] = {0, 0};
	struct oplus_p9415 *chip = dev_get_drvdata(dev);

	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL");
		return -EINVAL;
	}

	if (1 == sscanf(buf, "%x", &databuf[0])) {
		p9415_read_reg = databuf[0];
	}

	return count;
}

static DEVICE_ATTR(write_reg, S_IWUSR | S_IRUGO, p9415_write_reg_show, p9415_write_reg_store);
static DEVICE_ATTR(read_reg, S_IWUSR | S_IRUGO, p9415_read_reg_show, p9415_read_reg_store);

static struct attribute *p9415_attributes[] = {
	&dev_attr_write_reg.attr,
	&dev_attr_read_reg.attr,
	NULL
};

static struct attribute_group p9415_attribute_group = {
	.attrs = p9415_attributes
};
#endif /*WLS_QI_DEBUG*/

#define TRACK_LOCAL_T_NS_TO_S_THD		1000000000
#define TRACK_UPLOAD_COUNT_MAX		10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD	(24 * 3600)
static int p9415_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int p9415_track_upload_i2c_err_info(
	struct oplus_p9415 *chip, int err_type, u16 reg)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	curr_time = p9415_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (chip->debug_force_upload_period > 0 &&
	    curr_time - pre_upload_time > chip->debug_force_upload_period)
		upload_count = 0;

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->debug_force_i2c_err)
		err_type = -chip->debug_force_i2c_err;

	mutex_lock(&chip->track_i2c_err_lock);
	if (chip->i2c_err_uploading) {
		pr_info("i2c_err_uploading, should return\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->i2c_err_load_trigger)
		kfree(chip->i2c_err_load_trigger);
	chip->i2c_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->i2c_err_load_trigger) {
		pr_err("i2c_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->i2c_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->i2c_err_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
	chip->i2c_err_uploading = true;
	upload_count++;
	pre_upload_time = p9415_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(
		&(chip->i2c_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"p9415");
	index += snprintf(
		&(chip->i2c_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		OPLUS_CHG_TRACK_SCENE_I2C_ERR);

	oplus_chg_track_get_i2c_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(
		&(chip->i2c_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->chg_power_info);
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"$$access_reg@@0x%x", reg);
	schedule_delayed_work(&chip->i2c_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void p9415_track_i2c_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, i2c_err_load_trigger_work);

	if (!chip->i2c_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	if (chip->i2c_err_load_trigger) {
		kfree(chip->i2c_err_load_trigger);
		chip->i2c_err_load_trigger = NULL;
	}
	chip->i2c_err_uploading = false;
}

static int p9415_track_upload_wls_rx_err_info(
	struct oplus_p9415 *chip, int err_type)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	pr_info("start\n");
	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	memset(chip->wls_crux_info, 0, sizeof(chip->wls_crux_info));
	curr_time = p9415_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (chip->debug_force_upload_period > 0 &&
	    curr_time - pre_upload_time > chip->debug_force_upload_period)
		upload_count = 0;

	if (err_type == TRACK_WLS_TRX_ERR_DEFAULT) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	mutex_lock(&chip->track_rx_err_lock);
	if (chip->rx_err_uploading) {
		pr_info("rx_err_uploading, should return\n");
		mutex_unlock(&chip->track_rx_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->rx_err_load_trigger)
		kfree(chip->rx_err_load_trigger);
	chip->rx_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->rx_err_load_trigger) {
		pr_err("rx_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_rx_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->rx_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->rx_err_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
	chip->rx_err_uploading = true;
	upload_count++;
	pre_upload_time = p9415_track_get_local_time_s();
	mutex_unlock(&chip->track_rx_err_lock);

	index += snprintf(
		&(chip->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"p9415");
	index += snprintf(
		&(chip->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		OPLUS_CHG_TRACK_SCENE_WLS_RX_ERR);

	oplus_chg_track_get_wls_trx_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(
		&(chip->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->rx_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->chg_power_info);
	oplus_chg_track_obtain_wls_general_crux_info(
		chip->wls_crux_info, sizeof(chip->wls_crux_info));
	index += snprintf(&(chip->rx_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"%s", chip->wls_crux_info);
	schedule_delayed_work(&chip->rx_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void p9415_track_rx_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, rx_err_load_trigger_work);

	if (!chip->rx_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->rx_err_load_trigger));
	if (chip->rx_err_load_trigger) {
		kfree(chip->rx_err_load_trigger);
		chip->rx_err_load_trigger = NULL;
	}
	chip->rx_err_uploading = false;
}

static int p9415_track_upload_wls_tx_err_info(
	struct oplus_p9415 *chip, int err_type)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	memset(chip->wls_crux_info, 0, sizeof(chip->wls_crux_info));
	curr_time = p9415_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (chip->debug_force_upload_period > 0 &&
	    curr_time - pre_upload_time > chip->debug_force_upload_period)
		upload_count = 0;

	if (err_type == TRACK_WLS_TRX_ERR_DEFAULT) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	mutex_lock(&chip->track_tx_err_lock);
	if (chip->tx_err_uploading) {
		pr_info("tx_err_uploading, should return\n");
		mutex_unlock(&chip->track_tx_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->tx_err_load_trigger)
		kfree(chip->tx_err_load_trigger);
	chip->tx_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->tx_err_load_trigger) {
		pr_err("tx_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_tx_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->tx_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->tx_err_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
	chip->tx_err_uploading = true;
	upload_count++;
	pre_upload_time = p9415_track_get_local_time_s();
	mutex_unlock(&chip->track_tx_err_lock);

	index += snprintf(
		&(chip->tx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"p9415");
	index += snprintf(
		&(chip->tx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		OPLUS_CHG_TRACK_SCENE_WLS_TX_ERR);

	oplus_chg_track_get_wls_trx_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(
		&(chip->tx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->tx_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->chg_power_info);
	oplus_chg_track_obtain_wls_general_crux_info(
		chip->wls_crux_info, sizeof(chip->wls_crux_info));
	index += snprintf(&(chip->tx_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			"%s", chip->wls_crux_info);
	schedule_delayed_work(&chip->tx_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void p9415_track_tx_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, tx_err_load_trigger_work);

	if (!chip->tx_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->tx_err_load_trigger));
	if (chip->tx_err_load_trigger) {
		kfree(chip->tx_err_load_trigger);
		chip->tx_err_load_trigger = NULL;
	}
	chip->tx_err_uploading = false;
}

static int p9415_track_upload_wls_update_err_info(
	struct oplus_p9415 *chip, int err_type)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	if (!chip)
		return -EINVAL;

	mutex_lock(&chip->track_upload_lock);
	memset(chip->chg_power_info, 0, sizeof(chip->chg_power_info));
	memset(chip->err_reason, 0, sizeof(chip->err_reason));
	curr_time = p9415_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (chip->debug_force_upload_period > 0 &&
	    curr_time - pre_upload_time > chip->debug_force_upload_period)
		upload_count = 0;

	if (err_type == TRACK_WLS_TRX_ERR_DEFAULT) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	mutex_lock(&chip->track_update_err_lock);
	if (chip->update_err_uploading) {
		pr_info("update_err_uploading, should return\n");
		mutex_unlock(&chip->track_update_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->update_err_load_trigger)
		kfree(chip->update_err_load_trigger);
	chip->update_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->update_err_load_trigger) {
		pr_err("update_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_update_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->update_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->update_err_load_trigger->flag_reason =
		TRACK_NOTIFY_FLAG_WLS_TRX_ABNORMAL;
	chip->update_err_uploading = true;
	upload_count++;
	pre_upload_time = p9415_track_get_local_time_s();
	mutex_unlock(&chip->track_update_err_lock);

	index += snprintf(
		&(chip->update_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"p9415");
	index += snprintf(
		&(chip->update_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
		OPLUS_CHG_TRACK_SCENE_WLS_UPDATE_ERR);

	oplus_chg_track_get_wls_trx_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(
		&(chip->update_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
		"$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->update_err_load_trigger->crux_info[index]),
			OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->chg_power_info);
	schedule_delayed_work(&chip->update_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void p9415_track_update_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, update_err_load_trigger_work);

	if (!chip->update_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->update_err_load_trigger));
	if (chip->update_err_load_trigger) {
		kfree(chip->update_err_load_trigger);
		chip->update_err_load_trigger = NULL;
	}
	chip->update_err_uploading = false;
}

static int p9415_track_debugfs_init(struct oplus_p9415 *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_p9415;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_p9415 = debugfs_create_dir("p9415", debugfs_root);
	if (!debugfs_p9415) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_rx_err = TRACK_WLS_TRX_ERR_DEFAULT;
	chip->debug_force_tx_err = TRACK_WLS_TRX_ERR_DEFAULT;
	chip->debug_force_update_err = TRACK_WLS_TRX_ERR_DEFAULT;
	chip->debug_force_upload_period = 0;
	debugfs_create_u32("debug_force_i2c_err", 0644,
	    debugfs_p9415, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_rx_err", 0644,
	    debugfs_p9415, &(chip->debug_force_rx_err));
	debugfs_create_u32("debug_force_tx_err", 0644,
	    debugfs_p9415, &(chip->debug_force_tx_err));
	debugfs_create_u32("debug_force_update_err", 0644,
	    debugfs_p9415, &(chip->debug_force_update_err));
	debugfs_create_u32("debug_force_upload_period", 0644,
	    debugfs_p9415, &(chip->debug_force_upload_period));

	return ret;
}

static int p9415_track_init(struct oplus_p9415 *chip)
{
	int rc;

	if (!chip)
		return - EINVAL;

	mutex_init(&chip->track_i2c_err_lock);
	mutex_init(&chip->track_rx_err_lock);
	mutex_init(&chip->track_tx_err_lock);
	mutex_init(&chip->track_update_err_lock);
	mutex_init(&chip->track_upload_lock);
	chip->i2c_err_uploading = false;
	chip->i2c_err_load_trigger = NULL;
	chip->rx_err_uploading = false;
	chip->rx_err_load_trigger = NULL;
	chip->tx_err_uploading = false;
	chip->tx_err_load_trigger = NULL;
	chip->update_err_uploading = false;
	chip->update_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work,
				p9415_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->rx_err_load_trigger_work,
				p9415_track_rx_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->tx_err_load_trigger_work,
				p9415_track_tx_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->update_err_load_trigger_work,
				p9415_track_update_err_load_trigger_work);
	rc = p9415_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("p9415 debugfs init error, rc=%d\n", rc);
	}

	return rc;
}

static int p9415_set_trx_boost_enable(struct oplus_p9415 *chip, bool en)
{
	union oplus_chg_mod_propval pval;

	if (!is_wls_ocm_available(chip)) {
		pr_err("wls ocm not found\n");
		return -ENODEV;
	}

	pval.intval = en;
	return oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_POWER_EN, &pval);
}

static int p9415_set_trx_boost_vol(struct oplus_p9415 *chip, int vol_mv)
{
	union oplus_chg_mod_propval pval;

	if (!is_wls_ocm_available(chip)) {
		pr_err("wls ocm not found\n");
		return -ENODEV;
	}

	pval.intval = vol_mv;
	return oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_POWER_VOL, &pval);
}

__maybe_unused static int p9415_set_trx_boost_curr_limit(struct oplus_p9415 *chip, int curr_ma)
{
	union oplus_chg_mod_propval pval;

	if (!is_wls_ocm_available(chip)) {
		pr_err("wls ocm not found\n");
		return -ENODEV;
	}

	pval.intval = curr_ma;
	return oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT, &pval);
}

__maybe_unused static int p9415_get_rx_event_gpio_val(struct oplus_p9415 *chip)
{
	if (!gpio_is_valid(chip->rx_event_gpio)) {
		pr_err("rx_event_gpio invalid\n");
		return -ENODEV;
	}

	return gpio_get_value(chip->rx_event_gpio);
}

static int p9415_set_rx_enable(struct oplus_chg_ic_dev *dev, bool en)
{
	struct oplus_p9415 *chip;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->rx_en_active) ||
	    IS_ERR_OR_NULL(chip->rx_en_sleep)) {
		pr_err("rx_en pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(chip->pinctrl,
		en ? chip->rx_en_active : chip->rx_en_sleep);
	if (rc < 0)
		pr_err("can't %s rx\n", en ? "enable" : "disable");
	else
		pr_info("set rx %s\n", en ? "enable" : "disable");

	pr_err("vt_sleep: set value:%d, gpio_val:%d\n", !en, gpio_get_value(chip->rx_en_gpio));

	return rc;
}

static bool p9415_rx_is_enable(struct oplus_chg_ic_dev *dev)
{
	struct oplus_p9415 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (!gpio_is_valid(chip->rx_en_gpio)) {
		pr_err("rx_en_gpio invalid\n");
		return false;
	}
	/* When rx_en_gpio is low, RX is enabled;
	 * when rx_en_gpio is high, RX is sleeps;
	 * the "negation" operation to obtain the appropriate value;
	 */
	return !gpio_get_value(chip->rx_en_gpio);
}

static int p9415_get_vout(struct oplus_chg_ic_dev *dev, int *vout)
{
	struct oplus_p9415 *chip;
	char val_buf[2] = { 0, 0 };
	int temp;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_VOUT_R, val_buf, 2);
	if (rc < 0) {
		pr_err("read vout err, rc=%d\n", rc);
		return rc;
	}
	temp = val_buf[0] | val_buf[1] << 8;
	*vout = temp * 21000 / 4095;

	return 0;
}

static int p9415_set_vout(struct oplus_chg_ic_dev *dev, int vout)
{
	struct oplus_p9415 *chip;
	char val_buf[2];
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	val_buf[0] = vout & 0x00FF;
	val_buf[1] = (vout & 0xFF00) >> 8;

	rc = p9415_write_data(chip, P9415_REG_VOUT_W, val_buf, 2);
	if (rc < 0) {
		pr_err("set vout err, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int p9415_get_vrect(struct oplus_chg_ic_dev *dev, int *vrect)
{
	struct oplus_p9415 *chip;
	char val_buf[2] = { 0, 0 };
	int temp;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_VRECT, val_buf, 2);
	if (rc < 0) {
		pr_err("read vrect err, rc=%d\n", rc);
		return rc;
	}
	temp = val_buf[0] | val_buf[1] << 8;
	*vrect = temp * 26250 / 4095;

	return 0;
}

static int p9415_get_iout(struct oplus_chg_ic_dev *dev, int *iout)
{
	struct oplus_p9415 *chip;
	char val_buf[2] = { 0, 0 };
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_TOUT, val_buf, 2);
	if (rc < 0) {
		pr_err("read iout err, rc=%d\n", rc);
		return rc;
	}
	*iout = val_buf[0] | val_buf[1] << 8;

	return 0;
}

static int p9415_get_trx_vol(struct oplus_chg_ic_dev *dev, int *vol)
{
	struct oplus_p9415 *chip;
	char val_buf[2] = { 0, 0 };
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_TRX_VOL, val_buf, 2);
	if (rc < 0) {
		pr_err("read trx vol err, rc=%d\n", rc);
		return rc;
	}
	*vol = val_buf[0] | val_buf[1] << 8;

	return 0;
}

static int p9415_get_trx_curr(struct oplus_chg_ic_dev *dev, int *curr)
{
	struct oplus_p9415 *chip;
	char val_buf[2] = { 0, 0 };
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_TRX_CURR, val_buf, 2);
	if (rc < 0) {
		pr_err("read trx current err, rc=%d\n", rc);
		return rc;
	}
	*curr = val_buf[0] | val_buf[1] << 8;

	return 0;
}

static int p9415_get_cep_count(struct oplus_chg_ic_dev *dev, int *count)
{
	struct oplus_p9415 *chip;
	char val_buf[2] = { 0, 0 };
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_CEP_COUNT, val_buf, 2);
	if (rc < 0) {
		pr_err("Couldn't read cep change status, rc=%d\n", rc);
		return rc;
	}
	*count = val_buf[0] | val_buf[1] << 8;

	return 0;
}

static int p9415_get_cep_val(struct oplus_chg_ic_dev *dev, int *val)
{
	struct oplus_p9415 *chip;
	int rc;
	char temp = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_CEP, &temp, 1);
	if (rc < 0) {
		pr_err("Couldn't read CEP, rc = %x\n", rc);
		return rc;
	}
	*val = (signed char)temp;

	return rc;
}


static int p9415_get_work_freq(struct oplus_chg_ic_dev *dev, int *val)
{
	struct oplus_p9415 *chip;
	int rc;
	char temp;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_data(chip, P9415_REG_FREQ, &temp, 1);
	if (rc < 0) {
		pr_err("Couldn't read rx freq val, rc = %d\n", rc);
		return rc;
	}
	*val = (int)temp;
	return rc;
}

static int p9415_get_rx_mode(struct oplus_chg_ic_dev *dev, enum oplus_chg_wls_rx_mode *rx_mode)
{
	struct oplus_p9415 *chip;
	int rc;
	char temp;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (chip->adapter_type == 0) {
		rc = p9415_read_data(chip, P9415_REG_RX_MODE, &temp, 1);
		if (rc < 0) {
			pr_err("Couldn't read rx mode, rc=%d\n", rc);
			return rc;
		}
		if (temp == P9415_RX_MODE_EPP) {
			pr_err("rx running in EPP!\n");
			chip->adapter_type = P9415_RX_MODE_EPP;
		} else if (temp == P9415_RX_MODE_BPP) {
			pr_err("rx running in BPP!\n");
			chip->adapter_type = P9415_RX_MODE_BPP;
		} else {
			pr_err("rx running in Others!\n");
			chip->adapter_type = 0;
		}
	}
	if (chip->adapter_type == P9415_RX_MODE_EPP) {
		if (chip->rx_pwr_cap == 0) {
			rc = p9415_read_data(chip, P9415_REG_RX_PWR, &temp, 1);
			if (rc < 0) {
				pr_err("Couldn't read rx pwr, rc = %d\n",rc);
				return rc;
			}
			pr_err("running mode epp-%d/2w\n", temp);
			if (!chip->support_epp_11w && temp >= P9415_RX_PWR_15W)
				chip->rx_pwr_cap = P9415_RX_PWR_15W;
			else if (chip->support_epp_11w && temp >= P9415_RX_PWR_11W)
				chip->rx_pwr_cap = P9415_RX_PWR_11W;
			else if (temp >= P9415_RX_PWR_10W)
				chip->rx_pwr_cap = P9415_RX_PWR_10W;
			else
				chip->rx_pwr_cap = P9415_RX_PWR_5W;
		}
		if (chip->rx_pwr_cap == P9415_RX_PWR_15W ||
		    chip->rx_pwr_cap == P9415_RX_PWR_11W) {
			*rx_mode = OPLUS_CHG_WLS_RX_MODE_EPP_PLUS;
		} else if (chip->rx_pwr_cap == P9415_RX_PWR_10W) {
			*rx_mode = OPLUS_CHG_WLS_RX_MODE_EPP;
		} else {
			*rx_mode = OPLUS_CHG_WLS_RX_MODE_EPP_5W;
		}
	} else if (chip->adapter_type == P9415_RX_MODE_BPP) {
		*rx_mode = OPLUS_CHG_WLS_RX_MODE_BPP;
	} else {
		*rx_mode = OPLUS_CHG_WLS_RX_MODE_UNKNOWN;
	}
	pr_debug("rx_mode=%d\n", *rx_mode);

	return 0;
}

static int p9415_set_dcdc_enable(struct oplus_chg_ic_dev *dev, bool en)
{
	struct oplus_p9415 *chip;
	int rc;

	if (en == false)
		return 0;
	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_write_byte_mask(chip, P9415_REG_PWR_CTRL,
		P9415_DCDC_EN, P9415_DCDC_EN);
	if (rc < 0)
		pr_err("set dcdc enable error, rc=%d\n", rc);

	return rc;
}

static int p9415_set_trx_enable(struct oplus_chg_ic_dev *dev, bool en)
{
	struct oplus_p9415 *chip;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_write_byte(chip, P9415_REG_TRX_CTRL,
		en ? P9415_TRX_EN : 0x00);
	if (rc < 0)
		pr_err("can't %s trx, rc=%d\n", en ? "enable" : "disable", rc);

	return rc;
}

static int p9415_get_trx_status(struct oplus_chg_ic_dev *dev, u8 *status)
{
	struct oplus_p9415 *chip;
	int rc;
	char temp;
	u8 trx_state = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_byte(chip, P9415_REG_TRX_STATUS, &temp);
	if (rc) {
		pr_err("Couldn't read trx status, rc = %d\n", rc);
		return rc;
	}

	if (temp & P9415_TRX_STATUS_READY)
		trx_state |= WLS_TRX_STATUS_READY;
	if (temp & P9415_TRX_STATUS_DIGITALPING)
		trx_state |= WLS_TRX_STATUS_DIGITALPING;
	if (temp & P9415_TRX_STATUS_ANALOGPING)
		trx_state |= WLS_TRX_STATUS_ANALOGPING;
	if (temp & P9415_TRX_STATUS_TRANSFER)
		trx_state |= WLS_TRX_STATUS_TRANSFER;
	*status = trx_state;

	return rc;
}

static int p9415_get_trx_err(struct oplus_chg_ic_dev *dev, u8 *err)
{
	struct oplus_p9415 *chip;
	int rc;
	char temp = 0;
	u8 trx_err = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_byte(chip, P9415_REG_TRX_ERR, &temp);
	if (rc) {
		pr_err("Couldn't read trx err code, rc = %d\n", rc);
		return rc;
	}

	if (temp & P9415_TRX_ERR_RXAC)
		trx_err |= WLS_TRX_ERR_RXAC;
	if (temp & P9415_TRX_ERR_OCP)
		trx_err |= WLS_TRX_ERR_OCP;
	if (temp & P9415_TRX_ERR_OVP)
		trx_err |= WLS_TRX_ERR_OVP;
	if (temp & P9415_TRX_ERR_LVP)
		trx_err |= WLS_TRX_ERR_LVP;
	if (temp & P9415_TRX_ERR_FOD)
		trx_err |= WLS_TRX_ERR_FOD;
	if (temp & P9415_TRX_ERR_OTP)
		trx_err |= WLS_TRX_ERR_OTP;
	if (temp & P9415_TRX_ERR_CEPTIMEOUT)
		trx_err |= WLS_TRX_ERR_CEPTIMEOUT;
	if (temp & P9415_TRX_ERR_RXEPT)
		trx_err |= WLS_TRX_ERR_RXEPT;
	*err = trx_err;

	return rc;
}

static void p9415_rx_track_info(struct oplus_p9415 *chip)
{
	int rc = 0;
	char temp = 0;
	int trx_err = TRACK_WLS_TRX_ERR_DEFAULT;

	rc = p9415_read_byte(chip, P9415_REG_TRX_ERR, &temp);
	if (rc) {
		pr_err("Couldn't read trx err code, rc = %d\n", rc);
		return;
	}
	if (temp & P9415_TRX_ERR_RXAC)
		trx_err = TRACK_WLS_TRX_ERR_RXAC;
	if (temp & P9415_TRX_ERR_OCP)
		trx_err = TRACK_WLS_TRX_ERR_OCP;
	if (temp & P9415_TRX_ERR_OVP)
		trx_err = TRACK_WLS_TRX_ERR_OVP;
	if (temp & P9415_TRX_ERR_LVP)
		trx_err = TRACK_WLS_TRX_ERR_LVP;
	if (temp & P9415_TRX_ERR_FOD)
		trx_err = TRACK_WLS_TRX_ERR_FOD;
	if (temp & P9415_TRX_ERR_OTP)
		trx_err = TRACK_WLS_TRX_ERR_OTP;
	if (temp & P9415_TRX_ERR_RXEPT)
		trx_err = TRACK_WLS_TRX_ERR_RXEPT;

	if (chip->debug_force_rx_err)
		trx_err = chip->debug_force_rx_err;
	if (trx_err != TRACK_WLS_TRX_ERR_DEFAULT)
		p9415_track_upload_wls_rx_err_info(chip, trx_err);

	return;
}

static void p9415_tx_track_info(struct oplus_p9415 *chip)
{
	int rc = 0;
	char temp = 0;
	int trx_err = TRACK_WLS_TRX_ERR_DEFAULT;

	rc = p9415_read_byte(chip, P9415_REG_TRX_ERR, &temp);
	if (rc) {
		pr_err("Couldn't read trx err code, rc = %d\n", rc);
		return;
	}
	if (temp & P9415_TRX_ERR_RXAC)
		trx_err = TRACK_WLS_TRX_ERR_RXAC;
	if (temp & P9415_TRX_ERR_OCP)
		trx_err = TRACK_WLS_TRX_ERR_OCP;
	if (temp & P9415_TRX_ERR_OVP)
		trx_err = TRACK_WLS_TRX_ERR_OVP;
	if (temp & P9415_TRX_ERR_LVP)
		trx_err = TRACK_WLS_TRX_ERR_LVP;
	if (temp & P9415_TRX_ERR_FOD)
		trx_err = TRACK_WLS_TRX_ERR_FOD;
	if (temp & P9415_TRX_ERR_OTP)
		trx_err = TRACK_WLS_TRX_ERR_OTP;
	if (temp & P9415_TRX_ERR_RXEPT)
		trx_err = TRACK_WLS_TRX_ERR_RXEPT;

	if (chip->debug_force_tx_err)
		trx_err = chip->debug_force_tx_err;
	if (trx_err != TRACK_WLS_TRX_ERR_DEFAULT)
		p9415_track_upload_wls_tx_err_info(chip, trx_err);

	return;
}

static int p9415_get_headroom(struct oplus_chg_ic_dev *dev, int *val)
{
	struct oplus_p9415 *chip;
	int rc;
	char temp;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_read_byte(chip, P9415_REG_HEADROOM_R, &temp);
	if (rc) {
		pr_err("Couldn't read headroom, rc = %d\n", rc);
		return rc;
	}
	*val = (int)temp;

	return rc;
}

static int p9415_set_headroom(struct oplus_chg_ic_dev *dev, int val)
{
	struct oplus_p9415 *chip;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_write_byte(chip, P9415_REG_HEADROOM_W, val);
	if (rc)
		pr_err("can't set headroom, rc=%d\n", rc);

	return rc;
}

static u8 p9415_calculate_checksum(const u8 *data, int len)
{
	u8 temp = 0;

	while(len--)
		temp ^= *data++;

	pr_debug("checksum = %d\n", temp);
	return temp;
}

static int p9415_send_match_q(struct oplus_chg_ic_dev *dev, u8 data)
{
	struct oplus_p9415 *chip;
	u8 buf[4] = { 0x38, 0x48, 0x00, data };
	u8 checksum;
	
	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	checksum = p9415_calculate_checksum(buf, 4);
	p9415_write_byte(chip, 0x0050, buf[0]);
	p9415_write_byte(chip, 0x0051, buf[1]);
	p9415_write_byte(chip, 0x0052, buf[2]);
	p9415_write_byte(chip, 0x0053, buf[3]);
	p9415_write_byte(chip, 0x0054, checksum);

	p9415_write_byte_mask(chip, 0x004E, 0x01, 0x01);

	return 0;
}

static int p9415_set_fod_parm(struct oplus_chg_ic_dev *dev, u8 data[], int len)
{
	struct oplus_p9415 *chip;
	int rc;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = p9415_write_data(chip, 0x0068, data, len);
	if (rc < 0)
		pr_err("set fod parameter error, rc=%d\n", rc);

	return rc;
}

static int p9415_send_msg(struct oplus_chg_ic_dev *dev, unsigned char msg[], int len)
{
	struct oplus_p9415 *chip;
	char write_data[2] = { 0, 0 };

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (len != 4) {
		pr_err("data length error\n");
		return -EINVAL;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (msg[0] == WLS_CMD_INDENTIFY_ADAPTER) {
		write_data[0] = 0x10;
		write_data[1] = 0x00;
		p9415_write_data(chip, 0x0038, write_data, 2);

		write_data[0] = 0x10;
		write_data[1] = 0x00;
		p9415_write_data(chip, 0x0056, write_data, 2);

		write_data[0] = 0x20;
		write_data[1] = 0x00;
		p9415_write_data(chip, 0x004E, write_data, 2);
	}

	if ((msg[0] != WLS_CMD_GET_TX_ID) && (msg[0] != WLS_CMD_GET_TX_PWR)) {
		p9415_write_byte(chip, 0x0050, 0x48);
		p9415_write_byte(chip, 0x0051, msg[0]);
		p9415_write_byte(chip, 0x0052, msg[1]);
		p9415_write_byte(chip, 0x0053, msg[2]);
		p9415_write_byte(chip, 0x0054, msg[3]);
	} else {
		p9415_write_byte(chip, 0x0050, 0x18);
		p9415_write_byte(chip, 0x0051, msg[0]);
	}

	p9415_write_byte_mask(chip, 0x004E, 0x01, 0x01);

	return 0;
}

static int p9415_register_msg_callback(struct oplus_chg_ic_dev *dev, void *dev_data,
				       void (*msg_call_back)(void *, u8 []))
{
	struct oplus_p9415 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	chip->rx_msg.dev_data = dev_data;
	chip->rx_msg.msg_call_back = msg_call_back;

	return 0;
}

static int p9415_load_bootloader(struct oplus_p9415 *chip)
{
	int rc = 0;
	// configure the system
	rc = p9415_write_byte(chip, 0x3000, 0x5a); // write key
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3000 reg error!\n");
		return rc;
	}

	rc = p9415_write_byte(chip, 0x3004, 0x00); // set HS clock
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3004 reg error!\n");
		return rc;
	}

	rc = p9415_write_byte(chip, 0x3008, 0x09); // set AHB clock
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3008 reg error!\n");
		return rc;
	}

	rc = p9415_write_byte(chip, 0x300C, 0x05); // configure 1us pulse
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x300c reg error!\n");
		return rc;
	}

	rc = p9415_write_byte(chip, 0x300D, 0x1d); // configure 500ns pulse
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x300d reg error!\n");
		return rc;
	}

	rc = p9415_write_byte(chip, 0x3040, 0x11); // Enable MTP access via I2C
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	msleep(10);

	pr_err("<IDT UPDATE>-b-2--!\n");
	rc = p9415_write_byte(chip, 0x3040, 0x10); // halt microcontroller M0
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	msleep(10);

	pr_err("<IDT UPDATE>-b-3--!\n");
	rc = p9415_write_data(
		chip, 0x0800, mtpbootloader9320,
		sizeof(mtpbootloader9320)); // load provided by IDT array
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x1c00 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>-b-4--!\n");
	rc = p9415_write_byte(chip, 0x400, 0); // initialize buffer
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x400 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>-b-5--!\n");
	rc = p9415_write_byte(chip, 0x3048, 0xD0); // map RAM address 0x1c00 to OTP 0x0000
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3048 reg error!\n");
		return rc;
	}

	pr_err("<IDT UPDATE>-b-6--!\n");
	rc = p9415_write_byte(chip, 0x3040, 0x80); // run M0

	return 0;
}

static int p9415_load_fw(struct oplus_p9415 *chip, unsigned char *fw_data, int CodeLength)
{
	unsigned char write_ack = 0;
	int rc = 0;

	rc = p9415_write_data(chip, 0x400, fw_data,
							((CodeLength + 8 + 15) / 16) * 16);
	if (rc != 0) {
		pr_err("<IDT UPDATE>ERROR: write multi byte data error!\n");
		goto LOAD_ERR;
	}
	rc = p9415_write_byte(chip, 0x400, 0x01);
	if (rc != 0) {
		pr_err("<IDT UPDATE>ERROR: on OTP buffer validation\n");
		goto LOAD_ERR;
	}

	do {
		msleep(20);
		rc = p9415_read_byte(chip, 0x401, &write_ack);
		if (rc != 0) {
			pr_err("<IDT UPDATE>ERROR: on reading OTP buffer status\n");
			goto LOAD_ERR;
		}
	} while ((write_ack & 0x01) != 0);

	// check status
	if (write_ack != 2) { // not OK
		if (write_ack == 4)
			pr_err("<IDT UPDATE>ERROR: WRITE ERR\n");
		else if (write_ack == 8)
			pr_err("<IDT UPDATE>ERROR: CHECK SUM ERR\n");
		else
			pr_err("<IDT UPDATE>ERROR: UNKNOWN ERR\n");

		rc = -1;
	}
LOAD_ERR:
	return rc;
}

static int p9415_mtp(struct oplus_p9415 *chip, unsigned char *fw_buf, int fw_size)
{
	int rc;
	int i, j;
	unsigned char *fw_data;
	unsigned char write_ack;
	unsigned short int StartAddr;
	unsigned short int CheckSum;
	unsigned short int CodeLength;
	// pure fw size not contains last 128 bytes fw version.
	int pure_fw_size = fw_size - 128;

	pr_err("<IDT UPDATE>--1--!\n");

	rc = p9415_load_bootloader(chip);
	if (rc != 0) {
		pr_err("<IDT UPDATE>Update bootloader 1 error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_I2C);
		return rc;
	}

	msleep(100);

	pr_err("<IDT UPDATE>The idt firmware size: %d!\n", fw_size);

	// program pages of 128 bytes
	// 8-bytes header, 128-bytes data, 8-bytes padding to round to 16-byte boundary
	fw_data = kzalloc(144, GFP_KERNEL);
	if (!fw_data) {
		pr_err("<IDT UPDATE>can't alloc memory!\n");
		return -ENOMEM;
	}

	//ERASE FW VERSION(the last 128 byte of the MTP)
	memset(fw_data, 0x00, 144);
	StartAddr = pure_fw_size;
	CheckSum = StartAddr;
	CodeLength = 128;
	for (j = 127; j >= 0; j--)
		CheckSum += fw_data[j + 8]; // add the non zero values.

	CheckSum += CodeLength; // finish calculation of the check sum
	memcpy(fw_data + 2, (char *)&StartAddr, 2);
	memcpy(fw_data + 4, (char *)&CodeLength, 2);
	memcpy(fw_data + 6, (char *)&CheckSum, 2);
	rc = p9415_load_fw(chip, fw_data, CodeLength);
	if (rc < 0) { // not OK
		pr_err("<IDT UPDATE>ERROR: erase fw version ERR\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}

	// upgrade fw
	memset(fw_data, 0x00, 144);
	for (i = 0; i < pure_fw_size; i += 128) {
		pr_err("<IDT UPDATE>Begin to write chunk %d!\n", i);

		StartAddr = i;
		CheckSum = StartAddr;
		CodeLength = 128;

		memcpy(fw_data + 8, fw_buf + i, 128);

		j = pure_fw_size - i;
		if (j < 128) {
			j = ((j + 15) / 16) * 16;
			CodeLength = (unsigned short int)j;
		} else {
			j = 128;
		}

		j -= 1;
		for (; j >= 0; j--)
			CheckSum += fw_data[j + 8]; // add the non zero values

		CheckSum += CodeLength; // finish calculation of the check sum

		memcpy(fw_data + 2, (char *)&StartAddr, 2);
		memcpy(fw_data + 4, (char *)&CodeLength, 2);
		memcpy(fw_data + 6, (char *)&CheckSum, 2);

		//typedef struct { // write to structure at address 0x400
		// u16 Status;
		// u16 StartAddr;
		// u16 CodeLength;
		// u16 DataChksum;
		// u8 DataBuf[128];
		//} P9220PgmStrType;
		// read status is guaranteed to be != 1 at this point

		rc = p9415_load_fw(chip, fw_data, CodeLength);
		if (rc < 0) { // not OK
			pr_err("<IDT UPDATE>ERROR: write chunk %d ERR\n", i);
			p9415_track_upload_wls_update_err_info(
				chip, TRACK_WLS_UPDATE_ERR_OTHER);
			goto MTP_ERROR;
		}
	}

	msleep(100);
	pr_info("<IDT UPDATE>disable trx boost\n");
	rc = p9415_set_trx_boost_enable(chip, false);
	if (rc < 0) {
		pr_err("disable trx power error, rc=%d\n", rc);
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}
	msleep(3000);
	pr_info("<IDT UPDATE>enable trx boost\n");
	rc = p9415_set_trx_boost_vol(chip, P9415_MTP_VOL_MV);
	if (rc < 0) {
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		pr_err("set trx power vol(=%d), rc=%d\n", P9415_MTP_VOL_MV, rc);
		goto MTP_ERROR;
	}
	rc = p9415_set_trx_boost_enable(chip, true);
	if (rc < 0) {
		pr_err("enable trx power error, rc=%d\n", rc);
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}
	msleep(500);

	// Verify
	rc = p9415_load_bootloader(chip);
	if (rc != 0) {
		pr_err("<IDT UPDATE>Update bootloader 2 error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}
	msleep(100);
	rc = p9415_write_byte(chip, 0x402, 0x00); // write start address
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x402 reg error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}

	rc = p9415_write_byte(chip, 0x403, 0x00); // write start address
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x403 reg error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}

	rc = p9415_write_byte(chip, 0x404, pure_fw_size & 0xff); // write FW length low byte
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x404 reg error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}
	rc = p9415_write_byte(chip, 0x405, (pure_fw_size >> 8) & 0xff); // write FW length high byte
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x405 reg error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}

	// write CRC from FW release package
	fw_data[0] = fw_buf[pure_fw_size + 0x08];
	fw_data[1] = fw_buf[pure_fw_size + 0x09];
	p9415_write_data(chip, 0x406, fw_data, 2);

	rc = p9415_write_byte(chip, 0x400, 0x11);
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x406 reg error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}
	do {
		msleep(20);
		rc = p9415_read_byte(chip, 0x401, &write_ack);
		if (rc != 0) {
			pr_err("<IDT UPDATE>ERROR: on reading OTP buffer status\n");
			p9415_track_upload_wls_update_err_info(
				chip, TRACK_WLS_UPDATE_ERR_OTHER);
			goto MTP_ERROR;
		}
	} while ((write_ack & 0x01) != 0);
	// check status
	if (write_ack != 2) { // not OK
		if (write_ack == 4)
			pr_err("<IDT UPDATE>ERROR: CRC WRITE ERR\n");
		else if (write_ack == 8)
			pr_err("<IDT UPDATE>ERROR: CRC CHECK SUM ERR\n");
		else
			pr_err("<IDT UPDATE>ERROR: CRC UNKNOWN ERR\n");

		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_CRC);
		goto MTP_ERROR;
	}

	memset(fw_data, 0x00, 144);
	StartAddr = pure_fw_size;
	CheckSum = StartAddr;
	CodeLength = 128;
	memcpy(fw_data + 8, fw_buf + StartAddr, 128);
	j = 127;
	for (; j >= 0; j--)
		CheckSum += fw_data[j + 8]; // add the non zero values.

	CheckSum += CodeLength; // finish calculation of the check sum
	memcpy(fw_data + 2, (char *)&StartAddr, 2);
	memcpy(fw_data + 4, (char *)&CodeLength, 2);
	memcpy(fw_data + 6, (char *)&CheckSum, 2);

	rc = p9415_load_fw(chip, fw_data, CodeLength);
	if (rc < 0) { // not OK
		pr_err("<IDT UPDATE>ERROR: erase fw version ERR\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}

	// restore system
	rc = p9415_write_byte(chip, 0x3000, 0x5a); // write key
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3000 reg error!\n");
		p9415_track_upload_wls_update_err_info(
				chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}

	rc = p9415_write_byte(chip, 0x3048, 0x00); // remove code remapping
	if (rc != 0) {
		pr_err("<IDT UPDATE>Write 0x3048 reg error!\n");
		p9415_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		goto MTP_ERROR;
	}

	pr_err("<IDT UPDATE>OTP Programming finished\n");

	kfree(fw_data);
	return 0;

MTP_ERROR:
	kfree(fw_data);
	return -EINVAL;
}

static int p9415_get_rx_version(struct oplus_chg_ic_dev *dev, u32 *version)
{
	struct oplus_p9415 *chip;

	*version = 0;
	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL\n");
		return -ENODEV;
	}

	*version = chip->fw_rx_version;

	return 0;
}

static int p9415_get_tx_version(struct oplus_chg_ic_dev *dev, u32 *version)
{
	struct oplus_p9415 *chip;

	*version = 0;
	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL\n");
		return -ENODEV;
	}

	*version = chip->fw_tx_version;

	return 0;
}

static u32 p9415_get_fw_version_by_chip(struct oplus_p9415 *chip)
{
	u32 version = 0;
	char temp[4];
	int rc = 0;

	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL\n");
		return 0;
	}

	rc = p9415_read_data(chip, 0x001C, temp, 4);
	if (rc < 0) {
		pr_err("read rx version error!\n");
		return 0;
	}
	version |= temp[3] << 24;
	version |= temp[2] << 16;
	version |= temp[1] << 8;
	version |= temp[0];

	return version;
}

static int p9415_upgrade_firmware_by_buf(struct oplus_chg_ic_dev *dev, unsigned char *fw_buf, int fw_size)
{
	struct oplus_p9415 *chip;
	int rc = 0;
	int fw_ver_start_addr;
	u32 version = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (fw_buf == NULL) {
		pr_err("fw_buf is NULL");
		return -EINVAL;
	}

	fw_ver_start_addr = fw_size - 128;
	version = 0;
	version |= fw_buf[fw_ver_start_addr + 0x07] << 24;
	version |= fw_buf[fw_ver_start_addr + 0x06] << 16;
	version |= fw_buf[fw_ver_start_addr + 0x05] << 8;
	version |= fw_buf[fw_ver_start_addr + 0x04];

	disable_irq(chip->rx_con_irq);
	disable_irq(chip->rx_event_irq);

	rc = p9415_set_trx_boost_vol(chip, P9415_MTP_VOL_MV);
	if (rc < 0) {
		pr_err("set trx power vol(=%d), rc=%d\n", P9415_MTP_VOL_MV, rc);
		return rc;
	}
	rc = p9415_set_trx_boost_enable(chip, true);
	if (rc < 0) {
		pr_err("enable trx power error, rc=%d\n", rc);
		return rc;
	}
	msleep(50);

	if (p9415_get_fw_version_by_chip(chip) == version) {
		pr_err("<IDT UPDATE>fw is the same, fw[0x%x]!\n", version);
		goto no_need_update;
	}

	rc = p9415_mtp(chip, fw_buf, fw_size);
	if (rc != 0) {
		pr_err("<IDT UPDATE>update error, rc=%d\n", rc);
		version = 0;
	} else {
		version = p9415_get_fw_version_by_chip(chip);
	}

	msleep(100);
no_need_update:
	(void)p9415_set_trx_boost_enable(chip, false);
	msleep(20);

	enable_irq(chip->rx_con_irq);
	enable_irq(chip->rx_event_irq);

	chip->fw_rx_version = version;
	chip->fw_tx_version = version;

	return rc;
}

static int p9415_upgrade_firmware_by_img(struct oplus_chg_ic_dev *dev, char *fw_img, int fw_len)
{
	int rc = 0;
	struct oplus_p9415 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	if (chip == NULL) {
		pr_err("oplus_p9415 is NULL\n");
		return -ENODEV;
	}
	if (!chip->fw_data || chip->fw_length <= 0) {
		pr_err("fw data lfailed\n");
		return -EINVAL;
	}
	rc = p9415_upgrade_firmware_by_buf(dev, chip->fw_data, chip->fw_length);

	return rc;
}

static void p9415_event_process(struct oplus_p9415 *chip)
{
	int rc = -1;
	char temp[2] = { 0, 0 };
	char val_buf[6] = { 0, 0, 0, 0, 0, 0};

	if(!p9415_rx_is_enable(chip->ic_dev)) {
		pr_info("RX is sleep, Ignore events");
		return;
	}

	rc = p9415_read_data(chip, P9415_REG_STATUS, temp, 2);
	if (rc) {
		pr_err("Couldn't read 0x%04x rc = %x\n", P9415_REG_STATUS, rc);
		temp[0] = 0;
	} else {
		pr_info("read P9415_REG_STATUS = 0x%02x 0x%02x\n", temp[0], temp[1]);
	}

	if (temp[0] & P9415_LDO_ON) {
		pr_info("LDO is on, connected.");
		complete(&chip->ldo_on);
		if (is_wls_ocm_available(chip))
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_ONLINE);
		chip->connected_ldo_on = true;
	}
	if (temp[0] & P9415_VOUT_ERR) {
		pr_err("Vout residual voltage is too high\n");
	}
	if (temp[0] & P9415_EVENT) {
		rc = p9415_read_data(chip, 0x0058, val_buf, 6);
		if (rc) {
			pr_err("Couldn't read 0x%04x, rc=%d\n", 0x0058, rc);
		} else {
			pr_err("Received TX data: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
				val_buf[0], val_buf[1], val_buf[2], val_buf[3], val_buf[4], val_buf[5]);
			if (chip->rx_msg.msg_call_back != NULL)
					chip->rx_msg.msg_call_back(chip->rx_msg.dev_data, val_buf);
			p9415_rx_track_info(chip);
		}
	}
	if (temp[0] & P9415_TRX_EVENT) {
		pr_err("trx event\n");
		p9415_tx_track_info(chip);
		if (is_wls_ocm_available(chip))
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_CHECK_TRX);
		else
			pr_err("wls ocm not found\n");
	}
	p9415_write_byte_mask(chip, 0x0036, 0xFF, 0x00);
	p9415_write_byte_mask(chip, 0x0037, 0xFF, 0x00);
	p9415_write_byte_mask(chip, 0x0056, 0x30, 0x30);
	p9415_write_byte_mask(chip, 0x004E, 0x20, 0x20);
	schedule_delayed_work(&chip->check_event_work, msecs_to_jiffies(100));
}

static int p9415_connect_check(struct oplus_chg_ic_dev *dev)
{
	struct oplus_p9415 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	schedule_delayed_work(&chip->connect_work, 0);

	return 0;
}

static int p9415_set_chip_info(struct oplus_chg_ic_dev *dev)
{

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL");
		return false;
	}
	snprintf(dev->manu_name, 15, "%s", "IDT_P9415");

	return true;
}
static struct oplus_chg_ic_rx_ops p9415_dev_ops = {
	.rx_set_enable = p9415_set_rx_enable,
	.rx_is_enable = p9415_rx_is_enable,
	.rx_is_connected = p9415_rx_is_connected,
	.rx_get_vout = p9415_get_vout,
	.rx_set_vout = p9415_set_vout,
	.rx_get_vrect = p9415_get_vrect,
	.rx_get_iout = p9415_get_iout,
	.rx_get_trx_vol = p9415_get_trx_vol,
	.rx_get_trx_curr = p9415_get_trx_curr,
	.rx_get_cep_count = p9415_get_cep_count,
	.rx_get_cep_val = p9415_get_cep_val,
	.rx_get_work_freq = p9415_get_work_freq,
	.rx_get_rx_mode = p9415_get_rx_mode,
	.rx_set_dcdc_enable = p9415_set_dcdc_enable,
	.rx_set_trx_enable = p9415_set_trx_enable,
	.rx_get_trx_status = p9415_get_trx_status,
	.rx_get_trx_err = p9415_get_trx_err,
	.rx_get_headroom = p9415_get_headroom,
	.rx_set_headroom = p9415_set_headroom,
	.rx_send_match_q = p9415_send_match_q,
	.rx_set_fod_parm = p9415_set_fod_parm,
	.rx_send_msg = p9415_send_msg,
	.rx_register_msg_callback = p9415_register_msg_callback,
	.rx_get_rx_version = p9415_get_rx_version,
	.rx_get_trx_version = p9415_get_tx_version,
	.rx_upgrade_firmware_by_buf = p9415_upgrade_firmware_by_buf,
	.rx_upgrade_firmware_by_img = p9415_upgrade_firmware_by_img,
	.rx_connect_check = p9415_connect_check,
};

static void p9415_check_event_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, check_event_work);

	bool od2_state;

	od2_state = p9415_get_rx_event_gpio_val(chip);

	pr_info("od2_state = %s", od2_state ? "true" : "false");
	if (od2_state == false){
		msleep(50);
		od2_state = p9415_get_rx_event_gpio_val(chip);
		if (od2_state == false){
			pr_err("OD2 is low, reread the event.");
			p9415_event_process(chip);
			schedule_delayed_work(&chip->check_event_work, msecs_to_jiffies(1000));
		}
	}
}

static void p9415_check_ldo_on_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, check_ldo_on_work);
	int vout = 0;
	char buf[1] = {0};
	int rc;

	pr_info("connected_ldo_on = %s", chip->connected_ldo_on ? "true" : "false");
	if ((!chip->connected_ldo_on) && p9415_rx_is_connected(chip->ic_dev)) {
		pr_err("Connect but no ldo on event irq, check again.");
		p9415_event_process(chip);
		if (!chip->connected_ldo_on) {
			rc = p9415_read_data(chip, P9415_REG_FLAG, buf, 1);
			if (rc) {
				pr_err("Couldn't read 0x%04x, rc=%d\n", P9415_REG_FLAG, rc);
			} else {
				p9415_get_vout(chip->ic_dev, &vout);
				if ((buf[0] & P9415_LDO_FLAG) && (vout > LDO_ON_MV)) {
					if (is_wls_ocm_available(chip))
						oplus_chg_anon_mod_event(chip->wls_ocm,
							OPLUS_CHG_EVENT_ONLINE);
					chip->connected_ldo_on = true;
				}
			}
		}
	}
}

static void p9415_event_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, event_work);

	p9415_event_process(chip);
}

static void p9415_connect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_p9415 *chip =
		container_of(dwork, struct oplus_p9415, connect_work);
	bool connected = false;
	bool pre_connected = false;

	pre_connected = p9415_rx_is_connected(chip->ic_dev);
retry:
	reinit_completion(&chip->ldo_on);
	(void)wait_for_completion_timeout(&chip->ldo_on, msecs_to_jiffies(50));
	connected = p9415_rx_is_connected(chip->ic_dev);
	if (connected != pre_connected) {
		pre_connected = connected;
		goto retry;
	}
	if (is_wls_ocm_available(chip)) {
		if (chip->rx_connected != connected)
			pr_err("!!!!! rx_connected[%d] -> con_gpio[%d]\n", chip->rx_connected, connected);
		if (connected && chip->rx_connected == false) {
			chip->rx_connected = true;
			chip->connected_ldo_on = false;
			cancel_delayed_work_sync(&chip->check_ldo_on_work);
			schedule_delayed_work(&chip->check_ldo_on_work, P9415_CHECK_LDO_ON_DELAY);
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_PRESENT);
		} else if (!connected) {
			chip->rx_connected = false;
			chip->adapter_type = 0;
			chip->rx_pwr_cap = 0;
			chip->connected_ldo_on = false;
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_OFFLINE);
		}
	} else {
		pr_err("wls ocm not found\n");
	}
}

static irqreturn_t p9415_event_handler(int irq, void *dev_id)
{
	struct oplus_p9415 *chip = dev_id;

	pr_err("event irq\n");
	schedule_delayed_work(&chip->event_work, 0);
	return IRQ_HANDLED;
}

static irqreturn_t p9415_connect_handler(int irq, void *dev_id)
{
	struct oplus_p9415 *chip = dev_id;

	pr_err("connect irq\n");
	schedule_delayed_work(&chip->connect_work, 0);
	return IRQ_HANDLED;
}

static int p9415_gpio_init(struct oplus_p9415 *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		pr_err("get pinctrl fail\n");
		return -ENODEV;
	}

	chip->rx_event_gpio = of_get_named_gpio(node, "oplus,rx_event-gpio", 0);
	if (!gpio_is_valid(chip->rx_event_gpio)) {
		pr_err("rx_event_gpio not specified\n");
		return -ENODEV;
	}
	rc = gpio_request(chip->rx_event_gpio, "rx_event-gpio");
	if (rc < 0) {
		pr_err("rx_event_gpio request error, rc=%d\n", rc);
		return rc;
	}
	chip->rx_event_default = pinctrl_lookup_state(chip->pinctrl, "rx_event_default");
	if (IS_ERR_OR_NULL(chip->rx_event_default)) {
		pr_err("get rx_event_default fail\n");
		goto free_event_gpio;
	}
	gpio_direction_input(chip->rx_event_gpio);
	pinctrl_select_state(chip->pinctrl, chip->rx_event_default);
	chip->rx_event_irq = gpio_to_irq(chip->rx_event_gpio);
	rc = devm_request_irq(chip->dev, chip->rx_event_irq, p9415_event_handler,
			IRQF_TRIGGER_FALLING, "rx_event_irq", chip);
	if (rc < 0) {
		pr_err("rx_event_irq request error, rc=%d\n", rc);
		goto free_event_gpio;
	}
	enable_irq_wake(chip->rx_event_irq);

	chip->rx_con_gpio = of_get_named_gpio(node, "oplus,rx_con-gpio", 0);
	if (!gpio_is_valid(chip->rx_con_gpio)) {
		pr_err("rx_con_gpio not specified\n");
		goto disable_event_irq;
	}
	rc = gpio_request(chip->rx_con_gpio, "rx_con-gpio");
	if (rc < 0) {
		pr_err("rx_con_gpio request error, rc=%d\n", rc);
		goto disable_event_irq;
	}
	chip->rx_con_default = pinctrl_lookup_state(chip->pinctrl, "rx_con_default");
	if (IS_ERR_OR_NULL(chip->rx_con_default)) {
		pr_err("get idt_con_default fail\n");
		goto free_con_gpio;
	}
	gpio_direction_input(chip->rx_con_gpio);
	pinctrl_select_state(chip->pinctrl, chip->rx_con_default);
	chip->rx_con_irq = gpio_to_irq(chip->rx_con_gpio);
	rc = devm_request_irq(chip->dev, chip->rx_con_irq, p9415_connect_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"rx_con_irq", chip);
	if (rc < 0) {
		pr_err("rx_con_irq request error, rc=%d\n", rc);
		goto free_con_gpio;
	}
	enable_irq_wake(chip->rx_con_irq);

	chip->rx_mode_gpio = of_get_named_gpio(node, "oplus,rx_mode-gpio", 0);
	if (!gpio_is_valid(chip->rx_mode_gpio)) {
		pr_err("rx_mode_gpio not specified\n");
		goto free_en_gpio;
	}
	rc = gpio_request(chip->rx_mode_gpio, "rx_mode-gpio");
	if (rc < 0) {
		pr_err("rx_mode_gpio request error, rc=%d\n", rc);
		goto free_en_gpio;
	}
	chip->rx_mode_default = pinctrl_lookup_state(chip->pinctrl, "rx_mode_default");
	if (IS_ERR_OR_NULL(chip->rx_mode_default)) {
		pr_err("get rx_mode_default fail\n");
		goto free_mode_gpio;
	}
	gpio_direction_output(chip->rx_mode_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->rx_mode_default);

	chip->rx_en_gpio = of_get_named_gpio(node, "oplus,rx_en-gpio", 0);
	if (!gpio_is_valid(chip->rx_en_gpio)) {
		pr_err("rx_en_gpio not specified\n");
		goto disable_con_irq;
	}
	rc = gpio_request(chip->rx_en_gpio, "rx_en-gpio");
	if (rc < 0) {
		pr_err("rx_en_gpio request error, rc=%d\n", rc);
		goto disable_con_irq;
	}
	chip->rx_en_active = pinctrl_lookup_state(chip->pinctrl, "rx_en_active");
	if (IS_ERR_OR_NULL(chip->rx_en_active)) {
		pr_err("get rx_en_active fail\n");
		goto free_en_gpio;
	}
	chip->rx_en_sleep = pinctrl_lookup_state(chip->pinctrl, "rx_en_sleep");
	if (IS_ERR_OR_NULL(chip->rx_en_sleep)) {
		pr_err("get rx_en_sleep fail\n");
		goto free_en_gpio;
	}

	gpio_direction_output(chip->rx_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->rx_en_active);

	return 0;

free_mode_gpio:
	if (!gpio_is_valid(chip->rx_mode_gpio))
		gpio_free(chip->rx_mode_gpio);
free_en_gpio:
	if (!gpio_is_valid(chip->rx_en_gpio))
		gpio_free(chip->rx_en_gpio);
disable_con_irq:
	disable_irq(chip->rx_con_irq);
free_con_gpio:
	if (!gpio_is_valid(chip->rx_con_gpio))
		gpio_free(chip->rx_con_gpio);
disable_event_irq:
	disable_irq(chip->rx_event_irq);
free_event_gpio:
	if (!gpio_is_valid(chip->rx_event_gpio))
		gpio_free(chip->rx_event_gpio);
	return rc;
}

static void p9415_shutdown(struct i2c_client *client)
{
	struct oplus_p9415 *chip = i2c_get_clientdata(client);
	bool is_connected = false;
	int wait_wpc_disconn_cnt = 0;

	disable_irq(chip->rx_con_irq);
	disable_irq(chip->rx_event_irq);

	is_connected = p9415_rx_is_connected(chip->ic_dev);
	if (is_connected && (p9415_get_wls_type(chip) == OPLUS_CHG_WLS_VOOC
			|| p9415_get_wls_type(chip) == OPLUS_CHG_WLS_SVOOC
			|| p9415_get_wls_type(chip) == OPLUS_CHG_WLS_PD_65W)) {
		p9415_set_rx_enable(chip->ic_dev, false);
		msleep(100);
		while (wait_wpc_disconn_cnt < 10) {
			is_connected = p9415_rx_is_connected(chip->ic_dev);
			if (!is_connected)
				break;
			msleep(150);
			wait_wpc_disconn_cnt++;
		}
	}
	return;
}

static struct regmap_config p9415_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xFFFF,
};

static int p9415_driver_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct oplus_p9415 *chip;
	struct device_node *node = client->dev.of_node;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc = 0;

	chip = devm_kzalloc(&client->dev, sizeof(struct oplus_p9415), GFP_KERNEL);
	if (!chip) {
		pr_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	chip->dev = &client->dev;
	chip->regmap = devm_regmap_init_i2c(client, &p9415_regmap_config);
	if (!chip->regmap)
		return -ENODEV;
	chip->client = client;
	i2c_set_clientdata(client, chip);

	chip->support_epp_11w = of_property_read_bool(node, "oplus,support_epp_11w");

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		pr_err("can't get ic type, rc=%d\n", rc);
		goto reg_ic_err;
	}
	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		pr_err("can't get ic index, rc=%d\n", rc);
		goto reg_ic_err;
	}
	chip->fw_data = (char *)of_get_property(node, "oplus,fw_data", &chip->fw_length);
	if (!chip->fw_data) {
		pr_err("parse fw data failed\n");
		chip->fw_data = p9415_fw_data;
		chip->fw_length = sizeof(p9415_fw_data);
	} else {
		pr_err("parse fw data length[%d]\n", chip->fw_length);
	}
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, node->name, ic_index);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		pr_err("register %s error\n", node->name);
		goto reg_ic_err;
	}
	chip->ic_dev->dev_ops = &p9415_dev_ops;
	chip->ic_dev->type = ic_type;

	rc = p9415_gpio_init(chip);
	if (rc < 0) {
		pr_err("p9415 gpio init error.");
		goto gpio_init_err;
	}

	p9415_set_chip_info(chip->ic_dev);
	p9415_track_init(chip);

	device_init_wakeup(chip->dev, true);

	INIT_DELAYED_WORK(&chip->event_work, p9415_event_work);
	INIT_DELAYED_WORK(&chip->connect_work, p9415_connect_work);
	INIT_DELAYED_WORK(&chip->check_ldo_on_work, p9415_check_ldo_on_work);
	INIT_DELAYED_WORK(&chip->check_event_work, p9415_check_event_work);
	mutex_init(&chip->i2c_lock);
	init_completion(&chip->ldo_on);
	schedule_delayed_work(&chip->connect_work, 0);
#ifdef WLS_QI_DEBUG
	rc = sysfs_create_group(&chip->dev->kobj, &p9415_attribute_group);
	if (rc < 0) {
		pr_err("sysfs_create_group error fail\n");
	}
#endif

	return 0;

gpio_init_err:
	devm_oplus_chg_ic_unregister(chip->dev, chip->ic_dev);
reg_ic_err:
	i2c_set_clientdata(client, NULL);
	return rc;
}

static int p9415_driver_remove(struct i2c_client *client)
{
	struct oplus_p9415 *chip = i2c_get_clientdata(client);

	if (!gpio_is_valid(chip->rx_en_gpio))
		gpio_free(chip->rx_en_gpio);
	disable_irq(chip->rx_con_irq);
	if (!gpio_is_valid(chip->rx_con_gpio))
		gpio_free(chip->rx_con_gpio);
	disable_irq(chip->rx_event_irq);
	if (!gpio_is_valid(chip->rx_event_gpio))
		gpio_free(chip->rx_event_gpio);
	devm_oplus_chg_ic_unregister(chip->dev, chip->ic_dev);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct of_device_id p9415_match[] = {
	{ .compatible = "oplus,p9415-rx" },
	{},
};

static const struct i2c_device_id p9415_id_table[] = {
	{ "oplus,p9415-rx", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, p9415_id_table);

static struct i2c_driver p9415_driver = {
	.driver = {
		.name = "p9415-rx",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(p9415_match),
	},
	.probe = p9415_driver_probe,
	.remove = p9415_driver_remove,
	.shutdown = p9415_shutdown,
	.id_table = p9415_id_table,
};

static __init int p9415_driver_init(void)
{
	return i2c_add_driver(&p9415_driver);
}

static __exit void p9415_driver_exit(void)
{
	i2c_del_driver(&p9415_driver);
}

oplus_chg_module_register(p9415_driver);
