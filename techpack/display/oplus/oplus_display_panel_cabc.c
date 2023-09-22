/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_display_panel_cabc.c
** Description : oplus display panel cabc feature
** Version : 1.0
** Date : 2020/06/13
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#include "oplus_display_panel_cabc.h"

u32 oplus_last_backlight = 0;
static int cabc_mode = 0;
static int cabc_mode_backup = 1;
static int cabc_lock_flag = 0;
DEFINE_MUTEX(oplus_cabc_lock);

static int panel_cabc_mode_config(int mode)
{
	mutex_lock(&oplus_cabc_lock);
	if (mode != cabc_mode) {
		cabc_mode = mode;
	}

	if (cabc_mode == CABC_ENTER_SPECIAL) {
		cabc_lock_flag = 1;
		cabc_mode = CABC_MODE_OFF;
	} else if (cabc_mode == CABC_EXIT_SPECIAL) {
		cabc_lock_flag = 0;
		cabc_mode = cabc_mode_backup;
	} else {
		cabc_mode_backup = cabc_mode;
	}

	mutex_unlock(&oplus_cabc_lock);
	pr_info("%s,cabc mode is %d, cabc_mode_backup is %d, cabc_lock_flag=%d\n",
		__func__, cabc_mode, cabc_mode_backup, cabc_lock_flag);
	if (cabc_mode == cabc_mode_backup && cabc_lock_flag) {
		pr_err("cabc is locked, nothing to do");
		return -1;
	}

	return 0;
}

static int panel_cabc_cmd_config_unlock(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!dsi_panel_initialized(panel)) {
		return -EINVAL;
	}

	switch (mode) {
	case CABC_MODE_OFF:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_OFF);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_CABC_OFF cmds, rc=%d\n",
			       panel->name, rc);
		}
		break;
	case CABC_MODE_1:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_MODE1);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_CABC_MODE1 cmds, rc=%d\n",
			       panel->name, rc);
		}
		break;
	case CABC_MODE_2:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_MODE2);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_CABC_MODE2 cmds, rc=%d\n",
			       panel->name, rc);
		}
		break;
	case CABC_MODE_3:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_MODE3);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_CABC_MODE3 cmds, rc=%d\n",
			       panel->name, rc);
		}
		break;
	default:
		pr_err("[%s] CABC_MODE%d Invalid, default set CABC_MODE1\n", panel->name, mode);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_MODE1);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_CABC_MODE1 cmds, rc=%d\n",
			       panel->name, rc);
		}
	}

	return rc;
}

static int panel_cabc_cmd_config(struct dsi_display *display, int mode)
{
	int rc = 0;

	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	if (display->panel->panel_type == DSI_DISPLAY_PANEL_TYPE_OLED) {
		pr_err("oled cabc no need!\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE)
		dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_CORE_CLK, DSI_CLK_ON);

	mutex_lock(&display->panel->panel_lock);
	rc = panel_cabc_cmd_config_unlock(display->panel, mode);
	if (rc) {
		pr_err("[%s] panel_cabc_cmd_config_unlock fail, rc=%d\n", display->name, rc);
	}
	mutex_unlock(&display->panel->panel_lock);

	if (display->config.panel_mode == DSI_OP_CMD_MODE)
		dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_CORE_CLK, DSI_CLK_OFF);

	mutex_unlock(&display->display_lock);
	return rc;
}

int oplus_display_panel_get_cabc(void *data)
{
	uint32_t *temp = data;
	pr_info("%s: cabc_mode=%d\n", __func__, cabc_mode);

	(*temp) = cabc_mode;
	return 0;
}

int oplus_display_panel_set_cabc(void *data)
{
	struct dsi_display *display = get_main_display();
	uint32_t *temp_save = data;
	int rc = 0;

	if (!display || !display->panel) {
		pr_err("%s: display or display->panel is null\n", __func__);
		return -EINVAL;
	}

	if (!display->panel->oplus_priv.lcd_cabc_support) {
		pr_err("%s: panel not support cabc\n", __func__);
		return -EINVAL;
	}

	pr_info("%s: to set cabc_mode=%d\n", __func__, *temp_save);
	rc = panel_cabc_mode_config(*temp_save);
	if (rc) {
		pr_err("%s: Not to config cabc_mode=%d", __func__, *temp_save);
		return -EINVAL;
	}

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		rc = panel_cabc_cmd_config(display, cabc_mode);
	} else {
		pr_err("%s: panel is off, set cabc_mode=%d fail\n", __func__, *temp_save);
	}

	return rc;
}
