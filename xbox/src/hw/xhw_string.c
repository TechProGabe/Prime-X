/* xhw_string.c - memcpy, memmove, memset and memcmp for the whole program.
 *
 * nxdk's pdclib implements these as byte loops. Everything goes through
 * them (the game, HSD, the GX layer, pbkit, the mixer), and on hardware they
 * took about 40% of a VS match's frame. These move and compare 32 bits at a
 * time; objects in the link come before libpdclib, so these definitions win.
 * Compiled with -fno-builtin (CMakeLists.txt) so clang can't turn the loops
 * back into calls to themselves. */
#include <stddef.h>
#include <stdint.h>

/* Big copies, in blocks: the console's Pentium III (Coppermine) has no
 * hardware prefetcher, so rep movsl waits on every 32-byte line of a source
 * that isn't cached (the display lists' templates, round hw7: memcpy 5-9%
 * of the CPU). The next block's lines are prefetched before the current
 * block is copied, so their misses overlap the copy. */
#ifndef XHW_COPY_PREFETCH
#define XHW_COPY_PREFETCH 1   /* 0: plain rep movsl */
#endif
#define COPY_BLOCK 1024
#define COPY_BIG (2 * COPY_BLOCK)

static void prefetch_block(const char* p) {
    int i;
    for (i = 0; i < COPY_BLOCK; i += 32) __builtin_prefetch(p + i, 0, 3);
}

/* Small copies (most calls: a vertex, a struct, a quad's corners): moves
 * in a loop. Each rep movs starts in microcode (tens of cycles on the P6
 * core whatever the count), and the path below runs three of them: round
 * hw9's Main Plaza had 8% of the CPU on the small-copy rep instructions. */
#ifndef XHW_COPY_SMALL
#define XHW_COPY_SMALL 64   /* 0: rep movs for every size, as before */
#endif
/* Copies from COPY_MID bytes up to COPY_BIG get their source prefetched
 * whole before the rep movsl: its line misses overlap instead of coming one
 * by one (the dynamic display lists' batches, a few hundred bytes to 2 KB
 * from templates that aren't cached). */
#define COPY_MID 256

void* memcpy(void* dst, const void* src, size_t n) {
    void* ret = dst;
    size_t head, words;
    if (n < XHW_COPY_SMALL) {
        unsigned char* d = (unsigned char*)dst;
        const unsigned char* s = (const unsigned char*)src;
        while (n >= 8) {
            uint32_t a, b;
            __builtin_memcpy(&a, s, 4);
            __builtin_memcpy(&b, s + 4, 4);
            __builtin_memcpy(d, &a, 4);
            __builtin_memcpy(d + 4, &b, 4);
            d += 8;
            s += 8;
            n -= 8;
        }
        if (n >= 4) {
            uint32_t a;
            __builtin_memcpy(&a, s, 4);
            __builtin_memcpy(d, &a, 4);
            d += 4;
            s += 4;
            n -= 4;
        }
        while (n--) *d++ = *s++;
        return ret;
    }
    if (n >= 16) {
        head = (size_t)(-(uintptr_t)dst & 3);   /* align the destination */
        n -= head;
        words = n >> 2;
        n &= 3;
        if (head) __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(head) : : "memory");
        if (XHW_COPY_PREFETCH && words >= COPY_BIG / 4) {
            prefetch_block((const char*)src);
            while (words >= 2 * (COPY_BLOCK / 4)) {
                size_t block = COPY_BLOCK / 4;
                prefetch_block((const char*)src + COPY_BLOCK);
                __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(block) : : "memory");
                words -= COPY_BLOCK / 4;
            }
        } else if (XHW_COPY_PREFETCH && words >= COPY_MID / 4) {
            const char* p = (const char*)src;
            size_t i;
            for (i = 0; i < words * 4; i += 32) __builtin_prefetch(p + i, 0, 3);
        }
        __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(words) : : "memory");
    }
    if (n) __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d <= s || d >= s + n) return memcpy(dst, src, n);
    /* overlapping, destination above the source: copy backwards */
    d += n;
    s += n;
    while (n >= 4 && ((uintptr_t)d & 3)) {
        *--d = *--s;
        n--;
    }
    while (n >= 4) {
        uint32_t w;
        d -= 4;
        s -= 4;
        __builtin_memcpy(&w, s, 4);
        __builtin_memcpy(d, &w, 4);
        n -= 4;
    }
    while (n--) *--d = *--s;
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    void* ret = dst;
    uint32_t v = (uint8_t)c * 0x01010101u;
    if (n >= 16) {
        size_t head = (size_t)(-(uintptr_t)dst & 3), words;
        n -= head;
        words = n >> 2;
        n &= 3;
        if (head) __asm__ volatile("rep stosb" : "+D"(dst), "+c"(head) : "a"(v) : "memory");
        __asm__ volatile("rep stosl" : "+D"(dst), "+c"(words) : "a"(v) : "memory");
    } else {
        /* small: stores, not a rep start (as memcpy's small path) */
        unsigned char* d = (unsigned char*)dst;
        while (n >= 4) {
            __builtin_memcpy(d, &v, 4);
            d += 4;
            n -= 4;
        }
        while (n--) *d++ = (unsigned char)v;
        return ret;
    }
    if (n) __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(v) : "memory");
    return ret;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* p = (const unsigned char*)a;
    const unsigned char* q = (const unsigned char*)b;
    while (n >= 4) {
        uint32_t x, y;
        __builtin_memcpy(&x, p, 4);
        __builtin_memcpy(&y, q, 4);
        if (x != y) break;   /* the byte loop below finds which byte differs */
        p += 4;
        q += 4;
        n -= 4;
    }
    while (n--) {
        if (*p != *q) return *p - *q;
        p++;
        q++;
    }
    return 0;
}
