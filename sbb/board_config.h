#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            Partition scheme must have at least 3MB APP space.
//
// Camera model is selected via build_flags in platformio.ini:
//   -DCAMERA_MODEL_ESP32S3_EYE   (Freenove ESP32-S3 WROOM default)
//
// Available models (set exactly one in platformio.ini):
//   CAMERA_MODEL_WROVER_KIT          Has PSRAM
//   CAMERA_MODEL_ESP_EYE             Has PSRAM
//   CAMERA_MODEL_ESP32S3_EYE         Has PSRAM  <-- Freenove ESP32-S3 WROOM
//   CAMERA_MODEL_M5STACK_PSRAM       Has PSRAM
//   CAMERA_MODEL_M5STACK_V2_PSRAM    Has PSRAM
//   CAMERA_MODEL_M5STACK_WIDE        Has PSRAM
//   CAMERA_MODEL_M5STACK_ESP32CAM    No PSRAM
//   CAMERA_MODEL_M5STACK_UNITCAM     No PSRAM
//   CAMERA_MODEL_M5STACK_CAMS3_UNIT  Has PSRAM
//   CAMERA_MODEL_AI_THINKER          Has PSRAM
//   CAMERA_MODEL_TTGO_T_JOURNAL      No PSRAM
//   CAMERA_MODEL_XIAO_ESP32S3        Has PSRAM
//   CAMERA_MODEL_ESP32_CAM_BOARD
//   CAMERA_MODEL_ESP32S2_CAM_BOARD
//   CAMERA_MODEL_ESP32S3_CAM_LCD
//   CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3  Has PSRAM
//   CAMERA_MODEL_DFRobot_Romeo_ESP32S3        Has PSRAM

#include "camera_pins.h"

// ── SD_MMC pin assignment per board ───────────────────────────────────────
#if defined(CAMERA_MODEL_ESP32S3_EYE)
  constexpr int SD_MMC_CMD = 38;
  constexpr int SD_MMC_CLK = 39;
  constexpr int SD_MMC_D0  = 40;
#elif defined(CAMERA_MODEL_ESP32_CAM_BOARD) || defined(CAMERA_MODEL_AI_THINKER)
  constexpr int SD_MMC_CMD = 15;
  constexpr int SD_MMC_CLK = 14;
  constexpr int SD_MMC_D0  =  2;
#else
  #error "No SD_MMC pin mapping defined for this camera model. Add it to sbb/board_config.h"
#endif

#endif  // BOARD_CONFIG_H
