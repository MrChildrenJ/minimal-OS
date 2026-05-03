#pragma once

typedef int bool;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef uint32_t size_t;
typedef uint32_t paddr_t;               // physical memory addresses
typedef uint32_t vaddr_t;               // virtual memory addresses = uintptr_t in stdlib

#define true  1
#define false 0
#define NULL  ((void *) 0)
#define align_up(value, align)   __builtin_align_up(value, align)   // align must be power of 2
#define is_aligned(value, align) __builtin_is_aligned(value, align) // checks if value is a multiple of align
#define offsetof(type, member)   __builtin_offsetof(type, member)   // calculates the offset in bytes of a specific member from the beginning of its parent structure or union

// variable argument handling
#define va_list  __builtin_va_list      // 一個型別，用來宣告「指向當前讀取位置」的游標變數
#define va_start __builtin_va_start     // 初始化游標，讓它指向 fmt 之後第一個可變參數的位置
#define va_end   __builtin_va_end       // Clean up when done
#define va_arg   __builtin_va_arg       // 從游標位置讀取一個 type 型別的值，並將游標前進

void *memset(void *buf, char c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
char *strcpy(char *dst, const char *src);
int strcmp(const char *s1, const char *s2);
void printf(const char *fmt, ...);  