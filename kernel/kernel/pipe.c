#include <kernel/pipe.h>
#include <kernel/heap.h>
#include <kernel/proc.h>

#define MAX_PIPES 8
#define PIPE_SIZE 4096

typedef struct {
    pipe_t p;
    int open[2];
} kpipe_t;

static kpipe_t pipes[MAX_PIPES];

void pipe_init(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].p.buf = 0;
        pipes[i].open[0] = pipes[i].open[1] = 0;
    }
}

int pipe_create(int fds[2])
{
    int pid = proc_current() ? proc_current()->pid : 0;
    (void)pid;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].p.buf) {
            pipes[i].p.buf = kmalloc(PIPE_SIZE);
            if (!pipes[i].p.buf) return -1;
            pipes[i].p.size = PIPE_SIZE;
            pipes[i].p.head = pipes[i].p.tail = 0;
            pipes[i].open[0] = 1;
            pipes[i].open[1] = 1;
            fds[0] = i * 2 + 0;
            fds[1] = i * 2 + 1;
            return 0;
        }
    }
    return -1;
}

static kpipe_t* fd2pipe(int fd, int* which)
{
    int pi = fd / 2;
    *which = fd % 2;
    if (pi < 0 || pi >= MAX_PIPES) return 0;
    if (!pipes[pi].p.buf) return 0;
    if (pipes[pi].open[*which] == 0) return 0;
    return &pipes[pi];
}

int pipe_read(int fd, void* dst, int len)
{
    int w;
    kpipe_t* kp = fd2pipe(fd, &w);
    if (!kp || w != 0) return -1;
    pipe_t* p = &kp->p;
    int got = 0;
    while (got < len) {
        if (p->tail == p->head) return got;
        int chunk = p->head > p->tail ? (p->head - p->tail) : (p->size - p->tail);
        if (chunk > len - got) chunk = len - got;
        for (int i = 0; i < chunk; i++) ((char*)dst)[got + i] = p->buf[p->tail + i];
        p->tail = (p->tail + chunk) % p->size;
        got += chunk;
        break;
    }
    if (got < 0) got = 0;
    return got;
}

int pipe_write(int fd, const void* src, int len)
{
    int w;
    kpipe_t* kp = fd2pipe(fd, &w);
    if (!kp || w != 1) return -1;
    pipe_t* p = &kp->p;
    int put = 0;
    while (put < len) {
        int space = p->tail > p->head ? (p->tail - p->head - 1) : (p->size - p->head - 1);
        if (space < 0) space = 0;
        if (p->head == p->tail) {
            /* full? if no space, return what we put or -EAGAIN semantics; return put if partial */
            if (space == 0 && put == 0) return 0;
            if (space == 0) return put;
        }
        int chunk = len - put;
        if (chunk > space) chunk = space;
        for (int i = 0; i < chunk; i++) p->buf[p->head + i] = ((char*)src)[put + i];
        p->head = (p->head + chunk) % p->size;
        put += chunk;
        break;
    }
    return put;
}

int pipe_close(int fd)
{
    int w;
    kpipe_t* kp = fd2pipe(fd, &w);
    if (!kp) return 0;
    kp->open[w] = 0;
    if (kp->open[0] == 0 && kp->open[1] == 0) {
        kfree(kp->p.buf);
        kp->p.buf = 0;
    }
    return 0;
}
