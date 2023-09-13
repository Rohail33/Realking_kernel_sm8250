// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/log2.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/iio/consumer.h>
#include "../../../../../../kernel/msm-5.4/drivers/usb/typec/tcpc/inc/tcpci.h"
#include "../../../../../../kernel/msm-5.4/drivers/usb/typec/tcpc/inc/tcpm.h"
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/iio/consumer.h>
#include <linux/kthread.h>
#include <linux/qti_power_supply.h>

#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_short.h"
#include "../charger_ic/oplus_short_ic.h"
#include "../charger_ic/op_charge.h"

#include "oplus_mp2650.h"
#include "../gauge_ic/oplus_bq27541.h"
#include "../gauge_ic/oplus_sm5602.h"
#include "../wireless_ic/oplus_ra9530.h"
#include "../oplus_adapter.h"
#include "../oplus_pps.h"
#include "../oplus_configfs.h"
#include "../oplus_chg_ops_manager.h"
#include "../oplus_chg_module.h"
#include "../voocphy/oplus_voocphy.h"
#include <soc/oplus/system/boot_mode.h>
#include "oplus_sy6974b.h"
#include <soc/oplus/system/oplus_project.h>
#include "oplus_battery_sm6375.h"
#include "oplus_discrete_charger.h"
#include <linux/nvmem-consumer.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
int op10_subsys_init(void);
void op10_subsys_exit(void);
int rk826_subsys_init(void);
void rk826_subsys_exit(void);
int rt5125_subsys_init(void);
void rt5125_subsys_exit(void);
int sc8547_subsys_init(void);
void sc8547_subsys_exit(void);
int sc8547_slave_subsys_init(void);
void sc8547_slave_subsys_exit(void);
int sy6974b_charger_init(void);
void sy6974b_charger_exit(void);
int sy6970_charger_init(void);
void sy6970_charger_exit(void);
int sgm41511_charger_init(void);
void sgm41511_charger_exit(void);
int sgm41512_charger_init(void);
void sgm41512_charger_exit(void);
int ra9530_driver_init(void);
void ra9530_driver_exit(void);
int rt_pd_manager_init(void);
void rt_pd_manager_exit(void);
int adapter_ic_init(void);
void adapter_ic_exit(void);
int sgm7220_i2c_init(void);
void sgm7220_i2c_exit(void);

#endif

int typec_dir = 0;

#ifdef OPLUS_FEATURE_CHG_BASIC
/*for p922x compile*/
void __attribute__((weak)) oplus_set_wrx_otg_value(void)
{
	return;
}
int __attribute__((weak)) oplus_get_idt_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_otg_val(void)
{
	return 0;
}
void __attribute__((weak)) oplus_wireless_set_otg_en_val(void)
{
	return;
}
void __attribute__((weak)) oplus_dcin_irq_enable(void)
{
	return;
}

bool __attribute__((weak)) oplus_get_wired_otg_online(void)
{
	return false;
}

bool __attribute__((weak)) oplus_get_wired_chg_present(void)
{
	return false;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#define VBATT_DEFAULT_MV	3800
int __attribute__((weak)) qpnp_get_battery_voltage(void)
{
	return VBATT_DEFAULT_MV;//Not use anymore
}

int __attribute__((weak)) smbchg_get_boot_reason(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_enable_qc_detect(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_set_qc_config(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_get_typec_attach_state(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_cclogic_set_mode(int mode)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_inquire_cc_polarity(void)
{
	return 0;
}

int __attribute__((weak)) oplus_sy6974b_enter_shipmode(bool en)
{
	return 0;
}

bool oplus_is_ptcrb_version(void)
{
	return (get_eng_version() == PTCRB);
}
EXPORT_SYMBOL(oplus_is_ptcrb_version);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
extern void oplus_vooc_get_fastchg_started_pfunc(bool (*pfunc)(void));
extern void oplus_vooc_get_fastchg_ing_pfunc(bool (*pfunc)(void));
#endif

/*----------------------------------------------------------------*/
struct oplus_chg_chip *g_oplus_chip = NULL;
static struct task_struct *oplus_usbtemp_kthread;
struct delayed_work usbtemp_recover_work;
static DEFINE_MUTEX(pd_select_pdo_v);

#define OPLUS_SUPPORT_CCDETECT_IN_FTM_MODE	2
#define OPLUS_SUPPORT_CCDETECT_NOT_FTM_MODE	1
#define OPLUS_NOT_SUPPORT_CCDETECT		0

extern struct oplus_chg_operations  sgm41511_chg_ops;
extern int sy6970_adc_read_charge_current(void);
extern int oplus_usbtemp_monitor_common_new_method(void *data);
static int charger_ic__det_flag = 0;

int get_charger_ic_det(struct oplus_chg_chip *chip)
{
	int count = 0;
	int n = charger_ic__det_flag;

	if (!chip) {
		return charger_ic__det_flag;
	}

	while (n) {
		++ count;
		n = (n - 1) & n;
	}

	chip->is_double_charger_support = count>1 ? true:false;
	chg_err("charger_ic__det_flag:%d\n", charger_ic__det_flag);
	return charger_ic__det_flag;
}

void set_charger_ic(int sel)
{
	charger_ic__det_flag |= 1 << sel;
	return;
}

#define CHG_OPS_LEN 64
static bool is_ext_mp2650_chg_ops(void)
{
	return (strncmp(oplus_chg_ops_name_get(), "ext-mp2650", CHG_OPS_LEN) == 0);
}

static bool is_ext_sy6970_chg_ops(void)
{
	return (strncmp(oplus_chg_ops_name_get(), "ext-sy6970", 64) == 0);
}

static bool is_ext_sy6974b_chg_ops(void)
{
	return (strncmp(oplus_chg_ops_name_get(), "ext-sy6974b", CHG_OPS_LEN) == 0);
}

static bool is_ext_sgm41512_chg_ops(void)
{
	return (strncmp(oplus_chg_ops_name_get(), "ext-sgm41512", CHG_OPS_LEN) == 0);
}


static int oplus_get_iio_channel(struct smb_charger *chg, const char *propname,
					struct iio_channel **chan)
{
	int rc = 0;

	rc = of_property_match_string(chg->dev->of_node,
					"io-channel-names", propname);
	if (rc < 0)
		return 0;

	*chan = iio_channel_get(chg->dev, propname);
	if (IS_ERR(*chan)) {
		rc = PTR_ERR(*chan);
		if (rc != -EPROBE_DEFER)
			chg_err("%s channel unavailable, %d\n", propname, rc);
		*chan = NULL;
	}

	return rc;
}

static int oplus_parse_dt_adc_channels(struct smb_charger *chg)
{
	int rc = 0;

	rc = oplus_get_iio_channel(chg, "chgid_v_chan", &chg->iio.chgid_v_chan);
	if (rc < 0)
		return rc;

	rc = oplus_get_iio_channel(chg, "usbtemp_r_v_chan", &chg->iio.usbtemp_r_v_chan);
	if (rc < 0)
		return rc;

	rc = oplus_get_iio_channel(chg, "usbtemp_l_v_chan", &chg->iio.usbtemp_l_v_chan);
	if (rc < 0)
		return rc;

	rc = oplus_get_iio_channel(chg, "batbtb_temp_chan", &chg->iio.batbtb_temp_chan);
	if (rc < 0)
		return rc;

	rc = oplus_get_iio_channel(chg, "usbbtb_temp_chan", &chg->iio.usbbtb_temp_chan);
	if (rc < 0)
		return rc;

	rc = oplus_get_iio_channel(chg, "subboard_temp_chan", &chg->iio.subboard_temp_chan);
	if (rc < 0)
		return rc;

	return 0;
}

#define ERROR_BATT_TEMP -400
#define DEFAULT_SUBBOARD_TEMP	250
#define DIV_FACTOR_DECIDEGC	100
int oplus_get_subboard_temp(void)
{
	int rc = 0;
	int temp = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		chg_err("chip is NULL\n");
		temp = DEFAULT_SUBBOARD_TEMP;
		goto done;
	}

	if (oplus_gauge_get_i2c_err() > 0) {
		return ERROR_BATT_TEMP;;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;
	if (IS_ERR_OR_NULL(chg->iio.subboard_temp_chan)) {
		chg_err("subboard_temp_chan is NULL\n");
		temp = DEFAULT_SUBBOARD_TEMP;
		goto done;
	}

	rc = iio_read_channel_processed(chg->iio.subboard_temp_chan, &temp);
	if (rc < 0) {
		chg_err("Error in reading subboard_temp_chan IIO channel data, rc=%d\n", rc);
		temp = chg->iio.pre_batt_temp;
		goto done;
	}

	temp = temp / DIV_FACTOR_DECIDEGC;
	chg->iio.pre_batt_temp = temp;
done:
	return temp;
}

#define BTBTEMP_DEFAULT_MC	25000
#define UNIT_TRANS_1000		1000
int oplus_chg_get_battery_btb_temp_cal(void)
{
	int rc = 0;
	int temp = BTBTEMP_DEFAULT_MC;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		chg_err("discrete_charger not ready!\n");
		goto done;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->iio.batbtb_temp_chan)) {
		chg_err("chg->iio.batbtb_temp_chan is NULL !\n");
		goto done;
	}

	rc = iio_read_channel_processed(chg->iio.batbtb_temp_chan, &temp);
	if (rc < 0) {
		chg_err("Failed reading bat btb temp over ADC rc=%d\n", rc);
		goto done;
	}

done:
	return temp / UNIT_TRANS_1000;
}

int oplus_chg_get_usb_btb_temp_cal(void)
{
	int rc = 0;
	int temp = BTBTEMP_DEFAULT_MC;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		chg_err("discrete_charger not ready!\n");
		goto done;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->iio.usbbtb_temp_chan)) {
		chg_err("chg->iio.usbbtb_temp_chan is NULL !\n");
		goto done;
	}

	rc = iio_read_channel_processed(chg->iio.usbbtb_temp_chan, &temp);
	if (rc < 0) {
		chg_err("Failed reading usb btb temp over ADC rc=%d\n", rc);
		goto done;
	}

done:
	return temp / UNIT_TRANS_1000;
}

#define SOC_VALID_MASK	0x80
#define SOC_VALUE_MASK	0x7F
#define EMPTY_SOC	0
#define FULL_SOC	100
int oplus_chg_get_shutdown_soc(void)
{
	char *buf;
	size_t len;
	struct smb_charger *chg = NULL;
	char soc = 0;

	if (!g_oplus_chip)
		return 0;

	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;
	if (chg->soc_backup_nvmem) {
		buf = nvmem_cell_read(chg->soc_backup_nvmem, &len);
		if (IS_ERR_OR_NULL(buf)) {
			chg_err("failed to read nvmem cell\n");
			return PTR_ERR(buf);
		}

		if (len <= 0 || len > sizeof(soc)) {
			chg_err("nvmem cell length out of range %d\n", len);
			kfree(buf);
			return -EINVAL;
		}

		memcpy(&soc, buf, min(len, sizeof(soc)));
		if (((soc & SOC_VALID_MASK) == 0)
				|| (soc & SOC_VALUE_MASK) > FULL_SOC) {
			chg_err("soc 0x%02x invalid\n", soc);
			return -EINVAL;
		}

		soc = soc & SOC_VALUE_MASK;
		if (soc == EMPTY_SOC)
			soc = 1;
	}

	return soc;
}

int oplus_chg_backup_soc(int backup_soc)
{
	struct smb_charger *chg = NULL;
	char soc = 0;
	int rc = 0;

	if (!g_oplus_chip)
		return 0;

	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;

	if (backup_soc > FULL_SOC || backup_soc < EMPTY_SOC) {
		chg_err("soc invalid\n");
		return -EINVAL;
	}

	if (chg->soc_backup_nvmem) {
		soc = (backup_soc & SOC_VALUE_MASK) | SOC_VALID_MASK;
		rc = nvmem_cell_write(chg->soc_backup_nvmem, &soc, sizeof(soc));
		if (rc < 0)
			chg_err("store soc fail, rc=%d\n", rc);
	}

	return rc;
}

#ifdef CONFIG_OPLUS_FEATURE_CHG_MISC
extern bool ext_boot_with_console(void);
#endif

extern int oplus_usbtemp_monitor_common(void *data);
extern int oplus_usbtemp_monitor_common_new_method(void *data);

extern void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip);

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_disable(void);
void oplus_ccdetect_enable(void);
int oplus_ccdetect_get_power_role(void);
bool oplus_get_otg_switch_status(void);
bool oplus_ccdetect_support_check(void);
int oplus_get_otg_online_status(void);
bool oplus_get_otg_online_status_default(void);

void oplus_otg_enable_by_buckboost(void)
{
	if (!g_oplus_chip || !(g_oplus_chip->chg_ops->charging_disable) ||
		!(g_oplus_chip->chg_ops->otg_enable))
		return;

	if (g_oplus_chip->chg_ops->get_otg_enable &&
		g_oplus_chip->chg_ops->get_otg_enable()) {
		chg_err("otg already enabled,return");
		return;
	}
	g_oplus_chip->chg_ops->charging_disable();
	g_oplus_chip->chg_ops->otg_enable();
}

void oplus_otg_disable_by_buckboost(void)
{
	if (!g_oplus_chip || !(g_oplus_chip->chg_ops->otg_disable))
		return;

	g_oplus_chip->chg_ops->otg_disable();
}

static int oplus_chg_2uart_pinctrl_init(struct oplus_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return -EINVAL;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->chg_2uart_pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chg->chg_2uart_pinctrl)) {
		chg_err("get 2uart chg_2uart_pinctrl fail\n");
		return -EINVAL;
	}

	chg->chg_2uart_default = pinctrl_lookup_state(chg->chg_2uart_pinctrl, "2uart_active");
	if (IS_ERR_OR_NULL(chg->chg_2uart_default)) {
		chg_err("get chg_2uart_default fail\n");
		return -EINVAL;
	}

	chg->chg_2uart_sleep = pinctrl_lookup_state(chg->chg_2uart_pinctrl, "2uart_sleep");
	if (IS_ERR_OR_NULL(chg->chg_2uart_sleep)) {
		chg_err("get chg_2uart_sleep fail\n");
		return -EINVAL;
	}

#ifdef CONFIG_OPLUS_FEATURE_CHG_MISC
	if (!ext_boot_with_console())
		pinctrl_select_state(chg->chg_2uart_pinctrl, chg->chg_2uart_sleep);
#endif

	return 0;
}

int smbchg_get_chargerid_volt(void)
{
	int rc, chargerid_volt = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return 0;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->iio.chgid_v_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chg->iio.chgid_v_chan  is  NULL !\n", __func__);
		return 0;
	}

	rc = iio_read_channel_processed(chg->iio.chgid_v_chan, &chargerid_volt);
	if (rc < 0) {
		chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		return 0;
	}

	chargerid_volt = chargerid_volt / UNIT_TRANS_1000;
	chg_err("chargerid_volt: %d\n", chargerid_volt);

	return chargerid_volt;
}

static int smbchg_chargerid_switch_gpio_init(struct oplus_chg_chip *chip)
{
	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_active =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)) {
		chg_err("get chargerid_switch_active fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_sleep =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
		chg_err("get chargerid_switch_sleep fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.chargerid_switch_default =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_default");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
		chg_err("get chargerid_switch_default fail\n");
		return -EINVAL;
	}

	if (chip->normalchg_gpio.chargerid_switch_gpio > 0) {
		gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
	}
	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.chargerid_switch_default);

	return 0;
}

void smbchg_set_chargerid_switch_val(int value)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (oplus_vooc_get_adapter_update_real_status() == ADAPTER_FW_NEED_UPDATE
		|| oplus_vooc_get_btb_temp_over() == true) {
		chg_err("adapter update or btb_temp_over, return\n");
		return;
	}

	mutex_lock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);

	if (value) {
		gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
		pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.chargerid_switch_default);
	} else {
		gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
		pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.chargerid_switch_default);
	}

	mutex_unlock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);
	chg_err("set value:%d, gpio_val:%d\n",
		value, gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio));
}

int smbchg_get_chargerid_switch_val(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio);
}

static int oplus_ship_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.pinctrl fail\n");
	}

	chip->normalchg_gpio.ship_active =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ship_sleep =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);

	return 0;
}

static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

#define PWM_COUNT	5
#define PWM_DELAY_MS	3
static void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
#ifndef CONFIG_OPLUS_CHARGER_MTK
	int i = 0;
	struct smb_charger *chg = &chip->pmic_spmi.smb5_chip->chg;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}

	if (oplus_ship_check_is_gpio(chip) == true) {
		chg_debug("select gpio control\n");

		mutex_lock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
		}
		for (i = 0; i < PWM_COUNT; i++) {
			//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 1);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
			mdelay(PWM_DELAY_MS);
			//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 0);
			pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
			mdelay(PWM_DELAY_MS);
		}

		mutex_unlock(&chip->pmic_spmi.smb5_chip->chg.pinctrl_mutex);
		chg_debug("power off after 15s\n");
	} else {
		if(chg->sy6974b_shipmode_enable) {
			oplus_sy6974b_enter_shipmode(g_oplus_chip->enable_shipmode);
		} else if (chip->chg_ops->enable_shipmode != NULL) {
			chip->chg_ops->enable_shipmode(g_oplus_chip->enable_shipmode);
		}
	}
#endif
}

static int oplus_shortc_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.pinctrl fail\n");
	}
	chip->normalchg_gpio.shortc_active =
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "shortc_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
		chg_err("get shortc_active fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.shortc_active);

	return 0;
}

static bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio))
		return true;

	return false;
}

static int oplus_shipmode_id_gpio_init(struct oplus_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->shipmode_id_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chg->shipmode_id_pinctrl)) {
		chg_err("get shipmode_id_pinctrl fail\n");
	}

	chg->shipmode_id_active =
		pinctrl_lookup_state(chg->shipmode_id_pinctrl, "shipmode_id_active");
	if (IS_ERR_OR_NULL(chg->shipmode_id_active)) {
		chg_err("get shipmode_id_active fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chg->shipmode_id_pinctrl, chg->shipmode_id_active);

	return 0;
}

static bool oplus_shipmode_id_check_is_gpio(struct oplus_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (gpio_is_valid(chg->shipmode_id_gpio)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: tongfeng test shipmode_id_gpio true!\n", __func__);
		return true;
	}

	return false;
}

#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
static __maybe_unused bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return shortc_hw_status;
	}

	if (oplus_shortc_check_is_gpio(chip) == true) {
		shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else
static bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	return shortc_hw_status;
}
#endif /* CONFIG_OPLUS_SHORT_HW_CHECK */

int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->ccdetect_pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chg->ccdetect_pinctrl)) {
		chg_err("get ccdetect ccdetect_pinctrl fail\n");
		return -EINVAL;
	}

	chg->ccdetect_active = pinctrl_lookup_state(chg->ccdetect_pinctrl, "ccdetect_active");
	if (IS_ERR_OR_NULL(chg->ccdetect_active)) {
		chg_err("get ccdetect_active fail\n");
		return -EINVAL;
	}

	chg->ccdetect_sleep = pinctrl_lookup_state(chg->ccdetect_pinctrl, "ccdetect_sleep");
	if (IS_ERR_OR_NULL(chg->ccdetect_sleep)) {
		chg_err("get ccdetect_sleep fail\n");
		return -EINVAL;
	}

	if (chg->ccdetect_gpio > 0) {
		gpio_direction_input(chg->ccdetect_gpio);
	}

	pinctrl_select_state(chg->ccdetect_pinctrl, chg->ccdetect_active);

	return 0;
}

static void oplus_ccdetect_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
						ccdetect_work.work);
	int level;

	level = gpio_get_value(chg->ccdetect_gpio);
	if (level != 1) {
		oplus_ccdetect_enable();
		oplus_wake_up_usbtemp_thread();
	} else {
		oplus_chg_clear_abnormal_adapter_var();

		if (g_oplus_chip)
			g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();

		if (oplus_get_otg_switch_status() == false)
			oplus_ccdetect_disable();
		if(g_oplus_chip->usb_status == USB_TEMP_HIGH) {
			schedule_delayed_work(&usbtemp_recover_work, 0);
		}
	}
}

static void oplus_keep_vbus_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
						keep_vbus_work.work);

	chg->keep_vbus_5v = false;
}

void oplus_usbtemp_recover_work(struct work_struct *work)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return;
	}

	oplus_usbtemp_recover_func(g_oplus_chip);
}

void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->ccdetect_irq = gpio_to_irq(chg->ccdetect_gpio);
	printk(KERN_ERR "[OPLUS_CHG][%s]: chg->ccdetect_irq[%d]!\n", __func__, chg->ccdetect_irq);
}

void oplus_ccdetect_enable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	/* set DRP mode */
	if (chg != NULL && chg->tcpc != NULL) {
		pm_wakeup_dev_event(chg->dev, 2000, true);
		tcpm_typec_change_role_postpone(chg->tcpc, TYPEC_ROLE_TRY_SNK, true);
		pr_err("%s: set drp", __func__);
	} else if (chg != NULL && chg->external_cclogic) {
		oplus_chg_cclogic_set_mode(MODE_DRP);
		pr_err("%s: set drp", __func__);
	}
}

void oplus_ccdetect_disable(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (oplus_ccdetect_check_is_gpio(chip) != true)
		return;

	/* set SINK mode */
	if (chg != NULL && chg->tcpc != NULL) {
		pm_wakeup_dev_event(chg->dev, 2000, true);
		tcpm_typec_change_role_postpone(chg->tcpc,TYPEC_ROLE_SNK, true);
		pr_err("%s: set sink", __func__);
	} else if (chg != NULL && chg->external_cclogic) {
		oplus_chg_cclogic_set_mode(MODE_UFP);
		pr_err("%s: set sink", __func__);
	}
}

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip)
{
	struct smb_charger *chg = NULL;
	int boot_mode = get_boot_mode();

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	/* HW engineer requirement */
	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN
			|| boot_mode == MSM_BOOT_MODE__FACTORY)
		return false;

	if (gpio_is_valid(chg->ccdetect_gpio))
		return true;

	return false;
}

#define QC_CHARGER_VOLTAGE_HIGH 7500
#define QC_SOC_HIGH 90
#define QC_TEMP_HIGH 420
bool oplus_chg_check_qchv_condition(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		pr_err("oplus_chip is null\n");
		return false;
	}

	chip->charger_volt = chip->chg_ops->get_charger_volt();
	if (chip->dual_charger_support && chip->charger_volt < QC_CHARGER_VOLTAGE_HIGH
		&& chip->soc < QC_SOC_HIGH && chip->temperature <= QC_TEMP_HIGH && !chip->cool_down_force_5v) {
		return true;
	}

	return false;
}

bool oplus_ccdetect_support_check(void)
{
	struct smb_charger *chg = NULL;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int boot_mode = get_boot_mode();

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: g_oplus_chip not ready!\n", __func__);
		return OPLUS_NOT_SUPPORT_CCDETECT;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;
	if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN
			|| boot_mode == MSM_BOOT_MODE__FACTORY) {
			return OPLUS_SUPPORT_CCDETECT_IN_FTM_MODE;
	}
	if (gpio_is_valid(chg->ccdetect_gpio))
		return OPLUS_SUPPORT_CCDETECT_NOT_FTM_MODE;

	return OPLUS_NOT_SUPPORT_CCDETECT;
}
EXPORT_SYMBOL(oplus_ccdetect_support_check);

#define CCDETECT_DELAY_MS	50
irqreturn_t oplus_ccdetect_change_handler(int irq, void *data)
{
	struct oplus_chg_chip *chip = data;
	struct smb_charger *chg = &chip->pmic_spmi.smb5_chip->chg;

	cancel_delayed_work_sync(&chg->ccdetect_work);
	printk(KERN_ERR "[OPLUS_CHG][%s]: Scheduling ccdetect work!\n", __func__);
	schedule_delayed_work(&chg->ccdetect_work,
			msecs_to_jiffies(CCDETECT_DELAY_MS));
	return IRQ_HANDLED;
}

static void oplus_ccdetect_irq_register(struct oplus_chg_chip *chip)
{
	int ret = 0;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	ret = devm_request_threaded_irq(chip->dev, chg->ccdetect_irq,
			NULL, oplus_ccdetect_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "ccdetect-change", chip);
	if (ret < 0) {
		chg_err("Unable to request ccdetect-change irq: %d\n", ret);
	}
	printk(KERN_ERR "%s: !!!!! irq register\n", __FUNCTION__);

	ret = enable_irq_wake(chg->ccdetect_irq);
	if (ret != 0) {
		chg_err("enable_irq_wake: ccdetect_irq failed %d\n", ret);
	}
}

static bool oplus_usbtemp_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio))
		return true;

	return false;
}

bool oplus_usbtemp_check_is_support(void)
{
	if(oplus_usbtemp_check_is_gpio(g_oplus_chip) == true)
		return true;

	chg_err("dischg return false\n");

	return false;
}

#define USBTEMP_DEFAULT_C 25
#define USBTEMP_DEFAULT_VOLT_VALUE_MV 800
void oplus_get_usbtemp_volt(struct oplus_chg_chip *chip)
{
	int rc, usbtemp_volt = 0;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->iio.usbtemp_r_v_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chg->iio.usbtemp_r_v_chan is NULL !\n", __func__);
		chip->usbtemp_volt_r = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	} else {
		rc = iio_read_channel_processed(chg->iio.usbtemp_r_v_chan, &usbtemp_volt);
		if (rc < 0) {
			chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed get error\n", __func__);
			chip->usbtemp_volt_r = USBTEMP_DEFAULT_VOLT_VALUE_MV;
		} else {
			chip->usbtemp_volt_r = usbtemp_volt / UNIT_TRANS_1000;
		}
	}

	if (IS_ERR_OR_NULL(chg->iio.usbtemp_l_v_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chg->iio.usbtemp_l_v_chan is NULL !\n", __func__);
		chip->usbtemp_volt_l = USBTEMP_DEFAULT_VOLT_VALUE_MV;
	} else {
		rc = iio_read_channel_processed(chg->iio.usbtemp_l_v_chan, &usbtemp_volt);
		if (rc < 0) {
			chg_err("[OPLUS_CHG][%s]: iio_read_channel_processed get error\n", __func__);
			chip->usbtemp_volt_l = USBTEMP_DEFAULT_VOLT_VALUE_MV;
		} else {
			chip->usbtemp_volt_l = usbtemp_volt / UNIT_TRANS_1000;
		}
	}

	//chg_err("usbtemp_volt: %d, %d\n", chip->usbtemp_volt_r, chip->usbtemp_volt_l);
}
EXPORT_SYMBOL(oplus_get_usbtemp_volt);

static int oplus_dischg_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("chip NULL\n");
		return EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_enable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_enable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
		chg_err("get dischg_enable fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_disable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_disable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable)) {
		chg_err("get dischg_disable fail\n");
		return -EINVAL;
	}

	//pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);

	return 0;
}

static bool oplus_chg_get_vbus_status(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	return chip->charger_exist;
}

void oplus_set_typec_sinkonly(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (chg != NULL && chg->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: usbtemp occur otg switch[0]\n", __func__);
		//tcpm_typec_disable_function(chg->tcpc, false);
		chg->tcpc->typec_role_new = TYPEC_ROLE_SRC;
		tcpm_typec_change_role_postpone(chg->tcpc, TYPEC_ROLE_SNK, true);
	} else if (chg != NULL && chg->external_cclogic) {
		sgm7220_set_typec_sinkonly();
	}
}
EXPORT_SYMBOL(oplus_set_typec_sinkonly);

void oplus_set_typec_cc_open(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (chg != NULL && chg->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: usbtemp occur otg switch[0]\n", __func__);
		tcpm_typec_disable_function(chg->tcpc, true);
	} else if (chg != NULL && chg->external_cclogic) {
		sgm7220_set_typec_cc_open();
	}
}
EXPORT_SYMBOL(oplus_set_typec_cc_open);

#define USBTEMP_VBUS_MIN_MV 3000
bool oplus_usbtemp_condition(void)
{
	int level = -1;
	int chg_volt = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if(!chip) {
		return false;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;
	if (!chg || !chg->tcpc) {
		chg_err("chg or tcpc is null\n");
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(g_oplus_chip)){
		level = gpio_get_value(chg->ccdetect_gpio);
		if(level == 1
			|| tcpm_inquire_typec_attach_state(chg->tcpc) == TYPEC_ATTACHED_AUDIO
			|| tcpm_inquire_typec_attach_state(chg->tcpc) == TYPEC_ATTACHED_SRC){
			return false;
		}
		if (oplus_vooc_get_fastchg_ing() != true) {
			chg_volt = chip->chg_ops->get_charger_volt();
			if(chg_volt < USBTEMP_VBUS_MIN_MV) {
				return false;
			}
		}
		return true;
	}
	return oplus_chg_get_vbus_status(chip);
}
EXPORT_SYMBOL(oplus_usbtemp_condition);

static void oplus_usbtemp_thread_init(void)
{
	if (!g_oplus_chip)
		return;

	if (g_oplus_chip->support_usbtemp_protect_v2)
		oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_common_new_method, g_oplus_chip, "usbtemp_kthread");
	else
		oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_common, g_oplus_chip, "usbtemp_kthread");
	if (IS_ERR(oplus_usbtemp_kthread)) {
		chg_err("failed to cread oplus_usbtemp_kthread\n");
	}
}

void oplus_wake_up_usbtemp_thread(void)
{
	if (!g_oplus_chip)
		return;

	if (oplus_usbtemp_check_is_support() == true) {
		g_oplus_chip->usbtemp_check = oplus_usbtemp_condition();
		if (g_oplus_chip->usbtemp_check) {
			if (g_oplus_chip->support_usbtemp_protect_v2)
				wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq_new_method);
			else
				wake_up_interruptible(&g_oplus_chip->oplus_usbtemp_wq);
		}
	}
}

static int oplus_chg_parse_custom_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = &chip->pmic_spmi.smb5_chip->chg;
#endif
	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	if (g_oplus_chip) {
		g_oplus_chip->normalchg_gpio.chargerid_switch_gpio =
				of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
		if (g_oplus_chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
			chg_err("Couldn't read chargerid_switch-gpio rc = %d, chargerid_switch_gpio:%d\n",
					rc, g_oplus_chip->normalchg_gpio.chargerid_switch_gpio);
		} else {
			if (gpio_is_valid(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio)) {
				rc = gpio_request(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio, "charging-switch1-gpio");
				if (rc) {
					chg_err("unable to request chargerid_switch_gpio:%d\n", g_oplus_chip->normalchg_gpio.chargerid_switch_gpio);
				} else {
					smbchg_chargerid_switch_gpio_init(g_oplus_chip);
				}
			}
			chg_err("chargerid_switch_gpio:%d\n", g_oplus_chip->normalchg_gpio.chargerid_switch_gpio);
		}
	}

	if (g_oplus_chip) {
		g_oplus_chip->normalchg_gpio.dischg_gpio = of_get_named_gpio(node, "qcom,dischg-gpio", 0);
		if (g_oplus_chip->normalchg_gpio.dischg_gpio <= 0) {
			chg_err("Couldn't read qcom,dischg-gpio rc=%d, qcom,dischg-gpio:%d\n",
				rc, g_oplus_chip->normalchg_gpio.dischg_gpio);
		} else {
			if (oplus_usbtemp_check_is_support() == true) {
				if (gpio_is_valid(g_oplus_chip->normalchg_gpio.dischg_gpio)) {
					rc = gpio_request(g_oplus_chip->normalchg_gpio.dischg_gpio, "dischg-gpio");
					if (rc) {
						chg_err("unable to request dischg-gpio:%d\n", g_oplus_chip->normalchg_gpio.dischg_gpio);
					} else {
						oplus_dischg_gpio_init(g_oplus_chip);
					}
				}
			}
			chg_err("dischg-gpio:%d\n", g_oplus_chip->normalchg_gpio.dischg_gpio);
		}
	}

	if (g_oplus_chip) {
		g_oplus_chip->normalchg_gpio.ship_gpio =
				of_get_named_gpio(node, "qcom,ship-gpio", 0);
		if (g_oplus_chip->normalchg_gpio.ship_gpio <= 0) {
			chg_err("Couldn't read qcom,ship-gpio rc = %d, qcom,ship-gpio:%d\n",
					rc, g_oplus_chip->normalchg_gpio.ship_gpio);
		} else {
			if (oplus_ship_check_is_gpio(g_oplus_chip) == true) {
				rc = gpio_request(g_oplus_chip->normalchg_gpio.ship_gpio, "ship-gpio");
				if (rc) {
					chg_err("unable to request ship-gpio:%d\n",
							g_oplus_chip->normalchg_gpio.ship_gpio);
				} else {
					oplus_ship_gpio_init(g_oplus_chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", g_oplus_chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("ship-gpio:%d\n", g_oplus_chip->normalchg_gpio.ship_gpio);
		}
	}

	if (g_oplus_chip) {
		g_oplus_chip->normalchg_gpio.shortc_gpio =
				of_get_named_gpio(node, "qcom,shortc-gpio", 0);
		if (g_oplus_chip->normalchg_gpio.shortc_gpio <= 0) {
			chg_err("Couldn't read qcom,shortc-gpio rc = %d, qcom,shortc-gpio:%d\n",
					rc, g_oplus_chip->normalchg_gpio.shortc_gpio);
		} else {
			if (oplus_shortc_check_is_gpio(g_oplus_chip) == true) {
				rc = gpio_request(g_oplus_chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
				if (rc) {
					chg_err("unable to request shortc-gpio:%d\n",
							g_oplus_chip->normalchg_gpio.shortc_gpio);
				} else {
					oplus_shortc_gpio_init(g_oplus_chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", g_oplus_chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("shortc-gpio:%d\n", g_oplus_chip->normalchg_gpio.shortc_gpio);
		}
	}
#ifndef CONFIG_OPLUS_CHARGER_MTK
	if (g_oplus_chip) {
		chg->shipmode_id_gpio =
				of_get_named_gpio(node, "qcom,shipmode-id-gpio", 0);
		if (chg->shipmode_id_gpio <= 0) {
			chg_err("Couldn't read qcom,shipmode-id-gpio rc = %d, qcom,shipmode-id-gpio:%d\n",
					rc, chg->shipmode_id_gpio);
		} else {
			if (oplus_shipmode_id_check_is_gpio(g_oplus_chip) == true) {
				rc = gpio_request(chg->shipmode_id_gpio, "qcom,shipmode-id-gpio");
				if (rc) {
					chg_err("unable to request qcom,shipmode-id-gpio:%d\n",
							chg->shipmode_id_gpio);
				} else {
					oplus_shipmode_id_gpio_init(g_oplus_chip);
					if (rc)
						chg_err("unable to init qcom,shipmode-id-gpio:%d\n", chg->shipmode_id_gpio);
				}
			}
			chg_err("qcom,shipmode-id-gpio:%d\n", chg->shipmode_id_gpio);
		}
	}

	if (chip) {
		chg->ccdetect_gpio = of_get_named_gpio(node, "qcom,ccdetect-gpio", 0);
		if (chg->ccdetect_gpio <= 0) {
			chg_err("Couldn't read qcom,ccdetect-gpio rc=%d, qcom,ccdetect-gpio:%d\n",
					rc, chg->ccdetect_gpio);
		} else {
			if (oplus_ccdetect_check_is_gpio(chip) == true) {
				rc = gpio_request(chg->ccdetect_gpio, "ccdetect-gpio");
				if (rc) {
					chg_err("unable to request ccdetect-gpio:%d\n", chg->ccdetect_gpio);
				} else {
					rc = oplus_ccdetect_gpio_init(chip);
					if (rc)
						chg_err("unable to init ccdetect-gpio:%d\n", chg->ccdetect_gpio);
					else
						oplus_ccdetect_irq_init(chip);
				}
			}
			chg_err("ccdetect-gpio:%d\n", chg->ccdetect_gpio);
		}
	}

	if(g_oplus_chip) {
		chg->sy6974b_shipmode_enable = of_property_read_bool(node, "qcom,use_sy6974b_shipmode");
		chg->external_cclogic = of_property_read_bool(node, "qcom,use_external_cclogic");
		chg->pd_not_rise_vbus_only_5v = of_property_read_bool(node, "qcom,pd_not_rise_vbus_only_5v");
		g_oplus_chip->tbatt_use_subboard_temp = of_property_read_bool(node, "oplus,tbatt_use_subboard_temp");
	}
#endif
	return rc;
}

#define DISCONNECT			0
#define STANDARD_TYPEC_DEV_CONNECT	BIT(0)
#define OTG_DEV_CONNECT			BIT(1)
int oplus_get_typec_cc_orientation(void)
{
	int val = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if(chg != NULL && chg->tcpc != NULL) {
		if (tcpm_inquire_typec_attach_state(chg->tcpc) != TYPEC_UNATTACHED) {
			val = (int)tcpm_inquire_cc_polarity(chg->tcpc) + 1;
		} else {
			val = 0;
		}
		if (val != 0)
			printk(KERN_ERR "[OPLUS_CHG][%s]: cc[%d]\n", __func__, val);
	} else if (chg != NULL && chg->external_cclogic) {
		if (oplus_chg_get_typec_attach_state() != UNATTACHED_MODE) {
			val = oplus_chg_inquire_cc_polarity();
		} else {
			val = 0;
		}
		if (val != 0)
			printk(KERN_ERR "[OPLUS_CHG][%s]: cc[%d]\n", __func__, val);
	} else {
		val = 0;
	}

	return val;
}

bool oplus_get_otg_switch_status(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	return chip->otg_switch;
}

bool oplus_get_otg_online_status_default(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (!chg || (!chg->tcpc && !chg->external_cclogic)) {
		chg_err("chg or tcpc is null\n");
		return false;
	}

	if (chg->tcpc) {
		if (tcpm_inquire_typec_attach_state(chg->tcpc) == TYPEC_ATTACHED_SRC)
			g_oplus_chip->otg_online = true;
		else
			g_oplus_chip->otg_online = false;
	} else {
		if(oplus_chg_get_typec_attach_state() == SRC_MODE)
			g_oplus_chip->otg_online = true;
		else
			g_oplus_chip->otg_online = false;
	}
#endif
	//printk(KERN_ERR "[OPLUS_CHG][%s]: g_oplus_chip->otg_online:%d!\n", __func__, g_oplus_chip->otg_online);
	return g_oplus_chip->otg_online;
}

bool oplus_check_pdphy_ready(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	return (chg && chg->tcpc &&chg->tcpc->pd_inited_flag);
}

#define CCDETECT_RETRY_DELAY_MIN_US	5000
#define CCDETECT_RETRY_DELAY_MAX_US	5100

#if 1
int oplus_get_otg_online_status(void)
{
	int online = 0;
	int level = 0;
	int typec_otg = 0;
	static int pre_level = 1;
	static int pre_typec_otg = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: discrete_charger not ready!\n", __func__);
		return false;
	}

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (!chg || (!chg->tcpc && !chg->external_cclogic)) {
		chg_err("chg or tcpc is null\n");
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(chip) == true) {
		level = gpio_get_value(chg->ccdetect_gpio);
		//chg_err("ccdetect level is:%d\n", level);
		if (level != gpio_get_value(chg->ccdetect_gpio)) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio is unstable, try again...\n", __func__);
			usleep_range(CCDETECT_RETRY_DELAY_MIN_US, CCDETECT_RETRY_DELAY_MAX_US);
			level = gpio_get_value(chg->ccdetect_gpio);
		}
	} else {
		return oplus_get_otg_online_status_default();
	}

	online = (level == 1) ? DISCONNECT : STANDARD_TYPEC_DEV_CONNECT;

	if(chg->tcpc) {
		if (tcpm_inquire_typec_attach_state(chg->tcpc) == TYPEC_ATTACHED_SRC) {
			typec_otg = 1;
		} else {
			typec_otg = 0;
		}
	} else {
		if(oplus_chg_get_typec_attach_state() == SRC_MODE) {
			typec_otg = 1;
		} else {
			typec_otg = 0;
		}
	}
#endif
	online = online | ((typec_otg == 1) ? OTG_DEV_CONNECT : DISCONNECT);

	if ((pre_level ^ level) || (pre_typec_otg ^ typec_otg)) {
		pre_level = level;
		pre_typec_otg = typec_otg;
		printk(KERN_ERR "[OPLUS_CHG][%s]: gpio[%s], c-otg[%d], otg_online[%d]\n",
				__func__, level ? "H" : "L", typec_otg, online);
	}

	chip->otg_online = typec_otg;
	return online;
}
#endif

void oplus_set_otg_switch_status(bool value)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
#ifndef CONFIG_OPLUS_CHARGER_MTK
	struct smb_charger *chg = NULL;

	if (!chip)
		return;

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (chg != NULL && chg->tcpc != NULL) {
		if(oplus_ccdetect_check_is_gpio(g_oplus_chip) == true) {
			if(gpio_get_value(chg->ccdetect_gpio) == 0) {
				printk(KERN_ERR "[OPLUS_CHG][oplus_set_otg_switch_status]: gpio[L], should set, return\n");
					return;
				}
		}
		printk(KERN_ERR "[OPLUS_CHG][%s]: otg switch[%d]\n", __func__, value);
		tcpm_typec_change_role_postpone(chg->tcpc, value ? TYPEC_ROLE_TRY_SNK : TYPEC_ROLE_SNK, true);
	} else if (chg != NULL && chg->external_cclogic) {
		if(oplus_ccdetect_check_is_gpio(g_oplus_chip) == true) {
			if(gpio_get_value(chg->ccdetect_gpio) == 0) {
				printk(KERN_ERR "[OPLUS_CHG][oplus_set_otg_switch_status]: gpio[L], should set, return\n");
				return;
			}
		}
		printk(KERN_ERR "[OPLUS_CHG][%s]: otg switch[%d]\n", __func__, value);
		oplus_chg_cclogic_set_mode(value ? MODE_DRP : MODE_UFP);
	}
#endif
}

void oplus_set_pd_active(int active)
{
	struct smb_charger *chg = NULL;

	if (!g_oplus_chip)
		return;

	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;
	chg->pd_active = active;
}

int oplus_sm8150_get_pd_type(void)
{
	struct smb_charger *chg = NULL;

	if (!g_oplus_chip)
		return PD_INACTIVE;

	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;

	if (chg->pd_not_rise_vbus_only_5v) {
		return PD_INACTIVE;
	}

	if (chg->pd_active == QTI_POWER_SUPPLY_PD_PPS_ACTIVE) {
		if (oplus_pps_get_chg_status() != PPS_NOT_SUPPORT) {
			return PD_PPS_ACTIVE;
		} else {
			return PD_ACTIVE;
		}
	} else if (chg->pd_active == QTI_POWER_SUPPLY_PD_ACTIVE) {
		return PD_ACTIVE;
	}
	return PD_INACTIVE;
}

static int oplus_pdc_setup(int *vbus_mv, int *ibus_ma)
{
	int ret = 0;
	int vbus_mv_t = 0;
	int ibus_ma_t = 0;
	struct tcpc_device *tcpc = NULL;

	tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (tcpc == NULL) {
		printk(KERN_ERR "%s:get type_c_port0 fail\n", __func__);
		return -EINVAL;
	}

	ret = tcpm_dpm_pd_request(tcpc, *vbus_mv, *ibus_ma, NULL);
	if (ret != TCPM_SUCCESS) {
		printk(KERN_ERR "%s: tcpm_dpm_pd_request fail\n", __func__);
		return -EINVAL;
	}

	ret = tcpm_inquire_pd_contract(tcpc, &vbus_mv_t, &ibus_ma_t);
	if (ret != TCPM_SUCCESS) {
		printk(KERN_ERR "%s: inquire current vbus_mv and ibus_ma fail\n", __func__);
		return -EINVAL;
	}

	printk(KERN_ERR "%s: request vbus_mv[%d], ibus_ma[%d]\n", __func__, vbus_mv_t, ibus_ma_t);

	return 0;
}

#define PDO_9V_VBUS_MV      9000
#define PDO_VBUS_DEFAULT_MV 5000
#define PDO_IBUS_DEFAULT_MA 2000
static void oplus_pdo_select(int vbus_mv, int ibus_ma)
{
	struct smb_charger *chg = NULL;
	int vbus = PDO_VBUS_DEFAULT_MV, ibus = PDO_IBUS_DEFAULT_MA;
	struct tcpm_power_cap_val apdo_cap;
	struct tcpm_remote_power_cap pd_cap;

	uint8_t cap_i = 0;
	int ret;
	int i;

	if (!g_oplus_chip)
		return;

	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;
	if (!chg || !chg->tcpc) {
		chg_err("chg or tcpc is null\n");
		return;
	}
	if (chg->pd_active == QTI_POWER_SUPPLY_PD_PPS_ACTIVE) {
		while (1) {
			ret = tcpm_inquire_pd_source_apdo(chg->tcpc,
					TCPM_POWER_CAP_APDO_TYPE_PPS,
					&cap_i, &apdo_cap);
			if (ret == TCPM_ERROR_NOT_FOUND) {
				break;
			} else if (ret != TCPM_SUCCESS) {
				chg_err("tcpm_inquire_pd_source_apdo failed(%d)\n", ret);
				break;
			}

			chg_err("pps_boundary[%d], %d mv ~ %d mv, %d ma pl:%d\n",
				cap_i,
				apdo_cap.min_mv, apdo_cap.max_mv,
				apdo_cap.ma, apdo_cap.pwr_limit);

			if (apdo_cap.min_mv <= vbus_mv && vbus_mv <= apdo_cap.max_mv) {
				vbus = vbus_mv;
				ibus = apdo_cap.ma;
				if (ibus > ibus_ma)
					ibus = ibus_ma;
				break;
			}

		}
		if (cap_i == 0)
			chg_err("no APDO for pps\n");
	} else if (chg->pd_active == QTI_POWER_SUPPLY_PD_ACTIVE) {
		pd_cap.nr = 0;
		pd_cap.selected_cap_idx = 0;
		tcpm_get_remote_power_cap(chg->tcpc, &pd_cap);

		if (pd_cap.nr != 0) {
			for (i = 0; i < pd_cap.nr; i++) {
				if (vbus_mv <= pd_cap.max_mv[i]) {
					vbus = pd_cap.max_mv[i];
					ibus = pd_cap.ma[i];
					if (ibus > ibus_ma)
						ibus = ibus_ma;
					break;
				}
				chg_err("%d mv:[%d,%d] %d type:%d %d\n",
					i, pd_cap.min_mv[i],
					pd_cap.max_mv[i], pd_cap.ma[i],
					pd_cap.type[i]);
			}
		}
	} else {
		vbus = PDO_VBUS_DEFAULT_MV;
		ibus = PDO_IBUS_DEFAULT_MA;
	}
	oplus_pdc_setup(&vbus, &ibus);
}

#define PDO_5V_VBUS_MV		5000
#define PDO_5V_IBUS_MA		2000
#define PDO_5V_HIGH_IBUS_MA	3000
#define PDO_9V_VBUS_MV		9000
#define PDO_9V_IBUS_MA		2000
#define REQUEST_PDO_DELAY_MS	300
#define PDO_9V_TO_5V_IBUS_MA	500
#define VBUS_9V_THR_MV		7500
#define VBUS_5V_THR_MV		6500
#define SOC_9V_THR		90
int oplus_chg_set_pd_config(void)
{
	int ret = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		return -1;
	}
	if(chip->pd_svooc){
		return 0;
	}

	printk(KERN_ERR "%s \n", __func__);

	if (chip->dual_charger_support) {
		if ((chip->charger_volt > VBUS_9V_THR_MV && chip->soc >= SOC_9V_THR
			&& chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr)
			|| (chip->temperature >= chip->limits.tbatt_pdqc_to_5v_thr)
			|| chip->cool_down_force_5v) {
			chip->chg_ops->input_current_write(PDO_9V_TO_5V_IBUS_MA);
			oplus_chg_config_charger_vsys_threshold(0x03);//set Vsys Skip threshold 101%

			mutex_lock(&pd_select_pdo_v);
			oplus_pdo_select(PDO_5V_VBUS_MV, PDO_5V_IBUS_MA);
			mutex_unlock(&pd_select_pdo_v);

			printk(KERN_ERR "%s: vbus[%d], ibus[%d]\n", __func__, PDO_5V_VBUS_MV, PDO_5V_IBUS_MA);
		} else if (chip->charger_volt < VBUS_5V_THR_MV
				&& chip->soc < SOC_9V_THR
				&& chip->batt_volt <= chip->limits.vbatt_pdqc_to_9v_thr
				&& chip->charging_state == CHARGING_STATUS_CCCV
				&& chip->temperature < chip->limits.tbatt_pdqc_to_5v_thr
				&& !chip->cool_down_force_5v) {
			oplus_chg_config_charger_vsys_threshold(0x02);//set Vsys Skip threshold 104%
			oplus_chg_enable_burst_mode(false);

			mutex_lock(&pd_select_pdo_v);
			oplus_pdo_select(PDO_9V_VBUS_MV, PDO_9V_IBUS_MA);
			mutex_unlock(&pd_select_pdo_v);

			printk(KERN_ERR "%s: vbus[%d], ibus[%d], ret[%d]\n", __func__, PDO_9V_VBUS_MV, PDO_9V_IBUS_MA, ret);
		} else {
			printk(KERN_ERR "%s is dual charger, but not config the vbus and ibus \n", __func__);
			return -1;
		}
	} else {
		if (chip->limits.vbatt_pdqc_to_5v_thr > 0 && chip->charger_volt > VBUS_9V_THR_MV
			&& chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr) {
			chip->chg_ops->input_current_write(PDO_9V_TO_5V_IBUS_MA);
			oplus_chg_suspend_charger();
			oplus_chg_config_charger_vsys_threshold(0x03);//set Vsys Skip threshold 101%
			mutex_lock(&pd_select_pdo_v);
			oplus_pdo_select(PDO_5V_VBUS_MV, PDO_5V_IBUS_MA);
			mutex_unlock(&pd_select_pdo_v);
			msleep(REQUEST_PDO_DELAY_MS);
			printk(KERN_ERR "%s: vbus[%d], ibus[%d], ret[%d]\n", __func__, 5000, 2000, ret);
			oplus_chg_unsuspend_charger();
		} else if ((chip->vbatt_num == 1) && (chip->vooc_project == 1)) {
			chip->chg_ops->input_current_write(PDO_9V_TO_5V_IBUS_MA);
			oplus_chg_suspend_charger();
			oplus_chg_config_charger_vsys_threshold(0x03);//set Vsys Skip threshold 101%

			mutex_lock(&pd_select_pdo_v);
			oplus_pdo_select(PDO_5V_VBUS_MV, PDO_5V_HIGH_IBUS_MA);
			mutex_unlock(&pd_select_pdo_v);
			msleep(REQUEST_PDO_DELAY_MS);
			printk(KERN_ERR "%s: charger voltage=%d", __func__, qpnp_get_prop_charger_voltage_now());
			oplus_chg_unsuspend_charger();
		} else if (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr) {
			oplus_voocphy_set_pdqc_config();
			oplus_chg_suspend_charger();
			oplus_chg_config_charger_vsys_threshold(0x02);//set Vsys Skip threshold 104%
			oplus_chg_enable_burst_mode(false);
			mutex_lock(&pd_select_pdo_v);
			oplus_pdo_select(PDO_9V_VBUS_MV, PDO_9V_IBUS_MA);
			mutex_unlock(&pd_select_pdo_v);
			printk(KERN_ERR "%s: vbus[%d], ibus[%d], ret[%d]\n", __func__, PDO_9V_VBUS_MV, PDO_9V_IBUS_MA, ret);
			msleep(REQUEST_PDO_DELAY_MS);
			oplus_chg_unsuspend_charger();
		}
	}

	return ret;
}

#define OPLUS_SVID 0x22d9
uint32_t pd_svooc_abnormal_adapter[] = {
	0x20002,
	0x10002,
	0x10001,
	0x40001,
};

int oplus_get_adapter_svid(void)
{
	int i = 0, j = 0;
	uint32_t vdos[VDO_MAX_NR] = {0};
	struct tcpc_device *tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	struct tcpm_svid_list svid_list= {0, {0}};

	if (tcpc_dev == NULL || !g_oplus_chip) {
		chg_err("tcpc_dev is null return\n");
		return -1;
	}

	if (!oplus_is_vooc_project()) {
		chg_err("device don't support vooc\n");
		return -1;
	}

	tcpm_inquire_pd_partner_svids(tcpc_dev, &svid_list);
	for (i = 0; i < svid_list.cnt; i++) {
		chg_err("svid[%d] = %d\n", i, svid_list.svids[i]);
		if (svid_list.svids[i] == OPLUS_SVID) {
			g_oplus_chip->pd_svooc = true;
			chg_err("match svid and this is oplus adapter\n");
			break;
		}
	}

	tcpm_inquire_pd_partner_inform(tcpc_dev, vdos);
	if ((vdos[0] & 0xFFFF) == OPLUS_SVID) {
		g_oplus_chip->pd_svooc = true;
		chg_err("match svid and this is oplus adapter 11\n");
		for (j = 0; j < ARRAY_SIZE(pd_svooc_abnormal_adapter); j++) {
			if (pd_svooc_abnormal_adapter[j] == vdos[2]) {
				chg_err("This is oplus gnd abnormal adapter %x %x \n", vdos[1], vdos[2]);
				g_oplus_chip->is_abnormal_adapter = true;
				break;
			}
		}
	}


	return 0;
}

int oplus_chg_get_charger_subtype(void)
{
	struct smb_charger *chg = NULL;

	if (!g_oplus_chip)
		return CHARGER_SUBTYPE_DEFAULT;

	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;
	if (chg->pd_active)
		return CHARGER_SUBTYPE_PD;

	return CHARGER_SUBTYPE_DEFAULT;
}

int opchg_get_real_charger_type(void)
{
	struct smb_charger *chg = NULL;
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

	if (!g_oplus_chip)
		return POWER_SUPPLY_TYPE_UNKNOWN;

	chg = &g_oplus_chip->pmic_spmi.smb5_chip->chg;
	if (chg->pd_active && !chg->pd_sdp)
		return POWER_SUPPLY_TYPE_USB_PD;

	if (NULL != g_oplus_chip->chg_ops) {
		if (NULL != g_oplus_chip->chg_ops->get_charger_type) {
			charger_type = g_oplus_chip->chg_ops->get_charger_type();
		} else {
			chg_err("get_charger_type is NULL.\n");
		}
	} else {
		chg_err("not support now.\n");
	}

	return charger_type;
}

static enum power_supply_property oplus_discrete_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TYPE,
};

int oplus_get_usb_status(void)
{
	return g_oplus_chip->usb_status;
}
EXPORT_SYMBOL(oplus_get_usb_status);

#define USB_VOLTAGE_MAX 5000000
#define USB_VOLTAGE_MIN 5000000
static int oplus_discrete_usb_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int rc = 0;

	val->intval = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = oplus_chg_stats();
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = oplus_chg_stats();
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = USB_VOLTAGE_MAX;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = USB_VOLTAGE_MIN;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB_PD;
		break;
	default:
		pr_info("get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int oplus_discrete_usb_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	default:
		pr_info("Set prop %d is not supported in usb psy\n",
				psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int oplus_discrete_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	default:
		break;
	}

	return 0;
}

#ifndef OPLUS_FEATURE_CHG_BASIC
static const struct power_supply_desc usb_psy_desc = {
#else
static struct power_supply_desc usb_psy_desc = {
#endif
	.name = "usb",
#ifndef OPLUS_FEATURE_CHG_BASIC
	.type = POWER_SUPPLY_TYPE_USB_PD,
#else
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
#endif
	.properties = oplus_discrete_usb_props,
	.num_properties = ARRAY_SIZE(oplus_discrete_usb_props),
	.get_property = oplus_discrete_usb_get_prop,
	.set_property = oplus_discrete_usb_set_prop,
	.property_is_writeable = oplus_discrete_usb_prop_is_writeable,
};

static int oplus_discrete_init_usb_psy(struct smb5 *chip)
{
	struct power_supply_config usb_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_cfg.drv_data = chip;
	usb_cfg.of_node = chg->dev->of_node;
	chg->usb_psy = devm_power_supply_register(chg->dev,
						  &usb_psy_desc,
						  &usb_cfg);
	if (IS_ERR(chg->usb_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->usb_psy);
	}

	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_set_usb_props_type(enum power_supply_type type)
{
	chg_err("old type[%d], new type[%d]\n", usb_psy_desc.type, type);
	usb_psy_desc.type = type;
	return;
}
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
/*************************
 * AC PSY REGISTRATION *
 *************************/
 static enum power_supply_property oplus_discrete_ac_props[] = {
/*oplus own ac props*/
	POWER_SUPPLY_PROP_ONLINE,
};

static int oplus_discrete_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int rc = 0;

	rc = oplus_ac_get_property(psy, psp, val);

	return rc;
}

static const struct power_supply_desc ac_psy_desc = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = oplus_discrete_ac_props,
	.num_properties = ARRAY_SIZE(oplus_discrete_ac_props),
	.get_property = oplus_discrete_ac_get_property,
};

static int oplus_discrete_init_ac_psy(struct smb5 *chip)
{
	struct power_supply_config ac_cfg = {};
	struct smb_charger *chg = &chip->chg;

	ac_cfg.drv_data = chip;
	ac_cfg.of_node = chg->dev->of_node;
	chg->ac_psy = devm_power_supply_register(chg->dev,
						  &ac_psy_desc,
						  &ac_cfg);
	if (IS_ERR(chg->ac_psy)) {
		pr_err("Couldn't register AC power supply\n");
		return PTR_ERR(chg->ac_psy);
	}

	return 0;
}
#endif

/*************************
 * BATT PSY REGISTRATION *
 *************************/
static enum power_supply_property oplus_discrete_batt_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static int oplus_discrete_batt_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int rc = 0;
	struct oplus_chg_chip *chip = g_oplus_chip;
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
			if (chip->ui_soc == 0) {
				val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
				chg_err("bat pro POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, should shutdown!!!\n");
			}
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		case POWER_SUPPLY_PROP_CHARGE_FULL:
			val->intval = chip->batt_fcc * UNIT_TRANS_1000;
			break;
		case POWER_SUPPLY_PROP_CHARGE_COUNTER:
			val->intval = chip->ui_soc * chip->batt_capacity_mah * UNIT_TRANS_1000 / FULL_SOC;
			break;
		case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
			val->intval = 0;
			break;
		default:
			rc = oplus_battery_get_property(psy, psp, val);
			break;
	}
	return 0;
}

static int oplus_discrete_batt_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	return oplus_battery_set_property(psy, psp, val);
}

static int oplus_discrete_batt_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	return oplus_battery_property_is_writeable(psy, psp);
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = oplus_discrete_batt_props,
	.num_properties = ARRAY_SIZE(oplus_discrete_batt_props),
	.get_property = oplus_discrete_batt_get_prop,
	.set_property = oplus_discrete_batt_set_prop,
	.property_is_writeable = oplus_discrete_batt_prop_is_writeable,
};

static int oplus_discrete_init_batt_psy(struct smb5 *chip)
{
	struct power_supply_config batt_cfg = {};
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	batt_cfg.drv_data = chg;
	batt_cfg.of_node = chg->dev->of_node;
	chg->batt_psy = devm_power_supply_register(chg->dev,
					   &batt_psy_desc,
					   &batt_cfg);
	if (IS_ERR(chg->batt_psy)) {
		pr_err("Couldn't register battery power supply\n");
		return PTR_ERR(chg->batt_psy);
	}

	return rc;
}

static int oplus_discrete_iio_get_prop(struct smb_charger *chg, int channel, int *val)
{
	int rc = 0;

	*val = 0;

	switch (channel) {
	case PSY_IIO_USB_REAL_TYPE:
		*val = opchg_get_real_charger_type();
		break;
	case PSY_IIO_PD_IN_HARD_RESET:
		*val = chg->pd_hard_reset;
		break;
	case PSY_IIO_PD_SDP:
		*val = chg->pd_sdp;
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return IIO_VAL_INT;
}

#define KEEP_VBUS_DELAY 2000
static int oplus_discrete_iio_set_prop(struct smb_charger *chg, int channel, int val)
{
	int rc = 0;
	switch (channel) {
	case PSY_IIO_PD_IN_HARD_RESET:
		if (val) {
			chg->pd_hard_reset = 1;
			if (qpnp_is_power_off_charging() && !chg->first_hardreset) {
				chg->first_hardreset = true;
				chg->keep_vbus_5v = true;
				schedule_delayed_work(&chg->keep_vbus_work, msecs_to_jiffies(KEEP_VBUS_DELAY));
			}
		}
		else
			chg->pd_hard_reset = 0;
		break;
	case PSY_IIO_PD_SDP:
		chg->pd_sdp = val;
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_err("Couldn't set prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return 0;
}

static int oplus_discrete_of_xlate(struct iio_dev *indio_dev,
				const struct of_phandle_args *iiospec)
{
	struct smb5 *iio_chip = iio_priv(indio_dev);
	struct iio_chan_spec *iio_chan = iio_chip->iio_chan_ids;
	int i;

	for (i = 0; i < iio_chip->nchannels; i++, iio_chan++)
		if (iio_chan->channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

static int oplus_discrete_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct smb5 *iio_chip = iio_priv(indio_dev);
	struct smb_charger *chg = &iio_chip->chg;

	return oplus_discrete_iio_get_prop(chg, chan->channel, val);
}

static int oplus_discrete_write_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int val, int val2,
			 long mask)
{
	struct smb5 *iio_chip = iio_priv(indio_dev);
	struct smb_charger *chg = &iio_chip->chg;

	return oplus_discrete_iio_set_prop(chg, chan->channel, val);
}

static const struct iio_info oplus_discrete_iio_info = {
	.read_raw = oplus_discrete_read_raw,
	.write_raw = oplus_discrete_write_raw,
	.of_xlate = oplus_discrete_of_xlate,
};

static int oplus_discrete_direct_iio_read(struct device *dev, int iio_chan_no, int *val)
{
	struct smb5 *chip = dev_get_drvdata(dev);
	struct smb_charger *chg = &chip->chg;
	int rc;

	rc = oplus_discrete_iio_get_prop(chg, iio_chan_no, val);

	return (rc < 0) ? rc : 0;
}

static int oplus_discrete_direct_iio_write(struct device *dev, int iio_chan_no, int val)
{
	struct smb5 *chip = dev_get_drvdata(dev);
	struct smb_charger *chg = &chip->chg;

	return oplus_discrete_iio_set_prop(chg, iio_chan_no, val);
}

static int oplus_discrete_iio_init(struct smb5 *chip, struct platform_device *pdev,
		struct iio_dev *indio_dev)
{
	struct iio_chan_spec *iio_chan;
	int i, rc;

	for (i = 0; i < chip->nchannels; i++) {
		chip->iio_chans[i].indio_dev = indio_dev;
		iio_chan = &chip->iio_chan_ids[i];
		chip->iio_chans[i].channel = iio_chan;

		iio_chan->channel = oplus_discrete_chans_pmic[i].channel_num;
		iio_chan->datasheet_name = oplus_discrete_chans_pmic[i].datasheet_name;
		iio_chan->extend_name = oplus_discrete_chans_pmic[i].datasheet_name;
		iio_chan->info_mask_separate = oplus_discrete_chans_pmic[i].info_mask;
		iio_chan->type = oplus_discrete_chans_pmic[i].type;
		iio_chan->address = i;
	}

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->name = "oplus_discrete";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = chip->iio_chan_ids;
	indio_dev->num_channels = chip->nchannels;

	rc = devm_iio_device_register(&pdev->dev, indio_dev);
	if (rc)
		pr_err("iio device register failed rc=%d\n", rc);

	return rc;
}

#define VBUS_DEFAULT_MV		5000
#define CP_ADC_DELAY_MIN_US	1000
#define CP_ADC_DELAY_MAX_US	1500
int qpnp_get_prop_charger_voltage_now(void)
{
	int chg_vol = 0;
	u8 cp_adc_reg = 0;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();
	struct smb_charger *chg = NULL;

	if (!chip)
		return 0;

	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (qpnp_is_power_off_charging()) {
		if (chg->pd_hard_reset || chg->keep_vbus_5v) {
			chg_err("pd hardreset,return 5000\n");
			return VBUS_DEFAULT_MV;
		}
	}

	if (is_ext_mp2650_chg_ops()) {
		chg_vol = mp2650_get_vbus_voltage();
	} else if (is_ext_sy6974b_chg_ops() || is_ext_sy6970_chg_ops()) {
		if (!chip->charger_exist && !chip->ac_online)
			return 0;
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY
				|| oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			if (oplus_voocphy_get_fastchg_commu_ing())
				return oplus_voocphy_get_var_vbus();

			oplus_voocphy_get_adc_enable(&cp_adc_reg);
			if(cp_adc_reg == 0) {
				oplus_voocphy_set_adc_enable(true);
				usleep_range(CP_ADC_DELAY_MIN_US, CP_ADC_DELAY_MAX_US);
			}
			chg_vol = oplus_voocphy_get_cp_vbus();
		}
	} else if (is_ext_sgm41512_chg_ops()) {
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY
				|| oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			if (oplus_voocphy_get_fastchg_commu_ing())
				return oplus_voocphy_get_var_vbus();

			oplus_voocphy_get_adc_enable(&cp_adc_reg);
			if(cp_adc_reg == 0) {
				oplus_voocphy_set_adc_enable(true);
				usleep_range(CP_ADC_DELAY_MIN_US, CP_ADC_DELAY_MAX_US);
			}
			chg_vol = oplus_voocphy_get_cp_vbus();
		} else {
			chg_err("get chg_vol interface null\n");
		}
	}
	return chg_vol;
}

int oplus_chg_get_subcurrent(void)
{
	if (g_oplus_chip && g_oplus_chip->sub_chg_ops
		&& g_oplus_chip->sub_chg_ops->get_charger_current) {
		return g_oplus_chip->sub_chg_ops->get_charger_current();
	}
	return 0;
}
EXPORT_SYMBOL(oplus_chg_get_subcurrent);

int qpnp_get_prop_ibus_now(void)
{
	int ibus = -1;
	struct oplus_chg_chip *chip = oplus_chg_get_chg_struct();

	if (!chip)
		return 0;
	if (!chip->charger_exist)
		return 0;

	if (is_ext_mp2650_chg_ops()) {
		ibus = mp2650_get_ibus_current();
	} else if (is_ext_sy6974b_chg_ops()) {
		ibus = -1;
	} else if (is_ext_sgm41512_chg_ops()) {
		ibus = -1;
	} else if (is_ext_sy6970_chg_ops()) {
		ibus = oplus_gauge_get_batt_current() + sy6970_adc_read_charge_current();
	}
	return ibus;
}

#define GET_TCPC_CNT_MAX 5
static int discrete_charger_probe(struct platform_device *pdev)
{
	struct smb5 *chip;
	struct iio_dev *indio_dev;
	struct smb_charger *chg;
	int rc = 0;
	int level = 0;
	struct oplus_chg_chip *oplus_chip;
	static int get_tcpc_count = 0;

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(*oplus_chip), GFP_KERNEL);
	if (!oplus_chip)
		return -ENOMEM;
	oplus_chip->dev = &pdev->dev;

	rc = oplus_chg_parse_svooc_dt(oplus_chip);

	if (oplus_chip->vbatt_num == 1) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}

		charger_ic__det_flag = get_charger_ic_det(oplus_chip);
		if (oplus_chip->is_double_charger_support) {
			chg_err("charger_ic__det_flag = 0x%x.\n", charger_ic__det_flag);
			if (charger_ic__det_flag == 0) {
				chg_err("charger IC is null, will do after bettery init.\n");
				return -EPROBE_DEFER;
			}

			switch(charger_ic__det_flag) {
			/*Pikachu use the SY6970 as Main charger, the SGM41511 as slave charger.*/
			case (1 << SY6970 | 1 << SGM41511):
				oplus_chip->chg_ops = oplus_chg_ops_get();
				oplus_chip->sub_chg_ops = &sgm41511_chg_ops;
				if ((NULL == oplus_chip->chg_ops) || (NULL == oplus_chip->sub_chg_ops)) {
					chg_err("chg_ops is null, error!!!\n");
                                	return -EPROBE_DEFER;
				}
				break;
			case (1 << BQ2589X | 1 << SGM41511):
				oplus_chip->chg_ops = oplus_chg_ops_get();
				oplus_chip->sub_chg_ops = &sgm41511_chg_ops;
				if ((NULL == oplus_chip->chg_ops) || (NULL == oplus_chip->sub_chg_ops)) {
					chg_err("chg_ops is null, error!!!\n");
                                	return -EPROBE_DEFER;
				}
				chg_err("BQ2589X and  SGM41511!!!\n");
				break;
			default:
				chg_err("charger not supported now. \n");
				break;
			}
		} else {
			oplus_chip->chg_ops = oplus_chg_ops_get();
			if (!oplus_chip->chg_ops) {
				chg_err("oplus_chg_ops_get null, fatal error!!!\n");
				return -EPROBE_DEFER;
			}
		}
	} else {
		chg_err("[oplus_chg_init] gauge[%d]vooc[%d]adapter[%d]\n",
				oplus_gauge_ic_chip_is_null(),
				oplus_vooc_check_chip_is_null(),
				oplus_adapter_check_chip_is_null());
		if (oplus_gauge_ic_chip_is_null() || oplus_vooc_check_chip_is_null()
				|| oplus_charger_ic_chip_is_null() || oplus_adapter_check_chip_is_null()) {
			chg_err("[oplus_chg_init] vooc || gauge || chg not ready, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		//oplus_chip->chg_ops = (oplus_get_chg_ops());
	}

	g_oplus_chip = oplus_chip;
#endif

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &oplus_discrete_iio_info;
	chip = iio_priv(indio_dev);
	chip->nchannels = ARRAY_SIZE(oplus_discrete_chans_pmic);

	chip->iio_chans = devm_kcalloc(&pdev->dev, chip->nchannels,
					sizeof(*chip->iio_chans), GFP_KERNEL);
	if (!chip->iio_chans)
		return -ENOMEM;

	chip->iio_chan_ids = devm_kcalloc(&pdev->dev, chip->nchannels,
					sizeof(*chip->iio_chan_ids), GFP_KERNEL);

	if (!chip->iio_chan_ids)
		return -ENOMEM;

	chg = &chip->chg;
	chg->dev = &pdev->dev;
	mutex_init(&chg->adc_lock);

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chip->pmic_spmi.smb5_chip = chip;
#endif

	rc = oplus_discrete_iio_init(chip, pdev, indio_dev);
	if (rc < 0)
		return rc;

	if (of_find_property(chg->dev->of_node, "nvmem-cells", NULL)) {
		chg->soc_backup_nvmem = devm_nvmem_cell_get(chg->dev,
						"oplus_soc_backup");
		if (IS_ERR(chg->soc_backup_nvmem)) {
			rc = PTR_ERR(chg->soc_backup_nvmem);
			if (rc != -EPROBE_DEFER)
				chg_err("Failed to get nvmem-cells, rc=%d\n", rc);
			return rc;
		}
	}

	chg->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!chg->tcpc) {
		if (get_tcpc_count < GET_TCPC_CNT_MAX) {
			g_oplus_chip = NULL;/*Set g_oplus_chip to NULL after  discrete_charger Probe fail to avoid wild pointer */
			chg_err("get tcpc device type_c_port0 fail, count=%d\n", ++get_tcpc_count);
			return -EPROBE_DEFER;
		} else {
			chg_err("get tcpc device type_c_port0 fail, continue\n");
		}
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	mutex_init(&chg->pinctrl_mutex);
	chg->pre_current_ma = -1;
	chg->pd_hard_reset = 0;
	chg->first_hardreset = false;
	chg->keep_vbus_5v = false;
	INIT_DELAYED_WORK(&chg->ccdetect_work, oplus_ccdetect_work);
	INIT_DELAYED_WORK(&usbtemp_recover_work, oplus_usbtemp_recover_work);
	INIT_DELAYED_WORK(&chg->keep_vbus_work, oplus_keep_vbus_work);
#endif

	/* set driver data before resources request it */
	platform_set_drvdata(pdev, chip);

	chg->chg_param.iio_read = oplus_discrete_direct_iio_read;
	chg->chg_param.iio_write = oplus_discrete_direct_iio_write;

	rc = oplus_discrete_init_usb_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = oplus_discrete_init_batt_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize batt psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = oplus_discrete_init_ac_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize ac psy rc=%d\n", rc);
		goto cleanup;
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	oplus_chg_2uart_pinctrl_init(oplus_chip);
	oplus_parse_dt_adc_channels(chg);
	oplus_chg_init(oplus_chip);

	oplus_chip->con_volt = con_volt_pmr735a;
	oplus_chip->con_temp = con_temp_pmr735a;
	oplus_chip->len_array = ARRAY_SIZE(con_temp_pmr735a);
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
	chg->iio.pre_batt_temp = DEFAULT_SUBBOARD_TEMP;
	if (oplus_ccdetect_check_is_gpio(oplus_chip) == true) {
		oplus_ccdetect_irq_register(oplus_chip);
		level = gpio_get_value(chg->ccdetect_gpio);
		usleep_range(2000, 2100);
		if (level != gpio_get_value(chg->ccdetect_gpio)) {
			printk(KERN_ERR "[OPLUS_CHG][%s]: ccdetect_gpio is unstable,try again..\n",
				__func__);
			usleep_range(10000, 11000);
			level = gpio_get_value(chg->ccdetect_gpio);
		}
		if (level == 0)
			schedule_delayed_work(&chg->ccdetect_work, 6000);
	}

	oplus_chg_configfs_init(oplus_chip);
	oplus_chg_wake_update_work();

	if (oplus_usbtemp_check_is_support() == true) {
		oplus_usbtemp_thread_init();
	}

#if IS_BUILTIN(CONFIG_OPLUS_CHG)
	if (qpnp_is_power_off_charging() == false) {
		oplus_tbatt_power_off_task_init(oplus_chip);
	}
#endif
#endif
	device_init_wakeup(chg->dev, 1);
	chg_err("discrete charger probed successfully\n");

	return rc;

cleanup:
	platform_set_drvdata(pdev, NULL);

	return rc;
}


static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,discrete-charger", },
	{ },
};

static int discrete_charger_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#define SWITCH_TO_NORMAL_DELAY_MS	30
#define ENTER_SHIPMODE_DELAY_MS		1000
static void discrete_charger_shutdown(struct platform_device *pdev)
{
	struct smb5 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;
	int level = 0;

	if(!g_oplus_chip) {
		return;
	}

	if (chg->pd_active)
		oplus_pdo_select(PDO_5V_VBUS_MV, PDO_5V_IBUS_MA);

	if (g_oplus_chip) {
		oplus_vooc_reset_mcu();
		smbchg_set_chargerid_switch_val(0);
		oplus_vooc_switch_mode(NORMAL_CHARGER_MODE);
		msleep(SWITCH_TO_NORMAL_DELAY_MS);
	}

	if (oplus_shipmode_id_check_is_gpio(g_oplus_chip) == true) {
		level = gpio_get_value(chg->shipmode_id_gpio);
	}
	if((oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY)
				|| (oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY)) {
		if(g_oplus_chip->enable_shipmode)
			oplus_voocphy_set_adc_enable(false);
	}
	if (g_oplus_chip && g_oplus_chip->enable_shipmode && level != 1) {
		msleep(ENTER_SHIPMODE_DELAY_MS);
		smbchg_enter_shipmode(g_oplus_chip);
	}
}

static struct platform_driver discrete_charger_driver = {
	.driver		= {
		.name		= "qcom,discrete-charger",
		.of_match_table	= match_table,
	},
	.probe		= discrete_charger_probe,
	.remove		= discrete_charger_remove,
	.shutdown	= discrete_charger_shutdown,
};
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_platform_driver(discrete_charger_driver);
#else
static int __init discrete_charger_init(void)
{
	int ret;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	oplus_vooc_get_fastchg_started_pfunc(&oplus_vooc_get_fastchg_started);
	oplus_vooc_get_fastchg_ing_pfunc(&oplus_vooc_get_fastchg_ing);
#endif
	adapter_ic_init();
	bq27541_driver_init();
	sm5602_driver_init();
	rk826_subsys_init();
	op10_subsys_init();
	rt5125_subsys_init();
	sc8547_subsys_init();
	sc8547_slave_subsys_init();
	sy6974b_charger_init();
	sy6970_charger_init();
	sgm41511_charger_init();
	sgm41512_charger_init();
	ra9530_driver_init();
	mp2650_driver_init();
	sgm7220_i2c_init();

	ret = platform_driver_register(&discrete_charger_driver);
	if (ret)
		chg_err(" failed to register discrete charger platform driver.\n");

	rt_pd_manager_init();
	return ret;
}

static void __exit discrete_charger_exit(void)
{
	rt_pd_manager_exit();

	platform_driver_unregister(&discrete_charger_driver);

	sgm7220_i2c_exit();
	mp2650_driver_exit();
	sy6970_charger_exit();
	ra9530_driver_exit();
	sgm41512_charger_exit();
	sgm41511_charger_exit();
	sy6974b_charger_exit();
	sc8547_slave_subsys_exit();
	sc8547_subsys_exit();
	rt5125_subsys_exit();
	op10_subsys_exit();
	rk826_subsys_exit();
	sm5602_driver_exit();
	bq27541_driver_exit();
	adapter_ic_exit();
}
oplus_chg_module_register(discrete_charger);
#endif

MODULE_DESCRIPTION("Discrete Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: i2c-msm-geni tcpc_rt1711h");

