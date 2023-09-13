// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[VIRTUAL_GAUGE]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>

struct oplus_virtual_gauge_child {
	struct oplus_chg_ic_dev *ic_dev;
	int index;
	enum oplus_chg_ic_func *funcs;
	int func_num;
	enum oplus_chg_ic_virq_id *virqs;
	int virq_num;

	int batt_auth;
	int batt_hmac;
};

struct oplus_virtual_gauge_ic {
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	int child_num;
	struct oplus_virtual_gauge_child *child_list;

	struct work_struct gauge_online_work;
	struct work_struct gauge_offline_work;
	struct work_struct gauge_resume_work;

	int batt_capacity_mah;
};

static int oplus_chg_vg_virq_register(struct oplus_virtual_gauge_ic *chip);

static inline bool func_is_support(struct oplus_virtual_gauge_child *ic,
				   enum oplus_chg_ic_func func_id)
{
	switch (ic->func_num) {
	case OPLUS_IC_FUNC_INIT:
	case OPLUS_IC_FUNC_EXIT:
		return true; /* must support */
	default:
		break;
	}

	if (ic->func_num > 0)
		return oplus_chg_ic_func_is_support(ic->funcs, ic->func_num,
						    func_id);
	else
		return false;
}

static inline bool virq_is_support(struct oplus_virtual_gauge_child *ic,
				   enum oplus_chg_ic_virq_id virq_id)
{
	switch (ic->virq_num) {
	case OPLUS_IC_VIRQ_ERR:
	case OPLUS_IC_VIRQ_ONLINE:
	case OPLUS_IC_VIRQ_OFFLINE:
		return true; /* must support */
	default:
		break;
	}

	if (ic->virq_num > 0)
		return oplus_chg_ic_virq_is_support(ic->virqs, ic->virq_num,
						    virq_id);
	else
		return false;
}

static int oplus_chg_vg_child_funcs_init(struct oplus_virtual_gauge_ic *chip,
					 int child_num)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *func_node = NULL;
	int i, m;
	int rc = 0;

	for (i = 0; i < child_num; i++) {
		func_node =
			of_parse_phandle(node, "oplus,gauge_ic_func_group", i);
		if (func_node == NULL) {
			chg_err("can't get ic[%d] function group\n", i);
			rc = -ENODATA;
			goto err;
		}
		rc = of_property_count_elems_of_size(func_node, "functions",
						     sizeof(u32));
		if (rc < 0) {
			chg_err("can't get ic[%d] functions size, rc=%d\n", i,
				rc);
			goto err;
		}
		chip->child_list[i].func_num = rc;
		chip->child_list[i].funcs =
			devm_kzalloc(chip->dev,
				     sizeof(enum oplus_chg_ic_func) *
					     chip->child_list[i].func_num,
				     GFP_KERNEL);
		if (chip->child_list[i].funcs == NULL) {
			rc = -ENOMEM;
			chg_err("alloc child ic funcs memory error\n");
			goto err;
		}
		rc = of_property_read_u32_array(
			func_node, "functions",
			(u32 *)chip->child_list[i].funcs,
			chip->child_list[i].func_num);
		if (rc) {
			i++;
			chg_err("can't get ic[%d] functions, rc=%d\n", i, rc);
			goto err;
		}
		(void)oplus_chg_ic_func_table_sort(
			chip->child_list[i].funcs,
			chip->child_list[i].func_num);
	}

	return 0;

err:
	for (m = i; m > 0; m--)
		devm_kfree(chip->dev, chip->child_list[m - 1].funcs);
	return rc;
}

static int oplus_chg_vg_child_virqs_init(struct oplus_virtual_gauge_ic *chip,
					 int child_num)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *virq_node = NULL;
	int i, m;
	int rc = 0;

	for (i = 0; i < child_num; i++) {
		virq_node =
			of_parse_phandle(node, "oplus,gauge_ic_func_group", i);
		if (virq_node == NULL) {
			chg_err("can't get ic[%d] function group\n", i);
			rc = -ENODATA;
			goto err;
		}
		rc = of_property_count_elems_of_size(virq_node, "virqs",
						     sizeof(u32));
		if (rc <= 0) {
			chip->child_list[i].virq_num = 0;
			chip->child_list[i].virqs = NULL;
			continue;
		}
		chip->child_list[i].virq_num = rc;
		chip->child_list[i].virqs =
			devm_kzalloc(chip->dev,
				     sizeof(enum oplus_chg_ic_func) *
					     chip->child_list[i].virq_num,
				     GFP_KERNEL);
		if (chip->child_list[i].virqs == NULL) {
			rc = -ENOMEM;
			chg_err("alloc child ic virqs memory error\n");
			goto err;
		}
		rc = of_property_read_u32_array(
			virq_node, "virqs", (u32 *)chip->child_list[i].virqs,
			chip->child_list[i].virq_num);
		if (rc) {
			i++;
			chg_err("can't get ic[%d] virqs, rc=%d\n", i, rc);
			goto err;
		}
	}

	return 0;

err:
	for (m = i; m > 0; m--) {
		if (chip->child_list[m - 1].virqs != NULL)
			devm_kfree(chip->dev, chip->child_list[m - 1].virqs);
	}
	return rc;
}

static int oplus_chg_vg_child_init(struct oplus_virtual_gauge_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	int i;
	int rc = 0;

	rc = of_property_count_elems_of_size(node, "oplus,gauge_ic",
					     sizeof(u32));
	if (rc < 0) {
		chg_err("can't get gauge ic number, rc=%d\n", rc);
		return rc;
	}
	chip->child_num = rc;
	chip->child_list = devm_kzalloc(
		chip->dev,
		sizeof(struct oplus_virtual_gauge_child) * chip->child_num,
		GFP_KERNEL);
	if (chip->child_list == NULL) {
		rc = -ENOMEM;
		chg_err("alloc child ic memory error\n");
		return rc;
	}

	for (i = 0; i < chip->child_num; i++) {
		chip->child_list[i].batt_auth = -1;
		chip->child_list[i].batt_hmac = -1;
		chip->child_list[i].ic_dev =
			of_get_oplus_chg_ic(node, "oplus,gauge_ic", i);
		if (chip->child_list[i].ic_dev == NULL) {
			chg_debug("not find gauge ic %d\n", i);
			rc = -EAGAIN;
			goto read_property_err;
		}
	}

	rc = oplus_chg_vg_child_funcs_init(chip, chip->child_num);
	if (rc < 0)
		goto child_funcs_init_err;
	rc = oplus_chg_vg_child_virqs_init(chip, chip->child_num);
	if (rc < 0)
		goto child_virqs_init_err;

	return 0;

child_virqs_init_err:
	for (i = 0; i < chip->child_num; i++)
		devm_kfree(chip->dev, chip->child_list[i].funcs);
child_funcs_init_err:
read_property_err:
	for (; i >=0; i--)
		chip->child_list[i].ic_dev = NULL;
	devm_kfree(chip->dev, chip->child_list);
	return rc;
}

static int oplus_chg_vg_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_gauge_ic *chip;
	int i, m;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	rc = oplus_chg_vg_child_init(chip);
	if (rc < 0) {
		if (rc != -EAGAIN)
			chg_err("child list init error, rc=%d\n", rc);
		goto child_list_init_err;
	}

	rc = oplus_chg_vg_virq_register(chip);
	if (rc < 0) {
		chg_err("virq register error, rc=%d\n", rc);
		goto virq_register_err;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_INIT);
		if (rc < 0) {
			chg_err("child ic[%d] init error, rc=%d\n", i, rc);
			goto child_init_err;
		}
	}

	ic_dev->online = true;

	return 0;

child_init_err:
	for (m = i + 1; m > 0; m--)
		oplus_chg_ic_func(chip->child_list[m - 1].ic_dev,
				  OPLUS_IC_FUNC_EXIT);
virq_register_err:
child_list_init_err:

	return rc;
}

static int oplus_chg_vg_exit(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_EXIT);
		if (rc < 0)
			chg_err("child ic[%d] exit error, rc=%d\n", i, rc);
	}
	for (i = 0; i < chip->child_num; i++) {
		if (virq_is_support(&chip->child_list[i], OPLUS_IC_VIRQ_ERR)) {
			oplus_chg_ic_virq_release(chip->child_list[i].ic_dev,
						  OPLUS_IC_VIRQ_ERR, chip);
		}
	}
	for (i = 0; i < chip->child_num; i++) {
		if (chip->child_list[i].virqs != NULL)
			devm_kfree(chip->dev, chip->child_list[i].virqs);
	}
	for (i = 0; i < chip->child_num; i++)
		devm_kfree(chip->dev, chip->child_list[i].funcs);

	return 0;
}

static int oplus_chg_vg_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_REG_DUMP);
		if (rc < 0)
			chg_err("child ic[%d] exit error, rc=%d\n", i, rc);
	}

	return rc;
}

static int oplus_chg_vg_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	struct oplus_virtual_gauge_ic *chip;
	int i, index;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	index = 0;
	for (i = 0; i < chip->child_num; i++) {
		if (index >= len)
			return len;
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_SMT_TEST, buf + index,
				       len - index);
		if (rc < 0) {
			if (rc != -ENOTSUPP) {
				chg_err("child ic[%d] smt test error, rc=%d\n",
					i, rc);
				rc = snprintf(buf + index, len - index,
					"[%s]-[%s]:%d\n",
					chip->child_list[i].ic_dev->manu_name,
					"FUNC_ERR", rc);
			} else {
				rc = 0;
			}
		} else {
			if ((rc > 0) && buf[index + rc - 1] != '\n') {
				buf[index + rc] = '\n';
				index++;
			}
		}
		index += rc;
	}

	return index;
}

static int oplus_chg_vg_gauge_update(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_UPDATE)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_UPDATE);
		if (rc < 0)
			chg_err("child ic[%d] update gauge info error, rc=%d\n",
				i, rc);
	}

	return rc;
}

static int oplus_chg_vg_get_batt_vol(struct oplus_chg_ic_dev *ic_dev, int index,
				     int *vol_mv)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*vol_mv = oplus_chg_ic_get_item_data(buf, index);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
				       index, vol_mv);
		if (rc < 0)
			chg_err("child ic[%d] get battery voltage error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_max(struct oplus_chg_ic_dev *ic_dev,
				     int *vol_mv)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
	int num, tmp;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*vol_mv = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		num = oplus_chg_ic_get_item_num(buf, data->size);
		if (num <= 0)
			goto skip_overwrite;
		tmp = 0;
		*vol_mv = 0;
		for (i = 0; i < num; i++) {
			tmp = oplus_chg_ic_get_item_data(buf, i);
			if (i == 0 || tmp > *vol_mv)
				*vol_mv = tmp;
		}
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
				       vol_mv);
		if (rc < 0)
			chg_err("child ic[%d] get battery voltage max error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_min(struct oplus_chg_ic_dev *ic_dev,
				     int *vol_mv)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
	int num, tmp;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*vol_mv = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		num = oplus_chg_ic_get_item_num(buf, data->size);
		if (num <= 0)
			goto skip_overwrite;
		tmp = 0;
		*vol_mv = 0;
		for (i = 0; i < num; i++) {
			tmp = oplus_chg_ic_get_item_data(buf, i);
			if (i == 0 || tmp < *vol_mv)
				*vol_mv = tmp;
		}
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
				       vol_mv);
		if (rc < 0)
			chg_err("child ic[%d] get battery voltage min error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_curr(struct oplus_chg_ic_dev *ic_dev,
				      int *curr_ma)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*curr_ma = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
				       curr_ma);
		if (rc < 0)
			chg_err("child ic[%d] get battery current error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_temp(struct oplus_chg_ic_dev *ic_dev,
				      int *temp)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*temp = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP, temp);
		if (rc < 0)
			chg_err("child ic[%d] get battery temp error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*soc = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC, soc);
		if (rc < 0)
			chg_err("child ic[%d] get battery soc error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_fcc(struct oplus_chg_ic_dev *ic_dev, int *fcc)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*fcc = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC, fcc);
		if (rc < 0)
			chg_err("child ic[%d] get battery fcc error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_CC);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*cc = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_CC)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_CC, cc);
		if (rc < 0)
			chg_err("child ic[%d] get battery cc error, rc=%d\n", i,
				rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_rm(struct oplus_chg_ic_dev *ic_dev, int *rm)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_RM);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*rm = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_RM)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_RM, rm);
		if (rc < 0)
			chg_err("child ic[%d] get battery remaining capacity error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_soh(struct oplus_chg_ic_dev *ic_dev, int *soh)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*soh = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH, soh);
		if (rc < 0)
			chg_err("child ic[%d] get battery soh error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_auth(struct oplus_chg_ic_dev *ic_dev,
				      bool *pass)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*pass = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		if (chip->child_list[i].batt_auth > 0) {
			*pass = !!chip->child_list[i].batt_auth;
			rc = 0;
			break;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH, pass);
		if (rc < 0)
			chg_err("child ic[%d] get battery auth status error, rc=%d\n",
				i, rc);
		else
			chip->child_list[i].batt_auth = *pass;
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_hmac(struct oplus_chg_ic_dev *ic_dev,
				      bool *pass)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*pass = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		if (chip->child_list[i].batt_hmac > 0) {
			*pass = !!chip->child_list[i].batt_hmac;
			rc = 0;
			break;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC, pass);
		if (rc < 0)
			chg_err("child ic[%d] get battery hmac status error, rc=%d\n",
				i, rc);
		else
			chip->child_list[i].batt_hmac = *pass;
		break;
	}

	return rc;
}

static int oplus_chg_vg_set_batt_full(struct oplus_chg_ic_dev *ic_dev, bool full)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		full = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL, full);
		if (rc < 0)
			chg_err("child ic[%d] set battery full error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_fc(struct oplus_chg_ic_dev *ic_dev, int *fc)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_FC);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*fc = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_FC)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_FC, fc);
		if (rc < 0)
			chg_err("child ic[%d] get battery fc error, rc=%d\n", i,
				rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_qm(struct oplus_chg_ic_dev *ic_dev, int *qm)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_QM);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*qm = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_QM)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_QM, qm);
		if (rc < 0)
			chg_err("child ic[%d] get battery qm error, rc=%d\n", i,
				rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_pd(struct oplus_chg_ic_dev *ic_dev, int *pd)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_PD);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*pd = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_PD)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_PD, pd);
		if (rc < 0)
			chg_err("child ic[%d] get battery pd error, rc=%d\n", i,
				rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_rcu(struct oplus_chg_ic_dev *ic_dev, int *rcu)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*rcu = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU, rcu);
		if (rc < 0)
			chg_err("child ic[%d] get battery rcu error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_rcf(struct oplus_chg_ic_dev *ic_dev, int *rcf)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*rcf = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF, rcf);
		if (rc < 0)
			chg_err("child ic[%d] get battery rcf error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_fcu(struct oplus_chg_ic_dev *ic_dev, int *fcu)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*fcu = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU, fcu);
		if (rc < 0)
			chg_err("child ic[%d] get battery fcu error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_fcf(struct oplus_chg_ic_dev *ic_dev, int *fcf)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*fcf = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF, fcf);
		if (rc < 0)
			chg_err("child ic[%d] get battery fcf error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_sou(struct oplus_chg_ic_dev *ic_dev, int *sou)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*sou = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU, sou);
		if (rc < 0)
			chg_err("child ic[%d] get battery sou error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_do0(struct oplus_chg_ic_dev *ic_dev, int *do0)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*do0 = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0, do0);
		if (rc < 0)
			chg_err("child ic[%d] get battery do0 error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_doe(struct oplus_chg_ic_dev *ic_dev, int *doe)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*doe = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE, doe);
		if (rc < 0)
			chg_err("child ic[%d] get battery doe error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_trm(struct oplus_chg_ic_dev *ic_dev, int *trm)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*trm = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM, trm);
		if (rc < 0)
			chg_err("child ic[%d] get battery trm error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_pc(struct oplus_chg_ic_dev *ic_dev, int *pc)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_PC);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*pc = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_PC)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_PC, pc);
		if (rc < 0)
			chg_err("child ic[%d] get battery pc error, rc=%d\n", i,
				rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_qs(struct oplus_chg_ic_dev *ic_dev, int *qs)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_QS);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*qs = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_QS)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_QS, qs);
		if (rc < 0)
			chg_err("child ic[%d] get battery qs error, rc=%d\n", i,
				rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_update_dod0(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0);
		if (rc < 0)
			chg_err("child ic[%d] update dod0 error, rc=%d\n", i,
				rc);
		break;
	}

	return rc;
}

static int
oplus_chg_vg_update_soc_smooth_parameter(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH);
		if (rc < 0)
			chg_err("child ic[%d] update soc smooth parameter error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_cb_status(struct oplus_chg_ic_dev *ic_dev,
				      int *status)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*status = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS,
				       status);
		if (rc < 0)
			chg_err("child ic[%d] get balancing status error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_passedchg(struct oplus_chg_ic_dev *ic_dev,
				      int *val)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*val = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG,
				       val);
		if (rc < 0)
			chg_err("child ic[%d] get passedchg error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_set_lock(struct oplus_chg_ic_dev *ic_dev, bool lock)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_SET_LOCK);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		lock = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_SET_LOCK)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_SET_LOCK, lock);
		if (rc < 0)
			chg_err("child ic[%d] %s gauge error, rc=%d\n", i,
				lock ? "lock" : "unlock", rc);
	}

	return rc;
}

static int oplus_chg_vg_is_locked(struct oplus_chg_ic_dev *ic_dev, bool *locked)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
	bool tmp;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_IS_LOCKED);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*locked = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	*locked = false;
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_IS_LOCKED)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_IS_LOCKED, &tmp);
		if (rc < 0)
			chg_err("child ic[%d] get gauge locked status error, rc=%d\n", i, rc);
		else
			*locked |= tmp;
	}

	return rc;
}

static int oplus_chg_vg_get_batt_num(struct oplus_chg_ic_dev *ic_dev, int *num)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*num = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM, num);
		if (rc < 0)
			chg_err("child ic[%d] get battery num error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_device_type(struct oplus_chg_ic_dev *ic_dev,
					int *type)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*type = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
				       type);
		if (rc < 0)
			chg_err("child ic[%d] get device type error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int
oplus_chg_vg_get_device_type_for_vooc(struct oplus_chg_ic_dev *ic_dev,
				       int *type)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*type = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(
			    &chip->child_list[i],
			    OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(
			chip->child_list[i].ic_dev,
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC, type);
		if (rc < 0)
			chg_err("child ic[%d] get device type for vooc error, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_exist_status(struct oplus_chg_ic_dev *ic_dev,
					 bool *exist)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(
		ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*exist = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	*exist = true;
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!chip->child_list[i].ic_dev->online) {
			*exist = false;
			break;
		}
	}

	return rc;
}

static int oplus_chg_vg_get_batt_cap(struct oplus_chg_ic_dev *ic_dev,
				     int *cap_mah)
{
	struct oplus_virtual_gauge_ic *chip;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(
		ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*cap_mah = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*cap_mah = chip->batt_capacity_mah;

	return rc;
}

static int oplus_chg_vg_is_suspend(struct oplus_chg_ic_dev *ic_dev,
				   bool *suspend)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev,
					       OPLUS_IC_FUNC_GAUGE_IS_SUSPEND);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*suspend = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif

	*suspend = false;
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_IS_SUSPEND)) {
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_IS_SUSPEND, suspend);
		if (rc < 0) {
			chg_err("child ic[%d] get suspend status error, rc=%d\n",
				i, rc);
			*suspend = true;
		}
		if (*suspend)
			break;
	}

	return rc;
}

static int oplus_chg_vg_get_bcc_prams(struct oplus_chg_ic_dev *ic_dev, char *buf)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS, buf);
		if (rc < 0)
			chg_err("child ic[%d] get bcc prams err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_fastchg_update_bcc_parameters(struct oplus_chg_ic_dev *ic_dev, char *buf)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS, buf);
		if (rc < 0)
			chg_err("child ic[%d] get fastchg bcc prams err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_get_prev_bcc_prams(struct oplus_chg_ic_dev *ic_dev, char *buf)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS, buf);
		if (rc < 0)
			chg_err("child ic[%d] get prev bcc prams err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_set_bcc_parms(struct oplus_chg_ic_dev *ic_dev, const char *buf)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS, buf);
		if (rc < 0)
			chg_err("child ic[%d] set bcc prams err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_protect_check(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK);
		if (rc < 0)
			chg_err("child ic[%d] set protect check err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_afi_update_done(struct oplus_chg_ic_dev *ic_dev, bool *status)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
				     OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE, status);
		if (rc < 0)
			chg_err("child ic[%d] get afi update err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}


static int oplus_chg_vg_check_reset_condition(struct oplus_chg_ic_dev *ic_dev,
					bool *need_reset)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
			OPLUS_IC_FUNC_GAUGE_CHECK_RESET)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
			OPLUS_IC_FUNC_GAUGE_CHECK_RESET, need_reset);
		if (rc < 0)
			chg_err("child ic[%d] check condition err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_fg_reset(struct oplus_chg_ic_dev *ic_dev,
					bool *reset_done)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
			OPLUS_IC_FUNC_GAUGE_SET_RESET)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
			OPLUS_IC_FUNC_GAUGE_SET_RESET, reset_done);
		if (rc < 0)
			chg_err("child ic[%d] set fg reset err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static int oplus_chg_vg_set_battery_curve(struct oplus_chg_ic_dev *ic_dev,
					  int type, int adapter_id, bool pd_svooc)
{
	struct oplus_virtual_gauge_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		if (!func_is_support(&chip->child_list[i],
			OPLUS_IC_FUNC_GAUGE_SET_BATTERY_CURVE)) {
			rc = (rc == 0) ? -ENOTSUPP : rc;
			continue;
		}
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
			OPLUS_IC_FUNC_GAUGE_SET_BATTERY_CURVE, type,
			adapter_id, pd_svooc);
		if (rc < 0)
			chg_err("child ic[%d] set fg reset err, rc=%d\n",
				i, rc);
		break;
	}

	return rc;
}

static void *oplus_chg_vg_get_func(struct oplus_chg_ic_dev *ic_dev,
				   enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_chg_vg_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_chg_vg_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_chg_vg_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       oplus_chg_vg_smt_test);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE,
					       oplus_chg_vg_gauge_update);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
					       oplus_chg_vg_get_batt_vol);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
					       oplus_chg_vg_get_batt_max);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
					       oplus_chg_vg_get_batt_min);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
			oplus_chg_vg_get_batt_curr);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
			oplus_chg_vg_get_batt_temp);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
					       oplus_chg_vg_get_batt_soc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
					       oplus_chg_vg_get_batt_fcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
					       oplus_chg_vg_get_batt_cc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
					       oplus_chg_vg_get_batt_rm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
					       oplus_chg_vg_get_batt_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
			oplus_chg_vg_get_batt_auth);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
			oplus_chg_vg_get_batt_hmac);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
			oplus_chg_vg_set_batt_full);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FC,
					       oplus_chg_vg_get_batt_fc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_QM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_QM,
					       oplus_chg_vg_get_batt_qm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_PD:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_PD,
					       oplus_chg_vg_get_batt_pd);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU,
					       oplus_chg_vg_get_batt_rcu);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF,
					       oplus_chg_vg_get_batt_rcf);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU,
					       oplus_chg_vg_get_batt_fcu);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF,
					       oplus_chg_vg_get_batt_fcf);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU,
					       oplus_chg_vg_get_batt_sou);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0,
					       oplus_chg_vg_get_batt_do0);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE,
					       oplus_chg_vg_get_batt_doe);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM,
					       oplus_chg_vg_get_batt_trm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_PC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_PC,
					       oplus_chg_vg_get_batt_pc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_QS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_QS,
					       oplus_chg_vg_get_batt_qs);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0,
					       oplus_chg_vg_update_dod0);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH,
			oplus_chg_vg_update_soc_smooth_parameter);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS,
			oplus_chg_vg_get_cb_status);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG,
			oplus_chg_vg_get_passedchg);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_LOCK:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_LOCK,
					       oplus_chg_vg_set_lock);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_LOCKED:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_LOCKED,
					       oplus_chg_vg_is_locked);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
					       oplus_chg_vg_get_batt_num);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
			oplus_chg_vg_get_device_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
			oplus_chg_vg_get_device_type_for_vooc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST,
			oplus_chg_vg_get_exist_status);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP,
			oplus_chg_vg_get_batt_cap);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_IS_SUSPEND,
			oplus_chg_vg_is_suspend);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS,
			oplus_chg_vg_get_bcc_prams);
		break;
	case OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_FASTCHG_UPDATE_BCC_PARMS,
			oplus_chg_vg_fastchg_update_bcc_parameters);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_PREV_BCC_PARMS,
			oplus_chg_vg_get_prev_bcc_prams);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS,
			oplus_chg_vg_set_bcc_parms);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_PROTECT_CHECK,
			oplus_chg_vg_protect_check);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE,
			oplus_chg_vg_afi_update_done);
		break;
	case OPLUS_IC_FUNC_GAUGE_CHECK_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_CHECK_RESET,
			oplus_chg_vg_check_reset_condition);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_RESET,
			oplus_chg_vg_fg_reset);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATTERY_CURVE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_BATTERY_CURVE,
			oplus_chg_vg_set_battery_curve);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
static ssize_t oplus_chg_vg_get_func_data(struct oplus_chg_ic_dev *ic_dev,
					  enum oplus_chg_ic_func func_id,
					  void *buf)
{
	bool temp;
	int *item_data;
	ssize_t rc = 0;
	int batt_num, i, len;
	char *tmp_buf;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT))
		return -EINVAL;

	switch (func_id) {
	case OPLUS_IC_FUNC_SMT_TEST:
		tmp_buf = (char *)get_zeroed_page(GFP_KERNEL);
		if (!tmp_buf) {
			rc = -ENOMEM;
			break;
		}
		rc = oplus_chg_vg_smt_test(ic_dev, tmp_buf, PAGE_SIZE);
		if (rc < 0) {
			free_page((unsigned long)tmp_buf);
			break;
		}
		len = oplus_chg_ic_debug_str_data_init(buf, rc);
		memcpy(oplus_chg_ic_get_item_data_addr(buf, 0), tmp_buf, rc);
		free_page((unsigned long)tmp_buf);
		rc = len;
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL:
		rc = oplus_chg_ic_func(ic_dev, OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
				       &batt_num);
		if (rc < 0)
			break;
		oplus_chg_ic_debug_data_init(buf, batt_num);
		for (i = 0; i < batt_num; i++) {
			item_data = oplus_chg_ic_get_item_data_addr(buf, i);
			rc = oplus_chg_vg_get_batt_vol(ic_dev, i, item_data);
			if (rc < 0)
				break;
			*item_data = cpu_to_le32(*item_data);
		}
		rc = oplus_chg_ic_debug_data_size(batt_num);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_max(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_min(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_curr(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_temp(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_soc(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_fcc(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CC:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_cc(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RM:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_rm(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_soh(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_auth(ic_dev, &temp);
		if (rc < 0)
			break;
		*item_data = temp;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_hmac(ic_dev, &temp);
		if (rc < 0)
			break;
		*item_data = temp;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FC:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_fc(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_QM:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_qm(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_PD:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_pd(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_rcu(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_rcf(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_fcu(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_fcf(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_sou(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_do0(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_doe(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_trm(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_PC:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_pc(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_QS:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_qs(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_cb_status(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_passedchg(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_num(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_device_type(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_device_type_for_vooc(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_LOCKED:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_is_locked(ic_dev, &temp);
		if (rc < 0)
			break;
		*item_data = temp;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_exist_status(ic_dev, &temp);
		if (rc < 0)
			break;
		*item_data = temp;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_get_batt_cap(ic_dev, item_data);
		if (rc < 0)
			break;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_SUSPEND:
		oplus_chg_ic_debug_data_init(buf, 1);
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		rc = oplus_chg_vg_is_suspend(ic_dev, &temp);
		if (rc < 0)
			break;
		*item_data = temp;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	default:
		chg_err("this func(=%d) is not supported to get\n", func_id);
		return -ENOTSUPP;
		break;
	}

	return rc;
}

static int oplus_chg_vg_set_func_data(struct oplus_chg_ic_dev *ic_dev,
				      enum oplus_chg_ic_func func_id,
				      const void *buf, size_t buf_len)
{
	int rc = 0;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT))
		return -EINVAL;

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_init(ic_dev);
		break;
	case OPLUS_IC_FUNC_EXIT:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_exit(ic_dev);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_reg_dump(ic_dev);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_gauge_update(ic_dev);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_set_batt_full(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_update_dod0(ic_dev);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_update_soc_smooth_parameter(ic_dev);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_LOCK:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_vg_set_lock(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	default:
		chg_err("this func(=%d) is not supported to set\n", func_id);
		return -ENOTSUPP;
		break;
	}

	return rc;
}

enum oplus_chg_ic_func oplus_chg_vg_overwrite_funcs[] = {
	OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
	OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_QM,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_PD,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_RCU,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_RCF,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FCU,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_FCF,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_SOU,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_DO0,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_DOE,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_TRM,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_PC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_QS,
	OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS,
	OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG,
	OPLUS_IC_FUNC_GAUGE_SET_LOCK,
	OPLUS_IC_FUNC_GAUGE_IS_LOCKED,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
	OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
	OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST,
	OPLUS_IC_FUNC_GAUGE_GET_BATT_CAP,
	OPLUS_IC_FUNC_GAUGE_IS_SUSPEND,
};

#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

static void oplus_gauge_online_work(struct work_struct *work)
{
	struct oplus_virtual_gauge_ic *chip = container_of(
		work, struct oplus_virtual_gauge_ic, gauge_online_work);
	bool online = true;
	int i;

	for (i = 0; i < chip->child_num; i++) {
		if (!chip->child_list[i].ic_dev->online) {
			online = false;
			break;
		}
	}
	if (online)
		oplus_chg_vg_init(chip->ic_dev);

	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ONLINE);
}

static void oplus_gauge_offline_work(struct work_struct *work)
{
	struct oplus_virtual_gauge_ic *chip = container_of(
		work, struct oplus_virtual_gauge_ic, gauge_offline_work);
	bool offline = false;
	int i;

	for (i = 0; i < chip->child_num; i++) {
		if (!chip->child_list[i].ic_dev->online) {
			offline = true;
			break;
		}
	}
	if (offline)
		oplus_chg_vg_exit(chip->ic_dev);

	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_OFFLINE);
}

static void oplus_gauge_resume_work(struct work_struct *work)
{
	struct oplus_virtual_gauge_ic *chip = container_of(
		work, struct oplus_virtual_gauge_ic, gauge_resume_work);
	bool suspend = false;

	oplus_chg_vg_is_suspend(chip->ic_dev, &suspend);

	if (!suspend)
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_RESUME);
}

static void oplus_chg_vg_err_handler(struct oplus_chg_ic_dev *ic_dev,
				     void *virq_data)
{
	struct oplus_virtual_gauge_ic *chip = virq_data;

	oplus_chg_ic_move_err_msg(chip->ic_dev, ic_dev);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void oplus_chg_vg_online_handler(struct oplus_chg_ic_dev *ic_dev,
					 void *virq_data)
{
	struct oplus_virtual_gauge_ic *chip = virq_data;

	schedule_work(&chip->gauge_online_work);
}

static void oplus_chg_vg_offline_handler(struct oplus_chg_ic_dev *ic_dev,
					 void *virq_data)
{
	struct oplus_virtual_gauge_ic *chip = virq_data;

	schedule_work(&chip->gauge_offline_work);
}

static void oplus_chg_vg_resume_handler(struct oplus_chg_ic_dev *ic_dev,
					void *virq_data)
{
	struct oplus_virtual_gauge_ic *chip = virq_data;

	schedule_work(&chip->gauge_resume_work);
}

struct oplus_chg_ic_virq oplus_chg_vg_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_ONLINE },
	{ .virq_id = OPLUS_IC_VIRQ_OFFLINE },
	{ .virq_id = OPLUS_IC_VIRQ_RESUME },
};

static int oplus_chg_vg_virq_register(struct oplus_virtual_gauge_ic *chip)
{
	int i, rc;

	for (i = 0; i < chip->child_num; i++) {
		if (virq_is_support(&chip->child_list[i], OPLUS_IC_VIRQ_ERR)) {
			rc = oplus_chg_ic_virq_register(
				chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_ERR,
				oplus_chg_vg_err_handler, chip);
			if (rc < 0)
				chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d",
					rc);
		}
		if (virq_is_support(&chip->child_list[i], OPLUS_IC_VIRQ_ONLINE)) {
			rc = oplus_chg_ic_virq_register(
				chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_ONLINE,
				oplus_chg_vg_online_handler, chip);
			if (rc < 0)
				chg_err("register OPLUS_IC_VIRQ_ONLINE error, rc=%d",
					rc);
		}
		if (virq_is_support(&chip->child_list[i], OPLUS_IC_VIRQ_OFFLINE)) {
			rc = oplus_chg_ic_virq_register(
				chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_OFFLINE,
				oplus_chg_vg_offline_handler, chip);
			if (rc < 0)
				chg_err("register OPLUS_IC_VIRQ_OFFLINE error, rc=%d",
					rc);
		}
		if (virq_is_support(&chip->child_list[i], OPLUS_IC_VIRQ_RESUME)) {
			rc = oplus_chg_ic_virq_register(
				chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_RESUME,
				oplus_chg_vg_resume_handler, chip);
			if (rc < 0)
				chg_err("register OPLUS_IC_VIRQ_RESUME error, rc=%d",
					rc);
		}
	}

	return 0;
}

static int oplus_virtual_gauge_parse_dt(struct oplus_virtual_gauge_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc;

	rc = of_property_read_u32(node, "oplus,batt_capacity_mah",
				  &chip->batt_capacity_mah);
	if (rc < 0) {
		chg_err("can't get oplus,batt_capacity_mah, rc=%d\n", rc);
		chip->batt_capacity_mah = 2000;
	}

	return 0;
}

static int oplus_virtual_gauge_probe(struct platform_device *pdev)
{
	struct oplus_virtual_gauge_ic *chip;
	struct device_node *node = pdev->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_virtual_gauge_ic),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	INIT_WORK(&chip->gauge_online_work, oplus_gauge_online_work);
	INIT_WORK(&chip->gauge_offline_work, oplus_gauge_offline_work);
	INIT_WORK(&chip->gauge_resume_work, oplus_gauge_resume_work);

	rc = oplus_virtual_gauge_parse_dt(chip);
	if (rc < 0)
		goto parse_dt_err;

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto reg_ic_err;
	}
	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto reg_ic_err;
	}
	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	sprintf(ic_cfg.manu_name, "virtual gauge");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_vg_get_func;
	ic_cfg.virq_data = oplus_chg_vg_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(oplus_chg_vg_virq_table);
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	chip->ic_dev->debug.get_func_data = oplus_chg_vg_get_func_data;
	chip->ic_dev->debug.set_func_data = oplus_chg_vg_set_func_data;
	chip->ic_dev->debug.overwrite_funcs = oplus_chg_vg_overwrite_funcs;
	chip->ic_dev->debug.func_num = ARRAY_SIZE(oplus_chg_vg_overwrite_funcs);
#endif

	chg_err("probe success\n");
	return 0;

reg_ic_err:
parse_dt_err:
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	chg_err("probe error\n");
	return rc;
}

static int oplus_virtual_gauge_remove(struct platform_device *pdev)
{
	struct oplus_virtual_gauge_ic *chip = platform_get_drvdata(pdev);

	if (chip == NULL)
		return -ENODEV;

	if (chip->ic_dev->online)
		oplus_chg_vg_exit(chip->ic_dev);
	devm_oplus_chg_ic_unregister(&pdev->dev, chip->ic_dev);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id oplus_virtual_gauge_match[] = {
	{ .compatible = "oplus,virtual_gauge" },
	{},
};

static struct platform_driver oplus_virtual_gauge_driver = {
	.driver		= {
		.name = "oplus-virtual_gauge",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_virtual_gauge_match),
	},
	.probe		= oplus_virtual_gauge_probe,
	.remove		= oplus_virtual_gauge_remove,
};

static __init int oplus_virtual_gauge_init(void)
{
	return platform_driver_register(&oplus_virtual_gauge_driver);
}

static __exit void oplus_virtual_gauge_exit(void)
{
	platform_driver_unregister(&oplus_virtual_gauge_driver);
}

oplus_chg_module_register(oplus_virtual_gauge);
