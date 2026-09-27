################################################################################
# micro T-Kernel 3.0 BSP makefile
#     Target Board: Raspberry Pi Pico (RP2040)
################################################################################

GCC := arm-none-eabi-gcc
AS := arm-none-eabi-gcc
LINK := arm-none-eabi-gcc

CFLAGS := -mcpu=cortex-m0plus -mthumb -ffreestanding\
    -std=gnu11 \
    -O0 -g3 -Wall -Wno-array-bounds \
    -MMD -MP \
    -mfloat-abi=soft \
    $(EXTRA_DEFS) \
    $(FEATURE_DEFS) \

ASFLAGS := -mcpu=cortex-m0plus -mthumb -ffreestanding\
    -x assembler-with-cpp \
    -O0 -g3 \
    -MMD -MP \

LFLAGS := -mcpu=cortex-m0plus -mthumb -ffreestanding \
    -nostartfiles \
    --specs=nosys.specs \
    -O0 -g3 \
    -mfloat-abi=soft \

LNKFILE := "../etc/linker/pico_rp2040/tkernel_map.ld"

# ---- Board/CPU/Core dependent kernel sources ----
include mtkernel_3/lib/libtm/sysdepend/pico_rp2040/subdir.mk
include mtkernel_3/lib/libtk/sysdepend/cpu/rp2040/subdir.mk
include mtkernel_3/lib/libtk/sysdepend/cpu/core/armv6m/subdir.mk
include mtkernel_3/lib/libbsp/sysdepend/cpu/rp2040/subdir.mk
include mtkernel_3/kernel/sysdepend/pico_rp2040/subdir.mk
include mtkernel_3/kernel/sysdepend/cpu/rp2040/subdir.mk
include mtkernel_3/kernel/sysdepend/cpu/core/armv6m/subdir.mk

# ---- Device drivers (RP2040 sysdepend) ----
#include mtkernel_3/device/adc/sysdepend/rp2040/subdir.mk
include mtkernel_3/device/i2c/sysdepend/rp2040/subdir.mk
include mtkernel_3/device/ser/sysdepend/rp2040/subdir.mk
include mtkernel_3/device/i2c_slave/subdir.mk

# ---- USB Device Driver ----
include mtkernel_3/device/usb_hid/subdir.mk

# ---- WIZnet Ethernet Driver (W5100S) ----
# ---- ネットワーク (NET=1 のときのみ) ----
ifeq ($(NET),1)
include mtkernel_3/device/wiznet/subdir.mk

# ---- wasm3 WebAssembly Runtime (WASM=1 のときのみ) ----
ifeq ($(WASM),1)
include mtkernel_3/lib/wasm3/subdir.mk
endif

# ---- cJSON Library ----
include mtkernel_3/lib/cjson/subdir.mk
endif

# ---- Standard Extension ----
include mtkernel_3/kernel/extension/datetime/subdir.mk

# ---- アプリ ----
#   APP=presence  Pico4ML の離席判定 (カメラ + TensorFlow Lite Micro)
#   (指定なし)    キーボード
#   PRESENCE=1    キーボードに離席判定を同居させる (Win+L はキーボードのタスクが送る)
ifeq ($(APP),presence)
include mtkernel_3/app_presence/subdir.mk
else
include mtkernel_3/app_program/keyboard/subdir.mk
ifeq ($(PRESENCE),1)
PR_WITH_KEYBOARD := -DPRESENCE_WITH_KEYBOARD
include mtkernel_3/app_presence/subdir.mk
endif
endif
