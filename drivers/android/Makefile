ccflags-y += -mllvm -inline-threshold=15000
ccflags-y += -mllvm -inlinehint-threshold=10000
ccflags-y += -I$(src)			# needed for trace events

obj-$(CONFIG_ANDROID_BINDERFS)		+= binderfs.o
obj-$(CONFIG_ANDROID_BINDER_IPC)	+= binder.o binder_alloc.o
obj-$(CONFIG_ANDROID_BINDER_IPC_SELFTEST) += binder_alloc_selftest.o
obj-y += hwui_mon.o
obj-y += jank_detect_demo.o
obj-$(CONFIG_ANDROID_SIMPLE_LMK)	+= simple_lmk.o
