// Derived from apps/rocks/rocks.c; original game rules and credits, GPL-2.0.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
enum { BTN_PLUS=0,BTN_MINUS=1,BTN_A=2,BTN_B=3 };
#define W 960
#define H 672
#define TOP 48
#define PH (H - TOP)  // play field: W x PH, y = 0 is the row under the top bar
#define TICK_MS 40
#define DT (TICK_MS / 1000.0f)
#define TURN 2.4f     // rad/s while MENU +/- is held
#define THRUST 360.0f // px/s^2
#define VMAX 360.0f   // px/s
#define DRAG 0.5f     // fraction of speed kept per second while coasting
#define SHOT_V 640.0f // px/s, on top of the ship's speed
#define SHOT_LIFE 25  // ticks (1 s)
#define SHOT_GAP 6    // ticks between shots while B is held
#define SHIELD 75     // ticks of invulnerability after a (re)spawn
#define RESPAWN 38    // ticks without a ship after a crash
#define KNOB_DIR 1    // flip to -1 if turning the knob clockwise turns the ship the wrong way
#define NV 10         // corners per rock
#define MAXR 64
#define MAXB 8

typedef struct { float x, y, vx, vy; int size; uint8_t r[NV]; } rock_t; // size 0 = no rock
typedef struct { float x, y, vx, vy; int life; } shot_t;
static const float RAD[] = {0, 18, 36, 64}; // rock radius by size
static const int PTS[] = {0, 100, 50, 20};  // score by size

typedef struct {
    float x, y, vx, vy, a; // ship; a = heading, 0 = up, clockwise
    int gone, shield;      // ticks until the ship is back; ticks of invulnerability left
    bool thrust;
    rock_t rk[MAXR];
    shot_t sh[MAXB];
    int score, lives, over, t; // over: true once the last ship crashed
} rocks_game_t;
static rocks_game_t g;
static int ref, cool;
static float frand(float lo, float hi) { return lo + (hi - lo) * (module_random() % 10000) / 10000.0f; }
static float wrap(float v, float span) { return v - span * floorf(v / span); }
static float near(float d, float span) { return wrap(d + span / 2, span) - span / 2; } // shortest way round
static bool hit(float x0, float y0, float x1, float y1, float r)
{
    float dx = near(x1 - x0, W), dy = near(y1 - y0, PH);
    return dx * dx + dy * dy < r * r;
}

static void add_rock(float x, float y, int size, float v)
{
    for (int i = 0; i < MAXR; i++) {
        rock_t *r = &g.rk[i];
        if (r->size) continue;
        float q = frand(0, 2 * (float)M_PI);
        *r = (rock_t){.x = x, .y = y, .vx = v * cosf(q), .vy = v * sinf(q), .size = size};
        for (int k = 0; k < NV; k++) r->r[k] = frand(170, 255);
        return;
    }
}

static void wave(int level)
{
    for (int n = level < 6 ? 3 + level : 8; n--;) {
        float x, y;
        do x = frand(0, W), y = frand(0, PH); while (hit(x, y, g.x, g.y, 260));
        add_rock(x, y, 3, frand(40, 70 + 8 * level));
    }
}

static void spawn(void)
{
    g.x = W / 2, g.y = PH / 2, g.vx = g.vy = 0, g.a = 0, g.gone = 0, g.shield = SHIELD;
}

static bool rocks_tick(uint32_t held,bool fire,int a)
{
g.t++;
// ship
if (a >= 0 && ref >= 0 && fabsf(near(a - ref, 4096)) > 24) ref = a; // moved: the knob steers
if (a < 0) ref = -1;
else if (ref < 0) ref = a;
if (!g.gone) {
    g.a += ((held >> BTN_MINUS & 1) - (held >> BTN_PLUS & 1)) * TURN * DT;
    if (a >= 0 && ref == a) g.a = KNOB_DIR * a * 2 * (float)M_PI / 4096;
    g.thrust = held >> BTN_A & 1;
    if (g.thrust) {
        g.vx += THRUST * DT * sinf(g.a), g.vy -= THRUST * DT * cosf(g.a);
        float v = hypotf(g.vx, g.vy);
        if (v > VMAX) g.vx *= VMAX / v, g.vy *= VMAX / v;
    } else g.vx *= powf(DRAG, DT), g.vy *= powf(DRAG, DT);
    g.x = wrap(g.x + g.vx * DT, W), g.y = wrap(g.y + g.vy * DT, PH);
    if (g.shield) g.shield--;
    if (cool) cool--;
    if ((fire || (held >> BTN_B & 1)) && !cool)
        for (int i = 0; i < MAXB; i++)
            if (!g.sh[i].life) {
                float sx = sinf(g.a), sy = -cosf(g.a);
                g.sh[i] = (shot_t){g.x + 26 * sx, g.y + 26 * sy, g.vx + SHOT_V * sx, g.vy + SHOT_V * sy,
                                   SHOT_LIFE};
                cool = SHOT_GAP;
                break;
            }
} else if (!--g.gone) spawn();
// shots and rocks
for (int i = 0; i < MAXB; i++) {
    shot_t *s = &g.sh[i];
    if (s->life) s->life--, s->x = wrap(s->x + s->vx * DT, W), s->y = wrap(s->y + s->vy * DT, PH);
}
bool any = false;
for (int i = 0; i < MAXR; i++) {
    rock_t *r = &g.rk[i];
    if (!r->size) continue;
    r->x = wrap(r->x + r->vx * DT, W), r->y = wrap(r->y + r->vy * DT, PH);
    for (int j = 0; j < MAXB && r->size; j++) {
        shot_t *s = &g.sh[j];
        if (!s->life || !hit(s->x, s->y, r->x, r->y, RAD[r->size])) continue;
        s->life = 0;
        g.score += PTS[r->size];
        int size = r->size - 1;
        float v = hypotf(r->vx, r->vy) * 1.3f + 15;
        r->size = 0;
        if (size) add_rock(r->x, r->y, size, v), add_rock(r->x, r->y, size, v);
    }
    if (r->size && !g.gone && !g.shield && hit(g.x, g.y, r->x, r->y, RAD[r->size] * 0.85f + 12)) {
        g.gone = RESPAWN, g.thrust = false;
        if (!--g.lives) g.over = 1;
    }
    any |= r->size;
}
return !any && !g.gone;
}
