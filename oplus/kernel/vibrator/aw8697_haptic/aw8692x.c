/*
 * File: aw8692x.c
 *
 * Author: Ethan <chelvming@awinic.com>
 *
 * Copyright (c) 2021 AWINIC Technology CO., LTD
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include <linux/vmalloc.h>
#include <linux/pm_qos.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/soc.h>

#include "haptic_hv.h"
#include "haptic_hv_reg.h"

const uint8_t aw8692x_reg_list[AW8692X_REG_SUM] = {
	AW8692X_REG_RSTCFG,
	AW8692X_REG_SYSST,
	AW8692X_REG_SYSINT,
	AW8692X_REG_SYSINTM,
	AW8692X_REG_SYSST2,
	AW8692X_REG_SYSER,
	AW8692X_REG_PLAYCFG1,
	AW8692X_REG_PLAYCFG2,
	AW8692X_REG_PLAYCFG3,
	AW8692X_REG_PLAYCFG4,
	AW8692X_REG_WAVCFG1,
	AW8692X_REG_WAVCFG2,
	AW8692X_REG_WAVCFG3,
	AW8692X_REG_WAVCFG4,
	AW8692X_REG_WAVCFG5,
	AW8692X_REG_WAVCFG6,
	AW8692X_REG_WAVCFG7,
	AW8692X_REG_WAVCFG8,
	AW8692X_REG_WAVCFG9,
	AW8692X_REG_WAVCFG10,
	AW8692X_REG_WAVCFG11,
	AW8692X_REG_WAVCFG12,
	AW8692X_REG_WAVCFG13,
	AW8692X_REG_CONTCFG1,
	AW8692X_REG_CONTCFG2,
	AW8692X_REG_CONTCFG3,
	AW8692X_REG_CONTCFG4,
	AW8692X_REG_CONTCFG5,
	AW8692X_REG_CONTCFG6,
	AW8692X_REG_CONTCFG7,
	AW8692X_REG_CONTCFG8,
	AW8692X_REG_CONTCFG9,
	AW8692X_REG_CONTCFG10,
	AW8692X_REG_CONTCFG11,
	AW8692X_REG_CONTCFG12,
	AW8692X_REG_CONTCFG13,
	AW8692X_REG_CONTCFG14,
	AW8692X_REG_CONTCFG15,
	AW8692X_REG_CONTCFG16,
	AW8692X_REG_CONTCFG17,
	AW8692X_REG_CONTCFG18,
	AW8692X_REG_CONTCFG19,
	AW8692X_REG_CONTCFG20,
	AW8692X_REG_CONTCFG21,
	AW8692X_REG_RTPCFG1,
	AW8692X_REG_RTPCFG2,
	AW8692X_REG_RTPCFG3,
	AW8692X_REG_RTPCFG4,
	AW8692X_REG_RTPCFG5,
	AW8692X_REG_TRGCFG1,
	AW8692X_REG_TRGCFG2,
	AW8692X_REG_TRGCFG3,
	AW8692X_REG_TRGCFG4,
	AW8692X_REG_TRGCFG5,
	AW8692X_REG_TRGCFG6,
	AW8692X_REG_TRGCFG7,
	AW8692X_REG_TRGCFG8,
	AW8692X_REG_GLBCFG1,
	AW8692X_REG_GLBCFG2,
	AW8692X_REG_GLBCFG3,
	AW8692X_REG_GLBCFG4,
	AW8692X_REG_GLBRD5,
	AW8692X_REG_RAMADDRH,
	AW8692X_REG_RAMADDRL,
	AW8692X_REG_SYSCTRL1,
	AW8692X_REG_SYSCTRL2,
	AW8692X_REG_SYSCTRL3,
	AW8692X_REG_SYSCTRL4,
	AW8692X_REG_SYSCTRL5,
	AW8692X_REG_PWMCFG1,
	AW8692X_REG_PWMCFG2,
	AW8692X_REG_PWMCFG3,
	AW8692X_REG_PWMCFG4,
	AW8692X_REG_VBATCTRL,
	AW8692X_REG_DETCFG1,
	AW8692X_REG_DETCFG2,
	AW8692X_REG_DETRD1,
	AW8692X_REG_DETRD2,
	AW8692X_REG_DETRD3,
	AW8692X_REG_TRIMCFG1,
	AW8692X_REG_TRIMCFG2,
	AW8692X_REG_TRIMCFG3,
	AW8692X_REG_TRIMCFG4,
	AW8692X_REG_IDH,
	AW8692X_REG_IDL,
	AW8692X_REG_AUTOSIN1,
	AW8692X_REG_AUTOSIN2,
	AW8692X_REG_ANACFG20,
};

static void aw8692x_tm_config(struct aw_haptic *aw_haptic, uint8_t type)
{
	uint8_t reg_val = 0;
	if (type == AW_LOCK) {
		reg_val = AW8692X_BIT_TMCFG_TM_LOCK;
	} else if (type == AW_UNLOCK) {
		reg_val = AW8692X_BIT_TMCFG_TM_UNLOCK;
	} else {
		aw_dev_err("%s: type is error\n", __func__);
		return;
	}
	i2c_w_bytes(aw_haptic, AW8692X_REG_TMCFG, &reg_val,
		    AW_I2C_BYTE_ONE);
}

static void aw8692x_set_pwm(struct aw_haptic *aw_haptic, uint8_t mode)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	switch (mode) {
	case AW_PWM_48K:
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL4,
			   AW8692X_BIT_SYSCTRL4_WAVDAT_MODE_MASK,
			   AW8692X_BIT_SYSCTRL4_WAVDAT_48K);
		break;
	case AW_PWM_24K:
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL4,
			   AW8692X_BIT_SYSCTRL4_WAVDAT_MODE_MASK,
			   AW8692X_BIT_SYSCTRL4_WAVDAT_24K);
		break;
	case AW_PWM_12K:
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL4,
			   AW8692X_BIT_SYSCTRL4_WAVDAT_MODE_MASK,
			   AW8692X_BIT_SYSCTRL4_WAVDAT_12K);
		break;
	default:
		aw_dev_err("%s: error param!\n", __func__);
		break;
	}
}

static void aw8692x_set_gain(struct aw_haptic *aw_haptic, uint8_t gain)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_w_bytes(aw_haptic, AW8692X_REG_PLAYCFG2, &gain, AW_I2C_BYTE_ONE);
}

static void aw8692x_set_bst_peak_cur(struct aw_haptic *aw_haptic)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	aw8692x_tm_config(aw_haptic, AW_UNLOCK);
	i2c_w_bits(aw_haptic, AW8692X_REG_ANACFG13,
			   AW8692X_BIT_BSTCFG1_BST_PC_MASK,
			   AW8692X_BIT_BSTCFG1_BST_PEAKCUR_3P5A);
	aw8692x_tm_config(aw_haptic, AW_LOCK);
}

static void aw8692x_set_bst_vol(struct aw_haptic *aw_haptic, uint8_t bst_vol)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	if (bst_vol & 0x80)
		bst_vol = 0x7F;
	if (bst_vol < AW8692X_BIT_PLAYCFG1_BST_VOUT_6V)
		bst_vol = AW8692X_BIT_PLAYCFG1_BST_VOUT_6V;
	i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG1,
		   AW8692X_BIT_PLAYCFG1_BST_VOUT_VREFSET_MASK, bst_vol);
}

static void aw8692x_set_wav_seq(struct aw_haptic *aw_haptic, uint8_t wav,
				uint8_t seq)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_w_bytes(aw_haptic, AW8692X_REG_WAVCFG1 + wav, &seq,
		    AW_I2C_BYTE_ONE);
}

static void aw8692x_set_wav_loop(struct aw_haptic *aw_haptic, uint8_t wav,
				 uint8_t loop)
{
	uint8_t tmp = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	if (wav % 2) {
		tmp = loop << 0;
		i2c_w_bits(aw_haptic, AW8692X_REG_WAVCFG9 + (wav / 2),
			   AW8692X_BIT_WAVLOOP_SEQ_EVEN_MASK, tmp);
	} else {
		tmp = loop << 4;
		i2c_w_bits(aw_haptic, AW8692X_REG_WAVCFG9 + (wav / 2),
			   AW8692X_BIT_WAVLOOP_SEQ_ODD_MASK, tmp);
	}
}

static void aw8692x_set_rtp_data(struct aw_haptic *aw_haptic, uint8_t *data,
				 uint32_t len)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_w_bytes(aw_haptic, AW8692X_REG_RTPDATA, data, len);
}

static void aw8692x_set_ram_data(struct aw_haptic *aw_haptic, uint8_t *data,
				 uint32_t len)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_w_bytes(aw_haptic, AW8692X_REG_RAMDATA, data, len);
}

static void aw8692x_set_rtp_aei(struct aw_haptic *aw_haptic, bool flag)
{
	aw_dev_dbg("%s: enter!\n", __func__);

	if (flag) {
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSINTM,
			   AW8692X_BIT_SYSINTM_FF_AEM_MASK,
			   AW8692X_BIT_SYSINTM_FF_AEM_ON);
	} else {
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSINTM,
			   AW8692X_BIT_SYSINTM_FF_AEM_MASK,
			   AW8692X_BIT_SYSINTM_FF_AEM_OFF);
	}
}

static void aw8692x_set_ram_addr(struct aw_haptic *aw_haptic,
				 uint32_t base_addr)
{
	uint8_t ram_addr[2] = {0};

	ram_addr[0] = (uint8_t)AW8692X_SET_RAMADDR_H(base_addr);
	ram_addr[1] = (uint8_t)AW8692X_SET_RAMADDR_L(base_addr);

	i2c_w_bytes(aw_haptic, AW8692X_REG_RAMADDRH, ram_addr,
		    AW_I2C_BYTE_TWO);
}

static void aw8692x_set_base_addr(struct aw_haptic *aw_haptic)
{
	uint8_t rtp_addr[2] = {0};
	uint32_t base_addr = aw_haptic->ram.base_addr;

	rtp_addr[0] = (uint8_t)AW8692X_SET_BASEADDR_H(base_addr);
	rtp_addr[1] = (uint8_t)AW8692X_SET_BASEADDR_L(base_addr);

	i2c_w_bits(aw_haptic, AW8692X_REG_RTPCFG1,
		   AW8692X_BIT_RTPCFG1_BASE_ADDR_H_MASK,
		   rtp_addr[0]);
	i2c_w_bytes(aw_haptic, AW8692X_REG_RTPCFG2, &rtp_addr[1],
		    AW_I2C_BYTE_ONE);
}

static void aw8692x_auto_break_mode(struct aw_haptic *aw_haptic, bool flag)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	if (flag) {
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_BRK_EN_MASK,
			   AW8692X_BIT_PLAYCFG3_BRK_ENABLE);
	} else {
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_BRK_EN_MASK,
			   AW8692X_BIT_PLAYCFG3_BRK_DISABLE);
	}
}

static void aw8692x_f0_detect(struct aw_haptic *aw_haptic, bool flag)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	if (flag) {
		i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG1,
			   AW8692X_BIT_CONTCFG1_EN_F0_DET_MASK,
			   AW8692X_BIT_CONTCFG1_F0_DET_ENABLE);
	} else {
		i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG1,
			   AW8692X_BIT_CONTCFG1_EN_F0_DET_MASK,
			   AW8692X_BIT_CONTCFG1_F0_DET_DISABLE);
	}
}

static uint8_t aw8692x_get_glb_state(struct aw_haptic *aw_haptic)
{
	uint8_t state = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_GLBRD5, &state, AW_I2C_BYTE_ONE);
	return state;
}

static uint8_t aw8692x_get_chip_state(struct aw_haptic *aw_haptic)
{
	uint8_t chip_state_val = 0;

	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSST, &chip_state_val,
		    AW_I2C_BYTE_ONE);
	return chip_state_val;
}

static uint8_t aw8692x_read_irq_state(struct aw_haptic *aw_haptic)
{
	uint8_t irq_state_val = 0;

	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSINT, &irq_state_val,
		    AW_I2C_BYTE_ONE);
	return irq_state_val;
}

static void aw8692x_play_go(struct aw_haptic *aw_haptic, bool flag)
{
	uint8_t go_on = AW8692X_BIT_PLAYCFG4_GO_ON;
	uint8_t stop_on = AW8692X_BIT_PLAYCFG4_STOP_ON;

	aw_dev_info("%s: enter, flag = %d\n", __func__, flag);
	if (flag)
		i2c_w_bytes(aw_haptic, AW8692X_REG_PLAYCFG4, &go_on,
			    AW_I2C_BYTE_ONE);
	else
		i2c_w_bytes(aw_haptic, AW8692X_REG_PLAYCFG4, &stop_on,
			    AW_I2C_BYTE_ONE);
}

static int aw8692x_wait_enter_standby(struct aw_haptic *aw_haptic,
				       unsigned int cnt)
{
	int i = cnt;
	uint8_t reg_val = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	while (i) {
		reg_val = aw8692x_get_glb_state(aw_haptic);
		if (reg_val == AW8692X_BIT_GLBRD5_STATE_STANDBY) {
			aw_dev_info("%s: entered standby!\n",
				    __func__);
			return 0;
		}
		i--;
		aw_dev_dbg("%s: wait for standby\n",
			    __func__);
		usleep_range(2000, 2500);
	}

	return -ERANGE;
}

static void aw8692x_bst_mode_config(struct aw_haptic *aw_haptic, uint8_t mode)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	switch (mode) {
	case AW_BST_BOOST_MODE:
		aw_dev_info("%s: haptic bst mode = bst\n",
			    __func__);
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG1,
			   AW8692X_BIT_PLAYCFG1_BST_MODE_MASK,
			   AW8692X_BIT_PLAYCFG1_BST_MODE);
		break;
	case AW_BST_BYPASS_MODE:
		aw_dev_info("%s: haptic bst mode = bypass\n",
			    __func__);
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG1,
			   AW8692X_BIT_PLAYCFG1_BST_MODE_MASK,
			   AW8692X_BIT_PLAYCFG1_BST_MODE_BYPASS);
		break;
	default:
		aw_dev_err("%s: bst = %d error",
			   __func__, mode);
		break;
	}
}

static void aw8692x_play_mode(struct aw_haptic *aw_haptic, uint8_t play_mode)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	switch (play_mode) {
	case AW_STANDBY_MODE:
		aw_dev_info("%s: enter standby mode\n",
			    __func__);
		aw_haptic->play_mode = AW_STANDBY_MODE;
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL3,
			   AW8692X_BIT_SYSCTRL3_STANDBY_MASK,
			   AW8692X_BIT_SYSCTRL3_STANDBY_ON);
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL3,
			   AW8692X_BIT_SYSCTRL3_STANDBY_MASK,
			   AW8692X_BIT_SYSCTRL3_STANDBY_OFF);
		break;
	case AW_RAM_MODE:
		aw_dev_info("%s: enter ram mode\n", __func__);
		aw_haptic->play_mode = AW_RAM_MODE;
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_MASK,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		/* bst mode */
		aw8692x_bst_mode_config(aw_haptic, AW_BST_BOOST_MODE);
		break;
	case AW_RAM_LOOP_MODE:
		aw_dev_info("%s: enter ram loop mode\n",
			    __func__);
		aw_haptic->play_mode = AW_RAM_LOOP_MODE;
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_MASK,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		/* bst mode */
		/* bst mode Already configured in vibrator_work_routine func, close here */
		//aw8692x_bst_mode_config(aw_haptic, AW_BST_BYPASS_MODE);
		break;
	case AW_RTP_MODE:
		aw_dev_info("%s: enter rtp mode\n", __func__);
		aw_haptic->play_mode = AW_RTP_MODE;
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_MASK,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_RTP);
		/* bst mode */
		aw8692x_bst_mode_config(aw_haptic, AW_BST_BOOST_MODE);
		break;
	case AW_TRIG_MODE:
		aw_dev_info("%s: enter trig mode\n", __func__);
		aw_haptic->play_mode = AW_TRIG_MODE;
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_MASK,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		break;
	case AW_CONT_MODE:
		aw_dev_info("%s: enter cont mode\n", __func__);
		aw_haptic->play_mode = AW_CONT_MODE;
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_MASK,
			   AW8692X_BIT_PLAYCFG3_PLAY_MODE_CONT);
		/* bst mode */
		aw8692x_bst_mode_config(aw_haptic, AW_BST_BYPASS_MODE);
		break;
	default:
		aw_dev_err("%s: play mode %d error",
			   __func__, play_mode);
		break;
	}
}

static void aw8692x_stop(struct aw_haptic *aw_haptic)
{
	unsigned char reg_val = 0;
	int ret = 0;

	aw_dev_dbg("%s: enter\n", __func__);
	aw_haptic->play_mode = AW_STANDBY_MODE;
	reg_val = AW8692X_BIT_PLAYCFG4_STOP_ON;
	i2c_w_bytes(aw_haptic, AW8692X_REG_PLAYCFG4, &reg_val, AW_I2C_BYTE_ONE);
	ret = aw8692x_wait_enter_standby(aw_haptic, 40);
	if (ret < 0) {
		aw_dev_err("%s force to enter standby mode!\n",
			   __func__);
		aw8692x_play_mode(aw_haptic, AW_STANDBY_MODE);
	}
}

static void aw8692x_ram_init(struct aw_haptic *aw_haptic, bool flag)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	if (flag) {
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL3,
			   AW8692X_BIT_SYSCTRL3_EN_RAMINIT_MASK,
			   AW8692X_BIT_SYSCTRL3_EN_RAMINIT_ON);
	} else {
		i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL3,
			   AW8692X_BIT_SYSCTRL3_EN_RAMINIT_MASK,
			   AW8692X_BIT_SYSCTRL3_EN_RAMINIT_OFF);
	}
}

static void aw8692x_upload_lra(struct aw_haptic *aw_haptic, uint32_t flag)
{
	uint8_t cali_data = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	aw8692x_tm_config(aw_haptic, AW_UNLOCK);
	switch (flag) {
	case AW_WRITE_ZERO:
		aw_dev_info("%s: write zero to trim_lra!\n",
			    __func__);
		i2c_w_bits(aw_haptic, AW8692X_REG_ANACFG20,
			   AW8692X_BIT_ANACFG20_TRIM_LRA_MASK, cali_data);
		break;
	case AW_F0_CALI_LRA:
		aw_dev_info("%s: write f0_cali_data to trim_lra = 0x%02X\n",
			    __func__, aw_haptic->f0_cali_data);
		cali_data = (char)aw_haptic->f0_cali_data &
					AW8692X_BIT_ANACFG20_TRIM_LRA;
		i2c_w_bytes(aw_haptic, AW8692X_REG_ANACFG20, &cali_data,
			    AW_I2C_BYTE_ONE);
		break;
	case AW_OSC_CALI_LRA:
		aw_dev_info("%s: write osc_cali_data to trim_lra = 0x%02X\n",
			    __func__, aw_haptic->osc_cali_data);
		cali_data = (char)aw_haptic->osc_cali_data &
			    AW8692X_BIT_ANACFG20_TRIM_LRA;
		i2c_w_bytes(aw_haptic, AW8692X_REG_ANACFG20, &cali_data,
			    AW_I2C_BYTE_ONE);
		break;
	default:
		aw_dev_err("%s: error param!\n", __func__);
		break;
	}
	aw8692x_tm_config(aw_haptic, AW_LOCK);
}

static void aw8692x_vbat_mode_config(struct aw_haptic *aw_haptic, uint8_t flag)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	if (flag == AW_CONT_VBAT_HW_COMP_MODE) {
		i2c_w_bits(aw_haptic, AW8692X_REG_VBATCTRL,
			   AW8692X_BIT_VBATCTRL_VBAT_MODE_MASK,
			   AW8692X_BIT_VBATCTRL_VBAT_MODE_HW);
	} else {
		i2c_w_bits(aw_haptic, AW8692X_REG_VBATCTRL,
			   AW8692X_BIT_VBATCTRL_VBAT_MODE_MASK,
			   AW8692X_BIT_VBATCTRL_VBAT_MODE_SW);
	}
}

static void aw8692x_protect_config(struct aw_haptic *aw_haptic, uint8_t addr,
				  uint8_t val)
{
	aw_dev_info("%s: enter\n", __func__);
	if (addr == 1) {
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG2,
			   AW8692X_BIT_PWMCFG2_PRCT_MODE_MASK,
			   AW8692X_BIT_PWMCFG2_PRCT_MODE_VALID);
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG1,
			   AW8692X_BIT_PWMCFG1_PRC_EN_MASK,
			   AW8692X_BIT_PWMCFG1_PRC_ENABLE);
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG3,
			   AW8692X_BIT_PWMCFG3_PR_EN_MASK,
			   AW8692X_BIT_PWMCFG3_PR_ENABLE);
	} else if (addr == 0) {
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG2,
			   AW8692X_BIT_PWMCFG2_PRCT_MODE_MASK,
			   AW8692X_BIT_PWMCFG2_PRCT_MODE_INVALID);
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG1,
			   AW8692X_BIT_PWMCFG1_PRC_EN_MASK,
			   AW8692X_BIT_PWMCFG1_PRC_DISABLE);
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG3,
			   AW8692X_BIT_PWMCFG3_PR_EN_MASK,
			   AW8692X_BIT_PWMCFG3_PR_DISABLE);
	} else if (addr == 0x4C) {
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG1,
			   AW8692X_BIT_PWMCFG1_PRCTIME_MASK, val);
	} else if (addr == 0x4E) {
		i2c_w_bits(aw_haptic, AW8692X_REG_PWMCFG3,
			   AW8692X_BIT_PWMCFG3_PRLVL_MASK, val);
	} else if (addr == 0x4F) {
		i2c_w_bytes(aw_haptic, AW8692X_REG_PWMCFG4, &val,
			    AW_I2C_BYTE_ONE);
	}
}

static void aw8692x_cont_config(struct aw_haptic *aw_haptic)
{
	/* uint8_t drv1_time = 0xFF; */
	uint8_t drv2_time = 0xFF;

	aw_dev_info("%s: enter\n", __func__);
	/* work mode */
	aw8692x_play_mode(aw_haptic, AW_CONT_MODE);
	i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG6,
		   AW8692X_BIT_CONTCFG6_TRACK_EN_MASK,
		   AW8692X_BIT_CONTCFG6_TRACK_ENABLE);
	/* f0 driver level */
	i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG6,
		   AW8692X_BIT_CONTCFG6_DRV1_LVL_MASK,
		   aw_haptic->info.cont_drv1_lvl);
	i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG7,
		    &aw_haptic->info.cont_drv2_lvl, AW_I2C_BYTE_ONE);
	/* DRV1_TIME */
	/* i2c_w_byte(aw_haptic, AW8692X_REG_CONTCFG8, &drv1_time,
	 *	      AW_I2C_BYTE_ONE);
	 */
	/* DRV2_TIME */
	i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG9, &drv2_time,
		    AW_I2C_BYTE_ONE);
	/* cont play go */
	aw8692x_play_go(aw_haptic, true);
}

static void aw8692x_one_wire_init(struct aw_haptic *aw_haptic)
{
	uint8_t trig_prio = 0x6c;
	uint8_t delay_2p5ms = AW8692X_BIT_START_DLY_2P5MS;

	aw_dev_info("%s: enter\n", __func__);
	/*if enable one-wire, trig1 priority must be less than trig2 and trig3*/
	i2c_w_bytes(aw_haptic, AW8692X_REG_GLBCFG4, &trig_prio,
		    AW_I2C_BYTE_ONE);
	i2c_w_bytes(aw_haptic, AW8692X_REG_GLBCFG2, &delay_2p5ms,
		    AW_I2C_BYTE_ONE);
	i2c_w_bits(aw_haptic, AW8692X_REG_TRGCFG8,
		   AW8692X_BIT_TRGCFG8_TRG_ONEWIRE_MASK,
		   AW8692X_BIT_TRGCFG8_TRG_ONEWIRE_ENABLE);
}

static void aw8692x_trig1_param_init(struct aw_haptic *aw_haptic)
{
	aw_dev_info("%s: enter\n", __func__);
	aw_haptic->trig[0].trig_level = AW8692X_TRIG1_DUAL_LEVEL;
	aw_haptic->trig[0].trig_polar = AW8692X_TRIG1_DUAL_POLAR;
	aw_haptic->trig[0].pos_enable = AW8692X_TRIG1_POS_DISABLE;
	aw_haptic->trig[0].pos_sequence = AW8692X_TRIG1_POS_SEQ;
	aw_haptic->trig[0].neg_enable = AW8692X_TRIG1_NEG_DISABLE;
	aw_haptic->trig[0].neg_sequence = AW8692X_TRIG1_NEG_SEQ;
	aw_haptic->trig[0].trig_brk = AW8692X_TRIG1_BRK_DISABLE;
	aw_haptic->trig[0].trig_bst = AW8692X_TRIG1_BST_DISABLE;
}

static void aw8692x_trig2_param_init(struct aw_haptic *aw_haptic)
{
	aw_dev_info("%s: enter\n", __func__);
	aw_haptic->trig[1].trig_level = AW8692X_TRIG2_DUAL_LEVEL;
	aw_haptic->trig[1].trig_polar = AW8692X_TRIG2_DUAL_POLAR;
	aw_haptic->trig[1].pos_enable = AW8692X_TRIG2_POS_DISABLE;
	aw_haptic->trig[1].pos_sequence = AW8692X_TRIG2_POS_SEQ;
	aw_haptic->trig[1].neg_enable = AW8692X_TRIG2_NEG_DISABLE;
	aw_haptic->trig[1].neg_sequence = AW8692X_TRIG2_NEG_SEQ;
	aw_haptic->trig[1].trig_brk = AW8692X_TRIG2_BRK_DISABLE;
	aw_haptic->trig[1].trig_bst = AW8692X_TRIG2_BST_DISABLE;
}

static void aw8692x_trig3_param_init(struct aw_haptic *aw_haptic)
{
	aw_dev_info("%s: enter\n", __func__);
	aw_haptic->trig[2].trig_level = AW8692X_TRIG3_DUAL_LEVEL;
	aw_haptic->trig[2].trig_polar = AW8692X_TRIG3_DUAL_POLAR;
	aw_haptic->trig[2].pos_enable = AW8692X_TRIG3_POS_DISABLE;
	aw_haptic->trig[2].pos_sequence = AW8692X_TRIG3_POS_SEQ;
	aw_haptic->trig[2].neg_enable = AW8692X_TRIG3_NEG_DISABLE;
	aw_haptic->trig[2].neg_sequence = AW8692X_TRIG3_NEG_SEQ;
	aw_haptic->trig[2].trig_brk = AW8692X_TRIG3_BRK_DISABLE;
	aw_haptic->trig[2].trig_bst = AW8692X_TRIG3_BST_DISABLE;
}

static void aw8692x_trig1_param_config(struct aw_haptic *aw_haptic)
{
	uint8_t trig_config = 0;

	aw_dev_info("%s: enter\n", __func__);

	if (aw_haptic->trig[0].trig_level)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_MODE_LEVEL;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_MODE_EDGE;


	if (aw_haptic->trig[0].trig_polar)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_POLAR_NEG;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_POLAR_POS;


	if (aw_haptic->trig[0].trig_brk)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_AUTO_BRK_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_AUTO_BRK_DISABLE;


	if (aw_haptic->trig[0].trig_bst)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_BST_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG1_BST_DISABLE;


	i2c_w_bits(aw_haptic, AW8692X_REG_TRGCFG7,
		   (AW8692X_BIT_TRGCFG7_TRG1_MODE_MASK &
		    AW8692X_BIT_TRGCFG7_TRG1_POLAR_MASK &
		    AW8692X_BIT_TRGCFG7_TRG1_AUTO_BRK_MASK &
		    AW8692X_BIT_TRGCFG7_TRG1_BST_MASK), trig_config);

	trig_config = 0;

	/* pos config */
	if (aw_haptic->trig[0].pos_enable)
		trig_config |= AW8692X_BIT_TRG_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRG_DISABLE;

	trig_config |= aw_haptic->trig[0].pos_sequence;
	i2c_w_bytes(aw_haptic, AW8692X_REG_TRGCFG1, &trig_config,
		    AW_I2C_BYTE_ONE);

	trig_config = 0;

	/* neq config */
	if (aw_haptic->trig[0].neg_enable)
		trig_config |= AW8692X_BIT_TRG_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRG_DISABLE;

	trig_config |= aw_haptic->trig[0].neg_sequence;
	i2c_w_bytes(aw_haptic, AW8692X_REG_TRGCFG4, &trig_config,
		    AW_I2C_BYTE_ONE);
}

static void aw8692x_trig2_param_config(struct aw_haptic *aw_haptic)
{
	uint8_t trig_config = 0;

	aw_dev_info("%s: enter\n", __func__);

	if (aw_haptic->trig[1].trig_level)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_MODE_LEVEL;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_MODE_EDGE;


	if (aw_haptic->trig[1].trig_polar)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_POLAR_NEG;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_POLAR_POS;


	if (aw_haptic->trig[1].trig_brk)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_AUTO_BRK_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_AUTO_BRK_DISABLE;


	if (aw_haptic->trig[1].trig_bst)
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_BST_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRGCFG7_TRG2_BST_DISABLE;


	i2c_w_bits(aw_haptic, AW8692X_REG_TRGCFG7,
		   (AW8692X_BIT_TRGCFG7_TRG2_MODE_MASK &
		    AW8692X_BIT_TRGCFG7_TRG2_POLAR_MASK &
		    AW8692X_BIT_TRGCFG7_TRG2_AUTO_BRK_MASK &
		    AW8692X_BIT_TRGCFG7_TRG2_BST_MASK), trig_config);

	trig_config = 0;


	/* pos config */
	if (aw_haptic->trig[1].pos_enable)
		trig_config |= AW8692X_BIT_TRG_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRG_DISABLE;

	trig_config |= aw_haptic->trig[1].pos_sequence;
	i2c_w_bytes(aw_haptic, AW8692X_REG_TRGCFG2, &trig_config,
		    AW_I2C_BYTE_ONE);

	trig_config = 0;

	/* neq config */
	if (aw_haptic->trig[1].neg_enable)
		trig_config |= AW8692X_BIT_TRG_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRG_DISABLE;

	trig_config |= aw_haptic->trig[1].neg_sequence;
	i2c_w_bytes(aw_haptic, AW8692X_REG_TRGCFG5, &trig_config,
		    AW_I2C_BYTE_ONE);
}

static void aw8692x_trig3_param_config(struct aw_haptic *aw_haptic)
{
	uint8_t trig_config = 0;

	aw_dev_info("%s: enter\n", __func__);

	if (aw_haptic->trig[2].trig_level)
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_MODE_LEVEL;
	else
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_MODE_EDGE;


	if (aw_haptic->trig[2].trig_polar)
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_POLAR_NEG;
	else
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_POLAR_POS;


	if (aw_haptic->trig[2].trig_brk)
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_AUTO_BRK_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_AUTO_BRK_DISABLE;


	if (aw_haptic->trig[2].trig_bst)
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_BST_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRGCFG8_TRG3_BST_DISABLE;


	i2c_w_bits(aw_haptic, AW8692X_REG_TRGCFG8,
		   (AW8692X_BIT_TRGCFG8_TRG3_MODE_MASK &
		    AW8692X_BIT_TRGCFG8_TRG3_POLAR_MASK &
		    AW8692X_BIT_TRGCFG8_TRG3_AUTO_BRK_MASK &
		    AW8692X_BIT_TRGCFG8_TRG3_BST_MASK), trig_config);

	trig_config = 0;


	/* pos config */
	if (aw_haptic->trig[2].pos_enable)
		trig_config |= AW8692X_BIT_TRG_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRG_DISABLE;

	trig_config |= aw_haptic->trig[2].pos_sequence;
	i2c_w_bytes(aw_haptic, AW8692X_REG_TRGCFG3, &trig_config,
		    AW_I2C_BYTE_ONE);

	trig_config = 0;

	/* neq config */
	if (aw_haptic->trig[2].neg_enable)
		trig_config |= AW8692X_BIT_TRG_ENABLE;
	else
		trig_config |= AW8692X_BIT_TRG_DISABLE;

	trig_config |= aw_haptic->trig[2].neg_sequence;
	i2c_w_bytes(aw_haptic, AW8692X_REG_TRGCFG6, &trig_config,
		    AW_I2C_BYTE_ONE);
}

static void aw8692x_auto_bst_enable(struct aw_haptic *aw_haptic, uint8_t flag)
{
	aw_haptic->auto_boost = flag;
	aw_dev_info("%s: enter\n", __func__);
	if (flag) {
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_AUTO_BST_MASK,
			   AW8692X_BIT_PLAYCFG3_AUTO_BST_ENABLE);
	} else {
		i2c_w_bits(aw_haptic, AW8692X_REG_PLAYCFG3,
			   AW8692X_BIT_PLAYCFG3_AUTO_BST_MASK,
			   AW8692X_BIT_PLAYCFG3_AUTO_BST_DISABLE);
	}
}

static void aw8692x_interrupt_setup(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;

	aw_dev_dbg("%s: enter\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSINT, &reg_val, AW_I2C_BYTE_ONE);
	aw_dev_info("%s: reg SYSINT=0x%02X\n",
		    __func__, reg_val);
	/* edge int mode */
	i2c_w_bits(aw_haptic, AW8692X_REG_SYSCTRL4,
		   (AW8692X_BIT_SYSCTRL4_INT_MODE_MASK &
		    AW8692X_BIT_SYSCTRL4_INT_EDGE_MODE_MASK),
		   (AW8692X_BIT_SYSCTRL4_INT_MODE_EDGE |
		    AW8692X_BIT_SYSCTRL4_INT_EDGE_MODE_POS));
	/* int enable */
	i2c_w_bits(aw_haptic, AW8692X_REG_SYSINTM,
		   (AW8692X_BIT_SYSINTM_BST_SCPM_MASK &
		    AW8692X_BIT_SYSINTM_BST_OVPM_MASK &
		    AW8692X_BIT_SYSINTM_UVLM_MASK &
		    AW8692X_BIT_SYSINTM_OCDM_MASK &
		    AW8692X_BIT_SYSINTM_OTM_MASK),
		   (AW8692X_BIT_SYSINTM_BST_SCPM_OFF |
		    AW8692X_BIT_SYSINTM_BST_OVPM_OFF |
		    AW8692X_BIT_SYSINTM_UVLM_ON |
		    AW8692X_BIT_SYSINTM_OCDM_ON |
		    AW8692X_BIT_SYSINTM_OTM_ON));

}

static int aw8692x_juge_rtp_going(struct aw_haptic *aw_haptic)
{
	uint8_t glb_state = 0;
	uint8_t rtp_state = 0;

	glb_state = aw8692x_get_glb_state(aw_haptic);
	if (aw_haptic->rtp_routine_on
		|| (glb_state == AW8692X_BIT_GLBRD5_STATE_RTP_GO)) {
		rtp_state = 1;  /*is going on */
		aw_dev_info("%s: rtp_routine_on\n", __func__);
	}
	return rtp_state;
}

static void aw8692x_get_ram_data(struct aw_haptic *aw_haptic,
				    uint8_t *data, uint32_t size)
{
	i2c_r_bytes(aw_haptic, AW8692X_REG_RAMDATA, data, size);
}

static void aw8692x_get_first_wave_addr(struct aw_haptic *aw_haptic,
					uint8_t *wave_addr)
{
	uint8_t reg_array[3] = {0};

	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_RAMDATA, reg_array,
		    AW_I2C_BYTE_THREE);
	wave_addr[0] = reg_array[1];
	wave_addr[1] = reg_array[2];
}

static void aw8692x_get_wav_seq(struct aw_haptic *aw_haptic, uint8_t *seq,
				uint8_t len)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_WAVCFG1, seq, len);
}

static size_t aw8692x_get_wav_loop(struct aw_haptic *aw_haptic, char *buf)
{
	uint8_t i = 0;
	uint8_t reg_val[AW_SEQUENCER_LOOP_SIZE] = {0};
	size_t count = 0;

	i2c_r_bytes(aw_haptic, AW8692X_REG_WAVCFG9, reg_val,
		    AW_SEQUENCER_LOOP_SIZE);

	for (i = 0; i < AW_SEQUENCER_LOOP_SIZE; i++) {
		aw_haptic->loop[i * 2 + 0] = (reg_val[i] >> 4) & 0x0F;
		aw_haptic->loop[i * 2 + 1] = (reg_val[i] >> 0) & 0x0F;

		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d loop: 0x%02x\n", i * 2 + 1,
				  aw_haptic->loop[i * 2 + 0]);
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d loop: 0x%02x\n", i * 2 + 2,
				  aw_haptic->loop[i * 2 + 1]);
	}
	return count;
}

static void aw8692x_irq_clear(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;

	aw_dev_info("%s: enter\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSINT, &reg_val, AW_I2C_BYTE_ONE);
	aw_dev_dbg("%s: reg SYSINT=0x%02X\n",
		   __func__, reg_val);
}

static uint8_t aw8692x_get_prctmode(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_PWMCFG2, &reg_val, AW_I2C_BYTE_ONE);
	reg_val &= 0x08;
	return reg_val;
}

static void aw8692x_op_clean_status(struct aw_haptic *aw_haptic)
{
	aw_haptic->audio_ready = false;
	aw_haptic->haptic_ready = false;
	aw_haptic->pre_haptic_number = 0;
	aw_haptic->rtp_routine_on = 0;

	aw_dev_info("%s enter\n", __func__);
}

static int aw8692x_get_irq_state(struct aw_haptic *aw_haptic)
{
	int ret = -1;
	uint8_t reg_val = 0;

	aw_dev_dbg("%s: enter\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSINT, &reg_val, AW_I2C_BYTE_ONE);
	aw_dev_dbg("%s: reg SYSINT=0x%02X\n",
		    __func__, reg_val);

	if (reg_val & AW8692X_BIT_SYSINT_BST_OVPI) {
		aw8692x_op_clean_status(aw_haptic);
		aw_dev_err("%s: chip ov int error\n", __func__);
	}

	if (reg_val & AW8692X_BIT_SYSINT_UVLI) {
		aw8692x_op_clean_status(aw_haptic);
		aw_dev_err("%s: chip uvlo int error\n",
			   __func__);
	}

	if (reg_val & AW8692X_BIT_SYSINT_OCDI) {
		aw8692x_op_clean_status(aw_haptic);
		aw_dev_err("%s: chip over current int error\n",
			   __func__);
	}

	if (reg_val & AW8692X_BIT_SYSINT_OTI) {
		aw8692x_op_clean_status(aw_haptic);
		aw_dev_err("%s: chip over temperature int error\n", __func__);
	}

	if (reg_val & AW8692X_BIT_SYSINT_DONEI) {
		aw8692x_op_clean_status(aw_haptic);
		aw_dev_info("%s: chip playback done\n",
			    __func__);
	}

	if (reg_val & AW8692X_BIT_SYSINT_FF_AEI)
		ret = 0;

	if (reg_val & AW8692X_BIT_SYSINT_FF_AFI)
		aw_dev_info("%s: aw_haptic rtp mode fifo almost full!\n",
			    __func__);
	return ret;
}

static void aw8692x_read_f0(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val[4] = {0};
	uint32_t f0_reg = 0;
	unsigned long f0_tmp = 0;

	aw_dev_info("%s: enter\n", __func__);
	/* lra_f0 */
	i2c_r_bytes(aw_haptic, AW8692X_REG_CONTCFG14, reg_val, 4);
	f0_reg = (reg_val[0] << 8) | reg_val[1];
	if (!f0_reg) {
		aw_dev_err("%s: lra_f0 is error, f0_reg=0\n",
			   __func__);
		return;
	}
	f0_tmp = 384000 * 10 / f0_reg;
	aw_haptic->f0 = (uint32_t)f0_tmp;
	aw_dev_info("%s: lra_f0=%d\n", __func__, aw_haptic->f0);

	/* cont_f0 */
	f0_reg = (reg_val[2] << 8) | reg_val[3];
	if (!f0_reg) {
		aw_dev_err("%s: cont_f0 is error, f0_reg=0\n",
			   __func__);
		return;
	}
	f0_tmp = 384000 * 10 / f0_reg;
	aw_haptic->cont_f0 = (uint32_t)f0_tmp;
	aw_dev_info("%s: cont_f0=%d\n", __func__,
		    aw_haptic->cont_f0);
}

static int aw8692x_get_f0(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;
	uint8_t brk_en_default = 0;
	uint8_t reg_array[3] = {aw_haptic->info.cont_drv2_lvl,
				aw_haptic->info.cont_drv1_time,
				aw_haptic->info.cont_drv2_time};

	aw_dev_info("%s: enter\n", __func__);
	aw_haptic->f0 = aw_haptic->info.f0_pre;
	/* enter standby mode */
	aw8692x_stop(aw_haptic);
	/* f0 calibrate work mode */
	aw8692x_play_mode(aw_haptic, AW_CONT_MODE);
	/* enable f0 detect */
	aw8692x_f0_detect(aw_haptic, true);
	/* cont config */
	i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG6,
		   AW8692X_BIT_CONTCFG6_TRACK_EN_MASK,
		   AW8692X_BIT_CONTCFG6_TRACK_ENABLE);
	/* enable auto break */
	i2c_r_bytes(aw_haptic, AW8692X_REG_PLAYCFG3, &reg_val, AW_I2C_BYTE_ONE);
	brk_en_default = 0x04 & reg_val;
	aw8692x_auto_break_mode(aw_haptic, true);

	/* f0 driver level */
	i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG6,
		   AW8692X_BIT_CONTCFG6_DRV1_LVL_MASK,
		   aw_haptic->info.cont_drv1_lvl);
	i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG7, reg_array,
		    AW_I2C_BYTE_THREE);
	/* TRACK_MARGIN */
	if (!aw_haptic->info.cont_track_margin) {
		aw_dev_err("%s: aw_haptic->info.cont_track_margin = 0!\n",
			   __func__);
	} else {
		i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG11,
			    &aw_haptic->info.cont_track_margin,
			    AW_I2C_BYTE_ONE);
	}
	/* DRV_WIDTH */
	/*i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG3,
	*		 &aw_haptic->info.cont_drv_width, AW_I2C_BYTE_ONE);
	*/

	/* cont play go */
	aw8692x_play_go(aw_haptic, true);
	/* 300ms */
	aw8692x_wait_enter_standby(aw_haptic, 1000);
	aw8692x_read_f0(aw_haptic);
	/* restore default config */
	aw8692x_f0_detect(aw_haptic, false);
	/* recover auto break config */
	if (brk_en_default)
		aw8692x_auto_break_mode(aw_haptic, true);
	else
		aw8692x_auto_break_mode(aw_haptic, false);
	return 0;
}

static uint8_t aw8692x_rtp_get_fifo_afs(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSST, &reg_val, AW_I2C_BYTE_ONE);
	reg_val &= AW8692X_BIT_SYSST_FF_AFS;
	reg_val = reg_val >> 3;
	return reg_val;
}

static uint8_t aw8692x_rtp_get_fifo_aes(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSST, &reg_val, AW_I2C_BYTE_ONE);
	reg_val &= AW8692X_BIT_SYSST_FF_AES;
	reg_val = reg_val >> 4;
	return reg_val;
}

static uint8_t aw8692x_get_osc_status(struct aw_haptic *aw_haptic)
{
	uint8_t state = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSST2, &state, AW_I2C_BYTE_ONE);
	state &= AW8692X_BIT_SYSST2_FF_EMPTY;
	return state;
}

static int aw8692x_select_d2s_gain(unsigned char reg)
{
	int d2s_gain = 0;

	switch (reg) {
	case AW8692X_BIT_DETCFG2_D2S_GAIN_1:
		d2s_gain = 1;
		break;
	case AW8692X_BIT_DETCFG2_D2S_GAIN_2:
		d2s_gain = 2;
		break;
	case AW8692X_BIT_DETCFG2_D2S_GAIN_4:
		d2s_gain = 4;
		break;
	case AW8692X_BIT_DETCFG2_D2S_GAIN_8:
		d2s_gain = 8;
		break;
	case AW8692X_BIT_DETCFG2_D2S_GAIN_10:
		d2s_gain = 10;
		break;
	case AW8692X_BIT_DETCFG2_D2S_GAIN_16:
		d2s_gain = 16;
		break;
	case AW8692X_BIT_DETCFG2_D2S_GAIN_20:
		d2s_gain = 20;
		break;
	case AW8692X_BIT_DETCFG2_D2S_GAIN_40:
		d2s_gain = 40;
		break;
	default:
		d2s_gain = -1;
		break;
	}
	return d2s_gain;
}

static void aw8692x_get_lra_resistance(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;
	uint8_t reg_array[2] = {0};
	uint8_t mask = 0;
	uint8_t adc_fs_default = 0;
	uint8_t d2s_gain = 0;
	uint32_t lra_code = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	aw8692x_ram_init(aw_haptic, true);
	aw8692x_stop(aw_haptic);
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG2,
		   AW8692X_BIT_DETCFG2_DET_SEQ0_MASK,
		   AW8692X_BIT_DETCFG2_DET_SEQ0_RL);
	i2c_r_bytes(aw_haptic,  AW8692X_REG_DETCFG1, &reg_val, AW_I2C_BYTE_ONE);
	adc_fs_default = reg_val & AW8692X_BIT_DETCFG1_ADC_FS;
	mask = AW8692X_BIT_DETCFG1_ADC_FS_MASK &
					AW8692X_BIT_DETCFG1_DET_GO_MASK;
	reg_val = AW8692X_BIT_DETCFG1_ADC_FS_96KHZ |
					AW8692X_BIT_DETCFG1_DET_GO_DET_SEQ0;
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG1, mask, reg_val);
	usleep_range(3000, 3500);
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG1,
		   AW8692X_BIT_DETCFG1_DET_GO_MASK,
		   AW8692X_BIT_DETCFG1_DET_GO_NA);
	/* restore default config*/
	aw8692x_ram_init(aw_haptic, false);
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG1,
		   AW8692X_BIT_DETCFG1_ADC_FS_MASK, adc_fs_default);
	i2c_r_bytes(aw_haptic, AW8692X_REG_DETCFG2, &reg_val, AW_I2C_BYTE_ONE);
	reg_val &= AW8692X_BIT_DETCFG2_D2S_GAIN;
	d2s_gain = aw8692x_select_d2s_gain(reg_val);
	if (d2s_gain < 0) {
		aw_dev_err("%s d2s_gain is error\n", __func__);
		return;
	}
	i2c_r_bytes(aw_haptic, AW8692X_REG_DETRD1, reg_array, AW_I2C_BYTE_TWO);
	lra_code = ((reg_array[0] & AW8692X_BIT_DETRD1_AVG_DATA) << 8) +
								   reg_array[1];
	aw_haptic->lra = AW8692X_LRA_FORMULA(lra_code, d2s_gain);
}


static void aw8692x_set_repeat_seq(struct aw_haptic *aw_haptic, uint8_t seq)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	aw8692x_set_wav_seq(aw_haptic, 0x00, seq);
	aw8692x_set_wav_loop(aw_haptic, 0x00, AW8692X_WAVLOOP_INIFINITELY);
}

static void aw8692x_get_vbat(struct aw_haptic *aw_haptic)
{
	uint8_t reg_array[2] = {0};
	uint32_t vbat_code = 0;

	aw_dev_dbg("%s: enter!\n", __func__);
	aw8692x_stop(aw_haptic);
	aw8692x_ram_init(aw_haptic, true);
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG2,
		   AW8692X_BIT_DETCFG2_DET_SEQ0_MASK,
		   AW8692X_BIT_DETCFG2_DET_SEQ0_VBAT);
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG1,
		   AW8692X_BIT_DETCFG1_DET_GO_MASK,
		   AW8692X_BIT_DETCFG1_DET_GO_DET_SEQ0);
	usleep_range(3000, 3500);
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG1,
		   AW8692X_BIT_DETCFG1_DET_GO_MASK,
		   AW8692X_BIT_DETCFG1_DET_GO_NA);
	i2c_r_bytes(aw_haptic, AW8692X_REG_DETRD1, reg_array, AW_I2C_BYTE_TWO);
	vbat_code = ((reg_array[0] & AW8692X_BIT_DETRD1_AVG_DATA) << 8) +
								   reg_array[1];
	aw_haptic->vbat = AW8692X_VBAT_FORMULA(vbat_code);

	if (aw_haptic->vbat > AW8692X_VBAT_MAX) {
		aw_haptic->vbat = AW8692X_VBAT_MAX;
		aw_dev_info("%s: vbat max limit = %dmV\n",
			    __func__, aw_haptic->vbat);
	}
	if (aw_haptic->vbat < AW_VBAT_MIN) {
		aw_haptic->vbat = AW_VBAT_MIN;
		aw_dev_info("%s: vbat min limit = %dmV\n",
			    __func__, aw_haptic->vbat);
	}
	aw_dev_info("%s: awinic->vbat=%dmV, vbat_code=0x%02X\n",
		    __func__, aw_haptic->vbat, vbat_code);
	aw8692x_ram_init(aw_haptic, false);
}

static ssize_t aw8692x_get_reg(struct aw_haptic *aw_haptic, ssize_t len,
			       char *buf)
{
	uint8_t i = 0;
	uint8_t size = 0;
	uint8_t cnt = 0;
	uint8_t reg_array[AW8692X_REG_SUM] = {0};

	aw_dev_dbg("%s: enter!\n", __func__);

	for (i = 0; i <= (AW8692X_REG_ANACFG20 + 1); i++) {

		if (i == aw8692x_reg_list[cnt] &&
		    (cnt < sizeof(aw8692x_reg_list))) {
			size++;
			cnt++;
			continue;
		} else {
			if (size != 0) {
				i2c_r_bytes(aw_haptic,
					    aw8692x_reg_list[cnt-size],
					    &reg_array[cnt-size], size);
				size = 0;

			}
		}
	}

	for (i = 0; i < sizeof(reg_array); i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02X=0x%02X\n", aw8692x_reg_list[i],
				reg_array[i]);
	}

	return len;
}

static void aw8692x_offset_cali(struct aw_haptic *aw_haptic)
{

}

static void aw8692x_trig_init(struct aw_haptic *aw_haptic)
{
	aw_dev_dbg("%s: enter!\n", __func__);
	if (aw_haptic->info.is_enabled_one_wire) {
		aw_dev_info("%s: one wire is enabled!\n",
			    __func__);
		aw8692x_one_wire_init(aw_haptic);
	} else {
		aw8692x_trig1_param_init(aw_haptic);
		aw8692x_trig1_param_config(aw_haptic);
	}
	aw8692x_trig2_param_init(aw_haptic);
	aw8692x_trig3_param_init(aw_haptic);
	aw8692x_trig2_param_config(aw_haptic);
	aw8692x_trig3_param_config(aw_haptic);
}

#ifdef AW_CHECK_RAM_DATA
static int aw8692x_check_ram_data(struct aw_haptic *aw_haptic,
				  uint8_t *cont_data,
				  uint8_t *ram_data, uint32_t len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		if (ram_data[i] != cont_data[i]) {
			aw_dev_err("%s: check ramdata error, addr=0x%04x, ram_data=0x%02x, file_data=0x%02x\n",
				   __func__, i, ram_data[i], cont_data[i]);
			return -ERANGE;
		}
	}
	return 0;
}
#endif

static int aw8692x_container_update(struct aw_haptic *aw_haptic,
				     struct aw_haptic_container *awinic_cont)
{
	uint8_t ae_addr_h = 0;
	uint8_t af_addr_h = 0;
	uint8_t ae_addr_l = 0;
	uint8_t af_addr_l = 0;
	uint8_t reg_array[3] = {0};
	uint32_t base_addr = 0;
	uint32_t shift = 0;
	int i = 0;
	int len = 0;
	int ret = 0;

#ifdef AW_CHECK_RAM_DATA
	uint8_t ram_data[AW_RAMDATA_RD_BUFFER_SIZE] = {0};
#endif
	aw_dev_info("%s: enter\n", __func__);
	mutex_lock(&aw_haptic->lock);
	aw_haptic->ram.baseaddr_shift = 2;
	aw_haptic->ram.ram_shift = 4;
	/* RAMINIT Enable */
	aw8692x_ram_init(aw_haptic, true);
	/* Enter standby mode */
	aw8692x_stop(aw_haptic);
	/* base addr */
	shift = aw_haptic->ram.baseaddr_shift;
	aw_haptic->ram.base_addr =
	    (uint32_t)((awinic_cont->data[0 + shift] << 8) |
			   (awinic_cont->data[1 + shift]));
	base_addr = aw_haptic->ram.base_addr;
	aw_dev_info("%s: base_addr = %d\n", __func__,
		    aw_haptic->ram.base_addr);

	/* set FIFO_AE and FIFO_AF addr */
	ae_addr_h = (uint8_t)AW8692X_SET_FIFO_AE_ADDR_H(base_addr);
	af_addr_h = (uint8_t)AW8692X_SET_FIFO_AF_ADDR_H(base_addr);
	reg_array[0] = ae_addr_h | af_addr_h;
	reg_array[1] = (uint8_t)AW8692X_SET_FIFO_AE_ADDR_L(base_addr);
	reg_array[2] = (uint8_t)AW8692X_SET_FIFO_AF_ADDR_L(base_addr);
	i2c_w_bytes(aw_haptic, AW8692X_REG_RTPCFG3, reg_array,
		    AW_I2C_BYTE_THREE);

	/* get FIFO_AE and FIFO_AF addr */
	i2c_r_bytes(aw_haptic, AW8692X_REG_RTPCFG3, reg_array,
		    AW_I2C_BYTE_THREE);
	ae_addr_h = ((reg_array[0]) & AW8692X_BIT_RTPCFG3_FIFO_AEH) >> 4;
	ae_addr_l = reg_array[1];
	aw_dev_info("%s: almost_empty_threshold = %d\n",
		    __func__, (uint16_t)((ae_addr_h << 8) | ae_addr_l));
	af_addr_h = ((reg_array[0]) & AW8692X_BIT_RTPCFG3_FIFO_AFH);
	af_addr_l = reg_array[2];
	aw_dev_info("%s: almost_full_threshold = %d\n",
		    __func__, (uint16_t)((af_addr_h << 8) | af_addr_l));

	aw8692x_set_base_addr(aw_haptic);
	aw8692x_set_ram_addr(aw_haptic, aw_haptic->ram.base_addr);
	i = aw_haptic->ram.ram_shift;
	while (i < awinic_cont->len) {
		if ((awinic_cont->len - i) < AW_RAMDATA_WR_BUFFER_SIZE)
			len = awinic_cont->len - i;
		else
			len = AW_RAMDATA_WR_BUFFER_SIZE;

		i2c_w_bytes(aw_haptic, AW8692X_REG_RAMDATA,
			    &awinic_cont->data[i], len);
		i += len;
	}

#ifdef AW_CHECK_RAM_DATA
	aw8692x_set_ram_addr(aw_haptic, aw_haptic->ram.base_addr);
	i = aw_haptic->ram.ram_shift;
	while (i < awinic_cont->len) {
		if ((awinic_cont->len - i) < AW_RAMDATA_RD_BUFFER_SIZE)
			len = awinic_cont->len - i;
		else
			len = AW_RAMDATA_RD_BUFFER_SIZE;

		i2c_r_bytes(aw_haptic, AW8692X_REG_RAMDATA,
				    ram_data, len);
		ret = aw8692x_check_ram_data(aw_haptic, &awinic_cont->data[i],
					     ram_data, len);
		if (ret < 0)
			break;
		i += len;
	}
	if (ret)
		aw_dev_err("%s: ram data check sum error\n",
			   __func__);
	else
		aw_dev_info("%s: ram data check sum pass\n",
			    __func__);
#endif
	/* RAMINIT Disable */
	aw8692x_ram_init(aw_haptic, false);
	mutex_unlock(&aw_haptic->lock);
	aw_dev_info("%s: exit\n", __func__);
	return ret;
}

static unsigned long aw8692x_get_theory_time(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;
	uint32_t fre_val = 0;
	unsigned long theory_time = 0;

	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSCTRL2, &reg_val, AW_I2C_BYTE_ONE);
	fre_val = (reg_val & AW8692X_BIT_SYSCTRL2_RCK_FRE) >> 0;
	if (fre_val == AW8692X_RCK_FRE_24K)
		theory_time = (aw_haptic->rtp_len / 24000) * 1000000;	/*24K*/
	if (fre_val == AW8692X_RCK_FRE_48K)
		theory_time = (aw_haptic->rtp_len / 48000) * 1000000;	/*48K*/

	aw_dev_info("%s: microsecond:%ld  theory_time = %ld\n",
		    __func__, aw_haptic->microsecond, theory_time);
	return theory_time;
}

static void aw8692x_haptic_value_init(struct aw_haptic *aw_haptic)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	if (aw_haptic->device_id == 832 || aw_haptic->device_id == 833) {
		aw_haptic->info.f0_pre = AW8692X_0832_F0_PRE;
		aw_haptic->info.f0_cali_percent = AW8692X_0832_F0_CALI_PERCEN;
		aw_haptic->info.cont_drv1_lvl = AW8692X_0832_CONT_DRV1_LVL;
		aw_haptic->info.cont_drv2_lvl = AW8692X_0832_CONT_DRV2_LVL;
		aw_haptic->info.cont_drv1_time = AW8692X_0832_CONT_DRV1_TIME;
		aw_haptic->info.cont_drv2_time = AW8692X_0832_CONT_DRV2_TIME;
		aw_haptic->info.cont_drv_width = AW8692X_0832_CONT_DRV_WIDTH;
		aw_haptic->info.cont_wait_num = AW8692X_0832_CONT_WAIT_NUM;
		aw_haptic->info.cont_brk_time = AW8692X_0832_CONT_BRK_TIME;
		aw_haptic->info.cont_track_margin = AW8692X_0832_CONT_TRACK_MARGIN;
		aw_haptic->info.cont_tset = AW8692X_0832_CONT_TEST;
		aw_haptic->info.brk_bst_md = AW8692X_0832_BRK_BST_MD;
		aw_haptic->info.cont_bemf_set = AW8692X_0832_CONT_BEMF_SET;
		aw_haptic->info.cont_bst_brk_gain = AW8692X_0832_CONT_BST_BRK_GAIN;
		aw_haptic->info.cont_brk_gain = AW8692X_0832_CONT_BRK_GAIN;
	} else {
		aw_haptic->info.f0_pre = AW8692X_0815_F0_PRE;
		aw_haptic->info.f0_cali_percent = AW8692X_0815_F0_CALI_PERCEN;
		aw_haptic->info.cont_drv1_lvl = AW8692X_0815_CONT_DRV1_LVL;
		aw_haptic->info.cont_drv2_lvl = AW8692X_0815_CONT_DRV2_LVL;
		aw_haptic->info.cont_drv1_time = AW8692X_0815_CONT_DRV1_TIME;
		aw_haptic->info.cont_drv2_time = AW8692X_0815_CONT_DRV2_TIME;
		aw_haptic->info.cont_drv_width = AW8692X_0815_CONT_DRV_WIDTH;
		aw_haptic->info.cont_wait_num = AW8692X_0815_CONT_WAIT_NUM;
		aw_haptic->info.cont_brk_time = AW8692X_0815_CONT_BRK_TIME;
		aw_haptic->info.cont_track_margin = AW8692X_0815_CONT_TRACK_MARGIN;
		aw_haptic->info.cont_tset = AW8692X_0815_CONT_TEST;
		aw_haptic->info.brk_bst_md = AW8692X_0815_BRK_BST_MD;
		aw_haptic->info.cont_bemf_set = AW8692X_0815_CONT_BEMF_SET;
		aw_haptic->info.cont_bst_brk_gain = AW8692X_0815_CONT_BST_BRK_GAIN;
		aw_haptic->info.cont_brk_gain = AW8692X_0815_CONT_BRK_GAIN;
	}
#else
	aw_haptic->info.f0_pre = AW_HAPTIC_F0_PRE;
	aw_haptic->info.f0_cali_percent = AW_HAPTIC_F0_CALI_PERCEN;
	aw_haptic->info.cont_drv1_lvl = AW8692X_CONT_DRV1_LVL;
	aw_haptic->info.cont_drv2_lvl = AW8692X_CONT_DRV2_LVL;
	aw_haptic->info.cont_drv1_time = AW8692X_CONT_DRV1_TIME;
	aw_haptic->info.cont_drv2_time = AW8692X_CONT_DRV2_TIME;
	aw_haptic->info.cont_drv_width = AW8692X_CONT_DRV_WIDTH;
	aw_haptic->info.cont_wait_num = AW8692X_CONT_WAIT_NUM;
	aw_haptic->info.cont_brk_time = AW8692X_CONT_BRK_TIME;
	aw_haptic->info.cont_track_margin = AW8692X_CONT_TRACK_MARGIN;
	aw_haptic->info.cont_tset = AW8692X_CONT_TEST;
	aw_haptic->info.brk_bst_md = AW8692X_BRK_BST_MD;
	aw_haptic->info.cont_bemf_set = AW8692X_CONT_BEMF_SET;
	aw_haptic->info.cont_bst_brk_gain = AW8692X_CONT_BST_BRK_GAIN;
	aw_haptic->info.cont_brk_gain = AW8692X_CONT_BRK_GAIN;
#endif
	aw_haptic->info.max_bst_vol = AW8692X_MAX_BST_VOL;
	aw_haptic->info.d2s_gain = AW8692X_D2S_GAIN;
	aw_haptic->info.bst_vol_default = AW8692X_BST_VOL_DEFAULT;
	aw_haptic->info.bst_vol_ram = AW8692X_BST_VOL_RAM;
	aw_haptic->info.bst_vol_rtp = AW8692X_BST_VOL_RTP;
}
static void aw8692x_misc_para_init(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0;
	uint8_t reg_array[8] = {0};

	aw_dev_dbg("%s: enter!\n", __func__);
	/* Get vamx and gain */
	i2c_r_bytes(aw_haptic, AW8692X_REG_PLAYCFG1,
		    reg_array, AW_I2C_BYTE_TWO);
#ifdef OPLUS_FEATURE_CHG_BASIC
	 aw_haptic->vmax = AW_HAPTIC_HIGH_LEVEL_REG_VAL;
#else
	aw_haptic->vmax = reg_array[0] & AW8692X_BIT_PLAYCFG1_BST_VOUT_VREFSET;
#endif
	aw_haptic->gain = reg_array[1];
	/* Get wave_seq */
	i2c_r_bytes(aw_haptic, AW8692X_REG_WAVCFG1,
		    reg_array, AW_I2C_BYTE_EIGHT);
	aw_haptic->index = reg_array[0];
	memcpy(aw_haptic->seq, reg_array, AW_SEQUENCER_SIZE);
	aw8692x_tm_config(aw_haptic, AW_UNLOCK);
	reg_val = AW8692X_REG_SYSCTRL5_INIT_VAL;
	i2c_w_bytes(aw_haptic, AW8692X_REG_SYSCTRL5, &reg_val, AW_I2C_BYTE_ONE);
	reg_val = AW8692X_BIT_PWMCFG1_INIT_VAL;
	i2c_w_bytes(aw_haptic, AW8692X_REG_PWMCFG1, &reg_val,
		    AW_I2C_BYTE_ONE);
	reg_val = AW8692X_BIT_ANACFG12_INIT_VAL;
	i2c_w_bytes(aw_haptic, AW8692X_REG_ANACFG12, &reg_val,
		    AW_I2C_BYTE_ONE);
	reg_val = AW8692X_BIT_ANACFG15_INIT_VAL;
	i2c_w_bytes(aw_haptic, AW8692X_REG_ANACFG15, &reg_val,
		    AW_I2C_BYTE_ONE);
	reg_val = AW8692X_BIT_ANACFG16_INIT_VAL;
	i2c_w_bytes(aw_haptic, AW8692X_REG_ANACFG16, &reg_val,
		    AW_I2C_BYTE_ONE);
	if (!aw_haptic->info.brk_bst_md)
		aw_dev_err("%s aw_haptic->info.brk_bst_md = 0!\n",
			   __func__);
	if (!aw_haptic->info.cont_brk_time)
		aw_dev_err("%s aw_haptic->info.cont_brk_time = 0!\n", __func__);
	if (!aw_haptic->info.cont_tset)
		aw_dev_err("%s aw_haptic->info.cont_tset = 0!\n",
			   __func__);
	if (!aw_haptic->info.cont_bemf_set)
		aw_dev_err("%s aw_haptic->info.cont_bemf_set = 0!\n", __func__);
	if (!aw_haptic->info.cont_bst_brk_gain)
		aw_dev_err("%s aw_haptic->info.cont_bst_brk_gain = 0!\n",
			   __func__);
	if (!aw_haptic->info.cont_brk_gain)
		aw_dev_err("%s aw_haptic->info.cont_brk_gain = 0!\n", __func__);
	if (!aw_haptic->info.d2s_gain)
		aw_dev_err("%s aw_haptic->info.d2s_gain = 0!\n",
			   __func__);

	i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG1,
		   AW8692X_BIT_CONTCFG1_BRK_BST_MD_MASK,
		   aw_haptic->info.brk_bst_md << 6);
	i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG5,
		   AW8692X_BIT_CONTCFG5_BST_BRK_GAIN_MASK &
		   AW8692X_BIT_CONTCFG5_BRK_GAIN_MASK,
		   (aw_haptic->info.cont_bst_brk_gain << 4) |
		   aw_haptic->info.cont_brk_gain);
	i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG10,
		    &aw_haptic->info.cont_brk_time,
		    AW_I2C_BYTE_ONE);
	i2c_w_bits(aw_haptic, AW8692X_REG_CONTCFG13,
		   AW8692X_BIT_CONTCFG13_TSET_MASK &
		   AW8692X_BIT_CONTCFG13_BEME_SET_MASK,
		   (aw_haptic->info.cont_tset << 4) |
		   aw_haptic->info.cont_bemf_set);
	i2c_w_bits(aw_haptic, AW8692X_REG_DETCFG2,
		   AW8692X_BIT_DETCFG2_D2S_GAIN_MASK,
		   aw_haptic->info.d2s_gain);

	/* config auto brake func */
	aw8692x_auto_break_mode(aw_haptic, false);
	aw8692x_tm_config(aw_haptic, AW_LOCK);

}

/******************************************************
 *
 * Extern function : sysfs attr
 *
 ******************************************************/
static ssize_t aw8692x_bst_vol_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	len += snprintf(buf + len, PAGE_SIZE - len,
			"bst_vol_ram=%d, bst_vol_rtp=%d\n",
			aw_haptic->info.bst_vol_ram,
			aw_haptic->info.bst_vol_rtp);
	return len;
}

static ssize_t aw8692x_bst_vol_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	uint32_t databuf[2] = { 0, 0 };
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	if (sscanf(buf, "%d %d", &databuf[0], &databuf[1]) == 2) {
		aw_haptic->info.bst_vol_ram = databuf[0];
		aw_haptic->info.bst_vol_rtp = databuf[1];
	}
	return count;
}

static ssize_t aw8692x_cont_wait_num_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	ssize_t len = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_wait_num = 0x%02X\n",
			aw_haptic->info.cont_wait_num);
	return len;
}

static ssize_t aw8692x_cont_wait_num_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int rc = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	rc = kstrtou8(buf, 0, &aw_haptic->info.cont_wait_num);
	if (rc < 0)
		return rc;
	i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG4,
		       &aw_haptic->info.cont_wait_num, AW_I2C_BYTE_ONE);

	return count;
}

static ssize_t aw8692x_cont_drv_lvl_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	ssize_t len = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv1_lvl = 0x%02X, cont_drv2_lvl = 0x%02X\n",
			aw_haptic->info.cont_drv1_lvl,
			aw_haptic->info.cont_drv2_lvl);
	return len;
}

static ssize_t aw8692x_cont_drv_lvl_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	uint32_t databuf[2] = {0};
	uint8_t reg_array[2] = {0};
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw_haptic->info.cont_drv1_lvl = databuf[0];
		aw_haptic->info.cont_drv2_lvl = databuf[1];

		i2c_r_bytes(aw_haptic, AW8692X_REG_CONTCFG6, reg_array,
			    AW_I2C_BYTE_ONE);
		reg_array[0] &= AW8692X_BIT_CONTCFG6_DRV1_LVL_MASK;
		reg_array[0] |= aw_haptic->info.cont_drv1_lvl;
		reg_array[1] = aw_haptic->info.cont_drv2_lvl;
		i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG6, reg_array,
			    AW_I2C_BYTE_TWO);
	}
	return count;
}

static ssize_t aw8692x_cont_drv_time_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	ssize_t len = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	len += snprintf(buf + len, PAGE_SIZE - len,
			"cont_drv1_time = 0x%02X, cont_drv2_time = 0x%02X\n",
			aw_haptic->info.cont_drv1_time,
			aw_haptic->info.cont_drv2_time);
	return len;
}

static ssize_t aw8692x_cont_drv_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	uint8_t reg_array[2] = {0};
	uint32_t databuf[2] = {0};
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw_haptic->info.cont_drv1_time = databuf[0];
		aw_haptic->info.cont_drv2_time = databuf[1];
		reg_array[0] = (uint8_t)aw_haptic->info.cont_drv1_time;
		reg_array[1] = (uint8_t)aw_haptic->info.cont_drv2_time;
		i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG8, reg_array,
			   AW_I2C_BYTE_TWO);
	}
	return count;
}

static ssize_t aw8692x_cont_brk_time_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	ssize_t len = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	len += snprintf(buf + len, PAGE_SIZE - len, "cont_brk_time = 0x%02X\n",
			aw_haptic->info.cont_brk_time);
	return len;
}

static ssize_t aw8692x_cont_brk_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int rc = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	rc = kstrtou8(buf, 0, &aw_haptic->info.cont_brk_time);
	if (rc < 0)
		return rc;
	i2c_w_bytes(aw_haptic, AW8692X_REG_CONTCFG10,
		    &aw_haptic->info.cont_brk_time, AW_I2C_BYTE_ONE);
	return count;
}

static ssize_t aw8692x_trig_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	uint8_t i = 0;
	ssize_t len = 0;
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	for (i = 0; i < AW_TRIG_NUM; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"trig%d: trig_level=%d, trig_polar=%d, pos_enable=%d, pos_sequence=%d, neg_enable=%d, neg_sequence=%d trig_brk=%d, trig_bst=%d\n",
				i + 1,
				aw_haptic->trig[i].trig_level,
				aw_haptic->trig[i].trig_polar,
				aw_haptic->trig[i].pos_enable,
				aw_haptic->trig[i].pos_sequence,
				aw_haptic->trig[i].neg_enable,
				aw_haptic->trig[i].neg_sequence,
				aw_haptic->trig[i].trig_brk,
				aw_haptic->trig[i].trig_bst);
	}

	return len;
}

static ssize_t aw8692x_trig_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	uint32_t databuf[9] = { 0 };
	cdev_t *cdev = dev_get_drvdata(dev);
	struct aw_haptic *aw_haptic = container_of(cdev, struct aw_haptic,
						   vib_dev);

	if (sscanf(buf, "%d %d %d %d %d %d %d %d %d", &databuf[0], &databuf[1],
		   &databuf[2], &databuf[3], &databuf[4], &databuf[5],
		   &databuf[6], &databuf[7], &databuf[8]) == 9) {
		aw_dev_info("%s: %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
			    __func__, databuf[0], databuf[1], databuf[2],
			    databuf[3], databuf[4], databuf[5], databuf[6],
			    databuf[7], databuf[8]);
		if (databuf[0] < 1 || databuf[0] > 3) {
			aw_dev_info("%s: input trig_num out of range!\n",
				    __func__);
			return count;
		}
		if (databuf[0] == 1 && aw_haptic->info.is_enabled_one_wire) {
			aw_dev_info("%s: trig1 pin used for one wire!\n",
				    __func__);
			return count;
		}
		if (databuf[0] == 2 || databuf[0] == 3) {
			aw_dev_info("%s: trig2 and trig3 pin used for i2s!\n",
				    __func__);
			return count;
		}
		if (!aw_haptic->ram_init) {
			aw_dev_err("%s: ram init failed, not allow to play!\n",
				   __func__);
			return count;
		}
		if (databuf[4] > aw_haptic->ram.ram_num ||
		    databuf[6] > aw_haptic->ram.ram_num) {
			aw_dev_err("%s: input seq value out of range!\n",
				   __func__);
			return count;
		}
		databuf[0] -= 1;

		aw_haptic->trig[databuf[0]].trig_level = databuf[1];
		aw_haptic->trig[databuf[0]].trig_polar = databuf[2];
		aw_haptic->trig[databuf[0]].pos_enable = databuf[3];
		aw_haptic->trig[databuf[0]].pos_sequence = databuf[4];
		aw_haptic->trig[databuf[0]].neg_enable = databuf[5];
		aw_haptic->trig[databuf[0]].neg_sequence = databuf[6];
		aw_haptic->trig[databuf[0]].trig_brk = databuf[7];
		aw_haptic->trig[databuf[0]].trig_bst = databuf[8];
		mutex_lock(&aw_haptic->lock);
		switch (databuf[0]) {
		case AW8692X_TRIG1:
			aw8692x_trig1_param_config(aw_haptic);
			break;
		case AW8692X_TRIG2:
			aw8692x_trig2_param_config(aw_haptic);
			break;
		case AW8692X_TRIG3:
			aw8692x_trig3_param_config(aw_haptic);
			break;
		}
		mutex_unlock(&aw_haptic->lock);
	}
	return count;
}

static DEVICE_ATTR(bst_vol, S_IWUSR | S_IRUGO, aw8692x_bst_vol_show,
		   aw8692x_bst_vol_store);
static DEVICE_ATTR(cont_wait_num, S_IWUSR | S_IRUGO, aw8692x_cont_wait_num_show,
		   aw8692x_cont_wait_num_store);
static DEVICE_ATTR(cont_drv_lvl, S_IWUSR | S_IRUGO, aw8692x_cont_drv_lvl_show,
		   aw8692x_cont_drv_lvl_store);
static DEVICE_ATTR(cont_drv_time, S_IWUSR | S_IRUGO, aw8692x_cont_drv_time_show,
		   aw8692x_cont_drv_time_store);
static DEVICE_ATTR(cont_brk_time, S_IWUSR | S_IRUGO, aw8692x_cont_brk_time_show,
		   aw8692x_cont_brk_time_store);
static DEVICE_ATTR(trig, S_IWUSR | S_IRUGO, aw8692x_trig_show,
		   aw8692x_trig_store);

static struct attribute *aw8692x_vibrator_attributes[] = {
	&dev_attr_bst_vol.attr,
	&dev_attr_cont_wait_num.attr,
	&dev_attr_cont_drv_lvl.attr,
	&dev_attr_cont_drv_time.attr,
	&dev_attr_cont_brk_time.attr,
	&dev_attr_trig.attr,
	NULL
};

static struct attribute_group aw8692x_vibrator_attribute_group = {
	.attrs = aw8692x_vibrator_attributes
};

static int aw8692x_creat_node(struct aw_haptic *aw_haptic)
{
	int ret = 0;

	ret = sysfs_create_group(&aw_haptic->vib_dev.dev->kobj,
				 &aw8692x_vibrator_attribute_group);
	if (ret < 0) {
		aw_dev_err("%s: error creating aw8692x sysfs attr files\n",
			   __func__);
		return ret;
	}
	return 0;
}

static void aw8692x_dump_rtp_regs(struct aw_haptic *aw_haptic)
{
	uint8_t reg_array[4] = {0};
	uint8_t reg_name[][10] = {
		{"SYSINT"},
		{"SYSINTM"},
		{"SYSST2"},
	};
	int i = 0;

	i2c_r_bytes(aw_haptic, AW8692X_REG_SYSINT, reg_array, AW_I2C_BYTE_THREE);

	for (i = 0 ; i < AW_I2C_BYTE_THREE; i++) {
		aw_dev_info("%s REG_%s(0x%02x) = 0x%02X\n", __func__,
			    reg_name[i], i+2, reg_array[i]);
	}

	i2c_r_bytes(aw_haptic, AW8692X_REG_PLAYCFG4, &reg_array[0],
		    AW_I2C_BYTE_ONE);
	aw_dev_info("%s REG_G0(0x09) = 0x%02X\n", __func__,
		    reg_array[0]);
	i2c_r_bytes(aw_haptic, AW8692X_REG_GLBRD5, &reg_array[0],
		   AW_I2C_BYTE_ONE);
	aw_dev_info("%s REG_GLBRD5(0x3F) = 0x%02X\n", __func__,
		    reg_array[0]);
}

static void aw8692x_test(struct aw_haptic *aw_haptic)
{
	uint8_t reg_val = 0xFB;
	i2c_w_bytes(aw_haptic, AW8692X_REG_SYSINTM, &reg_val, AW_I2C_BYTE_ONE);
}

static int aw8692x_check_qualify(struct aw_haptic *aw_haptic)
{
	int ret = -1;
	unsigned char reg_val = 0;

	ret = i2c_r_bytes(aw_haptic, AW8692X_REG_EFCFG6, &reg_val,
			  AW_I2C_BYTE_ONE);
	if (ret < 0)
		return ret;
	if (!(reg_val & 0x80)) {
		aw_dev_err("%s: unqualified chip!", __func__);
		return -ERANGE;
	}
	return 0;
}

struct aw_haptic_func aw8692x_func_list = {
	.play_stop = aw8692x_stop,
	.ram_init = aw8692x_ram_init,
	.get_vbat = aw8692x_get_vbat,
	.creat_node = aw8692x_creat_node,
	.get_f0 = aw8692x_get_f0,
	.cont_config = aw8692x_cont_config,
	.offset_cali = aw8692x_offset_cali,
	.read_f0 = aw8692x_read_f0,
	.get_irq_state = aw8692x_get_irq_state,
	.juge_rtp_going = aw8692x_juge_rtp_going,
	.set_bst_peak_cur = aw8692x_set_bst_peak_cur,
	.get_theory_time = aw8692x_get_theory_time,
	.get_lra_resistance = aw8692x_get_lra_resistance,
	.set_pwm = aw8692x_set_pwm,
	.play_mode = aw8692x_play_mode,
	.set_bst_vol = aw8692x_set_bst_vol,
	.interrupt_setup = aw8692x_interrupt_setup,
	.set_repeat_seq = aw8692x_set_repeat_seq,
	.auto_bst_enable = aw8692x_auto_bst_enable,
	.vbat_mode_config = aw8692x_vbat_mode_config,
	.set_wav_seq = aw8692x_set_wav_seq,
	.set_wav_loop = aw8692x_set_wav_loop,
	.set_ram_addr = aw8692x_set_ram_addr,
	.set_rtp_data = aw8692x_set_rtp_data,
	.container_update = aw8692x_container_update,
	.protect_config = aw8692x_protect_config,
	.trig_init = aw8692x_trig_init,
	.irq_clear = aw8692x_irq_clear,
	.get_wav_loop = aw8692x_get_wav_loop,
	.play_go = aw8692x_play_go,
	.misc_para_init = aw8692x_misc_para_init,
	.set_rtp_aei = aw8692x_set_rtp_aei,
	.set_gain = aw8692x_set_gain,
	.upload_lra = aw8692x_upload_lra,
	.bst_mode_config = aw8692x_bst_mode_config,
	.get_reg = aw8692x_get_reg,
	.get_prctmode = aw8692x_get_prctmode,
	.get_ram_data = aw8692x_get_ram_data,
	.get_first_wave_addr = aw8692x_get_first_wave_addr,
	.get_glb_state = aw8692x_get_glb_state,
	.get_chip_state = aw8692x_get_chip_state,
	.read_irq_state = aw8692x_read_irq_state,
	.get_osc_status = aw8692x_get_osc_status,
	.rtp_get_fifo_afs = aw8692x_rtp_get_fifo_afs,
	.rtp_get_fifo_aes = aw8692x_rtp_get_fifo_aes,
	.get_wav_seq = aw8692x_get_wav_seq,
	.haptic_value_init = aw8692x_haptic_value_init,
	.set_ram_data = aw8692x_set_ram_data,
	.dump_rtp_regs = aw8692x_dump_rtp_regs,
	.aw_test = aw8692x_test,
	.check_qualify = aw8692x_check_qualify,
};
