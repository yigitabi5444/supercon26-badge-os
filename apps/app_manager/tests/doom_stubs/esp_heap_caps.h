#pragma once
#include <stddef.h>
enum { MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2 };
void *heap_caps_malloc(size_t,unsigned);
void *heap_caps_realloc(void *,size_t,unsigned);
void heap_caps_free(void *);
