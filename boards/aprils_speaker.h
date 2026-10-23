/* April's Speaker: Unexpected Maker FeatherS3 (ESP32-S3, 16 MB QSPI flash, 8 MB QSPI PSRAM)
 * + Pimoroni Pirate Audio 3W Amp HAT (ST7789 240x240, 2x MAX98357A I2S DAC+3 W amp, 4 buttons)
 * + microSD breakout on the display's SPI bus + Node ESP32 BT companion.
 *
 *   pio run -e aprils-speaker                 build
 *   pio run -e aprils-speaker -t upload       (refused unless upload_port is the FeatherS3 and it is the
 *                                              only Espressif USB device, see platformio_private.ini.example)
 * Pins: PROPOSED (main PCB not final). Table + reasons: docs/APRILS_SPEAKER.md.
 * FeatherS3 fixed functions (UM pinout card / arduino-esp32 variant um_feathers3): LDO2 EN + RGB power 39,
 * RGB data 40, VBAT sense 2 (442k/160k), VBUS sense 34, ambient light 4, blue LED 13, I2C 8/9 (+15/16),
 * SPI SCK 36 / MOSI 35 / MISO 37, UART0 43/44, straps 0/3, USB 19/20 (not broken out).
 */
#ifndef boards_aprils_speaker_h
#define boards_aprils_speaker_h

#define SHOW_LOGOS_DEFAULT 1

#define L10N_LANGUAGE EN
#define CLOCK_TTS_LANGUAGE "en"

/******************************************/
/* FeatherS3 LDO2 (3V3_2): IO39 HIGH enables it; it also powers the onboard RGB LED and shuts down in
 * deep sleep. yoradio_user.cpp enables it at boot (ARDUINO_FEATHERS3) and then holds SD/TFT CS high. */
#define LDO2_ENABLE   39
/******************************************/
/* Battery: onboard VBAT sense on GPIO2 (divider 442k / 160k -> x3.7625), 5 V presence on GPIO34 */
#define BATTERY_ENABLED 1
#define BATTERY_ADC_PIN 2
#define BATTERY_ADC_SCALE 3.7625f
#define BATTERY_VOLT_EMPTY 3.30f
#define BATTERY_VOLT_FULL  4.20f
#define CHARGE_SENSE_PIN 34
#define HIDE_BAT 1              /* no battery footer layout for the 240x240 panel yet (MQTT still reports it) */
/******************************************/

#define NEOSTATUS_ENABLE 1
#define LED_BUILTIN_S3    255
#define USE_BUILTIN_LED false
/******************************************/

/*****************************************/
// Pirate Audio: ST7789 240x240
#define DSP_MODEL DSP_ST7789_240
#define ROTATE_90 true
/* Adafruit rotation 0-3. Enclosure needs +90 deg CW from Pimoroni default (rot 1).
 * Full-bleed 240x240, zero glass offset. */
#define TFT_ROTATION 2
/*****************************************/
/* Display + SD on the FeatherS3's default SPI (SCK 36 / MOSI 35 / MISO 37, variant defaults), the
 * same shared-bus path as the ProS3 (SPI.begin() in sdpark_boot, no DSP_HSPI / SPI2). */
#define TFT_SPI_FREQ 20000000UL  /* shared with SD; 20 MHz is the conservative shared-bus rate (ProS3 bench-proven) */
#define TFT_COL_OFFSET 0
#define TFT_ROW_OFFSET 0

/*****************************************/
/* FeatherS3 pin map (PROPOSED) */
#define TFT_CS  38              /* non-RTC pin: RTC pins are kept for buttons */
#define TFT_DC  33              /* non-RTC */
#define TFT_RST -1              /* Pirate Audio has no panel reset */
#define BRIGHTNESS_PIN 10       /* backlight PWM */
#define I2S_BCLK 17
#define I2S_LRC  18
#define I2S_DOUT 14             /* no MCLK needed (MAX98357A). HAT amp enable = BCM25 pin 22, active high: TBD, no GPIO assigned */

/* Pirate Audio buttons (active low; HAT pull-ups). All on RTC GPIOs so any can wake from deep sleep. */
#define BTN_LEFT    5           /* HAT A */
#define BTN_RIGHT   6           /* HAT B */
#define BTN_CENTER  12          /* HAT X - select (Pi pin 36) */
#define BTN_MODE    11          /* HAT Y - mode   (Pi pin 18; old HATs: pin 38) */
/* yoRadio default: LEFT=vol-, RIGHT=vol+ - invert so A=up, B=down (volume screen only) */
#define BTN_VOLUME_INVERT_LR 1
/* Invert playlist scroll so A/B match on-screen in station list */
#define BTN_STATIONS_INVERT_LR 1

/* NeoStatus on the FeatherS3's onboard RGB LED (GPIO40, powered from LDO2) */
#define BUILTIN_NEOPIXEL_PIN 40
#define BUILTIN_NEOPIXEL_BOOT_BRIGHTNESS 32

/* microSD breakout: CS 1, SCK/MOSI/MISO shared with the TFT (36/35/37) */
#define SDC_CS       1
#define SD_SHARE_TFT_SPI 1
#define SD_FORCE_REINDEX 0   /* set 1 once to force a full card rescan */

/* BT companion (Node ESP32 / NodeMCU-32S running BT-A2DP-Source-HiGrow env nodemcu32s_bt_companion):
 * A2DP receive (phone -> speaker, I2S bus handover like the ProS3) and send (speaker -> BT headphones).
 * UART on the FeatherS3's UART0 pads 43/44 (free: Serial is native USB), same pins as the ProS3.
 * Still INERT: BT_COMPANION_PINS_TODO keeps it off every pin and redirects Bluetooth mode to web.
 * TODO(Cain): once the companion is wired as below, set BT_COMPANION_PINS_TODO 0. */
#define BT_COMPANION_ENABLE 1
#define BT_COMPANION_PINS_TODO 1
#define BT_COMPANION_UART_PORT 1
#define BT_COMPANION_UART_BAUD 115200
#define BT_COMPANION_UART_TX   43   /* -> Node ESP32 RX GPIO16 (proposed) */
#define BT_COMPANION_UART_RX   44   /* <- Node ESP32 TX GPIO17 (proposed) */
#define BT_COMPANION_WAKE_PIN  7    /* -> Node ESP32 WAKE GPIO33 (EXT0, active high) (proposed) */
#define BT_COMPANION_WAKE_PULSE_MS 600
#define BT_RX_VOL_GAIN_PCT 100
#define BT_COMPANION_SINK_NAME "HiFi-IIS"
#define BT_COMPANION_RX_LOCAL_NAME "April's Speaker"
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

#define WAKE_PIN1 BTN_MODE      /* Y wakes from deep sleep (low-battery sleep only; idle sleep is off) */
#define WAKE_PIN2 255
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
