/*
  Minimal freestanding <sched.h> for the LuminaOS SDL3 port.
  SDL's bundled dlmalloc uses sched_yield() inside its spin locks; on LUMIA
  this maps to the YIELD syscall (implemented in ports/sdl3/compat/libc.c).
*/
#ifndef LUMIA_COMPAT_SCHED_H
#define LUMIA_COMPAT_SCHED_H

int sched_yield(void);

#endif /* LUMIA_COMPAT_SCHED_H */
