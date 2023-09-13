/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_BATTERY_MTK6769_H__
#define __OPLUS_BATTERY_MTK6769_H__

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/alarmtimer.h>
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_charger.h>
#include <mt-plat/mtk_battery.h>
#include <uapi/linux/sched/types.h>
#include <linux/uaccess.h>

#include <mt-plat/charger_class.h>
#include "../../../../kernel-4.14/drivers/misc/mediatek/typec/tcpc/inc/tcpm.h"
#include "../../../../kernel-4.14/drivers/misc/mediatek/typec/tcpc/inc/mtk_direct_charge_vdm.h"
struct charger_manager;
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_pe_intf.h"
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_pe20_intf.h"
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_pdc_intf.h"

#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_charger_init.h"
#include "../../../../kernel-4.14/drivers/power/supply/mediatek/charger/mtk_charger_intf.h"

#define RT9471D 0
#define RT9467 1
#define BQ2589X 2
#define BQ2591X 3

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

#endif /* __OPLUS_BATTERY_MTK6769_H__ */
