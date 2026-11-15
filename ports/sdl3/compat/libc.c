/*
  Minimal freestanding C runtime glue for the LuminaOS SDL3 port.

  SDL's bundled implementations in src/stdlib provide SDL_malloc/dlmalloc,
  SDL_memcpy, SDL_strlen, SDL_snprintf, etc. This file only supplies the
  handful of libc symbols that SDL's bundled code and GCC call directly:
    - sbrk()      : dlmalloc MORECORE, backed by a static arena
    - errno       : plain global lvalue used by dlmalloc / SDL_iostream
    - sysconf()   : page size query from dlmalloc
    - sched_yield(): dlmalloc spinlock yield -> LuminaOS YIELD syscall
    - memcpy/memmove/memset/memcmp : dlmalloc + clients of the stdc macros
    - strerror()  : SDL_iostream error messages
    - a few POSIX stubs (lseek/read/write/close/getpid/time/_exit/abort/exit)
  All syscalls go through the LuminaOS int 0x80 ABI (see app_libc.h in the
  vendored LuminaOS-SDK).
*/
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

/* ---- int 0x80 syscall primitive (LuminaOS ABI) ------------------------- */
#define SYS_EXIT     0
#define SYS_SLEEP    20
#define SYS_YIELD    39

static inline long lumia_syscall(long n, long a1, long a2, long a3, long a4)
{
    long r;
    __asm__ volatile (
        "int $0x80"
        : "=a"(r)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "c"(a4)
        : "memory");
    return r;
}

/* ---- errno ------------------------------------------------------------- */
int errno;

char *strerror(int errnum)
{
    switch (errnum) {
    case ENOENT:  return "No such file or directory";
    case EINTR:   return "Interrupted system call";
    case EIO:     return "Input/output error";
    case EBADF:   return "Bad file descriptor";
    case EAGAIN:  return "Resource temporarily unavailable";
    case ENOMEM:  return "Cannot allocate memory";
    case EACCES:  return "Permission denied";
    case EEXIST:  return "File exists";
    case EINVAL:  return "Invalid argument";
    case ENOSYS:  return "Function not implemented";
    default:      return "Unknown error";
    }
}

/* ---- sbrk: bump allocator over a static arena --------------------------
   LuminaOS has no sbrk, so dlmalloc cannot grow the real heap. We hand it a
   fixed-size, page-aligned arena and implement the Unix sbrk() contract:
     sbrk(0)          -> current break (no change)
     sbrk(+n)         -> old break, advance by n (rounded up to a page)
     sbrk(-n)         -> attempt to shrink; supported as a simple rewind when
                         n does not cross prior allocations.
   Returns (void*)-1 and leaves the break unchanged on failure.
*/
/* Static arena backing dlmalloc. Kept modest because LuminaOS maps the whole
   .bss (LOAD segment) into physical pages when loading an ELF; 1 MiB is
   plenty for a software-framebuffer SDL app (a 320x200 XRGB8888 surface is
   256 KiB) while leaving RAM for the kernel and other processes. Override
   with -DLUMIA_SDL_HEAP_SIZE=... if an app needs more. */
#ifndef LUMIA_SDL_HEAP_SIZE
#define LUMIA_SDL_HEAP_SIZE (1u * 1024u * 1024u)
#endif

static char lumia_sdl_heap[LUMIA_SDL_HEAP_SIZE]
    __attribute__((aligned(4096)));
static char *lumia_brk = lumia_sdl_heap;
static char *lumia_brk_start = lumia_sdl_heap;
static char *lumia_brk_end = lumia_sdl_heap + LUMIA_SDL_HEAP_SIZE;

void *sbrk(long increment)
{
    char *old = lumia_brk;

    if (increment == 0) {
        return old;
    }
    if (increment > 0) {
        /* Round the request up to a page boundary for dlmalloc. */
        unsigned long incr = ((unsigned long)increment + 4095ul) & ~4095ul;
        if ((unsigned long)(lumia_brk_end - lumia_brk) < incr) {
            return (void *)-1;
        }
        lumia_brk += incr;
        return old;
    } else {
        long shr = -increment;
        /* Only allow rewinding down to the arena start. dlmalloc treats a
           failed shrink as "keep the memory", which is safe here. */
        if ((long)(lumia_brk - lumia_brk_start) < shr) {
            return (void *)-1;
        }
        lumia_brk -= shr;
        return old;
    }
}

/* ---- misc libc symbols used by SDL / GCC ------------------------------- */
int sched_yield(void)
{
    lumia_syscall(SYS_YIELD, 0, 0, 0, 0);
    return 0;
}

long sysconf(int name)
{
    (void)name;
    return 4096; /* _SC_PAGESIZE */
}

void _exit(int status)
{
    lumia_syscall(SYS_EXIT, status, 0, 0, 0);
    for (;;) { }
}

/* SDL_ExitProcess() calls this to terminate the current LUMIA process. */
void lumia_sys_exit(void)
{
    lumia_syscall(SYS_EXIT, 0, 0, 0, 0);
    for (;;) { }
}

void abort(void)
{
    _exit(134);
}

void exit(int status)
{
    _exit(status);
}

int getpid(void)
{
    /* No GETPID wrapper is exposed through the compat header; 0 is fine for
       SDL's single-process use of getpid (it is not called on LUMIA). */
    return 0;
}

long read(int fd, void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return -1;
}

long write(int fd, const void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return -1;
}

int close(int fd)
{
    (void)fd;
    return -1;
}

off_t lseek(int fd, off_t offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    return (off_t)-1;
}

/* A few generic SDL files (steam virtual gamepad, dlmalloc magic seed)
   reference these POSIX helpers; on LUMIA they are meaningful no-ops. */
int stat(const char *path, struct stat *buf)
{
    (void)path; (void)buf;
    return -1;
}

time_t time(time_t *tloc)
{
    /* dlmalloc uses time() only as a seed for its magic numbers. */
    if (tloc) {
        *tloc = 0;
    }
    return 0;
}

/* ---- memory helpers ----------------------------------------------------
   SDL's own src/stdlib/SDL_memcpy.c, SDL_memmove.c and SDL_memset.c define
   plain memcpy()/memmove()/memset() when HAVE_LIBC is off, so we must NOT
   define those here (that would be a multiple definition). Only memcmp()
   is missing from SDL's bundled set and is provided below.
*/
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y) {
            return (int)*x - (int)*y;
        }
        x++;
        y++;
    }
    return 0;
}
