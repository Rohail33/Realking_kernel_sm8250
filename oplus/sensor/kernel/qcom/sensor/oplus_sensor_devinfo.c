/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "oplus_sensor_devinfo.h"
#include <linux/version.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio.h>

#define CLOSE_PD  1
#define CLOSE_PD_CONDITION 2
#define ALIGN4(s) ((sizeof(s) + 3)&(~0x3))
#define SAR_MAX_CH_NUM 5

#define ALS_DEFAULT_PROPERTY_VALUE 1000

extern int oplus_press_cali_data_init(void);
extern void oplus_press_cali_data_clean(void);

extern int pad_als_data_init(void);
extern void pad_als_data_clean(void);

struct sensor_info * g_chip = NULL;
struct sensor_info_old * g_chip_old = NULL;

struct proc_dir_entry *sensor_proc_dir = NULL;
static struct oplus_als_cali_data *gdata = NULL;
static uint32_t g_ldo_enable;



static char* als_rear_feature[] = {
	"als-factor",
};


__attribute__((weak)) unsigned int get_serialID(void)
{
	return 0;
};
static void is_need_close_pd(struct sensor_hw* hw, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	int di = 0;
	int sn_size = 0;
	uint32_t *specific_sn = NULL;
	hw->feature.feature[2] = 0;
	rc = of_property_read_u32(ch_node, "is_need_close_pd", &value);

	if (!rc) {
		if (CLOSE_PD == value) {
			hw->feature.feature[2] = CLOSE_PD;
		} else if (CLOSE_PD_CONDITION == value) {
			sn_size = of_property_count_elems_of_size(ch_node, "sn_number", sizeof(uint32_t));
			pr_info("sn size %d\n", sn_size);
			specific_sn = (uint32_t *)kzalloc(sizeof(uint32_t) * sn_size, GFP_KERNEL);

			if (!specific_sn) {
				pr_err("%s kzalloc failed!\n", __func__);
				return;
			}

			of_property_read_u32_array(ch_node, "sn_number", specific_sn, sn_size);


			for (di = 0; di < sn_size; di++) {
				if (specific_sn[di] == get_serialID()) {
					hw->feature.feature[2] = CLOSE_PD;
					break;
				}
			}

			kfree(specific_sn);
		}
	}

}

static void parse_physical_sensor_common_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	int rc = 0;
	uint32_t chip_value = 0;
	rc = of_property_read_u32(ch_node, "sensor-name", &chip_value);

	if (rc) {
		hw->sensor_name = 0;
	} else {
		hw->sensor_name = chip_value;
	}

	rc = of_property_read_u32(ch_node, "bus-number", &chip_value);

	if (rc) {
		hw->bus_number = DEFAULT_CONFIG;/*read from registry */
	} else {
		hw->bus_number = chip_value;
	}

	rc = of_property_read_u32(ch_node, "sensor-direction", &chip_value);

	if (rc) {
		hw->direction = DEFAULT_CONFIG;/*read from registry */
	} else {
		hw->direction = chip_value;
	}

	rc = of_property_read_u32(ch_node, "irq-number", &chip_value);

	if (rc) {
		hw->irq_number = DEFAULT_CONFIG;/*read from registry */
	} else {
		hw->irq_number = chip_value;
	}

}

static void parse_magnetic_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	int soft_default_para[18] = {10000, 0, 0, 0, 0, 0, 0, 0, 10000, 0, 0, 0, 0, 0, 0, 0, 10000, 0};
	/*set default defaut mag */
	memcpy((void *)&hw->feature.parameter[0], (void *)&soft_default_para[0], sizeof(soft_default_para));
	rc = of_property_read_u32(ch_node, "parameter-number", &value);
	if (!rc && value > 0 && value < PARAMETER_NUM) {
		rc = of_property_read_u32_array(ch_node,
				"soft-mag-parameter", &hw->feature.parameter[0], value);
		for (di = 0; di < value; di++) {
			SENSOR_DEVINFO_DEBUG("soft magnetic parameter[%d] : %d\n", di,
				hw->feature.parameter[di]);
			}
		return;
	} else if (rc) {
		int prj_id = 0;
		int prj_dir[5];
		struct device_node *node = ch_node;
		struct device_node *ch_node_mag = NULL;
		prj_id = get_project();
		for_each_child_of_node(node, ch_node_mag) {
			if (ch_node_mag == NULL) {
				SENSOR_DEVINFO_DEBUG(" the mag_para use default parametyers");
				return;
				}
			rc = of_property_read_u32(ch_node_mag, "projects-num", &value);
			SENSOR_DEVINFO_DEBUG("get that project is %d", prj_id);
			rc = of_property_read_u32_array(ch_node_mag,
						"match-projects", &prj_dir[0], value);
			for (di = 0; di < value; di++) {
				SENSOR_DEVINFO_DEBUG(" which get there are %d projects", prj_dir[di]);
				if (prj_dir[di] == prj_id) {
				rc = of_property_read_u32(ch_node_mag, "parameter-number", &value);
					if (!rc && value > 0 && value < PARAMETER_NUM) {
					rc = of_property_read_u32_array(ch_node_mag,
						"soft-mag-parameter", &hw->feature.parameter[0], value);
					for (di = 0; di < value; di++) {
						SENSOR_DEVINFO_DEBUG("soft magnetic parameter[%d] : %d\n", di,
							hw->feature.parameter[di]);
						}
						return;
					} else {
						pr_info("parse soft magnetic parameter failed!\n");
					}
				}
				else
					continue;
			}
		}
	} else {
		pr_info("parse soft magnetic parameter failed!\n");
	}
}

static void parse_proximity_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *param[] = {
		"low_step",
		"high_step",
		"low_limit",
		"high_limit",
		"dirty_low_step",
		"dirty_high_step",
		"ps_dirty_limit",
		"ps_ir_limit",
		"ps_adjust_min",
		"ps_adjust_max",
		"sampling_count",
		"step_max",
		"step_min",
		"step_div",
		"anti_shake_delta",
		"dynamic_cali_max",
		"raw2offset_radio",
		"offset_max",
		"offset_range_min",
		"offset_range_max",
		"force_cali_limit",
		"cali_jitter_limit",
		"cal_offset_margin",
		"esd_gpio_use"
	};
	rc = of_property_read_u32(ch_node, "ps-type", &value);

	if (!rc) {
		hw->feature.feature[0] = value;
	}

	rc = of_property_read_u32(ch_node, "ps_saturation", &value);

	if (!rc) {
		hw->feature.feature[1] = value;
	}

	is_need_close_pd(hw, ch_node);
	rc = of_property_read_u32(ch_node, "ps_factory_cali_max", &value);

	if (!rc) {
		hw->feature.feature[3] = value;
	}

	for (di = 0; di < ARRAY_SIZE(param); di++) {
		rc = of_property_read_u32(ch_node, param[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("parameter[%d] : %d\n", di, hw->feature.parameter[di]);
	}

	rc = of_property_read_u32(ch_node, "parameter-number", &value);

	if (!rc && value > 0 && value < REG_NUM - 1) {
		hw->feature.reg[0] = value;
		rc = of_property_read_u32_array(ch_node,
				"sensor-reg", &hw->feature.reg[1], value);

		for (di = 0; di < value / 2; di++) {
			SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n", hw->feature.reg[di * 2 + 1],
				hw->feature.reg[2 * di + 2]);
		}
	} else {
		pr_info("parse alsps sensor reg failed\n");
	}

	SENSOR_DEVINFO_DEBUG("ps-type:%d ps_saturation:%d is_need_close_pd:%d\n",
		hw->feature.feature[0], hw->feature.feature[1], hw->feature.feature[2]);
}

static void parse_light_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	int di = 0;

	char *als_feature[] = {
		"als-type",
		"is-unit-device",
		"is-als-dri",
		"als-factor",
		"is_als_initialed",
		"als_buffer_length",
		"normalization_value",
		"use_lb_algo",
		"para-matrix",
		"als_ratio_type"
	};

	char *light_para[] = {
		"coef_a",
		"coef_b",
		"coef_c",
		"coef_d",
		"coef_e",
		"coef_ratio",
		"gold-reset-scale", /*gold scale value after sale*/
		/*lb para begin*/
		"lcd_type",
		"linear_comp_scope",
		"linear_comp_intercept",
		"temp_lux",
		"coef",
		"bal_coef",
		"ir_coef_stage_0",
		"ir_coef_stage_1",
		"ir_coef_stage_2",
		"ir_coef_stage_3",
		"ir_coef_val_0",
		"ir_coef_val_1",
		"ir_coef_val_2",
		"ir_coef_val_3",
		"ir_coef_val_4",
		"lb-dbv-coefs-id",
		"light-cut",
		/*lb para end*/
	};

	gdata->row_coe = 1000;

	for (di = 0; di < ARRAY_SIZE(als_feature); di++) {
		rc = of_property_read_u32(ch_node, als_feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;

			if (!strncmp(als_feature[di], "als-factor", 10)) {
				gdata->row_coe = value;
			}
		} else if (0 == strncmp(als_feature[di], "norm", 4)) {
			hw->feature.feature[di] = 1057;
		} else if (0 == strncmp(als_feature[di], "als_ratio_type", strlen("als_ratio_type"))) {
			hw->feature.feature[di] = 0; /*set defaut zero*/
		} else {
			pr_info("parse %s failed!", als_feature[di]);
		}

		SENSOR_DEVINFO_DEBUG("light feature[%s] : %d\n", als_feature[di], hw->feature.feature[di]);
	}

	for (di = 0; di < ARRAY_SIZE(light_para); di++) {
		rc = of_property_read_u32(ch_node, light_para[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		} else if (0 == strncmp(light_para[di], "gold-reset-scale", strlen("gold-reset-scale"))) {
			hw->feature.parameter[di] = 1001; /*set defaut value 1001*/
		} else {
			hw->feature.parameter[di] = 0; /*set defaut param*/
			pr_info("parse %s failed!", light_para[di]);
		}
		SENSOR_DEVINFO_DEBUG("light_para[%s] : %d\n", light_para[di], hw->feature.parameter[di]);
	}
}
static void parse_light_rear_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	int di = 0;

	for (di = 0; di < ARRAY_SIZE(als_rear_feature); di++) {
		rc = of_property_read_u32(ch_node, als_rear_feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		} else {
			pr_info("parse %s failed!", als_rear_feature[di]);
		}

		SENSOR_DEVINFO_DEBUG("parse_light_rear_sensor_dts-feature[%d] : %d\n", di, hw->feature.feature[di]);
	}
}

static void parse_sar_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	int di = 0;
	int rc = 0;
	int value = 0;
	int dc_offset_default[SAR_MAX_CH_NUM * 2] = {0, 0, 0, 0, 0, 30000, 30000, 30000, 30000, 30000};
	rc = of_property_read_u32(ch_node, "parameter-number", &value);
	if (!rc && value > 0 && value < PARAMETER_NUM) {
		rc = of_property_read_u32_array(ch_node,
			"sensor-reg", &hw->feature.parameter[0], value);
		for (di = 0; di < value / 2; di++) {
			SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n", hw->feature.parameter[di * 2],
				hw->feature.parameter[2 * di + 1]);
		}
	} else {
			pr_info("parse sar sensor reg failed\n");
	}
	/*channel-num*/
	rc = of_property_read_u32(ch_node, "channel-num", &value);
	if (!rc && value < SAR_MAX_CH_NUM) {
		hw->feature.feature[di] = value;
		SENSOR_DEVINFO_DEBUG("sar channel-num: %d\n", value);
	} else {
		pr_info("parse sar sensor channel-num failed, rc %d, value %d", rc, value);
	}
	/*reg->dc_offset*/
	rc = of_property_read_u32(ch_node, "is-dc-offset", &value);
	if (!rc && value == 1) {
		memcpy((void *)&hw->feature.reg[0], (void *)&dc_offset_default[0], SAR_MAX_CH_NUM * 2);
		for (di = 0; di < SAR_MAX_CH_NUM; di++) {
			SENSOR_DEVINFO_DEBUG("sar dc_offset_l[%d] = %d, dc_offset_H[%d] = %d",
				di, hw->feature.reg[di], di + SAR_MAX_CH_NUM, hw->feature.reg[di + SAR_MAX_CH_NUM]);
		}
		rc = of_property_read_u32_array(ch_node, "dc-offset", &hw->feature.reg[0], SAR_MAX_CH_NUM * 2);
		for (di = 0; di < SAR_MAX_CH_NUM; di++) {
			SENSOR_DEVINFO_DEBUG("sar dc_offset_l[%d] = %d, dc_offset_H[%d] = %d",
				di, hw->feature.reg[di], di + SAR_MAX_CH_NUM, hw->feature.reg[di + SAR_MAX_CH_NUM]);
		}
	} else {
			pr_info("parse sar sensor dc_offset failed, rc %d, value %d", rc, value);
	}

}

static void parse_down_sar_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	int di = 0;
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "parameter-number", &value);

	if (!rc && value > 0 && value < PARAMETER_NUM) {
		rc = of_property_read_u32_array(ch_node,
				"sensor-reg", &hw->feature.parameter[0], value);

		for (di = 0; di < value / 2; di++) {
			SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n", hw->feature.parameter[di * 2],
				hw->feature.parameter[2 * di + 1]);
		}

	} else {
		pr_info("parse down-sar sensor reg failed\n");
	}

}

static void parse_cct_sensor_dts(struct sensor_hw *hw, struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *feature[] = {
		"decoupled-driver",
		"publish-sensors",
		"is-ch-dri",
		"timer-size",
		"fac-cali-sensor"
	};

	char *para[] = {
		"para-matrix",
		"atime",
		"first-atime",
		"fac-cali-atime",
		"first-again",
		"fac-cali-again",
		"fd-time",
		"fac-cali-fd-time",
		"first-fd-gain",
		"fac-cali-fd-gain",
		"algo-flag"
	};

	hw->feature.feature[0] = 1;/*default use decoupled driver oplus_cct */

	for (di = 0; di < ARRAY_SIZE(feature); di++) {
		rc = of_property_read_u32(ch_node, feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_feature[%d] : %d\n", di, hw->feature.feature[di]);
	}

	for (di = 0; di < ARRAY_SIZE(para); di++) {
		rc = of_property_read_u32(ch_node, para[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_parameter[%d] : %d\n", di, hw->feature.parameter[di]);
	}
}

static void parse_cct_rear_sensor_dts(struct sensor_hw *hw, struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *feature[] = {
		"decoupled-driver",
		"publish-sensors",
		"is-ch-dri",
		"timer-size",
		"fac-cali-sensor"
	};

	char *para[] = {
		"para-matrix",
		"atime",
		"first-atime",
		"fac-cali-atime",
		"first-again",
		"fac-cali-again",
		"fd-time",
		"fac-cali-fd-time",
		"first-fd-gain",
		"fac-cali-fd-gain"
	};

	hw->feature.feature[0] = 1;/*default use decoupled driver oplus_cct */

	for (di = 0; di < ARRAY_SIZE(feature); di++) {
		rc = of_property_read_u32(ch_node, feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_feature[%d] : %d\n", di, hw->feature.feature[di]);
	}

	for (di = 0; di < ARRAY_SIZE(para); di++) {
		rc = of_property_read_u32(ch_node, para[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_parameter[%d] : %d\n", di, hw->feature.parameter[di]);
	}
}

static void parse_accelerometer_sensor_dts(struct sensor_hw *hw, struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *feature[] = {
		"use-sois",
		"single-acc"
	};

	hw->feature.feature[0] = 0;/*default not use s-ois */

	for (di = 0; di < ARRAY_SIZE(feature); di++) {
		rc = of_property_read_u32(ch_node, feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("gsensor_feature[%d] : %d\n", di, hw->feature.feature[di]);
	}
}

static void parse_each_physical_sensor_dts(struct sensor_hw* hw, struct device_node *ch_node)
{
	if (0 == strncmp(ch_node->name, "msensor", 7)) {
		parse_magnetic_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "psensor", 7)) {
		parse_proximity_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "lsensor", 7)) {
		parse_light_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "ssensor", 7)) {
		parse_sar_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "cctsensor", 7)) {
		parse_cct_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "cctrsensor", 7)) {
		parse_cct_rear_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "sdsensor", 7)) {
		parse_down_sar_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "lrsensor", 7)) {
		parse_light_rear_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "gsensor", 7)) {
		parse_accelerometer_sensor_dts(hw, ch_node);
	} else {
		/*do nothing */
	}

}

static void parse_pickup_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "is-need-prox", &value);

	if (!rc) {
		algo->feature[0] = value;
	}

	rc = of_property_read_u32(ch_node, "prox-type", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	SENSOR_DEVINFO_DEBUG("is-need-prox: %d, prox-type: %d\n",
		algo->feature[0], algo->parameter[0]);
}

static void parse_lux_aod_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "thrd-low", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	rc = of_property_read_u32(ch_node, "thrd-high", &value);

	if (!rc) {
		algo->parameter[1] = value;
	}

	rc = of_property_read_u32(ch_node, "als-type", &value);

	if (!rc) {
		algo->parameter[2] = value;
	}

	rc = of_property_read_u32(ch_node, "fold-feature", &value);

	if (!rc) {
		algo->feature[0] = value;
	}

	SENSOR_DEVINFO_DEBUG("thrd-low: %d, thrd-high: %d, als-type: %d, fold-feature: %d\n",
		algo->parameter[0], algo->parameter[1], algo->parameter[2], algo->feature[0]);

}

static void parse_fp_display_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "prox-type", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	SENSOR_DEVINFO_DEBUG("prox-type :%d\n", algo->parameter[0]);
}

static void parse_mag_fusion_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;

	rc = of_property_read_u32(ch_node, "fusion-type", &value);

	if (!rc) {
		algo->feature[0] = value;
	}

	rc = of_property_read_u32(ch_node, "mag-type", &value);

	if (!rc) {
		algo->feature[1] = value;
	}

	SENSOR_DEVINFO_DEBUG("fusion-type :%d mag-type : %d\n", algo->feature[0], algo->feature[1]);
}

static void parse_oplus_measurement_sensor_dts(struct sensor_algorithm *algo, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;

	rc = of_property_read_u32(ch_node, "acc-range", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	rc = of_property_read_u32(ch_node, "gyro-range", &value);

	if (!rc) {
		algo->parameter[1] = value;
	}

	SENSOR_DEVINFO_DEBUG("acc-range: %d, gyro-range: %d\n",
		algo->parameter[0], algo->parameter[1]);
}

static void parse_each_virtual_sensor_dts(struct sensor_algorithm *algo, struct device_node * ch_node)
{
	if (0 == strncmp(ch_node->name, "pickup", 6)) {
		parse_pickup_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "lux_aod", 6)) {
		parse_lux_aod_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "fp_display", 6)) {
		parse_fp_display_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "mag_fusion", 10)) {
		parse_mag_fusion_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "oplus_measurement", 17)) {
		parse_oplus_measurement_sensor_dts(algo, ch_node);
	} else {
		/* do nothing */
	}

}

static void oplus_sensor_parse_dts(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int rc = 0;
	int value = 0;
	bool is_virtual_sensor = false;
	struct device_node *ch_node = NULL;
	int sensor_type = 0;
	int sensor_index = 0;
	struct sensor_hw *hw = NULL;
	struct sensor_algorithm *algo = NULL;
	pr_info("start \n");

	for_each_child_of_node(node, ch_node) {
		is_virtual_sensor = false;

		if (of_property_read_bool(ch_node, "is-virtual-sensor")) {
			is_virtual_sensor = true;
		}

		rc = of_property_read_u32(ch_node, "sensor-type", &value);

		if (rc || (is_virtual_sensor && value >= SENSOR_ALGO_NUM)
			|| value >= SENSORS_NUM) {
			pr_info("parse sensor type failed!\n");
			continue;
		} else {
			sensor_type = value;
		}

		if (!is_virtual_sensor) {
			if (g_chip) {
				g_chip->s_vector[sensor_type].sensor_id = sensor_type;
			} else if (g_chip_old) {
				g_chip_old->s_vector[sensor_type].sensor_id = sensor_type;
			}
			rc = of_property_read_u32(ch_node, "sensor-index", &value);

			if (rc || value >= SOURCE_NUM) {
				pr_info("parse sensor index failed!\n");
				continue;
			} else {
				sensor_index = value;
			}

			if (g_chip) {
				hw = &g_chip->s_vector[sensor_type].hw[sensor_index];
			} else if (g_chip_old) {
				hw = &g_chip_old->s_vector[sensor_type].hw[sensor_index];
			}
			parse_physical_sensor_common_dts(hw, ch_node);
			parse_each_physical_sensor_dts(hw, ch_node);
		} else {
			if (g_chip) {
				g_chip->a_vector[sensor_type].sensor_id = sensor_type;
				algo = &g_chip->a_vector[sensor_type];
			} else if (g_chip_old) {
				g_chip_old->a_vector[sensor_type].sensor_id = sensor_type;
				algo = &g_chip_old->a_vector[sensor_type];
			}
			parse_each_virtual_sensor_dts(algo, ch_node);
		}
	}/*for_each_child_of_node */

	rc = of_property_read_u32(node, "als-dev_coef_1", &value);

	if (rc) {
		gdata->dev_coef_1 = ALS_DEFAULT_PROPERTY_VALUE;
	} else {
		gdata->dev_coef_1 = value == ALS_DEFAULT_PROPERTY_VALUE ?
				ALS_DEFAULT_PROPERTY_VALUE - 1 : value;
	}

	rc = of_property_read_u32(node, "als-dev_coef_2", &value);

	if (rc) {
		gdata->dev_coef_2 = ALS_DEFAULT_PROPERTY_VALUE;
	} else {
		gdata->dev_coef_2 = value == ALS_DEFAULT_PROPERTY_VALUE ?
				ALS_DEFAULT_PROPERTY_VALUE - 1 : value;
	}

	rc = of_property_read_u32(node, "als-dev_coef_h2l", &value);

	if (rc) {
		gdata->dev_coef_h2l = ALS_DEFAULT_PROPERTY_VALUE;
	} else {
		gdata->dev_coef_h2l = value == ALS_DEFAULT_PROPERTY_VALUE ?
				ALS_DEFAULT_PROPERTY_VALUE - 1 : value;
	}

	rc = of_property_read_u32(node, "als-dev_coef_l2h", &value);

	if (rc) {
		gdata->dev_coef_l2h = ALS_DEFAULT_PROPERTY_VALUE;
	} else {
		gdata->dev_coef_l2h = value == ALS_DEFAULT_PROPERTY_VALUE ?
				ALS_DEFAULT_PROPERTY_VALUE - 1 : value;
	}

	rc = of_property_read_u32(node, "ldo_enable", &g_ldo_enable);
}

static ssize_t als_type_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (g_chip) {
		len = sprintf(page, "%d", g_chip->s_vector[OPLUS_LIGHT].hw[0].feature.feature[0]);
	} else if (g_chip_old) {
		len = sprintf(page, "%d", g_chip_old->s_vector[OPLUS_LIGHT].hw[0].feature.feature[0]);
	} else {
		return -ENOMEM;
	}

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t red_max_lux_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata) {
		return -ENOMEM;
	}

	len = sprintf(page, "%d", gdata->red_max_lux);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t red_max_lux_write_proc(struct file *file, const char __user *buf,
	size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if (!gdata) {
		return -ENOMEM;
	}


	if (count > 256) {
		count = 256;
	}

	if (count > *off) {
		count -= *off;
	} else {
		count = 0;
	}

	if (copy_from_user(page, buf, count)) {
		return -EFAULT;
	}

	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if (input != gdata->red_max_lux) {
		gdata->red_max_lux = input;
	}

	return count;
}

static ssize_t white_max_lux_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata) {
		return -ENOMEM;
	}

	len = sprintf(page, "%d", gdata->white_max_lux);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t white_max_lux_write_proc(struct file *file, const char __user *buf,
	size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if (!gdata) {
		return -ENOMEM;
	}


	if (count > 256) {
		count = 256;
	}

	if (count > *off) {
		count -= *off;
	} else {
		count = 0;
	}

	if (copy_from_user(page, buf, count)) {
		return -EFAULT;
	}

	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if (input != gdata->white_max_lux) {
		gdata->white_max_lux = input;
	}

	return count;
}

static ssize_t blue_max_lux_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata) {
		return -ENOMEM;
	}

	len = sprintf(page, "%d", gdata->blue_max_lux);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t blue_max_lux_write_proc(struct file *file, const char __user *buf,
	size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if (!gdata) {
		return -ENOMEM;
	}


	if (count > 256) {
		count = 256;
	}

	if (count > *off) {
		count -= *off;
	} else {
		count = 0;
	}

	if (copy_from_user(page, buf, count)) {
		return -EFAULT;
	}

	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if (input != gdata->blue_max_lux) {
		gdata->blue_max_lux = input;
	}

	return count;
}

static ssize_t green_max_lux_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata) {
		return -ENOMEM;
	}

	len = sprintf(page, "%d", gdata->green_max_lux);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}
static ssize_t green_max_lux_write_proc(struct file *file, const char __user *buf,
	size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if (!gdata) {
		return -ENOMEM;
	}


	if (count > 256) {
		count = 256;
	}

	if (count > *off) {
		count -= *off;
	} else {
		count = 0;
	}

	if (copy_from_user(page, buf, count)) {
		return -EFAULT;
	}

	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if (input != gdata->green_max_lux) {
		gdata->green_max_lux = input;
	}

	return count;
}

static ssize_t cali_coe_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata) {
		return -ENOMEM;
	}

	len = sprintf(page, "%d", gdata->cali_coe);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t cali_coe_write_proc(struct file *file, const char __user *buf,
	size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if (!gdata) {
		return -ENOMEM;
	}


	if (count > 256) {
		count = 256;
	}

	if (count > *off) {
		count -= *off;
	} else {
		count = 0;
	}

	if (copy_from_user(page, buf, count)) {
		return -EFAULT;
	}

	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if (input != gdata->cali_coe) {
		gdata->cali_coe = input;
	}

	return count;
}

static ssize_t row_coe_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata) {
		return -ENOMEM;
	}

	len = sprintf(page, "%d", gdata->row_coe);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t row_coe_write_proc(struct file *file, const char __user *buf,
	size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if (!gdata) {
		return -ENOMEM;
	}


	if (count > 256) {
		count = 256;
	}

	if (count > *off) {
		count -= *off;
	} else {
		count = 0;
	}

	if (copy_from_user(page, buf, count)) {
		return -EFAULT;
	}

	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if (input != gdata->row_coe) {
		gdata->row_coe = input;
	}

	return count;
}

static ssize_t dev_coef_read_proc(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!gdata) {
		return -ENOMEM;
	}

	len = sprintf(page, "%d %d %d %d", gdata->dev_coef_1, gdata->dev_coef_2, gdata->dev_coef_h2l, gdata->dev_coef_l2h);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buf, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t dev_coef_write_proc(struct file *file, const char __user *buf,
	size_t count, loff_t *off)

{
	char page[256] = {0};

	if (!gdata) {
		return -ENOMEM;
	}


	if (count > 256) {
		count = 256;
	}

	if (count > *off) {
		count -= *off;
	} else {
		count = 0;
	}

	if (copy_from_user(page, buf, count)) {
		return -EFAULT;
	}

	*off += count;

	if (sscanf(page, "%d %d %d %d", &gdata->dev_coef_1, &gdata->dev_coef_2, &gdata->dev_coef_h2l, &gdata->dev_coef_l2h) != 4) {
		count = -EINVAL;
		return count;
	}

	return count;
}



#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops als_type_fops = {
	.proc_read = als_type_read_proc,
};

static const struct proc_ops red_max_lux_fops = {
	.proc_read = red_max_lux_read_proc,
	.proc_write = red_max_lux_write_proc,
};

static const struct proc_ops white_max_lux_fops = {
	.proc_read = white_max_lux_read_proc,
	.proc_write = white_max_lux_write_proc,
};

static const struct proc_ops blue_max_lux_fops = {
	.proc_read = blue_max_lux_read_proc,
	.proc_write = blue_max_lux_write_proc,
};

static const struct proc_ops green_max_lux_fops = {
	.proc_read = green_max_lux_read_proc,
	.proc_write = green_max_lux_write_proc,
};

static const struct proc_ops cali_coe_fops = {
	.proc_read = cali_coe_read_proc,
	.proc_write = cali_coe_write_proc,
};

static const struct proc_ops row_coe_fops = {
	.proc_read = row_coe_read_proc,
	.proc_write = row_coe_write_proc,
};

static const struct proc_ops dev_coef_fops = {
	.proc_read = dev_coef_read_proc,
	.proc_write = dev_coef_write_proc,
};

#else
static struct file_operations als_type_fops = {
	.read = als_type_read_proc,
};

static struct file_operations red_max_lux_fops = {
	.read = red_max_lux_read_proc,
	.write = red_max_lux_write_proc,
};

static struct file_operations white_max_lux_fops = {
	.read = white_max_lux_read_proc,
	.write = white_max_lux_write_proc,
};

static struct file_operations blue_max_lux_fops = {
	.read = blue_max_lux_read_proc,
	.write = blue_max_lux_write_proc,
};

static struct file_operations green_max_lux_fops = {
	.read = green_max_lux_read_proc,
	.write = green_max_lux_write_proc,
};

static struct file_operations cali_coe_fops = {
	.read = cali_coe_read_proc,
	.write = cali_coe_write_proc,
};

static struct file_operations row_coe_fops = {
	.read = row_coe_read_proc,
	.write = row_coe_write_proc,
};

static struct file_operations dev_coef_fops = {
	.read = dev_coef_read_proc,
	.write = dev_coef_write_proc,
};
#endif

static int oplus_als_cali_data_init(void)
{
	int rc = 0;
	struct proc_dir_entry *pentry;

	pr_info("%s call\n", __func__);

	if (gdata->proc_oplus_als) {
		printk("proc_oplus_als has alread inited\n");
		return 0;
	}

	gdata->proc_oplus_als =  proc_mkdir("als_cali", sensor_proc_dir);

	if (!gdata->proc_oplus_als) {
		pr_err("can't create proc_oplus_als proc\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("red_max_lux", 0666, gdata->proc_oplus_als,
			&red_max_lux_fops);

	if (!pentry) {
		pr_err("create red_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("green_max_lux", 0666, gdata->proc_oplus_als,
			&green_max_lux_fops);

	if (!pentry) {
		pr_err("create green_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("blue_max_lux", 0666, gdata->proc_oplus_als,
			&blue_max_lux_fops);

	if (!pentry) {
		pr_err("create blue_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("white_max_lux", 0666, gdata->proc_oplus_als,
			&white_max_lux_fops);

	if (!pentry) {
		pr_err("create white_max_lux proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("cali_coe", 0666, gdata->proc_oplus_als,
			&cali_coe_fops);

	if (!pentry) {
		pr_err("create cali_coe proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("row_coe", 0666, gdata->proc_oplus_als,
			&row_coe_fops);

	if (!pentry) {
		pr_err("create row_coe proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("dev_coef", 0666, gdata->proc_oplus_als,
			&dev_coef_fops);

	if (!pentry) {
		pr_err("create dev_coef_l2h proc failed.\n");
		rc = -EFAULT;
		return rc;
	}


	pentry = proc_create("als_type", 0666, gdata->proc_oplus_als,
			&als_type_fops);

	if (!pentry) {
		pr_err("create als_type_fops proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	return 0;
}

static int sensor_ldo_init(struct device *dev)
{
	int ldo;
	struct device_node *np = dev->of_node;
	struct pinctrl	   *gpio_pinctrl;
	struct pinctrl_state *gpio_active;
	struct pinctrl_state *gpio_sleep;
	char * active = "sensor_ldo_active";
	char * sleep  = "sensor_ldo_sleep";

	if (of_property_read_u32(np, "ldo-gpio", &ldo)) {
		dev_err(dev, "%s: ldo-gpio init fail\n", __func__);
		return -EINVAL;
	}
	gpio_pinctrl = devm_pinctrl_get(dev);
	if (gpio_pinctrl == NULL) {
		dev_err(dev, "%s: ldo_pinctrl init fail\n", __func__);
		return -EINVAL;
	}

	gpio_active = pinctrl_lookup_state(gpio_pinctrl, active);
	if (gpio_active == NULL) {
		dev_err(dev, "%s: ldo_active init fail\n", __func__);
		return -EINVAL;
	}

	gpio_sleep = pinctrl_lookup_state(gpio_pinctrl, sleep);
	if (gpio_sleep == NULL) {
		dev_err(dev, "%s: ldo_sleep init fail\n", __func__);
		return -EINVAL;
	}
	gpio_direction_output(ldo, 1);
	pinctrl_select_state(gpio_pinctrl, gpio_active);
	return 0;
}

static int oplus_devinfo_probe(struct platform_device *pdev)
{
	struct sensor_info * chip = NULL;
	struct sensor_info_old * chip_old = NULL;
	size_t smem_size = 0;
	void *smem_addr = NULL;
	int rc = 0;
	struct oplus_als_cali_data *data = NULL;

	pr_info("%s call\n", __func__);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		smem_size = ALIGN4(struct sensor_info);
		rc = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_SENSOR, smem_size);
		if (rc < 0 && rc != -EEXIST) {
				pr_err("%s smem_alloc fail\n", __func__);
				rc = -EFAULT;
				return rc;
		}

		smem_size = 0;
#endif
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
			SMEM_SENSOR,
			&smem_size);

	if (IS_ERR(smem_addr)) {
		pr_err("unable to acquire smem SMEM_SENSOR entry, smem_addr %p\n", smem_addr);
		return -EPROBE_DEFER; /*return -EPROBE_DEFER if smem not ready*/
	}

	if (smem_size == sizeof(struct sensor_info)) {
		chip = (struct sensor_info *)(smem_addr);
		memset(chip, 0, sizeof(struct sensor_info));
	} else if (smem_size == sizeof(struct sensor_info_old)) {
		chip_old = (struct sensor_info_old *)(smem_addr);
		memset(chip_old, 0, sizeof(struct sensor_info_old));
	} else {
		pr_err("unsupported SMEM_SENSOR size: %d\n", smem_size);
		return -EFAULT;
	}

	if (gdata) {
		printk("%s:just can be call one time\n", __func__);
		return 0;
	}

	data = kzalloc(sizeof(struct oplus_als_cali_data), GFP_KERNEL);

	if (data == NULL) {
		rc = -ENOMEM;
		printk("%s:kzalloc fail %d\n", __func__, rc);
		return rc;
	}

	gdata = data;

	g_chip = chip;
	g_chip_old = chip_old;

	oplus_sensor_parse_dts(pdev);

	pr_info("%s success\n", __func__);

	sensor_proc_dir = proc_mkdir("sensor", NULL);
	if (!sensor_proc_dir) {
		pr_err("can't create proc_sensor proc\n");
		rc = -EFAULT;
		return rc;
	}
	oplus_press_cali_data_init();
	rc = oplus_als_cali_data_init();

	if (rc < 0) {
		kfree(gdata);
		gdata = NULL;
	}
	if (g_ldo_enable) {
		sensor_ldo_init(&pdev->dev);
	}
	return 0;
}

static int oplus_devinfo_remove(struct platform_device *pdev)
{
	if (gdata) {
		kfree(gdata);
		gdata = NULL;
	}

	oplus_press_cali_data_clean();

	return 0;
}

static const struct of_device_id of_drv_match[] = {
	{ .compatible = "oplus,sensor-devinfo"},
	{},
};
MODULE_DEVICE_TABLE(of, of_drv_match);

static struct platform_driver _driver = {
	.probe	  = oplus_devinfo_probe,
	.remove	 = oplus_devinfo_remove,
	.driver	 = {
	.name	   = "sensor_devinfo",
	.of_match_table = of_drv_match,
	},
};

static int __init oplus_devinfo_init(void)
{
	pr_info("oplus_devinfo_init call\n");

	platform_driver_register(&_driver);
	return 0;
}

arch_initcall(oplus_devinfo_init);

MODULE_DESCRIPTION("sensor devinfo");
MODULE_LICENSE("GPL");

