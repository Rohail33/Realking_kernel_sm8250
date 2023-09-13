// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/iio/consumer.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched/clock.h>
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
#include <soc/oplus/system/kernel_fb.h>
#endif
#include "haptic_feedback.h"

static struct oplus_haptic_track *g_haptic_track_chip;

static bool oplus_haptic_event_record_time_need_update(void)
{
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct timespec now = {0};
	struct timespec last_record;
	long time_step;

	if (!chip)
		return false;

	getnstimeofday(&now);
	last_record = chip->lastest_record;

	time_step = (now.tv_sec - last_record.tv_sec) / SECSONDS_PER_HOUR;
	if (time_step >= UPLOAD_TIME_LIMIT_HOURS) {
		chip->lastest_record = now;
		return true;
	}

	return false;
}

static void oplus_haptic_event_queue_update(void) {
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct haptic_dev_track_event *dev_event;
	struct haptic_mem_alloc_track_event *mem_event;
	bool update_flag = false;

	if (!chip)
		return;

	update_flag = oplus_haptic_event_record_time_need_update();
	if (update_flag == false)
		return;

	/* clear dev_event_que */
	dev_event = &chip->dev_track_event;
	memset(dev_event->dev_event_que, 0, sizeof(struct haptic_dev_event_info) * MAX_DEV_EVENT_QUEUE_LEN);
	dev_event->que_front = 0;
	dev_event->que_rear = 0;

	/* clear mem_event_que */
	mem_event = &chip->mem_alloc_track_event;
	memset(mem_event->mem_event_que, 0, sizeof(struct haptic_mem_alloc_event_info) * MAX_MEM_ALLOC_EVENT_QUEUE_LEN);
	mem_event->que_front = 0;
	mem_event->que_rear = 0;
}

static struct haptic_fb_info g_haptic_fb_table[] = {
	{HAPTIC_I2C_READ_TRACK_ERR, "i2c_read_err", HAPTIC_TRACK_EVENT_DEVICE_ERR},
	{HAPTIC_I2C_WRITE_TRACK_ERR, "i2c_write_err", HAPTIC_TRACK_EVENT_DEVICE_ERR},

	{HAPTIC_F0_CALI_TRACK, "f0_cali_err", HAPTIC_TRACK_EVENT_FRE_CALI_ERR},
	{HAPTIC_OSC_CALI_TRACK, "osc_cali_err", HAPTIC_TRACK_EVENT_FRE_CALI_ERR},

	{HAPTIC_MEM_ALLOC_TRACK, "mem_alloc_err", HAPTIC_TRACK_EVENT_MEM_ALLOC_ERR},

	{HAPTIC_SPMI_READ_TRACK_ERR, "spmi_read_err", HAPTIC_TRACK_EVENT_DEVICE_ERR},
	{HAPTIC_SPMI_WRITE_TRACK_ERR, "spmi_write_err", HAPTIC_TRACK_EVENT_DEVICE_ERR},
};

static int oplus_haptic_event_payload_pack(struct haptic_fb_detail *fb_info)
{
	int i = 0;
	char *fb_event_field = NULL;
	char *fb_event_desc = NULL;
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	char *log_tag = HAPTIC_EVENT_TAG;
	char *event_id = HAPTIC_EVENT_ID;
	int len;

	if ((!chip) || (!chip->dcs_info) || (!fb_info))
		return TRACK_CMD_ERROR_CHIP_NULL;

	for (i = 0; i < ARRAY_SIZE(g_haptic_fb_table); i++) {
		if (g_haptic_fb_table[i].fb_event_type == fb_info->track_type) {
			fb_event_desc = g_haptic_fb_table[i].fb_event_desc;
			fb_event_field = g_haptic_fb_table[i].fb_event_field;
			break;
		}
	}

	if (i == ARRAY_SIZE(g_haptic_fb_table)) {
		haptic_fb_err("%s: invalid fb_event_type \n", __func__);
		return TRACK_CMD_ERROR_DATA_INVALID;
	}

	memset(chip->dcs_info, 0x0, OPLUS_HSPTIC_TRIGGER_MSG_LEN);
	snprintf(chip->dcs_info->payload, MAX_PAYLOAD_LEN,
		 "NULL$$EventField@@%s$$FieldData@@%s$$detailData@@%s",
		 fb_event_field,
		 fb_event_desc,
		 fb_info->detailData);
	chip->dcs_info->payload[MAX_PAYLOAD_LEN - 1] = '\0';
	len = strlen(chip->dcs_info->payload);

	chip->dcs_info->type = 1;
	strncpy(chip->dcs_info->log_tag, log_tag, MAX_HAPTIC_EVENT_TAG_LEN);
	chip->dcs_info->log_tag[MAX_HAPTIC_EVENT_TAG_LEN - 1] = '\0';
	strncpy(chip->dcs_info->event_id, event_id, MAX_HAPTIC_EVENT_ID_LEN);
	chip->dcs_info->event_id[MAX_HAPTIC_EVENT_ID_LEN - 1] = '\0';
	chip->dcs_info->payload_length = len + 1;

	return TRACK_CMD_ACK_OK;
}

static int oplus_haptic_track_upload_trigger_data(struct haptic_fb_detail *fb_detail)
{
	int rc = 0;
	struct oplus_haptic_track *chip = g_haptic_track_chip;

	if (!chip)
		return TRACK_CMD_ERROR_CHIP_NULL;

	if (!fb_detail) {
		haptic_fb_err("%s:dev_event is null \n", __func__);
		return TRACK_CMD_ERROR_CHIP_NULL;
	}

	mutex_lock(&chip->trigger_ack_lock);
	mutex_lock(&chip->payload_lock);
	mutex_lock(&chip->trigger_data_lock);

	rc = oplus_haptic_event_payload_pack(fb_detail);
	if (rc) {
		haptic_fb_err("%s: oplus_payload_pack err \n", __func__);
		mutex_unlock(&chip->trigger_data_lock);
		mutex_unlock(&chip->payload_lock);
		mutex_unlock(&chip->trigger_ack_lock);
		return rc;
	}
	chip->trigger_data_ok = true;
	mutex_unlock(&chip->trigger_data_lock);

	reinit_completion(&chip->trigger_ack);
	wake_up(&chip->upload_wq);

	rc = wait_for_completion_timeout(
		&chip->trigger_ack,
		msecs_to_jiffies(OPLUS_HAPTIC_TRACK_WAIT_TIME_MS));

	if (!rc) {
		if (delayed_work_pending(&chip->upload_info_dwork))
			cancel_delayed_work_sync(&chip->upload_info_dwork);
		pr_err("Error, timed_out_upload_trigger_data \r\n");
		mutex_unlock(&chip->payload_lock);
		mutex_unlock(&chip->trigger_ack_lock);
		return TRACK_CMD_ERROR_TIME_OUT;
	}

	mutex_unlock(&chip->payload_lock);
	mutex_unlock(&chip->trigger_ack_lock);
	haptic_fb_info("success\n");

	return TRACK_CMD_ACK_OK;
}

int oplus_haptic_track_dev_err(uint32_t track_type, uint32_t reg_addr, uint32_t err_code)
{
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct haptic_dev_track_event *track_event;
	uint32_t que_rear;

	if (!chip)
		return TRACK_CMD_ERROR_CHIP_NULL;

	oplus_haptic_event_queue_update();
	track_event = &chip->dev_track_event;
	que_rear = track_event->que_rear;

	if (que_rear >= MAX_DEV_EVENT_QUEUE_LEN)
		return TRACK_CMD_ERROR_QUEUE_FULL;

	track_event->dev_event_que[que_rear].track_type = track_type;
	track_event->dev_event_que[que_rear].reg_addr = reg_addr;
	track_event->dev_event_que[que_rear].err_code = err_code;
	track_event->que_rear++;

	schedule_delayed_work(&track_event->track_dev_err_load_trigger_work, 0);

	return TRACK_CMD_ACK_OK;
}
EXPORT_SYMBOL(oplus_haptic_track_dev_err);

int oplus_haptic_track_fre_cail(uint32_t track_type, uint32_t cali_data, uint32_t result_flag, char *fail_info)
{
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct haptic_fre_cail_track_event *track_event;

	if (!chip)
		return TRACK_CMD_ERROR_CHIP_NULL;

	track_event = &chip->fre_cail_track_event;
	memset(track_event->fre_event.fail_info, 0, sizeof(char) * MAX_FAIL_INFO_LEN);

	track_event->fre_event.track_type = track_type;
	track_event->fre_event.cali_data = cali_data;
	track_event->fre_event.result_flag = result_flag;
	strncpy(track_event->fre_event.fail_info, fail_info, MAX_FAIL_INFO_LEN - 1);
	track_event->fre_event.fail_info[MAX_FAIL_INFO_LEN - 1] = '\0';

	schedule_delayed_work(&track_event->track_fre_cail_load_trigger_work, 0);

	return TRACK_CMD_ACK_OK;
}
EXPORT_SYMBOL(oplus_haptic_track_fre_cail);

int oplus_haptic_track_mem_alloc_err(uint32_t track_type, uint32_t alloc_len, const char *fun_name)
{
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct haptic_mem_alloc_track_event *track_event;
	uint32_t que_rear;

	if (!chip)
		return TRACK_CMD_ERROR_CHIP_NULL;

	oplus_haptic_event_queue_update();
	track_event = &chip->mem_alloc_track_event;
	que_rear = track_event->que_rear;

	if (que_rear >= MAX_MEM_ALLOC_EVENT_QUEUE_LEN)
		return TRACK_CMD_ERROR_QUEUE_FULL;

	track_event->mem_event_que[que_rear].track_type = track_type;
	track_event->mem_event_que[que_rear].alloc_len = alloc_len;
	strncpy(track_event->mem_event_que[que_rear].fun_name, fun_name, MAX_FUN_NAME_LEN - 1);
	track_event->mem_event_que[que_rear].fun_name[MAX_FUN_NAME_LEN - 1] = '\0';
	track_event->que_rear++;

	schedule_delayed_work(&track_event->track_mem_alloc_err_load_trigger_work, 0);

	return TRACK_CMD_ACK_OK;
}
EXPORT_SYMBOL(oplus_haptic_track_mem_alloc_err);

static void oplus_haptic_track_dev_err_load_trigger_work(struct work_struct *work)
{
	uint32_t que_front;
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct delayed_work *dwork = to_delayed_work(work);
	struct haptic_dev_track_event *dev_event =
		container_of(dwork, struct haptic_dev_track_event,
		track_dev_err_load_trigger_work);
	struct haptic_fb_detail *fb_detail;

	if ((!chip) || (!dev_event)) {
		haptic_fb_err("%s:g_haptic_track_chip is null  \n", __func__);
		return;
	}

	que_front = dev_event->que_front;
	if (que_front >= dev_event->que_rear)
		return;

	fb_detail = &dev_event->dev_detail_data;
	fb_detail->track_type = dev_event->dev_event_que[que_front].track_type;
	memset(fb_detail->detailData, 0, MAX_DETAIL_INFO_LEN);
	snprintf(fb_detail->detailData, MAX_DETAIL_INFO_LEN,
		"reg_addr:0x%x,err_code:%u", dev_event->dev_event_que[que_front].reg_addr,
		dev_event->dev_event_que[que_front].err_code);

	dev_event->que_front++;
	oplus_haptic_track_upload_trigger_data(fb_detail);
}

static void oplus_haptic_track_fre_cail_load_trigger_work(struct work_struct *work)
{
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct delayed_work *dwork = to_delayed_work(work);
	struct haptic_fre_cail_track_event *cail_event =
		container_of(dwork, struct haptic_fre_cail_track_event,
		track_fre_cail_load_trigger_work);
	struct haptic_fb_detail *fb_detail;

	if ((!chip) || (!cail_event)) {
		haptic_fb_err("%s:g_haptic_track_chip is null  \n", __func__);
		return;
	}

	fb_detail = &cail_event->fre_cali_detail;
	fb_detail->track_type = cail_event->fre_event.track_type;
	memset(fb_detail->detailData, 0, MAX_DETAIL_INFO_LEN);
	snprintf(fb_detail->detailData, MAX_DETAIL_INFO_LEN,
		"cali_data:%u,cali_ret:0x%x,fail_info:%s", cail_event->fre_event.cali_data,
		cail_event->fre_event.result_flag, cail_event->fre_event.fail_info);

	oplus_haptic_track_upload_trigger_data(fb_detail);
}

static void oplus_haptic_track_mem_alloc_load_trigger_work(struct work_struct *work)
{
	uint32_t que_front;
	struct oplus_haptic_track *chip = g_haptic_track_chip;
	struct delayed_work *dwork = to_delayed_work(work);
	struct haptic_mem_alloc_track_event *mem_event =
		container_of(dwork, struct haptic_mem_alloc_track_event,
		track_mem_alloc_err_load_trigger_work);
	struct haptic_fb_detail *fb_detail;

	if ((!chip) || (!mem_event)) {
		haptic_fb_err("%s:g_haptic_track_chip is null  \n", __func__);
		return;
	}

	que_front = mem_event->que_front;
	if (que_front >= mem_event->que_rear)
		return;

	fb_detail = &mem_event->mem_detail;
	fb_detail->track_type = mem_event->mem_event_que[que_front].track_type;
	memset(fb_detail->detailData, 0, MAX_DETAIL_INFO_LEN);
	snprintf(fb_detail->detailData, MAX_DETAIL_INFO_LEN,
			"alloc_len:0x%x,fun_name:%s", mem_event->mem_event_que[que_front].alloc_len,
			mem_event->mem_event_que[que_front].fun_name);

	(mem_event->que_front)++;
	oplus_haptic_track_upload_trigger_data(fb_detail);
}

static void oplus_haptic_track_upload_info_dwork(struct work_struct *work)
{
	int ret = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_haptic_track *chip =
		container_of(dwork, struct oplus_haptic_track, upload_info_dwork);

	if ((!chip) || (!chip->dcs_info))
		return;

#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
	ret = fb_kevent_send_to_user(chip->dcs_info);
#endif
	if (!ret) {
		complete(&chip->trigger_ack);
	} else if (chip->fb_retry_cnt > 0) {
		queue_delayed_work(chip->trigger_upload_wq, &chip->upload_info_dwork,
				   msecs_to_jiffies(OPLUS_HAPTIC_UPDATE_INFO_DELAY_MS));
	}

	haptic_fb_info("retry_cnt: %d, ret = %d\n", chip->fb_retry_cnt, ret);
	chip->fb_retry_cnt--;
}

static int oplus_haptic_track_thread(void *data)
{
	int rc = 0;
	struct oplus_haptic_track *chip = (struct oplus_haptic_track *)data;

	if (!chip)
		return -1;

	haptic_fb_info("start\n");
	while (!kthread_should_stop()) {
		mutex_lock(&chip->upload_lock);
		rc = wait_event_interruptible(chip->upload_wq,
					      chip->trigger_data_ok);
		mutex_unlock(&chip->upload_lock);
		if (rc)
			return rc;
		if (!chip->trigger_data_ok)
			haptic_fb_err("oplus haptic track false wakeup, rc=%d\n", rc);
		mutex_lock(&chip->trigger_data_lock);
		chip->trigger_data_ok = false;
		chip->fb_retry_cnt = OPLUS_HAPTIC_FB_RETRY_TIME;

		queue_delayed_work(chip->trigger_upload_wq,
				   &chip->upload_info_dwork, 0);

		mutex_unlock(&chip->trigger_data_lock);
	}

	return rc;
}

static void oplus_haptic_track_init(struct oplus_haptic_track *track_dev)
{
	struct oplus_haptic_track *chip = track_dev;
	struct haptic_dev_track_event *dev_event;
	struct haptic_fre_cail_track_event *fre_cail_event;
	struct haptic_mem_alloc_track_event *mem_alloc_event;

	chip->trigger_data_ok = false;
	mutex_init(&chip->upload_lock);
	mutex_init(&chip->trigger_data_lock);
	mutex_init(&chip->trigger_ack_lock);
	init_waitqueue_head(&chip->upload_wq);
	init_completion(&chip->trigger_ack);
	mutex_init(&chip->payload_lock);

	/* init timespec */
	getnstimeofday(&track_dev->lastest_record);

	/* event track init */
	dev_event = &(track_dev->dev_track_event);
	INIT_DELAYED_WORK(&dev_event->track_dev_err_load_trigger_work,
			  oplus_haptic_track_dev_err_load_trigger_work);
	dev_event->que_front = 0;
	dev_event->que_rear = 0;

	/* fre cali track init */
	fre_cail_event = &(track_dev->fre_cail_track_event);
	INIT_DELAYED_WORK(&fre_cail_event->track_fre_cail_load_trigger_work,
	    oplus_haptic_track_fre_cail_load_trigger_work);

	/* mem alloc track init */
	mem_alloc_event = &(track_dev->mem_alloc_track_event);
	INIT_DELAYED_WORK(&mem_alloc_event->track_mem_alloc_err_load_trigger_work,
			  oplus_haptic_track_mem_alloc_load_trigger_work);
	mem_alloc_event->que_front = 0;
	mem_alloc_event->que_rear = 0;
}

/* debug node */
static struct haptic_dev_event_info event_info_node;
static struct haptic_fre_cail_event_info fre_cail_node;
static struct haptic_mem_alloc_event_info mem_alloc_node;

static ssize_t dev_event_track_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return oplus_haptic_track_dev_err(event_info_node.track_type, event_info_node.reg_addr, event_info_node.err_code);
}

static ssize_t dev_event_track_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t len)
{
	uint32_t track_type = 0;
	uint32_t reg_addr = 0;
	uint32_t err_code = 0;

	if (sscanf(buf, "%d %x %d", &track_type, &reg_addr, &err_code) == 3) {
		if (track_type >= HAPTIC_TRACK_TYPE_MAX) {
			haptic_fb_err("%s: first value out of range!\n", __func__);
			return len;
		}
		event_info_node.track_type = track_type;
		event_info_node.reg_addr = reg_addr;
		event_info_node.err_code = err_code;
	}

	return len;
}

static ssize_t dev_fre_cail_track_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return oplus_haptic_track_fre_cail(fre_cail_node.track_type, fre_cail_node.cali_data,
					   fre_cail_node.result_flag, fre_cail_node.fail_info);
}

static ssize_t dev_fre_cail_track_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t len)
{
	uint32_t track_type = 0;
	uint32_t cali_data = 0;
	uint32_t result_flag = 0;	/* 0 is success, 1 is fail */
	char fail_info[MAX_FAIL_INFO_LEN];

	if (sscanf(buf, "%d %d %d %127s", &track_type, &cali_data, &result_flag, &fail_info[0]) == 4) {
		if (track_type >= HAPTIC_TRACK_TYPE_MAX) {
			haptic_fb_err("%s: first value out of range!\n", __func__);
			return len;
		}
		fre_cail_node.track_type = track_type;
		fre_cail_node.cali_data = cali_data;
		fre_cail_node.result_flag = result_flag;
		memset(fre_cail_node.fail_info, 0, MAX_FAIL_INFO_LEN);
		strncpy(fre_cail_node.fail_info, fail_info, MAX_FAIL_INFO_LEN - 1);
		fre_cail_node.fail_info[MAX_FAIL_INFO_LEN - 1] = '\0';
	}

	return len;
}

static ssize_t dev_mem_alloc_track_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return oplus_haptic_track_mem_alloc_err(mem_alloc_node.track_type, mem_alloc_node.alloc_len, __func__);
}

static ssize_t dev_mem_alloc_track_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t len)
{
	uint32_t track_type = 0;
	uint32_t alloc_len = 0;

	if (sscanf(buf, "%d %x", &track_type, &alloc_len) == 2) {
		if (track_type >= HAPTIC_TRACK_TYPE_MAX) {
			haptic_fb_err("%s: first value out of range!\n", __func__);
			return len;
		}
		mem_alloc_node.track_type = track_type;
		mem_alloc_node.alloc_len = alloc_len;
	}

	return len;
}

static DEVICE_ATTR(event_track, 0664, dev_event_track_show,
		   dev_event_track_store);
static DEVICE_ATTR(fre_cali_track, 0664, dev_fre_cail_track_show,
		   dev_fre_cail_track_store);
static DEVICE_ATTR(mem_alloc_track, 0664, dev_mem_alloc_track_show,
		   dev_mem_alloc_track_store);

static struct attribute *haptic_fb_attributes[] = {
	&dev_attr_event_track.attr,
	&dev_attr_fre_cali_track.attr,
	&dev_attr_mem_alloc_track.attr,
	NULL,
};

static struct attribute_group haptic_fb_attributes_group = {
	.attrs = haptic_fb_attributes
};


static struct file_operations mytest_ops = {
	.owner = THIS_MODULE,
};

static int major;
static struct class *cls;

static int oplus_haptic_feedback_probe(struct platform_device *pdev)
{
	int ret;
	struct oplus_haptic_track *hapric_track;
	/* debug_node */
	struct device *mydev;

	major = register_chrdev(0, "haptic_fb", &mytest_ops);
	cls = class_create(THIS_MODULE, "haptic_fb_class");
	mydev = device_create(cls, 0, MKDEV(major, 0), NULL, "haptic_fb_device");
	ret = sysfs_create_group(&(mydev->kobj), &haptic_fb_attributes_group);
	if (ret < 0) {
		haptic_fb_err("%s: error creating sysfs attr files\n", __func__);
		return ret;
	}

	hapric_track = devm_kzalloc(&pdev->dev, sizeof(struct oplus_haptic_track), GFP_KERNEL);
	if (!hapric_track) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}

	hapric_track->dcs_info = (struct kernel_packet_info *)devm_kzalloc(&pdev->dev,
		sizeof(char) * OPLUS_HSPTIC_TRIGGER_MSG_LEN, GFP_KERNEL);
	if (!hapric_track->dcs_info) {
		ret = -ENOMEM;
		goto dcs_info_kmalloc_fail;
	}

	hapric_track->dev = &pdev->dev;
	platform_set_drvdata(pdev, hapric_track);

	oplus_haptic_track_init(hapric_track);

	/* creat feedback monitor thread */
	hapric_track->track_upload_kthread = kthread_run(oplus_haptic_track_thread, (void *)hapric_track,
							"track_upload_kthread");
	if (IS_ERR(hapric_track->track_upload_kthread)) {
		pr_err("failed to create oplus_haptic_track_thread\n");
		ret = -EINVAL;
		goto track_kthread_init_err;
	}

	INIT_DELAYED_WORK(&hapric_track->upload_info_dwork,
			  oplus_haptic_track_upload_info_dwork);

	hapric_track->trigger_upload_wq = create_workqueue("haptic_chg_trigger_upload_wq");
	g_haptic_track_chip = hapric_track;
	pr_info("probe done\n");

	return 0;
track_kthread_init_err:
	devm_kfree(&pdev->dev, hapric_track->dcs_info);
dcs_info_kmalloc_fail:
	devm_kfree(&pdev->dev, hapric_track);
	return ret;
}

static int oplus_haptic_feedback_remove(struct platform_device *pdev)
{
	struct oplus_haptic_track *hapric_track = platform_get_drvdata(pdev);

	device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);
	unregister_chrdev(major, "haptic_fb");

	mutex_destroy(&hapric_track->upload_lock);
	mutex_destroy(&hapric_track->trigger_data_lock);
	mutex_destroy(&hapric_track->trigger_ack_lock);
	mutex_destroy(&hapric_track->payload_lock);
	cancel_delayed_work_sync(&hapric_track->dev_track_event.track_dev_err_load_trigger_work);
	cancel_delayed_work_sync(&hapric_track->fre_cail_track_event.track_fre_cail_load_trigger_work);
	cancel_delayed_work_sync(&hapric_track->mem_alloc_track_event.track_mem_alloc_err_load_trigger_work);
	devm_kfree(&pdev->dev, hapric_track->dcs_info);
	devm_kfree(&pdev->dev, hapric_track);
	return 0;
}

static const struct of_device_id oplus_haptic_drv_match[] = {
	{.compatible = "oplus,haptic-feedback"},
	{},
};

static struct platform_driver oplus_haptic_feedback_driver = {
	.driver =
	{
		.name = "haptic_feedback",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_haptic_drv_match),
	},
	.probe      = oplus_haptic_feedback_probe,
	.remove     = oplus_haptic_feedback_remove,
};

static int __init haptic_feedback_init(void)
{
	pr_info("sensor_feedback_init call\n");

	return platform_driver_register(&oplus_haptic_feedback_driver);
}

static void __exit haptic_feedback_exit(void)
{
	pr_info("sensor_feedback_exit call\n");

	platform_driver_unregister(&oplus_haptic_feedback_driver);
}

module_init(haptic_feedback_init);
module_exit(haptic_feedback_exit);

MODULE_LICENSE("GPL v2");
