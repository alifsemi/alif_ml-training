/* TFLM + Ethos-U55 inference runner */
#ifndef TFLM_INFERENCE_H
#define TFLM_INFERENCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize TFLM for a model.
 *        Loads the flatbuffer, registers Ethos-U and FullyConnected,
 *        creates the interpreter, and allocates tensors.
 *        model_data must stay valid until the next tflm_init call.
 * @param model_data  TFLite flatbuffer (for example a Vela-compiled model)

 * @return 0 on success, -1 on failure
 */
int tflm_init(const uint8_t *model_data);

/**
 * @brief Run one inference on the model passed to tflm_init.
 *        Copies input_data into the input tensor, invokes the interpreter,
 *        and writes the output tensor into output_data.
 * @param input_data   Bytes copied into the input tensor
 * @param input_size   Size of input_data in bytes; must match the input tensor
 * @param output_data  Buffer that receives the output tensor bytes
 * @param output_size  Size of output_data in bytes; must match the output tensor
 * @return 0 on success, -1 on failure
 */
int tflm_run_inference(const void *input_data,
                       size_t input_size,
                       void *output_data,
                       size_t output_size);

/* Model tensor in the tensor arena, NHWC layout */
typedef struct {
  void *data;
  size_t bytes;
  int height;
  int width;
  int channels;
  float scale;       /* Quantization: real = (value - zero_point) * scale */
  int zero_point;
} tflm_tensor_t;

/**
 * @brief Get a model input tensor. Writing to tensor->data fills the input
 *        in place, so tflm_run_inference can be called with input_data = NULL.
 * @param index   Input index
 * @param tensor  Receives the tensor info
 * @return 0 on success, -1 on failure
 */
int tflm_get_input(int index, tflm_tensor_t *tensor);

/**
 * @brief Get a model output tensor. Valid after tflm_run_inference until the
 *        next inference.
 * @param index   Output index
 * @param tensor  Receives the tensor info
 * @return 0 on success, -1 on failure
 */
int tflm_get_output(int index, tflm_tensor_t *tensor);

#ifdef __cplusplus
}
#endif

#endif /* TFLM_INFERENCE_H */
