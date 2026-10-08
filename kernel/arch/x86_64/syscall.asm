BITS 64
section .text

extern isr_common_stub

global syscall_asm

syscall_asm:
    push qword 0
    push qword 0x80
    jmp isr_common_stub
