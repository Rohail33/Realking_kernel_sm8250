// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_IC_H__
#define __OPLUS_CHG_IC_H__

#include <linux/cdev.h>
#include "oplus_chg.h"

#define OPLUS_CHG_IC_INIT_RETRY_DELAY	100
#define OPLUS_CHG_IC_INIT_RETRY_MAX	1000
#define OPLUS_CHG_IC_MANU_NAME_MAX	32
#define OPLUS_CHG_IC_FW_ID_MAX		16

struct oplus_chg_ic_dev;

#define OPLUS_CHG_IC_FUNC_DEF(func_id) \
	typedef int (*func_id##_T)

#define oplus_chg_ic_func(ic, func_id, ...) ({				\
	func_id##_T _func = (ic && ic->get_func) ?			\
		ic->get_func(ic, func_id) : NULL;			\
	_func ? _func(ic, ##__VA_ARGS__) : -ENOTSUPP;			\
})

#define OPLUS_CHG_IC_FUNC_CHECK(func_id, func) ({		\
	__always_unused func_id##_T _func = func;		\
	(void *)func;						\
})

#define oplus_print_ic_err(msg)					\
	chg_err("[IC_ERR][%s]-[%s]:%s\n", msg->ic->name,	\
		oplus_chg_ic_err_text(msg->type), msg->msg);

#include "oplus_chg_ic_cfg.h"

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
#define OPLUS_CHG_IC_FUNC_DATA_MAGIC 0X20302009
struct oplus_chg_ic_func_date_item {
	u32 num;
	u32 size;
	u32 str_data;
	u8 buf[0];
};

struct oplus_chg_ic_func_date {
	u32 magic;
	u32 size;
	u32 item_num;
	u8 buf[0];
};

struct oplus_chg_ic_overwrite_data {
	enum oplus_chg_ic_func func_id;
	struct list_head list;
	u32 size;
	u8 buf[0];
};

struct oplus_chg_ic_debug_data {
	enum oplus_chg_ic_func func_id;
	struct list_head overwrite_list;
	struct mutex overwrite_list_lock;
	enum oplus_chg_ic_func *overwrite_funcs;
	int func_num;

	ssize_t (*get_func_data)(struct oplus_chg_ic_dev *, enum oplus_chg_ic_func, void *);
	int (*set_func_data)(struct oplus_chg_ic_dev *, enum oplus_chg_ic_func, const void *, size_t);
};
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

typedef void (*virq_handler_t)(struct oplus_chg_ic_dev *, void *);

struct oplus_chg_ic_virq {
	enum oplus_chg_ic_virq_id virq_id;
	void *virq_data;
	virq_handler_t virq_handler;
};

struct oplus_chg_ic_cfg {
	const char *name;
	enum oplus_chg_ic_type type;
	int index;
	char manu_name[OPLUS_CHG_IC_MANU_NAME_MAX];
	char fw_id[OPLUS_CHG_IC_FW_ID_MAX];
	struct oplus_chg_ic_virq *virq_data;
	int virq_num;
	void *(*get_func)(struct oplus_chg_ic_dev *ic_dev,
			  enum oplus_chg_ic_func func_id);
};

struct oplus_chg_ic_dev {
	const char *name;
	struct device *dev;
	struct oplus_chg_ic_dev *parent;
	enum oplus_chg_ic_type type;
	struct list_head list;
	struct list_head err_list;
	spinlock_t err_list_lock;
	int index;
	char manu_name[32];
	char fw_id[16];

	bool online;
	bool ic_virtual_enable;

	struct oplus_chg_ic_virq *virq_data;
	int virq_num;

	void *(*get_func)(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id);
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	int minor;
	struct cdev cdev;
	struct device *debug_dev;
	char cdev_name[128];
	struct oplus_chg_ic_debug_data debug;
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */
};

#define IC_ERR_MSG_MAX		1024
struct oplus_chg_ic_err_msg {
	struct oplus_chg_ic_dev *ic;
	struct list_head list;
	enum oplus_chg_ic_err type;
	int sub_type;
	char msg[IC_ERR_MSG_MAX];
};

static inline void *oplus_chg_ic_get_drvdata(const struct oplus_chg_ic_dev *ic_dev)
{
	return dev_get_drvdata(ic_dev->dev);
}

void oplus_chg_ic_list_lock(void);
void oplus_chg_ic_list_unlock(void);
struct oplus_chg_ic_dev *oplsu_chg_ic_find_by_name(const char *name);
struct oplus_chg_ic_dev *of_get_oplus_chg_ic(struct device_node *node, const char *prop_name, int index);
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
int oplus_chg_ic_reg_dump(struct oplus_chg_ic_dev *ic_dev);
int oplus_chg_ic_reg_dump_by_name(const char *name);
void oplus_chg_ic_reg_dump_all(void);
#endif
int oplus_chg_ic_virq_register(struct oplus_chg_ic_dev *ic_dev,
			       enum oplus_chg_ic_virq_id virq_id,
			       virq_handler_t handler,
			       void *virq_data);
int oplus_chg_ic_virq_release(struct oplus_chg_ic_dev *ic_dev,
			      enum oplus_chg_ic_virq_id virq_id,
			      void *virq_data);
int oplus_chg_ic_virq_trigger(struct oplus_chg_ic_dev *ic_dev,
			      enum oplus_chg_ic_virq_id virq_id);
int oplus_chg_ic_creat_err_msg(struct oplus_chg_ic_dev *ic_dev,
			       enum oplus_chg_ic_err err_type, int sub_err_type,
			       const char *format, ...);
int oplus_chg_ic_move_err_msg(struct oplus_chg_ic_dev *dest,
			      struct oplus_chg_ic_dev *src);
int oplus_chg_ic_clean_err_msg(struct oplus_chg_ic_dev *ic_dev,
			       struct oplus_chg_ic_err_msg *err_msg);
const char *oplus_chg_ic_err_text(enum oplus_chg_ic_err err_type);
struct oplus_chg_ic_dev *oplus_chg_ic_register(struct device *dev,
	struct oplus_chg_ic_cfg *cfg);
int oplus_chg_ic_unregister(struct oplus_chg_ic_dev *ic_dev);
struct oplus_chg_ic_dev *devm_oplus_chg_ic_register(struct device *dev,
	struct oplus_chg_ic_cfg *cfg);
int devm_oplus_chg_ic_unregister(struct device *dev, struct oplus_chg_ic_dev *ic_dev);

int oplus_chg_ic_func_table_sort(enum oplus_chg_ic_func *func_table, int func_num);
bool oplus_chg_ic_func_is_support(enum oplus_chg_ic_func *func_table, int func_num, enum oplus_chg_ic_func func_id);
bool oplus_chg_ic_virq_is_support(enum oplus_chg_ic_virq_id *virq_table, int virq_num, enum oplus_chg_ic_virq_id virq_id);

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
bool oplus_chg_ic_debug_data_check(const void *buf, size_t len);
int oplus_chg_ic_get_item_num(const void *buf, size_t len);
int oplus_chg_ic_get_item_data(const void *buf, int index);
void *oplus_chg_ic_get_item_data_addr(void *buf, int index);
void oplus_chg_ic_debug_data_init(void *buf, int argc);
int oplus_chg_ic_debug_str_data_init(void *buf, int len);
inline size_t oplus_chg_ic_debug_data_size(int argc);
struct oplus_chg_ic_overwrite_data *
oplus_chg_ic_get_overwrite_data(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id);
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

#endif /* __OPLUS_CHG_IC_H__ */
