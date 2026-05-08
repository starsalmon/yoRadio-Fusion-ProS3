// Módosítva "vol_step" by Tamás Várai
#include "options.h"
#include "player.h"
#include "config.h"
#include "telnet.h"
#include "display.h"
#include "sdmanager.h"
#include "netserver.h"
#include "timekeeper.h"
#include <time.h> /* ----- Auto On-Off Timer ----- */
#include "../displays/tools/l10n.h"
#include "../pluginsManager/pluginsManager.h"
#include "builtin_led.hpp"
#ifdef USE_NEXTION
#include "../displays/nextion.h"
#endif
Player player;
QueueHandle_t playerQueue;

#if VS1053_CS!=255 && !I2S_INTERNAL
  #if VS_HSPI
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI2),
                      pendingPlayStation(-1),
                      pendingPlayAt(0) {}
  #else
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI),
                      pendingPlayStation(-1),
                      pendingPlayAt(0) {}
  #endif

  void ResetChip(){
    pinMode(VS1053_RST, OUTPUT);
    digitalWrite(VS1053_RST, LOW);
    delay(30);
    digitalWrite(VS1053_RST, HIGH);
    delay(100);
  }

#else

  #if !I2S_INTERNAL
    Player::Player() :
      pendingPlayStation(-1),
      pendingPlayAt(0) {}
  #else
    Player::Player() :
      Audio(true, I2S_DAC_CHANNEL_BOTH_EN),
      pendingPlayStation(-1),
      pendingPlayAt(0) {}
  #endif

#endif

void Player::init() {
  Serial.print("##[BOOT]#\tplayer.init\t");
  playerQueue=NULL;
  _resumeFilePos = 0;
  _hasError=false;
  playerQueue = xQueueCreate( 8, sizeof( playerRequestParams_t ) ); //volt 5
  setOutputPins(false);
  delay(50);
#ifdef MQTT_ROOT_TOPIC
  memset(burl, 0, MQTT_BURL_SIZE);
#endif
  if(MUTE_PIN!=255) pinMode(MUTE_PIN, OUTPUT);
  #if I2S_DOUT!=255
    #if !I2S_INTERNAL
      #ifndef I2S_MCLK
        setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
      #else
        setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK);
      #endif
    #endif
  #else
    SPI.begin();
    if(VS1053_RST>0) ResetChip();
    begin();
  #endif
  setBalance(config.store.balance);
  setTone(config.store.bass, config.store.middle, config.store.trebble);
  setVolumeSteps(100); // "audio_change" "vol_step" Új beállítás, a maximális hangerő.
  setVolume(0);
  _status = STOPPED;
  _volTimer=false;
  //randomSeed(analogRead(0));
  #if PLAYER_FORCE_MONO
    forceMono(true);
  #endif
  _loadVol(config.store.volume);
  setConnectionTimeout(CONNECTION_TIMEOUT, CONNECTION_TIMEOUT_SSL);
  Serial.println("done");
}

/* ----- Auto On-Off Timer --Timed auto start/stop --- */
void Player::checkAutoStartStop() {
    static int lastCheckedMinute = -1;
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int curMinute = tm_now.tm_hour * 60 + tm_now.tm_min;

    if (curMinute == lastCheckedMinute) return; // only once per minute
    lastCheckedMinute = curMinute;

    auto timeToMinutes = [](const char* hhmm) -> int {
        if (strlen(hhmm) != 5 || hhmm[2] != ':') return -1;
        int h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0');
        int m = (hhmm[3] - '0') * 10 + (hhmm[4] - '0');
        if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
        return h * 60 + m;
    };

    int startMinute = timeToMinutes(config.store.autoStartTime);
    int stopMinute  = timeToMinutes(config.store.autoStopTime);
    // --- auto start ---
    if (startMinute == curMinute && !isRunning()) {
        display.putRequest(NEWMODE, PLAYER);
        sendCommand({PR_PLAY, config.lastStation()});
    }
    // --- auto stop ---
    if (stopMinute == curMinute && isRunning()) {
        sendCommand({PR_STOP, 0});
    }
}
/* ----- Auto On-Off Timer --Timed auto start/stop --- */

void Player::sendCommand(playerRequestParams_t request){
  if(playerQueue==NULL) return;
  xQueueSend(playerQueue, &request, PLQ_SEND_DELAY);
}

void Player::resetQueue(){
  if(playerQueue!=NULL) xQueueReset(playerQueue);
}

void Player::stopInfo() {
  config.setSmartStart(0);
  netserver.requestOnChange(MODE, 0);
}

void Player::setError(){
  _hasError=true;
  config.setTitle(config.tmpBuf);
  telnet.printf("##ERROR#:\t%s\n", config.tmpBuf);
}

void Player::setError(const char *e){
  strlcpy(config.tmpBuf, e, sizeof(config.tmpBuf));
  setError();
}

void Player::_stop(bool alreadyStopped){
  log_i("%s called", __func__);
  //if(config.getMode()==PM_SDCARD && !alreadyStopped) config.sdResumePos = player.getAudioFilePosition();
  if (config.getMode() == PM_SDCARD && !alreadyStopped) {
    config.sdResumePos = player.getAudioFilePosition();
    config.stopedSdStationId = config.lastStation();
  }
  _status = STOPPED;
  setOutputPins(false);
  if(!_hasError) config.setTitle((display.mode()==LOST || display.mode()==UPDATING)?"":LANG::const_PlStopped);
  config.station.bitrate = 0;
  config.setBitrateFormat(BF_UNKNOWN);
  #ifdef USE_NEXTION
    nextion.bitrate(config.station.bitrate);
  #endif
  //setDefaults();
  if(!alreadyStopped) stopSong();
  netserver.requestOnChange(BITRATE, 0);
  display.putRequest(DBITRATE);
  display.putRequest(PSTOP);
  //setDefaults();
  //if(!alreadyStopped) stopSong();
  if(!lockOutput) stopInfo();
  if (player_on_stop_play) player_on_stop_play();
  pm.on_stop_play();
}

void Player::initHeaders(const char *file) {
  if(strlen(file)==0 || true) return; //TODO Read TAGs
  connecttoFS(sdman,file);
  //eofHeader = false;
  //while(!eofHeader) Audio::loop();
  //netserver.requestOnChange(SDPOS, 0);
  //setDefaults();
}
void resetPlayer(){
  if(!config.store.watchdog) return;
  player.resetQueue();
  player.sendCommand({PR_STOP, 0});
  player.loop();
}

#ifndef PL_QUEUE_TICKS
  #define PL_QUEUE_TICKS 0
#endif
#ifndef PL_QUEUE_TICKS_ST
  #define PL_QUEUE_TICKS_ST 15
#endif
void Player::loop() {
if (pendingPlayStation >= 0 && millis() >= pendingPlayAt) {
  int st = pendingPlayStation;
  pendingPlayStation = -1;

  Serial.printf("[Deferred PLAY safe] %d\n", st);
  sendCommand({PR_PLAY, st});
}

  if(playerQueue==NULL) return;
  playerRequestParams_t requestP;
  if(xQueueReceive(playerQueue, &requestP, isRunning()?PL_QUEUE_TICKS:PL_QUEUE_TICKS_ST)){
    switch (requestP.type){
      case PR_STOP: _stop(); break;
      case PR_PLAY: {
        uint16_t st = (uint16_t)abs(requestP.payload);

#ifdef USE_DLNA
        if (config.store.playlistSource == PL_SRC_DLNA) {
          config.store.lastDlnaStation = st;
          config.saveValue(&config.store.lastDlnaStation, (uint16_t)st);
          config.sdResumePos = 0;
          config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_DLNA);
          _play(st);
          netserver.requestOnChange(GETINDEX, 0);
          break;
        }
#endif

        // ==== EREDETI VISSELKEDÉS (WEB / SD) ====
        if (requestP.payload > 0) {
          config.setLastStation(st);
        }
#ifdef USE_DLNA
        config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_WEB);
#endif
        _play(st);
        if (player_on_station_change) player_on_station_change();
        pm.on_station_change();
        break;
      }
      case PR_TOGGLE: {
        toggle();
        break;
      }
      case PR_VOL: {
        config.setVolume(requestP.payload);
        Audio::setVolume(volToI2S(requestP.payload));
        break;
      }
      #ifdef USE_SD
      case PR_CHECKSD: {
        if(config.getMode()==PM_SDCARD){
          if(!sdman.cardPresent()){
            sdman.stop();
            config.changeMode(PM_WEB);
          }
        }
        break;
      }
      #endif
      case PR_VUTONUS: {
        if(config.vuRefLevel>10) config.vuRefLevel -=10;
        break;
      }
      case PR_BURL: {
      #ifdef MQTT_ROOT_TOPIC
        if(strlen(burl)>0){
          browseUrl();
        }
      #endif
        break;
      }
      case PR_SWITCH_PLAYLIST: {
      #ifdef USE_DLNA
        // Switch playlist source (payload: 0=WEB, 1=DLNA, 2=SD)
        if(requestP.payload == 0) {
          // Switch to WEB playlist
          config.store.playlistSource = PL_SRC_WEB;
          config.saveValue(&config.store.playlistSource, (uint8_t)PL_SRC_WEB);
          config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_WEB);
          config.saveValue(&config.store.play_mode, (uint8_t)PM_WEB);
          telnet.print("##INFO#:\tSwitched to web playlist\n");
          // Stop current playback and switch to last web station
          if(isRunning()) {
            _stop();
          }
          // Optionally auto-play last web station
          // sendCommand({PR_PLAY, config.store.lastStation});
        } else if(requestP.payload == 1) {
          // Switch to DLNA playlist
          config.store.playlistSource = PL_SRC_DLNA;
          config.saveValue(&config.store.playlistSource, (uint8_t)PL_SRC_DLNA);
          config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_DLNA);
          config.saveValue(&config.store.play_mode, (uint8_t)PM_WEB);
          telnet.print("##INFO#:\tSwitched to DLNA playlist\n");
          if(isRunning()) {
            _stop();
          }
        } else if(requestP.payload == 2) {
          // Switch to SD card
#ifdef USE_SD
          if(SDC_CS != 255 && sdman.cardPresent()) {
            config.store.playlistSource = PL_SRC_WEB; // SD uses WEB source internally
            config.saveValue(&config.store.playlistSource, (uint8_t)PL_SRC_WEB);
            config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_WEB);
            config.saveValue(&config.store.play_mode, (uint8_t)PM_SDCARD);
            telnet.print("##INFO#:\tSwitched to SD card\n");
            if(isRunning()) {
              _stop();
            }
          } else {
            telnet.print("##ERROR#:\tSD card not available\n");
          }
#endif
        }
      #endif
        break;
      }

      default: break;
    }
  }
  checkAutoStartStop();   /* ----- Auto On-Off Timer ----- */
  Audio::loop();

#ifdef USE_SD
if (
  config.getMode() == PM_SDCARD &&
  !isRunning() &&
  _status == PLAYING &&
  player.getAudioFilePosition() == 0
) {
  Serial.println("[SD] EOF -> next()");
  next();
  return;
}
#endif

  if(_volTimer){
    if((millis()-_volTicks)>3000){
      config.saveVolume();
      _volTimer=false;
    }
  }
  
  
  /*
#ifdef MQTT_ROOT_TOPIC
  if(strlen(burl)>0){
    browseUrl();
  }
#endif*/
}

void Player::setOutputPins(bool isPlaying) {
  builtin_led_set(isPlaying);
  bool _ml = MUTE_LOCK?!MUTE_VAL:(isPlaying?!MUTE_VAL:MUTE_VAL);
  if(MUTE_PIN!=255) digitalWrite(MUTE_PIN, _ml);
}

void Player::_play(uint16_t stationId) {
  log_i("%s called, stationId=%d", __func__, stationId);
  _hasError=false;
  //setDefaults();
  _status = STOPPED;
  setOutputPins(false);
  remoteStationName = false;
  
  if(!config.prepareForPlaying(stationId)) return;
  _loadVol(config.store.volume);
  
  bool isConnected = false;
  if (config.getMode() == PM_SDCARD && SDC_CS != 255) {
    // A connecttoFS NEM támogat start offsetet SD-n → -1, indítás pozícionálás nélkül.
    isConnected = connecttoFS(sdman, config.station.url, -1);
  } else {
#ifdef USE_DLNA //DLNA mod
  // DLNA is WEB engine, de nem írjuk felül a mode-ot
    if (config.store.playlistSource != PL_SRC_DLNA)
#endif
    {
      config.saveValue(&config.store.play_mode,
                       static_cast<uint8_t>(PM_WEB));
    }
  }
  if (config.getMode() == PM_WEB) {
    isConnected = connecttohost(config.station.url);
  }

  connproc = true;
  if(isConnected){
    _status = PLAYING;
    config.configPostPlaying(stationId);
    setOutputPins(true);
    if (player_on_start_play) player_on_start_play();
    pm.on_start_play();
  }else{
    telnet.printf("##ERROR#:\tError connecting to %.128s\n", config.station.url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", config.station.url); setError();
#ifdef USE_DLNA
    // If DLNA connection failed, fall back to web playlist (similar to SD card behavior)
    if (config.store.playlistSource == PL_SRC_DLNA) {
      telnet.print("##INFO#:\tDLNA failed, switching to web playlist\n");
      config.store.playlistSource = PL_SRC_WEB;
      config.saveValue(&config.store.playlistSource, (uint8_t)PL_SRC_WEB);
      config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_WEB);
      // Optionally reload playlist or switch to last known good web station
      // config.setLastStation(config.store.lastStation);
    }
#endif
    _stop(true);
  };
}

#ifdef MQTT_ROOT_TOPIC
void Player::browseUrl(){
  _hasError=false;
  remoteStationName = true;
  config.setDspOn(1);
  resumeAfterUrl = _status==PLAYING;
  display.putRequest(PSTOP);
  setOutputPins(false);
  config.setTitle(LANG::const_PlConnect);
  if (connecttohost(burl)){
    _status = PLAYING;
    config.setTitle("");
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    display.putRequest(PSTART);
    if (player_on_start_play) player_on_start_play();
    pm.on_start_play();
  }else{
    telnet.printf("##ERROR#:\tError connecting to %.128s\n", burl);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", burl); setError();
    _stop(true);
  }
  //memset(burl, 0, MQTT_BURL_SIZE);
}
#endif

void Player::prev() {
  uint16_t lastStation = config.lastStation();
  if (config.getMode() != PM_SDCARD) {
    // WEB + DLNA: always sequential wrap
    if (lastStation <= 1) config.lastStation(config.playlistLength());
    else config.lastStation(lastStation - 1);
  } else {
    // SD: sequential unless snuffle enabled (SD-only feature)
    if (!config.store.sdsnuffle) {
      if (lastStation <= 1) config.lastStation(config.playlistLength());
      else config.lastStation(lastStation - 1);
    } else {
      config.lastStation(random(1, config.playlistLength() + 1));
    }
  }
  config.stopedSdStationId = -1;  // Reseteli a seek hez mentett SD fájl sorszámát.
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::next() {
  uint16_t lastStation = config.lastStation();
  if (config.getMode() != PM_SDCARD) {
    // WEB + DLNA: always sequential wrap
    if (lastStation >= config.playlistLength()) config.lastStation(1);
    else config.lastStation(lastStation + 1);
  } else {
    // SD: sequential unless snuffle enabled
    if (!config.store.sdsnuffle) {
      if (lastStation >= config.playlistLength()) config.lastStation(1);
      else config.lastStation(lastStation + 1);
    } else {
      config.lastStation(random(1, config.playlistLength() + 1));
    }
  }
  config.stopedSdStationId = -1;  // Reseteli a seek hez mentett SD fájl sorszámát.
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::toggle() {
  if (_status == PLAYING) {
    sendCommand({PR_STOP, 0});
  } else {
    sendCommand({PR_PLAY, config.lastStation()});
  }
}

void Player::stepVol(bool up) {
  if (up) {
    if (config.store.volume <= 100 - config.store.volsteps) setVol(config.store.volume + config.store.volsteps);
    else setVol(100);
  } else {
    if (config.store.volume >= config.store.volsteps) setVol(config.store.volume - config.store.volsteps);
    else setVol(0);
  }
}

uint8_t Player::volToI2S(uint8_t volume) {
  int vol = map(volume, 0, 100 - config.station.ovol , 0, 100);  // Módosítás "vol_step"
  if (vol > 100) vol = 100;
  if (vol < 0) vol = 0;
  return vol;
}

void Player::_loadVol(uint8_t volume) {
  setVolume(volToI2S(volume));
}

void Player::setVol(uint8_t volume) {
  _volTicks = millis();
  _volTimer = true;
  player.sendCommand({PR_VOL, volume});
}

#ifdef USE_DLNA
void Player::switchToWebPlaylist() {
  sendCommand({PR_SWITCH_PLAYLIST, 0});  // 0 = PL_SRC_WEB
}
#endif

