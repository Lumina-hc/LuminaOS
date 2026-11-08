BITS 64
section .text

global syscall_asm

; Linux System V AMD64 ABI trap gate.
; Entry state on the syscall stack:
;   [rsp+0]  rflags, [rsp+8] cs, [rsp+16] rip,
;   [rsp+24] user rsp, [rsp+32] ss
;   registers: rax = number, rdi/rsi/rdx/r10/r8/r9 = args.
; Save all callee-saved registers and the return registers on the
; interrupt stack, call the C handler, restore, and iretq.
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
