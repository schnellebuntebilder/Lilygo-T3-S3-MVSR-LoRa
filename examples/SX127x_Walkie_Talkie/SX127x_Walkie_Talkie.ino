/*
 * @Description: Walkie-talkie example (ported from SX126x to SX127x/SX1276)
 * @Author: LILYGO_L
 * @Date: 2024-11-07 10:04:25
 * @LastEditTime: 2026-05-07
 * @License: GPL 3.0
 */
#include "RadioLib.h"
#include "pin_config.h"
#include "Arduino_DriveBus_Library.h"
#include "codec2.h"
#include <Wire.h>
#include "Adafruit_GFX.h"
#include "Adafruit_SSD1306.h"

#define IIS_SAMPLE_RATE 8000 // sample rate
#define IIS_DATA_BIT 16      // data bit width

#define LORA_TRANSMISSION_HEAD_SIZE 12
#define LORA_TRANSMISSION_DATA_SIZE 200

const uint64_t Local_MAC = ESP.getEfuseMac();

std::vector<short> IIS_Read_Data_Stream;
std::vector<short> IIS_Write_Data_Stream;
std::vector<unsigned char> Lora_Send_Data_Stream;
std::vector<unsigned char> Lora_Receive_Data_Stream;

bool Lora_Transmission_Mode = 0; // default: receive mode

uint8_t Send_Package[LORA_TRANSMISSION_HEAD_SIZE + LORA_TRANSMISSION_DATA_SIZE] = {
    'M',
    'A',
    'C',
    ':',
    (uint8_t)(Local_MAC >> 56),
    (uint8_t)(Local_MAC >> 48),
    (uint8_t)(Local_MAC >> 40),
    (uint8_t)(Local_MAC >> 32),
    (uint8_t)(Local_MAC >> 24),
    (uint8_t)(Local_MAC >> 16),
    (uint8_t)(Local_MAC >> 8),
    (uint8_t)Local_MAC,
};

uint8_t Receive_Package[LORA_TRANSMISSION_HEAD_SIZE + LORA_TRANSMISSION_DATA_SIZE];

// flag to indicate that a packet was sent or received
volatile bool Radio_Operation_Flag = false;

volatile bool Boot_Key_Flag = false;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RST);

void displayMode(bool transmit)
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    if (transmit)
    {
        display.setTextSize(3);
        display.setCursor(16, 22);
        display.println("SPEAK");
    }
    else
    {
        display.setTextSize(3);
        display.setCursor(10, 22);
        display.println("LISTEN");
    }

    display.display();
}

#if defined T3_S3_MVSRBoard_V1_0
std::shared_ptr<Arduino_IIS_DriveBus> IIS_Bus =
    std::make_shared<Arduino_HWIIS>(I2S_NUM_0, MSM261_BCLK, MSM261_WS, MSM261_DATA);
#elif defined T3_S3_MVSRBoard_V1_1
std::shared_ptr<Arduino_IIS_DriveBus> IIS_Bus =
    std::make_shared<Arduino_HWIIS>(I2S_NUM_0, -1, MP34DT05TR_LRCLK, MP34DT05TR_DATA);
#else
#error "Unknown macro definition. Please select the correct macro definition."
#endif

std::unique_ptr<Arduino_IIS> IIS(new Arduino_MEMS(IIS_Bus));

std::shared_ptr<Arduino_IIS_DriveBus> IIS_Bus_1 =
    std::make_shared<Arduino_HWIIS>(I2S_NUM_1, MAX98357A_BCLK, MAX98357A_LRCLK,
                                    MAX98357A_DATA);

std::unique_ptr<Arduino_IIS> MAX98357A(new Arduino_Amplifier(IIS_Bus_1));

// SX127x uses DIO0 for RX/TX done, DIO1 for RX timeout
#ifdef T3_S3_SX1276
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1, SPI);
#endif

#ifdef T3_S3_SX1278
SX1278 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1, SPI);
#endif

void Radio_Interrupt(void)
{
    Radio_Operation_Flag = true;
}

/**
 * @brief Stereo to mono: extract one channel from interleaved stereo PCM
 */
bool IIS_Mono_Conversion(const int16_t *input_data, std::vector<int16_t> *output_data,
                         size_t input_data_size, bool extract_channel)
{
    if (input_data == nullptr || input_data_size < 1)
    {
        log_w("The input data is incorrect.");
        return false;
    }

    if ((input_data_size % 2) != 0)
    {
        log_w("The input data is not an even number and will result in the loss of one data point.");
        input_data_size--;
    }

    output_data->clear();

    if (extract_channel == 0) // left channel
    {
        for (size_t i = 0; i < input_data_size; i += 2)
        {
            output_data->push_back(input_data[i]);
        }
    }
    else // right channel
    {
        for (size_t i = 0; i < input_data_size; i += 2)
        {
            output_data->push_back(input_data[i + 1]);
        }
    }

    return true;
}

/**
 * @brief Mono to stereo: duplicate mono samples into interleaved stereo PCM
 */
bool IIS_Dual_Conversion(const int16_t *input_data, std::vector<int16_t> *output_data,
                         size_t input_data_size, float decibel_multiplier = 1.0)
{
    if (input_data == nullptr || input_data_size == 0)
    {
        log_w("The input data is incorrect.");
        return false;
    }

    output_data->clear();

    for (size_t i = 0; i < input_data_size; i++)
    {
        output_data->push_back((short)((float)input_data[i] * decibel_multiplier));
        output_data->push_back((short)((float)input_data[i] * decibel_multiplier));
    }

    return true;
}

/**
 * @brief In-place stereo channel operations (copy/clear L or R channel)
 */
bool IIS_Channel_Operation(int16_t *input_data, size_t input_data_size, size_t operation_mode)
{
    if (input_data == nullptr || input_data_size < 1)
    {
        log_w("The input data is incorrect.");
        return false;
    }

    if ((input_data_size % 2) != 0)
    {
        log_w("The input data is not an even number and will result in the loss of one data point.");
        input_data_size--;
    }

    switch (operation_mode)
    {
    case 1: // copy right channel to left
        for (size_t i = 0; i < input_data_size; i++)
        {
            input_data[2 * i] = input_data[2 * i + 1];
        }
        break;
    case 2: // copy left channel to right
        for (size_t i = 0; i < input_data_size; i++)
        {
            input_data[2 * i + 1] = input_data[2 * i];
        }
        break;
    case 3: // clear left channel
        for (size_t i = 0; i < input_data_size; i++)
        {
            input_data[2 * i] = 0;
        }
        break;
    case 4: // clear right channel
        for (size_t i = 0; i < input_data_size; i++)
        {
            input_data[2 * i + 1] = 0;
        }
        break;

    default:
        break;
    }

    return true;
}

void Codec2_Task(void *parameter)
{
    struct CODEC2 *codec2_state;
    // microphone sampling count: 1 sample = 8 bytes, 25 samples = 200 bytes
    const uint8_t msm_sample_frequency = LORA_TRANSMISSION_DATA_SIZE / 8;

    codec2_state = codec2_create(CODEC2_MODE_3200);
    if (codec2_state == NULL)
    {
        Serial.println("Failed to create Codec2");
        return;
    }

    // enable LPC post-filter for improved decoded speech clarity
    codec2_set_lpc_post_filter(codec2_state, 1, 0, 0.8, 0.2);

    const auto codec2_sample_size = codec2_samples_per_frame(codec2_state);
    const auto codec2_compress_size = codec2_bytes_per_frame(codec2_state);

    const auto msm_sample_buf_size = sizeof(short) * codec2_sample_size;
    auto msm_sample_buf = (short *)malloc(msm_sample_buf_size);
    auto codec2_buf = (short *)malloc(msm_sample_buf_size);
    auto codec2_compress_buf = (unsigned char *)malloc(sizeof(unsigned char) * codec2_compress_size);

    Serial.printf("Codec2 initialization successful\n   codec2_sample_size: %d\n    codec2_compress_size: %d\n",
                  codec2_sample_size, codec2_compress_size);
    Serial.println("Audio task started");

    while (1)
    {
        delay(1);

        if (Lora_Transmission_Mode == 1) // encode and fill send buffer
        {
            for (int i = 0; i < msm_sample_frequency; i++)
            {
                if (IIS->IIS_Read_Data(msm_sample_buf, msm_sample_buf_size) == true)
                {
                    codec2_encode(codec2_state, codec2_compress_buf, msm_sample_buf);

                    const auto current_buf_size = Lora_Send_Data_Stream.size();
                    Lora_Send_Data_Stream.resize(current_buf_size + codec2_compress_size);
                    memcpy(Lora_Send_Data_Stream.data() + current_buf_size, codec2_compress_buf,
                           codec2_compress_size);
                }
            }
        }

        if (Lora_Receive_Data_Stream.size() >= codec2_compress_size)
        {
            unsigned char lora_data_buf[codec2_compress_size];
            std::vector<short> output_buf;

            memcpy(lora_data_buf, Lora_Receive_Data_Stream.data(), codec2_compress_size);
            Lora_Receive_Data_Stream.erase(Lora_Receive_Data_Stream.begin(),
                                           Lora_Receive_Data_Stream.begin() + codec2_compress_size);

            codec2_decode(codec2_state, codec2_buf, lora_data_buf);

            IIS_Dual_Conversion(codec2_buf, &output_buf, codec2_sample_size, 5.0);

            const auto current_buf_size = IIS_Write_Data_Stream.size();
            const auto output_buf_size = output_buf.size() * 2; // stereo = *2

            IIS_Write_Data_Stream.resize(current_buf_size + output_buf_size);
            memcpy(IIS_Write_Data_Stream.data() + current_buf_size, output_buf.data(),
                   output_buf_size);
        }
    }
}

void MAX_Play_Task(void *parameter)
{
    const short max_play_size = sizeof(short) * 320;

    while (1)
    {
        delay(1);

        if (IIS_Write_Data_Stream.size() >= max_play_size)
        {
            short iis_data_buf[max_play_size];

            memcpy(iis_data_buf, IIS_Write_Data_Stream.data(), max_play_size);
            IIS_Write_Data_Stream.erase(IIS_Write_Data_Stream.begin(),
                                        IIS_Write_Data_Stream.begin() + max_play_size);

            MAX98357A->IIS_Write_Data(iis_data_buf, max_play_size);
        }
    }
}

void setup()
{
    Serial.begin(115200);

    Wire.begin(SCREEN_SDA, SCREEN_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Serial.println("SSD1306 initialization failed");
    }
    displayMode(false); // show LISTEN on startup

    pinMode(0, INPUT_PULLUP);

    pinMode(MAX98357A_SD_MODE, OUTPUT);
    digitalWrite(MAX98357A_SD_MODE, HIGH);

    attachInterrupt(
        BOOT_KEY,
        []
        {
            Boot_Key_Flag = true;
        },
        FALLING);

#if defined T3_S3_MVSRBoard_V1_0
    pinMode(MSM261_EN, OUTPUT);
    digitalWrite(MSM261_EN, HIGH);

    while (IIS->begin(i2s_mode_t::I2S_MODE_MASTER, ad_iis_data_mode_t::AD_IIS_DATA_IN, i2s_channel_fmt_t::I2S_CHANNEL_FMT_ONLY_RIGHT,
                      IIS_DATA_BIT, IIS_SAMPLE_RATE) == false)
    {
        Serial.println("MSM261 initialization fail");
        delay(2000);
    }
    Serial.println("MSM261 initialization successfully");

#elif defined T3_S3_MVSRBoard_V1_1
    pinMode(MP34DT05TR_EN, OUTPUT);
    digitalWrite(MP34DT05TR_EN, LOW);

    while (IIS->begin(i2s_mode_t::I2S_MODE_PDM, ad_iis_data_mode_t::AD_IIS_DATA_IN, i2s_channel_fmt_t::I2S_CHANNEL_FMT_ONLY_RIGHT,
                      IIS_DATA_BIT, IIS_SAMPLE_RATE) == false)
    {
        Serial.println("MP34DT05TR initialization fail");
        delay(2000);
    }
    Serial.println("MP34DT05TR initialization successfully");
#else
#error "Unknown macro definition. Please select the correct macro definition."
#endif

    while (MAX98357A->begin(i2s_mode_t::I2S_MODE_MASTER, ad_iis_data_mode_t::AD_IIS_DATA_OUT, i2s_channel_fmt_t::I2S_CHANNEL_FMT_RIGHT_LEFT,
                            IIS_DATA_BIT, IIS_SAMPLE_RATE) == false)
    {
        Serial.println("MAX98357A initialization fail");
        delay(2000);
    }
    Serial.println("MAX98357A initialization successfully");

    Serial.println("[SX1276] Initializing ... ");

    SPI.setFrequency(16000000);
    SPI.begin(LORA_SCLK, LORA_MISO, LORA_MOSI);

    // SX1276 uses LoRa mode (begin), not FSK
    int state = radio.begin();
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println("success!");
    }
    else
    {
        Serial.print("failed, code ");
        Serial.println(state);
        while (true)
            ;
    }

    radio.setFrequency(868.0);
    radio.setBandwidth(500.0);
    radio.setSpreadingFactor(7);  // SF7: ~80ms airtime per packet vs ~2600ms with SF12
    radio.setCodingRate(5);       // 4/5: less overhead than 4/8
    radio.setSyncWord(0xAB);
    radio.setOutputPower(17); // SX1276 max is 17 dBm (boost mode)
    radio.setCurrentLimit(240);
    radio.setPreambleLength(8);
    radio.setCRC(false);

    // SX127x uses DIO0 for RX done / TX done interrupts
    radio.setDio0Action(Radio_Interrupt, RISING);

    xTaskCreate(&Codec2_Task, "Codec2_Task", 32000, NULL, 5, NULL);
    xTaskCreate(&MAX_Play_Task, "MAX_Play_Task", 30000, NULL, 5, NULL);

    radio.startReceive();
}

void loop()
{
    if (Boot_Key_Flag == true)
    {
        delay(500);

        Lora_Transmission_Mode = !Lora_Transmission_Mode;
        displayMode(Lora_Transmission_Mode);
        if (Lora_Transmission_Mode == 0)
        {
            radio.startReceive();
        }
        IIS_Read_Data_Stream.clear();
        IIS_Write_Data_Stream.clear();
        Lora_Send_Data_Stream.clear();
        Lora_Receive_Data_Stream.clear();

        Boot_Key_Flag = false;
    }

    if (Lora_Send_Data_Stream.size() >= LORA_TRANSMISSION_DATA_SIZE)
    {
        memcpy(&Send_Package[LORA_TRANSMISSION_HEAD_SIZE], Lora_Send_Data_Stream.data(),
               LORA_TRANSMISSION_DATA_SIZE);
        Lora_Send_Data_Stream.erase(Lora_Send_Data_Stream.begin(),
                                    Lora_Send_Data_Stream.begin() + LORA_TRANSMISSION_DATA_SIZE);

        Serial.println("[SX1276] Sending packet ... ");
        radio.transmit(Send_Package,
                       LORA_TRANSMISSION_HEAD_SIZE + LORA_TRANSMISSION_DATA_SIZE);

        // clear TX-done interrupt that fires after blocking transmit
        Radio_Operation_Flag = false;

        // resume listening after blocking transmit when in receive mode
        if (Lora_Transmission_Mode == 0)
        {
            radio.startReceive();
        }
    }

    if (Radio_Operation_Flag && Lora_Transmission_Mode == 0) // only process in receive mode
    {
        Radio_Operation_Flag = false;

        if (radio.readData(Receive_Package,
                           LORA_TRANSMISSION_HEAD_SIZE + LORA_TRANSMISSION_DATA_SIZE) == RADIOLIB_ERR_NONE)
        {
            if ((Receive_Package[0] == 'M') &&
                (Receive_Package[1] == 'A') &&
                (Receive_Package[2] == 'C') &&
                (Receive_Package[3] == ':'))
            {
                uint64_t temp_mac = 0;
                for (size_t i = 0; i < 8; ++i)
                {
                    temp_mac |= (static_cast<uint64_t>(Receive_Package[4 + i]) << (56 - i * 8));
                }

                if (temp_mac != Local_MAC)
                {
                    Serial.println("[SX1276] Received packet!");

                    Serial.print("[SX1276] RSSI:\t\t");
                    Serial.print(radio.getRSSI());
                    Serial.println(" dBm");

                    Serial.print("[SX1276] SNR:\t\t");
                    Serial.print(radio.getSNR());
                    Serial.println(" dB");
                }

                const auto current_buf_size = Lora_Receive_Data_Stream.size();
                Lora_Receive_Data_Stream.resize(current_buf_size + LORA_TRANSMISSION_DATA_SIZE);
                memcpy(Lora_Receive_Data_Stream.data() + current_buf_size,
                       &Receive_Package[LORA_TRANSMISSION_HEAD_SIZE], LORA_TRANSMISSION_DATA_SIZE);
            }
        }

        // re-arm receiver after each received packet
        radio.startReceive();
    }

    delay(2); // xTask time
}
