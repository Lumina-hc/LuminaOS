BITS 16
default abs
section .text

global _start
extern bss_start, bss_end
extern kernel_main

boot_drive_addr equ 0x7C40

_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    call enable_a20

    lgdt [gdt32_desc]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode

BITS 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7000

    cld
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call setup_long_mode

    lgdt [gdt64_desc]
    jmp 0x08:long_mode

BITS 64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor ebp, ebp

    movzx edi, byte [boot_drive_addr]
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

BITS 32
setup_long_mode:
    mov edi, pml4
    mov eax, pdpt
    or eax, 0x03
    mov [edi], eax

    mov edi, pdpt
    mov eax, pd0
    or eax, 0x03
    mov [edi], eax
    mov eax, pd1
    or eax, 0x03
    mov [edi + 8], eax
    mov eax, pd2
    or eax, 0x03
    mov [edi + 16], eax
    mov eax, pd3
    or eax, 0x03
    mov [edi + 24], eax

    mov edi, pd0
    mov eax, 0x83
    mov ecx, 512
.map_pd0:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .map_pd0

    mov edi, pd1
    mov eax, 0x40000083
    mov ecx, 512
.map_pd1:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .map_pd1

    mov edi, pd2
    mov eax, 0x80000083
    mov ecx, 512
.map_pd2:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .map_pd2

    mov edi, pd3
    mov eax, 0xC0000083
    mov ecx, 512
.map_pd3:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .map_pd3

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, pml4
    mov cr3, eax

    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    ret

BITS 16
enable_a20:
    push ax

    mov ax, 0x2401
    int 0x15
    jnc .done

    call .wait_cmd
    mov al, 0xD1
    out 0x64, al
    call .wait_cmd
    mov al, 0xDF
    out 0x60, al
    call .wait_data

.done:
    pop ax
    ret

.wait_cmd:
    in al, 0x64
    test al, 2
    jnz .wait_cmd
    ret

.wait_data:
    in al, 0x64
    test al, 1
    jz .wait_data
    ret

BITS 32
align 8
gdt32:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt32_end:

gdt32_desc:
    dw gdt32_end - gdt32 - 1
    dd gdt32

align 8
gdt64:
    dq 0
    dq 0x00AF9B000000FFFF
    dq 0x00AF93000000FFFF
    dq 0x00AFFB000000FFFF
    dq 0x00AFF3000000FFFF
gdt64_end:

gdt64_desc:
    dw gdt64_end - gdt64 - 1
    dq gdt64

BITS 64
section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd0:
    resb 4096
pd1:
    resb 4096
pd2:
    resb 4096
pd3:
    resb 4096
