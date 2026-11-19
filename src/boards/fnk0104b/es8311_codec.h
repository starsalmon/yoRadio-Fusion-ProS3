/* ES8311 codec bring-up for Freenove FNK0104B (Apache-2.0 driver from Freenove / Espressif). */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#if defined(YORADIO_ES8311) && YORADIO_ES8311

#include "esp_err.h"
#include "es8311_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *es8311_handle_t;

typedef enum {
    ES8311_MIC_GAIN_MIN = -1,
    ES8311_MIC_GAIN_0DB,
    ES8311_MIC_GAIN_6DB,
    ES8311_MIC_GAIN_12DB,
    ES8311_MIC_GAIN_18DB,
    ES8311_MIC_GAIN_24DB,
    ES8311_MIC_GAIN_30DB,
    ES8311_MIC_GAIN_36DB,
    ES8311_MIC_GAIN_42DB,
    ES8311_MIC_GAIN_MAX
} es8311_mic_gain_t;

typedef enum {
    ES8311_FADE_OFF = 0,
    ES8311_FADE_4LRCK,
    ES8311_FADE_8LRCK,
    ES8311_FADE_16LRCK,
    ES8311_FADE_32LRCK,
    ES8311_FADE_64LRCK,
    ES8311_FADE_128LRCK,
    ES8311_FADE_256LRCK,
    ES8311_FADE_512LRCK,
    ES8311_FADE_1024LRCK,
    ES8311_FADE_2048LRCK,
    ES8311_FADE_4096LRCK,
    ES8311_FADE_8192LRCK,
    ES8311_FADE_16384LRCK,
    ES8311_FADE_32768LRCK,
    ES8311_FADE_65536LRCK
} es8311_fade_t;

typedef enum es8311_resolution_t {
    ES8311_RESOLUTION_16 = 16,
    ES8311_RESOLUTION_18 = 18,
    ES8311_RESOLUTION_20 = 20,
    ES8311_RESOLUTION_24 = 24,
    ES8311_RESOLUTION_32 = 32
} es8311_resolution_t;

typedef struct es8311_clock_config_t {
    bool mclk_inverted;
    bool sclk_inverted;
    bool mclk_from_mclk_pin;
    int  mclk_frequency;
    int  sample_frequency;
} es8311_clock_config_t;

es8311_handle_t es8311_create(uint16_t dev_addr);
void es8311_delete(es8311_handle_t dev);
esp_err_t es8311_init(es8311_handle_t dev, const es8311_clock_config_t *const clk_cfg,
                      const es8311_resolution_t res_in, const es8311_resolution_t res_out);
esp_err_t es8311_voice_volume_set(es8311_handle_t dev, int volume, int *volume_set);
esp_err_t es8311_sample_frequency_config(es8311_handle_t dev, int mclk_frequency, int sample_frequency);

/** Codec I2C + register init (Wire must already be started). Returns false on failure (non-fatal). */
bool yoradio_es8311_bringup(int sample_hz, int mclk_mul);

/** Map yoRadio 0..100 UI volume to codec DAC level. */
void yoradio_es8311_apply_volume(uint8_t vol_0_100);

/** Reconfigure clock dividers when I2S sample rate changes (best-effort). */
void yoradio_es8311_on_sample_rate(int sample_hz);

#ifdef __cplusplus
}
#endif

#else

static inline bool yoradio_es8311_bringup(int /*sample_hz*/, int /*mclk_mul*/ = 256) { return true; }
static inline void yoradio_es8311_apply_volume(uint8_t /*vol*/) {}
static inline void yoradio_es8311_on_sample_rate(int /*sample_hz*/) {}

#endif