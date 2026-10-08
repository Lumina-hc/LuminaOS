#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYSCALL_EXIT            0
#define SYSCALL_PUTPIXEL        1
#define SYSCALL_FILL_RECT       2
#define SYSCALL_DRAW_RECT       3
#define SYSCALL_DRAW_CHAR       4
#define SYSCALL_DRAW_TEXT       5
#define SYSCALL_GFX_CLEAR       6
#define SYSCALL_GFX_FLUSH       7
#define SYSCALL_GFX_W           8
#define SYSCALL_GFX_H           9
#define SYSCALL_GETCHAR         10
#define SYSCALL_GETCHAR_NB      11
#define SYSCALL_MOUSE_X         12
#define SYSCALL_MOUSE_Y         13
#define SYSCALL_MOUSE_L         14
#define SYSCALL_MOUSE_R         15
#define SYSCALL_MOUSE_CLICK     16
#define SYSCALL_FILE_READ       17
#define SYSCALL_FILE_WRITE      18
#define SYSCALL_FILE_LIST       19
#define SYSCALL_SLEEP           20
#define SYSCALL_CURSOR          21
#define SYSCALL_WIN_INFO        22
#define SYSCALL_SYSINFO         23
#define SYSCALL_WIN_SHOULD_CLOSE 24
#define SYSCALL_OPEN_FILE       25
#define SYSCALL_GET_OPEN_FILE   26
#define SYSCALL_COUNT           27

typedef struct {
    uint32_t mem_total_kb;
    uint32_t mem_free_kb;
    uint32_t mem_used_kb;
    uint32_t heap_free_kb;
    uint32_t heap_used_kb;
} sysinfo_t;

typedef struct {
    char name[12];
    uint8_t attr;
    uint8_t pad;
    uint32_t size;
} __attribute__((packed)) dir_entry_t;

void syscall_init(void);
long syscall_dispatch(long num, long a1, long a2, long a3, long a4);
int syscall_pending_file(char* out, int max);

#endif
