#ifndef AUTH_H
#define AUTH_H

int auth_name_valid(const char* s);
int auth_read(char* buf, int max);
int auth_has_users(void);
int auth_exists(const char* user);
int auth_check(const char* user, const char* pass);
int auth_register(const char* user, const char* pass);

#endif
