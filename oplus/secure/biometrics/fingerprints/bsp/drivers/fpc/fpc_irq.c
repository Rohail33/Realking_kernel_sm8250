// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/version.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
//#include <linux/irq.h>

#define DEBUG
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_wakeup.h>
#include <linux/spi/spi.h>
#include "../include/oplus_fp_common.h"
#include <soc/oplus/system/oplus_project.h>

#define FPC_IRQ_DEV_NAME    "fpc_irq"

/* Uncomment if DeviceTree should be used */

//#include <mtk_spi_hal.h>
#include <linux/platform_data/spi-mt65xx.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

// Platform specific


#define   FPC1020_RESET_LOW_US                              1000
#define   FPC1020_RESET_HIGH1_US                            100
#define   FPC1020_RESET_HIGH2_US                            1250
#define   FPC_TTW_HOLD_TIME                                 1000
#define   FPC_IRQ_WAKELOCK_TIMEOUT                          500
#define   FPC_WL_WAKELOCK_TIMEOUT                          0

#define   WAKELOCK_DISABLE                                  0
#define   WAKELOCK_ENABLE                                   1
#define   WAKELOCK_TIMEOUT_ENABLE                           2
#define   WAKELOCK_TIMEOUT_DISABLE                          3

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
struct mtk_spi {
    void __iomem                                        *base;
        void __iomem                                        *peri_regs;
        u32                                                 state;
        int                                                 pad_num;
        u32                                                 *pad_sel;
        struct clk                                          *parent_clk, *sel_clk, *spi_clk;
        struct spi_transfer                                 *cur_transfer;
        u32                                                 xfer_len;
        struct scatterlist                                  *tx_sgl, *rx_sgl;
        u32                                                 tx_sgl_len, rx_sgl_len;
        const struct mtk_spi_compatible                     *dev_comp;
        u32                                                 dram_8gb_offset;
};
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(4, 14, 0))
struct mtk_spi {
    void __iomem *base;
    u32 state;
    int pad_num;
    u32 *pad_sel;
    struct clk *parent_clk, *sel_clk, *spi_clk;
    struct spi_transfer *cur_transfer;
    u32 xfer_len;
    u32 num_xfered;
    struct scatterlist *tx_sgl, *rx_sgl;
    u32 tx_sgl_len, rx_sgl_len;
    const struct mtk_spi_compatible *dev_comp;
};
#else
struct mtk_spi {
    void __iomem *base;
    u32 state;
    int pad_num;
    u32 *pad_sel;
    struct clk *parent_clk, *sel_clk, *spi_clk, *spare_clk;
    struct spi_transfer *cur_transfer;
    u32 xfer_len;
    u32 num_xfered;
    struct scatterlist *tx_sgl, *rx_sgl;
    u32 tx_sgl_len, rx_sgl_len;
    const struct mtk_spi_compatible *dev_comp;
};
#endif

struct clk *globle_spi_clk;
struct mtk_spi *fpc_ms;
int g_use_gpio_power_enable;

typedef struct {
        struct spi_device                                   *spi;
        struct class                                        *class;
        struct device                                       *device;
        dev_t                                               devno;
        u8                                                  *huge_buffer;
        size_t                                              huge_buffer_size;
        struct input_dev                                    *input_dev;
} fpc1020_data_t;

struct vreg_config {
        char *name;
        unsigned long vmin;
        unsigned long vmax;
        int ua_load;
};

struct fpc1020_data {
        struct device *dev;
        struct platform_device *pldev;
        int irq_gpio;
        int rst_gpio;
        int vdd_en_gpio;
        int cs_gpio;
        bool cs_gpio_set;
        struct pinctrl *pinctrl;
        struct pinctrl_state *pstate_cs_func;
        struct input_dev *idev;
        int irq_num;
        struct mutex lock;
        bool prepared;
        int irq_enabled;
        struct wakeup_source ttw_wl;
        struct wakeup_source fpc_wl;
        struct wakeup_source fpc_irq_wl;
        //struct regulator                                *vreg[ARRAY_SIZE(vreg_conf)];
        unsigned power_num;
        fp_power_info_t pwr_list[FP_MAX_PWR_LIST_LEN];
};

static inline void fpc_wakeup_source_init(struct wakeup_source *ws,
                const char *name)
{
        if (ws) {
                memset(ws, 0, sizeof(*ws));
                ws->name = name;
        }
        wakeup_source_add(ws);
}

static inline void fpc_wakeup_source_trash(struct wakeup_source *ws)
{
        wakeup_source_remove(ws);
        if (!ws) {
                return;
        }
        __pm_relax(ws);
}

static int fpc1020_request_named_gpio(struct fpc1020_data *fpc1020,
                const char *label, int *gpio)
{
        struct device *dev = fpc1020->dev;
        struct device_node *np = dev->of_node;
        int rc = of_get_named_gpio(np, label, 0);
        if (rc < 0) {
                dev_err(dev, "failed to get '%s'\n", label);
                return rc;
        }

        *gpio = rc;
        rc = devm_gpio_request(dev, *gpio, label);
        if (rc) {
                dev_err(dev, "failed to request gpio %d\n", *gpio);
                return rc;
        }

        /*dev_info(dev, "%s - gpio: %d\n", label, *gpio);*/
        return 0;
}


static int vreg_setup(struct fpc1020_data *fpc_fp, fp_power_info_t *pwr_info,
        bool enable)
{
    int rc;
    struct regulator *vreg;
    struct device *dev = fpc_fp->dev;
    char *name = NULL;

    if (NULL == pwr_info) {
        pr_err("pwr_info is NULL\n");
        return -EINVAL;
    }
    name = (char *)pwr_info->vreg_config.name;
    if (NULL == name) {
        pr_err("name is NULL\n");
        return -EINVAL;
    }
    pr_err("Regulator %s vreg_setup,enable=%d \n", name, enable);

    vreg = pwr_info->vreg;
    if (enable) {
        if (!vreg) {
            vreg = regulator_get(dev, name);
            if (IS_ERR(vreg)) {
                pr_err("Unable to get  %s\n", name);
                return PTR_ERR(vreg);
            }
        }
        if (regulator_count_voltages(vreg) > 0) {
            rc = regulator_set_voltage(vreg, pwr_info->vreg_config.vmin,
                    pwr_info->vreg_config.vmax);
            if (rc) {
                pr_err("Unable to set voltage on %s, %d\n", name, rc);
            }
        }
        rc = regulator_set_load(vreg, pwr_info->vreg_config.ua_load);
        if (rc < 0) {
            pr_err("Unable to set current on %s, %d\n", name, rc);
        }
        rc = regulator_enable(vreg);
        if (rc) {
            pr_err("error enabling %s: %d\n", name, rc);
            regulator_put(vreg);
            vreg = NULL;
        }
        pwr_info->vreg = vreg;
    } else {
        if (vreg) {
            if (regulator_is_enabled(vreg)) {
                regulator_disable(vreg);
                pr_err("disabled %s\n", name);
            }
            regulator_put(vreg);
            pwr_info->vreg = NULL;
        }
        pr_err("disable vreg is null \n");
        rc = 0;
    }
    return rc;
}


/*power on auto during boot, no need fp driver power on*/
int fpc_power_on(struct fpc1020_data* fpc_dev)
{
    int rc = 0;
    unsigned index = 0;

    for (index = 0; index < fpc_dev->power_num; index++) {
        switch (fpc_dev->pwr_list[index].pwr_type) {
        case FP_POWER_MODE_LDO:
            rc = vreg_setup(fpc_dev, &(fpc_dev->pwr_list[index]), true);
            pr_info("---- power on ldo ----\n");
            break;
        case FP_POWER_MODE_GPIO:
            gpio_set_value(fpc_dev->pwr_list[index].pwr_gpio, fpc_dev->pwr_list[index].poweron_level);
            pr_info("set pwr_gpio %d\n", fpc_dev->pwr_list[index].poweron_level);
            break;
        case FP_POWER_MODE_AUTO:
            pr_info("[%s] power on auto, no need power on again\n", __func__);
            break;
        case FP_POWER_MODE_NOT_SET:
        default:
            rc = -1;
            pr_info("---- power on mode not set !!! ----\n");
            break;
        }
        if (rc) {
            pr_err("---- power on failed with mode = %d, index = %d, rc = %d ----\n",
                    fpc_dev->pwr_list[index].pwr_type, index, rc);
            break;
        } else {
            pr_info("---- power on ok with mode = %d, index = %d  ----\n",
                    fpc_dev->pwr_list[index].pwr_type, index);
        }
        msleep(fpc_dev->pwr_list[index].delay);
    }

    msleep(30);
    return rc;
}

/*power off auto during shut down, no need fp driver power off*/
int fpc_power_off(struct fpc1020_data* fpc_dev)
{
    int rc = 0;
    unsigned index = 0;

    for (index = 0; index < fpc_dev->power_num; index++) {
        switch (fpc_dev->pwr_list[index].pwr_type) {
        case FP_POWER_MODE_LDO:
            rc = vreg_setup(fpc_dev, &(fpc_dev->pwr_list[index]), false);
            pr_info("---- power on ldo ----\n");
            break;
        case FP_POWER_MODE_GPIO:
            gpio_set_value(fpc_dev->pwr_list[index].pwr_gpio, (fpc_dev->pwr_list[index].poweron_level == 0 ? 1: 0));
            pr_info("set pwr_gpio %d\n", (fpc_dev->pwr_list[index].poweron_level == 0 ? 1: 0));
            break;
        case FP_POWER_MODE_AUTO:
            pr_info("[%s] power on auto, no need power on again\n", __func__);
            break;
        case FP_POWER_MODE_NOT_SET:
        default:
            rc = -1;
            pr_info("---- power on mode not set !!! ----\n");
            break;
        }
        if (rc) {
            pr_err("---- power off failed with mode = %d, index = %d, rc = %d ----\n",
                    fpc_dev->pwr_list[index].pwr_type, index, rc);
            break;
        } else {
            pr_info("---- power off ok with mode = %d, index = %d  ----\n",
                    fpc_dev->pwr_list[index].pwr_type, index);
        }
    }

    msleep(30);
    return rc;
}



/* -------------------------------------------------------------------------- */
/**
 * sysfs node for controlling whether the driver is allowed
 * to enable SPI clk.
 */

static void fpc_enable_clk(void)
{
        #if !defined(CONFIG_MTK_CLKMGR)
        clk_prepare_enable(globle_spi_clk);
        //pr_err("%s, clk_prepare_enable\n", __func__);
        #else
        enable_clock(MT_CG_PERI_SPI0, "spi");
        //pr_err("%s, enable_clock\n", __func__);
        #endif
        return;
}

static void fpc_disable_clk(void)
{
        #if !defined(CONFIG_MTK_CLKMGR)
        clk_disable_unprepare(globle_spi_clk);
        //pr_err("%s, clk_disable_unprepare\n", __func__);
        #else
        disable_clock(MT_CG_PERI_SPI0, "spi");
        //pr_err("%s, disable_clock\n", __func__);
        #endif
        return;
}

static ssize_t clk_enable_set(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
        dev_dbg(fpc1020->dev, "why should we set clocks here? i refuse,%s\n", __func__);
        dev_dbg(fpc1020->dev, " buff is %d, %s\n", *buf,__func__);

        //update spi clk
        if(*buf == '1'){
                fpc_enable_clk();
                //dev_info(fpc1020->dev, "%s: enable spi clk\n", __func__);
        }
        if(*buf == '0'){
                fpc_disable_clk();
                //dev_info(fpc1020->dev, "%s: disable spi clk\n", __func__);
        }
        return 1;//set_clks(fpc1020, (*buf == '1')) ? : count;
}

static DEVICE_ATTR(clk_enable, S_IWUSR, NULL, clk_enable_set);

static DEFINE_SPINLOCK(fpc1020_lock);

static int fpc1020_enable_irq(struct fpc1020_data *fpc1020, bool enable)
{
        spin_lock_irq(&fpc1020_lock);
        if (enable) {
                if (!fpc1020->irq_enabled) {
                        enable_irq(gpio_to_irq(fpc1020->irq_gpio));
                        enable_irq_wake(gpio_to_irq(fpc1020->irq_gpio));
                        fpc1020->irq_enabled = 1;
                        dev_info(fpc1020->dev, "%s: enable\n", __func__);
                } else {
                        /*dev_info(fpc1020->dev, "%s: no need enable\n", __func__);*/
                }
        } else {
                if (fpc1020->irq_enabled) {
                        disable_irq_wake(gpio_to_irq(fpc1020->irq_gpio));
                        disable_irq_nosync(gpio_to_irq(fpc1020->irq_gpio));
                        fpc1020->irq_enabled = 0;
                        dev_info(fpc1020->dev, "%s: disable\n", __func__);
                } else {
                        /*dev_info(fpc1020->dev, "%s: no need disable\n", __func__);*/
                }
        }
        spin_unlock_irq(&fpc1020_lock);

        return 0;
}

/**
 * sysf node to check the interrupt status of the sensor, the interrupt
 * handler should perform sysf_notify to allow userland to poll the node.
 */
static ssize_t irq_get(struct device *device,
                struct device_attribute *attribute,
                char *buffer)
{
        struct fpc1020_data* fpc1020 = dev_get_drvdata(device);
        int irq = gpio_get_value(fpc1020->irq_gpio);
        return scnprintf(buffer, PAGE_SIZE, "%i\n", irq);
}


/**
 * writing to the irq node will just drop a printk message
 * and return success, used for latency measurement.
 */
static ssize_t irq_ack(struct device *device,
                struct device_attribute* attribute,
                const char *buffer, size_t count)
{
        struct fpc1020_data* fpc1020 = dev_get_drvdata(device);
        dev_dbg(fpc1020->dev, "%s\n", __func__);
        return count;
}


static ssize_t regulator_enable_set(struct device *dev,
                struct device_attribute *attribute, const char *buffer, size_t count)
{
        int op = 0;
        bool enable = false;
        int rc = 0;
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
        if (1 == sscanf(buffer, "%d", &op)) {
                if (op == 1) {
                        enable = true;
                        fpc_power_on(fpc1020);
                }
                else if (op == 0) {
                        enable = false;
                        fpc_power_off(fpc1020);
                }
        } else {
                printk("invalid content: '%s', length = %zd\n", buffer, count);
                return -EINVAL;
        }

        //rc = vreg_setup(fpc1020, "vdd_io", enable);
        return rc ? rc : count;
}

static ssize_t irq_enable_set(struct device *dev,
                struct device_attribute *attribute, const char *buffer, size_t count)
{
        int op = 0;
        bool enable = false;
        int rc = 0;
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
        if (1 == sscanf(buffer, "%d", &op)) {
                if (op == 1) {
                        enable = true;
                }
                else if (op == 0) {
                        enable = false;
                }
        } else {
                printk("invalid content: '%s', length = %zd\n", buffer, count);
                return -EINVAL;
        }
        rc = fpc1020_enable_irq(fpc1020,  enable);
        return rc ? rc : count;
}

static ssize_t irq_enable_get(struct device *dev,
                struct device_attribute* attribute,
                char *buffer)
{
        struct fpc1020_data* fpc1020 = dev_get_drvdata(dev);
        return scnprintf(buffer, PAGE_SIZE, "%i\n", fpc1020->irq_enabled);
}

static ssize_t wakelock_enable_set(struct device *dev,
                struct device_attribute *attribute, const char *buffer, size_t count)
{
        int op = 0;
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
        if (1 == sscanf(buffer, "%d", &op)) {
                if (op == WAKELOCK_ENABLE) {
                        __pm_wakeup_event(&fpc1020->fpc_wl, FPC_WL_WAKELOCK_TIMEOUT);
                        dev_info(dev, "%s, fpc wake_lock\n", __func__);
                } else if (op == WAKELOCK_DISABLE) {
                        __pm_relax(&fpc1020->fpc_wl);
                        dev_info(dev, "%s, fpc wake_unlock\n", __func__);
                } else if (op == WAKELOCK_TIMEOUT_ENABLE) {
                        __pm_wakeup_event(&fpc1020->ttw_wl, FPC_TTW_HOLD_TIME);
                        dev_info(dev, "%s, fpc wake_lock timeout\n", __func__);
                } else if (op == WAKELOCK_TIMEOUT_DISABLE) {
                        __pm_relax(&fpc1020->ttw_wl);
                        dev_info(dev, "%s, fpc wake_unlock timeout\n", __func__);
                }
        } else {
                printk("invalid content: '%s', length = %zd\n", buffer, count);
                return -EINVAL;
        }

        return count;
}

static ssize_t hardware_reset(struct device *dev, struct device_attribute *attribute, const char *buffer, size_t count)
{
        if (g_use_gpio_power_enable == 1) {
                struct  fpc1020_data *fpc1020 = dev_get_drvdata(dev);
                printk("fpc_interrupt: %s enter\n", __func__);
                gpio_direction_output(fpc1020->vdd_en_gpio, 0);
                gpio_set_value(fpc1020->rst_gpio, 0);
                udelay(FPC1020_RESET_LOW_US);
                gpio_set_value(fpc1020->rst_gpio, 1);
                gpio_direction_output(fpc1020->vdd_en_gpio, 1);
                printk("fpc_interrupt: %s exit\n", __func__);
        }

        return count;
}

static DEVICE_ATTR(irq_unexpected, S_IWUSR, NULL, hardware_reset);
static DEVICE_ATTR(irq, S_IRUSR | S_IWUSR, irq_get, irq_ack);
static DEVICE_ATTR(regulator_enable, S_IWUSR, NULL, regulator_enable_set);
static DEVICE_ATTR(irq_enable, S_IRUSR | S_IWUSR, irq_enable_get, irq_enable_set);

static DEVICE_ATTR(wakelock_enable, S_IWUSR, NULL, wakelock_enable_set);

static struct attribute *attributes[] = {
        /*
           &dev_attr_hw_reset.attr,
           */
        &dev_attr_irq.attr,
        &dev_attr_regulator_enable.attr,
        &dev_attr_irq_enable.attr,
        &dev_attr_wakelock_enable.attr,
        &dev_attr_clk_enable.attr,
        &dev_attr_irq_unexpected.attr,
        NULL
};

static const struct attribute_group attribute_group = {
        .attrs = attributes,
};

static irqreturn_t fpc1020_irq_handler(int irq, void *handle)
{
        struct fpc1020_data *fpc1020 = handle;
        dev_info(fpc1020->dev, "%s\n", __func__);

        /* Make sure 'wakeup_enabled' is updated before using it
         ** since this is interrupt context (other thread...) */
        smp_rmb();
        /*
           if (fpc1020->wakeup_enabled ) {
           wake_lock_timeout(&fpc1020->ttw_wl, msecs_to_jiffies(FPC_TTW_HOLD_TIME));
           }
           */
        __pm_wakeup_event(&fpc1020->fpc_irq_wl, FPC_IRQ_WAKELOCK_TIMEOUT);

        sysfs_notify(&fpc1020->dev->kobj, NULL, dev_attr_irq.attr.name);

        return IRQ_HANDLED;
}


void fpc_cleanup_pwr_list(struct fpc1020_data* fpc_dev) {
    unsigned index = 0;
    pr_err("%s cleanup", __func__);
    for (index = 0; index < fpc_dev->power_num; index++) {
        if (fpc_dev->pwr_list[index].pwr_type == FP_POWER_MODE_GPIO) {
            gpio_free(fpc_dev->rst_gpio);
        }
        memset(&(fpc_dev->pwr_list[index]), 0, sizeof(fp_power_info_t));
    }
}

int fpc_parse_pwr_list(struct fpc1020_data* fpc_dev) {
    int ret = 0;
    struct device *dev = fpc_dev->dev;
    struct device_node *np = dev->of_node;
    struct device_node *child = NULL;
    unsigned child_node_index = 0;
    int ldo_param_amount = 0;
    const char *node_name = NULL;
    fp_power_info_t *pwr_list = fpc_dev->pwr_list;
    /* pwr list init */
    fpc_cleanup_pwr_list(fpc_dev);

    /* parse each node */
    for_each_available_child_of_node(np, child) {
        if (child_node_index >= FP_MAX_PWR_LIST_LEN) {
            pr_err("too many nodes");
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }

        /* get type of this power */
        ret = of_property_read_u32(child, FP_POWER_NODE, &(pwr_list[child_node_index].pwr_type));
        if (ret) {
            pr_err("failed to request %s, ret = %d\n", FP_POWER_NODE, ret);
            goto exit;
        }

        pr_info("read power type of index %d, type : %u\n", child_node_index, pwr_list[child_node_index].pwr_type);

        switch(pwr_list[child_node_index].pwr_type) {
        case FP_POWER_MODE_LDO:
            /* read ldo supply name */
            ret = of_property_read_string(child, FP_POWER_NAME_NODE, &(pwr_list[child_node_index].vreg_config.name));
            if (ret) {
                pr_err("the param %s is not found !\n", FP_POWER_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            pr_debug("get ldo node name %s", pwr_list[child_node_index].vreg_config.name);

            /* read ldo config name */
            ret = of_property_read_string(child, FP_POWER_CONFIG, &node_name);
            if (ret) {
                pr_err("the param %s is not found !\n", FP_POWER_CONFIG);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            pr_debug("get config node name %s", node_name);

            ldo_param_amount = of_property_count_elems_of_size(np, node_name, sizeof(u32));
            pr_debug("get ldo_param_amount %d", ldo_param_amount);
            if(ldo_param_amount != LDO_PARAM_AMOUNT) {
                pr_err("failed to request size %s\n", node_name);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_VMAX_INDEX, &(pwr_list[child_node_index].vreg_config.vmax));
            if (ret) {
                pr_err("failed to request %s(%d), rc = %u\n", node_name, LDO_VMAX_INDEX, ret);
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_VMIN_INDEX, &(pwr_list[child_node_index].vreg_config.vmin));
            if (ret) {
                pr_err("failed to request %s(%d), rc = %u\n", node_name, LDO_VMIN_INDEX, ret);
                goto exit;
            }

            ret = of_property_read_u32_index(np, node_name, LDO_UA_INDEX, &(pwr_list[child_node_index].vreg_config.ua_load));
            if (ret) {
                pr_err("failed to request %s(%d), rc = %u\n", node_name, LDO_UA_INDEX, ret);
                goto exit;
            }

            pr_info("%s size = %d, ua=%d, vmax=%d, vmin=%d\n", node_name, ldo_param_amount,
                    pwr_list[child_node_index].vreg_config.ua_load,
                    pwr_list[child_node_index].vreg_config.vmax,
                    pwr_list[child_node_index].vreg_config.vmax);
            break;
        case FP_POWER_MODE_GPIO:
            /* read GPIO name */
            ret = of_property_read_string(child, FP_POWER_NAME_NODE, &node_name);
            if (ret) {
                pr_err("the param %s is not found !\n", FP_POWER_NAME_NODE);
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }
            pr_info("get config node name %s", node_name);

            /* get gpio by name */
            pwr_list[child_node_index].pwr_gpio = of_get_named_gpio(np, node_name, 0);
            pr_debug("end of_get_named_gpio %s, pwr_gpio: %d!\n", node_name, pwr_list[child_node_index].pwr_gpio);
            if (pwr_list[child_node_index].pwr_gpio < 0) {
                pr_err("falied to get fpc_pwr gpio!\n");
                ret = -FP_ERROR_GENERAL;
                goto exit;
            }

            /* get poweron-level of gpio */
            pr_info("get poweron level: %s", FP_POWERON_LEVEL_NODE);
            ret = of_property_read_u32(child, FP_POWERON_LEVEL_NODE, &pwr_list[child_node_index].poweron_level);
            if (ret) {
                /* property of poweron-level is not config, by default set to 1 */
                pwr_list[child_node_index].poweron_level = 1;
            } else {
                if (pwr_list[child_node_index].poweron_level != 0) {
                    pwr_list[child_node_index].poweron_level = 1;
                }
            }
            pr_info("gpio poweron level: %d\n", pwr_list[child_node_index].poweron_level);

            ret = devm_gpio_request(dev, pwr_list[child_node_index].pwr_gpio, node_name);
            if (ret) {
                pr_err("failed to request %s gpio, ret = %d\n", node_name, ret);
                goto exit;
            }
            gpio_direction_output(pwr_list[child_node_index].pwr_gpio, (pwr_list[child_node_index].poweron_level == 0 ? 1: 0));
            pr_err("set fpc_pwr %u output %d \n", child_node_index, pwr_list[child_node_index].poweron_level);
            break;

        case FP_POWER_MODE_AUTO:
            pr_info("%s power mode auto \n", __func__);
            break;
        default:
            pr_err("unknown type %u\n", pwr_list[child_node_index].pwr_type);
            ret = -FP_ERROR_GENERAL;
            goto exit;
        }

        /* get delay time of this power */
        ret = of_property_read_u32(child, FP_POWER_DELAY_TIME, &pwr_list[child_node_index].delay);
        if (ret) {
            pr_err("failed to request %s, ret = %d\n", FP_POWER_NODE, ret);
            goto exit;
        }
        child_node_index++;
    }
    fpc_dev->power_num = child_node_index;
exit:
    if (ret) {
        fpc_cleanup_pwr_list(fpc_dev);
    }
    return ret;
}


static int fpc1020_irq_probe(struct platform_device *pldev)
{
        struct device *dev = &pldev->dev;
        int rc = 0;
        int irqf;
        struct device_node *np = dev->of_node;

        struct fpc1020_data *fpc1020 = devm_kzalloc(dev, sizeof(*fpc1020),
                        GFP_KERNEL);
        if (!fpc1020) {
                dev_err(dev, "failed to allocate memory for struct fpc1020_data\n");
                rc = -ENOMEM;
                goto ERR_ALLOC;
        }
        g_use_gpio_power_enable = 0;
        fpc1020->cs_gpio_set = false;
        fpc1020->pinctrl = NULL;
        fpc1020->pstate_cs_func = NULL;
        fpc1020->dev = dev;
        dev_info(fpc1020->dev, "-->%s\n", __func__);
        dev_set_drvdata(dev, fpc1020);
        fpc1020->pldev = pldev;

        if (!np) {
                dev_err(dev, "no of node found\n");
                rc = -EINVAL;
                goto ERR_BEFORE_WAKELOCK;
        }

        if ((FP_FPC_1140 != get_fpsensor_type())
                        &&(FP_FPC_1260 != get_fpsensor_type())
                        &&(FP_FPC_1022 != get_fpsensor_type())
                        &&(FP_FPC_1023 != get_fpsensor_type())
                        &&(FP_FPC_1023_GLASS != get_fpsensor_type())
                        &&(FP_FPC_1270 != get_fpsensor_type())
                        &&(FP_FPC_1511 != get_fpsensor_type())
                        &&(FP_FPC_1542 != get_fpsensor_type())
                        &&(FP_FPC_1541 != get_fpsensor_type())) {
                dev_err(dev, "found not fpc sensor\n");
                rc = -EINVAL;
                goto ERR_BEFORE_WAKELOCK;
        }
        dev_info(dev, "found fpc sensor\n");

        rc = of_property_read_u32(np, "fpc,use_gpio_power_enable", &g_use_gpio_power_enable);
        if (rc) {
            dev_err(dev, "failed to request fpc,use_gpio_power_enable, ret = %d\n", rc);
            g_use_gpio_power_enable = 0;
        }

        if (g_use_gpio_power_enable == 1) {
            fpc1020->pinctrl = devm_pinctrl_get(&pldev->dev);
            if (IS_ERR(fpc1020->pinctrl)) {
                dev_err(&pldev->dev, "can not get the fpc pinctrl");
                return PTR_ERR(fpc1020->pinctrl);
            }
            fpc1020->pstate_cs_func = pinctrl_lookup_state(fpc1020->pinctrl, "fpc_cs_func");
            if (IS_ERR(fpc1020->pstate_cs_func)) {
                dev_err(&pldev->dev, "Can't find fpc_cs_func pinctrl state\n");
                return PTR_ERR(fpc1020->pstate_cs_func);
            }
        }

        fpc_wakeup_source_init(&fpc1020->ttw_wl, "fpc_ttw_wl");
        fpc_wakeup_source_init(&fpc1020->fpc_wl, "fpc_wl");
        fpc_wakeup_source_init(&fpc1020->fpc_irq_wl, "fpc_irq_wl");

        rc = fpc1020_request_named_gpio(fpc1020, "fpc,irq-gpio",
                        &fpc1020->irq_gpio);
        if (rc) {
                goto ERR_AFTER_WAKELOCK;
        }
        rc = gpio_direction_input(fpc1020->irq_gpio);

        if (rc < 0) {
                dev_err(&fpc1020->pldev->dev,
                "gpio_direction_input failed for INT.\n");
                goto ERR_AFTER_WAKELOCK;
        }

        rc = fpc1020_request_named_gpio(fpc1020, "fpc,reset-gpio",
                &fpc1020->rst_gpio);
        if (rc) {
                goto ERR_AFTER_WAKELOCK;
        }
        /*dev_info(fpc1020->dev, "fpc1020 requested gpio finished \n");*/

        irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
        mutex_init(&fpc1020->lock);
        rc = devm_request_threaded_irq(dev, gpio_to_irq(fpc1020->irq_gpio),
                        NULL, fpc1020_irq_handler, irqf,
                        dev_name(dev), fpc1020);
        if (rc) {
                dev_err(dev, "could not request irq %d\n",
                                gpio_to_irq(fpc1020->irq_gpio));
                goto ERR_AFTER_WAKELOCK;
        }
        /*dev_info(fpc1020->dev, "requested irq %d\n", gpio_to_irq(fpc1020->irq_gpio));*/

        /* Request that the interrupt should be wakeable */
        /*enable_irq_wake( gpio_to_irq( fpc1020->irq_gpio ) );*/

        disable_irq_nosync(gpio_to_irq(fpc1020->irq_gpio));
        fpc1020->irq_enabled = 0;

        rc = sysfs_create_group(&dev->kobj, &attribute_group);
        if (rc) {
                dev_err(dev, "could not create sysfs\n");
                goto ERR_AFTER_WAKELOCK;
        }

        rc = fpc_parse_pwr_list(fpc1020);
        if (rc) {
            pr_err("failed to parse power list, rc = %d\n", rc);
            goto ERR_AFTER_WAKELOCK;
        }
        //if (g_use_gpio_power_enable != 1) {
        fpc_power_on(fpc1020);
        //}
        mdelay(1);
        if (g_use_gpio_power_enable == 1) {
        /*get cs resource*/
                fpc1020->cs_gpio = of_get_named_gpio(pldev->dev.of_node, "fpc,gpio_cs", 0);
                if (!gpio_is_valid(fpc1020->cs_gpio)) {
                        dev_err(fpc1020->dev, "CS GPIO is invalid.\n");
                        return -1;
                }
                rc = gpio_request(fpc1020->cs_gpio, "fpc,gpio_cs");
                if (rc) {
                        dev_err(fpc1020->dev, "Failed to request CS GPIO. rc = %d\n", rc);
                        return -1;
                }
                gpio_direction_output(fpc1020->cs_gpio, 0);
                fpc1020->cs_gpio_set = true;

                if (fpc1020->cs_gpio_set) {
                        dev_info(fpc1020->dev, "---- pull CS up and set CS from gpio to func ----");
                        gpio_set_value(fpc1020->cs_gpio, 1);
                        pinctrl_select_state(fpc1020->pinctrl, fpc1020->pstate_cs_func);
                        fpc1020->cs_gpio_set = false;
                }
        }
        rc = gpio_direction_output(fpc1020->rst_gpio, 1);

        if (rc) {
                dev_err(fpc1020->dev,
                        "gpio_direction_output (reset) failed.\n");
                goto ERR_AFTER_WAKELOCK;
        }


        mdelay(2);
        gpio_set_value(fpc1020->rst_gpio, 1);
        udelay(FPC1020_RESET_HIGH2_US);

        dev_info(fpc1020->dev, "%s: ok\n", __func__);
        return 0;

ERR_AFTER_WAKELOCK:
        fpc_wakeup_source_trash(&fpc1020->ttw_wl);
        fpc_wakeup_source_trash(&fpc1020->fpc_wl);
        fpc_wakeup_source_trash(&fpc1020->fpc_irq_wl);
ERR_BEFORE_WAKELOCK:
        dev_err(fpc1020->dev, "%s failed rc = %d\n", __func__, rc);
        devm_kfree(fpc1020->dev, fpc1020);
ERR_ALLOC:
        return rc;
}


static int fpc1020_irq_remove(struct platform_device *pldev)
{
        struct  fpc1020_data *fpc1020 = dev_get_drvdata(&pldev->dev);
        sysfs_remove_group(&pldev->dev.kobj, &attribute_group);
        mutex_destroy(&fpc1020->lock);

        fpc_wakeup_source_trash(&fpc1020->ttw_wl);

        dev_info(fpc1020->dev, "%s: removed\n", __func__);
        return 0;
}

/* -------------------------------------------------------------------- */
static int fpc1020_spi_probe(struct spi_device *spi)
{
        //struct device *dev = &spi->dev;
        int error = 0;
        fpc1020_data_t *fpc1020 = NULL;
        /* size_t buffer_size; */

        fpc1020 = kzalloc(sizeof(*fpc1020), GFP_KERNEL);
        if (!fpc1020) {
                pr_err("failed to allocate memory for struct fpc1020_data\n");
                return -ENOMEM;
        }

        spi_set_drvdata(spi, fpc1020);
        fpc1020->spi = spi;

        fpc_ms=spi_master_get_devdata(spi->master);
        //pr_err("%s end\n",__func__);
        globle_spi_clk = fpc_ms->spi_clk;
        return error;
}

/* -------------------------------------------------------------------- */
static int fpc1020_spi_remove(struct spi_device *spi)
{
        return 0;
}


static struct of_device_id fpc1020_of_match[] = {
        { .compatible = "fpc,fpc_irq", },
        {}
};

MODULE_DEVICE_TABLE(of, fpc1020_of_match);

static struct of_device_id fpc1020_spi_of_match[] = {
        { .compatible = "fpc,fpc1020", },
        { .compatible = "oplus,oplus_fp" },
        {}
};

MODULE_DEVICE_TABLE(of, fpc1020_spi_of_match);

static struct platform_driver fpc1020_irq_driver = {
        .driver = {
                .name                   = "fpc_irq",
                .owner                  = THIS_MODULE,
                .of_match_table = fpc1020_of_match,
         },
        .probe  = fpc1020_irq_probe,
        .remove = fpc1020_irq_remove
};

static struct spi_driver fpc1020_spi_driver = {
        .driver = {
                .name   = "fpc1020",
                .owner  = THIS_MODULE,
                .bus    = &spi_bus_type,
#ifdef CONFIG_OF
                .of_match_table = fpc1020_spi_of_match,
#endif
        },
        .probe  = fpc1020_spi_probe,
        .remove = fpc1020_spi_remove
};


static int __init fpc1020_init(void)
{
        //int rc = 0;
        if ((FP_FPC_1140 != get_fpsensor_type())
                        &&(FP_FPC_1260 != get_fpsensor_type())
                        &&(FP_FPC_1022 != get_fpsensor_type())
                        &&(FP_FPC_1023 != get_fpsensor_type())
                        &&(FP_FPC_1023_GLASS != get_fpsensor_type())
                        &&(FP_FPC_1270 != get_fpsensor_type())
                        &&(FP_FPC_1511 != get_fpsensor_type())
                        &&(FP_FPC_1542 != get_fpsensor_type())
			&&(FP_FPC_1541 != get_fpsensor_type())) {
                pr_err("%s, found not fpc sensor: %d\n", __func__, get_fpsensor_type());
                return -EINVAL;
        }
        pr_err("%s, found fpc sensor: %d\n", __func__, get_fpsensor_type());

        if (spi_register_driver(&fpc1020_spi_driver))
        {
                pr_err("register spi driver fail");
                return -EINVAL;
        }
        if(platform_driver_register(&fpc1020_irq_driver) ){
                pr_err("platform_driver_register fail");
                return -EINVAL;
        }
        return 0;
}

static void __exit fpc1020_exit(void)
{
	pr_err("[DEBUG]fpc1020_exit++++++++++++++++++++++\n");
        platform_driver_unregister(&fpc1020_irq_driver);
        spi_unregister_driver(&fpc1020_spi_driver);
        //platform_device_unregister(fpc_irq_platform_device);
}

late_initcall(fpc1020_init);

module_exit(fpc1020_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aleksej Makarov");
MODULE_AUTHOR("Henrik Tillman <henrik.tillman@fingerprints.com>");
MODULE_DESCRIPTION("FPC1020 Fingerprint sensor device driver.");
