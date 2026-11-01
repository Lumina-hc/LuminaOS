#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

typedef struct {
    char* buf;
    int size;
    int head;
    int tail;
    int nwrite;
    int nread;
} pipe_t;

void pipe_init(void);
int pipe_create(int fds[2]);
int pipe_read(int fd, void* dst, int len);
int pipe_write(int fd, const void* src, int len);
int pipe_close(int fd);

#endif
