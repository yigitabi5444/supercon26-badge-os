// Derived from Badge OS apps/aliens/aliens.c at aab00d5; GPL-2.0, see repository LICENSE.
// Original integer-only game state and tick rules; no SDK, RTOS, storage or libc imports.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define W 960
#define H 672
#define TOP 48
#define TICK_MS 50
#define COLS 8
#define ROWS 5
#define NA (COLS * ROWS)
#define S 4             // sprite pixel size: 12 x 8 aliens are 48 x 32 px
#define AW (12 * S)
#define AH (8 * S)
#define CW 80           // formation cell
#define RH 56
#define DX 16           // a march step
#define DY 24           // a step down at the edge
#define GW (16 * S)     // the gun, 64 x 32
#define GH (8 * S)
#define PY (H - 64)     // gun top
#define GUN_V 12        // px per tick while A / B is held
#define SHOT_V 32       // player shot, px per tick
#define BOMB_V 12       // alien shots, px per tick
#define NB 3            // alien shots in the air at most
#define BOOM_TICKS 5    // a hit alien shows a burst this long (about one frame at ~3.5-4 frames/s)
#define DEAD_TICKS 30   // a hit gun freezes the game this long
#define KNOB_DIR 1      // flip to -1 if turning the knob clockwise moves the gun the wrong way


typedef struct {
    uint8_t alive[NA], boom[NA];
    int fx, fy, dir, frame, march_ms; // formation top left, march direction, sprite frame, time to the next step
    int px, sx, sy;                   // gun x; player shot (sy < 0: none)
    int bx[NB], by[NB];               // alien shots (by < 0: none)
    int score, lives, wave, dead, over;
} aliens_game_t;

static void aliens_fill(void *out, unsigned char value, unsigned n)
{
    volatile unsigned char *p = out; // freestanding: do not synthesize a libc memset call
    while (n--) *p++ = value;
}

static int aliens_kind(int row) { return row == 0 ? 0 : row < 3 ? 1 : 2; }

static void aliens_new_wave(aliens_game_t *s)
{
    s->wave++;
    aliens_fill(s->alive, 1, NA);
    aliens_fill(s->boom, 0, NA);
    s->fx = (W - (COLS - 1) * CW - AW) / 2;
    s->fy = TOP + 56 + (s->wave - 1 < 5 ? s->wave - 1 : 5) * DY; // each wave starts a bit lower
    s->dir = 1;
    s->march_ms = 0;
    s->sy = -1;
    for (int k = 0; k < NB; k++) s->by[k] = -1;
}

static bool aliens_hits(int x, int y, int w, int h, int rx, int ry, int rw, int rh)
{
    return x < rx + rw && rx < x + w && y < ry + rh && ry < y + h;
}

static void aliens_tick(aliens_game_t *s, int fire, int move, uint32_t (*random32)(void *), void *context) // one TICK_MS step of the game
{
    if (s->dead) { // the gun was hit: everything waits
        if (--s->dead == 0) {
            if (s->lives == 0) s->over = 1;
            for (int k = 0; k < NB; k++) s->by[k] = -1;
        }
        return;
    }
    s->px += move;
    if (s->px < 0) s->px = 0;
    if (s->px > W - GW) s->px = W - GW;
    if (fire && s->sy < 0) s->sx = s->px + GW / 2, s->sy = PY - 24;

    int n = 0, lo = COLS, hi = -1, bottom = 0;
    for (int i = 0; i < NA; i++) {
        if (s->boom[i]) s->boom[i]--;
        if (!s->alive[i]) continue;
        n++;
        if (i % COLS < lo) lo = i % COLS;
        if (i % COLS > hi) hi = i % COLS;
        bottom = s->fy + i / COLS * RH + AH;
    }
    if (n == 0) { // wave cleared, once the last burst has shown
        bool booms = false;
        for (int i = 0; i < NA; i++) booms |= s->boom[i];
        if (!booms) aliens_new_wave(s);
        return;
    }

    // the formation marches: fewer aliens and later waves march faster
    s->march_ms -= TICK_MS;
    if (s->march_ms <= 0) {
        s->march_ms = (60 + n * 22) * 6 / (5 + s->wave);
        s->frame ^= 1;
        int nx = s->fx + s->dir * DX;
        if (nx + lo * CW < 8 || nx + hi * CW + AW > W - 8) s->fy += DY, s->dir = -s->dir, bottom += DY;
        else s->fx = nx;
        if (bottom >= PY) s->lives = 0, s->dead = DEAD_TICKS; // they landed
    }

    if (s->sy >= 0) { // the player's shot
        s->sy -= SHOT_V;
        if (s->sy < TOP + 40) s->sy = -1;
        for (int i = 0; i < NA && s->sy >= 0; i++)
            if (s->alive[i] && aliens_hits(s->sx - 2, s->sy, 4, 24 + SHOT_V, s->fx + i % COLS * CW, s->fy + i / COLS * RH, AW, AH)) {
                s->alive[i] = 0;
                s->boom[i] = BOOM_TICKS;
                s->score += 30 - 10 * aliens_kind(i / COLS);
                s->sy = -1;
            }
    }

    for (int k = 0; k < NB; k++) { // alien shots fall; a free slot may fire from the lowest alien of a random column
        if (s->by[k] >= 0) {
            s->by[k] += BOMB_V;
            if (s->by[k] > H - 44) s->by[k] = -1;
            else if (aliens_hits(s->bx[k] - 4, s->by[k], 8, 24, s->px + 4, PY + 8, GW - 8, GH - 8)) {
                s->by[k] = -1;
                s->lives--;
                s->dead = DEAD_TICKS;
            }
        } else if ((int)(random32(context) % 100) < 2 + s->wave) {
            int c = random32(context) % COLS;
            for (int r = ROWS - 1; r >= 0; r--)
                if (s->alive[r * COLS + c]) {
                    s->bx[k] = s->fx + c * CW + AW / 2;
                    s->by[k] = s->fy + r * RH + AH;
                    break;
                }
        }
    }
}
