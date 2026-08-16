#include "album_art.h"

#include "options.h"

#if SD_ALBUM_ART_ENABLE

#include "Arduino.h"
#include "config.h"
#include "display.h"
#include "player.h"
#include "sdlock.h"

#include <JPEGDEC.h>

namespace {

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t s_task = nullptr;
static volatile bool s_hasPending = false;
static char s_pendingAudioPath[BUFLEN] = {0};

static uint16_t* s_bufA = nullptr;
static uint16_t* s_bufB = nullptr;
static uint16_t* s_front = nullptr; // published (stable) buffer
static uint16_t* s_back  = nullptr; // decode target
static uint16_t s_w = 0;            // published width
static uint16_t s_h = 0;            // published height
static char s_key[BUFLEN] = {0}; // last decoded art path

static bool ensureFixedBuffers() {
  // IMPORTANT: never malloc/free inside a critical section.
  // Allocate once at max size and reuse forever to avoid UAF on the display task.
  if (s_bufA && s_bufB && s_front && s_back) return true;

  constexpr uint16_t kMaxW = 80;
  constexpr uint16_t kMaxH = 80;
  const size_t bytes = (size_t)kMaxW * (size_t)kMaxH * sizeof(uint16_t);
  uint16_t* a = (uint16_t*)malloc(bytes);
  uint16_t* b = (uint16_t*)malloc(bytes);
  if (!a || !b) {
    if (a) free(a);
    if (b) free(b);
    return false;
  }
  memset(a, 0, bytes);
  memset(b, 0, bytes);

  portENTER_CRITICAL(&s_mux);
  // In case another call raced us (unlikely), keep the first allocation.
  if (!s_bufA && !s_bufB) {
    s_bufA = a;
    s_bufB = b;
    s_front = s_bufA;
    s_back  = s_bufB;
    s_w = 0;
    s_h = 0;
    s_key[0] = '\0';
    a = nullptr;
    b = nullptr;
  }
  portEXIT_CRITICAL(&s_mux);
  if (a) free(a);
  if (b) free(b);
  return (s_bufA && s_bufB && s_front && s_back);
}

static void publishBackBufferUnlocked(const char* key) {
  // Swap published pointer; old published buffer remains allocated (no UAF risk).
  uint16_t* tmp = s_front;
  s_front = s_back;
  s_back  = tmp;
  if (key) strlcpy(s_key, key, sizeof(s_key));
  else s_key[0] = '\0';
}

static bool buildDirAndPickArt(const char* audioPath, char* outArtPath, size_t outArtPathSz) {
  if (!audioPath || !audioPath[0] || !outArtPath || outArtPathSz == 0) return false;
  outArtPath[0] = '\0';

  // Serialize SD card operations (exists/open) against the audio local-file reader.
  if (!sdlock_take(pdMS_TO_TICKS(2000))) return false;

  // Derive directory of the audio file.
  char dir[BUFLEN];
  strlcpy(dir, audioPath, sizeof(dir));
  char* lastSlash = strrchr(dir, '/');
  if (!lastSlash) { sdlock_give(); return false; }
  if (lastSlash == dir) {
    // root dir
    dir[1] = '\0';
  } else {
    *lastSlash = '\0';
  }

  // Candidate filenames (common conventions).
  static const char* kNames[] = {
    "cover.jpg",
    "folder.jpg",
    "front.jpg",
    "album.jpg",
    "Cover.jpg",
    "Folder.jpg",
    "Front.jpg",
    "Album.jpg",
  };

  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
    const char* n = kNames[i];
    char p[BUFLEN];
    if (strcmp(dir, "/") == 0) snprintf(p, sizeof(p), "/%s", n);
    else snprintf(p, sizeof(p), "%s/%s", dir, n);
    if (config.SDPLFS() && config.SDPLFS()->exists(p)) {
      strlcpy(outArtPath, p, outArtPathSz);
      sdlock_give();
      return true;
    }
  }

  sdlock_give();
  return false;
}

struct DecodeCtx {
  uint16_t* dst = nullptr;
  uint16_t w = 0;
  uint16_t h = 0;
};

static int jpegDrawCallback(JPEGDRAW* pDraw) {
  if (!pDraw) return 0;
  DecodeCtx* ctx = (DecodeCtx*)pDraw->pUser;
  if (!ctx || !ctx->dst || ctx->w == 0 || ctx->h == 0) return 0;

  const int x = pDraw->x;
  const int y = pDraw->y;
  const int w = pDraw->iWidth;
  const int h = pDraw->iHeight;
  if (w <= 0 || h <= 0) return 1;

  // Bounds check; JPEGDEC should already constrain but be defensive.
  if (x >= (int)ctx->w || y >= (int)ctx->h) return 1;

  const int copyW = ((x + w) > (int)ctx->w) ? ((int)ctx->w - x) : w;
  const int copyH = ((y + h) > (int)ctx->h) ? ((int)ctx->h - y) : h;
  if (copyW <= 0 || copyH <= 0) return 1;

  const uint16_t* src = (const uint16_t*)pDraw->pPixels;
  for (int row = 0; row < copyH; row++) {
    uint16_t* drow = ctx->dst + (size_t)(y + row) * ctx->w + (size_t)x;
    const uint16_t* srow = src + (size_t)row * (size_t)w;
    memcpy(drow, srow, (size_t)copyW * sizeof(uint16_t));
  }
  return 1;
}

static void* jpegOpenCb(const char* szFilename, int32_t* pFileSize) {
  if (pFileSize) *pFileSize = 0;
  if (!szFilename || !config.SDPLFS()) return nullptr;
  if (!sdlock_take(pdMS_TO_TICKS(2000))) return nullptr;
  File f = config.SDPLFS()->open(szFilename, "r");
  sdlock_give();
  if (!f) return nullptr;
  File* fp = new File(f);
  if (!fp) return nullptr;
  if (pFileSize) *pFileSize = (int32_t)fp->size();
  return (void*)fp;
}

static void jpegCloseCb(void* pHandle) {
  File* fp = (File*)pHandle;
  if (!fp) return;
  fp->close();
  delete fp;
}

static int32_t jpegReadCb(JPEGFILE* pFile, uint8_t* pBuf, int32_t iLen) {
  if (!pFile || !pBuf || iLen <= 0) return 0;
  File* fp = (File*)pFile->fHandle;
  if (!fp) return 0;
  if (!sdlock_take(pdMS_TO_TICKS(2000))) return 0;
  const int32_t n = (int32_t)fp->read(pBuf, (size_t)iLen);
  sdlock_give();
  return n;
}

static int32_t jpegSeekCb(JPEGFILE* pFile, int32_t iPosition) {
  if (!pFile) return 0;
  File* fp = (File*)pFile->fHandle;
  if (!fp) return 0;
  if (!sdlock_take(pdMS_TO_TICKS(2000))) return 0;
  const bool ok = fp->seek((uint32_t)iPosition, SeekSet);
  sdlock_give();
  return ok ? iPosition : 0;
}

static void scaleCropCenterRgb565(const uint16_t* src, uint16_t srcW, uint16_t srcH,
                                  uint16_t* dst, uint16_t dstW, uint16_t dstH) {
  if (!src || !dst || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) return;

  // Compute a centered crop in source space that matches destination aspect ratio.
  // If source is wider than destination, crop width; otherwise crop height.
  uint32_t cropW = srcW;
  uint32_t cropH = srcH;
  if ((uint32_t)srcW * (uint32_t)dstH >= (uint32_t)srcH * (uint32_t)dstW) {
    // src wider
    cropH = srcH;
    cropW = ((uint32_t)srcH * (uint32_t)dstW) / (uint32_t)dstH;
    if (cropW == 0) cropW = 1;
    if (cropW > srcW) cropW = srcW;
  } else {
    // src taller
    cropW = srcW;
    cropH = ((uint32_t)srcW * (uint32_t)dstH) / (uint32_t)dstW;
    if (cropH == 0) cropH = 1;
    if (cropH > srcH) cropH = srcH;
  }

  const uint32_t cropX = (srcW > cropW) ? ((uint32_t)srcW - cropW) / 2u : 0u;
  const uint32_t cropY = (srcH > cropH) ? ((uint32_t)srcH - cropH) / 2u : 0u;

  // Nearest-neighbor resample crop->dst.
  for (uint16_t y = 0; y < dstH; y++) {
    const uint32_t sy = cropY + ((uint32_t)y * cropH) / (uint32_t)dstH;
    const uint16_t* srow = src + (size_t)sy * srcW;
    uint16_t* drow = dst + (size_t)y * dstW;
    for (uint16_t x = 0; x < dstW; x++) {
      const uint32_t sx = cropX + ((uint32_t)x * cropW) / (uint32_t)dstW;
      drow[x] = srow[sx];
    }
  }
}

static void decodeTask(void*) {
  for (;;) {
    char audioPath[BUFLEN];
    audioPath[0] = '\0';

    portENTER_CRITICAL(&s_mux);
    const bool has = s_hasPending;
    if (has) {
      s_hasPending = false;
      strlcpy(audioPath, s_pendingAudioPath, sizeof(audioPath));
    }
    portEXIT_CRITICAL(&s_mux);

    if (!has) break;

    // Only decode for SD mode.
    if (config.getMode() != PM_SDCARD) {
      portENTER_CRITICAL(&s_mux);
      // Keep buffers allocated; just clear published image.
      s_key[0] = '\0';
      portEXIT_CRITICAL(&s_mux);
      continue;
    }

    char artPath[BUFLEN];
    if (!buildDirAndPickArt(audioPath, artPath, sizeof(artPath))) {
      portENTER_CRITICAL(&s_mux);
      s_key[0] = '\0';
      portEXIT_CRITICAL(&s_mux);
      continue;
    }

    // Target dimensions: match station-logo sizes (64 on default VU layout, otherwise 80).
    const bool want64 = (config.store.vuLayout == 0);
    const uint16_t dstW = want64 ? 64 : 80;
    const uint16_t dstH = want64 ? 64 : 80;

    if (!ensureFixedBuffers()) {
      portENTER_CRITICAL(&s_mux);
      s_key[0] = '\0';
      portEXIT_CRITICAL(&s_mux);
      continue;
    }

    // Skip if we already decoded this art.
    bool same = false;
    portENTER_CRITICAL(&s_mux);
    same = (s_key[0] && strcmp(s_key, artPath) == 0 && s_w == dstW && s_h == dstH && s_front != nullptr);
    portEXIT_CRITICAL(&s_mux);
    if (same) continue;

    JPEGDEC jpeg;
    DecodeCtx ctx;

    if (!jpeg.open(artPath, jpegOpenCb, jpegCloseCb, jpegReadCb, jpegSeekCb, jpegDrawCallback)) {
      portENTER_CRITICAL(&s_mux);
      s_key[0] = '\0';
      portEXIT_CRITICAL(&s_mux);
      continue;
    }

    int srcW = jpeg.getWidth();
    int srcH = jpeg.getHeight();

    // Pick an efficient downscale so we get a reasonably-sized square/rect.
    // Keep it fairly large for TFT (but still safe for smaller displays).
    constexpr int kMaxDim = 160;
    int options = JPEG_LE_PIXELS;
    int div = 1;
    int maxSrc = (srcW > srcH) ? srcW : srcH;
    if (maxSrc > kMaxDim * 8)      { options |= JPEG_SCALE_EIGHTH; div = 8; }
    else if (maxSrc > kMaxDim * 4) { options |= JPEG_SCALE_QUARTER; div = 4; }
    else if (maxSrc > kMaxDim * 2) { options |= JPEG_SCALE_HALF; div = 2; }

    const uint16_t outW = (uint16_t)((srcW + (div - 1)) / div);
    const uint16_t outH = (uint16_t)((srcH + (div - 1)) / div);
    if (outW == 0 || outH == 0) {
      jpeg.close();
      portENTER_CRITICAL(&s_mux);
      s_key[0] = '\0';
      portEXIT_CRITICAL(&s_mux);
      continue;
    }

    uint16_t* tmp = (uint16_t*)malloc((size_t)outW * (size_t)outH * sizeof(uint16_t));
    if (!tmp) {
      jpeg.close();
      continue;
    }
    // Fill with background-ish black; JPEG may not write every pixel if it errors.
    memset(tmp, 0, (size_t)outW * (size_t)outH * sizeof(uint16_t));

    ctx.dst = tmp;
    ctx.w = outW;
    ctx.h = outH;
    jpeg.setUserPointer(&ctx);

    const int ok = jpeg.decode(0, 0, options);
    jpeg.close();

    if (!ok) {
      free(tmp);
      portENTER_CRITICAL(&s_mux);
      s_key[0] = '\0';
      portEXIT_CRITICAL(&s_mux);
      continue;
    }

    // Render into the *back* buffer (not visible), then publish with a tiny pointer swap.
    uint16_t* back = nullptr;
    portENTER_CRITICAL(&s_mux);
    back = s_back;
    portEXIT_CRITICAL(&s_mux);

    if (back) {
      // Clear only the region we will show (64x64 or 80x80).
      memset(back, 0, (size_t)dstW * (size_t)dstH * sizeof(uint16_t));
      scaleCropCenterRgb565(tmp, outW, outH, back, dstW, dstH);
      portENTER_CRITICAL(&s_mux);
      s_w = dstW;
      s_h = dstH;
      publishBackBufferUnlocked(artPath);
      portEXIT_CRITICAL(&s_mux);
    } else {
      portENTER_CRITICAL(&s_mux);
      s_key[0] = '\0';
      portEXIT_CRITICAL(&s_mux);
    }
    free(tmp);

    // Nudge the UI to refresh the image slot.
    if (display.ready() && config.getMode() == PM_SDCARD) {
      display.putRequest(NEWSTATION);
    }
  }

  s_task = nullptr;
  vTaskDelete(nullptr);
}

} // namespace

void album_art_request_for_sd_track(const char* audioPath) {
  if (!audioPath || !audioPath[0]) return;

  portENTER_CRITICAL(&s_mux);
  strlcpy(s_pendingAudioPath, audioPath, sizeof(s_pendingAudioPath));
  s_hasPending = true;
  portEXIT_CRITICAL(&s_mux);

  if (s_task) return;
  xTaskCreatePinnedToCore(decodeTask, "albArt", 12288, nullptr, 1, &s_task, 0);
}

bool album_art_get_current(const uint16_t** outPixels, uint16_t* outW, uint16_t* outH) {
  if (outPixels) *outPixels = nullptr;
  if (outW) *outW = 0;
  if (outH) *outH = 0;
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  if (s_front && s_w && s_h && s_key[0]) {
    if (outPixels) *outPixels = s_front;
    if (outW) *outW = s_w;
    if (outH) *outH = s_h;
    ok = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

void album_art_clear() {
  portENTER_CRITICAL(&s_mux);
  s_key[0] = '\0';
  s_pendingAudioPath[0] = '\0';
  s_hasPending = false;
  portEXIT_CRITICAL(&s_mux);
}

// Hook: called from Display::_title() on track change.
void player_on_track_change() {
  if (config.getMode() != PM_SDCARD) return;
  album_art_request_for_sd_track(config.station.url);
}

#else // SD_ALBUM_ART_ENABLE == 0

void album_art_request_for_sd_track(const char*) {}
bool album_art_get_current(const uint16_t**, uint16_t*, uint16_t*) { return false; }
void album_art_clear() {}

#endif

