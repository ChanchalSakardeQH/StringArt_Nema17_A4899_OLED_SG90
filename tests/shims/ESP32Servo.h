#pragma once
#include <Arduino.h>
class ESP32PWM { public: static void allocateTimer(int){} };
class Servo {
public:
  void setPeriodHertz(int){}
  int attach(int){ return 1; }
  int attach(int,int,int){ return 1; }
  void write(int){}
  int read(){ return 0; }
};
