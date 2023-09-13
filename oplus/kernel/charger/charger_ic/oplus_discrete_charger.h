// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __DISCRETE_CHARGER_HEADER__
#define __DISCRETE_CHARGER_HEADER__

/* Charger IC */
enum {
	RT9471D = 0,
	RT9467,
	BQ2589X,
	BQ2591X,
	BQ2560X,
	SY6970,
	SY6974B,
	SGM41511,
	SGM41512
};

extern void set_charger_ic(int sel);
int oplus_get_subboard_temp(void);
int qpnp_get_prop_charger_voltage_now(void);
bool oplus_check_pdphy_ready(void);
#endif
