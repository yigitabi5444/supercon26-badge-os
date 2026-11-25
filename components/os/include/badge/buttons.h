// Buttons as printed on the badge, two in each top corner: MENU + above A on the left, MENU - above B on the right.
// Polled every 20 ms; every new press goes into a queue of 8. The press that opened your app has been flushed.
// MENU + and MENU - held together for 0.4 s go home from any app (the OS restarts the badge between frames), so a
// game should not need that chord.
#pragma once
#include <stdbool.h>

enum { BTN_PLUS, BTN_MINUS, BTN_A, BTN_B };
int btn_get(int timeout_ms); // next press, -1 on timeout; timeout -1 waits forever
void btn_flush(void);
int btn_held(void); // buttons held right now: bit (1 << BTN_x) set while pressed, updated every 20 ms

// Opt in only while an app can handle its exit on the app task (Settings staged edits).
// The normal 0.4 s Home chord latches a request instead of restarting. No rendering/NVS in the button task.
// Cleared on every app transition. Physical BOOT/RESET recovery remains unchanged.
void btn_home_prompt(bool enabled);
bool btn_home_requested(void); // consumes a latched request, independent of the normal button queue

// After app I/O is quiescent, atomically stop deferral and consume any pending Home request.
bool btn_home_prompt_end(void);
