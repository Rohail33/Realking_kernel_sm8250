/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DSI_PANEL_H_
#define _DSI_PANEL_H_

#include <linux/of_device.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/backlight.h>
#include <drm/drm_panel.h>
#include <drm/msm_drm.h>

#include "dsi_defs.h"
#include "dsi_ctrl_hw.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "dsi_parser.h"
#include "msm_drv.h"
#ifdef OPLUS_BUG_STABILITY
#include "oplus_dsi_support.h"
struct oplus_brightness_alpha {
	u32 brightness;
	u32 alpha;
};
#endif /*OPLUS_BUG_STABILITY*/

#define MAX_BL_LEVEL 4096
#define MAX_BL_SCALE_LEVEL 1024
#define MAX_SV_BL_SCALE_LEVEL 65535
#define DSI_CMD_PPS_SIZE 135

#define DSI_MODE_MAX 32

/*
 * Defining custom dsi msg flag,
 * continued from drm_mipi_dsi.h
 * Override to use async transfer
 */
#define MIPI_DSI_MSG_ASYNC_OVERRIDE BIT(4)

enum dsi_panel_rotation {
	DSI_PANEL_ROTATE_NONE = 0,
	DSI_PANEL_ROTATE_HV_FLIP,
	DSI_PANEL_ROTATE_H_FLIP,
	DSI_PANEL_ROTATE_V_FLIP
};

enum dsi_backlight_type {
	DSI_BACKLIGHT_PWM = 0,
	DSI_BACKLIGHT_WLED,
	DSI_BACKLIGHT_DCS,
	DSI_BACKLIGHT_EXTERNAL,
	DSI_BACKLIGHT_UNKNOWN,
	DSI_BACKLIGHT_MAX,
};

enum bl_update_flag {
	BL_UPDATE_DELAY_UNTIL_FIRST_FRAME,
	BL_UPDATE_NONE,
};

enum {
	MODE_GPIO_NOT_VALID = 0,
	MODE_SEL_DUAL_PORT,
	MODE_SEL_SINGLE_PORT,
	MODE_GPIO_HIGH,
	MODE_GPIO_LOW,
};

enum dsi_dms_mode {
	DSI_DMS_MODE_DISABLED = 0,
	DSI_DMS_MODE_RES_SWITCH_IMMEDIATE,
};

enum dsi_panel_physical_type {
	DSI_DISPLAY_PANEL_TYPE_LCD = 0,
	DSI_DISPLAY_PANEL_TYPE_OLED,
	DSI_DISPLAY_PANEL_TYPE_MAX,
};

struct dsi_dfps_capabilities {
	enum dsi_dfps_type type;
	u32 min_refresh_rate;
	u32 max_refresh_rate;
	u32 *dfps_list;
	u32 dfps_list_len;
	bool dfps_support;
};

struct dsi_qsync_capabilities {
	/* qsync disabled if qsync_min_fps = 0 */
	u32 qsync_min_fps;
	u32 *qsync_min_fps_list;
	int qsync_min_fps_list_len;
};

struct dsi_dyn_clk_caps {
	bool dyn_clk_support;
	u32 *bit_clk_list;
	u32 bit_clk_list_len;
	enum dsi_dyn_clk_feature_type type;
	bool maintain_const_fps;
};

struct dsi_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *active;
	struct pinctrl_state *suspend;
};

struct dsi_panel_phy_props {
	u32 panel_width_mm;
	u32 panel_height_mm;
	enum dsi_panel_rotation rotation;
};

struct dsi_backlight_config {
	enum dsi_backlight_type type;
	enum bl_update_flag bl_update;

	u32 bl_min_level;
	u32 bl_max_level;
	u32 brightness_max_level;
#ifdef OPLUS_BUG_STABILITY
	u32 bl_normal_max_level;
	u32 brightness_normal_max_level;
	u32 brightness_default_level;
#endif /* OPLUS_BUG_STABILITY */

	u32 bl_level;
#ifdef OPLUS_BUG_STABILITY
	u32 oplus_raw_bl;
#endif /* OPLUS_BUG_STABILITY */

	u32 bl_scale;
	u32 bl_scale_sv;
	bool bl_inverted_dbv;
#ifdef OPLUS_BUG_STABILITY
	u32 bl_lvl_backup;
	u32 bl_dc_real;
#endif /* OPLUS_BUG_STABILITY */
	u32 bl_dcs_subtype;

	int en_gpio;
	/* PWM params */
	struct pwm_device *pwm_bl;
	bool pwm_enabled;
	u32 pwm_period_usecs;

	/* WLED params */
	struct led_trigger *wled;
	struct backlight_device *raw_bd;
};

struct dsi_reset_seq {
	u32 level;
	u32 sleep_ms;
};

struct dsi_panel_reset_config {
	struct dsi_reset_seq *sequence;
	u32 count;

	int reset_gpio;
	int disp_en_gpio;
	int lcd_mode_sel_gpio;
	u32 mode_sel_state;
#ifdef OPLUS_BUG_STABILITY
	int tp_cs_gpio;
	int panel_vout_gpio;
	int panel_te_esd_gpio;
	int panel_vddr_aod_en_gpio;
#endif
};

enum esd_check_status_mode {
	ESD_MODE_REG_READ,
	ESD_MODE_SW_BTA,
	ESD_MODE_PANEL_TE,
	ESD_MODE_SW_SIM_SUCCESS,
	ESD_MODE_SW_SIM_FAILURE,
#ifdef OPLUS_BUG_STABILITY
	/* A tablet Pad, modify esd */
	ESD_MODE_PANEL_ERROR_FLAG,
#endif /* OPLUS_BUG_STABILITY */
	ESD_MODE_MAX
};

struct drm_panel_esd_config {
	bool esd_enabled;

	enum esd_check_status_mode status_mode;
	struct dsi_panel_cmd_set status_cmd;
	u32 *status_cmds_rlen;
	u32 *status_valid_params;
	u32 *status_value;
	u8 *return_buf;
	u8 *status_buf;
	u32 groups;
#ifdef OPLUS_BUG_STABILITY
	/* A tablet Pad, modify esd */
	int esd_error_flag_gpio;
	int esd_error_flag_gpio_slave;
#endif /* OPLUS_BUG_STABILITY */
};

#ifdef OPLUS_BUG_STABILITY
struct dsi_panel_oplus_privite {
	const char *vendor_name;
	const char *manufacture_name;
	bool skip_mipi_last_cmd;
	struct oplus_brightness_alpha *bl_remap;
	int bl_remap_count;
	bool is_pxlw_iris5;
	bool bl_interpolate_nosub;
	bool bl_interpolate_remap_nosub;
	bool bl_interpolate_alpha_dc_nosub;
	bool is_oplus_project;
	bool is_raw_backlight;
	bool dfps_idle_off;
	bool aod_on_fod_off;
	bool esd_err_flag_enabled;
#ifdef OPLUS_FEATURE_AOD_RAMLESS
	bool is_aod_ramless;
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
#ifdef CONFIG_REGULATOR_TPS65132
	bool is_tps65132_support;
#endif /* CONFIG_REGULATOR_TPS65132 */
	bool is_osc_support;
	u32 osc_clk_mode0_rate;
	u32 osc_clk_mode1_rate;
	u32 osc_clk_rate_lastest;
	bool is_osc_rewrite_support;
	u32 osc_rewrite_clk_rate;
	bool is_90fps_switch;
	bool is_dc_seed_support;
	bool gamma_switch_enable;
	bool lcd_cabc_support;
	bool low_light_adjust_gamma_support;
	bool low_light_gamma_is_adjusted;
	u32 low_light_adjust_gamma_level;
	bool oplus_fp_hbm_config_flag;
/********************************************
	fp_type usage:
	bit(0):lcd capacitive fingerprint(aod/fod are not supported)
	bit(1):oled capacitive fingerprint(only support aod)
	bit(2):optical fingerprint old solution(dim layer and pressed icon are controlled by kernel)
	bit(3):optical fingerprint new solution(dim layer and pressed icon are not controlled by kernel)
	bit(4):local hbm
	bit(5):pressed icon brightness adaptive
	bit(6):ultrasonic fingerprint
	bit(7):ultra low power aod
********************************************/
	u32 fp_type;
	// Add for apollo support
	bool is_apollo_support;
	u32 sync_brightness_level;
	bool dc_apollo_sync_enable;
	u32 dc_apollo_sync_brightness_level;
	u32 dc_apollo_sync_brightness_level_pcc;
	u32 dc_apollo_sync_brightness_level_pcc_min;
};
#endif /* OPLUS_BUG_STABILITY */

struct dsi_panel {
	const char *name;
	const char *type;
	struct device_node *panel_of_node;
	struct mipi_dsi_device mipi_device;

	struct mutex panel_lock;
	struct drm_panel drm_panel;
	struct mipi_dsi_host *host;
	struct device *parent;

	struct dsi_host_common_cfg host_config;
	struct dsi_video_engine_cfg video_config;
	struct dsi_cmd_engine_cfg cmd_config;
	enum dsi_op_mode panel_mode;
	bool panel_mode_switch_enabled;

	struct dsi_dfps_capabilities dfps_caps;
	struct dsi_dyn_clk_caps dyn_clk_caps;
	struct dsi_panel_phy_props phy_props;

	struct dsi_display_mode *cur_mode;
	u32 num_timing_nodes;
	u32 num_display_modes;

	struct dsi_regulator_info power_info;
	struct dsi_backlight_config bl_config;
	struct dsi_panel_reset_config reset_config;
	struct dsi_pinctrl_info pinctrl;
	struct drm_panel_hdr_properties hdr_props;
	struct drm_panel_esd_config esd_config;

	struct dsi_parser_utils utils;

	bool lp11_init;
	bool ulps_feature_enabled;
	bool ulps_suspend_enabled;
	bool allow_phy_power_off;
	bool reset_gpio_always_on;
	atomic_t esd_recovery_pending;

	bool panel_initialized;
	bool te_using_watchdog_timer;
	struct dsi_qsync_capabilities qsync_caps;

	char dsc_pps_cmd[DSI_CMD_PPS_SIZE];
	enum dsi_dms_mode dms_mode;

	bool sync_broadcast_en;
#ifdef OPLUS_BUG_STABILITY
	bool is_hbm_enabled;
	/* Fix aod flash problem */
	bool need_power_on_backlight;
	struct oplus_brightness_alpha *ba_seq;
	int ba_count;
	struct oplus_brightness_alpha *dc_ba_seq;
	int dc_ba_count;

	struct dsi_panel_oplus_privite oplus_priv;
	int panel_id2;
	atomic_t esd_pending;
	bool is_dc_set_color_mode;
	bool nt36523w_ktz8866;
	/* A tablet Pad, add for FPC cause splash screen issue */
	bool nt36523w_old_fpc;
#endif
	int vddr_gpio;
	int panel_test_gpio;
#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	bool is_secondary;
#endif
	int power_mode;
	enum dsi_panel_physical_type panel_type;
#ifdef OPLUS_FEATURE_ADFR
	int vsync_switch_gpio;
	int vsync_switch_gpio_level;
	bool force_te_vsync;
	bool need_vsync_switch;
	u32 cur_h_active;
#endif /* OPLUS_FEATURE_ADFR */
};

static inline bool dsi_panel_ulps_feature_enabled(struct dsi_panel *panel)
{
	return panel->ulps_feature_enabled;
}

static inline bool dsi_panel_initialized(struct dsi_panel *panel)
{
	return panel->panel_initialized;
}

static inline void dsi_panel_acquire_panel_lock(struct dsi_panel *panel)
{
	mutex_lock(&panel->panel_lock);
}

static inline void dsi_panel_release_panel_lock(struct dsi_panel *panel)
{
	mutex_unlock(&panel->panel_lock);
}

static inline bool dsi_panel_is_type_oled(struct dsi_panel *panel)
{
	return (panel->panel_type == DSI_DISPLAY_PANEL_TYPE_OLED);
}

struct dsi_panel *dsi_panel_get(struct device *parent,
				struct device_node *of_node,
				struct device_node *parser_node,
				const char *type,
				int topology_override);

int dsi_panel_trigger_esd_attack(struct dsi_panel *panel);

void dsi_panel_put(struct dsi_panel *panel);

int dsi_panel_drv_init(struct dsi_panel *panel, struct mipi_dsi_host *host);

int dsi_panel_drv_deinit(struct dsi_panel *panel);

int dsi_panel_get_mode_count(struct dsi_panel *panel);

void dsi_panel_put_mode(struct dsi_display_mode *mode);

int dsi_panel_get_mode(struct dsi_panel *panel,
		       u32 index,
		       struct dsi_display_mode *mode,
		       int topology_override);

int dsi_panel_validate_mode(struct dsi_panel *panel,
			    struct dsi_display_mode *mode);

int dsi_panel_get_host_cfg_for_mode(struct dsi_panel *panel,
				    struct dsi_display_mode *mode,
				    struct dsi_host_config *config);

int dsi_panel_get_phy_props(struct dsi_panel *panel,
			    struct dsi_panel_phy_props *phy_props);
int dsi_panel_get_dfps_caps(struct dsi_panel *panel,
			    struct dsi_dfps_capabilities *dfps_caps);

int dsi_panel_pre_prepare(struct dsi_panel *panel);

int dsi_panel_set_lp1(struct dsi_panel *panel);

int dsi_panel_set_lp2(struct dsi_panel *panel);

int dsi_panel_set_nolp(struct dsi_panel *panel);

int dsi_panel_prepare(struct dsi_panel *panel);

int dsi_panel_enable(struct dsi_panel *panel);

int dsi_panel_post_enable(struct dsi_panel *panel);

int dsi_panel_pre_disable(struct dsi_panel *panel);

int dsi_panel_disable(struct dsi_panel *panel);

int dsi_panel_unprepare(struct dsi_panel *panel);

int dsi_panel_post_unprepare(struct dsi_panel *panel);

int dsi_panel_set_backlight(struct dsi_panel *panel, u32 bl_lvl);

int dsi_panel_update_pps(struct dsi_panel *panel);

int dsi_panel_send_qsync_on_dcs(struct dsi_panel *panel,
		int ctrl_idx);
int dsi_panel_send_qsync_off_dcs(struct dsi_panel *panel,
		int ctrl_idx);

int dsi_panel_send_roi_dcs(struct dsi_panel *panel, int ctrl_idx,
		struct dsi_rect *roi);

int dsi_panel_pre_mode_switch_to_video(struct dsi_panel *panel);
int dsi_panel_pre_mode_switch_to_cmd(struct dsi_panel *panel);
int dsi_panel_mode_switch_to_cmd(struct dsi_panel *panel);
int dsi_panel_mode_switch_to_vid(struct dsi_panel *panel);

int dsi_panel_switch(struct dsi_panel *panel);

int dsi_panel_post_switch(struct dsi_panel *panel);

void dsi_dsc_pclk_param_calc(struct msm_display_dsc_info *dsc, int intf_width);

void dsi_panel_bl_handoff(struct dsi_panel *panel);

struct dsi_panel *dsi_panel_ext_bridge_get(struct device *parent,
				struct device_node *of_node,
				int topology_override);

int dsi_panel_parse_esd_reg_read_configs(struct dsi_panel *panel);

void dsi_panel_ext_bridge_put(struct dsi_panel *panel);

void dsi_panel_calc_dsi_transfer_time(struct dsi_host_common_cfg *config,
		struct dsi_display_mode *mode, u32 frame_threshold_us);

#ifdef OPLUS_BUG_STABILITY
int dsi_panel_tx_cmd_set(struct dsi_panel *panel,
			   enum dsi_cmd_set_type type);
int dsi_panel_fps60_cmd_set(struct dsi_panel *panel);
int dsi_panel_fps120_cmd_set(struct dsi_panel *panel);
#endif
#endif /* _DSI_PANEL_H_ */
