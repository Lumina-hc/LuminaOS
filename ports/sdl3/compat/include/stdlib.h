/*
  Minimal freestanding <stdlib.h> for the LuminaOS SDL3 port.

  SDL's minimal config does not call libc malloc/free directly, but GCC's
  own <xmmintrin.h> -> <mm_malloc.h> (pulled in by SDL_intrin.h) includes
  <stdlib.h> and needs declarations for the allocation family. Declaring
  them here satisfies the header; SDL's own bundled allocator provides the
  actual SDL_malloc/SDL_free implementations used at runtime.
*/
#ifndef LUMINA_COMPAT_STDLIB_H
#define LUMINA_COMPAT_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void abort(void) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
long int strtol(const char *nptr, char **endptr, int base);
unsigned long int strtoul(const char *nptr, char **endptr, int base);
int abs(int j);
long labs(long j);

#endif /* LUMINA_COMPAT_STDLIB_H */
