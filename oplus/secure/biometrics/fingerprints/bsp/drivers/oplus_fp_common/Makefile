obj-y     +=  oplus_fp_common.o fingerprint_event.o
ccflags-$(CONFIG_OPLUS_FINGERPRINT_GKI_DISABLE) += -DFINGERPRINT_GKI_DISABLE
### Platform marco
ifeq ($(CONFIG_OPLUS_FINGERPRINT_QCOM),y)
ccflags-y += -DQCOM_PLATFORM
else ifeq ($(CONFIG_OPLUS_FINGERPRINT_MTK),y)
ccflags-y += -DMTK_PLATFORM
ifeq ($(CONFIG_OPLUS_FINGERPRINT_ENABLE_TEE_BINDERCORE),y)
ccflags-y += -DFP_TEE_BINDE_CORE_ENABLE
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
endif
endif
