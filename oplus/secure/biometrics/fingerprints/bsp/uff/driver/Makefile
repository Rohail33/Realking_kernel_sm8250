ccflags-$(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE) += -DCONFIG_OPLUS_FINGERPRINT_GKI_ENABLE

ifeq ($(CONFIG_UFF_FINGERPRINT_QCOM),y)
ccflags-y += -DQCOM_PLATFORM

ifeq ($(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE),y)
#tp for gki
ccflags-y += -I$(srctree)/../../vendor/oplus/kernel/touchpanel/oplus_touchscreen_v2/touchpanel_notify
endif

obj-$(CONFIG_UFF_FINGERPRINT)  += uff_fp_driver.o
uff_fp_driver-y := fp_driver.o fp_platform.o fingerprint_event.o fp_health.o
endif

ifeq ($(CONFIG_UFF_FINGERPRINT_MTK),y)
ccflags-y += -DMTK_PLATFORM
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc

ifneq ($(filter kernel-4.19, $(LINUX_KERNEL_VERSION)),)
include $(srctree)/drivers/tee/gud/Makefile.include
else ifneq ($(filter kernel-4.14 kernel-4.9, $(LINUX_KERNEL_VERSION)),)
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
endif

#lcd uiready for gki
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2/
ccflags-$(CONFIG_DRM_MEDIATEK_V2) += -DCONFIG_DRM_MEDIATEK_V2

# wl2868c supply for gki
ccflags-$(CONFIG_FP_SUPPLY_MODE_LDO) += -I$(srctree)/drivers/misc/mediatek/wl2868c
ccflags-$(CONFIG_FP_SUPPLY_MODE_LDO) += -DCONFIG_FP_SUPPLY_MODE_LDO

ifeq ($(CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE),y)
#tp for gki
ccflags-y += -I$(srctree)/../vendor/oplus/kernel/touchpanel/oplus_touchscreen_v2/touchpanel_notify
endif

obj-$(CONFIG_UFF_FINGERPRINT) += uff_fp_driver.o
uff_fp_driver-y := fp_driver.o fp_platform.o fingerprint_event.o fp_health.o
endif

obj-$(CONFIG_UFF_FINGERPRINT_REE_SPI) += ree_spi/
