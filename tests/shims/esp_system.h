#pragma once
#include <Arduino.h>
inline int esp_reset_reason(){ return 0; }
class EspClass { public: unsigned getFreeHeap(){return 0;} void restart(){} };
extern EspClass ESP;
