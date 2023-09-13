#ifndef _OPLUS_CHG_OPS_H_
#define _OPLUS_CHG_OPS_H_

#include "oplus_charger.h"
#include <linux/of.h>

int oplus_chg_ops_register(const char *name, struct oplus_chg_operations *chg_ops);
void oplus_chg_ops_deinit(void);
struct oplus_chg_operations *oplus_chg_ops_get(void);
void oplus_get_chg_ops_name_from_dt(struct device_node *node);
char *oplus_chg_ops_name_get(void);
#endif /* _OPLUS_CHG_OPS_H_ */
