#pragma once
#include <Arduino.h>
class File {
public:
  operator bool() const { return false; }
  void printf(const char*,...){}
  String readStringUntil(char){ return String(""); }
  bool available(){ return false; }
  size_t size(){ return 0; }
  size_t write(const unsigned char*, size_t n){ return n; }
  void close(){}
};
class FS { public:
  bool begin(bool=false){ return true; }
  File open(const char*,const char*){ return File(); }
  bool exists(const char*){ return false; }
  bool remove(const char*){ return true; }
};
extern FS LittleFS;
