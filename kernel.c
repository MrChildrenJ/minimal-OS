#include "kernel.h"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

// compiler sees extern      -> undefined ref
// linker sees undefined ref -> go to linker script / obj file to find symbol -> fill in
// extern ~= trust me, these exist
extern char __bss[], __bss_end[], __stack_top[];

// == sbicall & putchar ==
// arg0 - arg5: input args
// fid: func ID; eid: extension ID
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;      // Put arg0 into CPU register a0
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    // ecall: S-mode (kernel) -> M-mode (OpenSBI)
    // a2-a7 should be reserved by "callee" who can only modify a0 and a1
    __asm__ __volatile__("ecall"                // triggers a trap into firmware, after ecall, results are stored in:
                        : "=r"(a0), "=r"(a1)    // a0 → error, a1 → return value (written by f/w)
                        : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7) // pass args
                        : "memory");            // Tell compiler: memory may change, don’t optimize around this
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* call Console Putchar (sbi_console_putchar())*/);
}

void* memset(void* buf, char c, size_t n) {
    uint8_t* p = (uint8_t*) buf;
    while (n--)
        *p++ = c;
    return buf;
}

void kernel_main(void) {
    // memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    const char* s = "\n\nHello World!\n";
    for (int i = 0; s[i] != '\0'; i++) {
        putchar(s[i]);
    }

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}

__attribute__((section(".text.boot")))  // place this function in .text.boot
__attribute__((naked))          // No compiler-generated code (prologue/epilogue), we control everything
void boot(void) {               // entry point in linker script
    __asm__ __volatile__(       // __volatile__: don’t optimize
        "mv sp, %[stack_top]\n" // set the stack pointer at the end (highest) address of the stack area
        "j kernel_main\n"       // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // let compiler pass __stack_top into a reg, then pass to inline assembly
                                        // "r" -> put value in a register ([stack_top] is just a name tag)
    );
}