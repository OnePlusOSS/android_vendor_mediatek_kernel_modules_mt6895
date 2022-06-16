# DO NOT modify this file

ifdef VEXT_TARGET_PROJECT
$(call inherit-product, $(VEXT_TARGET_PROJECT_FOLDER)/device-vext.mk)

ifdef HAL_TARGET_PROJECT
PRODUCT_PACKAGES += $(MTK_OUT_OF_TREE_KERNEL_MODULES)
else
PRODUCT_HOST_PACKAGES :=
endif#HAL_TARGET_PROJECT

endif#VEXT_TARGET_PROJECT

ifdef KRN_TARGET_PROJECT
$(call inherit-product, $(KRN_TARGET_PROJECT_FOLDER)/device-kernel.mk)
endif#KRN_TARGET_PROJECT

# DO NOT modify this file
