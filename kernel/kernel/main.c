#include <kernel/tty.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/isr.h>
#include <kernel/keyboard.h>
#include <kernel/system.h>
#include <kernel/fat16.h>
#include <kernel/pmm.h>
#include <kernel/paging.h>
#include <kernel/heap.h>
#include <kernel/gdt.h>
#include <kernel/syscall.h>
#include <kernel/gfx.h>
#include <kernel/mouse.h>
#include <kernel/desktop.h>
#include <kernel/proc.h>
#include <kernel/paging.h>
#include <kernel/gdt.h>
#include <kernel/io.h>
#include <kernel/timer.h>
#include <kernel/sched.h>
#include <stdint.h>

extern void enter_user_mode(uint64_t rip, uint64_t rsp, uint64_t cs, uint64_t ss, uint64_t rflags);

static int str_equ(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_len(const char* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char* dst, const char* src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void print_uint(uint32_t value)
{
    char buf[10];
    int len = 0;
    if (value == 0) {
        tty_putchar('0');
        return;
    }
    while (value > 0) {
        buf[len++] = '0' + value % 10;
        value /= 10;
    }
    while (len > 0)
        tty_putchar(buf[--len]);
}

static void print_banner(void)
{
    tty_puts("============================================\n");
    tty_puts("         LuminaOS v0.7.1 - 64-bit\n");
    tty_puts("============================================\n\n");
    tty_puts("Type 'help' for commands\n\n");
}

static int fs_ready = 0;

static char cur_user[16];
static char path_stack[16][16];
static int path_depth = 0;

static void cmd_ls(void)
{
    fat16_entry_t entries[64];
    int count;
    if (fat16_list_dir(entries, 64, &count) != 0) {
        tty_puts("Failed to read directory\n");
        return;
    }
    if (count == 0) {
        tty_puts("(empty)\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        char name_buf[13];
        int len = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++)
            name_buf[len++] = entries[i].name[j];
        if (entries[i].name[8] != ' ') {
            name_buf[len++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++)
                name_buf[len++] = entries[i].name[j];
        }
        name_buf[len] = '\0';

        tty_puts("  ");
        tty_puts(name_buf);
        if (entries[i].attr & FAT16_ATTR_DIRECTORY) {
            tty_puts("/");
            uint32_t dir_size = 0;
            if (fat16_dir_total_size(entries[i].first_cluster, &dir_size) == 0) {
                tty_puts("  (");
                print_uint(dir_size);
                tty_puts(" B)\n");
            } else {
                tty_puts("  (0 B)\n");
            }
        } else {
            tty_puts("  (");
            print_uint(entries[i].size);
            tty_puts(" B)\n");
        }
    }
}

static void cmd_cat(const char* arg)
{
    if (!*arg) { tty_puts("Usage: cat <filename>\n"); return; }
    uint8_t buf[4096];
    uint32_t size;
    if (fat16_read_file(arg, buf, sizeof(buf), &size) != 0) {
        tty_puts("File not found\n");
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        char c = (char)buf[i];
        if (c >= 32 && c <= 126) tty_putchar(c);
        else if (c == '\n') tty_putchar('\n');
        else if (c == '\t') tty_putchar('\t');
        else if (c == '\r') continue;
        else tty_putchar('.');
    }
    tty_putchar('\n');
}

static void cmd_write(const char* arg)
{
    if (!*arg) { tty_puts("Usage: write <filename> <content>\n"); return; }
    const char* content = arg;
    while (*content && *content != ' ') content++;
    if (!*content) { tty_puts("Usage: write <filename> <content>\n"); return; }

    char fname[64];
    int flen = (int)(content - arg);
    if (flen > 63) flen = 63;
    for (int i = 0; i < flen; i++) fname[i] = arg[i];
    fname[flen] = '\0';

    while (*content == ' ') content++;

    int clen = 0;
    while (content[clen]) clen++;

    if (fat16_write_file(fname, content, (uint32_t)clen) != 0) {
        tty_puts("Write failed\n");
        return;
    }
    tty_puts("OK (");
    print_uint((uint32_t)clen);
    tty_puts(" bytes)\n");
}

static void cmd_rm(const char* arg)
{
    if (!*arg) { tty_puts("Usage: rm <filename>\n"); return; }
    if (fat16_delete_file(arg) != 0)
        tty_puts("Delete failed\n");
    else
        tty_puts("Deleted\n");
}

static void cmd_mkdir(const char* arg)
{
    if (!*arg) { tty_puts("Usage: mkdir <dirname>\n"); return; }
    if (fat16_mkdir(arg) != 0)
        tty_puts("Mkdir failed\n");
    else
        tty_puts("Created\n");
}

static void cmd_cd(const char* arg)
{
    if (!*arg) { tty_puts("Usage: cd <dirname>\n"); return; }
    if (str_equ(arg, "..")) {
        if (path_depth == 0) return;
        if (fat16_cd("..") == 0)
            path_depth--;
        else
            tty_puts("Not found\n");
        return;
    }
    if (fat16_cd(arg) == 0) {
        if (path_depth < 16) {
            str_copy(path_stack[path_depth], arg, 16);
            path_depth++;
        }
    } else {
        tty_puts("Not found\n");
    }
}

static int user_name_valid(const char* s)
{
    if (!*s) return 0;
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_'))
            return 0;
    }
    return 1;
}

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int read_users(char* buf, int max)
{
    uint32_t sz;
    if (fat16_read_file_in(0, "USERS.DAT", buf, (uint32_t)max, &sz) != 0)
        return 0;
    buf[sz] = '\0';
    return (int)sz;
}

static int user_exists(const char* data, const char* user)
{
    const char* p = data;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon) colon = p;
            p++;
        }
        if (colon) {
            int len = (int)(colon - line);
            if (str_len(user) == len) {
                int match = 1;
                for (int i = 0; i < len; i++) {
                    if (ascii_lower(line[i]) != ascii_lower(user[i])) { match = 0; break; }
                }
                if (match) return 1;
            }
        }
        if (*p == '\n') p++;
    }
    return 0;
}

static int user_check(const char* data, const char* user, const char* pass)
{
    const char* p = data;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon) colon = p;
            p++;
        }
        if (colon) {
            int ulen = (int)(colon - line);
            const char* pp = colon + 1;
            int plen = (int)(p - pp);
            if (str_len(user) == ulen && str_len(pass) == plen) {
                int umatch = 1;
                for (int i = 0; i < ulen; i++) {
                    if (ascii_lower(line[i]) != ascii_lower(user[i])) { umatch = 0; break; }
                }
                int pmatch = 1;
                for (int i = 0; i < plen; i++) {
                    if (pp[i] != pass[i]) { pmatch = 0; break; }
                }
                if (umatch && pmatch) return 1;
            }
        }
        if (*p == '\n') p++;
    }
    return 0;
}

static void cmd_register(const char* arg)
{
    if (!*arg) { tty_puts("Usage: register <username> <password>\n"); return; }
    const char* pass = arg;
    while (*pass && *pass != ' ') pass++;
    if (!*pass) { tty_puts("Usage: register <username> <password>\n"); return; }

    char user[16];
    int ulen = (int)(pass - arg);
    if (ulen > 15) ulen = 15;
    for (int i = 0; i < ulen; i++) user[i] = arg[i];
    user[ulen] = '\0';
    while (*pass == ' ') pass++;

    if (!user_name_valid(user) || !user_name_valid(pass)) {
        tty_puts("Invalid username or password\n");
        return;
    }

    char buf[4096];
    int n = read_users(buf, sizeof(buf) - 1);
    if (n > 0 && user_exists(buf, user)) {
        tty_puts("User already exists\n");
        return;
    }

    int plen = str_len(pass);
    if (n + ulen + 1 + plen + 1 >= (int)sizeof(buf)) {
        tty_puts("User database full\n");
        return;
    }

    for (int i = 0; i < ulen; i++) buf[n++] = ascii_lower(user[i]);
    buf[n++] = ':';
    for (int i = 0; i < plen; i++) buf[n++] = pass[i];
    buf[n++] = '\n';

    fat16_set_write_attr(FAT16_ATTR_ARCHIVE | FAT16_ATTR_HIDDEN);
    if (fat16_write_file_in(0, "USERS.DAT", buf, (uint32_t)n) != 0) {
        tty_puts("Register failed\n");
        return;
    }

    char verify[4096];
    if (read_users(verify, sizeof(verify) - 1) > 0 && user_exists(verify, user)) {
        tty_puts("User registered. Use login to sign in\n");
    } else {
        tty_puts("Register failed (write did not persist)\n");
    }
}

static void cmd_login(const char* arg)
{
    if (!*arg) { tty_puts("Usage: login <username> <password>\n"); return; }
    const char* pass = arg;
    while (*pass && *pass != ' ') pass++;
    if (!*pass) { tty_puts("Usage: login <username> <password>\n"); return; }

    char user[16];
    int ulen = (int)(pass - arg);
    if (ulen > 15) ulen = 15;
    for (int i = 0; i < ulen; i++) user[i] = arg[i];
    user[ulen] = '\0';
    while (*pass == ' ') pass++;

    char buf[4096];
    int n = read_users(buf, sizeof(buf) - 1);
    if (n == 0) {
        tty_puts("No users. Register first\n");
        return;
    }
    if (!user_check(buf, user, pass)) {
        tty_puts("Login failed\n");
        return;
    }

    str_copy(cur_user, user, 16);
    tty_puts("Welcome, ");
    tty_puts(cur_user);
    tty_puts("\n");
}

static void cmd_users(void)
{
    char buf[4096];
    int n = read_users(buf, sizeof(buf) - 1);
    if (n == 0) { tty_puts("No users registered\n"); return; }

    const char* p = buf;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon) colon = p;
            p++;
        }
        if (colon) {
            for (const char* q = line; q < colon; q++) tty_putchar(*q);
            tty_putchar('\n');
        }
        if (*p == '\n') p++;
    }
}

static void cmd_logout(void)
{
    if (!cur_user[0]) {
        tty_puts("Not logged in\n");
        return;
    }
    cur_user[0] = '\0';
    while (path_depth > 0) {
        fat16_cd("..");
        path_depth--;
    }
    tty_puts("Logged out\n");
}

static void cmd_mem(void)
{
    tty_puts("Physical memory:\n");
    tty_puts("  Total:   ");
    print_uint(PMM_MAX_PHYS / 1024);
    tty_puts(" KB\n");
    tty_puts("  Free:    ");
    print_uint((pmm_free_page_count() * PMM_PAGE_SIZE) / 1024);
    tty_puts(" KB\n");
    tty_puts("  Used:    ");
    print_uint((pmm_used_page_count() * PMM_PAGE_SIZE) / 1024);
    tty_puts(" KB\n");
    tty_puts("Kernel heap:\n");
    tty_puts("  Free:    ");
    print_uint(heap_free_bytes() / 1024);
    tty_puts(" KB\n");
    tty_puts("  Used:    ");
    print_uint(heap_used_bytes() / 1024);
    tty_puts(" KB\n");
}

static void cmd_memtest(void)
{
    char* a = (char*)kmalloc(100);
    char* b = (char*)kmalloc(64);
    char* c = (char*)kmalloc(4096);
    if (!a || !b || !c) { tty_puts("FAIL: alloc\n"); return; }
    if (((uintptr_t)a % 8) || ((uintptr_t)b % 8) || ((uintptr_t)c % 8)) { tty_puts("FAIL: align\n"); return; }
    for (int i = 0; i < 100; i++) a[i] = (char)i;
    for (int i = 0; i < 4096; i++) c[i] = (char)(i & 0xFF);
    for (int i = 0; i < 100; i++) if (a[i] != (char)i) { tty_puts("FAIL: write a\n"); return; }
    for (int i = 0; i < 4096; i++) if (c[i] != (char)(i & 0xFF)) { tty_puts("FAIL: write c\n"); return; }
    kfree(a);
    kfree(c);
    char* d = (char*)kmalloc(100);
    if (!d) { tty_puts("FAIL: reuse\n"); return; }
    kfree(d);
    kfree(b);

    uintptr_t p1 = pmm_alloc_zero();
    uintptr_t p2 = pmm_alloc_zero();
    if (!p1 || !p2) { tty_puts("FAIL: pmm alloc\n"); return; }
    if (p1 == p2 || p1 % PMM_PAGE_SIZE || p2 % PMM_PAGE_SIZE) { tty_puts("FAIL: pmm pages\n"); return; }
    for (int i = 0; i < PMM_PAGE_SIZE; i++)
        if (((char*)p1)[i] != 0) { tty_puts("FAIL: pmm zero\n"); return; }
    pmm_free(p1);
    if (pmm_alloc() != p1) { tty_puts("FAIL: pmm reuse\n"); return; }
    pmm_free(p1);
    pmm_free(p2);

    tty_puts("PASS\n");
}

static void cmd_ui(const char* arg)
{
    (void)arg;
    if (!fs_ready) { tty_puts("Filesystem not ready\n"); return; }

    gfx_init(800, 600);
    mouse_init();

    desktop_run(fs_ready, cur_user);

    gfx_disable();
    tty_clear();
    print_banner();
}

static void cmd_run(const char* path)
{
    if (!fs_ready) { tty_puts("Filesystem not ready\n"); return; }
    if (!*path) { tty_puts("Usage: run <file.elf>\n"); return; }

    gfx_init(800, 600);
    mouse_init();
    gfx_begin();

    proc_t* proc = proc_create(path, path);
    if (!proc) {
        gfx_disable();
        tty_clear();
        print_banner();
        tty_puts("Failed to load program\n");
        return;
    }

    proc_set_current(proc);

    uint64_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    paging_switch_user_pt(proc->page_dir_phys);

    enter_user_mode(proc->entry_eip, proc->user_esp,
                    GDT_USER_CS | 3, GDT_USER_DS | 3, 0x202);

    paging_switch_user_pt(saved_cr3);

    proc_destroy(proc);

    gfx_disable();
    tty_clear();
    print_banner();
}

static void print_prompt(void)
{
    tty_puts("LuminaOS:");
    tty_putchar('/');
    for (int i = 0; i < path_depth; i++) {
        tty_puts(path_stack[i]);
        if (i < path_depth - 1) tty_putchar('/');
    }
    tty_puts(" [");
    if (cur_user[0])
        tty_puts(cur_user);
    else
        tty_puts("guest");
    tty_puts("]> ");
}

static uint32_t syscall_stack;

void kernel_main(uint8_t boot_drive)
{
    (void)boot_drive;

    __asm__ volatile ("cli");

    tty_init();
    gdt_init();
    idt_init();
    isr_init();
    irq_init();
    keyboard_init();

    pmm_init();
    heap_init();
    paging_init();
    syscall_stack = pmm_alloc_zero();
    tss_set_stack(syscall_stack + 4096);
    timer_init();
    sched_init();
    syscall_init();

    __asm__ volatile ("sti");

    print_banner();

    tty_puts("Initializing ATA...\n");
    if (fat16_init() == 0) {
        fs_ready = 1;
        tty_puts("FAT16 filesystem ready\n\n");
        static uint8_t font_buf[8192];
        uint32_t fsz = 0;
        if (fat16_read_file("FONT16.BIN", font_buf, sizeof(font_buf), &fsz) == 0) {
            gfx_cjk_load(font_buf, fsz);
            tty_puts("CJK font loaded (");
            print_uint(gfx_cjk_count());
            tty_puts(" glyphs)\n");
        } else {
            tty_puts("CJK font not found\n");
        }
    } else {
        tty_puts("FAT16 init failed (disk I/O only)\n\n");
    }

    char cmd[128];
    int cmd_pos = 0;

    while (1)
    {
        print_prompt();

        cmd_pos = 0;
        while (1)
        {
            char c = keyboard_getchar();

            if (c == '\n')
            {
                cmd[cmd_pos] = '\0';
                tty_putchar('\n');
                break;
            }
            else if (c == '\b')
            {
                if (cmd_pos > 0)
                {
                    cmd_pos--;
                    tty_putchar('\b');
                    tty_putchar(' ');
                    tty_putchar('\b');
                }
            }
            else if (c >= ' ' && c <= '~')
            {
                if (cmd_pos < 127)
                {
                    cmd[cmd_pos++] = c;
                    tty_putchar(c);
                }
            }
        }

        if (cmd[0] == '\0')
            continue;

        if (cmd[0] == 'h' && str_equ(cmd, "help"))
        {
            tty_puts("Available commands:\n");
            tty_puts("  help    - Show this help\n");
            tty_puts("  clear   - Clear the screen\n");
            tty_puts("  echo    - Echo text\n");
            tty_puts("  ver     - Show version info\n");
            tty_puts("  reboot  - Reboot the system\n");
            tty_puts("  halt    - Halt the system\n");
            tty_puts("  register- Register a user\n");
            tty_puts("  login   - Log in\n");
            tty_puts("  logout  - Log out\n");
            tty_puts("  users   - List registered users\n");
            tty_puts("  mem     - Show memory usage\n");
            tty_puts("  memtest - Self-test memory allocator\n");
            tty_puts("  uptime  - Show system uptime\n");
            tty_puts("  ui      - Start the graphical UI\n");
            if (fs_ready) {
                tty_puts("  ls      - List files on disk\n");
                tty_puts("  cd      - Change directory\n");
                tty_puts("  mkdir   - Create a directory\n");
                tty_puts("  cat     - Read a file\n");
                tty_puts("  write   - Write a file\n");
                tty_puts("  rm      - Delete a file\n");
            }
        }
        else if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'e' && cmd[3] == 'a' && cmd[4] == 'r' && cmd[5] == '\0')
        {
            tty_clear();
            print_banner();
        }
        else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == ' ')
        {
            tty_puts(cmd + 5);
            tty_putchar('\n');
        }
        else if (str_equ(cmd, "ver"))
        {
            tty_puts("LuminaOS v0.7.1\n");
            tty_puts("Architecture: x86_64 (64-bit)\n");
            tty_puts("Kernel: Hybrid\n");
            tty_puts("Filesystem: FAT16\n");
        }
        else if (cmd[0] == 'r' && cmd[1] == 'e' && cmd[2] == 'b' && cmd[3] == 'o' && cmd[4] == 'o' && cmd[5] == 't' && cmd[6] == '\0')
        {
            tty_puts("Rebooting...\n");
            system_reboot();
        }
        else if (cmd[0] == 'h' && cmd[1] == 'a' && cmd[2] == 'l' && cmd[3] == 't' && cmd[4] == '\0')
        {
            tty_puts("System halted.\n");
            system_halt();
        }
        else if (cmd[0] == 'r' && cmd[1] == 'e' && cmd[2] == 'g' && cmd[3] == 'i' && cmd[4] == 's' && cmd[5] == 't' && cmd[6] == 'e' && cmd[7] == 'r' && cmd[8] == ' ')
        {
            cmd_register(cmd + 9);
        }
        else if (cmd[0] == 'l' && cmd[1] == 'o' && cmd[2] == 'g' && cmd[3] == 'i' && cmd[4] == 'n' && cmd[5] == ' ')
        {
            cmd_login(cmd + 6);
        }
        else if (str_equ(cmd, "logout"))
        {
            cmd_logout();
        }
        else if (str_equ(cmd, "users"))
        {
            cmd_users();
        }
        else if (str_equ(cmd, "mem"))
        {
            cmd_mem();
        }
        else if (str_equ(cmd, "memtest"))
        {
            cmd_memtest();
        }
        else if (str_equ(cmd, "uptime"))
        {
            uint32_t ticks = timer_ticks();
            tty_puts("Uptime: ");
            print_uint(ticks / 1000);
            tty_puts(".");
            uint32_t frac = (ticks % 1000) / 100;
            tty_putchar((char)('0' + frac));
            tty_puts("s\n");
        }
        else if (cmd[0] == 'u' && cmd[1] == 'i' && (cmd[2] == '\0' || cmd[2] == ' '))
        {
            cmd_ui(cmd + 3);
        }
        else if (cmd[0] == 'r' && cmd[1] == 'u' && cmd[2] == 'n' && cmd[3] == ' ')
        {
            cmd_run(cmd + 4);
        }
        else if (cur_user[0] && fs_ready && cmd[0] == 'l' && cmd[1] == 's' && cmd[2] == '\0')
        {
            cmd_ls();
        }
        else if (cur_user[0] && fs_ready && cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ')
        {
            cmd_cd(cmd + 3);
        }
        else if (cur_user[0] && fs_ready && cmd[0] == 'm' && cmd[1] == 'k' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' && cmd[5] == ' ')
        {
            cmd_mkdir(cmd + 6);
        }
        else if (cur_user[0] && fs_ready && cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ')
        {
            cmd_cat(cmd + 4);
        }
        else if (cur_user[0] && fs_ready && cmd[0] == 'w' && cmd[1] == 'r' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == 'e' && cmd[5] == ' ')
        {
            cmd_write(cmd + 6);
        }
        else if (cur_user[0] && fs_ready && cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == ' ')
        {
            cmd_rm(cmd + 3);
        }
        else if (fs_ready && !cur_user[0] &&
                 ((cmd[0] == 'l' && cmd[1] == 's') ||
                  (cmd[0] == 'c' && cmd[1] == 'd') ||
                  (cmd[0] == 'm' && cmd[1] == 'k') ||
                  (cmd[0] == 'c' && cmd[1] == 'a') ||
                  (cmd[0] == 'w' && cmd[1] == 'r') ||
                  (cmd[0] == 'r' && cmd[1] == 'm')))
        {
            tty_puts("Please login or register first\n");
        }
        else
        {
            tty_puts("Unknown command: ");
            tty_puts(cmd);
            tty_putchar('\n');
        }
    }
}
