#pragma once
#include "FreeRTOS.h"
extern int task_ok;
static inline int xTaskCreate(void (*fn)(void*),const char *n,unsigned stack,void *p,int prio,void *h){(void)fn;(void)n;(void)stack;(void)p;(void)prio;(void)h;return task_ok;}
static inline void vTaskDelay(unsigned ticks){(void)ticks;}
