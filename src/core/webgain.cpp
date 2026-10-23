#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "webgain.h"
extern volatile float g_webGainLinForAudio;
static int s_db = 0;
static void apply(){ g_webGainLinForAudio = powf(10.0f, (float)s_db / 20.0f); }
void webgain_init(){ Preferences p; const bool ok = p.begin("yrxgain", true); uint8_t raw = 0xEE;
  if(ok){ raw = p.getUChar("webgain", 0); s_db = raw; p.end(); }
  if(s_db>12) s_db=12; apply();
  Serial.printf("[GAIN] boot: nvs_begin=%s raw=%u db=%d lin=%.4f (unity path=%s)\n", ok?"ok":"FAIL(namespace absent -> 0 dB)",
                (unsigned)raw, s_db, (double)g_webGainLinForAudio, (g_webGainLinForAudio <= 1.0001f)?"yes":"no"); }
void webgain_set(int db, bool save){ if(db<0) db=0; if(db>12) db=12; s_db=db; apply();
  if(save){ Preferences p; if(p.begin("yrxgain", false)){ p.putUChar("webgain",(uint8_t)db); p.end(); } }
  Serial.printf("[WEBGAIN] set %d dB\n", db); }
int webgain_get(){ return s_db; }
