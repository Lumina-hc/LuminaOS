#ifndef KERNEL_BOOTINFO_H
#define KERNEL_BOOTINFO_H

#include <stdint.h>

#define LUMINA_BOOT_INFO_ADDR 0x6000ULL
#define LUMINA_BOOT_MAGIC     0x4C554D424F4F5401ULL

typedef struct {
    uint64_t Magic;
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t Width;
    uint32_t Height;
    uint32_t Stride;
    uint32_t PixelFormat;
} lum_boot_info_t;

static inline lum_boot_info_t* lum_boot_info(void)
{
    return (lum_boot_info_t*)(uintptr_t)LUMINA_BOOT_INFO_ADDR;
}

#endif
