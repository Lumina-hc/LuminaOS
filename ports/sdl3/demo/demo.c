/*
  LuminaOS SDL3 port - minimal demo.

  Built as a freestanding ELF64 app (no crt0, no libc): the entry point is
  _start, matching third_party/LuminaOS-SDK/elf.ld. It exercises the pieces
  the LUMIA port provides: SDL_Init(Video), a software framebuffer window,
  SDL_UpdateWindowSurface (blit via gfx syscalls), event polling and
  SDL_Delay (backed by the GET_TICKS/SLEEP syscalls).
*/
#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

/* LuminaOS exit syscall (int 0x80, syscall 0). */
static void lumia_exit(void)
{
    __asm__ volatile ("int $0x80" : : "a"(0L), "D"(0L), "S"(0L), "d"(0L), "c"(0L));
    for (;;) { }
}

static void draw(SDL_Surface *surface, int frame)
{
    Uint32 *pixels = (Uint32 *)surface->pixels;
    int pitch_px = surface->pitch / 4;
    int x, y;

    for (y = 0; y < surface->h; ++y) {
        for (x = 0; x < surface->w; ++x) {
            Uint8 r = (Uint8)(x + frame);
            Uint8 g = (Uint8)(y - frame);
            Uint8 b = (Uint8)(x ^ y);
            pixels[y * pitch_px + x] = (0xFFu << 24) |
                                       ((Uint32)r << 16) |
                                       ((Uint32)g << 8) |
                                       (Uint32)b;
        }
    }
}

void _start(void)
{
    SDL_Window *window;
    SDL_Surface *surface;
    SDL_Event event;
    int frame = 0;
    bool quit = false;

    SDL_SetMainReady();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        lumia_exit();
    }

    window = SDL_CreateWindow("SDL3 on LuminaOS", 320, 200, 0);
    if (!window) {
        SDL_Quit();
        lumia_exit();
    }

    surface = SDL_GetWindowSurface(window);
    if (!surface) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        lumia_exit();
    }

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
        }
        draw(surface, frame++);
        SDL_UpdateWindowSurface(window);
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    lumia_exit();
}
