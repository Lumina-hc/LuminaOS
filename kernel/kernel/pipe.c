#include "kernel/pipe.h"
#include "kernel/heap.h"
#include "kernel/proc.h"
#include <stdint.h>

#define MAX_PIPES 8
#define PIPE_SIZE 4096

typedef struct {
    uint8_t* buf;
    int size;
    int head;
    int tail;
} kpipe_t;

static kpipe_t pipes[MAX_PIPES];

extern void kernel_resume(void);

void pipe_init(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].buf = 0;
    }
}

int pipe_create(int fds[2])
{
    proc_t* p = proc_current();
    if (!p) return -1;

    int skip = -1;
    int fd_a = proc_fd_slot(p, &skip);
    if (fd_a < 0) return -1;

    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].buf) {
            uint8_t* buf = kmalloc(PIPE_SIZE);
            if (!buf) {
                proc_set_fd(p, fd_a, -1);
                return -1;
            }
            pipes[i].buf = buf;
            pipes[i].size = PIPE_SIZE;
            pipes[i].head = 0;
            pipes[i].tail = 0;
            int fd_b = proc_fd_slot(p, &skip);
            if (fd_b < 0) {
                kfree(buf);
                pipes[i].buf = 0;
                proc_set_fd(p, fd_a, -1);
                return -1;
            }
            proc_set_fd(p, fd_a, i * 2 + 0);
            proc_set_fd(p, fd_b, i * 2 + 1);
            fds[0] = fd_a;
            fds[1] = fd_b;
            return 0;
        }
    }

    proc_set_fd(p, fd_a, -1);
    return -1;
}

static int pipe_of_fd(proc_t* p, int fd)
{
    int owner = proc_get_fd(p, fd);
    if (owner < 0) return -1;
    int pi = owner / 2;
    if (pi < 0 || pi >= MAX_PIPES) return -1;
    if (!pipes[pi].buf) return -1;
    return owner;
}

static int pipe_fd_held(int owner_fd)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        proc_t* q = proc_at(i);
        if (!q || q->state == PROC_UNUSED) continue;
        for (int f = 0; f < 16; f++) {
            if (q->fds[f] == owner_fd) return 1;
        }
    }
    return 0;
}

static kpipe_t* lookup_pipe(int pipe_id)
{
    if (pipe_id < 0 || pipe_id >= MAX_PIPES) return 0;
    if (!pipes[pipe_id].buf) return 0;
    return &pipes[pipe_id];
}

int pipe_read(int fd, void* dst, int len)
{
    proc_t* p = proc_current();
    if (!p) return -1;

    int owner = pipe_of_fd(p, fd);
    if (owner < 0) return -1;
    int pipe_id = owner / 2;
    int which = owner % 2;
    if (which != 0) return -1;

    kpipe_t* kp = lookup_pipe(pipe_id);
    if (!kp) return -1;
    int write_fd = pipe_id * 2 + 1;

    while (1) {
        if (!pipe_fd_held(write_fd) && kp->tail == kp->head)
            return 0;
        if (kp->tail != kp->head) {
            int chunk = kp->head > kp->tail
                ? (kp->head - kp->tail)
                : (kp->size - kp->tail);
            if (chunk > len) chunk = len;
            for (int i = 0; i < chunk; i++)
                ((uint8_t*)dst)[i] = kp->buf[kp->tail + i];
            kp->tail = (kp->tail + chunk) % kp->size;
            proc_wake_pipe_owner(pipe_id, 1);
            return chunk;
        }
        p->state = PROC_BLOCKED;
        p->has_ctx = 1;
        proc_set_current(p);
        kernel_resume();
    }
}

int pipe_write(int fd, const void* src, int len)
{
    proc_t* p = proc_current();
    if (!p) return -1;

    int owner = pipe_of_fd(p, fd);
    if (owner < 0) return -1;
    int pipe_id = owner / 2;
    int which = owner % 2;
    if (which != 1) return -1;

    kpipe_t* kp = lookup_pipe(pipe_id);
    if (!kp) return -1;
    int read_fd = pipe_id * 2;

    while (1) {
        if (!pipe_fd_held(read_fd))
            return -1;
        int space = kp->tail > kp->head
            ? (kp->tail - kp->head - 1)
            : (kp->size - kp->head - 1);
        if (space < 0) space = 0;
        if (space == 0) {
            p->state = PROC_BLOCKED;
            p->has_ctx = 1;
            proc_set_current(p);
            kernel_resume();
            continue;
        }
        int chunk = len;
        if (chunk > space) chunk = space;
        for (int i = 0; i < chunk; i++)
            kp->buf[kp->head + i] = ((const uint8_t*)src)[i];
        kp->head = (kp->head + chunk) % kp->size;
        proc_wake_pipe_owner(pipe_id, 0);
        return chunk;
    }
}

int pipe_close(int fd)
{
    proc_t* p = proc_current();
    if (!p) return -1;

    int owner = proc_get_fd(p, fd);
    if (owner < 0) return -1;

    proc_set_fd(p, fd, -1);

    int pipe_id = owner / 2;
    int which = owner % 2;
    int other_fd = pipe_id * 2 + (1 - which);

    kpipe_t* kp = lookup_pipe(pipe_id);
    if (!kp) return -1;

    if (pipe_fd_held(other_fd)) {
        proc_wake_pipe_owner(pipe_id, 1 - which);
    } else {
        kfree(kp->buf);
        kp->buf = 0;
        kp->head = 0;
        kp->tail = 0;
    }

    return 0;
}
