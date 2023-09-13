#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/regulator/consumer.h>


#define MAX_CMDLINE_PARAM_LEN 1024
char sim_card_num[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(sim_card_num);

module_param_string(doublesim, sim_card_num, MAX_CMDLINE_PARAM_LEN,
0600);
MODULE_PARM_DESC(doublesim,
"doublesim=<doublesim>");

MODULE_LICENSE("GPL v2");
