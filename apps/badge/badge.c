// Badge app: saved picture, subject/body (or legacy text), then the default name tag.
#include <string.h>
#include "badge/badge.h"
#include "badge/font8x8_basic.h" // same font as text(); a second static copy is 1 KB

static bool px(int ch, int r, int c) // glyph pixel, false outside the 8x8 cell
{
    return r >= 0 && r < 8 && c >= 0 && c < 8 && (font8x8_basic[ch & 0x7F][r] >> c & 1);
}

// text() with smoothed edges: each font pixel is an s x s square whose corners are cut where the outline turns
// outward and filled where it turns inward, so diagonals come out as 45 degree strokes instead of stairs.
static void big(int x, int y, int s, const char *str, bool ink)
{
    for (; *str; str++, x += 8 * s)
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                bool on = px(*str, r, c);
                for (int v = 0; v < s; v++)
                    for (int u = 0; u < s; u++) {
                        int dy = v < s / 2 ? -1 : 1, dx = u < s / 2 ? -1 : 1; // the corner this pixel is near
                        int du = dx < 0 ? u : s - 1 - u, dv = dy < 0 ? v : s - 1 - v; // distance to it
                        bool a = px(*str, r, c + dx), b = px(*str, r + dy, c), corner = du + dv < s / 2;
                        bool set = on ? !(corner && !a && !b && !px(*str, r + dy, c + dx)) : corner && a && b;
                        int X = x + c * s + u, Y = y + r * s + v;
                        if (set && X >= 0 && X < W && Y >= TOP && Y < H) {
                            if (ink) fb[Y * (W / 8) + X / 8] |= 0x80 >> (X & 7);
                            else fb[Y * (W / 8) + X / 8] &= ~(0x80 >> (X & 7));
                        }
                    }
            }
}

static void centred(int y, int s, const char *str, bool ink) { big((W - (int)strlen(str) * 8 * s) / 2, y, s, str, ink); }

// Keep these bounds in sync with the Badge fields in app_settings.c.
#define SUBJECT_MAX 256
#define BODY_MAX 1000

// The font is basic ASCII. Preserve line breaks, make tabs spaces, and show unsupported legacy glyphs as '?'.
static bool normalise(char *s)
{
    char *out = s;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '\r') { if (p[1] == '\n') p++; c = '\n'; }
        else if (c == '\t') c = ' ';
        else if ((c < 0x20 && c != '\n') || c > 0x7e) {
            c = '?';
            while ((p[1] & 0xc0) == 0x80) p++; // one placeholder per UTF-8 character
        }
        *out++ = c;
    }
    while (out > s && (out[-1] == ' ' || out[-1] == '\n')) out--;
    *out = 0;
    char *start = s;
    while (*start == ' ' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    return *s != 0;
}

// Wrap at spaces, split long words, and keep explicit newlines. A line never exceeds cols characters.
static const char *next_line(const char *p, int cols, int *len)
{
    int n = 0, space = -1;
    while (p[n] && p[n] != '\n' && n < cols) {
        if (p[n] == ' ') space = n;
        n++;
    }
    int take = n;
    if (p[n] && p[n] != '\n' && p[n] != ' ' && space > 0) take = n = space;
    while (p[n] == ' ') n++;
    if (p[n] == '\n') n++;
    while (take && p[take - 1] == ' ') take--;
    *len = take;
    return p[n] ? p + n : NULL;
}

static int line_count(const char *p, int cols)
{
    int lines = 0, len;
    do { lines++; } while ((p = next_line(p, cols, &len)));
    return lines;
}

// Keep comfortable preferred heights; only shrink when wrapped lines need more room.
// Settings' preview uses the same wrapping, sizing and smoothed bitmap glyphs.
static void section(const char *p, int y, int h, int preferred)
{
    int s = preferred, lines;
    while ((lines = line_count(p, (W - 48) / (8 * s))) * 10 * s - 2 * s > h && s > 1) s--;
    int visible = (h + 2 * s) / (10 * s);
    bool overflow = lines > visible;
    if (overflow) lines = visible;
    y += (h - (lines * 10 * s - 2 * s)) / 2;
    for (int i = 0; i < lines; i++, y += 10 * s) {
        char line[W / 8 + 1];
        int len;
        const char *next = next_line(p, (W - 48) / (8 * s), &len);
        memcpy(line, p, len);
        line[len] = 0;
        // Pathological numbers of explicit newlines cannot fit even at 8 px. Signal that on both displays.
        centred(y, s, overflow && i == lines - 1 ? "..." : line, true);
        p = next;
    }
}

static void draw(void)
{
    memset(fb, 0, sizeof fb);
    if (cfg_get_image(fb + TOP * (W / 8))) return;
    char subject[SUBJECT_MAX + 1], body[BODY_MAX + 1], mirror[BODY_MAX + 1];
    cfg_get("badge_subject", subject, sizeof subject);
    cfg_get("badge_body", body, sizeof body);
    if (cfg_get("badge_legacy", mirror, sizeof mirror) >= 0) {
        char legacy[BODY_MAX + 1];
        cfg_get("badge_text", legacy, sizeof legacy);
        if (strcmp(legacy, mirror)) { // v0.8 edited or cleared text while the split fields were unknown to it
            *subject = 0;
            strcpy(body, legacy);
        }
    } else if (!*subject && !*body) cfg_get("badge_text", body, sizeof body); // no migration on read
    bool has_subject = normalise(subject), has_body = normalise(body);
    if (has_subject || has_body) {
        int y = TOP + 24, h = H - TOP - 48;
        if (has_subject && has_body) {
            int sh = (h - 24) / 3;
            section(subject, y, sh, 10);
            section(body, y + sh + 24, h - sh - 24, 6);
        } else section(has_subject ? subject : body, y, h, 12);
        return;
    }
    char id[5], name[11] = "Badge ";
    badge_id(id);
    strcat(name, id);
    box(24, TOP + 16, W - 48, 228, true); // the red band: HELLO / my name is
    centred(TOP + 40, 13, "HELLO", false);
    centred(TOP + 160, 5, "my name is", false);
    centred(TOP + 358, 11, name, true);
    box(24, H - 64, W - 48, 48, true);
}

static void run(void)
{
    ui_keys("Info", NULL, "Redraw", "Home");
    for (;;) {
        ui_wipe(); // a still screen: start it without ghosts
        draw();
        ui_frame(1);
        ui_close();
        int b;
        while ((b = btn_get(-1)) != BTN_A && b != BTN_B)
            if (b == BTN_PLUS) ui_info();
        if (b == BTN_B) return;
    }
}

static void icon(int x, int y) // a name tag: a white card with a black band and a name line
{
    icon_base(x, y);
    box(x + 24, y + 40, 128, 96, false);
    box(x + 24, y + 40, 128, 28, true);
    box(x + 40, y + 96, 96, 10, true);
}

const app_t app_badge = {"Badge", icon, run};
const app_info_t app_badge_info = {
    "Your name tag, custom text or saved picture.",
    "Code: apps/badge/badge.c", false};
