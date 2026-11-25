#pragma once
#include "badge_native_api.h"
#include <stdbool.h>
typedef void (*cb_native_keys_fn)(void *, const char *, const char *, const char *, const char *);
void cb_view_bind(const badge_native_api_t *api, cb_native_keys_fn keys);
bool cb_view_begin(void);
void box(int x, int y, int w, int h, bool ink);
void text(int x, int y, int scale, const char *s, bool ink);
void ui_keys(const char *plus, const char *minus, const char *a, const char *b);
void ui_frame(int pulls);
void ui_close(void);
