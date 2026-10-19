/* April's Speaker: Waveshare ESP32-S3-Zero + Pimoroni Pirate Audio 3W Amp HAT
 * (ST7789 240x240, PCM5102A I2S DAC + 3 W amp, 4 buttons) + microSD breakout on the TFT's SPI.
 *
 *   pio run -e aprils-speaker                 build
 *   pio run -e aprils-speaker -t upload       (refused unless the S3-Zero's MAC is on upload_port,
 *                                              see platformio_private.ini.example)
 * Wiring: WIRING.md / PINOUT.md in the aprils-speaker repo (moving here in the docs step)
 */
#ifndef boards_aprils_speaker_h
#define boards_aprils_speaker_h

#define SHOW_LOGOS_DEFAULT 1

#define L10N_LANGUAGE EN
#define CLOCK_TTS_LANGUAGE "en"

/******************************************/
/* Battery — add fuel gauge on custom PCB later */
#define BATTERY_ENABLED 0
#define HIDE_BAT 1
//#define BATTERY_ADC_PIN 1
//#define BATTERY_ADC_AUTO_SCALE 1
//#define BATTERY_VOLT_EMPTY 3.30f
//#define BATTERY_VOLT_FULL  4.20f
//#define FOOTER_BATTERY_COLORIZE 1
/******************************************/

//#define NEOSTATUS_ENABLE 0
#define NEOSTATUS_ENABLE 1
#define LED_BUILTIN_S3    255
#define USE_BUILTIN_LED false
/******************************************/

/*****************************************/
// Pirate Audio: ST7789 240x240
#define DSP_MODEL DSP_ST7789_240
#define ROTATE_90 true
/* Adafruit rotation 0–3. Enclosure needs +90° CW from Pimoroni default (rot 1).
 * Full-bleed 240×240 — zero glass offset (also in platformio.ini -D flags). */
#define TFT_ROTATION 2
/*****************************************/
#define TFT_SPI_FREQ 20000000UL  /* shared HSPI with SD — 20 MHz stable on bench */
#define DSP_HSPI 1
#define TFT_COL_OFFSET 0
#define TFT_ROW_OFFSET 0

/*****************************************/
/* Waveshare ESP32-S3-Zero + Pirate Audio HAT
 * Castellated GPIO 1–13; module pads 43/44 (UART); GPIO 21 onboard NeoPixel */
#define TFT_CS  4
#define TFT_DC  8
#define TFT_RST -1
#define TFT_SCLK 12
#define TFT_MOSI 13
#define TFT_MISO 43   /* SD DO — shared SPI with TFT (pad 43) */
#define BRIGHTNESS_PIN 2
#define I2S_DOUT 5
#define I2S_BCLK 6
#define I2S_LRC  7

/* Pirate Audio buttons (active low; HAT pull-ups) — see WIRING.md */
#define BTN_LEFT    3           /* HAT A */
#define BTN_RIGHT   9           /* HAT B */
#define BTN_CENTER  1           /* HAT X — select (Pi pin 36) */
#define BTN_MODE    10          /* HAT Y — mode   (Pi pin 18; old HATs: pin 38) */
/* yoRadio default: LEFT=vol−, RIGHT=vol+ — invert so A=up, B=down (volume screen only) */
#define BTN_VOLUME_INVERT_LR 1
/* Invert playlist scroll so A/B match on-screen in station list */
#define BTN_STATIONS_INVERT_LR 1

#define BUILTIN_NEOPIXEL_PIN 21
#define BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS 32

/* microSD breakout: CS=44, shares TFT SPI (SCK 12, MOSI 13, MISO 43) — WIRING.md */
#define SDC_CS       44
#define SD_HSPI      1
#define SD_SHARE_TFT_SPI 1
#define SD_FORCE_REINDEX 0   /* set 1 once to force a full card rescan */

/* BT companion (Node ESP32 / NodeMCU-32S running BT-A2DP-Source-HiGrow env nodemcu32s_bt_companion):
 * A2DP receive (phone -> speaker, I2S bus handover like the ProS3) and send (speaker -> BT headphones).
 * Compiled in but INERT until the wiring is confirmed: BT_COMPANION_PINS_TODO keeps it from touching
 * any pin and redirects Bluetooth mode to web. GPIO 43/44 are taken by the SD card here.
 * TODO(Cain): confirm S3-Zero pads for UART TX/RX + wake, then set the three pins and
 *             BT_COMPANION_PINS_TODO 0. Placeholders below are NOT wired. */
#define BT_COMPANION_ENABLE 1
#define BT_COMPANION_PINS_TODO 1
#define BT_COMPANION_UART_PORT 1
#define BT_COMPANION_UART_BAUD 115200
#define BT_COMPANION_UART_TX   15   /* TODO placeholder -> Node ESP32 RX GPIO16 */
#define BT_COMPANION_UART_RX   16   /* TODO placeholder <- Node ESP32 TX GPIO17 */
#define BT_COMPANION_WAKE_PIN  11   /* TODO placeholder -> Node ESP32 WAKE GPIO33 */
#define BT_COMPANION_WAKE_PULSE_MS 600
#define BT_RX_VOL_GAIN_PCT 100
#define BT_COMPANION_SINK_NAME "HiFi-IIS"
#define BT_COMPANION_RX_LOCAL_NAME "April's Speaker"

//#define BT_COMPANION_UART_PORT 1
//#define BT_COMPANION_UART_BAUD 115200
//#define BT_COMPANION_UART_TX 43
//#define BT_COMPANION_UART_RX 44
//#define BT_COMPANION_WAKE_PIN 11
//#ifndef BT_COMPANION_WAKE_PULSE_MS
//  #define BT_COMPANION_WAKE_PULSE_MS 600
//#endif
//#define BT_COMPANION_SINK_NAME "HiFi-IIS"
/*****************************************/

/*****************************************/
// Pirate Audio buttons (active low)
#define ENC_BTNL 255
#define ENC_BTNR 255
#define ENC_BTNB 255
#define ENC_INTERNALPULLUP true

#define BTN_INTERNALPULLUP true   /* + HAT pull-ups; helps if a button line is still floating */
/* Do not redefine BTN_LEFT/RIGHT/MODE/CENTER here — pins are in the map above. */
//#define BTN_LEFT    255
//#define BTN_RIGHT   255
//#define BTN_MODE    255
#define BTN_UP      255
#define BTN_DOWN    255

#define MODE_BUTTON_LED_PIN 255
#define MODE_BUTTON_LED_ACTIVE_HIGH 1
/********************************************/

/********************************************/
#define BIAMP_ENABLE 0          /* software DSP bi-amp — off (hardware bi-amp via L/R amp) */
#define MUTE_PIN  255
#define MUTE_VAL  LOW
/********************************************/

/*****************************************/
#define IR_PIN 255
/*****************************************/

/*****************************************/
#define SDSPISPEED 10000000
#define SD_ALBUM_ART_ENABLE 0
/*****************************************/

/********************************************/
#define SD_AUTORESUME_ON_MODE_SWITCH 1
#define TRACKPOS_IN_VU_SLOT 1   /* 240x240: SD/podcast track time sits where the VU meter goes */
#ifndef SHOW_LOGOS_DEFAULT
  #define SHOW_LOGOS_DEFAULT 0
#endif
#define CLOCK_TTS_ENABLED false
#define I2S_INTERNAL false

#define WAKE_PIN1 255
#define WAKE_PIN2 255           /* was BTN_CENTER — tied deep-sleep wake to select button */
#define WAKE_PIN3 255
#define MODE_BUTTON_LONG_PRESS_SLEEP 0   /* Y = mode cycle; no accidental deep sleep on bench */
#define AUTO_DEEPSLEEP_IDLE_MINUTES 0
#define AUTO_DEEPSLEEP_BATT_PCT 5

#define FADE_PERIOD 300
#define BH1750_ENABLE 0

#ifndef MQTT_QUIET_LOGS
  #define MQTT_QUIET_LOGS 1
#endif
/* MQTT enabled via mqttoptions.h (MQTT_ROOT_TOPIC) */

/* ***************************************** */
/* DIAGNOSTICS */
//#define DSP_DIAG_LOG 1
//#define AUDIO_DIAG_LOG 1
//#define BT2_DIAG_LOG 1
/* ***************************************** */

#endif
