#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>

typedef struct {
    int win_x, win_y, win_w, win_h;
    int client_x, client_y, client_w, client_h;
    int close_x, close_y, close_w, close_h;
    int min_x, min_y, min_w, min_h;
} win_info_t;

#include "proc.h"
int desktop_run(int fs_ready, const char* user);
void desktop_splash(void);
void desktop_win_info(win_info_t* info);
proc_t* desktop_foreground(void);
void desktop_dbg_dump(void);

#endif
