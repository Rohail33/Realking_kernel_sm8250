// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __SM8350_CHARGER_H
#define __SM8350_CHARGER_H

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/rpmsg.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/soc/qcom/battery_charger.h>

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifndef CONFIG_FB
#define CONFIG_FB
#endif
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define OEM_OPCODE_READ_BUFFER    0x10000
#define OEM_OPCODE_DEBUG_BUFFER    0x10002
#define BCC_OPCODE_READ_BUFFER    0x10003
#define OEM_READ_WAIT_TIME_MS    500
#define MAX_OEM_PROPERTY_DATA_SIZE 64
#endif

#define MSG_OWNER_BC			32778
#define MSG_TYPE_REQ_RESP		1
#define MSG_TYPE_NOTIFY			2

/* opcode for battery charger */
#define BC_SET_NOTIFY_REQ		0x04
#define BC_NOTIFY_IND			0x07
#define BC_BATTERY_STATUS_GET		0x30
#define BC_BATTERY_STATUS_SET		0x31
#define BC_USB_STATUS_GET		0x32
#define BC_USB_STATUS_SET		0x33
#define BC_WLS_STATUS_GET		0x34
#define BC_WLS_STATUS_SET		0x35
#define BC_SHIP_MODE_REQ_SET		0x36
#define BC_WLS_FW_CHECK_UPDATE		0x40
#define BC_WLS_FW_PUSH_BUF_REQ		0x41
#define BC_WLS_FW_UPDATE_STATUS_RESP	0x42
#define BC_WLS_FW_PUSH_BUF_RESP		0x43
#define BC_WLS_FW_GET_VERSION		0x44
#define BC_SHUTDOWN_NOTIFY		0x47
#define BC_GENERIC_NOTIFY		0x80

#ifdef OPLUS_FEATURE_CHG_BASIC
#define BC_VOOC_STATUS_GET			0X47
#define BC_VOOC_STATUS_SET			0X48
#define BC_OTG_ENABLE					0x49
#define BC_OTG_DISABLE					0x50
#define BC_VOOC_VBUS_ADC_ENABLE		0x51
#define BC_CID_DETECT					0x52
#define BC_QC_DETECT					0x53
#define BC_TYPEC_STATE_CHANGE			0x54
#define BC_PD_SVOOC					0x55
#define BC_USB_PLUGIN_IN_EVENT			0x56
#define BC_USB_PLUGIN_OUT_EVENT			0x57
#define BC_PD_SOFT_RESET				0x58
#define BC_CHG_STATUS_GET				0x59
#define BC_CHG_STATUS_SET				0x60
#define BC_ABNORMAL_PD_SVOOC_ADAPTER			0x67
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define USB_TEMP_HIGH		0x01
#define USB_WATER_DETECT	0x02
#define USB_RESERVE2		0x04
#define USB_RESERVE3		0x08
#define USB_RESERVE4		0x10
#define USB_DONOT_USE		0x80000000
#endif

/* Generic definitions */
#define MAX_STR_LEN			128
#define BC_WAIT_TIME_MS			2000//sjc 1K->2K
#define WLS_FW_PREPARE_TIME_MS		300
#define WLS_FW_WAIT_TIME_MS		500
#define WLS_FW_BUF_SIZE			128
#define DEFAULT_RESTRICT_FCC_UA		1000000

#ifdef OPLUS_FEATURE_CHG_BASIC
struct oem_read_buffer_req_msg {
    struct pmic_glink_hdr hdr;
    u32 data_size;
};

struct oem_read_buffer_resp_msg {
    struct pmic_glink_hdr hdr;
    u32 data_buffer[MAX_OEM_PROPERTY_DATA_SIZE];
    u32 data_size;
};
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
typedef enum _PM_TYPEC_PORT_ROLE_TYPE
{
    TYPEC_PORT_ROLE_DRP,
    TYPEC_PORT_ROLE_SNK,
    TYPEC_PORT_ROLE_SRC,
    TYPEC_PORT_ROLE_DISABLE,
    TYPEC_PORT_ROLE_INVALID
} PM_TYPEC_PORT_ROLE_TYPE;

enum enum_check_type {
	CHECK_CHARGER_EXIST,
	CHECK_ENUM_STATUS,
};
#endif

enum psy_type {
	PSY_TYPE_BATTERY,
	PSY_TYPE_USB,
	PSY_TYPE_WLS,
	PSY_TYPE_AC,//sjc add
	PSY_TYPE_MAX,
};

enum ship_mode_type {
	SHIP_MODE_PMIC,
	SHIP_MODE_PACK_SIDE,
};

/* property ids */
enum battery_property_id {
	BATT_STATUS,
	BATT_HEALTH,
	BATT_PRESENT,
	BATT_CHG_TYPE,
	BATT_CAPACITY,
	BATT_SOH,
	BATT_VOLT_OCV,
	BATT_VOLT_NOW,
	BATT_VOLT_MAX,
	BATT_CURR_NOW,
	BATT_CHG_CTRL_LIM,
	BATT_CHG_CTRL_LIM_MAX,
	BATT_TEMP,
	BATT_TECHNOLOGY,
	BATT_CHG_COUNTER,
	BATT_CYCLE_COUNT,
	BATT_CHG_FULL_DESIGN,
	BATT_CHG_FULL,
	BATT_MODEL_NAME,
	BATT_TTF_AVG,
	BATT_TTE_AVG,
	BATT_RESISTANCE,
	BATT_POWER_NOW,
	BATT_POWER_AVG,
	BATT_CHG_EN,//sjc add
	BATT_SET_PDO,//sjc add
	BATT_SET_QC,//sjc add
	BATT_SET_SHIP_MODE,/*sjc add*/
	BATT_SET_COOL_DOWN,/*lzj add*/
	BATT_SET_MATCH_TEMP,/*lzj add*/
	BATT_BATTERY_AUTH,/*lzj add*/
	BATT_RTC_SOC,/*lzj add*/
	BATT_UEFI_INPUT_CURRENT,
	BATT_UEFI_PRE_CHG_CURRENT,
	BATT_UEFI_CHG_EN,
	BATT_UEFI_LOAD_ADSP,
	BATT_VSYSMIN_SET,
	BATT_BAT_FULL_VOL_SET,
	BATT_BAT_FULL_CURR_SET,
	BATT_SEND_CHG_STATUS,
	BATT_UPDATE_SOC_SMOOTH_PARAM,
	BATT_BATTERY_HMAC,
	BATT_SET_BCC_CURRENT,
	BATT_PROP_MAX,
};

enum usb_property_id {
	USB_ONLINE,
	USB_VOLT_NOW,
	USB_VOLT_MAX,
	USB_CURR_NOW,
	USB_CURR_MAX,
	USB_INPUT_CURR_LIMIT,
	USB_TYPE,
	USB_ADAP_TYPE,
	USB_MOISTURE_DET_EN,
	USB_MOISTURE_DET_STS,
	USB_ADAP_SUBTYPE,//sjc add
#ifdef OPLUS_FEATURE_CHG_BASIC
	USB_VBUS_COLLAPSE_STATUS,
	USB_VOOCPHY_STATUS,
	USB_VOOCPHY_ENABLE,
	USB_OTG_AP_ENABLE,
	USB_OTG_SWITCH,
	USB_TYPEC_CC_ORIENTATION,
	USB_CID_STATUS,
	USB_TYPEC_MODE,
	USB_TYPEC_SINKONLY,
	USB_OTG_VBUS_REGULATOR_ENABLE,
	USB_VOOC_CHG_PARAM_INFO,
	USB_VOOC_FAST_CHG_TYPE,
	USB_DEBUG_REG,
	USB_VOOCPHY_RESET_AGAIN,
	USB_SUSPEND_PMIC,
	USB_OEM_MISC_CTL,
	USB_CCDETECT_HAPPENED,
	USB_GET_PPS_TYPE,
	USB_GET_PPS_STATUS,
	USB_SET_PPS_VOLT,
	USB_SET_PPS_CURR,
	USB_GET_PPS_MAX_CURR,
	USB_PPS_READ_VBAT0_VOLT,
	USB_PPS_CHECK_BTB_TEMP,
	USB_PPS_MOS_CTRL,
	USB_SET_RERUN_AICL,
#endif /*OPLUS_FEATURE_CHG_BASIC*/
	USB_TEMP,
	USB_REAL_TYPE,
	USB_TYPEC_COMPLIANT,
	USB_PROP_MAX,
};


enum wireless_property_id {
	WLS_ONLINE,
	WLS_VOLT_NOW,
	WLS_VOLT_MAX,
	WLS_CURR_NOW,
	WLS_CURR_MAX,
	WLS_TYPE,
	WLS_BOOST_EN,
	WLS_INPUT_CURR_LIMIT = 8,
	WLS_BOOST_VOLT = 10,
	WLS_BOOST_AICL_ENABLE,
	WLS_BOOST_AICL_RERUN,
	WLS_AICL_CFG_VOLT,
	WLS_PROP_MAX,
};

enum {
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP = 0x80,
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3,
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5,
};

enum OTG_SCHEME {
	OTG_SCHEME_CID,
	OTG_SCHEME_CCDETECT_GPIO,
	OTG_SCHEME_SWITCH,
	OTG_SCHEME_UNDEFINE,
};

enum OEM_MISC_CTL_CMD {
	OEM_MISC_CTL_CMD_LCM_EN = 0,
	OEM_MISC_CTL_CMD_LCM_25K = 2,
	OEM_MISC_CTL_CMD_NCM_AUTO_MODE = 4,
	OEM_MISC_CTL_CMD_VPH_TRACK_HIGH = 6,
};

struct battery_charger_set_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			power_state;
	u32			low_capacity;
	u32			high_capacity;
};

struct battery_charger_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			notification;
};

struct battery_charger_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			property_id;
	u32			value;
};

struct battery_charger_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u32			value;
	u32			ret_code;
};

struct battery_model_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	char			model[MAX_STR_LEN];
};

struct wireless_fw_check_req {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
	u32			fw_size;
};

struct wireless_fw_check_resp {
	struct pmic_glink_hdr	hdr;
	u32			ret_code;
};

struct wireless_fw_push_buf_req {
	struct pmic_glink_hdr	hdr;
	u8			buf[WLS_FW_BUF_SIZE];
	u32			fw_chunk_id;
};

struct wireless_fw_push_buf_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_status;
};

struct wireless_fw_update_status {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_done;
};

struct wireless_fw_get_version_req {
	struct pmic_glink_hdr	hdr;
};

struct wireless_fw_get_version_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
};

struct battery_charger_ship_mode_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			ship_mode_type;
};

struct psy_state {
	struct power_supply	*psy;
	char			*model;
	const int		*map;
	u32			*prop;
	u32			prop_count;
	u32			opcode_get;
	u32			opcode_set;
};

#ifdef OPLUS_FEATURE_CHG_BASIC
struct oplus_custom_gpio_pinctrl {
	int vchg_trig_gpio;
	int ccdetect_gpio;
	struct mutex pinctrl_mutex;
	struct pinctrl *vchg_trig_pinctrl;
	struct pinctrl_state *vchg_trig_default;
	struct pinctrl		*ccdetect_pinctrl;
	struct pinctrl_state	*ccdetect_active;
	struct pinctrl_state	*ccdetect_sleep;
	struct pinctrl			*usbtemp_l_gpio_pinctrl;
	struct pinctrl_state	*usbtemp_l_gpio_default;
	struct pinctrl			*usbtemp_r_gpio_pinctrl;
	struct pinctrl_state	*usbtemp_r_gpio_default;
};
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
struct oplus_chg_iio {
	struct iio_channel	*usbtemp_v_chan;
	struct iio_channel	*usbtemp_sup_v_chan;
	struct iio_channel	*battcon_btb_chan;
	struct iio_channel	*usbcon_btb_chan;
	struct iio_channel	*subboard_temp_v_chan;
};
#endif

struct battery_chg_dev {
	struct device			*dev;
	struct class			battery_class;
	struct pmic_glink_client	*client;
	struct mutex			rw_lock;
	struct completion		ack;
	struct completion		fw_buf_ack;
	struct completion		fw_update_ack;
	struct psy_state		psy_list[PSY_TYPE_MAX];
	struct dentry			*debugfs_dir;
	u32				*thermal_levels;
	const char			*wls_fw_name;
	int				curr_thermal_level;
	int				num_thermal_levels;
	atomic_t			state;
	struct work_struct		subsys_up_work;
	struct work_struct		usb_type_work;
#ifdef OPLUS_FEATURE_CHG_BASIC
	int ccdetect_irq;
	struct delayed_work	suspend_check_work;
	struct delayed_work	adsp_voocphy_status_work;
	struct delayed_work	otg_init_work;
	struct delayed_work	ccdetect_work;
	struct delayed_work	cid_status_change_work;
	struct delayed_work	typec_state_change_work;
	struct delayed_work	chg_status_send_work;
	struct delayed_work	usbtemp_recover_work;
	struct delayed_work	adsp_crash_recover_work;
	struct delayed_work	check_charger_out_work;
	struct delayed_work	adsp_voocphy_enable_check_work;
	struct delayed_work	otg_vbus_enable_work;
	struct delayed_work	otg_status_check_work;
	struct delayed_work	vbus_adc_enable_work;
	struct delayed_work	oem_lcm_en_check_work;
	struct delayed_work	check_charger_type_work;
	struct delayed_work	unsuspend_usb_work;
	struct delayed_work	reset_turn_on_chg_work;
	struct delayed_work	get_real_chg_type_work;
	u32			oem_misc_ctl_data;
	bool			oem_usb_online;
	struct delayed_work	adsp_voocphy_err_work;
	bool					otg_prohibited;
	bool					otg_online;
	bool					is_chargepd_ready;
	bool					pd_svooc;
	bool					usb_online;
	struct oplus_chg_iio	iio;
	unsigned long long 	hvdcp_detect_time;
	unsigned long long 	hvdcp_detach_time;
	bool 				hvdcp_detect_ok;
	bool					hvdcp_disable;
	struct delayed_work 	hvdcp_disable_work;
	bool					adsp_voocphy_err_check;

	struct delayed_work status_keep_clean_work;
	struct delayed_work usb_enum_check_work;
	struct wakeup_source *status_wake_lock;
	bool status_wake_lock_on;
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	int vchg_trig_irq;
	struct delayed_work vchg_trig_work;
	struct delayed_work wait_wired_charge_on;
	struct delayed_work wait_wired_charge_off;
	bool wls_fw_update;
#endif /*OPLUS_FEATURE_CHG_BASIC*/
	int				fake_soc;
	bool				block_tx;
	bool				ship_mode_en;
	bool				debug_battery_detected;
	bool				wls_fw_update_reqd;
	u32				wls_fw_version;
	struct notifier_block		reboot_notifier;
	u32				thermal_fcc_ua;
	u32				restrict_fcc_ua;
	bool				restrict_chg_en;
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_custom_gpio_pinctrl oplus_custom_gpio;
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct mutex    read_buffer_lock;
	struct completion    oem_read_ack;
	struct oem_read_buffer_resp_msg  read_buffer_dump;
	struct oem_read_buffer_resp_msg  debug_buffer;
	struct mutex    bcc_read_buffer_lock;
	struct completion    bcc_read_ack;
	struct oem_read_buffer_resp_msg  bcc_read_buffer_dump;
	int otg_scheme;
	bool pmic_is_pm7250b;
	int pre_current;
	int real_chg_type;
#endif
};

static int time_count = 0;
static bool usbtemp_check_status = false;

void usbtemp_reset_variables(void);

struct usbtemp_curr_monitor {
	int batt_curr;
	int temp_delta;
	int reset_time;
};

static struct usbtemp_curr_monitor temp_curr_monitor_table[] = {
	{5000, 57, 0}
};

/**********************************************************************
 **********************************************************************/

enum skip_reason {
	REASON_OTG_ENABLED	= BIT(0),
	REASON_FLASH_ENABLED	= BIT(1)
};

struct qcom_pmic {
	struct battery_chg_dev *bcdev_chip;

	/* for complie*/
	bool			otg_pulse_skip_dis;
	int			pulse_cnt;
	unsigned int	therm_lvl_sel;
	bool			psy_registered;
	int			usb_online;

	/* copy from msm8976_pmic begin */
	int			bat_charging_state;
	bool	 		suspending;
	bool			aicl_suspend;
	bool			usb_hc_mode;
	int    		usb_hc_count;
	bool			hc_mode_flag;
	/* copy form msm8976_pmic end */
};

enum {
	ADAPTER_ID_20W_0X13 = 0x13,
	ADAPTER_ID_20W_0X34 = 0x34,
	ADAPTER_ID_20W_0X45 = 0x45,
	ADAPTER_ID_30W_0X19 = 0x19,
	ADAPTER_ID_30W_0X29 = 0x29,
	ADAPTER_ID_30W_0X41 = 0x41,
	ADAPTER_ID_30W_0X42 = 0x42,
	ADAPTER_ID_30W_0X43 = 0x43,
	ADAPTER_ID_30W_0X44 = 0x44,
	ADAPTER_ID_30W_0X46 = 0x46,
	ADAPTER_ID_33W_0X49 = 0x49,
	ADAPTER_ID_33W_0X4A = 0x4A,
	ADAPTER_ID_33W_0X61 = 0x61,
	ADAPTER_ID_50W_0X11 = 0x11,
	ADAPTER_ID_50W_0X12 = 0x12,
	ADAPTER_ID_50W_0X21 = 0x21,
	ADAPTER_ID_50W_0X31 = 0x31,
	ADAPTER_ID_50W_0X33 = 0x33,
	ADAPTER_ID_50W_0X62 = 0x62,
	ADAPTER_ID_65W_0X14 = 0x14,
	ADAPTER_ID_65W_0X35 = 0x35,
	ADAPTER_ID_65W_0X63 = 0x63,
	ADAPTER_ID_65W_0X66 = 0x66,
	ADAPTER_ID_65W_0X6E = 0x6E,
	ADAPTER_ID_66W_0X36 = 0x36,
	ADAPTER_ID_66W_0X64 = 0x64,
	ADAPTER_ID_67W_0X6C = 0x6C,
	ADAPTER_ID_67W_0X6D = 0x6D,
	ADAPTER_ID_80W_0X4B = 0x4B,
	ADAPTER_ID_80W_0X4C = 0x4C,
	ADAPTER_ID_80W_0X4D = 0x4D,
	ADAPTER_ID_80W_0X4E = 0x4E,
	ADAPTER_ID_80W_0X65 = 0x65,
	ADAPTER_ID_100W_0X69 = 0x69,
	ADAPTER_ID_100W_0X6A = 0x6A,
	ADAPTER_ID_120W_0X32 = 0x32,
	ADAPTER_ID_120W_0X6B = 0x6B,
};
#endif /*__SM8350_CHARGER_H*/
