#include "common.h"

void putchar(char ch);

void printf(const char* fmt, ...) {
    va_list vargs;              // a list of arguments after "fmt"
    va_start(vargs, fmt);    // The cursor points to the first argument after fmt

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;              // skip current '%'
            switch (*fmt) {     // read the next character
                case '\0':      // '%' at the end of the format string
                    putchar('%');   // print the "skipped" % 
                    goto end;
                case '%':       // print '%'
                    putchar('%');
                    break;
                case 's': {     // print a NULL-terminated string.
                    const char *s = va_arg(vargs, const char *);    // va_arg: read a specific type (char*), then advance
                    while (*s) {
                        putchar(*s);
                        s++;
                    }
                    break;
                }
                case 'd': { // Print an integer in decimal.
                    int value = va_arg(vargs, int);     // read a int, then advance
                    unsigned magnitude = value;         // if value = INT_MIN(-2^31), mag = 2^31 (uint: 0 to 2^32-1)
                    if (value < 0) {
                        putchar('-');
                        magnitude = -magnitude;         // convert mag back to -2^31 --unsigned--> 2^31
                    }

                    unsigned divisor = 1;
                    while (magnitude / divisor > 9)
                        divisor *= 10;

                    while (divisor > 0) {
                        putchar('0' + magnitude / divisor);
                        magnitude %= divisor;
                        divisor /= 10;
                    }

                    break;
                }
                case 'u': { // Print an unsigned integer in decimal.
                    unsigned value = va_arg(vargs, unsigned);
                    unsigned divisor = 1;
                    while (value / divisor > 9)
                        divisor *= 10;
                    while (divisor > 0) {
                        putchar('0' + value / divisor);
                        value %= divisor;
                        divisor /= 10;
                    }
                    break;
                }
                case 'x': { // Print an integer in hexadecimal.
                    putchar('0');
                    putchar('x');
                    // printf("0x");    with overhead
                    unsigned value = va_arg(vargs, unsigned);
                    for (int i = 7; i >= 0; i--) {
                        unsigned nibble = (value >> (i * 4)) & 0xf; // nibble: 0 - 15
                        putchar("0123456789abcdef"[nibble]);
                    }
                }
            }
        } else {
            putchar(*fmt);
        }

        fmt++;
    }

end:
    va_end(vargs);
}

// == Memory Operation ==
void* memcpy(void* dst, const void* src, size_t n) {
    // Transform dst & src to d & s, becos void* can't be directly dereferenced or used in pointer arithmetic
    uint8_t* d = (uint8_t*) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void* memset(void* buf, char c, size_t n) {
    uint8_t* p = (uint8_t*) buf;
    while (n--)
        *p++ = c;
    return buf;
}


// == String Operation ==
char *strcpy(char* dst, const char* src) {
    char* d = dst;
    while (*src)        // keep copying even if src is longer than dst, NEVER use it in production
        *d++ = *src++;  // use strlcpy
    *d = '\0';          // strncpy doesn't guarantee ending with \0, and uses \0 for padding remaining space
    return dst;
}

int strcmp(const char* s1, const char* s2) {    // equal -> return 0 (counter-intuitive...)
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    // while (*s1 && *s2) {
    //     if (*s1 != *s2)
    //         break;
    //     s1++;
    //     s2++;
    // }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}