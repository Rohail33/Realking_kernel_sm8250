#ifndef __OPLUS_HAL_VOOC_H__
#define __OPLUS_HAL_VOOC_H__

enum oplus_chg_vooc_switch_mode {
	VOOC_SWITCH_MODE_NORMAL,
	VOOC_SWITCH_MODE_VOOC,
	VOOC_SWITCH_MODE_HEADPHONE,
};

enum oplus_chg_vooc_ic_type {
	OPLUS_VOOC_IC_UNKNOWN,
	OPLUS_VOOC_IC_RK826,
	OPLUS_VOOC_IC_OP10,
	OPLUS_VOOC_IC_RT5125,
	OPLUS_VPHY_IC_VPHY,
};

enum {
	FW_ERROR_DATA_MODE,
	FW_NO_CHECK_MODE,
	FW_CHECK_MODE,
};

void oplus_vooc_eint_register(struct oplus_chg_ic_dev *vooc_ic);
void oplus_vooc_eint_unregister(struct oplus_chg_ic_dev *vooc_ic);
int oplus_vooc_read_ap_data(struct oplus_chg_ic_dev *vooc_ic);
void oplus_vooc_set_data_active(struct oplus_chg_ic_dev *vooc_ic);
void oplus_vooc_set_data_sleep(struct oplus_chg_ic_dev *vooc_ic);
int oplus_vooc_set_reset_sleep(struct oplus_chg_ic_dev *vooc_ic);
int oplus_vooc_set_reset_active(struct oplus_chg_ic_dev *vooc_ic);
int oplus_vooc_set_clock_sleep(struct oplus_chg_ic_dev *vooc_ic);
int oplus_vooc_set_clock_active(struct oplus_chg_ic_dev *vooc_ic);
void oplus_vooc_reply_data(struct oplus_chg_ic_dev *vooc_ic, int ret_info,
			   int device_type, int data_width, int curr_ma);
void oplus_vooc_reply_data_no_type(struct oplus_chg_ic_dev *vooc_ic,
				   int ret_info, int data_width, int curr_ma);
void switch_fast_chg(struct oplus_chg_ic_dev *vooc_ic);
void switch_normal_chg(struct oplus_chg_ic_dev *vooc_ic);
enum oplus_chg_vooc_switch_mode oplus_chg_vooc_get_switch_mode(struct oplus_chg_ic_dev *vooc_ic);
int oplus_vooc_fw_check_then_recover(struct oplus_chg_ic_dev *vooc_ic);
int oplus_vooc_fw_check_then_recover_fix(struct oplus_chg_ic_dev *vooc_ic);
bool oplus_vooc_asic_fw_status(struct oplus_chg_ic_dev *vooc_ic);
int oplus_hal_vooc_init(struct oplus_chg_ic_dev *vooc_ic);
int set_chg_auto_mode(struct oplus_chg_ic_dev *vooc_ic, bool enable);
#endif /* __OPLUS_HAL_VOOC_H__ */
