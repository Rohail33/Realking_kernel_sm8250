// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[WLS_NOR]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
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
#include "../../oplus_charger.h"

extern struct oplus_chg_chip *g_oplus_chip;
extern bool oplus_get_wired_chg_present(void);

static bool is_nor_ic_available(struct oplus_wls_chg_normal *wls_nor)
{
	struct device_node *node = wls_nor->dev->of_node;

	if(wls_nor->nor_ic == NULL)
		wls_nor->nor_ic = of_get_oplus_chg_ic(node, "oplus,normal_ic");
	return !!wls_nor->nor_ic;
}

/*static bool is_batt_ocm_available(struct oplus_wls_chg_normal *wls_nor)
{
	if (!wls_nor->batt_ocm)
		wls_nor->batt_ocm = oplus_chg_mod_get_by_name("battery");
	return !!wls_nor->batt_ocm;
}*/

__maybe_unused static bool oplus_chg_is_usb_present(struct oplus_wls_chg_normal *wls_nor)
{
	return oplus_get_wired_chg_present();
}

static int get_batt_cell_num(struct oplus_wls_chg_normal *wls_nor)
{
	if (!g_oplus_chip) {
		pr_err("g_oplus_chip is null\n");
		return 1;
	}

	return g_oplus_chip->vbatt_num;
/*
	int rc;
	union oplus_chg_mod_propval pval;

	if (!is_batt_ocm_available(wls_nor)) {
		pr_err("batt ocm not found\n");
		return 1;
	}
	rc = oplus_chg_mod_get_property(wls_nor->batt_ocm, OPLUS_CHG_PROP_CELL_NUM, &pval);
	if (rc < 0)
		return 1;

	return pval.intval;
*/
}

int oplus_chg_wls_nor_set_input_enable(struct oplus_wls_chg_normal *wls_nor, bool en)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_set_input_enable(nor_ic, en);

	return rc;
}

int oplus_chg_wls_nor_set_output_enable(struct oplus_wls_chg_normal *wls_nor, bool en)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}
	if (oplus_chg_is_usb_present(wls_nor)) {
		pr_debug("usb present, exit\n");
		return -EPERM;
	}
	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_set_output_enable(nor_ic, en);

	return rc;
}

int oplus_chg_wls_nor_set_icl(struct oplus_wls_chg_normal *wls_nor, int icl_ma)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	pr_err("set icl to %d ma\n", icl_ma);

	mutex_lock(&wls_nor->icl_lock);
	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_set_icl(nor_ic, icl_ma);
	if (rc < 0) {
		pr_err("set icl to %d mA error, rc=%d\n", icl_ma, rc);
		mutex_unlock(&wls_nor->icl_lock);
		return rc;
	}
	
	wls_nor->icl_set_ma = icl_ma;
	mutex_unlock(&wls_nor->icl_lock);

	return 0;
}

static void oplus_chg_wls_nor_icl_set_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_wls_chg_normal *wls_nor = container_of(dwork, struct oplus_wls_chg_normal, icl_set_work);
	int icl_next_ma;
	int rc;

	if (!wls_nor->wls_dev->wls_status.rx_present)
		return;
	if (wls_nor->clean_source)
		return;

	mutex_lock(&wls_nor->icl_lock);
	if (wls_nor->icl_target_ma >= wls_nor->icl_set_ma) {
		icl_next_ma = wls_nor->icl_set_ma + wls_nor->icl_step_ma;
		if (icl_next_ma > wls_nor->icl_target_ma)
			icl_next_ma = wls_nor->icl_target_ma;
	} else {
		icl_next_ma = wls_nor->icl_set_ma - wls_nor->icl_step_ma;
		if (icl_next_ma < wls_nor->icl_target_ma)
			icl_next_ma = wls_nor->icl_target_ma;
	}
	mutex_unlock(&wls_nor->icl_lock);
	rc = oplus_chg_wls_nor_set_icl(wls_nor, icl_next_ma);
	if (rc < 0) {
		pr_err("set icl to %d mA error, rc=%d\n", icl_next_ma, rc);
		return;
	}
	if (icl_next_ma != wls_nor->icl_target_ma) {
		if (!wls_nor->wls_dev->wls_status.rx_present) {
			return;
		}
		schedule_delayed_work(&wls_nor->icl_set_work, msecs_to_jiffies(100));
		return;
	}
}

int oplus_chg_wls_nor_set_icl_by_step(struct oplus_wls_chg_normal *wls_nor, int icl_ma, int step_ma, bool block)
{
	struct oplus_chg_ic_dev *nor_ic;
	int icl_next_ma;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}
	nor_ic = wls_nor->nor_ic;

	if (block) {
		mutex_lock(&wls_nor->icl_lock);
		wls_nor->icl_target_ma = icl_ma;
		wls_nor->icl_step_ma = step_ma;
		mutex_unlock(&wls_nor->icl_lock);
next_step:
		mutex_lock(&wls_nor->icl_lock);
		if (icl_ma >= wls_nor->icl_set_ma) {
			icl_next_ma = wls_nor->icl_set_ma + step_ma;
			if (icl_next_ma > icl_ma)
				icl_next_ma = icl_ma;
		} else {
			icl_next_ma = wls_nor->icl_set_ma - step_ma;
			if (icl_next_ma < icl_ma)
				icl_next_ma = icl_ma;
		}
		mutex_unlock(&wls_nor->icl_lock);
		rc = oplus_chg_wls_nor_set_icl(wls_nor, icl_next_ma);
		if (rc < 0) {
			pr_err("set icl to %d mA error, rc=%d\n", icl_next_ma, rc);
			return rc;
		}
		if (wls_nor->clean_source) {
			return -EINVAL;
		}
		if (icl_next_ma != icl_ma) {
			msleep(100);
			goto next_step;
		}
	} else {
		mutex_lock(&wls_nor->icl_lock);
		wls_nor->icl_target_ma = icl_ma;
		wls_nor->icl_step_ma = step_ma;
		mutex_unlock(&wls_nor->icl_lock);
		schedule_delayed_work(&wls_nor->icl_set_work, 0);
	}

	return 0;
}

int oplus_chg_wls_nor_set_fcc(struct oplus_wls_chg_normal *wls_nor, int fcc_ma)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}
	if (oplus_chg_is_usb_present(wls_nor)) {
		pr_debug("usb present, exit\n");
		return -EPERM;
	}
	mutex_lock(&wls_nor->fcc_lock);
	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_set_fcc(nor_ic, fcc_ma);
	if (rc < 0) {
		pr_err("set fcc to %d mA error, rc=%d\n", fcc_ma, rc);
		mutex_unlock(&wls_nor->fcc_lock);
		return rc;
	}

	wls_nor->fcc_set_ma = fcc_ma;
	mutex_unlock(&wls_nor->fcc_lock);

	return 0;
}

static void oplus_chg_wls_nor_fcc_set_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_wls_chg_normal *wls_nor = container_of(dwork, struct oplus_wls_chg_normal, fcc_set_work);
	int fcc_next_ma;
	int rc;

	if (!wls_nor->wls_dev->wls_status.rx_present)
		return;
	if (wls_nor->clean_source)
		return;

	mutex_lock(&wls_nor->fcc_lock);
	if (wls_nor->fcc_target_ma >= wls_nor->fcc_set_ma) {
		fcc_next_ma = wls_nor->fcc_set_ma + wls_nor->fcc_step_ma;
		if (fcc_next_ma > wls_nor->fcc_target_ma)
			fcc_next_ma = wls_nor->fcc_target_ma;
	} else {
		fcc_next_ma = wls_nor->fcc_set_ma - wls_nor->fcc_step_ma;
		if (fcc_next_ma < wls_nor->fcc_target_ma)
			fcc_next_ma = wls_nor->fcc_target_ma;
	}
	mutex_unlock(&wls_nor->fcc_lock);
	rc = oplus_chg_wls_nor_set_fcc(wls_nor, fcc_next_ma);
	if (rc < 0) {
		pr_err("set fcc to %d mA error, rc=%d\n", fcc_next_ma, rc);
		return;
	}
	if (fcc_next_ma != wls_nor->fcc_target_ma) {
		if (!wls_nor->wls_dev->wls_status.rx_present) {
			return;
		}
		schedule_delayed_work(&wls_nor->fcc_set_work, msecs_to_jiffies(100));
		return;
	}
}

int oplus_chg_wls_nor_set_fcc_by_step(struct oplus_wls_chg_normal *wls_nor, int fcc_ma, int step_ma, bool block)
{
	struct oplus_chg_ic_dev *nor_ic;
	int fcc_next_ma;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}
	nor_ic = wls_nor->nor_ic;

	if (block) {
		mutex_lock(&wls_nor->fcc_lock);
		wls_nor->fcc_target_ma = fcc_ma;
		wls_nor->fcc_step_ma = step_ma;
		mutex_unlock(&wls_nor->fcc_lock);
next_step:
		mutex_lock(&wls_nor->fcc_lock);
		if (fcc_ma >= wls_nor->fcc_set_ma) {
			fcc_next_ma = wls_nor->fcc_set_ma + step_ma;
			if (fcc_next_ma > fcc_ma)
				fcc_next_ma = fcc_ma;
		} else {
			fcc_next_ma = wls_nor->fcc_set_ma - step_ma;
			if (fcc_next_ma < fcc_ma)
				fcc_next_ma = fcc_ma;
		}
		mutex_unlock(&wls_nor->fcc_lock);
		rc = oplus_chg_wls_nor_set_fcc(wls_nor, fcc_next_ma);
		if (rc < 0) {
			pr_err("set fcc to %d mA error, rc=%d\n", fcc_next_ma, rc);
			return rc;
		}
		if (wls_nor->clean_source) {
			return -EINVAL;
		}
		if (fcc_next_ma != fcc_ma) {
			msleep(100);
			goto next_step;
		}
	} else {
		mutex_lock(&wls_nor->fcc_lock);
		wls_nor->fcc_target_ma = fcc_ma;
		wls_nor->fcc_step_ma = step_ma;
		mutex_unlock(&wls_nor->fcc_lock);
		schedule_delayed_work(&wls_nor->fcc_set_work, 0);
	}

	return 0;
}

int oplus_chg_wls_nor_set_fv(struct oplus_wls_chg_normal *wls_nor, int fv_mv)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int cell_num;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	cell_num = get_batt_cell_num(wls_nor);
	rc = nor_ic_ops->chg_set_fv(nor_ic, fv_mv * cell_num);
	if (rc < 0) {
		pr_err("set fv to %d mV error, rc=%d\n", fv_mv * cell_num, rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_set_rechg_vol(struct oplus_wls_chg_normal *wls_nor, int rechg_vol_mv)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	if (nor_ic_ops->chg_set_rechg_vol == NULL)
		return 0;
	rc = nor_ic_ops->chg_set_rechg_vol(nor_ic, rechg_vol_mv);
	if (rc < 0) {
		pr_err("set rechg_vol to %d mV error, rc=%d\n", rechg_vol_mv, rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_get_icl(struct oplus_wls_chg_normal *wls_nor, int *icl_ma)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_get_icl(nor_ic, icl_ma);
	if (rc < 0) {
		pr_err("get icl error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_get_input_curr(struct oplus_wls_chg_normal *wls_nor, int *curr_ma)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_get_input_curr(nor_ic, curr_ma);
	if (rc < 0) {
		pr_err("get input current error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_get_input_vol(struct oplus_wls_chg_normal *wls_nor, int *vol_mv)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_get_input_vol(nor_ic, vol_mv);
	if (rc < 0) {
		pr_err("get input voltage error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_set_boost_en(struct oplus_wls_chg_normal *wls_nor, bool en)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	rc = nor_ic_ops->chg_set_boost_en(nor_ic, en);
	if (rc < 0) {
		pr_err("set boost %s error, rc=%d\n", en ? "enable" : "disable", rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_set_boost_vol(struct oplus_wls_chg_normal *wls_nor, int vol_mv)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	if (nor_ic_ops->chg_set_boost_vol == NULL)
		return 0;
	rc = nor_ic_ops->chg_set_boost_vol(nor_ic, vol_mv);
	if (rc < 0) {
		pr_err("set boost vol to %d mV, rc=%d\n", vol_mv, rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_set_boost_curr_limit(struct oplus_wls_chg_normal *wls_nor, int curr_ma)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	if (nor_ic_ops->chg_set_boost_curr_limit == NULL)
		return 0;
	rc = nor_ic_ops->chg_set_boost_curr_limit(nor_ic, curr_ma);
	if (rc < 0) {
		pr_err("set boost curr limit to %d mA, rc=%d\n", curr_ma, rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_set_aicl_enable(struct oplus_wls_chg_normal *wls_nor, bool en)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	if (nor_ic_ops->chg_set_aicl_enable == NULL)
		return 0;
	rc = nor_ic_ops->chg_set_aicl_enable(nor_ic, en);
	if (rc < 0) {
		pr_err("can't %s aicl, rc=%d\n", en ? "enable" : "disable", rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_set_aicl_rerun(struct oplus_wls_chg_normal *wls_nor)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	if (nor_ic_ops->chg_set_aicl_rerun == NULL)
		return 0;
	rc = nor_ic_ops->chg_set_aicl_rerun(nor_ic);
	if (rc < 0) {
		pr_err("can't rerun aicl, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int oplus_chg_wls_nor_set_vindpm(struct oplus_wls_chg_normal *wls_nor, int vindpm_mv)
{
	struct oplus_chg_ic_dev *nor_ic;
	struct oplus_chg_ic_buck_ops *nor_ic_ops;
	int rc = 0;

	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}
	if (!is_nor_ic_available(wls_nor)) {
		pr_err("nor_ic is NULL\n");
		return -ENODEV;
	}

	nor_ic = wls_nor->nor_ic;
	nor_ic_ops = nor_ic->dev_ops;
	if (nor_ic_ops->chg_set_vindpm)
		rc = nor_ic_ops->chg_set_vindpm(nor_ic, vindpm_mv);

	return rc;
}

int oplus_chg_wls_nor_clean_source(struct oplus_wls_chg_normal *wls_nor)
{
	if (wls_nor == NULL) {
		pr_err("wls_nor is NULL\n");
		return -ENODEV;
	}

	wls_nor->clean_source = true;
	cancel_delayed_work_sync(&wls_nor->icl_set_work);
	cancel_delayed_work_sync(&wls_nor->fcc_set_work);
	mutex_lock(&wls_nor->icl_lock);
	wls_nor->icl_target_ma = 0;
	wls_nor->icl_step_ma = 0;
	wls_nor->icl_set_ma = 0;
	mutex_unlock(&wls_nor->icl_lock);
	mutex_lock(&wls_nor->fcc_lock);
	wls_nor->fcc_target_ma = 0;
	wls_nor->fcc_step_ma = 0;
	wls_nor->fcc_set_ma = 0;
	mutex_unlock(&wls_nor->fcc_lock);
	wls_nor->clean_source = false;

	return 0;
}

int oplus_chg_wls_nor_init(struct oplus_chg_wls *wls_dev)
{
	struct oplus_wls_chg_normal *wls_nor;

	wls_nor = devm_kzalloc(wls_dev->dev, sizeof(struct oplus_wls_chg_normal), GFP_KERNEL);
	if (wls_nor == NULL) {
		pr_err("alloc memory error\n");
		return -ENOMEM;
	}
	wls_dev->wls_nor = wls_nor;
	wls_nor->dev = wls_dev->dev;
	wls_nor->wls_dev = wls_dev;

	INIT_DELAYED_WORK(&wls_nor->icl_set_work, oplus_chg_wls_nor_icl_set_work);
	INIT_DELAYED_WORK(&wls_nor->fcc_set_work, oplus_chg_wls_nor_fcc_set_work);
	mutex_init(&wls_nor->icl_lock);
	mutex_init(&wls_nor->fcc_lock);

	return 0;
}

int oplus_chg_wls_nor_remove(struct oplus_chg_wls *wls_dev)
{
	struct oplus_wls_chg_normal *wls_nor = wls_dev->wls_nor;

	devm_kfree(wls_dev->dev, wls_nor);
	wls_dev->wls_nor = NULL;

	return 0;
}
