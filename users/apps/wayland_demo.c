#include <app_libc.h>
#include "../include/users/wl_client.h"

#define W 480
#define H 320
#define STRIDE (W * 4)
#define POOL_SIZE (STRIDE * H * 2)

#define ID_REG 2
#define ID_SYNC 3
#define ID_COMP 10
#define ID_SHM 11
#define ID_SEAT 12
#define ID_OUTPUT 13
#define ID_WM 14
#define ID_POOL 20
#define ID_BUF0 21
#define ID_BUF1 22
#define ID_SURF 30
#define ID_XSURF 31
#define ID_TOP 32
#define ID_POINTER 40
#define ID_KEYBOARD 41
#define ID_FRAME 50

static wl_conn_t conn;
static uint32_t* fb[2];
static int buf_busy[2];
static int configured;
static int running = 1;
static int frame_pending;
static int bound_comp;
static int bound_shm;
static int bound_seat;
static int bound_wm;
static int sync_done;
static int ptr_x = W / 2;
static int ptr_y = H / 2;
static int ptr_down;
static int last_key = -1;
static void* keymap_ptr;
static uint32_t ticks;
static int sq_x[3] = { 60, 210, 360 };
static int sq_y[3] = { 60, 170, 100 };
static int sq_vx[3] = { 3, -2, 4 };
static int sq_vy[3] = { 2, 3, -2 };

static int str_eq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void fill_rect(uint32_t* p, int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0)
        return;
    for (int j = 0; j < h; j++) {
        uint32_t* d = p + (y + j) * W + x;
        for (int i = 0; i < w; i++)
            d[i] = c;
    }
}

static void dot(uint32_t* p, int x, int y, uint32_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H)
        p[y * W + x] = c;
}

static void ring(uint32_t* p, int cx, int cy, int r, uint32_t c)
{
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
        dot(p, cx + x, cy + y, c);
        dot(p, cx + y, cy + x, c);
        dot(p, cx - y, cy + x, c);
        dot(p, cx - x, cy + y, c);
        dot(p, cx - x, cy - y, c);
        dot(p, cx - y, cy - x, c);
        dot(p, cx + y, cy - x, c);
        dot(p, cx + x, cy - y, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void draw_frame(uint32_t* p)
{
    int shift = (int)(ticks * 2);
    for (int y = 0; y < H; y++) {
        int t = y * 96 / H;
        int r = 10 + t / 3;
        int g = 16 + t / 2;
        int b = 34 + t;
        for (int x = 0; x < W; x++) {
            int band = (x + y * 2 + shift) & 255;
            uint32_t c = (uint32_t)((r << 16) | (g << 8) | b);
            if (band < 14)
                c |= 0x00181818;
            p[y * W + x] = c;
        }
    }

    for (int i = 0; i < 3; i++) {
        sq_x[i] += sq_vx[i];
        sq_y[i] += sq_vy[i];
        if (sq_x[i] < 0 || sq_x[i] + 34 > W) {
            sq_vx[i] = -sq_vx[i];
            sq_x[i] += sq_vx[i];
        }
        if (sq_y[i] < 0 || sq_y[i] + 34 > H) {
            sq_vy[i] = -sq_vy[i];
            sq_y[i] += sq_vy[i];
        }
        uint32_t base = (i == 0) ? 0x4C8DFF : (i == 1 ? 0x39C48B : 0xE0913A);
        fill_rect(p, sq_x[i], sq_y[i], 34, 34, base);
        fill_rect(p, sq_x[i] + 9, sq_y[i] + 9, 16, 16, 0xFFFFFF);
    }

    uint32_t pc = ptr_down ? 0xFF6B6B : 0xFFFFFF;
    ring(p, ptr_x, ptr_y, ptr_down ? 12 : 9, pc);
    fill_rect(p, ptr_x - 1, ptr_y - 1, 3, 3, pc);
    fill_rect(p, ptr_x - 18, ptr_y, 10, 1, pc);
    fill_rect(p, ptr_x + 9, ptr_y, 10, 1, pc);
    fill_rect(p, ptr_x, ptr_y - 18, 1, 10, pc);
    fill_rect(p, ptr_x, ptr_y + 9, 1, 10, pc);

    if (last_key >= 0) {
        int k = last_key % 6;
        for (int i = 0; i < 6; i++) {
            uint32_t c = (i == k) ? 0xFFD166 : 0x2A3344;
            fill_rect(p, 16 + i * 22, H - 30, 18, 18, c);
        }
    }

    if (keymap_ptr)
        fill_rect(p, W - 14, 8, 6, 6, 0x39C48B);
}

static void send_get_registry(void)
{
    wl_msg_t m;
    wl_msg_begin(&m, WL_DISPLAY, 1);
    wl_msg_u32(&m, ID_REG);
    wl_msg_send(&conn, &m);
}

static void send_sync(void)
{
    wl_msg_t m;
    wl_msg_begin(&m, WL_DISPLAY, 0);
    wl_msg_u32(&m, ID_SYNC);
    wl_msg_send(&conn, &m);
}

static void bind_global(uint32_t name, const char* iface, uint32_t version)
{
    uint32_t id = 0;
    if (str_eq(iface, "wl_compositor")) {
        id = ID_COMP;
        if (version > 4) version = 4;
        bound_comp = 1;
    } else if (str_eq(iface, "wl_shm")) {
        id = ID_SHM;
        if (version > 1) version = 1;
        bound_shm = 1;
    } else if (str_eq(iface, "wl_seat")) {
        id = ID_SEAT;
        if (version > 5) version = 5;
        bound_seat = 1;
    } else if (str_eq(iface, "wl_output")) {
        id = ID_OUTPUT;
        if (version > 4) version = 4;
    } else if (str_eq(iface, "xdg_wm_base")) {
        id = ID_WM;
        if (version > 3) version = 3;
        bound_wm = 1;
    } else {
        return;
    }

    wl_msg_t m;
    wl_msg_begin(&m, ID_REG, 0);
    wl_msg_u32(&m, name);
    wl_msg_str(&m, iface);
    wl_msg_u32(&m, version);
    wl_msg_u32(&m, id);
    wl_msg_send(&conn, &m);
}

static void handle_event(const uint8_t* m, uint32_t size)
{
    uint32_t id = wl_be32(m);
    uint16_t op = wl_be16(m + 6);
    wl_args_t a;
    a.p = m + 8;
    a.left = size - 8;

    if (id == ID_REG && op == 0) {
        uint32_t name = wl_arg_u32(&a);
        uint32_t len = 0;
        const char* iface = wl_arg_str(&a, &len);
        uint32_t version = wl_arg_u32(&a);
        if (iface)
            bind_global(name, iface, version);
    } else if (id == ID_SYNC && op == 0) {
        (void)wl_arg_u32(&a);
        sync_done = 1;
    } else if (id == ID_SHM && op == 0) {
        (void)wl_arg_u32(&a);
    } else if (id == ID_SEAT && op == 0) {
        (void)wl_arg_u32(&a);
    } else if (id == ID_SEAT && op == 1) {
        uint32_t len = 0;
        (void)wl_arg_str(&a, &len);
    } else if (id == ID_WM && op == 0) {
        uint32_t serial = wl_arg_u32(&a);
        wl_msg_t r;
        wl_msg_begin(&r, ID_WM, 3);
        wl_msg_u32(&r, serial);
        wl_msg_send(&conn, &r);
    } else if (id == ID_XSURF && op == 0) {
        uint32_t serial = wl_arg_u32(&a);
        configured = 1;
        wl_msg_t r;
        wl_msg_begin(&r, ID_XSURF, 4);
        wl_msg_u32(&r, serial);
        wl_msg_send(&conn, &r);
    } else if (id == ID_TOP && op == 0) {
        (void)wl_arg_i32(&a);
        (void)wl_arg_i32(&a);
    } else if (id == ID_TOP && op == 1) {
        running = 0;
    } else if (id == ID_POINTER && op == 0) {
        (void)wl_arg_u32(&a);
        (void)wl_arg_u32(&a);
        ptr_x = wl_arg_i32(&a) / 256;
        ptr_y = wl_arg_i32(&a) / 256;
    } else if (id == ID_POINTER && op == 1) {
    } else if (id == ID_POINTER && op == 2) {
        (void)wl_arg_u32(&a);
        ptr_x = wl_arg_i32(&a) / 256;
        ptr_y = wl_arg_i32(&a) / 256;
    } else if (id == ID_POINTER && op == 3) {
        (void)wl_arg_u32(&a);
        (void)wl_arg_u32(&a);
        uint32_t button = wl_arg_u32(&a);
        uint32_t state = wl_arg_u32(&a);
        if (button == 0x110)
            ptr_down = (state == 1);
    } else if (id == ID_KEYBOARD && op == 0) {
        /* wl_keyboard.keymap: format, fd, size — map the XKB keymap. */
        (void)wl_arg_u32(&a);
        uint32_t fd = wl_arg_u32(&a);
        (void)wl_arg_u32(&a);
        keymap_ptr = wl_shm_map(&conn, (int)fd);
    } else if (id == ID_KEYBOARD && op == 3) {
        (void)wl_arg_u32(&a);
        (void)wl_arg_u32(&a);
        uint32_t key = wl_arg_u32(&a);
        uint32_t state = wl_arg_u32(&a);
        if (state == 1) {
            last_key = (int)key;
            if (key == 1)
                running = 0;
        }
    } else if (id == ID_FRAME && op == 0) {
        (void)wl_arg_u32(&a);
        frame_pending = 0;
    } else if (id == ID_BUF0 && op == 0) {
        buf_busy[0] = 0;
    } else if (id == ID_BUF1 && op == 0) {
        buf_busy[1] = 0;
    }
}

static void pump(void)
{
    if (wl_conn_recv(&conn) <= 0)
        return;

    uint32_t off = 0;
    while (conn.rx_len - off >= 8) {
        const uint8_t* m = conn.rx + off;
        uint32_t size = wl_be16(m + 4);
        if (size < 8 || size > conn.rx_len - off)
            break;
        handle_event(m, size);
        off += size;
    }
    if (off > 0)
        wl_conn_consume(&conn, off);
}

static void create_buffer(uint32_t id, uint32_t offset)
{
    wl_msg_t m;
    wl_msg_begin(&m, ID_POOL, 0);
    wl_msg_u32(&m, id);
    wl_msg_i32(&m, (int32_t)offset);
    wl_msg_i32(&m, W);
    wl_msg_i32(&m, H);
    wl_msg_i32(&m, STRIDE);
    wl_msg_u32(&m, 1);
    wl_msg_send(&conn, &m);
}

static void setup(void)
{
    int fd = wl_shm_alloc(&conn, POOL_SIZE);
    if (fd < 0) {
        running = 0;
        return;
    }
    void* base = wl_shm_map(&conn, fd);
    if (!base) {
        running = 0;
        return;
    }
    fb[0] = (uint32_t*)base;
    fb[1] = (uint32_t*)((uint8_t*)base + STRIDE * H);
    buf_busy[0] = 0;
    buf_busy[1] = 0;

    wl_send_fd(&conn, fd);

    wl_msg_t m;
    wl_msg_begin(&m, ID_SHM, 0);
    wl_msg_u32(&m, ID_POOL);
    wl_msg_i32(&m, POOL_SIZE);
    wl_msg_send(&conn, &m);

    create_buffer(ID_BUF0, 0);
    create_buffer(ID_BUF1, STRIDE * H);

    wl_msg_begin(&m, ID_COMP, 0);
    wl_msg_u32(&m, ID_SURF);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_WM, 2);
    wl_msg_u32(&m, ID_XSURF);
    wl_msg_u32(&m, ID_SURF);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_XSURF, 1);
    wl_msg_u32(&m, ID_TOP);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_TOP, 2);
    wl_msg_str(&m, "Wayland Demo");
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_TOP, 3);
    wl_msg_str(&m, "os.lumina.wayland_demo");
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_TOP, 8);
    wl_msg_i32(&m, 160);
    wl_msg_i32(&m, 120);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_TOP, 7);
    wl_msg_i32(&m, 1920);
    wl_msg_i32(&m, 1080);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_SEAT, 0);
    wl_msg_u32(&m, ID_POINTER);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_SEAT, 1);
    wl_msg_u32(&m, ID_KEYBOARD);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_SURF, 6);
    wl_msg_send(&conn, &m);
}

static int pick_buffer(void)
{
    if (!buf_busy[0])
        return 0;
    if (!buf_busy[1])
        return 1;
    return -1;
}

static void submit_frame(void)
{
    int b = pick_buffer();
    if (b < 0)
        return;

    uint32_t* p = fb[b];
    draw_frame(p);
    int id = (b == 0) ? ID_BUF0 : ID_BUF1;
    buf_busy[b] = 1;

    wl_msg_t m;
    wl_msg_begin(&m, ID_SURF, 1);
    wl_msg_u32(&m, id);
    wl_msg_i32(&m, 0);
    wl_msg_i32(&m, 0);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_SURF, 2);
    wl_msg_i32(&m, 0);
    wl_msg_i32(&m, 0);
    wl_msg_i32(&m, W);
    wl_msg_i32(&m, H);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_SURF, 3);
    wl_msg_u32(&m, ID_FRAME);
    wl_msg_send(&conn, &m);

    wl_msg_begin(&m, ID_SURF, 6);
    wl_msg_send(&conn, &m);

    frame_pending = 1;
    ticks++;
}

void _start(void)
{
    if (wl_conn_open(&conn) < 0) {
        sys_exit();
        return;
    }

    send_get_registry();
    send_sync();

    while (!sync_done && running) {
        pump();
        sys_sleep(4);
    }

    if (!bound_comp || !bound_shm || !bound_seat || !bound_wm) {
        running = 0;
    } else {
        setup();
    }

    while (running) {
        pump();
        if (configured && !frame_pending)
            submit_frame();
        sys_sleep(8);
    }

    wl_conn_close(&conn);
    sys_exit();
}
