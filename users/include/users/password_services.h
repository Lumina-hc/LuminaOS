#ifndef PASSWORD_SERVICES_H
#define PASSWORD_SERVICES_H

#include <stdint.h>

void name_input_init(void);
void password_input_init(void);
void reg_name_input_init(void);
void reg_password_input_init(void);

void password_service_init(void);
void password_service_draw(void);
void password_service_key(int key);
void password_service_mouse(int mx, int my, int click);
void password_service_tick(uint32_t now_ms);
int password_service_action(void);
void password_service_reset_action(void);
void password_service_clear(void);
const char* name_input_value(void);
const char* password_input_value(void);
const char* reg_name_input_value(void);
const char* reg_password_input_value(void);

#endif
