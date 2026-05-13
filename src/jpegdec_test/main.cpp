#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <Adafruit_ILI9341.h>
#include <JPEGDEC.h>

// Reuse pin definitions from this repo.
#include "../../myoptions.h"

static Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
static JPEGDEC jpeg;
static bool g_spiffsOk = false;
static bool g_sdOk = false;

static int jpegDrawCallback(JPEGDRAW* pDraw) {
  if (!pDraw || !pDraw->pPixels) return 0;

  // JPEGDEC gives us RGB565 blocks; draw them directly.
  tft.drawRGBBitmap((int16_t)pDraw->x, (int16_t)pDraw->y, (uint16_t*)pDraw->pPixels, (int16_t)pDraw->iWidth, (int16_t)pDraw->iHeight);
  yield();
  return 1;
}

static bool decodeJpegFromFileToScreenAutoFit(fs::FS& fs, const char* path) {
  File f = fs.open(path, "r");
  if (!f) return false;

  const size_t len = (size_t)f.size();
  if (len < 128 || len > (512u * 1024u)) {
    f.close();
    return false;
  }

  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) {
    f.close();
    return false;
  }

  const size_t got = f.read(buf, len);
  f.close();
  if (got != len) {
    free(buf);
    return false;
  }

  // JPEGDEC outputs to the callback in RGB565; we want uint16_t values to match ESP32 endianness.
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  const bool opened = jpeg.openRAM(buf, (int)len, jpegDrawCallback);
  if (!opened) {
    free(buf);
    jpeg.close();
    return false;
  }

  const int w = jpeg.getWidth();
  const int h = jpeg.getHeight();

  // Choose a scale that fits on-screen.
  int opts = 0;
  int factor = 1;
  while (factor < 8 && (w / factor > (int)tft.width() || h / factor > (int)tft.height())) {
    factor *= 2;
  }
  if      (factor == 8) opts = JPEG_SCALE_EIGHTH;
  else if (factor == 4) opts = JPEG_SCALE_QUARTER;
  else if (factor == 2) opts = JPEG_SCALE_HALF;

  const int sw = (w + factor - 1) / factor;
  const int sh = (h + factor - 1) / factor;
  const int x = (int)tft.width()  > sw ? ((int)tft.width()  - sw) / 2 : 0;
  const int y = (int)tft.height() > sh ? ((int)tft.height() - sh) / 2 : 0;

  Serial.printf("JPEG: %s (%dx%d) -> (%dx%d) scale=%dx opts=0x%x at (%d,%d)\n", path, w, h, sw, sh, factor, opts, x, y);
  const bool ok = jpeg.decode(x, y, opts) != 0;
  jpeg.close();
  free(buf);
  return ok;
}

void setup() {
  delay(250);
  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println("PROS3 JPEGDEC test");

#if (BRIGHTNESS_PIN != 255)
  // Ensure the TFT backlight is actually on for this test.
  pinMode(BRIGHTNESS_PIN, OUTPUT);
  analogWrite(BRIGHTNESS_PIN, 255);
#endif

#if defined(ARDUINO_PROS3)
  // Keep the PROS3-specific power/antenna init minimal but consistent.
#if defined(LDO2_ENABLE) && (LDO2_ENABLE != 255)
  pinMode(LDO2_ENABLE, OUTPUT);
  digitalWrite(LDO2_ENABLE, HIGH);
#endif
#if defined(RF_SWITCH) && (RF_SWITCH != 255) && defined(PROS3_USE_EXTERNAL_ANTENNA) && (PROS3_USE_EXTERNAL_ANTENNA)
  pinMode(RF_SWITCH, OUTPUT);
  digitalWrite(RF_SWITCH, HIGH);
#endif
#endif

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("JPEGDEC test");

  g_spiffsOk = SPIFFS.begin(true);
  if (!g_spiffsOk) {
    Serial.println("SPIFFS.begin failed");
    tft.println("SPIFFS fail");
  }

  if (SDC_CS != 255) {
    g_sdOk = SD.begin(SDC_CS);
    if (!g_sdOk) {
      Serial.println("SD.begin failed");
      tft.println("SD fail");
    } else {
      Serial.println("SD OK");
      tft.println("SD OK");
    }
  }

  tft.println();
  tft.println("Looking for:");
  tft.println("/test.jpg");
}

void loop() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (last != 0 && (uint32_t)(now - last) < 3000) return;
  last = now;

  tft.fillScreen(ILI9341_BLACK);

  bool ok = false;

  // Try SPIFFS first.
  if (g_spiffsOk) {
    ok = decodeJpegFromFileToScreenAutoFit(SPIFFS, "/test.jpg");
  }

  // Fallback to SD.
  if (!ok && g_sdOk) {
    ok = decodeJpegFromFileToScreenAutoFit(SD, "/test.jpg");
  }

  // Draw a small status footer so we don't cover the image.
  const int barH = 18;
  tft.fillRect(0, (int)tft.height() - barH, (int)tft.width(), barH, ok ? ILI9341_DARKGREEN : ILI9341_RED);
  tft.setCursor(2, (int)tft.height() - barH + 2);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.print(ok ? "OK" : "FAIL");

  Serial.println(ok ? "decode OK" : "decode FAIL");
}

