BITS 64
section .text

global syscall_asm

syscall_asm:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call syscall_asm_handler
    mov rsp, rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    iretq
