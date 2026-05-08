#include "../../core/options.h"
#if (TS_MODEL==TS_MODEL_CST816)

/*  touchscreen_cst816.cpp
 *
 *  Direkt I2C regiszter olvasás – Adafruit_CST8XX lib NÉLKÜL.
 *  Okok:
 *   - Az Adafruit lib polling-alapú, interrupt-vezérelt környezetben
 *     megbízhatatlan koordinátákat adhat vissza.
 *   - TCA6408 expander mögötti interrupt-láncon minden INT forrás
 *     (IMU, gomb, RTC, USB) hamis touch eseményt okozhat a lib-ben.
 *   - A keep-alive polling is véletlenszerű "phantom touch"-t eredményez.
 *
 *  A javítások:
 *   1. Direkt I2C olvasás: finger count ellenőrzés + X/Y raw regiszterek
 *   2. Sanity check: csak 0..239 tartományba eső koordináta fogadható el
 *   3. Keep-alive polling eltávolítva: csak interrupt / TCA P0 alapján probe
 *   4. TCA6408 ág: az ISR flag csak akkor törlődik, ha P0 valóban LOW volt
 */

#include "touchscreen_cst816.h"
#include <Wire.h>

#ifndef TS_TCA6408_PRESENT
  #define TS_TCA6408_PRESENT 0
#endif

#ifndef TS_INVERT_X
  #define TS_INVERT_X (TS_TCA6408_PRESENT ? 1 : 0)
#endif
#ifndef TS_INVERT_Y
  #define TS_INVERT_Y 0
#endif
#ifndef TS_SWAP_XY
  #define TS_SWAP_XY  0
#endif

#ifndef TCA6408_ADDR
  #define TCA6408_ADDR 0x20
#endif
#ifndef TCA6408_TS_BIT
  #define TCA6408_TS_BIT 0   // P0 = CST816 INT
#endif
#ifndef TS_I2C_ADDR
  #define TS_I2C_ADDR 0x15   // CST816 I2C cím
#endif

CST816_Adapter ts;

// ---------------------------------------------------------------
// Direkt CST816 regiszter olvasás (Adafruit lib nélkül)
// Regiszter térkép:
//   0x02 = finger count (0 = nincs érintés, 1 = érintés)
//   0x03 = XH  (felső 4 bit: esemény flag, alsó 4 bit: X[11:8])
//   0x04 = XL  (X[7:0])
//   0x05 = YH  (felső 4 bit: finger ID, alsó 4 bit: Y[11:8])
//   0x06 = YL  (Y[7:0])
// ---------------------------------------------------------------
static bool cst816_read_point(uint16_t &x, uint16_t &y) {
  // 5 byte burst read: 0x02..0x06
  Wire.beginTransmission(TS_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TS_I2C_ADDR, (uint8_t)5) < 5) return false;

  uint8_t fingers = Wire.read();  // 0x02
  uint8_t xh      = Wire.read();  // 0x03
  uint8_t xl      = Wire.read();  // 0x04
  uint8_t yh      = Wire.read();  // 0x05
  uint8_t yl      = Wire.read();  // 0x06

  if (fingers == 0) return false;

  uint16_t rx = ((uint16_t)(xh & 0x0F) << 8) | xl;
  uint16_t ry = ((uint16_t)(yh & 0x0F) << 8) | yl;

  // Sanity check: csak a fizikailag lehetséges tartomány fogadható el
  if (rx >= 240 || ry >= 240) return false;

  x = rx;
  y = ry;
  return true;
}

// ---------------------------------------------------------------
// Rotáció segédfüggvény
// ---------------------------------------------------------------
static inline void rotateMap(uint16_t &x, uint16_t &y,
                              uint16_t w, uint16_t h, uint8_t rot) {
  uint16_t nx = x, ny = y;
  switch (rot & 3) {
    case 0: nx = x;         ny = y;         break;
    case 1: nx = h-1 - y;  ny = x;         break;
    case 2: nx = w-1 - x;  ny = h-1 - y;   break;
    case 3: nx = y;         ny = w-1 - x;   break;
  }
  x = nx; y = ny;
}

// ========================= TCA6408 ÁG =========================
#if TS_TCA6408_PRESENT

static bool tca_read_u8(uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TCA6408_ADDR, (uint8_t)1) != 1) return false;
  val = Wire.read();
  return true;
}

static bool tca_write_u8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// Csak a touch bit (P0) vizsgálata – más INT forrásokat figyelmen kívül hagy
static inline bool tca_ts_irq_active() {
  uint8_t in = 0xFF;
  if (!tca_read_u8(0x00, in)) return false;
  return ((in & (1 << TCA6408_TS_BIT)) == 0);
}

static volatile bool g_touch_irq = false;
#if (TS_INT >= 0)
void IRAM_ATTR _tca_isr() { g_touch_irq = true; }
#endif

#else  // ---- NINCS TCA6408 ----

static inline bool tca_ts_irq_active() { return false; }
static volatile bool g_touch_irq = false;
#if (TS_INT >= 0)
void IRAM_ATTR _direct_ts_isr() { g_touch_irq = true; }
#endif

#endif // TS_TCA6408_PRESENT
// ===============================================================

bool CST816_Adapter::begin(uint16_t w, uint16_t h, bool flip) {
  _w = w; _h = h; _flip = flip;

  // I2C init
#if defined(TS_SDA) && defined(TS_SCL)
  Wire.begin(TS_SDA, TS_SCL, 400000);
#else
  Wire.begin();
  Wire.setClock(400000);
#endif

#if TS_TCA6408_PRESENT
  // TCA6408: minden pin input, polarity normál
  tca_write_u8(0x02, 0x00); // Polarity: normál (nem invertált)
  tca_write_u8(0x03, 0xFF); // Config: 1=input (minden pin bemenet)
  uint8_t dummy;
  tca_read_u8(0x00, dummy); // Első read: pending INT törlése

  #if (TS_INT >= 0)
    pinMode(TS_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TS_INT), _tca_isr, FALLING);
  #endif
#else
  // Közvetlen CST816 INT → MCU
  #if (TS_INT >= 0)
    pinMode(TS_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TS_INT), _direct_ts_isr, FALLING);
  #endif
#endif

  // TS_RST: GPIO0 strapping pin az I80 boardon → csak HIGH-on tartjuk,
  // soha ne húzzuk le boot közben!
#if (TS_RST >= 0)
  pinMode(TS_RST, OUTPUT);
  digitalWrite(TS_RST, HIGH);
  delay(5);
  // Soft reset: rövid LOW pulzus
  digitalWrite(TS_RST, LOW);
  delay(10);
  digitalWrite(TS_RST, HIGH);
  delay(50); // CST816 boot idő
#endif

  // Rövid várakozás az I2C bus stabilizálódásához
  delay(10);

  setRotation(flip ? 2 : 0);
  _hadTouch = false;
  _last = {0, 0};
  return true;
}

void CST816_Adapter::setRotation(uint8_t r) { _rot = (r & 3); }

void CST816_Adapter::read() {
  isTouched = false;

  // Probe feltétel: csak akkor olvasunk, ha tényleg jött jelzés
  // NINCS keep-alive polling – az véletlenszerű phantom touch-t okoz,
  // különösen TCA6408 esetén ahol más INT források is triggerelnek
  bool shouldProbe = tca_ts_irq_active();
#if (TS_INT >= 0)
  shouldProbe = shouldProbe || g_touch_irq;
#endif

  if (!shouldProbe) return;

  // ISR flag előzetes törlése (újabb interrupt nem veszik el,
  // mert az IRAM_ATTR ISR bármikor újra setelheti)
#if (TS_INT >= 0)
  g_touch_irq = false;
#endif

  // 3 minta, median-of-3 szűrés
  // A direkt I2C olvasás megbízhatóbb mint az Adafruit lib polling
  const int N = 3;
  uint16_t xs[N], ys[N];
  uint8_t n = 0;

  for (int i = 0; i < N; ++i) {
    uint16_t rx, ry;
    if (cst816_read_point(rx, ry)) {
      xs[n] = rx;
      ys[n] = ry;
      ++n;
    }
    if (i < N-1) delay(2);
  }

  if (n == 0) return; // nincs érvényes minta

  // Minirendezés a median meghatározásához
  auto sortN = [](uint16_t* a, uint8_t len) {
    for (uint8_t i = 0; i < len; i++)
      for (uint8_t j = i+1; j < len; j++)
        if (a[j] < a[i]) { uint16_t t = a[i]; a[i] = a[j]; a[j] = t; }
  };
  sortN(xs, n);
  sortN(ys, n);
  uint16_t x = xs[n/2];
  uint16_t y = ys[n/2];

  // Rotáció
  rotateMap(x, y, _w, _h, _rot);

  // Tengelyek kezelése (myoptions.h-ból vezérelt)
#if TS_SWAP_XY
  { uint16_t tx = x; x = y; y = tx; }
#endif
#if TS_INVERT_X
  x = (_w - 1) - x;
#endif
#if TS_INVERT_Y
  y = (_h - 1) - y;
#endif

  // Clamp a kijelző határaira
  if (x >= _w) x = _w - 1;
  if (y >= _h) y = _h - 1;

  // Axis-lock: ha az elmozdulás egyik irányban domináns, a másikat rögzítjük
  // Ez a round display-en a véletlen diagonal swipe-okat szűri ki
  const int MIN_LOCK  = 3;
  const int AXIS_BIAS = 6;
  int dx = (int)x - (int)_last.x;
  int dy = (int)y - (int)_last.y;
  if (abs(dx) > MIN_LOCK || abs(dy) > MIN_LOCK) {
    if      (abs(dy) > abs(dx) + AXIS_BIAS) x = _last.x; // vertikális mozgás
    else if (abs(dx) > abs(dy) + AXIS_BIAS) y = _last.y; // horizontális mozgás
  }

  points[0].x = _last.x = x;
  points[0].y = _last.y = y;
  isTouched   = true;
  _hadTouch   = true;
}

bool CST816_Adapter::touched() const { return isTouched; }

#endif // TS_MODEL==TS_MODEL_CST816