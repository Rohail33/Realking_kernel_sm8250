/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 */

#ifndef __PMIC_VOTER_H
#define __PMIC_VOTER_H

#include <linux/mutex.h>

struct votable;

enum votable_type {
	VOTE_MIN,
	VOTE_MAX,
	VOTE_SET_ANY,
	NUM_VOTABLE_TYPES,
};

#define JEITA_VOTER		"JEITA_VOTER"
#define STEP_VOTER		"STEP_VOTER"
#define USER_VOTER		"USER_VOTER"
#define DEF_VOTER		"DEF_VOTER"
#define MAX_VOTER		"MAX_VOTER"
#define BASE_MAX_VOTER		"BASE_MAX_VOTER"
#define RX_MAX_VOTER		"RX_MAX_VOTER"
#define EXIT_VOTER		"EXIT_VOTER"
#define FCC_VOTER		"FCC_VOTER"
#define CEP_VOTER		"CEP_VOTER"
#define QUIET_VOTER		"QUIET_VOTER"
#define BATT_VOL_VOTER		"BATT_VOL_VOTER"
#define BATT_CURR_VOTER		"BATT_CURR_VOTER"
#define IOUT_CURR_VOTER		"IOUT_CURR_VOTER"
#define SKIN_VOTER		"SKIN_VOTER"
#define STARTUP_CEP_VOTER	"STARTUP_CEP_VOTER"
#define HW_ERR_VOTER		"HW_ERR_VOTER"
#define CURR_ERR_VOTER		"CURR_ERR_VOTER"
#define OTG_EN_VOTER		"OTG_EN_VOTER"
#define TRX_EN_VOTER		"TRX_EN_VOTER"
#define UPGRADE_FW_VOTER	"UPGRADE_FW_VOTER"
#define DEBUG_VOTER		"DEBUG_VOTER"
#define FFC_VOTER		"FFC_VOTER"
#define USB_VOTER		"USB_VOTER"
#define UPGRADE_VOTER		"UPGRADE_VOTER"
#define CONNECT_VOTER		"CONNECT_VOTER"
#define UOVP_VOTER		"UOVP_VOTER"
#define STOP_VOTER		"STOP_VOTER"
#define FTM_TEST_VOTER		"FTM_TEST_VOTER"
#define CAMERA_VOTER		"CAMERA_VOTER"
#define CALL_VOTER		"CALL_VOTER"
#define COOL_DOWN_VOTER		"COOL_DOWN_VOTER"
#define RX_IIC_VOTER		"RX_IIC_VOTER"
#define TIMEOUT_VOTER		"TIMEOUT_VOTER"
#define CHG_DONE_VOTER		"CHG_DONE_VOTER"
#define SPEC_VOTER		"SPEC_VOTER"
#define FV_MAX_VOTER		"FV_MAX_VOTER"
#define OVER_FV_VOTER		"OVER_FV_VOTER"
#define CHG_FULL_VOTER		"CHG_FULL_VOTER"
#define WARM_FULL_VOTER		"WARM_FULL_VOTER"
#define SWITCH_RANGE_VOTER	"SWITCH_TEMP_RANGE"
#define NON_STANDARD_VOTER	"NON_STANDARD_VOTER"
#define BATT_TEMP_VOTER		"BATT_TEMP_VOTER"
#define BATT_SOC_VOTER		"BATT_SOC_VOTER"
#define WARM_SOC_VOTER		"WARM_SOC_VOTER"
#define WARM_VOL_VOTER		"WARM_VOL_VOTER"
#define FASTCHG_VOTER		"FASTCHG_VOTER"
#define FASTCHG_DUMMY_VOTER	"FASTCHG_DUMMY_VOTER"
#define MMI_CHG_VOTER		"MMI_CHG_VOTER"
#define HIDL_VOTER		"HIDL_VOTER"
#define SVID_VOTER		"SVID_VOTER"
#define FACTORY_TEST_VOTER	"FACTORY_TEST_VOTER"
#define BTB_TEMP_OVER_VOTER	"BTB_TEMP_OVER_VOTER"
#define STRATEGY_VOTER		"STRATEGY_VOTER"
#define BAD_VOLT_VOTER		"BAD_VOLT_VOTER"
#define WLAN_VOTER		"WLAN_VOTER"
#define LED_ON_VOTER		"LED_ON_VOTER"
#define BAD_CONNECTED_VOTER	"BAD_CONNECTED"
#define BCC_VOTER		"BCC_VOTER"
#define PDQC_VOTER		"PDQC_VOTER"
#define TYPEC_VOTER		"TYPEC_VOTER"
#define VOL_DIFF_VOTER		"VOL_DIFF_VOTER"
#define CHARGE_SUSPEND_VOTER	"CHARGE_SUSPEND_VOTER"
#define CHAEGE_DISABLE_VOTER	"CHAEGE_DISABLE_VOTER"
#define OTG_ONLINE_VOTER	"OTG_ONLINE_VOTER"
#define COPYCAT_ADAPTER		"COPYCAT_ADAPTER"

bool is_client_vote_enabled(struct votable *votable, const char *client_str);
bool is_client_vote_enabled_locked(struct votable *votable,
							const char *client_str);
bool is_override_vote_enabled(struct votable *votable);
bool is_override_vote_enabled_locked(struct votable *votable);
int get_client_vote(struct votable *votable, const char *client_str);
int get_client_vote_locked(struct votable *votable, const char *client_str);
int get_effective_result(struct votable *votable);
int get_effective_result_locked(struct votable *votable);
const char *get_effective_client(struct votable *votable);
const char *get_effective_client_locked(struct votable *votable);
int vote(struct votable *votable, const char *client_str, bool state, int val, bool step);
int vote_override(struct votable *votable, const char *override_client,
		  bool state, int val, bool step);
int rerun_election(struct votable *votable, bool step);
int rerun_election_unlock(struct votable *votable, bool step);
struct votable *find_votable(const char *name);
struct votable *create_votable(const char *name,
				int votable_type,
				int (*callback)(struct votable *votable,
						void *data,
						int effective_result,
						const char *effective_client,
						bool step),
				void *data);
void destroy_votable(struct votable *votable);
void lock_votable(struct votable *votable);
void unlock_votable(struct votable *votable);

#endif /* __PMIC_VOTER_H */
