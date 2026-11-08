#include <kernel/pipe.h>
#include <kernel/proc.h>
#include <kernel/heap.h>
#include <stdint.h>

#define MAX_PIPES 8

typedef struct {
    pipe_t p;
    int owner;
    int open;
} kpipe_t;

static kpipe_t pipes[MAX_PIPES];

void pipe_init(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].p.buf = 0;
        pipes[i].owner = 0;
        pipes[i].open = 0;
    }
}

int pipe_create(int rfd, int wfd, int pid)
{
    if (rfd < 0 || rfd >= MAX_PIPES || wfd < 0 || wfd >= MAX_PIPES)
        return -1;
    int pi = -1;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].p.buf)
            continue;
        if (i == rfd || i == wfd)
            continue;
        pi = i;
        break;
    }
    if (pi < 0)
        return -1;
    kpipe_t* kp = &pipes[pi];
    char* buf = kmalloc(4096);
    if (!buf) return -1;
    proc_t* cur = proc_current();
    kp->p.buf = buf;
    kp->p.size = 4096;
    kp->p.head = kp->p.tail = 0;
    kp->p.nwrite = 0;
    kp->p.nread = 0;
    kp->owner = pid;
    kp->open = 1;
    proc_set_fd(cur, rfd);
    proc_set_fd(cur, wfd);
    return 0;
}

static kpipe_t* fd2pipe(int pi, int pid)
{
    if (pi < 0 || pi >= MAX_PIPES) return 0;
    kpipe_t* kp = &pipes[pi];
    if (!kp->p.buf) return 0;
    if (kp->owner != pid) return 0;
    if (!kp->open) return 0;
    return kp;
}

int pipe_read(int fd, int pid, void* dst, int len)
{
    kpipe_t* kp = fd2pipe(fd, pid);
    if (!kp) return -1;
    pipe_t* p = &kp->p;
    int got = 0;
    if (p->tail == p->head)
        return 0;
    int chunk = p->head > p->tail ? (p->head - p->tail) : (p->size - p->tail);
    if (chunk > len) chunk = len;
    for (int i = 0; i < chunk; i++)
        ((char*)dst)[got + i] = p->buf[p->tail + i];
    p->tail = (p->tail + chunk) % p->size;
    got += chunk;
    p->nread += got;
    return got;
}

int pipe_write(int fd, int pid, const void* src, int len)
{
    kpipe_t* kp = fd2pipe(fd, pid);
    if (!kp) return -1;
    pipe_t* p = &kp->p;
    int put = 0;
    int space = p->tail > p->head ? (p->tail - p->head - 1) : (p->size - p->head - 1);
    if (space < 0) space = 0;
    if (space == 0) return 0;
    if (space > len) space = len;
    for (int i = 0; i < space; i++)
        p->buf[p->head + i] = ((const char*)src)[put + i];
    p->head = (p->head + space) % p->size;
    put += space;
    p->nwrite += put;
    return put;
}

int pipe_close(int pi, int pid)
{
    if (pi < 0 || pi >= MAX_PIPES) return -1;
    kpipe_t* kp = &pipes[pi];
    if (!kp->p.buf || kp->owner != pid) return -1;
    kp->open = 0;
    kfree(kp->p.buf);
    kp->p.buf = 0;
    return 0;
}
