// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
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
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include "aw8697.h"
#include "aw8697_reg.h"
#include "aw8697_config.h"
#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/proc_fs.h>
#include <linux/pm_qos.h>
#include <linux/vmalloc.h>
#include <soc/oplus/system/oplus_project.h>
#endif
#ifdef CONFIG_OPLUS_HAPTIC_OOS
#include <soc/oplus/system/boot_mode.h>
#endif

#ifdef CONFIG_OPLUS_HAPTIC_OOS
/* Total number of long vibration delay bin files
 * NOTE:- Any new long vib delay files added in future
 * should chance the count here
*/
#define AW8697_LONG_VIB_BIN_COUNT	25
/* Macro to evaluate long vib start index */
/*#define AW8697_LONG_INDEX_HEAD \
	(sizeof(aw8697_rtp_name)/AW8697_RTP_NAME_MAX - \
	 AW8697_LONG_VIB_BIN_COUNT)*/
#endif

#ifdef AAC_RICHTAP
#include <linux/mman.h>
#endif

/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AW8697_I2C_NAME "aw8697_haptic"
#define AW8697_HAPTIC_NAME "awinic_haptic"

#define AW8697_VERSION "v1.3.6"


#define AWINIC_RAM_UPDATE_DELAY

#define AW_I2C_RETRIES 10 //vincent modify for QC's sugestion 20190113
#define AW_I2C_RETRY_DELAY 2
#define AW_READ_CHIPID_RETRIES 5
#define AW_READ_CHIPID_RETRY_DELAY 2

#define AW8697_MAX_DSP_START_TRY_COUNT    10



#define AW8697_MAX_FIRMWARE_LOAD_CNT 20
#define OP_AW_DEBUG
//#define AISCAN_CTRL
/******************************************************
 *
 * variable
 *
 ******************************************************/
#define PM_QOS_VALUE_VB 400
struct pm_qos_request pm_qos_req_vb;

static uint8_t AW8697_HAPTIC_RAM_VBAT_COMP_GAIN;
#ifdef CONFIG_OPLUS_HAPTIC_OOS
static uint8_t AW8697_HAPTIC_HIGH_LEVEL_REG_VAL = 0x16;
#else
static uint8_t AW8697_HAPTIC_HIGH_LEVEL_REG_VAL = 0x18;
#endif

#define AW8697_RTP_NAME_MAX        64
//static char *aw8697_ram_name = "aw8697_haptic.bin";
//
static char aw8697_ram_name[5][30] ={
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_170.bin"},
};

static char aw8697_ram_name_0619[5][30] ={
{"aw8697_haptic_166.bin"},
{"aw8697_haptic_168.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_172.bin"},
{"aw8697_haptic_174.bin"},
};

static char aw8697_ram_name_1040[5][30] ={
{"aw8697_haptic_166.bin"},
{"aw8697_haptic_168.bin"},
{"aw8697_haptic_170.bin"},
{"aw8697_haptic_172.bin"},
{"aw8697_haptic_174.bin"},
};

static char aw8697_ram_name_170_soft[5][30] ={
{"aw8697_haptic_170_soft.bin"},
{"aw8697_haptic_170_soft.bin"},
{"aw8697_haptic_170_soft.bin"},
{"aw8697_haptic_170_soft.bin"},
{"aw8697_haptic_170_soft.bin"},
};

static char aw8697_ram_name_150[5][30] ={
{"aw8697_haptic_150.bin"},
{"aw8697_haptic_150.bin"},
{"aw8697_haptic_150.bin"},
{"aw8697_haptic_150.bin"},
{"aw8697_haptic_150.bin"},
};

static char aw8697_ram_name_150_soft[5][30] ={
{"aw8697_haptic_150_soft.bin"},
{"aw8697_haptic_150_soft.bin"},
{"aw8697_haptic_150_soft.bin"},
{"aw8697_haptic_150_soft.bin"},
{"aw8697_haptic_150_soft.bin"},
};

static char aw8697_long_sound_rtp_name[5][30] ={
    {"aw8697_long_sound_168.bin"},
    {"aw8697_long_sound_170.bin"},
    {"aw8697_long_sound_173.bin"},
    {"aw8697_long_sound_175.bin"},
};

static char aw8697_old_steady_test_rtp_name_0815[11][60] ={
    {"aw8697_old_steady_test_RTP_52_160Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_162Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_164Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_166Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_168Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_170Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_172Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_174Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_176Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_178Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_180Hz.bin"},
};

static char aw8697_old_steady_test_rtp_name_081538[11][60] ={
	{"aw8697_old_steady_test_RTP_52_145Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_146Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_147Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_148Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_149Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_150Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_151Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_152Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_153Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_154Hz.bin"},
	{"aw8697_old_steady_test_RTP_52_155Hz.bin"},
};

static char aw8697_high_temp_high_humidity_0815[11][60] ={
    {"aw8697_high_temp_high_humidity_channel_RTP_51_160Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_162Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_164Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_166Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_168Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_170Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_172Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_174Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_176Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_178Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_180Hz.bin"},
};

static char aw8697_high_temp_high_humidity_081538[11][60] ={
	{"aw8697_high_temp_high_humidity_channel_RTP_51_145Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_146Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_147Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_148Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_149Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_150Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_151Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_152Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_153Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_154Hz.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51_155Hz.bin"},
};

static char aw8697_old_steady_test_rtp_name_0832[11][60] ={
    {"aw8697_old_steady_test_RTP_52_225Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_226Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_227Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_228Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_229Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_230Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_231Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_232Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_233Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_234Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_235Hz.bin"},
};

static char aw8697_high_temp_high_humidity_0832[11][60] ={
    {"aw8697_high_temp_high_humidity_channel_RTP_51_225Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_226Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_227Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_228Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_229Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_230Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_231Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_232Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_233Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_234Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_235Hz.bin"},
};

static char aw8697_ringtone_rtp_f0_170_name[][AW8697_RTP_NAME_MAX] ={
        {"aw8697_rtp.bin"},
        {"aw8697_Hearty_channel_RTP_1_170.bin"},
        {"aw8697_Instant_channel_RTP_2_170.bin"},
        {"aw8697_Music_channel_RTP_3_170.bin"},
        {"aw8697_Percussion_channel_RTP_4_170.bin"},
        {"aw8697_Ripple_channel_RTP_5_170.bin"},
        {"aw8697_Bright_channel_RTP_6_170.bin"},
        {"aw8697_Fun_channel_RTP_7_170.bin"},
        {"aw8697_Glittering_channel_RTP_8_170.bin"},
        {"aw8697_Granules_channel_RTP_9_170.bin"},
        {"aw8697_Harp_channel_RTP_10_170.bin"},
        {"aw8697_Impression_channel_RTP_11_170.bin"},
        {"aw8697_Ingenious_channel_RTP_12_170.bin"},
        {"aw8697_Joy_channel_RTP_13_170.bin"},
        {"aw8697_Overtone_channel_RTP_14_170.bin"},
        {"aw8697_Receive_channel_RTP_15_170.bin"},
        {"aw8697_Splash_channel_RTP_16_170.bin"},

        {"aw8697_About_School_RTP_17_170.bin"},
        {"aw8697_Bliss_RTP_18_170.bin"},
        {"aw8697_Childhood_RTP_19_170.bin"},
        {"aw8697_Commuting_RTP_20_170.bin"},
        {"aw8697_Dream_RTP_21_170.bin"},
        {"aw8697_Firefly_RTP_22_170.bin"},
        {"aw8697_Gathering_RTP_23_170.bin"},
        {"aw8697_Gaze_RTP_24_170.bin"},
        {"aw8697_Lakeside_RTP_25_170.bin"},
        {"aw8697_Lifestyle_RTP_26_170.bin"},
        {"aw8697_Memories_RTP_27_170.bin"},
        {"aw8697_Messy_RTP_28_170.bin"},
        {"aw8697_Night_RTP_29_170.bin"},
        {"aw8697_Passionate_Dance_RTP_30_170.bin"},
        {"aw8697_Playground_RTP_31_170.bin"},
        {"aw8697_Relax_RTP_32_170.bin"},
        {"aw8697_Reminiscence_RTP_33_170.bin"},
        {"aw8697_Silence_From_Afar_RTP_34_170.bin"},
        {"aw8697_Silence_RTP_35_170.bin"},
        {"aw8697_Stars_RTP_36_170.bin"},
        {"aw8697_Summer_RTP_37_170.bin"},
        {"aw8697_Toys_RTP_38_170.bin"},
        {"aw8697_Travel_RTP_39_170.bin"},
        {"aw8697_Vision_RTP_40_170.bin"},

        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},
        {"aw8697_reserved.bin"},

        {"aw8697_reserved.bin"},
        {"aw8697_Simple_channel_RTP_48_170.bin"},
        {"aw8697_Pure_RTP_49_170.bin"},
        {"barca_alarm_ring_RTP_120_170.bin"},
        {"barca_incoming_ring_RTP_121_170.bin"},
        {"barca_notice_ring_RTP_122_170.bin"},
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static char aw8697_rtp_name_145Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
	{"aw8697_Hearty_channel_RTP_1.bin"},
	{"aw8697_Instant_channel_RTP_2_145Hz.bin"},
	{"aw8697_Music_channel_RTP_3.bin"},
	{"aw8697_Percussion_channel_RTP_4.bin"},
	{"aw8697_Ripple_channel_RTP_5.bin"},
	{"aw8697_Bright_channel_RTP_6.bin"},
	{"aw8697_Fun_channel_RTP_7.bin"},
	{"aw8697_Glittering_channel_RTP_8.bin"},
	{"aw8697_Granules_channel_RTP_9_145Hz.bin"},
	{"aw8697_Harp_channel_RTP_10.bin"},
	{"aw8697_Impression_channel_RTP_11_145Hz.bin"},
	{"aw8697_Ingenious_channel_RTP_12_145Hz.bin"},
	{"aw8697_Joy_channel_RTP_13_145Hz.bin"},
	{"aw8697_Overtone_channel_RTP_14.bin"},
	{"aw8697_Receive_channel_RTP_15_145Hz.bin"},
	{"aw8697_Splash_channel_RTP_16_145Hz.bin"},

	{"aw8697_About_School_RTP_17_145Hz.bin"},
	{"aw8697_Bliss_RTP_18.bin"},
	{"aw8697_Childhood_RTP_19_145Hz.bin"},
	{"aw8697_Commuting_RTP_20_145Hz.bin"},
	{"aw8697_Dream_RTP_21.bin"},
	{"aw8697_Firefly_RTP_22_145Hz.bin"},
	{"aw8697_Gathering_RTP_23.bin"},
	{"aw8697_Gaze_RTP_24_145Hz.bin"},
	{"aw8697_Lakeside_RTP_25_145Hz.bin"},
	{"aw8697_Lifestyle_RTP_26_145Hz.bin"},
	{"aw8697_Memories_RTP_27_145Hz.bin"},
	{"aw8697_Messy_RTP_28_145Hz.bin"},
	{"aw8697_Night_RTP_29_145Hz.bin"},
	{"aw8697_Passionate_Dance_RTP_30_145Hz.bin"},
	{"aw8697_Playground_RTP_31_145Hz.bin"},
	{"aw8697_Relax_RTP_32_145Hz.bin"},
	{"aw8697_Reminiscence_RTP_33.bin"},
	{"aw8697_Silence_From_Afar_RTP_34_145Hz.bin"},
	{"aw8697_Silence_RTP_35_145Hz.bin"},
	{"aw8697_Stars_RTP_36_145Hz.bin"},
	{"aw8697_Summer_RTP_37_145Hz.bin"},
	{"aw8697_Toys_RTP_38_145Hz.bin"},
	{"aw8697_Travel_RTP_39_145Hz.bin"},
	{"aw8697_Vision_RTP_40_145Hz.bin"},

	{"aw8697_waltz_channel_RTP_41_145Hz.bin"},
	{"aw8697_cut_channel_RTP_42_145Hz.bin"},
	{"aw8697_clock_channel_RTP_43_145Hz.bin"},
	{"aw8697_long_sound_channel_RTP_44_145Hz.bin"},
	{"aw8697_short_channel_RTP_45_145Hz.bin"},
	{"aw8697_two_error_remaind_RTP_46_145Hz.bin"},

	{"aw8697_kill_program_RTP_47_145Hz.bin"},
	{"aw8697_Simple_channel_RTP_48.bin"},
	{"aw8697_Pure_RTP_49_145Hz.bin"},
	{"aw8697_reserved_sound_channel_RTP_50.bin"},

	{"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

	{"aw8697_old_steady_test_RTP_52.bin"},
	{"aw8697_listen_pop_53_145Hz.bin"},
	{"aw8697_desk_7_RTP_54_145Hz.bin"},
	{"aw8697_nfc_10_RTP_55_145Hz.bin"},
	{"aw8697_vibrator_remain_12_RTP_56_145Hz.bin"},
	{"aw8697_notice_13_RTP_57.bin"},
	{"aw8697_third_ring_14_RTP_58.bin"},
	{"aw8697_reserved_59.bin"},

	{"aw8697_honor_fisrt_kill_RTP_60_145Hz.bin"},
	{"aw8697_honor_two_kill_RTP_61_145Hz.bin"},
	{"aw8697_honor_three_kill_RTP_62_145Hz.bin"},
	{"aw8697_honor_four_kill_RTP_63_145Hz.bin"},
	{"aw8697_honor_five_kill_RTP_64_145Hz.bin"},
	{"aw8697_honor_three_continu_kill_RTP_65_145Hz.bin"},
	{"aw8697_honor_four_continu_kill_RTP_66_145Hz.bin"},
	{"aw8697_honor_unstoppable_RTP_67_145Hz.bin"},
	{"aw8697_honor_thousands_kill_RTP_68_145Hz.bin"},
	{"aw8697_honor_lengendary_RTP_69_145Hz.bin"},


	{"aw8697_Freshmorning_RTP_70_145Hz.bin"},
	{"aw8697_Peaceful_RTP_71_145Hz.bin"},
	{"aw8697_Cicada_RTP_72_145Hz.bin"},
	{"aw8697_Electronica_RTP_73_145Hz.bin"},
	{"aw8697_Holiday_RTP_74_145Hz.bin"},
	{"aw8697_Funk_RTP_75_145Hz.bin"},
	{"aw8697_House_RTP_76_145Hz.bin"},
	{"aw8697_Temple_RTP_77_145Hz.bin"},
	{"aw8697_Dreamyjazz_RTP_78_145Hz.bin"},
	{"aw8697_Modern_RTP_79_145Hz.bin"},

	{"aw8697_Round_RTP_80_145Hz.bin"},
	{"aw8697_Rising_RTP_81_145Hz.bin"},
	{"aw8697_Wood_RTP_82_145Hz.bin"},
	{"aw8697_Heys_RTP_83_145Hz.bin"},
	{"aw8697_Mbira_RTP_84_145Hz.bin"},
	{"aw8697_News_RTP_85_145Hz.bin"},
	{"aw8697_Peak_RTP_86_145Hz.bin"},
	{"aw8697_Crisp_RTP_87_145Hz.bin"},
	{"aw8697_Singingbowls_RTP_88_145Hz.bin"},
	{"aw8697_Bounce_RTP_89_145Hz.bin"},

	{"aw8697_reserved_90.bin"},
	{"aw8697_reserved_91.bin"},
	{"aw8697_reserved_92.bin"},
	{"aw8697_reserved_93.bin"},
	{"aw8697_reserved_94.bin"},
	{"aw8697_reserved_95.bin"},
	{"aw8697_reserved_96.bin"},
	{"aw8697_reserved_97.bin"},
	{"aw8697_reserved_98.bin"},
	{"aw8697_reserved_99.bin"},

	{"aw8697_soldier_first_kill_RTP_100_145Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_145Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_145Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_145Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_145Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105_145Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_145Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_145Hz.bin"},
	{"aw8697_charging_simulation_RTP_108_145Hz.bin"},
	{"aw8697_fingerprint_success_RTP_109_145Hz.bin"},

	{"aw8697_fingerprint_effect1_RTP_110_145Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_145Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_145Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_145Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_145Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_145Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_145Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_145Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_145Hz.bin"},
	{"aw8697_reserved_119.bin"},

	{"aw8697_Miss_RTP_120.bin"},
	{"aw8697_Scenic_RTP_121_145Hz.bin"},
	{"aw8697_voice_assistant_RTP_122_145Hz.bin"},
/* used for 7 */
	{"aw8697_Appear_channel_RTP_123_145Hz.bin"},
	{"aw8697_Miss_RTP_124_145Hz.bin"},
	{"aw8697_Music_channel_RTP_125_145Hz.bin"},
	{"aw8697_Percussion_channel_RTP_126_145Hz.bin"},
	{"aw8697_Ripple_channel_RTP_127_145Hz.bin"},
	{"aw8697_Bright_channel_RTP_128_145Hz.bin"},
	{"aw8697_Fun_channel_RTP_129_145Hz.bin"},
	{"aw8697_Glittering_channel_RTP_130_145Hz.bin"},
	{"aw8697_Harp_channel_RTP_131_145Hz.bin"},
	{"aw8697_Overtone_channel_RTP_132_145Hz.bin"},
	{"aw8697_Simple_channel_RTP_133_145Hz.bin"},

	{"aw8697_Seine_past_RTP_134_145Hz.bin"},
	{"aw8697_Classical_ring_RTP_135_145Hz.bin"},
	{"aw8697_Long_for_RTP_136_145Hz.bin"},
	{"aw8697_Romantic_RTP_137_145Hz.bin"},
	{"aw8697_Bliss_RTP_138_145Hz.bin"},
	{"aw8697_Dream_RTP_139_145Hz.bin"},
	{"aw8697_Relax_RTP_140_145Hz.bin"},
	{"aw8697_Joy_channel_RTP_141_145Hz.bin"},
	{"aw8697_weather_wind_RTP_142_145Hz.bin"},
	{"aw8697_weather_cloudy_RTP_143_145Hz.bin"},
	{"aw8697_weather_thunderstorm_RTP_144_145Hz.bin"},
	{"aw8697_weather_default_RTP_145_145Hz.bin"},
	{"aw8697_weather_sunny_RTP_146_145Hz.bin"},
	{"aw8697_weather_smog_RTP_147_145Hz.bin"},
	{"aw8697_weather_snow_RTP_148_145Hz.bin"},
	{"aw8697_weather_rain_RTP_149_145Hz.bin"},

/* used for 7 end*/
	{"aw8697_rtp_lighthouse.bin"},
	{"aw8697_rtp_silk.bin"},
	{"aw8697_reserved_152.bin"},
	{"aw8697_reserved_153.bin"},
	{"aw8697_reserved_154.bin"},
	{"aw8697_reserved_155.bin"},
	{"aw8697_reserved_156.bin"},
	{"aw8697_reserved_157.bin"},
	{"aw8697_reserved_158.bin"},
	{"aw8697_reserved_159.bin"},
	{"aw8697_reserved_160.bin"},

	{"aw8697_reserved_161.bin"},
	{"aw8697_reserved_162.bin"},
	{"aw8697_reserved_163.bin"},
	{"aw8697_reserved_164.bin"},
	{"aw8697_reserved_165.bin"},
	{"aw8697_reserved_166.bin"},
	{"aw8697_reserved_167.bin"},
	{"aw8697_reserved_168.bin"},
	{"aw8697_reserved_169.bin"},
	{"aw8697_reserved_170.bin"},
	{"aw8697_Threefingers_Long_RTP_171_145Hz.bin"},
	{"aw8697_Threefingers_Up_RTP_172_145Hz.bin"},
	{"aw8697_Threefingers_Screenshot_RTP_173_145Hz.bin"},
	{"aw8697_Unfold_RTP_174_145Hz.bin"},
	{"aw8697_Close_RTP_175_145Hz.bin"},
	{"aw8697_HalfLap_RTP_176_145Hz.bin"},
	{"aw8697_Twofingers_Down_RTP_177_145Hz.bin"},
	{"aw8697_Twofingers_Long_RTP_178_145Hz.bin"},
	{"aw8697_Compatible_1_RTP_179_145Hz.bin"},
	{"aw8697_Compatible_2_RTP_180_145Hz.bin"},
	{"aw8697_Styleswitch_RTP_181_145Hz.bin"},
	{"aw8697_Waterripple_RTP_182_145Hz.bin"},
	{"aw8697_Suspendbutton_Bottomout_RTP_183_145Hz.bin"},
	{"aw8697_Suspendbutton_Menu_RTP_184_145Hz.bin"},
	{"aw8697_Complete_RTP_185_145Hz.bin"},
	{"aw8697_Bulb_RTP_186_145Hz.bin"},
	{"aw8697_Elasticity_RTP_187_145Hz.bin"},
	{"aw8697_Styleswitch_Soft_RTP_188_145Hz.bin"},
	{"aw8697_reserved_189.bin"},
	{"aw8697_reserved_190.bin"},
	{"aw8697_reserved_191.bin"},
	{"aw8697_reserved_192.bin"},
	{"aw8697_reserved_193.bin"},
	{"aw8697_reserved_194.bin"},
	{"aw8697_reserved_195.bin"},
	{"aw8697_reserved_196.bin"},
	{"aw8697_reserved_197.bin"},
	{"aw8697_reserved_198.bin"},
	{"aw8697_reserved_199.bin"},
	{"aw8697_reserved_200.bin"},
};

static char aw8697_rtp_name_150Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
	{"aw8697_Hearty_channel_RTP_1.bin"},
	{"aw8697_Instant_channel_RTP_2_150Hz.bin"},
	{"aw8697_Music_channel_RTP_3.bin"},
	{"aw8697_Percussion_channel_RTP_4.bin"},
	{"aw8697_Ripple_channel_RTP_5.bin"},
	{"aw8697_Bright_channel_RTP_6.bin"},
	{"aw8697_Fun_channel_RTP_7.bin"},
	{"aw8697_Glittering_channel_RTP_8.bin"},
	{"aw8697_Granules_channel_RTP_9_150Hz.bin"},
	{"aw8697_Harp_channel_RTP_10.bin"},
	{"aw8697_Impression_channel_RTP_11_150Hz.bin"},
	{"aw8697_Ingenious_channel_RTP_12_150Hz.bin"},
	{"aw8697_Joy_channel_RTP_13_150Hz.bin"},
	{"aw8697_Overtone_channel_RTP_14.bin"},
	{"aw8697_Receive_channel_RTP_15_150Hz.bin"},
	{"aw8697_Splash_channel_RTP_16_150Hz.bin"},

	{"aw8697_About_School_RTP_17_150Hz.bin"},
	{"aw8697_Bliss_RTP_18.bin"},
	{"aw8697_Childhood_RTP_19_150Hz.bin"},
	{"aw8697_Commuting_RTP_20_150Hz.bin"},
	{"aw8697_Dream_RTP_21.bin"},
	{"aw8697_Firefly_RTP_22_150Hz.bin"},
	{"aw8697_Gathering_RTP_23.bin"},
	{"aw8697_Gaze_RTP_24_150Hz.bin"},
	{"aw8697_Lakeside_RTP_25_150Hz.bin"},
	{"aw8697_Lifestyle_RTP_26_150Hz.bin"},
	{"aw8697_Memories_RTP_27_150Hz.bin"},
	{"aw8697_Messy_RTP_28_150Hz.bin"},
	{"aw8697_Night_RTP_29_150Hz.bin"},
	{"aw8697_Passionate_Dance_RTP_30_150Hz.bin"},
	{"aw8697_Playground_RTP_31_150Hz.bin"},
	{"aw8697_Relax_RTP_32_150Hz.bin"},
	{"aw8697_Reminiscence_RTP_33.bin"},
	{"aw8697_Silence_From_Afar_RTP_34_150Hz.bin"},
	{"aw8697_Silence_RTP_35_150Hz.bin"},
	{"aw8697_Stars_RTP_36_150Hz.bin"},
	{"aw8697_Summer_RTP_37_150Hz.bin"},
	{"aw8697_Toys_RTP_38_150Hz.bin"},
	{"aw8697_Travel_RTP_39_150Hz.bin"},
	{"aw8697_Vision_RTP_40_150Hz.bin"},

	{"aw8697_waltz_channel_RTP_41_150Hz.bin"},
	{"aw8697_cut_channel_RTP_42_150Hz.bin"},
	{"aw8697_clock_channel_RTP_43_150Hz.bin"},
	{"aw8697_long_sound_channel_RTP_44_150Hz.bin"},
	{"aw8697_short_channel_RTP_45_150Hz.bin"},
	{"aw8697_two_error_remaind_RTP_46_150Hz.bin"},

	{"aw8697_kill_program_RTP_47_150Hz.bin"},
	{"aw8697_Simple_channel_RTP_48.bin"},
	{"aw8697_Pure_RTP_49_150Hz.bin"},
	{"aw8697_reserved_sound_channel_RTP_50.bin"},

	{"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

	{"aw8697_old_steady_test_RTP_52.bin"},
	{"aw8697_listen_pop_53_150Hz.bin"},
	{"aw8697_desk_7_RTP_54_150Hz.bin"},
	{"aw8697_nfc_10_RTP_55_150Hz.bin"},
	{"aw8697_vibrator_remain_12_RTP_56_150Hz.bin"},
	{"aw8697_notice_13_RTP_57.bin"},
	{"aw8697_third_ring_14_RTP_58.bin"},
	{"aw8697_reserved_59.bin"},

	{"aw8697_honor_fisrt_kill_RTP_60_150Hz.bin"},
	{"aw8697_honor_two_kill_RTP_61_150Hz.bin"},
	{"aw8697_honor_three_kill_RTP_62_150Hz.bin"},
	{"aw8697_honor_four_kill_RTP_63_150Hz.bin"},
	{"aw8697_honor_five_kill_RTP_64_150Hz.bin"},
	{"aw8697_honor_three_continu_kill_RTP_65_150Hz.bin"},
	{"aw8697_honor_four_continu_kill_RTP_66_150Hz.bin"},
	{"aw8697_honor_unstoppable_RTP_67_150Hz.bin"},
	{"aw8697_honor_thousands_kill_RTP_68_150Hz.bin"},
	{"aw8697_honor_lengendary_RTP_69_150Hz.bin"},


	{"aw8697_Freshmorning_RTP_70_150Hz.bin"},
	{"aw8697_Peaceful_RTP_71_150Hz.bin"},
	{"aw8697_Cicada_RTP_72_150Hz.bin"},
	{"aw8697_Electronica_RTP_73_150Hz.bin"},
	{"aw8697_Holiday_RTP_74_150Hz.bin"},
	{"aw8697_Funk_RTP_75_150Hz.bin"},
	{"aw8697_House_RTP_76_150Hz.bin"},
	{"aw8697_Temple_RTP_77_150Hz.bin"},
	{"aw8697_Dreamyjazz_RTP_78_150Hz.bin"},
	{"aw8697_Modern_RTP_79_150Hz.bin"},

	{"aw8697_Round_RTP_80_150Hz.bin"},
	{"aw8697_Rising_RTP_81_150Hz.bin"},
	{"aw8697_Wood_RTP_82_150Hz.bin"},
	{"aw8697_Heys_RTP_83_150Hz.bin"},
	{"aw8697_Mbira_RTP_84_150Hz.bin"},
	{"aw8697_News_RTP_85_150Hz.bin"},
	{"aw8697_Peak_RTP_86_150Hz.bin"},
	{"aw8697_Crisp_RTP_87_150Hz.bin"},
	{"aw8697_Singingbowls_RTP_88_150Hz.bin"},
	{"aw8697_Bounce_RTP_89_150Hz.bin"},

	{"aw8697_reserved_90.bin"},
	{"aw8697_reserved_91.bin"},
	{"aw8697_reserved_92.bin"},
	{"aw8697_reserved_93.bin"},
	{"aw8697_reserved_94.bin"},
	{"aw8697_reserved_95.bin"},
	{"aw8697_reserved_96.bin"},
	{"aw8697_reserved_97.bin"},
	{"aw8697_reserved_98.bin"},
	{"aw8697_reserved_99.bin"},

	{"aw8697_soldier_first_kill_RTP_100_150Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_150Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_150Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_150Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_150Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105_150Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_150Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_150Hz.bin"},
	{"aw8697_charging_simulation_RTP_108_150Hz.bin"},
	{"aw8697_fingerprint_success_RTP_109_150Hz.bin"},

	{"aw8697_fingerprint_effect1_RTP_110_150Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_150Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_150Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_150Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_150Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_150Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_150Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_150Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_150Hz.bin"},
	{"aw8697_reserved_119.bin"},

	{"aw8697_Miss_RTP_120.bin"},
	{"aw8697_Scenic_RTP_121_150Hz.bin"},
	{"aw8697_voice_assistant_RTP_122_150Hz.bin"},
/* used for 7 */
	{"aw8697_Appear_channel_RTP_123_150Hz.bin"},
	{"aw8697_Miss_RTP_124_150Hz.bin"},
	{"aw8697_Music_channel_RTP_125_150Hz.bin"},
	{"aw8697_Percussion_channel_RTP_126_150Hz.bin"},
	{"aw8697_Ripple_channel_RTP_127_150Hz.bin"},
	{"aw8697_Bright_channel_RTP_128_150Hz.bin"},
	{"aw8697_Fun_channel_RTP_129_150Hz.bin"},
	{"aw8697_Glittering_channel_RTP_130_150Hz.bin"},
	{"aw8697_Harp_channel_RTP_131_150Hz.bin"},
	{"aw8697_Overtone_channel_RTP_132_150Hz.bin"},
	{"aw8697_Simple_channel_RTP_133_150Hz.bin"},

	{"aw8697_Seine_past_RTP_134_150Hz.bin"},
	{"aw8697_Classical_ring_RTP_135_150Hz.bin"},
	{"aw8697_Long_for_RTP_136_150Hz.bin"},
	{"aw8697_Romantic_RTP_137_150Hz.bin"},
	{"aw8697_Bliss_RTP_138_150Hz.bin"},
	{"aw8697_Dream_RTP_139_150Hz.bin"},
	{"aw8697_Relax_RTP_140_150Hz.bin"},
	{"aw8697_Joy_channel_RTP_141_150Hz.bin"},
	{"aw8697_weather_wind_RTP_142_150Hz.bin"},
	{"aw8697_weather_cloudy_RTP_143_150Hz.bin"},
	{"aw8697_weather_thunderstorm_RTP_144_150Hz.bin"},
	{"aw8697_weather_default_RTP_145_150Hz.bin"},
	{"aw8697_weather_sunny_RTP_146_150Hz.bin"},
	{"aw8697_weather_smog_RTP_147_150Hz.bin"},
	{"aw8697_weather_snow_RTP_148_150Hz.bin"},
	{"aw8697_weather_rain_RTP_149_150Hz.bin"},

/* used for 7 end*/
	{"aw8697_rtp_lighthouse.bin"},
	{"aw8697_rtp_silk.bin"},
	{"aw8697_reserved_152.bin"},
	{"aw8697_reserved_153.bin"},
	{"aw8697_reserved_154.bin"},
	{"aw8697_reserved_155.bin"},
	{"aw8697_reserved_156.bin"},
	{"aw8697_reserved_157.bin"},
	{"aw8697_reserved_158.bin"},
	{"aw8697_reserved_159.bin"},
	{"aw8697_reserved_160.bin"},

	{"aw8697_reserved_161.bin"},
	{"aw8697_reserved_162.bin"},
	{"aw8697_reserved_163.bin"},
	{"aw8697_reserved_164.bin"},
	{"aw8697_reserved_165.bin"},
	{"aw8697_reserved_166.bin"},
	{"aw8697_reserved_167.bin"},
	{"aw8697_reserved_168.bin"},
	{"aw8697_reserved_169.bin"},
	{"aw8697_reserved_170.bin"},
	{"aw8697_Threefingers_Long_RTP_171_150Hz.bin"},
	{"aw8697_Threefingers_Up_RTP_172_150Hz.bin"},
	{"aw8697_Threefingers_Screenshot_RTP_173_150Hz.bin"},
	{"aw8697_Unfold_RTP_174_150Hz.bin"},
	{"aw8697_Close_RTP_175_150Hz.bin"},
	{"aw8697_HalfLap_RTP_176_150Hz.bin"},
	{"aw8697_Twofingers_Down_RTP_177_150Hz.bin"},
	{"aw8697_Twofingers_Long_RTP_178_150Hz.bin"},
	{"aw8697_Compatible_1_RTP_179_150Hz.bin"},
	{"aw8697_Compatible_2_RTP_180_150Hz.bin"},
	{"aw8697_Styleswitch_RTP_181_150Hz.bin"},
	{"aw8697_Waterripple_RTP_182_150Hz.bin"},
	{"aw8697_Suspendbutton_Bottomout_RTP_183_150Hz.bin"},
	{"aw8697_Suspendbutton_Menu_RTP_184_150Hz.bin"},
	{"aw8697_Complete_RTP_185_150Hz.bin"},
	{"aw8697_Bulb_RTP_186_150Hz.bin"},
	{"aw8697_Elasticity_RTP_187_150Hz.bin"},
	{"aw8697_Styleswitch_Soft_RTP_188_150Hz.bin"},
	{"aw8697_reserved_189.bin"},
	{"aw8697_reserved_190.bin"},
	{"aw8697_reserved_191.bin"},
	{"aw8697_reserved_192.bin"},
	{"aw8697_reserved_193.bin"},
	{"aw8697_reserved_194.bin"},
	{"aw8697_reserved_195.bin"},
	{"aw8697_reserved_196.bin"},
	{"aw8697_reserved_197.bin"},
	{"aw8697_reserved_198.bin"},
	{"aw8697_reserved_199.bin"},
	{"aw8697_reserved_200.bin"},
};

static char aw8697_rtp_name_155Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
	{"aw8697_Hearty_channel_RTP_1.bin"},
	{"aw8697_Instant_channel_RTP_2_155Hz.bin"},
	{"aw8697_Music_channel_RTP_3.bin"},
	{"aw8697_Percussion_channel_RTP_4.bin"},
	{"aw8697_Ripple_channel_RTP_5.bin"},
	{"aw8697_Bright_channel_RTP_6.bin"},
	{"aw8697_Fun_channel_RTP_7.bin"},
	{"aw8697_Glittering_channel_RTP_8.bin"},
	{"aw8697_Granules_channel_RTP_9_155Hz.bin"},
	{"aw8697_Harp_channel_RTP_10.bin"},
	{"aw8697_Impression_channel_RTP_11_155Hz.bin"},
	{"aw8697_Ingenious_channel_RTP_12_155Hz.bin"},
	{"aw8697_Joy_channel_RTP_13_155Hz.bin"},
	{"aw8697_Overtone_channel_RTP_14.bin"},
	{"aw8697_Receive_channel_RTP_15_155Hz.bin"},
	{"aw8697_Splash_channel_RTP_16_155Hz.bin"},

	{"aw8697_About_School_RTP_17_155Hz.bin"},
	{"aw8697_Bliss_RTP_18.bin"},
	{"aw8697_Childhood_RTP_19_155Hz.bin"},
	{"aw8697_Commuting_RTP_20_155Hz.bin"},
	{"aw8697_Dream_RTP_21.bin"},
	{"aw8697_Firefly_RTP_22_155Hz.bin"},
	{"aw8697_Gathering_RTP_23.bin"},
	{"aw8697_Gaze_RTP_24_155Hz.bin"},
	{"aw8697_Lakeside_RTP_25_155Hz.bin"},
	{"aw8697_Lifestyle_RTP_26_155Hz.bin"},
	{"aw8697_Memories_RTP_27_155Hz.bin"},
	{"aw8697_Messy_RTP_28_155Hz.bin"},
	{"aw8697_Night_RTP_29_155Hz.bin"},
	{"aw8697_Passionate_Dance_RTP_30_155Hz.bin"},
	{"aw8697_Playground_RTP_31_155Hz.bin"},
	{"aw8697_Relax_RTP_32_155Hz.bin"},
	{"aw8697_Reminiscence_RTP_33.bin"},
	{"aw8697_Silence_From_Afar_RTP_34_155Hz.bin"},
	{"aw8697_Silence_RTP_35_155Hz.bin"},
	{"aw8697_Stars_RTP_36_155Hz.bin"},
	{"aw8697_Summer_RTP_37_155Hz.bin"},
	{"aw8697_Toys_RTP_38_155Hz.bin"},
	{"aw8697_Travel_RTP_39_155Hz.bin"},
	{"aw8697_Vision_RTP_40_155Hz.bin"},

	{"aw8697_waltz_channel_RTP_41_155Hz.bin"},
	{"aw8697_cut_channel_RTP_42_155Hz.bin"},
	{"aw8697_clock_channel_RTP_43_155Hz.bin"},
	{"aw8697_long_sound_channel_RTP_44_155Hz.bin"},
	{"aw8697_short_channel_RTP_45_155Hz.bin"},
	{"aw8697_two_error_remaind_RTP_46_155Hz.bin"},

	{"aw8697_kill_program_RTP_47_155Hz.bin"},
	{"aw8697_Simple_channel_RTP_48.bin"},
	{"aw8697_Pure_RTP_49_155Hz.bin"},
	{"aw8697_reserved_sound_channel_RTP_50.bin"},

	{"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

	{"aw8697_old_steady_test_RTP_52.bin"},
	{"aw8697_listen_pop_53_155Hz.bin"},
	{"aw8697_desk_7_RTP_54_155Hz.bin"},
	{"aw8697_nfc_10_RTP_55_155Hz.bin"},
	{"aw8697_vibrator_remain_12_RTP_56_155Hz.bin"},
	{"aw8697_notice_13_RTP_57.bin"},
	{"aw8697_third_ring_14_RTP_58.bin"},
	{"aw8697_reserved_59.bin"},

	{"aw8697_honor_fisrt_kill_RTP_60_155Hz.bin"},
	{"aw8697_honor_two_kill_RTP_61_155Hz.bin"},
	{"aw8697_honor_three_kill_RTP_62_155Hz.bin"},
	{"aw8697_honor_four_kill_RTP_63_155Hz.bin"},
	{"aw8697_honor_five_kill_RTP_64_155Hz.bin"},
	{"aw8697_honor_three_continu_kill_RTP_65_155Hz.bin"},
	{"aw8697_honor_four_continu_kill_RTP_66_155Hz.bin"},
	{"aw8697_honor_unstoppable_RTP_67_155Hz.bin"},
	{"aw8697_honor_thousands_kill_RTP_68_155Hz.bin"},
	{"aw8697_honor_lengendary_RTP_69_155Hz.bin"},


	{"aw8697_Freshmorning_RTP_70_155Hz.bin"},
	{"aw8697_Peaceful_RTP_71_155Hz.bin"},
	{"aw8697_Cicada_RTP_72_155Hz.bin"},
	{"aw8697_Electronica_RTP_73_155Hz.bin"},
	{"aw8697_Holiday_RTP_74_155Hz.bin"},
	{"aw8697_Funk_RTP_75_155Hz.bin"},
	{"aw8697_House_RTP_76_155Hz.bin"},
	{"aw8697_Temple_RTP_77_155Hz.bin"},
	{"aw8697_Dreamyjazz_RTP_78_155Hz.bin"},
	{"aw8697_Modern_RTP_79_155Hz.bin"},

	{"aw8697_Round_RTP_80_155Hz.bin"},
	{"aw8697_Rising_RTP_81_155Hz.bin"},
	{"aw8697_Wood_RTP_82_155Hz.bin"},
	{"aw8697_Heys_RTP_83_155Hz.bin"},
	{"aw8697_Mbira_RTP_84_155Hz.bin"},
	{"aw8697_News_RTP_85_155Hz.bin"},
	{"aw8697_Peak_RTP_86_155Hz.bin"},
	{"aw8697_Crisp_RTP_87_155Hz.bin"},
	{"aw8697_Singingbowls_RTP_88_155Hz.bin"},
	{"aw8697_Bounce_RTP_89_155Hz.bin"},

	{"aw8697_reserved_90.bin"},
	{"aw8697_reserved_91.bin"},
	{"aw8697_reserved_92.bin"},
	{"aw8697_reserved_93.bin"},
	{"aw8697_reserved_94.bin"},
	{"aw8697_reserved_95.bin"},
	{"aw8697_reserved_96.bin"},
	{"aw8697_reserved_97.bin"},
	{"aw8697_reserved_98.bin"},
	{"aw8697_reserved_99.bin"},

	{"aw8697_soldier_first_kill_RTP_100_155Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_155Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_155Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_155Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_155Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105_155Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_155Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_155Hz.bin"},
	{"aw8697_charging_simulation_RTP_108_155Hz.bin"},
	{"aw8697_fingerprint_success_RTP_109_155Hz.bin"},

	{"aw8697_fingerprint_effect1_RTP_110_155Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_155Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_155Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_155Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_155Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_155Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_155Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_155Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_155Hz.bin"},
	{"aw8697_reserved_119.bin"},

	{"aw8697_Miss_RTP_120.bin"},
	{"aw8697_Scenic_RTP_121_155Hz.bin"},
	{"aw8697_voice_assistant_RTP_122_155Hz.bin"},
/* used for 7 */
	{"aw8697_Appear_channel_RTP_123_155Hz.bin"},
	{"aw8697_Miss_RTP_124_155Hz.bin"},
	{"aw8697_Music_channel_RTP_125_155Hz.bin"},
	{"aw8697_Percussion_channel_RTP_126_155Hz.bin"},
	{"aw8697_Ripple_channel_RTP_127_155Hz.bin"},
	{"aw8697_Bright_channel_RTP_128_155Hz.bin"},
	{"aw8697_Fun_channel_RTP_129_155Hz.bin"},
	{"aw8697_Glittering_channel_RTP_130_155Hz.bin"},
	{"aw8697_Harp_channel_RTP_131_155Hz.bin"},
	{"aw8697_Overtone_channel_RTP_132_155Hz.bin"},
	{"aw8697_Simple_channel_RTP_133_155Hz.bin"},

	{"aw8697_Seine_past_RTP_134_155Hz.bin"},
	{"aw8697_Classical_ring_RTP_135_155Hz.bin"},
	{"aw8697_Long_for_RTP_136_155Hz.bin"},
	{"aw8697_Romantic_RTP_137_155Hz.bin"},
	{"aw8697_Bliss_RTP_138_155Hz.bin"},
	{"aw8697_Dream_RTP_139_155Hz.bin"},
	{"aw8697_Relax_RTP_140_155Hz.bin"},
	{"aw8697_Joy_channel_RTP_141_155Hz.bin"},
	{"aw8697_weather_wind_RTP_142_155Hz.bin"},
	{"aw8697_weather_cloudy_RTP_143_155Hz.bin"},
	{"aw8697_weather_thunderstorm_RTP_144_155Hz.bin"},
	{"aw8697_weather_default_RTP_145_155Hz.bin"},
	{"aw8697_weather_sunny_RTP_146_155Hz.bin"},
	{"aw8697_weather_smog_RTP_147_155Hz.bin"},
	{"aw8697_weather_snow_RTP_148_155Hz.bin"},
	{"aw8697_weather_rain_RTP_149_155Hz.bin"},

/* used for 7 end*/
	{"aw8697_rtp_lighthouse.bin"},
	{"aw8697_rtp_silk.bin"},
	{"aw8697_reserved_152.bin"},
	{"aw8697_reserved_153.bin"},
	{"aw8697_reserved_154.bin"},
	{"aw8697_reserved_155.bin"},
	{"aw8697_reserved_156.bin"},
	{"aw8697_reserved_157.bin"},
	{"aw8697_reserved_158.bin"},
	{"aw8697_reserved_159.bin"},
	{"aw8697_reserved_160.bin"},

	{"aw8697_reserved_161.bin"},
	{"aw8697_reserved_162.bin"},
	{"aw8697_reserved_163.bin"},
	{"aw8697_reserved_164.bin"},
	{"aw8697_reserved_165.bin"},
	{"aw8697_reserved_166.bin"},
	{"aw8697_reserved_167.bin"},
	{"aw8697_reserved_168.bin"},
	{"aw8697_reserved_169.bin"},
	{"aw8697_reserved_170.bin"},
	{"aw8697_Threefingers_Long_RTP_171_155Hz.bin"},
	{"aw8697_Threefingers_Up_RTP_172_155Hz.bin"},
	{"aw8697_Threefingers_Screenshot_RTP_173_155Hz.bin"},
	{"aw8697_Unfold_RTP_174_155Hz.bin"},
	{"aw8697_Close_RTP_175_155Hz.bin"},
	{"aw8697_HalfLap_RTP_176_155Hz.bin"},
	{"aw8697_Twofingers_Down_RTP_177_155Hz.bin"},
	{"aw8697_Twofingers_Long_RTP_178_155Hz.bin"},
	{"aw8697_Compatible_1_RTP_179_155Hz.bin"},
	{"aw8697_Compatible_2_RTP_180_155Hz.bin"},
	{"aw8697_Styleswitch_RTP_181_155Hz.bin"},
	{"aw8697_Waterripple_RTP_182_155Hz.bin"},
	{"aw8697_Suspendbutton_Bottomout_RTP_183_155Hz.bin"},
	{"aw8697_Suspendbutton_Menu_RTP_184_155Hz.bin"},
	{"aw8697_Complete_RTP_185_155Hz.bin"},
	{"aw8697_Bulb_RTP_186_155Hz.bin"},
	{"aw8697_Elasticity_RTP_187_155Hz.bin"},
	{"aw8697_Styleswitch_Soft_RTP_188_155Hz.bin"},
	{"aw8697_reserved_189.bin"},
	{"aw8697_reserved_190.bin"},
	{"aw8697_reserved_191.bin"},
	{"aw8697_reserved_192.bin"},
	{"aw8697_reserved_193.bin"},
	{"aw8697_reserved_194.bin"},
	{"aw8697_reserved_195.bin"},
	{"aw8697_reserved_196.bin"},
	{"aw8697_reserved_197.bin"},
	{"aw8697_reserved_198.bin"},
	{"aw8697_reserved_199.bin"},
	{"aw8697_reserved_200.bin"},
};
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
static char aw8697_rtp_name_165Hz[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_rtp.bin"},
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2_165Hz.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9_165Hz.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12_165Hz.bin"},
    {"aw8697_Joy_channel_RTP_13_165Hz.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15_165Hz.bin"},
    {"aw8697_Splash_channel_RTP_16_165Hz.bin"},

    {"aw8697_About_School_RTP_17_165Hz.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19_165Hz.bin"},
    {"aw8697_Commuting_RTP_20_165Hz.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22_165Hz.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24_165Hz.bin"},
    {"aw8697_Lakeside_RTP_25_165Hz.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27_165Hz.bin"},
    {"aw8697_Messy_RTP_28_165Hz.bin"},
    {"aw8697_Night_RTP_29_165Hz.bin"},
    {"aw8697_Passionate_Dance_RTP_30_165Hz.bin"},
    {"aw8697_Playground_RTP_31_165Hz.bin"},
    {"aw8697_Relax_RTP_32_165Hz.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34_165Hz.bin"},
    {"aw8697_Silence_RTP_35_165Hz.bin"},
    {"aw8697_Stars_RTP_36_165Hz.bin"},
    {"aw8697_Summer_RTP_37_165Hz.bin"},
    {"aw8697_Toys_RTP_38_165Hz.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41_165Hz.bin"},
    {"aw8697_cut_channel_RTP_42_165Hz.bin"},
    {"aw8697_clock_channel_RTP_43_165Hz.bin"},
    {"aw8697_long_sound_channel_RTP_44_165Hz.bin"},
    {"aw8697_short_channel_RTP_45_165Hz.bin"},
    {"aw8697_two_error_remaind_RTP_46_165Hz.bin"},

    {"aw8697_kill_program_RTP_47_165Hz.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49_165Hz.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53.bin"},
    {"aw8697_desk_7_RTP_54_165Hz.bin"},
    {"aw8697_nfc_10_RTP_55_165Hz.bin"},
    {"aw8697_vibrator_remain_12_RTP_56_165Hz.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60_165Hz.bin"},
    {"aw8697_honor_two_kill_RTP_61_165Hz.bin"},
    {"aw8697_honor_three_kill_RTP_62_165Hz.bin"},
    {"aw8697_honor_four_kill_RTP_63_165Hz.bin"},
    {"aw8697_honor_five_kill_RTP_64_165Hz.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65_165Hz.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66_165Hz.bin"},
    {"aw8697_honor_unstoppable_RTP_67_165Hz.bin"},
    {"aw8697_honor_thousands_kill_RTP_68_165Hz.bin"},
    {"aw8697_honor_lengendary_RTP_69_165Hz.bin"},
    {"aw8697_Airy_morning_RTP_70_165Hz.bin"},
    {"aw8697_Temple_morning_RTP_71_165Hz.bin"},
    {"aw8697_Water_cicidas_72_RTP_165Hz.bin"},
    {"aw8697_Electro_club_RTP_73_165Hz.bin"},
    {"aw8697_Vacation_RTP_74_165Hz.bin"},
    {"aw8697_Jazz_funk_RTP_75_165Hz.bin"},
    {"aw8697_House_club_RTP_76_165Hz.bin"},
    {"aw8697_temple_tone_RTP_77_165Hz.bin"},
    {"aw8697_Jazz_dreamy_RTP_78_165Hz.bin"},
    {"aw8697_Jazz_modern_RTP_79_165Hz.bin"},
    {"aw8697_Tone_round_RTP_80_165Hz.bin"},
    {"aw8697_Digi_rise_RTP_81_165Hz.bin"},
    {"aw8697_Wood_phone_RTP_82_165Hz.bin"},
    {"aw8697_Hey_RTP_83_165Hz.bin"},
    {"aw8697_Zanza_RTP_84_165Hz.bin"},
    {"aw8697_Info_RTP_85_165Hz.bin"},
    {"aw8697_Tip_top_RTP_86_165Hz.bin"},
    {"aw8697_Opop_short_RTP_87_165Hz.bin"},
    {"aw8697_bowl_bells_RTP_88_165Hz.bin"},
    {"aw8697_jumpy_RTP_89_165Hz.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"aw8697_reserved_94.bin"},
    {"aw8697_reserved_95.bin"},
    {"aw8697_reserved_96.bin"},
    {"aw8697_reserved_97.bin"},
    {"aw8697_reserved_98.bin"},
    {"aw8697_reserved_99.bin"},

	{"aw8697_soldier_first_kill_RTP_100_165Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_165Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_165Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_165Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_165Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107.bin"},
	{"aw8697_charging_simulation_RTP_108.bin"},
	{"aw8697_fingerprint_success_RTP_109.bin"},

	{"aw8697_fingerprint_effect1_RTP_110.bin"},
	{"aw8697_fingerprint_effect2_RTP_111.bin"},
	{"aw8697_fingerprint_effect3_RTP_112.bin"},
	{"aw8697_fingerprint_effect4_RTP_113.bin"},
	{"aw8697_fingerprint_effect5_RTP_114.bin"},
	{"aw8697_fingerprint_effect6_RTP_115.bin"},
	{"aw8697_fingerprint_effect7_RTP_116.bin"},
	{"aw8697_fingerprint_effect8_RTP_117.bin"},
	{"aw8697_breath_simulation_RTP_118.bin"},
	{"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121_165Hz.bin"},
    {"aw8697_voice_assistant_RTP_122.bin"},
/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123_165Hz.bin"},
    {"aw8697_Miss_RTP_oplusos7_124_165Hz.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125_165Hz.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126_165Hz.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127_165Hz.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128_165Hz.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129_165Hz.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130_165Hz.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131_165Hz.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132_165Hz.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133_165Hz.bin"},

	{"aw8697_Seine_past_RTP_oplusos7_134_165Hz.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135_165Hz.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136_165Hz.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137_165Hz.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138_165Hz.bin"},
    {"aw8697_Dream_RTP_oplusos7_139_165Hz.bin"},
    {"aw8697_Relax_RTP_oplusos7_140_165Hz.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141_165Hz.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142_165Hz.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143_165Hz.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144_165Hz.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145_165Hz.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146_165Hz.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147_165Hz.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148_165Hz.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149_165Hz.bin"},

/* used for oplusos 7 end*/
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk.bin"},
    {"aw8697_reserved_152.bin"},
    {"aw8697_reserved_153.bin"},
    {"aw8697_reserved_154.bin"},
    {"aw8697_reserved_155.bin"},
    {"aw8697_reserved_156.bin"},
    {"aw8697_reserved_157.bin"},
    {"aw8697_reserved_158.bin"},
    {"aw8697_reserved_159.bin"},
    {"aw8697_reserved_160.bin"},

	{"aw8697_oplus_its_oplus_RTP_161_165Hz.bin"},
    {"aw8697_oplus_tune_RTP_162_165Hz.bin"},
    {"aw8697_oplus_jingle_RTP_163_165Hz.bin"},
    {"aw8697_reserved_164.bin"},
    {"aw8697_reserved_165.bin"},
    {"aw8697_reserved_166.bin"},
    {"aw8697_reserved_167.bin"},
    {"aw8697_reserved_168.bin"},
    {"aw8697_reserved_169.bin"},
    {"aw8697_oplus_gt_RTP_170_165Hz.bin"},
};
#endif /* OPLUS_FEATURE_CHG_BASIC */

static char aw8697_rtp_name[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_rtp.bin"},
#ifdef OPLUS_FEATURE_CHG_BASIC
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12.bin"},
    {"aw8697_Joy_channel_RTP_13.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15.bin"},
    {"aw8697_Splash_channel_RTP_16.bin"},

    {"aw8697_About_School_RTP_17.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19.bin"},
    {"aw8697_Commuting_RTP_20.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24.bin"},
    {"aw8697_Lakeside_RTP_25.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27.bin"},
    {"aw8697_Messy_RTP_28.bin"},
    {"aw8697_Night_RTP_29.bin"},
    {"aw8697_Passionate_Dance_RTP_30.bin"},
    {"aw8697_Playground_RTP_31.bin"},
    {"aw8697_Relax_RTP_32.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34.bin"},
    {"aw8697_Silence_RTP_35.bin"},
    {"aw8697_Stars_RTP_36.bin"},
    {"aw8697_Summer_RTP_37.bin"},
    {"aw8697_Toys_RTP_38.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41.bin"},
    {"aw8697_cut_channel_RTP_42.bin"},
    {"aw8697_clock_channel_RTP_43.bin"},
    {"aw8697_long_sound_channel_RTP_44.bin"},
    {"aw8697_short_channel_RTP_45.bin"},
    {"aw8697_two_error_remaind_RTP_46.bin"},

    {"aw8697_kill_program_RTP_47.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53.bin"},
    {"aw8697_desk_7_RTP_54.bin"},
    {"aw8697_nfc_10_RTP_55.bin"},
    {"aw8697_vibrator_remain_12_RTP_56.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60.bin"},
    {"aw8697_honor_two_kill_RTP_61.bin"},
    {"aw8697_honor_three_kill_RTP_62.bin"},
    {"aw8697_honor_four_kill_RTP_63.bin"},
    {"aw8697_honor_five_kill_RTP_64.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66.bin"},
    {"aw8697_honor_unstoppable_RTP_67.bin"},
    {"aw8697_honor_thousands_kill_RTP_68.bin"},
    {"aw8697_honor_lengendary_RTP_69.bin"},
    {"aw8697_Airy_morning_RTP_70.bin"},
    {"aw8697_Temple_morning_RTP_71.bin"},
    {"aw8697_Water_cicidas_72_RTP.bin"},
    {"aw8697_Electro_club_RTP_73.bin"},
    {"aw8697_Vacation_RTP_74.bin"},
    {"aw8697_Jazz_funk_RTP_75.bin"},
    {"aw8697_House_club_RTP_76.bin"},
    {"aw8697_temple_tone_RTP_77.bin"},
    {"aw8697_Jazz_dreamy_RTP_78.bin"},
    {"aw8697_Jazz_modern_RTP_79.bin"},
    {"aw8697_Tone_round_RTP_80.bin"},
    {"aw8697_Digi_rise_RTP_81.bin"},
    {"aw8697_Wood_phone_RTP_82.bin"},
    {"aw8697_Hey_RTP_83.bin"},
    {"aw8697_Zanza_RTP_84.bin"},
    {"aw8697_Info_RTP_85.bin"},
    {"aw8697_Tip_top_RTP_86.bin"},
    {"aw8697_Opop_short_RTP_87.bin"},
    {"aw8697_bowl_bells_RTP_88.bin"},
    {"aw8697_jumpy_RTP_89.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"ALCloudscape_170HZ.bin"},
    {"ALGoodenergy_170HZ.bin"},
    {"NTblink_170HZ.bin"},
    {"NTwhoop_170HZ.bin"},
    {"Newfeeling_170HZ.bin"},
    {"nature_170HZ.bin"},

	{"aw8697_soldier_first_kill_RTP_100.bin"},
	{"aw8697_soldier_second_kill_RTP_101.bin"},
	{"aw8697_soldier_third_kill_RTP_102.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104.bin"},
	{"aw8697_stepable_regulate_RTP_105.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107.bin"},
	{"aw8697_charging_simulation_RTP_108.bin"},
	{"aw8697_fingerprint_success_RTP_109.bin"},

	{"aw8697_fingerprint_effect1_RTP_110.bin"},
	{"aw8697_fingerprint_effect2_RTP_111.bin"},
	{"aw8697_fingerprint_effect3_RTP_112.bin"},
	{"aw8697_fingerprint_effect4_RTP_113.bin"},
	{"aw8697_fingerprint_effect5_RTP_114.bin"},
	{"aw8697_fingerprint_effect6_RTP_115.bin"},
	{"aw8697_fingerprint_effect7_RTP_116.bin"},
	{"aw8697_fingerprint_effect8_RTP_117.bin"},
	{"aw8697_breath_simulation_RTP_118.bin"},
	{"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121.bin"},
    {"aw8697_voice_assistant_RTP_122.bin"},
/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123.bin"},
    {"aw8697_Miss_RTP_oplusos7_124.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133.bin"},

	{"aw8697_Seine_past_RTP_oplusos7_134.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138.bin"},
    {"aw8697_Dream_RTP_oplusos7_139.bin"},
    {"aw8697_Relax_RTP_oplusos7_140.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149.bin"},

/* used for oplusos 7 end*/
#endif
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk.bin"},
    {"aw8697_reserved_152.bin"},
    {"aw8697_reserved_153.bin"},
    {"aw8697_reserved_154.bin"},
    {"aw8697_reserved_155.bin"},
    {"aw8697_reserved_156.bin"},
    {"aw8697_reserved_157.bin"},
    {"aw8697_reserved_158.bin"},
    {"aw8697_reserved_159.bin"},
    {"aw8697_reserved_160.bin"},

	{"aw8697_oplus_its_oplus_RTP_161_170Hz.bin"},
    {"aw8697_oplus_tune_RTP_162_170Hz.bin"},
    {"aw8697_oplus_jingle_RTP_163_170Hz.bin"},
    {"aw8697_reserved_164.bin"},
    {"aw8697_reserved_165.bin"},
    {"aw8697_reserved_166.bin"},
    {"ring_McLarn_167.bin"},
    {"notif_Ardour_168.bin"},
    {"notif_Chic_169.bin"},
    {"aw8697_oplus_gt_RTP_170_170Hz.bin"},

	{"ringtone_Alacrity_RTP.bin"},
    {"ring_Amenity_RTP.bin"},
    {"ringtone_Blues_RTP.bin"},
    {"ring_Bounce_RTP.bin"},
    {"ring_Calm_RTP.bin"},
    {"ringtone_Cloud_RTP.bin"},
    {"ringtone_Cyclotron_RTP.bin"},
    {"ringtone_Distinct_RTP.bin"},
    {"ringtone_Dynamic_RTP.bin"},
    {"ringtone_Echo_RTP.bin"},
    {"ringtone_Expect_RTP.bin"},
    {"ringtone_Fanatical_RTP.bin"},
    {"ringtone_Funky_RTP.bin"},
    {"ringtone_Guitar_RTP.bin"},
    {"ringtone_Harping_RTP.bin"},
    {"ringtone_Highlight_RTP.bin"},
    {"ringtone_Idyl_RTP.bin"},
    {"ringtone_Innocence_RTP.bin"},
    {"ringtone_Journey_RTP.bin"},
    {"ringtone_Joyous_RTP.bin"},
    {"ring_Lazy_RTP.bin"},
    {"ringtone_Marimba_RTP.bin"},
    {"ring_Mystical_RTP.bin"},
    {"ringtone_Old_telephone_RTP.bin"},
    {"ringtone_Oneplus_tune_RTP.bin"},
    {"ringtone_Rhythm_RTP.bin"},
    {"ringtone_Optimistic_RTP.bin"},
    {"ringtone_Piano_RTP.bin"},
    {"ring_Whirl_RTP.bin"},
    {"VZW_Alrwave_RTP.bin"},
    {"t-jingle_RTP.bin"},
    {"ringtone_Eager.bin"},
    {"ringtone_Ebullition.bin"},
    {"ringtone_Friendship.bin"},
    {"ringtone_Jazz_life_RTP.bin"},
    {"ringtone_Sun_glittering_RTP.bin"},
    {"notif_Allay_RTP.bin"},
    {"notif_Allusion_RTP.bin"},
    {"notif_Amiable_RTP.bin"},
    {"notif_Blare_RTP.bin"},
    {"notif_Blissful_RTP.bin"},
    {"notif_Brisk_RTP.bin"},
    {"notif_Bubble_RTP.bin"},
    {"notif_Cheerful_RTP.bin"},
    {"notif_Clear_RTP.bin"},
    {"notif_Comely_RTP.bin"},
    {"notif_Cozy_RTP.bin"},
    {"notif_Ding_RTP.bin"},
    {"notif_Effervesce_RTP.bin"},
    {"notif_Elegant_RTP.bin"},
    {"notif_Free_RTP.bin"},
    {"notif_Hallucination_RTP.bin"},
    {"notif_Inbound_RTP.bin"},
    {"notif_Light_RTP.bin"},
    {"notif_Meet_RTP.bin"},
    {"notif_Naivety_RTP.bin"},
    {"notif_Quickly_RTP.bin"},
    {"notif_Rhythm_RTP.bin"},
    {"notif_Surprise_RTP.bin"},
    {"notif_Twinkle_RTP.bin"},
    {"Version_Alert_RTP.bin"},
    {"alarm_Alarm_clock_RTP.bin"},
    {"alarm_Beep_RTP.bin"},
    {"alarm_Breeze_RTP.bin"},
    {"alarm_Dawn_RTP.bin"},
    {"alarm_Dream_RTP.bin"},
    {"alarm_Fluttering_RTP.bin"},
    {"alarm_Flyer_RTP.bin"},
    {"alarm_Interesting_RTP.bin"},
    {"alarm_Leisurely_RTP.bin"},
    {"alarm_Memory_RTP.bin"},
    {"alarm_Relieved_RTP.bin"},
    {"alarm_Ripple_RTP.bin"},
    {"alarm_Slowly_RTP.bin"},
    {"alarm_spring_RTP.bin"},
    {"alarm_Stars_RTP.bin"},
    {"alarm_Surging_RTP.bin"},
    {"alarm_tactfully_RTP.bin"},
    {"alarm_The_wind_RTP.bin"},
    {"alarm_Walking_in_the_rain_RTP.bin"},
    {"BoHaoPanAnJian.bin"},
    {"BoHaoPanAnNiu.bin"},
    {"BoHaoPanKuaiJie.bin"},
    {"DianHuaGuaDuan.bin"},
    {"DianJinMoShiQieHuan.bin"},
    {"HuaDongTiaoZhenDong.bin"},
    {"LeiShen.bin"},
    {"XuanZhongYouXi.bin"},
    {"YeJianMoShiDaZi.bin"},
    {"YouXiSheZhiKuang.bin"},
    {"ZhuanYeMoShi.bin"},
    {"Climber_RTP.bin"},
    {"Chase_RTP.bin"},
    {"shuntai24k_rtp.bin"},
    {"wentai24k_rtp.bin"},
    {"Rock_RTP.bin"},
    {"In_game_ringtone_RTP.bin"},
    {"Wake_up_samurai_RTP.bin"},
    {"In_game_alarm_RTP.bin"},
    {"In_game_sms_RTP.bin"},
    {"Audition_RTP.bin"},
    {"20ms_RTP.bin"},
    {"40ms_RTP.bin"},
    {"60ms_RTP.bin"},
    {"80ms_RTP.bin"},
    {"100ms_RTP.bin"},
    {"120ms_RTP.bin"},
    {"140ms_RTP.bin"},
    {"160ms_RTP.bin"},
    {"180ms_RTP.bin"},
    {"200ms_RTP.bin"},
    {"220ms_RTP.bin"},
    {"240ms_RTP.bin"},
    {"260ms_RTP.bin"},
    {"280ms_RTP.bin"},
    {"300ms_RTP.bin"},
    {"320ms_RTP.bin"},
    {"340ms_RTP.bin"},
    {"360ms_RTP.bin"},
    {"380ms_RTP.bin"},
    {"400ms_RTP.bin"},
    {"420ms_RTP.bin"},
    {"440ms_RTP.bin"},
    {"460ms_RTP.bin"},
    {"480ms_RTP.bin"},
    {"500ms_RTP.bin"},
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static char aw8697_rtp_name_175Hz[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_rtp.bin"},
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2_175Hz.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9_175Hz.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12_175Hz.bin"},
    {"aw8697_Joy_channel_RTP_13_175Hz.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15_175Hz.bin"},
    {"aw8697_Splash_channel_RTP_16_175Hz.bin"},

    {"aw8697_About_School_RTP_17_175Hz.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19_175Hz.bin"},
    {"aw8697_Commuting_RTP_20_175Hz.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22_175Hz.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24_175Hz.bin"},
    {"aw8697_Lakeside_RTP_25_175Hz.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27_175Hz.bin"},
    {"aw8697_Messy_RTP_28_175Hz.bin"},
    {"aw8697_Night_RTP_29_175Hz.bin"},
    {"aw8697_Passionate_Dance_RTP_30_175Hz.bin"},
    {"aw8697_Playground_RTP_31_175Hz.bin"},
    {"aw8697_Relax_RTP_32_175Hz.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34_175Hz.bin"},
    {"aw8697_Silence_RTP_35_175Hz.bin"},
    {"aw8697_Stars_RTP_36_175Hz.bin"},
    {"aw8697_Summer_RTP_37_175Hz.bin"},
    {"aw8697_Toys_RTP_38_175Hz.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41_175Hz.bin"},
    {"aw8697_cut_channel_RTP_42_175Hz.bin"},
    {"aw8697_clock_channel_RTP_43_175Hz.bin"},
    {"aw8697_long_sound_channel_RTP_44_175Hz.bin"},
    {"aw8697_short_channel_RTP_45_175Hz.bin"},
    {"aw8697_two_error_remaind_RTP_46_175Hz.bin"},

    {"aw8697_kill_program_RTP_47_175Hz.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49_175Hz.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53.bin"},
    {"aw8697_desk_7_RTP_54_175Hz.bin"},
    {"aw8697_nfc_10_RTP_55_175Hz.bin"},
    {"aw8697_vibrator_remain_12_RTP_56_175Hz.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60_175Hz.bin"},
    {"aw8697_honor_two_kill_RTP_61_175Hz.bin"},
    {"aw8697_honor_three_kill_RTP_62_175Hz.bin"},
    {"aw8697_honor_four_kill_RTP_63_175Hz.bin"},
    {"aw8697_honor_five_kill_RTP_64_175Hz.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65_175Hz.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66_175Hz.bin"},
    {"aw8697_honor_unstoppable_RTP_67_175Hz.bin"},
    {"aw8697_honor_thousands_kill_RTP_68_175Hz.bin"},
    {"aw8697_honor_lengendary_RTP_69_175Hz.bin"},
    {"aw8697_Airy_morning_RTP_70_175Hz.bin"},
    {"aw8697_Temple_morning_RTP_71_175Hz.bin"},
    {"aw8697_Water_cicidas_72_RTP_175Hz.bin"},
    {"aw8697_Electro_club_RTP_73_175Hz.bin"},
    {"aw8697_Vacation_RTP_74_175Hz.bin"},
    {"aw8697_Jazz_funk_RTP_75_175Hz.bin"},
    {"aw8697_House_club_RTP_76_175Hz.bin"},
    {"aw8697_temple_tone_RTP_77_175Hz.bin"},
    {"aw8697_Jazz_dreamy_RTP_78_175Hz.bin"},
    {"aw8697_Jazz_modern_RTP_79_175Hz.bin"},
    {"aw8697_Tone_round_RTP_80_175Hz.bin"},
    {"aw8697_Digi_rise_RTP_81_175Hz.bin"},
    {"aw8697_Wood_phone_RTP_82_175Hz.bin"},
    {"aw8697_Hey_RTP_83_175Hz.bin"},
    {"aw8697_Zanza_RTP_84_175Hz.bin"},
    {"aw8697_Info_RTP_85_175Hz.bin"},
    {"aw8697_Tip_top_RTP_86_175Hz.bin"},
    {"aw8697_Opop_short_RTP_87_175Hz.bin"},
    {"aw8697_bowl_bells_RTP_88_175Hz.bin"},
    {"aw8697_jumpy_RTP_89_175Hz.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"ALCloudscape_170HZ.bin"},
    {"ALGoodenergy_170HZ.bin"},
    {"NTblink_170HZ.bin"},
    {"NTwhoop_170HZ.bin"},
    {"Newfeeling_170HZ.bin"},
    {"nature_170HZ.bin"},

	{"aw8697_soldier_first_kill_RTP_100_175Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_175Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_175Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_175Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_175Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107.bin"},
	{"aw8697_charging_simulation_RTP_108.bin"},
	{"aw8697_fingerprint_success_RTP_109.bin"},

	{"aw8697_fingerprint_effect1_RTP_110.bin"},
	{"aw8697_fingerprint_effect2_RTP_111.bin"},
	{"aw8697_fingerprint_effect3_RTP_112.bin"},
	{"aw8697_fingerprint_effect4_RTP_113.bin"},
	{"aw8697_fingerprint_effect5_RTP_114.bin"},
	{"aw8697_fingerprint_effect6_RTP_115.bin"},
	{"aw8697_fingerprint_effect7_RTP_116.bin"},
	{"aw8697_fingerprint_effect8_RTP_117.bin"},
	{"aw8697_breath_simulation_RTP_118.bin"},
	{"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121_175Hz.bin"},
    {"aw8697_voice_assistant_RTP_122.bin"},
/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123_175Hz.bin"},
    {"aw8697_Miss_RTP_oplusos7_124_175Hz.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125_175Hz.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126_175Hz.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127_175Hz.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128_175Hz.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129_175Hz.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130_175Hz.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131_175Hz.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132_175Hz.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133_175Hz.bin"},

	{"aw8697_Seine_past_RTP_oplusos7_134_175Hz.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135_175Hz.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136_175Hz.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137_175Hz.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138_175Hz.bin"},
    {"aw8697_Dream_RTP_oplusos7_139_175Hz.bin"},
    {"aw8697_Relax_RTP_oplusos7_140_175Hz.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141_175Hz.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142_175Hz.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143_175Hz.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144_175Hz.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145_175Hz.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146_175Hz.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147_175Hz.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148_175Hz.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149_175Hz.bin"},
/* used for oplusos 7 end*/
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk.bin"},
    {"aw8697_reserved_152.bin"},
    {"aw8697_reserved_153.bin"},
    {"aw8697_reserved_154.bin"},
    {"aw8697_reserved_155.bin"},
    {"aw8697_reserved_156.bin"},
    {"aw8697_reserved_157.bin"},
    {"aw8697_reserved_158.bin"},
    {"aw8697_reserved_159.bin"},
    {"aw8697_reserved_160.bin"},

	{"aw8697_oplus_its_oplus_RTP_161_175Hz.bin"},
    {"aw8697_oplus_tune_RTP_162_175Hz.bin"},
    {"aw8697_oplus_jingle_RTP_163_175Hz.bin"},
    {"aw8697_reserved_164.bin"},
    {"aw8697_reserved_165.bin"},
    {"aw8697_reserved_166.bin"},
    {"aw8697_reserved_167.bin"},
    {"aw8697_reserved_168.bin"},
    {"aw8697_reserved_169.bin"},
    {"aw8697_oplus_gt_RTP_170_175Hz.bin"},

	{"ringtone_Alacrity_RTP.bin"},
    {"ring_Amenity_RTP.bin"},
    {"ringtone_Blues_RTP.bin"},
    {"ring_Bounce_RTP.bin"},
    {"ring_Calm_RTP.bin"},
    {"ringtone_Cloud_RTP.bin"},
    {"ringtone_Cyclotron_RTP.bin"},
    {"ringtone_Distinct_RTP.bin"},
    {"ringtone_Dynamic_RTP.bin"},
    {"ringtone_Echo_RTP.bin"},
    {"ringtone_Expect_RTP.bin"},
    {"ringtone_Fanatical_RTP.bin"},
    {"ringtone_Funky_RTP.bin"},
    {"ringtone_Guitar_RTP.bin"},
    {"ringtone_Harping_RTP.bin"},
    {"ringtone_Highlight_RTP.bin"},
    {"ringtone_Idyl_RTP.bin"},
    {"ringtone_Innocence_RTP.bin"},
    {"ringtone_Journey_RTP.bin"},
    {"ringtone_Joyous_RTP.bin"},
    {"ring_Lazy_RTP.bin"},
    {"ringtone_Marimba_RTP.bin"},
    {"ring_Mystical_RTP.bin"},
    {"ringtone_Old_telephone_RTP.bin"},
    {"ringtone_Oneplus_tune_RTP.bin"},
    {"ringtone_Rhythm_RTP.bin"},
    {"ringtone_Optimistic_RTP.bin"},
    {"ringtone_Piano_RTP.bin"},
    {"ring_Whirl_RTP.bin"},
    {"VZW_Alrwave_RTP.bin"},
    {"t-jingle_RTP.bin"},
    {"ringtone_Eager.bin"},
    {"ringtone_Ebullition.bin"},
    {"ringtone_Friendship.bin"},
    {"ringtone_Jazz_life_RTP.bin"},
    {"ringtone_Sun_glittering_RTP.bin"},
    {"notif_Allay_RTP.bin"},
    {"notif_Allusion_RTP.bin"},
    {"notif_Amiable_RTP.bin"},
    {"notif_Blare_RTP.bin"},
    {"notif_Blissful_RTP.bin"},
    {"notif_Brisk_RTP.bin"},
    {"notif_Bubble_RTP.bin"},
    {"notif_Cheerful_RTP.bin"},
    {"notif_Clear_RTP.bin"},
    {"notif_Comely_RTP.bin"},
    {"notif_Cozy_RTP.bin"},
    {"notif_Ding_RTP.bin"},
    {"notif_Effervesce_RTP.bin"},
    {"notif_Elegant_RTP.bin"},
    {"notif_Free_RTP.bin"},
    {"notif_Hallucination_RTP.bin"},
    {"notif_Inbound_RTP.bin"},
    {"notif_Light_RTP.bin"},
    {"notif_Meet_RTP.bin"},
    {"notif_Naivety_RTP.bin"},
    {"notif_Quickly_RTP.bin"},
    {"notif_Rhythm_RTP.bin"},
    {"notif_Surprise_RTP.bin"},
    {"notif_Twinkle_RTP.bin"},
    {"Version_Alert_RTP.bin"},
    {"alarm_Alarm_clock_RTP.bin"},
    {"alarm_Beep_RTP.bin"},
    {"alarm_Breeze_RTP.bin"},
    {"alarm_Dawn_RTP.bin"},
    {"alarm_Dream_RTP.bin"},
    {"alarm_Fluttering_RTP.bin"},
    {"alarm_Flyer_RTP.bin"},
    {"alarm_Interesting_RTP.bin"},
    {"alarm_Leisurely_RTP.bin"},
    {"alarm_Memory_RTP.bin"},
    {"alarm_Relieved_RTP.bin"},
    {"alarm_Ripple_RTP.bin"},
    {"alarm_Slowly_RTP.bin"},
    {"alarm_spring_RTP.bin"},
    {"alarm_Stars_RTP.bin"},
    {"alarm_Surging_RTP.bin"},
    {"alarm_tactfully_RTP.bin"},
    {"alarm_The_wind_RTP.bin"},
    {"alarm_Walking_in_the_rain_RTP.bin"},
    {"BoHaoPanAnJian.bin"},
    {"BoHaoPanAnNiu.bin"},
    {"BoHaoPanKuaiJie.bin"},
    {"DianHuaGuaDuan.bin"},
    {"DianJinMoShiQieHuan.bin"},
    {"HuaDongTiaoZhenDong.bin"},
    {"LeiShen.bin"},
    {"XuanZhongYouXi.bin"},
    {"YeJianMoShiDaZi.bin"},
    {"YouXiSheZhiKuang.bin"},
    {"ZhuanYeMoShi.bin"},
    {"Climber_RTP.bin"},
    {"Chase_RTP.bin"},
    {"shuntai24k_rtp.bin"},
    {"wentai24k_rtp.bin"},
};
#endif /* OPLUS_FEATURE_CHG_BASIC */

#if defined(CONFIG_OPLUS_HAPTIC_OOS) && defined(CONFIG_ARCH_LITO)
static char aw8697_ram_name_0832[5][30] ={
	{"aw8697_haptic_166.bin"},
    {"aw8697_haptic_168.bin"},
    {"aw8697_haptic_170.bin"},
    {"aw8697_haptic_172.bin"},
    {"aw8697_haptic_174.bin"},
};
#else
static char aw8697_ram_name_0832[5][30] ={
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
    {"aw8697_haptic_235.bin"},
};
#endif

static char aw8697_ram_name_19161[5][30] ={
    {"aw8697_haptic_235_19161.bin"},
    {"aw8697_haptic_235_19161.bin"},
    {"aw8697_haptic_235_19161.bin"},
    {"aw8697_haptic_235_19161.bin"},
    {"aw8697_haptic_235_19161.bin"},
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static char aw8697_rtp_name_0832_226Hz[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_rtp.bin"},
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2_226Hz.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9_226Hz.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12_226Hz.bin"},
    {"aw8697_Joy_channel_RTP_13_226Hz.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15_226Hz.bin"},
    {"aw8697_Splash_channel_RTP_16_226Hz.bin"},

    {"aw8697_About_School_RTP_17_226Hz.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19_226Hz.bin"},
    {"aw8697_Commuting_RTP_20_226Hz.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22_226Hz.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24_226Hz.bin"},
    {"aw8697_Lakeside_RTP_25_226Hz.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27_226Hz.bin"},
    {"aw8697_Messy_RTP_28_226Hz.bin"},
    {"aw8697_Night_RTP_29_226Hz.bin"},
    {"aw8697_Passionate_Dance_RTP_30_226Hz.bin"},
    {"aw8697_Playground_RTP_31_226Hz.bin"},
    {"aw8697_Relax_RTP_32_226Hz.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34_226Hz.bin"},
    {"aw8697_Silence_RTP_35_226Hz.bin"},
    {"aw8697_Stars_RTP_36_226Hz.bin"},
    {"aw8697_Summer_RTP_37_226Hz.bin"},
    {"aw8697_Toys_RTP_38_226Hz.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41_226Hz.bin"},
    {"aw8697_cut_channel_RTP_42_226Hz.bin"},
    {"aw8697_clock_channel_RTP_43_226Hz.bin"},
    {"aw8697_long_sound_channel_RTP_44_226Hz.bin"},
    {"aw8697_short_channel_RTP_45_226Hz.bin"},
    {"aw8697_two_error_remaind_RTP_46_226Hz.bin"},

    {"aw8697_kill_program_RTP_47_226Hz.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49_226Hz.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53_235Hz.bin"},
    {"aw8697_desk_7_RTP_54_226Hz.bin"},
    {"aw8697_nfc_10_RTP_55_226Hz.bin"},
    {"aw8697_vibrator_remain_12_RTP_56_226Hz.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60_226Hz.bin"},
    {"aw8697_honor_two_kill_RTP_61_226Hz.bin"},
    {"aw8697_honor_three_kill_RTP_62_226Hz.bin"},
    {"aw8697_honor_four_kill_RTP_63_226Hz.bin"},
    {"aw8697_honor_five_kill_RTP_64_226Hz.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65_226Hz.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66_226Hz.bin"},
    {"aw8697_honor_unstoppable_RTP_67_226Hz.bin"},
    {"aw8697_honor_thousands_kill_RTP_68_226Hz.bin"},
    {"aw8697_honor_lengendary_RTP_69_226Hz.bin"},
    {"aw8697_Airy_morning_RTP_70_226Hz.bin"},
    {"aw8697_Temple_morning_RTP_71_226Hz.bin"},
    {"aw8697_Water_cicidas_72_RTP_226Hz.bin"},
    {"aw8697_Electro_club_RTP_73_226Hz.bin"},
    {"aw8697_Vacation_RTP_74_226Hz.bin"},
    {"aw8697_Jazz_funk_RTP_75_226Hz.bin"},
    {"aw8697_House_club_RTP_76_226Hz.bin"},
    {"aw8697_temple_tone_RTP_77_226Hz.bin"},
    {"aw8697_Jazz_dreamy_RTP_78_226Hz.bin"},
    {"aw8697_Jazz_modern_RTP_79_226Hz.bin"},
    {"aw8697_Tone_round_RTP_80_226Hz.bin"},
    {"aw8697_Digi_rise_RTP_81_226Hz.bin"},
    {"aw8697_Wood_phone_RTP_82_226Hz.bin"},
    {"aw8697_Hey_RTP_83_226Hz.bin"},
    {"aw8697_Zanza_RTP_84_226Hz.bin"},
    {"aw8697_Info_RTP_85_226Hz.bin"},
    {"aw8697_Tip_top_RTP_86_226Hz.bin"},
    {"aw8697_Opop_short_RTP_87_226Hz.bin"},
    {"aw8697_bowl_bells_RTP_88_226Hz.bin"},
    {"aw8697_jumpy_RTP_89_226Hz.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"aw8697_reserved_94.bin"},
    {"aw8697_reserved_95.bin"},
    {"aw8697_reserved_96.bin"},
    {"aw8697_reserved_97.bin"},
    {"aw8697_reserved_98.bin"},
    {"aw8697_reserved_99.bin"},

	{"aw8697_soldier_first_kill_RTP_100_226Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_226Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_226Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_226Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_226Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105_226Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_226Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_226Hz.bin"},
	{"aw8697_reserved_108.bin"},
	{"aw8697_reserved_109.bin"},

	{"aw8697_fingerprint_effect1_RTP_110_226Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_226Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_226Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_226Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_226Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_226Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_226Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_226Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_226Hz.bin"},
	{"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121_226Hz.bin"},
    {"aw8697_reserved_122.bin"},
/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123_226Hz.bin"},
    {"aw8697_Miss_RTP_oplusos7_124_226Hz.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125_226Hz.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126_226Hz.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127_226Hz.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128_226Hz.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129_226Hz.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130_226Hz.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131_226Hz.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132_226Hz.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133_226Hz.bin"},

	{"aw8697_Seine_past_RTP_oplusos7_134_226Hz.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135_226Hz.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136_226Hz.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137_226Hz.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138_226Hz.bin"},
    {"aw8697_Dream_RTP_oplusos7_139_226Hz.bin"},
    {"aw8697_Relax_RTP_oplusos7_140_226Hz.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141_226Hz.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142_226Hz.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143_226Hz.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144_226Hz.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145_226Hz.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146_226Hz.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147_226Hz.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148_226Hz.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149_226Hz.bin"},
    {"aw8697_Artist_Notification_RTP_150_226Hz.bin"},
    {"aw8697_Artist_Ringtong_RTP_151_226Hz.bin"},
    {"aw8697_Artist_Text_RTP_152_226Hz.bin"},
    {"aw8697_Artist_Alarm_RTP_153_226Hz.bin"},
/* used for oplusos 7 end*/
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk_19081.bin"},
    {"aw8697_reserved_156.bin"},
    {"aw8697_reserved_157.bin"},
    {"aw8697_reserved_158.bin"},
    {"aw8697_reserved_159.bin"},
    {"aw8697_reserved_160.bin"},

	{"aw8697_oplus_its_oplus_RTP_161_226Hz.bin"},
    {"aw8697_oplus_tune_RTP_162_226Hz.bin"},
    {"aw8697_oplus_jingle_RTP_163_226Hz.bin"},
    {"aw8697_reserved_164.bin"},
    {"aw8697_reserved_165.bin"},
    {"aw8697_reserved_166.bin"},
    {"aw8697_reserved_167.bin"},
    {"aw8697_reserved_168.bin"},
    {"aw8697_reserved_169.bin"},
    {"aw8697_reserved_170.bin"},
};
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
static char aw8697_rtp_name_0832_230Hz[][AW8697_RTP_NAME_MAX] = {
    {"aw8697_rtp.bin"},
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2_230Hz.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9_230Hz.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12_230Hz.bin"},
    {"aw8697_Joy_channel_RTP_13_230Hz.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15_230Hz.bin"},
    {"aw8697_Splash_channel_RTP_16_230Hz.bin"},

    {"aw8697_About_School_RTP_17_230Hz.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19_230Hz.bin"},
    {"aw8697_Commuting_RTP_20_230Hz.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22_230Hz.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24_230Hz.bin"},
    {"aw8697_Lakeside_RTP_25_230Hz.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27_230Hz.bin"},
    {"aw8697_Messy_RTP_28_230Hz.bin"},
    {"aw8697_Night_RTP_29_230Hz.bin"},
    {"aw8697_Passionate_Dance_RTP_30_230Hz.bin"},
    {"aw8697_Playground_RTP_31_230Hz.bin"},
    {"aw8697_Relax_RTP_32_230Hz.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34_230Hz.bin"},
    {"aw8697_Silence_RTP_35_230Hz.bin"},
    {"aw8697_Stars_RTP_36_230Hz.bin"},
    {"aw8697_Summer_RTP_37_230Hz.bin"},
    {"aw8697_Toys_RTP_38_230Hz.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41_230Hz.bin"},
    {"aw8697_cut_channel_RTP_42_230Hz.bin"},
    {"aw8697_clock_channel_RTP_43_230Hz.bin"},
    {"aw8697_long_sound_channel_RTP_44_230Hz.bin"},
    {"aw8697_short_channel_RTP_45_230Hz.bin"},
    {"aw8697_two_error_remaind_RTP_46_230Hz.bin"},

    {"aw8697_kill_program_RTP_47_230Hz.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49_230Hz.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53_235Hz.bin"},
    {"aw8697_desk_7_RTP_54_230Hz.bin"},
    {"aw8697_nfc_10_RTP_55_230Hz.bin"},
    {"aw8697_vibrator_remain_12_RTP_56_230Hz.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60_230Hz.bin"},
    {"aw8697_honor_two_kill_RTP_61_230Hz.bin"},
    {"aw8697_honor_three_kill_RTP_62_230Hz.bin"},
    {"aw8697_honor_four_kill_RTP_63_230Hz.bin"},
    {"aw8697_honor_five_kill_RTP_64_230Hz.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65_230Hz.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66_230Hz.bin"},
    {"aw8697_honor_unstoppable_RTP_67_230Hz.bin"},
    {"aw8697_honor_thousands_kill_RTP_68_230Hz.bin"},
    {"aw8697_honor_lengendary_RTP_69_230Hz.bin"},
    {"aw8697_Airy_morning_RTP_70_230Hz.bin"},
    {"aw8697_Temple_morning_RTP_71_230Hz.bin"},
    {"aw8697_Water_cicidas_72_RTP_230Hz.bin"},
    {"aw8697_Electro_club_RTP_73_230Hz.bin"},
    {"aw8697_Vacation_RTP_74_230Hz.bin"},
    {"aw8697_Jazz_funk_RTP_75_230Hz.bin"},
    {"aw8697_House_club_RTP_76_230Hz.bin"},
    {"aw8697_temple_tone_RTP_77_230Hz.bin"},
    {"aw8697_Jazz_dreamy_RTP_78_230Hz.bin"},
    {"aw8697_Jazz_modern_RTP_79_230Hz.bin"},
    {"aw8697_Tone_round_RTP_80_230Hz.bin"},
    {"aw8697_Digi_rise_RTP_81_230Hz.bin"},
    {"aw8697_Wood_phone_RTP_82_230Hz.bin"},
    {"aw8697_Hey_RTP_83_230Hz.bin"},
    {"aw8697_Zanza_RTP_84_230Hz.bin"},
    {"aw8697_Info_RTP_85_230Hz.bin"},
    {"aw8697_Tip_top_RTP_86_230Hz.bin"},
    {"aw8697_Opop_short_RTP_87_230Hz.bin"},
    {"aw8697_bowl_bells_RTP_88_230Hz.bin"},
    {"aw8697_jumpy_RTP_89_230Hz.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"aw8697_reserved_94.bin"},
    {"aw8697_reserved_95.bin"},
    {"aw8697_reserved_96.bin"},
    {"aw8697_reserved_97.bin"},
    {"aw8697_reserved_98.bin"},
    {"aw8697_reserved_99.bin"},

	{"aw8697_soldier_first_kill_RTP_100_230Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_230Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_230Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_230Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_230Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105_230Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_230Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_230Hz.bin"},
	{"aw8697_reserved_108.bin"},
	{"aw8697_reserved_109.bin"},

	{"aw8697_fingerprint_effect1_RTP_110_230Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_230Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_230Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_230Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_230Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_230Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_230Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_230Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_230Hz.bin"},
	{"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121_230Hz.bin"},
    {"aw8697_reserved_122.bin"},
/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123_230Hz.bin"},
    {"aw8697_Miss_RTP_oplusos7_124_230Hz.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125_230Hz.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126_230Hz.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127_230Hz.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128_230Hz.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129_230Hz.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130_230Hz.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131_230Hz.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132_230Hz.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133_230Hz.bin"},

	{"aw8697_Seine_past_RTP_oplusos7_134_230Hz.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135_230Hz.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136_230Hz.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137_230Hz.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138_230Hz.bin"},
    {"aw8697_Dream_RTP_oplusos7_139_230Hz.bin"},
    {"aw8697_Relax_RTP_oplusos7_140_230Hz.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141_230Hz.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142_230Hz.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143_230Hz.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144_230Hz.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145_230Hz.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146_230Hz.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147_230Hz.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148_230Hz.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149_230Hz.bin"},
    {"aw8697_Artist_Notification_RTP_150_230Hz.bin"},
    {"aw8697_Artist_Ringtong_RTP_151_230Hz.bin"},
    {"aw8697_Artist_Text_RTP_152_230Hz.bin"},
    {"aw8697_Artist_Alarm_RTP_153_230Hz.bin"},
/* used for oplusos 7 end*/
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk_19081.bin"},
    {"aw8697_reserved_156.bin"},
    {"aw8697_reserved_157.bin"},
    {"aw8697_reserved_158.bin"},
    {"aw8697_reserved_159.bin"},
    {"aw8697_reserved_160.bin"},

	{"aw8697_oplus_its_oplus_RTP_161_230Hz.bin"},
    {"aw8697_oplus_tune_RTP_162_230Hz.bin"},
    {"aw8697_oplus_jingle_RTP_163_230Hz.bin"},
    {"aw8697_reserved_164.bin"},
    {"aw8697_reserved_165.bin"},
    {"aw8697_reserved_166.bin"},
    {"aw8697_reserved_167.bin"},
    {"aw8697_reserved_168.bin"},
    {"aw8697_reserved_169.bin"},
    {"aw8697_reserved_170.bin"},
};
#endif /* OPLUS_FEATURE_CHG_BASIC */

static char aw8697_rtp_name_0832_234Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
#ifdef OPLUS_FEATURE_CHG_BASIC
	{"aw8697_Hearty_channel_RTP_1.bin"},
	{"aw8697_Instant_channel_RTP_2_234Hz.bin"},
	{"aw8697_Music_channel_RTP_3.bin"},
	{"aw8697_Percussion_channel_RTP_4.bin"},
	{"aw8697_Ripple_channel_RTP_5.bin"},
	{"aw8697_Bright_channel_RTP_6.bin"},
	{"aw8697_Fun_channel_RTP_7.bin"},
	{"aw8697_Glittering_channel_RTP_8.bin"},
	{"aw8697_Granules_channel_RTP_9_234Hz.bin"},
	{"aw8697_Harp_channel_RTP_10.bin"},
	{"aw8697_Impression_channel_RTP_11.bin"},
	{"aw8697_Ingenious_channel_RTP_12_234Hz.bin"},
	{"aw8697_Joy_channel_RTP_13_234Hz.bin"},
	{"aw8697_Overtone_channel_RTP_14.bin"},
	{"aw8697_Receive_channel_RTP_15_234Hz.bin"},
	{"aw8697_Splash_channel_RTP_16_234Hz.bin"},

	{"aw8697_About_School_RTP_17_234Hz.bin"},
	{"aw8697_Bliss_RTP_18.bin"},
	{"aw8697_Childhood_RTP_19_234Hz.bin"},
	{"aw8697_Commuting_RTP_20_234Hz.bin"},
	{"aw8697_Dream_RTP_21.bin"},
	{"aw8697_Firefly_RTP_22_234Hz.bin"},
	{"aw8697_Gathering_RTP_23.bin"},
	{"aw8697_Gaze_RTP_24_234Hz.bin"},
	{"aw8697_Lakeside_RTP_25_234Hz.bin"},
	{"aw8697_Lifestyle_RTP_26.bin"},
	{"aw8697_Memories_RTP_27_234Hz.bin"},
	{"aw8697_Messy_RTP_28_234Hz.bin"},
	{"aw8697_Night_RTP_29_234Hz.bin"},
	{"aw8697_Passionate_Dance_RTP_30_234Hz.bin"},
	{"aw8697_Playground_RTP_31_234Hz.bin"},
	{"aw8697_Relax_RTP_32_234Hz.bin"},
	{"aw8697_Reminiscence_RTP_33.bin"},
	{"aw8697_Silence_From_Afar_RTP_34_234Hz.bin"},
	{"aw8697_Silence_RTP_35_234Hz.bin"},
	{"aw8697_Stars_RTP_36_234Hz.bin"},
	{"aw8697_Summer_RTP_37_234Hz.bin"},
	{"aw8697_Toys_RTP_38_234Hz.bin"},
	{"aw8697_Travel_RTP_39.bin"},
	{"aw8697_Vision_RTP_40.bin"},

	{"aw8697_waltz_channel_RTP_41_234Hz.bin"},
	{"aw8697_cut_channel_RTP_42_234Hz.bin"},
	{"aw8697_clock_channel_RTP_43_234Hz.bin"},
	{"aw8697_long_sound_channel_RTP_44_234Hz.bin"},
	{"aw8697_short_channel_RTP_45_234Hz.bin"},
	{"aw8697_two_error_remaind_RTP_46_234Hz.bin"},

	{"aw8697_kill_program_RTP_47_234Hz.bin"},
	{"aw8697_Simple_channel_RTP_48.bin"},
	{"aw8697_Pure_RTP_49_234Hz.bin"},
	{"aw8697_reserved_sound_channel_RTP_50.bin"},

	{"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

	{"aw8697_old_steady_test_RTP_52.bin"},
	{"aw8697_listen_pop_53_235Hz.bin"},
	{"aw8697_desk_7_RTP_54_234Hz.bin"},
	{"aw8697_nfc_10_RTP_55_234Hz.bin"},
	{"aw8697_vibrator_remain_12_RTP_56_234Hz.bin"},
	{"aw8697_notice_13_RTP_57.bin"},
	{"aw8697_third_ring_14_RTP_58.bin"},
	{"aw8697_reserved_59.bin"},

	{"aw8697_honor_fisrt_kill_RTP_60_234Hz.bin"},
	{"aw8697_honor_two_kill_RTP_61_234Hz.bin"},
	{"aw8697_honor_three_kill_RTP_62_234Hz.bin"},
	{"aw8697_honor_four_kill_RTP_63_234Hz.bin"},
	{"aw8697_honor_five_kill_RTP_64_234Hz.bin"},
	{"aw8697_honor_three_continu_kill_RTP_65_234Hz.bin"},
	{"aw8697_honor_four_continu_kill_RTP_66_234Hz.bin"},
	{"aw8697_honor_unstoppable_RTP_67_234Hz.bin"},
	{"aw8697_honor_thousands_kill_RTP_68_234Hz.bin"},
	{"aw8697_honor_lengendary_RTP_69_234Hz.bin"},
	{"aw8697_Airy_morning_RTP_70_234Hz.bin"},
	{"aw8697_Temple_morning_RTP_71_234Hz.bin"},
	{"aw8697_Water_cicidas_72_RTP_234Hz.bin"},
	{"aw8697_Electro_club_RTP_73_234Hz.bin"},
	{"aw8697_Vacation_RTP_74_234Hz.bin"},
	{"aw8697_Jazz_funk_RTP_75_234Hz.bin"},
	{"aw8697_House_club_RTP_76_234Hz.bin"},
	{"aw8697_temple_tone_RTP_77_234Hz.bin"},
	{"aw8697_Jazz_dreamy_RTP_78_234Hz.bin"},
	{"aw8697_Jazz_modern_RTP_79_234Hz.bin"},
	{"aw8697_Tone_round_RTP_80_234Hz.bin"},
	{"aw8697_Digi_rise_RTP_81_234Hz.bin"},
	{"aw8697_Wood_phone_RTP_82_234Hz.bin"},
	{"aw8697_Hey_RTP_83_234Hz.bin"},
	{"aw8697_Zanza_RTP_84_234Hz.bin"},
	{"aw8697_Info_RTP_85_234Hz.bin"},
	{"aw8697_Tip_top_RTP_86_234Hz.bin"},
	{"aw8697_Opop_short_RTP_87_234Hz.bin"},
	{"aw8697_bowl_bells_RTP_88_234Hz.bin"},
	{"aw8697_jumpy_RTP_89_234Hz.bin"},

	{"aw8697_reserved_90.bin"},
	{"aw8697_reserved_91.bin"},
	{"aw8697_reserved_92.bin"},
	{"aw8697_reserved_93.bin"},
	/* Added by maruthanad.hari@bsp, oneplus tones start */
	{"aw8697_ALCloudscape_94_RTP_235Hz.bin"},
	{"aw8697_ALGoodenergy_95_RTP_235Hz.bin"},
	{"aw8697_NTblink_96_RTP_235Hz.bin"},
	{"aw8697_NTwhoop_97_RTP_235Hz.bin"},
	{"aw8697_Newfeeling_98_RTP_235Hz.bin"},
	{"aw8697_nature_99_RTP_235Hz.bin"},
	/* Added by maruthanad.hari@bsp, oneplus tones stop */
	{"aw8697_soldier_first_kill_RTP_100_234Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_234Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_234Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_234Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_234Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105_234Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_234Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_234Hz.bin"},
	{"aw8697_reserved_108.bin"},
	{"aw8697_reserved_109.bin"},

	{"aw8697_fingerprint_effect1_RTP_110_234Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_234Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_234Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_234Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_234Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_234Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_234Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_234Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_234Hz.bin"},
	{"aw8697_reserved_119.bin"},

	{"aw8697_Miss_RTP_120.bin"},
	{"aw8697_Scenic_RTP_121_234Hz.bin"},
	{"aw8697_reserved_122.bin"},
	/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123_234Hz.bin"},
	{"aw8697_Miss_RTP_oplusos7_124_234Hz.bin"},
	{"aw8697_Music_channel_RTP_oplusos7_125_234Hz.bin"},
	{"aw8697_Percussion_channel_RTP_oplusos7_126_234Hz.bin"},
	{"aw8697_Ripple_channel_RTP_oplusos7_127_234Hz.bin"},
	{"aw8697_Bright_channel_RTP_oplusos7_128_234Hz.bin"},
	{"aw8697_Fun_channel_RTP_oplusos7_129_234Hz.bin"},
	{"aw8697_Glittering_channel_RTP_oplusos7_130_234Hz.bin"},
	{"aw8697_Harp_channel_RTP_oplusos7_131_234Hz.bin"},
	{"aw8697_Overtone_channel_RTP_oplusos7_132_234Hz.bin"},
	{"aw8697_Simple_channel_RTP_oplusos7_133_234Hz.bin"},

	{"aw8697_Seine_past_RTP_oplusos7_134_234Hz.bin"},
	{"aw8697_Classical_ring_RTP_oplusos7_135_234Hz.bin"},
	{"aw8697_Long_for_RTP_oplusos7_136_234Hz.bin"},
	{"aw8697_Romantic_RTP_oplusos7_137_234Hz.bin"},
	{"aw8697_Bliss_RTP_oplusos7_138_234Hz.bin"},
	{"aw8697_Dream_RTP_oplusos7_139_234Hz.bin"},
	{"aw8697_Relax_RTP_oplusos7_140_234Hz.bin"},
	{"aw8697_Joy_channel_RTP_oplusos7_141_234Hz.bin"},
	{"aw8697_weather_wind_RTP_oplusos7_142_234Hz.bin"},
	{"aw8697_weather_cloudy_RTP_oplusos7_143_234Hz.bin"},
	{"aw8697_weather_thunderstorm_RTP_oplusos7_144_234Hz.bin"},
	{"aw8697_weather_default_RTP_oplusos7_145_234Hz.bin"},
	{"aw8697_weather_sunny_RTP_oplusos7_146_234Hz.bin"},
	{"aw8697_weather_smog_RTP_oplusos7_147_234Hz.bin"},
	{"aw8697_weather_snow_RTP_oplusos7_148_234Hz.bin"},
	{"aw8697_weather_rain_RTP_oplusos7_149_234Hz.bin"},
	{"aw8697_Artist_Notification_RTP_150_234Hz.bin"},
	{"aw8697_Artist_Ringtong_RTP_151_234Hz.bin"},
	{"aw8697_Artist_Text_RTP_152_234Hz.bin"},
	{"aw8697_Artist_Alarm_RTP_153_234Hz.bin"},
#endif
	{"aw8697_rtp_lighthouse.bin"},
	{"aw8697_rtp_silk_19081.bin"},
	{"aw8697_reserved_156.bin"},
	{"aw8697_reserved_157.bin"},
	{"aw8697_reserved_158.bin"},
	{"aw8697_reserved_159.bin"},
	{"aw8697_reserved_160.bin"},

	{"aw8697_oplus_its_oplus_RTP_161_234Hz.bin"},
	{"aw8697_oplus_tune_RTP_162_234Hz.bin"},
	{"aw8697_oplus_jingle_RTP_163_234Hz.bin"},
	{"aw8697_reserved_164.bin"},
	{"aw8697_reserved_165.bin"},
	{"aw8697_reserved_166.bin"},
	{"aw8697_reserved_167.bin"},
	{"aw8697_reserved_168.bin"},
	{"aw8697_reserved_169.bin"},
	{"aw8697_reserved_170.bin"},

	/* Added by maruthanad.hari@bsp, oneplus ringtone start */
	{"aw8697_aw8697_ringtone_Alacrity_RTP_171_234Hz.bin"},
	{"aw8697_ring_Amenity_RTP_172_234Hz.bin"},
	{"aw8697_ringtone_Blues_RTP_173_234Hz.bin"},
	{"aw8697_ring_Bounce_RTP_174_234Hz.bin"},
	{"aw8697_ring_Calm_RTP_175_234Hz.bin"},
	{"aw8697_ringtone_Cloud_RTP_176_234Hz.bin"},
	{"aw8697_ringtone_Cyclotron_RTP_177_234Hz.bin"},
	{"aw8697_ringtone_Distinct_RTP_178_234Hz.bin"},
	{"aw8697_ringtone_Dynamic_RTP_179_234Hz.bin"},
	{"aw8697_ringtone_Echo_RTP_180_234Hz.bin"},
	{"aw8697_ringtone_Expect_RTP_181_234Hz.bin"},
	{"aw8697_ringtone_Fanatical_RTP_182_234Hz.bin"},
	{"aw8697_ringtone_Funky_RTP_183_234Hz.bin"},
	{"aw8697_ringtone_Guitar_RTP_184_234Hz.bin"},
	{"aw8697_ringtone_Harping_RTP_185_234Hz.bin"},
	{"aw8697_ringtone_Highlight_RTP_186_234Hz.bin"},
	{"aw8697_ringtone_Idyl_RTP_187_234Hz.bin"},
	{"aw8697_ringtone_Innocence_RTP_188_234Hz.bin"},
	{"aw8697_ringtone_Journey_RTP_189_234Hz.bin"},
	{"aw8697_ringtone_Joyous_RTP_190_234Hz.bin"},
	{"aw8697_ring_Lazy_RTP_191_234Hz.bin"},
	{"aw8697_ringtone_Marimba_RTP_192_234Hz.bin"},
	{"aw8697_ring_Mystical_RTP_193_234Hz.bin"},
	{"aw8697_ringtone_Old_telephone_RTP_194_234Hz.bin"},
	{"aw8697_ringtone_Oneplus_tune_RTP_195_234Hz.bin"},
	{"aw8697_ringtone_Rhythm_RTP_196_234Hz.bin"},
	{"aw8697_ringtone_Optimistic_RTP_197_234Hz.bin"},
	{"aw8697_ringtone_Piano_RTP_198_234Hz.bin"},
	{"aw8697_ring_Whirl_RTP_199_234Hz.bin"},
	{"aw8697_VZW_Alrwave_RTP_200_234Hz.bin"},
	{"aw8697_t-jingle_RTP_201_234Hz.bin"},
	{"aw8697_ringtone_Eager_RTP_202_234Hz.bin"},
	{"aw8697_ringtone_Ebullition_RTP_203_234Hz.bin"},
	{"aw8697_ringtone_Friendship_RTP_204_234Hz.bin"},
	{"aw8697_ringtone_Jazz_life_RTP_205_234Hz.bin"},
	{"aw8697_ringtone_Sun_glittering_RTP_206_234Hz.bin"},
	{"aw8697_notif_Allay_RTP_207_234Hz.bin"},
	{"aw8697_notif_Allusion_RTP_208_234Hz.bin"},
	{"aw8697_notif_Amiable_RTP_209_234Hz.bin"},
	{"aw8697_notif_Blare_RTP_210_234Hz.bin"},
	{"aw8697_notif_Blissful_RTP_211_234Hz.bin"},
	{"aw8697_notif_Brisk_RTP_212_234Hz.bin"},
	{"aw8697_notif_Bubble_RTP_213_234Hz.bin"},
	{"aw8697_notif_Cheerful_RTP_214_234Hz.bin"},
	{"aw8697_notif_Clear_RTP_215_234Hz.bin"},
	{"aw8697_notif_Comely_RTP_216_234Hz.bin"},
	{"aw8697_notif_Cozy_RTP_217_234Hz.bin"},
	{"aw8697_notif_Ding_RTP_218_234Hz.bin"},
	{"aw8697_notif_Effervesce_RTP_219_234Hz.bin"},
	{"aw8697_notif_Elegant_RTP_220_234Hz.bin"},
	{"aw8697_notif_Free_RTP_221_234Hz.bin"},
	{"aw8697_notif_Hallucination_RTP_222_234Hz.bin"},
	{"aw8697_notif_Inbound_RTP_223_234Hz.bin"},
	{"aw8697_notif_Light_RTP_224_234Hz.bin"},
	{"aw8697_notif_Meet_RTP_225_234Hz.bin"},
	{"aw8697_notif_Naivety_RTP_226_234Hz.bin"},
	{"aw8697_notif_Quickly_RTP_227_234Hz.bin"},
	{"aw8697_notif_Rhythm_RTP_228_234Hz.bin"},
	{"aw8697_notif_Surprise_RTP_229_234Hz.bin"},
	{"aw8697_notif_Twinkle_RTP_230_234Hz.bin"},
	{"aw8697_Version_Alert_RTP_231_234Hz.bin"},
	{"aw8697_alarm_Alarm_clock_RTP_232_234Hz.bin"},
	{"aw8697_alarm_Beep_RTP_233_234Hz.bin"},
	{"aw8697_alarm_Breeze_RTP_234_234Hz.bin"},
	{"aw8697_alarm_Dawn_RTP_235_234Hz.bin"},
	{"aw8697_alarm_Dream_RTP_236_234Hz.bin"},
	{"aw8697_alarm_Fluttering_RTP_237_234Hz.bin"},
	{"aw8697_alarm_Flyer_RTP_238_234Hz.bin"},
	{"aw8697_alarm_Interesting_RTP_239_234Hz.bin"},
	{"aw8697_alarm_Leisurely_RTP_240_234Hz.bin"},
	{"aw8697_alarm_Memory_RTP_241_234Hz.bin"},
	{"aw8697_alarm_Relieved_RTP_242_234Hz.bin"},
	{"aw8697_alarm_Ripple_RTP_243_234Hz.bin"},
	{"aw8697_alarm_Slowly_RTP_244_234Hz.bin"},
	{"aw8697_alarm_spring_RTP_245_234Hz.bin"},
	{"aw8697_alarm_Stars_RTP_246_234Hz.bin"},
	{"aw8697_alarm_Surging_RTP_247_234Hz.bin"},
	{"aw8697_alarm_tactfully_RTP_248_234Hz.bin"},
	{"aw8697_alarm_The_wind_RTP_249_234Hz.bin"},
	{"aw8697_alarm_Walking_in_the_rain_RTP_250_234Hz.bin"},
	{"aw8697_BoHaoPanAnJian_RTP_251_234Hz.bin"},
	{"aw8697_BoHaoPanAnNiu_RTP_252_234Hz.bin"},
	{"aw8697_BoHaoPanKuaiJie_RTP_253_234Hz.bin"},
	{"aw8697_DianHuaGuaDuan_RTP_254_234Hz.bin"},
	{"aw8697_DianJinMoShiQieHuan_RTP_255_234Hz.bin"},
	{"aw8697_HuaDongTiaoZhenDong_RTP_256_234Hz.bin"},
	{"aw8697_LeiShen_RTP_257_234Hz.bin"},
	{"aw8697_XuanZhongYouXi_RTP_258_234Hz.bin"},
	{"aw8697_YeJianMoShiDaZi_RTP_259_234Hz.bin"},
	{"aw8697_YouXiSheZhiKuang_RTP_260_234Hz.bin"},
	{"aw8697_ZhuanYeMoShi_RTP_261_234Hz.bin"},
	{"aw8697_Climber_RTP_262_234Hz.bin"},
	{"aw8697_Chase_RTP_263_234Hz.bin"},
	{"aw8697_shuntai24k_RTP_264_234Hz.bin"},
	{"aw8697_wentai24k_RTP_265_234Hz.bin"},
	/* Added by maruthanad.hari@bsp, oneplus ringtone end */
	{"aw8697_reserved.bin"},
	{"aw8697_reserved.bin"},
	{"aw8697_reserved.bin"},
	{"aw8697_reserved.bin"},
	{"aw8697_reserved.bin"},
	{"aw8697_reserved.bin"},
	{"20ms_RTP.bin"},
	{"40ms_RTP.bin"},
	{"60ms_RTP.bin"},
	{"80ms_RTP.bin"},
	{"100ms_RTP.bin"},
	{"120ms_RTP.bin"},
	{"140ms_RTP.bin"},
	{"160ms_RTP.bin"},
	{"180ms_RTP.bin"},
	{"200ms_RTP.bin"},
	{"220ms_RTP.bin"},
	{"240ms_RTP.bin"},
	{"260ms_RTP.bin"},
	{"280ms_RTP.bin"},
	{"300ms_RTP.bin"},
	{"320ms_RTP.bin"},
	{"340ms_RTP.bin"},
	{"360ms_RTP.bin"},
	{"380ms_RTP.bin"},
	{"400ms_RTP.bin"},
	{"420ms_RTP.bin"},
	{"440ms_RTP.bin"},
	{"460ms_RTP.bin"},
	{"480ms_RTP.bin"},
	{"500ms_RTP.bin"},
};

static char aw8697_rtp_name_0832_237Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
#ifdef OPLUS_FEATURE_CHG_BASIC
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2_237Hz.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9_237Hz.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12_237Hz.bin"},
    {"aw8697_Joy_channel_RTP_13_237Hz.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15_237Hz.bin"},
    {"aw8697_Splash_channel_RTP_16_237Hz.bin"},

    {"aw8697_About_School_RTP_17_237Hz.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19_237Hz.bin"},
    {"aw8697_Commuting_RTP_20_237Hz.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22_237Hz.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24_237Hz.bin"},
    {"aw8697_Lakeside_RTP_25_237Hz.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27_237Hz.bin"},
    {"aw8697_Messy_RTP_28_237Hz.bin"},
    {"aw8697_Night_RTP_29_237Hz.bin"},
    {"aw8697_Passionate_Dance_RTP_30_237Hz.bin"},
    {"aw8697_Playground_RTP_31_237Hz.bin"},
    {"aw8697_Relax_RTP_32_237Hz.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34_237Hz.bin"},
    {"aw8697_Silence_RTP_35_237Hz.bin"},
    {"aw8697_Stars_RTP_36_237Hz.bin"},
    {"aw8697_Summer_RTP_37_237Hz.bin"},
    {"aw8697_Toys_RTP_38_237Hz.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},

    {"aw8697_waltz_channel_RTP_41_237Hz.bin"},
    {"aw8697_cut_channel_RTP_42_237Hz.bin"},
    {"aw8697_clock_channel_RTP_43_237Hz.bin"},
    {"aw8697_long_sound_channel_RTP_44_237Hz.bin"},
    {"aw8697_short_channel_RTP_45_237Hz.bin"},
    {"aw8697_two_error_remaind_RTP_46_237Hz.bin"},

    {"aw8697_kill_program_RTP_47_237Hz.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49_237Hz.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},

    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},

    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53_235Hz.bin"},
    {"aw8697_desk_7_RTP_54_237Hz.bin"},
    {"aw8697_nfc_10_RTP_55_237Hz.bin"},
    {"aw8697_vibrator_remain_12_RTP_56_237Hz.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_emergency_warning_RTP_59_234Hz.bin"},

    {"aw8697_honor_fisrt_kill_RTP_60_237Hz.bin"},
    {"aw8697_honor_two_kill_RTP_61_237Hz.bin"},
    {"aw8697_honor_three_kill_RTP_62_237Hz.bin"},
    {"aw8697_honor_four_kill_RTP_63_237Hz.bin"},
    {"aw8697_honor_five_kill_RTP_64_237Hz.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65_237Hz.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66_237Hz.bin"},
    {"aw8697_honor_unstoppable_RTP_67_237Hz.bin"},
    {"aw8697_honor_thousands_kill_RTP_68_237Hz.bin"},
    {"aw8697_honor_lengendary_RTP_69_237Hz.bin"},
    {"aw8697_Airy_morning_RTP_70_237Hz.bin"},
    {"aw8697_Temple_morning_RTP_71_237Hz.bin"},
    {"aw8697_Water_cicidas_72_RTP_237Hz.bin"},
    {"aw8697_Electro_club_RTP_73_237Hz.bin"},
    {"aw8697_Vacation_RTP_74_237Hz.bin"},
    {"aw8697_Jazz_funk_RTP_75_237Hz.bin"},
    {"aw8697_House_club_RTP_76_237Hz.bin"},
    {"aw8697_temple_tone_RTP_77_237Hz.bin"},
    {"aw8697_Jazz_dreamy_RTP_78_237Hz.bin"},
    {"aw8697_Jazz_modern_RTP_79_237Hz.bin"},
    {"aw8697_Tone_round_RTP_80_237Hz.bin"},
    {"aw8697_Digi_rise_RTP_81_237Hz.bin"},
    {"aw8697_Wood_phone_RTP_82_237Hz.bin"},
    {"aw8697_Hey_RTP_83_237Hz.bin"},
    {"aw8697_Zanza_RTP_84_237Hz.bin"},
    {"aw8697_Info_RTP_85_237Hz.bin"},
    {"aw8697_Tip_top_RTP_86_237Hz.bin"},
    {"aw8697_Opop_short_RTP_87_237Hz.bin"},
    {"aw8697_bowl_bells_RTP_88_237Hz.bin"},
    {"aw8697_jumpy_RTP_89_237Hz.bin"},

    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"aw8697_reserved_94.bin"},
    {"aw8697_reserved_95.bin"},
    {"aw8697_reserved_96.bin"},
    {"aw8697_reserved_97.bin"},
    {"aw8697_reserved_98.bin"},
    {"aw8697_reserved_99.bin"},

	{"aw8697_soldier_first_kill_RTP_100_237Hz.bin"},
	{"aw8697_soldier_second_kill_RTP_101_237Hz.bin"},
	{"aw8697_soldier_third_kill_RTP_102_237Hz.bin"},
	{"aw8697_soldier_fourth_kill_RTP_103_237Hz.bin"},
	{"aw8697_soldier_fifth_kill_RTP_104_237Hz.bin"},
	{"aw8697_stepable_regulate_RTP_105_237Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_237Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_237Hz.bin"},
	{"aw8697_reserved_108.bin"},
	{"aw8697_reserved_109.bin"},

	{"aw8697_fingerprint_effect1_RTP_110_237Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_237Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_237Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_237Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_237Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_237Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_237Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_237Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_237Hz.bin"},
	{"aw8697_reserved_119.bin"},

    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121_237Hz.bin"},
    {"aw8697_reserved_122.bin"},
/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123_237Hz.bin"},
    {"aw8697_Miss_RTP_oplusos7_124_237Hz.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125_237Hz.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126_237Hz.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127_237Hz.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128_237Hz.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129_237Hz.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130_237Hz.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131_237Hz.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132_237Hz.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133_237Hz.bin"},

	{"aw8697_Seine_past_RTP_oplusos7_134_237Hz.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135_237Hz.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136_237Hz.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137_237Hz.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138_237Hz.bin"},
    {"aw8697_Dream_RTP_oplusos7_139_237Hz.bin"},
    {"aw8697_Relax_RTP_oplusos7_140_237Hz.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141_237Hz.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142_237Hz.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143_237Hz.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144_237Hz.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145_237Hz.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146_237Hz.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147_237Hz.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148_237Hz.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149_237Hz.bin"},
    {"aw8697_Artist_Notification_RTP_150_237Hz.bin"},
    {"aw8697_Artist_Ringtong_RTP_151_237Hz.bin"},
    {"aw8697_Artist_Text_RTP_152_237Hz.bin"},
    {"aw8697_Artist_Alarm_RTP_153_237Hz.bin"},
#endif
    {"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk_19081.bin"},
    {"aw8697_reserved_156.bin"},
    {"aw8697_reserved_157.bin"},
    {"aw8697_reserved_158.bin"},
    {"aw8697_reserved_159.bin"},
    {"aw8697_reserved_160.bin"},

	{"aw8697_oplus_its_oplus_RTP_161_237Hz.bin"},
    {"aw8697_oplus_tune_RTP_162_237Hz.bin"},
    {"aw8697_oplus_jingle_RTP_163_237Hz.bin"},
    {"aw8697_reserved_164.bin"},
    {"aw8697_reserved_165.bin"},
    {"aw8697_reserved_166.bin"},
    {"aw8697_reserved_167.bin"},
    {"aw8697_reserved_168.bin"},
    {"aw8697_reserved_169.bin"},
    {"aw8697_reserved_170.bin"},
};

static char aw8697_ram_name_9595[5][30] ={
	{"aw8697_haptic_166.bin"},
    {"aw8697_haptic_168.bin"},
    {"aw8697_haptic_170.bin"},
    {"aw8697_haptic_172.bin"},
    {"aw8697_haptic_174.bin"},
};

static char aw8697_old_steady_test_rtp_name_9595[11][60] ={
{"aw8697_old_steady_test_RTP_52_160Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_162Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_164Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_166Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_168Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_170Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_172Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_174Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_176Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_178Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_180Hz.bin"},
};

static char aw8697_high_temp_high_humidity_9595[11][60] ={
{"aw8697_high_temp_high_humidity_channel_RTP_51_160Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_162Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_164Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_166Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_168Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_170Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_172Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_174Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_176Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_178Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_180Hz.bin"},
};

static char aw8697_rtp_name_9595_170Hz[][AW8697_RTP_NAME_MAX] = {
{"aw8697_rtp_170Hz.bin"},
    {"aw8697_Hearty_channel_RTP_1_170Hz.bin"},
    {"aw8697_Instant_channel_RTP_2_170Hz.bin"},
    {"aw8697_Music_channel_RTP_3_170Hz.bin"},
    {"aw8697_Percussion_channel_RTP_4_170Hz.bin"},
    {"aw8697_Ripple_channel_RTP_5_170Hz.bin"},
    {"aw8697_Bright_channel_RTP_6_170Hz.bin"},
    {"aw8697_Fun_channel_RTP_7_170Hz.bin"},
    {"aw8697_Glittering_channel_RTP_8_170Hz.bin"},
    {"aw8697_Granules_channel_RTP_9_170Hz.bin"},
    {"aw8697_Harp_channel_RTP_10_170Hz.bin"},
    {"aw8697_Impression_channel_RTP_11_170Hz.bin"},
    {"aw8697_Ingenious_channel_RTP_12_170Hz.bin"},
    {"aw8697_Joy_channel_RTP_13_170Hz.bin"},
    {"aw8697_Overtone_channel_RTP_14_170Hz.bin"},
    {"aw8697_Receive_channel_RTP_15_170Hz.bin"},
    {"aw8697_Splash_channel_RTP_16_170Hz.bin"},
    {"aw8697_About_School_RTP_17_170Hz.bin"},
    {"aw8697_Bliss_RTP_18_170Hz.bin"},
    {"aw8697_Childhood_RTP_19_170Hz.bin"},
    {"aw8697_Commuting_RTP_20_170Hz.bin"},
    {"aw8697_Dream_RTP_21_170Hz.bin"},
    {"aw8697_Firefly_RTP_22_170Hz.bin"},
    {"aw8697_Gathering_RTP_23_170Hz.bin"},
    {"aw8697_Gaze_RTP_24_170Hz.bin"},
    {"aw8697_Lakeside_RTP_25_170Hz.bin"},
    {"aw8697_Lifestyle_RTP_26_170Hz.bin"},
    {"aw8697_Memories_RTP_27_170Hz.bin"},
    {"aw8697_Messy_RTP_28_170Hz.bin"},
    {"aw8697_Night_RTP_29_170Hz.bin"},
    {"aw8697_Passionate_Dance_RTP_30_170Hz.bin"},
    {"aw8697_Playground_RTP_31_170Hz.bin"},
    {"aw8697_Relax_RTP_32_170Hz.bin"},
    {"aw8697_Reminiscence_RTP_33_170Hz.bin"},
    {"aw8697_Silence_From_Afar_RTP_34_170Hz.bin"},
    {"aw8697_Silence_RTP_35_170Hz.bin"},
    {"aw8697_Stars_RTP_36_170Hz.bin"},
    {"aw8697_Summer_RTP_37_170Hz.bin"},
    {"aw8697_Toys_RTP_38_170Hz.bin"},
    {"aw8697_Travel_RTP_39_170Hz.bin"},
    {"aw8697_Vision_RTP_40_170Hz.bin"},
    {"aw8697_waltz_channel_RTP_41_170Hz.bin"},
    {"aw8697_cut_channel_RTP_42_170Hz.bin"},
    {"aw8697_clock_channel_RTP_43_170Hz.bin"},
    {"aw8697_long_sound_channel_RTP_44_170Hz.bin"},
    {"aw8697_short_channel_RTP_45_170Hz.bin"},
    {"aw8697_two_error_remaind_RTP_46_170Hz.bin"},
    {"aw8697_kill_program_RTP_47_170Hz.bin"},
    {"aw8697_Simple_channel_RTP_48_170Hz.bin"},
    {"aw8697_Pure_RTP_49_170Hz.bin"},
    {"aw8697_reserved_sound_channel_RTP_50_170Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_170Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_170Hz.bin"},
    {"aw8697_listen_pop_53_170Hz.bin"},
    {"aw8697_desk_7_RTP_54_170Hz.bin"},
    {"aw8697_nfc_10_RTP_55_170Hz.bin"},
    {"aw8697_vibrator_remain_12_RTP_56_170Hz.bin"},
    {"aw8697_notice_13_RTP_57_170Hz.bin"},
    {"aw8697_third_ring_14_RTP_58_170Hz.bin"},
    {"aw8697_reserved_59.bin"},
    {"aw8697_honor_fisrt_kill_RTP_60_170Hz.bin"},
    {"aw8697_honor_two_kill_RTP_61_170Hz.bin"},
    {"aw8697_honor_three_kill_RTP_62_170Hz.bin"},
    {"aw8697_honor_four_kill_RTP_63_170Hz.bin"},
    {"aw8697_honor_five_kill_RTP_64_170Hz.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65_170Hz.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66_170Hz.bin"},
    {"aw8697_honor_unstoppable_RTP_67_170Hz.bin"},
    {"aw8697_honor_thousands_kill_RTP_68_170Hz.bin"},
    {"aw8697_honor_lengendary_RTP_69_170Hz.bin"},
    {"aw8697_reserved_70.bin"},
    {"aw8697_reserved_71.bin"},
    {"aw8697_reserved_72.bin"},
    {"aw8697_reserved_73.bin"},
    {"aw8697_reserved_74.bin"},
    {"aw8697_reserved_75.bin"},
    {"aw8697_reserved_76.bin"},
    {"aw8697_reserved_77.bin"},
    {"aw8697_reserved_78.bin"},
    {"aw8697_reserved_79.bin"},
    {"aw8697_reserved_80.bin"},
    {"aw8697_reserved_81.bin"},
    {"aw8697_reserved_82.bin"},
    {"aw8697_reserved_83.bin"},
    {"aw8697_reserved_84.bin"},
    {"aw8697_reserved_85.bin"},
    {"aw8697_reserved_86.bin"},
    {"aw8697_reserved_87.bin"},
    {"aw8697_reserved_88.bin"},
    {"aw8697_reserved_89.bin"},
    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"ALCloudscape_170HZ.bin"},
    {"ALGoodenergy_170HZ.bin"},
    {"NTblink_170HZ.bin"},
    {"NTwhoop_170HZ.bin"},
    {"Newfeeling_170HZ.bin"},
    {"nature_170HZ.bin"},
    {"aw8697_soldier_first_kill_RTP_100_170Hz.bin"},
    {"aw8697_soldier_second_kill_RTP_101_170Hz.bin"},
    {"aw8697_soldier_third_kill_RTP_102_170Hz.bin"},
    {"aw8697_soldier_fourth_kill_RTP_103_170Hz.bin"},
    {"aw8697_soldier_fifth_kill_RTP_104_170Hz.bin"},
    {"aw8697_stepable_regulate_RTP_105_170Hz.bin"},
    {"aw8697_voice_level_bar_edge_RTP_106_170Hz.bin"},
    {"aw8697_strength_level_bar_edge_RTP_107_170Hz.bin"},
    {"aw8697_charging_simulation_RTP_108_170Hz.bin"},
    {"aw8697_fingerprint_success_RTP_109_170Hz.bin"},
    {"aw8697_fingerprint_effect1_RTP_110_170Hz.bin"},
    {"aw8697_fingerprint_effect2_RTP_111_170Hz.bin"},
    {"aw8697_fingerprint_effect3_RTP_112_170Hz.bin"},
    {"aw8697_fingerprint_effect4_RTP_113_170Hz.bin"},
    {"aw8697_fingerprint_effect5_RTP_114_170Hz.bin"},
    {"aw8697_fingerprint_effect6_RTP_115_170Hz.bin"},
    {"aw8697_fingerprint_effect7_RTP_116_170Hz.bin"},
    {"aw8697_fingerprint_effect8_RTP_117_170Hz.bin"},
    {"aw8697_breath_simulation_RTP_118_170Hz.bin"},
    {"aw8697_reserved_119.bin"},
    {"aw8697_Miss_RTP_120_170Hz.bin"},
    {"aw8697_Scenic_RTP_121_170Hz.bin"},
    {"aw8697_reserved_122.bin"},
/* used for oplusos 7 */
	{"a8697_Appear_channel_RTP_oplusos7_123_170Hz.bin"},
    {"aw8697_Miss_RTP_oplusos7_124_170Hz.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125_170Hz.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126_170Hz.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127_170Hz.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128_170Hz.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129_170Hz.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130_170Hz.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131_170Hz.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132_170Hz.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133_170Hz.bin"},
    {"aw8697_Seine_past_RTP_oplusos7_134_170Hz.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135_170Hz.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136_170Hz.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137_170Hz.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138_170Hz.bin"},
    {"aw8697_Dream_RTP_oplusos7_139_170Hz.bin"},
    {"aw8697_Relax_RTP_oplusos7_140_170Hz.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141_170Hz.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142_170Hz.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143_170Hz.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144_170Hz.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145_170Hz.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146_170Hz.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147_170Hz.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148_170Hz.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149_170Hz.bin"},
/* used for oplusos 7 end*/
	{"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk.bin"},
    {"ringtone_Alacrity_RTP.bin"},
    {"ring_Amenity_RTP.bin"},
    {"ringtone_Blues_RTP.bin"},
    {"ring_Bounce_RTP.bin"},
    {"ring_Calm_RTP.bin"},
    {"ringtone_Cloud_RTP.bin"},
    {"ringtone_Cyclotron_RTP.bin"},
    {"ringtone_Distinct_RTP.bin"},
    {"ringtone_Dynamic_RTP.bin"},
    {"ringtone_Echo_RTP.bin"},
    {"ringtone_Expect_RTP.bin"},
    {"ringtone_Fanatical_RTP.bin"},
    {"ringtone_Funky_RTP.bin"},
    {"ringtone_Guitar_RTP.bin"},
    {"ringtone_Harping_RTP.bin"},
    {"ringtone_Highlight_RTP.bin"},
    {"ringtone_Idyl_RTP.bin"},
    {"ringtone_Innocence_RTP.bin"},
    {"ringtone_Journey_RTP.bin"},
    {"ringtone_Joyous_RTP.bin"},
    {"ring_Lazy_RTP.bin"},
    {"ringtone_Marimba_RTP.bin"},
    {"ring_Mystical_RTP.bin"},
    {"ringtone_Old_telephone_RTP.bin"},
    {"ringtone_Oneplus_tune_RTP.bin"},
    {"ringtone_Rhythm_RTP.bin"},
    {"ringtone_Optimistic_RTP.bin"},
    {"ringtone_Piano_RTP.bin"},
    {"ring_Whirl_RTP.bin"},
    {"VZW_Alrwave_RTP.bin"},
    {"t-jingle_RTP.bin"},
    {"ringtone_Eager.bin"},
    {"ringtone_Ebullition.bin"},
    {"ringtone_Friendship.bin"},
    {"ringtone_Jazz_life_RTP.bin"},
    {"ringtone_Sun_glittering_RTP.bin"},
    {"notif_Allay_RTP.bin"},
    {"notif_Allusion_RTP.bin"},
    {"notif_Amiable_RTP.bin"},
    {"notif_Blare_RTP.bin"},
    {"notif_Blissful_RTP.bin"},
    {"notif_Brisk_RTP.bin"},
    {"notif_Bubble_RTP.bin"},
    {"notif_Cheerful_RTP.bin"},
    {"notif_Clear_RTP.bin"},
    {"notif_Comely_RTP.bin"},
    {"notif_Cozy_RTP.bin"},
    {"notif_Ding_RTP.bin"},
    {"notif_Effervesce_RTP.bin"},
    {"notif_Elegant_RTP.bin"},
    {"notif_Free_RTP.bin"},
    {"notif_Hallucination_RTP.bin"},
    {"notif_Inbound_RTP.bin"},
    {"notif_Light_RTP.bin"},
    {"notif_Meet_RTP.bin"},
    {"notif_Naivety_RTP.bin"},
    {"notif_Quickly_RTP.bin"},
    {"notif_Rhythm_RTP.bin"},
    {"notif_Surprise_RTP.bin"},
    {"notif_Twinkle_RTP.bin"},
    {"Version_Alert_RTP.bin"},
    {"alarm_Alarm_clock_RTP.bin"},
    {"alarm_Beep_RTP.bin"},
    {"alarm_Breeze_RTP.bin"},
    {"alarm_Dawn_RTP.bin"},
    {"alarm_Dream_RTP.bin"},
    {"alarm_Fluttering_RTP.bin"},
    {"alarm_Flyer_RTP.bin"},
    {"alarm_Interesting_RTP.bin"},
    {"alarm_Leisurely_RTP.bin"},
    {"alarm_Memory_RTP.bin"},
    {"alarm_Relieved_RTP.bin"},
    {"alarm_Ripple_RTP.bin"},
    {"alarm_Slowly_RTP.bin"},
    {"alarm_spring_RTP.bin"},
    {"alarm_Stars_RTP.bin"},
    {"alarm_Surging_RTP.bin"},
    {"alarm_tactfully_RTP.bin"},
    {"alarm_The_wind_RTP.bin"},
    {"alarm_Walking_in_the_rain_RTP.bin"},
    {"BoHaoPanAnJian.bin"},
    {"BoHaoPanAnNiu.bin"},
    {"BoHaoPanKuaiJie.bin"},
    {"DianHuaGuaDuan.bin"},
    {"DianJinMoShiQieHuan.bin"},
    {"HuaDongTiaoZhenDong.bin"},
    {"LeiShen.bin"},
    {"XuanZhongYouXi.bin"},
    {"YeJianMoShiDaZi.bin"},
    {"YouXiSheZhiKuang.bin"},
    {"ZhuanYeMoShi.bin"},
    {"Climber_RTP.bin"},
    {"Chase_RTP.bin"},
    {"shuntai24k_rtp.bin"},
    {"wentai24k_rtp.bin"},
    {"20ms_RTP.bin"},
    {"40ms_RTP.bin"},
    {"60ms_RTP.bin"},
    {"80ms_RTP.bin"},
    {"100ms_RTP.bin"},
    {"120ms_RTP.bin"},
    {"140ms_RTP.bin"},
    {"160ms_RTP.bin"},
    {"180ms_RTP.bin"},
    {"200ms_RTP.bin"},
    {"220ms_RTP.bin"},
    {"240ms_RTP.bin"},
    {"260ms_RTP.bin"},
    {"280ms_RTP.bin"},
    {"300ms_RTP.bin"},
    {"320ms_RTP.bin"},
    {"340ms_RTP.bin"},
    {"360ms_RTP.bin"},
    {"380ms_RTP.bin"},
    {"400ms_RTP.bin"},
    {"420ms_RTP.bin"},
    {"440ms_RTP.bin"},
    {"460ms_RTP.bin"},
    {"480ms_RTP.bin"},
    {"500ms_RTP.bin"},
};

#ifdef CONFIG_OPLUS_HAPTIC_OOS
static char aw8697_ram_name_1815[5][30] ={
{"aw8697_haptic_166.bin"},
    {"aw8697_haptic_168.bin"},
    {"aw8697_haptic_170.bin"},
    {"aw8697_haptic_172.bin"},
    {"aw8697_haptic_174.bin"},
};

static char aw8697_old_steady_test_rtp_name_1815[11][60] ={
{"aw8697_old_steady_test_RTP_52_160Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_162Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_164Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_166Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_168Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_170Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_172Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_174Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_176Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_178Hz.bin"},
    {"aw8697_old_steady_test_RTP_52_180Hz.bin"},
};

static char aw8697_high_temp_high_humidity_1815[11][60] ={
{"aw8697_high_temp_high_humidity_channel_RTP_51_160Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_162Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_164Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_166Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_168Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_170Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_172Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_174Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_176Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_178Hz.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51_180Hz.bin"},
};

static char aw8697_rtp_name_1815_170Hz[][AW8697_RTP_NAME_MAX] = {
{"aw8697_rtp.bin"},
    {"aw8697_Hearty_channel_RTP_1.bin"},
    {"aw8697_Instant_channel_RTP_2.bin"},
    {"aw8697_Music_channel_RTP_3.bin"},
    {"aw8697_Percussion_channel_RTP_4.bin"},
    {"aw8697_Ripple_channel_RTP_5.bin"},
    {"aw8697_Bright_channel_RTP_6.bin"},
    {"aw8697_Fun_channel_RTP_7.bin"},
    {"aw8697_Glittering_channel_RTP_8.bin"},
    {"aw8697_Granules_channel_RTP_9.bin"},
    {"aw8697_Harp_channel_RTP_10.bin"},
    {"aw8697_Impression_channel_RTP_11.bin"},
    {"aw8697_Ingenious_channel_RTP_12.bin"},
    {"aw8697_Joy_channel_RTP_13.bin"},
    {"aw8697_Overtone_channel_RTP_14.bin"},
    {"aw8697_Receive_channel_RTP_15.bin"},
    {"aw8697_Splash_channel_RTP_16.bin"},
    {"aw8697_About_School_RTP_17.bin"},
    {"aw8697_Bliss_RTP_18.bin"},
    {"aw8697_Childhood_RTP_19.bin"},
    {"aw8697_Commuting_RTP_20.bin"},
    {"aw8697_Dream_RTP_21.bin"},
    {"aw8697_Firefly_RTP_22.bin"},
    {"aw8697_Gathering_RTP_23.bin"},
    {"aw8697_Gaze_RTP_24.bin"},
    {"aw8697_Lakeside_RTP_25.bin"},
    {"aw8697_Lifestyle_RTP_26.bin"},
    {"aw8697_Memories_RTP_27.bin"},
    {"aw8697_Messy_RTP_28.bin"},
    {"aw8697_Night_RTP_29.bin"},
    {"aw8697_Passionate_Dance_RTP_30.bin"},
    {"aw8697_Playground_RTP_31.bin"},
    {"aw8697_Relax_RTP_32.bin"},
    {"aw8697_Reminiscence_RTP_33.bin"},
    {"aw8697_Silence_From_Afar_RTP_34.bin"},
    {"aw8697_Silence_RTP_35.bin"},
    {"aw8697_Stars_RTP_36.bin"},
    {"aw8697_Summer_RTP_37.bin"},
    {"aw8697_Toys_RTP_38.bin"},
    {"aw8697_Travel_RTP_39.bin"},
    {"aw8697_Vision_RTP_40.bin"},
    {"aw8697_waltz_channel_RTP_41.bin"},
    {"aw8697_cut_channel_RTP_42.bin"},
    {"aw8697_clock_channel_RTP_43.bin"},
    {"aw8697_long_sound_channel_RTP_44.bin"},
    {"aw8697_short_channel_RTP_45.bin"},
    {"aw8697_two_error_remaind_RTP_46.bin"},
    {"aw8697_kill_program_RTP_47.bin"},
    {"aw8697_Simple_channel_RTP_48.bin"},
    {"aw8697_Pure_RTP_49.bin"},
    {"aw8697_reserved_sound_channel_RTP_50.bin"},
    {"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},
    {"aw8697_old_steady_test_RTP_52.bin"},
    {"aw8697_listen_pop_53.bin"},
    {"aw8697_desk_7_RTP_54.bin"},
    {"aw8697_nfc_10_RTP_55.bin"},
    {"aw8697_vibrator_remain_12_RTP_56.bin"},
    {"aw8697_notice_13_RTP_57.bin"},
    {"aw8697_third_ring_14_RTP_58.bin"},
    {"aw8697_reserved_59.bin"},
    {"aw8697_honor_fisrt_kill_RTP_60.bin"},
    {"aw8697_honor_two_kill_RTP_61.bin"},
    {"aw8697_honor_three_kill_RTP_62.bin"},
    {"aw8697_honor_four_kill_RTP_63.bin"},
    {"aw8697_honor_five_kill_RTP_64.bin"},
    {"aw8697_honor_three_continu_kill_RTP_65.bin"},
    {"aw8697_honor_four_continu_kill_RTP_66.bin"},
    {"aw8697_honor_unstoppable_RTP_67.bin"},
    {"aw8697_honor_thousands_kill_RTP_68.bin"},
    {"aw8697_honor_lengendary_RTP_69.bin"},
    {"aw8697_reserved_70.bin"},
    {"aw8697_reserved_71.bin"},
    {"aw8697_reserved_72.bin"},
    {"aw8697_reserved_73.bin"},
    {"aw8697_reserved_74.bin"},
    {"aw8697_reserved_75.bin"},
    {"aw8697_reserved_76.bin"},
    {"aw8697_reserved_77.bin"},
    {"aw8697_reserved_78.bin"},
    {"aw8697_reserved_79.bin"},
    {"aw8697_reserved_80.bin"},
    {"aw8697_reserved_81.bin"},
    {"aw8697_reserved_82.bin"},
    {"aw8697_reserved_83.bin"},
    {"aw8697_reserved_84.bin"},
    {"aw8697_reserved_85.bin"},
    {"aw8697_reserved_86.bin"},
    {"aw8697_reserved_87.bin"},
    {"aw8697_reserved_88.bin"},
    {"aw8697_reserved_89.bin"},
    {"aw8697_reserved_90.bin"},
    {"aw8697_reserved_91.bin"},
    {"aw8697_reserved_92.bin"},
    {"aw8697_reserved_93.bin"},
    {"ALCloudscape_170HZ.bin"},
    {"ALGoodenergy_170HZ.bin"},
    {"NTblink_170HZ.bin"},
    {"NTwhoop_170HZ.bin"},
    {"Newfeeling_170HZ.bin"},
    {"nature_170HZ.bin"},
    {"aw8697_soldier_first_kill_RTP_100.bin"},
    {"aw8697_soldier_second_kill_RTP_101.bin"},
    {"aw8697_soldier_third_kill_RTP_102.bin"},
    {"aw8697_soldier_fourth_kill_RTP_103.bin"},
    {"aw8697_soldier_fifth_kill_RTP_104.bin"},
    {"aw8697_stepable_regulate_RTP_105.bin"},
    {"aw8697_voice_level_bar_edge_RTP_106.bin"},
    {"aw8697_strength_level_bar_edge_RTP_107.bin"},
    {"aw8697_reserved_108.bin"},
    {"aw8697_reserved_109.bin"},
    {"aw8697_reserved_110.bin"},
    {"aw8697_reserved_111.bin"},
    {"aw8697_reserved_112.bin"},
    {"aw8697_reserved_113.bin"},
    {"aw8697_reserved_114.bin"},
    {"aw8697_reserved_115.bin"},
    {"aw8697_reserved_116.bin"},
    {"aw8697_reserved_117.bin"},
    {"aw8697_reserved_118.bin"},
    {"aw8697_reserved_119.bin"},
    {"aw8697_Miss_RTP_120.bin"},
    {"aw8697_Scenic_RTP_121.bin"},
    {"aw8697_reserved_122.bin"},
/* used for oplusos 7 */
	{"aw8697_Appear_channel_RTP_oplusos7_123.bin"},
    {"aw8697_Miss_RTP_oplusos7_124.bin"},
    {"aw8697_Music_channel_RTP_oplusos7_125.bin"},
    {"aw8697_Percussion_channel_RTP_oplusos7_126.bin"},
    {"aw8697_Ripple_channel_RTP_oplusos7_127.bin"},
    {"aw8697_Bright_channel_RTP_oplusos7_128.bin"},
    {"aw8697_Fun_channel_RTP_oplusos7_129.bin"},
    {"aw8697_Glittering_channel_RTP_oplusos7_130.bin"},
    {"aw8697_Harp_channel_RTP_oplusos7_131.bin"},
    {"aw8697_Overtone_channel_RTP_oplusos7_132.bin"},
    {"aw8697_Simple_channel_RTP_oplusos7_133.bin"},
    {"aw8697_Seine_past_RTP_oplusos7_134.bin"},
    {"aw8697_Classical_ring_RTP_oplusos7_135.bin"},
    {"aw8697_Long_for_RTP_oplusos7_136.bin"},
    {"aw8697_Romantic_RTP_oplusos7_137.bin"},
    {"aw8697_Bliss_RTP_oplusos7_138.bin"},
    {"aw8697_Dream_RTP_oplusos7_139.bin"},
    {"aw8697_Relax_RTP_oplusos7_140.bin"},
    {"aw8697_Joy_channel_RTP_oplusos7_141.bin"},
    {"aw8697_weather_wind_RTP_oplusos7_142.bin"},
    {"aw8697_weather_cloudy_RTP_oplusos7_143.bin"},
    {"aw8697_weather_thunderstorm_RTP_oplusos7_144.bin"},
    {"aw8697_weather_default_RTP_oplusos7_145.bin"},
    {"aw8697_weather_sunny_RTP_oplusos7_146.bin"},
    {"aw8697_weather_smog_RTP_oplusos7_147.bin"},
    {"aw8697_weather_snow_RTP_oplusos7_148.bin"},
    {"aw8697_weather_rain_RTP_oplusos7_149.bin"},
/* used for oplusos 7 end*/
	{"aw8697_rtp_lighthouse.bin"},
    {"aw8697_rtp_silk.bin"},
    {"aw8697_reserved_152.bin"},
    {"aw8697_reserved_153.bin"},
    {"aw8697_reserved_154.bin"},
    {"aw8697_reserved_155.bin"},
    {"aw8697_reserved_156.bin"},
    {"aw8697_reserved_157.bin"},
    {"aw8697_reserved_158.bin"},
    {"aw8697_reserved_159.bin"},
    {"aw8697_reserved_160.bin"},
    {"aw8697_reserved_161.bin"},
    {"aw8697_reserved_162.bin"},
    {"aw8697_reserved_163.bin"},
    {"aw8697_reserved_164.bin"},
    {"aw8697_reserved_165.bin"},
    {"aw8697_reserved_166.bin"},
    {"aw8697_reserved_167.bin"},
    {"aw8697_reserved_168.bin"},
    {"aw8697_reserved_169.bin"},
    {"aw8697_reserved_170.bin"},
    {"ringtone_Alacrity_RTP.bin"},
    {"ring_Amenity_RTP.bin"},
    {"ringtone_Blues_RTP.bin"},
    {"ring_Bounce_RTP.bin"},
    {"ring_Calm_RTP.bin"},
    {"ringtone_Cloud_RTP.bin"},
    {"ringtone_Cyclotron_RTP.bin"},
    {"ringtone_Distinct_RTP.bin"},
    {"ringtone_Dynamic_RTP.bin"},
    {"ringtone_Echo_RTP.bin"},
    {"ringtone_Expect_RTP.bin"},
    {"ringtone_Fanatical_RTP.bin"},
    {"ringtone_Funky_RTP.bin"},
    {"ringtone_Guitar_RTP.bin"},
    {"ringtone_Harping_RTP.bin"},
    {"ringtone_Highlight_RTP.bin"},
    {"ringtone_Idyl_RTP.bin"},
    {"ringtone_Innocence_RTP.bin"},
    {"ringtone_Journey_RTP.bin"},
    {"ringtone_Joyous_RTP.bin"},
    {"ring_Lazy_RTP.bin"},
    {"ringtone_Marimba_RTP.bin"},
    {"ring_Mystical_RTP.bin"},
    {"ringtone_Old_telephone_RTP.bin"},
    {"ringtone_Oneplus_tune_RTP.bin"},
    {"ringtone_Rhythm_RTP.bin"},
    {"ringtone_Optimistic_RTP.bin"},
    {"ringtone_Piano_RTP.bin"},
    {"ring_Whirl_RTP.bin"},
    {"VZW_Alrwave_RTP.bin"},
    {"t-jingle_RTP.bin"},
    {"ringtone_Eager.bin"},
    {"ringtone_Ebullition.bin"},
    {"ringtone_Friendship.bin"},
    {"ringtone_Jazz_life_RTP.bin"},
    {"ringtone_Sun_glittering_RTP.bin"},
    {"notif_Allay_RTP.bin"},
    {"notif_Allusion_RTP.bin"},
    {"notif_Amiable_RTP.bin"},
    {"notif_Blare_RTP.bin"},
    {"notif_Blissful_RTP.bin"},
    {"notif_Brisk_RTP.bin"},
    {"notif_Bubble_RTP.bin"},
    {"notif_Cheerful_RTP.bin"},
    {"notif_Clear_RTP.bin"},
    {"notif_Comely_RTP.bin"},
    {"notif_Cozy_RTP.bin"},
    {"notif_Ding_RTP.bin"},
    {"notif_Effervesce_RTP.bin"},
    {"notif_Elegant_RTP.bin"},
    {"notif_Free_RTP.bin"},
    {"notif_Hallucination_RTP.bin"},
    {"notif_Inbound_RTP.bin"},
    {"notif_Light_RTP.bin"},
    {"notif_Meet_RTP.bin"},
    {"notif_Naivety_RTP.bin"},
    {"notif_Quickly_RTP.bin"},
    {"notif_Rhythm_RTP.bin"},
    {"notif_Surprise_RTP.bin"},
    {"notif_Twinkle_RTP.bin"},
    {"Version_Alert_RTP.bin"},
    {"alarm_Alarm_clock_RTP.bin"},
    {"alarm_Beep_RTP.bin"},
    {"alarm_Breeze_RTP.bin"},
    {"alarm_Dawn_RTP.bin"},
    {"alarm_Dream_RTP.bin"},
    {"alarm_Fluttering_RTP.bin"},
    {"alarm_Flyer_RTP.bin"},
    {"alarm_Interesting_RTP.bin"},
    {"alarm_Leisurely_RTP.bin"},
    {"alarm_Memory_RTP.bin"},
    {"alarm_Relieved_RTP.bin"},
    {"alarm_Ripple_RTP.bin"},
    {"alarm_Slowly_RTP.bin"},
    {"alarm_spring_RTP.bin"},
    {"alarm_Stars_RTP.bin"},
    {"alarm_Surging_RTP.bin"},
    {"alarm_tactfully_RTP.bin"},
    {"alarm_The_wind_RTP.bin"},
    {"alarm_Walking_in_the_rain_RTP.bin"},
    {"BoHaoPanAnJian.bin"},
    {"BoHaoPanAnNiu.bin"},
    {"BoHaoPanKuaiJie.bin"},
    {"DianHuaGuaDuan.bin"},
    {"DianJinMoShiQieHuan.bin"},
    {"HuaDongTiaoZhenDong.bin"},
    {"LeiShen.bin"},
    {"XuanZhongYouXi.bin"},
    {"YeJianMoShiDaZi.bin"},
    {"YouXiSheZhiKuang.bin"},
    {"ZhuanYeMoShi.bin"},
    {"Climber_RTP.bin"},
    {"Chase_RTP.bin"},
    {"shuntai24k_rtp.bin"},
    {"wentai24k_rtp.bin"},
    {"Rock_RTP.bin"},
    {"In_game_ringtone_RTP.bin"},
    {"Wake_up_samurai_RTP.bin"},
    {"In_game_alarm_RTP.bin"},
    {"In_game_sms_RTP.bin"},
    {"Audition_RTP.bin"},
    {"20ms_RTP.bin"},
    {"40ms_RTP.bin"},
    {"60ms_RTP.bin"},
    {"80ms_RTP.bin"},
    {"100ms_RTP.bin"},
    {"120ms_RTP.bin"},
    {"140ms_RTP.bin"},
    {"160ms_RTP.bin"},
    {"180ms_RTP.bin"},
    {"200ms_RTP.bin"},
    {"220ms_RTP.bin"},
    {"240ms_RTP.bin"},
    {"260ms_RTP.bin"},
    {"280ms_RTP.bin"},
    {"300ms_RTP.bin"},
    {"320ms_RTP.bin"},
    {"340ms_RTP.bin"},
    {"360ms_RTP.bin"},
    {"380ms_RTP.bin"},
    {"400ms_RTP.bin"},
    {"420ms_RTP.bin"},
    {"440ms_RTP.bin"},
    {"460ms_RTP.bin"},
    {"480ms_RTP.bin"},
    {"500ms_RTP.bin"},
};

static char aw8697_rtp_name_0619_166Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
	{"aw8697_Hearty_channel_RTP_1.bin"},
	{"aw8697_Instant_channel_RTP_2.bin"},
	{"aw8697_Music_channel_RTP_3.bin"},
	{"aw8697_Percussion_channel_RTP_4.bin"},
	{"aw8697_Ripple_channel_RTP_5.bin"},
	{"aw8697_Bright_channel_RTP_6.bin"},
	{"aw8697_Fun_channel_RTP_7.bin"},
	{"aw8697_Glittering_channel_RTP_8.bin"},
	{"aw8697_Granules_channel_RTP_9.bin"},
	{"aw8697_Harp_channel_RTP_10.bin"},
	{"aw8697_Impression_channel_RTP_11.bin"},
	{"aw8697_Ingenious_channel_RTP_12.bin"},
	{"aw8697_Joy_channel_RTP_13.bin"},
	{"aw8697_Overtone_channel_RTP_14.bin"},
	{"aw8697_Receive_channel_RTP_15.bin"},
	{"aw8697_Splash_channel_RTP_16.bin"},
	{"aw8697_About_School_RTP_17.bin"},
	{"aw8697_Bliss_RTP_18.bin"},
	{"aw8697_Childhood_RTP_19.bin"},
	{"aw8697_Commuting_RTP_20.bin"},
	{"aw8697_Dream_RTP_21.bin"},
	{"aw8697_Firefly_RTP_22.bin"},
	{"aw8697_Gathering_RTP_23.bin"},
	{"aw8697_Gaze_RTP_24.bin"},
	{"aw8697_Lakeside_RTP_25.bin"},
	{"aw8697_Lifestyle_RTP_26.bin"},
	{"aw8697_Memories_RTP_27.bin"},
	{"aw8697_Messy_RTP_28.bin"},
	{"aw8697_Night_RTP_29.bin"},
	{"aw8697_Passionate_Dance_RTP_30.bin"},
	{"aw8697_Playground_RTP_31.bin"},
	{"aw8697_Relax_RTP_32.bin"},
	{"aw8697_Reminiscence_RTP_33.bin"},
	{"aw8697_Silence_From_Afar_RTP_34.bin"},
	{"aw8697_Silence_RTP_35.bin"},
	{"aw8697_Stars_RTP_36.bin"},
	{"aw8697_Summer_RTP_37.bin"},
	{"aw8697_Toys_RTP_38.bin"},
	{"aw8697_Travel_RTP_39.bin"},
	{"aw8697_Vision_RTP_40.bin"},
	{"aw8697_waltz_channel_RTP_41_166Hz.bin"},
	{"aw8697_cut_channel_RTP_42_166Hz.bin"},
	{"aw8697_clock_channel_RTP_43_166Hz.bin"},
	{"aw8697_long_sound_channel_RTP_44_166Hz.bin"},
	{"aw8697_short_channel_RTP_45_166Hz.bin"},
	{"aw8697_two_error_remaind_RTP_46_166Hz.bin"},
	{"aw8697_kill_program_166Hz.bin"},
	{"aw8697_Simple_channel_RTP_48.bin"},
	{"aw8697_Pure_RTP_49.bin"},
	{"aw8697_reserved_sound_channel_RTP_50.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},
	{"aw8697_old_steady_test_RTP_52.bin"},
	{"aw8697_listen_pop_53.bin"},
	{"aw8697_desk_54_166Hz.bin"},
	{"aw8697_nfc_55_166Hz.bin"},
	{"aw8697_vibrator_remain_12_166Hz.bin"},
	{"aw8697_notice_13_RTP_57.bin"},
	{"aw8697_third_ring_14_RTP_58.bin"},
	{"aw8697_reserved_59.bin"},
	{"aw8697_honor_fisrt_kill_RTP_60.bin"},
	{"aw8697_honor_two_kill_RTP_61.bin"},
	{"aw8697_honor_three_kill_RTP_62.bin"},
	{"aw8697_honor_four_kill_RTP_63.bin"},
	{"aw8697_honor_five_kill_RTP_64.bin"},
	{"aw8697_honor_three_continu_kill_RTP_65.bin"},
	{"aw8697_honor_four_continu_kill_RTP_66.bin"},
	{"aw8697_honor_unstoppable_RTP_67.bin"},
	{"aw8697_honor_thousands_kill_RTP_68.bin"},
	{"aw8697_honor_lengendary_RTP_69.bin"},
	{"aw8697_Airy_morning_RTP_70.bin"},
	{"aw8697_Temple_morning_RTP_71.bin"},
	{"aw8697_Water_cicidas_72_RTP.bin"},
	{"aw8697_Electro_club_RTP_73.bin"},
	{"aw8697_Vacation_RTP_74.bin"},
	{"aw8697_Jazz_funk_RTP_75.bin"},
	{"aw8697_House_club_RTP_76.bin"},
	{"aw8697_temple_tone_RTP_77.bin"},
	{"aw8697_Jazz_dreamy_RTP_78.bin"},
	{"aw8697_Jazz_modern_RTP_79.bin"},
	{"aw8697_Tone_round_RTP_80.bin"},
	{"aw8697_Digi_rise_RTP_81.bin"},
	{"aw8697_Wood_phone_RTP_82.bin"},
	{"aw8697_Hey_RTP_83.bin"},
	{"aw8697_Zanza_RTP_84.bin"},
	{"aw8697_Info_RTP_85.bin"},
	{"aw8697_Tip_top_RTP_86.bin"},
	{"aw8697_Opop_short_RTP_87.bin"},
	{"aw8697_bowl_bells_RTP_88.bin"},
	{"aw8697_jumpy_RTP_89.bin"},
	{"aw8697_reserved_90.bin"},
	{"aw8697_reserved_91.bin"},
	{"aw8697_reserved_92.bin"},
	{"aw8697_reserved_93.bin"},
	{"Cloudscape_166Hz.bin"},
	{"Good_energy_166Hz.bin"},
	{"Blink_166Hz.bin"},
	{"Whoop_doop_166Hz.bin"},
	{"OnePlus_new_feeling_166Hz.bin"},
	{"Nature_166Hz.bin"},
	{"aw8697_reserved_100.bin"},
	{"aw8697_reserved_101.bin"},
	{"aw8697_reserved_102.bin"},
	{"aw8697_reserved_103.bin"},
	{"aw8697_reserved_104.bin"},
	{"aw8697_stepable_regulate_RTP_105_166Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_166Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_166Hz.bin"},
	{"aw8697_charging_simulation_RTP_108_166Hz.bin"},
	{"aw8697_reserved_109.bin"},
	{"aw8697_fingerprint_effect1_RTP_110_166Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_166Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_166Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_166Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_166Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_166Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_166Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_166Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_166Hz.bin"},
	{"aw8697_reserved_119.bin"},
	{"aw8697_reserved_120.bin"},
	{"aw8697_reserved_121.bin"},
	{"aw8697_voice_assistant_RTP_122_166Hz.bin"},
	{"aw8697_reserved_123.bin"},
	{"aw8697_reserved_124.bin"},
	{"aw8697_reserved_125.bin"},
	{"aw8697_reserved_126.bin"},
	{"aw8697_reserved_127.bin"},
	{"aw8697_reserved_128.bin"},
	{"aw8697_reserved_129.bin"},
	{"aw8697_reserved_130.bin"},
	{"aw8697_reserved_131.bin"},
	{"aw8697_reserved_132.bin"},
	{"aw8697_reserved_133.bin"},
	{"aw8697_reserved_134.bin"},
	{"aw8697_reserved_135.bin"},
	{"aw8697_reserved_136.bin"},
	{"aw8697_reserved_137.bin"},
	{"aw8697_reserved_138.bin"},
	{"aw8697_reserved_139.bin"},
	{"aw8697_reserved_140.bin"},
	{"aw8697_reserved_141.bin"},
	{"aw8697_reserved_142.bin"},
	{"aw8697_reserved_143.bin"},
	{"aw8697_reserved_144.bin"},
	{"aw8697_reserved_145.bin"},
	{"aw8697_reserved_146.bin"},
	{"aw8697_reserved_147.bin"},
	{"aw8697_reserved_148.bin"},
	{"aw8697_reserved_149.bin"},
	{"aw8697_reserved_150.bin"},
	{"aw8697_reserved_151.bin"},
	{"aw8697_reserved_152.bin"},
	{"aw8697_reserved_153.bin"},
	{"aw8697_reserved_154.bin"},
	{"aw8697_reserved_155.bin"},
	{"aw8697_reserved_156.bin"},
	{"aw8697_reserved_157.bin"},
	{"aw8697_reserved_158.bin"},
	{"aw8697_reserved_159.bin"},
	{"aw8697_reserved_160.bin"},
	{"aw8697_reserved_161.bin"},
	{"aw8697_reserved_162.bin"},
	{"aw8697_reserved_163.bin"},
	{"aw8697_reserved_164.bin"},
	{"aw8697_reserved_165.bin"},
	{"aw8697_reserved_166.bin"},
	{"ring_McLarn_167.bin"},
	{"notif_Ardour_168.bin"},
	{"notif_Chic_169.bin"},
	{"aw8697_reserved_170.bin"},
	{"Alacrity_166Hz.bin"},
	{"Amenity_166Hz.bin"},
	{"Blues_166Hz.bin"},
	{"Bounce_166Hz.bin"},
	{"Calm_166Hz.bin"},
	{"Cloud_166Hz.bin"},
	{"Cyclotron_166Hz.bin"},
	{"Distinct_166Hz.bin"},
	{"Dynamic_166Hz.bin"},
	{"Echo_166Hz.bin"},
	{"Expect_166Hz.bin"},
	{"Fanatical_166Hz.bin"},
	{"Funky_166Hz.bin"},
	{"ringtone_Guitar_RTP.bin"},
	{"ringtone_Harping_RTP.bin"},
	{"Highlight_166Hz.bin"},
	{"Idyl_166Hz.bin"},
	{"Innocence_166Hz.bin"},
	{"Journey_166Hz.bin"},
	{"Joyous_166Hz.bin"},
	{"Lazy_166Hz.bin"},
	{"Marimba_166Hz.bin"},
	{"Mystical_166Hz.bin"},
	{"Old_telephone_166Hz.bin"},
	{"OnePlus_tune_166Hz.bin"},
	{"OnePlus_tune_rhythm_166Hz.bin"},
	{"Optimistic_166Hz.bin"},
	{"Piano_166Hz.bin"},
	{"Whirl_166Hz.bin"},
	{"aw8697_reserved.bin"},
	{"t-jingle_RTP.bin"},
	{"Eager_166Hz.bin"},
	{"Ebullition_166Hz.bin"},
	{"Friendship_166Hz.bin"},
	{"Jazz_life_166Hz.bin"},
	{"Sun_glittering_166Hz.bin"},
	{"Allay_166Hz.bin"},
	{"Allusion_166Hz.bin"},
	{"Amiable_166Hz.bin"},
	{"Blare_166Hz.bin"},
	{"Blissful_166Hz.bin"},
	{"Brisk_166Hz.bin"},
	{"Bubble_166Hz.bin"},
	{"Cheerful_166Hz.bin"},
	{"Clear_166Hz.bin"},
	{"Comely_166Hz.bin"},
	{"Cozy_166Hz.bin"},
	{"Ding_166Hz.bin"},
	{"Effervesce_166Hz.bin"},
	{"Elegant_166Hz.bin"},
	{"Free_166Hz.bin"},
	{"Hallucination_166Hz.bin"},
	{"Inbound_166Hz.bin"},
	{"Light_166Hz.bin"},
	{"Meet_166Hz.bin"},
	{"Naivety_166Hz.bin"},
	{"Quickly_166Hz.bin"},
	{"Rhythm_166Hz.bin"},
	{"Surprise_166Hz.bin"},
	{"Twinkle_166Hz.bin"},
	{"aw8697_reserved.bin"},
	{"Alarm_clock_166Hz.bin"},
	{"Beep_166Hz.bin"},
	{"Breeze_166Hz.bin"},
	{"Dawn_166Hz.bin"},
	{"Dream_166Hz.bin"},
	{"Fluttering_166Hz.bin"},
	{"Flyer_166Hz.bin"},
	{"Interesting_166Hz.bin"},
	{"Leisurely_166Hz.bin"},
	{"Memory_166Hz.bin"},
	{"Relieved_166Hz.bin"},
	{"Ripple_166Hz.bin"},
	{"Slowly_166Hz.bin"},
	{"Spring_166Hz.bin"},
	{"Stars_166Hz.bin"},
	{"Surging_166Hz.bin"},
	{"Tactfully_166Hz.bin"},
	{"The_wind_166Hz.bin"},
	{"Walking_in_the_rain_166Hz.bin"},
	{"BoHaoPanAnJian.bin"},
	{"BoHaoPanAnNiu.bin"},
	{"BoHaoPanKuaiJie.bin"},
	{"DianHuaGuaDuan.bin"},
	{"DianJinMoShiQieHuan.bin"},
	{"HuaDongTiaoZhenDong.bin"},
	{"LeiShen.bin"},
	{"XuanZhongYouXi.bin"},
	{"YeJianMoShiDaZi.bin"},
	{"YouXiSheZhiKuang.bin"},
	{"ZhuanYeMoShi.bin"},
	{"Climber_166Hz.bin"},
	{"Chase_166Hz.bin"},
	{"shuntai24k_rtp.bin"},
	{"wentai24k_rtp.bin"},
	{"Rock_RTP.bin"},
	{"In_game_ringtone_RTP.bin"},
	{"Wake_up_samurai_RTP.bin"},
	{"In_game_alarm_RTP.bin"},
	{"In_game_sms_RTP.bin"},
	{"Audition_RTP.bin"},
	{"20ms_RTP_166Hz.bin"},
	{"40ms_RTP_166Hz.bin"},
	{"60ms_RTP_166Hz.bin"},
	{"80ms_RTP_166Hz.bin"},
	{"100ms_RTP_166Hz.bin"},
	{"120ms_RTP_166Hz.bin"},
	{"140ms_RTP_166Hz.bin"},
	{"160ms_RTP_166Hz.bin"},
	{"180ms_RTP_166Hz.bin"},
	{"200ms_RTP_166Hz.bin"},
	{"220ms_RTP_166Hz.bin"},
	{"240ms_RTP_166Hz.bin"},
	{"260ms_RTP_166Hz.bin"},
	{"280ms_RTP_166Hz.bin"},
	{"300ms_RTP_166Hz.bin"},
	{"320ms_RTP_166Hz.bin"},
	{"340ms_RTP_166Hz.bin"},
	{"360ms_RTP_166Hz.bin"},
	{"380ms_RTP_166Hz.bin"},
	{"400ms_RTP_166Hz.bin"},
	{"420ms_RTP_166Hz.bin"},
	{"440ms_RTP_166Hz.bin"},
	{"460ms_RTP_166Hz.bin"},
	{"480ms_RTP_166Hz.bin"},
	{"500ms_RTP_166Hz.bin"},
};

static char aw8697_rtp_name_0619_170Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
	{"aw8697_Hearty_channel_RTP_1.bin"},
	{"aw8697_Instant_channel_RTP_2.bin"},
	{"aw8697_Music_channel_RTP_3.bin"},
	{"aw8697_Percussion_channel_RTP_4.bin"},
	{"aw8697_Ripple_channel_RTP_5.bin"},
	{"aw8697_Bright_channel_RTP_6.bin"},
	{"aw8697_Fun_channel_RTP_7.bin"},
	{"aw8697_Glittering_channel_RTP_8.bin"},
	{"aw8697_Granules_channel_RTP_9.bin"},
	{"aw8697_Harp_channel_RTP_10.bin"},
	{"aw8697_Impression_channel_RTP_11.bin"},
	{"aw8697_Ingenious_channel_RTP_12.bin"},
	{"aw8697_Joy_channel_RTP_13.bin"},
	{"aw8697_Overtone_channel_RTP_14.bin"},
	{"aw8697_Receive_channel_RTP_15.bin"},
	{"aw8697_Splash_channel_RTP_16.bin"},
	{"aw8697_About_School_RTP_17.bin"},
	{"aw8697_Bliss_RTP_18.bin"},
	{"aw8697_Childhood_RTP_19.bin"},
	{"aw8697_Commuting_RTP_20.bin"},
	{"aw8697_Dream_RTP_21.bin"},
	{"aw8697_Firefly_RTP_22.bin"},
	{"aw8697_Gathering_RTP_23.bin"},
	{"aw8697_Gaze_RTP_24.bin"},
	{"aw8697_Lakeside_RTP_25.bin"},
	{"aw8697_Lifestyle_RTP_26.bin"},
	{"aw8697_Memories_RTP_27.bin"},
	{"aw8697_Messy_RTP_28.bin"},
	{"aw8697_Night_RTP_29.bin"},
	{"aw8697_Passionate_Dance_RTP_30.bin"},
	{"aw8697_Playground_RTP_31.bin"},
	{"aw8697_Relax_RTP_32.bin"},
	{"aw8697_Reminiscence_RTP_33.bin"},
	{"aw8697_Silence_From_Afar_RTP_34.bin"},
	{"aw8697_Silence_RTP_35.bin"},
	{"aw8697_Stars_RTP_36.bin"},
	{"aw8697_Summer_RTP_37.bin"},
	{"aw8697_Toys_RTP_38.bin"},
	{"aw8697_Travel_RTP_39.bin"},
	{"aw8697_Vision_RTP_40.bin"},
	{"aw8697_waltz_channel_RTP_41_170Hz.bin"},
	{"aw8697_cut_channel_RTP_42_170Hz.bin"},
	{"aw8697_clock_channel_RTP_43_170Hz.bin"},
	{"aw8697_long_sound_channel_RTP_44_170Hz.bin"},
	{"aw8697_short_channel_RTP_45_170Hz.bin"},
	{"aw8697_two_error_remaind_RTP_46_170Hz.bin"},
	{"aw8697_kill_program_170Hz.bin"},
	{"aw8697_Simple_channel_RTP_48.bin"},
	{"aw8697_Pure_RTP_49.bin"},
	{"aw8697_reserved_sound_channel_RTP_50.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},
	{"aw8697_old_steady_test_RTP_52.bin"},
	{"aw8697_listen_pop_53.bin"},
	{"aw8697_desk_54_170Hz.bin"},
	{"aw8697_nfc_55_170Hz.bin"},
	{"aw8697_vibrator_remain_12_170Hz.bin"},
	{"aw8697_notice_13_RTP_57.bin"},
	{"aw8697_third_ring_14_RTP_58.bin"},
	{"aw8697_reserved_59.bin"},
	{"aw8697_honor_fisrt_kill_RTP_60.bin"},
	{"aw8697_honor_two_kill_RTP_61.bin"},
	{"aw8697_honor_three_kill_RTP_62.bin"},
	{"aw8697_honor_four_kill_RTP_63.bin"},
	{"aw8697_honor_five_kill_RTP_64.bin"},
	{"aw8697_honor_three_continu_kill_RTP_65.bin"},
	{"aw8697_honor_four_continu_kill_RTP_66.bin"},
	{"aw8697_honor_unstoppable_RTP_67.bin"},
	{"aw8697_honor_thousands_kill_RTP_68.bin"},
	{"aw8697_honor_lengendary_RTP_69.bin"},
	{"aw8697_Airy_morning_RTP_70.bin"},
	{"aw8697_Temple_morning_RTP_71.bin"},
	{"aw8697_Water_cicidas_72_RTP.bin"},
	{"aw8697_Electro_club_RTP_73.bin"},
	{"aw8697_Vacation_RTP_74.bin"},
	{"aw8697_Jazz_funk_RTP_75.bin"},
	{"aw8697_House_club_RTP_76.bin"},
	{"aw8697_temple_tone_RTP_77.bin"},
	{"aw8697_Jazz_dreamy_RTP_78.bin"},
	{"aw8697_Jazz_modern_RTP_79.bin"},
	{"aw8697_Tone_round_RTP_80.bin"},
	{"aw8697_Digi_rise_RTP_81.bin"},
	{"aw8697_Wood_phone_RTP_82.bin"},
	{"aw8697_Hey_RTP_83.bin"},
	{"aw8697_Zanza_RTP_84.bin"},
	{"aw8697_Info_RTP_85.bin"},
	{"aw8697_Tip_top_RTP_86.bin"},
	{"aw8697_Opop_short_RTP_87.bin"},
	{"aw8697_bowl_bells_RTP_88.bin"},
	{"aw8697_jumpy_RTP_89.bin"},
	{"aw8697_reserved_90.bin"},
	{"aw8697_reserved_91.bin"},
	{"aw8697_reserved_92.bin"},
	{"aw8697_reserved_93.bin"},
	{"Cloudscape_170Hz.bin"},
	{"Good_energy_170Hz.bin"},
	{"Blink_170Hz.bin"},
	{"Whoop_doop_170Hz.bin"},
	{"OnePlus_new_feeling_170Hz.bin"},
	{"Nature_170Hz.bin"},
	{"aw8697_reserved_100.bin"},
	{"aw8697_reserved_101.bin"},
	{"aw8697_reserved_102.bin"},
	{"aw8697_reserved_103.bin"},
	{"aw8697_reserved_104.bin"},
	{"aw8697_stepable_regulate_RTP_105_170Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_170Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_170Hz.bin"},
	{"aw8697_charging_simulation_RTP_108_170Hz.bin"},
	{"aw8697_reserved_109.bin"},
	{"aw8697_fingerprint_effect1_RTP_110_170Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_170Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_170Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_170Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_170Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_170Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_170Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_170Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_170Hz.bin"},
	{"aw8697_reserved_119.bin"},
	{"aw8697_reserved_120.bin"},
	{"aw8697_reserved_121.bin"},
	{"aw8697_voice_assistant_RTP_122_170Hz.bin"},
	{"aw8697_reserved_123.bin"},
	{"aw8697_reserved_124.bin"},
	{"aw8697_reserved_125.bin"},
	{"aw8697_reserved_126.bin"},
	{"aw8697_reserved_127.bin"},
	{"aw8697_reserved_128.bin"},
	{"aw8697_reserved_129.bin"},
	{"aw8697_reserved_130.bin"},
	{"aw8697_reserved_131.bin"},
	{"aw8697_reserved_132.bin"},
	{"aw8697_reserved_133.bin"},
	{"aw8697_reserved_134.bin"},
	{"aw8697_reserved_135.bin"},
	{"aw8697_reserved_136.bin"},
	{"aw8697_reserved_137.bin"},
	{"aw8697_reserved_138.bin"},
	{"aw8697_reserved_139.bin"},
	{"aw8697_reserved_140.bin"},
	{"aw8697_reserved_141.bin"},
	{"aw8697_reserved_142.bin"},
	{"aw8697_reserved_143.bin"},
	{"aw8697_reserved_144.bin"},
	{"aw8697_reserved_145.bin"},
	{"aw8697_reserved_146.bin"},
	{"aw8697_reserved_147.bin"},
	{"aw8697_reserved_148.bin"},
	{"aw8697_reserved_149.bin"},
	{"aw8697_reserved_150.bin"},
	{"aw8697_reserved_151.bin"},
	{"aw8697_reserved_152.bin"},
	{"aw8697_reserved_153.bin"},
	{"aw8697_reserved_154.bin"},
	{"aw8697_reserved_155.bin"},
	{"aw8697_reserved_156.bin"},
	{"aw8697_reserved_157.bin"},
	{"aw8697_reserved_158.bin"},
	{"aw8697_reserved_159.bin"},
	{"aw8697_reserved_160.bin"},
	{"aw8697_reserved_161.bin"},
	{"aw8697_reserved_162.bin"},
	{"aw8697_reserved_163.bin"},
	{"aw8697_reserved_164.bin"},
	{"aw8697_reserved_165.bin"},
	{"aw8697_reserved_166.bin"},
	{"ring_McLarn_167.bin"},
	{"notif_Ardour_168.bin"},
	{"notif_Chic_169.bin"},
	{"aw8697_reserved_170.bin"},
	{"Alacrity_170Hz.bin"},
	{"Amenity_170Hz.bin"},
	{"Blues_170Hz.bin"},
	{"Bounce_170Hz.bin"},
	{"Calm_170Hz.bin"},
	{"Cloud_170Hz.bin"},
	{"Cyclotron_170Hz.bin"},
	{"Distinct_170Hz.bin"},
	{"Dynamic_170Hz.bin"},
	{"Echo_170Hz.bin"},
	{"Expect_170Hz.bin"},
	{"Fanatical_170Hz.bin"},
	{"Funky_170Hz.bin"},
	{"ringtone_Guitar_RTP.bin"},
	{"ringtone_Harping_RTP.bin"},
	{"Highlight_170Hz.bin"},
	{"Idyl_170Hz.bin"},
	{"Innocence_170Hz.bin"},
	{"Journey_170Hz.bin"},
	{"Joyous_170Hz.bin"},
	{"Lazy_170Hz.bin"},
	{"Marimba_170Hz.bin"},
	{"Mystical_170Hz.bin"},
	{"Old_telephone_170Hz.bin"},
	{"OnePlus_tune_170Hz.bin"},
	{"OnePlus_tune_rhythm_170Hz.bin"},
	{"Optimistic_170Hz.bin"},
	{"Piano_170Hz.bin"},
	{"Whirl_170Hz.bin"},
	{"aw8697_reserved.bin"},
	{"t-jingle_RTP.bin"},
	{"Eager_170Hz.bin"},
	{"Ebullition_170Hz.bin"},
	{"Friendship_170Hz.bin"},
	{"Jazz_life_170Hz.bin"},
	{"Sun_glittering_170Hz.bin"},
	{"Allay_170Hz.bin"},
	{"Allusion_170Hz.bin"},
	{"Amiable_170Hz.bin"},
	{"Blare_170Hz.bin"},
	{"Blissful_170Hz.bin"},
	{"Brisk_170Hz.bin"},
	{"Bubble_170Hz.bin"},
	{"Cheerful_170Hz.bin"},
	{"Clear_170Hz.bin"},
	{"Comely_170Hz.bin"},
	{"Cozy_170Hz.bin"},
	{"Ding_170Hz.bin"},
	{"Effervesce_170Hz.bin"},
	{"Elegant_170Hz.bin"},
	{"Free_170Hz.bin"},
	{"Hallucination_170Hz.bin"},
	{"Inbound_170Hz.bin"},
	{"Light_170Hz.bin"},
	{"Meet_170Hz.bin"},
	{"Naivety_170Hz.bin"},
	{"Quickly_170Hz.bin"},
	{"Rhythm_170Hz.bin"},
	{"Surprise_170Hz.bin"},
	{"Twinkle_170Hz.bin"},
	{"aw8697_reserved.bin"},
	{"Alarm_clock_170Hz.bin"},
	{"Beep_170Hz.bin"},
	{"Breeze_170Hz.bin"},
	{"Dawn_170Hz.bin"},
	{"Dream_170Hz.bin"},
	{"Fluttering_170Hz.bin"},
	{"Flyer_170Hz.bin"},
	{"Interesting_170Hz.bin"},
	{"Leisurely_170Hz.bin"},
	{"Memory_170Hz.bin"},
	{"Relieved_170Hz.bin"},
	{"Ripple_170Hz.bin"},
	{"Slowly_170Hz.bin"},
	{"Spring_170Hz.bin"},
	{"Stars_170Hz.bin"},
	{"Surging_170Hz.bin"},
	{"Tactfully_170Hz.bin"},
	{"The_wind_170Hz.bin"},
	{"Walking_in_the_rain_170Hz.bin"},
	{"BoHaoPanAnJian.bin"},
	{"BoHaoPanAnNiu.bin"},
	{"BoHaoPanKuaiJie.bin"},
	{"DianHuaGuaDuan.bin"},
	{"DianJinMoShiQieHuan.bin"},
	{"HuaDongTiaoZhenDong.bin"},
	{"LeiShen.bin"},
	{"XuanZhongYouXi.bin"},
	{"YeJianMoShiDaZi.bin"},
	{"YouXiSheZhiKuang.bin"},
	{"ZhuanYeMoShi.bin"},
	{"Climber_170Hz.bin"},
	{"Chase_170Hz.bin"},
	{"shuntai24k_rtp.bin"},
	{"wentai24k_rtp.bin"},
	{"Rock_RTP.bin"},
	{"In_game_ringtone_RTP.bin"},
	{"Wake_up_samurai_RTP.bin"},
	{"In_game_alarm_RTP.bin"},
	{"In_game_sms_RTP.bin"},
	{"Audition_RTP.bin"},
	{"20ms_RTP_170Hz.bin"},
	{"40ms_RTP_170Hz.bin"},
	{"60ms_RTP_170Hz.bin"},
	{"80ms_RTP_170Hz.bin"},
	{"100ms_RTP_170Hz.bin"},
	{"120ms_RTP_170Hz.bin"},
	{"140ms_RTP_170Hz.bin"},
	{"160ms_RTP_170Hz.bin"},
	{"180ms_RTP_170Hz.bin"},
	{"200ms_RTP_170Hz.bin"},
	{"220ms_RTP_170Hz.bin"},
	{"240ms_RTP_170Hz.bin"},
	{"260ms_RTP_170Hz.bin"},
	{"280ms_RTP_170Hz.bin"},
	{"300ms_RTP_170Hz.bin"},
	{"320ms_RTP_170Hz.bin"},
	{"340ms_RTP_170Hz.bin"},
	{"360ms_RTP_170Hz.bin"},
	{"380ms_RTP_170Hz.bin"},
	{"400ms_RTP_170Hz.bin"},
	{"420ms_RTP_170Hz.bin"},
	{"440ms_RTP_170Hz.bin"},
	{"460ms_RTP_170Hz.bin"},
	{"480ms_RTP_170Hz.bin"},
	{"500ms_RTP_170Hz.bin"},
};

static char aw8697_rtp_name_0619_174Hz[][AW8697_RTP_NAME_MAX] = {
	{"aw8697_rtp.bin"},
	{"aw8697_Hearty_channel_RTP_1.bin"},
	{"aw8697_Instant_channel_RTP_2.bin"},
	{"aw8697_Music_channel_RTP_3.bin"},
	{"aw8697_Percussion_channel_RTP_4.bin"},
	{"aw8697_Ripple_channel_RTP_5.bin"},
	{"aw8697_Bright_channel_RTP_6.bin"},
	{"aw8697_Fun_channel_RTP_7.bin"},
	{"aw8697_Glittering_channel_RTP_8.bin"},
	{"aw8697_Granules_channel_RTP_9.bin"},
	{"aw8697_Harp_channel_RTP_10.bin"},
	{"aw8697_Impression_channel_RTP_11.bin"},
	{"aw8697_Ingenious_channel_RTP_12.bin"},
	{"aw8697_Joy_channel_RTP_13.bin"},
	{"aw8697_Overtone_channel_RTP_14.bin"},
	{"aw8697_Receive_channel_RTP_15.bin"},
	{"aw8697_Splash_channel_RTP_16.bin"},
	{"aw8697_About_School_RTP_17.bin"},
	{"aw8697_Bliss_RTP_18.bin"},
	{"aw8697_Childhood_RTP_19.bin"},
	{"aw8697_Commuting_RTP_20.bin"},
	{"aw8697_Dream_RTP_21.bin"},
	{"aw8697_Firefly_RTP_22.bin"},
	{"aw8697_Gathering_RTP_23.bin"},
	{"aw8697_Gaze_RTP_24.bin"},
	{"aw8697_Lakeside_RTP_25.bin"},
	{"aw8697_Lifestyle_RTP_26.bin"},
	{"aw8697_Memories_RTP_27.bin"},
	{"aw8697_Messy_RTP_28.bin"},
	{"aw8697_Night_RTP_29.bin"},
	{"aw8697_Passionate_Dance_RTP_30.bin"},
	{"aw8697_Playground_RTP_31.bin"},
	{"aw8697_Relax_RTP_32.bin"},
	{"aw8697_Reminiscence_RTP_33.bin"},
	{"aw8697_Silence_From_Afar_RTP_34.bin"},
	{"aw8697_Silence_RTP_35.bin"},
	{"aw8697_Stars_RTP_36.bin"},
	{"aw8697_Summer_RTP_37.bin"},
	{"aw8697_Toys_RTP_38.bin"},
	{"aw8697_Travel_RTP_39.bin"},
	{"aw8697_Vision_RTP_40.bin"},
	{"aw8697_waltz_channel_RTP_41_174Hz.bin"},
	{"aw8697_cut_channel_RTP_42_174Hz.bin"},
	{"aw8697_clock_channel_RTP_43_174Hz.bin"},
	{"aw8697_long_sound_channel_RTP_44_174Hz.bin"},
	{"aw8697_short_channel_RTP_45_174Hz.bin"},
	{"aw8697_two_error_remaind_RTP_46_174Hz.bin"},
	{"aw8697_kill_program_174Hz.bin"},
	{"aw8697_Simple_channel_RTP_48.bin"},
	{"aw8697_Pure_RTP_49.bin"},
	{"aw8697_reserved_sound_channel_RTP_50.bin"},
	{"aw8697_high_temp_high_humidity_channel_RTP_51.bin"},
	{"aw8697_old_steady_test_RTP_52.bin"},
	{"aw8697_listen_pop_53.bin"},
	{"aw8697_desk_54_174Hz.bin"},
	{"aw8697_nfc_55_174Hz.bin"},
	{"aw8697_vibrator_remain_12_174Hz.bin"},
	{"aw8697_notice_13_RTP_57.bin"},
	{"aw8697_third_ring_14_RTP_58.bin"},
	{"aw8697_reserved_59.bin"},
	{"aw8697_honor_fisrt_kill_RTP_60.bin"},
	{"aw8697_honor_two_kill_RTP_61.bin"},
	{"aw8697_honor_three_kill_RTP_62.bin"},
	{"aw8697_honor_four_kill_RTP_63.bin"},
	{"aw8697_honor_five_kill_RTP_64.bin"},
	{"aw8697_honor_three_continu_kill_RTP_65.bin"},
	{"aw8697_honor_four_continu_kill_RTP_66.bin"},
	{"aw8697_honor_unstoppable_RTP_67.bin"},
	{"aw8697_honor_thousands_kill_RTP_68.bin"},
	{"aw8697_honor_lengendary_RTP_69.bin"},
	{"aw8697_Airy_morning_RTP_70.bin"},
	{"aw8697_Temple_morning_RTP_71.bin"},
	{"aw8697_Water_cicidas_72_RTP.bin"},
	{"aw8697_Electro_club_RTP_73.bin"},
	{"aw8697_Vacation_RTP_74.bin"},
	{"aw8697_Jazz_funk_RTP_75.bin"},
	{"aw8697_House_club_RTP_76.bin"},
	{"aw8697_temple_tone_RTP_77.bin"},
	{"aw8697_Jazz_dreamy_RTP_78.bin"},
	{"aw8697_Jazz_modern_RTP_79.bin"},
	{"aw8697_Tone_round_RTP_80.bin"},
	{"aw8697_Digi_rise_RTP_81.bin"},
	{"aw8697_Wood_phone_RTP_82.bin"},
	{"aw8697_Hey_RTP_83.bin"},
	{"aw8697_Zanza_RTP_84.bin"},
	{"aw8697_Info_RTP_85.bin"},
	{"aw8697_Tip_top_RTP_86.bin"},
	{"aw8697_Opop_short_RTP_87.bin"},
	{"aw8697_bowl_bells_RTP_88.bin"},
	{"aw8697_jumpy_RTP_89.bin"},
	{"aw8697_reserved_90.bin"},
	{"aw8697_reserved_91.bin"},
	{"aw8697_reserved_92.bin"},
	{"aw8697_reserved_93.bin"},
	{"Cloudscape_174Hz.bin"},
	{"Good_energy_174Hz.bin"},
	{"Blink_174Hz.bin"},
	{"Whoop_doop_174Hz.bin"},
	{"OnePlus_new_feeling_174Hz.bin"},
	{"Nature_174Hz.bin"},
	{"aw8697_reserved_100.bin"},
	{"aw8697_reserved_101.bin"},
	{"aw8697_reserved_102.bin"},
	{"aw8697_reserved_103.bin"},
	{"aw8697_reserved_104.bin"},
	{"aw8697_stepable_regulate_RTP_105_174Hz.bin"},
	{"aw8697_voice_level_bar_edge_RTP_106_174Hz.bin"},
	{"aw8697_strength_level_bar_edge_RTP_107_174Hz.bin"},
	{"aw8697_charging_simulation_RTP_108_174Hz.bin"},
	{"aw8697_reserved_109.bin"},
	{"aw8697_fingerprint_effect1_RTP_110_174Hz.bin"},
	{"aw8697_fingerprint_effect2_RTP_111_174Hz.bin"},
	{"aw8697_fingerprint_effect3_RTP_112_174Hz.bin"},
	{"aw8697_fingerprint_effect4_RTP_113_174Hz.bin"},
	{"aw8697_fingerprint_effect5_RTP_114_174Hz.bin"},
	{"aw8697_fingerprint_effect6_RTP_115_174Hz.bin"},
	{"aw8697_fingerprint_effect7_RTP_116_174Hz.bin"},
	{"aw8697_fingerprint_effect8_RTP_117_174Hz.bin"},
	{"aw8697_breath_simulation_RTP_118_174Hz.bin"},
	{"aw8697_reserved_119.bin"},
	{"aw8697_reserved_120.bin"},
	{"aw8697_reserved_121.bin"},
	{"aw8697_voice_assistant_RTP_122_174Hz.bin"},
	{"aw8697_reserved_123.bin"},
	{"aw8697_reserved_124.bin"},
	{"aw8697_reserved_125.bin"},
	{"aw8697_reserved_126.bin"},
	{"aw8697_reserved_127.bin"},
	{"aw8697_reserved_128.bin"},
	{"aw8697_reserved_129.bin"},
	{"aw8697_reserved_130.bin"},
	{"aw8697_reserved_131.bin"},
	{"aw8697_reserved_132.bin"},
	{"aw8697_reserved_133.bin"},
	{"aw8697_reserved_134.bin"},
	{"aw8697_reserved_135.bin"},
	{"aw8697_reserved_136.bin"},
	{"aw8697_reserved_137.bin"},
	{"aw8697_reserved_138.bin"},
	{"aw8697_reserved_139.bin"},
	{"aw8697_reserved_140.bin"},
	{"aw8697_reserved_141.bin"},
	{"aw8697_reserved_142.bin"},
	{"aw8697_reserved_143.bin"},
	{"aw8697_reserved_144.bin"},
	{"aw8697_reserved_145.bin"},
	{"aw8697_reserved_146.bin"},
	{"aw8697_reserved_147.bin"},
	{"aw8697_reserved_148.bin"},
	{"aw8697_reserved_149.bin"},
	{"aw8697_reserved_150.bin"},
	{"aw8697_reserved_151.bin"},
	{"aw8697_reserved_152.bin"},
	{"aw8697_reserved_153.bin"},
	{"aw8697_reserved_154.bin"},
	{"aw8697_reserved_155.bin"},
	{"aw8697_reserved_156.bin"},
	{"aw8697_reserved_157.bin"},
	{"aw8697_reserved_158.bin"},
	{"aw8697_reserved_159.bin"},
	{"aw8697_reserved_160.bin"},
	{"aw8697_reserved_161.bin"},
	{"aw8697_reserved_162.bin"},
	{"aw8697_reserved_163.bin"},
	{"aw8697_reserved_164.bin"},
	{"aw8697_reserved_165.bin"},
	{"aw8697_reserved_166.bin"},
	{"ring_McLarn_167.bin"},
	{"notif_Ardour_168.bin"},
	{"notif_Chic_169.bin"},
	{"aw8697_reserved_170.bin"},
	{"Alacrity_174Hz.bin"},
	{"Amenity_174Hz.bin"},
	{"Blues_174Hz.bin"},
	{"Bounce_174Hz.bin"},
	{"Calm_174Hz.bin"},
	{"Cloud_174Hz.bin"},
	{"Cyclotron_174Hz.bin"},
	{"Distinct_174Hz.bin"},
	{"Dynamic_174Hz.bin"},
	{"Echo_174Hz.bin"},
	{"Expect_174Hz.bin"},
	{"Fanatical_174Hz.bin"},
	{"Funky_174Hz.bin"},
	{"ringtone_Guitar_RTP.bin"},
	{"ringtone_Harping_RTP.bin"},
	{"Highlight_174Hz.bin"},
	{"Idyl_174Hz.bin"},
	{"Innocence_174Hz.bin"},
	{"Journey_174Hz.bin"},
	{"Joyous_174Hz.bin"},
	{"Lazy_174Hz.bin"},
	{"Marimba_174Hz.bin"},
	{"Mystical_174Hz.bin"},
	{"Old_telephone_174Hz.bin"},
	{"OnePlus_tune_174Hz.bin"},
	{"OnePlus_tune_rhythm_174Hz.bin"},
	{"Optimistic_174Hz.bin"},
	{"Piano_174Hz.bin"},
	{"Whirl_174Hz.bin"},
	{"aw8697_reserved.bin"},
	{"t-jingle_RTP.bin"},
	{"Eager_174Hz.bin"},
	{"Ebullition_174Hz.bin"},
	{"Friendship_174Hz.bin"},
	{"Jazz_life_174Hz.bin"},
	{"Sun_glittering_174Hz.bin"},
	{"Allay_174Hz.bin"},
	{"Allusion_174Hz.bin"},
	{"Amiable_174Hz.bin"},
	{"Blare_174Hz.bin"},
	{"Blissful_174Hz.bin"},
	{"Brisk_174Hz.bin"},
	{"Bubble_174Hz.bin"},
	{"Cheerful_174Hz.bin"},
	{"Clear_174Hz.bin"},
	{"Comely_174Hz.bin"},
	{"Cozy_174Hz.bin"},
	{"Ding_174Hz.bin"},
	{"Effervesce_174Hz.bin"},
	{"Elegant_174Hz.bin"},
	{"Free_174Hz.bin"},
	{"Hallucination_174Hz.bin"},
	{"Inbound_174Hz.bin"},
	{"Light_174Hz.bin"},
	{"Meet_174Hz.bin"},
	{"Naivety_174Hz.bin"},
	{"Quickly_174Hz.bin"},
	{"Rhythm_174Hz.bin"},
	{"Surprise_174Hz.bin"},
	{"Twinkle_174Hz.bin"},
	{"aw8697_reserved.bin"},
	{"Alarm_clock_174Hz.bin"},
	{"Beep_174Hz.bin"},
	{"Breeze_174Hz.bin"},
	{"Dawn_174Hz.bin"},
	{"Dream_174Hz.bin"},
	{"Fluttering_174Hz.bin"},
	{"Flyer_174Hz.bin"},
	{"Interesting_174Hz.bin"},
	{"Leisurely_174Hz.bin"},
	{"Memory_174Hz.bin"},
	{"Relieved_174Hz.bin"},
	{"Ripple_174Hz.bin"},
	{"Slowly_174Hz.bin"},
	{"Spring_174Hz.bin"},
	{"Stars_174Hz.bin"},
	{"Surging_174Hz.bin"},
	{"Tactfully_174Hz.bin"},
	{"The_wind_174Hz.bin"},
	{"Walking_in_the_rain_174Hz.bin"},
	{"BoHaoPanAnJian.bin"},
	{"BoHaoPanAnNiu.bin"},
	{"BoHaoPanKuaiJie.bin"},
	{"DianHuaGuaDuan.bin"},
	{"DianJinMoShiQieHuan.bin"},
	{"HuaDongTiaoZhenDong.bin"},
	{"LeiShen.bin"},
	{"XuanZhongYouXi.bin"},
	{"YeJianMoShiDaZi.bin"},
	{"YouXiSheZhiKuang.bin"},
	{"ZhuanYeMoShi.bin"},
	{"Climber_174Hz.bin"},
	{"Chase_174Hz.bin"},
	{"shuntai24k_rtp.bin"},
	{"wentai24k_rtp.bin"},
	{"Rock_RTP.bin"},
	{"In_game_ringtone_RTP.bin"},
	{"Wake_up_samurai_RTP.bin"},
	{"In_game_alarm_RTP.bin"},
	{"In_game_sms_RTP.bin"},
	{"Audition_RTP.bin"},
	{"20ms_RTP_174Hz.bin"},
	{"40ms_RTP_174Hz.bin"},
	{"60ms_RTP_174Hz.bin"},
	{"80ms_RTP_174Hz.bin"},
	{"100ms_RTP_174Hz.bin"},
	{"120ms_RTP_174Hz.bin"},
	{"140ms_RTP_174Hz.bin"},
	{"160ms_RTP_174Hz.bin"},
	{"180ms_RTP_174Hz.bin"},
	{"200ms_RTP_174Hz.bin"},
	{"220ms_RTP_174Hz.bin"},
	{"240ms_RTP_174Hz.bin"},
	{"260ms_RTP_174Hz.bin"},
	{"280ms_RTP_174Hz.bin"},
	{"300ms_RTP_174Hz.bin"},
	{"320ms_RTP_174Hz.bin"},
	{"340ms_RTP_174Hz.bin"},
	{"360ms_RTP_174Hz.bin"},
	{"380ms_RTP_174Hz.bin"},
	{"400ms_RTP_174Hz.bin"},
	{"420ms_RTP_174Hz.bin"},
	{"440ms_RTP_174Hz.bin"},
	{"460ms_RTP_174Hz.bin"},
	{"480ms_RTP_174Hz.bin"},
	{"500ms_RTP_174Hz.bin"},
};

#endif

struct aw8697_container *aw8697_rtp = NULL;
struct aw8697 *g_aw8697;
#define  AW8697_CONTAINER_DEFAULT_SIZE  (2 * 1024 * 1024)   ////  2M
int aw8697_container_size = AW8697_CONTAINER_DEFAULT_SIZE;

static int aw8697_container_init(int size)
{
	if (!aw8697_rtp || size > aw8697_container_size) {
		if (aw8697_rtp) {
			vfree(aw8697_rtp);
			aw8697_rtp = NULL;
		}
		aw8697_rtp = vmalloc(size);
		if (!aw8697_rtp) {
			pr_err("%s: error allocating memory\n", __func__);
			return -1;
		}
		aw8697_container_size = size;
	}

	memset(aw8697_rtp, 0, size);

	return 0;
}

/******************************************************
 *
 * functions
 *
 ******************************************************/
static void aw8697_interrupt_clear(struct aw8697 *aw8697);
static int aw8697_haptic_trig_enable_config(struct aw8697 *aw8697);
static int aw8697_haptic_juge_RTP_is_going_on(struct aw8697 *aw8697);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0) || LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
__weak  void do_gettimeofday(struct timeval *tv)
{
    struct timespec64 now;

     ktime_get_real_ts64(&now);
     tv->tv_sec = now.tv_sec;
     tv->tv_usec = now.tv_nsec/1000;
}
#endif
 /******************************************************
 *
 * aw8697 i2c write/read
 *
 ******************************************************/
static int aw8697_i2c_write(struct aw8697 *aw8697,
         unsigned char reg_addr, unsigned char reg_data)
{
    int ret = -1;
    unsigned char cnt = 0;

    if (reg_addr == 0x04 || reg_addr == 0x05) {
        pr_debug("%s: reg_addr_0x%02x = 0x%02x\n", __func__, reg_addr, reg_data);
    }
    while(cnt < AW_I2C_RETRIES) {
        ret = i2c_smbus_write_byte_data(aw8697->i2c, reg_addr, reg_data);
        if(ret < 0) {
            pr_err("%s: i2c_write cnt=%d error=%d\n", __func__, cnt, ret);
        } else {
            break;
        }
        cnt ++;
        msleep(AW_I2C_RETRY_DELAY);
    }

    return ret;
}

static int aw8697_i2c_read(struct aw8697 *aw8697,
        unsigned char reg_addr, unsigned char *reg_data)
{
    int ret = -1;
    unsigned char cnt = 0;

    while(cnt < AW_I2C_RETRIES) {
        ret = i2c_smbus_read_byte_data(aw8697->i2c, reg_addr);
        if(ret < 0) {
            pr_err("%s: i2c_read cnt=%d error=%d\n", __func__, cnt, ret);
        } else {
            *reg_data = ret;
            break;
        }
        cnt ++;
        msleep(AW_I2C_RETRY_DELAY);
    }

    return ret;
}

static int aw8697_i2c_write_bits(struct aw8697 *aw8697,
         unsigned char reg_addr, unsigned int mask, unsigned char reg_data)
{
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, reg_addr, &reg_val);
    reg_val &= mask;
    reg_val |= reg_data;
    aw8697_i2c_write(aw8697, reg_addr, reg_val);

    return 0;
}

static int aw8697_i2c_writes(struct aw8697 *aw8697,
        unsigned char reg_addr, unsigned char *buf, unsigned int len)
{
    int ret = -1;
    unsigned char *data;

    data = kmalloc(len+1, GFP_KERNEL);
    if (data == NULL) {
        pr_err("%s: can not allocate memory\n", __func__);
        return  -ENOMEM;
    }

    data[0] = reg_addr;
    memcpy(&data[1], buf, len);

    ret = i2c_master_send(aw8697->i2c, data, len+1);
    if (ret < 0) {
        pr_err("%s: i2c master send error\n", __func__);
    }

    kfree(data);

    return ret;
}

static int aw8697_i2c_reads(struct aw8697 *aw8697,
        unsigned char reg_addr, unsigned char *buf, unsigned int len)
{
    int ret = -1;
	unsigned char cmd[1] = {reg_addr};
	ret = i2c_master_send(aw8697->i2c, cmd, 1);
	if (ret < 0) {
        pr_err("%s: i2c master send error\n", __func__);
        return ret;
    }

    ret = i2c_master_recv(aw8697->i2c, buf, len);
    if (ret != len) {
        pr_err("%s: couldn't read registers, return %d bytes\n",
            __func__,  ret);
        return ret;
    }

    return ret;
}

/*****************************************************
 *
 * ram update
 *
 *****************************************************/
static void aw8697_rtp_loaded(const struct firmware *cont, void *context)
{
    struct aw8697 *aw8697 = context;
    int ret = 0;
    pr_info("%s enter\n", __func__);

    if (!cont) {
        pr_err("%s: failed to read %s\n", __func__, aw8697_rtp_name[aw8697->rtp_file_num]);
        release_firmware(cont);
        return;
    }

    pr_info("%s: loaded %s - size: %zu\n", __func__, aw8697_rtp_name[aw8697->rtp_file_num],
                    cont ? cont->size : 0);

    /* aw8697 rtp update */
    mutex_lock(&aw8697->rtp_lock);
    #ifndef OPLUS_FEATURE_CHG_BASIC
    aw8697_rtp = kzalloc(cont->size+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        release_firmware(cont);
        mutex_unlock(&aw8697->rtp_lock);
        pr_err("%s: Error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(cont->size+sizeof(int));
    if (ret < 0) {
        release_firmware(cont);
        mutex_unlock(&aw8697->rtp_lock);
        pr_err("%s: Error allocating memory\n", __func__);
        return;
    }
    #endif
    aw8697_rtp->len = cont->size;
    pr_info("%s: rtp size = %d\n", __func__, aw8697_rtp->len);
    memcpy(aw8697_rtp->data, cont->data, cont->size);
    release_firmware(cont);
    mutex_unlock(&aw8697->rtp_lock);

    aw8697->rtp_init = 1;
    pr_info("%s: rtp update complete\n", __func__);
}

static int aw8697_rtp_update(struct aw8697 *aw8697)
{
    pr_info("%s enter\n", __func__);

    return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                aw8697_rtp_name[aw8697->rtp_file_num], aw8697->dev, GFP_KERNEL,
                aw8697, aw8697_rtp_loaded);
}


 static void aw8697_container_update(struct aw8697 *aw8697,
        struct aw8697_container *aw8697_cont)
{
    int i = 0;
    unsigned int shift = 0;

    pr_info("%s enter\n", __func__);

    mutex_lock(&aw8697->lock);

    aw8697->ram.baseaddr_shift = 2;
    aw8697->ram.ram_shift = 4;

    /* RAMINIT Enable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);

    /* base addr */
    shift = aw8697->ram.baseaddr_shift;
    aw8697->ram.base_addr = (unsigned int)((aw8697_cont->data[0+shift]<<8) |
            (aw8697_cont->data[1+shift]));
    pr_info("%s: base_addr=0x%4x\n", __func__, aw8697->ram.base_addr);

    aw8697_i2c_write(aw8697, AW8697_REG_BASE_ADDRH, aw8697_cont->data[0+shift]);
    aw8697_i2c_write(aw8697, AW8697_REG_BASE_ADDRL, aw8697_cont->data[1+shift]);

    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AEH,
                    (unsigned char)((aw8697->ram.base_addr>>1)>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AEL,
                    (unsigned char)((aw8697->ram.base_addr>>1)&0x00FF));
    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AFH,
                    (unsigned char)((aw8697->ram.base_addr-(aw8697->ram.base_addr>>2))>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_FIFO_AFL,
                    (unsigned char)((aw8697->ram.base_addr-(aw8697->ram.base_addr>>2))&0x00FF));

    /* ram */
    shift = aw8697->ram.baseaddr_shift;
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, aw8697_cont->data[0+shift]);
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, aw8697_cont->data[1+shift]);
    shift = aw8697->ram.ram_shift;
	i = aw8697->ram.ram_shift;
	while(i < aw8697_cont->len) {
		if (aw8697_cont->len-i < 2048) {
			aw8697_i2c_writes(aw8697, AW8697_REG_RAMDATA, &aw8697_cont->data[i], aw8697_cont->len-i);
			break;
		}
		aw8697_i2c_writes(aw8697, AW8697_REG_RAMDATA, &aw8697_cont->data[i], 2048);
		i += 2048;
	}

#if 0
    /* ram check */
    shift = aw8697->ram.baseaddr_shift;
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, aw8697_cont->data[0+shift]);
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, aw8697_cont->data[1+shift]);
    shift = aw8697->ram.ram_shift;
    for(i=shift; i<aw8697_cont->len; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_RAMDATA, &reg_val);
        if(reg_val != aw8697_cont->data[i]) {
            pr_err("%s: ram check error addr=0x%04x, file_data=0x%02x, ram_data=0x%02x\n",
                        __func__, i, aw8697_cont->data[i], reg_val);
            return;
        }
    }
#endif

    /* RAMINIT Disable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);

    mutex_unlock(&aw8697->lock);

    pr_info("%s exit\n", __func__);
}


static void aw8697_ram_loaded(const struct firmware *cont, void *context)
{
    struct aw8697 *aw8697 = context;
    struct aw8697_container *aw8697_fw;
    int i = 0;
    unsigned short check_sum = 0;

    pr_info("%s enter\n", __func__);

    if (!cont) {
        pr_err("%s: failed to read %s\n", __func__, aw8697_ram_name);
        release_firmware(cont);
        return;
    }

    //pr_info("%s: loaded %s - size: %zu\n", __func__, aw8697_ram_name,
    ///                cont ? cont->size : 0);
/*
    for(i=0; i<cont->size; i++) {
        pr_info("%s: addr:0x%04x, data:0x%02x\n", __func__, i, *(cont->data+i));
    }
*/

    /* check sum */
    for(i=2; i<cont->size; i++) {
        check_sum += cont->data[i];
    }
    if(check_sum != (unsigned short)((cont->data[0]<<8)|(cont->data[1]))) {
        pr_err("%s: check sum err: check_sum=0x%04x\n", __func__, check_sum);
        return;
    } else {
        pr_info("%s: check sum pass : 0x%04x\n", __func__, check_sum);
        aw8697->ram.check_sum = check_sum;
    }

    /* aw8697 ram update */
    aw8697_fw = vmalloc(cont->size+sizeof(int));
    if (!aw8697_fw) {
        release_firmware(cont);
        pr_err("%s: Error allocating memory\n", __func__);
        return;
    }
    memset(aw8697_fw, 0, cont->size+sizeof(int));
    aw8697_fw->len = cont->size;
    memcpy(aw8697_fw->data, cont->data, cont->size);
    release_firmware(cont);

    aw8697_container_update(aw8697, aw8697_fw);

    aw8697->ram.len = aw8697_fw->len;

    vfree(aw8697_fw);

    aw8697->ram_init = 1;
    pr_info("%s: fw update complete\n", __func__);

    aw8697_haptic_trig_enable_config(aw8697);

    aw8697_rtp_update(aw8697);
}

static int aw8697_ram_update(struct aw8697 *aw8697)
{
    unsigned char index = 0;
    aw8697->ram_init = 0;
    aw8697->rtp_init = 0;

    //f0 165 166,166
    // 167 168, 168
    //169 170 ,170
    //171 172, 172
    //173 174,174
    //>175 ,174
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if (aw8697->device_id == 815 || aw8697->device_id == 1815 || aw8697->device_id == 9595) {
#else
	if (aw8697->device_id == 815 || aw8697->device_id == 9595) {
#endif
		if (aw8697->f0 < F0_VAL_MIN_0815 ||
		    aw8697->f0 > F0_VAL_MAX_0815) {
			aw8697->f0 = 1700;
		}
	} else if (aw8697->device_id == 81538) {
		if (aw8697->f0 < F0_VAL_MIN_081538 || aw8697->f0 > F0_VAL_MAX_081538) {
			aw8697->f0 = 1500;
		}
	} else if (aw8697->device_id == 619) {
		if (aw8697->f0 < F0_VAL_MIN_0619 || aw8697->f0 > F0_VAL_MAX_0619) {
			aw8697->f0 = 1700;
		}
	} else if (aw8697->device_id == 1040) {
		if (aw8697->f0 < F0_VAL_MIN_1040 || aw8697->f0 > F0_VAL_MAX_1040) {
			aw8697->f0 = 1700;
		}
	} else if (aw8697->device_id == 832) {
		if (aw8697->f0 < F0_VAL_MIN_0832 || aw8697->f0 > F0_VAL_MAX_0832) {
			aw8697->f0 = 2350;
		}
	} else {
		if (aw8697->f0 < F0_VAL_MIN_0833 || aw8697->f0 > F0_VAL_MAX_0833) {
		aw8697->f0 = 2350;
		}
	}
    aw8697->haptic_real_f0 = (aw8697->f0 / 10);// get f0 from nvram
    pr_err("%s: aw8697->haptic_real_f0 [%d]\n", __func__, aw8697->haptic_real_f0);
/*
    if (aw8697->haptic_real_f0 <167)
    {
        index = 0;
    }
    else if(aw8697->haptic_real_f0 <169)
    {
        index = 1;
    }
    else if(aw8697->haptic_real_f0 <171)
    {
        index = 2;
    }
    else if(aw8697->haptic_real_f0 <173)
    {
        index = 3;
    }
    else
    {
        index = 4;
    }
*/
	if (aw8697->device_id == 9595) {
		if (aw8697->haptic_real_f0 < 167) {
			index = 0;
		} else if (aw8697->haptic_real_f0 < 169) {
			index = 1;
		} else if (aw8697->haptic_real_f0 < 171) {
			index = 2;
		} else if (aw8697->haptic_real_f0 < 173) {
			index = 3;
		} else {
			index = 4;
		}
	}

#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if (aw8697->device_id == 1815) {
		if (aw8697->haptic_real_f0 < 167) {
			index = 0;
		} else if (aw8697->haptic_real_f0 < 169) {
			index = 1;
		} else if (aw8697->haptic_real_f0 < 171) {
			index = 2;
		} else if (aw8697->haptic_real_f0 < 173) {
			index = 3;
		} else {
			index = 4;
		}
	}

#ifdef CONFIG_ARCH_LITO
        if (aw8697->device_id == 832) {
                if (aw8697->haptic_real_f0 < 167) {
                        index = 0;
                } else if (aw8697->haptic_real_f0 < 169) {
                        index = 1;
                } else if (aw8697->haptic_real_f0 < 171) {
                        index = 2;
                } else if (aw8697->haptic_real_f0 < 173) {
                        index = 3;
                } else {
                        index = 4;
                }
        }
#endif /* CONFIG_ARCH_LITO */
#endif /* CONFIG_OPLUS_HAPTIC_OOS */

        if (aw8697->device_id == 619) {
                if (aw8697->haptic_real_f0 < 167) {
                        index = 0;
                } else if (aw8697->haptic_real_f0 < 169) {
                        index = 1;
                } else if (aw8697->haptic_real_f0 < 171) {
                        index = 2;
                } else if (aw8697->haptic_real_f0 < 173) {
                        index = 3;
                } else {
                        index = 4;
                }
        }

        if (aw8697->device_id == 1040) {
                if (aw8697->haptic_real_f0 < 167) {
                        index = 0;
                } else if (aw8697->haptic_real_f0 < 169) {
                        index = 1;
                } else if (aw8697->haptic_real_f0 < 171) {
                        index = 2;
                } else if (aw8697->haptic_real_f0 < 173) {
                        index = 3;
                } else {
                        index = 4;
                }
        }

	if (aw8697->device_id == 832) {
		pr_info("%s:0832 haptic bin name  %s \n", __func__, aw8697_ram_name_0832[index]);
		return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    aw8697_ram_name_0832[index], aw8697->dev, GFP_KERNEL,
                    aw8697, aw8697_ram_loaded);
	} else if (aw8697->device_id == 833) {
		pr_info("%s:19065 haptic bin name  %s \n", __func__, aw8697_ram_name_19161[index]);
		return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    aw8697_ram_name_19161[index], aw8697->dev, GFP_KERNEL,
		    aw8697, aw8697_ram_loaded);
	} else if (aw8697->device_id == 9595) {
		pr_info("%s:9595 haptic bin name  %s \n", __func__, aw8697_ram_name_9595[index]);
		return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
			    aw8697_ram_name_9595[index], aw8697->dev, GFP_KERNEL,
			    aw8697, aw8697_ram_loaded);
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	} else if (aw8697->device_id == 1815) {
		pr_info("%s:1815 haptic bin name  %s \n", __func__, aw8697_ram_name_1815[index]);
		return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
			aw8697_ram_name_1815[index], aw8697->dev, GFP_KERNEL,
                    aw8697, aw8697_ram_loaded);
#endif
	} else if (aw8697->device_id == 619) {
		pr_info("%s:0619 haptic bin name  %s \n", __func__, aw8697_ram_name_0619[index]);
		return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    aw8697_ram_name_0619[index], aw8697->dev, GFP_KERNEL,
                    aw8697, aw8697_ram_loaded);
	} else if (aw8697->device_id == 1040) {
		pr_info("%s:1040 haptic bin name  %s \n", __func__, aw8697_ram_name_1040[index]);
		return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
                    aw8697_ram_name_1040[index], aw8697->dev, GFP_KERNEL,
                    aw8697, aw8697_ram_loaded);
	} else if (aw8697->device_id == 81538) {
		if (aw8697->vibration_style == AW8697_HAPTIC_VIBRATION_CRISP_STYLE) {
			pr_info("%s:150Hz haptic bin name  %s \n", __func__, aw8697_ram_name_150[index]);
			return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8697_ram_name_150[index], aw8697->dev, GFP_KERNEL,
				aw8697, aw8697_ram_loaded);
		} else if (aw8697->vibration_style == AW8697_HAPTIC_VIBRATION_SOFT_STYLE) {
			pr_info("%s:150Hz haptic bin name  %s \n", __func__, aw8697_ram_name_150_soft[index]);
			return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8697_ram_name_150_soft[index], aw8697->dev, GFP_KERNEL,
				aw8697, aw8697_ram_loaded);
		} else {
			pr_info("%s:150Hz haptic bin name  %s \n", __func__, aw8697_ram_name_150[index]);
			return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8697_ram_name_150[index], aw8697->dev, GFP_KERNEL,
				aw8697, aw8697_ram_loaded);
		}
	} else {
		if (aw8697->vibration_style == AW8697_HAPTIC_VIBRATION_CRISP_STYLE) {
			pr_info("%s:crisp haptic bin name  %s \n", __func__, aw8697_ram_name[index]);
			return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8697_ram_name[index], aw8697->dev, GFP_KERNEL,
				aw8697, aw8697_ram_loaded);
		} else if (aw8697->vibration_style == AW8697_HAPTIC_VIBRATION_SOFT_STYLE) {
			pr_info("%s:soft haptic bin name  %s \n", __func__, aw8697_ram_name_170_soft[index]);
			return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8697_ram_name_170_soft[index], aw8697->dev, GFP_KERNEL,
				aw8697, aw8697_ram_loaded);
		} else {
			pr_info("%s:haptic bin name  %s \n", __func__, aw8697_ram_name[index]);
			return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8697_ram_name[index], aw8697->dev, GFP_KERNEL,
				aw8697, aw8697_ram_loaded);
		}
	}
}

#ifdef AWINIC_RAM_UPDATE_DELAY
static void aw8697_ram_work_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, ram_work.work);

    pr_info("%s enter\n", __func__);

    aw8697_ram_update(aw8697);

}
#endif

static int aw8697_ram_init(struct aw8697 *aw8697)
{
#ifdef AWINIC_RAM_UPDATE_DELAY
    int ram_timer_val = 5000;
    INIT_DELAYED_WORK(&aw8697->ram_work, aw8697_ram_work_routine);
    schedule_delayed_work(&aw8697->ram_work, msecs_to_jiffies(ram_timer_val));
#else
    aw8697_ram_update(aw8697);
#endif
    return 0;
}



/*****************************************************
 *
 * haptic control
 *
 *****************************************************/
static int aw8697_haptic_softreset(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_i2c_write(aw8697, AW8697_REG_ID, 0xAA);
    usleep_range(3000, 3500);
    return 0;
}

static int aw8697_haptic_active(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_WORK_MODE_MASK, AW8697_BIT_SYSCTRL_ACTIVE);
    aw8697_interrupt_clear(aw8697);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_UVLO_MASK, AW8697_BIT_SYSINTM_UVLO_EN);
    return 0;
}

static int aw8697_haptic_play_mode(struct aw8697 *aw8697, unsigned char play_mode)
{
    pr_debug("%s enter\n", __func__);

    switch(play_mode) {
        case AW8697_HAPTIC_STANDBY_MODE:
            aw8697->play_mode = AW8697_HAPTIC_STANDBY_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                    AW8697_BIT_SYSINTM_UVLO_MASK, AW8697_BIT_SYSINTM_UVLO_OFF);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_WORK_MODE_MASK, AW8697_BIT_SYSCTRL_STANDBY);
            break;
        case AW8697_HAPTIC_RAM_MODE:
            aw8697->play_mode = AW8697_HAPTIC_RAM_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_RAM_LOOP_MODE:
            aw8697->play_mode = AW8697_HAPTIC_RAM_LOOP_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }

           aw8697_haptic_active(aw8697);

#if defined (OPLUS_FEATURE_CHG_BASIC) && !defined(CONFIG_OPLUS_HAPTIC_OOS)
	   if (aw8697->device_id == DEV_ID_0619)
		   break;
#endif

            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_RTP_MODE:
            aw8697->play_mode = AW8697_HAPTIC_RTP_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RTP);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RTP_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RTP_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_TRIG_MODE:
            aw8697->play_mode = AW8697_HAPTIC_TRIG_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_ENABLE);
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK&AW8697_BIT_SYSCTRL_WORK_MODE_MASK,
                        AW8697_BIT_SYSCTRL_BST_MODE_BOOST|AW8697_BIT_SYSCTRL_STANDBY);
                aw8697_haptic_active(aw8697);
            } else {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                        AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
            }
            mdelay(2);
            break;
        case AW8697_HAPTIC_CONT_MODE:
            aw8697->play_mode = AW8697_HAPTIC_CONT_MODE;
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_CONT);
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                    AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);
            if(aw8697->auto_boost) {
                aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                        AW8697_BIT_BST_AUTO_BST_RAM_MASK, AW8697_BIT_BST_AUTO_BST_RAM_DISABLE);
            }
            aw8697_haptic_active(aw8697);
            break;
        default:
            dev_err(aw8697->dev, "%s: play mode %d err",
                    __func__, play_mode);
            break;
    }
    return 0;
}

static int aw8697_haptic_play_go(struct aw8697 *aw8697, bool flag)
{
    unsigned char reg_val = 0;
	pr_debug("%s enter,flag = %d\n", __func__, flag);

    if(flag == true) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_GO,
            AW8697_BIT_GO_MASK, AW8697_BIT_GO_ENABLE);
        mdelay(2);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_GO,
            AW8697_BIT_GO_MASK, AW8697_BIT_GO_DISABLE);
    }

    aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &reg_val);
    //pr_info("%s reg_val[%d] \n", __func__, reg_val);
    return 0;
}

static int aw8697_set_clock(struct aw8697 *aw8697, int clock_type)
{
    if (clock_type == AW8697_HAPTIC_CLOCK_CALI_F0)
    {
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, aw8697->clock_system_f0_cali_lra);
    } else if (clock_type == AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, aw8697->clock_standard_OSC_lra_rtim_code);
    }
    return 0;
}

#if 1

static void aw8697_haptic_reset_init(struct aw8697 *aw8697);

static int aw8697_haptic_stop_delay(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned int cnt = 200;

    while(cnt--) {
        aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &reg_val);
        //pr_info("%s wait for standby, reg glb_state=0x%02x\n",  __func__, reg_val);
        if((reg_val&0x0f) == 0x00) {
            return 0;
        }
        mdelay(2);
    }
    pr_err("%s do not enter standby automatically\n", __func__);
    aw8697_haptic_softreset(aw8697);
    aw8697_haptic_reset_init(aw8697);

    return 0;
}
static int aw8697_haptic_stop(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);
    aw8697->play_mode = AW8697_HAPTIC_STANDBY_MODE;

    aw8697_haptic_play_go(aw8697, false);
    aw8697_haptic_stop_delay(aw8697);
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    //set clock to cali f0
    aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_F0);

    return 0;
}
#else
// new stop

static int aw8697_haptic_android_stop(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    return 0;
}
#endif
static int aw8697_haptic_start(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_haptic_play_go(aw8697, true);

    return 0;
}

static int aw8697_haptic_set_wav_seq(struct aw8697 *aw8697,
        unsigned char wav, unsigned char seq)
{
    aw8697_i2c_write(aw8697, AW8697_REG_WAVSEQ1+wav,
            seq);
    return 0;
}

static int aw8697_haptic_set_wav_loop(struct aw8697 *aw8697,
        unsigned char wav, unsigned char loop)
{
    unsigned char tmp = 0;

    if(wav%2) {
        tmp = loop<<0;
        aw8697_i2c_write_bits(aw8697, AW8697_REG_WAVLOOP1+(wav/2),
                AW8697_BIT_WAVLOOP_SEQNP1_MASK, tmp);
    } else {
        tmp = loop<<4;
        aw8697_i2c_write_bits(aw8697, AW8697_REG_WAVLOOP1+(wav/2),
                AW8697_BIT_WAVLOOP_SEQN_MASK, tmp);
    }

    return 0;
}
/*
static int aw8697_haptic_set_main_loop(struct aw8697 *aw8697,
        unsigned char loop)
{
    aw8697_i2c_write(aw8697, AW8697_REG_MAIN_LOOP, loop);
    return 0;
}
*/

static int aw8697_haptic_set_repeat_wav_seq(struct aw8697 *aw8697, unsigned char seq)
{
    aw8697_haptic_set_wav_seq(aw8697, 0x00, seq);
    aw8697_haptic_set_wav_loop(aw8697, 0x00, AW8697_BIT_WAVLOOP_INIFINITELY);

    return 0;
}


static int aw8697_haptic_set_bst_vol(struct aw8697 *aw8697, unsigned char bst_vol)
{
    if(bst_vol & 0xe0) {
        bst_vol = 0x1f;
    }
    aw8697_i2c_write_bits(aw8697, AW8697_REG_BSTDBG4,
            AW8697_BIT_BSTDBG4_BSTVOL_MASK, (bst_vol<<1));
    return 0;
}

static int aw8697_haptic_set_bst_peak_cur(struct aw8697 *aw8697, unsigned char peak_cur)
{
    peak_cur &= AW8697_BSTCFG_PEAKCUR_LIMIT;
    aw8697_i2c_write_bits(aw8697, AW8697_REG_BSTCFG,
            AW8697_BIT_BSTCFG_PEAKCUR_MASK, peak_cur);
    return 0;
}

static int aw8697_haptic_set_gain(struct aw8697 *aw8697, unsigned char gain)
{
    aw8697_i2c_write(aw8697, AW8697_REG_DATDBG, gain);
    return 0;
}

static int aw8697_haptic_set_pwm(struct aw8697 *aw8697, unsigned char mode)
{
    switch(mode) {
        case AW8697_PWM_48K:
            aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMDBG,
                AW8697_BIT_PWMDBG_PWM_MODE_MASK, AW8697_BIT_PWMDBG_PWM_48K);
            break;
        case AW8697_PWM_24K:
            aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMDBG,
                AW8697_BIT_PWMDBG_PWM_MODE_MASK, AW8697_BIT_PWMDBG_PWM_24K);
            break;
        case AW8697_PWM_12K:
            aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMDBG,
                AW8697_BIT_PWMDBG_PWM_MODE_MASK, AW8697_BIT_PWMDBG_PWM_12K);
            break;
        default:
            break;
    }
    return 0;
}

static int aw8697_haptic_play_wav_seq(struct aw8697 *aw8697, unsigned char flag)
{
    pr_debug("%s enter\n", __func__);

    if(flag) {
        aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RAM_MODE);
        aw8697_haptic_start(aw8697);
    }
    return 0;
}

static int aw8697_haptic_play_repeat_seq(struct aw8697 *aw8697, unsigned char flag)
{
    pr_debug("%s enter\n", __func__);

    if(flag) {
        aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RAM_LOOP_MODE);
        aw8697_haptic_start(aw8697);
    }

    return 0;
}

/*****************************************************
 *
 * motor protect
 *
 *****************************************************/
static int aw8697_haptic_swicth_motorprotect_config(struct aw8697 *aw8697, unsigned char addr, unsigned char val)
{
    pr_debug("%s enter\n", __func__);
    if(addr == 1) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
                AW8697_BIT_DETCTRL_PROTECT_MASK, AW8697_BIT_DETCTRL_PROTECT_SHUTDOWN);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMPRC,
                AW8697_BIT_PWMPRC_PRC_MASK, AW8697_BIT_PWMPRC_PRC_ENABLE);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRLVL,
                AW8697_BIT_PRLVL_PR_MASK, AW8697_BIT_PRLVL_PR_ENABLE);
    } else if (addr == 0) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
                AW8697_BIT_DETCTRL_PROTECT_MASK,  AW8697_BIT_DETCTRL_PROTECT_NO_ACTION);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMPRC,
                AW8697_BIT_PWMPRC_PRC_MASK, AW8697_BIT_PWMPRC_PRC_DISABLE);
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRLVL,
                AW8697_BIT_PRLVL_PR_MASK, AW8697_BIT_PRLVL_PR_DISABLE);
    } else if (addr == 0x2d){
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PWMPRC,
                AW8697_BIT_PWMPRC_PRCTIME_MASK, val);
    }else if (addr == 0x3e){
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRLVL,
                AW8697_BIT_PRLVL_PRLVL_MASK, val);
    }else if (addr == 0x3f){
        aw8697_i2c_write_bits(aw8697, AW8697_REG_PRTIME,
                AW8697_BIT_PRTIME_PRTIME_MASK, val);
    } else{
        /*nothing to do;*/
    }
     return 0;
}

/*****************************************************
 *
 * os calibration
 *
 *****************************************************/
static int aw8697_haptic_os_calibration(struct aw8697 *aw8697)
{
    unsigned int cont = 2000;
    unsigned char reg_val = 0;
    pr_debug("%s enter\n", __func__);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_DIAG_GO_MASK, AW8697_BIT_DETCTRL_DIAG_GO_ENABLE);
    while(1){
        aw8697_i2c_read(aw8697, AW8697_REG_DETCTRL, &reg_val);
        if((reg_val & 0x01) == 0 || cont ==0)
            break;
         cont--;
    }
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);
    return 0;
}
/*****************************************************
 *
 * trig config
 *
 *****************************************************/
static int aw8697_haptic_trig_param_init(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

#ifdef OPLUS_FEATURE_CHG_BASIC
    aw8697->trig[0].enable = 0;
#else
    aw8697->trig[0].enable = AW8697_TRG1_ENABLE;
#endif
    aw8697->trig[0].default_level = AW8697_TRG1_DEFAULT_LEVEL;
    aw8697->trig[0].dual_edge = AW8697_TRG1_DUAL_EDGE;
    aw8697->trig[0].frist_seq = AW8697_TRG1_FIRST_EDGE_SEQ;
    aw8697->trig[0].second_seq = AW8697_TRG1_SECOND_EDGE_SEQ;
#ifdef OPLUS_FEATURE_CHG_BASIC
    aw8697->trig[1].enable = 0;
#else
    aw8697->trig[1].enable = AW8697_TRG2_ENABLE;
#endif
    aw8697->trig[1].default_level = AW8697_TRG2_DEFAULT_LEVEL;
    aw8697->trig[1].dual_edge = AW8697_TRG2_DUAL_EDGE;
    aw8697->trig[1].frist_seq = AW8697_TRG2_FIRST_EDGE_SEQ;
    aw8697->trig[1].second_seq = AW8697_TRG2_SECOND_EDGE_SEQ;
#ifdef OPLUS_FEATURE_CHG_BASIC
    aw8697->trig[2].enable = 0;
#else
    aw8697->trig[2].enable = AW8697_TRG3_ENABLE;
#endif
    aw8697->trig[2].default_level = AW8697_TRG3_DEFAULT_LEVEL;
    aw8697->trig[2].dual_edge = AW8697_TRG3_DUAL_EDGE;
    aw8697->trig[2].frist_seq = AW8697_TRG3_FIRST_EDGE_SEQ;
    aw8697->trig[2].second_seq = AW8697_TRG3_SECOND_EDGE_SEQ;

    return 0;
}

static int aw8697_haptic_trig_param_config(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    if(aw8697->trig[0].default_level) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG1_POLAR_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG1_POLAR_POS);
    }
    if(aw8697->trig[1].default_level) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG2_POLAR_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG2_POLAR_POS);
    }
    if(aw8697->trig[2].default_level) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG3_POLAR_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_POLAR_MASK, AW8697_BIT_TRGCFG1_TRG3_POLAR_POS);
    }

    if(aw8697->trig[0].dual_edge) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG1_EDGE_POS_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG1_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG1_EDGE_POS);
    }
    if(aw8697->trig[1].dual_edge) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG2_EDGE_POS_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG2_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG2_EDGE_POS);
    }
    if(aw8697->trig[2].dual_edge) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG3_EDGE_POS_NEG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG1,
                AW8697_BIT_TRGCFG1_TRG3_EDGE_MASK, AW8697_BIT_TRGCFG1_TRG3_EDGE_POS);
    }

    if(aw8697->trig[0].frist_seq) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG1_WAV_P, aw8697->trig[0].frist_seq);
    }
    if(aw8697->trig[0].second_seq && aw8697->trig[0].dual_edge) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG1_WAV_N, aw8697->trig[0].second_seq);
    }
    if(aw8697->trig[1].frist_seq) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG2_WAV_P, aw8697->trig[1].frist_seq);
    }
    if(aw8697->trig[1].second_seq && aw8697->trig[1].dual_edge) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG2_WAV_N, aw8697->trig[1].second_seq);
    }
    if(aw8697->trig[2].frist_seq) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG3_WAV_P, aw8697->trig[1].frist_seq);
    }
    if(aw8697->trig[2].second_seq && aw8697->trig[2].dual_edge) {
        aw8697_i2c_write(aw8697, AW8697_REG_TRG3_WAV_N, aw8697->trig[1].second_seq);
    }

    return 0;
}

static int aw8697_haptic_trig_enable_config(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG2,
            AW8697_BIT_TRGCFG2_TRG1_ENABLE_MASK, aw8697->trig[0].enable);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG2,
            AW8697_BIT_TRGCFG2_TRG2_ENABLE_MASK, aw8697->trig[1].enable);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_TRG_CFG2,
            AW8697_BIT_TRGCFG2_TRG3_ENABLE_MASK, aw8697->trig[2].enable);

    return 0;
}


static int aw8697_haptic_auto_boost_config(struct aw8697 *aw8697, unsigned char flag)
{
    aw8697->auto_boost = flag;
    if(flag) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                AW8697_BIT_BST_AUTO_BST_AUTOSW_MASK, AW8697_BIT_BST_AUTO_BST_AUTOMATIC_BOOST);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_BST_AUTO,
                AW8697_BIT_BST_AUTO_BST_AUTOSW_MASK, AW8697_BIT_BST_AUTO_BST_MANUAL_BOOST);
    }
    return 0;
}

/*****************************************************
 *
 * vbat mode
 *
 *****************************************************/
static int aw8697_haptic_cont_vbat_mode(struct aw8697 *aw8697, unsigned char flag)
{
    if(flag == AW8697_HAPTIC_CONT_VBAT_HW_COMP_MODE) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ADCTEST,
                AW8697_BIT_ADCTEST_VBAT_MODE_MASK, AW8697_BIT_ADCTEST_VBAT_HW_COMP);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ADCTEST,
                AW8697_BIT_ADCTEST_VBAT_MODE_MASK, AW8697_BIT_ADCTEST_VBAT_SW_COMP);
    }
    return 0;
}

static int aw8697_haptic_get_vbat(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned int cont = 2000;

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_VBAT_GO_MASK, AW8697_BIT_DETCTRL_VABT_GO_ENABLE);

    while(1){
        aw8697_i2c_read(aw8697, AW8697_REG_DETCTRL, &reg_val);
        if((reg_val & 0x02) == 0 || cont == 0)
             break;
        cont--;
    }

    aw8697_i2c_read(aw8697, AW8697_REG_VBATDET, &reg_val);
    aw8697->vbat = 6100 * reg_val / 256;
    if(aw8697->vbat > AW8697_VBAT_MAX) {
        aw8697->vbat = AW8697_VBAT_MAX;
        pr_debug("%s vbat max limit = %dmV\n", __func__, aw8697->vbat);
    }
    if(aw8697->vbat < AW8697_VBAT_MIN) {
        aw8697->vbat = AW8697_VBAT_MIN;
        pr_debug("%s vbat min limit = %dmV\n", __func__, aw8697->vbat);
    }

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);

    return 0;
}

static int aw8697_haptic_ram_vbat_comp(struct aw8697 *aw8697, bool flag)
{
    int temp_gain = 0;

    if(flag) {
        if(aw8697->ram_vbat_comp == AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE) {
            aw8697_haptic_get_vbat(aw8697);
            temp_gain = aw8697->gain * AW8697_VBAT_REFER / aw8697->vbat;
            if(temp_gain > (128*AW8697_VBAT_REFER/AW8697_VBAT_MIN)) {
                temp_gain = 128*AW8697_VBAT_REFER/AW8697_VBAT_MIN;
                pr_debug("%s gain limit=%d\n", __func__, temp_gain);
            }
            aw8697_haptic_set_gain(aw8697, temp_gain);
        } else {
            aw8697_haptic_set_gain(aw8697, aw8697->gain);
        }
    } else {
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
    }
    pr_err("%s: aw8697->gain = 0x%x, temp_gain = 0x%x, aw8697->ram_vbat_comp=%d.\n",
        __func__, aw8697->gain, temp_gain, aw8697->ram_vbat_comp);

    return 0;
}

/*****************************************************
 *
 * f0
 *
 *****************************************************/
static int aw8697_haptic_set_f0_preset(struct aw8697 *aw8697)
{
    unsigned int f0_reg = 0;

    pr_debug("%s enter\n", __func__);

	f0_reg = 1000000000 / (aw8697->f0_pre * AW8697_HAPTIC_F0_COEFF);
    aw8697_i2c_write(aw8697, AW8697_REG_F_PRE_H, (unsigned char)((f0_reg>>8)&0xff));
    aw8697_i2c_write(aw8697, AW8697_REG_F_PRE_L, (unsigned char)((f0_reg>>0)&0xff));

    return 0;
}

static int aw8697_haptic_read_f0(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;
    unsigned int f0_reg = 0;
    unsigned long f0_tmp = 0;

    pr_debug("%s enter\n", __func__);

    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_F0_H, &reg_val);
    f0_reg = (reg_val<<8);
    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_F0_L, &reg_val);
    f0_reg |= (reg_val<<0);
#ifndef OPLUS_FEATURE_CHG_BASIC
	f0_tmp = 1000000000 / (f0_reg * AW8697_HAPTIC_F0_COEFF);
#else
    if (f0_reg != 0) {
		f0_tmp = 1000000000 / (f0_reg * AW8697_HAPTIC_F0_COEFF);
    } else {
        aw8697->f0 = 0;
        pr_err("%s f0_tmp=%d, error  here \n", __func__, f0_tmp);
        return 0;
    }
#endif
    aw8697->f0 = (unsigned int)f0_tmp;
    pr_info("%s f0=%d\n", __func__, aw8697->f0);

    return 0;
}

static int aw8697_haptic_read_cont_f0(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;
    unsigned int f0_reg = 0;
    unsigned long f0_tmp = 0;

    pr_debug("%s enter\n", __func__);

    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_CONT_H, &reg_val);
    f0_reg = (reg_val<<8);
    ret = aw8697_i2c_read(aw8697, AW8697_REG_F_LRA_CONT_L, &reg_val);
    f0_reg |= (reg_val<<0);

#ifndef OPLUS_FEATURE_CHG_BASIC
	f0_tmp = 1000000000 / (f0_reg * AW8697_HAPTIC_F0_COEFF);
#else
    if (f0_reg != 0) {
        f0_tmp = 1000000000 / (f0_reg * AW8697_HAPTIC_F0_COEFF);
    } else {
        pr_err("%s f0_tmp=%d, error  here \n", __func__, f0_tmp);
    }
#endif
    aw8697->cont_f0 = (unsigned int)f0_tmp;
    pr_info("%s f0=%d\n", __func__, aw8697->cont_f0);

    return 0;
}

static int aw8697_haptic_read_beme(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;

    ret = aw8697_i2c_read(aw8697, AW8697_REG_WAIT_VOL_MP, &reg_val);
    aw8697->max_pos_beme = (reg_val<<0);
    ret = aw8697_i2c_read(aw8697, AW8697_REG_WAIT_VOL_MN, &reg_val);
    aw8697->max_neg_beme = (reg_val<<0);

    pr_info("%s max_pos_beme=%d\n", __func__, aw8697->max_pos_beme);
    pr_info("%s max_neg_beme=%d\n", __func__, aw8697->max_neg_beme);

    return 0;
}



/*****************************************************
 *
 * rtp
 *
 *****************************************************/
static void aw8697_haptic_set_rtp_aei(struct aw8697 *aw8697, bool flag)
{
    if(flag) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AE_MASK, AW8697_BIT_SYSINTM_FF_AE_EN);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AE_MASK, AW8697_BIT_SYSINTM_FF_AE_OFF);
    }
}
/*
static void aw8697_haptic_set_rtp_afi(struct aw8697 *aw8697, bool flag)
{
    if(flag) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AF_MASK, AW8697_BIT_SYSINTM_FF_AF_EN);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
                AW8697_BIT_SYSINTM_FF_AF_MASK, AW8697_BIT_SYSINTM_FF_AF_OFF);
    }
}
*/

static unsigned char aw8697_haptic_rtp_get_fifo_aei(struct aw8697 *aw8697)
{
    unsigned char ret;
    unsigned char reg_val;


    if(aw8697->osc_cali_flag==1){
        aw8697_i2c_read(aw8697, AW8697_REG_SYSST, &reg_val);
        reg_val &= AW8697_BIT_SYSST_FF_AES;
        ret = reg_val>>4;
    }else{
      aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
      reg_val &= AW8697_BIT_SYSINT_FF_AEI;
      ret = reg_val>>4;
    }

    return ret;
}

static unsigned char aw8697_haptic_rtp_get_fifo_afi(struct aw8697 *aw8697)
{
    unsigned char ret = 0;
    unsigned char reg_val = 0;


    if(aw8697->osc_cali_flag==1){
        aw8697_i2c_read(aw8697, AW8697_REG_SYSST, &reg_val);
        reg_val &= AW8697_BIT_SYSST_FF_AFS;
        ret = reg_val>>3;
    }else{
      aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
      reg_val &= AW8697_BIT_SYSINT_FF_AFI;
      ret = reg_val>>3;
    }

    return ret;
}

/*****************************************************
 *
 * rtp
 *
 *****************************************************/
static void aw8697_dump_rtp_regs(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, 0x02, &reg_val);
    pr_info("%s reg_0x02 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x03, &reg_val);
    pr_info("%s reg_0x03 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x04, &reg_val);
    pr_info("%s reg_0x04 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x05, &reg_val);
    pr_info("%s reg_0x05 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x46, &reg_val);
    pr_info("%s reg_0x46 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x30, &reg_val);
    pr_info("%s reg_0x30 = 0x%02x\n", __func__, reg_val);
}

static void aw8697_pm_qos_enable(struct aw8697 *aw8697, bool enabled)
{
	mutex_lock(&aw8697->qos_lock);
	if (enabled) {
		if (!pm_qos_request_active(&pm_qos_req_vb)) {
			pm_qos_add_request(&pm_qos_req_vb, PM_QOS_CPU_DMA_LATENCY, PM_QOS_VALUE_VB);
		} else {
			pm_qos_update_request(&pm_qos_req_vb, PM_QOS_VALUE_VB);
		}
	} else {
		pm_qos_update_request(&pm_qos_req_vb, PM_QOS_DEFAULT_VALUE);
	}
	mutex_unlock(&aw8697->qos_lock);
}

static int aw8697_haptic_rtp_init(struct aw8697 *aw8697)
{
    unsigned int buf_len = 0;
    bool rtp_start = true;
	unsigned char glb_state_val = 0x0;

    pr_info("%s enter\n", __func__);
    aw8697_pm_qos_enable(aw8697, true);
	if(aw8697->ram.base_addr == 0) {
		aw8697_ram_update(aw8697);
	}
    aw8697->rtp_cnt = 0;
    mutex_lock(&aw8697->rtp_lock);
    aw8697_dump_rtp_regs(aw8697);
    aw8697_haptic_play_go(aw8697, true);
    while((!aw8697_haptic_rtp_get_fifo_afi(aw8697)) &&
            (aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)) {
        ///pr_info("%s rtp cnt = %d\n", __func__, aw8697->rtp_cnt);
        if (rtp_start) {
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr);
            }
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                    &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            //pr_info("%s 111 rtp cnt = %d\n", __func__, aw8697->rtp_cnt);
            rtp_start = false;
        } else {
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr>>2);
            }
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                    &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            //pr_info("%s 222 rtp cnt = %d\n", __func__, aw8697->rtp_cnt);
        }
        aw8697->rtp_cnt += buf_len;
		aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &glb_state_val);
		if (aw8697->rtp_cnt >= aw8697_rtp->len || aw8697->ram.base_addr == 0 || (glb_state_val & 0x0f) == 0) {
			pr_info("%s: rtp update complete, aw8697->ram.base_addr[%d], glb_state_val[%d]\n", __func__, aw8697->ram.base_addr, glb_state_val);
            aw8697->rtp_cnt = 0;
            aw8697_dump_rtp_regs(aw8697);
            mutex_unlock(&aw8697->rtp_lock);
            aw8697_pm_qos_enable(aw8697, false);
            return 0;
        }
    }
    mutex_unlock(&aw8697->rtp_lock);

    if(aw8697->play_mode == AW8697_HAPTIC_RTP_MODE) {
        aw8697_haptic_set_rtp_aei(aw8697, true);
    }
    aw8697_pm_qos_enable(aw8697, false);

    pr_info("%s exit\n", __func__);

    return 0;
}


static int aw8697_clock_OSC_trim_calibration(unsigned long int theory_time, unsigned long int real_time)
{
    unsigned int real_code = 0;
    unsigned int LRA_TRIM_CODE = 0;
    unsigned int DFT_LRA_TRIM_CODE = 0;
    unsigned int Not_need_cali_threshold = 10;

    ///unsigned char real_code;

    if(theory_time == real_time )
    {
        printk("aw_osctheory_time == real_time:%ld  theory_time = %ld not need to cali\n",real_time,theory_time);
        return 0;
    }
    if(theory_time < real_time ){
        if((real_time - theory_time) > (theory_time/50 ))
        {
            printk("aw_osc(real_time - theory_time) > (theory_time/50 ) not to cali\n");
            return DFT_LRA_TRIM_CODE;
        }

        if ((real_time - theory_time) < (Not_need_cali_threshold*theory_time/10000))
        {
            printk("aw_oscmicrosecond:%ld  theory_time = %ld not need to cali\n",real_time,theory_time);
            return DFT_LRA_TRIM_CODE;
        }

        real_code = ((real_time - theory_time)* 4000) / theory_time;
        real_code = ((real_code%10 < 5)? 0 : 1) + real_code/10;
        real_code = 32 + real_code;
    }
    if(theory_time > real_time){
        if(( theory_time - real_time) > (theory_time/50 ))
        {
            printk("aw_osc(( theory_time - real_time) > (theory_time/50 ))  not to cali \n");
            return DFT_LRA_TRIM_CODE;
        }
        if ((theory_time - real_time) < (Not_need_cali_threshold*theory_time/10000))
        {
            printk("aw_oscmicrosecond:%ld  theory_time = %ld not need to cali\n",real_time,theory_time);
            return DFT_LRA_TRIM_CODE;
        }
        real_code = 32 - ((theory_time - real_time )* 400 )/ theory_time ;
        real_code = ((theory_time - real_time)* 4000) / theory_time ;
        real_code = ((real_code%10 < 5)? 0 : 1) + real_code/10;
        real_code = 32 - real_code;

    }

    if (real_code>31)
    {
        LRA_TRIM_CODE = real_code -32;
    }
    else
    {
        LRA_TRIM_CODE = real_code +32;
    }
    printk("aw_oscmicrosecond:%ld  theory_time = %ld real_code =0X%02X LRA_TRIM_CODE 0X%02X\n",real_time,theory_time,real_code,LRA_TRIM_CODE);

    return LRA_TRIM_CODE;
}

static int aw8697_rtp_trim_lra_calibration(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned int fre_val = 0;
    unsigned int theory_time = 0;
    ///unsigned int real_code = 0;
    unsigned int lra_rtim_code = 0;

    aw8697_i2c_read(aw8697, AW8697_REG_PWMDBG, &reg_val);
    fre_val = (reg_val & 0x006f )>> 5;

    if(fre_val == 3)
      theory_time = (aw8697->rtp_len / 12000) * 1000000; /*12K */
    if(fre_val == 2)
      theory_time = (aw8697->rtp_len / 24000) * 1000000; /*24K */
    if(fre_val == 1 || fre_val == 0)
      theory_time = (aw8697->rtp_len / 48000) * 1000000; /*48K */

    printk("microsecond:%ld  theory_time = %d\n",aw8697->microsecond,theory_time);

    lra_rtim_code = aw8697_clock_OSC_trim_calibration(theory_time,aw8697->microsecond);

    if(lra_rtim_code > 0 )
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, (char)lra_rtim_code);
    aw8697->clock_standard_OSC_lra_rtim_code = lra_rtim_code;
    return 0;
}

static unsigned char aw8697_haptic_osc_read_INT(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    aw8697_i2c_read(aw8697, AW8697_REG_DBGSTAT, &reg_val);
    return reg_val;
}

static int aw8697_rtp_osc_calibration(struct aw8697 *aw8697)
{
    const struct firmware *rtp_file;
    int ret = -1;
    unsigned int buf_len = 0;
    ///unsigned char reg_val = 0;
    unsigned char osc_int_state = 0;
    ///unsigned int  pass_cont=1;
    ///unsigned int  cyc_cont=150;
    aw8697->rtp_cnt = 0;
    aw8697->timeval_flags = 1;
    aw8697->osc_cali_flag =1;

    pr_info("%s enter\n", __func__);

    /* fw loaded */

    //vincent add for stop

    aw8697_haptic_stop(aw8697);
    aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD);

    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;


    if(aw8697->f0_cali_flag == AW8697_HAPTIC_CALI_F0) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_REG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_EFUSE);
    }
    ret = request_firmware(&rtp_file,
            aw8697_rtp_name[/*aw8697->rtp_file_num*/ 0],
            aw8697->dev);
    if(ret < 0)
    {
	    pr_err("%s: failed to read %s\n", __func__,
		    aw8697_rtp_name[/*aw8697->rtp_file_num*/ 0]);
	    return ret;
    }
    aw8697->rtp_init = 0;
    mutex_lock(&aw8697->rtp_lock);//vincent
    #ifndef OPLUS_FEATURE_CHG_BASIC
    kfree(aw8697_rtp);
    aw8697_rtp = kzalloc(rtp_file->size+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);
        return -1;
    }
    #else
    ret = aw8697_container_init(rtp_file->size+sizeof(int));
    if (ret < 0) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);
        return -1;
    }
    #endif
    aw8697_rtp->len = rtp_file->size;
    aw8697->rtp_len = rtp_file->size;
    mutex_unlock(&aw8697->rtp_lock);//vincent

    pr_info("%s: rtp file [%s] size = %d\n", __func__,
            aw8697_rtp_name[/*aw8697->rtp_file_num*/ 0], aw8697_rtp->len);
    memcpy(aw8697_rtp->data, rtp_file->data, rtp_file->size);
    release_firmware(rtp_file);

    //aw8697->rtp_init = 1; //Don't enter aw8697_irq,because osc calibration use while(1) function

    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);

    /* rtp mode config */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
    /* haptic start */
    aw8697_haptic_start(aw8697);

    aw8697_pm_qos_enable(aw8697, true);

    //while(aw8697->rtp_file_num > 0 && (aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)){
    while(1)
    {
        if (!aw8697_haptic_rtp_get_fifo_afi(aw8697)) //not almost full
        {
            pr_info("%s !aw8697_haptic_rtp_get_fifo_afi done aw8697->rtp_cnt= %d \n", __func__,aw8697->rtp_cnt);
            mutex_lock(&aw8697->rtp_lock);
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2))
            {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            }
            else {
                buf_len = (aw8697->ram.base_addr>>2);
            }

            if (aw8697->rtp_cnt != aw8697_rtp->len)
            {
                if(aw8697->timeval_flags ==1)
                {
#ifdef KERNEL_VERSION_49
                    do_gettimeofday(&aw8697->start);
#else
					aw8697->kstart = ktime_get();
#endif
                    aw8697->timeval_flags = 0;
                }

                aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,&aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
                aw8697->rtp_cnt += buf_len;
            }
             mutex_unlock(&aw8697->rtp_lock);

        }


        osc_int_state = aw8697_haptic_osc_read_INT(aw8697);
        if(osc_int_state&AW8697_BIT_SYSINT_DONEI)
        {
#ifdef KERNEL_VERSION_49
            do_gettimeofday(&aw8697->end);
#else
			aw8697->kend = ktime_get();
#endif
            pr_info("%s vincent playback done aw8697->rtp_cnt= %d \n", __func__,aw8697->rtp_cnt);
            break;
        }

#ifdef KERNEL_VERSION_49
        do_gettimeofday(&aw8697->end);
        aw8697->microsecond = (aw8697->end.tv_sec - aw8697->start.tv_sec)*1000000 +
            (aw8697->end.tv_usec - aw8697->start.tv_usec);
#else
		aw8697->kend = ktime_get();
		aw8697->microsecond = ktime_to_us(ktime_sub(aw8697->kend, aw8697->kstart));
#endif
        if (aw8697->microsecond > 6000000)
        {
            pr_info("%s vincent time out aw8697->rtp_cnt %d osc_int_state %02x\n", __func__,aw8697->rtp_cnt, osc_int_state);
            break;
        }

    }
    //ENABLE IRQ
    aw8697_pm_qos_enable(aw8697, false);
    aw8697->osc_cali_flag =0;
#ifdef KERNEL_VERSION_49
    aw8697->microsecond = (aw8697->end.tv_sec - aw8697->start.tv_sec)*1000000 +
        (aw8697->end.tv_usec - aw8697->start.tv_usec);
#else
	aw8697->microsecond = ktime_to_us(ktime_sub(aw8697->kend, aw8697->kstart));
#endif
    /*calibration osc*/
    printk("%s 2018_microsecond:%ld \n",__func__,aw8697->microsecond);

    pr_info("%s exit\n", __func__);
    return 0;
}

static void aw8697_op_clean_status(struct aw8697 *aw8697)
{
    aw8697->audio_ready = false;
    aw8697->haptic_ready = false;
    aw8697->pre_haptic_number = 0;
#ifndef CONFIG_OPLUS_HAPTIC_OOS
	aw8697->rtp_routine_on = 0;
#endif
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	/*aw8697->sin_add_flag = 0;*/
#endif
    pr_info("%s enter\n", __FUNCTION__);
}


const struct firmware *aw8697_old_work_file_load_accord_f0(struct aw8697 *aw8697)
{
    const struct firmware *rtp_file;
    unsigned int f0_file_num = 1024;
    int ret = -1;

    if (aw8697->rtp_file_num == AW8697_WAVEFORM_INDEX_OLD_STEADY
         || aw8697->rtp_file_num == AW8697_WAVEFORM_INDEX_HIGH_TEMP)
    {
#ifdef CONFIG_OPLUS_HAPTIC_OOS
        if (aw8697->device_id == 815 || aw8697->device_id == 1815) {
#else
        if (aw8697->device_id == 815 || aw8697->device_id == 9595) {
#endif
            if(aw8697->f0 <= 1610)
                f0_file_num = 0;
            else if(aw8697->f0 <= 1630)
                f0_file_num = 1;
            else if(aw8697->f0 <= 1650)
                f0_file_num = 2;
            else if(aw8697->f0 <= 1670)
                f0_file_num = 3;
            else if(aw8697->f0 <= 1690)
                f0_file_num = 4;
            else if(aw8697->f0 <= 1710)
                f0_file_num = 5;
            else if(aw8697->f0 <= 1730)
                f0_file_num = 6;
            else if(aw8697->f0 <= 1750)
                f0_file_num = 7;
            else if(aw8697->f0 <= 1770)
                f0_file_num = 8;
            else if(aw8697->f0 <= 1790)
                f0_file_num = 9;
            else
                f0_file_num = 10;
		} else if (aw8697->device_id == 81538) {
			if (aw8697->f0 <= 1450) {
				f0_file_num = 0;
			} else if (aw8697->f0 <= 1460) {
				f0_file_num = 1;
			} else if (aw8697->f0 <= 1470) {
				f0_file_num = 2;
			} else if (aw8697->f0 <= 1480) {
				f0_file_num = 3;
			} else if (aw8697->f0 <= 1490) {
				f0_file_num = 4;
			} else if (aw8697->f0 <= 1500) {
				f0_file_num = 5;
			} else if (aw8697->f0 <= 1510) {
				f0_file_num = 6;
			} else if (aw8697->f0 <= 1520) {
				f0_file_num = 7;
			} else if (aw8697->f0 <= 1530) {
				f0_file_num = 8;
			} else if (aw8697->f0 <= 1540) {
				f0_file_num = 9;
			} else {
				f0_file_num = 10;
			}
        } else if(aw8697->device_id == 832 || aw8697->device_id == 833){
            if(aw8697->f0 <= 2255)
                f0_file_num = 0;
            else if(aw8697->f0 <= 2265)
                f0_file_num = 1;
            else if(aw8697->f0 <= 2275)
                f0_file_num = 2;
            else if(aw8697->f0 <= 2285)
                f0_file_num = 3;
            else if(aw8697->f0 <= 2295)
                f0_file_num = 4;
            else if(aw8697->f0 <= 2305)
                f0_file_num = 5;
            else if(aw8697->f0 <= 2315)
                f0_file_num = 6;
            else if(aw8697->f0 <= 2325)
                f0_file_num = 7;
            else if(aw8697->f0 <= 2335)
                f0_file_num = 8;
            else if(aw8697->f0 <= 2345)
                f0_file_num = 9;
            else
                f0_file_num = 10;
        }
        if (aw8697->rtp_file_num == AW8697_WAVEFORM_INDEX_OLD_STEADY) {
		if (aw8697->device_id == 815) {
			ret = request_firmware(&rtp_file,
					aw8697_old_steady_test_rtp_name_0815[f0_file_num],
					aw8697->dev);
		} else if (aw8697->device_id == 81538) {
			ret = request_firmware(&rtp_file,
					aw8697_old_steady_test_rtp_name_081538[f0_file_num],
					aw8697->dev);
#ifdef CONFIG_OPLUS_HAPTIC_OOS
		} else if (aw8697->device_id == 1815) {
			ret = request_firmware(&rtp_file,
					aw8697_old_steady_test_rtp_name_1815[f0_file_num],
					aw8697->dev);
#endif
		} else if (aw8697->device_id == 9595) {
			ret = request_firmware(&rtp_file,
					aw8697_old_steady_test_rtp_name_9595[f0_file_num],
					aw8697->dev);
		} else if (aw8697->device_id == 832 || aw8697->device_id == 833) {
			ret = request_firmware(&rtp_file,
					aw8697_old_steady_test_rtp_name_0832[f0_file_num],
					aw8697->dev);
		}
        } else {
            if(aw8697->device_id == 815){
                ret = request_firmware(&rtp_file,
                        aw8697_high_temp_high_humidity_0815[f0_file_num],
                        aw8697->dev);
			} else if (aw8697->device_id == 81538) {
				ret = request_firmware(&rtp_file,
						aw8697_high_temp_high_humidity_081538[f0_file_num],
						aw8697->dev);
#ifdef CONFIG_OPLUS_HAPTIC_OOS
		} else if (aw8697->device_id == 1815) {
                ret = request_firmware(&rtp_file,
                        aw8697_high_temp_high_humidity_1815[f0_file_num],
                        aw8697->dev);
#endif
		} else if (aw8697->device_id == 9595) {
                ret = request_firmware(&rtp_file,
                        aw8697_high_temp_high_humidity_9595[f0_file_num],
                        aw8697->dev);
            }else if(aw8697->device_id == 832 || aw8697->device_id == 833){
                ret = request_firmware(&rtp_file,
                        aw8697_high_temp_high_humidity_0832[f0_file_num],
                        aw8697->dev);
            }
        }
        if(ret < 0)
        {
            pr_err("%s: failed to read id[%d],index[%d]\n", __func__, aw8697->device_id, f0_file_num);
            aw8697->rtp_routine_on = 0;
            return NULL;
        }
        return rtp_file;
    }
    return NULL;
}

const struct firmware *aw8697_rtp_load_file_accord_f0(struct aw8697 *aw8697)
{
    const struct firmware *rtp_file;
    unsigned int f0_file_num = 1024;
    int ret = -1;

    if (aw8697->rtp_file_num == AW8697_WAVEFORM_INDEX_OLD_STEADY
         || aw8697->rtp_file_num == AW8697_WAVEFORM_INDEX_HIGH_TEMP)
    {
        return aw8697_old_work_file_load_accord_f0(aw8697);
    }

    return NULL;  // mark by huangtongfeng

    if ((aw8697->rtp_file_num >=  RINGTONES_START_INDEX && aw8697->rtp_file_num <= RINGTONES_END_INDEX)
        || (aw8697->rtp_file_num >=  NEW_RING_START && aw8697->rtp_file_num <= NEW_RING_END)
		|| (aw8697->rtp_file_num >=  OS12_NEW_RING_START && aw8697->rtp_file_num <= OS12_NEW_RING_END)
        || aw8697->rtp_file_num == RINGTONES_SIMPLE_INDEX
        || aw8697->rtp_file_num == RINGTONES_PURE_INDEX)
    {
        if (aw8697->f0 <= 1670)
        {
            f0_file_num = aw8697->rtp_file_num;
            pr_info("%s  ringtone f0_file_num[%d]\n", __func__, f0_file_num);
            ret = request_firmware(&rtp_file,
                    aw8697_ringtone_rtp_f0_170_name[f0_file_num],
                    aw8697->dev);
            if(ret < 0)
            {
                pr_err("%s: failed to read %s\n", __func__,
                        aw8697_ringtone_rtp_f0_170_name[f0_file_num]);
                aw8697->rtp_routine_on = 0;
                return NULL;
            }
            return rtp_file;
        }
        return NULL;
    }
    else if (aw8697->rtp_file_num == AW8697_RTP_LONG_SOUND_INDEX
        || aw8697->rtp_file_num == AW8697_WAVEFORM_INDEX_OLD_STEADY)
    {
        if (aw8697->f0 <= 1650)
            f0_file_num = 0;
        else if (aw8697->f0 <= 1670)
            f0_file_num = 1;
        else if (aw8697->f0 <= 1700)
            f0_file_num = 2;
        else
            f0_file_num = 3;
        pr_info("%s long sound or old steady test f0_file_num[%d], aw8697->rtp_file_num[%d]\n", __func__, f0_file_num, aw8697->rtp_file_num);

        if (aw8697->rtp_file_num == AW8697_RTP_LONG_SOUND_INDEX) {
            ret = request_firmware(&rtp_file,
                    aw8697_long_sound_rtp_name[f0_file_num],
                    aw8697->dev);
        }
        else if (aw8697->rtp_file_num == AW8697_WAVEFORM_INDEX_OLD_STEADY){
            ret = request_firmware(&rtp_file,
                    aw8697_old_steady_test_rtp_name_0815[f0_file_num],
                    aw8697->dev);
        }
        if(ret < 0)
        {
            pr_err("%s: failed to read %s\n", __func__,
                    aw8697_long_sound_rtp_name[f0_file_num]);
            aw8697->rtp_routine_on = 0;
            return NULL;
        }
        return rtp_file;
    }
    return NULL;
}

#ifdef CONFIG_OPLUS_HAPTIC_OOS
unsigned char *one_sine_data;
unsigned int rtp_500ms_first_sine_place;
unsigned int one_sine_data_length;
unsigned int one_sine_data_time;

#define RTP_500MS_FIRST_SINE_PLACE_1815		141
#define ONE_SINE_DATA_LENGTH_1815		141
#define ONE_SINE_DATA_TIME_1815			588
static unsigned char one_sine_data_1815[] =  {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0b, 0x0c,
	0x0d, 0x0d, 0x0e, 0x0f, 0x0f, 0x10, 0x10, 0x11, 0x11, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x13, 0x13, 0x13, 0x13, 0x12, 0x12, 0x12, 0x11,
	0x11, 0x10, 0x10, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x08, 0x07, 0x06,
	0x05, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0xff, 0xfe, 0xfd, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9,
	0xf8, 0xf7, 0xf7, 0xf6, 0xf5, 0xf4, 0xf4, 0xf3, 0xf2, 0xf2, 0xf1, 0xf0, 0xf0, 0xef, 0xef, 0xef,
	0xee, 0xee, 0xed, 0xed, 0xed, 0xed, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xed,
	0xed, 0xed, 0xed, 0xee, 0xee, 0xef, 0xef, 0xf0, 0xf0, 0xf1, 0xf1, 0xf2, 0xf2, 0xf3, 0xf4, 0xf4,
	0xf5, 0xf6, 0xf7, 0xf8, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0xff,
};

#define RTP_500MS_FIRST_SINE_PLACE_0832         102
#define ONE_SINE_DATA_LENGTH_0832               102
#define ONE_SINE_DATA_TIME_0832                 425
static unsigned char one_sine_data_0832[] =  {
	0x00, 0x01, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16, 0x17, 0x19, 0x1a, 0x1c,
	0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x22, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x22, 0x21,
	0x21, 0x20, 0x1f, 0x1e, 0x1c, 0x1b, 0x19, 0x18, 0x16, 0x15, 0x13, 0x11, 0x0f, 0x0d, 0x0b, 0x09,
	0x06, 0x04, 0x02, 0x00, 0xff, 0xfd, 0xfa, 0xf8, 0xf6, 0xf4, 0xf2, 0xf0, 0xee, 0xec, 0xea, 0xe9,
	0xe7, 0xe6, 0xe4, 0xe3, 0xe2, 0xe1, 0xe0, 0xdf, 0xde, 0xde, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
	0xdd, 0xde, 0xdf, 0xdf, 0xe0, 0xe1, 0xe2, 0xe4, 0xe5, 0xe6, 0xe8, 0xea, 0xeb, 0xed, 0xef, 0xf1,
	0xf3, 0xf5, 0xf7, 0xf9, 0xfc, 0xfe,
};

#define RTP_500MS_FIRST_SINE_PLACE_0619         141
#define ONE_SINE_DATA_LENGTH_0619               141
#define ONE_SINE_DATA_TIME_0619                 588
static unsigned char one_sine_data_0619[] =  {
	0x00, 0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	0x11, 0x12, 0x13, 0x14, 0x14, 0x15, 0x16, 0x16, 0x17, 0x18, 0x18, 0x19, 0x19, 0x19, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1b, 0x1b, 0x1b, 0x1b, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x19, 0x19, 0x18, 0x18, 0x17,
	0x17, 0x16, 0x15, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09,
	0x08, 0x07, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0xff, 0xfe, 0xfd, 0xfc, 0xfa, 0xf9, 0xf8, 0xf7,
	0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0, 0xef, 0xee, 0xed, 0xec, 0xeb, 0xeb, 0xea, 0xe9, 0xe9,
	0xe8, 0xe8, 0xe7, 0xe7, 0xe6, 0xe6, 0xe6, 0xe6, 0xe5, 0xe5, 0xe5, 0xe5, 0xe5, 0xe6, 0xe6, 0xe6,
	0xe6, 0xe7, 0xe7, 0xe7, 0xe8, 0xe8, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed, 0xed, 0xee, 0xef, 0xf0,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xff,
};

#define RTP_500MS_FIRST_SINE_PLACE_1040         151
#define ONE_SINE_DATA_LENGTH_1040               151
#define ONE_SINE_DATA_TIME_1040                 588
static unsigned char one_sine_data_1040[] =  {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x14, 0x15, 0x16, 0x16, 0x17, 0x17, 0x18, 0x18, 0x19, 0x19, 0x1a,
	0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1b, 0x1b, 0x1b, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x19, 0x19, 0x19,
	0x18, 0x18, 0x17, 0x17, 0x16, 0x15, 0x15, 0x14, 0x13, 0x12, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0d,
	0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x02, 0x01, 0x00, 0x00, 0xff, 0xfe, 0xfd,
	0xfc, 0xfb, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0, 0xf0, 0xef, 0xee, 0xed,
	0xec, 0xec, 0xeb, 0xea, 0xea, 0xe9, 0xe9, 0xe8, 0xe8, 0xe7, 0xe7, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
	0xe6, 0xe5, 0xe5, 0xe5, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe7, 0xe7, 0xe7, 0xe8, 0xe8, 0xe9, 0xe9,
	0xea, 0xeb, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfe, 0xff,
};

static void aw8697_update_sin_rtp_data(struct aw8697 *aw8697)
{
	aw8697->sin_num = ((aw8697->duration - 500) * 100 / one_sine_data_time) + 1;
	aw8697->sin_data_lenght = (size_t)(aw8697->sin_num * one_sine_data_length);
}

static void aw8697_update_rtp_data(struct aw8697 *aw8697, const struct firmware *rtp_file)
{
	int i = 0;
	size_t data_location = 0;
	unsigned char *temp_rtp_data = NULL;

	temp_rtp_data = vmalloc(aw8697_rtp->len);
	if (temp_rtp_data == NULL) {
		pr_err("%s: vmalloc memory fail\n", __func__);
		return;
	}
	memset(temp_rtp_data, 0x00, aw8697_rtp->len);

	memcpy(temp_rtp_data, rtp_file->data, rtp_500ms_first_sine_place);
	data_location += rtp_500ms_first_sine_place;

	for (i = 1; i <= aw8697->sin_num; i++) {
		memcpy(temp_rtp_data + data_location, one_sine_data, one_sine_data_length);
		data_location += one_sine_data_length;
	}

	memcpy(temp_rtp_data + data_location, rtp_file->data + rtp_500ms_first_sine_place, rtp_file->size-rtp_500ms_first_sine_place);

	memcpy(aw8697_rtp->data, temp_rtp_data, aw8697_rtp->len);

	vfree(temp_rtp_data);
}
#endif

static void aw8697_rtp_work_routine(struct work_struct *work)
{
    const struct firmware *rtp_file = NULL;
    int ret = -1;
    struct aw8697 *aw8697 = container_of(work, struct aw8697, rtp_work);

    pr_info("%s enter\n", __func__);
	if (aw8697->rtp_routine_on) {
		pr_info("%s rtp_routine_on,ignore vibrate before done\n", __func__);
		return;
	}
    aw8697->rtp_routine_on = 1;
    /* fw loaded */

    rtp_file = aw8697_rtp_load_file_accord_f0(aw8697);
    if (!rtp_file)
    {
        pr_info("%s  aw8697->rtp_file_num[%d]\n", __func__, aw8697->rtp_file_num);
        aw8697->rtp_routine_on = 1;
        if (aw8697->device_id == 815) {
            if (aw8697->f0 <= 1670) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_165Hz[aw8697->rtp_file_num],
                aw8697->dev);
            } else if (aw8697->f0 <= 1725) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name[aw8697->rtp_file_num],
                aw8697->dev);
            } else {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_175Hz[aw8697->rtp_file_num],
                aw8697->dev);
            }
		} else if (aw8697->device_id == 81538) {
			if (aw8697->f0 <= 1470) {
				ret = request_firmware(&rtp_file,
				aw8697_rtp_name_145Hz[aw8697->rtp_file_num],
				aw8697->dev);
			} else if (aw8697->f0 <= 1525) {
				ret = request_firmware(&rtp_file,
				aw8697_rtp_name_150Hz[aw8697->rtp_file_num],
				aw8697->dev);
			} else {
				ret = request_firmware(&rtp_file,
				aw8697_rtp_name_155Hz[aw8697->rtp_file_num],
				aw8697->dev);
			}
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	} else if (aw8697->device_id == 1815) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_1815_170Hz[aw8697->rtp_file_num],
                aw8697->dev);
#endif
	} else if (aw8697->device_id == 9595) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_9595_170Hz[aw8697->rtp_file_num],
                aw8697->dev);
        } else if (aw8697->device_id == 619) {
#ifdef CONFIG_OPLUS_HAPTIC_OOS
		if (aw8697->f0 <= 1680) {
			ret = request_firmware(&rtp_file,
			aw8697_rtp_name_0619_166Hz[aw8697->rtp_file_num],
			aw8697->dev);
		} else if (aw8697->f0 <= 1720) {
			ret = request_firmware(&rtp_file,
			aw8697_rtp_name_0619_170Hz[aw8697->rtp_file_num],
			aw8697->dev);
		} else {
			ret = request_firmware(&rtp_file,
			aw8697_rtp_name_0619_174Hz[aw8697->rtp_file_num],
			aw8697->dev);
		}
#else
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name[aw8697->rtp_file_num],
                aw8697->dev);
#endif
        } else if (aw8697->device_id == 1040) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name[aw8697->rtp_file_num],
                aw8697->dev);
        } else if (aw8697->device_id == 832){
#ifndef CONFIG_OPLUS_HAPTIC_OOS
            if (aw8697->f0 <= 2280) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_0832_226Hz[aw8697->rtp_file_num],
                aw8697->dev);
            } else if (aw8697->f0 <= 2320) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_0832_230Hz[aw8697->rtp_file_num],
                aw8697->dev);
            } else {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_0832_234Hz[aw8697->rtp_file_num],
                aw8697->dev);
            }
#else
		ret = request_firmware(&rtp_file,
		aw8697_rtp_name_0832_234Hz[aw8697->rtp_file_num],
		aw8697->dev);
#endif /* CONFIG_OPLUS_HAPTIC_OOS */
        } else {
            if (aw8697->f0 <= 2280) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_0832_226Hz[aw8697->rtp_file_num],
                aw8697->dev);
            } else if (aw8697->f0 <= 2320) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_0832_230Hz[aw8697->rtp_file_num],
                aw8697->dev);
            } else if (aw8697->f0 <= 2350) {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_0832_234Hz[aw8697->rtp_file_num],
                aw8697->dev);
            } else {
                ret = request_firmware(&rtp_file,
                aw8697_rtp_name_0832_237Hz[aw8697->rtp_file_num],
                aw8697->dev);
            }
        }
        if(ret < 0)
        {
		pr_err("%s: failed to read %s, aw8697->f0 = %d\n", __func__,
			aw8697_rtp_name[aw8697->rtp_file_num], aw8697->f0);
            aw8697->rtp_routine_on = 0;
            return ;
        }
    }
    aw8697->rtp_init = 0;

    mutex_lock(&aw8697->rtp_lock);//vincent
#ifndef CONFIG_OPLUS_HAPTIC_OOS
    #ifndef OPLUS_FEATURE_CHG_BASIC
    kfree(aw8697_rtp);
    aw8697_rtp = kzalloc(rtp_file->size+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(rtp_file->size+sizeof(int));
    if (ret < 0) {
        release_firmware(rtp_file);
        mutex_unlock(&aw8697->rtp_lock);//vincent
        pr_err("%s: error allocating memory\n", __func__);

        aw8697_op_clean_status(aw8697);
        aw8697->rtp_routine_on = 0;
        return;
    }
    #endif
    aw8697_rtp->len = rtp_file->size;


    memcpy(aw8697_rtp->data, rtp_file->data, rtp_file->size);
#else
	if (aw8697->sin_add_flag == 1) {
		aw8697_update_sin_rtp_data(aw8697);
		ret = aw8697_container_init(rtp_file->size + aw8697->sin_data_lenght + sizeof(int));
		if (ret < 0) {
			release_firmware(rtp_file);
			mutex_unlock(&aw8697->rtp_lock);
			pr_err("%s: error allocating memory\n", __func__);

			aw8697_op_clean_status(aw8697);
			aw8697->rtp_routine_on = 0;
			return;
		}
		aw8697_rtp->len = rtp_file->size + aw8697->sin_data_lenght;
	} else {
		ret = aw8697_container_init(rtp_file->size + sizeof(int));
		if (ret < 0) {
			release_firmware(rtp_file);
			mutex_unlock(&aw8697->rtp_lock);
			pr_err("%s: error allocating memory\n", __func__);

			aw8697_op_clean_status(aw8697);
			aw8697->rtp_routine_on = 0;
			return;
		}
		aw8697_rtp->len = rtp_file->size;
	}

	if (!aw8697_rtp) {
		release_firmware(rtp_file);
		pr_err("%s: error allocating memory\n", __func__);
		aw8697_op_clean_status(aw8697);
		aw8697->rtp_routine_on = 0;
		mutex_unlock(&aw8697->rtp_lock);
		return;
	}

	pr_info("%s: rtp file [%s] size = %d\n", __func__,
		aw8697_rtp_name[aw8697->rtp_file_num], aw8697_rtp->len);
	if (aw8697->sin_add_flag == 1) {
		aw8697_update_rtp_data(aw8697, rtp_file);
		aw8697->sin_add_flag = 0;
	} else
		memcpy(aw8697_rtp->data, rtp_file->data, rtp_file->size);
#endif
    mutex_unlock(&aw8697->rtp_lock);//vincent
    release_firmware(rtp_file);

	if (aw8697->device_id == 815) {
		pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
		aw8697_rtp_name[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
	} else if (aw8697->device_id == 81538) {
		pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
		aw8697_rtp_name_150Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	} else if (aw8697->device_id == 1815) {
        pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
            aw8697_rtp_name_1815_170Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
#endif
	} else if (aw8697->device_id == 619) {
#ifdef CONFIG_OPLUS_HAPTIC_OOS
		if (aw8697->f0 <= 1680) {
			pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
			aw8697_rtp_name_0619_166Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
		} else if (aw8697->f0 <= 1720) {
			pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
			aw8697_rtp_name_0619_170Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
		} else {
			pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
			aw8697_rtp_name_0619_174Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
		}
#else
		pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
		aw8697_rtp_name[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
#endif
	} else if (aw8697->device_id == 1040) {
                pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
                aw8697_rtp_name[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
	} else if (aw8697->device_id == 9595) {
		pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
		aw8697_rtp_name_9595_170Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
	} else {
#ifdef CONFIG_OPLUS_HAPTIC_OOS
		pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
		aw8697_rtp_name_0832_234Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
#else
		pr_info("%s: rtp file [%s] size = %d, f0 = %d\n", __func__,
		aw8697_rtp_name_0832_230Hz[aw8697->rtp_file_num], aw8697_rtp->len, aw8697->f0);
#endif
	}


    aw8697->rtp_init = 1;
    mutex_lock(&aw8697->lock);

    /* set clock to stand */
    aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD);

    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);
    if(aw8697->rtp_routine_on) {
        /* rtp mode config */
        aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);

        aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
                AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
        /* haptic start */
        //aw8697_haptic_start(aw8697);

        aw8697_haptic_rtp_init(aw8697);
    }
    mutex_unlock(&aw8697->lock);

    aw8697_op_clean_status(aw8697);
    aw8697->rtp_routine_on = 0;
}

static void aw8697_rtp_single_cycle_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, rtp_single_cycle_work);
    const struct firmware *rtp_file;
    int ret = -1;
    unsigned int buf_len = 0;
    unsigned char reg_val = 0;
  //  unsigned int  pass_cont=1;
    unsigned int  cyc_cont=150;
    aw8697->rtp_cnt = 0;
    aw8697->osc_cali_flag =1;

    pr_info("%s enter\n", __func__);
    printk("%s---%d\n",__func__,__LINE__);
    /* fw loaded */
    if(aw8697->rtp_loop == 0xFF){
        ret = request_firmware(&rtp_file,aw8697_rtp_name[aw8697->rtp_serial[1]],aw8697->dev);
    } else{
        printk("%s A single cycle : err value\n",__func__);
    }
    if(ret < 0)
    {
        pr_err("%s: failed to read %s\n", __func__,
                aw8697_rtp_name[aw8697->rtp_serial[1]]);
        return;
    }
    aw8697->rtp_init = 0;
    #ifndef OPLUS_FEATURE_CHG_BASIC
    kfree(aw8697_rtp);
    printk("%s---%d\n",__func__,__LINE__);
    aw8697_rtp = kzalloc(rtp_file->size+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        release_firmware(rtp_file);
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(rtp_file->size+sizeof(int));
    if (ret < 0) {
        release_firmware(rtp_file);
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #endif
    aw8697_rtp->len = rtp_file->size;
    pr_info("%s: rtp file [%s] size = %d\n", __func__,
            aw8697_rtp_name[aw8697->rtp_serial[1]], aw8697_rtp->len);
    memcpy(aw8697_rtp->data, rtp_file->data, rtp_file->size);
    printk("%s---%d\n",__func__,__LINE__);
    release_firmware(rtp_file);

    //aw8697->rtp_init = 1; //Don't enter aw8697_irq,because osc calibration use while(1) function
    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);
    printk("%s---%d\n",__func__,__LINE__);
    /* rtp mode config */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
    /* haptic start */
    aw8697_haptic_start(aw8697);

    while(aw8697->rtp_cycle_flag == 1 ){
        if(!aw8697_haptic_rtp_get_fifo_afi(aw8697)){
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr>>2);
            }

            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            aw8697->rtp_cnt += buf_len;

            if(aw8697->rtp_cnt == aw8697_rtp->len) {
            //  pr_info("%s: rtp update complete,enter again\n", __func__);
              aw8697->rtp_cnt = 0;
            }
        }else{
                aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
                if(reg_val & AW8697_BIT_SYSST_DONES) {
                      pr_info("%s chip playback done\n", __func__);
                     break;
                }
                while(1){
                    if(aw8697_haptic_rtp_get_fifo_aei(aw8697)){
                        printk("-----%s---%d----while(1)--\n",__func__,__LINE__);
                    break;
                    }
                    cyc_cont--;
                    if(cyc_cont == 0){
                        cyc_cont = 150;
                    break;
                }
              }
            }/*else*/
        }/*while*/
    pr_info("%s exit\n", __func__);
}

static void aw8697_rtp_regroup_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, rtp_regroup_work);
    const struct firmware *rtp_file;
    unsigned int buf_len = 0;
    unsigned char reg_val = 0;
    unsigned int  cyc_cont=150;
    int rtp_len_tmp =0;
    int aw8697_rtp_len = 0;
    int i, ret = 0;
    unsigned char *p = NULL;
    aw8697->rtp_cnt = 0;
    aw8697->osc_cali_flag =1;
    pr_info("%s enter\n", __func__);

    for(i=1;i<=aw8697->rtp_serial[0];i++){
        if((request_firmware(&rtp_file,aw8697_rtp_name[aw8697->rtp_serial[i]],aw8697->dev)) < 0){
             pr_err("%s: failed to read %s\n", __func__,aw8697_rtp_name[aw8697->rtp_serial[i]]);
        }
        aw8697_rtp_len =rtp_len_tmp + rtp_file->size;
        rtp_len_tmp = aw8697_rtp_len;
        pr_info("%s:111 rtp file [%s] size = %d\n", __func__,
            aw8697_rtp_name[aw8697->rtp_serial[i]], aw8697_rtp_len);
        release_firmware(rtp_file);
    }

    rtp_len_tmp = 0;
    aw8697->rtp_init = 0;
    #ifndef OPLUS_FEATURE_CHG_BASIC
    kfree(aw8697_rtp);
    aw8697_rtp = kzalloc(aw8697_rtp_len+sizeof(int), GFP_KERNEL);
    if (!aw8697_rtp) {
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #else
    ret = aw8697_container_init(aw8697_rtp_len+sizeof(int));
    if (ret < 0) {
        pr_err("%s: error allocating memory\n", __func__);
        return;
    }
    #endif
    aw8697_rtp->len = aw8697_rtp_len;
    for(i=1;i<=aw8697->rtp_serial[0];i++){
        if((request_firmware(&rtp_file,aw8697_rtp_name[aw8697->rtp_serial[i]],aw8697->dev)) < 0){
             pr_err("%s: failed to read %s\n", __func__,aw8697_rtp_name[aw8697->rtp_serial[i]]);
        }
    p = &(aw8697_rtp->data[0]) + rtp_len_tmp;
    memcpy( p , rtp_file->data, rtp_file->size);
    rtp_len_tmp += rtp_file->size;
    release_firmware(rtp_file);
    pr_info("%s: rtp file [%s]\n", __func__,
        aw8697_rtp_name[aw8697->rtp_serial[i]]);
    }

    //for(j=0; j<aw8697_rtp_len; j++) {
    //    printk("%s: addr:%d, data:%d\n", __func__, j, aw8697_rtp->data[j]);
    //   }
    //aw8697->rtp_init = 1; //Don't enter aw8697_irq,because osc calibration use while(1) function
    /* gain */
    aw8697_haptic_ram_vbat_comp(aw8697, false);
    /* rtp mode config */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);
    /* haptic start */
    aw8697_haptic_start(aw8697);

    while(aw8697->rtp_cycle_flag == 1 ){
        if(!aw8697_haptic_rtp_get_fifo_afi(aw8697)){
            if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
            } else {
                buf_len = (aw8697->ram.base_addr>>2);
            }
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA,
                &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
            aw8697->rtp_cnt += buf_len;
            if(aw8697->rtp_cnt == aw8697_rtp->len) {
                pr_info("%s: rtp update complete\n", __func__);
                aw8697->rtp_cnt = 0;
                aw8697->rtp_loop--;
                if(aw8697->rtp_loop == 0)
                    return;
            }
        }else{
                aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
                if(reg_val & AW8697_BIT_SYSST_DONES) {
                      pr_info("%s chip playback done\n", __func__);
                     break;
                }
                while(1){
                    if(aw8697_haptic_rtp_get_fifo_aei(aw8697)){
                        printk("-----%s---%d----while(1)--\n",__func__,__LINE__);
                        break;
                    }
                    cyc_cont--;
                    if(cyc_cont == 0){
                        cyc_cont = 150;
                    break;
                }
              }
            }/*else*/
        }/*while*/
    pr_info("%s exit\n", __func__);
}

static int aw8697_rtp_regroup_work(struct aw8697 *aw8697)
{
    aw8697_haptic_stop(aw8697);
    if(aw8697->rtp_serial[0] > 0){
      printk("%s---%d\n",__func__,__LINE__);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
            if(aw8697->rtp_serial[0] <= (sizeof(aw8697_rtp_name)/AW8697_RTP_NAME_MAX)) {
                if(aw8697->rtp_loop == 0xFF){   //if aw8697->rtp_loop = 0xff then  single cycle ;
                    aw8697->rtp_cycle_flag = 1;
                    schedule_work(&aw8697->rtp_single_cycle_work);
              } else if( (aw8697->rtp_loop > 0 ) && (aw8697->rtp_loop < 0xff) ) {
                    aw8697->rtp_cycle_flag = 1;
                    schedule_work(&aw8697->rtp_regroup_work);
              } else {
                    printk("%s---%d\n",__func__,__LINE__);
              }
            }
    } else {
      aw8697->rtp_cycle_flag  = 0;
    }
    return 0;
}

/*****************************************************
 *
 * haptic - audio
 *
 *****************************************************/
#ifdef OP_AW_DEBUG
#if 0
static int aw8697_haptic_audio_uevent_report_scan(struct aw8697 *aw8697, bool flag)
{
#ifdef AISCAN_CTRL
    char *envp[2];

    pr_info("%s: flag=%d\n", __func__, flag);

    if (flag) {
        envp[0] = "AI_SCAN=1";
    } else {
        envp[0] = "AI_SCAN=0";
    }

    envp[1] = NULL;

    kobject_uevent_env(&aw8697->dev->kobj, KOBJ_CHANGE, envp);
#endif
    return 0;
}
#endif
static int aw8697_haptic_audio_ctr_list_insert(
    struct haptic_audio *haptic_audio, struct haptic_ctr *haptic_ctr)
{
    struct haptic_ctr *p_new = NULL;

    p_new = (struct haptic_ctr *)kzalloc(
        sizeof(struct haptic_ctr), GFP_KERNEL);
    if (p_new == NULL ) {
        pr_err("%s: kzalloc memory fail\n", __func__);
        return -1;
    }
    /* update new list info */
    p_new->cnt = haptic_ctr->cnt;
    p_new->cmd = haptic_ctr->cmd;
    p_new->play = haptic_ctr->play;
    p_new->wavseq = haptic_ctr->wavseq;
    p_new->loop = haptic_ctr->loop;
    p_new->gain = haptic_ctr->gain;

    INIT_LIST_HEAD(&(p_new->list));
    list_add(&(p_new->list), &(haptic_audio->ctr_list));

    return 0;
}

static int aw8697_haptic_audio_ctr_list_clear(struct haptic_audio *haptic_audio)
{
    struct haptic_ctr *p_ctr = NULL;
    struct haptic_ctr *p_ctr_bak = NULL;

    list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
                     &(haptic_audio->ctr_list), list) {
        list_del(&p_ctr->list);
        kfree(p_ctr);
    }

    return 0;
}
static int aw8697_haptic_audio_init(struct aw8697 *aw8697)
{

    pr_debug("%s enter\n", __func__);

    aw8697_haptic_set_wav_seq(aw8697, 0x01, 0x00);
    return 0;
}
static int aw8697_haptic_audio_off(struct aw8697 *aw8697)
{
    pr_debug("%s enter\n", __func__);
    mutex_lock(&aw8697->lock);
    aw8697_haptic_set_gain(aw8697, 0x80);
    aw8697_haptic_stop(aw8697);
    mutex_unlock(&aw8697->lock);
    aw8697_haptic_audio_ctr_list_clear(&aw8697->haptic_audio);
    //mutex_lock(&aw8697->haptic_audio.lock);
    aw8697->gun_type = 0xff;
    aw8697->bullet_nr =0;
    aw8697->gun_mode =0;
    //mutex_unlock(&aw8697->haptic_audio.lock);

    return 0;
}
#endif

/*****************************************************
 *
 * haptic - audio
 *
 *****************************************************/
static enum hrtimer_restart aw8697_haptic_audio_timer_func(struct hrtimer *timer)
{
    struct aw8697 *aw8697 = container_of(timer, struct aw8697, haptic_audio.timer);

    pr_debug("%s enter\n", __func__);
    schedule_work(&aw8697->haptic_audio.work);

    hrtimer_start(&aw8697->haptic_audio.timer,
            ktime_set(aw8697->haptic_audio.timer_val/1000000,
                    (aw8697->haptic_audio.timer_val%1000000)*1000),
            HRTIMER_MODE_REL);
    return HRTIMER_NORESTART;
}

static void aw8697_haptic_audio_work_routine(struct work_struct *work)
{
    struct aw8697 *aw8697 =
        container_of(work, struct aw8697, haptic_audio.work);
    struct haptic_audio *haptic_audio = NULL;
    struct haptic_ctr *p_ctr = NULL;
    struct haptic_ctr *p_ctr_bak = NULL;
    unsigned int ctr_list_flag = 0;
    unsigned int ctr_list_input_cnt = 0;
    unsigned int ctr_list_output_cnt = 0;
    unsigned int ctr_list_diff_cnt = 0;
    unsigned int ctr_list_del_cnt = 0;

    //int rtp_is_going_on = 0;

    pr_debug("%s enter\n", __func__);

    haptic_audio = &(aw8697->haptic_audio);
    mutex_lock(&aw8697->haptic_audio.lock);
    memset(&aw8697->haptic_audio.ctr, 0, sizeof(struct haptic_ctr));
    ctr_list_flag = 0;
    list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
                     &(haptic_audio->ctr_list), list) {
        ctr_list_flag = 1;
        break;
    }
    if (ctr_list_flag == 0)
        pr_debug("%s: ctr list empty\n", __func__);
    if (ctr_list_flag == 1) {
        list_for_each_entry_safe(p_ctr, p_ctr_bak,
                     &(haptic_audio->ctr_list), list) {
            ctr_list_input_cnt = p_ctr->cnt;
            break;
        }
        list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
                         &(haptic_audio->ctr_list),
                         list) {
            ctr_list_output_cnt = p_ctr->cnt;
            break;
        }
        if (ctr_list_input_cnt > ctr_list_output_cnt) {
            ctr_list_diff_cnt =
                ctr_list_input_cnt - ctr_list_output_cnt;
        }
        if (ctr_list_input_cnt < ctr_list_output_cnt) {
            ctr_list_diff_cnt =
                32 + ctr_list_input_cnt - ctr_list_output_cnt;
        }
        if (ctr_list_diff_cnt > 2) {
            list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
                             &(haptic_audio->
                               ctr_list), list) {
                if ((p_ctr->play == 0)
                    && (AW8697_HAPTIC_CMD_ENABLE ==
                    (AW8697_HAPTIC_CMD_HAPTIC & p_ctr->
                     cmd))) {
                    list_del(&p_ctr->list);
                    kfree(p_ctr);
                    ctr_list_del_cnt++;
                }
                if (ctr_list_del_cnt == ctr_list_diff_cnt)
                    break;
            }
        }

    }

    /* get the last data from list */
    list_for_each_entry_safe_reverse(p_ctr, p_ctr_bak,
                     &(haptic_audio->ctr_list), list) {
        aw8697->haptic_audio.ctr.cnt = p_ctr->cnt;
        aw8697->haptic_audio.ctr.cmd = p_ctr->cmd;
        aw8697->haptic_audio.ctr.play = p_ctr->play;
        aw8697->haptic_audio.ctr.wavseq = p_ctr->wavseq;
        aw8697->haptic_audio.ctr.loop = p_ctr->loop;
        aw8697->haptic_audio.ctr.gain = p_ctr->gain;
        list_del(&p_ctr->list);
        kfree(p_ctr);
        break;
    }
    if (aw8697->haptic_audio.ctr.play) {
        pr_debug
        ("%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
         __func__, aw8697->haptic_audio.ctr.cnt,
         aw8697->haptic_audio.ctr.cmd,
         aw8697->haptic_audio.ctr.play,
         aw8697->haptic_audio.ctr.wavseq,
         aw8697->haptic_audio.ctr.loop,
         aw8697->haptic_audio.ctr.gain);
    }

    /* rtp mode jump */
    if (aw8697->rtp_routine_on) {
        mutex_unlock(&aw8697->haptic_audio.lock);
        return;
    }

    mutex_unlock(&aw8697->haptic_audio.lock);

    /* aw8697 haptic play control */
    if ((aw8697->haptic_audio.ctr.cmd & AW8697_HAPTIC_CMD_HAPTIC) == AW8697_HAPTIC_CMD_ENABLE) {
        if (aw8697->haptic_audio.ctr.play == AW8697_HAPTIC_PLAY_ENABLE) {
            pr_info("%s: haptic_audio_play_start\n", __func__);
            mutex_lock(&aw8697->lock);
            aw8697_haptic_stop(aw8697);
            aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RAM_MODE);

            aw8697_haptic_set_wav_seq(aw8697, 0x00,
                          aw8697->haptic_audio.ctr.wavseq);
            aw8697_haptic_set_wav_seq(aw8697, 0x01, 0x00);

            aw8697_haptic_set_wav_loop(aw8697, 0x00,
                           aw8697->haptic_audio.ctr.loop);

            aw8697_haptic_set_gain(aw8697,
                           aw8697->haptic_audio.ctr.gain);

            aw8697_haptic_start(aw8697);
            mutex_unlock(&aw8697->lock);
        } else if (AW8697_HAPTIC_PLAY_STOP ==
               aw8697->haptic_audio.ctr.play) {
            mutex_lock(&aw8697->lock);
            aw8697_haptic_stop(aw8697);
            mutex_unlock(&aw8697->lock);
        } else if (AW8697_HAPTIC_PLAY_GAIN ==
               aw8697->haptic_audio.ctr.play) {
            mutex_lock(&aw8697->lock);
            aw8697_haptic_set_gain(aw8697,
                           aw8697->haptic_audio.ctr.gain);
            mutex_unlock(&aw8697->lock);
        }
    }
}

//hch 20190917
static ssize_t aw8697_gun_type_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8697->gun_type);
}

static ssize_t aw8697_gun_type_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_debug("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
    aw8697->gun_type = val;
    mutex_unlock(&aw8697->lock);
    return count;
}

//hch 20190917
static ssize_t aw8697_bullet_nr_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8697->bullet_nr);
}

static ssize_t aw8697_bullet_nr_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_debug("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
    aw8697->bullet_nr = val;
    mutex_unlock(&aw8697->lock);
    return count;
}

//hch 20190917
static ssize_t aw8697_gun_mode_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8697->gun_mode);
}
static ssize_t aw8697_gun_mode_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_debug("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
    aw8697->gun_mode = val;
    mutex_unlock(&aw8697->lock);
    return count;
}


/*****************************************************
 *
 * haptic cont
 *
 *****************************************************/
static int aw8697_haptic_cont(struct aw8697 *aw8697)
{
    pr_info("%s enter\n", __func__);

    /* work mode */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_CONT_MODE);

    /* preset f0 */
    aw8697->f0_pre = aw8697->f0;
    aw8697_haptic_set_f0_preset(aw8697);

    /* lpf */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_FC_MASK, AW8697_BIT_DATCTRL_FC_1000HZ);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_LPF_ENABLE_MASK, AW8697_BIT_DATCTRL_LPF_ENABLE);

    /* cont config */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_ZC_DETEC_MASK, AW8697_BIT_CONT_CTRL_ZC_DETEC_ENABLE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_WAIT_PERIOD_MASK, AW8697_BIT_CONT_CTRL_WAIT_1PERIOD);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_MODE_MASK, AW8697_BIT_CONT_CTRL_BY_GO_SIGNAL);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_EN_CLOSE_MASK, AW8697_CONT_PLAYBACK_MODE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_F0_DETECT_MASK, AW8697_BIT_CONT_CTRL_F0_DETECT_DISABLE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_O2C_MASK, AW8697_BIT_CONT_CTRL_O2C_DISABLE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_AUTO_BRK_MASK, AW8697_BIT_CONT_CTRL_AUTO_BRK_ENABLE);

    /* TD time */
    aw8697_i2c_write(aw8697, AW8697_REG_TD_H, (unsigned char)(aw8697->cont_td>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_TD_L, (unsigned char)(aw8697->cont_td>>0));
    aw8697_i2c_write(aw8697, AW8697_REG_TSET, 0x12);

    /* zero cross */
    aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_H, (unsigned char)(aw8697->cont_zc_thr>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_L, (unsigned char)(aw8697->cont_zc_thr>>0));

    /* bemf */
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_H, 0x10);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_L, 0x08);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_H, 0x03);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_L, 0xf8);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_BEMF_NUM,
            AW8697_BIT_BEMF_NUM_BRK_MASK, aw8697->cont_num_brk);
    aw8697_i2c_write(aw8697, AW8697_REG_TIME_NZC, 0x23);    // 35*171us=5.985ms

    /* f0 driver level */
    aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL, aw8697->cont_drv_lvl);
    aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL_OV, aw8697->cont_drv_lvl_ov);

    /* cont play go */
    aw8697_haptic_play_go(aw8697, true);

    return 0;
}

static int aw8697_dump_f0_registers(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, 0x04, &reg_val);
    pr_info("%s reg_0x04 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x46, &reg_val);
    pr_info("%s reg_0x46 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x48, &reg_val);
    pr_info("%s reg_0x48 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x49, &reg_val);
    pr_info("%s reg_0x49 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x4A, &reg_val);
    pr_info("%s reg_0x4A = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x68, &reg_val);
    pr_info("%s reg_0x68 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x69, &reg_val);
    pr_info("%s reg_0x69 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6a, &reg_val);
    pr_info("%s reg_0x6a = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6b, &reg_val);
    pr_info("%s reg_0x6b = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6D, &reg_val);
    pr_info("%s reg_0x6D = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x6f, &reg_val);
    pr_info("%s reg_0x6f = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x70, &reg_val);
    pr_info("%s reg_0x70 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x71, &reg_val);
    pr_info("%s reg_0x71 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x7D, &reg_val);
    pr_info("%s reg_0x7D = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x7E, &reg_val);
    pr_info("%s reg_0x7E = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x7F, &reg_val);
    pr_info("%s reg_0x7F = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x36, &reg_val);
    pr_info("%s reg_0x36 = 0x%02x\n", __func__, reg_val);

    aw8697_i2c_read(aw8697, 0x5b, &reg_val);
    pr_info("%s reg_0x5b = 0x%02x\n", __func__, reg_val);

    return 0;
}

/*****************************************************
 *
 * haptic f0 cali
 *
 *****************************************************/
static int aw8697_haptic_get_f0(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;
    unsigned char f0_pre_num = 0;
    unsigned char f0_wait_num = 0;
    unsigned char f0_repeat_num = 0;
    unsigned char f0_trace_num = 0;
    unsigned int t_f0_ms = 0;
    unsigned int t_f0_trace_ms = 0;
    unsigned int f0_cali_cnt = 50;

    pr_info("%s enter\n", __func__);

    aw8697->f0 = aw8697->f0_pre;

    /* f0 calibrate work mode */
    aw8697_haptic_stop(aw8697);
    aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, 0);
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_CONT_MODE);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_EN_CLOSE_MASK, AW8697_BIT_CONT_CTRL_OPEN_PLAYBACK);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_F0_DETECT_MASK, AW8697_BIT_CONT_CTRL_F0_DETECT_ENABLE);

    /* LPF */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_FC_MASK, AW8697_BIT_DATCTRL_FC_1000HZ);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DATCTRL,
            AW8697_BIT_DATCTRL_LPF_ENABLE_MASK, AW8697_BIT_DATCTRL_LPF_ENABLE);

    /* LRA OSC Source */
    if(aw8697->f0_cali_flag == AW8697_HAPTIC_CALI_F0) {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_REG);
    } else {
        aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
                AW8697_BIT_ANACTRL_LRA_SRC_MASK, AW8697_BIT_ANACTRL_LRA_SRC_EFUSE);
    }

    /* preset f0 */
    aw8697_haptic_set_f0_preset(aw8697);

    /* beme config */
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_H, 0x10);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHH_L, 0x08);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_H, 0x03);
    aw8697_i2c_write(aw8697, AW8697_REG_BEMF_VTHL_L, 0xf8);

    /* f0 driver level */
    aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL, aw8697->cont_drv_lvl);

    /* f0 trace parameter */
    f0_pre_num = 0x05;
    f0_wait_num = 0x03;
    f0_repeat_num = 0x02;
    f0_trace_num = 0x0f;
    aw8697_i2c_write(aw8697, AW8697_REG_NUM_F0_1, (f0_pre_num<<4)|(f0_wait_num<<0));
    aw8697_i2c_write(aw8697, AW8697_REG_NUM_F0_2, (f0_repeat_num<<0));
    aw8697_i2c_write(aw8697, AW8697_REG_NUM_F0_3, (f0_trace_num<<0));

    /* clear aw8697 interrupt */
    ret = aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    aw8697_dump_f0_registers(aw8697);   // add by huangtongfeng

    /* play go and start f0 calibration */
    aw8697_haptic_play_go(aw8697, true);

    /* f0 trace time */
    t_f0_ms = 1000*10/aw8697->f0_pre;
    t_f0_trace_ms = t_f0_ms * (f0_pre_num + f0_wait_num + (f0_trace_num+f0_wait_num)*(f0_repeat_num-1));
    msleep(t_f0_trace_ms);

	for(i = 0; i < f0_cali_cnt; i++) {
#ifdef CONFIG_OPLUS_HAPTIC_OOS
		ret = aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &reg_val);
		/* f0 calibrate done */
		if ((reg_val & 0x0f) == 0x00) {
#else
			ret = aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
		/* f0 calibrate done */
		if(reg_val & 0x01) {
#endif
			aw8697_haptic_read_f0(aw8697);
			aw8697_haptic_read_beme(aw8697);
			break;
		}
		msleep(10);
		pr_info("%s f0 cali sleep 10ms\n", __func__);
	}

    if(i == f0_cali_cnt) {
        ret = -1;
    } else {
        ret = 0;
    }
    aw8697_dump_f0_registers(aw8697);   // add by huangtongfeng

    /* restore default config */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_EN_CLOSE_MASK, AW8697_CONT_PLAYBACK_MODE);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_CONT_CTRL,
            AW8697_BIT_CONT_CTRL_F0_DETECT_MASK, AW8697_BIT_CONT_CTRL_F0_DETECT_DISABLE);

    return ret;
}

static int aw8697_haptic_f0_calibration(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char reg_val = 0;
    unsigned int f0_limit = 0;
    char f0_cali_lra = 0;
    int f0_cali_step = 0;
#ifndef CONFIG_OPLUS_HAPTIC_OOS
	int f0_dft_step = 0;
#endif

    pr_err("%s enter\n", __func__);

    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;
    aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, 0);
    if(aw8697_haptic_get_f0(aw8697)) {
        pr_err("%s get f0 error, user defafult f0\n", __func__);
    } else {
        /* max and min limit */
        f0_limit = aw8697->f0;
        pr_err("%s get f0=%d\n", __func__, f0_limit);

#ifdef OPLUS_FEATURE_CHG_BASIC
        if (aw8697->device_id == 832 || aw8697->device_id == 833) {
            if(aw8697->f0*100 < AW8697_0832_HAPTIC_F0_PRE*(100-AW8697_0832_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0832_HAPTIC_F0_PRE*(100-AW8697_0832_HAPTIC_F0_CALI_PERCEN)/100;
            }
            if(aw8697->f0*100 > AW8697_0832_HAPTIC_F0_PRE*(100+AW8697_0832_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0832_HAPTIC_F0_PRE*(100+AW8697_0832_HAPTIC_F0_CALI_PERCEN)/100;
            }
		} else if (aw8697->device_id == 81538) {
			if(aw8697->f0*100 < AW8697_081538_HAPTIC_F0_PRE*(100-AW8697_081538_HAPTIC_F0_CALI_PERCEN)) {
				f0_limit = AW8697_081538_HAPTIC_F0_PRE*(100-AW8697_081538_HAPTIC_F0_CALI_PERCEN)/100;
			}
			if(aw8697->f0*100 > AW8697_081538_HAPTIC_F0_PRE*(100+AW8697_081538_HAPTIC_F0_CALI_PERCEN)) {
				f0_limit = AW8697_081538_HAPTIC_F0_PRE*(100+AW8697_081538_HAPTIC_F0_CALI_PERCEN)/100;
			}
        } else {
            if(aw8697->f0*100 < AW8697_0815_HAPTIC_F0_PRE*(100-AW8697_0815_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0815_HAPTIC_F0_PRE*(100-AW8697_0815_HAPTIC_F0_CALI_PERCEN)/100;
            }
            if(aw8697->f0*100 > AW8697_0815_HAPTIC_F0_PRE*(100+AW8697_0815_HAPTIC_F0_CALI_PERCEN)) {
                f0_limit = AW8697_0815_HAPTIC_F0_PRE*(100+AW8697_0815_HAPTIC_F0_CALI_PERCEN)/100;
            }
        }
#else
        if(aw8697->f0*100 < AW8697_HAPTIC_F0_PRE*(100-AW8697_HAPTIC_F0_CALI_PERCEN)) {
            f0_limit = AW8697_HAPTIC_F0_PRE*(100-AW8697_HAPTIC_F0_CALI_PERCEN)/100;
        }
        if(aw8697->f0*100 > AW8697_HAPTIC_F0_PRE*(100+AW8697_HAPTIC_F0_CALI_PERCEN)) {
            f0_limit = AW8697_HAPTIC_F0_PRE*(100+AW8697_HAPTIC_F0_CALI_PERCEN)/100;
        }
#endif

#ifdef CONFIG_OPLUS_HAPTIC_OOS
		/* calculate cali step */
		f0_cali_step = 100000 * ((int)f0_limit - (int)aw8697->f0_pre) / ((int)f0_limit * 25);
		pr_info("%s  line=%d f0_cali_step=%d\n", __func__, __LINE__, f0_cali_step);
		pr_info("%s line=%d  f0_limit=%d\n", __func__, __LINE__, (int)f0_limit);
		pr_info("%s line=%d  aw8697->f0_pre=%d\n", __func__, __LINE__, (int)aw8697->f0_pre);

		if (f0_cali_step >= 0) {	/*f0_cali_step >= 0 */
			if (f0_cali_step % 10 >= 5)
				f0_cali_step = f0_cali_step / 10 + 1 + 32;
			else
				f0_cali_step = f0_cali_step / 10 + 32;
		} else {	/*f0_cali_step < 0 */
			if (f0_cali_step % 10 <= -5)
				f0_cali_step = 32 + (f0_cali_step / 10 - 1);
			else
				f0_cali_step = 32 + f0_cali_step / 10;
		}

		if (f0_cali_step > 31)
			f0_cali_lra = (char)f0_cali_step - 32;
		else
			f0_cali_lra = (char)f0_cali_step + 32;
		pr_info("%s f0_cali_lra=%d\n", __func__, (int)f0_cali_lra);

		aw8697->clock_system_f0_cali_lra = (int)f0_cali_lra;
		pr_info("%s f0_cali_lra=%d\n", __func__, (int)f0_cali_lra);
#else
		/* calculate cali step */
		f0_cali_step = 10000*((int)f0_limit-(int)aw8697->f0_pre)/((int)f0_limit*25);
		pr_debug("%s f0_cali_step=%d\n", __func__, f0_cali_step);

		/* get default cali step */
		aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &reg_val);
		if (reg_val & 0x20) {
			f0_dft_step = reg_val - 0x40;
		} else {
			f0_dft_step = reg_val;
		}
		pr_debug("%s f0_dft_step=%d\n", __func__, f0_dft_step);

		/* get new cali step */
		f0_cali_step += f0_dft_step;
		pr_debug("%s f0_cali_step=%d\n", __func__, f0_cali_step);

		if (f0_cali_step > 31) {
			f0_cali_step = 31;
		} else if (f0_cali_step < -32) {
			f0_cali_step = -32;
		}
		f0_cali_lra = (char)f0_cali_step;
		pr_debug("%s f0_cali_lra=%d\n", __func__, f0_cali_lra);

		/* get cali step complement code*/
		if (f0_cali_step < 0) {
			f0_cali_lra += 0x40;
		}
		pr_debug("%s reg f0_cali_lra=%d\n", __func__, f0_cali_lra);
#endif

        /* update cali step */
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, (char)f0_cali_lra);
        aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &reg_val);
        aw8697->clock_system_f0_cali_lra = f0_cali_lra;
        pr_info("%s final trim_lra=0x%02x\n", __func__, reg_val);
    }

    if(aw8697_haptic_get_f0(aw8697)) {
        pr_err("%s get f0 error, user defafult f0\n", __func__);
    }

    /* restore default work mode */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    aw8697->play_mode = AW8697_HAPTIC_RAM_MODE;
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_PLAY_MODE_MASK, AW8697_BIT_SYSCTRL_PLAY_MODE_RAM);
    aw8697_haptic_stop(aw8697);

    return ret;
}
#ifdef AAC_RICHTAP
static void haptic_clean_buf(struct aw8697 *aw8697, int status)
{
    struct mmap_buf_format *opbuf = aw8697->start_buf;
    int i = 0;

    for(i = 0; i < RICHTAP_MMAP_BUF_SUM; i++)
    {
        opbuf->status = status;
        opbuf = opbuf->kernel_next;
    }
}

static void rtp_work_proc(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, haptic_rtp_work);
    struct mmap_buf_format *opbuf = aw8697->start_buf;
    uint32_t count = 100;
    uint8_t reg_val = 0x10;
	ktime_t t_start;
	s64 elapsed_us;

    opbuf = aw8697->start_buf;
    count = 100;
    while(true && count--)
    {
        if(opbuf->status == MMAP_BUF_DATA_VALID) {
			mutex_lock(&aw8697->lock);
            aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);
            aw8697_haptic_set_rtp_aei(aw8697, true);
            aw8697_interrupt_clear(aw8697);
            aw8697_haptic_start(aw8697);
			mutex_unlock(&aw8697->lock);
            break;
        }
        else {
            msleep(1);
        }
    }

	t_start = ktime_get();
    reg_val = 0x10;
    while(true)
    {
        elapsed_us = ktime_us_delta(ktime_get(), t_start);
        if (elapsed_us > 800000) {
			pr_info("Failed ! %s endless loop\n", __func__);
			break;
        }
        if(reg_val & 0x01 || (aw8697->done_flag == true) || (opbuf->status == MMAP_BUF_DATA_FINISHED) \
                          || (opbuf->status == MMAP_BUF_DATA_INVALID)) {
            break;
        }
        else if(opbuf->status == MMAP_BUF_DATA_VALID && (reg_val & 0x01 << 4)) {
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA, opbuf->data, opbuf->length);
			memset(opbuf->data, 0, opbuf->length);
            opbuf->status = MMAP_BUF_DATA_INVALID;
            opbuf = opbuf->kernel_next;
			t_start = ktime_get();
        }
        else {
            msleep(1);
        }
        aw8697_i2c_read(aw8697, AW8697_REG_SYSST, &reg_val);
    }
    aw8697_haptic_set_rtp_aei(aw8697, false);
    aw8697->haptic_rtp_mode = false;
}
#endif

/*****************************************************
 *
 * haptic fops
 *
 *****************************************************/
static int aw8697_file_open(struct inode *inode, struct file *file)
{
    if (!try_module_get(THIS_MODULE))
        return -ENODEV;

    file->private_data = (void*)g_aw8697;

    return 0;
}

static int aw8697_file_release(struct inode *inode, struct file *file)
{
    file->private_data = (void*)NULL;

    module_put(THIS_MODULE);

    return 0;
}

static long aw8697_file_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct aw8697 *aw8697 = (struct aw8697 *)file->private_data;
#ifdef AAC_RICHTAP
    uint32_t tmp;
#endif
    int ret = 0;

    dev_info(aw8697->dev, "%s: cmd=0x%x, arg=0x%lx\n",
              __func__, cmd, arg);

    mutex_lock(&aw8697->lock);
#ifdef AAC_RICHTAP
    switch(cmd)
    {
        case RICHTAP_GET_HWINFO:
            tmp = RICHTAP_AW_8697;
            if(copy_to_user((void __user *)arg, &tmp, sizeof(uint32_t)))
                ret = -EFAULT;
            break;
        case RICHTAP_RTP_MODE:
            aw8697_haptic_stop(aw8697);
			if (copy_from_user(aw8697->rtp_ptr, (void __user *)arg,
                              RICHTAP_MMAP_BUF_SIZE * RICHTAP_MMAP_BUF_SUM)) {
                ret = -EFAULT;
                break;
            }
            tmp = *((uint32_t*)aw8697->rtp_ptr);
            if(tmp > (RICHTAP_MMAP_BUF_SIZE * RICHTAP_MMAP_BUF_SUM - 4)) {
                dev_err(aw8697->dev, "rtp mode date len error %d\n", tmp);
                ret = -EINVAL;
                break;
            }
			aw8697_haptic_set_bst_vol(aw8697, 0x11);
			aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD);
            aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_RTP_MODE);
            aw8697_haptic_start(aw8697);
            usleep_range(2000, 2500);
            aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA, &aw8697->rtp_ptr[4], tmp);
            break;
        case RICHTAP_OFF_MODE:
            break;
        case RICHTAP_GET_F0:
			/*tmp = aw8697->f0;*/
			tmp = aw8697->haptic_real_f0 * 10;
            if(copy_to_user((void __user *)arg, &tmp, sizeof(uint32_t)))
                ret = -EFAULT;
            break;
        case RICHTAP_SETTING_GAIN:
            if(arg > 0x80)
                arg = 0x80;
            aw8697_i2c_write(aw8697, AW8697_REG_DATDBG, arg);
            break;
        case RICHTAP_STREAM_MODE:
            haptic_clean_buf(aw8697, MMAP_BUF_DATA_INVALID);
            aw8697_haptic_stop(aw8697);
            aw8697->done_flag = false;
            aw8697->haptic_rtp_mode = true;
			aw8697_haptic_set_bst_vol(aw8697, 0x11);
			aw8697_set_clock(aw8697, AW8697_HAPTIC_CLOCK_CALI_OSC_STANDARD);
            schedule_work(&aw8697->haptic_rtp_work);
			/*queue_work(aw8697->work_queue, &aw8697->haptic_rtp_work);*/
            break;
        case RICHTAP_STOP_MODE:
            aw8697->done_flag = true;
			aw8697_op_clean_status(aw8697);
			haptic_clean_buf(aw8697, MMAP_BUF_DATA_FINISHED);
			usleep_range(2000, 2000);
            aw8697_haptic_set_rtp_aei(aw8697, false);

            aw8697_haptic_stop(aw8697);
            aw8697->haptic_rtp_mode = false;
			/*wait_event_interruptible(haptic->doneQ, !haptic->task_flag);*/
            break;
        default:
            dev_err(aw8697->dev, "%s, unknown cmd\n", __func__);
            break;
    }
#else
    if(_IOC_TYPE(cmd) != AW8697_HAPTIC_IOCTL_MAGIC) {
        dev_err(aw8697->dev, "%s: cmd magic err\n",
                __func__);
        mutex_unlock(&aw8697->lock);
        return -EINVAL;
    }

	switch(cmd) {
        default:
            dev_err(aw8697->dev, "%s, unknown cmd\n", __func__);
            break;
    }
#endif
    mutex_unlock(&aw8697->lock);

    return ret;
}

static ssize_t aw8697_file_read(struct file* filp, char* buff, size_t len, loff_t* offset)
{
    struct aw8697 *aw8697 = (struct aw8697 *)filp->private_data;
    int ret = 0;
    int i = 0;
    unsigned char reg_val = 0;
    unsigned char *pbuff = NULL;

    mutex_lock(&aw8697->lock);

    dev_info(aw8697->dev, "%s: len=%zu\n", __func__, len);

    switch(aw8697->fileops.cmd)
    {
        case AW8697_HAPTIC_CMD_READ_REG:
            pbuff = (unsigned char *)kzalloc(len, GFP_KERNEL);
            if(pbuff != NULL) {
                for(i=0; i<len; i++) {
                    aw8697_i2c_read(aw8697, aw8697->fileops.reg+i, &reg_val);
                    pbuff[i] = reg_val;
                }
                for(i=0; i<len; i++) {
                    dev_info(aw8697->dev, "%s: pbuff[%d]=0x%02x\n",
                            __func__, i, pbuff[i]);
                }
                ret = copy_to_user(buff, pbuff, len);
                if(ret) {
                    dev_err(aw8697->dev, "%s: copy to user fail\n", __func__);
                }
                kfree(pbuff);
            } else {
                dev_err(aw8697->dev, "%s: alloc memory fail\n", __func__);
            }
            break;
        default:
            dev_err(aw8697->dev, "%s, unknown cmd %d \n", __func__, aw8697->fileops.cmd);
            break;
    }

    mutex_unlock(&aw8697->lock);


    return len;
}

static ssize_t proc_vibration_style_read(struct file *filp, char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct aw8697 *aw8697 = (struct aw8697 *)filp->private_data;
	uint8_t ret = 0;
	int style = 0;
	char page[10];

	style = aw8697->vibration_style;

	dev_info(aw8697->dev, "%s:aw8697 touch_style=%d\n", __func__, style);
	sprintf(page, "%d\n", style);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));
	return ret;
}

static ssize_t proc_vibration_style_write(struct file *filp, const char __user *buf,
				      size_t count, loff_t *lo)
{
	struct aw8697 *aw8697 = (struct aw8697 *)filp->private_data;
	char *buffer = NULL;
	int val;
	int rc = 0;

	buffer = (char *)kzalloc(count, GFP_KERNEL);
	if(buffer == NULL || buf == NULL) {
		dev_err(aw8697->dev, "%s: alloc memory fail\n", __func__);
		return count;
	}
	if (count > sizeof(buffer)) {
		return -EFAULT;
	}
	if (copy_from_user(buffer, buf, count)) {
		if(buffer != NULL) {
			kfree(buffer);
		}
		dev_err(aw8697->dev, "%s: error.\n", __func__);
		return count;
	}

	dev_err(aw8697->dev, "buffer=%s", buffer);
	rc = kstrtoint(buffer, 0, &val);
	if (rc < 0) {
		kfree(buffer);
		return count;
	}
	dev_err(aw8697->dev, "val = %d", val);

	if (val == 0) {
		aw8697->vibration_style = AW8697_HAPTIC_VIBRATION_CRISP_STYLE;
		aw8697_ram_update(aw8697);
	} else if (val == 1) {
		aw8697->vibration_style = AW8697_HAPTIC_VIBRATION_SOFT_STYLE;
		aw8697_ram_update(aw8697);
	} else {
		aw8697->vibration_style = AW8697_HAPTIC_VIBRATION_CRISP_STYLE;
	}
	kfree(buffer);
	return count;
}

static const struct file_operations proc_vibration_style_ops = {
	.read = proc_vibration_style_read,
	.write = proc_vibration_style_write,
	.open =  aw8697_file_open,
	.owner = THIS_MODULE,
};

static ssize_t aw8697_file_write(struct file* filp, const char* buff, size_t len, loff_t* off)
{
    struct aw8697 *aw8697 = (struct aw8697 *)filp->private_data;
    int i = 0;
    int ret = 0;
    unsigned char *pbuff = NULL;

    pbuff = (unsigned char *)kzalloc(len, GFP_KERNEL);
    if(pbuff == NULL) {
        dev_err(aw8697->dev, "%s: alloc memory fail\n", __func__);
        return len;
    }
    ret = copy_from_user(pbuff, buff, len);
    if(ret) {
#ifdef OPLUS_FEATURE_CHG_BASIC
        if(pbuff != NULL) {
            kfree(pbuff);
        }
#endif
        dev_err(aw8697->dev, "%s: copy from user fail\n", __func__);
        return len;
    }

    for(i=0; i<len; i++) {
        dev_info(aw8697->dev, "%s: pbuff[%d]=0x%02x\n",
                __func__, i, pbuff[i]);
    }

    mutex_lock(&aw8697->lock);

    aw8697->fileops.cmd = pbuff[0];

    switch(aw8697->fileops.cmd)
    {
        case AW8697_HAPTIC_CMD_READ_REG:
            if(len == 2) {
                aw8697->fileops.reg = pbuff[1];
            } else {
                dev_err(aw8697->dev, "%s: read cmd len %zu err\n", __func__, len);
            }
            break;
        case AW8697_HAPTIC_CMD_WRITE_REG:
            if(len > 2) {
                for(i=0; i<len-2; i++) {
                    dev_info(aw8697->dev, "%s: write reg0x%02x=0x%02x\n",
                            __func__, pbuff[1]+i, pbuff[i+2]);
                    aw8697_i2c_write(aw8697, pbuff[1]+i, pbuff[2+i]);
                }
            } else {
                dev_err(aw8697->dev, "%s: write cmd len %zu err\n", __func__, len);
            }
            break;
        default:
            dev_err(aw8697->dev, "%s, unknown cmd %d \n", __func__, aw8697->fileops.cmd);
            break;
    }

    mutex_unlock(&aw8697->lock);

    if(pbuff != NULL) {
        kfree(pbuff);
    }
    return len;
}

#ifdef AAC_RICHTAP
static int aw8697_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long phys;
    struct aw8697 *aw8697 = (struct aw8697 *)filp->private_data;
    int ret = 0;
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 7, 0)
	/*only accept PROT_READ, PROT_WRITE and MAP_SHARED from the API of mmap*/
	vm_flags_t vm_flags = calc_vm_prot_bits(PROT_READ|PROT_WRITE, 0) | calc_vm_flag_bits(MAP_SHARED);
	vm_flags |= current->mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC| VM_SHARED | VM_MAYSHARE;
	if (vma && (pgprot_val(vma->vm_page_prot) != pgprot_val(vm_get_page_prot(vm_flags))))
        return -EPERM;

	if (vma && ((vma->vm_end - vma->vm_start) != (PAGE_SIZE << RICHTAP_MMAP_PAGE_ORDER)))
        return -ENOMEM;
#endif
    phys = virt_to_phys(aw8697->start_buf);

    ret = remap_pfn_range(vma, vma->vm_start, (phys >> PAGE_SHIFT), (vma->vm_end - vma->vm_start), vma->vm_page_prot);
    if(ret) {
        dev_err(aw8697->dev, "Error mmap failed\n");
        return ret;
    }

    return ret;
}
#endif

static int init_vibrator_proc(struct aw8697 *aw8697)
{
	int ret = 0;
	struct proc_dir_entry *prEntry_da = NULL;
	struct proc_dir_entry *prEntry_tmp = NULL;

	prEntry_da = proc_mkdir("vibrator", NULL);
	if (prEntry_da == NULL) {
		ret = -ENOMEM;
		dev_err(aw8697->dev, "%s: Couldn't create vibrator proc entry\n",
			  __func__);
	}

	prEntry_tmp = proc_create_data("touch_style", 0664, prEntry_da,
				       &proc_vibration_style_ops, aw8697);
	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		dev_err(aw8697->dev, "%s: Couldn't create proc entry, %d\n", __func__,
			  __LINE__);
	}
	return 0;
}

static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .read = aw8697_file_read,
    .write = aw8697_file_write,
#ifdef AAC_RICHTAP
    .mmap = aw8697_file_mmap,
#endif
    .unlocked_ioctl = aw8697_file_unlocked_ioctl,
    .open = aw8697_file_open,
    .release = aw8697_file_release,
};

static struct miscdevice aw8697_haptic_misc =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = AW8697_HAPTIC_NAME,
    .fops = &fops,
};

static int aw8697_haptic_init(struct aw8697 *aw8697)
{
    int ret = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;

    pr_info("%s enter\n", __func__);

    ret = misc_register(&aw8697_haptic_misc);
    if(ret) {
        dev_err(aw8697->dev,  "%s: misc fail: %d\n", __func__, ret);
        return ret;
    }

    /* haptic audio */
    aw8697->haptic_audio.delay_val = 1;
    aw8697->haptic_audio.timer_val = 21318;
    INIT_LIST_HEAD(&(aw8697->haptic_audio.ctr_list));

    hrtimer_init(&aw8697->haptic_audio.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    aw8697->haptic_audio.timer.function = aw8697_haptic_audio_timer_func;
    INIT_WORK(&aw8697->haptic_audio.work, aw8697_haptic_audio_work_routine);

    mutex_init(&aw8697->haptic_audio.lock);
    aw8697->gun_type = 0xff;
    aw8697->bullet_nr = 0x00;
    aw8697->gun_mode = 0x00;
	aw8697->rtp_routine_on = 0; /*default rtp on is init to 0*/
    aw8697_op_clean_status(aw8697);
     mutex_init(&aw8697->rtp_lock);
	mutex_init(&aw8697->qos_lock);
    /* haptic init */
    mutex_lock(&aw8697->lock);

    aw8697->activate_mode = AW8697_HAPTIC_ACTIVATE_CONT_MODE;
	aw8697->vibration_style = AW8697_HAPTIC_VIBRATION_CRISP_STYLE;

    ret = aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1, &reg_val);
    aw8697->index = reg_val & 0x7F;
    ret = aw8697_i2c_read(aw8697, AW8697_REG_DATDBG, &reg_val);
    aw8697->gain = reg_val & 0xFF;
    ret = aw8697_i2c_read(aw8697, AW8697_REG_BSTDBG4, &reg_val);
#ifdef OPLUS_FEATURE_CHG_BASIC
    aw8697->vmax = AW8697_HAPTIC_HIGH_LEVEL_REG_VAL;
#else
    aw8697->vmax = (reg_val>>1)&0x1F;
#endif
    for(i=0; i<AW8697_SEQUENCER_SIZE; i++) {
        ret = aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1+i, &reg_val);
        aw8697->seq[i] = reg_val;
    }

    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);

    aw8697_haptic_set_pwm(aw8697, AW8697_PWM_24K);

    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG1, 0x30);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG2, 0xeb);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG3, 0xd4);
    aw8697_i2c_write(aw8697, AW8697_REG_TSET, 0x12);
    aw8697_i2c_write(aw8697, AW8697_REG_R_SPARE, 0x68);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANADBG,
            AW8697_BIT_ANADBG_IOC_MASK, AW8697_BIT_ANADBG_IOC_4P65A);

    aw8697_haptic_set_bst_peak_cur(aw8697, AW8697_DEFAULT_PEAKCUR);

    aw8697_haptic_swicth_motorprotect_config(aw8697, 0x00, 0x00);

    aw8697_haptic_auto_boost_config(aw8697, false);

    aw8697_haptic_trig_param_init(aw8697);
    aw8697_haptic_trig_param_config(aw8697);
	init_vibrator_proc(aw8697);

	aw8697_haptic_os_calibration(aw8697);

    aw8697_haptic_cont_vbat_mode(aw8697,
            AW8697_HAPTIC_CONT_VBAT_HW_COMP_MODE);
    aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE;

    mutex_unlock(&aw8697->lock);


    /* f0 calibration */
    mutex_lock(&aw8697->lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
    if (aw8697->device_id == 832 || aw8697->device_id == 833) {//19065 and 19161
        aw8697->f0_pre = AW8697_0832_HAPTIC_F0_PRE;
        aw8697->cont_drv_lvl = AW8697_0832_HAPTIC_CONT_DRV_LVL;
        aw8697->cont_drv_lvl_ov = AW8697_0832_HAPTIC_CONT_DRV_LVL_OV;
        aw8697->cont_td = AW8697_0832_HAPTIC_CONT_TD;
        aw8697->cont_zc_thr = AW8697_0832_HAPTIC_CONT_ZC_THR;
        aw8697->cont_num_brk = AW8697_0832_HAPTIC_CONT_NUM_BRK;
	} else if (aw8697->device_id == 81538) {
		aw8697->f0_pre = AW8697_081538_HAPTIC_F0_PRE;
		aw8697->cont_drv_lvl = AW8697_081538_HAPTIC_CONT_DRV_LVL;
		aw8697->cont_drv_lvl_ov = AW8697_081538_HAPTIC_CONT_DRV_LVL_OV;
		aw8697->cont_td = AW8697_081538_HAPTIC_CONT_TD;
		aw8697->cont_zc_thr = AW8697_081538_HAPTIC_CONT_ZC_THR;
		aw8697->cont_num_brk = AW8697_081538_HAPTIC_CONT_NUM_BRK;
    } else {
        aw8697->f0_pre = AW8697_0815_HAPTIC_F0_PRE;
        aw8697->cont_drv_lvl = AW8697_0815_HAPTIC_CONT_DRV_LVL;
        aw8697->cont_drv_lvl_ov = AW8697_0815_HAPTIC_CONT_DRV_LVL_OV;
        aw8697->cont_td = AW8697_0815_HAPTIC_CONT_TD;
        aw8697->cont_zc_thr = AW8697_0815_HAPTIC_CONT_ZC_THR;
        aw8697->cont_num_brk = AW8697_0815_HAPTIC_CONT_NUM_BRK;
    }
    pr_err("%s get f0_pre=%d\n", __func__, aw8697->f0_pre);
#endif
#ifndef OPLUS_FEATURE_CHG_BASIC
    aw8697_haptic_f0_calibration(aw8697);
#endif
    mutex_unlock(&aw8697->lock);

    return ret;
}



/*****************************************************
 *
 * vibrator
 *
 *****************************************************/
#ifdef TIMED_OUTPUT
static int aw8697_vibrator_get_time(struct timed_output_dev *dev)
{
    struct aw8697 *aw8697 = container_of(dev, struct aw8697, to_dev);

    if (hrtimer_active(&aw8697->timer)) {
        ktime_t r = hrtimer_get_remaining(&aw8697->timer);
        return ktime_to_ms(r);
    }

    return 0;
}

static void aw8697_vibrator_enable( struct timed_output_dev *dev, int value)
{
    struct aw8697 *aw8697 = container_of(dev, struct aw8697, to_dev);
    unsigned char reg_val = 0;

    mutex_lock(&aw8697->lock);

    pr_debug("%s enter\n", __func__);
    /*RTP mode do not allow other vibrate begign*/
    if(aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)
    {
         aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &reg_val);
         if((reg_val&0x0f) != 0x00) {
             pr_info("%s RTP on && not stop,return\n", __func__);
             mutex_unlock(&aw8697->lock);
             return;
         }
    }
    /*RTP mode do not allow other vibrate end*/

    aw8697_haptic_stop(aw8697);

    if (value > 0) {
        aw8697_haptic_ram_vbat_comp(aw8697, false);
        aw8697_haptic_play_wav_seq(aw8697, value);
    }

    mutex_unlock(&aw8697->lock);

    pr_debug("%s exit\n", __func__);
}

#else
static enum led_brightness aw8697_haptic_brightness_get(struct led_classdev *cdev)
{
    struct aw8697 *aw8697 =
        container_of(cdev, struct aw8697, cdev);

    return aw8697->amplitude;
}

static void aw8697_haptic_brightness_set(struct led_classdev *cdev,
                enum led_brightness level)
{
    int rtp_is_going_on = 0;
    struct aw8697 *aw8697 =  container_of(cdev, struct aw8697, cdev);

    // activate vincent
    rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
    if (rtp_is_going_on)
    {
        return ;
    }

    aw8697->amplitude = level;

    mutex_lock(&aw8697->lock);

    aw8697_haptic_stop(aw8697);
    if (aw8697->amplitude > 0) {
        aw8697_haptic_ram_vbat_comp(aw8697, false);
        aw8697_haptic_play_wav_seq(aw8697, aw8697->amplitude);
    }

    mutex_unlock(&aw8697->lock);

}
#endif

static ssize_t aw8697_state_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->state);
}

static ssize_t aw8697_state_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}

static ssize_t aw8697_duration_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ktime_t time_rem;
    s64 time_ms = 0;

    if (hrtimer_active(&aw8697->timer)) {
        time_rem = hrtimer_get_remaining(&aw8697->timer);
        time_ms = ktime_to_ms(time_rem);
    }

    return snprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t aw8697_duration_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

#ifdef OPLUS_FEATURE_CHG_BASIC
    pr_err("%s: value=%d\n", __FUNCTION__, val);
#endif
    /* setting 0 on duration is NOP for now */
    if (val <= 0)
        return count;

#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if (aw8697_haptic_juge_RTP_is_going_on(aw8697))
        return count;
#endif

    aw8697->duration = val;

    return count;
}

static ssize_t aw8697_activate_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    /* For now nothing to show */
    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->state);
}
//  vincent

static int aw8697_haptic_juge_RTP_is_going_on(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    unsigned char rtp_state = 0;


    aw8697_i2c_read(aw8697, AW8697_REG_SYSCTRL, &reg_val);
    if((reg_val&AW8697_BIT_SYSCTRL_PLAY_MODE_RTP)&&(!(reg_val&AW8697_BIT_SYSCTRL_STANDBY)))
    {
            rtp_state = 1;// is going on
    }
    if (aw8697->rtp_routine_on) {
        pr_info("%s:rtp_routine_on\n", __func__);
        rtp_state = 1;/*is going on*/
    }
    //pr_err("%s AW8697_REG_SYSCTRL 0x04==%02x rtp_state=%d\n", __func__,reg_val,rtp_state);

    return rtp_state;
}

//  vincent


static ssize_t aw8697_activate_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
	unsigned int val = 0;
	int rc = 0;
	int rtp_is_going_on = 0;
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	int rtp_max_num = 0;
#endif

    // activate vincent
    rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
    if (rtp_is_going_on)
    {
        return count;
    }
    // activate vincent
    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    if (val != 0 && val != 1)
        return count;

    pr_err("%s: value=%d\n", __FUNCTION__, val);
#ifndef CONFIG_OPLUS_HAPTIC_OOS
    hrtimer_cancel(&aw8697->timer);
#endif

    aw8697->state = val;

#ifndef CONFIG_OPLUS_HAPTIC_OOS
#ifdef OPLUS_FEATURE_CHG_BASIC
    if (aw8697->state)
    {
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if ((aw8697->boot_mode == MSM_BOOT_MODE__FACTORY || aw8697->boot_mode == MSM_BOOT_MODE__RF || aw8697->boot_mode == MSM_BOOT_MODE__WLAN)) {
		aw8697->gain = 0x60;
		mutex_lock(&aw8697->lock);
		aw8697_haptic_set_gain(aw8697, aw8697->gain);
		aw8697_haptic_set_repeat_wav_seq(aw8697, 1);
		mutex_unlock(&aw8697->lock);
		cancel_work_sync(&aw8697->vibrator_work);
		queue_work(system_highpri_wq, &aw8697->vibrator_work);
		return count;
	}
#endif
		pr_err("%s: 1 aw8697->gain=0x%02x\n", __FUNCTION__, aw8697->gain);
        if (aw8697->gain >= AW8697_HAPTIC_RAM_VBAT_COMP_GAIN) {
            aw8697->gain = AW8697_HAPTIC_RAM_VBAT_COMP_GAIN;
        }
        mutex_lock(&aw8697->lock);

#ifdef CONFIG_OPLUS_HAPTIC_OOS
        if (aw8697->device_id == 815 || aw8697->device_id == 1815)
#else
        if (aw8697->device_id == 815 || aw8697->device_id == 9595)
#endif
            aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_repeat_wav_seq(aw8697, AW8697_WAVEFORM_INDEX_SINE_CYCLE);

        mutex_unlock(&aw8697->lock);
        cancel_work_sync(&aw8697->vibrator_work);
        //schedule_work(&aw8697->vibrator_work);
        queue_work(system_highpri_wq, &aw8697->vibrator_work);
    } else {
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        mutex_unlock(&aw8697->lock);
    }
#endif
#else
	if (aw8697->device_id == 9595) {
		rtp_max_num = sizeof(aw8697_rtp_name_9595_170Hz) / AW8697_RTP_NAME_MAX;
	} else if (aw8697->device_id == 1815) {
		rtp_max_num = sizeof(aw8697_rtp_name_1815_170Hz) / AW8697_RTP_NAME_MAX;
	} else if (aw8697->device_id == 832) {
		rtp_max_num = sizeof(aw8697_rtp_name_0832_234Hz) / AW8697_RTP_NAME_MAX;
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	} else if (aw8697->device_id == 619) {
		rtp_max_num = sizeof(aw8697_rtp_name_0619_170Hz) / AW8697_RTP_NAME_MAX;
#endif /* CONFIG_OPLUS_HAPTIC_OOS */
	} else {
		rtp_max_num = sizeof(aw8697_rtp_name) / AW8697_RTP_NAME_MAX;
	}

	mutex_lock(&aw8697->lock);
	if (aw8697->gain >= 0x75) {
		aw8697->gain = 0x75;
	}
	aw8697_haptic_set_gain(aw8697, aw8697->gain);
	if (val == 0) {
		aw8697_haptic_stop(aw8697);
		aw8697_haptic_set_rtp_aei(aw8697, false);
		aw8697_interrupt_clear(aw8697);
		aw8697->sin_add_flag = 0;
		pr_info("%s: value=%d\n", __FUNCTION__, val);
	} else {
		if (aw8697->duration <= 500) {
			aw8697->sin_add_flag = 0;
			val = (aw8697->duration - 1) / 20;
			val = val + rtp_max_num - AW8697_LONG_VIB_BIN_COUNT;
		} else {
			aw8697->sin_add_flag = 1;
			val = rtp_max_num - 1;
		}

		pr_info("%s:a aw8697->rtp_file_num=%d\n", __FUNCTION__, val);
		aw8697_haptic_stop(aw8697);
		aw8697_haptic_set_rtp_aei(aw8697, false);
		aw8697_interrupt_clear(aw8697);
		if (val < rtp_max_num) {
			aw8697->rtp_file_num = val;
			rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
			if (!rtp_is_going_on)
				queue_work(system_highpri_wq, &aw8697->rtp_work);
			else
				pr_info("%s: rtp_is_going_on ignore vibrate:%d\n", __FUNCTION__, aw8697->rtp_file_num);
		} else {
			pr_err("%s: rtp_file_num 0x%02x over max value 0x%02x \n", __func__, aw8697->rtp_file_num, rtp_max_num);
		}
	}
	mutex_unlock(&aw8697->lock);
#endif

    return count;
}

static ssize_t aw8697_activate_mode_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "activate_mode=%d\n", aw8697->activate_mode);
}

static ssize_t aw8697_activate_mode_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    aw8697->activate_mode = val;
    mutex_unlock(&aw8697->lock);
    return count;
}

static ssize_t aw8697_index_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned char reg_val = 0;
    aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1, &reg_val);
    aw8697->index = reg_val;

    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->index);
}

static ssize_t aw8697_index_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_debug("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
    aw8697->index = val;
    aw8697_haptic_set_repeat_wav_seq(aw8697, aw8697->index);
    mutex_unlock(&aw8697->lock);
    return count;
}

static ssize_t aw8697_vmax_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8697->vmax);
}

struct aw8697_vmax_map {
	int level;
	int vmax;
	int gain;
};
#ifdef CONFIG_OPLUS_HAPTIC_OOS
static struct aw8697_vmax_map vmax_map[] = {
	{800,  0x00, 0x30},
	{900,  0x00, 0x36},
	{1000, 0x00, 0x42},
	{1100, 0x00, 0x48},
	{1200, 0x00, 0x54},
	{1300, 0x00, 0x60},
	{1400, 0x00, 0x64},
	{1500, 0x00, 0x70},
	{1600, 0x00, 0x75},
	{1700, 0x02, 0x75},
	{1800, 0x04, 0x75},
	{1900, 0x06, 0x75},
	{2000, 0x08, 0x75},
	{2100, 0x10, 0x75},
	{2200, 0x12, 0x75},
	{2300, 0x14, 0x75},
	{2400, 0x16, 0x75},
};
#else
static struct aw8697_vmax_map vmax_map[] = {
	{800,  0x00, 0x40},
	{900,  0x00, 0x49},
	{1000, 0x00, 0x51},
	{1100, 0x00, 0x5A},
	{1200, 0x00, 0x62},
	{1300, 0x00, 0x6B},
	{1400, 0x00, 0x73},
	{1500, 0x00, 0x7C},
	{1600, 0x01, 0x80},
	{1700, 0x04, 0x80},
	{1800, 0x07, 0x80},
	{1900, 0x0A, 0x80},
	{2000, 0x0D, 0x80},
	{2100, 0x10, 0x80},
	{2200, 0x12, 0x80},
	{2300, 0x15, 0x80},
	{2400, 0x18, 0x80},
};
#endif

int aw8697_convert_level_to_vmax(struct aw8697 *aw8697, int val)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(vmax_map); i++) {
		if(val == vmax_map[i].level) {
			aw8697->vmax = vmax_map[i].vmax;
			aw8697->gain = vmax_map[i].gain;
			break;
		}
	}
	if(i == ARRAY_SIZE(vmax_map)) {
		aw8697->vmax = vmax_map[i - 1].vmax;
		aw8697->gain = vmax_map[i - 1].gain;
	}

	if (aw8697->vmax > AW8697_HAPTIC_HIGH_LEVEL_REG_VAL)
		aw8697->vmax = AW8697_HAPTIC_HIGH_LEVEL_REG_VAL;

	return i;
}

static ssize_t aw8697_vmax_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
    if (val <= 255) {
        aw8697->gain = (val * AW8697_HAPTIC_RAM_VBAT_COMP_GAIN) / 255;
	} else if (val <= 2400) {
        aw8697_convert_level_to_vmax(aw8697, val);
    } else {
        aw8697->vmax = AW8697_HAPTIC_HIGH_LEVEL_REG_VAL;
        aw8697->gain = 0x80;
    }

    if (val == 2550) {  // for old test only
        aw8697->gain = AW8697_HAPTIC_RAM_VBAT_COMP_GAIN;
    }

#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if (val == 100 || val == 101 || val == 102 || val == 105) {
		aw8697->vmax = 0x16;
		aw8697->gain = 0x50;
	} else if (val == 103 || val == 106) {
		aw8697->vmax = 0x16;
		aw8697->gain = 0x60;
	} else if (val == 104 || val == 107) {
		aw8697->vmax = 0x16;
		aw8697->gain = 0x70;
	} else if (val == 108 || val == 109) {
		aw8697->vmax = 0x16;
		aw8697->gain = 0x80;
	}
#endif

    aw8697_haptic_set_gain(aw8697, aw8697->gain);
    aw8697_haptic_set_bst_vol(aw8697, aw8697->vmax);
#else
    aw8697->vmax = val;
    aw8697_haptic_set_bst_vol(aw8697, aw8697->vmax);
#endif
    mutex_unlock(&aw8697->lock);
    pr_err("%s:aw8697->gain[0x%x], aw8697->vmax[0x%x] end\n", __FUNCTION__, aw8697->gain, aw8697->vmax);
    return count;
}

static ssize_t aw8697_gain_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8697->gain);
}

static ssize_t aw8697_gain_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s: value=%d\n", __FUNCTION__, val);

    mutex_lock(&aw8697->lock);
    aw8697->gain = val;
    aw8697_haptic_set_gain(aw8697, aw8697->gain);
    mutex_unlock(&aw8697->lock);
    return count;
}

static ssize_t aw8697_seq_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    size_t count = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;

    for(i=0; i<AW8697_SEQUENCER_SIZE; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_WAVSEQ1+i, &reg_val);
        count += snprintf(buf+count, PAGE_SIZE-count,
                "seq%d: 0x%02x\n", i+1, reg_val);
        aw8697->seq[i] |= reg_val;
    }
    return count;
}

static ssize_t aw8697_seq_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};

    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        pr_debug("%s: seq%d=0x%x\n", __FUNCTION__, databuf[0], databuf[1]);
        mutex_lock(&aw8697->lock);
        aw8697->seq[databuf[0]] = (unsigned char)databuf[1];
        aw8697_haptic_set_wav_seq(aw8697, (unsigned char)databuf[0],
                aw8697->seq[databuf[0]]);
        mutex_unlock(&aw8697->lock);
    }
    return count;
}

static ssize_t aw8697_loop_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    size_t count = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;

    for(i=0; i<AW8697_SEQUENCER_LOOP_SIZE; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_WAVLOOP1+i, &reg_val);
        aw8697->loop[i*2+0] = (reg_val>>4)&0x0F;
        aw8697->loop[i*2+1] = (reg_val>>0)&0x0F;

        count += snprintf(buf+count, PAGE_SIZE-count,
                "seq%d loop: 0x%02x\n", i*2+1, aw8697->loop[i*2+0]);
        count += snprintf(buf+count, PAGE_SIZE-count,
                "seq%d loop: 0x%02x\n", i*2+2, aw8697->loop[i*2+1]);
    }
      count += snprintf(buf+count, PAGE_SIZE-count,
              "loop: 0x%02x\n", aw8697->rtp_loop);
    return count;
}

static ssize_t aw8697_loop_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};
    unsigned int val = 0;
    int rc = 0;
    aw8697->rtp_loop = 0;
    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        pr_debug("%s: seq%d loop=0x%x\n", __FUNCTION__, databuf[0], databuf[1]);
        mutex_lock(&aw8697->lock);
        aw8697->loop[databuf[0]] = (unsigned char)databuf[1];
        aw8697_haptic_set_wav_loop(aw8697, (unsigned char)databuf[0],
                aw8697->loop[databuf[0]]);
        mutex_unlock(&aw8697->lock);

    }else{
        rc = kstrtouint(buf, 0, &val);
        if (rc < 0)
            return rc;
        aw8697->rtp_loop = val;
        printk("%s  aw8697->rtp_loop = 0x%x",__func__,aw8697->rtp_loop);
    }

    return count;
}

static ssize_t aw8697_rtp_num_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char i = 0;
  //  unsigned char reg_val = 0;
    for(i = 0; i < AW8697_RTP_NUM; i ++) {
        len += snprintf(buf+len, PAGE_SIZE-len, "num: %d, serial:%d \n",i, aw8697->rtp_serial[i]);
    }
    return len;
}

static ssize_t aw8697_rtp_num_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    /*datebuf[0] is connect number */
    /*databuf[1]- databuf[x] is sequence and number*/
    unsigned int databuf[AW8697_RTP_NUM] = {0,0,0,0,0,0}; /*custom modify it,if you want*/
    unsigned int val = 0;
    int rc = 0;

    if( AW8697_RTP_NUM  == sscanf(buf, "%x %x %x %x %x %x", &databuf[0], &databuf[1],
     &databuf[2], &databuf[3], &databuf[4], &databuf[5])) {
       for(val = 0 ;val < AW8697_RTP_NUM ; val ++ ){
            printk("%s: databuf = %d \n", __FUNCTION__, databuf[val]);
            aw8697->rtp_serial[val] = (unsigned char)databuf[val];
      }
    } else {
        rc = kstrtouint(buf, 0, &val);
        if (rc < 0)
            return rc;
        if(val ==0)
            aw8697->rtp_serial[0] = 0;
    }
    aw8697_rtp_regroup_work(aw8697);
    return count;
}

static ssize_t aw8697_reg_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;
    for(i = 0; i < AW8697_REG_MAX; i ++) {
        if(!(aw8697_reg_access[i]&REG_RD_ACCESS))
           continue;
        aw8697_i2c_read(aw8697, i, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x \n", i, reg_val);
    }
    return len;
}

static ssize_t aw8697_reg_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};

    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        aw8697_i2c_write(aw8697, (unsigned char)databuf[0], (unsigned char)databuf[1]);
    }

    return count;
}

static ssize_t aw8697_rtp_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "rtp play: %d\n", aw8697->rtp_cnt);

    return len;
}

static ssize_t aw8697_rtp_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;
    int rtp_is_going_on = 0;
	int rtp_max_num = 0;
    static bool mute = false;

	mutex_lock(&aw8697->lock);

    rc = kstrtouint(buf, 0, &val);
	if (rc < 0) {
		mutex_unlock(&aw8697->lock);
		return rc;
	}
    pr_err("%s: val [%d] \n", __func__, val);

    if (val == 1025) {
        mute = true;
	mutex_unlock(&aw8697->lock);
        return count;
    } else if(val == 1026) {
        mute = false;
	mutex_unlock(&aw8697->lock);
        return count;
    }

#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if ((aw8697->device_id == 1815 || aw8697->device_id == 832) &&
	    val >= OPLUS_START_INDEX && val <= OPLUS_END_INDEX) {
	/* */
		val = val - 30;
	}

        if ((aw8697->device_id == 619 || aw8697->device_id == 1040) &&
	     val >= OPLUS_START_INDEX && val <= OPLUS_END_INDEX) {
                val = val - 30;
        }
#endif
	if (aw8697->device_id == 9595 && val >= OPLUS_START_INDEX && val <= OPLUS_END_INDEX) {
		val = val - 49;
	}

    /*OP add for juge rtp on begin*/
    rtp_is_going_on = aw8697_haptic_juge_RTP_is_going_on(aw8697);
    if (rtp_is_going_on && (val == AUDIO_READY_STATUS))
    {
        pr_info("%s: seem audio status rtp[%d]\n", __FUNCTION__,val);
        mutex_unlock(&aw8697->lock);
        return count;
    }
    /*OP add for juge rtp on end*/
    if (((val >=  RINGTONES_START_INDEX && val <= RINGTONES_END_INDEX)
        || (val >=  NEW_RING_START && val <= NEW_RING_END)
		|| (val >=  OS12_NEW_RING_START && val <= OS12_NEW_RING_END)
        || (val >=  OPLUS_RING_START && val <= OPLUS_RING_END)
#ifdef  CONFIG_OPLUS_HAPTIC_OOS
        || (val >= OPLUS_RING_START_INDEX && val <= OPLUS_RING_END_INDEX)
        || val == RINGTONES_NEWLIFE_INDEX
#endif
        || val == RINGTONES_SIMPLE_INDEX
        || val == RINGTONES_PURE_INDEX
        || val == AUDIO_READY_STATUS)) {
        if (val == AUDIO_READY_STATUS)
           aw8697->audio_ready = true;
        else
           aw8697->haptic_ready = true;
       pr_info("%s:audio[%d]and haptic[%d] ready\n", __FUNCTION__,
                        aw8697->audio_ready, aw8697->haptic_ready);
       if (aw8697->haptic_ready && !aw8697->audio_ready) {
           aw8697->pre_haptic_number = val;
       }
       if (!aw8697->audio_ready || !aw8697->haptic_ready) {
           mutex_unlock(&aw8697->lock);
           return count;
       }
    }
    if (val == AUDIO_READY_STATUS && aw8697->pre_haptic_number) {
        pr_info("pre_haptic_number:%d\n",aw8697->pre_haptic_number);
       val = aw8697->pre_haptic_number;
    }
    if (!val) {
      aw8697_op_clean_status(aw8697);
    }
    aw8697_haptic_stop(aw8697);

    aw8697_haptic_set_rtp_aei(aw8697, false);
    aw8697_interrupt_clear(aw8697);
    mutex_unlock(&aw8697->lock);

	if (aw8697->device_id == 9595) {
		rtp_max_num = sizeof(aw8697_rtp_name_9595_170Hz) / AW8697_RTP_NAME_MAX;
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	} else if (aw8697->device_id == 1815) {
		rtp_max_num = sizeof(aw8697_rtp_name_1815_170Hz) / AW8697_RTP_NAME_MAX;
	} else if (aw8697->device_id == 619) {
		rtp_max_num = sizeof(aw8697_rtp_name_0619_170Hz) / AW8697_RTP_NAME_MAX;
#endif
	} else {
		rtp_max_num = sizeof(aw8697_rtp_name) / AW8697_RTP_NAME_MAX;
	}

	if (val < rtp_max_num) {
		aw8697->rtp_file_num = val;
		if(val) {
			queue_work(system_unbound_wq, &aw8697->rtp_work);
		}
	} else {
		pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
	}

    return count;
}

static ssize_t aw8697_ram_update_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    //struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    //struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    //struct led_classdev *cdev = dev_get_drvdata(dev);
    //struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "sram update mode\n");
    return len;
}

static ssize_t aw8697_ram_update_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    if(val) {
        aw8697_ram_update(aw8697);
    }
    return count;
}

static ssize_t aw8697_f0_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    mutex_lock(&aw8697->lock);
    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;//AW8697_HAPTIC_LRA_F0;
    aw8697_haptic_get_f0(aw8697);
    mutex_unlock(&aw8697->lock);

#ifdef OPLUS_FEATURE_CHG_BASIC
    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->f0);
#else
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 lra f0 = %d\n", aw8697->f0);
#endif
    return len;
}

static ssize_t aw8697_f0_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s:  f0 = %d\n", __FUNCTION__, val);

#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if (aw8697->device_id == 815 || aw8697->device_id == 1815 || aw8697->device_id == 9595) {
#else
	if (aw8697->device_id == 815 || aw8697->device_id == 9595) {
#endif
		aw8697->f0 = val;
		if (aw8697->f0 < F0_VAL_MIN_0815 ||
		    aw8697->f0 > F0_VAL_MAX_0815) {
			aw8697->f0 = 1700;
		}
	} else if (aw8697->device_id == 81538) {
		aw8697->f0 = val;
		if (aw8697->f0 < F0_VAL_MIN_081538 || aw8697->f0 > F0_VAL_MAX_081538) {
			aw8697->f0 = 1500;
		}
	} else if (aw8697->device_id == 832) {
		aw8697->f0 = val;
		if (aw8697->f0 < F0_VAL_MIN_0832 || aw8697->f0 > F0_VAL_MAX_0832) {
			aw8697->f0 = 2300;
		}
	} else if (aw8697->device_id == 833) {
		aw8697->f0 = val;
		if (aw8697->f0 < F0_VAL_MIN_0833 || aw8697->f0 > F0_VAL_MAX_0833) {
			aw8697->f0 = 2330;
		}
	} else if (aw8697->device_id == 619) {
		aw8697->f0 = val;
		if (aw8697->f0 < F0_VAL_MIN_0619 || aw8697->f0 > F0_VAL_MAX_0619) {
		aw8697->f0 = 1700;
		}
	} else if (aw8697->device_id == 1040) {
		aw8697->f0 = val;
		if (aw8697->f0 < F0_VAL_MIN_1040 || aw8697->f0 > F0_VAL_MAX_1040) {
		aw8697->f0 = 1700;
		}
	}
	aw8697_ram_update(aw8697);

	return count;
}

static ssize_t aw8697_cali_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    mutex_lock(&aw8697->lock);
    aw8697->f0_cali_flag = AW8697_HAPTIC_CALI_F0;
    aw8697_haptic_get_f0(aw8697);
    mutex_unlock(&aw8697->lock);
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cali f0 = %d\n", aw8697->f0);
    return len;
}

static ssize_t aw8697_cali_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    if(val) {
        mutex_lock(&aw8697->lock);
        aw8697->clock_system_f0_cali_lra = 0;
        aw8697_haptic_f0_calibration(aw8697);
        mutex_unlock(&aw8697->lock);
    }
    return count;
}

#ifdef CONFIG_OPLUS_HAPTIC_OOS
#define  RESISTANCE_ABNORMAL_THR 5

static ssize_t aw8697_short_check_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
	ssize_t len = 0;
	int rc = 0;
	bool short_circuit = 0;
	unsigned char reg_val = 0;

	mutex_lock(&aw8697->lock);
	aw8697_haptic_stop(aw8697);
	aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
		AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
	aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
		AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);

	aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
		AW8697_BIT_ANACTRL_HD_PD_MASK, AW8697_BIT_ANACTRL_HD_HZ_EN);
	aw8697_i2c_write_bits(aw8697, AW8697_REG_D2SCFG,
		AW8697_BIT_D2SCFG_CLK_ADC_MASK, AW8697_BIT_D2SCFG_CLK_ASC_1P5MHZ);

	aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
		AW8697_BIT_DETCTRL_RL_OS_MASK, AW8697_BIT_DETCTRL_RL_DETECT);
	aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
		AW8697_BIT_DETCTRL_DIAG_GO_MASK, AW8697_BIT_DETCTRL_DIAG_GO_ENABLE);
	usleep_range(3000, 3500);
	rc = aw8697_i2c_read(aw8697, AW8697_REG_RLDET, &reg_val);
	if (rc < 0)
		short_circuit = true;
	aw8697->lra = 298 * reg_val;
	if (aw8697->lra/100 < RESISTANCE_ABNORMAL_THR)
		short_circuit = true;
	pr_info("aw8697_short_check aw8697->lra/100 :%d\n", aw8697->lra/100);
	aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
		AW8697_BIT_ANACTRL_HD_PD_MASK, AW8697_BIT_ANACTRL_HD_PD_EN);
	aw8697_i2c_write_bits(aw8697, AW8697_REG_D2SCFG,
		AW8697_BIT_D2SCFG_CLK_ADC_MASK, AW8697_BIT_D2SCFG_CLK_ASC_6MHZ);

	aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
		AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);
	aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
		AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
	mutex_unlock(&aw8697->lock);
	if (short_circuit) {
		len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", -1);
		return len;
	} else {
		mutex_lock(&aw8697->lock);
		aw8697_i2c_write(aw8697, AW8697_REG_SYSINTM, 0x9f);
		aw8697_haptic_set_wav_seq(aw8697, 0x01, 0x00);
		aw8697_haptic_set_wav_loop(aw8697, 0, 0);
		aw8697_haptic_stop(aw8697);
		aw8697_haptic_play_wav_seq(aw8697, 1);
		mutex_unlock(&aw8697->lock);
		usleep_range(200000, 205000);
		aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
		aw8697_i2c_write(aw8697, AW8697_REG_SYSINTM, 0xb9);
		pr_info("aw8697_short_check 0x02:0x%x\n", reg_val);
		short_circuit =  reg_val & AW8697_BIT_SYSINT_OCDI;
		if (short_circuit) {
			len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", -1);
			return len;
		}
	}
	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", 0);
	return len;
}

static ssize_t aw8697_short_check_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
	return count;
}
#endif

static ssize_t aw8697_cont_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    aw8697_haptic_read_cont_f0(aw8697);
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont f0 = %d\n", aw8697->cont_f0);
    return len;
}

static ssize_t aw8697_cont_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;
    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    if(val) {
        aw8697_haptic_cont(aw8697);
    }
    mutex_unlock(&aw8697->lock);
    return count;
}


static ssize_t aw8697_cont_td_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont delay time = 0x%04x\n", aw8697->cont_td);
    return len;
}

static ssize_t aw8697_cont_td_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    if(1 == sscanf(buf, "%x", &databuf[0])) {
        aw8697->cont_td = databuf[0];
        aw8697_i2c_write(aw8697, AW8697_REG_TD_H, (unsigned char)(databuf[0]>>8));
        aw8697_i2c_write(aw8697, AW8697_REG_TD_L, (unsigned char)(databuf[0]>>0));
    }
    return count;
}

static ssize_t aw8697_cont_drv_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont drv level = %d\n", aw8697->cont_drv_lvl);
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont drv level overdrive= %d\n", aw8697->cont_drv_lvl_ov);
    return len;
}

static ssize_t aw8697_cont_drv_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0, 0};
    if(2 == sscanf(buf, "%d %d", &databuf[0], &databuf[1])) {
        aw8697->cont_drv_lvl = databuf[0];
        aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL, aw8697->cont_drv_lvl);
        aw8697->cont_drv_lvl_ov = databuf[1];
        aw8697_i2c_write(aw8697, AW8697_REG_DRV_LVL_OV, aw8697->cont_drv_lvl_ov);
    }
    return count;
}

static ssize_t aw8697_cont_num_brk_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont break num = %d\n", aw8697->cont_num_brk);
    return len;
}

static ssize_t aw8697_cont_num_brk_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    if(1 == sscanf(buf, "%d", &databuf[0])) {
        aw8697->cont_num_brk = databuf[0];
        if(aw8697->cont_num_brk > 7) {
            aw8697->cont_num_brk = 7;
        }
        aw8697_i2c_write_bits(aw8697, AW8697_REG_BEMF_NUM,
                AW8697_BIT_BEMF_NUM_BRK_MASK, aw8697->cont_num_brk);
    }
    return count;
}

static ssize_t aw8697_cont_zc_thr_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697 cont zero cross thr = 0x%04x\n", aw8697->cont_zc_thr);
    return len;
}

static ssize_t aw8697_cont_zc_thr_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    if(1 == sscanf(buf, "%x", &databuf[0])) {
        aw8697->cont_zc_thr = databuf[0];
        aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_H, (unsigned char)(databuf[0]>>8));
        aw8697_i2c_write(aw8697, AW8697_REG_ZC_THRSH_L, (unsigned char)(databuf[0]>>0));
    }
    return count;
}

static ssize_t aw8697_vbat_monitor_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    aw8697_haptic_get_vbat(aw8697);
    len += snprintf(buf+len, PAGE_SIZE-len, "vbat=%dmV\n", aw8697->vbat);
    mutex_unlock(&aw8697->lock);

    return len;
}

static ssize_t aw8697_vbat_monitor_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    return count;
}

static ssize_t aw8697_lra_resistance_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char reg_val = 0;

    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BYPASS);


    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
            AW8697_BIT_ANACTRL_HD_PD_MASK, AW8697_BIT_ANACTRL_HD_HZ_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_D2SCFG,
            AW8697_BIT_D2SCFG_CLK_ADC_MASK, AW8697_BIT_D2SCFG_CLK_ASC_1P5MHZ);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_RL_OS_MASK, AW8697_BIT_DETCTRL_RL_DETECT);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DETCTRL,
            AW8697_BIT_DETCTRL_DIAG_GO_MASK, AW8697_BIT_DETCTRL_DIAG_GO_ENABLE);
    msleep(3);
    aw8697_i2c_read(aw8697, AW8697_REG_RLDET, &reg_val);
    aw8697->lra = 298 * reg_val;

#ifdef OPLUS_FEATURE_CHG_BASIC
    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->lra);
#else
    len += snprintf(buf+len, PAGE_SIZE-len, "r_lra=%dmohm\n", aw8697->lra);
#endif

    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANACTRL,
            AW8697_BIT_ANACTRL_HD_PD_MASK, AW8697_BIT_ANACTRL_HD_PD_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_D2SCFG,
            AW8697_BIT_D2SCFG_CLK_ADC_MASK, AW8697_BIT_D2SCFG_CLK_ASC_6MHZ);

    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_BST_MODE_MASK, AW8697_BIT_SYSCTRL_BST_MODE_BOOST);
    mutex_unlock(&aw8697->lock);

    return len;
}

static ssize_t aw8697_lra_resistance_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    return count;
}

static ssize_t aw8697_auto_boost_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    len += snprintf(buf+len, PAGE_SIZE-len, "auto_boost=%d\n", aw8697->auto_boost);

    return len;
}


static ssize_t aw8697_auto_boost_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    aw8697_haptic_auto_boost_config(aw8697, val);
    mutex_unlock(&aw8697->lock);

    return count;
}

static ssize_t aw8697_prctmode_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char reg_val = 0;

    aw8697_i2c_read(aw8697, AW8697_REG_RLDET, &reg_val);

    len += snprintf(buf+len, PAGE_SIZE-len, "prctmode=%d\n", reg_val&0x20);
    return len;
}


static ssize_t aw8697_prctmode_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    #ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
    #else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
    #endif
    unsigned int databuf[2] = {0, 0};
    unsigned int addr=0;
    unsigned int val=0;
    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        addr = databuf[0];
        val=databuf[1];
        mutex_lock(&aw8697->lock);
        aw8697_haptic_swicth_motorprotect_config(aw8697, addr, val);
        mutex_unlock(&aw8697->lock);
   }
    return count;
}

static ssize_t aw8697_trig_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned char i = 0;
    for(i=0; i<AW8697_TRIG_NUM; i++) {
        len += snprintf(buf+len, PAGE_SIZE-len,
            "trig%d: enable=%d, default_level=%d, dual_edge=%d, frist_seq=%d, second_seq=%d\n",
            i+1, aw8697->trig[i].enable, aw8697->trig[i].default_level, aw8697->trig[i].dual_edge,
            aw8697->trig[i].frist_seq, aw8697->trig[i].second_seq);
    }

    return len;
}

static ssize_t aw8697_trig_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    #ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
    #else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
    #endif
    unsigned int databuf[6] = {0};
    if(sscanf(buf, "%d %d %d %d %d %d",
            &databuf[0], &databuf[1], &databuf[2], &databuf[3], &databuf[4], &databuf[5])) {
        pr_debug("%s: %d, %d, %d, %d, %d, %d\n", __func__,
            databuf[0], databuf[1], databuf[2], databuf[3], databuf[4], databuf[5]);
        if(databuf[0] > 3) {
            databuf[0] = 3;
        }
        if(databuf[0] > 0) {
            databuf[0] -= 1;
        }
        aw8697->trig[databuf[0]].enable = databuf[1];
        aw8697->trig[databuf[0]].default_level = databuf[2];
        aw8697->trig[databuf[0]].dual_edge = databuf[3];
        aw8697->trig[databuf[0]].frist_seq = databuf[4];
        aw8697->trig[databuf[0]].second_seq = databuf[5];
        mutex_lock(&aw8697->lock);
        aw8697_haptic_trig_param_config(aw8697);
        aw8697_haptic_trig_enable_config(aw8697);
        mutex_unlock(&aw8697->lock);
   }
    return count;
}

static ssize_t aw8697_ram_vbat_comp_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    len += snprintf(buf+len, PAGE_SIZE-len, "ram_vbat_comp=%d\n", aw8697->ram_vbat_comp);

    return len;
}


static ssize_t aw8697_ram_vbat_comp_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    if(val) {
        aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE;
    } else {
        aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_DISABLE;
    }
    mutex_unlock(&aw8697->lock);

    return count;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
static ssize_t aw8697_f0_data_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;

    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->clock_system_f0_cali_lra);
    return len;
}

#ifdef CONFIG_OPLUS_HAPTIC_OOS
static int aw8697_check_f0_data(struct aw8697 *aw8697)
{
	unsigned int f0_limit = 0;
	char f0_cali_lra = 0;
	int f0_cali_step = 0;
	int ret = 0;

	f0_limit = aw8697->f0;
	pr_err("%s get f0=%d\n", __func__, f0_limit);

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (aw8697->device_id == 832 || aw8697->device_id == 833) {
		if(aw8697->f0*100 < AW8697_0832_HAPTIC_F0_PRE*(100-AW8697_0832_HAPTIC_F0_CALI_PERCEN)) {
			f0_limit = AW8697_0832_HAPTIC_F0_PRE*(100-AW8697_0832_HAPTIC_F0_CALI_PERCEN)/100;
		}
		if(aw8697->f0*100 > AW8697_0832_HAPTIC_F0_PRE*(100+AW8697_0832_HAPTIC_F0_CALI_PERCEN)) {
			f0_limit = AW8697_0832_HAPTIC_F0_PRE*(100+AW8697_0832_HAPTIC_F0_CALI_PERCEN)/100;
		}
	} else {
		if(aw8697->f0*100 < AW8697_0815_HAPTIC_F0_PRE*(100-AW8697_0815_HAPTIC_F0_CALI_PERCEN)) {
			f0_limit = AW8697_0815_HAPTIC_F0_PRE*(100-AW8697_0815_HAPTIC_F0_CALI_PERCEN)/100;
		}
		if(aw8697->f0*100 > AW8697_0815_HAPTIC_F0_PRE*(100+AW8697_0815_HAPTIC_F0_CALI_PERCEN)) {
			f0_limit = AW8697_0815_HAPTIC_F0_PRE*(100+AW8697_0815_HAPTIC_F0_CALI_PERCEN)/100;
		}
	}
#else
	if(aw8697->f0*100 < AW8697_HAPTIC_F0_PRE*(100-AW8697_HAPTIC_F0_CALI_PERCEN)) {
		f0_limit = AW8697_HAPTIC_F0_PRE*(100-AW8697_HAPTIC_F0_CALI_PERCEN)/100;
	}
	if(aw8697->f0*100 > AW8697_HAPTIC_F0_PRE*(100+AW8697_HAPTIC_F0_CALI_PERCEN)) {
		f0_limit = AW8697_HAPTIC_F0_PRE*(100+AW8697_HAPTIC_F0_CALI_PERCEN)/100;
	}
#endif

	/* calculate cali step */
	f0_cali_step = 100000 * ((int)f0_limit - (int)aw8697->f0_pre) / ((int)f0_limit * 25);
	pr_info("%s  line=%d f0_cali_step=%d\n", __func__, __LINE__, f0_cali_step);
	pr_info("%s line=%d  f0_limit=%d\n", __func__, __LINE__, (int)f0_limit);
	pr_info("%s line=%d  aw8697->f0_pre=%d\n", __func__, __LINE__, (int)aw8697->f0_pre);

	if (f0_cali_step >= 0) {
		if (f0_cali_step % 10 >= 5)
			f0_cali_step = f0_cali_step / 10 + 1 + 32;
		else
			f0_cali_step = f0_cali_step / 10 + 32;
	} else {	/*f0_cali_step < 0 */
		if (f0_cali_step % 10 <= -5)
			f0_cali_step = 32 + (f0_cali_step / 10 - 1);
		else
			f0_cali_step = 32 + f0_cali_step / 10;
	}

	if (f0_cali_step > 31)
		f0_cali_lra = (char)f0_cali_step - 32;
	else
		f0_cali_lra = (char)f0_cali_step + 32;
	pr_info("%s f0_cali_lra=%d\n", __func__, (int)f0_cali_lra);

	ret = (int)f0_cali_lra;
	if ((ret - (int)aw8697->clock_system_f0_cali_lra) > 3 ||
		((int)aw8697->clock_system_f0_cali_lra - ret) > 3) {
		pr_info("%s old f0_cali_lra = %d , new f0_cali_lra = %d\n", __func__, aw8697->clock_system_f0_cali_lra, (int)f0_cali_lra);
		aw8697->clock_system_f0_cali_lra = (int)f0_cali_lra;
	}

	return 0;
}
#endif

static ssize_t aw8697_f0_data_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;
    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    pr_err("%s:  f0 = %d\n", __FUNCTION__, val);

    aw8697->clock_system_f0_cali_lra = val;
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	aw8697_check_f0_data(aw8697);
#endif
    mutex_lock(&aw8697->lock);
    aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, aw8697->clock_system_f0_cali_lra);
    mutex_unlock(&aw8697->lock);

    return count;
}

static ssize_t aw8697_rtp_going_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
	ssize_t len = 0;
	int val = -1;

	val = aw8697_haptic_juge_RTP_is_going_on(aw8697);
	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", val);
	return len;
}

static ssize_t aw8697_rtp_going_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8697_osc_cali_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	aw8697->microsecond = 5000000;
#endif
	printk("aw8697_osc_cali_show: 2018_microsecond:%lu \n", aw8697->microsecond);
	len += snprintf(buf + len, PAGE_SIZE - len, "%lu\n", aw8697->microsecond);
    return len;
}
#endif

static ssize_t aw8697_osc_cali_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int val = 0;

    int rc = 0;

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;

    mutex_lock(&aw8697->lock);
    if(val == 3){
        aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, 0);
        aw8697->clock_standard_OSC_lra_rtim_code = 0;
        aw8697_rtp_osc_calibration(aw8697);
        aw8697_rtp_trim_lra_calibration(aw8697);
    }
   if(val == 1)
      aw8697_rtp_osc_calibration(aw8697);

    mutex_unlock(&aw8697->lock);

    return count;
}

static ssize_t aw8697_haptic_audio_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->haptic_audio.ctr.cnt);
    return len;
}

static ssize_t aw8697_haptic_audio_store(struct device *dev,
                     struct device_attribute *attr,
                     const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[6] = { 0 };
    struct haptic_ctr *hap_ctr = NULL;

    if (aw8697->rtp_on)
       return count;
    if (!aw8697->ram_init)
        return count;
    if (6 ==
        sscanf(buf, "%d %d %d %d %d %d", &databuf[0], &databuf[1],
           &databuf[2], &databuf[3], &databuf[4], &databuf[5])) {
        if (databuf[2]) {
            pr_debug
            ("%s: cnt=%d, cmd=%d, play=%d, wavseq=%d, loop=%d, gain=%d\n",
             __func__, databuf[0], databuf[1], databuf[2],
             databuf[3], databuf[4], databuf[5]);
        }

        hap_ctr =
            (struct haptic_ctr *)kzalloc(sizeof(struct haptic_ctr),
                         GFP_KERNEL);
        if (hap_ctr == NULL) {
            pr_err("%s: kzalloc memory fail\n", __func__);
            return count;
        }
        mutex_lock(&aw8697->haptic_audio.lock);
        hap_ctr->cnt = (unsigned char)databuf[0];
        hap_ctr->cmd = (unsigned char)databuf[1];
        hap_ctr->play = (unsigned char)databuf[2];
        hap_ctr->wavseq = (unsigned char)databuf[3];
        hap_ctr->loop = (unsigned char)databuf[4];
        hap_ctr->gain = (unsigned char)databuf[5];
        aw8697_haptic_audio_ctr_list_insert(&aw8697->haptic_audio,
                            hap_ctr);

        if (hap_ctr->cmd == 0xff) {
        pr_debug("%s: haptic_audio stop\n", __func__);
            if (hrtimer_active(&aw8697->haptic_audio.timer)) {
                pr_debug("%s: cancel haptic_audio_timer\n",
                    __func__);
                hrtimer_cancel(&aw8697->haptic_audio.timer);
                aw8697->haptic_audio.ctr.cnt = 0;
                aw8697_haptic_audio_off(aw8697);
            }
        } else {
            if (hrtimer_active(&aw8697->haptic_audio.timer)) {
            } else {
                pr_debug("%s: start haptic_audio_timer\n",
                    __func__);
                aw8697_haptic_audio_init(aw8697);
                hrtimer_start(&aw8697->haptic_audio.timer,
                          ktime_set(aw8697->haptic_audio.
                            delay_val / 1000000,
                            (aw8697->haptic_audio.
                             delay_val % 1000000) *
                            1000),
                          HRTIMER_MODE_REL);
            }
        }
        mutex_unlock(&aw8697->haptic_audio.lock);
        kfree(hap_ctr);
    }
    return count;
}


static ssize_t aw8697_haptic_audio_time_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "haptic_audio.delay_val=%dus\n", aw8697->haptic_audio.delay_val);
    len += snprintf(buf+len, PAGE_SIZE-len, "haptic_audio.timer_val=%dus\n", aw8697->haptic_audio.timer_val);
    return len;
}

static ssize_t aw8697_haptic_audio_time_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[2] = {0};

    if (aw8697->rtp_on)
       return count;
    if (!aw8697->ram_init)
       return count;

    if(2 == sscanf(buf, "%d %d", &databuf[0], &databuf[1])) {
        aw8697->haptic_audio.delay_val = databuf[0];
        aw8697->haptic_audio.timer_val = databuf[1];
    }
    return count;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
static void motor_old_test_work(struct work_struct *work)
{
    struct aw8697 *aw8697 = container_of(work, struct aw8697, motor_old_test_work);

    pr_err("%s: motor_old_test_mode = %d. aw8697->gain[0x%02x]\n", __func__, aw8697->motor_old_test_mode, aw8697->gain);

    if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_TRANSIENT) {
        mutex_lock(&aw8697->lock);

        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
		aw8697_haptic_set_wav_seq(aw8697, 0, AW8697_WAVEFORM_INDEX_TRANSIENT);
        aw8697_haptic_set_wav_loop(aw8697, 0, 0);
        aw8697_haptic_play_wav_seq(aw8697, 1);

        mutex_unlock(&aw8697->lock);
    } else if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_STEADY) {
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
        mutex_unlock(&aw8697->lock);
        if(AW8697_WAVEFORM_INDEX_OLD_STEADY < (sizeof(aw8697_rtp_name)/AW8697_RTP_NAME_MAX)) {
            aw8697->rtp_file_num = AW8697_WAVEFORM_INDEX_OLD_STEADY;
            if(AW8697_WAVEFORM_INDEX_OLD_STEADY) {
                //schedule_work(&aw8697->rtp_work);
                queue_work(system_unbound_wq, &aw8697->rtp_work);
            }
        } else {
            pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
        }
    } else if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_HIGH_TEMP_HUMIDITY){
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
        mutex_unlock(&aw8697->lock);
        if(AW8697_WAVEFORM_INDEX_HIGH_TEMP < (sizeof(aw8697_rtp_name)/AW8697_RTP_NAME_MAX)) {
            aw8697->rtp_file_num = AW8697_WAVEFORM_INDEX_HIGH_TEMP;
            if(AW8697_WAVEFORM_INDEX_HIGH_TEMP) {
                //schedule_work(&aw8697->rtp_work);
                queue_work(system_unbound_wq, &aw8697->rtp_work);
            }
        } else {
            pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
        }
    } else if (aw8697->motor_old_test_mode == MOTOR_OLD_TEST_LISTEN_POP){
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        aw8697->gain = 0x80;
        aw8697_haptic_set_gain(aw8697, aw8697->gain);
        aw8697_haptic_set_bst_vol(aw8697, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
        aw8697_haptic_set_rtp_aei(aw8697, false);
        aw8697_interrupt_clear(aw8697);
        mutex_unlock(&aw8697->lock);
        if(AW8697_WAVEFORM_INDEX_LISTEN_POP < (sizeof(aw8697_rtp_name)/AW8697_RTP_NAME_MAX)) {
            aw8697->rtp_file_num = AW8697_WAVEFORM_INDEX_LISTEN_POP;
            if(AW8697_WAVEFORM_INDEX_LISTEN_POP) {
                //schedule_work(&aw8697->rtp_work);
                queue_work(system_unbound_wq, &aw8697->rtp_work);
            }
        } else {
            pr_err("%s: rtp_file_num 0x%02x over max value \n", __func__, aw8697->rtp_file_num);
        }
    } else {
        aw8697->motor_old_test_mode = 0;
        mutex_lock(&aw8697->lock);
        aw8697_haptic_stop(aw8697);
        //aw8697_haptic_android_stop(aw8697);
        mutex_unlock(&aw8697->lock);
    }
}


static ssize_t aw8697_motor_old_test_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    return 0;
}

static ssize_t aw8697_motor_old_test_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    unsigned int databuf[1] = {0};

    if(1 == sscanf(buf, "%x", &databuf[0])) {
        if(databuf[0] == 0) {
            cancel_work_sync(&aw8697->motor_old_test_work);
            mutex_lock(&aw8697->lock);
            aw8697_haptic_stop(aw8697);
            mutex_unlock(&aw8697->lock);
        } else if(databuf[0] <= MOTOR_OLD_TEST_ALL_NUM) {
            cancel_work_sync(&aw8697->motor_old_test_work);
            aw8697->motor_old_test_mode = databuf[0];
            pr_err("%s: motor_old_test_mode = %d.\n", __func__, aw8697->motor_old_test_mode);
            schedule_work(&aw8697->motor_old_test_work);
        }
    }

    return count;
}

static ssize_t aw8697_waveform_index_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    return 0;
}

static ssize_t aw8697_waveform_index_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
	unsigned int databuf[1] = {0};


	if(1 == sscanf(buf, "%d", &databuf[0])) {
		pr_err("%s: waveform_index = %d\n", __FUNCTION__, databuf[0]);
		mutex_lock(&aw8697->lock);
		aw8697->seq[0] = (unsigned char)databuf[0];
		aw8697_haptic_set_wav_seq(aw8697, 0, aw8697->seq[0]);
		aw8697_haptic_set_wav_seq(aw8697, 1, 0);
		aw8697_haptic_set_wav_loop(aw8697, 0, 0);
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if(databuf[0] == 0x10 || databuf[0] == 0x11 || databuf[0] == 0x12) {
		if(aw8697->gain > 0x75) {
			aw8697->gain = 0x75;
		}
	}
#endif
		mutex_unlock(&aw8697->lock);
	}
	return count;
}

static ssize_t aw8697_osc_data_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    unsigned char lra_rtim_code = 0;
    ssize_t len = 0;

    mutex_lock(&aw8697->lock);
    aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &lra_rtim_code);
    mutex_unlock(&aw8697->lock);

    pr_err("%s: lra_rtim_code = %d, code=%d\n", __FUNCTION__, lra_rtim_code, aw8697->clock_standard_OSC_lra_rtim_code);

    len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw8697->clock_standard_OSC_lra_rtim_code);
    return len;

}

static ssize_t aw8697_osc_data_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    unsigned int databuf[1] = {0};
    //unsigned int lra_rtim_code = 0;

    if(1 == sscanf(buf, "%d", &databuf[0])) {
        pr_err("%s: lra_rtim_code = %d\n", __FUNCTION__, databuf[0]);
        aw8697->clock_standard_OSC_lra_rtim_code = databuf[0];
        mutex_lock(&aw8697->lock);
        if(databuf[0] > 0 )
            aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, (char)databuf[0]);
        mutex_unlock(&aw8697->lock);
    }
    return count;
}

static ssize_t aw8697_haptic_ram_test_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    ssize_t len = 0;
    unsigned int ram_test_result = 0;
    //len += snprintf(buf+len, PAGE_SIZE-len, "aw8697->ram_test_flag_0=%d  0:pass,!0= failed\n", aw8697->ram_test_flag_0 );
    //len += snprintf(buf+len, PAGE_SIZE-len, "aw8697->ram_test_flag_1=%d  0:pass,!0= failed\n", aw8697->ram_test_flag_1 );
    if (aw8697->ram_test_flag_0 != 0 || aw8697->ram_test_flag_1 != 0) {
        ram_test_result = 1;  // failed
        len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", ram_test_result);
    } else {
        ram_test_result = 0;  // pass
        len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", ram_test_result);
    }
    return len;
}

static ssize_t aw8697_haptic_ram_test_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
    struct aw8697_container *aw8697_ramtest;
    int i ,j= 0;
    unsigned int val = 0;
    int rc = 0;
 //   unsigned int databuf[2] = {0};
    unsigned int start_addr;
 //   unsigned int end_addr;
    unsigned int tmp_len,retries;
    //unsigned char reg_val = 0;
    char *pbuf = NULL;
    pr_info("%s enter\n", __func__);

    rc = kstrtouint(buf, 0, &val);
    if (rc < 0)
        return rc;
    start_addr = 0;
    aw8697->ram_test_flag_0 = 0;
    aw8697->ram_test_flag_1 = 0;
    tmp_len = 1024 ; // /*1K*/
    retries= 8;  /*tmp_len * retries  =8*1024*/
    aw8697_ramtest = kzalloc(tmp_len*sizeof(char) +sizeof(int), GFP_KERNEL);
    if (!aw8697_ramtest) {
        pr_err("%s: error allocating memory\n", __func__);
        return count ;
    }
    pbuf = kzalloc(tmp_len*sizeof(char), GFP_KERNEL);
    if (!pbuf) {
        pr_err("%s: Error allocating memory\n", __func__);
        kfree(aw8697_ramtest);
        return count;
    }
    aw8697_ramtest->len = tmp_len;
    //printk("%s  --%d   aw8697_ramtest->len =%d \n",__func__,__LINE__,aw8697_ramtest->len);

    if (val ==1){
            /* RAMINIT Enable */
            aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);
        for(j=0;j<retries;j++){


             /*test 1   start*/
             memset(aw8697_ramtest->data, 0xff, aw8697_ramtest->len);
             memset(pbuf, 0x00, aw8697_ramtest->len);
             /* write ram 1 test */
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_writes(aw8697, AW8697_REG_RAMDATA,
                            aw8697_ramtest->data, aw8697_ramtest->len);

             /* read ram 1 test */
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_reads(aw8697, AW8697_REG_RAMDATA, pbuf, aw8697_ramtest->len);

                 for(i=0;i<aw8697_ramtest->len ;i++){
                    if(pbuf[i]  != 0xff ){
                     //   printk("%s---%d---pbuf[%d]= 0x%x\n",__func__,__LINE__,i,pbuf[i]);
                        aw8697->ram_test_flag_1++;
                    }
                 }
             /*test 1  end*/
            /*test 0 start*/
            memset(aw8697_ramtest->data, 0x00, aw8697_ramtest->len);
            memset(pbuf, 0xff, aw8697_ramtest->len);

            /* write ram 0 test */
            aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
            aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_writes(aw8697, AW8697_REG_RAMDATA,
                            aw8697_ramtest->data, aw8697_ramtest->len);

             /* read ram 0 test */
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, start_addr>>8);
             aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, start_addr&0x00FF);

             aw8697_i2c_reads(aw8697, AW8697_REG_RAMDATA, pbuf, aw8697_ramtest->len);

                for(i=0;i<aw8697_ramtest->len ;i++){
                    if(pbuf[i]  != 0 ){
                      //  printk("%s---%d---pbuf[%d]= 0x%x\n",__func__,__LINE__,i,pbuf[i]);
                         aw8697->ram_test_flag_0++;
                     }
                 }

             /*test 0 end*/
             start_addr += tmp_len;
        }
        /* RAMINIT Disable */
         aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
                     AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);
    }
    kfree(aw8697_ramtest);
    kfree(pbuf);
    pbuf = NULL;
    pr_info("%s exit\n", __func__);
    return count;
}

#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
static ssize_t aw8697_device_id_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
#ifdef TIMED_OUTPUT
    struct timed_output_dev *to_dev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
    struct led_classdev *cdev = dev_get_drvdata(dev);
    struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif

    return snprintf(buf, PAGE_SIZE, "%d\n", aw8697->device_id);
}

static ssize_t aw8697_device_id_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}
#endif

static DEVICE_ATTR(state, S_IWUSR | S_IRUGO, aw8697_state_show, aw8697_state_store);

//static DEVICE_ATTR(duration, S_IWUSR | S_IRUGO, aw8697_duration_show, aw8697_duration_store);
//static DEVICE_ATTR(activate, S_IWUSR | S_IRUGO, aw8697_activate_show, aw8697_activate_store);
static DEVICE_ATTR(duration, S_IWUSR | S_IWGRP | S_IRUGO, aw8697_duration_show, aw8697_duration_store);
static DEVICE_ATTR(activate, S_IWUSR | S_IWGRP | S_IRUGO, aw8697_activate_show, aw8697_activate_store);
static DEVICE_ATTR(activate_mode, S_IWUSR | S_IRUGO, aw8697_activate_mode_show, aw8697_activate_mode_store);
static DEVICE_ATTR(index, S_IWUSR | S_IRUGO, aw8697_index_show, aw8697_index_store);
static DEVICE_ATTR(vmax, S_IWUSR | S_IRUGO, aw8697_vmax_show, aw8697_vmax_store);
static DEVICE_ATTR(gain, S_IWUSR | S_IRUGO, aw8697_gain_show, aw8697_gain_store);
static DEVICE_ATTR(seq, S_IWUSR | S_IRUGO, aw8697_seq_show, aw8697_seq_store);
static DEVICE_ATTR(loop, S_IWUSR | S_IRUGO, aw8697_loop_show, aw8697_loop_store);
static DEVICE_ATTR(register, S_IWUSR | S_IRUGO, aw8697_reg_show, aw8697_reg_store);
static DEVICE_ATTR(rtp, S_IWUSR | S_IRUGO, aw8697_rtp_show, aw8697_rtp_store);
static DEVICE_ATTR(ram_update, S_IWUSR | S_IRUGO, aw8697_ram_update_show, aw8697_ram_update_store);
static DEVICE_ATTR(f0, S_IWUSR | S_IRUGO, aw8697_f0_show, aw8697_f0_store);
static DEVICE_ATTR(cali, S_IWUSR | S_IRUGO, aw8697_cali_show, aw8697_cali_store);
#ifdef CONFIG_OPLUS_HAPTIC_OOS
static DEVICE_ATTR(short_circuit_check, S_IWUSR | S_IRUGO, aw8697_short_check_show, aw8697_short_check_store);
#endif
static DEVICE_ATTR(cont, S_IWUSR | S_IRUGO, aw8697_cont_show, aw8697_cont_store);
static DEVICE_ATTR(cont_td, S_IWUSR | S_IRUGO, aw8697_cont_td_show, aw8697_cont_td_store);
static DEVICE_ATTR(cont_drv, S_IWUSR | S_IRUGO, aw8697_cont_drv_show, aw8697_cont_drv_store);
static DEVICE_ATTR(cont_num_brk, S_IWUSR | S_IRUGO, aw8697_cont_num_brk_show, aw8697_cont_num_brk_store);
static DEVICE_ATTR(cont_zc_thr, S_IWUSR | S_IRUGO, aw8697_cont_zc_thr_show, aw8697_cont_zc_thr_store);
static DEVICE_ATTR(vbat_monitor, S_IWUSR | S_IRUGO, aw8697_vbat_monitor_show, aw8697_vbat_monitor_store);
static DEVICE_ATTR(lra_resistance, S_IWUSR | S_IRUGO, aw8697_lra_resistance_show, aw8697_lra_resistance_store);
static DEVICE_ATTR(auto_boost, S_IWUSR | S_IRUGO, aw8697_auto_boost_show, aw8697_auto_boost_store);
static DEVICE_ATTR(prctmode, S_IWUSR | S_IRUGO, aw8697_prctmode_show, aw8697_prctmode_store);
static DEVICE_ATTR(trig, S_IWUSR | S_IRUGO, aw8697_trig_show, aw8697_trig_store);
static DEVICE_ATTR(ram_vbat_comp, S_IWUSR | S_IRUGO, aw8697_ram_vbat_comp_show, aw8697_ram_vbat_comp_store);
#ifdef OPLUS_FEATURE_CHG_BASIC
static DEVICE_ATTR(osc_cali, S_IWUSR | S_IRUGO, aw8697_osc_cali_show, aw8697_osc_cali_store);
#endif

static DEVICE_ATTR(rtp_num, S_IWUSR | S_IRUGO, aw8697_rtp_num_show, aw8697_rtp_num_store);
static DEVICE_ATTR(haptic_audio, S_IWUSR | S_IRUGO, aw8697_haptic_audio_show, aw8697_haptic_audio_store);
static DEVICE_ATTR(haptic_audio_time, S_IWUSR | S_IRUGO, aw8697_haptic_audio_time_show, aw8697_haptic_audio_time_store);
#ifdef OPLUS_FEATURE_CHG_BASIC
static DEVICE_ATTR(motor_old, S_IWUSR | S_IRUGO, aw8697_motor_old_test_show, aw8697_motor_old_test_store);
static DEVICE_ATTR(waveform_index, S_IWUSR | S_IRUGO, aw8697_waveform_index_show, aw8697_waveform_index_store);
static DEVICE_ATTR(osc_data, S_IWUSR | S_IRUGO, aw8697_osc_data_show, aw8697_osc_data_store);
static DEVICE_ATTR(ram_test, S_IWUSR | S_IRUGO, aw8697_haptic_ram_test_show, aw8697_haptic_ram_test_store);
static DEVICE_ATTR(f0_data, S_IWUSR | S_IRUGO, aw8697_f0_data_show, aw8697_f0_data_store);
static DEVICE_ATTR(rtp_going, S_IWUSR | S_IRUGO, aw8697_rtp_going_show, aw8697_rtp_going_store);
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
static DEVICE_ATTR(device_id, S_IWUSR | S_IRUGO, aw8697_device_id_show, aw8697_device_id_store);
#endif

static DEVICE_ATTR(gun_type, S_IWUSR | S_IRUGO, aw8697_gun_type_show, aw8697_gun_type_store);   //hch 20190917
static DEVICE_ATTR(bullet_nr, S_IWUSR | S_IRUGO, aw8697_bullet_nr_show, aw8697_bullet_nr_store);    //hch 20190917
static DEVICE_ATTR(gun_mode, S_IWUSR | S_IRUGO, aw8697_gun_mode_show, aw8697_gun_mode_store);   //hch 20190917

static struct attribute *aw8697_vibrator_attributes[] = {
    &dev_attr_state.attr,
    &dev_attr_duration.attr,
    &dev_attr_activate.attr,
    &dev_attr_activate_mode.attr,
    &dev_attr_index.attr,
    &dev_attr_vmax.attr,
    &dev_attr_gain.attr,
    &dev_attr_seq.attr,
    &dev_attr_loop.attr,
    &dev_attr_register.attr,
    &dev_attr_rtp.attr,
    &dev_attr_ram_update.attr,
    &dev_attr_f0.attr,
    &dev_attr_cali.attr,
#ifdef CONFIG_OPLUS_HAPTIC_OOS
	&dev_attr_short_circuit_check.attr,
#endif
    &dev_attr_cont.attr,
    &dev_attr_cont_td.attr,
    &dev_attr_cont_drv.attr,
    &dev_attr_cont_num_brk.attr,
    &dev_attr_cont_zc_thr.attr,
    &dev_attr_vbat_monitor.attr,
    &dev_attr_lra_resistance.attr,
    &dev_attr_auto_boost.attr,
    &dev_attr_prctmode.attr,
    &dev_attr_trig.attr,
    &dev_attr_ram_vbat_comp.attr,
    &dev_attr_osc_cali.attr,
    &dev_attr_rtp_num.attr,
    &dev_attr_haptic_audio.attr,
    &dev_attr_haptic_audio_time.attr,
#ifdef OPLUS_FEATURE_CHG_BASIC
    &dev_attr_motor_old.attr,
    &dev_attr_waveform_index.attr,
    &dev_attr_osc_data.attr,
    &dev_attr_ram_test.attr,
    &dev_attr_f0_data.attr,
    &dev_attr_rtp_going.attr,
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
    &dev_attr_device_id.attr,
#endif
    &dev_attr_gun_type.attr,    //hch 20190917
    &dev_attr_bullet_nr.attr,   //hch 20190917
    &dev_attr_gun_mode.attr,    //hch 20190917
    NULL
};

static struct attribute_group aw8697_vibrator_attribute_group = {
    .attrs = aw8697_vibrator_attributes
};

static enum hrtimer_restart aw8697_vibrator_timer_func(struct hrtimer *timer)
{
    struct aw8697 *aw8697 = container_of(timer, struct aw8697, timer);

    pr_debug("%s enter\n", __func__);
    aw8697->state = 0;
    //schedule_work(&aw8697->vibrator_work);
    queue_work(system_highpri_wq, &aw8697->vibrator_work);

    return HRTIMER_NORESTART;
}

static void aw8697_vibrator_work_routine(struct work_struct *work)
{
      unsigned int val = 0;
    struct aw8697 *aw8697 = container_of(work, struct aw8697, vibrator_work);

#ifdef OPLUS_FEATURE_CHG_BASIC
    aw8697->activate_mode = AW8697_HAPTIC_ACTIVATE_RAM_MODE;
    pr_err("%s enter, aw8697->state[%d], aw8697->activate_mode[%d], aw8697->ram_vbat_comp[%d]\n",
                __func__, aw8697->state, aw8697->activate_mode, aw8697->ram_vbat_comp);
#endif
    mutex_lock(&aw8697->lock);
    aw8697_haptic_stop(aw8697);
    mutex_unlock(&aw8697->lock);
    //aw8697_haptic_android_stop(aw8697);
    if(aw8697->state) {
        mutex_lock(&aw8697->lock);
        if(aw8697->activate_mode == AW8697_HAPTIC_ACTIVATE_RAM_MODE) {

			if (aw8697->device_id == 832 || aw8697->device_id == 833 || aw8697->device_id == 81538) {
				aw8697_haptic_ram_vbat_comp(aw8697, false);
			} else {
				aw8697_haptic_ram_vbat_comp(aw8697, true);
			}
            aw8697_haptic_play_repeat_seq(aw8697, true);
        } else if(aw8697->activate_mode == AW8697_HAPTIC_ACTIVATE_CONT_MODE) {
            aw8697_haptic_cont(aw8697);
        }
        mutex_unlock(&aw8697->lock);
        val = aw8697->duration;
        /* run ms timer */
        hrtimer_start(&aw8697->timer,
                  ktime_set(val / 1000, (val % 1000) * 1000000),
                  HRTIMER_MODE_REL);
    }
}

static int aw8697_vibrator_init(struct aw8697 *aw8697)
{
    int ret = 0;

    pr_info("%s enter\n", __func__);

#ifdef TIMED_OUTPUT
    aw8697->to_dev.name = "vibrator";
    aw8697->to_dev.get_time = aw8697_vibrator_get_time;
    aw8697->to_dev.enable = aw8697_vibrator_enable;

    ret = timed_output_dev_register(&(aw8697->to_dev));
    if ( ret < 0){
        dev_err(aw8697->dev, "%s: fail to create timed output dev\n",
                __func__);
        return ret;
    }
    ret = sysfs_create_group(&aw8697->to_dev.dev->kobj, &aw8697_vibrator_attribute_group);
    if (ret < 0) {
        dev_err(aw8697->dev, "%s error creating sysfs attr files\n", __func__);
        return ret;
    }
#else
    aw8697->cdev.name = "vibrator";
    aw8697->cdev.brightness_get = aw8697_haptic_brightness_get;
    aw8697->cdev.brightness_set = aw8697_haptic_brightness_set;

    ret = devm_led_classdev_register(&aw8697->i2c->dev, &aw8697->cdev);
    if (ret < 0){
        dev_err(aw8697->dev, "%s: fail to create led dev\n",
                __func__);
        return ret;
    }
    ret = sysfs_create_group(&aw8697->cdev.dev->kobj, &aw8697_vibrator_attribute_group);
    if (ret < 0) {
        dev_err(aw8697->dev, "%s error creating sysfs attr files\n", __func__);
        return ret;
     }
#endif
    hrtimer_init(&aw8697->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    aw8697->timer.function = aw8697_vibrator_timer_func;
    INIT_WORK(&aw8697->vibrator_work, aw8697_vibrator_work_routine);

    INIT_WORK(&aw8697->rtp_work, aw8697_rtp_work_routine);

    INIT_WORK(&aw8697->rtp_single_cycle_work, aw8697_rtp_single_cycle_routine);
    INIT_WORK(&aw8697->rtp_regroup_work, aw8697_rtp_regroup_routine);
    mutex_init(&aw8697->lock);

    return 0;
}




/******************************************************
 *
 * irq
 *
 ******************************************************/
static void aw8697_interrupt_clear(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;
    //pr_info("%s enter\n", __func__);
    aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    //pr_info("%s: reg SYSINT=0x%x\n", __func__, reg_val);
}

static void aw8697_interrupt_setup(struct aw8697 *aw8697)
{
    unsigned char reg_val = 0;

    //pr_info("%s enter\n", __func__);

    aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    //pr_info("%s: reg SYSINT=0x%x\n", __func__, reg_val);

    /* edge int mode */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_DBGCTRL,
            AW8697_BIT_DBGCTRL_INT_MODE_MASK, AW8697_BIT_DBGCTRL_INT_MODE_EDGE);

    /* int enable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_BSTERR_MASK, AW8697_BIT_SYSINTM_BSTERR_OFF);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_OV_MASK, AW8697_BIT_SYSINTM_OV_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_UVLO_MASK, AW8697_BIT_SYSINTM_UVLO_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_OCD_MASK, AW8697_BIT_SYSINTM_OCD_EN);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSINTM,
            AW8697_BIT_SYSINTM_OT_MASK, AW8697_BIT_SYSINTM_OT_EN);
}

static irqreturn_t aw8697_irq(int irq, void *data)
{
    struct aw8697 *aw8697 = data;
    unsigned char reg_val = 0;
    unsigned char dbg_val = 0;
    unsigned int buf_len = 0;
	unsigned char glb_state_val = 0x0;

    pr_debug("%s enter\n", __func__);
#ifdef AAC_RICHTAP
    if(aw8697->haptic_rtp_mode) {
        return IRQ_HANDLED;
    }
#endif
    aw8697_pm_qos_enable(aw8697, true);

    aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    //pr_info("%s: reg SYSINT=0x%x\n", __func__, reg_val);
    aw8697_i2c_read(aw8697, AW8697_REG_DBGSTAT, &dbg_val);
    //pr_info("%s: reg DBGSTAT=0x%x\n", __func__, dbg_val);

    if(reg_val & AW8697_BIT_SYSINT_OVI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip ov int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_UVLI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip uvlo int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_OCDI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip over current int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_OTI) {
        aw8697_op_clean_status(aw8697);
        pr_err("%s chip over temperature int error\n", __func__);
    }
    if(reg_val & AW8697_BIT_SYSINT_DONEI) {
        aw8697_op_clean_status(aw8697);
        pr_info("%s chip playback done\n", __func__);
    }

    if(reg_val & AW8697_BIT_SYSINT_FF_AEI) {
        pr_debug("%s: aw8697 rtp fifo almost empty int\n", __func__);
        if(aw8697->rtp_init) {
            while((!aw8697_haptic_rtp_get_fifo_afi(aw8697)) &&
                    (aw8697->play_mode == AW8697_HAPTIC_RTP_MODE)) {
                pr_debug("%s: aw8697 rtp mode fifo update, cnt=%d\n",
                        __func__, aw8697->rtp_cnt);
                if (!aw8697_rtp) {
                   pr_info("%s:aw8697_rtp is null break\n",
                        __func__);
                   break;
                }
                mutex_lock(&aw8697->rtp_lock);//vincent
                if((aw8697_rtp->len-aw8697->rtp_cnt) < (aw8697->ram.base_addr>>2)) {
                    buf_len = aw8697_rtp->len-aw8697->rtp_cnt;
                } else {
                    buf_len = (aw8697->ram.base_addr>>2);
                }
			if (aw8697->rtp_cnt <= aw8697_rtp->len)
				aw8697_i2c_writes(aw8697, AW8697_REG_RTP_DATA, &aw8697_rtp->data[aw8697->rtp_cnt], buf_len);
                aw8697->rtp_cnt += buf_len;
				aw8697_i2c_read(aw8697, AW8697_REG_GLB_STATE, &glb_state_val);
				if (aw8697->rtp_cnt >= aw8697_rtp->len || (glb_state_val & 0x0f) == 0) {
					aw8697_op_clean_status(aw8697);
					pr_info("%s: rtp update complete,glb_state_val=0x%x\n", __func__, glb_state_val);
                    aw8697_haptic_set_rtp_aei(aw8697, false);
                    aw8697->rtp_cnt = 0;
                    aw8697->rtp_init = 0;
                    //
                    mutex_unlock(&aw8697->rtp_lock);//vincent
                    break;
                }
                //
                mutex_unlock(&aw8697->rtp_lock);//vincent
            }
        } else {
            pr_err("%s: aw8697 rtp init = %d, init error\n", __func__, aw8697->rtp_init);
        }
    }

    if(reg_val & AW8697_BIT_SYSINT_FF_AFI) {
        pr_debug("%s: aw8697 rtp mode fifo full empty\n", __func__);
    }

    if(aw8697->play_mode != AW8697_HAPTIC_RTP_MODE) {
        aw8697_haptic_set_rtp_aei(aw8697, false);
    }
    aw8697_pm_qos_enable(aw8697, false);

    aw8697_i2c_read(aw8697, AW8697_REG_SYSINT, &reg_val);
    pr_debug("%s: reg SYSINT=0x%x\n", __func__, reg_val);
    aw8697_i2c_read(aw8697, AW8697_REG_SYSST, &reg_val);
    pr_debug("%s: reg SYSST=0x%x\n", __func__, reg_val);

    pr_debug("%s exit\n", __func__);

    return IRQ_HANDLED;
}

/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static int aw8697_parse_dt(struct device *dev, struct aw8697 *aw8697,
        struct device_node *np) {
    aw8697->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
    if (aw8697->reset_gpio < 0) {
        dev_err(dev, "%s: no reset gpio provided, will not HW reset device\n", __func__);
        return -1;
    } else {
        dev_info(dev, "%s: reset gpio provided ok\n", __func__);
    }
    aw8697->irq_gpio =  of_get_named_gpio(np, "irq-gpio", 0);
    if (aw8697->irq_gpio < 0) {
        dev_err(dev, "%s: no irq gpio provided.\n", __func__);
    } else {
        dev_info(dev, "%s: irq gpio provided ok.\n", __func__);
    }
#ifdef OPLUS_FEATURE_CHG_BASIC
    if (of_property_read_u32(np, "qcom,device_id", &aw8697->device_id))
        aw8697->device_id = 815;
    dev_info(dev, "%s: aw8697->device_id=%d\n", __func__, aw8697->device_id);
	if (of_property_read_u8(np, "qcom,aw8697_boost_voltage", &AW8697_HAPTIC_HIGH_LEVEL_REG_VAL)) {
#ifdef CONFIG_OPLUS_HAPTIC_OOS
		AW8697_HAPTIC_HIGH_LEVEL_REG_VAL = 0x16;
#else
		AW8697_HAPTIC_HIGH_LEVEL_REG_VAL = 0x18;
#endif
	}
	dev_info(dev, "%s: aw8697 boost_voltage=%d\n", __func__, AW8697_HAPTIC_HIGH_LEVEL_REG_VAL);
#endif
    return 0;
}

static int aw8697_hw_reset(struct aw8697 *aw8697)
{
    pr_info("%s enter\n", __func__);

    if (aw8697 && gpio_is_valid(aw8697->reset_gpio)) {
        gpio_set_value_cansleep(aw8697->reset_gpio, 0);
        usleep_range(1000, 2000);
        gpio_set_value_cansleep(aw8697->reset_gpio, 1);
        usleep_range(3500, 4000);
    } else {
        if (aw8697)
           dev_err(aw8697->dev, "%s:  failed\n", __func__);
    }
    return 0;
}

static void aw8697_haptic_reset_init(struct aw8697 *aw8697)
{
   // int ret = 0;

    unsigned char reg_val = 0;

    pr_info("%s enter software reset\n", __func__);


    /* haptic init */
    aw8697_haptic_play_mode(aw8697, AW8697_HAPTIC_STANDBY_MODE);
    aw8697_haptic_set_pwm(aw8697, AW8697_PWM_24K);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG1, 0x30);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG2, 0xeb);
    aw8697_i2c_write(aw8697, AW8697_REG_BSTDBG3, 0xd4);
    aw8697_i2c_write(aw8697, AW8697_REG_TSET, 0x12);
    aw8697_i2c_write(aw8697, AW8697_REG_R_SPARE, 0x68);
    aw8697_i2c_write_bits(aw8697, AW8697_REG_ANADBG,
            AW8697_BIT_ANADBG_IOC_MASK, AW8697_BIT_ANADBG_IOC_4P65A);

    aw8697_haptic_set_bst_peak_cur(aw8697, AW8697_DEFAULT_PEAKCUR);
    aw8697_haptic_swicth_motorprotect_config(aw8697, 0x00, 0x00);
    aw8697_haptic_auto_boost_config(aw8697, false);
    aw8697_haptic_trig_param_init(aw8697);
    aw8697_haptic_trig_param_config(aw8697);
   aw8697_haptic_os_calibration(aw8697);
    aw8697_haptic_cont_vbat_mode(aw8697,
            AW8697_HAPTIC_CONT_VBAT_HW_COMP_MODE);
    aw8697->ram_vbat_comp = AW8697_HAPTIC_RAM_VBAT_COMP_ENABLE;

    aw8697_i2c_read(aw8697, AW8697_REG_TRIM_LRA, &reg_val);
    aw8697_i2c_write(aw8697, AW8697_REG_TRIM_LRA, reg_val);
}

/*****************************************************
 *
 * check chip id
 *
 *****************************************************/
static int aw8697_read_chipid(struct aw8697 *aw8697)
{
    int ret = -1;
    unsigned char cnt = 0;
    unsigned char reg = 0;

    while(cnt < AW_READ_CHIPID_RETRIES) {
        /* hardware reset */
        aw8697_hw_reset(aw8697);

        ret = aw8697_i2c_read(aw8697, AW8697_REG_ID, &reg);
        if (ret < 0) {
            dev_err(aw8697->dev, "%s: failed to read register AW8697_REG_ID: %d\n", __func__, ret);
        }
        switch (reg) {
        case AW8697_CHIPID:
            pr_info("%s aw8697 detected\n", __func__);
            aw8697->chipid = AW8697_CHIPID;
            //aw8697->flags |= AW8697_FLAG_SKIP_INTERRUPTS;
            aw8697_haptic_softreset(aw8697);
            return 0;
        default:
            pr_info("%s unsupported device revision (0x%x)\n", __func__, reg );
            break;
        }
        cnt ++;

        msleep(AW_READ_CHIPID_RETRY_DELAY);
    }

    return -EINVAL;
}

/******************************************************
 *
 * sys group attribute: reg
 *
 ******************************************************/
static ssize_t aw8697_i2c_reg_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);

    unsigned int databuf[2] = {0, 0};

    if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        aw8697_i2c_write(aw8697, (unsigned char)databuf[0], (unsigned char)databuf[1]);
    }

    return count;
}

static ssize_t aw8697_i2c_reg_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);
    ssize_t len = 0;
    unsigned char i = 0;
    unsigned char reg_val = 0;
    for(i = 0; i < AW8697_REG_MAX; i ++) {
        if(!(aw8697_reg_access[i]&REG_RD_ACCESS))
           continue;
        aw8697_i2c_read(aw8697, i, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x \n", i, reg_val);
    }
    return len;
}
static ssize_t aw8697_i2c_ram_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);

    unsigned int databuf[1] = {0};

    if(1 == sscanf(buf, "%x", &databuf[0])) {
        if(1 == databuf[0]) {
            aw8697_ram_update(aw8697);
        }
    }

    return count;
}

static ssize_t aw8697_i2c_ram_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    struct aw8697 *aw8697 = dev_get_drvdata(dev);
    ssize_t len = 0;
    unsigned int i = 0;
    unsigned char reg_val = 0;

    aw8697_haptic_stop(aw8697);
    /* RAMINIT Enable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_EN);

    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRH, (unsigned char)(aw8697->ram.base_addr>>8));
    aw8697_i2c_write(aw8697, AW8697_REG_RAMADDRL, (unsigned char)(aw8697->ram.base_addr&0x00ff));
    len += snprintf(buf+len, PAGE_SIZE-len, "aw8697_haptic_ram:\n");
    for(i=0; i<aw8697->ram.len; i++) {
        aw8697_i2c_read(aw8697, AW8697_REG_RAMDATA, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "0x%02x,", reg_val);
    }
    len += snprintf(buf+len, PAGE_SIZE-len, "\n");
    /* RAMINIT Disable */
    aw8697_i2c_write_bits(aw8697, AW8697_REG_SYSCTRL,
            AW8697_BIT_SYSCTRL_RAMINIT_MASK, AW8697_BIT_SYSCTRL_RAMINIT_OFF);

    return len;
}

static DEVICE_ATTR(reg, S_IWUSR | S_IRUGO, aw8697_i2c_reg_show, aw8697_i2c_reg_store);
static DEVICE_ATTR(ram, S_IWUSR | S_IRUGO, aw8697_i2c_ram_show, aw8697_i2c_ram_store);

static struct attribute *aw8697_attributes[] = {
    &dev_attr_reg.attr,
    &dev_attr_ram.attr,
    NULL
};

static struct attribute_group aw8697_attribute_group = {
    .attrs = aw8697_attributes
};

/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw8697_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
    struct aw8697 *aw8697;
    struct device_node *np = i2c->dev.of_node;
    int irq_flags = 0;
    int ret = -1;

    pr_info("%s enter\n", __func__);

    AW8697_HAPTIC_RAM_VBAT_COMP_GAIN = 0x80;

    if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
        dev_err(&i2c->dev, "check_functionality failed\n");
        return -EIO;
    }

    aw8697 = devm_kzalloc(&i2c->dev, sizeof(struct aw8697), GFP_KERNEL);
    if (aw8697 == NULL)
        return -ENOMEM;

    aw8697->dev = &i2c->dev;
    aw8697->i2c = i2c;

    i2c_set_clientdata(i2c, aw8697);

    /* aw8697 rst & int */
    if (np) {
        ret = aw8697_parse_dt(&i2c->dev, aw8697, np);
        if (ret) {
            dev_err(&i2c->dev, "%s: failed to parse device tree node\n", __func__);
            goto err_parse_dt;
        }
    } else {
        aw8697->reset_gpio = -1;
        aw8697->irq_gpio = -1;
    }

#if defined (OPLUS_FEATURE_CHG_BASIC) && !defined(CONFIG_OPLUS_HAPTIC_OOS)
	if (aw8697->device_id == DEV_ID_0619)
		AW8697_HAPTIC_RAM_VBAT_COMP_GAIN = 0x3B;
#endif

#ifdef CONFIG_OPLUS_HAPTIC_OOS
	if (aw8697->device_id == DEV_ID_0619) {
		one_sine_data = one_sine_data_0619;
		rtp_500ms_first_sine_place = RTP_500MS_FIRST_SINE_PLACE_0619;
		one_sine_data_length = ONE_SINE_DATA_LENGTH_0619;
		one_sine_data_time = ONE_SINE_DATA_TIME_0619;
	} else if (aw8697->device_id == DEV_ID_0832) {
		one_sine_data = one_sine_data_0832;
		rtp_500ms_first_sine_place = RTP_500MS_FIRST_SINE_PLACE_0832;
		one_sine_data_length = ONE_SINE_DATA_LENGTH_0832;
		one_sine_data_time = ONE_SINE_DATA_TIME_0832;
	} else if (aw8697->device_id == DEV_ID_1040) {
		one_sine_data = one_sine_data_1040;
                rtp_500ms_first_sine_place = RTP_500MS_FIRST_SINE_PLACE_1040;
                one_sine_data_length = ONE_SINE_DATA_LENGTH_1040;
                one_sine_data_time = ONE_SINE_DATA_TIME_1040;
	} else {
		one_sine_data = one_sine_data_1815;
		rtp_500ms_first_sine_place = RTP_500MS_FIRST_SINE_PLACE_1815;
		one_sine_data_length = ONE_SINE_DATA_LENGTH_1815;
		one_sine_data_time = ONE_SINE_DATA_TIME_1815;
	}
#endif

    if (gpio_is_valid(aw8697->reset_gpio)) {
        ret = devm_gpio_request_one(&i2c->dev, aw8697->reset_gpio,
            GPIOF_OUT_INIT_LOW, "aw8697_rst");
        if (ret){
            dev_err(&i2c->dev, "%s: rst request failed\n", __func__);
            goto err_reset_gpio_request;
        }
    }

    if (gpio_is_valid(aw8697->irq_gpio)) {
        ret = devm_gpio_request_one(&i2c->dev, aw8697->irq_gpio,
            GPIOF_DIR_IN, "aw8697_int");
        if (ret){
            dev_err(&i2c->dev, "%s: int request failed\n", __func__);
            goto err_irq_gpio_request;
        }
    }

    /* aw8697 chip id */
    ret = aw8697_read_chipid(aw8697);
    if (ret < 0) {
        dev_err(&i2c->dev, "%s: aw8697_read_chipid failed ret=%d\n", __func__, ret);
        goto err_id;
    }

// add by huangtongfeng for vmalloc mem
    ret = aw8697_container_init(aw8697_container_size);
    if (ret < 0) {
        dev_err(&i2c->dev, "%s: aw8697_rtp alloc memory failed \n", __func__);
    }
// add end

    /* aw8697 irq */
    if (gpio_is_valid(aw8697->irq_gpio) &&
        !(aw8697->flags & AW8697_FLAG_SKIP_INTERRUPTS)) {
        /* register irq handler */
        aw8697_interrupt_setup(aw8697);
        irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
        ret = devm_request_threaded_irq(&i2c->dev,
                    gpio_to_irq(aw8697->irq_gpio),
                    NULL, aw8697_irq, irq_flags,
                    "aw8697", aw8697);
        if (ret != 0) {
            dev_err(&i2c->dev, "%s: failed to request IRQ %d: %d\n",
                    __func__, gpio_to_irq(aw8697->irq_gpio), ret);
            goto err_irq;
        }
    } else {
        dev_info(&i2c->dev, "%s skipping IRQ registration\n", __func__);
        /* disable feature support if gpio was invalid */
        aw8697->flags |= AW8697_FLAG_SKIP_INTERRUPTS;
    }

    dev_set_drvdata(&i2c->dev, aw8697);
#ifdef AAC_RICHTAP
    aw8697->rtp_ptr = kmalloc(RICHTAP_MMAP_BUF_SIZE * RICHTAP_MMAP_BUF_SUM, GFP_KERNEL);
    if(aw8697->rtp_ptr == NULL) {
        dev_err(&i2c->dev, "malloc rtp memory failed\n");
        return -ENOMEM;
    }

    aw8697->start_buf = (struct mmap_buf_format *)__get_free_pages(GFP_KERNEL, RICHTAP_MMAP_PAGE_ORDER);
	if (aw8697->start_buf == NULL)    {
        dev_err(&i2c->dev, "Error __get_free_pages failed\n");
        return -ENOMEM;
    }
    SetPageReserved(virt_to_page(aw8697->start_buf));
    {
        struct mmap_buf_format *temp;
        uint32_t i = 0;
        temp = aw8697->start_buf;
        for( i = 1; i < RICHTAP_MMAP_BUF_SUM; i++)
        {
			temp->kernel_next = (aw8697->start_buf + i);
			temp = temp->kernel_next;
        }
        temp->kernel_next = aw8697->start_buf;
    }

    INIT_WORK(&aw8697->haptic_rtp_work, rtp_work_proc);
    //init_waitqueue_head(&aw8697->doneQ);
    aw8697->done_flag = true;
    aw8697->haptic_rtp_mode = false;
#endif
	ret = sysfs_create_group(&i2c->dev.kobj, &aw8697_attribute_group);
	if (ret < 0) {
        dev_info(&i2c->dev, "%s error creating sysfs attr files\n", __func__);
        goto err_sysfs;
	}

    g_aw8697 = aw8697;

    aw8697_vibrator_init(aw8697);

    aw8697_haptic_init(aw8697);

    aw8697_ram_init(aw8697);
#ifdef OPLUS_FEATURE_CHG_BASIC
    INIT_WORK(&aw8697->motor_old_test_work, motor_old_test_work);
    aw8697->motor_old_test_mode = 0;
#endif
#ifdef CONFIG_OPLUS_HAPTIC_OOS
#ifdef CONFIG_QGKI
	aw8697->boot_mode = get_boot_mode();
#else
	aw8697->boot_mode = MSM_BOOT_MODE__NORMAL;
#endif
	if ((aw8697->boot_mode == MSM_BOOT_MODE__FACTORY || aw8697->boot_mode == MSM_BOOT_MODE__RF || aw8697->boot_mode == MSM_BOOT_MODE__WLAN)) {
		aw8697->gain = 0x60;
	}
#endif
    pr_info("%s probe completed successfully!\n", __func__);

    return 0;

err_sysfs:
    devm_free_irq(&i2c->dev, gpio_to_irq(aw8697->irq_gpio), aw8697);
#ifdef AAC_RICHTAP
    kfree(aw8697->rtp_ptr);
    free_pages((unsigned long)aw8697->start_buf, RICHTAP_MMAP_PAGE_ORDER);
#endif
err_irq:
err_id:
    if (gpio_is_valid(aw8697->irq_gpio))
        devm_gpio_free(&i2c->dev, aw8697->irq_gpio);
err_irq_gpio_request:
    if (gpio_is_valid(aw8697->reset_gpio))
        devm_gpio_free(&i2c->dev, aw8697->reset_gpio);
err_reset_gpio_request:
err_parse_dt:
    devm_kfree(&i2c->dev, aw8697);
    aw8697 = NULL;
    return ret;
}

static int aw8697_i2c_remove(struct i2c_client *i2c)
{
    struct aw8697 *aw8697 = i2c_get_clientdata(i2c);

    pr_info("%s enter\n", __func__);

    sysfs_remove_group(&i2c->dev.kobj, &aw8697_attribute_group);

    devm_free_irq(&i2c->dev, gpio_to_irq(aw8697->irq_gpio), aw8697);

    if (gpio_is_valid(aw8697->irq_gpio))
        devm_gpio_free(&i2c->dev, aw8697->irq_gpio);
    if (gpio_is_valid(aw8697->reset_gpio))
        devm_gpio_free(&i2c->dev, aw8697->reset_gpio);
#ifdef AAC_RICHTAP
    kfree(aw8697->rtp_ptr);
    free_pages((unsigned long)aw8697->start_buf, RICHTAP_MMAP_PAGE_ORDER);
#endif
    devm_kfree(&i2c->dev, aw8697);
    aw8697 = NULL;

    return 0;
}


static int __maybe_unused aw8697_suspend(struct device *dev)
{

    int ret = 0;
     struct aw8697 *aw8697 = dev_get_drvdata(dev);
     mutex_lock(&aw8697->lock);
     aw8697_haptic_stop(aw8697);
     mutex_unlock(&aw8697->lock);

    return ret;
}

static int __maybe_unused aw8697_resume(struct device *dev)
{

    int ret = 0;


    return ret;
}


static SIMPLE_DEV_PM_OPS(aw8697_pm_ops, aw8697_suspend, aw8697_resume);

static const struct i2c_device_id aw8697_i2c_id[] = {
    { AW8697_I2C_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, aw8697_i2c_id);

static struct of_device_id aw8697_dt_match[] = {
    { .compatible = "awinic,aw8697_haptic" },
    { },
};

static struct i2c_driver aw8697_i2c_driver = {
    .driver = {
        .name = AW8697_I2C_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(aw8697_dt_match),
        .pm = &aw8697_pm_ops, // vincent add for QC's sugesstion suspend and resume 20190113
    },
    .probe = aw8697_i2c_probe,
    .remove = aw8697_i2c_remove,
    .id_table = aw8697_i2c_id,
};


static int __init aw8697_i2c_init(void)
{
    int ret = 0;

    pr_info("aw8697 driver version %s\n", AW8697_VERSION);

    ret = i2c_add_driver(&aw8697_i2c_driver);
    if(ret){
        pr_err("fail to add aw8697 device into i2c\n");
        return ret;
    }

    return 0;
}


//late_initcall(aw8697_i2c_init);
module_init(aw8697_i2c_init);


static void __exit aw8697_i2c_exit(void)
{
    i2c_del_driver(&aw8697_i2c_driver);
}
module_exit(aw8697_i2c_exit);


MODULE_DESCRIPTION("AW8697 Haptic Driver");
MODULE_LICENSE("GPL v2");
