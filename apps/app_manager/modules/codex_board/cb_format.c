#include "cb_format.h"
#include <stdarg.h>

void cb_format(char *out, size_t capacity, const char *format, ...)
{
    if (!capacity) return;
    char *end = out + capacity - 1;
    va_list args;
    va_start(args, format);
    while (*format && out < end) {
        if (*format != '%') {
            *out++ = *format++;
            continue;
        }
        format++;
        unsigned precision = (unsigned)-1;
        if (*format == '.') {
            precision = 0;
            while (*++format >= '0' && *format <= '9')
                precision = 10 * precision + (unsigned)(*format - '0');
        }
        switch (*format++) {
        case 's': {
            const char *value = va_arg(args, const char *);
            while (out < end && precision && *value) {
                *out++ = *value++;
                precision--;
            }
            break;
        }
        case 'u': {
            unsigned value = va_arg(args, unsigned);
            char reversed[10];
            unsigned count = 0;
            do {
                reversed[count++] = (char)('0' + value % 10);
                value /= 10;
            } while (value);
            while (count && out < end) *out++ = reversed[--count];
            break;
        }
        case '%':
            *out++ = '%';
            break;
        default:
            goto done;
        }
    }
done:
    *out = '\0';
    va_end(args);
}
