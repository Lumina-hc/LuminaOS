/*
  Minimal freestanding <string.h> for the LuminaOS SDL3 port.

  SDL's "minimal" configuration leaves the HAVE_MEM and HAVE_STR macros
  undefined, so SDL uses its own bundled implementations from
  src/stdlib/SDL_string.c and never calls the libc versions. This header
  only has to satisfy the unconditional `#include <string.h>` in
  SDL_stdinc.h and pull in size_t / NULL.
*/
#ifndef LUMIA_COMPAT_STRING_H
#define LUMIA_COMPAT_STRING_H

#include <stddef.h>

/* SDL_iostream.c reports fd errors with strerror()/errno but does not
   include <errno.h> in the no-libc config, so expose the needed macros here. */
extern int errno;
char *strerror(int errnum);
#define EPERM   1
#define ENOENT  2
#define EINTR   4
#define EIO     5
#define EBADF   9
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EEXIST  17
#define EINVAL  22
#define ENOSYS  38

#endif /* LUMIA_COMPAT_STRING_H */
