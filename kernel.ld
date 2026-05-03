/* Tell (GNU) linker how to combine compiled obj files into final executable/binary. */

ENTRY(boot)                     /* to reference "boot" which is usually in assembly(.S/.s) */

SECTIONS {
    . = 0x80200000;             /* on RISC-V/QEMU (virt machine), this is where OpenSBI hands off execution to the kernel */

    .text :{                    /* Code section */
        KEEP(*(.text.boot));    /* forces the boot code to appear first; KEEP: not GC'd by linker even no reference */
        *(.text .text.*);       /* Then all other .text sections follow. */
    }

    .rodata : ALIGN(4) {        /* rodata: read-only; 4-bytes aligned */
        *(.rodata .rodata.*);   /* string literals, const globals */
    }

    .data : ALIGN(4) {          /* Initialized global/static variables */
        *(.data .data.*);
    }

    .bss : ALIGN(4) {           /* Uninitialized globals (zeroed at runtime) */
        __bss = .;              /* .bss start address = . */
        *(.bss .bss.* .sbss .sbss.*);   /* collect all un-init variables; .sbss = small bss(for optimisation) */
        __bss_end = .;          /* .bss end address */
    }

                                /* @ boot
                                memset(__bss, 0, __bss_end - __bss);     void *memset(void *ptr, int value_to_be_set, size_t num_of_bytes);
                                */

    . = ALIGN(4);
    . += 128 * 1024;            /* 128KB (for kernel stack) */
    __stack_top = .;            /* grow downwardly */
}