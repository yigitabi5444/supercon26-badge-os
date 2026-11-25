#define W 960
#define H 672
#define TOP 48

static const had_native_io_t *io;
static const badge_native_api_t *screen;
static enum { JOB_NONE, JOB_FEED, JOB_IMAGE } job;
static int pending_image;
static int stage, bytes, imgs_done, join_ms, cur_post;
static bool reading, link_open, end_failed;
static char err[64];

static void *alloc(size_t n) { return io->alloc(io->context, n); }
static void free_port(void *p) { if (p) io->dealloc(io->context, p); }
static bool finish_job(bool close_link)
{
    if (end_failed) return false;
    if (!io->end_job(io->context, close_link)) {
        // The host still owns its worker and output. Explicit release handles it.
        end_failed = true;
        return false;
    }
    job = JOB_NONE;
    link_open = !close_link;
    return true;
}
static bool feed_stop_port(void)
{
    if (link_open) finish_job(true);
    for (int i = 0; i < nposts; i++) {
        free_port(posts[i].body);
        posts[i].body = NULL;
    }
    for (int i = 0; i < nimg; i++) {
        free_port(imgs[i].bits);
        imgs[i].bits = NULL;
    }
    nposts = nimg = 0;
    return !end_failed;
}
static void feed_start_port(void)
{
    stage = bytes = imgs_done = join_ms = cur_post = 0;
    reading = false;
    err[0] = 0;
    if (!io->begin_feed(io->context)) {
        stage = 3;
        had_format(err, sizeof err, "Could not start the feed download.");
        return;
    }
    link_open = true;
    job = JOB_FEED;
}
static void load_pixels(img_t *image, const had_result_t *r)
{
    if (!r->data || !r->width || !r->height || image->w < 1 || image->h < 1 ||
        r->width > (unsigned)image->w || r->height > (unsigned)image->h ||
        r->width > W || r->height > 544) return;
    size_t len = ((r->width + 7u) / 8u) * r->height;
    if (r->length != len) return;
    uint8_t *bits = alloc(len);
    if (!bits) return;
    memcpy(bits, r->data, len);
    image->bits = bits;
    image->w = (int)r->width;
    image->h = (int)r->height;
    image->state = 1;
}
static void feed_tick(void)
{
    if (!link_open) return;
    if (job != JOB_NONE) {
        had_result_t r = {0};
        had_job_status_t status = io->poll(io->context, &r);
        if (job == JOB_FEED) {
            stage = r.wifi_joined ? 1 : 0;
            join_ms = r.join_ms <= 20000 ? (int)r.join_ms : 20000;
            bytes = r.bytes < 1024 * 1024 ? (int)r.bytes : 1024 * 1024;
            if (status == HAD_PENDING) return;
            const bool ok = status == HAD_OK && had_feed_parse((const char *)r.data, r.length,
                posts, imgs, alloc, &nposts, &nimg);
            stage = ok ? 2 : 3;
            if (!ok) {
                r.error[sizeof r.error - 1] = 0;
                had_format(err, sizeof err, "%s", r.error[0] ? r.error : "No posts in the feed.");
            }
            finish_job(!ok);
            return; // First choose the current screen/article on the next iteration.
        }
        if (status == HAD_PENDING) return;
        img_t *img = &imgs[pending_image];
        img->state = -1;
        if (status == HAD_OK) load_pixels(img, &r);
        imgs_done++;
        if (!finish_job(false)) return;
    }
    int i = next_image(); // Existing order: current cover, open article, future covers, rest.
    if (i < 0) {
        finish_job(true);
        return;
    }
    const img_t *img = &imgs[i];
    if (img->w > 0 && img->h > 0 && img->w <= W && img->h <= 544 &&
        io->begin_image(io->context, img->url, (unsigned)img->w, (unsigned)img->h)) {
        pending_image = i;
        job = JOB_IMAGE;
    } else {
        imgs[i].state = -1;
        imgs_done++;
    }
}

static int btn_get(int timeout) { return io->key(io->context, (unsigned)timeout); }
static void ui_keys(const char *p, const char *m, const char *a, const char *b)
{ io->keys(io->context, p, m, a, b); }
static void ui_frame(int pulls) { (void)pulls; screen->submit_frame(screen->context); }
static void ui_close(void) { screen->frame_idle(screen->context); }
static void ui_wipe(void) { screen->wipe(screen->context); }
static bool begin_frame(void)
{
    if (!screen->begin_frame(screen->context)) {
        screen->frame_idle(screen->context);
        if (!screen->begin_frame(screen->context)) return false;
    }
    screen->clear(screen->context);
    return true;
}
static void box(int x, int y, int w, int h, bool ink)
{ screen->box(screen->context, x, y, w, h, ink); }
static void outline(int x, int y, int w, int h, int t)
{ screen->outline(screen->context, x, y, w, h, t, true); }
static void text(int x, int y, int scale, const char *s, bool ink)
{
    if (scale == 5) {
        // ABI2 host text supports only scale 1..4; retain the original size-5
        // glyphs through drawing primitives and the exact Badge OS font.
        for (; *s; ++s, x += 40)
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                    if (font8x8_basic[(unsigned char)*s & 127][r] & (1u << c))
                        box(x + c * 5, y + r * 5, 5, 5, ink);
        return;
    }
    char part[65]; // ABI2 bounds native text to 64 bytes, including long metadata labels.
    for (; *s;) {
        size_t n = 0;
        while (n < sizeof part - 1 && s[n]) ++n;
        memcpy(part, s, n);
        part[n] = 0;
        screen->text(screen->context, x, y, scale, part, ink);
        x += (int)n * 8 * scale;
        s += n;
    }
}
static void blit_rows(int x, int y, const img_t *img, int maxh)
{
    unsigned w = (unsigned)img->w, stride = (w + 7u) / 8u;
    unsigned rows = (unsigned)(img->h < maxh ? img->h : maxh);
    if (io->blit_mono) {
        io->blit_mono(io->context, x, y, w, rows, img->bits, stride * rows);
        return;
    }
    // Fallback keeps ABI2 framebuffer ownership and renders the same ink.
    // A host blit avoids repeated calls for a large dithered image.
    for (unsigned r = 0; r < rows; r++) {
        const uint8_t *row = img->bits + stride * r;
        for (unsigned c = 0; c < w;) {
            if (!(row[c / 8] & (0x80u >> (c % 8)))) { c++; continue; }
            unsigned start = c++;
            while (c < w && (row[c / 8] & (0x80u >> (c % 8)))) c++;
            box(x + (int)start, y + (int)r, (int)(c - start), 1, true);
        }
    }
}
