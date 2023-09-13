#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>


#define MAX_CMDLINE_PARAM_LEN 1024
char startup_mode[MAX_CMDLINE_PARAM_LEN];
char bootmode[MAX_CMDLINE_PARAM_LEN];
char serial_no[MAX_CMDLINE_PARAM_LEN];
char verified_bootstate[MAX_CMDLINE_PARAM_LEN];
char prj_name[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(startup_mode);
EXPORT_SYMBOL(bootmode);
EXPORT_SYMBOL(serial_no);
EXPORT_SYMBOL(verified_bootstate);
EXPORT_SYMBOL(prj_name);

module_param_string(startupmode, startup_mode, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(startupmode,
"androidboot.startupmode=<startupmode>");

module_param_string(mode, bootmode, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(mode,
"androidboot.mode=<mode>");

module_param_string(serialno, serial_no, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(serialno,
"androidboot.serialno=<serialno>");

module_param_string(verifiedbootstate, verified_bootstate, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(verifiedbootstate,
"androidboot.verifiedbootstate=<verifiedbootstate>");

module_param_string(prjname, prj_name, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(prjname,
"androidboot.prjname=<prjname>");

MODULE_LICENSE("GPL v2");
