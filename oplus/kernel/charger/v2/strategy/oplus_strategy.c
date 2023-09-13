// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[STRATEGY]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/device.h>
#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_strategy.h>

static DEFINE_MUTEX(list_lock);
static LIST_HEAD(strategy_list);

static struct oplus_chg_strategy_desc *strategy_desc_find_by_name(const char *name)
{
	struct oplus_chg_strategy_desc *desc;

	if (name == NULL) {
		chg_err("name is NULL\n");
		return NULL;
	}

	mutex_lock(&list_lock);
	list_for_each_entry(desc, &strategy_list, list) {
		if (desc->name == NULL)
			continue;
		if (strcmp(desc->name, name) == 0) {
			mutex_unlock(&list_lock);
			return desc;
		}
	}
	mutex_unlock(&list_lock);

	return NULL;
}

struct oplus_chg_strategy *
oplus_chg_strategy_alloc(const char *name, unsigned char *buf, size_t size)
{
	struct oplus_chg_strategy *strategy;
	struct oplus_chg_strategy_desc *desc;

	if (name == NULL) {
		chg_err("name is NULL\n");
		return NULL;
	}
	if (buf == NULL) {
		chg_err("buf is NULL\n");
		return NULL;
	}

	desc = strategy_desc_find_by_name(name);
	if (desc == NULL) {
		chg_err("No strategy with name %s was found\n", name);
		return NULL;
	}

	strategy = desc->strategy_alloc(buf, size);
	if (IS_ERR_OR_NULL(strategy)) {
		chg_err("%s strategy alloc error, rc=%ld\n", name, PTR_ERR(strategy));
		return NULL;
	}
	strategy->desc = desc;

	return strategy;
}

int oplus_chg_strategy_release(struct oplus_chg_strategy *strategy)
{
	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}

	strategy->desc->strategy_release(strategy);
	kfree(strategy);

	return 0;
}

int oplus_chg_strategy_init(struct oplus_chg_strategy *strategy)
{
	int rc;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	if (strategy->desc == NULL) {
		chg_err("strategy desc is NULL\n");
		return -EINVAL;
	}

	rc = strategy->desc->strategy_init(strategy);
	if (rc < 0) {
		chg_err("%s strategy init error, rc=%d\n", strategy->desc->name, rc);
		return rc;
	}
	strategy->initialized = true;

	return 0;
}

int oplus_chg_strategy_get_data(struct oplus_chg_strategy *strategy, int *ret)
{
	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	if (ret == NULL) {
		chg_err("ret is NULL\n");
		return -EINVAL;
	}

	return strategy->desc->strategy_get_data(strategy, ret);
}

int oplus_chg_strategy_register(struct oplus_chg_strategy_desc *desc)
{
	struct oplus_chg_strategy_desc *desc_temp;

	if (desc == NULL) {
		chg_err("strategy desc is NULL");
		return -EINVAL;
	}

	mutex_lock(&list_lock);
	list_for_each_entry(desc_temp, &strategy_list, list) {
		if (desc_temp->name == NULL)
			continue;
		if (strcmp(desc_temp->name, desc->name) == 0) {
			chg_err("the same strategy name already exists\n");
			mutex_unlock(&list_lock);
			return -EINVAL;
		}
	}
	list_add(&desc->list, &strategy_list);
	mutex_unlock(&list_lock);

	return 0;
}

int oplus_chg_strategy_read_data(struct device *dev,
				 const char *prop_str, uint8_t **buf)
{
	struct device_node *node;
	int rc = 0, size;

	if (dev == NULL) {
		chg_err("dev is NULL\n");
		return -EINVAL;
	}
	if (buf == NULL) {
		chg_err("buf is NULL\n");
		return -EINVAL;
	}

	node = dev->of_node;
	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		chg_err("read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}
	size = rc * sizeof(u32);
	if (size > PAGE_SIZE) {
		chg_err("%s data is too long, the max cannot exceed 1 page\n",
			prop_str);
		return -EINVAL;
	}

	*buf = devm_kzalloc(dev, size, GFP_KERNEL);
	if (*buf == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	rc = of_property_read_u32_array(node, prop_str, (u32 *)*buf,
					size / sizeof(u32));
	if (rc) {
		pr_err("read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return size;
}

extern int cgcl_strategy_register(void);

static __init int oplus_chg_strategy_module_init(void)
{
	cgcl_strategy_register();

	return 0;
}

static __exit void oplus_chg_strategy_module_exit(void)
{
	struct oplus_chg_strategy_desc *desc, *tmp;

	mutex_lock(&list_lock);
	list_for_each_entry_safe(desc, tmp, &strategy_list, list) {
		list_del_init(&desc->list);
	}
	mutex_unlock(&list_lock);
}

oplus_chg_module_core_register(oplus_chg_strategy_module);
