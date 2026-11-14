ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
PHDRS {
    flash PT_LOAD FLAGS(5);
    ramcode PT_LOAD FLAGS(5);
    ramdata PT_LOAD FLAGS(6);
}
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH :flash
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH :flash
    .ARM.extab : { *(.ARM.extab*) } > FLASH :flash
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH :flash
    .ramfunc : {
        . = ALIGN(4); _sdata = .;
        *(.RamFunc*) . = ALIGN(4);
    } > RAM AT> FLASH :ramcode
    .data : {
        *(.data*)
        . = ALIGN(4); _edata = .;
    } > RAM AT> FLASH :ramdata
    _sidata = LOADADDR(.ramfunc);
    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4); _ebss = .;
    } > RAM :ramdata
    .stack (NOLOAD) : { . = ALIGN(8); . += 0x4000; } > RAM :NONE
    /DISCARD/ : { *(.note*) *(.comment*) }
}
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= 0x08010000, "Bootloader exceeds 64 KiB")
