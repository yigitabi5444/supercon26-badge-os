#include "had_format.h"
#include <stdarg.h>

void had_format(char *out, size_t capacity, const char *format, ...)
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
        int precision = -1;
        if (*format == '.' && format[1] == '*') {
            precision = va_arg(args, int);
            format += 2;
        }
        switch (*format++) {
        case 's': {
            const char *value = va_arg(args, const char *);
            while (out < end && precision && *value) {
                *out++ = *value++;
                if (precision > 0) precision--;
            }
            break;
        }
        case 'd': {
            int signed_value = va_arg(args, int);
            unsigned value = (unsigned)signed_value;
            if (signed_value < 0) {
                *out++ = '-';
                value = 0u - value;
            }
            char reversed[10];
            unsigned count = 0;
            do {
                reversed[count++] = (char)('0' + value % 10);
                value /= 10;
            } while (value);
            while (count && out < end) *out++ = reversed[--count];
            break;
        }
        default:
            goto done;
        }
    }
done:
    *out = '\0';
    va_end(args);
}
