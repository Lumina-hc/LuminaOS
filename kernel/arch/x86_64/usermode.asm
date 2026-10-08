BITS 64
section .text

global enter_user_mode
global kernel_resume

extern sched_user_enter
extern sched_user_exit

section .bss
align 16
saved_rsp:  resq 1
saved_rbx:  resq 1
saved_rbp:  resq 1
saved_r12:  resq 1
user_rsp:   resq 1
user_rflags: resq 1

section .text

enter_user_mode:
    mov [rel saved_rsp], rsp
    mov [rel saved_rbx], rbx
    mov [rel saved_rbp], rbp
    mov [rel saved_r12], r12

    mov [rel user_rsp], rsi
    mov [rel user_rflags], r8

    mov r12, rdi
    mov rbp, rdx
    mov rbx, rcx

    call sched_user_enter

    push rbx
    push qword [rel user_rsp]
    push qword [rel user_rflags]
    push rbp
    push r12

    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    iretq

kernel_resume:
    pushfq
    pop rax
    cli
    push rax
    call sched_user_exit
    pop rax

    mov rsp, [rel saved_rsp]
    push rax
    popfq

    mov rbx, [rel saved_rbx]
    mov rbp, [rel saved_rbp]
    mov r12, [rel saved_r12]
    ret
