#ifndef sdmanager_h
#define sdmanager_h

class SDManager : public SDFS {
  public:
    bool ready;
    bool indexing() const { return _indexing; }
  public:
    SDManager(FSImplPtr impl) : SDFS(impl) {}
    bool start();
    void stop();
    bool cardPresent();
    void listSD(File &plSDfile, File &plSDindex, const char * dirname, uint8_t levels);
    void indexSDPlaylist();
  private:
    uint32_t _sdFCount;
    bool _indexing = false;
  private:
    bool _checkNoMedia(const char* path);
    bool _endsWith (const char* base, const char* str);
};

extern SDManager sdman;
#if (defined(SD_SPIPINS) || SD_HSPI) && !(defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && DSP_HSPI)
extern SPIClass  SDSPI;
#endif
#endif
