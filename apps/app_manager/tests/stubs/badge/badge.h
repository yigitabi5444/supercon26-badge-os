#pragma once
#include <stdbool.h>
#define W 960
#define H 672
#define TOP 48
#define ICON 176
enum { BTN_PLUS, BTN_MINUS, BTN_A, BTN_B };
typedef struct { const char *name; void (*icon)(int,int); void (*run)(void); } app_t;
#if CONFIG_BADGE_APP_MANAGER_HELP
typedef struct { const char *info,*credit; bool launch_info; } app_info_t;
#endif
void box(int,int,int,int,bool);
void text(int,int,int,const char*,bool);
void ui_keys(const char*,const char*,const char*,const char*);
void ui_frame(int);
void ui_close(void);
int btn_get(int);
void btn_home_prompt(bool);
bool btn_home_requested(void);
int btn_held(void);
void btn_flush(void);
void icon_base(int,int);
bool wifi_connected(void);
