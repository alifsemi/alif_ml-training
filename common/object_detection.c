/* Pre and post processing for the YOLO-fastest 192x192 face detection model */
#include <math.h>

#include "object_detection.h"

#define NUM_CLASSES        1                    /* Face */
#define NUM_ANCHORS        3                    /* Boxes predicted per grid cell */
#define VALUES_PER_ANCHOR  (5 + NUM_CLASSES)    /* x, y, w, h, objectness, class scores */

/* Anchor box sizes (width, height) in input pixels, estimated during training.
 * The coarse 6x6 grid detects large faces, the 12x12 grid small ones. */
static const float anchors[2][NUM_ANCHORS * 2] = {
  {38, 77, 47, 97, 61, 126},  /* 6x6 branch */
  {14, 26, 19, 37, 28, 55},   /* 12x12 branch */
};

/* Worst case: every anchor of both branches passes the threshold */
#define MAX_CANDIDATES  ((6 * 6 + 12 * 12) * NUM_ANCHORS)

/* Box candidate before NMS, center format in input pixels */
typedef struct {
  float cx, cy, w, h;
  float score;
} candidate_t;

static candidate_t candidates[MAX_CANDIDATES];

/* ------------------------------------------------------------------------- */
/* Pre processing                                                            */
/*                                                                           */
/* Camera image (RGB888, 3 x uint8 per pixel) -> model input tensor          */
/* (1 x INT8 per pixel), done in a single pass per pixel:                    */
/*  1. Color conversion: the model was trained on grayscale images, so       */
/*     RGB is reduced to luminance Y = 0.299 R + 0.587 G + 0.114 B           */
/*  2. Quantization: the real input value is pixel / 255 (0.0..1.0), which   */
/*     the INT8 tensor stores as q = real / scale + zero_point with          */
/*     scale = 1/255 and zero_point = -128, i.e. simply q = pixel - 128      */
/* No resize is needed because the image is already 192x192.                 */
/* ------------------------------------------------------------------------- */

void od_pre_process(const uint8_t *rgb, int8_t *input, size_t num_pixels)
{
  for (size_t i = 0; i < num_pixels; i++) {
    uint32_t r = rgb[3 * i + 0];
    uint32_t g = rgb[3 * i + 1];
    uint32_t b = rgb[3 * i + 2];

    /* Grayscale with integer weights for 0.299 R + 0.587 G + 0.114 B (sum = 256) */
    uint32_t gray = (77 * r + 150 * g + 29 * b) >> 8;

    /* Input quantization is scale = 1/255, zero_point = -128, so the
     * pixel value 0..255 maps to -128..127 */
    input[i] = (int8_t)((int32_t)gray - 128);
  }
}

/* ------------------------------------------------------------------------- */
/* Post processing                                                           */
/*                                                                           */
/* YOLO output tensors -> list of face bounding boxes:                       */
/*  1. Dequantization: INT8 outputs back to real values,                     */
/*     real = (q - zero_point) * scale                                       */
/*  2. Decoding: each grid cell and anchor predicts 6 raw values             */
/*     (tx, ty, tw, th, objectness, class score):                            */
/*      - sigmoid() squashes objectness and class score to 0..1              */
/*        probabilities, score = objectness * class probability              */
/*      - sigmoid(tx/ty) is the box center offset inside its grid cell       */
/*      - exp(tw/th) scales the anchor box width/height                      */
/*  3. Thresholding: boxes with score <= OD_SCORE_THRESHOLD are dropped      */
/*  4. Non-maximum suppression (NMS): the same face is found by several      */
/*     cells/anchors, so boxes are sorted by score and any box overlapping   */
/*     a better one by IoU > OD_NMS_THRESHOLD is removed                     */
/*  5. Conversion: center format to top left corner + size, clipped to       */
/*     the image                                                             */
/* ------------------------------------------------------------------------- */

static float sigmoid(float x)
{
  return 1.0f / (1.0f + expf(-x));
}

static float dequantize(const od_output_t *out, int index)
{
  return ((float)out->data[index] - (float)out->zero_point) * out->scale;
}

/* Decode one output branch and append candidates above the score threshold */
static int decode_branch(const od_output_t *out, const float *anchor, int count)
{
  const int grid = out->grid_size;
  const float cell_size = (float)OD_IMAGE_WIDTH / (float)grid;

  for (int gy = 0; gy < grid; gy++) {
    for (int gx = 0; gx < grid; gx++) {
      for (int a = 0; a < NUM_ANCHORS; a++) {
        int base = ((gy * grid + gx) * NUM_ANCHORS + a) * VALUES_PER_ANCHOR;

        float objectness = sigmoid(dequantize(out, base + 4));
        if (objectness <= OD_SCORE_THRESHOLD) {
          continue;
        }
        float score = sigmoid(dequantize(out, base + 5)) * objectness;
        if (score <= OD_SCORE_THRESHOLD) {
          continue;
        }

        candidate_t *c = &candidates[count++];
        /* Box center is an offset (0..1) inside the grid cell */
        c->cx = (sigmoid(dequantize(out, base + 0)) + (float)gx) * cell_size;
        c->cy = (sigmoid(dequantize(out, base + 1)) + (float)gy) * cell_size;
        /* Box size is a scale factor of the anchor box */
        c->w = expf(dequantize(out, base + 2)) * anchor[2 * a + 0];
        c->h = expf(dequantize(out, base + 3)) * anchor[2 * a + 1];
        c->score = score;
      }
    }
  }
  return count;
}

/* Intersection over union of two center format boxes */
static float iou(const candidate_t *a, const candidate_t *b)
{
  float left   = fmaxf(a->cx - a->w / 2, b->cx - b->w / 2);
  float right  = fminf(a->cx + a->w / 2, b->cx + b->w / 2);
  float top    = fmaxf(a->cy - a->h / 2, b->cy - b->h / 2);
  float bottom = fminf(a->cy + a->h / 2, b->cy + b->h / 2);

  if (right <= left || bottom <= top) {
    return 0.0f;
  }
  float intersection = (right - left) * (bottom - top);
  float union_area = a->w * a->h + b->w * b->h - intersection;
  return intersection / union_area;
}

/* Sort candidates by score, highest first (insertion sort, the list is short) */
static void sort_by_score(candidate_t *list, int count)
{
  for (int i = 1; i < count; i++) {
    candidate_t key = list[i];
    int j = i - 1;
    while (j >= 0 && list[j].score < key.score) {
      list[j + 1] = list[j];
      j--;
    }
    list[j + 1] = key;
  }
}

static int clamp(float value, int max)
{
  if (value < 0.0f) {
    return 0;
  }
  if (value > (float)max) {
    return max;
  }
  return (int)value;
}

int od_post_process(const od_output_t outputs[2], od_box_t *boxes, int max_boxes)
{
  /* 1. Decode both branches into box candidates */
  int count = 0;
  count = decode_branch(&outputs[0], anchors[0], count);
  count = decode_branch(&outputs[1], anchors[1], count);

  /* 2. Non-maximum suppression: keep the best box, drop boxes overlapping it */
  sort_by_score(candidates, count);
  for (int i = 0; i < count; i++) {
    if (candidates[i].score == 0.0f) {
      continue;
    }
    for (int j = i + 1; j < count; j++) {
      if (candidates[j].score != 0.0f && iou(&candidates[i], &candidates[j]) > OD_NMS_THRESHOLD) {
        candidates[j].score = 0.0f;
      }
    }
  }

  /* 3. Convert the remaining boxes to top left corner + size, clipped to the image */
  int num_boxes = 0;
  for (int i = 0; i < count && num_boxes < max_boxes; i++) {
    const candidate_t *c = &candidates[i];
    if (c->score == 0.0f) {
      continue;
    }
    int x0 = clamp(c->cx - c->w / 2, OD_IMAGE_WIDTH);
    int y0 = clamp(c->cy - c->h / 2, OD_IMAGE_HEIGHT);
    int x1 = clamp(c->cx + c->w / 2, OD_IMAGE_WIDTH);
    int y1 = clamp(c->cy + c->h / 2, OD_IMAGE_HEIGHT);

    boxes[num_boxes].x = x0;
    boxes[num_boxes].y = y0;
    boxes[num_boxes].w = x1 - x0;
    boxes[num_boxes].h = y1 - y0;
    boxes[num_boxes].score = c->score;
    num_boxes++;
  }
  return num_boxes;
}
