/***************************************************************
** Copyright (C) 2018-2020 OPLUS. All rights reserved.
** OPLUS_BUG_STABILITY
** File : oplus_adfr.h
** Description : ADFR kernel module
** Version : 1.0
** Date : 2020/10/23
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  CaiHuiyue      2020/10/23        1.0           Build this moudle
******************************************************************/
#include "sde_trace.h"
#include "msm_drv.h"
#include "sde_kms.h"
#include "sde_connector.h"
#include "sde_crtc.h"
#include "sde_encoder_phys.h"

#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_parser.h"
#include "dsi_drm.h"
#include "dsi_defs.h"

#include "oplus_adfr.h"

#include <linux/list.h>
#include <linux/list_sort.h>
#include <drm/drm_modes.h>

#define OPLUS_ADFR_CONFIG_GLOBAL (1<<0)
#define OPLUS_ADFR_CONFIG_FAKEFRAME (1<<1)
#define OPLUS_ADFR_CONFIG_VSYNC_SWITCH (1<<2)
#define OPLUS_ADFR_CONFIG_VSYNC_SWITCH_MODE (1<<3)

#define OPLUS_ADFR_DEBUG_GLOBAL_DISABLE (1<<0)
#define OPLUS_ADFR_DEBUG_FAKEFRAME_DISABLE (1<<1)
#define OPLUS_ADFR_DEBUG_VSYNC_SWITCH_DISABLE (1<<2)

#define ADFR_GET_GLOBAL_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_GLOBAL)
#define ADFR_GET_FAKEFRAME_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_FAKEFRAME)
#define ADFR_GET_VSYNC_SWITCH_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_VSYNC_SWITCH)
#define ADFR_GET_VSYNC_SWITCH_MODE(config) ((config) & OPLUS_ADFR_CONFIG_VSYNC_SWITCH_MODE)


#define OPLUS_ADFR_AUTO_MAGIC 0X00800000
#define OPLUS_ADFR_AUTO_MODE_MAGIC 0X00400000
#define OPLUS_ADFR_AUTO_MODE_VALUE(auto_value) (((auto_value)&0X003F0000)>>16)
#define OPLUS_ADFR_AUTO_FAKEFRAME_MAGIC 0X00008000
#define OPLUS_ADFR_AUTO_FAKEFRAME_VALUE(auto_value) (((auto_value)&0X00007F00)>>8)
#define OPLUS_ADFR_AUTO_MIN_FPS_MAGIC 0X00000080
#define OPLUS_ADFR_AUTO_MIN_FPS_VALUE(auto_value) ((auto_value)&0X0000007F)

#define SDC_AUTO_MIN_FPS_CMD_OFFSET 2
#define SDC_MANUAL_MIN_FPS_CMD_OFFSET 1
#define SDC_MIN_FPS_CMD_SIZE 2

#define to_dsi_bridge(x)  container_of((x), struct dsi_bridge, base)

static u32 oplus_adfr_config = 0;
static u32 oplus_adfr_debug = 0;
static bool need_deferred_fakeframe = false;
bool oplus_adfr_compatibility_mode = false;
struct oplus_te_refcount te_refcount = {0, 0, 0, 0};

/* qsync mode minfps */
bool oplus_adfr_qsync_mode_minfps_updated = false;
static u32 oplus_adfr_qsync_mode_minfps = 0;

/* samsung auto mode */
bool oplus_adfr_auto_mode_updated = false;
static u32 oplus_adfr_auto_mode = 0;
bool oplus_adfr_auto_fakeframe_updated = false;
static u32 oplus_adfr_auto_fakeframe = 0;
bool oplus_adfr_auto_min_fps_updated = false;
static u32 oplus_adfr_auto_min_fps = 0;
static u64 oplus_adfr_auto_update_counter = 0;
bool oplus_adfr_need_filter_auto_on_cmd = false;

/* --------------- adfr misc ---------------*/

void oplus_adfr_init(void *panel_node)
{
	static bool inited = false;
	u32 config = 0;
	int rc = 0;
	struct device_node *of_node = panel_node;

	pr_info("kVRR oplus_adfr_init now.");

	if (!of_node) {
		pr_err("kVRR oplus_adfr_init: the param is null.");
		return;
	}

	if (inited) {
		pr_warning("kVRR adfr config = %#X already!", oplus_adfr_config);
		return;
	}

	rc = of_property_read_u32(of_node, "oplus,adfr-config", &config);
	if (rc == 0) {
		oplus_adfr_config = config;
	} else {
		oplus_adfr_config = 0;
	}

	if (oplus_adfr_is_support()) {
		/* if adfr-compatibility-mode is define, should not do the vsync switch, just set to TE vsync always */
		oplus_adfr_compatibility_mode = of_property_read_bool(of_node, "oplus,adfr-compatibility-mode");
	}

	inited = true;

	pr_info("kVRR adfr config = %#X, adfr compatibility mode = %d\n", oplus_adfr_config, oplus_adfr_compatibility_mode);
}

ssize_t oplus_adfr_get_debug(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_err("kVRR get adfr config %#X debug %#X \n", oplus_adfr_config, oplus_adfr_debug);
	return scnprintf(buf, PAGE_SIZE, "debug:0x%08X config:0x%08X auto_mode:0x%08X fakeframe:0x%08X auto_minfps:0x%08X auto_counter:%llu\n",
		oplus_adfr_debug, oplus_adfr_config, oplus_adfr_auto_mode, oplus_adfr_auto_fakeframe, oplus_adfr_auto_min_fps, oplus_adfr_auto_update_counter);
}

ssize_t oplus_adfr_set_debug(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%u", &oplus_adfr_debug);
	pr_err("kVRR get adfr config %#X debug %#X \n", oplus_adfr_config, oplus_adfr_debug);

	return count;
}

static inline bool oplus_adfr_fakeframe_is_enable(void)
{
	return (bool)(ADFR_GET_FAKEFRAME_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_FAKEFRAME_DISABLE) &&
		oplus_adfr_auto_fakeframe);
}

bool oplus_adfr_vsync_switch_is_enable(void)
{
	return (bool)(ADFR_GET_VSYNC_SWITCH_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_VSYNC_SWITCH_DISABLE));
}

enum oplus_vsync_mode oplus_adfr_get_vsync_mode(void)
{
	if (!oplus_adfr_vsync_switch_is_enable()) {
		return OPLUS_INVALID_VSYNC;
	}

	return (enum oplus_vsync_mode)ADFR_GET_VSYNC_SWITCH_MODE(oplus_adfr_config);
}

inline bool oplus_adfr_is_support(void)
{
	return  (bool)(ADFR_GET_GLOBAL_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_GLOBAL_DISABLE));
}

int oplus_enable_te_refcount(void *data)
{
	unsigned int *te_enable =  (unsigned int *)data;
	struct dsi_display *display = NULL;
	DSI_INFO("%s te_enable = %d", __func__, (*te_enable));

	display = get_main_display();
	if (display == NULL) {
		pr_err("%s error :NULL display", __func__);
		return -1;
	}

	if ((*te_enable) == 1) {
		te_refcount.te_calculate_enable = true;
		te_refcount.start_timeline = ktime_get();
		te_refcount.te_refcount = 0;
	} else if ((*te_enable) == 0) {
		te_refcount.te_calculate_enable = false;
		te_refcount.end_timeline = ktime_get();
	}

	dsi_display_adfr_change_te_irq_status(display, te_refcount.te_calculate_enable);

	return 0;
}

int oplus_get_te_fps(void *data)
{
	unsigned int *te_fps =  (unsigned int *)data;

	unsigned long long end_time, start_time;

	end_time = ktime_to_ms(te_refcount.end_timeline);
	start_time = ktime_to_ms(te_refcount.start_timeline);

	if (end_time < start_time) {
		pr_err("%s error :out of time", __func__);
	}

	(*te_fps) = te_refcount.te_refcount*1000 / (end_time - start_time);

	DSI_INFO("%s te count = %d, end_time = %lld, start_time = %lld, te fps = %d",
		__func__, te_refcount.te_refcount, end_time, start_time, (*te_fps));

	return 0;
}

/* --------------- msm_drv ---------------*/

int oplus_adfr_thread_create(void *msm_param_ptr,
	void *msm_priv, void *msm_ddev, void *msm_dev)
{
	struct sched_param *param;
	struct msm_drm_private *priv;
	struct drm_device *ddev;
	struct device *dev;
	int i, ret = 0;

	param = msm_param_ptr;
	priv = msm_priv;
	ddev = msm_ddev;
	dev = msm_dev;

	for (i = 0; i < priv->num_crtcs; i++) {
		/* initialize adfr thread */
		priv->adfr_thread[i].crtc_id = priv->crtcs[i]->base.id;
		kthread_init_worker(&priv->adfr_thread[i].worker);
		priv->adfr_thread[i].dev = ddev;
		priv->adfr_thread[i].thread =
			kthread_run(kthread_worker_fn,
				&priv->adfr_thread[i].worker,
				"adfr:%d", priv->adfr_thread[i].crtc_id);
		ret = sched_setscheduler(priv->adfr_thread[i].thread,
							SCHED_FIFO, param);
		if (ret)
			pr_warn("kVRR adfr thread priority update failed: %d\n",
									ret);

		if (IS_ERR(priv->adfr_thread[i].thread)) {
			dev_err(dev, "kVRR failed to create adfr_commit kthread\n");
			priv->adfr_thread[i].thread = NULL;
		}

		if ((!priv->adfr_thread[i].thread)) {
			/* clean up previously created threads if any */
			for(; i >= 0; i--) {
				if (priv->adfr_thread[i].thread) {
					kthread_stop(
						priv->adfr_thread[i].thread);
					priv->adfr_thread[i].thread = NULL;
				}
			}
			return -EINVAL;
		}
	}

	return 0;
}

void oplus_adfr_thread_destroy(void *msm_priv)
{
	struct msm_drm_private *priv;
	int i;

	priv = msm_priv;

	for (i = 0; i < priv->num_crtcs; i++) {
		if (priv->adfr_thread[i].thread) {
			kthread_flush_worker(&priv->adfr_thread[i].worker);
			kthread_stop(priv->adfr_thread[i].thread);
			priv->adfr_thread[i].thread = NULL;
		}
	}
}

/* ------------ sde_connector ------------ */
/* handle it early since qsync min fps dirty will disappeared with high probabilities */
int oplus_adfr_handle_qsync_mode_minfps(u32 propval)
{
	int handled = 0;
	SDE_INFO("kVRR update qsync mode minfps %u[%08X]\n", propval, propval);

	SDE_ATRACE_BEGIN("oplus_adfr_handle_qsync_mode_minfps");

	oplus_adfr_qsync_mode_minfps_updated = true;
	oplus_adfr_qsync_mode_minfps = propval;
	handled = 1;

	SDE_ATRACE_INT("oplus_adfr_qsync_mode_minfps", oplus_adfr_qsync_mode_minfps);
	SDE_ATRACE_END("oplus_adfr_handle_qsync_mode_minfps");

	SDE_INFO("kVRR qsync mode minfps %u[%d]\n",
		oplus_adfr_qsync_mode_minfps, oplus_adfr_qsync_mode_minfps_updated);

	return handled;
}

bool oplus_adfr_qsync_mode_minfps_is_updated(void) {
	bool updated = oplus_adfr_qsync_mode_minfps_updated;
	oplus_adfr_qsync_mode_minfps_updated = false;
	return updated;
}

u32 oplus_adfr_get_qsync_mode_minfps(void) {
	SDE_INFO("kVRR get qsync mode minfps %u\n", oplus_adfr_qsync_mode_minfps);
	return oplus_adfr_qsync_mode_minfps;
}

/* --------------- sde_crtc ---------------*/

void sde_crtc_adfr_handle_frame_event(void *crt, void* event)
{
	struct drm_crtc *crtc = crt;
	struct sde_crtc *sde_crtc = to_sde_crtc(crtc);
	struct sde_crtc_frame_event *fevent = event;
	struct drm_encoder *encoder;

	/* cancel deferred adfr fakeframe timer */
	if (oplus_adfr_fakeframe_is_enable() &&
		(fevent->event & SDE_ENCODER_FRAME_EVENT_SIGNAL_RETIRE_FENCE)) {
		mutex_lock(&sde_crtc->crtc_lock);
		list_for_each_entry(encoder, &crtc->dev->mode_config.encoder_list, head) {
			if (encoder->crtc != crtc)
				continue;

			sde_encoder_adfr_cancel_fakeframe(encoder);
		}
		mutex_unlock(&sde_crtc->crtc_lock);
	}
}


/* --------------- sde_encoder ---------------*/

static inline struct dsi_display_mode_priv_info *oplus_get_current_mode_priv_info(struct drm_connector * drm_conn)
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
	struct dsi_display *dsi_display;
	struct dsi_panel *panel;

	if (!drm_conn) {
		SDE_ERROR("kVRR adfr drm_conn is null.\n");
		return NULL;
	}

	priv = drm_conn->dev->dev_private;
	sde_kms = to_sde_kms(priv->kms);

	if (!sde_kms) {
		SDE_ERROR("kVRR adfr sde_kms is null.\n");
		return NULL;
	}

	if (sde_kms->dsi_display_count && sde_kms->dsi_displays) {
		/* only use primary dsi */
		dsi_display = sde_kms->dsi_displays[0];
	} else {
		SDE_ERROR("kVRR adfr sde_kms's dsi_display is null.\n");
		return NULL;
	}

	panel = dsi_display->panel;

	if (!panel || !panel->cur_mode) {
		SDE_ERROR("kVRR adfr dsi_display's panel is null.\n");
		return NULL;
	}

	return panel->cur_mode->priv_info;
}

void sde_encoder_adfr_prepare_commit(void *crt, void *enc, void *conn) {
	struct dsi_display_mode_priv_info *priv_info;
	struct drm_crtc *crtc = crt;
	struct drm_connector *drm_conn = conn;

	if (!oplus_adfr_fakeframe_is_enable()) {
		return;
	}

	/* when power on, disable deferred fakeframe
	** after power on and before first frame flush
	** if panel get a fakeframe then refresh itself (with a dirty buffer), tearing happen
	** so for power on case, set need_deferred_fakeframe false
	** this can avoid deferred fakeframe tearing issue (eg. AOD)
	** power off --> sde_encoder_virt_disable set "sde_enc->cur_master = NULL"
	** power on  --> sde_encoder_virt_enable  set "sde_enc->cur_master = XXX"
	** prepare_commit need cur_master is not null but it is before than sde_encoder_virt_enable
	** so use prepare_commit(NULL, NULL, NULL) to imply this commit is first commit after power on
	*/
	if (!crt && !enc && !conn) {
		need_deferred_fakeframe = false;
		/* SDE_ATRACE_INT("need_deferred_fakeframe", need_deferred_fakeframe); */
		return;
	}
	/* after power on, enable deferred fakeframe */
	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		need_deferred_fakeframe = true;
	} else {
		need_deferred_fakeframe = false;
		/* SDE_DEBUG("kVRR display stats: %d , skip fakeframe", get_oplus_display_power_status()); */
		return;
	}

	if (!crt || !enc || !conn) {
		SDE_ERROR("kVRR sde_encoder_adfr_prepare_commit error: %p %p %p",
			crt, enc, conn);
		return;
	}

	priv_info = oplus_get_current_mode_priv_info(drm_conn);

	/* check 1st bit */
	if (!priv_info || !(priv_info->fakeframe_config & 0X00000001)) {
		return;
	}

	/* before commit send a fakeframe to triger the panel flush
	** but if pre-frame is pending, ignore this time
	** because pre-frame is a real frame, Not Need fakeframe
	** SDE_ATRACE_INT("frame_pending", sde_crtc_frame_pending(sde_enc->crtc));
	*/
	if ((sde_crtc_frame_pending(crtc) == 0)) {
		sde_encoder_adfr_trigger_fakeframe(enc);
	}
}

void sde_encoder_adfr_kickoff(void *crt, void *enc, void *conn) {
	struct dsi_display_mode_priv_info *priv_info;
	struct drm_connector *drm_conn = conn;
	int deferred_ms = -1;

	if (!oplus_adfr_fakeframe_is_enable()) {
		return;
	}

	/* SDE_ATRACE_INT("need_deferred_fakeframe", need_deferred_fakeframe); */
	if (!need_deferred_fakeframe) {
		/* SDE_ERROR("kVRR sde_encoder_adfr_kickoff skip, need_deferred_fakeframe is false."); */
		return;
	}

	if (!crt || !enc || !conn) {
		SDE_ERROR("kVRR sde_encoder_adfr_kickoff error:  %p %p %p",
			crt, enc, conn);
		return;
	}

	priv_info = oplus_get_current_mode_priv_info(drm_conn);

	/* check 2st bit */
	if (!priv_info || !(priv_info->fakeframe_config & 0X00000002)) {
		return;
	}
	deferred_ms = priv_info->deferred_fakeframe_time;

	oplus_adfr_fakeframe_timer_start(enc, deferred_ms);
}

/* --------------- sde_encoder_phys_cmd ---------------*/
/* if force_qsync_mode_off is true, close qsync window immediately */
void oplus_adfr_force_qsync_mode_off(void *drm_connector)
{
	struct drm_connector *connector = drm_connector;
	struct sde_connector *c_conn;
	struct dsi_bridge *c_bridge;
	struct dsi_display *display;

	if (!connector || !connector->encoder || !connector->encoder->bridge)
		return;

	c_conn = to_sde_connector(connector);
	c_bridge = to_dsi_bridge(connector->encoder->bridge);
	display = c_bridge->display;

	if (!display)
		return;

	if (display->force_qsync_mode_off) {
		SDE_INFO("kVRR force qsync mode update %d -> %d\n",
				c_conn->qsync_mode, SDE_RM_QSYNC_DISABLED);
		c_conn->qsync_updated = true;
		c_conn->qsync_mode = SDE_RM_QSYNC_DISABLED;
		/* qsync disable need change min fps */
		c_conn->qsync_curr_dynamic_min_fps = 0;
		c_conn->qsync_deferred_window_status = SET_WINDOW_IMMEDIATELY;
		display->force_qsync_mode_off = false;
		SDE_ATRACE_INT("qsync_mode_cmd", 0);
	}

	return;
}

int oplus_adfr_adjust_tearcheck_for_dynamic_qsync(void *sde_phys_enc)
{
	struct sde_encoder_phys *phys_enc = sde_phys_enc;
	struct sde_hw_tear_check tc_cfg = {0};
	struct sde_connector *sde_conn = NULL;
	int ret = 0;

	if (!phys_enc || !phys_enc->connector) {
		SDE_ERROR("kVRR invalid encoder parameters\n");
		return -EINVAL;
	}

	sde_conn = to_sde_connector(phys_enc->connector);

	if (sde_connector_get_qsync_mode(phys_enc->connector) == 0 ||
		sde_connector_get_qsync_dynamic_min_fps(phys_enc->connector) == 0) {
		phys_enc->current_sync_threshold_start = phys_enc->qsync_sync_threshold_start;
		return ret;
	}

	SDE_ATRACE_BEGIN("adjust_tearcheck_for_qsync");
	SDE_ATRACE_INT("frame_state", atomic_read(&phys_enc->frame_state));
	SDE_DEBUG("frame_state = %d\n", atomic_read(&phys_enc->frame_state));

	/* this time maybe remain in qsync window, so shrink qsync window
	** to avoid tearing and keep qsync enable for this frame
	*/
	if (atomic_read(&phys_enc->frame_state) != 0) {
		/* 300 is a estimated value */
		tc_cfg.sync_threshold_start = 300;
	} else {
		/* remain use original qsync window */
		tc_cfg.sync_threshold_start = phys_enc->qsync_sync_threshold_start;
	}

	if(phys_enc->current_sync_threshold_start != tc_cfg.sync_threshold_start) {
		SDE_ATRACE_BEGIN("update_qsync");

		if (phys_enc->has_intf_te &&
			phys_enc->hw_intf->ops.update_tearcheck)
			phys_enc->hw_intf->ops.update_tearcheck(
				phys_enc->hw_intf, &tc_cfg);
		else if (phys_enc->hw_pp->ops.update_tearcheck)
			phys_enc->hw_pp->ops.update_tearcheck(
				phys_enc->hw_pp, &tc_cfg);
		SDE_EVT32(DRMID(phys_enc->parent), tc_cfg.sync_threshold_start);
		phys_enc->current_sync_threshold_start = tc_cfg.sync_threshold_start;
		/* trigger AP update qsync flush */
		sde_conn->qsync_updated = true;

		SDE_ATRACE_END("update_qsync");
	}

	SDE_DEBUG("kVRR threshold_lines %d\n", phys_enc->current_sync_threshold_start);
	SDE_ATRACE_INT("threshold_lines", phys_enc->current_sync_threshold_start);
	SDE_ATRACE_END("adjust_tearcheck_for_qsync");

	return ret;
}

/* --------------- dsi_connector ---------------*/

int sde_connector_send_fakeframe(void *conn)
{
	struct drm_connector *connector = conn;
	struct sde_connector *c_conn;
	int rc;

	if (!connector) {
		SDE_ERROR("kVRR invalid argument\n");
		return -EINVAL;
	}

	c_conn = to_sde_connector(connector);
	if (!c_conn->display) {
		SDE_ERROR("kVRR invalid connector display\n");
		return -EINVAL;
	}

	rc = dsi_display_send_fakeframe(c_conn->display);

	SDE_EVT32(connector->base.id, rc);
	return rc;
}

/* --------------- dsi_display ---------------*/

/* update qsync min fps */
int dsi_display_qsync_update_min_fps(void *dsi_display, void *dsi_params)
{
	struct dsi_display *display = dsi_display;
	struct msm_display_conn_params *params = dsi_params;
	int i;
	int rc = 0;

	if (!params->qsync_update) {
		return 0;
	}

	/* allow qsync off but update qsync min fps only */
	SDE_ATRACE_BEGIN("dsi_display_qsync_update_min_fps");

	mutex_lock(&display->display_lock);

	display_for_each_ctrl(i, display) {
		/* send the commands to updaet qsync min fps */
		rc = dsi_panel_send_qsync_min_fps_dcs(display->panel, i, params->qsync_dynamic_min_fps);
		if (rc) {
			DSI_ERR("kVRR fail qsync UPDATE cmds rc:%d\n", rc);
			goto exit;
		}
	}

exit:
	SDE_EVT32(params->qsync_mode, params->qsync_dynamic_min_fps, rc);
	mutex_unlock(&display->display_lock);

	SDE_ATRACE_END("dsi_display_qsync_update_min_fps");

	return rc;
}

/* save qsync info, then restore qsync status after panel enable*/
int dsi_display_qsync_restore(void *dsi_display)
{
	struct msm_display_conn_params params;
	struct dsi_display *display = dsi_display;
	int rc = 0;

	if (display->need_qsync_restore) {
		display->need_qsync_restore = false;
	} else {
		return 0;
	}

	params.qsync_update = display->current_qsync_mode ||
						  display->current_qsync_dynamic_min_fps;

	if (!params.qsync_update) {
		DSI_DEBUG("kVRR %s:INFO: qsync status is clean.\n", __func__);
		return 0;
	}

	params.qsync_mode = display->current_qsync_mode;
	params.qsync_dynamic_min_fps = display->current_qsync_dynamic_min_fps;

	SDE_ATRACE_BEGIN("dsi_display_qsync_restore");

	DSI_INFO("kVRR qsync restore mode %d minfps %d \n",
	         params.qsync_mode, params.qsync_dynamic_min_fps);
	rc = dsi_display_pre_commit(display, &params);
	SDE_EVT32(params.qsync_mode, params.qsync_dynamic_min_fps, rc);

	SDE_ATRACE_END("dsi_display_qsync_restore");

	return rc;
}

int dsi_display_send_fakeframe(void *disp)
{
	struct dsi_display *display = (struct dsi_display *)disp;
	int i, rc = 0;

	if (!display) {
		pr_err("kVRR Invalid params\n");
		return -EINVAL;
	}

	SDE_ATRACE_BEGIN("dsi_display_send_fakeframe");
	display_for_each_ctrl(i, display) {
		/* send the commands to simulate a frame transmission */
		rc = dsi_panel_send_fakeframe_dcs(display->panel, i);
		if (rc) {
			DSI_ERR("kVRR fail fake frame cmds rc:%d\n", rc);
			goto exit;
		}
	}

exit:
	SDE_ATRACE_END("dsi_display_send_fakeframe");
	SDE_EVT32(rc);

	return rc;
}

/* --------------- dsi_panel ---------------*/

const char *qsync_min_fps_set_map[DSI_CMD_QSYNC_MIN_FPS_COUNTS] = {
	"qcom,mdss-dsi-qsync-min-fps-0",
	"qcom,mdss-dsi-qsync-min-fps-1",
	"qcom,mdss-dsi-qsync-min-fps-2",
	"qcom,mdss-dsi-qsync-min-fps-3",
	"qcom,mdss-dsi-qsync-min-fps-4",
	"qcom,mdss-dsi-qsync-min-fps-5",
	"qcom,mdss-dsi-qsync-min-fps-6",
	"qcom,mdss-dsi-qsync-min-fps-7",
	"qcom,mdss-dsi-qsync-min-fps-8",
	"qcom,mdss-dsi-qsync-min-fps-9",
};

int dsi_panel_send_qsync_min_fps_dcs(void *dsi_panel,
		int ctrl_idx, uint32_t min_fps)
{
	struct dsi_panel *panel = dsi_panel;
	struct dsi_display_mode_priv_info *priv_info;
	int rc = 0;
	int i = 0;

	if (!panel || !panel->cur_mode) {
		DSI_ERR("kVRR Invalid params\n");
		return -EINVAL;
	}

	priv_info = panel->cur_mode->priv_info;

	mutex_lock(&panel->panel_lock);

	/* select a best fps to fit min_fps */
	for(i = priv_info->qsync_min_fps_sets_size - 1; i >= 0; i--) {
		if(priv_info->qsync_min_fps_sets[i] <= min_fps) {
			DSI_DEBUG("kVRR ctrl:%d qsync find min fps %d\n", ctrl_idx, priv_info->qsync_min_fps_sets[i]);
			break;
		}
	}

	if(i >= 0 && i < priv_info->qsync_min_fps_sets_size) {
		DSI_INFO("kVRR ctrl:%d qsync update min fps %d use \n", ctrl_idx, min_fps);
		SDE_ATRACE_INT("oplus_adfr_qsync_mode_minfps_cmd", min_fps);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_QSYNC_MIN_FPS_0+i);
		if (rc)
			DSI_ERR("kVRR [%s] failed to send DSI_CMD_QSYNC_MIN_FPS cmds rc=%d\n",
				panel->name, rc);
	} else {
		DSI_ERR("kVRR ctrl:%d failed to sets qsync min fps %u, %d\n", ctrl_idx, min_fps, i);
	}
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int dsi_panel_send_fakeframe_dcs(void *dsi_panel,
		int ctrl_idx)
{
	struct dsi_panel *panel = dsi_panel;
	int rc = 0;

	/* SDC's auto, fakeframe and minfps are available only after power on */
	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		DSI_INFO("kVRR ignore %s when power is %d", __FUNCTION__, get_oplus_display_power_status());
		return 0;
	}

	if (!panel) {
		DSI_ERR("kVRR invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	DSI_DEBUG("kVRR ctrl:%d fake frame\n", ctrl_idx);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_FAKEFRAME);
	if (rc)
		DSI_ERR("kVRR [%s] failed to send DSI_CMD_FAKEFRAME cmds rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}


static int dsi_panel_parse_qsync_min_fps(
		struct dsi_display_mode_priv_info *priv_info,
		struct dsi_parser_utils *utils)
{
	int rc = 0;
	u32 i;

	if (!priv_info) {
		DSI_ERR("kVRR dsi_panel_parse_qsync_min_fps err: invalid mode priv info\n");
		return -EINVAL;
	}

	priv_info->qsync_min_fps_sets_size = 0;

	for (i = 0; i < DSI_CMD_QSYNC_MIN_FPS_COUNTS; i++) {
		rc = utils->read_u32(utils->data, qsync_min_fps_set_map[i],
			&priv_info->qsync_min_fps_sets[i]);
		if (rc) {
			DSI_DEBUG("kVRR failed to parse qsync min fps set %u\n", i);
			break;
		}
		else {
			priv_info->qsync_min_fps_sets_size++;
			DSI_DEBUG("kVRR parse qsync min fps set %u = %u\n",
			priv_info->qsync_min_fps_sets_size - 1, priv_info->qsync_min_fps_sets[i]);
		}
	}

	return rc;
}

static int dsi_panel_parse_fakeframe(
		struct dsi_display_mode_priv_info *priv_info,
		struct dsi_parser_utils *utils)
{
	int rc = 0;

	if (!priv_info) {
		DSI_ERR("kVRR dsi_panel_parse_fakeframe err: invalid mode priv info\n");
		return -EINVAL;
	}

	priv_info->fakeframe_config = 0;
	priv_info->deferred_fakeframe_time = 0;

	rc = utils->read_u32(utils->data, "oplus,adfr-fakeframe-config",
			&priv_info->fakeframe_config);
	if (rc) {
		DSI_DEBUG("kVRR failed to parse fakeframe.\n");
	}

	rc = utils->read_u32(utils->data, "oplus,adfr-fakeframe-deferred-time",
			&priv_info->deferred_fakeframe_time);
	if (rc) {
		DSI_DEBUG("kVRR failed to parse deferred_fakeframe_time.\n");
	}

	DSI_DEBUG("kVRR adfr fakeframe_config: %u, deferred_fakeframe_time: %u \n",
		priv_info->fakeframe_config, priv_info->deferred_fakeframe_time);

	return rc;
}

int oplus_dsi_panel_parse_panel_vsync_source(
		struct dsi_display_mode *mode,
		struct dsi_parser_utils *utils)
{
	u32 panel_vsync_source = 0;
	int rc = 0;

	if (!mode || !mode->priv_info) {
		DSI_ERR("kVRR invalid arguments\n");
		return -EINVAL;
	}

	if (oplus_adfr_get_vsync_mode() == OPLUS_DOUBLE_TE_VSYNC) {
		rc = utils->read_u32(utils->data,
				"qcom,mdss-dsi-panel-vsync-source", &panel_vsync_source);
	}

	mode->vsync_source = rc ? 0xff : panel_vsync_source;

	if (rc) {
		DSI_ERR("kVRR adfr without vsync_source\n", mode->vsync_source);
	} else {
		DSI_DEBUG("kVRR adfr vsync_source %u \n", mode->vsync_source);
	}

	return rc;
}

int dsi_panel_parse_adfr(void *dsi_mode, void *dsi_utils)
{
	struct dsi_display_mode *mode = dsi_mode;
	struct dsi_parser_utils *utils = dsi_utils;
	struct dsi_display_mode_priv_info *priv_info = mode->priv_info;

	if (dsi_panel_parse_qsync_min_fps(priv_info, utils)) {
		DSI_DEBUG("kVRR adfr failed to parse qsyn min fps\n");
	}
	if (dsi_panel_parse_fakeframe(priv_info, utils)) {
		DSI_DEBUG("kVRR adfr failed to parse fakeframe\n");
	}

	if (oplus_dsi_panel_parse_panel_vsync_source(mode, utils)) {
		DSI_DEBUG("kVRR adfr failed to parse vsync source\n");
	}

	return 0;
}

/* reset auto mode status as panel power on and timing switch to SM */
void dsi_panel_adfr_status_reset(void *dsi_panel)
{
	struct dsi_panel *panel = dsi_panel;
	u32 refresh_rate = 120;
	u32 h_skew = SDC_ADFR;
	u32 oplus_adfr_auto_min_fps_cmd = OPLUS_ADFR_AUTO_MIN_FPS_MAX;

	if ((panel == NULL) || (panel->cur_mode == NULL)) {
		DSI_ERR("kVRR Invalid params");
		return;
	}

	h_skew = panel->cur_mode->timing.h_skew;
	refresh_rate = panel->cur_mode->timing.refresh_rate;

	if ((h_skew == SDC_ADFR) || (h_skew == SDC_MFR)) {
		/* after auto off cmd was sent, auto on cmd filter start */
		oplus_adfr_auto_on_cmd_filter_set(true);

		oplus_adfr_auto_mode = OPLUS_ADFR_AUTO_OFF;
		if (refresh_rate == 60) {
			oplus_adfr_auto_min_fps = OPLUS_ADFR_AUTO_MIN_FPS_60HZ;
		} else {
			/* 90hz min fps in auto mode off should be 0x08 which will be corrected before cmd sent */
			oplus_adfr_auto_min_fps = OPLUS_ADFR_AUTO_MIN_FPS_MAX;

			/* if fakeframe is sent after resolution switch, local garbage issue will happen in low probability */
			if (panel->cur_h_active == panel->cur_mode->timing.h_active) {
				/* 120/90hz should open fakeframe when power on or timing switch */
				oplus_adfr_auto_fakeframe = OPLUS_ADFR_FAKEFRAME_ON;
			}
		}

		if (refresh_rate == 90) {
			/* should +8 in auto off mode */
			oplus_adfr_auto_min_fps_cmd = oplus_adfr_auto_min_fps + 8;
		} else {
			oplus_adfr_auto_min_fps_cmd = oplus_adfr_auto_min_fps;
		}

		/* update auto mode and qsync para when timing switch or panel enable for debug */
		SDE_ATRACE_INT("oplus_adfr_auto_mode", oplus_adfr_auto_mode);
		SDE_ATRACE_INT("oplus_adfr_auto_fakeframe", oplus_adfr_auto_fakeframe);
		SDE_ATRACE_INT("oplus_adfr_auto_min_fps", oplus_adfr_auto_min_fps);
		SDE_ATRACE_INT("oplus_adfr_auto_mode_cmd", oplus_adfr_auto_mode);
		SDE_ATRACE_INT("oplus_adfr_auto_min_fps_cmd", oplus_adfr_auto_min_fps_cmd);
		SDE_ATRACE_INT("oplus_adfr_qsync_mode_minfps_cmd", 0);
		DSI_INFO("kVRR auto mode reset: auto mode %d, fakeframe %d, min fps %d\n", oplus_adfr_auto_mode,
			oplus_adfr_auto_fakeframe, oplus_adfr_auto_min_fps);
	} else {
		SDE_ATRACE_INT("oplus_adfr_auto_mode_cmd", 0);
		SDE_ATRACE_INT("oplus_adfr_auto_min_fps_cmd", 0);
		SDE_ATRACE_INT("oplus_adfr_qsync_mode_minfps_cmd", refresh_rate);
		DSI_INFO("kVRR oplus_adfr_qsync_mode_minfps_cmd %d\n", refresh_rate);
	}
	SDE_ATRACE_INT("h_skew", h_skew);

	return;
}

/* --------------- vsync switch ---------------*/

/* ------------- mux switch ------------ */
static int oplus_dsi_display_adfr_enable_and_waiting_for_next_te_irq(struct dsi_display *display)
{
	int const switch_te_timeout = msecs_to_jiffies(1100);

	dsi_display_adfr_change_te_irq_status(display, true);
	DSI_INFO("kVRR Waiting for the next TE to switch\n");

	display->vsync_switch_pending = true;
	reinit_completion(&display->switch_te_gate);

	if (!wait_for_completion_timeout(&display->switch_te_gate, switch_te_timeout)) {
		DSI_ERR("kVRR vsync switch TE check failed\n");
		dsi_display_adfr_change_te_irq_status(display, false);
		return -EINVAL;
	}

	return 0;
}

/*GPIO SWITCH: 0-TP Vsync    1-TE Vsync*/
static int oplus_dsi_display_vsync_switch_check_te(struct dsi_display *display, int level)
{
	int rc = 0;

	if ((display == NULL) || (display->panel == NULL)) {
		DSI_ERR("kVRR Invalid params");
		return -EINVAL;
	}

	if (level == display->panel->vsync_switch_gpio_level) {
		DSI_INFO("kVRR vsync_switch_gpio is already %d\n", level);
		return 0;
	}

	if (display->panel->force_te_vsync == true) {
		DSI_INFO("kVRR force te vsync, filter other vsync switch\n");
		return 0;
	}

	if (!gpio_is_valid(display->panel->vsync_switch_gpio)) {
		DSI_ERR("kVRR vsync_switch_gpio is invalid\n");
		return -EINVAL;
	}

	oplus_dsi_display_adfr_enable_and_waiting_for_next_te_irq(display);

	if (oplus_adfr_compatibility_mode == false) {
		if (level) {
			rc = gpio_direction_output(display->panel->vsync_switch_gpio, 1);
			if (rc) {
				DSI_ERR("kVRR unable to set dir for vsync_switch_gpio, rc=%d\n", rc);
				dsi_display_adfr_change_te_irq_status(display, false);
				return rc;
			} else {
				DSI_INFO("kVRR set vsync_switch_gpio to 1\n");
			}
		} else {
			gpio_set_value(display->panel->vsync_switch_gpio, 0);
			DSI_INFO("kVRR set vsync_switch_gpio to 0\n");
		}
	}

	dsi_display_adfr_change_te_irq_status(display, false);

	display->panel->vsync_switch_gpio_level = level;
	SDE_ATRACE_INT("vsync_switch_gpio_level", display->panel->vsync_switch_gpio_level);

	return rc;
}

static int oplus_dsi_display_set_vsync_switch_gpio(struct dsi_display *display, int level)
{
	struct dsi_panel *panel = NULL;
	int rc = 0;

	/* only support in mux switch */
	if (oplus_adfr_get_vsync_mode() != OPLUS_EXTERNAL_TE_TP_VSYNC) {
		DSI_ERR("kVRR %s is not supported\n", __func__);
		return -EINVAL;
	}

	if ((display == NULL) || (display->panel == NULL))
		return -EINVAL;

	panel = display->panel;

	mutex_lock(&display->display_lock);

	if (!panel->panel_initialized) {
		if (gpio_is_valid(panel->vsync_switch_gpio)) {
			if (level) {
				rc = gpio_direction_output(panel->vsync_switch_gpio, 1);/* TE Vsync */
				if (rc) {
					DSI_ERR("kVRR unable to set dir for vsync_switch_gpio gpio rc=%d\n", rc);
				} else {
					DSI_INFO("kVRR set vsync_switch_gpio to 1\n");
				}
			} else {
				gpio_set_value(panel->vsync_switch_gpio, 0);/*TP Vsync */
				DSI_INFO("kVRR set vsync_switch_gpio to 0\n");
			}
			panel->vsync_switch_gpio_level = level;
			SDE_ATRACE_INT("vsync_switch_gpio_level", panel->vsync_switch_gpio_level);
		}
	} else {
		oplus_dsi_display_vsync_switch_check_te(display, level);
	}

	mutex_unlock(&display->display_lock);
	return rc;
}

static int oplus_dsi_display_get_vsync_switch_gpio(struct dsi_display *display)
{
	if ((display == NULL) || (display->panel == NULL))
		return -EINVAL;

	return display->panel->vsync_switch_gpio_level;
}


/*GPIO SWITCH: 0-TP Vsync    1-TE Vsync*/
ssize_t oplus_set_vsync_switch(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;
	int vsync_switch_gpio = 0;

	sscanf(buf, "%du", &vsync_switch_gpio);

	printk(KERN_INFO "kVRR %s oplus_set_vsync_switch = %d\n", __func__, vsync_switch_gpio);

	ret = oplus_dsi_display_set_vsync_switch_gpio(display, vsync_switch_gpio);
	if (ret)
		pr_err("kVRR oplus_dsi_display_set_vsync_switch_gpio(%d) fail\n", vsync_switch_gpio);

	return count;
}

ssize_t oplus_get_vsync_switch(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_display *display = get_main_display();
	int vsync_switch_gpio = OPLUS_VSYNC_SWITCH_TE;

	vsync_switch_gpio = oplus_dsi_display_get_vsync_switch_gpio(display);

	return sprintf(buf, "%d\n", vsync_switch_gpio);
}

void oplus_dsi_display_vsync_switch(void *disp, bool force_te_vsync)
{
	struct dsi_display *display = disp;
	int level = OPLUS_VSYNC_SWITCH_TE;
	int h_skew = SDC_ADFR;
	int rc = 0;

	if (!oplus_adfr_vsync_switch_is_enable()) {
		SDE_EVT32(0);
		return;
	}

	if ((display == NULL) || (display->panel == NULL) || (display->panel->cur_mode == NULL)) {
		DSI_ERR("kVRR Invalid params");
		return;
	}

	if (force_te_vsync == true) {
		if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
			if (display->panel->vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TP) {
				level = OPLUS_VSYNC_SWITCH_TE;
				oplus_dsi_display_vsync_switch_check_te(display, level);

				display->panel->force_te_vsync = true;
			}
		}
	} else {
		/* disable fake frame before vsync switch */
		oplus_adfr_auto_fakeframe = OPLUS_ADFR_FAKEFRAME_OFF;
		DSI_INFO("kVRR fakeframe %d\n", oplus_adfr_auto_fakeframe);
		SDE_ATRACE_INT("oplus_adfr_auto_fakeframe", oplus_adfr_auto_fakeframe);
		mutex_lock(&display->panel->panel_lock);
		rc = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_ADFR_PRE_SWITCH);
		mutex_unlock(&display->panel->panel_lock);
		if (rc)
			DSI_ERR("kVRR [%s] failed to send DSI_CMD_ADFR_PRE_SWITCH cmds rc=%d\n", display->panel, rc);

		if (oplus_adfr_get_vsync_mode() != OPLUS_EXTERNAL_TE_TP_VSYNC) {
			DSI_DEBUG("kVRR OPLUS_EXTERNAL_TE_TP_VSYNC is not supported\n");
			return;
		}

		h_skew = display->panel->cur_mode->timing.h_skew;

		if (h_skew == OPLUS_ADFR) {
			level = OPLUS_VSYNC_SWITCH_TE;
		} else {
			level = OPLUS_VSYNC_SWITCH_TP;
		}

		oplus_dsi_display_vsync_switch_check_te(display, level);
	}
}

void sde_kms_adfr_vsync_switch(void *m_kms,
		void *d_crtc)
{
	struct msm_kms *kms = m_kms;
	struct drm_crtc *crtc = d_crtc;
	struct drm_encoder *encoder;
	struct drm_device *dev;

	if (!kms || !crtc || !crtc->state) {
		SDE_ERROR("kVRR invalid params\n");
		return;
	}

	dev = crtc->dev;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc != crtc)
			continue;
		sde_encoder_adfr_vsync_switch(encoder);
	}
}

/*
 if use TP when timing switch (resolution switch), tearing happen
 it seems like DDIC does not support MIPI offset writes after resolution switching
 TE is official, so do the TE switch after timing switch because MIPI will be reset after that
 if current use TE, do nothing
*/
void oplus_adfr_resolution_vsync_switch(void *dsi_panel)
{
	int rc = 0;
	struct dsi_panel *panel = dsi_panel;

	if ((panel == NULL) || (panel->cur_mode == NULL)) {
		DSI_ERR("kVRR Invalid params");
		return;
	}

	/* just do switch when use tp vsync and resolution change */
	if ((panel->cur_h_active != panel->cur_mode->timing.h_active) && (panel->vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TP)) {
		if (gpio_is_valid(panel->vsync_switch_gpio)) {
			rc = gpio_direction_output(panel->vsync_switch_gpio, 1);
			if (rc) {
				DSI_ERR("kVRR unable to set dir for vsync_switch_gpio gpio rc=%d\n", rc);
			} else {
				DSI_INFO("kVRR set vsync_switch_gpio to 1\n");
			}
			panel->vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TE;
		}

		/* after one frame commit completed, change back to current mode vsync */
		panel->need_vsync_switch = true;
		SDE_ATRACE_INT("vsync_switch_gpio_level", panel->vsync_switch_gpio_level);
	}
	panel->cur_h_active = panel->cur_mode->timing.h_active;
}

/* vsync switch Entry and exit */
void oplus_adfr_aod_fod_vsync_switch(void *dsi_panel, bool force_te_vsync)
{
	struct dsi_panel *panel = dsi_panel;
	int h_skew = SDC_ADFR;
	int rc = 0;

	if (!oplus_adfr_vsync_switch_is_enable()) {
		SDE_EVT32(0);
		return;
	}

	if (panel == NULL) {
		DSI_ERR("kVRR Invalid params");
		return;
	}

	/* force switch to te vsync as tp vsync will change in aod and fod mode */
	if (force_te_vsync == true) {
		if (panel->vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TP) {
			if (gpio_is_valid(panel->vsync_switch_gpio)) {
				rc = gpio_direction_output(panel->vsync_switch_gpio, 1);
				if (rc) {
					DSI_ERR("kVRR unable to set dir for vsync_switch_gpio gpio rc=%d\n", rc);
				} else {
					DSI_INFO("kVRR set vsync_switch_gpio to 1\n");
				}
				panel->vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TE;
				panel->force_te_vsync = true;
				SDE_ATRACE_INT("vsync_switch_gpio_level", panel->vsync_switch_gpio_level);
			}
		}
	} else {
		/* change back to tp vysnc since aod/fod mode is off */
		if ((panel->force_te_vsync == true) && (panel->vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TE)) {
			h_skew = panel->cur_mode->timing.h_skew;
			/* maybe change to OA in aod/fod mode */
			if (h_skew == SDC_ADFR || h_skew == SDC_MFR || h_skew == OPLUS_MFR) {
				panel->need_vsync_switch = true;
			}
			panel->force_te_vsync = false;
		}
	}
}

/* switch to tp vsync since panel is no longer in aod mode after power on */
void oplus_adfr_vsync_switch_reset(void *dsi_panel)
{
	struct dsi_panel *panel = dsi_panel;
	u32 h_skew = SDC_ADFR;

	if ((panel == NULL) || (panel->cur_mode == NULL)) {
		DSI_ERR("kVRR Invalid params");
		return;
	}

	h_skew = panel->cur_mode->timing.h_skew;

	if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
		/* reset to tp vsync after power on */
		if (panel->panel_initialized == false) {
			if (panel->force_te_vsync == true) {
				/* maybe change to OA in aod/fod mode */
				if (h_skew == SDC_ADFR || h_skew == SDC_MFR || h_skew == OPLUS_MFR) {
					/* could not change vsync gpio if the machine is incompatible with adfr */
					if (oplus_adfr_compatibility_mode == false) {
						if (gpio_is_valid(panel->vsync_switch_gpio)) {
							gpio_set_value(panel->vsync_switch_gpio, 0);
							DSI_INFO("kVRR set vsync_switch_gpio to 0\n");
							panel->vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TP;
						}
					}
				}
				panel->force_te_vsync = false;
				SDE_ATRACE_INT("vsync_switch_gpio_level", panel->vsync_switch_gpio_level);
			}
		}
	}
}

/* ---------- te source switch --------- */
u32 oplus_get_vsync_source(void *drm_mode) {
	struct drm_display_mode *mode = drm_mode;
	char *vsync_source_str = NULL;

	if (!mode) {
		DSI_ERR("kVRR oplus_get_vsync_source error, mode is null, use 0 default.\n");
		return 0;
	}

	vsync_source_str = strstr(mode->name, "0cmd");
	if (vsync_source_str) {
		DSI_INFO("kVRR oplus_get_vsync_source 0, %s.\n", vsync_source_str);
		return 0;
	}

	vsync_source_str = strstr(mode->name, "1cmd");
	if (vsync_source_str) {
		DSI_INFO("kVRR oplus_get_vsync_source 1, %s.\n", vsync_source_str);
		return 1;
	}

	DSI_ERR("kVRR oplus_get_vsync_source error, name is invalid, use 0 default.\n");
	return 0;
}

void sde_kms_adfr_vsync_source_switch(void *m_kms,
		void *d_crtc)
{
	struct msm_kms *kms = m_kms;
	struct drm_crtc *crtc = d_crtc;
	struct drm_encoder *encoder;
	struct drm_device *dev;

	if (!kms || !crtc || !crtc->state) {
		SDE_ERROR("kVRR invalid params\n");
		return;
	}

	dev = crtc->dev;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc != crtc)
			continue;
		sde_encoder_adfr_vsync_source_switch(encoder);
	}
}

/*return value*/
/*1: switch to panel TE*/
/*0: don't need to switch to panel TE*/
int dsi_panel_aod_need_vsync_source_switch(void *dsi_panel)
{
	struct dsi_panel *panel = dsi_panel;
	u32 h_skew = SDC_ADFR;

	if ((panel == NULL) || (panel->cur_mode == NULL)) {
		DSI_ERR("kVRR Invalid params");
		return 0;
	}

	h_skew = panel->cur_mode->timing.h_skew;
	DSI_INFO("kVRR %s h_skew = %d", __func__, h_skew);

	if ((h_skew == OPLUS_ADFR) || (h_skew == OPLUS_MFR)) {
		return 0;
	} else if ((h_skew == SDC_ADFR) || (h_skew == SDC_MFR)) {
		return 1;
	} else {
		DSI_ERR("kVRR %s error value", __func__);
		return 0;
	}
}

/* --------------- auto mode ---------------*/
/* if auto on command is to be sent within the same frame, filter it out */
bool oplus_adfr_auto_on_cmd_filter_set(bool enable)
{
	oplus_adfr_need_filter_auto_on_cmd = enable;
	return oplus_adfr_need_filter_auto_on_cmd;
}

bool oplus_adfr_auto_on_cmd_filter_get(void)
{
	return oplus_adfr_need_filter_auto_on_cmd;
}

bool oplus_adfr_has_auto_mode(u32 value)
{
	return (value & OPLUS_ADFR_AUTO_MAGIC);
}

int oplus_adfr_handle_auto_mode(u32 propval)
{
	int handled = 0;
	DSI_INFO("kVRR update auto mode %u 0x%08X \n", propval, propval);

	SDE_ATRACE_BEGIN("oplus_adfr_handle_auto_mode");

	if (!(propval & OPLUS_ADFR_AUTO_MAGIC)) {
		DSI_INFO("kVRR update auto mode skip, without auto magic %08X \n", propval);
		SDE_ATRACE_END("oplus_adfr_handle_auto_mode");
		return handled;
	} else if (oplus_adfr_auto_on_cmd_filter_get() && (propval & OPLUS_ADFR_AUTO_MODE_MAGIC) &&
		(OPLUS_ADFR_AUTO_MODE_VALUE(propval) == OPLUS_ADFR_AUTO_ON)) {
		DSI_INFO("kVRR auto off and auto on cmd are sent on the same frame, filter it\n");
		SDE_ATRACE_END("oplus_adfr_handle_auto_mode");
		handled = 1;
		return handled;
	}

	handled = 1;
	oplus_adfr_auto_update_counter += 1;
	DSI_INFO("kVRR auto update counter %llu\n", oplus_adfr_auto_update_counter);

	if (propval & OPLUS_ADFR_AUTO_MODE_MAGIC) {
		if (OPLUS_ADFR_AUTO_MODE_VALUE(propval) != oplus_adfr_auto_mode) {
			oplus_adfr_auto_mode_updated = true;
			/* when auto mode changes, write the corresponding min fps again */
			oplus_adfr_auto_min_fps_updated = true;
			oplus_adfr_auto_mode = OPLUS_ADFR_AUTO_MODE_VALUE(propval);
			handled += 2;
		}
	}

	if (propval & OPLUS_ADFR_AUTO_FAKEFRAME_MAGIC) {
		if (OPLUS_ADFR_AUTO_FAKEFRAME_VALUE(propval) != oplus_adfr_auto_fakeframe) {
			oplus_adfr_auto_fakeframe_updated = true;
			oplus_adfr_auto_fakeframe = OPLUS_ADFR_AUTO_FAKEFRAME_VALUE(propval);
			handled += 4;
		}
	}

	if (propval & OPLUS_ADFR_AUTO_MIN_FPS_MAGIC) {
		if (OPLUS_ADFR_AUTO_MIN_FPS_VALUE(propval) != oplus_adfr_auto_min_fps) {
			oplus_adfr_auto_min_fps_updated = true;
			oplus_adfr_auto_min_fps = OPLUS_ADFR_AUTO_MIN_FPS_VALUE(propval);
			handled += 8;
		}
	}

	if (handled == 1) {
		DSI_WARN("kVRR update auto mode nothing, unknown or repetitive value %08X\n", propval);
	}

	SDE_ATRACE_INT("auto_handled", handled);
	SDE_ATRACE_INT("oplus_adfr_auto_mode", oplus_adfr_auto_mode);
	SDE_ATRACE_INT("oplus_adfr_auto_fakeframe", oplus_adfr_auto_fakeframe);
	SDE_ATRACE_INT("oplus_adfr_auto_min_fps", oplus_adfr_auto_min_fps);
	SDE_ATRACE_END("oplus_adfr_handle_auto_mode");

	/* latest setting, but if power on/off or timing switch, the mode and min fps are not right */
	DSI_INFO("kVRR auto mode %d[%d], fakeframe %d[%d], min fps %d[%d]\n",
		oplus_adfr_auto_mode, oplus_adfr_auto_mode_updated,
		oplus_adfr_auto_fakeframe, oplus_adfr_auto_fakeframe_updated,
		oplus_adfr_auto_min_fps, oplus_adfr_auto_min_fps_updated);

	return handled;
}

static int dsi_panel_send_auto_on_dcs(struct dsi_panel *panel,
		int ctrl_idx)
{
	int rc = 0;

	/* SDC's auto, fakeframe and minfps are available only after power on */
	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		DSI_INFO("kVRR ignore %s when power is %d", __FUNCTION__, get_oplus_display_power_status());
		return 0;
	}

	if (!panel) {
		DSI_ERR("kVRR invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	DSI_INFO("kVRR ctrl:%d auto on\n", ctrl_idx);
	SDE_ATRACE_INT("oplus_adfr_auto_mode_cmd", OPLUS_ADFR_AUTO_ON);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_QSYNC_ON);
	if (rc)
		DSI_ERR("kVRR [%s] failed to send DSI_CMD_SET_AUTO_ON cmds rc=%d\n",
		       panel->name, rc);

	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int dsi_panel_send_auto_off_dcs(struct dsi_panel *panel,
		int ctrl_idx)
{
	int rc = 0;

	/* SDC's auto, fakeframe and minfps are available only after power on */
	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		DSI_INFO("kVRR ignore %s when power is %d", __FUNCTION__, get_oplus_display_power_status());
		return 0;
	}

	if (!panel) {
		DSI_ERR("kVRR invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	DSI_INFO("kVRR ctrl:%d auto off\n", ctrl_idx);
	SDE_ATRACE_INT("oplus_adfr_auto_mode_cmd", OPLUS_ADFR_AUTO_OFF);
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_QSYNC_OFF);
	if (rc) {
		DSI_ERR("kVRR [%s] failed to send DSI_CMD_SET_AUTO_OFF cmds rc=%d\n",
		       panel->name, rc);
	} else {
		/* after auto off cmd was sent, auto on cmd filter start */
		oplus_adfr_auto_on_cmd_filter_set(true);
	}

	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int dsi_display_auto_mode_enable(struct dsi_display *display, bool enable)
{
	int i;
	int rc = 0;

	mutex_lock(&display->display_lock);

	display_for_each_ctrl(i, display) {
		if (enable) {
			/* send the commands to enable auto mode */
			rc = dsi_panel_send_auto_on_dcs(display->panel, i);
			if (rc) {
				DSI_ERR("kVRR fail auto ON cmds rc:%d\n", rc);
				goto exit;
			}
		} else {
			/* send the commands to disbale auto mode */
			rc = dsi_panel_send_auto_off_dcs(display->panel, i);
			if (rc) {
				DSI_ERR("kVRR fail auto OFF cmds rc:%d\n", rc);
				goto exit;
			}
		}
	}

exit:
	SDE_EVT32(enable, rc);
	mutex_unlock(&display->display_lock);
	return rc;
}

static int dsi_panel_auto_minfps_check(struct dsi_panel *panel, u32 extend_frame)
{
	int h_skew = panel->cur_mode->timing.h_skew;
	int refresh_rate = panel->cur_mode->timing.refresh_rate;

	if (h_skew == SDC_ADFR) {
		if (oplus_adfr_auto_mode == OPLUS_ADFR_AUTO_OFF) {
			if (refresh_rate == 120) {
				if (extend_frame < 0 || extend_frame > 4)
					extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
			} else if (refresh_rate == 90) {
				if (extend_frame < 0 || extend_frame > 2)
					extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX + 8;
				else
					extend_frame = extend_frame + 8;
			}
		} else {
			if (refresh_rate == 120) {
				if (extend_frame < 0 || extend_frame > 119)
					extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
			} else if (refresh_rate == 90) {
				if (extend_frame < 0 || extend_frame > 5)
					extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
			}
		}
	} else if (h_skew == SDC_MFR) {
		if (extend_frame < 1 || extend_frame > 4)
			extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_60HZ;
	}

	return extend_frame;
}

static int dsi_panel_send_auto_minfps_dcs(struct dsi_panel *panel,
		int ctrl_idx, u32 extend_frame)
{
	int rc = 0;
	struct dsi_display_mode *mode;
	struct dsi_cmd_desc *cmds;
	size_t tx_len;
	u8 *tx_buf;
	u32 count;
	int k = 0;

	/* SDC's auto, fakeframe and minfps are available only after power on */
	if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		DSI_INFO("kVRR ignore %s %u when power is %d", __FUNCTION__, extend_frame, get_oplus_display_power_status());
		return 0;
	}

	if (!panel || !panel->cur_mode) {
		DSI_ERR("kVRR invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mode = panel->cur_mode;

	/*check minfps*/
	extend_frame = dsi_panel_auto_minfps_check(panel, extend_frame);

	/*update the sdc min fps cmds*/
	if (oplus_adfr_auto_mode == OPLUS_ADFR_AUTO_OFF) {
		cmds = mode->priv_info->cmd_sets[DSI_CMD_QSYNC_MIN_FPS_0].cmds;
		count = mode->priv_info->cmd_sets[DSI_CMD_QSYNC_MIN_FPS_0].count;

		if (count == 0) {
			DSI_ERR("kVRR [%s] No commands to be sent for manual min fps.\n", panel->name);
			goto exit;
		}

		if (count <= SDC_MANUAL_MIN_FPS_CMD_OFFSET) {
			DSI_ERR("kVRR [%s] No commands to be sent for manual min fps, wrong cmds count.\n", panel->name);
			goto exit;
		}

		/*update manual min fps*/
		tx_len = cmds[SDC_MANUAL_MIN_FPS_CMD_OFFSET].msg.tx_len;
		tx_buf = (u8 *)cmds[SDC_MANUAL_MIN_FPS_CMD_OFFSET].msg.tx_buf;
		if (tx_len != SDC_MIN_FPS_CMD_SIZE) {
			DSI_ERR("kVRR [%s] No commands to be sent for manual min fps, wrong cmds size %u.\n", panel->name, tx_len);
			goto exit;
		}

		tx_buf[SDC_MIN_FPS_CMD_SIZE-1] = extend_frame;
		DSI_INFO("kVRR send manual min fps %u .\n", extend_frame);
		for (k = 0; k < tx_len; k++) {
			DSI_DEBUG("kVRR manual min fps %02x", tx_buf[k]);
		}

		DSI_DEBUG("kVRR ctrl:%d manual min fps\n", ctrl_idx);
		SDE_ATRACE_INT("oplus_adfr_auto_min_fps_cmd", extend_frame);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_QSYNC_MIN_FPS_0);
		if (rc)
			DSI_ERR("kVRR [%s] failed to send DSI_CMD_QSYNC_MIN_FPS_0 cmds rc=%d\n",
				panel->name, rc);
	} else {
		cmds = mode->priv_info->cmd_sets[DSI_CMD_QSYNC_MIN_FPS_1].cmds;
		count = mode->priv_info->cmd_sets[DSI_CMD_QSYNC_MIN_FPS_1].count;

		if (count == 0) {
			DSI_DEBUG("kVRR [%s] No commands to be sent for auto min fps.\n", panel->name);
			goto exit;
		}

		if (count <= SDC_AUTO_MIN_FPS_CMD_OFFSET) {
			DSI_ERR("kVRR [%s] No commands to be sent for auto min fps, wrong cmds count.\n", panel->name);
			goto exit;
		}
		/*update auto min fps*/
		tx_len = cmds[SDC_AUTO_MIN_FPS_CMD_OFFSET].msg.tx_len;
		tx_buf = (u8 *)cmds[SDC_AUTO_MIN_FPS_CMD_OFFSET].msg.tx_buf;
		if (tx_len != SDC_MIN_FPS_CMD_SIZE) {
			DSI_ERR("kVRR [%s] No commands to be sent for auto min fps, wrong cmds size %u.\n", panel->name, tx_len);
			goto exit;
		}

		tx_buf[SDC_MIN_FPS_CMD_SIZE-1] = extend_frame;
		DSI_INFO("kVRR send auto min fps %u .\n", extend_frame);
		for (k = 0; k < tx_len; k++) {
			DSI_DEBUG("kVRR auto min fps %02x", tx_buf[k]);
		}

		DSI_DEBUG("kVRR ctrl:%d auto min fps\n", ctrl_idx);
		SDE_ATRACE_INT("oplus_adfr_auto_min_fps_cmd", extend_frame);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_QSYNC_MIN_FPS_1);
		if (rc)
			DSI_ERR("kVRR [%s] failed to send DSI_CMD_QSYNC_MIN_FPS_1 cmds rc=%d\n",
				panel->name, rc);
	}

exit:
	SDE_EVT32(extend_frame, rc);
	mutex_unlock(&panel->panel_lock);
	return rc;
}

static int dsi_display_auto_mode_min_fps(struct dsi_display *display, u32 extend_frame)
{
	int i;
	int rc = 0;

	mutex_lock(&display->display_lock);

	display_for_each_ctrl(i, display) {
		/* send the commands to set auto mode min fps */
		rc = dsi_panel_send_auto_minfps_dcs(display->panel, i, extend_frame);
		if (rc) {
			DSI_ERR("kVRR fail auto Min Fps cmds rc:%d\n", rc);
			goto exit;
		}
	}

exit:
	SDE_EVT32(extend_frame, rc);
	mutex_unlock(&display->display_lock);
	return rc;
}

int dsi_display_auto_mode_update(void *dsi_display)
{
	struct dsi_display *display = dsi_display;
	int h_skew = SDC_ADFR;
	int rc = 0;

	if (!display || !display->panel || !display->panel->cur_mode) {
		DSI_ERR("kVRR dsi_display_auto_mode_update Invalid params\n");
		return -EINVAL;
	}

	h_skew = display->panel->cur_mode->timing.h_skew;
	if ((h_skew != SDC_ADFR) && (h_skew != SDC_MFR)) {
		/* DSI_ERR("kVRR OPLUS ADFR does not support auto mode setting\n"); */
		return 0;
	}

	SDE_ATRACE_BEGIN("dsi_display_auto_mode_update");

	if (oplus_adfr_auto_mode_updated) {
		dsi_display_auto_mode_enable(display, oplus_adfr_auto_mode);
		oplus_adfr_auto_mode_updated = false;
	}

	if (oplus_adfr_auto_min_fps_updated) {
		dsi_display_auto_mode_min_fps(display, oplus_adfr_auto_min_fps);
		oplus_adfr_auto_min_fps_updated = false;
	}

	if (oplus_adfr_auto_fakeframe_updated) {
		/* fake frame update take effect immediately, so just reset it's updated status here */
		oplus_adfr_auto_fakeframe_updated = false;
	}

	SDE_ATRACE_END("dsi_display_auto_mode_update");

	return rc;
}

/**
 * drm_mode_compare_for_adfr - compare modes for favorability
 * @priv: unused
 * @lh_a: list_head for first mode
 * @lh_b: list_head for second mode
 *
 * Compare two modes, given by @lh_a and @lh_b, returning a value indicating
 * which is better.
 *
 * Returns:
 * Negative if @lh_a is better than @lh_b, zero if they're equivalent, or
 * positive if @lh_b is better than @lh_a.
 */
static int drm_mode_compare_for_adfr(void *priv, struct list_head *lh_a, struct list_head *lh_b)
{
	struct drm_display_mode *a = list_entry(lh_a, struct drm_display_mode, head);
	struct drm_display_mode *b = list_entry(lh_b, struct drm_display_mode, head);
	int diff = 0;

	if ((a == NULL) || (b == NULL))
		return diff;

	diff = ((b->type & DRM_MODE_TYPE_PREFERRED) != 0) -
		((a->type & DRM_MODE_TYPE_PREFERRED) != 0);
	if (diff)
		return diff;
	diff = a->hdisplay * a->vdisplay - b->hdisplay * b->vdisplay;
	if (diff)
		return diff;

	diff = a->vrefresh - b->vrefresh;
	if (diff)
		return diff;

	diff = b->clock - a->clock;
	return diff;
}

/**
 * drm_mode_sort_for_adfr - sort mode list
 * @mode_list: list of drm_display_mode structures to sort
 *
 * Sort @mode_list by favorability, moving good modes to the head of the list.
 */
void drm_mode_sort_for_adfr(struct list_head *mode_list)
{
	list_sort(NULL, mode_list, drm_mode_compare_for_adfr);
}
EXPORT_SYMBOL(drm_mode_sort_for_adfr);

