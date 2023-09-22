/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
** OPLUS_BUG_STABILITY
** File : oplus_display_panel_seed.c
** Description : oplus display panel seed feature
** Version : 1.0
** Date : 2020/06/13
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#include "oplus_display_panel_seed.h"
#include "oplus_dsi_support.h"
#include "oplus_display_private_api.h"

extern unsigned int is_project(int project);
extern bool oplus_dc_v2_on;
static bool seed_mode_flag = false;

int seed_mode = 0;
DEFINE_MUTEX(oplus_seed_lock);

int oplus_display_get_seed_mode(void)
{
	return seed_mode;
}

int __oplus_display_set_seed(int mode) {
	struct dsi_display *display = get_main_display();

	mutex_lock(&oplus_seed_lock);
	if (is_support_panel_dc_seed_mode_flag(display->panel->oplus_priv.vendor_name)) {
		if ((display->panel->bl_config.bl_lvl_backup == 0) && (oplus_dc_v2_on == false) && (seed_mode == OPLUS_SEED_MODE1)) {
			seed_mode_flag = true;
		}
	}
	if(mode != seed_mode) {
		seed_mode = mode;
	}
	mutex_unlock(&oplus_seed_lock);
	return 0;
}

int dsi_panel_seed_mode_unlock(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!dsi_panel_initialized(panel))
		return -EINVAL;

	if (oplus_dc_v2_on) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_ENTER);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_ENTER cmds, rc=%d\n",
				panel->name, rc);
		}
		if (is_support_panel_dc_seed_mode_flag(panel->oplus_priv.vendor_name)) {
			if (seed_mode_flag == true) {
				seed_mode = OPLUS_SEED_MODE1;
				mode = OPLUS_SEED_MODE1;
				seed_mode_flag = false;
			}
		}
	} else {
			int frame_time_us = mult_frac(1000, 1000, panel->cur_mode->timing.refresh_rate);
			rc = is_support_panel_dc_exit_backlight_select(panel, frame_time_us);
			if (rc) {
				pr_err("[%s] failed to is_support_panel_dc_exit_backlight_select, rc=%d\n", rc);
			}
	}

	switch (mode) {
	case OPLUS_SEED_MODE0:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE0);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE0 cmds, rc=%d\n",
					panel->name, rc);
		}
		break;
	case OPLUS_SEED_MODE1:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE1);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE1 cmds, rc=%d\n",
					panel->name, rc);
		}
		break;
	case OPLUS_SEED_MODE2:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE2);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE2 cmds, rc=%d\n",
					panel->name, rc);
		}
		break;
	case OPLUS_SEED_MODE3:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE3);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE3 cmds, rc=%d\n",
					panel->name, rc);
		}
		break;
	case OPLUS_SEED_MODE4:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE4);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE4 cmds, rc=%d\n",
					panel->name, rc);
		}
		break;
	default:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_OFF);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_OFF cmds, rc=%d\n",
					panel->name, rc);
		}
		pr_err("[%s] seed mode Invalid %d\n",
			panel->name, mode);
	}

	if (!oplus_dc_v2_on) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_EXIT);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_EXIT cmds, rc=%d\n",
				panel->name, rc);
		}
	}

	return rc;
}

int dsi_panel_loading_effect_mode_unlock(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!dsi_panel_initialized(panel)) {
		return -EINVAL;
	}

	switch (mode) {
	case PANEL_LOADING_EFFECT_MODE1:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_MODE1);

		if (rc) {
			pr_err("[%s] failed to send PANEL_LOADING_EFFECT_MODE1 cmds, rc=%d\n",
			       panel->name, rc);
		}

		break;

	case PANEL_LOADING_EFFECT_MODE2:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_MODE2);

		if (rc) {
			pr_err("[%s] failed to send PANEL_LOADING_EFFECT_MODE2 cmds, rc=%d\n",
			       panel->name, rc);
		}

		break;

	case PANEL_LOADING_EFFECT_OFF:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_OFF);

		if (rc) {
			pr_err("[%s] failed to send PANEL_LOADING_EFFECT_OFF cmds, rc=%d\n",
			       panel->name, rc);
		}

		break;

	default:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_OFF);

		if (rc) {
			pr_err("[%s] failed to send PANEL_LOADING_EFFECT_OFF cmds, rc=%d\n",
			       panel->name, rc);
		}

		pr_err("[%s] loading effect mode Invalid %d\n",
		       panel->name, mode);
	}

	return rc;
}

int dsi_panel_dc_seed_mode_unlock(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!dsi_panel_initialized(panel))
		return -EINVAL;

	if (oplus_dc_v2_on) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_ENTER);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_ENTER cmds, rc=%d\n",
				panel->name, rc);
		}
	} else {
		int frame_time_us = mult_frac(1000, 1000, panel->cur_mode->timing.refresh_rate);
		rc = is_support_panel_dc_exit_backlight_select(panel, frame_time_us);
		if (rc) {
			pr_err("[%s] failed to is_support_panel_dc_exit_backlight_select, rc=%d\n", rc);
		}
	}

	switch (mode) {
	case OPLUS_SEED_MODE0:
		if ((panel->is_dc_set_color_mode) && (!panel->is_hbm_enabled) && (oplus_dc_v2_on)) {
			panel->is_dc_set_color_mode = false;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE0_DC_SWITCH);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE0_DC_SWITCH cmds, rc=%d\n",
						panel->name, rc);
			}
		} else if ((panel->is_dc_set_color_mode) && (!panel->is_hbm_enabled) && (!oplus_dc_v2_on)) {
			panel->is_dc_set_color_mode = false;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE0_SWITCH);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE0_SWITCH cmds, rc=%d\n",
						panel->name, rc);
			}
		} else if (oplus_dc_v2_on) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE0_DC);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE0_DC cmds, rc=%d\n",
						panel->name, rc);
			}
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE0);
			if (rc) {
					pr_err("[%s] failed to send DSI_CMD_SEED_MODE0 cmds, rc=%d\n",
							panel->name, rc);
			}
		}
		break;
	case OPLUS_SEED_MODE1:
		if ((panel->is_dc_set_color_mode) && (!panel->is_hbm_enabled) && (oplus_dc_v2_on)) {
			panel->is_dc_set_color_mode = false;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE1_DC_SWITCH);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE1_DC_SWITCH cmds, rc=%d\n",
						panel->name, rc);
			}
		} else if ((panel->is_dc_set_color_mode) && (!panel->is_hbm_enabled) && (!oplus_dc_v2_on)) {
			panel->is_dc_set_color_mode = false;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE1_SWITCH);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE1_SWITCH cmds, rc=%d\n",
						panel->name, rc);
			}
		} else if (oplus_dc_v2_on) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE1_DC);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE1_DC cmds, rc=%d\n",
						panel->name, rc);
			}
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE1);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE1 cmds, rc=%d\n",
						panel->name, rc);
			}
		}
		break;
	case OPLUS_SEED_MODE2:
		if ((panel->is_dc_set_color_mode) && (!panel->is_hbm_enabled) && (oplus_dc_v2_on)) {
			panel->is_dc_set_color_mode = false;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE2_DC_SWITCH);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE2_DC_SWITCH cmds, rc=%d\n",
						panel->name, rc);
			}
		} else if ((panel->is_dc_set_color_mode) && (!panel->is_hbm_enabled) && (!oplus_dc_v2_on)) {
			panel->is_dc_set_color_mode = false;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE2_SWITCH);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE2_SWITCH cmds, rc=%d\n",
						panel->name, rc);
			}
		} else if (oplus_dc_v2_on) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE2_DC);
			if (rc) {
				pr_err("[%s] failed to send DSI_CMD_SEED_MODE2_DC cmds, rc=%d\n",
						panel->name, rc);
			}
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE2);
			if (rc) {
					pr_err("[%s] failed to send DSI_CMD_SEED_MODE2 cmds, rc=%d\n",
							panel->name, rc);
			}
		}
		break;
	case OPLUS_SEED_MODE3:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE3);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE3 cmds, rc=%d\n",
					panel->name, rc);
		}
		break;
	case OPLUS_SEED_MODE4:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE4);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE4 cmds, rc=%d\n",
					panel->name, rc);
		}
		break;
	default:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_OFF);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_OFF cmds, rc=%d\n",
					panel->name, rc);
		}
		pr_err("[%s] seed mode Invalid %d\n",
			panel->name, mode);
	}

	if (!oplus_dc_v2_on) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_EXIT);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_EXIT cmds, rc=%d\n",
				panel->name, rc);
		}
	}

	return rc;
}

int dsi_panel_seed_mode(struct dsi_panel *panel, int mode) {
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	/* mutex_lock(&panel->panel_lock); */

	if ((!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3")
		|| !strcmp(panel->oplus_priv.vendor_name, "AMB655XL08"))
		&& (mode >= PANEL_LOADING_EFFECT_FLAG)) {
		rc = dsi_panel_loading_effect_mode_unlock(panel, mode);
	} else if (!strcmp(panel->oplus_priv.vendor_name, "s6e3fc3")
                        && (is_project(20813) || is_project(20814))
			&& (mode >= PANEL_LOADING_EFFECT_FLAG)) {
		mode = mode - PANEL_LOADING_EFFECT_FLAG;
		rc = dsi_panel_seed_mode_unlock(panel, mode);
		seed_mode = mode;
	} else if (panel->oplus_priv.is_oplus_project) {
		rc = dsi_panel_loading_effect_mode_unlock(panel, mode);
	} else if (is_support_panel_seed_mode_exceed(panel->oplus_priv.vendor_name, mode)) {
		mode = mode - PANEL_LOADING_EFFECT_FLAG;
		if (panel->oplus_priv.is_dc_seed_support) {
			rc = dsi_panel_dc_seed_mode_unlock(panel, mode);
		} else {
			rc = dsi_panel_seed_mode_unlock(panel, mode);
		}
		seed_mode = mode;
	} else {
		rc = dsi_panel_seed_mode_unlock(panel, mode);
	}

	/* mutex_unlock(&panel->panel_lock); */
	return rc;
}

int dsi_display_seed_mode(struct dsi_display *display, int mode) {
	int rc = 0;
	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
	}

	mutex_lock(&display->panel->panel_lock);

	rc = dsi_panel_seed_mode(display->panel, mode);
		if (rc) {
			pr_err("[%s] failed to dsi_panel_seed_or_loading_effect, rc=%d\n",
			       display->name, rc);
	}

	mutex_unlock(&display->panel->panel_lock);

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
	rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);

	}
	mutex_unlock(&display->display_lock);
	return rc;
}

int oplus_dsi_update_seed_mode(void)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	ret = dsi_display_seed_mode(display, seed_mode);

	return ret;
}

int oplus_display_panel_get_seed(void *data)
{
	uint32_t *temp = data;
	printk(KERN_INFO "oplus_display_get_seed = %d\n",seed_mode);

	(*temp) = seed_mode;
	return 0;
}

int oplus_display_panel_set_seed(void *data)
{
	uint32_t *temp_save = data;

	printk(KERN_INFO "%s oplus_display_set_seed = %d\n", __func__, *temp_save);
	seed_mode = *temp_save;

	__oplus_display_set_seed(*temp_save);
	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if(get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_set_seed and main display is null");
			return -EINVAL;
		}
		dsi_display_seed_mode(get_main_display(), seed_mode);
	} else {
		printk(KERN_ERR	 "%s oplus_display_set_seed = %d, but now display panel status is not on\n", __func__, *temp_save);
	}

	return 0;
}
