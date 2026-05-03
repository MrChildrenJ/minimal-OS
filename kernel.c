#include "kernel.h"
#include "common.h"

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


// == Exception Handler ==
__attribute__((naked))          // again, no prologue/epilogue
__attribute__((aligned(4)))
void kernel_entry(void) {       // addr of this func is always in stvec reg
    __asm__ __volatile__(
        "csrw sscratch, sp\n"       // save "value at sp" to sscratch
        "addi sp, sp, -4 * 31\n"    // Allocate space (31 words) for the trap_frame struct
        "sw ra,  4 * 0(sp)\n"       // all general purpose regs except for sp
        "sw gp,  4 * 1(sp)\n"       // ex: save gp to "sp + 4*1"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        "csrr a0, sscratch\n"   // read original sp value from sscratch to a0
        "sw a0, 4 * 30(sp)\n"   // save to "30th slot" of trap_frame

        "mv a0, sp\n"           // store sp's value to a0 reg(1st arg)
        "call handle_trap\n"    // call handle_trap with arg a0, which is a ptr to the trap_frame struct

        "lw ra,  4 * 0(sp)\n"   // recover all regs
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"  // recover sp
        "sret\n"                // Supervisor Return. Return from S-mode trap, CPU jumb back to where exception occured
    );
}

void handle_trap(struct trap_frame *f) {    // original stack pointer
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry);  // (uint32_t) kernel_entry = address of kernel_entry func
    __asm__ __volatile__("unimp");              // causes an illegal_instruction trap = "csrrw x0, cycle, x0"
                                                // = reads and writes the cycle reg into x0. Buy "cycle" is read-only
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