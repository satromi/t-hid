# tflm.mk — TensorFlow Lite Micro の静的ライブラリ (libtflm.a) を作る
#
#   make -f tflm.mk TFLM_ROOT=<lib/pico-tflmicro> OUT=<出力先> [CROSS=arm-none-eabi-] [ARCH=...]
#
#   実機 (RP2040): CROSS=arm-none-eabi- ARCH="-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft"
#   PC のテスト  : CC=clang CXX=clang++ AR=llvm-ar ARCH=
#
#   CMSIS-NN の arm_nn_mat_mult_nt_t_s8.c はデュアルコア処理 (pico-sdk の multicore) が
#   有効な状態で書かれているので、それを無効にしたものを OUT に作ってからビルドする。

TFLM_ROOT ?= ../lib/pico-tflmicro
OUT       ?= build/tflm
CROSS     ?=
CC        ?= $(CROSS)gcc
CXX       ?= $(CROSS)g++
AR        ?= $(CROSS)ar
ARCH      ?=

ifeq ($(origin CC),default)
CC := $(CROSS)gcc
endif
ifeq ($(origin CXX),default)
CXX := $(CROSS)g++
endif
ifeq ($(origin AR),default)
AR := $(CROSS)ar
endif

TFLM_MK_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(TFLM_MK_DIR)tflm_sources.mk
include $(TFLM_MK_DIR)tflm_flags.mk

TFLM_CFLAGS   := $(ARCH) -Os -ffunction-sections -fdata-sections -std=gnu11 $(TFLM_DEFS) $(TFLM_INC)
TFLM_CXXFLAGS := $(ARCH) -Os -ffunction-sections -fdata-sections -std=gnu++17 \
	-fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-unwind-tables \
	-include cstdint $(TFLM_DEFS) $(TFLM_INC)

MATMUL_SRC := src/third_party/cmsis_nn/Source/NNSupportFunctions/arm_nn_mat_mult_nt_t_s8.c
MATMUL_GEN := $(OUT)/gen/arm_nn_mat_mult_nt_t_s8.c

OBJS := $(patsubst %,$(OUT)/obj/%.o,$(TFLM_SRCS)) $(OUT)/obj/gen/arm_nn_mat_mult_nt_t_s8.c.o

$(OUT)/libtflm.a: $(OBJS)
	@rm -f $@
	@echo "AR $@"; $(AR) rcs $@ $^

$(MATMUL_GEN): $(TFLM_ROOT)/$(MATMUL_SRC)
	@mkdir -p $(dir $@)
	sed 's/^#define TF_LITE_PICO_MULTICORE/\/* dual-core disabled *\//' $< > $@

$(OUT)/obj/gen/arm_nn_mat_mult_nt_t_s8.c.o: $(MATMUL_GEN)
	@mkdir -p $(dir $@)
	$(CC) $(TFLM_CFLAGS) -c -o $@ $<

$(OUT)/obj/%.cpp.o: $(TFLM_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(TFLM_CXXFLAGS) -c -o $@ $<

$(OUT)/obj/%.c.o: $(TFLM_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TFLM_CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -rf $(OUT)
