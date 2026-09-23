#pragma once
#include <Arduino.h>
#include <esp_system.h>
#include <freertos_shim.h>
#define WL_CONNECTED 3
#define WIFI_AP 2
#define WIFI_STA 1
#define WIFI_AP_STA 3
class WiFiClass {
public:
  void begin(const char*, const char*){}
  void disconnect(bool=false,bool=false){}
public:
  int status(){ return 0; }
  IPAddressStub localIP(){ return IPAddressStub(); }
  IPAddressStub softAPIP(){ return IPAddressStub(); }
  int getMode(){ return 1; }
  void mode(int){}
  bool softAP(const char*,const char*){ return true; }
};
extern WiFiClass WiFi;
