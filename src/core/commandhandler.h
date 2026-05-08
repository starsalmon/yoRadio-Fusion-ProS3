#ifndef commandhandler_h
#define commandhandler_h

class CommandHandler {
public:
  bool exec(const char *command, const char *value, uint8_t cid=0);

private:
  static bool strEquals(const char *a, const char *b) {
    return strcmp(a, b) == 0;
  }
};

extern CommandHandler cmd;

// ----- HTTP helperk -----
class AsyncWebServerRequest;  // forward deklaráció

namespace CmdHttp {
  void handleSet(AsyncWebServerRequest *request);        // /set
  void handleSetVu(AsyncWebServerRequest *request);      // /setvu
  void handleSetVuLayout(AsyncWebServerRequest *request);// /setvuLayout
  void handleSetDate(AsyncWebServerRequest *request);    // /setdate
  void handleSetNameday(AsyncWebServerRequest *request); // /setnameday
  void handleSetClockFont(AsyncWebServerRequest *request);// /setClockFont
}

// VU-hoz használt globális időzítő
extern uint32_t g_vuSaveDue;

#endif
