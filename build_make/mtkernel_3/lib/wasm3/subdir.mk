################################################################################
# wasm3 WebAssembly Runtime (Cortex-M0+ / RP2040 向け設定)
#
# 既存の Bytecode VM を段階的に置換するための WebAssembly 実行環境。
# WASI / uvwasi / tracer は除外して最小構成でビルド。
#
# Pico W (RP2040, Cortex-M0+) 用チューニング:
#   d_m3HasFloat=0              : FPU 無しのため浮動小数無効化
#   d_m3CodePageAlignSize=4096  : default 32KB は大きすぎ
#   d_m3MaxFunctionStackHeight=256 : default 2000 → メモリ節約
#   d_m3MaxLinearMemoryPages=2  : 1 page=64KB → 128KB 上限
#   d_m3VerboseErrorMessages=0  : Flash 節約
#   d_m3CascadedOpcodes=0       : opcode table ~3KB 節約
#   d_m3RecordBacktraces=0      : backtrace 無効
################################################################################

WASM3_CFLAGS := \
    -Dd_m3HasFloat=0 \
    -Dd_m3CodePageAlignSize=4096 \
    -Dd_m3MaxFunctionStackHeight=256 \
    -Dd_m3MaxLinearMemoryPages=2 \
    -Dd_m3VerboseErrorMessages=0 \
    -Dd_m3CascadedOpcodes=0 \
    -Dd_m3RecordBacktraces=0 \
    -Dd_m3EnableOpProfiling=0 \
    -Dd_m3EnableOpTracing=0 \
    -Dd_m3EnableStrace=0 \
    -DM3_HAS_TAIL_CALL=0

WASM3_INCPATH := -I"../lib/wasm3/source"

# wasm3 コアソース (WASI / uvwasi / tracer は除外)
WASM3_OBJS = \
    ./mtkernel_3/lib/wasm3/m3_api_libc.o \
    ./mtkernel_3/lib/wasm3/m3_bind.o \
    ./mtkernel_3/lib/wasm3/m3_code.o \
    ./mtkernel_3/lib/wasm3/m3_compile.o \
    ./mtkernel_3/lib/wasm3/m3_core.o \
    ./mtkernel_3/lib/wasm3/m3_env.o \
    ./mtkernel_3/lib/wasm3/m3_exec.o \
    ./mtkernel_3/lib/wasm3/m3_function.o \
    ./mtkernel_3/lib/wasm3/m3_info.o \
    ./mtkernel_3/lib/wasm3/m3_module.o \
    ./mtkernel_3/lib/wasm3/m3_parse.o

OBJS += $(WASM3_OBJS)
C_DEPS += $(WASM3_OBJS:.o=.d)

./mtkernel_3/lib/wasm3/%.o: ../lib/wasm3/source/%.c
	@echo 'Building wasm3: $<'
	@mkdir -p ./mtkernel_3/lib/wasm3
	$(GCC) $(CFLAGS) $(WASM3_CFLAGS) $(WASM3_INCPATH) -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
