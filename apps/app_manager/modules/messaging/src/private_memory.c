// Private module-local replacements for portable QR libc operations.
#include <stddef.h>
#include <stdint.h>
size_t msg_strlen(const char *s) { size_t n=0;while(s[n])n++;return n; }
char *msg_strchr(const char *s,int c) {for(;;s++){if(*s==(char)c)return (char *)s;if(!*s)return 0;}}
void *msg_memcpy(void *d,const void *s,size_t n)
{ unsigned char *to=d;const unsigned char *from=s;for(size_t i=0;i<n;i++)to[i]=from[i];return d; }
void *msg_memset(void *d,int v,size_t n)
{ unsigned char *to=d;for(size_t i=0;i<n;i++)to[i]=(unsigned char)v;return d; }
void *msg_memmove(void *d,const void *s,size_t n)
{ unsigned char *to=d;const unsigned char *from=s;if((uintptr_t)to<(uintptr_t)from)for(size_t i=0;i<n;i++)to[i]=from[i];else while(n){n--;to[n]=from[n];}return d; }
// Encoder callers are bounded QR geometry/penalty values, never INT/LONG_MIN.
int msg_abs(int n) {return n<0?-n:n;}
long msg_labs(long n) {return n<0?-n:n;}
#ifdef __XTENSA__
// GCC may emit ordinary memory calls for structure copies even when freestanding.
void *memcpy(void *,const void *,size_t) __attribute__((alias("msg_memcpy")));
void *memset(void *,int,size_t) __attribute__((alias("msg_memset")));
void *memmove(void *,const void *,size_t) __attribute__((alias("msg_memmove")));
#endif
