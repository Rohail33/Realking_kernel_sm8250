// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <oplus_cfg.h>
#include "oplus_chg_wls.h"

static const char * const strategy_soc_range[] = {
	"strategy_soc_0_to_30",
	"strategy_soc_30_to_70",
	"strategy_soc_70_to_90"
};

static const char * const strategy_temp_range[] = {
	"strategy_temp_0_to_50",
	"strategy_temp_50_to_120",
	"strategy_temp_120_to_160",
	"strategy_temp_160_to_400",
	"strategy_temp_400_to_440"
};

static int oplus_wls_get_skin_data(struct oplus_param_head *param_head,
				   const char *name, char *buf, int *step)
{
	struct oplus_cfg_data_head *data_head;
	ssize_t data_len;
	const int skin_range_data_size =
		sizeof(struct oplus_chg_wls_skin_range_data);
	int rc;
	int i;
	struct oplus_chg_wls_skin_range_data *tmp;

	data_head = oplus_cfg_find_param_by_name(param_head, name);
	if (data_head == NULL)
		return -ENODATA;

	data_len = oplus_cfg_get_data_size(data_head);
	if (data_len < 0 || data_len % skin_range_data_size) {
		pr_err("%s data len error, data_len=%d\n", name, data_len);
		return -EINVAL;
	}
	rc = oplus_cfg_get_data(data_head, buf, data_len);
	if (rc < 0) {
		pr_err("get %s data buf error, rc=%d\n", name, rc);
		return rc;
	}
	*step = data_len / skin_range_data_size;

	tmp = (struct oplus_chg_wls_skin_range_data *)buf;
	for (i = 0; i < *step; i++)
		pr_err("[TEST]:%s[%d]: %d %d %d\n", name, i,
		       tmp[i].low_threshold, tmp[i].high_threshold, tmp[i].curr_ma);

	return 0;
}

static int oplus_wls_get_strategy_data(struct oplus_param_head *param_head,
				       const char *name, char *buf,
				       int max_step, int *step)
{
	struct oplus_cfg_data_head *data_head;
	ssize_t data_len;
	const int strategy_data_size = sizeof(struct oplus_chg_strategy_data);
	char *tmp_buf;
	int rc;
	int i;
	struct oplus_chg_strategy_data *tmp;

	data_head = oplus_cfg_find_param_by_name(param_head, name);
	if (data_head == NULL)
		return -ENODATA;
	tmp_buf = kzalloc(max_step * strategy_data_size, GFP_KERNEL);
	if (!tmp_buf) {
		pr_err("%s alloc data buf error\n", name);
		return -ENOMEM;
	}

	data_len = oplus_cfg_get_data_size(data_head);
	if (data_len < 0 || data_len % strategy_data_size) {
		pr_err("%s data len error, data_len=%d\n", name, data_len);
		rc = -EINVAL;
		goto err;
	}
	rc = oplus_cfg_get_data(data_head, tmp_buf, data_len);
	if (rc < 0) {
		pr_err("get %s data buf error, rc=%d\n", name, rc);
		goto err;
	}
	memcpy(buf, tmp_buf, max_step * strategy_data_size);

	if (step)
		*step = data_len / strategy_data_size;

	tmp = (struct oplus_chg_strategy_data *)buf;
	for (i = 0; i < data_len / strategy_data_size; i++)
		pr_err("[TEST]:%s[%d]: %d %d %d %d %d\n", name, i,
		       tmp[i].cool_temp, tmp[i].heat_temp, tmp[i].curr_data,
		       tmp[i].heat_next_index, tmp[i].cool_next_index);

err:
	kfree(tmp_buf);
	return rc;
}

static int oplus_wls_get_step_data(struct oplus_param_head *param_head,
				   const char *name, char *buf,
				   int max_step, int *step)
{
	struct oplus_cfg_data_head *data_head;
	ssize_t data_len;
	const int range_data_size = sizeof(struct oplus_chg_wls_range_data);
	char *tmp_buf;
	int rc;
	int i;
	struct oplus_chg_wls_range_data *tmp;

	data_head = oplus_cfg_find_param_by_name(param_head, name);
	if (data_head == NULL)
		return -ENODATA;
	tmp_buf = kzalloc(max_step * range_data_size, GFP_KERNEL);
	if (!tmp_buf) {
		pr_err("%s alloc data buf error\n", name);
		return -ENOMEM;
	}

	data_len = oplus_cfg_get_data_size(data_head);
	if (data_len < 0 || data_len % range_data_size) {
		pr_err("%s data len error, data_len=%d\n", name, data_len);
		rc = -EINVAL;
		goto err;
	}
	rc = oplus_cfg_get_data(data_head, tmp_buf, data_len);
	if (rc < 0) {
		pr_err("get %s data buf error, rc=%d\n", name, rc);
		goto err;
	}
	memcpy(buf, tmp_buf, max_step * range_data_size);

	if (step)
		*step = data_len / range_data_size;

	tmp = (struct oplus_chg_wls_range_data *)buf;
	for (i = 0; i < data_len / range_data_size; i++)
		pr_err("[TEST]:%s[%d]: %d %d %d %d %d\n", name, i,
		       tmp[i].low_threshold, tmp[i].low_threshold, tmp[i].curr_ma,
		       tmp[i].vol_max_mv, tmp[i].need_wait);

err:
	kfree(tmp_buf);
	return rc;
}

#define NAME_BUF_MAX 256
static int oplus_wls_get_fastchg_strategy_data(
	struct oplus_param_head *param_head, struct oplus_chg_wls *wls_dev)
{
	struct oplus_cfg_data_head *data_head;
	ssize_t data_len;
	const int range_data_size = sizeof(struct oplus_chg_wls_range_data);
	char *tmp_buf;
	char name_buf[NAME_BUF_MAX];
	int max_step[WLS_FAST_SOC_MAX][WLS_FAST_TEMP_MAX];
	int i, j;
	int rc = 0;
	int m;
	struct oplus_chg_wls_range_data *tmp;

	tmp_buf = kzalloc(WLS_FAST_SOC_MAX * WLS_FAST_TEMP_MAX *
				  WLS_MAX_STEP_CHG_ENTRIES * range_data_size,
			  GFP_KERNEL);
	if (!tmp_buf) {
		pr_err("fastchg strategy alloc data buf error\n");
		return -ENOMEM;
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			memset(name_buf, 0, NAME_BUF_MAX);
			snprintf(name_buf, NAME_BUF_MAX, "%s:%s",
				 strategy_soc_range[i], strategy_temp_range[j]);
			data_head = oplus_cfg_find_param_by_name(param_head,
								 name_buf);
			if (data_head == NULL) {
				rc = -ENODATA;
				pr_err("%s not found\n", name_buf);
				goto err;
			}
			data_len = oplus_cfg_get_data_size(data_head);
			if (data_len < 0 || data_len % range_data_size) {
				pr_err("%s data len error, data_len=%d\n",
				       name_buf, data_len);
				rc = -EINVAL;
				goto err;
			}
			rc = oplus_cfg_get_data(
				data_head,
				tmp_buf + (WLS_FAST_TEMP_MAX * i + j) * WLS_MAX_STEP_CHG_ENTRIES * range_data_size,
				data_len);
			if (rc < 0) {
				pr_err("get %s data buf error, rc=%d\n",
				       name_buf, rc);
				goto err;
			}
			max_step[i][j] = data_len / range_data_size;
		}
	}

	for (i = 0; i < WLS_FAST_SOC_MAX; i++) {
		for (j = 0; j < WLS_FAST_TEMP_MAX; j++) {
			memcpy(wls_dev->fcc_steps[i].fcc_step[j].fcc_step,
			       tmp_buf + (WLS_FAST_TEMP_MAX * i + j) * WLS_MAX_STEP_CHG_ENTRIES * range_data_size,
			       WLS_MAX_STEP_CHG_ENTRIES * range_data_size);
			wls_dev->fcc_steps[i].fcc_step[j].max_step =
				max_step[i][j];

			tmp = wls_dev->fcc_steps[i].fcc_step[j].fcc_step;
			for (m = 0; m < max_step[i][j]; m++)
				pr_err("[TEST]:%s:%s[%d]: %d %d %d %d %d\n",
				       strategy_soc_range[i],
				       strategy_temp_range[j], m,
				       tmp[m].low_threshold,
				       tmp[m].high_threshold,
				       tmp[m].curr_ma,
				       tmp[m].vol_max_mv,
				       tmp[m].need_wait);
		}
	}

err:
	kfree(tmp_buf);
	return rc;
}

static int oplus_wls_update_config(void *data, struct oplus_param_head *param_head)
{
	struct oplus_chg_wls *wls_dev;
	struct oplus_chg_wls_dynamic_config *dynamic_cfg;

	if (data == NULL) {
		pr_err("data is NULL\n");
		return -EINVAL;
	}
	if (param_head == NULL) {
		pr_err("param_head is NULL\n");
		return -EINVAL;
	}

	wls_dev = (struct oplus_chg_wls *)data;
	dynamic_cfg = &wls_dev->dynamic_config;

	(void)oplus_wls_get_skin_data(param_head, "oplus,epp_plus-skin-step",
				      (char *)dynamic_cfg->epp_plus_skin_step,
				      &wls_dev->epp_plus_skin_step.max_step);
	(void)oplus_wls_get_skin_data(param_head, "oplus,epp-skin-step",
				      (char *)dynamic_cfg->epp_skin_step,
				      &wls_dev->epp_skin_step.max_step);
	(void)oplus_wls_get_skin_data(param_head, "oplus,bpp-skin-step",
				      (char *)dynamic_cfg->bpp_skin_step,
				      &wls_dev->bpp_skin_step.max_step);
	(void)oplus_wls_get_skin_data(
		param_head, "oplus,epp_plus-led-on-skin-step",
		(char *)dynamic_cfg->epp_plus_led_on_skin_step,
		&wls_dev->epp_plus_led_on_skin_step.max_step);
	(void)oplus_wls_get_skin_data(param_head, "oplus,epp-led-on-skin-step",
				      (char *)dynamic_cfg->epp_led_on_skin_step,
				      &wls_dev->epp_led_on_skin_step.max_step);

	(void)oplus_wls_get_strategy_data(
		param_head, "oplus,wls-fast-chg-led-off-strategy-data",
		(char *)dynamic_cfg->wls_fast_chg_led_off_strategy_data,
		WLS_MAX_STEP_CHG_ENTRIES, NULL);
	(void)oplus_wls_get_strategy_data(
		param_head, "oplus,wls-fast-chg-led-on-strategy-data",
		(char *)dynamic_cfg->wls_fast_chg_led_on_strategy_data,
		WLS_MAX_STEP_CHG_ENTRIES, NULL);

	/* non-ffc */
	(void)oplus_wls_get_step_data(
		param_head, "non-ffc-bpp",
		(char *)dynamic_cfg->non_ffc_step[OPLUS_WLS_CHG_MODE_BPP],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->non_ffc_step[OPLUS_WLS_CHG_MODE_BPP].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "non-ffc-epp",
		(char *)dynamic_cfg->non_ffc_step[OPLUS_WLS_CHG_MODE_EPP],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->non_ffc_step[OPLUS_WLS_CHG_MODE_EPP].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "non-ffc-epp-plus",
		(char *)dynamic_cfg->non_ffc_step[OPLUS_WLS_CHG_MODE_EPP_PLUS],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->non_ffc_step[OPLUS_WLS_CHG_MODE_EPP_PLUS].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "non-ffc-airvooc",
		(char *)dynamic_cfg->non_ffc_step[OPLUS_WLS_CHG_MODE_AIRVOOC],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->non_ffc_step[OPLUS_WLS_CHG_MODE_AIRVOOC].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "non-ffc-airsvooc",
		(char *)dynamic_cfg->non_ffc_step[OPLUS_WLS_CHG_MODE_AIRSVOOC],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->non_ffc_step[OPLUS_WLS_CHG_MODE_AIRSVOOC].max_step);

	/* cv step */
	(void)oplus_wls_get_step_data(
		param_head, "cv-bpp",
		(char *)dynamic_cfg->cv_step[OPLUS_WLS_CHG_MODE_BPP],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->cv_step[OPLUS_WLS_CHG_MODE_BPP].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "cv-epp",
		(char *)dynamic_cfg->cv_step[OPLUS_WLS_CHG_MODE_EPP],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->cv_step[OPLUS_WLS_CHG_MODE_EPP].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "cv-epp-plus",
		(char *)dynamic_cfg->cv_step[OPLUS_WLS_CHG_MODE_EPP_PLUS],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->cv_step[OPLUS_WLS_CHG_MODE_EPP_PLUS].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "cv-airvooc",
		(char *)dynamic_cfg->cv_step[OPLUS_WLS_CHG_MODE_AIRVOOC],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->cv_step[OPLUS_WLS_CHG_MODE_AIRVOOC].max_step);
	(void)oplus_wls_get_step_data(
		param_head, "cv-airsvooc",
		(char *)dynamic_cfg->cv_step[OPLUS_WLS_CHG_MODE_AIRSVOOC],
		WLS_MAX_STEP_CHG_ENTRIES,
		&wls_dev->cv_step[OPLUS_WLS_CHG_MODE_AIRSVOOC].max_step);

	(void)oplus_wls_get_fastchg_strategy_data(param_head, wls_dev);

	return 0;
}

int oplus_wls_reg_debug_config(struct oplus_chg_wls *wls_dev)
{
	int rc;

	wls_dev->debug_cfg.type = OPLUS_CHG_WLS_NORMAL_PARAM;
	wls_dev->debug_cfg.update = oplus_wls_update_config;
	wls_dev->debug_cfg.priv_data = wls_dev;
	rc = oplus_cfg_register(&wls_dev->debug_cfg);
	if (rc < 0)
		pr_err("spec cfg register error, rc=%d\n", rc);

	return rc;
}

void oplus_wls_unreg_debug_config(struct oplus_chg_wls *wls_dev)
{
	oplus_cfg_unregister(&wls_dev->debug_cfg);
}
