/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** OPLUS_BUG_STABILITY
** File : oplus_dc_diming.c
** Description : oplus dc_diming feature
** Version : 1.0
** Date : 2020/04/15
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/15        1.0           Build this moudle
******************************************************************/

#include "oplus_display_private_api.h"
#include "oplus_dc_diming.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_aod.h"
#include "sde_trace.h"
#include "dsi_defs.h"

int oplus_dimlayer_bl = 0;
int oplus_dimlayer_bl_enabled = 0;
int oplus_datadimming_v3_skip_frame = 2;
int oplus_panel_alpha = 0;
int oplus_underbrightness_alpha = 0;
static struct dsi_panel_cmd_set oplus_priv_seed_cmd_set;
int oplus_datadimming_vblank_count = 0;
atomic_t oplus_datadimming_vblank_ref = ATOMIC_INIT(0);

extern int oplus_dimlayer_bl_on_vblank;
extern int oplus_dimlayer_bl_off_vblank;
extern int oplus_dimlayer_bl_delay;
extern int oplus_dimlayer_bl_delay_after;
extern int oplus_dimlayer_bl_enable;
extern int oplus_dimlayer_bl_enable_v2;
extern int oplus_dimlayer_bl_enable_v2_real;
extern int oplus_dimlayer_bl_enable_v3;
extern int oplus_dimlayer_bl_enable_v3_real;
extern int oplus_fod_on_vblank;
extern int oplus_fod_off_vblank;
extern bool oplus_skip_datadimming_sync;
extern int oplus_dimlayer_hbm_vblank_count;
extern atomic_t oplus_dimlayer_hbm_vblank_ref;
extern int oplus_dc2_alpha;
extern int oplus_seed_backlight;
extern int oplus_panel_alpha;
extern int mca_mode;
extern ktime_t oplus_backlight_time;
extern int seed_mode;
extern int dsi_panel_seed_mode(struct dsi_panel *panel, int mode);

extern void send_kevent_msg(int *msg);
int dc_apollo_enable = 0;
EXPORT_SYMBOL(dc_apollo_enable);

struct hbm_status
{
	bool hbm_aod_status; /*use for hbm aod cmd*/
	bool hbm_status;     /*use for hbm cmd*/
	bool hbm_pvt_status; /*use for hbm other cmds*/
};

struct hbm_status oplus_hbm_status = {0};

int dsi_panel_tx_cmd_hbm_pre_check(struct dsi_panel *panel, enum dsi_cmd_set_type type, const char** prop_map)
{
	int ret = 0;

	DSI_DEBUG("%s cmd=%s", __func__, prop_map[type]);
	if (NULL == panel) {
		return ret;
	}
	if ((panel->oplus_priv.is_oplus_project) &&
		(!strcmp(panel->oplus_priv.vendor_name, "AMB655X"))) {
		switch(type) {
		case DSI_CMD_AOD_HBM_ON_PVT:
		case DSI_CMD_AOD_HBM_ON:
			if (oplus_hbm_status.hbm_pvt_status == 1) {
				DSI_DEBUG("%s skip cmd=%s", __func__, prop_map[type]);
				ret = 1;
			}
			break;
		case DSI_CMD_AOD_HBM_OFF_PVT:
		case DSI_CMD_AOD_HBM_OFF:
			if (oplus_hbm_status.hbm_pvt_status == 0) {
				DSI_DEBUG("%s skip cmd=%s", __func__, prop_map[type]);
				ret = 1;
			}
			break;
		default:
			break;
		}
	}

	return ret;
}

void dsi_panel_tx_cmd_hbm_post_check(struct dsi_panel *panel, enum dsi_cmd_set_type type)
{
	if (NULL == panel) {
		return;
	}

	if ((panel->oplus_priv.is_oplus_project) &&
		(!strcmp(panel->oplus_priv.vendor_name, "AMB655X"))) {
		switch(type) {
		case DSI_CMD_AOD_HBM_ON_PVT:
		case DSI_CMD_AOD_HBM_ON:
			if (oplus_hbm_status.hbm_pvt_status == 0) {
				oplus_hbm_status.hbm_pvt_status = 1;
			}
			break;
		case DSI_CMD_AOD_HBM_OFF_PVT:
		case DSI_CMD_AOD_HBM_OFF:
			if (oplus_hbm_status.hbm_pvt_status == 1) {
				oplus_hbm_status.hbm_pvt_status = 0;
			}
			break;
		case DSI_CMD_SET_NOLP:
		case DSI_CMD_SET_OFF:
		case DSI_CMD_SET_NOLP_PVT:
			oplus_hbm_status.hbm_pvt_status = 0;
			break;
		default:
			break;
		}
	}

	DSI_DEBUG("%s [hbm_pvt,hbm,hbm_aod] = [%d %d %d]", __func__,
		oplus_hbm_status.hbm_pvt_status, oplus_hbm_status.hbm_status, oplus_hbm_status.hbm_aod_status);

	return;
}



static struct oplus_brightness_alpha brightness_seed_alpha_lut_dc[] = {
	{0, 0xff},
	{1, 0xfc},
	{2, 0xfb},
	{3, 0xfa},
	{4, 0xf9},
	{5, 0xf8},
	{6, 0xf7},
	{8, 0xf6},
	{10, 0xf4},
	{15, 0xf0},
	{20, 0xea},
	{30, 0xe0},
	{45, 0xd0},
	{70, 0xbc},
	{100, 0x98},
	{120, 0x80},
	{140, 0x70},
	{160, 0x58},
	{180, 0x48},
	{200, 0x30},
	{220, 0x20},
	{240, 0x10},
	{260, 0x00},
};

/*Jiasong.ZhongPSW.MM.Display.LCD.Stable,2020-09-17 add for DC backlight */
int dsi_panel_parse_oplus_dc_config(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct oplus_brightness_alpha *seq;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	arr = utils->get_property(utils->data, "oplus,dsi-dc-brightness", &length);
	if (!arr) {
		DSI_ERR("[%s] oplus,dsi-dc-brightness  not found\n", panel->name);
		return -EINVAL;
	}

	if (length & 0x1) {
		DSI_ERR("[%s] oplus,dsi-dc-brightness length error\n", panel->name);
		return -EINVAL;
	}

	DSI_DEBUG("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "oplus,dsi-dc-brightness",
					arr_32, length);
	if (rc) {
		DSI_ERR("[%s] cannot read dsi-dc-brightness\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->dc_ba_seq = seq;
	panel->dc_ba_count = count;

	for (i = 0; i < length; i += 2) {
		seq->brightness = arr_32[i];
		seq->alpha = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

extern void oplus_dsi_display_change_te_irq_status(void *disp, bool enable);

int oplus_dsi_display_enable_and_waiting_for_next_te_irq(void)
{
	int const switch_te_timeout = msecs_to_jiffies(18);
	struct dsi_display *display = get_main_display();
	SDE_ATRACE_BEGIN("wait_te_irq");
	/* enable te irq */

	if (display->panel->cur_mode->timing.refresh_rate == 60) {
		msleep(9);
	} else if (display->panel->cur_mode->timing.refresh_rate == 90) {
		msleep(11);
	}

	oplus_dsi_display_change_te_irq_status(display, true);
	pr_info("Waiting for the next TE to switch\n");

	display->vsync_switch_pending = true;
	reinit_completion(&display->switch_te_gate);

	if (!wait_for_completion_timeout(&display->switch_te_gate, switch_te_timeout)) {
		DSI_ERR("hbm vsync switch TE check failed\n");
		oplus_dsi_display_change_te_irq_status(display, false);
		return -EINVAL;
	}
	/* disable te irq */
	oplus_dsi_display_change_te_irq_status(display, false);
	SDE_ATRACE_END("wait_te_irq");

	return 0;
}

int sde_connector_update_backlight(struct drm_connector *connector, bool post)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct dsi_display *dsi_display;

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	dsi_display = c_conn->display;
	if (!dsi_display || !dsi_display->panel || !dsi_display->panel->cur_mode) {
		SDE_ERROR("Invalid params(s) dsi_display %pK, panel %pK\n",
				dsi_display,
				((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	if (!connector->state || !connector->state->crtc)
		return 0;

	if (oplus_dimlayer_bl != oplus_dimlayer_bl_enabled) {
		struct sde_connector *c_conn = to_sde_connector(connector);
		struct drm_crtc *crtc = connector->state->crtc;
		struct dsi_panel *panel = dsi_display->panel;
		int bl_lvl = dsi_display->panel->bl_config.bl_level;
		u32 current_vblank;
		int on_vblank = 0;
		int off_vblank = 0;
		int vblank = 0;
		int ret = 0;
		int vblank_get = -EINVAL;
		int on_delay = 0, on_delay_after = 0;
		int off_delay = 0, off_delay_after = 0;
		int delay = 0, delay_after = 0;
		int rc = 0;

		if (sde_crtc_get_fingerprint_mode(crtc->state)) {
			oplus_dimlayer_bl_enabled = oplus_dimlayer_bl;
			goto done;
		}

		if (oplus_dimlayer_bl) {
			mutex_lock(&dsi_display->panel->panel_lock);

			if (!dsi_display->panel->panel_initialized) {
				pr_err("panel not initialized, failed to set mca\n");
				mutex_unlock(&dsi_display->panel->panel_lock);
				return 0;
			}
			dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
					     DSI_CORE_CLK, DSI_CLK_ON);

			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_MCA_OFF);
			if (rc) {
				pr_err("DC on failed to send DSI_CMD_MCA_OFF cmds, rc=%d\n", rc);
			}

			dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
					     DSI_CORE_CLK, DSI_CLK_OFF);

			mutex_unlock(&dsi_display->panel->panel_lock);
		} else {
			mutex_lock(&dsi_display->panel->panel_lock);

			if (!dsi_display->panel->panel_initialized) {
				pr_err("panel not initialized, failed to set mca\n");
				mutex_unlock(&dsi_display->panel->panel_lock);
				return 0;
			}
			dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
					     DSI_CORE_CLK, DSI_CLK_ON);

			if (mca_mode == 0) {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_MCA_OFF);
				if (rc) {
					pr_err("DC off failed to send DSI_CMD_MCA_OFF cmds, rc=%d\n", rc);
				}
			} else if (mca_mode == 1) {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_MCA_ON);
				if (rc) {
					pr_err("DC off failed to send DSI_CMD_MCA_ON cmds, rc=%d\n", rc);
				}
			}

			dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
					     DSI_CORE_CLK, DSI_CLK_OFF);

			mutex_unlock(&dsi_display->panel->panel_lock);
		}

		if (panel->cur_mode->timing.refresh_rate == 120) {
			if (bl_lvl < 103) {
				on_vblank = 0;
				off_vblank = 2;
			} else {
				on_vblank = 0;
				off_vblank = 0;
			}
		} else {
			if (bl_lvl < 103) {
				on_vblank = -1;
				off_vblank = 1;
				on_delay = 11000;
			} else {
				on_vblank = -1;
				off_vblank = -1;
				on_delay = 11000;
				off_delay = 11000;
			}
		}

		if (oplus_dimlayer_bl_on_vblank != INT_MAX)
			on_vblank = oplus_dimlayer_bl_on_vblank;

		if (oplus_dimlayer_bl_off_vblank != INT_MAX)
			off_vblank = oplus_dimlayer_bl_off_vblank;


		if (oplus_dimlayer_bl) {
			vblank = on_vblank;
			delay = on_delay;
			delay_after = on_delay_after;
		} else {
			vblank = off_vblank;
			delay = off_delay;
			delay_after = off_delay_after;
		}

		if (oplus_dimlayer_bl_delay >= 0)
			delay = oplus_dimlayer_bl_delay;

		if (oplus_dimlayer_bl_delay_after >= 0)
			delay_after = oplus_dimlayer_bl_delay_after;

		vblank_get = drm_crtc_vblank_get(crtc);
		if (vblank >= 0) {
			if (!post) {
				oplus_dimlayer_bl_enabled = oplus_dimlayer_bl;
				current_vblank = drm_crtc_vblank_count(crtc);
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						current_vblank != drm_crtc_vblank_count(crtc),
						msecs_to_jiffies(34));
				current_vblank = drm_crtc_vblank_count(crtc) + vblank;
				if (delay > 0)
					usleep_range(delay, delay + 100);
				_sde_connector_update_bl_scale_(c_conn);
				if (delay_after)
					usleep_range(delay_after, delay_after + 100);
				if (vblank > 0) {
					ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
							current_vblank == drm_crtc_vblank_count(crtc),
							msecs_to_jiffies(17 * 3));
				}
			}
		} else {
			if (!post) {
				current_vblank = drm_crtc_vblank_count(crtc);
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						current_vblank != drm_crtc_vblank_count(crtc),
						msecs_to_jiffies(34));
			} else {
				if (vblank < -1) {
					current_vblank = drm_crtc_vblank_count(crtc) + 1 - vblank;
					ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
							current_vblank == drm_crtc_vblank_count(crtc),
							msecs_to_jiffies(17 * 3));
				}
				oplus_dimlayer_bl_enabled = oplus_dimlayer_bl;

				if (delay > 0)
					usleep_range(delay, delay + 100);
				_sde_connector_update_bl_scale_(c_conn);
				if (delay_after)
					usleep_range(delay_after, delay_after + 100);
			}
		}
		if (!vblank_get)
			drm_crtc_vblank_put(crtc);
	}

	if (oplus_dimlayer_bl_enable_v2 != oplus_dimlayer_bl_enable_v2_real) {
		struct sde_connector *c_conn = to_sde_connector(connector);

		oplus_dimlayer_bl_enable_v2_real = oplus_dimlayer_bl_enable_v2;
		_sde_connector_update_bl_scale_(c_conn);
	}

	if (oplus_dimlayer_bl_enable_v3 != oplus_dimlayer_bl_enable_v3_real) {
		struct sde_connector *c_conn = to_sde_connector(connector);

		if (oplus_datadimming_v3_skip_frame > 0) {
			oplus_datadimming_v3_skip_frame--;
		} else {
			oplus_dimlayer_bl_enable_v3_real = oplus_dimlayer_bl_enable_v3;
			_sde_connector_update_bl_scale_(c_conn);
			oplus_datadimming_v3_skip_frame = 2;
		}
	}

done:
	if (post) {
		if (oplus_datadimming_vblank_count> 0) {
			oplus_datadimming_vblank_count--;
		} else {
			while (atomic_read(&oplus_datadimming_vblank_ref) > 0) {
				drm_crtc_vblank_put(connector->state->crtc);
				atomic_dec(&oplus_datadimming_vblank_ref);
			}
		}
	}

	return 0;
}

int fingerprint_wait_vsync(struct drm_encoder *drm_enc, struct dsi_panel *panel)
{
	SDE_ATRACE_BEGIN("wait_vsync");

	if (!drm_enc || !drm_enc->crtc || !panel) {
		SDE_ERROR("%s encoder is disabled", __func__);
		return -ENOLINK;
	}

	if (sde_encoder_is_disabled(drm_enc)) {
		SDE_ERROR("%s encoder is disabled", __func__);
		return -EIO;
	}

	mutex_unlock(&panel->panel_lock);
	sde_encoder_wait_for_event(drm_enc,  MSM_ENC_VBLANK);
	mutex_lock(&panel->panel_lock);
	SDE_ATRACE_END("wait_vsync");

	return 0;
}

static int sde_connector_update_hbm_enter(struct drm_connector *connector, struct dsi_display *dsi_display, unsigned int fps_period_us)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct drm_crtc *crtc = c_conn->encoder->crtc;
	struct dsi_panel *panel = dsi_display->panel;
	int vblank = 0;
	u32 target_vblank, current_vblank;
	int rc = 0;
	int ret;

	if (!strcmp(panel->oplus_priv.vendor_name, "AMB655XL08")) {
		int hima_hbm_mode = 1;
		lcdinfo_notify(1, &hima_hbm_mode);
	}
#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (!dsi_display->panel->oplus_priv.is_aod_ramless || oplus_display_mode) {
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
		mutex_lock(&dsi_display->panel->panel_lock);

		if (!dsi_display->panel->panel_initialized) {
			dsi_display->panel->is_hbm_enabled = false;
			pr_err("panel not initialized, failed to Enter OnscreenFingerprint\n");
			mutex_unlock(&dsi_display->panel->panel_lock);
			return 0;
		}
		dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_ON);

		if (oplus_seed_backlight) {
			int frame_time_us;

			frame_time_us = mult_frac(1000, 1000, panel->cur_mode->timing.refresh_rate);
			oplus_panel_process_dimming_v2(panel, panel->bl_config.bl_level, true);
			if (is_skip_panel_dc_set_brightness(panel->oplus_priv.vendor_name)) {
				mipi_dsi_dcs_set_display_brightness(&panel->mipi_device, panel->bl_config.bl_level);
				oplus_panel_process_dimming_v2_post(panel, true);
			}
			usleep_range(frame_time_us, frame_time_us + 100);
		}

		if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "ANA6706")) {
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_MCA_OFF);
			if (rc) {
				pr_err("FP on failed to send DSI_CMD_MCA_OFF cmds, rc=%d\n", rc);
			}
		}
#ifdef OPLUS_FEATURE_AOD_RAMLESS
		else if (dsi_display->panel->oplus_priv.is_aod_ramless) {
			ktime_t delta = ktime_sub(ktime_get(), oplus_backlight_time);
			s64 delta_us = ktime_to_us(delta);
			if (delta_us < 34000 && delta_us >= 0)
				usleep_range(34000 - delta_us, 34000 - delta_us + 100);
		}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

		/* Add for driver control loading at FOD sence */
		if ((dsi_display->panel->oplus_priv.is_oplus_project) &&
			(!strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X"))) {
			dsi_panel_seed_mode(dsi_display->panel, PANEL_LOADING_EFFECT_OFF);
		}

		if (OPLUS_DISPLAY_AOD_SCENE != get_oplus_display_scene() &&
				dsi_display->panel->bl_config.bl_level) {              /* NO OPLUS_DISPLAY_AOD_SCENE */
			if (strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X")) {
				if (dsi_display->config.panel_mode != DSI_OP_VIDEO_MODE) {
					if (!strcmp(dsi_display->panel->name, "samsung ams662zs01 dsc cmd 21623")) {
						current_vblank = drm_crtc_vblank_count(crtc);
					} else {
						current_vblank = drm_crtc_vblank_count(crtc);
					}
						if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3")) {
							SDE_ATRACE_BEGIN("wait_vblank");
							if (dsi_display->panel->cur_mode->timing.refresh_rate == 60) {
								ret = fingerprint_wait_vsync(c_conn->encoder, dsi_display->panel);
							} else {
								ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
									current_vblank != drm_crtc_vblank_count(crtc),
									usecs_to_jiffies(fps_period_us));
							}
							SDE_ATRACE_END("wait_vblank");
						}
						else if (!strcmp(dsi_display->panel->name, "samsung ams662zs01 dsc cmd 21623")) {
							ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
								current_vblank != drm_crtc_vblank_count(crtc),
								msecs_to_jiffies(17));
						} else {
							ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
								current_vblank != drm_crtc_vblank_count(crtc),
								msecs_to_jiffies(17));
						}
						if (!ret) {
							pr_err("fp enter:wait sync vblank timeout target_vblank=%d current_vblank=%d\n",
									current_vblank, drm_crtc_vblank_count(crtc));
						}
				}
			}

			if (!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01") ||
				!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01IN20057")) {
				usleep_range(5500, 5600);
			}

			if (is_support_panel_hbm_enter_send_hbm_on_cmd(dsi_display->panel->oplus_priv.vendor_name)) {
				if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X")) {
					ret = fingerprint_wait_vsync(c_conn->encoder, dsi_display->panel);
					if (!ret) {
							pr_err("fp enter:wait sync vblank timeout target_vblank=%d current_vblank=%d\n",
									current_vblank, drm_crtc_vblank_count(crtc));
						}
				}
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_ON);
			} else {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
			}

			vblank = panel->cur_mode->priv_info->fod_on_vblank;
			target_vblank = drm_crtc_vblank_count(crtc) + vblank;

			if (strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X")) {
				if (vblank) {
					if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3")) {
						ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
								target_vblank <= drm_crtc_vblank_count(crtc),
								usecs_to_jiffies((vblank + 1) * fps_period_us));
					} else {
						ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
								target_vblank == drm_crtc_vblank_count(crtc),
								msecs_to_jiffies((vblank + 1) * 17));
					}
					if (!ret) {
						pr_err("OnscreenFingerprint failed to wait vblank timeout target_vblank=%d current_vblank=%d\n",
								target_vblank, drm_crtc_vblank_count(crtc));
					}
				}
			}
		} else {                    /* OPLUS_DISPLAY_AOD_SCENE */
			if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3") ||
				((!strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X")) &&
				(!dsi_display->panel->oplus_priv.aod_on_fod_off)) ||
				panel->oplus_priv.oplus_fp_hbm_config_flag) {
					if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X")) {
						ret = fingerprint_wait_vsync(c_conn->encoder, dsi_display->panel);
						if (!ret) {
							pr_err("fp enter:wait sync vblank timeout target_vblank=%d current_vblank=%d\n",
									current_vblank, drm_crtc_vblank_count(crtc));
						}
						rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
					} else {
						rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_ON);
					}
			} else {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
				if ((dsi_display->panel->oplus_priv.is_oplus_project) &&
					(!strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X"))) {
						set_oplus_display_scene(OPLUS_DISPLAY_AOD_HBM_SCENE);
				}
			}
		}

		dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);

		mutex_unlock(&dsi_display->panel->panel_lock);
		if (rc) {
			pr_err("failed to send DSI_CMD_HBM_ON cmds, rc=%d\n", rc);
			return rc;
		}
#ifdef OPLUS_FEATURE_AOD_RAMLESS
	}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
	return rc;
}

static int sde_connector_update_hbm_exit(struct drm_connector *connector, struct dsi_display *dsi_display, unsigned int fps_period_us)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct drm_crtc *crtc = c_conn->encoder->crtc;
	struct dsi_panel *panel = dsi_display->panel;
	int vblank = 0;
	u32 target_vblank, current_vblank;
	int rc = 0;
	int ret;

	if (!strcmp(panel->oplus_priv.vendor_name, "AMB655XL08")) {
		int hima_hbm_mode = 0;
		lcdinfo_notify(1, &hima_hbm_mode);
	}
	mutex_lock(&dsi_display->panel->panel_lock);

	if (!dsi_display->panel->panel_initialized) {
		dsi_display->panel->is_hbm_enabled = true;
		pr_err("panel not initialized, failed to Exit OnscreenFingerprint\n");
		mutex_unlock(&dsi_display->panel->panel_lock);
		return 0;
	}

	current_vblank = drm_crtc_vblank_count(crtc);

	if (oplus_panel_hbm_exit_check_wait_vblank(dsi_display->panel->oplus_priv.vendor_name)) {
		if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3")) {
			ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
					current_vblank != drm_crtc_vblank_count(crtc),
					usecs_to_jiffies(fps_period_us));
		} else {
			ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
					current_vblank != drm_crtc_vblank_count(crtc),
					msecs_to_jiffies(17));
		}
	}

	if (strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3")
		&& strcmp(dsi_display->panel->name, "samsung ams662zs01 dsc cmd 21623")) {
		oplus_skip_datadimming_sync = true;
		oplus_panel_update_backlight_unlock(panel);
		oplus_skip_datadimming_sync = false;
	}

	vblank = panel->cur_mode->priv_info->fod_off_vblank;
	target_vblank = drm_crtc_vblank_count(crtc) + vblank;

	dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			     DSI_CORE_CLK, DSI_CLK_ON);

	if (dsi_display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		panel->oplus_priv.skip_mipi_last_cmd = true;
	}

	if (dsi_display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		panel->oplus_priv.skip_mipi_last_cmd = false;
	}

	if (OPLUS_DISPLAY_AOD_HBM_SCENE == get_oplus_display_scene()) {              /* OPLUS_DISPLAY_AOD_HBM_SCENE */
		if (OPLUS_DISPLAY_POWER_DOZE_SUSPEND == get_oplus_display_power_status() ||
				OPLUS_DISPLAY_POWER_DOZE == get_oplus_display_power_status()) {
			if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3") && (panel->panel_id2 >= 5)) {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF_PVT);
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_AOR_RESTORE);
			} else {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF);
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_AOR_RESTORE);
			}
			oplus_update_aod_light_mode_unlock(panel);
			set_oplus_display_scene(OPLUS_DISPLAY_AOD_SCENE);
		} else {
			if ((!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3")) ||
				(!strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB670YF01")) ||
				(!strcmp(panel->oplus_priv.vendor_name, "AMB655X") && (dsi_display->panel->oplus_priv.is_oplus_project))) {
				if (!sde_crtc_get_fingerprint_mode(connector->state->crtc->state)) {
					dsi_panel_seed_mode(dsi_display->panel, seed_mode);
					dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_AOR_RESTORE);
				}
			}
			if (is_support_panel_hbm_enter_send_hbm_off_cmd(dsi_display)) {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_OFF);
			} else {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_SET_NOLP);
			}
			oplus_panel_update_backlight_unlock(dsi_display->panel);
			set_oplus_display_scene(OPLUS_DISPLAY_NORMAL_SCENE);
			/* set nolp would exit hbm, restore when panel status on hbm */
			if (panel->bl_config.bl_level > panel->bl_config.bl_normal_max_level) {
				if (!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01") ||
					!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01IN20057") ||
					!strcmp(panel->name, "s6e3fc3_fhd_oled_cmd_samsung")) {
					if (!strcmp(panel->name, "samsung ams643ye01 in 20127 amoled fhd+ panel")) {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER1_SWITCH);
						oplus_dsi_display_enable_and_waiting_for_next_te_irq();
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER2_SWITCH);
					} else {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
					}
				}
				oplus_panel_update_backlight_unlock(panel);
			}

			if (oplus_display_get_hbm_mode()) {
				if (!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3") && (panel->panel_id2 >= 5)) {
					rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON_PVT);
				} else {
					rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
				}
			}
		}
	} else if (oplus_display_get_hbm_mode()) {    /* When hbm_mode is true */
		/* Do nothing to skip hbm off */
	} else if (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {            /* OPLUS_DISPLAY_AOD_SCENE */
		if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3") && (panel->panel_id2 >= 5)) {
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF_PVT);
		} else {
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF);
		}
		oplus_update_aod_light_mode_unlock(panel);
	} else {                                                                      /* Others SCENE */
		if ((!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3")) ||
			(!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01")) ||
			(!strcmp(panel->oplus_priv.vendor_name, "AMB655UV01") && (dsi_display->panel->oplus_priv.is_oplus_project)) ||
			(!strcmp(panel->oplus_priv.vendor_name, "ANA6706") && (dsi_display->panel->oplus_priv.is_oplus_project)) ||
			(!strcmp(panel->oplus_priv.vendor_name, "AMB655X") && (dsi_display->panel->oplus_priv.is_oplus_project))) {
			if (!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3")) {
				fingerprint_wait_vsync(c_conn->encoder, dsi_display->panel);
			}
			dsi_panel_seed_mode(dsi_display->panel, seed_mode);
			dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_AOR_RESTORE);
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_OFF);
			oplus_panel_update_backlight_unlock(panel);
		} else if (oplus_panel_support_exit_global_hbm(panel)) {
			oplus_panel_update_backlight_unlock(panel);
		} else {
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_OFF);
			if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655XL08")) {
				oplus_panel_update_backlight_unlock(panel);
			}
			if (!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01") ||
				!strcmp(panel->oplus_priv.vendor_name, "AMS643YE01IN20057") ||
				!strcmp(panel->name, "s6e3fc3_fhd_oled_cmd_samsung") ||
				!strcmp(panel->oplus_priv.vendor_name, "SOFE03F")) {
				if(panel->bl_config.bl_level > panel->bl_config.brightness_normal_max_level) {
					if (!strcmp(panel->name, "samsung ams643ye01 in 20127 amoled fhd+ panel")) {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER1_SWITCH);
						oplus_dsi_display_enable_and_waiting_for_next_te_irq();
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER2_SWITCH);
					} else {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH);
					}
				}
				oplus_panel_update_backlight_unlock(panel);
			}
		}
	}

	if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "ANA6706")) {
		if (mca_mode == 0) {
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_MCA_OFF);
			if (rc) {
				pr_err("FP off failed to send DSI_CMD_MCA_OFF cmds, rc=%d\n", rc);
			}
		} else if (mca_mode == 1) {
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_MCA_ON);
			if (rc) {
				pr_err("FP off failed to send DSI_CMD_MCA_ON cmds, rc=%d\n", rc);
			}
		}
	}

	dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			     DSI_CORE_CLK, DSI_CLK_OFF);
	mutex_unlock(&dsi_display->panel->panel_lock);
	if (strcmp(dsi_display->panel->oplus_priv.vendor_name, "AMB655X")) {
		if (vblank) {
			if (!strcmp(dsi_display->panel->oplus_priv.vendor_name, "S6E3HC3")) {
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						target_vblank <= drm_crtc_vblank_count(crtc),
						usecs_to_jiffies((vblank + 1) * fps_period_us));
			} else {
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						target_vblank == drm_crtc_vblank_count(crtc),
						msecs_to_jiffies((vblank + 1) * 17));
			}
			if (!ret) {
				pr_err("OnscreenFingerprint failed to wait vblank timeout target_vblank=%d current_vblank=%d\n",
						target_vblank, drm_crtc_vblank_count(crtc));
			}
		}
	}
	return rc;
}

extern struct dc_apollo_pcc_sync dc_apollo;

int sde_connector_update_hbm(struct drm_connector *connector)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct dsi_display *dsi_display;
	struct sde_connector_state *c_state;
	int rc = 0;
	int fingerprint_mode;

	if (!c_conn) {
		SDE_ERROR("Invalid params sde_connector null\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	c_state = to_sde_connector_state(connector->state);

	dsi_display = c_conn->display;
	if (!dsi_display || !dsi_display->panel) {
		SDE_ERROR("Invalid params(s) dsi_display %pK, panel %pK\n",
			dsi_display,
			((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	if (!c_conn->encoder || !c_conn->encoder->crtc ||
	    !c_conn->encoder->crtc->state) {
		return 0;
	}

	fingerprint_mode = sde_crtc_get_fingerprint_mode(c_conn->encoder->crtc->state);

	if (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {
		if (sde_crtc_get_fingerprint_pressed(c_conn->encoder->crtc->state)) {
			sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, true);
		} else {
			sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, false);
			fingerprint_mode = false;
		}
	} else {
		sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, false);
	}

	if (fingerprint_mode != dsi_display->panel->is_hbm_enabled) {
		struct dsi_panel *panel = dsi_display->panel;
		char fingerprint_name[60];
		unsigned int fps_period_us =
			1000000/dsi_display->modes->timing.refresh_rate + 1;

		if (oplus_fod_on_vblank >= 0)
			panel->cur_mode->priv_info->fod_on_vblank = oplus_fod_on_vblank;
		if (oplus_fod_off_vblank >= 0)
			panel->cur_mode->priv_info->fod_off_vblank = oplus_fod_off_vblank;

		pr_err("OnscreenFingerprint mode: %s",
		       fingerprint_mode ? "Enter" : "Exit");
		sprintf(fingerprint_name, "OnscreenFingerprint_%s_%d",
			fingerprint_mode ? "Enter" : "Exit", fps_period_us);
		dsi_display->panel->is_hbm_enabled = fingerprint_mode;

		if (dsi_display->panel->oplus_priv.dc_apollo_sync_enable && dsi_display->panel->is_hbm_enabled) {
			dc_apollo.pcc_last = dsi_display->panel->oplus_priv.dc_apollo_sync_brightness_level_pcc;
		}

		SDE_ATRACE_BEGIN(fingerprint_name);
		if (fingerprint_mode) {
			rc = sde_connector_update_hbm_enter(connector, dsi_display, fps_period_us);
			if (rc) {
				pr_err("failed to sde_connector_update_hbm_enter, rc=%d\n", rc);
			}
		} else {
			rc = sde_connector_update_hbm_exit(connector, dsi_display, fps_period_us);
			if (rc) {
				pr_err("failed to sde_connector_update_hbm_exit, rc=%d\n", rc);
			}
		}
		SDE_ATRACE_END(fingerprint_name);
	}

	if (oplus_dimlayer_hbm_vblank_count > 0) {
		oplus_dimlayer_hbm_vblank_count--;
	} else {
		while (atomic_read(&oplus_dimlayer_hbm_vblank_ref) > 0) {
			drm_crtc_vblank_put(connector->state->crtc);
			atomic_dec(&oplus_dimlayer_hbm_vblank_ref);
		}
	}

	return 0;
}

int oplus_seed_bright_to_alpha(int brightness)
{
	struct dsi_display *display = get_main_display();
	struct oplus_brightness_alpha *lut = NULL;
	int count = 0;
	int i = 0;
	int alpha;

	if (!display)
		return 0;

	if (oplus_panel_alpha)
		return oplus_panel_alpha;

	if (display->panel->dc_ba_seq && display->panel->dc_ba_count) {
		count = display->panel->dc_ba_count;
		lut = display->panel->dc_ba_seq;
	} else {
		count = ARRAY_SIZE(brightness_seed_alpha_lut_dc);
		lut = brightness_seed_alpha_lut_dc;
	}

	for (i = 0; i < count; i++) {
		if (lut[i].brightness >= brightness)
			break;
	}

	if (i == 0)
		alpha = lut[0].alpha;
	else if (i == count)
		alpha = lut[count - 1].alpha;
	else if (!strcmp(display->panel->oplus_priv.vendor_name, "samsung_AMS678UW01")) {
		alpha = interpolate(brightness, lut[i-1].brightness,
				    lut[i].brightness, lut[i-1].alpha,
				    lut[i].alpha, false);
	} else {
		alpha = interpolate(brightness, lut[i-1].brightness,
				    lut[i].brightness, lut[i-1].alpha,
				    lut[i].alpha, display->panel->oplus_priv.bl_interpolate_nosub);
	}
	return alpha;
}

struct dsi_panel_cmd_set *
oplus_dsi_update_seed_backlight(struct dsi_panel *panel, int brightness,
				enum dsi_cmd_set_type type)
{
	enum dsi_cmd_set_state state;
	struct dsi_cmd_desc *cmds;
	struct dsi_cmd_desc *oplus_cmd;
	u8 *tx_buf;
	int count, rc = 0;
	int i = 0;
	int k = 0;
	int alpha = oplus_seed_bright_to_alpha(brightness);

	if (type != DSI_CMD_SEED_MODE0 &&
		type != DSI_CMD_SEED_MODE1 &&
		type != DSI_CMD_SEED_MODE2 &&
		type != DSI_CMD_SEED_MODE3 &&
		type != DSI_CMD_SEED_MODE4 &&
		type != DSI_CMD_SEED_MODE0_SWITCH &&
		type != DSI_CMD_SEED_MODE1_SWITCH &&
		type != DSI_CMD_SEED_MODE2_SWITCH &&
		type != DSI_CMD_SEED_MODE0_DC_SWITCH &&
		type != DSI_CMD_SEED_MODE1_DC_SWITCH &&
		type != DSI_CMD_SEED_MODE2_DC_SWITCH &&
		type != DSI_CMD_SEED_MODE0_DC &&
		type != DSI_CMD_SEED_MODE1_DC &&
		type != DSI_CMD_SEED_MODE2_DC &&
		type != DSI_CMD_SEED_OFF) {
		return NULL;
	}

	if (type == DSI_CMD_SEED_OFF)
		type = DSI_CMD_SEED_MODE0;

	cmds = panel->cur_mode->priv_info->cmd_sets[type].cmds;
	count = panel->cur_mode->priv_info->cmd_sets[type].count;
	state = panel->cur_mode->priv_info->cmd_sets[type].state;

	oplus_cmd = kmemdup(cmds, sizeof(*cmds) * count, GFP_KERNEL);
	if (!oplus_cmd) {
		rc = -ENOMEM;
		goto error;
	}

	for (i = 0; i < count; i++)
		oplus_cmd[i].msg.tx_buf = NULL;

	for (i = 0; i < count; i++) {
		u32 size;

		size = oplus_cmd[i].msg.tx_len * sizeof(u8);

		oplus_cmd[i].msg.tx_buf = kmemdup(cmds[i].msg.tx_buf, size, GFP_KERNEL);
		if (!oplus_cmd[i].msg.tx_buf) {
			rc = -ENOMEM;
			goto error;
		}
	}

	for (i = 0; i < count; i++) {
		if (oplus_cmd[i].msg.tx_len != 0x16)
			continue;
		tx_buf = (u8 *)oplus_cmd[i].msg.tx_buf;
		for (k = 0; k < oplus_cmd[i].msg.tx_len; k++) {
			if (k == 0) {
				continue;
			}
			tx_buf[k] = tx_buf[k] * (255 - alpha) / 255;
		}
	}

	if (oplus_priv_seed_cmd_set.cmds) {
		for (i = 0; i < oplus_priv_seed_cmd_set.count; i++)
			kfree(oplus_priv_seed_cmd_set.cmds[i].msg.tx_buf);
		kfree(oplus_priv_seed_cmd_set.cmds);
	}

	oplus_priv_seed_cmd_set.cmds = oplus_cmd;
	oplus_priv_seed_cmd_set.count = count;
	oplus_priv_seed_cmd_set.state = state;
	oplus_dc2_alpha = alpha;

	return &oplus_priv_seed_cmd_set;

error:
	if (oplus_cmd) {
		for (i = 0; i < count; i++)
			kfree(oplus_cmd[i].msg.tx_buf);
		kfree(oplus_cmd);
	}
	return ERR_PTR(rc);
}

int oplus_display_panel_get_dim_alpha(void *buf)
{
	unsigned int* temp_alpha = buf;
	struct dsi_display *display = get_main_display();

	if (!display->panel->is_hbm_enabled ||
		(get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON)) {
		(*temp_alpha) = 0;
		return 0;
	}

	(*temp_alpha) = oplus_underbrightness_alpha;
	return 0;
}

int oplus_display_panel_set_dim_alpha(void *buf)
{
	unsigned int* temp_alpha = buf;

	(*temp_alpha) = oplus_panel_alpha;

	return 0;
}

int oplus_display_panel_get_dim_dc_alpha(void *buf)
{
	int ret = 0;
	unsigned int* temp_dim_alpha = buf;
	struct dsi_display *display = get_main_display();

	if (!display || !display->panel) {
		pr_err("%s main display is NULL\n", __func__);
		(*temp_dim_alpha) = 0;
		return 0;
	}

	if (display->panel->is_hbm_enabled ||
		get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) {
		ret = 0;
	}
	if (oplus_dc2_alpha != 0) {
		ret = oplus_dc2_alpha;
	} else if (oplus_underbrightness_alpha != 0) {
		ret = 0;
	} else if (oplus_dimlayer_bl_enable_v3_real) {
		ret = 1;
	}

	(*temp_dim_alpha) = ret;
	return 0;
}

int oplus_display_panel_set_dimlayer_enable(void *data)
{
	struct dsi_display *display = NULL;
	struct drm_connector *dsi_connector = NULL;
	uint32_t *dimlayer_enable = data;

	display = get_main_display();
	if (!display) {
		return -EINVAL;
	}

	if (!strcmp(display->panel->name, "samsung ams662zs01 dsc cmd 21623")) {
		dc_apollo_enable = *dimlayer_enable;
		pr_info("DC BKL %s\n", *dimlayer_enable?"ON":"OFF");
		return 0;
	}

	dsi_connector = display->drm_conn;
	if (display && display->name) {
		int enable = (*dimlayer_enable);
		int err = 0;

		mutex_lock(&display->display_lock);
		if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
			pr_err("[%s]: display not ready\n", __func__);
		} else {
			err = drm_crtc_vblank_get(dsi_connector->state->crtc);
			if (err) {
				pr_err("failed to get crtc vblank, error=%d\n", err);
			} else {
				/* do vblank put after 7 frames */
				oplus_datadimming_vblank_count = 7;
				atomic_inc(&oplus_datadimming_vblank_ref);
			}
		}

		usleep_range(17000, 17100);
		if (!strcmp(display->panel->oplus_priv.vendor_name, "ANA6706")) {
			oplus_dimlayer_bl_enable = enable;
		} else {
			if (!strcmp(display->panel->name, "boe nt37800 amoled fhd+ panel with DSC"))
				oplus_dimlayer_bl_enable_v3 = enable;
			else
				oplus_dimlayer_bl_enable_v2 = enable;
		}
		mutex_unlock(&display->display_lock);
	}

	return 0;
}

int oplus_display_panel_get_dimlayer_enable(void *data)
{
	uint32_t *dimlayer_bl_enable = data;

	(*dimlayer_bl_enable) = oplus_dimlayer_bl_enable_v2;

	return 0;
}

