/* TFLM + Ethos-U55 inference runner
 *
 * tflm_init loads a caller-supplied TFLite model and allocates tensors.
 * tflm_run_inference copies input, invokes once, and copies the output.
 * The interpreter registers the Ethos-U custom op and FullyConnected
 * (CPU fallback) so a Vela-compiled model can execute on the NPU.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/micro/debug_log.h"
#include "tensorflow/lite/micro/cortex_m_generic/debug_log_callback.h"
#include "tensorflow/lite/schema/schema_generated.h"


#include "tflm_inference.h"
#include "tflm_util.h"
#if defined(NPU_PMU)
#include "npu_pmu.h"
#endif
#if defined(POWER_GPIO)
#include "power_gpio.h"
#endif

// For PMU cycle counter
#include "RTE_Components.h"
#include CMSIS_device_header

/* Op resolver - Vela models use the ethos-u custom op + FullyConnected fallback */
namespace {
using InferenceOpResolver = tflite::MicroMutableOpResolver<8>;

InferenceOpResolver op_resolver;
bool ops_registered = false;

alignas(tflite::MicroInterpreter) uint8_t interpreter_buffer[sizeof(tflite::MicroInterpreter)];
tflite::MicroInterpreter* interpreter = nullptr;

TfLiteStatus RegisterOps(InferenceOpResolver& resolver) {
  TF_LITE_ENSURE_STATUS(resolver.AddEthosU());

  // CPU based models need the software operators in runtime, list only the needed ones to save resources
  // Also Vela compiled models may need CPU operators if they are not 100% NPU accelerated (see Vela log)
#if MODEL_KWS_CPU
  TF_LITE_ENSURE_STATUS(resolver.AddReshape());
  TF_LITE_ENSURE_STATUS(resolver.AddAveragePool2D());
  TF_LITE_ENSURE_STATUS(resolver.AddConv2D());
  TF_LITE_ENSURE_STATUS(resolver.AddDepthwiseConv2D());
  TF_LITE_ENSURE_STATUS(resolver.AddFullyConnected());
  TF_LITE_ENSURE_STATUS(resolver.AddRelu());
#endif

#if MODEL_YOLO_CPU
  TF_LITE_ENSURE_STATUS(resolver.AddDepthwiseConv2D());
  TF_LITE_ENSURE_STATUS(resolver.AddConv2D());
  TF_LITE_ENSURE_STATUS(resolver.AddAdd());
  TF_LITE_ENSURE_STATUS(resolver.AddResizeNearestNeighbor());
  TF_LITE_ENSURE_STATUS(resolver.AddPad());
  TF_LITE_ENSURE_STATUS(resolver.AddMaxPool2D());
  TF_LITE_ENSURE_STATUS(resolver.AddConcatenation());
#endif

  return kTfLiteOk;
}


}  // namespace

/* Tensor arena - must be large enough for the model's activation buffers.
 * See Vela output for the peak RAM usage when using NPU
 * For CPU only you can inspect the peak RAM usage by checking UART log */
uint8_t tensor_arena[0x80000] __attribute__((aligned(16), section(".bss.tensor_arena")));

static void TflmDebugLog(const char* s) {
  printf("TFLM:%s", s);
}

extern "C" int tflm_init(const uint8_t *model_data) {
  if (model_data == nullptr) {
    printf("  [TFLM] ERROR: Invalid model buffer\n");
    return -1;
  }

  RegisterDebugLogCallback(TflmDebugLog);
  printf("  [TFLM] Initializing TensorFlow Lite Micro...\n");

  tflite::InitializeTarget();

  /* Load model */
  const tflite::Model* model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    printf("  [TFLM] ERROR: Model schema version mismatch (%lu vs %d)\n",
           (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
    return -1;
  }
  printf("  [TFLM] Model loaded (schema v%lu)\n",
         (unsigned long)model->version());
  TflmUtil::print_model_specs(model);

  /* Register ops once; the resolver is reused across init calls */
  if (!ops_registered) {
    if (RegisterOps(op_resolver) != kTfLiteOk) {
      printf("  [TFLM] ERROR: Failed to register ops\n");
      return -1;
    }
    ops_registered = true;
  }

  if (interpreter != nullptr) {
    interpreter->~MicroInterpreter();
    interpreter = nullptr;
  }

  /* Create interpreter */
  interpreter = new (interpreter_buffer) tflite::MicroInterpreter(
      model, op_resolver, tensor_arena, sizeof(tensor_arena));

  printf("  [TFLM] Tensor arena @ %p (size=%u)\n",
         (void*)tensor_arena, (unsigned)sizeof(tensor_arena));
  printf("  [TFLM] Model data  @ %p\n", (void*)model_data);

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    printf("  [TFLM] ERROR: AllocateTensors() failed\n");
    interpreter->~MicroInterpreter();
    interpreter = nullptr;
    return -1;
  }
  TflmUtil::print_arena_usage(interpreter);

  // Enable PMU cycle counter for measurements
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
  ARM_PMU_Enable();
  ARM_PMU_CNTR_Enable(PMU_CNTENSET_CCNTR_ENABLE_Msk);

#if defined(NPU_PMU)
  // Requires npu_init() to have been called
  npu_pmu_init();
#endif

#if defined(POWER_GPIO)
  if (power_gpio_init() != 0) {
    printf("  [TFLM] ERROR: Failed to initialize power measurement GPIO\n");
    return -1;
  }
#endif

  return 0;
}

extern "C" int tflm_run_inference(const void *input_data,
                                  size_t input_size,
                                  void *output_data,
                                  size_t output_size) {
  if (interpreter == nullptr) {
    printf("  [TFLM] ERROR: tflm_init() has not been called\n");
    return -1;
  }

  TfLiteTensor* input = interpreter->input(0);
  TfLiteTensor* output = interpreter->output(0);

  if (input == nullptr || output == nullptr) {
    printf("  [TFLM] ERROR: Failed to get input/output tensors\n");
    return -1;
  }

  printf("  [TFLM] Input:  type=%d, bytes=%u\n",
         input->type, (unsigned)input->bytes);
  printf("  [TFLM] Output: type=%d, bytes=%u\n",
         output->type, (unsigned)output->bytes);
  printf("  [TFLM] Input  quant: scale=%.6f, zero_point=%d\n",
         (double)input->params.scale, input->params.zero_point);
  printf("  [TFLM] Output quant: scale=%.6f, zero_point=%d\n",
         (double)output->params.scale, output->params.zero_point);

  printf("  [TFLM] Input  data @ %p\n", (void*)input->data.data);
  printf("  [TFLM] Output data @ %p\n", (void*)output->data.data);

  if (input_data) {
    if (input_size != input->bytes) {
      printf("  [TFLM] ERROR: Input size %u does not match tensor size %u\n",
             (unsigned)input_size, (unsigned)input->bytes);
      return -1;
    }
    memcpy(input->data.raw, input_data, input->bytes);
  }

#if defined(NPU_PMU)
  npu_pmu_reset();
#endif
#if defined(POWER_GPIO)
  power_gpio_high();
#endif
  uint32_t inference_time = ARM_PMU_Get_CCNTR();
  TfLiteStatus invoke_status = interpreter->Invoke();
  inference_time = ARM_PMU_Get_CCNTR() - inference_time;
#if defined(POWER_GPIO)
  power_gpio_low();
#endif
  if (invoke_status != kTfLiteOk) {
    printf("  [TFLM] ERROR: Invoke() failed\n");
    return -1;
  }

  if (output_data) {
    if (output_size != output->bytes) {
      printf("  [TFLM] ERROR: Output size %u does not match tensor size %u\n",
             (unsigned)output_size, (unsigned)output->bytes);
      return -1;
    }
    memcpy(output_data, output->data.raw, output->bytes);
  }

  printf("  [TFLM] Inference complete\n");
  printf("  [TFLM] Inference time: %.3fms\n", inference_time * 1000.0f / SystemCoreClock);
#if defined(NPU_PMU)
  npu_pmu_print();
#endif
  return 0;
}
