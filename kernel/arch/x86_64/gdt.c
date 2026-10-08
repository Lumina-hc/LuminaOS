#include <kernel/gdt.h>

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) gdt_tss_desc_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint16_t ss0;
    uint8_t pad0[6];
    uint64_t rsp1;
    uint16_t ss1;
    uint8_t pad1[6];
    uint64_t rsp2;
    uint16_t ss2;
    uint8_t pad2[6];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

#define GDT_CODE_ENTRIES 5
#define GDT_ENTRIES (GDT_CODE_ENTRIES + 1)

typedef struct {
    gdt_entry_t entries[GDT_CODE_ENTRIES];
    gdt_tss_desc_t tss;
} __attribute__((packed)) gdt_t;

static gdt_t gdt;
static gdt_ptr_t gdt_ptr;
static tss_t tss;

#define IST_STACK_SIZE 16384
static uint8_t ist_stack[IST_STACK_SIZE] __attribute__((aligned(16)));

static uint64_t kernel_stack_top;

extern void gdt_flush(uint64_t);

static void gdt_set_entry(int num, uint64_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
    gdt.entries[num].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt.entries[num].base_low = (uint16_t)(base & 0xFFFF);
    gdt.entries[num].base_mid = (uint8_t)((base >> 16) & 0xFF);
    gdt.entries[num].base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt.entries[num].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (granularity & 0xF0));
    gdt.entries[num].access = access;
}

static void gdt_set_tss(uint64_t base, uint32_t limit)
{
    gdt.tss.limit_low = (uint16_t)(limit & 0xFFFF);
    gdt.tss.base_low = (uint16_t)(base & 0xFFFF);
    gdt.tss.base_mid = (uint8_t)((base >> 16) & 0xFF);
    gdt.tss.access = 0x89;
    gdt.tss.granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt.tss.base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt.tss.base_upper = (uint32_t)(base >> 32);
    gdt.tss.reserved = 0;
}

static void gdt_write_msr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("mov %0, %%ecx\n\t"
                      "mov %1, %%eax\n\t"
                      "mov %2, %%edx\n\t"
                      "wrmsr"
                      :
                      : "r"(msr), "r"(lo), "r"(hi)
                      : "ecx", "eax", "edx");
}

void tss_set_stack(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
    tss.ss0 = GDT_KERNEL_DS;
}

void tss_set_kernel_stack(uint64_t rsp)
{
    kernel_stack_top = rsp;
}

void gdt_init(void)
{
    for (int i = 0; i < (int)sizeof(tss_t) / (int)sizeof(uint8_t); i++)
        ((uint8_t*)&tss)[i] = 0;

    tss.ist[0] = (uint64_t)(ist_stack + IST_STACK_SIZE);
    tss.iomap_base = sizeof(tss_t);

    gdt_ptr.limit = sizeof(gdt_t) - 1;
    gdt_ptr.base = (uint64_t)&gdt.entries[0];

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9B, 0xAF);
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x93, 0xCF);
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFB, 0xAF);
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF3, 0xCF);
    gdt_set_tss((uint64_t)&tss, sizeof(tss_t) - 1);

    gdt_flush((uint64_t)&gdt_ptr);

    gdt_write_msr(0xC0000101, 0);
    gdt_write_msr(0xC0000100, 0);
    gdt_write_msr(0xC0000102, 0);
}
