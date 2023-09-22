/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_display_panel_cabc.h
** Description : oplus display panel cabc feature
** Version : 1.0
** Date : 2020/06/13
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#ifndef _OPLUS_DISPLAY_PANEL_CABC_H_
#define _OPLUS_DISPLAY_PANEL_CABC_H_

#include <linux/err.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"

enum {
	CABC_MODE_OFF = 0,
	CABC_MODE_1 = 1,
	CABC_MODE_2,
	CABC_MODE_3,
	CABC_EXIT_SPECIAL = 8,
	CABC_ENTER_SPECIAL = 9,
};

extern u32 oplus_last_backlight;

int oplus_display_panel_get_cabc(void *data);
int oplus_display_panel_set_cabc(void *data);

#endif  /* _OPLUS_DISPLAY_PANEL_CABC_H_ */