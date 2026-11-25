/* The engine uses these memory conversions to generate WAD and save names. */
#include "doom_glue.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>

typedef struct { char *str; size_t cap, used; } output_t;

static void put(output_t *out, char ch)
{
    if (out->cap && out->used < out->cap - 1) out->str[out->used] = ch;
    out->used++;
}

static int decimal(const char **s)
{
    int value = 0;
    while ((unsigned)(**s - '0') <= 9) {
        if (value > 4096) return -1;
        value = value * 10 + *(*s)++ - '0';
    }
    return value;
}

int doom_vsnprintf(char *str, size_t capacity, const char *fmt, va_list ap)
{
    output_t out = {str, capacity, 0};
    bool incomplete = false;
    while (*fmt) {
        if (*fmt != '%') { put(&out, *fmt++); continue; }
        fmt++;
        if (*fmt == '%') { put(&out, *fmt++); continue; }
        bool left = *fmt == '-';
        if (left) fmt++;
        bool zero = *fmt == '0';
        int width = decimal(&fmt), precision = -1;
        if (width < 0) break;
        if (*fmt == '.') {
            fmt++;
            precision = decimal(&fmt);
            if (precision < 0) break;
        }
        char ch = *fmt;
        if (ch == 's' || ch == 'c') {
            char character = ch == 'c' ? (char)va_arg(ap, int) : '\0';
            const char *s = ch == 's' ? va_arg(ap, const char *) : &character;
            if (!s) s = "(null)";
            int n = ch == 'c' ? 1 : 0;
            if (ch == 's') while ((precision < 0 || n < precision) && s[n]) n++;
            if (!left) for (int i = n; i < width; i++) put(&out, ' ');
            for (int i = 0; i < n; i++) put(&out, s[i]);
            if (left) for (int i = n; i < width; i++) put(&out, ' ');
        } else if (ch && strchr("diuxXo", ch)) {
            unsigned base = ch == 'o' ? 8 : (ch == 'x' || ch == 'X') ? 16 : 10;
            bool signed_value = ch == 'd' || ch == 'i';
            int value = signed_value ? va_arg(ap, int) : 0;
            bool negative = signed_value && value < 0;
            unsigned n = signed_value ? (unsigned)value : va_arg(ap, unsigned);
            if (negative) n = 0u - n;
            const char *digits = ch == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char number[3 * sizeof n];
            int count = 0;
            if (n || precision != 0) do {
                number[count++] = digits[n % base];
                n /= base;
            } while (n);
            int zeros = precision > count ? precision - count : 0;
            int blanks = width - count - zeros - negative;
            if (precision < 0 && zero && !left && blanks > 0) { zeros += blanks; blanks = 0; }
            if (!left) for (int i = 0; i < blanks; i++) put(&out, ' ');
            if (negative) put(&out, '-');
            for (int i = 0; i < zeros; i++) put(&out, '0');
            while (count) put(&out, number[--count]);
            if (left) for (int i = 0; i < blanks; i++) put(&out, ' ');
        } else {
            incomplete = true;
            break;
        }
        fmt++;
    }
    if (capacity) str[out.used < capacity ? out.used : capacity - 1] = '\0';
    return incomplete || *fmt || out.used > INT_MAX ? -1 : (int)out.used;
}

int doom_snprintf(char *str, size_t cap, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = doom_vsnprintf(str, cap, fmt, args);
    va_end(args);
    return n;
}

static int digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int doom_sscanf(const char *str, const char *fmt, ...)
{
    while (*fmt && *fmt != '%') {
        if (*fmt == ' ') while (isspace((unsigned char)*str)) str++;
        else if (*fmt != *str++) return 0;
        fmt++;
    }
    if (!*fmt || !fmt[1] || fmt[2] || !strchr("dioxX", fmt[1])) return 0;
    while (isspace((unsigned char)*str)) str++;
    bool minus = *str == '-';
    if (*str == '+' || *str == '-') str++;
    unsigned base = fmt[1] == 'o' ? 8 : (fmt[1] == 'x' || fmt[1] == 'X') ? 16 : 10;
    if (fmt[1] == 'i' && *str == '0') base = 8;
    if ((base == 16 || fmt[1] == 'i') && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
        base = 16;
    }
    const char *start = str;
    unsigned value = 0;
    int n;
    while ((n = digit((unsigned char)*str)) >= 0 && (unsigned)n < base) {
        if (value > (UINT_MAX - (unsigned)n) / base) return 0;
        value = value * base + (unsigned)n;
        str++;
    }
    if (str == start) return 0;
    if (minus) value = 0u - value;
    va_list args;
    va_start(args, fmt);
    memcpy(va_arg(args, int *), &value, sizeof value);
    va_end(args);
    return 1;
}
