#pragma once
#include <Arduino.h>
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_OPTIONS, HTTP_DELETE };
class WebServer {
public:
  WebServer(int){}
  void on(const char*, void(*)()){}
  void on(const char*, HTTPMethod, void(*)()){}
  void onNotFound(void(*)()){}
  void begin(){}
  void handleClient(){}
  bool hasArg(const char*){ return false; }
  String arg(const char*){ return String(""); }
  HTTPMethod method(){ return HTTP_GET; }
  void send(int){}
  void send(int,const char*,const String&){}
  void send(int,const char*,const char*){}
  void sendHeader(const char*,const char*){}
  void setContentLength(size_t){}
  void sendContent(const char*,size_t){}
  void sendContent(const String&){}
  void send_P(int,const char*,const char*){}
};
