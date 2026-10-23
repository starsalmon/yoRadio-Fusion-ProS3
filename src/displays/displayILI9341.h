#ifndef displayILI9341_h
#define displayILI9341_h

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "fonts/bootlogo_cust128.h" //bootlogo99x64.h
#include "fonts/dsfont35.h"

typedef GFXcanvas16 Canvas;
typedef Adafruit_ILI9341 yoDisplay;

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
#define YO_TFT_BUSLOCK_WRITE 1
#endif
#include "tools/commongfx.h"

#if __has_include("conf/displayILI9341conf_custom.h")
  #include "conf/displayILI9341conf_custom.h"
#else
  #include "conf/displayILI9341conf.h"
#endif

#endif
