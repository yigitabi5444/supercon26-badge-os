#pragma once
#include "FreeRTOS.h"
typedef struct native_test_task *TaskHandle_t;
typedef enum { eRunning,eReady,eBlocked,eSuspended,eDeleted,eInvalid } eTaskState;
BaseType_t xTaskCreate(void (*)(void *),const char *,uint32_t,void *,UBaseType_t,TaskHandle_t *);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t);
TickType_t xTaskGetTickCount(void);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t);
void vTaskDelay(TickType_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDelete(TaskHandle_t);
eTaskState eTaskGetState(TaskHandle_t);
