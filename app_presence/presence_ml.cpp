/*
 * presence_ml.cpp — 人が写っているかの推論 (TensorFlow Lite Micro)
 *
 *   モデルは TensorFlow の person detection (MobileNet v1 0.25、96x96 グレースケール、
 *   int8 量子化)。出力は [人でない, 人] の 2 クラス。
 */
#include "presence_ml.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

extern const unsigned char g_person_detect_model_data[];

namespace {

constexpr int kTensorArenaSize = 136 * 1024;
constexpr int kPersonIndex = 1;

alignas(16) uint8_t tensor_arena[kTensorArenaSize];
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;

}  // namespace

extern "C" int presence_ml_init(void)
{
	const tflite::Model *model = tflite::GetModel(g_person_detect_model_data);
	if (model->version() != TFLITE_SCHEMA_VERSION) return -1;

	static tflite::MicroMutableOpResolver<5> resolver;
	resolver.AddAveragePool2D();
	resolver.AddConv2D();
	resolver.AddDepthwiseConv2D();
	resolver.AddReshape();
	resolver.AddSoftmax();

	static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena,
							    kTensorArenaSize);
	interpreter = &static_interpreter;
	if (interpreter->AllocateTensors() != kTfLiteOk) return -2;

	input = interpreter->input(0);
	if (input->type != kTfLiteInt8 ||
	    input->bytes != PRESENCE_ML_COLS * PRESENCE_ML_ROWS) return -3;
	return 0;
}

extern "C" int8_t *presence_ml_input(void)
{
	return input ? input->data.int8 : nullptr;
}

extern "C" int presence_ml_invoke(void)
{
	if (!interpreter || interpreter->Invoke() != kTfLiteOk) return -129;
	return interpreter->output(0)->data.int8[kPersonIndex];
}
