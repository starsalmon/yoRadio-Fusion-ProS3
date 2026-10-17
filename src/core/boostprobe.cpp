#include <Arduino.h>
#include "boostprobe.h"
#include "dsplog.h"
#include "../battery/battery.h"

#ifndef BOOSTPROBE_LOWBAT_PIN
#define BOOSTPROBE_LOWBAT_PIN 6   // LiPo SHIM LOW_BAT -> ProS3 IO6 (schematic)
#endif

static volatile bool s_busy = false;
static uint16_t s_beforeMv = 0;
static bool s_beforeOk = false;
static int s_beforeLow = -1;

static void probeTask(void*) {
  const uint32_t t0 = micros();
  uint16_t mn = 0xFFFF, mx = 0, firstMv = 0; uint32_t n = 0, fails = 0, lowCnt = 0, pinN = 0;
  uint32_t minAtUs = 0;
  // Burst: ~20 ms, back-to-back I2C reads + IO6 polls.
  while ((uint32_t)(micros() - t0) < 20000) {
    uint16_t mv;
    if (battery_read_vcell_mv(&mv)) {
      if (!n) firstMv = mv;
      if (mv < mn) { mn = mv; minAtUs = micros() - t0; }
      if (mv > mx) mx = mv;
      n++;
    } else fails++;
    pinN++; if (digitalRead(BOOSTPROBE_LOWBAT_PIN) == LOW) lowCnt++;
  }
  DSPLOG("VBAT boost-on burst 20ms: before=%s%umV first=%umV min=%umV(@%luus) max=%umV dip=%dmV n=%lu fail=%lu | LOW_BAT(IO6) before=%d low=%lu/%lu",
         s_beforeOk ? "" : "n/a ", s_beforeMv, firstMv, mn == 0xFFFF ? 0 : mn, (unsigned long)minAtUs, mx,
         (s_beforeOk && n) ? (int)s_beforeMv - (int)mn : 0, (unsigned long)n, (unsigned long)fails,
         s_beforeLow, (unsigned long)lowCnt, (unsigned long)pinN);
  // The gauge only refreshes VCELL periodically, so also follow it for ~1 s.
  uint16_t mn2 = 0xFFFF, mx2 = 0; uint32_t low2 = 0;
  for (int i = 0; i < 20; i++) {
    vTaskDelay(pdMS_TO_TICKS(50));
    uint16_t mv;
    if (battery_read_vcell_mv(&mv)) { if (mv < mn2) mn2 = mv; if (mv > mx2) mx2 = mv; }
    if (digitalRead(BOOSTPROBE_LOWBAT_PIN) == LOW) low2++;
  }
  DSPLOG("VBAT boost-on follow 1s: min=%umV max=%umV LOW_BAT low=%lu/20",
         mn2 == 0xFFFF ? 0 : mn2, mx2, (unsigned long)low2);
  s_busy = false;
  vTaskDelete(nullptr);
}

void boostprobe_beforeOn() {
  static bool s_pinInit = false;
  if (!s_pinInit) { pinMode(BOOSTPROBE_LOWBAT_PIN, INPUT_PULLUP); s_pinInit = true; delayMicroseconds(50); }
  if (s_busy) return;
  s_beforeOk = battery_read_vcell_mv((uint16_t*)&s_beforeMv);
  s_beforeLow = digitalRead(BOOSTPROBE_LOWBAT_PIN);
  s_busy = true;
  // High priority so the burst isn't starved; it only lasts ~20 ms then mostly sleeps.
  if (xTaskCreatePinnedToCore(probeTask, "bstprobe", 3072, nullptr, 20, nullptr, 1) != pdPASS) s_busy = false;
}
