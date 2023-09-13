// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[IC]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>

struct ic_devres {
	struct oplus_chg_ic_dev *ic_dev;
};

static DEFINE_MUTEX(list_lock);
static LIST_HEAD(ic_list);
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
static DEFINE_IDA(oplus_chg_ic_ida);
#define OPLUS_CHG_IC_MAX 256
static struct class oplus_chg_ic_class;
static dev_t oplus_chg_ic_devno;

static const char * const err_type_text[] = {
	[OPLUS_IC_ERR_UNKNOWN]		= "Unknown",
	[OPLUS_IC_ERR_I2C]		= "I2C",
	[OPLUS_IC_ERR_GPIO]		= "GPIO",
	[OPLUS_IC_ERR_PLAT_PMIC]	= "PlatformPMIC",
	[OPLUS_IC_ERR_BUCK_BOOST]	= "Buck/Boost",
	[OPLUS_IC_ERR_GAUGE]		= "Gauge",
	[OPLUS_IC_ERR_WLS_RX]		= "WirelessRX",
	[OPLUS_IC_ERR_CP]		= "ChargePump",
	[OPLUS_IC_ERR_CC_LOGIC]		= "CCLogic",
};

int oplus_chg_ic_get_new_minor(void)
{
	return ida_simple_get(&oplus_chg_ic_ida, 0, OPLUS_CHG_IC_MAX - 1,
			      GFP_KERNEL);
}

void oplus_chg_ic_free_minor(unsigned int minor)
{
	ida_simple_remove(&oplus_chg_ic_ida, minor);
}
#endif

void oplus_chg_ic_list_lock(void)
{
	mutex_lock(&list_lock);
}

void oplus_chg_ic_list_unlock(void)
{
	mutex_unlock(&list_lock);
}

struct oplus_chg_ic_dev *oplsu_chg_ic_find_by_name(const char *name)
{
	struct oplus_chg_ic_dev *dev_temp;

	if (name == NULL) {
		chg_err("name is NULL\n");
		return NULL;
	}

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_list, list) {
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

struct oplus_chg_ic_dev *of_get_oplus_chg_ic(struct device_node *node,
					     const char *prop_name, int index)
{
	struct device_node *ic_node;

	ic_node = of_parse_phandle(node, prop_name, index);
	if (!ic_node)
		return NULL;

	chg_debug("search %s\n", ic_node->name);
	return oplsu_chg_ic_find_by_name(ic_node->name);
}

#ifdef OPLUS_CHG_REG_DUMP_ENABLE
int oplus_chg_ic_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_chg_ic_ops *ic_ops;
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL\n");
		return -ENODEV;
	}
	ic_ops = (struct oplus_chg_ic_ops *)ic_dev->dev_ops;
	if (ic_ops->reg_dump == NULL) {
		chg_err("%s not support reg dump\n", ic_dev->name);
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
		chg_err("ic name is NULL\n");
		return -EINVAL;
	}
	ic_dev = oplsu_chg_ic_find_by_name(name);
	if (ic_dev == NULL) {
		chg_err("%s ic not found\n", name);
		return -ENODEV;
	}
	rc = oplus_chg_ic_reg_dump(ic_dev);
	return rc;
}

void oplus_chg_ic_reg_dump_all(void)
{
	struct oplus_chg_ic_dev *dev_temp;

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_list, list) {
		(void)oplus_chg_ic_reg_dump(dev_temp);
	}
	mutex_unlock(&list_lock);
}
#endif /* OPLUS_CHG_REG_DUMP_ENABLE */

int oplus_chg_ic_func_table_sort(enum oplus_chg_ic_func *func_table,
				 int func_num)
{
	/* TODO */
	return 0;
}

bool oplus_chg_ic_func_is_support(enum oplus_chg_ic_func *func_table,
				  int func_num, enum oplus_chg_ic_func func_id)
{
	int i;

	/* TODO: Use binary search to calculate and send*/
	for (i = 0; i < func_num; i++) {
		if (func_id == func_table[i])
			return true;
	}
	return false;
}

bool oplus_chg_ic_virq_is_support(enum oplus_chg_ic_virq_id *virq_table,
				  int virq_num,
				  enum oplus_chg_ic_virq_id virq_id)
{
	int i;

	/* TODO: Use binary search to calculate and send*/
	for (i = 0; i < virq_num; i++) {
		if (virq_id == virq_table[i])
			return true;
	}
	return false;
}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
bool oplus_chg_ic_debug_data_check(const void *buf, size_t len)
{
	struct oplus_chg_ic_func_date *debug_data;
	struct oplus_chg_ic_func_date_item *item;
	size_t item_size = 0;
	int i;

	if (buf == NULL) {
		chg_err("data buf is NULL\n");
		return false;
	}

	if (len < sizeof(struct oplus_chg_ic_func_date)) {
		chg_err("data buf too short\n");
		return false;
	}
	debug_data = (struct oplus_chg_ic_func_date *)buf;
	if (debug_data->magic != OPLUS_CHG_IC_FUNC_DATA_MAGIC) {
		chg_err("data buf magic error\n");
		return false;
	}

	for (i = 0; i < debug_data->item_num; i++) {
		item = (struct oplus_chg_ic_func_date_item *)(debug_data->buf + item_size);
		if (item->num != i) {
			chg_err("item num error\n");
			return false;
		}
		item_size += (item->size + sizeof(struct oplus_chg_ic_func_date_item));
	}

	if (debug_data->size != item_size) {
		chg_err("data size error, data_size=%d, item_size=%lu\n", debug_data->size, item_size);
		return false;
	}

	return true;
}

int oplus_chg_ic_get_item_num(const void *buf, size_t len)
{
	struct oplus_chg_ic_func_date *debug_data;

	if (buf == NULL) {
		chg_err("data buf is NULL\n");
		return -EINVAL;
	}
	if (len < sizeof(struct oplus_chg_ic_func_date)) {
		chg_err("data buf too short\n");
		return -EINVAL;
	}
	debug_data = (struct oplus_chg_ic_func_date *)buf;
	if (debug_data->magic != OPLUS_CHG_IC_FUNC_DATA_MAGIC) {
		chg_err("data buf magic error\n");
		return -EINVAL;
	}

	return (int)debug_data->item_num;
}

int oplus_chg_ic_get_item_data(const void *buf, int index)
{
	struct oplus_chg_ic_func_date *debug_data;
	struct oplus_chg_ic_func_date_item *item;
	size_t item_size = 0;
	int i;

	debug_data = (struct oplus_chg_ic_func_date *)buf;
	for (i = 0; i <= index; i++) {
		item = (struct oplus_chg_ic_func_date_item *)(debug_data->buf + item_size);
		item_size += (item->size + sizeof(struct oplus_chg_ic_func_date_item));
	}

	return le32_to_cpu(*((u32 *)item->buf));
}

void *oplus_chg_ic_get_item_data_addr(void *buf, int index)
{
	struct oplus_chg_ic_func_date *debug_data;
	struct oplus_chg_ic_func_date_item *item;
	size_t item_size = 0;
	int i;

	debug_data = (struct oplus_chg_ic_func_date *)buf;
	for (i = 0; i <= index; i++) {
		item = (struct oplus_chg_ic_func_date_item *)(debug_data->buf + item_size);
		item_size += (item->size + sizeof(struct oplus_chg_ic_func_date_item));
	}

	return (void *)item->buf;
}

void oplus_chg_ic_debug_data_init(void *buf, int argc)
{
	struct oplus_chg_ic_func_date *debug_data;
	struct oplus_chg_ic_func_date_item *item;
	size_t item_size = 0;
	int i;

	debug_data = (struct oplus_chg_ic_func_date *)buf;
	debug_data->magic = OPLUS_CHG_IC_FUNC_DATA_MAGIC;
	debug_data->size = (sizeof(struct oplus_chg_ic_func_date_item) + 4) * argc;
	debug_data->item_num = argc;

	for (i = 0; i <= argc; i++) {
		item = (struct oplus_chg_ic_func_date_item *)(debug_data->buf + item_size);
		item->num = i;
		item->size = 4;
		item->str_data = 0;
		item_size += (item->size + sizeof(struct oplus_chg_ic_func_date_item));
	}
}

int oplus_chg_ic_debug_str_data_init(void *buf, int len)
{
	struct oplus_chg_ic_func_date *debug_data;
	struct oplus_chg_ic_func_date_item *item;
	int size;

	debug_data = (struct oplus_chg_ic_func_date *)buf;
	debug_data->magic = OPLUS_CHG_IC_FUNC_DATA_MAGIC;
	debug_data->size = len + 1 + sizeof(struct oplus_chg_ic_func_date_item);
	debug_data->item_num = 1;
	size = sizeof(struct oplus_chg_ic_func_date) + debug_data->size;

	item = (struct oplus_chg_ic_func_date_item *)(debug_data->buf);
	item->num = 0;
	item->size = len + 1;
	item->str_data = 1;
	memset(item->buf, 0, item->size);

	return size;
}

bool oplus_chg_ic_is_overwrited(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id)
{
	struct oplus_chg_ic_overwrite_data *data;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL\n");
		return false;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(data, &ic_dev->debug.overwrite_list, list) {
		if (data->func_id != func_id)
			continue;
		rcu_read_unlock();
		return true;
	}
	rcu_read_unlock();

	return false;
}

struct oplus_chg_ic_overwrite_data *
oplus_chg_ic_get_overwrite_data(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id)
{
	struct oplus_chg_ic_overwrite_data *data;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL\n");
		return NULL;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(data, &ic_dev->debug.overwrite_list, list) {
		if (data->func_id != func_id)
			continue;
		rcu_read_unlock();
		return data;
	}
	rcu_read_unlock();

	return NULL;
}

inline size_t oplus_chg_ic_debug_data_size(int argc)
{
	return sizeof(struct oplus_chg_ic_func_date) +
	       (sizeof(struct oplus_chg_ic_func_date_item) + 4) * argc;
}

static const struct file_operations oplus_chg_ic_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = NULL,
	.read = NULL,
	.open = NULL,
	.unlocked_ioctl = NULL,
};

static ssize_t func_id_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", ic_dev->debug.func_id);
}

static ssize_t func_id_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);
	int val;

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ic_dev->debug.func_id = val;

	return count;
}
static DEVICE_ATTR_RW(func_id);

static ssize_t data_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);
	ssize_t rc;

	if (!ic_dev->debug.get_func_data)
		return -ENOTSUPP;

	rc = ic_dev->debug.get_func_data(ic_dev, ic_dev->debug.func_id,
					 (void *)buf);

	return rc;
}

static ssize_t data_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);
	int rc;

	if (!ic_dev->debug.set_func_data)
		return -ENOTSUPP;

	rc = ic_dev->debug.set_func_data(ic_dev, ic_dev->debug.func_id,
					 (void *)buf, count);

	return rc;
}
static DEVICE_ATTR_RW(data);

static ssize_t overwrite_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);
	int argc;
	struct oplus_chg_ic_overwrite_data *overwrite_data;
	int i;

	if (!oplus_chg_ic_func_is_support(ic_dev->debug.overwrite_funcs,
					  ic_dev->debug.func_num,
					  ic_dev->debug.func_id)) {
		chg_err("this func(=%d) not support overwrite\n", ic_dev->debug.func_id);
		return -ENOTSUPP;
	}

	if (!oplus_chg_ic_debug_data_check((const void *)buf, count))
		return -EINVAL;
	argc = oplus_chg_ic_get_item_num((const void *)buf, count);
	if (argc < 0)
		return argc;
	for (i = 0; i < argc; i++)
		chg_err("overwrite argv[%d] = %d\n", i,
			oplus_chg_ic_get_item_data((const void *)buf, i));

	overwrite_data =
		devm_kzalloc(dev,
			     sizeof(struct oplus_chg_ic_overwrite_data) + count,
			     GFP_KERNEL);
	if (overwrite_data == NULL) {
		chg_err("alloc overwrite data buf error\n");
		return -ENOMEM;
	}
	overwrite_data->func_id = ic_dev->debug.func_id;
	memcpy(overwrite_data->buf, buf, count);
	overwrite_data->size = count;
	mutex_lock(&ic_dev->debug.overwrite_list_lock);
	list_add_rcu(&overwrite_data->list, &ic_dev->debug.overwrite_list);
	mutex_unlock(&ic_dev->debug.overwrite_list_lock);

	return count;
}
static DEVICE_ATTR_WO(overwrite);

static ssize_t clean_overwrite_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);
	struct oplus_chg_ic_overwrite_data *data;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&ic_dev->debug.overwrite_list_lock);
	list_for_each_entry_rcu(data, &ic_dev->debug.overwrite_list, list) {
		if (data->func_id == ic_dev->debug.func_id) {
			list_del_rcu(&data->list);
			synchronize_rcu();
			chg_info("clean %s func %d overwrite\n",
				 ic_dev->name, data->func_id);
			devm_kfree(dev, data);
			break;
		}
	}
	mutex_unlock(&ic_dev->debug.overwrite_list_lock);

	return count;
}
static DEVICE_ATTR_WO(clean_overwrite);

static ssize_t virq_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);
	struct oplus_chg_ic_virq *virq;
	int i;
	int val;

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	for (i = 0; i < ic_dev->virq_num; i++) {
		virq = &ic_dev->virq_data[i];
		if (virq->virq_id == val) {
			if (virq->virq_handler == NULL) {
				chg_err("%s virq(=%d) handler is NULL\n",
					ic_dev->name, val);
				return -ENOTSUPP;
			}
			virq->virq_handler(ic_dev, virq->virq_data);
			return count;
		}
	}

	chg_err("%s not support this virq_id(=%d)\n", ic_dev->name, val);
	return -ENOTSUPP;
}
static DEVICE_ATTR_WO(virq);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", ic_dev->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t manu_name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", ic_dev->manu_name);
}
static DEVICE_ATTR_RO(manu_name);

static ssize_t fw_id_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct oplus_chg_ic_dev *ic_dev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", ic_dev->fw_id);
}
static DEVICE_ATTR_RO(fw_id);

static struct device_attribute *oplus_chg_ic_attributes[] = {
	&dev_attr_func_id,
	&dev_attr_data,
	&dev_attr_overwrite,
	&dev_attr_clean_overwrite,
	&dev_attr_virq,
	&dev_attr_name,
	&dev_attr_manu_name,
	&dev_attr_fw_id,
	NULL
};

static ssize_t version_show(struct class *C, struct class_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%s", OPLUS_CHG_IC_CFG_VERSION);
}
static CLASS_ATTR_RO(version);

static struct attribute *oplus_chg_ic_class_attrs[] = {
	&class_attr_version.attr,
	NULL,
};
ATTRIBUTE_GROUPS(oplus_chg_ic_class);
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

int oplus_chg_ic_virq_register(struct oplus_chg_ic_dev *ic_dev,
			       enum oplus_chg_ic_virq_id virq_id,
			       virq_handler_t handler,
			       void *virq_data)
{
	struct oplus_chg_ic_virq *virq;
	int i;

	if (ic_dev == NULL)
		return -ENODEV;

	for (i = 0; i < ic_dev->virq_num; i++) {
		virq = &ic_dev->virq_data[i];
		if (virq->virq_id == virq_id) {
			virq->virq_data = virq_data;
			virq->virq_handler = handler;
			return 0;
		}
	}

	chg_err("%s not support this virq_id(=%d)\n", ic_dev->name, virq_id);
	return -ENOTSUPP;
}

int oplus_chg_ic_virq_release(struct oplus_chg_ic_dev *ic_dev,
			      enum oplus_chg_ic_virq_id virq_id,
			      void *virq_data)
{
	struct oplus_chg_ic_virq *virq;
	int i;

	if (ic_dev == NULL)
		return -ENODEV;

	for (i = 0; i < ic_dev->virq_num; i++) {
		virq = &ic_dev->virq_data[i];
		if (virq->virq_id == virq_id) {
			if (virq_data != virq->virq_data) {
				chg_err("virq_data does not match\n");
				return -EINVAL;
			}
			virq->virq_data = NULL;
			virq->virq_handler = NULL;
			return 0;
		}
	}

	chg_err("%s not support this virq_id(=%d)\n", ic_dev->name, virq_id);
	return -ENOTSUPP;
}

int oplus_chg_ic_virq_trigger(struct oplus_chg_ic_dev *ic_dev,
			      enum oplus_chg_ic_virq_id virq_id)
{
	struct oplus_chg_ic_virq *virq;
	int i;

	if (ic_dev == NULL)
		return -ENODEV;

	for (i = 0; i < ic_dev->virq_num; i++) {
		virq = &ic_dev->virq_data[i];
		if (virq->virq_id == virq_id) {
			if (virq->virq_handler == NULL ||
			    virq->virq_data == NULL) {
				chg_info("%s virq(=%d) handler or data is NULL \n",
					 ic_dev->name, virq_id);
				return 0;
			}
			virq->virq_handler(ic_dev, virq->virq_data);
			return 0;
		}
	}

	chg_err("%s not support this virq_id(=%d)\n", ic_dev->name, virq_id);
	return -ENOTSUPP;
}

__printf(4, 5)
int oplus_chg_ic_creat_err_msg(struct oplus_chg_ic_dev *ic_dev,
			       enum oplus_chg_ic_err err_type, int sub_err_type,
			       const char *format, ...)
{
	va_list args;
	struct oplus_chg_ic_err_msg *err_msg;
	int length;

	if (ic_dev == NULL) {
		chg_err("ic_dev is null\n");
		return -ENODEV;
	}
	err_msg = kzalloc(sizeof(struct oplus_chg_ic_err_msg), GFP_KERNEL);
	if (err_msg == NULL) {
		chg_err("%s: alloc err msg buf error\r\n", ic_dev->name);
		return -ENOMEM;
	}

	err_msg->ic = ic_dev;
	err_msg->type = err_type;
	err_msg->sub_type = sub_err_type;
	va_start(args, format);
	length = vsnprintf(err_msg->msg, IC_ERR_MSG_MAX - 1, format, args);
	va_end(args);

	spin_lock(&ic_dev->err_list_lock);
	list_add_tail(&err_msg->list, &ic_dev->err_list);
	spin_unlock(&ic_dev->err_list_lock);

	return length;
}

int oplus_chg_ic_move_err_msg(struct oplus_chg_ic_dev *dest,
			      struct oplus_chg_ic_dev *src)
{
	if (dest == NULL) {
		chg_err("dest is null\n");
		return -ENODEV;
	}
	if (src == NULL) {
		chg_err("src is null\n");
		return -ENODEV;
	}

	spin_lock(&dest->err_list_lock);
	spin_lock(&src->err_list_lock);
	if (!list_empty(&src->err_list))
		list_bulk_move_tail(&dest->err_list, src->err_list.next,
				    src->err_list.prev);
	spin_unlock(&src->err_list_lock);
	spin_unlock(&dest->err_list_lock);

	return 0;
}

int oplus_chg_ic_clean_err_msg(struct oplus_chg_ic_dev *ic_dev,
			       struct oplus_chg_ic_err_msg *err_msg)
{
	if (ic_dev == NULL) {
		chg_err("ic_dev is null\n");
		return -ENODEV;
	}
	if (err_msg == NULL) {
		chg_err("err_msg is null\n");
		return -EINVAL;
	}

	spin_lock(&ic_dev->err_list_lock);
	list_del(&err_msg->list);
	spin_unlock(&ic_dev->err_list_lock);
	kfree(err_msg);

	return 0;
}

const char *oplus_chg_ic_err_text(enum oplus_chg_ic_err err_type)
{
	return err_type_text[err_type];
}

static struct oplus_chg_ic_dev *
__oplus_chg_ic_register(struct device *dev, struct oplus_chg_ic_cfg *cfg)
{
	struct oplus_chg_ic_dev *dev_temp;
	struct oplus_chg_ic_dev *ic_dev;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct device_attribute **attrs;
	struct device_attribute *attr;
	int rc;
#endif

	if (cfg == NULL) {
		chg_err("ic cfg is NULL\n");
		return NULL;
	}

	ic_dev = kzalloc(sizeof(struct oplus_chg_ic_dev), GFP_KERNEL);
	if (ic_dev == NULL) {
		chg_err("alloc oplus_chg_ic error\n");
		return NULL;
	}
	ic_dev->name = cfg->name;
	ic_dev->index = cfg->index;
	memcpy(ic_dev->manu_name, cfg->manu_name, OPLUS_CHG_IC_MANU_NAME_MAX);
	memcpy(ic_dev->fw_id, cfg->fw_id, OPLUS_CHG_IC_FW_ID_MAX);
	ic_dev->type = cfg->type;
	ic_dev->get_func = cfg->get_func;
	ic_dev->virq_data = cfg->virq_data;
	ic_dev->virq_num = cfg->virq_num;
	ic_dev->dev = dev;

	INIT_LIST_HEAD(&ic_dev->err_list);
	spin_lock_init(&ic_dev->err_list_lock);
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	mutex_init(&ic_dev->debug.overwrite_list_lock);
	INIT_LIST_HEAD(&ic_dev->debug.overwrite_list);

	ic_dev->minor = oplus_chg_ic_get_new_minor();
	if (ic_dev->minor < 0) {
		chg_err("cannot register more ICs\n");
		goto get_minor_err;
	}
	snprintf(ic_dev->cdev_name, sizeof(ic_dev->cdev_name), "vic-%d",
		 ic_dev->minor);
	ic_dev->debug_dev =
		device_create(&oplus_chg_ic_class, ic_dev->dev,
			      MKDEV(MAJOR(oplus_chg_ic_devno), ic_dev->minor),
			      ic_dev, "vic-%d", ic_dev->minor);
	if (IS_ERR(ic_dev->debug_dev)) {
		chg_err("debug device create failed, rc=%ld\n",
			PTR_ERR(ic_dev->debug_dev));
		goto device_create_err;
	}
	cdev_init(&ic_dev->cdev, &oplus_chg_ic_fops);
	ic_dev->cdev.owner = THIS_MODULE;

	rc = cdev_add(&ic_dev->cdev,
		      MKDEV(MAJOR(oplus_chg_ic_devno), ic_dev->minor), 1);
	if (rc < 0) {
		chg_err("cdev_add failed, rc=%d\n", rc);
		goto cdev_add_err;
	}
	attrs = oplus_chg_ic_attributes;
	while ((attr = *attrs++)) {
		rc = device_create_file(ic_dev->debug_dev, attr);
		if (rc) {
			chg_err("device_create_file fail!\n");
			goto device_create_file_err;
		}
	}
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

	mutex_lock(&list_lock);
	list_for_each_entry(dev_temp, &ic_list, list) {
		if (dev_temp->name == NULL)
			continue;
		if (strcmp(dev_temp->name, ic_dev->name) == 0) {
			chg_err("device with the same name already exists\n");
			mutex_unlock(&list_lock);
			goto name_err;
		}
	}
	list_add(&ic_dev->list, &ic_list);
	mutex_unlock(&list_lock);

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	kobject_uevent(&ic_dev->debug_dev->kobj, KOBJ_CHANGE);
#endif

	return ic_dev;

name_err:
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	attrs = oplus_chg_ic_attributes;
	while ((attr = *attrs++))
		device_remove_file(ic_dev->debug_dev, attr);
device_create_file_err:
	cdev_del(&ic_dev->cdev);
cdev_add_err:
	device_destroy(&oplus_chg_ic_class,
		       MKDEV(MAJOR(oplus_chg_ic_devno), ic_dev->minor));
device_create_err:
	oplus_chg_ic_free_minor(ic_dev->minor);
get_minor_err:
#endif
	kfree(ic_dev);
	return NULL;
}

struct oplus_chg_ic_dev *oplus_chg_ic_register(struct device *dev,
					       struct oplus_chg_ic_cfg *cfg)
{
	return __oplus_chg_ic_register(dev, cfg);
}

int oplus_chg_ic_unregister(struct oplus_chg_ic_dev *ic_dev)
{
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct device_attribute **attrs;
	struct device_attribute *attr;
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL\n");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	attrs = oplus_chg_ic_attributes;
	while ((attr = *attrs++))
		device_remove_file(ic_dev->debug_dev, attr);
	cdev_del(&ic_dev->cdev);
	device_destroy(&oplus_chg_ic_class,
		       MKDEV(MAJOR(oplus_chg_ic_devno), ic_dev->minor));
	oplus_chg_ic_free_minor(ic_dev->minor);
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

	mutex_lock(&list_lock);
	list_del(&ic_dev->list);
	mutex_unlock(&list_lock);
	kfree(ic_dev);

	return 0;
}

static void devm_oplus_chg_ic_release(struct device *dev, void *res)
{
	struct ic_devres *this = res;

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_chg_ic_attributes;
	while ((attr = *attrs++))
		device_remove_file(this->ic_dev->debug_dev, attr);
	cdev_del(&this->ic_dev->cdev);
	device_destroy(&oplus_chg_ic_class,
		       MKDEV(MAJOR(oplus_chg_ic_devno), this->ic_dev->minor));
	oplus_chg_ic_free_minor(this->ic_dev->minor);
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */
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
						    struct oplus_chg_ic_cfg *cfg)
{
	struct ic_devres *dr;
	struct oplus_chg_ic_dev *ic_dev;

	dr = devres_alloc(devm_oplus_chg_ic_release, sizeof(struct ic_devres),
			  GFP_KERNEL);
	if (!dr) {
		chg_err("devres_alloc error\n");
		return NULL;
	}

	ic_dev = __oplus_chg_ic_register(dev, cfg);
	if (ic_dev == NULL) {
		devres_free(dr);
		return NULL;
	}

	dr->ic_dev = ic_dev;
	devres_add(dev, dr);

	return ic_dev;
}
EXPORT_SYMBOL(devm_oplus_chg_ic_register);

int devm_oplus_chg_ic_unregister(struct device *dev,
				 struct oplus_chg_ic_dev *ic_dev)
{
	struct ic_devres match_data = { ic_dev };

	if (dev == NULL) {
		chg_err("dev is NULL\n");
		return -ENODEV;
	}
	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL\n");
		return -ENODEV;
	}

	WARN_ON(devres_destroy(dev, devm_oplus_chg_ic_release,
			       devm_oplus_chg_ic_match, &match_data));

	return 0;
}
EXPORT_SYMBOL(devm_oplus_chg_ic_unregister);

static __init int oplus_chg_ic_class_init(void)
{
	int rc = 0;

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	oplus_chg_ic_class.name = "virtual_ic";
	oplus_chg_ic_class.class_groups = oplus_chg_ic_class_groups;
	rc = class_register(&oplus_chg_ic_class);
	if (rc < 0) {
		chg_err("Failed to create oplus_chg_ic_class, rc=%d\n", rc);
		goto err;
	}

	rc = alloc_chrdev_region(&oplus_chg_ic_devno, 0, OPLUS_CHG_IC_MAX,
				 "virtual_ic");
	if (rc < 0) {
		chg_err("alloc oplus_chg_ic_devno fail!\n");
		goto err;
	}

	return 0;
err:
	class_unregister(&oplus_chg_ic_class);
#endif
	return rc;
}

static __exit void oplus_chg_ic_class_exit(void)
{
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	unregister_chrdev_region(oplus_chg_ic_devno, OPLUS_CHG_IC_MAX);
	class_unregister(&oplus_chg_ic_class);
#endif
}

oplus_chg_module_core_register(oplus_chg_ic_class);
