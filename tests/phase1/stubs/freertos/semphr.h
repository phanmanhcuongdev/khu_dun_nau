#pragma once
#include "FreeRTOS.h"
#include <stdlib.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){pthread_mutex_t *m=malloc(sizeof(*m));if(m)pthread_mutex_init(m,NULL);return m;}
static inline int xSemaphoreTake(SemaphoreHandle_t m,int t){(void)t;return pthread_mutex_lock(m)==0;}
static inline void xSemaphoreGive(SemaphoreHandle_t m){pthread_mutex_unlock(m);}
