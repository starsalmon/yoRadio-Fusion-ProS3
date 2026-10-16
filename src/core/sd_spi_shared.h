#pragma once
#include "options.h"

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255

void yoradio_sd_spi_acquire();
void yoradio_sd_spi_release();

#endif
