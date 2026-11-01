/*
 * LuminaOS EFI bootloader.
 * Loads KERNEL.BIN from the boot volume, sets up 64-bit paging, installs a
 * GDT, switches the graphics mode to 800x600x32 and jumps to the kernel's
 * 64-bit efi_entry (which reuses the BIOS long_mode code path).
 */

#include "efi.h"

#ifndef KERNEL_ENTRY
#define KERNEL_ENTRY 0
#endif
#ifndef KERNEL_BSS_START
#define KERNEL_BSS_START 0
#endif
#ifndef KERNEL_BSS_END
#define KERNEL_BSS_END 0
#endif

#define KERNEL_LOAD 0x7E00
#define PAGESZ      0x1000
#define MAX_KERNEL  0x100000

static const CHAR16 LUMINA_SIGN[]  = L"LuminaOS UEFI loader\r\n";
static const CHAR16 LUMINA_FAIL[]  = L"FATAL: KERNEL.BIN not found\r\n";
static const CHAR16 LUMINA_SMALL[] = L"FATAL: KERNEL.BIN too large\r\n";

static void* mcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void mzero(void* dst, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = 0;
}

__attribute__((aligned(4096)))
static uint64_t lum_pml4[512];
__attribute__((aligned(4096)))
static uint64_t lum_pdpt[512];
__attribute__((aligned(4096)))
static uint64_t lum_pd[8][512];

static void lum_build_paging(void)
{
    for (int i = 0; i < 512; i++) {
        lum_pml4[i] = 0;
        lum_pdpt[i] = 0;
    }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 512; j++)
            lum_pd[i][j] = 0;

    lum_pml4[0] = (uint64_t)(uintptr_t)lum_pdpt | 0x03;
    lum_pml4[1] = (uint64_t)(uintptr_t)lum_pdpt | 0x03;

    for (int i = 0; i < 8; i++)
        lum_pdpt[i] = (uint64_t)(uintptr_t)lum_pd[i] | 0x03;

    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 512; j++)
            lum_pd[i][j] = ((uint64_t)i * 0x40000000ULL + (uint64_t)j * 0x200000ULL) | 0x83;
}

__attribute__((aligned(16)))
static uint64_t lum_gdt[5] = {
    0x0000000000000000ULL,
    0x00AF9B000000FFFFULL,
    0x00AF93000000FFFFULL,
    0x00AFFB000000FFFFULL,
    0x00AFF3000000FFFFULL,
};

/* Force a base-relocation entry so the firmware can relocate this image. */
static uint64_t lum_reloc_pad __attribute__((used))
    = (uint64_t)(uintptr_t)&lum_gdt;

struct __attribute__((packed)) lum_gdtr {
    uint16_t limit;
    uint64_t base;
};

static struct lum_gdtr lum_gdtr_desc;

static void lum_fill_boot_info(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop)
{
    LUMINA_BOOT_INFO* bi = (LUMINA_BOOT_INFO*)(uintptr_t)LUMINA_BOOT_INFO_ADDR;
    if (!gop || !gop->Mode || !gop->Mode->Info) return;
    bi->Magic = LUMINA_BOOT_MAGIC;
    bi->FrameBufferBase = gop->Mode->FrameBufferBase;
    bi->FrameBufferSize = gop->Mode->FrameBufferSize;
    bi->Width = gop->Mode->Info->HorizontalResolution;
    bi->Height = gop->Mode->Info->VerticalResolution;
    bi->Stride = gop->Mode->Info->PixelsPerScanLine;
    bi->PixelFormat = gop->Mode->Info->PixelFormat;
}

static const uint32_t lum_modes[][2] = {
    { 1920, 1080 },
    { 1600, 900 },
    { 1440, 900 },
    { 1366, 768 },
    { 1280, 800 },
    { 1280, 720 },
    { 1024, 768 },
    { 800, 600 },
};
#define LUM_MODE_COUNT ((int)(sizeof(lum_modes) / sizeof(lum_modes[0])))

static void lum_set_mode(EFI_SYSTEM_TABLE* st)
{
    EFI_BOOT_SERVICES* bs = st->BootServices;
    uint64_t count = 0;
    EFI_HANDLE* handles = NULL;
    EFI_GUID* gopG = (EFI_GUID*)&LUMINA_GOP_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* fallback = NULL;
    if (bs->LocateHandleBuffer(2, gopG, NULL, &count, &handles))
        return;

    for (int p = 0; p < LUM_MODE_COUNT; p++) {
        for (uint64_t i = 0; i < count; i++) {
            EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
            if (bs->OpenProtocol(handles[i], gopG, (void**)&gop, NULL, NULL,
                                 EFI_OPEN_PROTOCOL_GET_PROTOCOL))
                continue;
            if (!fallback) fallback = gop;
            for (uint32_t m = 0; m < gop->Mode->MaxMode; m++) {
                uint64_t infosz = 0;
                EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = NULL;
                if (gop->QueryMode(gop, m, &infosz, &info))
                    continue;
                if (info->HorizontalResolution == lum_modes[p][0] &&
                    info->VerticalResolution == lum_modes[p][1] &&
                    info->PixelFormat != 2 && info->PixelFormat != 3) {
                    gop->SetMode(gop, m);
                    lum_fill_boot_info(gop);
                    return;
                }
            }
        }
    }
    lum_fill_boot_info(fallback);
}

static uint64_t lum_load_kernel(EFI_SYSTEM_TABLE* st, uint8_t** out)
{
    EFI_BOOT_SERVICES* bs = st->BootServices;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* out2 = st->ConOut;
    uint64_t count = 0;
    EFI_HANDLE* handles = NULL;
    EFI_GUID* sfsG = (EFI_GUID*)&LUMINA_SFS_GUID;
    EFI_GUID* fileG = (EFI_GUID*)&LUMINA_FILE_GUID;
    if (bs->LocateHandleBuffer(2, sfsG, NULL, &count, &handles)) {
        out2->OutputString(out2, (CHAR16*)L"DBG: LHB fail\r\n");
        return 0;
    }
    out2->OutputString(out2, (CHAR16*)L"DBG: fs handles=");
    {
        CHAR16 nb[16];
        int p = 0;
        uint64_t v = count;
        if (v == 0) nb[p++] = L'0';
        while (v) { nb[p++] = (CHAR16)(L'0' + (v % 10)); v /= 10; }
        for (int a = 0, b = p - 1; a < b; a++, b--) { CHAR16 t = nb[a]; nb[a] = nb[b]; nb[b] = t; }
        nb[p] = 0;
        out2->OutputString(out2, nb);
    }
    out2->OutputString(out2, (CHAR16*)L"\r\n");

    for (uint64_t i = 0; i < count; i++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = NULL;
        if (bs->OpenProtocol(handles[i], sfsG, (void**)&fs, NULL, NULL,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL))
            continue;
        EFI_FILE_PROTOCOL* root = NULL;
        if (fs->OpenVolume(fs, &root)) {
            out2->OutputString(out2, (CHAR16*)L"DBG: openvol fail\r\n");
            continue;
        }

        EFI_FILE_PROTOCOL* file = NULL;
        EFI_STATUS r = root->Open(root, (void**)&file, (CHAR16*)L"\\KERNEL.BIN",
                                  EFI_FILE_MODE_READ, 0);
        if (r) {
            out2->OutputString(out2, (CHAR16*)L"DBG: no kernel here\r\n");
            root->Close(root);
            continue;
        }
        out2->OutputString(out2, (CHAR16*)L"DBG: open OK\r\n");

        static uint8_t infobuf[512];
        uint64_t infosz = sizeof(infobuf);
        r = file->GetInfo(file, fileG, &infosz, infobuf);
        if (r) {
            out2->OutputString(out2, (CHAR16*)L"DBG: getinfo fail\r\n");
            file->Close(file);
            root->Close(root);
            continue;
        }
        uint64_t size = ((EFI_FILE_INFO*)infobuf)->FileSize;
        if (size == 0 || size > MAX_KERNEL) {
            out2->OutputString(out2, (CHAR16*)L"DBG: bad size\r\n");
            file->Close(file);
            root->Close(root);
            return size;
        }

        EFI_PHYSICAL_ADDRESS buf = 0;
        if (bs->AllocatePages(EFI_ALLOCATE_ANY_PAGES, EfiLoaderData,
                              (size + PAGESZ - 1) / PAGESZ, &buf)) {
            out2->OutputString(out2, (CHAR16*)L"DBG: alloc fail\r\n");
            file->Close(file);
            root->Close(root);
            return 0;
        }

        uint64_t off = 0;
        r = 0;
        while (off < size) {
            uint64_t chunk = size - off;
            r = file->Read(file, &chunk, (void*)(uintptr_t)(buf + off));
            if (r) break;
            if (chunk == 0) { r = 1; break; }
            off += chunk;
        }
        file->Close(file);
        root->Close(root);

        if (!r) {
            *out = (uint8_t*)(uintptr_t)buf;
            return size;
        }
        out2->OutputString(out2, (CHAR16*)L"DBG: read fail\r\n");
    }
    return 0;
}

__attribute__((noreturn))
static void lum_enter(uint64_t entry)
{
    __asm__ volatile("push $0x08\n\tpush %0\n\tlretq" :: "r"(entry) : "memory");
    __builtin_unreachable();
}

__attribute__((noreturn))
void efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE* st)
{
    (void)image;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* out = st->ConOut;
    out->ClearScreen(out);
    out->OutputString(out, (CHAR16*)LUMINA_SIGN);

    lum_set_mode(st);

    uint8_t* kernel = NULL;
    uint64_t size = lum_load_kernel(st, &kernel);
    if (size == 0 || kernel == NULL) {
        out->OutputString(out, (CHAR16*)LUMINA_FAIL);
        for (;;) __asm__ volatile("cli; hlt");
    }
    if (size > MAX_KERNEL) {
        out->OutputString(out, (CHAR16*)LUMINA_SMALL);
        for (;;) __asm__ volatile("cli; hlt");
    }

    lum_build_paging();

    lum_gdtr_desc.limit = sizeof(lum_gdt) - 1;
    lum_gdtr_desc.base = (uint64_t)(uintptr_t)lum_gdt;

    __asm__ volatile("cli" ::: "memory");
    __asm__ volatile("lgdt %0" :: "m"(lum_gdtr_desc) : "memory");
    __asm__ volatile(
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %0, %%rax\n\t"
        "mov %%rax, %%cr3\n\t"
        :: "r"((uint64_t)(uintptr_t)lum_pml4) : "rax", "memory");

    mzero((void*)KERNEL_BSS_START, KERNEL_BSS_END - KERNEL_BSS_START);
    mcpy((void*)KERNEL_LOAD, kernel, size);
    lum_enter((uint64_t)KERNEL_ENTRY);

    __builtin_unreachable();
}