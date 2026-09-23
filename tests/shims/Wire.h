#pragma once
#include <Arduino.h>
class TwoWire {
public:
  void begin(int,int){}
  void setClock(unsigned long){}
  void beginTransmission(uint8_t){}
  size_t write(uint8_t){ return 1; }
  size_t write(const uint8_t*,size_t){ return 1; }
  uint8_t endTransmission(){ return 0; }
};
extern TwoWire Wire;
