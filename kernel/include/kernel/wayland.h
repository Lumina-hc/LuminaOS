#ifndef WAYLAND_H
#define WAYLAND_H

#include <stdint.h>

#define WL_MAX_CLIENTS 4
#define WL_MAX_OBJECTS 256
#define WL_MAX_SURFACES 8
#define WL_MAX_BUFFERS 24
#define WL_MAX_POOLS 8
#define WL_MAX_FDS 8
#define WL_MAX_FRAME_CB 4
#define WL_MAX_TITLE 64
#define WL_RX_SIZE 8192
#define WL_EVENT_QUEUE 16384
#define WL_SHM_SLOT_BASE 0x30000000ULL
#define WL_SHM_SLOT_SIZE 0x01000000ULL
#define WL_SHM_MAX_SIZE 0x01000000UL

typedef struct {
    int id;
    int client_id;
    int pid;
    int surface_id;
    int toplevel_id;
    int x;
    int y;
    int w;
    int h;
    int stride;
    int mapped;
    int is_new;
    int dirty;
    int close_req;
    uint32_t* pixels;
    char title[WL_MAX_TITLE];
} wl_surface_info_t;

void wl_init(void);

int wl_connect(int pid);
int wl_send(int cid, const void* data, uint32_t len);
int wl_recv(int cid, void* data, uint32_t len);
int wl_send_fd(int cid, int fd);
int wl_shm_alloc(int cid, uint32_t size);
uint64_t wl_shm_map(int cid, int fd);
void wl_disconnect(int cid);
void wl_release_pid(int pid);

int wl_next_surface(int after, wl_surface_info_t* out);
void wl_surface_clear_new(int id);
void wl_surface_clear_dirty(int id);
void wl_surface_presented(int id);

void wl_input_enter(int id, int x, int y);
void wl_input_motion(int id, int x, int y);
void wl_input_leave(int id);
void wl_input_button(int id, int button, int down, int x, int y);
void wl_input_keyboard_focus(int id);
void wl_input_key(int id, int key, int down, uint32_t mods);
void wl_input_close(int id);

#endif
