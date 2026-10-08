BITS 16
ORG 0x7C00

bpb:
    jmp short start
    nop
bpb_oem:                db "LUMINAOS"
bpb_bytes_per_sector:   dw 512
bpb_sectors_per_cluster: db 1
bpb_reserved_sectors:   dw 1
bpb_fat_count:          db 2
bpb_root_dir_entries:   dw 224
bpb_total_sectors:      dw 2880
bpb_media_descriptor:   db 0xF0
bpb_sectors_per_fat:    dw 12
bpb_sectors_per_track:  dw 18
bpb_heads:              dw 2
bpb_hidden_sectors:     dd 0
bpb_large_sectors:      dd 0
ebs_drive_number:       db 0
ebs_reserved:           db 0
ebs_signature:          db 0x29
ebs_volume_id:          dd 0x4C554D49
ebs_volume_label:       db "LUMINAOS   "
ebs_system_id:          db "FAT16   "

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    mov ax, 0x0003
    int 0x10

    mov ax, 0x4F02
    mov bx, 0x4143
    int 0x10

    mov si, msg_boot
    call print_string

    mov al, [bpb_fat_count]
    mul word [bpb_sectors_per_fat]
    add ax, [bpb_reserved_sectors]
    mov [root_lba], ax

    mov bx, 0x7E00
    mov cx, 14
    call read_sectors

    mov ax, [root_lba]
    add ax, 14
    mov [data_lba], ax

    mov cx, [bpb_root_dir_entries]
    mov di, 0x7E00
.find:
    push cx
    mov cx, 11
    mov si, kernel_filename
    push di
    repe cmpsb
    pop di
    je .found
    pop cx
    add di, 32
    loop .find

    mov si, msg_not_found
    call print_string
    jmp hang

.found:
    pop cx
    mov ax, [di + 26]
    mov [cluster], ax

    mov ax, [bpb_reserved_sectors]
    mov bx, 0x5000
    mov cx, [bpb_sectors_per_fat]
    call read_sectors

    mov ax, 0x07E0
    mov es, ax
    xor bx, bx

.load:
    push bx
    mov ax, [cluster]
    sub ax, 2
    xor bx, bx
    mov bl, [bpb_sectors_per_cluster]
    mul bx
    add ax, [data_lba]
    mov cx, 1
    pop bx
    push bx
    call read_sectors
    pop bx
    add bx, 512

    mov ax, [cluster]
    shl ax, 1
    mov si, 0x5000
    add si, ax
    mov ax, [si]
    mov [cluster], ax
    cmp ax, 0xFFF8
    jb .load

    mov si, msg_jump
    call print_string

    mov dl, [boot_drive]
    jmp 0x07E0:0x0000

read_sectors:
    pusha
    mov di, ax
    mov si, cx
.loop:
    mov ax, di
    xor dx, dx
    div word [bpb_sectors_per_track]
    inc dx
    mov cx, dx
    xor dx, dx
    div word [bpb_heads]
    mov dh, dl
    mov ch, al
    mov dl, [boot_drive]
    mov ah, 0x02
    mov al, 1
    int 0x13
    jc .error
    add bx, 512
    inc di
    dec si
    jnz .loop
    popa
    ret
.error:
    mov si, msg_error
    call print_string
    jmp hang

print_string:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

hang:
    jmp hang

boot_drive:      db 0
cluster:         dw 0
root_lba:        dw 0
data_lba:        dw 0
kernel_filename: db "KERNEL  BIN"
msg_boot:        db 13,10,"LuminaOS v0.7.1",13,10,0
msg_not_found:   db "KERNEL.BIN not found",13,10,0
msg_error:       db "Disk error",13,10,0
msg_jump:        db "Starting kernel...",13,10,0

times 510 - ($ - $$) db 0
dw 0xAA55
