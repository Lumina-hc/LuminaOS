#include <kernel/wayland.h>
#include <kernel/pmm.h>
#include <kernel/paging.h>
#include <kernel/proc.h>
#include <kernel/timer.h>
#include <kernel/io.h>
#include <stdint.h>

#define WL_OBJ_NONE 0
#define WL_OBJ_DISPLAY 1
#define WL_OBJ_REGISTRY 2
#define WL_OBJ_CALLBACK 3
#define WL_OBJ_COMPOSITOR 4
#define WL_OBJ_SURFACE 5
#define WL_OBJ_REGION 6
#define WL_OBJ_SHM 7
#define WL_OBJ_SHM_POOL 8
#define WL_OBJ_BUFFER 9
#define WL_OBJ_SEAT 10
#define WL_OBJ_POINTER 11
#define WL_OBJ_KEYBOARD 12
#define WL_OBJ_TOUCH 13
#define WL_OBJ_OUTPUT 14
#define WL_OBJ_XDG_WM_BASE 15
#define WL_OBJ_XDG_SURFACE 16
#define WL_OBJ_XDG_TOPLEVEL 17
#define WL_OBJ_XDG_POSITIONER 18

#define WL_GLOBAL_COMPOSITOR 1
#define WL_GLOBAL_SHM 2
#define WL_GLOBAL_SEAT 3
#define WL_GLOBAL_OUTPUT 4
#define WL_GLOBAL_XDG_WM_BASE 5

#define WL_SEAT_CAP_POINTER 1
#define WL_SEAT_CAP_KEYBOARD 2

#define WL_BTN_LEFT 0x110
#define WL_BTN_RIGHT 0x111
#define WL_BTN_MIDDLE 0x112

#define WL_KEYMAP_FD 100

#define WL_KEYMAP_BASE 0x38000000ULL

typedef struct {
    uint8_t type;
    uint8_t used;
    int a;
} wl_object_t;

typedef struct {
    int used;
    int client;
    int pid;
    int handle;
    uint32_t size;
    uint32_t pages;
    uintptr_t phys;
    uint64_t uaddr;
} wl_pool_t;

typedef struct {
    int used;
    int client;
    int obj;
    int pool;
    uint32_t offset;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t* data;
    int busy;
    int destroy_pending;
} wl_buffer_t;

typedef struct {
    int used;
    int client;
    int obj;
    int xdg_surf;
    int xdg_topl;
    int pending_buffer;
    int pending_attach;
    int pending_x;
    int pending_y;
    int current_buffer;
    int configured;
    int ever_mapped;
    int mapped;
    int dirty;
    int is_new;
    int close_req;
    int pointer_entered;
    int kb_entered;
    int frame_cb[WL_MAX_FRAME_CB];
    int frame_count;
    int32_t geom_x;
    int32_t geom_y;
    int32_t geom_w;
    int32_t geom_h;
    int32_t min_w;
    int32_t min_h;
    int32_t max_w;
    int32_t max_h;
    char title[WL_MAX_TITLE];
    char app_id[WL_MAX_TITLE];
} wl_surface_t;

typedef struct {
    int used;
    int pid;
    uint32_t serial;
    uint32_t registry_obj;
    uint8_t rx[WL_RX_SIZE];
    uint32_t rx_len;
    uint8_t ev[WL_EVENT_QUEUE];
    uint32_t ev_len;
    int ev_overflow;
    int pending_fd[WL_MAX_FDS];
    int pending_fd_count;
    int pointer_obj;
    int keyboard_obj;
    int km_mapped;
    uint32_t last_mods;
    wl_object_t obj[WL_MAX_OBJECTS];
} wl_client_t;

static wl_client_t clients[WL_MAX_CLIENTS];
static wl_surface_t surfs[WL_MAX_SURFACES];
static wl_buffer_t bufs[WL_MAX_BUFFERS];
static wl_pool_t pools[WL_MAX_POOLS];
static int g_kb_focus;

static const char wl_keymap_text[] =
    "xkb_keymap {\n"
    "xkb_keycodes {\n"
    "minimum = 8;\n"
    "maximum = 255;\n"
    "<ESC> = 9;\n"
    "<AE01> = 10; <AE02> = 11; <AE03> = 12; <AE04> = 13; <AE05> = 14;\n"
    "<AE06> = 15; <AE07> = 16; <AE08> = 17; <AE09> = 18; <AE10> = 19;\n"
    "<AE11> = 20; <AE12> = 21; <BKSP> = 22; <TAB> = 23;\n"
    "<AD01> = 24; <AD02> = 25; <AD03> = 26; <AD04> = 27; <AD05> = 28;\n"
    "<AD06> = 29; <AD07> = 30; <AD08> = 31; <AD09> = 32; <AD10> = 33;\n"
    "<AD11> = 34; <AD12> = 35; <RTRN> = 36; <LCTL> = 37;\n"
    "<AC01> = 38; <AC02> = 39; <AC03> = 40; <AC04> = 41; <AC05> = 42;\n"
    "<AC06> = 43; <AC07> = 44; <AC08> = 45; <AC09> = 46;\n"
    "<AC10> = 47; <AC11> = 48; <TLDE> = 49; <LFSH> = 50; <BKSL> = 51;\n"
    "<AB01> = 52; <AB02> = 53; <AB03> = 54; <AB04> = 55; <AB05> = 56;\n"
    "<AB06> = 57; <AB07> = 58; <AB08> = 59; <AB09> = 60; <AB10> = 61;\n"
    "<RTSH> = 62; <KPMUL> = 63; <LALT> = 64; <SPCE> = 65; <CAPS> = 66;\n"
    "<FK01> = 67; <FK02> = 68; <FK03> = 69; <FK04> = 70; <FK05> = 71;\n"
    "<FK06> = 72; <FK07> = 73; <FK08> = 74; <FK09> = 75; <FK10> = 76;\n"
    "<NMLK> = 77; <SCLK> = 78;\n"
    "<KP7> = 79; <KP8> = 80; <KP9> = 81; <KPSU> = 82;\n"
    "<KP4> = 83; <KP5> = 84; <KP6> = 85; <KPAD> = 86;\n"
    "<KP1> = 87; <KP2> = 88; <KP3> = 89; <KP0> = 90; <KPDOT> = 91;\n"
    "<LSGT> = 94; <FK11> = 95; <FK12> = 96;\n"
    "<KPEN> = 104; <RCTL> = 105; <KPDV> = 106; <PRSC> = 107; <RALT> = 108;\n"
    "<HOME> = 110; <UP> = 111; <PGUP> = 112; <LEFT> = 113; <RGHT> = 114;\n"
    "<END> = 115; <DOWN> = 116; <PGDN> = 117; <INS> = 118; <DELE> = 119;\n"
    "<LWIN> = 133; <RWIN> = 134;\n"
    "};\n"
    "xkb_types {\n"
    "type \"ONE_LEVEL\" {\n"
    "modifiers = none;\n"
    "map[none] = Level1;\n"
    "level_name[Level1] = \"Any\";\n"
    "};\n"
    "type \"TWO_LEVEL\" {\n"
    "modifiers = Shift;\n"
    "map[Shift] = Level2;\n"
    "level_name[Level1] = \"Base\";\n"
    "level_name[Level2] = \"Shift\";\n"
    "};\n"
    "type \"ALPHABETIC\" {\n"
    "modifiers = Shift + Lock;\n"
    "map[Shift] = Level2;\n"
    "map[Lock] = Level2;\n"
    "preserve[Lock] = Shift;\n"
    "level_name[Level1] = \"Base\";\n"
    "level_name[Level2] = \"Caps\";\n"
    "};\n"
    "};\n"
    "xkb_compat {\n"
    "interpret Shift_L + AnyOf(all) { action = SetMods(modifiers = Shift); };\n"
    "interpret Shift_R + AnyOf(all) { action = SetMods(modifiers = Shift); };\n"
    "interpret Control_L + AnyOf(all) { action = SetMods(modifiers = Control); };\n"
    "interpret Control_R + AnyOf(all) { action = SetMods(modifiers = Control); };\n"
    "interpret Alt_L + AnyOf(all) { action = SetMods(modifiers = Mod1); };\n"
    "interpret Alt_R + AnyOf(all) { action = SetMods(modifiers = Mod1); };\n"
    "interpret Super_L + AnyOf(all) { action = SetMods(modifiers = Mod4); };\n"
    "interpret Super_R + AnyOf(all) { action = SetMods(modifiers = Mod4); };\n"
    "interpret Caps_Lock + AnyOf(all) { action = LockMods(modifiers = Lock); };\n"
    "interpret Num_Lock + AnyOf(all) { action = LockMods(modifiers = Mod2); };\n"
    "};\n"
    "xkb_symbols {\n"
    "name[group1] = \"English (US)\";\n"
    "key <ESC> { type = \"ONE_LEVEL\", symbols = [ Escape ] };\n"
    "key <AE01> { type = \"TWO_LEVEL\", symbols = [ 1, exclam ] };\n"
    "key <AE02> { type = \"TWO_LEVEL\", symbols = [ 2, at ] };\n"
    "key <AE03> { type = \"TWO_LEVEL\", symbols = [ 3, numbersign ] };\n"
    "key <AE04> { type = \"TWO_LEVEL\", symbols = [ 4, dollar ] };\n"
    "key <AE05> { type = \"TWO_LEVEL\", symbols = [ 5, percent ] };\n"
    "key <AE06> { type = \"TWO_LEVEL\", symbols = [ 6, asciicircum ] };\n"
    "key <AE07> { type = \"TWO_LEVEL\", symbols = [ 7, ampersand ] };\n"
    "key <AE08> { type = \"TWO_LEVEL\", symbols = [ 8, asterisk ] };\n"
    "key <AE09> { type = \"TWO_LEVEL\", symbols = [ 9, parenleft ] };\n"
    "key <AE10> { type = \"TWO_LEVEL\", symbols = [ 0, parenright ] };\n"
    "key <AE11> { type = \"TWO_LEVEL\", symbols = [ minus, underscore ] };\n"
    "key <AE12> { type = \"TWO_LEVEL\", symbols = [ equal, plus ] };\n"
    "key <BKSP> { type = \"ONE_LEVEL\", symbols = [ BackSpace ] };\n"
    "key <TAB> { type = \"ONE_LEVEL\", symbols = [ Tab ] };\n"
    "key <AD01> { type = \"ALPHABETIC\", symbols = [ q, Q ] };\n"
    "key <AD02> { type = \"ALPHABETIC\", symbols = [ w, W ] };\n"
    "key <AD03> { type = \"ALPHABETIC\", symbols = [ e, E ] };\n"
    "key <AD04> { type = \"ALPHABETIC\", symbols = [ r, R ] };\n"
    "key <AD05> { type = \"ALPHABETIC\", symbols = [ t, T ] };\n"
    "key <AD06> { type = \"ALPHABETIC\", symbols = [ y, Y ] };\n"
    "key <AD07> { type = \"ALPHABETIC\", symbols = [ u, U ] };\n"
    "key <AD08> { type = \"ALPHABETIC\", symbols = [ i, I ] };\n"
    "key <AD09> { type = \"ALPHABETIC\", symbols = [ o, O ] };\n"
    "key <AD10> { type = \"ALPHABETIC\", symbols = [ p, P ] };\n"
    "key <AD11> { type = \"TWO_LEVEL\", symbols = [ bracketleft, braceleft ] };\n"
    "key <AD12> { type = \"TWO_LEVEL\", symbols = [ bracketright, braceright ] };\n"
    "key <RTRN> { type = \"ONE_LEVEL\", symbols = [ Return ] };\n"
    "key <LCTL> { type = \"ONE_LEVEL\", symbols = [ Control_L ] };\n"
    "key <AC01> { type = \"ALPHABETIC\", symbols = [ a, A ] };\n"
    "key <AC02> { type = \"ALPHABETIC\", symbols = [ s, S ] };\n"
    "key <AC03> { type = \"ALPHABETIC\", symbols = [ d, D ] };\n"
    "key <AC04> { type = \"ALPHABETIC\", symbols = [ f, F ] };\n"
    "key <AC05> { type = \"ALPHABETIC\", symbols = [ g, G ] };\n"
    "key <AC06> { type = \"ALPHABETIC\", symbols = [ h, H ] };\n"
    "key <AC07> { type = \"ALPHABETIC\", symbols = [ j, J ] };\n"
    "key <AC08> { type = \"ALPHABETIC\", symbols = [ k, K ] };\n"
    "key <AC09> { type = \"ALPHABETIC\", symbols = [ l, L ] };\n"
    "key <AC10> { type = \"TWO_LEVEL\", symbols = [ semicolon, colon ] };\n"
    "key <AC11> { type = \"TWO_LEVEL\", symbols = [ apostrophe, quotedbl ] };\n"
    "key <TLDE> { type = \"TWO_LEVEL\", symbols = [ grave, asciitilde ] };\n"
    "key <LFSH> { type = \"ONE_LEVEL\", symbols = [ Shift_L ] };\n"
    "key <BKSL> { type = \"TWO_LEVEL\", symbols = [ backslash, bar ] };\n"
    "key <AB01> { type = \"ALPHABETIC\", symbols = [ z, Z ] };\n"
    "key <AB02> { type = \"ALPHABETIC\", symbols = [ x, X ] };\n"
    "key <AB03> { type = \"ALPHABETIC\", symbols = [ c, C ] };\n"
    "key <AB04> { type = \"ALPHABETIC\", symbols = [ v, V ] };\n"
    "key <AB05> { type = \"ALPHABETIC\", symbols = [ b, B ] };\n"
    "key <AB06> { type = \"ALPHABETIC\", symbols = [ n, N ] };\n"
    "key <AB07> { type = \"ALPHABETIC\", symbols = [ m, M ] };\n"
    "key <AB08> { type = \"TWO_LEVEL\", symbols = [ comma, less ] };\n"
    "key <AB09> { type = \"TWO_LEVEL\", symbols = [ period, greater ] };\n"
    "key <AB10> { type = \"TWO_LEVEL\", symbols = [ slash, question ] };\n"
    "key <RTSH> { type = \"ONE_LEVEL\", symbols = [ Shift_R ] };\n"
    "key <KPMUL> { type = \"ONE_LEVEL\", symbols = [ KP_Multiply ] };\n"
    "key <LALT> { type = \"ONE_LEVEL\", symbols = [ Alt_L ] };\n"
    "key <SPCE> { type = \"ONE_LEVEL\", symbols = [ space ] };\n"
    "key <CAPS> { type = \"ONE_LEVEL\", symbols = [ Caps_Lock ] };\n"
    "key <FK01> { type = \"ONE_LEVEL\", symbols = [ F1 ] };\n"
    "key <FK02> { type = \"ONE_LEVEL\", symbols = [ F2 ] };\n"
    "key <FK03> { type = \"ONE_LEVEL\", symbols = [ F3 ] };\n"
    "key <FK04> { type = \"ONE_LEVEL\", symbols = [ F4 ] };\n"
    "key <FK05> { type = \"ONE_LEVEL\", symbols = [ F5 ] };\n"
    "key <FK06> { type = \"ONE_LEVEL\", symbols = [ F6 ] };\n"
    "key <FK07> { type = \"ONE_LEVEL\", symbols = [ F7 ] };\n"
    "key <FK08> { type = \"ONE_LEVEL\", symbols = [ F8 ] };\n"
    "key <FK09> { type = \"ONE_LEVEL\", symbols = [ F9 ] };\n"
    "key <FK10> { type = \"ONE_LEVEL\", symbols = [ F10 ] };\n"
    "key <NMLK> { type = \"ONE_LEVEL\", symbols = [ Num_Lock ] };\n"
    "key <SCLK> { type = \"ONE_LEVEL\", symbols = [ Scroll_Lock ] };\n"
    "key <KP7> { type = \"ONE_LEVEL\", symbols = [ KP_7 ] };\n"
    "key <KP8> { type = \"ONE_LEVEL\", symbols = [ KP_8 ] };\n"
    "key <KP9> { type = \"ONE_LEVEL\", symbols = [ KP_9 ] };\n"
    "key <KPSU> { type = \"ONE_LEVEL\", symbols = [ KP_Subtract ] };\n"
    "key <KP4> { type = \"ONE_LEVEL\", symbols = [ KP_4 ] };\n"
    "key <KP5> { type = \"ONE_LEVEL\", symbols = [ KP_5 ] };\n"
    "key <KP6> { type = \"ONE_LEVEL\", symbols = [ KP_6 ] };\n"
    "key <KPAD> { type = \"ONE_LEVEL\", symbols = [ KP_Add ] };\n"
    "key <KP1> { type = \"ONE_LEVEL\", symbols = [ KP_1 ] };\n"
    "key <KP2> { type = \"ONE_LEVEL\", symbols = [ KP_2 ] };\n"
    "key <KP3> { type = \"ONE_LEVEL\", symbols = [ KP_3 ] };\n"
    "key <KP0> { type = \"ONE_LEVEL\", symbols = [ KP_0 ] };\n"
    "key <KPDOT> { type = \"ONE_LEVEL\", symbols = [ KP_Decimal ] };\n"
    "key <LSGT> { type = \"TWO_LEVEL\", symbols = [ less, greater ] };\n"
    "key <FK11> { type = \"ONE_LEVEL\", symbols = [ F11 ] };\n"
    "key <FK12> { type = \"ONE_LEVEL\", symbols = [ F12 ] };\n"
    "key <KPEN> { type = \"ONE_LEVEL\", symbols = [ KP_Enter ] };\n"
    "key <RCTL> { type = \"ONE_LEVEL\", symbols = [ Control_R ] };\n"
    "key <KPDV> { type = \"ONE_LEVEL\", symbols = [ KP_Divide ] };\n"
    "key <PRSC> { type = \"ONE_LEVEL\", symbols = [ Print ] };\n"
    "key <RALT> { type = \"ONE_LEVEL\", symbols = [ Alt_R ] };\n"
    "key <HOME> { type = \"ONE_LEVEL\", symbols = [ Home ] };\n"
    "key <UP> { type = \"ONE_LEVEL\", symbols = [ Up ] };\n"
    "key <PGUP> { type = \"ONE_LEVEL\", symbols = [ Prior ] };\n"
    "key <LEFT> { type = \"ONE_LEVEL\", symbols = [ Left ] };\n"
    "key <RGHT> { type = \"ONE_LEVEL\", symbols = [ Right ] };\n"
    "key <END> { type = \"ONE_LEVEL\", symbols = [ End ] };\n"
    "key <DOWN> { type = \"ONE_LEVEL\", symbols = [ Down ] };\n"
    "key <PGDN> { type = \"ONE_LEVEL\", symbols = [ Next ] };\n"
    "key <INS> { type = \"ONE_LEVEL\", symbols = [ Insert ] };\n"
    "key <DELE> { type = \"ONE_LEVEL\", symbols = [ Delete ] };\n"
    "key <LWIN> { type = \"ONE_LEVEL\", symbols = [ Super_L ] };\n"
    "key <RWIN> { type = \"ONE_LEVEL\", symbols = [ Super_R ] };\n"
    "};\n"
    "};\n";

static uint32_t km_size;
static uint32_t km_pages;
static uintptr_t km_phys;

static void km_copy_out(void)
{
    uint8_t* dst = (uint8_t*)km_phys;
    for (uint32_t i = 0; i < km_size; i++)
        dst[i] = (uint8_t)wl_keymap_text[i];
}

static void wl_zero(void* dst, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < n; i++)
        d[i] = 0;
}

static void wl_copy(void* dst, const void* src, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
}

static void wl_move(void* dst, const void* src, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (uint32_t i = 0; i < n; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (uint32_t i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
}

static void wl_strcpy(char* dst, const char* src, int max)
{
    int i = 0;
    if (src) {
        while (src[i] && i < max - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int wl_streq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void put_u16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_u16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t msg_begin(wl_client_t* c, uint32_t obj, uint16_t opcode)
{
    uint32_t off = c->ev_len;
    if (off + 8 > WL_EVENT_QUEUE) {
        c->ev_overflow = 1;
        return off;
    }
    put_u32(c->ev + off, obj);
    put_u16(c->ev + off + 4, 0);
    put_u16(c->ev + off + 6, opcode);
    c->ev_len = off + 8;
    return off;
}

static void msg_raw(wl_client_t* c, const uint8_t* p, uint32_t n)
{
    if (c->ev_overflow)
        return;
    if (c->ev_len + n > WL_EVENT_QUEUE) {
        c->ev_overflow = 1;
        return;
    }
    wl_copy(c->ev + c->ev_len, p, n);
    c->ev_len += n;
}

static void msg_u32(wl_client_t* c, uint32_t v)
{
    if (c->ev_overflow)
        return;
    if (c->ev_len + 4 > WL_EVENT_QUEUE) {
        c->ev_overflow = 1;
        return;
    }
    put_u32(c->ev + c->ev_len, v);
    c->ev_len += 4;
}

static void msg_i32(wl_client_t* c, int32_t v)
{
    msg_u32(c, (uint32_t)v);
}

static void msg_zero(wl_client_t* c, uint32_t n)
{
    static const uint8_t z[4] = { 0, 0, 0, 0 };
    while (n >= 4) {
        msg_raw(c, z, 4);
        n -= 4;
    }
    if (n > 0)
        msg_raw(c, z, n);
}

static void msg_str(wl_client_t* c, const char* s)
{
    uint32_t len = 0;
    if (s) {
        while (s[len])
            len++;
        len++;
    }
    msg_u32(c, len);
    if (len == 0 || c->ev_overflow)
        return;
    msg_raw(c, (const uint8_t*)s, len);
    uint32_t pad = (4 - (len & 3)) & 3;
    if (pad)
        msg_zero(c, pad);
}

static void msg_empty_array(wl_client_t* c)
{
    msg_u32(c, 0);
}

static void msg_end(wl_client_t* c, uint32_t off)
{
    if (c->ev_overflow || c->ev_len < off + 8) {
        c->ev_len = off;
        c->ev_overflow = 0;
        return;
    }
    uint32_t size = c->ev_len - off;
    put_u16(c->ev + off + 4, (uint16_t)size);
}

static wl_client_t* client_by_cid(int cid)
{
    if (cid < 0 || cid >= WL_MAX_CLIENTS)
        return 0;
    return clients[cid].used ? &clients[cid] : 0;
}

static wl_client_t* client_owned(int cid)
{
    wl_client_t* c = client_by_cid(cid);
    if (!c)
        return 0;
    proc_t* p = proc_current();
    if (!p || p->pid != c->pid)
        return 0;
    return c;
}

static wl_surface_t* surf_by_id(int id)
{
    if (id < 1 || id > WL_MAX_SURFACES)
        return 0;
    return surfs[id - 1].used ? &surfs[id - 1] : 0;
}

static int surf_alloc(int cid, int obj)
{
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (surfs[i].used)
            continue;
        wl_zero(&surfs[i], sizeof(wl_surface_t));
        surfs[i].used = 1;
        surfs[i].client = cid;
        surfs[i].obj = obj;
        surfs[i].current_buffer = -1;
        surfs[i].pending_buffer = -1;
        return i + 1;
    }
    return 0;
}

static int buffer_alloc(int cid, int obj, int pool, uint32_t offset,
                        uint32_t width, uint32_t height, uint32_t stride,
                        uint32_t format)
{
    for (int i = 0; i < WL_MAX_BUFFERS; i++) {
        if (bufs[i].used)
            continue;
        wl_zero(&bufs[i], sizeof(wl_buffer_t));
        bufs[i].used = 1;
        bufs[i].client = cid;
        bufs[i].obj = obj;
        bufs[i].pool = pool;
        bufs[i].offset = offset;
        bufs[i].width = width;
        bufs[i].height = height;
        bufs[i].stride = stride;
        bufs[i].format = format;
        bufs[i].data = (uint32_t*)(pools[pool].phys + offset);
        return i;
    }
    return -1;
}

static void client_release(wl_client_t* c, int unmap);
static void surface_destroy(wl_client_t* c, wl_surface_t* s);
static void buffer_release(wl_client_t* c, int idx);
static void send_xdg_configure(wl_client_t* c, wl_surface_t* s);
static void push_fd(wl_client_t* c, int fd);

static void send_delete_id(wl_client_t* c, uint32_t id)
{
    uint32_t off = msg_begin(c, 1, 1);
    msg_u32(c, id);
    msg_end(c, off);
}

static void release_id(wl_client_t* c, uint32_t id)
{
    if (id < WL_MAX_OBJECTS && c->obj[id].used) {
        c->obj[id].used = 0;
        send_delete_id(c, id);
    }
}

static void send_callback_done(wl_client_t* c, uint32_t id, uint32_t serial)
{
    if (!c->obj[id].used || c->obj[id].type != WL_OBJ_CALLBACK)
        return;
    c->obj[id].used = 0;
    uint32_t off = msg_begin(c, id, 0);
    msg_u32(c, serial);
    msg_end(c, off);
    send_delete_id(c, id);
}

static void buffer_release(wl_client_t* c, int idx)
{
    if (idx < 0 || idx >= WL_MAX_BUFFERS)
        return;
    wl_buffer_t* b = &bufs[idx];
    if (!b->used)
        return;
    b->busy = 0;
    if (b->destroy_pending) {
        b->used = 0;
        return;
    }
    if (b->obj >= 0 && c->obj[b->obj].used) {
        uint32_t off = msg_begin(c, (uint32_t)b->obj, 0);
        msg_end(c, off);
    }
}

static void surface_destroy(wl_client_t* c, wl_surface_t* s)
{
    int id = (int)(s - surfs) + 1;
    if (s->current_buffer >= 0)
        buffer_release(c, s->current_buffer);
    for (int i = 0; i < s->frame_count; i++)
        release_id(c, (uint32_t)s->frame_cb[i]);
    if (g_kb_focus == id)
        g_kb_focus = 0;
    release_id(c, (uint32_t)s->obj);
    s->used = 0;
}

static void send_registry_global(wl_client_t* c, uint32_t name,
                                 const char* iface, uint32_t version)
{
    uint32_t off = msg_begin(c, c->registry_obj, 0);
    msg_u32(c, name);
    msg_str(c, iface);
    msg_u32(c, version);
    msg_end(c, off);
}

static void send_shm_format(wl_client_t* c, uint32_t obj, uint32_t format)
{
    uint32_t off = msg_begin(c, obj, 0);
    msg_u32(c, format);
    msg_end(c, off);
}

static void send_seat_caps(wl_client_t* c, uint32_t obj, uint32_t caps)
{
    uint32_t off = msg_begin(c, obj, 0);
    msg_u32(c, caps);
    msg_end(c, off);
}

static void send_seat_name(wl_client_t* c, uint32_t obj, const char* name)
{
    uint32_t off = msg_begin(c, obj, 1);
    msg_str(c, name);
    msg_end(c, off);
}

static void send_output_geometry(wl_client_t* c, uint32_t obj)
{
    uint32_t off = msg_begin(c, obj, 0);
    msg_i32(c, 0);
    msg_i32(c, 0);
    msg_i32(c, 340);
    msg_i32(c, 190);
    msg_i32(c, 0);
    msg_str(c, "Lumina");
    msg_str(c, "Virtual-1");
    msg_i32(c, 0);
    msg_end(c, off);
}

static void send_output_mode(wl_client_t* c, uint32_t obj, int w, int h)
{
    uint32_t off = msg_begin(c, obj, 1);
    msg_u32(c, 3);
    msg_i32(c, w);
    msg_i32(c, h);
    msg_i32(c, 60000);
    msg_end(c, off);
}

static void send_output_done(wl_client_t* c, uint32_t obj)
{
    uint32_t off = msg_begin(c, obj, 2);
    msg_end(c, off);
}

static void send_output_scale(wl_client_t* c, uint32_t obj, int scale)
{
    uint32_t off = msg_begin(c, obj, 3);
    msg_i32(c, scale);
    msg_end(c, off);
}

static void send_output_name(wl_client_t* c, uint32_t obj)
{
    uint32_t off = msg_begin(c, obj, 4);
    msg_str(c, "WL-1");
    msg_end(c, off);
}

static void send_output_description(wl_client_t* c, uint32_t obj)
{
    uint32_t off = msg_begin(c, obj, 5);
    msg_str(c, "LuminaOS virtual output");
    msg_end(c, off);
}

static void send_xdg_ping(wl_client_t* c, uint32_t obj, uint32_t serial)
{
    uint32_t off = msg_begin(c, obj, 0);
    msg_u32(c, serial);
    msg_end(c, off);
}

static void send_xdg_surface_configure(wl_client_t* c, wl_surface_t* s,
                                       uint32_t serial)
{
    if (s->xdg_surf < 0 || !c->obj[s->xdg_surf].used)
        return;
    uint32_t off = msg_begin(c, (uint32_t)s->xdg_surf, 0);
    msg_u32(c, serial);
    msg_end(c, off);
}

static void send_xdg_toplevel_configure(wl_client_t* c, wl_surface_t* s,
                                        int width, int height)
{
    if (s->xdg_topl < 0 || !c->obj[s->xdg_topl].used)
        return;
    uint32_t off = msg_begin(c, (uint32_t)s->xdg_topl, 0);
    msg_i32(c, width);
    msg_i32(c, height);
    msg_empty_array(c);
    msg_end(c, off);
}

static void send_xdg_toplevel_close(wl_client_t* c, wl_surface_t* s)
{
    if (s->xdg_topl < 0 || !c->obj[s->xdg_topl].used)
        return;
    uint32_t off = msg_begin(c, (uint32_t)s->xdg_topl, 1);
    msg_end(c, off);
}

static void send_xdg_configure(wl_client_t* c, wl_surface_t* s)
{
    uint32_t serial = c->serial++;
    s->configured = 1;
    send_xdg_surface_configure(c, s, serial);
    int w = 640;
    int h = 480;
    if (s->current_buffer >= 0) {
        w = (int)bufs[s->current_buffer].width;
        h = (int)bufs[s->current_buffer].height;
    }
    send_xdg_toplevel_configure(c, s, w, h);
}

static void send_pointer_enter(wl_client_t* c, int obj, uint32_t surface,
                               int x, int y)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 0);
    msg_u32(c, c->serial++);
    msg_u32(c, surface);
    msg_i32(c, x * 256);
    msg_i32(c, y * 256);
    msg_end(c, off);
}

static void send_pointer_leave(wl_client_t* c, int obj, uint32_t surface)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 1);
    msg_u32(c, c->serial++);
    msg_u32(c, surface);
    msg_end(c, off);
}

static void send_pointer_motion(wl_client_t* c, int obj, int x, int y)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 2);
    msg_u32(c, timer_ticks());
    msg_i32(c, x * 256);
    msg_i32(c, y * 256);
    msg_end(c, off);
}

static void send_pointer_button(wl_client_t* c, int obj, int button,
                                int down, int x, int y)
{
    (void)x;
    (void)y;
    uint32_t off = msg_begin(c, (uint32_t)obj, 3);
    msg_u32(c, c->serial++);
    msg_u32(c, timer_ticks());
    msg_u32(c, (uint32_t)button);
    msg_u32(c, down ? 1u : 0u);
    msg_end(c, off);
}

static void send_pointer_frame(wl_client_t* c, int obj)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 5);
    msg_end(c, off);
}

static void send_keyboard_enter(wl_client_t* c, int obj, uint32_t surface)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 1);
    msg_u32(c, c->serial++);
    msg_u32(c, surface);
    msg_empty_array(c);
    msg_end(c, off);
}

static void send_keyboard_leave(wl_client_t* c, int obj, uint32_t surface)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 2);
    msg_u32(c, c->serial++);
    msg_u32(c, surface);
    msg_end(c, off);
}

static void send_keyboard_key(wl_client_t* c, int obj, int key, int down)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 3);
    msg_u32(c, c->serial++);
    msg_u32(c, timer_ticks());
    msg_u32(c, (uint32_t)key);
    msg_u32(c, down ? 1u : 0u);
    msg_end(c, off);
}

static void send_keyboard_mods(wl_client_t* c, int obj, uint32_t mods)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 4);
    msg_u32(c, c->serial++);
    msg_u32(c, mods);
    msg_u32(c, 0);
    msg_u32(c, 0);
    msg_u32(c, 0);
    msg_end(c, off);
}

static void send_keyboard_repeat(wl_client_t* c, int obj)
{
    uint32_t off = msg_begin(c, (uint32_t)obj, 5);
    msg_i32(c, 25);
    msg_i32(c, 600);
    msg_end(c, off);
}

static void send_keyboard_keymap(wl_client_t* c, int obj)
{
    if (!km_phys)
        return;
    if (obj < 0 || !c->obj[obj].used)
        return;
    push_fd(c, WL_KEYMAP_FD);
    static int km_logged;
    if (!km_logged) {
        km_logged = 1;
        debug_puts("[wl] keymap sent\n");
    }
    uint32_t off = msg_begin(c, (uint32_t)obj, 0);
    msg_u32(c, 1);
    msg_u32(c, WL_KEYMAP_FD);
    msg_u32(c, km_size);
    msg_end(c, off);
}

typedef struct {
    const uint8_t* p;
    uint32_t left;
    int bad;
} wl_args_t;

static uint32_t arg_u32(wl_args_t* a)
{
    if (a->bad || a->left < 4) {
        a->bad = 1;
        return 0;
    }
    uint32_t v = get_u32(a->p);
    a->p += 4;
    a->left -= 4;
    return v;
}

static int32_t arg_i32(wl_args_t* a)
{
    return (int32_t)arg_u32(a);
}

static const char* arg_str(wl_args_t* a, uint32_t* len)
{
    uint32_t n = arg_u32(a);
    if (a->bad)
        return 0;
    if (n == 0) {
        *len = 0;
        return 0;
    }
    if (n > a->left) {
        a->bad = 1;
        return 0;
    }
    const char* s = (const char*)a->p;
    uint32_t pad = (n + 3) & ~3u;
    if (pad > a->left) {
        a->bad = 1;
        return 0;
    }
    a->p += pad;
    a->left -= pad;
    *len = n;
    return s;
}

static const uint8_t* arg_array(wl_args_t* a, uint32_t* len)
{
    uint32_t n = arg_u32(a);
    if (a->bad)
        return 0;
    if (n == 0) {
        *len = 0;
        return 0;
    }
    if (n > a->left) {
        a->bad = 1;
        return 0;
    }
    const uint8_t* p = a->p;
    uint32_t pad = (n + 3) & ~3u;
    if (pad > a->left) {
        a->bad = 1;
        return 0;
    }
    a->p += pad;
    a->left -= pad;
    *len = n;
    return p;
}

static int arg_fd(wl_client_t* c)
{
    if (c->pending_fd_count <= 0)
        return -1;
    int fd = c->pending_fd[0];
    for (int i = 1; i < c->pending_fd_count; i++)
        c->pending_fd[i - 1] = c->pending_fd[i];
    c->pending_fd_count--;
    return fd;
}

static int obj_add(wl_client_t* c, uint32_t id, int type, int a)
{
    if (id < 2 || id >= WL_MAX_OBJECTS)
        return 0;
    if (c->obj[id].used)
        return 0;
    c->obj[id].used = 1;
    c->obj[id].type = (uint8_t)type;
    c->obj[id].a = a;
    return 1;
}

static int find_pool(wl_client_t* c, int handle)
{
    for (int i = 0; i < WL_MAX_POOLS; i++) {
        if (pools[i].used && pools[i].client == (c - clients) &&
            pools[i].handle == handle)
            return i;
    }
    return -1;
}

static void commit_surface(wl_client_t* c, wl_surface_t* s)
{
    if (!s->configured) {
        if (s->xdg_topl >= 0)
            send_xdg_configure(c, s);
        else
            s->configured = 1;
    }

    if (s->pending_attach) {
        int old = s->current_buffer;
        int nb = s->pending_buffer;
        s->pending_attach = 0;
        s->pending_buffer = -1;

        if (nb >= 0 && bufs[nb].used) {
            s->current_buffer = nb;
            bufs[nb].busy = 1;
            s->mapped = 1;
            s->dirty = 1;
            if (!s->ever_mapped) {
                s->ever_mapped = 1;
                s->is_new = 1;
            }
        } else {
            s->current_buffer = -1;
            s->mapped = 0;
            s->dirty = 1;
        }

        if (old >= 0 && old != s->current_buffer)
            buffer_release(c, old);
    }
}

static void handle_surface(wl_client_t* c, wl_surface_t* s, uint16_t opcode,
                           wl_args_t* a)
{
    (void)c;
    switch (opcode) {
    case 0:
        surface_destroy(c, s);
        break;
    case 1: {
        uint32_t buffer = arg_u32(a);
        int32_t x = arg_i32(a);
        int32_t y = arg_i32(a);
        if (a->bad)
            return;
        s->pending_attach = 1;
        s->pending_x = x;
        s->pending_y = y;
        if (buffer == 0) {
            s->pending_buffer = -1;
        } else if (buffer < WL_MAX_OBJECTS && c->obj[buffer].used &&
                   c->obj[buffer].type == WL_OBJ_BUFFER) {
            s->pending_buffer = c->obj[buffer].a;
        } else {
            s->pending_buffer = -1;
        }
        break;
    }
    case 2:
        (void)arg_i32(a);
        (void)arg_i32(a);
        (void)arg_i32(a);
        (void)arg_i32(a);
        break;
    case 3: {
        uint32_t id = arg_u32(a);
        if (a->bad || !obj_add(c, id, WL_OBJ_CALLBACK, (int)(s - surfs) + 1))
            return;
        if (s->frame_count < WL_MAX_FRAME_CB)
            s->frame_cb[s->frame_count++] = (int)id;
        break;
    }
    case 4:
    case 5:
        (void)arg_u32(a);
        break;
    case 6:
        commit_surface(c, s);
        break;
    case 7:
    case 8:
        (void)arg_i32(a);
        break;
    case 9:
        (void)arg_i32(a);
        (void)arg_i32(a);
        (void)arg_i32(a);
        (void)arg_i32(a);
        break;
    case 10:
        (void)arg_i32(a);
        (void)arg_i32(a);
        break;
    default:
        break;
    }
}

static void handle_xdg_toplevel(wl_client_t* c, wl_surface_t* s,
                                uint16_t opcode, wl_args_t* a)
{
    switch (opcode) {
    case 0:
        if (s->xdg_topl >= 0)
            release_id(c, (uint32_t)s->xdg_topl);
        s->xdg_topl = -1;
        break;
    case 1:
        (void)arg_u32(a);
        break;
    case 2: {
        uint32_t len = 0;
        const char* str = arg_str(a, &len);
        if (str && len > 0)
            wl_strcpy(s->title, str, WL_MAX_TITLE);
        break;
    }
    case 3: {
        uint32_t len = 0;
        const char* str = arg_str(a, &len);
        if (str && len > 0)
            wl_strcpy(s->app_id, str, WL_MAX_TITLE);
        break;
    }
    case 4:
        (void)arg_u32(a);
        (void)arg_u32(a);
        (void)arg_i32(a);
        (void)arg_i32(a);
        break;
    case 5:
        (void)arg_u32(a);
        (void)arg_u32(a);
        break;
    case 6:
        (void)arg_u32(a);
        (void)arg_u32(a);
        (void)arg_u32(a);
        break;
    case 7:
        s->max_w = arg_i32(a);
        s->max_h = arg_i32(a);
        break;
    case 8:
        s->min_w = arg_i32(a);
        s->min_h = arg_i32(a);
        break;
    default:
        break;
    }
}

static void handle_message(wl_client_t* c, const uint8_t* m, uint32_t size)
{
    uint32_t id = get_u32(m);
    uint16_t opcode = get_u16(m + 6);
    wl_args_t a;
    a.p = m + 8;
    a.left = size - 8;
    a.bad = 0;

    if (id >= WL_MAX_OBJECTS || !c->obj[id].used)
        return;

    wl_object_t* o = &c->obj[id];

    switch (o->type) {
    case WL_OBJ_DISPLAY:
        if (opcode == 0) {
            uint32_t new_id = arg_u32(&a);
            if (!a.bad && obj_add(c, new_id, WL_OBJ_CALLBACK, 0))
                send_callback_done(c, new_id, c->serial++);
        } else if (opcode == 1) {
            uint32_t new_id = arg_u32(&a);
            if (!a.bad && obj_add(c, new_id, WL_OBJ_REGISTRY, 0)) {
                c->registry_obj = new_id;
                send_registry_global(c, WL_GLOBAL_COMPOSITOR, "wl_compositor", 4);
                send_registry_global(c, WL_GLOBAL_SHM, "wl_shm", 1);
                send_registry_global(c, WL_GLOBAL_SEAT, "wl_seat", 5);
                send_registry_global(c, WL_GLOBAL_OUTPUT, "wl_output", 4);
                send_registry_global(c, WL_GLOBAL_XDG_WM_BASE, "xdg_wm_base", 3);
            }
        }
        break;

    case WL_OBJ_REGISTRY:
        if (opcode == 0) {
            uint32_t name = arg_u32(&a);
            uint32_t len = 0;
            const char* iface = arg_str(&a, &len);
            uint32_t version = arg_u32(&a);
            uint32_t new_id = arg_u32(&a);
            (void)name;
            if (a.bad || !iface)
                break;
            if (wl_streq(iface, "wl_compositor")) {
                if (version > 4)
                    version = 4;
                obj_add(c, new_id, WL_OBJ_COMPOSITOR, (int)version);
            } else if (wl_streq(iface, "wl_shm")) {
                if (version > 1)
                    version = 1;
                if (obj_add(c, new_id, WL_OBJ_SHM, (int)version)) {
                    send_shm_format(c, new_id, 0);
                    send_shm_format(c, new_id, 1);
                }
            } else if (wl_streq(iface, "wl_seat")) {
                if (version > 5)
                    version = 5;
                if (obj_add(c, new_id, WL_OBJ_SEAT, (int)version)) {
                    send_seat_caps(c, new_id, WL_SEAT_CAP_POINTER | WL_SEAT_CAP_KEYBOARD);
                    send_seat_name(c, new_id, "seat0");
                }
            } else if (wl_streq(iface, "wl_output")) {
                if (version > 4)
                    version = 4;
                if (obj_add(c, new_id, WL_OBJ_OUTPUT, (int)version)) {
                    send_output_geometry(c, new_id);
                    send_output_mode(c, new_id, 800, 600);
                    send_output_scale(c, new_id, 1);
                    send_output_name(c, new_id);
                    send_output_description(c, new_id);
                    send_output_done(c, new_id);
                }
            } else if (wl_streq(iface, "xdg_wm_base")) {
                if (version > 3)
                    version = 3;
                obj_add(c, new_id, WL_OBJ_XDG_WM_BASE, (int)version);
            }
        }
        break;

    case WL_OBJ_CALLBACK:
        break;

    case WL_OBJ_COMPOSITOR:
        if (opcode == 0) {
            uint32_t new_id = arg_u32(&a);
            if (a.bad)
                break;
            int sid = surf_alloc((int)(c - clients), (int)new_id);
            if (sid > 0) {
                surfs[sid - 1].xdg_surf = -1;
                surfs[sid - 1].xdg_topl = -1;
                wl_strcpy(surfs[sid - 1].title, "Wayland", WL_MAX_TITLE);
                if (!obj_add(c, new_id, WL_OBJ_SURFACE, sid))
                    surfs[sid - 1].used = 0;
            }
        } else if (opcode == 1) {
            uint32_t new_id = arg_u32(&a);
            if (!a.bad)
                obj_add(c, new_id, WL_OBJ_REGION, 0);
        }
        break;

    case WL_OBJ_SURFACE:
        handle_surface(c, &surfs[o->a - 1], opcode, &a);
        break;

    case WL_OBJ_REGION:
        if (opcode == 0)
            release_id(c, id);
        break;

    case WL_OBJ_SHM:
        if (opcode == 0) {
            uint32_t new_id = arg_u32(&a);
            int handle = arg_fd(c);
            int32_t size = arg_i32(&a);
            (void)size;
            if (a.bad || new_id < 2)
                break;
            int pi = find_pool(c, handle);
            if (pi >= 0)
                obj_add(c, new_id, WL_OBJ_SHM_POOL, pi);
        } else if (opcode == 1) {
            release_id(c, id);
        }
        break;

    case WL_OBJ_SHM_POOL:
        if (opcode == 0) {
            uint32_t new_id = arg_u32(&a);
            int32_t offset = arg_i32(&a);
            int32_t width = arg_i32(&a);
            int32_t height = arg_i32(&a);
            int32_t stride = arg_i32(&a);
            uint32_t format = arg_u32(&a);
            int pi = o->a;
            if (a.bad || new_id < 2 || pi < 0 || pi >= WL_MAX_POOLS)
                break;
            if (!pools[pi].used || width <= 0 || height <= 0 || stride <= 0)
                break;
            if (offset < 0 || format > 1 || (stride & 3))
                break;
            uint64_t need = (uint64_t)(uint32_t)offset +
                            (uint64_t)(uint32_t)stride * (uint32_t)height;
            if (need > pools[pi].size)
                break;
            int bi = buffer_alloc((int)(c - clients), (int)new_id, pi,
                                  (uint32_t)offset, (uint32_t)width,
                                  (uint32_t)height, (uint32_t)stride, format);
            if (bi >= 0 && !obj_add(c, new_id, WL_OBJ_BUFFER, bi))
                bufs[bi].used = 0;
        } else if (opcode == 1) {
            release_id(c, id);
        }
        break;

    case WL_OBJ_BUFFER:
        if (opcode == 0) {
            int bi = o->a;
            if (bi >= 0 && bi < WL_MAX_BUFFERS) {
                bufs[bi].obj = -1;
                if (bufs[bi].busy)
                    bufs[bi].destroy_pending = 1;
                else
                    bufs[bi].used = 0;
            }
            release_id(c, id);
        }
        break;

    case WL_OBJ_SEAT:
        if (opcode == 0) {
            uint32_t new_id = arg_u32(&a);
            if (!a.bad && obj_add(c, new_id, WL_OBJ_POINTER, 0))
                c->pointer_obj = (int)new_id;
        } else if (opcode == 1) {
            uint32_t new_id = arg_u32(&a);
            if (!a.bad && obj_add(c, new_id, WL_OBJ_KEYBOARD, 0)) {
                c->keyboard_obj = (int)new_id;
                send_keyboard_keymap(c, (int)new_id);
                send_keyboard_repeat(c, (int)new_id);
            }
        } else if (opcode == 2) {
            uint32_t new_id = arg_u32(&a);
            if (!a.bad)
                obj_add(c, new_id, WL_OBJ_TOUCH, 0);
        } else if (opcode == 3) {
            release_id(c, id);
        }
        break;

    case WL_OBJ_POINTER:
        if (opcode == 1) {
            c->pointer_obj = 0;
            release_id(c, id);
        }
        break;

    case WL_OBJ_KEYBOARD:
        if (opcode == 0) {
            c->keyboard_obj = 0;
            release_id(c, id);
        }
        break;

    case WL_OBJ_OUTPUT:
        if (opcode == 0)
            release_id(c, id);
        break;

    case WL_OBJ_XDG_WM_BASE:
        if (opcode == 0) {
            release_id(c, id);
        } else if (opcode == 1) {
            uint32_t new_id = arg_u32(&a);
            if (!a.bad)
                obj_add(c, new_id, WL_OBJ_XDG_POSITIONER, 0);
        } else if (opcode == 2) {
            uint32_t new_id = arg_u32(&a);
            uint32_t surface = arg_u32(&a);
            if (a.bad || surface >= WL_MAX_OBJECTS)
                break;
            if (!c->obj[surface].used || c->obj[surface].type != WL_OBJ_SURFACE)
                break;
            int sid = c->obj[surface].a;
            if (!obj_add(c, new_id, WL_OBJ_XDG_SURFACE, sid))
                break;
            surfs[sid - 1].xdg_surf = (int)new_id;
        } else if (opcode == 3) {
            (void)arg_u32(&a);
        }
        break;

    case WL_OBJ_XDG_SURFACE:
        if (opcode == 0) {
            int sid = o->a;
            if (sid >= 1 && sid <= WL_MAX_SURFACES)
                surfs[sid - 1].xdg_surf = -1;
            release_id(c, id);
        } else if (opcode == 1) {
            uint32_t new_id = arg_u32(&a);
            int sid = o->a;
            if (a.bad || sid < 1 || sid > WL_MAX_SURFACES)
                break;
            if (!obj_add(c, new_id, WL_OBJ_XDG_TOPLEVEL, sid))
                break;
            surfs[sid - 1].xdg_topl = (int)new_id;
        } else if (opcode == 2) {
            (void)arg_u32(&a);
            (void)arg_u32(&a);
            (void)arg_u32(&a);
        } else if (opcode == 3) {
            int sid = o->a;
            if (sid < 1 || sid > WL_MAX_SURFACES)
                break;
            wl_surface_t* s = &surfs[sid - 1];
            s->geom_x = arg_i32(&a);
            s->geom_y = arg_i32(&a);
            s->geom_w = arg_i32(&a);
            s->geom_h = arg_i32(&a);
        } else if (opcode == 4) {
            (void)arg_u32(&a);
        }
        break;

    case WL_OBJ_XDG_TOPLEVEL: {
        int sid = o->a;
        if (sid < 1 || sid > WL_MAX_SURFACES)
            break;
        handle_xdg_toplevel(c, &surfs[sid - 1], opcode, &a);
        break;
    }

    case WL_OBJ_XDG_POSITIONER:
        if (opcode == 0)
            release_id(c, id);
        break;

    case WL_OBJ_TOUCH:
        break;

    default:
        break;
    }
}

void wl_init(void)
{
    wl_zero(clients, sizeof(clients));
    wl_zero(surfs, sizeof(surfs));
    wl_zero(bufs, sizeof(bufs));
    wl_zero(pools, sizeof(pools));
    g_kb_focus = 0;

    km_size = (uint32_t)sizeof(wl_keymap_text) - 1;
    km_pages = (km_size + 4095) / 4096;
    km_phys = pmm_alloc_contig(km_pages);
    if (km_phys)
        km_copy_out();
}

int wl_connect(int pid)
{
    for (int i = 0; i < WL_MAX_CLIENTS; i++) {
        if (clients[i].used && clients[i].pid == pid)
            return i;
    }
    for (int i = 0; i < WL_MAX_CLIENTS; i++) {
        if (clients[i].used)
            continue;
        wl_zero(&clients[i], sizeof(wl_client_t));
        clients[i].used = 1;
        clients[i].pid = pid;
        clients[i].serial = 1;
        clients[i].pointer_obj = 0;
        clients[i].keyboard_obj = 0;
        clients[i].obj[1].used = 1;
        clients[i].obj[1].type = WL_OBJ_DISPLAY;
        clients[i].obj[1].a = 0;
        return i;
    }
    return -1;
}

int wl_send(int cid, const void* data, uint32_t len)
{
    wl_client_t* c = client_owned(cid);
    if (!c || !data)
        return -1;
    if (len > WL_RX_SIZE - c->rx_len)
        return -1;
    wl_copy(c->rx + c->rx_len, data, len);
    c->rx_len += len;

    uint32_t used = 0;
    while (c->rx_len - used >= 8) {
        const uint8_t* m = c->rx + used;
        uint32_t size = get_u16(m + 4);
        if (size < 8 || size > c->rx_len - used)
            break;
        if (size & 3) {
            used = c->rx_len;
            break;
        }
        handle_message(c, m, size);
        used += size;
    }
    if (used > 0) {
        wl_move(c->rx, c->rx + used, c->rx_len - used);
        c->rx_len -= used;
    }
    return (int)len;
}

int wl_recv(int cid, void* data, uint32_t len)
{
    wl_client_t* c = client_owned(cid);
    if (!c || !data)
        return -1;
    uint32_t n = c->ev_len;
    if (n > len)
        n = len;
    if (n > 0) {
        wl_copy(data, c->ev, n);
        wl_move(c->ev, c->ev + n, c->ev_len - n);
        c->ev_len -= n;
    }
    return (int)n;
}

static void push_fd(wl_client_t* c, int fd)
{
    if (c->pending_fd_count >= WL_MAX_FDS)
        return;
    c->pending_fd[c->pending_fd_count++] = fd;
}

int wl_send_fd(int cid, int fd)
{
    wl_client_t* c = client_owned(cid);
    if (!c || fd < 0)
        return -1;
    push_fd(c, fd);
    return 0;
}

int wl_shm_alloc(int cid, uint32_t size)
{
    wl_client_t* c = client_owned(cid);
    if (!c || size == 0 || size > WL_SHM_MAX_SIZE)
        return -1;

    int pi = -1;
    for (int i = 0; i < WL_MAX_POOLS; i++) {
        if (!pools[i].used) {
            pi = i;
            break;
        }
    }
    if (pi < 0)
        return -1;

    uint32_t pages = (size + 4095) / 4096;
    uintptr_t phys = pmm_alloc_contig(pages);
    if (!phys)
        return -1;

    uint64_t uaddr = WL_SHM_SLOT_BASE + (uint64_t)pi * WL_SHM_SLOT_SIZE;
    for (uint32_t p = 0; p < pages; p++) {
        uint64_t va = uaddr + (uint64_t)p * 4096;
        uint64_t pa = (uint64_t)phys + (uint64_t)p * 4096;
        if (!paging_map_user_page(va, pa, PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
            for (uint32_t q = 0; q < p; q++)
                paging_unmap_user_page(uaddr + (uint64_t)q * 4096);
            for (uint32_t q = 0; q < pages; q++)
                pmm_free(phys + (uintptr_t)q * 4096);
            return -1;
        }
    }

    wl_pool_t* pool = &pools[pi];
    wl_zero(pool, sizeof(wl_pool_t));
    pool->used = 1;
    pool->client = cid;
    pool->pid = c->pid;
    pool->handle = pi + 1;
    pool->size = size;
    pool->pages = pages;
    pool->phys = phys;
    pool->uaddr = uaddr;
    return pool->handle;
}

uint64_t wl_shm_map(int cid, int fd)
{
    wl_client_t* c = client_owned(cid);
    if (!c)
        return 0;
    if (fd == WL_KEYMAP_FD) {
        if (!km_phys)
            return 0;
        if (!c->km_mapped) {
            for (uint32_t p = 0; p < km_pages; p++) {
                uint64_t va = WL_KEYMAP_BASE + (uint64_t)p * 4096;
                if (!paging_map_user_page(va, (uint64_t)km_phys + (uint64_t)p * 4096,
                                          PAGE_PRESENT | PAGE_USER))
                    return 0;
            }
            c->km_mapped = 1;
        }
        return WL_KEYMAP_BASE;
    }
    int pi = find_pool(c, fd);
    if (pi < 0)
        return 0;
    return pools[pi].uaddr;
}

static void client_release(wl_client_t* c, int unmap)
{
    int cid = (int)(c - clients);

    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (!surfs[i].used || surfs[i].client != cid)
            continue;
        int sid = i + 1;
        if (g_kb_focus == sid)
            g_kb_focus = 0;
        surfs[i].used = 0;
    }

    for (int i = 0; i < WL_MAX_BUFFERS; i++) {
        if (bufs[i].used && bufs[i].client == cid)
            bufs[i].used = 0;
    }

    for (int i = 0; i < WL_MAX_POOLS; i++) {
        if (!pools[i].used || pools[i].client != cid)
            continue;
        if (unmap) {
            for (uint32_t p = 0; p < pools[i].pages; p++)
                paging_unmap_user_page(pools[i].uaddr + (uint64_t)p * 4096);
        }
        for (uint32_t p = 0; p < pools[i].pages; p++)
            pmm_free(pools[i].phys + (uintptr_t)p * 4096);
        pools[i].used = 0;
    }

    if (unmap && c->km_mapped) {
        for (uint32_t p = 0; p < km_pages; p++)
            paging_unmap_user_page(WL_KEYMAP_BASE + (uint64_t)p * 4096);
        c->km_mapped = 0;
    }

    wl_zero(c, sizeof(wl_client_t));
}

void wl_disconnect(int cid)
{
    wl_client_t* c = client_owned(cid);
    if (!c)
        return;
    client_release(c, 1);
}

void wl_release_pid(int pid)
{
    for (int i = 0; i < WL_MAX_CLIENTS; i++) {
        if (clients[i].used && clients[i].pid == pid)
            client_release(&clients[i], 0);
    }
}

int wl_next_surface(int after, wl_surface_info_t* out)
{
    if (!out)
        return 0;
    for (int i = after; i < WL_MAX_SURFACES; i++) {
        wl_surface_t* s = &surfs[i];
        if (!s->used || s->xdg_topl < 0)
            continue;
        wl_client_t* c = client_by_cid(s->client);
        if (!c)
            continue;
        wl_zero(out, sizeof(wl_surface_info_t));
        out->id = i + 1;
        out->client_id = s->client;
        out->pid = c->pid;
        out->surface_id = s->obj;
        out->toplevel_id = s->xdg_topl;
        out->mapped = s->mapped;
        out->is_new = s->is_new;
        out->dirty = s->dirty;
        out->close_req = s->close_req;
        wl_strcpy(out->title, s->title, WL_MAX_TITLE);
        if (s->current_buffer >= 0) {
            wl_buffer_t* b = &bufs[s->current_buffer];
            out->w = (int)b->width;
            out->h = (int)b->height;
            out->stride = (int)(b->stride / 4);
            out->pixels = b->data;
        }
        return i + 1;
    }
    return 0;
}

void wl_surface_clear_new(int id)
{
    wl_surface_t* s = surf_by_id(id);
    if (s)
        s->is_new = 0;
}

void wl_surface_clear_dirty(int id)
{
    wl_surface_t* s = surf_by_id(id);
    if (s)
        s->dirty = 0;
}

void wl_surface_presented(int id)
{
    wl_surface_t* s = surf_by_id(id);
    if (!s)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c)
        return;
    s->dirty = 0;
    s->is_new = 0;
    for (int i = 0; i < s->frame_count; i++) {
        int cb = s->frame_cb[i];
        send_callback_done(c, (uint32_t)cb, c->serial++);
    }
    s->frame_count = 0;
}

void wl_input_enter(int id, int x, int y)
{
    wl_surface_t* s = surf_by_id(id);
    if (!s || !s->mapped)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c || !c->pointer_obj)
        return;
    if (!s->pointer_entered) {
        send_pointer_enter(c, c->pointer_obj, (uint32_t)s->obj, x, y);
        s->pointer_entered = 1;
    }
}

void wl_input_motion(int id, int x, int y)
{
    wl_surface_t* s = surf_by_id(id);
    if (!s || !s->mapped)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c || !c->pointer_obj)
        return;
    if (!s->pointer_entered) {
        send_pointer_enter(c, c->pointer_obj, (uint32_t)s->obj, x, y);
        s->pointer_entered = 1;
    }
    send_pointer_motion(c, c->pointer_obj, x, y);
    send_pointer_frame(c, c->pointer_obj);
}

void wl_input_leave(int id)
{
    wl_surface_t* s = surf_by_id(id);
    if (!s)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c)
        return;
    if (!s->pointer_entered)
        return;
    if (c->pointer_obj) {
        send_pointer_leave(c, c->pointer_obj, (uint32_t)s->obj);
        send_pointer_frame(c, c->pointer_obj);
    }
    s->pointer_entered = 0;
}

void wl_input_button(int id, int button, int down, int x, int y)
{
    wl_surface_t* s = surf_by_id(id);
    if (!s || !s->mapped)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c || !c->pointer_obj)
        return;
    if (!s->pointer_entered) {
        send_pointer_enter(c, c->pointer_obj, (uint32_t)s->obj, x, y);
        s->pointer_entered = 1;
    }
    send_pointer_button(c, c->pointer_obj, button, down, x, y);
    send_pointer_frame(c, c->pointer_obj);
}

void wl_input_keyboard_focus(int id)
{
    if (id == g_kb_focus)
        return;

    wl_surface_t* old = surf_by_id(g_kb_focus);
    if (old) {
        wl_client_t* oc = client_by_cid(old->client);
        if (oc && oc->keyboard_obj && old->kb_entered)
            send_keyboard_leave(oc, oc->keyboard_obj, (uint32_t)old->obj);
        old->kb_entered = 0;
    }

    g_kb_focus = id;

    wl_surface_t* s = surf_by_id(id);
    if (!s)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c || !c->keyboard_obj)
        return;
    send_keyboard_keymap(c, c->keyboard_obj);
    send_keyboard_enter(c, c->keyboard_obj, (uint32_t)s->obj);
    send_keyboard_mods(c, c->keyboard_obj, c->last_mods);
    s->kb_entered = 1;
}

void wl_input_key(int id, int key, int down, uint32_t mods)
{
    wl_surface_t* s = surf_by_id(id);
    if (!s)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c || !c->keyboard_obj)
        return;
    if (mods != c->last_mods) {
        c->last_mods = mods;
        send_keyboard_mods(c, c->keyboard_obj, mods);
    }
    send_keyboard_key(c, c->keyboard_obj, key, down);
}

void wl_input_close(int id)
{
    wl_surface_t* s = surf_by_id(id);
    if (!s)
        return;
    wl_client_t* c = client_by_cid(s->client);
    if (!c)
        return;
    s->close_req = 1;
    send_xdg_toplevel_close(c, s);
}
