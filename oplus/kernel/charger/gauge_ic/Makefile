obj-y	+= oplus_bq27541.o
obj-y	+= oplus_sh366002.o
ifeq ($(CONFIG_OPLUS_SM6375R_CHARGER),y)
	obj-y	+= oplus_sm5602.o
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6877),y)
	obj-y   += oplus_optiga/
endif
#add for 21061 optiga
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6893),y)
	obj-y   += oplus_optiga/
endif
ifeq ($(CONFIG_OPLUS_CHARGER_OPTIGA),y)
	obj-y   += oplus_optiga/
endif
