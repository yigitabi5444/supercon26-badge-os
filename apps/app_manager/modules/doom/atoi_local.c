/* Diagnostic compatibility substitution for Picolibc atoi -> strtol -> TLS errno. */
#include <limits.h>
#include <stdlib.h>

int atoi(const char *s)
{
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    int negative = *s == '-';
    if (*s == '+' || *s == '-') s++;

    unsigned limit = (unsigned)INT_MAX + negative;
    unsigned value = 0;
    while (*s >= '0' && *s <= '9') {
        unsigned digit = (unsigned)(*s++ - '0');
        if (value > (limit - digit) / 10) return negative ? INT_MIN : INT_MAX;
        value = value * 10 + digit;
    }
    if (negative && value == limit) return INT_MIN;
    return negative ? -(int)value : (int)value;
}
