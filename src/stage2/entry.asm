[BITS 32]

section .text.entry alloc exec write
global _start
global bios_int
extern stage2_main
extern __bss_start
extern __bss_end

_start:
    ; store memory map and boot drive
    push edx
    push ecx

    ; zero out bss
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call stage2_main

.halt:
    cli
    hlt
    jmp .halt


; ------------------------------------------------------------------------------
; void bios_int(uint8_t int_num, bios_regs_t *regs)
; cdecl stack: [esp+4] = int_num, [esp+8] = regs pointer
; ------------------------------------------------------------------------------
bios_int:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    cli

    ; self modify the int 0x00 opcode byte with the requested interrupt number
    mov al, [ebp + 8]
    mov [.int_opcode + 1], al

    ; save 32 bit stack pointer, gdtr, idtr
    mov [.saved_esp], esp
    sgdt [.saved_gdtr]
    sidt [.saved_idtr]

    ; copy input regs into low memory struct so real mode can see them
    mov esi, [ebp + 12]
    mov edi, .regs_scratch
    mov ecx, 34 ; sizeof(bios_regs_t)
    rep movsb

    jmp 0x18:.pm16

[BITS 16]

.pm16:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and eax, ~1
    mov cr0, eax


    jmp 0x0000:.rm16

.rm16:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00

    lidt [.rm_idtr]

    ; load input registers
    mov ax, [.regs_scratch + 28]
    mov ds, ax
    mov ax, [.regs_scratch + 30]
    mov es, ax

    mov eax, [cs:.regs_scratch + 0]
    mov ebx, [cs:.regs_scratch + 4]
    mov ecx, [cs:.regs_scratch + 8]
    mov edx, [cs:.regs_scratch + 12]
    mov esi, [cs:.regs_scratch + 16]
    mov edi, [cs:.regs_scratch + 20]
    mov ebp, [cs:.regs_scratch + 24]

    sti   

.int_opcode:
    int 0x00 ; byte patched in at runtime
    cli
    mov [cs:.regs_scratch + 0], eax
    mov [cs:.regs_scratch + 4], ebx
    mov [cs:.regs_scratch + 8], ecx
    mov [cs:.regs_scratch + 12], edx
    mov [cs:.regs_scratch + 16], esi
    mov [cs:.regs_scratch + 20], edi
    mov [cs:.regs_scratch + 24], ebp

    mov ax, ds
    mov [cs:.regs_scratch + 28], ax
    mov ax, es
    mov [cs:.regs_scratch + 30], ax
    pushf
    pop ax
    mov [cs:.regs_scratch + 32], ax

    ; Explicitly zero DS before loading GDTR/IDTR and switching back to Protected Mode
    xor ax, ax
    mov ds, ax

    lgdt [.saved_gdtr]
    lidt [.saved_idtr]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:.pm32

[BITS 32]
.pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, [.saved_esp]

    ; Copy 34 bytes (17 words) from .regs_scratch back to the C `regs` struct pointer
    mov esi, .regs_scratch
    mov edi, [esp + 24]     ; Adjust offset to match your pushed registers (e.g. pushad = +40, 4 regs = +24)
    mov ecx, 17
    cld
    rep movsw

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; low mem scratch storage
align 4
.saved_esp:    dd 0
.saved_gdtr:   dw 0
               dd 0
.saved_idtr:   dw 0
               dd 0
.rm_idtr:      dw 0x03FF
               dd 0x00000000
.regs_scratch: times 34 db 0