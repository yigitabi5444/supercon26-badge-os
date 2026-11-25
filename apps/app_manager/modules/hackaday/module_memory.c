#include <stdint.h>
#include <string.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t length)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (length--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t length)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    if ((uintptr_t)d > (uintptr_t)s) {
        while (length) {
            length--;
            d[length] = s[length];
        }
    } else {
        while (length--) *d++ = *s++;
    }
    return dest;
}

void *memset(void *dest, int value, size_t length)
{
    unsigned char *d = dest;
    while (length--) *d++ = (unsigned char)value;
    return dest;
}

void *memchr(const void *src, int value, size_t length)
{
    const unsigned char *s = src;
    for (; length; length--, s++)
        if (*s == (unsigned char)value) return (void *)s;
    return NULL;
}

int memcmp(const void *a, const void *b, size_t length)
{
    const unsigned char *p = a, *q = b;
    for (; length; length--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *restrict dest, const char *restrict src)
{
    char *d = dest;
    while ((*d++ = *src++)) {}
    return dest;
}

size_t strlen(const char *s)
{
    const char *end = s;
    while (*end) end++;
    return (size_t)(end - s);
}

int strncmp(const char *a, const char *b, size_t length)
{
    while (length && *a && *a == *b) { length--; a++; b++; }
    return length ? (unsigned char)*a - (unsigned char)*b : 0;
}

char *strcat(char *restrict dest, const char *restrict src)
{
    strcpy(dest + strlen(dest), src);
    return dest;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if ((unsigned char)*s == (unsigned char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strstr(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return (char *)s;
    for (; *s; s++)
        if (*s == *needle && !strncmp(s, needle, n)) return (char *)s;
    return NULL;
}
