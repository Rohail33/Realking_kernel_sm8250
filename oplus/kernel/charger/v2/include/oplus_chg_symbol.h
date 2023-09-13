#ifndef __OPLUS_CHG_SYMBOL_H__
#define __OPLUS_CHG_SYMBOL_H__

#ifdef OPLUS_CHG_KO_BUILD

#if __and(IS_MODULE(CONFIG_OPLUS_CHG), IS_MODULE(CONFIG_OPLUS_CHG_V2))

#include <linux/kconfig.h>

#if IS_ENABLED(CONFIG_OPLUS_SM8350_CHARGER) || IS_ENABLED(CONFIG_OPLUS_SM8450_CHARGER) || IS_ENABLED(CONFIG_OPLUS_SM8550_CHARGER)
#if IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER) || IS_ENABLED(CONFIG_OPLUS_ADSP_SM8450_CHARGER)
#define USE_ADSP
#endif
#endif

#ifdef USE_ADSP

#if IS_ENABLED(CONFIG_OPLUS_SM8350_CHARGER)
#define oplus_turn_off_power_when_adsp_crash oplus_turn_off_power_when_adsp_crash_v2
#endif
#define oplus_is_pd_svooc oplus_is_pd_svooc_v2
#if defined(CONFIG_OPLUS_SM8350_CHARGER)
#define oplus_adsp_crash_recover_work oplus_adsp_crash_recover_work_v2
#endif
#define qti_battery_charger_get_prop qti_battery_charger_get_prop_v2
#endif /* USE_ADSP */

#if IS_ENABLED(CONFIG_OPLUS_CHARGER_MTK6895S)

#define oplus_chg_wake_update_work oplus_chg_wake_update_work_v2
#define oplus_chg_check_chip_is_null oplus_chg_check_chip_is_null_v2
#define oplus_chg_check_ui_soc oplus_chg_check_ui_soc_v2
#define oplus_is_vooc_project oplus_is_vooc_project_v2
#define oplus_chg_show_vooc_logo_ornot oplus_chg_show_vooc_logo_ornot_v2
#define oplus_get_prop_status oplus_get_prop_status_v2
#define oplus_mt_get_vbus_status oplus_mt_get_vbus_status_v2
#define oplus_chg_get_notify_flag oplus_chg_get_notify_flag_v2
#define oplus_chg_get_ui_soc oplus_chg_get_ui_soc_v2
#define oplus_chg_get_voocphy_support oplus_chg_get_voocphy_support_v2
#define oplus_gauge_init oplus_gauge_init_v2
#define chr_get_debug_level chr_get_debug_level_v2
#define mtk_chg_enable_vbus_ovp mtk_chg_enable_vbus_ovp_v2
#define mt_power_supply_type_check mt_power_supply_type_check_v2
#define mt_usb_connect mt_usb_connect_v2
#define mt_usb_disconnect mt_usb_disconnect_v2
#define get_rtc_spare_oplus_fg_value get_rtc_spare_oplus_fg_value_v2
#define set_rtc_spare_oplus_fg_value set_rtc_spare_oplus_fg_value_v2
#define is_meta_mode is_meta_mode_v2
#define oplus_tchg_01c_precision oplus_tchg_01c_precision_v2
#define oplus_force_get_subboard_temp oplus_force_get_subboard_temp_v2

#endif /* CONFIG_OPLUS_CHARGER_MTK6895S */

#endif /* CONFIG_OPLUS_CHG & CONFIG_OPLUS_CHG_V2 */
#endif /* OPLUS_CHG_KO_BUILD */

#endif /* __OPLUS_CHG_SYMBOL_H__ */
