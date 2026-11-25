#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { AM_COUNT = 7, AM_ICON_BYTES = 128, AM_SHOT_BYTES = 2240 };
typedef struct {
    const char *id, *name;
    uint32_t package_bytes_hint; // Curated ELF estimate; verified receipt sizes take priority.
    const char *description, *credit;
    int icon, shot;
    bool native;
} am_entry_t;
typedef struct {
    bool installed;
    const char *version,*package_version; // Independent of the OS version.
    uint32_t size,package_size;
    bool update_available;
    bool disabled, package_available, fetch_available;
} am_local_t;
typedef struct {
    unsigned selected, info_scroll, list_scroll, action;
    bool menu;
    const char *notice;
} am_view_t;
enum am_action { AM_INSTALL, AM_OPEN, AM_UPDATE, AM_UNINSTALL, AM_DETAILS, AM_FETCH };
typedef struct { enum am_action kind; const char *label; bool enabled; } am_action_t;

extern const am_entry_t am_catalogue[AM_COUNT];
extern const uint8_t am_icons[AM_COUNT][AM_ICON_BYTES];
extern const uint8_t am_shots[2][AM_SHOT_BYTES];
extern const uint8_t am_native_shots[1][AM_SHOT_BYTES];
void am_home_reset(void);
void am_home_add(const char *name);
bool am_has_home(unsigned id);
bool am_downloadable(unsigned id,const am_local_t *);
bool am_is_builtin(unsigned id,const am_local_t *);
unsigned am_order(const am_local_t local[AM_COUNT], unsigned order[AM_COUNT]);
unsigned am_group(unsigned,const am_local_t *);
void am_next(am_view_t *view, const am_local_t local[AM_COUNT]);
size_t am_actions(unsigned selected, const am_local_t *local, bool store_ready, am_action_t out[4]);
void app_manager_ui(void);
void am_manager_open_installed(unsigned); // Home shortcut: same authenticated Open and quiescent shutdown.
