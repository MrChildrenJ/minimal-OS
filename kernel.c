typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

// compiler sees extern      -> undefined ref
// linker sees undefined ref -> go to linker script / obj file to find symbol -> fill in
// extern ~= trust me, these exist
extern char __bss[], __bss_end[], __stack_top[];

void* memset(void* buf, char c, size_t n) {
    uint8_t* p = (uint8_t*) buf;
    while (n--)
        *p++ = c;
    return buf;
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    
    for (;;);
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