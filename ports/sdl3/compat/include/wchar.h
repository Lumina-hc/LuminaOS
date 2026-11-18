/*
  Minimal freestanding <wchar.h> for the LuminaOS SDL3 port.
  SDL only needs the wchar_t type and WCHAR_MIN/WCHAR_MAX macros.
  On x86_64 SysV, wchar_t is a signed int.
*/
#ifndef LUMINA_COMPAT_WCHAR_H
#define LUMINA_COMPAT_WCHAR_H

#include <stddef.h>

#ifndef __WCHAR_TYPE__
#define __WCHAR_TYPE__ int
#endif

typedef __WCHAR_TYPE__ wchar_t;

#ifndef WCHAR_MIN
#define WCHAR_MIN (-2147483647 - 1)
#endif
#ifndef WCHAR_MAX
#define WCHAR_MAX 2147483647
#endif

#endif /* LUMINA_COMPAT_WCHAR_H */
