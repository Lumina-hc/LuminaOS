#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>

typedef struct {
    int win_x, win_y, win_w, win_h;
    int client_x, client_y, client_w, client_h;
    int close_x, close_y, close_w, close_h;
} win_info_t;

int desktop_run(int fs_ready, const char* user);
void desktop_win_info(win_info_t* info);

#endif
