#pragma once
#define UPLOAD_FILE_START 0
#define UPLOAD_FILE_WRITE 1
#define UPLOAD_FILE_END 2
#define UPLOAD_FILE_ABORTED 3
struct HTTPUpload { int status=0; unsigned currentSize=0; unsigned char* buf=nullptr; };
#include <Arduino.h>
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_OPTIONS, HTTP_DELETE };
class WebServer {
public:
  WebServer(int){}
  void on(const char*, void(*)()){}
  void on(const char*, HTTPMethod, void(*)()){}
  void on(const char*, HTTPMethod, void(*)(), void(*)()){}
  HTTPUpload& upload(){ static HTTPUpload u; return u; }
  void streamFile(class File&, const char*){}
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
