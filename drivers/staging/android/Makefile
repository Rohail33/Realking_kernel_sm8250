ccflags-y += -I$(src)			# needed for trace events

obj-y					+= ion/

obj-$(CONFIG_ASHMEM)			+= ashmem.o
obj-$(CONFIG_ANDROID_VSOC)		+= vsoc.o
<<<<<<< HEAD
=======
#ifdef OPLUS_FEATURE_HANS_FREEZE
obj-$(CONFIG_OPLUS_HANS) += hans.o
obj-$(CONFIG_OPLUS_HANS) += hans_netfilter.o
#endif /*OPLUS_FEATURE_HANS_FREEZE*/
obj-$(CONFIG_ANDROID_LOW_MEMORY_KILLER)	+= lowmemorykiller.o
>>>>>>> 937ba0eba78c (staging: android: Port lowmemorykiller driver from 4.14)
