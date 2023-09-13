/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_BATTERY_MTK6768_H__
#define __OPLUS_BATTERY_MTK6768_H__

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/alarmtimer.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_charger.h>
#include <mt-plat/mtk_battery.h>
#include <mt-plat/charger_class.h>
#else
#include <mt-plat/v1/charger_type.h>
#include <mt-plat/v1/mtk_battery.h>
#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/mtk_charger.h>
#endif

#include <uapi/linux/sched/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include "../../../../kernel-4.14/drivers/misc/mediatek/typec/tcpc/inc/tcpm.h"
#include "../../../../kernel-4.14/drivers/misc/mediatek/typec/tcpc/inc/mtk_direct_charge_vdm.h"
struct charger_manager;
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_pe_intf.h"
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_pe20_intf.h"
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_pdc_intf.h"

#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_charger_init.h"
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_charger_intf.h"

#else
#include "../../../../kernel-4.19/drivers/misc/mediatek/typec/tcpc/inc/tcpm.h"
#include "../../../../kernel-4.19/drivers/misc/mediatek/typec/tcpc/inc/mtk_direct_charge_vdm.h"
struct charger_manager;
#include "../../../../kernel-4.19/drivers/power/supply/mediatek/charger/mtk_pe_intf.h"
#include "../../../../kernel-4.19/drivers/power/supply/mediatek/charger/mtk_pe20_intf.h"
#include "../../../../kernel-4.19/drivers/power/supply/mediatek/charger/mtk_pdc_intf.h"
#include "../../../../kernel-4.19/drivers/power/supply/mediatek/charger/mtk_charger_init.h"
#include "../../../../kernel-4.19/drivers/power/supply/mediatek/charger/mtk_charger_intf.h"
#endif

#define RT9471D 0
#define RT9467 1
#define BQ2589X 2
#define BQ2591X 3
#define SY6974 4
#define SGM41512 8

typedef enum {
        OPLUS_MINI_USB_TYPE = 0,
        OPLUS_MICRO_USB_TYPE,
        OPLUS_TYPE_C_USB_TYPE,
}OPLUS_CHG_USB_TYPE_STAT;

typedef enum {
        AP_TEMP_BELOW_T0 = 0,
        AP_TEMP_T0_TO_T1,
        AP_TEMP_T1_TO_T2,
        AP_TEMP_T2_TO_T3,
        AP_TEMP_ABOVE_T3
}OPLUS_CHG_AP_TEMP_STAT;

typedef enum {
        BATT_TEMP_EXTEND_BELOW_T0 = 0,
        BATT_TEMP_EXTEND_T0_TO_T1,
        BATT_TEMP_EXTEND_T1_TO_T2,
        BATT_TEMP_EXTEND_T2_TO_T3,
        BATT_TEMP_EXTEND_T3_TO_T4,
        BATT_TEMP_EXTEND_T4_TO_T5,
        BATT_TEMP_EXTEND_T5_TO_T6,
        BATT_TEMP_EXTEND_T6_TO_T7,
        BATT_TEMP_EXTEND_ABOVE_T7
}OPLUS_CHG_BATT_TEMP_EXTEND_STAT;

struct master_chg_psy {
	struct device *dev;
	struct power_supply_desc mastercharger_psy_desc;
	struct power_supply_config mastercharger_psy_cfg;
	struct power_supply *mastercharger_psy;
};
#endif /* __OPLUS_BATTERY_MTK6768_H__ */
