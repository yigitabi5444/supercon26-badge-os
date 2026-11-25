// Display: the 9.7" black/white/red e-paper, 960 x 672, behind a 1-bit frame buffer.
//
// Draw into fb (badge/draw.h has box, outline, text), then ui_frame() shows it.
// - Rows 0..TOP-1 are the OS top bar on every screen: ui_frame draws it over fb. Keep your picture below TOP.
// - A frame is one calm whole-screen update: only the pixels that changed move, nothing flashes. ~3.5-4 frames/s at
//   best on most badges (E2969JS0B1 panel: a ~0.24 s pull plus ~35 ms to send), ~1.6 on the E2969JS084.
// - ui_frame blocks only until the previous frame's pull is done, sends yours and returns while the panel pulls it:
//   draw the next frame meanwhile. Run game logic on its own clock (apps/snake), not one step per frame.
// - No change since the last frame = no refresh, so a loop that calls ui_frame costs nothing while nothing moves.
// - Ink comes out dark red; erased ink leaves a light ghost. ui_wipe() shakes the ghosts off at scene changes.
// - Call ui_close() when the screen will sit still for a while: it waits for the last pull and powers the panel down.
// - Safe from any task (one lock): an app may draw on a task of its own.
#pragma once
#include <stdint.h>

#define W 960
#define H 672
#define PLANE (W * H / 8) // MONO_HLSB, MSB = leftmost pixel, 1 = black
#define TOP 48            // rows 0..TOP-1 are the OS top bar on every screen: ui_frame draws over them

extern uint8_t fb[PLANE]; // what the next ui_frame() shows

void ui_frame(int pulls); // show fb (pulls is ignored)
void ui_close(void);
// What each button does now, shown beside it in the top bar (two buttons in each top corner: MENU + above A on the
// left, MENU - above B on the right). Up to ~16 characters each; NULL or "" = no label. Set it again when it changes;
// it shows from the next ui_frame. Every app starts with no labels.
void ui_keys(const char *plus, const char *minus, const char *a, const char *b);
// Runtime Info retains the latest app frame and labels; background frames/wipes are suppressed.
// A free app button may call this; apps that wipe from a second task must serialize their drawer.
void ui_info(void);
void ui_info_until(const volatile bool *dismiss); // Settings save can dismiss the card
void ui_wipe(void); // scene change: an all-black then an all-white frame shake the ghosts off (~2.8 s), fb all white
