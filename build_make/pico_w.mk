################################################################################
# micro T-Kernel 3.0 BSP makefile
#     Target Board: Raspberry Pi Pico W (RP2040 + CYW43439)
#
# BLE HID キーボードビルド用。
# pico_rp2040.mk をベースに BLE/CYW43/BTstack を追加。
# WiFi は W5100S 経由で独自実装するため、CYW43 は BT のみ使用。
################################################################################

GCC := arm-none-eabi-gcc
AS := arm-none-eabi-gcc
LINK := arm-none-eabi-gcc

CFLAGS := -mcpu=cortex-m0plus -mthumb -ffreestanding\
    -std=gnu11 \
    -O0 -g3 -Wall -Wno-array-bounds \
    -MMD -MP \
    -mfloat-abi=soft \
    -DKB_OUTPUT_BLE \
    $(EXTRA_DEFS) \

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

# BTstack / CYW43 インクルードパス
INCPATH += \
    -I"../lib/btstack/src" \
    -I"../lib/btstack/src/ble" \
    -I"../lib/btstack/src/ble/gatt-service" \
    -I"../lib/btstack/platform/embedded" \
    -I"../lib/cyw43-driver/src" \
    -I"../lib/cyw43-driver/firmware" \
    -I"../device/ble_hid" \
    -I"../device/ble_hid/sysdepend/rp2040" \
    -I"../lib/btstack/3rd-party/lwip/core/src/include" \
    -I"../lib/btstack/3rd-party/micro-ecc" \
    -I"../lib/btstack/3rd-party/rijndael" \
    -I"../device/wifi" \

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
# I2C slave は BLE split では不要 (Pico W slave は BLE Matrix Service を使う)
# I2C split (SPLIT_TRANSPORT_AUTO) 時の slave は _PICO_RP2040_ でビルドする
# include mtkernel_3/device/i2c_slave/subdir.mk

# ---- USB Device Driver (USB/BLE 自動切替: USB も含む) ----
include mtkernel_3/device/usb_hid/subdir.mk

# ---- WiFi ネットワーク (WIFI_CYW43 定義時のみ) ----
# make TARGET=_PICO_W_ EXTRA_DEFS="-DWIFI_CYW43" で有効化
ifneq (,$(findstring WIFI_CYW43,$(EXTRA_DEFS)))
include mtkernel_3/lib/lwip/subdir.mk
include mtkernel_3/device/wifi/subdir.mk
endif

# ---- cJSON Library ----
include mtkernel_3/lib/cjson/subdir.mk

# ---- Standard Extension ----
include mtkernel_3/kernel/extension/datetime/subdir.mk

# ---- BLE HID (CYW43439 + BTstack) ----
include mtkernel_3/device/ble_hid/subdir.mk
include mtkernel_3/lib/btstack/subdir.mk

# ---- Keyboard framework ----
include mtkernel_3/app_program/keyboard/subdir.mk

# ---- Application entry point (usermain) ----
ifneq (,$(findstring WIFI_CYW43,$(EXTRA_DEFS)))
  # WiFi + BLE キーボード
  include mtkernel_3/app_wifi_program/subdir.mk
else
  # BLE キーボード専用 (ネットワークなし)
  include mtkernel_3/app_kb_program/subdir.mk
endif
