/*
 * SC8517 battery charging driver
*/

#define pr_fmt(fmt)	"[sc8517] %s: " fmt, __func__

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
#include<linux/ktime.h>
#include "oplus_voocphy.h"
#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "oplus_sc8517.h"

static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;
static bool error_reported = false;
extern void oplus_chg_sc8547_error(int report_flag, int *buf, int len);
static int sc8517_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip,
			int err_type, int reg);
static int sc8517_track_upload_cp_err_info(struct oplus_voocphy_manager *chip,
			int err_type);

static int sc8517_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data);
static void sc8517_i2c_error(bool happen)
{
	int report_flag = 0;
	if (!oplus_voocphy_mg)
		return;

	if (error_reported)
		return;

	if (happen) {
		oplus_voocphy_mg->voocphy_iic_err = 1;
		oplus_voocphy_mg->voocphy_iic_err_num++;
		report_flag |= (1 << 0);
		oplus_chg_sc8547_error(report_flag, NULL, 0);
		if (oplus_voocphy_mg->voocphy_iic_err_num >= 10){
			error_reported = true;
		}
	}
}

/************************************************************************/
static int __sc8517_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		sc8517_i2c_error(true);
		chg_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		sc8517_track_upload_i2c_err_info(chip, ret, reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __sc8517_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		sc8517_i2c_error(true);
		chg_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		sc8517_track_upload_i2c_err_info(chip, ret, reg);
		return ret;
	}

	return 0;
}

static int sc8517_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8517_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int sc8517_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8517_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}


static int sc8517_update_bits(struct i2c_client *client, u8 reg,
                              u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __sc8517_read_byte(client, reg, &tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8517_write_byte(client, reg, tmp);
	if (ret)
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sc8517_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		sc8517_i2c_error(true);
		chg_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&i2c_rw_lock);
		sc8517_track_upload_i2c_err_info(chip, ret, reg);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static s32 sc8517_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;
	struct oplus_voocphy_manager *chip;

	chip = i2c_get_clientdata(client);
	mutex_lock(&i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		sc8517_i2c_error(true);
		chg_err("i2c write word fail: can't write 0x%02X to reg:0x%02X \n", val, reg);
		mutex_unlock(&i2c_rw_lock);
		sc8517_track_upload_i2c_err_info(chip, ret, reg);
		return ret;
	}
	mutex_unlock(&i2c_rw_lock);
	return 0;
}
static int sc8517_read_i2c_block(struct i2c_client *client, u8 reg, u8 length, u8 *returnData)
{
	int rc = 0;
	int retry = 3;


	mutex_lock(&i2c_rw_lock);
	rc = i2c_smbus_read_i2c_block_data(client, reg, length, returnData);

	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_read_i2c_block_data(client, reg, length, returnData);
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (rc < 0) {
		chg_err("read err, rc = %d,\n", rc);
	}
	mutex_unlock(&i2c_rw_lock);
	return rc;
}

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int sc8517_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

static int sc8517_track_upload_i2c_err_info(struct oplus_voocphy_manager *chip,
		int err_type, int reg)
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
	curr_time = sc8517_track_get_local_time_s();
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
	chip->i2c_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->i2c_err_load_trigger) {
		pr_err("i2c_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_i2c_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->i2c_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->i2c_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->i2c_err_uploading = true;
	upload_count++;
	pre_upload_time = sc8517_track_get_local_time_s();
	mutex_unlock(&chip->track_i2c_err_lock);

	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
				"sc8517");
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
				OPLUS_CHG_TRACK_SCENE_I2C_ERR);

	oplus_chg_track_get_i2c_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->i2c_err_load_trigger->crux_info[index]),
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

static void sc8517_track_i2c_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
			container_of(dwork, struct oplus_voocphy_manager,
			i2c_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->i2c_err_load_trigger));
	if (chip->i2c_err_load_trigger) {
		kfree(chip->i2c_err_load_trigger);
		chip->i2c_err_load_trigger = NULL;
	}
	chip->i2c_err_uploading = false;
}

static int sc8517_dump_reg_info(struct oplus_voocphy_manager *chip,
				char *dump_info, int len)
{
	int ret;
	u8 data[6] = {0};
	int index = 0;

	if(!chip || !dump_info)
		return 0;

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_09, 6, data);
	if (ret < 0) {
		pr_err("read SC8517_REG_09 6 bytes failed\n");
		return -EINVAL;
	}

	index += snprintf(&(dump_info[index]), len - index,
			"REG_09~REG_0E:[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]",
			data[0], data[1], data[2], data[3], data[4], data[5]);

	return 0;
}

static int sc8517_track_upload_cp_err_info(struct oplus_voocphy_manager *chip,
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
	curr_time = sc8517_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	pr_info("err_type = %d\n", err_type);

	if (err_type == TRACK_BIDIRECT_CP_ERR_DEFAULT) {
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
	chip->cp_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->cp_err_load_trigger) {
		pr_err("cp_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_cp_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->cp_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->cp_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_CP_ABNORMAL;
	chip->cp_err_uploading = true;
	upload_count++;
	pre_upload_time = sc8517_track_get_local_time_s();
	mutex_unlock(&chip->track_cp_err_lock);

	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$device_id@@%s",
				"sc8517");
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "$$err_scene@@%s",
				OPLUS_CHG_TRACK_SCENE_BIDIRECT_CP_ERR);

	oplus_chg_track_get_bidirect_cp_err_reason(err_type, chip->err_reason, sizeof(chip->err_reason));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$err_reason@@%s", chip->err_reason);

	oplus_chg_track_obtain_power_info(chip->chg_power_info, sizeof(chip->chg_power_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index, "%s", chip->chg_power_info);
	sc8517_dump_reg_info(chip, chip->dump_info, sizeof(chip->dump_info));
	index += snprintf(&(chip->cp_err_load_trigger->crux_info[index]),
				OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
				"$$reg_info@@%s", chip->dump_info);
	schedule_delayed_work(&chip->cp_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	pr_info("success\n");

	return 0;
}

static void sc8517_track_cp_err_load_trigger_work(
		struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_voocphy_manager *chip =
			container_of(dwork, struct oplus_voocphy_manager,
			cp_err_load_trigger_work);

	if (!chip)
		return;

	oplus_chg_track_upload_trigger_data(*(chip->cp_err_load_trigger));
	if (chip->cp_err_load_trigger) {
		kfree(chip->cp_err_load_trigger);
		chip->cp_err_load_trigger = NULL;
	}
	chip->cp_err_uploading = false;
}

static int sc8517_track_debugfs_init(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sc8517;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_sc8517 = debugfs_create_dir("sc8517", debugfs_root);
	if (!debugfs_sc8517) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_i2c_err = false;
	chip->debug_force_cp_err = TRACK_BIDIRECT_CP_ERR_DEFAULT;
	debugfs_create_u32("debug_force_i2c_err", 0644,
			debugfs_sc8517, &(chip->debug_force_i2c_err));
	debugfs_create_u32("debug_force_cp_err", 0644,
			debugfs_sc8517, &(chip->debug_force_cp_err));

	return ret;
}

static int sc8517_track_init(struct oplus_voocphy_manager *chip)
{
	int rc;

	if (!chip)
		return - EINVAL;

	mutex_init(&chip->track_i2c_err_lock);
	mutex_init(&chip->track_cp_err_lock);
	mutex_init(&chip->track_upload_lock);
	chip->i2c_err_uploading = false;
	chip->i2c_err_load_trigger = NULL;
	chip->cp_err_uploading = false;
	chip->cp_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->i2c_err_load_trigger_work,
				sc8517_track_i2c_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->cp_err_load_trigger_work,
				sc8517_track_cp_err_load_trigger_work);
	rc = sc8517_track_debugfs_init(chip);
	if (rc < 0) {
		pr_err("sc8517 debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int sc8517_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		chg_err("failed: chip is null\n");
		return -1;
	}

	//predata
	ret = sc8517_write_word(chip->client, SC8517_REG_2A, val);
	if (ret < 0) {
		chg_err("failed: write predata\n");
		return -1;
	}
	chg_err("write predata 0x%0x\n", val);
	return ret;
}

static int sc8517_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret;
	if (!chip) {
		chg_err("failed: chip is null\n");
		return -1;
	}

	//txbuff
	ret = sc8517_write_word(chip->client, SC8517_REG_25, val);
	if (ret < 0) {
		chg_err("write txbuff\n");
		return -1;
	}

	return ret;
}

static int sc8517_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	s32 data;
	if (!chip) {
		chg_err("chip is null\n");
		return -1;
	}

	data = sc8517_read_word(chip->client, SC8517_REG_27);

	if (data < 0) {
		chg_err("sc8517_read_word faile\n");
		return -1;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	chg_err("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n", data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void sc8517_update_data(struct oplus_voocphy_manager *chip)
{
	/*int_flag*/
	//sc8517_read_byte(chip->client, SC8517_REG_09, &data);
	chip->int_flag = 0;

	chip->cp_vsys = 0;
	chip->cp_vbat = 0;

	chip->cp_ichg = 0;
	chip->cp_vbus = 0;

	chg_err("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d int_flag = %d",chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->int_flag);
}

static int sc8517_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	return 0;
}

int sc8517_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	return 0;
}

/*********************************************************************/
static int sc8517_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret;
	u8 val;
	if (enable)
		val = SC8517_RESET_REG;
	else
		val = SC8517_NO_REG_RESET;

	val <<= SC8517_REG_RESET_SHIFT;

	ret = sc8517_update_bits(chip->client, SC8517_REG_06,
	                         SC8517_REG_RESET_MASK, val);
	chg_err("----enable = %d\n",enable);

	return ret;
}

static int sc8517_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	ret = sc8517_read_byte(chip->client, SC8517_REG_02, data);
	if (ret < 0) {
		chg_err("SC8517_REG_07\n");
		return -1;
	}

	*data = *data & SC8517_CHG_EN_MASK;

	return ret;
}

static int sc8517_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	sc8517_write_byte(chip->client, SC8517_REG_02, 0x7a);//vac range disable

	if (enable) {
		ret = sc8517_write_byte(chip->client, SC8517_REG_02, 0x7b);//enable mos
	}
	else {
		ret = sc8517_write_byte(chip->client, SC8517_REG_02, 0x78);//disable mos
	}
	chg_err("----enable = %d\n",enable);

	if (ret < 0) {
		chg_err("SC8517_REG_07\n");
		return -1;
	}
	return ret;
}


static int sc8517_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;



	return ret;
}

static int sc8517_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	return 0;
}

static u8 sc8517_get_vbus_status(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 value = 0;

	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	ret = sc8517_read_byte(chip->client, SC8517_REG_0B, &value);
	if (ret < 0) {
		chg_err("SC8517_REG_0B\n");
		return -1;
	}

	value = (value & VBUS_INRANGE_STATUS_MASK) >> VBUS_INRANGE_STATUS_SHIFT;
	chg_err("----value = %d\n",value);

	return value;
}

static int sc8517_get_voocphy_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		pr_err("Failed\n");
		return -1;
	}

	ret = sc8517_read_byte(chip->client, SC8517_REG_2B, data);
	if (ret < 0) {
		pr_err("SC8517_REG_2B\n");
		return -1;
	}

	return ret;
}

static void sc8517_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	if(!chip) {
		pr_err( "!!!!! oplus_voocphy_manager chip NULL");
		return;
	}
	chg_err("SC8517_REG_09 -->09~0E[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]\n", chip->reg_dump[9], chip->reg_dump[10], chip->reg_dump[11], chip->reg_dump[12], chip->reg_dump[13], chip->reg_dump[14]);

	return;
}

static u8 sc8517_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 int_column[6];

	if (!chip) {
		chg_err("%s: chip null\n", __func__);
		return -1;
	}
	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_09, 6, int_column);

	if (ret < 0) {
		sc8517_i2c_error(true);
		chg_err(" read SC8517_REG_09 6 bytes failed\n");
		memset(chip->int_column, 0, sizeof(chip->int_column));
		/* set int_column[1]=1, otherwise the dcdc chg can't be enabled*/
		chip->int_column[1] = BIT(0);
		return chip->int_column[1];
	}
	memcpy(chip->int_column, int_column, sizeof(chip->int_column));
	chg_err("SC8517_REG_09 -->09~0E[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]\n", chip->int_column[0], chip->int_column[1], chip->int_column[2], chip->int_column[3], chip->int_column[4], chip->int_column[5]);

	return int_column[1];
}

static u8 sc8517_get_chg_auto_mode(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 value = 0;

	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}
	ret = sc8517_read_byte(chip->client, SC8517_REG_15, &value);
	value = value & SC8517_CHG_MODE_MASK;

	chg_err("----value = %d\n",value);
	return value;
}

static int sc8517_set_chg_auto_mode(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	if(enable && (sc8517_get_chg_auto_mode(chip) == SC8517_CHG_FIX_MODE)) {
		ret = sc8517_update_bits(chip->client, SC8517_REG_15,SC8517_CHG_MODE_MASK, SC8517_CHG_AUTO_MODE);
	}
	else if(!enable && (sc8517_get_chg_auto_mode(chip) == SC8517_CHG_AUTO_MODE)) {
		ret = sc8517_update_bits(chip->client, SC8517_REG_15,SC8517_CHG_MODE_MASK, SC8517_CHG_FIX_MODE);
	}
	else {
		ret = 0;
	}
	chg_err(",enable = %d\n",enable);

	if (ret < 0) {
		chg_err("SC8517_REG_15\n");
		return -1;
	}
	return ret;
}

static void sc8517_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("Failed\n");
		return;
	}

	sc8517_write_byte(chip->client, SC8517_REG_04, 0x36);	//WD:1000ms
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xd1);	//Loose_det=1

	chg_err("pd svooc \n");
}

static bool sc8517_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("Failed\n");
		return false;
	}
	chg_err("no ucp flag,return true \n");
	return true;
}

void sc8517_send_handshake(struct oplus_voocphy_manager *chip)
{
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x81);//enable voocphy and handshake
}


static int sc8517_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	//turn off mos
	sc8517_write_byte(chip->client, SC8517_REG_02, 0x78);
	//clear tx data
	sc8517_write_byte(chip->client, SC8517_REG_25, 0x00);
	sc8517_write_byte(chip->client, SC8517_REG_26, 0x00);
	//disable vooc phy irq
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x7F);//mask all flag
	sc8517_write_byte(chip->client, SC8517_REG_10, 0x79);//disable irq
	//set D+ HiZ
	sc8517_write_byte(chip->client, SC8517_REG_20, 0xc0);

	//select big bang mode

	//disable vooc
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00);

	sc8517_write_byte(chip->client, SC8517_REG_04, 0x06);	//disable wdt

	//set predata
	//sc8517_write_word(chip->client, SC8517_REG_2A, 0x0);
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x02);//reset voocphy
	msleep(1);
	//sc8517_set_predata(0x0);

	chg_err("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}


static int sc8517_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	//set predata 0
	sc8517_write_word(chip->client, SC8517_REG_2A, 0x0);

	//dpdm
	sc8517_write_byte(chip->client, SC8517_REG_20, 0x21);
	sc8517_write_byte(chip->client, SC8517_REG_21, 0x80);
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xD1);

	//clear tx data
	sc8517_write_byte(chip->client, SC8517_REG_25, 0x00);
	sc8517_write_byte(chip->client, SC8517_REG_26, 0x00);

	//vooc
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x05);//diable vooc int flag mask
 	sc8517_send_handshake(chip);

	chg_err ("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static irqreturn_t sc8517_charger_interrupt(int irq, void *dev_id)
{
	struct oplus_voocphy_manager *chip = dev_id;

	if (!chip) {
		return IRQ_HANDLED;
	}

	return oplus_voocphy_interrupt_handler(chip);
}

static int sc8517_init_device(struct oplus_voocphy_manager *chip)
{
	sc8517_reg_reset(chip, true);
	msleep(10);
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00);//VOOC_CTRL:disable,no handshake
	chg_err("----\n");

	return 0;
}

static int sc8517_init_vooc(struct oplus_voocphy_manager *chip)
{
	chg_err(" >>>>start init vooc\n");

	sc8517_write_byte(chip->client, SC8517_REG_24, 0x02);//reset voocphy
	sc8517_write_word(chip->client, SC8517_REG_2A, 0x00);//reset predata 00
	msleep(1);
	sc8517_write_byte(chip->client, SC8517_REG_20, 0x21);//VOOC_CTRL:disable,no handshake
	sc8517_write_byte(chip->client, SC8517_REG_21, 0x80);//VOOC_CTRL:disable,no handshake
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xD1);//VOOC_CTRL:disable,no handshake
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x25);//mask sedseq flag,rx start,tx done
	/*sc8517_vac_inrange_enable(chip, SC8517_VAC_INRANGE_DISABLE);*/

	return 0;
}

static int sc8517_hardware_init(struct oplus_voocphy_manager *chip)
{
	chg_err("----\n");
	sc8517_reg_reset(chip, true);
	msleep(10);
	sc8517_write_byte(chip->client, SC8517_REG_01, 0x5e);//disable v1x_scp 
	sc8517_write_byte(chip->client, SC8517_REG_06, 0x85);//enable  audio mode
	sc8517_write_byte(chip->client, SC8517_REG_08, 0xA6);//REF_SKIP_R 40mv
	sc8517_write_byte(chip->client, SC8517_REG_29, 0x05);//Masked Pulse_filtered, RX_Start,Tx_Done,soft intflag
	sc8517_write_byte(chip->client, SC8517_REG_10, 0x79);//Masked Pulse_filtered, RX_Start,Tx_Done
	return 0;
}

int sc8517_dump_registers(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 int_column[7];

	if (!chip) {
		chg_err("%s: chip null\n", __func__);
		return -1;
	}

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_00, 7, int_column);
	if (ret < 0) {
		chg_err(" read SC8517_REG_00 7 bytes failed\n");
		return -1;
	}
	chg_err("[00~06][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
				int_column[0], int_column[1], int_column[2], int_column[3], int_column[4], int_column[5], int_column[6]);

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_07, 7, int_column);
	if (ret < 0) {
		chg_err(" read SC8517_REG_07 7 bytes failed\n");
		return -1;
	}
	chg_err("[07~0D][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
				int_column[0], int_column[1], int_column[2], int_column[3], int_column[4], int_column[5], int_column[6]);

	ret = sc8517_read_i2c_block(chip->client, SC8517_REG_0E, 7, int_column);
	if (ret < 0) {
		chg_err(" read SC8517_REG_0E 7 bytes failed\n");
		return -1;
	}
	chg_err("[0E~14][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
				int_column[0], int_column[1], int_column[2], int_column[3], int_column[4], int_column[5], int_column[6]);

	return ret;
}

static int sc8517_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		chg_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->irq_gpio = of_get_named_gpio(node,
	                                   "qcom,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		chg_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			rc = gpio_request(chip->irq_gpio,
			                  "irq_gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
				         chip->irq_gpio);
			}
		}
		chg_err("chip->irq_gpio =%d\n", chip->irq_gpio);
	}
	//irq_num
	chip->irq = gpio_to_irq(chip->irq_gpio);
	chg_err("irq way1 chip->irq =%d\n",chip->irq);

	/* set voocphy pinctrl*/
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->charging_inter_active =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_active");
	if (IS_ERR_OR_NULL(chip->charging_inter_active)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	chip->charging_inter_sleep =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_sleep");
	if (IS_ERR_OR_NULL(chip->charging_inter_sleep)) {
		chg_err(": %d Failed to get the state pinctrl handle\n", __LINE__);
		return -EINVAL;
	}

	//irq active
	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl, chip->charging_inter_active); /* no_PULL */

	rc = gpio_get_value(chip->irq_gpio);
	chg_err("irq chip->irq_gpio input =%d irq_gpio_stat = %d\n", chip->irq_gpio, rc);

	return 0;
}

static int sc8517_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret = 0;

	sc8517_irq_gpio_init(chip);
	chg_err("sc8517 chip->irq = %d\n", chip->irq);
	if (chip->irq) {
		ret = request_threaded_irq(chip->irq, NULL,
		                           sc8517_charger_interrupt,
		                           IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		                           "sc8517_charger_irq", chip);
		if (ret < 0) {
			chg_err("request irq for irq=%d failed, ret =%d\n",
			         chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
	}
	chg_err("request irq ok\n");

	return ret;
}

static int sc8517_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x36);	//WD:1000ms
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xd1);	//Loose_det=1
	chg_err("----\n");

	return 0;
}

static int sc8517_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	chg_err("----\n");
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x46);	//WD:5000ms
	sc8517_write_byte(chip->client, SC8517_REG_2C, 0xd1);	//Loose_det=1
	return 0;
}

static int sc8517_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	chg_err("----\n");

	sc8517_write_byte(chip->client, SC8517_REG_02, 0x78);	//disable acdrv,close mos
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x06);	//dsiable wdt
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00);	//close voocphy

	return 0;
}

static int sc8517_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	chg_err("----\n");
	sc8517_write_byte(chip->client, SC8517_REG_02, 0x78);	//disable acdrv,close mos
	sc8517_write_byte(chip->client, SC8517_REG_04, 0x06);	//dsiable wdt
	sc8517_write_byte(chip->client, SC8517_REG_24, 0x00);	//close voocphy
	return 0;
}

static int sc8517_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		chg_err("chip is null exit\n");
		return -1;
	}
	chg_err("----reason = %d\n",reason);
	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		sc8517_init_device(chip);
		chg_err("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		sc8517_svooc_hw_setting(chip);
		chg_err("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		sc8517_vooc_hw_setting(chip);
		chg_err("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		sc8517_5v2a_hw_setting(chip);
		chg_err("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		sc8517_pdqc_hw_setting(chip);
		chg_err("SETTING_REASON_PDQC\n");
		break;
	default:
		chg_err("do nothing\n");
		break;
	}
	return 0;
}

static ssize_t sc8517_show_registers(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8517");
	for (addr = 0x0; addr <= 0x3C; addr++) {
		if((addr < 0x24) || (addr > 0x2B && addr < 0x33)
		   || addr == 0x36 || addr == 0x3C) {
			ret = sc8517_read_byte(chip->client, addr, &val);
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

static ssize_t sc8517_store_register(struct device *dev,
                                     struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x3C)
		sc8517_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}


static DEVICE_ATTR(registers, 0660, sc8517_show_registers, sc8517_store_register);

static void sc8517_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}


static struct of_device_id sc8517_charger_match_table[] = {
	{
		.compatible = "sc,sc8517-master",
	},
	{},
};

static int sc8517_gpio_init(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get chargerid_switch_gpio pinctrl fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_high =
	    pinctrl_lookup_state(chip->pinctrl,
	                         "switch1_act_switch2_act");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_high)) {
		chg_err("get switch1_act_switch2_act fail\n");
		return -EINVAL;
	}

	chip->charger_gpio_sw_ctrl2_low =
	    pinctrl_lookup_state(chip->pinctrl,
	                         "switch1_sleep_switch2_sleep");
	if (IS_ERR_OR_NULL(chip->charger_gpio_sw_ctrl2_low)) {
		chg_err("get switch1_sleep_switch2_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->pinctrl,
	                     chip->charger_gpio_sw_ctrl2_low);

	chg_err("oplus_chip is ready!\n");
	return 0;
}

static int sc8517_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node * node = NULL;

	if (!chip) {
		chg_err("chip null\n");
		return -1;
	}

	/* Parsing gpio switch gpio47*/
	node = chip->dev->of_node;
	chip->switch1_gpio = of_get_named_gpio(node,
	                                       "qcom,charging_switch1-gpio", 0);
	if (chip->switch1_gpio < 0) {
		chg_err("chip->switch1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->switch1_gpio)) {
			rc = gpio_request(chip->switch1_gpio,
			                  "charging-switch1-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
				         chip->switch1_gpio);
			} else {
				rc = sc8517_gpio_init(chip);
				if (rc)
					chg_err("unable to init charging_sw_ctrl2-gpio:%d\n",
					        chip->switch1_gpio);
			}
		}
		chg_err("chip->switch1_gpio =%d\n", chip->switch1_gpio);
	}

	return 0;
}

static void sc8517_set_switch_fast_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("sc8517_set_switch_fast_charger chip null\n");
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_high);
	gpio_direction_output(chip->switch1_gpio, 1);	/* out 1*/

	chg_err("switch switch2 %d to fast finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sc8517_set_switch_normal_charger(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("sc8517_set_switch_normal_charger chip null\n");
		return;
	}

	pinctrl_select_state(chip->pinctrl, chip->charger_gpio_sw_ctrl2_low);
	if (chip->switch1_gpio > 0) {
		gpio_direction_output(chip->switch1_gpio, 0);	/* in 0*/
	}

	chg_err("switch switch2 %d to normal finshed\n", gpio_get_value(chip->switch1_gpio));

	return;
}

static void sc8517_set_switch_mode(struct oplus_voocphy_manager *chip, int mode)
{
	switch (mode) {
	case VOOC_CHARGER_MODE:
		sc8517_set_switch_fast_charger(chip);
		break;
	case NORMAL_CHARGER_MODE:
	default:
		sc8517_set_switch_normal_charger(chip);
		break;
	}

	return;
}

static void register_voocphy_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int ret = 0;
	char *version;
	char *manufacture;

	version = "sc8517";
	manufacture = "MP";

	ret = register_device_proc("voocphy", version, manufacture);
	if (ret)
		chg_err("register_voocphy_devinfo fail\n");
#endif
}

static struct oplus_voocphy_operations oplus_sc8517_ops = {
	.hw_setting		= sc8517_hw_setting,
	.init_vooc		= sc8517_init_vooc,
	.set_predata	= sc8517_set_predata,
	.set_txbuff		= sc8517_set_txbuff,
	.get_adapter_info	= sc8517_get_adapter_info,
	.update_data		= sc8517_update_data,
	.get_chg_enable		= sc8517_get_chg_enable,
	.set_chg_enable		= sc8517_set_chg_enable,
	.reset_voocphy		= sc8517_reset_voocphy,
	.reactive_voocphy	= sc8517_reactive_voocphy,
	.set_switch_mode	= sc8517_set_switch_mode,
	.send_handshake		= sc8517_send_handshake,
	.get_cp_vbat		= sc8517_get_cp_vbat,
	.get_int_value		= sc8517_get_int_value,
	.get_adc_enable		= sc8517_get_adc_enable,
	.set_adc_enable		= sc8517_set_adc_enable,
	.get_ichg			= sc8517_get_cp_ichg,
	.set_pd_svooc_config = sc8517_set_pd_svooc_config,
	.get_pd_svooc_config = sc8517_get_pd_svooc_config,
	.get_vbus_status	 = sc8517_get_vbus_status,
	.set_chg_auto_mode 	= sc8517_set_chg_auto_mode,
	.get_voocphy_enable = sc8517_get_voocphy_enable,
	.dump_voocphy_reg	= sc8517_dump_reg_in_err_issue,
	.upload_cp_error	= sc8517_track_upload_cp_err_info,
};

static int sc8517_charger_probe(struct i2c_client *client,
                                const struct i2c_device_id *id)
{
	struct oplus_voocphy_manager *chip;
	int ret;

	chg_err("sc8517_charger_probe enter!\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&i2c_rw_lock);

	i2c_set_clientdata(client, chip);

	sc8517_create_device_node(&(client->dev));

	ret = sc8517_parse_dt(chip);

	sc8517_hardware_init(chip);

	//sc8517_init_device(chip);

	ret = sc8517_irq_register(chip);
	if (ret < 0)
		goto err_1;

	chip->ops = &oplus_sc8517_ops;

	oplus_voocphy_init(chip);

	oplus_voocphy_mg = chip;
	register_voocphy_devinfo();
	init_proc_voocphy_debug();
	sc8517_track_init(chip);
	sc8517_dump_registers(chip);

	chg_err("sc8517_parse_dt successfully!\n");

	return 0;

err_1:
	chg_err("sc8517 probe err_1\n");
	return ret;
}

static void sc8517_charger_shutdown(struct i2c_client *client)
{
	sc8517_update_bits(client, SC8517_REG_06,SC8517_REG_RESET_MASK, SC8517_RESET_REG << SC8517_REG_RESET_SHIFT);
	msleep(10);
	sc8517_write_byte(client, SC8517_REG_01, 0x5e);//disable v1x_scp 
	sc8517_write_byte(client, SC8517_REG_08, 0xA6);//REF_SKIP_R 40mv
	sc8517_write_byte(client, SC8517_REG_29, 0x05);//Masked Pulse_filtered, RX_Start,Tx_Done,bit soft intflag

	return;
}

static const struct i2c_device_id sc8517_charger_id[] = {
	{"sc8517-master", 0},
	{},
};

static struct i2c_driver sc8517_charger_driver = {
	.driver		= {
		.name	= "sc8517-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sc8517_charger_match_table,
	},
	.id_table	= sc8517_charger_id,

	.probe		= sc8517_charger_probe,
	.shutdown	= sc8517_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init sc8517_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&sc8517_charger_driver) != 0) {
		chg_err(" failed to register sc8517 i2c driver.\n");
	} else {
		chg_debug(" Success to register sc8517 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(sc8517_subsys_init);
#else
int sc8517_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&sc8517_charger_driver) != 0) {
		chg_err(" failed to register sc8517 i2c driver.\n");
	} else {
		chg_debug(" Success to register sc8517 i2c driver.\n");
	}

	return ret;
}

void sc8517_subsys_exit(void)
{
	i2c_del_driver(&sc8517_charger_driver);
}
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

//module_i2c_driver(sc8517_charger_driver);

MODULE_DESCRIPTION("SC SC8517 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("kongfanhong");
