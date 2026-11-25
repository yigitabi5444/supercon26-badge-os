#pragma once
#include <stdbool.h>
#include <stdint.h>
#define W 960
#define H 672
#define TOP 48
#define ICON 176
#define PLANE (W*H/8)
#define LEDS 8
typedef struct { const char *name;void (*icon)(int,int);void (*run)(void); } app_t;
enum { BTN_PLUS,BTN_MINUS,BTN_A,BTN_B };
extern uint8_t fb[PLANE];
void box(int,int,int,int,bool);
void text(int,int,int,const char*,bool);
void icon_base(int,int);
void ui_frame(int);
void ui_close(void);
void ui_wipe(void);
void ui_keys(const char *,const char *,const char *,const char *);
int btn_get(int);
int btn_held(void);
void btn_flush(void);
int knob_angle(void);
bool wifi_connect(int);
bool wifi_connected(void);
void wifi_off(void);
void ranging_start(void);
int cfg_get(const char *,char *,int);
bool cfg_set(const char *,const char *);
void leds(const uint8_t [LEDS][3]);
