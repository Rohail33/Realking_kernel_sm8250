// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[STRATEGY_CGCL]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <oplus_chg.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_comm.h>
#include <oplus_strategy.h>

struct cgcl_strategy_temp_region {
	int index;
	int temp;
};

struct cgcl_strategy_data {
	int32_t cool_temp;
	int32_t heat_temp;
	int32_t curr_data;
	int32_t heat_next_index;
	int32_t cool_next_index;
} __attribute__((packed));

struct cgcl_strategy {
	struct oplus_chg_strategy strategy;
	struct cgcl_strategy_data *data;
	int data_num;
	int temp_min;
	int temp_max;
	int temp_type;
	struct cgcl_strategy_temp_region temp_region;
};

#define CGCL_DATA_SIZE	sizeof(struct cgcl_strategy_data)

static struct oplus_mms *comm_topic;
static struct oplus_mms *gauge_topic;

__maybe_unused static bool is_comm_topic_available()
{
	if (!comm_topic)
		comm_topic = oplus_mms_get_by_name("common");
	return !!comm_topic;
}

__maybe_unused static bool is_gauge_topic_available()
{
	if (!gauge_topic)
		gauge_topic = oplus_mms_get_by_name("gauge");
	return !!gauge_topic;
}

static int cgcl_strategy_get_index(struct cgcl_strategy *strategy, int temp)
{
	int i;

	for (i = 0; i < strategy->data_num; i++) {
		if (temp <= strategy->data[i].heat_temp)
			return i;
	}

	chg_err("Temperature exceeds the highest threshold\n");
	return strategy->data_num - 1;
}

static bool buf_is_zero(unsigned char *buf, size_t size)
{
	while (--size) {
		if (buf[size] != 0)
			return false;
	}

	if (buf[0] != 0)
		return false;
	else
		return true;
}

static int cgcl_strategy_get_data_table_num(unsigned char *buf, size_t size)
{
	struct cgcl_strategy_data *data;
	int i;

	data = (struct cgcl_strategy_data *)buf;
	for (i = 0; i < size / CGCL_DATA_SIZE; i++) {
		if (buf_is_zero((unsigned char *)(&data[i]), CGCL_DATA_SIZE))
			return i;
	}
	return i;
}

static int cgcl_strategy_get_temp(struct cgcl_strategy *cgcl, int *temp)
{
	union mms_msg_data data = { 0 };
	int rc;

	switch (cgcl->temp_type) {
	case STRATEGY_USE_BATT_TEMP:
		if (!is_gauge_topic_available()) {
			chg_err("gauge topic not found\n");
			return -ENODEV;
		}
		rc = oplus_mms_get_item_data(gauge_topic, GAUGE_ITEM_TEMP,
					     &data, false);
		if (rc < 0) {
			chg_err("can't get battery temp, rc=%d\n", rc);
			return rc;
		}

		*temp = data.intval;
		break;
	case STRATEGY_USE_SHELL_TEMP:
		if (!is_comm_topic_available()) {
			chg_err("common topic not found\n");
			return -ENODEV;
		}
		rc = oplus_mms_get_item_data(comm_topic, COMM_ITEM_SHELL_TEMP,
					     &data, false);
		if (rc < 0) {
			chg_err("can't get shell temp, rc=%d\n", rc);
			return rc;
		}

		*temp = data.intval;
		break;
	default:
		chg_err("not support temp type, type=%d\n", cgcl->temp_type);
		return -EINVAL;
	}

	return 0;
}

static struct oplus_chg_strategy *
cgcl_strategy_alloc(unsigned char *buf, size_t size)
{
	struct cgcl_strategy *strategy;
	int data_num;
	size_t data_size = size - sizeof(u32); /* first data is temp type*/

	if (buf == NULL) {
		chg_err("buf is NULL\n");
		return ERR_PTR(-EINVAL);
	}
	if (data_size % CGCL_DATA_SIZE) {
		chg_err("buf size does not meet the requirements, size=%lu\n",
			data_size);
		return ERR_PTR(-EINVAL);
	}
	data_num =
		cgcl_strategy_get_data_table_num(buf + sizeof(u32), data_size);
	if (data_num == 0) {
		chg_err("data num is 0\n");
		return ERR_PTR(-EINVAL);
	}

	strategy = kzalloc(sizeof(struct cgcl_strategy), GFP_KERNEL);
	if (strategy == NULL) {
		chg_err("alloc strategy memory error\n");
		return ERR_PTR(-ENOMEM);
	}
	strategy->temp_type = *((u32 *)buf);
	chg_info("temp_type=%d\n", strategy->temp_type);
	if ((strategy->temp_type != STRATEGY_USE_BATT_TEMP) &&
	    (strategy->temp_type != STRATEGY_USE_SHELL_TEMP)) {
		chg_err("unknown temp type, type=%d\n", strategy->temp_type);
		kfree(strategy);
		return ERR_PTR(-EINVAL);
	}

	strategy->data_num = data_num;
	data_size = strategy->data_num * CGCL_DATA_SIZE;
	strategy->data = kzalloc(data_size, GFP_KERNEL);
	if (strategy == NULL) {
		chg_err("alloc strategy data memory error\n");
		kfree(strategy);
		return ERR_PTR(-ENOMEM);
	}

	memcpy(strategy->data, buf + sizeof(u32), data_size);
	strategy->temp_min = strategy->data[0].cool_temp;
	strategy->temp_max =
		strategy->data[strategy->data_num - 1].heat_temp;

	return &strategy->strategy;
}

static int cgcl_strategy_release(struct oplus_chg_strategy *strategy)
{
	struct cgcl_strategy *cgcl;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	cgcl = (struct cgcl_strategy *)strategy;

	kfree(cgcl->data);

	return 0;
}

static int cgcl_strategy_init(struct oplus_chg_strategy *strategy)
{
	struct cgcl_strategy *cgcl;
	int temp;
	int rc;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}

	cgcl = (struct cgcl_strategy *)strategy;
	rc = cgcl_strategy_get_temp(cgcl, &temp);
	if (rc < 0) {
		chg_err("can't get temp, rc=%d\n", rc);
		return rc;
	}

	cgcl->temp_region.temp = temp;
	cgcl->temp_region.index =
		cgcl_strategy_get_index(cgcl, temp);

	return 0;
}

static int cgcl_strategy_get_data(struct oplus_chg_strategy *strategy, int *ret)
{
	struct cgcl_strategy *cgcl;
	int temp_diff, temp;
	int rc;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	if (ret == NULL) {
		chg_err("ret is NULL\n");
		return -EINVAL;
	}

	cgcl = (struct cgcl_strategy *)strategy;
	rc = cgcl_strategy_get_temp(cgcl, &temp);
	if (rc < 0) {
		chg_err("can't get temp, rc=%d\n", rc);
		return rc;
	}

	temp_diff = temp - cgcl->temp_region.temp;

	while (temp_diff != 0) {
		if (temp_diff > 0 &&
		    temp > cgcl->data[cgcl->temp_region.index].heat_temp) {
			cgcl->temp_region.index =
				cgcl->data[cgcl->temp_region.index]
					.heat_next_index;
			if (temp > cgcl->data[cgcl->temp_region.index]
					    .heat_temp &&
			    cgcl->data[cgcl->temp_region.index].heat_temp <
				    cgcl->temp_max)
				cgcl->temp_region.temp =
					cgcl->data[cgcl->temp_region.index]
						.heat_temp;
			else
				cgcl->temp_region.temp = temp;
		} else if (temp_diff < 0 &&
			   temp <= cgcl->data[cgcl->temp_region.index]
					   .cool_temp) {
			cgcl->temp_region.index =
				cgcl->data[cgcl->temp_region.index]
					.cool_next_index;
			if (temp <= cgcl->data[cgcl->temp_region.index]
					    .cool_temp &&
			    cgcl->data[cgcl->temp_region.index].cool_temp >
				    cgcl->temp_max)
				cgcl->temp_region.temp =
					cgcl->data[cgcl->temp_region.index]
						.heat_temp;
			else
				cgcl->temp_region.temp = temp;
		} else {
			cgcl->temp_region.temp = temp;
		}
		temp_diff = temp - cgcl->temp_region.temp;
	}
	*ret = cgcl->data[cgcl->temp_region.index].curr_data;

	chg_info("ret=%d, index=%d, temp=%d\n",
		 *ret, cgcl->temp_region.index, temp);

	return 0;
}

static struct oplus_chg_strategy_desc cgcl_strategy_desc = {
	.name = "cgcl",
	.strategy_init = cgcl_strategy_init,
	.strategy_release = cgcl_strategy_release,
	.strategy_alloc = cgcl_strategy_alloc,
	.strategy_get_data = cgcl_strategy_get_data,
};

int cgcl_strategy_register(void)
{
	return oplus_chg_strategy_register(&cgcl_strategy_desc);
}
