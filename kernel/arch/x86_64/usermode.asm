BITS 64
section .text

global enter_user_mode
global kernel_resume
global resume_user_mode
global sysret_asm

extern sched_user_enter
extern sched_user_exit

section .bss
align 16
saved_rsp:    resq 1
saved_rbx:    resq 1
saved_rbp:    resq 1
saved_r12:    resq 1
saved_r13:    resq 1
saved_r14:    resq 1
saved_r15:    resq 1
saved_rflags: resq 1
user_ctx:     resq 1

section .text

enter_user_mode:
    ; args: rdi = rip, rsi = rsp, r8 = rflags
    mov [rel saved_rsp], rsp
    mov [rel saved_rbx], rbx
    mov [rel saved_rbp], rbp
    mov [rel saved_r12], r12
    mov [rel saved_r13], r13
    mov [rel saved_r14], r14
    mov [rel saved_r15], r15
    pushfq
    pop qword [rel saved_rflags]

    mov r11, rdi
    mov r10, rsi
    mov r9,  r8

    call sched_user_enter

    swapgs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rbp, r9
    lea rsp, [rel user_ctx + 1]

    push qword 0x202
    push rbp
    push qword 0x203
    push r10
    push r11

    swapgs
    retfq

kernel_resume:
    cli
    call sched_user_exit

    mov rsp, [rel saved_rsp]
    push qword [rel saved_rflags]
    popfq

    mov rbx, [rel saved_rbx]
    mov rbp, [rel saved_rbp]
    mov r12, [rel saved_r12]
    mov r13, [rel saved_r13]
    mov r14, [rel saved_r14]
    mov r15, [rel saved_r15]
    ret

resume_user_mode:
    ; arg: rdi = pointer to isr_regs_t
    mov [rel saved_rsp], rsp
    mov [rel saved_rbx], rbx
    mov [rel saved_rbp], rbp
    mov [rel saved_r12], r12
    mov [rel saved_r13], r13
    mov [rel saved_r14], r14
    mov [rel saved_r15], r15
    pushfq
    pop qword [rel saved_rflags]

    mov rsi, rdi
    call sched_user_enter

    mov rax, [rsi + 112]
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov r11, [rsi + 32]
    mov r10, [rsi + 40]
    mov r9,  [rsi + 48]
    mov r8,  [rsi + 56]
    mov rbp, [rsi + 64]
    mov rdi, [rsi + 72]
    mov rsi, [rsi + 80]
    mov rdx, [rsi + 88]
    mov rcx, [rsi + 96]
    mov rbx, [rsi + 104]

    mov rbp, [rsi + 168]

    swapgs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rsp, [rsi + 160]

    lea rbp, [rsp + 56]

    push qword [rsi + 168]
    push qword [rsi + 152]
    push qword [rsi + 144]
    push qword [rsi + 136]

    swapgs
    retfq

sysret_asm:
    cli
    call sched_user_enter
    mov rax, [rel saved_rflags]
    and rax, 0xFFFFFED8
    or  rax, 0x2
    mov [rel saved_rflags], rax
    jmp kernel_resume
