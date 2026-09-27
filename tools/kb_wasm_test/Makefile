# キーボードブレイン wasm キー処理の PC 上検証
#
#   make run                    … テスト用 wasm と検証プログラムをビルドして実行
#   make check MOD=path/to.wasm … 自作のキー処理モジュールを事前チェック
#
# 要件: clang (wasm32 ターゲット + ホスト向けビルドができること)

CLANG ?= clang
ROOT  := ../..
KB    := $(ROOT)/app_program/keyboard

WASM_FLAGS := --target=wasm32 -nostdlib -O2 -fno-builtin \
	-Wl,--no-entry -Wl,--allow-undefined -Wl,--strip-all \
	-Wl,--initial-memory=65536 -Wl,--max-memory=65536 -Wl,-z,stack-size=4096

WASM3_SRC := $(addprefix $(ROOT)/lib/wasm3/source/, \
	m3_api_libc.c m3_bind.c m3_code.c m3_compile.c m3_core.c m3_env.c \
	m3_exec.c m3_function.c m3_info.c m3_module.c m3_parse.c)

HOST_SRC := kb_wasm_test.c $(ROOT)/app_h533/kb_wasm.c $(ROOT)/device/wiznet/tk_sha256.c \
	$(KB)/kb_process.c $(KB)/kb_keymap.c $(WASM3_SRC)

HOST_FLAGS := -O1 -DUSE_WASM -DMTKBSP_CPU_STM32H5 -D_CRT_SECURE_NO_WARNINGS \
	-DKEYMAP_FILE='"../../tools/kb_wasm_test/test_keymap.c"' \
	-include shim/fake_flash.h \
	-Ishim -I$(ROOT)/app_h533 -I$(KB) -I$(ROOT)/lib/wasm3/source \
	-Wno-everything

EXE := kb_wasm_test.exe
WASMS := parity.wasm default.wasm trap.wasm noscan.wasm badimport.wasm

.PHONY: all run check clean

all: $(EXE) $(WASMS)

parity.wasm: ../picolib/kb/kb_default.c ../picolib/kb/kb_brain_api.h
	$(CLANG) $(WASM_FLAGS) -DKB_PARITY -o $@ $<

default.wasm: ../picolib/kb/kb_default.c ../picolib/kb/kb_brain_api.h
	$(CLANG) $(WASM_FLAGS) -o $@ $<

%.wasm: %.c
	$(CLANG) $(WASM_FLAGS) -o $@ $<

$(EXE): $(HOST_SRC) shim/tk/tkernel.h shim/fake_flash.h test_keymap.c
	$(CLANG) $(HOST_FLAGS) -o $@ $(HOST_SRC)

run: all
	./$(EXE) parity.wasm default.wasm trap.wasm noscan.wasm badimport.wasm

check: $(EXE)
	@test -n "$(MOD)" || (echo 'usage: make check MOD=path/to/module.wasm'; exit 2)
	./$(EXE) --check $(MOD)

clean:
	rm -f $(EXE) $(WASMS) *.pdb *.ilk
