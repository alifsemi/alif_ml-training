/* OV5675 camera + ISP capture
 *
 * Data path:
 *   OV5675 sensor (RAW10 Bayer, 1296x972)
 *     -> MIPI CSI-2 receiver -> CPI -> ISP
 *        (black level, demosaic, auto exposure, color correction, gamma,
 *         square crop 972x972 and scaling to CAMERA_WIDTH x CAMERA_HEIGHT)
 *     -> RGB888 planar frame in SRAM (written by the ISP DMA)
 */
#include <stdio.h>

#include "RTE_Components.h"
#include CMSIS_device_header

#include "Driver_CPI.h"
#include "Driver_ISP.h"
#include "isp_param.h"
#include "vsi_comm_video.h"
#include "services_lib_api.h"

#include "camera.h"
#include "power.h"

#if RTE_CPI_AXI_PORT
#error "RTE_CPI_AXI_PORT must be disabled: the ISP writes the frame, the raw CPI buffer is not needed"
#endif

#define PLANE_SIZE  (CAMERA_WIDTH * CAMERA_HEIGHT)

/* ISP output frame: three planes R, G, B. The ISP writes this buffer with DMA,
 * so it must be in SRAM (global address), not in the CPU local TCM. */
static uint8_t isp_frame[3 * PLANE_SIZE] __attribute__((aligned(32), section(".bss.camera_frame_buf")));
static VIDEO_BUF_S isp_buffer;

extern ARM_DRIVER_CPI Driver_CPI;
static ARM_DRIVER_CPI *const camera = &Driver_CPI;

/* Events set from the camera/ISP interrupt callback */
#define EVENT_FRAME_DONE  (1U << 0)
#define EVENT_ERROR       (1U << 1)
static volatile uint32_t camera_events;

static void camera_callback(uint32_t event)
{
  switch (event) {
    case ARM_ISP_MI_EVENT_MP_FRAME_END_DETECTED:  /* ISP has written the full frame */
    case ARM_ISP_MI_EVENT_FILL_MP_Y_DETECTED:
    case ARM_ISP_MI_EVENT_MP_Y_WRAP_DETECTED:
      camera_events |= EVENT_FRAME_DONE;
      break;
    case ARM_CPI_EVENT_ERR_HARDWARE:
    case ARM_CPI_EVENT_MIPI_CSI2_ERROR:
    case ARM_CPI_EVENT_ERR_CAMERA_INPUT_FIFO_OVERRUN:
    case ARM_CPI_EVENT_ERR_CAMERA_OUTPUT_FIFO_OVERRUN:
      camera_events |= EVENT_ERROR;
      break;
    default:
      break;
  }
}

/* ISP configuration on top of the pack defaults (Alif_CMSIS/Source/ov5675_isp_param.c).
 * Must be done before the ISP is initialized. */
static void isp_configure(void)
{
  const uint32_t sensor_w = RTE_OV5675_CAMERA_SENSOR_FRAME_WIDTH;
  const uint32_t sensor_h = RTE_OV5675_CAMERA_SENSOR_FRAME_HEIGHT;

  /* OV5675 Bayer pattern as seen by the ISP (the pack default GBRG gives wrong colors) */
  port_attr.pixelFormat = PIXEL_FORMAT_GRBG10;

  /* Centered square crop, so that scaling to the square model input keeps the aspect ratio */
  uint32_t side = sensor_w < sensor_h ? sensor_w : sensor_h;
  isp_param_set_crop((sensor_h - side) / 2, (sensor_w - side) / 2, side, side);

  /* Auto exposure measures the cropped image */
  calibration_data.modules.aem.blockWin.hOffs = 0;
  calibration_data.modules.aem.blockWin.vOffs = 0;
  calibration_data.modules.aem.blockWin.hSize = side;
  calibration_data.modules.aem.blockWin.vSize = side;

  /* Output buffer: one RGB888 planar frame */
  isp_buffer.index = 0;
  isp_buffer.numPlanes = 3;
  isp_buffer.imageSize = sizeof(isp_frame);
  isp_buffer.planes[0].dmaPhyAddr = (vsi_dma_t)&isp_frame[0 * PLANE_SIZE];  /* R */
  isp_buffer.planes[1].dmaPhyAddr = (vsi_dma_t)&isp_frame[1 * PLANE_SIZE];  /* G */
  isp_buffer.planes[2].dmaPhyAddr = (vsi_dma_t)&isp_frame[2 * PLANE_SIZE];  /* B */
}

int camera_init(void)
{
  int ret;

  /* Camera and MIPI CSI-2 clocks, MIPI receiver PHY power */
  if (power_enable(CAMERA_MASK | MIPI_CSI_MASK,
                   LDO_PHY_MASK | MIPI_RX_DPHY_MASK | MIPI_PLL_DPHY_MASK) != 0) {
    return -1;
  }

  isp_configure();

  ret = camera->Initialize(camera_callback);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: Initialize failed (%d)\n", ret);
    return ret;
  }

  ret = camera->PowerControl(ARM_POWER_FULL);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: PowerControl failed (%d)\n", ret);
    return ret;
  }

  /* CPI + MIPI CSI-2 receiver */
  ret = camera->Control(CPI_CONFIGURE, 0);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: CPI configuration failed (%d)\n", ret);
    return ret;
  }

  /* OV5675 sensor registers over I2C */
  ret = camera->Control(CPI_CAMERA_SENSOR_CONFIGURE, 0);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: Sensor configuration failed (%d)\n", ret);
    return ret;
  }

  ret = camera->Control(CPI_EVENTS_CONFIGURE,
                        ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED |
                        ARM_CPI_EVENT_ERR_CAMERA_INPUT_FIFO_OVERRUN |
                        ARM_CPI_EVENT_ERR_CAMERA_OUTPUT_FIFO_OVERRUN |
                        ARM_CPI_EVENT_ERR_HARDWARE);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: Event configuration failed (%d)\n", ret);
    return ret;
  }

  /* Give the output buffer to the ISP */
  ret = camera->Control(ISP_CONTROL_QBUF, (uint32_t)&isp_buffer);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: ISP buffer configuration failed (%d)\n", ret);
    return ret;
  }

#if RTE_ISP_EXPM_MODULE
  /* The driver offsets the AE measurement window by the crop offset, but the
   * measurement runs on the already cropped image. Reset the offsets to zero. */
  static struct isp_params meas_params;
  meas_params.valid_mask = ISP_PARAM_MASK_AEM;
  ret = camera->Control(ISP_CONTROL_GET_PARAM, (uint32_t)&meas_params);
  if (ret == ARM_DRIVER_OK) {
    meas_params.aem.h_offs = 0;
    meas_params.aem.v_offs = 0;
    ret = camera->Control(ISP_CONTROL_SET_PARAM, (uint32_t)&meas_params);
  }
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: ISP measurement window configuration failed (%d)\n", ret);
    return ret;
  }
#endif

  printf("  [CAM] OV5675 %dx%d -> ISP %dx%d RGB888 planar\n",
         RTE_OV5675_CAMERA_SENSOR_FRAME_WIDTH, RTE_OV5675_CAMERA_SENSOR_FRAME_HEIGHT,
         CAMERA_WIDTH, CAMERA_HEIGHT);
  return 0;
}

#if RTE_ISP_AE_MODULE
/* The ISP auto exposure computes new exposure and gain values every frame,
 * but they have to be written to the sensor by the application. */
static int camera_update_exposure(void)
{
  static uint32_t prev_int_line;
  static uint32_t prev_gain;
  struct isp_ae_cached_values ae = {0};

  int ret = camera->Control(ISP_CONTROL_AE_GET_CACHED, (uint32_t)&ae);
  if (ret != ARM_DRIVER_OK || ae.int_line == 0) {
    return ARM_DRIVER_OK;
  }

  uint32_t gain_q16_16 = (ae.again * ae.dgain) / 16;
  if (ae.int_line == prev_int_line && gain_q16_16 == prev_gain) {
    return ARM_DRIVER_OK;
  }

  ret = camera->Control(CPI_ISP_CAMERA_SENSOR_EXPOSURE, ae.int_line);
  if (ret == ARM_DRIVER_OK) {
    ret = camera->Control(CPI_ISP_CAMERA_SENSOR_GAIN, gain_q16_16);
  }
  prev_int_line = ae.int_line;
  prev_gain = gain_q16_16;
  return ret;
}
#endif

const uint8_t *camera_capture(void)
{
  camera_events = 0;

  /* Frame goes CPI -> ISP -> isp_frame, so the CPI buffer address is not used */
  int ret = camera->CaptureFrame((uint8_t *)0xABCDABCD);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: CaptureFrame failed (%d)\n", ret);
    return NULL;
  }

  while (!(camera_events & (EVENT_FRAME_DONE | EVENT_ERROR))) {
    __WFI();
  }

  ret = camera->Control(ISP_PROCESS_FRAME_END, 0);
  if (ret != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: ISP frame end processing failed (%d)\n", ret);
    return NULL;
  }

#if RTE_ISP_AE_MODULE
  if (camera_update_exposure() != ARM_DRIVER_OK) {
    printf("  [CAM] ERROR: Exposure update failed\n");
    return NULL;
  }
#endif

  if (camera_events & EVENT_ERROR) {
    printf("  [CAM] ERROR: Capture error event\n");
    return NULL;
  }

  /* The ISP wrote the frame with DMA: drop stale CPU cache lines before reading it */
  RTSS_InvalidateDCache_by_Addr(isp_frame, sizeof(isp_frame));
  return isp_frame;
}
