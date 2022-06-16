# Precautions as below
# 1. According to ko_order_table.csv, gki_ko.mk will dynamically append ko into BOARD_*_KERNEL_MODULES
# 2. Please modify ko_order_table.csv to update ko, instead of gki_ko.mk

ifeq ($(strip $(KERNEL_OUT)),)
$(error KERNEL_OUT should not be empty!)
endif

ifneq ($(wildcard $(VEXT_TARGET_PROJECT_FOLDER)/ko_order_table.csv),)
ko_order_table := $(VEXT_TARGET_PROJECT_FOLDER)/ko_order_table.csv
else
ko_order_table := $(VEXT_PROJECT_FOLDER)/ko_order_table.csv
endif
ko_order_table_contents := $(shell cat $(ko_order_table) | cut -d , -f 1-6 | sed 's/ //g')
ko_comma := ,
$(foreach ko,$(ko_order_table_contents), \
  $(eval ko_line := $(subst $(ko_comma), ,$(ko))) \
  $(eval ko_name := $(word 1,$(ko_line))) \
  $(eval ko_path := $(word 2,$(ko_line))) \
  $(eval ko_partition := $(word 3,$(ko_line))) \
  $(eval ko_loaded := $(word 4,$(ko_line))) \
  $(eval ko_recovery := $(word 5,$(ko_line))) \
  $(eval ko_mode := $(subst /, ,$(word 6,$(ko_line)))) \
  $(if $(filter $(TARGET_BUILD_VARIANT),$(ko_mode)),\
    $(if $(filter vendor,$(ko_partition)),\
      $(eval BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_OUT)$(ko_path))\
      $(if $(filter Y,$(ko_loaded)),\
        $(eval BOARD_VENDOR_KERNEL_MODULES_LOAD += $(KERNEL_OUT)$(ko_path))))\
    $(if $(filter ramdisk,$(ko_partition)),\
      $(eval BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(KERNEL_OUT)$(ko_path))\
      $(if $(filter Y,$(ko_loaded)),\
        $(eval BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD += $(KERNEL_OUT)$(ko_path))))\
    $(if $(filter Y,$(ko_recovery)),\
      $(eval BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD += $(KERNEL_OUT)$(ko_path)))\
  ) \
)

BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD)

ifeq ($(wildcard vendor/mediatek/internal),)
BOARD_VENDOR_KERNEL_MODULES := $(subst $(KERNEL_OUT)/../vendor/mediatek/kernel_modules/met_drv_secure_v3/met_plf.ko,,$(BOARD_VENDOR_KERNEL_MODULES))
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(subst $(KERNEL_OUT)/../vendor/mediatek/kernel_modules/met_drv_secure_v3/met_plf.ko,,$(BOARD_VENDOR_KERNEL_MODULES_LOAD))
endif
