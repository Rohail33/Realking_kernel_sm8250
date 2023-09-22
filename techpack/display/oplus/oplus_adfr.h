/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** OPLUS_BUG_STABILITY
** File : oplus_adfr.h
** Description : ADFR kernel module
** Version : 1.0
** Date : 2020/10/23
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  CaiHuiyue      2020/10/23        1.0         Build this moudle
******************************************************************/

#ifndef _OPLUS_ADFR_H_
#define _OPLUS_ADFR_H_

/* please just only include linux common head file to keep me pure */
#include <linux/device.h>
#include <linux/hrtimer.h>

enum oplus_vsync_mode {
	OPLUS_DOUBLE_TE_VSYNC = 0,
	OPLUS_EXTERNAL_TE_TP_VSYNC = 8,
	OPLUS_INVALID_VSYNC,
};

enum oplus_te_source {
	OPLUS_TE_SOURCE_TE = 0, /* TE0 */
	OPLUS_TE_SOURCE_TP = 1, /* TE1 */
};

enum oplus_vsync_switch {
	OPLUS_VSYNC_SWITCH_TP = 0,	/* TP VSYNC */
	OPLUS_VSYNC_SWITCH_TE = 1,	/* TE VSYNC */
};

enum h_skew_type {
	SDC_ADFR = 0,				/* SA */
	SDC_MFR = 1,				/* SM */
	OPLUS_ADFR = 2,				/* OA */
	OPLUS_MFR = 3,				/* OM */
};

enum oplus_adfr_auto_mode_value {
	OPLUS_ADFR_AUTO_OFF = 0,
	OPLUS_ADFR_AUTO_ON = 1,
};

enum oplus_adfr_auto_fakeframe_value {
	OPLUS_ADFR_FAKEFRAME_OFF = 0,
	OPLUS_ADFR_FAKEFRAME_ON = 1,
};

enum oplus_adfr_auto_min_fps_value {
	OPLUS_ADFR_AUTO_MIN_FPS_MAX = 0,
	OPLUS_ADFR_AUTO_MIN_FPS_60HZ = 1,
};

enum deferred_window_status {
	DEFERRED_WINDOW_END = 0,			/* deferred min fps window end */
	DEFERRED_WINDOW_START = 1,			/* deferred min fps window start */
	DEFERRED_WINDOW_NEXT_FRAME = 2,		/* set the min fps window in next frame */
	SET_WINDOW_IMMEDIATELY = 3,			/* set the min fps window immediately */
};

struct oplus_te_refcount {
	bool te_calculate_enable; /*enable by hidl interface */
	u64 te_refcount;          /* count for te */
	ktime_t start_timeline;   /* the irq enable timeline */
	ktime_t end_timeline;     /* the irq disable timeline */
};

extern bool oplus_adfr_compatibility_mode;

/* --------------- adfr misc ---------------*/
void oplus_adfr_init(void *dsi_panel);
inline bool oplus_adfr_is_support(void);
ssize_t oplus_adfr_get_debug(struct device *dev,
	struct device_attribute *attr, char *buf);
ssize_t oplus_adfr_set_debug(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
ssize_t oplus_set_vsync_switch(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
ssize_t oplus_get_vsync_switch(struct device *dev,
		struct device_attribute *attr, char *buf);
int oplus_enable_te_refcount(void *data);
int oplus_get_te_fps(void *data);

/* --------------- msm_drv ---------------*/
int oplus_adfr_thread_create(void *msm_param,
	void *msm_priv, void *msm_ddev, void *msm_dev);
void oplus_adfr_thread_destroy(void *msm_priv);

/* ------------ sde_connector ------------ */
/* for qsync minfps */
int oplus_adfr_handle_qsync_mode_minfps(u32 propval);
bool oplus_adfr_qsync_mode_minfps_is_updated(void);
u32 oplus_adfr_get_qsync_mode_minfps(void);

/* --------------- sde_crtc ---------------*/
void sde_crtc_adfr_handle_frame_event(void *crtc, void* event);

/* --------------- sde_encoder ---------------*/
/* if frame start commit, cancel the second fake frame */
int sde_encoder_adfr_cancel_fakeframe(void *enc);
/* timer handler for second fake frame */
enum hrtimer_restart sde_encoder_fakeframe_timer_handler(struct hrtimer *timer);
/* the fake frame cmd send function */
void sde_encoder_fakeframe_work_handler(struct kthread_work *work);
void oplus_adfr_fakeframe_timer_start(void *enc, int deferred_ms);
int sde_encoder_adfr_trigger_fakeframe(void *enc);
/* trigger first fake frame */
void sde_encoder_adfr_prepare_commit(void *crtc, void *enc, void *conn);
/* trigger second fake frame */
void sde_encoder_adfr_kickoff(void *crtc, void *enc, void *conn);

/* --------------- sde_encoder_phys_cmd ---------------*/
void oplus_adfr_force_qsync_mode_off(void *drm_connector);
int oplus_adfr_adjust_tearcheck_for_dynamic_qsync(void *sde_phys_enc);

/* --------------- dsi_connector ---------------*/
int sde_connector_send_fakeframe(void *conn);

/* --------------- dsi_display ---------------*/
int dsi_display_qsync_update_min_fps(void *dsi_display, void *dsi_params);
int dsi_display_qsync_restore(void *dsi_display);
/*
 * dsi_display_send_fakeframe - send 2C/3C dcs to Panel
 * @display: Pointer to private display structure
 * Returns: Zero on success
 */
int dsi_display_send_fakeframe(void *disp);
void dsi_display_adfr_change_te_irq_status(void *display, bool enable);

/* --------------- dsi_panel ---------------*/
int dsi_panel_parse_adfr(void *dsi_mode, void *dsi_utils);
int dsi_panel_send_qsync_min_fps_dcs(void *dsi_panel,
				int ctrl_idx, uint32_t min_fps);
int dsi_panel_send_fakeframe_dcs(void *dsi_panel,
				int ctrl_idx);
void dsi_panel_adfr_status_reset(void *dsi_panel);

/* --------------- vsync switch ---------------*/
void oplus_dsi_display_vsync_switch(void *disp, bool force_te_vsync);
bool oplus_adfr_vsync_switch_is_enable(void);
enum oplus_vsync_mode oplus_adfr_get_vsync_mode(void);
/* ------------- mux switch ------------ */
void sde_encoder_adfr_vsync_switch(void *enc);
void sde_kms_adfr_vsync_switch(void *m_kms, void *d_crtc);
void oplus_adfr_resolution_vsync_switch(void *dsi_panel);
void oplus_adfr_aod_fod_vsync_switch(void *dsi_panel, bool force_te_vsync);
void oplus_adfr_vsync_switch_reset(void *dsi_panel);
/* ---------- te source switch --------- */
u32 oplus_get_vsync_source(void *drm_mode);
void sde_encoder_adfr_vsync_source_switch(void *enc);
void sde_kms_adfr_vsync_source_switch(void *m_kms, void *d_crtc);
bool oplus_adfr_need_deferred_vsync_source_switch(void *encoder, void *adj_mode);
int dsi_panel_aod_need_vsync_source_switch(void *dsi_panel);
void sde_encoder_adfr_aod_fod_source_switch(void *dsi_display, int te_source);

/* --------------- auto mode ---------------*/
bool oplus_adfr_auto_on_cmd_filter_set(bool enable);
bool oplus_adfr_auto_on_cmd_filter_get(void);
int oplus_adfr_handle_auto_mode(u32 propval);
int dsi_display_auto_mode_update(void *dsi_display);
bool oplus_adfr_has_auto_mode(u32 value);
void drm_mode_sort_for_adfr(struct list_head *mode_list);
#endif /* _OPLUS_ADFR_H_ */
