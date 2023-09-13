#include "fingerprint_ree_dts.h"
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include "fingerprint_log.h"
int g_cs_gpio_disable;

void fp_cleanup_pwr_list(struct spidev_data *dev) {
    unsigned index = 0;
    FP_LOGI("%s cleanup power list", __func__);
    for (index = 0; index < dev->power_num; index++) {
        if (dev->pwr_list[index].pwr_type == FP_POWER_MODE_GPIO) {
            if (gpio_is_valid(dev->irq_gpio)) {
                gpio_free(dev->pwr_list[index].pwr_gpio);
                FP_LOGI("remove pwr_gpio success\n");
            }
        }
        if (dev->pwr_list[index].pwr_type == FP_POWER_MODE_LDO) {
            memset(&(dev->pwr_list[index]), 0, sizeof(fp_power_info_t));
        }
    }
}

static int vreg_setup(struct spidev_data *fp_dev, fp_power_info_t *pwr_info,
    bool enable)
{
    int rc;
    struct regulator *vreg;
    struct device *dev = &fp_dev->spi->dev;
    const char *name = NULL;

    if (NULL == pwr_info || NULL == pwr_info->vreg_config.name) {
        FP_LOGE("pwr_info is NULL\n");
        return -EINVAL;
    }
    vreg = pwr_info->vreg;
    name = pwr_info->vreg_config.name;
    FP_LOGI("Regulator %s vreg_setup,enable=%d \n", name, enable);

    if (enable) {
        if (!vreg) {
            vreg = regulator_get(dev, name);
            if (IS_ERR(vreg)) {
                FP_LOGE("Unable to get  %s\n", name);
                return PTR_ERR(vreg);
            }
        }
        if (regulator_count_voltages(vreg) > 0) {
            rc = regulator_set_voltage(vreg, pwr_info->vreg_config.vmin,
                    pwr_info->vreg_config.vmax);
            if (rc) {
                FP_LOGE("Unable to set voltage on %s, %d\n", name, rc);
            }
        }
        rc = regulator_set_load(vreg, pwr_info->vreg_config.ua_load);
        if (rc < 0) {
            FP_LOGE("Unable to set current on %s, %d\n", name, rc);
        }
        rc = regulator_enable(vreg);
        if (rc) {
            FP_LOGE("error enabling %s: %d\n", name, rc);
            regulator_put(vreg);
            vreg = NULL;
        }
        pwr_info->vreg = vreg;
    } else {
        if (vreg) {
            if (regulator_is_enabled(vreg)) {
                regulator_disable(vreg);
                FP_LOGE("disabled %s\n", name);
            }
            regulator_put(vreg);
            pwr_info->vreg = NULL;
        }
        FP_LOGE("disable vreg is null \n");
        rc = 0;
    }
    return rc;
}

int parse_kernel_pwr_list(struct spidev_data *fp_dev) {
    int ret = 0;
    struct device *dev = &fp_dev->spi->dev;
    struct device_node *np = dev->of_node;
    struct device_node *child = NULL;
    unsigned child_node_index = 0;
    int ldo_param_amount = 0;
    const char *node_name = NULL;
    fp_power_info_t *pwr_list = fp_dev->pwr_list;

    fp_cleanup_pwr_list(fp_dev);

    for_each_available_child_of_node(np, child) {
        if (child_node_index >= FP_MAX_PWR_LIST_LEN) {
            FP_LOGE("too many nodes");
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }
        //get power mode for dts
        ret = of_property_read_u32(child, FP_POWER_NODE, &(pwr_list[child_node_index].pwr_type));
        if (ret) {
            FP_LOGE("failed to request %s, ret = %d\n", FP_POWER_NODE, ret);
            goto exit;
        }

        switch (pwr_list[child_node_index].pwr_type) {
        case FP_POWER_MODE_LDO:
            ret = of_property_read_string(child, FP_POWER_NAME_NODE, &(pwr_list[child_node_index].vreg_config.name));
            if (ret) {
                FP_LOGE("the param %s is not found !\n", FP_POWER_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            FP_LOGD("get ldo node name %s", pwr_list[child_node_index].vreg_config.name);

            /* read ldo config name */
            ret = of_property_read_string(child, FP_POWER_CONFIG, &node_name);
            if (ret) {
                FP_LOGE("the param %s is not found !\n", FP_POWER_CONFIG);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            FP_LOGD("get config node name %s", node_name);

            ldo_param_amount = of_property_count_elems_of_size(np, node_name, sizeof(u32));
            FP_LOGD("get ldo_param_amount %d", ldo_param_amount);
            if (ldo_param_amount != LDO_PARAM_AMOUNT) {
                FP_LOGE("failed to request size %s\n", node_name);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_VMAX_INDEX, &(pwr_list[child_node_index].vreg_config.vmax));
            if (ret) {
                FP_LOGE("failed to request %s(%d), rc = %u\n", node_name, LDO_VMAX_INDEX, ret);
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_VMIN_INDEX, &(pwr_list[child_node_index].vreg_config.vmin));
            if (ret) {
                FP_LOGE("failed to request %s(%d), rc = %u\n", node_name, LDO_VMIN_INDEX, ret);
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_UA_INDEX, &(pwr_list[child_node_index].vreg_config.ua_load));
            if (ret) {
                FP_LOGE("failed to request %s(%d), rc = %u\n", node_name, LDO_UA_INDEX, ret);
                goto exit;
            }

            FP_LOGI("%s size = %d, ua=%d, vmax=%d, vmin=%d\n", node_name, ldo_param_amount,
                    pwr_list[child_node_index].vreg_config.ua_load,
                    pwr_list[child_node_index].vreg_config.vmax,
                    pwr_list[child_node_index].vreg_config.vmin);
            break;
        case FP_POWER_MODE_GPIO:
            /* read GPIO name */
            ret = of_property_read_string(child, FP_POWER_NAME_NODE, &node_name);
            if (ret) {
                FP_LOGE("the param %s is not found !\n", FP_POWER_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            FP_LOGI("get config node name %s", node_name);

            /* get gpio by name */
            fp_dev->pwr_list[child_node_index].pwr_gpio = of_get_named_gpio(np, node_name, 0);
            FP_LOGD("end of_get_named_gpio %s, pwr_gpio: %d!\n", node_name, pwr_list[child_node_index].pwr_gpio);
            if (fp_dev->pwr_list[child_node_index].pwr_gpio < 0) {
                FP_LOGE("falied to get goodix_pwr gpio!\n");
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            /* get poweron-level of gpio */
            FP_LOGI("get poweron level: %s", FP_POWERON_LEVEL_NODE);
            ret = of_property_read_u32(child, FP_POWERON_LEVEL_NODE, &pwr_list[child_node_index].poweron_level);
            if (ret) {
                /* property of poweron-level is not config, by default set to 1 */
                pwr_list[child_node_index].poweron_level = 1;
            } else {
                if (pwr_list[child_node_index].poweron_level != 0) {
                    pwr_list[child_node_index].poweron_level = 1;
                }
            }
            FP_LOGI("gpio poweron level: %d\n", pwr_list[child_node_index].poweron_level);

            ret = devm_gpio_request(dev, pwr_list[child_node_index].pwr_gpio, node_name);
            if (ret) {
                FP_LOGE("failed to request %s gpio, ret = %d\n", node_name, ret);
                goto exit;
            }
            gpio_direction_output(pwr_list[child_node_index].pwr_gpio, (pwr_list[child_node_index].poweron_level == 0 ? 1: 0));
            FP_LOGE("set goodix_pwr %u output %d \n", child_node_index, pwr_list[child_node_index].poweron_level);
            break;
        default:
            FP_LOGE("unknown type %u\n", pwr_list[child_node_index].pwr_type);
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }
        /* get delay time of this power */
        ret = of_property_read_u32(child, FP_POWER_DELAY_TIME, &pwr_list[child_node_index].delay);
        if (ret) {
            FP_LOGE("failed to request %s, ret = %d\n", FP_POWER_NODE, ret);
            goto exit;
        }
        child_node_index++;
    }
    fp_dev->power_num = child_node_index;
exit:
    if (ret) {
        fp_cleanup_pwr_list(fp_dev);
    }
    return ret;
}

int fp_parse_dts(struct spidev_data *dev)
{
    int rc = 0;
    struct device_node *node = NULL;
    struct platform_device *pdev = NULL;
    dev->cs_gpio_set = false;
    dev->pinctrl = NULL;
    dev->pstate_spi_6mA = NULL;
    dev->pstate_default = NULL;
    dev->pstate_cs_func = NULL;
    dev->pstate_irq_no_pull = NULL;
    dev->is_optical = false;
    g_cs_gpio_disable = 0;
    node = of_find_compatible_node(NULL, NULL, "goodix,goodix_fp");

    if (node) {
        pdev = of_find_device_by_node(node);
        if (pdev == NULL) {
            FP_LOGE("[err] %s can not find device by node \n", __func__);
            return -1;
        }
    } else {
        FP_LOGE("[err] %s can not find compatible node \n", __func__);
        return -1;
    }

    rc = of_property_read_u32(node, "gf,cs_gpio_disable", &g_cs_gpio_disable);
    if (rc) {
        dev_err(&pdev->dev, "failed to request gf,cs_gpio_disable, ret = %d\n", rc);
        g_cs_gpio_disable = 0;
    }

    /*get clk pinctrl resource*/
    dev->pinctrl = devm_pinctrl_get(&pdev->dev);
    if (IS_ERR(dev->pinctrl)) {
        dev_err(&pdev->dev, "can not get the gf pinctrl");
        return PTR_ERR(dev->pinctrl);
    }

    if (g_cs_gpio_disable != 1) {
        dev->pstate_cs_func = pinctrl_lookup_state(dev->pinctrl, "gf_cs_func");
        if (IS_ERR(dev->pstate_cs_func)) {
            dev_err(&pdev->dev, "Can't find gf_cs_func pinctrl state\n");
            return PTR_ERR(dev->pstate_cs_func);
        }
    }

    if (dev->is_optical == true) {
        dev->pstate_spi_6mA  = pinctrl_lookup_state(dev->pinctrl, "gf_spi_drive_6mA");
        if (IS_ERR(dev->pstate_spi_6mA)) {
            dev_err(&pdev->dev, "Can't find gf_spi_drive_6mA pinctrl state\n");
            return PTR_ERR(dev->pstate_spi_6mA);
        }
        pinctrl_select_state(dev->pinctrl, dev->pstate_spi_6mA);

        dev->pstate_default = pinctrl_lookup_state(dev->pinctrl, "default");
        if (IS_ERR(dev->pstate_default)) {
            dev_err(&pdev->dev, "Can't find default pinctrl state\n");
            return PTR_ERR(dev->pstate_default);
        }
        pinctrl_select_state(dev->pinctrl, dev->pstate_default);
    } else {
        dev->pstate_irq_no_pull = pinctrl_lookup_state(dev->pinctrl, "gf_spi_drive_6mA");
        if (IS_ERR(dev->pstate_irq_no_pull)) {
            dev_err(&pdev->dev, "Can't find irq_no_pull pinctrl state\n");
            return PTR_ERR(dev->pstate_irq_no_pull);
        } else {
            pinctrl_select_state(dev->pinctrl, dev->pstate_irq_no_pull);
        }

        dev->pstate_default = pinctrl_lookup_state(dev->pinctrl, "default");
        if (IS_ERR(dev->pstate_default)) {
            dev_err(&pdev->dev, "Can't find default pinctrl state\n");
            return PTR_ERR(dev->pstate_default);
        }
        pinctrl_select_state(dev->pinctrl, dev->pstate_default);
    }

    /*get reset resource*/
    dev->reset_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_reset", 0);
    if (!gpio_is_valid(dev->reset_gpio)) {
        FP_LOGI("RESET GPIO is invalid.\n");
        return -1;
    }
    rc = gpio_request(dev->reset_gpio, "goodix_reset");
    if (rc) {
        dev_err(&dev->spi->dev, "Failed to request RESET GPIO. rc = %d\n", rc);
        return -1;
    }

    gpio_direction_output(dev->reset_gpio, 0);

    msleep(3);

    /*get cs resource*/
    if (g_cs_gpio_disable != 1) {
        dev->cs_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_cs", 0);
        if (!gpio_is_valid(dev->cs_gpio)) {
            FP_LOGI("CS GPIO is invalid.\n");
            return -1;
        }
        rc = gpio_request(dev->cs_gpio, "goodix_cs");
        if (rc) {
            dev_err(&dev->spi->dev, "Failed to request CS GPIO. rc = %d\n", rc);
            return -1;
        }
        gpio_direction_output(dev->cs_gpio, 0);
        dev->cs_gpio_set = true;
    }

    /*get irq resourece*/
    dev->irq_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_irq", 0);
    if (!gpio_is_valid(dev->irq_gpio)) {
        FP_LOGI("IRQ GPIO is invalid.\n");
        return -1;
    }

    rc = gpio_request(dev->irq_gpio, "goodix_irq");
    if (rc) {
        dev_err(&dev->spi->dev, "Failed to request IRQ GPIO. rc = %d\n", rc);
        return -1;
    }
    gpio_direction_input(dev->irq_gpio);

    rc = parse_kernel_pwr_list(dev);
    if (rc) {
        FP_LOGE("failed to parse power list, rc = %d\n", rc);
        fp_cleanup_pwr_list(dev);
        return rc;
    }
    return 0;
}

void sensor_cleanup(struct spidev_data *dev) {
    FP_LOGI("[info] %s\n", __func__);
    if (gpio_is_valid(dev->irq_gpio)) {
        gpio_free(dev->irq_gpio);
        FP_LOGI("remove irq_gpio success\n");
    }
    if (gpio_is_valid(dev->cs_gpio)) {
        gpio_free(dev->cs_gpio);
        FP_LOGI("remove cs_gpio success\n");
    }
    if (gpio_is_valid(dev->reset_gpio)) {
        gpio_free(dev->reset_gpio);
        FP_LOGI("remove reset_gpio success\n");
    }

    fp_cleanup_pwr_list(dev);
}

// power function
int fp_power_on(struct spidev_data *dev)
{
    int rc = 0;
    unsigned index = 0;
    for (index = 0; index < dev->power_num; index++) {
        switch (dev->pwr_list[index].pwr_type) {
        case FP_POWER_MODE_LDO:
            rc = vreg_setup(dev, &(dev->pwr_list[index]), true);
            FP_LOGI(" power on ldo \n");
            break;
        case FP_POWER_MODE_GPIO:
            gpio_set_value(dev->pwr_list[index].pwr_gpio, dev->pwr_list[index].poweron_level);
            FP_LOGI("set pwr_gpio %d\n", dev->pwr_list[index].poweron_level);
            break;
        default:
            rc = -1;
            FP_LOGI(" power on mode not set !!! \n");
            break;
        }

        if (rc) {
            FP_LOGE(" power on failed with mode = %d, index = %d, rc = %d \n",
                dev->pwr_list[index].pwr_type, index, rc);
            break;
        } else {
            FP_LOGI(" power on ok with mode = %d, index = %d  \n",
                    dev->pwr_list[index].pwr_type, index);
        }
        msleep(dev->pwr_list[index].delay);
    }

    msleep(30);
    return rc;
}

int fp_power_off(struct spidev_data *dev)
{
    int rc = 0;
    unsigned index = 0;

    for (index = 0; index < dev->power_num; index++) {
        switch (dev->pwr_list[index].pwr_type) {
        case FP_POWER_MODE_LDO:
            rc = vreg_setup(dev, &(dev->pwr_list[index]), false);
            FP_LOGI(" power on ldo \n");
            break;
        case FP_POWER_MODE_GPIO:
            gpio_set_value(dev->pwr_list[index].pwr_gpio, (dev->pwr_list[index].poweron_level == 0 ? 1 : 0));
            FP_LOGI("set pwr_gpio %d\n", (dev->pwr_list[index].poweron_level == 0 ? 1 : 0));
            break;
        default:
            rc = -1;
            FP_LOGI(" power on mode not set !!! \n");
            break;
        }

        if (rc) {
            FP_LOGE(" power off failed with mode = %d, index = %d, rc = %d \n",
                    dev->pwr_list[index].pwr_type, index, rc);
            break;
        } else {
            FP_LOGI(" power off ok with mode = %d, index = %d  \n",
                    dev->pwr_list[index].pwr_type, index);
        }
    }
    msleep(30);
    return rc;
}

// hardware control
int fp_hw_reset(struct spidev_data *dev, unsigned int delay_ms) {
    if (dev == NULL) {
        FP_LOGI("Input buff is NULL.\n");
        return -1;
    }

    gpio_set_value(dev->reset_gpio, 0);
    mdelay(20);
    gpio_set_value(dev->reset_gpio, 1);
    if (dev->cs_gpio_set) {
        FP_LOGI(" pull CS up and set CS from gpio to func ");
        gpio_set_value(dev->cs_gpio, 1);
        pinctrl_select_state(dev->pinctrl, dev->pstate_cs_func);
        dev->cs_gpio_set = false;
    }
    mdelay(delay_ms);
    return 0;
}

int fp_irq_num(struct spidev_data *dev) {
    if (dev == NULL) {
        FP_LOGI("Input buff is NULL.\n");
        return -1;
    } else {
        return gpio_to_irq(dev->irq_gpio);
    }
}
