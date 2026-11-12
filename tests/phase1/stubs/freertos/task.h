#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,int,TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
