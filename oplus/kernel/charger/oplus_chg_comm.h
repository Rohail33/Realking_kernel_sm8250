// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_COMM_H__
#define __OPLUS_CHG_COMM_H__

#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include "oplus_chg_core.h"
#endif

#define CHG_TIMEOUT_COUNT		6000 /* 10hr */
#define BATT_SOFT_OVP_MV		4500
#define FFC_CHG_STEP_MAX		4
#define BATT_NON_TEMP			(-400)
#define BATT_TEMP_HYST			20
#define HEARTBEAT_INTERVAL_MS		5000
#define CHG_CMD_DATA_LEN		256
#define CHG_CMD_TIME_MS			3000

enum oplus_chg_temp_region {
	BATT_TEMP_UNKNOWN = 0,	/*< -10*/
	BATT_TEMP_COLD,		/*-10-0*/
	BATT_TEMP_LITTLE_COLD,	/*0-5*/
	BATT_TEMP_COOL,		/*5-12*/
	BATT_TEMP_LITTLE_COOL,	/*12-16*/
	BATT_TEMP_NORMAL,	/*16-40*/
	BATT_TEMP_LITTLE_WARM,	/*40-44*/
	BATT_TEMP_WARM,		/*44-53*/
	BATT_TEMP_HOT,		/*>53*/
	BATT_TEMP_INVALID,
};

enum oplus_chg_ffc_status {
	FFC_DEFAULT = 0,
	FFC_FAST,
	FFC_IDLE,
};

enum oplus_chg_ffc_temp_region {
	FFC_TEMP_COOL,
	FFC_TEMP_PRE_NORMAL,
	FFC_TEMP_NORMAL,
	FFC_TEMP_WARM,
	FFC_TEMP_INVALID,
};

enum oplus_chg_batt_status {
	BATT_STATUS_GOOD,
	BATT_STATUS_BAD_TEMP, /* cold or hot */
	BATT_STATUS_BAD,
	BATT_STATUS_REMOVED, /* on v2.2 only */
	BATT_STATUS_INVALID_v1 = BATT_STATUS_REMOVED,
	BATT_STATUS_INVALID
};

enum oplus_chg_charge_mode {
	CHG_MODE_AC_5V,
	CHG_MODE_QC_HV,
	CHG_MODE_QC_LV,
	CHG_MODE_VOOC_HV,
	CHG_MODE_VOOC_LV,
	CHG_MODE_PD_HV,
	CHG_MODE_PD_LV,
	CHG_MODE_WLS_BPP,
	CHG_MODE_WLS_EPP,
	CHG_MODE_WLS_EPP_PLUS,
	CHG_MODE_MAX,
};

struct oplus_chg_cmd {
	unsigned int cmd;
	unsigned int data_size;
	unsigned char data_buf[CHG_CMD_DATA_LEN];
};

enum oplus_chg_cmd_type {
	CMD_WLS_THIRD_PART_AUTH,
	CMD_UPDATE_UI_SOH,
	CMD_INIT_UI_SOH,
};

enum oplus_chg_cmd_error {
	CMD_ACK_OK,
	CMD_ERROR_CHIP_NULL,
	CMD_ERROR_DATA_NULL,
	CMD_ERROR_DATA_INVALID,
	CMD_ERROR_HIDL_NOT_READY,
	CMD_ERROR_TIME_OUT,
};

struct oplus_chg_comm_config {
	uint8_t check_batt_full_by_sw;

	int32_t fv_offset_voltage_mv;
	int32_t little_cold_iterm_ma;
	int32_t sw_iterm_ma;
	int32_t full_count_sw_num;
	int32_t batt_uv_mv;
	int32_t batt_ov_mv;
	int32_t batt_oc_ma;
	int32_t batt_ovd_mv; /* Double cell pressure difference is too large */
	int32_t batt_temp_thr[BATT_TEMP_INVALID - 1];
	int32_t vbatmax_mv[BATT_TEMP_INVALID];
	int32_t ffc_temp_thr[FFC_TEMP_INVALID - 1];
	int32_t wls_vbatdet_mv[BATT_TEMP_INVALID];
	int32_t wls_ffc_step_max;
	int32_t wls_ffc_fv_mv[FFC_CHG_STEP_MAX];
	int32_t wls_ffc_fv_cutoff_mv[FFC_CHG_STEP_MAX];
	int32_t wls_ffc_icl_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int32_t wls_ffc_fcc_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int32_t wls_ffc_fcc_cutoff_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int32_t usb_ffc_step_max;
	int32_t usb_ffc_fv_mv[FFC_CHG_STEP_MAX];
	int32_t usb_ffc_fv_cutoff_mv[FFC_CHG_STEP_MAX];
	int32_t usb_ffc_fcc_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int32_t usb_ffc_fcc_cutoff_ma[FFC_CHG_STEP_MAX][FFC_TEMP_INVALID - 2];
	int32_t bpp_vchg_min_mv;
	int32_t bpp_vchg_max_mv;
	int32_t epp_vchg_min_mv;
	int32_t epp_vchg_max_mv;
	int32_t fast_vchg_min_mv;
	int32_t fast_vchg_max_mv;
	int32_t batt_curr_limit_thr_mv;
} __attribute__((packed));

void oplus_chg_comm_status_init(struct oplus_chg_mod *comm_ocm);
enum oplus_chg_temp_region oplus_chg_comm_get_temp_region(struct oplus_chg_mod *comm_ocm);
enum oplus_chg_ffc_temp_region oplus_chg_comm_get_ffc_temp_region(struct oplus_chg_mod *comm_ocm);
int oplus_chg_comm_switch_ffc(struct oplus_chg_mod *comm_ocm);
int oplus_chg_comm_check_ffc(struct oplus_chg_mod *comm_ocm);
bool oplus_chg_comm_check_batt_full_by_sw(struct oplus_chg_mod *comm_ocm);
void oplus_chg_comm_check_term_current(struct oplus_chg_mod *comm_ocm);
enum oplus_chg_ffc_status oplus_chg_comm_get_ffc_status(struct oplus_chg_mod *comm_ocm);
bool oplus_chg_comm_get_chg_done(struct oplus_chg_mod *comm_ocm);
void oplus_chg_comm_update_config(struct oplus_chg_mod *comm_ocm);
bool oplus_chg_comm_batt_vol_over_cl_thr(struct oplus_chg_mod *comm_ocm);
int oplus_chg_comm_get_batt_health(struct oplus_chg_mod *comm_ocm);
int oplus_chg_comm_get_batt_status(struct oplus_chg_mod *comm_ocm);

int oplus_chg_comm_reg_mutual_notifier(struct notifier_block *nb);
int oplus_chg_comm_unreg_mutual_notifier(struct notifier_block *nb);
ssize_t oplus_chg_comm_send_mutual_cmd(struct oplus_chg_mod *comm_ocm, char *buf);
ssize_t oplus_chg_comm_response_mutual_cmd(struct oplus_chg_mod *comm_ocm, const char *buf, size_t count);
int oplus_chg_common_set_mutual_cmd(struct oplus_chg_mod *comm_ocm, u32 cmd, u32 data_size, const void *data_buf);
#endif
