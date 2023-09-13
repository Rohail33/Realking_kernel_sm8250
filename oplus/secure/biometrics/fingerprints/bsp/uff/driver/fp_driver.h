/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __FP_DRIVER_H
#define __FP_DRIVER_H

#include <linux/input.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include "include/oplus_fp_common.h"
#include "include/fingerprint_event.h"
#include "include/fp_health.h"

// one step: define by compile
/**************************************************/
#if defined(QCOM_PLATFORM)
#define USE_PLATFORM_BUS 1
#define ONSCREENFINGERPRINT_EVENT 0x10
#elif defined(MTK_PLATFORM)
#define USE_SPI_BUS 1
#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#endif
#include <linux/version.h>
#define SPI_CLK_CONTROL 1
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))&&(LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0))
#define ONSCREENFINGERPRINT_EVENT 0x20
#else
#define ONSCREENFINGERPRINT_EVENT 20
#endif
#else
#endif
/**************************************************/

// second step
/**************************************************/
#if defined(USE_PLATFORM_BUS)
typedef struct platform_device oplus_fp_device;
typedef struct platform_driver oplus_driver;
#define oplus_driver_register(pdriver) platform_driver_register(pdriver);  // inbit
#define oplus_driver_unregister(pdriver) platform_driver_unregister(pdriver);  // uninit

#elif defined(USE_SPI_BUS)
typedef struct spi_device oplus_fp_device;
typedef struct spi_driver oplus_driver;
#define oplus_driver_register(pdriver) spi_register_driver(pdriver);  // inbit
#define oplus_driver_unregister(pdriver) spi_unregister_driver(pdriver);  // uninit
#endif

#if defined(SPI_CLK_CONTROL)
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);
#define fp_spi_enable_master_clk(pdriver) mt_spi_enable_master_clk(pdriver);
#define fp_spi_disable_master_clk(pdriver) mt_spi_disable_master_clk(pdriver);
#else
#define fp_spi_enable_master_clk(pdriver) (void)(pdriver);
#define fp_spi_disable_master_clk(pdriver) (void)(pdriver);
#endif

#if defined(CONFIG_DRM_MSM)
#include <linux/msm_drm_notify.h>
#define oplus_register_notifier_client(pnotifier) msm_drm_register_client(pnotifier);  // inbit
#elif defined(CONFIG_FB)
#define oplus_register_notifier_client(pnotifier) fb_register_client(pnotifier);  // inbit
#elif (IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER))
#include <drm/drm_panel.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#endif

#if defined(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
#if defined(CONFIG_DRM_MEDIATEK_V2)
#define oplus_register_notifier_client(source, pnotifier) mtk_disp_notifier_register(source, pnotifier);
#else
#define oplus_register_notifier_client(pnotifier) fb_register_client(pnotifier);
#endif
#endif

/************************************************/

// nav function
/************************************************/
// for driver
struct fp_key_map {
    unsigned int type;
    unsigned int code;
};

//#define FP_KEY_INPUT_HOME    KEY_HOME
//#define FP_KEY_INPUT_POWER    KEY_POWER
//#define FP_KEY_INPUT_CAMERA    KEY_CAMERA

#define FP_KEY_INPUT_HOME 0x11
#define FP_KEY_INPUT_POWER 0x12
#define FP_KEY_INPUT_CAMERA 0x13

// for hal msg
typedef enum fp_key_event {
    FP_KEY_NONE = 0,
    FP_KEY_HOME,
    FP_KEY_POWER,
    FP_KEY_MENU,  // need del
    FP_KEY_BACK,  // need del
    FP_KEY_CAMERA,
} fp_key_event_t;

struct fp_key {
    enum fp_key_event key;
    uint32_t          value; /* key down = 1, key up = 0 */
};
/************************************************/

// ioctl function
/************************************************/
#define FP_IOC_MAGIC 'O'  // define magic number   // need modify to 'o'
#define FP_IOC_INIT _IOR(FP_IOC_MAGIC, 0, uint8_t)
#define FP_IOC_EXIT _IO(FP_IOC_MAGIC, 1)
#define FP_IOC_RESET _IO(FP_IOC_MAGIC, 2)
#define FP_IOC_ENABLE_IRQ _IO(FP_IOC_MAGIC, 3)
#define FP_IOC_DISABLE_IRQ _IO(FP_IOC_MAGIC, 4)
#define FP_IOC_ENABLE_SPI_CLK _IOW(FP_IOC_MAGIC, 5, uint32_t)
#define FP_IOC_DISABLE_SPI_CLK _IO(FP_IOC_MAGIC, 6)
#define FP_IOC_ENABLE_POWER _IO(FP_IOC_MAGIC, 7)
#define FP_IOC_DISABLE_POWER _IO(FP_IOC_MAGIC, 8)
#define FP_IOC_INPUT_KEY_EVENT _IOW(FP_IOC_MAGIC, 9, struct fp_key)
#define FP_IOC_ENTER_SLEEP_MODE _IO(FP_IOC_MAGIC, 10)
#define FP_IOC_GET_FW_INFO _IOR(FP_IOC_MAGIC, 11, uint8_t)
#define FP_IOC_REMOVE _IO(FP_IOC_MAGIC, 12)
#define FP_IOC_RD_IRQ_VALUE _IOW(FP_IOC_MAGIC, 13, int32_t)

//#define FP_IOC_CHIP_INFO    _IOW(FP_IOC_MAGIC, 13, struct fp_ioc_chip_info) //no used
//#define FP_IOC_NAV_EVENT _IOW(FP_IOC_MAGIC, 14, fp_nav_event_t)  //no used
#define FP_IOC_POWER_RESET _IO(FP_IOC_MAGIC, 17)
#define FP_IOC_WAKELOCK_TIMEOUT_ENABLE _IO(FP_IOC_MAGIC, 18)
#define FP_IOC_WAKELOCK_TIMEOUT_DISABLE _IO(FP_IOC_MAGIC, 19)
#define FP_IOC_CLEAN_TOUCH_FLAG _IO(FP_IOC_MAGIC, 20)
// #define FP_IOC_MAXNR 21 /* THIS MACRO IS NOT USED NOW... */
/************************************************/

#define FP_IOC_AUTO_SEND_TOUCHDOWN        _IO(FP_IOC_MAGIC, 21)
#define FP_IOC_AUTO_SEND_TOUCHUP        _IO(FP_IOC_MAGIC, 22)
#define FP_IOC_STOP_WAIT_INTERRUPT_EVENT _IO(FP_IOC_MAGIC, 23)

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_OLC)
#define FP_IOC_REPORT_OLC_EVENT _IOW(FP_IOC_MAGIC, 24, struct fp_exception_info)
#endif

// netlink function
/************************************************/
#define FP_NETLINK_ENABLE 1
#define FP_NET_EVENT_FB_BLACK 2
#define FP_NET_EVENT_FB_UNBLACK 3
#define NETLINKROUTE 25



/************************************************/

/************************************************/
struct fp_dev {
    dev_t            devt;
    struct list_head device_entry;
    oplus_fp_device *pdev;
    struct clk *     core_clk;
    struct clk *     iface_clk;

    struct input_dev *input;
    /* buffer is NULL unless this device is open (users > 0) */
    unsigned              users;
    signed                irq_gpio;
    signed                reset_gpio;
    signed                pwr_gpio;
    int                   irq;
    int                   irq_enabled;
    int                   clk_enabled;
    struct notifier_block notifier;
#if defined(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE)
    struct notifier_block tp_notifier;
#endif
#if (IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER))
    struct drm_panel     *active_panel;
    void                 *notifier_cookie;
    bool                 is_panel_registered;
#endif
    signed                device_available;
    signed                fb_black;

    unsigned        power_num;
    fp_power_info_t pwr_list[FP_MAX_PWR_LIST_LEN];
    uint32_t        notify_tpinfo_flag;
    uint32_t        ftm_poweroff_flag;

#if defined(MTK_PLATFORM)
    struct pinctrl *pinctrl;
    struct pinctrl_state *pstate_default;
    struct pinctrl_state *pstate_spi;
#endif
    unsigned int   ldo_voltage;
    unsigned int   ldo_num;
};

// dts function
int  fp_parse_dts(struct fp_dev *fp_dev);
void fp_cleanup_device(struct fp_dev *fp_dev);

// power function
int fp_power_on(struct fp_dev *fp_dev);
int fp_power_off(struct fp_dev *fp_dev);
int fp_power_reset(struct fp_dev *fp_dev);

// hardware control
int fp_hw_reset(struct fp_dev *fp_dev, unsigned int delay_ms);
int fp_irq_num(struct fp_dev *fp_dev);

// netlink funciton
int  fp_netlink_init(void);
void fp_netlink_exit(void);

// feature
void fp_cleanup_pwr_list(struct fp_dev *fp_dev);
int  fp_parse_pwr_list(struct fp_dev *fp_dev);
int  fp_parse_ftm_poweroff_flag(struct fp_dev *fp_dev);
/************************************************/

#endif /*__FP_DRIVER_H*/
