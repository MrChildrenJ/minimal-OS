#pragma once
#include "common.h"

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