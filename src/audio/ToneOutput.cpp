#include "audio/ToneOutput.h"

#include "configuration.h"

#include <Arduino.h>

#if defined(T_WATCH_S3)
#include <driver/i2s.h>
#include <esp_err.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif

namespace audio
{

#if defined(T_WATCH_S3)

#ifndef DAC_I2S_BCK
#define DAC_I2S_BCK 48
#define DAC_I2S_WS 15
#define DAC_I2S_DOUT 46
#define DAC_I2S_MCLK 0
#endif

namespace
{
constexpr i2s_port_t kTonePort = I2S_NUM_0;
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kBufferSamples = 256;
constexpr int16_t kAmplitude = 15000;
constexpr TickType_t kIdleDelayTicks = pdMS_TO_TICKS(10);

TaskHandle_t toneTaskHandle = nullptr;
volatile bool tonePlaying = false;
volatile uint32_t toneFrequencyHz = 0;
bool i2sReady = false;
float waveformPhase = 0.0f;

void toneWriterTask(void *param)
{
    static int16_t buffer[kBufferSamples];
    while (true) {
        if (!tonePlaying) {
            vTaskDelay(kIdleDelayTicks);
            continue;
        }

        uint32_t freq = toneFrequencyHz;
        if (freq < 20)
            freq = 20;

        float phase = waveformPhase;
        const float phaseIncrement = (2.0f * static_cast<float>(M_PI) * static_cast<float>(freq)) / static_cast<float>(kSampleRate);

        for (size_t i = 0; i < kBufferSamples; ++i) {
            float sample = sinf(phase);
            buffer[i] = static_cast<int16_t>(sample * static_cast<float>(kAmplitude));
            phase += phaseIncrement;
            if (phase >= 2.0f * static_cast<float>(M_PI))
                phase -= 2.0f * static_cast<float>(M_PI);
        }
        waveformPhase = phase;

        size_t bytesWritten = 0;
        i2s_write(kTonePort, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
    }
}

void ensureToneTask()
{
    if (toneTaskHandle)
        return;
    xTaskCreatePinnedToCore(toneWriterTask, "ToneI2S", 2048, nullptr, 1, &toneTaskHandle, 0);
}

void ensureI2S()
{
    if (i2sReady)
        return;

    i2s_config_t config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = kSampleRate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = static_cast<i2s_comm_format_t>(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = kBufferSamples,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    if (i2s_driver_install(kTonePort, &config, 0, nullptr) != ESP_OK) {
        LOG_ERROR("ToneOutput: failed to install I2S driver");
        return;
    }

    i2s_pin_config_t pins = {};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
    pins.mck_io_num = (DAC_I2S_MCLK >= 0) ? DAC_I2S_MCLK : I2S_PIN_NO_CHANGE;
#endif
    pins.bck_io_num = DAC_I2S_BCK;
    pins.ws_io_num = DAC_I2S_WS;
    pins.data_out_num = DAC_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(kTonePort, &pins) != ESP_OK) {
        LOG_ERROR("ToneOutput: failed to set I2S pins");
        i2s_driver_uninstall(kTonePort);
        return;
    }

    i2s_set_sample_rates(kTonePort, kSampleRate);
    i2sReady = true;
}

} // namespace

void toneOutputPlay(uint32_t frequencyHz)
{
    if (frequencyHz == 0)
        frequencyHz = 1;
    ensureI2S();
    if (!i2sReady)
        return;
    ensureToneTask();
    toneFrequencyHz = frequencyHz;
    tonePlaying = true;
}

void toneOutputStop()
{
    tonePlaying = false;
    if (i2sReady)
        i2s_zero_dma_buffer(kTonePort);
}

void toneOutputReset()
{
    tonePlaying = false;
    if (i2sReady) {
        i2s_zero_dma_buffer(kTonePort);
        i2s_driver_uninstall(kTonePort);
    }
    i2sReady = false;
}

#else

void toneOutputPlay(uint32_t frequencyHz)
{
    int32_t buzzerPin = static_cast<int32_t>(config.device.buzzer_gpio);
    if (buzzerPin >= 0)
        tone(buzzerPin, frequencyHz);
}

void toneOutputStop()
{
    int32_t buzzerPin = static_cast<int32_t>(config.device.buzzer_gpio);
    if (buzzerPin >= 0)
        noTone(buzzerPin);
}

void toneOutputReset()
{
    toneOutputStop();
}

#endif

} // namespace audio
