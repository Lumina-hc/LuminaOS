BITS 64
section .text

global gdt_flush

gdt_flush:
    lgdt [rdi]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov ax, 0x28
    ltr ax

    ret
