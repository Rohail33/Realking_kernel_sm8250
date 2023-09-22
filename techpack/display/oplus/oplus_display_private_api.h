/***************************************************************
** Copyright (C),  2018,  OPLUS Mobile Comm Corp.,  Ltd
** OPLUS_BUG_STABILITY
** File : oplus_display_private_api.h
** Description : oplus display private api implement
** Version : 1.0
** Date : 2018/03/20
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Hu.Jie          2018/03/20        1.0           Build this moudle
******************************************************************/
#ifndef _OPLUS_DISPLAY_PRIVATE_API_H_
#define _OPLUS_DISPLAY_PRIVATE_API_H_

#include <linux/err.h>
#include <linux/list.h>
#include <linux/of.h>
#include "msm_drv.h"
#include "sde_connector.h"
#include "sde_crtc.h"
#include "sde_hw_dspp.h"
#include "sde_plane.h"
#include "msm_mmu.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <drm/drm_mipi_dsi.h>
#include "oplus_dsi_support.h"

#define  CYCLE_TIME_60HZ_U  17000

int oplus_panel_update_backlight_unlock(struct dsi_panel *panel);

int oplus_set_display_vendor(struct dsi_display *display);

int oplus_dsi_update_spr_mode(void);

int oplus_dsi_update_seed_mode(void);

void oplus_panel_process_dimming_v2_post(struct dsi_panel *panel, bool force_disable);

int oplus_panel_process_dimming_v2(struct dsi_panel *panel, int bl_lvl, bool force_disable);

int oplus_panel_process_dimming_v3(struct dsi_panel *panel, int brightness);

bool is_dsi_panel(struct drm_crtc *crtc);

int interpolate(int x, int xa, int xb, int ya, int yb, bool nosub);

int dsi_display_oplus_set_power(struct drm_connector *connector, int power_mode, void *disp);

void lcdinfo_notify(unsigned long val, void *v);

void dsi_display_gamma_read(struct dsi_display *display);

int gamma_switch(struct dsi_panel *panel);

int dsi_panel_write_gamma_90(struct dsi_panel *panel);

int oplus_dimming_gamma_write(struct dsi_panel *panel);
void oplus_dimming_gamma_read_work_init(void *dsi_display);
void oplus_dimming_gamma_schedule_read_work(void *dsi_display);

int oplus_set_aod_gamma_data_status(struct dsi_panel *panel);

bool is_support_panel_backlight_smooths(const char *panel_name);

bool is_skip_panel_ccd_check(const char *panel_name);

bool is_support_panel_seed_mode_exceed(const char *panel_name, int mode);

int is_support_panel_dc_exit_backlight_select(struct dsi_panel *panel, int frame_time_us);

bool is_skip_panel_dimming_v2_post(const char *panel_name);

bool is_skip_panel_dc_set_brightness(const char *panel_name);

bool is_support_panel_hbm_enter_send_hbm_on_cmd(const char *panel_name);
bool is_support_panel_hbm_enter_send_hbm_off_cmd(struct dsi_display *dsi_display);

bool is_support_panel_dc_seed_mode_flag(const char *panel_name);

bool oplus_panel_hbm_exit_check_wait_vblank(const char *vendor);

bool oplus_panel_support_exit_global_hbm(struct dsi_panel *panel);

bool oplus_panel_support_global_hbm_switch(struct dsi_panel *panel, u32 bl_lvl);

void oplus_dc_pcc_backlight (struct dsi_display *display, struct sde_connector *c_conn, int bl_lvl);

#endif /* _OPLUS_DISPLAY_PRIVATE_API_H_ */
