#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p)
{ p->taken=false;return p; }
static inline int xSemaphoreTake(SemaphoreHandle_t p,unsigned ticks)
{ (void)ticks;if(p->taken)return 0;p->taken=true;return pdTRUE; }
static inline int xSemaphoreGive(SemaphoreHandle_t p)
{ if(!p->taken)return 0;p->taken=false;return pdTRUE; }
