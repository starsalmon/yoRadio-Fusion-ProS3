/* Freenove FNK0104B — ESP32-S3R8 (8 MB OPI PSRAM, 16 MB flash), 2.8" ILI9341 + FT6336U + ES8311 + SDMMC.
 * Env: freenove-fnk0104b  (-DYORADIO_BOARD_FNK0104B=1)
 * Verified pins (Cain ESP32 Helper / board docs unless noted).
 */
#ifndef boards_fnk0104b_h
#define boards_fnk0104b_h

#define L10N_LANGUAGE EN
#define CLOCK_TTS_LANGUAGE "en"
#define SHOW_LOGOS_DEFAULT 1

/******************************************/
/* Display: ILI9341 240x320 SPI (FSPI pins per Freenove 2.8" kit) */
#define DSP_MODEL DSP_ILI9341
#define TFT_SPI_FREQ 40000000UL   /* >40 MHz glitches on this panel */
#define DSP_HSPI 1                /* explicit SPI2 + pins below */
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1
#define BRIGHTNESS_PIN 45         /* active HIGH */
/* IPS glass needs INVON to look normal; the "invert display" setting toggles relative to that.
   BGR pixel order is the Adafruit_ILI9341 setRotation() default. */
#define YORADIO_ILI9341_INVERT_ON 1
/* Landscape layout (320x240) like ProS3 ILI9341 — rotation from flipscreen / factory default */
#define TFT_ROTATION 3
/******************************************/

/******************************************/
/* Touch: FT6336U @ 0x38 */
#define TS_MODEL TS_MODEL_FT6X36
#define TS_SDA 16
#define TS_SCL 15
#define TS_INT 17
#define TS_RST 18
/******************************************/

/******************************************/
/* I2C shared: touch + ES8311 (0x18) + MPU6050 (0x68, Cain add-on) */
#ifndef ES8311_I2C_ADDR
#define ES8311_I2C_ADDR 0x18
#endif
#define YORADIO_ES8311 1
#define ES8311_MCLK_MULTIPLE 256
/* Fixed codec DAC level (es8311_voice_volume_set scale: 75 -> REG32 0xBF = 0 dB). yoRadio's
   volume is applied once, digitally in Audio (as with the plain I2S DACs), not here too. */
#define ES8311_DAC_VOLUME 75

/* I2S — Freenove FNK0104B/AB pin map */
#define I2S_MCLK 4
#define I2S_BCLK 5
#define I2S_LRC  7
#define I2S_DOUT 8
/* ES8311 mic data (not used by yoRadio playback path) */
#define I2S_DIN  6

/* GPIO1 (AUDIO_EN) drives the FM8002E SHUTDOWN pin directly (Freenove 2.8" schematic, R26 10K).
   FM8002E datasheet: SHUTDOWN high = amp off (4.2 uA), low = amp on. So the muted level is HIGH;
   Freenove's sketches just hold it LOW. Same MUTE_PIN logic as ProS3 / April's Speaker. */
#define MUTE_PIN 1
#define MUTE_VAL HIGH          /* HIGH = FM8002E shut down (boot / idle until first play) */
#define BIAMP_ENABLE 0
/******************************************/

/******************************************/
/* SD: on-board microSD in 4-bit SDMMC (not shared TFT SPI) */
#define YORADIO_SD_MMC 1
#define SD_MMC_CLK 38
#define SD_MMC_CMD 40
#define SD_MMC_D0  39
#define SD_MMC_D1  41
#define SD_MMC_D2  48
#define SD_MMC_D3  47
#define SDC_CS     255
#define SD_SHARE_TFT_SPI 0
#define SD_FORCE_REINDEX 0
#define SD_ALBUM_ART_ENABLE 0
#define SD_AUTORESUME_ON_MODE_SWITCH 1
/******************************************/

/******************************************/
/* Battery: GPIO9 + 2:1 divider (Helper); Bruce sample firmware uses GPIO8 — prefer 9 */
#define BATTERY_ENABLED 1
#define BATTERY_ADC_PIN 9
#define BATTERY_ADC_SCALE 2.0f
#define BATTERY_VOLT_EMPTY 3.30f
#define BATTERY_VOLT_FULL  4.20f
#define FOOTER_BATTERY_COLORIZE 1
/******************************************/

/******************************************/
/* WS2812: Helper verified GPIO42 (Bruce uses 40 on some sketches) */
#define NEOSTATUS_ENABLE 1
#define NEOSTATUS_PIN 42
#define NEOSTATUS_COUNT 8
#define NEOSTATUS_BRIGHTNESS 200
#define BUILTIN_NEOPIXEL_PIN 255
#define LED_BUILTIN_S3 255
#define USE_BUILTIN_LED false
/******************************************/

/******************************************/
/* Controls: BOOT button */
#define BTN_MODE 0
#define BTN_INTERNALPULLUP true
#define ENC_BTNL 255
#define ENC_BTNR 255
#define ENC_BTNB 255
#define ENC_INTERNALPULLUP true
#define BTN_LEFT 255
#define BTN_RIGHT 255
#define BTN_UP 255
#define BTN_DOWN 255
#define IR_PIN 255
#define MODE_BUTTON_LED_PIN 255
/******************************************/

/******************************************/
/* MPU6050 @ 0x68 on the SAME I2C bus as touch + ES8311 (SDA16/SCL15, begun in config.cpp):
   orient uses Wire (shared), never begins/ends it. Auto-rotate 180 only, like the radio.
   Mount (on its edge): sensor X = screen normal. GoL (TFT_eSPI rotation 1, y down) measured
   gravity screenX=+Z, screenY=-Y. yoRadio's base rotation is 3 (flipscreen off), 180 from
   that, so in orient's frame (H = toward screen right, V = toward screen top, reads +1 g
   upright): H = +Z, V = +Y, N = X. Wrong left/right = ORIENT_INVERT_X, upside down = _Y. */
#define MPU6050_I2C_ADDR 0x68
#define ORIENT_ENABLE      1
#define ORIENT_SHARED_WIRE 1
#define ORIENT_SDA         TS_SDA
#define ORIENT_SCL         TS_SCL
#define ORIENT_ALL_FOUR    0
#define ORIENT_H_AXIS      2
#define ORIENT_V_AXIS      1
#define ORIENT_N_AXIS      0
#define ORIENT_AXIS_SWAP   0
#define ORIENT_INVERT_X    0
#define ORIENT_INVERT_Y    0
/******************************************/

/******************************************/
/* MQTT: credentials/host from the shared mqttoptions.h; own root so it never drives or mirrors
   the radio's topics / HA entities (src/core/options.h applies the override). */
#define BOARD_MQTT_ROOT_TOPIC "yoradio/fnk0104b/"
/******************************************/

/******************************************/
/* No BT companion on this unit */
#define BT_COMPANION_ENABLE 0
/******************************************/

/******************************************/
#define CLOCK_TTS_ENABLED false
#define I2S_INTERNAL false
#define WAKE_PIN1 BTN_MODE
#define WAKE_PIN2 255
#define WAKE_PIN3 255
#define AUTO_DEEPSLEEP_IDLE_MINUTES 0   /* avoid sleep until bench-validated */
#define AUTO_DEEPSLEEP_BATT_PCT 5
#define FADE_PERIOD 300
#define BH1750_ENABLE 0
#ifndef MQTT_QUIET_LOGS
  #define MQTT_QUIET_LOGS 1
#endif
#define BOOT_SCREEN_MIN_MS 1500
/******************************************/

/* ***************************************** */
/* DIAGNOSTICS */
//#define DSP_DIAG_LOG 1
//#define AUDIO_DIAG_LOG 1
/* ***************************************** */

#endif