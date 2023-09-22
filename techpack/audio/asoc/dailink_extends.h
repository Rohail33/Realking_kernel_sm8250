/************************************************************************************
** File: -
**
** Copyright (C), 2020-2025, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**     Add for audio FE extends dailink
** Version: 1.0
** --------------------------- Revision History: --------------------------------
**               <author>                                <date>          <desc>
**
************************************************************************************/

#ifndef __DAILINK_EXTENDS_H
#define __DAILINK_EXTENDS_H

#define MI2S_TX_HOSTLESS_DAILINK(_name, _stream_name, _cpu_dai_name) \
{                                                           \
	.name = _name,                                      \
	.stream_name = _stream_name,                        \
	.cpu_dai_name = _cpu_dai_name,                      \
	.platform_name = "msm-pcm-hostless",                \
	.dynamic = 1,                                       \
	.dpcm_capture = 1,                                  \
	.trigger = {SND_SOC_DPCM_TRIGGER_POST,              \
			SND_SOC_DPCM_TRIGGER_POST},         \
	.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,           \
	.ignore_suspend = 1,                                \
	.ignore_pmdown_time = 1,                            \
	.codec_dai_name = "snd-soc-dummy-dai",              \
	.codec_name = "snd-soc-dummy",                      \
}                                                           \

#define TX_CDC_DMA_HOSTLESS_DAILINK(_name, _stream_name, _cpu_dai_name) \
{                                                           \
	.name = _name,                                      \
	.stream_name = _stream_name,                        \
	.cpu_dai_name = _cpu_dai_name,                      \
	.platform_name = "msm-pcm-hostless",                \
	.dynamic = 1,                                       \
	.dpcm_playback = 1,                                 \
	.dpcm_capture = 1,                                  \
	.trigger = {SND_SOC_DPCM_TRIGGER_POST,              \
			SND_SOC_DPCM_TRIGGER_POST},         \
	.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,           \
	.ignore_suspend = 1,                                \
	.codec_dai_name = "snd-soc-dummy-dai",              \
	.codec_name = "snd-soc-dummy",                      \
}                                                           \


/*Add for pri tdm_0 feedback*/
#define PRI_TDM_TX_HOSTLESS_DAILINK(_name, _stream_name, _cpu_dai_name) \
{                                                           \
	.name = _name,                                      \
	.stream_name = _stream_name,                        \
	.cpu_dai_name = _cpu_dai_name,                      \
	.platform_name = "msm-pcm-hostless",                \
	.dynamic = 1,                                       \
	.dpcm_capture = 1,                                  \
	.trigger = {SND_SOC_DPCM_TRIGGER_POST,              \
			SND_SOC_DPCM_TRIGGER_POST},         \
	.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,           \
	.ignore_suspend = 1,                                \
	.ignore_pmdown_time = 1,                            \
	.codec_dai_name = "snd-soc-dummy-dai",              \
	.codec_name = "snd-soc-dummy",                      \
}                                                           \


#endif /* __DAILINK_EXTENDS_H */
