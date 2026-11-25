#pragma once
#include <stddef.h>

// The Board view uses only %s, %.Ns, %u and %%; capacity includes the NUL.
void cb_format(char *out, size_t capacity, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
