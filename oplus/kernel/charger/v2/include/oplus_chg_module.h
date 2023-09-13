#ifndef __OP_CHG_MODULE_H__
#define __OP_CHG_MODULE_H__

#include <linux/types.h>
#include <linux/module.h>

#ifdef MODULE

#define OPLUS_CHG_MODEL_MAGIC 0x20300000

struct oplus_chg_module {
	const char *name;
	size_t magic;
	int (*chg_module_init) (void);
	void (*chg_module_exit) (void);
};

#define oplus_chg_module_register(__name)			\
__attribute__((section(".oplus_chg_module.normal.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODEL_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#define oplus_chg_module_core_register(__name)			\
__attribute__((section(".oplus_chg_module.core.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODEL_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#define oplus_chg_module_early_register(__name)			\
__attribute__((section(".oplus_chg_module.early.data"), used))	\
struct oplus_chg_module __name##_module = {			\
	.name = #__name,					\
	.magic = OPLUS_CHG_MODEL_MAGIC,				\
	.chg_module_init = __name##_init,			\
	.chg_module_exit = __name##_exit,			\
}

#define oplus_chg_module_late_register(__name)			\
__attribute__((section(".oplus_chg_module.late.data"), used))	\
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

#define oplus_chg_module_core_register(__name)	\
	fs_initcall(__name##_init);		\
	module_exit(__name##_exit)

#define oplus_chg_module_early_register(__name)	\
	rootfs_initcall(__name##_init);		\
	module_exit(__name##_exit)

#define oplus_chg_module_late_register(__name)	\
	late_initcall(__name##_init);		\
	module_exit(__name##_exit)

#endif /* MODULE */

#endif /* __OP_CHG_MODULE_H__ */
