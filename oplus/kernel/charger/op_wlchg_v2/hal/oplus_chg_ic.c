// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[IC]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include "oplus_chg_ic.h"

struct ic_devres {
	struct oplus_chg_ic_dev *ic_dev;
};

static DEFINE_MUTEX(list_lock);
static LIST_HEAD(ic_list);

void oplus_chg_ic_list_lock(void)
{
	mutex_lock(&list_lock);
}

void oplus_chg_ic_list_unlock(void)
{
	mutex_unlock(&list_lock);
}

int oplsu_chg_ic_add_child(struct oplus_chg_ic_dev *ic_dev, struct oplus_chg_ic_dev *ch_dev)
{
	struct oplus_chg_ic_dev *dev_temp;

	if (ic_dev == NULL) {
		pr_err("ic_dev is NULL\n");
		return -ENODEV;
	}

	if (ch_dev == NULL) {
		pr_err("ch_dev is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&list_lock);
	list_add(&ch_dev->brother_list, &ic_dev->child_list);
	atomic_inc(&ic_dev->child_num);
	list_for_each_entry(dev_temp, &ic_dev->child_list, brother_list) {
		atomic_set(&dev_temp->brother_num, atomic_read(&ic_dev->child_num));
	}
	mutex_unlock(&list_lock);

	return 0;
}

struct oplus_chg_ic_dev *oplsu_chg_ic_get_child_by_index(struct oplus_chg_ic_dev *ic_dev, int c_index)
{
	struct oplus_chg_ic_dev *dev_temp;

	if (ic_dev == NULL) {
		pr_err("ic_dev is NULL\n");
		return NULL;
	}
	if (c_index >= atomic_read(&ic_dev->child_num)) {
		pr_err("no such device, index=%d\n", c_index);
		return NULL;
	}

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_dev->child_list, brother_list) {
		if (dev_temp->index == c_index) {
			mutex_unlock(&list_lock);
			return dev_temp;
		}
	}
	mutex_unlock(&list_lock);

	pr_err("no such device, index=%d\n", c_index);
	return NULL;
}

struct oplus_chg_ic_dev *oplsu_chg_ic_find_by_name(const char *name)
{
	struct oplus_chg_ic_dev *dev_temp;

	if (name == NULL) {
		pr_err("name is NULL\n");
		return NULL;
	}

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_list, list){
		if (dev_temp->name == NULL)
			continue;
		if (strcmp(dev_temp->name, name) == 0) {
			mutex_unlock(&list_lock);
			return dev_temp;
		}
	}
	mutex_unlock(&list_lock);

	return NULL;
}

struct oplus_chg_ic_dev *of_get_oplus_chg_ic(struct device_node *node, const char *prop_name)
{
	struct device_node *ic_node;

	ic_node = of_parse_phandle(node, prop_name, 0);
	if (!ic_node)
  		return NULL;
	
	return oplsu_chg_ic_find_by_name(ic_node->name);
}

#ifdef OPLUS_CHG_REG_DUMP_ENABLE
int oplus_chg_ic_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_chg_ic_ops *ic_ops;
	int rc;

	if (ic_dev == NULL) {
		pr_err("ic_dev is NULL\n");
		return -ENODEV;
	}
	ic_ops = (struct oplus_chg_ic_ops *)ic_dev->dev_ops;
	if (ic_ops->reg_dump == NULL) {
		pr_err("%s not support reg dump\n", ic_dev->name);
		return -EINVAL;
	}

	rc = ic_ops->reg_dump(ic_dev);
	return rc;
}

int oplus_chg_ic_reg_dump_by_name(const char *name)
{
	struct oplus_chg_ic_dev *ic_dev;
	int rc;

	if (name == NULL) {
		pr_err("ic name is NULL\n");
		return -EINVAL;
	}
	ic_dev = oplsu_chg_ic_find_by_name(name);
	if (ic_dev == NULL) {
		pr_err("%s ic not found\n", name);
		return -ENODEV;
	}
	rc = oplus_chg_ic_reg_dump(ic_dev);
	return rc;
}

void oplus_chg_ic_reg_dump_all(void)
{
	struct oplus_chg_ic_dev *dev_temp;

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_list, list){
		(void)oplus_chg_ic_reg_dump(dev_temp);
	}
	mutex_unlock(&list_lock);
}
#endif /* OPLUS_CHG_REG_DUMP_ENABLE */

struct oplus_chg_ic_dev *oplus_chg_ic_register(struct device *dev,
	const char *name, int index)
{
	struct oplus_chg_ic_dev *dev_temp;
	struct oplus_chg_ic_dev *ic_dev;

	if (name == NULL) {
		pr_err("ic name is NULL\n");
		return NULL;
	}

	ic_dev = kzalloc(sizeof(struct oplus_chg_ic_dev), GFP_KERNEL);
	if (ic_dev == NULL) {
		pr_err("alloc oplus_chg_ic error\n");
		return NULL;
	}
	ic_dev->name = name;
	ic_dev->index = index;
	ic_dev->dev = dev;

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_list, list){
		if (dev_temp->name == NULL)
			continue;
		if (strcmp(dev_temp->name, ic_dev->name) == 0) {
			pr_err("device with the same name already exists\n");
			mutex_unlock(&list_lock);
			kfree(ic_dev);
			return NULL;
		}
	}
	list_add(&ic_dev->list, &ic_list);
	INIT_LIST_HEAD(&ic_dev->child_list);
	atomic_set(&ic_dev->child_num, 0);
	INIT_LIST_HEAD(&ic_dev->brother_list);
	atomic_set(&ic_dev->brother_num, 0);
	mutex_unlock(&list_lock);

	return ic_dev;
}

int oplus_chg_ic_unregister(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		pr_err("ic_dev is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&list_lock);
	list_del(&ic_dev->list);
	mutex_unlock(&list_lock);
	kfree(ic_dev);

	return 0;
}

static void devm_oplus_chg_ic_release(struct device *dev, void *res)
{
	struct ic_devres *this = res;

	mutex_lock(&list_lock);
	list_del(&this->ic_dev->list);
	mutex_unlock(&list_lock);
	kfree(this->ic_dev);
}

static int devm_oplus_chg_ic_match(struct device *dev, void *res, void *data)
{
	struct ic_devres *this = res, *match = data;

	return this->ic_dev == match->ic_dev;
}

struct oplus_chg_ic_dev *devm_oplus_chg_ic_register(struct device *dev,
	const char *name, int index)
{
	struct ic_devres *dr;
	struct oplus_chg_ic_dev *dev_temp;
	struct oplus_chg_ic_dev *ic_dev;

	if (name == NULL) {
		pr_err("ic name is NULL\n");
		return NULL;
	}

	dr = devres_alloc(devm_oplus_chg_ic_release, sizeof(struct ic_devres),
  			  GFP_KERNEL);
	if (!dr) {
		pr_err("devres_alloc error\n");
		return NULL;
	}

	ic_dev = kzalloc(sizeof(struct oplus_chg_ic_dev), GFP_KERNEL);
	if (ic_dev == NULL) {
		pr_err("alloc oplus_chg_ic error\n");
		devres_free(dr);
		return NULL;
	}
	ic_dev->name = name;
	ic_dev->index = index;
	ic_dev->dev = dev;

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_list, list){
		if (dev_temp->name == NULL)
			continue;
		if (strcmp(dev_temp->name, ic_dev->name) == 0) {
			pr_err("device with the same name already exists\n");
			mutex_unlock(&list_lock);
			devres_free(dr);
			kfree(ic_dev);
			return NULL;
		}
	}
	list_add(&ic_dev->list, &ic_list);
	INIT_LIST_HEAD(&ic_dev->child_list);
	atomic_set(&ic_dev->child_num, 0);
	INIT_LIST_HEAD(&ic_dev->brother_list);
	atomic_set(&ic_dev->brother_num, 0);
	mutex_unlock(&list_lock);

	dr->ic_dev = ic_dev;
	devres_add(dev, dr);

	return ic_dev;
}

int devm_oplus_chg_ic_unregister(struct device *dev, struct oplus_chg_ic_dev *ic_dev)
{
	struct ic_devres match_data = { ic_dev };

	if (dev == NULL) {
		pr_err("dev is NULL\n");
		return -ENODEV;
	}
	if (ic_dev == NULL) {
		pr_err("ic_dev is NULL\n");
		return -ENODEV;
	}

	WARN_ON(devres_destroy(dev, devm_oplus_chg_ic_release,
			       devm_oplus_chg_ic_match, &match_data));

	return 0;
}
