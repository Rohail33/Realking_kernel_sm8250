#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>

#define DEF_SYMBOLE(ret, name, ...)				\
	extern ret name##_v1(__VA_ARGS__);			\
	extern ret name##_v2(__VA_ARGS__);			\
	ret name(__VA_ARGS__) {
#define ADD_FUNC_BODY(name, ...)				\
		switch (oplus_chg_version) {			\
		case OPLUS_CHG_V2:				\
			return name##_v2(__VA_ARGS__);		\
		case OPLUS_CHG_V1:				\
		default:					\
			return name##_v1(__VA_ARGS__);		\
		}						\
	}							\
	EXPORT_SYMBOL(name);

static int oplus_chg_version;

enum {
	OPLUS_CHG_V1 = 0,
	OPLUS_CHG_V2,
};

#if IS_ENABLED(CONFIG_OPLUS_SM8350_CHARGER) || IS_ENABLED(CONFIG_OPLUS_SM8450_CHARGER)
#if IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER)
#define USE_ADSP
#endif
#endif

#ifdef USE_ADSP
#include <linux/soc/qcom/battery_charger.h>
#endif

#ifdef USE_ADSP

#if IS_ENABLED(CONFIG_OPLUS_SM8350_CHARGER)
DEF_SYMBOLE(void, oplus_turn_off_power_when_adsp_crash, void)
ADD_FUNC_BODY(oplus_turn_off_power_when_adsp_crash)
#endif

DEF_SYMBOLE(bool, oplus_is_pd_svooc, void)
ADD_FUNC_BODY(oplus_is_pd_svooc)

#if defined(CONFIG_OPLUS_SM8350_CHARGER)
DEF_SYMBOLE(void, oplus_adsp_crash_recover_work, void)
ADD_FUNC_BODY(oplus_adsp_crash_recover_work)
#endif

DEF_SYMBOLE(int, qti_battery_charger_get_prop, const char *name, enum battery_charger_prop prop_id, int *val)
ADD_FUNC_BODY(qti_battery_charger_get_prop, name, prop_id, val)
#endif /* USE_ADSP */

#if IS_ENABLED(CONFIG_OPLUS_CHARGER_MTK6895S)

#include "oplus_gauge.h"

DEF_SYMBOLE(int, oplus_chg_wake_update_work, void)
ADD_FUNC_BODY(oplus_chg_wake_update_work)

DEF_SYMBOLE(bool, oplus_chg_check_chip_is_null, void)
ADD_FUNC_BODY(oplus_chg_check_chip_is_null)

DEF_SYMBOLE(int, oplus_chg_check_ui_soc, void)
ADD_FUNC_BODY(oplus_chg_check_ui_soc)

DEF_SYMBOLE(int, oplus_is_vooc_project, void)
ADD_FUNC_BODY(oplus_is_vooc_project)

DEF_SYMBOLE(int, oplus_chg_show_vooc_logo_ornot, void)
ADD_FUNC_BODY(oplus_chg_show_vooc_logo_ornot)

DEF_SYMBOLE(int, oplus_get_prop_status, void)
ADD_FUNC_BODY(oplus_get_prop_status)

DEF_SYMBOLE(bool, oplus_mt_get_vbus_status, void)
ADD_FUNC_BODY(oplus_mt_get_vbus_status)

DEF_SYMBOLE(int, oplus_chg_get_notify_flag, void)
ADD_FUNC_BODY(oplus_chg_get_notify_flag)

DEF_SYMBOLE(int, oplus_chg_get_ui_soc, void)
ADD_FUNC_BODY(oplus_chg_get_ui_soc)

DEF_SYMBOLE(int, oplus_chg_get_voocphy_support, void)
ADD_FUNC_BODY(oplus_chg_get_voocphy_support)

DEF_SYMBOLE(void, oplus_gauge_init, struct oplus_gauge_chip *chip)
ADD_FUNC_BODY(oplus_gauge_init, chip)

DEF_SYMBOLE(int, chr_get_debug_level, void)
ADD_FUNC_BODY(chr_get_debug_level)

DEF_SYMBOLE(int, mtk_chg_enable_vbus_ovp, bool enable)
ADD_FUNC_BODY(mtk_chg_enable_vbus_ovp, enable)

DEF_SYMBOLE(int, mt_power_supply_type_check, void)
ADD_FUNC_BODY(mt_power_supply_type_check)

DEF_SYMBOLE(void, mt_usb_connect, void)
ADD_FUNC_BODY(mt_usb_connect)

DEF_SYMBOLE(void, mt_usb_disconnect, void)
ADD_FUNC_BODY(mt_usb_disconnect)

DEF_SYMBOLE(int, get_rtc_spare_oplus_fg_value, void)
ADD_FUNC_BODY(get_rtc_spare_oplus_fg_value)

DEF_SYMBOLE(int, set_rtc_spare_oplus_fg_value, int soc)
ADD_FUNC_BODY(set_rtc_spare_oplus_fg_value, soc)

DEF_SYMBOLE(bool, is_meta_mode, void)
ADD_FUNC_BODY(is_meta_mode)

DEF_SYMBOLE(bool, oplus_tchg_01c_precision, void)
ADD_FUNC_BODY(oplus_tchg_01c_precision)

DEF_SYMBOLE(int, oplus_force_get_subboard_temp, void)
ADD_FUNC_BODY(oplus_force_get_subboard_temp)

#endif /* CONFIG_OPLUS_CHARGER_MTK6895S */

static int __init oplus_chg_symbol_init(void)
{
	struct device_node *node;

	node = of_find_node_by_path("/soc/oplus_chg_core");
	if ((node != NULL) && of_property_read_bool(node, "oplus,chg_framework_v2"))
		oplus_chg_version = OPLUS_CHG_V2;
	else
		oplus_chg_version = OPLUS_CHG_V1;

	return 0;
}

static void __exit oplus_chg_symbol_exit(void)
{
}

subsys_initcall(oplus_chg_symbol_init);
module_exit(oplus_chg_symbol_exit);

MODULE_DESCRIPTION("oplus charge symbol");
MODULE_LICENSE("GPL");
