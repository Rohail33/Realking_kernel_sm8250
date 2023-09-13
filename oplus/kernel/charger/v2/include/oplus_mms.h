// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#ifndef __OPLUS_MMS_H__
#define __OPLUS_MMS_H__

#include <linux/spinlock.h>
#include <oplus_chg_ic.h>

#define TOPIC_NAME_MAX		128
#define TOPIC_MSG_STR_BUF	1024

#define TOPIC_SUBS_RETRY_DELAY	100
#define TOPIC_SUBS_RETRY_MAX	50000

enum oplus_mms_type {
	OPLUS_MMS_TYPE_UNKNOWN,
	OPLUS_MMS_TYPE_ERROR,
	OPLUS_MMS_TYPE_GAUGE,
	OPLUS_MMS_TYPE_BATTERY,
	OPLUS_MMS_TYPE_USB,
	OPLUS_MMS_TYPE_WLS,
	OPLUS_MMS_TYPE_TEMP,
	OPLUS_MMS_TYPE_VOOC,
	OPLUS_MMS_TYPE_AIRVOOC,
	OPLUS_MMS_TYPE_COMM,
};

enum mms_msg_type {
	MSG_TYPE_TIMER,
	MSG_TYPE_ITEM,
};

enum mms_msg_prio {
	MSG_PRIO_HIGH,
	MSG_PRIO_MEDIUM,
	MSG_PRIO_LOW,
};

struct oplus_mms;

union mms_msg_data {
	int intval;
	char *strval;
};

struct mms_item_desc {
	u32 item_id;
	bool str_data;
	bool up_thr_enable;
	bool down_thr_enable;
	bool dead_thr_enable;
	int update_thr_up;
	int update_thr_down;
	int dead_zone_thr;
	int (*update)(struct oplus_mms *, union mms_msg_data *);
};

struct mms_item {
	struct mms_item_desc desc;
	bool updated;
	rwlock_t lock;
	union mms_msg_data data;
	union mms_msg_data pre_data;
};

enum mms_msg_payload {
	MSG_LOAD_NULL,
	MSG_LOAD_INT,
	MSG_LOAD_STR,
};

struct mms_msg {
	enum mms_msg_type type;
	enum mms_msg_prio prio;
	u32 item_id;
	struct list_head list;
	struct completion ack;
	enum mms_msg_payload payload;
	bool sync;
	u8 buf[0];
};

struct mms_subscribe {
	char name[TOPIC_NAME_MAX];
	struct oplus_mms *mms;
	void *priv_data;
	struct list_head list;
	struct list_head callback_list;
	void (*callback)(struct mms_subscribe *, enum mms_msg_type, u32);
};

struct oplus_mms_config {
	struct device_node *of_node;
	struct fwnode_handle *fwnode;

	/* Driver private data */
	void *drv_data;

	int update_interval;
	/* Device specific sysfs attributes */
	const struct attribute_group **attr_grp;
};

struct oplus_mms_desc {
	const char *name;
	enum oplus_mms_type type;
	struct mms_item *item_table;
	int item_num;
	const u32 *update_items;
	int update_items_num;
	int update_interval;
	void (*update)(struct oplus_mms *, bool publish);
};

struct oplus_mms {
	const struct oplus_mms_desc *desc;
	int static_update_interval;
	int normal_update_interval;
	struct list_head subscribe_list;
	struct list_head msg_list;
	spinlock_t subscribe_lock;
	struct mutex msg_lock;
	struct delayed_work update_work;
	struct delayed_work msg_work;

	struct device_node *of_node;
	void *drv_data;
	struct device dev;
	spinlock_t changed_lock;
	bool changed;
	bool initialized;
	bool removing;
	atomic_t use_cnt;

#ifdef CONFIG_OPLUS_CHG_MMS_DEBUG
	u32 debug_item_id;
	struct mms_subscribe *debug_subs;
#endif /* CONFIG_OPLUS_CHG_MMS_DEBUG */
};

typedef void (*mms_callback_t)(struct oplus_mms *topic, void *data);

#define to_oplus_mms(device) container_of(device, struct oplus_mms, dev)

static inline int oplus_mms_update_interval(struct oplus_mms *mms)
{
	if (!mms)
		return 0;
	return mms->normal_update_interval;
}

bool oplus_mms_item_is_str(struct oplus_mms *mms, u32 id);
int oplus_mms_get_item_data(struct oplus_mms *mms, u32 item_id,
			    union mms_msg_data *data, bool update);
bool oplus_mms_item_update(struct oplus_mms *mms, u32 item_id, bool check_update);
void oplus_mms_topic_update(struct oplus_mms *topic, bool publish);
struct mms_msg *oplus_mms_alloc_msg(enum mms_msg_type type,
				    enum mms_msg_prio prio, u32 item_id);
struct mms_msg *oplus_mms_alloc_int_msg(enum mms_msg_type type,
					enum mms_msg_prio prio, u32 item_id,
					int data);
struct mms_msg *oplus_mms_alloc_str_msg(enum mms_msg_type type,
					enum mms_msg_prio prio, u32 item_id,
					const char *format, ...);
int oplus_mms_publish_msg(struct oplus_mms *mms, struct mms_msg *msg);
int oplus_mms_publish_msg_sync(struct oplus_mms *mms, struct mms_msg *msg);
int oplus_mms_publish_ic_err_msg(struct oplus_mms *topic, u32 item_id,
				 struct oplus_chg_ic_err_msg *err_msg);
int oplus_mms_analysis_ic_err_msg(char *buf, size_t buf_size, int *name_index,
				  int *type, int *sub_type, int *msg_index);
struct mms_subscribe *oplus_mms_subscribe(
	struct oplus_mms *mms, void *priv_data,
	void (*callback)(struct mms_subscribe *, enum mms_msg_type, u32),
	const char *format, ...);
int oplus_mms_unsubscribe(struct mms_subscribe *subs);
int oplus_mms_wait_topic(const char *name, mms_callback_t call, void *data);
int oplus_mms_set_publish_interval(struct oplus_mms *mms, int time_ms);
int oplus_mms_stop_publish(struct oplus_mms *mms);
int oplus_mms_restore_publish(struct oplus_mms *mms);
struct oplus_mms *oplus_mms_get_by_name(const char *name);
void oplus_mms_put(struct oplus_mms *mms);
struct oplus_mms *__must_check oplus_mms_register(struct device *parent,
		const struct oplus_mms_desc *desc,
		const struct oplus_mms_config *cfg);
struct oplus_mms *__must_check
oplus_mms_register_no_ws(struct device *parent,
		const struct oplus_mms_desc *desc,
		const struct oplus_mms_config *cfg);
struct oplus_mms *__must_check
devm_oplus_mms_register(struct device *parent,
		const struct oplus_mms_desc *desc,
		const struct oplus_mms_config *cfg);
struct oplus_mms *__must_check
devm_oplus_mms_register_no_ws(struct device *parent,
		const struct oplus_mms_desc *desc,
		const struct oplus_mms_config *cfg);
void oplus_mms_unregister(struct oplus_mms *mms);
void *oplus_mms_get_drvdata(struct oplus_mms *mms);

#endif /* __OPLUS_MMS_H__ */
