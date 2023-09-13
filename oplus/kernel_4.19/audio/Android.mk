# Android makefile for oplus audio kernel modules
MY_LOCAL_PATH := $(call my-dir)

ifeq ($(call is-board-platform-in-list,msmnile $(MSMSTEPPE) $(TRINKET) kona lito bengal sdmshrike),true)
UAPI_OUT := $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/include
$(shell mkdir -p $(UAPI_OUT)/linux;)
$(shell mkdir -p $(UAPI_OUT)/sound;)
endif

$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/oplus/kernel_4.19/audio/driver/Module.symvers)
include $(MY_LOCAL_PATH)/driver/Android.mk

#Add for max98937 codec
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/oplus/kernel_4.19/audio/codecs/max989xx/Module.symvers)
include $(MY_LOCAL_PATH)/codecs/max989xx/Android.mk

#Add for tfa98xx codec
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/oplus/kernel_4.19/audio/codecs/tfa98xx-v6/Module.symvers)
include $(MY_LOCAL_PATH)/codecs/tfa98xx-v6/Android.mk

#Add for ak43xx codec
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/oplus/kernel_4.19/audio/codecs/ak4376/Module.symvers)
include $(MY_LOCAL_PATH)/codecs/ak4376/Android.mk

#Add for aw87xxx codec
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/oplus/kernel_4.19/audio/codecs/aw87xxx/Module.symvers)
include $(MY_LOCAL_PATH)/codecs/aw87xxx/Android.mk

#Add for sia81xx codec
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/oplus/kernel_4.19/audio/codecs/sia81xx/Module.symvers)
include $(MY_LOCAL_PATH)/codecs/sia81xx/Android.mk

