// Offline reader. Copy checked against the app API, docs/os.md and the apps.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "badge/badge.h"

enum { DIAGRAM_NONE, DIAGRAM_RADAR, DIAGRAM_REFRESH };
// Explicit line breaks keep the copy/code readable without a general text-layout framework.
static const struct {
    const char *label, *summary, *title, *body;
    int diagram;
} topics[] = {
    {"Start here", "Buttons and Home", "Using this guide",
     "Left buttons pick a topic: MENU +\n"
     "moves up; A moves down.\n"
     "\n"
     "Right buttons scroll the answer:\n"
     "MENU - moves up; B moves down.\n"
     "\n"
     "The topic list follows your choice.\n"
     "An answer opens at the top.\n"
     "\n"
     "Hold MENU + and MENU - together for\n"
     "0.4 seconds to return Home.",
     DIAGRAM_NONE},
    {"Stock firmware", "MicroPython or C?", "Why not the original\nMicroPython firmware?",
     "You can use the stock firmware. It\n"
     "already has the event schedule, name\n"
     "badge and built-in art gallery.\n"
     "\n"
     "Badge OS is an optional C firmware\n"
     "built on ESP-IDF, with this\n"
     "launcher, games, utilities and\n"
     "shared ranging.\n"
     "\n"
     "Stock apps use MicroPython and their\n"
     "own hardware APIs. Running them here\n"
     "needs a port or a compatible\n"
     "runtime. They do not run unchanged\n"
     "today.",
     DIAGRAM_NONE},
    {"How Radar works", "Ranges and a map", "How Radar works",
     "Badges announce themselves with\n"
     "ESP-NOW, then use Wi-Fi round-trip\n"
     "timing (FTM) to estimate distance.\n"
     "\n"
     "They share ranges between badges.\n"
     "Radar tries to fit a map to those\n"
     "ranges once it sees 3+ badges.\n"
     "\n"
     "Missing or noisy ranges can leave\n"
     "the layout uncertain. The drawing\n"
     "can rotate or mirror: there is no\n"
     "compass bearing.\n"
     "\n"
     "Settings, Pong and online apps pause\n"
     "ranging. Return Home to restore it.",
     DIAGRAM_RADAR},
    {"Fast refresh", "Speed and ghosts", "How fast refresh works",
     "The driver remembers the old frame.\n"
     "Only pixels that change are driven;\n"
     "unchanged frames need no refresh.\n"
     "\n"
     "The whole frame is still sent and\n"
     "scanned. A small change near the\n"
     "bottom costs the same as one at the\n"
     "top; there is no lower-row speed\n"
     "boost.\n"
     "\n"
     "On most badges (B1), upstream\n"
     "reports about 35 ms to send and a\n"
     "0.24 s pull: about 3.5-4 frames/s at\n"
     "best in an open session.\n"
     "\n"
     "Rare 84 panels have a different\n"
     "driver path, documented at about 1.6\n"
     "frames/s. Upstream marks that\n"
     "hardware path as untested.\n"
     "\n"
     "These are upstream figures, not\n"
     "measurements of your badge. Apps can\n"
     "be slower; drawing and opening a\n"
     "panel session add time.\n"
     "\n"
     "ui_frame waits for the last pull,\n"
     "sends this frame and starts a pull.\n"
     "It returns while the ink changes, so\n"
     "the app can draw the next frame.\n"
     "ui_close waits for completion and\n"
     "powers down the panel session.\n"
     "\n"
     "The short drive can leave ghosts.\n"
     "Ink may look brown or dark red; fast\n"
     "mode does not offer true red.\n"
     "\n"
     "Scene changes use ui_wipe: a\n"
     "black/white cleanup, about 2.8 s on\n"
     "B1. It reduces ghosts but can leave\n"
     "old red. Use Redraw or clear ghosts\n"
     "where offered; wipes cost time.\n"
     "\n"
     "E-paper holds its image when idle.\n"
     "It needs no constant refresh. Apps\n"
     "close sessions when sitting still.\n"
     "\n"
     "The slow full-color clean (~35 s) is\n"
     "not used here. Let updates finish\n"
     "before reset or unplugging. Use the\n"
     "Home chord to exit through the OS.",

     DIAGRAM_REFRESH},
    {"Build an app", "Make a C utility", "Make an app or utility",
     "Add apps/demo/demo.c with:\n"
     "\n"
     "#include \"badge/badge.h\"\n"
     "static void run(void) {\n"
     " ui_keys(0,0,0,\"Home\");\n"
     " text(48,TOP+80,3,\"Hi\",true);\n"
     " ui_frame(1);\n"
     " ui_close();\n"
     " while (btn_get(-1) != BTN_B) {}\n"
     "}\n"
     "static void icon(int x,int y) {\n"
     " icon_base(x,y);\n"
     " text(x+40,y+40,8,\"D\",false);\n"
     "}\n"
     "const app_t app_demo = {\n"
     " .name=\"Demo\", .icon=icon, .run=run\n"
     "};\n"
     "\n"
     "In apps/demo/CMakeLists.txt put:\n"
     "\n"
     "idf_component_register(\n"
     " SRCS \"demo.c\" PRIV_REQUIRES os)\n"
     "\n"
     "Add app_demo to the extern list and\n"
     "apps[] in main/apps.c. App folders\n"
     "are discovered automatically.\n"
     "\n"
     "Draw below TOP; label the buttons.\n"
     "Returning goes Home. Read\n"
     "apps/README.md for the full contract\n"
     "and Info metadata. Drawing tasks\n"
     "need at least 6 KB of stack.\n"
     "\n"
     "Build with ESP-IDF 6.0.2:\n"
     "\n"
     "idf.py build\n"
     "\n"
     "Use the OS display API. Preserve\n"
     "panel safety and USB recovery. If\n"
     "your app takes the radio, restore\n"
     "ranging when done or return Home.",

     DIAGRAM_NONE},
    {"Settings / Wi-Fi", "Set up by phone", "Settings and Wi-Fi",
     "Open Settings to change your name,\n"
     "Badge, Wi-Fi or weather settings.\n"
     "\n"
     "On your phone, scan the QR or join\n"
     "the network shown on the badge. Open\n"
     "http://192.168.4.1 if needed. Saving\n"
     "restarts the badge.\n"
     "\n"
     "The hotspot has no internet. Online\n"
     "apps use your saved 2.4 GHz Wi-Fi\n"
     "network.",
     DIAGRAM_NONE},
    {"Online update", "Update this badge", "Online updating",
     "Update > Online update installs a\n"
     "newer release over saved Wi-Fi.\n"
     "\n"
     "Keep power steady until the badge\n"
     "finishes and restarts.\n"
     "\n"
     "If the new firmware cannot reach\n"
     "Home, it can roll back next boot.",
     DIAGRAM_NONE},
    {"USB update", "Copy to a partner", "Wired updating",
     "Update > Flash another badge copies\n"
     "this badge's firmware to a partner.\n"
     "\n"
     "Use a USB-C data cable. Both badges\n"
     "need batteries; neither powers the\n"
     "other through this connection.\n"
     "\n"
     "On a stock MicroPython target, hold\n"
     "BOOT and tap RESET.\n"
     "\n"
     "Wait for the update to finish.",
     DIAGRAM_NONE},
    {"Power / battery", "Charge and gauge", "Power and battery",
     "USB-C powers and charges the badge.\n"
     "Charging works with the switch off.\n"
     "\n"
     "The battery percentage is a rough\n"
     "voltage estimate. It cannot predict\n"
     "how much run time is left.\n"
     "\n"
     "Radio use can delay fresh readings.\n"
     "USB in the bar shows external power,\n"
     "not the battery's charge level.",
     DIAGRAM_NONE},
    {"Add-ons", "AS5600 knob", "Supported add-ons",
     "An AS5600 knob can move the paddle\n"
     "in Pong and steer in Asteroids. Fit\n"
     "its magnet and use an SAO port.\n"
     "\n"
     "Buttons work without a knob. Other\n"
     "add-ons need software support;\n"
     "sharing a connector is not enough.",
     DIAGRAM_NONE},

};

enum {
    NTOPICS = sizeof topics / sizeof *topics,
    VISIBLE = 4, COLS = 36, RIGHT = 352, EDGE = W - 24,
    BODY_Y = TOP + 104, BODY_END = H - 76, LINE = 24, SCROLL_STEP = 5
};
typedef struct { int topic, first, scroll; } reader_t;

static const char *read_line(const char *p, char line[COLS + 1])
{
    int n = 0;
    while (*p && *p != '\n' && n < COLS) line[n++] = *p++;
    line[n] = 0;
    if (*p == '\n') p++;
    return p;
}

static int diagram_height(int topic)
{
    return topics[topic].diagram == DIAGRAM_RADAR ? 288 : topics[topic].diagram == DIAGRAM_REFRESH ? 288 : 0;
}

static int content_height(int topic)
{
    int lines = 1;
    for (const char *p = topics[topic].body; *p; p++) lines += *p == '\n';
    return diagram_height(topic) + lines * LINE;
}

static int max_scroll(int topic)
{
    int extra = content_height(topic) - (BODY_END - BODY_Y);
    return extra > 0 ? (extra + LINE - 1) / LINE : 0;
}

// Diagrams share the answer's clipped viewport, so scrolling cannot overwrite either pane or the top bar.
static void diagram_box(int x, int y, int w, int h, bool ink)
{
    int x1 = x + w, y1 = y + h;
    if (x < RIGHT) x = RIGHT;
    if (y < BODY_Y) y = BODY_Y;
    if (x1 > EDGE) x1 = EDGE;
    if (y1 > BODY_END) y1 = BODY_END;
    if (x1 > x && y1 > y) box(x, y, x1 - x, y1 - y, ink);
}

static void diagram_text(int x, int y, const char *s)
{
    if (y >= BODY_Y && y + 16 <= BODY_END) text(x, y, 2, s, true);
}

static void diagram_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0, n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (!n) return;
    for (int i = 0; i <= n; i++) diagram_box(x0 + dx * i / n, y0 + dy * i / n, 2, 2, true);
}

static void draw_diagram(int topic, int y)
{
    if (topics[topic].diagram == DIAGRAM_RADAR) {
        // A 3-4-5 triangle: illustrative ranges, not live measurements or a compass.
        diagram_text(RIGHT, y, "Example: shared ranges");
        int ax = RIGHT + 104, ay = y + 228, bx = ax + 240, cy = ay - 180;
        diagram_line(ax, ay, bx, ay);
        diagram_line(ax, ay, ax, cy);
        diagram_line(ax, cy, bx, ay);
        diagram_box(ax - 5, ay - 5, 12, 12, true);
        diagram_box(bx - 5, ay - 5, 12, 12, true);
        diagram_box(ax - 5, cy - 5, 12, 12, true);
        diagram_text(RIGHT + 24, ay - 8, "You");
        diagram_text(bx + 16, ay - 8, "Badge B");
        diagram_text(ax + 16, cy - 8, "Badge C");
        diagram_text(RIGHT + 24, y + 132, "3 m");
        diagram_text(ax + 96, ay + 16, "4 m");
        diagram_text(ax + 144, y + 124, "5 m");
    } else if (topics[topic].diagram == DIAGRAM_REFRESH) {
        const char *label[] = {"OLD", "NEW", "DRIVE"};
        const int mask[] = {1 << 4, (1 << 4) | (1 << 5), 1 << 5};
        for (int k = 0; k < 3; k++) {
            int x = RIGHT + 16 + k * 184;
            diagram_text(x, y, label[k]);
            for (int i = 0; i < 9; i++) {
                int cx = x + i % 3 * 28, cy = y + 32 + i / 3 * 28;
                diagram_box(cx, cy, 27, 27, true);
                if (!(mask[k] & (1 << i))) diagram_box(cx + 1, cy + 1, 25, 25, false);
            }
            if (k < 2) diagram_text(x + 128, y + 64, ">");
        }
        diagram_text(RIGHT, y + 136, "Only the extra pixel is driven.");
        diagram_text(RIGHT, y + 160, "Dots: scan. Bar: changed pixels.");
        int x = RIGHT + 24, top = y + 188;
        diagram_box(x, top, 128, 72, true);
        diagram_box(x + 2, top + 2, 124, 68, false);
        for (int r = 4; r < 68; r += 6)
            for (int c = 6; c < 124; c += 8) diagram_box(x + c, top + r, 1, 1, true);
        diagram_box(x + 8, top + 48, 32, 3, true);
        diagram_text(RIGHT + 176, y + 196, "Whole panel");
        diagram_text(RIGHT + 176, y + 220, "~35 ms send (B1)");
        diagram_text(RIGHT + 176, y + 244, "~0.24 s pull (B1)");
    }
}

static void draw_reader(const reader_t *r)
{
    box(0, TOP, W, H - TOP, false);
    text(16, TOP + 16, 3, "Topics", true);
    if (r->first > 0) text(208, TOP + 20, 2, "^ More", true);
    box(336, TOP + 16, 2, H - TOP - 84, true);
    for (int i = r->first; i < NTOPICS && i < r->first + VISIBLE; i++) {
        int y = TOP + 60 + (i - r->first) * 122;
        bool selected = i == r->topic;
        box(16, y, 304, 112, selected);
        outline(16, y, 304, 112, 2);
        text(30, y + 14, 2, topics[i].label, !selected);
        const char *p = topics[i].summary;
        for (int sy = y + 48; *p; sy += 20) {
            char line[COLS + 1];
            p = read_line(p, line);
            text(30, sy, 2, line, !selected);
        }
    }
    const char *p = topics[r->topic].title;
    for (int y = TOP + 24; *p; y += 32) {
        char line[COLS + 1];
        p = read_line(p, line);
        text(RIGHT, y, 3, line, true);
    }
    int y = BODY_Y - r->scroll * LINE;
    draw_diagram(r->topic, y);
    y += diagram_height(r->topic);
    p = topics[r->topic].body;
    for (; *p; y += LINE) {
        char line[COLS + 1];
        p = read_line(p, line);
        if (y >= BODY_Y && y + 16 <= BODY_END) text(RIGHT, y, 2, line, true);
    }
    int max = max_scroll(r->topic);
    if (max) {
        int view = BODY_END - BODY_Y, height = view * view / content_height(r->topic);
        if (height < 20) height = 20;
        box(W - 12, BODY_Y, 2, view, true);
        box(W - 16, BODY_Y + (view - height) * r->scroll / max, 10, height, true);
    }
    char count[48];
    int last = r->first + VISIBLE < NTOPICS ? r->first + VISIBLE : NTOPICS;
    if (last < NTOPICS) snprintf(count, sizeof count, "v More below %d-%d/%d", r->first + 1, last, NTOPICS);
    else snprintf(count, sizeof count, "%d-%d of %d topics", r->first + 1, last, NTOPICS);
    text(16, H - 56, 2, count, true);
    text(RIGHT, H - 56, 2, r->scroll < max ? "More below: B" : r->scroll ? "End: MENU - goes up" : "Answer fits on screen", true);
    text(16, H - 24, 2, "Hold MENU + and MENU -: Home", true);
    ui_frame(1);
    ui_close();
}

static bool reader_step(reader_t *r, int b)
{
    int topic = r->topic, scroll = r->scroll;
    if (b == BTN_PLUS && r->topic > 0) r->topic--;
    if (b == BTN_A && r->topic < NTOPICS - 1) r->topic++;
    if (r->topic != topic) r->scroll = 0;
    else if (b == BTN_MINUS) r->scroll = r->scroll > SCROLL_STEP ? r->scroll - SCROLL_STEP : 0;
    else if (b == BTN_B) {
        r->scroll += SCROLL_STEP;
        if (r->scroll > max_scroll(r->topic)) r->scroll = max_scroll(r->topic);
    }
    if (r->topic < r->first) r->first = r->topic;
    if (r->topic >= r->first + VISIBLE) r->first = r->topic - VISIBLE + 1;
    return r->topic != topic || r->scroll != scroll;
}

static void run(void)
{
    ui_keys("Topic up", "Read up", "Topic down", "Read down");
    reader_t r = {0};
    draw_reader(&r);
    for (;;) {
        if (reader_step(&r, btn_get(-1))) draw_reader(&r);
    }
}

static void icon(int x, int y)
{
    icon_base(x, y);
    text(x + 40, y + 40, 12, "?", false);
}

const app_t app_faq = {.name = "FAQ", .icon = icon, .run = run};
const app_info_t app_faq_info = {
    .info = "Browse offline help. Left buttons choose a topic; right buttons scroll its answer. Hold both top MENU buttons for Home.",
    .credit = "Code: apps/faq/faq.c", .launch_info = true,
};
