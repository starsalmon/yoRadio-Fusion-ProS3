#include "sd_spi_shared.h"

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255

#include <Arduino.h>
#include "spidog.h"

void yoradio_sd_spi_acquire() {
  sdog.begin();
  sdog.takeMutex();
#if defined(TFT_CS) && (TFT_CS != 255)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
}

void yoradio_sd_spi_release() {
  sdog.giveMutex();
}

#endif
