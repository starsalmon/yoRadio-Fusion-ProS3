#pragma once
// Streaming RSS <item> scanner for podcast indexing.
//
// Pure C/C++ (no Arduino types) so it can be unit-tested natively on a PC.
// Bytes are fed in chunks; each byte is looked at once (no buffer rescans), and only the
// fields we use are kept: <title>, <itunes:subtitle> and the url="" attribute of
// <ppg:enclosureSecure>, <enclosure>, <media:content>. Item size is unlimited, so long
// show notes no longer cause items to be dropped.
//
// Field semantics match the previous String-based parser:
//  - title/subtitle: first exact "<title>" / "<itunes:subtitle>" in the item, text up to
//    the matching close tag; trimmed; a leading CDATA wrapper stripped; tabs/CR/LF -> ' ';
//    right-trimmed; basic entities (&amp; &lt; &gt; &quot; &apos;) decoded.
//  - url: first tag whose name starts with the given prefix; value of url="..."; trimmed;
//    BBC "audio-nondrm-download-rss" -> "audio-nondrm-download" rewrite.
// Difference (intentional): markup inside CDATA sections / comments is skipped, so HTML
// inside <description> can no longer be mistaken for an item field.

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

namespace podrss {

static inline void sanitizeTsvField(const char* in, char* out, size_t outSz) {
  if (!out || outSz == 0) return;
  out[0] = '\0';
  if (!in) return;
  size_t o = 0;
  for (size_t i = 0; in[i] && (o + 1) < outSz; i++) {
    const char c = in[i];
    out[o++] = (c == '\t' || c == '\r' || c == '\n') ? ' ' : c;
  }
  out[o] = '\0';
  while (o > 0 && out[o - 1] == ' ') out[--o] = '\0';
}

static inline void decodeBasicXmlEntities(char* s) {
  if (!s) return;
  struct Ent { const char* from; const char* to; };
  static const Ent ents[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
  for (const auto& e : ents) {
    const size_t fromLen = strlen(e.from), toLen = strlen(e.to);
    for (;;) {
      char* p = strstr(s, e.from);
      if (!p) break;
      memcpy(p, e.to, toLen);  // all replacements are shorter than the entity
      memmove(p + toLen, p + fromLen, strlen(p + fromLen) + 1);
    }
  }
}

static inline bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

// String::trim() equivalent (Arduino trims isspace()).
static inline void trimWs(char* s) {
  size_t n = strlen(s);
  while (n > 0 && isWs(s[n - 1])) s[--n] = '\0';
  size_t i = 0;
  while (s[i] && isWs(s[i])) i++;
  if (i) memmove(s, s + i, n - i + 1);
}

static inline void trimInPlace(char* s) {
  if (!s) return;
  while (*s == ' ') memmove(s, s + 1, strlen(s));
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
}

static inline void truncateEllipsis(char* s, size_t maxChars) {
  if (!s || maxChars == 0) return;
  const size_t n = strlen(s);
  if (n < maxChars) return;
  if (maxChars <= 4) { s[maxChars - 1] = '\0'; return; }
  s[maxChars - 4] = '.'; s[maxChars - 3] = '.'; s[maxChars - 2] = '.'; s[maxChars - 1] = '\0';
}

// Raw captured text -> final field (same steps as the old extractTagText()).
static inline void finishText(char* raw, char* out, size_t outSz) {
  trimWs(raw);
  if (strncmp(raw, "<![CDATA[", 9) == 0) {
    char* end = strstr(raw, "]]>");
    if (end && end > raw) {  // old code: indexOf("]]>") > 0
      *end = '\0';
      memmove(raw, raw + 9, strlen(raw + 9) + 1);
    }
  }
  sanitizeTsvField(raw, out, outSz);
  decodeBasicXmlEntities(out);
}

// Tag text (without '<' and '>') -> url attribute (same steps as the old extractAttrUrl()).
static inline bool urlFromTag(const char* tag, char* out, size_t outSz) {
  out[0] = '\0';
  const char* a = strstr(tag, "url=\"");
  if (!a) return false;
  a += 5;
  const char* q = strchr(a, '"');
  if (!q) return false;
  char u[1024];
  size_t n = (size_t)(q - a);
  if (n >= sizeof(u)) n = sizeof(u) - 1;
  memcpy(u, a, n);
  u[n] = '\0';
  trimWs(u);
  if (strstr(u, "open.live.bbc.co.uk/mediaselector/")) {
    // String::replace(): replace every occurrence.
    const char* from = "audio-nondrm-download-rss";
    const char* to = "audio-nondrm-download";
    char* p;
    while ((p = strstr(u, from)) != nullptr) {
      memmove(p + strlen(to), p + strlen(from), strlen(p + strlen(from)) + 1);
      memcpy(p, to, strlen(to));
    }
  }
  sanitizeTsvField(u, out, outSz);
  return out[0] != '\0';
}

struct Item {
  char title[192];
  char subtitle[256];
  char url[320];
  bool okTitle;
  bool okSubtitle;
  bool okUrl;
  uint32_t pubEpoch;  // UTC seconds from <pubDate>, 0 if missing/unparsable
};

// RFC 822 date ("Wed, 22 Oct 2026 06:00:00 +0000" / "GMT" / "+1100") -> UTC epoch, 0 on failure.
static inline uint32_t parseRfc822(const char* s) {
  static const char* mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
  while (*s && !isdigit((unsigned char)*s)) s++;  // skip weekday
  int d = 0, y = 0, hh = 0, mm = 0, ss = 0; char m[4] = {0};
  if (sscanf(s, "%d %3s %d %d:%d:%d", &d, m, &y, &hh, &mm, &ss) < 5) return 0;
  const char* mp = strstr(mon, m);
  if (!mp || m[0] == 0) return 0;
  int mo = (int)(mp - mon) / 3 + 1;
  if (y < 100) y += 2000;
  if (y < 1990 || y > 2100 || d < 1 || d > 31) return 0;
  // days from civil (Howard Hinnant)
  int yy = y - (mo <= 2); int era = yy / 400; int yoe = yy - era * 400;
  int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + doe - 719468;
  long t = days * 86400L + hh * 3600L + mm * 60L + ss;
  // zone offset
  const char* z = s; int sp = 0;
  while (*z && sp < 4) { if (*z == ' ') sp++; z++; }
  while (*z == ' ') z++;
  if ((*z == '+' || *z == '-') && isdigit((unsigned char)z[1])) {
    int v = atoi(z + 1); long off = (v / 100) * 3600L + (v % 100) * 60L;
    t -= (*z == '+') ? off : -off;
  }
  return t > 0 ? (uint32_t)t : 0;
}

// Build the playlist label exactly like the old podcasts.cpp did.
static inline void formatEpisodeLabel(const char* show, const Item& it, char* safeTitle, size_t safeSz) {
  char epTitle[192];
  char epDesc[256];
  strncpy(epTitle, it.title, sizeof(epTitle)); epTitle[sizeof(epTitle) - 1] = '\0';
  epDesc[0] = '\0';
  {
    const char* seps[] = {" -- ", " \xE2\x80\x93 ", " \xE2\x80\x94 "};
    for (size_t i = 0; i < 3; i++) {
      char* dd = strstr(epTitle, seps[i]);
      if (dd) {
        *dd = '\0';
        dd += strlen(seps[i]);
        strncpy(epDesc, dd, sizeof(epDesc)); epDesc[sizeof(epDesc) - 1] = '\0';
        break;
      }
    }
  }
  if (epDesc[0] == '\0' && it.okSubtitle && it.subtitle[0]) {
    strncpy(epDesc, it.subtitle, sizeof(epDesc)); epDesc[sizeof(epDesc) - 1] = '\0';
  }
  trimInPlace(epTitle);
  trimInPlace(epDesc);
  truncateEllipsis(epDesc, 96);
  char episodeField[480];
  if (epDesc[0]) snprintf(episodeField, sizeof(episodeField), "%s - %s", epTitle, epDesc);
  else { strncpy(episodeField, epTitle, sizeof(episodeField)); episodeField[sizeof(episodeField) - 1] = '\0'; }
  char fullTitle[520];
  snprintf(fullTitle, sizeof(fullTitle), "%s - %s", show, episodeField);
  sanitizeTsvField(fullTitle, safeTitle, safeSz);
}

class Scanner {
 public:
  // Called for every complete <item> that has a title and a URL. Return false to stop.
  typedef bool (*ItemCb)(const Item& it, void* user);

  void reset(ItemCb cb, void* user) {
    _cb = cb; _user = user; _st = S_TEXT; _inItem = false; _stopped = false;
    _tagLen = 0; _capLen = 0; _cap = C_NONE; _matchPos = 0; _closeLen = 0; _skipMatch = 0;
    clearItem();
  }
  bool stopped() const { return _stopped; }

  void feed(const char* p, size_t n) {
    for (size_t i = 0; i < n && !_stopped; i++) step(p[i]);
  }

 private:
  enum State : uint8_t { S_TEXT, S_TAG, S_CAPTURE, S_SKIP_CDATA, S_SKIP_COMMENT };
  enum Cap : uint8_t { C_NONE, C_TITLE, C_SUBTITLE, C_PUBDATE };
  static const size_t TAG_MAX = 1024;
  static const size_t CAP_MAX = 1024;

  ItemCb _cb = nullptr;
  void* _user = nullptr;
  State _st = S_TEXT;
  bool _inItem = false;
  bool _stopped = false;
  char _tag[TAG_MAX + 1];
  size_t _tagLen = 0;
  char _capBuf[CAP_MAX + 1];
  size_t _capLen = 0;
  Cap _cap = C_NONE;
  char _close[24];
  size_t _closeLen = 0;
  size_t _matchPos = 0;
  uint8_t _skipMatch = 0;
  bool _haveTitle, _haveSub, _haveDate, _haveEncSec, _haveEnc, _haveMedia;
  char _urlEncSec[320], _urlEnc[320], _urlMedia[320];
  Item _it;

  void clearItem() {
    _haveTitle = _haveSub = _haveDate = _haveEncSec = _haveEnc = _haveMedia = false;
    _urlEncSec[0] = _urlEnc[0] = _urlMedia[0] = '\0';
    memset(&_it, 0, sizeof(_it));
  }

  void step(char c) {
    switch (_st) {
      case S_TEXT:
        if (c == '<') { _st = S_TAG; _tagLen = 0; }
        break;
      case S_TAG:
        if (c == '>') { _tag[_tagLen] = '\0'; _st = S_TEXT; onTag(); break; }
        if (_tagLen < TAG_MAX) _tag[_tagLen++] = c;
        if (_tagLen == 8 && memcmp(_tag, "![CDATA[", 8) == 0) { _st = S_SKIP_CDATA; _skipMatch = 0; }
        else if (_tagLen == 3 && memcmp(_tag, "!--", 3) == 0) { _st = S_SKIP_COMMENT; _skipMatch = 0; }
        break;
      case S_SKIP_CDATA:   skipUntil(c, "]]>"); break;
      case S_SKIP_COMMENT: skipUntil(c, "-->"); break;
      case S_CAPTURE:      capture(c); break;
    }
  }

  void skipUntil(char c, const char* end) {
    // end is "]]>" or "-->": two equal chars then '>'.
    if (c == end[_skipMatch]) {
      if (++_skipMatch == 3) { _st = S_TEXT; _skipMatch = 0; }
    } else if (_skipMatch == 2 && c == end[1]) {
      // "]]]" / "---": still have the two-char prefix.
    } else {
      _skipMatch = (c == end[0]) ? 1 : 0;
    }
  }

  // Capture raw text until the literal close tag (like indexOf(close)).
  void capture(char c) {
    if (_capLen < CAP_MAX) _capBuf[_capLen++] = c;
    if (c == _close[_matchPos]) {
      if (++_matchPos == _closeLen) { endCapture(); }
    } else {
      // Restart match; close tags start with '<' which can't appear in the middle of one.
      _matchPos = (c == _close[0]) ? 1 : 0;
    }
  }

  void endCapture() {
    // Drop the close tag from the buffer (if it fit).
    size_t len = _capLen;
    if (len >= _closeLen) len -= _closeLen;
    _capBuf[len] = '\0';
    if (_cap == C_TITLE) {
      finishText(_capBuf, _it.title, sizeof(_it.title));
      _it.okTitle = _it.title[0] != '\0';
    } else if (_cap == C_SUBTITLE) {
      finishText(_capBuf, _it.subtitle, sizeof(_it.subtitle));
      _it.okSubtitle = _it.subtitle[0] != '\0';
    } else if (_cap == C_PUBDATE) {
      _it.pubEpoch = parseRfc822(_capBuf);
    }
    _cap = C_NONE; _capLen = 0; _matchPos = 0;
    _st = S_TEXT;
  }

  void startCapture(Cap cap, const char* close) {
    _cap = cap; _capLen = 0; _matchPos = 0;
    _closeLen = strlen(close);
    memcpy(_close, close, _closeLen + 1);
    _st = S_CAPTURE;
  }

  static bool nameIs(const char* tag, const char* name) {
    const size_t n = strlen(name);
    if (strncmp(tag, name, n) != 0) return false;
    const char e = tag[n];
    return e == '\0' || isWs(e) || e == '/';
  }

  void onTag() {
    if (!_inItem) {
      if (nameIs(_tag, "item")) {
        _inItem = true;
        clearItem();
        if (_tagLen > 0 && _tag[_tagLen - 1] == '/') _inItem = false;  // <item/>
      }
      return;
    }
    if (strcmp(_tag, "/item") == 0) { endItem(); return; }
    if (!_haveTitle && strcmp(_tag, "title") == 0) { _haveTitle = true; startCapture(C_TITLE, "</title>"); return; }
    if (!_haveSub && strcmp(_tag, "itunes:subtitle") == 0) { _haveSub = true; startCapture(C_SUBTITLE, "</itunes:subtitle>"); return; }
    if (!_haveDate && strcmp(_tag, "pubDate") == 0) { _haveDate = true; startCapture(C_PUBDATE, "</pubDate>"); return; }
    // Prefix matches, like the old indexOf("<" + tag).
    if (!_haveEncSec && strncmp(_tag, "ppg:enclosureSecure", 19) == 0) { _haveEncSec = true; urlFromTag(_tag, _urlEncSec, sizeof(_urlEncSec)); return; }
    if (!_haveEnc && strncmp(_tag, "enclosure", 9) == 0) { _haveEnc = true; urlFromTag(_tag, _urlEnc, sizeof(_urlEnc)); return; }
    if (!_haveMedia && strncmp(_tag, "media:content", 13) == 0) { _haveMedia = true; urlFromTag(_tag, _urlMedia, sizeof(_urlMedia)); return; }
  }

  void endItem() {
    _inItem = false;
    const char* u = _urlEncSec[0] ? _urlEncSec : (_urlEnc[0] ? _urlEnc : _urlMedia);
    strncpy(_it.url, u, sizeof(_it.url)); _it.url[sizeof(_it.url) - 1] = '\0';
    _it.okUrl = _it.url[0] != '\0';
    if (!_it.okTitle || !_it.okUrl) return;
    if (_cb && !_cb(_it, _user)) _stopped = true;
  }
};

}  // namespace podrss
