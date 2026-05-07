# 1 "C:\\Users\\FELIXW~1\\AppData\\Local\\Temp\\tmpdhqbm14a"
#include <Arduino.h>
# 1 "D:/work/Lilygo-T3-S3-MVSR-LoRa/sbb/Receive_data/Receive_data.ino"
# 9 "D:/work/Lilygo-T3-S3-MVSR-LoRa/sbb/Receive_data/Receive_data.ino"
#include <RadioLib.h>
#include "pin_config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SD.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#ifdef T3_S3_SX1276
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1, SPI);
#endif


#define BMP_BYTES 1024
#define JPEG_MAX_SIZE (32 * 1024)
#define PKT_DATA_SIZE 250
#define PKT_HEADER 4
#define PKT_TOTAL_MAX 140


static uint8_t bitmapBuf[BMP_BYTES];
static bool bmpPktReceived[8] = {};
static int bmpPktTotal = 0;
static int bmpPktCount = 0;


static uint8_t jpegBuf[JPEG_MAX_SIZE];
static bool jpegPktReceived[PKT_TOTAL_MAX] = {};
static int jpegPktTotal = 0;
static int jpegPktCount = 0;
static uint8_t jpegLastPktLen = 0;

volatile bool receivedFlag = false;

#if defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void);
void showBitmap();
void saveJpeg(uint32_t size);
void setup();
void loop();
#line 50 "D:/work/Lilygo-T3-S3-MVSR-LoRa/sbb/Receive_data/Receive_data.ino"
void setFlag(void) { receivedFlag = true; }


void displayPrint(const char *l1, const char *l2 = nullptr,
                  const char *l3 = nullptr, const char *l4 = nullptr)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    if (l1) display.println(l1);
    if (l2) display.println(l2);
    if (l3) display.println(l3);
    if (l4) display.println(l4);
    display.display();
}


void showBitmap()
{
    display.clearDisplay();
    display.drawBitmap(0, 0, bitmapBuf, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.display();
}


void saveJpeg(uint32_t size)
{
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 40000000)) {
        SPI.end();
        displayPrint("Receive data", "SD mount FAILED");
        Serial.println("[JPEG] SD mount failed");
        return;
    }

    SD.remove("/capture.jpg");
    File f = SD.open("/capture.jpg", FILE_WRITE);
    if (!f) {
        SD.end(); SPI.end();
        displayPrint("Receive data", "JPG open FAILED");
        Serial.println("[JPEG] Could not open /capture.jpg for writing");
        return;
    }
    f.write(jpegBuf, size);
    f.close();
    SD.end();
    SPI.end();

    char buf[32];
    snprintf(buf, sizeof(buf), "JPG saved %luB", (unsigned long)size);
    displayPrint("Receive data", buf);
    Serial.printf("[JPEG] Saved /capture.jpg  %lu bytes\n", (unsigned long)size);
}

void setup()
{
    Serial.begin(115200);

    Wire.begin(SCREEN_SDA, SCREEN_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    displayPrint("Receive data", "Initializing...");

    SPI.begin(LORA_SCLK, LORA_MISO, LORA_MOSI);

    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        char buf[32]; snprintf(buf, sizeof(buf), "LoRa ERR %d", state);
        displayPrint("Receive data", buf); while (true);
    }
    radio.setFrequency(868.1);
    radio.setBandwidth(500.0);
    radio.setSpreadingFactor(9);
    radio.setCodingRate(6);
    radio.setSyncWord(0xAB);
    radio.setPreambleLength(16);
    radio.setCRC(false);
    radio.setDio0Action(setFlag, RISING);
    radio.startReceive();

    displayPrint("Receive data", "LoRa OK", "Waiting for image...");
    Serial.println("Waiting for BMP/JPEG packets...");
}

void loop()
{
    if (!receivedFlag) return;
    receivedFlag = false;


    uint8_t buf[PKT_HEADER + PKT_DATA_SIZE];
    int state = radio.readData(buf, sizeof(buf));

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[RX] Error %d\n", state);
        radio.startReceive();
        return;
    }

    uint8_t type = buf[0];
    uint8_t idx = buf[1];
    uint8_t total = buf[2];
    uint8_t dataLen = buf[3];


    if (type == 'I') {
        if (idx >= 8 || dataLen > PKT_DATA_SIZE) {
            Serial.printf("[BMP] Bad packet idx=%d len=%d\n", idx, dataLen);
            radio.startReceive();
            return;
        }
        if (bmpPktTotal == 0 || total != bmpPktTotal) {
            memset(bmpPktReceived, 0, sizeof(bmpPktReceived));
            memset(bitmapBuf, 0, sizeof(bitmapBuf));
            bmpPktTotal = total;
            bmpPktCount = 0;
            Serial.printf("[BMP] New transfer: %d packets\n", total);
        }
        if (!bmpPktReceived[idx]) {
            memcpy(&bitmapBuf[idx * PKT_DATA_SIZE], &buf[4], dataLen);
            bmpPktReceived[idx] = true;
            bmpPktCount++;
            Serial.printf("[BMP] %d/%d RSSI=%.0fdBm\n", idx+1, total, radio.getRSSI());
            char line[32];
            snprintf(line, sizeof(line), "BMP %d/%d RSSI:%.0f", idx+1, total, radio.getRSSI());
            displayPrint("Receive data", line);
        }
        if (bmpPktCount == bmpPktTotal) {
            Serial.println("[BMP] Complete – waiting for JPEG");
            bmpPktTotal = 0;
        }


    } else if (type == 'J') {
        if (idx >= PKT_TOTAL_MAX || dataLen > PKT_DATA_SIZE) {
            Serial.printf("[JPG] Bad packet idx=%d len=%d\n", idx, dataLen);
            radio.startReceive();
            return;
        }
        if (jpegPktTotal == 0 || total != jpegPktTotal) {
            memset(jpegPktReceived, 0, sizeof(jpegPktReceived));
            memset(jpegBuf, 0, sizeof(jpegBuf));
            jpegPktTotal = total;
            jpegPktCount = 0;
            Serial.printf("[JPG] New transfer: %d packets\n", total);
        }
        if (!jpegPktReceived[idx]) {
            uint32_t offset = (uint32_t)idx * PKT_DATA_SIZE;
            if (offset + dataLen <= JPEG_MAX_SIZE) {
                memcpy(&jpegBuf[offset], &buf[4], dataLen);
                jpegPktReceived[idx] = true;
                jpegPktCount++;
                if (idx == (uint8_t)(total - 1)) jpegLastPktLen = dataLen;
            }
            Serial.printf("[JPG] %d/%d RSSI=%.0fdBm\n", idx+1, total, radio.getRSSI());
            char line[32];
            snprintf(line, sizeof(line), "JPG %d/%d RSSI:%.0f", idx+1, total, radio.getRSSI());
            displayPrint("Receive data", line);
        }
        if (jpegPktCount == jpegPktTotal) {
            Serial.println("[JPG] Complete – saving to SD");
            uint32_t jpegSize = (uint32_t)(jpegPktTotal - 1) * PKT_DATA_SIZE
                              + (uint32_t)jpegLastPktLen;
            jpegPktTotal = 0;

            radio.standby();
            SPI.end();
            saveJpeg(jpegSize);
            showBitmap();

            SPI.begin(LORA_SCLK, LORA_MISO, LORA_MOSI);
            radio.startReceive();
            return;
        }

    } else {
        Serial.printf("[RX] Unknown type 0x%02X, ignoring\n", type);
    }

    radio.startReceive();
}