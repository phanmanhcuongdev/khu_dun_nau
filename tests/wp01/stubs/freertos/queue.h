#pragma once
#include <stddef.h>
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
extern int queue_ok;
static inline QueueHandle_t xQueueCreate(int n,size_t s){(void)n;(void)s;return queue_ok?(void*)1:NULL;}
static inline int xQueueReceive(QueueHandle_t q,void *p,unsigned t){(void)q;(void)p;(void)t;return 0;}
static inline int xQueueOverwrite(QueueHandle_t q,const void *p){(void)q;(void)p;return pdPASS;}
static inline void vQueueDelete(QueueHandle_t q){(void)q;}
