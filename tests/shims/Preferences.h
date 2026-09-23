#pragma once
#include <Arduino.h>
class Preferences {
public:
  bool begin(const char*, bool=false){ return true; }
  void end(){}
  String getString(const char*, const String &d=String()){ return d; }
  size_t putString(const char*, const String &){ return 0; }
  bool remove(const char*){ return true; }
};
