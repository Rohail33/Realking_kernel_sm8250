/*
 * aw87xxx_monitor.c
 *
 * Copyright (c) 2020 AWINIC Technology CO., LTD
 *
 * Author: Bruce <zhaolei@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include "aw87xxx_monitor.h"
#include "aw87xxx.h"

static DEFINE_MUTEX(g_dsp_lock);
static DEFINE_MUTEX(g_msg_dsp_lock);
/****************************************************************************
* aw87xxx dsp communication
*****************************************************************************/
#ifdef AWINIC_ADSP_ENABLE
extern int aw_send_afe_cal_apr(uint32_t param_id,
			       void *buf, int cmd_size, bool write);
extern int aw_send_afe_rx_module_enable(void *buf, int size);
extern int aw_send_afe_tx_module_enable(void *buf, int size);
#else
static int aw_send_afe_cal_apr(uint32_t param_id,
			       void *buf, int cmd_size, bool write)
{
	pr_info("%s enter, no define AWINIC_ADSP_ENABLE\n", __func__);
	return 0;
}

static int aw_send_afe_rx_module_enable(void *buf, int size)
{
	pr_info("%s: no define AWINIC_ADSP_ENABLE\n", __func__);

	return 0;
}

static int aw_send_afe_tx_module_enable(void *buf, int size)
{
	return 0;
}
#endif

int aw_send_afe_module_enable(void *buf, int size, uint8_t type)
{
	int ret;

	switch (type) {
	case AW_RX_MODULE:
		ret = aw_send_afe_rx_module_enable(buf, size);
		break;
	case AW_TX_MODULE:
		ret = aw_send_afe_tx_module_enable(buf, size);
		break;
	default:
		pr_err("%s: unsupported type %d\n", __func__, type);
		return -EINVAL;
	}
	return ret;
}

static int aw_get_params_id_by_index(int index, int32_t *params_id,
				     int channel)
{
	pr_info("%s enter, channel = %d\n", __func__, channel);

	if (index > INDEX_PARAMS_ID_MAX || channel > 1) {
		pr_err("%s: error: index is %d, channel %d\n",
		       __func__, index, channel);
		return -EINVAL;
	}
	*params_id = PARAM_ID_INDEX_TABLE[channel][index];
	return 0;
}

static int aw_write_data_to_dsp(int index, void *data, int len, int channel)
{
	int ret;
	int32_t param_id;

	pr_info("%s enter, channel = %d\n", __func__, channel);
	ret = aw_get_params_id_by_index(index, &param_id, channel);
	if (ret < 0)
		return ret;
	mutex_lock(&g_dsp_lock);
	ret = aw_send_afe_cal_apr(param_id, data, len, true);
	if (ret < 0) {
		pr_info("%s: aw_write_data_to_dsp failed\n", __func__);
		mutex_unlock(&g_dsp_lock);
		return ret;
	}
	mutex_unlock(&g_dsp_lock);
	return 0;
}

static int aw_read_data_from_dsp(int index, void *data, int len, int channel)
{
	int ret;
	int32_t param_id;

	pr_info("%s enter, channel = %d\n", __func__, channel);

	ret = aw_get_params_id_by_index(index, &param_id, channel);
	if (ret < 0) {
		pr_info("%s:aw_get_params_id_by_index failed\n", __func__);
		return ret;
	}

	mutex_lock(&g_dsp_lock);
	ret = aw_send_afe_cal_apr(param_id, data, len, false);
	if (ret < 0) {
		pr_info("%s: read data from dsp failed\n", __func__);
		mutex_unlock(&g_dsp_lock);
		return ret;
	}
	mutex_unlock(&g_dsp_lock);

	return 0;
}

int aw_get_dsp_msg_data(char *data_ptr, int data_size, int inline_id,
			int channel)
{
	int32_t cmd_msg[6] = { 0 };
	int ret;

	cmd_msg[0] = DSP_MSG_TYPE_CMD;
	cmd_msg[1] = inline_id;
	cmd_msg[2] = AWINIC_DSP_MSG_HDR_VER;

	pr_info("%s enter\n", __func__);

	mutex_lock(&g_msg_dsp_lock);
	ret = aw_write_data_to_dsp(INDEX_PARAMS_ID_AWDSP_RX_MSG,
				   cmd_msg, sizeof(cmd_msg), channel);
	if (ret < 0) {
		pr_err("%s:inline_id: %d, write cmd to dsp failed\n",
		       __func__, inline_id);
		goto dsp_msg_failed;
	}

	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_AWDSP_RX_MSG,
				    data_ptr, data_size, channel);
	if (ret < 0) {
		pr_err("%s:inline_id: %d, read data from dsp failed\n",
		       __func__, inline_id);
		goto dsp_msg_failed;
	}

	mutex_unlock(&g_msg_dsp_lock);
	return 0;

 dsp_msg_failed:
	mutex_unlock(&g_msg_dsp_lock);
	return ret;
}
EXPORT_SYMBOL(aw_get_dsp_msg_data);

/****************************************************************************
* aw87xxx get battery capacity
*****************************************************************************/
static int aw87xxx_get_battery_capacity(struct aw87xxx *aw87xxx,
					uint32_t *vbat_capacity)
{
	char name[] = "battery";
	int ret;
	union power_supply_propval prop = { 0 };
	struct power_supply *psy = NULL;

	aw_dev_info(aw87xxx->dev, "%s enter, dev_i2c%d@0x%02X\n", __func__,
		    aw87xxx->i2c_seq, aw87xxx->i2c_addr);

	psy = power_supply_get_by_name(name);
	if (psy) {
		ret = power_supply_get_property(psy,
						POWER_SUPPLY_PROP_CAPACITY,
						&prop);
		if (ret < 0) {
			aw_dev_err(aw87xxx->dev,
				   "%s: get vbat capacity failed\n", __func__);
			return -EINVAL;
		}
		*vbat_capacity = prop.intval;
		aw_dev_info(aw87xxx->dev, "%s: The percentage is %d\n",
			    __func__, *vbat_capacity);
	} else {
		aw_dev_err(aw87xxx->dev, "%s: no struct power supply name : %s",
			   __func__, name);
		return -EINVAL;
	}
	return 0;
}

/*****************************************************
 * aw87xxx monitor control
*****************************************************/
void aw87xxx_monitor_stop(struct aw87xxx_monitor *monitor)
{
	struct aw87xxx *aw87xxx = container_of(monitor,
					       struct aw87xxx, monitor);

	aw_dev_info(aw87xxx->dev, "%s enter, dev_i2c%d@0x%02X\n", __func__,
		    aw87xxx->i2c_seq, aw87xxx->i2c_addr);
	cancel_delayed_work(&aw87xxx->monitor.work);

}
EXPORT_SYMBOL(aw87xxx_monitor_stop);

static int aw87xxx_vbat_monitor_update_vmax(struct aw87xxx *aw87xxx,
					    int vbat_vol)
{
	int ret = -1;
	int i = 0;
	uint32_t vmax_flag = 0;
	uint32_t vmax_set = 0;
	uint32_t enable = 0;
	struct aw87xxx_monitor *monitor = NULL;

	monitor = &aw87xxx->monitor;
	aw_dev_info(aw87xxx->dev, "%s enter, dev_i2c%d@0x%02X\n", __func__,
		    aw87xxx->i2c_seq, aw87xxx->i2c_addr);
	aw_dev_info(aw87xxx->dev, "%s: monitor->vmax_cfg->vmax_cfg_num = %d\n",
		    __func__, monitor->vmax_cfg->vmax_cfg_num);
	for (i = 0; i < monitor->vmax_cfg->vmax_cfg_num; i++) {
		if (vbat_vol > monitor->vmax_cfg->vmax_cfg_total[i].min_thr) {
			vmax_set = monitor->vmax_cfg->vmax_cfg_total[i].vmax;
			vmax_flag = 1;
			aw_dev_dbg(aw87xxx->dev,
				   "%s: read setting vmax=0x%x, step[%d]: min_thr=%d\n",
				   __func__, vmax_set, i,
				   monitor->vmax_cfg->
				   vmax_cfg_total[i].min_thr);
			break;
		}
	}
	aw_dev_info(aw87xxx->dev,
		    "%s: vmax_flag = %d, monitor->pre_vmax = 0x%08x, vmax_set = 0x%08x\n",
		    __func__, vmax_flag, monitor->pre_vmax, vmax_set);
	if (vmax_flag) {
		if (monitor->pre_vmax != vmax_set) {
			pr_info("%s:channel = %d\n", __func__,
				aw87xxx->pa_channel);
			ret =
			    aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_ENBALE,
						  &enable, sizeof(uint32_t),
						  aw87xxx->pa_channel);
			if (!enable || ret < 0) {
				aw_dev_err(aw87xxx->dev,
					   "%s: get rx failed or rx disable, ret=%d, enable=%d\n",
					   __func__, ret, enable);
				return -EPERM;
			}

			ret = aw_write_data_to_dsp(INDEX_PARAMS_ID_RX_VMAX,
						   &vmax_set, sizeof(uint32_t),
						   aw87xxx->pa_channel);
			if (ret) {
				aw_dev_err(aw87xxx->dev,
					   "%s: set dsp msg fail, ret=%d\n",
					   __func__, ret);
				return ret;
			} else {
				aw_dev_info(aw87xxx->dev,
					    "%s: set dsp vmax=0x%x sucess\n",
					    __func__, vmax_set);
				monitor->pre_vmax = vmax_set;
			}
		} else {
			aw_dev_info(aw87xxx->dev, "%s:vmax=0x%x no change\n",
				    __func__, vmax_set);
		}
	}
	return 0;
}

void aw87xxx_monitor_work_func(struct work_struct *work)
{
	int ret;
	uint32_t vbat_capacity;
	uint32_t ave_capacity;
	struct aw87xxx *aw87xxx = container_of(work,
					       struct aw87xxx, monitor.work.work);

	aw_dev_info(aw87xxx->dev, "%s enter, dev_i2c%d@0x%02X\n", __func__,
		    aw87xxx->i2c_seq, aw87xxx->i2c_addr);

	ret = aw87xxx_get_battery_capacity(aw87xxx, &vbat_capacity);
	if (ret < 0)
		return;

	if (aw87xxx->monitor.timer_cnt < aw87xxx->monitor.timer_cnt_max) {
		aw87xxx->monitor.timer_cnt++;
		aw87xxx->monitor.vbat_sum += vbat_capacity;
	aw_dev_info(aw87xxx->dev, "%s: timer_cnt = %d\n",
			    __func__, aw87xxx->monitor.timer_cnt);
	}
	if ((aw87xxx->monitor.timer_cnt == aw87xxx->monitor.timer_cnt_max) ||
	    (aw87xxx->monitor.first_entry == AW_FIRST_ENTRY)) {
		if (aw87xxx->monitor.first_entry == AW_FIRST_ENTRY)
			aw87xxx->monitor.first_entry = AW_NOT_FIRST_ENTRY;
		ave_capacity = aw87xxx->monitor.vbat_sum /
		    aw87xxx->monitor.timer_cnt;
		if (aw87xxx->monitor.custom_capacity)
			ave_capacity = aw87xxx->monitor.custom_capacity;
		aw_dev_info(aw87xxx->dev, "%s: get average capacity = %d\n",
			    __func__, ave_capacity);
		aw87xxx_vbat_monitor_update_vmax(aw87xxx, ave_capacity);
		aw87xxx->monitor.timer_cnt = 0;
		aw87xxx->monitor.vbat_sum = 0;
	}
	schedule_delayed_work(&aw87xxx->monitor.work,
				msecs_to_jiffies(aw87xxx->monitor.timer_val));
}
EXPORT_SYMBOL(aw87xxx_monitor_work_func);

/**********************************************************
 * aw87xxx monitor attribute
***********************************************************/
static ssize_t aw87xxx_get_vbat(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"vbat capacity=%d\n", aw87xxx->monitor.custom_capacity);

	return len;
}

static ssize_t aw87xxx_set_vbat(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	int ret = -1;
	uint32_t capacity;

	if (len == 0)
		return 0;
	ret = kstrtouint(buf, 0, &capacity);
	if (ret < 0)
		return ret;
	aw_dev_info(aw87xxx->dev, "%s: set capacity = %d\n",
		    __func__, capacity);
	if (capacity >= AW87XXX_VBAT_CAPACITY_MIN &&
	    capacity <= AW87XXX_VBAT_CAPACITY_MAX)
		aw87xxx->monitor.custom_capacity = capacity;

	return len;
}

static ssize_t aw87xxx_get_vmax(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t vmax_get = 0;
	int ret = -1;

	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_VMAX,
				    &vmax_get, sizeof(uint32_t),
				    aw87xxx->pa_channel);
	if (ret)
		aw_dev_err(aw87xxx->dev, "%s: get dsp vmax fail, ret=%d\n",
			   __func__, ret);
	else
		len += snprintf(buf + len, PAGE_SIZE - len,
				"get_vmax=0x%x\n", vmax_get);

	return len;
}

static ssize_t aw87xxx_set_vmax(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	uint32_t vmax_set = 0;
	int ret = -1;

	if (len == 0)
		return 0;
	ret = kstrtouint(buf, 0, &vmax_set);
	if (ret < 0)
		return ret;
	aw_dev_info(aw87xxx->dev, "%s: vmax_set=%d\n", __func__, vmax_set);
	ret = aw_write_data_to_dsp(INDEX_PARAMS_ID_RX_VMAX,
				   &vmax_set, sizeof(uint32_t),
				   aw87xxx->pa_channel);
	if (ret)
		aw_dev_err(aw87xxx->dev, "%s: send dsp_msg error, ret=%d\n",
			   __func__, ret);
	mdelay(2);
	return len;
}

static ssize_t aw87xxx_get_monitor(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t local_enable;

	local_enable = aw87xxx->monitor.monitor_flag;
	len += snprintf(buf + len, PAGE_SIZE - len,
			"aw87xxx monitor enable: %d\n", local_enable);
	return len;
}

static ssize_t aw87xxx_set_monitor(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	uint32_t enable = 0;
	int ret = -1;

	if (count == 0)
		return 0;
	ret = kstrtouint(buf, 0, &enable);
	if (ret < 0)
		return ret;
	aw_dev_info(aw87xxx->dev, "%s:monitor enable set =%d\n",
		    __func__, enable);
	aw87xxx->monitor.monitor_flag = enable;
	if (aw87xxx->monitor.monitor_flag &&
	    aw87xxx->current_mode != AW87XXX_RCV_MODE &&
	    aw87xxx->monitor.cfg_update_flag == AW87XXX_CFG_OK)
		schedule_delayed_work(&aw87xxx->monitor.work,
					msecs_to_jiffies(aw87xxx->monitor.timer_val));
	return count;
}

static ssize_t aw87xxx_get_rx(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	ssize_t len = 0;
	int ret = -1;
	uint32_t enable;

	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_ENBALE,
				    &enable, sizeof(uint32_t),
				    aw87xxx->pa_channel);
	if (ret)
		aw_dev_err(aw87xxx->dev, "%s: dsp_msg error, ret=%d\n",
			   __func__, ret);
	else
		len += snprintf(buf + len, PAGE_SIZE - len,
				"aw87xxx rx: %d\n", enable);
	return len;
}

static ssize_t aw87xxx_set_rx(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	int ret = -1;
	uint32_t enable;

	ret = kstrtouint(buf, 0, &enable);
	if (ret < 0)
		return ret;
	aw_dev_info(aw87xxx->dev, "%s: set rx enable=%d\n", __func__, enable);
	ret = aw_send_afe_rx_module_enable(&enable, sizeof(uint32_t));
	if (ret)
		aw_dev_err(aw87xxx->dev, "%s: dsp_msg error, ret=%d\n",
			   __func__, ret);
	return count;
}

static ssize_t aw87xxx_set_vmax_time(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	unsigned int timer_val = 0;
	int ret;

	ret = kstrtouint(buf, 0, &timer_val);
	if (ret < 0)
		return ret;
	aw_dev_info(aw87xxx->dev, "%s:timer_val =%d\n", __func__, timer_val);
	aw87xxx->monitor.timer_val = timer_val;
	return count;
}

static ssize_t aw87xxx_get_vmax_time(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"aw87xxx_vmax_timer_val = %d\n",
			aw87xxx->monitor.timer_val);
	return len;
}

static DEVICE_ATTR(vbat, S_IWUSR | S_IRUGO, aw87xxx_get_vbat, aw87xxx_set_vbat);
static DEVICE_ATTR(vmax, S_IWUSR | S_IRUGO, aw87xxx_get_vmax, aw87xxx_set_vmax);
static DEVICE_ATTR(monitor, S_IWUSR | S_IRUGO, aw87xxx_get_monitor,
		   aw87xxx_set_monitor);
static DEVICE_ATTR(rx, S_IWUSR | S_IRUGO, aw87xxx_get_rx, aw87xxx_set_rx);
static DEVICE_ATTR(vmax_time, S_IWUSR | S_IRUGO,
		   aw87xxx_get_vmax_time, aw87xxx_set_vmax_time);

static struct attribute *aw87xxx_monitor_attr[] = {
	&dev_attr_vbat.attr,
	&dev_attr_vmax.attr,
	&dev_attr_monitor.attr,
	&dev_attr_rx.attr,
	&dev_attr_vmax_time.attr,
	NULL
};

static struct attribute_group aw87xxx_monitor_attr_group = {
	.attrs = aw87xxx_monitor_attr
};

/**********************************************************
 * aw87xxx monitor init
***********************************************************/
void aw87xxx_monitor_init(struct aw87xxx_monitor *monitor)
{
	struct aw87xxx *aw87xxx =
	    container_of(monitor, struct aw87xxx, monitor);
	int ret;

	aw_dev_info(aw87xxx->dev, "%s enter, dev_i2c%d@0x%02X\n", __func__,
		    aw87xxx->i2c_seq, aw87xxx->i2c_addr);

	INIT_DELAYED_WORK(&monitor->work, aw87xxx_monitor_work_func);

	ret = sysfs_create_group(&aw87xxx->dev->kobj,
				 &aw87xxx_monitor_attr_group);
	if (ret < 0) {
		aw_dev_err(aw87xxx->dev,
			   "%s: failed to create monitor sysfs nodes\n",
			   __func__);
	}
}
EXPORT_SYMBOL(aw87xxx_monitor_init);

void aw87xxx_parse_monitor_dt(struct aw87xxx_monitor *monitor)
{
	int ret;
	struct aw87xxx *aw87xxx = container_of(monitor,
		struct aw87xxx, monitor);
	struct device_node *np = aw87xxx->dev->of_node;

	aw_dev_info(aw87xxx->dev, "%s enter, dev_i2c%d@0x%02X\n", __func__,
		    aw87xxx->i2c_seq, aw87xxx->i2c_addr);

	ret = of_property_read_u32(np, "monitor-flag", &monitor->monitor_flag);
	if (ret) {
		monitor->monitor_flag = AW87XXX_MONITOR_DEFAULT_FLAG;
		aw_dev_err(aw87xxx->dev,
			   "%s: monitor-flag get failed ,user default value!\n",
			   __func__);
	} else {
		aw_dev_info(aw87xxx->dev, "%s: monitor-flag = %d\n",
			    __func__, monitor->monitor_flag);
	}

	ret = of_property_read_u32(np, "monitor-timer-val",
				   &monitor->timer_val);
	if (ret) {
		monitor->timer_val = AW87XXX_MONITOR_DEFAULT_TIMER_VAL;
		aw_dev_err(aw87xxx->dev,
			   "%s: monitor-timer-val get failed,user default value!\n",
			   __func__);
	} else {
		aw_dev_info(aw87xxx->dev, "%s: monitor-timer-val = %d\n",
			    __func__, monitor->timer_val);
	}

	ret = of_property_read_u32(np, "monitor-timer-count-max",
				   &monitor->timer_cnt_max);
	if (ret) {
		monitor->timer_cnt_max = AW87XXX_MONITOR_DEFAULT_TIMER_COUNT;
		aw_dev_err(aw87xxx->dev,
			   "%s: monitor-timer-count-max get failed,user default config!\n",
			   __func__);
	} else {
		aw_dev_info(aw87xxx->dev, "%s: monitor-timer-count-max = %d\n",
			    __func__, monitor->timer_cnt_max);
	}

}
EXPORT_SYMBOL(aw87xxx_parse_monitor_dt);
