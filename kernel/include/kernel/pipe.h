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
int pipe_create(int rfd, int wfd, int pid);
int pipe_read(int fd, int pid, void* dst, int len);
int pipe_write(int fd, int pid, const void* src, int len);
int pipe_close(int pi, int pid);

#endif
