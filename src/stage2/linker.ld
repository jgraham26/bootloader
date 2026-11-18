OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
    . = 0x7E00;

    .text : ALIGN(4)
    {
        *(.text.entry)
        *(.text*)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata*)
    }

    .data : ALIGN(4)
    {
        *(.data*)
    }

    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }
}