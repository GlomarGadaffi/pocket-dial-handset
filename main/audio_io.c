#include "audio_io.h"
#include "board_mvsr.h"
#include "poc_config.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "audio_io";

static i2s_chan_handle_t s_spk = NULL;   // TX -> MAX98357A
static i2s_chan_handle_t s_mic = NULL;   // RX <- MSM261 I2S MEMS

#define AUDIO_TIMEOUT_MS 200

static void init_speaker(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(MVSR_SPK_I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_spk, NULL));     // TX only

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(POC_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MVSR_SPK_BCLK,
            .ws   = MVSR_SPK_LRCLK,
            .dout = MVSR_SPK_DATA,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_spk, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_spk));
}

static void init_mic(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(MVSR_MIC_I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cc, NULL, &s_mic));     // RX only

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(POC_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MVSR_MIC_BCLK,
            .ws   = MVSR_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = MVSR_MIC_DATA,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_mic, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_mic));
}

esp_err_t audio_init(void)
{
    // Enable rails first.
    gpio_set_direction(MVSR_MIC_EN, GPIO_MODE_OUTPUT);
    gpio_set_direction(MVSR_SPK_SD_MODE, GPIO_MODE_OUTPUT);
    audio_mic_enable(true);
    audio_amp_enable(true);

    init_speaker();
    init_mic();
    ESP_LOGI(TAG, "I2S up: mic=I2S%d spk=I2S%d @ %d Hz mono",
             MVSR_MIC_I2S_PORT, MVSR_SPK_I2S_PORT, POC_SAMPLE_RATE_HZ);
    return ESP_OK;
}

size_t audio_read_mic(int16_t *buf, size_t samples)
{
    size_t got = 0;
    if (i2s_channel_read(s_mic, buf, samples * sizeof(int16_t), &got, AUDIO_TIMEOUT_MS) != ESP_OK)
        return 0;
    return got / sizeof(int16_t);
}

size_t audio_write_spk(const int16_t *buf, size_t samples)
{
    size_t put = 0;
    if (i2s_channel_write(s_spk, buf, samples * sizeof(int16_t), &put, AUDIO_TIMEOUT_MS) != ESP_OK)
        return 0;
    return put / sizeof(int16_t);
}

void audio_amp_enable(bool on) { gpio_set_level(MVSR_SPK_SD_MODE, on ? 1 : 0); }
void audio_mic_enable(bool on) { gpio_set_level(MVSR_MIC_EN, on ? 1 : 0); }
