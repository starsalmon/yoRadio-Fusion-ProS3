#pragma once
#ifndef yo_freefonts_h
#define yo_freefonts_h

/*******************************************************************************
 *  yo_freefonts.h  –  DejaVu Sans GFXfont, teljes magyar karakterkészlettel
 *
 *  Az Adafruit_GFX drawChar() csak unsigned char (uint8_t) paramétert fogad,
 *  ezért az ő (U+0151) és ű (U+0171) csonkolódna 'Q'-ra és 'q'-ra.
 *  A yoDrawGlyph() saját glyph renderer uint16_t kódponttal oldja meg ezt.
 ******************************************************************************/

#ifndef DSP_LCD

#include <Adafruit_GFX.h>
#include "../fonts/DejaVuSansBold16_HU.h"
#include "../fonts/DejaVuSansBold14_HU.h"
#include "../fonts/DejaVuSansBold12_HU.h"
#include "../fonts/DejaVuSans13_HU.h"
#include "../fonts/DejaVuSans11_HU.h"
#include "../fonts/DejaVuSans8_HU.h"
#include "../fonts/DejaVuSansBold8_HU.h"

/* ── Font szerepkör leképzés ─────────────────────────────────────────────── */
inline const GFXfont* yoScrollFont(uint8_t textsize) {
  switch (textsize) {
    case 6:  return &DejaVuSansBold8_HU;
    case 5:  return &DejaVuSans11_HU;
    case 4:  return &DejaVuSansBold16_HU;   // állomásnév
    case 3:  return &DejaVuSansBold14_HU;
    case 2:  return &DejaVuSansBold12_HU;   // előadó  (Bold 10→12)
    case 1:  return &DejaVuSans13_HU;       // dalcím  (Sans 11→13)
    default: return &DejaVuSans8_HU;
  }
}

inline const GFXfont* yoTextFont(uint8_t /*textsize*/) {
  return &DejaVuSans8_HU;
}

/* ── Segédfüggvények ─────────────────────────────────────────────────────── */
inline uint16_t yoFontHeight(const GFXfont* f) {
  if (!f) return 8;
  return pgm_read_byte(&f->yAdvance);
}

inline int16_t yoBaselineOffset(const GFXfont* f) {
  if (!f) return 0;
  uint8_t yAdv = pgm_read_byte(&f->yAdvance);
  return (int16_t)(yAdv - yAdv / 5);
}

inline uint8_t yoGlyphWidth(const GFXfont* f, uint16_t cp) {
  if (!f) return 6;
  uint16_t first = pgm_read_word(&f->first);
  uint16_t last  = pgm_read_word(&f->last);
  if (cp < first || cp > last) return 0;
  const GFXglyph* g = (const GFXglyph*) pgm_read_ptr(&f->glyph);
  return pgm_read_byte(&(g[cp - first].xAdvance));
}

inline uint16_t yoStringWidth(const GFXfont* f, const char* str) {
  if (!f || !str) return strlen(str) * 6;
  uint16_t w = 0;
  const uint8_t* p = (const uint8_t*)str;
  while (*p) {
    uint32_t cp;
    if      (*p < 0x80)            { cp = *p++; }
    else if ((*p & 0xE0) == 0xC0) { cp = (*p++ & 0x1F) << 6;  if (*p) cp |= (*p++ & 0x3F); }
    else if ((*p & 0xF0) == 0xE0) { cp = (*p++ & 0x0F) << 12; if (*p) cp |= (*p++ & 0x3F) << 6; if (*p) cp |= (*p++ & 0x3F); }
    else                           { cp = *p++; }
    w += yoGlyphWidth(f, (uint16_t)cp);
  }
  return w;
}

inline void yoApplyFont(Adafruit_GFX& gfx, const GFXfont* f) {
  gfx.setFont(f);
  gfx.setTextSize(1);
}

/* ── Saját glyph renderer – megkerüli a drawChar uint8_t korlátját ──────────
 *
 *  Az Adafruit_GFX drawChar(x, y, unsigned char c, ...) csak uint8_t
 *  karakterkódot fogad – az ő (U+0151=337) és ű (U+0171=369) csonkolódna.
 *  Ez a függvény közvetlenül olvassa a GFXfont bitmap adatát és drawPixel-lel
 *  rajzolja ki, pontosan ahogy a drawChar tenné – de uint16_t cp-vel.
 * ───────────────────────────────────────────────────────────────────────────  */
inline void yoDrawGlyph(Adafruit_GFX& gfx, const GFXfont* f,
                        uint16_t cp, int16_t x, int16_t y,
                        uint16_t fg, uint16_t bg) {
  if (!f) return;
  uint16_t first = pgm_read_word(&f->first);
  uint16_t last  = pgm_read_word(&f->last);
  if (cp < first || cp > last) return;

  const GFXglyph* glyphs = (const GFXglyph*) pgm_read_ptr(&f->glyph);
  const GFXglyph* glyph  = &glyphs[cp - first];
  const uint8_t*  bitmap = (const uint8_t*)  pgm_read_ptr(&f->bitmap);

  uint16_t bo  = pgm_read_word(&glyph->bitmapOffset);
  uint8_t  w   = pgm_read_byte(&glyph->width);
  uint8_t  h   = pgm_read_byte(&glyph->height);
  uint8_t  adv = pgm_read_byte(&glyph->xAdvance);
  int8_t   xo  = pgm_read_byte(&glyph->xOffset);
  int8_t   yo  = pgm_read_byte(&glyph->yOffset);

  if (w == 0 || h == 0) {
    gfx.setCursor(x + adv, y);
    return;
  }

  // Háttér törlése (drawChar bg-kezelése alapján)
  // (opcionális, a psFrameBuffer fillRect már kitörölte)

  // Bitmap kirajzolás – folyamatos bit stream olvasás
  uint8_t  bits = 0, bit = 0;
  uint16_t b    = bo;
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      if (!(bit++ & 7)) bits = pgm_read_byte(&bitmap[b++]);
      if (bits & 0x80)  gfx.drawPixel(x + xo + xx, y + yo + yy, fg);
      else if (bg != fg) gfx.drawPixel(x + xo + xx, y + yo + yy, bg);
      bits <<= 1;
    }
  }
  gfx.setCursor(x + adv, y);
}

/* ── UTF-8 nagybetűsítés (Latin + Latin Extended-A) ─────────────────────────
 *  A toupper() csak ASCII-ra működik. Ez a függvény UTF-8 streamet dekódol
 *  és a Latin/Latin Extended-A karaktereket nagybetűre konvertálja.
 *  Lefedi: á→Á, é→É, ő→Ő, ű→Ű, ö→Ö stb.
 * ───────────────────────────────────────────────────────────────────────────  */
inline uint32_t yoToUpperCP(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 32;
  if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) return cp - 32;
  if (cp >= 0x0101 && cp <= 0x017E && (cp & 1)) return cp - 1;
  return cp;
}

inline void yoUtf8ToUpper(char* dst, const char* src, size_t maxlen) {
  const uint8_t* p = (const uint8_t*)src;
  size_t out = 0;
  while (*p && out < maxlen - 1) {
    uint32_t cp; uint8_t bytes;
    if      (*p < 0x80)            { cp = *p; bytes = 1; }
    else if ((*p & 0xE0) == 0xC0) { cp = (*p & 0x1F) << 6  | (*(p+1) & 0x3F); bytes = 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = (*p & 0x0F) << 12 | (*(p+1) & 0x3F) << 6 | (*(p+2) & 0x3F); bytes = 3; }
    else                           { cp = *p; bytes = 1; }
    cp = yoToUpperCP(cp);
    if      (cp < 0x80)  { if (out+1 >= maxlen) break; dst[out++] = (char)cp; }
    else if (cp < 0x800) { if (out+2 >= maxlen) break; dst[out++] = (char)(0xC0|(cp>>6)); dst[out++] = (char)(0x80|(cp&0x3F)); }
    else                 { if (out+3 >= maxlen) break; dst[out++] = (char)(0xE0|(cp>>12)); dst[out++] = (char)(0x80|((cp>>6)&0x3F)); dst[out++] = (char)(0x80|(cp&0x3F)); }
    p += bytes;
  }
  dst[out] = '\0';
}

/* ── UTF-8 string nyomtatása – saját glyph rendererrel ──────────────────── */
inline void yoPrintUtf8(Adafruit_GFX& gfx, const char* str,
                        uint16_t fgcolor, uint16_t bgcolor,
                        const GFXfont* f) {
  if (!str || !f) return;
  int16_t x = gfx.getCursorX();
  int16_t y = gfx.getCursorY();
  const uint8_t* p = (const uint8_t*)str;
  while (*p) {
    uint32_t cp;
    if      (*p < 0x80)            { cp = *p++; }
    else if ((*p & 0xE0) == 0xC0) { cp = (*p++ & 0x1F) << 6;  if (*p) cp |= (*p++ & 0x3F); }
    else if ((*p & 0xF0) == 0xE0) { cp = (*p++ & 0x0F) << 12; if (*p) cp |= (*p++ & 0x3F) << 6; if (*p) cp |= (*p++ & 0x3F); }
    else                           { cp = *p++; }
    if (cp >= 0x20) {
      yoDrawGlyph(gfx, f, (uint16_t)cp, x, y, fgcolor, bgcolor);
      x = gfx.getCursorX();  // yoDrawGlyph frissíti a cursort
    }
  }
}

#endif  // !DSP_LCD
#endif  // yo_freefonts_h