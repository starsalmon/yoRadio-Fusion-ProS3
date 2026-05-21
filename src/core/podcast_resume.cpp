#include "podcast_resume.h"

#include <Arduino.h>
#include "SPIFFS.h"

namespace {

static constexpr const char* kPath = "/data/podcast_resume.bin";
static constexpr uint32_t kMagic = 0x50523031u; // "PR01"
static constexpr size_t kMaxEntries = 10;

struct Header {
  uint32_t magic;
  uint32_t version;
  uint32_t count;
};

struct Entry {
  uint32_t key;      // hash(url)
  uint32_t sec;      // resume position (seconds)
  uint32_t durSec;   // last known duration
  uint32_t lastMs;   // monotonic-ish for LRU (millis snapshot)
};

static bool s_loaded = false;
static bool s_dirty = false;
static uint32_t s_lastFlushMs = 0;
static Entry s_entries[kMaxEntries] = {};

static uint32_t fnv1a32(const char* s) {
  if (!s) return 0;
  uint32_t h = 2166136261u;
  for (const uint8_t* p = (const uint8_t*)s; *p; ++p) {
    h ^= (uint32_t)(*p);
    h *= 16777619u;
  }
  return h ? h : 1u; // avoid 0 as a sentinel
}

static void loadOnce() {
  if (s_loaded) return;
  s_loaded = true;

  if (!SPIFFS.begin(true)) {
    // If SPIFFS isn't ready, we just operate in-memory and try later.
    return;
  }

  if (!SPIFFS.exists(kPath)) return;
  File f = SPIFFS.open(kPath, "rb");
  if (!f) return;

  Header hdr{};
  if (f.readBytes((char*)&hdr, sizeof(hdr)) != sizeof(hdr)) return;
  if (hdr.magic != kMagic || hdr.version != 1u) return;
  if (hdr.count != (uint32_t)kMaxEntries) return;

  const size_t want = sizeof(s_entries);
  if (f.readBytes((char*)s_entries, want) != (int)want) {
    memset(s_entries, 0, sizeof(s_entries));
    return;
  }
}

static void flushIfNeeded(bool force) {
  if (!s_dirty) return;

  const uint32_t now = millis();
  if (!force) {
    // Avoid pounding SPIFFS on every 1s UI tick.
    if (s_lastFlushMs != 0 && (uint32_t)(now - s_lastFlushMs) < 5000u) return;
  }

  if (!SPIFFS.begin(true)) return;

  File f = SPIFFS.open(kPath, "wb");
  if (!f) return;

  Header hdr{};
  hdr.magic = kMagic;
  hdr.version = 1u;
  hdr.count = (uint32_t)kMaxEntries;
  f.write((const uint8_t*)&hdr, sizeof(hdr));
  f.write((const uint8_t*)s_entries, sizeof(s_entries));
  f.close();

  s_lastFlushMs = now;
  s_dirty = false;
}

static int findIdx(uint32_t key) {
  for (size_t i = 0; i < kMaxEntries; i++) {
    if (s_entries[i].key == key) return (int)i;
  }
  return -1;
}

static int findVictimIdx() {
  // Prefer empty slot.
  for (size_t i = 0; i < kMaxEntries; i++) {
    if (s_entries[i].key == 0) return (int)i;
  }
  // Else least-recently-updated.
  uint32_t bestMs = s_entries[0].lastMs;
  int best = 0;
  for (size_t i = 1; i < kMaxEntries; i++) {
    if (s_entries[i].lastMs < bestMs) {
      bestMs = s_entries[i].lastMs;
      best = (int)i;
    }
  }
  return best;
}

} // namespace

bool podcast_resume_get_sec(const char* url, uint32_t* outSec) {
  if (outSec) *outSec = 0;
  if (!url || !url[0]) return false;

  loadOnce();
  const uint32_t key = fnv1a32(url);
  const int idx = findIdx(key);
  if (idx < 0) return false;

  const uint32_t sec = s_entries[idx].sec;
  if (sec == 0) return false;
  if (outSec) *outSec = sec;

  // Touch LRU.
  s_entries[idx].lastMs = millis();
  s_dirty = true;
  flushIfNeeded(false);
  return true;
}

void podcast_resume_update_sec(const char* url, uint32_t curSec, uint32_t durSec, bool forceFlush) {
  if (!url || !url[0]) return;

  loadOnce();
  const uint32_t key = fnv1a32(url);
  int idx = findIdx(key);
  if (idx < 0) idx = findVictimIdx();

  // If we're near EOF (or duration unknown), avoid resuming into the last seconds.
  uint32_t secToStore = curSec;
  if (durSec > 0) {
    if (curSec >= (durSec > 2 ? (durSec - 2) : durSec)) secToStore = 0;
  } else {
    // Without duration, keep a small sanity clamp so we don't store nonsense.
    if (curSec > 24u * 3600u) secToStore = 0;
  }

  Entry& e = s_entries[idx];
  const bool changed = (e.key != key) || (e.sec != secToStore) || (e.durSec != durSec);
  if (!changed) {
    // Still touch LRU.
    e.lastMs = millis();
    return;
  }

  e.key = key;
  e.sec = secToStore;
  e.durSec = durSec;
  e.lastMs = millis();
  s_dirty = true;
  flushIfNeeded(forceFlush);
}

