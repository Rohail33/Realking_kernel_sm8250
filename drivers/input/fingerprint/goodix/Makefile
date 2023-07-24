ifdef CONFIG_INLINE_OPTIMIZATION
ccflags-y += -mllvm -inline-threshold=15000
ccflags-y += -mllvm -inlinehint-threshold=10000
endif

obj-$(CONFIG_FINGERPRINT_GOODIX) += gf_spi.o platform.o netlink.o
