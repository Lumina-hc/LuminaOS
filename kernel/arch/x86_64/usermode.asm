BITS 64
section .text

global enter_user_mode
global kernel_resume
global resume_user_mode

extern sched_user_enter
extern sched_user_exit

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

section .text

enter_user_mode:
    mov [rel saved_rsp], rsp
    mov [rel saved_rbx], rbx
    mov [rel saved_rbp], rbp
    mov [rel saved_r12], r12
    mov [rel saved_r13], r13
    mov [rel saved_r14], r14
    mov [rel saved_r15], r15
    pushfq
    pop qword [rel saved_rflags]

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

; void resume_user_mode(isr_regs_t* ctx)
; Re-enters a previously suspended user process. Saves the kernel
; continuation exactly like enter_user_mode so a later kernel_resume
; unwinds back to the caller.
resume_user_mode:
    mov [rel saved_rsp], rsp
    mov [rel saved_rbx], rbx
    mov [rel saved_rbp], rbp
    mov [rel saved_r12], r12
    mov [rel saved_r13], r13
    mov [rel saved_r14], r14
    mov [rel saved_r15], r15
    pushfq
    pop qword [rel saved_rflags]

    mov [rel resume_ctx], rdi
    call sched_user_enter

    mov rsp, [rel resume_ctx]

    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, [rsp + 112]
    mov r15, [rsp + 0]
    mov r14, [rsp + 8]
    mov r13, [rsp + 16]
    mov r12, [rsp + 24]
    mov r11, [rsp + 32]
    mov r10, [rsp + 40]
    mov r9,  [rsp + 48]
    mov r8,  [rsp + 56]
    mov rbp, [rsp + 64]
    mov rdi, [rsp + 72]
    mov rsi, [rsp + 80]
    mov rdx, [rsp + 88]
    mov rcx, [rsp + 96]
    mov rbx, [rsp + 104]

    lea rsp, [rsp + 136]
    iretq
