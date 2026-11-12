#pragma once
#include <stdint.h>
// MPU6050 auto screen rotation (ORIENT_ENABLE in the board header; default off -> all stubs).
// Own I2C bus (Wire1), never the fuel-gauge/ALS bus on Wire. Settings in NVS "yrxorient"
// (not the EEPROM store, so no layout change). Auto rotation never writes config.store.flipscreen.
void    orient_init();               // setup(): loads NVS, starts the sensor task
void    orient_loop();               // loop(): applies a pending rotation on the player screen only
uint8_t orient_quarter();            // extra quarter turns (0..3) on top of base/flip rotation
uint8_t orient_rotmode();            // 0 off, 1 auto 180, 2 auto all four
void    orient_setRotmode(uint8_t m, bool save);
bool    orient_lock();
void    orient_setLock(bool on, bool save);
uint8_t orient_maxMode();            // 1 (180 only) or 2 (all four), per board
bool    orient_present();
int     orient_diag(char* buf, int n);   // JSON: present/addr/raw accel/angle/rotation (MQTT <root>orient)            // MPU found and responding
// Screen-plane gravity (g, down = fluid pull). Uses last MPU sample from the orient task.
bool    orient_gravityScreen(float* gx, float* gy);
int     orient_remapButton(int id);  // physical button index -> role index for the current rotation
