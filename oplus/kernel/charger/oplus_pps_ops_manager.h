#ifndef _OPLUS_PPS_OPS_H_
#define _OPLUS_PPS_OPS_H_

#include <linux/of.h>
#include "oplus_charger.h"
#include "oplus_pps.h"

int oplus_pps_ops_register(const char *name, struct oplus_pps_operations *pps_ops);
void oplus_pps_ops_deinit(void);
struct oplus_pps_operations *oplus_pps_ops_get(void);
void oplus_get_pps_ops_name_from_dt(struct device_node *node);
char *oplus_pps_ops_name_get(void);
#endif /* _OPLUS_CHG_OPS_H_ */
