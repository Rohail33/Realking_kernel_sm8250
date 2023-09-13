#ifndef __OPLUS_MMS_WIRED_H__
#define __OPLUS_MMS_WIRED_H__

#include <oplus_mms.h>

enum wired_topic_item {
	WIRED_ITEM_PRESENT,
	WIRED_ITEM_ONLINE,
	WIRED_ITEM_ERR_CODE,
	WIRED_ITEM_CHG_TYPE,
	WIRED_ITEM_BC12_COMPLETED,
	WIRED_ITEM_CC_MODE,
	WIRED_ITEM_CC_DETECT,
	WIRED_ITEM_USB_STATUS,
	WIRED_ITEM_USB_TEMP_VOLT_L,
	WIRED_ITEM_USB_TEMP_VOLT_R,
	WIRED_ITEM_USB_TEMP_L,
	WIRED_ITEM_USB_TEMP_R,
	WIRED_ITEM_OTG_ENABLE,
	WIRED_ITEM_CHARGER_CURR_MAX,
	WIRED_ITEM_CHARGER_VOL_MAX,
	WIRED_ITEM_CHARGER_VOL_MIN,
	WIRED_TIME_ABNORMAL_ADAPTER,
	WIRED_TIME_TYPEC_STATE,
};

enum oplus_wired_cc_detect_status {
	CC_DETECT_NULL,
	CC_DETECT_NOTPLUG,
	CC_DETECT_PLUGIN,
};

typedef enum {
	BCC_CURR_DONE_UNKNOW = 0,
	BCC_CURR_DONE_REQUEST,
	BCC_CURR_DONE_ACK,
}OPLUS_BCC_CURR_DONE_STATUS;

#define SMART_CHARGE_USER_USBTEMP	1
#define SMART_CHARGE_USER_OTHER		0

int oplus_wired_get_charger_cycle(void);
void oplus_wired_dump_regs(void);
int oplus_wired_get_vbus(void);
bool oplus_wired_get_vbus_collapse_status(void);
int oplus_wired_set_icl(int icl_ma, bool step);
int oplus_wired_set_icl_by_vooc(struct oplus_mms *topic, int icl_ma);
int oplus_wired_set_fcc(int fcc_ma);
int oplus_wired_smt_test(char buf[], int len);
bool oplus_wired_is_present(void);
int oplus_wired_input_enable(bool enable);
bool oplus_wired_input_is_enable(void);
int oplus_wired_output_enable(bool enable);
bool oplus_wired_output_is_enable(void);
int oplus_wired_get_icl(void);
int oplus_wired_set_fv(int fv_mv);
int oplus_wired_set_iterm(int iterm_ma);
int oplus_wired_set_rechg_vol(int vol_mv);
int oplus_wired_get_ibus(void);
int oplus_wired_get_chg_type(void);
int oplus_wired_otg_boost_enable(bool enable);
int oplus_wired_set_otg_boost_vol(int vol_mv);
int oplus_wired_set_otg_boost_curr_limit(int curr_ma);
int oplus_wired_aicl_enable(bool enable);
int oplus_wired_aicl_rerun(void);
int oplus_wired_aicl_reset(void);
int oplus_wired_get_cc_orientation(void);
int oplus_wired_get_hw_detect(void);
int oplus_wired_rerun_bc12(void);
int oplus_wired_qc_detect_enable(bool enable);
int oplus_wired_shipmode_enable(bool enable);
int oplus_wired_set_qc_config(enum oplus_chg_qc_version version, int vol_mv);
int oplus_wired_set_pd_config(u32 pdo);
int oplus_wired_get_usb_temp_volt(int *vol_l, int *vol_r);
int oplus_wired_get_usb_temp(int *temp_l, int *temp_r);
bool oplus_wired_usb_temp_check_is_support(void);
int oplus_wired_get_usb_btb_temp(void);
int oplus_wired_get_batt_btb_temp(void);
enum oplus_chg_typec_port_role_type oplus_wired_get_typec_mode(void);
int oplus_wired_set_typec_mode(enum oplus_chg_typec_port_role_type mode);
int oplus_wired_set_otg_switch_status(bool en);
bool oplus_wired_get_otg_switch_status(void);
int oplus_wired_wdt_enable(struct oplus_mms *topic, bool enable);
int oplus_wired_kick_wdt(struct oplus_mms *topic);
int oplus_wired_get_voltage_max(struct oplus_mms *topic);
int oplus_wired_get_shutdown_soc(struct oplus_mms *topic);
int oplus_wired_get_otg_online_status(struct oplus_mms *topic);
void oplus_wired_check_bcc_curr_done(struct oplus_mms *topic);
int oplus_wired_get_bcc_curr_done_status(struct oplus_mms *topic);
void oplus_wired_set_bcc_curr_request(struct oplus_mms *topic);
#endif /* __OPLUS_MMS_WIRED_H__ */
