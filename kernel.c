#include "kernel.h"
#include "common.h"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

// compiler sees extern      -> undefined ref
// linker sees undefined ref -> go to linker script / obj file to find symbol -> fill in
// extern ~= trust me, these exist
extern char __bss[], __bss_end[], __stack_top[];
extern char __free_ram[], __free_ram_end[];


// == Context Switch ==
__attribute__((naked)) void switch_context(uint32_t* prev_sp, uint32_t* next_sp) {
    __asm__ __volatile__(
        // Save callee-saved registers onto the current process's stack.            sp = 1024
        "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers    sp - 13 * 4 = 972
        "sw ra,  0  * 4(sp)\n"   // Save callee-saved registers only
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"  // 直接壓在 process 自己的 kernel stack 上，用 sp 指向它

        // Switch the stack pointer.
        "sw sp, (a0)\n"         // *prev_sp = sp;                                   *prev_sp = 972                    
        "lw sp, (a1)\n"         // Switch stack pointer (sp) here                   sp = value in "a1" reg
        
        // Restore callee-saved registers from the next process's stack.    
        "lw ra,  0  * 4(sp)\n"  // Restore callee-saved registers only
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"  // We've popped 13 4-byte registers from the stack
        "ret\n"
    );
}

struct process procs[PROCS_MAX]; // All process control structures

struct process* create_process(uint32_t pc) {
    // Find an unused process control structure.
    struct process* curr = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            curr = &procs[i];
            break;
        }
    }

    if (!curr)
        PANIC("no free process slots");

    // Stack callee-saved registers. These reg values will be restored in the 1st context switch in switch_context
    // / 需要對這個地址做 *--sp，必須是指標才能 dereference
    uint32_t* sp = (uint32_t*) &curr->stack[sizeof(curr->stack)];   // uint8_t* -> uint32_t*

    for (int i = 11; i >= 0; i--) {
        *--sp = 0;  // set s0 to s11 as zero
    }

    *--sp = pc;                     // ra   save the value of prog counter into "ra"! (for return)

    // Initialize fields.
    curr->pid = i + 1;              // procs: 1 ~ 8
    curr->state = PROC_RUNNABLE;
    curr->sp = (uint32_t) sp;       // store sp into struct, only needs value, doesn't need ptr semantic
    return curr;
}

// == Context Switch Testing ==
void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // do nothing; nop = no operation
}

struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        switch_context(&proc_a->sp, &proc_b->sp);   // args are addr to sp of two processes
        delay();
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        switch_context(&proc_b->sp, &proc_a->sp);
        delay();
    }
}

// == Memory Allocation
paddr_t alloc_pages(uint32_t n) {                       // allocate n pages; return "physical" addr of the start of new block
    static paddr_t next_paddr = (paddr_t) __free_ram;   // static variable
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE); // fill with zeroes
    return paddr;
}


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

    WRITE_CSR(stvec, (uint32_t) kernel_entry);              // register trap handler

    proc_a = create_process((uint32_t) proc_a_entry);   // arg: program counter, then set to sp
    proc_b = create_process((uint32_t) proc_b_entry);
    proc_a_entry();

    PANIC("booted!");
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