#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>


#define MAX_CMDLINE_PARAM_LEN 1024
char cdt[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(cdt);

module_param_string(cdt_intergrity, cdt, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(cdt_intergrity,
"cdt_intergrity=<cdt_intergrity>");

MODULE_LICENSE("GPL v2");
