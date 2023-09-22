/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2013, 2015, The Linux Foundation. All rights reserved.
 */

#ifndef _Q6_AUDIO_H_
#define _Q6_AUDIO_H_

#include <ipc/apr.h>

enum {
	LEGACY_PCM_MODE = 0,
	LOW_LATENCY_PCM_MODE,
	ULTRA_LOW_LATENCY_PCM_MODE,
	ULL_POST_PROCESSING_PCM_MODE,
};

#ifdef OPLUS_FEATURE_AUDIODETECT
extern int general_playback_muted_cnt;
extern int general_record_muted_cnt;
extern int general_playback_zd_cnt;
extern int general_record_zd_cnt;
extern int general_playback_pop_cnt;
extern int general_record_pop_cnt;
extern int general_playback_clip_cnt;
extern int general_record_clip_cnt;


extern int voip_rx_muted_cnt;
extern int voip_tx_muted_cnt;
extern int voip_rx_zd_cnt;
extern int voip_tx_zd_cnt;
extern int voip_rx_pop_cnt;
extern int voip_tx_pop_cnt;
extern int voip_rx_clip_cnt;
extern int voip_tx_clip_cnt;

extern int voice_rx_muted_cnt;
extern int voice_tx_muted_cnt;
extern int voice_rx_zd_cnt;
extern int voice_tx_zd_cnt;
extern int voice_rx_pop_cnt;
extern int voice_tx_pop_cnt;
extern int voice_rx_clip_cnt;
extern int voice_tx_clip_cnt;
#endif /* OPLUS_FEATURE_AUDIODETECT */

int q6audio_get_port_index(u16 port_id);

int q6audio_convert_virtual_to_portid(u16 port_id);

int q6audio_validate_port(u16 port_id);

int q6audio_is_digital_pcm_interface(u16 port_id);

int q6audio_get_port_id(u16 port_id);

#endif
