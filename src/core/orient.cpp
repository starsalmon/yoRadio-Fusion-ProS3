#include <Arduino.h>
#include "options.h"
#include "orient.h"

#ifndef ORIENT_ENABLE
  #define ORIENT_ENABLE 0
#endif

#if ORIENT_ENABLE
#include <Wire.h>
#include <Preferences.h>
#include <math.h>
#include "display.h"
#include "config.h"

#ifndef ORIENT_SDA
  #error "ORIENT_ENABLE needs ORIENT_SDA / ORIENT_SCL in the board header"
#endif
#ifndef ORIENT_ALL_FOUR
  #define ORIENT_ALL_FOUR 0          // 0: 180 only (two landscape rotations); 1: all four
#endif
#ifndef ORIENT_AXIS_SWAP
  #define ORIENT_AXIS_SWAP 0         // mount calibration: swap sensor X/Y
#endif
// Screen-frame mapping: which sensor axis (0=X,1=Y,2=Z) is the screen's horizontal (H),
// vertical (V, +V = gravity when upright) and normal (N). Only H/V (the screen plane) decide the
// rotation; N (forward/back pitch) only gates "too flat to tell". AXIS_SWAP/INVERT apply after.
#ifndef ORIENT_H_AXIS
  #define ORIENT_H_AXIS 0
#endif
#ifndef ORIENT_V_AXIS
  #define ORIENT_V_AXIS 1
#endif
#ifndef ORIENT_N_AXIS
  #define ORIENT_N_AXIS 2
#endif
#ifndef ORIENT_INVERT_X
  #define ORIENT_INVERT_X 0
#endif
#ifndef ORIENT_INVERT_Y
  #define ORIENT_INVERT_Y 0
#endif
#ifndef ORIENT_QUARTER_DIR
  #define ORIENT_QUARTER_DIR 1       // +1 / -1: which way setRotation steps relative to the tilt
#endif
#ifndef ORIENT_DEFAULT_MODE
  #define ORIENT_DEFAULT_MODE (ORIENT_ALL_FOUR ? 2 : 1)
#endif
#ifndef ORIENT_I2C_HZ
  #define ORIENT_I2C_HZ 100000
#endif

static const uint32_t POLL_MS = 200, HOLD_MS = 1000, MIN_GAP_MS = 2000, PLAYER_SETTLE_MS = 1000;
static const float FLAT_G = 0.8f, MIN_PLANE_G = 0.5f, HYST_DEG = 17.5f;

static uint8_t  s_addr = 0;
static volatile bool s_present = false;
static volatile uint8_t s_quarter = 0;      // applied
static volatile int8_t  s_want = -1;        // decided by the task, applied by orient_loop
static uint8_t  s_mode = ORIENT_DEFAULT_MODE;
static bool     s_lock = false;
static uint8_t  s_who = 0;
static volatile float s_raw[3] = {0,0,0}, s_ang = -1;

uint8_t orient_maxMode(){ return ORIENT_ALL_FOUR ? 2 : 1; }
uint8_t orient_quarter(){ return s_quarter; }

bool orient_gravityScreen(float* gx, float* gy) {
  if (!gx || !gy) return false;
  if (!s_present) return false;
  float ax = s_raw[ORIENT_H_AXIS], ay = s_raw[ORIENT_V_AXIS];
#if ORIENT_AXIS_SWAP
  { float t = ax; ax = ay; ay = t; }
#endif
#if ORIENT_INVERT_X
  ax = -ax;
#endif
#if ORIENT_INVERT_Y
  ay = -ay;
#endif
  // Accelerometer reports gravity as upward bias; fluid wants downward acceleration.
  *gx = -ax;
  *gy = -ay;
  return true;
}
uint8_t orient_rotmode(){ return s_mode; }
bool    orient_lock(){ return s_lock; }
bool    orient_present(){ return s_present; }
int orient_diag(char* buf, int n){
  return snprintf(buf, n, "{\"present\":%d,\"addr\":\"0x%02X\",\"who\":\"0x%02X\",\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"angle\":%.0f,\"rotation\":%d,\"mode\":%u,\"lock\":%d,\"map\":\"H%dV%dN%d\"}",
    s_present?1:0, s_addr, s_who, s_raw[0], s_raw[1], s_raw[2], s_ang, s_quarter*90, (unsigned)s_mode, s_lock?1:0,
    ORIENT_H_AXIS, ORIENT_V_AXIS, ORIENT_N_AXIS);
}

static void save(){
  Preferences p; if(p.begin("yrxorient", false)){ p.putUChar("rotmode", s_mode); p.putUChar("rotlock", s_lock?1:0); p.end(); }
}
void orient_setRotmode(uint8_t m, bool sv){ if(m > orient_maxMode()) m = orient_maxMode(); s_mode = m; if(sv) save();
  Serial.printf("[ORIENT] rotmode %u\n", (unsigned)m); }
void orient_setLock(bool on, bool sv){ s_lock = on; if(sv) save(); Serial.printf("[ORIENT] rotlock %u\n", (unsigned)on); }

static bool wr(uint8_t reg, uint8_t v){ Wire1.beginTransmission(s_addr); Wire1.write(reg); Wire1.write(v); return Wire1.endTransmission() == 0; }
static bool rd(uint8_t reg, uint8_t* buf, uint8_t n){
  Wire1.beginTransmission(s_addr); Wire1.write(reg);
  if(Wire1.endTransmission(false) != 0) return false;
  if(Wire1.requestFrom((int)s_addr, (int)n) != n) return false;
  for(uint8_t i=0;i<n;i++) buf[i] = Wire1.read();
  return true;
}
static bool probe(){
  const uint8_t cand[2] = {0x68, 0x69};
  for(uint8_t a : cand){
    s_addr = a; uint8_t who = 0;
    if(!rd(0x75, &who, 1)) continue;
    // 0x68 = MPU6050; 0x70/0x71/0x73/0x98 = MPU6500/9250/6886-style clones with the same accel map.
    s_who = who;
    if(who==0x68 || who==0x69 || who==0x70 || who==0x71 || who==0x73 || who==0x98){
      Serial.printf("[ORIENT] MPU at 0x%02X WHO_AM_I=0x%02X\n", a, who); return true;
    }
    Serial.printf("[ORIENT] 0x%02X answered WHO_AM_I=0x%02X (not an MPU), skipping\n", a, who);
  }
  return false;
}
static bool setupMpu(){
  return wr(0x6B, 0x80) && (delay(100), true)   // reset
      && wr(0x6B, 0x01)                         // wake, PLL on gyro X
      && wr(0x19, 9)                            // 100 Hz sample rate
      && wr(0x1A, 6)                            // DLPF ~5 Hz
      && wr(0x1C, 0x00);                        // +-2 g
}

static float angDiff(float a, float b){ float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f; return fabsf(d); }

static void orientTask(void*){
  vTaskDelay(pdMS_TO_TICKS(1500));
  if(!Wire1.begin(ORIENT_SDA, ORIENT_SCL, ORIENT_I2C_HZ)){ Serial.println("[ORIENT] Wire1.begin failed, auto-rotate off"); vTaskDelete(nullptr); }
  Wire1.setTimeOut(20);
  if(!probe() || !setupMpu()){ Serial.println("[ORIENT] no MPU6050 on the orient bus, auto-rotate off"); Wire1.end(); vTaskDelete(nullptr); }
  s_present = true;
  uint8_t cur = 0, cand = 0xFF; uint32_t candSince = 0, lastRot = 0; int fails = 0;
  for(;;){
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    uint8_t b[6];
    if(!rd(0x3B, b, 6)){
      if(++fails >= 10){ Serial.println("[ORIENT] I2C read failing, auto-rotate off"); s_present = false; Wire1.end(); vTaskDelete(nullptr); }
      continue;
    }
    fails = 0;
    float g[3];
    for(int k=0;k<3;k++){ g[k] = (int16_t)((b[2*k]<<8)|b[2*k+1]) / 16384.0f; s_raw[k] = g[k]; }
    float ax = g[ORIENT_H_AXIS], ay = g[ORIENT_V_AXIS], az = g[ORIENT_N_AXIS];
#if ORIENT_AXIS_SWAP
    { float t = ax; ax = ay; ay = t; }
#endif
#if ORIENT_INVERT_X
    ax = -ax;
#endif
#if ORIENT_INVERT_Y
    ay = -ay;
#endif
    cur = s_quarter;
    if(s_mode == 0){ if(cur != 0) s_want = 0; cand = 0xFF; continue; }
    if(s_lock){ cand = 0xFF; continue; }
    const float plane = sqrtf(ax*ax + ay*ay);
    if(fabsf(az) > FLAT_G || plane < MIN_PLANE_G){ s_ang = -1; cand = 0xFF; continue; }
    // Angle of gravity in the screen plane: 0 = "down" along +Y (base orientation).
    float a = atan2f(ax, ay) * 57.29578f; if(a < 0) a += 360.0f;
    s_ang = a;
    const float curDeg = (float)((cur * 90 * ORIENT_QUARTER_DIR + 360) % 360);
    uint8_t next = cur;
    if(s_mode >= 2){
      if(angDiff(a, curDeg) > 45.0f + HYST_DEG){
        int q = ((int)lroundf(a / 90.0f)) & 3;           // nearest quarter in tilt space
        next = (uint8_t)(((q * ORIENT_QUARTER_DIR) % 4 + 4) % 4);
      }
    }else{
      if(cur & 1) next = 0;                              // left mode 2 while sideways
      else if(angDiff(a, curDeg) > 90.0f + HYST_DEG) next = cur ^ 2;
    }
    if(next == cur){ cand = 0xFF; continue; }
    const uint32_t now = millis();
    if(next != cand){ cand = next; candSince = now; continue; }
    if(now - candSince >= HOLD_MS && now - lastRot >= MIN_GAP_MS){
      s_want = (int8_t)next; lastRot = now; cand = 0xFF;
    }
  }
}

void orient_init(){
  Preferences p;
  if(p.begin("yrxorient", true)){ s_mode = p.getUChar("rotmode", ORIENT_DEFAULT_MODE); s_lock = p.getUChar("rotlock", 0) != 0; p.end(); }
  if(s_mode > orient_maxMode()) s_mode = orient_maxMode();
  Serial.printf("[ORIENT] boot: rotmode=%u rotlock=%u SDA=%d SCL=%d\n", (unsigned)s_mode, (unsigned)s_lock, ORIENT_SDA, ORIENT_SCL);
  xTaskCreatePinnedToCore(orientTask, "orient", 3072, nullptr, 1, nullptr, 0);
}

void orient_loop(){
  static uint32_t playerSince = 0;
  if(display.mode() != PLAYER){ playerSince = 0; return; }   // boot, menus, SD list, playlist, sleep, screensaver
  // Screen off (dspon=0 keeps mode PLAYER) or panel asleep: no flip/redraw on a sleeping panel.
  // A pending rotation stays queued and is applied once the screen is back on.
  if(!config.store.dspon || display.panelAsleep){ playerSince = 0; return; }
  const uint32_t now = millis();
  if(playerSince == 0){ playerSince = now ? now : 1; return; }
  if(now - playerSince < PLAYER_SETTLE_MS) return;
  const int8_t w = s_want;
  if(w < 0) return;
  s_want = -1;
  if((uint8_t)w == s_quarter) return;
  s_quarter = (uint8_t)w;
  Serial.printf("[ORIENT] rotate -> %d deg\n", w * 90);
  display.flip();                                  // DspCore::flip takes the TFT bus lock
  display.putRequest(NEWMODE, CLEAR);
  display.putRequest(NEWMODE, PLAYER);
}

int orient_remapButton(int id){
#if defined(ORIENT_REMAP_BUTTONS) && ORIENT_REMAP_BUTTONS
  // Pirate Audio buttons clockwise around the panel: A(TL)=0, X(TR)=1, Y(BR)=7, B(BL)=2 (button[] indices).
  static const int ring[4] = {0, 1, 7, 2};
  const uint8_t k = s_quarter;
  if(k == 0) return id;
  for(int p = 0; p < 4; p++) if(ring[p] == id) return ring[(p + k) & 3];
#endif
  return id;
}

#else  // ORIENT_ENABLE == 0
void    orient_init(){}
void    orient_loop(){}
uint8_t orient_quarter(){ return 0; }
uint8_t orient_rotmode(){ return 0; }
void    orient_setRotmode(uint8_t, bool){}
bool    orient_lock(){ return false; }
void    orient_setLock(bool, bool){}
uint8_t orient_maxMode(){ return 0; }
bool    orient_present(){ return false; }
int     orient_diag(char* b, int n){ if(n) b[0]=0; return 0; }
bool    orient_gravityScreen(float*, float*){ return false; }
int     orient_remapButton(int id){ return id; }
#endif
