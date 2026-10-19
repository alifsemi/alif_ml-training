/* Test image for the object detection pipeline */
#ifndef SAMPLE_IMAGE_H
#define SAMPLE_IMAGE_H

#include <stdint.h>

#define SAMPLE_IMAGE_WIDTH  192
#define SAMPLE_IMAGE_HEIGHT 192

/* RGB888, row by row from top to bottom (see misc/samples/bmp_to_c.py) */
extern const uint8_t sample_image[SAMPLE_IMAGE_WIDTH * SAMPLE_IMAGE_HEIGHT * 3];

#endif /* SAMPLE_IMAGE_H */
