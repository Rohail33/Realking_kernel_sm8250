/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __SILEAD_FP_MTK_H__
#define __SILEAD_FP_MTK_H__

struct fp_plat_t {
#ifdef CONFIG_OF
    u32 spi_id;
    u32 spi_irq;
    u32 spi_reg;
    struct pinctrl *pinctrl;
    struct pinctrl_state *pins_default;
    struct pinctrl_state *pins_irq, *pins_rst_h, *pins_rst_l;
    struct pinctrl_state *spi_default;
#ifdef BSP_SIL_POWER_SUPPLY_PINCTRL
    struct pinctrl_state *pins_avdd_h, *pins_vddio_h;
#endif /* BSP_SIL_POWER_SUPPLY_PINCTRL */
#endif
};

#endif /* __SILEAD_FP_MTK_H__ */

/* End of file silead_fp_mtk.h */
