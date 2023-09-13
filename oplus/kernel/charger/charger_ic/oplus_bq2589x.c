// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */


#define pr_fmt(fmt)	"[bq2589x]:%s: " fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/reboot.h>
#include <mt-plat/upmu_common.h>
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
#include <mt-plat/charger_class.h>
#include <mt-plat/charger_type.h>
#endif /* CONFIG_OPLUS_CHARGER_MTK6765R */
#include <mt-plat/mtk_boot.h>

#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include <charger_class.h>
#include <mtk_pd.h>
#define _BQ25890H_
#include "oplus_bq2589x_reg.h"
#include <linux/time.h>
#include <mtk_musb.h>
#ifdef OPLUS_FEATURE_CHG_BASIC
#include <soc/oplus/system/oplus_project.h>
#endif

extern void set_charger_ic(int sel);
extern struct charger_consumer *charger_manager_get_by_name(
		struct device *dev,	const char *name);
extern int mt6357_get_vbus_voltage(void);
extern bool mt6357_get_vbus_status(void);
extern int oplus_battery_meter_get_battery_voltage(void);
extern int oplus_get_rtc_ui_soc(void);
extern int oplus_set_rtc_ui_soc(int value);
extern int set_rtc_spare_fg_value(int val);
extern void mt_usb_connect(void);
extern void mt_usb_disconnect(void);
extern bool mt6357_chrdet_status(void);
extern void oplus_wake_up_usbtemp_thread(void);
extern void oplus_get_usbtemp_burn_volt(struct oplus_chg_chip *chip);
extern bool oplus_usb_temp_is_high(struct oplus_chg_chip *chip);
extern bool oplus_chg_wake_update_work(void);
extern int oplus_bq2560x_set_current(int curr);

#define DEFAULT_CV 4435

#define OPLUS_BC12_RETRY_TIME             round_jiffies_relative(msecs_to_jiffies(200))
#define OPLUS_BC12_RETRY_TIME_CDP         round_jiffies_relative(msecs_to_jiffies(400))
#define BQ_CHARGER_CURRENT_MAX_MA         (3400)

#define BQ2589X_ERR   (1 << 0)
#define BQ2589X_INFO  (1 << 1)
#define BQ2589X_DEBUG (1 << 2)

#define CDP_TIMEOUT			40

#define RETRY_CNT			3

#define BQ2598X_USB_VLIM	4500
#define BQ2598X_USB_ILIM	2000
#define BQ2598X_USB_VREG	4200
#define BQ2598X_USB_ICHG	2000
#define BQ2598X_IPRECHG		256
#define BQ2598X_ITERM		250
#define BQ2598X_BOOSTV		5000
#define BQ2598X_BOOSTI		1200

#define BQ2589X_CURR_750MA	750
#define BQ2589X_CURR_1200MA	1200
#define BQ2589X_CURR_1400MA	1400
#define BQ2589X_CURR_1650MA	1650
#define BQ2589X_CURR_1870MA	1870
#define BQ2589X_CURR_2150MA	2150
#define BQ2589X_CURR_2450MA	2450

#define BQ2589X_INP_VOL_4V4	4400
#define BQ2589X_INP_VOL_4V5	4500
#define BQ2589X_CHG_CURR_512MA	512

#define BQ2589X_PART_NO		0x03

#define BQ2589X_DUMP_REG_MAX	0x14

#define BQ2589X_MIN_ICHG	60

#define MT6357_VBUS_VOL_TH	3300

struct bq2589x_pep20_efficiency_table {
	u32 vbat;
	u32 vchr;
};

#define BQ2589X_PE2_TABLE_COUNT	10
static struct bq2589x_pep20_efficiency_table bq2589x_pe2_table[BQ2589X_PE2_TABLE_COUNT] = {
	{3400000, 8000000},
	{3500000, 8000000},
	{3600000, 8000000},
	{3700000, 8500000},
	{3800000, 8500000},
	{3900000, 8500000},
	{4000000, 9000000},
	{4100000, 9000000},
	{4200000, 9000000},
	{4400000, 9000000},
};

#define CHG_VOL_7V6	7600
#define CHG_VOL_7V5	7500
#define CHG_VOL_5V4	5400
#define CHG_VOL_4V4	4400

#define CHG_VOL_5V5_UV	5500000

#define MT6357_VBUS_VOL_7V5	7500
#define MT6357_VBUS_VOL_5V8	5800

#define BATT_VOL_4V1	4100
#define BATT_VOL_4V15	4150
#define BATT_VOL_4V2	4200
#define BATT_VOL_4V25	4250
#define BATT_VOL_4V3	4300
#define BATT_VOL_4V4	4400

#define BQ2589X_INP_CURR_700MA	700
#define BQ2589X_INP_CURR_500MA	500
#define BQ2589X_INP_CURR_3250MA	3250

#define SOC_90	90
#define SOC_85	85

static int bq2589x_chg_dbg_enable = BQ2589X_ERR|BQ2589X_INFO|BQ2589X_DEBUG;
module_param(bq2589x_chg_dbg_enable, int, 0644);
MODULE_PARM_DESC(bq2589x_chg_dbg_enable, "debug charger bq2589x");

#ifdef chg_debug
#undef chg_debug
#define chg_debug(fmt, ...) \
	if (bq2589x_chg_dbg_enable & BQ2589X_DEBUG) { \
		printk(KERN_ERR "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
	} else { \
		printk(KERN_NOTICE "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
	}
#endif

#ifdef chg_info
#undef chg_info
#define chg_info(fmt, ...) \
	if (bq2589x_chg_dbg_enable & BQ2589X_INFO) { \
		printk(KERN_ERR "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
	} else { \
		printk(KERN_NOTICE "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
	}
#else
#define chg_info(fmt, ...) \
	if (bq2589x_chg_dbg_enable & BQ2589X_INFO) { \
                printk(KERN_ERR "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
        } else { \
                printk(KERN_NOTICE "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
        }
#endif

#ifdef chg_err
#undef chg_err
#define chg_err(fmt, ...) \
	if (bq2589x_chg_dbg_enable & BQ2589X_ERR) { \
		printk(KERN_ERR "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
	} else { \
		printk(KERN_NOTICE "[OPLUS_CHG][%s]"fmt, __func__, ##__VA_ARGS__); \
	}
#endif

enum {
	PN_BQ25890H,
	PN_BQ25892,
	PN_BQ25895,
};

static int pn_data[] = {
	[PN_BQ25890H] = 0x03,
	[PN_BQ25892] = 0x00,
	[PN_BQ25895] = 0x07,
};

static char *pn_str[] = {
	[PN_BQ25890H] = "bq25890H",
	[PN_BQ25892] = "bq25892",
	[PN_BQ25895] = "bq25895",
};

enum hvdcp_type {
	HVDCP_5V,
	HVDCP_9V,
	HVDCP_12V,
	HVDCP_20V,
	HVDCP_CONTINOUS,
	HVDCP_DPF_DMF,
};

struct chg_para{
	int vlim;
	int ilim;
	int vreg;
	int ichg;
};

struct bq2589x_platform_data {
	int iprechg;
	int iterm;
	int boostv;
	int boosti;
	struct chg_para usb;
};

struct bq2589x {
	struct device *dev;
	struct i2c_client *client;
	struct delayed_work bq2589x_aicr_setting_work;
	struct delayed_work bq2589x_retry_adapter_detection;
	struct delayed_work bq2589x_vol_convert_work;
	struct delayed_work init_work;
	struct delayed_work enter_hz_work;
	struct delayed_work bq2589x_hvdcp_bc12_work;
#ifdef CONFIG_TCPC_CLASS
	/*type_c_port0*/
	struct tcpc_device *tcpc;
#endif
	int part_no;
	int revision;

	const char *chg_dev_name;
	const char *eint_name;

	bool chg_det_enable;
	bool otg_enable;

	enum charger_type chg_type;
	enum power_supply_type oplus_chg_type;

	int status;
	int irq;

	struct mutex i2c_rw_lock;
	struct mutex chgdet_en_lock;

	bool charge_enabled;	/* Register bit status */
	bool power_good;
	bool chg_need_check;
	struct bq2589x_platform_data *platform_data;
	struct charger_device *chg_dev;
	struct timespec ptime[2];

	struct power_supply *psy;
	struct charger_consumer *chg_consumer;
	bool disable_hight_vbus;
	bool pdqc_setup_5v;
	bool is_bc12_end;
	bool is_force_aicl;
	bool hvdcp_can_enabled;
	int pre_current_ma;
	int pre_cur;
	int aicr;
	int chg_cur;
	int vbus_type;
	bool hvdcp_checked;
	int hw_aicl_point;
	bool retry_hvdcp_algo;
	bool nonstand_retry_bc;
	bool camera_on;
	bool calling_on;
	bool is_bq2589x;

	bool is_force_dpdm;
	bool cdp_retry;
	bool sdp_retry;
	bool chg_start_check;
	bool is_retry_bc12;
	bool cdp_retry_aicl;
	bool usb_connect_start;
	int boot_mode;
};

static bool disable_pe = 0;
static bool disable_qc = 0;
static bool disable_pd = 0;
static bool dumpreg_by_irq = 1;
static int  current_percent = 50;

module_param(disable_pe, bool, 0644);
module_param(disable_qc, bool, 0644);
module_param(disable_pd, bool, 0644);
module_param(current_percent, int, 0644);
module_param(dumpreg_by_irq, bool, 0644);


static struct bq2589x *g_bq;
struct task_struct *charger_type_kthread;
static DECLARE_WAIT_QUEUE_HEAD(oplus_chgtype_wq);
int bq2589x_disable_enlim(struct bq2589x *bq);
static bool bq2589x_is_usb(struct bq2589x *bq);
void oplus_wake_up_usbtemp_thread(void);

extern struct oplus_chg_chip *g_oplus_chip;

extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);
void oplus_bq2589x_set_mivr_by_battery_vol(void);
static void bq2589x_dump_regs(struct bq2589x *bq);
int bq2589x_enable_enlim(struct bq2589x *bq);
int oplus_bq2589x_set_aicr(int current_ma);
int oplus_bq2589x_set_ichg(int cur);
static void bq2589x_enter_hz_work_handler(struct work_struct *work);
static oplus_bq2589x_get_charger_subtype(void);
static bool bq2589x_is_dcp(struct bq2589x *bq);
static bool bq2589x_is_hvdcp(struct bq2589x *bq);

static const struct charger_properties bq2589x_chg_props = {
	.alias_name = "bq2589x",
};

void oplus_for_cdp(void)
{
	int timeout = CDP_TIMEOUT;
	if (is_usb_rdy() == false) {
		while (is_usb_rdy() == false && timeout > 0) {
			msleep(10);
			timeout--;
		}

		if (timeout == 0) {
			chg_debug("usb_rdy timeout");
		} else {
			chg_debug("usb_rdy free");
		}
	} else {
		chg_debug("usb_rdy PASS");
	}
}

static int g_bq2589x_read_reg(struct bq2589x *bq, u8 reg, u8 *data)
{
	s32 ret;
	int retry = RETRY_CNT;

	ret = i2c_smbus_read_byte_data(bq->client, reg);

	if (ret < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_read_byte_data(bq->client, reg);
			if (ret < 0) {
				retry--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		chg_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int g_bq2589x_write_reg(struct bq2589x *bq, int reg, u8 val)
{
	s32 ret;
	int retry_cnt = RETRY_CNT;

	ret = i2c_smbus_write_byte_data(bq->client, reg, val);

	if (ret < 0) {
		while(retry_cnt > 0) {
			usleep_range(5000, 5000);
			ret = i2c_smbus_write_byte_data(bq->client, reg, val);
			if (ret < 0) {
				retry_cnt--;
			} else {
				break;
			}
		}
	}

	if (ret < 0) {
		chg_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}
	return 0;
}

static int bq2589x_read_byte(struct bq2589x *bq, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = g_bq2589x_read_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static int bq2589x_write_byte(struct bq2589x *bq, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = g_bq2589x_write_reg(bq, reg, data);
	mutex_unlock(&bq->i2c_rw_lock);

	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
	}

	return ret;
}

static int bq2589x_update_bits(struct bq2589x *bq, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&bq->i2c_rw_lock);
	ret = g_bq2589x_read_reg(bq, reg, &tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = g_bq2589x_write_reg(bq, reg, tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
	}

out:
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}

static int bq2589x_enable_otg(struct bq2589x *bq)
{
	u8 val = BQ2589X_OTG_ENABLE << BQ2589X_OTG_CONFIG_SHIFT;

	bq2589x_update_bits(bq, BQ2589X_REG_03,
				   BQ2589X_OTG_CONFIG_MASK, val);

	if(g_oplus_chip->is_double_charger_support) {
		msleep(50);
		g_oplus_chip->sub_chg_ops->charger_suspend();
	}

	return 0;
}

static int bq2589x_disable_otg(struct bq2589x *bq)
{
	u8 val = BQ2589X_OTG_DISABLE << BQ2589X_OTG_CONFIG_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_03,
				   BQ2589X_OTG_CONFIG_MASK, val);
}

static int bq2589x_enable_hvdcp(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_HVDCP_ENABLE << BQ2589X_HVDCPEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02,
				BQ2589X_HVDCPEN_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_enable_hvdcp);

static int bq2589x_disable_hvdcp(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_HVDCP_DISABLE << BQ2589X_HVDCPEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02,
				BQ2589X_HVDCPEN_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_disable_hvdcp);

static int bq2589x_disable_maxc(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_MAXC_DISABLE << BQ2589X_MAXCEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02,
				BQ2589X_MAXCEN_MASK, val);
	return ret;
}

static int bq2589x_disable_batfet_rst(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_BATFET_RST_EN_DISABLE << BQ2589X_BATFET_RST_EN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_09,
				BQ2589X_BATFET_RST_EN_MASK, val);
	return ret;
}

static int bq2589x_enable_charger(struct bq2589x *bq)
{
	int ret;

	u8 val = BQ2589X_CHG_ENABLE << BQ2589X_CHG_CONFIG_SHIFT;

	chg_info("%s\n", __func__);
	ret = bq2589x_update_bits(bq, BQ2589X_REG_03,
				BQ2589X_CHG_CONFIG_MASK, val);

	if ((g_oplus_chip != NULL)
		&& (g_oplus_chip->is_double_charger_support)
		&& g_oplus_chip->slave_charger_enable) {
		if (!bq2589x_is_usb(bq) && bq->hvdcp_can_enabled) {
			chg_debug("enable slave charger.");
			g_oplus_chip->sub_chg_ops->charging_enable();
		} else {
			chg_debug("disable slave charger.");
			g_oplus_chip->sub_chg_ops->charging_disable();
		}
	}

	return ret;
}

static int bq2589x_disable_charger(struct bq2589x *bq)
{
	int ret = 0;
	u8 val = BQ2589X_CHG_DISABLE << BQ2589X_CHG_CONFIG_SHIFT;

	chg_info("%s\n", __func__);
	ret = bq2589x_update_bits(bq, BQ2589X_REG_03,
				BQ2589X_CHG_CONFIG_MASK, val);

	if ((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->charging_disable();
	}

	return ret;
}

int bq2589x_adc_start(struct bq2589x *bq, bool oneshot)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_02, &val);
	if (ret < 0) {
		chg_err("%s failed to read register 0x02:%d\n", __func__, ret);
		return ret;
	}

	if (((val & BQ2589X_CONV_RATE_MASK) >> BQ2589X_CONV_RATE_SHIFT) == BQ2589X_ADC_CONTINUE_ENABLE)
		return 0; /*is doing continuous scan*/
	if (oneshot)
		ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_START_MASK,
					BQ2589X_CONV_START << BQ2589X_CONV_START_SHIFT);
	else
		ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_RATE_MASK,
					BQ2589X_ADC_CONTINUE_ENABLE << BQ2589X_CONV_RATE_SHIFT);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_adc_start);

int bq2589x_adc_stop(struct bq2589x *bq)
{
	return bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_CONV_RATE_MASK,
				BQ2589X_ADC_CONTINUE_DISABLE << BQ2589X_CONV_RATE_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_adc_stop);


int bq2589x_adc_read_battery_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_0E, &val);
	if (ret < 0) {
		chg_err("read battery voltage failed :%d\n", ret);
		return ret;
	} else {
		volt = BQ2589X_BATV_BASE + ((val & BQ2589X_BATV_MASK) >> BQ2589X_BATV_SHIFT) * BQ2589X_BATV_LSB;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_battery_volt);


int bq2589x_adc_read_sys_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_0F, &val);
	if (ret < 0) {
		chg_err("read system voltage failed :%d\n", ret);
		return ret;
	} else {
		volt = BQ2589X_SYSV_BASE + ((val & BQ2589X_SYSV_MASK) >> BQ2589X_SYSV_SHIFT) * BQ2589X_SYSV_LSB;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_sys_volt);

int bq2589x_adc_read_vbus_volt(struct bq2589x *bq)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_11, &val);
	if (ret < 0) {
		chg_err("read vbus voltage failed :%d\n", ret);
		return ret;
	} else {
		volt = BQ2589X_VBUSV_BASE + ((val & BQ2589X_VBUSV_MASK) >> BQ2589X_VBUSV_SHIFT) * BQ2589X_VBUSV_LSB;
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_vbus_volt);

int bq2589x_get_vbus_adc(struct charger_device *chg_dev, u32* vbus)
{
	int vol = 0;
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	vol = bq2589x_adc_read_vbus_volt(bq);
	*vbus = vol;

	return 0;
}

int bq2589x_adc_read_temperature(struct bq2589x *bq)
{
	uint8_t val;
	int temp;
	int ret;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_10, &val);
	if (ret < 0) {
		chg_err("read temperature failed :%d\n", ret);
		return ret;
	} else {
		temp = BQ2589X_TSPCT_BASE + ((val & BQ2589X_TSPCT_MASK) >> BQ2589X_TSPCT_SHIFT) * BQ2589X_TSPCT_LSB;
		return temp;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_temperature);

int bq2589x_adc_read_charge_current(void)
{
	uint8_t val;
	int volt;
	int ret;
	ret = bq2589x_read_byte(g_bq, BQ2589X_REG_12, &val);
	if (ret < 0) {
		chg_err("read charge current failed :%d\n", ret);
		return ret;
	} else {
		volt = (int)(BQ2589X_ICHGR_BASE + ((val & BQ2589X_ICHGR_MASK) >> BQ2589X_ICHGR_SHIFT) * BQ2589X_ICHGR_LSB);
		return volt;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_adc_read_charge_current);
int bq2589x_set_chargecurrent(struct bq2589x *bq, int curr)
{
	u8 ichg;

	chg_info(" ichg = %d\n", curr);

	if (curr < BQ2589X_ICHG_BASE)
		curr = BQ2589X_ICHG_BASE;

	ichg = (curr - BQ2589X_ICHG_BASE)/BQ2589X_ICHG_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_04,
						BQ2589X_ICHG_MASK, ichg << BQ2589X_ICHG_SHIFT);
}

int bq2589x_set_term_current(struct bq2589x *bq, int curr)
{
	u8 iterm;

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->term_current_set(curr + 200);
	}

	if (curr < BQ2589X_ITERM_BASE)
		curr = BQ2589X_ITERM_BASE;

	iterm = (curr - BQ2589X_ITERM_BASE) / BQ2589X_ITERM_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_05,
						BQ2589X_ITERM_MASK, iterm << BQ2589X_ITERM_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_term_current);

int bq2589x_set_prechg_current(struct bq2589x *bq, int curr)
{
	u8 iprechg;

	if (curr < BQ2589X_IPRECHG_BASE)
		curr = BQ2589X_IPRECHG_BASE;

	iprechg = (curr - BQ2589X_IPRECHG_BASE) / BQ2589X_IPRECHG_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_05,
						BQ2589X_IPRECHG_MASK, iprechg << BQ2589X_IPRECHG_SHIFT);
}
EXPORT_SYMBOL_GPL(bq2589x_set_prechg_current);

int bq2589x_set_chargevolt(struct bq2589x *bq, int volt)
{
	u8 val;

	chg_debug("volt = %d", volt);

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->float_voltage_write(volt);
	}

	if (volt < BQ2589X_VREG_BASE)
		volt = BQ2589X_VREG_BASE;

	val = (volt - BQ2589X_VREG_BASE)/BQ2589X_VREG_LSB;
	return bq2589x_update_bits(bq, BQ2589X_REG_06,
						BQ2589X_VREG_MASK, val << BQ2589X_VREG_SHIFT);
}

int bq2589x_set_input_volt_limit(struct bq2589x *bq, int volt)
{
	u8 val;

	chg_debug(" volt = %d", volt);

	if (volt < BQ2589X_VINDPM_BASE)
		volt = BQ2589X_VINDPM_BASE;

	val = (volt - BQ2589X_VINDPM_BASE) / BQ2589X_VINDPM_LSB;

	bq2589x_update_bits(bq, BQ2589X_REG_0D,
						BQ2589X_FORCE_VINDPM_MASK, BQ2589X_FORCE_VINDPM_ENABLE << BQ2589X_FORCE_VINDPM_SHIFT);

	return bq2589x_update_bits(bq, BQ2589X_REG_0D,
						BQ2589X_VINDPM_MASK, val << BQ2589X_VINDPM_SHIFT);
}

int bq2589x_set_input_current_limit(struct bq2589x *bq, int curr)
{
	u8 val;

	chg_debug("curr = %d", curr);

	if (curr < BQ2589X_IINLIM_BASE)
		curr = BQ2589X_IINLIM_BASE;

	val = (curr - BQ2589X_IINLIM_BASE) / BQ2589X_IINLIM_LSB;

	return bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_IINLIM_MASK,
						val << BQ2589X_IINLIM_SHIFT);
}


int bq2589x_set_watchdog_timer(struct bq2589x *bq, u8 timeout)
{
	u8 val;

	val = (timeout - BQ2589X_WDT_BASE) / BQ2589X_WDT_LSB;
	val <<= BQ2589X_WDT_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07,
						BQ2589X_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_set_watchdog_timer);

int bq2589x_disable_watchdog_timer(struct bq2589x *bq)
{
	u8 val = BQ2589X_WDT_DISABLE << BQ2589X_WDT_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07,
						BQ2589X_WDT_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_disable_watchdog_timer);

int bq2589x_reset_watchdog_timer(struct bq2589x *bq)
{
	u8 val = BQ2589X_WDT_RESET << BQ2589X_WDT_RESET_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_03,
						BQ2589X_WDT_RESET_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_reset_watchdog_timer);


int bq2589x_force_dpdm(struct bq2589x *bq, bool enable)
{
	int ret = 0;
	u8 val = 0;
	if (enable) {
		val = BQ2589X_AUTO_DPDM_ENABLE << BQ2589X_FORCE_DPDM_SHIFT;
	} else {
		val = BQ2589X_AUTO_DPDM_DISABLE << BQ2589X_FORCE_DPDM_SHIFT;
	}

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_FORCE_DPDM_MASK, val);

	chg_info("Force DPDM %s, enable=%d\n", !ret ?  "successfully" : "failed", enable);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_force_dpdm);

int bq2589x_reset_chip(struct bq2589x *bq)
{
	int ret;
	u8 val = BQ2589X_RESET << BQ2589X_RESET_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_14,
						BQ2589X_RESET_MASK, val);
	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_reset_chip);

int bq2589x_enable_enlim(struct bq2589x *bq)
{
	u8 val = BQ2589X_ENILIM_ENABLE << BQ2589X_ENILIM_SHIFT;
	return bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_ENILIM_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_enable_enlim);
int bq2589x_enter_hiz_mode(struct bq2589x *bq)
{
	u8 val = 0;
	int result = 0;
	int boot_mode = get_boot_mode();
	chg_info(" enter");
	if ((boot_mode == META_BOOT)
		|| (oplus_usb_temp_is_high(g_oplus_chip))
		|| (g_oplus_chip->mmi_chg == 0)) {
		val = BQ2589X_HIZ_ENABLE << BQ2589X_ENHIZ_SHIFT;
		result = bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_ENHIZ_MASK, val);
	} else {
		result = bq2589x_disable_charger(bq);
	}
	return result;
}
EXPORT_SYMBOL_GPL(bq2589x_enter_hiz_mode);

int bq2589x_exit_hiz_mode(struct bq2589x *bq)
{
	u8 val = 0;
	chg_info(" enter");

	val = BQ2589X_HIZ_DISABLE << BQ2589X_ENHIZ_SHIFT;
	bq2589x_update_bits(bq, BQ2589X_REG_00, BQ2589X_ENHIZ_MASK, val);

	return 0;
}
EXPORT_SYMBOL_GPL(bq2589x_exit_hiz_mode);

int bq2589x_disable_enlim(struct bq2589x *bq)
{
	u8 val = BQ2589X_ENILIM_DISABLE << BQ2589X_ENILIM_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_00,
						BQ2589X_ENILIM_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_disable_enlim);

int bq2589x_get_hiz_mode(struct bq2589x *bq, u8 *state)
{
	u8 val;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_00, &val);
	if (ret)
		return ret;
	*state = (val & BQ2589X_ENHIZ_MASK) >> BQ2589X_ENHIZ_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(bq2589x_get_hiz_mode);

static int bq2589x_enable_term(struct bq2589x *bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_TERM_ENABLE << BQ2589X_EN_TERM_SHIFT;
	else
		val = BQ2589X_TERM_DISABLE << BQ2589X_EN_TERM_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_07,
						BQ2589X_EN_TERM_MASK, val);
	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->set_charging_term_disable();
	}

	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_enable_term);

int bq2589x_set_boost_current(struct bq2589x *bq, int curr)
{
	u8 temp;

	if (curr < BQ2589X_CURR_750MA)
		temp = BQ2589X_BOOST_LIM_500MA;
	else if (curr < BQ2589X_CURR_1200MA)
		temp = BQ2589X_BOOST_LIM_750MA;
	else if (curr < BQ2589X_CURR_1400MA)
		temp = BQ2589X_BOOST_LIM_1200MA;
	else if (curr < BQ2589X_CURR_1650MA)
		temp = BQ2589X_BOOST_LIM_1400MA;
	else if (curr < BQ2589X_CURR_1870MA)
		temp = BQ2589X_BOOST_LIM_1650MA;
	else if (curr < BQ2589X_CURR_2150MA)
		temp = BQ2589X_BOOST_LIM_1875MA;
	else if (curr < BQ2589X_CURR_2450MA)
		temp = BQ2589X_BOOST_LIM_2150MA;
	else
		temp = BQ2589X_BOOST_LIM_2450MA;

	chg_info("boost current temp = %d mA", temp);
	return bq2589x_update_bits(bq, BQ2589X_REG_0A,
				BQ2589X_BOOST_LIM_MASK,
				temp << BQ2589X_BOOST_LIM_SHIFT);
}

static int bq2589x_vmin_limit(struct bq2589x *bq)
{
        u8 val = 4 << BQ2589X_SYS_MINV_SHIFT;

        return bq2589x_update_bits(bq, BQ2589X_REG_03,
                                   BQ2589X_SYS_MINV_MASK, val);
}

static int bq2589x_enable_auto_dpdm(struct bq2589x *bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_AUTO_DPDM_ENABLE << BQ2589X_AUTO_DPDM_EN_SHIFT;
	else
		val = BQ2589X_AUTO_DPDM_DISABLE << BQ2589X_AUTO_DPDM_EN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02,
						BQ2589X_AUTO_DPDM_EN_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_enable_auto_dpdm);

int bq2589x_set_boost_voltage(struct bq2589x *bq, int volt)
{
	u8 val = 0;

	if (volt < BQ2589X_BOOSTV_BASE)
		volt = BQ2589X_BOOSTV_BASE;
	if (volt > BQ2589X_BOOSTV_BASE
			+ (BQ2589X_BOOSTV_MASK >> BQ2589X_BOOSTV_SHIFT)
			* BQ2589X_BOOSTV_LSB)
		volt = BQ2589X_BOOSTV_BASE
			+ (BQ2589X_BOOSTV_MASK >> BQ2589X_BOOSTV_SHIFT)
			* BQ2589X_BOOSTV_LSB;

	val = ((volt - BQ2589X_BOOSTV_BASE) / BQ2589X_BOOSTV_LSB)
			<< BQ2589X_BOOSTV_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_0A,
				BQ2589X_BOOSTV_MASK, val);
}
EXPORT_SYMBOL_GPL(bq2589x_set_boost_voltage);

static int bq2589x_enable_ico(struct bq2589x *bq, bool enable)
{
	u8 val;
	int ret;

	if (enable)
		val = BQ2589X_ICO_ENABLE << BQ2589X_ICOEN_SHIFT;
	else
		val = BQ2589X_ICO_DISABLE << BQ2589X_ICOEN_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_02, BQ2589X_ICOEN_MASK, val);

	return ret;
}
EXPORT_SYMBOL_GPL(bq2589x_enable_ico);

static int bq2589x_read_idpm_limit(struct bq2589x *bq, int *icl)
{
	uint8_t val;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_13, &val);
	if (ret < 0) {
		chg_err("read vbus voltage failed :%d\n", ret);
		return ret;
	} else {
		*icl = BQ2589X_IDPM_LIM_BASE + ((val & BQ2589X_IDPM_LIM_MASK) >> BQ2589X_IDPM_LIM_SHIFT) * BQ2589X_IDPM_LIM_LSB;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_read_idpm_limit);

static int bq2589x_enable_safety_timer(struct bq2589x *bq)
{
	const u8 val = BQ2589X_CHG_TIMER_ENABLE << BQ2589X_EN_TIMER_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07, BQ2589X_EN_TIMER_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(bq2589x_enable_safety_timer);

static int bq2589x_disable_safety_timer(struct bq2589x *bq)
{
	const u8 val = BQ2589X_CHG_TIMER_DISABLE << BQ2589X_EN_TIMER_SHIFT;

	return bq2589x_update_bits(bq, BQ2589X_REG_07, BQ2589X_EN_TIMER_MASK,
				   val);
}
EXPORT_SYMBOL_GPL(bq2589x_disable_safety_timer);



static int bq2589x_switch_to_hvdcp(struct bq2589x *bq, enum hvdcp_type type)
{
	int ret = 0;
	int val = 0;
	int mask = 0;

	switch (type) {
	case HVDCP_5V:
		if (bq->is_bq2589x) {
			val = (BQ2589X_DP_0P6V << BQ2589X_DPDAC_SHIFT) | (BQ2589X_DM_0V << BQ2589X_DMDAC_SHIFT);
			mask = BQ2589X_DPDAC_MASK | BQ2589X_DMDAC_MASK;
			ret = bq2589x_update_bits(bq, BQ2589X_REG_01, mask, val);
		} else {
			val = BQ2589X_HVDCP_DISABLE << BQ2589X_HVDCPEN_SHIFT;
			ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 8, val);
			Charger_Detect_Init();
			oplus_for_cdp();
			bq2589x_force_dpdm(bq, true);
		}
		chg_info(" set to 5v\n");
		break;

	case HVDCP_9V:
		if (bq->is_bq2589x) {
			val = (BQ2589X_DP_3P3V << BQ2589X_DPDAC_SHIFT) | (BQ2589X_DM_0P6V << BQ2589X_DMDAC_SHIFT);
			mask = BQ2589X_DPDAC_MASK | BQ2589X_DMDAC_MASK;
			ret = bq2589x_update_bits(bq, BQ2589X_REG_01, mask, val);
		} else {
			/*HV_TYPE: Higher Voltage Types*/
			val = BQ2589X_HVDCP_DISABLE << BQ2589X_HVDCPHV_SHIFT;
			ret = bq2589x_update_bits(bq, BQ2589X_REG_02, 4, val);
		}
		chg_info(" set to 9v\n");
		break;

	default:
		chg_err(" not support now.\n");
		break;
	}
	return ret;
}

static int bq2589x_check_charge_done(struct bq2589x *bq, bool *done)
{
	int ret;
	u8 val;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &val);
	if (!ret) {
		val = val & BQ2589X_CHRG_STAT_MASK;
		val = val >> BQ2589X_CHRG_STAT_SHIFT;
		*done = (val == BQ2589X_CHRG_STAT_CHGDONE);
	}

	return ret;
}

static struct bq2589x_platform_data *bq2589x_parse_dt(struct device_node *np,
						      struct bq2589x *bq)
{
	int ret;
	struct bq2589x_platform_data *pdata;

	pdata = devm_kzalloc(bq->dev, sizeof(struct bq2589x_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_string(np, "charger_name", &bq->chg_dev_name) < 0) {
		bq->chg_dev_name = "primary_chg";
		pr_warn("no charger name\n");
	}

	if (of_property_read_string(np, "eint_name", &bq->eint_name) < 0) {
		bq->eint_name = "chr_stat";
		pr_warn("no eint name\n");
	}

	bq->chg_det_enable =
	    of_property_read_bool(np, "ti,bq2589x,charge-detect-enable");

	ret = of_property_read_u32(np, "ti,bq2589x,usb-vlim", &pdata->usb.vlim);
	if (ret) {
		pdata->usb.vlim = BQ2598X_USB_VLIM;
		chg_err("Failed to read node of ti,bq2589x,usb-vlim\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,usb-ilim", &pdata->usb.ilim);
	if (ret) {
		pdata->usb.ilim = BQ2598X_USB_ILIM;
		chg_err("Failed to read node of ti,bq2589x,usb-ilim\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,usb-vreg", &pdata->usb.vreg);
	if (ret) {
		pdata->usb.vreg = BQ2598X_USB_VREG;
		chg_err("Failed to read node of ti,bq2589x,usb-vreg\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,usb-ichg", &pdata->usb.ichg);
	if (ret) {
		pdata->usb.ichg = BQ2598X_USB_ICHG;
		chg_err("Failed to read node of ti,bq2589x,usb-ichg\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,precharge-current",
				   &pdata->iprechg);
	if (ret) {
		pdata->iprechg = BQ2598X_IPRECHG;
		chg_err("Failed to read node of ti,bq2589x,precharge-current\n");
	}

	ret = of_property_read_u32(np, "ti,bq2589x,termination-current",
				   &pdata->iterm);
	if (ret) {
		pdata->iterm = BQ2598X_ITERM;
		chg_err("Failed to read node of ti,bq2589x,termination-current\n");
	}

	ret =
	    of_property_read_u32(np, "ti,bq2589x,boost-voltage",
				 &pdata->boostv);
	if (ret) {
		pdata->boostv = BQ2598X_BOOSTV;
		chg_err("Failed to read node of ti,bq2589x,boost-voltage\n");
	}

	ret =
	    of_property_read_u32(np, "ti,bq2589x,boost-current",
				 &pdata->boosti);
	if (ret) {
		pdata->boosti = BQ2598X_BOOSTI;
		chg_err("Failed to read node of ti,bq2589x,boost-current\n");
	}


	return pdata;
}

static int bq2589x_get_charger_type(struct bq2589x *bq, enum power_supply_type *type)
{
	int ret;

	u8 reg_val = 0;
	int vbus_stat = 0;
	enum power_supply_type oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);

	if (ret)
		return ret;

	vbus_stat = (reg_val & BQ2589X_VBUS_STAT_MASK);
	vbus_stat >>= BQ2589X_VBUS_STAT_SHIFT;
	bq->vbus_type = vbus_stat;
	chg_info("type:%d, reg0B = 0x%x\n", vbus_stat, reg_val);
	switch (vbus_stat) {
	case BQ2589X_VBUS_TYPE_NONE:
		oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	case BQ2589X_VBUS_TYPE_SDP:
		oplus_chg_type = POWER_SUPPLY_TYPE_USB;
		break;
	case BQ2589X_VBUS_TYPE_CDP:
		oplus_chg_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case BQ2589X_VBUS_TYPE_DCP:
		oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case BQ2589X_VBUS_TYPE_HVDCP:
		oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		if(!disable_qc) {
			bq->hvdcp_can_enabled = true;
		}
		break;
	case BQ2589X_VBUS_TYPE_UNKNOWN:
		oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case BQ2589X_VBUS_TYPE_NON_STD:
		oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	default:
		oplus_chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	}

	*type = oplus_chg_type;

	return 0;
}

static int bq2589x_inform_charger_type(struct bq2589x *bq)
{
	int ret = 0;
	union power_supply_propval propval;

	if (!bq->psy) {
		bq->psy = power_supply_get_by_name("mtk-master-charger");
		if (IS_ERR_OR_NULL(bq->psy)) {
			chg_err("Couldn't get bq->psy");
			return -EINVAL;
		}
	}

	if (bq->power_good)
		propval.intval = 1;
	else
		propval.intval = 0;

	chg_info("inform power supply online status:%d", propval.intval);
	ret = power_supply_set_property(bq->psy, POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0) {
		chg_err("inform power supply online failed:%d", ret);
	}

	power_supply_changed(bq->psy);
	return ret;
}


static int bq2589x_cfg_dpdm2hiz_mode(struct bq2589x *bq)
{
	int ret = 0;
	int val = 0;
	int mask = 0;
	u8 reg_val = 0;

	if (bq->is_bq2589x) {
		val = (BQ2589X_DP_HIZ << BQ2589X_DPDAC_SHIFT) | (BQ2589X_DM_HIZ << BQ2589X_DMDAC_SHIFT);
		mask = BQ2589X_DPDAC_MASK | BQ2589X_DMDAC_MASK;

		ret = bq2589x_update_bits(bq, BQ2589X_REG_01, mask, val);
	}
	ret = bq2589x_read_byte(bq, BQ2589X_REG_01, &reg_val);
	chg_debug("(%d,%d)", reg_val, ret);
	return 0;
}

static irqreturn_t bq2589x_irq_handler(int irq, void *data)
{
	struct bq2589x *bq = (struct bq2589x *)data;
	int ret = 0;
	u8 reg_val = 0;
	u8 hz_mode = 0;
	bool prev_pg = false;
	enum power_supply_type prev_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	enum power_supply_type cur_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	struct oplus_chg_chip *chip = g_oplus_chip;
	if (chip == NULL)
		return IRQ_HANDLED;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);
	if (ret)
		return IRQ_HANDLED;

	prev_pg = bq->power_good;
	bq->power_good = !!(reg_val & BQ2589X_PG_STAT_MASK);
	chg_info("(%d, %d)\n", prev_pg, bq->power_good);
	oplus_chg_track_check_wired_charging_break(bq->power_good);
	oplus_bq2589x_set_mivr_by_battery_vol();

	if (dumpreg_by_irq)
		bq2589x_dump_regs(bq);

	if (!prev_pg && bq->power_good) {
		if (!bq->chg_det_enable)
			return IRQ_HANDLED;

		chg_info("adapter/usb inserted.");

		/*step1: eable ilim and set input current to protect the charger.*/
		/*
                * Enable the ilim to protect the charger,
                * because of the charger input current limit will change to default value when bc1.2 complete..
                */
		bq2589x_enable_enlim(bq);
		bq->is_force_aicl = true;
		bq2589x_set_input_current_limit(bq, BQ2589X_INP_CURR_500MA);

		/*step2: start 5s thread */
		oplus_chg_wake_update_work();

		/*step3: BC1.2*/
		chg_debug("adapter/usb inserted. start bc1.2");
		Charger_Detect_Init();
		if (bq->is_force_dpdm) {
			bq->is_force_dpdm = false;
			bq2589x_force_dpdm(bq, false);
		} else {
			bq2589x_disable_hvdcp(bq);
			bq2589x_force_dpdm(bq, true);
		}
		bq2589x_enable_auto_dpdm(bq, false);

		bq->chg_need_check = true;
		bq->chg_start_check = false;
		bq->hvdcp_checked = false;

		/*Step4: check SDP/CDP, retry BC1.2*/
		chg_debug("wake up the chgtype thread.");
		wake_up_interruptible(&oplus_chgtype_wq);
	} else if (prev_pg && !bq->power_good) {
		bq->chg_cur = 0;
		bq->aicr = 500;
		bq2589x_adc_start(bq, false);

		ret = bq2589x_get_hiz_mode(bq, &hz_mode);
		if (!ret && hz_mode) {
			chg_err("hiz mode ignore\n");
			return IRQ_HANDLED;
		}
		bq->is_force_aicl = false;
		bq->pre_current_ma = -1;
		bq->usb_connect_start = false;
		bq->hvdcp_can_enabled = false;
		bq->hvdcp_checked = false;
		bq->sdp_retry = false;
		bq->cdp_retry = false;
		bq->is_force_dpdm = false;
		bq->retry_hvdcp_algo = false;
		bq->chg_type = CHARGER_UNKNOWN;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		bq->nonstand_retry_bc = false;
		if (chip) {
			chip->pd_chging = false;
		}
		bq2589x_inform_charger_type(bq);
		if (chip->is_double_charger_support) {
                        chip->sub_chg_ops->charging_disable();
                }

		bq2589x_enable_enlim(bq);
		bq2589x_disable_hvdcp(bq);
		bq2589x_cfg_dpdm2hiz_mode(bq);

		Charger_Detect_Release();
		cancel_delayed_work_sync(&bq->bq2589x_retry_adapter_detection);
		cancel_delayed_work_sync(&bq->bq2589x_aicr_setting_work);
		cancel_delayed_work_sync(&bq->bq2589x_hvdcp_bc12_work);
		oplus_chg_wake_update_work();
		chg_info("adapter/usb removed.");
		return IRQ_HANDLED;
	} else if (!prev_pg && !bq->power_good) {
		chg_info("prev_pg & now_pg is false\n");
		return IRQ_HANDLED;
	}

	if (bq->otg_enable) {
		chg_info("otg_enable\n");

		bq2589x_disable_enlim(bq);
		return IRQ_HANDLED;
	}

	if (!(BQ2589X_VBUS_STAT_MASK & reg_val)) {
		return IRQ_HANDLED;
	}


	/*step5: get charger type.*/
	prev_chg_type = bq->oplus_chg_type;
	ret = bq2589x_get_charger_type(bq, &cur_chg_type);
	chg_info(" prev_chg_type %d --> cur_chg_type %d, bq->oplus_chg_type %d, chg_det_enable %d, ret %d\n",
		 prev_chg_type, cur_chg_type, bq->oplus_chg_type, bq->chg_det_enable, ret);

	/* Fix the bug : the adapter type is changed  from DCP to SDP/CDP when doing hvdcp BC1.2 */
	if ((prev_chg_type == POWER_SUPPLY_TYPE_USB_DCP)
		&& ((cur_chg_type == POWER_SUPPLY_TYPE_USB) || (cur_chg_type == POWER_SUPPLY_TYPE_USB_CDP))) {
		chg_info(" keep cur_chg_type %d as prev_chg_type %d ", cur_chg_type, prev_chg_type);
		cur_chg_type = prev_chg_type;
	}

	if ((cur_chg_type == POWER_SUPPLY_TYPE_USB)
		|| (cur_chg_type == POWER_SUPPLY_TYPE_USB_CDP)) {
		if ((get_project() == 21251) || (get_project() == 21253) || (get_project() == 21254))
			bq->usb_connect_start = true;
		chg_info(" type usb %d\n", bq->usb_connect_start);
		bq->oplus_chg_type = cur_chg_type;

		/*Step 6.1 CDP/SDP */
		if (bq->usb_connect_start) {
			Charger_Detect_Release();
			bq2589x_disable_enlim(bq);
			bq->is_force_aicl = false;
			bq2589x_inform_charger_type(bq);
			oplus_chg_wake_update_work();
		}
	} else if (cur_chg_type != POWER_SUPPLY_TYPE_UNKNOWN) {
		/*Step 6.2: DCP/HVDCP*/
		chg_info(" cur_chg_type = %d, vbus_type = %d\n", cur_chg_type, bq->vbus_type);
		Charger_Detect_Release();

		bq->oplus_chg_type = cur_chg_type;
		bq->is_force_aicl = false;
		bq2589x_inform_charger_type(bq);

		/*Step7: HVDCP and BC1.2*/
		if (!bq->hvdcp_checked && !bq2589x_is_hvdcp(bq)) {
			chg_info(" enable hvdcp.");
			if (!bq2589x_is_dcp(bq)) {
				chg_debug(" not dcp.");
			}

			schedule_delayed_work(&bq->bq2589x_hvdcp_bc12_work, msecs_to_jiffies(1500));
		} else if (bq->hvdcp_checked) {
			chg_info(" bq2589x hvdcp is checked");

			/*Step8: HVDCP AICL and config.*/
			if (bq->hvdcp_can_enabled) {
				chg_info(" bq2589x hvdcp_can_enabled.");
			}

			/*restart AICL after the BC1.2 of HDVCP check*/
			schedule_delayed_work(&bq->bq2589x_aicr_setting_work, 0);
			oplus_chg_wake_update_work();
		} else {
			chg_err("oplus_chg_type = %d, hvdcp_checked = %d", bq->oplus_chg_type, bq->hvdcp_checked);
		}
	} else {
		chg_err("oplus_chg_type = %d, vbus_type = %d", bq->oplus_chg_type, bq->vbus_type);
	}

	oplus_wake_up_usbtemp_thread();
	return IRQ_HANDLED;
}

static int bq2589x_register_interrupt(struct device_node *np, struct bq2589x *bq)
{
	int ret = 0;

	bq->irq = irq_of_parse_and_map(np, 0);

	ret = devm_request_threaded_irq(bq->dev, bq->irq, NULL,
					bq2589x_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					bq->eint_name, bq);
	if (ret < 0) {
		chg_err("request thread irq failed:%d\n", ret);
		return ret;
	}

	enable_irq_wake(bq->irq);

	return 0;
}

static int bq2589x_init_device(struct bq2589x *bq)
{
	int ret;

	bq2589x_disable_watchdog_timer(bq);
	bq->is_force_dpdm = false;
	ret = bq2589x_set_prechg_current(bq, bq->platform_data->iprechg);
	if (ret) {
		chg_err("Failed to set prechg current, ret = %d\n", ret);
	}
	ret = bq2589x_set_chargevolt(bq, DEFAULT_CV);
	if (ret) {
		chg_err("Failed to set default cv, ret = %d\n", ret);
	}
	ret = bq2589x_set_term_current(bq, bq->platform_data->iterm);
	if (ret) {
		chg_err("Failed to set termination current, ret = %d\n", ret);
	}
	ret = bq2589x_set_boost_voltage(bq, bq->platform_data->boostv);
	if (ret) {
		chg_err("Failed to set boost voltage, ret = %d\n", ret);
	}
	ret = bq2589x_set_boost_current(bq, bq->platform_data->boosti);
	if (ret) {
		chg_err("Failed to set boost current, ret = %d\n", ret);
	}
	ret = bq2589x_enable_enlim(g_bq);
	if (ret) {
		chg_err("Failed to set enlim, ret = %d\n", ret);
	}
	ret = bq2589x_enable_auto_dpdm(bq, false);
	if (ret) {
		chg_err("Failed to set auto dpdm, ret = %d\n", ret);
	}
	ret = bq2589x_vmin_limit(bq);
	if (ret) {
		chg_err("Failed to set vmin limit, ret = %d\n", ret);
	}
	ret = bq2589x_adc_start(bq, true);
	if (ret) {
		chg_err("Failed to start adc, ret = %d\n", ret);
	}

	ret = bq2589x_set_input_volt_limit(bq, BQ2589X_INP_VOL_4V4);
	if (ret) {
		chg_err("Failed to set input volt limit, ret = %d\n", ret);
	}
	bq->boot_mode = get_boot_mode();
	if (bq->boot_mode == META_BOOT) {
		chg_err(" disable_charger for meta boot.\n");
		bq2589x_disable_charger(bq);
		INIT_DELAYED_WORK(&bq->enter_hz_work, bq2589x_enter_hz_work_handler);

		/*Stop charging for META BOOT after the ELT&ETS ports is enable */
		schedule_delayed_work(&bq->enter_hz_work, 0);
	}

	return 0;
}

void bq2589x_initial_status(bool is_charger_on)
{
	if(!g_bq)
		return;
	if (is_charger_on) {
		Charger_Detect_Init();
		oplus_for_cdp();
		bq2589x_enable_auto_dpdm(g_bq, false);
		g_bq->is_force_aicl = true;
		g_bq->is_retry_bc12 = true;
		bq2589x_force_dpdm(g_bq, true);
		g_bq->is_force_dpdm = true;
	} else {
		g_bq->is_force_dpdm = false;
	}
}
EXPORT_SYMBOL_GPL(bq2589x_initial_status);


static bool bq2589x_is_dcp(struct bq2589x *bq)
{
        int ret = 0;
        u8 reg_val = 0;
        int vbus_stat = 0;

        ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);
        if (ret) {
                return false;
        }

        vbus_stat = (reg_val & BQ2589X_VBUS_STAT_MASK);
        vbus_stat >>= BQ2589X_VBUS_STAT_SHIFT;
        if (vbus_stat == BQ2589X_VBUS_TYPE_DCP)
                return true;

        return false;
}


static bool bq2589x_is_hvdcp(struct bq2589x *bq)
{
	int ret = 0;
	u8 reg_val = 0;
	int vbus_stat = 0;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);
	if (ret) {
		return false;
	}

	vbus_stat = (reg_val & BQ2589X_VBUS_STAT_MASK);
	vbus_stat >>= BQ2589X_VBUS_STAT_SHIFT;
	if (vbus_stat == BQ2589X_VBUS_TYPE_HVDCP)
		return true;

	return false;
}

static bool bq2589x_is_usb(struct bq2589x *bq)
{
	int ret = 0;
	u8 reg_val = 0;
	int vbus_stat = 0;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);
	if (ret) {
		return false;
	}

	vbus_stat = (reg_val & BQ2589X_VBUS_STAT_MASK);
	vbus_stat >>= BQ2589X_VBUS_STAT_SHIFT;

	if (vbus_stat == BQ2589X_VBUS_TYPE_SDP) {
		return true;
	}

	return false;
}

static void determine_initial_status(struct bq2589x *bq)
{
	enum power_supply_type type = POWER_SUPPLY_TYPE_UNKNOWN;
	if (bq2589x_is_hvdcp(bq)) {
		bq2589x_get_charger_type(bq, &type);
		bq2589x_inform_charger_type(bq);
	}
}

static int bq2589x_detect_device(struct bq2589x *bq)
{
	int ret;
	u8 data;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_14, &data);
	if (!ret) {
		bq->part_no = (data & BQ2589X_PN_MASK) >> BQ2589X_PN_SHIFT;
		bq->revision =
		    (data & BQ2589X_DEV_REV_MASK) >> BQ2589X_DEV_REV_SHIFT;
	}

	pr_notice("bq->part_no = 0x%08x revision=%d\n", bq->part_no, bq->revision);
	if (bq->part_no == BQ2589X_PART_NO) {
		bq->is_bq2589x = true;
		pr_notice("----BQ2589x IC----\n");
	} else {
		bq->is_bq2589x = false;
		pr_notice("----SY6970 IC-----\n");
	}

	return ret;
}

static void register_charger_devinfo(struct bq2589x *bq)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int ret = 0;
	char *version;
	char *manufacture;

	if(!bq) {
		chg_err("No bq2589x device found!\n");
		return;
        }
	switch(bq->part_no) {
	case BQ2589X_PART_NO:
		version = "bq2589x";
		manufacture = "TI";
        	break;
	default:
		version = "sy6970";
		manufacture = "silergy corp";
		break;
	}
	if (strcmp(bq->chg_dev_name, "primary_chg") == 0) {
		ret = register_device_proc("charger", version, manufacture);
	} else {
		ret = register_device_proc("secondary_charger", version, manufacture);
	}
	if(ret) {
		pr_err("register_charger_devinfo failed\n");
	}
#endif
}

static void bq2589x_dump_regs(struct bq2589x *bq)
{
	int addr;
	u8 val[25];
	int ret;
	char buf[400];
	char *s = buf;

	for (addr = 0x0; addr <= BQ2589X_DUMP_REG_MAX; addr++) {
		ret = bq2589x_read_byte(bq, addr, &val[addr]);
		msleep(1);
	}

	s+=sprintf(s, "bq2589x_dump_regs:");
	for (addr = 0x0; addr <= BQ2589X_DUMP_REG_MAX; addr++) {
		s+=sprintf(s, "[0x%.2x,0x%.2x]", addr, val[addr]);
	}
	s+=sprintf(s, "\n");

	chg_info("%s", buf);
}

static ssize_t
bq2589x_show_registers(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct bq2589x *bq = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "bq2589x Reg");
	for (addr = 0x0; addr <= BQ2589X_DUMP_REG_MAX; addr++) {
		ret = bq2589x_read_byte(bq, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t
bq2589x_store_registers(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct bq2589x *bq = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < BQ2589X_DUMP_REG_MAX) {
		bq2589x_write_byte(bq, (unsigned char) reg,
				   (unsigned char) val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, bq2589x_show_registers,
		   bq2589x_store_registers);

static struct attribute *bq2589x_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group bq2589x_attr_group = {
	.attrs = bq2589x_attributes,
};

static int bq2589x_charging(struct charger_device *chg_dev, bool enable)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	u8 val;

	if (enable)
		ret = bq2589x_enable_charger(bq);
	else
		ret = bq2589x_disable_charger(bq);

	chg_debug(" %s charger %s\n", enable ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	ret = bq2589x_read_byte(bq, BQ2589X_REG_03, &val);

	if (!ret)
		bq->charge_enabled = !!(val & BQ2589X_CHG_CONFIG_MASK);

	return ret;
}

static int bq2589x_plug_in(struct charger_device *chg_dev)
{
	int ret;

	ret = bq2589x_charging(chg_dev, true);

	if (ret) {
		chg_err("Failed to enable charging:%d", ret);
	}
	return ret;
}

static int bq2589x_plug_out(struct charger_device *chg_dev)
{
	int ret;

	ret = bq2589x_charging(chg_dev, false);

	if (ret) {
		chg_err("Failed to disable charging:%d", ret);
	}
	return ret;
}

static int bq2589x_dump_register(struct charger_device *chg_dev)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	bq2589x_dump_regs(bq);

	return 0;
}

static int bq2589x_is_charging_enable(struct charger_device *chg_dev, bool *en)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	*en = bq->charge_enabled;

	return 0;
}

static int bq2589x_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	ret = bq2589x_check_charge_done(bq, done);

	return ret;
}

static int bq2589x_set_ichg(struct charger_device *chg_dev, u32 curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	chg_debug("charge curr = %d", curr);
	return bq2589x_set_chargecurrent(bq, curr / 1000);
}

static int _bq2589x_get_ichg(struct bq2589x *bq, u32 *curr)
{
	u8 reg_val;
	int ichg;
	int ret;

	bq2589x_adc_start(bq, true);
	ret = bq2589x_read_byte(bq, BQ2589X_REG_04, &reg_val);
	if (!ret) {
		ichg = (reg_val & BQ2589X_ICHG_MASK) >> BQ2589X_ICHG_SHIFT;
		ichg = ichg * BQ2589X_ICHG_LSB + BQ2589X_ICHG_BASE;
		*curr = ichg * 1000;
	}
	bq2589x_adc_start(bq, false);

	return ret;
}

static int bq2589x_get_ichg(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int ichg;
	int ret;

	ret = _bq2589x_get_ichg(bq, curr);
	return ret;
}

static u32 oplus_get_charger_current(void) {
	u32 value = 0;

	if (!g_bq) {
		return 0;
	}

	_bq2589x_get_ichg(g_bq, &value);
	return value;
}


static int bq2589x_get_min_ichg(struct charger_device *chg_dev, u32 *curr)
{
	*curr = BQ2589X_MIN_ICHG * 1000;

	return 0;
}

static int bq2589x_set_vchg(struct charger_device *chg_dev, u32 volt)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	chg_debug("charge volt = %d", volt);

	return bq2589x_set_chargevolt(bq, volt / 1000);
}

static int bq2589x_get_vchg(struct charger_device *chg_dev, u32 *volt)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int vchg;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_06, &reg_val);
	if (!ret) {
		vchg = (reg_val & BQ2589X_VREG_MASK) >> BQ2589X_VREG_SHIFT;
		vchg = vchg * BQ2589X_VREG_LSB + BQ2589X_VREG_BASE;
		*volt = vchg * 1000;
	}

	return ret;
}

static int bq2589x_set_ivl(struct charger_device *chg_dev, u32 volt)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	chg_debug("vindpm volt = %d", volt);

	return bq2589x_set_input_volt_limit(bq, volt / 1000);
}

static int bq2589x_set_icl(struct charger_device *chg_dev, u32 curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	chg_debug("indpm curr = %d", curr);

	return bq2589x_set_input_current_limit(bq, curr / 1000);
}

static int bq2589x_get_icl(struct charger_device *chg_dev, u32 *curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	u8 reg_val;
	int icl;
	int ret;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_00, &reg_val);
	if (!ret) {
		icl = (reg_val & BQ2589X_IINLIM_MASK) >> BQ2589X_IINLIM_SHIFT;
		icl = icl * BQ2589X_IINLIM_LSB + BQ2589X_IINLIM_BASE;
		*curr = icl * 1000;
	}

	return ret;
}

static int bq2589x_kick_wdt(struct charger_device *chg_dev)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	return bq2589x_reset_watchdog_timer(bq);
}

static int bq2589x_set_otg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	if (en) {
		bq2589x_disable_charger(bq);
		ret = bq2589x_enable_otg(bq);
	} else {
		ret = bq2589x_disable_otg(bq);
		bq2589x_enable_charger(bq);
	}
	if(!ret)
		bq->otg_enable = en;

	chg_info(" %s OTG %s\n", en ? "enable" : "disable",
	       !ret ? "successfully" : "failed");

	return ret;
}

static int bq2589x_set_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	if (en)
		ret = bq2589x_enable_safety_timer(bq);
	else
		ret = bq2589x_disable_safety_timer(bq);

	return ret;
}

static int bq2589x_is_safety_timer_enabled(struct charger_device *chg_dev,
					   bool *en)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	u8 reg_val;

	ret = bq2589x_read_byte(bq, BQ2589X_REG_07, &reg_val);

	if (!ret)
		*en = !!(reg_val & BQ2589X_EN_TIMER_MASK);

	return ret;
}

static int bq2589x_set_boost_ilmt(struct charger_device *chg_dev, u32 curr)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;

	chg_debug("otg curr = %d", curr);

	ret = bq2589x_set_boost_current(bq, curr / 1000);

	return ret;
}

static int bq2589x_chgdet_en(struct bq2589x *bq, bool en)
{
	int ret = 0;
	u8 val = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if ((chip == NULL) || (NULL == bq)) {
		return -1;
	}

	chg_debug(" enable:%d\n", en);
	bq->chg_det_enable = en;

	if (en) {
		Charger_Detect_Init();
		oplus_for_cdp();
                bq2589x_enable_auto_dpdm(bq, false);
                /* bq->is_force_aicl = true; */
                bq->is_retry_bc12 = true;
                bq2589x_force_dpdm(bq, true);
		bq->is_force_dpdm = true;
	} else {
		bq->pre_current_ma = -1;
		bq->hvdcp_can_enabled = false;
		bq->hvdcp_checked = false;
		bq->retry_hvdcp_algo = false;
		bq->nonstand_retry_bc = false;
		bq->sdp_retry = false;
		bq->cdp_retry = false;
		bq->usb_connect_start = false;
		bq->is_force_aicl = false;
                bq->is_force_dpdm = false;

		bq2589x_disable_hvdcp(bq);
		Charger_Detect_Release();

		memset(&bq->ptime[0], 0, sizeof(struct timespec));
		memset(&bq->ptime[1], 0, sizeof(struct timespec));
		cancel_delayed_work_sync(&bq->bq2589x_retry_adapter_detection);
		cancel_delayed_work_sync(&bq->bq2589x_aicr_setting_work);
                cancel_delayed_work_sync(&bq->bq2589x_hvdcp_bc12_work);

		bq->chg_type = CHARGER_UNKNOWN;
		bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		if (chip->is_double_charger_support && chip->sub_chg_ops) {
			chip->sub_chg_ops->charging_disable();
		}

		if (bq->power_good && mt6357_get_vbus_voltage() < MT6357_VBUS_VOL_TH) {
			chg_err("vbus off but pg is good");
			bq->power_good = false;
		}

		bq2589x_inform_charger_type(bq);
	}

	return ret;
}


static int bq2589x_enable_chgdet(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	chg_debug("bq2589x_enable_chgdet:%d", en);

	ret = bq2589x_chgdet_en(bq, en);

	return ret;
}

static int bq2589x_enter_ship_mode(struct bq2589x *bq, bool en)
{
	int ret;
	u8 val;

	if (en)
		val = BQ2589X_BATFET_OFF;
	else
		val = BQ2589X_BATFET_ON;
	val <<= BQ2589X_BATFET_DIS_SHIFT;

	ret = bq2589x_update_bits(bq, BQ2589X_REG_09,
						BQ2589X_BATFET_DIS_MASK, val);
	return ret;
}

static int bq2589x_enable_shipmode(bool en)
{
	int ret;

	ret = bq2589x_enter_ship_mode(g_bq, en);

	return 0;
}

static int bq2589x_set_hz_mode(bool en)
{
	int ret;

	if (en) {
		ret = bq2589x_enter_hiz_mode(g_bq);
	} else {
		ret = bq2589x_exit_hiz_mode(g_bq);
	}

	return ret;
}

#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
static int bq2589x_set_pep20_efficiency_table(struct charger_device *chg_dev)
{
	int ret = 0;
	struct mtk_charger *chg_mgr = NULL;

	chg_mgr = charger_dev_get_drvdata(chg_dev);
	if (!chg_mgr)
		return -EINVAL;

	for (int i = 0; i < BQ2589X_PE2_TABLE_COUNT; ++i) {
		chg_mgr->pe2.profile[i].vbat = bq2589x_pe2_table.vbat[i];
		chg_mgr->pe2.profile[i].vchr = bq2589x_pe2_table.vchr[i];
	}

	return ret;
}

struct timespec ptime[13];
static int cptime[13][2];

static int dtime(int i)
{
	struct timespec time;

	time = timespec_sub(ptime[i], ptime[i-1]);
	return time.tv_nsec/1000000;
}

#define PEOFFTIME 40
#define PEONTIME 90

static int bq2589x_set_pep20_current_pattern(struct charger_device *chg_dev,
	u32 chr_vol)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);
	int ret;
	int value;
	int i, j = 0;
	int flag;

	bq2589x_set_input_volt_limit(bq, BQ2589X_INP_VOL_4V5);
	bq2589x_set_chargecurrent(bq, BQ2589X_CHG_CURR_512MA);
	bq2589x_enable_ico(bq, false);
	usleep_range(1000, 1200);
	value = (chr_vol - CHG_VOL_5V5_UV) / 500000;
	bq2589x_set_input_current_limit(bq, 0);

	msleep(70);

	get_monotonic_boottime(&ptime[j++]);
	for (i = 4; i >= 0; i--) {
		flag = value & (1 << i);

		if (flag == 0) {
			bq2589x_set_input_current_limit(bq, BQ2589X_INP_CURR_700MA);
			msleep(PEOFFTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				chg_info(
					"charging_set_ta20_current_pattern fail1: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;
			bq2589x_set_input_current_limit(bq, 0);
			msleep(PEONTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEONTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				chg_info(
					"charging_set_ta20_current_pattern fail2: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;

		} else {
			bq2589x_set_input_current_limit(bq, BQ2589X_INP_CURR_700MA);
			msleep(PEONTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEONTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 90 || cptime[j][1] > 115) {
				chg_info(
					"charging_set_ta20_current_pattern fail3: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;
			bq2589x_set_input_current_limit(bq, 0);

			msleep(PEOFFTIME);
			get_monotonic_boottime(&ptime[j]);
			cptime[j][0] = PEOFFTIME;
			cptime[j][1] = dtime(j);
			if (cptime[j][1] < 30 || cptime[j][1] > 65) {
				chg_info(
					"charging_set_ta20_current_pattern fail4: idx:%d target:%d actual:%d\n",
					i, PEOFFTIME, cptime[j][1]);
				return -EIO;
			}
			j++;
		}
	}

	bq2589x_set_input_current_limit(bq, BQ2589X_INP_CURR_700MA);
	msleep(160);
	get_monotonic_boottime(&ptime[j]);
	cptime[j][0] = 160;
	cptime[j][1] = dtime(j);
	if (cptime[j][1] < 150 || cptime[j][1] > 240) {
		chg_info(
			"charging_set_ta20_current_pattern fail5: idx:%d target:%d actual:%d\n",
			i, PEOFFTIME, cptime[j][1]);
		return -EIO;
	}
	j++;

	bq2589x_set_input_current_limit(bq, 0);
	msleep(30);
	bq2589x_set_input_current_limit(bq, BQ2589X_INP_CURR_700MA);

	chg_info(
	"[charging_set_ta20_current_pattern]:chr_vol:%d bit:%d time:%3d %3d %3d %3d %3d %3d %3d %3d %3d %3d %3d!!\n",
	chr_vol, value,
	cptime[1][0], cptime[2][0], cptime[3][0], cptime[4][0], cptime[5][0],
	cptime[6][0], cptime[7][0], cptime[8][0], cptime[9][0], cptime[10][0], cptime[11][0]);

	chg_info(
	"[charging_set_ta20_current_pattern2]:chr_vol:%d bit:%d time:%3d %3d %3d %3d %3d %3d %3d %3d %3d %3d %3d!!\n",
	chr_vol, value,
	cptime[1][1], cptime[2][1], cptime[3][1], cptime[4][1], cptime[5][1],
	cptime[6][1], cptime[7][1], cptime[8][1], cptime[9][1], cptime[10][1], cptime[11][1]);

	bq2589x_enable_ico(bq, true);

	bq2589x_set_input_current_limit(bq, BQ2589X_INP_CURR_3250MA);

	bq->pre_current_ma = -1;
	return 0;
}

static int bq2589x_set_pep20_reset(struct charger_device *chg_dev)
{
	struct bq2589x *bq = dev_get_drvdata(&chg_dev->dev);

	bq2589x_set_input_volt_limit(bq, BQ2589X_INP_VOL_4V5);
	bq2589x_set_chargecurrent(bq, BQ2589X_CHG_CURR_512MA);

	bq2589x_enable_ico(bq, false);

	bq2589x_set_input_current_limit(bq, 0);

	msleep(250);

	bq2589x_set_input_current_limit(bq, BQ2589X_INP_CURR_700MA);

	bq2589x_enable_ico(bq, true);
	bq->pre_current_ma = -1;

	return 0;
}
#endif  /* CONFIG_OPLUS_CHARGER_MTK6765R */

static bool oplus_usbtemp_condition(void) {
	if ((g_bq && g_bq->power_good)
		||(g_oplus_chip && g_oplus_chip->charger_exist)) {
		return true;
	} else {
		return false;
	}
}

static struct charger_ops bq2589x_chg_ops = {
	/* Normal charging */
	.plug_in = bq2589x_plug_in,
	.plug_out = bq2589x_plug_out,
	.dump_registers = bq2589x_dump_register,
	.enable = bq2589x_charging,
	.is_enabled = bq2589x_is_charging_enable,
	.get_charging_current = bq2589x_get_ichg,
	.set_charging_current = bq2589x_set_ichg,
	.get_input_current = bq2589x_get_icl,
	.set_input_current = bq2589x_set_icl,
	.get_constant_voltage = bq2589x_get_vchg,
	.set_constant_voltage = bq2589x_set_vchg,
	.kick_wdt = bq2589x_kick_wdt,
	.set_mivr = bq2589x_set_ivl,
	.is_charging_done = bq2589x_is_charging_done,
	.get_min_charging_current = bq2589x_get_min_ichg,

	/* Safety timer */
	.enable_safety_timer = bq2589x_set_safety_timer,
	.is_safety_timer_enabled = bq2589x_is_safety_timer_enabled,

	/* Power path */
	.enable_powerpath = NULL,
	.is_powerpath_enabled = NULL,

	.enable_chg_type_det = bq2589x_enable_chgdet,
	/* OTG */
	.enable_otg = bq2589x_set_otg,
	.set_boost_current_limit = bq2589x_set_boost_ilmt,
	.enable_discharge = NULL,
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
	/* PE+/PE+20 */
	.send_ta_current_pattern = NULL,
	.set_pe20_efficiency_table = bq2589x_set_pep20_efficiency_table,
	.send_ta20_current_pattern = bq2589x_set_pep20_current_pattern,
	.reset_ta = bq2589x_set_pep20_reset,
	.enable_cable_drop_comp = NULL,
#endif /* CONFIG_OPLUS_CHARGER_MTK6765R */

	/* ADC */
	.get_tchg_adc = get_chgntc_adc_temp,
	.get_vbus_adc = bq2589x_get_vbus_adc,
};

void oplus_bq2589x_dump_registers(void)
{
	bq2589x_dump_regs(g_bq);

	if((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->kick_wdt();
        g_oplus_chip->sub_chg_ops->dump_registers();
	}
}

int oplus_bq2589x_kick_wdt(void)
{
	return bq2589x_reset_watchdog_timer(g_bq);
}

void oplus_bq2589x_set_mivr(int vbatt)
{
	if(g_bq->hw_aicl_point == 4400 && vbatt > BATT_VOL_4V25) {
		g_bq->hw_aicl_point = 4500;
	} else if (g_bq->hw_aicl_point == 4500 && vbatt < BATT_VOL_4V15) {
		g_bq->hw_aicl_point = 4400;
	}

	bq2589x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);
	if (g_oplus_chip->is_double_charger_support) {
		g_oplus_chip->sub_chg_ops->set_aicl_point(vbatt);
	}
}

void oplus_bq2589x_set_mivr_by_battery_vol(void)
{
	u32 mV = 0;
	int vbatt = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip) {
		vbatt = chip->batt_volt;
	}

	if(vbatt > BATT_VOL_4V3) {
		mV = vbatt + 400;
	} else if (vbatt > BATT_VOL_4V2) {
		mV = vbatt + 300;
	} else {
		mV = vbatt + 200;
	}

	if(mV < BATT_VOL_4V4)
		mV = BATT_VOL_4V4;

	bq2589x_set_input_volt_limit(g_bq, mV);
}

static int usb_icl[] = {
	100, 500, 900, 1200, 1500, 1750, 2000, 3000,
};

int oplus_bq2589x_set_aicr(int current_ma)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int aicl_point_temp = 0;
	int main_cur = 0;
	int slave_cur = 0;
	g_bq->pre_current_ma = current_ma;
	g_bq->aicr = current_ma;

	if (g_bq->is_force_aicl) {
		chg_info("is_force_aicl current_ma = %d mA", current_ma);
		return 0;
	}

	if (chip && chip->is_double_charger_support) {
		rc = chip->sub_chg_ops->charging_disable();
		if (rc < 0) {
			chg_err("disable sub charging failed");
		}
		chg_debug("disable subchg ");
	}

	if (chip) {
		chg_debug("usb input max current limit=%d, em_mode = %d", current_ma, chip->em_mode);
        }

	if (chip && chip->is_double_charger_support) {
		chg_vol = mt6357_get_vbus_voltage();
		if (chg_vol > CHG_VOL_7V6) {
			aicl_point_temp = aicl_point = 7600;
		} else {
			if (chip->batt_volt > BATT_VOL_4V1)
				aicl_point_temp = aicl_point = 4550;
			else
				aicl_point_temp = aicl_point = 4500;
		}
	} else {
		if (chip->batt_volt > BATT_VOL_4V1)
			aicl_point_temp = aicl_point = 4550;
		else
			aicl_point_temp = aicl_point = 4500;
	}

	chg_info("usb input max current limit=%d, aicl_point_temp=%d", current_ma, aicl_point_temp);
	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	bq2589x_disable_enlim(g_bq);

	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		pr_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1500 */
	aicl_point_temp = aicl_point + 50;
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(120);
	chg_vol = mt6357_get_vbus_voltage();
	if (chg_vol < aicl_point_temp) {
		i = i - 2; /* We DO NOT use 1.2A here */
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 1; /* We use 1.2A here */
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 5; /* 1750 */
	aicl_point_temp = aicl_point + 50;
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(120);
	chg_vol = mt6357_get_vbus_voltage(); /* bq2589x_adc_read_vbus_volt(g_bq); */
	if (chg_vol < aicl_point_temp) {
		i = i - 2; /* 1.2 */
		goto aicl_pre_step;
	}

	i = 6; /* 2000 */
	aicl_point_temp = aicl_point;
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	if (chg_vol < aicl_point_temp) {
		i =  i - 2; /* 1.5 */
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 7; /* 3000 */
	bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	msleep(90);
	chg_vol = mt6357_get_vbus_voltage(); /* bq2589x_adc_read_vbus_volt(g_bq); */
	if (chg_vol < aicl_point_temp) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	if ((chip->is_double_charger_support)
		&& (chip->slave_charger_enable || chip->em_mode)) {
		slave_cur = (usb_icl[i] * g_oplus_chip->slave_pct)/100;
		slave_cur -= slave_cur % 100;
		main_cur = usb_icl[i] - slave_cur;
		chg_debug("aicl: main_cur: %d,  slave_cur: %d", main_cur, slave_cur);
		bq2589x_set_input_current_limit(g_bq, main_cur);

		if (chip->sub_chg_ops && chip->sub_chg_ops->input_current_write_without_aicl) {
			chip->sub_chg_ops->input_current_write_without_aicl(slave_cur);
		}

		if (chip->sub_chg_ops && chip->sub_chg_ops->charging_enable) {
			chip->sub_chg_ops->charging_enable();
		}

		if (chip->em_mode) {
			chip->slave_charger_enable = true;
			oplus_bq2589x_set_ichg(3400);
		}
	} else {
		bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	}

	chg_info("aicl_pre_step: current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d, main %d mA, slave %d mA, slave_charger_enable:%d\n",
		chg_vol, i, usb_icl[i], aicl_point_temp,
		main_cur, slave_cur,
		chip->slave_charger_enable);
	return rc;
aicl_end:
	if ((g_oplus_chip->is_double_charger_support)
		&& (chip->slave_charger_enable || chip->em_mode)) {
		slave_cur = (usb_icl[i] * g_oplus_chip->slave_pct)/100;
		slave_cur -= slave_cur % 100;
		main_cur = usb_icl[i] - slave_cur;
		chg_debug("aicl: main_cur: %d,  slave_cur: %d", main_cur, slave_cur);
		bq2589x_set_input_current_limit(g_bq, main_cur);

		if (chip->sub_chg_ops && chip->sub_chg_ops->input_current_write_without_aicl) {
			chip->sub_chg_ops->input_current_write_without_aicl(slave_cur);
		}
		if (chip->sub_chg_ops && chip->sub_chg_ops->charging_enable) {
			chip->sub_chg_ops->charging_enable();
		}

		if (chip->em_mode) {
			chip->slave_charger_enable = true;
			oplus_bq2589x_set_ichg(3400);
		}
	} else {
		bq2589x_set_input_current_limit(g_bq, usb_icl[i]);
	}

	g_bq->is_force_aicl = false;
	chg_info("aicl_end: current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d, main %d mA, slave %d mA, slave_charger_enable:%d\n",
		chg_vol, i, usb_icl[i], aicl_point_temp,
		main_cur, slave_cur,
		chip->slave_charger_enable);
	return rc;
}

void oplus_bq2589x_safe_calling_status_check()
{
	if(g_oplus_chip == NULL) {
		return;
	}
	if((g_oplus_chip->charger_volt > CHG_VOL_7V5) && (g_oplus_chip->calling_on)) {
		if(g_bq->hvdcp_can_enabled == true) {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
			g_bq->calling_on = g_oplus_chip->calling_on;
			g_bq->hvdcp_can_enabled = false;
			chg_info("%s:calling is on, disable hvdcp\n", __func__);
		}
	} else if ((g_bq->calling_on) && (g_oplus_chip->calling_on == false)) {
		if((g_oplus_chip->ui_soc < SOC_90) || (g_oplus_chip->batt_volt < BATT_VOL_4V25)) {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			g_bq->hvdcp_can_enabled = true;
			chg_info("%s:calling is off, enable hvdcp\n", __func__);
		}
		g_bq->calling_on = g_oplus_chip->calling_on;
	}
}

void oplus_bq2589x_safe_camera_status_check()
{
	if(g_oplus_chip == NULL) {
		return;
	}
	if((g_oplus_chip->charger_volt > CHG_VOL_7V5) && (g_oplus_chip->camera_on)) {
		if(g_bq->hvdcp_can_enabled == true) {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
			g_bq->camera_on = g_oplus_chip->camera_on;
			g_bq->hvdcp_can_enabled = false;
			chg_info("%s:Camera is on, disable hvdcp\n", __func__);
		}
	} else if ((g_bq->camera_on) && (g_oplus_chip->camera_on == false)) {
		if((g_oplus_chip->ui_soc < SOC_90) || (g_oplus_chip->batt_volt < BATT_VOL_4V25)) {
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			g_bq->hvdcp_can_enabled = true;
			chg_info("%s:Camera is off, enable hvdcp\n", __func__);
		}
		g_bq->camera_on = g_oplus_chip->camera_on;
	}
}

int oplus_bq2589x_set_input_current_limit(int current_ma)
{
	struct timespec diff;
	unsigned int ms = 0;
	int cur_ma = current_ma;

	if (g_bq == NULL)
		return 0;
	chg_debug(" current = %d\n", current_ma);
	oplus_bq2589x_safe_camera_status_check();
	oplus_bq2589x_safe_calling_status_check();
	oplus_bq2589x_set_aicr(current_ma);
	return 0;
}

int oplus_bq2589x_set_cv(int cur)
{
	return bq2589x_set_chargevolt(g_bq, cur);
}

int oplus_bq2589x_set_ieoc(int cur)
{
	return bq2589x_set_term_current(g_bq, cur);
}

int oplus_bq2589x_charging_enable(void)
{
	return bq2589x_enable_charger(g_bq);
}

int oplus_bq2589x_charging_disable(void)
{
	chg_debug(" disable");

	bq2589x_disable_watchdog_timer(g_bq);
	g_bq->pre_current_ma = -1;
	g_bq->hw_aicl_point = 4400;
	bq2589x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);

	return bq2589x_disable_charger(g_bq);
}

int oplus_bq2589x_hardware_init(void)
{
	int ret = 0;

	chg_info(" init ");

	g_bq->hw_aicl_point = 4400;
	bq2589x_set_input_volt_limit(g_bq, g_bq->hw_aicl_point);

	/* Enable charging */
	if (strcmp(g_bq->chg_dev_name, "primary_chg") == 0) {
		ret = bq2589x_enable_charger(g_bq);
		if (ret < 0) {
			dev_notice(g_bq->dev, "%s: en chg failed\n", __func__);
		} else {
			bq2589x_set_input_current_limit(g_bq, BQ2589X_INP_CURR_500MA);
		}
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		if(!bq2589x_is_usb(g_bq)) {
			g_oplus_chip->sub_chg_ops->hardware_init();
		} else {
			g_oplus_chip->sub_chg_ops->charging_disable();
		}
	}

	return ret;
}

int oplus_bq2589x_is_charging_enabled(void)
{
	return g_bq->charge_enabled;
}

int oplus_bq2589x_is_charging_done(void)
{
	int ret = 0;
	bool done;

	bq2589x_check_charge_done(g_bq, &done);

	return done;
}

int oplus_bq2589x_enable_otg(void)
{
	int ret = 0;

	ret = bq2589x_set_boost_current(g_bq, g_bq->platform_data->boosti);
	ret = bq2589x_enable_otg(g_bq);

	if (ret < 0) {
		dev_notice(g_bq->dev, "%s en otg fail(%d)\n", __func__, ret);
		return ret;
	}

	g_bq->otg_enable = true;
	return ret;
}

int oplus_bq2589x_disable_otg(void)
{
	int ret = 0;

	ret = bq2589x_disable_otg(g_bq);

	if (ret < 0) {
		dev_notice(g_bq->dev, "%s disable otg fail(%d)\n", __func__, ret);
		return ret;
	}

	g_bq->otg_enable = false;
	return ret;
}

int oplus_bq2589x_disable_te(void)
{
	return  bq2589x_enable_term(g_bq, false);
}

int oplus_bq2589x_get_chg_current_step(void)
{
	return BQ2589X_ICHG_LSB;
}

int oplus_bq2589x_get_charger_type(void)
{
	return g_bq->oplus_chg_type;
}

int oplus_bq2589x_charger_suspend(void)
{
	if (g_bq)
		bq2589x_enter_hiz_mode(g_bq);
	if ((g_oplus_chip) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->slave_charger_enable = false;
		g_oplus_chip->sub_chg_ops->charger_suspend();
	}
	printk("%s\n", __func__);
	return 0;
}

int oplus_bq2589x_charger_unsuspend(void)
{
	if (g_bq) {
		bq2589x_exit_hiz_mode(g_bq);
	}

	if ((g_oplus_chip != NULL) && (g_oplus_chip->is_double_charger_support)) {
		g_oplus_chip->sub_chg_ops->charger_unsuspend();
	}
	printk("%s\n", __func__);
	return 0;
}

int oplus_bq2589x_set_rechg_vol(int vol)
{
	return 0;
}

int oplus_bq2589x_reset_charger(void)
{
	return 0;
}

bool oplus_bq2589x_check_charger_resume(void)
{
	return true;
}

void oplus_bq2589x_set_chargerid_switch_val(int value)
{
	return;
}

int oplus_bq2589x_get_chargerid_switch_val(void)
{
	return 0;
}

int oplus_bq2589x_get_chargerid_volt(void)
{
	return 0;
}

bool oplus_bq2589x_check_chrdet_status(void)
{
	bool status = false;
#ifdef CONFIG_TCPC_CLASS
        if (g_bq->tcpc) {
                if (tcpm_inquire_typec_attach_state(g_bq->tcpc) == TYPEC_ATTACHED_SRC) {
                        chg_info(" OTG online.");
                        return false;
                }
        }
#endif
	if (mt6357_chrdet_status()) {
		status = mt6357_get_vbus_status();
	}
	return status;
}

static int oplus_bq2589x_get_charger_subtype(void)
{
	if ((bq2589x_is_hvdcp(g_bq) || g_bq->hvdcp_can_enabled)
		&& (!disable_qc)) {
		return CHARGER_SUBTYPE_QC;
	} else {
		return CHARGER_SUBTYPE_DEFAULT;
	}
}

bool oplus_bq2589x_need_to_check_ibatt(void)
{
	return false;
}

int oplus_bq2589x_get_dyna_aicl_result(void)
{
	int ma = 0;

	bq2589x_read_idpm_limit(g_bq, &ma);
	return ma;
}

void vol_convert_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int retry = 100;
	u32 icharging = 0;

	if (!g_bq->pdqc_setup_5v) {
		if (mt6357_get_vbus_voltage() < 5800) {
			/*protect the charger: enable ilim.*/
			bq2589x_enable_enlim(g_bq);
			icharging = oplus_get_charger_current();
			if (icharging < 500) {
				icharging = 500;
			}

			/*Fix 11V3A oplus charger can't change to 9V after back to normal temperature.*/
			chg_info("wait charger respond");
			oplus_bq2589x_set_ichg(100);
			msleep(1500);

			bq2589x_enable_hvdcp(g_bq);
			bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			Charger_Detect_Init();
			oplus_for_cdp();
			g_bq->is_force_aicl = true;
			g_bq->is_retry_bc12 = true;
			bq2589x_force_dpdm(g_bq, true);
			g_bq->hvdcp_checked = false;
			msleep(50);
			while(retry--) {
				if (mt6357_get_vbus_voltage() > 7600) {
					chg_info("set_to_9v success\n");
					break;
				}
				msleep(50);
			}
			chg_info("set_to_9v complete.\n");
			g_bq->is_force_aicl = false;
			bq2589x_disable_enlim(g_bq);
			oplus_bq2589x_set_ichg(icharging);
		}
	} else {
		if (mt6357_get_vbus_voltage() > MT6357_VBUS_VOL_7V5) {
			/*protect the charger: enable ilim.*/
			bq2589x_enable_enlim(g_bq);
			bq2589x_disable_hvdcp(g_bq);
			Charger_Detect_Init();
			oplus_for_cdp();
			g_bq->is_force_aicl = true;
			g_bq->is_retry_bc12 = true;
			bq2589x_force_dpdm(g_bq, true);
			msleep(50);
			while(retry--) {
				if (mt6357_get_vbus_voltage() < MT6357_VBUS_VOL_5V8) {
					chg_info("set_to_5v success");
					break;
				}
				msleep(50);
			}
			chg_info("set_to_5v complete, set is_force_aicl as false.\n");
			g_bq->is_force_aicl = false;
			bq2589x_disable_enlim(g_bq);
		}
	}
	g_bq->is_bc12_end = true;

	return;
}

int oplus_bq2589x_set_qc_config(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct mtk_charger *info = NULL;
	static int qc_to_9v_count = 0;
	int ret = 0;

	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info) {
		chg_info("%s:error\n", __func__);
		return -1;
	}

	if (!chip) {
		chg_info("%s: error\n", __func__);
		return -1;
	}

	if(disable_qc) {
		chg_info("%s:disable_QC\n", __func__);
		return -1;
	}

	if(g_bq->disable_hight_vbus == 1) {
		chg_info("%s:disable_hight_vbus\n", __func__);
		return -1;
	}

	if(chip->charging_state == CHARGING_STATUS_FAIL) {
		chg_info("%s:charging_status_fail\n", __func__);
		return -1;
	}

	chg_info("temperature %d, tbatt_pdqc_to_5v_thr %d, charger_volt %d, batt_volt %d, ui_soc %d, \
		icharging %d, \
		vbatt_pdqc_to_5v_thr %d, \
		cool_down_force_5v %d\n",
		chip->temperature,
		chip->limits.tbatt_pdqc_to_5v_thr,
		chip->charger_volt,
		chip->batt_volt,
		chip->ui_soc,
		chip->icharging,
		chip->limits.vbatt_pdqc_to_5v_thr,
		chip->cool_down_force_5v);
	if (chip->cool_down_force_5v
		|| ((chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) && (chip->ui_soc >= SOC_90))
		|| ((chip->limits.tbatt_pdqc_to_5v_thr > 0) && (chip->temperature > chip->limits.tbatt_pdqc_to_5v_thr))) {
		if (mt6357_get_vbus_voltage() > MT6357_VBUS_VOL_7V5) {
			chg_info(" start set qc to 5V");
			g_bq->pdqc_setup_5v = true;
			qc_to_9v_count = 0;
			if (g_bq->is_bc12_end) {
                                g_bq->is_bc12_end = false;
				schedule_delayed_work(&g_bq->bq2589x_vol_convert_work, 0);
			}
		}
	} else { /* 9v */
		if (mt6357_get_vbus_voltage() < MT6357_VBUS_VOL_7V5) {
			chg_info("start set to 9V. count (%d)", qc_to_9v_count);
			g_bq->pdqc_setup_5v = false;

			/*Fix HW-050450C00 HVDCP BUG.*/
			if (qc_to_9v_count >= 3) {
				chg_err("set hvdcp_can_enabled as false and disable hvdcp");
				g_bq->hvdcp_can_enabled = false;
				bq2589x_disable_hvdcp(g_bq);
				ret = -1;
			} else {
				if (g_bq->is_bc12_end) {
					g_bq->is_bc12_end = false;
					schedule_delayed_work(&g_bq->bq2589x_vol_convert_work, 0);
				}
				qc_to_9v_count++;
			}
		} else {
			qc_to_9v_count = 0;
		}
	}

	return ret;
}

int oplus_bq2589x_enable_qc_detect(void)
{
	return 0;
}

bool oplus_bq2589x_need_retry_aicl(void)
{
	static bool connect = false;
	if (!g_bq)
		return false;

	if (g_bq->boot_mode == META_BOOT && !connect) {
		if(g_oplus_chip->chg_ops->get_charger_volt() > CHG_VOL_4V4) {
			g_bq->chg_type = STANDARD_HOST;
			g_bq->oplus_chg_type = POWER_SUPPLY_TYPE_USB;
			g_oplus_chip->charger_type = POWER_SUPPLY_TYPE_USB;
			g_oplus_chip->chg_ops->usb_connect();
			connect = true;
		}
	}
	if (g_bq->cdp_retry_aicl) {
		g_bq->cdp_retry_aicl = false;
		chg_err("retry aicl\n");
		return true;
	}
	return g_bq->cdp_retry_aicl;
}

#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
static bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio)) {
		return true;
	}

	return false;
}

static bool oplus_bq2589x_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = true;
	struct oplus_chg_chip *chip = g_oplus_chip;
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return shortc_hw_status;
	}

	if (oplus_shortc_check_is_gpio(chip) == true) {
		shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else /* CONFIG_OPLUS_SHORT_HW_CHECK */
static bool oplus_bq2589x_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = true;
	return shortc_hw_status;
}
#endif /* CONFIG_OPLUS_SHORT_HW_CHECK */

int oplus_bq2589x_chg_set_high_vbus(bool en)
{
	int subtype;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		chg_info("%s: error\n", __func__);
		return false;
	}

	if(en) {
		g_bq->disable_hight_vbus = 0;
		if(chip->charger_volt >CHG_VOL_7V5) {
			chg_info("%s:charger_volt already 9v\n", __func__);
			return false;
		}

		if (g_bq->pdqc_setup_5v) {
			chg_info("%s:pdqc already setup5v no need 9v\n", __func__);
			return false;
		}
	} else {
		g_bq->disable_hight_vbus = 1;
		if(chip->charger_volt < CHG_VOL_5V4) {
			chg_info("%s:charger_volt already 5v\n", __func__);
			return false;
		}
	}

	subtype = oplus_bq2589x_get_charger_subtype();
	if(subtype == CHARGER_SUBTYPE_QC) {
		if (en) {
			if (!(g_bq->is_bq2589x)) {
				bq2589x_enable_hvdcp(g_bq);
				bq2589x_force_dpdm(g_bq, true);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			}
			chg_info("%s:QC Force output 9V\n", __func__);
		} else {
			if (!(g_bq->is_bq2589x)) {
				bq2589x_disable_hvdcp(g_bq);
				bq2589x_force_dpdm(g_bq, true);
			} else {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_5V);
			}
			chg_info("%s: set qc to 5V", __func__);
		}
	} else {
		chg_info("%s:do nothing\n", __func__);
	}

	return false;
}

int oplus_charger_type_thread(void *data)
{
	int ret;

	u8 reg_val = 0;
	int vbus_stat = 0;
	enum charger_type chg_type = CHARGER_UNKNOWN;
	struct bq2589x *bq = (struct bq2589x *) data;
	int re_check_count = 0;

	struct sched_param param = {.sched_priority = MAX_RT_PRIO-1};
	sched_setscheduler(current, SCHED_FIFO, &param);
	while (1) {
		wait_event_interruptible(oplus_chgtype_wq, bq->chg_need_check == true);
		re_check_count = 0;
		bq->chg_start_check = true;
		bq->chg_need_check = false;
RECHECK:
		ret = bq2589x_read_byte(bq, BQ2589X_REG_0B, &reg_val);
		if (ret)
			break;

		vbus_stat = (reg_val & BQ2589X_VBUS_STAT_MASK);
		vbus_stat >>= BQ2589X_VBUS_STAT_SHIFT;
		bq->vbus_type = vbus_stat;

		switch (vbus_stat) {
		case BQ2589X_VBUS_TYPE_NONE:
			bq->chg_type = CHARGER_UNKNOWN;
			bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			msleep(20);
			re_check_count++;
			if(re_check_count == 75) {
				bq->chg_type = CHARGER_UNKNOWN;
				goto RECHECK;
			} else if (bq->power_good) {
				goto RECHECK;
			}
		case BQ2589X_VBUS_TYPE_SDP:
			bq->chg_type = STANDARD_HOST;
			if (!bq->sdp_retry) {
				bq->sdp_retry = true;
				schedule_delayed_work(&g_bq->bq2589x_retry_adapter_detection, OPLUS_BC12_RETRY_TIME);
			}
			break;
		case BQ2589X_VBUS_TYPE_CDP:
			bq->chg_type = CHARGING_HOST;
			if (!bq->cdp_retry) {
				bq->cdp_retry = true;
				schedule_delayed_work(&bq->bq2589x_retry_adapter_detection, OPLUS_BC12_RETRY_TIME_CDP);
			}
			break;
		case BQ2589X_VBUS_TYPE_DCP:
			bq->chg_type = STANDARD_CHARGER;
			break;
		case BQ2589X_VBUS_TYPE_HVDCP:
			bq->chg_type = STANDARD_CHARGER;
			bq->hvdcp_can_enabled = true;
			break;
		case BQ2589X_VBUS_TYPE_UNKNOWN:
			bq->chg_type = NONSTANDARD_CHARGER;
			break;
		case BQ2589X_VBUS_TYPE_NON_STD:
			bq->chg_type = NONSTANDARD_CHARGER;
			break;
		default:
			bq->chg_type = NONSTANDARD_CHARGER;
			break;
		}
		bq2589x_adc_start(bq, true);
	}

	return 0;
}

static void charger_type_thread_init(void)
{
	g_bq->chg_need_check = false;
	charger_type_kthread = kthread_run(oplus_charger_type_thread, g_bq, "chgtype_kthread");
	if (IS_ERR(charger_type_kthread)) {
		chg_err("failed to cread oplus_usbtemp_kthread\n");
	}
}

int oplus_bq2589x_get_pd_type(void)
{
	struct mtk_charger *info = NULL;

	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info) {
		chg_info("%s:error\n", __func__);
		return false;
	}

	if(disable_pd) {
		chg_info("%s:disable_PD\n", __func__);
		return false;
	}

/*
	info->enable_pe_4 = false;
	if(mtk_pdc_check_charger(info))
	{
		return true;
	}
*/

	return false;
}
#define VBUS_VOL_5V	5000
#define VBUS_VOL_9V	9000
#define IBUS_VOL_2A	2000
#define IBUS_VOL_3A	3000
int oplus_bq2589x_pd_setup(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct mtk_charger *info = NULL;
	int vbus = 0, ibus = 0;

	if(g_bq->chg_consumer != NULL)
		info = g_bq->chg_consumer->cm;

	if(!info) {
		chg_info("%s:error\n", __func__);
		return false;
	}

	if (!chip) {
		chg_info("%s: error\n", __func__);
		return false;
	}

	if(disable_pd) {
		chg_info("%s:disable_PD\n", __func__);
		return false;
	}

	if(g_bq->disable_hight_vbus == 1) {
		chg_info("%s:disable_hight_vbus\n", __func__);
		return false;
	}

	if(chip->charging_state == CHARGING_STATUS_FAIL) {
		chg_info("%s:charging_status_fail\n", __func__);
		return false;
	}

	if ((chip->charger_volt > CHG_VOL_7V5) &&
			((chip->temperature >= 450) || (chip->temperature < 0))) {
		vbus = VBUS_VOL_5V;
		ibus = IBUS_VOL_3A;
		oplus_pdc_setup(&vbus, &ibus);
		chg_info("%s: pd set 5V, batt temperature hot or cold\n", __func__);
		return false;
	}

	if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > CHG_VOL_7V5
		&& chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr&&chip->ui_soc >= SOC_85&&chip->icharging > -1000) {
		chg_info("%s: pd set qc to 5V", __func__);
		vbus = VBUS_VOL_5V;
		ibus = IBUS_VOL_3A;
		oplus_pdc_setup(&vbus, &ibus);
		g_bq->pdqc_setup_5v = 1;
	} else {
		chg_info("%s:pd Force output 9V\n", __func__);
		vbus = VBUS_VOL_9V;
		ibus = IBUS_VOL_2A;
		oplus_pdc_setup(&vbus, &ibus);
		g_bq->pdqc_setup_5v = 0;
	}

	return true;
}

#ifndef CONFIG_MTK_BOOT_REASON
enum boot_reason_t get_boot_reason(void)
{
	return BR_UNKNOWN;
}
#else
extern enum boot_reason_t get_boot_reason(void);
#endif

static void oplus_mt_power_off(void)
{
	if (!g_oplus_chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	if (g_oplus_chip->ac_online != true) {
		if(!mt6357_get_vbus_status())
			kernel_power_off();
	} else {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ac_online is true, return!\n", __func__);
	}
}

static void bq2589x_init_work_handler(struct work_struct *work)
{
	int boot_mode = get_boot_mode();
	u8 ret = 0;
	u8 reg_val = 0;

	chg_debug("boot_mode = %d", boot_mode);

	if ((boot_mode != META_BOOT) && mt6357_get_vbus_status() && !g_oplus_chip->ac_online) {
		chg_info("USB is inserted!");
		mutex_lock(&g_bq->chgdet_en_lock);
#ifdef CONFIG_TCPC_CLASS
		if (tcpm_inquire_typec_attach_state(g_bq->tcpc) != TYPEC_ATTACHED_SRC)
#endif
		 {
			ret = bq2589x_read_byte(g_bq, BQ2589X_REG_0B, &reg_val);
			if (0 == ret) {
				g_bq->power_good = !!(reg_val & BQ2589X_PG_STAT_MASK);
			}

			chg_info("USB is inserted, power_good = %d !", g_bq->power_good);

			bq2589x_chgdet_en(g_bq, false);
			msleep(50);

			/*Enable hvdcp to fix PD 45w/65w charger can't identify as QC Charger.*/
			if (!g_bq->is_bq2589x) {
				bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
			}
			bq2589x_enable_hvdcp(g_bq);

			bq2589x_chgdet_en(g_bq, true);
		}

		mutex_unlock(&g_bq->chgdet_en_lock);
	}
	return;
}

static void bq2589x_hvdcp_bc12_work_handler(struct work_struct *work)
{
	if (!g_bq) {
		return;
	}
	chg_info("start hvdcp bc1.2.");
	bq2589x_enable_enlim(g_bq);
	g_bq->hvdcp_checked = true;
	bq2589x_enable_hvdcp(g_bq);
	bq2589x_switch_to_hvdcp(g_bq, HVDCP_9V);
	Charger_Detect_Init();
	oplus_for_cdp();
	bq2589x_force_dpdm(g_bq, true);
	bq2589x_enable_auto_dpdm(g_bq, false);
	return;
}


static void bq2589x_enter_hz_work_handler(struct work_struct *work) {
	chg_debug("enter hz mode for meta boot mode!");
	bq2589x_enter_hiz_mode(g_bq);
}

struct oplus_chg_operations  oplus_chg_bq2589x_ops = {
	.dump_registers = oplus_bq2589x_dump_registers,
	.kick_wdt = oplus_bq2589x_kick_wdt,
	.hardware_init = oplus_bq2589x_hardware_init,
	.charging_current_write_fast = oplus_bq2589x_set_ichg,
	.set_aicl_point = oplus_bq2589x_set_mivr,
	.input_current_write = oplus_bq2589x_set_input_current_limit,
	.float_voltage_write = oplus_bq2589x_set_cv,
	.term_current_set = oplus_bq2589x_set_ieoc,
	.charging_enable = oplus_bq2589x_charging_enable,
	.charging_disable = oplus_bq2589x_charging_disable,
	.get_charging_enable = oplus_bq2589x_is_charging_enabled,
	.charger_suspend = oplus_bq2589x_charger_suspend,
	.charger_unsuspend = oplus_bq2589x_charger_unsuspend,
	.set_rechg_vol = oplus_bq2589x_set_rechg_vol,
	.reset_charger = oplus_bq2589x_reset_charger,
	.read_full = oplus_bq2589x_is_charging_done,
	.otg_enable = oplus_bq2589x_enable_otg,
	.otg_disable = oplus_bq2589x_disable_otg,
	.set_charging_term_disable = oplus_bq2589x_disable_te,
	.check_charger_resume = oplus_bq2589x_check_charger_resume,
	.get_charger_type = oplus_bq2589x_get_charger_type,
	.get_charger_volt = mt6357_get_vbus_voltage,
	.get_chargerid_volt = oplus_bq2589x_get_chargerid_volt,
	.set_chargerid_switch_val = oplus_bq2589x_set_chargerid_switch_val,
	.get_chargerid_switch_val = oplus_bq2589x_get_chargerid_switch_val,
	.check_chrdet_status = oplus_bq2589x_check_chrdet_status,
	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = oplus_get_rtc_ui_soc,
	.set_rtc_soc = oplus_set_rtc_ui_soc,
	.set_power_off = oplus_mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
	.get_chg_current_step = oplus_bq2589x_get_chg_current_step,
	.need_to_check_ibatt = oplus_bq2589x_need_to_check_ibatt,
	.get_dyna_aicl_result = oplus_bq2589x_get_dyna_aicl_result,
	.get_shortc_hw_gpio_status = oplus_bq2589x_get_shortc_hw_gpio_status,
	.oplus_chg_get_pd_type = oplus_bq2589x_get_pd_type,
	.oplus_chg_pd_setup = oplus_bq2589x_pd_setup,
	.get_charger_subtype = oplus_bq2589x_get_charger_subtype,
	.set_qc_config = oplus_bq2589x_set_qc_config,
	.enable_qc_detect = oplus_bq2589x_enable_qc_detect,
	.oplus_chg_set_high_vbus = oplus_bq2589x_chg_set_high_vbus,
	.enable_shipmode = bq2589x_enable_shipmode,
	.oplus_chg_set_hz_mode = bq2589x_set_hz_mode,
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
	.get_usbtemp_volt = oplus_get_usbtemp_burn_volt,
};

static void retry_detection_work_callback(struct work_struct *work)
{
	if (g_bq->sdp_retry || g_bq->cdp_retry || g_bq->retry_hvdcp_algo) {
		Charger_Detect_Init();
		chg_info("usb/cdp start bc1.2 once");
		oplus_for_cdp();
		g_bq->usb_connect_start = true;
		g_bq->is_force_aicl = true;
		g_bq->is_retry_bc12 = true;
		bq2589x_force_dpdm(g_bq, true);
	}
}

static void aicr_setting_work_callback(struct work_struct *work)
{
	int re_check_count = 0;

	if (!g_bq)
                return;

	chg_debug("start.");
	if (g_bq->oplus_chg_type == POWER_SUPPLY_TYPE_USB_DCP && g_bq->is_force_aicl) {
		while (g_bq->is_force_aicl) {
			if (re_check_count++ < 200) {
				msleep(20);
			} else {
				break;
			}
		}
	}
	chg_debug("begin aicr current = %d mA, wait %d mS", g_bq->aicr, re_check_count*20);
	oplus_bq2589x_set_aicr(g_bq->aicr);
	return;
}

int oplus_bq2589x_set_ichg(int cur)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	int ua = 0;
	int main_ua = 0;
	int slave_ua = 0;
	int ret = 0;
	int temp = 0;

	chg_debug(" current = %d ma", cur);
	if (cur > BQ_CHARGER_CURRENT_MAX_MA) {
		cur = BQ_CHARGER_CURRENT_MAX_MA;
	}

	ua = cur * 1000;
	if (chip->is_double_charger_support) {
		if (chip->slave_charger_enable || chip->em_mode) {
			main_ua = ua * (100-chip->slave_pct) / 100;
			ret = bq2589x_set_chargecurrent(g_bq, main_ua/1000);
			if (!ret) {
				ret = _bq2589x_get_ichg(g_bq, &main_ua);
			} else {
				chg_err("set main_ua failed.");
			}
			slave_ua = ua - main_ua;
			if (chip->sub_chg_ops) {
				chg_debug("set main_ua = %d, slave_ua = %d", main_ua, slave_ua);

				/*slave charger: set ichg */
				temp = slave_ua/1000%64;
				if (temp > 32) {
					slave_ua = slave_ua + (64- temp)*1000;
				}

				ret = oplus_bq2560x_set_current(slave_ua/1000);
				if (ret) {
					chg_err("set slave_ua:%d failed", slave_ua);
				}
			}
		} else {
			ret = bq2589x_set_chargecurrent(g_bq, ua/1000);
		}
	} else {
		ret = bq2589x_set_chargecurrent(g_bq, ua/1000);
		if (ret) {
			chg_err("set main ua %d failed", ua);
		}
	}

	chg_info(" current = %d, slave_pct=%d ua = %d, main_ua = %d, slave_ua = %d, slave_charger_enable) = %d\n",
		cur, chip->slave_pct, ua, main_ua, slave_ua, chip->slave_charger_enable);
	return 0;
}

static struct of_device_id bq2589x_charger_match_table[] = {
	{.compatible = "ti,bq25890h", },
	{.compatible = "ti,bq25892", },
	{.compatible = "ti,bq25895", },
	{},
};

MODULE_DEVICE_TABLE(of, bq2589x_charger_match_table);

static const struct i2c_device_id bq2589x_i2c_device_id[] = {
	{ "bq25890h", 0x03 },
	{ "bq25892", 0x00 },
	{ "bq25895", 0x07 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, bq2589x_i2c_device_id);

static int bq2589x_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct bq2589x *bq;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret = 0;

	chg_debug("bq2589x probe enter");
	bq = devm_kzalloc(&client->dev, sizeof(struct bq2589x), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;
	g_bq = bq;

	bq->chg_consumer =
		charger_manager_get_by_name(&client->dev, "bq2589x");

	i2c_set_clientdata(client, bq);
	mutex_init(&bq->i2c_rw_lock);
	mutex_init(&bq->chgdet_en_lock);
	ret = bq2589x_detect_device(bq);
	if (ret) {
		chg_err("No bq2589x device found!\n");
		ret = -ENODEV;
		goto err_nodev;
	}

	bq->platform_data = bq2589x_parse_dt(node, bq);
	if (!bq->platform_data) {
		chg_err("No platform data provided.\n");
		ret = -EINVAL;
		goto err_parse_dt;
	}

	bq2589x_reset_chip(bq);

	ret = bq2589x_init_device(bq);
	if (ret) {
		chg_err("Failed to init device\n");
		goto err_init;
	}

	charger_type_thread_init();
	bq->is_bc12_end = true;

	bq->oplus_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	bq->pre_current_ma = -1;
	bq->chg_start_check = true;
	bq->usb_connect_start = false;
	bq->hvdcp_can_enabled = false;
	bq->sdp_retry = false;
	bq->cdp_retry = false;

	Charger_Detect_Init();
	bq2589x_disable_hvdcp(bq);
	bq2589x_disable_maxc(bq);
	bq2589x_disable_batfet_rst(bq);
	/*Enable AICL for sy6970*/
	bq2589x_enable_ico(bq, true);
	register_charger_devinfo(bq);

	INIT_DELAYED_WORK(&bq->bq2589x_aicr_setting_work, aicr_setting_work_callback);
	INIT_DELAYED_WORK(&bq->bq2589x_vol_convert_work, vol_convert_work);
	INIT_DELAYED_WORK(&bq->bq2589x_retry_adapter_detection, retry_detection_work_callback);
	INIT_DELAYED_WORK(&bq->init_work, bq2589x_init_work_handler);
	INIT_DELAYED_WORK(&bq->bq2589x_hvdcp_bc12_work, bq2589x_hvdcp_bc12_work_handler);

	bq->chg_dev = charger_device_register(bq->chg_dev_name,
					      &client->dev, bq,
					      &bq2589x_chg_ops,
					      &bq2589x_chg_props);
	if (IS_ERR_OR_NULL(bq->chg_dev)) {
		ret = PTR_ERR(bq->chg_dev);
		goto err_device_register;
	}

	bq2589x_register_interrupt(node, bq);

	ret = sysfs_create_group(&bq->dev->kobj, &bq2589x_attr_group);
	if (ret) {
		chg_err("failed to register sysfs. err: %d\n", ret);
		goto err_sysfs_create;
	}

	determine_initial_status(bq);

	if (strcmp(bq->chg_dev_name, "primary_chg") == 0) {
		schedule_delayed_work(&bq->init_work, msecs_to_jiffies(14000));

#ifdef CONFIG_TCPC_CLASS
		bq->tcpc = tcpc_dev_get_by_name("type_c_port0");
		if (!bq->tcpc) {
			chr_err("%s get tcpc device type_c_port0 fail\n", __func__);
		}
#endif
	}

	set_charger_ic(BQ2589X);
	chg_err("bq2589x probe successfully, Part Num:%d, Revision:%d\n!",
	       bq->part_no, bq->revision);

	g_bq->camera_on = false;
	g_bq->calling_on = false;

	return 0;

err_sysfs_create:
	charger_device_unregister(bq->chg_dev);
err_device_register:
err_init:
err_parse_dt:
err_nodev:
	mutex_destroy(&bq->i2c_rw_lock);
	mutex_destroy(&bq->chgdet_en_lock);
	devm_kfree(bq->dev, bq);
	return ret;
}

static int bq2589x_charger_remove(struct i2c_client *client)
{
	struct bq2589x *bq = i2c_get_clientdata(client);

	mutex_destroy(&bq->i2c_rw_lock);
	mutex_destroy(&bq->chgdet_en_lock);

	sysfs_remove_group(&bq->dev->kobj, &bq2589x_attr_group);

	return 0;
}

static void bq2589x_charger_shutdown(struct i2c_client *client)
{
	if ((g_oplus_chip != NULL) && g_bq != NULL) {
		if((g_bq->hvdcp_can_enabled) && (g_oplus_chip->charger_exist)) {
			bq2589x_disable_hvdcp(g_bq);
			bq2589x_force_dpdm(g_bq, true);
			oplus_bq2589x_charging_disable();
		}
	}
}

static struct i2c_driver bq2589x_charger_driver = {
	.driver = {
		   .name = "bq2589x-charger",
		   .owner = THIS_MODULE,
		   .of_match_table = bq2589x_charger_match_table,
		   },

	.probe = bq2589x_charger_probe,
	.remove = bq2589x_charger_remove,
	.shutdown = bq2589x_charger_shutdown,
	.id_table = bq2589x_i2c_device_id,
};

module_i2c_driver(bq2589x_charger_driver);

MODULE_DESCRIPTION("TI BQ2589X Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");
