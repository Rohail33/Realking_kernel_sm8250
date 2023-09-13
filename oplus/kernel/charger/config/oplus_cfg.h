// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, 2019 The Linux Foundation. All rights reserved.
 */

#ifndef __OPLUS_CFG_H__
#define __OPLUS_CFG_H__

#include <linux/types.h>
#include <linux/kernel.h>

#define OPLUS_CFG_MAGIC 0x02000300

enum oplus_param_type {
	OPLUS_CHG_WIRED_SPEC_PARAM,
	OPLUS_CHG_WIRED_NORMAL_PARAM,
	OPLUS_CHG_WLS_SPEC_PARAM,
	OPLUS_CHG_WLS_NORMAL_PARAM,
	OPLUS_CHG_COMM_SPEC_PARAM,
	OPLUS_CHG_COMM_NORMAL_PARAM,
	OPLUS_CHG_VOOC_SPEC_PARAM,
	OPLUS_CHG_VOOC_NORMAL_PARAM,
	OPLUS_CHG_VOOCPHY_PARAM,
	OPLUS_CHG_PPS_PARAM,
	OPLUS_CFG_PARAM_MAX,
};

struct oplus_cfg_head {
	u32 magic;
	u32 head_size;
	u32 size;
	u8 signature[512];
	u8 buf[0];
} __attribute__ ((packed));

struct oplus_cfg_check_info {
	u32 uid;
	u32 pid;
} __attribute__ ((packed));

struct oplus_cfg_param_info {
	u32 magic;
	u32 param_num;
	u32 param_index[0];
} __attribute__ ((packed));

struct oplus_param_head {
	u32 magic;
	u32 size;
	u32 type;
	u8 data[0];
} __attribute__ ((packed));

struct oplus_cfg_data_head {
	u32 magic;
	u32 data_index;
	u32 size;
	u8 data[0];
} __attribute__ ((packed));

struct oplus_cfg {
	enum oplus_param_type type;
	void *priv_data;
	struct list_head list;
	int (*update)(void *, struct oplus_param_head *);
};

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG)
struct oplus_cfg_data_head *
oplus_cfg_find_param_by_name(struct oplus_param_head *param_head,
				const char *name);
ssize_t oplus_cfg_get_data_size(struct oplus_cfg_data_head *data_head);
int oplus_cfg_get_data(struct oplus_cfg_data_head *data_head, u8 *buf,
			   size_t len);
int oplus_cfg_get_data_by_name(struct oplus_param_head *param_head,
				   const char *name, u8 *buf, size_t len);
int oplus_cfg_register(struct oplus_cfg *cfg);
int oplus_cfg_unregister(struct oplus_cfg *cfg);
#else /* CONFIG_OPLUS_DYNAMIC_CONFIG */
static inline struct oplus_cfg_data_head *
oplus_cfg_find_param_by_name(struct oplus_param_head *param_head,
				 const char *name)
{
	return NULL;
}

static inline ssize_t
oplus_cfg_get_data_size(struct oplus_cfg_data_head *data_head)
{
	return 0;
}

static inline int
oplus_cfg_get_data(struct oplus_cfg_data_head *data_head, u8 *buf,
		       size_t len)
{
	return 0;
}

static inline int
oplus_cfg_get_data_by_name(struct oplus_param_head *param_head,
			       const char *name, u8 *buf, size_t len)
{
	return 0;
}

static inline int oplus_cfg_register(struct oplus_cfg *cfg)
{
	return 0;
}

static inline int oplus_cfg_unregister(struct oplus_cfg *cfg)
{
	return 0;
}
#endif /* CONFIG_OPLUS_DYNAMIC_CONFIG */

#endif /* __OPLUS_CFG_H__ */
