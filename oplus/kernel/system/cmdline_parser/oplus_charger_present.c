#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>


#define MAX_CMDLINE_PARAM_LEN 1024
char charger_present[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(charger_present);

module_param_string(oplus_charger_present, charger_present, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(oplus_charger_present,
"oplus_charger_present=<oplus_charger_present>");

MODULE_LICENSE("GPL v2");

