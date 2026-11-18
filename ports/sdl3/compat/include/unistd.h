/*
  Minimal freestanding <unistd.h> for the LuminaOS SDL3 port.

  SDL includes <unistd.h> in many core files (for _exit, getpid, sleep,
  read/write, ...). LuminaOS exposes these through the int 0x80 syscall ABI
  in app_libc.h; this header only supplies the prototypes so the generic SDL
  code compiles. Files that would actually call these either use the LUMINA
  drivers (which call the syscalls directly) or are compiled out.
*/
#ifndef LUMINA_COMPAT_UNISTD_H
#define LUMINA_COMPAT_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

/* POSIX seek origins (also referenced by SDL_iostream.c via unistd.h). */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* dlmalloc queries the page size through sysconf(_SC_PAGESIZE). */
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE

void _exit(int status) __attribute__((noreturn));
off_t lseek(int fd, off_t offset, int whence);

/* dlmalloc's MORECORE: a bump allocator backed by a static arena, provided
   in ports/sdl3/compat/libc.c. */
void *sbrk(long increment);

/* dlmalloc defines LACKS_STRING_H yet still calls these directly; our compat
   libc implements them (also used by SDL clients through the stdc macros). */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
long sysconf(int name);
int getpid(void);
unsigned int sleep(unsigned int seconds);
int close(int fd);
long read(int fd, void *buf, size_t count);
long write(int fd, const void *buf, size_t count);

#endif /* LUMINA_COMPAT_UNISTD_H */
