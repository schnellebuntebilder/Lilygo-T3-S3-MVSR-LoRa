/**********************************************************************
  Filename    : Camera SD Snapshot
  Description : Captures grayscale frames, stores JPG and dithered 1-bit BMP
                on SD, and serves them from a Wi-Fi access point.
**********************************************************************/
#include <Arduino.h>
#include "esp_camera.h"
#include "img_converters.h"
#include <WiFi.h>
#include <WebServer.h>
#include "camera_pins.h"
#include "FS.h"
#include "SD_MMC.h"
#include <stdlib.h>

namespace {

constexpr int SD_MMC_CMD = 38;
constexpr int SD_MMC_CLK = 39;
constexpr int SD_MMC_D0 = 40;
constexpr char AP_SSID[] = "ESPCAM";
constexpr char AP_PASSWORD[] = "nullbisneun";
constexpr char FULL_IMAGE_PATH[] = "/capture.jpg";
constexpr char THUMB_IMAGE_PATH[] = "/capture.bmp";
constexpr int TARGET_WIDTH = 128;
constexpr int TARGET_HEIGHT = 64;
constexpr unsigned long CAPTURE_INTERVAL_MS = 1000;

WebServer server(80);

void captureAndSave();

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 5;
  config.fb_count = 2;

  if (psramFound()) {
    config.jpeg_quality = 5;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  sensor->set_vflip(sensor, 1);
  sensor->set_brightness(sensor, 1);
  sensor->set_saturation(sensor, 0);
  return true;
}

bool initSdCard() {
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
    Serial.println("SD_MMC mount failed");
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No SD_MMC card attached");
    return false;
  }

  return true;
}

void sendFileFromSd(const char *path, const char *contentType) {
  if (!SD_MMC.exists(path)) {
    server.send(404, "text/plain", "File not found yet. Wait for the next capture cycle.");
    return;
  }

  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }

  server.streamFile(file, contentType);
  file.close();
}

void handleRoot() {
  String html;
  html.reserve(2048);
  html += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESPCAM</title></head><body style='font-family:sans-serif;max-width:760px;margin:16px auto;padding:0 12px'>";
  html += "<h2>ESPCAM</h2>";
  html += "<p><a href='/capture.jpg'>capture.jpg</a></p>";
  html += "<p><a href='/capture.bmp'>capture.bmp</a></p>";
  html += "<p><a href='/capture'>Capture now</a></p>";
  html += "<p id='status'>Auto refresh active</p>";
  html += "<h3>capture.jpg</h3>";
  html += "<img id='jpg' src='/capture.jpg' style='display:block;max-width:100%;height:auto;border:1px solid #999;margin-bottom:16px'>";
  html += "<h3>capture.bmp</h3>";
  html += "<img id='bmp' src='/capture.bmp' style='display:block;max-width:100%;height:auto;border:1px solid #999'>";
  html += "<script>";
  html += "const jpg=document.getElementById('jpg');";
  html += "const bmp=document.getElementById('bmp');";
  html += "const status=document.getElementById('status');";
  html += "function refreshImages(){const stamp=Date.now();jpg.src='/capture.jpg?t='+stamp;bmp.src='/capture.bmp?t='+stamp;status.textContent='Updated: '+new Date().toLocaleTimeString();}";
  html += "setInterval(refreshImages, 1000);";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void initWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/cam.html", HTTP_GET, handleRoot);
  server.on("/capture.jpg", HTTP_GET, []() {
    sendFileFromSd(FULL_IMAGE_PATH, "image/jpeg");
  });
  server.on("/capture.bmp", HTTP_GET, []() {
    sendFileFromSd(THUMB_IMAGE_PATH, "image/bmp");
  });
  server.on("/capture", HTTP_GET, []() {
    captureAndSave();
    server.sendHeader("Location", "/cam.html", true);
    server.send(302, "text/plain", "Captured");
  });
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
}

bool initAccessPoint() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Failed to start WiFi AP");
    return false;
  }

  const IPAddress ip = WiFi.softAPIP();
  Serial.printf("AP started. SSID: %s\n", AP_SSID);
  Serial.printf("Open: http://%u.%u.%u.%u/\n", ip[0], ip[1], ip[2], ip[3]);
  return true;
}

bool writeJpegFromGray(const char *path, uint8_t *buf, int width, int height) {
  uint8_t *jpgBuf = nullptr;
  size_t jpgLen = 0;

  if (!fmt2jpg(buf, static_cast<size_t>(width) * static_cast<size_t>(height), width, height,
               PIXFORMAT_GRAYSCALE, 90, &jpgBuf, &jpgLen)) {
    Serial.println("JPEG conversion failed");
    return false;
  }

  if (SD_MMC.exists(path)) {
    SD_MMC.remove(path);
  }

  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open %s\n", path);
    free(jpgBuf);
    return false;
  }

  const size_t written = file.write(jpgBuf, jpgLen);
  file.close();
  free(jpgBuf);

  if (written != jpgLen) {
    Serial.printf("Write incomplete for %s\n", path);
    return false;
  }

  return true;
}

uint8_t computeAverageLuminance(const uint8_t *buf, int width, int height) {
  const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
  uint32_t sum = 0;

  for (size_t index = 0; index < pixelCount; index++) {
    sum += buf[index];
  }

  return static_cast<uint8_t>(sum / pixelCount);
}

bool ditherToPackedBitmap(const uint8_t *buf, int width, int height, uint8_t exposureCenter,
                          uint8_t *packed, int rowStride) {
  int16_t *currentError = static_cast<int16_t *>(calloc(static_cast<size_t>(width + 2), sizeof(int16_t)));
  int16_t *nextError = static_cast<int16_t *>(calloc(static_cast<size_t>(width + 2), sizeof(int16_t)));
  if (!currentError || !nextError) {
    Serial.println("Not enough memory for dithering buffers");
    free(currentError);
    free(nextError);
    return false;
  }

  memset(packed, 0, static_cast<size_t>(rowStride) * static_cast<size_t>(height));
  const int exposureBias = 128 - static_cast<int>(exposureCenter);

  for (int y = 0; y < height; y++) {
    memset(nextError, 0, static_cast<size_t>(width + 2) * sizeof(int16_t));
    const size_t rowOffset = static_cast<size_t>(height - 1 - y) * static_cast<size_t>(rowStride);

    for (int x = 0; x < width; x++) {
      int value = static_cast<int>(buf[y * width + x]) + exposureBias + currentError[x + 1];
      value = constrain(value, 0, 255);

      const int quantized = (value >= 128) ? 255 : 0;
      if (quantized != 0) {
        packed[rowOffset + static_cast<size_t>(x / 8)] |= static_cast<uint8_t>(0x80 >> (x % 8));
      }

      const int error = value - quantized;
      const int err7 = (error * 7) / 16;
      const int err3 = (error * 3) / 16;
      const int err5 = (error * 5) / 16;
      const int err1 = error - err7 - err3 - err5;

      currentError[x + 2] = static_cast<int16_t>(currentError[x + 2] + err7);
      nextError[x] = static_cast<int16_t>(nextError[x] + err3);
      nextError[x + 1] = static_cast<int16_t>(nextError[x + 1] + err5);
      nextError[x + 2] = static_cast<int16_t>(nextError[x + 2] + err1);
    }

    int16_t *temp = currentError;
    currentError = nextError;
    nextError = temp;
  }

  free(currentError);
  free(nextError);
  return true;
}

bool writeBmp1Bit(const char *path, const uint8_t *buf, int width, int height, uint8_t exposureCenter) {
  const int rowBytesPacked = (width + 7) / 8;
  const int rowStride = (rowBytesPacked + 3) & ~3;
  const uint32_t pixelDataSize = static_cast<uint32_t>(rowStride * height);
  const uint32_t headerSize = 14 + 40 + 8;
  const uint32_t fileSize = headerSize + pixelDataSize;

  if (SD_MMC.exists(path)) {
    SD_MMC.remove(path);
  }

  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open %s\n", path);
    return false;
  }

  uint8_t header[62] = {0};
  header[0] = 'B';
  header[1] = 'M';
  header[2] = static_cast<uint8_t>(fileSize & 0xFF);
  header[3] = static_cast<uint8_t>((fileSize >> 8) & 0xFF);
  header[4] = static_cast<uint8_t>((fileSize >> 16) & 0xFF);
  header[5] = static_cast<uint8_t>((fileSize >> 24) & 0xFF);
  header[10] = static_cast<uint8_t>(headerSize & 0xFF);

  header[14] = 40;
  header[18] = static_cast<uint8_t>(width & 0xFF);
  header[19] = static_cast<uint8_t>((width >> 8) & 0xFF);
  header[22] = static_cast<uint8_t>(height & 0xFF);
  header[23] = static_cast<uint8_t>((height >> 8) & 0xFF);
  header[26] = 1;
  header[28] = 1;
  header[34] = static_cast<uint8_t>(pixelDataSize & 0xFF);
  header[35] = static_cast<uint8_t>((pixelDataSize >> 8) & 0xFF);
  header[36] = static_cast<uint8_t>((pixelDataSize >> 16) & 0xFF);
  header[37] = static_cast<uint8_t>((pixelDataSize >> 24) & 0xFF);

  header[54] = 0x00;
  header[55] = 0x00;
  header[56] = 0x00;
  header[57] = 0x00;
  header[58] = 0xFF;
  header[59] = 0xFF;
  header[60] = 0xFF;
  header[61] = 0x00;

  if (file.write(header, sizeof(header)) != sizeof(header)) {
    file.close();
    Serial.printf("Failed to write BMP header to %s\n", path);
    return false;
  }

  uint8_t *bitmap = static_cast<uint8_t *>(malloc(static_cast<size_t>(pixelDataSize)));
  if (!bitmap) {
    file.close();
    Serial.println("Not enough memory for BMP bitmap buffer");
    return false;
  }

  if (!ditherToPackedBitmap(buf, width, height, exposureCenter, bitmap, rowStride)) {
    free(bitmap);
    file.close();
    return false;
  }

  if (file.write(bitmap, static_cast<size_t>(pixelDataSize)) != static_cast<size_t>(pixelDataSize)) {
    free(bitmap);
    file.close();
    Serial.printf("Failed while writing BMP data to %s\n", path);
    return false;
  }

  free(bitmap);
  file.close();
  return true;
}

bool saveResizedAndCropped(const camera_fb_t *fb) {
  if (fb->width <= 0 || fb->height <= 0) {
    Serial.println("Invalid frame size");
    return false;
  }

  const int scaledWidth = TARGET_WIDTH;
  const int scaledHeight = (fb->height * TARGET_WIDTH) / fb->width;
  if (scaledHeight < TARGET_HEIGHT) {
    Serial.println("Scaled image is too small for target crop");
    return false;
  }

  uint8_t *scaled = static_cast<uint8_t *>(malloc(static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight)));
  uint8_t *cropped = static_cast<uint8_t *>(malloc(static_cast<size_t>(TARGET_WIDTH) * static_cast<size_t>(TARGET_HEIGHT)));
  if (!scaled || !cropped) {
    Serial.println("Not enough memory for resize/crop buffers");
    free(scaled);
    free(cropped);
    return false;
  }

  for (int y = 0; y < scaledHeight; y++) {
    const int srcY = (y * fb->height) / scaledHeight;
    for (int x = 0; x < scaledWidth; x++) {
      const int srcX = (x * fb->width) / scaledWidth;
      scaled[y * scaledWidth + x] = fb->buf[srcY * fb->width + srcX];
    }
  }

  const int cropStartY = (scaledHeight - TARGET_HEIGHT) / 2;
  for (int y = 0; y < TARGET_HEIGHT; y++) {
    memcpy(cropped + (y * TARGET_WIDTH), scaled + ((cropStartY + y) * scaledWidth), TARGET_WIDTH);
  }

  const uint8_t averageLuminance = computeAverageLuminance(cropped, TARGET_WIDTH, TARGET_HEIGHT);
  Serial.printf("BMP dithering center: %u\n", averageLuminance);
  const bool ok = writeBmp1Bit(THUMB_IMAGE_PATH, cropped, TARGET_WIDTH, TARGET_HEIGHT, averageLuminance);
  free(scaled);
  free(cropped);
  return ok;
}

void captureAndSave() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  if (fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("Frame is not grayscale");
    esp_camera_fb_return(fb);
    return;
  }

  const bool fullOk = writeJpegFromGray(FULL_IMAGE_PATH, fb->buf, fb->width, fb->height);
  const bool thumbOk = saveResizedAndCropped(fb);

  esp_camera_fb_return(fb);

  if (fullOk) {
    Serial.printf("Saved %s\n", FULL_IMAGE_PATH);
  }
  if (thumbOk) {
    Serial.printf("Saved %s\n", THUMB_IMAGE_PATH);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println();

  if (!initSdCard()) {
    Serial.println("SD init failed");
    return;
  }

  if (!initCamera()) {
    Serial.println("Camera init failed");
    return;
  }

  if (!initAccessPoint()) {
    return;
  }

  initWebServer();

  captureAndSave();
  Serial.println("Ready. Capturing every 1 second and overwriting /capture.jpg and /capture.bmp.");
}

void loop() {
  static unsigned long lastCaptureMs = 0;
  server.handleClient();

  const unsigned long now = millis();
  if (now - lastCaptureMs >= CAPTURE_INTERVAL_MS) {
    lastCaptureMs = now;
    captureAndSave();
  }

  delay(10);
}
