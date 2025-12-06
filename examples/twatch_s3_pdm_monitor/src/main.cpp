#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>

// PDM microphone pins from pinout.jpeg
constexpr int MIC_DATA = 47; // PDM data
constexpr int MIC_CLK = 44;  // PDM clock

constexpr int SAMPLE_RATE = 16000; // Hz
constexpr int BUFFER_SAMPLES = 512; // Must match dma_buf_len

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\nT-Watch S3 PDM mic monitor");
    Serial.println("Pins: CLK=IO44, DATA=IO47");

    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = BUFFER_SAMPLES;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_PIN_NO_CHANGE; // Not used in PDM mode
    pins.ws_io_num = MIC_CLK;            // PDM clock
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = MIC_DATA;         // PDM data

    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pins));
    ESP_ERROR_CHECK(i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO));
}

void loop()
{
    static int16_t samples[BUFFER_SAMPLES];
    size_t bytes_read = 0;

    esp_err_t err = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytes_read, portMAX_DELAY);
    if (err != ESP_OK || bytes_read == 0)
        return;

    const size_t count = bytes_read / sizeof(int16_t);
    double accum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        int32_t s = samples[i];
        accum += static_cast<double>(s) * static_cast<double>(s);
    }

    const double rms = sqrt(accum / count);
    double norm = rms / 32768.0; // Normalize to full-scale
    if (norm < 1e-9)
        norm = 1e-9;
    const double dbfs = 20.0 * log10(norm);

    int bar_len = static_cast<int>(norm * 80.0);
    if (bar_len > 80)
        bar_len = 80;

    Serial.printf("%6.1f dBFS |", dbfs);
    for (int i = 0; i < 80; ++i)
        Serial.print(i < bar_len ? '#' : ' ');
    Serial.println('|');
}
