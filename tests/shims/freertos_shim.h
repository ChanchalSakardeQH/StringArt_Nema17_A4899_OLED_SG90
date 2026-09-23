#pragma once
#include <Arduino.h>
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
#define portMAX_DELAY 0xffffffffUL
#define portTICK_PERIOD_MS 1
inline unsigned pdMS_TO_TICKS(unsigned ms){ return ms; }
inline void vTaskDelay(unsigned){}
inline SemaphoreHandle_t xSemaphoreCreateMutex(){ return nullptr; }
inline int xSemaphoreTake(SemaphoreHandle_t,unsigned long){ return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t){ return 1; }
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*,int){ return 1; }
