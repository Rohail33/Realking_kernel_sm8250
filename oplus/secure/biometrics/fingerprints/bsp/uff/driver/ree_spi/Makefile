#
# Makefile for kernel Fingerprint drivers.
#
obj-$(CONFIG_UFF_FINGERPRINT_REE_SPI) += fingerprint_spi.o
ifeq ($(CONFIG_UFF_FINGERPRINT_REE_SPI), y)
fingerprint_spi-y := fingerprint_ree_spi.o fingerprint_ree_dts.o ../fingerprint_event.o
else
fingerprint_spi-objs := fingerprint_ree_spi.o fingerprint_ree_dts.o ../fingerprint_event.o
endif
