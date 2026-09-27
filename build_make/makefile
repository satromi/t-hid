################################################################################
# micro T-Kernel 3.0 BSP  makefile
################################################################################

# Specify the name of the executable file to be built (Do not include the extension)
EXE_FILE := mtkernel_3

# Specify only one target name
#TARGET := _IOTE_M367_
#TARGET := _IOTE_RX231_
#TARGET := _IOTE_STM32L4_
#TARGET := _IOTE_RZA2M_
#TARGET := _NUCLEO_H723_
TARGET := _PICO_RP2040_

RM := rm -rf

# Workaround: GCC needs writable TEMP directory
export TEMP ?= /tmp
export TMP  ?= /tmp

INCPATH = -I"../include" -I"../config" -I"../kernel/knlinc" -I"../device/include"

-include sources.mk

include mtkernel_3/lib/libtm/subdir.mk
include mtkernel_3/lib/libtk/subdir.mk
include mtkernel_3/kernel/usermain/subdir.mk
include mtkernel_3/kernel/tstdlib/subdir.mk
include mtkernel_3/kernel/tkernel/subdir.mk
include mtkernel_3/kernel/sysinit/subdir.mk
include mtkernel_3/kernel/inittask/subdir.mk

include mtkernel_3/device/subdir.mk


# ======================================================================
# オプション機能の選択 (RP2040)
#
#   NET=1   W5100S Ethernet とネットワークスタックを載せる
#   MCP=1   MCP サーバ (MQTT/JSON-RPC) を載せる。NET=1 を含む
#   WASM=1  WebAssembly ランタイムと wasm 系 MCP ツールを載せる
#           wasm ツールは MCP 経由で呼ぶため WASM=1 は MCP=1 を含む
#
# 既定はすべて無効。キーボード単体のイメージが最小になる。
#
# 注意: W5100S-EVB-Pico は SPI/CS/RST/INT に GP16-21 を使う。TL Split の
# マトリクスと 6 本重なるため、キーボード基板では NET=1 にしないこと。
# ======================================================================
NET  ?= 0
MCP  ?= 0
WASM ?= 0
ifeq ($(WASM),1)
override MCP := 1
endif
ifeq ($(MCP),1)
override NET := 1
endif

FEATURE_DEFS :=
ifeq ($(PRESENCE),1)
FEATURE_DEFS += -DUSE_PRESENCE
endif
ifeq ($(NET),1)
FEATURE_DEFS += -DUSE_NET
endif
ifeq ($(MCP),1)
FEATURE_DEFS += -DUSE_MCP
endif
ifeq ($(WASM),1)
FEATURE_DEFS += -DUSE_WASM
endif

# GAMEPAD=1: USB をキーボード + ゲームパッドの複合デバイスにする
#   キーマップの JS_* キーがゲームパッドのボタン・十字キー・スティックになる
#   (docs/guide/gamepad.md)。フラグを変えたら make clean してからビルドする
GAMEPAD ?= 0
ifeq ($(GAMEPAD),1)
FEATURE_DEFS += -DUSB_HID_GAMEPAD
endif

ifeq ($(TARGET), _IOTE_M367_)
include iote_m367.mk
endif
ifeq ($(TARGET), _IOTE_RX231_)
include iote_rx231.mk
endif
ifeq ($(TARGET), _IOTE_STM32L4_)
include iote_stm32l4.mk
endif
ifeq ($(TARGET), _IOTE_RZA2M_)
include iote_rza2m.mk
endif
ifeq ($(TARGET), _NUCLEO_H723_)
include nucleo_h723.mk
endif
ifeq ($(TARGET), _PICO_RP2040_)
include pico_rp2040.mk
endif
ifeq ($(TARGET), _PICO_W_)
ifeq ($(WIFI),1)
include pico_w_wifi.mk
else
include pico_w.mk
endif
endif

# app_program/subdir.mk は W5100S ネットワークテスト用 (app_main.c のワイルドカード)
# Pico W では pico_w.mk / pico_w_wifi.mk 内で app を選択するため除外
ifneq ($(TARGET), _PICO_W_)
ifneq ($(APP),presence)
-include mtkernel_3/app_program/subdir.mk
endif
endif

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

# Build target: RP2040/Pico W generates .elf + .uf2, others generate .elf only
ifneq (,$(filter $(TARGET), _PICO_RP2040_ _PICO_W_))
all: $(EXE_FILE).elf $(EXE_FILE).uf2
else
all: $(EXE_FILE).elf
	@echo ' '
endif

$(EXE_FILE).elf: $(OBJS)
	@echo 'Linker: $@'
	$(LINK)  $(LFLAGS) -T $(LNKFILE) -Wl,-Map,"$(EXE_FILE).map" -o "$(EXE_FILE).elf" $(OBJS) $(EXTRA_LIBS) -lm
	@echo 'Finished building target: $@'
	@echo ' '

# Generate .uf2 for RP2040 BOOTSEL flashing (RP2040 only)
ELF2UF2 ?= python elf2uf2.py

$(EXE_FILE).uf2: $(EXE_FILE).elf
	$(ELF2UF2) $(EXE_FILE).elf $(EXE_FILE).uf2
	@echo 'Generated: $(EXE_FILE).uf2 (copy to RP2040 in BOOTSEL mode)'
	@echo ' '

# Other EXE_FILEs
clean:
	-$(RM) $(OBJS) $(SECONDARY_SIZE) $(ASM_DEPS) $(S_UPPER_DEPS) $(C_DEPS) $(EXE_FILE).elf $(EXE_FILE).bin $(EXE_FILE).uf2 $(EXE_FILE).map
	-@echo ' '

# Keymap name for output filenames (default: "default")
KEYMAP ?= default

# Build both master and slave firmware
split: split_master split_slave
	@echo ' '
	@echo '=== Both firmwares built (keymap=$(KEYMAP)) ==='
	@echo '  Master: $(EXE_FILE)_master_$(KEYMAP).uf2'
	@echo '  Slave:  $(EXE_FILE)_slave_$(KEYMAP).uf2'
	@echo ' '

split_master:
	@echo '=== Building MASTER firmware (keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_RP2040_ clean
	$(MAKE) -j1 TARGET=_PICO_RP2040_ KEYMAP=$(KEYMAP) all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_master_$(KEYMAP).uf2
	@echo 'Master: $(EXE_FILE)_master_$(KEYMAP).uf2'
	@echo ' '

split_slave:
	@echo '=== Building SLAVE firmware (keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_RP2040_ clean
	$(MAKE) -j1 TARGET=_PICO_RP2040_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DKB_IS_MASTER=0" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_slave_$(KEYMAP).uf2
	@echo 'Slave: $(EXE_FILE)_slave_$(KEYMAP).uf2'
	@echo ' '

# ======================================================================
# キーボードブレイン構成: 左右の Pico を両方 I2C スレーブにし、
# NUCLEO-H533RE (build_bsp2) が I2C マスタとしてキー処理と USB HID を担う
#   左手: I2C アドレス 0x31、右手: 0x32 (既定のスレーブアドレス)
# ======================================================================
brain_slaves: brain_slave_left brain_slave_right
	@echo ' '
	@echo '=== Brain slave firmwares built ==='
	@echo '  Left  (0x31): $(EXE_FILE)_brain_slave_L.uf2'
	@echo '  Right (0x32): $(EXE_FILE)_brain_slave_R.uf2'
	@echo ' '

brain_slave_left:
	@echo '=== Building BRAIN SLAVE firmware (left hand, I2C 0x31) ==='
	$(MAKE) TARGET=_PICO_RP2040_ clean
	$(MAKE) -j1 TARGET=_PICO_RP2040_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DKB_IS_MASTER=0 -DKB_MATRIX_HAND_R=0 -DI2C_SLAVE_ADDRESS=0x31" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_brain_slave_L.uf2
	@echo ' '

brain_slave_right:
	@echo '=== Building BRAIN SLAVE firmware (right hand, I2C 0x32) ==='
	$(MAKE) TARGET=_PICO_RP2040_ clean
	$(MAKE) -j1 TARGET=_PICO_RP2040_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DKB_IS_MASTER=0 -DKB_MATRIX_HAND_R=1 -DI2C_SLAVE_ADDRESS=0x32" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_brain_slave_R.uf2
	@echo ' '

# ======================================================================
# 離席判定 (Pico4ML): カメラの画像から人の有無を推論し、離席したら Win+L を送る
#   TensorFlow Lite Micro (lib/pico-tflmicro) をライブラリにしてからリンクする
# ======================================================================
presence:
	@echo '=== Building PRESENCE firmware (Pico4ML) ==='
	$(MAKE) TARGET=_PICO_RP2040_ APP=presence clean
	$(MAKE) -f ../app_presence/tflm.mk TFLM_ROOT=../../lib/pico-tflmicro OUT=tflm_build \
		CROSS=arm-none-eabi- ARCH="-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft"
	$(MAKE) -j1 TARGET=_PICO_RP2040_ APP=presence all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_presence.uf2
	@echo 'Presence: $(EXE_FILE)_presence.uf2'
	@echo ' '

# キーボードと同居させる (TL Split のマスター、単体動作)。Pico4ML で試すときは
# マトリクスのピンがカメラと重なるので PRESENCE_KB_DEFS=-DKB_MATRIX_NONE を付ける
presence_kb:
	@echo '=== Building KEYBOARD + PRESENCE firmware ==='
	$(MAKE) TARGET=_PICO_RP2040_ PRESENCE=1 clean
	$(MAKE) -f ../app_presence/tflm.mk TFLM_ROOT=../../lib/pico-tflmicro OUT=tflm_build \
		CROSS=arm-none-eabi- ARCH="-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft"
	$(MAKE) -j1 TARGET=_PICO_RP2040_ PRESENCE=1 KEYMAP=$(KEYMAP) \
		EXTRA_DEFS="-DSPLIT_ENABLED=0 $(PRESENCE_KB_DEFS)" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_presence_kb.uf2
	@echo 'Keyboard + presence: $(EXE_FILE)_presence_kb.uf2'
	@echo ' '

# ======================================================================
# 片手ビルド: 分割の片側だけを USB 接続して単体で動かす
#   HAND=L (既定) / HAND=R でスキャンするピン割当と担当行を選ぶ
#   half        … その手のキーマップどおりに普通に入力する
#   wiring_test … 押したセルの座標を "R03C2" 形式で入力する (配線確認用)
# ======================================================================
HAND ?= L
ifeq ($(HAND),R)
HAND_DEFS := -DKB_MATRIX_HAND_R=1
else
HAND_DEFS := -DKB_MATRIX_HAND_R=0
endif

# 片側の基板だけを USB 接続し、その手のキーだけを普通に入力する
half:
	@echo '=== Building STANDALONE HALF firmware (hand=$(HAND), keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_RP2040_ clean
	$(MAKE) -j1 TARGET=_PICO_RP2040_ KEYMAP=$(KEYMAP) EXTRA_DEFS="$(HAND_DEFS) -DSPLIT_ENABLED=0" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_half_$(HAND)_$(KEYMAP).uf2
	@echo 'Half: $(EXE_FILE)_half_$(HAND)_$(KEYMAP).uf2'
	@echo ' '

wiring_test:
	@echo '=== Building WIRING TEST firmware (hand=$(HAND)) ==='
	$(MAKE) TARGET=_PICO_RP2040_ clean
	$(MAKE) -j1 TARGET=_PICO_RP2040_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DKB_WIRING_TEST $(HAND_DEFS)" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_wiring_test_$(HAND).uf2
	@echo 'Wiring test: $(EXE_FILE)_wiring_test_$(HAND).uf2'
	@echo ' '

# ======================================================================
# BLE split: 左右両方 Pico W (BLE ワイヤレス接続)
# ======================================================================
split_ble: split_ble_master split_ble_slave
	@echo ' '
	@echo '=== Both BLE split firmwares built (keymap=$(KEYMAP)) ==='
	@echo '  Master (Pico W, BLE): $(EXE_FILE)_master_ble_$(KEYMAP).uf2'
	@echo '  Slave  (Pico W, BLE): $(EXE_FILE)_slave_ble_$(KEYMAP).uf2'

split_ble_master:
	@echo '=== Building BLE MASTER (keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_W_ clean
	$(MAKE) -j1 TARGET=_PICO_W_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DSPLIT_TRANSPORT_BLE" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_master_ble_$(KEYMAP).uf2

split_ble_slave:
	@echo '=== Building BLE SLAVE (keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_W_ clean
	$(MAKE) -j1 TARGET=_PICO_W_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DKB_IS_MASTER=0 -DSPLIT_TRANSPORT_BLE" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_slave_ble_$(KEYMAP).uf2

# ======================================================================
# Hybrid split: Master=Pico W (BLE+USB auto), Slave=任意 (I2C or BLE)
#   Master は起動時に I2C slave を probe し、なければ BLE scan
#   Slave は Pico W なら BLE、RP2040 なら I2C
# ======================================================================
split_auto: split_auto_master split_auto_slave_i2c
	@echo ' '
	@echo '=== Hybrid split firmwares built (keymap=$(KEYMAP)) ==='
	@echo '  Master (Pico W, auto): $(EXE_FILE)_master_auto_$(KEYMAP).uf2'
	@echo '  Slave  (RP2040, I2C):  $(EXE_FILE)_slave_$(KEYMAP).uf2'
	@echo '  (Slave に BLE 版を使う場合: make split_auto_slave_ble)'

split_auto_master:
	@echo '=== Building AUTO MASTER (keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_W_ clean
	$(MAKE) -j1 TARGET=_PICO_W_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DSPLIT_TRANSPORT_AUTO" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_master_auto_$(KEYMAP).uf2

split_auto_slave_i2c:
	@echo '=== Building I2C SLAVE (keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_RP2040_ clean
	$(MAKE) -j1 TARGET=_PICO_RP2040_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DKB_IS_MASTER=0" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_slave_$(KEYMAP).uf2

split_auto_slave_ble:
	@echo '=== Building BLE SLAVE (keymap=$(KEYMAP)) ==='
	$(MAKE) TARGET=_PICO_W_ clean
	$(MAKE) -j1 TARGET=_PICO_W_ KEYMAP=$(KEYMAP) EXTRA_DEFS="-DKB_IS_MASTER=0 -DSPLIT_TRANSPORT_BLE" all
	cp $(EXE_FILE).uf2 $(EXE_FILE)_slave_ble_$(KEYMAP).uf2

.PHONY: all clean dependents split split_master split_slave half wiring_test brain_slaves brain_slave_left brain_slave_right
.PHONY: split_ble split_ble_master split_ble_slave
.PHONY: split_auto split_auto_master split_auto_slave_i2c split_auto_slave_ble
