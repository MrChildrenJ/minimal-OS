#pragma once
#include "common.h"

#define PROCS_MAX 8       // Maximum number of processes
#define PROC_UNUSED   0   // Unused process control structure
#define PROC_RUNNABLE 1   // Runnable process

// == Page Table ==
#define SATP_SV32 (1u << 31)    // single bit in the satp reg, which indicates "enable paging in Sv32 mode"
#define PAGE_V    (1 << 0)      // "Valid" bit (entry is enabled)
#define PAGE_R    (1 << 1)      // Readable
#define PAGE_W    (1 << 2)      // Writable
#define PAGE_X    (1 << 3)      // Executable
#define PAGE_U    (1 << 4)      // User (accessible in user mode)

// == User Mode ==
// The base virtual address of an application image. This needs to match the starting address defined in user.ld
#define USER_BASE 0x1000000
// SPIE: Supervisor Previous Interrupt Enable
#define SSTATUS_SPIE (1 << 5)

// == Process ==
struct process {
    int pid;                // process ID
    int state;              // process state: PROC_UNUSED or PROC_RUNNABLE
    vaddr_t sp;             // Stack pointer
    uint32_t* page_table;   // ptr to the first-level page table.
    uint8_t stack[8192];    // Kernel stack, contains saved CPU regs, return addr (where it was called from), and local variables
};

// sbi: interface between kernel and firmware
struct sbiret {
    long error;
    long value;
};

/*
/ Prints where the panic occurred, then enters an infinite loop to halt processing
/ Why macro i/o func? __FILE__ and __LINE__ must expand at the call site (where it's called)
/ Macro expands at the call site during "preprocessing"
/ if decl func, FILE & LINE always points to panic()'s definition
/ while (1) {} makes the compiler aware that execution never continues past PANIC(), enabling better dead code analysis
/ ##__VA_ARGS__: GCC/Clang extension, defining macros that "accept a variable number of arguments"
/                ## removes the preceding , when the variable arguments are empty
                 Ex: PANIC("oops!") can also successfully compile
*/
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)

// == Exception Handler ==
struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));

#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid);
void putchar(char ch);

void yield(void);
paddr_t alloc_pages(uint32_t n);

void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags);