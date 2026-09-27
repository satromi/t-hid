################################################################################
# μT-Kernel 3.0 BSP2 — NUCLEO-H533RE キーボードブレイン
#
#   USER USB    : USB HID キーボード + FIDO U2F (複合デバイス)
#   I2C1        : 左右の Pico (I2C スレーブ) からマトリクス取得
#   WIZ550io    : W5500 Ethernet + MCP サーバ (wasm キー処理のアップロード)
#
# Usage:
#   make                    # Build firmware (NET/MCP/WASM/U2F 全て有効)
#   make NET=0              # キーボード + U2F (ネットワークなし)
#   make NET=0 U2F=0        # キーボードのみ
#   make KEYMAP=dvorak      # 組み込みキーマップの選択
#   make GAMEPAD=1          # USB にゲームパッドを加える (キーマップの JS_* キー)
#   make TZ=1               # TrustZone: U2F の鍵と暗号処理をセキュア側に分ける
#                           #   (オプションバイトの設定が必要: docs/guide/u2f.md)
#   make TZ=1 TZTEST=1      # TrustZone の自己テストを一緒に動かす
#   make clean              # Clean
#   make flash              # Flash via ST-Link
#
# Prerequisites:
#   - arm-none-eabi-gcc (GNU ARM Embedded Toolchain)
#   - STM32CubeProgrammer (for flash)
################################################################################

# ツールチェーン
CROSS = arm-none-eabi-
GCC   = $(CROSS)gcc
AS    = $(CROSS)gcc
LD    = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE  = $(CROSS)size

# 出力
TARGET = mtk3_h533
OUTDIR = build

################################################################################
# 機能選択 (依存: WASM → MCP → NET)
#   下位を無効にすると上位も無効 (NET=0 ならキーボードのみ)
################################################################################
NET    ?= 1
MCP    ?= 1
WASM   ?= 1
KEYMAP ?= default

# FIDO U2F 認証器 (USB 複合デバイス, ネットワーク不要)
U2F    ?= 1

# ゲームパッド (USB 複合デバイスに HID ゲームパッドを加える, docs/guide/gamepad.md)
GAMEPAD ?= 0

# TrustZone (U2F の鍵と暗号処理をセキュア側イメージに置く)
TZ     ?= 0
ifeq ($(TZ),1)
override U2F := 1
endif

ifeq ($(NET),0)
override MCP := 0
endif
ifeq ($(MCP),0)
override WASM := 0
endif
ifeq ($(WASM),1)
override MCP := 1
endif
ifeq ($(MCP),1)
override NET := 1
endif

FEATURE_DEFS := -DKB_BRAIN
ifeq ($(NET),1)
FEATURE_DEFS += -DUSE_NET
endif
ifeq ($(MCP),1)
FEATURE_DEFS += -DUSE_MCP
endif
ifeq ($(WASM),1)
FEATURE_DEFS += -DUSE_WASM
endif
ifeq ($(U2F),1)
FEATURE_DEFS += -DUSE_U2F -DUSB_HID_FIDO
endif
ifeq ($(TZ),1)
FEATURE_DEFS += -DTZ_NONSECURE
endif
ifeq ($(GAMEPAD),1)
FEATURE_DEFS += -DUSB_HID_GAMEPAD
endif

# ターゲット定義
CPU_FLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
DEFINES   = -D_STM32CUBE_NUCLEO_H533_ -DSTM32H533xx -DWIZCHIP_W5500 $(FEATURE_DEFS) $(UECC_DEFS) \
	-DKEYMAP_FILE='"keymaps/$(KEYMAP)/keymap.c"'

# BSP2 ルートパス
BSP2 = ../mtk3_bsp2

# インクルードパス
INCPATH = \
	-I$(BSP2) \
	-I$(BSP2)/include \
	-I$(BSP2)/mtkernel/include \
	-I$(BSP2)/config \
	-I$(BSP2)/mtkernel/kernel/knlinc \
	-I../device/include \
	-I$(BSP2)/mtkernel/device/include \
	-I../device/wiznet \
	-I../device/wiznet/sysdepend/w5500 \
	-I../app_h533 \
	-I../app_program/keyboard \
	-I../lib/cjson \
	-I../lib/wasm3/source \
	-I../lib/micro-ecc \
	-I$(OUTDIR) \
	-I.

# コンパイルフラグ
CFLAGS = $(CPU_FLAGS) -ffreestanding -std=gnu11 -O1 -g \
	-Wall -Wno-array-bounds $(DEFINES) $(INCPATH)

ASFLAGS = $(CPU_FLAGS) $(DEFINES) $(INCPATH)

ifeq ($(TZ),1)
LDSCRIPT = nucleo_h533_ns.ld
FLASH_ADDR = 0x08010000
else
LDSCRIPT = nucleo_h533.ld
FLASH_ADDR = 0x08000000
endif

LDFLAGS = $(CPU_FLAGS) -T $(LDSCRIPT) -Wl,-Map,$(OUTDIR)/$(TARGET).map \
	-nostartfiles --specs=nosys.specs
LDLIBS = -lm

################################################################################
# ソースファイル
################################################################################

# μT-Kernel カーネル
KERN_SRC = \
	$(BSP2)/mtkernel/kernel/sysinit/sysinit.c \
	$(BSP2)/mtkernel/kernel/inittask/inittask.c \
	$(BSP2)/mtkernel/kernel/tkernel/task.c \
	$(BSP2)/mtkernel/kernel/tkernel/task_manage.c \
	$(BSP2)/mtkernel/kernel/tkernel/task_sync.c \
	$(BSP2)/mtkernel/kernel/tkernel/semaphore.c \
	$(BSP2)/mtkernel/kernel/tkernel/eventflag.c \
	$(BSP2)/mtkernel/kernel/tkernel/mutex.c \
	$(BSP2)/mtkernel/kernel/tkernel/messagebuf.c \
	$(BSP2)/mtkernel/kernel/tkernel/mailbox.c \
	$(BSP2)/mtkernel/kernel/tkernel/memory.c \
	$(BSP2)/mtkernel/kernel/tkernel/mempfix.c \
	$(BSP2)/mtkernel/kernel/tkernel/mempool.c \
	$(BSP2)/mtkernel/kernel/tkernel/time_calls.c \
	$(BSP2)/mtkernel/kernel/tkernel/timer.c \
	$(BSP2)/mtkernel/kernel/tkernel/misc_calls.c \
	$(BSP2)/mtkernel/kernel/tkernel/device.c \
	$(BSP2)/mtkernel/kernel/tkernel/deviceio.c \
	$(BSP2)/mtkernel/kernel/tkernel/int.c \
	$(BSP2)/mtkernel/kernel/tkernel/klock.c \
	$(BSP2)/mtkernel/kernel/tkernel/power.c \
	$(BSP2)/mtkernel/kernel/tkernel/rendezvous.c \
	$(BSP2)/mtkernel/kernel/tkernel/tkinit.c \
	$(BSP2)/mtkernel/kernel/tkernel/wait.c \
	$(BSP2)/mtkernel/kernel/tkernel/objname.c \
	$(BSP2)/mtkernel/kernel/tkernel/cpuctl.c \
	$(BSP2)/mtkernel/kernel/tstdlib/string.c \
	$(BSP2)/mtkernel/kernel/tstdlib/bitop.c

# BSP2 システム依存 (STM32Cube + ARMv8-M)
SYSDEP_SRC = \
	$(BSP2)/sysdepend/stm32_cube/hw_setting.c \
	$(BSP2)/sysdepend/stm32_cube/devinit.c \
	$(BSP2)/sysdepend/stm32_cube/power_save.c \
	$(BSP2)/sysdepend/stm32_cube/cpu/core/armv8m/sys_start.c \
	$(BSP2)/sysdepend/stm32_cube/cpu/core/armv8m/interrupt.c \
	$(BSP2)/sysdepend/stm32_cube/cpu/core/armv8m/cpu_cntl.c \
	$(BSP2)/sysdepend/stm32_cube/cpu/core/armv8m/exc_hdr.c

# BSP2 ライブラリ
LIB_SRC = \
	$(BSP2)/sysdepend/stm32_cube/lib/libtk/cpu/core/armv8m/int_armv8m.c \
	$(BSP2)/sysdepend/stm32_cube/lib/libtk/cpu/core/armv8m/wusec_armv8m.c \
	$(BSP2)/sysdepend/stm32_cube/lib/libtk/cpu/stm32h5/int_stm32h5.c \
	$(BSP2)/sysdepend/stm32_cube/lib/libtk/cpu/stm32h5/ptimer_stm32h5.c \
	$(BSP2)/mtkernel/lib/libtk/fastlock.c \
	$(BSP2)/mtkernel/lib/libtk/fastmlock.c \
	$(BSP2)/mtkernel/lib/libtk/kmalloc.c \
	$(BSP2)/mtkernel/lib/libtm/libtm.c \
	$(BSP2)/mtkernel/lib/libtm/libtm_printf.c \
	$(BSP2)/sysdepend/stm32_cube/lib/libtm/nucleo_stm32h533/tm_com.c

# ASM ソース
ASM_SRC = \
	$(BSP2)/sysdepend/stm32_cube/cpu/core/armv8m/dispatch.S \
	startup_stm32h533.S

# ボード初期化 + 拡張
BOARD_SRC = \
	stm32h5_hal_stub.c \
	../kernel/extension/datetime/datetime.c

# デバイス共通 + USB HID + I2C マスタ
DEV_SRC = \
	../device/common/drvif/msdrvif.c \
	../device/usb_hid/usb_hid.c \
	../device/usb_hid/usb_hid_class.c \
	../device/usb_hid/sysdepend/stm32h5/usb_hid_stm32h5.c \
	../device/i2c_master/i2c_master_stm32h5.c \
	../device/flash/stm32h5_flash.c

# キーボードブレイン (キー処理は app_program/keyboard のソースを共用)
APP_SRC = \
	../app_h533/usermain.c \
	../app_h533/kb_brain.c \
	../app_h533/kb_wasm.c \
	../app_program/keyboard/kb_process.c \
	../app_program/keyboard/kb_keymap.c \
	../app_program/keyboard/split_transport.c

# WIZnet ドライバ (W5500 + プロトコルスタック)
NET_SRC = \
	../device/wiznet/sysdepend/w5500/w5500_io.c \
	../device/wiznet/sysdepend/w5500/w5500_chip.c \
	../device/wiznet/sysdepend/w5500/w5500_spi_stm32.c \
	../device/wiznet/wiznet_dev.c \
	../device/wiznet/tk_socket.c \
	../device/wiznet/tk_dhcp.c \
	../device/wiznet/tk_dns.c \
	../device/wiznet/tk_mqtt.c \
	../device/wiznet/tk_sntp.c \
	../device/wiznet/tk_netmon.c \
	../device/wiznet/tk_httpd.c

# SHA-256 (MCP の認証、wasm 検証、U2F で共用)
COMMON_SRC = \
	../device/wiznet/tk_sha256.c

# FIDO U2F 認証器 (P-256 は PKA、自己診断で不一致なら micro-ecc)
U2F_SRC = \
	../app_h533/u2f/u2f_task.c \
	../app_h533/u2f/ctaphid.c \
	../app_h533/u2f/u2f.c \
	../app_h533/u2f/u2f_cert.c \
	../app_h533/u2f/u2f_crypto.c \
	../app_h533/u2f/u2f_store.c \
	../app_h533/u2f/u2f_mcp.c \
	../lib/micro-ecc/uECC.c

# micro-ecc: P-256 のみ
UECC_DEFS = \
	-DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
	-DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 \
	-DuECC_SUPPORT_COMPRESSED_POINT=0

# MCP サーバ (cJSON を使用)
MCP_SRC = \
	../device/wiznet/tk_mcp_core.c \
	../device/wiznet/tk_mcp_tools.c \
	../device/wiznet/tk_mcp_vm.c \
	../lib/cjson/cJSON.c

# WebAssembly ランタイム + キーボード用 MCP ツール
WASM_SRC = \
	../device/wiznet/tk_wasm.c \
	../device/wiznet/tk_wasm_host.c \
	../device/wiznet/tk_wasm_mcp.c \
	../app_h533/kb_brain_mcp.c

# wasm3 コア (WASI / uvwasi / tracer は除外)
WASM3_SRC = \
	../lib/wasm3/source/m3_api_libc.c \
	../lib/wasm3/source/m3_bind.c \
	../lib/wasm3/source/m3_code.c \
	../lib/wasm3/source/m3_compile.c \
	../lib/wasm3/source/m3_core.c \
	../lib/wasm3/source/m3_env.c \
	../lib/wasm3/source/m3_exec.c \
	../lib/wasm3/source/m3_function.c \
	../lib/wasm3/source/m3_info.c \
	../lib/wasm3/source/m3_module.c \
	../lib/wasm3/source/m3_parse.c

################################################################################
# TrustZone (TZ=1)
#   非セキュア側: U2F はタスク・CTAPHID・MCP だけを残し、処理はセキュア側を呼ぶ
#   セキュア側:   U2F の処理・鍵・暗号・鍵の保存 (build/secure/secure.elf)
################################################################################
ifeq ($(TZ),1)
U2F_SRC = \
	../app_h533/u2f/u2f_task.c \
	../app_h533/u2f/ctaphid.c \
	../app_h533/u2f/u2f_mcp.c \
	../tz_h533/nonsecure/u2f_sec_client.c
INCPATH := -I../tz_h533/nonsecure/kernel/include $(INCPATH) -I../tz_h533/include -I../app_h533/u2f

# μT-Kernel 3.0 セキュア機能拡張の標準実行モデル: カーネルの一部を差し替える
TZ_KERN = ../tz_h533/nonsecure/kernel
KERN_SRC := $(filter-out %/task_manage.c %/inittask.c,$(KERN_SRC)) \
	$(TZ_KERN)/task_manage.c $(TZ_KERN)/inittask.c $(TZ_KERN)/task_sysdep.c
SYSDEP_SRC := $(filter-out %/sys_start.c,$(SYSDEP_SRC)) $(TZ_KERN)/sys_start.c
ASM_SRC := $(filter-out %/dispatch.S,$(ASM_SRC)) $(TZ_KERN)/dispatch.S

SEC_DIR    = $(OUTDIR)/secure
SEC_IMPLIB = $(SEC_DIR)/sec_nsclib.o

SEC_SRC = \
	../tz_h533/secure/sec_main.c \
	../tz_h533/secure/sec_task.c \
	../tz_h533/secure/sec_u2f_api.c \
	../app_h533/u2f/u2f.c \
	../app_h533/u2f/u2f_cert.c \
	../app_h533/u2f/u2f_crypto.c \
	../app_h533/u2f/u2f_store.c \
	../device/wiznet/tk_sha256.c \
	../device/flash/stm32h5_flash.c \
	../lib/micro-ecc/uECC.c

# 自己テスト (TZTEST=1): セキュア側の実行中のタスク切り替えなどを実機で確かめる
ifeq ($(TZTEST),1)
U2F_SRC += ../tz_h533/nonsecure/tz_selftest.c
SEC_SRC += ../tz_h533/secure/sec_selftest.c
FEATURE_DEFS += -DTZ_SELFTEST
endif

SEC_CFLAGS =$(CPU_FLAGS) -mcmse -ffreestanding -std=gnu11 -O1 -g \
	-Wall -Wno-array-bounds -DU2F_SECURE -DUSE_U2F $(UECC_DEFS) \
	-I../tz_h533/secure/shim -I../tz_h533/include -I../app_h533/u2f \
	-I../lib/micro-ecc -I../device/wiznet

SEC_OBJS = $(patsubst %.c,$(SEC_DIR)/%.o,$(notdir $(SEC_SRC))) $(SEC_DIR)/startup_sec.o \
	$(SEC_DIR)/sec_ctx.o
endif

CSRC = $(KERN_SRC) $(SYSDEP_SRC) $(LIB_SRC) $(BOARD_SRC) $(DEV_SRC) $(APP_SRC) $(COMMON_SRC)
ifeq ($(U2F),1)
CSRC += $(U2F_SRC)
endif
ifeq ($(NET),1)
CSRC += $(NET_SRC)
endif
ifeq ($(MCP),1)
CSRC += $(MCP_SRC)
endif
# wasm3 の malloc/calloc は tk_wasm.c の Kmalloc ラッパーに差し替わるため、
# wasm3 は tk_wasm.c と必ず一緒にリンクする
ifeq ($(WASM),1)
CSRC += $(WASM_SRC) $(WASM3_SRC)
endif

################################################################################
# wasm3 設定 (Cortex-M33 + FPU)
#   M3_HAS_TAIL_CALL は既定 (1): GCC の musttail で命令ディスパッチを
#   末尾呼び出しにし、wasm 命令ごとにネイティブスタックを積まない
################################################################################
WASM3_CFLAGS = \
	-O2 \
	-Dd_m3HasFloat=1 \
	-Dd_m3CodePageAlignSize=4096 \
	-Dd_m3MaxFunctionStackHeight=256 \
	-Dd_m3MaxLinearMemoryPages=2 \
	-Dd_m3VerboseErrorMessages=0 \
	-Dd_m3CascadedOpcodes=0 \
	-Dd_m3RecordBacktraces=0 \
	-Dd_m3EnableOpProfiling=0 \
	-Dd_m3EnableOpTracing=0 \
	-Dd_m3EnableStrace=0 \
	-Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable

################################################################################
# ビルドルール
################################################################################

OBJS = $(patsubst %.c,$(OUTDIR)/%.o,$(notdir $(CSRC)))
OBJS += $(patsubst %.S,$(OUTDIR)/%.o,$(notdir $(ASM_SRC)))

VPATH = $(sort $(dir $(CSRC) $(ASM_SRC)))

all: $(OUTDIR) $(OUTDIR)/$(TARGET).elf $(OUTDIR)/$(TARGET).bin
	$(SIZE) $(OUTDIR)/$(TARGET).elf

$(OUTDIR):
	mkdir -p $(OUTDIR)

# MCP API キー埋め込みヘッダ (.mcp_api_key が無ければ Flash 保存キーのみ使用)
APIKEY_HDR = $(OUTDIR)/mcp_apikey_embedded.h

$(APIKEY_HDR): | $(OUTDIR)
	python ../build_make/gen_apikey_header.py ../.mcp_api_key $@

$(OUTDIR)/tk_mcp_core.o: $(APIKEY_HDR)

$(OBJS): | $(OUTDIR)

# wasm3 ソースは専用フラグ
WASM3_OBJS = $(patsubst %.c,$(OUTDIR)/%.o,$(notdir $(WASM3_SRC)))
$(WASM3_OBJS): $(OUTDIR)/%.o: ../lib/wasm3/source/%.c
	@echo 'CC (wasm3): $<'
	$(GCC) $(CFLAGS) $(WASM3_CFLAGS) -c -o $@ $<

# tk_mcp_vm.c: GCC 15 Cortex-M33 デバッグ情報バグ回避 (-g0)
$(OUTDIR)/tk_mcp_vm.o: ../device/wiznet/tk_mcp_vm.c
	@echo 'CC (no-debug): $<'
	$(GCC) $(CFLAGS) -g0 -c -o $@ $<

$(OUTDIR)/%.o: %.c
	@echo 'CC: $<'
	$(GCC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/%.o: %.S
	@echo 'AS: $<'
	$(AS) $(ASFLAGS) -c -o $@ $<

ifeq ($(TZ),1)
$(SEC_DIR):
	mkdir -p $(SEC_DIR)

define SEC_RULE
$(SEC_DIR)/$(notdir $(1:.c=.o)): $(1) | $(SEC_DIR)
	@echo 'CC (secure): $$<'
	$(GCC) $(SEC_CFLAGS) -c -o $$@ $$<
endef
$(foreach f,$(SEC_SRC),$(eval $(call SEC_RULE,$(f))))

$(SEC_DIR)/startup_sec.o: ../tz_h533/secure/startup_sec.S | $(SEC_DIR)
	$(AS) $(CPU_FLAGS) -c -o $@ $<

$(SEC_DIR)/sec_ctx.o: ../tz_h533/secure/sec_ctx.S | $(SEC_DIR)
	$(AS) $(CPU_FLAGS) -mcmse -c -o $@ $<

# 差し替えたカーネルのファイル。同じ名前の元のファイルより優先し、
# 元の場所のヘッダを "..." で読めるようにする
$(OUTDIR)/task_manage.o: $(TZ_KERN)/task_manage.c | $(OUTDIR)
	@echo 'CC (tz kernel): $<'
	$(GCC) $(CFLAGS) -iquote $(BSP2)/mtkernel/kernel/tkernel -c -o $@ $<

$(OUTDIR)/inittask.o: $(TZ_KERN)/inittask.c | $(OUTDIR)
	@echo 'CC (tz kernel): $<'
	$(GCC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/sys_start.o: $(TZ_KERN)/sys_start.c | $(OUTDIR)
	@echo 'CC (tz kernel): $<'
	$(GCC) $(CFLAGS) -iquote $(BSP2)/sysdepend/stm32_cube/cpu/core/armv8m -c -o $@ $<

$(OUTDIR)/dispatch.o: $(TZ_KERN)/dispatch.S | $(OUTDIR)
	@echo 'AS (tz kernel): $<'
	$(AS) $(ASFLAGS) -c -o $@ $<

# 呼び出し口のアドレスを非セキュア側に渡すライブラリ (CMSE の implib) も作る
$(SEC_DIR)/secure.elf $(SEC_IMPLIB) &: $(SEC_OBJS) ../tz_h533/secure/sec.ld
	@echo 'LD (secure): $(SEC_DIR)/secure.elf'
	$(LD) $(CPU_FLAGS) -mcmse -T ../tz_h533/secure/sec.ld -nostartfiles \
		--specs=nano.specs --specs=nosys.specs \
		-Wl,--cmse-implib,--out-implib=$(SEC_IMPLIB) \
		-Wl,-Map,$(SEC_DIR)/secure.map -o $(SEC_DIR)/secure.elf $(SEC_OBJS)
	$(SIZE) $(SEC_DIR)/secure.elf

$(SEC_DIR)/secure.bin: $(SEC_DIR)/secure.elf
	$(OBJCOPY) -O binary $< $@

all: $(SEC_DIR)/secure.bin
endif

$(OUTDIR)/$(TARGET).elf: $(OBJS) $(SEC_IMPLIB)
	@echo 'LD: $@'
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(SEC_IMPLIB) $(LDLIBS)

$(OUTDIR)/$(TARGET).bin: $(OUTDIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(OUTDIR)

# STM32CubeProgrammer の CLI。PATH にない場合は STM32_PROGRAMMER=<exe のパス> で指定する
STM32_PROGRAMMER ?= STM32_Programmer_CLI

ifeq ($(TZ),1)
flash: $(OUTDIR)/$(TARGET).bin $(SEC_DIR)/secure.bin
	"$(STM32_PROGRAMMER)" -c port=SWD -w $(SEC_DIR)/secure.bin 0x0C000000 -v
	"$(STM32_PROGRAMMER)" -c port=SWD -w $(OUTDIR)/$(TARGET).bin $(FLASH_ADDR) -v -rst
else
flash: $(OUTDIR)/$(TARGET).bin
	"$(STM32_PROGRAMMER)" -c port=SWD -w $< $(FLASH_ADDR) -v -rst
endif

.PHONY: all clean flash
