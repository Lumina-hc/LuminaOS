/*
  Minimal freestanding <stdio.h> for the LuminaOS SDL3 port.
  SDL's core io layer uses only SEEK_SET/CUR/END and the EOF constant.
  A few generic files reference FILE / snprintf declarations too, so those
  are declared here for type-checking; the LUMIA drivers never call them.
*/
#ifndef LUMIA_COMPAT_STDIO_H
#define LUMIA_COMPAT_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF     (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct lumia_compat_file FILE;

int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);

#endif /* LUMIA_COMPAT_STDIO_H */
