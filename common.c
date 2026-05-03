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