#pragma once

// variable argument handling
#define va_list  __builtin_va_list      // 一個型別，用來宣告「指向當前讀取位置」的游標變數
#define va_start __builtin_va_start     // 初始化游標，讓它指向 fmt 之後第一個可變參數的位置
#define va_end   __builtin_va_end       // Clean up when done
#define va_arg   __builtin_va_arg       // 從游標位置讀取一個 type 型別的值，並將游標前進

void printf(const char *fmt, ...);  