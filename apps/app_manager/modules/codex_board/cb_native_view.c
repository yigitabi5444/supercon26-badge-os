#include "cb_native_view.h"

static const badge_native_api_t *host;
static cb_native_keys_fn set_keys;

void cb_view_bind(const badge_native_api_t *api, cb_native_keys_fn keys)
{
    host = api;
    set_keys = keys;
}
bool cb_view_begin(void)
{
    if (!host->begin_frame(host->context)) return false;
    host->clear(host->context);
    return true;
}
void box(int x, int y, int w, int h, bool ink)
{
    host->box(host->context, x, y, w, h, ink);
}
void text(int x, int y, int scale, const char *s, bool ink)
{
    host->text(host->context, x, y, scale, s, ink);
}
void ui_keys(const char *plus, const char *minus, const char *a, const char *b)
{
    if (set_keys) set_keys(host->context, plus, minus, a, b);
}
void ui_frame(int pulls)
{
    (void)pulls;
    host->submit_frame(host->context);
}
void ui_close(void)
{
    host->frame_idle(host->context);
}
