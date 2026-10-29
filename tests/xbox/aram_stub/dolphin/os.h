/* host build of ar.c: interrupts are a no-op lock, a panic exits */
#ifndef ARAM_STUB_OS_H
#define ARAM_STUB_OS_H
#include <stdio.h>
#include <stdlib.h>
#include <dolphin/types.h>
static inline BOOL OSDisableInterrupts(void) { return 1; }
static inline BOOL OSRestoreInterrupts(BOOL l) { return l; }
#define OSPanic(f, l, msg) (printf("panic %s:%d %s\n", f, l, msg), exit(3))
#endif
