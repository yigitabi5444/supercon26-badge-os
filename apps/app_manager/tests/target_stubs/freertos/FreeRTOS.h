#pragma once
#include <stdbool.h>
typedef struct { bool taken; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
#define pdTRUE 1
