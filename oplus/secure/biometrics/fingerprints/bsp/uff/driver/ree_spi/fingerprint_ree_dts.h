#ifndef FINGERPRINT_REE_DTS_H
#define FINGERPRINT_REE_DTS_H

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include "fingerprint_ree_spi.h"

/* 0: power mode not set, 1: ldo power, 2: gpio power, 3: auto power */
#define FP_POWER_NODE                   "power-mode"
#define FP_POWER_NAME_NODE              "power-name"
#define FP_POWER_CONFIG                 "power-config"
#define LDO_PARAM_AMOUNT                3
#define LDO_POWER_NODE                  "ldo"
#define LDO_CONFIG_NODE                 "ldo-config"
#define FP_POWER_DELAY_TIME             "delay-time"
#define FP_POWERON_LEVEL_NODE           "poweron-level"
#define FP_NOTIFY_TPINFO_FLAG           "notify_tpinfo_flag"
#define FP_FTM_POWEROFF_FLAG            "ftm_poweroff_flag"
#define LDO_VMAX_INDEX                  (0)
#define LDO_VMIN_INDEX                  (1)
#define LDO_UA_INDEX                    (2)

void fp_cleanup_pwr_list(struct spidev_data *dev);
static int vreg_setup(struct spidev_data *goodix_fp, fp_power_info_t *pwr_info,
    bool enable);
int parse_kernel_pwr_list(struct spidev_data *dev);
int fp_parse_dts(struct spidev_data *dev);
void sensor_cleanup(struct spidev_data *dev);
int fp_power_on(struct spidev_data *dev);
int fp_power_off(struct spidev_data *dev);
int fp_hw_reset(struct spidev_data *dev, unsigned int delay_ms);
int fp_irq_num(struct spidev_data *dev);
#endif //FINGERPRINT_REE_DTS_H
