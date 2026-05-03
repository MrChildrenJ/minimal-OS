#pragma once

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