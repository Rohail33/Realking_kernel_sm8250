// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[CORE]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include "oplus_chg_core.h"
#endif
#include <linux/property.h>
#include "oplus_chg_module.h"
#include "oplus_chg_core.h"

ATOMIC_NOTIFIER_HEAD(oplus_chg_event_notifier);
EXPORT_SYMBOL_GPL(oplus_chg_event_notifier);

ATOMIC_NOTIFIER_HEAD(oplus_chg_changed_notifier);
EXPORT_SYMBOL_GPL(oplus_chg_changed_notifier);

static struct device_type oplus_chg_dev_type;

static DEFINE_MUTEX(mod_list_lock);
static LIST_HEAD(mod_list);

#define OPLUS_CHG_DEFERRED_REGISTER_TIME msecs_to_jiffies(10)

#ifdef MODULE
__attribute__((weak)) size_t __oplus_chg_module_start;
__attribute__((weak)) size_t __oplus_chg_module_end;

static int oplus_chg_get_module_num(void)
{
	size_t addr_size = (size_t)&__oplus_chg_module_end -
			   (size_t)&__oplus_chg_module_start;

	if (addr_size == 0)
		return 0;
	if (addr_size % sizeof(struct oplus_chg_module) != 0) {
		pr_err("oplus chg module address is error, please check oplus_chg_module.lds\n");
		return 0;
	}

	return (addr_size / sizeof(struct oplus_chg_module));
}

static struct oplus_chg_module *oplus_chg_find_first_module(void)
{
	size_t start_addr = (size_t)&__oplus_chg_module_start;
	return (struct oplus_chg_module *)READ_ONCE_NOCHECK(start_addr);
}
#endif /* MODULE */

static void oplus_chg_mod_changed_work(struct work_struct *work)
{
	unsigned long flags;
	struct oplus_chg_mod *ocm =
		container_of(work, struct oplus_chg_mod, changed_work);

	dev_dbg(&ocm->dev, "%s\n", __func__);

	spin_lock_irqsave(&ocm->changed_lock, flags);
	/*
	 * Check 'changed' here to avoid issues due to race between
	 * oplus_chg_mod_changed() and this routine. In worst case
	 * oplus_chg_mod_changed() can be called again just before we take above
	 * lock. During the first call of this routine we will mark 'changed' as
	 * false and it will stay false for the next call as well.
	 */
	if (likely(ocm->changed)) {
		ocm->changed = false;
		spin_unlock_irqrestore(&ocm->changed_lock, flags);
		atomic_notifier_call_chain(&oplus_chg_changed_notifier,
					   OPLUS_CHG_EVENT_CHANGED, ocm);
		//kobject_uevent(&ocm->dev.kobj, KOBJ_CHANGE);
		spin_lock_irqsave(&ocm->changed_lock, flags);
	}

	/*
	 * Hold the wakeup_source until all events are processed.
	 * oplus_chg_mod_changed() might have called again and have set 'changed'
	 * to true.
	 */
	if (likely(!ocm->changed))
		pm_relax(&ocm->dev);
	spin_unlock_irqrestore(&ocm->changed_lock, flags);
}

void oplus_chg_mod_changed(struct oplus_chg_mod *ocm)
{
	unsigned long flags;

	dev_dbg(&ocm->dev, "%s\n", __func__);

	spin_lock_irqsave(&ocm->changed_lock, flags);
	ocm->changed = true;
	pm_stay_awake(&ocm->dev);
	spin_unlock_irqrestore(&ocm->changed_lock, flags);
	schedule_work(&ocm->changed_work);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_changed);

static void oplus_chg_mod_deferred_register_work(struct work_struct *work)
{
	struct oplus_chg_mod *ocm = container_of(work, struct oplus_chg_mod,
						 deferred_register_work.work);

	if (ocm->dev.parent) {
		while (!mutex_trylock(&ocm->dev.parent->mutex)) {
			if (ocm->removing)
				return;
			msleep(10);
		}
	}

	kobject_uevent(&ocm->dev.kobj, KOBJ_CHANGE);

	if (ocm->dev.parent)
		mutex_unlock(&ocm->dev.parent->mutex);
}

/**
 * oplus_chg_mod_get_by_name() - Search for a power supply and returns its ref
 * @name: Power supply name to fetch
 *
 * If power supply was found, it increases reference count for the
 * internal power supply's device. The user should oplus_chg_mod_put()
 * after usage.
 *
 * Return: On success returns a reference to a power supply with
 * matching name equals to @name, a NULL otherwise.
 */
struct oplus_chg_mod *oplus_chg_mod_get_by_name(const char *name)
{
	struct oplus_chg_mod *ocm = NULL;

	mutex_lock(&mod_list_lock);
	list_for_each_entry(ocm, &mod_list, list) {
		if (ocm->desc->name == NULL)
			continue;
		if (strcmp(ocm->desc->name, name) == 0) {
			mutex_unlock(&mod_list_lock);
			return ocm;
		}
	}
	mutex_unlock(&mod_list_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_get_by_name);

/**
 * oplus_chg_mod_put() - Drop reference obtained with oplus_chg_mod_get_by_name
 * @ocm: Reference to put
 *
 * The reference to power supply should be put before unregistering
 * the power supply.
 */
void oplus_chg_mod_put(struct oplus_chg_mod *ocm)
{
	might_sleep();

	atomic_dec(&ocm->use_cnt);
	put_device(&ocm->dev);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_put);

int oplus_chg_mod_get_property(struct oplus_chg_mod *ocm,
			       enum oplus_chg_mod_property ocm_prop,
			       union oplus_chg_mod_propval *val)
{
	if (atomic_read(&ocm->use_cnt) <= 0 || ocm_prop >= OPLUS_CHG_PROP_MAX) {
		if (!ocm->initialized)
			return -EAGAIN;
		return -ENODEV;
	}

	return ocm->desc->get_property(ocm, ocm_prop, val);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_get_property);

int oplus_chg_mod_set_property(struct oplus_chg_mod *ocm,
			       enum oplus_chg_mod_property ocm_prop,
			       const union oplus_chg_mod_propval *val)
{
	if (atomic_read(&ocm->use_cnt) <= 0 || !ocm->desc->set_property ||
	    ocm_prop >= OPLUS_CHG_PROP_MAX)
		return -ENODEV;

	return ocm->desc->set_property(ocm, ocm_prop, val);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_set_property);

int oplus_chg_mod_property_is_writeable(struct oplus_chg_mod *ocm,
					enum oplus_chg_mod_property ocm_prop)
{
	if (atomic_read(&ocm->use_cnt) <= 0 ||
	    !ocm->desc->property_is_writeable)
		return -ENODEV;

	return ocm->desc->property_is_writeable(ocm, ocm_prop);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_property_is_writeable);

int oplus_chg_mod_powers(struct oplus_chg_mod *ocm, struct device *dev)
{
	return sysfs_create_link(&ocm->dev.kobj, &dev->kobj, "oplus_chg");
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_powers);

static void oplus_chg_dev_release(struct device *dev)
{
	struct oplus_chg_mod *ocm = to_oplus_chg_mod(dev);
	dev_dbg(dev, "%s\n", __func__);
	kfree(ocm);
}

int oplus_chg_reg_changed_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&oplus_chg_changed_notifier, nb);
}
EXPORT_SYMBOL_GPL(oplus_chg_reg_changed_notifier);

void oplus_chg_unreg_changed_notifier(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&oplus_chg_changed_notifier, nb);
}
EXPORT_SYMBOL_GPL(oplus_chg_unreg_changed_notifier);

int oplus_chg_reg_event_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&oplus_chg_event_notifier, nb);
}
EXPORT_SYMBOL_GPL(oplus_chg_reg_event_notifier);

void oplus_chg_unreg_event_notifier(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&oplus_chg_event_notifier, nb);
}
EXPORT_SYMBOL_GPL(oplus_chg_unreg_event_notifier);

int oplus_chg_reg_mod_notifier(struct oplus_chg_mod *ocm,
			       struct notifier_block *nb)
{
	if (ocm == NULL)
		return -ENODEV;
	if (ocm->notifier == NULL) {
		pr_err("%s mod not support notifier\n", ocm->desc->name);
		return -EINVAL;
	}
	return atomic_notifier_chain_register(ocm->notifier, nb);
}
EXPORT_SYMBOL_GPL(oplus_chg_reg_mod_notifier);

void oplus_chg_unreg_mod_notifier(struct oplus_chg_mod *ocm,
				  struct notifier_block *nb)
{
	if (ocm == NULL)
		return;
	if (ocm->notifier == NULL) {
		pr_err("%s mod not support notifier\n", ocm->desc->name);
		return;
	}
	atomic_notifier_chain_unregister(ocm->notifier, nb);
}
EXPORT_SYMBOL_GPL(oplus_chg_unreg_mod_notifier);

void oplus_chg_global_event(struct oplus_chg_mod *owner_ocm,
			    enum oplus_chg_event events)
{
	atomic_notifier_call_chain(&oplus_chg_event_notifier, events,
				   owner_ocm);
}
EXPORT_SYMBOL_GPL(oplus_chg_global_event);

int oplus_chg_mod_event(struct oplus_chg_mod *ocm_receive,
			struct oplus_chg_mod *ocm_send,
			enum oplus_chg_event events)
{
	if (ocm_receive == NULL)
		return -ENODEV;
	if (ocm_receive->notifier == NULL) {
		pr_err("%s mod not support notifier\n",
		       ocm_receive->desc->name);
		return -EINVAL;
	}
	atomic_notifier_call_chain(ocm_receive->notifier, events, ocm_send);

	return 0;
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_event);

/* anonymous message */
int oplus_chg_anon_mod_event(struct oplus_chg_mod *ocm_receive,
			     enum oplus_chg_event events)
{
	if (ocm_receive == NULL)
		return -ENODEV;
	if (ocm_receive->notifier == NULL) {
		pr_err("%s mod not support notifier\n",
		       ocm_receive->desc->name);
		return -EINVAL;
	}
	atomic_notifier_call_chain(ocm_receive->notifier, events, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(oplus_chg_anon_mod_event);

static struct oplus_chg_mod *__must_check __oplus_chg_mod_register(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg, bool ws)
{
	struct device *dev;
	struct oplus_chg_mod *ocm;
	struct oplus_chg_mod *ocm_temp;
	int rc;

	if (!parent)
		pr_warn("%s: Expected proper parent device for '%s'\n",
			__func__, desc->name);

	if (!desc || !desc->name || !desc->properties || !desc->num_properties)
		return ERR_PTR(-EINVAL);

	ocm = kzalloc(sizeof(struct oplus_chg_mod), GFP_KERNEL);
	if (!ocm)
		return ERR_PTR(-ENOMEM);

	dev = &ocm->dev;

	device_initialize(dev);

	dev->class = NULL;
	dev->type = &oplus_chg_dev_type;
	dev->parent = parent;
	dev->release = oplus_chg_dev_release;
	dev_set_drvdata(dev, ocm);
	ocm->desc = desc;
	if (cfg) {
		ocm->drv_data = cfg->drv_data;
		ocm->of_node =
			cfg->fwnode ? to_of_node(cfg->fwnode) : cfg->of_node;
		ocm->supplied_to = cfg->supplied_to;
		ocm->num_supplicants = cfg->num_supplicants;
	}

	rc = dev_set_name(dev, "%s", desc->name);
	if (rc)
		goto dev_set_name_failed;

	INIT_WORK(&ocm->changed_work, oplus_chg_mod_changed_work);
	INIT_DELAYED_WORK(&ocm->deferred_register_work,
			  oplus_chg_mod_deferred_register_work);

	spin_lock_init(&ocm->changed_lock);
	rc = device_add(dev);
	if (rc)
		goto device_add_failed;

	rc = device_init_wakeup(dev, ws);
	if (rc)
		goto wakeup_init_failed;

	mutex_lock(&mod_list_lock);
	list_for_each_entry(ocm_temp, &mod_list, list) {
		if (ocm_temp->desc->name == NULL)
			continue;
		if (strcmp(ocm_temp->desc->name, ocm->desc->name) == 0) {
			pr_err("device with the same name already exists\n");
			mutex_unlock(&mod_list_lock);
			rc = -EINVAL;
			goto add_to_list_failed;
		}
	}
	list_add(&ocm->list, &mod_list);
	mutex_unlock(&mod_list_lock);

	atomic_inc(&ocm->use_cnt);
	ocm->initialized = true;

	queue_delayed_work(system_power_efficient_wq,
			   &ocm->deferred_register_work,
			   OPLUS_CHG_DEFERRED_REGISTER_TIME);

	return ocm;

add_to_list_failed:
	device_init_wakeup(&ocm->dev, false);
wakeup_init_failed:
device_add_failed:
dev_set_name_failed:
	put_device(dev);
	return ERR_PTR(rc);
}

struct oplus_chg_mod *__must_check oplus_chg_mod_register(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg)
{
	return __oplus_chg_mod_register(parent, desc, cfg, true);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_register);

/**
 * oplus_chg_mod_register_no_ws() - Register new non-waking-source power supply
 * @parent:	Device to be a parent of power supply's device, usually
 *		the device which probe function calls this
 * @desc:	Description of power supply, must be valid through whole
 *		lifetime of this power supply
 * @cfg:	Run-time specific configuration accessed during registering,
 *		may be NULL
 *
 * Return: A pointer to newly allocated oplus_chg_mod on success
 * or ERR_PTR otherwise.
 * Use oplus_chg_mod_unregister() on returned oplus_chg_mod pointer to release
 * resources.
 */
struct oplus_chg_mod *__must_check oplus_chg_mod_register_no_ws(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg)
{
	return __oplus_chg_mod_register(parent, desc, cfg, false);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_register_no_ws);

static void devm_oplus_chg_mod_release(struct device *dev, void *res)
{
	struct oplus_chg_mod **ocm = res;

	oplus_chg_mod_unregister(*ocm);
}

/**
 * devm_oplus_chg_mod_register() - Register managed power supply
 * @parent:	Device to be a parent of power supply's device, usually
 *		the device which probe function calls this
 * @desc:	Description of power supply, must be valid through whole
 *		lifetime of this power supply
 * @cfg:	Run-time specific configuration accessed during registering,
 *		may be NULL
 *
 * Return: A pointer to newly allocated oplus_chg_mod on success
 * or ERR_PTR otherwise.
 * The returned oplus_chg_mod pointer will be automatically unregistered
 * on driver detach.
 */
struct oplus_chg_mod *__must_check devm_oplus_chg_mod_register(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg)
{
	struct oplus_chg_mod **ptr, *ocm;

	ptr = devres_alloc(devm_oplus_chg_mod_release, sizeof(*ptr),
			   GFP_KERNEL);

	if (!ptr)
		return ERR_PTR(-ENOMEM);
	ocm = __oplus_chg_mod_register(parent, desc, cfg, true);
	if (IS_ERR(ocm)) {
		devres_free(ptr);
	} else {
		*ptr = ocm;
		devres_add(parent, ptr);
	}
	return ocm;
}
EXPORT_SYMBOL_GPL(devm_oplus_chg_mod_register);

/**
 * devm_oplus_chg_mod_register_no_ws() - Register managed non-waking-source power supply
 * @parent:	Device to be a parent of power supply's device, usually
 *		the device which probe function calls this
 * @desc:	Description of power supply, must be valid through whole
 *		lifetime of this power supply
 * @cfg:	Run-time specific configuration accessed during registering,
 *		may be NULL
 *
 * Return: A pointer to newly allocated oplus_chg_mod on success
 * or ERR_PTR otherwise.
 * The returned oplus_chg_mod pointer will be automatically unregistered
 * on driver detach.
 */
struct oplus_chg_mod *__must_check devm_oplus_chg_mod_register_no_ws(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg)
{
	struct oplus_chg_mod **ptr, *ocm;

	ptr = devres_alloc(devm_oplus_chg_mod_release, sizeof(*ptr),
			   GFP_KERNEL);

	if (!ptr)
		return ERR_PTR(-ENOMEM);
	ocm = __oplus_chg_mod_register(parent, desc, cfg, false);
	if (IS_ERR(ocm)) {
		devres_free(ptr);
	} else {
		*ptr = ocm;
		devres_add(parent, ptr);
	}
	return ocm;
}
EXPORT_SYMBOL_GPL(devm_oplus_chg_mod_register_no_ws);

/**
 * oplus_chg_mod_unregister() - Remove this power supply from system
 * @ocm:	Pointer to power supply to unregister
 *
 * Remove this power supply from the system. The resources of power supply
 * will be freed here or on last oplus_chg_mod_put() call.
 */
void oplus_chg_mod_unregister(struct oplus_chg_mod *ocm)
{
	mutex_lock(&mod_list_lock);
	list_del(&ocm->list);
	mutex_unlock(&mod_list_lock);
	WARN_ON(atomic_dec_return(&ocm->use_cnt));
	ocm->removing = true;
	cancel_work_sync(&ocm->changed_work);
	cancel_delayed_work_sync(&ocm->deferred_register_work);
	device_init_wakeup(&ocm->dev, false);
	device_unregister(&ocm->dev);
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_unregister);

void *oplus_chg_mod_get_drvdata(struct oplus_chg_mod *ocm)
{
	return ocm->drv_data;
}
EXPORT_SYMBOL_GPL(oplus_chg_mod_get_drvdata);

static int __init oplus_chg_class_init(void)
{
	int rc;
#ifdef MODULE
	int module_num, i;
	struct oplus_chg_module *first_module;
	struct oplus_chg_module *oplus_module;
#endif
#if __and(IS_MODULE(CONFIG_OPLUS_CHG), IS_MODULE(CONFIG_OPLUS_CHG_V2))
	struct device_node *node;

	node = of_find_node_by_path("/soc/oplus_chg_core");
	if (node != NULL &&
	    of_property_read_bool(node, "oplus,chg_framework_v2"))
		return 0;
#endif /* CONFIG_OPLUS_CHG_V2 */

#ifdef MODULE
	module_num = oplus_chg_get_module_num();
	if (module_num == 0) {
		pr_err("oplus chg module not found, please check oplus_chg_module.lds\n");
		goto end;
	} else {
		pr_info("find %d oplus chg module\n", module_num);
	}
	first_module = oplus_chg_find_first_module();
	for (i = 0; i < module_num; i++) {
		oplus_module = &first_module[i];
		if ((oplus_module->magic == OPLUS_CHG_MODEL_MAGIC) &&
		    (oplus_module->chg_module_init != NULL)) {
			pr_info("%s init\n", oplus_module->name);
			rc = oplus_module->chg_module_init();
			if (rc < 0) {
				pr_err("%s init error, rc=%d\n",
				       oplus_module->name, rc);
				goto module_init_err;
			}
		}
	}

end:
#endif /* MODULE */

	return 0;

#ifdef MODULE
module_init_err:
	for (i = i - 1; i >= 0; i--) {
		oplus_module = &first_module[i];
		if ((oplus_module->magic == OPLUS_CHG_MODEL_MAGIC) &&
		    (oplus_module->chg_module_exit != NULL))
			oplus_module->chg_module_exit();
	}
#endif /* MODULE */
	return rc;
}

static void __exit oplus_chg_class_exit(void)
{
#ifdef MODULE
	int module_num, i;
	struct oplus_chg_module *first_module;
	struct oplus_chg_module *oplus_module;

	module_num = oplus_chg_get_module_num();
	first_module = oplus_chg_find_first_module();
	for (i = module_num - 1; i >= 0; i--) {
		oplus_module = &first_module[i];
		if ((oplus_module->magic == OPLUS_CHG_MODEL_MAGIC) &&
		    (oplus_module->chg_module_exit != NULL))
			oplus_module->chg_module_exit();
	}
#endif /* MODULE */
}

subsys_initcall(oplus_chg_class_init);
module_exit(oplus_chg_class_exit);

MODULE_DESCRIPTION("oplus charge management subsystem");
MODULE_AUTHOR("Nick Hu <nick.hu>");
MODULE_LICENSE("GPL");
