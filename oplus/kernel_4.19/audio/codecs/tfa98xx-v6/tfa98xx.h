/*
 * Copyright (C) 2014 NXP Semiconductors, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __TFA98XX_INC__
#define __TFA98XX_INC__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>

#include "tfa_device.h"
#include "tfa_container.h"
#include "config.h"

/* max. length of a alsa mixer control name */
#define MAX_CONTROL_NAME        48

#define TFA98XX_MAX_REGISTER              0xff

#define TFA98XX_FLAG_SKIP_INTERRUPTS	(1 << 0)
#define TFA98XX_FLAG_SAAM_AVAILABLE	(1 << 1)
#define TFA98XX_FLAG_STEREO_DEVICE	(1 << 2)
#define TFA98XX_FLAG_MULTI_MIC_INPUTS	(1 << 3)
#define TFA98XX_FLAG_TAPDET_AVAILABLE	(1 << 4)
#define TFA98XX_FLAG_CALIBRATION_CTL	(1 << 5)
#define TFA98XX_FLAG_REMOVE_PLOP_NOISE	(1 << 6)
#define TFA98XX_FLAG_LP_MODES	        (1 << 7)
#define TFA98XX_FLAG_TDM_DEVICE         (1 << 8)

/*To support tfa9873*/
#define TFA98XX_FLAG_ADAPT_NOISE_MODE   (1 << 9)
#ifdef OPLUS_ARCH_EXTENDS
#define TFA98XX_FLAG_CHIP_SELECTED      (1 << 16)

//chip select
#define CHIP_SELECTOR_LEFT	(1)
#define CHIP_SELECTOR_RIGHT	(2)
#define CHIP_SELECTOR_STEREO	(3)

//device i2c address
#define CHIP_LEFT_ADDR		(0x34)
#define CHIP_RIGHT_ADDR		(0x35)

/*modified for pad tdm pa count*/
#define CHIP_4PA_COUNT    (4)
/*modified for pad tdm chip select*/
#define CHIP_SELECTOR_4PA_LEFT_UP      (1)
#define CHIP_SELECTOR_4PA_LEFT_DOWN    (2)
#define CHIP_SELECTOR_4PA_RIGHT_UP     (3)
#define CHIP_SELECTOR_4PA_RIGHT_DOWN   (4)
#define CHIP_SELECTOR_4PA_LEFT_BOTH    (5)
#define CHIP_SELECTOR_4PA_RIGHT_BOTH   (6)

/*modified for pad tdm device i2c address*/
#define CHIP_LEFT_UP_ADDR       (0x35)
#define CHIP_LEFT_DOWN_ADDR     (0x34)
#define CHIP_RIGHT_UP_ADDR      (0x36)
#define CHIP_RIGHT_DOWN_ADDR    (0x37)
#endif /* OPLUS_ARCH_EXTENDS */

#define TFA98XX_NUM_RATES		9

/* DSP init status */
enum tfa98xx_dsp_init_state {
	TFA98XX_DSP_INIT_STOPPED,	/* DSP not running */
	TFA98XX_DSP_INIT_RECOVER,	/* DSP error detected at runtime */
	TFA98XX_DSP_INIT_FAIL,		/* DSP init failed */
	TFA98XX_DSP_INIT_PENDING,	/* DSP start requested */
	TFA98XX_DSP_INIT_DONE,		/* DSP running */
	TFA98XX_DSP_INIT_INVALIDATED,	/* DSP was running, requires re-init */
};

enum tfa98xx_dsp_fw_state {
       TFA98XX_DSP_FW_NONE = 0,
       TFA98XX_DSP_FW_PENDING,
       TFA98XX_DSP_FW_FAIL,
       TFA98XX_DSP_FW_OK,
};

struct tfa98xx_firmware {
	void			*base;
	struct tfa98xx_device	*dev;
	char			name[9];	//TODO get length from tfa parameter defs
};

struct tfa98xx_baseprofile {
	char basename[MAX_CONTROL_NAME];    /* profile basename */
	int len;                            /* profile length */
	int item_id;                        /* profile id */
	int sr_rate_sup[TFA98XX_NUM_RATES]; /* sample rates supported by this profile */
	struct list_head list;              /* list of all profiles */
};

struct tfa98xx {
	struct regmap *regmap;
	struct i2c_client *i2c;
	struct regulator *vdd;
	struct snd_soc_component *component;
	struct workqueue_struct *tfa98xx_wq;
	struct delayed_work init_work;
	struct delayed_work monitor_work;
	struct delayed_work interrupt_work;
	struct delayed_work tapdet_work;

	/*To support tfa9873*/
	struct delayed_work nmodeupdate_work;

	#ifdef OPLUS_FEATURE_FADE_IN
	struct delayed_work fadein_work;
	#endif /* OPLUS_FEATURE_FADE_IN */
	struct mutex dsp_lock;
	int dsp_init;
	int dsp_fw_state;
	int sysclk;
	int rst_gpio;
	u16 rev;
	int audio_mode;
	struct tfa98xx_firmware fw;
	char *fw_name;
	int rate;
	wait_queue_head_t wq;
	struct device *dev;
	unsigned int init_count;
	int pstream;
	int cstream;
	struct input_dev *input;
	bool tapdet_enabled;		/* service enabled */
	bool tapdet_open;		/* device file opened */
	unsigned int tapdet_profiles;	/* tapdet profile bitfield */
	bool tapdet_poll;		/* tapdet running on polling mode */

	unsigned int rate_constraint_list[TFA98XX_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;

	int reset_gpio;
	int power_gpio;
	int irq_gpio;

	struct list_head list;
	struct tfa_device *tfa;
	int vstep;
	int profile;
	int prof_vsteps[TFACONT_MAXPROFS]; /* store vstep per profile (single device) */

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_dir;
#else
	struct proc_dir_entry *dbg_dir;
#endif/*CONFIG_DEBUG_FS*/
	u8 reg;
	unsigned int flags;
	bool set_mtp_cal;
	uint16_t cal_data;
	#ifdef OPLUS_ARCH_EXTENDS
	struct regulator *tfa98xx_vdd;
	#endif /* OPLUS_ARCH_EXTENDS */

	#ifdef OPLUS_FEATURE_FADE_IN
	bool fadein_enable;
	#endif /* OPLUS_FEATURE_FADE_IN */
};


#endif /* __TFA98XX_INC__ */

