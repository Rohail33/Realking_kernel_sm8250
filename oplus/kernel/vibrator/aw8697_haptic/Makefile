#for AWINIC AW8697 Haptic
#ifdef OPLUS_FEATURE_CHG_BASIC
ifeq ($(strip $(BRAND_SHOW_FLAG)), oneplus)
	ccflags-y += -DCONFIG_OPLUS_HAPTIC_OOS
else ifeq ($(strip $(CONFIG_OPLUS_CHG_OOS)), y)
	ccflags-y += -DCONFIG_OPLUS_HAPTIC_OOS
endif
#endif
obj-$(CONFIG_AW8697_HAPTIC) += aw8697.o

obj-$(CONFIG_AW8697_HAPTIC) += haptic.o
haptic-objs := aw8692x.o haptic_hv.o
