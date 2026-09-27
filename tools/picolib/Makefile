# picolib — wasm32 用サンプル ビルドルール
#
# 要件:
#   clang 16+ (wasm32 target 付き)
#   Windows: winget install LLVM.LLVM
#   Linux:   apt install clang lld
#   macOS:   brew install llvm
#
# 検証: `clang --target=wasm32 --print-supported-cpus` が mvp 含むこと

CLANG ?= clang

# wasm32 用 cflags:
#   -target wasm32        — ターゲット ISA
#   -nostdlib             — libc なし
#   -O2                   — 最適化
#   -Wl,--no-entry        — _start なし
#   -Wl,--allow-undefined — host import を許容
#   -Wl,--export=run      — run 関数を exportable に
#   -Wl,--strip-all       — デバッグ情報除去 (サイズ最小)
#   -fno-builtin          — builtin 最適化無効 (picolib.h の関数名と衝突回避)
#   -Wl,--initial-memory=65536 -Wl,--max-memory=65536
#                         — WASM linear memory を 1 page=64KB 固定 (growable 無効)。
#                           wasm3 on Pico W の Kmalloc では複数 page の動的
#                           成長に連続領域を確保できないため必須
#   -Wl,-z,stack-size=4096 — user stack 4KB (典型サンプルに十分)
CFLAGS := --target=wasm32 -nostdlib -O2 -fno-builtin \
          -Wl,--no-entry \
          -Wl,--allow-undefined \
          -Wl,--export=run \
          -Wl,--strip-all \
          -Wl,--initial-memory=65536 \
          -Wl,--max-memory=65536 \
          -Wl,-z,stack-size=4096

SRCS := $(wildcard examples/*.c)
OUT  := $(SRCS:.c=.wasm)

# キーボードブレイン (NUCLEO-H533RE) 用キー処理モジュール
#   run ではなく kb_scan / kb_init を export する
KB_SRCS := $(wildcard kb/*.c)
KB_OUT  := $(KB_SRCS:.c=.wasm)
comma := ,
KB_CFLAGS := $(filter-out -Wl$(comma)--export=run,$(CFLAGS))

.PHONY: all clean verify kb

all: $(OUT) $(KB_OUT)

kb: $(KB_OUT)

kb/%.wasm: kb/%.c kb/kb_brain_api.h
	$(CLANG) $(KB_CFLAGS) -o $@ $<
	@wc -c $@ | awk '{printf "  %s: %d bytes\n", $$2, $$1}'

examples/%.wasm: examples/%.c picolib.h
	$(CLANG) $(CFLAGS) -o $@ $<
	@wc -c $@ | awk '{printf "  %s: %d bytes\n", $$2, $$1}'

# 検証: clang + wasm32 target が利用可能か
verify:
	@$(CLANG) --version | head -1
	@$(CLANG) --target=wasm32 --print-target-triple

clean:
	rm -f examples/*.wasm kb/*.wasm
