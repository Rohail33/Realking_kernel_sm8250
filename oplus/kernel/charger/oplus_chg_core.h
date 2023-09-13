// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef _OPLUS_CHG_CORE_H_
#define _OPLUS_CHG_CORE_H_

#include <linux/device.h>
#include <linux/version.h>

struct device;
struct device_type;
struct oplus_chg_mod;

#ifdef OPLUS_CHG_DEBUG_LOG
#undef pr_info
#undef pr_debug
#define pr_info pr_err
#define pr_debug pr_err
#endif

enum oplus_chg_mod_type {
	OPLUS_CHG_MOD_COMMON,
	OPLUS_CHG_MOD_USB,
	OPLUS_CHG_MOD_WIRELESS,
	OPLUS_CHG_MOD_BATTERY,
	OPLUS_CHG_MOD_MAIN,
	OPLUS_CHG_MOD_TRACK,
};

enum oplus_chg_event {
	OPLUS_CHG_EVENT_CHANGED,
	OPLUS_CHG_EVENT_ONLINE,
	OPLUS_CHG_EVENT_OFFLINE,
	OPLUS_CHG_EVENT_PRESENT,
	OPLUS_CHG_EVENT_NO_PRESENT,
	OPLUS_CHG_EVENT_APSD_DONE,
	OPLUS_CHG_EVENT_LCD_ON,
	OPLUS_CHG_EVENT_LCD_OFF,
	OPLUS_CHG_EVENT_CALL_ON,
	OPLUS_CHG_EVENT_CALL_OFF,
	OPLUS_CHG_EVENT_CAMERA_ON,
	OPLUS_CHG_EVENT_CAMERA_OFF,
	OPLUS_CHG_EVENT_OP_TRX,
	OPLUS_CHG_EVENT_CHECK_TRX,
	OPLUS_CHG_EVENT_ADSP_STARTED,
	OPLUS_CHG_EVENT_OTG_ENABLE,
	OPLUS_CHG_EVENT_OTG_DISABLE,
	OPLUS_CHG_EVENT_POWER_CHANGED,
	OPLUS_CHG_EVENT_CHARGE_DONE,
	OPLUS_CHG_EVENT_CLEAN_CHARGE_DONE,
	OPLUS_CHG_EVENT_SVOOC_ONLINE,
	OPLUS_CHG_EVENT_RX_IIC_ERR,
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	OPLUS_CHG_EVENT_REG_DUMP,
#endif
	OPLUS_CHG_EVENT_RX_FAST_ERR,
	OPLUS_CHG_EVENT_TX_EPP_CAP,
};

enum {
	OPLUS_CHG_STATUS_UNKNOWN = 0,
	OPLUS_CHG_STATUS_CHARGING,
	OPLUS_CHG_STATUS_DISCHARGING,
	OPLUS_CHG_STATUS_NOT_CHARGING,
	OPLUS_CHG_STATUS_FULL,
};

/* What algorithm is the charger using? */
enum {
	OPLUS_CHG_CHARGE_TYPE_UNKNOWN = 0,
	OPLUS_CHG_CHARGE_TYPE_NONE,
	OPLUS_CHG_CHARGE_TYPE_TRICKLE, /* slow speed */
	OPLUS_CHG_CHARGE_TYPE_FAST, /* fast speed */
	OPLUS_CHG_CHARGE_TYPE_STANDARD, /* normal speed */
	OPLUS_CHG_CHARGE_TYPE_ADAPTIVE, /* dynamically adjusted speed */
	OPLUS_CHG_CHARGE_TYPE_CUSTOM, /* use CHARGE_CONTROL_* props */
};

enum {
	OPLUS_CHG_HEALTH_UNKNOWN = 0,
	OPLUS_CHG_HEALTH_GOOD,
	OPLUS_CHG_HEALTH_OVERHEAT,
	OPLUS_CHG_HEALTH_DEAD,
	OPLUS_CHG_HEALTH_OVERVOLTAGE,
	OPLUS_CHG_HEALTH_UNSPEC_FAILURE,
	OPLUS_CHG_HEALTH_COLD,
	OPLUS_CHG_HEALTH_WATCHDOG_TIMER_EXPIRE,
	OPLUS_CHG_HEALTH_SAFETY_TIMER_EXPIRE,
	OPLUS_CHG_HEALTH_OVERCURRENT,
	OPLUS_CHG_HEALTH_WARM,
	OPLUS_CHG_HEALTH_COOL,
	OPLUS_CHG_HEALTH_HOT,
};

enum {
	OPLUS_CHG_TECHNOLOGY_UNKNOWN = 0,
	OPLUS_CHG_TECHNOLOGY_NiMH,
	OPLUS_CHG_TECHNOLOGY_LION,
	OPLUS_CHG_TECHNOLOGY_LIPO,
	OPLUS_CHG_TECHNOLOGY_LiFe,
	OPLUS_CHG_TECHNOLOGY_NiCd,
	OPLUS_CHG_TECHNOLOGY_LiMn,
};

enum {
	OPLUS_CHG_CAPACITY_LEVEL_UNKNOWN = 0,
	OPLUS_CHG_CAPACITY_LEVEL_CRITICAL,
	OPLUS_CHG_CAPACITY_LEVEL_LOW,
	OPLUS_CHG_CAPACITY_LEVEL_NORMAL,
	OPLUS_CHG_CAPACITY_LEVEL_HIGH,
	OPLUS_CHG_CAPACITY_LEVEL_FULL,
};

enum {
	OPLUS_CHG_SCOPE_UNKNOWN = 0,
	OPLUS_CHG_SCOPE_SYSTEM,
	OPLUS_CHG_SCOPE_DEVICE,
};

enum oplus_chg_usb_type {
	OPLUS_CHG_USB_TYPE_UNKNOWN = 0,
	OPLUS_CHG_USB_TYPE_SDP,
	OPLUS_CHG_USB_TYPE_DCP,
	OPLUS_CHG_USB_TYPE_CDP,
	OPLUS_CHG_USB_TYPE_ACA,
	OPLUS_CHG_USB_TYPE_C,
	OPLUS_CHG_USB_TYPE_PD,
	OPLUS_CHG_USB_TYPE_PD_DRP,
	OPLUS_CHG_USB_TYPE_PD_PPS,
	OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID,
	OPLUS_CHG_USB_TYPE_QC2,
	OPLUS_CHG_USB_TYPE_QC3,
	OPLUS_CHG_USB_TYPE_VOOC,
	OPLUS_CHG_USB_TYPE_SVOOC,
};

enum oplus_chg_wls_type {
	OPLUS_CHG_WLS_UNKNOWN,
	OPLUS_CHG_WLS_BPP,
	OPLUS_CHG_WLS_EPP,
	OPLUS_CHG_WLS_EPP_PLUS,
	OPLUS_CHG_WLS_VOOC,
	OPLUS_CHG_WLS_SVOOC,
	OPLUS_CHG_WLS_PD_65W,
	OPLUS_CHG_WLS_TRX,
};

enum oplus_chg_temp_region_type {
	OPLUS_CHG_BATT_TEMP_COLD = 0,
	OPLUS_CHG_BATT_TEMP_LITTLE_COLD,
	OPLUS_CHG_BATT_TEMP_COOL,
	OPLUS_CHG_BATT_TEMP_LITTLE_COOL,
	OPLUS_CHG_BATT_TEMP_PRE_NORMAL,
	OPLUS_CHG_BATT_TEMP_NORMAL,
	OPLUS_CHG_BATT_TEMP_WARM,
	OPLUS_CHG_BATT_TEMP_HOT,
	OPLUS_CHG_BATT_TEMP_INVALID,
};

enum oplus_chg_wls_rx_mode {
	OPLUS_CHG_WLS_RX_MODE_UNKNOWN,
	OPLUS_CHG_WLS_RX_MODE_BPP,
	OPLUS_CHG_WLS_RX_MODE_EPP,
	OPLUS_CHG_WLS_RX_MODE_EPP_PLUS,
	OPLUS_CHG_WLS_RX_MODE_EPP_5W,
};

enum oplus_chg_wls_trx_status {
	OPLUS_CHG_WLS_TRX_STATUS_ENABLE,
	OPLUS_CHG_WLS_TRX_STATUS_CHARGING,
	OPLUS_CHG_WLS_TRX_STATUS_DISENABLE,
};

enum oplus_chg_mod_property {
	OPLUS_CHG_PROP_TYPE,
	OPLUS_CHG_PROP_STATUS,
	OPLUS_CHG_PROP_ONLINE,
	OPLUS_CHG_PROP_PRESENT,
	OPLUS_CHG_PROP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_VOLTAGE_MAX,
	OPLUS_CHG_PROP_VOLTAGE_MIN,
	OPLUS_CHG_PROP_CURRENT_NOW,
	OPLUS_CHG_PROP_CURRENT_MAX,
	OPLUS_CHG_PROP_INPUT_CURRENT_NOW,
	OPLUS_CHG_PROP_USB_TYPE,
	OPLUS_CHG_PROP_FASTCHG_STATUS,
	OPLUS_CHG_PROP_ADAPTER_SID,
	OPLUS_CHG_PROP_ADAPTER_TYPE,
	OPLUS_CHG_PROP_DOCK_TYPE,
	OPLUS_CHG_PROP_WLS_SKEW_CURR,
	OPLUS_CHG_PROP_VERITY,
	OPLUS_CHG_PROP_TEMP_REGION,
	OPLUS_CHG_PROP_CON_TEMP1,
	OPLUS_CHG_PROP_CON_TEMP2,
	OPLUS_CHG_PROP_CHG_ENABLE,
	OPLUS_CHG_PROP_OTG_MODE,
	OPLUS_CHG_PROP_TRX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TRX_CURRENT_NOW,
	OPLUS_CHG_PROP_TRX_STATUS,
	OPLUS_CHG_PROP_TRX_ONLINE,
	OPLUS_CHG_PROP_WLS_TYPE,
	OPLUS_CHG_PROP_DEVIATED,
	OPLUS_CHG_PROP_FORCE_TYPE,
	OPLUS_CHG_PROP_STATUS_DELAY,
	OPLUS_CHG_PROP_PATH_CTRL,
	OPLUS_CHG_PROP_QUIET_MODE,
	OPLUS_CHG_PROP_VRECT_NOW,
	OPLUS_CHG_PROP_TRX_POWER_EN,
	OPLUS_CHG_PROP_TRX_POWER_VOL,
	OPLUS_CHG_PROP_TRX_POWER_CURR_LIMIT,
	OPLUS_CHG_PROP_CAPACITY,
	OPLUS_CHG_PROP_REAL_CAPACITY,
	OPLUS_CHG_PROP_CHARGE_TYPE,
	OPLUS_CHG_PROP_CELL_NUM,
	OPLUS_CHG_PROP_MODEL_NAME,
	OPLUS_CHG_PROP_TEMP,
	OPLUS_CHG_PROP_HEALTH,
	OPLUS_CHG_PROP_TECHNOLOGY,
	OPLUS_CHG_PROP_CYCLE_COUNT,
	OPLUS_CHG_PROP_VOLTAGE_OCV,
	OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT,
	OPLUS_CHG_PROP_CHARGE_CONTROL_LIMIT_MAX,
	OPLUS_CHG_PROP_CHARGE_COUNTER,
	OPLUS_CHG_PROP_CHARGE_FULL_DESIGN,
	OPLUS_CHG_PROP_CHARGE_FULL,
	OPLUS_CHG_PROP_TIME_TO_FULL_AVG,
	OPLUS_CHG_PROP_TIME_TO_FULL_NOW,
	OPLUS_CHG_PROP_TIME_TO_EMPTY_AVG,
	OPLUS_CHG_PROP_POWER_NOW,
	OPLUS_CHG_PROP_POWER_AVG,
	OPLUS_CHG_PROP_CAPACITY_LEVEL,
	OPLUS_CHG_PROP_SHIP_MODE,
	OPLUS_CHG_PROP_FACTORY_MODE,
	OPLUS_CHG_PROP_TX_POWER,
	OPLUS_CHG_PROP_RX_POWER,
	OPLUS_CHG_PROP_RX_MAX_POWER,
	OPLUS_CHG_PROP_VOLTAGE_NOW_CELL1,
	OPLUS_CHG_PROP_VOLTAGE_NOW_CELL2,
	OPLUS_CHG_PROP_MMI_CHARGING_ENABLE,
	OPLUS_CHG_PROP_TYPEC_CC_ORIENTATION,
	OPLUS_CHG_PROP_HW_DETECT,
	OPLUS_CHG_PROP_FOD_CAL,
	OPLUS_CHG_PROP_SKIN_TEMP,
	OPLUS_CHG_PROP_BATT_CHG_ENABLE,
	OPLUS_CHG_PROP_ONLINE_KEEP,
	OPLUS_CHG_PROP_CONNECT_DISABLE,
	OPLUS_CHG_PROP_REMAINING_CAPACITY,
	OPLUS_CHG_PROP_CALL_ON,
	OPLUS_CHG_PROP_CAMERA_ON,
	OPLUS_CHG_PROP_OTG_SWITCH,
	OPLUS_CHG_PROP_BATTERY_NOTIFY_CODE,
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	OPLUS_CHG_PROP_REG_DUMP,
#endif
#ifndef CONFIG_OPLUS_CHG_OOS
	OPLUS_CHG_PROP_AUTHENTICATE,
	OPLUS_CHG_PROP_BATTERY_CC,
	OPLUS_CHG_PROP_BATTERY_FCC,
	OPLUS_CHG_PROP_BATTERY_RM,
	OPLUS_CHG_PROP_BATTERY_SOH,
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	OPLUS_CHG_PROP_CALL_MODE,
#endif
	OPLUS_CHG_PROP_CHARGE_TECHNOLOGY,
#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
	OPLUS_CHG_PROP_CHIP_SOC,
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	OPLUS_CHG_PROP_COOL_DOWN,
#endif
	OPLUS_CHG_PROP_FAST_CHARGE,
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	OPLUS_CHG_PROP_SHORT_C_LIMIT_CHG,
	OPLUS_CHG_PROP_SHORT_C_LIMIT_RECHG,
	OPLUS_CHG_PROP_CHARGE_TERM_CURRENT,
	OPLUS_CHG_PROP_INPUT_CURRENT_SETTLED,
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	OPLUS_CHG_PROP_SHORT_C_HW_FEATURE,
	OPLUS_CHG_PROP_SHORT_C_HW_STATUS,
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	OPLUS_CHG_PROP_SHORT_IC_OTP_STATUS,
	OPLUS_CHG_PROP_SHORT_IC_VOLT_THRESH,
	OPLUS_CHG_PROP_SHORT_IC_OTP_VALUE,
#endif
	OPLUS_CHG_PROP_VOOCCHG_ING,
	OPLUS_CHG_PROP_OTG_ONLINE,
	OPLUS_CHG_PROP_USB_STATUS,
	OPLUS_CHG_PROP_FAST_CHG_TYPE,
	OPLUS_CHG_PROP_USBTEMP_VOLT_L,
	OPLUS_CHG_PROP_USBTEMP_VOLT_R,
	OPLUS_CHG_PROP_TX_VOLTAGE_NOW,
	OPLUS_CHG_PROP_TX_CURRENT_NOW,
	OPLUS_CHG_PROP_CP_VOLTAGE_NOW,
	OPLUS_CHG_PROP_CP_CURRENT_NOW,
	OPLUS_CHG_PROP_WIRELESS_MODE,
	OPLUS_CHG_PROP_WIRELESS_TYPE,
	OPLUS_CHG_PROP_CEP_INFO,
	OPLUS_CHG_PROP_REAL_TYPE,
	OPLUS_CHG_PROP_CHARGE_NOW,
#endif /* CONFIG_OPLUS_CHG_OOS */
	OPLUS_CHG_PROP_RX_VOUT_UVP,
	OPLUS_CHG_PROP_FW_UPGRADING,
	OPLUS_CHG_PROP_BCC_SUPPORT,
	OPLUS_CHG_PROP_BCC_CURRENT,
	OPLUS_CHG_PROP_WLS_BCC_MAX,
	OPLUS_CHG_PROP_WLS_BCC_MIN,
	OPLUS_CHG_PROP_WLS_BCC_STOP,
	OPLUS_CHG_PROP_MAX,
	/* extended property */
	OPLUS_CHG_EXTERN_PROP_UPGRADE_FW = OPLUS_CHG_PROP_MAX,
	OPLUS_CHG_EXTERN_PROP_CHARGE_PARAMETER,
	OPLUS_CHG_EXTERN_PROP_VOLTAGE_NOW_CELL,
	OPLUS_CHG_EXTERN_PROP_PATH_CURRENT,
	OPLUS_CHG_PROP_FTM_TEST,
};

union oplus_chg_mod_propval {
	int intval;
	const char *strval;
};

struct oplus_chg_mod;

struct oplus_chg_exten_prop {
	enum oplus_chg_mod_property exten_prop;
	ssize_t (*show)(struct device *dev,
			struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count);
};

struct oplus_chg_mod_config {
	struct device_node *of_node;
	struct fwnode_handle *fwnode;

	/* Driver private data */
	void *drv_data;

	/* Device specific sysfs attributes */
	const struct attribute_group **attr_grp;

	char **supplied_to;
	size_t num_supplicants;
};

struct oplus_chg_mod_desc {
	const char *name;
	enum oplus_chg_mod_type type;
	enum oplus_chg_mod_property *properties;
	enum oplus_chg_mod_property *uevent_properties;
	struct oplus_chg_exten_prop *exten_properties;
	size_t num_properties;
	size_t uevent_num_properties;
	size_t num_exten_properties;

	int (*get_property)(struct oplus_chg_mod *ocm,
			    enum oplus_chg_mod_property ocm_prop,
			    union oplus_chg_mod_propval *val);
	int (*set_property)(struct oplus_chg_mod *ocm,
			    enum oplus_chg_mod_property ocm_prop,
			    const union oplus_chg_mod_propval *val);
	int (*property_is_writeable)(struct oplus_chg_mod *ocm,
				     enum oplus_chg_mod_property ocm_prop);
};

struct oplus_chg_mod {
	const struct oplus_chg_mod_desc *desc;

	char **supplied_to;
	size_t num_supplicants;

	struct device_node *of_node;
	void *drv_data;

	/* private */
	struct device dev;
	struct work_struct changed_work;
	struct delayed_work deferred_register_work;
	struct atomic_notifier_head *notifier;
	spinlock_t changed_lock;
	bool changed;
	bool initialized;
	bool removing;
	atomic_t use_cnt;
	struct list_head list;
};

#define to_oplus_chg_mod(device) container_of(device, struct oplus_chg_mod, dev)

#define OPLUS_CHG_EXTEN_RWATTR(__prop, __name)	\
{						\
	.exten_prop = __prop,			\
	.show = __name##_show,			\
	.store = __name##_store,		\
}

#define OPLUS_CHG_EXTEN_ROATTR(__prop, __name)	\
{						\
	.exten_prop = __prop,			\
	.show = __name##_show,			\
	.store = NULL,				\
}

#define OPLUS_CHG_EXTEN_WOATTR(__prop, __name)	\
{						\
	.exten_prop = __prop,			\
	.show = NULL,				\
	.store = __name##_store,		\
}

extern struct atomic_notifier_head oplus_chg_event_notifier;
extern struct atomic_notifier_head oplus_chg_changed_notifier;
extern void oplus_chg_mod_changed(struct oplus_chg_mod *ocm);
extern struct oplus_chg_mod *oplus_chg_mod_get_by_name(const char *name);
extern void oplus_chg_mod_put(struct oplus_chg_mod *ocm);
extern int oplus_chg_mod_get_property(struct oplus_chg_mod *ocm,
				      enum oplus_chg_mod_property ocm_prop,
				      union oplus_chg_mod_propval *val);
extern int oplus_chg_mod_set_property(struct oplus_chg_mod *ocm,
				      enum oplus_chg_mod_property ocm_prop,
				      const union oplus_chg_mod_propval *val);
extern int
oplus_chg_mod_property_is_writeable(struct oplus_chg_mod *ocm,
				    enum oplus_chg_mod_property ocm_prop);
extern int oplus_chg_mod_powers(struct oplus_chg_mod *ocm, struct device *dev);
extern int oplus_chg_reg_changed_notifier(struct notifier_block *nb);
extern void oplus_chg_unreg_changed_notifier(struct notifier_block *nb);
extern int oplus_chg_reg_event_notifier(struct notifier_block *nb);
extern void oplus_chg_unreg_event_notifier(struct notifier_block *nb);
extern int oplus_chg_reg_mod_notifier(struct oplus_chg_mod *ocm,
				      struct notifier_block *nb);
extern void oplus_chg_unreg_mod_notifier(struct oplus_chg_mod *ocm,
					 struct notifier_block *nb);
extern void oplus_chg_global_event(struct oplus_chg_mod *owner_ocm,
				   enum oplus_chg_event events);
extern int oplus_chg_mod_event(struct oplus_chg_mod *ocm_receive,
			       struct oplus_chg_mod *ocm_send,
			       enum oplus_chg_event events);
extern int oplus_chg_anon_mod_event(struct oplus_chg_mod *ocm_receive,
				    enum oplus_chg_event events);
extern struct oplus_chg_mod *__must_check oplus_chg_mod_register(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg);
extern struct oplus_chg_mod *__must_check oplus_chg_mod_register_no_ws(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg);
extern struct oplus_chg_mod *__must_check devm_oplus_chg_mod_register(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg);
extern struct oplus_chg_mod *__must_check devm_oplus_chg_mod_register_no_ws(
	struct device *parent, const struct oplus_chg_mod_desc *desc,
	const struct oplus_chg_mod_config *cfg);
extern void oplus_chg_mod_unregister(struct oplus_chg_mod *ocm);
extern void *oplus_chg_mod_get_drvdata(struct oplus_chg_mod *ocm);

extern int ocm_to_psy_status[];
extern int ocm_to_psy_charge_type[];
extern int ocm_to_psy_health[];
extern int ocm_to_psy_technology[];
extern int ocm_to_psy_scope[];
extern int ocm_to_psy_capacity_level[];
extern int ocm_to_psy_usb_type[];

#ifdef CONFIG_SYSFS

extern void oplus_chg_mod_init_attrs(struct device_type *dev_type);
extern int oplus_chg_uevent(struct device *dev, struct kobj_uevent_env *env);

#else

static inline void oplus_chg_mod_init_attrs(struct device_type *dev_type)
{
}
#define oplus_chg_uevent NULL

#endif /* CONFIG_SYSFS */

#endif /* _OPLUS_CHG_CORE_H_ */
