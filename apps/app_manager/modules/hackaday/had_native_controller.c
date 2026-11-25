/* Projection of 9b73810 Hackaday reader and feed job selection. */
#include "had_native_controller.h"
#include "had_feed_slice.h"
#include "badge/font8x8_basic.h"
#include "had_format.h"
#include <stdio.h>
#include <string.h>

static int next_image(void);
#include "had_port.inc"
static int next_image(void)
{
    int i = -1, best = 1 << 30, cp = cur_post;
    for (int k = 0; k < nimg; k++) {
        int p = imgs[k].post, cover = posts[p].cover == k;
        int rank = p == cp && cover ? 0 : p == cp && reading ? 1 : cover ? 2 + (p - cp + nposts) % nposts : 100 + k;
        if (!imgs[k].state && rank < best) best = rank, i = k;
    }
    return i;
}
#define MAXL 4000    // laid-out lines of the open post
#define WIPE_EVERY 6 // ponytail: scene changes (posts, page turns) between ghost wipes (2.8 s); tune on the badge
#define X0 32        // text column
#define X1 (W - 24)  // right edge of the text; the scroll bar sits right of it

// How much of s (up to its end or a '\n') goes on a line of cols characters; *next: where the next line starts.
static int cut(const char *s, int cols, const char **next)
{
    int n = 0;
    while (n <= cols && s[n] && s[n] != '\n') n++;
    if (n > cols) {
        int k = cols;
        while (k > 0 && s[k] != ' ') k--;
        n = k ? k : cols; // one long word: hard break
    }
    const char *t = s + n;
    while (*t == ' ') t++;
    *next = t;
    return n;
}

static void line(int x, int y, int scale, const char *s, int n)
{
    char b[128];
    had_format(b, sizeof b, "%.*s", n, s);
    text(x, y, scale, b, true);
}

// Word-wrapped s at x, at most maxl lines of lh px, "..." if cut; returns the y below it.
static int wrap(int x, int y, int scale, int lh, int maxl, const char *s)
{
    int cols = (X1 - x) / (8 * scale);
    for (const char *t; *s && maxl--; y += lh, s = t) {
        int n = cut(s, cols, &t);
        if (!maxl && *t) {
            char b[128];
            had_format(b, sizeof b, "%.*s...", n < cols - 3 ? n : cols - 3, s);
            text(x, y, scale, b, true);
        } else line(x, y, scale, s, n);
        if (*t == '\n') t++;
    }
    return y;
}

static void meta(char *o, int n, int i) // "author, date  i/n"
{
    const post_t *p = &posts[i];
    had_format(o, n - 12, "%s%s%s", p->author, *p->author && *p->date ? ", " : "", p->date);
    int k = strlen(o);
    had_format(o + k, n - k, "  %d/%d", i + 1, nposts);
}

// The open post laid out as lines: text (s, n at scale), an image (img >= 0) or a gap (neither); h px each.
typedef struct {
    const char *s;
    int16_t n, h;
    int8_t scale, img;
} line_t;
static line_t *lines;
static int nl;
static char head[80];

static void add(const char *s, int n, int h, int scale, int img)
{
    if (nl < MAXL) lines[nl++] = (line_t){s, n, h, scale, img};
}

static void layout(int i)
{
    const char *t;
    nl = 0;
    for (const char *s = posts[i].title; *s; s = t) add(s, cut(s, (X1 - X0) / 24, &t), 30, 3, -1);
    meta(head, sizeof head, i);
    add(head, strlen(head), 34, 2, -1);
    for (const char *s = posts[i].body ? posts[i].body : ""; nl < MAXL && *s; s = t) {
        unsigned char c = *s;
        if (c >= 0x80 && c - 0x80 < nimg && (s[1] == '\n' || !s[1])) { // an image
            add(NULL, 0, imgs[c - 0x80].h + 12, 0, c - 0x80);
            t = s + 1 + (s[1] == '\n');
            continue;
        }
        add(s, cut(s, (X1 - X0) / 16, &t), 22, 2, -1);
        if (*t == '\n') t++, add(NULL, 0, 12, 0, -1); // paragraph gap
    }
}

static int page_end(int top) // the first line below the screen when top is the first line on it
{
    int y = TOP + 8;
    while (top < nl && y + lines[top].h <= H) y += lines[top++].h;
    return top;
}

static int page_back(int top) // the top of the page before
{
    for (int y = TOP + 8; top > 0 && y + lines[top - 1].h <= H; y += lines[--top].h) {}
    return top;
}

// Image i at y, centred (featured images are wider than the text column), at most maxh px high.
static void picture(int i, int y, int maxh)
{
    if (i < 0 || i >= nimg || maxh <= 0) return;
    img_t *m = &imgs[i];
    int x = m->w > X1 - X0 ? (W - m->w) / 2 : X0 + (X1 - X0 - m->w) / 2;
    if (m->state <= 0) {
        outline(x, y, m->w, maxh, 2);
        text(x + 16, y + 16, 2, m->state ? "(no image)" : "(image loading)", true);
        return;
    }
    blit_rows(x, y, m, maxh);
}

// The browser: post i's featured image, its title and author under it.
static void draw_card(int i)
{
    char s[80];
    post_t *p = &posts[i];
    if (!begin_frame()) return;
    int y;
    if (p->cover >= 0) {
        picture(p->cover, TOP, imgs[p->cover].h);
        y = wrap(X0, TOP + imgs[p->cover].h + 8, 2, 20, 2, p->title);
    } else y = wrap(X0, TOP + 48, 4, 40, 6, p->title) + 16;
    meta(s, sizeof s, i);
    text(X0, y, 2, s, true);
    ui_frame(1);
}

static void draw_post(int top)
{
    if (!begin_frame()) return;
    int y = TOP + 8, end = page_end(top);
    for (int i = top; i < end; y += lines[i++].h) {
        line_t *l = &lines[i];
        if (l->s) line(X0, y, l->scale, l->s, l->n);
        else if (l->img >= 0) picture(l->img, y, l->h - 12); // decoded size can differ from the box by a pixel
    }
    if (top > 0 || end < nl) { // scroll bar
        int y0 = TOP + 8, sh = H - 8 - y0;
        box(W - 14, y0 + sh * top / nl, 6, sh * (end - top) / nl, true);
    }
    ui_frame(1);
}

static void message(int scale, const char *a, const char *b)
{
    if (!begin_frame()) return;
    text(48, TOP + 48, scale, a, true);
    if (b) wrap(48, TOP + 48 + 8 * scale + 32, 3, 32, 6, b);
    ui_frame(1);
}

// The next press; -1 when an image came in (redraw). The panel sleeps after 3 s without a press.
static int wait_btn(void)
{
    int seen = imgs_done;
    for (int idle = 0;; idle++) {
        feed_tick();
        if (end_failed) return HAD_EXIT_REQUESTED;
        if (imgs_done != seen) return HAD_NO_KEY;
        int b = btn_get(500);
        if (b != HAD_NO_KEY) return b;
        if (idle == 6) ui_close();
    }
}

static bool loading(void)
{
    char s[64];
    int shown = -1;
    while (stage < 2) {
        feed_tick();
        if (end_failed) return false;
        if (stage >= 2) break;
        int now = stage * 100000 + bytes / 1024;
        if (now != shown && begin_frame()) {
            shown = now;
            text(48, TOP + 48, 5, "Loading...", true);
            text(48, TOP + 128, 3, "hackaday.com", true);
            text(48, TOP + 200, 3, "Joining Wi-Fi...", true);
            if (stage >= 1) {
                int tenths = (join_ms + 50) / 100;
                had_format(s, sizeof s, "Wi-Fi joined in %d.%d s", tenths / 10, tenths % 10);
                text(48, TOP + 240, 3, s, true);
                had_format(s, sizeof s, "Downloading the feed: %d KB", bytes / 1024);
                text(48, TOP + 280, 3, s, true);
            }
            ui_frame(1);
        }
        if (btn_get(100) == HAD_EXIT_REQUESTED) return false;
    }
    return true;
}

static void run_reader(void)
{
    for (int switches = 0;;) { // load
        if (!feed_stop_port()) return;
        feed_start_port();
        ui_keys(NULL, NULL, NULL, NULL);
        if (!loading()) return;
        if (stage == 3) {
            ui_keys(NULL, NULL, NULL, "Retry");
            message(5, "No feed.", err);
            int b;
            while ((b = wait_btn()) != HAD_B) {
                if (b == HAD_EXIT_REQUESTED) return;
            }
            continue;
        }
        for (int sel = 0, post = -1, top = 0;;) { // post < 0: the browser
            bool scene = false;
            cur_post = post < 0 ? sel : post, reading = post >= 0; // its images are fetched first
            if (post < 0) ui_keys("Prev", "Next", "Open", "Refresh"), draw_card(sel);
            else ui_keys("Up", "Down", post + 1 < nposts ? "Next post" : NULL, "Back"), draw_post(top);
            int b = wait_btn();
            if (b == HAD_EXIT_REQUESTED) return;
            if (post < 0) {
                if (b == HAD_PLUS && sel > 0) sel--, scene = true;
                else if (b == HAD_MINUS && sel < nposts - 1) sel++, scene = true;
                else if (b == HAD_A) post = sel, top = 0, layout(post), scene = true;
                else if (b == HAD_B) break; // refresh
            } else if (b == HAD_PLUS && top > 0) top = page_back(top), scene = true;
            else if (b == HAD_MINUS && page_end(top) < nl) top = page_end(top), scene = true;
            else if (b == HAD_A && post + 1 < nposts) sel = ++post, top = 0, layout(post), scene = true;
            else if (b == HAD_B) post = -1, scene = true;
            if (scene && ++switches % WIPE_EVERY == 0) ui_wipe();
        }
    }
}


bool had_native_run(const had_native_io_t *binding)
{
    if (io || !binding || !binding->view || !binding->key || !binding->keys ||
        !binding->alloc || !binding->dealloc || !binding->begin_feed || !binding->begin_image ||
        !binding->poll || !binding->end_job) return false;
    const badge_native_api_t *s = binding->view;
    if (s->abi_version != BADGE_NATIVE_ABI || s->struct_size < sizeof *s ||
        s->width != W || s->height != H || s->top != TOP || !s->begin_frame ||
        !s->clear || !s->submit_frame || !s->frame_idle || !s->wipe ||
        !s->box || !s->outline || !s->text) return false;
    io = binding;
    screen = s;
    end_failed = link_open = false;
    job = JOB_NONE;
    posts = alloc(HAD_POSTS_MAX * sizeof *posts);
    imgs = alloc(HAD_IMAGES_MAX * sizeof *imgs);
    lines = alloc(MAXL * sizeof *lines);
    bool ready = posts && imgs && lines;
    if (ready) run_reader();
    else message(5, "No feed.", "Not enough app memory.");
    feed_stop_port();
    ui_close();
    free_port(lines);
    free_port(imgs);
    free_port(posts);
    lines = NULL;
    imgs = NULL;
    posts = NULL;
    screen = NULL;
    io = NULL;
    return ready && !end_failed;
}
