/*
 * Send_data: on BOOT button press, connects to ESPCAM WiFi AP,
 * downloads /capture.bmp and /capture.jpg via HTTP, then transmits
 * both over LoRa SX1276 in packets of up to 250 bytes.
 *
 * Packet format BMP : ['I', pkt_idx, total_pkts, data_len, <data>]
 * Packet format JPEG: ['J', pkt_idx, total_pkts, data_len, <data>]
 */
#include <RadioLib.h>
#include "pin_config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#ifdef T3_S3_SX1276
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1, SPI);
#endif

// ── WiFi / camera credentials ─────────────────────────────────────
#define WIFI_SSID   "ESPCAM"
#define WIFI_PASS   "nullbisneun"
#define CAM_URL_BMP "http://192.168.4.1/capture.bmp"
#define CAM_URL_JPG "http://192.168.4.1/capture.jpg"
#define WIFI_TIMEOUT_MS 10000

// ── Bitmap transfer constants ──────────────────────────────────────
#define BMP_WIDTH      128
#define BMP_HEIGHT     64
#define BMP_BYTES      (BMP_WIDTH * BMP_HEIGHT / 8)  // 1024
#define PKT_DATA_SIZE  250    // pixel data bytes per LoRa packet
#define PKT_HEADER     4      // 'I' + idx + total + len
#define PKT_TOTAL      ((BMP_BYTES + PKT_DATA_SIZE - 1) / PKT_DATA_SIZE)  // 5

static uint8_t bitmapBuf[BMP_BYTES];
static bool    bitmapReady = false;

// ── JPEG transfer buffers ────────────────────────────────────────────
#define JPEG_MAX_SIZE  (32 * 1024)
static uint8_t  jpegBuf[JPEG_MAX_SIZE];
static uint32_t jpegSize     = 0;
static int      jpegPktTotal = 0;
static bool     jpegReady    = false;

// TX state machine
enum TxPhase : int8_t { PHASE_IDLE = -1, PHASE_BMP = 0, PHASE_JPEG = 1 };
enum TxState  : uint8_t { STATE_IDLE, STATE_TX_WAIT, STATE_ACK_WAIT };
volatile bool radioFlag = false;
static TxPhase       txPhase    = PHASE_IDLE;
static TxState       txState    = STATE_IDLE;
static int           txPacket   = 0;
static int           retryCount = 0;
static unsigned long ackDeadline = 0;
static uint8_t       txBuf[PKT_HEADER + PKT_DATA_SIZE];

#define ACK_TIMEOUT_MS 3000
#define MAX_RETRIES    5

#if defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void) { radioFlag = true; }

// ── Display helper ──────────────────────────────────────────────────
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

// ── BMP reader ─────────────────────────────────────────────────────
// Parses 1-bit BMP pixel data from a raw byte buffer, flipping rows
// top<->bottom (BMP stores bottom-to-top).
bool parseBmp(const uint8_t *raw, int rawLen, uint8_t *dst)
{
    if (rawLen < 14 || raw[0] != 'B' || raw[1] != 'M') {
        displayPrint("Not a BMP"); return false;
    }
    uint32_t pixelOffset = (uint32_t)raw[10] | ((uint32_t)raw[11] << 8)
                         | ((uint32_t)raw[12] << 16) | ((uint32_t)raw[13] << 24);
    if ((int)(pixelOffset + BMP_BYTES) > rawLen) {
        displayPrint("BMP too small"); return false;
    }
    const uint8_t *pixels = raw + pixelOffset;
    const int rowBytes = BMP_WIDTH / 8;
    for (int row = 0; row < BMP_HEIGHT; row++)
        memcpy(&dst[row * rowBytes], &pixels[(BMP_HEIGHT - 1 - row) * rowBytes], rowBytes);
    return true;
}

// ── WiFi + HTTP fetch ──────────────────────────────────────────────
// Downloads url into buf (max maxLen bytes). Returns actual size or 0.
uint32_t httpFetch(const char *url, uint8_t *buf, uint32_t maxLen)
{
    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) {
        char msg[40]; snprintf(msg, sizeof(msg), "HTTP %d", code);
        displayPrint("Fetch failed", msg);
        http.end();
        return 0;
    }
    int len = http.getSize();
    if (len <= 0 || (uint32_t)len > maxLen) {
        char msg[40]; snprintf(msg, sizeof(msg), "Size %d", len);
        displayPrint("Fetch size err", msg);
        http.end();
        return 0;
    }
    WiFiClient *stream = http.getStreamPtr();
    uint32_t got = 0;
    while (http.connected() && got < (uint32_t)len) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min((uint32_t)avail, maxLen - got);
            got += stream->readBytes(buf + got, toRead);
        }
    }
    http.end();
    return got;
}

// ── Fetch BMP + JPEG from camera over WiFi ─────────────────────────
bool fetchFromCamera()
{
    displayPrint("Connecting...", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > WIFI_TIMEOUT_MS) {
            WiFi.disconnect(true);
            displayPrint("WiFi timeout"); return false;
        }
        delay(100);
    }
    displayPrint("WiFi OK", "Fetching BMP...");

    // Temporary buffer for raw BMP file (header + pixels)
    static uint8_t bmpRaw[1200];  // 14B header + 40B DIB + 1024B pixels
    uint32_t bmpRawLen = httpFetch(CAM_URL_BMP, bmpRaw, sizeof(bmpRaw));
    if (bmpRawLen == 0) { WiFi.disconnect(true); return false; }

    displayPrint("WiFi OK", "Fetching JPG...");
    jpegSize = httpFetch(CAM_URL_JPG, jpegBuf, JPEG_MAX_SIZE);
    if (jpegSize == 0) { WiFi.disconnect(true); return false; }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (!parseBmp(bmpRaw, (int)bmpRawLen, bitmapBuf)) return false;

    jpegPktTotal = (int)((jpegSize + PKT_DATA_SIZE - 1) / PKT_DATA_SIZE);
    return true;
}

// ── Send one packet ────────────────────────────────────────────────
void sendPacket(TxPhase phase, int idx)
{
    uint8_t    *src;
    uint32_t    totalBytes;
    int         pktTotal;
    const char *label;

    if (phase == PHASE_BMP) {
        src        = bitmapBuf;
        totalBytes = BMP_BYTES;
        pktTotal   = PKT_TOTAL;
        label      = "BMP";
        txBuf[0]   = 'I';
    } else {
        src        = jpegBuf;
        totalBytes = jpegSize;
        pktTotal   = jpegPktTotal;
        label      = "JPG";
        txBuf[0]   = 'J';
    }

    int offset  = idx * PKT_DATA_SIZE;
    int dataLen = min((int)PKT_DATA_SIZE, (int)(totalBytes - offset));
    txBuf[1] = (uint8_t)idx;
    txBuf[2] = (uint8_t)pktTotal;
    txBuf[3] = (uint8_t)dataLen;
    memcpy(&txBuf[4], &src[offset], dataLen);

    char buf[32];
    snprintf(buf, sizeof(buf), "TX %d/%d  %dB", idx + 1, pktTotal, dataLen);
    displayPrint(label, buf);

    radio.startTransmit(txBuf, PKT_HEADER + dataLen);
    txPhase  = phase;
    txPacket = idx;
}

// ── Retry current packet or abort after MAX_RETRIES ────────────────
void retryAndSend()
{
    if (++retryCount > MAX_RETRIES) {
        txPhase    = PHASE_IDLE;
        txState    = STATE_IDLE;
        retryCount = 0;
        displayPrint("Send data", "ACK timeout!", "Press BOOT to retry");
        radio.startReceive();
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "Retry %d/%d", retryCount, MAX_RETRIES);
        displayPrint("Send data", buf);
        sendPacket(txPhase, txPacket);
        txState = STATE_TX_WAIT;
    }
}

void setup()
{
    pinMode(0, INPUT_PULLUP);

    Wire.begin(SCREEN_SDA, SCREEN_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);

    // Init LoRa
    SPI.begin(LORA_SCLK, LORA_MISO, LORA_MOSI);
    displayPrint("Send data", "LoRa init...");

    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        char buf[32]; snprintf(buf, sizeof(buf), "LoRa ERR %d", state);
        displayPrint("Send data", buf); while (true);
    }
    radio.setFrequency(868.1);
    radio.setBandwidth(500.0);
    radio.setSpreadingFactor(9);
    radio.setCodingRate(6);
    radio.setSyncWord(0xAB);
    radio.setOutputPower(17);
    radio.setPreambleLength(16);
    radio.setCRC(false);
    radio.setDio0Action(setFlag, RISING);

    displayPrint("Send data", "Ready!", "Press BOOT to fetch+send");
}

void loop()
{
    // BOOT button: fetch images from camera, then start TX
    if (digitalRead(0) == LOW && txState == STATE_IDLE) {
        delay(200);
        bitmapReady = false;
        jpegReady   = false;
        // LoRa shares the SPI bus – put it to sleep while WiFi is active
        radio.standby();
        if (fetchFromCamera()) {
            bitmapReady = true;
            jpegReady   = true;
            displayPrint("Send data", "BMP+JPG OK", "Starting TX...");
            delay(500);
            sendPacket(PHASE_BMP, 0);
            txState = STATE_TX_WAIT;
        } else {
            displayPrint("Send data", "Fetch FAILED", "Press BOOT to retry");
            radio.startReceive();  // keep LoRa awake
        }
    }

    // Radio interrupt: TX done or ACK received
    if (radioFlag) {
        radioFlag = false;

        if (txState == STATE_TX_WAIT) {
            // TX done – switch to RX to wait for ACK
            radio.finishTransmit();
            ackDeadline = millis() + ACK_TIMEOUT_MS;
            txState = STATE_ACK_WAIT;
            radio.startReceive();

        } else if (txState == STATE_ACK_WAIT) {
            // ACK received
            uint8_t ackBuf[8];
            int st = radio.readData(ackBuf, sizeof(ackBuf));
            if (st == RADIOLIB_ERR_NONE && ackBuf[0] == 'A') {
                uint8_t ackIdx  = ackBuf[1];
                uint8_t ackType = ackBuf[2];
                bool valid = (txPhase == PHASE_BMP  && ackType == 'I' && ackIdx == (uint8_t)txPacket) ||
                             (txPhase == PHASE_JPEG && ackType == 'J' && ackIdx == (uint8_t)txPacket);
                if (valid) {
                    retryCount = 0;
                    int next = txPacket + 1;
                    if (txPhase == PHASE_BMP) {
                        if (next < PKT_TOTAL) {
                            sendPacket(PHASE_BMP, next);
                        } else {
                            // BMP done, continue with JPEG
                            sendPacket(PHASE_JPEG, 0);
                        }
                    } else {  // PHASE_JPEG
                        if (next < jpegPktTotal) {
                            sendPacket(PHASE_JPEG, next);
                        } else {
                            // All done
                            txPhase = PHASE_IDLE;
                            txState = STATE_IDLE;
                            displayPrint("Done!", "BMP+JPG sent", "Press BOOT again");
                            radio.startReceive();
                            return;
                        }
                    }
                    txState = STATE_TX_WAIT;
                } else {
                    retryAndSend();  // wrong ACK
                }
            } else {
                retryAndSend();  // bad read or not an ACK packet
            }
        }
    }

    // ACK timeout
    if (txState == STATE_ACK_WAIT && (long)(millis() - ackDeadline) >= 0) {
        retryAndSend();
    }
}
