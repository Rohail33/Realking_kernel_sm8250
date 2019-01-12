#
# Makefile for the kernel mmc device drivers.
#

obj-$(CONFIG_MMC)		+= core/
obj-$(subst m,y,$(CONFIG_MMC))	+= host/

obj-$(CONFIG_BOGUS_MMC)		+= bogus_mmc.o
