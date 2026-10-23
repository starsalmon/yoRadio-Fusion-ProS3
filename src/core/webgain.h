#pragma once
// Web-stream gain 0..12 dB (NVS "yrxgain"/"webgain"; not in the EEPROM store, so no layout change).
void webgain_init();
void webgain_set(int db, bool save);
int  webgain_get();
