/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** OPLUS_BUG_STABILITY
** File : oplus_aod.h
** Description : oplus aod feature
** Version : 1.0
** Date : 2020/04/23
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/23        1.0           Build this moudle
******************************************************************/
#ifndef _OPLUS_AOD_H_
#define _OPLUS_AOD_H_

#define RAMLESS_AOD_AREA_NUM		6

#include "dsi_display.h"

struct aod_area {
	bool enable;
	int x;
	int y;
	int w;
	int h;
	int color;
	int bitdepth;
	int mono;
	int gray;
};

/*
	panel_aod_area and panel_aod_area_para,
	need to be consistent with hild ioctrl
*/
struct panel_aod_area {
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
	uint32_t color;
	uint32_t bitdepth;
	uint32_t mono;
	uint32_t gray;
};

struct panel_aod_area_para {
	struct panel_aod_area aod_area[RAMLESS_AOD_AREA_NUM];
	uint32_t size;
};

#ifdef OPLUS_FEATURE_AOD_RAMLESS
extern int oplus_display_mode;
extern wait_queue_head_t oplus_aod_wait;
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

int dsi_display_aod_on(struct dsi_display *display);

int dsi_display_aod_off(struct dsi_display *display);

int oplus_update_aod_light_mode_unlock(struct dsi_panel *panel);

int oplus_update_aod_light_mode(void);

int oplus_panel_set_aod_light_mode(void *buf);
int oplus_panel_get_aod_light_mode(void *buf);
int __oplus_display_set_aod_light_mode(int mode);
#ifdef OPLUS_FEATURE_AOD_RAMLESS
bool is_oplus_ramless_aod(void);
int oplus_ramless_panel_update_aod_area_unlock(void);
int oplus_ramless_panel_display_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state);
int oplus_ramless_panel_get_aod_area(void *buf);
int oplus_ramless_panel_set_aod_area(void *buf);
int oplus_ramless_panel_get_video(void *buf);
int oplus_ramless_panel_set_video(void *buf);
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
#endif
