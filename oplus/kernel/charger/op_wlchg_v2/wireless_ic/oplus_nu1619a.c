// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[NU1619A]: %s[%d]: " fmt, __func__, __LINE__

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
#include "oplus_chg_core.h"
#endif
#include "../../oplus_chg_module.h"
#include "../oplus_chg_wls.h"
#include "../hal/oplus_chg_ic.h"
#include "oplus_nu1619a_fw.h"
#include "oplus_nu1619a_reg.h"
#include "../oplus_chg_track.h"

#ifndef I2C_ERR_MAX
#define I2C_ERR_MAX 6
#endif

#define BOOT_AREA	0
#define RX_AREA		1
#define TX_AREA		2
#define LDO_ON_MA	100

typedef enum {
	TX_STATUS_OFF,
	TX_STATUS_ON,
	TX_STATUS_READY,
	TX_STATUS_PING_DEVICE,
	TX_STATUS_TRANSFER,
	TX_STATUS_ERR_RXAC,
	TX_STATUS_ERR_OCP,
	TX_STATUS_ERR_OVP,
	TX_STATUS_ERR_LVP,
	TX_STATUS_ERR_FOD,
	TX_STATUS_ERR_OTP,
	TX_STATUS_ERR_CEPTIMEOUT,
	TX_STATUS_ERR_RXEPT,
	TX_STATUS_UNKNOW,
} TX_STATUS;


#define NU1619_CHECK_LDO_ON_DELAY round_jiffies_relative(msecs_to_jiffies(1000))

static atomic_t i2c_err_count;

struct rx_msg_struct {
	void (*msg_call_back)(void *dev_data, u8 data[]);
	void *dev_data;
};

struct oplus_nu1619 {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;

	int rx_con_gpio;
	int rx_con_irq;
	int rx_event_gpio;
	int rx_event_irq;
	int rx_en_gpio;
	int dcdc_en_gpio;
	int mode_sw_gpio;

	bool fw_updating;
	bool check_fw_update;
	bool connected_ldo_on;
	bool boot_no_need_update;
	bool rx_no_need_update;
	bool tx_no_need_update;
	const char *fw_boot_data;
	const char *fw_rx_data;
	const char *fw_tx_data;
	int fw_boot_length;
	int fw_rx_length;
	int fw_tx_length;
	u32 fw_boot_version;
	u32 fw_rx_version;
	u32 fw_tx_version;
	bool rx_connected;
	int adapter_type;
	int rx_pwr_cap;
	int tx_status;
	bool support_epp_11w;

	struct mutex i2c_lock;

	struct pinctrl *pinctrl;
	struct pinctrl_state *rx_con_default;
	struct pinctrl_state *rx_event_default;
	struct pinctrl_state *rx_en_active;
	struct pinctrl_state *rx_en_sleep;
	struct pinctrl_state *dcdc_en_active;
	struct pinctrl_state *dcdc_en_sleep;
	struct pinctrl_state *mode_sw_active;
	struct pinctrl_state *mode_sw_sleep;

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

static int nu1619_get_running_mode(struct oplus_nu1619 *chip);
static int nu1619_get_power_cap(struct oplus_nu1619 *chip);
static int nu1619_track_upload_i2c_err_info(
	struct oplus_nu1619 *chip, int err_type, u16 reg);

static bool nu1619_rx_is_connected(struct oplus_chg_ic_dev *dev)
{
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (!gpio_is_valid(chip->rx_con_gpio)) {
		pr_err("rx_con_gpio invalid\n");
		return false;
	}

	return !!gpio_get_value(chip->rx_con_gpio);
}

static bool is_wls_ocm_available(struct oplus_nu1619 *dev)
{
	if (!dev->wls_ocm)
		dev->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!dev->wls_ocm;
}

static int nu1619_get_wls_type(struct oplus_nu1619 *chip)
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

static __inline__ void nu1619_i2c_err_inc(struct oplus_nu1619 *chip)
{
	if (nu1619_rx_is_connected(chip->ic_dev)
			&& (atomic_inc_return(&i2c_err_count) > I2C_ERR_MAX)) {
		atomic_set(&i2c_err_count, 0);
		pr_err("read i2c error\n");
		oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_RX_IIC_ERR);
		// todo, add i2c error callback
	}
}

static __inline__ void nu1619_i2c_err_clr(void)
{
	atomic_set(&i2c_err_count, 0);
}

static int nu1619_read_byte(struct oplus_nu1619 *chip, u16 addr, u8 *data)
{
	char cmd_buf[2] = {addr >> 8, addr & 0xff};
	int rc = 0;

	mutex_lock(&chip->i2c_lock);
	rc = i2c_master_send(chip->client, cmd_buf, 2);
	if (rc < 2) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		nu1619_track_upload_i2c_err_info(chip, rc, addr);
		goto error;
	}

	rc = i2c_master_recv(chip->client, data, 1);
	if (rc < 1) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	nu1619_i2c_err_clr();
	if (chip->debug_force_i2c_err)
		nu1619_track_upload_i2c_err_info(chip, rc, addr);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	nu1619_i2c_err_inc(chip);
	return rc;
}

static int nu1619_read_data(struct oplus_nu1619 *chip, u16 addr, u8 *buf, int len)
{
	char cmd_buf[2] = {addr >> 8, addr & 0xff};
	int rc = 0;

	mutex_lock(&chip->i2c_lock);
	rc = i2c_master_send(chip->client, cmd_buf, 2);
	if (rc < 2) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		nu1619_track_upload_i2c_err_info(chip, rc, addr);
		goto error;
	}

	rc = i2c_master_recv(chip->client, buf, len);
	if (rc < len) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		nu1619_track_upload_i2c_err_info(chip, rc, addr);
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	nu1619_i2c_err_clr();
	if (chip->debug_force_i2c_err)
		nu1619_track_upload_i2c_err_info(chip, rc, addr);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	nu1619_i2c_err_inc(chip);
	return rc;
}

static int nu1619_write_byte(struct oplus_nu1619 *chip, u16 addr, u8 data)
{
	u8 buf_temp[3] = {addr >> 8, addr & 0xff, data};
	int rc = 0;

	mutex_lock(&chip->i2c_lock);
	rc = i2c_master_send(chip->client, buf_temp, 3);
	if (rc < 3) {
		pr_err("write 0x%04x error, rc=%d\n", addr, rc);
		mutex_unlock(&chip->i2c_lock);
		nu1619_i2c_err_inc(chip);
		rc = rc < 0 ? rc : -EIO;
		nu1619_track_upload_i2c_err_info(chip, rc, addr);
		return rc;
	}
	mutex_unlock(&chip->i2c_lock);
	nu1619_i2c_err_clr();

	return 0;
}

/*static int nu1619_write_data(struct oplus_nu1619 *chip, u16 addr, u8 *buf, int len)
{
	u8 *buf_temp = NULL;
	int i = 0;
	int rc = 0;

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
}*/

static int nu1619_write_cmd_d(struct oplus_nu1619 *chip, u8 data)
{
	int rc = 0;
	static int cmd_d = 0;

	cmd_d &= 0x20;
	cmd_d |= data;
	cmd_d ^= 0x20;
	rc = nu1619_write_byte(chip, 0x000d, cmd_d);
	if (rc < 0) {
		pr_err("write cmd_d err, rc=%d\n", rc);
		return rc;
	}
	/*pr_err("write cmd_d = 0x%x\n", cmd_d);*/

	return rc;
}

__maybe_unused static int nu1619_read_byte_mask(struct oplus_nu1619 *chip,
				u16 addr, u8 mask, u8 *data)
{
	u8 temp = 0;
	int rc = 0;

	rc = nu1619_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;

	*data = mask & temp;

	return 0;
}

__maybe_unused static int nu1619_write_byte_mask(struct oplus_nu1619 *chip, u16 addr,
				u8 mask, u8 data)
{
	u8 temp = 0;
	int rc = 0;

	rc = nu1619_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;
	temp = (data & mask) | (temp & (~mask));
	rc = nu1619_write_byte(chip, addr, temp);
	if (rc < 0)
		return rc;

	return 0;
}

#define TRACK_LOCAL_T_NS_TO_S_THD		1000000000
#define TRACK_UPLOAD_COUNT_MAX		10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD	(24 * 3600)
static int nu1619_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int nu1619_track_upload_i2c_err_info(
	struct oplus_nu1619 *chip, int err_type, u16 reg)
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
	curr_time = nu1619_track_get_local_time_s();
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
	pre_upload_time = nu1619_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(
		&(chip->i2c_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"nu1619");
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

static void nu1619_track_i2c_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, i2c_err_load_trigger_work);

	if (!chip->i2c_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	if (chip->i2c_err_load_trigger) {
		kfree(chip->i2c_err_load_trigger);
		chip->i2c_err_load_trigger = NULL;
	}
	chip->i2c_err_uploading = false;
}

static int nu1619_track_upload_wls_rx_err_info(
	struct oplus_nu1619 *chip, int err_type)
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
	curr_time = nu1619_track_get_local_time_s();
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
	pre_upload_time = nu1619_track_get_local_time_s();
	mutex_unlock(&chip->track_rx_err_lock);

	index += snprintf(
		&(chip->rx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"nu1619");
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

static void nu1619_track_rx_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, rx_err_load_trigger_work);

	if (!chip->rx_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->rx_err_load_trigger));
	if (chip->rx_err_load_trigger) {
		kfree(chip->rx_err_load_trigger);
		chip->rx_err_load_trigger = NULL;
	}
	chip->rx_err_uploading = false;
}

static int nu1619_track_upload_wls_tx_err_info(
	struct oplus_nu1619 *chip, int err_type)
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
	curr_time = nu1619_track_get_local_time_s();
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
	pre_upload_time = nu1619_track_get_local_time_s();
	mutex_unlock(&chip->track_tx_err_lock);

	index += snprintf(
		&(chip->tx_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"nu1619");
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

static void nu1619_track_tx_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, tx_err_load_trigger_work);

	if (!chip->tx_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->tx_err_load_trigger));
	if (chip->tx_err_load_trigger) {
		kfree(chip->tx_err_load_trigger);
		chip->tx_err_load_trigger = NULL;
	}
	chip->tx_err_uploading = false;
}

static int nu1619_track_upload_wls_update_err_info(
	struct oplus_nu1619 *chip, int err_type)
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
	curr_time = nu1619_track_get_local_time_s();
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
	pre_upload_time = nu1619_track_get_local_time_s();
	mutex_unlock(&chip->track_update_err_lock);

	index += snprintf(
		&(chip->update_err_load_trigger->crux_info[index]),
		OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
		"nu1619");
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

static void nu1619_track_update_err_load_trigger_work(
	struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, update_err_load_trigger_work);

	if (!chip->update_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->update_err_load_trigger));
	if (chip->update_err_load_trigger) {
		kfree(chip->update_err_load_trigger);
		chip->update_err_load_trigger = NULL;
	}
	chip->update_err_uploading = false;
}

static int nu1619_track_debugfs_init(struct oplus_nu1619 *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_nu1619;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_nu1619 = debugfs_create_dir("nu1619", debugfs_root);
	if (!debugfs_nu1619) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_rx_err = TRACK_WLS_TRX_ERR_DEFAULT;
	chip->debug_force_tx_err = TRACK_WLS_TRX_ERR_DEFAULT;
	chip->debug_force_update_err = TRACK_WLS_TRX_ERR_DEFAULT;
	chip->debug_force_upload_period = 0;
	debugfs_create_u32("debug_force_i2c_err", 0644,
	    debugfs_nu1619, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_rx_err", 0644,
	    debugfs_nu1619, &(chip->debug_force_rx_err));
	debugfs_create_u32("debug_force_tx_err", 0644,
	    debugfs_nu1619, &(chip->debug_force_tx_err));
	debugfs_create_u32("debug_force_update_err", 0644,
	    debugfs_nu1619, &(chip->debug_force_update_err));
	debugfs_create_u32("debug_force_upload_period", 0644,
	    debugfs_nu1619, &(chip->debug_force_upload_period));

	return ret;
}

static int nu1619_track_init(struct oplus_nu1619 *chip)
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
				nu1619_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->rx_err_load_trigger_work,
				nu1619_track_rx_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->tx_err_load_trigger_work,
				nu1619_track_tx_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->update_err_load_trigger_work,
				nu1619_track_update_err_load_trigger_work);
	rc = nu1619_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("nu1619 debugfs init error, rc=%d\n", rc);
	}

	return rc;
}

static int nu1619_set_trx_boost_enable(struct oplus_nu1619 *chip, bool en)
{
	union oplus_chg_mod_propval pval;

	if (!is_wls_ocm_available(chip)) {
		pr_err("wls ocm not found\n");
		return -ENODEV;
	}

	pval.intval = en;
	return oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_POWER_EN, &pval);
}

static int nu1619_set_trx_boost_vol(struct oplus_nu1619 *chip, int vol_mv)
{
	union oplus_chg_mod_propval pval;

	if (!is_wls_ocm_available(chip)) {
		pr_err("wls ocm not found\n");
		return -ENODEV;
	}

	pval.intval = vol_mv;
	return oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_POWER_VOL, &pval);
}

__maybe_unused static int nu1619_set_trx_boost_curr_limit(struct oplus_nu1619 *chip, int curr_ma)
{
	union oplus_chg_mod_propval pval;

	if (!is_wls_ocm_available(chip)) {
		pr_err("wls ocm not found\n");
		return -ENODEV;
	}

	pval.intval = curr_ma;
	return oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT, &pval);
}

__maybe_unused static int nu1619_get_rx_event_gpio_val(struct oplus_nu1619 *chip)
{
	if (!gpio_is_valid(chip->rx_event_gpio)) {
		pr_err("rx_event_gpio invalid\n");
		return -ENODEV;
	}

	return gpio_get_value(chip->rx_event_gpio);
}

static bool nu1619_is_in_tx_mode(struct oplus_nu1619 *chip)
{
	union oplus_chg_mod_propval pval = {0, };

	if (!is_wls_ocm_available(chip)) {
		pr_err("wls ocm not found\n");
		return false;
	}
	oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_WLS_TYPE, &pval);
	if (pval.intval == OPLUS_CHG_WLS_TRX)
		return true;
	return false;
}

static int nu1619_set_rx_enable(struct oplus_chg_ic_dev *dev, bool en)
{
	struct oplus_nu1619 *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (IS_ERR_OR_NULL(chip->pinctrl) || IS_ERR_OR_NULL(chip->rx_en_active)
			|| IS_ERR_OR_NULL(chip->rx_en_sleep)) {
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

static bool nu1619_rx_is_enable(struct oplus_chg_ic_dev *dev)
{
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
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

static int nu1619_get_vout(struct oplus_chg_ic_dev *dev, int *vout)
{
	struct oplus_nu1619 *chip;
	char val_buf[5] = {0, 0, 0, 0, 0};
	int vout_value = 0;
	int retry_count = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

READ_VOUT_RETRY:
	nu1619_write_byte(chip, 0x00c, NU1619_REG_VOUT);
	nu1619_read_data(chip, 0x008, val_buf, 3);
	if (val_buf[0] == (NU1619_REG_VOUT ^ 0x80)) {
		vout_value = (val_buf[2] << 8) + val_buf[1];
		retry_count = 0;
	} else {
		if (retry_count < 3) {
			msleep(1);
			retry_count++;
			goto READ_VOUT_RETRY;
		}
		pr_err("<~WPC~> read vout err\n");
		return -1;
	}
	*vout = vout_value;

	if (printk_ratelimit())
		pr_err("<~WPC~> vout:%d\n", vout_value);

	return 0;
}

static int nu1619_set_rx_charge_voltage(struct oplus_chg_ic_dev *dev, int vol)
{
	struct oplus_nu1619 *chip;
	char write_data_vout[2] = {0, 0};
	char write_data_vrect[2] = {0, 0};

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	write_data_vout[0] = vol & 0x00FF;
	write_data_vout[1] = (vol & 0xFF00) >> 8;
	nu1619_write_byte(chip, 0x0000, write_data_vout[1]);
	nu1619_write_byte(chip, 0x0001, write_data_vout[0]);
	write_data_vrect[0] = (vol + 100) & 0x00FF;
	write_data_vrect[1] = ((vol + 100) & 0xFF00) >> 8;
	nu1619_write_byte(chip, 0x0002, write_data_vrect[1]);
	nu1619_write_byte(chip, 0x0003, write_data_vrect[0]);
	nu1619_write_cmd_d(chip, 0x02);

	pr_err("<~WPC~> nu1619_set_rx_charge_voltage:%d\n", vol);

	return 0;
}

static int nu1619_get_vrect(struct oplus_chg_ic_dev *dev, int *vrect)
{
	struct oplus_nu1619 *chip;
	char val_buf[5] = {0, 0, 0, 0, 0};
	int vrect_value = 0;
	int retry_count = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

READ_VRECT_RETRY:
	nu1619_write_byte(chip, 0x000c, NU1619_REG_VRECT);
	nu1619_read_data(chip, 0x0008, val_buf, 3);
	if (val_buf[0] == (NU1619_REG_VRECT ^ 0x80)) {
		vrect_value = (val_buf[2] << 8) + val_buf[1];
		retry_count = 0;
	} else {
		if (retry_count < 3) {
			msleep(1);
			retry_count++;
			goto READ_VRECT_RETRY;
		}
		pr_err("<~WPC~> read vrect err\n");
		return -1;
	}
	*vrect = vrect_value;

	if (printk_ratelimit())
		pr_err("<~WPC~> vrect:%d\n", vrect_value);

	return 0;
}

static int nu1619_get_iout(struct oplus_chg_ic_dev *dev, int *iout)
{
	struct oplus_nu1619 *chip;
	char val_buf[5] = {0, 0, 0, 0, 0};
	int iout_value = 0;
	int retry_count = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

READ_IOUT_RETRY:
	nu1619_write_byte(chip, 0x000c, NU1619_REG_IOUT);
	nu1619_read_data(chip, 0x0008, val_buf, 3);
	if (val_buf[0] == (NU1619_REG_IOUT ^ 0x80)) {
		iout_value = (val_buf[2] << 8) + val_buf[1];
		retry_count = 0;
	} else {
		if (retry_count < 3) {
			msleep(1);
			retry_count++;
			goto READ_IOUT_RETRY;
		}
		pr_err("<~WPC~> read iout err\n");
		return -1;
	}
	*iout = iout_value;

	if (printk_ratelimit())
		pr_err("<~WPC~> iout:%d\n", iout_value);

	return 0;
}


static int nu1619_get_tx_vout(struct oplus_chg_ic_dev *dev, int *vout)
{
	struct oplus_nu1619 *chip;
	char val_buf[5] = {0, 0, 0, 0, 0};
	int vout_value = 0;
	int retry_count = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

READ_TX_VOUT_RETRY:
	nu1619_write_byte(chip, 0x000c, NU1619_REG_TX_VOUT);
	nu1619_read_data(chip, 0x0008, val_buf, 3);
	if (val_buf[0] == (NU1619_REG_TX_VOUT ^ 0x80)) {
		vout_value = (val_buf[2] << 8) + val_buf[1];
		retry_count = 0;
	} else {
		if (retry_count < 3) {
			msleep(1);
			retry_count++;
			goto READ_TX_VOUT_RETRY;
		}
		pr_err("<~WPC~> read tx vout err\n");
		return -1;
	}
	*vout = vout_value;

	pr_err("<~WPC~> tx vout:%d\n", vout_value);

	return 0;
}

static int nu1619_get_tx_iout(struct oplus_chg_ic_dev *dev, int *iout)
{
	struct oplus_nu1619 *chip;
	char val_buf[5] = {0, 0, 0, 0, 0};
	int iout_value = 0;
	int retry_count = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

READ_TX_IOUT_RETRY:
	nu1619_write_byte(chip, 0x000c, NU1619_REG_TX_IOUT);
	nu1619_read_data(chip, 0x0008, val_buf, 3);
	if (val_buf[0] == (NU1619_REG_TX_IOUT ^ 0x80)) {
		iout_value = (val_buf[2] << 8) + val_buf[1];
		retry_count = 0;
	} else {
		if (retry_count < 3) {
			msleep(1);
			retry_count++;
			goto READ_TX_IOUT_RETRY;
		}
		pr_err("<~WPC~> read tx vout err\n");
		return -1;
	}
	*iout = iout_value;

	pr_err("<~WPC~> tx iout:%d\n", iout_value);

	return 0;
}


static int nu1619_get_cep_count(struct oplus_chg_ic_dev *dev, int *count)
{
#if 1
	static int pre_count = 0;

	pre_count++;
	*count = pre_count;
	return 0;
#else
	struct oplus_nu1619 *chip;
	char val_buf[2] = {0, 0};
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = nu1619_read_data(chip, NU1619_REG_CEP_COUNT, val_buf, 2);
	if (rc < 0) {
		pr_err("Couldn't read cep change status, rc=%d\n", rc);
		return rc;
	}
	*count = val_buf[0] | val_buf[1] << 8;

	return 0;
#endif
}

static int nu1619_get_cep_val(struct oplus_chg_ic_dev *dev, int *val)
{
	struct oplus_nu1619 *chip;
	char val_buf[5] = {0, 0, 0, 0, 0};
	int rc = 0;
	char temp = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = nu1619_write_byte(chip, 0x000c, NU1619_REG_CEP);
	rc = nu1619_read_data(chip, 0x0008, val_buf, 3);
	if (val_buf[0] == (NU1619_REG_CEP ^ 0x80)) {
		temp = val_buf[1];
	}
	*val = (signed char)temp;

	if (printk_ratelimit())
		pr_err("<~WPC~> cep value:%d\n", *val);

	return rc;
}


static int nu1619_get_work_freq(struct oplus_chg_ic_dev *dev, int *val)
{
	return 0;
}

static int nu1619_get_power_cap(struct oplus_nu1619 *chip)
{
	char val_buf[5] = {0, 0, 0, 0, 0};
	char temp[3] = {0, 0, 0};

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return 0;
	}

	if (chip->rx_pwr_cap != 0)
		return chip->rx_pwr_cap;

	nu1619_write_byte(chip, 0x000c, NU1619_REG_TX_PWR_CAP);
	nu1619_read_data(chip, 0x0008, temp, 2);
	if (temp[0] == (NU1619_REG_TX_PWR_CAP ^ 0x80)) {
		val_buf[0] = temp[1];
	}
	if (!chip->support_epp_11w && val_buf[0] >= NU1619_RX_PWR_15W) {
		chip->rx_pwr_cap = NU1619_RX_PWR_15W;
	} else if (chip->support_epp_11w && val_buf[0] >= NU1619_RX_PWR_11W) {
		chip->rx_pwr_cap = NU1619_RX_PWR_11W;
	} else if (val_buf[0] < NU1619_RX_PWR_10W && val_buf[0] != 0) {
		/*treat <10W as 5W*/
		chip->rx_pwr_cap = NU1619_RX_PWR_5W;
	} else {
		/*default running mode epp 10w*/
		chip->rx_pwr_cap = NU1619_RX_PWR_10W;
	}
	if (chip->adapter_type == 0) {
		chip->adapter_type = nu1619_get_running_mode(chip);
	}
	if (chip->adapter_type == NU1619_RX_MODE_EPP) {
		pr_err("running mode epp-%d/2w\n", val_buf[0]);
	}
	return chip->rx_pwr_cap;
}

static int nu1619_get_running_mode(struct oplus_nu1619 *chip)
{
	char val_buf[5] = {0, 0, 0, 0, 0};
	int rc = 0;
	char temp = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return 0;
	}

	if (chip->adapter_type != 0)
		return chip->adapter_type;

	rc = nu1619_write_byte(chip, 0x000c, NU1619_REG_RX_MODE);
	rc = nu1619_read_data(chip, 0x0008, val_buf, 3);
	if (val_buf[0] == (NU1619_REG_RX_MODE ^ 0x80)) {
		temp = val_buf[1];
	}
	if (rc < 0) {
		pr_err("Couldn't read rx mode, rc=%d\n", rc);
		return 0;
	}
	if (temp == NU1619_RX_MODE_EPP) {
		pr_err("rx running in EPP!\n");
		chip->adapter_type = NU1619_RX_MODE_EPP;
	} else if (temp == NU1619_RX_MODE_BPP) {
		pr_err("rx running in BPP!\n");
		chip->adapter_type = NU1619_RX_MODE_BPP;
	} else {
		pr_err("rx running in Others!\n");
		chip->adapter_type = 0;
	}
	if (chip->rx_pwr_cap == 0 && chip->adapter_type == NU1619_RX_MODE_EPP)
		chip->rx_pwr_cap = nu1619_get_power_cap(chip);
	return chip->adapter_type;
}

static int nu1619_get_rx_mode(struct oplus_chg_ic_dev *dev, enum oplus_chg_wls_rx_mode *rx_mode)
{
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	chip->adapter_type = nu1619_get_running_mode(chip);
	chip->rx_pwr_cap = nu1619_get_power_cap(chip);
	if (chip->adapter_type == NU1619_RX_MODE_EPP) {
		if (chip->rx_pwr_cap == NU1619_RX_PWR_15W ||
		    chip->rx_pwr_cap == NU1619_RX_PWR_11W)
			*rx_mode = OPLUS_CHG_WLS_RX_MODE_EPP_PLUS;
		else if (chip->rx_pwr_cap == NU1619_RX_PWR_5W)
			*rx_mode = OPLUS_CHG_WLS_RX_MODE_EPP_5W;
		else
			*rx_mode = OPLUS_CHG_WLS_RX_MODE_EPP;
	} else if (chip->adapter_type == NU1619_RX_MODE_BPP) {
		*rx_mode = OPLUS_CHG_WLS_RX_MODE_BPP;
	} else{
		chip->adapter_type = 0;
		*rx_mode = OPLUS_CHG_WLS_RX_MODE_UNKNOWN;
	}
	pr_debug("!!! rx_mode=%d\n", *rx_mode);

	return 0;
}

static int nu1619_set_rx_mode(struct oplus_chg_ic_dev *dev, enum oplus_chg_wls_rx_mode rx_mode)
{
	struct oplus_nu1619 *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->mode_sw_active) ||
	    IS_ERR_OR_NULL(chip->mode_sw_sleep)) {
		pr_err("mode_sw pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(chip->pinctrl, chip->mode_sw_active);
	if (rc < 0)
		pr_err("can't set mode_sw active, rc=%d\n", rc);
	else
		pr_err("set mode_sw active\n");

        pr_err("mode_sw: gpio_val:%d\n", gpio_get_value(chip->mode_sw_gpio));

	rc = nu1619_set_rx_enable(dev, false);
	if (rc < 0)
		pr_err("set rx disable failed, rc=%d\n", rc);

	msleep(100);

	rc = nu1619_set_rx_enable(dev, true);
	if (rc < 0)
		pr_err("set rx enable failed, rc=%d\n", rc);

        return rc;
}

static bool nu1619_get_mode_sw_active(struct oplus_nu1619 *chip)
{
	if (!chip) {
		pr_err("oplus_nu1619 chip is null!\n");
		return false;
	}

	if (!gpio_is_valid(chip->mode_sw_gpio)) {
		pr_err("mode_sw_gpio not specified\n");
		return false;
	}

	return gpio_get_value(chip->mode_sw_gpio);
}

static int nu1619_set_mode_sw_default(struct oplus_nu1619 *chip)
{
	int rc = 0;

	if (!chip) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->mode_sw_active) ||
	    IS_ERR_OR_NULL(chip->mode_sw_sleep)) {
		pr_err("mode_sw pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(chip->pinctrl, chip->mode_sw_sleep);
	if (rc < 0)
		pr_err("can't set mode_sw active, rc=%d\n", rc);
	else
		pr_err("set mode_sw default\n");

        pr_err("mode_sw: gpio_val:%d\n", gpio_get_value(chip->mode_sw_gpio));

	return rc;
}

static int nu1619_set_dcdc_enable(struct oplus_chg_ic_dev *dev, bool en)
{
	struct oplus_nu1619 *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
		IS_ERR_OR_NULL(chip->dcdc_en_active) ||
		IS_ERR_OR_NULL(chip->dcdc_en_sleep)) {
		pr_err("dcdc_en pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(chip->pinctrl,
		en ? chip->dcdc_en_active : chip->dcdc_en_sleep);
	if (rc < 0)
		pr_err("can't %s dcdc_en, rc=%d\n", en ? "enable" : "disable", rc);
	else
		pr_err("set dcdc_en %s\n", en ? "enable" : "disable");

	pr_err("dcdc_en: set value:%d, gpio_val:%d\n", en, gpio_get_value(chip->dcdc_en_gpio));

	return rc;
}

static int nu1619_set_tx_enable(struct oplus_chg_ic_dev *dev, bool en)
{
	struct oplus_nu1619 *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	rc = nu1619_write_cmd_d(chip, 0x07);
	if (rc < 0)
		pr_err("can't %s tx, rc=%d\n", en ? "enable" : "disable", rc);

	return rc;
}

static int nu1619_set_tx_start(struct oplus_chg_ic_dev *dev)
{
	int ret = -1;
	static int cnt = 0;
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	chip->tx_status = TX_STATUS_ON;

	while (ret != 0 && ++cnt < 10) {
		ret = nu1619_write_byte(chip, 0x000d, 0x20);
		if (ret != 0)
			msleep(10);
	}
	pr_err("ret=%d, cnt=%d\n", ret, cnt);
	cnt = 0;

	return ret;
}

static int nu1619_get_tx_status(struct oplus_chg_ic_dev *dev, u8 *status)
{
	struct oplus_nu1619 *chip;
	u8 trx_state = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (chip->tx_status == TX_STATUS_READY)
		trx_state |= WLS_TRX_STATUS_READY;
	if (chip->tx_status == TX_STATUS_PING_DEVICE)
		trx_state |= WLS_TRX_STATUS_DIGITALPING;
	if (chip->tx_status == TX_STATUS_TRANSFER)
		trx_state |= WLS_TRX_STATUS_TRANSFER;
	*status = trx_state;

	return 0;
}

static int nu1619_get_tx_err(struct oplus_chg_ic_dev *dev, u8 *err)
{
	struct oplus_nu1619 *chip;
	u8 trx_err = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	switch (chip->tx_status) {
	case TX_STATUS_ERR_RXAC:
		trx_err |= WLS_TRX_ERR_RXAC;
		break;
	case TX_STATUS_ERR_OCP:
		trx_err |= WLS_TRX_ERR_OCP;
		break;
	case TX_STATUS_ERR_OVP:
		trx_err |= WLS_TRX_ERR_OVP;
		break;
	case TX_STATUS_ERR_LVP:
		trx_err |= WLS_TRX_ERR_LVP;
		break;
	case TX_STATUS_ERR_FOD:
		trx_err |= WLS_TRX_ERR_FOD;
		break;
	case TX_STATUS_ERR_OTP:
		trx_err |= WLS_TRX_ERR_OTP;
		break;
	case TX_STATUS_ERR_CEPTIMEOUT:
		trx_err |= WLS_TRX_ERR_CEPTIMEOUT;
		break;
	case TX_STATUS_ERR_RXEPT:
		trx_err |= WLS_TRX_ERR_RXEPT;
		break;
	default:
		break;
	}

	*err = trx_err;

	return 0;
}

static int nu1619_get_headroom(struct oplus_chg_ic_dev *dev, int *val)
{
	return 0;
}

static int nu1619_set_headroom(struct oplus_chg_ic_dev *dev, int val)
{
	return 0;
}

static int nu1619_send_match_q(struct oplus_chg_ic_dev *dev, u8 data)
{
	struct oplus_nu1619 *chip;
	u8 buf[4] = {0x38, 0x48, 0x00, data};
	//u8 checksum = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	//checksum = nu1619_calculate_checksum(buf, 4);
	nu1619_write_byte(chip, 0x0000, buf[0]);
	nu1619_write_byte(chip, 0x0001, buf[1]);
	nu1619_write_byte(chip, 0x0002, buf[2]);
	nu1619_write_byte(chip, 0x0003, buf[3]);
	//nu1619_write_byte(chip, 0x0054, checksum);

	nu1619_write_cmd_d(chip, 0x01);

	return 0;
}

static int nu1619_set_fod_parm(struct oplus_chg_ic_dev *dev, u8 data[], int len)
{
	return 0;
}

static int nu1619_send_msg(struct oplus_chg_ic_dev *dev, unsigned char msg[], int len)
{
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	if (len != 4) {
		pr_err("data length error\n");
		return -EINVAL;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (msg[0] == WLS_CMD_GET_TX_PWR) {
		nu1619_write_byte(chip, 0x0000, 0x18);
		nu1619_write_byte(chip, 0x0001, msg[0]);
		nu1619_write_byte(chip, 0x0002, ~msg[0]);
		nu1619_write_byte(chip, 0x0003, 0xff);
		nu1619_write_byte(chip, 0x0004, 0x00);
	} else if (msg[0] == WLS_CMD_GET_TX_ID) {
		nu1619_write_byte(chip, 0x0000, 0x18);
		nu1619_write_byte(chip, 0x0001, msg[0]);
	} else {
		nu1619_write_byte(chip, 0x0000, 0x48);
		nu1619_write_byte(chip, 0x0001, msg[0]);
		nu1619_write_byte(chip, 0x0002, msg[1]);
		nu1619_write_byte(chip, 0x0003, msg[2]);
		nu1619_write_byte(chip, 0x0004, msg[3]);
	}

	nu1619_write_cmd_d(chip, 0x01);

	return 0;
}

static int nu1619_register_msg_callback(struct oplus_chg_ic_dev *dev, void *dev_data,
				void (*msg_call_back)(void *, u8 []))
{
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	chip->rx_msg.dev_data = dev_data;
	chip->rx_msg.msg_call_back = msg_call_back;

	return 0;
}

static int nu1619_get_idt_con_val(struct oplus_nu1619 *chip)
{
	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return 0;
	}

	if (!gpio_is_valid(chip->rx_con_gpio)) {
		pr_err("rx_con_gpio invalid\n");
		return 0;
	}

	return gpio_get_value(chip->rx_con_gpio);
}

static bool nu1619_check_i2c_is_ok(struct oplus_nu1619 *chip)
{
	char read_data = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	nu1619_write_byte(chip, 0x0000, 0x88);
	msleep(10);
	nu1619_read_data(chip, 0x0000, &read_data, 1);

	if (read_data == 0x88) {
		return true;
	} else {
		return false;
	}
}

static int nu1619_config_gpio_1v8(struct oplus_nu1619 *chip)
{
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	pr_err("<FW UPDATE> nu1619_config_gpio_1v8\n");

	ret = nu1619_write_byte(chip, 0x1000, 0x10);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x1130, 0x3e);
	if (ret < 0)
		return ret;

	return 1;
}
static int nu1619_enter_write_mode(struct oplus_nu1619 *chip)
{
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	pr_err("<FW UPDATE> nu1619_enter_write_mode\n");

	ret = nu1619_write_byte(chip, 0x0017, 0x01);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x1000, 0x30);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x001a, 0x5a);
	if (ret < 0)
		return ret;

	return 1;
}

static int nu1619_exit_write_mode(struct oplus_nu1619 *chip)
{
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	pr_err("<FW UPDATE> nu1619_exit_write_mode\n");

	ret = nu1619_write_byte(chip, 0x001a, 0x00);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x0017, 0x00);
	if (ret < 0)
		return ret;

	return 1;
}

static int nu1619_enter_dtm_mode(struct oplus_nu1619 *chip)
{
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	pr_err("<FW UPDATE> nu1619_enter_dtm_mode\n");

	ret = nu1619_write_byte(chip, 0x2017, 0x69);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x2017, 0x96);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x2017, 0x66);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x2017, 0x99);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x2018, 0xff);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x2019, 0xff);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x0001, 0x5a);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x0003, 0xa5);
	if (ret < 0)
		return ret;

	return 1;
}

static int nu1619_exit_dtm_mode(struct oplus_nu1619 *chip)
{
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	pr_err("<FW UPDATE> nu1619_exit_dtm_mode\n");

	ret = nu1619_write_byte(chip, 0x2018, 0x00);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x2019, 0x00);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x0001, 0x00);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x0003, 0x00);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x2017, 0x55);
	if (ret < 0)
		return ret;

	return 1;
}

static int nu1619_wirte_firmware_data(struct oplus_nu1619 *chip, unsigned short addr, unsigned short length)
{
	const char *fw_data = NULL;
	char addr_h = 0;
	char addr_l = 0;
	int i = 0;
	char j = 0;
	unsigned count1 = 0;
	unsigned count2 = 0;
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	pr_err("<FW UPDATE> nu1619_wirte_firmware_data: addr=%d length=%d\n", addr, length);

	/************write_mtp_addr begin************/
	if (addr == 0) {
		fw_data = chip->fw_boot_data;
	}
	if (addr == 256) {
		fw_data = chip->fw_rx_data;
	}
	if (addr == 4864) {
		fw_data = chip->fw_tx_data;
	}

	if (fw_data == NULL) {
		pr_err("<FW UPDATE> fw_data NULL\n");
		return -ENODEV;
	}

	addr_h = (char)(addr >> 8);
	addr_l = (char)(addr & 0xff);
	ret = nu1619_write_byte(chip, 0x0010, addr_h);
	if (ret < 0) {
		return ret;
	}
	ret = nu1619_write_byte(chip, 0x0011, addr_l);
	if (ret < 0) {
		return ret;
	}
	/************write_mtp_addr end************/

	ret = nu1619_enter_write_mode(chip);
	if (ret < 0) {
		return ret;
	}

	/************write data begin************/
	for (i = 0; i < length; i += 4) {
		ret = nu1619_write_byte(chip, 0x0012, fw_data[i + 3]);
		if (ret < 0) {
			return ret;
		}
		for (j = 0; j < 60; j++) {
			if (nu1619_get_idt_con_val(chip) == 1) {
				usleep_range(50, 50);
				if (nu1619_get_idt_con_val(chip) == 0) {
					count1++;
					break;
				}
			} else if (nu1619_get_idt_con_val(chip) == 0) {
				usleep_range(100, 110);
				count2++;
				break;
			}
		}
		ret = nu1619_write_byte(chip, 0x0012, fw_data[i + 2]);
		if (ret < 0) {
			return ret;
		}
		for (j = 0; j < 60; j++) {
			if (nu1619_get_idt_con_val(chip) == 1) {
				usleep_range(50, 50);
				if (nu1619_get_idt_con_val(chip) == 0) {
					count1++;
					break;
				}
			} else if (nu1619_get_idt_con_val(chip) == 0) {
				usleep_range(100, 110);
				count2++;
				break;
			}
		}
		ret = nu1619_write_byte(chip, 0x0012, fw_data[i + 1]);
		if (ret < 0) {
			return ret;
		}
		for (j = 0; j < 60; j++) {
			if (nu1619_get_idt_con_val(chip) == 1) {
				usleep_range(50, 50);
				if (nu1619_get_idt_con_val(chip) == 0) {
					count1++;
					break;
				}
			} else if (nu1619_get_idt_con_val(chip) == 0) {
				usleep_range(100, 110);
				count2++;
				break;
			}
		}
		ret = nu1619_write_byte(chip, 0x0012, fw_data[i + 0]);
		if (ret < 0) {
			return ret;
		}
		for (j = 0; j < 60; j++) {
			if (nu1619_get_idt_con_val(chip) == 1) {
				usleep_range(50, 50);
				if (nu1619_get_idt_con_val(chip) == 0) {
					count1++;
					break;
				}
			} else if (nu1619_get_idt_con_val(chip) == 0) {
				usleep_range(100, 110);
				count2++;
				break;
			}
		}
	}
	/************write data end************/

	ret = nu1619_exit_write_mode(chip);
	if (ret < 0) {
		return ret;
	}

	pr_err("<FW UPDATE> nu1619_wirte_firmware_data: count1=%d count2=%d count=%d\n",
		count1, count2, count1 + count2);

	return 1;
}

static bool nu1619_download_firmware_data(struct oplus_nu1619 *chip, char area)
{
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	pr_err("<FW UPDATE> nu1619_download_firmware_data enter\n");

	ret = nu1619_enter_dtm_mode(chip);
	if (ret < 0) {
		return false;
	}
	ret = nu1619_config_gpio_1v8(chip);
	if (ret < 0) {
		return false;
	}
	msleep(10);
	if (area == BOOT_AREA) {
		ret = nu1619_wirte_firmware_data(chip, 0, chip->fw_boot_length);
		if (ret < 0) {
			return false;
		}
	}
	if (area == RX_AREA) {
		ret = nu1619_wirte_firmware_data(chip, 256, chip->fw_rx_length);
		if (ret < 0) {
			return false;
		}
	}
	if (area == TX_AREA) {
		ret = nu1619_wirte_firmware_data(chip, 4864, chip->fw_tx_length);
		if (ret < 0) {
			return false;
		}
	}
	ret = nu1619_exit_dtm_mode(chip);
	if (ret < 0) {
		return false;
	}

	pr_err("<FW UPDATE> nu1619_download_firmware_data exit\n");

	return true;
}

static bool nu1619_download_firmware_area(struct oplus_nu1619 *chip, char area)
{
	bool status = false;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	/*if (area == BOOT_AREA) {
		chip->fw_data_length = chip->fw_boot_length;
	} else if (area == RX_AREA) {
		chip->fw_data_length = chip->fw_rx_length;
	} else if (area == TX_AREA) {
		chip->fw_data_length = chip->fw_tx_length;
	}*/
	status = nu1619_download_firmware_data(chip, area);
	if (!status) {
		return false;
	}
	return true;
}

static bool nu1619_download_firmware(struct oplus_nu1619 *chip)
{
	bool status = false;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	if (chip->boot_no_need_update == false) {
		status = nu1619_download_firmware_area(chip, BOOT_AREA);
		if (!status) {
			pr_err("<FW UPDATE> BOOT_AREA download fail!!!\n");
			return false;
		}
		msleep(20);
	}

	if (chip->rx_no_need_update == false) {
		status = nu1619_download_firmware_area(chip, RX_AREA);
		if (!status) {
			pr_err("<FW UPDATE> RX_AREA download fail!!!\n");
			return false;
		}
		msleep(20);
	}

	if (chip->tx_no_need_update == false) {
		status = nu1619_download_firmware_area(chip, TX_AREA);
		if (!status) {
			pr_err("<FW UPDATE> TX_AREA download fail!!!\n");
			return false;
		}
	}
	return true;
}

static bool nu1619_req_checksum_and_fw_version(struct oplus_nu1619 *chip,
		char *boot_check_sum, char *rx_check_sum, char *tx_check_sum,
		char *boot_ver, char *rx_ver, char *tx_ver)
{
	char read_buffer1[4] = {0};
	char read_buffer2[5] = {0};

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	nu1619_write_byte(chip, 0x000d, 0x21);
	msleep(20);
	nu1619_read_data(chip, 0x0008, read_buffer1, 3);
	pr_err("<FW UPDATE> check sum value: 0x0008,9,a = 0x%x,0x%x,0x%x\n",
		read_buffer1[0], read_buffer1[1], read_buffer1[2]);
	nu1619_read_data(chip, 0x0020, read_buffer2, 5);
	pr_err("<FW UPDATE> 0x0020,21,22,23,24 = 0x%x,0x%x,0x%x,0x%x,0x%x\n",
		read_buffer2[0], read_buffer2[1], read_buffer2[2], read_buffer2[3], read_buffer2[4]);
	*boot_check_sum = read_buffer1[0];
	*rx_check_sum = read_buffer1[1];
	*tx_check_sum = read_buffer1[2];
	*boot_ver = read_buffer2[2];
	*rx_ver = read_buffer2[1];
	*tx_ver = read_buffer2[0];

	return true;
}

static bool nu1619_check_firmware_version(struct oplus_nu1619 *chip)
{
	char boot_checksum = 0;
	char rx_checksum = 0;
	char tx_checksum = 0;
	char boot_version = 0;
	char rx_version = 0;
	char tx_version = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	nu1619_req_checksum_and_fw_version(chip, &boot_checksum, &rx_checksum, &tx_checksum,
		&boot_version, &rx_version, &tx_version);
	pr_err("<FW UPDATE> check_sum: 0x%x,0x%x,0x%x  version:0x%x,0x%x,0x%x\n",
		boot_checksum, rx_checksum, tx_checksum, boot_version, rx_version, tx_version);
	pr_err("<FW UPDATE> boot (0xff&(~fw_boot_data[fw_boot_length-5]))=0x%x\n",
		(0xff & (~chip->fw_boot_data[chip->fw_boot_length - 5])));
	pr_err("<FW UPDATE> rx (0xff&(~fw_rx_data[fw_rx_length-5]))=0x%x\n",
		(0xff & (~chip->fw_rx_data[chip->fw_rx_length - 5])));
	pr_err("<FW UPDATE> tx (0xff&(~fw_tx_data[fw_tx_length-5]))=0x%x\n",
		(0xff & (~chip->fw_tx_data[chip->fw_tx_length - 5])));

	chip->fw_rx_version = rx_version;
	chip->fw_tx_version = tx_version;

	if ((boot_version == (0xff & (~chip->fw_boot_data[chip->fw_boot_length - 5]))) && (boot_checksum == 0x66)) {
		chip->boot_no_need_update = true;
		pr_err("<FW UPDATE> boot_no_need_update=true\n");
	}
	if ((rx_version == (0xff & (~chip->fw_rx_data[chip->fw_rx_length - 5]))) && (rx_checksum == 0x66)) {
		chip->rx_no_need_update = true;
		pr_err("<FW UPDATE> rx_no_need_update=true\n");
	}
	if ((tx_version == (0xff & (~chip->fw_tx_data[chip->fw_tx_length - 5]))) && (tx_checksum == 0x66)) {
		chip->tx_no_need_update = true;
		pr_err("<FW UPDATE> tx_no_need_update=true\n");
	}

	pr_err("<FW UPDATE> boot_no_need_update=%d, rx_no_need_update=%d, tx_no_need_update=%d\n",
		chip->boot_no_need_update, chip->rx_no_need_update, chip->tx_no_need_update);

	if ((chip->boot_no_need_update) && (chip->rx_no_need_update) && (chip->tx_no_need_update))
		return false;
	return true;
}

static bool nu1619_check_firmware(struct oplus_nu1619 *chip)
{
	char boot_checksum = 0;
	char rx_checksum = 0;
	char tx_checksum = 0;
	char boot_version = 0;
	char rx_version = 0;
	char tx_version = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	nu1619_req_checksum_and_fw_version(chip, &boot_checksum, &rx_checksum, &tx_checksum,
		&boot_version, &rx_version, &tx_version);
	pr_err("<FW UPDATE> check_sum: 0x%x,0x%x,0x%x  version:0x%x,0x%x,0x%x\n",
		boot_checksum, rx_checksum, tx_checksum, boot_version, rx_version, tx_version);

	chip->fw_rx_version = rx_version;
	chip->fw_tx_version = tx_version;

	if ((boot_checksum == 0x66) && (rx_checksum == 0x66) && (tx_checksum == 0x66)) {
		return true;
	} else {
		return false;
	}
}

static bool nu1619_onekey_download_firmware(struct oplus_nu1619 *chip)
{
	bool status = false;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	pr_err("<FW UPDATE> enter\n");

	if (!nu1619_check_i2c_is_ok(chip)) {
		pr_err("<FW UPDATE> i2c error!\n");
		nu1619_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_I2C);
		return false;
	} else {
		pr_err("<FW UPDATE> i2c success!\n");
	}

	status = nu1619_check_firmware_version(chip);
	if (!status) {
		pr_err("<FW UPDATE> fw is the same, no need download fw!!!\n");
		return true;
	}

	pr_err("<FW UPDATE> ************reset power 1************\n");
	nu1619_set_trx_boost_enable(chip, false);
	msleep(100);
	nu1619_set_trx_boost_vol(chip, NU1619_MTP_VOL_MV);
	nu1619_set_trx_boost_enable(chip, true);
	msleep(100);

	status = nu1619_download_firmware(chip);
	if (!status) {
		pr_err("<FW UPDATE> nu1619_download_firmware fail!!!\n");
		nu1619_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_OTHER);
		return false;
	}

	pr_err("<FW UPDATE> ************reset power 2************\n");
	nu1619_set_trx_boost_enable(chip, false);
	msleep(100);
	nu1619_set_trx_boost_vol(chip, NU1619_MTP_VOL_MV);
	nu1619_set_trx_boost_enable(chip, true);
	msleep(100);

	status = nu1619_check_firmware(chip);
	if (!status) {
		pr_err("<FW UPDATE> nu1619_check_firmware fail!!!\n");
		nu1619_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_CRC);
		return false;
	}
	pr_err("<FW UPDATE> end\n");

	return true;
}

static int nu1619_check_idt_fw_update(struct oplus_chg_ic_dev *dev, char *fw_img, int fw_len)
{
	int rc = 0;
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	pr_err("<FW UPDATE> check idt fw update<><><><><><><><>\n");

	disable_irq(chip->rx_con_irq);
	disable_irq(chip->rx_event_irq);

	rc = nu1619_set_trx_boost_vol(chip, NU1619_MTP_VOL_MV);
	if (rc < 0) {
		pr_err("set trx power vol(=%d), rc=%d\n", NU1619_MTP_VOL_MV, rc);
		return rc;
	}
	rc = nu1619_set_trx_boost_enable(chip, true);
	if (rc < 0) {
		pr_err("enable trx power error, rc=%d\n", rc);
		return rc;
	}
	msleep(100);

	if (nu1619_onekey_download_firmware(chip) == true)
		pr_err("<FW UPDATE> check idt fw update ok<><><><><><><><>\n");
	else
		pr_err("<FW UPDATE> check idt fw update fail<><><><><><><><>\n");

	msleep(100);
	nu1619_set_trx_boost_enable(chip, false);
	msleep(20);

	enable_irq(chip->rx_con_irq);
	enable_irq(chip->rx_event_irq);

	return rc;
}

static int nu1619_get_rx_version(struct oplus_chg_ic_dev *dev, u32 *version)
{
	struct oplus_nu1619 *chip;

	*version = 0;
	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	*version = chip->fw_rx_version;

	return 0;
}

static int nu1619_get_tx_version(struct oplus_chg_ic_dev *dev, u32 *version)
{
	struct oplus_nu1619 *chip;

	*version = 0;
	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return false;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return -ENODEV;
	}

	*version = chip->fw_tx_version;

	return 0;
}

static int nu1619_enter_read_mode(struct oplus_nu1619 *chip)
{
	int ret;

	pr_err("<FW UPDATE> nu1619_enter_read_mode\n");
	ret = nu1619_write_byte(chip, 0x0017, 0x01);
	if (ret < 0)
		return ret;

	return 1;
}

static int nu1619_read_pause(struct oplus_nu1619 *chip)
{
	int ret;

	/*pr_err("<FW UPDATE> nu1619_read_pause\n");*/
	ret = nu1619_write_byte(chip, 0x0018, 0x02);
	if (ret < 0)
		return ret;

	ret = nu1619_write_byte(chip, 0x0018, 0x00);
	if (ret < 0)
		return ret;

	return 1;
}

static int nu1619_exit_read_mode(struct oplus_nu1619 *chip)
{
	int ret;

	pr_err("<FW UPDATE> nu1619_exit_read_mode\n");
	ret = nu1619_write_byte(chip, 0x0017, 0x00);
	if (ret < 0)
		return ret;

	return 1;
}

static bool nu1619_upgrade_firmware(struct oplus_nu1619 *chip, u8 *fw_buf, int fw_size)
{
	u16 addr = 0;
	u8 addr_h = 0;
	u8 addr_l = 0;
	int i = 0;
	int j = 0;
	u8 read_buf[4] = {0, 0, 0, 0};
	u8 *fw_data = NULL;
	int pass_count = 0;
	int ret = 0;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return false;
	}

	if (fw_buf == NULL) {
		pr_err("fw_buf == NULL\n");
		return false;
	}

	pr_err("<FW UPDATE> enter\n");

	if (!nu1619_check_i2c_is_ok(chip) ||
	    chip->debug_force_update_err == TRACK_WLS_UPDATE_ERR_I2C) {
		pr_err("<FW UPDATE> i2c error!\n");
		nu1619_track_upload_wls_update_err_info(
			chip, TRACK_WLS_UPDATE_ERR_I2C);
		return false;
	} else {
		pr_err("<FW UPDATE> i2c success!\n");
	}

	if (fw_buf[fw_size - 4] == 0x52)
		addr = 256;/*RX_AREA;*/
	else if (fw_buf[fw_size - 4] == 0x54)
		addr = 4864;/*TX_AREA;*/
	else if (fw_buf[fw_size - 4] == 0x42)
		addr = 0;/*BOOT_AREA;*/

	pr_err("<FW UPDATE> download %s fw enter\n", (addr == 256) ? "rx" : "tx/boot");

	fw_data = fw_buf;

	pr_err("<FW UPDATE> fw_size[%d]\n", fw_size);

	/************prepare_for_mtp_write begin************/
	ret = nu1619_enter_dtm_mode(chip);
	if (ret < 0)
		return false;
	pr_err("<FW UPDATE> enter_dtm_mode OK\n");

	ret = nu1619_config_gpio_1v8(chip);
	if (ret < 0)
		return false;
	pr_err("<FW UPDATE> config_gpio_1v8 OK\n");
	/************prepare_for_mtp_write end************/

	msleep(20);

	/************write_mtp_addr begin************/
	addr_h = (u8)(addr >> 8);
	addr_l = (u8)(addr & 0xff);
	ret = nu1619_write_byte(chip, 0x0010, addr_h);
	if (ret < 0)
		return false;
	ret = nu1619_write_byte(chip, 0x0011, addr_l);
	if (ret < 0)
		return false;
	/************write_mtp_addr end************/

	/************enable write begin************/
	ret = nu1619_enter_write_mode(chip);
	if (ret < 0)
		return false;
	pr_err("<FW UPDATE> enter_write_mode OK\n");
	/************enable write end************/

	/************write data begin************/
	for (i = 0; i < fw_size; i += 4) {
		nu1619_write_byte(chip, 0x0012, 0xff & (~fw_data[i + 3]));
		usleep_range(1000, 1100);
		nu1619_write_byte(chip, 0x0012, 0xff & (~fw_data[i + 2]));
		usleep_range(1000, 1100);
		nu1619_write_byte(chip, 0x0012, 0xff & (~fw_data[i + 1]));
		usleep_range(1000, 1100);
		nu1619_write_byte(chip, 0x0012, 0xff & (~fw_data[i + 0]));
		usleep_range(1000, 1100);
	}
	/************write data end************/

	/************end************/
	ret = nu1619_exit_write_mode(chip);
	if (ret < 0)
		return false;
	pr_err("<FW UPDATE> exit_write_mode OK\n");
	/************end************/

	/************exit dtm begin************/
	ret = nu1619_exit_dtm_mode(chip);
	if (ret < 0)
		return false;
	pr_err("<FW UPDATE> download rx exit\n");
	/************exit dtm end************/

	msleep(500);

	pr_err("<FW UPDATE> check rx enter\n");
	pass_count = 0;

	/************prepare_for_mtp_read begin************/
	ret = nu1619_enter_dtm_mode(chip);
	if (ret < 0)
		return false;

	ret = nu1619_enter_read_mode(chip);
	if (ret < 0)
		return false;
	/************prepare_for_mtp_read end************/

	msleep(10);

	for (i = 0; i < fw_size; i += 4) {
		/************write_mtp_addr begin************/
		addr_h = (u8)(addr >> 8);
		addr_l = (u8)(addr & 0xff);
		nu1619_write_byte(chip, 0x0010, addr_h);
		nu1619_write_byte(chip, 0x0011, addr_l);
		/************write_mtp_addr end************/

		addr++;

		/************read pause begin************/
		ret = nu1619_read_pause(chip);
		if (ret < 0)
			return false;
		/************read pause end************/

		/************read data begin************/
		nu1619_read_data(chip, 0x0013, &read_buf[3], 1);
		nu1619_read_data(chip, 0x0014, &read_buf[2], 1);
		nu1619_read_data(chip, 0x0015, &read_buf[1], 1);
		nu1619_read_data(chip, 0x0016, &read_buf[0], 1);
		/************read data end************/

		if (i < 20) {
			dev_err(chip->dev, "read_buf[3]=0x%x \n", read_buf[3]);
			dev_err(chip->dev, "read_buf[2]=0x%x \n", read_buf[2]);
			dev_err(chip->dev, "read_buf[1]=0x%x \n", read_buf[1]);
			dev_err(chip->dev, "read_buf[0]=0x%x \n", read_buf[0]);

			dev_err(chip->dev, "fw_data[3]=0x%x \n", (0xff & (~fw_data[i + 3])));
			dev_err(chip->dev, "fw_data[2]=0x%x \n", (0xff & (~fw_data[i + 3])));
			dev_err(chip->dev, "fw_data[1]=0x%x \n", (0xff & (~fw_data[i + 1])));
			dev_err(chip->dev, "fw_data[0]=0x%x \n", (0xff & (~fw_data[i + 0])));
		}

		if ((read_buf[0] == (0xff & (~fw_data[i + 0])))
				&& (read_buf[1] == (0xff & (~fw_data[i + 1])))
				&& (read_buf[2] == (0xff & (~fw_data[i + 2])))
				&& (read_buf[3] == (0xff & (~fw_data[i + 3])))) {
			pass_count++;
		} else {
			j++;
			if (j >= 50) {
				goto CCC;
			}
		}
	}

CCC:
	/***********exit read begin************/
	ret = nu1619_exit_read_mode(chip);
	if (ret < 0)
		return false;
	/***********exit read end************/

	/************exit dtm begin************/
	ret = nu1619_exit_dtm_mode(chip);
	if (ret < 0)
		return false;
	/************exit dtm end************/

	pr_err("<FW UPDATE> error_count=%d, pass_count=%d, chip->fw_rx_length=%ld\n",
		j, pass_count, chip->fw_rx_length);

	pr_err("<FW UPDATE> fw_data version=0x%x, 0x%x, 0x%x, 0x%x\n",
		0xff & (~fw_data[fw_size - 4]), 0xff & (~fw_data[fw_size - 3]),
		0xff & (~fw_data[fw_size - 2]), 0xff & (~fw_data[fw_size - 1]));

	/*rx_fw_version = fw_data[fw_buf - 1];
	rx_bin_project_id_h = fw_data[fw_buf - 3];
	rx_bin_project_id_l = fw_data[fw_buf - 2];*/

	pr_err("<FW UPDATE> check rx exit\n");

	if (fw_size == (pass_count * 4)) {
		pr_err("<FW UPDATE> push fw success!\n");
		if (fw_buf[fw_size - 4] == 0x52)
			chip->fw_rx_version = fw_data[fw_size - 1];
		else if (fw_buf[fw_size - 4] == 0x54)
			chip->fw_tx_version = fw_data[fw_size - 1];
		return true;
	} else {
		pr_err("<FW UPDATE> push fw fail!!!\n");
		return false;
	}
}

static int nu1619_upgrade_firmware_by_buf(struct oplus_chg_ic_dev *dev, unsigned char *fw_buf, int fw_size)
{
	struct oplus_nu1619 *chip;
	int rc = 0;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);

	if (fw_buf == NULL) {
		pr_err("fw_buf is NULL\n");
		return -EINVAL;
	}

	pr_err("<FW UPDATE> check idt fw update<><><><><><><><>\n");

	disable_irq(chip->rx_con_irq);
	disable_irq(chip->rx_event_irq);

	rc = nu1619_set_trx_boost_vol(chip, NU1619_MTP_VOL_MV);
	if (rc < 0) {
		pr_err("set trx power vol(=%d), rc=%d\n", NU1619_MTP_VOL_MV, rc);
		return rc;
	}
	rc = nu1619_set_trx_boost_enable(chip, true);
	if (rc < 0) {
		pr_err("enable trx power error, rc=%d\n", rc);
		return rc;
	}
	msleep(100);

	if (nu1619_upgrade_firmware(chip, fw_buf, fw_size) == true)
		pr_err("<FW UPDATE> check idt fw update ok<><><><><><><><>\n");
	else
		pr_err("<FW UPDATE> check idt fw update fail<><><><><><><><>\n");

	msleep(100);
	(void)nu1619_set_trx_boost_enable(chip, false);
	msleep(20);

	enable_irq(chip->rx_con_irq);
	enable_irq(chip->rx_event_irq);

	return rc;
}

static void nu1619_clear_irq(struct oplus_nu1619 *chip)
{
	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return;
	}

	pr_err("nu1619_clear_irq----------\n");
	nu1619_write_byte(chip, 0x0000, 0xff);
	nu1619_write_byte(chip, 0x0001, 0xff);
	nu1619_write_cmd_d(chip, 0x08);
	return;
}

static void nu1619_increase_trx_boost_vol(struct oplus_nu1619 *chip)
{
	int i;
	int j;
	int value;
	int rc;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return;
	}

	for (i = 1; i < NU1619_TRX_VOL_MAX_MV / NU1619_TRX_VOL_STEP_MV; i++) {
		value = NU1619_TRX_VOL_START_MV + i * NU1619_TRX_VOL_STEP_MV;
		if (value > NU1619_TRX_VOL_MAX_MV)
			break;
		for (j = 0; j < NU1619_WAIT_INC_DELAY_MS / NU1619_WAIT_INC_STEP_MS; j++) {
			msleep(NU1619_WAIT_INC_STEP_MS);
			if (nu1619_is_in_tx_mode(chip) == false)
				goto end;
		}
		rc = nu1619_set_trx_boost_vol(chip, value);
		if (rc < 0) {
			pr_err("set trx boost vol(=%d), rc=%d\n", value, rc);
			return;
		}
	}
end:
	return;
}

static void nu1619_tx_event_config(struct oplus_nu1619 *chip, int status, int err)
{
	int tx_err = TRACK_WLS_TRX_ERR_DEFAULT;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return;
	}

	if (err != 0) {
		switch (err) {
		case NU1619_TX_ERR_RXAC:
			chip->tx_status = TX_STATUS_ERR_RXAC;
			tx_err = TRACK_WLS_TRX_ERR_RXAC;
			break;
		case NU1619_TX_ERR_OCP:
			chip->tx_status = TX_STATUS_ERR_OCP;
			tx_err = TRACK_WLS_TRX_ERR_OCP;
			break;
		case NU1619_TX_ERR_OVP:
			chip->tx_status = TX_STATUS_ERR_OVP;
			tx_err = TRACK_WLS_TRX_ERR_OVP;
			break;
		case NU1619_TX_ERR_LVP:
			chip->tx_status = TX_STATUS_ERR_LVP;
			tx_err = TRACK_WLS_TRX_ERR_LVP;
			break;
		case NU1619_TX_ERR_FOD:
			chip->tx_status = TX_STATUS_ERR_FOD;
			tx_err = TRACK_WLS_TRX_ERR_FOD;
			break;
		case NU1619_TX_ERR_OTP:
			chip->tx_status = TX_STATUS_ERR_OTP;
			tx_err = TRACK_WLS_TRX_ERR_OTP;
			break;
		case NU1619_TX_ERR_CEPTIMEOUT:
			chip->tx_status = TX_STATUS_ERR_CEPTIMEOUT;
			break;
		case NU1619_TX_ERR_RXEPT:
			chip->tx_status = TX_STATUS_ERR_RXEPT;
			tx_err = TRACK_WLS_TRX_ERR_RXEPT;
			break;
		default:
			break;
		}
	} else {
		switch (status) {
		case NU1619_TX_STATUS_READY:
			chip->tx_status = TX_STATUS_READY;
			break;
		case NU1619_TX_STATUS_DIGITALPING:
		case NU1619_TX_STATUS_ANALOGPING:
			chip->tx_status = TX_STATUS_PING_DEVICE;
			break;
		case NU1619_TX_STATUS_TRANSFER:
			chip->tx_status = TX_STATUS_TRANSFER;
			break;
		default:
			break;
		}
	}

	if (chip->debug_force_tx_err)
		tx_err = chip->debug_force_tx_err;
	if (tx_err != TRACK_WLS_TRX_ERR_DEFAULT)
		nu1619_track_upload_wls_tx_err_info(chip, tx_err);
	return;
}

static void nu1619_event_process(struct oplus_nu1619 *chip)
{
	int rc = -1;
	char temp[3] = {0, 0, 0};
	char val_buf[6] = {0, 0, 0, 0, 0, 0};
	int retry_counts = 0;
	union oplus_chg_mod_propval temp_val = {0, };
	int rx_err = TRACK_WLS_TRX_ERR_DEFAULT;

	if (chip == NULL) {
		pr_err("oplus_nu1619 is NULL\n");
		return;
	}

	if (!nu1619_rx_is_enable(chip->ic_dev) && nu1619_is_in_tx_mode(chip) == false) {
		pr_info("RX is sleep or TX is disable, Ignore events\n");
		return;
	}

	rc = nu1619_read_data(chip, NU1619_REG_STATUS, temp, 2);
	if (rc) {
		pr_err("Couldn't read 0x%04x rc = %x\n", NU1619_REG_STATUS, rc);
		temp[0] = 0;
		temp[1] = 0;
	} else {
		pr_err("read 0x23 0x24 = 0x%02x 0x%02x\n", temp[0], temp[1]);
	}

	if (nu1619_is_in_tx_mode(chip) == true) {
		if (temp[0] == NU1619_TX_INCREASE_BOOST_VOL) {
			pr_err("increase tx vol!\n");
			nu1619_increase_trx_boost_vol(chip);
		} else if (temp[0] == NU1619_TX_DECREASE_BOOST_VOL) {
			pr_err("decrease tx vol!\n");
			nu1619_set_trx_boost_vol(chip, NU1619_TRX_VOL_START_MV);
		}
		nu1619_tx_event_config(chip, temp[0], temp[1]);
		if (is_wls_ocm_available(chip))
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_CHECK_TRX);
		else
			pr_err("wls ocm not found\n");

		goto out;
	} else {
		chip->tx_status = TX_STATUS_OFF;
	}

	if ((temp[0] & NU1619_LDO_ON) == NU1619_LDO_ON && temp[1] == 0x00) {
		pr_err("LDO is on, connected.\n");
		complete(&chip->ldo_on);
		if (is_wls_ocm_available(chip))
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_ONLINE);
		chip->connected_ldo_on = true;
	}

	if (chip->rx_connected == true) {
		if (temp[0] & NU1619_RX_ERR_OCP) {
			rx_err = TRACK_WLS_TRX_ERR_OCP;
			pr_err("rx OCP happen!\n");
		} else if (temp[0] & NU1619_RX_ERR_OVP) {
			rx_err = TRACK_WLS_TRX_ERR_OVP;
			pr_err("rx OVP happen!\n");
		} else if (temp[0] & NU1619_RX_ERR_OTP) {
			rx_err = TRACK_WLS_TRX_ERR_OTP;
			pr_err("rx OTP happen!\n");
		} else if (temp[0] & NU1619_VOUT_ERR) {
			rx_err = TRACK_WLS_TRX_ERR_VOUT;
			pr_err("rx VOUT_ERR happen!\n");
		}
		if (temp[1] & NU1619_RX_ERR_UV_ALARM) {
			if (is_wls_ocm_available(chip)) {
				pr_err("rx UVP happen!\n");
				temp_val.intval = true;
				oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_RX_VOUT_UVP, &temp_val);
			}
		}
		if (temp[1] & NU1619_RX_ERR_UV_ALARM_CLEAR) {
			if (is_wls_ocm_available(chip)) {
				pr_err("rx UVP clear!\n");
				temp_val.intval = false;
				oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_RX_VOUT_UVP, &temp_val);
			}
		}

		if (chip->debug_force_rx_err)
			rx_err = chip->debug_force_rx_err;
		if (rx_err != TRACK_WLS_TRX_ERR_DEFAULT)
			nu1619_track_upload_wls_rx_err_info(chip, rx_err);
	}

	if (temp[1] & 0x01) {
		chip->rx_pwr_cap = nu1619_get_power_cap(chip);
		if (is_wls_ocm_available(chip))
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_TX_EPP_CAP);
	}

	if (temp[0] & NU1619_EVENT) {
retry:
		rc = 0;
		nu1619_write_byte(chip, 0x000c, 0x87);
		nu1619_read_data(chip, 0x0008, temp, 3);
		if (temp[0] == (0x87 ^ 0x80)) {
			rc += 1;
			val_buf[0] = temp[1];
			val_buf[1] = temp[2];
		}
		nu1619_write_byte(chip, 0x000c, 0x89);
		nu1619_read_data(chip, 0x0008, temp, 3);
		if (temp[0] == (0x89 ^ 0x80)) {
			rc += 1;
			val_buf[2] = temp[1];
			val_buf[3] = temp[2];
		}
		nu1619_write_byte(chip, 0x000c, 0x8b);
		nu1619_read_data(chip, 0x0008, temp, 3);
		if (temp[0] == (0x8b ^ 0x80)) {
			rc += 1;
			val_buf[4] = temp[1];
			val_buf[5] = temp[2];
		}

		if (rc != 3) {
			retry_counts++;
			if (retry_counts < 3) {
				pr_err("data error: 0x%04x rc=%x, retry[%d].\n", 0x0058, rc, retry_counts);
				goto retry;
			} else
				pr_err("data error: 0x%04x rc=%x\n", 0x0058, rc);
		} else {
			pr_err("Received TX data: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
				val_buf[0], val_buf[1], val_buf[2], val_buf[3], val_buf[4], val_buf[5]);
			if (chip->rx_msg.msg_call_back != NULL)
					chip->rx_msg.msg_call_back(chip->rx_msg.dev_data, val_buf);
		}
	}

out:
	nu1619_clear_irq(chip);
}

static int nu1619_connect_check(struct oplus_chg_ic_dev *dev)
{
	struct oplus_nu1619 *chip;

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(dev);
	schedule_delayed_work(&chip->connect_work, 0);

	return 0;
}

static int nu1619_set_chip_info(struct oplus_chg_ic_dev *dev)
{

	if (dev == NULL) {
		pr_err("oplus_chg_ic_dev is NULL\n");
		return false;
	}
	snprintf(dev->manu_name, 15, "%s", "NV_NU1619");

	return true;
}

static struct oplus_chg_ic_rx_ops nu1619_dev_ops = {
	.rx_set_enable = nu1619_set_rx_enable,
	.rx_is_enable = nu1619_rx_is_enable,
	.rx_is_connected = nu1619_rx_is_connected,
	.rx_get_vout = nu1619_get_vout,
	.rx_set_vout = nu1619_set_rx_charge_voltage,
	.rx_get_vrect = nu1619_get_vrect,
	.rx_get_iout = nu1619_get_iout,
	.rx_get_trx_vol = nu1619_get_tx_vout,
	.rx_get_trx_curr = nu1619_get_tx_iout,
	.rx_get_cep_count = nu1619_get_cep_count,
	.rx_get_cep_val = nu1619_get_cep_val,
	.rx_get_work_freq = nu1619_get_work_freq,
	.rx_get_rx_mode = nu1619_get_rx_mode,
	.rx_set_rx_mode = nu1619_set_rx_mode,
	.rx_set_dcdc_enable = nu1619_set_dcdc_enable,
	.rx_set_trx_enable = nu1619_set_tx_enable,
	.rx_set_trx_start = nu1619_set_tx_start,
	.rx_get_trx_status = nu1619_get_tx_status,
	.rx_get_trx_err = nu1619_get_tx_err,
	.rx_get_headroom = nu1619_get_headroom,
	.rx_set_headroom = nu1619_set_headroom,
	.rx_send_match_q = nu1619_send_match_q,
	.rx_set_fod_parm = nu1619_set_fod_parm,
	.rx_send_msg = nu1619_send_msg,
	.rx_register_msg_callback = nu1619_register_msg_callback,
	.rx_get_rx_version = nu1619_get_rx_version,
	.rx_get_trx_version = nu1619_get_tx_version,
	.rx_upgrade_firmware_by_buf = nu1619_upgrade_firmware_by_buf,
	.rx_upgrade_firmware_by_img = nu1619_check_idt_fw_update,
	.rx_connect_check = nu1619_connect_check,
};

/*static void nu1619_check_event_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, check_event_work);
	bool od2_state = 0;

	od2_state = nu1619_get_rx_event_gpio_val(chip);

	pr_info("od2_state = %s", od2_state ? "true" : "false");
	if (od2_state == false) {
		msleep(50);
		od2_state = nu1619_get_rx_event_gpio_val(chip);
		if (od2_state == false) {
			pr_err("OD2 is low, reread the event.\n");
			nu1619_event_process(chip);
			schedule_delayed_work(&chip->check_event_work, msecs_to_jiffies(1000));
		}
	}
}*/

static void nu1619_check_ldo_on_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, check_ldo_on_work);
	int iout = 0;

	pr_info("connected_ldo_on = %s\n", chip->connected_ldo_on ? "true" : "false");
	if ((!chip->connected_ldo_on) && nu1619_rx_is_connected(chip->ic_dev)) {
		pr_err("Connect but no ldo on event irq, check again.\n");
		nu1619_get_iout(chip->ic_dev, &iout);
		if (iout >= LDO_ON_MA) {
			if (is_wls_ocm_available(chip))
				oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_ONLINE);
			chip->connected_ldo_on = true;
		} else {
			schedule_delayed_work(&chip->check_ldo_on_work, NU1619_CHECK_LDO_ON_DELAY);
		}
	}
}

static void nu1619_event_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, event_work);

	if (chip->rx_connected == true || nu1619_is_in_tx_mode(chip) == true)
		nu1619_event_process(chip);
}

static void nu1619_connect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_nu1619 *chip =
		container_of(dwork, struct oplus_nu1619, connect_work);
	bool connected = false;
	bool pre_connected = false;

	pre_connected = nu1619_rx_is_connected(chip->ic_dev);
retry:
	reinit_completion(&chip->ldo_on);
	(void)wait_for_completion_timeout(&chip->ldo_on, msecs_to_jiffies(50));
	connected = nu1619_rx_is_connected(chip->ic_dev);
	if (connected != pre_connected) {
		pre_connected = connected;
		pr_err("retry to check connect\n");
		goto retry;
	}
	if (is_wls_ocm_available(chip)) {
		if (chip->rx_connected != connected)
			pr_err("!!!!! rx_connected[%d] -> con_gpio[%d]\n", chip->rx_connected, connected);
		if (connected && chip->rx_connected == false) {
			chip->rx_connected = true;
			chip->connected_ldo_on = false;
			if (nu1619_get_mode_sw_active(chip))
				nu1619_set_mode_sw_default(chip);
			cancel_delayed_work_sync(&chip->check_ldo_on_work);
			schedule_delayed_work(&chip->check_ldo_on_work, NU1619_CHECK_LDO_ON_DELAY);
			oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_PRESENT);
			if (nu1619_get_running_mode(chip) == NU1619_RX_MODE_EPP)
				oplus_chg_anon_mod_event(chip->wls_ocm, OPLUS_CHG_EVENT_TX_EPP_CAP);
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

static irqreturn_t nu1619_event_handler(int irq, void *dev_id)
{
	struct oplus_nu1619 *chip = dev_id;

	pr_err("!!!event irq\n");
	schedule_delayed_work(&chip->event_work, 0);
	return IRQ_HANDLED;
}

static irqreturn_t nu1619_connect_handler(int irq, void *dev_id)
{
	struct oplus_nu1619 *chip = dev_id;

	pr_err("!!!connect irq\n");
	schedule_delayed_work(&chip->connect_work, 0);
	return IRQ_HANDLED;
}

static int nu1619_gpio_init(struct oplus_nu1619 *chip)
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
	rc = devm_request_irq(chip->dev, chip->rx_event_irq, nu1619_event_handler,
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
	rc = devm_request_irq(chip->dev, chip->rx_con_irq, nu1619_connect_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"rx_con_irq", chip);
	if (rc < 0) {
		pr_err("rx_con_irq request error, rc=%d\n", rc);
		goto free_con_gpio;
	}
	enable_irq_wake(chip->rx_con_irq);

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
	pr_err("set rx_en_gpio value:0, gpio_val:%d\n", gpio_get_value(chip->rx_en_gpio));

	chip->dcdc_en_gpio = of_get_named_gpio(node, "oplus,dcdc_en-gpio", 0);
	if (!gpio_is_valid(chip->dcdc_en_gpio)) {
		pr_err("dcdc_en_gpio not specified\n");
		goto free_en_gpio;
	}
	rc = gpio_request(chip->dcdc_en_gpio, "dcdc_en-gpio");
	if (rc < 0) {
		pr_err("dcdc_en_gpio request error, rc=%d\n", rc);
		goto free_en_gpio;
	}
	chip->dcdc_en_active = pinctrl_lookup_state(chip->pinctrl, "dcdc_en_active");
	if (IS_ERR_OR_NULL(chip->dcdc_en_active)) {
		pr_err("get dcdc_en_active fail\n");
		goto free_dcdc_en_gpio;
	}
	chip->dcdc_en_sleep = pinctrl_lookup_state(chip->pinctrl, "dcdc_en_sleep");
	if (IS_ERR_OR_NULL(chip->dcdc_en_sleep)) {
		pr_err("get dcdc_en_sleep fail\n");
		goto free_dcdc_en_gpio;
	}
	gpio_direction_output(chip->dcdc_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->dcdc_en_sleep);

	chip->mode_sw_gpio = of_get_named_gpio(node, "oplus,mode_sw-gpio", 0);
	if (!gpio_is_valid(chip->mode_sw_gpio)) {
		pr_err("mode_sw_gpio not specified\n");
		goto exit;
	}
	rc = gpio_request(chip->mode_sw_gpio, "mode_sw-gpio");
	if (rc < 0) {
		pr_err("mode_sw_gpio request error, rc=%d\n", rc);
		goto exit;
	}
	chip->mode_sw_active = pinctrl_lookup_state(chip->pinctrl, "mode_sw_active");
	if (IS_ERR_OR_NULL(chip->mode_sw_active)) {
		pr_err("get mode_sw_active fail\n");
		goto free_mode_sw_gpio;
	}
	chip->mode_sw_sleep = pinctrl_lookup_state(chip->pinctrl, "mode_sw_sleep");
	if (IS_ERR_OR_NULL(chip->mode_sw_sleep)) {
		pr_err("get mode_sw_sleep fail\n");
		goto free_mode_sw_gpio;
	}
	gpio_direction_output(chip->mode_sw_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->mode_sw_sleep);

exit:
	return 0;
free_mode_sw_gpio:
	if (!gpio_is_valid(chip->mode_sw_gpio))
		gpio_free(chip->mode_sw_gpio);
free_dcdc_en_gpio:
	if (!gpio_is_valid(chip->dcdc_en_gpio))
		gpio_free(chip->dcdc_en_gpio);
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

static void nu1619_shutdown(struct i2c_client *client)
{
	struct oplus_nu1619 *chip = i2c_get_clientdata(client);
	bool is_connected = false;
	int wait_wpc_disconn_cnt = 0;

	disable_irq(chip->rx_con_irq);
	disable_irq(chip->rx_event_irq);

	is_connected = nu1619_rx_is_connected(chip->ic_dev);
	if (is_connected && (nu1619_get_wls_type(chip) == OPLUS_CHG_WLS_VOOC
			|| nu1619_get_wls_type(chip) == OPLUS_CHG_WLS_SVOOC
			|| nu1619_get_wls_type(chip) == OPLUS_CHG_WLS_PD_65W)) {
		nu1619_set_rx_enable(chip->ic_dev, false);
		msleep(100);
		while (wait_wpc_disconn_cnt < 10) {
			is_connected = nu1619_rx_is_connected(chip->ic_dev);
			if (!is_connected)
				break;
			msleep(150);
			wait_wpc_disconn_cnt++;
		}
	}
	return;
}

static struct regmap_config nu1619_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xFFFF,
};

static int nu1619_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct oplus_nu1619 *chip;
	struct device_node *node = client->dev.of_node;
	enum oplus_chg_ic_type ic_type;
	int ic_index = 0;
	int rc = 0;

	pr_err("call !\n");
	chip = devm_kzalloc(&client->dev, sizeof(struct oplus_nu1619), GFP_KERNEL);
	if (!chip) {
		pr_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	chip->dev = &client->dev;
	chip->regmap = devm_regmap_init_i2c(client, &nu1619_regmap_config);
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
	chip->fw_boot_data = (char *)of_get_property(node, "oplus,fw_boot_data", &chip->fw_boot_length);
	if (!chip->fw_boot_data) {
		pr_err("parse fw boot data failed\n");
		chip->fw_boot_data = nu1619_fw_boot_data;
		chip->fw_boot_length = sizeof(nu1619_fw_boot_data);
	} else {
		pr_err("parse fw boot data length[%d]\n", chip->fw_boot_length);
	}
	chip->fw_rx_data = (char *)of_get_property(node, "oplus,fw_rx_data", &chip->fw_rx_length);
	if (!chip->fw_rx_data) {
		pr_err("parse fw rx data failed\n");
		chip->fw_rx_data = nu1619_fw_rx_data;
		chip->fw_rx_length = sizeof(nu1619_fw_rx_data);
	} else {
		pr_err("parse fw rx data length[%d]\n", chip->fw_rx_length);
	}
	chip->fw_tx_data = (char *)of_get_property(node, "oplus,fw_tx_data", &chip->fw_tx_length);
	if (!chip->fw_tx_data) {
		pr_err("parse fw tx data failed\n");
		chip->fw_tx_data = nu1619_fw_tx_data;
		chip->fw_tx_length = sizeof(nu1619_fw_tx_data);
	} else {
		pr_err("parse fw tx data length[%d]\n", chip->fw_tx_length);
	}
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, node->name, ic_index);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		pr_err("register %s error\n", node->name);
		goto reg_ic_err;
	}
	chip->ic_dev->dev_ops = &nu1619_dev_ops;
	chip->ic_dev->type = ic_type;

	rc = nu1619_gpio_init(chip);
	if (rc < 0) {
		pr_err("nu1619 gpio init error.");
		goto gpio_init_err;
	}

	nu1619_set_chip_info(chip->ic_dev);
	nu1619_track_init(chip);

	device_init_wakeup(chip->dev, true);

	INIT_DELAYED_WORK(&chip->event_work, nu1619_event_work);
	INIT_DELAYED_WORK(&chip->connect_work, nu1619_connect_work);
	INIT_DELAYED_WORK(&chip->check_ldo_on_work, nu1619_check_ldo_on_work);
	//INIT_DELAYED_WORK(&chip->check_event_work, nu1619_check_event_work);
	mutex_init(&chip->i2c_lock);
	init_completion(&chip->ldo_on);
	schedule_delayed_work(&chip->connect_work, 0);
	schedule_delayed_work(&chip->event_work, msecs_to_jiffies(1000));
	pr_err("call end!\n");
	return 0;

gpio_init_err:
	devm_oplus_chg_ic_unregister(chip->dev, chip->ic_dev);
reg_ic_err:
	i2c_set_clientdata(client, NULL);
	return rc;
}

static int nu1619_driver_remove(struct i2c_client *client)
{
	struct oplus_nu1619 *chip = i2c_get_clientdata(client);

	if (!gpio_is_valid(chip->dcdc_en_gpio))
		gpio_free(chip->dcdc_en_gpio);
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

static const struct of_device_id nu1619_match[] = {
	{.compatible = "oplus,nu1619-rx"},
	{},
};

static const struct i2c_device_id nu1619_id_table[] = {
	{"oplus,nu1619-rx", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, nu1619_id_table);

static struct i2c_driver nu1619_driver = {
	.driver = {
		.name = "nu1619-rx",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nu1619_match),
	},
	.probe = nu1619_driver_probe,
	.remove = nu1619_driver_remove,
	.shutdown = nu1619_shutdown,
	.id_table = nu1619_id_table,
};

static __init int nu1619_driver_init(void)
{
	return i2c_add_driver(&nu1619_driver);
}

static __exit void nu1619_driver_exit(void)
{
	i2c_del_driver(&nu1619_driver);
}

oplus_chg_module_register(nu1619_driver);
