// The home screen's apps, in this order, 8 to a page. Each one lives in apps/<name>/ (how to write one: apps/README.md).
#include "home.h"
#include <stddef.h>

extern const app_t app_badge, app_clock, app_update, app_pong, app_snake, app_life, app_aliens, app_blocks, app_rocks,
    app_radar, app_source, app_hackaday, app_video, app_weather, app_settings, app_doom, app_codex_board,
    app_faq, app_messaging;

const app_t *const apps[] = {
    &app_badge, // the name tag first: what a badge is for
    &app_clock,
    &app_update,
    &app_pong,
    &app_snake,
    &app_life,
    &app_aliens,
    &app_blocks,
    &app_rocks,
    &app_radar,
    &app_source,
    &app_hackaday,
    &app_video,
    &app_weather,
    &app_settings,
    &app_doom,
    &app_codex_board,
    &app_faq,
    &app_messaging,
};
const int napps = sizeof apps / sizeof *apps;

// Optional metadata follows the same launcher order; unmodified upstream apps need none.
extern const app_info_t app_badge_info, app_clock_info, app_update_info, app_pong_info,
    app_snake_info, app_life_info, app_aliens_info, app_blocks_info,
    app_rocks_info, app_radar_info, app_source_info, app_hackaday_info,
    app_video_info, app_weather_info, app_settings_info, app_faq_info,
    app_messaging_info;
const app_info_t *const app_infos[] = {
    &app_badge_info,
    &app_clock_info,
    &app_update_info,
    &app_pong_info,
    &app_snake_info,
    &app_life_info,
    &app_aliens_info,
    &app_blocks_info,
    &app_rocks_info,
    &app_radar_info,
    &app_source_info,
    &app_hackaday_info,
    &app_video_info,
    &app_weather_info,
    &app_settings_info,
    NULL,
    NULL,
    &app_faq_info,
    &app_messaging_info,
};
_Static_assert(sizeof apps / sizeof *apps == sizeof app_infos / sizeof *app_infos, "App help order must match apps");
