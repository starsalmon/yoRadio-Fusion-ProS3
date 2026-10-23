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
    // Cheap change detection: FNV-1a over every indexable media path (same filters/depth
    // as listSD) + count, stored in /data/indexsd.sig. Names/paths are all the index holds.
    bool signatureMatches();
  private:
    bool     _sigOnly = false;
    uint32_t _sigHash = 0;
    uint32_t _sigCount = 0;
    uint32_t _scanEntries = 0;
    void     _sigReset() { _sigHash = 2166136261u; _sigCount = 0; _scanEntries = 0; }
    void     _sigAdd(const char* p) { for (; *p; p++) { _sigHash ^= (uint8_t)*p; _sigHash *= 16777619u; } _sigHash ^= '\n'; _sigHash *= 16777619u; _sigCount++; }
    bool     _readSig(uint32_t& c, uint32_t& h);
    void     _writeSig();
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
