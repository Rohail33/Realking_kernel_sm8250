// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[WLS_FAST]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include "../../oplus_chg_core.h"
#endif
#include "../../oplus_chg_module.h"
#include "oplus_chg_ic.h"
#include "../oplus_chg_wls.h"

static bool is_fast_ic_available(struct oplus_wls_chg_fast *wls_fast)
{
	struct device_node *node = wls_fast->dev->of_node;

	if(wls_fast->fast_ic == NULL)
		wls_fast->fast_ic = of_get_oplus_chg_ic(node, "oplus,fast_ic");
	return !!wls_fast->fast_ic;
}

int oplus_chg_wls_fast_set_enable(struct oplus_wls_chg_fast *wls_fast, bool en)
{
	struct oplus_chg_ic_dev *fast_ic;
	struct oplus_chg_ic_cp_ops *fast_ic_ops;
	int rc;

	if (wls_fast == NULL) {
		pr_err("wls_fast is NULL\n");
		return -ENODEV;
	}
	if (!is_fast_ic_available(wls_fast)) {
		pr_err("fast_ic is NULL\n");
		return -ENODEV;
	}

	fast_ic = wls_fast->fast_ic;
	fast_ic_ops = fast_ic->dev_ops;
	rc = fast_ic_ops->cp_set_enable(fast_ic, en);

	return rc;
}

int oplus_chg_wls_fast_start(struct oplus_wls_chg_fast *wls_fast)
{
	struct oplus_chg_ic_dev *fast_ic;
	struct oplus_chg_ic_cp_ops *fast_ic_ops;
	int rc;

	if (wls_fast == NULL) {
		pr_err("wls_fast is NULL\n");
		return -ENODEV;
	}
	if (!is_fast_ic_available(wls_fast)) {
		pr_err("fast_ic is NULL\n");
		return -ENODEV;
	}

	fast_ic = wls_fast->fast_ic;
	fast_ic_ops = fast_ic->dev_ops;
	rc = fast_ic_ops->cp_start(fast_ic);

	return rc;
}

int oplus_chg_wls_fast_smt_test(struct oplus_wls_chg_fast *wls_fast)
{
	struct oplus_chg_ic_dev *fast_ic;
	struct oplus_chg_ic_cp_ops *fast_ic_ops;
	int rc;

	if (wls_fast == NULL) {
		pr_err("wls_fast is NULL\n");
		return -ENODEV;
	}
	if (!is_fast_ic_available(wls_fast)) {
		pr_err("fast_ic is NULL\n");
		return -ENODEV;
	}

	fast_ic = wls_fast->fast_ic;
	fast_ic_ops = fast_ic->dev_ops;
	rc = fast_ic_ops->cp_start(fast_ic);

	return rc;
}

int oplus_chg_wls_fast_get_fault(struct oplus_wls_chg_fast *wls_fast, char *fault)
{
	struct oplus_chg_ic_dev *fast_ic;
	struct oplus_chg_ic_cp_ops *fast_ic_ops;
	int rc;

	if (wls_fast == NULL) {
		pr_err("wls_fast is NULL\n");
		return -ENODEV;
	}

	if (!is_fast_ic_available(wls_fast)) {
		pr_err("fast_ic is NULL\n");
		return -ENODEV;
	}

	if (fault == NULL) {
		pr_err("fault is NULL");
		return -ENODEV;
	}

	fast_ic = wls_fast->fast_ic;
	fast_ic_ops = fast_ic->dev_ops;
	rc = fast_ic_ops->cp_get_fault(fast_ic, fault);

	return rc;
}

int oplus_chg_wls_fast_init(struct oplus_chg_wls *wls_dev)
{
	struct oplus_wls_chg_fast *wls_fast;

	wls_fast = devm_kzalloc(wls_dev->dev, sizeof(struct oplus_wls_chg_fast), GFP_KERNEL);
	if (wls_fast == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}
	wls_dev->wls_fast = wls_fast;
	wls_fast->dev = wls_dev->dev;
	wls_fast->wls_dev = wls_dev;

	return 0;
}

int oplus_chg_wls_fast_remove(struct oplus_chg_wls *wls_dev)
{
	struct oplus_wls_chg_fast *wls_fast = wls_dev->wls_fast;

	devm_kfree(wls_dev->dev, wls_fast);
	wls_dev->wls_fast = NULL;

	return 0;
}
