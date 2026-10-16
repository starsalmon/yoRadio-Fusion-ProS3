#pragma once
#include "options.h"

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255

extern "C" void yoradio_sd_spi_acquire();
extern "C" void yoradio_sd_spi_release();
bool yoradio_sd_spi_bus_busy();

#endif
