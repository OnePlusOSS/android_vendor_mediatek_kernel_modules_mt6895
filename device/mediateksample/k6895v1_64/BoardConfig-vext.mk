TARGET_BOARD_PLATFORM := mt6895

BOARD_BOOT_HEADER_VERSION := 4
BOARD_KERNEL_PAGESIZE := 4096

# fstab
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := ext4
#ifdef OPLUS_FEATURE_BUILD
#OuZijian@ANDROID.BUILD, 2021/08/06, Add for enabling independent system_ext
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := ext4
#endif
TARGET_COPY_OUT_PRODUCT := product
#ifdef OPLUS_FEATURE_BUILD
#OuZijian@ANDROID.BUILD, 2021/08/06, Add for enabling independent system_ext
# TARGET_COPY_OUT_SYSTEM_EXT := system/system_ext
TARGET_COPY_OUT_SYSTEM_EXT := system_ext
#endif

include device/mediatek/mt6895/BoardConfig-vext.mk

ifneq ($(MTK_K64_SUPPORT), yes)
BOARD_KERNEL_CMDLINE = bootopt=64S3,32S1,32S1
else
BOARD_KERNEL_CMDLINE = bootopt=64S3,32N2,64N2
endif

# ko
include $(VEXT_PROJECT_FOLDER)/gki_ko.mk

# ptgen
MTK_PTGEN_CHIP := $(call to-upper,$(TARGET_BOARD_PLATFORM))
include vendor/mediatek/proprietary/tools/ptgen/common/ptgen.mk
#Config partition size
ifneq ($(CALLED_FROM_SETUP),true)
include $(MTK_PTGEN_OUT)/partition_size.mk
endif

# Assign lk build tool for quark/lk2 build
LK_CLANG_BINDIR := $(PWD)/prebuilts/clang/host/linux-x86/clang-r383902/bin
LK_TOOLCHAIN := ARCH_arm_TOOLCHAIN_PREFIX=$(PWD)/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9.1/bin/arm-linux-androideabi- ARCH_arm64_TOOLCHAIN_PREFIX=$(PWD)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9.1/bin/aarch64-linux-android-

