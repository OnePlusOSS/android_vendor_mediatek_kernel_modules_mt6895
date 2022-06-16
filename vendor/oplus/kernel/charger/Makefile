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
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6779R),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif
ifeq ($(CONFIG_OPLUS_CHARGER_MTK6765R),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif

subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/typec/tcpc/inc
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include


oplus_chg_v2-y += oplus_debug_info.o
oplus_chg_v2-y += oplus_charger.o
oplus_chg_v2-y += oplus_gauge.o
oplus_chg_v2-y += oplus_vooc.o
oplus_chg_v2-y += oplus_pps.o
oplus_chg_v2-y += oplus_adapter.o
oplus_chg_v2-y += oplus_short.o
oplus_chg_v2-y += oplus_wireless.o
oplus_chg_v2-y += oplus_configfs.o
oplus_chg_v2-y += oplus_chg_ops_manager.o
oplus_chg_v2-y += adapter_ic/oplus_stm.o
oplus_chg_v2-y += charger_ic/oplus_short_ic.o
oplus_chg_v2-y += gauge_ic/oplus_bq27541.o
oplus_chg_v2-y += vooc_ic/oplus_stm8s.o
oplus_chg_v2-y += vooc_ic/oplus_n76e.o
oplus_chg_v2-y += vooc_ic/oplus_rk826.o
oplus_chg_v2-y += vooc_ic/oplus_op10.o
oplus_chg_v2-y += vooc_ic/oplus_rt5125.o
oplus_chg_v2-y += vooc_ic/oplus_vooc_fw.o
oplus_chg_v2-y += voocphy/oplus_sc8547.o
oplus_chg_v2-y += voocphy/oplus_sc8547_slave.o
oplus_chg_v2-y += charger_ic/s2asl01_switching.o
oplus_chg_v2-y += charger_ic/oplus_switching.o
oplus_chg_v2-y += charger_ic/oplus_usbtemp.o
ifneq ($(CONFIG_OPLUS_CHARGER_MTK),y)
oplus_chg_v2-y += wireless_ic/oplus_p922x.o
oplus_chg_v2-y += wireless_ic/oplus_nu1619.o
oplus_chg_v2-y += wireless_ic/oplus_chargepump.o
endif

ifeq ($(CONFIG_OPLUS_CHARGER_OPTIGA),y)
oplus_chg_v2-y += gauge_ic/oplus_optiga/oplus_optiga.o \
		gauge_ic/oplus_optiga/ECC/Optiga_Ecc.o \
		gauge_ic/oplus_optiga/ECC/Optiga_Math.o \
		gauge_ic/oplus_optiga/Platform/board.o \
		gauge_ic/oplus_optiga/SWI/Optiga_Auth.o \
		gauge_ic/oplus_optiga/SWI/Optiga_Nvm.o \
		gauge_ic/oplus_optiga/SWI/Optiga_Swi.o
endif

#/*#ifdef OPLUS_FEATURE_CHG_BASIC*/
oplus_chg_v2-y += oplus_pps.o
oplus_chg_v2-y += oplus_pps_ops_manager.o
oplus_chg_v2-y += chargepump_ic/oplus_sc8571_master.o
oplus_chg_v2-y += chargepump_ic/oplus_sc8571_slave.o
oplus_chg_v2-y += chargepump_ic/oplus_bq25980_master.o
oplus_chg_v2-y += chargepump_ic/oplus_bq25980_slave.o
oplus_chg_v2-y += chargepump_ic/oplus_nu2205_master.o
oplus_chg_v2-y += chargepump_ic/oplus_nu2205_slave.o
oplus_chg_v2-y += chargepump_ic/oplus_pps_cp.o
#/*endif*/

oplus_chg_v2-$(CONFIG_OPLUS_CHARGER_MTK6895S)     += charger_ic/oplus_battery_mtk6895S.o charger_ic/oplus_mp2650.o charger_ic/oplus_charge_pump.o voocphy/oplus_adsp_voocphy.o voocphy/oplus_voocphy.o

#obj-$(CONFIG_oplus_chg_v2) := oplus_chg_v2.o
obj-m := oplus_chg_v2.o

#else
#gki_support else
#endif
#gki_support endif

