/*
  Minimal freestanding <time.h> for the LuminaOS SDL3 port.
  SDL's bundled dlmalloc includes it for magic-number initialisation;
  SDL's time/timer generic code uses clock() only when HAVE_LIBC is set.
*/
#ifndef LUMIA_COMPAT_TIME_H
#define LUMIA_COMPAT_TIME_H

#include <sys/types.h>

time_t time(time_t *tloc);

#define CLOCKS_PER_SEC 1000000L

#endif /* LUMIA_COMPAT_TIME_H */
