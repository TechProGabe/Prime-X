/* Force-included first into every xbox/src/sdk unit (xbox/CMakeLists.txt).
 * The SDK compiles on nxdk's own triple; this only adds what aurora's
 * headers and the SDK files expect from a hosted C library. */
#ifndef XSDK_PRELUDE_H
#define XSDK_PRELUDE_H

/* aurora's dolphin/os.h uses va_list without including <stdarg.h> */
#include <stdarg.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#define M_SQRT2 1.41421356237309504880
#endif

/* memcpy & co. as builtins despite -ffreestanding (xbox/include/xbuiltin.h) */
#include "../xbuiltin.h"

/* pdclib's stdout and stderr are dead handles on nxdk: printf-family output
 * goes to the Xbox log instead (xbox/src/sdk/log.c defines these). */
#include <stdio.h>
#ifndef XSDK_NO_STDIO_RENAME
int xsdk_printf(const char* fmt, ...);
int xsdk_vprintf(const char* fmt, va_list ap);
int xsdk_fprintf(FILE* f, const char* fmt, ...);
int xsdk_vfprintf(FILE* f, const char* fmt, va_list ap);
int xsdk_puts(const char* s);
int xsdk_fputs(const char* s, FILE* f);
#define printf xsdk_printf
#define vprintf xsdk_vprintf
#define fprintf xsdk_fprintf
#define vfprintf xsdk_vfprintf
#define puts xsdk_puts
#define fputs xsdk_fputs
#endif

#endif
