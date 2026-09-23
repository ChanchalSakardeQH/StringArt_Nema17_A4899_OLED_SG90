#pragma once
#include <Arduino.h>
class File {
public:
  operator bool() const { return false; }
  void printf(const char*,...){}
  String readStringUntil(char){ return String(""); }
  bool available(){ return false; }
  void close(){}
};
class FS { public:
  bool begin(bool=false){ return true; }
  File open(const char*,const char*){ return File(); }
};
extern FS LittleFS;
