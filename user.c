#include "user.h"

extern char __stack_top[];  // linker symbol is an addr itself, not a ptr

__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;);
}

void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}

int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0, 0);
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

int syscall(int sysno, int arg0, int arg1, int arg2) {
    register int a0 __asm__("a0") = arg0;
    register int a1 __asm__("a1") = arg1;
    register int a2 __asm__("a2") = arg2;
    register int a3 __asm__("a3") = sysno;

    // ecall: delegate processing to the kernel by calling exception handler, then transfer control to kernel
    // a0: return value from kernel
    __asm__ __volatile__("ecall"
                         : "=r"(a0)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                         : "memory");

    return a0;
}

// == File System ==
int readfile(const char *filename, char *buf, int len) {
    return syscall(SYS_READFILE, (int) filename, (int) buf, len);
}

int writefile(const char *filename, const char *buf, int len) {
    return syscall(SYS_WRITEFILE, (int) filename, (int) buf, len);
}