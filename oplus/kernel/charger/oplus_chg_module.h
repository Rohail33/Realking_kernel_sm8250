// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __OP_CHG_MODULE_H__
#define __OP_CHG_MODULE_H__

#include <linux/types.h>

#ifdef MODULE

#define OPLUS_CHG_MODEL_MAGIC 0x20300000

struct oplus_chg_module {
	const char *name;
	size_t magic;
	int (*chg_module_init) (void);
	void (*chg_module_exit) (void);
};

#define oplus_chg_module_register(__name)			\
__attribute__((section(".oplus_chg_module.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODEL_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#else /* MODULE */

#define oplus_chg_module_register(__name)	\
	module_init(__name##_init);		\
	module_exit(__name##_exit)

#endif /* MODULE */

#endif /* __OP_CHG_MODULE_H__ */
