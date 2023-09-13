# Copyright (C), 2008-2030, OPLUS Mobile Comm Corp., Ltd
### All rights reserved.
###
### File: - OplusKernelEnvConfig.mk
### Description:
###     you can get the oplus feature variables set in android side in this file
###     this file will add global macro for common oplus added feature
###     BSP team can do customzation by referring the feature variables
### Version: 1.0
### Date: 2020-03-18
### Author: Liang.Sun
###
### ------------------------------- Revision History: ----------------------------
### <author>                        <date>       <version>   <desc>
### ------------------------------------------------------------------------------
##################################################################################

-include oplus_native_features.mk

###ifdef OPLUS_ARCH_INJECT
OPLUS_CONNECTIVITY_NATIVE_FEATURE_SET :=

##Add OPLUS Debug/Feature Macro Support for kernel/driver
##ifeq ($(OPLUS_FEATURE_TEST), yes)
## OPLUS_CONNECTIVITY_NATIVE_FEATURE_SET += OPLUS_FEATURE_TEST
##endif

ifeq ($(OPLUS_FEATURE_WIFI_MTUDETECT), yes)
OPLUS_CONNECTIVITY_NATIVE_FEATURE_SET += OPLUS_FEATURE_WIFI_MTUDETECT
endif

$(foreach myfeature,$(OPLUS_CONNECTIVITY_NATIVE_FEATURE_SET),\
    $( \
        $(eval KBUILD_CFLAGS += -D$(myfeature)) \
        $(eval KBUILD_CPPFLAGS += -D$(myfeature)) \
        $(eval CFLAGS_KERNEL += -D$(myfeature)) \
        $(eval CFLAGS_MODULE += -D$(myfeature)) \
    ) \
)
###endif OPLUS_ARCH_INJECT

ALLOWED_MCROS := \
OPLUS_FEATURE_UFS_DRIVER  \
OPLUS_FEATURE_UFS_SHOW_LATENCY \
OPLUS_FEATURE_UFSPLUS  \
OPLUS_FEATURE_TP_BSPFWUPDATE  \
OPLUS_FEATURE_CHG_BASIC  \
OPLUS_FEATURE_WIFI_BDF  \
OPLUS_FEATURE_SENSOR_SMEM  \
OPLUS_FEATURE_TP_BASIC  \
OPLUS_FEATURE_PXLW_IRIS5  \
OPLUS_FEATURE_AUDIO_FTM \
OPLUS_FEATURE_SPEAKER_MUTE \
OPLUS_FEATURE_MI2S_SLAVE \
OPLUS_FEATURE_KTV \
OPLUS_FEATURE_CAMERA_OIS  \
OPLUS_BUG_COMPATIBILITY \
OPLUS_BUG_STABILITY \
OPLUS_BUG_DEBUG \
OPLUS_ARCH_INJECT \
OPLUS_ARCH_EXTENDS \
OPLUS_FEATURE_AUDIODETECT \
VENDOR_EDIT \
OPLUS_FEATURE_CAMERA_COMMON \
OPLUS_FEATURE_ADSP_RECOVERY \
OPLUS_FEATURE_SMARTPA_PM \
OPLUS_FEATURE_IMPEDANCE_MATCH \
OPLUS_FEATURE_WIFI_DCS_SWITCH \
OPLUS_FEATURE_MIC_VA_MIC_CLK_SWITCH

ifeq ($(OPLUS_FEATURE_ADFR_KERNEL), yes)
    ALLOWED_MCROS += OPLUS_FEATURE_ADFR
endif

$(foreach myfeature,$(ALLOWED_MCROS),\
         $(eval KBUILD_CFLAGS += -D$(myfeature)) \
         $(eval KBUILD_CPPFLAGS += -D$(myfeature)) \
         $(eval CFLAGS_KERNEL += -D$(myfeature)) \
         $(eval CFLAGS_MODULE += -D$(myfeature)) \
)

# BSP team can do customzation by referring the feature variables

ifeq ($(OPLUS_FEATURE_AOD_RAMLESS),yes)
KBUILD_CFLAGS += -DOPLUS_FEATURE_AOD_RAMLESS
KBUILD_CPPFLAGS += -DOPLUS_FEATURE_AOD_RAMLESS
CFLAGS_KERNEL += -DOPLUS_FEATURE_AOD_RAMLESS
CFLAGS_MODULE += -DOPLUS_FEATURE_AOD_RAMLESS
endif

ifeq ($(OPLUS_FEATURE_UFS_SHOW_LATENCY),yes)
export OPLUS_FEATURE_UFS_SHOW_LATENCY=y
endif

ifeq ($(OPLUS_FEATURE_UFSPLUS),yes)
export OPLUS_FEATURE_UFSPLUS=y
endif

ifeq ($(OPLUS_FEATURE_ADFR_KERNEL),yes)
export OPLUS_FEATURE_ADFR_KERNEL=yes
endif

ifeq ($(OPLUS_FEATURE_PXLW_IRIS5),yes)
export OPLUS_FEATURE_PXLW_IRIS5=yes
endif
