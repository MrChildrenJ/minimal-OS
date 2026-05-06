ENTRY(start)

SECTIONS {
    . = 0x01000000;

    /* machine code */
    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    /* read-only data */
    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    /* data with initial values */
    .data : ALIGN(4) {
        *(.data .data.*);
    }

    /* data that should be zero-filled at startup */
    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16);
        . += 64 * 1024; /* 64KB, smaller than kernel's 128KB */
        __stack_top = .;

       ASSERT(. < 0x01800000, "too large executable");  /* 01800000 - 01000000 = 8 * 16^5 = 8MB */
    }
}