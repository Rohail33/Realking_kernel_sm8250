// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[STG]: %s[%d]: " fmt, __func__, __LINE__

#include "oplus_chg_strategy.h"

static int oplus_chg_strategy_get_index(struct oplus_chg_strategy *strategy,
					int temp)
{
	int i;

	for (i = 0; i < strategy->table_size; i++) {
		if (temp <= strategy->table_data[i].heat_temp)
			return i;
	}

	pr_err("Temperature exceeds the highest threshold\n");
	return strategy->table_size - 1;
}

int read_chg_strategy_data_from_node(struct device_node *node,
				     const char *prop_str,
				     struct oplus_chg_strategy_data *ranges)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;
	per_tuple_length = sizeof(struct oplus_chg_strategy_data) / sizeof(u32);
	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n", prop_str,
		       length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;

	if (tuples > CHG_STRATEGY_DATA_TABLE_MAX) {
		pr_err("too many entries(%d), only %d allowed\n", tuples,
		       CHG_STRATEGY_DATA_TABLE_MAX);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str, (u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].cool_temp > ranges[i].heat_temp) {
			pr_err("%s thresholds should be in ascendant ranges\n",
			       prop_str);
			rc = -EINVAL;
			goto clean;
		}
	}

	return tuples;
clean:
	memset(ranges, 0, tuples * sizeof(struct oplus_chg_strategy_data));
	return rc;
}

int oplus_chg_get_chg_strategy_data_len(struct oplus_chg_strategy_data data[],
					int max_len)
{
	int i;

	for (i = 0; i < max_len; i++) {
		if (data[i].cool_temp == 0 &&
		    data[i].heat_temp == 0 &&
		    data[i].curr_data == 0 &&
		    data[i].heat_next_index == 0 &&
		    data[i].cool_next_index == 0)
			return i;
	}

	return i;
}

void oplus_chg_strategy_init(struct oplus_chg_strategy *strategy,
			     struct oplus_chg_strategy_data *data,
			     int data_size, int temp)
{
	strategy->table_data = data;
	strategy->table_size = data_size;
	strategy->temp_min = strategy->table_data[0].cool_temp;
	strategy->temp_max =
		strategy->table_data[strategy->table_size - 1].heat_temp;
	strategy->temp_region.temp = temp;
	strategy->temp_region.index =
		oplus_chg_strategy_get_index(strategy, temp);
	strategy->initialized = true;
}

int oplus_chg_strategy_get_data(
	struct oplus_chg_strategy *strategy,
	struct oplus_chg_strategy_temp_region *temp_region, int temp)
{
	int temp_diff;

	temp_diff = temp - strategy->temp_region.temp;

	while (temp_diff != 0) {
		if (temp_diff > 0 &&
		    temp > strategy->table_data[strategy->temp_region.index].heat_temp) {
			strategy->temp_region.index =
				strategy->table_data[strategy->temp_region.index].heat_next_index;
			if (temp > strategy->table_data[strategy->temp_region.index].heat_temp &&
			    strategy->table_data[strategy->temp_region.index].heat_temp < strategy->temp_max)
				strategy->temp_region.temp =
					strategy->table_data[strategy->temp_region.index].heat_temp;
			else
				strategy->temp_region.temp = temp;
		} else if (temp_diff < 0 &&
			   temp <= strategy->table_data[strategy->temp_region.index].cool_temp) {
			strategy->temp_region.index =
				strategy->table_data[strategy->temp_region.index].cool_next_index;
			if (temp <= strategy->table_data[strategy->temp_region.index].cool_temp &&
			    strategy->table_data[strategy->temp_region.index].cool_temp > strategy->temp_max)
				strategy->temp_region.temp =
					strategy->table_data[strategy->temp_region.index].heat_temp;
			else
				strategy->temp_region.temp = temp;
		} else {
			strategy->temp_region.temp = temp;
		}
		temp_diff = temp - strategy->temp_region.temp;
	}

	return strategy->table_data[strategy->temp_region.index].curr_data;
}
