#pragma once
#include "FreeRTOS.h"
#include <stddef.h>
typedef struct mock_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueReceive(QueueHandle_t,void*,unsigned);
int xQueueSend(QueueHandle_t,const void*,unsigned);
int xQueueOverwrite(QueueHandle_t,const void*);
void vQueueDelete(QueueHandle_t);
