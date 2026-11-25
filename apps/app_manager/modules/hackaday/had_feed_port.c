#include "had_format.h"
#include <string.h>
#include "had_feed_slice.h"

#define W 960
#define CAP (1024 * 1024) // feed buffer in PSRAM; Hackaday's feed carries full post bodies (~100-300 KB)
#define MAXP 10           // posts
#define MAXI 64           // images in all, at most PERP per post (~32 KB each once dithered)
#define PERP 8
#define IW 640            // pictures in a post fit inside IW x IH
#define IH 400
#define CH 544            // featured images fit inside W x CH: the title and author go under it


post_t *posts;
img_t *imgs;
int nposts, nimg;

// ---- Text: HTML/XML/UTF-8 to the 7-bit font.
static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy"; // U+00C0..U+00FF

static int put(char *o, int n, int max, unsigned cp)
{
    const char *s = NULL;
    char c[2] = {0};
    if (cp >= 0x20 && cp < 0x7F) c[0] = cp;
    else if (cp == '\n') c[0] = '\n';
    else if (cp == '\t' || cp == '\r' || cp == 0xA0) c[0] = ' ';
    else if (cp == 0x2018 || cp == 0x2019 || cp == 0x2032) c[0] = '\'';
    else if (cp == 0x201C || cp == 0x201D || cp == 0x2033) c[0] = '"';
    else if (cp >= 0x2010 && cp <= 0x2015) c[0] = '-';
    else if (cp == 0x2026) s = "...";
    else if (cp == 0xD7) c[0] = 'x';
    else if (cp >= 0xC0 && cp <= 0xFF) c[0] = latin1[cp - 0xC0];
    if (!s) s = c; // anything else is dropped
    for (; *s && n < max - 1; s++) {
        if (*s == ' ' && (n == 0 || o[n - 1] == ' ' || o[n - 1] == '\n')) continue; // collapse whitespace
        if (*s == '\n') {
            while (n && o[n - 1] == ' ') n--;
            if (n == 0 || o[n - 1] == '\n') continue; // no empty paragraphs
        }
        o[n++] = *s;
    }
    return n;
}

// The value of name="..." inside the tag [s, e), into o; false if it is not there.
static bool attr(const char *s, const char *e, const char *name, char *o, int max)
{
    char k[16];
    had_format(k, sizeof k, " %s=\"", name);
    int kl = (int)strlen(k);
    for (; e - s > kl; s++)
        if (!strncmp(s, k, kl)) {
            const char *v = s + kl, *q = memchr(v, '"', e - v);
            if (!q) return false;
            had_format(o, max, "%.*s", (int)(q - v), v);
            return true;
        }
    return false;
}

static int dimension(const char *s)
{
    unsigned v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10u + (unsigned)(*s - '0');
        if (v > 65535u) return 0;
    }
    return (int)v;
}

// An image of post p from the tag [s, e) (its URL in attribute u): returns its marker byte, 0 if not taken.
static int add_img(int p, const char *s, const char *e, const char *u)
{
    char src[300], v[12];
    int w = 0, h = 0, k = 0;
    for (int i = 0; i < nimg; i++) k += imgs[i].post == p;
    if (nimg == MAXI || k == PERP || !attr(s, e, u, src, sizeof src)) return 0;
    if (attr(s, e, "width", v, sizeof v)) w = dimension(v);
    if (attr(s, e, "height", v, sizeof v)) h = dimension(v);
    char *q = strchr(src, '?'), *end = q ? q : src + strlen(src);
    if ((w && w < 64) || (end - src > 4 && !strncmp(end - 4, ".gif", 4))) return 0; // smileys, icons, animations
    img_t *m = &imgs[nimg];
    int bw = k ? IW : W, bh = k ? IH : CH; // the first one is the featured image: bigger
    if (w <= 0 || h <= 0) w = 16, h = 9;   // unknown: assume a wide picture
    if (w * bh > h * bw) m->w = bw, m->h = bw * h / w; // the box it is scaled into
    else m->h = bh, m->w = bh * w / h;
    if (!m->w) m->w = 1;
    if (!m->h) m->h = 1;
    if (strstr(src, "/wp-content/uploads/")) { // WordPress resizes on its own server: a ~40 KB JPEG
        if (q) *q = 0;
        had_format(m->url, sizeof m->url, "%s?fit=%d,%d&quality=40", src, m->w, m->h);
    } else had_format(m->url, sizeof m->url, "%s", src);
    m->post = p, m->bits = NULL, m->state = 0;
    if (!k) posts[p].cover = nimg;
    return 0x80 + nimg++;
}

// Copies [s, e) into o as plain ASCII: no CDATA markers, entities and UTF-8 punctuation decoded. Tags: dropped, or
// for post p >= 0 (a body) block tags become paragraph breaks and <img> an image marker on its own line.
static unsigned entity_value(const char *s, const char *end)
{
    if (s == end) return 0;
    unsigned base = *s == 'x' ? 16 : 10;
    if (base == 16) s++;
    unsigned value = 0;
    for (; s < end; s++) {
        unsigned digit;
        if (*s >= '0' && *s <= '9') digit = (unsigned)(*s - '0');
        else if ((*s | 32) >= 'a' && (*s | 32) <= 'f') digit = (unsigned)((*s | 32) - 'a' + 10);
        else break;
        if (digit >= base) break;
        value = value * base + digit;
        if (value > 0x10ffff) return 0;
    }
    return value;
}

static void clean(char *o, int max, const char *s, const char *e, int p)
{
    int n = 0;
    while (s < e) {
        if (e - s >= 9 && !strncmp(s, "<![CDATA[", 9)) s += 9;
        else if (e - s >= 3 && !strncmp(s, "]]>", 3)) s += 3;
        else if (*s == '<') {
            const char *t = s;
            bool closing = e - s > 1 && s[1] == '/';
            while (s < e && *s != '>') s++;
            if (s < e) s++;
            if (p < 0) continue;
            char tag[16] = " ";
            for (int k = 1, i = 1 + closing; k < 14 && i < s - t && (((t[i] | 32) >= 'a' && (t[i] | 32) <= 'z') || (t[i] >= '0' && t[i] <= '9')); i++)
                tag[k++] = t[i] | 32;
            strcat(tag, " ");
            if (!strcmp(tag, " img ")) {
                int m = add_img(p, t, s, "src");
                if (m && n < max - 3) n = put(o, n, max, '\n'), o[n++] = m, o[n++] = '\n';
            } else if (!strcmp(tag, " iframe ") && !closing) {
                for (const char *c = "\n[video]\n"; *c; c++) n = put(o, n, max, *c);
            } else if (!strcmp(tag, " li ") && !closing) {
                n = put(o, n, max, '\n'), n = put(o, n, max, '-'), n = put(o, n, max, ' ');
            } else if (strstr(" p br div h1 h2 h3 h4 h5 h6 ul ol li figure figcaption blockquote pre tr table ", tag)) {
                n = put(o, n, max, '\n');
            }
        } else if (*s == '&') {
            const char *semi = memchr(s, ';', e - s < 10 ? e - s : 10);
            unsigned cp = '&';
            if (semi && s[1] == '#') cp = entity_value(s + 2, semi);
            else if (semi) {
                static const struct { const char *n; unsigned cp; } ent[] = {
                    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '},
                    {"hellip", 0x2026}, {"ndash", '-'}, {"mdash", '-'}, {"rsquo", '\''}, {"lsquo", '\''},
                    {"rdquo", '"'}, {"ldquo", '"'}};
                cp = 0;
                for (size_t i = 0; i < sizeof ent / sizeof *ent; i++)
                    if (strlen(ent[i].n) == (size_t)(semi - s - 1) && !strncmp(s + 1, ent[i].n, semi - s - 1)) cp = ent[i].cp;
            }
            n = put(o, n, max, cp);
            s = semi ? semi + 1 : s + 1;
        } else { // UTF-8
            unsigned char c = *s++;
            unsigned cp = c;
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if (more) cp = c & (0x3F >> more);
            for (; more && s < e; more--) cp = cp << 6 | (*s++ & 0x3F);
            n = put(o, n, max, cp);
        }
    }
    while (n && (o[n - 1] == ' ' || o[n - 1] == '\n')) n--;
    o[n] = 0;
}

// <tag>...</tag> inside [s, e): its content's start, its end in *end; NULL if not there.
static const char *find(const char *s, const char *e, const char *tag, const char **end)
{
    char open[32], close[32];
    had_format(open, sizeof open, "<%s>", tag);
    had_format(close, sizeof close, "</%s>", tag);
    const char *a = strstr(s, open), *b = a ? strstr(a, close) : NULL;
    if (!b || b > e) return NULL;
    *end = b;
    return a + strlen(open);
}

static void field(char *o, int max, const char *s, const char *e, const char *tag)
{
    const char *b, *a = find(s, e, tag, &b);
    if (a) clean(o, max, a, b, -1);
    else *o = 0;
}

static void parse(const char *feed, void *(*allocate)(size_t))
{
    nposts = nimg = 0;
    for (const char *s = strstr(feed, "<item>"), *e; s && nposts < MAXP; s = strstr(e, "<item>")) {
        if (!(e = strstr(s, "</item>"))) break;
        int i = nposts++;
        post_t *p = &posts[i];
        p->cover = -1;
        field(p->title, sizeof p->title, s, e, "title");
        field(p->author, sizeof p->author, s, e, "dc:creator");
        field(p->date, 17, s, e, "pubDate"); // "Sat, 07 Nov 2026"
        const char *b, *a = find(s, e, "content:encoded", &b); // the full post
        if (!a) a = b = e;
        if ((p->body = allocate((size_t)(b - a + 1)))) clean(p->body, b - a + 1, a, b, i);
    }
}


bool had_feed_parse(const char *input, size_t size, post_t *post_storage,
                    img_t *image_storage, void *(*alloc)(size_t),
                    int *post_count, int *image_count)
{
    if (!input || size >= CAP || input[size] || !post_storage || !image_storage ||
        !alloc || !post_count || !image_count) return false;
    posts = post_storage;
    imgs = image_storage;
    parse(input, alloc);
    *post_count = nposts;
    *image_count = nimg;
    return nposts != 0;
}
