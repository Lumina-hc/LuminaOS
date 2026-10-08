#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <stdint.h>

#define HEAP_START 0x100000
#define HEAP_SIZE  0x400000
#define HEAP_ALIGN 8
#define HEAP_MIN_SPLIT 32

#define BLOCK_MAGIC 0xCAFEBABE
#define BLOCK_FREE  0x01

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;
    uint32_t flags;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* heap_head;
static uint32_t heap_free;
static uint32_t heap_used;

static uint32_t heap_align_up(uint32_t v)
{
    return (v + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
}

static void heap_block_init(heap_block_t* b, uint32_t size, uint32_t flags)
{
    b->magic = BLOCK_MAGIC;
    b->size = size;
    b->flags = flags;
    b->next = 0;
}

static void heap_add_free(heap_block_t* b, uint32_t size)
{
    heap_block_init(b, size, BLOCK_FREE);
    heap_free += sizeof(heap_block_t) + b->size;

    heap_block_t* prev = 0;
    heap_block_t** pp = &heap_head;
    while (*pp && (uintptr_t)*pp < (uintptr_t)b) {
        prev = *pp;
        pp = &(*pp)->next;
    }
    b->next = *pp;
    *pp = b;

    if (prev && (uintptr_t)prev + sizeof(heap_block_t) + prev->size == (uintptr_t)b) {
        prev->size += sizeof(heap_block_t) + b->size;
        prev->next = b->next;
        b = prev;
    }
    if (b->next && (uintptr_t)b + sizeof(heap_block_t) + b->size == (uintptr_t)b->next) {
        b->size += sizeof(heap_block_t) + b->next->size;
        b->next = b->next->next;
    }
}

void heap_init(void)
{
    heap_head = 0;
    heap_free = 0;
    heap_used = 0;

    pmm_reserve(HEAP_START, HEAP_SIZE);

    heap_add_free((heap_block_t*)HEAP_START, HEAP_SIZE - sizeof(heap_block_t));
}

void* kmalloc(uint32_t size)
{
    if (size == 0) return 0;

    uint32_t need = heap_align_up(size);

    heap_block_t** pp = &heap_head;
    while (*pp) {
        heap_block_t* b = *pp;
        if (b->size >= need) {
            if (b->size >= need + sizeof(heap_block_t) + HEAP_MIN_SPLIT) {
                heap_block_t* split = (heap_block_t*)((uintptr_t)b + sizeof(heap_block_t) + need);
                heap_block_init(split, b->size - need - sizeof(heap_block_t), BLOCK_FREE);
                split->next = b->next;
                b->size = need;
                b->next = split;
            }
            *pp = b->next;
            heap_free -= sizeof(heap_block_t) + b->size;
            heap_used += sizeof(heap_block_t) + b->size;
            b->flags = 0;
            return (void*)((uintptr_t)b + sizeof(heap_block_t));
        }
        pp = &(*pp)->next;
    }
    return 0;
}

void kfree(void* ptr)
{
    if (!ptr) return;
    heap_block_t* b = (heap_block_t*)((uintptr_t)ptr - sizeof(heap_block_t));
    if (b->magic != BLOCK_MAGIC) return;
    if (b->flags & BLOCK_FREE) return;

    heap_used -= sizeof(heap_block_t) + b->size;
    heap_add_free(b, b->size);
}

uint32_t heap_free_bytes(void)
{
    return heap_free;
}

uint32_t heap_used_bytes(void)
{
    return heap_used;
}
