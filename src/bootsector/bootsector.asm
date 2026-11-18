[BITS 16]
[ORG 0x7c00]

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00

    jmp 0:main

main:
    mov byte [boot_drive], dl

    mov si, welcome_msg
    call print_string

    ; check for extensions

    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13

    jc error

    ; read next stages from disk

    mov cx, 63
    mov di, 0x7E00
    xor bx, bx
    mov eax, 1
    call read_disk
    jc error
    

    ; enable a20
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    test al, 2
    jnz .a20_enabled
    or al, 2
    and al, 0xFE
    out 0x92, al
.a20_enabled:

    xor ebx, ebx
    mov di, 0x5004
    xor bp, bp
.e820_loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150 ; SMAP
    mov dword [es:di + 20], 1 ; force valid acpi 3.0 extended attribute
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc bp
    add di, 24
    test ebx, ebx
    jnz .e820_loop
.e820_done:
    mov [0x5000], bp ; entry count

    ; inform bios of operating mode
    mov ax, 0xEC00
    mov bl, 0x02
    int 0x15

    ; load gdt and jump to protected mode
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry





    


    jmp end




error:
    mov si, error_msg
    call print_string

end:
    cli
    hlt
    jmp end

print_string:
    push ax
    push bx
    mov ah, 0x0E
    xor bh, bh
.next_char:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next_char
.done:
    pop bx
    pop ax
    ret

; cx: num_sectors
; di: offset of buffer
; bx: segment of buffer
; eax: lba
read_disk:
    mov word [DAP.num_sectors], cx
    mov word [DAP.buffer_offset], di
    mov word [DAP.buffer_segment], bx
    mov dword [DAP.lba_low], eax
    mov dl, byte [boot_drive]
    mov ah, 0x42
    mov si, DAP
    int 0x13
    ret


[BITS 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    movzx edx, byte [boot_drive]
    mov ecx, 0x5000
    jmp 0x7E00

boot_drive: db 0


welcome_msg: db "Hello world!", 0
error_msg: db "Error...", 0


align 4
DAP:
.size: db 0x10
.zero: db 0x00
.num_sectors: dw 0x0000
.buffer_offset: dw 0x0000
.buffer_segment: dw 0x0000
.lba_low: dd 0
.lba_high: dd 0

align 8
gdt_start:
dq 0x0000000000000000       ; 0x00: Null descriptor
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit Code (base 0, limit 4GiB, RX)
    dq 0x00CF92000000FFFF       ; 0x10: 32-bit Data (base 0, limit 4GiB, RW)
    dq 0x00009A000000FFFF       ; 0x18: 16-bit Code (base 0, limit 64KiB, RX - for VBE thunk)
    dq 0x000092000000FFFF       ; 0x20: 16-bit Data (base 0, limit 64KiB, RW - for VBE thunk)
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start




times 446 - ($ - $$) nop

; partition tables


; Partition 1: FAT32 LBA (0x0C), Bootable (0x80), 1 MiB -> 21 MiB
db 0x80                         ; Bootable flag
db 0x20, 0x21, 0x00             ; Starting CHS (H=32, S=33, C=0 -> LBA 2048)
db 0x0C                         ; Partition type: W95 FAT32 (LBA)
db 0xDF, 0x13, 0x02             ; Ending CHS (LBA 43007)
dd 2048                         ; Starting LBA (1 MiB / 512)
dd 40960                        ; Sector count (20 MiB / 512)

; Partition 2: Linux/ext2 (0x83), Non-bootable (0x00), 21 MiB -> 64 MiB
db 0x00                         ; Non-bootable
db 0xDF, 0x14, 0x02             ; Starting CHS (LBA 43008)
db 0x83                         ; Partition type: Linux native (ext2)
db 0x1F, 0x20, 0x08             ; Ending CHS (LBA 131071)
dd 43008                        ; Starting LBA (21 MiB / 512)
dd 88064                        ; Sector count (43 MiB / 512)

; unused
times 32 db 0


dw 0xAA55