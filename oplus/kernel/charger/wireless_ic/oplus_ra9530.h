/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __OPLUS_RA9530_H__
#define __OPLUS_RA9530_H__

#include "../oplus_wireless.h"

#define ERR_NUM                         (-1)
#define ERR_SRAM                        (-2)
#define ERR_EEPROM                      (-3)
#define S_MAGIC_WORDS                   0x55

#define HEX_0                           0x00
#define HEX_1                           0x01
#define HEX_2                           0x02
#define HEX_3                           0x03
#define HEX_4                           0x04
#define HEX_5                           0x05
#define HEX_6                           0x06
#define HEX_7                           0x07
#define HEX_8                           0x08
#define HEX_9                           0x09
#define HEX_10                          0x10
#define HEX_54                          0x54
#define HEX_5A                          0x5a
#define HEX_80                          0x80
#define HEX_F0                          0xF0
#define HEX_3000                        0x3000
#define HEX_3020                        0x3020
#define NUM_0                           0
#define NUM_1                           1
#define NUM_2                           2
#define NUM_3                           3
#define NUM_4                           4
#define NUM_5                           5
#define NUM_6                           6
#define NUM_8                           8
#define NUM_10                          10
#define MS_5                            5
#define MS_20                           20
#define MS_30                           30
#define MS_100                          100
#define MS_150                          150
#define MS_500                          500
#define MS_1000                         1000
#define MS_3000                         3000
#define MS_5000                         5000
#define REG_1000                        0x1000
#define REG_1008                        0x1008
#define REG_1009                        0x1009
#define REG_3040                        0x3040
#define REG_3048                        0x3048
#define REG_5C50                        0x5C50
#define REG_5C2C                        0x5C2C
#define REG_17                          0x0017
#define REG_1A                          0x001A
#define REG_1C                          0x001C
#define DATA_31                         0x31


#define RA9530_FW_VERSION_OFFSET        9
#define RA9530_PURESIZE_OFFSET          128
#define RA9530_FW_PAGE_SIZE             144
#define RA9530_FW_CODE_LENGTH           128
#define RA9530_CHECK_SS_INT_TIME        50 /*msec*/
#define RA9530_GET_BLE_ADDR_TIMEOUT     10 /*10 * 500 ms = 5 sec*/

#define RA9530_OCP_THRESHOLD            500
#define RA9530_OVP_THRESHOLD            12000
#define RA9530_LVP_THRESHOLD            4000
#define RA9530_PCOP_THRESHOLD1          700
#define RA9530_PCOP_THRESHOLD2          500
#define RA9530_FOD_THRESHOLD2           400
#define RA9530_SOC_THRESHOLD2           90

#define RA9530_REG_INT_CLR								0x28
#define RA9530_REG_INT_EN								0x34
#define RA9530_REG_INT_FLAG								0x30
#define RA9530_REG_CHARGE_PERCENT						0x3A
#define RA9530_REG_UVLO_THD								0xEC

#define RA9530_INT_FLAG_EPT								BIT(0)
#define RA9530_INT_FLAG_STR_DPING						BIT(1)
#define RA9530_INT_FLAG_SS								BIT(2)
#define RA9530_INT_FLAG_ID								BIT(3)
#define RA9530_INT_FLAG_CFG								BIT(4)
#define RA9530_INT_FLAG_DPING							BIT(6)
#define RA9530_INT_FLAG_TX_INT							BIT(7)

#define RA9530_INT_FLAG_BLE_ADDR						BIT(0)
#define RA9530_INT_FLAG_PPP								BIT(5)
#define RA9530_INT_FLAG_STOP_CHARGING					BIT(7)

#define RA9530_INT_FLAG_PRIVATE_PKG						BIT(1)

#define RA9530_REG_RTX_CMD								0x7C
#define RA9530_REG_RTX_STATUS							0x7E
#define RA9530_RTX_DIGITALPING							BIT(0)
#define RA9530_RTX_READY								BIT(1)
#define RA9530_RTX_TRANSFER								BIT(3)

#define RA9530_REG_RTX_ERR_STATUS						0x7a
#define RA9530_RTX_ERR_TX_EPT_CMD						BIT(0)
#define RA9530_RTX_ERR_TX_IDAU_FAIL						BIT(3)

#define RA9530_RTX_ERR_TX_CEP_TIMEOUT					BIT(8)
#define RA9530_RTX_ERR_TX_OCP							BIT(10)
#define RA9530_RTX_ERR_TX_OVP							BIT(11)
#define RA9530_RTX_ERR_TX_LVP							BIT(12)
#define RA9530_RTX_ERR_TX_FOD							BIT(13)
#define RA9530_RTX_ERR_TX_OTP							BIT(14)
#define RA9530_RTX_ERR_TX_POCP							BIT(15)

#define RA9530_BLE_MAC_ADDR0							0x1b4
#define RA9530_BLE_MAC_ADDR1							0x1b5
#define RA9530_BLE_MAC_ADDR2							0x1b6
#define RA9530_BLE_MAC_ADDR3							0x1b7
#define RA9530_BLE_MAC_ADDR4							0x1b8
#define RA9530_BLE_MAC_ADDR5							0x1b9
#define RA9530_PRIVATE_DATA_REG							0x19d

#define P9418_REG_OCP_THRESHOLD							0x6A
#define P9418_REG_OVP_THRESHOLD							0x6C
#define P9418_REG_LVP_THRESHOLD							0x60
#define P9418_REG_POCP_THRESHOLD1						0x0A
#define P9418_REG_POCP_THRESHOLD2						0x0C
#define P9418_REG_FOD_THRESHOLD							0x92
#define RA9530_REG_PING									0x2C
#define RA9530_PING_SUCC								BIT(4)

#define RA9530_REG_CURRENT_MA	 						0x172 /* ma 16bit */
#define RA9530_REG_AVR_CURRENT_MA	 					0x1C6 /* ma 16bit */
#define RA9530_REG_VOLTAGE_MV							0x80 /* mv 16bit */
#define RA9530_REG_OUTPUT_VOLTAGE_MV					0x82 /* mv 16bit */

#define RA9530_REG_IDENTITY									0x5870

#define RA9530_UPDATE_INTERVAL							round_jiffies_relative(msecs_to_jiffies(3000))
#define RA9530_UPDATE_RETRY_INTERVAL					round_jiffies_relative(msecs_to_jiffies(3000))

#define MIN_TO_MS		(60000)

enum PEN_STATUS {
	PEN_STATUS_UNDEFINED,
	PEN_STATUS_NEAR,
	PEN_STATUS_FAR,
};

enum POWER_ENABLE_REASON {
	PEN_REASON_UNDEFINED,
	PEN_REASON_NEAR,
	PEN_REASON_RECHARGE,
};

enum POWER_DISABLE_REASON {
	PEN_REASON_UNKNOWN,
	PEN_REASON_FAR,
	PEN_REASON_CHARGE_FULL,
	PEN_REASON_CHARGE_TIMEOUT,
	PEN_REASON_CHARGE_STOP,
	PEN_REASON_CHARGE_EPT,
	PEN_REASON_CHARGE_OCP,
	PEN_OFF_REASON_MAX = 10,
};


/*extern struct oplus_chg_chip *ra9530_chip;*/
struct oplus_ra9530_ic{
	struct i2c_client				*client;
	struct device					*dev;
	struct device					*wireless_dev;

	struct power_supply				*wireless_psy;
	enum power_supply_type			wireless_type;
	enum wireless_mode				wireless_mode;
	bool							present;
	bool							i2c_ready;
	bool							is_power_on;
	uint8_t							pen_status;
	uint8_t							power_enable_reason;
	uint32_t						power_enable_times;
	uint8_t							power_disable_reason;
	uint8_t							power_disable_times[PEN_OFF_REASON_MAX];

	bool							unhealth_memory_handle_support;
	uint16_t						boot_check_status;
	unsigned char					*match_fw_buf;
	int								match_fw_size;

	/* RA9530 threshold parameter */
	int								soc_threshould;
	int								ocp_threshold;
	int								ovp_threshold;
	int								lvp_threshold;
	int								pcop_threshold1;
	int								pcop_threshold2;
	int								fod_threshold;
	int								tx_current;
	int								tx_avr_current;
	int								tx_voltage;
	int								tx_output_voltage;
	int								rx_soc;
	int								ping_succ_time;
	/* RA9530 status parameter */
	char							int_flag_data[4];
	unsigned int					idt_fw_version;
	int								cep_count_flag;
	unsigned long int				ble_mac_addr;
	unsigned long int				private_pkg_data;

	int								idt_int_gpio;		/*for WPC*/
	int								idt_int_irq;		/*for WPC*/
	int								vbat_en_gpio;		/*for WPC*/
	int								booster_en_gpio;	/*for WPC*/
	int								wrx_en_gpio;		/*for WPC*/

	struct pinctrl					*pinctrl;
	struct pinctrl_state 			*idt_int_active;	/*for WPC*/
	struct pinctrl_state 			*idt_int_sleep;		/*for WPC*/
	struct pinctrl_state 			*idt_int_default;	/*for WPC*/
	struct pinctrl_state 			*vbat_en_active;	/*for WPC*/
	struct pinctrl_state 			*vbat_en_sleep;		/*for WPC*/
	struct pinctrl_state 			*vbat_en_default;	/*for WPC*/
	struct pinctrl_state 			*booster_en_active;	/*for WPC*/
	struct pinctrl_state 			*booster_en_sleep;	/*for WPC*/
	struct pinctrl_state 			*booster_en_default;	/*for WPC*/

	struct delayed_work				ra9530_update_work;  /*for WPC*/
	struct delayed_work				power_check_work;
	struct work_struct				power_enable_work;
	struct work_struct				power_switch_work;
	struct mutex					flow_mutex;
	struct wakeup_source			*bus_wakelock;
	u64								tx_start_time;
	u64								upto_ble_time;          /* enable tx to get mac time(ms) */
	u64								charger_done_time;      /* enable tx to charger done time(min) */
	uint32_t						power_expired_time;     /* charger done expired time(min) */
	struct delayed_work				check_point_dwork;
};

void ra9530_wpc_print_log(void);
void ra9530_set_vbat_en_val(struct oplus_ra9530_ic *chip, int value);
int ra9530_get_vbat_en_val(void);
void ra9530_set_booster_en_val(struct oplus_ra9530_ic *chip, int value);
int ra9530_get_booster_en_val(void);
bool ra9530_firmware_is_updating(void);
bool ra9530_check_chip_is_null(void);
void ra9530_ept_type_detect_func(struct oplus_ra9530_ic *chip);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
int ra9530_driver_init(void);
void ra9530_driver_exit(void);
#endif
#endif	/*__OPLUS_RA9530_H__*/

