# tflm_flags.mk — TensorFlow Lite Micro を使うソースのインクルードパスと定義 (TFLM_ROOT を先に決めておく)

TFLM_INC := \
	-I$(TFLM_ROOT)/src \
	-I$(TFLM_ROOT)/src/third_party/ruy \
	-I$(TFLM_ROOT)/src/third_party/gemmlowp \
	-I$(TFLM_ROOT)/src/third_party/kissfft \
	-I$(TFLM_ROOT)/src/third_party/flatbuffers \
	-I$(TFLM_ROOT)/src/third_party/flatbuffers/include \
	-I$(TFLM_ROOT)/src/third_party/cmsis/CMSIS/Core/Include \
	-I$(TFLM_ROOT)/src/third_party/cmsis_nn/Include

TFLM_DEFS := -DTF_LITE_DISABLE_X86_NEON=1 -DTF_LITE_STATIC_MEMORY=1 -DTF_LITE_USE_CTIME=1 \
	-DTFLITE_USE_CTIME=1 -DCMSIS_NN=1 -DARDUINO=1

