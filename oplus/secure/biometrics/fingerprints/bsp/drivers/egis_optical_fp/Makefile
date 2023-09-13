#
# Makefile for kernel Egis Fingerprint drivers.
#

ifeq ($(CONFIG_OPLUS_FINGERPRINT_QCOM),y)
obj-y    += et713_platform.o et713_netlink.o
else ifeq ($(CONFIG_OPLUS_FINGERPRINT_MTK),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
include $(srctree)/drivers/tee/gud/Makefile.include

ccflags-y += -DCONFIG_MTK_DRIVER
obj-y    += et713_mtk.o et713_netlink.o
endif
