/*
 * An exposure adjustment driver based on Qcom DSPP for OLED devices
 *
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) Sony Mobile Communications Inc. All rights reserved.
 * Copyright (C) 2014-2018, AngeloGioacchino Del Regno <kholk11@gmail.com>
 * Copyright (C) 2018, Devries <therkduan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef EXPOSURE_ADJUSTMENT_H
#define EXPOSURE_ADJUSTMENT_H

/**
 * Device-specific parameters
 * Modify these when porting
 * @ELVSS_OFF_THRESHOLD: Minimum backlight threshold for disable smart elvss
 * @EXPOSURE_ADJUSTMENT_MIN: Minimum available PCC coefficient for OLED panel
 */
#define ELVSS_OFF_THRESHOLD        258
#define EXPOSURE_ADJUSTMENT_MIN    5200

/* PCC coefficient when exposure is 255 */
#define EXPOSURE_ADJUSTMENT_MAX    32768
/* Scale for the PCC coefficient with elvss backlight range */
#define PCC_BACKLIGHT_SCALE \
(EXPOSURE_ADJUSTMENT_MAX - EXPOSURE_ADJUSTMENT_MIN) / ELVSS_OFF_THRESHOLD

void ea_panel_mode_ctrl(struct dsi_panel *panel, bool enable);
u32 ea_panel_calc_backlight(u32 bl_lvl);
#endif /* EXPOSURE_ADJUSTMENT_H */
