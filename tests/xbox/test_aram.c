/* test_aram.c - host check of ARAM's disc-backed pages and swap file
 * (xbox/src/sdk/ar.c). Built and run by tools/xbox/test_aram.py, which
 * compiles ar.c into this file with the Xbox calls it makes stubbed here:
 * a 16 MB "lazy" region whose decommitted chunks lose their bytes, a disc
 * image made of a hash of the offset, a swap file in memory (writes can be
 * made to fail).
 *
 * Random DMA writes (copies the DVD just read, so disc-backed, and plain
 * data), DMA reads, CPU reads that fault a chunk back in, and reclaims
 * (from the low-memory tick and from a failed commit) are checked against
 * a shadow copy of what ARAM must hold. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARAM_BYTES (16u * 1024 * 1024)
#define LCHUNK (64u * 1024)

/* ---- stubs for ar.c ---- */
static uint8_t* s_mem;                 /* the lazy region */
static uint8_t s_bits[ARAM_BYTES / LCHUNK];
static void (*s_fill)(void* chunk);
static uint32_t (*s_reclaim_fn)(uint32_t, int);
static uint32_t s_frame;
static const uint8_t* s_disc_src;      /* the buffer the DVD "just read" */
static uint32_t s_disc_len, s_disc_off;
static uint8_t* s_swapf;               /* the swap file */
static int s_swap_fail;                /* the next this many writes fail */
static unsigned s_lost_pages, s_writes, s_fills;

static uint8_t img_byte(uint32_t off) {
    uint32_t h = off * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    return (uint8_t)(h >> 13);
}

static int chunk_of(const void* p) { return (int)(((const uint8_t*)p - s_mem) / LCHUNK); }
static int in_region(const void* p) { return (const uint8_t*)p >= s_mem && (const uint8_t*)p < s_mem + ARAM_BYTES; }

/* as xhw_sys.c's lazy_commit_chunk: commit (zeroed), fill, then the bit */
static void commit_chunk(int c) {
    if (s_bits[c]) return;
    memset(s_mem + (size_t)c * LCHUNK, 0, LCHUNK);
    if (s_fill) s_fill(s_mem + (size_t)c * LCHUNK);
    s_bits[c] = 1;
}

typedef struct xhw_mutex { int depth; } xhw_mutex;
typedef struct xhw_event xhw_event;
xhw_mutex* xhw_mutex_create(void) { return (xhw_mutex*)calloc(1, sizeof(xhw_mutex)); }
void xhw_mutex_lock(xhw_mutex* m) { m->depth++; }
void xhw_mutex_unlock(xhw_mutex* m) { m->depth--; }
int xhw_mutex_trylock(xhw_mutex* m) { m->depth++; return 1; }
void xhw_event_signal(xhw_event* e) { (void)e; }
void xhw_logf(const char* fmt, ...) { (void)fmt; }
void xhw_fatal(const char* t, const char* m) { printf("FATAL %s: %s\n", t, m); exit(2); }
void* xhw_reserve_lazy(uintptr_t va, uint32_t bytes) {
    (void)va;
    s_mem = (uint8_t*)malloc(bytes);
    memset(s_mem, 0xEE, bytes);
    return s_mem;
}
void xhw_lazy_set_fill(const void* base, void (*fill)(void* chunk)) { (void)base; s_fill = fill; }
int xhw_lazy_is_committed(const void* p) { return !in_region(p) || s_bits[chunk_of(p)]; }
int xhw_lazy_commit_pages(void* chunk) { (void)chunk; return 1; }
void xhw_lazy_decommit(void* chunk) {
    int c = chunk_of(chunk);
    s_bits[c] = 0;
    memset(chunk, 0xCD, LCHUNK);   /* gone: a refill must put the bytes back */
}
void xhw_commit(const void* p, uint32_t bytes) {
    uintptr_t a, end;
    if (!bytes || !in_region(p)) return;
    end = (uintptr_t)p + bytes;
    for (a = (uintptr_t)p; a < end; a = ((a - (uintptr_t)s_mem) / LCHUNK + 1) * LCHUNK + (uintptr_t)s_mem)
        commit_chunk(chunk_of((const void*)a));
}
uint32_t xhw_mem_free_kb(void) { return 1000; }
void xhw_mem_add_reclaim(uint32_t (*fn)(uint32_t, int), int first) { (void)first; s_reclaim_fn = fn; }
const char* xhw_save_dir(void) { return ""; }
void* xhw_file_create_rw(const char* path) {
    (void)path;
    s_swapf = (uint8_t*)calloc(1, ARAM_BYTES);
    return s_swapf;
}
/* ar.c's swap file since RC1 (kept at 8 MB) and its ARQ worker, which the
 * test doesn't start: no thread, so the copies run on the caller as in
 * release builds (XSDK_ARQ_ASYNC 0) */
void* xhw_file_open_rw_sized(const char* path, uint32_t min_bytes) {
    (void)min_bytes;
    return xhw_file_create_rw(path);
}
xhw_event* xhw_event_create(void) { static int dummy; return (xhw_event*)&dummy; }
int xhw_event_wait(xhw_event* e, uint32_t timeout_ms) { (void)e, (void)timeout_ms; return 0; }
int xhw_thread_start(void (*fn)(void*), void* arg, int priority, uint32_t stack_bytes) {
    (void)fn, (void)arg, (void)priority, (void)stack_bytes;
    return 0;
}
void xhw_sleep_ms(uint32_t ms) { (void)ms; }
void xsdk_dvd_log(void) {}
int xhw_file_read(void* f, uint32_t off, void* dst, uint32_t len) {
    if (off + len > ARAM_BYTES) return 0;
    memcpy(dst, (uint8_t*)f + off, len);
    return 1;
}
int xhw_file_write(void* f, uint32_t off, const void* src, uint32_t len) {
    if (s_swap_fail > 0 || off + len > ARAM_BYTES) {
        s_swap_fail--;
        return 0;
    }
    memcpy((uint8_t*)f + off, src, len);
    s_writes++;
    return 1;
}
int xsdk_dvd_disc_source(const void* p, uint32_t len, uint32_t* image_off) {
    const uint8_t* a = (const uint8_t*)p;
    if (!s_disc_src || a < s_disc_src || a + len > s_disc_src + s_disc_len) return 0;
    *image_off = s_disc_off + (uint32_t)(a - s_disc_src);
    return 1;
}
int xsdk_dvd_image_read(uint32_t off, void* dst, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) ((uint8_t*)dst)[i] = img_byte(off + i);
    s_fills++;
    return 1;
}
unsigned xsdk_frame_count(void) { return s_frame; }

#define main ar_unused_main
#include "ar.c"
#undef main

/* ---- the test ---- */
static uint32_t s_rng = 7;
static uint32_t rnd(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static uint8_t s_shadow[ARAM_BYTES];
static uint8_t s_buf[256 * 1024];

static void dma(uint32_t type, uint8_t* mram, uint32_t aram, uint32_t len) {
    ARQRequest r;
    if (type == 0) ARQPostRequest(&r, 0, 0, 0, (uintptr_t)mram, aram, len, NULL);
    else ARQPostRequest(&r, 0, 1, 0, aram, (uintptr_t)mram, len, NULL);
}

int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 200000, i, bad = 0;
    unsigned writes = 0, reads = 0, cpureads = 0, reclaims = 0, urgent = 0;
    ARInit(NULL, 0);
    memset(s_shadow, 0, sizeof s_shadow);   /* never written: reads back zero */
    for (i = 0; i < n && bad < 10; i++) {
        uint32_t op = rnd() % 100;
        uint32_t aram = 0x4000u + (rnd() % (ARAM_BYTES - 0x4000u - 256 * 1024)) / 32 * 32;
        uint32_t len = 32 + (rnd() % (64 * 1024)) / 32 * 32;
        if (rnd() % 8 == 0) len = 32 + (rnd() % (200 * 1024)) / 32 * 32;
        if (op < 35) {   /* a write: half the time straight off the disc */
            uint32_t k;
            if (rnd() % 2) {
                s_disc_off = (rnd() % (1u << 30)) / 4096 * 4096 + (rnd() % 2 ? 0 : (rnd() % 4096) / 32 * 32);
                for (k = 0; k < len; k++) s_buf[k] = img_byte(s_disc_off + k);
                s_disc_src = s_buf;
                s_disc_len = len;
            } else {
                for (k = 0; k < len; k++) s_buf[k] = (uint8_t)rnd();
                s_disc_src = NULL;
            }
            dma(0, s_buf, aram, len);
            s_disc_src = NULL;
            memcpy(s_shadow + aram, s_buf, len);
            writes++;
        } else if (op < 55) {   /* a DMA read back */
            dma(1, s_buf, aram, len);
            if (memcmp(s_buf, s_shadow + aram, len) != 0) {
                printf("op %d: DMA read at %08x + %u differs\n", i, aram, len);
                bad++;
            }
            reads++;
        } else if (op < 75) {   /* the mixer reads a byte: a fault commits and fills */
            uint32_t a = 0x4000u + rnd() % (ARAM_BYTES - 0x4000u);
            int c = (int)(a / LCHUNK);
            if (!s_bits[c]) commit_chunk(c);
            if (s_mem[a] != s_shadow[a]) {
                printf("op %d: CPU read at %08x: %02x, want %02x\n", i, a, s_mem[a], s_shadow[a]);
                bad++;
            }
            cpureads++;
        } else if (op < 95) {   /* memory pressure */
            int u = rnd() % 4 == 0;
            s_swap_fail = rnd() % 20 == 0;   /* now and then one write fails */
            s_reclaim_fn(64 + rnd() % 2048, u);
            s_swap_fail = 0;
            reclaims++;
            urgent += (unsigned)u;
        } else {
            s_frame += 1 + rnd() % 200;
        }
    }
    /* everything once more, whole */
    for (i = 0; i < (int)(ARAM_BYTES / sizeof s_buf) && bad < 10; i++) {
        uint32_t a = (uint32_t)i * (uint32_t)sizeof s_buf;
        dma(1, s_buf, a, sizeof s_buf);
        if (a >= 0x4000u && memcmp(s_buf, s_shadow + a, sizeof s_buf) != 0) {
            printf("final DMA read at %08x differs\n", a);
            bad++;
        }
    }
    (void)s_lost_pages;
    printf("aram: %d ops (%u writes, %u DMA reads, %u CPU reads, %u reclaims, %u urgent); %u swap writes, %u image "
           "reads; %u KB on the disc, %u KB in the swap file: %s\n",
           n, writes, reads, cpureads, reclaims, urgent, s_writes, s_fills, xsdk_aram_disc_kb(), xsdk_aram_swap_kb(),
           bad ? "FAIL" : "ok");
    return bad != 0;
}
