#ifndef WL_CLIENT_H
#define WL_CLIENT_H

#include <app_libc.h>
#include <stdint.h>

#define WL_SYS_CONNECT 40
#define WL_SYS_SEND 41
#define WL_SYS_RECV 42
#define WL_SYS_SEND_FD 43
#define WL_SYS_SHM_ALLOC 44
#define WL_SYS_SHM_MAP 45
#define WL_SYS_DISCONNECT 46

#define WL_DISPLAY 1
#define WL_MAX_RX 8192

typedef struct {
    int cid;
    uint32_t next_id;
    uint8_t rx[WL_MAX_RX];
    uint32_t rx_len;
} wl_conn_t;

typedef struct {
    uint8_t buf[2048];
    uint32_t len;
} wl_msg_t;

typedef struct {
    const uint8_t* p;
    uint32_t left;
} wl_args_t;

static inline uint16_t wl_be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t wl_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void wl_put16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static inline void wl_put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline int wl_conn_open(wl_conn_t* c)
{
    c->cid = (int)syscall5(WL_SYS_CONNECT, 0, 0, 0, 0);
    c->next_id = 2;
    c->rx_len = 0;
    return c->cid;
}

static inline void wl_conn_close(wl_conn_t* c)
{
    syscall5(WL_SYS_DISCONNECT, c->cid, 0, 0, 0);
    c->cid = -1;
}

static inline int wl_conn_recv(wl_conn_t* c)
{
    if (c->rx_len >= WL_MAX_RX)
        return -1;
    int n = (int)syscall5(WL_SYS_RECV, c->cid,
                          (long)(c->rx + c->rx_len),
                          (long)(WL_MAX_RX - c->rx_len), 0);
    if (n > 0)
        c->rx_len += (uint32_t)n;
    return n;
}

static inline void wl_conn_consume(wl_conn_t* c, uint32_t n)
{
    if (n >= c->rx_len) {
        c->rx_len = 0;
        return;
    }
    for (uint32_t i = 0; i < c->rx_len - n; i++)
        c->rx[i] = c->rx[i + n];
    c->rx_len -= n;
}

static inline void wl_msg_begin(wl_msg_t* m, uint32_t obj, uint16_t opcode)
{
    m->len = 8;
    wl_put32(m->buf + 0, obj);
    wl_put16(m->buf + 4, 0);
    wl_put16(m->buf + 6, opcode);
}

static inline void wl_msg_u32(wl_msg_t* m, uint32_t v)
{
    wl_put32(m->buf + m->len, v);
    m->len += 4;
}

static inline void wl_msg_i32(wl_msg_t* m, int32_t v)
{
    wl_msg_u32(m, (uint32_t)v);
}

static inline void wl_msg_byte(wl_msg_t* m, uint8_t v)
{
    m->buf[m->len++] = v;
}

static inline void wl_msg_str(wl_msg_t* m, const char* s)
{
    uint32_t n = 0;
    if (s) {
        while (s[n])
            n++;
        n++;
    }
    wl_msg_u32(m, n);
    for (uint32_t i = 0; i < n; i++)
        wl_msg_byte(m, (uint8_t)s[i]);
    while (m->len & 3)
        wl_msg_byte(m, 0);
}

static inline void wl_msg_empty_array(wl_msg_t* m)
{
    wl_msg_u32(m, 0);
}

static inline int wl_msg_send(wl_conn_t* c, wl_msg_t* m)
{
    wl_put16(m->buf + 4, (uint16_t)m->len);
    return (int)syscall5(WL_SYS_SEND, c->cid, (long)m->buf, m->len, 0);
}

static inline int wl_send_fd(wl_conn_t* c, int fd)
{
    return (int)syscall5(WL_SYS_SEND_FD, c->cid, fd, 0, 0);
}

static inline int wl_shm_alloc(wl_conn_t* c, uint32_t size)
{
    return (int)syscall5(WL_SYS_SHM_ALLOC, c->cid, size, 0, 0);
}

static inline void* wl_shm_map(wl_conn_t* c, int fd)
{
    return (void*)syscall5(WL_SYS_SHM_MAP, c->cid, fd, 0, 0);
}

static inline uint32_t wl_arg_u32(wl_args_t* a)
{
    if (a->left < 4)
        return 0;
    uint32_t v = wl_be32(a->p);
    a->p += 4;
    a->left -= 4;
    return v;
}

static inline int32_t wl_arg_i32(wl_args_t* a)
{
    return (int32_t)wl_arg_u32(a);
}

static inline const char* wl_arg_str(wl_args_t* a, uint32_t* len)
{
    uint32_t n = wl_arg_u32(a);
    if (n == 0) {
        *len = 0;
        return 0;
    }
    if (n > a->left) {
        a->left = 0;
        *len = 0;
        return 0;
    }
    const char* s = (const char*)a->p;
    uint32_t pad = (n + 3) & ~3u;
    if (pad > a->left) {
        a->left = 0;
        *len = 0;
        return 0;
    }
    a->p += pad;
    a->left -= pad;
    *len = n;
    return s;
}

#endif
