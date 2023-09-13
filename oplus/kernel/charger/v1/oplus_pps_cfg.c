// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2022 Oplus. All rights reserved.
 */

#define NAME_BUF_MAX 256
static int oplus_get_strategy_data(
	struct oplus_param_head *param_head, const char *name, struct batt_curves_soc *metadata)
{
	struct oplus_cfg_data_head *data_head;
	ssize_t data_len;
	const int range_data_size = sizeof(struct batt_curve);
	char *tmp_buf;
	char name_buf[NAME_BUF_MAX];
	int curv_num[BATT_CURVE_SOC_RANGE_MAX][BATT_CURVE_TEMP_RANGE_MAX];
	int i, j;
	int rc = 0;
	int m;
	struct batt_curve *tmp;

	tmp_buf = kzalloc(BATT_CURVE_SOC_RANGE_MAX * BATT_CURVE_TEMP_RANGE_MAX * BATT_PPS_SYS_MAX * range_data_size, GFP_KERNEL);
	if (!tmp_buf) {
		pr_err("fastchg strategy alloc data buf error\n");
		return -ENOMEM;
	}

	for (i = 0; i < BATT_CURVE_SOC_RANGE_MAX; i++) {
		for (j = 0; j < BATT_CURVE_TEMP_RANGE_MAX; j++) {
			memset(name_buf, 0, NAME_BUF_MAX);
			snprintf(name_buf, NAME_BUF_MAX, "%s:%s:%s", name, pps_strategy_soc[i], pps_strategy_temp[j]);
			data_head = oplus_cfg_find_param_by_name(param_head, name_buf);
			if (data_head == NULL) {
				rc = -ENODATA;
				pr_err("%s not found\n", name_buf);
				goto err;
			}
			data_len = oplus_cfg_get_data_size(data_head);
			if (data_len < 0 || data_len % range_data_size) {
				pr_err("%s data len error, data_len=%d\n", name_buf, data_len);
				rc = -EINVAL;
				goto err;
			}
			rc = oplus_cfg_get_data(
				data_head,
				tmp_buf + (BATT_CURVE_TEMP_RANGE_MAX * i + j) * BATT_PPS_SYS_MAX * range_data_size,
				data_len);
			if (rc < 0) {
				pr_err("get %s data buf error, rc=%d\n",
				       name_buf, rc);
				goto err;
			}
			curv_num[i][j] = data_len / range_data_size;
		}
	}

	for (i = 0; i < BATT_CURVE_SOC_RANGE_MAX; i++) {
		for (j = 0; j < BATT_CURVE_TEMP_RANGE_MAX; j++) {
			memcpy(metadata[i].batt_curves_temp[j].batt_curves,
			       tmp_buf + (BATT_CURVE_TEMP_RANGE_MAX * i + j) * BATT_PPS_SYS_MAX * range_data_size,
			       BATT_PPS_SYS_MAX * range_data_size);
			metadata[i].batt_curves_temp[j].batt_curve_num = curv_num[i][j];

			tmp = metadata[i].batt_curves_temp[j].batt_curves;
			for (m = 0; m < curv_num[i][j]; m++)
				pr_err("[TEST]:%s:%s:%s[%d]: %u %u %u %d %u\n",
				       name,
				       pps_strategy_soc[i],
				       pps_strategy_temp[j], m,
				       tmp[m].target_vbus,
				       tmp[m].target_vbat,
				       tmp[m].target_ibus,
				       tmp[m].exit,
				       tmp[m].target_time);
		}
	}

err:
	kfree(tmp_buf);
	return rc;
}

static int oplus_pps_update_config(void *data, struct oplus_param_head *param_head)
{
	struct oplus_pps_chip *chip;

	if (data == NULL) {
		pr_err("data is NULL\n");
		return -EINVAL;
	}
	if (param_head == NULL) {
		pr_err("param_head is NULL\n");
		return -EINVAL;
	}
	chip = (struct oplus_pps_chip *)data;

	(void)oplus_get_strategy_data(param_head, "pps_charge_third_strategy", chip->batt_curves_third_soc);
	(void)oplus_get_strategy_data(param_head, "pps_charge_oplus_strategy", chip->batt_curves_oplus_soc);

	return 0;
}

static int oplus_pps_reg_debug_config(struct oplus_pps_chip *chip)
{
	int rc;

	chip->debug_cfg.type = OPLUS_CHG_PPS_PARAM;
	chip->debug_cfg.update = oplus_pps_update_config;
	chip->debug_cfg.priv_data = chip;
	rc = oplus_cfg_register(&chip->debug_cfg);
	if (rc < 0)
		pr_err("spec cfg register error, rc=%d\n", rc);

	return rc;
}

__maybe_unused static void oplus_pps_unreg_debug_config(struct oplus_pps_chip *chip)
{
	oplus_cfg_unregister(&chip->debug_cfg);
}
