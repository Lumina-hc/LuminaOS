#include <kernel/auth.h>
#include <kernel/fat16.h>

#define AUTH_DB_MAX 4096

static int str_len(const char* s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

int auth_name_valid(const char* s)
{
    if (!s || !*s)
        return 0;
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_'))
            return 0;
    }
    return 1;
}

int auth_read(char* buf, int max)
{
    uint32_t size = 0;
    if (max < 2)
        return 0;
    if (fat16_read_file_in(0, "USERS.DAT", buf, (uint32_t)(max - 1), &size) != 0)
        return 0;
    buf[size] = '\0';
    return (int)size;
}

int auth_has_users(void)
{
    char buf[AUTH_DB_MAX];
    return auth_read(buf, sizeof(buf)) > 0;
}

static int user_exists(const char* data, const char* user)
{
    const char* p = data;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon)
                colon = p;
            p++;
        }
        if (colon) {
            int len = (int)(colon - line);
            if (str_len(user) == len) {
                int match = 1;
                for (int i = 0; i < len; i++) {
                    if (ascii_lower(line[i]) != ascii_lower(user[i])) {
                        match = 0;
                        break;
                    }
                }
                if (match)
                    return 1;
            }
        }
        if (*p == '\n')
            p++;
    }
    return 0;
}

int auth_exists(const char* user)
{
    char buf[AUTH_DB_MAX];
    if (auth_read(buf, sizeof(buf)) <= 0)
        return 0;
    return user_exists(buf, user);
}

int auth_check(const char* user, const char* pass)
{
    char buf[AUTH_DB_MAX];
    if (auth_read(buf, sizeof(buf)) <= 0)
        return 0;

    const char* p = buf;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon)
                colon = p;
            p++;
        }
        if (colon) {
            int ulen = (int)(colon - line);
            const char* pp = colon + 1;
            int plen = (int)(p - pp);
            if (str_len(user) == ulen && str_len(pass) == plen) {
                int umatch = 1;
                for (int i = 0; i < ulen; i++) {
                    if (ascii_lower(line[i]) != ascii_lower(user[i])) {
                        umatch = 0;
                        break;
                    }
                }
                int pmatch = 1;
                for (int i = 0; i < plen; i++) {
                    if (pp[i] != pass[i]) {
                        pmatch = 0;
                        break;
                    }
                }
                if (umatch && pmatch)
                    return 1;
            }
        }
        if (*p == '\n')
            p++;
    }
    return 0;
}

int auth_register(const char* user, const char* pass)
{
    if (!auth_name_valid(user) || !auth_name_valid(pass))
        return -1;

    char buf[AUTH_DB_MAX];
    int n = auth_read(buf, sizeof(buf));
    if (n > 0 && user_exists(buf, user))
        return -2;

    int ulen = str_len(user);
    int plen = str_len(pass);
    if (n + ulen + 1 + plen + 1 >= (int)sizeof(buf))
        return -3;

    for (int i = 0; i < ulen; i++)
        buf[n++] = ascii_lower(user[i]);
    buf[n++] = ':';
    for (int i = 0; i < plen; i++)
        buf[n++] = pass[i];
    buf[n++] = '\n';

    fat16_set_write_attr(FAT16_ATTR_ARCHIVE | FAT16_ATTR_HIDDEN);
    if (fat16_write_file_in(0, "USERS.DAT", buf, (uint32_t)n) != 0)
        return -4;

    char verify[AUTH_DB_MAX];
    if (auth_read(verify, sizeof(verify)) > 0 && user_exists(verify, user))
        return 0;
    return -4;
}
