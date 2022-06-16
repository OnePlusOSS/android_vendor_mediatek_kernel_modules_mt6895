PRODUCT_PROPERTY_OVERRIDES :=
PRODUCT_COPY_FILES :=
MTK_OUT_OF_TREE_KERNEL_MODULES :=

# device/mediatekprojects/k6895v1_64_k54/device.mk
PRODUCT_COPY_FILES += $(LOCAL_PATH)/init.mtkgki.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.mtkgki.rc
PRODUCT_COPY_FILES += $(LOCAL_PATH)/factory_init.project.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/factory_init.project.rc
PRODUCT_COPY_FILES += $(LOCAL_PATH)/init.project.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.project.rc
PRODUCT_COPY_FILES += $(LOCAL_PATH)/meta_init.project.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/meta_init.project.rc
PRODUCT_COPY_FILES += $(LOCAL_PATH)/init.insmod.mt6895.cfg:$(TARGET_COPY_OUT_VENDOR)/etc/init.insmod.mt6895.cfg

ifneq ($(MTK_AUDIO_TUNING_TOOL_VERSION),)
  ifneq ($(strip $(MTK_AUDIO_TUNING_TOOL_VERSION)),V1)
    MTK_AUDIO_PARAM_DIR_LIST += $(MTK_TARGET_PROJECT_FOLDER)/audio_param
  endif
endif

#
$(call inherit-product, device/mediatek/mt6895/device-vext.mk)
TRUSTONIC_TEE_VERSION = 510