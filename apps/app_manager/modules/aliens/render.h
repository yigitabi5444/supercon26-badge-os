// Derived from unchanged Badge OS Aliens sprites/drawing; GPL-2.0, see repository LICENSE.
#pragma once
// Sprites: '#' is ink. Two frames each, swapped on every march step.
static const char kinds[3][2][8][13] __attribute__((aligned(4))) = {
    { // horned, top row, 30 points
        { "#..........#", ".#..####..#.", "..########..", ".##.####.##.", "############", "..##....##..",
          ".##..##..##.", "##........##" },
        { "..#......#..", "..#.####.#..", "..########..", ".##.####.##.", "############", "..##....##..",
          "...##..##...", "..##....##.." },
    },
    { // jelly, rows 2-3, 20 points
        { "....####....", "..##.##.##..", ".##########.", "############", "#.#.#..#.#.#", "#.#.#..#.#.#",
          ".#..#..#..#.", "#...#..#...#" },
        { "....####....", "..##.##.##..", ".##########.", "############", ".#.#.##.#.#.", ".#.#.##.#.#.",
          "..#..##..#..", ".#...##...#." },
    },
    { // eye, rows 4-5, 10 points
        { "....####....", "..########..", ".###....###.", "###..##..###", ".###....###.", "..########..",
          ".#.#.##.#.#.", "#.#......#.#" },
        { "....####....", "..########..", ".###....###.", "###.##...###", ".###....###.", "..########..",
          "..#.#..#.#..", ".#.#....#.#." },
    },
};
static const char gun[8][17] __attribute__((aligned(4))) = { "......####......", ".....######.....", "....########....", ".##############.",
                                    "################", "################", "####.######.####", "###..........###" };
static const char burst[8][13] __attribute__((aligned(4))) = { "#..#....#..#", ".#..#..#..#.", "..#......#..", "##........##",
                                      "..#......#..", ".#..#..#..#.", "#..#....#..#", "....#..#...." };

// Module-local rendering adapter. Caller owns the host frame lease.
static void module_clear(void);
static void module_box(int x, int y, int w, int h, int ink);
static void module_outline(int x, int y, int w, int h, int thickness, int ink);
static void module_text(int x, int y, int scale, const char *s, int ink);

static void sprite(const char *rows, int stride, int x, int y)
{
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < stride && rows[r * stride + c]; c++)
            if (rows[r * stride + c] == '#') module_box(x + c * S, y + r * S, S, S, true);
}

// Freestanding integer formatting, with no libc imports or pointers into string-pool padding.
static const char score_prefix[] __attribute__((aligned(4))) = "SCORE ";
static const char wave_prefix[] __attribute__((aligned(4))) = "WAVE ";
static const char over_prefix[] __attribute__((aligned(4))) = " GAME OVER  SCORE ";
static const char empty_suffix[] __attribute__((aligned(4))) = "";
static const char space_suffix[] __attribute__((aligned(4))) = " ";
static const char revision[] __attribute__((aligned(4))) = "v" ALIENS_APP_VERSION;
static int number_text(char *out, const char *prefix, int value, const char *suffix)
{
    int n = 0, count = 0;
    char digits[10];
    while (*prefix) out[n++] = *prefix++;
    uint32_t u = (uint32_t)value;
    if (value < 0) out[n++] = '-', u = 0u - u;
    do { digits[count++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (count) out[n++] = digits[--count];
    while (*suffix) out[n++] = *suffix++;
    out[n] = 0;
    return n;
}

static int aliens_draw(const aliens_game_t *s) // g -> fb; returns nonzero once the game-over box is in it
{
    module_clear();
    char t[32];
    number_text(t, score_prefix, s->score, empty_suffix);
    module_text(16, TOP + 8, 3, t, true);
    number_text(t, wave_prefix, s->wave, empty_suffix);
    module_text(W / 2 - 72, TOP + 8, 3, t, true);
    for (int k = 0; k < s->lives; k++) // lives left, the gun in play included
        for (int r = 0; r < 8; r++)
            for (int c = 0; gun[r][c]; c++)
                if (gun[r][c] == '#') module_box(W - 16 - (k + 1) * 40 + c * 2, TOP + 12 + r * 2, 2, 2, true);
    for (int i = 0; i < NA; i++) {
        int x = s->fx + i % COLS * CW, y = s->fy + i / COLS * RH;
        if (s->alive[i]) sprite(&kinds[aliens_kind(i / COLS)][s->frame][0][0], 13, x, y);
        else if (s->boom[i]) sprite(&burst[0][0], 13, x, y);
    }
    if (s->sy >= 0) module_box(s->sx - 2, s->sy, 4, 24, true);
    for (int k = 0; k < NB; k++)
        if (s->by[k] >= 0) { // a zigzag bolt
            module_box(s->bx[k] - 4, s->by[k], 4, 8, true);
            module_box(s->bx[k], s->by[k] + 8, 4, 8, true);
            module_box(s->bx[k] - 4, s->by[k] + 16, 4, 8, true);
        }
    if (s->dead) sprite(&burst[0][0], 13, s->px + 8, PY);
    else sprite(&gun[0][0], 17, s->px, PY);
    module_box(0, H - 20, W, 4, true); // the ground
    module_text(W - 64, H - 16, 1, revision, true);
    if (s->over) {
        int n = number_text(t, over_prefix, s->score, space_suffix), x = (W - n * 32) / 2;
        module_box(x - 8, H / 2 - 32, n * 32 + 16, 64, false);
        module_outline(x - 8, H / 2 - 32, n * 32 + 16, 64, 4, true);
        module_text(x, H / 2 - 16, 4, t, true);
    }
    return s->over;
}
