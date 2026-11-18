/*
  Minimal freestanding <errno.h> for the LuminaOS SDL3 port.
  SDL's bundled dlmalloc assigns `errno = ENOMEM` on failure; a single
  global errno lvalue is sufficient in the single-threaded LUMINA build.
*/
#ifndef LUMINA_COMPAT_ERRNO_H
#define LUMINA_COMPAT_ERRNO_H

extern int errno;

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
#define EOVERFLOW 75

#endif /* LUMINA_COMPAT_ERRNO_H */
