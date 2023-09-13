// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define SIM_DETECT_NAME "sim_detect"

/**for sim_detect log**/
#define SIMDETECT_ERR(a, arg...)  pr_err("[sim_detect]:" a, ##arg)

/**sim_detect log end**/


#define MODEM_DETECT_CMD 55

static struct of_device_id sim_detect_id[] = {
	{.compatible = "oplus, sim_detect", },
	{},
};

struct sim_detect_data {
	struct platform_device *pdev;
	int sim_detect;
};

#ifdef CONFIG_OEM_QMI
extern int oem_qmi_common_req(u32 cmd_type, const char *req_data, u32 req_len,
	char *resp_data, u32 resp_len);
#else
static int oem_qmi_common_req(u32 cmd_type, const char *req_data, u32 req_len,
	char *resp_data, u32 resp_len) {
	return -1;
}
#endif

static ssize_t proc_sim_detect_read(struct file *file,
                                    char __user *user_buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	char page[25] = {0};
	int sim_detect_value = -1;
	struct sim_detect_data *sim_detect_data = PDE_DATA(file_inode(file));

	if (!sim_detect_data)
		return 0;

	if (sim_detect_data->sim_detect >= 0) {
		sim_detect_value = gpio_get_value(sim_detect_data->sim_detect);

	} else {
		char resp_data[8] = {0};
		if (oem_qmi_common_req(MODEM_DETECT_CMD, NULL, 0, resp_data, 8)) {
			SIMDETECT_ERR("failed to read status from modem\n");
		} else {
			sim_detect_value = resp_data[0];
		}
	}

	SIMDETECT_ERR("sim_detect_value:%d\n", sim_detect_value);

	ret = snprintf(page, sizeof(page) - 1, "%d\n", sim_detect_value);
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static const struct proc_ops sim_detect_ops = {
	.proc_read  = proc_sim_detect_read,
	.proc_open  = simple_open,
};
#else
static const struct file_operations sim_detect_ops = {
	.read  = proc_sim_detect_read,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#endif

static int sim_card_detect_init(struct sim_detect_data *sim_detect_data)
{
	int ret = 0;
	struct device_node *np = NULL;
	struct proc_dir_entry *p = NULL;

	np = sim_detect_data->pdev->dev.of_node;
	sim_detect_data->sim_detect = of_get_named_gpio(np, "Hw,sim_det", 0);

	if (sim_detect_data->sim_detect < 0) {
		const char *out_string;
		SIMDETECT_ERR("sim detect gpio not specified\n");
		if (of_property_read_string(np, "Hw,sim_det", &out_string)
			|| strcmp(out_string, "modem_det")) {
			SIMDETECT_ERR("modem det not specified\n");
			ret = -1;
			goto err;
		}
	}

	p = proc_create_data("sim_detect", 0644, NULL, &sim_detect_ops,
                         sim_detect_data);
	if (!p) {
		SIMDETECT_ERR("proc create sim detect failed\n");
		ret = -1;
		goto err;
	}
err:
	return ret;
}

static int sim_detect_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct sim_detect_data *sim_detect_data = NULL;

	SIMDETECT_ERR("sim_detect_probe enter\n");
	sim_detect_data = devm_kzalloc(&pdev->dev, sizeof(struct sim_detect_data), GFP_KERNEL);

	if (IS_ERR_OR_NULL(sim_detect_data)) {
		SIMDETECT_ERR("sim_detect_data kzalloc failed\n");
		ret = -ENOMEM;
		return ret;
	}

	/*parse_dts*/
	sim_detect_data->pdev = pdev;

	sim_card_detect_init(sim_detect_data);

	platform_set_drvdata(pdev, sim_detect_data);

	return ret;
}

static int sim_detect_remove(struct platform_device *pdev)
{
	struct sim_detect_data *sim_detect_data = platform_get_drvdata(pdev);

	if (sim_detect_data) {
		remove_proc_entry(SIM_DETECT_NAME, NULL);
	}
	return 0;
}

static struct platform_driver sim_detect_platform_driver = {
	.probe = sim_detect_probe,
	.remove = sim_detect_remove,
	.driver = {
		.name = SIM_DETECT_NAME,
		.of_match_table = sim_detect_id,
	},
};

module_platform_driver(sim_detect_platform_driver);

MODULE_DESCRIPTION("sim_detect");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qicai.gu <qicai.gu>");
