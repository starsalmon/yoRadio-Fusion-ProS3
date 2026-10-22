#ifdef YORADIO_HANG_DIAG
#include <Arduino.h>
#include <WiFi.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_core_dump.h"
#include "esp_attr.h"
#include "hangdiag.h"
#ifndef HANGDIAG_WDT_S
#define HANGDIAG_WDT_S 30
#endif
struct HD { uint32_t magic, boots, upMs, loopMs, dspMs; char tag[24]; };
RTC_NOINIT_ATTR static HD s_hd;
static const char* rr(esp_reset_reason_t r){
  switch(r){case ESP_RST_POWERON:return "POWERON";case ESP_RST_SW:return "SW";case ESP_RST_PANIC:return "PANIC";
  case ESP_RST_INT_WDT:return "INT_WDT";case ESP_RST_TASK_WDT:return "TASK_WDT";case ESP_RST_WDT:return "WDT";
  case ESP_RST_BROWNOUT:return "BROWNOUT";case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";case ESP_RST_USB:return "USB";
  case ESP_RST_EXT:return "EXT";default:return "OTHER";}}
void hangdiag_boot(){
  esp_reset_reason_t r=esp_reset_reason();
  if(s_hd.magic!=0x48444731){ memset(&s_hd,0,sizeof(s_hd)); s_hd.magic=0x48444731; Serial.println("[HD] RTC breadcrumbs: none (cold)"); }
  else { s_hd.tag[sizeof(s_hd.tag)-1]=0;
    Serial.printf("[HD] prev boot#%lu: up=%lums lastLoop=%lums lastDsp=%lums lastTag=%s\n",(unsigned long)s_hd.boots,(unsigned long)s_hd.upMs,(unsigned long)s_hd.loopMs,(unsigned long)s_hd.dspMs,s_hd.tag); }
  s_hd.boots++;
  Serial.printf("[HD] reset reason=%s(%d) boot#%lu\n",rr(r),(int)r,(unsigned long)s_hd.boots);
  size_t a=0,sz=0;
  if(esp_core_dump_image_get(&a,&sz)==ESP_OK && sz>0) Serial.printf("[HD] coredump present in flash: %u bytes @0x%x (read with espcoredump.py)\n",(unsigned)sz,(unsigned)a);
  else Serial.println("[HD] no coredump in flash");
  esp_task_wdt_config_t c={.timeout_ms=HANGDIAG_WDT_S*1000u,.idle_core_mask=0,.trigger_panic=true};
  esp_err_t e=esp_task_wdt_reconfigure(&c); if(e==ESP_ERR_INVALID_STATE) e=esp_task_wdt_init(&c);
  enableLoopWDT();
  Serial.printf("[HD] TWDT %ds panic (loopTask%s) err=%d\n",HANGDIAG_WDT_S,"+DspTask",(int)e);
}
void hangdiag_hb(const char* tag){
  uint32_t now=millis(); s_hd.loopMs=now; s_hd.upMs=now;
  strncpy(s_hd.tag,tag,sizeof(s_hd.tag)-1);
  static uint32_t last=0;
  if(now-last>=10000){ last=now;
    Serial.printf("[HD] up=%lus heap=%u min=%u big=%u psram=%u wifi=%d rssi=%d dspAge=%lums\n",(unsigned long)(now/1000),
      (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getMinFreeHeap(),(unsigned)ESP.getMaxAllocHeap(),(unsigned)ESP.getFreePsram(),
      (int)WiFi.status(),(int)WiFi.RSSI(),(unsigned long)(now-s_hd.dspMs)); }
}
void hangdiag_dspTaskRegister(){ esp_task_wdt_add(NULL); }
void hangdiag_dspFeed(){ s_hd.dspMs=millis(); esp_task_wdt_reset(); }
#endif
