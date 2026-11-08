BITS 64
section .text

global enter_user_mode
global kernel_resume
global resume_user_mode
global sysret_asm

extern sched_user_enter
extern sched_user_exit
extern paging_switch_kernel

section .bss
align 16
saved_rsp:  resq 1
saved_rbx:  resq 1
saved_rbp:  resq 1
saved_r12:  resq 1
saved_r13:  resq 1
saved_r14:  resq 1
saved_r15:  resq 1
saved_rflags: resq 1
user_rsp:   resq 1
user_rflags: resq 1
resume_ctx: resq 1
kernel_ret_rip: resq 1

section .text

enter_user_mode:
    mov [rel kernel_ret_rip], rdi
    mov [rel user_rsp], rsi
    mov [rel user_rflags], r8
    mov [rel saved_rbx], rbx
    mov [rel saved_rbp], rbp
    mov [rel saved_r12], r12
    mov [rel saved_r13], r13
    mov [rel saved_r14], r14
    mov [rel saved_r15], r15
    pushfq
    pop qword [rel saved_rflags]

    call sched_user_enter

    lea rsp, [rel user_rsp]
    and rsp, 0xFFFFFFFFFFFFFFF0
    sub rsp, 128

    push qword [rel user_rflags]
    push qword 0
    push qword 0
    push qword 0

    mov rax, rdi
    mov r12, r12
    sysret

kernel_resume:
    cli
    call sched_user_exit
    call paging_switch_kernel

    mov rsp, [rel saved_rsp]
    mov rdi, [rel user_rsp]
    pushfq
    pop rax
    and rax, 0xFFFFFFFE
    or  rax, 2
    push rax

    mov r12, [rel saved_r12]
    mov r13, [rel saved_r13]
    mov r14, [rel saved_r14]
    mov r15, [rel saved_r15]
    mov rbp, [rel saved_rbp]
    mov rbx, [rel saved_rbx]
    popfq
    sysret

resume_user_mode:
    mov [rel kernel_ret_rip], rdi
    mov [rel user_rsp], rsi
    mov [rel user_rflags], r8
    mov [rel saved_rbx], rbx
    mov [rel saved_rbp], rbp
    mov [rel saved_r12], r12
    mov [rel saved_r13], r13
    mov [rel saved_r14], r14
    mov [rel saved_r15], r15
    pushfq
    pop qword [rel saved_rflags]

    call sched_user_enter

    mov rbx, [rel resume_ctx]

    push qword [rbx + 168]
    push qword [rbx + 160]
    push qword [rbx + 152]
    push qword [rbx + 144]
    push qword [rbx + 136]

    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov r15, [rbx + 0]
    mov r14, [rbx + 8]
    mov r13, [rbx + 16]
    mov r12, [rbx + 24]
    mov r11, [rbx + 32]
    mov r10, [rbx + 40]
    mov r9,  [rbx + 48]
    mov r8,  [rbx + 56]
    mov rbp, [rbx + 64]
    mov rdi, [rbx + 72]
    mov rsi, [rbx + 80]
    mov rdx, [rbx + 88]
    mov rcx, [rbx + 96]
    mov rax, [rbx + 112]
    mov rbx, [rbx + 104]

    iretq
