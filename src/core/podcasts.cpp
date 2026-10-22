#include "podcasts.h"

#include "config.h"
#include "display.h"
#include "network.h"
#include "player.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <new>
#include <esp_heap_caps.h>

#include "podcast_rss.h"

namespace {

static TaskHandle_t s_podTask = nullptr;
static bool s_forceBuild = false;
static uint32_t s_lastIndexMs = 0;
static volatile bool s_cancelBuild = false;

#ifndef PODBUILD_STACK
// The RSS parsing/build path uses HTTPClient + String heavy operations.
// On PROS3 this can overflow smaller task stacks (stack canary in "podBuild").
// Increase to avoid boot-loop when indexing on startup.
#define PODBUILD_STACK 32768
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
  if (s_cancelBuild) return true;
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

using podrss::sanitizeTsvField;

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

static int httpGetWithRedirects(HTTPClient& http, String& url, uint8_t maxHops,
                                const char* ifNoneMatch = nullptr, const char* ifModifiedSince = nullptr,
                                String* respEtag = nullptr, String* respLm = nullptr) {
  for (uint8_t hop = 0; hop <= maxHops; hop++) {
    if (shouldAbortBuild()) return -3;
    if (!httpBeginForUrl(http, url.c_str())) return -1;
    // Conditional GET (per-feed cache): server answers 304 if the feed is unchanged.
    if (ifNoneMatch && ifNoneMatch[0]) http.addHeader("If-None-Match", ifNoneMatch);
    if (ifModifiedSince && ifModifiedSince[0]) http.addHeader("If-Modified-Since", ifModifiedSince);
    // Ensure we can read Location (redirects) and the cache validators.
    const char* hdrs[] = {"Location", "location", "ETag", "Last-Modified"};
    http.collectHeaders(hdrs, 4);
    http.setConnectTimeout(4000);
    http.setTimeout(4000);
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

    // Keep the 304's validators so the caller can check them (some servers send 304 on
    // If-Modified-Since alone and ignore If-None-Match).
    if (respEtag) *respEtag = http.header("ETag");
    if (respLm) *respLm = http.header("Last-Modified");
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

#ifndef POD_RX_CHUNK
#define POD_RX_CHUNK 1536
#endif

struct FeedCtx {
  File* out;
  File* cache;   // per-feed cache being written (may be null)
  const char* show;
  uint16_t lim;
  uint16_t got;
  uint16_t items;
};

static bool onItem(const podrss::Item& it, void* user) {
  FeedCtx* c = (FeedCtx*)user;
  c->items++;
  char safeTitle[520];
  podrss::formatEpisodeLabel(c->show, it, safeTitle, sizeof(safeTitle));
  c->out->print(safeTitle);
  c->out->print('\t');
  c->out->print(it.url);
  c->out->print('\t');
  c->out->println('0');
  if (c->cache && *c->cache) {
    c->cache->print(safeTitle);
    c->cache->print('\t');
    c->cache->print(it.url);
    c->cache->print('\t');
    c->cache->println('0');
  }
  c->got++;
  return c->got < c->lim;
}

// Scanner (~4 KB) + RX chunk live in PSRAM when available, only during a build.
static podrss::Scanner* sc = nullptr;
static uint8_t* rx = nullptr;

static void* podAlloc(size_t n) {
#if defined(BOARD_HAS_PSRAM)
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return malloc(n);
}

static void freeBuffers() {
  if (sc) { sc->~Scanner(); free(sc); sc = nullptr; }
  if (rx) { free(rx); rx = nullptr; }
}

static bool allocBuffers() {
  if (!sc) {
    void* m = podAlloc(sizeof(podrss::Scanner));
    if (m) sc = new (m) podrss::Scanner();
  }
  if (!rx) rx = (uint8_t*)podAlloc(POD_RX_CHUNK);
  if (!sc || !rx) { freeBuffers(); return false; }
  return true;
}

// ---- Per-feed episode cache (SPIFFS) ----------------------------------------------------
// /data/pc_<hash>.tsv : header "#pc1\t<limit>\t<etag>\t<last-modified>" + that feed's lines
// (same format as podcast_episodes.csv). Lets unchanged feeds answer 304 (no body) and
// keeps a feed's last episodes if it fails to download.
#define POD_CACHE_DIR "/data"
#define POD_CACHE_PREFIX "pc_"
#define POD_CACHE_MIN_FREE 16384u   // don't write caches when SPIFFS is nearly full
#define POD_MAX_SOURCES 64

static uint32_t s_cacheIds[POD_MAX_SOURCES];
static uint16_t s_cacheIdCount = 0;

static uint32_t feedHash(const char* show, const char* url) {
  uint32_t h = 2166136261u;
  for (const char* p = show; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  h ^= '\t'; h *= 16777619u;
  for (const char* p = url; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  return h;
}

static void cachePath(uint32_t id, const char* ext, char* out, size_t outSz) {
  snprintf(out, outSz, POD_CACHE_DIR "/" POD_CACHE_PREFIX "%08lx.%s", (unsigned long)id, ext);
}

// Reads the cache header. Returns true if a cache with the same limit exists.
static bool cacheReadMeta(const char* path, uint16_t lim, char* etag, size_t etagSz, char* lm, size_t lmSz) {
  etag[0] = '\0'; lm[0] = '\0';
  File c = SPIFFS.open(path, "r");
  if (!c) return false;
  char line[256];
  size_t n = c.readBytesUntil('\n', line, sizeof(line) - 1);
  line[n] = '\0';
  c.close();
  if (strncmp(line, "#pc1\t", 5) != 0) return false;
  char* p = line + 5;
  char* t1 = strchr(p, '\t');
  if (!t1) return false;
  *t1 = '\0';
  if ((uint16_t)atoi(p) != lim) return false;
  char* e = t1 + 1;
  char* t2 = strchr(e, '\t');
  if (!t2) return false;
  *t2 = '\0';
  strlcpy(etag, e, etagSz);
  strlcpy(lm, t2 + 1, lmSz);
  return true;
}

// Appends the cached episode lines to `out`. Returns how many were copied.
static uint32_t cacheCopyTo(const char* path, File& out) {
  File c = SPIFFS.open(path, "r");
  if (!c) return 0;
  char line[1024];
  uint32_t lines = 0;
  bool first = true;
  while (c.available()) {
    size_t n = c.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\r') line[--n] = '\0';
    if (first) { first = false; continue; }  // header
    if (n == 0) continue;
    out.println(line);
    lines++;
  }
  c.close();
  return lines;
}

static bool spiffsHasRoom() {
  const size_t total = SPIFFS.totalBytes();
  const size_t used = SPIFFS.usedBytes();
  return total > used && (total - used) >= POD_CACHE_MIN_FREE;
}

// Remove caches of feeds that are no longer in podcasts.csv (only after a full pass).
static void cachePruneUnused() {
  File dir = SPIFFS.open(POD_CACHE_DIR);
  if (!dir || !dir.isDirectory()) return;
  char toRemove[8][40];
  uint8_t nRemove = 0;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    const char* nm = e.name();
    const char* base = strrchr(nm, '/');
    base = base ? base + 1 : nm;
    if (strncmp(base, POD_CACHE_PREFIX, 3) == 0 && strlen(base) == 15) {
      const uint32_t id = (uint32_t)strtoul(base + 3, nullptr, 16);
      bool used = false;
      for (uint16_t i = 0; i < s_cacheIdCount; i++) if (s_cacheIds[i] == id) { used = true; break; }
      if (!used || strcmp(base + 12, "tmp") == 0) {
        if (nRemove < 8) snprintf(toRemove[nRemove++], sizeof(toRemove[0]), POD_CACHE_DIR "/%s", base);
      }
    }
    e.close();
  }
  dir.close();
  for (uint8_t i = 0; i < nRemove; i++) {
    Serial.printf("[POD] cache prune %s\n", toRemove[i]);
    SPIFFS.remove(toRemove[i]);
  }
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

  if (!allocBuffers()) {
    out.close();
    f.close();
    SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
    Serial.println("[POD] out of memory for RSS buffers");
    setProgress(0, 0, "");
    return 0;
  }

  s_cacheIdCount = 0;
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
      freeBuffers();
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

    const uint32_t cid = feedHash(show, feedUrl);
    if (s_cacheIdCount < POD_MAX_SOURCES) s_cacheIds[s_cacheIdCount++] = cid;
    char cPath[40], cTmp[40];
    cachePath(cid, "tsv", cPath, sizeof(cPath));
    cachePath(cid, "tmp", cTmp, sizeof(cTmp));
    char etag[128], lastMod[64];
    const bool haveCache = cacheReadMeta(cPath, lim, etag, sizeof(etag), lastMod, sizeof(lastMod));

    HTTPClient http;
    String effectiveUrl(feedUrl);
    String rEtag, rLm;
    int code = haveCache ? httpGetWithRedirects(http, effectiveUrl, 5, etag, lastMod, &rEtag, &rLm)
                         : httpGetWithRedirects(http, effectiveUrl, 5);
    if (code == 304) {
      // Only trust a 304 whose validators match what we stored for this feed; otherwise
      // (or a 304 we never asked for) refetch unconditionally so a 200 is parsed.
      char rE[128], rL[64];
      sanitizeTsvField(rEtag.c_str(), rE, sizeof(rE));
      sanitizeTsvField(rLm.c_str(), rL, sizeof(rL));
      bool ok = haveCache && (etag[0] || lastMod[0]);
      if (ok && etag[0]) ok = (rE[0] == '\0') || strcmp(rE, etag) == 0;
      if (ok && !etag[0] && rL[0]) ok = strcmp(rL, lastMod) == 0;
      if (!ok) {
        Serial.printf("[POD] untrusted 304 (etag '%s' vs '%s'), refetching\n", rE, etag);
        effectiveUrl = feedUrl;
        code = httpGetWithRedirects(http, effectiveUrl, 5);
      }
    }
    if (code == -3) {  // cancelled during connect/redirects
      out.close();
      f.close();
      SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
      setProgress(0, 0, "");
      freeBuffers();
      return 0;
    }
    if (code == 304) {
      const uint32_t n = cacheCopyTo(cPath, out);
      Serial.printf("[POD] unchanged (304), %lu cached episodes\n", (unsigned long)n);
      written += n;
      delay(0);
      continue;
    }
    if (code <= 0 || code >= 300) {
      Serial.printf("[POD] feed HTTP %d for %s\n", code, effectiveUrl.c_str());
      if (haveCache) {
        const uint32_t n = cacheCopyTo(cPath, out);
        Serial.printf("[POD] using %lu cached episodes\n", (unsigned long)n);
        written += n;
      }
      delay(0);
      continue;
    }

    WiFiClient* s = http.getStreamPtr();
    if (!s) {
      http.end();
      if (haveCache) written += cacheCopyTo(cPath, out);
      continue;
    }

    // New cache file for this feed (written alongside the playlist, swapped in on success).
    File cache;
    if (spiffsHasRoom()) {
      SPIFFS.remove(cTmp);
      cache = SPIFFS.open(cTmp, "w");
      if (cache) {
        char hdr[256];
        String et = http.header("ETag");
        String lmv = http.header("Last-Modified");
        char etS[128], lmS[64];
        sanitizeTsvField(et.c_str(), etS, sizeof(etS));
        sanitizeTsvField(lmv.c_str(), lmS, sizeof(lmS));
        // Over-long validators would be truncated and never match: don't store them.
        if (et.length() >= sizeof(etS)) etS[0] = '\0';
        if (lmv.length() >= sizeof(lmS)) lmS[0] = '\0';
        snprintf(hdr, sizeof(hdr), "#pc1\t%u\t%s\t%s", (unsigned)lim, etS, lmS);
        cache.println(hdr);
      }
    }
    if (!cache) SPIFFS.remove(cPath);  // can't refresh the cache: drop stale validators

    FeedCtx ctx;
    ctx.out = &out;
    ctx.cache = &cache;
    ctx.show = show;
    ctx.lim = lim;
    ctx.got = 0;
    ctx.items = 0;
    sc->reset(onItem, &ctx);

    uint32_t lastRxMs = millis();
    bool aborted = false;
    bool timedOut = false;
    // Chunked reads + streaming scanner: each byte is touched once, no String churn.
    while (!sc->stopped() && (s->connected() || s->available())) {
      if (shouldAbortBuild()) { aborted = true; break; }
      const int avail = s->available();
      if (avail <= 0) {
        if ((uint32_t)(millis() - lastRxMs) > 4000u) { timedOut = true; break; }
        delay(1);
        continue;
      }
      const int want = (avail < (int)POD_RX_CHUNK) ? avail : (int)POD_RX_CHUNK;
      const int n = s->read(rx, (size_t)want);
      if (n <= 0) { delay(1); continue; }
      lastRxMs = millis();
      sc->feed((const char*)rx, (size_t)n);
      delay(0);
    }

    http.end();
    if (cache) cache.close();
    if (aborted) {
      SPIFFS.remove(cTmp);
      out.close();
      f.close();
      SPIFFS.remove(PLAYLIST_PODCAST_TMP_PATH);
      setProgress(0, 0, "");
      freeBuffers();
      return 0;
    }
    if (ctx.got > 0) {
      written += ctx.got;
      // Save validators only after a complete parse (limit reached or clean end of body).
      if (timedOut && !sc->stopped()) {
        SPIFFS.remove(cTmp);
        SPIFFS.remove(cPath);
        Serial.printf("[POD] partial read for %s, cache not saved\n", show);
      } else if (SPIFFS.exists(cTmp)) {
        SPIFFS.remove(cPath);
        if (!SPIFFS.rename(cTmp, cPath)) SPIFFS.remove(cTmp);
      }
    } else {
      SPIFFS.remove(cTmp);
      Serial.printf("[POD] no <item> parsed for %s (%s)\n", show, effectiveUrl.c_str());
      if (haveCache) {
        const uint32_t n = cacheCopyTo(cPath, out);
        Serial.printf("[POD] using %lu cached episodes\n", (unsigned long)n);
        written += n;
      }
    }
    delay(0);
  }

  out.flush();
  out.close();
  f.close();
  freeBuffers();
  // Full pass finished (not cancelled): drop caches of feeds no longer listed.
  if (s_cacheIdCount < POD_MAX_SOURCES) cachePruneUnused();

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
  // Don't clear s_cancelBuild here: a cancel requested before the task started must win.

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
  s_cancelBuild = false;

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

void podcasts_requestCancel() {
  // Best-effort: build checks this frequently while parsing RSS.
  s_cancelBuild = true;
}

