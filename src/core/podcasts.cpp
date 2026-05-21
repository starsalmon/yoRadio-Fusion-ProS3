#include "podcasts.h"

#include "config.h"
#include "display.h"
#include "network.h"
#include "player.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace {

static TaskHandle_t s_podTask = nullptr;
static bool s_forceBuild = false;
static uint32_t s_lastIndexMs = 0;

#ifndef PODBUILD_STACK
// The RSS parsing/build path uses HTTPClient + String heavy operations.
// On PROS3 this can overflow smaller task stacks (stack canary in "podBuild").
// Increase to avoid boot-loop when indexing on startup.
#define PODBUILD_STACK 16384
#endif

static inline bool epochLooksValid(uint32_t epoch) {
  // Rough sanity check: epoch seconds after 2023-01-01.
  return epoch >= 1672531200u;
}

static inline uint32_t epochNow() {
  const time_t t = time(nullptr);
  if (t <= 0) return 0u;
  return (uint32_t)t;
}

static portMUX_TYPE s_progMux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_progCur = 0;
static uint16_t s_progTotal = 0;
static char s_progShow[96] = {0};

static inline bool shouldAbortBuild() {
  // Abort if the user left Podcast mode or playback started (avoid contention and UI races).
  if (config.getMode() != PM_PODCAST) return true;
  if (player.isRunning()) return true;
  return false;
}

static void setProgress(uint16_t cur, uint16_t total, const char* show) {
  portENTER_CRITICAL(&s_progMux);
  s_progCur = cur;
  s_progTotal = total;
  if (show) strlcpy(s_progShow, show, sizeof(s_progShow));
  else      s_progShow[0] = '\0';
  portEXIT_CRITICAL(&s_progMux);
}

static void sanitizeTsvField(const char* in, char* out, size_t outSz) {
  if (!out || outSz == 0) return;
  out[0] = '\0';
  if (!in) return;
  size_t o = 0;
  for (size_t i = 0; in[i] && (o + 1) < outSz; i++) {
    const char c = in[i];
    if (c == '\t' || c == '\r' || c == '\n') {
      out[o++] = ' ';
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
  // trim right
  while (o > 0 && out[o - 1] == ' ') out[--o] = '\0';
}

static void decodeBasicXmlEntities(char* s) {
  if (!s) return;
  // Minimal in-place decoding for common entities in titles.
  struct Ent { const char* from; const char* to; };
  static const Ent ents[] = {
    {"&amp;", "&"},
    {"&lt;", "<"},
    {"&gt;", ">"},
    {"&quot;", "\""},
    {"&apos;", "'"},
  };

  for (const auto& e : ents) {
    for (;;) {
      char* p = strstr(s, e.from);
      if (!p) break;
      const size_t fromLen = strlen(e.from);
      const size_t toLen = strlen(e.to);
      // move tail left/right as needed
      if (toLen <= fromLen) {
        memcpy(p, e.to, toLen);
        memmove(p + toLen, p + fromLen, strlen(p + fromLen) + 1);
      } else {
        // Expand only if space; otherwise just stop decoding this instance.
        const size_t tailLen = strlen(p + fromLen);
        const size_t curLen = strlen(s);
        if (curLen + (toLen - fromLen) + 1 >= BUFLEN * 3) return;
        memmove(p + toLen, p + fromLen, tailLen + 1);
        memcpy(p, e.to, toLen);
      }
    }
  }
}

static void trimInPlace(char* s) {
  if (!s) return;
  // left trim
  while (*s == ' ') memmove(s, s + 1, strlen(s));
  // right trim
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
}

static void truncateEllipsis(char* s, size_t maxChars) {
  if (!s || maxChars == 0) return;
  const size_t n = strlen(s);
  if (n < maxChars) return;
  if (maxChars <= 4) {
    s[maxChars - 1] = '\0';
    return;
  }
  // Keep room for "..."
  s[maxChars - 4] = '.';
  s[maxChars - 3] = '.';
  s[maxChars - 2] = '.';
  s[maxChars - 1] = '\0';
}

static bool extractTagText(const String& item, const char* tag, char* out, size_t outSz) {
  if (!out || outSz == 0) return false;
  out[0] = '\0';
  const String open = String("<") + tag + ">";
  const String close = String("</") + tag + ">";
  int s = item.indexOf(open);
  if (s < 0) return false;
  s += open.length();
  int e = item.indexOf(close, s);
  if (e < 0) return false;
  String t = item.substring(s, e);
  t.trim();
  // CDATA
  if (t.startsWith("<![CDATA[")) {
    const int end = t.indexOf("]]>");
    if (end > 0) t = t.substring(9, end);
  }
  sanitizeTsvField(t.c_str(), out, outSz);
  decodeBasicXmlEntities(out);
  return out[0] != '\0';
}

static bool extractAttrUrl(const String& item, const char* tag, const char* attr, char* out, size_t outSz) {
  if (!out || outSz == 0) return false;
  out[0] = '\0';
  int t = item.indexOf(String("<") + tag);
  if (t < 0) return false;
  int end = item.indexOf(">", t);
  if (end < 0) return false;
  String head = item.substring(t, end);
  const String needle = String(attr) + "=\"";
  int a = head.indexOf(needle);
  if (a < 0) return false;
  a += needle.length();
  int q = head.indexOf("\"", a);
  if (q < 0) return false;
  String u = head.substring(a, q);
  u.trim();
  // BBC podcast feeds often use the "audio-nondrm-download-rss" mediaset, but the
  // playable direct links use "audio-nondrm-download". Rewriting here keeps the
  // playlist URLs compatible with the audio engine.
  if (u.indexOf("open.live.bbc.co.uk/mediaselector/") >= 0) {
    u.replace("audio-nondrm-download-rss", "audio-nondrm-download");
  }
  sanitizeTsvField(u.c_str(), out, outSz);
  return out[0] != '\0';
}

static bool parsePodcastLine(const char* line, char* name, size_t nameSz, char* url, size_t urlSz, uint16_t& limit) {
  if (!line) return false;
  // Robust format:
  //   <show name><ws><http(s)://...><ws><episodes_to_list?>
  // Supports TAB or spaces between fields; show name can contain spaces.
  const char* s = line;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '\0' || *s == '#') return false;

  const char* h = strstr(s, "https://");
  if (!h) h = strstr(s, "http://");
  if (!h) return false;

  // Name is everything before the URL.
  char n[96];
  size_t nlen = (size_t)(h - s);
  if (nlen >= sizeof(n)) nlen = sizeof(n) - 1;
  memcpy(n, s, nlen);
  n[nlen] = '\0';

  // URL is contiguous until whitespace.
  const char* u0 = h;
  const char* u1 = u0;
  while (*u1 && *u1 != ' ' && *u1 != '\t' && *u1 != '\r' && *u1 != '\n') u1++;
  char u[256];
  size_t ulen = (size_t)(u1 - u0);
  if (ulen >= sizeof(u)) ulen = sizeof(u) - 1;
  memcpy(u, u0, ulen);
  u[ulen] = '\0';

  // Optional limit after URL.
  const char* p2 = u1;
  while (*p2 == ' ' || *p2 == '\t') p2++;

  sanitizeTsvField(n, name, nameSz);
  sanitizeTsvField(u, url, urlSz);

  limit = (*p2) ? (uint16_t)atoi(p2) : 5;
  if (limit == 0) limit = 5;

  return (name[0] != '\0' && url[0] != '\0');
}

static bool httpBeginForUrl(HTTPClient& http, const char* url) {
  if (!url) return false;
  if (strncmp(url, "https://", 8) == 0) {
    static WiFiClientSecure s_client;
    s_client.setInsecure();
    if (!http.begin(s_client, url)) return false;
  } else {
    if (!http.begin(url)) return false;
  }

  // Many podcast endpoints behave better with a browser-ish UA.
  http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36");
  return true;
}

static bool resolveRedirectUrl(const String& baseUrl, const String& location, String& outUrl) {
  outUrl = location;
  outUrl.trim();
  if (outUrl.length() == 0) return false;
  if (outUrl.startsWith("//")) {
    outUrl = String("http:") + outUrl;
    return true;
  }
  if (outUrl.startsWith("http://") || outUrl.startsWith("https://")) {
    return true;
  }
  if (outUrl.startsWith("/")) {
    const int scheme = baseUrl.indexOf("://");
    if (scheme < 0) return false;
    const int hostStart = scheme + 3;
    int slash = baseUrl.indexOf("/", hostStart);
    if (slash < 0) slash = baseUrl.length();
    outUrl = baseUrl.substring(0, slash) + outUrl;
    return true;
  }
  // Relative path: append to directory of base URL.
  int lastSlash = baseUrl.lastIndexOf('/');
  if (lastSlash <= 0) return false;
  outUrl = baseUrl.substring(0, lastSlash + 1) + outUrl;
  return true;
}

static int httpGetWithRedirects(HTTPClient& http, String& url, uint8_t maxHops) {
  for (uint8_t hop = 0; hop <= maxHops; hop++) {
    if (!httpBeginForUrl(http, url.c_str())) return -1;
    // Ensure we can read Location headers on redirect responses.
    const char* hdrs[] = {"Location", "location"};
    http.collectHeaders(hdrs, 2);
    http.setTimeout(8000);
    const int code = http.GET();
    if (code > 0 && code < 300) {
      return code;
    }

    // Manual redirects (some cores don't follow automatically).
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String loc = http.getLocation();
      if (loc.length() == 0) loc = http.header("Location");
      if (loc.length() == 0) loc = http.header("location");
      http.end();
      String next;
      if (!resolveRedirectUrl(url, loc, next)) {
        Serial.printf("[POD] redirect %d but no Location for %s\n", code, url.c_str());
        return code;
      }
      Serial.printf("[POD] redirect %d: %s -> %s\n", code, url.c_str(), next.c_str());
      url = next;
      continue;
    }

    http.end();
    return code;
  }
  return -2;
}

static uint16_t countPodcastSources() {
  File f = SPIFFS.open(PODCASTS_PATH, "r");
  if (!f) return 0;
  uint16_t count = 0;
  char line[384];
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0';
    if (line[0] == '\0' || line[0] == '#') continue;
    char show[96];
    char feedUrl[256];
    uint16_t lim = 0;
    if (parsePodcastLine(line, show, sizeof(show), feedUrl, sizeof(feedUrl), lim)) count++;
  }
  f.close();
  return count;
}

} // namespace

bool podcasts_isIndexDue(uint32_t minIntervalSeconds) {
  if (minIntervalSeconds == 0) return true;

  // Prefer wall clock if SNTP/RTC has set the time.
  const uint32_t now = epochNow();
  uint32_t lastEpoch = config.store.lastPodcastIndexEpoch;
  if (lastEpoch == 0xFFFFFFFFu) lastEpoch = 0u;
  if (epochLooksValid(now) && epochLooksValid(lastEpoch)) {
    return (now - lastEpoch) >= minIntervalSeconds;
  }

  // Fallback: throttle within a single uptime using millis().
  if (s_lastIndexMs != 0u) {
    const uint32_t minMs = minIntervalSeconds * 1000u;
    return (uint32_t)(millis() - s_lastIndexMs) >= minMs;
  }

  // No time source yet; allow a build (best-effort).
  return true;
}

void podcasts_getProgress(uint16_t* cur, uint16_t* total, char* showOut, uint16_t showOutSz) {
  if (cur) *cur = 0;
  if (total) *total = 0;
  if (showOut && showOutSz) showOut[0] = '\0';

  portENTER_CRITICAL(&s_progMux);
  const uint16_t c = s_progCur;
  const uint16_t t = s_progTotal;
  const char* s = s_progShow;
  portEXIT_CRITICAL(&s_progMux);

  if (cur) *cur = c;
  if (total) *total = t;
  if (showOut && showOutSz) strlcpy(showOut, s ? s : "", (size_t)showOutSz);
}

uint32_t podcasts_buildEpisodesPlaylist() {
  if (network.status != CONNECTED) return 0;
  if (shouldAbortBuild()) return 0;

  const uint16_t totalSources = countPodcastSources();
  setProgress(0, totalSources, "");

  File f = SPIFFS.open(PODCASTS_PATH, "r");
  if (!f) {
    Serial.printf("[POD] missing %s\n", PODCASTS_PATH);
    setProgress(0, 0, "");
    return 0;
  }

  // Build to a temp file first, then atomically swap in.
  // This avoids the UI trying to open a file we just deleted mid-draw.
  SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
  File out = SPIFFS.open(PLAYLIST_PODCAST_TMP_PATH, "w");
  if (!out) {
    f.close();
    Serial.printf("[POD] cannot create %s\n", PLAYLIST_PODCAST_TMP_PATH);
    setProgress(0, 0, "");
    return 0;
  }

  uint32_t written = 0;
  uint32_t sources = 0;
  uint16_t progressCur = 0;

  char line[384];
  while (f.available()) {
    if (shouldAbortBuild()) {
      out.close();
      f.close();
      SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
      setProgress(0, 0, "");
      return 0;
    }

    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0';
    if (line[0] == '\0' || line[0] == '#') continue;

    char show[96];
    char feedUrl[256];
    uint16_t lim = 0;
    if (!parsePodcastLine(line, show, sizeof(show), feedUrl, sizeof(feedUrl), lim)) continue;
    sources++;
    progressCur++;
    setProgress(progressCur, totalSources, show);
    if (show[0]) {
      Serial.printf("[POD] source %u/%u: %s\n", (unsigned)progressCur, (unsigned)totalSources, show);
    } else {
      Serial.printf("[POD] source %u/%u\n", (unsigned)progressCur, (unsigned)totalSources);
    }

    HTTPClient http;
    String effectiveUrl(feedUrl);
    const int code = httpGetWithRedirects(http, effectiveUrl, 5);
    if (code <= 0 || code >= 300) {
      Serial.printf("[POD] feed HTTP %d for %s\n", code, effectiveUrl.c_str());
      delay(0);
      continue;
    }

    WiFiClient* s = http.getStreamPtr();
    if (!s) { http.end(); continue; }

    uint16_t got = 0;
    String buf;
    buf.reserve(4096);
    uint32_t lastRxMs = millis();
    bool sawAnyItem = false;

    while (s->connected() && got < lim) {
      if (shouldAbortBuild()) {
        http.end();
        out.close();
        f.close();
        SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
        setProgress(0, 0, "");
        return 0;
      }

      if (!s->available()) {
        if ((uint32_t)(millis() - lastRxMs) > 6000u) break;
        delay(1);
        continue;
      }

      while (s->available() && got < lim) {
        if (shouldAbortBuild()) {
          http.end();
          out.close();
          f.close();
          SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
          setProgress(0, 0, "");
          return 0;
        }

        const char c = (char)s->read();
        lastRxMs = millis();
        buf += c;
        // Keep buffer bounded
        if (buf.length() > 16384) {
          buf.remove(0, buf.length() - 8192);
        }

        // Extract items
        for (;;) {
          int is = buf.indexOf("<item");
          if (is < 0) break;
          int ie = buf.indexOf("</item>", is);
          if (ie < 0) break;
          const int end = ie + 7;
          String item = buf.substring(is, end);
          buf.remove(0, end);
          sawAnyItem = true;

          char title[192];
          char subtitle[256];
          char url[320];
          bool okTitle = extractTagText(item, "title", title, sizeof(title));
          const bool okSubtitle = extractTagText(item, "itunes:subtitle", subtitle, sizeof(subtitle));

          // Prefer HTTPS enclosure URLs when available (e.g. BBC provides both).
          bool okUrl = extractAttrUrl(item, "ppg:enclosureSecure", "url", url, sizeof(url));
          if (!okUrl) okUrl = extractAttrUrl(item, "enclosure", "url", url, sizeof(url));
          if (!okUrl) okUrl = extractAttrUrl(item, "media:content", "url", url, sizeof(url));
          if (!okTitle || !okUrl) continue;

          // We render 3 lines as:
          //   meta:  station.name (show)
          //   line2: title1 (episode name)
          //   line3: title2 (short episode description)
          //
          // `Config::loadStation()` already splits the playlist label on the first " - "
          // to populate (show, episode). Then Display::_title() splits station.title on
          // " - " to populate (title1, title2). So we can populate line 3 by ensuring
          // the episode portion contains one more " - ".
          //
          // Example:
          //   "<show> - Risky Business #838 -- GitHub investigates..."
          // becomes:
          //   "<show> - Risky Business #838 - GitHub investigates..."

          char epTitle[192];
          char epDesc[256];
          strlcpy(epTitle, title, sizeof(epTitle));
          epDesc[0] = '\0';

          // Split on common "title — description" patterns inside the episode title.
          // (ASCII double-hyphen, en dash, em dash)
          {
            const char* seps[] = {" -- ", " – ", " — "};
            const char* sepHit = nullptr;
            char* dd = nullptr;
            for (size_t i = 0; i < (sizeof(seps) / sizeof(seps[0])); i++) {
              dd = strstr(epTitle, seps[i]);
              if (dd) {
                sepHit = seps[i];
                break;
              }
            }
            if (dd && sepHit) {
              *dd = '\0';
              dd += strlen(sepHit);
              strlcpy(epDesc, dd, sizeof(epDesc));
            }
          }
          if (epDesc[0] == '\0' && okSubtitle && subtitle[0]) {
            // Fallback: use itunes:subtitle (truncated) for line 3.
            strlcpy(epDesc, subtitle, sizeof(epDesc));
          }

          trimInPlace(epTitle);
          trimInPlace(epDesc);

          // Keep line 3 short enough to be useful on small displays.
          truncateEllipsis(epDesc, 96);

          char episodeField[480];
          if (epDesc[0]) {
            snprintf(episodeField, sizeof(episodeField), "%s - %s", epTitle, epDesc);
          } else {
            strlcpy(episodeField, epTitle, sizeof(episodeField));
          }

          char fullTitle[520];
          snprintf(fullTitle, sizeof(fullTitle), "%s - %s", show, episodeField);
          // Don't sanitize in-place: sanitizeTsvField() clears the output first.
          char safeTitle[520];
          sanitizeTsvField(fullTitle, safeTitle, sizeof(safeTitle));

          out.print(safeTitle);
          out.print('\t');
          out.print(url);
          out.print('\t');
          out.println('0');
          written++;
          got++;

          if (got >= lim) break;
        }
      }
      delay(0);
    }

    http.end();
    if (!sawAnyItem) {
      Serial.printf("[POD] no <item> parsed for %s (%s)\n", show, effectiveUrl.c_str());
    }
    delay(0);
  }

  out.flush();
  out.close();
  f.close();

  if (written > 0) {
    // Swap without a "missing file" window (avoids UI briefly seeing playlistLength()==0).
    SPIFFS.remove(PLAYLIST_PODCAST_PATH ".bak");
    const bool hadOld = SPIFFS.exists(PLAYLIST_PODCAST_PATH);
    if (hadOld) {
      if (!SPIFFS.rename(PLAYLIST_PODCAST_PATH, PLAYLIST_PODCAST_PATH ".bak")) {
        Serial.println("[POD] rename old->bak failed; aborting update");
        SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
        return 0;
      }
    }

    if (!SPIFFS.rename(PLAYLIST_PODCAST_TMP_PATH, PLAYLIST_PODCAST_PATH)) {
      Serial.println("[POD] rename tmp->final failed; restoring previous list");
      SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
      if (hadOld) {
        SPIFFS.remove(PLAYLIST_PODCAST_PATH);
        (void)SPIFFS.rename(PLAYLIST_PODCAST_PATH ".bak", PLAYLIST_PODCAST_PATH);
      }
      return 0;
    }

    if (hadOld) {
      SPIFFS.remove(PLAYLIST_PODCAST_PATH ".bak");
    }
  } else {
    // Keep the last good list if refresh failed.
    SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
  }

  Serial.printf("[POD] built episodes: %lu (sources=%lu)\n",
                (unsigned long)written, (unsigned long)sources);
  setProgress(totalSources, totalSources, "done");
  return written;
}

static void podcastBuildTask(void*) {
  const bool force = s_forceBuild;
  s_forceBuild = false;

  // Only build if it makes sense (or forced).
  if (!force) {
    if (network.status != CONNECTED) {
      Serial.println("[POD] build skipped: not connected");
      s_podTask = nullptr;
      vTaskDelete(nullptr);
      return;
    }
  }

  // Even on forced builds, do not fight active playback or mode switches.
  if (shouldAbortBuild()) {
    Serial.println("[POD] build aborted (mode changed or playback started)");
    setProgress(0, 0, "");
    s_podTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  Serial.println("[POD] build start");
  const uint32_t wrote = podcasts_buildEpisodesPlaylist();
  if (wrote > 0) {
    config.indexPodcastPlaylist();
    config.initPodcastPlaylist();
    s_lastIndexMs = millis();
    const uint32_t now = epochNow();
    if (epochLooksValid(now)) {
      config.saveValue(&config.store.lastPodcastIndexEpoch, now);
    }
    if (display.ready() && config.getMode() == PM_PODCAST && display.mode() == STATIONS) {
      display.putRequest(DRAWPLAYLIST);
    }
  }
  Serial.println("[POD] build done");
  s_podTask = nullptr;
  vTaskDelete(nullptr);
}

void podcasts_requestBuild(bool force) {
  if (force) s_forceBuild = true;
  if (s_podTask) return;

  if (!force) {
    // Throttle refreshes so switching into Podcast mode isn't always expensive.
    // (Forced builds bypass this gate.)
    if (!podcasts_isIndexDue(3u * 60u * 60u)) return; // 3 hours

    // Keep this low-impact: only refresh while you're in Podcast mode and not playing.
    if (config.getMode() != PM_PODCAST) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!display.ready()) return;
    if (player.isRunning()) return;
  }

  // Run on the app side, but separate from MQTT callbacks / UI loop.
  xTaskCreatePinnedToCore(podcastBuildTask, "podBuild", PODBUILD_STACK, nullptr, 1, &s_podTask, 0);
}

bool podcasts_buildInProgress() {
  return s_podTask != nullptr;
}

