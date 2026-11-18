/*
  Minimal freestanding <sys/types.h> for the LuminaOS SDL3 port.
  Provides the POSIX integer typedefs referenced by generic SDL files.
*/
#ifndef LUMINA_COMPAT_SYS_TYPES_H
#define LUMINA_COMPAT_SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t u_int64_t;
typedef uint32_t u_int32_t;
typedef uint16_t u_int16_t;
typedef uint8_t  u_int8_t;
typedef long     ssize_t;
typedef uint32_t mode_t;
typedef uint32_t ino_t;
typedef int32_t  pid_t;
typedef uint64_t off_t;
typedef int64_t  time_t;

#endif /* LUMINA_COMPAT_SYS_TYPES_H */
