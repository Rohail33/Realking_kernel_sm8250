#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/time.h>

#include <linux/device.h>
#include <linux/nls.h>
#include <linux/kdev_t.h>
#include "oplus_chg_core.h"

#include "oplus_pps_ops_manager.h"

#define PPS_OPS_DESC_NAME_MAX_LENTH 64

struct oplus_pps_ops_desc {
	struct list_head list;
	struct oplus_pps_operations *pps_ops;
	char name[PPS_OPS_DESC_NAME_MAX_LENTH];
};

struct oplus_pps_ops_mg_data {
	struct list_head pps_ops_list_head;
	spinlock_t pps_ops_list_lock;
	char pps_ops_name[PPS_OPS_DESC_NAME_MAX_LENTH];
};

static struct oplus_pps_ops_mg_data g_oplus_pps_ops_mg_data;
static bool has_inited = false;

static void oplus_pps_ops_manager_init(void)
{
	if (has_inited)
		return;

	INIT_LIST_HEAD(&g_oplus_pps_ops_mg_data.pps_ops_list_head);
	spin_lock_init(&g_oplus_pps_ops_mg_data.pps_ops_list_lock);
	has_inited = true;
}

static struct oplus_pps_ops_desc *oplus_pps_ops_desc_get(const char *name)
{
	struct list_head *pos;
	struct oplus_pps_ops_desc *loopup = NULL, *element = NULL;

	if (list_empty(&g_oplus_pps_ops_mg_data.pps_ops_list_head)) {
		chg_err("pps_ops_list_head list_empty\n");
		return NULL;
	}

	spin_lock(&g_oplus_pps_ops_mg_data.pps_ops_list_lock);
	list_for_each (pos, &g_oplus_pps_ops_mg_data.pps_ops_list_head) {
		element = list_entry(pos, struct oplus_pps_ops_desc, list);
		chg_err("members->name: %s\n", element->name);
		if (!strncmp(name, element->name, PPS_OPS_DESC_NAME_MAX_LENTH)) {
			loopup = element;
			chg_err("name: %s\n", name);
			break;
		}
	}
	spin_unlock(&g_oplus_pps_ops_mg_data.pps_ops_list_lock);

	return loopup;
}

void oplus_get_pps_ops_name_from_dt(struct device_node *node)
{
	const char *pps_ops_name_dt = NULL;
	char *pps_ops_name = g_oplus_pps_ops_mg_data.pps_ops_name;
	int rc;

	rc = of_property_read_string(node, "oplus,pps_ops", &pps_ops_name_dt);

	strncpy(pps_ops_name, (rc ? "mcu-op10" : pps_ops_name_dt), PPS_OPS_DESC_NAME_MAX_LENTH);
	pps_ops_name[PPS_OPS_DESC_NAME_MAX_LENTH - 1] = '\0';

	chg_err("pps_ops_name: %s\n", pps_ops_name);
}

int oplus_pps_ops_register(const char *name, struct oplus_pps_operations *pps_ops)
{
	struct oplus_pps_ops_desc *oplus_pps_ops_desc_new = NULL;

	if (!has_inited)
		oplus_pps_ops_manager_init();

	oplus_pps_ops_desc_new = oplus_pps_ops_desc_get(name);

	if (oplus_pps_ops_desc_new == NULL) {
		oplus_pps_ops_desc_new = kmalloc(sizeof(struct oplus_pps_ops_desc), GFP_ATOMIC);
		if (oplus_pps_ops_desc_new != NULL) {
			strncpy(oplus_pps_ops_desc_new->name, name, PPS_OPS_DESC_NAME_MAX_LENTH);
			(oplus_pps_ops_desc_new->name)[PPS_OPS_DESC_NAME_MAX_LENTH - 1] = '\0';
			oplus_pps_ops_desc_new->pps_ops = pps_ops;

			spin_lock(&g_oplus_pps_ops_mg_data.pps_ops_list_lock);
			list_add_tail(&oplus_pps_ops_desc_new->list, &g_oplus_pps_ops_mg_data.pps_ops_list_head);
			spin_unlock(&g_oplus_pps_ops_mg_data.pps_ops_list_lock);

			chg_err("->name: %s\n", oplus_pps_ops_desc_new->name);
			return 0;
		}
	}

	return -1;
}

void oplus_pps_ops_deinit(void)
{
	struct list_head *pos;
	struct oplus_pps_ops_desc *oplus_pps_ops_desc_loopup = NULL;

	spin_lock(&g_oplus_pps_ops_mg_data.pps_ops_list_lock);
	list_for_each (pos, &g_oplus_pps_ops_mg_data.pps_ops_list_head) {
		oplus_pps_ops_desc_loopup = list_entry(pos, struct oplus_pps_ops_desc, list);
		if (oplus_pps_ops_desc_loopup != NULL) {
			list_del(&oplus_pps_ops_desc_loopup->list);
			kfree(oplus_pps_ops_desc_loopup);
		}
	}
	spin_unlock(&g_oplus_pps_ops_mg_data.pps_ops_list_lock);
}

struct oplus_pps_operations *oplus_pps_ops_get(void)
{
	struct oplus_pps_ops_desc *pps_ops_desc = NULL;

	if (!has_inited)
		oplus_pps_ops_manager_init();

	pps_ops_desc = oplus_pps_ops_desc_get(g_oplus_pps_ops_mg_data.pps_ops_name);
	if (pps_ops_desc != NULL) {
		chg_err("name: %s\n", g_oplus_pps_ops_mg_data.pps_ops_name);
		return pps_ops_desc->pps_ops;
	}

	return NULL;
}

char *oplus_pps_ops_name_get(void)
{
	chg_err("oplus_pps_ops_name_get: %s\n", g_oplus_pps_ops_mg_data.pps_ops_name);
	return g_oplus_pps_ops_mg_data.pps_ops_name;
}
