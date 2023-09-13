/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __SILEAD_FP_QCOM_H__
#define __SILEAD_FP_QCOM_H__

#include <linux/pinctrl/consumer.h>
#include <linux/clk.h>
#include <soc/qcom/scm.h>

struct fp_plat_t {
    u32 qup_id;
#ifdef QSEE_V4
    u32 max_speed_hz;
#else
    /* pinctrl info */
    struct pinctrl  *pinctrl;
    struct pinctrl_state  *active;
    struct pinctrl_state  *sleep;
#ifdef BSP_SIL_POWER_SUPPLY_PINCTRL
    struct pinctrl_state *pins_avdd_h, *pins_vddio_h;
#endif /* BSP_SIL_POWER_SUPPLY_PINCTRL */
    /* clock info */
    struct clk    *core_clk;
    struct clk    *iface_clk;
#endif /* QSEE_V4 */
};

#endif /* __SILEAD_FP_QCOM_H__ */

/* End of file silead_fp_qcom.h */
