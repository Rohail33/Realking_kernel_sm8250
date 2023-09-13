#
# Makefile for kernel Egis Fingerprint drivers.
#
ifeq ($(CONFIG_OPLUS_FINGERPRINT_QCOM),y)
obj-y += egis_qcom.o
obj-y +=ets_navi_input_qcom.o
else ifeq ($(CONFIG_OPLUS_FINGERPRINT_MTK),y)
# Each configuration option enables a list of files.
#include $(srctree)/drivers/misc/mediatek/Makefile.custom

obj-y	+= egis.o
obj-y +=ets_navi_input.o
endif