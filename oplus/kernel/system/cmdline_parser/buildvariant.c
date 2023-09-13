#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>


#define MAX_CMDLINE_PARAM_LEN 1024
char build_variant[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(build_variant);

module_param_string(buildvariant, build_variant, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(buildvariant,
"buildvariant=<buildvariant>");

MODULE_LICENSE("GPL v2");
