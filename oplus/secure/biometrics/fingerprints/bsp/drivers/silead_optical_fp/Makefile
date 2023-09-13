#
# Makefile for kernel Silead Fingerprint drivers.
#

ifeq ($(CONFIG_OPLUS_FINGERPRINT_QCOM),y)
obj-y += silead_spi_qcom.o
ccflags-y += -DBSP_SIL_PLAT_QCOM
else ifeq ($(CONFIG_OPLUS_FINGERPRINT_MTK),y)
obj-y += silead_spi_mtk.o
ccflags-y += -DBSP_SIL_PLAT_MTK
ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include \
                    -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc

ifneq ($(filter kernel-4.19, $(LINUX_KERNEL_VERSION)),)
include $(srctree)/drivers/tee/gud/Makefile.include
else ifneq ($(filter kernel-4.14 kernel-4.9, $(LINUX_KERNEL_VERSION)),)
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
endif

endif
endif

