################################################################################
# 離席判定 (Pico4ML): make TARGET=_PICO_RP2040_ APP=presence
#
#   C のソースは通常どおり、C++ (TensorFlow Lite Micro を使う部分とモデル) は g++ で
#   コンパイルする。TensorFlow Lite Micro 本体は app_presence/tflm.mk で作った
#   libtflm.a をリンクする。
################################################################################

PR_DIR   := ../app_presence
TFLM_ROOT := ../../lib/pico-tflmicro
TFLM_OUT := tflm_build
PR_MODEL := $(TFLM_ROOT)/examples/person_detection/tensorflow/lite/micro/tools/make/downloads/person_model

include $(PR_DIR)/tflm_flags.mk

GXX := arm-none-eabi-g++
PR_CXXFLAGS := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections \
	-std=gnu++17 -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-unwind-tables \
	-include cstdint $(TFLM_DEFS) $(TFLM_INC) -I$(TFLM_ROOT)/examples/person_detection

OBJS += \
./mtkernel_3/app_presence/presence_main.o \
./mtkernel_3/app_presence/hm01b0.o \
./mtkernel_3/app_presence/st7735.o \
./mtkernel_3/app_presence/presence_ml.o \
./mtkernel_3/app_presence/tflm_port.o \
./mtkernel_3/app_presence/person_detect_model_data.o \

C_DEPS += \
./mtkernel_3/app_presence/presence_main.d \
./mtkernel_3/app_presence/hm01b0.d \
./mtkernel_3/app_presence/st7735.d \

EXTRA_LIBS += $(TFLM_OUT)/libtflm.a -lsupc++

mtkernel_3/app_presence/%.o: $(PR_DIR)/%.c
	@echo 'Building presence: $<'
	$(GCC) $(CFLAGS) $(EXTRA_DEFS) $(PR_WITH_KEYBOARD) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

mtkernel_3/app_presence/%.o: $(PR_DIR)/%.cpp
	@echo 'Building presence (C++): $<'
	@mkdir -p $(dir $@)
	$(GXX) $(PR_CXXFLAGS) -c -o "$@" "$<"

mtkernel_3/app_presence/person_detect_model_data.o: $(PR_MODEL)/person_detect_model_data.cpp
	@echo 'Building model: $<'
	@mkdir -p $(dir $@)
	$(GXX) $(PR_CXXFLAGS) -c -o "$@" "$<"
