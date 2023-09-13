gki_support =
ifeq ($(CONFIG_OPLUS_CHG),y)
gki_support = yes
endif
ifeq ($(CONFIG_OPLUS_CHG),m)
gki_support = yes
endif

subdir-ccflags-y += -I$(srctree)/$(src)
subdir-ccflags-y += -Werror=parentheses
subdir-ccflags-y += -DOPLUS_CHG_KO_BUILD
ccflags-$(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER) += -I$(srctree)/drivers/power/oplus/config

ifdef gki_support

ifeq ($(CONFIG_OPLUS_CHARGER_MTK6983S),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6895S),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/usb20/
subdir-ccflags-y += -I$(srctree)/drivers/power/supply
subdir-ccflags-y += -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2
else ifeq ($(CONFIG_OPLUS_CHARGER_MTK6789S),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/usb20/
subdir-ccflags-y += -I$(srctree)/drivers/power/supply
endif

ifeq ($(CONFIG_OPLUS_CHARGER_MTK6985S),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif

oplus_chg-y += oplus_debug_info.o
oplus_chg-y += oplus_charger.o
oplus_chg-y += oplus_gauge.o
oplus_chg-y += oplus_vooc.o
oplus_chg-y += oplus_pps.o
oplus_chg-y += oplus_quirks.o
oplus_chg-y += oplus_adapter.o
oplus_chg-y += oplus_short.o
oplus_chg-y += oplus_wireless.o
oplus_chg-y += oplus_configfs.o
oplus_chg-y += oplus_chg_ops_manager.o
oplus_chg-y += oplus_pps.o
oplus_chg-y += oplus_pps_ops_manager.o
oplus_chg-y += adapter_ic/oplus_stm.o
oplus_chg-y += charger_ic/oplus_short_ic.o
oplus_chg-y += gauge_ic/oplus_bq27541.o
oplus_chg-y += gauge_ic/oplus_sh366002.o
ifeq ($(CONFIG_OPLUS_SM6375R_CHARGER),y)
oplus_chg-y += gauge_ic/oplus_sm5602.o
endif
oplus_chg-y += vooc_ic/oplus_stm8s.o
oplus_chg-y += vooc_ic/oplus_n76e.o
oplus_chg-y += vooc_ic/oplus_rk826.o
oplus_chg-y += vooc_ic/oplus_op10.o
oplus_chg-y += vooc_ic/oplus_rt5125.o
oplus_chg-y += vooc_ic/oplus_vooc_fw.o
oplus_chg-y += voocphy/oplus_sy6529.o
oplus_chg-y += voocphy/oplus_hl7138.o
oplus_chg-y += voocphy/oplus_sc8517.o
oplus_chg-y += voocphy/oplus_sc8547.o
oplus_chg-y += voocphy/oplus_sc8547_slave.o
oplus_chg-y += charger_ic/s2asl01_switching.o
ifeq ($(CONFIG_OPLUS_SM6375R_CHARGER),y)
oplus_chg-y += wireless_ic/oplus_p922x.o
oplus_chg-y += wireless_ic/oplus_ra9530.o
endif
oplus_chg-y += charger_ic/oplus_switching.o
oplus_chg-y += wireless_ic/oplus_nu1619.o
oplus_chg-y += wireless_ic/oplus_chargepump.o
oplus_chg-y += gauge_ic/oplus_optiga/ECC/Optiga_Ecc.o
oplus_chg-y += gauge_ic/oplus_optiga/ECC/Optiga_Math.o
oplus_chg-y += gauge_ic/oplus_optiga/Platform/board.o
oplus_chg-y += gauge_ic/oplus_optiga/SWI/Optiga_Auth.o
oplus_chg-y += gauge_ic/oplus_optiga/SWI/Optiga_Nvm.o
oplus_chg-y += gauge_ic/oplus_optiga/SWI/Optiga_Swi.o
oplus_chg-y += gauge_ic/oplus_optiga/oplus_optiga.o
oplus_chg-y += oplus_chg_core.o
oplus_chg-y += oplus_chg_voter.o
oplus_chg-y += oplus_chg_comm.o
oplus_chg-y += oplus_chg_track.o
oplus_chg-y += ufcs_ic/oplus_sc8547a.o
oplus_chg-y += oplus_ufcs.o
oplus_chg-$(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER) += op_wlchg_v2/oplus_chg_wls_cfg.o

oplus_chg-$(CONFIG_OPLUS_SM8350_CHARGER) += charger_ic/oplus_battery_sm8350.o charger_ic/oplus_da9313.o charger_ic/oplus_mp2650.o voocphy/oplus_adsp_voocphy.o voocphy/oplus_voocphy.o wireless_ic/oplus_p922x.o
oplus_chg-$(CONFIG_OPLUS_SM8450_CHARGER) += charger_ic/oplus_battery_sm8450.o charger_ic/oplus_da9313.o charger_ic/oplus_mp2650.o voocphy/oplus_adsp_voocphy.o voocphy/oplus_voocphy.o wireless_ic/oplus_p922x.o \
		charger_ic/oplus_hl7227.o \
		op_wlchg_v2/wireless_ic/oplus_p9415.o \
		op_wlchg_v2/wireless_ic/oplus_nu1619a.o \
		op_wlchg_v2/hal/oplus_chg_ic.o \
		op_wlchg_v2/hal/wls_chg_fast.o \
		op_wlchg_v2/hal/wls_chg_normal.o \
		op_wlchg_v2/hal/wls_chg_rx.o \
		op_wlchg_v2/oplus_chg_strategy.o \
		op_wlchg_v2/oplus_chg_wls.o \
		chargepump_ic/oplus_pps_cp.o \
		chargepump_ic/oplus_sc8571_master.o \
		chargepump_ic/oplus_sc8571_slave_a.o \
		chargepump_ic/oplus_sc8571_slave_b.o \
		chargepump_ic/oplus_bq25980_master.o \
		chargepump_ic/oplus_bq25980_slave.o \
		chargepump_ic/oplus_nu2205_master.o \
		chargepump_ic/oplus_nu2205_slave.o \
		charger_ic/oplus_mos_ctrl.o
oplus_chg-$(CONFIG_OPLUS_SM8550_CHARGER) += charger_ic/oplus_battery_sm8550.o voocphy/oplus_adsp_voocphy.o voocphy/oplus_voocphy.o \
		charger_ic/oplus_hl7227.o \
		op_wlchg_v2/wireless_ic/oplus_p9415.o \
		op_wlchg_v2/wireless_ic/oplus_nu1619a.o \
		op_wlchg_v2/hal/oplus_chg_ic.o \
		op_wlchg_v2/hal/wls_chg_fast.o \
		op_wlchg_v2/hal/wls_chg_normal.o \
		op_wlchg_v2/hal/wls_chg_rx.o \
		op_wlchg_v2/oplus_chg_strategy.o \
		op_wlchg_v2/oplus_chg_wls.o \
		charger_ic/oplus_mp2650.o \
		chargepump_ic/oplus_pps_cp.o \
		chargepump_ic/oplus_sc8571_master.o \
		chargepump_ic/oplus_sc8571_slave_a.o \
		chargepump_ic/oplus_sc8571_slave_b.o \
		chargepump_ic/oplus_bq25980_master.o \
		chargepump_ic/oplus_bq25980_slave.o \
		chargepump_ic/oplus_nu2205_master.o \
		chargepump_ic/oplus_nu2205_slave.o \
		charger_ic/oplus_mos_ctrl.o
oplus_chg-$(CONFIG_OPLUS_SM6375R_CHARGER) += \
		voocphy/oplus_voocphy.o \
		charger_ic/oplus_sy6974b.o \
		charger_ic/oplus_sgm7220.o \
		charger_ic/oplus_sy6970.o \
		charger_ic/oplus_sgm41511.o \
		charger_ic/oplus_sgm41512.o \
		charger_ic/oplus_discrete_charger.o \
		charger_ic/oplus_mp2650.o \
		charger_ic/rt_pd_manager.o \
		charger_ic/oplus_usbtemp.o
oplus_chg-$(CONFIG_OPLUS_CHARGER_MTK6983S) += \
		charger_ic/oplus_mp2650.o \
		charger_ic/oplus_usbtemp.o \
		charger_ic/oplus_charge_pump.o \
		charger_ic/oplus_hl7227.o \
		op_wlchg_v2/wireless_ic/oplus_nu1619a.o \
		op_wlchg_v2/hal/oplus_chg_ic.o \
		op_wlchg_v2/hal/wls_chg_fast.o \
		op_wlchg_v2/hal/wls_chg_normal.o \
		op_wlchg_v2/hal/wls_chg_rx.o \
		op_wlchg_v2/oplus_chg_strategy.o \
		op_wlchg_v2/oplus_chg_wls.o \
		voocphy/oplus_voocphy.o \
		charger_ic/oplus_battery_mtk6983S.o \
		charger_ic/oplus_mos_ctrl.o \
		wireless_ic/oplus_p9418.o
oplus_chg-$(CONFIG_OPLUS_CHARGER_MTK6895S) += \
		charger_ic/oplus_usbtemp.o \
		charger_ic/oplus_mp2650.o \
		charger_ic/oplus_charge_pump.o \
		chargepump_ic/oplus_pps_cp.o \
		chargepump_ic/oplus_sc8571_master.o \
		chargepump_ic/oplus_sc8571_slave_a.o \
		chargepump_ic/oplus_sc8571_slave_b.o \
		chargepump_ic/oplus_bq25980_master.o \
		chargepump_ic/oplus_bq25980_slave.o \
		chargepump_ic/oplus_nu2205_master.o \
		chargepump_ic/oplus_nu2205_slave.o \
		charger_ic/oplus_hl7227.o \
		op_wlchg_v2/wireless_ic/oplus_nu1619a.o \
		op_wlchg_v2/hal/oplus_chg_ic.o \
		op_wlchg_v2/hal/wls_chg_fast.o \
		op_wlchg_v2/hal/wls_chg_normal.o \
		op_wlchg_v2/hal/wls_chg_rx.o \
		op_wlchg_v2/oplus_chg_strategy.o \
		op_wlchg_v2/oplus_chg_wls.o \
		voocphy/oplus_voocphy.o \
		charger_ic/oplus_battery_mtk6895S.o
oplus_chg-$(CONFIG_OPLUS_CHARGER_MTK6789S) += \
		charger_ic/oplus_usbtemp.o \
		op_wlchg_v2/hal/oplus_chg_ic.o \
		op_wlchg_v2/hal/wls_chg_fast.o \
		op_wlchg_v2/hal/wls_chg_normal.o \
		op_wlchg_v2/hal/wls_chg_rx.o \
		op_wlchg_v2/oplus_chg_strategy.o \
		op_wlchg_v2/oplus_chg_wls.o \
		voocphy/oplus_voocphy.o \
		charger_ic/oplus_battery_mtk6789S.o \
		charger_ic/oplus_bq2589x_gki.o
oplus_chg-$(CONFIG_OPLUS_CHARGER_MTK6985S) += \
		charger_ic/oplus_mp2650.o \
		charger_ic/oplus_usbtemp.o \
		charger_ic/oplus_charge_pump.o \
		charger_ic/oplus_hl7227.o \
		op_wlchg_v2/wireless_ic/oplus_nu1619a.o \
		op_wlchg_v2/hal/oplus_chg_ic.o \
		op_wlchg_v2/hal/wls_chg_fast.o \
		op_wlchg_v2/hal/wls_chg_normal.o \
		op_wlchg_v2/hal/wls_chg_rx.o \
		op_wlchg_v2/oplus_chg_strategy.o \
		op_wlchg_v2/oplus_chg_wls.o \
		voocphy/oplus_voocphy.o \
		charger_ic/oplus_battery_mtk6985S.o

ccflags-y += -DOPLUS_CHG_REG_DUMP_ENABLE

KBUILD_LDS_MODULE_ATTACH = oplus_chg_module.lds

obj-$(CONFIG_OPLUS_CHG) := oplus_chg.o

else # gki_support

ifeq ($(CONFIG_OPLUS_CHARGER_MTK),y)
subdir-ccflags-y += -Werror
subdir-ccflags-y += -Wno-unused-variable
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6779Q),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6889),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6893),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6877),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -Werror
subdir-ccflags-y += -Wunused
subdir-ccflags-y += -Wno-unused-result
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6779R),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6765R),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6765S),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
ifneq ($(CONFIG_OPLUS_CHARGER_MTK),y)
ifeq ($(BRAND_SHOW_FLAG),oneplus)
obj-y      += op_wlchg/
else
obj-y      += wireless_ic/
endif
endif

obj-y      += charger_ic/
obj-y      += gauge_ic/
obj-y      += vooc_ic/
obj-y      += adapter_ic/
obj-y	   += chargepump_ic/
obj-y	   += oplus_pps_ops_manager.o

obj-y      += voocphy/

#obj-y   += oplus_voocphy.o

obj-y	+= oplus_charger.o
obj-y	+= oplus_gauge.o
obj-y	+= oplus_vooc.o
obj-y	+= oplus_adapter.o
obj-y 	+= oplus_chg_track.o
obj-y 	+= oplus_quirks.o

ifneq ($(CONFIG_OPLUS_CHARGER_MTK),y)
ifeq ($(BRAND_SHOW_FLAG),oneplus)
obj-y	+= oplus_wlchg_policy.o
else
obj-y	+= oplus_wireless.o
endif # BRAND_SHOW_FLAG
else
obj-y	+= oplus_wireless.o
endif # CONFIG_OPLUS_CHARGER_MTK

obj-y	+= oplus_short.o
obj-y   += oplus_debug_info.o
obj-y   += oplus_chg_ops_manager.o
obj-y	+= oplus_pps.o
obj-y   += oplus_configfs.o
obj-y   += charger_ic/s2asl01_switching.o
obj-y   += charger_ic/oplus_switching.o
obj-y   += chargepump_ic/
obj-y	+= oplus_pps_ops_manager.o
obj-y 	+= oplus_quirks.o

obj-y += oplus_chg_voter.o
obj-y += oplus_chg_core.o
obj-y += oplus_chg_comm.o

obj-y += ufcs_ic/
obj-y += oplus_ufcs.o
endif # gki_support
