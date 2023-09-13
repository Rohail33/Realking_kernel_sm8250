// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_IC_H__
#define __OPLUS_CHG_IC_H__

#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#else
#include "../../oplus_chg_core.h"
#endif

enum oplus_chg_ic_type {
	OPLUS_CHG_IC_BUCK,
	OPLUS_CHG_IC_BOOST,
	OPLUS_CHG_IC_BUCK_BOOST,
	OPLUS_CHG_IC_CP_DIV2,
	OPLUS_CHG_IC_CP_MUL2,
	OPLUS_CHG_IC_CP_TW2,
	OPLUS_CHG_IC_RX,
	OPLUS_CHG_IC_VIRTUAL_RX,
	OPLUS_CHG_IC_VIRTUAL_BUCK,
	OPLUS_CHG_IC_VIRTUAL_CP,
	OPLUS_CHG_IC_VIRTUAL_USB,
	OPLUS_CHG_IC_TYPEC,
};

struct oplus_chg_ic_dev {
	const char *name;
	struct device *dev;
	void *dev_ops;
	enum oplus_chg_ic_type type;
	struct list_head list;
	struct list_head child_list;
	atomic_t child_num;
	struct list_head brother_list;
	atomic_t brother_num;
	int index;
	char manu_name[16];
	char fw_id[16];
};

struct oplus_chg_ic_ops {
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
	int (*reg_dump)(struct oplus_chg_ic_dev *);
#endif
};

struct oplus_chg_ic_rx_ops {
	struct oplus_chg_ic_ops ic_ops;
	int (*rx_set_enable)(struct oplus_chg_ic_dev *, bool);
	bool (*rx_is_enable)(struct oplus_chg_ic_dev *);
	bool (*rx_is_connected)(struct oplus_chg_ic_dev *);
	int (*rx_get_vout)(struct oplus_chg_ic_dev *, int *);
	int (*rx_set_vout)(struct oplus_chg_ic_dev *, int);
	int (*rx_get_vrect)(struct oplus_chg_ic_dev *, int *);
	int (*rx_get_iout)(struct oplus_chg_ic_dev *, int *);
	int (*rx_get_trx_vol)(struct oplus_chg_ic_dev *, int *);
	int (*rx_get_trx_curr)(struct oplus_chg_ic_dev *, int *);
	int (*rx_get_cep_count)(struct oplus_chg_ic_dev *, int *);
	int (*rx_get_cep_val)(struct oplus_chg_ic_dev *, int *);
	int (*rx_get_work_freq)(struct oplus_chg_ic_dev *, int *);
	int (*rx_get_rx_mode)(struct oplus_chg_ic_dev *, enum oplus_chg_wls_rx_mode *);
	int (*rx_set_rx_mode)(struct oplus_chg_ic_dev *, enum oplus_chg_wls_rx_mode);
	int (*rx_set_dcdc_enable)(struct oplus_chg_ic_dev *, bool);
	int (*rx_set_trx_enable)(struct oplus_chg_ic_dev *, bool);
	int (*rx_set_trx_start)(struct oplus_chg_ic_dev *);
	int (*rx_get_trx_status)(struct oplus_chg_ic_dev *, u8 *);
	int (*rx_get_trx_err)(struct oplus_chg_ic_dev *, u8 *);
	int (*rx_get_headroom)(struct oplus_chg_ic_dev *, int *);
	int (*rx_set_headroom)(struct oplus_chg_ic_dev *, int);
	int (*rx_send_match_q)(struct oplus_chg_ic_dev *, u8);
	int (*rx_set_fod_parm)(struct oplus_chg_ic_dev *, u8 [], int);
	int (*rx_send_msg)(struct oplus_chg_ic_dev *, unsigned char [], int len);
	int (*rx_register_msg_callback)(struct oplus_chg_ic_dev *, void *, void (*)(void *, u8 []));
	int (*rx_get_rx_version)(struct oplus_chg_ic_dev *, u32 *);
	int (*rx_get_trx_version)(struct oplus_chg_ic_dev *, u32 *);
	int (*rx_upgrade_firmware_by_buf)(struct oplus_chg_ic_dev *, unsigned char *, int);
	int (*rx_upgrade_firmware_by_img)(struct oplus_chg_ic_dev *, char *, int);
	int (*rx_connect_check)(struct oplus_chg_ic_dev *);
};

struct oplus_chg_ic_buck_ops {
	struct oplus_chg_ic_ops ic_ops;
	int (*chg_set_input_enable)(struct oplus_chg_ic_dev *, bool);
	int (*chg_set_output_enable)(struct oplus_chg_ic_dev *, bool);
	int (*chg_set_icl)(struct oplus_chg_ic_dev *, int);
	int (*chg_set_fcc)(struct oplus_chg_ic_dev *, int);
	int (*chg_set_fv)(struct oplus_chg_ic_dev *, int);
	int (*chg_set_rechg_vol)(struct oplus_chg_ic_dev *, int);
	int (*chg_get_icl)(struct oplus_chg_ic_dev *, int *);
	int (*chg_get_input_curr)(struct oplus_chg_ic_dev *, int *);
	int (*chg_get_input_vol)(struct oplus_chg_ic_dev *, int *);
	int (*chg_set_boost_en)(struct oplus_chg_ic_dev *, bool);
	int (*chg_set_boost_vol)(struct oplus_chg_ic_dev *, int);
	int (*chg_set_boost_curr_limit)(struct oplus_chg_ic_dev *, int);
	int (*chg_set_aicl_enable)(struct oplus_chg_ic_dev *, bool);
	int (*chg_set_aicl_rerun)(struct oplus_chg_ic_dev *);
	int (*chg_set_vindpm)(struct oplus_chg_ic_dev *, int);
	int (*wls_set_bcc_max_curr)(struct oplus_chg_ic_dev *, int);
	int (*wls_set_bcc_min_curr)(struct oplus_chg_ic_dev *, int);
	int (*wls_set_bcc_stop_curr)(struct oplus_chg_ic_dev *, int);
};

struct oplus_chg_ic_cp_ops {
	struct oplus_chg_ic_ops ic_ops;
	int (*cp_set_enable)(struct oplus_chg_ic_dev *, bool);
	int (*cp_start)(struct oplus_chg_ic_dev *);
	int (*cp_test)(struct oplus_chg_ic_dev *);
	int (*cp_get_fault)(struct oplus_chg_ic_dev *, char *);
};

struct oplus_chg_ic_typec_ops {
	struct oplus_chg_ic_ops ic_ops;
	int (*typec_get_cc_orientation)(struct oplus_chg_ic_dev *, int *);
	int (*otg_enable)(struct oplus_chg_ic_dev *, bool);
	int (*typec_get_hw_detect)(struct oplus_chg_ic_dev *, int *);
	int (*typec_get_vbus_status)(struct oplus_chg_ic_dev *, int *);
};

static inline void *oplus_chg_ic_get_drvdata(const struct oplus_chg_ic_dev *ic_dev)
{
	return dev_get_drvdata(ic_dev->dev);
}

void oplus_chg_ic_list_lock(void);
void oplus_chg_ic_list_unlock(void);
int oplsu_chg_ic_add_child(struct oplus_chg_ic_dev *ic_dev, struct oplus_chg_ic_dev *ch_dev);
struct oplus_chg_ic_dev *oplsu_chg_ic_get_child_by_index(struct oplus_chg_ic_dev *p_dev, int c_index);
struct oplus_chg_ic_dev *oplsu_chg_ic_find_by_name(const char *name);
struct oplus_chg_ic_dev *of_get_oplus_chg_ic(struct device_node *node, const char *prop_name);
#ifdef OPLUS_CHG_REG_DUMP_ENABLE
int oplus_chg_ic_reg_dump(struct oplus_chg_ic_dev *ic_dev);
int oplus_chg_ic_reg_dump_by_name(const char *name);
void oplus_chg_ic_reg_dump_all(void);
#endif
struct oplus_chg_ic_dev *oplus_chg_ic_register(struct device *dev,
	const char *name, int index);
int oplus_chg_ic_unregister(struct oplus_chg_ic_dev *ic_dev);
struct oplus_chg_ic_dev *devm_oplus_chg_ic_register(struct device *dev,
	const char *name, int index);
int devm_oplus_chg_ic_unregister(struct device *dev, struct oplus_chg_ic_dev *ic_dev);

#endif /* __OPLUS_CHG_IC_H__ */
