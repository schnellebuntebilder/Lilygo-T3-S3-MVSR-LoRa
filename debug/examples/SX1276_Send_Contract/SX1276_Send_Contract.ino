/*
 * @Description: SX1276 Send Contract - ported from SX1262_Send_Contract
 * @License: GPL 3.0
 *
 * Sends "Hello World! #N" packets continuously via interrupt-driven transmit.
 * Uses SX1276 with DIO0 for TX-done interrupt (replaces SX1262 DIO1).
 */
#include <RadioLib.h>
#include "pin_config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#ifdef T3_S3_SX1276
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1, SPI);
#endif

#ifdef T3_S3_SX1278
SX1278 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1, SPI);
#endif

// save transmission state between loops
int transmissionState = RADIOLIB_ERR_NONE;

volatile bool transmittedFlag = false;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void)
{
    // we sent a packet, set the flag
    transmittedFlag = true;
}

// counter to keep track of transmitted packets
int count = 0;

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

void setup()
{
    Serial.begin(115200);

    Wire.begin(SCREEN_SDA, SCREEN_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("SSD1306 init failed");
    }
    displayPrint("SX1276 Send", "Initializing...");

    SPI.begin(LORA_SCLK, LORA_MISO, LORA_MOSI);
    Serial.print("[SX1276] Initializing ... ");
    int state = radio.begin();
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println("success!");
        displayPrint("SX1276 Send", "LoRa OK");
    }
    else
    {
        Serial.print("failed, code ");
        Serial.println(state);
        char buf[32];
        snprintf(buf, sizeof(buf), "Error code: %d", state);
        displayPrint("SX1276 Send", "LoRa FAILED", buf);
        while (true)
            ;
    }

    radio.setFrequency(868.1);
    radio.setBandwidth(500.0);
    radio.setSpreadingFactor(9);
    radio.setCodingRate(6);
    radio.setSyncWord(0xAB);
    radio.setOutputPower(17);
    radio.setPreambleLength(16);
    radio.setCRC(false);

    // set the function that will be called when packet transmission is finished
    // SX1276 uses DIO0 for TX done (not setPacketSentAction like SX1262)
    radio.setDio0Action(setFlag, RISING);

    // start transmitting the first packet
    Serial.print("[SX1276] Sending first packet ... ");
    displayPrint("SX1276 Send", "Sending #0...");
    transmissionState = radio.startTransmit("Hello World!");
}

void loop()
{
    // check if the previous transmission finished
    if (transmittedFlag)
    {
        // reset flag
        transmittedFlag = false;

        if (transmissionState == RADIOLIB_ERR_NONE)
        {
            Serial.println("transmission finished!");
            char buf[32];
            snprintf(buf, sizeof(buf), "TX OK  #%d", count);
            displayPrint("SX1276 Send", buf);
        }
        else
        {
            Serial.print("failed, code ");
            Serial.println(transmissionState);
            char buf[32];
            snprintf(buf, sizeof(buf), "TX FAIL code:%d", transmissionState);
            displayPrint("SX1276 Send", buf);
        }

        // clean up after transmission
        radio.finishTransmit();

        // wait a second before transmitting again
        delay(1000);

        // send another one
        Serial.print("[SX1276] Sending another packet ... ");
        String str = "Hello World! #" + String(count++);
        char buf[32];
        snprintf(buf, sizeof(buf), "Sending #%d...", count);
        displayPrint("SX1276 Send", buf);
        transmissionState = radio.startTransmit(str);
    }
}
