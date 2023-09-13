/*
 * OPLUS_ARCH_EXTENDS
 * ak4376.c  --  audio driver for AK4376A and AK4377A
 *
 * Copyright (C) 2017 Asahi Kasei Microdevices Corporation
 *  Author                Date        Revision      DS ver.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  K.Tsubota           15/06/12	    1.0           00
 *  K.Tsubota           16/06/17	    1.1           00
 *  K.Tsubota           16/10/31	    1.2           01
 *  K.Tsubota           16/11/24	    1.3           01
 *  K.Tsubota           17/08/09	    1.4           01
 *  J.Wakasugi          18/05/30        2.0      AK4376A 00  // for AK4376A/AK4377A & 4.9.XX
 *                                               AK4377A 01
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  kernel version: 4.9
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/version.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#ifdef OPLUS_ARCH_EXTENDS
#include <linux/regulator/consumer.h>
#endif /* OPLUS_ARCH_EXTENDS */

#include "ak4376.h"

#ifdef OPLUS_ARCH_EXTENDS
#include <soc/oplus/system/oplus_project.h>
#endif /* OPLUS_ARCH_EXTENDS */

#define AK4376_DRV_NAME "ak4376_codec"
//#define AK4376_DEBUG				//used at debug mode
//#define CONFIG_DEBUG_FS_CODEC		//used at debug mode

#define PLL_MCKI_MODE
//#define PLL_BICK_MODE

#ifdef AK4376_DEBUG
#define akdbgprt printk
#else
#define akdbgprt(format, arg...) do {} while (0)
#endif

#ifdef OPLUS_ARCH_EXTENDS
static bool ftm_pdn_on = true;
#endif /* OPLUS_ARCH_EXTENDS */

/* AK4376 Codec Private Data */
struct ak4376_priv {
	struct mutex mutex;
	unsigned int priv_pdn_en;			//PDN GPIO pin
	int pdn1;							//PDN control, 0:Off, 1:On, 2:No use(assume always On)
	int pdn2;							//PDN control for kcontrol
	int fs1;
	int rclk;							//Master Clock
	int nBickFreq;						//0:32fs, 1:48fs, 2:64fs
	int nPllMode;
	int nPllMCKI;						//0:9.6MHz, 1:11.2896MHz, 2:12.288MHz, 3:19.2MHz
	int deviceID;						//0x20:AK4376, 0x21:AK4376A, 0x22:AK4377A, 0x30:AK4377
	int lpmode;							//0:High Performance, 1:Low power mode
	int xtalfreq;						//0:12.288MHz, 1:11.2896MHz
	int nDACOn;
	struct i2c_client *i2c;
	struct regmap *regmap;
	#ifdef OPLUS_ARCH_EXTENDS
	int audio_vdd_en_gpio;
	struct regulator *ak4376_tvdd;
	struct regulator *ak4376_avdd;
	#endif /* OPLUS_ARCH_EXTENDS */

	#ifdef OPLUS_ARCH_EXTENDS
	#ifdef USB_SWITCH_MAX20328
	bool pdn_pending;
	bool amp_pending;
	struct mutex pdn_lock;
	#endif
	#endif /* OPLUS_ARCH_EXTENDS */
};

static const struct reg_default ak4376a_patch[] = {
	{ 0x0B, 0x99 },      /* 0x0B AK4376A_0B_LCH_OUTPUT_VOLUME */
	{ 0x26, 0x20 },      /* 0x26  AK4376A_26_DAC32_DEBUG2		*/
	{ 0x2A, 0x07 },      /* 0x2A  AK4376A_2A_LDO3_DEBUG1		*/
};

static const struct reg_default ak4377a_patch[] = {
	{ 0x0B, 0x99 },      /* 0x0B  AK4377A_0B_LCH_OUTPUT_VOLUME */
	{ 0x25, 0x69 },      /* 0x25  AK4377A_25_DAC32_DEBUG2		*/
	{ 0x26, 0x70 },      /* 0x26  AK4377A_26_DAC32_DEBUG2		*/
	{ 0x2A, 0x07 },      /* 0x2A  AK4377A_2A_LDO3_DEBUG1		*/
};

/* ak4376 register cache & default register settings */
static const struct reg_default ak4376_reg[] = {
	{ 0x0, 0x00 },	/*	0x00	AK4376_00_POWER_MANAGEMENT1		*/
	{ 0x1, 0x00 },	/*	0x01	AK4376_01_POWER_MANAGEMENT2		*/
	{ 0x2, 0x00 },	/*	0x02	AK4376_02_POWER_MANAGEMENT3		*/
	{ 0x3, 0x00 },	/*	0x03	AK4376_03_POWER_MANAGEMENT4		*/
	{ 0x4, 0x00 },	/*	0x04	AK4376_04_OUTPUT_MODE_SETTING	*/
	{ 0x5, 0x00 },	/*	0x05	AK4376_05_CLOCK_MODE_SELECT		*/
	{ 0x6, 0x00 },	/*	0x06	AK4376_06_DIGITAL_FILTER_SELECT	*/
	{ 0x7, 0x00 },	/*	0x07	AK4376_07_DAC_MONO_MIXING		*/
	{ 0x8, 0x00 },	/*	0x08	AK4376_08_RESERVED				*/
	{ 0x9, 0x00 },	/*	0x09	AK4376_09_RESERVED				*/
	{ 0xA, 0x00 },	/*	0x0A	AK4376_0A_RESERVED				*/
	{ 0xB, 0x19 },	/*	0x0B	AK4376_0B_LCH_OUTPUT_VOLUME		*/
	{ 0xC, 0x19 },	/*	0x0C	AK4376_0C_RCH_OUTPUT_VOLUME		*/
	{ 0xD, 0x0B },	/*	0x0D	AK4376_0D_HP_VOLUME_CONTROL		*/
	{ 0xE, 0x00 },	/*	0x0E	AK4376_0E_PLL_CLK_SOURCE_SELECT	*/
	{ 0xF, 0x00 },	/*	0x0F	AK4376_0F_PLL_REF_CLK_DIVIDER1	*/
	{ 0x10, 0x00 },	/*	0x10	AK4376_10_PLL_REF_CLK_DIVIDER2	*/
	{ 0x11, 0x00 },	/*	0x11	AK4376_11_PLL_FB_CLK_DIVIDER1	*/
	{ 0x12, 0x00 },	/*	0x12	AK4376_12_PLL_FB_CLK_DIVIDER2	*/
	{ 0x13, 0x00 },	/*	0x13	AK4376_13_DAC_CLK_SOURCE		*/
	{ 0x14, 0x00 },	/*	0x14	AK4376_14_DAC_CLK_DIVIDER		*/
	{ 0x15, 0x40 },	/*	0x15	AK4376_15_AUDIO_IF_FORMAT		*/
	{ 0x16, 0x00 },	/*	0x16	AK4376_16_DUMMY					*/
	{ 0x17, 0x00 },	/*	0x17	AK4376_17_DUMMY					*/
	{ 0x18, 0x00 },	/*	0x18	AK4376_18_DUMMY					*/
	{ 0x19, 0x00 },	/*	0x19	AK4376_19_DUMMY					*/
	{ 0x1A, 0x00 },	/*	0x1A	AK4376_1A_DUMMY					*/
	{ 0x1B, 0x00 },	/*	0x1B	AK4376_1B_DUMMY					*/
	{ 0x1C, 0x00 },	/*	0x1C	AK4376_1C_DUMMY					*/
	{ 0x1D, 0x00 },	/*	0x1D	AK4376_1D_DUMMY					*/
	{ 0x1E, 0x00 },	/*	0x1E	AK4376_1E_DUMMY					*/
	{ 0x1F, 0x00 },	/*	0x1F	AK4376_1F_DUMMY					*/
	{ 0x20, 0x00 },	/*	0x20	AK4376_20_DUMMY					*/
	{ 0x21, 0x00 },	/*	0x21	AK4376_21_DUMMY					*/
	{ 0x22, 0x00 },	/*	0x22	AK4376_22_DUMMY					*/
	{ 0x23, 0x00 },	/*	0x23	AK4376_23_DUMMY					*/
	{ 0x24, 0x00 },	/*	0x24	AK4376_24_MODE_CONTROL			*/
	{ 0x25, 0x6c }, /*	0x25                                      */
	{ 0x26, 0x20 }, /*	0x26    AK4376_26_DAC_ADJUSTMENT_1        */
	{ 0x27, 0x00 }, /*	0x27                                      */
	{ 0x28, 0x00 }, /*	0x28                                      */
	{ 0x29, 0x00 }, /*	0x29                                      */
	{ 0x2A, 0x07 }, /*	0x2A    AK4376_2A_DAC_ADJUSTMENT_2        */
};

static unsigned int ak4376_reg_read(struct snd_soc_component *component, unsigned int reg);
static bool ak4376_readable(struct device *dev, unsigned int reg);

#ifdef OPLUS_ARCH_EXTENDS
#ifdef USB_SWITCH_MAX20328
static struct snd_soc_component *ak4376_codec_info;
#endif
#endif /* OPLUS_ARCH_EXTENDS */

static int ak4376_update_register(struct snd_soc_component *component)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	int i;

	if (ak4376->deviceID == -1)
		return 0;

	if (ak4376->deviceID == 0x22) {
		for (i = 0; i < ARRAY_SIZE(ak4377a_patch); i++) {
			snd_soc_component_write(component, ak4377a_patch[i].reg, ak4377a_patch[i].def);
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(ak4376a_patch); i++) {
			snd_soc_component_write(component, ak4376a_patch[i].reg, ak4376a_patch[i].def);
		}
	}

	return 0;
}

/* GPIO control for PDN */
static int ak4376_pdn_control(struct snd_soc_component *component, int pdn)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	pr_info("%s pdn=%d\n", __func__, pdn);

	if ((ak4376->pdn1 == 0) && (pdn == 1)) {
		#ifdef OPLUS_ARCH_EXTENDS
		#ifdef USB_SWITCH_MAX20328
		mutex_lock(&ak4376->pdn_lock);
		#endif
		#endif /* OPLUS_ARCH_EXTENDS */

		ret = gpio_direction_output(ak4376->priv_pdn_en, 1);
		pr_info("%s: Turn on priv_pdn_en, ret=%d \n", __func__, ret);
		ak4376->pdn1 = 1;
		ak4376->pdn2 = 1;
		udelay(800);

		regcache_sync(ak4376->regmap);
		ak4376_update_register(component);

		#ifdef OPLUS_ARCH_EXTENDS
		#ifdef USB_SWITCH_MAX20328
		mutex_unlock(&ak4376->pdn_lock);
		#endif
		#endif /* OPLUS_ARCH_EXTENDS */
	} else if ((ak4376->pdn1 == 1) && (pdn == 0)) {
		#ifdef OPLUS_ARCH_EXTENDS
		#ifdef USB_SWITCH_MAX20328
		mutex_lock(&ak4376->pdn_lock);
		#endif
		#endif /* OPLUS_ARCH_EXTENDS */

		//i2c_lock_adapter(ak4376->i2c->adapter);
		i2c_lock_bus(ak4376->i2c->adapter,I2C_LOCK_ROOT_ADAPTER);
		ret = gpio_direction_output(ak4376->priv_pdn_en, 0);
		pr_info("%s: Turn off priv_pdn_en, ret=%d\n", __func__, ret);
		ak4376->pdn1 = 0;
		ak4376->pdn2 = 0;
		udelay(1);

		//i2c_unlock_adapter(ak4376->i2c->adapter);
		i2c_unlock_bus(ak4376->i2c->adapter, I2C_LOCK_ROOT_ADAPTER);
		regcache_mark_dirty(ak4376->regmap);
		pr_info("%s unlock adapter\n", __func__);

		#ifdef OPLUS_ARCH_EXTENDS
		#ifdef USB_SWITCH_MAX20328
		mutex_unlock(&ak4376->pdn_lock);
		#endif
		#endif /* OPLUS_ARCH_EXTENDS */
	}

	return 0;
}

#ifdef OPLUS_ARCH_EXTENDS
#ifdef USB_SWITCH_MAX20328
static int ak4376_pdn_control_non_blocked(struct snd_soc_component *component, int pdn)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	pr_info("%s pdn=%d\n", __func__, pdn);

	if ((ak4376->pdn1 == 0) && (pdn == 1)) {
		ret = gpio_direction_output(ak4376->priv_pdn_en, 1);
		pr_info("%s Turn on priv_pdn_en, ret=%d \n", __FUNCTION__, ret);
		ak4376->pdn1 = 1;
		ak4376->pdn2 = 1;
		udelay(800);

		regcache_sync(ak4376->regmap);
		ak4376_update_register(component);
	} else if ((ak4376->pdn1 == 1) && (pdn == 0)) {
		//i2c_lock_adapter(ak4376->i2c->adapter);
		i2c_lock_bus(ak4376->i2c->adapter,I2C_LOCK_ROOT_ADAPTER);
		ret = gpio_direction_output(ak4376->priv_pdn_en, 0);
		pr_info("%s Turn off priv_pdn_en, ret=%d\n", __FUNCTION__, ret);
		ak4376->pdn1 = 0;
		ak4376->pdn2 = 0;
		udelay(1);

		//i2c_unlock_adapter(ak4376->i2c->adapter);
		i2c_unlock_bus(ak4376->i2c->adapter, I2C_LOCK_ROOT_ADAPTER);
		pr_info("%s unlock adapter\n", __FUNCTION__);
	}

	return 0;
}
#endif
#endif /* OPLUS_ARCH_EXTENDS */

#ifdef CONFIG_DEBUG_FS_CODEC
static struct mutex io_lock;
static struct snd_soc_component *ak4376_codec;

static ssize_t reg_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret, i, fpt;
	#ifdef DAC_DEBUG
	int rx[30];
	#else
	int rx[22];
	#endif

	mutex_lock(&io_lock);
	for (i = 0; i < AK4376_16_DUMMY; i++) {
		ret = ak4376_reg_read(ak4376_codec, i);
		if (ret < 0) {
			pr_err("%s: read register error.\n", __func__);
			break;
		} else {
			rx[i] = ret;
		}
	}

	rx[22] = ak4376_reg_read(ak4376_codec, AK4376_24_MODE_CONTROL);
	mutex_unlock(&io_lock);

	if (i == 22) {
		ret = 0;
		fpt = 0;
		for (i = 0; i < AK4376_16_DUMMY; i++, fpt += 6) {
			ret += sprintf(buf + fpt, "%02x,%02x\n", i, rx[i]);
		}

		ret += sprintf(buf + i * 6, "24,%02x\n", rx[22]);

		return ret;
	} else {
		return sprintf(buf, "read error");
	}
}

static ssize_t reg_data_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	char *ptr_data = (char *)buf;
	char *p;
	int i, pt_count = 0;
	u16 val[20];

	while ((p = strsep(&ptr_data, ","))) {
		if (!*p)
			break;

		if (pt_count >= 20)
			break;

		val[pt_count] = simple_strtoul(p, NULL, 16);

		pt_count++;
	}

	mutex_lock(&io_lock);
	for (i = 0; i < pt_count; i+=2) {
		snd_soc_component_write(ak4376_codec, (unsigned int)val[i], (unsigned int)val[i+1]);
		pr_debug("%s: write add=%d, val=%d.\n", __func__, val[i], val[i+1]);
	}
	mutex_unlock(&io_lock);

	return count;
}

static DEVICE_ATTR(reg_data, 0666, reg_data_show, reg_data_store);

#endif

/* Output Digital volume control:
 * from -12.5 to 3 dB in 0.5 dB steps (mute instead of -12.5 dB) */
static DECLARE_TLV_DB_SCALE(ovl_tlv, -1250, 50, 0);
static DECLARE_TLV_DB_SCALE(ovr_tlv, -1250, 50, 0);

/* HP-Amp Analog volume control:
 * from -22 to 6 dB in 2 dB steps (mute instead of -42 dB) */
static DECLARE_TLV_DB_SCALE(hpg_tlv, -2200, 200, 0);

static const char *ak4376_ovolcn_select_texts[] = {"Dependent", "Independent"};
static const char *ak4376_mdacl_select_texts[] = {"x1", "x1/2"};
static const char *ak4376_mdacr_select_texts[] = {"x1", "x1/2"};
static const char *ak4376_invl_select_texts[] = {"Normal", "Inverting"};
static const char *ak4376_invr_select_texts[] = {"Normal", "Inverting"};
static const char *ak4376_cpmod_select_texts[] =
		{"Automatic Switching", "+-VDD Operation", "+-1/2VDD Operation"};
static const char *ak4376_hphl_select_texts[] = {"9ohm", "Hi-Z"};
static const char *ak4376_hphr_select_texts[] = {"9ohm", "Hi-Z"};
static const char *ak4376_dacfil_select_texts[]  =
		{"Sharp Roll-Off", "Slow Roll-Off", "Short Delay Sharp Roll-Off", "Short Delay Slow Roll-Off"};
static const char *ak4376_bcko_select_texts[] = {"64fs", "32fs"};
static const char *ak4376_dfthr_select_texts[] = {"Digital Filter", "Bypass"};
static const char *ak4376_ngate_select_texts[] = {"On", "Off"};
static const char *ak4376_ngatet_select_texts[] = {"Short", "Long"};

static const struct soc_enum ak4376_dac_enum[] = {
	SOC_ENUM_SINGLE(AK4376_0B_LCH_OUTPUT_VOLUME, 7,
			ARRAY_SIZE(ak4376_ovolcn_select_texts), ak4376_ovolcn_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 2,
			ARRAY_SIZE(ak4376_mdacl_select_texts), ak4376_mdacl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 6,
			ARRAY_SIZE(ak4376_mdacr_select_texts), ak4376_mdacr_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 3,
			ARRAY_SIZE(ak4376_invl_select_texts), ak4376_invl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 7,
			ARRAY_SIZE(ak4376_invr_select_texts), ak4376_invr_select_texts),
	SOC_ENUM_SINGLE(AK4376_03_POWER_MANAGEMENT4, 2,
			ARRAY_SIZE(ak4376_cpmod_select_texts), ak4376_cpmod_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 0,
			ARRAY_SIZE(ak4376_hphl_select_texts), ak4376_hphl_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 1,
			ARRAY_SIZE(ak4376_hphr_select_texts), ak4376_hphr_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 6,
			ARRAY_SIZE(ak4376_dacfil_select_texts), ak4376_dacfil_select_texts),
	SOC_ENUM_SINGLE(AK4376_15_AUDIO_IF_FORMAT, 3,
			ARRAY_SIZE(ak4376_bcko_select_texts), ak4376_bcko_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 3,
			ARRAY_SIZE(ak4376_dfthr_select_texts), ak4376_dfthr_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 0,
			ARRAY_SIZE(ak4376_ngate_select_texts), ak4376_ngate_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 1,
			ARRAY_SIZE(ak4376_ngatet_select_texts), ak4376_ngatet_select_texts),
};

static const char *bickfreq_on_select[] = {"32fs", "48fs", "64fs"};
static const char *pllmcki_on_select[] = {"9.6MHz", "11.2896MHz", "12.288MHz", "19.2MHz"};
static const char *lpmode_on_select[] = {"High Performance", "Low Power"};
static const char *xtalfreq_on_select[] = {"12.288MHz", "11.2896MHz"};
static const char *pdn_on_select[] = {"Off", "On"};
static const char *pllmode_on_select[] = {"PLL_OFF", "PLL_BICK_MODE", "PLL_MCKI_MODE", "XTAL_MODE"};

static const struct soc_enum ak4376_bitset_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(bickfreq_on_select), bickfreq_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pllmcki_on_select), pllmcki_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(lpmode_on_select), lpmode_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(xtalfreq_on_select), xtalfreq_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pdn_on_select), pdn_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pllmode_on_select), pllmode_on_select),
};

static int get_bickfs(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ucontrol->value.enumerated.item[0] = ak4376->nBickFreq;

	return 0;
}

static int ak4376_set_bickfs(struct snd_soc_component *component)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s(%d) nBickFreq=%d\n",__FUNCTION__,__LINE__,ak4376->nBickFreq);

	if (ak4376->nBickFreq == 0) { //32fs
		snd_soc_component_update_bits(component, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x01); //DL1-0=01(16bit, >=32fs)
	} else if(ak4376->nBickFreq == 1) { //48fs
		snd_soc_component_update_bits(component, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x00); //DL1-0=00(24bit, >=48fs)
	} else { //64fs
		snd_soc_component_update_bits(component, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x02); //DL1-0=1x(32bit, >=64fs)
	}

	return 0;
}

static int set_bickfs(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376->nBickFreq = ucontrol->value.enumerated.item[0];

	ak4376_set_bickfs(component);

	return 0;
}

static int get_pllmcki(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->nPllMCKI;

	return 0;
}

static int set_pllmcki(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->nPllMCKI = ucontrol->value.enumerated.item[0];

	return 0;
}

static int get_lpmode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->lpmode;

	return 0;
}

static int ak4376_set_lpmode(struct snd_soc_component *component)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	if (ak4376->lpmode == 0) { //High Performance Mode
		snd_soc_component_update_bits(component, AK4376_02_POWER_MANAGEMENT3, 0x10, 0x00); //LPMODE=0(High Performance Mode)
		if (ak4376->fs1 <= 12000) {
			snd_soc_component_update_bits(component, AK4376_24_MODE_CONTROL, 0x40, 0x40); //DSMLP=1
		} else {
			snd_soc_component_update_bits(component, AK4376_24_MODE_CONTROL, 0x40, 0x00); //DSMLP=0
		}
	} else { //Low Power Mode
		snd_soc_component_update_bits(component, AK4376_02_POWER_MANAGEMENT3, 0x10, 0x10); //LPMODE=1(Low Power Mode)
		snd_soc_component_update_bits(component, AK4376_24_MODE_CONTROL, 0x40, 0x40); //DSMLP=1
	}

	return 0;
}

static int set_lpmode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->lpmode = ucontrol->value.enumerated.item[0];

	ak4376_set_lpmode(component);

	return 0;
}

static int get_xtalfreq(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->xtalfreq;

	return 0;
}

static int set_xtalfreq(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->xtalfreq = ucontrol->value.enumerated.item[0];

	return 0;
}

static int get_pdn(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	pr_info("%s ak4376->pdn2=%d\n", __func__, ak4376->pdn2);

	ucontrol->value.enumerated.item[0] = ak4376->pdn2;

	return 0;
}

static int set_pdn(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->pdn2 = ucontrol->value.enumerated.item[0];

	pr_info("%s ak4376->pdn2=%d\n", __func__, ak4376->pdn2);

	//if (ak4376->pdn1 == 0)
	ak4376_pdn_control(component, ak4376->pdn2);

	return 0;
}

#ifdef OPLUS_ARCH_EXTENDS
static char const *ftm_hp_rev_text[] = {"NG", "OK"};
static const struct soc_enum ftm_hp_rev_enum  = SOC_ENUM_SINGLE_EXT(2, ftm_hp_rev_text);
static int ftm_hp_rev_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	int retries = 5;
	bool pdn = false;
	u8 devID;
	u8 chipID;
	u8 deviceID;

	if (ak4376->pdn1 == 0) {
		ak4376_pdn_control(component, 1);
		pdn = true;
		mdelay(10);
	}

retry:
	devID = ak4376_reg_read(component, AK4376_15_AUDIO_IF_FORMAT);
	devID &= 0xE0;
	devID >>= 1;

	pr_info("%s: devID 0x%02x\n", __func__, devID);
	if (devID == 0x20) {
		chipID = ak4376_reg_read(component, AK4376_21_CHIPID);
		chipID &= 0x07;
	} else {
		chipID = 0;
	}

	pr_info("%s: chipID 0x%02x\n", __func__, chipID);
	deviceID = devID + chipID;

	if (!(devID & 0x30)) {
		pr_err("%s i2c error at retries left: %d, devID:%x\n", __func__, retries, devID);
		if (retries) {
			retries--;
			msleep(5);
			goto retry;
		}
	}

	pr_info("%s: ID revision 0x%02x\n", __func__, deviceID);
	switch (ak4376->deviceID) {
	case 0x10:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x30:
		ucontrol->value.integer.value[0] = 1;
		break;
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	if (pdn) {
		ak4376_pdn_control(component, 0);
	}

	return 0;
}

static int ftm_hp_rev_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}
#endif /* OPLUS_ARCH_EXTENDS */

static int get_pllmode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ucontrol->value.enumerated.item[0] = ak4376->nPllMode;

	return 0;
}

static int set_pllmode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->nPllMode = ucontrol->value.enumerated.item[0];

	akdbgprt("\t[AK4376] %s(%d) nPllMode=%d\n",__FUNCTION__,__LINE__,ak4376->nPllMode);

	return 0;
}

#ifdef AK4376_DEBUG
static const char *test_reg_select[]   =
{
	"read AK4376 Reg 00:24",
};

static const struct soc_enum ak4376_enum[] =
{
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(test_reg_select), test_reg_select),
};

static int nTestRegNo = 0;

static int get_test_reg(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	/* Get the current output routing */
	ucontrol->value.enumerated.item[0] = nTestRegNo;

	return 0;
}

static int set_test_reg(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	u32 currMode = ucontrol->value.enumerated.item[0];
	int i, value;
	int regs, rege;

	nTestRegNo = currMode;

	regs = 0x00;
	rege = 0x24;

	for (i = regs; i <= rege; i++ ) {
		if (ak4376_readable(NULL, i)) {
			value = snd_soc_component_read32(component, i);
			printk("***AK4376 Addr,Reg=(%x, %x)\n", i, value);
		}
	}

	if (ak4376->deviceID == 0x22) {
		i = 0x25;
		value = ak4376_reg_read(component, i);
		printk("***AK4376 Addr,Reg=(%x, %x)\n", i, value);
	}
	i = 0x26;
	value = ak4376_reg_read(component, i);
	printk("***AK4376 Addr,Reg=(%x, %x)\n", i, value);
	i = 0x2A;
	value = ak4376_reg_read(component, i);
	printk("***AK4376 Addr,Reg=(%x, %x)\n", i, value);

	return 0;
}
#endif

static const struct snd_kcontrol_new ak4376_snd_controls[] = {
	SOC_SINGLE_TLV("AK4376 Digital Output VolumeL",
			AK4376_0B_LCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovl_tlv),
	SOC_SINGLE_TLV("AK4376 Digital Output VolumeR",
			AK4376_0C_RCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovr_tlv),
	SOC_SINGLE_TLV("AK4376 HP-Amp Analog Volume",
			AK4376_0D_HP_VOLUME_CONTROL, 0, 0x0F, 0, hpg_tlv),

	SOC_ENUM("AK4376 Digital Volume Control", ak4376_dac_enum[0]),
	SOC_ENUM("AK4376 DACL Signal Level", ak4376_dac_enum[1]),
	SOC_ENUM("AK4376 DACR Signal Level", ak4376_dac_enum[2]),
	SOC_ENUM("AK4376 DACL Signal Invert", ak4376_dac_enum[3]),
	SOC_ENUM("AK4376 DACR Signal Invert", ak4376_dac_enum[4]),
	SOC_ENUM("AK4376 Charge Pump Mode", ak4376_dac_enum[5]),
	SOC_ENUM("AK4376 HPL Power-down Resistor", ak4376_dac_enum[6]),
	SOC_ENUM("AK4376 HPR Power-down Resistor", ak4376_dac_enum[7]),
	SOC_ENUM("AK4376 DAC Digital Filter Mode", ak4376_dac_enum[8]),
	SOC_ENUM("AK4376 BICK Output Frequency", ak4376_dac_enum[9]),
	SOC_ENUM("AK4376 Digital Filter Mode", ak4376_dac_enum[10]),
	SOC_ENUM("AK4376 Noise Gate", ak4376_dac_enum[11]),
	SOC_ENUM("AK4376 Noise Gate Time", ak4376_dac_enum[12]),

	SOC_ENUM_EXT("AK4376 BICK Frequency Select", ak4376_bitset_enum[0], get_bickfs, set_bickfs),
	SOC_ENUM_EXT("AK4376 PLL MCKI Frequency", ak4376_bitset_enum[1], get_pllmcki, set_pllmcki),
	SOC_ENUM_EXT("AK4376 Low Power Mode", ak4376_bitset_enum[2], get_lpmode, set_lpmode),
	SOC_ENUM_EXT("AK4376 Xtal Frequency", ak4376_bitset_enum[3], get_xtalfreq, set_xtalfreq),
	SOC_ENUM_EXT("AK4376 PDN Control", ak4376_bitset_enum[4], get_pdn, set_pdn),
	SOC_ENUM_EXT("AK4376 PLL Mode", ak4376_bitset_enum[5], get_pllmode, set_pllmode),

#ifdef AK4376_DEBUG
	SOC_ENUM_EXT("Reg Read", ak4376_enum[0], get_test_reg, set_test_reg),
#endif

	#ifdef OPLUS_ARCH_EXTENDS
	SOC_ENUM_EXT("HP_Pa Revision", ftm_hp_rev_enum,
			ftm_hp_rev_get, ftm_hp_rev_put),
	#endif /* OPLUS_ARCH_EXTENDS */
};

/* DAC control */
static int ak4376_dac_event2(struct snd_soc_component *component, int event)
{
	u8 MSmode;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	MSmode = snd_soc_component_read32(component, AK4376_15_AUDIO_IF_FORMAT);
	pr_info("%s event = %d, MSmode = 0x%x, nPllMode=%d \n", __func__, event, MSmode, ak4376->nPllMode);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU: /* before widget power up */
		ak4376->nDACOn = 1;
		snd_soc_component_update_bits(component, AK4376_01_POWER_MANAGEMENT2, 0x01,0x01);//PMCP1=1
		mdelay(6); //wait 6ms
		udelay(500); //wait 0.5ms
		snd_soc_component_update_bits(component, AK4376_01_POWER_MANAGEMENT2, 0x30,0x30); //PMLDO1P/N=1
		mdelay(1); //wait 1ms
		break;
	case SND_SOC_DAPM_POST_PMU: /* after widget power up */
		snd_soc_component_update_bits(component, AK4376_01_POWER_MANAGEMENT2, 0x02,0x02); //PMCP2=1
		mdelay(4); //wait 4ms
		udelay(500); //wait 0.5ms
		break;
	case SND_SOC_DAPM_PRE_PMD: /* before widget power down */
		snd_soc_component_update_bits(component, AK4376_01_POWER_MANAGEMENT2, 0x02,0x00); //PMCP2=0
		break;
	case SND_SOC_DAPM_POST_PMD: /* after widget power down */
		snd_soc_component_update_bits(component, AK4376_01_POWER_MANAGEMENT2, 0x30,0x00); //PMLDO1P/N=0
		snd_soc_component_update_bits(component, AK4376_01_POWER_MANAGEMENT2, 0x01,0x00); //PMCP1=0

		if (ak4376->nPllMode == 0) {
			if (MSmode & 0x10) { //Master mode
				snd_soc_component_update_bits(component, AK4376_15_AUDIO_IF_FORMAT, 0x10,0x00); //MS bit = 0
			}
		}

		ak4376->nDACOn = 0;

		break;
	}

	return 0;
}

static int ak4376_dac_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_dac_event2(component, event);

	return 0;
}

/* PLL control */
static int ak4376_pll_event2(struct snd_soc_component *component, int event)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	pr_info("ak4376_pll_event2 event = %d\n",event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU: /* before widget power up */
	case SND_SOC_DAPM_POST_PMU: /* after widget power up */
		if ((ak4376->nPllMode == 1) || (ak4376->nPllMode == 2)) {
			snd_soc_component_update_bits(component, AK4376_00_POWER_MANAGEMENT1, 0x01,0x01); //PMPLL=1
		} else if (ak4376->nPllMode == 3) {
			snd_soc_component_update_bits(component, AK4376_00_POWER_MANAGEMENT1, 0x10,0x10); //PMOSC=1
		}
		break;
	case SND_SOC_DAPM_PRE_PMD: /* before widget power down */
	case SND_SOC_DAPM_POST_PMD: /* after widget power down */
		if ((ak4376->nPllMode == 1) || (ak4376->nPllMode == 2)) {
			snd_soc_component_update_bits(component, AK4376_00_POWER_MANAGEMENT1, 0x01,0x00); //PMPLL=0
		} else if (ak4376->nPllMode == 3) {
			snd_soc_component_update_bits(component, AK4376_00_POWER_MANAGEMENT1, 0x10,0x00); //PMOSC=0
		}
		break;
	}

	return 0;
}

static int ak4376_pll_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_pll_event2(component, event);

	return 0;
}

/* HPL Mixer */
static const struct snd_kcontrol_new ak4376_hpl_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACL", AK4376_07_DAC_MONO_MIXING, 0, 1, 0),
	SOC_DAPM_SINGLE("RDACL", AK4376_07_DAC_MONO_MIXING, 1, 1, 0),
};

/* HPR Mixer */
static const struct snd_kcontrol_new ak4376_hpr_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACR", AK4376_07_DAC_MONO_MIXING, 4, 1, 0),
	SOC_DAPM_SINGLE("RDACR", AK4376_07_DAC_MONO_MIXING, 5, 1, 0),
};

#ifdef OPLUS_ARCH_EXTENDS
#ifdef USB_SWITCH_MAX20328
/* HP-AMP */
static int ak4376_HPAmpL_event2(struct snd_soc_component *component, int event)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	pr_info("%s event = %d\n", __func__, event);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU: /* before widget power up */
		//snd_soc_component_update_bits(component, AK4376_04_OUTPUT_MODE_SETTING, 0x01, 0x00);
		mdelay(6); //wait 6ms
		break;
	case SND_SOC_DAPM_POST_PMU: /* after widget power up */
		break;
	case SND_SOC_DAPM_PRE_PMD: /* before widget power down */
		mutex_lock(&ak4376->pdn_lock);
		//snd_soc_component_update_bits(component, AK4376_04_OUTPUT_MODE_SETTING, 0x01, 0x01);
		mdelay(6); //wait 6ms
		mutex_unlock(&ak4376->pdn_lock);
		break;
	case SND_SOC_DAPM_POST_PMD: /* after widget power down */
		break;
	}

	return 0;
}

static int ak4376_HPAmpL_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
	struct snd_soc_codec *codec = w->codec;
#else
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
#endif

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_HPAmpL_event2(component, event);

	return 0;
}

static int ak4376_HPAmpR_event2(struct snd_soc_component *component, int event)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	pr_info("%s event = %d\n", __func__, event);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU: /* before widget power up */
		//snd_soc_component_update_bits(component, AK4376_04_OUTPUT_MODE_SETTING, 0x02, 0x00);
		mdelay(6); //wait 6ms
		break;
	case SND_SOC_DAPM_POST_PMU: /* after widget power up */
		break;
	case SND_SOC_DAPM_PRE_PMD: /* before widget power down */
		mutex_lock(&ak4376->pdn_lock);
		//snd_soc_component_update_bits(component, AK4376_04_OUTPUT_MODE_SETTING, 0x02, 0x02);
		mdelay(6); //wait 6ms
		mutex_unlock(&ak4376->pdn_lock);
		break;
	case SND_SOC_DAPM_POST_PMD: /* after widget power down */
		break;
	}

	return 0;
}

static int ak4376_HPAmpR_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
	struct snd_soc_codec *codec = w->codec;
#else
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
#endif

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_HPAmpR_event2(component, event);

	return 0;
}
#endif /* USB_SWITCH_MAX20328 */
#endif /* OPLUS_ARCH_EXTENDS */

/* ak4376 dapm widgets */
static const struct snd_soc_dapm_widget ak4376_dapm_widgets[] = {
	// DAC
	SND_SOC_DAPM_DAC_E("AK4376 DAC", "NULL", AK4376_02_POWER_MANAGEMENT3, 0, 0,
			ak4376_dac_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

	// PLL, OSC
	SND_SOC_DAPM_SUPPLY_S("AK4376 PLL", 0, SND_SOC_NOPM, 0, 0,
			ak4376_pll_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

	SND_SOC_DAPM_AIF_IN("AK4376 SDTI", "Playback", 0, SND_SOC_NOPM, 0, 0),

	// Analog Output
	SND_SOC_DAPM_OUTPUT("AK4376 HPL"),
	SND_SOC_DAPM_OUTPUT("AK4376 HPR"),

#ifdef OPLUS_ARCH_EXTENDS
#ifdef USB_SWITCH_MAX20328
	SND_SOC_DAPM_MIXER_E("AK4376 HPR Mixer", AK4376_03_POWER_MANAGEMENT4, 1, 0,
			&ak4376_hpr_mixer_controls[0], ARRAY_SIZE(ak4376_hpr_mixer_controls),
			ak4376_HPAmpR_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

	SND_SOC_DAPM_MIXER_E("AK4376 HPL Mixer", AK4376_03_POWER_MANAGEMENT4, 0, 0,
			&ak4376_hpl_mixer_controls[0], ARRAY_SIZE(ak4376_hpl_mixer_controls),
			ak4376_HPAmpL_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),
#else /* USB_SWITCH_MAX20328 */
	SND_SOC_DAPM_MIXER("AK4376 HPR Mixer", AK4376_03_POWER_MANAGEMENT4, 1, 0,
			&ak4376_hpr_mixer_controls[0], ARRAY_SIZE(ak4376_hpr_mixer_controls)),

	SND_SOC_DAPM_MIXER("AK4376 HPL Mixer", AK4376_03_POWER_MANAGEMENT4, 0, 0,
			&ak4376_hpl_mixer_controls[0], ARRAY_SIZE(ak4376_hpl_mixer_controls)),
#endif /* USB_SWITCH_MAX20328 */

#else /* OPLUS_ARCH_EXTENDS */
	SND_SOC_DAPM_MIXER("AK4376 HPR Mixer", AK4376_03_POWER_MANAGEMENT4, 1, 0,
			&ak4376_hpr_mixer_controls[0], ARRAY_SIZE(ak4376_hpr_mixer_controls)),

	SND_SOC_DAPM_MIXER("AK4376 HPL Mixer", AK4376_03_POWER_MANAGEMENT4, 0, 0,
			&ak4376_hpl_mixer_controls[0], ARRAY_SIZE(ak4376_hpl_mixer_controls)),
#endif /* OPLUS_ARCH_EXTENDS */
};

#ifdef OPLUS_ARCH_EXTENDS
#ifdef USB_SWITCH_MAX20328
int ak4376_HPLR_HZ(bool HPLR_HZ)
{
	int ret;
	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(ak4376_codec_info);
	pr_info("%s: HPLR_HZ:%s\n", __func__, (HPLR_HZ) ? "ENABLE" : "DISABLE");

	if (HPLR_HZ) {
		//enable AK4376 PDN
		mutex_lock(&ak4376->pdn_lock);
		if (!ak4376->pdn1) {
			pr_info("%s: pdn is off, turn it on\n", __func__);
			ak4376_pdn_control_non_blocked(ak4376_codec_info, 1);
			ak4376->pdn_pending = true;
			msleep(30);
		} else {
			ak4376->pdn_pending = false;
		}

		//turn off amp
		ret = ak4376_reg_read(ak4376_codec_info, AK4376_03_POWER_MANAGEMENT4);
		if (ret & 0x3) {
			pr_info("%s: amp still on, turn it off\n", __func__);
			ak4376->amp_pending = true;
			snd_soc_component_update_bits(ak4376_codec_info, AK4376_03_POWER_MANAGEMENT4, 0x03, 0x00);
			msleep(30);
		} else {
			ak4376->amp_pending = false;
		}

		//setup pull-down resistor
		snd_soc_component_update_bits(ak4376_codec_info, AK4376_04_OUTPUT_MODE_SETTING, 0x03, 0x03);
		msleep(20);
	} else {
		//setup pull-down resistor
		snd_soc_component_update_bits(ak4376_codec_info, AK4376_04_OUTPUT_MODE_SETTING, 0x03, 0x00);
		msleep(20);
		if (ak4376->amp_pending) {
			pr_info("%s: recover amp\n", __func__);
			ak4376->amp_pending = false;
			snd_soc_component_update_bits(ak4376_codec_info, AK4376_03_POWER_MANAGEMENT4, 0x03, 0x03);
			msleep(30);
		}

		//release PDN
		if (ak4376->pdn_pending) {
			pr_info("%s: recover pdn\n", __func__);
			ak4376->pdn_pending = false;
			ak4376_pdn_control_non_blocked(ak4376_codec_info, 0);
		}

		mutex_unlock(&ak4376->pdn_lock);
	}

	return 0;
}
EXPORT_SYMBOL(ak4376_HPLR_HZ);
#endif
#endif /* OPLUS_ARCH_EXTENDS */

static const struct snd_soc_dapm_route ak4376_intercon[] =
{
	{"AK4376 DAC", NULL, "AK4376 PLL"},
	{"AK4376 DAC", NULL, "AK4376 SDTI"},

	{"AK4376 HPL Mixer", "LDACL", "AK4376 DAC"},
	{"AK4376 HPL Mixer", "RDACL", "AK4376 DAC"},
	{"AK4376 HPR Mixer", "LDACR", "AK4376 DAC"},
	{"AK4376 HPR Mixer", "RDACR", "AK4376 DAC"},

	{"AK4376 HPL", NULL, "AK4376 HPL Mixer"},
	{"AK4376 HPR", NULL, "AK4376 HPR Mixer"},
};

static int ak4376_set_mcki(struct snd_soc_component *component, int fs, int rclk)
{
	u8 mode;
	u8 mode2;
	int mcki_rate;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s fs=%d rclk=%d\n",__FUNCTION__, fs, rclk);

	if ((fs != 0)&&(rclk != 0)) {
		if (rclk > 28800000) return -EINVAL;

		if (ak4376->nPllMode == 0) { //PLL_OFF
			mcki_rate = rclk/fs;
		} else { //XTAL_MODE
			if ( ak4376->xtalfreq == 0 ) { //12.288MHz
				mcki_rate = 12288000/fs;
			} else { //11.2896MHz
				mcki_rate = 11289600/fs;
			}
		}

		mode = snd_soc_component_read32(component, AK4376_05_CLOCK_MODE_SELECT);
		mode &= ~AK4376_CM;

		if (ak4376->lpmode == 0) { //High Performance Mode
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				mode2 = snd_soc_component_read32(component, AK4376_24_MODE_CONTROL);
				if ( fs <= 12000 ) {
					mode2 |= 0x40; //DSMLP=1
					snd_soc_component_write(component, AK4376_24_MODE_CONTROL, mode2);
				} else {
					mode2 &= ~0x40; //DSMLP=0
					snd_soc_component_write(component, AK4376_24_MODE_CONTROL, mode2);
				}
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				return -EINVAL;
			}
		} else { //Low Power Mode (LPMODE == DSMLP == 1)
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				return -EINVAL;
			}
		}

		snd_soc_component_write(component, AK4376_05_CLOCK_MODE_SELECT, mode);
	}

	return 0;
}

static int ak4376_set_pllblock(struct snd_soc_component *component, int fs)
{
	u8 mode;
	int nMClk, nPLLClk, nRefClk;
	int PLDbit, PLMbit, MDIVbit;
	int PLLMCKI;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	mode = snd_soc_component_read32(component, AK4376_05_CLOCK_MODE_SELECT);
	mode &= ~AK4376_CM;

	if (fs <= 24000) {
		mode |= AK4376_CM_1;
		nMClk = 512 * fs;
	} else if (fs <= 96000) {
		mode |= AK4376_CM_0;
		nMClk = 256 * fs;
	} else if (fs <= 192000) {
		mode |= AK4376_CM_3;
		nMClk = 128 * fs;
	} else { //fs > 192kHz
		mode |= AK4376_CM_1;
		nMClk = 64 * fs;
	}

	snd_soc_component_write(component, AK4376_05_CLOCK_MODE_SELECT, mode);

	if ((fs % 8000) == 0) {
		nPLLClk = 122880000;
	} else if ((fs == 11025) && (ak4376->nBickFreq == 1) && (ak4376->nPllMode == 1)) { //3.18
		nPLLClk = 101606400;
	} else {
		nPLLClk = 112896000;
	}

	if (ak4376->nPllMode == 1) { //BICK_PLL (Slave)
		if (ak4376->nBickFreq == 0) { //32fs
			if (fs <= 96000)
				PLDbit = 1;
			else if (fs <= 192000)
				PLDbit = 2;
			else
				PLDbit = 4;
			nRefClk = 32 * fs / PLDbit;
		} else if (ak4376->nBickFreq == 1) { //48fs
			if (fs <= 16000)
				PLDbit = 1;
			else if (fs <= 192000)
				PLDbit = 3;
			else
				PLDbit = 6;
			nRefClk = 48 * fs / PLDbit;
		} else { // 64fs
			if (fs <= 48000)
				PLDbit = 1;
			else if (fs <= 96000)
				PLDbit = 2;
			else if (fs <= 192000)
				PLDbit = 4;
			else
				PLDbit = 8;
			nRefClk = 64 * fs / PLDbit;
		}
	} else { //MCKI_PLL (Master)
		if (ak4376->nPllMCKI == 0) { //9.6MHz
			PLLMCKI = 9600000;
			if ((fs % 4000) == 0)
				nRefClk = 1920000;
			else
				nRefClk = 384000;
		} else if (ak4376->nPllMCKI == 1) { //11.2896MHz
			PLLMCKI = 11289600;
			if ((fs % 4000) == 0)
				return -EINVAL;
			else
				nRefClk = 2822400;
		} else if (ak4376->nPllMCKI == 2) { //12.288MHz
			PLLMCKI = 12288000;
			if ((fs % 4000) == 0)
				nRefClk = 3072000;
			else
				nRefClk = 768000;
		} else { //19.2MHz
			PLLMCKI = 19200000;
			if ((fs % 4000) == 0)
				nRefClk = 1920000;
			else
				nRefClk = 384000;
		}
		PLDbit = PLLMCKI / nRefClk;
	}

	PLMbit = nPLLClk / nRefClk;
	MDIVbit = nPLLClk / nMClk;

	PLDbit--;
	PLMbit--;
	MDIVbit--;

	//PLD15-0
	snd_soc_component_write(component, AK4376_0F_PLL_REF_CLK_DIVIDER1, ((PLDbit & 0xFF00) >> 8));
	snd_soc_component_write(component, AK4376_10_PLL_REF_CLK_DIVIDER2, ((PLDbit & 0x00FF) >> 0));
	//PLM15-0
	snd_soc_component_write(component, AK4376_11_PLL_FB_CLK_DIVIDER1, ((PLMbit & 0xFF00) >> 8));
	snd_soc_component_write(component, AK4376_12_PLL_FB_CLK_DIVIDER2, ((PLMbit & 0x00FF) >> 0));

	if (ak4376->nPllMode == 1 ) { //BICK_PLL (Slave)
		snd_soc_component_update_bits(component, AK4376_0E_PLL_CLK_SOURCE_SELECT, 0x03, 0x01); //PLS=1(BICK)
	} else { //MCKI PLL (Slave/Master)
		snd_soc_component_update_bits(component, AK4376_0E_PLL_CLK_SOURCE_SELECT, 0x03, 0x00); //PLS=0(MCKI)
	}

	//MDIV7-0
	snd_soc_component_write(component, AK4376_14_DAC_CLK_DIVIDER, MDIVbit);

	return 0;
}

static int ak4376_set_timer(struct snd_soc_component *component)
{
	int ret, curdata;
	int count, tm, nfs;
	int lvdtm, vddtm, hptm;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	lvdtm = 0;
	vddtm = 0;
	hptm = 0;

	nfs = ak4376->fs1;

	//LVDTM2-0 bits set
	ret = snd_soc_component_read32(component, AK4376_03_POWER_MANAGEMENT4);
	curdata = (ret & 0x70) >> 4; //Current data Save
	ret &= ~0x70;
	do {
		count = 1000 * (64 << lvdtm);
		tm = count / nfs;
		if (tm > LVDTM_HOLD_TIME) break;
		lvdtm++;
	} while (lvdtm < 7); //LVDTM2-0 = 0~7

	if (curdata != lvdtm) {
		snd_soc_component_write(component, AK4376_03_POWER_MANAGEMENT4, (ret | (lvdtm << 4)));
	}

	//VDDTM3-0 bits set
	ret = snd_soc_component_read32(component, AK4376_04_OUTPUT_MODE_SETTING);
	curdata = (ret & 0x3C) >> 2; //Current data Save
	ret &= ~0x3C;
	do {
		count = 1000 * (1024 << vddtm);
		tm = count / nfs;
		if (tm > VDDTM_HOLD_TIME) break;
		vddtm++;
	} while (vddtm < 8); //VDDTM3-0 = 0~8

	if (curdata != vddtm) {
		snd_soc_component_write(component, AK4376_04_OUTPUT_MODE_SETTING, (ret | (vddtm<<2)));
	}

	return 0;
}

static int ak4376_hw_params_set(struct snd_soc_component *component, int nfs1)
{
	u8 fs;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__, __LINE__);

	fs = snd_soc_component_read32(component, AK4376_05_CLOCK_MODE_SELECT);
	fs &= ~AK4376_FS;

	switch (nfs1) {
	case 8000:
		fs |= AK4376_FS_8KHZ;
		break;
	case 11025:
		fs |= AK4376_FS_11_025KHZ;
		break;
	case 16000:
		fs |= AK4376_FS_16KHZ;
		break;
	case 22050:
		fs |= AK4376_FS_22_05KHZ;
		break;
	case 32000:
		fs |= AK4376_FS_32KHZ;
		break;
	case 44100:
		fs |= AK4376_FS_44_1KHZ;
		break;
	case 48000:
		fs |= AK4376_FS_48KHZ;
		break;
	case 88200:
		fs |= AK4376_FS_88_2KHZ;
		break;
	case 96000:
		fs |= AK4376_FS_96KHZ;
		break;
	case 176400:
		fs |= AK4376_FS_176_4KHZ;
		break;
	case 192000:
		fs |= AK4376_FS_192KHZ;
		break;
	case 352800:
		fs |= AK4376_FS_352_8KHZ;
		break;
	case 384000:
		fs |= AK4376_FS_384KHZ;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_component_write(component, AK4376_05_CLOCK_MODE_SELECT, fs);

	if (ak4376->nPllMode == 0) { //PLL Off
		snd_soc_component_update_bits(component, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x00); //DACCKS=0
		ak4376_set_mcki(component, nfs1, ak4376->rclk);
	} else if (ak4376->nPllMode == 3) { //XTAL MODE
		snd_soc_component_update_bits(component, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x02); //DACCKS=2
		ak4376_set_mcki(component, nfs1, ak4376->rclk);
	} else { //PLL mode
		snd_soc_component_update_bits(component, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x01); //DACCKS=1
		ak4376_set_pllblock(component, nfs1);
	}

	ak4376_set_timer(component);

	return 0;
}

static int ak4376_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->fs1 = params_rate(params);

	printk("\t[AKM test] %s dai->name=%s\n", __FUNCTION__, dai->name); //AKM test
	ak4376_hw_params_set(component, ak4376->fs1);

	return 0;
}

static int ak4376_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s freq=%dHz(%d)\n", __FUNCTION__, freq, __LINE__);

	ak4376_pdn_control(component, 1);

	ak4376->rclk = freq;

	if ((ak4376->nPllMode == 0) || (ak4376->nPllMode == 3)) { //Not PLL mode
		ak4376_set_mcki(component, ak4376->fs1, freq);
	}

	return 0;
}

static int ak4376_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{

	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	u8 format;

	pr_info("%s fmt=0x%x, deviceID=%d \n", __func__, fmt, ak4376->deviceID);

	ak4376_pdn_control(component, 1);

	/* set master/slave audio interface */
	format = snd_soc_component_read32(component, AK4376_15_AUDIO_IF_FORMAT);
	format &= ~AK4376_DIF;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		format |= AK4376_SLAVE_MODE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		format |= AK4376_MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
	default:
		dev_err(component->dev, "Clock mode unsupported");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format |= AK4376_DIF_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format |= AK4376_DIF_MSB_MODE;
		break;
	default:
		return -EINVAL;
	}

	/* set format */
	pr_info("%s format=0x%x\n", __func__, format);
	snd_soc_component_write(component, AK4376_15_AUDIO_IF_FORMAT, format);

	return 0;
}

static bool ak4376_readable(struct device *dev, unsigned int reg)
{
	int ret = false;

	if (reg <= AK4376_2A_DAC_ADJUSTMENT_2) {
		ret = true;
	} else {
		ret = false;
	}

	return ret;
}

static bool ak4376_volatile(struct device *dev, unsigned int reg)
{
	int ret = false;

	switch (reg) {
	case AK4376_15_AUDIO_IF_FORMAT:
	case AK4376_21_CHIPID:
		ret = true;
		break;
	default:
		#ifdef AK4376_DEBUG
		ret = true;
		#else
		ret = false;
		#endif
		break;
	}

	return ret;
}

static bool ak4376_writeable(struct device *dev, unsigned int reg)
{
	bool ret = false;

	if (reg <= AK4376_2A_DAC_ADJUSTMENT_2) {
		ret = true;
	} else {
		ret = false;
	}

	return ret;
}

unsigned int ak4376_i2c_read(struct snd_soc_component *component, unsigned int reg)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	int ret = -1;
	unsigned char tx[1], rx[1];

	struct i2c_msg xfer[2];
	struct i2c_client *client = ak4376->i2c;

	tx[0] = reg;
	rx[0] = 0;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = tx;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 1;
	xfer[1].buf = rx;

	ret = i2c_transfer(client->adapter, xfer, 2);

	if (ret != 2) {
		akdbgprt("\t[ak4376] %s error ret = %d \n", __FUNCTION__, ret);
	}

	return (unsigned int)rx[0];
}

static unsigned int ak4376_reg_read(struct snd_soc_component *component, unsigned int reg)
{
	unsigned char tx[1];
	int wlen, rlen;
	int ret = 0;
	unsigned int rdata = 0;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	if (ak4376->pdn1 == 0) {
		rdata = snd_soc_component_read32(component, reg);
		dev_dbg(component->dev,"%s Read cache\n", __FUNCTION__);
	} else if ((ak4376->pdn1 == 1) || (ak4376->pdn1 == 2)) {
		wlen = 1;
		rlen = 1;
		tx[0] = reg;

		ret = ak4376_i2c_read(component, reg);

		dev_dbg(component->dev,"%s Read IC register, reg=0x%x val=0x%x\n", __FUNCTION__, reg, ret);

		if (ret < 0) {
			dev_dbg(component->dev,"%s error ret = %d\n", __FUNCTION__, ret);
			rdata = -EIO;
		} else {
			rdata = ret;
		}
	}

	return rdata;

}

// * for AK4376
static int ak4376_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *codec_dai)
{
	int ret = 0;

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__, __LINE__);

	return ret;
}


static int ak4376_set_bias_level(struct snd_soc_component *component,
		enum snd_soc_bias_level level)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	pr_info("%s: current bias_level=%d\n", __func__, snd_soc_component_get_bias_level(component));
	pr_info("%s: set bias_level=%d\n", __func__, level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_STANDBY)
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_STANDBY\n",__FUNCTION__,__LINE__);

		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_ON)
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level >= SND_SOC_BIAS_ON\n",__FUNCTION__,__LINE__);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_PREPARE) {
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_PREPARE\n",__FUNCTION__,__LINE__);
			//ak4376_pdn_control(codec, 0);
		}

		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_OFF\n",__FUNCTION__,__LINE__);
		break;
	case SND_SOC_BIAS_OFF:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_STANDBY) {
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_STANDBY\n",__FUNCTION__,__LINE__);
			#ifdef OPLUS_ARCH_EXTENDS
			if (ftm_pdn_on) {
				ak4376_pdn_control(component, 0);
				ftm_pdn_on = false;
			}
			#endif /* OPLUS_ARCH_EXTENDS */
		}
		break;
	}

	dapm->bias_level = level;

	return 0;
}

static int ak4376_set_dai_mute(struct snd_soc_dai *dai, int mute)
{
	u8 MSmode;
	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	pr_info("%s enter, mute=%d \n", __func__, mute);
	akdbgprt("\t[AK4376] %s(%d) snd_soc_codec_get_bias_level(codec)=%d\n", __FUNCTION__, __LINE__, snd_soc_component_get_bias_level(component));

	if (ak4376->nPllMode == 0) {
		if ( ak4376->nDACOn == 0 ) {
			MSmode = snd_soc_component_read32(component, AK4376_15_AUDIO_IF_FORMAT);
			if (MSmode & 0x10) { //Master mode
				snd_soc_component_update_bits(component, AK4376_15_AUDIO_IF_FORMAT, 0x10, 0x00); //MS bit = 0
			}
		}
	}

	if (snd_soc_component_get_bias_level(component) <= SND_SOC_BIAS_STANDBY) {
		akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level <= SND_SOC_BIAS_STANDBY\n", __FUNCTION__, __LINE__);
		ak4376_pdn_control(component, 0);
	}

	return 0;
}

#define AK4376_RATES		(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
				SNDRV_PCM_RATE_192000)

#define AK4376_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)


static struct snd_soc_dai_ops ak4376_dai_ops = {
	.hw_params = ak4376_hw_params,
	.set_sysclk = ak4376_set_dai_sysclk,
	.set_fmt = ak4376_set_dai_fmt,
	.trigger = ak4376_trigger,
	.digital_mute = ak4376_set_dai_mute,
};

struct snd_soc_dai_driver ak4376_dai[] = {
	{
		.name = "ak4376-AIF1",
		.playback = {
		       .stream_name = "Playback",
		       .channels_min = 1,
		       .channels_max = 2,
		       .rates = AK4376_RATES,
		       .formats = AK4376_FORMATS,
		},
		.ops = &ak4376_dai_ops,
	},
};

static int ak4376_init_reg(struct snd_soc_component *component)
{
	u8 devID;
	u8 chipID;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376->deviceID = -1;
	ak4376_pdn_control(component, 1);

	devID = ak4376_reg_read(component, AK4376_15_AUDIO_IF_FORMAT);
	devID &= 0xE0;
	devID >>= 1;

	if (devID == 0x20) {
		chipID = ak4376_reg_read(component, AK4376_21_CHIPID);
		chipID &= 0x07;
	} else {
		chipID = 0;
	}

	ak4376->deviceID = devID + chipID;

	pr_info("%s Device/ChipID=0x%X\n", __func__, ak4376->deviceID);

	switch (ak4376->deviceID) {
	case 0:
		pr_info("AK4375 is connecting.\n");
		break;
	case 0x10:
		pr_info("AK4375A is connecting.\n");
		break;
	case 0x20:
		pr_info("AK4376 is connecting.\n");
		break;
	case 0x21:
		pr_info("AK4376A is connecting.\n");
		break;
	case 0x22:
		pr_info("AK4377A is connecting.\n");
		break;
	case 0x30:
		pr_info("AK4377 is connecting.\n");
		break;
	case 0x70:
		pr_info("AK4331 is connecting.\n");
		break;
	default:
		pr_info("This device is not AK437X\n");
		break;
	}

	ak4376_update_register(component);

	return 0;
}

#ifndef OPLUS_ARCH_EXTENDS
static int ak4376_parse_dt(struct ak4376_priv *ak4376)
{
	struct device *dev;
	struct device_node *np;

	dev = &(ak4376->i2c->dev);

	np = dev->of_node;

	if (!np)
		return -1;

	printk("Read PDN pin from device tree\n");

	ak4376->priv_pdn_en = of_get_named_gpio(np, "ak4376,pdn-gpio", 0);
	if (ak4376->priv_pdn_en < 0) {
		ak4376->priv_pdn_en = -1;
		return -1;
	}

	if( !gpio_is_valid(ak4376->priv_pdn_en) ) {
		printk(KERN_ERR "ak4376 pdn pin(%u) is invalid\n", ak4376->priv_pdn_en);
		return -1;
	}

	return 0;
}
#endif /* OPLUS_ARCH_EXTENDS */

static int ak4376_probe(struct snd_soc_component *component)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	#ifndef OPLUS_ARCH_EXTENDS
	ret = ak4376_parse_dt(ak4376);
	akdbgprt("\t[AK4376] %s(%d) ret=%d\n",__FUNCTION__,__LINE__,ret);
	if (ret < 0)
		ak4376->pdn1 = 2; //No use GPIO control

	ret = gpio_request(ak4376->priv_pdn_en, "ak4376 pdn");
	akdbgprt("\t[AK4376] %s : gpio_request ret = %d\n",__FUNCTION__, ret);
	if (ret) {
		akdbgprt("\t[AK4376] %s(%d) cannot get ak4376 pdn gpio\n",__FUNCTION__,__LINE__);
		ak4376->pdn1 = 2; //No use GPIO control
	}

	ret = gpio_direction_output(ak4376->priv_pdn_en, 0);
	if (ret) {
		pr_info("%s pdn_en=0 fail, ret=%d\n", __func__, ret);
		gpio_free(ak4376->priv_pdn_en);
		ak4376->pdn1 = 2;
	} else {
		akdbgprt("\t[AK4376] %s(%d) pdn_en=0\n", __FUNCTION__,__LINE__);
	}
	#endif /* OPLUS_ARCH_EXTENDS */

	#ifdef CONFIG_DEBUG_FS_CODEC
	mutex_init(&io_lock);
	ak4376_codec = component;
	#endif

#ifdef OPLUS_ARCH_EXTENDS
#ifdef USB_SWITCH_MAX20328
	ak4376_codec_info = component;
	ak4376->pdn_pending = false;
	ak4376->amp_pending = false;
	mutex_init(&ak4376->pdn_lock);
#endif
#endif /* OPLUS_ARCH_EXTENDS */

	ak4376_init_reg(component);

	akdbgprt("\t[AK4376 Effect] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376->fs1 = 48000;
	ak4376->rclk = 0;
	ak4376->nBickFreq = 1; //0:32fs, 1:48fs, 2:64fs
	ak4376->nPllMCKI = 0; //0:9.6MHz, 1:11.2896MHz, 2:12.288MHz, 3:19.2MHz
	ak4376->lpmode = 0; //0:High Performance mode, 1:Low Power Mode
	ak4376->xtalfreq = 0; //0:12.288MHz, 1:11.2896MHz
	ak4376->nDACOn = 0;

#ifdef PLL_BICK_MODE //at ak4376_pdata.h
	ak4376->nPllMode = 1;
#else
	#ifdef PLL_MCKI_MODE
		ak4376->nPllMode = 2;
	#else
		#ifdef XTAL_MODE
			ak4376->nPllMode = 3;
		#endif
			ak4376->nPllMode = 0;
	#endif
#endif
	pr_info("%s nPllMode=%d \n", __func__, ak4376->nPllMode);

	return ret;
}

static void ak4376_remove(struct snd_soc_component *component)
{

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__, __LINE__);

	ak4376_set_bias_level(component, SND_SOC_BIAS_OFF);
}

static int ak4376_suspend(struct snd_soc_component *component)
{

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__, __LINE__);

	ak4376_set_bias_level(component, SND_SOC_BIAS_OFF);

	return 0;
}

static int ak4376_resume(struct snd_soc_component *component)
{

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__, __LINE__);

	//ak4376_init_reg(codec);

	return 0;
}

struct snd_soc_component_driver soc_codec_dev_ak4376 = {
	.name = AK4376_DRV_NAME,
    .probe = ak4376_probe,
	.remove = ak4376_remove,
	.suspend = ak4376_suspend,
	.resume = ak4376_resume,

	//.idle_bias_off = true,
	.idle_bias_on = false,
	.set_bias_level = ak4376_set_bias_level,
	.controls = ak4376_snd_controls,
	.num_controls = ARRAY_SIZE(ak4376_snd_controls),
	.dapm_widgets = ak4376_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ak4376_dapm_widgets),
	.dapm_routes = ak4376_intercon,
	.num_dapm_routes = ARRAY_SIZE(ak4376_intercon),
};
EXPORT_SYMBOL_GPL(soc_codec_dev_ak4376);

static const struct regmap_config ak4376_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = (AK4376_MAX_REGISTERS - 1),
	.readable_reg = ak4376_readable,
	.volatile_reg = ak4376_volatile,
	.writeable_reg = ak4376_writeable,

	.reg_defaults = ak4376_reg,
	.num_reg_defaults = ARRAY_SIZE(ak4376_reg),
	.cache_type = REGCACHE_RBTREE,

};

static struct of_device_id ak4376_i2c_dt_ids[] = {
	{.compatible = "akm,ak4376"},
	{ }
};
MODULE_DEVICE_TABLE(of, ak4376_i2c_dt_ids);

static int ak4376_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct ak4376_priv *ak4376;
	int ret = 0, ret2 = 0;

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__, __LINE__);

	ak4376 = kzalloc(sizeof(struct ak4376_priv), GFP_KERNEL);
	if (ak4376 == NULL)
		return -ENOMEM;

	ak4376->regmap = devm_regmap_init_i2c(i2c, &ak4376_regmap);
	if (IS_ERR(ak4376->regmap)) {
		akdbgprt("[*****AK4376*****] %s regmap error\n", __FUNCTION__);
		ret = PTR_ERR(ak4376->regmap);
		if (ak4376) {
			kfree(ak4376);
		}
		return ret;
	}

	#ifdef  CONFIG_DEBUG_FS_CODEC
	ret = device_create_file(&i2c->dev, &dev_attr_reg_data);
	if (ret) {
		pr_err("%s: Error to create reg_data\n", __FUNCTION__);
	}
	#endif

	i2c_set_clientdata(i2c, ak4376);

	ak4376->i2c = i2c;
	ak4376->pdn1 = 0;
	ak4376->pdn2 = 0;
	ak4376->priv_pdn_en = 0;

	#ifdef OPLUS_ARCH_EXTENDS
	ak4376->priv_pdn_en = of_get_named_gpio(i2c->dev.of_node,
			"ak4376,reset-gpio", 0);

	ret2 = gpio_request(ak4376->priv_pdn_en, "pdn_en");
	if (ret2) {
		akdbgprt("\t[AK4376] %s(%d) cannot get pdn_en gpio\n", __FUNCTION__, __LINE__);
		ak4376->pdn1 = 2;    //No use GPIO control
	} else {
		ret2 = gpio_direction_output(ak4376->priv_pdn_en, 1);
		if (ret2) {
			pr_info("%s pdn_en=0 fail, ret2=%d\n", __FUNCTION__, ret2);
			gpio_free(ak4376->priv_pdn_en);
			ak4376->pdn1 = 2;
		} else {
			dev_dbg(&i2c->dev, "vad_clock_en=0\n");
			akdbgprt("\t[AK4376] %s(%d) pdn_en=1\n", __FUNCTION__, __LINE__);
		}
	}
	#endif /* OPLUS_ARCH_EXTENDS */

	ret = snd_soc_register_component(&i2c->dev,
			&soc_codec_dev_ak4376, &ak4376_dai[0], ARRAY_SIZE(ak4376_dai));
	if (ret < 0) {
		kfree(ak4376);
		pr_err("\t[AK4376 Error!] %s(%d)\n", __FUNCTION__, __LINE__);
		return ret;
	}

	pr_info("%s: pdn1=%d, ret=%d", __func__, ak4376->pdn1, ret);

	return ret;
}

static int ak4376_i2c_remove(struct i2c_client *client)
{
	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	#ifdef CONFIG_DEBUG_FS_CODEC
	device_remove_file(&client->dev, &dev_attr_reg_data);
	#endif

	snd_soc_unregister_component(&client->dev);
	kfree(i2c_get_clientdata(client));

	return 0;
}

static const struct i2c_device_id ak4376_i2c_id[] = {
	{"ak4376", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, ak4376_i2c_id);

static struct i2c_driver ak4376_i2c_driver = {
	.driver = {
		.name = "ak4376",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ak4376_i2c_dt_ids), //3.18
	},
	.probe = ak4376_i2c_probe,
	.remove = ak4376_i2c_remove,
	.id_table = ak4376_i2c_id,
};

static int __init ak4376_modinit(void)
{

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__,__LINE__);

	return i2c_add_driver(&ak4376_i2c_driver);
}

module_init(ak4376_modinit);

static void __exit ak4376_exit(void)
{
	i2c_del_driver(&ak4376_i2c_driver);
}
module_exit(ak4376_exit);

MODULE_DESCRIPTION("ASoC ak4376 codec driver");
MODULE_LICENSE("GPL v2");
