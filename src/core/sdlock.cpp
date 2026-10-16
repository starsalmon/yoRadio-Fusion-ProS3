#include "sdlock.h"
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
#include "sd_spi_shared.h"
#endif

namespace {
SemaphoreHandle_t s_sdMtx = nullptr;

static inline void ensureInit() {
  if (s_sdMtx) return;
  s_sdMtx = xSemaphoreCreateMutex();
}
} // namespace

bool sdlock_take(TickType_t timeoutTicks) {
  ensureInit();
  if (!s_sdMtx) return false;
  if (xSemaphoreTake(s_sdMtx, timeoutTicks) != pdTRUE) return false;
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
  yoradio_sd_spi_acquire();
#endif
  return true;
}

void sdlock_give() {
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
  yoradio_sd_spi_release();
#endif
  if (!s_sdMtx) return;
  xSemaphoreGive(s_sdMtx);
}

