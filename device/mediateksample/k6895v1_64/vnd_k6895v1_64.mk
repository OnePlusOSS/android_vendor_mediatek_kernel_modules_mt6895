KRN_TARGET_PROJECT := mgk_64_k510
KRN_TARGET_PROJECT_FOLDER := device/mediatek/kernel/$(KRN_TARGET_PROJECT)
HAL_TARGET_PROJECT := mgvi_64_armv82
HAL_TARGET_PROJECT_FOLDER := device/mediatek/vendor/$(HAL_TARGET_PROJECT)
VEXT_TARGET_PROJECT := k6895v1_64
#ifdef OPLUS_FEATURE_SECURITY_COMMON
#Meilin.Zhou@BSP.Security.Basic, 2021/11/22, add for images sign arguments
MTK_BASE_PROJECT := k6895v1_64
#endif /* OPLUS_FEATURE_SECURITY_COMMON */
VEXT_TARGET_PROJECT_FOLDER := device/mediateksample/$(VEXT_TARGET_PROJECT)

#ifdef OPLUS_FEATURE_SECURITY_COMMON
#Meilin.Zhou@BSP.Security.Basic, 2021/11/22, add for images sign arguments
MTK_BASE_PROJECT := k6895v1_64
#endif /* OPLUS_FEATURE_SECURITY_COMMON */

include $(KRN_TARGET_PROJECT_FOLDER)/krn_$(KRN_TARGET_PROJECT).mk
include $(HAL_TARGET_PROJECT_FOLDER)/hal_$(HAL_TARGET_PROJECT).mk
include $(VEXT_TARGET_PROJECT_FOLDER)/vext_$(VEXT_TARGET_PROJECT).mk

PRODUCT_NAME := vnd_k6895v1_64
