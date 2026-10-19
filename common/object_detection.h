/* Pre and post processing for the YOLO-fastest 192x192 face detection model
 *
 * Plain C port of the Arm ML Embedded Evaluation Kit object_detection use case:
 * https://github.com/alifsemi/alif_ml-embedded-evaluation-kit/tree/a5156b79936bec74f7e2dfe055a5c1a216e44dd6/source/lib/mlek/use_case/object_detection
 */
#ifndef OBJECT_DETECTION_H
#define OBJECT_DETECTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Model input: 192x192 grayscale, INT8 */
#define OD_IMAGE_WIDTH   192
#define OD_IMAGE_HEIGHT  192

/* Post processing parameters (same defaults as the reference) */
#define OD_SCORE_THRESHOLD  0.5f   /* Minimum objectness and class score */
#define OD_NMS_THRESHOLD    0.45f  /* Boxes overlapping more than this (IoU) are merged */

/* One YOLO output branch (model output tensor), shape [1, grid, grid, 3 * (5 + classes)] */
typedef struct {
  const int8_t *data;
  int grid_size;     /* 6 for the stride-32 branch, 12 for the stride-16 branch */
  float scale;       /* Output tensor quantization */
  int zero_point;
} od_output_t;

/* Detected object in input image pixel coordinates */
typedef struct {
  int x;             /* Top left corner */
  int y;
  int w;
  int h;
  float score;       /* Confidence 0..1 */
} od_box_t;

/**
 * @brief Convert an RGB888 camera image to the model input tensor format.
 *        RGB -> grayscale -> INT8 (pixel - 128).
 * @param rgb         Source image, 3 bytes per pixel (R, G, B)
 * @param input       Model input tensor data
 * @param num_pixels  Number of pixels (width * height), must equal input tensor size
 */
void od_pre_process(const uint8_t *rgb, int8_t *input, size_t num_pixels);

/**
 * @brief Decode the two YOLO output branches into bounding boxes.
 *        Sigmoid/exp decoding with anchors -> score threshold -> non-maximum suppression.
 * @param outputs    The two model outputs: [0] = 6x6 branch, [1] = 12x12 branch
 * @param boxes      Array that receives the detections, sorted by score
 * @param max_boxes  Size of the boxes array
 * @return Number of detections written to boxes
 */
int od_post_process(const od_output_t outputs[2], od_box_t *boxes, int max_boxes);

#ifdef __cplusplus
}
#endif

#endif /* OBJECT_DETECTION_H */
