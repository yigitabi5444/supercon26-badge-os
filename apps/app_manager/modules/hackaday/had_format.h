#pragma once
#include <stddef.h>

// The reader and RSS parser use only %s, %.*s and %d.
void had_format(char *out, size_t capacity, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
