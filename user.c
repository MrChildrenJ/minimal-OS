#include "user.h"

extern char __stack_top[];  // linker symbol is an addr itself, not a ptr

__attribute__((noreturn)) void exit(void) {
    for (;;);
}

void putchar(char ch) {
    /* TODO */
}

/* Works with user.ld's KEEP(*(.text.start)) to ensure start is placed at the very beginning of .text (i.e., 0x1000000)
   so the program counter starts executing start from the base address" */
__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    // in kernel: memset(__bss, 0, (size_t) __bss_end - (size_t) __bss); thus we don't need to clean ourselves
    __asm__ __volatile__(
        "mv sp, %[stack_top] \n"    // set sp to top of stack
        "call main           \n"    // call user's main
        "call exit           \n"
        :: [stack_top] "r" (__stack_top)
    );
}