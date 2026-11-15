#include <kernel/config.h>
#include <kernel/fat16.h>

#define CONFIG_FILE "SETUP.CFG"
#define CONFIG_MAX  256

static int cfg_loaded;
static int cfg_done;
static int cfg_lang;
static int cfg_tz;

static int str_eq(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int line_starts(const char* p, const char* key, int* out)
{
    int n = 0;
    while (key[n]) {
        if (p[n] != key[n])
            return 0;
        n++;
    }
    p += n;
    int v = 0;
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -v : v;
    return 1;
}

void config_reset(void)
{
    cfg_loaded = 1;
    cfg_done = 0;
    cfg_lang = 0;
    cfg_tz = 480;
}

int config_load(void)
{
    char buf[CONFIG_MAX];
    uint32_t size = 0;

    config_reset();
    if (fat16_read_file_in(0, CONFIG_FILE, buf, sizeof(buf) - 1, &size) != 0)
        return 0;
    if (size == 0)
        return 0;
    buf[size] = '\0';

    for (uint32_t i = 0; i < size; ) {
        char* line = &buf[i];
        while (i < size && buf[i] != '\n' && buf[i] != '\r')
            i++;
        buf[i] = '\0';
        while (i < size && (buf[i] == '\n' || buf[i] == '\r'))
            i++;

        int v = 0;
        if (line_starts(line, "done=", &v))
            cfg_done = v ? 1 : 0;
        else if (line_starts(line, "lang=", &v))
            cfg_lang = v;
        else if (line_starts(line, "tz=", &v))
            cfg_tz = v;
    }

    cfg_loaded = 1;
    return cfg_done;
}

static void append(char* buf, int* n, const char* s)
{
    while (*s)
        buf[(*n)++] = *s++;
}

static void append_int(char* buf, int* n, int v)
{
    char tmp[12];
    int p = 0;
    int neg = v < 0;
    if (neg)
        v = -v;
    if (v == 0)
        tmp[p++] = '0';
    while (v) {
        tmp[p++] = (char)('0' + v % 10);
        v /= 10;
    }
    if (neg)
        buf[(*n)++] = '-';
    while (p > 0)
        buf[(*n)++] = tmp[--p];
}

int config_save(void)
{
    char buf[CONFIG_MAX];
    int n = 0;

    append(buf, &n, "done=");
    append_int(buf, &n, cfg_done ? 1 : 0);
    append(buf, &n, "\nlang=");
    append_int(buf, &n, cfg_lang);
    append(buf, &n, "\ntz=");
    append_int(buf, &n, cfg_tz);
    append(buf, &n, "\n");

    fat16_set_write_attr(FAT16_ATTR_ARCHIVE | FAT16_ATTR_HIDDEN);
    if (fat16_write_file_in(0, CONFIG_FILE, buf, (uint32_t)n) != 0)
        return -1;

    char verify[CONFIG_MAX];
    uint32_t size = 0;
    if (fat16_read_file_in(0, CONFIG_FILE, verify, sizeof(verify) - 1, &size) != 0)
        return -2;
    if ((int)size != n)
        return -2;
    return 0;
}

int config_done(void)
{
    return cfg_done;
}

int config_lang(void)
{
    return cfg_lang;
}

int config_tz(void)
{
    return cfg_tz;
}

void config_set_lang(int lang)
{
    cfg_lang = lang ? 1 : 0;
}

void config_set_tz(int minutes)
{
    if (minutes < -12 * 60)
        minutes = -12 * 60;
    if (minutes > 14 * 60)
        minutes = 14 * 60;
    cfg_tz = minutes;
}

void config_mark_done(void)
{
    cfg_done = 1;
}
