// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __WLS_CHG_INTF_H__
#define __WLS_CHG_INTF_H__

enum wls_path_type {
	WLS_PATH_RX,
	WLS_PATH_FAST,
	WLS_PATH_NORMAAL,
};

/* wls rx */
int oplus_chg_wls_rx_enable(struct oplus_wls_chg_rx *wls_rx, bool en);
int oplus_chg_wls_rx_get_vout(struct oplus_wls_chg_rx *wls_rx, int *vol_mv);
int oplus_chg_wls_get_cep_check_update(struct oplus_wls_chg_rx *wls_rx,
					      int *cep);
int oplus_chg_wls_get_cep(struct oplus_wls_chg_rx *wls_rx, int *cep);
int oplus_chg_wls_rx_set_vout(struct oplus_wls_chg_rx *wls_rx, int vol_mv, int wait_time_s);
int oplus_chg_wls_rx_set_vout_step(struct oplus_wls_chg_rx *wls_rx, int target_vol_mv,
				   int step_mv, int wait_time_s);
int oplus_chg_wls_rx_get_vrect(struct oplus_wls_chg_rx *wls_rx, int *vol_mv);
int oplus_chg_wls_rx_get_iout(struct oplus_wls_chg_rx *wls_rx, int *curr_ma);
int oplus_chg_wls_rx_get_trx_vol(struct oplus_wls_chg_rx *wls_rx, int *vol_mv);
int oplus_chg_wls_rx_get_trx_curr(struct oplus_wls_chg_rx *wls_rx, int *curr_ma);
int oplus_chg_wls_rx_get_work_freq(struct oplus_wls_chg_rx *wls_rx, int *freq);
int oplus_chg_wls_rx_get_rx_mode(struct oplus_wls_chg_rx *wls_rx, enum oplus_chg_wls_rx_mode *rx_mode);
int oplus_chg_wls_rx_set_rx_mode(struct oplus_wls_chg_rx *wls_rx, enum oplus_chg_wls_rx_mode rx_mode);
int oplus_chg_wls_rx_set_dcdc_enable(struct oplus_wls_chg_rx *wls_rx, bool en);
int oplus_chg_wls_rx_set_trx_enable(struct oplus_wls_chg_rx *wls_rx, bool en);
int oplus_chg_wls_rx_set_trx_start(struct oplus_wls_chg_rx *wls_rx);
int oplus_chg_wls_rx_get_trx_status(struct oplus_wls_chg_rx *wls_rx, u8 *status);
int oplus_chg_wls_rx_get_trx_err(struct oplus_wls_chg_rx *wls_rx, u8 *err);
int oplus_chg_wls_rx_send_match_q(struct oplus_wls_chg_rx *wls_rx, u8 data);
int oplus_chg_wls_rx_set_fod_parm(struct oplus_wls_chg_rx *wls_rx, u8 buf[], int len);
bool oplus_chg_wls_rx_is_connected(struct oplus_wls_chg_rx *wls_rx);
int oplus_chg_wls_rx_send_msg(struct oplus_wls_chg_rx *wls_rx, unsigned char msg, unsigned char data);
int oplus_chg_wls_rx_send_data(struct oplus_wls_chg_rx *wls_rx, unsigned char msg, unsigned char data[], int len);
int oplus_chg_wls_rx_register_msg_callback(struct oplus_wls_chg_rx *wls_rx,
					   void *dev_data,
					   void (*call_back)(void *wls_rx, u8 []));
int oplus_chg_wls_rx_upgrade_firmware_by_img(struct oplus_wls_chg_rx *wls_rx);
int oplus_chg_wls_rx_upgrade_firmware_by_buf(struct oplus_wls_chg_rx *wls_rx,
					     unsigned char *buf, int len);
int oplus_chg_wls_rx_get_rx_version(struct oplus_wls_chg_rx *wls_rx, u32 *version);
int oplus_chg_wls_rx_get_trx_version(struct oplus_wls_chg_rx *wls_rx, u32 *version);
int oplus_chg_wls_rx_connect_check(struct oplus_wls_chg_rx *wls_rx);
int oplus_chg_wls_rx_clean_source(struct oplus_wls_chg_rx *wls_rx);
int oplus_chg_wls_rx_smt_test(struct oplus_wls_chg_rx *wls_rx);
int oplus_chg_wls_rx_init(struct oplus_chg_wls *wls_dev);
int oplus_chg_wls_rx_remove(struct oplus_chg_wls *wls_dev);
bool is_rx_ic_available(struct oplus_wls_chg_rx *wls_rx);

/* wls normal */
int oplus_chg_wls_nor_set_input_enable(struct oplus_wls_chg_normal *wls_nor, bool en);
int oplus_chg_wls_nor_set_output_enable(struct oplus_wls_chg_normal *wls_nor, bool en);
int oplus_chg_wls_nor_set_icl(struct oplus_wls_chg_normal *wls_nor, int icl_ma);
int oplus_chg_wls_nor_set_icl_by_step(struct oplus_wls_chg_normal *wls_nor, int icl_ma, int step_ma, bool block);
int oplus_chg_wls_nor_set_fcc(struct oplus_wls_chg_normal *wls_nor, int fcc_ma);
int oplus_chg_wls_nor_set_fcc_by_step(struct oplus_wls_chg_normal *wls_nor, int fcc_ma, int step_ma, bool block);
int oplus_chg_wls_nor_set_fv(struct oplus_wls_chg_normal *wls_nor, int fv_mv);
int oplus_chg_wls_nor_set_rechg_vol(struct oplus_wls_chg_normal *wls_nor, int rechg_vol_mv);
int oplus_chg_wls_nor_get_icl(struct oplus_wls_chg_normal *wls_nor, int *icl_ma);
int oplus_chg_wls_nor_get_input_curr(struct oplus_wls_chg_normal *wls_nor, int *curr_ma);
int oplus_chg_wls_nor_get_input_vol(struct oplus_wls_chg_normal *wls_nor, int *vol_mv);
int oplus_chg_wls_nor_set_boost_en(struct oplus_wls_chg_normal *wls_nor, bool en);
int oplus_chg_wls_nor_set_boost_vol(struct oplus_wls_chg_normal *wls_nor, int vol_mv);
int oplus_chg_wls_nor_set_boost_curr_limit(struct oplus_wls_chg_normal *wls_nor, int curr_ma);
int oplus_chg_wls_nor_set_aicl_enable(struct oplus_wls_chg_normal *wls_nor, bool en);
int oplus_chg_wls_nor_set_aicl_rerun(struct oplus_wls_chg_normal *wls_nor);
int oplus_chg_wls_nor_set_vindpm(struct oplus_wls_chg_normal *wls_nor, int vindpm_mv);
int oplus_chg_wls_nor_clean_source(struct oplus_wls_chg_normal *wls_nor);
int oplus_chg_wls_nor_init(struct oplus_chg_wls *wls_dev);
int oplus_chg_wls_nor_remove(struct oplus_chg_wls *wls_dev);

/* wls fast */
int oplus_chg_wls_fast_set_enable(struct oplus_wls_chg_fast *wls_fast, bool en);
int oplus_chg_wls_fast_start(struct oplus_wls_chg_fast *wls_fast);
int oplus_chg_wls_fast_smt_test(struct oplus_wls_chg_fast *wls_fast);
int oplus_chg_wls_fast_get_fault(struct oplus_wls_chg_fast *wls_fast, char *fault);
int oplus_chg_wls_fast_init(struct oplus_chg_wls *wls_dev);
int oplus_chg_wls_fast_remove(struct oplus_chg_wls *wls_dev);

#endif /* __WLS_CHG_INTF_H__ */
