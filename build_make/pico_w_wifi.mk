################################################################################
# micro T-Kernel 3.0 BSP makefile
#     Target Board: Raspberry Pi Pico W (RP2040 + CYW43439)
#
# WiFi + MCP ビルド用。BLE=0 で WiFi-only モード (MCP テスト用)。
#
# Usage:
#   make TARGET=_PICO_W_ WIFI=1 all           # WiFi + BLE + keyboard
#   make TARGET=_PICO_W_ WIFI=1 BLE=0 all     # WiFi-only (MCP テスト)
################################################################################

GCC := arm-none-eabi-gcc
AS := arm-none-eabi-gcc
LINK := arm-none-eabi-gcc

# BLE デフォルト有効 (BLE=0 で無効化)
BLE ?= 1

CFLAGS := -mcpu=cortex-m0plus -mthumb -ffreestanding\
    -std=gnu11 \
    -O0 -g3 -Wall -Wno-array-bounds \
    -MMD -MP \
    -mfloat-abi=soft \
    -DWIFI_CYW43 \
    -DUSE_WASM \
    $(EXTRA_DEFS) \

ifeq ($(BLE),1)
CFLAGS += -DKB_OUTPUT_BLE
endif

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

# CYW43 / lwIP インクルードパス (常に必要)
INCPATH += \
    -I"../lib/cyw43-driver/src" \
    -I"../lib/cyw43-driver/firmware" \
    -I"../device/ble_hid" \
    -I"../device/ble_hid/sysdepend/rp2040" \
    -I"../lib/btstack/3rd-party/lwip/core/src/include" \
    -I"../device/wifi" \

ifeq ($(BLE),1)
# BTstack ヘッダ (BLE 有効時のみ)
INCPATH += \
    -I"../lib/btstack/src" \
    -I"../lib/btstack/src/ble" \
    -I"../lib/btstack/src/ble/gatt-service" \
    -I"../lib/btstack/platform/embedded" \

endif

# ---- Board/CPU/Core dependent kernel sources ----
include mtkernel_3/lib/libtm/sysdepend/pico_rp2040/subdir.mk
include mtkernel_3/lib/libtk/sysdepend/cpu/rp2040/subdir.mk
include mtkernel_3/lib/libtk/sysdepend/cpu/core/armv6m/subdir.mk
include mtkernel_3/lib/libbsp/sysdepend/cpu/rp2040/subdir.mk
include mtkernel_3/kernel/sysdepend/pico_rp2040/subdir.mk
include mtkernel_3/kernel/sysdepend/cpu/rp2040/subdir.mk
include mtkernel_3/kernel/sysdepend/cpu/core/armv6m/subdir.mk

# ---- Device drivers (RP2040 sysdepend) ----
include mtkernel_3/device/i2c/sysdepend/rp2040/subdir.mk
include mtkernel_3/device/ser/sysdepend/rp2040/subdir.mk

# ---- USB Device Driver ----
include mtkernel_3/device/usb_hid/subdir.mk

# ---- lwIP TCP/IP Stack ----
include mtkernel_3/lib/lwip/subdir.mk

# ---- WiFi Device Driver ----
include mtkernel_3/device/wifi/subdir.mk

# ---- MQTT/MCP over WiFi (WizNet 互換 API) ----
include mtkernel_3/device/wiznet/subdir_wifi.mk

# ---- cJSON Library ----
include mtkernel_3/lib/cjson/subdir.mk

# ---- wasm3 WebAssembly Runtime ----
include mtkernel_3/lib/wasm3/subdir.mk

# ---- Standard Extension ----
include mtkernel_3/kernel/extension/datetime/subdir.mk

ifeq ($(BLE),1)
# ---- BLE HID (CYW43439 + BTstack) ----
include mtkernel_3/device/ble_hid/subdir.mk
include mtkernel_3/lib/btstack/subdir.mk

# ---- Keyboard framework ----
include mtkernel_3/app_program/keyboard/subdir.mk
else
# ---- CYW43 HAL only (WiFi-only, no BLE/BTstack) ----
include mtkernel_3/device/ble_hid/subdir_cyw43_hal.mk
endif

# ---- WiFi App (app_wifi_program/app_main.c) ----
include mtkernel_3/app_wifi_program/subdir.mk
