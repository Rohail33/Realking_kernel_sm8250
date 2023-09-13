// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[8547a]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>

#include <trace/events/sched.h>
#include <linux/ktime.h>
#include <uapi/linux/sched/types.h>

#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"

#include "../charger_ic/oplus_switching.h"
#include "../oplus_ufcs.h"
#include "../oplus_chg_module.h"
#include "../voocphy/oplus_sc8547.h"
#include "oplus_sc8547a.h"
#include "../voocphy/oplus_voocphy.h"

static struct oplus_sc8574a_ufcs *sc8574a_ufcs = NULL;
static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;

static bool error_reported = false;
extern void oplus_chg_sc8547_error(int report_flag, int *buf, int len);

#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200
#define I2C_ERR_NUM 10
#define MAIN_I2C_ERROR (1 << 0)

static int sc8547_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data);
static int sc8547_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip,
					    int err_type, u8 reg);
static int sc8547_track_upload_cp_err_info(struct oplus_voocphy_manager *chip,
					   int err_type);

static void sc8547_i2c_error(bool happen)
{
	int report_flag = 0;
	if (!oplus_voocphy_mg || error_reported)
		return;

	if (happen) {
		oplus_voocphy_mg->voocphy_iic_err = true;
		oplus_voocphy_mg->voocphy_iic_err_num++;
		if (oplus_voocphy_mg->voocphy_iic_err_num >= I2C_ERR_NUM) {
			report_flag |= MAIN_I2C_ERROR;
			oplus_chg_sc8547_error(report_flag, NULL, 0);
			error_reported = true;
		}
	} else {
		oplus_voocphy_mg->voocphy_iic_err_num = 0;
		oplus_chg_sc8547_error(0, NULL, 0);
	}
}

/************************************************************************/
static int __sc8547_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	sc8547_i2c_error(false);
	*data = (u8)ret;

	return 0;
}

static int __sc8547_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		return ret;
	}
	sc8547_i2c_error(false);
	return 0;
}

static int sc8547_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8547_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8547_update_bits(struct i2c_client *client, u8 reg, u8 mask,
			      u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8547_read_byte(client, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8547_write_byte(client, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sc8547_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	sc8547_i2c_error(false);
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sc8547_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		sc8547_track_upload_i2c_err_info(oplus_voocphy_mg, ret, reg);
		mutex_unlock(&i2c_rw_lock);
		return ret;
	}
	sc8547_i2c_error(false);
	mutex_unlock(&i2c_rw_lock);
	return 0;
}

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int sc8547_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int sc8547_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip,
					    int err_type, u8 reg)
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
	curr_time = sc8547_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
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
	chip->i2c_err_load_trigger =
		kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->i2c_err_load_trigger) {
		pr_err("i2c_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->i2c_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->i2c_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->i2c_err_uploading = true;
	upload_count++;
	pre_upload_time = sc8547_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sc8547a");
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_I2C_ERR);

	oplus_chg_track_get_i2c_err_reason(err_type, chip->err_reason,
					   sizeof(chip->err_reason));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info,
					  sizeof(chip->chg_power_info));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$access_reg@@0x%02x", reg);
	schedule_delayed_work(&chip->i2c_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void sc8547_track_i2c_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip = container_of(
		dwork, struct oplus_voocphy_manager, i2c_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	if (chip->i2c_err_load_trigger) {
		kfree(chip->i2c_err_load_trigger);
		chip->i2c_err_load_trigger = NULL;
	}
	chip->i2c_err_uploading = false;
}

static int sc8547_dump_reg_info(struct oplus_voocphy_manager *chip,
				char *dump_info, int len)
{
	int ret;
	u8 data[2] = { 0, 0 };
	int index = 0;

	if (!chip || !dump_info)
		return 0;

	ret = sc8547_read_byte(chip->client, SC8547_REG_06, &data[0]);
	if (ret < 0) {
		pr_err(" read SC8547_REG_06 failed\n");
		return -EINVAL;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_0F, &data[1]);
	if (ret < 0) {
		pr_err(" read SC8547_REG_0F failed\n");
		return -EINVAL;
	}

	index += snprintf(&(dump_info[index]), len - index,
			  "0x%02x=0x%02x,0x%02x=0x%02x", SC8547_REG_06, data[0],
			  SC8547_REG_0F, data[1]);

	return 0;
}

static int sc8547_track_upload_cp_err_info(struct oplus_voocphy_manager *chip,
					   int err_type)
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
	memset(chip->dump_info, 0, sizeof(chip->dump_info));
	curr_time = sc8547_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (err_type == TRACK_CP_ERR_DEFAULT) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	mutex_lock(&chip->track_cp_err_lock);
	if (chip->cp_err_uploading) {
		pr_info("cp_err_uploading, should return\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->cp_err_load_trigger)
		kfree(chip->cp_err_load_trigger);
	chip->cp_err_load_trigger =
		kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->cp_err_load_trigger) {
		pr_err("cp_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->cp_err_load_trigger->type_reason =
		TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->cp_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->cp_err_uploading = true;
	upload_count++;
	pre_upload_time = sc8547_track_get_local_time_s();
	mutex_unlock(&chip->track_cp_err_lock);

	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$device_id@@%s", "sc8547a");
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_CP_ERR);

	oplus_chg_track_get_cp_err_reason(err_type, chip->err_reason,
					  sizeof(chip->err_reason));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info,
					  sizeof(chip->chg_power_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s",
			  chip->chg_power_info);
	sc8547_dump_reg_info(chip, chip->dump_info, sizeof(chip->dump_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
			  OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$reg_info@@%s", chip->dump_info);
	schedule_delayed_work(&chip->cp_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void sc8547_track_cp_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip = container_of(
		dwork, struct oplus_voocphy_manager, cp_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->cp_err_load_trigger));
	if (chip->cp_err_load_trigger) {
		kfree(chip->cp_err_load_trigger);
		chip->cp_err_load_trigger = NULL;
	}
	chip->cp_err_uploading = false;
}

static int sc8547_track_debugfs_init(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sc8547;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_sc8547 = debugfs_create_dir("sc8547", debugfs_root);
	if (!debugfs_sc8547) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_cp_err = TRACK_WLS_TRX_ERR_DEFAULT;
	debugfs_create_u32("debug_force_i2c_err", 0644, debugfs_sc8547,
			   &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_cp_err", 0644, debugfs_sc8547,
			   &(chip->debug_force_cp_err));

	return ret;
}

static int sc8547_track_init(struct oplus_voocphy_manager *chip)
{
	int rc;

	if (!chip)
		return -EINVAL;

	mutex_init(&chip->track_i2c_err_lock);
	mutex_init(&chip->track_cp_err_lock);
	mutex_init(&chip->track_upload_lock);
	chip->i2c_err_uploading = false;
	chip->i2c_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
	chip->cp_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work,
			  sc8547_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cp_err_load_trigger_work,
			  sc8547_track_cp_err_load_trigger_work);
	rc = sc8547_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("sc8547 debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int sc8547_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = sc8547_write_word(chip->client, SC8547_REG_31, val);
	if (ret < 0) {
		pr_err("failed: write predata\n");
		return -1;
	}
	pr_info("write predata 0x%0x\n", val);
	return ret;
}

static int sc8547_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		pr_err("failed: chip is null\n");
		return -1;
	}

	ret = sc8547_write_word(chip->client, SC8547_REG_2C, val);
	if (ret < 0) {
		pr_err("write txbuff\n");
		return -1;
	}

	return ret;
}

static int sc8547_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	s32 data;
	if (!chip) {
		pr_err("chip is null\n");
		return -1;
	}

	data = sc8547_read_word(chip->client, SC8547_REG_2E);

	if (data < 0) {
		pr_err("sc8547_read_word faile\n");
		return -1;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	pr_info("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n", data,
		chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void sc8547_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[4] = { 0 };
	int i = 0;
	u8 data = 0;
	s32 ret = 0;

	/*int_flag*/
	sc8547_read_byte(chip->client, SC8547_REG_0F, &data);
	chip->int_flag = data;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_19, 4,
					    data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read vsys vbat data_block[%d] = %u\n", i,
			data_block[i]);
	}

	chip->cp_vsys = ((data_block[0] << 8) | data_block[1]) * SC8547_VOUT_ADC_LSB;
	chip->cp_vbat = ((data_block[2] << 8) | data_block[3]) * SC8547_VOUT_ADC_LSB;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_13, 4,
					    data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}
	for (i = 0; i < 4; i++) {
		pr_info("read ichg vbus data_block[%d] = %u\n", i,
			data_block[i]);
	}

	chip->cp_ichg =
		((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;
	chip->cp_vbus = (((data_block[2] & SC8547_VBUS_POL_H_MASK) << 8) |
			 data_block[3]) *
			SC8547_VBUS_ADC_LSB;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_17, 2,
					    data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vac error\n");
	} else {
		sc8547_i2c_error(false);
	}
	for (i = 0; i < 2; i++) {
		pr_info("read vac data_block[%d] = %u\n", i, data_block[i]);
	}

	chip->cp_vac = (((data_block[0] & SC8547_VAC_POL_H_MASK) << 8) |
			data_block[1]) *
		       SC8547_VAC_ADC_LSB;

	pr_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d cp_vac = "
		"%d int_flag = %d",
		chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat,
		chip->cp_vac, chip->int_flag);
}

static int sc8547_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	int cp_ichg = 0;
	u8 cp_enable = 0;
	s32 ret = 0;

	sc8547_get_chg_enable(chip, &cp_enable);

	if (cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_13, 2,
					    data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547 read ichg error \n");
	} else {
		sc8547_i2c_error(false);
	}

	cp_ichg = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;

	return cp_ichg;
}

static int sc8547_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/*parse data_block for improving time of interrupt*/
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_1B, 2,
					    data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547 read vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	chip->cp_vbat = ((data_block[0] << 8) | data_block[1]) * SC8547_VBAT_ADC_LSB;

	return chip->cp_vbat;
}

static int sc8547_get_cp_vbus(struct oplus_voocphy_manager *chip)
{
	u8 data_block[2] = { 0 };
	s32 ret = 0;

	/* parse data_block for improving time of interrupt */
	ret = i2c_smbus_read_i2c_block_data(chip->client, SC8547_REG_15, 2,
					    data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547 read vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	chip->cp_vbus = (((data_block[0] & SC8547_VBUS_POL_H_MASK) << 8) |
			 data_block[1]) *
			SC8547_VBUS_ADC_LSB;

	return chip->cp_vbus;
}

/*********************************************************************/
static int sc8547_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;
	if (enable)
		val = SC8547_RESET_REG;
	else
		val = SC8547_NO_REG_RESET;

	val <<= SC8547_REG_RESET_SHIFT;

	ret = sc8547_update_bits(chip->client, SC8547_REG_07,
				 SC8547_REG_RESET_MASK, val);

	return ret;
}

static bool sc8547a_hw_version_check(struct oplus_voocphy_manager *chip)
{
	int ret;
	u8 val;
	ret = sc8547_read_byte(chip->client, SC8547_REG_36, &val);
	if (val == SC8547A_DEVICE_ID)
		return true;
	else
		return false;
}

static int sc8547_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_07, data);
	if (ret < 0) {
		pr_err("SC8547_REG_07\n");
		return -1;
	}

	*data = *data >> 7;

	return ret;
}

static int sc8547_get_voocphy_enable(struct oplus_voocphy_manager *chip,
				     u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_2B, data);
	if (ret < 0) {
		pr_err("SC8547_REG_2B\n");
		return -1;
	}

	return ret;
}

static void sc8547_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	int i = 0, p = 0;
	if (!chip) {
		pr_err("!!!!! oplus_voocphy_manager chip NULL");
		return;
	}

	for (i = 0; i < 37; i++) {
		p = p + 1;
		sc8547_read_byte(chip->client, i, &chip->reg_dump[p]);
	}
	for (i = 0; i < 9; i++) {
		p = p + 1;
		sc8547_read_byte(chip->client, 43 + i, &chip->reg_dump[p]);
	}
	p = p + 1;
	sc8547_read_byte(chip->client, SC8547_REG_36, &chip->reg_dump[p]);
	p = p + 1;
	sc8547_read_byte(chip->client, SC8547_REG_3A, &chip->reg_dump[p]);
	pr_err("p[%d], ", p);

	return;
}

static int sc8547_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_11, data);
	if (ret < 0) {
		pr_err("SC8547_REG_11\n");
		return -1;
	}

	*data = *data >> 7;

	return ret;
}

static u8 sc8547_match_err_value(struct oplus_voocphy_manager *chip,
				 u8 reg_state, u8 reg_flag)
{
	int err_type = TRACK_CP_ERR_DEFAULT;
	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -EINVAL;
	}

	if (reg_state & SC8547_PIN_DIAG_FALL_FLAG_MASK)
		err_type = TRACK_CP_ERR_CFLY_CDRV_FAULT;
	else if (reg_flag & SC8547_VBAT_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBAT_OVP;
	else if (reg_flag & SC8547_IBAT_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBAT_OCP;
	else if (reg_flag & SC8547_VBUS_OVP_FLAG_MASK)
		err_type = TRACK_CP_ERR_VBUS_OVP;
	else if (reg_flag & SC8547_IBUS_OCP_FLAG_MASK)
		err_type = TRACK_CP_ERR_IBUS_OCP;

	if (chip->debug_force_cp_err)
		err_type = chip->debug_force_cp_err;
	if (err_type != TRACK_CP_ERR_DEFAULT)
		sc8547_track_upload_cp_err_info(chip, err_type);

	return 0;
}

static u8 sc8547_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;
	u8 state = 0;

	if (!chip) {
		pr_err("%s: chip null\n", __func__);
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_0F, &data);
	if (ret < 0) {
		pr_err(" read SC8547_REG_0F failed\n");
		return -1;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_06, &state);
	if (ret < 0) {
		pr_err(" read SC8547_REG_06 failed\n");
		return -1;
	}
	ufcs_err("REG06 = 0x%x REG0F = 0x%x\n", state, data);

	sc8547_match_err_value(chip, state, data);

	return data;
}

static int sc8547_set_chg_enable(struct oplus_voocphy_manager *chip,
				 bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}
	if (enable)
		return sc8547_write_byte(chip->client, SC8547_REG_07, 0x80);
	else
		return sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
}

static void sc8547_set_pd_svooc_config(struct oplus_voocphy_manager *chip,
				       bool enable)
{
	int ret = 0;
	u8 reg_data = 0;
	if (!chip) {
		pr_err("Failed\n");
		return;
	}

	if (enable) {
		reg_data = 0x90 | (chip->ocp_reg & 0xf);
		sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
		sc8547_write_byte(chip->client, SC8547_REG_09, 0x13);
	} else {
		reg_data = 0x10 | (chip->ocp_reg & 0xf);
		sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_05, &reg_data);
	if (ret < 0) {
		pr_err("SC8547_REG_05\n");
		return;
	}
	pr_err("pd_svooc config SC8547_REG_05 = %d\n", reg_data);
}

static bool sc8547_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 data = 0;

	if (!chip) {
		pr_err("Failed\n");
		return false;
	}

	ret = sc8547_read_byte(chip->client, SC8547_REG_05, &data);
	if (ret < 0) {
		pr_err("SC8547_REG_05\n");
		return false;
	}

	pr_err("SC8547_REG_05 = 0x%0x\n", data);

	data = data >> 7;
	if (data == 1)
		return true;
	else
		return false;
}

static int sc8547_set_adc_enable(struct oplus_voocphy_manager *chip,
				 bool enable)
{
	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	if (enable)
		return sc8547_write_byte(chip->client, SC8547_REG_11, 0x80);
	else
		return sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);
}

static void sc8547_send_handshake(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x81);
}

static int sc8547_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;

	/* turn off mos */
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
	/* hwic config with plugout */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	sc8547_write_byte(chip->client, SC8547_REG_00, reg_data);
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x07);
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50);
	reg_data = 0x10 | (chip->ocp_reg & 0xf);

	sc8547_write_byte(chip->client, SC8547_REG_05, reg_data);
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00);

	/* clear tx data */
	sc8547_write_byte(chip->client, SC8547_REG_2C, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_2D, 0x00);

	/* disable vooc phy irq */
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x7f);

	/* set D+ HiZ */
	sc8547_write_byte(chip->client, SC8547_REG_21, 0xc0);

	/* select big bang mode */

	/* disable vooc */
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00);

	/* set predata */
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);

	/* mask insert irq */
	sc8547_write_word(chip->client, SC8547_REG_10, 0x02);
	sc8547_write_byte(chip->client, SC8547A_ADDR_OTG_EN, 0x04);
	pr_err("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}

static int sc8547_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	u8 value;

	/*set predata to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us*/
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);
	sc8547_read_byte(chip->client, SC8547_REG_3A, &value);
	value = value | (3 << 5);
	sc8547_write_byte(chip->client, SC8547_REG_3A, value);

	/*dpdm*/
	sc8547_write_byte(chip->client, SC8547_REG_21, 0x21);
	sc8547_write_byte(chip->client, SC8547_REG_22, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xD1);

	/*clear tx data*/
	sc8547_write_byte(chip->client, SC8547_REG_2C, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_2D, 0x00);

	/*vooc*/
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x05);
	sc8547_send_handshake(chip);

	pr_info("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static irqreturn_t sc8547a_charger_interrupt(int irq, void *dev_id)
{
	struct oplus_voocphy_manager *chip = dev_id;

	if (!chip || !sc8574a_ufcs) {
		return IRQ_HANDLED;
	}
	if (atomic_read(&sc8574a_ufcs->suspended) == 1) {
		ufcs_err("sc8574a is suspend!\n");
		return IRQ_HANDLED;
	}

	if (sc8574a_ufcs->ufcs_enable) {
		kthread_queue_work(sc8574a_ufcs->wq, &sc8574a_ufcs->rcv_work);
		return IRQ_HANDLED;
	} else {
		return oplus_voocphy_interrupt_handler(chip);
	}
}

static int sc8547_init_device(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;
	sc8547_write_byte(chip->client, SC8547_REG_11,
			  0x0); /* ADC_CTRL:disable */
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x7);
	sc8547_write_byte(chip->client, SC8547_REG_04,
			  0x50); /* VBUS_OVP:10 2:1 or 1:1V */
	reg_data = 0x20 | (chip->ovp_reg & 0x1f);
	sc8547_write_byte(chip->client, SC8547_REG_00,
			  reg_data); /* VBAT_OVP:4.65V */
	reg_data = 0x10 | (chip->ocp_reg & 0xf);
	sc8547_write_byte(chip->client, SC8547_REG_05,
			  reg_data); /* IBUS_OCP_UCP:3.6A */
	sc8547_write_byte(chip->client, SC8547_REG_01, 0xbf);
	sc8547_write_byte(chip->client, SC8547_REG_2B,
			  0x00); /* OOC_CTRL:disable */
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	sc8547_update_bits(chip->client, SC8547_REG_09,
			   SC8547_IBUS_UCP_RISE_MASK_MASK,
			   (1 << SC8547_IBUS_UCP_RISE_MASK_SHIFT));
	sc8547_write_byte(chip->client, SC8547_REG_10,
			  0x02); /* mask insert irq */
	sc8547_write_byte(chip->client, SC8547A_ADDR_OTG_EN,
			  0x04); /* OTG EN */
	pr_err("sc8547_init_device done");

	return 0;
}

static int sc8547_init_vooc(struct oplus_voocphy_manager *chip)
{
	u8 value;
	pr_err(" >>>>start init vooc\n");

	sc8547_reg_reset(chip, true);
	sc8547_init_device(chip);

	/*to avoid cmd of adjust current(0x01)return error, add voocphy bit0 hold time to 800us */
	/*SET PREDATA */
	sc8547_write_word(chip->client, SC8547_REG_31, 0x0);
	/*sc8547_set_predata(0x0);*/
	sc8547_read_byte(chip->client, SC8547_REG_3A, &value);
	value = value | (1 << 6);
	sc8547_write_byte(chip->client, SC8547_REG_3A, value);
	pr_err("read value %d\n", value);

	/*dpdm */
	sc8547_write_byte(chip->client, SC8547_REG_21, 0x21);
	sc8547_write_byte(chip->client, SC8547_REG_22, 0x00);
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xD1);

	/*vooc */
	sc8547_write_byte(chip->client, SC8547_REG_30, 0x05);

	return 0;
}

static int sc8547_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->irq_gpio = of_get_named_gpio(node, "qcom,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		pr_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			rc = gpio_request(chip->irq_gpio, "irq_gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n",
				       chip->irq_gpio);
			}
		}
		pr_err("chip->irq_gpio =%d\n", chip->irq_gpio);
	}
	chip->irq = gpio_to_irq(chip->irq_gpio);
	pr_err("irq chip->irq = %d\n", chip->irq);

	/* set voocphy pinctrl*/
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->charging_inter_active =
		pinctrl_lookup_state(chip->pinctrl, "charging_inter_active");
	if (IS_ERR_OR_NULL(chip->charging_inter_active)) {
		chg_err(": %d Failed to get the state pinctrl handle\n",
			__LINE__);
		return -EINVAL;
	}

	chip->charging_inter_sleep =
		pinctrl_lookup_state(chip->pinctrl, "charging_inter_sleep");
	if (IS_ERR_OR_NULL(chip->charging_inter_sleep)) {
		chg_err(": %d Failed to get the state pinctrl handle\n",
			__LINE__);
		return -EINVAL;
	}

	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl,
			     chip->charging_inter_active); /* no_PULL */

	rc = gpio_get_value(chip->irq_gpio);
	pr_err("irq chip->irq_gpio input =%d irq_gpio_stat = %d\n",
	       chip->irq_gpio, rc);

	return 0;
}

static int sc8547_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret = 0;

	sc8547_irq_gpio_init(chip);
	pr_err(" sc8547 chip->irq = %d\n", chip->irq);
	if (chip->irq) {
		ret = request_threaded_irq(chip->irq, NULL,
					   sc8547a_charger_interrupt,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   "sc8547_charger_irq", chip);
		if (ret < 0) {
			pr_debug("request irq for irq=%d failed, ret =%d\n",
				 chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
	}
	pr_debug("request irq ok\n");

	return ret;
}

static int sc8547_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01); /*VAC_OVP:12v*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP:10v*/
	reg_data = 0x10 | (chip->ocp_reg & 0xf);
	sc8547_write_byte(chip->client, SC8547_REG_05,
			  reg_data); /*IBUS_OCP_UCP:3.6A*/
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x13); /*WD:1000ms*/
	sc8547_write_byte(chip->client, SC8547_REG_11,
			  0x80); /*ADC_CTRL:ADC_EN*/
	sc8547_write_byte(chip->client, SC8547_REG_0D, 0x70);
	/*sc8547_write_byte(chip->client, SC8547_REG_2B, 0x81);*/ /*VOOC_CTRL,send handshake*/

	sc8547_write_byte(chip->client, SC8547_REG_33, 0xd1); /*Loose_det=1*/
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	return 0;
}

static int sc8547_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x07); /*VAC_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_05, 0x1c); /*IBUS_OCP_UCP:*/
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x93); /*WD:1000ms*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x80); /*ADC_CTRL:*/
	sc8547_write_byte(chip->client, SC8547_REG_33, 0xd1); /*Loose_det*/
	sc8547_write_byte(chip->client, SC8547_REG_3A, 0x60);
	return 0;
}

static int sc8547_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x07); /*VAC_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x10); /*WD:DISABLE*/

	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00); /*ADC_CTRL:*/
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00); /*VOOC_CTRL*/
	return 0;
}

static int sc8547_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8547_write_byte(chip->client, SC8547_REG_00, 0x2E); /*VAC_OV:*/
	sc8547_write_byte(chip->client, SC8547_REG_02, 0x01); /*VAC_OVP:*/
	sc8547_write_byte(chip->client, SC8547_REG_04, 0x50); /*VBUS_OVP*/
	sc8547_write_byte(chip->client, SC8547_REG_07, 0x0);
	sc8547_write_byte(chip->client, SC8547_REG_09, 0x10); /*WD:DISABLE*/
	sc8547_write_byte(chip->client, SC8547_REG_11, 0x00); /*ADC_CTRL*/
	sc8547_write_byte(chip->client, SC8547_REG_2B, 0x00); /*VOOC_CTRL*/
	pr_err("sc8547_pdqc_hw_setting done");
	return 0;
}

static int sc8547_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		pr_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		sc8547_init_device(chip);
		pr_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		sc8547_svooc_hw_setting(chip);
		pr_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		sc8547_vooc_hw_setting(chip);
		pr_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		sc8547_5v2a_hw_setting(chip);
		pr_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		sc8547_pdqc_hw_setting(chip);
		pr_info("SETTING_REASON_PDQC\n");
		break;
	default:
		pr_err("do nothing\n");
		break;
	}
	return 0;
}

static ssize_t sc8547_show_registers(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8547");
	for (addr = 0x0; addr <= 0x3C; addr++) {
		if ((addr < 0x24) || (addr > 0x2B && addr < 0x33) ||
		    addr == 0x36 || addr == 0x3C) {
			ret = sc8547_read_byte(chip->client, addr, &val);
			if (ret == 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx,
				               "Reg[%.2X] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t sc8547_store_register(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x3C)
		sc8547_write_byte(chip->client, (unsigned char)reg,
				  (unsigned char)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sc8547_show_registers,
		   sc8547_store_register);

static void sc8547_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static int sc8547_gpio_init(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n",
		       __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get chargerid_switch_gpio pinctrl fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_high =
		pinctrl_lookup_state(chip->pinctrl, "switch1_act_switch2_act");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_high)) {
		chg_err("get switch1_act_switch2_act fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_low = pinctrl_lookup_state(
		chip->pinctrl, "switch1_sleep_switch2_sleep");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_low)) {
		chg_err("get switch1_sleep_switch2_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);

	chip->slave_charging_inter_default = pinctrl_lookup_state(
		chip->pinctrl, "slave_charging_inter_default");
	if (IS_ERR_OR_NULL(chip->slave_charging_inter_default)) {
		chg_err("get slave_charging_inter_default fail\n");
	} else {
		pinctrl_select_state(chip->pinctrl,
				     chip->slave_charging_inter_default);
	}

	printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip is ready!\n", __func__);
	return 0;
}

static int sc8547_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = NULL;

	if (!chip) {
		pr_debug("chip null\n");
		return -1;
	}

	/* Parsing gpio switch gpio47*/
	node = chip->dev->of_node;
	chip->switch1_gpio =
		of_get_named_gpio(node, "qcom,charging_switch1-gpio", 0);
	if (chip->switch1_gpio < 0) {
		pr_debug("chip->switch1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->switch1_gpio)) {
			rc = gpio_request(chip->switch1_gpio,
			                  "charging-switch1-gpio");
			if (rc) {
				pr_debug("unable to request gpio [%d]\n",
					 chip->switch1_gpio);
			} else {
				rc = sc8547_gpio_init(chip);
				if (rc)
					chg_err("unable to init "
						"charging_sw_ctrl2-gpio:%d\n",
						chip->switch1_gpio);
			}
		}
		pr_debug("chip->switch1_gpio =%d\n", chip->switch1_gpio);
	}

	rc = of_property_read_u32(node, "ovp_reg", &chip->ovp_reg);
	if (rc) {
		chip->ovp_reg = 0xE;
	} else {
		chg_err("ovp_reg is %d\n", chip->ovp_reg);
	}

	rc = of_property_read_u32(node, "ocp_reg", &chip->ocp_reg);
	if (rc) {
		chip->ocp_reg = 0x8;
	} else {
		chg_err("ocp_reg is %d\n", chip->ocp_reg);
	}

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_low",
				  &chip->voocphy_vbus_low);
	if (rc) {
		chip->voocphy_vbus_low = DEFUALT_VBUS_LOW;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_low);

	rc = of_property_read_u32(node, "qcom,voocphy_vbus_high",
				  &chip->voocphy_vbus_high);
	if (rc) {
		chip->voocphy_vbus_high = DEFUALT_VBUS_HIGH;
	}
	chg_err("voocphy_vbus_high is %d\n", chip->voocphy_vbus_high);

	return 0;
}

static void sc8547_set_switch_fast_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sc8547_set_switch_fast_charger chip null\n");
		return;
	}

	if (chip->switch1_gpio < 0) {
		chg_err("miss sc8547_set_switch_normal_charger gpio\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_high)) {
		chg_err("%s pinctrl or active or sleep null!\n", __func__);
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_high);
	gpio_direction_output(chip->switch1_gpio, 1); /* out 1*/

	pr_err("switch switch2 %d to fast finshed\n",
	       gpio_get_value(chip->switch1_gpio));

	return;
}

static void sc8547_set_switch_normal_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		pr_err("sc8547_set_switch_normal_charger chip null\n");
		return;
	}

	if (chip->switch1_gpio < 0) {
		chg_err("miss sc8547_set_switch_normal_charger gpio\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_low)) {
		chg_err("%s pinctrl or active or sleep null!\n", __func__);
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);
	if (chip->switch1_gpio > 0) {
		gpio_direction_output(chip->switch1_gpio, 0); /* in 0*/
	}

	pr_err("switch switch2 %d to normal finshed\n",
	       gpio_get_value(chip->switch1_gpio));

	return;
}

static void sc8547_set_switch_mode(struct oplus_voocphy_manager *chip, int mode)
{
	switch (mode) {
	case VOOC_CHARGER_MODE:
		sc8547_set_switch_fast_charger(chip);
		break;
	case NORMAL_CHARGER_MODE:
	default:
		sc8547_set_switch_normal_charger(chip);
		break;
	}

	return;
}

static struct oplus_voocphy_operations oplus_sc8547_ops = {
	.hw_setting = sc8547_hw_setting,
	.init_vooc = sc8547_init_vooc,
	.set_predata = sc8547_set_predata,
	.set_txbuff = sc8547_set_txbuff,
	.get_adapter_info = sc8547_get_adapter_info,
	.update_data = sc8547_update_data,
	.get_chg_enable = sc8547_get_chg_enable,
	.set_chg_enable = sc8547_set_chg_enable,
	.reset_voocphy = sc8547_reset_voocphy,
	.reactive_voocphy = sc8547_reactive_voocphy,
	.set_switch_mode = sc8547_set_switch_mode,
	.send_handshake = sc8547_send_handshake,
	.get_cp_vbat = sc8547_get_cp_vbat,
	.get_cp_vbus = sc8547_get_cp_vbus,
	.get_int_value = sc8547_get_int_value,
	.get_adc_enable = sc8547_get_adc_enable,
	.set_adc_enable = sc8547_set_adc_enable,
	.get_ichg = sc8547_get_cp_ichg,
	.set_pd_svooc_config = sc8547_set_pd_svooc_config,
	.get_pd_svooc_config = sc8547_get_pd_svooc_config,
	.get_voocphy_enable = sc8547_get_voocphy_enable,
	.dump_voocphy_reg = sc8547_dump_reg_in_err_issue,
};

static int sc8547_charger_choose(struct oplus_voocphy_manager *chip)
{
	int ret;

	if (!oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip already exists!");
		return 0;
	} else {
		ret = i2c_smbus_read_byte_data(chip->client, 0x07);
		pr_err("0x07 = %d\n", ret);
		if (ret < 0) {
			pr_err("i2c communication fail");
			return -EPROBE_DEFER;
		} else
			return 1;
	}
}

#ifndef I2C_ERR_MAX
#define I2C_ERR_MAX 2
#endif

typedef int (*callback)(void *buf);
static int sc8574a_send_soft_reset(void);

static int sc8574a_source_hard_reset(void);

static inline void sc8574a_i2c_err_inc(void)
{
	if (atomic_inc_return(&sc8574a_ufcs->i2c_err_count) > I2C_ERR_MAX) {
		atomic_set(&sc8574a_ufcs->i2c_err_count, 0);
		sc8574a_ufcs->error.i2c_error = 1;
	}
}

static inline void sc8574a_i2c_err_clr(void)
{
	atomic_set(&sc8574a_ufcs->i2c_err_count, 0);
	sc8574a_ufcs->error.i2c_error = 0;
}

static inline void sc8574a_id_inc(void)
{
	return;
	sc8574a_ufcs->msg_send_id = sc8574a_ufcs->msg_send_id < 15 ?
					    (sc8574a_ufcs->msg_send_id + 1) :
					    0;
}

static inline void sc8574a_reset_state_machine(void)
{
	atomic_set(&sc8574a_ufcs->i2c_err_count, 0);

	if (mutex_is_locked(&sc8574a_ufcs->chip_lock))
		mutex_unlock(&sc8574a_ufcs->chip_lock);

	sc8574a_ufcs->state = STATE_NULL;
	memset(&sc8574a_ufcs->dev_msg, 0, sizeof(struct comm_msg));
	memset(sc8574a_ufcs->dev_buffer, 0, sizeof(sc8574a_ufcs->dev_buffer));
	memset(&sc8574a_ufcs->rcv_msg, 0, sizeof(struct comm_msg));
	memset(sc8574a_ufcs->rcv_buffer, 0, sizeof(sc8574a_ufcs->rcv_buffer));

	memset(&sc8574a_ufcs->src_cap, 0, sizeof(struct oplus_ufcs_src_cap));
	memset(&sc8574a_ufcs->info, 0, sizeof(struct oplus_ufcs_sink_info));
	memset(&sc8574a_ufcs->flag, 0, sizeof(struct sc8574a_error_flag));
	memset(&sc8574a_ufcs->error, 0, sizeof(struct oplus_ufcs_error));
	sc8574a_ufcs->msg_send_id = 0;
	sc8574a_ufcs->msg_recv_id = 0;

	sc8574a_ufcs->get_flag_failed = 0;
	sc8574a_ufcs->handshake_success = 0;
	sc8574a_ufcs->ack_received = 0;
	sc8574a_ufcs->msg_received = 0;
	sc8574a_ufcs->soft_reset = 0;
	sc8574a_ufcs->uct_mode = 0;
	sc8574a_ufcs->oplus_id = false;
	sc8574a_ufcs->abnormal_id = false;
}

static inline void sc8574a_clr_rx_buffer(void)
{
	memset(&sc8574a_ufcs->rcv_msg, 0, sizeof(struct comm_msg));
	memset(sc8574a_ufcs->rcv_buffer, 0, sizeof(sc8574a_ufcs->rcv_buffer));
}

static inline void sc8574a_clr_tx_buffer(void)
{
	memset(&sc8574a_ufcs->dev_msg, 0, sizeof(struct comm_msg));
	memset(sc8574a_ufcs->dev_buffer, 0, sizeof(sc8574a_ufcs->dev_buffer));
}

static inline void sc8574a_clr_flag_reg(void)
{
	memset(&sc8574a_ufcs->flag, 0, sizeof(struct sc8574a_error_flag));
}

static inline void sc8574a_set_state(enum UFCS_MACHINE_STATE state)
{
	sc8574a_ufcs->state = state;
}

static inline void sc8574a_call_end(void)
{
	sc8574a_ufcs->state = STATE_IDLE;
	sc8574a_ufcs->ack_received = 0;
	sc8574a_ufcs->msg_received = 0;
	sc8574a_ufcs->soft_reset = 0;
}

static inline void sc8574a_get_chip_lock(void)
{
	mutex_lock(&sc8574a_ufcs->chip_lock);
}

static inline void sc8574a_release_chip_lock(void)
{
	mutex_unlock(&sc8574a_ufcs->chip_lock);
}

static inline void sc8574a_reset_msg_counter(void)
{
	sc8574a_ufcs->msg_send_id = 0;
	sc8574a_ufcs->msg_recv_id = 0;
	sc8574a_ufcs->soft_reset = 0;
}

static bool sc8574a_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static void sc8574a_awake_init(struct oplus_sc8574a_ufcs *chip)
{
	if (!chip)
		return;
	chip->chip_ws = wakeup_source_register(chip->dev, "sc8574a_ws");
}

static void sc8574a_set_awake(struct oplus_sc8574a_ufcs *chip, bool awake)
{
	static bool pm_flag = false;

	if (!chip || !chip->chip_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->chip_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->chip_ws);
		pm_flag = false;
	}
}

static int sc8574a_read_byte(struct oplus_sc8574a_ufcs *chip, u8 addr, u8 *data)
{
	u8 addr_buf = addr & 0xff;
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, &addr_buf, 1);
	if (rc < 1) {
		ufcs_err("write 0x%04x error, rc = %d \n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg,
							 -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	rc = i2c_master_recv(chip->client, data, 1);
	if (rc < 1) {
		pr_err("read 0x%04x error, rc = %d \n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg,
							 -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}
	mutex_unlock(&i2c_rw_lock);
	sc8574a_i2c_err_clr();
	return 0;
error:
	mutex_unlock(&i2c_rw_lock);
	sc8574a_i2c_err_inc();
	return rc;
}

static int sc8574a_read_data(struct oplus_sc8574a_ufcs *chip, u8 addr, u8 *buf,
			     int len)
{
	u8 addr_buf = addr & 0xff;
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, &addr_buf, 1);
	if (rc < 1) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg,
							 -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	rc = i2c_master_recv(chip->client, buf, len);
	if (rc < len) {
		pr_err("read 0x%04x error, rc=%d\n", addr, rc);
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg,
							 -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}
	mutex_unlock(&i2c_rw_lock);
	sc8574a_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&i2c_rw_lock);
	sc8574a_i2c_err_inc();
	return rc;
}

static int sc8574a_write_byte(struct oplus_sc8574a_ufcs *chip, u8 addr, u8 data)
{
	u8 buf[2] = { addr & 0xff, data };
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, buf, 2);
	if (rc < 2) {
		ufcs_err("write 0x%04x error, rc = %d \n", addr, rc);
		mutex_unlock(&i2c_rw_lock);
		sc8574a_i2c_err_inc();
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg,
							 -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&i2c_rw_lock);
	sc8574a_i2c_err_clr();
	return 0;
}

static int sc8574a_write_data(struct oplus_sc8574a_ufcs *chip, u8 addr,
			      u16 length, u8 *data)
{
	u8 *buf;
	int rc = 0;

	buf = kzalloc(length + 1, GFP_KERNEL);
	if (!buf) {
		ufcs_err("alloc memorry for i2c buffer error\n");
		return -ENOMEM;
	}

	buf[0] = addr & 0xff;
	memcpy(&buf[1], data, length);

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, buf, length + 1);
	if (rc < length + 1) {
		ufcs_err("write 0x%04x error, ret = %d \n", addr, rc);
		mutex_unlock(&i2c_rw_lock);
		kfree(buf);
		sc8574a_i2c_err_inc();
		if (oplus_voocphy_mg)
			sc8547_track_upload_i2c_err_info(oplus_voocphy_mg,
							 -ENOTCONN, addr);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&i2c_rw_lock);
	kfree(buf);
	sc8574a_i2c_err_clr();
	return 0;
}

static int sc8574a_write_bit_mask(struct oplus_sc8574a_ufcs *chip, u8 reg,
				  u8 mask, u8 data)
{
	u8 temp = 0;
	int rc = 0;

	rc = sc8574a_read_byte(chip, reg, &temp);
	if (rc < 0)
		return rc;

	temp = (data & mask) | (temp & (~mask));

	rc = sc8574a_write_byte(chip, reg, temp);
	if (rc < 0)
		return rc;

	return 0;
}

static struct comm_msg *
sc8574a_construct_ctrl_msg(enum UFCS_CTRL_MSG_TYPE ctrl_msg)
{
	u8 addr = UFCS_ADDR_SRC;

	if (ctrl_msg == UFCS_CTRL_GET_CABLE_INFO)
		addr = UFCS_ADDR_CABLE;

	memset(&sc8574a_ufcs->dev_msg, 0, sizeof(struct comm_msg));
	sc8574a_ufcs->dev_msg.header =
		UFCS_HEADER(addr, sc8574a_ufcs->msg_send_id, HEAD_TYPE_CTRL);
	sc8574a_ufcs->dev_msg.command = ctrl_msg;
	sc8574a_ufcs->dev_msg.len = DATA_LENGTH_BLANK;
	msleep(10);
	return &sc8574a_ufcs->dev_msg;
}

static struct comm_msg *
sc8574a_construct_data_msg(enum UFCS_DATA_MSG_TYPE data_msg, void *buffer)
{
	u8 addr = UFCS_ADDR_SRC;
	u16 buf_16 = 0;
	u32 buf_32 = 0;
	u64 buf_64 = 0;

	sc8574a_clr_tx_buffer();
	sc8574a_ufcs->dev_msg.header =
		UFCS_HEADER(addr, sc8574a_ufcs->msg_send_id, HEAD_TYPE_DATA);

	switch (data_msg) {
	case UFCS_DATA_REQUEST:
		sc8574a_ufcs->dev_msg.command = UFCS_DATA_REQUEST;
		sc8574a_ufcs->dev_msg.len =
			DATA_LENGTH_BLANK + REQUEST_PDO_LENGTH;
		buf_64 = *(u64 *)buffer;
		buf_64 = SWAP64(buf_64);
		memcpy(sc8574a_ufcs->dev_buffer, &buf_64,
		       sizeof(struct request_pdo));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	case UFCS_DATA_SINK_INFO:
		sc8574a_ufcs->dev_msg.command = UFCS_DATA_SINK_INFO;
		sc8574a_ufcs->dev_msg.len =
			DATA_LENGTH_BLANK + sizeof(struct sink_info);
		buf_64 = cpu_to_be64p((u64 *)buffer);
		memcpy(sc8574a_ufcs->dev_buffer, &buf_64,
		       sizeof(struct sink_info));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	case UFCS_DATA_VERIFY_REQUEST:
		sc8574a_ufcs->dev_msg.command = UFCS_DATA_VERIFY_REQUEST;
		sc8574a_ufcs->dev_msg.len =
			DATA_LENGTH_BLANK + VERIFY_REQUEST_LENGTH;
		memcpy(sc8574a_ufcs->dev_buffer, buffer,
		       sizeof(struct verify_request));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	case UFCS_DATA_DEVICE_INFO:
		sc8574a_ufcs->dev_msg.command = UFCS_DATA_DEVICE_INFO;
		sc8574a_ufcs->dev_msg.len =
			DATA_LENGTH_BLANK + DEVICE_INFO_LENGTH;
		buf_64 = *(u64 *)buffer;
		buf_64 = SWAP64(buf_64);
		memcpy(sc8574a_ufcs->dev_buffer, &buf_64,
		       sizeof(struct device_info));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	case UFCS_DATA_ERROR_INFO:
		sc8574a_ufcs->dev_msg.command = UFCS_DATA_ERROR_INFO;
		sc8574a_ufcs->dev_msg.len =
			DATA_LENGTH_BLANK + sizeof(struct error_info);
		buf_32 = cpu_to_be32p((u32 *)buffer);
		memcpy(sc8574a_ufcs->dev_buffer, &buf_32,
		       sizeof(struct error_info));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	case UFCS_DATA_CONFIG_WATCHDOG:
		sc8574a_ufcs->dev_msg.command = UFCS_DATA_CONFIG_WATCHDOG;
		sc8574a_ufcs->dev_msg.len =
			DATA_LENGTH_BLANK + CONFIG_WTD_LENGTH;
		buf_16 = *(u16 *)buffer;
		buf_16 = SWAP16(buf_16);
		memcpy(sc8574a_ufcs->dev_buffer, &buf_16,
		       sizeof(struct config_watchdog));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	case UFCS_DATA_REFUSE:
		sc8574a_ufcs->dev_msg.command = UFCS_DATA_REFUSE;
		sc8574a_ufcs->dev_msg.len =
			DATA_LENGTH_BLANK + REFUSE_MSG_LENGTH;
		buf_32 = *(u32 *)buffer;
		buf_32 = SWAP32(buf_32);
		memcpy(sc8574a_ufcs->dev_buffer, &buf_32,
		       sizeof(struct refuse_msg));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	default:
		ufcs_err("data msg can't be handled! msg = %d\n", data_msg);
		break;
	}
	msleep(10);

	return &sc8574a_ufcs->dev_msg;
}

static struct comm_msg *
sc8574a_construct_vnd_msg(enum UFCS_VND_MSG_TYPE vnd_msg, void *buffer)
{
	u8 addr = UFCS_ADDR_SRC;
	u64 buf_64 = 0;

	sc8574a_clr_tx_buffer();
	sc8574a_ufcs->dev_msg.header =
		UFCS_HEADER(addr, sc8574a_ufcs->msg_send_id, HEAD_TYPE_DATA);

	switch (vnd_msg) {
	case UFCS_VND_CHECK_POWER:
		sc8574a_ufcs->dev_msg.vender = OPLUS_DEV_VENDOR;
		sc8574a_ufcs->dev_msg.len =
			VND_LENGTH_BLANK + REQUEST_POWER_LENGTH;
		buf_64 = *(u64 *)buffer;
		buf_64 = SWAP64(buf_64);
		memcpy(sc8574a_ufcs->dev_buffer, &buf_64,
		       sizeof(struct charger_power));
		sc8574a_ufcs->dev_msg.buf = sc8574a_ufcs->dev_buffer;
		break;
	default:
		ufcs_err("data msg can't be handled! msg = %d\n", vnd_msg);
		break;
	}

	return &sc8574a_ufcs->dev_msg;
}

static int sc8574a_write_tx_buffer(struct oplus_sc8574a_ufcs *chip, u8 *buf,
				   u16 len)
{
	sc8574a_write_byte(chip, SC8547A_ADDR_TX_LENGTH, len);
	sc8574a_write_data(chip, SC8547A_ADDR_TX_BUFFER0, len, buf);
	sc8574a_write_bit_mask(chip, SC8547A_ADDR_UFCS_CTRL0,
			       SC8547A_MASK_SND_CMP, SC8547A_CMD_SND_CMP);
	return 0;
}

static int sc8574a_trans_msg(struct oplus_sc8574a_ufcs *chip,
			     struct comm_msg *msg)
{
	u8 *tx_buf;
	u16 k = 0;
	u16 send_len = 0;
	u16 i = 0;

	if (msg->len > MAX_TX_MSG_SIZE) {
		ufcs_err("message length is too long. length = %d \n",
			 msg->len);
		return -ENOMEM;
	}
	ufcs_err(" [0x%x, 0x%x, 0x%x]\n", msg->header, msg->command,
		 chip->msg_send_id);

	tx_buf = kzalloc(MAX_TX_BUFFER_SIZE, GFP_KERNEL);
	if (!tx_buf) {
		ufcs_err("failed to allocate memory for tx_buf\n");
		return -ENOMEM;
	}

	tx_buf[i++] = msg->header >> 8;
	tx_buf[i++] = msg->header & 0xff;
	tx_buf[i++] = msg->command;
	tx_buf[i++] = msg->len;

	for (k = 0; k < msg->len; k++)
		tx_buf[i++] = msg->buf[k];

	if (msg->len)
		send_len = msg->len + WRT_DATA_BGN_POS;
	else
		send_len = 3;

	sc8574a_write_tx_buffer(chip, tx_buf, send_len);
	sc8574a_id_inc();
	kfree(tx_buf);

	return 0;
}

static void sc8574a_retrieve_cp_err(struct oplus_sc8574a_ufcs *chip)
{
	if (!oplus_voocphy_mg ||
	    (chip->state != STATE_IDLE && chip->state != STATE_WAIT_MSG))
		return;
	sc8547_get_int_value(oplus_voocphy_mg);
}

static int sc8574a_rcv_msg(struct oplus_sc8574a_ufcs *chip)
{
	u8 rx_length = 0;
	int i = 0;
	struct comm_msg *rcv = &chip->rcv_msg;
	struct receive_error *rf = &chip->flag.rcv_error;
	struct communication_error *ce = &chip->flag.commu_error;

	/*Decide whether to receive rx buffer*/
	if (ce->bus_conflict) {
		chip->soft_reset = 1;
		ufcs_err("bus conflict! try to softreset!\n");
		goto error;
	}

	if (rf->sent_cmp) {
		if (rf->msg_trans_fail) {
			ufcs_err("sent packet complete = 1 && msg trans fail!\n");
			goto error;
		}
		if (rf->ack_rcv_timeout)
			ufcs_err("sent packet complete = 1 && ack receive timeout!\n");
		if (rf->data_rdy)
			ufcs_err("sent packet complete = 1 && data ready = 1!\n");
	} else {
		if (!rf->data_rdy) {
			ufcs_err("sent packet complete = 0 && data ready = 0!\n");
			goto error;
		}
	}

	if ((chip->state != STATE_IDLE) && (chip->state != STATE_WAIT_MSG)) {
		chip->ack_received = 1;
		if ((rf->sent_cmp) && (rf->data_rdy))
			ufcs_err("ack interrupt read delayed! need to read msg!\n");
		else
			return 0;
	}

	sc8574a_read_byte(chip, SC8547A_ADDR_RX_LENGTH, &rx_length);
	if (rx_length > SC8547A_LEN_MAX) {
		ufcs_err("rx_length = %d, limit to SC8547A_LEN_MAX\n");
		rx_length = SC8547A_LEN_MAX;
	}
	rcv->len = rx_length;

	sc8574a_read_data(chip, SC8547A_ADDR_RX_BUFFER0, chip->rcv_buffer, rcv->len);

	sc8574a_retrieve_cp_err(chip);

	rcv->header = (chip->rcv_buffer[i++]) << 8;
	rcv->header += (chip->rcv_buffer[i++]);
	if ((rcv->header == 0x00) || (rcv->header == 0xFF)) {
		ufcs_err("receive header is wrong! header = 0x%02x", rcv->header);
		return 0;
	}

	chip->msg_recv_id = UFCS_HEADER_ID(rcv->header);

	if (UFCS_HEADER_TYPE(rcv->header) == HEAD_TYPE_CTRL) {
		rcv->command = chip->rcv_buffer[i++];
		rcv->len = 0;
		rcv->buf = NULL;
	} else if (UFCS_HEADER_TYPE(rcv->header) == HEAD_TYPE_DATA) {
		rcv->command = chip->rcv_buffer[i++];
		rcv->len = chip->rcv_buffer[i++];
		rcv->buf = &chip->rcv_buffer[READ_DATA_BGN_POS];
	} else if (UFCS_HEADER_TYPE(rcv->header) == HEAD_TYPE_VENDER) {
		rcv->vender = chip->rcv_buffer[i++] << 8;
		rcv->vender += chip->rcv_buffer[i++];
		rcv->len = chip->rcv_buffer[i++];
		rcv->buf = &chip->rcv_buffer[READ_VENDER_BGN_POS];
	} else {
		ufcs_err("head err[0x%x, 0x%x]\n", rcv->header,  chip->rcv_buffer[i++]);
		goto error;
	}
	ufcs_err("head ok[0x%x, 0x%x, 0x%x]\n", rcv->header, rcv->command, rcv->len);
	return 0;

error:
	if ((chip->state != STATE_IDLE) && (chip->state != STATE_WAIT_MSG))
		chip->ack_received = 0;
	return -EINVAL;
}

static int sc8574a_retrieve_reg_flags(struct oplus_sc8574a_ufcs *chip)
{
	struct sc8574a_error_flag *reg = &chip->flag;
	int rc = 0;
	u8 flag_buf[SC8547A_FLAG_NUM] = { 0 };

	rc = sc8574a_read_data(chip, SC8547A_ADDR_GENERAL_INT_FLAG1, flag_buf,
			       SC8547A_FLAG_NUM);
	if (rc < 0) {
		ufcs_err("failed to read flag register\n");
		chip->get_flag_failed = 1;
		return -EBUSY;
	}

	reg->hd_error.dp_ovp = 0;
	reg->hd_error.dm_ovp = 0;
	reg->hd_error.temp_shutdown = 0;
	reg->hd_error.wtd_timeout = 0;
	reg->rcv_error.ack_rcv_timeout =
		flag_buf[1] & SC8547A_FLAG_ACK_RECEIVE_TIMEOUT;
	reg->hd_error.hardreset = flag_buf[1] & SC8547A_FLAG_HARD_RESET;
	reg->rcv_error.data_rdy = flag_buf[1] & SC8547A_FLAG_DATA_READY;
	reg->rcv_error.sent_cmp =
		flag_buf[1] & SC8547A_FLAG_SENT_PACKET_COMPLETE;
	reg->commu_error.crc_error = flag_buf[1] & SC8547A_FLAG_CRC_ERROR;
	reg->commu_error.baud_error =
		flag_buf[1] & SC8547A_FLAG_BAUD_RATE_ERROR;

	reg->commu_error.training_error =
		flag_buf[2] & SC8547A_FLAG_TRAINING_BYTE_ERROR;
	reg->rcv_error.msg_trans_fail =
		flag_buf[2] & SC8547A_FLAG_MSG_TRANS_FAIL;
	reg->commu_error.byte_timeout =
		flag_buf[2] & SC8547A_FLAG_DATA_BYTE_TIMEOUT;
	reg->commu_error.baud_change =
		flag_buf[2] & SC8547A_FLAG_BAUD_RATE_CHANGE;
	reg->commu_error.rx_len_error = flag_buf[2] & SC8547A_FLAG_LENGTH_ERROR;
	reg->commu_error.rx_overflow = flag_buf[2] & SC8547A_FLAG_RX_OVERFLOW;
	reg->commu_error.bus_conflict = flag_buf[2] & SC8547A_FLAG_BUS_CONFLICT;

	reg->commu_error.start_fail = 0;
	reg->commu_error.stop_error = 0;

	if (chip->state == STATE_HANDSHAKE) {
		chip->handshake_success =
			(flag_buf[1] & SC8547A_FLAG_HANDSHAKE_SUCCESS) &&
			!(flag_buf[1] & SC8547A_FLAG_HANDSHAKE_FAIL);
	}
	ufcs_err("[0x%x, 0x%x, 0x%x]\n", flag_buf[0], flag_buf[1], flag_buf[2]);
	chip->get_flag_failed = 0;
	return 0;
}

static int sc8574a_dump_registers(struct oplus_sc8574a_ufcs *chip)
{
	int rc = 0;
	u8 addr;
	u8 val_buf[6] = { 0x0 };
	if (atomic_read(&sc8574a_ufcs->suspended) == 1) {
		ufcs_err("sc8574a is suspend!\n");
		return -ENODEV;
	}

	for (addr = 0; addr <= 5; addr++) {
		rc = sc8574a_read_byte(chip, addr, &val_buf[addr]);
		if (rc < 0) {
			ufcs_err("sc8574a_dump_registers Couldn't read 0x%02x "
				 "rc = %d\n",
				 addr, rc);
			break;
		}
	}
	ufcs_err(":[0~5][0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n", val_buf[0],
		 val_buf[1], val_buf[2], val_buf[3], val_buf[4], val_buf[5]);

	return 0;
}

static int sc8574a_chip_enable(void)
{
	u8 addr_buf[SC8547A_ENABLE_REG_NUM] = { SC8547A_ADDR_DPDM_CTRL,
						SC8547A_ADDR_UFCS_CTRL0,
						SC8547A_ADDR_TXRX_BUFFER_CTRL,
						SC8547A_ADDR_GENERAL_INT_FLAG1,
						SC8547A_ADDR_UFCS_INT_MASK0,
						SC8547A_ADDR_UFCS_INT_MASK1 };
	u8 cmd_buf[SC8547A_ENABLE_REG_NUM] = {
		SC8547A_CMD_DPDM_EN,	  SC8547A_CMD_EN_CHIP,
		SC8547A_CMD_CLR_TX_RX,	SC8547A_CMD_MASK_ACK_DISCARD,
		SC8547A_CMD_MASK_ACK_TIMEOUT, SC8547A_MASK_TRANING_BYTE_ERROR
	};
	int i = 0, rc = 0;
	struct oplus_sc8574a_ufcs *chip = sc8574a_ufcs;

	if (IS_ERR_OR_NULL(sc8574a_ufcs)) {
		ufcs_err("global pointer sc8574a_ufcs error\n");
		return -ENODEV;
	}

	if (atomic_read(&sc8574a_ufcs->suspended) == 1) {
		ufcs_err("sc8574a is suspend!\n");
		return -ENODEV;
	}
	if (oplus_voocphy_mg) {
		sc8547_reg_reset(oplus_voocphy_mg, true);
		msleep(10);
		sc8547_init_device(oplus_voocphy_mg);
	}
	for (i = 0; i < SC8547A_ENABLE_REG_NUM; i++) {
		rc = sc8574a_write_byte(chip, addr_buf[i], cmd_buf[i]);
		if (rc < 0) {
			ufcs_err("write i2c failed!\n");
			return rc;
		}
	}
	chip->ufcs_enable = true;
	sc8574a_reset_state_machine();
	sc8574a_set_state(STATE_HANDSHAKE);
	return 0;
}

static int sc8574a_chip_disable(void)
{
	int rc = 0;
	if (IS_ERR_OR_NULL(sc8574a_ufcs)) {
		ufcs_err("sc8574a_ufcs null \n");
		return -ENODEV;
	}

	rc = sc8574a_write_byte(sc8574a_ufcs, SC8547A_ADDR_UFCS_CTRL0,
				SC8547A_CMD_DIS_CHIP);
	if (rc < 0) {
		ufcs_err("write i2c failed\n");
		return rc;
	}
	sc8574a_ufcs->ufcs_enable = false;
	sc8574a_reset_state_machine();
	return 0;
}

static int sc8574a_report_error(struct oplus_ufcs_error *error)
{
	if (IS_ERR_OR_NULL(error)) {
		ufcs_err("sc8574a_error_flag buffer is null!\n");
		return -EINVAL;
	}

	if (sc8574a_ufcs->get_flag_failed) {
		ufcs_err("failed to read sc8574a buffer \n");
		return -EINVAL;
	}

	memcpy(error, &sc8574a_ufcs->error, sizeof(struct oplus_ufcs_error));
	return 0;
}

static int sc8574a_if_legal_state(enum UFCS_MACHINE_STATE expected_state)
{
	if (sc8574a_ufcs->state != expected_state) {
		ufcs_err("state machine expected = %d, now in = %d\n",
			 expected_state, sc8574a_ufcs->state);
		return -EINVAL;
	}

	if (sc8574a_ufcs->uct_mode == true) {
		ufcs_err("chip is in uct_mode! ap is not allowed to call "
			 "function!\n");
		return -EBUSY;
	}

	if (sc8574a_ufcs->error.i2c_error) {
		ufcs_err("i2c error happened over 2 times!\n");
		return -EINVAL;
	}

	if (sc8574a_ufcs->error.rcv_hardreset) {
		ufcs_err("received hard reset!\n");
		return -EINVAL;
	}

	if (sc8574a_ufcs->error.hardware_error) {
		ufcs_err("hardware error happened!\n");
		return -EINVAL;
	}

	if (sc8574a_ufcs->error.power_rdy_timeout) {
		ufcs_err("power ready timeout!\n");
		return -EINVAL;
	}

	return 0;
}

static int sc8574a_wait_for_ack(int ack_time, int *expected_flag,
				int expected_val)
{
	int rc = 0;
	struct receive_error *rcv_err = &sc8574a_ufcs->flag.rcv_error;

	if (sc8574a_ufcs->state != STATE_HANDSHAKE)
		sc8574a_set_state(STATE_WAIT_ACK);

	reinit_completion(&sc8574a_ufcs->rcv_cmp);
	rc = wait_for_completion_timeout(&sc8574a_ufcs->rcv_cmp,
					 msecs_to_jiffies(ack_time));
	if (!rc) {
		ufcs_err("ACK TIMEOUT!\n");
		goto error;
	}

	if (*expected_flag == expected_val)
		return 0;
	else {
		ufcs_err("expected flag value = %d, real value = %d\n",
			 expected_val, *expected_flag);
		goto error;
	}

	return 0;

error:
	if ((rcv_err->sent_cmp == 1) && (rcv_err->msg_trans_fail == 1))
		sc8574a_ufcs->soft_reset = 1;
	return -EBUSY;
}

static int sc8574a_check_refuse_msg(void)
{
	struct refuse_msg refuse = { 0 };
	u32 data = be32_to_cpup((u32 *)sc8574a_ufcs->rcv_msg.buf);

	memcpy(&refuse, &data, sizeof(struct refuse_msg));
	ufcs_err(" reason = 0x%x\n", refuse.reason);

	if (refuse.reason == REASON_DEVICE_BUSY)
		sc8574a_ufcs->soft_reset = 1;
	return -EPERM;
}

static int sc8574a_wait_for_msg(callback func, void *buffer, int msg_time,
				int expected_type, int expected_msg)
{
	int rc = 0;
	int msg_type = 0;
	int msg_command = 0;
	struct receive_error *rf = &sc8574a_ufcs->flag.rcv_error;

	sc8574a_set_state(STATE_WAIT_MSG);

	if ((rf->sent_cmp) && (rf->data_rdy)) {
		sc8574a_clr_flag_reg();
		goto msg_parse;
	}

	reinit_completion(&sc8574a_ufcs->rcv_cmp);
	rc = wait_for_completion_timeout(&sc8574a_ufcs->rcv_cmp,
					 msecs_to_jiffies(msg_time));
	if (!rc) {
		ufcs_err("wait for msg = 0x%02x, timeout!\n", expected_msg);
		return -EBUSY;
	}

	if (sc8574a_ufcs->msg_received == 0) {
		ufcs_err("no message is received!\n");
		return -EINVAL;
	}

msg_parse:
	msg_type = UFCS_HEADER_TYPE(sc8574a_ufcs->rcv_msg.header);
	msg_command = sc8574a_ufcs->rcv_msg.command;

	if ((msg_type != expected_type) || (msg_command != expected_msg)) {
		if ((msg_type != HEAD_TYPE_DATA) ||
		    (msg_command != UFCS_DATA_REFUSE)) {
			ufcs_err("unexpected message received! type = 0x%x, "
				 "msg = 0x%02x\n",
				 msg_type, msg_command);
			return -EINVAL;
		} else {
			return sc8574a_check_refuse_msg();
		}
	}

	if (func != NULL)
		rc = (*func)(buffer);

	if (rc < 0)
		return -EBUSY;
	else
		return 0;
}

static int sc8574a_handshake(void)
{
	int rc = 0;
	int retry_count = 0;
	rc = sc8574a_if_legal_state(STATE_HANDSHAKE);
	if (rc < 0)
		return rc;
retry:
	retry_count++;
	if (retry_count > UFCS_HANDSHAKE_RETRY_CNTS) {
		ufcs_err("handshake retry over 1 times, failed!\n");
		sc8574a_set_state(STATE_HANDSHAKE);
		return -EBUSY;
	}

	rc = sc8574a_write_bit_mask(sc8574a_ufcs, SC8547A_ADDR_UFCS_CTRL0,
				    SC8547A_MASK_EN_HANDSHAKE,
				    SC8547A_CMD_EN_HANDSHAKE);
	if (rc < 0) {
		ufcs_err("I2c send handshake error\n");
		goto retry;
	}

	rc = sc8574a_wait_for_ack(UFCS_HANDSHAKE_TIMEOUT,
				  &sc8574a_ufcs->handshake_success, 1);
	if (rc < 0) {
		ufcs_err("handshake retry. retry count = %d\n", retry_count);
		goto retry;
	} else {
		ufcs_err("handshake success!\n");
		sc8574a_set_state(STATE_PING);
		sc8574a_ufcs->ack_received = 0;
		sc8574a_ufcs->msg_received = 0;
		return 0;
	}
}

static int sc8574a_ping(void)
{
	int rc = 0;
	enum UFCS_BAUD_RATE baud_num = UFCS_BAUD_115200;

	rc = sc8574a_if_legal_state(STATE_PING);
	if (rc < 0)
		return rc;

retry:
	if (baud_num > UFCS_BAUD_19200) {
		sc8574a_set_state(STATE_PING);
		ufcs_err("baud number is lower than 19200, failed!\n");
		return -EBUSY;
	}

	sc8574a_write_bit_mask(sc8574a_ufcs, SC8547A_ADDR_UFCS_CTRL0,
			       FLAG_BAUD_RATE_VALUE,
			       (baud_num << FLAG_BAUD_NUM_SHIFT));

	rc = sc8574a_trans_msg(sc8574a_ufcs,
			       sc8574a_construct_ctrl_msg(UFCS_CTRL_PING));
	if (rc < 0) {
		ufcs_err("failed to send ping to sc8574a !\n");
		baud_num++;
		goto retry;
	}

	rc = sc8574a_wait_for_ack(UFCS_PING_TIMEOUT,
				  &sc8574a_ufcs->ack_received, 1);
	if (rc < 0) {
		baud_num++;
		ufcs_err("change baud rate to %d\n", baud_num);
		goto retry;
	} else {
		sc8574a_call_end();
		ufcs_err("ping success!\n");
		return baud_num;
	}
}

static int sc8574a_send_refuse(enum UFCS_REFUSE_REASON e_refuse_reason)
{
	int rc = 0;
	struct comm_msg *rcv = &sc8574a_ufcs->rcv_msg;
	struct refuse_msg msg = { 0 };

	msg.reason = e_refuse_reason;
	msg.cmd = rcv->command;
	msg.msg_type = UFCS_HEADER_TYPE(rcv->header);
	msg.msg_id = sc8574a_ufcs->msg_recv_id;

	rc = sc8574a_trans_msg(sc8574a_ufcs,
			       sc8574a_construct_data_msg(UFCS_DATA_REFUSE,
							  (void *)(&msg)));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received,
				  1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to send refuse to adapter\n");
	return rc;
}

static int sc8574a_parse_src_cap(void *payload)
{
	struct oplus_ufcs_src_cap *cap = (struct oplus_ufcs_src_cap *)payload;
	u64 *buffer = (u64 *)(sc8574a_ufcs->rcv_msg.buf);
	struct source_cap *output_cap = NULL;
	u64 data = 0;
	int i = 0;
	int len = sc8574a_ufcs->rcv_msg.len / PDO_LENGTH;

	if ((len > MAX_PDO_NUM) || (len < MIN_PDO_NUM)) {
		ufcs_err("error PDO number = %d");
		sc8574a_send_refuse(REASON_CMD_NOT_DETECT);
		return -EINVAL;
	}

	memset(cap, 0, sizeof(struct oplus_ufcs_src_cap));
	cap->cap_num = len;

	for (i = 0; i < len; i++, buffer++) {
		data = SWAP64(*buffer);
		output_cap = (struct source_cap *)(&data);
		cap->min_curr[i] = (output_cap->min_cur) * 10;
		cap->max_curr[i] = (output_cap->max_cur) * 10;
		cap->min_volt[i] = (output_cap->min_vol) * 10;
		cap->max_volt[i] = (output_cap->max_vol) * 10;
	}

	memset(&sc8574a_ufcs->src_cap, 0, sizeof(struct oplus_ufcs_src_cap));
	memcpy(&sc8574a_ufcs->src_cap, cap, sizeof(struct oplus_ufcs_src_cap));
	return 0;
}

static int
__sc8574a_retrieve_output_capabilities(struct oplus_ufcs_src_cap *buffer)
{
	int rc = 0;

	rc = sc8574a_trans_msg(sc8574a_ufcs, sc8574a_construct_ctrl_msg(
						     UFCS_CTRL_GET_OUTPUT_CAP));
	if (rc < 0) {
		ufcs_err("failed to send get output cap to sc8574a\n");
		goto end;
	}

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received,
				  1);
	if (rc < 0)
		goto end;

	rc = sc8574a_wait_for_msg(sc8574a_parse_src_cap, (void *)buffer,
				  UFCS_MSG_TIMEOUT, HEAD_TYPE_DATA,
				  UFCS_DATA_OUTPUT_CAP);
	if (rc < 0)
		goto end;

	return 0;

end:
	ufcs_err("failed to get source cap from adapter!!!\n");
	return rc;
}

static int
_sc8574a_retrieve_output_capabilities(struct oplus_ufcs_src_cap *buffer)
{
	int rc = 0;

	rc = __sc8574a_retrieve_output_capabilities(buffer);

	if ((rc < 0) && (sc8574a_ufcs->soft_reset)) {
		if (!sc8574a_send_soft_reset()) {
			rc = __sc8574a_retrieve_output_capabilities(buffer);
			if (rc < 0)
				sc8574a_source_hard_reset();
		}
	}

	return rc;
}

static int
sc8574a_retrieve_output_capabilities(struct oplus_ufcs_src_cap *buffer)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send get_output_cap to adapter\n");
		return rc;
	}

	sc8574a_get_chip_lock();
	rc = _sc8574a_retrieve_output_capabilities(buffer);
	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return rc;
}

static int sc8574a_select_legal_pdo(struct request_pdo *req, int volt, int curr)
{
	struct oplus_ufcs_src_cap *pdo = &sc8574a_ufcs->src_cap;
	int i = 0;
	if (sc8574a_ufcs->uct_mode == true) {
		req->cur = curr / 10;
		req->vol = volt / 10;
		req->reserve = 0;
		req->index = 1;
		return 0;
	}

	if (pdo->cap_num == 0) {
		ufcs_err("empty pdo list! \n");
		return -EBUSY;
	}

	for (i = 0; i < pdo->cap_num; i++) {
		if ((volt >= pdo->min_volt[i]) && (volt <= pdo->max_volt[i])) {
			if ((curr >= pdo->min_curr[i]) &&
			    (curr <= pdo->max_curr[i])) {
				req->cur = curr / 10;
				req->vol = volt / 10;
				req->index = i + 1;
				return 0;
			}
		}
	}

	ufcs_err("there's no legal pdo to select!!!\n");
	return -EBUSY;
}

static int sc8574a_wait_power_request(void *buf)
{
	int rc = 0;

	sc8574a_set_state(STATE_WAIT_MSG);

	reinit_completion(&sc8574a_ufcs->rcv_cmp);
	rc = wait_for_completion_timeout(
		&sc8574a_ufcs->rcv_cmp,
		msecs_to_jiffies(UFCS_POWER_RDY_TIMEOUT));
	if (!rc) {
		ufcs_err("Power Ready TIMEOUT!\n");
		goto error;
	}

	if (sc8574a_ufcs->msg_received == 0) {
		ufcs_err("invalid message received!\n");
		goto error;
	}

	if ((UFCS_HEADER_TYPE(sc8574a_ufcs->rcv_msg.header) !=
	     HEAD_TYPE_CTRL) ||
	    (sc8574a_ufcs->rcv_msg.command != UFCS_CTRL_POWER_READY)) {
		ufcs_err("Power ready isn't received! Received %02x\n",
			 sc8574a_ufcs->rcv_msg.command);
		goto error;
	}

	return 0;

error:
	sc8574a_ufcs->error.power_rdy_timeout = 1;
	sc8574a_source_hard_reset();
	return -EBUSY;
}

static int __sc8574a_power_negotiation(int volt, int curr)
{
	int rc = 0;
	struct request_pdo pdo = { 0 };

	rc = sc8574a_select_legal_pdo(&pdo, volt, curr);
	if (rc < 0)
		return rc;

	rc = sc8574a_trans_msg(sc8574a_ufcs,
			       sc8574a_construct_data_msg(UFCS_DATA_REQUEST,
							  (void *)(&pdo)));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received,
				  1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(sc8574a_wait_power_request, NULL,
				  UFCS_MSG_TIMEOUT, HEAD_TYPE_CTRL,
				  UFCS_CTRL_ACCEPT);
	if (rc < 0) {
		ufcs_err("failed to receive POWER READY\n");
		goto error;
	}
	return 0;

error:
	ufcs_err("failed to request pdo to adapter!!!\n");
	return rc;
}

static int _sc8574a_power_negotiation(int volt, int curr)
{
	int rc = 0;

	rc = __sc8574a_power_negotiation(volt, curr);

	if ((rc < 0) && (sc8574a_ufcs->soft_reset)) {
		if (!sc8574a_send_soft_reset()) {
			rc = __sc8574a_power_negotiation(volt, curr);
			if (rc < 0)
				sc8574a_source_hard_reset();
		}
	}

	return rc;
}

static int sc8574a_power_negotiation(int volt, int curr)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send request to adapter\n");
		return rc;
	}

	sc8574a_get_chip_lock();
	rc = _sc8574a_power_negotiation(volt, curr);
	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return rc;
}

static int sc8574a_report_max_current(int volt)
{
	struct oplus_ufcs_src_cap *pdo = &sc8574a_ufcs->src_cap;
	int i = 0;

	sc8574a_get_chip_lock();
	if (pdo->cap_num == 0) {
		ufcs_err("empty pdo list! \n");
		goto error;
	}

	for (i = 0; i <= pdo->cap_num - 1; i++) {
		if ((volt >= pdo->min_volt[i]) && (volt <= pdo->max_volt[i])) {
			ufcs_err("i = %d, max_curr = %d\n", i,
				 pdo->max_curr[i]);
			sc8574a_release_chip_lock();
			return pdo->max_curr[i];
		}
	}
	ufcs_err("there's no legal pdo to select!!!\n");
	for (i = 0; i <= pdo->cap_num - 1; i++) {
		ufcs_err("available pdo list %d: volt = %d ~ %d, current = %d "
			 "~ %d",
			 i, pdo->min_volt[i], pdo->max_volt[i],
			 pdo->min_curr[i], pdo->max_curr[i]);
	}

error:
	sc8574a_release_chip_lock();
	return -EBUSY;
}

static void sc8574a_reconstruct_msg(u8 *array, int length)
{
	int i = 0;
	int j = length - 1;
	u8 tmp = 0;

	for (; i <= j; i++, j--) {
		tmp = array[j];
		array[j] = array[i];
		array[i] = tmp;
	}
}

static int sc8574a_send_device_info(void *buffer)
{
	struct device_info dev_info = { 0 };
	int rc = 0;

	dev_info.dev_vender = OPLUS_DEV_VENDOR;
	dev_info.ic_vender = SC8547A_VENDOR_ID;
	dev_info.hardware_ver = SC8547A_HARDWARE_VER;
	dev_info.software_ver = SC8547A_SOFTWARE_VER;

	ufcs_err("replying device info to adapter. dev = %d, ic = %d, "
		 "hardware_ver = %d, software_ver = %d\n",
		 dev_info.dev_vender, dev_info.ic_vender, dev_info.hardware_ver,
		 dev_info.software_ver);

	rc = sc8574a_trans_msg(sc8574a_ufcs,
			       sc8574a_construct_data_msg(UFCS_DATA_DEVICE_INFO,
							  (void *)(&dev_info)));
	if (rc < 0) {
		ufcs_err("failed to send dev_info to adapter");
		return rc;
	}

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received,
				  1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		return rc;
	}

	return 0;
}

static int sc8574a_parse_verify_request(void *buffer)
{
	struct verify_response *resp = (struct verify_response *)buffer;

	if (sc8574a_ufcs->rcv_msg.len != VERIFY_RESPONSE_LENGTH) {
		ufcs_err("verify response length = %d, not 48!\n",
			 sc8574a_ufcs->rcv_msg.len);
		return -EINVAL;
	}
	memcpy(resp->encrypt_result, sc8574a_ufcs->rcv_msg.buf,
	       ADAPTER_ENCRYPT_LENGTH);
	memcpy(resp->adapter_random,
	       &sc8574a_ufcs->rcv_msg.buf[ADAPTER_ENCRYPT_LENGTH],
	       ADAPTER_RANDOM_LENGTH);
	sc8574a_reconstruct_msg(resp->encrypt_result, ADAPTER_ENCRYPT_LENGTH);
	sc8574a_reconstruct_msg(resp->adapter_random, ADAPTER_RANDOM_LENGTH);
	return 0;
}

static int __sc8574a_adapter_encryption(struct oplus_ufcs_encry_array *array)
{
	int rc = 0;
	struct verify_request req = { 0 };
	struct verify_response resp = { 0 };
	ufcs_err("!\n");
	if (!rc) {
		return -EBUSY;
	}
	memcpy(req.phone_random, array, PHONE_RANDOM_LENGTH);
	sc8574a_reconstruct_msg(req.phone_random, PHONE_RANDOM_LENGTH);
	req.id = UFCS_KEY_VER;

	rc = sc8574a_trans_msg(sc8574a_ufcs, sc8574a_construct_data_msg(
						     UFCS_DATA_VERIFY_REQUEST,
						     (void *)(&req)));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received,
				  1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(NULL, NULL, UFCS_MSG_TIMEOUT, HEAD_TYPE_CTRL,
				  UFCS_CTRL_ACCEPT);
	if (rc < 0) {
		ufcs_err("failed to receive accept\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(sc8574a_send_device_info, NULL,
				  UFCS_MSG_TIMEOUT, HEAD_TYPE_CTRL,
				  UFCS_CTRL_GET_DEVICE_INFO);
	if (rc < 0) {
		ufcs_err("get_device_info failed!\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(sc8574a_parse_verify_request, &resp,
				  UFCS_MSG_TIMEOUT, HEAD_TYPE_DATA,
				  UFCS_DATA_VERIFY_RESPONSE);
	if (rc < 0) {
		ufcs_err("verify_response failed!\n");
		goto error;
	}

	memcpy(array->adapter_random, resp.adapter_random,
	       ADAPTER_RANDOM_LENGTH);
	memcpy(array->adapter_encry_result, resp.encrypt_result,
	       ADAPTER_ENCRYPT_LENGTH);
	return 0;

error:
	ufcs_err("failed to encrypt with adapter!\n");
	return rc;
}

static int sc8574a_adapter_encryption(struct oplus_ufcs_encry_array *array)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send verify_request to adapter\n");
		return rc;
	}

	sc8574a_get_chip_lock();
	rc = __sc8574a_adapter_encryption(array);
	if ((rc < 0) && (sc8574a_ufcs->soft_reset)) {
		if (!sc8574a_send_soft_reset()) {
			rc = __sc8574a_adapter_encryption(array);
			if (rc < 0)
				sc8574a_source_hard_reset();
		}
	}

	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return rc;
}

static int sc8574a_parse_src_info(void *buffer)
{
	int **p = (int **)buffer;
	u64 *raw_data = (u64 *)sc8574a_ufcs->rcv_msg.buf;
	u64 data = SWAP64(*raw_data);
	struct src_info *res = (struct src_info *)(&data);

	*p[0] = (res->vol) * 10;
	*p[1] = (res->cur) * 10;

	return 0;
}

static int __sc8574a_retrieve_source_info(int *p_volt, int *p_curr)
{
	int rc = 0;
	int *p[2] = { p_volt, p_curr };

	rc = sc8574a_trans_msg(
		sc8574a_ufcs,
		sc8574a_construct_ctrl_msg(UFCS_CTRL_GET_SOURCE_INFO));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received,
				  1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(sc8574a_parse_src_info, p, UFCS_MSG_TIMEOUT,
				  HEAD_TYPE_DATA, UFCS_DATA_SOURCE_INFO);
	if (rc < 0) {
		ufcs_err("failed to receive source info from adapter\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to get source info\n");
	return rc;
}

static int _sc8574a_retrieve_source_info(int *p_volt, int *p_curr)
{
	int rc = 0;

	rc = __sc8574a_retrieve_source_info(p_volt, p_curr);

	if ((rc < 0) && (sc8574a_ufcs->soft_reset)) {
		if (!sc8574a_send_soft_reset()) {
			rc = __sc8574a_retrieve_source_info(p_volt, p_curr);
			if (rc < 0)
				sc8574a_source_hard_reset();
		}
	}

	return rc;
}

static int sc8574a_retrieve_source_info(int *p_volt, int *p_curr)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send get src_info to adapter\n");
		return rc;
	}

	sc8574a_get_chip_lock();
	rc = _sc8574a_retrieve_source_info(p_volt, p_curr);
	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return rc;
}

static int sc8574a_parse_adapter_id(void *payload)
{
	u64 data = be64_to_cpup((u64 *)sc8574a_ufcs->rcv_msg.buf);
	memcpy(payload, &data, sizeof(struct device_info));
	return 0;
}

static u16 __sc8574a_retrieve_adapter_id(void)
{
	int rc = 0;
	struct device_info dev = { 0 };

	rc = sc8574a_trans_msg(
		sc8574a_ufcs,
		sc8574a_construct_ctrl_msg(UFCS_CTRL_GET_DEVICE_INFO));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(sc8574a_parse_adapter_id, &dev,
				  UFCS_MSG_TIMEOUT, HEAD_TYPE_DATA,
				  UFCS_DATA_DEVICE_INFO);
	if (rc < 0) {
		ufcs_err("failed to receive device info from adapter\n");
		goto error;
	}
	ufcs_err(" [0x%x, 0x%x, 0x%x, 0x%x]\n", dev.software_ver,
		 dev.hardware_ver, dev.ic_vender, dev.dev_vender);
	if (((dev.software_ver >= ABNORMAL_HARDWARE_VER_A_MIN) &&
		(dev.software_ver <= ABNORMAL_HARDWARE_VER_A_MAX)) &&
	    ((dev.ic_vender == ABNORMAL_IC_VEND) &&
	    (dev.dev_vender == ABNORMAL_DEV_VEND)))
		sc8574a_ufcs->abnormal_id = true;

	if ((dev.ic_vender == UCT_MODE_IC_VEND) &&
	    (dev.dev_vender == UCT_MODE_DEV_VEND))
		sc8574a_ufcs->uct_mode = true;

	if ((dev.ic_vender == OPLUS_IC_VENDOR) &&
	    (dev.dev_vender == OPLUS_DEV_VENDOR))
		sc8574a_ufcs->oplus_id = true;
	else
		sc8574a_ufcs->oplus_id = false;
	return dev.hardware_ver;

error:
	ufcs_err("failed to get adapter id\n");
	return rc;
}

static u16 sc8574a_retrieve_adapter_id(void)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0)
		return rc;

	sc8574a_get_chip_lock();
	rc = __sc8574a_retrieve_adapter_id();

	if ((rc < 0) && (sc8574a_ufcs->soft_reset)) {
		if (!sc8574a_send_soft_reset()) {
			rc = __sc8574a_retrieve_adapter_id();
			if (rc < 0)
				sc8574a_source_hard_reset();
		}
	}

	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return rc;
}

static bool sc8574a_check_oplus_id(void)
{
	return sc8574a_ufcs->oplus_id;
}

static bool sc8574a_check_abnormal_id(void)
{
	return sc8574a_ufcs->abnormal_id;
}

static int sc8574a_flags_handler(struct oplus_sc8574a_ufcs *chip)
{
	struct hardware_error *fault = &chip->flag.hd_error;

	sc8574a_retrieve_reg_flags(chip);

	if ((fault->dm_ovp) || (fault->dm_ovp) || (fault->temp_shutdown) ||
	    (fault->wtd_timeout)) {
		chip->error.hardware_error = 1;
		ufcs_err("read sc8574a: find hardware error!\n");
		goto error;
	}

	if (fault->hardreset) {
		chip->error.rcv_hardreset = 1;
		ufcs_err("read sc8574a: adapter request hardreset!\n");
		goto error;
	}

	return 0;
error:
	complete(&chip->rcv_cmp);
	return -1;
}

static int sc8574a_source_hard_reset(void)
{
	int rc = 0;
	int retry_count = 0;

retry:
	retry_count++;
	if (retry_count > UFCS_HARDRESET_RETRY_CNTS) {
		ufcs_err("send hard reset, retry count over!\n");
		return -EBUSY;
	}

	rc = sc8574a_write_bit_mask(sc8574a_ufcs, SC8547A_ADDR_UFCS_CTRL0,
				    SEND_SOURCE_HARDRESET,
				    SEND_SOURCE_HARDRESET);
	if (rc < 0) {
		ufcs_err("I2c send handshake error\n");
		goto retry;
	}

	msleep(100);
	return 0;
}

static int sc8574a_send_soft_reset(void)
{
	int rc = 0;

	ufcs_err("sending soft reset to adapter\n");

	rc = sc8574a_trans_msg(
		sc8574a_ufcs, sc8574a_construct_ctrl_msg(UFCS_CTRL_SOFT_RESET));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received, 1);
	if (rc < 0)
		goto error;

	sc8574a_reset_msg_counter();
	ufcs_err("sending soft reset complete");
	return 0;

error:
	sc8574a_reset_msg_counter();
	sc8574a_source_hard_reset();
	ufcs_err("failed to send soft reset! sending hard reset!\n");
	return -EBUSY;
}

static int sc8574a_restore_sink_info(struct oplus_ufcs_sink_info *info)
{
	memcpy(&sc8574a_ufcs->info, info, sizeof(struct oplus_ufcs_sink_info));
	return 0;
}

static int sc8574a_exit_ufcs_mode(void)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send exit_ufcs to adapter\n");
		return rc;
	}

	sc8574a_get_chip_lock();

	rc = sc8574a_trans_msg(sc8574a_ufcs, sc8574a_construct_ctrl_msg(
						     UFCS_CTRL_EXIT_UFCS_MODE));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return 0;

error:
	sc8574a_release_chip_lock();
	sc8574a_call_end();
	ufcs_err("failed to send exit_ufcs_mode!\n");
	return rc;
}

static int __sc8574a_config_watchdog(int time_ms)
{
	int rc = 0;
	struct config_watchdog wdt = { 0 };

	wdt.timer = time_ms;

	rc = sc8574a_trans_msg(sc8574a_ufcs, sc8574a_construct_data_msg(
						     UFCS_DATA_CONFIG_WATCHDOG,
						     (void *)(&wdt)));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received, 1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(NULL, NULL, UFCS_MSG_TIMEOUT, HEAD_TYPE_CTRL,
				  UFCS_CTRL_ACCEPT);
	if (rc < 0) {
		ufcs_err("config_watchdog is not accepted by adapter\n");
		goto error;
	}

	return 0;

error:
	ufcs_err("failed to send config_watchdog to adapter\n");
	return rc;
}

static int _sc8574a_config_watchdog(int time_ms)
{
	int rc = 0;

	rc = __sc8574a_config_watchdog(time_ms);

	if ((rc < 0) && (sc8574a_ufcs->soft_reset)) {
		if (!sc8574a_send_soft_reset()) {
			rc = __sc8574a_config_watchdog(time_ms);
			if (rc < 0)
				sc8574a_source_hard_reset();
		}
	}

	return rc;
}

static int sc8574a_config_watchdog(int time_ms)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send config_watchdog to adapter\n");
		return rc;
	}

	sc8574a_get_chip_lock();
	rc = _sc8574a_config_watchdog(time_ms);
	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return rc;
}

static int sc8574a_parse_power_info(void *power_info)
{
	int **res = (int **)power_info;
	u64 *raw_data = (u64 *)sc8574a_ufcs->rcv_msg.buf;
	u64 data = SWAP64(*raw_data);
	struct charger_power *chg_power = (struct charger_power *)(&data);

	memset(res, 0, sizeof(struct oplus_charger_power));

	*res[0] = chg_power->vnd_p1;
	*res[1] = chg_power->vnd_p2;
	*res[2] = chg_power->vnd_p3;

	return 0;
}

static int __sc8574a_get_power_ability(struct oplus_charger_power *buffer)
{
	int rc = 0;
	struct charger_power power_ability = { 0 };
	power_ability.vnd_cmd = UFCS_VND_CHECK_POWER;

	rc = sc8574a_trans_msg(sc8574a_ufcs, sc8574a_construct_vnd_msg(
						     UFCS_VND_CHECK_POWER,
						     (void *)(&power_ability)));
	if (rc < 0)
		goto error;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received,
				  1);
	if (rc < 0) {
		ufcs_err("failed to receive ack\n");
		goto error;
	}

	rc = sc8574a_wait_for_msg(sc8574a_parse_power_info, (void *)buffer,
				  UFCS_MSG_TIMEOUT, HEAD_TYPE_CTRL,
				  UFCS_CTRL_ACCEPT);
	if (rc < 0) {
		ufcs_err(" error\n");
		goto error;
	}

	return 0;
error:
	ufcs_err("fail\n");
	return rc;
}

static int _sc8574a_get_power_ability(struct oplus_charger_power *buffer)
{
	int rc = 0;

	rc = __sc8574a_get_power_ability(buffer);

	if ((rc < 0) && (sc8574a_ufcs->soft_reset)) {
		if (!sc8574a_send_soft_reset()) {
			rc = __sc8574a_get_power_ability(buffer);
			if (rc < 0)
				sc8574a_source_hard_reset();
		}
	}

	return rc;
}

static int sc8574a_get_power_ability(struct oplus_charger_power *buffer)
{
	int rc = 0;

	rc = sc8574a_if_legal_state(STATE_IDLE);
	if (rc < 0) {
		ufcs_err("not ready to send config_watchdog to adapter\n");
		return rc;
	}

	sc8574a_get_chip_lock();
	rc = _sc8574a_get_power_ability(buffer);
	sc8574a_release_chip_lock();
	sc8574a_call_end();
	return rc;
}

static void sc8574a_notify_baudrate_change(void)
{
	u8 baud_rate = 0;

	if (!sc8574a_ufcs->flag.commu_error.baud_change) {
		ufcs_err("baud_rate change flag = 0, PING msg is received!\n");
		return;
	}

	sc8574a_read_byte(sc8574a_ufcs, SC8547A_ADDR_UFCS_CTRL0, &baud_rate);
	baud_rate = baud_rate & FLAG_BAUD_RATE_VALUE;
	ufcs_err("baud_rate change! new value = %d\n", baud_rate);
}

static int sc8574a_reply_sink_info(void)
{
	int rc = 0;
	struct sink_info info = { 0 };

	info.cur = abs(sc8574a_ufcs->info.batt_curr / 10);
	info.vol = sc8574a_ufcs->info.batt_volt / 10;
	info.usb_temp = ATTRIBUTE_NOT_SUPPORTED;
	info.dev_temp = ATTRIBUTE_NOT_SUPPORTED;

	rc = sc8574a_trans_msg(sc8574a_ufcs,
			       sc8574a_construct_data_msg(UFCS_DATA_SINK_INFO,
							  (void *)(&info)));
	if (rc < 0)
		return rc;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received, 1);
	if (rc < 0)
		return rc;

	return 0;
}

static int sc8574a_reply_error_info(void)
{
	int rc = 0;
	struct error_info err = { 0 };

	rc = sc8574a_trans_msg(sc8574a_ufcs,
			       sc8574a_construct_data_msg(UFCS_DATA_ERROR_INFO,
							  (void *)(&err)));
	if (rc < 0)
		return rc;

	rc = sc8574a_wait_for_ack(UFCS_ACK_TIMEOUT, &sc8574a_ufcs->ack_received, 1);
	if (rc < 0)
		return rc;

	return 0;
}

static int sc8574a_parse_power_change(void)
{
	int i = 0;
	u8 buffer[POWER_CHANGE_LENGTH] = { 0 };
	struct oplus_ufcs_src_cap *cap = &sc8574a_ufcs->src_cap;
	u8 *msg = sc8574a_ufcs->rcv_msg.buf;
	u8 len = sc8574a_ufcs->rcv_msg.len / POWER_CHANGE_LENGTH;

	if (len != cap->cap_num) {
		ufcs_err("invalid power change capability number = %d, "
			 "expected number = %d\n",
			 len, cap->cap_num);
		goto error;
	}

	for (i = 0; i < len; i++) {
		memset(buffer, 0, POWER_CHANGE_LENGTH);
		memcpy(buffer, &msg[i * POWER_CHANGE_LENGTH],
		       POWER_CHANGE_LENGTH);
		cap->max_curr[i] = (buffer[1] << 8 | buffer[2]) * 10;
		ufcs_err("power change! src_cap[%d]: voltage = %dmV, current = "
			 "%dmA\n",
			 i, cap->max_volt[i], cap->max_curr[i]);
	}

	return 0;
error:
	ufcs_err("adapter send invalid power change message!\n");
	return -EINVAL;
}

static int sc8574a_check_adapter_error(void)
{
	int rc = 0;
	unsigned long error_msg =
		be32_to_cpup((u32 *)sc8574a_ufcs->rcv_msg.buf) &
		VALID_ERROR_INFO_BITS;
	unsigned long error_type = find_first_bit(&error_msg, ERROR_INFO_NUM);

	int len = sc8574a_ufcs->rcv_msg.len;
	ufcs_err(" error_msg = %04x, len = %d to phone.\n", error_msg, len);

	if (len != ERROR_INFO_LEN) {
		ufcs_err("len = %d", len);
		sc8574a_send_refuse(REASON_CMD_NOT_DETECT);
		return rc;
	}

	if (error_msg == ATTRIBUTE_NOT_SUPPORTED) {
		ufcs_err("adapter sent error info = 0!\n");
		return rc;
	}

	for (; error_type < ERROR_INFO_NUM;
	     error_type = find_first_bit(&error_msg, ERROR_INFO_NUM)) {
		ufcs_err("receive adapter error! msg = %s\n",
			 adapter_error_info[error_type]);
		clear_bit(error_type, &error_msg);
		return -EINVAL;
	}
	return rc;
}

static bool sc8574a_check_uct_mode(void)
{
	return sc8574a_ufcs->uct_mode;
}

static bool sc8574a_quit_uct_mode(void)
{
	if (sc8574a_ufcs->error.rcv_hardreset)
		return true;
	else
		return false;
}

static int sc8574a_ctrl_uct_mode(int msg_num)
{
	int rc = 0;
	int volt = 0;
	int curr = 0;
	struct oplus_ufcs_src_cap cap = { 0 };

	switch (msg_num) {
	case UFCS_CTRL_GET_OUTPUT_CAP:
		rc = _sc8574a_retrieve_output_capabilities(&cap);
		break;

	case UFCS_CTRL_GET_SOURCE_INFO:
		rc = _sc8574a_retrieve_source_info(&volt, &curr);
		break;

	default:
		ufcs_err("uct mode message is not supported! msg_type = ctrl "
			 "msg; msg_num = %d\n",
			 msg_num);
		break;
	}

	return rc;
}

static int sc8574a_data_uct_mode(int msg_num)
{
	int rc = 0;
	int volt = UFCS_UCT_POWER_VOLT;
	int curr = UFCS_UCT_POWER_CURR;

	switch (msg_num) {
	case UFCS_DATA_REQUEST:
		rc = _sc8574a_power_negotiation(volt, curr);
		break;

	case UFCS_DATA_CONFIG_WATCHDOG:
		rc = _sc8574a_config_watchdog(UCT_MODE_WATCHDOG);
		break;

	default:
		ufcs_err("uct mode message is not supported! msg_type = data "
			 "msg; msg_num = %d\n",
			 msg_num);
		break;
	}

	return rc;
}

static int sc8574a_reply_uct_mode(void)
{
	struct uct_mode mode = { 0 };
	u16 uct_data = be16_to_cpup((u16 *)sc8574a_ufcs->rcv_msg.buf);
	int rc = 0;
	memcpy(&mode, &uct_data, sizeof(struct uct_mode));

	if (mode.msg_type == HEAD_TYPE_CTRL)
		rc = sc8574a_ctrl_uct_mode(mode.msg_num);
	else if (mode.msg_type == HEAD_TYPE_DATA)
		rc = sc8574a_data_uct_mode(mode.msg_num);
	else
		ufcs_err("uct mode message type is not supported! msg_type = "
			 "%d\n",
			 mode.msg_type);

	return rc;
}

static int sc8574a_reply_ctrl_msg(enum UFCS_CTRL_MSG_TYPE cmd)
{
	int rc = 0;

	switch (cmd) {
	case UFCS_CTRL_PING:
		sc8574a_notify_baudrate_change();
		break;
	case UFCS_CTRL_SOFT_RESET:
		ufcs_err("receive soft reset from adapter!\n");
		sc8574a_reset_msg_counter();
		break;
	case UFCS_CTRL_GET_SINK_INFO:
		rc = sc8574a_reply_sink_info();
		break;
	case UFCS_CTRL_GET_DEVICE_INFO:
		rc = sc8574a_send_device_info(NULL);
		break;
	case UFCS_CTRL_GET_ERROR_INFO:
		rc = sc8574a_reply_error_info();
		break;
	case UFCS_CTRL_GET_CABLE_INFO:
	case UFCS_CTRL_DETECT_CABLE_INFO:
	case UFCS_CTRL_START_CABLE_DETECT:
	case UFCS_CTRL_END_CABLE_DETECT:
		ufcs_err("reply cable related topic to adapter. send refuse to "
			 "adapter\n");
		rc = sc8574a_send_refuse(REASON_ID_NOT_SUPPORT);
		break;
	case UFCS_CTRL_EXIT_UFCS_MODE:
		ufcs_err("receive EXIT_UFCS_MODE from adapter!\n");
		sc8574a_ufcs->error.rcv_hardreset = 1;
		break;
	default:
		ufcs_err("reply data cmd = %d to adapter! msg = refuse\n", cmd);
		rc = sc8574a_send_refuse(REASON_ID_NOT_SUPPORT);
		break;
	}

	return rc;
}

static int sc8574a_reply_data_msg(enum UFCS_DATA_MSG_TYPE cmd)
{
	int rc = 0;

	switch (cmd) {
	case UFCS_DATA_CABLE_INFO:
	case UFCS_DATA_VERIFY_REQUEST:
	case UFCS_DATA_VERIFY_RESPONSE:
		ufcs_err("reply data cmd = %d to adapter! msg = refuse\n", cmd);
		rc = sc8574a_send_refuse(REASON_ID_NOT_SUPPORT);
		break;
	case UFCS_DATA_POWER_CHANGE:
		rc = sc8574a_parse_power_change();
		break;
	case UFCS_DATA_ERROR_INFO:
		rc = sc8574a_check_adapter_error();
		if (rc < 0)
			sc8574a_ufcs->error.rcv_hardreset = 1;
		break;
	case UFCS_DATA_UCT_REQUEST:
		rc = sc8574a_reply_uct_mode();
		break;
	default:
		ufcs_err("reply data cmd = %d to adapter! msg = refuse\n", cmd);
		rc = sc8574a_send_refuse(REASON_ID_NOT_SUPPORT);
		break;
	}

	return rc;
}

static void sc8574a_reply_adapter(struct work_struct *work)
{
	struct oplus_sc8574a_ufcs *chip =
		container_of(work, struct oplus_sc8574a_ufcs, reply_work);
	int rc = 0;
	int msg_type = UFCS_HEADER_TYPE(chip->rcv_msg.header);
	int msg_command = chip->rcv_msg.command;

	sc8574a_get_chip_lock();
	sc8574a_set_awake(chip, true);
	ufcs_err(" [0x%x, 0x%02x]\n", msg_type, msg_command);

	if (msg_type == HEAD_TYPE_CTRL)
		rc = sc8574a_reply_ctrl_msg(msg_command);
	else if (msg_type == HEAD_TYPE_DATA)
		rc = sc8574a_reply_data_msg(msg_command);
	else {
		rc = sc8574a_send_refuse(REASON_CMD_NOT_DETECT);
		ufcs_err("adapter sending wrong msg\n", msg_type, msg_command);
	}
	if (rc < 0)
		ufcs_err("reply to adapter failed!\n");

	if (chip->soft_reset)
		sc8574a_send_soft_reset();

	sc8574a_set_awake(chip, false);
	sc8574a_release_chip_lock();
	sc8574a_call_end();

	if (chip->error.rcv_hardreset)
		oplus_ufcs_source_exit();
}

static void sc8574a_rcv_work(struct kthread_work *work)
{
	struct oplus_sc8574a_ufcs *chip =
		container_of(work, struct oplus_sc8574a_ufcs, rcv_work);
	int rc = 0;

	sc8574a_set_awake(chip, true);
	rc = sc8574a_flags_handler(chip);
	if (rc < 0)
		return;
	ufcs_err(" chip->state = %d\n", chip->state);

	if ((chip->state != STATE_HANDSHAKE) && (chip->state != STATE_NULL)) {
		sc8574a_clr_rx_buffer();
		rc = sc8574a_rcv_msg(chip);
		if (rc < 0) {
			ufcs_err("error when retreive msg from sc8574a!\n");
			return;
		}
	}

	switch (chip->state) {
	case STATE_HANDSHAKE:
	case STATE_PING:
	case STATE_WAIT_ACK:
		complete_all(&chip->rcv_cmp);
		break;
	case STATE_WAIT_MSG:
		if (!rc)
			chip->msg_received = 1;
		else
			chip->msg_received = 0;
		complete_all(&chip->rcv_cmp);
		break;
	case STATE_IDLE:
		queue_work(system_highpri_wq, &chip->reply_work);
		break;
	default:
		ufcs_err("state machine error! state = %d\n", chip->state);
		break;
	}
	sc8574a_set_awake(chip, false);
	/*schedule_delayed_work(&chip->ufcs_service_work, 0);*/
}

static void ufcs_service(struct work_struct *work)
{
	struct oplus_sc8574a_ufcs *chip = container_of(
		work, struct oplus_sc8574a_ufcs, ufcs_service_work.work);

	if (!chip || !oplus_voocphy_mg) {
		ufcs_err("chip null\n");
		return;
	}
	if ((chip->state != STATE_IDLE) && (chip->state != STATE_WAIT_MSG))
		return;
	sc8547_get_int_value(oplus_voocphy_mg);
}

static int sc8574a_hardware_init(struct oplus_sc8574a_ufcs *chip)
{
	return 0;
}

static int sc8574a_parse_dt(struct oplus_sc8574a_ufcs *chip)
{
	struct device_node *node = NULL;

	if (!chip) {
		return -1;
	}
	node = chip->dev->of_node;
	return 0;
}

static void register_ufcs_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int rc = 0;
	char *version;
	char *manufacture;

	version = "sc8574a";
	manufacture = "SouthChip";

	rc = register_device_proc("sc8574a", version, manufacture);
	if (rc)
		ufcs_err("register_ufcs_devinfo fail\n");
#endif
}

static struct regmap_config sc8574a_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = SC8547A_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = sc8574a_is_volatile_reg,
};

int __attribute__((weak)) oplus_sm8350_pps_mos_ctrl(int on)
{
	return 1;
}

extern int oplus_chg_get_battery_btb_temp_cal(void);
extern int oplus_chg_get_usb_btb_temp_cal(void);
extern void oplus_chg_adc_switch_ctrl(void);

int __attribute__((weak)) oplus_chg_get_battery_btb_temp_cal(void)
{
	return 25;
}

int __attribute__((weak)) oplus_chg_get_usb_btb_temp_cal(void)
{
	return 25;
}

void __attribute__((weak)) oplus_chg_adc_switch_ctrl(void)
{
	return;
}

static int oplus_ufcs_mos_ctrl(int on)
{
	int ret = 0;
	if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_AP_VOOCPHY) {
		ret = oplus_voocphy_set_chg_enable(on);
	} else if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_ADSP_VOOCPHY) {
		ret = oplus_sm8350_pps_mos_ctrl(on);
	} else {
		/*do nothing*/
		ret = 0;
	}
	return ret;
}

static int oplus_ufcs_get_btb_temp_status(void)
{
	bool btb_status = true;
	int btb_temp = 0, usb_temp = 0;
	static unsigned char temp_over_count = 0;

	oplus_chg_adc_switch_ctrl();

	btb_temp = oplus_chg_get_battery_btb_temp_cal();
	usb_temp = oplus_chg_get_usb_btb_temp_cal();

	ufcs_err("btb_temp: %d, usb_temp: %d", btb_temp, usb_temp);
	if (btb_temp >= UFCS_BTB_TEMP_MAX || usb_temp >= UFCS_USB_TEMP_MAX) {
		temp_over_count++;
		if (temp_over_count > UFCS_BTB_USB_OVER_CNTS) {
			btb_status = false;
			ufcs_err("btb_and_usb temp over");
		}
	} else {
		temp_over_count = 0;
	}

	return btb_status;
}

static void oplus_ufcs_cp_hardware_init(void)
{
	ufcs_err(" end\n");
}

static int oplus_ufcs_cp_cfg_mode_init(int mode)
{
	int ret = 0;
	if (oplus_ufcs_get_support_type() != UFCS_SUPPORT_AP_VOOCPHY) {
		return ret;
	}
	if (mode == UFCS_SC_MODE)
		ret = oplus_voocphy_cp_svooc_init();
	else
		ret = oplus_voocphy_reset_voocphy();

	ufcs_err(" mode = %d\n", mode);

	return 0;
}

static int sc8547_master_get_vbus(void)
{
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	int cp_ibus = 0, vbus = 0;
	if (!oplus_voocphy_mg)
		return 0;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client,
					    SC8547_REG_13, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	cp_ibus = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;
	vbus = (((data_block[2] & SC8547_VBUS_POL_H_MASK) << 8) |
		data_block[3]) *
	       SC8547_VBUS_ADC_LSB;

	return vbus;
}

static int sc8547_master_get_ibus(void)
{
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	int cp_ibus = 0, vbus = 0;
	if (!oplus_voocphy_mg)
		return 0;

	memset(data_block, 0, sizeof(u8) * 4);

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client,
					    SC8547_REG_13, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	cp_ibus = ((data_block[0] << 8) | data_block[1]) * SC8547_IBUS_ADC_LSB;
	vbus = (((data_block[2] & SC8547_VBUS_POL_H_MASK) << 8) |
		data_block[3]) *
	       SC8547_VBUS_ADC_LSB;

	return cp_ibus;
}

static int sc8547_master_get_vac(void)
{
	int vac = 0;
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	if (!oplus_voocphy_mg)
		return 0;

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client,
					    SC8547_REG_17, 2, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vac error\n");
	} else {
		sc8547_i2c_error(false);
	}

	vac = (((data_block[0] & SC8547_VAC_POL_H_MASK) << 8) | data_block[1]) *
	      SC8547_VAC_ADC_LSB;

	return vac;
}

static int sc8547_master_get_vout(void)
{
	int vout = 0;
	u8 data_block[4] = { 0 };
	s32 ret = 0;
	if (!oplus_voocphy_mg)
		return 0;

	ret = i2c_smbus_read_i2c_block_data(oplus_voocphy_mg->client,
					    SC8547_REG_19, 4, data_block);
	if (ret < 0) {
		sc8547_i2c_error(true);
		pr_err("sc8547_update_data read vsys vbat error \n");
	} else {
		sc8547_i2c_error(false);
	}

	vout = ((data_block[0] << 8) | data_block[1]) * 125 / 100;
	/*chip->cp_vbat = ((data_block[2] << 8) | data_block[3])*125 / 100;*/

	return vout;
}

struct oplus_ufcs_operations oplus_ufcs_ops = {
	.ufcs_get_btb_temp_status = oplus_ufcs_get_btb_temp_status,
	.ufcs_mos_ctrl = oplus_ufcs_mos_ctrl,
	.get_ufcs_max_cur = sc8574a_report_max_current,

	.ufcs_cp_hardware_init = oplus_ufcs_cp_hardware_init,
	.ufcs_cp_mode_init = oplus_ufcs_cp_cfg_mode_init,

	.ufcs_chip_enable = sc8574a_chip_enable,
	.ufcs_chip_disable = sc8574a_chip_disable,
	.ufcs_get_error = sc8574a_report_error,
	.ufcs_transfer_sink_info = sc8574a_restore_sink_info,
	.ufcs_handshake = sc8574a_handshake,
	.ufcs_ping = sc8574a_ping,
	.ufcs_get_power_ability = sc8574a_get_power_ability,
	.ufcs_get_src_cap = sc8574a_retrieve_output_capabilities,
	.ufcs_request_pdo = sc8574a_power_negotiation,
	.ufcs_adapter_encryption = sc8574a_adapter_encryption,
	.ufcs_get_src_info = sc8574a_retrieve_source_info,
	.get_adapter_id = sc8574a_retrieve_adapter_id,
	.ufcs_config_watchdog = sc8574a_config_watchdog,
	.ufcs_exit = sc8574a_exit_ufcs_mode,
	.ufcs_hard_reset = sc8574a_source_hard_reset,
	.ufcs_check_uct_mode = sc8574a_check_uct_mode,
	.ufcs_check_oplus_id = sc8574a_check_oplus_id,
	.ufcs_check_abnormal_id = sc8574a_check_abnormal_id,
	.ufcs_quit_uct_mode = sc8574a_quit_uct_mode,

	.ufcs_get_cp_master_vbus = sc8547_master_get_vbus,
	.ufcs_get_cp_master_ibus = sc8547_master_get_ibus,
	.ufcs_get_cp_master_vac = sc8547_master_get_vac,
	.ufcs_get_cp_master_vout = sc8547_master_get_vout,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static void oplus_ufcs_sched_set_fifo(struct oplus_sc8574a_ufcs *chip)
{
	struct sched_param sp = {.sched_priority = MAX_RT_PRIO / 2 };

	if (!chip) {
		ufcs_err("chip null\n");
		return;
	}

	sched_setscheduler(chip->wq->task, SCHED_FIFO, &sp);
}
#else
static void oplus_ufcs_sched_set_fifo(struct oplus_sc8574a_ufcs *chip)
{
	if (!chip) {
		ufcs_err("chip null\n");
		return;
	}
	sched_set_fifo(chip->wq->task);
}
#endif

static int sc8547a_driver_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	struct oplus_sc8574a_ufcs *chip_ic;
	struct oplus_ufcs_chip *chip_ufcs;
	int ret;

	client->addr = SC8547A_I2C_ADDR;
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);
	if (!sc8547a_hw_version_check(chip)) {
		pr_err("sc8547 dont use ufcs\n");
		return -ENOMEM;
	}

	ret = sc8547_charger_choose(chip);
	if (ret <= 0)
		return ret;

	sc8547_create_device_node(&(client->dev));

	ret = sc8547_parse_dt(chip);

	sc8547_reg_reset(chip, true);

	sc8547_init_device(chip);

	ret = sc8547_irq_register(chip);
	if (ret < 0)
		goto err_1;

	chip->ops = &oplus_sc8547_ops;

	oplus_voocphy_init(chip);

	oplus_voocphy_mg = chip;
	sc8547_track_init(chip);

	init_proc_voocphy_debug();

	ufcs_err("sc8547a_parse_dt successfully!\n");
	chip_ic = devm_kzalloc(&client->dev, sizeof(struct oplus_sc8574a_ufcs),
			       GFP_KERNEL);
	if (!chip_ic) {
		ufcs_err("failed to allocate oplus_sc8574a_ufcs\n");
		return -ENOMEM;
	}

	chip_ic->regmap = devm_regmap_init_i2c(client, &sc8574a_regmap_config);
	if (!chip_ic->regmap) {
		ret = -ENODEV;
		goto regmap_init_err;
	}

	chip_ic->dev = &client->dev;
	chip_ic->client = client;
	i2c_set_clientdata(client, chip_ic);
	sc8574a_ufcs = chip_ic;

	chip_ufcs = devm_kzalloc(&client->dev, sizeof(struct oplus_ufcs_chip),
				 GFP_KERNEL);
	if (!chip_ufcs) {
		ufcs_err("kzalloc() chip_ufcs failed.\n");
		return -ENOMEM;
	}
	chip_ufcs->client = client;
	chip_ufcs->dev = &client->dev;

	chip_ic->wq = kthread_create_worker(0, dev_name(&client->dev));
	if (IS_ERR(chip_ic->wq)) {
		ufcs_err("create kthread worker failed!\n");
		goto regmap_init_err;
	}
	oplus_ufcs_sched_set_fifo(chip_ic);
	kthread_init_work(&chip_ic->rcv_work, sc8574a_rcv_work);
	INIT_WORK(&chip_ic->reply_work, sc8574a_reply_adapter);
	INIT_DELAYED_WORK(&(chip_ic->ufcs_service_work), ufcs_service);
	mutex_init(&chip_ic->chip_lock);
	init_completion(&chip_ic->rcv_cmp);
	sc8574a_awake_init(chip_ic);
	sc8574a_hardware_init(chip_ic);
	sc8574a_dump_registers(chip_ic);
	sc8574a_parse_dt(chip_ic);
	chip_ic->ufcs_enable = false;
	chip_ufcs->ops = &oplus_ufcs_ops;
	oplus_ufcs_init(chip_ufcs);

	register_ufcs_devinfo();
	ufcs_err("call end!\n");

	return 0;
regmap_init_err:
	devm_kfree(&client->dev, chip_ic);
	return ret;
err_1:
	pr_err("sc8547 probe err_1\n");
	return ret;
}

static int sc8574a_pm_resume(struct device *dev_chip)
{
	struct i2c_client *client =
		container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sc8574a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 0);
	ufcs_err(" %d!\n", chip->suspended);

	return 0;
}

static int sc8574a_pm_suspend(struct device *dev_chip)
{
	struct i2c_client *client =
		container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sc8574a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 1);
	ufcs_err(" %d!\n", chip->suspended);

	return 0;
}

static const struct dev_pm_ops sc8547a_pm_ops = {
	.resume = sc8574a_pm_resume,
	.suspend = sc8574a_pm_suspend,
};

static int sc8547a_driver_remove(struct i2c_client *client)
{
	struct oplus_sc8574a_ufcs *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return -ENODEV;

	kthread_destroy_worker(chip->wq);
	devm_kfree(&client->dev, chip);

	return 0;
}

static void sc8547a_shutdown(struct i2c_client *client)
{
	sc8547_write_byte(client, SC8547_REG_11, 0x00);
	sc8547_write_byte(client, SC8547_REG_21, 0x00);
	sc8574a_chip_disable();
	return;
}

static const struct of_device_id sc8547a_match[] = {
	{.compatible = "oplus,sc8547a-ufcs" },
	{},
};

static const struct i2c_device_id sc8547a_id[] = {
	{ "oplus,sc8547a-ufcs", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sc8547a_id);

static struct i2c_driver sc8547a_i2c_driver = {
	.driver =
		{
			.name = "sc8547a-ufcs",
			.owner = THIS_MODULE,
			.of_match_table = sc8547a_match,
			.pm = &sc8547a_pm_ops,
		},
	.probe = sc8547a_driver_probe,
	.remove = sc8547a_driver_remove,
	.id_table = sc8547a_id,
	.shutdown = sc8547a_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init sc8547a_i2c_driver_init(void)
{
	int ret = 0;
	ufcs_err(" init start\n");

	if (i2c_add_driver(&sc8547a_i2c_driver) != 0) {
		ufcs_err(" failed to register sc8574a i2c driver.\n");
	} else {
		ufcs_err(" Success to register sc8574a i2c driver.\n");
	}

	return ret;
}

subsys_initcall(sc8547a_i2c_driver_init);
#else
static __init int sc8547a_i2c_driver_init(void)
{
	return i2c_add_driver(&sc8547a_i2c_driver);
}

static __exit void sc8547a_i2c_driver_exit(void)
{
	i2c_del_driver(&sc8547a_i2c_driver);
}

oplus_chg_module_register(sc8547a_i2c_driver);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC SC8547A VOOCPHY&UFCS Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
