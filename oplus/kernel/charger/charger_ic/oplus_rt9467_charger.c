/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>

#include <mt-plat/upmu_common.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_class.h>
#include <mt-plat/charger_type.h>
#else
#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/charger_type.h>
#endif

#include <mt-plat/mtk_boot.h>
#if 0
//#ifdef CONFIG_RT_REGMAP
#include <mt-plat/rt-regmap.h>
#endif

#include "../../mediatek/charger/mtk_charger_intf.h"
#include "oplus_rt9467_reg.h"

#include <linux/time.h>

#define I2C_ACCESS_MAX_RETRY	5
#define RT9467_DRV_VERSION	"1.0.18_MTK"

#define TOTAL_CHG_PERCENT 100
#define PRIMARY_CHG_PERCENT 70
#define SECONDARY_CHG_PERCENT (TOTAL_CHG_PERCENT-PRIMARY_CHG_PERCENT)

struct rt9467_info *rt9467 = NULL;
static enum power_supply_type charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

#ifdef OPLUS_FEATURE_CHG_BASIC
extern struct oplus_chg_chip *g_oplus_chip;
void oplus_wake_up_usbtemp_thread(void);
#endif
extern void set_charger_ic(int sel);

/* ======================= */
/* RT9467 Parameter        */
/* ======================= */

static const u32 rt9467_boost_oc_threshold[] = {
	500000, 700000, 1100000, 1300000, 1800000, 2100000, 2400000, 3000000,
}; /* uA */

static const u32 rt9467_safety_timer[] = {
	4, 6, 8, 10, 12, 14, 16, 20,
}; /* hour */

enum rt9467_irq_idx {
	RT9467_IRQIDX_CHG_STATC = 0,
	RT9467_IRQIDX_CHG_FAULT,
	RT9467_IRQIDX_TS_STATC,
	RT9467_IRQIDX_CHG_IRQ1,
	RT9467_IRQIDX_CHG_IRQ2,
	RT9467_IRQIDX_CHG_IRQ3,
	RT9467_IRQIDX_DPDM_IRQ,
	RT9467_IRQIDX_MAX,
};

enum rt9467_irq_stat {
	RT9467_IRQSTAT_CHG_STATC = 0,
	RT9467_IRQSTAT_CHG_FAULT,
	RT9467_IRQSTAT_TS_STATC,
	RT9467_IRQSTAT_MAX,
};

enum rt9467_chg_type {
	RT9467_CHG_TYPE_NOVBUS = 0,
	RT9467_CHG_TYPE_UNDER_GOING,
	RT9467_CHG_TYPE_SDP,
	RT9467_CHG_TYPE_SDPNSTD,
	RT9467_CHG_TYPE_DCP,
	RT9467_CHG_TYPE_CDP,
	RT9467_CHG_TYPE_MAX,
};

enum rt9467_usbsw_state {
	RT9467_USBSW_CHG = 0,
	RT9467_USBSW_USB,
};

static const u8 rt9467_irq_maskall[RT9467_IRQIDX_MAX] = {
	0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

struct irq_mapping_tbl {
	const char *name;
	const int id;
};

#define RT9467_IRQ_MAPPING(_name, _id) {.name = #_name, .id = _id}
static const struct irq_mapping_tbl rt9467_irq_mapping_tbl[] = {
	RT9467_IRQ_MAPPING(chg_treg, 4),
	RT9467_IRQ_MAPPING(chg_aicr, 5),
	RT9467_IRQ_MAPPING(chg_mivr, 6),
	RT9467_IRQ_MAPPING(pwr_rdy, 7),
	RT9467_IRQ_MAPPING(chg_vsysuv, 12),
	RT9467_IRQ_MAPPING(chg_vsysov, 13),
	RT9467_IRQ_MAPPING(chg_vbatov, 14),
	RT9467_IRQ_MAPPING(chg_vbusov, 15),
	RT9467_IRQ_MAPPING(ts_batcold, 20),
	RT9467_IRQ_MAPPING(ts_batcool, 21),
	RT9467_IRQ_MAPPING(ts_batwarm, 22),
	RT9467_IRQ_MAPPING(ts_bathot, 23),
	RT9467_IRQ_MAPPING(ts_statci, 24),
	RT9467_IRQ_MAPPING(chg_faulti, 25),
	RT9467_IRQ_MAPPING(chg_statci, 26),
	RT9467_IRQ_MAPPING(chg_tmri, 27),
	RT9467_IRQ_MAPPING(chg_batabsi, 28),
	RT9467_IRQ_MAPPING(chg_adpbadi, 29),
	RT9467_IRQ_MAPPING(chg_rvpi, 30),
	RT9467_IRQ_MAPPING(otpi, 31),
	RT9467_IRQ_MAPPING(chg_aiclmeasi, 32),
	RT9467_IRQ_MAPPING(chg_ichgmeasi, 33),
	RT9467_IRQ_MAPPING(chgdet_donei, 34),
	RT9467_IRQ_MAPPING(wdtmri, 35),
	RT9467_IRQ_MAPPING(ssfinishi, 36),
	RT9467_IRQ_MAPPING(chg_rechgi, 37),
	RT9467_IRQ_MAPPING(chg_termi, 38),
	RT9467_IRQ_MAPPING(chg_ieoci, 39),
	RT9467_IRQ_MAPPING(adc_donei, 40),
	RT9467_IRQ_MAPPING(pumpx_donei, 41),
	RT9467_IRQ_MAPPING(bst_batuvi, 45),
	RT9467_IRQ_MAPPING(bst_midovi, 46),
	RT9467_IRQ_MAPPING(bst_olpi, 47),
	RT9467_IRQ_MAPPING(attachi, 48),
	RT9467_IRQ_MAPPING(detachi, 49),
	RT9467_IRQ_MAPPING(hvdcpi, 52),
	RT9467_IRQ_MAPPING(chgdeti, 54),
	RT9467_IRQ_MAPPING(dcdti, 55),
};

enum rt9467_charging_status {
	RT9467_CHG_STATUS_READY = 0,
	RT9467_CHG_STATUS_PROGRESS,
	RT9467_CHG_STATUS_DONE,
	RT9467_CHG_STATUS_FAULT,
	RT9467_CHG_STATUS_MAX,
};

static const char *rt9467_chg_status_name[RT9467_CHG_STATUS_MAX] = {
	"ready", "progress", "done", "fault",
};

static const u8 rt9467_val_en_hidden_mode[] = {
	0x49, 0x32, 0xB6, 0x27, 0x48, 0x18, 0x03, 0xE2,
};

enum rt9467_iin_limit_sel {
	RT9467_IINLMTSEL_3_2A = 0,
	RT9467_IINLMTSEL_CHG_TYP,
	RT9467_IINLMTSEL_AICR,
	RT9467_IINLMTSEL_LOWER_LEVEL, /* lower of above three */
};

enum rt9467_adc_sel {
	RT9467_ADC_VBUS_DIV5 = 1,
	RT9467_ADC_VBUS_DIV2,
	RT9467_ADC_VSYS,
	RT9467_ADC_VBAT,
	RT9467_ADC_TS_BAT = 6,
	RT9467_ADC_IBUS = 8,
	RT9467_ADC_IBAT,
	RT9467_ADC_REGN = 11,
	RT9467_ADC_TEMP_JC,
	RT9467_ADC_MAX,
};

/*
 * Unit for each ADC parameter
 * 0 stands for reserved
 * For TS_BAT, the real unit is 0.25.
 * Here we use 25, please remember to divide 100 while showing the value
 */
static const int rt9467_adc_unit[RT9467_ADC_MAX] = {
	0,
	RT9467_ADC_UNIT_VBUS_DIV5,
	RT9467_ADC_UNIT_VBUS_DIV2,
	RT9467_ADC_UNIT_VSYS,
	RT9467_ADC_UNIT_VBAT,
	0,
	RT9467_ADC_UNIT_TS_BAT,
	0,
	RT9467_ADC_UNIT_IBUS,
	RT9467_ADC_UNIT_IBAT,
	0,
	RT9467_ADC_UNIT_REGN,
	RT9467_ADC_UNIT_TEMP_JC,
};

static const int rt9467_adc_offset[RT9467_ADC_MAX] = {
	0,
	RT9467_ADC_OFFSET_VBUS_DIV5,
	RT9467_ADC_OFFSET_VBUS_DIV2,
	RT9467_ADC_OFFSET_VSYS,
	RT9467_ADC_OFFSET_VBAT,
	0,
	RT9467_ADC_OFFSET_TS_BAT,
	0,
	RT9467_ADC_OFFSET_IBUS,
	RT9467_ADC_OFFSET_IBAT,
	0,
	RT9467_ADC_OFFSET_REGN,
	RT9467_ADC_OFFSET_TEMP_JC,
};

struct rt9467_desc {
	u32 ichg;	/* uA */
	u32 aicr;	/* uA */
	u32 mivr;	/* uV */
	u32 cv;		/* uV */
	u32 ieoc;	/* uA */
	u32 safety_timer;	/* hour */
	u32 ircmp_resistor;	/* uohm */
	u32 ircmp_vclamp;	/* uV */
	bool en_te;
	bool en_wdt;
	bool en_irq_pulse;
	bool en_jeita;
	bool en_chgdet;
	int regmap_represent_slave_addr;
	const char *regmap_name;
	const char *chg_dev_name;
	bool ceb_invert;
	int pre_current_ma;
};

/* These default values will be applied if there's no property in dts */
static struct rt9467_desc rt9467_default_desc = {
	.ichg = 2000000,	/* uA */
	.aicr = 500000,		/* uA */
	.mivr = 4400000,	/* uV */
	.cv = 4350000,		/* uA */
	.ieoc = 250000,		/* uA */
	.safety_timer = 12,
#ifdef CONFIG_MTK_BIF_SUPPORT
	.ircmp_resistor = 0,		/* uohm */
	.ircmp_vclamp = 0,		/* uV */
#else
	.ircmp_resistor = 25000,	/* uohm */
	.ircmp_vclamp = 32000,		/* uV */
#endif /* CONFIG_MTK_BIF_SUPPORT */
	.en_te = true,
	.en_wdt = true,
	.en_irq_pulse = false,
	.en_jeita = false,
	.en_chgdet = true,
	.regmap_represent_slave_addr = RT9467_SLAVE_ADDR,
	.regmap_name = "rt9467",
	.chg_dev_name = "primary_chg",
	.ceb_invert = false,
	.pre_current_ma = -1,
};

struct rt9467_info {
	struct i2c_client *client;
	struct mutex i2c_access_lock;
	struct mutex adc_access_lock;
	struct mutex irq_access_lock;
	struct mutex aicr_access_lock;
	struct mutex ichg_access_lock;
	struct mutex pe_access_lock;
	struct mutex hidden_mode_lock;
	struct mutex bc12_access_lock;
	struct mutex ieoc_lock;
	struct mutex tchg_lock;
	struct device *dev;
	struct charger_device *chg_dev;
	struct charger_properties chg_props;
	struct rt9467_desc *desc;
	struct switch_dev *usb_switch;
	struct power_supply *psy;
	struct charger_consumer *chg_consumer;
	wait_queue_head_t wait_queue;
	int irq;
	int aicr_limit;
	u32 intr_gpio;
	u32 ceb_gpio;
	u8 chip_rev;
	u8 irq_flag[RT9467_IRQIDX_MAX];
	u8 irq_stat[RT9467_IRQSTAT_MAX];
	u8 irq_mask[RT9467_IRQIDX_MAX];
	u32 hidden_mode_cnt;
	bool bc12_en;
	bool pwr_rdy;
	enum charger_type chg_type;
	u32 ieoc;
	u32 ichg;
	bool ieoc_wkard;
	struct work_struct init_work;
	atomic_t bc12_sdp_cnt;
	atomic_t bc12_wkard;
	int tchg;

#ifdef CONFIG_TCPC_CLASS
	atomic_t tcpc_usb_connected;
#else
	struct work_struct chgdet_work;
#endif /* CONFIG_TCPC_CLASS */

#if 0
//#ifdef CONFIG_RT_REGMAP
	struct rt_regmap_device *regmap_dev;
	struct rt_regmap_properties *regmap_prop;
#endif /* CONFIG_RT_REGMAP */
	atomic_t hvdcp_en_cnt;
    bool hvdcp_can_enabled;
	int otg_ocp_cnt;
	struct mutex otg_access_lock;
	bool disable_hight_vbus;
	bool pdqc_setup_5v;
	struct timespec ptime[2];
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static bool disable_PE = 0;
static bool disable_QC = 0;
static bool disable_PD = 0;
module_param(disable_PE, bool, 0644);
module_param(disable_QC, bool, 0644);
module_param(disable_PD, bool, 0644);
#endif

/* ======================= */
/* Register Address        */
/* ======================= */

static const unsigned char rt9467_reg_addr[] = {
	RT9467_REG_CORE_CTRL0,
	RT9467_REG_CHG_CTRL1,
	RT9467_REG_CHG_CTRL2,
	RT9467_REG_CHG_CTRL3,
	RT9467_REG_CHG_CTRL4,
	RT9467_REG_CHG_CTRL5,
	RT9467_REG_CHG_CTRL6,
	RT9467_REG_CHG_CTRL7,
	RT9467_REG_CHG_CTRL8,
	RT9467_REG_CHG_CTRL9,
	RT9467_REG_CHG_CTRL10,
	RT9467_REG_CHG_CTRL11,
	RT9467_REG_CHG_CTRL12,
	RT9467_REG_CHG_CTRL13,
	RT9467_REG_CHG_CTRL14,
	RT9467_REG_CHG_CTRL15,
	RT9467_REG_CHG_CTRL16,
	RT9467_REG_CHG_ADC,
	RT9467_REG_CHG_DPDM1,
	RT9467_REG_CHG_DPDM2,
	RT9467_REG_CHG_DPDM3,
	RT9467_REG_CHG_CTRL19,
	RT9467_REG_CHG_CTRL17,
	RT9467_REG_CHG_CTRL18,
	RT9467_REG_DEVICE_ID,
	RT9467_REG_CHG_STAT,
	RT9467_REG_CHG_NTC,
	RT9467_REG_ADC_DATA_H,
	RT9467_REG_ADC_DATA_L,
	RT9467_REG_ADC_DATA_TUNE_H,
	RT9467_REG_ADC_DATA_TUNE_L,
	RT9467_REG_ADC_DATA_ORG_H,
	RT9467_REG_ADC_DATA_ORG_L,
	RT9467_REG_CHG_STATC,
	RT9467_REG_CHG_FAULT,
	RT9467_REG_TS_STATC,
	/* Skip IRQ evt to prevent reading clear while dumping registers */
	RT9467_REG_CHG_STATC_CTRL,
	RT9467_REG_CHG_FAULT_CTRL,
	RT9467_REG_TS_STATC_CTRL,
	RT9467_REG_CHG_IRQ1_CTRL,
	RT9467_REG_CHG_IRQ2_CTRL,
	RT9467_REG_CHG_IRQ3_CTRL,
	RT9467_REG_DPDM_IRQ_CTRL,
};

/* ========= */
/* RT Regmap */
/* ========= */

#if 0
//#ifdef CONFIG_RT_REGMAP
RT_REG_DECL(RT9467_REG_CORE_CTRL0, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL1, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL2, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL3, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL4, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL5, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL6, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL7, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL8, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL9, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL10, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL11, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL12, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL13, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL14, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL15, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL16, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_ADC, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_DPDM1, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_DPDM2, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_DPDM3, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL19, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL17, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_CTRL18, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL1, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL2, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL4, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL6, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL7, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL8, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL9, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_HIDDEN_CTRL15, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_DEVICE_ID, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_STAT, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_NTC, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_ADC_DATA_H, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_ADC_DATA_L, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_ADC_DATA_TUNE_H, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_ADC_DATA_TUNE_L, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_ADC_DATA_ORG_H, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_ADC_DATA_ORG_L, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_STATC, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_FAULT, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_TS_STATC, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_IRQ1, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_IRQ2, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_IRQ3, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_DPDM_IRQ, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_STATC_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_FAULT_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_TS_STATC_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_IRQ1_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_IRQ2_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_CHG_IRQ3_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(RT9467_REG_DPDM_IRQ_CTRL, 1, RT_VOLATILE, {});

static const rt_register_map_t rt9467_regmap_map[] = {
	RT_REG(RT9467_REG_CORE_CTRL0),
	RT_REG(RT9467_REG_CHG_CTRL1),
	RT_REG(RT9467_REG_CHG_CTRL2),
	RT_REG(RT9467_REG_CHG_CTRL3),
	RT_REG(RT9467_REG_CHG_CTRL4),
	RT_REG(RT9467_REG_CHG_CTRL5),
	RT_REG(RT9467_REG_CHG_CTRL6),
	RT_REG(RT9467_REG_CHG_CTRL7),
	RT_REG(RT9467_REG_CHG_CTRL8),
	RT_REG(RT9467_REG_CHG_CTRL9),
	RT_REG(RT9467_REG_CHG_CTRL10),
	RT_REG(RT9467_REG_CHG_CTRL11),
	RT_REG(RT9467_REG_CHG_CTRL12),
	RT_REG(RT9467_REG_CHG_CTRL13),
	RT_REG(RT9467_REG_CHG_CTRL14),
	RT_REG(RT9467_REG_CHG_CTRL15),
	RT_REG(RT9467_REG_CHG_CTRL16),
	RT_REG(RT9467_REG_CHG_ADC),
	RT_REG(RT9467_REG_CHG_DPDM1),
	RT_REG(RT9467_REG_CHG_DPDM2),
	RT_REG(RT9467_REG_CHG_DPDM3),
	RT_REG(RT9467_REG_CHG_CTRL19),
	RT_REG(RT9467_REG_CHG_CTRL17),
	RT_REG(RT9467_REG_CHG_CTRL18),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL1),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL2),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL4),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL6),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL7),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL8),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL9),
	RT_REG(RT9467_REG_CHG_HIDDEN_CTRL15),
	RT_REG(RT9467_REG_DEVICE_ID),
	RT_REG(RT9467_REG_CHG_STAT),
	RT_REG(RT9467_REG_CHG_NTC),
	RT_REG(RT9467_REG_ADC_DATA_H),
	RT_REG(RT9467_REG_ADC_DATA_L),
	RT_REG(RT9467_REG_ADC_DATA_TUNE_H),
	RT_REG(RT9467_REG_ADC_DATA_TUNE_L),
	RT_REG(RT9467_REG_ADC_DATA_ORG_H),
	RT_REG(RT9467_REG_ADC_DATA_ORG_L),
	RT_REG(RT9467_REG_CHG_STATC),
	RT_REG(RT9467_REG_CHG_FAULT),
	RT_REG(RT9467_REG_TS_STATC),
	RT_REG(RT9467_REG_CHG_IRQ1),
	RT_REG(RT9467_REG_CHG_IRQ2),
	RT_REG(RT9467_REG_CHG_IRQ3),
	RT_REG(RT9467_REG_DPDM_IRQ),
	RT_REG(RT9467_REG_CHG_STATC_CTRL),
	RT_REG(RT9467_REG_CHG_FAULT_CTRL),
	RT_REG(RT9467_REG_TS_STATC_CTRL),
	RT_REG(RT9467_REG_CHG_IRQ1_CTRL),
	RT_REG(RT9467_REG_CHG_IRQ2_CTRL),
	RT_REG(RT9467_REG_CHG_IRQ3_CTRL),
	RT_REG(RT9467_REG_DPDM_IRQ_CTRL),
};
#endif /* CONFIG_RT_REGMAP */

/* ========================= */
/* I2C operations            */
/* ========================= */

static int rt9467_device_read(void *client, u32 addr, int leng, void *dst)
{
	struct i2c_client *i2c = (struct i2c_client *)client;
	int rc = 0;
	int retry = 3;

	rc = i2c_smbus_read_i2c_block_data(i2c, addr, leng, dst);

	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_read_i2c_block_data(i2c, addr, leng, dst);
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	return rc;
}

static int rt9467_device_write(void *client, u32 addr, int leng,
	const void *src)
{
	struct i2c_client *i2c = (struct i2c_client *)client;
	int rc = 0;
	int retry = 3;

	rc = i2c_smbus_write_i2c_block_data(i2c, addr, leng, src);
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_write_i2c_block_data(i2c, addr, leng, src);
			if (rc < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	return rc;
}

#if 0
//#ifdef CONFIG_RT_REGMAP
static struct rt_regmap_fops rt9467_regmap_fops = {
	.read_device = rt9467_device_read,
	.write_device = rt9467_device_write,
};

static int rt9467_register_rt_regmap(struct rt9467_info *info)
{
	int ret = 0;
	struct i2c_client *client = info->client;
	struct rt_regmap_properties *prop = NULL;

	dev_info(info->dev, "%s\n", __func__);

	prop = devm_kzalloc(&client->dev, sizeof(struct rt_regmap_properties),
		GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop->name = info->desc->regmap_name;
	prop->aliases = info->desc->regmap_name;
	prop->register_num = ARRAY_SIZE(rt9467_regmap_map);
	prop->rm = rt9467_regmap_map;
	prop->rt_regmap_mode = RT_SINGLE_BYTE | RT_CACHE_DISABLE |
		RT_IO_PASS_THROUGH;
	prop->io_log_en = 0;

	info->regmap_prop = prop;
	info->regmap_dev = rt_regmap_device_register_ex(info->regmap_prop,
		&rt9467_regmap_fops, &client->dev, client,
		info->desc->regmap_represent_slave_addr, info);
	if (!info->regmap_dev) {
		dev_notice(info->dev, "%s: register regmap dev fail\n",
			__func__);
		return -EIO;
	}

	return ret;
}
#endif /* CONFIG_RT_REGMAP */

static inline int __rt9467_i2c_write_byte(struct rt9467_info *info, u8 cmd,
	u8 data)
{
	int ret = 0, retry = 0;

	do {
#if 0
//#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_write(info->regmap_dev, cmd, 1, &data);
#else
		ret = rt9467_device_write(info->client, cmd, 1, &data);
#endif
		retry++;
		if (ret < 0)
			udelay(10);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0)
		dev_notice(info->dev, "%s: I2CW[0x%02X] = 0x%02X fail\n",
			__func__, cmd, data);
	else
		dev_dbg(info->dev, "%s: I2CW[0x%02X] = 0x%02X\n", __func__,
			cmd, data);

	return ret;
}

static int rt9467_i2c_write_byte(struct rt9467_info *info, u8 cmd, u8 data)
{
	int ret = 0;

	mutex_lock(&info->i2c_access_lock);
	ret = __rt9467_i2c_write_byte(info, cmd, data);
	mutex_unlock(&info->i2c_access_lock);

	return ret;
}

static inline int __rt9467_i2c_read_byte(struct rt9467_info *info, u8 cmd)
{
	int ret = 0, ret_val = 0, retry = 0;

	do {
#if 0
//#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_read(info->regmap_dev, cmd, 1, &ret_val);
#else
		ret = rt9467_device_read(info->client, cmd, 1, &ret_val);
#endif
		retry++;
		if (ret < 0)
			udelay(10);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_notice(info->dev, "%s: I2CR[0x%02X] fail\n", __func__, cmd);
		return ret;
	}

	ret_val = ret_val & 0xFF;

	dev_dbg(info->dev, "%s: I2CR[0x%02X] = 0x%02X\n", __func__, cmd,
		ret_val);

	return ret_val;
}

static int rt9467_i2c_read_byte(struct rt9467_info *info, u8 cmd)
{
	int ret = 0;

	mutex_lock(&info->i2c_access_lock);
	ret = __rt9467_i2c_read_byte(info, cmd);
	mutex_unlock(&info->i2c_access_lock);

	if (ret < 0)
		return ret;

	return (ret & 0xFF);
}

static inline int __rt9467_i2c_block_write(struct rt9467_info *info, u8 cmd,
	u32 leng, const u8 *data)
{
	int ret = 0;

#if 0
//#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_write(info->regmap_dev, cmd, leng, data);
#else
	ret = rt9467_device_write(info->client, cmd, leng, data);
#endif

	return ret;
}


static int rt9467_i2c_block_write(struct rt9467_info *info, u8 cmd, u32 leng,
	const u8 *data)
{
	int ret = 0;

	mutex_lock(&info->i2c_access_lock);
	ret = __rt9467_i2c_block_write(info, cmd, leng, data);
	mutex_unlock(&info->i2c_access_lock);

	return ret;
}

static inline int __rt9467_i2c_block_read(struct rt9467_info *info, u8 cmd,
	u32 leng, u8 *data)
{
	int ret = 0;

#if 0
//#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_read(info->regmap_dev, cmd, leng, data);
#else
	ret = rt9467_device_read(info->client, cmd, leng, data);
#endif

	return ret;
}


static int rt9467_i2c_block_read(struct rt9467_info *info, u8 cmd, u32 leng,
	u8 *data)
{
	int ret = 0;

	mutex_lock(&info->i2c_access_lock);
	ret = __rt9467_i2c_block_read(info, cmd, leng, data);
	mutex_unlock(&info->i2c_access_lock);

	return ret;
}


static int rt9467_i2c_test_bit(struct rt9467_info *info, u8 cmd, u8 shift,
	bool *is_one)
{
	int ret = 0;
	u8 data = 0;

	ret = rt9467_i2c_read_byte(info, cmd);
	if (ret < 0) {
		*is_one = false;
		return ret;
	}

	data = ret & (1 << shift);
	*is_one = (data == 0 ? false : true);

	return ret;
}

static int rt9467_i2c_update_bits(struct rt9467_info *info, u8 cmd, u8 data,
	u8 mask)
{
	int ret = 0;
	u8 reg_data = 0;

	mutex_lock(&info->i2c_access_lock);
	ret = __rt9467_i2c_read_byte(info, cmd);
	if (ret < 0) {
		mutex_unlock(&info->i2c_access_lock);
		return ret;
	}

	reg_data = ret & 0xFF;
	reg_data &= ~mask;
	reg_data |= (data & mask);

	ret = __rt9467_i2c_write_byte(info, cmd, reg_data);
	mutex_unlock(&info->i2c_access_lock);

	return ret;
}

static inline int rt9467_set_bit(struct rt9467_info *info, u8 reg, u8 mask)
{
	return rt9467_i2c_update_bits(info, reg, mask, mask);
}

static inline int rt9467_clr_bit(struct rt9467_info *info, u8 reg, u8 mask)
{
	return rt9467_i2c_update_bits(info, reg, 0x00, mask);
}

/* ================== */
/* Internal Functions */
/* ================== */
static int rt9467_get_mivr(struct charger_device *chg_dev, u32 *mivr);
static int __rt9467_get_mivr(struct rt9467_info *info, u32 *mivr);
static int rt9467_get_aicr(struct charger_device *chg_dev, u32 *aicr);
static int rt9467_get_ichg(struct charger_device *chg_dev, u32 *ichg);
static int rt9467_set_aicr(struct charger_device *chg_dev, u32 aicr);
static int rt9467_set_ichg(struct charger_device *chg_dev, u32 aicr);
static int rt9467_kick_wdt(struct charger_device *chg_dev);
static int rt9467_enable_charging(struct charger_device *chg_dev, bool en);
static int rt9467_get_ieoc(struct rt9467_info *info, u32 *ieoc);
static int rt9467_enable_hidden_mode(struct rt9467_info *info, bool en);

static inline void rt9467_irq_set_flag(struct rt9467_info *info, u8 *irq,
	u8 mask)
{
	mutex_lock(&info->irq_access_lock);
	*irq |= mask;
	mutex_unlock(&info->irq_access_lock);
}

static inline void rt9467_irq_clr_flag(struct rt9467_info *info, u8 *irq,
	u8 mask)
{
	mutex_lock(&info->irq_access_lock);
	*irq &= ~mask;
	mutex_unlock(&info->irq_access_lock);
}

static inline const char *rt9467_get_irq_name(struct rt9467_info *info,
	int irqnum)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(rt9467_irq_mapping_tbl); i++) {
		if (rt9467_irq_mapping_tbl[i].id == irqnum)
			return rt9467_irq_mapping_tbl[i].name;
	}

	return "not found";
}

static inline void rt9467_irq_mask(struct rt9467_info *info, int irqnum)
{
	dev_dbg(info->dev, "%s: irq = %d, %s\n", __func__, irqnum,
		rt9467_get_irq_name(info, irqnum));
	info->irq_mask[irqnum / 8] |= (1 << (irqnum % 8));
}

static inline void rt9467_irq_unmask(struct rt9467_info *info, int irqnum)
{
	dev_dbg(info->dev, "%s: irq = %d, %s\n", __func__, irqnum,
		rt9467_get_irq_name(info, irqnum));
	info->irq_mask[irqnum / 8] &= ~(1 << (irqnum % 8));
}

static inline u8 rt9467_closest_reg(u32 min, u32 max, u32 step, u32 target)
{
	/* Smaller than minimum supported value, use minimum one */
	if (target < min)
		return 0;

	/* Greater than maximum supported value, use maximum one */
	if (target >= max)
		return (max - min) / step;

	return (target - min) / step;
}

static inline u8 rt9467_closest_reg_via_tbl(const u32 *tbl, u32 tbl_size,
	u32 target)
{
	u32 i = 0;

	/* Smaller than minimum supported value, use minimum one */
	if (target < tbl[0])
		return 0;

	for (i = 0; i < tbl_size - 1; i++) {
		if (target >= tbl[i] && target < tbl[i + 1])
			return i;
	}

	/* Greater than maximum supported value, use maximum one */
	return tbl_size - 1;
}

static inline u32 rt9467_closest_value(u32 min, u32 max, u32 step, u8 reg_val)
{
	u32 ret_val = 0;

	ret_val = min + reg_val * step;
	if (ret_val > max)
		ret_val = max;

	return ret_val;
}

static int rt9467_get_adc(struct rt9467_info *info,
	enum rt9467_adc_sel adc_sel, int *adc_val)
{
	int ret = 0, i = 0;
	const int max_wait_times = 6;
	u8 adc_data[6] = {0};
	u32 aicr = 0, ichg = 0;
	bool adc_start = false;

	mutex_lock(&info->adc_access_lock);

	rt9467_enable_hidden_mode(info, true);

	/* Select ADC to desired channel */
	ret = rt9467_i2c_update_bits(info, RT9467_REG_CHG_ADC,
		adc_sel << RT9467_SHIFT_ADC_IN_SEL, RT9467_MASK_ADC_IN_SEL);
	if (ret < 0) {
		dev_notice(info->dev, "%s: select ch to %d fail(%d)\n",
			__func__, adc_sel, ret);
		goto out;
	}

	/* Workaround for IBUS & IBAT */
	if (adc_sel == RT9467_ADC_IBUS) {
		mutex_lock(&info->aicr_access_lock);
		ret = rt9467_get_aicr(info->chg_dev, &aicr);
		if (ret < 0) {
			dev_notice(info->dev, "%s: get aicr fail\n", __func__);
			goto out_unlock_all;
		}
	} else if (adc_sel == RT9467_ADC_IBAT) {
		mutex_lock(&info->ichg_access_lock);
		ret = rt9467_get_ichg(info->chg_dev, &ichg);
		if (ret < 0) {
			dev_notice(info->dev, "%s: get ichg fail\n", __func__);
			goto out_unlock_all;
		}
	}

	/* Start ADC conversation */
	ret = rt9467_set_bit(info, RT9467_REG_CHG_ADC, RT9467_MASK_ADC_START);
	if (ret < 0) {
		dev_notice(info->dev, "%s: start con fail(%d), sel = %d\n",
			__func__, ret, adc_sel);
		goto out_unlock_all;
	}

	for (i = 0; i < max_wait_times; i++) {
		msleep(35);
		ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_ADC,
			RT9467_SHIFT_ADC_START, &adc_start);
		if (ret >= 0 && !adc_start)
			break;
	}
	if (i == max_wait_times) {
		dev_notice(info->dev, "%s: wait con fail(%d), sel = %d\n",
			__func__, ret, adc_sel);
		ret = -EINVAL;
		goto out_unlock_all;
	}

	mdelay(1);

	/* Read ADC data high/low byte */
	ret = rt9467_i2c_block_read(info, RT9467_REG_ADC_DATA_H, 6, adc_data);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read ADC data fail\n", __func__);
		goto out_unlock_all;
	}
	dev_dbg(info->dev,
		"%s: adc_tune = (0x%02X, 0x%02X), adc_org = (0x%02X, 0x%02X)\n",
		__func__, adc_data[2], adc_data[3], adc_data[4], adc_data[5]);

	/* Calculate ADC value */
	*adc_val = ((adc_data[0] << 8) + adc_data[1]) * rt9467_adc_unit[adc_sel]
		+ rt9467_adc_offset[adc_sel];

	dev_dbg(info->dev,
		"%s: adc_sel = %d, adc_h = 0x%02X, adc_l = 0x%02X, val = %d\n",
		__func__, adc_sel, adc_data[0], adc_data[1], *adc_val);

	ret = 0;

out_unlock_all:
	/* Coefficient of IBUS & IBAT */
	if (adc_sel == RT9467_ADC_IBUS) {
		if (aicr < 400000) /* 400mA */
			*adc_val = *adc_val * 67 / 100;
		mutex_unlock(&info->aicr_access_lock);
	} else if (adc_sel == RT9467_ADC_IBAT) {
		if (ichg >= 100000 && ichg <= 450000) /* 100~450mA */
			*adc_val = *adc_val * 57 / 100;
		else if (ichg >= 500000 && ichg <= 850000) /* 500~850mA */
			*adc_val = *adc_val * 63 / 100;
		mutex_unlock(&info->ichg_access_lock);
	}

out:
	rt9467_enable_hidden_mode(info, false);
	mutex_unlock(&info->adc_access_lock);
	return ret;
}

static int rt9467_set_usbsw_state(struct rt9467_info *info, int state)
{
	dev_info(info->dev, "%s: state = %d\n", __func__, state);

	if (state == RT9467_USBSW_CHG)
		Charger_Detect_Init();
	else
		Charger_Detect_Release();

	return 0;
}

static inline int __rt9467_enable_chgdet_flow(struct rt9467_info *info, bool en)
{
	int ret = 0;
	enum rt9467_usbsw_state usbsw =
		en ? RT9467_USBSW_CHG : RT9467_USBSW_USB;

	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	rt9467_set_usbsw_state(info, usbsw);
	ret = (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_DPDM1, RT9467_MASK_USBCHGEN);
	if (ret >= 0)
		info->bc12_en = en;

	return ret;
}

static int rt9467_enable_chgdet_flow(struct rt9467_info *info, bool en)
{
	int ret = 0, i = 0;
	bool pwr_rdy = false;
	const int max_wait_cnt = 200;

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	if (en) {
		/* Workaround for CDP port */
		for (i = 0; i < max_wait_cnt; i++) {
			if (is_usb_rdy())
				break;
			dev_dbg(info->dev, "%s: CDP block\n", __func__);
			ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_STATC,
				RT9467_SHIFT_PWR_RDY, &pwr_rdy);
			if (ret >= 0 && !pwr_rdy) {
				dev_info(info->dev, "%s: plug out\n",
					__func__);
				return 0;
			}
			msleep(100);
		}
		if (i == max_wait_cnt)
			dev_notice(info->dev, "%s: CDP timeout\n", __func__);
		else
			dev_info(info->dev, "%s: CDP free\n", __func__);
	}

	mutex_lock(&info->bc12_access_lock);
	ret = __rt9467_enable_chgdet_flow(info, en);
	mutex_unlock(&info->bc12_access_lock);

	return ret;
}

static int rt9467_inform_psy_changed(struct rt9467_info *info)
{
	int ret = 0;
	union power_supply_propval propval;

	dev_info(info->dev, "%s: pwr_rdy = %d, type = %d\n", __func__,
		info->pwr_rdy, info->chg_type);

	/* Get chg type det power supply */
	info->psy = power_supply_get_by_name("charger");
	if (!info->psy) {
		dev_notice(info->dev, "%s: get power supply fail\n", __func__);
		return -EINVAL;
	}

	/* inform chg det power supply */
	propval.intval = info->pwr_rdy;
	ret = power_supply_set_property(info->psy, POWER_SUPPLY_PROP_ONLINE,
		&propval);
	if (ret < 0)
		dev_notice(info->dev, "%s: psy online fail(%d)\n", __func__,
			ret);

	propval.intval = info->chg_type;
	ret = power_supply_set_property(info->psy,
		POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
	if (ret < 0)
		dev_notice(info->dev, "%s: psy type fail(%d)\n", __func__, ret);

	return ret;
}

static inline int rt9467_enable_ilim(struct rt9467_info *info, bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL3, RT9467_MASK_ILIM_EN);
}

static inline int rt9467_toggle_chgdet_flow(struct rt9467_info *info)
{
	int ret = 0;
	u8 data = 0, usbd_off[2] = {0}, usbd_on[2] = {0};
	int retry = 3;
	struct i2c_client *client = info->client;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = usbd_off,
		},
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = usbd_on,
		},
	};
 
	dev_info(info->dev, "%s\n", __func__);
	/* read data */
	ret = i2c_smbus_read_i2c_block_data(client, RT9467_REG_CHG_DPDM1,
		1, &data);

	if (ret < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_i2c_block_data(client, RT9467_REG_CHG_DPDM1,
				1, &data);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		dev_notice(info->dev, "%s: read usbd fail\n", __func__);
		goto out;
	}

	/* usbd off and then on */
	usbd_off[0] = usbd_on[0] = RT9467_REG_CHG_DPDM1;
	usbd_off[1] = data & ~RT9467_MASK_USBCHGEN;
	usbd_on[1] = data | RT9467_MASK_USBCHGEN;
	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		dev_notice(info->dev, "%s: usbd off/on fail(%d)\n",
				      __func__, ret);
out:

	return ret < 0 ? ret : 0;
}

static int rt9467_bc12_sdp_workaround(struct rt9467_info *info)
{
	int ret = 0;

	dev_info(info->dev, "%s\n", __func__);

	mutex_lock(&info->i2c_access_lock);

	ret = rt9467_toggle_chgdet_flow(info);
	if (ret < 0)
		goto err;

	mdelay(10);

	ret = rt9467_toggle_chgdet_flow(info);
	if (ret < 0)
		goto err;

	goto out;
err:
	dev_notice(info->dev, "%s: fail\n", __func__);
out:
	mutex_unlock(&info->i2c_access_lock);
	return ret;
}
static int rt9467_reset_hvdcp_ta_ex(struct charger_device *chg_dev);
static int rt9467_bc12_hvdcp_workaround(struct rt9467_info *info)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	
	dev_info(info->dev, "%s\n", __func__);

	rt9467_reset_hvdcp_ta_ex(info->chg_dev);
	ret = rt9467_set_bit(info, RT9467_REG_CHG_DPDM1, 0x40);
	if (ret < 0) {
		dev_notice(info->dev, "%s: fail\n", __func__);
		return ret;
	}
	get_monotonic_boottime(&rt9467->ptime[0]);
	mutex_lock(&info->i2c_access_lock);
	ret = rt9467_toggle_chgdet_flow(info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: fail\n", __func__);
	}
	
        mutex_unlock(&info->i2c_access_lock);
	return ret;
}

static int __rt9467_chgdet_handler(struct rt9467_info *info)
{
	int ret = 0;
	bool pwr_rdy = false, inform_psy = true, bypass_usbsw = false, hvdcp_state = false;
	u8 usb_status = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
   
	dev_info(info->dev, "%s\n", __func__);

	/* disabled by user, do nothing */
	if (!info->desc->en_chgdet) {
		dev_info(info->dev, "%s: bc12 is disabled by dts\n", __func__);
		return 0;
	}

#ifdef CONFIG_TCPC_CLASS
	pwr_rdy = atomic_read(&info->tcpc_usb_connected);
#else
	/* check power ready */
	ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_STATC,
		RT9467_SHIFT_PWR_RDY, &pwr_rdy);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read pwr rdy state fail\n",
			__func__);
		return ret;
	}
#endif

	/* no change in pwr_rdy state */
	if (info->pwr_rdy == pwr_rdy &&
		atomic_read(&info->bc12_wkard) == 0 &&
		atomic_read(&info->hvdcp_en_cnt) >= 5) {
		dev_info(info->dev, "%s: pwr_rdy(%d) state is the same\n",
			__func__, pwr_rdy);
		inform_psy = false;
        dev_notice(info->dev,"usbchgen off when pwr_rdy is the same");
		goto out;
	}
	info->pwr_rdy = pwr_rdy;

	/* plug out */
	if (!pwr_rdy) {
		info->chg_type = CHARGER_UNKNOWN;
		charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		atomic_set(&info->bc12_sdp_cnt, 0);
		atomic_set(&info->hvdcp_en_cnt, 0);
		/* force clear HVDCP_EN */
		rt9467_clr_bit(info, RT9467_REG_CHG_DPDM1, 0x40);
#ifdef OPLUS_FEATURE_CHG_BASIC
		memset(&rt9467->ptime[0], 0, sizeof(struct timespec));
		memset(&rt9467->ptime[1], 0, sizeof(struct timespec));
		info->desc->pre_current_ma = -1;
#endif
        dev_notice(info->dev,"usbchgen off when plug out");
		goto out;
	}

	/* plug in */
	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_DPDM2);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read type fail\n", __func__);
		return ret;
	}
	usb_status = (ret & RT9467_MASK_USB_STATUS) >> RT9467_SHIFT_USB_STATUS;

	switch (usb_status) {
	case RT9467_CHG_TYPE_UNDER_GOING:
		dev_info(info->dev, "%s: under going...\n", __func__);
		return ret;
	case RT9467_CHG_TYPE_SDP:
		info->chg_type = STANDARD_HOST;
		break;
	case RT9467_CHG_TYPE_SDPNSTD:
		info->chg_type = NONSTANDARD_CHARGER;
		break;
	case RT9467_CHG_TYPE_CDP:
		info->chg_type = CHARGING_HOST;
		break;
	case RT9467_CHG_TYPE_DCP:
		info->chg_type = STANDARD_CHARGER;
		break;
	default:
		info->chg_type = NONSTANDARD_CHARGER;
		break;
	}
    dev_notice(info->dev, "%s: read chg type = %d\n", __func__, info->chg_type);
	/* BC12 workaround (NONSTD -> STP) */
	if (atomic_read(&info->bc12_sdp_cnt) < 2 &&
		info->chg_type == STANDARD_HOST) {
		ret = rt9467_bc12_sdp_workaround(info);
		/* Workaround success, wait for next event */
		if (ret >= 0) {
			atomic_inc(&info->bc12_sdp_cnt);
			atomic_set(&info->bc12_wkard, 1);
			return ret;
		}
		goto out;
	}

	if (info->chg_type == STANDARD_CHARGER) {
        rt9467_i2c_test_bit(info, RT9467_REG_CHG_DPDM1, RT9467_SHIFT_HVDCP, &hvdcp_state);
		if (atomic_read(&info->hvdcp_en_cnt) < 5 && !hvdcp_state) {
            dev_notice(info->dev, "usbchgen hvdcp_en_cnt: %d\n",atomic_read(&info->hvdcp_en_cnt));
            mdelay(50);
			ret = rt9467_bc12_hvdcp_workaround(info);
			if (ret < 0)
				goto out;
            if (atomic_read(&info->hvdcp_en_cnt))
                inform_psy = false;
			atomic_inc(&info->hvdcp_en_cnt);

		} else {
			//atomic_set(&info->hvdcp_en_cnt, 0);
			inform_psy = false;
		}
		bypass_usbsw = true;
	}

out:
	atomic_set(&info->bc12_wkard, 0);

	/* turn off USB charger detection */
	if (!bypass_usbsw) {
        dev_notice(info->dev, "usbchgen off when not bypass\n");
		ret = __rt9467_enable_chgdet_flow(info, false);
		if (ret < 0)
			dev_notice(info->dev, "%s: disable chrdet fail\n", __func__);
	}

	if (inform_psy){
			switch (info->chg_type) {
			case STANDARD_HOST:
				charger_type = POWER_SUPPLY_TYPE_USB;
				break;
			case NONSTANDARD_CHARGER:
				charger_type = POWER_SUPPLY_TYPE_USB_DCP;
				break;
			case CHARGING_HOST:
				charger_type = POWER_SUPPLY_TYPE_USB_CDP;
				break;
			case STANDARD_CHARGER:
				charger_type = POWER_SUPPLY_TYPE_USB_DCP;
				break;
			case CHARGER_UNKNOWN:
				charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
				break;
			default:
				charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
				break;
			}
		rt9467_inform_psy_changed(info);
	}

	return 0;
}

static int rt9467_chgdet_handler(struct rt9467_info *info)
{
	int ret = 0;

	mutex_lock(&info->bc12_access_lock);
	ret = __rt9467_chgdet_handler(info);
	mutex_unlock(&info->bc12_access_lock);

	return ret;
}

static int rt9467_set_aicl_vth(struct rt9467_info *info, u32 aicl_vth)
{
	u8 reg_aicl_vth = 0;

	reg_aicl_vth = rt9467_closest_reg(RT9467_AICL_VTH_MIN,
		RT9467_AICL_VTH_MAX, RT9467_AICL_VTH_STEP, aicl_vth);

	dev_info(info->dev, "%s: vth = %d(0x%02X)\n", __func__, aicl_vth,
		reg_aicl_vth);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL14,
		reg_aicl_vth << RT9467_SHIFT_AICL_VTH, RT9467_MASK_AICL_VTH);
}

static int __rt9467_set_aicr(struct rt9467_info *info, u32 aicr)
{
	u8 reg_aicr = 0;

	reg_aicr = rt9467_closest_reg(RT9467_AICR_MIN, RT9467_AICR_MAX,
		RT9467_AICR_STEP, aicr);

	dev_info(info->dev, "%s: aicr = %d(0x%02X)\n", __func__, aicr,
		reg_aicr);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL3,
		reg_aicr << RT9467_SHIFT_AICR, RT9467_MASK_AICR);
}

static int __rt9467_run_aicl(struct rt9467_info *info)
{
	int ret = 0;
	u32 mivr = 0, aicl_vth = 0, aicr = 0;
	bool mivr_act = false;

	/* Check whether MIVR loop is active */
	ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_STATC,
		RT9467_SHIFT_CHG_MIVR, &mivr_act);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read mivr stat fail\n", __func__);
		goto out;
	}

	if (!mivr_act) {
		dev_info(info->dev, "%s: mivr loop is not active\n", __func__);
		goto out;
	}

	ret = __rt9467_get_mivr(info, &mivr);
	if (ret < 0)
		goto out;

	/* Check if there's a suitable AICL_VTH */
	aicl_vth = mivr + 200000;
	if (aicl_vth > RT9467_AICL_VTH_MAX) {
		dev_notice(info->dev, "%s: no suitable vth, vth = %d\n",
			__func__, aicl_vth);
		ret = -EINVAL;
		goto out;
	}

	ret = rt9467_set_aicl_vth(info, aicl_vth);
	if (ret < 0)
		goto out;

	/* Clear AICL measurement IRQ */
	rt9467_irq_clr_flag(info, &info->irq_flag[RT9467_IRQIDX_CHG_IRQ2],
		RT9467_MASK_CHG_AICLMEASI);

	mutex_lock(&info->pe_access_lock);
	mutex_lock(&info->aicr_access_lock);

	ret = rt9467_set_bit(info, RT9467_REG_CHG_CTRL14,
		RT9467_MASK_AICL_MEAS);
	if (ret < 0)
		goto unlock_out;

	ret = wait_event_interruptible_timeout(info->wait_queue,
		info->irq_flag[RT9467_IRQIDX_CHG_IRQ2] &
		RT9467_MASK_CHG_AICLMEASI,
		msecs_to_jiffies(3500));
	if (ret <= 0) {
		dev_notice(info->dev, "%s: wait AICL time out\n", __func__);
		ret = -EIO;
		goto unlock_out;
	}

	ret = rt9467_get_aicr(info->chg_dev, &aicr);
	if (ret < 0)
		goto unlock_out;

	info->aicr_limit = aicr;
	dev_dbg(info->dev, "%s: OK, aicr upper bound = %dmA\n", __func__,
		aicr / 1000);

unlock_out:
	mutex_unlock(&info->aicr_access_lock);
	mutex_unlock(&info->pe_access_lock);
out:
	return ret;
}

#ifndef CONFIG_TCPC_CLASS
static void rt9467_chgdet_work_handler(struct work_struct *work)
{
	int ret = 0;
	bool pwr_rdy = false;
	struct rt9467_info *info = (struct rt9467_info *)container_of(work,
		struct rt9467_info, chgdet_work);

	/* Check power ready */
	ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_STATC,
		RT9467_SHIFT_PWR_RDY, &pwr_rdy);

	if (!pwr_rdy)
		return;

	/* Enable USB charger type detection */
	ret = rt9467_enable_chgdet_flow(info, true);
	if (ret < 0)
		dev_notice(info->dev, "%s: enable usb chrdet fail\n", __func__);

}
#endif /* CONFIG_TCPC_CLASS */


/* Prevent back boost */
static int rt9467_toggle_cfo(struct rt9467_info *info)
{
	int ret = 0;
	u8 data = 0;

	dev_info(info->dev, "%s\n", __func__);
	mutex_lock(&info->i2c_access_lock);
	ret = rt9467_device_read(info->client, RT9467_REG_CHG_CTRL2, 1, &data);
	if (ret < 0) {
		dev_notice(info->dev, "%s read cfo fail(%d)\n", __func__, ret);
		goto out;
	}

	/* CFO off */
	data &= ~RT9467_MASK_CFO_EN;
	ret = rt9467_device_write(info->client, RT9467_REG_CHG_CTRL2, 1, &data);
	if (ret < 0) {
		dev_notice(info->dev, "%s cfo off fail(%d)\n", __func__, ret);
		goto out;
	}

	/* CFO on */
	data |= RT9467_MASK_CFO_EN;
	ret = rt9467_device_write(info->client, RT9467_REG_CHG_CTRL2, 1, &data);
	if (ret < 0)
		dev_notice(info->dev, "%s cfo on fail(%d)\n", __func__, ret);

out:
	mutex_unlock(&info->i2c_access_lock);
	return ret;
}

/* IRQ handlers */
static int rt9467_pwr_rdy_irq_handler(struct rt9467_info *info)
{
#ifndef CONFIG_TCPC_CLASS
	int ret = 0;
	bool pwr_rdy = false;
#endif /* CONFIG_TCPC_CLASS */

	dev_notice(info->dev, "%s\n", __func__);

#ifndef CONFIG_TCPC_CLASS
	ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_STATC,
		RT9467_SHIFT_PWR_RDY, &pwr_rdy);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read pwr rdy fail\n", __func__);
		goto out;
	}

	if (!pwr_rdy) {
		dev_info(info->dev, "%s: pwr rdy = 0\n", __func__);
		goto out;
	}

	ret = rt9467_enable_chgdet_flow(info, true);
	if (ret < 0)
		dev_notice(info->dev, "%s: en chgdet fail(%d)\n", __func__,
			ret);

out:
#endif /* CONFIG_TCPC_CLASS */

	return 0;
}

static int rt9467_chg_mivr_irq_handler(struct rt9467_info *info)
{
	int ret = 0;
	bool mivr_act = false;
	int adc_ibus = 0;

	dev_notice(info->dev, "%s\n", __func__);

	/* Check whether MIVR loop is active */
	ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_STATC,
		RT9467_SHIFT_CHG_MIVR, &mivr_act);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read mivr stat fail\n", __func__);
		goto out;
	}

	if (!mivr_act) {
		dev_info(info->dev, "%s: mivr loop is not active\n", __func__);
		goto out;
	}

	if (strcmp(info->desc->chg_dev_name, "primary_chg") == 0) {
		/* Check IBUS ADC */
		ret = rt9467_get_adc(info, RT9467_ADC_IBUS, &adc_ibus);
		if (ret < 0) {
			dev_notice(info->dev, "%s: get ibus fail\n", __func__);
			return ret;
		}
		if (adc_ibus < 100000) { /* 100mA */
			ret = rt9467_toggle_cfo(info);
			return ret;
		}
	}
out:
	return 0;
}

static int rt9467_chg_aicr_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_treg_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_vsysuv_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_vsysov_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_vbatov_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_vbusov_irq_handler(struct rt9467_info *info)
{
	int ret = 0;
	bool vbusov = false;
	struct chgdev_notify *noti = &(info->chg_dev->noti);

	dev_notice(info->dev, "%s\n", __func__);
	ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_FAULT,
		RT9467_SHIFT_VBUSOV, &vbusov);
	if (ret < 0)
		return ret;

	noti->vbusov_stat = vbusov;
	dev_info(info->dev, "%s: vbusov = %d\n", __func__, vbusov);
	//charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_VBUS_OVP);

	return 0;
}

static int rt9467_ts_bat_cold_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_ts_bat_cool_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_ts_bat_warm_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_ts_bat_hot_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_ts_statci_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_faulti_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_statci_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_tmri_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	//charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT);
	return 0;
}

static int rt9467_chg_batabsi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_adpbadi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_rvpi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_otpi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_aiclmeasi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	rt9467_irq_set_flag(info, &info->irq_flag[RT9467_IRQIDX_CHG_IRQ2],
		RT9467_MASK_CHG_AICLMEASI);
	wake_up_interruptible(&info->wait_queue);
	return 0;
}

static int rt9467_chg_ichgmeasi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chgdet_donei_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_wdtmri_irq_handler(struct rt9467_info *info)
{
	int ret = 0;

	dev_notice(info->dev, "%s\n", __func__);
	ret = rt9467_kick_wdt(info->chg_dev);
	if (ret < 0)
		dev_notice(info->dev, "%s: kick wdt fail\n", __func__);

	return ret;
}

static int rt9467_ssfinishi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_rechgi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	//charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_RECHG);
	return 0;
}

static int rt9467_chg_termi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_chg_ieoci_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	//charger_dev_notify(info->chg_dev, CHARGER_DEV_NOTIFY_EOC);
	return 0;
}

static int rt9467_adc_donei_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_pumpx_donei_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_bst_batuvi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_bst_midovi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_enable_otg(struct charger_device *chg_dev, bool en);
static int rt9467_bst_olpi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s: otp_ocp_cnt = %d\n", __func__,
		info->otg_ocp_cnt);

	if (info->otg_ocp_cnt < 3)
		info->otg_ocp_cnt++;
	else {
		dev_notice(info->dev, "%s: otp_ocp_cnt access retry, disable otg\n", __func__);
		info->otg_ocp_cnt = 0;
		return rt9467_enable_otg(info->chg_dev, false);
	}

	return 0;
}

static int rt9467_attachi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);

	/* check bc12_en state */
	mutex_lock(&info->bc12_access_lock);
	if (!info->bc12_en) {
		dev_notice(info->dev, "%s: bc12 disabled, ignore irq\n",
			__func__);
		mutex_unlock(&info->bc12_access_lock);
		return 0;
	}
	mutex_unlock(&info->bc12_access_lock);
	return rt9467_chgdet_handler(info);
}

static int rt9467_detachi_irq_handler(struct rt9467_info *info)
{
	int ret = 0;

	dev_notice(info->dev, "%s\n", __func__);
    info->hvdcp_can_enabled = 0;
#ifndef CONFIG_TCPC_CLASS
	ret = rt9467_chgdet_handler(info);
#endif /* CONFIG_TCPC_CLASS */
	return ret;
}

static int rt9467_hvdcpi_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	
#ifdef OPLUS_FEATURE_CHG_BASIC
	if(disable_QC){
		dev_info(info->dev, "%s:disable_QC\n", __func__);
		return 0;
	}
#endif

    info->hvdcp_can_enabled = 1;
    return 0;
}

static int rt9467_chgdeti_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

static int rt9467_dcdti_irq_handler(struct rt9467_info *info)
{
	dev_notice(info->dev, "%s\n", __func__);
	return 0;
}

typedef int (*rt9467_irq_fptr)(struct rt9467_info *);
static rt9467_irq_fptr rt9467_irq_handler_tbl[56] = {
	NULL,
	NULL,
	NULL,
	NULL,
	rt9467_chg_treg_irq_handler,
	rt9467_chg_aicr_irq_handler,
	rt9467_chg_mivr_irq_handler,
	rt9467_pwr_rdy_irq_handler,
	NULL,
	NULL,
	NULL,
	NULL,
	rt9467_chg_vsysuv_irq_handler,
	rt9467_chg_vsysov_irq_handler,
	rt9467_chg_vbatov_irq_handler,
	rt9467_chg_vbusov_irq_handler,
	NULL,
	NULL,
	NULL,
	NULL,
	rt9467_ts_bat_cold_irq_handler,
	rt9467_ts_bat_cool_irq_handler,
	rt9467_ts_bat_warm_irq_handler,
	rt9467_ts_bat_hot_irq_handler,
	rt9467_ts_statci_irq_handler,
	rt9467_chg_faulti_irq_handler,
	rt9467_chg_statci_irq_handler,
	rt9467_chg_tmri_irq_handler,
	rt9467_chg_batabsi_irq_handler,
	rt9467_chg_adpbadi_irq_handler,
	rt9467_chg_rvpi_irq_handler,
	rt9467_chg_otpi_irq_handler,
	rt9467_chg_aiclmeasi_irq_handler,
	rt9467_chg_ichgmeasi_irq_handler,
	rt9467_chgdet_donei_irq_handler,
	rt9467_wdtmri_irq_handler,
	rt9467_ssfinishi_irq_handler,
	rt9467_chg_rechgi_irq_handler,
	rt9467_chg_termi_irq_handler,
	rt9467_chg_ieoci_irq_handler,
	rt9467_adc_donei_irq_handler,
	rt9467_pumpx_donei_irq_handler,
	NULL,
	NULL,
	NULL,
	rt9467_bst_batuvi_irq_handler,
	rt9467_bst_midovi_irq_handler,
	rt9467_bst_olpi_irq_handler,
	rt9467_attachi_irq_handler,
	rt9467_detachi_irq_handler,
	NULL,
	NULL,
	rt9467_hvdcpi_irq_handler,
	NULL,
	rt9467_chgdeti_irq_handler,
	rt9467_dcdti_irq_handler,
};

static inline int rt9467_enable_irqrez(struct rt9467_info *info, bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL13, RT9467_MASK_IRQ_REZ);
}

static int __rt9467_irq_handler(struct rt9467_info *info)
{
	int ret = 0, i = 0, j = 0;
	u8 evt[RT9467_IRQIDX_MAX] = {0};
	u8 mask[RT9467_IRQIDX_MAX] = {0};
	u8 stat[RT9467_IRQSTAT_MAX] = {0};
	u8 usb_status_old = 0, usb_status_new = 0;
	bool is_otg_mode = false, is_adc_done = false;

	dev_info(info->dev, "%s\n", __func__);

	/* Read DPDM status before reading evts */
	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_DPDM2);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read type fail\n", __func__);
		goto err_read_irq;
	}
	usb_status_old = (ret & RT9467_MASK_USB_STATUS) >>
		RT9467_SHIFT_USB_STATUS;

	/* Read event and skip CHG_IRQ3 */
	ret = rt9467_i2c_block_read(info, RT9467_REG_CHG_IRQ1, 2, &evt[3]);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read evt1 fail(%d)\n", __func__,
			ret);
		goto err_read_irq;
	}
	
	/* For otg mode read bst_olpi, cannot skip */
	ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_CTRL1,
		RT9467_SHIFT_OPA_MODE, &is_otg_mode);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read opa_mode fail(%d)\n", __func__,
			ret);
		goto err_read_irq;
	}
	if (is_otg_mode) {
		mutex_lock(&info->adc_access_lock);
		ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_ADC,
				RT9467_SHIFT_ADC_START, &is_adc_done);
		if (ret < 0) {
			dev_notice(info->dev, "%s: read adc_start fail(%d)\n",
					__func__,ret);
			mutex_unlock(&info->adc_access_lock);
			goto err_read_irq;
		}
		is_adc_done = !is_adc_done;
		if (is_adc_done) {
			/*Read CHG_IRQ3*/
			ret = rt9467_i2c_block_read(info, RT9467_REG_CHG_IRQ3,
				1, &evt[5]);
			if (ret < 0) {
				dev_notice(info->dev,
				"%s: read chg_irq3 fail(%d)\n", __func__,ret);
				mutex_unlock(&info->adc_access_lock);
				goto err_read_irq;
			}
		}
		mutex_unlock(&info->adc_access_lock);
	}

	ret = rt9467_i2c_block_read(info, RT9467_REG_DPDM_IRQ, 1, &evt[6]);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read evt2 fail(%d)\n", __func__,
			ret);
		goto err_read_irq;
	}

	ret = rt9467_i2c_block_read(info, RT9467_REG_CHG_STATC, 3, evt);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read stat fail(%d)\n", __func__,
			ret);
		goto err_read_irq;
	}

	/* Read DPDM status after reading evts */
	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_DPDM2);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read type fail\n", __func__);
		goto err_read_irq;
	}
	usb_status_new = (ret & RT9467_MASK_USB_STATUS) >>
		RT9467_SHIFT_USB_STATUS;

	/* Read mask */
	ret = rt9467_i2c_block_read(info, RT9467_REG_CHG_STATC_CTRL,
		ARRAY_SIZE(mask), mask);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read mask fail(%d)\n", __func__,
			ret);
		goto err_read_irq;
	}

	/* Detach */
	if (usb_status_old != RT9467_CHG_TYPE_NOVBUS &&
		usb_status_new == RT9467_CHG_TYPE_NOVBUS)
		evt[RT9467_IRQIDX_DPDM_IRQ] |= 0x02;

	/* Attach */
	if (usb_status_new >= RT9467_CHG_TYPE_SDP &&
		usb_status_new <= RT9467_CHG_TYPE_CDP &&
		usb_status_old != usb_status_new)
		evt[RT9467_IRQIDX_DPDM_IRQ] |= 0x01;

	/* Store/Update stat */
	memcpy(stat, info->irq_stat, RT9467_IRQSTAT_MAX);

	for (i = 0; i < RT9467_IRQIDX_MAX; i++) {
		evt[i] &= ~mask[i];
		if (i < RT9467_IRQSTAT_MAX) {
			info->irq_stat[i] = evt[i];
			evt[i] ^= stat[i];
		}
		for (j = 0; j < 8; j++) {
			if (!(evt[i] & (1 << j)))
				continue;
			if (rt9467_irq_handler_tbl[i * 8 + j])
				rt9467_irq_handler_tbl[i * 8 + j](info);
		}
	}

err_read_irq:
	return ret;
}

static irqreturn_t rt9467_irq_handler(int irq, void *data)
{
	int ret = 0;
	struct rt9467_info *info = (struct rt9467_info *)data;

	dev_info(info->dev, "%s\n", __func__);

	ret = __rt9467_irq_handler(info);
	ret = rt9467_enable_irqrez(info, true);
	if (ret < 0)
		dev_notice(info->dev, "%s: en irqrez fail\n", __func__);

	return IRQ_HANDLED;
}

static int rt9467_irq_register(struct rt9467_info *info)
{
	int ret = 0, len = 0;
	char *name = NULL;

	if (strcmp(info->desc->chg_dev_name, "secondary_chg") == 0)
		return 0;

	dev_info(info->dev, "%s\n", __func__);

	/* request gpio */
	len = strlen(info->desc->chg_dev_name);
	name = devm_kzalloc(info->dev, len + 10, GFP_KERNEL);
	snprintf(name,  len + 10, "%s_irq_gpio", info->desc->chg_dev_name);
	ret = devm_gpio_request_one(info->dev, info->intr_gpio, GPIOF_IN, name);
	if (ret < 0) {
		dev_notice(info->dev, "%s: gpio request fail\n", __func__);
		return ret;
	}

	ret = gpio_to_irq(info->intr_gpio);
	if (ret < 0) {
		dev_notice(info->dev, "%s: irq mapping fail\n", __func__);
		return ret;
	}
	info->irq = ret;
	dev_info(info->dev, "%s: irq = %d\n", __func__, info->irq);

	/* Request threaded IRQ */
	name = devm_kzalloc(info->dev, len + 5, GFP_KERNEL);
	snprintf(name, len + 5, "%s_irq", info->desc->chg_dev_name);
	ret = devm_request_threaded_irq(info->dev, info->irq, NULL,
		rt9467_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name,
		info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: request thread irq fail\n",
			__func__);
		return ret;
	}
	device_init_wakeup(info->dev, true);

	return 0;
}

static inline int rt9467_maskall_irq(struct rt9467_info *info)
{
	dev_info(info->dev, "%s\n", __func__);
	return rt9467_i2c_block_write(info, RT9467_REG_CHG_STATC_CTRL,
		ARRAY_SIZE(rt9467_irq_maskall), rt9467_irq_maskall);
}

static inline int rt9467_irq_init(struct rt9467_info *info)
{
	dev_info(info->dev, "%s\n", __func__);
	return rt9467_i2c_block_write(info, RT9467_REG_CHG_STATC_CTRL,
		ARRAY_SIZE(info->irq_mask), info->irq_mask);
}

static bool rt9467_is_hw_exist(struct rt9467_info *info)
{
	int ret = 0;
	u8 vendor_id = 0, chip_rev = 0;
	int retry = 3;

	ret = i2c_smbus_read_byte_data(info->client, RT9467_REG_DEVICE_ID);

	if (ret < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_byte_data(info->client, RT9467_REG_DEVICE_ID);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0)
		return false;

	vendor_id = ret & 0xF0;
	chip_rev = ret & 0x0F;
	if (vendor_id != RT9467_VENDOR_ID) {
		dev_notice(info->dev, "%s: vendor id is incorrect (0x%02X)\n",
			__func__, vendor_id);
		return false;
	}

	dev_info(info->dev, "%s: 0x%02X\n", __func__, chip_rev);
	info->chip_rev = chip_rev;

	return true;
}

static int rt9467_set_safety_timer(struct rt9467_info *info, u32 hr)
{
	u8 reg_st = 0;

	reg_st = rt9467_closest_reg_via_tbl(rt9467_safety_timer,
		ARRAY_SIZE(rt9467_safety_timer), hr);

	dev_info(info->dev, "%s: time = %d(0x%02X)\n", __func__, hr, reg_st);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL12,
		reg_st << RT9467_SHIFT_WT_FC, RT9467_MASK_WT_FC);
}

static inline int rt9467_enable_wdt(struct rt9467_info *info, bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL13, RT9467_MASK_WDT_EN);
}

static inline int rt9467_select_input_current_limit(struct rt9467_info *info,
	enum rt9467_iin_limit_sel sel)
{
	dev_info(info->dev, "%s: sel = %d\n", __func__, sel);
	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL2,
		sel << RT9467_SHIFT_IINLMTSEL, RT9467_MASK_IINLMTSEL);
}

static int rt9467_enable_hidden_mode(struct rt9467_info *info, bool en)
{
	int ret = 0;

	mutex_lock(&info->hidden_mode_lock);

	if (en) {
		if (info->hidden_mode_cnt == 0) {
			ret = rt9467_i2c_block_write(info, 0x70,
				ARRAY_SIZE(rt9467_val_en_hidden_mode),
				rt9467_val_en_hidden_mode);
			if (ret < 0)
				goto err;
		}
		info->hidden_mode_cnt++;
	} else {
		if (info->hidden_mode_cnt == 1) /* last one */
			ret = rt9467_i2c_write_byte(info, 0x70, 0x00);
		info->hidden_mode_cnt--;
		if (ret < 0)
			goto err;
	}
	dev_dbg(info->dev, "%s: en = %d\n", __func__, en);
	goto out;

err:
	dev_notice(info->dev, "%s: en = %d fail(%d)\n", __func__, en, ret);
out:
	mutex_unlock(&info->hidden_mode_lock);
	return ret;
}

static int rt9467_set_iprec(struct rt9467_info *info, u32 iprec)
{
	u8 reg_iprec = 0;

	reg_iprec = rt9467_closest_reg(RT9467_IPREC_MIN, RT9467_IPREC_MAX,
		RT9467_IPREC_STEP, iprec);

	dev_info(info->dev, "%s: iprec = %d(0x%02X)\n", __func__, iprec,
		reg_iprec);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL8,
		reg_iprec << RT9467_SHIFT_IPREC, RT9467_MASK_IPREC);
}

static int rt9467_sw_workaround(struct rt9467_info *info)
{
	int ret = 0;

	dev_info(info->dev, "%s\n", __func__);

	rt9467_enable_hidden_mode(info, true);

	/* Disable TS auto sensing */
	ret = rt9467_clr_bit(info, RT9467_REG_CHG_HIDDEN_CTRL15, 0x01);
	if (ret < 0)
		goto out;

	/* Set precharge current to 850mA, only do this in normal boot */
	if (info->chip_rev <= RT9467_CHIP_REV_E3) {
		/* Worst case delay: wait auto sensing */
		msleep(200);

		if (get_boot_mode() == NORMAL_BOOT) {
			ret = rt9467_set_iprec(info, 850000);
			if (ret < 0)
				goto out;

			/* Increase Isys drop threshold to 2.5A */
			ret = rt9467_i2c_write_byte(info,
				RT9467_REG_CHG_HIDDEN_CTRL7, 0x1c);
			if (ret < 0)
				goto out;
		}
	}

	/* Only revision <= E1 needs the following workaround */
	if (info->chip_rev > RT9467_CHIP_REV_E1)
		goto out;

	/* ICC: modify sensing node, make it more accurate */
	ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL8, 0x00);
	if (ret < 0)
		goto out;

	/* DIMIN level */
	ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL9, 0x86);

out:
	rt9467_enable_hidden_mode(info, false);
	return ret;
}

static inline int rt9467_enable_hz(struct rt9467_info *info, bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL1, RT9467_MASK_HZ_EN);
}

/* Reset all registers' value to default */
static int rt9467_reset_chip(struct rt9467_info *info)
{
	int ret = 0;

	dev_info(info->dev, "%s\n", __func__);

	/* disable hz before reset chip */
	ret = rt9467_enable_hz(info, false);
	if (ret < 0) {
		dev_notice(info->dev, "%s: disable hz fail\n", __func__);
		return ret;
	}

	return rt9467_set_bit(info, RT9467_REG_CORE_CTRL0, RT9467_MASK_RST);
}

static inline int __rt9467_enable_te(struct rt9467_info *info, bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL2, RT9467_MASK_TE_EN);
}

static inline int __rt9467_enable_safety_timer(struct rt9467_info *info,
	bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL12, RT9467_MASK_TMR_EN);
}

static int __rt9467_set_ieoc(struct rt9467_info *info, u32 ieoc)
{
	int ret = 0;
	u8 reg_ieoc = 0;

	/* IEOC workaround */
	if (info->ieoc_wkard)
		ieoc += 100000; /* 100mA */

	reg_ieoc = rt9467_closest_reg(RT9467_IEOC_MIN, RT9467_IEOC_MAX,
		RT9467_IEOC_STEP, ieoc);

	dev_info(info->dev, "%s: ieoc = %d(0x%02X)\n", __func__, ieoc,
		reg_ieoc);

	ret = rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL9,
		reg_ieoc << RT9467_SHIFT_IEOC, RT9467_MASK_IEOC);

	/* Store IEOC */
	return rt9467_get_ieoc(info, &info->ieoc);
}

static int __rt9467_get_mivr(struct rt9467_info *info, u32 *mivr)
{
	int ret = 0;
	u8 reg_mivr = 0;

	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_CTRL6);
	if (ret < 0)
		return ret;
	reg_mivr = ((ret & RT9467_MASK_MIVR) >> RT9467_SHIFT_MIVR) & 0xFF;

	*mivr = rt9467_closest_value(RT9467_MIVR_MIN, RT9467_MIVR_MAX,
		RT9467_MIVR_STEP, reg_mivr);

	return ret;
}

static int rt9467_get_mivr(struct charger_device *chg_dev, u32 *mivr)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return __rt9467_get_mivr(info, mivr);
}

static int __rt9467_set_mivr(struct rt9467_info *info, u32 mivr)
{
	u8 reg_mivr = 0;

	reg_mivr = rt9467_closest_reg(RT9467_MIVR_MIN, RT9467_MIVR_MAX,
		RT9467_MIVR_STEP, mivr);

	dev_info(info->dev, "%s: mivr = %d(0x%02X)\n", __func__, mivr,
		reg_mivr);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL6,
		reg_mivr << RT9467_SHIFT_MIVR, RT9467_MASK_MIVR);
}

static inline int rt9467_enable_jeita(struct rt9467_info *info, bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL16, RT9467_MASK_JEITA_EN);
}


static int rt9467_get_charging_status(struct rt9467_info *info,
	enum rt9467_charging_status *chg_stat)
{
	int ret = 0;

	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_STAT);
	if (ret < 0)
		return ret;

	*chg_stat = (ret & RT9467_MASK_CHG_STAT) >> RT9467_SHIFT_CHG_STAT;

	return ret;
}

static int rt9467_get_ieoc(struct rt9467_info *info, u32 *ieoc)
{
	int ret = 0;
	u8 reg_ieoc = 0;

	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_CTRL9);
	if (ret < 0)
		return ret;

	reg_ieoc = (ret & RT9467_MASK_IEOC) >> RT9467_SHIFT_IEOC;
	*ieoc = rt9467_closest_value(RT9467_IEOC_MIN, RT9467_IEOC_MAX,
		RT9467_IEOC_STEP, reg_ieoc);

	return ret;
}

static inline int __rt9467_is_charging_enable(struct rt9467_info *info,
	bool *en)
{
	return rt9467_i2c_test_bit(info, RT9467_REG_CHG_CTRL2,
		RT9467_SHIFT_CHG_EN, en);
}

static int __rt9467_get_ichg(struct rt9467_info *info, u32 *ichg)
{
	int ret = 0;
	u8 reg_ichg = 0;

	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_CTRL7);
	if (ret < 0)
		return ret;

	reg_ichg = (ret & RT9467_MASK_ICHG) >> RT9467_SHIFT_ICHG;
	*ichg = rt9467_closest_value(RT9467_ICHG_MIN, RT9467_ICHG_MAX,
		RT9467_ICHG_STEP, reg_ichg);

	return ret;
}

static inline int rt9467_ichg_workaround(struct rt9467_info *info, u32 uA)
{
	int ret = 0;

	/* Vsys short protection */
	rt9467_enable_hidden_mode(info, true);

	if (info->ichg >= 900000 && uA < 900000)
		ret = rt9467_i2c_update_bits(info, RT9467_REG_CHG_HIDDEN_CTRL7,
			0x00, 0x60);
	else if (uA >= 900000 && info->ichg < 900000)
		ret = rt9467_i2c_update_bits(info, RT9467_REG_CHG_HIDDEN_CTRL7,
			0x40, 0x60);

	rt9467_enable_hidden_mode(info, false);
	return ret;
}

static int __rt9467_set_ichg(struct rt9467_info *info, u32 ichg)
{
	int ret = 0;
	u8 reg_ichg = 0;
#if 0
	if (strcmp(info->desc->chg_dev_name, "primary_chg") == 0) {
		ichg = (ichg < 500000) ? 500000 : ichg;
		rt9467_ichg_workaround(info, ichg);
	}
#endif

	reg_ichg = rt9467_closest_reg(RT9467_ICHG_MIN, RT9467_ICHG_MAX,
		RT9467_ICHG_STEP, ichg);

	dev_info(info->dev, "%s: ichg = %d(0x%02X)\n", __func__, ichg,
		reg_ichg);

	ret = rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL7,
		reg_ichg << RT9467_SHIFT_ICHG, RT9467_MASK_ICHG);
	if (ret < 0)
		return ret;

	/* Store Ichg setting */
	__rt9467_get_ichg(info, &info->ichg);

	/* Workaround to make IEOC accurate */
	if (ichg < 900000 && !info->ieoc_wkard) { /* 900mA */
		ret = __rt9467_set_ieoc(info, info->ieoc + 100000);
		info->ieoc_wkard = true;
	} else if (ichg >= 900000 && info->ieoc_wkard) {
		info->ieoc_wkard = false;
		ret = __rt9467_set_ieoc(info, info->ieoc - 100000);
	}

	return ret;
}

static int __rt9467_set_cv(struct rt9467_info *info, u32 cv)
{
	u8 reg_cv = 0;

	reg_cv = rt9467_closest_reg(RT9467_CV_MIN, RT9467_CV_MAX,
		RT9467_CV_STEP, cv);

	dev_info(info->dev, "%s: cv = %d(0x%02X)\n", __func__, cv, reg_cv);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL4,
		reg_cv << RT9467_SHIFT_CV, RT9467_MASK_CV);
}

static int rt9467_set_ircmp_resistor(struct rt9467_info *info, u32 uohm)
{
	u8 reg_resistor = 0;

	reg_resistor = rt9467_closest_reg(RT9467_IRCMP_RES_MIN,
		RT9467_IRCMP_RES_MAX, RT9467_IRCMP_RES_STEP, uohm);

	dev_info(info->dev, "%s: resistor = %d(0x%02X)\n", __func__, uohm,
		reg_resistor);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL18,
		reg_resistor << RT9467_SHIFT_IRCMP_RES, RT9467_MASK_IRCMP_RES);
}

static int rt9467_set_ircmp_vclamp(struct rt9467_info *info, u32 uV)
{
	u8 reg_vclamp = 0;

	reg_vclamp = rt9467_closest_reg(RT9467_IRCMP_VCLAMP_MIN,
		RT9467_IRCMP_VCLAMP_MAX, RT9467_IRCMP_VCLAMP_STEP, uV);

	dev_info(info->dev, "%s: vclamp = %d(0x%02X)\n", __func__, uV,
		reg_vclamp);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL18,
		reg_vclamp << RT9467_SHIFT_IRCMP_VCLAMP,
		RT9467_MASK_IRCMP_VCLAMP);
}

static int rt9467_enable_pump_express(struct rt9467_info *info, bool en)
{
	int ret = 0, i = 0;
	bool pumpx_en = false;
	const int max_wait_times = 3;

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	ret = rt9467_set_aicr(info->chg_dev, 800000);
	if (ret < 0)
		return ret;

	ret = rt9467_set_ichg(info->chg_dev, 2000000);
	if (ret < 0)
		return ret;

	ret = rt9467_enable_charging(info->chg_dev, true);
	if (ret < 0)
		return ret;

	rt9467_enable_hidden_mode(info, true);

	ret = rt9467_clr_bit(info, RT9467_REG_CHG_HIDDEN_CTRL9, 0x80);
	if (ret < 0)
		dev_notice(info->dev, "%s: disable skip mode fail\n", __func__);

	ret = (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL17, RT9467_MASK_PUMPX_EN);
	if (ret < 0)
		goto out;

	for (i = 0; i < max_wait_times; i++) {
		msleep(2500);
		ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_CTRL17,
			RT9467_SHIFT_PUMPX_EN, &pumpx_en);
		if (ret >= 0 && !pumpx_en)
			break;
	}
	if (i == max_wait_times) {
		dev_notice(info->dev, "%s: pumpx done fail(%d)\n", __func__,
			ret);
		ret = -EIO;
	} else
		ret = 0;

out:
	rt9467_set_bit(info, RT9467_REG_CHG_HIDDEN_CTRL9, 0x80);
	rt9467_enable_hidden_mode(info, false);
	return ret;
}

static inline int rt9467_enable_irq_pulse(struct rt9467_info *info, bool en)
{
	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL1, RT9467_MASK_IRQ_PULSE);
}

static inline int rt9467_get_irq_number(struct rt9467_info *info,
	const char *name)
{
	int i = 0;

	if (!name) {
		dev_notice(info->dev, "%s: null name\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rt9467_irq_mapping_tbl); i++) {
		if (!strcmp(name, rt9467_irq_mapping_tbl[i].name))
			return rt9467_irq_mapping_tbl[i].id;
	}

	return -EINVAL;
}

static int rt9467_parse_dt(struct rt9467_info *info, struct device *dev)
{
	int ret = 0, irq_cnt = 0;
	struct rt9467_desc *desc = NULL;
	struct device_node *np = dev->of_node;
	const char *name = NULL;
	int irqnum = 0;

	dev_info(info->dev, "%s\n", __func__);

	if (!np) {
		dev_notice(info->dev, "%s: no device node\n", __func__);
		return -EINVAL;
	}

	info->desc = &rt9467_default_desc;

	desc = devm_kzalloc(dev, sizeof(struct rt9467_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &rt9467_default_desc, sizeof(struct rt9467_desc));

	if (of_property_read_string(np, "charger_name",
		&desc->chg_dev_name) < 0)
		dev_notice(info->dev, "%s: no charger name\n", __func__);

#if (!defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND))
	ret = of_get_named_gpio(np, "rt,intr_gpio", 0);
	if (ret < 0)
		return ret;
	info->intr_gpio = ret;
	if (strcmp(desc->chg_dev_name, "secondary_chg") == 0) {
		ret = of_get_named_gpio(np, "rt,ceb_gpio", 0);
		if (ret < 0)
			return ret;
		info->ceb_gpio = ret;
	}
#else
	ret = of_property_read_u32(np, "rt,intr_gpio_num", &info->intr_gpio);
	if (ret < 0)
		return ret;
	if (strcmp(desc->chg_dev_name, "secondary_chg") == 0) {
		ret = of_property_read_u32(np, "rt,ceb_gpio_num",
			&info->ceb_gpio);
		if (ret < 0)
			return ret;
	}
#endif

	dev_info(info->dev, "%s: intr/ceb gpio = %d, %d\n", __func__,
		info->intr_gpio, info->ceb_gpio);

	/* request ceb gpio for secondary charger */
	if (strcmp(desc->chg_dev_name, "secondary_chg") == 0) {
		ret = devm_gpio_request_one(info->dev, info->ceb_gpio,
			GPIOF_DIR_OUT, "rt9467_sec_ceb_gpio");
		if (ret < 0) {
			dev_notice(info->dev, "%s: ceb gpio request fail\n",
				__func__);
			return ret;
		}
	}

	if (of_property_read_u32(np, "regmap_represent_slave_addr",
		&desc->regmap_represent_slave_addr) < 0)
		dev_notice(info->dev, "%s: no regmap slave addr\n", __func__);

	if (of_property_read_string(np, "regmap_name",
		&(desc->regmap_name)) < 0)
		dev_notice(info->dev, "%s: no regmap name\n", __func__);

	if (of_property_read_u32(np, "ichg", &desc->ichg) < 0)
		dev_notice(info->dev, "%s: no ichg\n", __func__);

	if (of_property_read_u32(np, "aicr", &desc->aicr) < 0)
		dev_notice(info->dev, "%s: no aicr\n", __func__);

	if (of_property_read_u32(np, "mivr", &desc->mivr) < 0)
		dev_notice(info->dev, "%s: no mivr\n", __func__);

	if (of_property_read_u32(np, "cv", &desc->cv) < 0)
		dev_notice(info->dev, "%s: no cv\n", __func__);

	if (of_property_read_u32(np, "ieoc", &desc->ieoc) < 0)
		dev_notice(info->dev, "%s: no ieoc\n", __func__);

	if (of_property_read_u32(np, "safety_timer", &desc->safety_timer) < 0)
		dev_notice(info->dev, "%s: no safety timer\n", __func__);

	if (of_property_read_u32(np, "ircmp_resistor",
		&desc->ircmp_resistor) < 0)
		dev_notice(info->dev, "%s: no ircmp resistor\n", __func__);

	if (of_property_read_u32(np, "ircmp_vclamp", &desc->ircmp_vclamp) < 0)
		dev_notice(info->dev, "%s: no ircmp vclamp\n", __func__);

	desc->en_te = of_property_read_bool(np, "en_te");
	desc->en_wdt = of_property_read_bool(np, "en_wdt");
	desc->en_irq_pulse = of_property_read_bool(np, "en_irq_pulse");
	desc->en_jeita = of_property_read_bool(np, "en_jeita");
	desc->ceb_invert = of_property_read_bool(np, "ceb_invert");
	desc->en_chgdet = of_property_read_bool(np, "en_chgdet");

	while (true) {
		ret = of_property_read_string_index(np, "interrupt-names",
			irq_cnt, &name);
		if (ret < 0)
			break;
		irq_cnt++;
		irqnum = rt9467_get_irq_number(info, name);
		if (irqnum >= 0)
			rt9467_irq_unmask(info, irqnum);
	}

	info->desc = desc;

	return 0;
}


/* =========================================================== */
/* Released interfaces                                         */
/* =========================================================== */

static int __rt9467_enable_charging(struct rt9467_info *info, bool en)
{
	int ret = 0;

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	/* set hz/ceb pin for secondary charger */
	if (strcmp(info->desc->chg_dev_name, "secondary_chg") == 0) {
		ret = rt9467_enable_hz(info, !en);
		if (ret < 0) {
			dev_notice(info->dev, "%s: set hz of sec chg fail\n",
				__func__);
			return ret;
		}
		if (info->desc->ceb_invert)
			gpio_set_value(info->ceb_gpio, en);
		else
			gpio_set_value(info->ceb_gpio, !en);
	}

	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL2, RT9467_MASK_CHG_EN);

}

static int rt9467_enable_charging(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	/* set hz/ceb pin for secondary charger */
	if (strcmp(info->desc->chg_dev_name, "secondary_chg") == 0) {
		ret = rt9467_enable_hz(info, !en);
		if (ret < 0) {
			dev_notice(info->dev, "%s: set hz of sec chg fail\n",
				__func__);
			return ret;
		}
		if (info->desc->ceb_invert)
			gpio_set_value(info->ceb_gpio, en);
		else
			gpio_set_value(info->ceb_gpio, !en);
	}

	return (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL2, RT9467_MASK_CHG_EN);
}

static int rt9467_enable_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return __rt9467_enable_safety_timer(info, en);
}

static int __rt9467_set_boost_current_limit(struct rt9467_info *info,
	u32 current_limit)
{
	u8 reg_ilimit = 0;

	reg_ilimit = rt9467_closest_reg_via_tbl(rt9467_boost_oc_threshold,
		ARRAY_SIZE(rt9467_boost_oc_threshold), current_limit);

	dev_info(info->dev, "%s: boost ilimit = %d(0x%02X)\n", __func__,
		current_limit, reg_ilimit);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL10,
		reg_ilimit << RT9467_SHIFT_BOOST_OC, RT9467_MASK_BOOST_OC);
}

static int rt9467_set_boost_current_limit(struct charger_device *chg_dev,
	u32 current_limit)
{
	u8 reg_ilimit = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	reg_ilimit = rt9467_closest_reg_via_tbl(rt9467_boost_oc_threshold,
		ARRAY_SIZE(rt9467_boost_oc_threshold), current_limit);

	dev_info(info->dev, "%s: boost ilimit = %d(0x%02X)\n", __func__,
		current_limit, reg_ilimit);

	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL10,
		reg_ilimit << RT9467_SHIFT_BOOST_OC, RT9467_MASK_BOOST_OC);
}

static int __rt9467_enable_otg(struct rt9467_info *info, bool en)
{
		int ret = 0;
		bool en_otg = false;
		u8 hidden_val = en ? 0x00 : 0x0F;
		u8 lg_slew_rate = en ? 0x7c : 0x73;
	
		dev_info(info->dev, "%s: en = %d\n", __func__, en);
	
		mutex_lock(&info->otg_access_lock);
		rt9467_enable_hidden_mode(info, true);

		if(en)
			oplus_wake_up_usbtemp_thread();

		/* Set OTG_OC to 500mA */
		ret = __rt9467_set_boost_current_limit(info, 1300000);
		if (ret < 0) {
			dev_notice(info->dev, "%s: set current limit fail\n", __func__);
			mutex_unlock(&info->otg_access_lock);
			return ret;
		}
	
		/*
		 * Woraround : slow Low side mos Gate driver slew rate
		 * for decline VBUS noise
		 * reg[0x23] = 0x7c after entering OTG mode
		 * reg[0x23] = 0x73 after leaving OTG mode
		 */
		ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL4,
			lg_slew_rate);
		if (ret < 0) {
			dev_notice(info->dev,
				"%s: set Low side mos Gate drive speed fail(%d)\n",
				__func__, ret);
			goto out;
		}
	
		/* Enable WDT */
		if (en && info->desc->en_wdt) {
			ret = rt9467_enable_wdt(info, true);
			if (ret < 0) {
				dev_notice(info->dev, "%s: en wdt fail\n", __func__);
				goto err_en_otg;
			}
		}
	
		/* Switch OPA mode */
		ret = (en ? rt9467_set_bit : rt9467_clr_bit)
			(info, RT9467_REG_CHG_CTRL1, RT9467_MASK_OPA_MODE);
	
		msleep(20);
	
		if (en) {
			ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_CTRL1,
				RT9467_SHIFT_OPA_MODE, &en_otg);
			if (ret < 0 || !en_otg) {
				dev_notice(info->dev, "%s: otg fail(%d)\n", __func__,
					ret);
				goto err_en_otg;
			}
		}
	
		/*
		 * Woraround reg[0x25] = 0x00 after entering OTG mode
		 * reg[0x25] = 0x0F after leaving OTG mode
		 */
		ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL6,
			hidden_val);
		if (ret < 0)
			dev_notice(info->dev, "%s: workaroud fail(%d)\n", __func__,
				ret);
	
		/* Disable WDT */
		if (!en) {
			ret = rt9467_enable_wdt(info, false);
			if (ret < 0)
				dev_notice(info->dev, "%s: disable wdt fail\n",
					__func__);
		}
		goto out;
	
	err_en_otg:
		/* Disable WDT */
		ret = rt9467_enable_wdt(info, false);
		if (ret < 0)
			dev_notice(info->dev, "%s: disable wdt fail\n", __func__);
	
		/* Recover Low side mos Gate slew rate */
		ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL4, 0x73);
		if (ret < 0)
			dev_notice(info->dev,
				"%s: recover Low side mos Gate drive speed fail(%d)\n",
				__func__, ret);
		ret = -EIO;
	out:
		rt9467_enable_hidden_mode(info, false);
		mutex_unlock(&info->otg_access_lock);
		return ret;

}

static int rt9467_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	bool en_otg = false;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);
	u8 hidden_val = en ? 0x00 : 0x0F;
	u8 lg_slew_rate = en ? 0x7c : 0x73;

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	mutex_lock(&info->otg_access_lock);
	rt9467_enable_hidden_mode(info, true);

	if(en)
		oplus_wake_up_usbtemp_thread();

	/* Set OTG_OC to 500mA */
	ret = rt9467_set_boost_current_limit(chg_dev, 1300000);
	if (ret < 0) {
		dev_notice(info->dev, "%s: set current limit fail\n", __func__);
		mutex_unlock(&info->otg_access_lock);
		return ret;
	}

	/*
	 * Woraround : slow Low side mos Gate driver slew rate
	 * for decline VBUS noise
	 * reg[0x23] = 0x7c after entering OTG mode
	 * reg[0x23] = 0x73 after leaving OTG mode
	 */
	ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL4,
		lg_slew_rate);
	if (ret < 0) {
		dev_notice(info->dev,
			"%s: set Low side mos Gate drive speed fail(%d)\n",
			__func__, ret);
		goto out;
	}

	/* Enable WDT */
	if (en && info->desc->en_wdt) {
		ret = rt9467_enable_wdt(info, true);
		if (ret < 0) {
			dev_notice(info->dev, "%s: en wdt fail\n", __func__);
			goto err_en_otg;
		}
	}

	/* Switch OPA mode */
	ret = (en ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL1, RT9467_MASK_OPA_MODE);

	msleep(20);

	if (en) {
		ret = rt9467_i2c_test_bit(info, RT9467_REG_CHG_CTRL1,
			RT9467_SHIFT_OPA_MODE, &en_otg);
		if (ret < 0 || !en_otg) {
			dev_notice(info->dev, "%s: otg fail(%d)\n", __func__,
				ret);
			goto err_en_otg;
		}
	}

	/*
	 * Woraround reg[0x25] = 0x00 after entering OTG mode
	 * reg[0x25] = 0x0F after leaving OTG mode
	 */
	ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL6,
		hidden_val);
	if (ret < 0)
		dev_notice(info->dev, "%s: workaroud fail(%d)\n", __func__,
			ret);

	/* Disable WDT */
	if (!en) {
		ret = rt9467_enable_wdt(info, false);
		if (ret < 0)
			dev_notice(info->dev, "%s: disable wdt fail\n",
				__func__);
	}
	goto out;

err_en_otg:
	/* Disable WDT */
	ret = rt9467_enable_wdt(info, false);
	if (ret < 0)
		dev_notice(info->dev, "%s: disable wdt fail\n", __func__);

	/* Recover Low side mos Gate slew rate */
	ret = rt9467_i2c_write_byte(info, RT9467_REG_CHG_HIDDEN_CTRL4, 0x73);
	if (ret < 0)
		dev_notice(info->dev,
			"%s: recover Low side mos Gate drive speed fail(%d)\n",
			__func__, ret);
	ret = -EIO;
out:
	rt9467_enable_hidden_mode(info, false);
	mutex_unlock(&info->otg_access_lock);
	return ret;
}

static int rt9467_enable_discharge(struct charger_device *chg_dev, bool en)
{
	int ret = 0, i = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);
	const int check_dischg_max = 3;
	bool is_dischg = true;

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	ret = rt9467_enable_hidden_mode(info, true);
	if (ret < 0)
		return ret;

	/* Set bit2 of reg[0x21] to 1 to enable discharging */
	ret = (en ? rt9467_set_bit : rt9467_clr_bit)(info,
		RT9467_REG_CHG_HIDDEN_CTRL2, 0x04);
	if (ret < 0) {
		dev_notice(info->dev, "%s: en = %d, fail\n", __func__, en);
		return ret;
	}

	if (!en) {
		for (i = 0; i < check_dischg_max; i++) {
			ret = rt9467_i2c_test_bit(info,
				RT9467_REG_CHG_HIDDEN_CTRL2, 2, &is_dischg);
			if (ret >= 0 && !is_dischg)
				break;
			/* Disable discharging */
			ret = rt9467_clr_bit(info, RT9467_REG_CHG_HIDDEN_CTRL2,
				0x04);
		}
		if (i == check_dischg_max)
			dev_notice(info->dev, "%s: disable dischg fail(%d)\n",
				__func__, ret);
	}

	rt9467_enable_hidden_mode(info, false);
	return ret;
}

static int rt9467_enable_power_path(struct charger_device *chg_dev, bool en)
{
	u32 mivr = (en ? 4500000 : RT9467_MIVR_MAX);
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "%s: en = %d\n", __func__, en);
	return __rt9467_set_mivr(info, mivr);
}

static int rt9467_enable_chg_type_det(struct charger_device *chg_dev, bool en)
{
	int ret = 0;

#ifdef CONFIG_TCPC_CLASS
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	if (!info->desc->en_chgdet) {
		dev_info(info->dev, "%s: bc12 is disabled by dts\n", __func__);
		return 0;
	}

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	atomic_set(&info->tcpc_usb_connected, en);

	/* TypeC detach */
	if (!en) {
        dev_notice(info->dev, "usbchgen off when typec unattach");
		ret = rt9467_chgdet_handler(info);
		return ret;
	}

	/* plug in, make usb switch to RT9467 */
	ret = rt9467_enable_chgdet_flow(info, true);
	if (ret < 0)
		dev_notice(info->dev, "%s: en chgdet fail(%d)\n", __func__,
			ret);
#endif /* CONFIG_TCPC_CLASS */

	return ret;
}

static int rt9467_is_power_path_enable(struct charger_device *chg_dev, bool *en)
{
	int ret = 0;
	u32 mivr = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = __rt9467_get_mivr(info, &mivr);
	if (ret < 0)
		return ret;

	*en = ((mivr == RT9467_MIVR_MAX) ? false : true);

	return ret;
}

static int rt9467_set_ichg(struct charger_device *chg_dev, u32 ichg)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	mutex_lock(&info->ichg_access_lock);
	mutex_lock(&info->ieoc_lock);
	ret = __rt9467_set_ichg(info, ichg);
	mutex_unlock(&info->ieoc_lock);
	mutex_unlock(&info->ichg_access_lock);

	return ret;
}

static int rt9467_set_ieoc(struct charger_device *chg_dev, u32 ieoc)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	mutex_lock(&info->ichg_access_lock);
	mutex_lock(&info->ieoc_lock);
	ret = __rt9467_set_ieoc(info, ieoc);
	mutex_unlock(&info->ieoc_lock);
	mutex_unlock(&info->ichg_access_lock);

	return ret;
}

static int rt9467_set_aicr(struct charger_device *chg_dev, u32 aicr)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	mutex_lock(&info->aicr_access_lock);
	ret = __rt9467_set_aicr(info, aicr);
	info->desc->pre_current_ma = -1;
	mutex_unlock(&info->aicr_access_lock);

	return ret;
}

static int rt9467_set_mivr(struct charger_device *chg_dev, u32 mivr)
{
	int ret = 0;
	bool en = true;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = rt9467_is_power_path_enable(chg_dev, &en);
	if (ret < 0) {
		dev_notice(info->dev, "%s: get power path en fail\n", __func__);
		return ret;
	}

	if (!en) {
		dev_info(info->dev,
			"%s: power path is disabled, op is not allowed\n",
			__func__);
		return -EINVAL;
	}

	return __rt9467_set_mivr(info, mivr);
}

static int rt9467_set_cv(struct charger_device *chg_dev, u32 cv)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return __rt9467_set_cv(info, cv);
}

static int rt9467_set_pep_current_pattern(struct charger_device *chg_dev,
	bool is_increase)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "%s: pump_up = %d\n", __func__, is_increase);

	mutex_lock(&info->pe_access_lock);

	/* Set to PE1.0 */
	ret = rt9467_clr_bit(info, RT9467_REG_CHG_CTRL17,
		RT9467_MASK_PUMPX_20_10);

	/* Set Pump Up/Down */
	ret = (is_increase ? rt9467_set_bit : rt9467_clr_bit)
		(info, RT9467_REG_CHG_CTRL17, RT9467_MASK_PUMPX_UP_DN);

	/* Enable PumpX */
	ret = rt9467_enable_pump_express(info, true);
	mutex_unlock(&info->pe_access_lock);

	return ret;
}

static int rt9467_set_pep20_reset(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	mutex_lock(&info->pe_access_lock);
	ret = rt9467_set_mivr(chg_dev, 4500000);
	if (ret < 0)
		goto out;

	/* Disable PSK mode */
	rt9467_enable_hidden_mode(info, true);
	ret = rt9467_clr_bit(info, RT9467_REG_CHG_HIDDEN_CTRL9, 0x80);
	if (ret < 0)
		dev_notice(info->dev, "%s: disable skip mode fail\n", __func__);

	ret = rt9467_set_aicr(chg_dev, 100000);
	if (ret < 0)
		goto psk_out;

	msleep(250);

	ret = rt9467_set_aicr(chg_dev, 700000);

psk_out:
	rt9467_set_bit(info, RT9467_REG_CHG_HIDDEN_CTRL9, 0x80);
	rt9467_enable_hidden_mode(info, false);
out:
	mutex_unlock(&info->pe_access_lock);
	return ret;
}

static int rt9467_set_pep20_current_pattern(struct charger_device *chg_dev,
	u32 uV)
{
	int ret = 0;
	u8 reg_volt = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	mutex_lock(&info->pe_access_lock);

	reg_volt = rt9467_closest_reg(RT9467_PEP20_VOLT_MIN,
		RT9467_PEP20_VOLT_MAX, RT9467_PEP20_VOLT_STEP, uV);

	dev_info(info->dev, "%s: volt = %d(0x%02X)\n", __func__, uV, reg_volt);

	/* Set to PEP2.0 */
	ret = rt9467_set_bit(info, RT9467_REG_CHG_CTRL17,
		RT9467_MASK_PUMPX_20_10);
	if (ret < 0)
		goto out;

	/* Set Voltage */
	ret = rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL17,
		reg_volt << RT9467_SHIFT_PUMPX_DEC, RT9467_MASK_PUMPX_DEC);
	if (ret < 0)
		goto out;

	/* Enable PumpX */
	ret = rt9467_enable_pump_express(info, true);

out:
	mutex_unlock(&info->pe_access_lock);
	return ret;
}

static int rt9467_enable_cable_drop_comp(struct charger_device *chg_dev,
	bool en)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "%s: en = %d\n", __func__, en);

	mutex_lock(&info->pe_access_lock);

	/* Set to PEP2.0 */
	ret = rt9467_set_bit(info, RT9467_REG_CHG_CTRL17,
		RT9467_MASK_PUMPX_20_10);
	if (ret < 0)
		goto out;

	/* Set Voltage */
	ret = rt9467_i2c_update_bits(info, RT9467_REG_CHG_CTRL17,
		0x1F << RT9467_SHIFT_PUMPX_DEC, RT9467_MASK_PUMPX_DEC);
	if (ret < 0)
		goto out;

	/* Enable PumpX */
	ret = rt9467_enable_pump_express(info, true);

out:
	mutex_unlock(&info->pe_access_lock);
	return ret;
}

static int rt9467_get_ichg(struct charger_device *chg_dev, u32 *ichg)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return __rt9467_get_ichg(info, ichg);
}

static int __rt9467_get_aicr(struct rt9467_info *info, u32 *aicr)
{
	int ret = 0;
	u8 reg_aicr = 0;

	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_CTRL3);
	if (ret < 0)
		return ret;

	reg_aicr = (ret & RT9467_MASK_AICR) >> RT9467_SHIFT_AICR;
	*aicr = rt9467_closest_value(RT9467_AICR_MIN, RT9467_AICR_MAX,
		RT9467_AICR_STEP, reg_aicr);

	return ret;
}

static int rt9467_get_aicr(struct charger_device *chg_dev, u32 *aicr)
{
	int ret = 0;
	u8 reg_aicr = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_CTRL3);
	if (ret < 0)
		return ret;

	reg_aicr = (ret & RT9467_MASK_AICR) >> RT9467_SHIFT_AICR;
	*aicr = rt9467_closest_value(RT9467_AICR_MIN, RT9467_AICR_MAX,
		RT9467_AICR_STEP, reg_aicr);

	return ret;
}

static int rt9467_get_cv(struct charger_device *chg_dev, u32 *cv)
{
	int ret = 0;
	u8 reg_cv = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = rt9467_i2c_read_byte(info, RT9467_REG_CHG_CTRL4);
	if (ret < 0)
		return ret;

	reg_cv = (ret & RT9467_MASK_CV) >> RT9467_SHIFT_CV;
	*cv = rt9467_closest_value(RT9467_CV_MIN, RT9467_CV_MAX,
		RT9467_CV_STEP, reg_cv);

	return ret;
}

static int rt9467_get_tchg(struct charger_device *chg_dev, int *tchg_min,
	int *tchg_max)
{
	int ret = 0, adc_temp = 0;
	u32 retry_cnt = 3;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	/* Get value from ADC */
	ret = rt9467_get_adc(info, RT9467_ADC_TEMP_JC, &adc_temp);
	if (ret < 0)
		return ret;

	/* Check unusual temperature */
	while (adc_temp >= 120 && retry_cnt > 0) {
		dev_notice(info->dev,
			   "%s: [WARNING] t = %d\n", __func__, adc_temp);
		rt9467_get_adc(info, RT9467_ADC_VBAT, &adc_temp);
		ret = rt9467_get_adc(info, RT9467_ADC_TEMP_JC, &adc_temp);
		retry_cnt--;
	}
	if (ret < 0)
		return ret;

	mutex_lock(&info->tchg_lock);
	/* Use previous one to prevent system from rebooting */
	if (adc_temp >= 120)
		adc_temp = info->tchg;
	else
		info->tchg = adc_temp;
	mutex_unlock(&info->tchg_lock);

	*tchg_min = adc_temp;
	*tchg_max = adc_temp;

	dev_info(info->dev, "%s: temperature = %d\n", __func__, adc_temp);
	return ret;
}

static int rt9467_get_mivr_state(struct charger_device *chg_dev, bool *in_loop)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return rt9467_i2c_test_bit(info, RT9467_REG_CHG_STATC,
				   RT9467_SHIFT_CHG_MIVR, in_loop);
}

#if 0
static int rt9467_get_ibat(struct charger_device *chg_dev, u32 *ibat)
{
	int ret = 0, adc_ibat = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	/* Get value from ADC */
	ret = rt9467_get_adc(info, RT9467_ADC_IBAT, &adc_ibat);
	if (ret < 0)
		return ret;

	*ibat = adc_ibat;

	dev_info(info->dev, "%s: ibat = %dmA\n", __func__, adc_ibat);
	return ret;
}
#endif

#if 0 /* Uncomment if you need this API */
static int rt9467_get_vbus(struct charger_device *chg_dev, u32 *vbus)
{
	int ret = 0, adc_vbus = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	/* Get value from ADC */
	ret = rt9467_get_adc(info, RT9467_ADC_VBUS_DIV2, &adc_vbus);
	if (ret < 0)
		return ret;

	*vbus = adc_vbus;

	dev_info(info->dev, "%s: vbus = %dmA\n", __func__, adc_vbus);
	return ret;
}
#endif

static int rt9467_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	int ret = 0;
	enum rt9467_charging_status chg_stat = RT9467_CHG_STATUS_READY;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = rt9467_get_charging_status(info, &chg_stat);

	/* Return is charging done or not */
	switch (chg_stat) {
	case RT9467_CHG_STATUS_READY:
	case RT9467_CHG_STATUS_PROGRESS:
	case RT9467_CHG_STATUS_FAULT:
		*done = false;
		break;
	case RT9467_CHG_STATUS_DONE:
		*done = true;
		break;
	default:
		*done = false;
		break;
	}

	return ret;
}

static int rt9467_is_safety_timer_enable(struct charger_device *chg_dev,
	bool *en)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return rt9467_i2c_test_bit(info, RT9467_REG_CHG_CTRL12,
		RT9467_SHIFT_TMR_EN, en);
}

static int __rt9467_kick_wdt(struct rt9467_info *info)
{
	enum rt9467_charging_status chg_status = RT9467_CHG_STATUS_READY;

	/* Any I2C communication can reset watchdog timer */
	return rt9467_get_charging_status(info, &chg_status);
}

static int rt9467_kick_wdt(struct charger_device *chg_dev)
{
	enum rt9467_charging_status chg_status = RT9467_CHG_STATUS_READY;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	/* Any I2C communication can reset watchdog timer */
	return rt9467_get_charging_status(info, &chg_status);
}

static int rt9467_set_pep20_efficiency_table(struct charger_device *chg_dev)
{
	int ret = 0;
	struct charger_manager *chg_mgr = NULL;

	chg_mgr = charger_dev_get_drvdata(chg_dev);
	if (!chg_mgr)
		return -EINVAL;
		
	chg_mgr->pe2.profile[0].vbat = 3400000;
	chg_mgr->pe2.profile[1].vbat = 3500000;
	chg_mgr->pe2.profile[2].vbat = 3600000;
	chg_mgr->pe2.profile[3].vbat = 3700000;
	chg_mgr->pe2.profile[4].vbat = 3800000;
	chg_mgr->pe2.profile[5].vbat = 3900000;
	chg_mgr->pe2.profile[6].vbat = 4000000;
	chg_mgr->pe2.profile[7].vbat = 4100000;
	chg_mgr->pe2.profile[8].vbat = 4200000;
	chg_mgr->pe2.profile[9].vbat = 4400000;

	chg_mgr->pe2.profile[0].vchr = 8000000;
	chg_mgr->pe2.profile[1].vchr = 8000000;
	chg_mgr->pe2.profile[2].vchr = 8000000;
	chg_mgr->pe2.profile[3].vchr = 8500000;
	chg_mgr->pe2.profile[4].vchr = 8500000;
	chg_mgr->pe2.profile[5].vchr = 8500000;
	chg_mgr->pe2.profile[6].vchr = 9000000;
	chg_mgr->pe2.profile[7].vchr = 9000000;
	chg_mgr->pe2.profile[8].vchr = 9000000;
	chg_mgr->pe2.profile[9].vchr = 9000000;

	return ret;
}

static int __rt9467_enable_auto_sensing(struct rt9467_info *info, bool en)
{
	int ret = 0;
	u8 auto_sense = 0;
	u8 *data = 0x00;

	/* enter hidden mode */
	ret = rt9467_device_write(info->client, 0x70,
		ARRAY_SIZE(rt9467_val_en_hidden_mode),
		rt9467_val_en_hidden_mode);
	if (ret < 0)
		return ret;

	ret = rt9467_device_read(info->client, RT9467_REG_CHG_HIDDEN_CTRL15, 1,
		&auto_sense);
	if (ret < 0) {
		dev_notice(info->dev, "%s: read auto sense fail\n", __func__);
		goto out;
	}

	if (en)
		auto_sense &= 0xFE; /* clear bit0 */
	else
		auto_sense |= 0x01; /* set bit0 */
	ret = rt9467_device_write(info->client, RT9467_REG_CHG_HIDDEN_CTRL15, 1,
		&auto_sense);
	if (ret < 0)
		dev_notice(info->dev, "%s: en = %d fail\n", __func__, en);

out:
	return rt9467_device_write(info->client, 0x70, 1, &data);
}

/*
 * This function is used in shutdown function
 * Use i2c smbus directly
 */
static int rt9467_sw_reset(struct rt9467_info *info)
{
	int ret = 0;
	u8 evt[RT9467_IRQIDX_MAX] = {0};

	/* Register 0x01 ~ 0x10 */
	u8 reg_data[] = {
		0x10, 0x03, 0x23, 0x3C, 0x67, 0x0B, 0x4C, 0xA1,
		0x3C, 0x58, 0x2C, 0x02, 0x52, 0x05, 0x00, 0x10
	};

	dev_info(info->dev, "%s\n", __func__);

	/* Disable auto sensing/Enable HZ,ship mode of secondary charger */
	if (strcmp(info->desc->chg_dev_name, "secondary_chg") == 0) {
		mutex_lock(&info->hidden_mode_lock);
		mutex_lock(&info->i2c_access_lock);
		__rt9467_enable_auto_sensing(info, false);
		mutex_unlock(&info->i2c_access_lock);
		mutex_unlock(&info->hidden_mode_lock);

		reg_data[0] = 0x14; /* HZ */
		reg_data[1] = 0x83; /* Shipping mode */
	}

	/* Mask all irq */
	mutex_lock(&info->i2c_access_lock);
	ret = rt9467_device_write(info->client, RT9467_REG_CHG_STATC_CTRL,
		ARRAY_SIZE(rt9467_irq_maskall), rt9467_irq_maskall);
	if (ret < 0)
		dev_notice(info->dev, "%s: mask all irq fail\n", __func__);

	/* Read all irq */
	ret = rt9467_device_read(info->client, RT9467_REG_CHG_STATC, 5, evt);
	if (ret < 0)
		dev_notice(info->dev, "%s: read evt1 fail(%d)\n", __func__,
			ret);

	ret = rt9467_device_read(info->client, RT9467_REG_DPDM_IRQ, 1, &evt[6]);
	if (ret < 0)
		dev_notice(info->dev, "%s: read evt2 fail(%d)\n", __func__,
			ret);

	/* Reset necessary registers */
	ret = rt9467_device_write(info->client, RT9467_REG_CHG_CTRL1,
		ARRAY_SIZE(reg_data), reg_data);
	if (ret < 0)
		dev_notice(info->dev, "%s: reset registers fail\n", __func__);
	mutex_unlock(&info->i2c_access_lock);

	return ret;
}

int rt9467_enable_shipmode(bool en)
{
	if(en)
		return rt9467_set_bit(rt9467, RT9467_REG_CHG_CTRL2, RT9467_MASK_SHIP_EN);
	else
		return rt9467_clr_bit(rt9467, RT9467_REG_CHG_CTRL2, RT9467_MASK_SHIP_EN);
}

static int rt9467_init_setting(struct rt9467_info *info)
{
	int ret = 0;
	struct rt9467_desc *desc = info->desc;
	u8 evt[RT9467_IRQIDX_MAX] = {0};

	dev_info(info->dev, "%s\n", __func__);

	/* disable USB charger type detection before reset IRQ */
	ret = rt9467_enable_chgdet_flow(info, false);
	if (ret < 0) {
		dev_notice(info->dev, "%s: disable usb chrdet fail\n",
			__func__);
		goto err;
	}

	ret = rt9467_clr_bit(info, RT9467_REG_CHG_DPDM1, 0x40);
	if (ret < 0) {
		dev_notice(info->dev, "%s: disable attach delay fail\n",
			__func__);
		goto err;
	}

	/* mask all irq */
	ret = rt9467_maskall_irq(info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: mask all irq fail\n", __func__);
		goto err;
	}

	/* clear event */
	ret = rt9467_i2c_block_read(info, RT9467_REG_CHG_STATC, ARRAY_SIZE(evt),
		evt);
	if (ret < 0) {
		dev_notice(info->dev, "%s: clr evt fail(%d)\n", __func__, ret);
		goto err;
	}

	ret = __rt9467_set_ichg(info, desc->ichg);
	if (ret < 0)
		dev_notice(info->dev, "%s: set ichg fail\n", __func__);

	ret = __rt9467_set_aicr(info, desc->aicr);
	if (ret < 0)
		dev_notice(info->dev, "%s: set aicr fail\n", __func__);

	ret = __rt9467_set_mivr(info, desc->mivr);
	if (ret < 0)
		dev_notice(info->dev, "%s: set mivr fail\n", __func__);

	ret = __rt9467_set_cv(info, desc->cv);
	if (ret < 0)
		dev_notice(info->dev, "%s: set cv fail\n", __func__);

	ret = __rt9467_set_ieoc(info, desc->ieoc);
	if (ret < 0)
		dev_notice(info->dev, "%s: set ieoc fail\n", __func__);

	ret = __rt9467_enable_te(info, desc->en_te);
	if (ret < 0)
		dev_notice(info->dev, "%s: set te fail\n", __func__);

	ret = rt9467_set_safety_timer(info, desc->safety_timer);
	if (ret < 0)
		dev_notice(info->dev, "%s: set fast timer fail\n", __func__);

	ret = __rt9467_enable_safety_timer(info, true);
	if (ret < 0)
		dev_notice(info->dev, "%s: enable chg timer fail\n", __func__);

	ret = rt9467_enable_wdt(info, desc->en_wdt);
	if (ret < 0)
		dev_notice(info->dev, "%s: set wdt fail\n", __func__);

	ret = rt9467_enable_jeita(info, desc->en_jeita);
	if (ret < 0)
		dev_notice(info->dev, "%s: disable jeita fail\n", __func__);

	ret = rt9467_enable_irq_pulse(info, desc->en_irq_pulse);
	if (ret < 0)
		dev_notice(info->dev, "%s: set irq pulse fail\n", __func__);

	/* set ircomp according to BIF */
	ret = rt9467_set_ircmp_resistor(info, desc->ircmp_resistor);
	if (ret < 0)
		dev_notice(info->dev, "%s: set ircmp resistor fail\n",
			__func__);

	ret = rt9467_set_ircmp_vclamp(info, desc->ircmp_vclamp);
	if (ret < 0)
		dev_notice(info->dev, "%s: set ircmp clamp fail\n", __func__);

	ret = rt9467_sw_workaround(info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: workaround fail\n", __func__);
		return ret;
	}

	/* Enable HZ mode of secondary charger */
	if (strcmp(info->desc->chg_dev_name, "secondary_chg") == 0) {
		ret = rt9467_enable_hz(info, true);
		if (ret < 0)
			dev_notice(info->dev, "%s: hz sec chg fail\n",
				__func__);
	}
#ifdef OPLUS_FEATURE_CHG_BASIC
	ret = rt9467_set_bit(info, RT9467_REG_CHG_CTRL19, 0x80);
	if (ret < 0) {
		dev_notice(info->dev, "%s: disable system reset fail\n",
			__func__);
		goto err;
	}
#endif /*OPLUS_FEATURE_CHG_BASIC*/
err:
	return ret;
}

static int rt9467_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "%s\n", __func__);

	/* Enable WDT */
	if (info->desc->en_wdt) {
		ret = rt9467_enable_wdt(info, true);
		if (ret < 0)
			dev_notice(info->dev, "%s: en wdt fail\n", __func__);
	}

	/* Enable charging */
	if (strcmp(info->desc->chg_dev_name, "primary_chg") == 0) {
		ret = rt9467_enable_charging(chg_dev, true);
		if (ret < 0)
			dev_notice(info->dev, "%s: en chg fail\n", __func__);
	}

	return ret;
}

static int rt9467_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_info(info->dev, "%s\n", __func__);

	/* Reset AICR limit */
	info->aicr_limit = -1;

	/* Disable charging */
	ret = rt9467_enable_charging(chg_dev, false);
	if (ret < 0) {
		dev_notice(info->dev, "%s: disable chg fail\n", __func__);
		return ret;
	}

	/* Disable WDT */
	ret = rt9467_enable_wdt(info, false);
	if (ret < 0)
		dev_notice(info->dev, "%s: disable wdt fail\n", __func__);

	/* enable HZ mode of secondary charger */
	if (strcmp(info->desc->chg_dev_name, "secondary_chg") == 0) {
		ret = rt9467_enable_hz(info, true);
		if (ret < 0)
			dev_notice(info->dev, "%s: en hz of sec chg fail\n",
				__func__);
	}

	return ret;
}

static int rt9467_is_chip_enabled(struct charger_device *chg_dev, bool *en)
{
	*en = true;
	return 0;
}

static int rt9467_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return __rt9467_is_charging_enable(info, en);
}

static int rt9467_get_min_ichg(struct charger_device *chg_dev, u32 *uA)
{
	int ret = 0;

	*uA = rt9467_closest_value(RT9467_ICHG_MIN, RT9467_ICHG_MAX,
		RT9467_ICHG_STEP, 0);

	return ret;
}

static int rt9467_run_aicl(struct charger_device *chg_dev, u32 *uA)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	ret = __rt9467_run_aicl(info);
	if (ret >= 0)
		*uA = info->aicr_limit;

	return ret;
}

static int __rt9467_dump_registers(struct rt9467_info *info)
{
	int i = 0, ret = 0;
	u32 ichg = 0, aicr = 0, mivr = 0, ieoc = 0;
	bool chg_en = 0;
	int adc_vsys = 0, adc_vbat = 0, adc_ibat = 0, adc_ibus = 0;
	enum rt9467_charging_status chg_status = RT9467_CHG_STATUS_READY;
	u8 chg_stat = 0, chg_ctrl[2] = {0};

	ret = __rt9467_get_ichg(info, &ichg);
	ret = __rt9467_get_aicr(info, &aicr);
	ret = __rt9467_get_mivr(info, &mivr);
	ret = __rt9467_is_charging_enable(info, &chg_en);
	ret = rt9467_get_ieoc(info, &ieoc);
	ret = rt9467_get_charging_status(info, &chg_status);
	ret = rt9467_get_adc(info, RT9467_ADC_VSYS, &adc_vsys);
	ret = rt9467_get_adc(info, RT9467_ADC_VBAT, &adc_vbat);
	ret = rt9467_get_adc(info, RT9467_ADC_IBAT, &adc_ibat);
	ret = rt9467_get_adc(info, RT9467_ADC_IBUS, &adc_ibus);
	chg_stat = rt9467_i2c_read_byte(info, RT9467_REG_CHG_STATC);
	ret = rt9467_i2c_block_read(info, RT9467_REG_CHG_CTRL1, 2, chg_ctrl);

	/* Charging fault, dump all registers' value */
	if (chg_status == RT9467_CHG_STATUS_FAULT) {
		for (i = 0; i < ARRAY_SIZE(rt9467_reg_addr); i++)
			ret = rt9467_i2c_read_byte(info, rt9467_reg_addr[i]);
	}

	dev_info(info->dev,
		"%s: ICHG=%dmA,AICR=%dmA,MIVR=%dmV,IEOC=%dmA,VSYS=%dmV,VBAT=%dmV,IBAT=%dmA,IBUS=%dmA"
		"CHG_EN=%d,CHG_STATUS=%s,CHG_STAT=0x%02X,CHG_CTRL1=0x%02X,CHG_CTRL2=0x%02X\n",
		__func__, ichg / 1000, aicr / 1000, mivr / 1000, ieoc / 1000, adc_vsys / 1000, adc_vbat / 1000, adc_ibat / 1000, adc_ibus / 1000,
		chg_en, rt9467_chg_status_name[chg_status], chg_stat, chg_ctrl[0], chg_ctrl[1]);

	return ret;

}

static int rt9467_dump_register(struct charger_device *chg_dev)
{
	int i = 0, ret = 0;
	u32 ichg = 0, aicr = 0, mivr = 0, ieoc = 0;
	bool chg_en = 0;
	int adc_vsys = 0, adc_vbat = 0, adc_ibat = 0, adc_ibus = 0;
	enum rt9467_charging_status chg_status = RT9467_CHG_STATUS_READY;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);
	u8 chg_stat = 0, chg_ctrl[2] = {0};

	ret = rt9467_get_ichg(chg_dev, &ichg);
	ret = rt9467_get_aicr(chg_dev, &aicr);
	ret = __rt9467_get_mivr(info, &mivr);
	ret = __rt9467_is_charging_enable(info, &chg_en);
	ret = rt9467_get_ieoc(info, &ieoc);
	ret = rt9467_get_charging_status(info, &chg_status);
	ret = rt9467_get_adc(info, RT9467_ADC_VSYS, &adc_vsys);
	ret = rt9467_get_adc(info, RT9467_ADC_VBAT, &adc_vbat);
	ret = rt9467_get_adc(info, RT9467_ADC_IBAT, &adc_ibat);
	ret = rt9467_get_adc(info, RT9467_ADC_IBUS, &adc_ibus);
	chg_stat = rt9467_i2c_read_byte(info, RT9467_REG_CHG_STATC);
	ret = rt9467_i2c_block_read(info, RT9467_REG_CHG_CTRL1, 2, chg_ctrl);

	/* Charging fault, dump all registers' value */
	if (chg_status == RT9467_CHG_STATUS_FAULT) {
		for (i = 0; i < ARRAY_SIZE(rt9467_reg_addr); i++)
			ret = rt9467_i2c_read_byte(info, rt9467_reg_addr[i]);
	}

	dev_info(info->dev,
		"%s: ICHG = %dmA, AICR = %dmA, MIVR = %dmV, IEOC = %dmA\n",
		__func__, ichg / 1000, aicr / 1000, mivr / 1000, ieoc / 1000);

	dev_info(info->dev,
		"%s: VSYS = %dmV, VBAT = %dmV, IBAT = %dmA, IBUS = %dmA\n",
		__func__, adc_vsys / 1000, adc_vbat / 1000, adc_ibat / 1000,
		adc_ibus / 1000);

	dev_info(info->dev,
		"%s: CHG_EN = %d, CHG_STATUS = %s, CHG_STAT = 0x%02X\n",
		__func__, chg_en, rt9467_chg_status_name[chg_status], chg_stat);

	dev_info(info->dev, "%s: CHG_CTRL1 = 0x%02X, CHG_CTRL2 = 0x%02X\n",
		__func__, chg_ctrl[0], chg_ctrl[1]);

	return ret;
}

static int rt9467_enable_te(struct charger_device *chg_dev, bool en)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	return __rt9467_enable_te(info, en);
}

static int rt9467_reset_eoc_state(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	/* Toggle EOC_RST */
	rt9467_enable_hidden_mode(info, true);
	ret = rt9467_set_bit(info, RT9467_REG_CHG_HIDDEN_CTRL1, 0x80);
	if (ret < 0) {
		dev_notice(info->dev, "%s: set eoc rst fail\n", __func__);
		goto out;
	}

	ret = rt9467_clr_bit(info, RT9467_REG_CHG_HIDDEN_CTRL1, 0x80);
	if (ret < 0)
		dev_notice(info->dev, "%s: clr eoc rst fail\n", __func__);
out:
	rt9467_enable_hidden_mode(info, false);
	return ret;
}

static int rt9467_do_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	switch (event) {
	case EVENT_EOC:
		//charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
		break;
	case EVENT_RECHARGE:
		//charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_RECHG);
		break;
	default:
		break;
	}
	return 0;
}

static int rt9467_get_ibus(struct charger_device *chg_dev,u32* ibus_val)
{
    int ret = 0;
    struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);
    ret = rt9467_get_adc(info,RT9467_ADC_IBUS,ibus_val);
    return ret;
}
static int rt9467_get_ibat(struct charger_device *chg_dev,u32* ibat_val)
{
    int ret = 0;
    struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);
    ret = rt9467_get_adc(info,RT9467_ADC_IBAT,ibat_val);
    return ret;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static int rt9467_send_hvdcp_pattern_ex(struct charger_device *chg_dev, bool up)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_notice(info->dev,"%s : %d\n", __func__, up);
	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_DPDM2,
		up ? 0x20 : 0x80, 0xE0);
}
#endif

static int rt9467_reset_hvdcp_ta_ex(struct charger_device *chg_dev)
{
	struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);

	dev_notice(info->dev,"%s\n", __func__);
	return rt9467_i2c_update_bits(info, RT9467_REG_CHG_DPDM2, 0x80, 0xE0);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static int rt9467_hvdcp_can_enable(struct charger_device *chg_dev,bool *en)
{
    struct rt9467_info *info = dev_get_drvdata(&chg_dev->dev);
	//dev_notice(info->dev,"%s\n", __func__);
    *en = info->hvdcp_can_enabled;
	return 0;
}
#endif

static struct charger_ops rt9467_chg_ops = {
	/* Normal charging */
	.plug_in = rt9467_plug_in,
	.plug_out = rt9467_plug_out,
	.dump_registers = rt9467_dump_register,
	.enable = rt9467_enable_charging,
	.is_enabled = rt9467_is_charging_enable,
	.is_chip_enabled = rt9467_is_chip_enabled,
	.get_charging_current = rt9467_get_ichg,
	.set_charging_current = rt9467_set_ichg,
	.get_input_current = rt9467_get_aicr,
	.set_input_current = rt9467_set_aicr,
	.get_constant_voltage = rt9467_get_cv,
	.set_constant_voltage = rt9467_set_cv,
	.kick_wdt = rt9467_kick_wdt,
	.get_mivr = rt9467_get_mivr,
	.set_mivr = rt9467_set_mivr,
	.get_mivr_state = rt9467_get_mivr_state,
	.is_charging_done = rt9467_is_charging_done,
	.get_min_charging_current = rt9467_get_min_ichg,
	.set_eoc_current = rt9467_set_ieoc,
	.enable_termination = rt9467_enable_te,
	.run_aicl = rt9467_run_aicl,
	.reset_eoc_state = rt9467_reset_eoc_state,

	/* Safety timer */
	.enable_safety_timer = rt9467_enable_safety_timer,
	.is_safety_timer_enabled = rt9467_is_safety_timer_enable,

	/* Power path */
	.enable_powerpath = rt9467_enable_power_path,
	.is_powerpath_enabled = rt9467_is_power_path_enable,

	/* Charger type detection */
	.enable_chg_type_det = rt9467_enable_chg_type_det,

	/* OTG */
	.enable_otg = rt9467_enable_otg,
	.set_boost_current_limit = rt9467_set_boost_current_limit,
	.enable_discharge = rt9467_enable_discharge,

	/* PE+/PE+20 */
	.send_ta_current_pattern = rt9467_set_pep_current_pattern,
	.set_pe20_efficiency_table = rt9467_set_pep20_efficiency_table,
	.send_ta20_current_pattern = rt9467_set_pep20_current_pattern,
	.reset_ta = rt9467_set_pep20_reset,
	.enable_cable_drop_comp = rt9467_enable_cable_drop_comp,

	/* ADC */
	.get_tchg_adc = rt9467_get_tchg,
    .get_ibus_adc = rt9467_get_ibus,
    .get_ibat_adc = rt9467_get_ibat,

	/* Event */
	.event = rt9467_do_event,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	/* HVDCP */
	.send_hvdcp_pattern_ex = rt9467_send_hvdcp_pattern_ex,
	.reset_hvdcp_ta_ex = rt9467_reset_hvdcp_ta_ex,
	.hvdcp_can_enabled = rt9467_hvdcp_can_enable,
#endif
};

void oplus_rt9467_dump_registers(void)
{
	__rt9467_dump_registers(rt9467);
}

int oplus_rt9467_kick_wdt(void)
{
	return __rt9467_kick_wdt(rt9467);
}

int oplus_rt9467_set_ichg(int cur)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	u32 uA = cur*1000;
	u32 temp_uA;
	int ret = 0;

	if(cur <= 1000){   // <= 1A
		ret = __rt9467_set_ichg(rt9467, uA);
		chip->sub_chg_ops->charging_current_write_fast(0);
	} else {
		temp_uA = uA  * PRIMARY_CHG_PERCENT / TOTAL_CHG_PERCENT;
		ret = __rt9467_set_ichg(rt9467, temp_uA);
		ret = __rt9467_get_ichg(rt9467, &temp_uA);
		uA-=temp_uA;
		chip->sub_chg_ops->charging_current_write_fast(uA/1000);
	}
	return ret;
}

void oplus_rt9467_set_mivr(int vbatt)
{

	u32 uV = vbatt*1000 + 200000;
#ifdef OPLUS_FEATURE_CHG_BASIC
    if(uV<4200000)
        uV = 4200000;
#endif	
	__rt9467_set_mivr(rt9467, uV);
}
static int usb_icl[] = {
	300, 500, 900, 1200, 1500, 1750, 2000, 3000,
};
int oplus_rt9467_set_aicr(int current_ma)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int aicl_point_temp = 0;
	
	if (rt9467->desc->pre_current_ma == current_ma)
		return rc;
	else
		rt9467->desc->pre_current_ma = current_ma;

	if(chip->is_double_charger_support)
		chip->sub_chg_ops->input_current_write(0);
		
	dev_info(rt9467->dev, "%s usb input max current limit=%d\n", __func__,current_ma);
	aicl_point_temp = aicl_point = 4500;
	__rt9467_set_mivr(rt9467, 4200000);
	
	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}
	
	i = 1; /* 500 */
	__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	msleep(90);
	
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		chg_debug( "use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1500 */
	aicl_point_temp = aicl_point + 50;
	__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	msleep(120);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 2; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 1; //We use 1.2A here
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 5; /* 1750 */
	aicl_point_temp = aicl_point + 50;
	__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	msleep(120);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 2; //1.2
		goto aicl_pre_step;
	}

	i = 6; /* 2000 */
	aicl_point_temp = aicl_point;
	__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	msleep(90);
	if (chg_vol < aicl_point_temp) {
		i =  i - 2;//1.5
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 7; /* 3000 */
	__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_get_vbus();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	if(chip->is_double_charger_support){
		if(usb_icl[i]>1000){
			__rt9467_set_aicr(rt9467, usb_icl[i] * 1000*70/100);
			chip->sub_chg_ops->input_current_write(usb_icl[i]*30/100);
		}else{
			__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
		}
	}else{
		__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	}
	dev_info(rt9467->dev, "%s:usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n",__func__, chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
aicl_end:
	if(chip->is_double_charger_support){
		if(usb_icl[i]>1000){
			__rt9467_set_aicr(rt9467, usb_icl[i] * 1000*70/100);
			chip->sub_chg_ops->input_current_write(usb_icl[i]*30/100);
		}else{
			__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
		}
	}else{
		__rt9467_set_aicr(rt9467, usb_icl[i] * 1000);
	}
	dev_info(rt9467->dev, "%s:usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n",__func__, chg_vol, i, usb_icl[i], aicl_point_temp);
	return rc;
}

int oplus_rt9467_set_cv(int cur)
{
	u32 uV = cur*1000;
	return __rt9467_set_cv(rt9467, uV);
}

int oplus_rt9467_set_ieoc(int cur)
{
	u32 uA = cur*1000;
	return __rt9467_set_ieoc(rt9467, uA);
}

int oplus_rt9467_charging_enable(void)
{
	return __rt9467_enable_charging(rt9467, true);
}

int oplus_rt9467_charging_disable(void)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct charger_manager *info = NULL;
	
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}
	
	mtk_pdc_plugout(info);
	if(rt9467->hvdcp_can_enabled){
		rt9467_i2c_update_bits(rt9467, RT9467_REG_CHG_DPDM2, 0x80, 0xE0);
		dev_info(rt9467->dev, "%s: set qc to 5V", __func__);
	}

	rt9467_enable_wdt(rt9467, false);
#endif
	rt9467->desc->pre_current_ma = -1;
	return __rt9467_enable_charging(rt9467, false);
}

int oplus_rt9467_hardware_init(void)
{
	int ret = 0;

	dev_info(rt9467->dev, "%s\n", __func__);

	/* Enable WDT */
	if (rt9467->desc->en_wdt) {
		ret = rt9467_enable_wdt(rt9467, true);
		if (ret < 0)
			dev_notice(rt9467->dev, "%s: en wdt fail\n", __func__);
	}

	/* Enable charging */
	if (strcmp(rt9467->desc->chg_dev_name, "primary_chg") == 0) {
		ret = __rt9467_enable_charging(rt9467, true);
		if (ret < 0)
			dev_notice(rt9467->dev, "%s: en chg fail\n", __func__);
	}

	return ret;

}

int oplus_rt9467_is_charging_enabled(void)
{
	bool en;
	__rt9467_is_charging_enable(rt9467, &en);
	return en;
}

int oplus_rt9467_is_charging_done(void)
{
	int ret = 0;
	enum rt9467_charging_status chg_stat = RT9467_CHG_STATUS_READY;

	rt9467_get_charging_status(rt9467, &chg_stat);

	/* Return is charging done or not */
	switch (chg_stat) {
	case RT9467_CHG_STATUS_READY:
	case RT9467_CHG_STATUS_PROGRESS:
	case RT9467_CHG_STATUS_FAULT:
		ret = false;
		break;
	case RT9467_CHG_STATUS_DONE:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;

}

int oplus_rt9467_enable_otg(void)
{
	int ret = 0;
	ret = __rt9467_enable_otg(rt9467, true);

	if (ret < 0) {
		dev_notice(rt9467->dev, "%s en otg fail(%d)\n", __func__, ret);
		return ret;
	}

	return ret;
}

int oplus_rt9467_disable_otg(void)
{
	int ret = 0;
	ret = __rt9467_enable_otg(rt9467, false);

	if (ret < 0) {
		dev_notice(rt9467->dev, "%s disable otg fail(%d)\n", __func__, ret);
		return ret;
	}

	return ret;

}

int oplus_rt9467_disable_te(void)
{
	return __rt9467_enable_te(rt9467, false);
}

int oplus_rt9467_get_chg_current_step(void)
{
	return RT9467_ICHG_STEP;
}

int oplus_rt9467_get_charger_type(void)
{
	return charger_type;
}

int oplus_rt9467_charger_suspend(void)
{
	return 0;
}

int oplus_rt9467_charger_unsuspend(void)
{
	return 0;
}

int oplus_rt9467_set_rechg_vol(int vol)
{
	return 0;
}

int oplus_rt9467_reset_charger(void)
{
	return 0;
}

int oplus_rt9467_set_hz_mode(bool en)
{
	return rt9467_enable_hz(rt9467,en);
}

bool oplus_rt9467_check_charger_resume(void)
{
	return true;
}

void oplus_rt9467_set_chargerid_switch_val(int value)
{
	return;
}

int oplus_rt9467_get_chargerid_switch_val(void)
{
	return 0;
}

int oplus_rt9467_get_charger_subtype(void)
{
	struct charger_manager *info = NULL;
		
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}
	
	if(mtk_pdc_check_charger(info)&&(!disable_PD)){
		return CHARGER_SUBTYPE_PD;
	}else if(rt9467->hvdcp_can_enabled){
		return CHARGER_SUBTYPE_QC;
	}else if(mtk_pe20_get_is_connect(info)){
		return CHARGER_SUBTYPE_PE20;
	}else{
		return CHARGER_SUBTYPE_DEFAULT;
	}
}

bool oplus_rt9467_need_to_check_ibatt(void)
{
	return false;
}

int oplus_rt9467_get_dyna_aicl_result(void)
{
	u32 uA;
	int mA = 0;

	__rt9467_get_aicr(rt9467, &uA);
	mA = (int)uA/1000;
	return mA;
}

int oplus_rt9467_set_qc_config(void)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct charger_manager *info = NULL;
		
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}
	
	if (!chip) {
		dev_info(rt9467->dev, "%s: error\n", __func__);
		return false;
	}

	if(disable_QC){
		dev_info(rt9467->dev, "%s:disable_QC\n", __func__);
		return false;
	}
	
	if(mtk_pe20_get_is_connect(info)&&mtk_pe20_get_is_enable(info)){
		mtk_pe20_reset_ta_vchr(info);
		mtk_pe20_set_is_enable(info, false);
	}

	if(rt9467->disable_hight_vbus==1){
		dev_info(rt9467->dev, "%s:disable_hight_vbus\n", __func__);
		return false;
	}

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500
		&& chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr&&chip->ui_soc>=85&&chip->icharging > -1000) {
		rt9467_i2c_update_bits(rt9467, RT9467_REG_CHG_DPDM2, 0x80, 0xE0);
		rt9467->pdqc_setup_5v = 1;
		dev_info(rt9467->dev, "%s: set qc to 5V", __func__);
	} else { // 9v
		dev_info(rt9467->dev, "%s:qc Force output 9V\n",__func__);
		rt9467_i2c_update_bits(rt9467, RT9467_REG_CHG_DPDM2,0x20, 0xE0);
		rt9467->pdqc_setup_5v = 0;
	}

	return true;
#endif
}

int oplus_rt9467_enable_qc_detect(void)
{
	return 0;
}

bool oplus_rt9467_get_shortc_hw_gpio_status(void)
{
	return false;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
int oplus_rt9467_chg_get_pe20_type(void)
{
	struct charger_manager *info = NULL;
	
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}

	if(disable_PE){
		dev_info(rt9467->dev, "%s:disable_PE\n", __func__);
		return false;
	}
	info->enable_hv_charging = true;
	info->enable_pe_2  = true;
	info->data.ta_stop_battery_soc = 90;
	mtk_pe20_set_is_enable(info,true);
	mtk_pe20_set_to_check_chr_type(info, true);
	mtk_pe20_set_is_cable_out_occur(info, false);
	mtk_pe20_check_charger(info);
	if( mtk_pe20_get_is_connect(info))
     	return true;

	return false;
}
EXPORT_SYMBOL(oplus_rt9467_chg_get_pe20_type);

int oplus_rt9467_chg_pe20_setup(void)
{
	struct charger_manager *info = NULL;
	
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}

	if(disable_PE){
		dev_info(rt9467->dev, "%s:disable_PE\n", __func__);
		return false;
	}

	if(rt9467->disable_hight_vbus==1){
		dev_info(rt9467->dev, "%s:disable_hight_vbus\n", __func__);
		return false;
	}
	
 	mtk_pe20_start_algorithm(info);
	return true;
}
EXPORT_SYMBOL(oplus_rt9467_chg_pe20_setup);

int oplus_rt9467_chg_reset_pe20(void)
{
	struct charger_manager *info = NULL;
	
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}
	
	if(disable_PE){
		dev_info(rt9467->dev, "%s:disable_PE\n", __func__);
		return false;
	}

	mtk_pe20_reset_ta_vchr(info);
	mtk_pe20_set_is_cable_out_occur(info, true);
	mtk_pe20_set_is_enable(info, false);
	return true;
}
EXPORT_SYMBOL(oplus_rt9467_chg_reset_pe20);
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
int oplus_rt9467_get_pd_type(void)
{
	struct charger_manager *info = NULL;
	
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}
	
	if(disable_PD){
		dev_info(rt9467->dev, "%s:disable_PD\n", __func__);
		return false;
	}
	
	info->enable_pe_4 = false;
	if(mtk_pdc_check_charger(info))
	{
		return true;
	}
	
	return false;
	
}

#define VBUS_VOL_5V	5000
#define VBUS_VOL_9V	9000
#define IBUS_VOL_2A	2000
#define IBUS_VOL_3A	3000
int oplus_rt9467_pd_setup (void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct charger_manager *info = NULL;
	int vbus = 0, ibus = 0;
		
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}
	
	if (!chip) {
		dev_info(rt9467->dev, "%s: error\n", __func__);
		return false;
	}

	if(disable_PD){
		dev_info(rt9467->dev, "%s:disable_PD\n", __func__);
		return false;
	}
	
	if(rt9467->disable_hight_vbus==1){
		dev_info(rt9467->dev, "%s:disable_hight_vbus\n", __func__);
		return false;
	}

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > 7500
		&& chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr&&chip->ui_soc>=85&&chip->icharging > -1000) {
		dev_info(rt9467->dev, "%s: pd set qc to 5V", __func__);
		vbus = VBUS_VOL_5V;
		ibus = IBUS_VOL_3A;
		oplus_pdc_setup(&vbus, &ibus);
		rt9467->pdqc_setup_5v = 1;
	}else{
		dev_info(rt9467->dev, "%s:pd Force output 9V\n",__func__);
		vbus = VBUS_VOL_9V;
		ibus = IBUS_VOL_2A;
		oplus_pdc_setup(&vbus, &ibus);
		rt9467->pdqc_setup_5v = 0;
	}
	
	return true;
}
int oplus_rt9467_chg_set_high_vbus(bool en)
{
	int subtype;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct charger_manager *info = NULL;
	int vbus = 0, ibus = 0;
		
	if(rt9467->chg_consumer != NULL)
		info = rt9467->chg_consumer->cm;

	if(!info){
		dev_info(rt9467->dev, "%s:error\n", __func__);
		return false;
	}
	
	if (!chip) {
		dev_info(rt9467->dev, "%s: error\n", __func__);
		return false;
	}

	if(en){
		rt9467->disable_hight_vbus= 0;
		if(chip->charger_volt >7500){
			dev_info(rt9467->dev, "%s:charger_volt already 9v\n", __func__);
			return false;
		}

		if(rt9467->pdqc_setup_5v){
			dev_info(rt9467->dev, "%s:pdqc already setup5v no need 9v\n", __func__);
			return false;
		}

	}else{
		rt9467->disable_hight_vbus= 1;
		if(chip->charger_volt < 5400){
			dev_info(rt9467->dev, "%s:charger_volt already 5v\n", __func__);
			return false;
		}
	}
	
	subtype=oplus_rt9467_get_charger_subtype();
	if(subtype==CHARGER_SUBTYPE_PD){
	  if(en){
	  	dev_info(rt9467->dev, "%s:pd Force output 9V\n",__func__);
		vbus = VBUS_VOL_9V;
		ibus = IBUS_VOL_2A;
		oplus_pdc_setup(&vbus, &ibus);
	  }else{
		vbus = VBUS_VOL_5V;
		ibus = IBUS_VOL_3A;
		oplus_pdc_setup(&vbus, &ibus);
		dev_info(rt9467->dev, "%s: set pd to 5V", __func__);
	  }
	}else if(subtype==CHARGER_SUBTYPE_QC){
	  if(en){
	  	dev_info(rt9467->dev, "%s:QC Force output 9V\n",__func__);
		rt9467_i2c_update_bits(rt9467, RT9467_REG_CHG_DPDM2,0x20, 0xE0);
	  }else{
		rt9467_i2c_update_bits(rt9467, RT9467_REG_CHG_DPDM2, 0x80, 0xE0);
		dev_info(rt9467->dev, "%s: set qc to 5V", __func__);
	  }
	
	}else if(subtype==CHARGER_SUBTYPE_PE20){
	  if(!en){
	  	dev_info(rt9467->dev, "%s: set pe20 to 5V", __func__);
		mtk_pe20_reset_ta_vchr(info);
	  }
	}else{
		dev_info(rt9467->dev, "%s:do nothing\n", __func__);
	}

	return false;
}

#endif

struct oplus_chg_operations  oplus_chg_rt9467_ops = {
	.dump_registers = oplus_rt9467_dump_registers,
	.kick_wdt = oplus_rt9467_kick_wdt,
	.hardware_init = oplus_rt9467_hardware_init,
	.charging_current_write_fast = oplus_rt9467_set_ichg,
	.set_aicl_point = oplus_rt9467_set_mivr,
	.input_current_write = oplus_rt9467_set_aicr,
	.float_voltage_write = oplus_rt9467_set_cv,
	.term_current_set = oplus_rt9467_set_ieoc,
	.charging_enable = oplus_rt9467_charging_enable,
	.charging_disable = oplus_rt9467_charging_disable,
	.get_charging_enable = oplus_rt9467_is_charging_enabled,
	.charger_suspend = oplus_rt9467_charger_suspend,
	.charger_unsuspend = oplus_rt9467_charger_unsuspend,
	.set_rechg_vol = oplus_rt9467_set_rechg_vol,
	.reset_charger = oplus_rt9467_reset_charger,
	.read_full = oplus_rt9467_is_charging_done,
	.otg_enable = oplus_rt9467_enable_otg,
	.otg_disable = oplus_rt9467_disable_otg,
	.set_charging_term_disable = oplus_rt9467_disable_te,
	.check_charger_resume = oplus_rt9467_check_charger_resume,

	.get_charger_type = oplus_rt9467_get_charger_type,
	.get_charger_volt = battery_get_vbus,
//	int (*get_charger_current)(void);
	.get_chargerid_volt = NULL,
    .set_chargerid_switch_val = oplus_rt9467_set_chargerid_switch_val,
    .get_chargerid_switch_val = oplus_rt9467_get_chargerid_switch_val,
	.check_chrdet_status = (bool (*) (void)) pmic_chrdet_status,

	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = oplus_get_rtc_ui_soc,
	.set_rtc_soc = oplus_set_rtc_ui_soc,
	.set_power_off = mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
    .get_chg_current_step = oplus_rt9467_get_chg_current_step,
    .need_to_check_ibatt = oplus_rt9467_need_to_check_ibatt,
    .get_dyna_aicl_result = oplus_rt9467_get_dyna_aicl_result,
    .get_shortc_hw_gpio_status = oplus_rt9467_get_shortc_hw_gpio_status,
//	void (*check_is_iindpm_mode) (void);
    .oplus_chg_get_pd_type = oplus_rt9467_get_pd_type,
    .oplus_chg_pd_setup = oplus_rt9467_pd_setup,
	.get_charger_subtype = oplus_rt9467_get_charger_subtype,
	.set_qc_config = oplus_rt9467_set_qc_config,
	.enable_qc_detect = oplus_rt9467_enable_qc_detect,
//#ifdef OPLUS_FEATURE_CHG_BASIC
//	.oplus_chg_get_pe20_type = oplus_rt9467_chg_get_pe20_type,
//	.oplus_chg_pe20_setup = oplus_rt9467_chg_pe20_setup,
//	.oplus_chg_reset_pe20 = oplus_rt9467_chg_reset_pe20,
	.oplus_chg_set_high_vbus = oplus_rt9467_chg_set_high_vbus,
//#endif
	.oplus_chg_set_hz_mode = oplus_rt9467_set_hz_mode,
	
};

static ssize_t shipping_mode_9467_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret = 0, tmp = 0;
	//struct rt9471_chip *chip = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &tmp);
	if (ret < 0) {
		dev_notice(dev, "%s parsing number fail(%d)\n", __func__, ret);
		return -EINVAL;
	}
	if (tmp != 5526789)
		return -EINVAL;

	ret = rt9467_enable_shipmode(true);
	if (ret < 0) {
		dev_notice(dev, "%s enter shipping mode fail(%d)\n",
				__func__, ret);
		return ret;
	}

	return count;
}

static const DEVICE_ATTR_WO(shipping_mode_9467);

static void rt9467_init_setting_work_handler(struct work_struct *work)
{
	int ret = 0, retry_cnt = 0;
	struct rt9467_info *info = (struct rt9467_info *)container_of(work,
		struct rt9467_info, init_work);

	do {
		/* Select IINLMTSEL to use AICR */
		ret = rt9467_select_input_current_limit(info,
			RT9467_IINLMTSEL_AICR);
		if (ret < 0) {
			dev_notice(info->dev, "%s: sel ilmtsel fail\n",
				__func__);
			retry_cnt++;
		}
	} while (retry_cnt < 5 && ret < 0);

	msleep(150);

	retry_cnt = 0;
	do {
		/* Disable hardware ILIM */
		ret = rt9467_enable_ilim(info, false);
		if (ret < 0) {
			dev_notice(info->dev, "%s: disable ilim fail\n",
				__func__);
			retry_cnt++;
		}
	} while (retry_cnt < 5 && ret < 0);

	rt9467_dump_register(info->chg_dev);

	/* Schedule work for microB's BC1.2 */
#ifndef CONFIG_TCPC_CLASS
	if (info->desc->en_chgdet)
		schedule_work(&info->chgdet_work);
#endif /* CONFIG_TCPC_CLASS */
}

/* ========================= */
/* I2C driver function       */
/* ========================= */

static int rt9467_probe(struct i2c_client *client,
	const struct i2c_device_id *dev_id)
{
	int ret = 0;
	struct rt9467_info *info = NULL;

	pr_info("%s(%s)\n", __func__, RT9467_DRV_VERSION);

	info = devm_kzalloc(&client->dev, sizeof(struct rt9467_info),
		GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	rt9467 = info;
#ifdef OPLUS_FEATURE_CHG_BASIC
	info->chg_consumer =
		charger_manager_get_by_name(&client->dev, "rt9467");
#endif
	mutex_init(&info->i2c_access_lock);
	mutex_init(&info->adc_access_lock);
	mutex_init(&info->irq_access_lock);
	mutex_init(&info->aicr_access_lock);
	mutex_init(&info->ichg_access_lock);
	mutex_init(&info->hidden_mode_lock);
	mutex_init(&info->pe_access_lock);
	mutex_init(&info->bc12_access_lock);
	mutex_init(&info->ieoc_lock);
	mutex_init(&info->tchg_lock);
	mutex_init(&info->otg_access_lock);
	atomic_set(&info->bc12_sdp_cnt, 0);
	atomic_set(&info->hvdcp_en_cnt, 0);
	atomic_set(&info->bc12_wkard, 0);
	info->otg_ocp_cnt = 0;
#ifdef CONFIG_TCPC_CLASS
	atomic_set(&info->tcpc_usb_connected, 0);
#endif /* CONFIG_TCPC_CLASS */

	info->client = client;
	info->dev = &client->dev;
	info->aicr_limit = -1;
	info->hidden_mode_cnt = 0;
	info->bc12_en = true;
	info->ieoc_wkard = false;
	info->ieoc = 250000; /* register default value 250mA */
	info->ichg = 2000000; /* register default value 2000mA */
	info->tchg = 25;
    info->hvdcp_can_enabled=0;
	info->disable_hight_vbus = 0;
	info->pdqc_setup_5v = 0;

	memcpy(info->irq_mask, rt9467_irq_maskall, RT9467_IRQIDX_MAX);

	/* Init wait queue head */
	init_waitqueue_head(&info->wait_queue);

	INIT_WORK(&info->init_work, rt9467_init_setting_work_handler);
#ifndef CONFIG_TCPC_CLASS
	INIT_WORK(&info->chgdet_work, rt9467_chgdet_work_handler);
#endif /* CONFIG_TCPC_CLASS */

	/* Is HW exist */
	if (!rt9467_is_hw_exist(info)) {
		dev_notice(info->dev, "%s: no rt9467 exists\n", __func__);
		ret = -ENODEV;
		goto err_no_dev;
	}
	i2c_set_clientdata(client, info);

	ret = rt9467_parse_dt(info, &client->dev);
	if (ret < 0) {
		dev_notice(info->dev, "%s: parse dt fail\n", __func__);
		goto err_parse_dt;
	}

#if 0
//#ifdef CONFIG_RT_REGMAP
	ret = rt9467_register_rt_regmap(info);
	if (ret < 0)
		goto err_register_regmap;
#endif /* CONFIG_RT_REGMAP */

#if 0 /* If you need ext usbsw, uncomment this part */
	info->usb_switch = switch_dev_get_by_name("usb_switch");
	if (!info->usb_switch)
		dev_notice(info->dev, "%s: get usb switch fail\n", __func__);
#endif

	ret = rt9467_reset_chip(info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: reset chip fail\n", __func__);
		goto err_reset_chip;
	}

	ret = rt9467_init_setting(info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: init setting fail\n", __func__);
		goto err_init_setting;
	}

	/* Register charger device */
	info->chg_dev = charger_device_register(
		info->desc->chg_dev_name, info->dev, info, &rt9467_chg_ops,
		&info->chg_props);
	if (IS_ERR_OR_NULL(info->chg_dev)) {
		ret = PTR_ERR(info->chg_dev);
		goto err_register_chg_dev;
	}

	ret = rt9467_irq_register(info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: irq register fail\n", __func__);
		goto err_irq_register;
	}

	ret = rt9467_irq_init(info);
	if (ret < 0) {
		dev_notice(info->dev, "%s: irq init fail\n", __func__);
		goto err_irq_init;
	}

	ret = device_create_file(info->dev, &dev_attr_shipping_mode_9467);
	if (ret < 0) {
		dev_notice(info->dev, "%s create file fail(%d)\n",
				      __func__, ret);
	}
	
	schedule_work(&info->init_work);
	dev_info(info->dev, "%s: successfully\n", __func__);
	set_charger_ic(RT9467);
	return ret;

err_irq_init:
err_irq_register:
	charger_device_unregister(info->chg_dev);
err_register_chg_dev:
err_init_setting:
err_reset_chip:
#if 0
//#ifdef CONFIG_RT_REGMAP
	rt_regmap_device_unregister(info->regmap_dev);
err_register_regmap:
#endif
err_parse_dt:
err_no_dev:
	mutex_destroy(&info->i2c_access_lock);
	mutex_destroy(&info->adc_access_lock);
	mutex_destroy(&info->irq_access_lock);
	mutex_destroy(&info->aicr_access_lock);
	mutex_destroy(&info->ichg_access_lock);
	mutex_destroy(&info->hidden_mode_lock);
	mutex_destroy(&info->pe_access_lock);
	mutex_destroy(&info->bc12_access_lock);
	mutex_destroy(&info->ieoc_lock);
	mutex_destroy(&info->tchg_lock);
	mutex_destroy(&info->otg_access_lock);
	return ret;
}


static int rt9467_remove(struct i2c_client *client)
{
	int ret = 0;
	struct rt9467_info *info = i2c_get_clientdata(client);

	pr_info("%s\n", __func__);

	if (info) {
#if 0
//#ifdef CONFIG_RT_REGMAP
		rt_regmap_device_unregister(info->regmap_dev);
#endif
		mutex_destroy(&info->i2c_access_lock);
		mutex_destroy(&info->adc_access_lock);
		mutex_destroy(&info->irq_access_lock);
		mutex_destroy(&info->aicr_access_lock);
		mutex_destroy(&info->ichg_access_lock);
		mutex_destroy(&info->hidden_mode_lock);
		mutex_destroy(&info->pe_access_lock);
		mutex_destroy(&info->bc12_access_lock);
		mutex_destroy(&info->ieoc_lock);
		mutex_destroy(&info->tchg_lock);
		mutex_destroy(&info->otg_access_lock);
	}
	device_remove_file(info->dev, &dev_attr_shipping_mode_9467);

	return ret;
}

static void rt9467_shutdown(struct i2c_client *client)
{
	int ret = 0;
	struct rt9467_info *info = i2c_get_clientdata(client);

	pr_info("%s\n", __func__);
	if (info) {
		ret = rt9467_sw_reset(info);
		if (ret < 0)
			pr_notice("%s: sw reset fail\n", __func__);
	}
}

static int rt9467_suspend(struct device *dev)
{
	struct rt9467_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);
	if (device_may_wakeup(dev))
		enable_irq_wake(info->irq);

	return 0;
}

static int rt9467_resume(struct device *dev)
{
	struct rt9467_info *info = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);
	if (device_may_wakeup(dev))
		disable_irq_wake(info->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rt9467_pm_ops, rt9467_suspend, rt9467_resume);

static const struct i2c_device_id rt9467_i2c_id[] = {
	{"rt9467", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt9467_i2c_id);

static const struct of_device_id rt9467_of_match[] = {
	{ .compatible = "richtek,rt9467", },
	{},
};
MODULE_DEVICE_TABLE(of, rt9467_of_match);

#ifndef CONFIG_OF
#define RT9467_BUSNUM 1

static struct i2c_board_info rt9467_i2c_board_info __initdata = {
	I2C_BOARD_INFO("rt9467", RT9467_SALVE_ADDR)
};
#endif /* CONFIG_OF */


static struct i2c_driver rt9467_i2c_driver = {
	.driver = {
		.name = "rt9467",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rt9467_of_match),
		.pm = &rt9467_pm_ops,
	},
	.probe = rt9467_probe,
	.remove = rt9467_remove,
	.shutdown = rt9467_shutdown,
	.id_table = rt9467_i2c_id,
};

static int __init rt9467_init(void)
{
	int ret = 0;

#ifdef CONFIG_OF
	pr_info("%s: with dts\n", __func__);
#else
	pr_info("%s: without dts\n", __func__);
	i2c_register_board_info(RT9467_BUSNUM, &rt9467_i2c_board_info, 1);
#endif

	ret = i2c_add_driver(&rt9467_i2c_driver);
	if (ret < 0)
		pr_notice("%s: register i2c driver fail\n", __func__);

	return ret;
}
module_init(rt9467_init);


static void __exit rt9467_exit(void)
{
	i2c_del_driver(&rt9467_i2c_driver);
}
module_exit(rt9467_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("ShuFanLee <shufan_lee@richtek.com>");
MODULE_DESCRIPTION("RT9467 Charger Driver");
MODULE_VERSION(RT9467_DRV_VERSION);

/*
 * Release Note
 * 1.0.18
 * (1) Check tchg 3 times if it >= 120 degree
 *
 * 1.0.17
 * (1) Add ichg workaround
 *
 * 1.0.16
 * (1) Fix type error of enable_auto_sensing in sw_reset
 * (2) Move irq_mask to info structure
 * (3) Remove config of Charger_Detect_Init/Release
 *
 * 1.0.15
 * (1) Do ilim select in WQ and register charger class in probe
 *
 * 1.0.14
 * (1) Disable attach delay
 * (2) Enable IRQ_RZE at the end of irq handler
 * (3) Remove IRQ related registers from reg_addr
 * (4) Recheck status in irq handler
 * (5) Use bc12_access_lock instead of chgdet_lock
 *
 * 1.0.13
 * (1) Add do event interface for polling mode
 * (2) Check INT pin after reading evt
 *
 * 1.0.12
 * (1) Add MTK_SSUSB config for Charger_Detect_Init/Release
 *
 * 1.0.11
 * (1) Disable psk mode in pep20_reest
 * (2) For secondary chg, enter shipping mode before shdn
 * (3) Add BC12 sdp workaround
 * (4) Remove enabling/disabling ILIM in chgdet_handler
 *
 * 1.0.10
 * (1) Add IEOC workaround
 * (2) Release set_ieoc/enable_te interface
 * (3) Fix type errors
 *
 * 1.0.9
 * (1) Add USB workaround for CDP port
 * (2) Plug in -> usbsw to charger, after chgdet usbsw to AP
 *     Plug out -> usbsw to AP
 * (3) Filter out not changed irq state
 * (4) Not to use CHG_IRQ3
 *
 * 1.0.8
 * (1) Set irq to wake up system
 * (2) Refine I2C driver related table
 *
 * 1.0.7
 * (1) Enable/Disable ILIM in chgdet_handler
 *
 * 1.0.6
 * (1) Prevent backboot
 * (2) Add CEB pin control for secondary charger
 * (3) After PE pattern -> Enable skip mode
 *     Disable skip mode -> Start PE pattern
 * (4) Disable BC12 detection before reset IRQ in init_setting
 *
 * 1.0.5
 * (1) Remove wait CDP flow
 * (2) Add rt9467_chgdet_handler for attachi/detachi
 * (3) Set secondary chg to HZ if it is not in charging mode
 * (4) Add is_charging_enabled, get_min_ichg OPS
 *
 * 1.0.4
 * (1) Set ichg&aicr, enable chg before sending PE+ series pattern
 * (2) Add enable_cable_drop_com OPS
 *
 * 1.0.3
 * (1) IRQs are default unmasked before E4, need to mask them manually
 *
 * 1.0.2
 * (1) Fix AICL workqueue lock issue
 *
 * 1.0.1
 * (1) Fix IRQ init sequence
 *
 * 1.0.0
 * (1) Initial released
 */
