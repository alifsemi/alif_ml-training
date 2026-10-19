/* Compile-time selection of the TFLite model. */
#ifndef MODEL_H
#define MODEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TFLite flatbuffer selected by the MODEL_YOLO_* or MODEL_KWS_* define.
 *        The buffer stays valid for the life of the program.
 *        MODEL_YOLO_U85_256_SRAM is copied into SRAM (.bss.model_sram)
 *        before the pointer is returned.
 * @return Pointer to the model bytes
 */
const uint8_t *get_model_tflite(void);

/**
 * @brief Size of the buffer returned by get_model_tflite.
 * @return Size in bytes
 */
const size_t get_model_tflite_size(void);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_H */
