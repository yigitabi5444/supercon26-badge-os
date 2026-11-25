#pragma once

#include "badge_doom_api.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern const badge_doom_api_t *doom_api;
void doom_save_reset(void);
void *doom_malloc(size_t);
void *doom_calloc(size_t, size_t);
void *doom_realloc(void *, size_t);
void doom_free(void *);
char *doom_strdup(const char *);
_Noreturn void doom_exit(int);
FILE *doom_fopen(const char *, const char *);
size_t doom_fread(void *, size_t, size_t, FILE *);
size_t doom_fwrite(const void *, size_t, size_t, FILE *);
int doom_fclose(FILE *);
long doom_ftell(FILE *);
int doom_fseek(FILE *, long, int);
int doom_mkdir(const char *, mode_t);
int doom_remove(const char *);
int doom_rename(const char *, const char *);
int doom_system(const char *);
int doom_snprintf(char *, size_t, const char *, ...);
int doom_vsnprintf(char *, size_t, const char *, va_list);
int doom_sscanf(const char *, const char *, ...);

#ifdef __XTENSA__
int doom_console_printf(const char *, ...);
int doom_console_fprintf(FILE *, const char *, ...);
int doom_console_vfprintf(FILE *, const char *, va_list);
int doom_console_puts(const char *);
int doom_console_fputs(const char *, FILE *);
int doom_console_fputc(int, FILE *);
int doom_console_fflush(FILE *);
#endif

#ifdef DOOM_ENGINE_INTERCEPT
#define malloc doom_malloc
#define calloc doom_calloc
#define realloc doom_realloc
#define free doom_free
#define strdup doom_strdup
#define exit doom_exit
#define fopen doom_fopen
#define fread doom_fread
#define fwrite doom_fwrite
#define fclose doom_fclose
#define ftell doom_ftell
#define fseek doom_fseek
#define mkdir doom_mkdir
#define remove doom_remove
#define rename doom_rename
#define system doom_system
#define snprintf doom_snprintf
#define vsnprintf doom_vsnprintf
#define sscanf doom_sscanf
#ifdef __XTENSA__
#define printf doom_console_printf
#define fprintf doom_console_fprintf
#define vfprintf doom_console_vfprintf
#define puts doom_console_puts
#define fputs doom_console_fputs
#define fputc doom_console_fputc
#define fflush doom_console_fflush
#endif
#endif
