// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "OPLUS_CHG[WLS_RX]: %s[%d]: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#ifdef CONFIG_OPLUS_CHG_OOS
#include <linux/oem/oplus_chg.h>
#include <linux/oem/project_info.h>
#else
#include "../../oplus_chg_core.h"
#endif
#include "../../oplus_chg_module.h"
#include "oplus_chg_ic.h"
#include "../oplus_chg_wls.h"
#include "wls_chg_fw.h"

struct oplus_wls_chg_firmware *oplus_wls_firmware;

#ifdef MODULE
__attribute__((weak)) size_t __oplus_chg_wls_fw_start;
__attribute__((weak)) size_t __oplus_chg_wls_fw_end;
#else /* MODULE */
static struct oplus_wls_chg_firmware *oplus_wls_chg_find_first_firmware(void)
{
	struct oplus_wls_chg_firmware *wls_firmware;

	wls_firmware = &oplus_wls_firmware_start_fw;

	wls_firmware--;
	while (wls_firmware != NULL &&
	       wls_firmware->magic0 == OPLUS_CHG_FIRMWARE_MAGIC0 &&
	       wls_firmware->magic1 == OPLUS_CHG_FIRMWARE_MAGIC1)
		wls_firmware--;

	wls_firmware++;
	return wls_firmware;
}
#endif /* MODULE */

bool is_rx_ic_available(struct oplus_wls_chg_rx *wls_rx)
{
	struct device_node *node = wls_rx->dev->of_node;

	if(wls_rx->rx_ic == NULL)
		wls_rx->rx_ic = of_get_oplus_chg_ic(node, "oplus,rx_ic");
	return !!wls_rx->rx_ic;
}

int oplus_chg_wls_rx_enable(struct oplus_wls_chg_rx *wls_rx, bool en)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (wls_rx == NULL) {
		pr_err("wls_rx is NULL\n");
		return -ENODEV;
	}
	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}

	rx_ic = wls_rx->rx_ic;
	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_set_enable(rx_ic, en);

	return rc;
}

int oplus_chg_wls_rx_get_vout(struct oplus_wls_chg_rx *wls_rx, int *vol_mv)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (wls_rx == NULL) {
		pr_err("wls_rx is NULL\n");
		return -ENODEV;
	}
	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}

	rx_ic = wls_rx->rx_ic;
	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_vout(rx_ic, vol_mv);

	return rc;
}

int oplus_chg_wls_get_cep_check_update(struct oplus_wls_chg_rx *wls_rx,
					      int *cep)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	static int cep_count;
	int cep_count_temp;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_cep_count(rx_ic, &cep_count_temp);
	if (rc < 0) {
		pr_err("can't get cep count, rc=%d\n", rc);
		return rc;
	}
	if (cep_count == cep_count_temp) {
		pr_info("cep not update\n");
		return -EINVAL;
	}
	cep_count = cep_count_temp;
	rc = rx_ic_ops->rx_get_cep_val(rx_ic, cep);

	return rc;
}

int oplus_chg_wls_get_cep(struct oplus_wls_chg_rx *wls_rx, int *cep)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_cep_val(rx_ic, cep);

	return rc;
}

static void oplus_chg_wls_cep_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_wls_chg_rx *wls_rx = container_of(dwork, struct oplus_wls_chg_rx, cep_check_work);
	int rc;
	int cep;

	if (!wls_rx->wls_dev->wls_status.rx_online) {
		pr_info("rx is offline, exit\n");
		complete(&wls_rx->cep_ok_ack);
		return;
	}

	rc = oplus_chg_wls_get_cep_check_update(wls_rx, &cep);
	if (rc < 0)
		goto out;
	pr_info("wkcs: cep=%d\n", cep);
	if (abs(cep) < 3) {
		wls_rx->cep_is_ok = true;
		complete(&wls_rx->cep_ok_ack);
		return;
	}

out:
	schedule_delayed_work(&wls_rx->cep_check_work, msecs_to_jiffies(100));
}

int oplus_chg_wls_rx_set_vout_ms(struct oplus_wls_chg_rx *wls_rx, int vol_mv, int wait_time_ms)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int cep;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_set_vout(rx_ic, vol_mv);
	if (rc < 0) {
		pr_err("can't set vout to %dmV, rc=%d\n", vol_mv, rc);
		return rc;
	}
	pr_err("set vout to %d mV\n", vol_mv);
	wls_rx->vol_set_mv = vol_mv;
	wls_rx->cep_is_ok = false;
	pr_err("vol_set_mv=%d\n", wls_rx->vol_set_mv);
	(void)oplus_chg_wls_get_cep_check_update(wls_rx, &cep);
	if (wait_time_ms > 0) {
		reinit_completion(&wls_rx->cep_ok_ack);
		schedule_delayed_work(&wls_rx->cep_check_work, msecs_to_jiffies(100));
		rc = wait_for_completion_timeout(&wls_rx->cep_ok_ack, msecs_to_jiffies(wait_time_ms));
		if (!rc) {
			pr_err("wait cep timeout\n");
			cancel_delayed_work_sync(&wls_rx->cep_check_work);
			return -ETIMEDOUT;
		}
		if (wls_rx->clean_source)
			return -EINVAL;
	} else if (wait_time_ms < 0) {
		reinit_completion(&wls_rx->cep_ok_ack);
		schedule_delayed_work(&wls_rx->cep_check_work, msecs_to_jiffies(100));
		wait_for_completion(&wls_rx->cep_ok_ack);
		if (wls_rx->clean_source)
			return -EINVAL;
	}

	return 0;
}

int oplus_chg_wls_rx_set_vout(struct oplus_wls_chg_rx *wls_rx, int vol_mv, int wait_time_s)
{
	unsigned long wait_time_ms;
	int rc;

	if (wait_time_s > 0)
		wait_time_ms = wait_time_s * 1000;
	else
		wait_time_ms = wait_time_s;

	rc = oplus_chg_wls_rx_set_vout_ms(wls_rx, vol_mv, wait_time_ms);
	if (rc < 0)
		return rc;

	return 0;
}

int oplus_chg_wls_rx_set_vout_step(struct oplus_wls_chg_rx *wls_rx, int target_vol_mv,
				   int step_mv, int wait_time_s)
{
	int next_step_mv;
	unsigned long remaining_time_ms;
	unsigned long stop_time;
	unsigned long start_time = jiffies;
	bool no_timeout;
	int rc;

	if (wait_time_s == 0) {
		pr_err("waiting time must be greater than 0\n");
		return -EINVAL;
	} else if (wait_time_s > 0) {
		no_timeout = false;
		remaining_time_ms = wait_time_s * 1000;
		stop_time = msecs_to_jiffies(wait_time_s * 1000) + start_time;
	} else {
		no_timeout = true;
	}

next_step:
	if (target_vol_mv >= wls_rx->vol_set_mv) {
		next_step_mv = wls_rx->vol_set_mv + step_mv;
		if (next_step_mv > target_vol_mv)
			next_step_mv = target_vol_mv;
	} else {
		next_step_mv = wls_rx->vol_set_mv - step_mv;
		if (next_step_mv < target_vol_mv)
			next_step_mv = target_vol_mv;
	}
	rc = oplus_chg_wls_rx_set_vout_ms(wls_rx, next_step_mv, no_timeout ? -1 : remaining_time_ms);
	if (rc < 0) {
		pr_err("can't set vout to %dmV, rc=%d\n", next_step_mv, rc);
		return rc;
	}
	if (next_step_mv != target_vol_mv) {
		if (!no_timeout) {
			if (time_is_before_jiffies(stop_time)) {
				pr_err("can't set vout to %dmV\n", next_step_mv);
				return -ETIMEDOUT;
			}
			remaining_time_ms = remaining_time_ms - jiffies_to_msecs(jiffies - start_time);
			start_time = jiffies;
		}
		goto next_step;
	}

	return 0;
}

int oplus_chg_wls_rx_get_vrect(struct oplus_wls_chg_rx *wls_rx, int *vol_mv)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_vrect(rx_ic, vol_mv);

	return rc;
}

int oplus_chg_wls_rx_get_iout(struct oplus_wls_chg_rx *wls_rx, int *curr_ma)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_iout(rx_ic, curr_ma);

	return rc;
}

int oplus_chg_wls_rx_get_trx_vol(struct oplus_wls_chg_rx *wls_rx, int *vol_mv)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_trx_vol(rx_ic, vol_mv);

	return rc;
}

int oplus_chg_wls_rx_get_trx_curr(struct oplus_wls_chg_rx *wls_rx, int *curr_ma)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_trx_curr(rx_ic, curr_ma);

	return rc;
}

int oplus_chg_wls_rx_get_work_freq(struct oplus_wls_chg_rx *wls_rx, int *freq)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_work_freq(rx_ic, freq);

	return rc;
}

int oplus_chg_wls_rx_get_rx_mode(struct oplus_wls_chg_rx *wls_rx, enum oplus_chg_wls_rx_mode *rx_mode)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_rx_mode(rx_ic, rx_mode);

	return rc;
}

int oplus_chg_wls_rx_set_rx_mode(struct oplus_wls_chg_rx *wls_rx, enum oplus_chg_wls_rx_mode rx_mode)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc = 0;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	if (rx_ic_ops->rx_set_rx_mode)
		rc = rx_ic_ops->rx_set_rx_mode(rx_ic, rx_mode);

	return rc;
}

int oplus_chg_wls_rx_set_dcdc_enable(struct oplus_wls_chg_rx *wls_rx, bool en)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_set_dcdc_enable(rx_ic, en);

	return rc;
}

int oplus_chg_wls_rx_set_trx_enable(struct oplus_wls_chg_rx *wls_rx, bool en)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_set_trx_enable(rx_ic, en);

	return rc;
}

int oplus_chg_wls_rx_set_trx_start(struct oplus_wls_chg_rx *wls_rx)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc = 0;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	if (rx_ic_ops && rx_ic_ops->rx_set_trx_start)
		rc = rx_ic_ops->rx_set_trx_start(rx_ic);

	return rc;
}

int oplus_chg_wls_rx_get_trx_status(struct oplus_wls_chg_rx *wls_rx, u8 *status)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_trx_status(rx_ic, status);

	return rc;
}

int oplus_chg_wls_rx_get_trx_err(struct oplus_wls_chg_rx *wls_rx, u8 *err)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_get_trx_err(rx_ic, err);

	return rc;
}

int oplus_chg_wls_rx_send_match_q(struct oplus_wls_chg_rx *wls_rx, u8 data)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_send_match_q(rx_ic, data);

	return rc;
}

int oplus_chg_wls_rx_set_fod_parm(struct oplus_wls_chg_rx *wls_rx, u8 buf[], int len)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_set_fod_parm(rx_ic, buf, len);

	return rc;
}

bool oplus_chg_wls_rx_is_connected(struct oplus_wls_chg_rx *wls_rx)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return false;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	return rx_ic_ops->rx_is_connected(rx_ic);
}

int oplus_chg_wls_rx_send_msg(struct oplus_wls_chg_rx *wls_rx, unsigned char msg, unsigned char data)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	unsigned char buf[4] = {msg, ~msg, data, ~data};
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_send_msg(rx_ic, buf, ARRAY_SIZE(buf));
	pr_info("send msg, msg=0x%02x, data=0x%02x\n", msg, data);

	return rc;
}

int oplus_chg_wls_rx_send_data(struct oplus_wls_chg_rx *wls_rx, unsigned char msg,
			       unsigned char data[], int len)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	unsigned char *buf;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;
	buf = devm_kzalloc(wls_rx->dev, len + 1, GFP_KERNEL);
	if (!buf) {
		pr_err("alloc data buf error\n");
		return -ENOMEM;
	}
	buf[0] = msg;
	memcpy(buf + 1, data, len);

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_send_msg(rx_ic, buf, len + 1);

	return rc;
}

int oplus_chg_wls_rx_register_msg_callback(struct oplus_wls_chg_rx *wls_rx,
					   void *dev_data,
					   void (*call_back)(void *, u8 []))
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_register_msg_callback(rx_ic, dev_data, call_back);

	return rc;
}

int oplus_chg_wls_rx_upgrade_firmware_by_img(struct oplus_wls_chg_rx *wls_rx)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc = 0;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}

	if (oplus_wls_firmware == NULL) {
		pr_err("The wls_firmware address error!");
		return -EFAULT;
	}

	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	__pm_stay_awake(wls_rx->update_fw_wake_lock);
	wls_rx->wls_dev->wls_status.fw_upgrading = true;
	rc = rx_ic_ops->rx_upgrade_firmware_by_img(rx_ic,
		oplus_wls_firmware->wls_chg_firmware, oplus_wls_firmware->fw_size);
	if (rc < 0) {
		pr_err("can't upgrade firmware by img, rc=%d\n", rc);
		goto out;
	}

#ifdef CONFIG_OPLUS_CHG_OOS
	push_component_info(WIRELESS_CHARGE, rx_ic->fw_id, rx_ic->manu_name);
#endif

out:
	wls_rx->wls_dev->wls_status.fw_upgrading = false;
	__pm_relax(wls_rx->update_fw_wake_lock);
	return rc;
}

int oplus_chg_wls_rx_upgrade_firmware_by_buf(struct oplus_wls_chg_rx *wls_rx,
					     unsigned char *buf, int len)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc = 0;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;

	rx_ic_ops = rx_ic->dev_ops;
	__pm_stay_awake(wls_rx->update_fw_wake_lock);
	wls_rx->wls_dev->wls_status.fw_upgrading = true;
	rc = rx_ic_ops->rx_upgrade_firmware_by_buf(rx_ic, buf, len);
	if (rc < 0) {
		pr_err("can't upgrade firmware by buf, rc=%d\n", rc);
		goto out;
	}

out:
	wls_rx->wls_dev->wls_status.fw_upgrading = false;
	__pm_relax(wls_rx->update_fw_wake_lock);
	return rc;
}

int oplus_chg_wls_rx_get_rx_version(struct oplus_wls_chg_rx *wls_rx, u32 *version)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}

	rx_ic = wls_rx->rx_ic;
	rx_ic_ops = rx_ic->dev_ops;

	rc = rx_ic_ops->rx_get_rx_version(rx_ic, version);
	if (rc < 0)
		pr_err("can't get rx version, rc=%d\n", rc);

	return rc;
}

int oplus_chg_wls_rx_get_trx_version(struct oplus_wls_chg_rx *wls_rx, u32 *version)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}

	rx_ic = wls_rx->rx_ic;
	rx_ic_ops = rx_ic->dev_ops;

	rc = rx_ic_ops->rx_get_trx_version(rx_ic, version);
	if (rc < 0)
		pr_err("can't get tx version, rc=%d\n", rc);

	return rc;
}

int oplus_chg_wls_rx_connect_check(struct oplus_wls_chg_rx *wls_rx)
{
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;
	int rc;

	if (!is_rx_ic_available(wls_rx)) {
		pr_err("rx_ic is NULL\n");
		return -ENODEV;
	}

	rx_ic = wls_rx->rx_ic;
	rx_ic_ops = rx_ic->dev_ops;
	rc = rx_ic_ops->rx_connect_check(rx_ic);
	if (rc < 0)
		pr_err("can't start rx connect check, rc=%d\n", rc);

	return rc;
}

int oplus_chg_wls_rx_clean_source(struct oplus_wls_chg_rx *wls_rx)
{
	if (wls_rx == NULL) {
		pr_err("wls_rx is NULL\n");
		return -ENODEV;
	}

	wls_rx->clean_source = true;
	cancel_delayed_work_sync(&wls_rx->cep_check_work);
	complete(&wls_rx->cep_ok_ack);
	wls_rx->cep_is_ok = true;
	wls_rx->vol_set_mv = 5000;
	wls_rx->clean_source = false;

	return 0;
}

int oplus_chg_wls_rx_smt_test(struct oplus_wls_chg_rx *wls_rx)
{
	int rc;
	u32 err_code = 0;
	u32 version;
	struct oplus_chg_ic_dev *rx_ic;
	struct oplus_chg_ic_rx_ops *rx_ic_ops;

	if (wls_rx == NULL) {
		pr_err("wls_rx is NULL\n");
		return -ENODEV;
	}
	rx_ic = wls_rx->rx_ic;
	rx_ic_ops = rx_ic->dev_ops;

	rc = rx_ic_ops->rx_get_rx_version(rx_ic, &version);
	if (rc < 0) {
		pr_err("can't get rx version, rc=%d\n", rc);
		err_code |= BIT(rx_ic->index);
	}

	return err_code;
}

int oplus_chg_wls_rx_init(struct oplus_chg_wls *wls_dev)
{
	struct oplus_wls_chg_rx *wls_rx;
	struct oplus_wls_chg_firmware *wls_firmware;
#ifdef MODULE
	struct oplus_wls_chg_firmware *last_wls_firmware;
#endif
	wls_rx = devm_kzalloc(wls_dev->dev, sizeof(struct oplus_wls_chg_rx), GFP_KERNEL);
	if (wls_rx == NULL) {
		pr_err("alloc memory error\n");
		devm_kfree(wls_dev->dev, wls_rx);
		return -ENOMEM;
	}

#ifdef MODULE
	wls_firmware = (struct oplus_wls_chg_firmware *)&__oplus_chg_wls_fw_start;
	last_wls_firmware = (struct oplus_wls_chg_firmware *)&__oplus_chg_wls_fw_end;
	last_wls_firmware--;
	while ((size_t)wls_firmware <= (size_t)last_wls_firmware) {
		if ((wls_firmware->magic == OPLUS_CHG_FIRMWARE_MAGIC) &&
		    (!strcasecmp(wls_firmware->name, wls_dev->wls_chg_fw_name))) {
			oplus_wls_firmware = wls_firmware;
			pr_err("The target firmware is %s, has select the wls_firmware is %s\n",
			       wls_dev->wls_chg_fw_name, wls_firmware->name);
			break;
		}
		wls_firmware++;
	}
#else
	wls_firmware = oplus_wls_chg_find_first_firmware();
	if (wls_firmware == NULL)
		pr_err("The wls_firmware address error!");
	while (wls_firmware != NULL &&
		wls_firmware->magic0 == OPLUS_CHG_FIRMWARE_MAGIC0 &&
		wls_firmware->magic1 == OPLUS_CHG_FIRMWARE_MAGIC1) {
		if (!strcasecmp(wls_firmware->name, wls_dev->wls_chg_fw_name)) {
			oplus_wls_firmware = wls_firmware;
			pr_err("The target firmware is %s, has select the wls_firmware is %s\n",
			       wls_dev->wls_chg_fw_name, wls_firmware->name);
			break;
		}
		wls_firmware++;
	}
#endif /* MODULE */

	if(oplus_wls_firmware == NULL){
		pr_err("No wireless firmware available to match\n");
		devm_kfree(wls_dev->dev, wls_rx);
		return -EFAULT;
	}


	wls_dev->wls_rx = wls_rx;
	wls_rx->dev = wls_dev->dev;
	wls_rx->wls_dev = wls_dev;

	INIT_DELAYED_WORK(&wls_rx->cep_check_work, oplus_chg_wls_cep_check_work);
	init_completion(&wls_rx->cep_ok_ack);
	wls_rx->update_fw_wake_lock = wakeup_source_register(wls_rx->dev, "wls_update_lock");

	return 0;
}

int oplus_chg_wls_rx_remove(struct oplus_chg_wls *wls_dev)
{
	struct oplus_wls_chg_rx *wls_rx = wls_dev->wls_rx;

	devm_kfree(wls_dev->dev, wls_rx);
	wls_dev->wls_rx = NULL;

	return 0;
}
