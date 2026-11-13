// OS-private drawing into an explicit one-bit framebuffer (Info never overwrites app drawing).
#pragma once
#include <stdbool.h>
#include <stdint.h>
void box_into(uint8_t *dst, int x, int y, int w, int h, bool ink);
void text_into(uint8_t *dst, int x, int y, int scale, const char *s, bool ink);
