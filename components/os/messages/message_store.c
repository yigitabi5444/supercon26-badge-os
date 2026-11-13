#include "message_store.h"
#include <stdlib.h>
#include <string.h>

static void put64(uint8_t *p, uint64_t n) { for (int i = 0; i < 8; i++, n >>= 8) p[i] = n; }
static uint64_t get64(const uint8_t *p) { uint64_t n = 0; for (int i = 7; i >= 0; i--) n = (n << 8) | p[i]; return n; }
uint32_t message_crc(const uint8_t *p, size_t n)
{
    uint32_t c = ~0u;
    while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); }
    return ~c;
}
static bool nonzero(const uint8_t *p, size_t n) { while (n--) if (*p++) return true; return false; }
static bool plain(const char *p, size_t n, bool newline)
{
    const char *end = memchr(p, 0, n);
    if (!end) return false;
    for (; p < end; p++) if ((*p < 32 || *p > 126) && !(newline && *p == '\n')) return false;
    return true;
}
bool message_valid(const message_t *m)
{
    return nonzero(m->id, 16) && nonzero(m->thread, 16) && nonzero(m->sender, 6) && m->logical &&
        m->logical < UINT64_MAX && m->clock <= 1 && (m->clock || !m->seconds) &&
        (!m->clock || (m->seconds >= 1577836800 && m->seconds <= 4102444800ULL)) &&
        plain(m->title, sizeof m->title, false) && plain(m->body, sizeof m->body, true) &&
        m->body[0] && plain(m->name, sizeof m->name, false);
}
void message_encode(const message_t *m, uint8_t p[MSG_RECORD_BYTES])
{
    memset(p, 0, MSG_RECORD_BYTES);
    memcpy(p, m->id, 16); memcpy(p + 16, m->thread, 16); memcpy(p + 32, m->sender, 6);
    put64(p + 38, m->logical); put64(p + 46, m->seconds); p[54] = m->clock;
    memcpy(p + 55, m->title, strlen(m->title)); memcpy(p + 88, m->body, strlen(m->body));
    memcpy(p + 329, m->name, strlen(m->name));
}
bool message_decode(message_t *m, const uint8_t *p, size_t n)
{
    if (n != MSG_RECORD_BYTES) return false;
    memset(m, 0, sizeof *m);
    memcpy(m->id, p, 16); memcpy(m->thread, p + 16, 16); memcpy(m->sender, p + 32, 6);
    m->logical = get64(p + 38); m->seconds = get64(p + 46); m->clock = p[54];
    memcpy(m->title, p + 55, 33); memcpy(m->body, p + 88, 241); memcpy(m->name, p + 329, 13);
    uint8_t canonical[MSG_RECORD_BYTES];
    if (!message_valid(m)) return false;
    message_encode(m, canonical);
    return !memcmp(p, canonical, sizeof canonical); // reject hidden bytes after string terminators
}
bool message_ignored(const message_state_t *s, const uint8_t id[16])
{ for (int i = 0; i < s->ignore_count; i++) if (!memcmp(s->ignored[i], id, 16)) return true; return false; }
int message_find(const message_state_t *s, const uint8_t id[16])
{ for (int i = 0; i < s->count; i++) if (!memcmp(s->messages[i].id, id, 16)) return i; return -1; }
uint64_t message_next_logical(const message_state_t *s)
{
    uint64_t n = s->high_water;
    for (int i = 0; i < s->count; i++) if (s->messages[i].logical > n) n = s->messages[i].logical;
    return n == UINT64_MAX - 1 ? n : n + 1; // saturation keeps a hostile counter from disabling composition
}
static bool before(const message_t *a, const message_t *b)
{ return a->logical < b->logical || (a->logical == b->logical && memcmp(a->id, b->id, 16) < 0); }
static bool snapshot(message_state_t *s, const uint8_t *p, size_t size)
{
    if (size < 41 || size > MSG_SNAPSHOT_BYTES) return false;
    if (memcmp(p, "BMS2", 4) || p[4] != 2 || p[5] > MSG_CAP || p[6] > 1 || p[7] != MSG_CAP ||
        memcmp(p + 28, "DONE", 4)) return false;
    size_t deleted = 40 + p[5] * (MSG_RECORD_BYTES + 1);
    if (deleted >= size || p[deleted] > MSG_IGNORED || size != deleted + 1 + p[deleted] * 16) return false;
    uint32_t saved = (uint32_t)get64(p + 16);
    if (saved != message_crc(p + 32, size - 32) ||
        (uint32_t)(get64(p + 16) >> 32) != message_crc(p, 16) || get64(p + 24) != 0x454e4f4400000000ULL) return false;
    memset(s, 0, sizeof *s); s->count = p[5]; s->relay = p[6]; s->high_water = get64(p + 32);
    if (s->high_water == UINT64_MAX) return false;
    for (int i = 0; i < s->count; i++) {
        const uint8_t *r = p + 40 + i * (MSG_RECORD_BYTES + 1);
        if (r[MSG_RECORD_BYTES] > 1 || !message_decode(&s->messages[i], r, MSG_RECORD_BYTES)) return false;
        if (s->messages[i].logical > s->high_water) return false;
        s->read[i] = r[MSG_RECORD_BYTES];
        for (int j = 0; j < i; j++) if (!memcmp(s->messages[i].id, s->messages[j].id, 16)) return false;
        if (i && !before(&s->messages[i - 1], &s->messages[i])) return false;
    }
    s->ignore_count = p[deleted]; if (s->ignore_count > MSG_IGNORED) return false;
    for (int i = 0; i < s->ignore_count; i++) {
        const uint8_t *id = p + deleted + 1 + i * 16;
        if (!nonzero(id, 16) || message_find(s, id) >= 0) return false;
        for (int j = 0; j < i; j++) if (!memcmp(s->ignored[j], id, 16)) return false;
        memcpy(s->ignored[i], id, 16);
    }
    return true;
}
size_t message_pack(const message_state_t *s, uint64_t generation, uint8_t p[MSG_SNAPSHOT_BYTES])
{
    memset(p, 0, MSG_SNAPSHOT_BYTES);
    memcpy(p, "BMS2", 4); p[4] = 2; p[7] = MSG_CAP; p[5] = s->count; p[6] = s->relay;
    put64(p + 8, generation); put64(p + 32, s->high_water);
    for (int i = 0; i < s->count; i++) {
        uint8_t *r = p + 40 + i * (MSG_RECORD_BYTES + 1);
        message_encode(&s->messages[i], r); r[MSG_RECORD_BYTES] = s->read[i];
    }
    size_t deleted = 40 + s->count * (MSG_RECORD_BYTES + 1);
    size_t size = deleted + 1 + s->ignore_count * 16;
    p[deleted] = s->ignore_count;
    memcpy(p + deleted + 1, s->ignored, s->ignore_count * 16);
    put64(p + 16, ((uint64_t)message_crc(p, 16) << 32) | message_crc(p + 32, size - 32));
    memcpy(p + 28, "DONE", 4);
    return size;
}
bool message_unpack(message_state_t *s, uint64_t *generation, const uint8_t *p, size_t n)
{
    if (!snapshot(s, p, n) || !get64(p + 8)) return false;
    *generation = get64(p + 8); return true;
}
static int commit(message_db_t *db, const message_state_t *s)
{
    if (!db->ready || !db->persist || db->generation == UINT64_MAX) return MSG_IO;
    int result = db->persist(db->persist_ctx, s, db->generation + 1);
    if (result == MSG_OK) { db->state = *s; db->generation++; }
    else if (result != MSG_FULL) db->ready = false; // require reopen after an uncertain atomic-blob result
    return result;
}

int message_put(message_db_t *db, const message_t *m, bool read)
{
    if (!db->ready) return MSG_IO;
    if (!message_valid(m)) return MSG_INVALID;
    if (message_ignored(&db->state, m->id)) return MSG_DELETED;
    int at = message_find(&db->state, m->id);
    if (at >= 0) {
        uint8_t a[MSG_RECORD_BYTES], b[MSG_RECORD_BYTES];
        message_encode(m, a); message_encode(&db->state.messages[at], b);
        return memcmp(a, b, sizeof a) ? MSG_CONFLICT : MSG_DUP;
    }
    if (db->state.count == MSG_CAP) return MSG_FULL;
    message_state_t *s = malloc(sizeof *s);
    if (!s) return MSG_IO;
    *s = db->state; at = s->count++;
    while (at > 0 && before(m, &s->messages[at - 1])) { s->messages[at] = s->messages[at - 1]; s->read[at] = s->read[at - 1]; at--; }
    s->messages[at] = *m; s->read[at] = read;
    if (m->logical > s->high_water) s->high_water = m->logical;
    int result = commit(db, s); free(s); return result;
}
static int change(message_db_t *db, const uint8_t thread[16], int action)
{
    if (!db->ready) return MSG_IO;
    message_state_t *s = malloc(sizeof *s);
    if (!s) return MSG_IO;
    *s = db->state;
    if (action < 2) s->relay = action;
    else for (int i = 0; i < s->count; i++) if (!memcmp(s->messages[i].thread, thread, 16)) {
        if (action == 2) s->read[i] = 1;
        else {
            if (!message_ignored(s, s->messages[i].id)) {
                if (s->ignore_count == MSG_IGNORED) {
                    memmove(s->ignored, s->ignored + 1, (MSG_IGNORED - 1) * 16); s->ignore_count--;
                }
                memcpy(s->ignored[s->ignore_count++], s->messages[i].id, 16);
            }
            memmove(s->messages + i, s->messages + i + 1, (s->count - i - 1) * sizeof(message_t));
            memmove(s->read + i, s->read + i + 1, s->count - i - 1); s->count--; memset(s->messages + s->count, 0, sizeof(message_t)); s->read[s->count] = 0; i--; }
    }
    int result = !memcmp(s, &db->state, sizeof *s) ? MSG_DUP : commit(db, s);
    free(s); return result;
}
int message_mark_read(message_db_t *db, const uint8_t thread[16]) { return change(db, thread, 2); }
int message_relay(message_db_t *db, bool enabled) { return change(db, NULL, enabled); }
int message_forget(message_db_t *db, const uint8_t thread[16]) { return change(db, thread, 3); }
void message_hex(const uint8_t id[16], char out[33])
{ const char *h = "0123456789abcdef"; for (int i = 0; i < 16; i++) { out[2*i] = h[id[i] >> 4]; out[2*i+1] = h[id[i] & 15]; } out[32] = 0; }
bool message_unhex(const char *s, uint8_t id[16])
{
    if (strlen(s) != 32) return false;
    for (int i = 0; i < 16; i++) {
        unsigned n = 0;
        for (int j = 0; j < 2; j++) { char c = s[2*i+j]; if (c < '0' || (c > '9' && c < 'a') || c > 'f') return false;
            n = n * 16 + (c <= '9' ? c - '0' : c - 'a' + 10); }
        id[i] = n;
    }
    return true;
}
