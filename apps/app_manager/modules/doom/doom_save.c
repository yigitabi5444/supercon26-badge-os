#include "doom_glue.h"

#include <errno.h>
#include "i_system.h"

/* The engine opens one save stream at a time. Reads are cached in small blocks.
   A write stays private until the engine renames its completed temporary save. */
enum { SAVE_CLOSED, SAVE_READ, SAVE_TEMP, SAVE_RECOVERY };
static struct {
    int mode;
    uint32_t slot, position;
    uint32_t read_offset, read_bytes, read_length;
    uint8_t read_block[BADGE_DOOM_READ_MAX];
    uint8_t *write_data;
    uint32_t write_length, write_capacity;
    bool ready, failed;
} save;

static FILE *const save_file = (FILE *)(void *)&save;
static const char temp_path[] = "./.savegame/temp.dsg";
static const char recovery_path[] = "/tmp/recovery.dsg";

static int slot_number(const char *path)
{
    static const char prefix[] = "./.savegame/doomsav";
    size_t n = sizeof prefix - 1;
    if (strncmp(path, prefix, n)) return -1;
    if (path[n] < '0' || path[n] >= '0' + BADGE_DOOM_SAVE_SLOTS || strcmp(path + n + 1, ".dsg")) return -1;
    return path[n] - '0';
}

void doom_save_reset(void)
{
    doom_free(save.write_data);
    save.write_data = NULL;
    save.write_capacity = save.write_length = save.position = 0;
    save.ready = save.failed = false;
    save.mode = SAVE_CLOSED;
}

static bool resize_stage(uint32_t bytes)
{
    if (bytes > BADGE_DOOM_SAVE_MAX) return false;
    if (bytes <= save.write_capacity) return true;
    uint32_t capacity = save.write_capacity ? save.write_capacity * 2 : BADGE_DOOM_READ_MAX;
    if (capacity < bytes) capacity = bytes;
    if (capacity > BADGE_DOOM_SAVE_MAX) capacity = BADGE_DOOM_SAVE_MAX;
    uint8_t *out = doom_realloc(save.write_data, capacity);
    if (!out) return false;
    save.write_data = out;
    save.write_capacity = capacity;
    return true;
}

static bool read_block(uint32_t offset)
{
    uint32_t capacity = BADGE_DOOM_SAVE_MAX - offset;
    if (capacity > sizeof save.read_block) capacity = sizeof save.read_block;
    int32_t n = capacity ? doom_api->save_read(doom_api->base.context, save.slot,
                                               offset, save.read_block, capacity) : 0;
    if (n < 0 || (uint32_t)n > capacity) return false;
    save.read_offset = offset;
    save.read_bytes = (uint32_t)n;
    if ((uint32_t)n < capacity || offset + (uint32_t)n == BADGE_DOOM_SAVE_MAX)
        save.read_length = offset + (uint32_t)n;
    return true;
}

FILE *doom_fopen(const char *path, const char *mode)
{
    if (save.mode == SAVE_CLOSED) {
        int slot = slot_number(path);
        if (slot >= 0 && (!strcmp(mode, "rb") || !strcmp(mode, "r"))) {
            save.slot = (uint32_t)slot;
            save.position = 0;
            save.read_length = UINT32_MAX;
            if (read_block(0) && save.read_bytes) {
                save.mode = SAVE_READ;
                return save_file;
            }
        } else if (!strcmp(mode, "wb") && (!strcmp(path, temp_path) || !strcmp(path, recovery_path))) {
            doom_save_reset();
            if (resize_stage(BADGE_DOOM_READ_MAX)) {
                save.mode = !strcmp(path, temp_path) ? SAVE_TEMP : SAVE_RECOVERY;
                return save_file;
            }
        }
    }
    errno = ENOENT;
    return NULL;
}

size_t doom_fread(void *out, size_t width, size_t count, FILE *file)
{
    if (file != save_file || save.mode != SAVE_READ || !width || count > SIZE_MAX / width) return 0;
    uint8_t *dst = out;
    size_t left = width * count;
    while (left && save.position < save.read_length && save.position < BADGE_DOOM_SAVE_MAX) {
        if (save.position < save.read_offset || save.position - save.read_offset >= save.read_bytes) {
            uint32_t next = save.position & ~(BADGE_DOOM_READ_MAX - 1u);
            if (!read_block(next) || save.position - next >= save.read_bytes) break;
        }
        uint32_t rel = save.position - save.read_offset;
        size_t bytes = save.read_bytes - rel;
        if (bytes > left) bytes = left;
        memcpy(dst, save.read_block + rel, bytes);
        save.position += (uint32_t)bytes;
        dst += bytes;
        left -= bytes;
    }
    return (width * count - left) / width;
}

size_t doom_fwrite(const void *data, size_t width, size_t count, FILE *file)
{
    if (file != save_file || (save.mode != SAVE_TEMP && save.mode != SAVE_RECOVERY) || !width || save.failed) return 0;
    if (count > (BADGE_DOOM_SAVE_MAX - save.position) / width ||
        !resize_stage(save.position + (uint32_t)(width * count))) {
        save.failed = true;
        return 0;
    }
    size_t bytes = width * count;
    memcpy(save.write_data + save.position, data, bytes);
    save.position += (uint32_t)bytes;
    if (save.position > save.write_length) save.write_length = save.position;
    return count;
}

long doom_ftell(FILE *file) { return file == save_file && save.mode != SAVE_CLOSED ? (long)save.position : -1; }
int doom_fseek(FILE *file, long offset, int origin)
{
    if (file != save_file || save.mode == SAVE_CLOSED) return -1;
    uint32_t end = save.mode == SAVE_READ ? save.read_length : save.write_length;
    uint32_t base;
    switch (origin) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = save.position; break;
    case SEEK_END:
        if (end == UINT32_MAX) return -1;
        base = end;
        break;
    default: return -1;
    }
    if (offset < -(long)base || offset > (long)(BADGE_DOOM_SAVE_MAX - base)) return -1;
    uint32_t position = (uint32_t)((long)base + offset);
    if (position > end) return -1;
    save.position = position;
    return 0;
}

int doom_fclose(FILE *file)
{
    if (file != save_file || save.mode == SAVE_CLOSED) return EOF;
    int mode = save.mode;
    save.mode = SAVE_CLOSED;
    if (mode == SAVE_READ) return 0;
    save.ready = !save.failed && save.write_length != 0;
    if (mode == SAVE_RECOVERY) {
        int success = save.ready && doom_api->save_replace(doom_api->base.context, BADGE_DOOM_RECOVERY_SAVE,
                                                           save.write_data, save.write_length) == 1;
        doom_save_reset();
        if (!success) I_Error("Could not write the Doom recovery save");
        return success ? 0 : EOF;
    }
    return save.ready ? 0 : EOF;
}

int doom_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    return !strcmp(path, ".") || !strcmp(path, "./.savegame/") ? 0 : -1;
}

/* The engine removes the old name before rename. The host's replace commits
   both effects together, so a rejected or interrupted write leaves it valid. */
int doom_remove(const char *path) { return save.ready && slot_number(path) >= 0 ? 0 : -1; }

int doom_rename(const char *from, const char *to)
{
    int slot = slot_number(to);
    if (strcmp(from, temp_path) || slot < 0) return -1;
    bool success = save.ready && doom_api->save_replace(doom_api->base.context, (uint32_t)slot,
                                                         save.write_data, save.write_length) == 1;
    if (!success) {
        bool recovered = save.ready && doom_api->save_replace(doom_api->base.context, BADGE_DOOM_RECOVERY_SAVE,
                                                               save.write_data, save.write_length) == 1;
        I_Error(recovered ? "Doom save failed; kept a recovery copy" : "Doom save failed");
    }
    doom_save_reset();
    return success ? 0 : -1;
}
