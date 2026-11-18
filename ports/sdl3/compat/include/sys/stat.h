/*
  Minimal freestanding <sys/stat.h> for the LuminaOS SDL3 port.
  SDL_steam_virtual_gamepad.c includes it on non-Linux/non-Windows; the LUMINA
  config disables joystick so the file compiles to an empty translation unit,
  but the header must still exist for the preprocessor.
*/
#ifndef LUMINA_COMPAT_SYS_STAT_H
#define LUMINA_COMPAT_SYS_STAT_H

#include <sys/types.h>

struct stat {
    uint64_t st_size;
    time_t   st_mtime;
    mode_t   st_mode;
};

int stat(const char *path, struct stat *buf);

#endif /* LUMINA_COMPAT_SYS_STAT_H */
