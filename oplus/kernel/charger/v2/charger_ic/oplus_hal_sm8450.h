// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __SM8450_CHARGER_H
#define __SM8450_CHARGER_H

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
#include <oplus_chg_ic.h>

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifndef CONFIG_FB
#define CONFIG_FB
#endif
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define OEM_OPCODE_READ_BUFFER    0x10000
#define OEM_READ_WAIT_TIME_MS    500
#define MAX_OEM_PROPERTY_DATA_SIZE 128
#define QC_TYPE_CHECK_INTERVAL 200 /* ms */
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
#define BC_VOOC_STATUS_GET		0X48
#define BC_VOOC_STATUS_SET		0X49
#define BC_OTG_ENABLE			0x50
#define BC_OTG_DISABLE			0x51
#define BC_VOOC_VBUS_ADC_ENABLE		0x52
#define BC_CID_DETECT			0x53
#define BC_QC_DETECT			0x54
#define BC_TYPEC_STATE_CHANGE		0x55
#define BC_PD_SVOOC			0x56
#define BC_PLUGIN_IRQ 			0x57
#define BC_APSD_DONE			0x58
#define BC_CHG_STATUS_GET		0x59
#define BC_PD_SOFT_RESET		0x5A
#define BC_CHG_STATUS_SET		0x60
#define BC_ADSP_NOTIFY_AP_SUSPEND_CHG	0X61
#define BC_ADSP_NOTIFY_AP_CP_BYPASS_INIT	0x0062
#define BC_ADSP_NOTIFY_AP_CP_MOS_ENABLE		0x0063
#define BC_ADSP_NOTIFY_AP_CP_MOS_DISABLE	0x0064
#define BC_PPS_OPLUS				0x65
#define BC_ADSP_NOTIFY_TRACK			0x66
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#define USB_OTG_CURR_LIMIT_MAX	3000
#define USB_OTG_CURR_LIMIT_HIGH	1700
#define USB_OTG_REAL_SOC_MIN	10
#endif

/* Generic definitions */
#define MAX_STR_LEN			128
#define BC_WAIT_TIME_MS			2500/* sjc 1K->2K */
#define WLS_FW_PREPARE_TIME_MS		300
#define WLS_FW_WAIT_TIME_MS		500
#define WLS_FW_BUF_SIZE			128
#define DEFAULT_RESTRICT_FCC_UA		1000000
#define BATTMNGR_EFAILED                512 /*Error: i2c Operation Failed*/

#ifdef OPLUS_FEATURE_CHG_BASIC
struct ntc_table_855 {
	int temperature;	/* ohm */
	int volt;		/* 0.1*celsius */
};

#define resistance_convert_temperature_855(V, T, i, table)	\
	do {							\
		for (i = 0; i < ARRAY_SIZE(table); i ++)	\
			if (table[i].volt <= V)			\
				break;				\
		if (i == 0)					\
			T = table[0].temperature;		\
		else if (i == ARRAY_SIZE(table))		\
			T = table[ARRAY_SIZE(table) - 1].temperature;	\
		else						\
			T = table[i].temperature * (V - table[i - 1].volt) / (table[i].volt - table[i - 1].volt) +	\
				table[i - 1].temperature * (table[i].volt - V) / (table[i].volt - table[i - 1].volt);	\
	} while (0)

static struct ntc_table_855 con_temp_volt_855[] = {
	{-400, 1831},
	{-390, 1828},
	{-380, 1825},
	{-370, 1822},
	{-360, 1818},
	{-350, 1814},
	{-340, 1810},
	{-330, 1806},
	{-320, 1801},
	{-310, 1796},
	{-300, 1791},
	{-290, 1786},
	{-280, 1780},
	{-270, 1774},
	{-260, 1768},
	{-250, 1761},
	{-240, 1754},
	{-230, 1747},
	{-220, 1739},
	{-210, 1731},
	{-200, 1725},
	{-190, 1716},
	{-180, 1707},
	{-170, 1697},
	{-160, 1687},
	{-150, 1677},
	{-140, 1666},
	{-130, 1655},
	{-120, 1643},
	{-110, 1631},
	{-100, 1618},
	{-90, 1605},
	{-80, 1591},
	{-70, 1577},
	{-60, 1562},
	{-50, 1547},
	{-40, 1531},
	{-30, 1515},
	{-20, 1499},
	{-10, 1482},
	{00, 1465},
	{10, 1447},
	{20, 1429},
	{30, 1410},
	{40, 1391},
	{50, 1372},
	{60, 1352},
	{70, 1332},
	{80, 1311},
	{90, 1291},
	{100, 1270},
	{110, 1248},
	{120, 1227},
	{130, 1205},
	{140, 1183},
	{150, 1161},
	{160, 1139},
	{170, 1117},
	{180, 1094},
	{190, 1072},
	{200, 1049},
	{210, 1027},
	{220, 1004},
	{230, 982},
	{240, 960},
	{250, 938},
	{260, 915},
	{270, 893},
	{280, 872},
	{290, 850},
	{300, 829},
	{310, 808},
	{320, 787},
	{330, 766},
	{340, 746},
	{350, 726},
	{360, 706},
	{370, 687},
	{380, 668},
	{390, 649},
	{400, 631},
	{410, 613},
	{420, 595},
	{430, 578},
	{440, 561},
	{450, 544},
	{460, 528},
	{470, 512},
	{480, 497},
	{490, 482},
	{500, 467},
	{510, 453},
	{520, 439},
	{530, 426},
	{540, 412},
	{550, 400},
	{560, 387},
	{570, 375},
	{580, 363},
	{590, 352},
	{600, 341},
	{610, 330},
	{620, 320},
	{630, 310},
	{640, 300},
	{650, 290},
	{660, 281},
	{670, 272},
	{680, 264},
	{690, 255},
	{700, 247},
	{710, 239},
	{720, 232},
	{730, 224},
	{740, 217},
	{750, 210},
	{760, 204},
	{770, 197},
	{780, 191},
	{790, 185},
	{800, 179},
	{810, 174},
	{820, 168},
	{830, 163},
	{840, 158},
	{850, 153},
	{860, 148},
	{870, 143},
	{880, 139},
	{890, 135},
	{900, 131},
	{910, 126},
	{920, 123},
	{930, 119},
	{940, 115},
	{950, 112},
	{960, 108},
	{970, 105},
	{980, 102},
	{990, 99},
	{1000, 96},
	{1010, 93},
	{1020, 90},
	{1030, 87},
	{1040, 85},
	{1050, 82},
	{1060, 80},
	{1070, 78},
	{1080, 75},
	{1090, 73},
	{1100, 71},
	{1110, 69},
	{1120, 67},
	{1130, 65},
	{1140, 63},
	{1150, 61},
	{1160, 60},
	{1170, 58},
	{1180, 56},
	{1190, 55},
	{1200, 53},
	{1210, 52},
	{1220, 50},
	{1230, 49},
	{1240, 47},
	{1250, 46},
};

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

enum lcm_en_status {
	LCM_EN_DEAFULT = 1,
	LCM_EN_ENABLE,
	LCM_EN_DISABLE,
};

enum psy_type {
	PSY_TYPE_BATTERY,
	PSY_TYPE_USB,
	PSY_TYPE_WLS,
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
#ifdef OPLUS_FEATURE_CHG_BASIC
	BATT_CHG_EN,/* sjc add */
	BATT_SET_PDO,/* sjc add */
	BATT_SET_QC,/* sjc add */
	BATT_SET_SHIP_MODE,/*sjc add*/
	BATT_SET_COOL_DOWN,/*lzj add*/
	BATT_SET_MATCH_TEMP,/*lzj add*/
	BATT_BATTERY_AUTH,/*lzj add*/
	BATT_RTC_SOC,/*lzj add*/
	BATT_UEFI_INPUT_CURRENT,
	BATT_UEFI_PRE_CHG_CURRENT,
	BATT_UEFI_UEFI_CHG_EN,
	BATT_UEFI_LOAD_ADSP,
	BATT_UEFI_SET_VSYSTEM_MIN,
	BATT_SEND_CHG_STATUS,
	BATT_ADSP_GAUGE_INIT,
	BATT_UPDATE_SOC_SMOOTH_PARAM,
	BATT_BATTERY_HMAC,
	BATT_SET_BCC_CURRENT,
	BATT_ZY0603_CHECK_RC_SFR,
	BATT_ZY0603_SOFT_RESET,
	BATT_AFI_UPDATE_DONE,
#endif
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
#ifdef OPLUS_FEATURE_CHG_BASIC
	USB_ADAP_SUBTYPE,
	USB_VBUS_COLLAPSE_STATUS,
	USB_VOOCPHY_STATUS,
	USB_VOOCPHY_ENABLE,
	USB_OTG_AP_ENABLE,
	USB_OTG_SWITCH,
	USB_POWER_SUPPLY_RELEASE_FIXED_FREQUENCE,
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
	USB_PPS_CP_MODE_INIT,
	USB_PPS_CHECK_AUTHENTICATE,
	USB_PPS_GET_AUTHENTICATE,
	USB_PPS_GET_CP_VBUS,
	USB_PPS_GET_CP_MASTER_IBUS,
	USB_PPS_GET_CP_SLAVE_IBUS,
	USB_PPS_MOS_SLAVE_CTRL,
	USB_PPS_GET_R_COOL_DOWN,
	USB_PPS_GET_DISCONNECT_STATUS,
	USB_PPS_VOOCPHY_ENABLE,
	USB_IN_STATUS,
	USB_GET_BATT_CURR,
	/*USB_ADSP_TRACK_DEBUG,*/
#endif /*OPLUS_FEATURE_CHG_BASIC*/
	USB_TEMP,
	USB_REAL_TYPE,
	USB_TYPEC_COMPLIANT,
#ifdef OPLUS_FEATURE_CHG_BASIC
	USB_PPS_FORCE_SVOOC,
#endif /*OPLUS_FEATURE_CHG_BASIC*/
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
#ifdef OPLUS_FEATURE_CHG_BASIC
	WLS_INPUT_CURR_LIMIT = 8,
	WLS_BOOST_VOLT = 10,
	WLS_BOOST_AICL_ENABLE,
	WLS_BOOST_AICL_RERUN,
#endif
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

enum OTG_BOOST_SOURCE {
	OTG_BOOST_SOURCE_PMIC,
	OTG_BOOST_SOURCE_EXTERNAL,
};

enum OEM_MISC_CTL_CMD {
	OEM_MISC_CTL_CMD_LCM_EN = 0,
	OEM_MISC_CTL_CMD_LCM_25K = 2,
	OEM_MISC_CTL_CMD_NCM_AUTO_MODE = 4,
	OEM_MISC_CTL_CMD_VPH_TRACK_HIGH = 6,
};

typedef enum _QCOM_PM_TYPEC_PORT_ROLE_TYPE
{
        QCOM_TYPEC_PORT_ROLE_DRP,
        QCOM_TYPEC_PORT_ROLE_SNK,
        QCOM_TYPEC_PORT_ROLE_SRC,
        QCOM_TYPEC_PORT_ROLE_DISABLE,
        QCOM_TYPEC_PORT_ROLE_INVALID
} QCOM_PM_TYPEC_PORT_ROLE_TYPE;

QCOM_PM_TYPEC_PORT_ROLE_TYPE qcom_typec_port_role[] = {
	QCOM_TYPEC_PORT_ROLE_DRP,
	QCOM_TYPEC_PORT_ROLE_SNK,
	QCOM_TYPEC_PORT_ROLE_SRC,
	QCOM_TYPEC_PORT_ROLE_DRP,
	QCOM_TYPEC_PORT_ROLE_INVALID,
	QCOM_TYPEC_PORT_ROLE_DISABLE,
	QCOM_TYPEC_PORT_ROLE_INVALID,
	QCOM_TYPEC_PORT_ROLE_INVALID
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
	u32                     fw_crc;
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
	int otg_boost_en_gpio;
	int otg_ovp_en_gpio;
	int mcu_en_gpio;
	struct mutex pinctrl_mutex;
	struct pinctrl *vchg_trig_pinctrl;
	struct pinctrl_state *vchg_trig_default;
	struct pinctrl			*usbtemp_l_gpio_pinctrl;
	struct pinctrl_state	*usbtemp_l_gpio_default;
	struct pinctrl			*usbtemp_r_gpio_pinctrl;
	struct pinctrl_state	*usbtemp_r_gpio_default;
	struct pinctrl		*subboard_temp_gpio_pinctrl;
	struct pinctrl_state	*subboard_temp_gpio_default;
	struct pinctrl          *otg_boost_en_pinctrl;
	struct pinctrl_state    *otg_boost_en_active;
	struct pinctrl_state    *otg_boost_en_sleep;
	struct pinctrl          *otg_ovp_en_pinctrl;
	struct pinctrl_state    *otg_ovp_en_active;
	struct pinctrl_state    *otg_ovp_en_sleep;
	struct pinctrl		*mcu_en_pinctrl;
	struct pinctrl_state	*mcu_en_active;
	struct pinctrl_state	*mcu_en_sleep;
};

#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
struct oplus_chg_iio {
	struct iio_channel	*usbtemp_v_chan;
	struct iio_channel	*usbtemp_sup_v_chan;
	struct iio_channel	*subboard_temp_chan;
	struct iio_channel	*batt0_con_btb_chan;
	struct iio_channel	*batt1_con_btb_chan;
	struct iio_channel	*usbcon_btb_chan;
};
#endif

struct battery_chg_dev {
	struct device			*dev;
#ifdef OPLUS_FEATURE_CHG_BASIC
	struct oplus_chg_ic_dev		*buck_ic;
	struct oplus_chg_ic_dev		*gauge_ic;
	struct oplus_mms		*vooc_topic;
	struct oplus_mms		*common_topic;
	struct votable			*chg_disable_votable;
#endif
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
	int				charger_type;
	atomic_t			state;
	struct work_struct		subsys_up_work;
	struct work_struct		usb_type_work;
#ifdef OPLUS_FEATURE_CHG_BASIC
	int ccdetect_irq;
	struct delayed_work	suspend_check_work;
	struct delayed_work	adsp_voocphy_status_work;
	struct delayed_work	otg_init_work;
	struct delayed_work	cid_status_change_work;
	struct delayed_work	usbtemp_recover_work;
	struct delayed_work	adsp_crash_recover_work;
	struct delayed_work	voocphy_enable_check_work;
	struct delayed_work	otg_vbus_enable_work;
	struct delayed_work	otg_status_check_work;
	struct delayed_work	vbus_adc_enable_work;
	struct delayed_work	plugin_irq_work;
	struct delayed_work	recheck_input_current_work;
	struct delayed_work	qc_type_check_work;
	struct delayed_work	unsuspend_usb_work;
	struct delayed_work	oem_lcm_en_check_work;
	struct delayed_work	ctrl_lcm_frequency;
	u32			oem_misc_ctl_data;
	bool			oem_usb_online;
	bool			oem_lcm_check;
	struct delayed_work	voocphy_err_work;
	bool					otg_prohibited;
	bool					otg_online;
	bool					is_chargepd_ready;
	bool					pd_svooc;
	bool			is_external_chg;
	struct oplus_chg_iio	iio;
	unsigned long long 	hvdcp_detect_time;
	unsigned long long 	hvdcp_detach_time;
	bool 				hvdcp_detect_ok;
	bool					hvdcp_disable;
	struct delayed_work 	hvdcp_disable_work;
	bool					voocphy_err_check;
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	int vchg_trig_irq;
	struct delayed_work vchg_trig_work;
	struct delayed_work wait_wired_charge_on;
	struct delayed_work wait_wired_charge_off;
	struct delayed_work mcu_en_init_work;
	bool wls_fw_update;
	bool voocphy_bidirect_cp_support;
	int batt_num;
	bool	cid_status;
	atomic_t suspended;
#endif /*OPLUS_FEATURE_CHG_BASIC*/
	int				fake_soc;
	bool				block_tx;
	bool				ship_mode_en;
	bool				debug_battery_detected;
	bool				wls_fw_update_reqd;
	u32				wls_fw_version;
	u16				wls_fw_crc;
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
	int otg_scheme;
	int otg_boost_src;
	int otg_curr_limit_max;
	int otg_curr_limit_high;
	int otg_real_soc_min;
	struct notifier_block	ssr_nb;
	void		*subsys_handle;
	int usb_in_status;
#endif
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

#ifdef OPLUS_FEATURE_CHG_BASIC
int oplus_adsp_voocphy_get_fast_chg_type(void);
int oplus_adsp_voocphy_enable(bool enable);
int oplus_adsp_voocphy_reset_again(void);
#endif
#endif /*__SM8450_CHARGER_H*/
