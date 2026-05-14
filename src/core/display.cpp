// Modified in v0.9.710 ("vol_step") by Tamás Várai
#include "Arduino.h"
#include "options.h"
#include "SPIFFS.h"
#include "WiFi.h"
#include "time.h"
#include "config.h"
#include "display.h"
#include "player.h"
#include "network.h"
#include "netserver.h"
#include "timekeeper.h"
#include "../pluginsManager/pluginsManager.h"
#include "../displays/dspcore.h"
#include "../displays/widgets/widgets.h"
#include "../displays/widgets/pages.h"
#include "../displays/tools/l10n.h"
#include "../battery.h"
#include "../displays/bitmaps/footer_icons_16.h"
#include "../myoptions.h"
#include "sdmanager.h"

Display display;
#ifdef USE_NEXTION
#include "../displays/nextion.h"
Nextion nextion;
#endif

#ifndef CORE_STACK_SIZE
  // Display task does layout, string splitting, JPEG decode callbacks, etc.
  // 8k was too tight and can trip the stack canary (see DspTask crashes).
  #define CORE_STACK_SIZE  1024*16
#endif
#ifndef DSP_TASK_PRIORITY
  #define DSP_TASK_PRIORITY  2
#endif
#ifndef DSP_TASK_CORE_ID
  #define DSP_TASK_CORE_ID  0
#endif
#ifndef DSP_TASK_DELAY
  #define DSP_TASK_DELAY pdMS_TO_TICKS(10) // cap for 50 fps
#endif

#define DSP_QUEUE_TICKS 0

#ifndef DSQ_SEND_DELAY
  //#define DSQ_SEND_DELAY portMAX_DELAY
  #define DSQ_SEND_DELAY  pdMS_TO_TICKS(200)
#endif

#ifndef SAVER_Y_MIN
#define SAVER_Y_MIN TFT_FRAMEWDT
#endif

#ifndef BOOT_PRG_COLOR
  #define BOOT_PRG_COLOR 0xFFFF
#endif
#ifndef BOOT_TXT_COLOR
  #define BOOT_TXT_COLOR 0xFFFF
#endif

QueueHandle_t displayQueue;

static char batteryIconGlyph(float pct, bool charging) {
  // 8 dedicated classic glyph slots in fonts/glcdfont_EN.c
  // Non-charging:   027 high, 030 med, 031 low, 032 vlow
  // Charging:       033 high, 036 med, 037 low, 017 vlow
  if (charging) {
    if (pct >= 75.0f) return '\033';
    if (pct >= 45.0f) return '\036';
    if (pct >= 20.0f) return '\037';
    return '\017';
  }
  if (pct >= 75.0f) return '\027';
  if (pct >= 45.0f) return '\030';
  if (pct >= 20.0f) return '\031';
  return '\032';
}

static uint16_t batteryPctColor565(float pct) {
  // Match the user's LVGL palette using RGB565 output.
  int level = (int)(pct + 0.5f);
  if (level < 0) level = 0;
  if (level > 100) level = 100;

  const auto rgb565 = [](uint8_t r, uint8_t g, uint8_t b) -> uint16_t {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  };

  // tomato / orange / yellow / lightgreen / greenyellow
  if (level <= 20)      return rgb565(0xFF, 0x63, 0x47);
  else if (level <= 40) return rgb565(0xFF, 0x8C, 0x00);
  else if (level <= 60) return rgb565(0xFF, 0xD7, 0x00);
  else if (level <= 80) return rgb565(0x90, 0xEE, 0x90);
  else                  return rgb565(0xAD, 0xFF, 0x2F);
}

static void loopDspTask(void * pvParameters){
  while(true){
  #ifndef DUMMYDISPLAY
    if(displayQueue==NULL) break;
    if(timekeeper.loop0()){
      display.loop();
    #ifndef NETSERVER_LOOP1
      netserver.loop();
    #endif
    }
  #else
    timekeeper.loop0();
    #ifndef NETSERVER_LOOP1
      netserver.loop();
    #endif
  #endif
    vTaskDelay(DSP_TASK_DELAY);
  }
  vTaskDelete( NULL );
}

void Display::_createDspTask(){
  xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE,  NULL,  DSP_TASK_PRIORITY, NULL, DSP_TASK_CORE_ID);
}

#ifndef DUMMYDISPLAY
//============================================================================================================================
DspCore dsp;

Page *pages[] = { new Page(), new Page(), new Page(), new Page() };

#if !((DSP_MODEL==DSP_ST7735 && DTYPE==INITR_BLACKTAB) || DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7796 || DSP_MODEL==DSP_ILI9488 \
 || DSP_MODEL==DSP_ILI9486 || DSP_MODEL==DSP_ILI9341 || DSP_MODEL==DSP_ILI9225 || DSP_MODEL==DSP_ST7789_170 || DSP_MODEL==DSP_ST7789_240 || DSP_MODEL==DSP_NV3041A)
  #undef  BITRATE_FULL
  #define BITRATE_FULL     false
#endif


void returnPlayer(){
  display.putRequest(NEWMODE, PLAYER);
}

Display::~Display() {
  delete _pager;
  delete _footer;
  delete _plwidget;
  delete _nums;
  delete _clock;
  delete _date;
  delete _meta;
  delete _title1;
  delete _title2;
  delete _plcurrent;
  if (_stationLogoScaled) {
    free(_stationLogoScaled);
    _stationLogoScaled = nullptr;
  }
}

void Display::init() {
  Serial.print("##[BOOT]#\tdisplay.init\t");
#ifdef USE_NEXTION
  nextion.begin();
#endif
#if LIGHT_SENSOR!=255
  analogSetAttenuation(ADC_0db);
#endif
  _bootStep = 0;
  dsp.initDisplay();
  displayQueue=NULL;
  displayQueue = xQueueCreate( 5, sizeof( requestParams_t ) );
  while(displayQueue==NULL){;}
  _createDspTask();
  while(_bootStep != 0) { delay(10); }
  //_pager.begin();
  //_bootScreen();
  _pager = new Pager();
  _footer = new Page();
  _plwidget = new PlayListWidget();
  _nums = new NumWidget();
  _clock = new ClockWidget();
  _date = new DateWidget();
  _meta = new ScrollWidget();
  _title1 = new ScrollWidget();
  _plcurrent = new ScrollWidget();
  Serial.println("done");
}

uint16_t Display::width(){ return dsp.width(); }
uint16_t Display::height(){ return dsp.height(); }
#if (TIME_SIZE > 19) && (DSP_MODEL != DSP_ST7735)

  #undef BOOT_PRG_COLOR
  #undef BOOT_TXT_COLOR
  
  #if DSP_MODEL==DSP_SSD1322
    #define BOOT_PRG_COLOR    WHITE
    #define BOOT_TXT_COLOR    WHITE
    #define PINK              WHITE
  #elif DSP_MODEL==DSP_SSD1327
    #define BOOT_PRG_COLOR    0x07
    #define BOOT_TXT_COLOR    0x3f
    #define PINK              0x02
  #else
    #define BOOT_PRG_COLOR    0xE68B
    #define BOOT_TXT_COLOR    0xFFFF
    #define PINK              0xF97F
  #endif
#endif

void Display::_bootScreen(){
  _boot = new Page();
  _boot->addWidget(new ProgressWidget(bootWdtConf, bootPrgConf, BOOT_PRG_COLOR, 0));
  _bootstring = (TextWidget*) &_boot->addWidget(new TextWidget(bootstrConf, 50, true, BOOT_TXT_COLOR, 0));
  _pager->addPage(_boot);
  _pager->setPage(_boot, true);
  dsp.drawLogo(bootLogoTop);
  _bootStep = 1;
}

void Display::_buildPager(){
  // Header bar uses the theme "fill" color. Many themes set STATION_BG dark
  // and STATION_FILL as the intended header color.
  _meta->init("*", metaConf, config.theme.meta, config.theme.metafill);
  _title1->init("*", title1Conf, config.theme.title1, config.theme.background);
  _clock->init(getclockConf(), 0, 0);
  _date->init(getDateConf(), config.theme.date, config.theme.background);
  _date->setNamedayEnabled(config.store.showNameday);
  #if DSP_MODEL==DSP_NOKIA5110
    _plcurrent->init("*", playlistConf, 0, 1);
  #else
    _plcurrent->init("*", playlistConf, config.theme.plcurrent, config.theme.plcurrentbg);
  #endif
  _plwidget->init(_plcurrent);
  #if !defined(DSP_LCD)
    _plcurrent->moveTo({TFT_FRAMEWDT, (uint16_t)(_plwidget->currentTop()), (int16_t)playlistConf.width});
  #endif
  #ifndef HIDE_TITLE2
    _title2 = new ScrollWidget("*", title2Conf, config.theme.title2, config.theme.background);
  #endif
  #if !defined(DSP_LCD) && DSP_MODEL!=DSP_NOKIA5110
    _plbackground = new FillWidget(playlBGConf, config.theme.plcurrentfill);
    // Ensure the whole player page background is painted every time the page redraws.
    // Without a full-page fill, stale pixels (often black from the boot page) can remain
    // visible in areas not covered by other widgets until the first full page transition.
    FillWidget* _playerBackground = new FillWidget(
      FillConfig{ { 0, 0, 0, WA_LEFT }, (uint16_t)dsp.width(), (uint16_t)dsp.height(), false },
      config.theme.background
    );
    FillWidget* _metabackground = nullptr;
    FillWidget* _metaline = nullptr;
    // Always paint the full header background.
    // (Previously, when stationLine was enabled, only the 1px line widget was created,
    // leaving most of the header bar unpainted.)
    _metabackground = new FillWidget(metaBGConf, config.theme.metafill);
    if (config.store.stationLine) {
      _metaline = new FillWidget(metaBGConfLine, config.theme.div);
    }
  #endif
  #if DSP_MODEL==DSP_NOKIA5110
    _plbackground = new FillWidget(playlBGConf, 1);
    //_metabackground = new FillWidget(metaBGConf, 1);
  #endif
  #ifndef HIDE_VU
    _vuwidget = new VuWidget(getvuConf(), getbandsConf(), config.theme.vumax, config.theme.vumin, config.theme.background);
  #endif
  #ifndef HIDE_VOLBAR
    _volbar = new SliderWidget(volbarConf, config.theme.volbarin, config.theme.background, 100, config.theme.volbarout); // "vol_step"
  #endif
  #ifndef HIDE_HEAPBAR
    _heapbar = new SliderWidget(heapbarConf, config.theme.buffer, config.theme.background, psramInit()?300000:1600 * config.store.abuff);
  #endif
  #ifndef HIDE_VOL
    _voltxt = new TextWidget(voltxtConf, 10, false, config.theme.vol, config.theme.background);
  #endif
  #if !defined(HIDE_BAT) && (!defined(BATTERY_ENABLED) || (BATTERY_ENABLED != 0))
    _battxt = new TextWidget(battxtConf, 20, false, config.theme.rssi, config.theme.background);
  #endif
  #ifndef HIDE_IP
    _volip = new TextWidget(iptxtConf, 30, false, config.theme.ip, config.theme.background);
  #endif
  #ifndef HIDE_RSSI
    #if DSP_MODEL!=DSP_ILI9341
      _rssi = new TextWidget(rssiConf, 20, false, config.theme.rssi, config.theme.background);
    #endif
  #endif

  // Footer icons for ILI9341: keep classic glyph icons alongside smooth text.
  #if DSP_MODEL==DSP_ILI9341
    #ifndef HIDE_IP
      _ipIcon = new BitmapWidget(ipiconConf, ICON_LAN_16x18, ICON_LAN_W, ICON_LAN_H, config.theme.ip, config.theme.background, BitmapFormat::GFX_MSB);
    #endif
    #ifndef HIDE_VOL
      {
        int vol = config.store.volume;
        if (vol > 100) vol = 100;
        if (vol < 0) vol = 0;
        const uint8_t* vbmp = (vol <= 33) ? ICON_VOL_MUTE_18 : (vol <= 66) ? ICON_VOL_DOWN_18 : ICON_VOL_UP_18;
        _volIcon = new BitmapWidget(voliconConf, vbmp, ICON_VOL_W, ICON_VOL_H, config.theme.vol, config.theme.background, BitmapFormat::GFX_MSB);
      }
    #endif
    #if !defined(HIDE_BAT) && (!defined(BATTERY_ENABLED) || (BATTERY_ENABLED != 0))
      {
        const float pct = battery_is_ready() ? battery_get_percent() : 0.0f;
        const auto bmpForPct = [](float p) -> const uint8_t* {
          if (p >= 99.5f) return ICON_BAT_FULL_12;
          if (p >= 91.0f) return ICON_BAT_90_12;
          if (p >= 80.0f) return ICON_BAT_80_12;
          if (p >= 60.0f) return ICON_BAT_60_12;
          if (p >= 40.0f) return ICON_BAT_40_12;
          if (p >= 20.0f) return ICON_BAT_20_12;
          if (p >= 10.0f) return ICON_BAT_10_12;
          return ICON_BAT_EMPTY_12;
        };
        const uint8_t* bmp = bmpForPct(pct);
        uint16_t fg = config.theme.rssi;
        #if defined(FOOTER_BATTERY_COLORIZE) && (FOOTER_BATTERY_COLORIZE != 0)
          fg = batteryPctColor565(pct);
        #endif
        _batIcon = new BitmapWidget(baticonConf, bmp, ICON_BAT_W, ICON_BAT_H, fg, config.theme.background, BitmapFormat::GFX_MSB);
      }
      // Charging bolt icon (visible only when 5V sense is on)
      {
        const uint8_t* bbmp = battery_usb_present() ? ICON_BOLT_9x12 : nullptr;
        _batChgIcon = new BitmapWidget(batchgConf, bbmp, ICON_BOLT_W, ICON_BOLT_H, config.theme.rssi, config.theme.background, BitmapFormat::GFX_MSB);
      }
    #endif
    #ifndef HIDE_RSSI
      _rssiIcon = new TextWidget(rssibarConf, 4, false, config.theme.rssi, config.theme.background);
      _rssiIcon->setText("\001\002"); // default (weak signal) until first RSSI update
    #endif
  #endif
  _nums->init(numConf, 10, false, config.theme.digit, config.theme.background);
  #ifndef HIDE_WEATHER
    _weather = new ScrollWidget("\007", weatherConf, config.theme.weather, config.theme.background);
   #if DSP_MODEL==DSP_ILI9488 || DSP_MODEL==DSP_ILI9486 || DSP_MODEL==DSP_NV3041A || DSP_MODEL==DSP_ST7796 || DSP_MODEL==DSP_ILI9341 || DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7789_170
    _weatherIcon  = new WeatherIconWidget();
    _weatherIcon->init(getWeatherIconConf(), config.theme.background);
   #endif
  #endif

  // Station logo (RGB565). Shown only for known stations.
  _stationLogo = new RgbImageWidget();
  _stationLogo->init({0, 0, 0, WA_LEFT}, config.theme.background);
  _stationLogo->lock(true);
  
  #if DSP_MODEL==DSP_ILI9341
    if(_volIcon)  _footer->addWidget(_volIcon);
    if(_ipIcon)   _footer->addWidget(_ipIcon);
    if(_batChgIcon) _footer->addWidget(_batChgIcon);
    if(_batIcon)  _footer->addWidget(_batIcon);
    if(_rssiIcon) _footer->addWidget(_rssiIcon);
  #endif
  if(_voltxt)   _footer->addWidget( _voltxt);
  if(_volip)    _footer->addWidget( _volip);
  if(_battxt)   _footer->addWidget( _battxt);
  if(_rssi)     _footer->addWidget( _rssi);
  if(_heapbar)  _footer->addWidget( _heapbar);
  // Draw volume bar last so it stays visible even if any icon clears overlap it.
  if(_volbar)   _footer->addWidget( _volbar);
  
  if(_playerBackground) pages[PG_PLAYER]->addWidget(_playerBackground);
  if(_metabackground) pages[PG_PLAYER]->addWidget(_metabackground);
  if(_metaline) pages[PG_PLAYER]->addWidget(_metaline);
  if(_stationLogo) pages[PG_PLAYER]->addWidget(_stationLogo);
  pages[PG_PLAYER]->addWidget(_meta);
  pages[PG_PLAYER]->addWidget(_title1);
  if(_title2) pages[PG_PLAYER]->addWidget(_title2);
  if(_weather) pages[PG_PLAYER]->addWidget(_weather);
  #if DSP_MODEL==DSP_ILI9488 || DSP_MODEL==DSP_ILI9486 || DSP_MODEL==DSP_NV3041A || DSP_MODEL==DSP_ST7796 || DSP_MODEL==DSP_ST7789  || DSP_MODEL==DSP_ILI9341
  // Show the weather icon on all layouts
    if (_weatherIcon) pages[PG_PLAYER]->addWidget(_weatherIcon);
  #elif DSP_MODEL==DSP_ST7789_170
  // Show the weather icon only on non-default layouts
    if (_weatherIcon && config.store.vuLayout != 0) {
      pages[PG_PLAYER]->addWidget(_weatherIcon);
    }
  #endif
  #if BITRATE_FULL
    _fullbitrate = new BitrateWidget(getfullbitrateConf(), config.theme.bitrate, config.theme.background);
    pages[PG_PLAYER]->addWidget( _fullbitrate);
  #else
    _bitrate = new TextWidget(bitrateConf, 30, false, config.theme.bitrate, config.theme.background);
    pages[PG_PLAYER]->addWidget( _bitrate);
  #endif
  // Station number and play-mode widgets (left/right side of meta row)
  #if STATION_WIDGETS
   #if DSP_MODEL!=DSP_ST7735 && DSP_MODEL!=DSP_ST7789_240 && DSP_MODEL!=DSP_GC9A01 && DSP_MODEL!=DSP_GC9A01A && DSP_MODEL!=DSP_GC9A01_I80
    // Use the header/meta fill colour as background so these widgets never
    // "punch holes" into the top bar during early boot/layout changes.
    _stationNum = new StationNumWidget(getstationNumConf(), config.theme.div, config.theme.metafill);
    pages[PG_PLAYER]->addWidget(_stationNum);
    _playMode   = new PlayModeWidget(getplayModeConf(), config.theme.div, config.theme.metafill);
    pages[PG_PLAYER]->addWidget(_playMode);
   #endif 
  #endif
  #if DSP_MODEL==DSP_ILI9488 || DSP_MODEL==DSP_ILI9486 || DSP_MODEL==DSP_NV3041A || DSP_MODEL==DSP_ST7796
    _speechWidget = new StatusWidget(getstatusConf(0), "TTS", config.theme.bitrate, config.theme.clockbg);
    pages[PG_PLAYER]->addWidget(_speechWidget);
    _blfadeWidget = new StatusWidget(getstatusConf(1), "FADE", config.theme.bitrate, config.theme.clockbg);
    pages[PG_PLAYER]->addWidget(_blfadeWidget);
    #ifdef USE_LEDSTRIP_PLUGIN
    _lstripWidget = new StatusWidget(getstatusConf(2), "RGB", config.theme.bitrate, config.theme.clockbg);
    pages[PG_PLAYER]->addWidget(_lstripWidget);
    #endif
  #endif
  if(_vuwidget) pages[PG_PLAYER]->addWidget( _vuwidget);
  pages[PG_PLAYER]->addWidget(_clock);
  pages[PG_SCREENSAVER]->addWidget(_clock);
  pages[PG_PLAYER]->addWidget(_date);
  #if DSP_MODEL==DSP_ILI9488 || DSP_MODEL==DSP_ILI9486 || DSP_MODEL==DSP_NV3041A || DSP_MODEL==DSP_ST7796
  pages[PG_SCREENSAVER]->addWidget(_date);
  #endif
  pages[PG_PLAYER]->addPage(_footer);

  if(_metabackground) pages[PG_DIALOG]->addWidget(_metabackground);
  if(_metaline) pages[PG_DIALOG]->addWidget(_metaline);
  pages[PG_DIALOG]->addWidget(_meta);
  pages[PG_DIALOG]->addWidget(_nums);
  
  #if !defined(DSP_LCD) && DSP_MODEL!=DSP_NOKIA5110
    pages[PG_DIALOG]->addPage(_footer);
  #endif
  #if !defined(DSP_LCD)
  if(_plbackground) {
    pages[PG_PLAYLIST]->addWidget( _plbackground);
    _plbackground->setHeight(_plwidget->itemHeight());
    _plbackground->moveTo({0,(uint16_t)(_plwidget->currentTop()-playlistConf.widget.textsize*2), (int16_t)playlBGConf.width});
  }
  #endif
  pages[PG_PLAYLIST]->addWidget(_plcurrent);
  pages[PG_PLAYLIST]->addWidget(_plwidget);
  for(const auto& p: pages) _pager->addPage(p);
}

void Display::_apScreen() {
  if(_boot) _pager->removePage(_boot);
  #ifndef DSP_LCD
    _boot = new Page();
    #if DSP_MODEL!=DSP_NOKIA5110
      #if DSP_INVERT_TITLE || defined(DSP_OLED)
      _boot->addWidget(new FillWidget(metaBGConf, config.theme.metafill));
      #else
      _boot->addWidget(new FillWidget(metaBGConfInv, config.theme.metafill));
      #endif
    #endif

    ScrollWidget *bootTitle = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*", apTitleConf, config.theme.meta, config.theme.metafill));
    bootTitle->setText("yoRadio AP Mode");
#if DSP_MODEL == DSP_ST7789_76    
    { char _buf[50]; snprintf(_buf, sizeof(_buf), "%s : %s", LANG::apNameTxt, apSsid);
      TextWidget *apname = (TextWidget*) &_boot->addWidget(new TextWidget(apNameConf, 50, false, config.theme.clock, config.theme.background));
      apname->setText(_buf); }
    { char _buf[50]; snprintf(_buf, sizeof(_buf), "%s : %s", LANG::apPassTxt, apPassword);
      TextWidget *appass = (TextWidget*) &_boot->addWidget(new TextWidget(apPassConf, 50, false, config.theme.clock, config.theme.background));
      appass->setText(_buf); }
#else
    TextWidget *apname = (TextWidget*) &_boot->addWidget(new TextWidget(apNameConf, 30, false, config.theme.title1, config.theme.background));
    apname->setText(LANG::apNameTxt);
    TextWidget *apname2 = (TextWidget*) &_boot->addWidget(new TextWidget(apName2Conf, 30, false, config.theme.clock, config.theme.background));
    apname2->setText(apSsid);
    TextWidget *appass = (TextWidget*) &_boot->addWidget(new TextWidget(apPassConf, 30, false, config.theme.title1, config.theme.background));
    appass->setText(LANG::apPassTxt);
    TextWidget *appass2 = (TextWidget*) &_boot->addWidget(new TextWidget(apPass2Conf, 30, false, config.theme.clock, config.theme.background));
    appass2->setText(apPassword);
#endif
    ScrollWidget *bootSett = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*", apSettConf, config.theme.title2, config.theme.background));
    bootSett->setText(config.ipToStr(WiFi.softAPIP()), LANG::apSettFmt);
    _pager->addPage(_boot);
    _pager->setPage(_boot);
  #else
    dsp.apScreen();
  #endif
}

void Display::_start() {
  if(_boot) _pager->removePage(_boot);
  #ifdef USE_NEXTION
    nextion.wake();
  #endif
  // Only switch to the AP-mode screen when we are actually running SoftAP.
  // During boot/transitions the status can briefly be non-CONNECTED; we still
  // want to build the normal player UI so the header/station number is visible.
  if (network.status == SOFT_AP) {
    _apScreen();
    #ifdef USE_NEXTION
      nextion.apScreen();
    #endif
    _bootStep = 2;
    return;
  }
  #ifdef USE_NEXTION
    //nextion.putcmd("page player");
    nextion.start();
  #endif
  _buildPager();
  _mode = PLAYER;
  config.setTitle(LANG::const_PlReady);
  // NOTE: JPEG decoding inside DspTask can trip the stack canary (bootloop).
  // We'll reintroduce a JPEG demo later using a dedicated worker task with a larger stack.
  
  if(_heapbar)  _heapbar->lock(!config.store.audioinfo);
  
  if(_weather)  _weather->lock(!config.store.showweather);
  if(_weather && config.store.showweather)  _weather->setText(LANG::const_getWeather);
  if(_weatherIcon) {
    if (config.store.showweather) _weatherIcon->unlock();
    else                          _weatherIcon->lock();
  }

  if(_vuwidget) _vuwidget->lock();
  if(_rssi || _rssiIcon) _setRSSI(WiFi.RSSI());
  #ifndef HIDE_BAT
    if (_battxt) {
      const float pct = battery_is_ready() ? battery_get_percent() : 0.0f;
      #if defined(FOOTER_BATTERY_COLORIZE) && (FOOTER_BATTERY_COLORIZE != 0)
        _battxt->setFgColor(batteryPctColor565(pct));
      #else
        _battxt->setFgColor(config.theme.rssi);
      #endif
      _battxt->setText((int)pct, battxtFmt);
    }
  #endif
  /*#ifndef HIDE_IP
    if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
  #endif*/
  #ifndef HIDE_IP
    #if DSP_MODEL != DSP_ST7789_76
      if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
    #endif
  #endif
  // Force a full redraw when entering the player page at boot,
  // otherwise some widgets (top meta bar) can remain blank until the first playback event.
  // Also hard-clear the full screen to the theme background; otherwise remnants of the boot
  // page (often black) can remain visible until the first full page transition.
  dsp.fillScreen(config.theme.background);
  _pager->setPage(pages[PG_PLAYER], true);
  _volume();
  _station();
//  _refreshWeatherUI();
  _time(false);
  if (_speechWidget) _speechWidget->setStat(config.store.ttsEnabled  != 0);
  if (_blfadeWidget) _blfadeWidget->setStat(config.store.blDimEnable != 0);
  if (_lstripWidget) _lstripWidget->setStat(config.store.lsEnabled   != 0);
  _bootStep = 2;
  pm.on_display_player();
}

void Display::_showDialog(const char *title){
  dsp.setScrollId(NULL);
  _pager->setPage( pages[PG_DIALOG]);
  #ifdef META_MOVE
    _meta->moveTo(metaMove);
  #endif
  _meta->setAlign(WA_CENTER);
  _meta->setText(title);
}

void Display::_swichMode(displayMode_e newmode) {
  #ifdef USE_NEXTION
    //nextion.swichMode(newmode);
    nextion.putRequest({NEWMODE, newmode});
  #endif
if (newmode == _mode || 
    (network.status != CONNECTED && network.status != SDREADY)) return;

  _mode = newmode;
  dsp.setScrollId(NULL);
  if (newmode == PLAYER) {
    if (config.isScreensaver) {
        if (_clock) _clock->moveTo(getclockMove());
        if (_date)  _date->moveTo(getdateMove());
    }
   if (player.isRunning()) {
     auto cm = getclockMove();
     if (cm.width < 0) _clock->moveBack();
     else              _clock->moveTo(cm);
   } else {
    _clock->moveBack();
  }
    #ifdef DSP_LCD
      dsp.clearDsp();
    #endif
    numOfNextStation = 0;
    #ifdef META_MOVE
      _meta->moveBack();
    #endif
    _meta->setAlign(metaConf.widget.align);
    _meta->setText(config.station.name);
    #if STATION_WIDGETS
    if (_stationNum) _stationNum->setNum(config.lastStation());
    if (_playMode) {
      uint8_t _pm = config.getMode();
      #ifdef USE_DLNA
      if (_pm == PM_WEB && config.store.playlistSource == PL_SRC_DLNA) _pm = 2;
      #endif
      _playMode->setMode(_pm);
    }
    #endif
    _nums->setText("");
    config.isScreensaver = false;
    _pager->setPage( pages[PG_PLAYER]);
    if (_speechWidget) _speechWidget->setStat(config.store.ttsEnabled  != 0);
    if (_blfadeWidget) _blfadeWidget->setStat(config.store.blDimEnable != 0);
    if (_lstripWidget) _lstripWidget->setStat(config.store.lsEnabled   != 0);
    config.setDspOn(config.store.dspon, false);
    pm.on_display_player();
    _updateStationLogo();
//    _kickWeatherRefresh();
  }
  if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
    config.isScreensaver = true;
    _pager->setPage( pages[PG_SCREENSAVER]);
    _layoutChange(player.isRunning());
    if (_clock) {
      WidgetConfig ccfg = getclockConf();
      _clock->moveTo({ccfg.left, ccfg.top, 0});
      _clock->draw(true);
    }
    if (_date) {
        auto dc = getDateConf();
        _date->moveTo({ dc.widget.left, dc.widget.top, (int16_t)dc.width });
    }
    if (newmode == SCREENBLANK) {
      //dsp.clearClock();
      _clock->clear();
      config.setDspOn(false, false);
    }
  }else{
    config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    config.isScreensaver = false;
  }
  
  if (newmode == VOL) {
  #if DSP_MODEL == DSP_ST7789_76
      _showDialog(config.ipToStr(WiFi.localIP()));
  #else
    #ifndef HIDE_IP
      _showDialog(LANG::const_DlgVolume);
    #else
      _showDialog(config.ipToStr(WiFi.localIP()));
    #endif
  #endif
      _nums->setText(config.store.volume, numtxtFmt);
  }
  if (newmode == LOST)      _showDialog(LANG::const_DlgLost);
  if (newmode == UPDATING)  _showDialog(LANG::const_DlgUpdate);
  if (newmode == SLEEPING)  _showDialog("SLEEPING");
  if (newmode == SDCHANGE)  _showDialog(LANG::const_waitForSD);
  if (newmode == INFO || newmode == SETTINGS || newmode == TIMEZONE || newmode == WIFI) _showDialog(LANG::const_DlgNextion);
  if (newmode == NUMBERS) _showDialog("");
  if (newmode == STATIONS) {
    _pager->setPage( pages[PG_PLAYLIST]);
    _plcurrent->setText("");
    currentPlItem = config.lastStation();
    _drawPlaylist();
  }
  
}

void Display::resetQueue(){
  if(displayQueue!=NULL) xQueueReset(displayQueue);
}

void Display::_drawPlaylist() {
  _plwidget->drawPlaylist(currentPlItem);

  uint8_t stations_list_return_time = config.store.stationsListReturnTime;

  if (stations_list_return_time < 2) stations_list_return_time = 2;
  if (stations_list_return_time > 30) stations_list_return_time = 30;

  timekeeper.waitAndReturnPlayer(stations_list_return_time);
}

void Display::_drawNextStationNum(uint16_t num) {
  timekeeper.waitAndReturnPlayer(30);
  _meta->setText(config.stationByNum(num));
  _nums->setText(num, "%d");
}

void Display::putRequest(displayRequestType_e type, int payload){
  if(displayQueue==NULL) return;
  requestParams_t request;
  request.type = type;
  request.payload = payload;
  xQueueSend(displayQueue, &request, DSQ_SEND_DELAY);
  #ifdef USE_NEXTION
    nextion.putRequest(request);
  #endif
}

void Display::_layoutChange(bool played){
  if (config.store.vumeter) {
    if (played) {
      if (_vuwidget) _vuwidget->unlock();
      auto cm = getclockMove();
      if (cm.width < 0) _clock->moveBack();
      else              _clock->moveTo(cm);
      if (_weather) _weather->moveTo(weatherMoveVU);
      if (_date) {
        _date->setText("");
        auto dm = getdateMove();
        if (dm.width < 0) _date->moveBack();
        else              _date->moveTo(dm);
        _date->update();
      }

    } else {
      // Keep VU visible even when not playing (it will naturally settle to idle).
      if (_vuwidget) _vuwidget->unlock();
      _clock->moveBack();
      if (_weather) _weather->moveBack();
      if (_date) {
        _date->setText("");
        _date->moveBack();
        _date->update();
      }
    }

  } else {
    if (_vuwidget && !_vuwidget->locked()) _vuwidget->lock();
    if (played) {
      if (_weather) _weather->moveTo(weatherMove);
      _clock->moveBack();

      if (_date) {
        _date->setText("");
        _date->moveBack(); 
        _date->update();
      }
    } else {
      if (_weather) _weather->moveBack();
      _clock->moveBack();

      if (_date) {
        _date->setText("");
        _date->moveBack();
        _date->update();
      }
    }
  }

  // VU/weather layout changes can affect where there's free space for the logo.
  _layoutStationLogo();
}


void Display::loop() {
  if(_bootStep==0) {
    _pager->begin();
    _bootScreen();
    return;
  }
  if(displayQueue==NULL || _locked) return;

  _pager->loop();
#ifdef USE_NEXTION
  nextion.loop();
#endif
  requestParams_t request;
  if(xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)){
    bool pm_result = true;
    pm.on_display_queue(request, pm_result);
    if(pm_result)
      switch (request.type){
        case NEWMODE: _swichMode((displayMode_e)request.payload); break;
        case CLOSEPLAYLIST: player.sendCommand({PR_PLAY, request.payload}); break;
        case CLOCK: 
          if(_mode==PLAYER || _mode==SCREENSAVER) _time(request.payload==1);
          /*#ifdef USE_NEXTION
            if(_mode==TIMEZONE) nextion.localTime(network.timeinfo);
            if(_mode==INFO)     nextion.rssi();
          #endif*/
          break;
        case NEWTITLE: _title(); break;
        case NEWSTATION: _station(); break;
        case NEXTSTATION: _drawNextStationNum(request.payload); break;
        case DRAWPLAYLIST: _drawPlaylist(); break;
        case DRAWVOL: _volume(); break;
        case DBITRATE: {
          if(_mode==PLAYER){   // only update the bitrate widget on the player screen
            char buf[20]; 
            snprintf(buf, 20, bitrateFmt, config.station.bitrate); 
            if(_bitrate) { _bitrate->setText(config.station.bitrate==0?"":buf); } 
            if(_fullbitrate) { 
              _fullbitrate->setBitrate(config.station.bitrate); 
              _fullbitrate->setFormat(config.configFmt); 
            } 
          }
        }
        break;
        case AUDIOINFO: if(_heapbar)  { _heapbar->lock(!config.store.audioinfo); _heapbar->setValue(player.inBufferFilled()); } break;
        case SHOWVUMETER: {
          if (_vuwidget) {
            if (config.store.vumeter) _vuwidget->unlock();
            else                       _vuwidget->lock(); 

            _layoutChange(player.isRunning()); 
          }
          break;
        }
        case SHOWWEATHER: {
          if(_weather) _weather->lock(!config.store.showweather);
          if (_weatherIcon) {
            if (config.store.showweather) _weatherIcon->unlock();
            else                          _weatherIcon->lock();
          }
          if(!config.store.showweather){
            /*#ifndef HIDE_IP
            if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
            #endif*/
            #ifndef HIDE_IP
              #if DSP_MODEL != DSP_ST7789_76
                if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
              #endif
            #endif
          }else{
            if(_weather) _weather->setText(LANG::const_getWeather);
//            _kickWeatherRefresh();
          }
          _updateStationLogo();
          break;
        }
        case NEWWEATHER: {
          if(_weather && timekeeper.weatherBuf) _weather->setText(timekeeper.weatherBuf);
          //strcpy(timekeeper.weatherIcon, "50d"); // teszt 
          if(_weatherIcon && config.store.showweather && !_weatherIcon->locked()) {
             _weatherIcon->setIcon(timekeeper.weatherIcon);
             _weatherIcon->setTemp(timekeeper.tempC);
          }
          break;
        }
        case BOOTSTRING: {
          if(_bootstring) _bootstring->setText(config.ssids[request.payload].ssid, LANG::bootstrFmt);
          /*#ifdef USE_NEXTION
            char buf[50];
            snprintf(buf, 50, bootstrFmt, config.ssids[request.payload].ssid);
            nextion.bootString(buf);
          #endif*/
          break;
        }
        case WAITFORSD: {
          if(_bootstring) _bootstring->setText(LANG::const_waitForSD);
          break;
        }
        case SDFILEINDEX: {
          if(_mode == SDCHANGE) _nums->setText(request.payload, "%d");
          break;
        }
        case DSPRSSI:
          if (_rssi || _rssiIcon) _setRSSI(request.payload);
          if (_heapbar && config.store.audioinfo) _heapbar->setValue(player.isRunning()?player.inBufferFilled():0);
          break;
        case PSTART:
          if (config.getMode() == PM_SDCARD) _sdPstartAt = millis();
          _layoutChange(true);
          break;
        case PSTOP:
          if (config.getMode() == PM_SDCARD) _sdPstartAt = 0;
          _layoutChange(false);
          break;
        case DSP_START: _start();  break;
        case NEWIP: {
          /*#ifndef HIDE_IP
            if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
          #endif*/
          #ifndef HIDE_IP
            #if DSP_MODEL != DSP_ST7789_76
              if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
            #endif
          #endif
          break;
        }
        case DSP_RECONF: {
          _beginRebuild();
          config.loadTheme();
          dsp.clearDsp();
          _oldPager = _pager;
          for (int i=0; i<4; i++) _oldPages[i] = pages[i];
          _rebuildUI();
            if (_vuwidget && !config.store.vumeter) {
              _vuwidget->lock(); 
            }
         // FORCE redraw
          _station();
          _title();
          _volume();
          _time(false);
          _layoutChange(player.isRunning());
           if (_fullbitrate) { 
            _fullbitrate->setBitrate(config.station.bitrate);
            _fullbitrate->setFormat(config.configFmt);
           }
          if (_bitrate) {
              char buf[20];
              snprintf(buf, sizeof(buf), bitrateFmt, config.station.bitrate);
              _bitrate->setText(config.station.bitrate==0 ? "" : buf);
          }
          if (_volip) {
             _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
          }
          if (_weather && config.store.showweather) {
             _weather->setText(LANG::const_getWeather);
          }
          if (_rssi || _rssiIcon) _setRSSI(WiFi.RSSI());
          if (_speechWidget) _speechWidget->setStat(config.store.ttsEnabled  != 0);
          if (_blfadeWidget) _blfadeWidget->setStat(config.store.blDimEnable != 0);
          if (_lstripWidget) _lstripWidget->setStat(config.store.lsEnabled   != 0);
          _endRebuild();
          _kickWeatherRefresh();
          break;
        }

        case STATUS_SPEECH: if(_speechWidget) _speechWidget->setStat(request.payload != 0); break;
        case STATUS_BLFADE: if(_blfadeWidget) _blfadeWidget->setStat(request.payload != 0); break;
        case STATUS_LSTRIP: if(_lstripWidget) _lstripWidget->setStat(request.payload != 0); break;
        case NEWBATTERY:
            if (_battxt) {
                const float pct = battery_is_ready() ? battery_get_percent() : 0.0f;
                #if defined(FOOTER_BATTERY_COLORIZE) && (FOOTER_BATTERY_COLORIZE != 0)
                  _battxt->setFgColor(batteryPctColor565(pct));
                #else
                  _battxt->setFgColor(config.theme.rssi);
                #endif
                _battxt->setText((int)pct, battxtFmt);
            }
            if (_batChgIcon) {
                const uint8_t* bbmp = battery_usb_present() ? ICON_BOLT_9x12 : nullptr;
                _batChgIcon->setBitmap(bbmp, ICON_BOLT_W, ICON_BOLT_H);
            }
            if (_batIcon) {
                const float pct = battery_is_ready() ? battery_get_percent() : 0.0f;
                const auto bmpForPct = [](float p) -> const uint8_t* {
                  if (p >= 99.5f) return ICON_BAT_FULL_12;
                  if (p >= 91.0f) return ICON_BAT_90_12;
                  if (p >= 80.0f) return ICON_BAT_80_12;
                  if (p >= 60.0f) return ICON_BAT_60_12;
                  if (p >= 40.0f) return ICON_BAT_40_12;
                  if (p >= 20.0f) return ICON_BAT_20_12;
                  if (p >= 10.0f) return ICON_BAT_10_12;
                  return ICON_BAT_EMPTY_12;
                };
                const uint8_t* bmp = bmpForPct(pct);
                uint16_t fg = config.theme.rssi;
                #if defined(FOOTER_BATTERY_COLORIZE) && (FOOTER_BATTERY_COLORIZE != 0)
                  fg = batteryPctColor565(pct);
                #endif
                _batIcon->setBitmapAndColor(bmp, ICON_BAT_W, ICON_BAT_H, fg);
            }
            // Redraw the volume bar last so it always stays visible.
            if (_volbar) {
              int vol = config.store.volume;
              if (vol > 100) vol = 100;
              if (vol < 0) vol = 0;
              _volbar->setValue(vol);
            }
            break;
        default: break;

        if (uxQueueMessagesWaiting(displayQueue))
          return;
      }
  }

  dsp.loop();
/*
  #if I2S_DOUT==255
  player.computeVUlevel();
  #endif
*/
}

void Display::_setRSSI(int rssi) {
  if(!_rssi && !_rssiIcon) return;
#if RSSI_DIGIT
  if (_rssi) _rssi->setText(rssi, rssiFmt);
  return;
#endif
  // If we have a dedicated RSSI icon widget, show bars there and keep digits on the main RSSI widget.
  if (_rssiIcon) {
    char rssiG[3];
    int rssi_steps[] = {RSSI_STEPS};
    if(rssi >= rssi_steps[0]) strlcpy(rssiG, "\004\006", 3);
    if(rssi >= rssi_steps[1] && rssi < rssi_steps[0]) strlcpy(rssiG, "\004\005", 3);
    if(rssi >= rssi_steps[2] && rssi < rssi_steps[1]) strlcpy(rssiG, "\004\002", 3);
    if(rssi >= rssi_steps[3] && rssi < rssi_steps[2]) strlcpy(rssiG, "\003\002", 3);
    if(rssi <  rssi_steps[3] || rssi >=  0) strlcpy(rssiG, "\001\002", 3);
    _rssiIcon->setText(rssiG);
    return;
  }

  // If the RSSI widget uses a GFXfont (DejaVu), the legacy icon-bar codes
  // (\001..\006) will not render correctly. Fall back to digits.
  if (_rssi && _rssi->hasGfxFont()) {
    _rssi->setText(rssi, rssiFmt);
    return;
  }
  char rssiG[3];
  int rssi_steps[] = {RSSI_STEPS};
  if(rssi >= rssi_steps[0]) strlcpy(rssiG, "\004\006", 3);
  if(rssi >= rssi_steps[1] && rssi < rssi_steps[0]) strlcpy(rssiG, "\004\005", 3);
  if(rssi >= rssi_steps[2] && rssi < rssi_steps[1]) strlcpy(rssiG, "\004\002", 3);
  if(rssi >= rssi_steps[3] && rssi < rssi_steps[2]) strlcpy(rssiG, "\003\002", 3);
  if(rssi <  rssi_steps[3] || rssi >=  0) strlcpy(rssiG, "\001\002", 3);
  if (_rssi) _rssi->setText(rssiG);
}

void Display::_station() {
  _meta->setAlign(metaConf.widget.align);
  _meta->setText(config.station.name);
  // Refresh station number and play-mode widgets
  #if STATION_WIDGETS
  if (_stationNum) _stationNum->setNum(config.lastStation());
  if (_playMode) {
      uint8_t _pm = config.getMode();
      #ifdef USE_DLNA
      if (_pm == PM_WEB && config.store.playlistSource == PL_SRC_DLNA) _pm = 2;
      #endif
      _playMode->setMode(_pm);
    }
  #endif
/*#ifdef USE_NEXTION
  nextion.newNameset(config.station.name);
  nextion.bitrate(config.station.bitrate);
  nextion.bitratePic(ICON_NA);
#endif*/

  if (config.getMode() == PM_SDCARD) {
    _sdStationChangedAt = millis();
  }

  _updateStationLogo();
}

namespace {
struct RectI16 {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

static inline bool isNameChar(char c) {
  return isalnum((unsigned char)c) != 0;
}

static bool normEqualsNoCase(const char* a, const char* b) {
  if (!a || !b) return false;
  // Compare after lowercasing and skipping non-alnum characters.
  const unsigned char* pa = (const unsigned char*)a;
  const unsigned char* pb = (const unsigned char*)b;
  while (true) {
    while (*pa && !isNameChar((char)*pa)) pa++;
    while (*pb && !isNameChar((char)*pb)) pb++;
    if (!*pa || !*pb) break;
    char ca = (char)tolower(*pa);
    char cb = (char)tolower(*pb);
    if (ca != cb) return false;
    pa++; pb++;
  }
  while (*pa && !isNameChar((char)*pa)) pa++;
  while (*pb && !isNameChar((char)*pb)) pb++;
  return (*pa == '\0' && *pb == '\0');
}

static bool containsNoCase(const char* haystack, const char* needle) {
  if (!haystack || !needle || !*needle) return false;
  const size_t nlen = strlen(needle);
  if (nlen == 0) return false;

  for (const char* p = haystack; *p; p++) {
    size_t i = 0;
    while (needle[i] &&
           p[i] &&
           (char)tolower((unsigned char)p[i]) == (char)tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == nlen) return true;
  }
  return false;
}

static void spiffsKeyFromStationName(const char* stationName, char* outKey, size_t outKeyLen) {
  if (!outKey || outKeyLen == 0) return;
  outKey[0] = '\0';
  if (!stationName || !stationName[0]) return;

  // Normalize to a short ASCII key then hash it to keep SPIFFS filenames short.
  // Must match tools/pio/gen_station_logos_from_images.py.
  char norm[96];
  size_t j = 0;
  bool lastUnderscore = false;
  for (const unsigned char* p = (const unsigned char*)stationName; *p && j + 1 < sizeof(norm); p++) {
    const unsigned char c = *p;
    if (isalnum(c)) {
      norm[j++] = (char)tolower(c);
      lastUnderscore = false;
    } else {
      if (!lastUnderscore && j > 0 && j + 1 < sizeof(norm)) {
        norm[j++] = '_';
        lastUnderscore = true;
      }
    }
  }
  while (j > 0 && norm[j - 1] == '_') j--;
  norm[j] = '\0';
  if (j == 0) {
    strlcpy(norm, "logo", sizeof(norm));
  }

  // FNV-1a 32-bit hash.
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; norm[i]; i++) {
    h ^= (uint8_t)norm[i];
    h *= 0x01000193u;
  }

  // 8 hex chars.
  snprintf(outKey, outKeyLen, "%08lx", (unsigned long)h);
}

static bool decodeRgb565RleFileToBuf(File& f, uint32_t wordCount, uint16_t w, uint16_t h, uint16_t* outPixels) {
  if (!outPixels || w == 0 || h == 0 || (wordCount & 1u) != 0u) return false;
  const size_t outCount = (size_t)w * (size_t)h;
  size_t outI = 0;

  for (uint32_t wi = 0; wi + 1u < wordCount; wi += 2u) {
    uint16_t count = 0;
    uint16_t color = 0;
    if (f.read((uint8_t*)&count, 2) != 2) return false;
    if (f.read((uint8_t*)&color, 2) != 2) return false;
    if (count == 0) break;
    if (outI + (size_t)count > outCount) return false;
    for (uint16_t k = 0; k < count; k++) {
      outPixels[outI++] = color;
    }
  }
  return outI == outCount;
}

static uint16_t* loadStationLogoFromSpiffs(const char* stationName, uint16_t* outW, uint16_t* outH) {
  if (outW) *outW = 0;
  if (outH) *outH = 0;
  if (!stationName || !stationName[0]) return nullptr;

  char path[128];
  // Special-case default logo.
  if (strcmp(stationName, "_DEFAULT_") == 0 || strcmp(stationName, "default") == 0) {
    strlcpy(path, "/logos/default.ylg", sizeof(path));
  } else {
    char key[96];
    spiffsKeyFromStationName(stationName, key, sizeof(key));
    if (!key[0]) return nullptr;
    snprintf(path, sizeof(path), "/logos/%s.ylg", key);
  }
  if (!SPIFFS.exists(path)) return nullptr;

  File f = SPIFFS.open(path, "r");
  if (!f) return nullptr;

  struct Header {
    char magic[4];
    uint8_t version;
    uint8_t format;
    uint16_t w;
    uint16_t h;
    uint32_t wordCount;
  } hdr;

  if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) return nullptr;
  if (memcmp(hdr.magic, "YLG0", 4) != 0) return nullptr;
  if (hdr.version != 1) return nullptr;
  if (hdr.w == 0 || hdr.h == 0) return nullptr;

  const size_t pixelCount = (size_t)hdr.w * (size_t)hdr.h;
  const size_t pixelBytes = pixelCount * sizeof(uint16_t);
  uint16_t* buf = (uint16_t*)malloc(pixelBytes);
  if (!buf) return nullptr;

  bool ok = false;
  if (hdr.format == 0) {
    const uint32_t expectedWords = (uint32_t)pixelCount;
    if (hdr.wordCount == expectedWords) {
      ok = (f.read((uint8_t*)buf, pixelBytes) == (int)pixelBytes);
    }
  } else if (hdr.format == 1) {
    ok = decodeRgb565RleFileToBuf(f, hdr.wordCount, hdr.w, hdr.h, buf);
  }

  if (!ok) {
    free(buf);
    return nullptr;
  }

  if (outW) *outW = hdr.w;
  if (outH) *outH = hdr.h;
  return buf;
}

static inline bool rectIntersects(const RectI16& a, const RectI16& b) {
  if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return false;
  return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

static inline bool rectInside(const RectI16& r, int16_t w, int16_t h) {
  return r.x >= 0 && r.y >= 0 && (r.x + r.w) <= w && (r.y + r.h) <= h;
}
} // namespace

void Display::_layoutStationLogo() {
  if (!_stationLogo) return;
  const int16_t logoW = (int16_t)_stationLogoW;
  const int16_t logoH = (int16_t)_stationLogoH;
  if (logoW <= 0 || logoH <= 0) return;

  const int16_t sw = (int16_t)dsp.width();
  const int16_t sh = (int16_t)dsp.height();

  // Avoid drawing into the footer/volume bar.
  const int16_t footerTop = (int16_t)volbarConf.widget.top;
  const int16_t maxY = footerTop - logoH - 2;

  // Clock positioning (used to avoid overlapping the seconds on the default layout).
  const auto cc = getclockConf();
  const bool clockRightAligned = (cc.align == WA_RIGHT);
  const int16_t clockTop = (int16_t)cc.top;

  RectI16 reserved[6];
  int reservedN = 0;

  // Reserve weather icon area (and its temperature text).
  if (_weatherIcon && config.store.showweather) {
    const auto wc = getWeatherIconConf();
    const bool verticalTemp = (config.store.vuLayout == 0);
    const int16_t tempH = verticalTemp ? 14 : 0;
    const int16_t extraW = verticalTemp ? 0 : 44; // temp printed to the right
    reserved[reservedN++] = { (int16_t)wc.left, (int16_t)(wc.top - tempH), (int16_t)(64 + extraW), (int16_t)(64 + tempH) };
  }

  // Reserve bitrate widget area (prevents logo clears from corrupting it on station changes).
  if (_fullbitrate) {
    const auto bc = getfullbitrateConf();
    reserved[reservedN++] = { (int16_t)bc.widget.left, (int16_t)bc.widget.top,
                              (int16_t)(bc.dimension * 2), (int16_t)(bc.dimension / 2) };
  }

  // Reserve VU area when enabled, even if audio hasn't started yet.
  // Otherwise the logo can be placed "under" the VU widget and appear invisible.
  if (_vuwidget && config.store.vumeter) {
    const auto vc = getvuConf();
    const auto bands = getbandsConf();
    const bool alignTruthy = (vc.align != WA_LEFT);
    const int16_t maxDim = (int16_t)(alignTruthy ? bands.width : bands.height);
    int16_t vuW = 0;
    int16_t vuH = 0;
    switch (config.store.vuLayout) {
      case 3:
        vuW = maxDim;
        vuH = (int16_t)(bands.height * 2 + bands.space);
        break;
      case 2:
      case 1:
        vuW = (int16_t)(maxDim * 2 + bands.space);
        vuH = (int16_t)bands.height;
        break;
      case 0:
      default:
        vuW = (int16_t)(bands.width * 2 + bands.space);
        vuH = maxDim;
        break;
    }
    reserved[reservedN++] = { (int16_t)vc.left, (int16_t)vc.top, vuW, vuH };
  }

  // Prefer the weather-icon slot when weather is disabled.
  // VU layouts already move the weather icon; we follow that positioning.
  if (_weatherIcon && !config.store.showweather) {
    const auto wc = getWeatherIconConf();
    int16_t px = (int16_t)wc.left;
    int16_t py = (int16_t)wc.top;
    // Clamp so we never run off-screen.
    if (px + logoW > sw - 1) px = (int16_t)(sw - TFT_FRAMEWDT - logoW);
    if (px < 0) px = 0;

    RectI16 pref = { px, py, logoW, logoH };

    // Non-default VU layouts: nudge the slot slightly upward for better alignment.
    if (config.store.vuLayout != 0) {
      pref.y = (pref.y >= 4) ? (int16_t)(pref.y - 4) : 0;
    }

    // Default VU layout: only force "above clock" when the logo is wider than the
    // right margin reserved for the clock (prevents overlap with seconds).
    if (config.store.vuLayout == 0 && clockRightAligned && pref.w > (int16_t)cc.left) {
      const int16_t yAboveClock = (int16_t)(clockTop - pref.h - 2);
      if (yAboveClock >= 0 && pref.y > yAboveClock) pref.y = yAboveClock;
    }

    if (pref.y <= maxY && rectInside(pref, sw, sh)) {
      bool ok = true;
      for (int i = 0; i < reservedN; i++) {
        if (rectIntersects(pref, reserved[i])) { ok = false; break; }
      }
      if (ok) {
        _stationLogo->moveTo({ (uint16_t)pref.x, (uint16_t)pref.y, logoW });
        return;
      }
    }
  }

  // Candidate slots (try to keep it in the "content" area).
  RectI16 candidates[6];
  int candN = 0;

  // Extra "above clock" slot for default layout (keeps seconds safe).
  // Only needed when the logo is wider than the clock's right margin.
  if (config.store.vuLayout == 0 && clockRightAligned && logoW > (int16_t)cc.left) {
    int16_t yAboveClock = (int16_t)(clockTop - logoH - 2);
    if (yAboveClock < 0) yAboveClock = 0;
    candidates[candN++] = { (int16_t)(sw - TFT_FRAMEWDT - logoW), yAboveClock, logoW, logoH };
  }

  candidates[candN++] = { (int16_t)(sw - TFT_FRAMEWDT - logoW), 100, logoW, logoH };           // top-right
  candidates[candN++] = { (int16_t)TFT_FRAMEWDT,               100, logoW, logoH };            // top-left
  candidates[candN++] = { (int16_t)((sw - logoW) / 2),         96,  logoW, logoH };            // top-center
  candidates[candN++] = { (int16_t)(sw - TFT_FRAMEWDT - logoW), 120, logoW, logoH };           // right, a bit lower

  RectI16 chosen = candidates[0];
  bool found = false;

  for (int ci = 0; ci < candN; ci++) {
    auto& c = candidates[ci];
    if (c.y > maxY) continue;
    if (!rectInside(c, sw, sh)) continue;
    bool ok = true;
    for (int i = 0; i < reservedN; i++) {
      if (rectIntersects(c, reserved[i])) { ok = false; break; }
    }
    if (ok) { chosen = c; found = true; break; }
  }

  if (!found) {
    // Last resort: clamp within the allowed area.
    chosen.x = (chosen.x < 0) ? 0 : chosen.x;
    chosen.y = (chosen.y < 0) ? 0 : chosen.y;
    if (chosen.y > maxY) chosen.y = maxY;
  }

  // Avoid unnecessary clears/redraws (prevents flicker).
  if (_stationLogoX == chosen.x && _stationLogoY == chosen.y && _stationLogoMoveW == logoW) {
    return;
  }
  _stationLogo->moveTo({ (uint16_t)chosen.x, (uint16_t)chosen.y, logoW });
  _stationLogoX = chosen.x;
  _stationLogoY = chosen.y;
  _stationLogoMoveW = logoW;
}

void Display::_updateStationLogo() {
  if (!_stationLogo) return;

  // Global toggle: hide both station logos and SD album art.
  if (!config.getShowlogos()) {
    if (_stationLogo->locked()) return;
    if (_stationLogoScaled) { free(_stationLogoScaled); _stationLogoScaled = nullptr; }
    _stationLogoLastKey[0] = '\0';
    _stationLogoUsedDefault = false;
    _stationLogo->unlock();
    _stationLogo->setImage(nullptr, 0, 0);
    _stationLogo->lock(true);
    _stationLogoW = 0;
    _stationLogoH = 0;
    _stationLogoX = -1;
    _stationLogoY = -1;
    _stationLogoMoveW = -1;
    _stationLogoLastPixels = nullptr;
    return;
  }

  if (config.getMode() == PM_SDCARD) {
    // SD album art removed (JPEG decoding was unstable on this target).
    // Keep the image widget hidden in SD mode for now.
    if (_stationLogo->locked()) return;
    if (_stationLogoScaled) { free(_stationLogoScaled); _stationLogoScaled = nullptr; }
    _stationLogoLastKey[0] = '\0';
    _stationLogoUsedDefault = false;
    _stationLogo->unlock();
    _stationLogo->setImage(nullptr, 0, 0);
    _stationLogo->lock(true);
    _stationLogoW = 0;
    _stationLogoH = 0;
    _stationLogoX = -1;
    _stationLogoY = -1;
    _stationLogoMoveW = -1;
    _stationLogoLastPixels = nullptr;
    return;
  }

  const bool isWeb = (config.getMode() == PM_WEB);
  // IMPORTANT: use the playlist station name for the logo key.
  // `config.station.name` may be updated from ICY metadata after playback starts.
  const char* sname = (config.station.playlistName[0] ? config.station.playlistName : config.station.name);

  auto clearScaled = [&]() {
    if (_stationLogoScaled) {
      free(_stationLogoScaled);
      _stationLogoScaled = nullptr;
    }
  };

  // If the weather icon is enabled, it owns that screen real-estate.
  // Station logos take the weather-icon slot only when weather is disabled.
  if (_weatherIcon && config.store.showweather) {
    clearScaled();
    _stationLogoLastKey[0] = '\0';
    _stationLogoUsedDefault = false;
    _stationLogo->unlock();
    _stationLogo->setImage(nullptr, 0, 0); // clears previous area while unlocked
    _stationLogo->lock(true);
    _stationLogoW = 0;
    _stationLogoH = 0;
    _stationLogoX = -1;
    _stationLogoY = -1;
    _stationLogoMoveW = -1;
    _stationLogoLastPixels = nullptr;
    return;
  }

  if (!isWeb || !sname || !sname[0]) {
    clearScaled();
    _stationLogoLastKey[0] = '\0';
    _stationLogoUsedDefault = false;
    _stationLogo->unlock();
    _stationLogo->setImage(nullptr, 0, 0); // clears previous area while unlocked
    _stationLogo->lock(true);
    _stationLogoW = 0;
    _stationLogoH = 0;
    _stationLogoX = -1;
    _stationLogoY = -1;
    _stationLogoMoveW = -1;
    _stationLogoLastPixels = nullptr;
    return;
  }

  // SPIFFS-only logos:
  // - station-specific: /logos/<hash>.ylg
  // - fallback default: /logos/default.ylg (generated from station_logos/default_logo.png)
  const bool want64 = (config.store.vuLayout == 0);
  const uint16_t wantW = want64 ? 64 : 80;
  const uint16_t wantH = want64 ? 64 : 80;

  char key[16];
  spiffsKeyFromStationName(sname, key, sizeof(key));
  if (!key[0]) strlcpy(key, "00000000", sizeof(key));

  // If we were showing the default due to a missing logo and the logo file
  // has appeared since (uploadfs), refresh without requiring a station change.
  char stationPath[64];
  snprintf(stationPath, sizeof(stationPath), "/logos/%s.ylg", key);
  const bool stationFileNowExists = SPIFFS.exists(stationPath);

  // If we're already showing this station at the right size, do nothing.
  if (_stationLogoScaled &&
      _stationLogoLastKey[0] &&
      strcmp(_stationLogoLastKey, key) == 0 &&
      _stationLogoW == wantW &&
      _stationLogoH == wantH &&
      _stationLogoLastPixels == _stationLogoScaled &&
      !(_stationLogoUsedDefault && stationFileNowExists) &&
      !_stationLogo->locked()) {
    return;
  }

  uint16_t fw = 0;
  uint16_t fh = 0;
  uint16_t* filePixels = loadStationLogoFromSpiffs(sname, &fw, &fh);
  const bool usedDefault = (filePixels == nullptr);
  if (!filePixels) {
    // Default logo from SPIFFS.
    fw = 0;
    fh = 0;
    filePixels = loadStationLogoFromSpiffs("_DEFAULT_", &fw, &fh);
  }

  if (!filePixels) {
    // Nothing to show.
    clearScaled();
    _stationLogoLastKey[0] = '\0';
    _stationLogo->unlock();
    _stationLogo->setImage(nullptr, 0, 0);
    _stationLogo->lock(true);
    _stationLogoW = 0;
    _stationLogoH = 0;
    _stationLogoX = -1;
    _stationLogoY = -1;
    _stationLogoMoveW = -1;
    _stationLogoLastPixels = nullptr;
    return;
  }

  clearScaled();
  _stationLogoScaled = filePixels;
  strlcpy(_stationLogoLastKey, key, sizeof(_stationLogoLastKey));
  _stationLogoUsedDefault = usedDefault;
  const uint16_t* pixels = _stationLogoScaled;
  uint16_t w = fw;
  uint16_t h = fh;

  // Default VU layout: downscale large logos so they can sit back in the
  // "weather slot" without overlapping track text or the clock seconds.
  if (config.store.vuLayout == 0 && w == 80 && h == 80) {
    const uint16_t dw = 64;
    const uint16_t dh = 64;
    // IMPORTANT: `pixels` may already point at `_stationLogoScaled` (e.g. RLE-decoded).
    // Don't free `_stationLogoScaled` until after we've finished reading from `pixels`.
    const uint16_t* srcPixels = pixels;
    uint16_t* tmp = (uint16_t*)malloc((size_t)dw * (size_t)dh * sizeof(uint16_t));
    if (tmp) {
      for (uint16_t y = 0; y < dh; y++) {
        const uint16_t sy = (uint16_t)((uint32_t)y * (uint32_t)h / (uint32_t)dh);
        for (uint16_t x = 0; x < dw; x++) {
          const uint16_t sx = (uint16_t)((uint32_t)x * (uint32_t)w / (uint32_t)dw);
          tmp[(size_t)y * dw + x] = srcPixels[(size_t)sy * w + sx];
        }
      }

      // Replace any previous scaled/decoded buffer with `tmp`.
      if (_stationLogoScaled && srcPixels == _stationLogoScaled) {
        free(_stationLogoScaled);
      } else {
        clearScaled();
      }
      _stationLogoScaled = tmp;
      pixels = _stationLogoScaled;
      w = dw;
      h = dh;
    }
  } else {
    // If the selected pixels are not using our dynamic buffer, free any
    // previous buffer now. (If `pixels` *is* `_stationLogoScaled`, keep it.)
    if (pixels != _stationLogoScaled) {
      clearScaled();
    }
  }

  // If nothing changed (common when station name updates via ICY), do nothing to avoid flicker.
  if (_stationLogoLastPixels == pixels && _stationLogoW == w && _stationLogoH == h && !_stationLogo->locked()) {
    return;
  }

  // IMPORTANT: avoid drawing at the old position then moving (move clears old area and can
  // erase the header bar). Clear image first, move to final position, then draw.
  _stationLogo->lock(true);
  _stationLogoW = w;
  _stationLogoH = h;
  _layoutStationLogo();
  _stationLogo->unlock();
  _stationLogo->setImage(pixels, w, h);
  _stationLogoLastPixels = pixels;
}

char *split(char *str, const char *delim) {
  char *dmp = strstr(str, delim);
  if (dmp == NULL) return NULL;
  *dmp = '\0'; 
  return dmp + strlen(delim);
}

void Display::_title() {
  // If title is empty but the player is running, fall back to station name
  if (strlen(config.station.title) == 0 && player.isRunning()) {
    strlcpy(config.station.title, config.station.name, sizeof(config.station.title));
  }
  
  if (strlen(config.station.title) > 0) {
    // Avoid VLA on the display-task stack (can trip stack canary).
    static char tmpbuf[BUFLEN];
    strlcpy(tmpbuf, config.station.title, sizeof(tmpbuf));
     //Serial.printf("display.cpp -> _title(be) -> tmpbuf %s\r\n", tmpbuf);
    // IMPORTANT: clean invalid UTF-8 before splitting!
    _utf8_clean(tmpbuf);
    // Serial.printf("display.cpp -> _title(ki) -> tmpbuf %s\r\n", tmpbuf);
    char *stitle = split(tmpbuf, " - ");
    if (stitle && _title2) {
      _title1->setText(tmpbuf);
      _title2->setText(stitle);
    } else {
      _title1->setText(config.station.title);
      if (_title2) {
        _title2->setText("");
      }
    }
  } else {
    _title1->setText("");
    if (_title2) {
      _title2->setText("");
    }
  }
  if (player_on_track_change) {
    player_on_track_change();
  }
  pm.on_track_change();
}

void Display::_utf8_clean(char *s)
{
    char *in = s;
    char *out = s;

    while (*in) {
        unsigned char c = (unsigned char)*in;

        // --- Filter zero-width characters ---
        if (c == 0xE2 && (unsigned char)in[1] == 0x80 &&
           ((unsigned char)in[2] == 0x8B || 
            (unsigned char)in[2] == 0x8C || 
            (unsigned char)in[2] == 0x8D)) {
            in += 3;
            continue;
        }

        // Soft hyphen
        if (c == 0xC2 && (unsigned char)in[1] == 0xAD) {
            in += 2;
            continue;
        }

        // --- Keep all other UTF-8 intact ---
        // Just copy byte-by-byte
        *out++ = *in++;
    }

    *out = '\0';
}

void Display::_time(bool redraw) {
  
#if LIGHT_SENSOR!=255
  if(config.store.dspon) {
    config.store.brightness = AUTOBACKLIGHT(analogRead(LIGHT_SENSOR));
    config.setBrightness();
  }
#endif
  static int lastSSMinute = -1;
  if (!config.isScreensaver) {
    lastSSMinute = -1;
  }
  
  if (config.isScreensaver) {
    int m = network.timeinfo.tm_min;

    if (m != lastSSMinute) {
      lastSSMinute = m;

      WidgetConfig ccfg = getclockConf();

      uint16_t contentH = (TIME_SIZE < 19)
        ? (uint16_t)(TIME_SIZE * CHARHEIGHT)
        : (uint16_t)_clock->dateSize();

      uint16_t topMargin    = (TIME_SIZE < 19) ? TFT_FRAMEWDT : (uint16_t)(TFT_FRAMEWDT + TIME_SIZE);
      uint16_t bottomMargin = (TIME_SIZE < 19) ? TFT_FRAMEWDT : (uint16_t)(TFT_FRAMEWDT * 2);
      int16_t ymin = topMargin;
      int16_t ymax = (int16_t)dsp.height() - (int16_t)contentH - (int16_t)bottomMargin;

#ifdef SAVER_Y_MIN
      if (SAVER_Y_MIN < ymax) ymax = SAVER_Y_MIN;
#endif

      if (ymax < ymin) ymax = ymin;
      uint16_t ft = random(ymin, ymax + 1);
      uint16_t cw = _clock->clockWidth();
      uint16_t lt = (uint16_t)random(TFT_FRAMEWDT, dsp.width() - cw - TFT_FRAMEWDT);

      MoveConfig pos;
      pos.y = ft;
      pos.x = (ccfg.align == WA_CENTER) ? (lt - (dsp.width() - cw)/2) : lt;
      pos.width = 0;

      dsp.fillScreen(0x0000);
      _clock->moveTo(pos);

      if (_date) {
        _date->setText("");

        const ScrollConfig& dc = getDateConf();
        const auto         cc = getclockConf();

        MoveConfig dm;
        dm.width = dc.width;

        const int16_t dY = (int16_t)dc.widget.top - (int16_t)cc.top;
        dm.y = (uint16_t)((int16_t)pos.y + dY);

        const int16_t dX = (int16_t)pos.x - (int16_t)cc.left;   // cc.left is the clock "base" X
        dm.x = (uint16_t)((int16_t)dc.widget.left + dX);

        _date->moveTo(dm);
      }
    }
  }

  _clock->draw();
  if (_date) _date->update();
  /*#ifdef USE_NEXTION
    nextion.printClock(network.timeinfo);
  #endif*/
}

/* Trying to Get a % sign to show when changing volume
void Display::_volume() {
  if (_volbar) {
    int vol = config.store.volume;
    if (vol > 100) vol = 100;
    if (vol < 0) vol = 0;
    _volbar->setValue(vol);
  }

#ifndef HIDE_VOL
  if (_voltxt) {
    _voltxt->setText(config.store.volume, voltxtFmt);
  }
#endif

  if (_mode == VOL) {
    timekeeper.waitAndReturnPlayer(2);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", config.store.volume);
    _nums->setText(buf);   // ← literal text, not numeric formatter
  }
}
*/

/* original function */
void Display::_volume() {
  int vol = (config.store.volume);
  if (vol > 100) vol = 100;
  if (vol < 0) vol = 0;

  // Update volume icon first; it clears a 24x24 area, so we'll redraw the bar after.
  #if DSP_MODEL==DSP_ILI9341
    #ifndef HIDE_VOL
      if (_volIcon) {
        const uint8_t* vbmp = (vol <= 33) ? ICON_VOL_MUTE_18 : (vol <= 66) ? ICON_VOL_DOWN_18 : ICON_VOL_UP_18;
        _volIcon->setBitmap(vbmp, ICON_VOL_W, ICON_VOL_H);
      }
    #endif
  #endif

  if (_volbar) {                                // "vol_step" modification
    _volbar->setValue(vol);
  }
  #ifndef HIDE_VOL
  if(_voltxt) // if we're on the main screen
    _voltxt->setText(config.store.volume, voltxtFmt); // "vol_step" modification
  #endif
  if(_mode==VOL) {
    timekeeper.waitAndReturnPlayer(2);
    _nums->setText(config.store.volume, numtxtFmt);
  }
  /*#ifdef USE_NEXTION
    nextion.setVol(config.store.volume, _mode == VOL);
  #endif */
}

void Display::flip(){ dsp.flip(); }

void Display::invert(){ dsp.invert(); }

void  Display::setContrast(){
  #if DSP_MODEL==DSP_NOKIA5110
    dsp.setContrast(config.store.contrast);
  #endif
}

bool Display::deepsleep(){
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.sleep();
  return true;
#endif
  return false;
}

void Display::wakeup(){
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.wake();
#endif
}

void Display::_refreshWeatherUI() {
  if (!_weather) return;

  _weather->lock(!config.store.showweather);

  if (config.store.showweather) {
    if (timekeeper.weatherBuf && timekeeper.weatherBuf[0] != '\0') {
      _weather->setText(timekeeper.weatherBuf);
    } else {
      _weather->setText(LANG::const_getWeather);
    }
  } else {
  /*#ifndef HIDE_IP
    if (_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
  #endif*/
  #ifndef HIDE_IP
    #if DSP_MODEL != DSP_ST7789_76
      if(_volip) _volip->setText(config.ipToStr(WiFi.localIP()), iptxtFmt);
    #endif
  #endif
  }
}

void Display::_kickWeatherRefresh() {
  if (!_weather) return;
  _weather->lock(!config.store.showweather);
  if (config.store.showweather) {
    _weather->setAlign(weatherConf.widget.align);
    _weather->setText(LANG::const_getWeather);
    timekeeper.forceWeather = true;
  }
}

void Display::requestReconfigure() {
  putRequest(DSP_RECONF, 0);
}

void Display::_beginRebuild() {
  _locked = true;
  resetQueue();
  dsp.setScrollId(NULL);
}

void Display::_rebuildUI() {
  for (auto &p : pages) p = new Page();

  _pager     = new Pager();
  _footer    = new Page();
  _plwidget  = new PlayListWidget();
  _nums      = new NumWidget();
  _clock     = new ClockWidget();
  _date      = new DateWidget();
  _meta      = new ScrollWidget();
  _title1    = new ScrollWidget();
  _plcurrent = new ScrollWidget();
  _title2    = nullptr;
  _speechWidget = nullptr;
  _blfadeWidget = nullptr;
  _lstripWidget = nullptr;
  _buildPager();

  switch (_mode) {
    case PLAYER:     _pager->setPage(pages[PG_PLAYER]);     break;
    case SCREENSAVER:_pager->setPage(pages[PG_SCREENSAVER]);break;
    case STATIONS:   _pager->setPage(pages[PG_PLAYLIST]);   break;
    case VOL:
    case LOST:
    case UPDATING:
    case INFO:
    case SETTINGS:
    case TIMEZONE:
    case WIFI:
    case NUMBERS:
    case SLEEPING:
    case SDCHANGE:
    case SCREENBLANK:
    default:         _pager->setPage(pages[PG_DIALOG]);     break;
  }

  if (_rssi || _rssiIcon) _setRSSI(WiFi.RSSI());
  _volume();
  _station();
  _refreshWeatherUI();
  _time(false);
  _layoutChange(player.isRunning());
}

void Display::_endRebuild() {
  _locked = false;
}

void Display::setSpeechActive(bool v) { putRequest(STATUS_SPEECH, v ? 1 : 0); }
void Display::setBlfadeActive(bool v) { putRequest(STATUS_BLFADE, v ? 1 : 0); }
void Display::setLstripActive(bool v) { putRequest(STATUS_LSTRIP, v ? 1 : 0); }

//============================================================================================================================
#else // !DUMMYDISPLAY
//============================================================================================================================
void Display::init(){
  _createDspTask();
  #ifdef USE_NEXTION
  nextion.begin(true);
  #endif
}
void Display::_start(){
  #ifdef USE_NEXTION
  //nextion.putcmd("page player");
  nextion.start();
  #endif
  config.setTitle(LANG::const_PlReady);
}

void Display::putRequest(displayRequestType_e type, int payload){
  if(type==DSP_START) _start();

  #ifdef USE_NEXTION
    requestParams_t request;
    request.type = type;
    request.payload = payload;
    nextion.putRequest(request);
  #else
    if(type==NEWMODE) mode((displayMode_e)payload);
  #endif
}
//============================================================================================================================
#endif // DUMMYDISPLAY
