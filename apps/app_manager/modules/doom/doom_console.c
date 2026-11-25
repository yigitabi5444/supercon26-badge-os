/* The badge has no Doom console. Keep engine progress output module-local. */
#include "doom_glue.h"

#undef putchar
int doom_probe_errno;
FILE *stdout;
FILE *stderr;

int doom_console_printf(const char *format, ...) { (void)format; return 0; }
int doom_console_fprintf(FILE *file, const char *format, ...) { (void)file; (void)format; return 0; }
int doom_console_vfprintf(FILE *file, const char *format, va_list args)
{ (void)file; (void)format; (void)args; return 0; }
int doom_console_puts(const char *line) { (void)line; return 0; }
int doom_console_fputs(const char *line, FILE *file) { (void)line; (void)file; return 0; }
int doom_console_fputc(int ch, FILE *file) { (void)file; return ch; }
int doom_console_fflush(FILE *file) { (void)file; return 0; }
