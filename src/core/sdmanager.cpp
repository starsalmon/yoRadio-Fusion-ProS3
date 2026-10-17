//v0.9.686
#include "options.h"
#if SDC_CS!=255
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "vfs_api.h"
#include "sd_diskio.h"
//#define USE_SD
#include "config.h"
#include "sdmanager.h"
#include "display.h"
#include "player.h"
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
#include "sd_spi_shared.h"
#endif

#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && defined(DSP_HSPI) && DSP_HSPI
extern SPIClass SPI2;
#define SDREALSPI SPI2
#elif defined(SD_SPIPINS) || SD_HSPI
SPIClass  SDSPI(HSPI);
#define SDREALSPI SDSPI
#else
  #define SDREALSPI SPI
#endif

#ifndef SDSPISPEED
  #define SDSPISPEED 20000000
#endif

SDManager sdman(FSImplPtr(new VFSImpl()));

bool SDManager::start(){
  // Never touch TFT_CS here. The TFT driver owns it from display.init() onwards (it is pulled up
  // from reset in main.cpp and driven HIGH at LDO2-on in yoradio_user.cpp). At boot in SD mode
  // this runs from setup() while the display task is still drawing the boot logo inside an
  // Adafruit startWrite()..endWrite(); re-running pinMode()+digitalWrite() on TFT_CS from here
  // cut that transfer (logo only partly drawn) and glitched CS mid-byte, so the panel took
  // misaligned bits as commands and dropped to its blank (white) state.
  pinMode(SDC_CS, OUTPUT);
  digitalWrite(SDC_CS, HIGH);
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
  // Hold the shared-bus lock across the whole mount (card init + FAT mount + retries).
  // Recursive: sd_diskio's AcquireSPI takes it again per operation. TFT transactions are
  // still serialised by SPIClass, so a TFT transfer never sees SD traffic mid-way.
  yoradio_sd_spi_acquire();
#endif
  ready = begin(SDC_CS, SDREALSPI, SDSPISPEED);
  vTaskDelay(10);
  if(!ready) ready = begin(SDC_CS, SDREALSPI, SDSPISPEED);
  vTaskDelay(20);
  if(!ready) ready = begin(SDC_CS, SDREALSPI, SDSPISPEED);
  vTaskDelay(50);
  if(!ready) ready = begin(SDC_CS, SDREALSPI, SDSPISPEED);
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
  yoradio_sd_spi_release();
#endif
  return ready;
}

void SDManager::stop(){
  end();
  ready = false;
}
#include "diskio_impl.h"
bool SDManager::cardPresent() {

  if (_indexing) return true;
  if (!ready) return false;
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI
  return true;
#endif
  if(sectorSize()<1) {
    return false;
  }
  // Avoid VLA on stack (can trip compiler/lints); SD sectors are typically 512 bytes.
  uint8_t buff[512] = {0};
  const uint32_t ss = sectorSize();
  if (ss == 0 || ss > sizeof(buff)) {
    return false;
  }
  bool bread = readRAW(buff, 1);
  if(sectorSize()>0 && !bread) return false;
  return bread;
}

bool SDManager::_checkNoMedia(const char* path){
  if (path[strlen(path) - 1] == '/')
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "%s%s", path, ".nomedia");
  else
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "%s/%s", path, ".nomedia");
  bool nm = exists(config.tmpBuf);
  return nm;
}

bool SDManager::_endsWith (const char* base, const char* str) {
  int slen = strlen(str) - 1;
  const char *p = base + strlen(base) - 1;
  while(p > base && isspace(*p)) p--;
  p -= slen;
  if (p < base) return false;
  return (strncmp(p, str, slen) == 0);
}

void SDManager::listSD(File &plSDfile, File &plSDindex, const char* dirname, uint8_t levels) {
    File root = sdman.open(dirname);
    if (!root) {
        Serial.println("##[ERROR]#\tFailed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("##[ERROR]#\tNot a directory");
        return;
    }

    uint32_t pos = 0;
    char* filePath;
    while (true) {
        vTaskDelay(2);
        taskYIELD();
        bool isDir;
        String fileName = root.getNextFileName(&isDir);
        if (fileName.isEmpty()) break;
        filePath = (char*)malloc(fileName.length() + 1);
        if (filePath == NULL) {
            Serial.println("Memory allocation failed");
            break;
        }
        strcpy(filePath, fileName.c_str());
        const char* fn = strrchr(filePath, '/');
        fn = fn ? (fn + 1) : filePath;

        // Skip macOS metadata/AppleDouble and hidden/system files.
        // These commonly appear as "._<name>" and can break SD playback if selected.
        if (fn[0] == '.' ||
            strcmp(fn, ".DS_Store") == 0 ||
            strcmp(fn, "._.DS_Store") == 0 ||
            strcmp(fn, "Thumbs.db") == 0 ||
            strcmp(fn, "desktop.ini") == 0) {
          free(filePath);
          continue;
        }
        if (isDir) {
            if (levels && !_checkNoMedia(filePath)) {
                listSD(plSDfile, plSDindex, filePath, levels - 1);
            }
        } else {
            // Extension checks (case-insensitive).
            char fnLower[64] = {0};
            strlcpy(fnLower, fn, sizeof(fnLower));
            strlwr(fnLower);
            if (_endsWith(fnLower, ".mp3") || _endsWith(fnLower, ".m4a") || _endsWith(fnLower, ".aac") ||
                _endsWith(fnLower, ".wav") || _endsWith(fnLower, ".flac") ||
                _endsWith(fnLower, ".ogg") || _endsWith(fnLower, ".opus")) {
                pos = plSDfile.position();
                plSDfile.printf("%s\t%s\t0\n", fn, filePath);
                plSDindex.write((uint8_t*)&pos, 4);
                Serial.print(".");
                _sdFCount++;
#if !(defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI)
                if (display.mode() == SDCHANGE) {
                  display.putRequest(SDFILEINDEX, _sdFCount);
                }
#endif
                if (_sdFCount % 64 == 0) Serial.println();
            }
        }
        free(filePath);
    }
    root.close();
}

void SDManager::indexSDPlaylist() {
  _sdFCount = 0;
  _indexing = true;
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
  yoradio_sd_spi_acquire();
#elif defined(TFT_CS) && (TFT_CS != 255)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
  vTaskDelay(pdMS_TO_TICKS(50));
  if(exists(PLAYLIST_SD_PATH)) remove(PLAYLIST_SD_PATH);
  if(exists(INDEX_SD_PATH)) remove(INDEX_SD_PATH);
  File playlist = open(PLAYLIST_SD_PATH, "w", true);
  if (!playlist) {
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_release();
#endif
    _indexing = false;
    return;
  }
  File index = open(INDEX_SD_PATH, "w", true);
  if (!index) {
    playlist.close();
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
    yoradio_sd_spi_release();
#endif
    _indexing = false;
    return;
  }
  listSD(playlist, index, "/", SD_MAX_LEVELS);
  index.flush();
  index.close();
  playlist.flush();
  playlist.close();
  Serial.println();
  Serial.printf("##[SD]#\tindex done tracks=%lu\n", (unsigned long)_sdFCount);
  delay(50);
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI && SDC_CS != 255
  yoradio_sd_spi_release();
#endif
  _indexing = false;
#if defined(SD_SHARE_TFT_SPI) && SD_SHARE_TFT_SPI
  if (display.ready() && display.mode() == SDCHANGE) {
    display.putRequest(SDFILEINDEX, (int)_sdFCount);
  }
#endif
}
#endif
