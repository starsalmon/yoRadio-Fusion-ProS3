#ifndef widgets_h
#define widgets_h
#if DSP_MODEL!=DSP_DUMMY
#include "widgetsconfig.h"

#ifndef DSP_LCD
  #define CHARWIDTH   6
  #define CHARHEIGHT  8
#else
  #define CHARWIDTH   1
  #define CHARHEIGHT  1
#endif

class psFrameBuffer;

class Widget{
  public:
    Widget(){ _active   = false; }
    virtual ~Widget(){}
    virtual void loop(){}
    virtual void init(WidgetConfig conf, uint16_t fgcolor, uint16_t bgcolor){
      _config = conf;
      _fgcolor  = fgcolor;
      _bgcolor  = bgcolor;
      _width = _backMove.width = 0;
      _backMove.x = _config.left;
      _backMove.y = _config.top;
      _moved = _locked = false;
    }
    void setAlign(WidgetAlign align){
      _config.align = align;
    }
    void setActive(bool act, bool clr=false) { _active = act; if(_active && !_locked) _draw(); if(clr && !_locked) _clear(); }
    void setFgColor(uint16_t fg) {
      if (_fgcolor == fg) return;
      if (_active && !_locked) _clear();
      _fgcolor = fg;
      if (_active && !_locked) _draw();
    }
    void lock(bool lck=true) { _locked = lck; if(_locked) _reset(); if(_locked && _active) _clear();  }
    void unlock() { _locked = false; }
    bool locked() { return _locked; }
    void moveTo(MoveConfig mv){
      if(mv.width<0) return;
      _moved = true;
      if(_active && !_locked) _clear();
      _config.left = mv.x;
      _config.top = mv.y;
      if(mv.width>0) _width = mv.width;
      _reset();
      _draw();
    }
    void moveBack(){
      if(!_moved) return;
      if(_active && !_locked) _clear();
      _config.left = _backMove.x;
      _config.top = _backMove.y;
      _width = _backMove.width;
      _moved = false;
      _reset();
      _draw();
    }
  protected:
    bool _active, _moved, _locked;
    uint16_t _fgcolor, _bgcolor, _width;
    WidgetConfig _config;
    MoveConfig   _backMove;
    virtual void _draw() {}
    virtual void _clear() {}
    virtual void _reset() {}
};

enum class BitmapFormat : uint8_t {
  // XBM: 1-bit, LSB-first within each byte (AdafruitGFX drawXBitmap)
  XBM_LSB = 0,
  // GFX bitmap: 1-bit, MSB-first within each byte (AdafruitGFX drawBitmap)
  GFX_MSB = 1,
};

class BitmapWidget: public Widget {
  public:
    BitmapWidget() {}
    BitmapWidget(WidgetConfig wconf, const uint8_t* bmp, uint8_t w, uint8_t h, uint16_t fgcolor, uint16_t bgcolor, BitmapFormat fmt = BitmapFormat::XBM_LSB) {
      init(wconf, bmp, w, h, fgcolor, bgcolor, fmt);
    }
    using Widget::init;
    void init(WidgetConfig wconf, const uint8_t* bmp, uint8_t w, uint8_t h, uint16_t fgcolor, uint16_t bgcolor, BitmapFormat fmt = BitmapFormat::XBM_LSB);
    void setBitmap(const uint8_t* bmp, uint8_t w, uint8_t h);
    void setBitmapAndColor(const uint8_t* bmp, uint8_t w, uint8_t h, uint16_t fgcolor);
  protected:
    const uint8_t* _bmp = nullptr;
    uint8_t _bw = 0;
    uint8_t _bh = 0;
    BitmapFormat _fmt = BitmapFormat::XBM_LSB;
    void _draw();
    void _clear();
};

class TextWidget: public Widget {
  public:
    TextWidget() {}
    TextWidget(WidgetConfig wconf, uint16_t buffsize, bool uppercase, uint16_t fgcolor, uint16_t bgcolor) { init(wconf, buffsize, uppercase, fgcolor, bgcolor); }
    ~TextWidget();
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t buffsize, bool uppercase, uint16_t fgcolor, uint16_t bgcolor);
    void setText(const char* txt);
    void setText(int val, const char *format);
    void setText(const char* txt, const char *format);
    bool uppercase() { return _uppercase; }
    void setGfxFont(const GFXfont* f) { _gfxFont = f; }
    bool hasGfxFont() const { return _gfxFont != nullptr; }
  protected:
    char *_text;
    char *_oldtext;
    bool _uppercase;
    uint16_t  _buffsize, _textwidth, _oldtextwidth, _oldleft, _textheight;
    uint8_t _charWidth;
    uint8_t _classicTextSize = 1;
    const GFXfont* _gfxFont = nullptr;
  protected:
    void _draw();
    uint16_t _realLeft(bool w_fb=false);
    void _charSize(uint8_t textsize, uint8_t& width, uint16_t& height);
    uint16_t _measureText(const char* txt);
    int16_t  _baselineY();
};

class FillWidget: public Widget {
  public:
    FillWidget() {}
    FillWidget(FillConfig conf, uint16_t bgcolor) { init(conf, bgcolor); }
    using Widget::init;
    void init(FillConfig conf, uint16_t bgcolor);
    void setHeight(uint16_t newHeight);
  protected:
    uint16_t _height;
    void _draw();
};

class ScrollWidget: public TextWidget {
  public:
    ScrollWidget(){}
    ScrollWidget(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    ~ScrollWidget();
    using Widget::init;
    void init(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    void loop();
    void setText(const char* txt);
    void setText(const char* txt, const char *format);
    void setWindowWidth(uint16_t w);
  private:
    char *_sep;
    char *_window;
    int _winchars = 0;
    int16_t _x;
    bool _doscroll;
    uint8_t _scrolldelta;
    uint16_t _scrolltime;
    uint32_t _scrolldelay;
    uint16_t _sepwidth, _startscrolldelay;
    uint8_t _charWidth;
    uint16_t _winLeft(bool fb = false) const;
    psFrameBuffer* _fb=nullptr;
    /* pixel-alapú scroll support (FreeSans fontokhoz) */
    uint16_t _pixelTextWidth(const char* txt);
    void     _applyFont(Adafruit_GFX& gfx);
  private:
    void _setTextParams();
    void _calcX();
    void _drawFrame();
    void _draw();
    bool _checkIsScrollNeeded();
    bool _checkDelay(int m, uint32_t &tstamp);
    void _clear();
    void _reset();
};

class SliderWidget: public Widget {
  public:
    SliderWidget(){}
    SliderWidget(FillConfig conf, uint16_t fgcolor, uint16_t bgcolor, uint32_t maxval, uint16_t oucolor=0){
      init(conf, fgcolor, bgcolor, maxval, oucolor);
    }
    using Widget::init;
    void init(FillConfig conf, uint16_t fgcolor, uint16_t bgcolor, uint32_t maxval, uint16_t oucolor=0);
    void setValue(uint32_t val);
  protected:
    uint16_t _height, _oucolor, _oldvalwidth;
    uint32_t _max, _value;
    bool _outlined;
    void _draw();
    void _drawslider();
    void _clear();
    void _reset();
};

class VuWidget: public Widget {
  public:
    VuWidget() {}
    VuWidget(WidgetConfig wconf, VUBandsConfig bands, uint16_t vumaxcolor, uint16_t vumincolor, uint16_t bgcolor)
            { init(wconf, bands, vumaxcolor, vumincolor, bgcolor); }
    ~VuWidget();
    using Widget::init;
    void init(WidgetConfig wconf, VUBandsConfig bands, uint16_t vumaxcolor, uint16_t vumincolor, uint16_t bgcolor);
    void loop();
  private:
    int16_t _peakL;                    
    int16_t _peakR;                    
    const uint16_t _peakFallRate = 2;  
    uint8_t _peakFallDelayCounter;     
    const uint8_t _peakFallDelay = 2;  
    uint16_t _maxDimension;
    uint8_t _labelSize = 18;
    void _drawLabelsVertical(int x, int y, int w, int labelSize);
    void _drawLabelsHorizontal(int x, int y, int h, int labelSize);
    void _drawSingleLabel(int x, int y, int h, char ch, int labelSize);

  protected:
    #if !defined(DSP_LCD) && !defined(DSP_OLED)
      Canvas *_canvas;
    #endif
    VUBandsConfig _bands;
    uint16_t _vumaxcolor, _vumincolor;
    void _draw();
    void _clear();
};

class NumWidget: public TextWidget {
  public:
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t buffsize, bool uppercase, uint16_t fgcolor, uint16_t bgcolor);
    void setText(const char* txt);
    void setText(int val, const char *format);
  protected:
    void _getBounds();
    void _draw();
};

class ProgressWidget: public TextWidget {
  public:
    ProgressWidget() {}
    ProgressWidget(WidgetConfig conf, ProgressConfig pconf, uint16_t fgcolor, uint16_t bgcolor) { 
      init(conf, pconf, fgcolor, bgcolor);
    }
    using Widget::init;
    void init(WidgetConfig conf, ProgressConfig pconf, uint16_t fgcolor, uint16_t bgcolor){
      TextWidget::init(conf, pconf.width, false, fgcolor, bgcolor);
      _speed = pconf.speed; _width = pconf.width; _barwidth = pconf.barwidth;
      _pg = 0; 
    }
    void loop();
  private:
    uint8_t _pg;
    uint16_t _speed, _barwidth;
    uint32_t _scrolldelay;
    void _progress();
    bool _checkDelay(int m, uint32_t &tstamp);
};

class ClockWidget: public Widget {
  public:
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t fgcolor, uint16_t bgcolor);
    void draw(bool force=false);
    uint8_t textsize(){ return _config.textsize; }
    void clear(){ _clearClock(); }
    inline uint16_t dateSize(){ return _space+ _dateheight; }
    inline uint16_t clockWidth(){ return _clockwidth; }
    uint16_t clockWidth()  const;
    uint16_t clockHeight() const;
    uint16_t dateSize()    const;
  private:
  #ifndef DSP_LCD
    #if DSP_MODEL==DSP_ILI9225
    auto &getRealDsp();
    #else
    Adafruit_GFX &getRealDsp();
    #endif
  #endif
    int _lastSec = -1;
    ScrollWidget* _dateScroll = nullptr;
    uint8_t  _dateScrollSize = 0;
    uint16_t _dateleft_px = 0; 
    uint16_t _datewidth_px = 0;
  protected:
    char  _timebuffer[20]="00:00";
    char _tmp[128], _datebuf[128];
    uint8_t _superfont;
    uint16_t _clockleft, _clockwidth, _timewidth, _dotsleft, _linesleft;
    uint8_t  _clockheight, _timeheight, _dateheight, _space;
    uint16_t _forceflag = 0;
    bool dots = true;
    bool _fullclock;
    psFrameBuffer* _fb=nullptr;
    void _draw();
    void _clear();
    void _reset();
    void _getTimeBounds();
    void _printClock(bool force=false);
    void _clearClock();
    bool _getTime();
    uint16_t _left();
    uint16_t _top();
    void _begin();
};

class BitrateWidget: public Widget {
  public:
    BitrateWidget() {}
    BitrateWidget(BitrateConfig bconf, uint16_t fgcolor, uint16_t bgcolor) { init(bconf, fgcolor, bgcolor); }
    ~BitrateWidget(){}
    using Widget::init;
    void init(BitrateConfig bconf, uint16_t fgcolor, uint16_t bgcolor);
    void setBitrate(uint16_t bitrate);
    void setFormat(BitrateFormat format);
  protected:
    BitrateFormat _format;
    char _buf[6];
    uint8_t _charWidth;
    uint16_t _dimension, _bitrate, _textheight;
    void _draw();
    void _clear();
    void _charSize(uint8_t textsize, uint8_t& width, uint16_t& height);
};
/**
 * StationNumWidget – outline box a sorszammal (mint BitrateWidget bal fele)
 * PlayModeWidget   – filled box a moddal     (mint BitrateWidget jobb fele)
 * Mindketto theme.bitrate szint hasznal.
 */
class StationNumWidget: public Widget {
  public:
    StationNumWidget() {}
    StationNumWidget(StationNumConfig conf, uint16_t fgcolor, uint16_t bgcolor) { init(conf, fgcolor, bgcolor); }
    ~StationNumWidget() {}
    using Widget::init;
    void init(StationNumConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    void setNum(uint16_t num);
  protected:
    uint16_t _dimension;
    uint16_t _num;
    void _draw();
    void _clear();
};

class PlayModeWidget: public Widget {
  public:
    PlayModeWidget() {}
    PlayModeWidget(PlayModeConfig conf, uint16_t fgcolor, uint16_t bgcolor) { init(conf, fgcolor, bgcolor); }
    ~PlayModeWidget() {}
    using Widget::init;
    void init(PlayModeConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    void setMode(uint8_t mode);
  protected:
    uint16_t _dimension;
    uint8_t  _mode;
    void _draw();
    void _clear();
};

class PlayListWidget: public Widget {
  public:
    using Widget::init;
    void init(ScrollWidget* current);
    void drawPlaylist(uint16_t currentItem);
    inline uint16_t itemHeight(){ return _plItemHeight; }
    inline uint16_t currentTop(){ return _plYStart+_plCurrentPos*_plItemHeight; }

  private:
    ScrollWidget* _current;

    uint16_t _plItemHeight, _plTtemsCount, _plCurrentPos;
    int      _plYStart;

    // ===== Moving cursor cache =====
    static const uint8_t MAX_PL_PAGE_ITEMS = 15;
    String   _plCache[MAX_PL_PAGE_ITEMS];
    int16_t  _plLoadedPage = -1;
    int16_t  _plLastGlobalPos = -1;
    uint32_t _plLastDrawTime = 0;

    // ===== common =====
    uint8_t _fillPlMenu(int from, uint8_t count);

    // Két külön kirajzoló ág
    void _drawMovingCursor(uint16_t currentItem);
    void _drawScrollCenter(uint16_t currentItem);

    void _printMoving(uint8_t pos, const char* item);
    void _printScroll(uint8_t pos, const char* item);

    void _loadPlaylistPage(int pageIndex, int itemsPerPage);
};

class DateWidget : public ScrollWidget {
public:
  //using ScrollWidget::ScrollWidget;
  using ScrollWidget::init;

  void init(ScrollConfig conf, uint16_t fg, uint16_t bg) {
    ScrollWidget::init("\007", conf, fg, bg);
  }

  void setNamedayEnabled(bool en) { _namedayEnabled = en; }
  void update();

private:
  bool _namedayEnabled = true;
};

class StatusWidget : public Widget {
  public:
    StatusWidget() {}
    StatusWidget(StatusWidgetConfig conf, const char* label,
                 uint16_t activecolor, uint16_t inactivecolor)
      { init(conf, label, activecolor, inactivecolor); }
    ~StatusWidget() {}
    using Widget::init;
    void init(StatusWidgetConfig conf, const char* label,
              uint16_t activecolor, uint16_t inactivecolor);
    // _statusActive tárolja a tényleges logikai állapotot (be/ki van-e kapcsolva a feature)
    // Page::setActive(true)  → újrarajzol a tárolt állapottal
    // Page::setActive(false) → nem csinál semmit (clearDsp úgyis törli)
    // setStat(bool) → beállítja és rajzolja az állapotot
    void setActive(bool act, bool clr=false) { if(act) _draw(); }
    void setStat(bool stat) { _active = stat; _draw(); }
  protected:
    uint16_t _boxW, _boxH;
    const char* _label;
    void _draw();
    void _clear();
};

class WeatherIconWidget : public Widget {
  public:
    using Widget::init;

    void init(WidgetConfig wconf, uint16_t bgcolor);
    void setIcon(const char* code);
    void setTemp(float tempC);

  protected:
    const uint16_t* _img = nullptr;
    uint16_t _iw = 0, _ih = 0;
    char _temp[8] = {0};
    bool _hasTemp = false;
    void _draw() override;
    void _clear() override;
};

// Simple RGB565 image widget (for station logos, etc.)
class RgbImageWidget : public Widget {
  public:
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t bgcolor);
    void setImage(const uint16_t* img, uint16_t w, uint16_t h);
  protected:
    const uint16_t* _img = nullptr;
    uint16_t _iw = 0, _ih = 0;
    bool _hasDrawn = false;
    bool _useKey = false;
    uint16_t _key = 0;
    void _draw() override;
    void _clear() override;
};

#endif
#endif




