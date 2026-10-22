#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "webgain.h"
extern volatile float g_webGainLinForAudio;
static int s_db = 0;
static void apply(){ g_webGainLinForAudio = powf(10.0f, (float)s_db / 20.0f); }
void webgain_init(){ Preferences p; if(p.begin("yrxgain", true)){ s_db = p.getUChar("webgain", 0); p.end(); } if(s_db>12) s_db=12; apply(); Serial.printf("[WEBGAIN] %d dB\n", s_db); }
void webgain_set(int db, bool save){ if(db<0) db=0; if(db>12) db=12; s_db=db; apply();
  if(save){ Preferences p; if(p.begin("yrxgain", false)){ p.putUChar("webgain",(uint8_t)db); p.end(); } }
  Serial.printf("[WEBGAIN] set %d dB\n", db); }
int webgain_get(){ return s_db; }
