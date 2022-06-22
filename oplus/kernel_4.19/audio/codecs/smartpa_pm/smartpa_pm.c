/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/version.h>
#include <linux/input.h>
#include <dsp/apr_audio-v2.h>

//#define TEST_DEBUG_LOG 1

static char const *smartpa_pm_ctrl_text[] = {"Off", "On"};
static const struct soc_enum smartpa_pm_ctrl_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(smartpa_pm_ctrl_text), smartpa_pm_ctrl_text);

extern int set_smartpa_pm_status_apr(void *buf, int cmd_size);
extern int get_smartpa_pm_result_apr(void *buf, int cmd_size);

static int smartpa_pm_ctrl_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	//struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	unsigned int val = ucontrol->value.integer.value[0];

	ret = set_smartpa_pm_status_apr(&val, sizeof(int));
	if (ret != 0) {
    	pr_info("%s(), apr set failed, ret = %d", __func__, ret);
	}
	#ifdef TEST_DEBUG_LOG
	pr_info("%s(), ret = %d", __func__, ret);
	#endif

	return ret;
}

static int smartpa_pm_result_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int len = 4 + sizeof(struct param_hdr_v3);//status + param_hdr_v3;
	int count = len + 2*sizeof(int); //L&R(2*4)
	uint8_t * buffer = kzalloc(count, GFP_KERNEL);

	if (!buffer) {
		pr_err("%s(), kzalloc failed", __func__);
		return -ENOMEM;
	}

	ret = get_smartpa_pm_result_apr(buffer, count);
	if (ret == 0) {
		ucontrol->value.integer.value[0] = *(unsigned int *) (buffer + len);
		ucontrol->value.integer.value[1] = *(unsigned int *)(buffer + len + sizeof(int));
		#ifdef TEST_DEBUG_LOG
		pr_info("%s(), get 10000*lspk=(%d), 10000*rspk=(%d)", __func__,
				(unsigned int)(*(float *)(buffer+len)*10000),
				(unsigned int)(*(float *)(buffer+len+sizeof(int))*10000));
		#endif
	} else {
    	pr_info("%s(), apr get failed, ret = %d", __func__, ret);
	}
	kfree(buffer);

	return ret;
}

static int smartpa_pm_result_ctl(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0x7fffffff; /* 32 bit value,  */

	return 0;
}

static const struct snd_kcontrol_new smartpa_pm_controls[] = {
	SOC_ENUM_EXT("SMARTPA_PM_STATE", smartpa_pm_ctrl_enum,
					NULL, smartpa_pm_ctrl_put),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "SMARTPA_PM_RESULT",
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.info = smartpa_pm_result_ctl,
		.get = smartpa_pm_result_get
	},
};

void add_smartpa_pm_controls(struct snd_soc_component *component)
{
	#ifdef TEST_DEBUG_LOG
	pr_info("%s(), enter", __func__);
	#endif
	snd_soc_add_component_controls(component, smartpa_pm_controls,
				ARRAY_SIZE(smartpa_pm_controls));
}
