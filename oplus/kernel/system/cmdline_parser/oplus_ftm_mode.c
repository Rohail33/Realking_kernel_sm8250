#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>


#define MAX_CMDLINE_PARAM_LEN 1024
char oplus_ftm_mode[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(oplus_ftm_mode);

module_param_string(oplus_ftm_mode, oplus_ftm_mode, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(oplus_ftm_mode,
"oplus_ftm_mode=<oplus_ftm_mode>");

MODULE_LICENSE("GPL v2");

