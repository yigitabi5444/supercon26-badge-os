/* Diagnostic-only: the engine probe has one foreground context. */
#include <errno.h>
#undef errno
extern int doom_probe_errno __attribute__((visibility("hidden")));
#define errno doom_probe_errno
