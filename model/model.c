/* Selects one flatbuffer and exposes it to the application.
 *
 * Define exactly one of:
 *   MODEL_YOLO_U55_128       yolo-fastest_192_face_v4_vela_U55_128.c
 *   MODEL_YOLO_U55_256       yolo-fastest_192_face_v4_vela_U55_256.c
 *   MODEL_YOLO_U85_256       yolo-fastest_192_face_v4_vela_U85_256.c
 *   MODEL_YOLO_U85_256_SRAM  yolo-fastest_192_face_v4_vela_U85_256_SRAM.c
 *   MODEL_YOLO_U85_256_OSPI  yolo-fastest_192_face_v4_vela_U85_256_OSPI.c
 *   MODEL_YOLO_CPU           yolo-fastest_192_face_v4.c
 *   MODEL_KWS_U55_128        kws_micronet_m_vela_U55_128.c
 *   MODEL_KWS_U55_256        kws_micronet_m_vela_U55_256.c
 *   MODEL_KWS_CPU            kws_micronet_m.c
 */

#include <string.h>

#include "model.h"

#if defined(MODEL_YOLO_U55_128)
#include "yolo-fastest_192_face_v4_vela_U55_128.c"
#elif defined(MODEL_YOLO_U55_256)
#include "yolo-fastest_192_face_v4_vela_U55_256.c"
#elif defined(MODEL_YOLO_U85_256_SRAM)
#include "yolo-fastest_192_face_v4_vela_U85_256_SRAM.c"
#elif defined(MODEL_YOLO_U85_256_OSPI)
#include "yolo-fastest_192_face_v4_vela_U85_256_OSPI.c"
#elif defined(MODEL_YOLO_U85_256)
#include "yolo-fastest_192_face_v4_vela_U85_256.c"
#elif defined(MODEL_YOLO_CPU)
#include "yolo-fastest_192_face_v4.c"
#elif defined(MODEL_KWS_U55_128)
#include "kws_micronet_m_vela_U55_128.c"
#elif defined(MODEL_KWS_U55_256)
#include "kws_micronet_m_vela_U55_256.c"
#elif defined(MODEL_KWS_CPU)
#include "kws_micronet_m.c"
#else
#error "Define one MODEL_YOLO_* or MODEL_KWS_* macro to select a model"
#endif

#if defined(MODEL_YOLO_U85_256_SRAM)
/* SRAM copy of the flatbuffer. sizeof(model_tflite) is a constant here,
 * in the translation unit that defines the array. A static array in main.c
 * cannot be sized with get_model_tflite_size(). */
static uint8_t model_data_in_sram[sizeof(model_tflite)]
    __attribute__((aligned(16), section(".bss.model_sram")));
#endif

#if defined(MODEL_YOLO_U85_256_OSPI)
static uint8_t model_data_in_psram[sizeof(model_tflite)]
    __attribute__((aligned(16), section(".bss.model_psram")));
#endif

const uint8_t *get_model_tflite(void)
{
#if defined(MODEL_YOLO_U85_256_SRAM)
    memcpy(model_data_in_sram, model_tflite, sizeof(model_tflite));
    return model_data_in_sram;
#elif defined(MODEL_YOLO_U85_256_OSPI)
    memcpy(model_data_in_psram, model_tflite, sizeof(model_tflite));
    return model_data_in_psram;
#else
    return model_tflite;
#endif
}

const size_t get_model_tflite_size(void)
{
    return model_tflite_len;
}
