#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>

#include "oplus_chg_ops_manager.h"

#define CHG_OPS_DESC_NAME_MAX_LENTH 64

struct oplus_chg_ops_desc {
	struct list_head list;
	struct oplus_chg_operations *chg_ops;
	char name[CHG_OPS_DESC_NAME_MAX_LENTH];
};

struct oplus_chg_ops_mg_data {
	struct list_head chg_ops_list_head;
	spinlock_t chg_ops_list_lock;
	char chg_ops_name[CHG_OPS_DESC_NAME_MAX_LENTH];
};

static struct oplus_chg_ops_mg_data g_oplus_chg_ops_mg_data;
static bool hasInited = false;

static void oplus_chg_ops_manager_init(void)
{
	if (hasInited)
		return;

	INIT_LIST_HEAD(&g_oplus_chg_ops_mg_data.chg_ops_list_head);
	spin_lock_init(&g_oplus_chg_ops_mg_data.chg_ops_list_lock);
	hasInited = true;
}

static struct oplus_chg_ops_desc *oplus_chg_ops_desc_get(const char *name)
{
	struct list_head *pos;
	struct oplus_chg_ops_desc *loopup = NULL, *element = NULL;

	if (list_empty(&g_oplus_chg_ops_mg_data.chg_ops_list_head)) {
		chg_err("chg_ops_list_head list_empty\n");
		return NULL;
	}

	spin_lock(&g_oplus_chg_ops_mg_data.chg_ops_list_lock);
	list_for_each (pos, &g_oplus_chg_ops_mg_data.chg_ops_list_head) {
		element = list_entry(pos, struct oplus_chg_ops_desc, list);
		chg_err("members->name: %s\n", element->name);
		if (!strncmp(name, element->name, CHG_OPS_DESC_NAME_MAX_LENTH)) {
			loopup = element;
			chg_err("name: %s\n", name);
			break;
		}
	}
	spin_unlock(&g_oplus_chg_ops_mg_data.chg_ops_list_lock);

	return loopup;
}

void oplus_get_chg_ops_name_from_dt(struct device_node *node)
{
	const char *chg_ops_name_dt = NULL;
	char *chg_ops_name = g_oplus_chg_ops_mg_data.chg_ops_name;
	int rc;

	rc = of_property_read_string(node, "oplus,chg_ops", &chg_ops_name_dt);

	strncpy(chg_ops_name, (rc ? "plat-pmic" : chg_ops_name_dt), CHG_OPS_DESC_NAME_MAX_LENTH);
	chg_ops_name[CHG_OPS_DESC_NAME_MAX_LENTH - 1] = '\0';

	chg_err("chg_ops_name: %s\n", chg_ops_name);
}

int oplus_chg_ops_register(const char *name, struct oplus_chg_operations *chg_ops)
{
	struct oplus_chg_ops_desc *oplus_chg_ops_desc_new = NULL;

	if (!hasInited)
		oplus_chg_ops_manager_init();

	oplus_chg_ops_desc_new = oplus_chg_ops_desc_get(name);

	if (oplus_chg_ops_desc_new == NULL) {
		oplus_chg_ops_desc_new = kmalloc(sizeof(struct oplus_chg_ops_desc), GFP_ATOMIC);
		if (oplus_chg_ops_desc_new != NULL) {
			strncpy(oplus_chg_ops_desc_new->name, name, CHG_OPS_DESC_NAME_MAX_LENTH);
			(oplus_chg_ops_desc_new->name)[CHG_OPS_DESC_NAME_MAX_LENTH - 1] = '\0';
			oplus_chg_ops_desc_new->chg_ops = chg_ops;

			spin_lock(&g_oplus_chg_ops_mg_data.chg_ops_list_lock);
			list_add_tail(&oplus_chg_ops_desc_new->list, &g_oplus_chg_ops_mg_data.chg_ops_list_head);
			spin_unlock(&g_oplus_chg_ops_mg_data.chg_ops_list_lock);

			chg_err("->name: %s\n", oplus_chg_ops_desc_new->name);
			return 0;
		}
	}

	return -1;
}

void oplus_chg_ops_deinit(void)
{
	struct list_head *pos;
	struct oplus_chg_ops_desc *oplus_chg_ops_desc_loopup = NULL;

	spin_lock(&g_oplus_chg_ops_mg_data.chg_ops_list_lock);
	list_for_each (pos, &g_oplus_chg_ops_mg_data.chg_ops_list_head) {
		oplus_chg_ops_desc_loopup = list_entry(pos, struct oplus_chg_ops_desc, list);
		if (oplus_chg_ops_desc_loopup != NULL) {
			list_del(&oplus_chg_ops_desc_loopup->list);
			kfree(oplus_chg_ops_desc_loopup);
		}
	}
	spin_unlock(&g_oplus_chg_ops_mg_data.chg_ops_list_lock);
}

struct oplus_chg_operations *oplus_chg_ops_get(void)
{
	struct oplus_chg_ops_desc *chg_ops_desc = NULL;

	if (!hasInited)
		oplus_chg_ops_manager_init();

	chg_ops_desc = oplus_chg_ops_desc_get(g_oplus_chg_ops_mg_data.chg_ops_name);
	if (chg_ops_desc != NULL) {
		chg_err("name: %s\n", g_oplus_chg_ops_mg_data.chg_ops_name);
		return chg_ops_desc->chg_ops;
	}

	return NULL;
}

char *oplus_chg_ops_name_get(void)
{
	return g_oplus_chg_ops_mg_data.chg_ops_name;
}
