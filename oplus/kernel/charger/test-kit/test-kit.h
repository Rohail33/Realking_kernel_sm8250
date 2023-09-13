// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2022 Oplus. All rights reserved.
 */

#ifndef __TEST_KIT_H__
#define __TEST_KIT_H__

#include <linux/list.h>
#include <linux/gpio.h>

struct test_feature;

typedef bool (*test_kit_func_t)(struct test_feature *feature,
				char *buf, size_t len);
typedef bool (*test_kit_gpio_check_func_t)(void *info, char *buf,
					   size_t len, size_t *use_size);
typedef bool (*test_kit_typec_port_check_func_t)(void *info, char *buf,
					   size_t len, size_t *use_size);

struct test_feature_cfg {
	const char *name;
	void *test_info;
	test_kit_func_t test_func;
};

struct test_feature {
	void *private_data;
	struct list_head list;
	bool enable;

	const struct test_feature_cfg *cfg;
};

struct test_kit_typec_port_info {
	const char *name;
	int case_num;
	int status;
	int situation;
};

struct test_kit_soc_gpio_info {
	struct gpio_chip *chip;
	const char *name;
	int num;
	bool is_out;
	bool is_high;
	int func;
	int pull;
	int drive;
};

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
struct test_feature * __must_check
test_feature_register(const struct test_feature_cfg *cfg, void *private_data);
void test_feature_unregister(struct test_feature *feature);

bool test_kit_typec_port_test(struct test_feature *feature,
				 char *buf, size_t len);
bool test_kit_qcom_soc_gpio_test(struct test_feature *feature,
				 char *buf, size_t len);
bool test_kit_mtk_soc_gpio_test(struct test_feature *feature,
				 char *buf, size_t len);
int test_kit_reg_typec_port_check(test_kit_typec_port_check_func_t func);
void test_kit_unreg_typec_port_check(void);
int test_kit_reg_qcom_soc_gpio_check(test_kit_gpio_check_func_t func);
void test_kit_unreg_qcom_soc_gpio_check(void);
int test_kit_reg_qcom_spmi_gpio_check(test_kit_gpio_check_func_t func);
void test_kit_unreg_qcom_spmi_gpio_check(void);
int test_kit_reg_mtk_soc_gpio_check(test_kit_gpio_check_func_t func);
void test_kit_unreg_mtk_soc_gpio_check(void);
int test_kit_reg_mtk_spmi_gpio_check(test_kit_gpio_check_func_t func);
void test_kit_unreg_mtk_spmi_gpio_check(void);
bool test_kit_mtk_gpio_check(void *info, char *buf, size_t len, size_t *use_size);
bool test_kit_qcom_gpio_check(void *info, char *buf, size_t len, size_t *use_size);
#else /* CONFIG_OPLUS_CHG_TEST_KIT */
inline struct test_feature *
test_feature_register(const struct test_feature_cfg *cfg, void *private_data)
{
	return NULL;
}

inline void test_feature_unregister(struct test_feature *feature)
{
}

bool test_kit_qcom_soc_gpio_test(struct test_feature *feature,
				 char *buf, size_t len)
{
	return false;
}

bool test_kit_mtk_soc_gpio_test(struct test_feature *feature,
				 char *buf, size_t len)
{
	return false;
}

int test_kit_reg_qcom_soc_gpio_check(test_kit_gpio_check_func_t func)
{
	return -ENOTSUPP;
}

int test_kit_reg_mtk_soc_gpio_check(test_kit_gpio_check_func_t func)
{
	return -ENOTSUPP;
}

void test_kit_unreg_qcom_soc_gpio_check(void)
{
}

void test_kit_unreg_mtk_soc_gpio_check(void)
{
}

int test_kit_reg_qcom_spmi_gpio_check(test_kit_gpio_check_func_t func)
{
	return -ENOTSUPP;
}

int test_kit_reg_mtk_spmi_gpio_check(test_kit_gpio_check_func_t func)
{
	return -ENOTSUPP;
}

void test_kit_unreg_qcom_spmi_gpio_check(void)
{
}

void test_kit_unreg_mtk_spmi_gpio_check(void)
{
}

bool test_kit_typec_port_test(struct test_feature *feature,
				 char *buf, size_t len)
{
	return false;
}

int test_kit_reg_typec_port_check(test_kit_gpio_check_func_t func)
{
	return -ENOTSUPP;
}

void test_kit_unreg_typec_port_check(void)
{
}

bool test_kit_mtk_gpio_check(void *info, char *buf, size_t len, size_t *use_size)
{
	return false;
}
bool test_kit_qcom_gpio_check(void *info, char *buf, size_t len, size_t *use_size)
{
	return false;
}
#endif /* CONFIG_OPLUS_CHG_TEST_KIT */

#endif /* __TEST_KIT_H__ */
