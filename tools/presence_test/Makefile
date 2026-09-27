# 離席判定 (app_presence) の推論部分の PC 上検証
#
#   make run   … TensorFlow Lite Micro を PC 向けにビルドし、付属の画像で判定を確かめる
#
# 要件: clang / llvm-ar (ホスト向けビルド)

CLANG   ?= clang
CLANGXX ?= clang++
LLVMAR  ?= llvm-ar
ROOT    := ../..
TFLM_ROOT := $(ROOT)/lib/pico-tflmicro
TFLM    := $(TFLM_ROOT)
MODEL   := $(TFLM)/examples/person_detection/tensorflow/lite/micro/tools/make/downloads/person_model
OUT     := build
CC      := $(CLANG)
CXX     := $(CLANGXX)
AR      := $(LLVMAR)
# CMSIS の ARM 用コンパイラ定義を PC 向けに補う
ARCH    := -D__RESTRICT=__restrict "-D__STATIC_FORCEINLINE=static inline" "-D__STATIC_INLINE=static inline" -DCLZ=__builtin_clz

include $(ROOT)/app_presence/tflm.mk

EXE := presence_test.exe
.DEFAULT_GOAL := $(EXE)
APP_SRC := presence_test.cpp $(ROOT)/app_presence/presence_ml.cpp $(ROOT)/app_presence/tflm_port.cpp \
	$(MODEL)/person_detect_model_data.cpp $(MODEL)/person_image_data.cpp $(MODEL)/no_person_image_data.cpp

$(EXE): $(APP_SRC) $(OUT)/libtflm.a
	$(CLANGXX) -O1 -std=c++17 -D_CRT_SECURE_NO_WARNINGS -include cstdint $(ARCH) $(TFLM_DEFS) $(TFLM_INC) \
		-I$(ROOT)/app_presence -I$(TFLM)/examples/person_detection -Wno-everything -o $@ $(APP_SRC) $(OUT)/libtflm.a

run: $(EXE)
	./$(EXE)

.PHONY: run
